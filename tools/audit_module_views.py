#!/usr/bin/env python3
"""Report QML outside legacy throughout maintained SDK module trees."""
import argparse
import os
import re
from pathlib import Path

SDK = Path(__file__).resolve().parents[1]
OMIT = {"legacy", "vendor", "vendors", "third_party", "third-party", "thirdparty",
        "external", "extern", "deps", "_deps", "dependencies", "build", "node_modules",
        "site-packages", "dist-packages", "__pycache__"}
LEGACY_PROJECTS = {"lvrs", "lvrs-svelte"}
# iiGPU stages the upstream Dawn distribution here; this is not an SDK view
# source. Keep this path-specific so similarly named application resources are
# still audited.
EXTERNAL_TREES = {"iiGPU/resources/dawn"}
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".m", ".mm", ".py", ".js"}
# Recognize raw and escaped source strings, without treating Qt Quick headers
# or a reference to an isolated .qml path as a view declaration.
QML_IMPORT = re.compile(r"\bimport[ \t]+(?:QtQuick|QtQml|LVRS)(?=[. \t\r\n\\\"']|$)")


def omitted_directory(name: str) -> bool:
    lower = name.lower()
    return (name.startswith(".") or lower in OMIT
            or lower.startswith(("build-", "build_")))


def module_files(root: Path):
    root = Path(root)
    for module in root.iterdir():
        if (module.is_symlink() or not module.is_dir() or omitted_directory(module.name)
                or module.name.lower() in LEGACY_PROJECTS):
            continue
        pending = [module]
        while pending:
            with os.scandir(pending.pop()) as entries:
                for entry in entries:
                    name = entry.name
                    if entry.is_dir(follow_symlinks=False):
                        directory = Path(entry.path)
                        if (not omitted_directory(name)
                                and directory.relative_to(root).as_posix() not in EXTERNAL_TREES):
                            pending.append(directory)
                    elif entry.is_file(follow_symlinks=False):
                        yield Path(entry.path), Path(entry.path).relative_to(root).as_posix()


def scan_sdk(root: Path) -> list[str]:
    return sorted(relative for path, relative in module_files(root) if path.suffix.lower() == ".qml")


def scan_embedded_qml(root: Path) -> list[str]:
    result = []
    for path, relative in module_files(root):
        if path.suffix.lower() in SOURCE_SUFFIXES:
            text = path.read_text(encoding="utf-8", errors="replace")
            if QML_IMPORT.search(text):
                result.append(relative)
    return sorted(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=SDK, help="SDK directory to inspect")
    args = parser.parse_args()
    violations = scan_sdk(args.sdk)
    embedded = scan_embedded_qml(args.sdk)
    for path in violations:
        print(path)
    print(f"Unisolated module QML files: {len(violations)}")
    for path in embedded:
        print(path)
    print(f"Unisolated embedded QML import declarations: {len(embedded)}")
    return int(bool(violations or embedded))


if __name__ == "__main__":
    raise SystemExit(main())
