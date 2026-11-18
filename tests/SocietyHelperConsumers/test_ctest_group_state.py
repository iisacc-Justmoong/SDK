from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent


class ConsumerGroupStateContract(unittest.TestCase):
    def test_product_source_and_resource_workspace_do_not_require_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        link = re.search(r"target_link_libraries\(SocietyGroupState\s+([^)]*)\)", cmake)
        self.assertIsNotNone(link)
        self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn('target_include_directories(SocietyGroupState PRIVATE "${society}/App")', cmake)
        self.assertIn('SOCIETY_TEST_DIRECTORY="${society}/resources"', cmake)
        self.assertIn("SocietyHelperConsumers.group_state_ctest_contract", cmake)

    def test_society_consumer_sources_follow_the_current_product_layout(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        headers = (ROOT / "headers.cpp").read_text()
        for source in ("AccountController.cpp", "LicenseBroker.cpp", "LocalLicenseBroker.cpp"):
            self.assertIn("${society}/App/Society/Account/" + source, cmake)
        self.assertIn("${society}/App/Society/Network/NetworkDriveController.cpp", cmake)
        self.assertNotIn("${society}/src/App/", cmake)
        self.assertNotIn('#include "App/', headers)
        self.assertIn('#include "Society/Account/AccountController.h"', headers)

    def test_projection_consumer_has_explicit_dependencies(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        link = re.search(r"target_link_libraries\(SocietyDependencyContract\s+([^)]*)\)", cmake)
        self.assertIsNotNone(link)
        self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn("iiWebServer::iiWebServer", link.group(1))
        self.assertIn('SOCIETY_TEST_DIRECTORY="${society}/resources"', cmake[link.end():])


if __name__ == "__main__":
    unittest.main()
