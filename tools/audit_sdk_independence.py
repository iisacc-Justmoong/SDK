#!/usr/bin/env python3
# Lexical SDK inventory. It does not claim to evaluate CMake or C++ semantics.
import argparse
import ast
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

ALIASES = {"iiSocialPublish": "iiSocialIntegration",
           "iiFileProviderNative": "iiFileProvider", "iiFileProviderCoreIO": "iiFileProvider",
           "iiXmlMarkup": "iiXml", "iiHtmlBlock": "iiXml", "iiWebGPU": "iiGPU",
           "iiBrushTools": "iiPaintEngine", "ii_file_provider": "iiFileProvider"}
EXCLUDED = {".git", "build", "docs", "tests", "test", "resources",
            "third_party", "third-party", "vendor", "node_modules", "__pycache__"}
SOURCE_EXTENSIONS = {".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}
MAX_BYTES = 4 * 1024 * 1024


def production_path(path):
    for component in path.parts:
        lowered = component.lower()
        if component.startswith(".") or lowered in EXCLUDED or lowered.startswith(("cmake-build", "build_", "build-")):
            return False
    return True


def prefilter_pattern(owner, names, aliases=ALIASES):
    references = {name for name in names if aliases.get(name, name) != owner}
    references.update(alias for alias, target in aliases.items() if target in names and target != owner)
    return "|".join(re.escape(name) for name in sorted(references, key=len, reverse=True)) or r"(?!)"


def candidate_paths(root, names, aliases=ALIASES):
    # rg avoids re-reading thousands of files with no possible SDK name. The
    # pure-Python walk is retained for hosts where rg is unavailable or fails.
    executable = shutil.which("rg")
    if executable:
        command = [executable, "--files-with-matches", "--null", "--hidden", "--no-ignore"]
        for suffix in sorted(SOURCE_EXTENSIONS):
            command.extend(("--iglob", "*" + suffix))
        for pattern in ("*.py", "*.cmake", "*.cmake.in", "CMakeLists.txt", "install-dependencies.txt"):
            command.extend(("--iglob", pattern))
        for directory in sorted(EXCLUDED | {"cmake-build*", "build_*", "build-*"}):
            command.extend(("--iglob", "!**/" + directory + "/**"))
        command.extend(("-e", prefilter_pattern(root.name, names, aliases), str(root)))
        try:
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False)
        except OSError:
            result = None
        if result is not None and result.returncode in (0, 1):
            paths = []
            for raw in result.stdout.split(b"\0"):
                if not raw:
                    continue
                path = Path(os.fsdecode(raw))
                try:
                    relative = path.relative_to(root)
                except ValueError:
                    continue
                if production_path(relative) and not path.is_symlink():
                    paths.append(path)
            return paths
    paths = []
    for current, dirs, filenames in os.walk(root, followlinks=False):
        current = Path(current)
        dirs[:] = [d for d in dirs if production_path((current / d).relative_to(root))
                   and not (current / d).is_symlink()]
        paths.extend(current / name for name in filenames)
    return paths


def owner_of(reference, names, aliases=ALIASES):
    # Headers sometimes use the module prefix without a directory separator.
    candidates = dict((name, name) for name in names)
    candidates.update((alias, owner) for alias, owner in aliases.items() if owner in names)
    segments = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", reference)
    for segment in segments:
        for prefix in sorted(candidates, key=len, reverse=True):
            if segment.startswith(prefix):
                suffix = segment[len(prefix):]
                if not suffix or suffix[0].isupper() or suffix[0] in "_0123456789":
                    return candidates[prefix]
    return None


def cmake_references(text, owner, names, aliases=ALIASES):
    # CMake commands are inventoried lexically. Variable expansion and nested
    # generator expressions require a configured target-graph inspection.
    text = re.sub(r"#[^\n]*", "", text)
    pattern = re.compile(r"\b(find_package|find_dependency|target_link_libraries|link_libraries|"
                         r"target_include_directories|include_directories|target_sources|"
                         r"add_subdirectory|add_module|add_modules|add_library|add_executable|include)"
                         r"\s*\(([^)]*)\)", re.I | re.S)
    rows = []
    for match in pattern.finditer(text):
        command, args = match.group(1), match.group(2)
        if command.lower() in {"find_package", "find_dependency"}:
            tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)?", args)[:1]
        else:
            tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)?", args)
        for token in tokens:
            module = owner_of(token, names, aliases)
            if module and module != owner:
                rows.append({"module": module, "kind": command.lower(), "reference": token,
                             "line": text.count("\n", 0, match.start()) + 1})
    return rows


