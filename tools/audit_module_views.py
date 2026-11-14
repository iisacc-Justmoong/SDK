#!/usr/bin/env python3
"""Report QML outside legacy throughout maintained SDK module trees."""
import argparse
import os
from pathlib import Path

SDK = Path(__file__).resolve().parents[1]
OMIT = {"legacy", "vendor", "vendors", "third_party", "third-party", "thirdparty",
        "external", "extern", "deps", "_deps", "dependencies", "build", "node_modules"}


def scan_sdk(root: Path) -> list[str]:
    root = Path(root)
    result: set[str] = set()
    for module in root.iterdir():
        if module.is_symlink() or not module.is_dir() or not (module.name.startswith("ii") or module.name == "LVUI"):
            continue
        pending = [module]
        while pending:
            with os.scandir(pending.pop()) as entries:
                for entry in entries:
                    name = entry.name
                    if entry.is_dir(follow_symlinks=False):
                        if (not name.startswith(".") and name.lower() not in OMIT
                                and not name.lower().startswith(("build-", "build_"))):
                            pending.append(Path(entry.path))
                    elif name.lower().endswith(".qml") and entry.is_file(follow_symlinks=False):
                        result.add(Path(entry.path).relative_to(root).as_posix())
    return sorted(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=SDK, help="SDK directory to inspect")
    args = parser.parse_args()
    violations = scan_sdk(args.sdk)
    for path in violations:
        print(path)
    print(f"Unisolated module QML files: {len(violations)}")
    return int(bool(violations))


if __name__ == "__main__":
    raise SystemExit(main())
