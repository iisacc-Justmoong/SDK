"""Build-time C/C++ declaration discovery using libclang's stable C interface."""

import argparse
import ctypes as ct
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


class _String(ct.Structure):
    """The libclang-owned string handle; release after conversion."""
    _fields_ = [("data", ct.c_void_p), ("private_flags", ct.c_uint)]


class _Cursor(ct.Structure):
    """Opaque AST cursor value as defined by clang-c/Index.h."""
    _fields_ = [("kind", ct.c_uint), ("xdata", ct.c_int), ("data", ct.c_void_p * 3)]


class _Type(ct.Structure):
    """Opaque AST type value as defined by clang-c/Index.h."""
    _fields_ = [("kind", ct.c_uint), ("data", ct.c_void_p * 2)]


class _Location(ct.Structure):
    """Opaque source location value as defined by clang-c/CXSourceLocation.h."""
    _fields_ = [("ptr_data", ct.c_void_p * 2), ("int_data", ct.c_uint)]


_Visitor = ct.CFUNCTYPE(ct.c_uint, _Cursor, _Cursor, ct.c_void_p)
_KINDS = {
    "StructDecl": "type", "UnionDecl": "type", "ClassDecl": "type", "EnumDecl": "type",
    "ClassTemplate": "type", "ClassTemplatePartialSpecialization": "type",
    "TypeAliasDecl": "type_alias", "TypedefDecl": "type_alias", "TypeAliasTemplateDecl": "type_alias",
    "FieldDecl": "field", "CXXMethod": "method", "Constructor": "constructor",
    "Destructor": "destructor", "ConversionFunction": "method", "FunctionDecl": "function",
    "FunctionTemplate": "function_template", "VarDecl": "variable", "ParmDecl": "parameter",
    "EnumConstantDecl": "enum_value", "C++ base class specifier": "base",
}
_ACCESS = {0: "unspecified", 1: "public", 2: "protected", 3: "private"}


def _library_path():
    """Locate libclang explicitly, on the platform search path, or in the active Xcode toolchain."""
    explicit = os.environ.get("IIAGENTAPPLICATIONINTERFACE_LIBCLANG")
    if explicit:
        return explicit
    for name in ("clang", "libclang", "clang-21", "clang-20", "clang-19", "clang-18", "clang-17"):
        value = ctypes.util.find_library(name)
        if value:
            return value
    if sys.platform == "darwin":
        try:
            clang = subprocess.check_output(["xcrun", "--find", "clang"], text=True).strip()
            candidate = Path(clang).resolve().parents[1] / "lib" / "libclang.dylib"
            if candidate.is_file():
                return str(candidate)
        except (OSError, subprocess.CalledProcessError):
            pass
    raise RuntimeError("libclang not found; set IIAGENTAPPLICATIONINTERFACE_LIBCLANG to its library path")


