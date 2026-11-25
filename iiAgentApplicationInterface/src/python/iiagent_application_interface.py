"""Python host API for iiAgentApplicationInterface's C++ runtime; callbacks execute synchronously."""

import ctypes as _ct
import ctypes.util as _ctutil
from enum import IntEnum
import json as _json
import os as _os
from pathlib import Path as _Path


class Status(IntEnum):
    """Native operation result categories; failure does not imply rollback."""
    ok = 0
    invalid_argument = 1
    not_found = 2
    already_exists = 3
    denied = 4
    validation_failed = 5
    handler_failed = 6
    internal_error = 7


class AgentError(RuntimeError):
    """An operation failed with a native status and an optional host diagnostic."""
    def __init__(self, status, message=""):
        self.status = Status(status)
        self.message = message
        super().__init__(f"{self.status.name}: {message}")


_AUTH = _ct.CFUNCTYPE(_ct.c_int, _ct.c_void_p, _ct.c_char_p, _ct.c_int,
                      _ct.c_char_p, _ct.c_char_p, _ct.POINTER(_ct.c_char_p), _ct.c_size_t)
_VALIDATE = _ct.CFUNCTYPE(_ct.c_int, _ct.c_void_p, _ct.c_char_p, _ct.POINTER(_ct.c_char_p))
_HANDLE = _ct.CFUNCTYPE(_ct.c_int, _ct.c_void_p, _ct.c_char_p, _ct.c_char_p, _ct.POINTER(_ct.c_char_p))
_OBSERVE = _ct.CFUNCTYPE(None, _ct.c_void_p, _ct.c_int, _ct.c_char_p, _ct.c_char_p, _ct.c_uint64, _ct.c_uint64)
_SOURCE_COMPILE = _ct.CFUNCTYPE(_ct.c_int, _ct.c_void_p, _ct.c_char_p, _ct.c_char_p,
                                _ct.POINTER(_ct.c_uint64), _ct.POINTER(_ct.c_char_p))
_SOURCE_LOAD = _ct.CFUNCTYPE(_ct.c_int, _ct.c_void_p, _ct.c_uint64, _ct.POINTER(_ct.c_uint64),
                             _ct.POINTER(_ct.c_void_p), _ct.POINTER(_ct.c_char_p))
_SOURCE_RELEASE = _ct.CFUNCTYPE(None, _ct.c_void_p, _ct.c_uint64)


class _SourceBackend(_ct.Structure):
    """C callback table retained for as long as its compiled artifact exists."""
    _fields_ = [("size", _ct.c_size_t), ("compile", _SOURCE_COMPILE), ("load", _SOURCE_LOAD),
                ("release_artifact", _SOURCE_RELEASE), ("release_module", _SOURCE_RELEASE),
                ("user", _ct.c_void_p)]

_ACTIONS = ("read_object", "discover_capability", "execute", "observe",
            "read_native_object", "write_native_object", "execute_procedure", "compile_procedure", "load_procedure")
_EVENTS = ("object_added", "object_removed", "capability_added", "capability_removed",
           "state_changed", "command_completed", "command_failed", "procedure_completed", "procedure_failed")


def _encode(value):
    """Encode a C ABI text argument, refusing embedded NUL truncation."""
    if not isinstance(value, str):
        raise TypeError("iiAgentApplicationInterface text arguments must be strings")
    if "\0" in value:
        raise ValueError("iiAgentApplicationInterface text arguments cannot contain NUL")
    return value.encode("utf-8")


def _decode(value):
    """Copy borrowed UTF-8 callback text into a Python string."""
    return value.decode("utf-8") if value else ""


