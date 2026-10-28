#!/usr/bin/env python3
# Workspace consumer: supplies file publication to the independent iiGPU tool.
import importlib.util
import os
from pathlib import Path
import sys

def main():
    sdk = Path(__file__).resolve().parents[1]
    for candidate in (sdk / "iiFileProvider/python",
                      Path(os.environ.get("IIFILEPROVIDER_PREFIX", Path.home() / ".local/SDK/iiFileProvider")) / "share/iiFileProvider/python"):
        if (candidate / "ii_file_provider/__init__.py").is_file():
            sys.path.insert(0, str(candidate))
            break
    import ii_file_provider as storage

    spec = importlib.util.spec_from_file_location("ii_gpu_fetch_dawn", sdk / "iiGPU/tools/fetch_dawn.py")
    fetch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fetch)
    return fetch.main(storage)

if __name__ == "__main__":
    main()
