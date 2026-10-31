"""Test WebGPU verification paths without opening a GPU or running a consumer."""
import os
from pathlib import Path
import subprocess
import unittest
from unittest import mock

import relocation
import resource_workspace

ROOT = Path(__file__).resolve().parents[2]


class BuildLayoutTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "Creating directory symlinks requires Windows developer privileges")
    def test_relocation_rejects_a_symlinked_build(self):
        with resource_workspace.temporary(ROOT, "webgpu-layout-fixture-") as fixture:
            source = Path(fixture) / "project"
            source.mkdir()
            (source / "real").mkdir()
            (source / "build").symlink_to(source / "real", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "cannot be a symlink"):
                relocation.main([str(source), str(source / "build"), "--compile-only"])

    def test_inputs_and_compiled_consumer_are_separate_and_removed(self):
        with resource_workspace.temporary(ROOT, "webgpu-layout-fixture-") as fixture:
            root = Path(fixture)
            source = root / "project"
            source.mkdir()
            build = source / "build"
            commands = []

            def run(command, **kwargs):
                commands.append(command)
                if command[1] == "--install":
                    prefix = Path(command[command.index("--prefix") + 1])
                    self.assertTrue(prefix.is_relative_to(source / "resources"))
                    (prefix / "lib/cmake/iiGPU").mkdir(parents=True)
                    (prefix / "lib/cmake/iiGPU/iiGPUConfig.cmake").write_text("# relocatable\n")
                if "-B" in command:
                    self.assertTrue(Path(command[command.index("-B") + 1]).is_relative_to(build))
                return subprocess.CompletedProcess(command, 0, stdout="")

            with mock.patch.object(relocation.subprocess, "run", side_effect=run):
                relocation.main([str(source), str(build), "Release", "--compile-only"])
            self.assertEqual(len(commands), 3)
            self.assertFalse(any(command[0] == "ctest" for command in commands))
            self.assertEqual(list(build.iterdir()), [])
            self.assertEqual(list((source / "resources/.state/test-workspaces").iterdir()), [])

    def test_failed_install_cleans_both_owned_directories(self):
        with resource_workspace.temporary(ROOT, "webgpu-layout-fixture-") as fixture:
            source = Path(fixture) / "project"
            source.mkdir()
            build = source / "build"
            failure = subprocess.CompletedProcess([], 1, stdout="install failed")
            with mock.patch.object(relocation.subprocess, "run", return_value=failure):
                with self.assertRaisesRegex(RuntimeError, "install failed"):
                    relocation.main([str(source), str(build), "--compile-only"])
            self.assertEqual(list(build.iterdir()), [])
            self.assertEqual(list((source / "resources/.state/test-workspaces").iterdir()), [])


if __name__ == "__main__":
    unittest.main()
