"""MCP orchestration uses synthetic host callbacks and no real application."""
import json
import http.client
import io
import os
import queue
import sys
import threading
import time
import unittest
if os.environ.get('IIAGENT_COMMAND_MODULE_DIR'):
    sys.path.insert(0, os.environ['IIAGENT_COMMAND_MODULE_DIR'])
import iiagent_application_commands as c
from iiagent_application_mcp import McpCommandServer, make_http_server, serve_stdio

class Backend:
    def authenticate(self, credential):
        return 'alice' if credential == 'good' else None
    def describe(self, principal):
        tool = c.ApplicationCommandDescriptor(); tool.name = 'doc.rename'; tool.target_id = 'doc'
        tool.input_schema = '{"type":"object","properties":{"name":{"type":"string"}},"required":["name"]}'
        return [tool]
    def execute(self, principal, invocation, cancel, progress):
        progress(1, 2, 'working')
        if json.loads(invocation.arguments_json)['name'] == 'wait':
            while not cancel.cancelled:
                time.sleep(.005)
        result = c.ApplicationCommandResult(); result.output_json = '{"ok":true}'
        result.cancelled = cancel.cancelled
        return result

class McpServerTests(unittest.TestCase):
    def setUp(self): self.server = McpCommandServer(Backend())
    def tearDown(self): self.server.close()
    def call(self, message, credential='good'):
        events = queue.Queue()
        self.server.submit(json.dumps(message), credential, events.put)
        return events
    def test_authorization_schema_and_progress(self):
        rejected = self.call({'jsonrpc':'2.0','id':1,'method':'tools/list'}, 'bad')
        self.assertIn('error', json.loads(rejected.get(timeout=3)))
        listed = self.call({'jsonrpc':'2.0','id':2,'method':'tools/list'})
        self.assertEqual(json.loads(listed.get(timeout=3))['result']['tools'][0]['name'], 'doc.rename')
        bad = self.call({'jsonrpc':'2.0','id':3,'method':'tools/call','params':{'name':'doc.rename','arguments':{}}})
        self.assertTrue(json.loads(bad.get(timeout=3))['result']['isError'])
        messages = self.call({'jsonrpc':'2.0','id':4,'method':'tools/call','params':{'name':'doc.rename','arguments':{'name':'ok'},'_meta':{'progressToken':'t'}}})
        first=json.loads(messages.get(timeout=3)); last=json.loads(messages.get(timeout=3))
        self.assertEqual(first['method'], 'notifications/progress')
        self.assertEqual(last['result']['structuredContent'], {'ok':True})
    def test_cooperative_cancellation(self):
        events = self.call({'jsonrpc':'2.0','id':'running','method':'tools/call','params':{'name':'doc.rename','arguments':{'name':'wait'},'_meta':{'progressToken':'t'}}})
        self.assertEqual(json.loads(events.get(timeout=3))['method'],'notifications/progress')
        self.call({'jsonrpc':'2.0','method':'notifications/cancelled','params':{'requestId':'running'}})
        with self.assertRaises(queue.Empty): events.get(timeout=.2)

    def test_cancel_is_scoped_to_authenticated_principal(self):
        class TwoPrincipals(Backend):
            def authenticate(self, credential):
                return {'good':'alice', 'other':'bob'}.get(credential)
            def execute(self, principal, invocation, cancel, progress):
                self.flag = cancel
                return super().execute(principal, invocation, cancel, progress)
        backend = TwoPrincipals(); server = McpCommandServer(backend); events = queue.Queue()
        try:
            server.submit(json.dumps({'jsonrpc':'2.0','id':'same-id','method':'tools/call',
                'params':{'name':'doc.rename','arguments':{'name':'wait'},'_meta':{'progressToken':'t'}}}),
                'good', events.put)
            self.assertEqual(json.loads(events.get(timeout=3))['method'], 'notifications/progress')
            notice = json.dumps({'jsonrpc':'2.0','method':'notifications/cancelled','params':{'requestId':'same-id'}})
            server.submit(notice, 'other', events.put)
            self.assertFalse(backend.flag.cancelled)
            server.submit(notice, 'good', events.put)
            self.assertTrue(backend.flag.cancelled)
        finally:
            server.close()

    def test_output_schema_is_enforced(self):
        class BadOutput(Backend):
            def describe(self, principal):
                descriptor = super().describe(principal)[0]
                descriptor.output_schema = '{"type":"object","required":["result"]}'
                return [descriptor]
        server = McpCommandServer(BadOutput()); events = queue.Queue()
        try:
            server.submit(json.dumps({'jsonrpc':'2.0','id':12,'method':'tools/call',
                'params':{'name':'doc.rename','arguments':{'name':'ok'}}}), 'good', events.put)
            self.assertTrue(json.loads(events.get(timeout=3))['result']['isError'])
        finally:
            server.close()

    def test_changed_schema_is_not_executed(self):
        class ChangingBackend(Backend):
            def __init__(self): self.descriptions = 0; self.executions = 0
            def describe(self, principal):
                descriptor = super().describe(principal)[0]
                self.descriptions += 1
                if self.descriptions > 1:
                    descriptor.input_schema = '{"type":"object","required":["new_required"]}'
                return [descriptor]
            def execute(self, *arguments):
                self.executions += 1
                return super().execute(*arguments)
        backend = ChangingBackend()
        server = McpCommandServer(backend)
        events = queue.Queue()
        try:
            server.submit(json.dumps({'jsonrpc':'2.0','id':11,'method':'tools/call',
                'params':{'name':'doc.rename','arguments':{'name':'old'}}}), 'good', events.put)
            reply = json.loads(events.get(timeout=3))
            self.assertTrue(reply.get('error') or reply.get('result', {}).get('isError'))
            self.assertEqual(backend.executions, 0)
        finally:
            server.close()

    def test_http_progress_and_cancel(self):
        httpd = make_http_server(self.server, ('127.0.0.1', 0))
        thread = threading.Thread(target=httpd.serve_forever); thread.start()
        headers = {'Content-Type':'application/json', 'Accept':'application/json, text/event-stream',
                   'Authorization':'Bearer good'}
        call = http.client.HTTPConnection('127.0.0.1', httpd.server_port, timeout=3)
        cancel = http.client.HTTPConnection('127.0.0.1', httpd.server_port, timeout=3)
        try:
            payload = {'jsonrpc':'2.0','id':'running-http','method':'tools/call',
                       'params':{'name':'doc.rename','arguments':{'name':'wait'},'_meta':{'progressToken':'token'}}}
            call.request('POST', '/mcp', json.dumps(payload), headers)
            response = call.getresponse()
            self.assertEqual(response.status, 200)
            progress = response.readline().decode().removeprefix('data: ')
            self.assertEqual(json.loads(progress)['method'], 'notifications/progress')
            notice = {'jsonrpc':'2.0','method':'notifications/cancelled','params':{'requestId':'running-http'}}
            cancel.request('POST', '/mcp', json.dumps(notice), headers)
            cancelled = cancel.getresponse()
            self.assertEqual(cancelled.status, 202)
            cancelled.read()
            self.assertEqual(response.read().strip(), b'')
        finally:
            self.server.close(); call.close(); cancel.close()
            httpd.shutdown(); httpd.server_close(); thread.join()

    def test_http_retains_large_integer_arguments(self):
        class LargeIntegerBackend(Backend):
            def describe(self, principal):
                descriptor = super().describe(principal)[0]
                descriptor.input_schema = '{"type":"object"}'
                return [descriptor]
            def execute(self, principal, invocation, cancel, progress):
                self.arguments = invocation.arguments_json
                result = c.ApplicationCommandResult(); result.output_json = '{"ok":true}'
                return result
        backend = LargeIntegerBackend(); server = McpCommandServer(backend)
        httpd = make_http_server(server, ('127.0.0.1', 0))
        thread = threading.Thread(target=httpd.serve_forever); thread.start()
        conn = http.client.HTTPConnection('127.0.0.1', httpd.server_port, timeout=30)
        try:
            arguments = '{"value":' + '8' * 4499 + '}'
            payload = ('{"jsonrpc":"2.0","id":9,"method":"tools/call",'
                       '"params":{"name":"doc.rename","arguments":' + arguments + '}}')
            headers = {'Content-Type':'application/json', 'Accept':'application/json, text/event-stream',
                       'Authorization':'Bearer good'}
            conn.request('POST', '/mcp', payload, headers)
            response = conn.getresponse(); body = response.read().decode()
            self.assertEqual(response.status, 200)
            self.assertFalse(json.loads(body.removeprefix('data: ').strip())['result']['isError'])
            self.assertEqual(backend.arguments, arguments)
        finally:
            conn.close(); server.close(); httpd.shutdown(); httpd.server_close(); thread.join()

    def test_stdio_and_http_transports(self):
        stream_server = McpCommandServer(Backend())
        incoming = io.StringIO('{"jsonrpc":"2.0","id":7,"method":"tools/list"}\n')
        outgoing = io.StringIO()
        serve_stdio(stream_server, 'good', incoming, outgoing)
        self.assertEqual(json.loads(outgoing.getvalue())['id'], 7)
        httpd = make_http_server(self.server, ('127.0.0.1', 0), ['https://allowed.example'])
        thread = threading.Thread(target=httpd.serve_forever); thread.start()
        try:
            def request(origin=None, credential='good'):
                conn = http.client.HTTPConnection('127.0.0.1', httpd.server_port, timeout=3)
                headers = {'Content-Type':'application/json', 'Accept':'application/json, text/event-stream',
                           'Authorization':'Bearer '+credential}
                if origin: headers['Origin'] = origin
                conn.request('POST', '/mcp', '{"jsonrpc":"2.0","id":8,"method":"tools/list"}', headers)
                response = conn.getresponse(); body = response.read().decode(); status = response.status
                conn.close(); return status, body
            self.assertEqual(request(origin='https://bad.example')[0], 403)
            self.assertEqual(request(credential='bad')[0], 401)
            status, body = request(origin='https://allowed.example')
            self.assertEqual(status, 200)
            self.assertEqual(json.loads(body.removeprefix('data: ').strip())['id'], 8)
        finally:
            httpd.shutdown(); httpd.server_close(); thread.join()

if __name__ == '__main__': unittest.main()
