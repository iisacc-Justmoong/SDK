"""Inventory local Qt declarations without traversing weights or build trees.

This is a lexical inventory, not a C++ semantic dependency analyzer. Build and
runtime dependency checks must accompany it. Output stays inside Workspace.
"""
import argparse
import json
import re
from pathlib import Path

QT_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.M)
QT_TARGET = re.compile(r'\bQt[56]::[A-Za-z0-9_]+')


def inventory(workspace):
    sdk = workspace / "SDK"
    modules = sorted(p for p in sdk.iterdir() if (p / "CMakeLists.txt").is_file())
    product = workspace / "Product/Dreamscapes"
    if product.is_dir():
        modules.append(product)
    result = {}
    for module in modules:
        candidates = {module / "CMakeLists.txt"}
        candidates.update(module.glob("cmake/*.cmake*"))
        for name in ("src", "Src", "Native/src", "CoreIO/src", "tests", "Test"):
            directory = module / name
            if directory.is_dir():
                candidates.update(p for p in directory.rglob("*")
                                  if p.suffix in {".h", ".hpp", ".cpp", ".mm", ".cc", ".cxx"}
                                  and "build" not in p.relative_to(module).parts)
        candidates.update(module.glob("*.h"))
        rows = []
        seen_files = set()
        for path in sorted(candidates):
            if not path.is_file() or path.is_symlink() or path.stat().st_size > 1024 * 1024:
                continue
            identity = (path.stat().st_dev, path.stat().st_ino)
            if identity in seen_files:
                continue
            seen_files.add(identity)
            text = path.read_text(errors="replace")
            headers = []
            for name in QT_INCLUDE.findall(text):
                if (path.parent / name).is_file():
                    continue
                if re.match(r"^(Q[A-Z]|Qt(?:Core|Gui|Quick|Qml|Concurrent|Network|Sql|Multimedia|Svg|Bluetooth|WebSockets|Test)(?:/|$)|Qt(?:Global|Endian)$)", name):
                    headers.append(name)
            targets = sorted(set(QT_TARGET.findall(text)))
            if headers or targets:
                relative = path.relative_to(module)
                rows.append({"path": str(path.relative_to(workspace)),
                             "test": any(p.lower() in {"tests", "test"} for p in relative.parts)
                                     or path.name.startswith("tst_"),
                             "qtHeaders": sorted(set(headers)), "declaredQtTargets": targets})
        result[str(module.relative_to(workspace))] = rows
    return {"kind": "lexical-source-inventory", "includesUntrackedSource": True,
            "limitations": "Declarations can be optional or test-only; this is not proof of an active link edge.",
            "modules": result}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = inventory(args.workspace.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
    print(f"Inventoried {len(data['modules'])} module source roots: {args.output}")
