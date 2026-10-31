"""Exercise workspace synchronization with real local Git remotes."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import resource_workspace
import unittest


SDK = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sync_workspace", SDK / "sync_workspace.py")
sync = importlib.util.module_from_spec(SPEC)


class WorkspaceSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SPEC.loader.exec_module(sync)

    def setUp(self):
        self.temporary = resource_workspace.temporary("workspace-sync-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.source = self.base / "source"
        self.source.mkdir()
        self.git(self.source, "init", "-b", "main")
        self.git(self.source, "config", "user.name", "Workspace test")
        self.git(self.source, "config", "user.email", "workspace@example.invalid")
        (self.source / "README.md").write_text("first\n", encoding="utf-8")
        self.git(self.source, "add", ".")
        self.git(self.source, "commit", "-m", "First")
        self.commit = self.git(self.source, "rev-parse", "HEAD")
        self.remote = self.base / "remote.git"
        subprocess.run(["git", "clone", "--bare", str(self.source), str(self.remote)],
                       capture_output=True, check=True)
        self.root = self.base / "Workspace"
        self.manifest = self.base / "manifest.json"
        self.write_manifest()

    @staticmethod
    def git(path, *arguments):
        return subprocess.check_output(["git", "-C", str(path), *arguments],
                                       stderr=subprocess.DEVNULL, text=True).strip()

    def write_manifest(self, path="Product/Thinking Space", **overrides):
        row = {"path": path, "url": str(self.remote), "branch": "main",
               "sourceCommit": self.commit, "lfs": False, **overrides}
        self.manifest.write_text(json.dumps({"version": 1, "repositories": [row],
                                             "directories": ["SDK/Empty"]}), encoding="utf-8")

    def run_sync(self):
        return sync.synchronize(self.root, self.manifest)

    def test_report_is_machine_readable_stdout_without_build_files(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            report = self.run_sync()
        lines = [line.removeprefix('WORKSPACE_SYNC_REPORT=') for line in output.getvalue().splitlines()
                 if line.startswith('WORKSPACE_SYNC_REPORT=')]
        self.assertEqual(len(lines), 1)
        self.assertEqual(json.loads(lines[0]), report)
        self.assertFalse((self.root / 'build/workspace-sync').exists())

    def test_clone_preserves_space_and_structure_and_is_repeatable(self):
        first = self.run_sync()
        self.assertEqual(first["verified"], 1)
        target = self.root / "Product/Thinking Space"
        self.assertEqual(self.git(target, "rev-parse", "HEAD"), self.commit)
        self.assertEqual((target / "README.md").read_text(), "first\n")
        self.assertTrue((self.root / "SDK/Empty").is_dir())
        self.assertEqual(self.run_sync()["verified"], 1)

    def test_update_fetches_new_remote_commit_and_preserves_snapshot(self):
        self.run_sync()
        (self.source / "README.md").write_text("second\n", encoding="utf-8")
        self.git(self.source, "commit", "-am", "Second")
        self.git(self.source, "push", str(self.remote), "main")
        result = self.run_sync()
        self.assertEqual(result["verified"], 1)
        self.assertFalse(result["repositories"][0]["snapshotExact"])
        self.assertEqual((self.root / "Product/Thinking Space/README.md").read_text(), "second\n")

    def test_dirty_checkout_is_preserved_and_reported(self):
        self.run_sync()
        target = self.root / "Product/Thinking Space/README.md"
        target.write_text("local change\n")
        result = self.run_sync()
        self.assertEqual(result["verified"], 0)
        self.assertEqual(target.read_text(), "local change\n")
        self.assertIn("uncommitted", result["repositories"][0]["error"])

    def test_existing_crlf_checkout_keeps_its_line_ending_configuration(self):
        target = self.root / "Product/Thinking Space"
        target.parent.mkdir(parents=True)
        subprocess.run(["git", "clone", "--config", "core.autocrlf=true",
                        str(self.remote), str(target)], capture_output=True, check=True)
        self.assertEqual((target / "README.md").read_bytes(), b"first\r\n")
        self.assertEqual(self.git(target, "status", "--porcelain"), "")
        self.assertEqual(self.run_sync()["verified"], 1)
        self.assertEqual(self.git(target, "config", "core.autocrlf"), "true")

    def test_wrong_remote_is_not_modified(self):
        self.run_sync()
        target = self.root / "Product/Thinking Space"
        self.git(target, "remote", "set-url", "origin", str(self.base / "wrong.git"))
        self.assertEqual(self.run_sync()["verified"], 0)
        self.assertEqual(self.git(target, "remote", "get-url", "origin"),
                         str(self.base / "wrong.git"))

    def test_occupied_nonrepo_is_not_overwritten(self):
        target = self.root / "Product/Thinking Space"
        target.mkdir(parents=True)
        (target / "keep.txt").write_text("keep")
        self.assertEqual(self.run_sync()["verified"], 0)
        self.assertEqual((target / "keep.txt").read_text(), "keep")
        self.assertFalse((target / ".git").exists())

    def test_path_escape_is_rejected_before_writes(self):
        self.write_manifest(path="../outside")
        with self.assertRaises(ValueError):
            self.run_sync()
        self.assertFalse(self.root.exists())

    def test_symlink_destination_is_rejected(self):
        self.root.mkdir()
        (self.root / "Product").symlink_to(self.source, target_is_directory=True)
        result = self.run_sync()
        self.assertEqual(result["verified"], 0)
        self.assertFalse((self.source / "Thinking Space").exists())

    def test_symlink_root_is_rejected_before_report_writes(self):
        self.root.symlink_to(self.source, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.run_sync()
        self.assertFalse((self.source / "build").exists())

    def test_local_commit_is_not_discarded(self):
        self.run_sync()
        target = self.root / "Product/Thinking Space"
        self.git(target, "config", "user.name", "Workspace test")
        self.git(target, "config", "user.email", "workspace@example.invalid")
        (target / "README.md").write_text("local commit\n")
        self.git(target, "commit", "-am", "Local")
        head = self.git(target, "rev-parse", "HEAD")
        self.assertEqual(self.run_sync()["verified"], 0)
        self.assertEqual(self.git(target, "rev-parse", "HEAD"), head)

    def test_git_container_is_initialized_without_copying_nested_repo(self):
        # Parent checkouts may need to be added around existing ignored children.
        (self.source / ".gitignore").write_text("/*/\n")
        self.git(self.source, "add", ".gitignore")
        self.git(self.source, "commit", "-m", "Ignore children")
        self.git(self.source, "push", str(self.remote), "main")
        child = self.root / "Product/Existing"
        child.mkdir(parents=True)
        self.git(child, "init", "-b", "main")
        self.write_manifest(path="Product")
        data = json.loads(self.manifest.read_text())
        data["directories"].append("Product/Existing")
        self.manifest.write_text(json.dumps(data))
        self.assertEqual(self.run_sync()["verified"], 1)
        self.assertTrue((child / ".git").is_dir())


if __name__ == "__main__":
    unittest.main()