def cpp_references(text, owner, names, aliases=ALIASES):
    pattern = re.compile(r'^\s*#\s*(?:include|import)\s*[<"]([^>"\n]+)[>"]', re.M)
    rows = []
    for match in pattern.finditer(text):
        module = owner_of(match.group(1), names, aliases)
        if module and module != owner:
            rows.append({"module": module, "kind": "header", "reference": match.group(1),
                         "line": text.count("\n", 0, match.start()) + 1})
    return rows


def python_references(text, owner, names, aliases=ALIASES):
    rows = []
    try:
        tree = ast.parse(text)
    except (SyntaxError, ValueError):
        return rows
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imports = [alias.name for alias in node.names]
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            imports = [node.module]
        else:
            continue
        for reference in imports:
            module = owner_of(reference, names, aliases)
            if module and module != owner:
                rows.append({"module": module, "kind": "python-import", "reference": reference,
                             "line": node.lineno})
    return rows


def select_roots(roots, selected, aliases=ALIASES):
    if not selected:
        return roots
    resolved = {aliases.get(name, name) for name in selected}
    unknown = resolved - {root.name for root in roots}
    if unknown:
        raise ValueError("Unknown SDK module(s): " + ", ".join(sorted(unknown)))
    return [root for root in roots if root.name in resolved]


def build_uses_directory(project_text, directory):
    project_text = re.sub(r"#[^\n]*", "", project_text)
    commands = re.compile(r"\b(?:add_subdirectory|include|add_executable|add_library|target_sources|"
                          r"target_include_directories|include_directories|add_custom_command|"
                          r"add_custom_target|execute_process)\s*\(([^)]*)\)", re.I | re.S)
    token = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(directory) + r"(?=[/\\\s)\"])", re.I)
    return any(token.search(match.group(1) + ")") for match in commands.finditer(project_text))


def detached_application(project_text, application_text, directory):
    return ("# iisdk:consumer-application" in application_text
            and bool(re.search(r"\bproject\s*\(", application_text, re.I))
            and not build_uses_directory(project_text, directory))


def detached_example(project_text, directory):
    return not build_uses_directory(project_text, directory)


def owned_modules(project_text, directory_name, names):
    # A checkout may use a former directory name while exporting a package
    # named by project(). Both names identify that checkout's own code.
    project_text = re.sub(r"#[^\n]*", "", project_text)
    match = re.search(r"\bproject\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", project_text, re.I)
    owned = {directory_name}
    if match and match.group(1) in names:
        owned.add(match.group(1))
    return owned


