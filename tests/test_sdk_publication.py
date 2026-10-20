"""Keep independently delivered SDK sources in the clone manifest."""
import json
from pathlib import Path
import subprocess
import unittest


SDK = Path(__file__).resolve().parents[1]


class SdkPublicationTests(unittest.TestCase):
    def test_all_implemented_sdk_modules_are_registered(self):
        manifest = json.loads((SDK / "workspace-repositories.json").read_text())
        rows = {row["path"]: row for row in manifest["repositories"]}
        modules = [path for path in SDK.iterdir() if path.is_dir()
                   and not path.name.startswith((".", "build"))
                   and any((path / marker).is_file()
                           for marker in ("CMakeLists.txt", "install.sh", "package.json"))]
        self.assertTrue(modules)
        for module in modules:
            with self.subTest(module=module.name):
                row = rows.get("SDK/" + module.name)
                self.assertIsNotNone(row, "Implemented SDK module missing from clone manifest")
                self.assertTrue(row["url"].startswith("https://github.com/iisacc-Justmoong/"))
                self.assertTrue(row["branch"])

    def test_shared_performance_sources_are_not_ignored(self):
        for name in ("tools/performance_audit.py", "tools/run_performance_benchmarks.py",
                     "docs/PERFORMANCE_REVIEW.md", "docs/PERFORMANCE_REVIEW_20261020.md",
                     "benchmarks/performance/CMakeLists.txt"):
            with self.subTest(path=name):
                result = subprocess.run(["git", "-C", str(SDK), "check-ignore", "--quiet",
                                         "--no-index", name], check=False)
                self.assertEqual(result.returncode, 1)

    def test_build_outputs_remain_ignored(self):
        for name in ("build/git-publish-20261021/inventory.json",
                     "benchmarks/performance/build/CMakeCache.txt",
                     "tools/__pycache__/performance_audit.cpython-313.pyc"):
            with self.subTest(path=name):
                result = subprocess.run(["git", "-C", str(SDK), "check-ignore", "--quiet",
                                         "--no-index", name], check=False)
                self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
