"""Compile source at runtime, load it, run it and clean its generated artifact."""
import gc
import importlib.util
import os
import sys
import tempfile
import weakref
from pathlib import Path

from iiagent_application_interface import AgentError, Runtime, Status
from iiagent_application_interface_native import NativeSourceBackend

root = Path(__file__).resolve().parents[1]
build = Path(os.environ["IIAGENTAPPLICATIONINTERFACE_TEST_BUILD"])
source = (root / "resources/source_runtime/rename.cpp").read_text(encoding="utf-8")
compiler = os.environ["IIAGENTAPPLICATIONINTERFACE_TEST_CXX"]
library = os.environ["IIAGENTAPPLICATIONINTERFACE_LIBRARY"]
include_flags = ["-I" + str(root / "src"), "-I" + str(root / "src/ModuleImports"),
                 "-I" + str(build / "public")]
link_flags = [library]
if os.name != "nt":
    link_flags += ["-Wl,-rpath," + str(build)]
provider = tempfile
provider_path = os.environ.get("IIAGENTAPPLICATIONINTERFACE_TEST_FILE_PROVIDER")
if provider_path:
    sys.path.insert(0, provider_path)
    import ii_file_provider as provider

spec = importlib.util.spec_from_file_location("python_source_storage", root / "example/python_source_storage.py")
example_storage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(example_storage)

class TestStorage(example_storage.IiFileProviderSourceStorage):
    """Exercise the application adapter with a provider-compatible test double."""
    def __init__(self):
        super().__init__(provider)
        self.created = 0
        self.removed = 0
    def create_artifact(self, directory, suffix):
        path = super().create_artifact(directory, suffix)
        self.created += 1
        return path
    def remove_artifact(self, path):
        super().remove_artifact(path)
        self.removed += 1

storage = TestStorage()
backend = NativeSourceBackend(compiler, build, storage=storage, compile_flags=include_flags, link_flags=link_flags,
                              unload_after_execution=True)
allow = {"compile": True, "load": True, "execute": True}
def policy(principal, action, obj, cap, permissions):
    """Grant each compilation, loading and execution stage independently."""
    if principal != "agent":
        return False
    return allow.get({"compile_procedure": "compile", "load_procedure": "load",
                      "execute_procedure": "execute"}.get(action), True)

def expect(status, function):
    """Require the exact native error category."""
    try:
        function()
    except AgentError as error:
        assert error.status == status, error
    else:
        raise AssertionError(f"expected {status}")

