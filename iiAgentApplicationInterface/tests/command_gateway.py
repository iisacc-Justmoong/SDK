"""Exercise command CLI and stdio MCP against a disposable local gateway fixture."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULES = os.environ['IIAGENT_COMMAND_MODULE_DIR']
ENV = os.environ.copy()
ENV['PYTHONPATH'] = os.pathsep.join((str(ROOT/'src/python'), MODULES))
ENV['IIAGENT_APPLICATION_CREDENTIAL'] = 'fixture-secret'
ENV['PYTHONDONTWRITEBYTECODE'] = '1'
for key in ('DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','DYLD_FRAMEWORK_PATH','DYLD_FALLBACK_FRAMEWORK_PATH'):
    ENV.pop(key, None)

class Gateway:
    def __init__(self, endpoint):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.bind(endpoint); self.socket.listen(8); self.socket.settimeout(.1)
        self.closed = threading.Event(); self.invocations = []
        self.capability_reads = 0; self.change_schema_on = None
        self.input_schema_override = None; self.output_schema_override = None
        self.thread = threading.Thread(target=self.run); self.thread.start()
    def run(self):
        while not self.closed.is_set():
            try: conn, _ = self.socket.accept()
            except TimeoutError: continue
            except OSError: break
            with conn:
                data = b''
                while b'\n' not in data:
                    chunk = conn.recv(65536)
                    if not chunk: break
                    data += chunk
                try: req = json.loads(data)
                except Exception: continue
                out = {'protocol':'iiagent.application.v1','status':0,'value':'','objects':[],
                       'capabilities':[], 'next_cursor':''}
                if req.get('credential') != 'fixture-secret':
                    out['status'] = 4
                elif req['operation'] == 'list_objects':
                    out['objects'] = [{'id':'doc','type':'document'}]
                elif req['operation'] == 'list_capabilities':
                    self.capability_reads += 1
                    schema = {'type':'object','properties':{'name':{'type':'string','minLength':1}},'required':['name']}
                    if self.change_schema_on and self.capability_reads >= self.change_schema_on:
                        schema['required'].append('new_required')
                    out['capabilities'] = [{'id':'rename','object_id':'doc','description':'Rename',
                        'input_schema':self.input_schema_override or json.dumps(schema),
                        'output_schema':self.output_schema_override or '{"type":"object"}',
                        'permissions':['edit']}]
                elif req['operation'] == 'invoke':
                    self.invocations.append(req)
                    out['value'] = req['input']
                conn.sendall(json.dumps(out).encode() + b'\n')
    def close(self):
        self.closed.set(); self.socket.close(); self.thread.join(timeout=3)
        if self.thread.is_alive(): raise AssertionError('fixture did not stop')

class GatewayTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='mcp-', dir=ROOT/'resources')
        self.endpoint = str(Path(self.directory.name)/'gateway.sock')
        self.gateway = Gateway(self.endpoint)
    def tearDown(self):
        self.gateway.close(); self.directory.cleanup()
    def test_cli_schema_and_gateway(self):
        base = [sys.executable,'-B','-m','iiagent_application_command_cli','--endpoint',self.endpoint,
                '--target','doc','--command','rename','--arguments']
        invalid = subprocess.run(base+['{}'], capture_output=True, text=True, env=ENV, timeout=15)
        self.assertEqual(invalid.returncode, 1, invalid.stderr)
        self.assertFalse(self.gateway.invocations)
        valid = subprocess.run(base+['{"name":"New"}'], capture_output=True, text=True, env=ENV, timeout=15)
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertEqual(json.loads(valid.stdout)['status'], 0)
        self.assertEqual(json.loads(self.gateway.invocations[0]['input']), {'name':'New'})
    def test_stdio_mcp_gateway(self):
        process = subprocess.Popen([sys.executable,'-B','-m','iiagent_application_mcp','--endpoint',self.endpoint],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=ENV, text=True, bufsize=1)
        def exchange(value):
            process.stdin.write(json.dumps(value)+'\n'); process.stdin.flush()
            return json.loads(process.stdout.readline())
        try:
            init = exchange({'jsonrpc':'2.0','id':1,'method':'initialize',
                'params':{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'fixture','version':'1'}}})
            self.assertEqual(init['result']['protocolVersion'],'2025-11-25')
            listed = exchange({'jsonrpc':'2.0','id':2,'method':'tools/list'})
            name = listed['result']['tools'][0]['name']
            result = exchange({'jsonrpc':'2.0','id':3,'method':'tools/call',
                               'params':{'name':name,'arguments':{'name':'MCP'}}})
            self.assertEqual(result['result']['structuredContent'], {'name':'MCP'})
            self.assertEqual(json.loads(self.gateway.invocations[0]['input']), {'name':'MCP'})
        finally:
            process.stdin.close()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait(); raise
            stderr = process.stderr.read()
            process.stdout.close(); process.stderr.close()
            self.assertEqual(process.returncode, 0, stderr)

    def test_gateway_revalidates_a_changed_contract_before_invoking(self):
        self.gateway.change_schema_on = 4
        process = subprocess.Popen([sys.executable,'-B','-m','iiagent_application_mcp','--endpoint',self.endpoint],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=ENV, text=True, bufsize=1)
        def exchange(value):
            process.stdin.write(json.dumps(value)+'\n'); process.stdin.flush()
            return json.loads(process.stdout.readline())
        try:
            listed = exchange({'jsonrpc':'2.0','id':21,'method':'tools/list'})
            name = listed['result']['tools'][0]['name']
            result = exchange({'jsonrpc':'2.0','id':22,'method':'tools/call',
                               'params':{'name':name,'arguments':{'name':'old'}}})
            self.assertTrue(result.get('error') or result.get('result', {}).get('isError'))
            self.assertFalse(self.gateway.invocations)
        finally:
            process.stdin.close()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait(); raise
            stderr = process.stderr.read(); process.stdout.close(); process.stderr.close()
            self.assertEqual(process.returncode, 0, stderr)

    def test_cli_and_mcp_keep_exact_schema_numbers(self):
        boundary = ('{"type":"object","properties":{"value":'
                    '{"type":"number","maximum":0.1234567890123456789}}}')
        too_large = '{"value":0.1234567890123456790}'
        base = [sys.executable, '-B', '-m', 'iiagent_application_command_cli',
                '--endpoint', self.endpoint, '--target', 'doc', '--command', 'rename',
                '--arguments', too_large]
        self.gateway.input_schema_override = boundary
        invalid = subprocess.run(base, capture_output=True, text=True, env=ENV, timeout=45)
        self.assertEqual(invalid.returncode, 1, invalid.stderr)
        self.assertFalse(self.gateway.invocations)

        self.gateway.input_schema_override = '{"type":"object"}'
        self.gateway.output_schema_override = boundary
        invalid_output = subprocess.run(base, capture_output=True, text=True, env=ENV, timeout=45)
        self.assertEqual(invalid_output.returncode, 1, invalid_output.stderr)
        self.assertEqual(json.loads(invalid_output.stdout)['status'], 5)

        self.gateway.input_schema_override = boundary
        self.gateway.output_schema_override = '{"type":"object"}'
        self.gateway.invocations.clear()
        process = subprocess.Popen([sys.executable, '-B', '-m', 'iiagent_application_mcp',
                                    '--endpoint', self.endpoint], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=ENV, text=True, bufsize=1)
        try:
            process.stdin.write('{"jsonrpc":"2.0","id":31,"method":"tools/list"}\n'); process.stdin.flush()
            listed = json.loads(process.stdout.readline())
            name = listed['result']['tools'][0]['name']
            message = ('{"jsonrpc":"2.0","id":32,"method":"tools/call","params":{"name":'
                       + json.dumps(name) + ',"arguments":' + too_large + '}}\n')
            process.stdin.write(message); process.stdin.flush()
            result = json.loads(process.stdout.readline())
            self.assertTrue(result.get('error') or result.get('result', {}).get('isError'))
            self.assertFalse(self.gateway.invocations)
            self.gateway.input_schema_override = '{"type":"object"}'
            self.gateway.output_schema_override = boundary
            message = message.replace('"id":32', '"id":33')
            process.stdin.write(message); process.stdin.flush()
            result = json.loads(process.stdout.readline())
            self.assertTrue(result.get('error') or result.get('result', {}).get('isError'))
            self.assertEqual(len(self.gateway.invocations), 1)
        finally:
            process.stdin.close()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait(); raise
            stderr = process.stderr.read(); process.stdout.close(); process.stderr.close()
            self.assertEqual(process.returncode, 0, stderr)

    def test_large_integer_survives_mcp_envelope_and_gateway(self):
        self.gateway.input_schema_override = ('{"type":"object","properties":{"value":'
                                              '{"type":"integer","maximum":' + '9' * 4500 + '}}}')
        arguments = '{"value":' + '8' * 4499 + '}'
        process = subprocess.Popen([sys.executable, '-B', '-m', 'iiagent_application_mcp',
                                    '--endpoint', self.endpoint], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=ENV, text=True, bufsize=1)
        try:
            process.stdin.write('{"jsonrpc":"2.0","id":41,"method":"tools/list"}\n'); process.stdin.flush()
            listed = json.loads(process.stdout.readline(), parse_int=str)
            name = listed['result']['tools'][0]['name']
            message = ('{"jsonrpc":"2.0","id":42,"method":"tools/call","params":{"name":'
                       + json.dumps(name) + ',"arguments":' + arguments + '}}\n')
            process.stdin.write(message); process.stdin.flush()
            result = json.loads(process.stdout.readline(), parse_int=str)
            self.assertFalse(result.get('error') or result.get('result', {}).get('isError'))
            self.assertEqual(self.gateway.invocations[0]['input'], arguments)
        finally:
            process.stdin.close()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait(); raise
            stderr = process.stderr.read(); process.stdout.close(); process.stderr.close()
            self.assertEqual(process.returncode, 0, stderr)

if __name__ == '__main__': unittest.main()
