"""Portable checks for Windows build policy; these do not execute MSVC."""
import ctypes
import importlib.util
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'tests'))
import resource_workspace


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RuntimeRetirementTests(unittest.TestCase):
    def setUp(self):
        self.temporary = resource_workspace.temporary("runtime-retirement-test-")
        self.addCleanup(self.temporary.cleanup)
        self.prefix = Path(self.temporary.name)
        self.environment = mock.patch.dict(os.environ, {
            "USERPROFILE": str(self.prefix), "DOKAN_ROOT": str(self.prefix / "dokan"),
            "SDK_VERIFICATION_DIR": str(self.prefix / "logs"),
            "ONNXRuntime_ROOT": str(self.prefix / "retired-runtime"),
            "PATH": os.pathsep.join([str(self.prefix / "ONNXRuntime/lib"), str(self.prefix / "onnx-free/bin"), "/usr/bin"]),
        })
        self.environment.start()
        self.addCleanup(self.environment.stop)
        for name in ("iiSDKCore", "iiOnnxRuntime"):
            (self.prefix / ".local/SDK" / name).mkdir(parents=True)
        with mock.patch.object(ctypes, "windll", SimpleNamespace(kernel32=mock.Mock()), create=True):
            self.builder = load("windows_builder_retirement", ROOT / "BuildProject.py")

    def test_environment_excludes_retired_runtime_and_preserves_native_sdk(self):
        environment = self.builder.environment("SDK/iiDecision", build=self.prefix / "build")
        self.assertFalse(any("onnx" in name.casefold() for name in environment))
        self.assertNotIn("onnxruntime", environment["PATH"].casefold())
        self.assertIn(str(self.prefix / "onnx-free/bin"), environment["PATH"])
        self.assertNotIn("onnx", environment["CMAKE_PREFIX_PATH"].casefold())
        self.assertIn("iiSDKCore", environment["CMAKE_PREFIX_PATH"])
        self.assertIn("iiSDKCore", environment["PATH"])

    def test_state_captures_use_the_report_workspace(self):
        environment = self.builder.environment("SDK/LVRS", build=self.prefix / "build", qt_lvrs=True)
        capture = Path(environment["LVRS_STATE_CAPTURE_DIR"])
        self.assertTrue(capture.is_relative_to(self.prefix / "logs"))
        self.assertFalse(capture.is_relative_to(self.prefix / "build"))

    def test_build_tree_cannot_be_used_as_verification_state(self):
        with mock.patch.dict(os.environ, {"SDK_VERIFICATION_DIR": str(self.prefix / "build" / "logs")}), \
             mock.patch.object(ctypes, "windll", SimpleNamespace(kernel32=mock.Mock()), create=True):
            with self.assertRaisesRegex(ValueError, "outside build/"):
                load("windows_builder_bad_report_path", ROOT / "BuildProject.py")

    def test_configuration_clears_legacy_options_without_enabling_them(self):
        environment = self.builder.environment("SDK/iiDecision", build=self.prefix / "build")
        with mock.patch.object(self.builder.shutil, "which", return_value="/toolchain/cl.exe"), \
             mock.patch.object(self.builder, "run", return_value={"exit_code": 1}) as run:
            self.builder.configure("SDK/iiDecision", [], build=self.prefix / "build", env=environment)
        command = run.call_args.args[2]
        self.assertIn("-UIIDECISION_ONNX", command)
        self.assertIn("-UONNXRuntime_*", command)
        self.assertFalse(any(value.startswith("-D") and "onnx" in value.casefold() for value in command))

    def test_new_user_can_prepare_environment_before_any_sdk_is_installed(self):
        new_user = self.prefix / "new-user"
        with mock.patch.dict(os.environ, {"USERPROFILE": str(new_user)}):
            environment = self.builder.environment("SDK/iiJob", build=self.prefix / "build")
        self.assertIn("CMAKE_PREFIX_PATH", environment)
        self.assertNotIn(str(new_user / ".local/SDK/iiJob/bin"), environment["PATH"])

    def test_dokan_is_only_forwarded_to_society_container_when_supplied(self):
        with mock.patch.dict(os.environ):
            os.environ.pop("DOKAN_ROOT", None)
            environment = self.builder.environment("SDK/iiJob", build=self.prefix / "build")
            with mock.patch.object(self.builder.shutil, "which", return_value="/toolchain/cl.exe"), \
                 mock.patch.object(self.builder, "run", return_value={"exit_code": 1}) as run:
                self.builder.configure("SDK/iiJob", [], build=self.prefix / "build", env=environment)
                self.assertFalse(any(arg.startswith("-DDokan_ROOT=") for arg in run.call_args.args[2]))
                self.builder.configure("SDK/iiSocietyContainer", [], build=self.prefix / "build", env=environment)
                self.assertFalse(any(arg.startswith("-DDokan_ROOT=") for arg in run.call_args.args[2]))
                environment["DOKAN_ROOT"] = str(self.prefix / "dokan")
                self.builder.configure("SDK/iiSocietyContainer", [], build=self.prefix / "build", env=environment)
                self.assertIn("-DDokan_ROOT=" + environment["DOKAN_ROOT"], run.call_args.args[2])

    def test_configure_uses_current_python_interpreter(self):
        environment = self.builder.environment("SDK/iiJob", build=self.prefix / "build")
        with mock.patch.object(self.builder.shutil, "which", return_value="/toolchain/cl.exe"), \
             mock.patch.object(self.builder, "run", return_value={"exit_code": 1}) as run:
            self.builder.configure("SDK/iiJob", [], build=self.prefix / "build", env=environment)
        command = run.call_args.args[2]
        self.assertIn("-DPython3_EXECUTABLE=" + Path(sys.executable).as_posix(), command)

    def test_explicit_dependency_prefixes_and_qt_directory_are_preserved(self):
        qt = self.prefix / "custom-qt/lib/cmake/Qt6"
        custom = self.prefix / "external-dependencies"
        with mock.patch.dict(os.environ, {"CMAKE_PREFIX_PATH": str(custom), "Qt6_DIR": str(qt)}):
            environment = self.builder.environment("SDK/iiJob", build=self.prefix / "build")
            self.assertIn(str(custom), environment["CMAKE_PREFIX_PATH"].split(';'))
            with mock.patch.object(self.builder.shutil, "which", return_value="/toolchain/cl.exe"), \
                 mock.patch.object(self.builder, "run", return_value={"exit_code": 1}) as run:
                self.builder.configure("SDK/iiJob", [], build=self.prefix / "build", env=environment)
            self.assertIn("-DQt6_DIR=" + str(qt), run.call_args.args[2])
            # Windows normalizes environment variable keys to uppercase.
            uppercase = {key: value for key, value in environment.items() if key.casefold() != 'qt6_dir'}
            uppercase['QT6_DIR'] = str(qt)
            with mock.patch.object(self.builder.shutil, "which", return_value="/toolchain/cl.exe"), \
                 mock.patch.object(self.builder, "run", return_value={"exit_code": 1}) as run:
                self.builder.configure("SDK/iiJob", [], build=self.prefix / "build", env=uppercase)
            self.assertIn("-DQt6_DIR=" + str(qt), run.call_args.args[2])

    def test_explicit_vcpkg_root_controls_search_and_toolchain(self):
        vcpkg = self.prefix / "custom-vcpkg"
        with mock.patch.dict(os.environ, {"VCPKG_ROOT": str(vcpkg)}):
            environment = self.builder.environment("SDK/iiJob", build=self.prefix / "build")
            self.assertIn((vcpkg / 'installed/x64-windows').as_posix(),
                          environment['CMAKE_PREFIX_PATH'].split(';'))
            with mock.patch.object(self.builder.shutil, "which", return_value="/toolchain/cl.exe"), \
                 mock.patch.object(self.builder, "run", return_value={"exit_code": 1}) as run:
                self.builder.configure("SDK/iiJob", [], build=self.prefix / "build", env=environment)
            self.assertIn('-DCMAKE_TOOLCHAIN_FILE=' + (vcpkg / 'scripts/buildsystems/vcpkg.cmake').as_posix(),
                          run.call_args.args[2])

    def test_a_retired_accelerator_skip_is_not_accepted_as_current_evidence(self):
        verifier = load("windows_verifier_retirement", ROOT / "Verify.py")
        result = {"status": "installed", "steps": [{"exit_code": 0,
            "tests": {"total": 2, "passed": 1, "failed": [],
                      "skipped": ["iiDecision.Acceleration.onnx"]}}]}
        self.assertFalse(verifier.runtime_passed("iiDecision", result))
        result["steps"][0]["tests"]["skipped"] = ["iiDecision.Acceleration.metal"]
        self.assertTrue(verifier.runtime_passed("iiDecision", result))


if __name__ == "__main__":
    unittest.main()
