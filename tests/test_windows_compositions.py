import importlib.util
import ast
import datetime
import json
import os
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock

path = Path(__file__).resolve().parents[1] / "Windows/Verify.py"
spec = importlib.util.spec_from_file_location("windows_compositions", path)
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


def passed_project():
    return {"status": "installed", "steps": [{"exit_code": 0,
        "tests": {"total": 1, "passed": 1, "failed": [], "skipped": []}}]}


def step(names, skipped=()):
    return {"exit_code": 0, "tests": {"total": len(names), "passed": len(names)-len(skipped),
        "failed": [], "skipped": list(skipped), "names": list(names)}}


class CompositionGateTest(unittest.TestCase):
    def setUp(self):
        self.definitions = {"gpu": {"owner": "iiDecision", "requires": ("iiDecision", "iiGPU"),
            "configuration": ("gpu-consumer",), "required_tests": {"vulkan", "reuse"}}}
        self.projects = {"iiDecision": passed_project(), "iiGPU": passed_project()}
        self.builder = mock.Mock()

    def test_requires_all_prerequisite_modules(self):
        self.projects["iiGPU"]["status"] = "failed"
        result = verify.check_compositions(self.builder, self.definitions, self.projects)
        self.assertEqual(result["gpu"]["status"], "dependency-failed")
        self.builder.check_additional_configuration.assert_not_called()

    def test_rejects_a_passing_suite_that_dropped_required_cases(self):
        self.builder.check_additional_configuration.return_value = [step(["vulkan"])]
        result = verify.check_compositions(self.builder, self.definitions, self.projects)
        self.assertEqual(result["gpu"]["status"], "missing-contract-tests")
        self.assertFalse(verify.compositions_passed(result))

    def test_requires_each_configured_case_to_pass(self):
        self.builder.check_additional_configuration.return_value = [step(["vulkan", "reuse"])]
        result = verify.check_compositions(self.builder, self.definitions, self.projects)
        self.assertTrue(verify.compositions_passed(result))
        self.builder.check_additional_configuration.assert_called_once_with("SDK/iiDecision", ("gpu-consumer",))

    def test_partial_selection_does_not_force_other_modules(self):
        result = verify.check_compositions(self.builder, self.definitions, self.projects, {"iiDecision"})
        self.assertEqual(result, {})
        self.builder.check_additional_configuration.assert_not_called()

    def test_unapproved_vulkan_skip_fails_the_gate(self):
        self.builder.check_additional_configuration.return_value = [step(["vulkan", "reuse"], ["vulkan"])]
        result = verify.check_compositions(self.builder, self.definitions, self.projects)
        self.assertFalse(verify.compositions_passed(result))

    def test_real_definitions_preserve_moved_contract_tests(self):
        definitions = verify.composition_definitions(Path("/workspace/SDK"), Path("/profile"))
        self.assertIn("iiDecision.Acceleration.vulkan_reuse", definitions["decision-gpu"]["required_tests"])
        self.assertIn("iiDecision.InstalledGpu.vulkan", definitions["decision-gpu"]["required_tests"])
        self.assertIn("iiFileProvider.CoreIO", definitions["provider-coreio"]["required_tests"])
        self.assertIn("iiAudioGen.GeneratedFile", definitions["audio-file-output"]["required_tests"])
        self.assertEqual(set(definitions["xml-file-input"]["requires"]), {"iiXml", "iiFileProvider"})
        self.assertIn("iiXml.FileProvider", definitions["xml-file-input"]["required_tests"])
        self.assertEqual(set(definitions["calendar-account"]["requires"]), {"iiCalendar", "iiAccountManager", "iiFileProvider"})
        self.assertIn("iiCalendar.iisacc_bridge", definitions["calendar-account"]["required_tests"])
        self.assertEqual(set(definitions["calendar-file"]["requires"]), {"iiCalendar", "iiFileProvider"})
        self.assertEqual(definitions["calendar-file"]["required_tests"], {"iiCalendar.file_storage", "iiCalendar.file_accounts"})
        self.assertEqual(set(definitions["voice-file"]["requires"]), {"iiVoiceOver", "iiFileProvider"})
        self.assertIn("iiVoiceOver.FileProvider", definitions["voice-file"]["required_tests"])
        self.assertEqual(set(definitions["voice-cli"]["requires"]), {"iiVoiceOver", "iiSDKCore", "iiFileProvider"})
        self.assertTrue(definitions["voice-cli"]["build_only"])
        self.assertEqual(set(definitions["camera-image"]["requires"]), {"iiCameraIntegration", "iiGraphicAdjust", "iiFileProvider"})
        self.assertEqual(definitions["camera-image"]["required_tests"], {
            "iiCamera.processing.import", "iiCamera.processing.pipeline", "iiCamera.processing.encoding",
            "iiCamera.processing.validation", "iiCamera.processing.capture", "iiCamera.processing.cli",
            "iiCamera.processing.package"})
        self.assertEqual(set(definitions["camera-media"]["requires"]), {"iiCameraIntegration", "iiFileProvider"})
        self.assertEqual(definitions["camera-media"]["required_tests"], {
            "iiCamera.media", "iiCamera.cli_help", "iiCamera.cli_frame"})
        self.assertIn("-DIICAMERA_FFMPEG_EXECUTABLE=/workspace/Tools/FFmpeg/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe",
                      definitions["camera-media"]["configuration"][4])
        self.assertEqual(set(definitions["midi-async"]["requires"]), {"iiCSMIDI", "iiSDKCore"})
        self.assertIn("iiCSMIDI.Async", definitions["midi-async"]["required_tests"])
        self.assertEqual(set(definitions["midi-file"]["requires"]), {"iiCSMIDI", "iiFileProvider", "iiJson"})
        self.assertIn("iiCSMIDI.authorship", definitions["midi-file"]["required_tests"])
        self.assertEqual(set(definitions["network-balancer"]["requires"]), {"iiNetwork", "iiSDKCore", "iiJson"})
        self.assertEqual(definitions["network-balancer"]["required_tests"], {
            "iiNetwork.LoadBalancer", "iiNetwork.LoadBalancerExample"})
        self.assertEqual(set(definitions["json-core-value"]["requires"]), {"iiJson", "iiSDKCore"})
        self.assertEqual(definitions["json-core-value"]["required_tests"], {"iiJson.CoreValue"})
        self.assertEqual(set(definitions["paint-file"]["requires"]), {"iiPaintEngine", "iiFileProvider"})
        self.assertEqual(definitions["paint-file"]["required_tests"], {"iiPaintEngine.FileProvider"})
        self.assertEqual(definitions["paint-file"]["configuration"][1],
                         Path("/workspace/SDK/iiPaintEngine/Example/FileProvider"))
        self.assertTrue(definitions["graphic-examples"]["build_only"])

    def test_camera_build_does_not_require_other_sdk_modules(self):
        projects = json.loads((path.parent / "projects.json").read_text())
        self.assertEqual(projects["iiCameraIntegration"]["dependencies"], [])

    def test_midi_build_does_not_require_other_sdk_modules(self):
        projects = json.loads((path.parent / "projects.json").read_text())
        self.assertEqual(projects["iiCSMIDI"]["dependencies"], [])

    def test_json_build_does_not_require_other_sdk_modules(self):
        projects = json.loads((path.parent / "projects.json").read_text())
        self.assertEqual(projects["iiJson"]["dependencies"], [])

    def test_paint_build_does_not_require_other_sdk_modules(self):
        projects = json.loads((path.parent / "projects.json").read_text())
        self.assertEqual(projects["iiPaintEngine"]["dependencies"], [])

    def test_errors_are_reported_as_failed_composition(self):
        self.builder.check_additional_configuration.side_effect = RuntimeError("missing package")
        result = verify.check_compositions(self.builder, self.definitions, self.projects)
        self.assertEqual(result["gpu"]["status"], "failed")
        self.assertIn("missing package", result["gpu"]["error"])

    def test_prerequisite_that_becomes_stale_invalidates_composition(self):
        result = {"gpu": {"status": "installed", "requires": ["iiDecision", "iiGPU"]}}
        self.projects["iiGPU"]["status"] = "stale-source"
        verify.propagate_invalid_compositions(result, self.projects)
        self.assertEqual(result["gpu"]["status"], "dependency-failed")
        self.assertEqual(result["gpu"]["blocked_by"], ["iiGPU"])

    def test_build_only_composition_requires_configure_and_build_success(self):
        self.definitions["gpu"]["required_tests"] = set()
        self.definitions["gpu"]["build_only"] = True
        self.builder.check_additional_configuration.return_value = [{"exit_code": 0}, {"exit_code": 0}]
        self.assertTrue(verify.compositions_passed(verify.check_compositions(self.builder, self.definitions, self.projects)))
        self.builder.check_additional_configuration.return_value = [{"exit_code": 0}, {"exit_code": 1}]
        self.assertFalse(verify.compositions_passed(verify.check_compositions(self.builder, self.definitions, self.projects)))

    def test_windows_builder_records_executed_case_names(self):
        builder_source = path.parent / "BuildProject.py"
        tree = ast.parse(builder_source.read_text())
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "test")
        module = ast.fix_missing_locations(ast.Module(body=[function], type_ignores=[]))
        namespace = {"datetime": datetime, "RUN": path.parent / "build", "ROOT": path.parents[2],
                     "ET": ET, "run": mock.Mock(return_value={"exit_code": 0})}
        exec(compile(module, str(builder_source), "exec"), namespace)
        junit = ET.ElementTree(ET.fromstring(
            '<testsuites><testcase name="vulkan"/><testcase name="metal"><skipped/></testcase></testsuites>'))
        with mock.patch.object(Path, "exists", return_value=True), mock.patch.object(ET, "parse", return_value=junit):
            result = namespace["test"]("SDK/iiDecision", build=path.parent / "build")
        self.assertEqual(result["tests"]["names"], ["vulkan", "metal"])
        self.assertEqual(result["tests"]["skipped"], ["metal"])

    def test_composition_receives_its_installed_owner_runtime(self):
        builder_source = path.parent / "BuildProject.py"
        tree = ast.parse(builder_source.read_text())
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "add_composition_runtime_path")
        module = ast.fix_missing_locations(ast.Module(body=[function], type_ignores=[]))
        namespace = {"os": os, "Path": Path}
        exec(compile(module, str(builder_source), "exec"), namespace)
        add_runtime = namespace["add_composition_runtime_path"]
        environment = {"PATH": "existing"}
        add_runtime("midi-async-consumer", Path("/stage/midi"), environment)
        self.assertEqual(environment["PATH"].split(os.pathsep)[0], str(Path("/stage/midi/bin")))
        add_runtime("midi-async-consumer", Path("/stage/midi"), environment)
        self.assertEqual(environment["PATH"].count(str(Path("/stage/midi/bin"))), 1)
        untouched = {"PATH": "existing"}
        add_runtime("qt", Path("/stage/midi"), untouched)
        self.assertEqual(untouched["PATH"], "existing")
        definitions = verify.composition_definitions(Path("/workspace/SDK"), Path("/profile"))
        for definition in definitions.values():
            phase, _, _, prefix, _, _ = definition["configuration"]
            environment = {"PATH": "existing"}
            add_runtime(phase, prefix, environment)
            self.assertEqual(environment["PATH"].split(os.pathsep)[0], str(prefix / "bin"), phase)


if __name__ == "__main__":
    unittest.main()
