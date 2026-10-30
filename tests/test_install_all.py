"""Execution contracts for the SDK installer, with temporary SDKs under build/."""
import importlib.util
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("install_all", ROOT / "install_all.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallAllTest(unittest.TestCase):
    def test_independent_modules_do_not_require_obsolete_gpu_edges(self):
        projects, _ = installer.discover(ROOT)
        self.assertNotIn('iiSDKCore', projects['iiGPU']['dependencies'])
        self.assertNotIn('iiGPU', projects['iiDecision']['dependencies'])

    @unittest.skipUnless(os.name == "nt", "Windows shell selection")
    def test_windows_shell_selection_never_starts_wsl(self):
        with self.assertRaisesRegex(ValueError, "not WSL"):
            installer.installer_bash({"BASH_BINARY": "C:/Windows/System32/bash.exe"})
        bash = installer.installer_bash({"PATH": "C:/Windows/System32"})
        self.assertIsNotNone(bash)
        self.assertNotEqual(Path(bash).parent.name.lower(), "system32")
        result = subprocess.run([bash, "--version"], capture_output=True, text=True, check=True)
        self.assertIn("GNU bash", result.stdout)

    def test_windows_drive_prefixes_are_preserved(self):
        self.project("Native")
        with patch.dict(os.environ, {"CMAKE_PREFIX_PATH": "C:/SDK;D:/Qt"}), \
             patch.object(installer.os, "pathsep", ";"), \
             patch.object(installer, "install_project", return_value={}) as install:
            self.assertEqual(installer.main(["--root", str(self.root)]), 0)
        prefixes = install.call_args.args[4]["CMAKE_PREFIX_PATH"].split(";")
        self.assertIn("C:/SDK", prefixes)
        self.assertIn("D:/Qt", prefixes)

    def test_utf8_project_sources_are_discovered(self):
        path = self.project("Unicode")
        (path / "CMakeLists.txt").write_bytes("# 한국어 주석\nproject(Unicode)\n".encode("utf-8"))
        self.assertIn("Unicode", installer.discover(self.root)[0])

    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="installer test ", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def project(self, name, dependencies=(), fail=False):
        path = self.root / name
        path.mkdir()
        (path / "install.sh").write_text("#!/bin/sh\n" + ("exit 7\n" if fail else "printf '%s\\n' installed > installed.txt\n"), encoding="utf-8", newline="\n")
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
        report_path = next((self.root / ".state/install-all").glob("*/report.json"))
        report = json.loads(report_path.read_text())
        self.assertFalse((report_path.parent / "New Library.log").exists())
        self.assertNotIn("log", report["projects"]["New Library"])
        self.assertFalse((self.root / "build/install-all").exists())
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
        # Recover a cached path to a removed build tool.
        cache = path / "build/CMakeCache.txt"
        cache.write_text(re.sub(r"(CMAKE_MAKE_PROGRAM:[^=]+=).*", r"\1/deleted IDE/bin/build-tool", cache.read_text()))
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(prefix)]), 0)
        subprocess.run([str(prefix / "NewNative/bin/probe")], check=True)

    def test_no_tests_builds_and_installs_without_running_application(self):
        path = self.root / "Native"
        path.mkdir()
        (path / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(Native NONE)
include(CTest)
option(NATIVE_BUILD_EXAMPLES "Build examples" ON)
if(BUILD_TESTING)
  add_custom_target(unwanted_test ALL COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/test-built")
endif()
if(NATIVE_BUILD_EXAMPLES)
  add_custom_target(unwanted_example ALL COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/example-built")
endif()
add_test(NAME must_not_run COMMAND "${CMAKE_COMMAND}" -E false)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" DESTINATION share)
''')
        prefix = self.root / ".installation"
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(prefix), "--no-tests"]), 0)
        report = json.loads(next((self.root / ".state/install-all").glob("*/report.json")).read_text())
        self.assertEqual(report["projects"]["Native"]["tests"], "not_run")
        self.assertTrue((prefix / "Native/share/CMakeLists.txt").exists())
        self.assertFalse((path / "build/test-built").exists())
        self.assertFalse((path / "build/example-built").exists())

    def test_default_cmake_build_includes_tests_but_not_examples(self):
        path = self.root / "Native"
        path.mkdir()
        (path / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(Native NONE)
include(CTest)
option(NATIVE_BUILD_EXAMPLES "Build examples" ON)
if(BUILD_TESTING)
  add_custom_target(test_target ALL COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/test-built")
  add_test(NAME native_contract COMMAND "${CMAKE_COMMAND}" -E true)
endif()
if(NATIVE_BUILD_EXAMPLES)
  add_custom_target(example ALL COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/example-built")
endif()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" DESTINATION share)
''')
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(self.root / ".installation")]), 0)
        self.assertTrue((path / "build/test-built").exists())
        self.assertFalse((path / "build/example-built").exists())

    def test_running_status_is_persisted_before_installer(self):
        self.project("Native")
        def check_report(*args):
            report = json.loads(next((self.root / ".state/install-all").glob("*/report.json")).read_text())
            self.assertEqual(report["projects"]["Native"]["status"], "running")
            return {}
        with patch.object(installer, "install_project", side_effect=check_report):
            self.assertEqual(installer.main(["--root", str(self.root)]), 0)

    def test_report_and_log_creation_uses_provider(self):
        self.project("Native")
        with patch.object(installer, "provider_operation", wraps=installer.provider_operation) as provider:
            self.assertEqual(installer.main(["--root", str(self.root)]), 0)
        operations = [call.args[0] for call in provider.call_args_list]
        self.assertIn("mkdir", operations)
        self.assertIn("create", operations)
        self.assertIn("write", operations)

    def test_provider_log_creation_failure_is_reported(self):
        self.project("Native")
        original = installer.provider_operation
        def fail_create(operation, path, content=""):
            if operation == "create":
                raise ValueError("provider unavailable")
            return original(operation, path, content)
        with patch.object(installer, "provider_operation", side_effect=fail_create):
            self.assertEqual(installer.main(["--root", str(self.root)]), 1)
        report = json.loads(next((self.root / ".state/install-all").glob("*/report.json")).read_text())
        self.assertEqual(report["projects"]["Native"]["status"], "failed")
        self.assertIn("provider unavailable", report["projects"]["Native"]["error"])

    def test_existing_host_profile_is_preserved_without_explicit_refresh(self):
        path = self.root / "Native"
        path.mkdir()
        (path / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(Native NONE)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" DESTINATION share)
''')
        host = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}[installer.platform.system()]
        prefix = self.root / ".installation"
        profile = prefix / "Native/platforms" / host / "share"
        profile.mkdir(parents=True)
        marker = profile / "CMakeLists.txt"
        marker.write_text("preserve different backend")
        os.utime(marker, (1, 1))
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(prefix), "--no-tests"]), 0)
        self.assertEqual(marker.read_text(), "preserve different backend")
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(prefix), "--no-tests", "--refresh-host-prefix"]), 0)
        self.assertNotEqual(marker.read_text(), "preserve different backend")

    def test_manifest_must_contain_a_file_in_the_requested_prefix(self):
        build = self.root / "build"
        build.mkdir(exist_ok=True)
        elsewhere = self.root / "unrelated"
        elsewhere.write_text("old installation")
        (build / "install_manifest.txt").write_text(str(elsewhere) + "\n")
        with self.assertRaisesRegex(ValueError, "requested prefix"):
            installer.check_manifest(build, "Native", self.root / "prefix")

    def test_lvrs_defaults_to_cmake_even_when_custom_installer_exists(self):
        path = self.project("LVRS", fail=True)
        (path / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(LVRS NONE)
include(CTest)
add_test(NAME probe COMMAND "${CMAKE_COMMAND}" -E true)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" DESTINATION share)
''')
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(self.root / ".installation")]), 0)

    def test_sdk_package_cache_variants_are_cleared_before_explicit_overrides(self):
        path = self.root / "iiFileProvider"
        path.mkdir()
        (path / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(iiFileProvider NONE)
include(CTest)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" DESTINATION share)
''')
        commands = []
        def record(command, directory, environment, log):
            commands.append(command)
        with patch.object(installer, "run", side_effect=record), patch.object(installer, "check_manifest", return_value=1):
            self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(self.root / ".installation"), "--no-tests", "--define=iiFileProvider=-DiiFileProviderNative_DIR=/explicit"]), 0)
        configure = commands[0]
        self.assertIn("-Uii*_DIR", configure)
        self.assertLess(configure.index("-UiiFileProvider*_DIR"), configure.index("-DiiFileProviderNative_DIR=/explicit"))

    def test_stale_variant_cache_selects_current_installed_package(self):
        dependency = self.root / "iiDep"
        dependency.mkdir()
        (dependency / "CMakeLists.txt").write_text("project(iiDep NONE)\n")
        consumer = self.root / "Consumer"
        consumer.mkdir()
        (consumer / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(Consumer NONE)
find_package(iiDepNative CONFIG REQUIRED)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/selected.txt" "${DEP_SELECTION}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/selected.txt" DESTINATION share)
''')
        old = self.root / "old-package"
        old.mkdir()
        (old / "iiDepNativeConfig.cmake").write_text('set(DEP_SELECTION "old")\n')
        prefix = self.root / ".installation"
        current = prefix / "iiDep/lib/cmake/iiDepNative"
        current.mkdir(parents=True)
        (current / "iiDepNativeConfig.cmake").write_text('set(DEP_SELECTION "current")\n')
        subprocess.run(["cmake", "-S", str(consumer), "-B", str(consumer / "build"),
                        f"-DiiDepNative_DIR={old}"], check=True, capture_output=True)
        self.assertEqual((consumer / "build/selected.txt").read_text(), "old")
        self.assertEqual(installer.main(["--root", str(self.root), "--prefix-root", str(prefix),
                                         "--only", "Consumer", "--no-dependencies", "--no-tests"]), 0)
        self.assertEqual((prefix / "Consumer/share/selected.txt").read_text(), "current")


if __name__ == "__main__":
    unittest.main()
