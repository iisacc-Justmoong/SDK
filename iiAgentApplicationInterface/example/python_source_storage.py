"""Application adapter connecting runtime compilation to iiFileProvider storage."""

from contextlib import contextmanager
import os
from pathlib import Path


class IiFileProviderSourceStorage:
    """Reserve compiler outputs through an app-supplied ii_file_provider module.

    The application provides the module so iiAgentApplicationInterface itself
    does not import or link another SDK. The provider's mkstemp must return an
    open file descriptor and an absolute path, like ii_file_provider.mkstemp.
    """

    def __init__(self, provider):
        """Store the app-selected provider without taking ownership of it."""
        self.provider = provider

    def create_artifact(self, directory, suffix):
        """Reserve a unique compiler output in the caller's build directory."""
        descriptor, path = self.provider.mkstemp(prefix="iiagent-procedure-", suffix=suffix, dir=directory)
        os.close(descriptor)
        return path

    @contextmanager
    def open_diagnostics(self, directory):
        """Provide and clean a seekable, provider-created compiler diagnostic file."""
        descriptor, path = self.provider.mkstemp(prefix="iiagent-diagnostic-", dir=directory)
        try:
            stream = os.fdopen(descriptor, "w+b")
        except BaseException:
            os.close(descriptor)
            Path(path).unlink(missing_ok=True)
            raise
        try:
            with stream:
                yield stream
        finally:
            Path(path).unlink(missing_ok=True)

    def remove_artifact(self, path):
        """Remove a generated output when its owning backend releases it."""
        Path(path).unlink(missing_ok=True)