def inventory(sdk, selected=None):
    entries = [p for p in sdk.iterdir() if p.is_dir()
               and (p.name.startswith("ii") or p.name in {"LVRS", "LVUI", "LVRS-Svelte"})]
    roots = sorted(p for p in entries if not p.is_symlink())
    names = {p.name for p in roots}
    aliases = dict(ALIASES)
    resolved_sdk = sdk.resolve()
    for entry in entries:
        if entry.is_symlink():
            target = entry.resolve()
            if target.parent == resolved_sdk and target.name in names:
                aliases[entry.name] = target.name
                names.add(entry.name)
    roots = select_roots(roots, selected, aliases)
    selected_names = {root.name for root in roots}
    modules = {}
    skipped = []
    for root in roots:
        rows = []
        files = 0
        detached = set()
        app_cmake = root / "tools/CMakeLists.txt"
        root_cmake = root / "CMakeLists.txt"
        project_text = ""
        if root_cmake.is_file():
            try:
                project_text = root_cmake.read_text(errors="replace")
            except OSError:
                skipped.append(str(root_cmake.relative_to(sdk)))
        owned = owned_modules(project_text, root.name, names)
        if app_cmake.is_file() and root_cmake.is_file():
            try:
                if detached_application(project_text, app_cmake.read_text(errors="replace"), "tools"):
                    detached.add("tools")
            except OSError:
                skipped.append(str(app_cmake.relative_to(sdk)))
        if project_text:
            for entry in root.iterdir():
                if entry.is_dir() and not entry.is_symlink() and entry.name.lower() in {"example", "examples"}:
                    if detached_example(project_text, entry.name):
                        detached.add(entry.name)
        detached_projects = {}

        def in_detached_project(relative):
            for parent in relative.parents:
                if parent == Path("."):
                    break
                if parent not in detached_projects:
                    cmake = root / parent / "CMakeLists.txt"
                    detached_projects[parent] = False
                    if cmake.is_file():
                        try:
                            application = cmake.read_text(errors="replace")
                            if detached_application(project_text, application, parent.as_posix()):
                                # Intermediate CMake projects may attach the nested
                                # application even if the root does not name it.
                                independent = True
                                for ancestor in parent.parents:
                                    if ancestor == Path("."):
                                        break
                                    ancestor_cmake = root / ancestor / "CMakeLists.txt"
                                    if ancestor_cmake.is_file() and build_uses_directory(
                                            ancestor_cmake.read_text(errors="replace"),
                                            parent.relative_to(ancestor).as_posix()):
                                        independent = False
                                        break
                                detached_projects[parent] = independent
                        except OSError:
                            skipped.append(str(cmake.relative_to(sdk)))
                if detached_projects[parent]:
                    return True
            return False

        for path in candidate_paths(root, names, aliases):
            name = path.name
            relative = path.relative_to(root)
            if (not production_path(relative) or relative.parts[0] in detached
                    or path.is_symlink() or in_detached_project(relative)):
                continue
            is_cmake = name == "CMakeLists.txt" or name.endswith((".cmake", ".cmake.in"))
            is_source = path.suffix.lower() in SOURCE_EXTENSIONS
            is_python = path.suffix.lower() == ".py"
            if not (is_cmake or is_source or is_python or name == "install-dependencies.txt"):
                continue
            try:
                if path.stat().st_size > MAX_BYTES:
                    skipped.append(str(path.relative_to(sdk)))
                    continue
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                skipped.append(str(path.relative_to(sdk)))
                continue
            files += 1
            if is_cmake:
                found = cmake_references(text, root.name, names, aliases)
            elif is_source:
                found = cpp_references(text, root.name, names, aliases)
            elif is_python:
                found = python_references(text, root.name, names, aliases)
            else:
                found = []
                for line, entry in enumerate(text.splitlines(), 1):
                    ref = entry.split("#", 1)[0].strip()
                    module = owner_of(ref, names, aliases)
                    if module and module != root.name:
                        found.append({"module": module, "kind": "install-dependency", "reference": ref, "line": line})
            for row in found:
                row["path"] = str(path.relative_to(sdk))
            rows.extend(row for row in found if row["module"] not in owned)
        rows.sort(key=lambda row: (row["module"], row["path"], row["line"], row["kind"]))
        modules[root.name] = {"inspected_files": files,
                              "dependencies": sorted({row["module"] for row in rows}), "references": rows}
    manifest = sdk / "Windows/projects.json"
    windows = {}
    if manifest.is_file():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        for owner, record in data.items():
            if owner not in selected_names or not isinstance(record, dict):
                continue
            deps = sorted({mapped for dependency in record.get("dependencies", [])
                           if (mapped := owner_of(dependency, names, aliases)) and mapped != owner})
            if deps:
                windows[owner] = deps
    return {"kind": "lexical-sdk-independence-inventory", "modules": modules,
            "windows_manifest": windows, "skipped_files": skipped,
            "limitations": "Optional code is included; generated, vendor, test and detached application trees are excluded. "
                           "CMake variables, generated code, ABI/symbol dependencies and runtime composition require independent verification."}


def main():
    parser = argparse.ArgumentParser(description="Inventory direct SDK references without modifying the workspace")
    parser.add_argument("--sdk", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--json", action="store_true", help="print detailed JSON")
    parser.add_argument("--module", action="append", help="restrict inspection to this SDK module; repeat as needed")
    parser.add_argument("--strict", action="store_true", help="exit nonzero on declared cross-SDK references or incomplete scan")
    args = parser.parse_args()
    try:
        report = inventory(args.sdk.resolve(), args.module)
    except ValueError as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        for name, row in report["modules"].items():
            if row["dependencies"]:
                print(f"{name}: {', '.join(row['dependencies'])} ({len(row['references'])} references)")
        print(f"Modules: {len(report['modules'])}; modules with references: "
              f"{sum(bool(row['dependencies']) for row in report['modules'].values())}; "
              f"Windows manifest modules with dependencies: {len(report['windows_manifest'])}; "
              f"skipped files: {len(report['skipped_files'])}")
        print(report["limitations"])
    if args.strict and (report["skipped_files"] or report["windows_manifest"] or
                        any(row["dependencies"] for row in report["modules"].values())):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
