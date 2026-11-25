"""Exercise build discovery against real source and the native runtime."""
import json
import os
from pathlib import Path
import shutil
import tempfile

from iiagent_application_interface import Runtime
from iiagent_application_interface_discovery import discover_compilation_database, publish_catalog


root = Path(__file__).resolve().parents[1]
fixture = root / "resources" / "build_discovery"
compiler = shutil.which("clang++")
assert compiler, "clang++ is required for the build-discovery test"
with tempfile.TemporaryDirectory(dir=os.environ["IIAGENTAPPLICATIONINTERFACE_TEST_BUILD"]) as temporary:
    db = Path(temporary) / "compile_commands.json"
    source = fixture / "model.cpp"
    db.write_text(json.dumps([{
        "directory": str(fixture), "file": str(source),
        "arguments": [compiler, "-std=c++23", "-I", str(fixture),
                      "-I", str(fixture / "priority_user"), "-isystem", str(fixture / "priority_system"),
                      "-c", str(source), "-o", "CMakeFiles/fixture.dir/model.cpp.o"]
    }]), encoding="utf-8")
    catalog = discover_compilation_database(db, ["fixture"], [fixture])
    declarations = catalog["declarations"]
    assert catalog["coverage"]["runtime_values"] is False
    assert catalog["coverage"]["member_invocation"] is False
    assert catalog["coverage"]["prebuilt_binaries"] is False
    assert all(item["translation_units"] for item in declarations)
    def find(name, kind):
        """Return declarations by qualified name and semantic kind."""
        return [item for item in declarations if item["qualified_name"] == name and item["kind"] == kind]
    assert find("demo::Note", "type")
    assert find("demo::Project", "type")
    assert any(item["kind"] == "base" and item["type"].endswith("Base") for item in declarations)
    assert find("demo::Preferred", "type")
    assert not find("demo::Shadowed", "type")
    assert find("demo::Note::id", "field")[0]["access"] == "public"
    assert "Stable note identifier" in find("demo::Note::id", "field")[0]["documentation"]
    assert "Archive the note" in find("demo::Note::archive", "method")[0]["brief"]
    assert find("demo::Note::macro_member", "field")
    assert find("demo::Box", "type") and find("demo::Box::value", "field")
    assert find("demo::Color", "type") and find("demo::Color::red", "enum_value")
    assert find("demo::Note::flags", "field")[0]["access"] == "private"
    assert find("demo::Note::invalidate", "method")[0]["access"] == "protected"
    assert len(find("demo::Project::add", "method")) == 2
    assert find("demo::active", "variable")
    assert find("demo::Note::opened", "variable")
    # Explicit application roots must remain visible even when compiled with -isystem.
    command = json.loads(db.read_text(encoding="utf-8"))
    command[0]["arguments"][2] = "-isystem"
    db.write_text(json.dumps(command), encoding="utf-8")
    system_catalog = discover_compilation_database(db, ["fixture"], [fixture])
    assert any(item["qualified_name"] == "demo::Project::secret" for item in system_catalog["declarations"])
    # Invalid translation units must fail rather than publishing a partial catalog.
    broken = fixture / "broken.cpp"
    command[0]["file"] = str(broken)
    command[0]["arguments"] = [str(broken) if item == str(source) else item
                               for item in command[0]["arguments"]]
    db.write_text(json.dumps(command), encoding="utf-8")
    try:
        discover_compilation_database(db, ["fixture"], [fixture])
    except RuntimeError:
        pass
    else:
        raise AssertionError("invalid C++ source must fail discovery")
    try:
        discover_compilation_database(db, ["missing-target"], [fixture])
    except ValueError:
        pass
    else:
        raise AssertionError("a missing target must fail instead of reporting an empty catalog")
    with Runtime(lambda principal, action, obj, cap, permissions: principal == "reader") as runtime:
        publish_catalog(runtime, catalog)
        assert runtime.object("denied", "application.build_catalog") is None
        found = runtime.object("reader", "application.build_catalog")
        assert found and json.loads(found["state"])["declarations"] == declarations
        authoring = runtime.procedure_authoring_context("reader")
        build_info = next(item["object"] for item in authoring["objects"]
                          if item["object"]["id"] == "application.build_catalog")
        assert any(item["qualified_name"] == "demo::Note::archive"
                   for item in json.loads(build_info["state"])["declarations"])
        assert runtime.procedure_authoring_context("denied")["objects"] == []
