"""Exercise a live C++ app through the installed client and an independent CLI process."""

import json
import os
from pathlib import Path
import secrets
import selectors
import socket
import subprocess
import sys
import tempfile


def check(host_binary, python_dir, compiler, library, module_root, build_dir):
    """Authenticate independent agents, mutate the same live object, and reject spoofing."""
    sys.path.insert(0, python_dir)
    from iiagent_application_interface_client import ExternalClient

    token = secrets.token_urlsafe(32)
    child_env = os.environ.copy()
    child_env["IIAGENT_TEST_CREDENTIAL"] = token
    child_env["IIAGENT_APPLICATION_CREDENTIAL"] = token
    child_env["PYTHONPATH"] = python_dir
    child_env["PYTHONDONTWRITEBYTECODE"] = "1"
    with tempfile.TemporaryDirectory(prefix="agent-", dir=build_dir) as directory:
        endpoint = str(Path(directory) / "app.sock")
        artifact = str(Path(directory) / "procedure.so")
        host = subprocess.Popen([host_binary, endpoint, compiler, artifact, library, module_root],
                                env=child_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        selector = selectors.DefaultSelector()
        selector.register(host.stdout, selectors.EVENT_READ)
        try:
            if not selector.select(timeout=30) or host.stdout.readline().strip() != "READY":
                raise AssertionError("external application host did not become ready")

            def call(request, credential=token):
                """Issue one independent CLI process without putting the secret in arguments or output."""
                env = child_env.copy()
                env["IIAGENT_APPLICATION_CREDENTIAL"] = credential
                completed = subprocess.run([sys.executable, "-B", "-m", "iiagent_application_interface_cli",
                                            "--endpoint", endpoint], input=json.dumps(request), text=True,
                                           capture_output=True, env=env, timeout=60)
                if token in completed.stdout or token in completed.stderr:
                    raise AssertionError("credential leaked through CLI output")
                response = json.loads(completed.stdout)
                if completed.returncode not in (0, 1):
                    raise AssertionError("CLI request did not reach the application")
                return response

            client = ExternalClient(endpoint, token)
            try:
                ExternalClient(endpoint, token, max_bytes=127)
            except ValueError:
                pass
            else:
                raise AssertionError("invalid byte limits must be rejected")
            try:
                client.request("describe", credential="spoof")
            except ValueError:
                pass
            else:
                raise AssertionError("request fields must not replace credentials")
            info = client.request("describe")
            assert info["status"] == 0 and "objects" in info and not info["objects"]
            assert {item["name"] for item in info["operations"]} == {
                "describe", "list_objects", "read_object", "list_capabilities", "invoke", "execute_source", "execute_draft"}
            assert all("required" in item and "optional" in item for item in info["operations"])
            # A caller that disconnects before reading must not poison the host.
            abandoned = {"protocol": "iiagent.application.v1", "operation": "describe", "credential": token}
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as transient:
                transient.connect(endpoint)
                transient.sendall(json.dumps(abandoned).encode("utf-8") + b"\n")
            assert client.request("describe")["status"] == 0
            page = call({"operation": "list_objects", "limit": 1})
            assert page["status"] == 0 and page["objects"][0]["id"] == "control"
            assert "state" not in page["objects"][0] and page["next_cursor"] == "control"
            page = call({"operation": "list_objects", "limit": 1, "cursor": page["next_cursor"]})
            assert page["status"] == 0 and page["objects"][0]["id"] == "document"
            assert "state" not in page["objects"][0] and page["next_cursor"] == ""
            assert client.request("list_capabilities")["status"] == 1
            assert client.request("invoke", object_id="document")["status"] == 1
            assert client.request("read_object", object_id="hidden")["status"] == 4
            assert client.request("read_object", object_id="document")["object"]["state"] == "initial"
            caps = call({"operation": "list_capabilities", "object_id": "document", "limit": 1})
            assert [item["id"] for item in caps["capabilities"]] == ["publish"] and caps["next_cursor"] == "publish"
            caps = call({"operation": "list_capabilities", "object_id": "document", "limit": 1, "cursor": caps["next_cursor"]})
            assert [item["id"] for item in caps["capabilities"]] == ["rename"] and caps["next_cursor"] == ""
            denied = call({"operation": "invoke", "object_id": "document", "capability_id": "rename",
                           "input": "unauthorized"}, credential="invalid")
            assert denied["status"] == 4
            assert client.request("read_object", object_id="document")["object"]["state"] == "initial"
            changed = call({"operation": "invoke", "object_id": "document", "capability_id": "rename",
                            "input": "Changed from an external agent"})
            assert changed["status"] == 0 and changed["value"] == "Changed from an external agent"
            assert client.request("read_object", object_id="document")["object"]["state"] == changed["value"]
            spoof = call({"operation": "read_object", "object_id": "document", "principal": "approved-agent"})
            assert spoof["status"] == 1
            body = '''auto object = context.write_object<external_host_fixture::Document>("document");
if (!object) return {iiAgentApplicationInterface::Status::denied, "document unavailable"};
object->rename(std::string(input));
return context.invoke("document", "publish", object->title());'''
            procedure = call({"operation": "execute_draft", "procedure_id": "external.draft",
                              "preamble": '#include "Document.h"', "body": body, "input": "Native change"})
            assert procedure["status"] == 0 and procedure["value"] == "Native change"
            assert client.request("read_object", object_id="document")["object"]["state"] == "Native change"
            assert not Path(artifact).exists()
            assert call({"operation": "invoke", "object_id": "control", "capability_id": "shutdown"})["status"] == 0
            if host.wait(timeout=10) != 0:
                raise AssertionError("test host did not stop cleanly")
            assert not Path(endpoint).exists()
        finally:
            selector.close()
            if host.poll() is None:
                host.terminate()
                try:
                    host.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait(timeout=5)
            host.stdout.close()
            host.stderr.close()
    print("External CLI and client operated on the same authenticated live application")


if __name__ == "__main__":
    check(*sys.argv[1:])