def _load_library():
    """Locate an explicit build, an installed sibling library, or a system library."""
    explicit = _os.environ.get("IIAGENTAPPLICATIONINTERFACE_LIBRARY")
    if explicit:
        library = _ct.CDLL(explicit)
    else:
        prefix = _Path(__file__).resolve().parents[3]
        names = (prefix / "lib" / "libiiAgentApplicationInterface.dylib", prefix / "lib" / "libiiAgentApplicationInterface.so",
                 prefix / "lib64" / "libiiAgentApplicationInterface.so", prefix / "bin" / "iiAgentApplicationInterface.dll")
        candidate = next((str(path) for path in names if path.exists()), None)
        candidate = candidate or _ctutil.find_library("iiAgentApplicationInterface")
        if not candidate:
            raise RuntimeError("iiAgentApplicationInterface library not found; set IIAGENTAPPLICATIONINTERFACE_LIBRARY")
        library = _ct.CDLL(candidate)
    ptr, text, output = _ct.c_void_p, _ct.c_char_p, _ct.POINTER(_ct.c_char_p)
    signatures = {
        "iiagent_application_interface_create": (ptr, [_AUTH, ptr]),
        "iiagent_application_interface_destroy": (None, [ptr]),
        "iiagent_application_interface_free": (None, [ptr]),
        "iiagent_application_interface_add_object": (_ct.c_int, [ptr, text, text, text, text]),
        "iiagent_application_interface_remove_object": (_ct.c_int, [ptr, text]),
        "iiagent_application_interface_update_state": (_ct.c_int, [ptr, text, text]),
        "iiagent_application_interface_add_capability": (_ct.c_int, [ptr, text, text, text, text, text,
            _ct.POINTER(text), _ct.c_size_t, _VALIDATE, _HANDLE, ptr]),
        "iiagent_application_interface_remove_capability": (_ct.c_int, [ptr, text, text]),
        "iiagent_application_interface_objects": (_ct.c_int, [ptr, text, output]),
        "iiagent_application_interface_object": (_ct.c_int, [ptr, text, text, output]),
        "iiagent_application_interface_capabilities": (_ct.c_int, [ptr, text, text, output]),
        "iiagent_application_interface_procedure_authoring_context": (_ct.c_int, [ptr, text, output]),
        "iiagent_application_interface_compose_procedure_source": (_ct.c_int, [ptr, text, text, text, output]),
        "iiagent_application_interface_invoke": (_ct.c_int, [ptr, text, text, text, text, output]),
        "iiagent_application_interface_execute_procedure": (_ct.c_int, [ptr, text, text, text, _HANDLE, ptr, output]),
        "iiagent_application_interface_execute_source": (_ct.c_int,
            [ptr, text, text, text, _ct.POINTER(_SourceBackend), text, output]),
        "iiagent_application_interface_build_source": (_ct.c_int,
            [ptr, text, text, text, _ct.POINTER(_SourceBackend), _ct.POINTER(ptr), output]),
        "iiagent_application_interface_build_procedure": (_ct.c_int,
            [ptr, text, text, text, text, _ct.POINTER(_SourceBackend), _ct.POINTER(ptr), output]),
        "iiagent_application_interface_execute_built": (_ct.c_int, [ptr, text, ptr, text, output]),
        "iiagent_application_interface_destroy_compiled": (None, [ptr]),
        "iiagent_application_interface_subscribe": (_ct.c_uint64, [ptr, text, _OBSERVE, ptr]),
        "iiagent_application_interface_unsubscribe": (None, [ptr, _ct.c_uint64]),
    }
    for name, (result, arguments) in signatures.items():
        function = getattr(library, name)
        function.restype, function.argtypes = result, arguments
    return library


