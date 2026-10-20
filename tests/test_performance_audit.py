import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

SDK = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("performance_audit", SDK / "tools/performance_audit.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class PerformanceAuditTests(unittest.TestCase):
    def test_cpp_properties_fields_and_callback_work_have_source_locations(self):
        source = b'''class Demo { Q_OBJECT
Q_PROPERTY(int count\n READ count NOTIFY changed)
int width,height;
void run() { auto callback=[] { for(int x=0;x<3;++x) for(int y=0;y<2;++y) emitEvent(); }; }
};'''
        records, errors = audit.extract(source, "cpp", audit.parser_for("cpp"))
        self.assertFalse(errors)
        self.assertTrue(any(r["syntax"] == "qt_property" and r["name"] == "count" and r["line"] == 2 for r in records))
        self.assertTrue({"width", "height"}.issubset({r["name"] for r in records}))
        callback = next(r for r in records if r["name"] == "callback")
        self.assertEqual(callback["max_loop_depth"], 2)
        self.assertIn("nested_iteration", callback["flags"])
        self.assertEqual(next(r for r in records if r["name"] == "run")["loops"], 0)

    def test_qml_members_are_scoped_to_object_id(self):
        source = b'Item { id: root; property int value: 1; signal changed(int x); function work() { for(let n=0;n<3;++n) ping() } }'
        records, errors = audit.extract(source, "qmljs", audit.parser_for("qmljs"))
        self.assertFalse(errors)
        self.assertTrue(any(r["kind"] == "object" and r["name"] == "root" for r in records))
        self.assertTrue(all(r["scope"] == "root" for r in records if r["name"] in {"value", "changed", "work"}))

    def test_discovery_prunes_builds_dependencies_and_keeps_role_boundaries(self):
        parent = SDK / "build/performance-audit-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temporary:
            root = Path(temporary)
            fixtures = {"iiOne/src/A.cpp": "struct A { int x; };", "iiOne/Test/a.cpp": "void test() {}",
                        "iiOne/build/ignored.cpp": "void generated() {}", "iiOne/ThirdParty/vendor.cpp": "void vendor() {}",
                        "iiOne/.svelte-kit/generated.js": "function generated() {}",
                        "iiOne/dist/generated.js": "function packaged() {}",
                        "iiOne/.build.stale/generated.cpp": "void stale() {}",
                        "Windows/Verify.py": "def verify(): pass\n"}
            for name, value in fixtures.items():
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(value)
            (root / "iiEmpty").mkdir()
            inventory = audit.discover_inventory(root)
            before = {name: (root/name).read_bytes() for name in fixtures}
            report = audit.audit_inventory(root, inventory, root/"build/report")
            self.assertEqual(report["totals"]["runtime_files"], 1)
            self.assertEqual(report["totals"]["objects"], 1)
            self.assertEqual(len(report["modules"]), 3)
            windows = next(m for m in report["modules"] if m["module"] == "Windows")
            self.assertEqual(windows["kind"], "platform_tooling")
            self.assertEqual(windows["runtime_files"], 0)
            rows = [json.loads(line) for line in (root/"build/report/members.jsonl").read_text().splitlines()]
            self.assertTrue(any(r["role"] == "test_or_tool" for r in rows))
            self.assertEqual(before, {name: (root/name).read_bytes() for name in fixtures})
            with zipfile.ZipFile(root/"build/source.zip", "w") as archive:
                for module in inventory:
                    for name in module["source_files"]:
                        archive.write(root/module["module"]/name,module["module"]+"/"+name)
            (root/"iiOne/src/A.cpp").write_text("struct Changed {};")
            with zipfile.ZipFile(root/"build/source.zip") as archive:
                pinned = audit.audit_inventory(root, inventory, root/"build/pinned", archive)
            self.assertEqual(pinned["totals"]["objects"], 1)
            self.assertIn('"name": "A"', (root/"build/pinned/members.jsonl").read_text())

    def test_syntax_recovery_is_visible(self):
        _, errors = audit.extract(b"void broken( {", "cpp", audit.parser_for("cpp"))
        self.assertTrue(errors)

    def test_annotations_in_comments_and_strings_are_not_members(self):
        source=b'class S { /* Q_PROPERTY(int fake READ fake) */ const char* label="Q_PROPERTY(int ghost READ ghost)"; int real; };'
        records, _=audit.extract(source,"cpp",audit.parser_for("cpp"))
        self.assertFalse(any(r["syntax"]=="qt_property" for r in records))


if __name__ == "__main__": unittest.main()
