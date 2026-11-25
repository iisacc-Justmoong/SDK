"""CLI for submitting one JSON request to an approved running application."""

import argparse
import json
import os
import sys

from iiagent_application_interface_client import ExternalClient


def main(argv=None):
    """Read an operation from stdin, authenticate using an environment variable and print JSON."""
    parser = argparse.ArgumentParser(description="Call a running application's iiAgent interface over a local socket")
    parser.add_argument("--endpoint", required=True, help="application-owned local socket path")
    parser.add_argument("--credential-env", default="IIAGENT_APPLICATION_CREDENTIAL", help="environment variable containing the host-issued credential")
    parser.add_argument("--timeout", type=float, default=60.0, help="response timeout in seconds")
    args = parser.parse_args(argv)
    try:
        encoded = sys.stdin.buffer.read(1024 * 1024 + 1)
        if len(encoded) > 1024 * 1024:
            raise ValueError("stdin request exceeds the byte limit")
        request = json.loads(encoded)
        if not isinstance(request, dict):
            raise ValueError("stdin must contain a JSON object")
        operation = request.pop("operation", None)
        credential = os.environ.get(args.credential_env, "")
        response = ExternalClient(args.endpoint, credential, timeout=args.timeout).request(operation, **request)
        print(json.dumps(response, ensure_ascii=False, separators=(",", ":")))
        return 0 if response["status"] == 0 else 1
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
