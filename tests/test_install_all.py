"""Execution contracts for the SDK installer, with temporary SDKs under build/."""
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("install_all", ROOT / "install_all.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallAllTest(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="installer test ", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def project(self, name, dependencies=(), fail=False):
        path = self.root / name
        path.mkdir()
        (path / "install.sh").write_text("#!/bin/sh\n" + ("exit 7\n" if fail else "printf '%s\\n' installed > installed.txt\n"))
        if dependencies:
            (path / "install-dependencies.txt").write_text("\n".join(dependencies))
        return path

    def test_new_sdk_is_discovered_and_dependency_is_installed_first(self):
        self.project("A Consumer", ["Z Foundation"])
        self.project("Z Foundation")
        self.project("New Library")
        (self.root / "Empty Placeholder").mkdir()
        (self.root / ".private").mkdir()
        self.assertEqual(installer.main(["--root", str(self.root)]), 0)
        report = json.loads(next((self.root / "build/install-all").glob("*/report.json")).read_text())
        self.assertLess(report["order"].index("Z Foundation"), report["order"].index("A Consumer"))
        self.assertEqual(report["skipped_directories"], ["Empty Placeholder"])
        self.assertEqual(report["projects"]["New Library"]["status"], "installed")

    def test_failure_blocks_dependents_but_runs_independent_projects(self):
        self.project("A", fail=True)
        self.project("B", ["A"])
        self.project("C")
        self.assertEqual(installer.main(["--root", str(self.root)]), 1)
        self.assertFalse((self.root / "B/installed.txt").exists())
        self.assertTrue((self.root / "C/installed.txt").exists())

    def test_cycle_fails_before_installation(self):
        self.project("A", ["B"])
        self.project("B", ["A"])
        with self.assertRaisesRegex(ValueError, "cycle"):
            installer.main(["--root", str(self.root)])
        self.assertFalse((self.root / "A/installed.txt").exists())

    def test_targeted_retry_reuses_verified_dependencies(self):
        self.project("Consumer", ["Foundation"])
        self.project("Foundation", fail=True)
        self.assertEqual(installer.main(["--root", str(self.root), "--only", "Consumer", "--no-dependencies"]), 0)
        self.assertTrue((self.root / "Consumer/installed.txt").exists())
        self.assertFalse((self.root / "Foundation/installed.txt").exists())

    def test_cmake_dependencies_and_dry_run(self):
        a, b = self.project("FolderA"), self.project("FolderB")
        (a / "CMakeLists.txt").write_text("project(PackageA)\nfind_package(PackageB CONFIG REQUIRED)\n")
        (b / "CMakeLists.txt").write_text("project(PackageB)\n")
        projects, _ = installer.discover(self.root)
        self.assertEqual(projects["FolderA"]["dependencies"], {"FolderB"})
        self.assertEqual(installer.main(["--root", str(self.root), "--dry-run"]), 0)
        self.assertFalse((a / "installed.txt").exists())

    def test_cmake_fallback_really_builds_tests_and_installs(self):
        path = self.root / "NewNative"
        path.mkdir()
        (path / "main.cpp").write_text("int main() { return 0; }\n")
        (path / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(NewNative LANGUAGES CXX)
include(CTest)
add_executable(probe main.cpp)
add_test(NAME probe COMMAND probe)
install(TARGETS probe RUNTIME DESTINATION bin)
''')
        prefix = self.root / ".installation"
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(prefix)]), 0)
        subprocess.run([str(prefix / "NewNative/bin/probe")], check=True)
        # A removed IDE can leave its bundled Ninja/Make in an otherwise valid
        # build cache. Reinstallation must select the available equivalent tool.
        cache = path / "build/CMakeCache.txt"
        cache.write_text(re.sub(r"(CMAKE_MAKE_PROGRAM:[^=]+=).*", r"\1/deleted IDE/bin/build-tool", cache.read_text()))
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(prefix)]), 0)
        subprocess.run([str(prefix / "NewNative/bin/probe")], check=True)


if __name__ == "__main__":
    unittest.main()
