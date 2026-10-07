"""Run the Windows entry point against real Git repositories in PowerShell."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SDK = Path(__file__).resolve().parents[1]
POWERSHELL = os.environ.get("POWERSHELL_BINARY") or shutil.which("pwsh") or shutil.which("powershell")


@unittest.skipUnless(POWERSHELL, "PowerShell is required for the native entry-point tests")
class PowerShellWorkspaceSyncTests(unittest.TestCase):
    def setUp(self):
        (SDK / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="powershell sync ", dir=SDK / "build")
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
                       check=True, capture_output=True)
        self.root = self.base / "Workspace"
        self.manifest = self.base / "manifest.json"
        self.target = self.root / "Product/Thinking Space"
        self.write_manifest()

    @staticmethod
    def git(path, *arguments):
        return subprocess.check_output(["git", "-C", str(path), *arguments],
                                       text=True, stderr=subprocess.DEVNULL).strip()

    def write_manifest(self, path="Product/Thinking Space", **overrides):
        row = {"path": path, "url": str(self.remote), "branch": "main",
               "sourceCommit": self.commit, "lfs": False, **overrides}
        self.manifest.write_text(json.dumps({"version": 1, "repositories": [row],
                                             "directories": ["SDK/Empty"]}), encoding="utf-8")

    def invoke(self, *, inline=False, audit=False):
        command = [str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive"]
        environment = dict(os.environ, WORKSPACE_TEST_ROOT=str(self.root),
                           WORKSPACE_TEST_MANIFEST=str(self.manifest),
                           WORKSPACE_TEST_SCRIPT=str(SDK / "sync_workspace.ps1"))
        if inline:
            command += ["-Command", "& ([scriptblock]::Create((Get-Content -Raw -Encoding UTF8 "
                        "-LiteralPath $env:WORKSPACE_TEST_SCRIPT))) "
                        "-WorkspaceRoot $env:WORKSPACE_TEST_ROOT -Manifest $env:WORKSPACE_TEST_MANIFEST"]
        else:
            command += ["-File", str(SDK / "sync_workspace.ps1"), "-WorkspaceRoot", str(self.root),
                        "-Manifest", str(self.manifest)]
            if audit:
                command.append("-AuditOnly")
        return subprocess.run(command, env=environment, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=60)

    def run_sync(self, *, complete=True, **kwargs):
        process = self.invoke(**kwargs)
        self.assertEqual(process.returncode == 0, complete, process.stdout + process.stderr)
        report = json.loads((self.root / "build/workspace-sync/report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["complete"], complete)
        return report

    def test_clone_with_spaces_repeat_and_inline_console_invocation(self):
        self.assertEqual(self.run_sync()["verified"], 1)
        self.assertEqual(self.git(self.target, "rev-parse", "HEAD"), self.commit)
        self.assertEqual((self.target / "README.md").read_bytes(), b"first\n")
        self.assertEqual(self.git(self.target, "config", "core.autocrlf"), "false")
        self.assertTrue((self.root / "SDK/Empty").is_dir())
        self.assertEqual(self.run_sync(inline=True)["verified"], 1)
        self.assertTrue(self.run_sync(audit=True)["auditOnly"])

    def test_default_manifest_without_script_file_context(self):
        self.run_sync()
        (self.root / "SDK/workspace-repositories.json").write_bytes(self.manifest.read_bytes())
        command = [str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive", "-Command",
                   "& ([scriptblock]::Create((Get-Content -Raw -Encoding UTF8 "
                   "-LiteralPath $env:WORKSPACE_TEST_SCRIPT))) -WorkspaceRoot $env:WORKSPACE_TEST_ROOT"]
        result = subprocess.run(command, env=dict(os.environ, WORKSPACE_TEST_ROOT=str(self.root),
                                WORKSPACE_TEST_SCRIPT=str(SDK / "sync_workspace.ps1")),
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fetch_and_fast_forward_remote_updates(self):
        self.run_sync()
        (self.source / "README.md").write_text("second\n")
        self.git(self.source, "commit", "-am", "Second")
        self.git(self.source, "push", str(self.remote), "main")
        report = self.run_sync()
        self.assertFalse(report["repositories"][0]["snapshotExact"])
        self.assertEqual((self.target / "README.md").read_text(), "second\n")

    def test_dirty_checkout_is_preserved(self):
        self.run_sync()
        (self.target / "README.md").write_text("local changes\n")
        report = self.run_sync(complete=False)
        self.assertIn("uncommitted", report["repositories"][0]["error"])
        self.assertEqual((self.target / "README.md").read_text(), "local changes\n")

    def test_existing_crlf_is_preserved(self):
        self.target.parent.mkdir(parents=True)
        subprocess.run(["git", "clone", "--config", "core.autocrlf=true", str(self.remote),
                        str(self.target)], check=True, capture_output=True)
        self.assertEqual((self.target / "README.md").read_bytes(), b"first\r\n")
        self.run_sync()
        self.assertEqual(self.git(self.target, "config", "core.autocrlf"), "true")

    def test_local_commit_is_preserved(self):
        self.run_sync()
        self.git(self.target, "config", "user.name", "Workspace test")
        self.git(self.target, "config", "user.email", "workspace@example.invalid")
        (self.target / "README.md").write_text("local commit\n")
        self.git(self.target, "commit", "-am", "Local")
        head = self.git(self.target, "rev-parse", "HEAD")
        report = self.run_sync(complete=False)
        self.assertIn("local commits were preserved", report["repositories"][0]["error"])
        self.assertEqual(self.git(self.target, "rev-parse", "HEAD"), head)

    def test_wrong_origin_and_wrong_branch_are_preserved(self):
        self.run_sync()
        self.git(self.target, "checkout", "-b", "local-work")
        report = self.run_sync(complete=False)
        self.assertIn("branch differs", report["repositories"][0]["error"])
        self.git(self.target, "checkout", "main")
        wrong = str(self.base / "wrong.git")
        self.git(self.target, "remote", "set-url", "origin", wrong)
        report = self.run_sync(complete=False)
        self.assertIn("origin differs", report["repositories"][0]["error"])
        self.assertEqual(self.git(self.target, "remote", "get-url", "origin"), wrong)

    def test_occupied_nonrepo_is_preserved(self):
        self.target.mkdir(parents=True)
        (self.target / "keep.txt").write_text("keep")
        self.run_sync(complete=False)
        self.assertEqual((self.target / "keep.txt").read_text(), "keep")
        self.assertFalse((self.target / ".git").exists())

    def test_manifest_invalid_paths_fail_before_writes(self):
        for name in ["../outside", "Product/../outside", "SDK/CON.txt", "Service/folder.",
                     "Product\\Wrong", "Product//Wrong"]:
            with self.subTest(path=name):
                self.write_manifest(path=name)
                result = self.invoke()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid", result.stderr)
                self.assertFalse(self.root.exists())

    def test_symlink_destination_and_root_are_rejected(self):
        self.root.mkdir()
        (self.root / "Product").symlink_to(self.source, target_is_directory=True)
        report = self.run_sync(complete=False)
        self.assertIn("Reparse", report["repositories"][0]["error"])
        self.assertFalse((self.source / "Thinking Space").exists())
        (self.root / "Product").unlink()
        shutil.rmtree(self.root)
        self.root.symlink_to(self.source, target_is_directory=True)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.source / "build").exists())

    def test_parent_container_preserves_existing_nested_repository(self):
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
        self.run_sync()
        self.assertTrue((child / ".git").is_dir())

    def test_missing_remote_reports_git_error_and_continues_other_repositories(self):
        data = json.loads(self.manifest.read_text())
        data["repositories"].insert(0, {**data["repositories"][0], "path": "SDK/Unavailable",
                                         "url": str(self.base / "missing.git")})
        self.manifest.write_text(json.dumps(data))
        report = self.run_sync(complete=False)
        self.assertEqual(report["verified"], 1)
        self.assertEqual(report["total"], 2)
        failure = next(row for row in report["repositories"] if row["path"] == "SDK/Unavailable")
        self.assertIn("does not exist", failure["error"])
        self.assertTrue((self.target / "README.md").exists())


if __name__ == "__main__":
    unittest.main()
