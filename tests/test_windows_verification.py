"""The release gate must fail closed without running builds or changing Git."""
import copy
import os
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

path = Path(__file__).resolve().parents[1]/'Windows/Verify.py'
spec = importlib.util.spec_from_file_location('windows_verification', path)
verification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verification)


class WindowsVerificationTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt' and shutil.which('cl.exe'), 'Requires the Windows MSVC development shell')
    def test_clean_native_build_and_changed_header_relink_the_consumer(self):
        build_root = path.parents[1]/'build'
        build_root.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='windows-order-', dir=build_root) as temporary:
            source = Path(temporary)
            build = source/'build'
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