class _Clang:
    """Typed bindings for the libclang calls needed by the build catalog."""
    def __init__(self, path=None):
        """Load a selected libclang shared library and configure its C function signatures."""
        self.lib = ct.CDLL(path or _library_path())
        signatures = {
            "clang_createIndex": (ct.c_void_p, [ct.c_int, ct.c_int]),
            "clang_disposeIndex": (None, [ct.c_void_p]),
            "clang_parseTranslationUnit2FullArgv": (ct.c_int, [ct.c_void_p, ct.c_char_p, ct.POINTER(ct.c_char_p),
                ct.c_int, ct.c_void_p, ct.c_uint, ct.c_uint, ct.POINTER(ct.c_void_p)]),
            "clang_disposeTranslationUnit": (None, [ct.c_void_p]),
            "clang_getTranslationUnitCursor": (_Cursor, [ct.c_void_p]),
            "clang_visitChildren": (ct.c_uint, [_Cursor, _Visitor, ct.c_void_p]),
            "clang_getCursorKindSpelling": (_String, [ct.c_uint]),
            "clang_getCursorSpelling": (_String, [_Cursor]),
            "clang_getCursorUSR": (_String, [_Cursor]),
            "clang_Cursor_getRawCommentText": (_String, [_Cursor]),
            "clang_Cursor_getBriefCommentText": (_String, [_Cursor]),
            "clang_getCursorType": (_Type, [_Cursor]),
            "clang_getTypeSpelling": (_String, [_Type]),
            "clang_getCursorSemanticParent": (_Cursor, [_Cursor]),
            "clang_Cursor_isNull": (ct.c_int, [_Cursor]),
            "clang_getCursorLocation": (_Location, [_Cursor]),
            "clang_getExpansionLocation": (None, [_Location, ct.POINTER(ct.c_void_p), ct.POINTER(ct.c_uint),
                                                   ct.POINTER(ct.c_uint), ct.POINTER(ct.c_uint)]),
            "clang_getFileName": (_String, [ct.c_void_p]),
            "clang_getCXXAccessSpecifier": (ct.c_uint, [_Cursor]),
            "clang_Cursor_getStorageClass": (ct.c_uint, [_Cursor]),
            "clang_CXXMethod_isStatic": (ct.c_uint, [_Cursor]),
            "clang_isCursorDefinition": (ct.c_uint, [_Cursor]),
            "clang_getNumDiagnostics": (ct.c_uint, [ct.c_void_p]),
            "clang_getDiagnostic": (ct.c_void_p, [ct.c_void_p, ct.c_uint]),
            "clang_getDiagnosticSeverity": (ct.c_uint, [ct.c_void_p]),
            "clang_getDiagnosticSpelling": (_String, [ct.c_void_p]),
            "clang_disposeDiagnostic": (None, [ct.c_void_p]),
            "clang_getCString": (ct.c_char_p, [_String]),
            "clang_disposeString": (None, [_String]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.lib, name)
            function.restype, function.argtypes = result, arguments

    def text(self, value):
        """Copy and release one libclang string value."""
        try:
            raw = self.lib.clang_getCString(value)
            return raw.decode("utf-8", "replace") if raw else ""
        finally:
            self.lib.clang_disposeString(value)

    def location(self, cursor):
        """Resolve the expansion site of a cursor, including declarations emitted by macros."""
        location = self.lib.clang_getCursorLocation(cursor)
        file_handle, line, column, offset = ct.c_void_p(), ct.c_uint(), ct.c_uint(), ct.c_uint()
        self.lib.clang_getExpansionLocation(location, ct.byref(file_handle), ct.byref(line),
                                            ct.byref(column), ct.byref(offset))
        if not file_handle:
            return None
        return Path(self.text(self.lib.clang_getFileName(file_handle))).resolve(), line.value, column.value

    def qualified_name(self, cursor):
        """Build a qualified name using semantic rather than merely lexical parents."""
        names = []
        current = cursor
        for _ in range(128):
            if self.lib.clang_Cursor_isNull(current):
                break
            kind = self.text(self.lib.clang_getCursorKindSpelling(current.kind))
            if kind == "TranslationUnit":
                break
            name = self.text(self.lib.clang_getCursorSpelling(current))
            if name:
                names.append(name)
            current = self.lib.clang_getCursorSemanticParent(current)
        return "::".join(reversed(names))

    def parse(self, source, arguments, roots):
        """Parse one translation unit and return its scoped, source-backed declarations."""
        index = self.lib.clang_createIndex(0, 0)
        if not index:
            raise RuntimeError("libclang could not create an index")
        unit = ct.c_void_p()
        encoded = [str(argument).encode("utf-8") for argument in arguments]
        array = (ct.c_char_p * len(encoded))(*encoded)
        try:
            error = self.lib.clang_parseTranslationUnit2FullArgv(index, os.fsencode(source), array, len(encoded),
                                                          None, 0, 0, ct.byref(unit))
            if error or not unit:
                raise RuntimeError(f"libclang could not parse {source} (error {error})")
            diagnostics = []
            for number in range(self.lib.clang_getNumDiagnostics(unit)):
                diagnostic = self.lib.clang_getDiagnostic(unit, number)
                try:
                    if self.lib.clang_getDiagnosticSeverity(diagnostic) >= 3:
                        diagnostics.append(self.text(self.lib.clang_getDiagnosticSpelling(diagnostic)))
                finally:
                    self.lib.clang_disposeDiagnostic(diagnostic)
            if diagnostics:
                raise RuntimeError(f"analysis failed for {source}: " + "; ".join(diagnostics))
            declarations, callback_errors = [], []

            @_Visitor
            def visit(cursor, _parent, _data):
                try:
                    location = self.location(cursor)
                    if location is None:
                        return 2
                    path, line, column = location
                    if not any(path == root or root in path.parents for root in roots):
                        return 1
                    spelling = self.text(self.lib.clang_getCursorKindSpelling(cursor.kind))
                    kind = _KINDS.get(spelling)
                    if kind:
                        name = self.text(self.lib.clang_getCursorSpelling(cursor))
                        qualified = self.qualified_name(cursor)
                        if not name:
                            name = f"(anonymous@{line}:{column})"
                            qualified = qualified + ("::" if qualified else "") + name
                        usr = self.text(self.lib.clang_getCursorUSR(cursor))
                        identity = usr or f"{path}:{line}:{column}:{spelling}:{qualified}"
                        static = self.lib.clang_Cursor_getStorageClass(cursor) == 3
                        if spelling == "CXXMethod":
                            static = bool(self.lib.clang_CXXMethod_isStatic(cursor))
                        declarations.append({
                            "id": identity, "kind": kind, "name": name, "qualified_name": qualified,
                            "type": self.text(self.lib.clang_getTypeSpelling(self.lib.clang_getCursorType(cursor))),
                            "documentation": self.text(self.lib.clang_Cursor_getRawCommentText(cursor)),
                            "brief": self.text(self.lib.clang_Cursor_getBriefCommentText(cursor)),
                            "access": _ACCESS.get(self.lib.clang_getCXXAccessSpecifier(cursor), "unspecified"),
                            "static": bool(static), "definition": bool(self.lib.clang_isCursorDefinition(cursor)),
                            "file": str(path), "line": line, "column": column,
                        })
                    return 2
                except BaseException as exc:
                    callback_errors.append(exc)
                    return 0

            self.lib.clang_visitChildren(self.lib.clang_getTranslationUnitCursor(unit), visit, None)
            if callback_errors:
                raise RuntimeError(f"libclang traversal failed: {callback_errors[0]}") from callback_errors[0]
            return declarations
        finally:
            if unit:
                self.lib.clang_disposeTranslationUnit(unit)
            self.lib.clang_disposeIndex(index)


def _compile_arguments(entry):
    """Remove output/dependency generation while retaining each translation unit's compiler options."""
    directory = Path(entry["directory"]).resolve()
    source = Path(entry["file"])
    source = (directory / source).resolve() if not source.is_absolute() else source.resolve()
    arguments = entry.get("arguments")
    if arguments is None:
        if os.name == "nt":
            raise ValueError("Windows compilation databases must supply an arguments array")
        arguments = shlex.split(entry["command"])
    if not arguments:
        raise ValueError(f"empty compile command for {source}")
    result, output, i = [], entry.get("output", ""), 1
    while i < len(arguments):
        value = arguments[i]
        if value in ("-o", "-MF", "-MT", "-MQ", "-MJ"):
            if i + 1 >= len(arguments):
                raise ValueError(f"missing argument to {value} in {source}")
            if value == "-o":
                output = arguments[i + 1]
            i += 2
            continue
        if value in ("-c", "-MMD", "-MD", "-MP", "-MG"):
            i += 1
            continue
        candidate = Path(value)
        if not value.startswith("-") and (directory / candidate).resolve() == source:
            i += 1
            continue
        result.append(value)
        i += 1
    result.insert(0, "-working-directory=" + str(directory))
    result.insert(0, arguments[0])
    return source, output.replace("\\", "/"), result


def _explicit_include_paths(arguments):
    """Identify caller-provided search paths so probing cannot reorder their priority."""
    working = next((Path(value.split("=", 1)[1]) for value in arguments
                    if value.startswith("-working-directory=")), Path.cwd())
    paths, i = set(), 1
    flags = ("-isystem", "-iquote", "-idirafter", "-iframework", "-I", "-F")
    while i < len(arguments):
        value, found = arguments[i], None
        for flag in flags:
            if value == flag:
                if i + 1 < len(arguments):
                    found = arguments[i + 1]
                    i += 1
                break
            if value.startswith(flag) and len(value) > len(flag):
                found = value[len(flag):]
                break
        if found:
            path = Path(found)
            paths.add((working / path).resolve() if not path.is_absolute() else path.resolve())
        i += 1
    for key in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "OBJC_INCLUDE_PATH"):
        for value in os.environ.get(key, "").split(os.pathsep):
            if value:
                path = Path(value)
                paths.add((working / path).resolve() if not path.is_absolute() else path.resolve())
    return paths


