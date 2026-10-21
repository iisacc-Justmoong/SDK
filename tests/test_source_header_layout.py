"""Guard the authored SDK layout; installed/dependency headers are separate contracts."""
import json
import os
from pathlib import Path
import unittest


SDK = Path(__file__).resolve().parents[1]
EXCLUDED = {
    ".git", "build", "node_modules", "vendor", "third_party", "third-party",
    "ThirdParty", "external", "_deps", ".venv", "__pycache__", "dist",
}


def separated_trees(module):
    found = []
    for directory, subdirectories, _ in os.walk(module):
        subdirectories[:] = [name for name in subdirectories
                             if name not in EXCLUDED and not name.startswith("build-")]
        if "include" in subdirectories:
            found.append((Path(directory) / "include").relative_to(module).as_posix())
            subdirectories.remove("include")
        if Path(directory).name == "include":
            found.append(Path(directory).relative_to(module).as_posix())
            subdirectories.clear()
    return found


class SourceHeaderLayoutTests(unittest.TestCase):
    def test_no_authored_include_trees(self):
        violations = {}
        for module in sorted(SDK.iterdir()):
            if module.is_dir() and (module / "CMakeLists.txt").is_file():
                found = separated_trees(module)
                if found:
                    violations[module.name] = found
        self.assertEqual(violations, {}, json.dumps(violations, indent=2))

    def test_migrated_public_headers_are_next_to_implementations(self):
        fixture = json.loads((SDK / "tests/fixtures/source-header-layout.json").read_text())
        for module in fixture["modules"]:
            for relative in module["publicHeaders"]:
                with self.subTest(module=module["module"], header=relative):
                    header = SDK / module["module"] / relative
                    self.assertTrue(header.is_file(), f"missing public header: {header}")
                    self.assertIn("src", header.parts)
                    self.assertNotIn("include", header.relative_to(SDK).parts)
                    self.assertTrue(any(path.suffix in {".cpp", ".cc", ".c", ".mm"}
                                        for path in header.parent.iterdir()),
                                    f"header has no adjacent implementation: {header}")


if __name__ == "__main__":
    unittest.main()
