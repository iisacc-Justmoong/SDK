"""Windows SDK recovery operations. This tool never commits or pushes."""
import argparse
import ctypes
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN = Path(os.environ.get('SDK_VERIFICATION_DIR', ROOT / 'build/maintenance' / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-sdk-verification')))
RUN.mkdir(parents=True, exist_ok=True)
ctypes.windll.kernel32.SetErrorMode(0x8003)
PYTHON = ROOT / 'Tools/Python314/tools/python.exe'

def environment(key):
    env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1', VSLANG='1033', CMAKE_GENERATOR='Ninja', QSG_RENDER_LOOP='basic', CTEST_PARALLEL_LEVEL='1')
    env['CL'] = '/Zc:__cplusplus /bigobj /DNOMINMAX' if key == 'SDK/iiVoiceOver' else '/utf-8 /Zc:__cplusplus /bigobj /DNOMINMAX'
    build = ROOT / key / 'build'
    sdk_root = Path(os.environ['USERPROFILE']) / '.local/SDK'
    prefixes = sorted(path for path in sdk_root.iterdir() if path.is_dir() and path.name != 'node_modules')
    env['RUSTUP_HOME'] = str(ROOT/'Tools/Rust/rustup')
    env['CARGO_HOME'] = str(ROOT/'Tools/Rust/cargo')
    env['CARGO_BUILD_JOBS'] = '4'
    env['ONNXRuntime_ROOT'] = str(ROOT/'Tools/ONNXRuntime/onnxruntime-win-x64-1.31.0')
    env['CMAKE_PREFIX_PATH'] = ';'.join(['C:/Qt/6.8.3/msvc2022_64', (ROOT / 'Tools/vcpkg/installed/x64-windows').as_posix()] + [path.as_posix() for path in prefixes])
    env['PATH'] = os.pathsep.join(sorted({str(p.parent) for p in build.rglob('*.dll')}) + [str(ROOT/'Tools/Rust/cargo/bin'), str(Path(env['ONNXRuntime_ROOT'])/'lib')] + [str(path / 'bin') for path in prefixes] + [env['PATH']])
    if key == 'SDK/iiWebServer':
        env['PYTHONPATH'] = str(ROOT/'Tools/python-serverhost') + os.pathsep + env.get('PYTHONPATH', '')
        env['IIWEBSERVER_TEST_FTP_PYTHON'] = str(ROOT/'Tools/PythonServerHost/Scripts/python.exe')
        env['IIWEBSERVER_TEST_RCLONE'] = str(ROOT/'Tools/Rclone/rclone-v1.75.2-windows-amd64/rclone.exe')
        env['CURL_SSL_BACKEND'] = 'openssl'
    return env

def run(key, phase, command, env=None, timeout=3600, cwd=None):
    tag = key.replace('/', '__') + '-' + phase
    number = 1
    while (RUN / f'{tag}-{number:03}.log').exists():
        number += 1
    log = RUN / f'{tag}-{number:03}.log'
    start = time.monotonic()
    with log.open('w', encoding='utf-8') as out:
        out.write(subprocess.list2cmdline(list(map(str, command))) + '\n')
        out.flush()
        child = subprocess.Popen(list(map(str, command)), cwd=cwd or ROOT / key, env=env or environment(key), stdout=out, stderr=subprocess.STDOUT)
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], stdout=out, stderr=subprocess.STDOUT)
            child.wait(timeout=30)
            code = 124
    result = {'phase': phase, 'exit_code': code, 'seconds': round(time.monotonic() - start, 2), 'log': str(log), 'command': list(map(str, command))}
    print(key, phase, code, result['seconds'], log.name, flush=True)
    return result

def cache_matches_release(cache, key, compiler):
    expected = {'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_CXX_COMPILER': Path(compiler).as_posix(),
                'CMAKE_INSTALL_PREFIX': (Path(os.environ['USERPROFILE']) / '.local' / key).as_posix(),
                'CMAKE_TOOLCHAIN_FILE': (ROOT/'Tools/vcpkg/scripts/buildsystems/vcpkg.cmake').as_posix(),
                'CMAKE_PROJECT_INCLUDE': (ROOT/'SDK/Windows/WindowsMsvcPrefix.cmake').as_posix()}
    for name, value in expected.items():
        actual = re.search(r'^'+re.escape(name)+r':[^=]+=(.*)$', cache, re.M)
        if not actual or actual[1].replace('\\', '/').casefold() != value.casefold():
            return False
    return True


