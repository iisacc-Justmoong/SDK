"""Contract test for the Python wrapper over the actual native runtime."""
import gc

from iiagent_application_interface import AgentError, Runtime, Status


def expect_status(expected, call):
    """Require a host-visible native status without hiding unexpected failures."""
    try:
        call()
    except AgentError as error:
        assert error.status == expected, (error.status, expected)
    else:
        raise AssertionError(f"expected {expected}")


with Runtime() as closed:
    closed.add_object("doc", "document", state="secret")
    assert closed.objects("unknown") == []

allowed = {"value": True}
with Runtime(lambda principal, action, obj, cap, permissions:
             allowed["value"] and principal == "alice" and obj != "hidden") as runtime:
    runtime.add_object("doc", "document", description='한글 "note"', state="old\nstate")
    runtime.add_object("hidden", "document", state="secret")
    assert runtime.objects("alice")[0]["description"] == '한글 "note"'
    assert runtime.object("alice", "hidden") is None
    assert runtime.object("mallory", "doc") is None
    assert runtime.object("alice", "doc")["revision"] == 1
    seen = []
    token = runtime.subscribe("alice", lambda event: seen.append(event))
    calls = []

    def rename(principal, text):
        """Apply the host's own command and publish its resulting state."""
        calls.append(principal)
        runtime.update_state("doc", text)
        return "renamed " + text

    runtime.add_capability("doc", "rename", "Rename", "input-schema", "output-schema",
                           ["document.write"], lambda text: (bool(text), "empty"), rename)
    gc.collect()
    assert runtime.capabilities("alice", "doc")[0]["permissions"] == ["document.write"]
    expect_status(Status.validation_failed, lambda: runtime.invoke("alice", "doc", "rename", ""))
    expect_status(Status.denied, lambda: runtime.invoke("mallory", "doc", "rename", "name"))
    assert not calls
    assert runtime.invoke("alice", "doc", "rename", '새 이름 "x"') == 'renamed 새 이름 "x"'
    assert calls == ["alice"]
    assert seen[-2]["kind"] == "state_changed" and seen[-1]["kind"] == "command_completed"
    assert runtime.object("alice", "doc")["state"] == '새 이름 "x"'
    # A Python-authored procedure invokes the same registered application command.
    assert runtime.execute_procedure("alice", "rename-procedure",
        lambda app, text: app.invoke("doc", "rename", text), "procedure result") == "renamed procedure result"
    assert runtime.object("alice", "doc")["state"] == "procedure result"
    assert seen[-1]["kind"] == "procedure_completed" and seen[-1]["capability_id"] == "rename-procedure"
    saved_context = []
    assert runtime.execute_procedure("alice", "context-lifetime",
        lambda app, text: saved_context.append(app) or "done", "") == "done"
    try:
        saved_context[0].objects()
    except RuntimeError:
        pass
    else:
        raise AssertionError("procedure context must expire when its callback returns")
    expect_status(Status.handler_failed, lambda: runtime.execute_procedure(
        "alice", "throws", lambda app, text: 1 / 0, ""))
    assert seen[-1]["kind"] == "procedure_failed" and seen[-1]["capability_id"] == "throws"
    expect_status(Status.denied, lambda: runtime.execute_procedure(
        "mallory", "rename-procedure", lambda app, text: "unexpected", ""))
    allowed["value"] = False
    runtime.update_state("doc", "private change")
    before = len(seen)
    allowed["value"] = True
    runtime.unsubscribe(token)
    runtime.update_state("doc", "after unsubscribe")
    assert len(seen) == before
    runtime.remove_capability("doc", "rename")
    expect_status(Status.denied, lambda: runtime.invoke("alice", "doc", "rename", "x"))
    runtime.remove_object("doc")
    assert runtime.object("alice", "doc") is None
    try:
        runtime.add_object("bad\0id", "type")
    except ValueError:
        pass
    else:
        raise AssertionError("embedded NUL must be rejected")

try:
    runtime.objects("alice")
except RuntimeError:
    pass
else:
    raise AssertionError("closed runtime must not be reused")

# A host policy exception must deny rather than cross the native callback boundary.
with Runtime(lambda *args: 1 / 0) as faulty:
    faulty.add_object("x", "document")
    assert faulty.objects("alice") == []

# Invalid host callbacks are rejected during registration, not deferred to execution.
with Runtime(lambda *args: True) as invalid:
    invalid.add_object("x", "document")
    try:
        invalid.add_capability("x", "bad", "", "", "", [], None, lambda *args: "")
    except TypeError:
        pass
    else:
        raise AssertionError("validator must be callable")