class _SourceCallbacks:
    """Own C callbacks and opaque backend resources across all uses of one artifact."""
    def __init__(self, runtime, backend):
        if not all(callable(getattr(backend, name, None)) for name in ("compile", "load", "release", "unload")):
            raise TypeError("backend must provide compile, load, release and unload methods")
        self.runtime = runtime
        self.backend = backend
        self.artifacts = {}
        self.modules = {}
        self.next_handle = 1
        self.diagnostic_buffer = None

        @_SOURCE_COMPILE
        def compile_source(_user, identifier, text, output, error):
            runtime._callback_depth += 1
            artifact = None
            key = None
            try:
                artifact = backend.compile(_decode(identifier), _decode(text))
                key = self._handle()
                self.artifacts[key] = artifact
                output[0] = key
                return 0
            except BaseException as exc:
                if key is not None:
                    self.artifacts.pop(key, None)
                if artifact is not None:
                    try:
                        backend.release(artifact)
                    except BaseException:
                        pass
                self._diagnostic(error, exc)
                return 1
            finally:
                runtime._callback_depth -= 1

        @_SOURCE_LOAD
        def load_source(_user, artifact_id, output, descriptor, error):
            runtime._callback_depth += 1
            module = None
            key = None
            try:
                module = backend.load(self.artifacts[artifact_id])
                address = int(module.descriptor_address)
                if not address:
                    raise RuntimeError("module has no procedure descriptor")
                key = self._handle()
                self.modules[key] = module
                output[0] = key
                descriptor[0] = address
                return 0
            except BaseException as exc:
                if key is not None:
                    self.modules.pop(key, None)
                if module is not None:
                    try:
                        backend.unload(module)
                    except BaseException:
                        pass
                self._diagnostic(error, exc)
                return 1
            finally:
                runtime._callback_depth -= 1

        @_SOURCE_RELEASE
        def release_artifact(_user, key):
            runtime._callback_depth += 1
            try:
                backend.release(self.artifacts.pop(key))
            except BaseException:
                pass
            finally:
                runtime._callback_depth -= 1

        @_SOURCE_RELEASE
        def release_module(_user, key):
            runtime._callback_depth += 1
            try:
                backend.unload(self.modules.pop(key))
            except BaseException:
                pass
            finally:
                runtime._callback_depth -= 1

        self.table = _SourceBackend(_ct.sizeof(_SourceBackend), compile_source, load_source,
                                    release_artifact, release_module, None)

    def _handle(self):
        """Allocate one nonzero handle within this callback table's lifetime."""
        value = self.next_handle
        self.next_handle += 1
        return value

    def _diagnostic(self, output, error):
        """Keep a replacement-safe UTF-8 diagnostic alive across the native callback."""
        try:
            try:
                message = str(error).replace("\0", "\\0")[:8192]
            except BaseException:
                message = "source backend failed"
            self.diagnostic_buffer = _ct.create_string_buffer(message.encode("utf-8", "replace"))
            output[0] = _ct.cast(self.diagnostic_buffer, _ct.c_char_p)
        except BaseException:
            pass

    def dispose(self):
        """Break callback reference cycles after native code has released its table."""
        self.table = None
        self.diagnostic_buffer = None
        self.runtime = None
        self.backend = None


class CompiledProcedure:
    """Own a reusable native procedure and retain its host backend callbacks."""
    def __init__(self, runtime, pointer, callbacks, diagnostic):
        self._pointer = None
        self._runtime = runtime
        self._callbacks = callbacks
        self._active_calls = 0
        self.diagnostic = diagnostic
        self._pointer = pointer

    def execute(self, principal, input_text=""):
        """Reauthorize module loading and execution for the supplied principal."""
        if not self._pointer:
            raise RuntimeError("compiled procedure is closed")
        self._active_calls += 1
        try:
            return self._runtime._output(self._runtime._lib.iiagent_application_interface_execute_built,
                                         _encode(principal), self._pointer, _encode(input_text))
        finally:
            self._active_calls -= 1

    def close(self):
        """Release this artifact once; its own active execution must finish first."""
        if self._active_calls:
            raise RuntimeError("cannot close a compiled procedure during its execution")
        if self._pointer:
            pointer, self._pointer = self._pointer, None
            callbacks, self._callbacks = self._callbacks, None
            try:
                self._runtime._lib.iiagent_application_interface_destroy_compiled(pointer)
            finally:
                callbacks.dispose()

    def __enter__(self):
        """Use the compiled artifact as a deterministic context manager."""
        if not self._pointer:
            raise RuntimeError("compiled procedure is closed")
        return self

    def __exit__(self, _type, _value, _traceback):
        """Release the compiled artifact when its owning scope exits."""
        self.close()

    def __del__(self):
        """Best-effort cleanup; explicit close remains the preferred ownership contract."""
        try:
            self.close()
        except BaseException:
            pass


