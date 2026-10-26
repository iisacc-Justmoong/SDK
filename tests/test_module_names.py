"""Canonical module spelling is shared by checkout, C++, CMake and catalogs."""
import json
from pathlib import Path
import unittest


SDK = Path(__file__).resolve().parents[1]
CAMERA = "iiCameraIntegration"
OLD_CAMERA = "iiCamera" + "Inter" + "gration"


class ModuleNameTests(unittest.TestCase):
    def test_camera_checkout_headers_and_export_use_the_canonical_name(self):
        module = SDK / CAMERA
        self.assertTrue(module.is_dir())
        self.assertFalse((SDK / OLD_CAMERA).exists())
        for relative in (
            "src/" + CAMERA + "/" + CAMERA + ".h",
            "src/" + CAMERA + "/Camera.h",
            "src/" + CAMERA + "/Devices.h",
            "cmake/" + CAMERA + "Config.cmake.in",
        ):
            with self.subTest(path=relative):
                self.assertTrue((module / relative).is_file())
        text = (module / "CMakeLists.txt").read_text()
        self.assertIn("project(" + CAMERA, text)
        self.assertIn("BASE_NAME IICAMERAINTEGRATION", text)
        self.assertNotIn(OLD_CAMERA.casefold(), text.casefold())

    def test_catalog_names_and_remote_url_match_the_checkout(self):
        catalog = json.loads((SDK / "workspace-repositories.json").read_text())
        entries = {row["path"]: row for row in catalog["repositories"]}
        self.assertIn("SDK/" + CAMERA, entries)
        self.assertNotIn("SDK/" + OLD_CAMERA, entries)
        self.assertTrue(entries["SDK/" + CAMERA]["url"].endswith("/" + CAMERA + ".git"))
        projects = json.loads((SDK / "Windows/projects.json").read_text())
        self.assertIn(CAMERA, projects)
        self.assertNotIn(OLD_CAMERA, projects)
        for project in projects.values():
            self.assertNotIn(OLD_CAMERA, project.get("dependencies", []))

    def test_society_integration_units_use_the_correct_suffix(self):
        directory = SDK.parent / "Product/Society/src/App/AI"
        for provider in ("HuggingFace", "Civitai", "Ollama"):
            name = provider + "Integration"
            old = provider + "Inter" + "gration"
            with self.subTest(provider=provider):
                self.assertFalse((directory / (old + ".h")).exists())
                text = (directory / (name + ".h")).read_text()
                self.assertIn("class " + name, text)
                self.assertNotIn(old, text)

    def test_license_module_reference_uses_the_registered_spelling(self):
        text = (SDK.parent / "Product/Dreamscapes/README.md").read_text()
        self.assertNotIn("iiLi" + "senseManager", text)


if __name__ == "__main__":
    unittest.main()