def configure(key, extras):
    source = ROOT / key
    build = source / 'build'
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8', errors='replace') if (build / 'CMakeCache.txt').exists() else ''
    compiler = Path(shutil.which('cl.exe') or '').resolve().as_posix()
    if not compiler.lower().endswith('/cl.exe'):
        raise RuntimeError('Enter the x64 MSVC development environment first')
    fresh = ['--fresh'] if cache and ('CMAKE_GENERATOR:INTERNAL=Ninja' not in cache or not re.search(r'CMAKE_CXX_COMPILER:[^=]+=.*[/\\]cl\.exe', cache)) else []
    command = ['cmake', *fresh, '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=cl.exe', '-DCMAKE_CXX_COMPILER=cl.exe', '-DBUILD_TESTING=ON', '-DCMAKE_INSTALL_PREFIX=' + (Path(os.environ['USERPROFILE']) / '.local' / key).as_posix(), '-DCMAKE_PREFIX_PATH=' + environment(key)['CMAKE_PREFIX_PATH'], '-DCMAKE_TOOLCHAIN_FILE=' + (ROOT / 'Tools/vcpkg/scripts/buildsystems/vcpkg.cmake').as_posix(), '-DVCPKG_TARGET_TRIPLET=x64-windows', '-DCMAKE_PROJECT_INCLUDE=' + (ROOT / 'SDK/Windows/WindowsMsvcPrefix.cmake').as_posix(), '-DPython3_EXECUTABLE=' + PYTHON.as_posix(), '-DPython_EXECUTABLE=' + PYTHON.as_posix(), '-DQt6_DIR=C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6']
    command = [('-DCMAKE_C_COMPILER='+compiler) if argument == '-DCMAKE_C_COMPILER=cl.exe' else ('-DCMAKE_CXX_COMPILER='+compiler) if argument == '-DCMAKE_CXX_COMPILER=cl.exe' else argument for argument in command]
    options = {
        'LVRS': ['LVRS_ENABLE_IPO=OFF', 'LVRS_BUILD_TESTS=ON', 'LVRS_BUILD_EXAMPLES=ON'],
        'iiDecision': ['IIDECISION_ONNX=ON', 'IIDECISION_LAYA=ON', 'ONNXRuntime_ROOT=' + (ROOT/'Tools/ONNXRuntime/onnxruntime-win-x64-1.31.0').as_posix()],
        'iiFileProvider': ['IIFILEPROVIDER_BUILD_CORE_IO=ON'],
        'iiXml': ['IIXML_BUILD_SHARED=OFF'],
        'iiHtmlBlock': ['IIHTMLBLOCK_BUILD_SHARED=OFF'],
        'iiSharedCanvas': ['IISHAREDCANVAS_BUILD_SHARED=OFF', 'IISHAREDCANVAS_BUILD_QUICK=ON'],
        'iiGeneralDocument': ['IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP=ON'],
        'iiSocietyContainer': ['Dokan_ROOT=' + os.environ['DOKAN_ROOT']],
        'iiCameraIntergration': ['IICAMERA_FFMPEG_EXECUTABLE=' + (ROOT / 'Tools/FFmpeg/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe').as_posix()],
    }
    command += ['-D' + option for option in options.get(source.name, [])]
    command += extras
    result = run(key, 'configure', command)
    if result['exit_code'] == 0:
        actual_cache = (build/'CMakeCache.txt').read_text(encoding='utf-8', errors='replace')
        if not cache_matches_release(actual_cache, key, compiler):
            retry = [argument for argument in command if argument != '--fresh']
            retry.insert(1, '--fresh')
            result = run(key, 'configure-fresh', retry)
            if result['exit_code']:
                return result
            actual_cache = (build/'CMakeCache.txt').read_text(encoding='utf-8', errors='replace')
            if not cache_matches_release(actual_cache, key, compiler):
                raise RuntimeError('CMake did not retain the requested Release compiler/prefix/toolchain configuration')
        helper = run(key, 'ninja-prefix', [PYTHON, ROOT / 'SDK/Windows/Configure-WindowsNinja.py', build])
        if helper['exit_code']:
            return helper
    return result

