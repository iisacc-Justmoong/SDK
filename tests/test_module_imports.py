"""Guard the extensionless public C++ entries of the independent SDK packages."""

from os import walk
from pathlib import Path
import re
import unittest

SDK = Path(__file__).resolve().parents[1]
CXX_SUFFIXES = {".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".mm"}
APP_SOURCE_DIRS = {"App", "app", "src", "Source", "tests", "Tests", "test", "tools", "native"}
THIRD_PARTY_DIRS = {"vendor", "Vendor", "third_party", "ThirdParty", "resources", "build", "node_modules"}
# Tests of private widget state and clipboard glue use the checkout directly.
# These names are deliberately absent from the public iiGUI API.
IIGUI_INTERNAL_TEST_HEADERS = {"iiGUI/WidgetState.h", "iiGUI/DesktopCommon.h"}


def cpp_projects():
    """Find independent C++ SDKs, omitting aliases and retired packages."""
    for cmake in sorted(SDK.glob("*/CMakeLists.txt")):
        module = cmake.parent
        if module.is_symlink() or module.name in {"LVRS", "Windows"}:
            continue
        if re.search(r"\bproject\s*\([^)]*\bCXX\b", cmake.read_text(encoding="utf-8")):
            yield module


def application_sources():
    """Yield owned application code without traversing packages or build output."""
    for family in ("Product", "Service"):
        for candidate in (SDK.parent / family).glob("*"):
            if not candidate.is_dir() or candidate.name.startswith("."):
                continue
            projects = list(candidate.iterdir()) if candidate.name == "Utility" else [candidate]
            for project in projects:
                for area in APP_SOURCE_DIRS:
                    for directory, subdirs, names in walk(project / area):
                        subdirs[:] = [name for name in subdirs
                                      if not name.startswith(".") and name not in THIRD_PARTY_DIRS]
                        for name in names:
                            source = Path(directory) / name
                            if source.suffix in CXX_SUFFIXES and not source.is_symlink():
                                yield source, area


class ModulePublicImports(unittest.TestCase):
    """Verify the SDK export contract and canonical imports at application boundaries."""

    def test_every_cpp_module_exports_an_extensionless_entry(self):
        """Each package owns both its source header and relocatable install rule."""
        modules = list(cpp_projects())
        self.assertGreater(len(modules), 1)
        for module in modules:
            with self.subTest(module=module.name):
                entry = module / "src" / "ModuleImports" / module.name
                self.assertTrue(entry.is_file(), f"Missing <{module.name}>")
                self.assertIn("#include", entry.read_text(encoding="utf-8"))
                registration = module / "cmake" / "ModuleImports.cmake"
                text = registration.read_text(encoding="utf-8")
                self.assertIn("BUILD_INTERFACE", text)
                self.assertIn("INSTALL_INTERFACE", text)
                self.assertIn("/iisdk", text)
                self.assertIn("install(FILES", text)
                self.assertIn("ModuleImports.cmake", (module / "CMakeLists.txt").read_text(encoding="utf-8"))

    def test_application_uses_public_module_names(self):
        """Reject leaf includes in product sources while keeping private iiGUI tests possible."""
        modules = {module.name for module in cpp_projects()}
        modules.update({"iiBrushTools", "iiWebGPU", "iiImageCodec", "iiSocialPublish"})
        leaves = re.compile(r"^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]", re.M)
        violations = []
        count = 0
        for source, area in application_sources():
            for include in leaves.findall(source.read_text(encoding="utf-8", errors="replace")):
                if include in modules:
                    count += 1
                prefix = include.partition("/")[0]
                if prefix == include or prefix not in modules:
                    continue
                if area.lower() in {"test", "tests"} and include in IIGUI_INTERNAL_TEST_HEADERS:
                    continue
                if (source.parent / include).is_file():
                    continue
                violations.append(f"{source.relative_to(SDK.parent)}: {include}")
        self.assertGreater(count, 0, "No application module imports were checked")
        self.assertEqual(violations, [], "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
