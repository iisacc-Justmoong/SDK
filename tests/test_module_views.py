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
    def test_excludes_only_the_known_dawn_distribution(self):
        (SDK / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="module-view-dawn-", dir=SDK / "build") as tmp:
            root = Path(tmp)
            declaration = "import " + "QtQuick"
            for name, content in {
                "iiGPU/resources/dawn/macos-arm64/include/Foreign.qml": "",
                "iiGPU/resources/dawn/macos-arm64/include/Foreign.cpp": 'const char* view = "' + declaration + '\\nItem {}";',
                "iiGPU/resources/dawn-owned/Local.qml": "",
                "AnotherModule/resources/dawn/Local.qml": "",
                "iiGPU/resources/views/Local.cpp": 'const char* view = "' + declaration + '\\nItem {}";',
            }.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            self.assertEqual(audit.scan_sdk(root), [
                "AnotherModule/resources/dawn/Local.qml",
                "iiGPU/resources/dawn-owned/Local.qml",
            ])
            self.assertEqual(audit.scan_embedded_qml(root), ["iiGPU/resources/views/Local.cpp"])

    def test_scans_project_names_without_ii_prefix(self):
        (SDK / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="module-view-names-", dir=SDK / "build") as tmp:
            root = Path(tmp)
            for name in (
                "Flamewall/src/Views/Panel.qml",
                "Custom SDK/src/Views/Dialog.qml",
                "LVUI/tests/Fixture.qml",
                "LVRS/src/LegacySystem.qml",
                "LVRS-Svelte/src/LegacySystem.qml",
                "build/generated.qml",
                ".recovery/UserData.qml",
                "vendor/Upstream.qml",
            ):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            self.assertEqual(audit.scan_sdk(root), [
                "Custom SDK/src/Views/Dialog.qml",
                "Flamewall/src/Views/Panel.qml",
                "LVUI/tests/Fixture.qml",
            ])

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
                "iiOne/resources/runtime/python/lib/site-packages/torch/External.qml": "",
                "iiOne/resources/runtime/python/lib/dist-packages/numpy/External.qml": "",
                "iiOne/resources/runtime/python/lib/site-packages-owned/Live.qml": "",
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
                "iiOne/Native/examples/NativeDemo.qml", "iiOne/Native/qml/Embedded.qml", "iiOne/Native/src/Live.QML", "iiOne/examples/Demo.qml", "iiOne/qml/Root.qml", "iiOne/resources/runtime/python/lib/site-packages-owned/Live.qml", "iiOne/resources/views/Embedded.qml",
                "iiOne/src/Views/Live.qml", "iiOne/src/tests/Fixture.qml", "iiOne/tests/UnisolatedHarness.qml"])
            cli = subprocess.run([sys.executable, "-B", str(SDK / "tools/audit_module_views.py"), "--sdk", str(root)],
                                 text=True, capture_output=True, check=False)
            self.assertEqual(cli.returncode, 1)
            self.assertIn("Unisolated module QML files: 10", cli.stdout)

    def test_workspace_has_no_unisolated_module_qml(self):
        self.assertEqual(audit.scan_sdk(SDK), [])

    def test_reports_embedded_qml_but_not_legacy_or_qt_headers(self):
        (SDK / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="module-embedded-qml-", dir=SDK / "build") as tmp:
            root = Path(tmp)
            declaration = "import " + "QtQuick"
            qml_import = "import " + "QtQml"
            lvrs_import = "import " + "LVRS"
            files = {
                "iiOne/src/Raw.cpp": 'auto source = R"(' + declaration + '\nItem {})";',
                "iiOne/tests/Escaped.cpp": 'auto source = "' + declaration + '\\nItem {}";',
                "iiOne/src/Engine.cpp": 'auto source = "' + qml_import + '\\nQtObject {}";',
                "iiOne/tests/LegacySystem.py": "source = '''" + lvrs_import + " 1.0\\nWindow {}'''",
                "iiOne/src/Bindings.cpp": '#include <QtQuick/QQuickItem>\n',
                "iiOne/tests/Legacy/Old.cpp": declaration,
                "iiOne/src/vendor/Upstream.cpp": declaration,
                "iiOne/build/Generated.cpp": declaration,
                "LVRS/tests/Old.cpp": declaration,
            }
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            self.assertEqual(audit.scan_embedded_qml(root), [
                "iiOne/src/Engine.cpp", "iiOne/src/Raw.cpp", "iiOne/tests/Escaped.cpp",
                "iiOne/tests/LegacySystem.py"])
            cli = subprocess.run([sys.executable, "-B", str(SDK / "tools/audit_module_views.py"), "--sdk", str(root)],
                                 text=True, capture_output=True, check=False)
            self.assertEqual(cli.returncode, 1)
            self.assertIn("Unisolated embedded QML import declarations: 4", cli.stdout)

    def test_workspace_has_no_unisolated_embedded_qml(self):
        self.assertEqual(audit.scan_embedded_qml(SDK), [])


if __name__ == "__main__":
    unittest.main()
