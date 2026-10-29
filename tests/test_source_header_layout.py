"""Guard the authored SDK layout; installed/dependency headers are separate contracts."""
import json
import os
from pathlib import Path
import tempfile
import unittest


SDK = Path(__file__).resolve().parents[1]
EXCLUDED = {
    ".git", "build", "node_modules", "vendor", "third_party", "third-party",
    "ThirdParty", "external", "_deps", ".venv", "__pycache__", "dist",
    "resources",
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


def repeated_source_directories(module):
    candidates = (module / "src" / module.name,
                  module / "Native" / "src" / module.name)
    return [path for path in candidates if path.is_dir()]


def sdk_source_modules(root):
    return [module for module in sorted(root.iterdir())
            if module.is_dir() and module.name not in EXCLUDED
            and not module.name.startswith((".", "build-"))
            and ((module / "src").is_dir() or (module / "Native" / "src").is_dir())]


class SourceHeaderLayoutTests(unittest.TestCase):
    def test_resource_dependency_headers_are_not_authored_include_trees(self):
        with tempfile.TemporaryDirectory(dir=SDK / "build") as directory:
            module = Path(directory)
            (module / "src/include").mkdir(parents=True)
            (module / "resources/dependency/include").mkdir(parents=True)
            self.assertEqual(separated_trees(module), ["src/include"])

    def test_repeated_directory_check_allows_extensionless_header(self):
        with tempfile.TemporaryDirectory(dir=SDK / "build") as directory:
            module = Path(directory) / "example"
            (module / "src").mkdir(parents=True)
            (module / "src/example").write_text("#pragma once\n")
            self.assertEqual(repeated_source_directories(module), [])
            (module / "Native/src/example").mkdir(parents=True)
            self.assertEqual(repeated_source_directories(module), [module / "Native/src/example"])

    def test_source_module_discovery_includes_non_cmake_modules(self):
        with tempfile.TemporaryDirectory(dir=SDK / "build") as directory:
            root = Path(directory)
            (root / "OtherSDK/src/OtherSDK").mkdir(parents=True)
            (root / "build/src/build").mkdir(parents=True)
            modules = sdk_source_modules(root)
            self.assertEqual(modules, [root / "OtherSDK"])
            self.assertEqual(repeated_source_directories(modules[0]),
                             [root / "OtherSDK/src/OtherSDK"])

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

    def test_all_sdk_modules_do_not_repeat_their_name_in_src(self):
        violations = {}
        for module in sdk_source_modules(SDK):
            duplicates = repeated_source_directories(module)
            if duplicates:
                violations[module.name] = [str(path.relative_to(SDK)) for path in duplicates]
        self.assertEqual(violations, {}, json.dumps(violations, indent=2))


if __name__ == "__main__":
    unittest.main()