def _driver_include_paths(source, arguments):
    """Ask the configured compiler for its real standard and framework include search paths."""
    language = "c" if source.suffix.lower() == ".c" else "c++"
    command = [arguments[0], *arguments[1:], "-E", "-x", language, "-", "-v"]
    try:
        result = subprocess.run(command, input="", text=True, capture_output=True, timeout=60, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError(f"cannot query compiler include paths for {source}: {error}") from error
    output = result.stderr + "\n" + result.stdout
    start = output.find("#include <...> search starts here:")
    end = output.find("End of search list.", start)
    if start < 0 or end < 0:
        raise RuntimeError(f"compiler did not report include paths for {source}: {result.stderr.strip()}")
    paths = []
    explicit = _explicit_include_paths(arguments)
    for line in output[start:end].splitlines()[1:]:
        name = line.strip()
        if not name:
            continue
        framework = name.endswith(" (framework directory)")
        if framework:
            name = name[:-len(" (framework directory)")]
        if Path(name).resolve() not in explicit:
            paths.extend(("-iframework" if framework else "-isystem", name))
    return paths


def discover_compilation_database(database, targets, roots, *, exclude=(), libclang=None):
    """Discover declarations from the named CMake targets and explicit application source roots.

    Raises on missing targets, absent compile databases or parse errors. Declarations are
    source metadata, not proof of live instances or permission to read or invoke them.
    """
    roots = tuple(Path(root).resolve() for root in roots)
    if not targets or not roots or any(not root.is_dir() for root in roots):
        raise ValueError("at least one target and existing source root are required")
    entries = json.loads(Path(database).read_text(encoding="utf-8"))
    if not isinstance(entries, list):
        raise ValueError("compilation database must contain a JSON array")
    excluded = {Path(path).resolve() for path in exclude}
    matches, seen_targets = [], set()
    for entry in entries:
        source, output, arguments = _compile_arguments(entry)
        matched = next((target for target in targets if f"CMakeFiles/{target}.dir/" in output), None)
        if matched and source not in excluded:
            matches.append((source, arguments))
            seen_targets.add(matched)
    missing = set(targets) - seen_targets
    if missing:
        raise ValueError("no C/C++ compile commands for target(s): " + ", ".join(sorted(missing)))
    clang = _Clang(libclang)
    merged = {}
    include_cache = {}
    for source, arguments in matches:
        cache_key = (source.suffix.lower() == ".c", tuple(arguments))
        if cache_key not in include_cache:
            include_cache[cache_key] = _driver_include_paths(source, arguments)
        for declaration in clang.parse(source, [*arguments, *include_cache[cache_key]], roots):
            key = (declaration["id"], declaration["kind"])
            declaration["translation_units"] = [str(source)]
            if key in merged:
                previous = merged[key]
                if str(source) not in previous["translation_units"]:
                    previous["translation_units"].append(str(source))
                previous["definition"] = previous["definition"] or declaration["definition"]
                if previous["access"] == "unspecified":
                    previous["access"] = declaration["access"]
                previous["static"] = previous["static"] or declaration["static"]
                for field in ("documentation", "brief"):
                    if not previous[field]:
                        previous[field] = declaration[field]
            else:
                merged[key] = declaration
    declarations = sorted(merged.values(), key=lambda item: (item["qualified_name"], item["kind"], item["id"]))
    for declaration in declarations:
        declaration["translation_units"].sort()
    return {"format": "iiAgentApplicationInterface.build-catalog.v1", "targets": list(targets),
            "source_roots": [str(root) for root in roots],
            "translation_units": sorted({str(source) for source, _ in matches}),
            "coverage": {"source_roots_only": True, "libclang_declaration_subset": True,
                         "runtime_values": False, "member_invocation": False,
                         "prebuilt_binaries": False, "complete_application": False},
            "runtime_instances": False, "declarations": declarations}


def publish_catalog(runtime, catalog):
    """Publish source metadata as one host-owned object under normal runtime authorization."""
    runtime.add_object("application.build_catalog", "application.build_catalog",
                       "Build-time C/C++ source declarations",
                       json.dumps(catalog, ensure_ascii=False, sort_keys=True, separators=(",", ":")))


def _write_if_changed(path, content):
    """Update generated build output only when its bytes actually differ."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8")
    if not path.exists() or path.read_bytes() != encoded:
        path.write_bytes(encoded)


def main(argv=None):
    """Generate the application-owned C++ publication function from a compile database."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", required=True)
    parser.add_argument("--target", action="append", required=True)
    parser.add_argument("--root", action="append", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    try:
        catalog = discover_compilation_database(args.database, args.target, args.root, exclude=[args.output])
        payload = json.dumps(catalog, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        delimiter = "ia" + hashlib.sha256(payload.encode("utf-8")).hexdigest()[:12]
        if ")" + delimiter + '"' in payload:
            raise ValueError("generated C++ raw-string delimiter collides with catalog text")
        source = ('#include <iiAgentApplicationInterface>\n#include <utility>\n\n'
                  'namespace iiAgentApplicationInterface::generated {\n'
                  'Status publish_build_catalog(Runtime& runtime) {\n'
                  '    return runtime.add_object({"application.build_catalog", "application.build_catalog",\n'
                  '        "Build-time C/C++ source declarations", R"' + delimiter + '(' + payload + ')' + delimiter + '"});\n'
                  '}\n'
                  'std::unique_ptr<Runtime> create_application_runtime(Authorizer authorizer) {\n'
                  '    auto runtime = std::make_unique<Runtime>(std::move(authorizer));\n'
                  '    if (publish_build_catalog(*runtime) != Status::ok) return {};\n'
                  '    return runtime;\n'
                  '}\n}\n')
        _write_if_changed(args.output, source)
        return 0
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"build discovery failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
