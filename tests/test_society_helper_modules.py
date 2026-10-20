"""Workspace ownership and dependency contracts for the unified Society SDK."""
import importlib.util
import json
from pathlib import Path
import re
import unittest

SDK = Path(__file__).resolve().parents[1]
LEGACY = {"iiSocietyGeneration", "iiSocietyMonitor", "iiSocietySync"}
spec = importlib.util.spec_from_file_location("society_installer", SDK / "install_all.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class SocietyHelperModulesTests(unittest.TestCase):
    def test_retired_sdks_are_not_discovered_or_registered(self):
        projects, _ = installer.discover(SDK)
        self.assertFalse(LEGACY.intersection(projects))
        registry = json.loads((SDK / "workspace-repositories.json").read_text())
        names = {Path(item["path"]).name for item in registry["repositories"]}
        self.assertFalse(LEGACY.intersection(names))
        self.assertIn("iiSocietyHelper", projects)

    def test_component_dependencies_are_acyclic_and_ordered(self):
        projects, _ = installer.discover(SDK)
        order = installer.ordered(projects)
        deps = projects["iiSocietyHelper"]["dependencies"]
        self.assertTrue({"iiSDKCore", "iiSocietyContainer", "iiServerHost",
                         "iiLocalDiffusion", "iiSharedCanvas"}.issubset(deps))
        for dependency in deps:
            self.assertLess(order.index(dependency), order.index("iiSocietyHelper"))
        self.assertFalse(LEGACY.intersection(deps))

    def test_windows_metadata_has_no_retired_node_or_edge(self):
        projects = json.loads((SDK / "Windows/projects.json").read_text())
        self.assertFalse(LEGACY.intersection(projects))
        for name, project in projects.items():
            self.assertFalse(LEGACY.intersection(project.get("dependencies", [])), name)
        self.assertIn("iiSocietyHelper", projects["iiPhotoLibrary"]["dependencies"])

    def test_consumers_use_only_the_new_package(self):
        paths = [SDK / "iiPhotoLibrary/CMakeLists.txt",
                 SDK / "tests/SocietyHelperConsumers/CMakeLists.txt"]
        for name in ["Society", "Dreamscapes"]:
            product = SDK.parent / "Product" / name / "CMakeLists.txt"
            if product.is_file():
                paths.append(product)
        for path in paths:
            text = path.read_text()
            version = re.search(r"find_package\(\s*iiSocietyHelper\s+(\d+)\.(\d+)\.(\d+)\b", text)
            self.assertIsNotNone(version, str(path))
            self.assertGreaterEqual(tuple(map(int, version.groups())), (0, 10, 0), str(path))
            for legacy in LEGACY:
                self.assertNotRegex(text, rf"(?:find_package|find_dependency)\(\s*{legacy}\b")
                self.assertNotIn(legacy + "::", text)


if __name__ == "__main__":
    unittest.main()
