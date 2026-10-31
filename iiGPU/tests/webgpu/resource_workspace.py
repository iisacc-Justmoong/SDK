"""Keep WebGPU test fixtures and compiled consumers in their proper locations."""
import os
from pathlib import Path
import sys


provider_dir = os.environ.get("IIFILEPROVIDER_PYTHON_DIRECTORY") or str(
    Path.home() / ".local/SDK/iiFileProvider/share/iiFileProvider/python")
if provider_dir not in sys.path:
    sys.path.insert(0, provider_dir)
import ii_file_provider as provider


def temporary(project, prefix="webgpu-test-"):
    project = Path(project).resolve(strict=True)
    resources = project / "resources"
    state = resources / ".state"
    directory = state / "test-workspaces"
    if any(path.is_symlink() for path in (resources, state, directory)):
        raise ValueError("WebGPU test resource directories cannot be symlinks")
    provider.mkdir(directory, parents=True, exist_ok=True)
    return provider.TemporaryDirectory(prefix=prefix, dir=directory)


def compiled(build, prefix="webgpu-consumer-"):
    build = Path(build)
    if build.is_symlink():
        raise ValueError("WebGPU build directory cannot be a symlink")
    provider.mkdir(build, parents=True, exist_ok=True)
    return provider.TemporaryDirectory(prefix=prefix, dir=build)
