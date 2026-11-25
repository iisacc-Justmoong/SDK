"""Native C++ source compiler and loader for a trusted application's Python host."""

import _ctypes
import ctypes
import os
from pathlib import Path
import subprocess
import sys


class _Artifact:
    """One generated native library, owned until it and any loaded module are released."""
    def __init__(self, path):
        self.path = path
        self.released = False


class _Module:
    """A loaded library and its borrowed iiAgent procedure descriptor."""
    def __init__(self, library, descriptor_address, artifact):
        self.library = library
        self.descriptor_address = descriptor_address
        self.artifact = artifact


class NativeSourceBackend:
    """Compile source through a host-selected clang++-style driver and load it in-process.

    The application supplies a storage object whose create_artifact,
    open_diagnostics and remove_artifact methods manage build/ resources. It
    also supplies compiler flags, libraries and a matching C++ ABI. Native code
    has the application's privileges. Keep modules loaded unless the app knows
    the procedure did not retain executable references or background work.
    """
    def __init__(self, compiler, build_dir, *, storage, compile_flags=(), link_flags=(),
                 working_directory=None, timeout=60, max_source_bytes=1024 * 1024,
                 unload_after_execution=False):
        self.compiler = str(compiler)
        self.build_dir = Path(build_dir).resolve()
        if self.build_dir.name != "build" or not self.build_dir.is_dir():
            raise ValueError("build_dir must be an existing build/ directory")
        if not all(callable(getattr(storage, name, None)) for name in
                   ("create_artifact", "open_diagnostics", "remove_artifact")):
            raise TypeError("storage must create artifacts, open diagnostics and remove artifacts")
        self.storage = storage
        self.compile_flags = tuple(str(value) for value in compile_flags)
        self.link_flags = tuple(str(value) for value in link_flags)
        self.working_directory = str(working_directory or self.build_dir)
        self.timeout = timeout
        self.max_source_bytes = max_source_bytes
        self.unload_after_execution = unload_after_execution
        self._artifacts = set()
        self._modules = set()

    @property
    def active_artifacts(self):
        """Count generated artifacts that have not yet been removed."""
        return len(self._artifacts)

    @property
    def active_modules(self):
        """Count modules still held by this backend."""
        return len(self._modules)

    def compile(self, identifier, source):
        """Compile source from standard input without a shell or a generated source file."""
        del identifier
        encoded = source.encode("utf-8")
        if len(encoded) > self.max_source_bytes:
            raise ValueError("source exceeds the configured byte limit")
        suffix = ".dll" if os.name == "nt" else (".dylib" if sys.platform == "darwin" else ".so")
        artifact = _Artifact(Path(self.storage.create_artifact(self.build_dir, suffix)).resolve())
        if artifact.path.parent != self.build_dir:
            raise ValueError("the storage artifact must be directly inside build_dir")
        command = [self.compiler, *self.compile_flags, "-std=c++23", "-shared"]
        if os.name != "nt":
            command.append("-fPIC")
        command += ["-x", "c++", "-", "-x", "none", *self.link_flags, "-o", str(artifact.path)]
        try:
            with self.storage.open_diagnostics(self.build_dir) as diagnostic:
                result = subprocess.run(command, input=encoded, stdout=subprocess.DEVNULL, stderr=diagnostic,
                                        cwd=self.working_directory, timeout=self.timeout, check=False)
                diagnostic.seek(0)
                message = diagnostic.read(8192).decode("utf-8", "replace")
            if result.returncode != 0 or not artifact.path.is_file() or artifact.path.stat().st_size == 0:
                raise RuntimeError("procedure compilation failed: " + message.strip())
            self._artifacts.add(artifact)
            return artifact
        except BaseException:
            self.storage.remove_artifact(artifact.path)
            raise

    def load(self, artifact):
        """Load a compiled module and locate its standard C-linkage descriptor entry."""
        if artifact not in self._artifacts or artifact.released:
            raise ValueError("unknown or released procedure artifact")
        library = ctypes.CDLL(str(artifact.path))
        try:
            provider = getattr(library, "iiagent_application_procedure_v1")
            provider.restype = ctypes.c_void_p
            provider.argtypes = []
            descriptor = provider()
            if not descriptor:
                raise RuntimeError("procedure descriptor is null")
            module = _Module(library, descriptor, artifact)
            self._modules.add(module)
            return module
        except BaseException:
            self._close_library(library)
            raise

    def unload(self, module):
        """Release a module now or retain it until explicit backend closure."""
        if module not in self._modules:
            return
        if self.unload_after_execution:
            self._close_library(module.library)
            self._modules.remove(module)

    def release(self, artifact):
        """Remove an artifact once no retained module still depends on it."""
        if artifact not in self._artifacts:
            return
        artifact.released = True
        if not any(module.artifact is artifact for module in self._modules):
            self.storage.remove_artifact(artifact.path)
            self._artifacts.remove(artifact)

    def close(self):
        """Unload retained modules and remove their artifacts when the host deems it safe."""
        for module in list(self._modules):
            self._close_library(module.library)
            self._modules.remove(module)
        for artifact in list(self._artifacts):
            self.storage.remove_artifact(artifact.path)
            self._artifacts.remove(artifact)

    @staticmethod
    def _close_library(library):
        """Release an OS library handle only after the procedure has fully returned."""
        handle = library._handle
        if handle:
            if os.name == "nt":
                _ctypes.FreeLibrary(handle)
            else:
                _ctypes.dlclose(handle)
            library._handle = 0
