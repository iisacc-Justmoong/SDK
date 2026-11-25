"""Connect an external agent to an application-owned local iiAgent endpoint."""

import json
import socket


class ExternalClient:
    """Send bounded, authenticated one-request connections to a running application.

    The host establishes the endpoint and decides whether the supplied credential
    and operating-system peer are an approved agent. A client-provided name is
    never treated as an authenticated principal.
    """

    def __init__(self, endpoint, credential="", *, timeout=60.0, max_bytes=1024 * 1024):
        """Configure a local socket path, host-issued credential and transport limits."""
        if not isinstance(endpoint, str) or not endpoint or "\0" in endpoint:
            raise ValueError("endpoint must be a nonempty local socket path")
        if not isinstance(credential, str) or "\0" in credential:
            raise ValueError("credential must be NUL-free text")
        if timeout <= 0 or isinstance(max_bytes, bool) or not isinstance(max_bytes, int) or max_bytes < 128:
            raise ValueError("invalid client limits")
        self.endpoint = endpoint
        self._credential = credential
        self.timeout = timeout
        self.max_bytes = max_bytes

    def request(self, operation, **parameters):
        """Return one JSON response; the app authenticates and authorizes every request."""
        if not isinstance(operation, str) or not operation or "\0" in operation:
            raise ValueError("operation is required")
        if {"protocol", "credential", "operation"} & parameters.keys():
            raise ValueError("reserved request fields cannot be overridden")
        request = {"protocol": "iiagent.application.v1", "operation": operation,
                   "credential": self._credential, **parameters}
        encoded = json.dumps(request, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
        if len(encoded) > self.max_bytes:
            raise ValueError("request exceeds the configured byte limit")
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(self.timeout)
            connection.connect(self.endpoint)
            connection.sendall(encoded)
            data = bytearray()
            while True:
                part = connection.recv(min(65536, self.max_bytes + 1 - len(data)))
                if not part:
                    raise ConnectionError("application closed before completing its response")
                data.extend(part)
                if len(data) > self.max_bytes:
                    raise ValueError("response exceeds the configured byte limit")
                if b"\n" in part:
                    break
            line, _, trailing = bytes(data).partition(b"\n")
            if trailing:
                raise ValueError("unexpected data after the application response")
        response = json.loads(line.decode("utf-8"))
        if not isinstance(response, dict) or response.get("protocol") != "iiagent.application.v1" or type(response.get("status")) is not int:
            raise ValueError("invalid application response")
        return response
