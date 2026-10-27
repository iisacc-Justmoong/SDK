"""Account package names must agree across source, exports and workspace tooling."""
import json
from pathlib import Path
import unittest


SDK = Path(__file__).resolve().parents[1]
NAME = "iiAccountManager"
RETIRED = "ii" + "Acount" + "Manager"


class AccountManagerNameTests(unittest.TestCase):
    def test_checkout_and_public_headers_use_the_canonical_name(self):
        module = SDK / NAME
        self.assertTrue(module.is_dir(), str(module))
        self.assertFalse((SDK / RETIRED).exists())
        for relative in (
            NAME + ".h",
            "src/" + NAME + "/AccountManager.h",
            "src/" + NAME + "/Export.h",
            "Native/src/" + NAME + "/NativeAccount.h",
            "Native/src/" + NAME + "/BrowserSession.h",
            "cmake/" + NAME + "Config.cmake.in",
            "Native/cmake/" + NAME + "NativeConfig.cmake.in",
        ):
            with self.subTest(path=relative):
                self.assertTrue((module / relative).is_file())

    def test_workspace_and_windows_catalogs_register_the_same_module(self):
        manifest = json.loads((SDK / "workspace-repositories.json").read_text())
        repositories = {entry["path"]: entry for entry in manifest["repositories"]}
        self.assertIn("SDK/" + NAME, repositories)
        self.assertNotIn("SDK/" + RETIRED, repositories)
        self.assertTrue(repositories["SDK/" + NAME]["url"].endswith("/" + NAME + ".git"))
        projects = json.loads((SDK / "Windows/projects.json").read_text())
        self.assertIn(NAME, projects)
        self.assertNotIn(RETIRED, projects)
        for project in projects.values():
            self.assertNotIn(RETIRED, project.get("dependencies", []))

    def test_export_macro_and_package_templates_use_the_canonical_name(self):
        module = SDK / NAME
        export = (module / "src" / NAME / "Export.h").read_text()
        self.assertIn("IIACCOUNTMANAGER_EXPORT", export)
        self.assertIn("IIACCOUNTMANAGER_BUILD", export)
        for relative in ("CMakeLists.txt", "Native/CMakeLists.txt"):
            with self.subTest(path=relative):
                text = (module / relative).read_text()
                self.assertIn("project(" + NAME, text)
                self.assertIn(NAME + "::", text)
                self.assertNotIn(RETIRED.casefold(), text.casefold())

    def test_native_qt_and_product_dependents_use_the_canonical_name(self):
        workspace = SDK.parent
        paths = (
            "SDK/iiCalendar/tests/iisacc_bridge.cpp",
            "SDK/iiLicenseManager/src/iiLicenseManager/LicenseClient.h",
            "SDK/iiLicenseManager/Native/cmake/iiLicenseManagerNativeConfig.cmake.in",
            "SDK/iiUpdateManager/Native/src/iiUpdateManager/NativeUpdateManager.cpp",
            "SDK/iiSocietyHelper/src/iiSocietyHelper/AccountSession.h",
            "SDK/iiSocietyHelper/cmake/iiSocietyHelperConfig.cmake.in",
            "SDK/iiVirtualizer/cmake/iiVirtualizerConfig.cmake.in",
            "Product/Society/App/Society/Account/AccountController.h",
            "Product/Congregation/App/Engine/LicenseService.h",
            "Product/build-policy/SingleApp.cmake",
        )
        for relative in paths:
            with self.subTest(path=relative):
                text = (workspace / relative).read_text()
                self.assertIn(NAME, text)
                self.assertNotIn(RETIRED.casefold(), text.casefold())


if __name__ == "__main__":
    unittest.main()
