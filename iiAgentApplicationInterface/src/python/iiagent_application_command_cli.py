"""Invoke one authenticated application command through the native CLI adapter."""
import argparse
import json
import os
import sys
import iiagent_application_commands as native
from iiagent_application_interface_client import ExternalClient


def main(argv=None):
    parser = argparse.ArgumentParser(description='Invoke a command on an application-owned iiAgent endpoint')
    parser.add_argument('--endpoint', required=True)
    parser.add_argument('--credential-env', default='IIAGENT_APPLICATION_CREDENTIAL')
    parser.add_argument('--timeout', type=float, default=60.0)
    parser.add_argument('--target', required=True)
    parser.add_argument('--command', required=True)
    parser.add_argument('--arguments', default='{}')
    args = parser.parse_args(argv)
    parsed = native.CliCommandAdapter().execute(['--target', args.target, '--command', args.command, '--arguments', args.arguments])
    if parsed.status != native.Status.OK:
        print(json.dumps({'status':1,'diagnostic':parsed.diagnostic}), file=sys.stderr); return 2
    try:
        credential = os.environ.get(args.credential_env, '')
        if not credential: raise ValueError('the host-issued credential is missing')
        client = ExternalClient(args.endpoint, credential, timeout=args.timeout)
        cursor = ''
        capability = None
        while True:
            page = client.request('list_capabilities', object_id=parsed.invocation.target_id, cursor=cursor, limit=100)
            if page['status'] != 0: raise ValueError('command discovery was denied')
            capability = next((item for item in page['capabilities'] if item['id'] == parsed.invocation.command), None)
            if capability is not None: break
            next_cursor = page.get('next_cursor', '')
            if not next_cursor or next_cursor == cursor: raise ValueError('command unavailable')
            cursor = next_cursor
        raw_schema = capability.get('input_schema', '')
        try: schema = json.loads(raw_schema, parse_int=str, parse_float=str)
        except (TypeError, ValueError): schema = None
        if isinstance(schema, (dict, bool)):
            request = native.CommandSchemaValidationRequest(); request.schema_json = raw_schema
            request.instance_json = parsed.invocation.arguments_json
            verdict = native.CommandSchemaValidator().execute(request)
            if not verdict.valid:
                print(json.dumps({'status':5,'diagnostic':verdict.diagnostic}, ensure_ascii=False)); return 1
        response = client.request('invoke', object_id=parsed.invocation.target_id,
                                  capability_id=parsed.invocation.command, input=parsed.invocation.arguments_json)
        raw_output_schema = capability.get('output_schema', '')
        try: output_schema = json.loads(raw_output_schema, parse_int=str, parse_float=str)
        except (TypeError, ValueError): output_schema = None
        if response['status'] == 0 and isinstance(output_schema, (dict, bool)):
            request = native.CommandSchemaValidationRequest(); request.schema_json = raw_output_schema
            request.instance_json = response.get('value', '')
            if not native.CommandSchemaValidator().execute(request).valid:
                print(json.dumps({'status':5,'diagnostic':'Command result does not match its output schema'})); return 1
        print(json.dumps(response, ensure_ascii=False, separators=(',', ':')))
        return 0 if response['status'] == 0 else 1
    except (OSError, ValueError, TypeError) as error:
        print(json.dumps({'error':str(error)}, ensure_ascii=False), file=sys.stderr); return 2


if __name__ == '__main__':
    raise SystemExit(main())
