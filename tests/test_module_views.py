"""Check module sources, examples and tests without visiting user data or builds."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SDK = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("audit_module_views", SDK / "tools" / "audit_module_views.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class ModuleViewAuditTests(unittest.TestCase):
    def test_reports_sources_examples_and_tests_but_not_isolated_legacy(self):
        (SDK / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="module-view-audit-", dir=SDK / "build") as tmp:
            root = Path(tmp)
            files = {
                "iiOne/src/Views/Live.qml": "",
                "iiOne/Native/src/Live.QML": "",
                "iiOne/Native/qml/Embedded.qml": "",
                "iiOne/qml/Root.qml": "",
                "iiOne/resources/views/Embedded.qml": "",
                "iiOne/resources/dependencies/sources/External.qml": "",
                "iiOne/src/Legacy/qml/Old.qml": "",
                "iiOne/src/third_party/External.qml": "",
                "iiOne/src/tests/Fixture.qml": "",
                "iiOne/tests/Legacy/OldHarness.qml": "",
                "iiOne/tests/UnisolatedHarness.qml": "",
                "iiOne/examples/Demo.qml": "",
                "iiOne/Native/examples/NativeDemo.qml": "",
                "iiOne/build/Generated.qml": "",
                "LVRS/src/qml/LegacySystem.qml": "",
            }
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            external = root / "external"
            external.mkdir()
            (external / "External.qml").write_text("", encoding="utf-8")
            try:  # Windows hosts may not grant symbolic-link creation.
                (root / "iiOne/src/linked").symlink_to(external, target_is_directory=True)
                (root / "iiLinked").symlink_to(root / "iiOne", target_is_directory=True)
                (root / "iiOne/src/linked.qml").symlink_to(external / "External.qml")
            except (OSError, NotImplementedError):
                pass
            self.assertEqual(audit.scan_sdk(root), [
                "iiOne/Native/examples/NativeDemo.qml", "iiOne/Native/qml/Embedded.qml", "iiOne/Native/src/Live.QML", "iiOne/examples/Demo.qml", "iiOne/qml/Root.qml", "iiOne/resources/views/Embedded.qml",
                "iiOne/src/Views/Live.qml", "iiOne/src/tests/Fixture.qml", "iiOne/tests/UnisolatedHarness.qml"])
            cli = subprocess.run([sys.executable, "-B", str(SDK / "tools/audit_module_views.py"), "--sdk", str(root)],
                                 text=True, capture_output=True, check=False)
            self.assertEqual(cli.returncode, 1)
            self.assertIn("Unisolated module QML files: 9", cli.stdout)

    def test_workspace_has_no_unisolated_module_qml(self):
        self.assertEqual(audit.scan_sdk(SDK), [])


if __name__ == "__main__":
    unittest.main()
