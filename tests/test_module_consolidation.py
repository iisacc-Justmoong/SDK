"""Keep merged module ownership and package discovery consistent."""
import json
from pathlib import Path
import unittest

SDK = Path(__file__).resolve().parents[1]

class ModuleConsolidationTests(unittest.TestCase):
    def test_retired_modules_are_not_independent_builds(self):
        manifest = json.loads((SDK / "workspace-repositories.json").read_text())
        paths = {entry["path"] for entry in manifest["repositories"]}
        paths.update(entry["path"] for entry in manifest.get("bundledModules", []))
        windows = json.loads((SDK / "Windows/projects.json").read_text())
        for retired in ("iiWebGPU", "iiHtmlBlock", "iiBrushTools"):
            with self.subTest(retired=retired):
                self.assertNotIn("SDK/" + retired, paths)
                self.assertNotIn(retired, windows)
                self.assertFalse((SDK / retired).exists())

    def test_active_components_are_owned_by_their_modules(self):
        required = {
            "iiGPU": ("src/WebGPUComponent/WebGPU.cpp", "cmake/iiWebGPUConfig.cmake.in"),
            "iiXml": ("src/Html/iiHtmlBlock.cpp", "cmake/iiHtmlBlockConfig.cmake.in"),
            "iiPaintEngine": ("src/iiBrushTools/Gesture.cpp", "cmake/iiBrushToolsConfig.cmake.in"),
        }
        for module, files in required.items():
            for filename in files:
                with self.subTest(module=module, filename=filename):
                    self.assertTrue((SDK / module / filename).is_file())

if __name__ == "__main__":
    unittest.main()