class ProcedureContext:
    """A scoped view of an agent's authorized application objects and commands."""
    def __init__(self, runtime, principal):
        self._runtime = runtime
        self._principal = principal
        self._open = True

    def _check(self):
        """Reject use after the surrounding synchronous procedure has returned."""
        if not self._open:
            raise RuntimeError("procedure context is no longer active")

    @property
    def subject(self):
        """Return the host-authenticated principal for this procedure."""
        self._check()
        return self._principal

    def objects(self):
        """List objects visible to this procedure's principal."""
        self._check()
        return self._runtime.objects(self._principal)

    def object(self, object_id):
        """Read one authorized application object snapshot."""
        self._check()
        return self._runtime.object(self._principal, object_id)

    def capabilities(self, object_id):
        """Discover the principal's authorized commands for one object."""
        self._check()
        return self._runtime.capabilities(self._principal, object_id)

    def invoke(self, object_id, capability_id, input_text):
        """Execute the application's existing command with the same principal."""
        self._check()
        return self._runtime.invoke(self._principal, object_id, capability_id, input_text)


class Runtime:
    """Own a single-threaded native registry; host code must authenticate principals.

    The policy receives principal, action, object id, capability id and permissions.
    A missing or raising policy denies access. Use as a context manager and keep all
    calls on one thread. Callbacks must not destroy the runtime while it is active.
    """
    def __init__(self, authorizer=None):
        if authorizer is not None and not callable(authorizer):
            raise TypeError("authorizer must be callable")
        self._lib = _load_library()
        self._callbacks = []
        self._callback_depth = 0
        self._ptr = None
        if authorizer is None:
            self._policy = _AUTH()
        else:
            @_AUTH
            def policy(_user, principal, action, obj, capability, permissions, count):
                self._callback_depth += 1
                try:
                    return int(bool(authorizer(_decode(principal), _ACTIONS[action], _decode(obj),
                                               _decode(capability), [_decode(permissions[i]) for i in range(count)])))
                except BaseException:
                    return 0
                finally:
                    self._callback_depth -= 1
            self._policy = policy
        self._ptr = self._lib.iiagent_application_interface_create(self._policy, None)
        if not self._ptr:
            raise RuntimeError("could not create iiAgentApplicationInterface runtime")

    def _active(self):
        """Return the native handle or reject use after explicit closure."""
        if not self._ptr:
            raise RuntimeError("iiAgentApplicationInterface runtime is closed")
        return self._ptr

    def _status(self, status, message=""):
        """Translate a native status to the Python exception contract."""
        if status != Status.ok:
            raise AgentError(status, message)

    def _output(self, function, *arguments):
        """Copy and free native output even when an operation reports failure."""
        output = _ct.c_char_p()
        status = function(self._active(), *arguments, _ct.byref(output))
        try:
            value = _decode(output.value)
        finally:
            if output:
                self._lib.iiagent_application_interface_free(_ct.cast(output, _ct.c_void_p))
        self._status(status, value)
        return value

    def close(self):
        """Release the runtime and callbacks once, outside active callbacks."""
        if self._callback_depth:
            raise RuntimeError("cannot close iiAgentApplicationInterface during a callback")
        if self._ptr:
            self._lib.iiagent_application_interface_destroy(self._ptr)
            self._ptr = None
            self._callbacks.clear()

    def __enter__(self):
        """Return this open runtime for deterministic context-manager ownership."""
        self._active()
        return self

    def __exit__(self, _type, _value, _traceback):
        """Release the native runtime when the host's scope exits."""
        self.close()

    def __del__(self):
        """Best-effort fallback; callers should close explicitly."""
        try:
            self.close()
        except BaseException:
            pass

    def add_object(self, object_id, object_type, description="", state=""):
        """Register an application-owned object and its initial textual state."""
        self._status(self._lib.iiagent_application_interface_add_object(self._active(), *map(_encode, (object_id, object_type, description, state))))

    def remove_object(self, object_id):
        """Remove an object and all its registered capabilities."""
        self._status(self._lib.iiagent_application_interface_remove_object(self._active(), _encode(object_id)))

    def update_state(self, object_id, state):
        """Publish state changed by the trusted host; unchanged text emits no event."""
        self._status(self._lib.iiagent_application_interface_update_state(self._active(), _encode(object_id), _encode(state)))

    def add_capability(self, object_id, capability_id, description, input_schema, output_schema,
                       permissions, validator, handler):
        """Register host validation and execution functions for a semantic command.

        validator(input) returns bool or (bool, reason); handler(principal, input)
        returns textual output. Exceptions become validation or handler failures.
        Callbacks are retained until this runtime closes, including after removal.
        """
        if not callable(validator) or not callable(handler):
            raise TypeError("validator and handler must be callable")
        if isinstance(permissions, (str, bytes)):
            raise TypeError("permissions must be a sequence of strings")
        encoded = [_encode(item) for item in permissions]
        array = (_ct.c_char_p * len(encoded))(*encoded)
        buffers = {}

        @_VALIDATE
        def validate(_user, input_text, reason):
            self._callback_depth += 1
            try:
                result = validator(_decode(input_text))
                accepted, message = result if isinstance(result, tuple) else (bool(result), "")
                buffers["reason"] = _ct.create_string_buffer(_encode(message))
                reason[0] = _ct.cast(buffers["reason"], _ct.c_char_p)
                return int(bool(accepted))
            except BaseException:
                return 0
            finally:
                self._callback_depth -= 1

        @_HANDLE
        def run(_user, principal, input_text, output):
            self._callback_depth += 1
            try:
                value = handler(_decode(principal), _decode(input_text))
                buffers["output"] = _ct.create_string_buffer(_encode(value))
                output[0] = _ct.cast(buffers["output"], _ct.c_char_p)
                return 0
            except BaseException:
                return 1
            finally:
                self._callback_depth -= 1

        self._status(self._lib.iiagent_application_interface_add_capability(self._active(),
            *map(_encode, (object_id, capability_id, description, input_schema, output_schema)),
            array, len(encoded), validate, run, None))
        self._callbacks.extend((validate, run, buffers))

    def remove_capability(self, object_id, capability_id):
        """Remove one command; retained callback storage is released on close."""
        self._status(self._lib.iiagent_application_interface_remove_capability(self._active(), _encode(object_id), _encode(capability_id)))

    def objects(self, principal):
        """Return the current authorized object snapshots as Python dictionaries."""
        return _json.loads(self._output(self._lib.iiagent_application_interface_objects, _encode(principal)))

    def object(self, principal, object_id):
        """Return one readable object snapshot or None if unavailable."""
        return _json.loads(self._output(self._lib.iiagent_application_interface_object, _encode(principal), _encode(object_id)))

    def capabilities(self, principal, object_id):
        """Return discoverable capability metadata for a readable object."""
        return _json.loads(self._output(self._lib.iiagent_application_interface_capabilities, _encode(principal), _encode(object_id)))

    def invoke(self, principal, object_id, capability_id, input_text):
        """Authorize, validate and execute one command, returning textual output."""
        return self._output(self._lib.iiagent_application_interface_invoke, *map(_encode, (principal, object_id, capability_id, input_text)))

    def execute_procedure(self, principal, procedure_id, procedure, input_text=""):
        """Run a trusted Python callable under a separately authorized procedure identity.

        The callable receives a scoped ProcedureContext and input text. Its command
        calls use the same native runtime, identity, validation, and authorization.
        The callable is trusted host code, not an isolated execution environment.
        """
        if not callable(procedure):
            raise TypeError("procedure must be callable")
        buffers = {}

        @_HANDLE
        def run(_user, caller, text, output):
            self._callback_depth += 1
            context = None
            try:
                context = ProcedureContext(self, _decode(caller))
                value = procedure(context, _decode(text))
                buffers["output"] = _ct.create_string_buffer(_encode(value))
                output[0] = _ct.cast(buffers["output"], _ct.c_char_p)
                return 0
            except BaseException:
                return 1
            finally:
                if context is not None:
                    context._open = False
                self._callback_depth -= 1

        return self._output(self._lib.iiagent_application_interface_execute_procedure,
                            _encode(principal), _encode(procedure_id), _encode(input_text), run, None)

    def execute_source(self, principal, procedure_id, source, backend, input_text=""):
        """Compile, load and run trusted source once using a host-supplied backend."""
        callbacks = _SourceCallbacks(self, backend)
        try:
            return self._output(self._lib.iiagent_application_interface_execute_source,
                                _encode(principal), _encode(procedure_id), _encode(source),
                                _ct.byref(callbacks.table), _encode(input_text))
        finally:
            callbacks.dispose()

    def procedure_authoring_context(self, principal):
        """Return the authorized object/command snapshot and C++ procedure contract."""
        return _json.loads(self._output(self._lib.iiagent_application_interface_procedure_authoring_context,
                                        _encode(principal)))

    def compose_procedure_source(self, procedure_id, body, *, preamble=""):
        """Assemble a full native module from trusted agent-authored C++ components."""
        return self._output(self._lib.iiagent_application_interface_compose_procedure_source,
                            _encode(procedure_id), _encode(preamble), _encode(body))

    def _build(self, function, arguments, backend):
        """Compile without loading and transfer callback ownership to the returned object."""
        callbacks = _SourceCallbacks(self, backend)
        pointer, output = _ct.c_void_p(), _ct.c_char_p()
        transferred = False
        try:
            status = function(self._active(), *arguments, _ct.byref(callbacks.table),
                              _ct.byref(pointer), _ct.byref(output))
            try:
                diagnostic = _decode(output.value)
            finally:
                if output:
                    self._lib.iiagent_application_interface_free(_ct.cast(output, _ct.c_void_p))
            self._status(status, diagnostic)
            if not pointer:
                raise RuntimeError("compiler returned no procedure handle")
            procedure = CompiledProcedure(self, pointer, callbacks, diagnostic)
            transferred = True
            return procedure
        except BaseException:
            if pointer:
                self._lib.iiagent_application_interface_destroy_compiled(pointer)
            raise
        finally:
            if not transferred:
                callbacks.dispose()

    def build_source(self, principal, procedure_id, source, backend):
        """Compile a complete trusted module once for later authorized execution."""
        return self._build(self._lib.iiagent_application_interface_build_source,
                           tuple(map(_encode, (principal, procedure_id, source))), backend)

    def build_procedure(self, principal, procedure_id, body, backend, *, preamble=""):
        """Assemble and compile an agent-authored C++ body for repeated execution."""
        return self._build(self._lib.iiagent_application_interface_build_procedure,
                           tuple(map(_encode, (principal, procedure_id, preamble, body))), backend)

    def subscribe(self, principal, observer):
        """Subscribe to currently authorized changes and return its nonzero token."""
        if not callable(observer):
            raise TypeError("observer must be callable")
        @_OBSERVE
        def receive(_user, kind, obj, capability, instance, revision):
            self._callback_depth += 1
            try:
                observer({"kind": _EVENTS[kind], "object_id": _decode(obj), "capability_id": _decode(capability),
                          "instance": instance, "revision": revision})
            except BaseException:
                pass
            finally:
                self._callback_depth -= 1
        self._callbacks.append(receive)
        token = self._lib.iiagent_application_interface_subscribe(self._active(), _encode(principal), receive, None)
        if not token:
            raise RuntimeError("could not subscribe to iiAgentApplicationInterface events")
        return token

    def unsubscribe(self, token):
        """Stop future events for this subscription; unknown tokens are ignored."""
        self._lib.iiagent_application_interface_unsubscribe(self._active(), token)
