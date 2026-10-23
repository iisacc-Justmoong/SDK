import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("qt_inventory", Path(__file__).resolve().parents[1] / "tools/audit_qt_dependencies.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class InventoryTest(unittest.TestCase):
    def test_includes_untracked_source_without_scanning_build_or_local_query_headers(self):
        # TemporaryDirectory lives in the caller's Workspace build directory.
        parent = Path(__file__).resolve().parents[1] / "build"
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as folder:
            root = Path(folder)
            module = root / "SDK/example"
            (module / "src").mkdir(parents=True)
            (module / "tests").mkdir()
            (module / "build").mkdir()
            (module / "CMakeLists.txt").write_text("target_link_libraries(example Qt6::Core)")
            (module / "src/Query.h").write_text("#pragma once")
            (module / "src/a.cpp").write_text('#include "Query.h"\n#include <QImage>\n#include <QtQml/qqml.h>')
            (module / "tests/t.cpp").write_text("#include <QtTest>")
            (module / "build/generated.cpp").write_text("#include <QObject>")
            rows = audit.inventory(root)["modules"]["SDK/example"]
            self.assertEqual(len(rows), 3)
            source = next(r for r in rows if r["path"].endswith("a.cpp"))
            self.assertEqual(source["qtHeaders"], ["QImage", "QtQml/qqml.h"])
            self.assertFalse(source["test"])
            self.assertTrue(next(r for r in rows if r["path"].endswith("t.cpp"))["test"])


if __name__ == "__main__":
    unittest.main()
