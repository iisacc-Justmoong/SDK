"""The release gate must fail closed without running builds or changing Git."""
import copy
import json
import os
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import resource_workspace
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[1]/'Windows/Verify.py'
spec = importlib.util.spec_from_file_location('windows_verification', path)
verification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verification)


class WindowsVerificationTests(unittest.TestCase):
    def test_reports_are_confined_to_windows_resource_state(self):
        with resource_workspace.temporary('windows-report-layout-') as temporary:
            root = Path(temporary)
            state = root/'SDK/Windows/resources/.state/verification'
            self.assertEqual(verification.report_directory(None, root, 'fixed-run'), state/'fixed-run')
            self.assertEqual(verification.report_directory(state/'explicit', root, 'unused'), state/'explicit')
            for outside in (root/'build/maintenance', root/'SDK/Windows/resources/other'):
                with self.subTest(outside=outside), self.assertRaisesRegex(RuntimeError, 'resource'):
                    verification.report_directory(outside, root, 'unused')

    @unittest.skipIf(os.name == 'nt', 'Directory symlinks require Windows developer privileges')
    def test_linked_windows_resource_root_is_rejected(self):
        with resource_workspace.temporary('windows-report-link-') as temporary:
            root = Path(temporary)
            (root/'SDK/Windows').mkdir(parents=True)
            (root/'other').mkdir()
            (root/'SDK/Windows/resources').symlink_to(root/'other', target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, 'resource'):
                verification.report_directory(None, root, 'run')

    @unittest.skipUnless(os.name == 'nt', 'Windows SDK environment helper')
    def test_owned_foundation_dll_directory_precedes_transitive_runtime_copies(self):
        spec = importlib.util.spec_from_file_location('sdk_builder_runtime_order_test', path.parent/'BuildProject.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with resource_workspace.temporary('windows-runtime-') as temporary:
            root = Path(temporary)
            core = root/'.local/SDK/iiSDKCore/bin'
            consumer = root/'.local/SDK/iiSDKBuilder/bin'
            core.mkdir(parents=True)
            consumer.mkdir(parents=True)
            with patch.object(builder, 'ROOT', root), \
                    patch.dict(os.environ, {'USERPROFILE': str(root), 'PATH': str(consumer)}):
                paths = builder.environment('SDK/iiGUI')['PATH'].split(os.pathsep)
            self.assertLess(paths.index(str(core)), paths.index(str(consumer)))

    @unittest.skipUnless(os.name == 'nt', 'Windows SDK environment helper')
    def test_dependencies_precede_fresh_nested_outputs_and_unrelated_runtime_copies_follow(self):
        spec = importlib.util.spec_from_file_location('sdk_builder_nested_runtime_test', path.parent/'BuildProject.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with resource_workspace.temporary('windows-nested-runtime-') as temporary:
            root = Path(temporary)
            core = root/'.local/SDK/iiSDKCore/bin'
            consumer = root/'.local/SDK/iiSDKBuilder/bin'
            native = root/'SDK/iiFileProvider/build/Native'
            for directory in (core, consumer, native):
                directory.mkdir(parents=True)
            (native/'iiFileProviderNative.dll').write_bytes(b'fixture')
            with patch.object(builder, 'ROOT', root), \
                    patch.dict(os.environ, {'USERPROFILE': str(root), 'PATH': str(consumer)}):
                paths = builder.environment('SDK/iiFileProvider')['PATH'].split(os.pathsep)
            self.assertLess(paths.index(str(core)), paths.index(str(native)))
            self.assertLess(paths.index(str(native)), paths.index(str(consumer)))

    @unittest.skipUnless(os.name == 'nt', 'Windows SDK environment helper')
    def test_nested_build_environment_does_not_repeat_sdk_or_system_paths(self):
        spec = importlib.util.spec_from_file_location('sdk_builder_environment_test', path.parent/'BuildProject.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with resource_workspace.temporary('windows-path-') as temporary:
            root = Path(temporary)
            prefix = root/'.local/SDK/example'
            (prefix/'bin').mkdir(parents=True)
            original_path = os.pathsep.join([str(prefix/'bin'), 'C:/Windows/System32',
                                             str(prefix/'bin'), 'C:/Windows/System32'])
            with patch.object(builder, 'ROOT', root), \
                    patch.dict(os.environ, {'USERPROFILE': str(root), 'PATH': original_path}):
                result = builder.environment('SDK/LVRS-Svelte')
            entries = result['PATH'].split(os.pathsep)
            normalized = [os.path.normcase(os.path.normpath(entry)) for entry in entries]
            self.assertEqual(len(normalized), len(set(normalized)))
            self.assertIn(os.path.normcase(os.path.normpath(str(prefix/'bin'))), normalized)
            self.assertIn(os.path.normcase(os.path.normpath('C:/Windows/System32')), normalized)

    @unittest.skipUnless(os.name == 'nt', 'Windows SDK configure helper')
    def test_relocated_checkout_discards_the_old_cmake_source_cache(self):
        spec = importlib.util.spec_from_file_location('sdk_builder_relocation_test', path.parent/'BuildProject.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with resource_workspace.temporary('windows-relocation-') as temporary:
            root = Path(temporary)
            build = root/'SDK/iiWebServer/build'
            build.mkdir(parents=True)
            (build/'CMakeCache.txt').write_text(
                'CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_CXX_COMPILER:FILEPATH=C:/Compiler/cl.exe\n'
                'CMAKE_HOME_DIRECTORY:INTERNAL=C:/old/SDK/iiServerHost\n', encoding='utf-8')
            with patch.object(builder, 'ROOT', root), patch.object(builder.shutil, 'which', return_value='C:/Compiler/cl.exe'), \
                    patch.object(builder, 'environment', return_value={'CMAKE_PREFIX_PATH': ''}), \
                    patch.dict(os.environ, {'DOKAN_ROOT': str(root/'dokan')}), \
                    patch.object(builder, 'run', return_value={'exit_code': 1}) as run:
                builder.configure('SDK/iiWebServer', [])
            self.assertIn('--fresh', run.call_args.args[2])

    def test_every_buildable_sdk_is_registered_and_native_ui_is_ordered(self):
        sdk = path.parents[1]
        projects = json.loads((path.parent/'projects.json').read_text(encoding='utf-8'))
        discovered = {directory.name for directory in sdk.iterdir() if directory.is_dir() and not directory.is_symlink()
                      and any((directory/marker).is_file() for marker in ('CMakeLists.txt', 'package.json'))}
        self.assertEqual(set(projects), discovered)
        order = verification.dependency_order(projects)
        for dependency, consumer in [('iiSDKCore', 'LVUI'), ('LVUI', 'iiGUI'),
                                     ('iiGUI', 'LVRS'), ('iiVisualCodec', 'iiSharedCanvas')]:
            with self.subTest(dependency=dependency, consumer=consumer):
                self.assertIn(dependency, projects[consumer]['dependencies'])
                self.assertLess(order.index(dependency), order.index(consumer))
        for module in ('iiGPU', 'iiDecision', 'iiGraphicAdjust', 'iiAudioGen', 'iiFileProvider', 'iiXml'):
            with self.subTest(independent=module):
                self.assertEqual(projects[module]['dependencies'], [])
        definitions = verification.composition_definitions(sdk, Path('/profile'))
        self.assertEqual(set(definitions['decision-gpu']['requires']), {'iiDecision', 'iiGPU'})
        self.assertEqual(set(definitions['provider-coreio']['requires']), {'iiFileProvider', 'iiSDKCore'})

    def test_native_gpu_skips_do_not_hide_vulkan_execution_failure(self):
        allowed=verification.OPTIONAL_SKIPS['iiGPU']
        self.assertEqual(allowed, {'iiGPU.metal.'+case for case in
            ('contracts','transfers','dispatch','lifecycle','concurrency','interop')})
        self.assertIn('iiDecision.Acceleration.metal_reuse', verification.OPTIONAL_SKIPS['iiDecision'])
        self.assertNotIn('iiDecision.Acceleration.vulkan_reuse', verification.OPTIONAL_SKIPS['iiDecision'])
        options=(path.parent/'BuildProject.py').read_text(encoding='utf-8')
        self.assertIn("'iiGPU': ['IIGPU_METAL=OFF', 'IIGPU_VULKAN=ON']", options)

    @unittest.skipUnless(os.name == 'nt' and shutil.which('cl.exe'), 'Requires the Windows MSVC development shell')
    def test_clean_native_build_and_changed_header_relink_the_consumer(self):
        with resource_workspace.temporary('windows-order-input-') as temporary, \
             resource_workspace.compiled(path.parent, 'windows-order-build-') as build_directory:
            source = Path(temporary)
            build = Path(build_directory)
            (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.28)\nproject(OrderProbe LANGUAGES CXX)\nadd_library(value STATIC value.cpp)\ntarget_compile_features(value PUBLIC cxx_std_23)\nadd_executable(probe main.cpp)\ntarget_link_libraries(probe PRIVATE value)\n')
            (source/'value.h').write_text('inline constexpr int result = 7;\n')
            (source/'value.cpp').write_text('#include "value.h"\nint value() { return result; }\n')
            (source/'main.cpp').write_text('#include <cstdio>\nint value();\nint main() { std::printf("%d", value()); }\n')
            def run(command):
                result = subprocess.run(list(map(str, command)), capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout+'\n'+result.stderr)
                return result.stdout.strip()
            run(['cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_CXX_COMPILER='+Path(shutil.which('cl.exe')).as_posix(),
                 '-DCMAKE_PROJECT_INCLUDE='+(path.parent/'WindowsMsvcPrefix.cmake').as_posix()])
            run([sys.executable, path.parent/'Configure-WindowsNinja.py', build])
            run(['cmake', '--build', build, '--parallel', '4'])
            self.assertEqual(run([build/'probe.exe']), '7')
            (source/'value.h').write_text('inline constexpr int result = 11;\n')
            run(['cmake', '--build', build, '--parallel', '4'])
            self.assertEqual(run([build/'probe.exe']), '11')

    @unittest.skipUnless(os.name == 'nt', 'MSVC cache validation runs on Windows')
    def test_compiler_reset_cannot_silently_change_release_configuration(self):
        spec = importlib.util.spec_from_file_location('sdk_builder_cache_test', path.parent/'BuildProject.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        compiler = 'C:/Compiler/cl.exe'
        values = {'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_CXX_COMPILER': compiler,
                  'CMAKE_INSTALL_PREFIX': (Path(os.environ['USERPROFILE'])/'.local/SDK/iiSDKCore').as_posix(),
                  'CMAKE_TOOLCHAIN_FILE': (builder.ROOT/'Tools/vcpkg/scripts/buildsystems/vcpkg.cmake').as_posix(),
                  'CMAKE_PROJECT_INCLUDE': (builder.ROOT/'SDK/Windows/WindowsMsvcPrefix.cmake').as_posix()}
        cache = '\n'.join(name+':STRING='+value for name, value in values.items())
        self.assertTrue(builder.cache_matches_release(cache, 'SDK/iiSDKCore', compiler))
        self.assertFalse(builder.cache_matches_release(cache.replace('=Release', '=Debug'), 'SDK/iiSDKCore', compiler))
        self.assertFalse(builder.cache_matches_release(cache.replace('C:/Compiler/cl.exe', 'C:/Other/cl.exe'), 'SDK/iiSDKCore', compiler))
        self.assertFalse(builder.cache_matches_release(cache.replace('CMAKE_PROJECT_INCLUDE', 'REMOVED_INCLUDE'), 'SDK/iiSDKCore', compiler))

    def receipt(self):
        return {'status': 'installed', 'steps': [
            {'phase': 'build', 'exit_code': 0},
            {'phase': 'test', 'exit_code': 0, 'tests': {'total': 2, 'passed': 2, 'failed': [], 'skipped': []}},
            {'phase': 'install', 'exit_code': 0}]}

    def test_dependency_order_and_invalid_graphs(self):
        graph = {'app': {'dependencies': ['middle']}, 'base': {'dependencies': []}, 'middle': {'dependencies': ['base']}}
        self.assertEqual(verification.dependency_order(graph), ['base', 'middle', 'app'])
        graph['base']['dependencies'] = ['app']
        with self.assertRaises(ValueError):
            verification.dependency_order(graph)
        with self.assertRaises(ValueError):
            verification.dependency_order({'app': {'dependencies': ['missing']}})
        for excluded in ['Deprecated', 'UTILITY']:
            with self.assertRaises(ValueError):
                verification.dependency_order({excluded: {'dependencies': []}})

    def test_missing_or_extra_project_cannot_open_the_gate(self):
        projects = {'one': {}, 'two': {}}
        self.assertFalse(verification.all_passed(projects, {'one': self.receipt()}))
        results = {name: self.receipt() for name in ['one', 'two', 'extra']}
        self.assertFalse(verification.all_passed(projects, results))
        self.assertTrue(verification.all_passed(projects, {name: self.receipt() for name in projects}))
        self.assertFalse(verification.all_passed({}, {}))

    def test_failure_or_no_execution_cannot_open_the_gate(self):
        for invalid in ['configure-failed', 'test-failed', 'source-changed-during-verification', 'stale-source']:
            receipt = self.receipt()
            receipt['status'] = invalid
            self.assertFalse(verification.runtime_passed('one', receipt))
        receipt = self.receipt()
        receipt['steps'][0]['exit_code'] = 2
        self.assertFalse(verification.runtime_passed('one', receipt))
        receipt = self.receipt()
        receipt['steps'][1]['tests'] = {'total': 0, 'passed': 0, 'failed': [], 'skipped': []}
        self.assertFalse(verification.runtime_passed('one', receipt))
        receipt['steps'][1]['tests']['passed'] = 1
        self.assertFalse(verification.runtime_passed('one', receipt))

    def test_failed_prerequisites_block_consumers_and_leave_independent_projects_runnable(self):
        results = {'good': self.receipt(), 'bad': {'status': 'build-failed', 'steps': []},
                   'blocked': {'status': 'dependency-failed', 'steps': []}}
        self.assertEqual(verification.failed_dependencies({'dependencies': ['good', 'bad']}, results), ['bad'])
        self.assertEqual(verification.failed_dependencies({'dependencies': ['blocked']}, results), ['blocked'])
        self.assertEqual(verification.failed_dependencies({'dependencies': ['good']}, results), [])
        self.assertEqual(verification.failed_dependencies({'dependencies': []}, results), [])
        self.assertEqual(verification.failed_dependencies({'dependencies': ['external']}, results), [])

    def test_changed_prerequisite_invalidates_already_executed_transitive_consumers(self):
        projects = {'base': {'dependencies': []}, 'middle': {'dependencies': ['base']},
                    'app': {'dependencies': ['middle']}, 'independent': {'dependencies': []}}
        results = {name: self.receipt() for name in projects}
        results['base']['status'] = 'stale-source'
        verification.propagate_invalid_dependencies(projects, results)
        self.assertEqual(results['middle']['status'], 'dependency-failed')
        self.assertEqual(results['middle']['blocked_by'], ['base'])
        self.assertEqual(results['app']['status'], 'dependency-failed')
        self.assertTrue(verification.runtime_passed('independent', results['independent']))
        self.assertFalse(verification.all_passed(projects, results))

    def test_every_additional_configuration_must_execute_and_pass(self):
        receipt = self.receipt()
        second = copy.deepcopy(receipt['steps'][1])
        second['phase'] = 'qt-test'
        receipt['steps'].append(second)
        self.assertTrue(verification.runtime_passed('LVRS', receipt))
        for tests in ({'total': 0, 'passed': 0, 'failed': [], 'skipped': []},
                      {'total': 1, 'passed': 0, 'failed': ['qt'], 'skipped': []},
                      {'total': 1, 'passed': 0, 'failed': [], 'skipped': ['qt']}):
            with self.subTest(tests=tests):
                second['tests'] = tests
                self.assertFalse(verification.runtime_passed('LVRS', receipt))

    @unittest.skipUnless(os.name == 'nt', 'Windows SDK environment helper')
    def test_qt_consumers_choose_the_separate_lvrs_prefix(self):
        spec = importlib.util.spec_from_file_location('sdk_builder_qt_test', path.parent/'BuildProject.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with resource_workspace.temporary('windows-qt-') as temporary:
            root = Path(temporary)
            prefix = root/'.local/SDK/LVRS'
            (prefix/'bin').mkdir(parents=True)
            (prefix/'qt/bin').mkdir(parents=True)
            with patch.object(builder, 'ROOT', root), patch.dict(os.environ, {'USERPROFILE': str(root)}):
                qt = builder.environment('SDK/iiAccountManager')
                native = builder.environment('SDK/LVRS')
                configurations = builder.additional_configurations('SDK/LVRS')
            self.assertIn((prefix/'qt').as_posix(), qt['CMAKE_PREFIX_PATH'].split(';'))
            self.assertNotIn(prefix.as_posix(), qt['CMAKE_PREFIX_PATH'].split(';'))
            self.assertIn(str(prefix/'qt/bin'), qt['PATH'].split(os.pathsep))
            self.assertNotIn((prefix/'qt').as_posix(), native['CMAKE_PREFIX_PATH'].split(';'))
            self.assertEqual(configurations[0][3], prefix/'qt')

    def test_only_known_unavailable_accelerators_may_skip(self):
        receipt = self.receipt()
        tests = receipt['steps'][1]['tests']
        tests.update(total=3, skipped=['iiDecision.Acceleration.metal'])
        self.assertTrue(verification.runtime_passed('iiDecision', receipt))
        self.assertFalse(verification.runtime_passed('other', receipt))
        broken = copy.deepcopy(receipt)
        broken['steps'][1]['tests']['skipped'] = ['iiDecision.Laya.Typed']
        self.assertFalse(verification.runtime_passed('iiDecision', broken))


if __name__ == '__main__':
    unittest.main()
