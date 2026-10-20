"""Ownership contracts for the independently installable agent and inference SDKs."""
from pathlib import Path
import importlib.util
import json
import unittest

SDK = Path(__file__).resolve().parents[1]


class ModuleBoundaries(unittest.TestCase):
    def test_agent_is_an_independent_package(self):
        agent = SDK / "iiMultimodalAgent"
        self.assertTrue((agent / "CMakeLists.txt").is_file())
        self.assertTrue((agent / "src/iiMultimodalAgent/agent/Engine.cpp").is_file())
        self.assertTrue((agent / "cmake/iiMultimodalAgentConfig.cmake.in").is_file())
        self.assertTrue((agent / "tests/consumer/CMakeLists.txt").is_file())

    def test_inference_does_not_depend_on_agent_or_decision(self):
        inference = SDK / "iiLocalLLM"
        for name in ("CMakeLists.txt", "cmake/iiLocalLLMConfig.cmake.in"):
            text = (inference / name).read_text(encoding="utf-8")
            self.assertNotRegex(text, r"find_(?:package|dependency)\(ii(?:Decision|MultimodalAgent)\b")
            self.assertNotIn("src/agent/", text)
            self.assertNotIn("src/mcp/", text)
        self.assertFalse((inference / "src/agent").exists())
        self.assertFalse((inference / "src/mcp").exists())

    def test_decision_is_owned_by_the_agent(self):
        agent = SDK / "iiMultimodalAgent"
        text = (agent / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("iiLocalLLM::iiLocalLLM", text)
        self.assertIn("iiDecision::iiDecision", text)
        gate = (agent / "src/iiMultimodalAgent/agent/DecisionGate.cpp").read_text(encoding="utf-8")
        self.assertIn("iiDecision::ExactDecisionMaker", gate)
        self.assertNotIn("iiMultimodalAgent", (SDK / "iiDecision/CMakeLists.txt").read_text(encoding="utf-8"))

    def test_all_generation_modules_are_linked_and_installed(self):
        agent = SDK / "iiMultimodalAgent"
        for name in ("CMakeLists.txt", "cmake/iiMultimodalAgentConfig.cmake.in"):
            text = (agent / name).read_text(encoding="utf-8")
            for dependency in ("iiLocalDiffusion", "iiAudioGen", "iiVoiceOver"):
                self.assertIn(dependency, text)
        expected = {"iiLocalLLM", "iiDecision", "iiFileProvider", "iiLocalDiffusion", "iiAudioGen", "iiVoiceOver"}
        windows = json.loads((SDK / "Windows/projects.json").read_text(encoding="utf-8"))
        self.assertEqual(set(windows["iiMultimodalAgent"]["dependencies"]), expected)
        self.assertNotIn("iiDecision", windows["iiLocalLLM"]["dependencies"])

    def test_installer_discovers_the_new_module_without_a_cycle(self):
        spec = importlib.util.spec_from_file_location("agent_install_all", SDK / "install_all.py")
        installer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(installer)
        projects, _ = installer.discover(SDK)
        dependencies = projects["iiMultimodalAgent"]["dependencies"]
        self.assertEqual(dependencies, {"iiLocalLLM", "iiDecision", "iiFileProvider", "iiLocalDiffusion", "iiAudioGen", "iiVoiceOver"})
        order = installer.ordered(projects)
        for dependency in dependencies:
            self.assertLess(order.index(dependency), order.index("iiMultimodalAgent"))


if __name__ == "__main__":
    unittest.main()
