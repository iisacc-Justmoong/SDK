"""Exercise the durable store across separate processes in project resources."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

provider_dir = os.environ.get("IIFILEPROVIDER_PYTHON_DIRECTORY") or str(
    Path.home() / ".local/SDK/iiFileProvider/share/iiFileProvider/python")
sys.path.insert(0, provider_dir)
import ii_file_provider as provider

supplied_root = Path(sys.argv[1])
if supplied_root.is_symlink():
    raise RuntimeError("resource root is redirected")
root = supplied_root.resolve(strict=True)
base = root / "test-workspaces"
provider.mkdir(base, parents=True, exist_ok=True)
work = base / uuid.uuid4().hex
provider.mkdir(work)
try:
    database = work / "jobs.sqlite3"
    provider.write_bytes(database, b"")
    provider.write_bytes(work / "foreign.sqlite3", b"foreign data")
    for phase in ("first", "retry", "recovery"):
        subprocess.run([sys.argv[2], phase, str(database)], cwd=work, check=True)
finally:
    shutil.rmtree(work)
