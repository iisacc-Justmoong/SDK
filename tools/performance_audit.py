#!/usr/bin/env python3
"""Build a source-located SDK declaration/work inventory; flags are review leads.

Tree-sitter is a development-only dependency. This tool neither rewrites sources
nor equates a syntactic pattern with a proven runtime performance defect.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import zipfile

EXCLUDED = {"build", ".git", "node_modules", "thirdparty", "third_party",
            "vendor", "_vendor", "external", "dependencies", ".venv", "venv", "_deps", "__pycache__",
            ".svelte-kit", ".build", "dist"}
NON_RUNTIME = {"test", "tests", "example", "examples", "benchmarks", "tools", "scripts"}
LANGUAGES = {
    ".h": "cpp", ".hpp": "cpp", ".hh": "cpp", ".hxx": "cpp", ".cpp": "cpp",
    ".cc": "cpp", ".cxx": "cpp", ".c": "c", ".mm": "cpp", ".m": "c",
    ".qml": "qmljs", ".js": "javascript", ".jsx": "javascript", ".ts": "typescript",
    ".tsx": "tsx", ".py": "python", ".rs": "rust", ".swift": "swift",
}
OBJECTS = {"class_specifier", "struct_specifier", "union_specifier", "enum_specifier",
           "class_declaration", "struct_item", "enum_item", "trait_item", "impl_item",
           "ui_object_definition", "interface_declaration", "protocol_declaration"}
FUNCTIONS = {"function_definition", "function_declaration", "function_item",
             "method_definition", "arrow_function", "function_expression",
             "constructor_declaration", "init_declaration", "deinit_declaration", "lambda_expression"}
LOOPS = {"for_statement", "for_range_loop", "while_statement", "do_statement",
         "for_in_statement", "for_expression", "while_expression"}
CALLS = {"call_expression", "call", "macro_invocation", "new_expression"}


def excluded_directory(name):
    return name.lower() in EXCLUDED or name.lower().startswith(("build-", "cmake-build", ".build."))


def text(node, source):
    return source[node.start_byte:node.end_byte].decode("utf-8", "replace")


def walk(node):
    stack = [node]
    while stack:
        item = stack.pop()
        yield item
        stack.extend(reversed(item.named_children))


def parser_for(language):
    from tree_sitter import Language, Parser
    module = importlib.import_module("tree_sitter_typescript" if language == "tsx" else "tree_sitter_" + language)
    factory = "language_tsx" if language == "tsx" else "language_typescript" if language == "typescript" else "language"
    return Parser(Language(getattr(module, factory)()))


def qt_macros(source, ignored=()):
    # These annotations contain balanced type/function parentheses in real headers.
    for match in re.finditer(rb"\b(Q_PROPERTY|Q_CLASSINFO|Q_ENUM|Q_FLAG|Q_DECLARE_FLAGS|QML_NAMED_ELEMENT|QML_ELEMENT)\s*\(", source):
        if any(start <= match.start() < end for start, end in ignored): continue
        depth, end = 1, match.end()
        while end < len(source) and depth:
            if source[end] == 40: depth += 1
            elif source[end] == 41: depth -= 1
            end += 1
        if not depth:
            yield match[1], match.start(), end, source[match.end():end-1]


def masked_cpp(source, ignored=()):
    # Export/decorator macros are not C++ grammar. Preserve every byte offset.
    pattern = rb"\b(?:[A-Z][A-Z0-9_]*(?:_EXPORT|_API)|Q_OBJECT|Q_GADGET|Q_INVOKABLE|Q_SLOTS|slots)\b"
    result = re.sub(pattern, lambda m: b" " * len(m[0]), source)
    result = bytearray(result)
    for _, start, end, _ in qt_macros(source, ignored):
        for i in range(start, end):
            if result[i] not in (10, 13): result[i] = 32
    result = bytes(result)
    return re.sub(rb"\bsignals\b(?=\s*:)", b"public ", result)


def name_of(node, source):
    if node.type == "ui_object_definition":
        name = node.child_by_field_name("type_name")
        body = node.child_by_field_name("initializer")
        if body:
            for child in body.named_children:
                key = child.child_by_field_name("name")
                if child.type == "ui_binding" and key and text(key, source) == "id":
                    value = child.child_by_field_name("value")
                    if value: return text(value, source).rstrip("; ")
        if name: return text(name, source)
    for field in ("name", "declarator"):
        value = node.child_by_field_name(field)
        if value is not None:
            while value.type in {"function_declarator", "pointer_declarator", "reference_declarator", "init_declarator"}:
                child = value.child_by_field_name("declarator")
                if child is None:
                    break
                value = child
            return text(value, source)
    parent = node.parent
    if node.type in {"arrow_function", "function_expression", "lambda_expression"} and parent is not None:
        value = parent.child_by_field_name("name") or parent.child_by_field_name("key") or parent.child_by_field_name("declarator")
        if value is not None:
            return text(value, source)
    return "<anonymous>"


def scope_of(node, source):
    result = []
    parent = node.parent
    while parent is not None:
        if parent.type in OBJECTS | {"namespace_definition", "class_definition"}:
            name = name_of(parent, source)
            if name != "<anonymous>":
                result.append(name)
        parent = parent.parent
    return "::".join(reversed(result))


def function_metrics(node, source):
    loops = 0
    max_depth = 0
    calls = collections.Counter()
    repeated_loop_calls = collections.Counter()
    stack = [(node, 0)]
    while stack:
        child, depth = stack.pop()
        if child != node and child.type in FUNCTIONS:
            continue
        if child.type in LOOPS:
            depth += 1
            loops += 1
            max_depth = max(max_depth, depth)
        if child.type in CALLS:
            target = child.child_by_field_name("function") or child.child_by_field_name("name")
            if target is not None:
                name = text(target, source)
                calls[name] += 1
                if depth:
                    repeated_loop_calls[name] += 1
        stack.extend((n, depth) for n in child.named_children)
    body = text(node, source)
    flags = []
    if max_depth > 1:
        flags.append("nested_iteration")
    if re.search(r"\b(?:removeFirst|pop_front)\s*\(|\.erase\s*\([^;\n]*\.begin\s*\(", body):
        flags.append("front_removal_review")
    if "beginResetModel(" in body or "beginResetModel (" in body:
        flags.append("full_model_reset")
    if re.search(r"\b(?:QRegularExpression|std::regex)\b", body) and not re.search(r"static\s+(?:const\s+)?(?:QRegularExpression|std::regex)", body):
        flags.append("regex_construction")
    if re.search(r"\b(?:std::async|QtConcurrent::run|std::thread|std::jthread)\s*[({<]", body):
        flags.append("asynchronous_admission_review")
    if loops and re.search(r"(?:\.contains\(|\.find\().*(?:\[|\.value\()", body, re.S):
        flags.append("repeated_lookup_review")
    expensive = [n for n in repeated_loop_calls if re.search(
        r"(?:sqlite3_prepare|Statement|\.prepare$|readAll$|toJson$|serialize|entryInfoList$|recursive_directory_iterator|QRegularExpression|sort$|stable_sort$|toStdString$|toUtf8$|pow$|sin$|cos$|find$|find_if$|indexOf$)", n)]
    if expensive:
        flags.append("loop_work_review")
    return {"loops": loops, "max_loop_depth": max_depth, "flags": flags,
            "calls": dict(calls), "loop_calls": dict(repeated_loop_calls),
            "loop_work_leads": expensive}


def extract(source, language, parser):
    ignored = []
    if language in {"cpp", "c"}:
        original = parser.parse(source)
        ignored = [(n.start_byte, n.end_byte) for n in walk(original.root_node)
                   if n.type in {"comment", "string_literal", "raw_string_literal", "char_literal"}]
    parse_source = masked_cpp(source, ignored) if language in {"cpp", "c"} else source
    tree = parser.parse(parse_source)
    records = []
    errors = []
    for node in walk(tree.root_node):
        if node.type == "ERROR":
            errors.append({"line": node.start_point.row + 1, "end_line": node.end_point.row + 1})
        kind = "object" if node.type in OBJECTS | {"class_definition"} else "function" if node.type in FUNCTIONS else None
        if node.type == "field_declaration":
            kind = "method_declaration" if any(n.type == "function_declarator" for n in walk(node)) else "member"
        elif node.type in {"ui_property", "ui_signal", "property_declaration", "field_definition", "public_field_definition"}:
            kind = "member"
        elif node.type == "enumerator":
            kind = "enum_value"
        if kind is None:
            continue
        record = {"kind": kind, "syntax": node.type, "name": name_of(node, source),
                  "scope": scope_of(node, source), "line": node.start_point.row + 1,
                  "end_line": node.end_point.row + 1, "bytes": node.end_byte - node.start_byte}
        if kind == "function":
            record.update(function_metrics(node, source))
        records.append(record)
        if node.type == "field_declaration":
            for declarator in node.children_by_field_name("declarator")[1:]:
                extra = dict(record)
                extra["name"] = name_of(declarator, source) if declarator.type != "field_identifier" else text(declarator, source)
                extra["bytes"] = declarator.end_byte - declarator.start_byte
                records.append(extra)
    # Objective-C selectors can appear in error-recovery nodes of C/C++ grammar.
    if language in {"c", "cpp"}:
        for macro, start, end, arguments in qt_macros(source, ignored):
            if macro != b"Q_PROPERTY": continue
            prefix = re.split(rb"\b(?:READ|WRITE|RESET|NOTIFY|MEMBER|BINDABLE|CONSTANT|FINAL|REQUIRED|DESIGNABLE|SCRIPTABLE|STORED|USER|REVISION)\b", arguments, maxsplit=1)[0]
            names = re.findall(rb"[A-Za-z_]\w*", prefix)
            owner = tree.root_node.descendant_for_byte_range(start, end)
            records.append({"kind": "member", "syntax": "qt_property", "name": names[-1].decode() if names else "<anonymous>",
                            "scope": scope_of(owner, source), "line": source[:start].count(b"\n")+1,
                            "end_line": source[:end].count(b"\n")+1, "bytes": end-start})
        for match in re.finditer(rb"(?m)^\s*[-+]\s*\([^\n)]+\)\s*([^;{]+)[;{]", source):
            records.append({"kind": "objc_method", "name": match[1].decode("utf-8", "replace").strip(),
                            "scope": "", "line": source[:match.start()].count(b"\n") + 1,
                            "end_line": source[:match.end()].count(b"\n") + 1,
                            "bytes": match.end() - match.start()})
    return records, errors


def discover_inventory(sdk):
    modules = []
    for module in sorted(sdk.iterdir()):
        if not module.is_dir() or module.is_symlink() or module.name.startswith(".") or module.name in EXCLUDED | {"tools", "benchmarks", "docs", "tests", "cmake"}:
            continue
        sources = []
        for directory, children, files in os.walk(module, followlinks=False):
            children[:] = sorted(n for n in children if not excluded_directory(n))
            for name in sorted(files):
                path = Path(directory) / name
                if path.suffix in LANGUAGES or path.suffix == ".svelte":
                    if path.is_file() and not path.is_symlink(): sources.append(path.relative_to(module).as_posix())
        kind = "platform_tooling" if module.name == "Windows" and not (module/"CMakeLists.txt").exists() else "sdk_module"
        modules.append({"module": module.name, "kind": kind, "source_files": sources})
    return modules


def audit_inventory(sdk, inventory, output, archive=None):
    output.mkdir(parents=True, exist_ok=True)
    parsers = {}
    summaries = []
    leads = []
    with (output / "members.jsonl").open("w") as members:
        for module in inventory:
            summary = {"module": module["module"], "kind": module.get("kind", "sdk_module"), "files": [], "runtime_files": 0,
                       "objects": 0, "functions": 0, "members": 0, "syntax_recovery_files": 0}
            for name in module["source_files"]:
                path = Path(name)
                if any(excluded_directory(p) for p in path.parts[:-1]):
                    continue
                role = "test_or_tool" if module.get("kind") == "platform_tooling" or any(p.lower() in NON_RUNTIME for p in path.parts) else "runtime"
                full = sdk / module["module"] / path
                if archive is None and not full.is_file():
                    continue
                source = archive.read(module["module"] + "/" + name) if archive else full.read_bytes()
                language = LANGUAGES.get(path.suffix)
                segments = [(source, language, 0)]
                if path.suffix == ".svelte":
                    segments = [(m[2], "typescript" if b"ts" in m[1] else "javascript", source[:m.start(2)].count(b"\n"))
                                for m in re.finditer(rb"<script\b([^>]*)>(.*?)</script>", source, re.S)]
                file_records = []
                file_errors = []
                for content, grammar, line_offset in segments:
                    if grammar is None:
                        continue
                    if grammar not in parsers:
                        parsers[grammar] = parser_for(grammar)
                    records, errors = extract(content, grammar, parsers[grammar])
                    for record in records:
                        record["line"] += line_offset
                        record["end_line"] += line_offset
                        record.update(module=module["module"], file=name, role=role)
                        members.write(json.dumps(record, ensure_ascii=False) + "\n")
                        file_records.append(record)
                        if role == "runtime" and record.get("flags"):
                            leads.append(record)
                    file_errors.extend({k: v + line_offset for k, v in error.items()} for error in errors)
                summary["files"].append({"path": name, "role": role, "sha256": hashlib.sha256(source).hexdigest(),
                                         "declarations": len(file_records), "syntax_recovery": file_errors})
                if role == "runtime":
                    summary["runtime_files"] += 1
                    summary["objects"] += sum(r["kind"] == "object" for r in file_records)
                    summary["functions"] += sum(r["kind"] == "function" for r in file_records)
                    summary["members"] += sum(r["kind"] in {"member", "method_declaration", "objc_method", "enum_value"} for r in file_records)
                    summary["syntax_recovery_files"] += bool(file_errors)
            summaries.append(summary)
    leads.sort(key=lambda r: (len(r["flags"]), r.get("max_loop_depth", 0), r["bytes"]), reverse=True)
    report = {"schema_version": 2, "modules": summaries, "totals": {k: sum(m[k] for m in summaries) for k in
              ("runtime_files", "objects", "functions", "members", "syntax_recovery_files")},
              "review_leads": len(leads), "note": "Syntactic review leads require contract inspection and measurement; syntax recovery is explicitly reported."}
    (output / "coverage.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    (output / "review-leads.json").write_text(json.dumps(leads, indent=2, ensure_ascii=False) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--inventory", type=Path, help="Pinned file inventory; discovers module sources when omitted")
    parser.add_argument("--source-archive", type=Path, help="Read module/path sources from a pinned ZIP instead of the live checkout")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text()) if args.inventory else discover_inventory(args.sdk)
    (args.output).mkdir(parents=True, exist_ok=True)
    (args.output / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    if args.source_archive:
        with zipfile.ZipFile(args.source_archive) as archive:
            report = audit_inventory(args.sdk, inventory, args.output, archive)
    else:
        report = audit_inventory(args.sdk, inventory, args.output)
    print(json.dumps({"modules": len(report["modules"]), **report["totals"], "review_leads": report["review_leads"]}))


if __name__ == "__main__":
    main()
