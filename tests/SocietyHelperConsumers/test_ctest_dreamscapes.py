from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent


class DreamscapesCTestContract(unittest.TestCase):
    def test_consumer_keeps_three_cases_without_qttest(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertNotRegex(cmake, r'Qt6::Test|COMPONENTS[^\n]*\bTest\b')
        self.assertNotIn('${generation}/tst_LocalSociety.cpp', cmake)
        self.assertIn('SocietyHelperConsumers.dreamscapes_host.${case}', cmake)
        self.assertIn('COMMAND DreamscapesHelperHost "${case}"', cmake)
        source = (ROOT / 'dreamscapes_host_ctest.cpp').read_text()
        for case in ('initialMirrorStaysUnavailableUntilSocietyPublishesIt',
                     'nativePreviewsPublishRealPixelsAcrossPassesAndCleanUp',
                     'generatesOnHostBeforeBackgroundModelDownloadThenUsesLocalCache'):
            self.assertIn(f'runner.run("{case}"', source)
            self.assertIn(case, cmake)
        self.assertNotRegex(source, r'\b(?:QTest|QSignalSpy)\b|<QtTest|\bQTEST_|\bQ(?:VERIFY|COMPARE|TRY|SKIP)\w*')

    def test_signal_observations_and_resources_are_isolated(self):
        source = (ROOT / 'dreamscapes_host_ctest.cpp').read_text()
        for token in ('previewChanged', 'previewPixels', 'previewWasUnpublished',
                      'synchronizedCount', 'synchronizedConnection',
                      'SOCIETY_HELPER_TEST_RESOURCES', 'SOCIETY_CONTAINER_PATH',
                      'modelPreferencesFile', 'QTemporaryDir'):
            self.assertIn(token, source)
        self.assertNotIn('CMAKE_CURRENT_BINARY_DIR', source)

    def test_selected_packages_precede_inherited_header_paths(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        for target in ('iiLocalDiffusion::iiLocalDiffusion', 'iiLocalDiffusion::ModelPackaging'):
            self.assertIn(f'set_property(TARGET {target} PROPERTY SYSTEM FALSE)', cmake)
        self.assertIn('$<TARGET_PROPERTY:iiLocalDiffusion::ModelPackaging,INTERFACE_INCLUDE_DIRECTORIES>', cmake)
        for variable in ('DYLD_LIBRARY_PATH', 'LD_LIBRARY_PATH', 'PATH'):
            self.assertIn(f'{variable}=path_list_prepend:$<TARGET_FILE_DIR:iiLocalDiffusion::iiLocalDiffusion>', cmake)


if __name__ == '__main__':
    unittest.main()
