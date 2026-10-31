"""Own disposable SDK test inputs under resources, not the shared build tree."""
import os
from pathlib import Path
import sys

SDK = Path(__file__).resolve().parents[1]
provider_dir = os.environ.get("IIFILEPROVIDER_PYTHON_DIRECTORY") or str(
    Path.home() / ".local/SDK/iiFileProvider/share/iiFileProvider/python")
if provider_dir not in sys.path:
    sys.path.insert(0, provider_dir)
import ii_file_provider as provider


def root():
    resources = SDK / "resources"
    state = resources / ".state"
    directory = state / "test-workspaces"
    if any(path.is_symlink() for path in (resources, state, directory)):
        raise ValueError("SDK test resource directories cannot be symlinks")
    provider.mkdir(directory, parents=True, exist_ok=True)
    return directory


def temporary(prefix="sdk-test-"):
    if not prefix or "/" in prefix or "\\" in prefix or prefix in (".", ".."):
        raise ValueError("Test prefix must be a single name")
    return provider.TemporaryDirectory(prefix=prefix, dir=root())


def compiled(project, prefix="sdk-test-build-"):
    project = Path(project).resolve(strict=True)
    build = project / "build"
    if build.is_symlink():
        raise ValueError("SDK test build directory cannot be a symlink")
    provider.mkdir(build, parents=True, exist_ok=True)
    return provider.TemporaryDirectory(prefix=prefix, dir=build)
