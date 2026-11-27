"""Host-composed MCP command server for stdio and Streamable HTTP.

Authentication, command discovery and execution are supplied by the host. No
credential, application object, network listener or thread is obtained until
explicitly passed to or started by the caller.
"""
from concurrent.futures import ThreadPoolExecutor
import json
import queue
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

import iiagent_application_commands as native


def _inspect_envelope(message):
    """Inspect transport fields without converting command payload numbers.

    The original JSON is passed to the native adapter unchanged. Only request
    IDs fit for that adapter need conversion; larger payload integers must not
    trip Python's integer digit limit during this preliminary inspection.
    """
    def request_integer(text):
        return int(text) if len(text) <= 20 else None
    return json.loads(message, parse_int=request_integer, parse_float=lambda _: None)


class McpCommandServer:
    """Coordinate native command objects with host callbacks and scoped cancellation.

    ``backend`` supplies ``authenticate(credential)``, ``describe(principal)``
    and ``execute(principal, invocation, cancellation, progress)``. An optional
    ``schema_resources(principal, descriptor)`` returns explicit URI/schema data.
    Authentication
    must return an immutable, hashable host identity or None. Descriptors are
    already filtered for discovery; execute must reauthorize. In-process handlers
    must poll cancellation; isolated processes may share it with the native runner.
    """
    def __init__(self, backend, max_workers=8, max_active=128):
        if max_workers < 1 or max_active < 1:
            raise ValueError('worker and admission limits must be positive')
        self._backend = backend
        self._executor = ThreadPoolExecutor(max_workers=max_workers, thread_name_prefix='iiagent-mcp')
        self._max_active = max_active
        self._active = {}
        self._lock = threading.Lock()
        self._closed = False

    @staticmethod
    def _error(message, code, text):
        try:
            parsed = _inspect_envelope(message)
            if not isinstance(parsed, dict) or 'id' not in parsed:
                return ''
            identity = parsed['id']
        except Exception:
            identity = None
        return json.dumps({'jsonrpc':'2.0','id':identity,'error':{'code':code,'message':text}}, separators=(',', ':'))

    def authenticate(self, credential):
        """Authenticate one request. A missing, rejected or failing verifier denies."""
        try:
            principal = self._backend.authenticate(credential)
            if principal is None:
                return None
            hash(principal)
            return principal
        except Exception:
            return None

    def submit(self, message, credential, send, on_finished=None):
        """Process one JSON-RPC message; tool work runs on a bounded worker pool.

        ``send`` receives UTF-8 JSON text; callbacks may run on a worker thread.
        ``on_finished`` is called after a request terminates, including cancellation.
        The host must keep callbacks alive and serialize its own app state.
        """
        done = on_finished or (lambda: None)
        if not isinstance(message, str) or len(message.encode('utf-8')) > 2 * 1024 * 1024:
            send(self._error(message if isinstance(message, str) else '', -32600, 'Request too large'))
            done(); return
        principal = self.authenticate(credential)
        if principal is None:
            error = self._error(message, -32000, 'Authentication failed')
            if error: send(error)
            done(); return
        try:
            parsed = _inspect_envelope(message)
            method = parsed.get('method') if isinstance(parsed, dict) else None
            descriptors = self._backend.describe(principal) if method in ('tools/list', 'tools/call') else []
            request = native.McpCommandInput(); request.message = message; request.published = descriptors
            adapted = native.McpCommandAdapter().execute(request)
        except Exception:
            error = self._error(message, -32603, 'Command discovery failed')
            if error: send(error)
            done(); return
        if adapted.cancellation_id_json:
            key = (principal, adapted.cancellation_id_json)
            with self._lock: active = self._active.get(key)
            if active: active['cancel'].execute()
            done(); return
        if not adapted.invocation:
            if adapted.response: send(adapted.response)
            done(); return
        invocation = adapted.invocation
        invocation.principal = principal if isinstance(principal, str) else ''
        descriptor = next((d for d in descriptors if d.name == invocation.command and d.target_id == invocation.target_id), None)
        if descriptor is None:
            error = self._error(message, -32602, 'Tool unavailable')
            if error: send(error)
            done(); return
        # Discovery is only a snapshot. A queued job must not validate against
        # a superseded schema or capability mapping before execution.
        def fingerprint(item):
            return (item.name, item.target_id, item.input_schema, item.output_schema,
                    tuple(item.required_scopes), item.capability_id, item.opaque_input)
        published_contract = fingerprint(descriptor)
        flag = native.CommandCancellationHandle()
        key = (principal, adapted.request_id_json)
        record = {'cancel':flag, 'last':None, 'token':adapted.progress_token_json, 'delivery':threading.Lock()}
        with self._lock:
            if self._closed or len(self._active) >= self._max_active or key in self._active:
                error = self._error(message, -32000, 'Command capacity or request identity unavailable')
            else:
                self._active[key] = record
                error = ''
        if error:
            send(error); done(); return

        def progress(value, total=None, text=''):
            with record['delivery']:
                with self._lock:
                    current = self._active.get(key)
                    if current is not record or flag.cancelled or not record['token']:
                        return
                    if record['last'] is not None and value <= record['last']:
                        return
                    event = native.CommandProgressEvent(); event.request_id = adapted.request_id_json
                    event.token_json = record['token']; event.progress = value; event.total = total; event.message = text
                    data = native.McpCommandInput(); data.operation = native.McpCommandOperation.PROGRESS; data.progress = event
                    result = native.McpCommandAdapter().execute(data)
                    if result.status != native.Status.OK or not result.response:
                        return
                    record['last'] = value
                try: send(result.response)
                except Exception: flag.execute()

        def run():
            try:
                current = [d for d in self._backend.describe(principal)
                           if d.name == invocation.command and d.target_id == invocation.target_id]
                if len(current) != 1 or fingerprint(current[0]) != published_contract:
                    raise ValueError('Command contract changed while queued')
                validation = native.CommandSchemaValidationRequest()
                validation.schema_json = descriptor.input_schema
                validation.instance_json = invocation.arguments_json
                resources = getattr(self._backend, 'schema_resources', None)
                if resources is not None: validation.resources = resources(principal, descriptor)
                verdict = native.CommandSchemaValidator().execute(validation)
                if not verdict.valid:
                    result = native.ApplicationCommandResult(); result.status = native.Status.VALIDATION_FAILED
                    result.diagnostic = verdict.diagnostic or 'Invalid arguments'
                else:
                    result = self._backend.execute(principal, invocation, flag, progress)
                    if not isinstance(result, native.ApplicationCommandResult):
                        raise TypeError('host returned an invalid command result')
                    if result.status == native.Status.OK and descriptor.output_schema:
                        validation.schema_json = descriptor.output_schema; validation.instance_json = result.output_json
                        verdict = native.CommandSchemaValidator().execute(validation)
                        if not verdict.valid:
                            result = native.ApplicationCommandResult(); result.status = native.Status.VALIDATION_FAILED
                            result.diagnostic = 'Command result does not match its output schema'
            except BaseException:
                result = native.ApplicationCommandResult(); result.status = native.Status.HANDLER_FAILED
                result.diagnostic = 'Command execution failed'
            try:
                with record['delivery']:
                    with self._lock: self._active.pop(key, None)
                    if not flag.cancelled:
                        data = native.McpCommandInput(); data.operation = native.McpCommandOperation.RESULT
                        data.request_id_json = adapted.request_id_json; data.result = result
                        encoded = native.McpCommandAdapter().execute(data)
                        if encoded.response: send(encoded.response)
            finally:
                done()
        try:
            self._executor.submit(run)
        except RuntimeError:
            with self._lock: self._active.pop(key, None)
            send(self._error(message, -32000, 'Server closed')); done()

    def close(self):
        """Request cancellation of all jobs; the host controls handler completion."""
        with self._lock:
            self._closed = True
            active = list(self._active.values())
        for record in active: record['cancel'].execute()
        self._executor.shutdown(wait=False, cancel_futures=False)


