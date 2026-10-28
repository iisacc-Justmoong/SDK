import importlib.util
import ast
import re
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "sdk_independence", Path(__file__).resolve().parents[1] / "tools/audit_sdk_independence.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class BoundaryInventoryTest(unittest.TestCase):
    def setUp(self):
        self.names = {"iiAlpha", "iiBeta", "iiFileProvider", "iiGPU", "iiSDKCore", "LVUI"}

    def test_cmake_commands_and_same_module_components(self):
        text = """
        include(CTest)
        find_package(Qt6 REQUIRED COMPONENTS Core)
        find_package(iiAlpha CONFIG REQUIRED)
        target_link_libraries(alpha PRIVATE iiAlpha::Core)
        # find_package(iiBeta CONFIG REQUIRED)
        find_package(iiBeta CONFIG REQUIRED)
        target_link_libraries(alpha PRIVATE iiBeta::Runtime)
        add_subdirectory(../iiSDKCore core)
        include(../LVUI/cmake/shared.cmake)
        target_include_directories(alpha PRIVATE ../iiFileProvider/src)
        add_modules(../iiGPU)
        """
        found = audit.cmake_references(text, "iiAlpha", self.names)
        self.assertEqual({entry["module"] for entry in found},
                         {"iiBeta", "iiSDKCore", "LVUI", "iiFileProvider", "iiGPU"})
        self.assertEqual(sum(entry["module"] == "iiBeta" for entry in found), 2)

    def test_cpp_headers_and_python_imports(self):
        text = """
        #include <iiAlpha/Self.h>
        #include <iiFileProviderNative.h>
        #include <iiSDKCore/Runtime/EventLoop.h>
        #include "../iiBeta/detail.h"
        // #include <iiGPU/GPU.h>
        """
        found = audit.cpp_references(text, "iiAlpha", self.names)
        self.assertEqual({entry["module"] for entry in found}, {"iiFileProvider", "iiSDKCore", "iiBeta"})
        python = "import ii_file_provider as fp\nfrom iiBeta import Port\n# import iiGPU\n"
        found = audit.python_references(python, "iiAlpha", self.names)
        self.assertEqual({entry["module"] for entry in found}, {"iiFileProvider", "iiBeta"})

    def test_aliases_and_excluded_application_directories(self):
        self.assertEqual(audit.owner_of("iiWebGPU::iiWebGPU", self.names), "iiGPU")
        self.assertEqual(audit.owner_of("iiFileProviderNative", self.names), "iiFileProvider")
        names = self.names | {"iiSocialIntegration"}
        self.assertEqual(audit.owner_of("iiSocialPublish/Publishing.h", names), "iiSocialIntegration")
        self.assertEqual(audit.cpp_references('#include <iiSocialPublish/Publishing.h>', "iiSocialIntegration", names), [])
        self.assertTrue(audit.production_path(Path("examples/CMakeLists.txt")))
        self.assertFalse(audit.production_path(Path("build/generated.cpp")))
        self.assertFalse(audit.production_path(Path("tests/consumer/main.cpp")))
        self.assertTrue(audit.production_path(Path("Native/src/Markup.cpp")))
        self.assertTrue(audit.production_path(Path("CoreIO/CMakeLists.txt")))

    def test_module_scope_does_not_hide_unknown_names(self):
        roots = [Path("/sdk/iiAlpha"), Path("/sdk/iiBeta")]
        self.assertEqual(audit.select_roots(roots, ["iiBeta"]), [roots[1]])
        with self.assertRaises(ValueError):
            audit.select_roots(roots, ["iiMissing"])

    def test_prefilter_includes_aliases_without_counting_self(self):
        pattern = re.compile(audit.prefilter_pattern("iiAlpha", self.names))
        self.assertIsNone(pattern.search("iiAlpha/Self.h"))
        self.assertIsNotNone(pattern.search("iiBeta::Runtime"))
        self.assertIsNotNone(pattern.search("ii_file_provider"))
        self.assertIsNotNone(pattern.search("iiWebGPU::iiWebGPU"))

    def test_symlink_alias_maps_to_its_real_module(self):
        names = self.names | {"iiAlias"}
        aliases = dict(audit.ALIASES, iiAlias="iiAlpha")
        self.assertEqual(audit.owner_of("iiAlias::Runtime", names, aliases), "iiAlpha")
        self.assertIsNone(re.compile(audit.prefilter_pattern("iiAlpha", names, aliases)).search("iiAlias"))
        found = audit.cpp_references('#include <iiAlias/Runtime.h>', "iiBeta", names, aliases)
        self.assertEqual([row["module"] for row in found], ["iiAlpha"])
        roots = [Path("/sdk/iiAlpha"), Path("/sdk/iiBeta")]
        self.assertEqual(audit.select_roots(roots, ["iiAlias"], aliases), [roots[0]])

    def test_prefilter_rejects_generated_and_external_paths(self):
        paths = [b"/sdk/iiAlpha/src/main.cpp", b"/sdk/iiAlpha/build/generated.cpp",
                 b"/sdk/iiAlpha/examples/app.cpp", b"/sibling/unsafe.cpp"]
        result = SimpleNamespace(returncode=0, stdout=bytes([0]).join(paths) + bytes([0]))
        with mock.patch.object(audit.shutil, "which", return_value="/fake/rg"):
            with mock.patch.object(audit.subprocess, "run", return_value=result):
                found = audit.candidate_paths(Path("/sdk/iiAlpha"), self.names)
        self.assertEqual(found, [Path("/sdk/iiAlpha/src/main.cpp"), Path("/sdk/iiAlpha/examples/app.cpp")])

    def test_tool_application_is_only_excluded_when_separately_configured(self):
        app = "# iisdk:consumer-application" + chr(10) + "project(Tool)"
        self.assertTrue(audit.detached_application("project(iiAlpha)", app, "tools"))
        self.assertFalse(audit.detached_application("add_subdirectory(tools)", app, "tools"))
        self.assertFalse(audit.detached_application("add_executable(cli tools/main.cpp)", app, "tools"))
        self.assertFalse(audit.detached_application("project(iiAlpha)", "project(Tool)", "tools"))
        self.assertTrue(audit.detached_application("install(DIRECTORY examples/adapter DESTINATION share)", app,
                                                   "examples/adapter"))
        self.assertFalse(audit.detached_application("add_subdirectory(examples/adapter)", app, "examples/adapter"))
        self.assertFalse(audit.detached_application("add_library(x examples/adapter/App/x.cpp)", app,
                                                    "examples/adapter"))

    def test_checkout_can_own_its_declared_package_name(self):
        self.assertEqual(audit.owned_modules("project(iiBeta LANGUAGES CXX)", "iiAlpha", self.names),
                         {"iiAlpha", "iiBeta"})
        self.assertEqual(audit.owned_modules("# project(iiBeta)\nproject(Other)", "iiAlpha", self.names),
                         {"iiAlpha"})
        self.assertEqual(audit.owned_modules("project(${NAME})", "iiAlpha", self.names), {"iiAlpha"})

    def test_example_is_detached_only_when_root_does_not_use_it(self):
        self.assertTrue(audit.detached_example("project(iiAlpha)", "examples"))
        self.assertFalse(audit.detached_example("add_subdirectory(examples)", "examples"))
        self.assertFalse(audit.detached_example("add_executable(app examples/main.cpp)", "examples"))
        self.assertTrue(audit.detached_example("# add_subdirectory(examples)", "examples"))

    def test_windows_provider_build_does_not_embed_core_adapter(self):
        script = Path(__file__).resolve().parents[1] / "Windows/BuildProject.py"
        tree = ast.parse(script.read_text())
        configure = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "configure")
        assignment = next(node for node in configure.body if isinstance(node, ast.Assign)
                          and any(isinstance(target, ast.Name) and target.id == "options" for target in node.targets))
        index = next(i for i, key in enumerate(assignment.value.keys)
                     if isinstance(key, ast.Constant) and key.value == "iiFileProvider")
        self.assertIn("IIFILEPROVIDER_BUILD_CORE_IO=OFF", ast.literal_eval(assignment.value.values[index]))


if __name__ == "__main__":
    unittest.main()