def test(key, regex=None):
    stamp = datetime.datetime.now().strftime('%H%M%S%f')
    junit = RUN / (key.replace('/', '__') + '-' + stamp + '-junit.xml')
    command = ['ctest', '--test-dir', str(ROOT / key / 'build'), '-C', 'Release', '--output-on-failure', '--parallel', '1', '--no-tests=error', '--output-junit', str(junit)]
    if regex:
        command += ['-R', regex]
    result = run(key, 'test', command)
    if junit.exists():
        cases = list(ET.parse(junit).getroot().iter('testcase'))
        result['tests'] = {'total': len(cases), 'passed': sum(c.find('failure') is None and c.find('error') is None and c.find('skipped') is None for c in cases), 'failed': [c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None], 'skipped': [c.attrib['name'] for c in cases if c.find('skipped') is not None]}
    return result

def qt_diagnostics(key, regex):
    listing = subprocess.check_output(['ctest', '--test-dir', str(ROOT / key / 'build'), '-C', 'Release', '--show-only=json-v1'], env=environment(key), text=True, encoding='utf-8')
    outcomes = []
    for test_case in json.loads(listing)['tests']:
        if not re.search(regex or '.', test_case['name']):
            continue
        command = test_case.get('command')
        if not command:
            continue
        env = environment(key)
        for prop in test_case.get('properties', []):
            if prop['name'] == 'ENVIRONMENT':
                for definition in prop['value']:
                    name, value = definition.split('=', 1)
                    env[name] = value
        report = RUN / (test_case['name'] + '-' + datetime.datetime.now().strftime('%H%M%S%f') + '-qt.txt')
        result = run(key, test_case['name'] + '-diagnostic', command + ['-o', str(report) + ',txt'], env=env, timeout=240, cwd=ROOT / key / 'build')
        result['qt_log'] = str(report)
        outcomes.append(result)
    return outcomes

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['configure', 'build', 'test', 'install', 'check', 'qt'])
    parser.add_argument('key')
    parser.add_argument('--regex')
    args, extras = parser.parse_known_args()
    key = args.key
    assert key.startswith('SDK/') and (ROOT / key).resolve().is_relative_to(ROOT / 'SDK')
    if args.action == 'qt':
        result = qt_diagnostics(key, args.regex)
    elif args.action == 'configure':
        result = configure(key, extras)
    elif args.action == 'build':
        result = run(key, 'build', ['cmake', '--build', str(ROOT / key / 'build'), '--config', 'Release', '--parallel', '4'])
    elif args.action == 'test':
        result = test(key, args.regex)
    elif args.action == 'install':
        verification = RUN / (key.replace('/', '__') + '-test-result.json')
        if verification.exists() and json.loads(verification.read_text(encoding='utf-8')).get('exit_code') != 0:
            raise RuntimeError('The most recent runtime verification failed; installation is blocked.')
        result = run(key, 'install', ['cmake', '--install', str(ROOT / key / 'build'), '--config', 'Release', '--prefix', str(Path(os.environ['USERPROFILE']) / '.local' / key)])
    else:
        result = {'steps': []}
        for action in ['configure', 'build', 'test', 'install']:
            if action == 'configure':
                step = configure(key, extras)
            elif action == 'test':
                step = test(key)
            else:
                command = ['cmake', '--' + action, str(ROOT / key / 'build'), '--config', 'Release']
                command += ['--parallel', '4'] if action == 'build' else ['--prefix', str(Path(os.environ['USERPROFILE']) / '.local' / key)]
                step = run(key, action, command)
            result['steps'].append(step)
            if step['exit_code']:
                result['status'] = action + '-failed'
                break
        else:
            result['status'] = 'installed'
    record = RUN / (key.replace('/', '__') + '-' + args.action + '-result.json')
    record.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    if isinstance(result, list):
        print('Qt diagnostics:', len(result), 'failed:', sum(step['exit_code'] != 0 for step in result), flush=True)
    else:
        tests = result.get('tests', next((step.get('tests') for step in reversed(result.get('steps', [])) if step.get('tests')), None))
        print('Result:', result.get('status', result.get('exit_code')), tests or '', flush=True)
    if isinstance(result, dict) and (result.get('exit_code', 0) != 0 or result.get('status', '').endswith('-failed')):
        sys.exit(1)

if __name__ == '__main__':
    main()