def serve_stdio(server, credential, reader, writer):
    """Serve newline-framed MCP messages on explicitly supplied text streams."""
    output_lock = threading.Lock()
    def send(message):
        with output_lock:
            writer.write(message + '\n'); writer.flush()
    while True:
        line = reader.readline(2 * 1024 * 1024 + 1)
        if not line: break
        if len(line.encode('utf-8')) > 2 * 1024 * 1024:
            break
        server.submit(line.rstrip('\r\n'), credential, send)
    server.close()


def make_http_server(server, address, allowed_origins=(), endpoint='/mcp'):
    """Create a Streamable HTTP server; the caller starts/stops its serve loop.

    ``address`` and allowed browser origins are explicit host inputs. Every POST
    is authenticated by the backend. JSON-RPC responses use request-scoped SSE;
    GET returns 405 because unsolicited streams are not offered.
    """
    origins = frozenset(allowed_origins)
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *_): pass
        def reject(self, status):
            self.send_response(status); self.send_header('Content-Length', '0'); self.send_header('Connection', 'close'); self.end_headers()
        def do_GET(self):
            origin = self.headers.get('Origin')
            if origin and origin not in origins: self.reject(403); return
            self.reject(405 if urlsplit(self.path).path == endpoint else 404)
        def do_POST(self):
            if urlsplit(self.path).path != endpoint: self.reject(404); return
            origin = self.headers.get('Origin')
            if origin and origin not in origins: self.reject(403); return
            auth = self.headers.get('Authorization', '')
            if not auth.startswith('Bearer ') or server.authenticate(auth[7:]) is None: self.reject(401); return
            accept = self.headers.get('Accept', '')
            if 'application/json' not in accept or 'text/event-stream' not in accept: self.reject(406); return
            if self.headers.get_content_type() != 'application/json': self.reject(415); return
            version = self.headers.get('MCP-Protocol-Version')
            if version and version != '2025-11-25': self.reject(400); return
            try:
                length = int(self.headers.get('Content-Length', '-1'))
                if length < 0 or length > 2 * 1024 * 1024: self.reject(413); return
                payload = self.rfile.read(length).decode('utf-8')
                parsed = _inspect_envelope(payload)
            except (ValueError, UnicodeError): self.reject(400); return
            if not isinstance(parsed, dict): self.reject(400); return
            if 'id' not in parsed:
                server.submit(payload, auth[7:], lambda _: None)
                self.send_response(202); self.send_header('Content-Length', '0'); self.end_headers(); return
            messages = queue.Queue(); done = threading.Event()
            server.submit(payload, auth[7:], messages.put, done.set)
            self.send_response(200); self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache'); self.send_header('Connection', 'close'); self.end_headers()
            try:
                while not done.is_set() or not messages.empty():
                    try: message = messages.get(timeout=.1)
                    except queue.Empty: continue
                    self.wfile.write(('data: ' + message + '\n\n').encode('utf-8')); self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                notice = json.dumps({'jsonrpc':'2.0','method':'notifications/cancelled','params':{'requestId':parsed['id']}})
                server.submit(notice, auth[7:], lambda _: None)
            finally:
                self.close_connection = True
    return ThreadingHTTPServer(address, Handler)