with Runtime(policy) as runtime:
    runtime.add_object("document", "Document", state="Draft")
    active_procedure, prevented_close = [None], []
    def rename(principal, text):
        """A callback cannot destroy the compiled artifact currently invoking it."""
        if active_procedure[0] is not None:
            try:
                active_procedure[0].close()
            except RuntimeError:
                prevented_close.append(True)
        return text + " by " + principal
    runtime.add_capability("document", "rename", "Rename", "text", "text", ["write"],
        lambda text: bool(text), rename)
    authoring = runtime.procedure_authoring_context("agent")
    assert authoring["format"] == "iiAgentApplicationInterface.procedure-authoring.v1"
    assert authoring["language"] == "C++23" and "invoke" in authoring["context_methods"]
    assert any(item["name"] == "invoke" and "std::string_view" in item["signature"]
               for item in authoring["context_api"])
    document = next(item for item in authoring["objects"] if item["object"]["id"] == "document")
    assert document["capabilities"][0]["id"] == "rename" and not document["native_readable"]
    assert runtime.procedure_authoring_context("stranger")["objects"] == []
    agent_id = 'draft."\\\n한글'
    body = 'return context.invoke("document", "rename", input);'
    complete = runtime.compose_procedure_source(agent_id, body)
    assert body in complete and agent_id not in complete and "iiagent_application_procedure_v1" in complete
    allow["compile"] = False
    expect(Status.denied, lambda: runtime.build_procedure("agent", agent_id, body, backend))
    allow["compile"] = True
    expect(Status.invalid_argument, lambda: runtime.build_procedure("agent", "empty", "", backend))
    expect(Status.handler_failed, lambda: runtime.build_procedure("agent", "invalid", "this is not C++;", backend))
    assert backend.active_artifacts == 0
    before_build = storage.created
    with runtime.build_procedure("agent", agent_id, body, backend) as built:
        active_procedure[0] = built
        gc.collect()
        assert backend.active_artifacts == 1 and backend.active_modules == 0
        allow["compile"] = False
        assert built.execute("agent", "First") == "First by agent"
        assert built.execute("agent", "Second") == "Second by agent"
        allow["load"] = False
        expect(Status.denied, lambda: built.execute("agent", "denied"))
        allow["load"] = True
        allow["execute"] = False
        expect(Status.denied, lambda: built.execute("agent", "denied"))
        allow["execute"] = True
        assert backend.active_artifacts == 1 and backend.active_modules == 0
        assert storage.created == before_build + 1 and len(prevented_close) == 2
        active_procedure[0] = None
    assert backend.active_artifacts == 0
    try:
        built.execute("agent", "after close")
    except RuntimeError:
        pass
    else:
        raise AssertionError("a closed compiled procedure cannot run")
    allow["compile"] = True
    with runtime.build_source("agent", "source.rename", source, backend) as authored_source:
        assert authored_source.execute("agent", "From source") == "From source by agent"
    allow["compile"] = False
    expect(Status.denied, lambda: runtime.execute_source("agent", "source.rename", source, backend, "x"))
    assert backend.active_artifacts == 0
    allow["compile"] = True
    allow["load"] = False
    expect(Status.denied, lambda: runtime.execute_source("agent", "source.rename", source, backend, "x"))
    assert backend.active_artifacts == 0
    allow["load"] = True
    assert runtime.execute_source("agent", "source.rename", source, backend, "Updated") == "Updated by agent"
    assert backend.active_artifacts == 0 and backend.active_modules == 0
    expect(Status.invalid_argument, lambda: runtime.execute_source("agent", "wrong-id", source, backend, "x"))
    assert backend.active_artifacts == 0 and backend.active_modules == 0
    expect(Status.handler_failed, lambda: runtime.execute_source("agent", "source.rename", "invalid c++", backend, "x"))
    assert backend.active_artifacts == 0

    retaining = NativeSourceBackend(compiler, build, storage=storage, compile_flags=include_flags, link_flags=link_flags)
    assert runtime.execute_source("agent", "source.rename", source, retaining, "Kept") == "Kept by agent"
    assert retaining.active_artifacts == 1 and retaining.active_modules == 1
    retaining.close()
    assert retaining.active_artifacts == 0 and retaining.active_modules == 0

    class OutsideStorage(TestStorage):
        """An output outside build/ must be rejected before the compiler runs."""
        def create_artifact(self, directory, suffix):
            return directory.parent / ("outside" + suffix)
    outside = NativeSourceBackend(compiler, build, storage=OutsideStorage(), compile_flags=include_flags, link_flags=link_flags)
    expect(Status.handler_failed, lambda: runtime.execute_source("agent", "source.rename", source, outside, "x"))
    assert outside.active_artifacts == 0 and outside.active_modules == 0

    # A driver that reports success without writing output is not a valid compilation.
    empty_output = NativeSourceBackend(sys.executable, build, storage=storage, compile_flags=["-c", "pass"])
    empty_artifact = None
    try:
        empty_artifact = empty_output.compile("source.rename", source)
    except RuntimeError:
        pass
    else:
        raise AssertionError("an empty output cannot be loaded as compiled code")
    finally:
        if empty_artifact is not None:
            empty_output.release(empty_artifact)
    assert empty_output.active_artifacts == 0

    class UnprintableError(Exception):
        """Exercise host exceptions that cannot be converted to ordinary UTF-8 text."""
        def __str__(self):
            raise RuntimeError("cannot format")
    class FailingBackend:
        """Backend whose diagnostic must not escape the C callback boundary."""
        def compile(self, identifier, text):
            raise UnprintableError()
        def load(self, artifact):
            raise AssertionError("load must not run")
        def release(self, artifact):
            raise AssertionError("no artifact exists")
        def unload(self, module):
            raise AssertionError("no module exists")
    expect(Status.handler_failed, lambda: runtime.execute_source("agent", "source.rename", source, FailingBackend(), "x"))

    # Callback tables must not retain host backends after failure or artifact closure.
    class MemoryBackend:
        """An in-memory backend sufficient to exercise callback ownership."""
        def __init__(self): self.released = 0
        def compile(self, identifier, text): return object()
        def load(self, artifact): raise AssertionError("load must not run")
        def release(self, artifact): self.released += 1
        def unload(self, module): raise AssertionError("load must not run")
    gc_enabled = gc.isenabled()
    gc.disable()
    try:
        failed_backend = FailingBackend()
        failed_reference = weakref.ref(failed_backend)
        expect(Status.handler_failed, lambda: runtime.execute_source("agent", "source.rename", source, failed_backend))
        del failed_backend
        assert failed_reference() is None
        memory_backend = MemoryBackend()
        memory_reference = weakref.ref(memory_backend)
        memory_procedure = runtime.build_source("agent", "memory", "source", memory_backend)
        del memory_backend
        assert memory_reference() is not None
        memory_procedure.close()
        del memory_procedure
        assert memory_reference() is None
    finally:
        if gc_enabled:
            gc.enable()

    # Dropping an unrelated compiled handle from a host callback must release it.
    disposal_backend = MemoryBackend()
    disposal_holder = [runtime.build_source("agent", "dispose.other", "source", disposal_backend)]
    def release_other(_context, _input):
        disposal_holder.clear()
        gc.collect()
        return "released"
    assert runtime.execute_procedure("agent", "dispose.test", release_other) == "released"
    assert disposal_backend.released == 1
# An artifact can be safely released after its owning runtime has closed.
closed_runtime = Runtime(policy)
late = closed_runtime.build_source("agent", "source.rename", source, backend)
closed_runtime.close()
try:
    late.execute("agent", "unavailable")
except RuntimeError:
    pass
else:
    raise AssertionError("a closed runtime cannot execute a compiled procedure")
late.close()
backend.close()
assert storage.created == storage.removed