class GatewayBackend:
    """Connect the adapter to an app-owned iiAgent local gateway.

    The gateway authenticates every operation and rechecks execution policy.
    Legacy synchronous gateway calls cannot receive progress or cancellation;
    hosts needing cooperative cancellation should supply a backend that accepts
    the cancellation handle directly.
    """
    class _Identity:
        __slots__ = ('credential',)
        def __init__(self, credential): self.credential = credential
        def __hash__(self): return hash(self.credential)
        def __eq__(self, other): return isinstance(other, GatewayBackend._Identity) and self.credential == other.credential

    def __init__(self, endpoint, timeout=60.0, limit=5000):
        self.endpoint, self.timeout, self.limit = endpoint, timeout, limit

    def _client(self, credential):
        from iiagent_application_interface_client import ExternalClient
        return ExternalClient(self.endpoint, credential, timeout=self.timeout)

    def authenticate(self, credential):
        if not credential: return None
        try:
            response = self._client(credential).request('describe')
            return self._Identity(credential) if response.get('status') == 0 else None
        except Exception:
            return None

    @staticmethod
    def _tool_name(object_id, capability_id):
        import hashlib
        return 'cmd_' + hashlib.sha256((object_id + '\0' + capability_id).encode('utf-8')).hexdigest()

    @staticmethod
    def _pages(client, operation, **arguments):
        cursor = ''
        while True:
            response = client.request(operation, cursor=cursor, limit=100, **arguments)
            if response['status'] != 0: raise ValueError('discovery denied')
            yield response
            next_cursor = response.get('next_cursor', '')
            if not next_cursor: return
            if next_cursor == cursor: raise ValueError('discovery cursor did not advance')
            cursor = next_cursor

    def describe(self, principal):
        client = self._client(principal.credential)
        descriptors = []
        for page in self._pages(client, 'list_objects'):
            for obj in page.get('objects', []):
                for caps in self._pages(client, 'list_capabilities', object_id=obj['id']):
                    for capability in caps.get('capabilities', []):
                        if len(descriptors) >= self.limit: raise ValueError('discovery limit exceeded')
                        descriptor = native.ApplicationCommandDescriptor()
                        descriptor.name = self._tool_name(obj['id'], capability['id'])
                        descriptor.target_id = obj['id']; descriptor.capability_id = capability['id']
                        descriptor.description = obj['id'] + ' / ' + capability['id'] + ': ' + capability.get('description', '')
                        descriptor.required_scopes = capability.get('permissions', [])
                        try:
                            raw_schema = capability['input_schema']
                            schema = json.loads(raw_schema, parse_int=str, parse_float=str)
                            if not isinstance(schema, dict): raise ValueError('not an object schema')
                            descriptor.input_schema = raw_schema
                        except (KeyError, TypeError, ValueError):
                            descriptor.opaque_input = True
                            descriptor.input_schema = json.dumps({'type':'object','properties':{'input':{'type':'string'}},
                                'required':['input'],'additionalProperties':False}, separators=(',', ':'))
                        try:
                            raw_output = capability.get('output_schema', '')
                            output = json.loads(raw_output, parse_int=str, parse_float=str)
                            if isinstance(output, dict): descriptor.output_schema = raw_output
                        except (TypeError, ValueError): pass
                        descriptors.append(descriptor)
        return descriptors

    def execute(self, principal, invocation, cancellation, progress):
        result = native.ApplicationCommandResult()
        if cancellation.cancelled:
            result.status = native.Status.HANDLER_FAILED; result.cancelled = True; return result
        # Resolve the descriptor again immediately before execution. The app's
        # gateway independently reauthenticates and reauthorizes the command.
        descriptors = self.describe(principal)
        current = next((d for d in descriptors if d.name == invocation.command and d.target_id == invocation.target_id), None)
        if current is None:
            result.status = native.Status.DENIED; result.diagnostic = 'Command unavailable'; return result
        # The app can change a published contract while the job is queued.
        # Validate the latest gateway snapshot before mapping or invoking it;
        # the app still reauthorizes and runs its own validator in Runtime.
        validation = native.CommandSchemaValidationRequest()
        validation.schema_json = current.input_schema
        validation.instance_json = invocation.arguments_json
        verdict = native.CommandSchemaValidator().execute(validation)
        if not verdict.valid:
            result.status = native.Status.VALIDATION_FAILED
            result.diagnostic = verdict.diagnostic or 'Command arguments no longer match the current schema'
            return result
        payload = (json.loads(invocation.arguments_json)['input'] if current.opaque_input
                   else invocation.arguments_json)
        response = self._client(principal.credential).request('invoke', object_id=current.target_id,
                                                             capability_id=current.capability_id, input=payload)
        if response['status'] == 0:
            result.output_json = response.get('value', '')
        else:
            result.status = native.Status.HANDLER_FAILED
            result.diagnostic = response.get('value', '') or 'Command failed'
        result.cancelled = cancellation.cancelled
        return result


def main(argv=None):
    """Run a gateway-backed MCP proxy using stdio or explicit HTTP bind settings."""
    import argparse
    import os
    import sys
    parser = argparse.ArgumentParser(description='Expose an approved iiAgent gateway as MCP tools')
    parser.add_argument('--endpoint', required=True, help='application-owned local gateway endpoint')
    parser.add_argument('--transport', choices=('stdio', 'http'), default='stdio')
    parser.add_argument('--credential-env', default='IIAGENT_APPLICATION_CREDENTIAL')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--allow-origin', action='append', default=[])
    args = parser.parse_args(argv)
    backend = GatewayBackend(args.endpoint)
    server = McpCommandServer(backend)
    try:
        if args.transport == 'stdio':
            credential = os.environ.get(args.credential_env, '')
            if not credential: raise ValueError('the host-issued credential is missing')
            serve_stdio(server, credential, sys.stdin, sys.stdout)
        else:
            http = make_http_server(server, (args.host, args.port), args.allow_origin)
            try: http.serve_forever()
            finally: http.server_close()
        return 0
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr); return 2
    finally:
        server.close()


if __name__ == '__main__':
    raise SystemExit(main())
