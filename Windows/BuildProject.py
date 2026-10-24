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

QT_LVRS_CONSUMERS = {'SDK/iiAcountManager', 'SDK/iiLicenseManager',
                     'SDK/iiSocietyHelper', 'SDK/iiPhotoLibrary', 'Product/Dreamscapes'}


def environment(key, build=None, qt_lvrs=False):
    env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1', VSLANG='1033', CMAKE_GENERATOR='Ninja', QSG_RENDER_LOOP='basic', CTEST_PARALLEL_LEVEL='1')
    env['CL'] = '/Zc:__cplusplus /bigobj /DNOMINMAX' if key == 'SDK/iiVoiceOver' else '/utf-8 /Zc:__cplusplus /bigobj /DNOMINMAX'
    build = build or ROOT / key / 'build'
    sdk_root = Path(os.environ['USERPROFILE']) / '.local/SDK'
    installed = {path.name: path for path in sdk_root.iterdir() if path.is_dir() and path.name != 'node_modules'}
    projects = json.loads(Path(__file__).with_name('projects.json').read_text(encoding='utf-8'))
    pending = set(installed) & set(projects)
    prefixes = []
    while pending:
        ready = sorted((name for name in pending if not (set(projects[name]['dependencies']) & pending)),
                       key=lambda name: (name != 'iiSDKCore', name))
        if not ready:
            raise RuntimeError('Cyclic runtime SDK dependencies')
        prefixes.extend(installed[name] for name in ready)
        pending.difference_update(ready)
    prefixes.extend(installed[name] for name in sorted(set(installed) - set(projects)))
    required, visiting = set(), list(projects.get(Path(key).name, {'dependencies': list(projects)})['dependencies'])
    while visiting:
        name = visiting.pop()
        if name in required:
            continue
        required.add(name)
        visiting.extend(projects.get(name, {}).get('dependencies', []))
    env['RUSTUP_HOME'] = str(ROOT/'Tools/Rust/rustup')
    env['CARGO_HOME'] = str(ROOT/'Tools/Rust/cargo')
    env['CARGO_BUILD_JOBS'] = '4'
    env['ONNXRuntime_ROOT'] = str(ROOT/'Tools/ONNXRuntime/onnxruntime-win-x64-1.31.0')
    use_qt = qt_lvrs or key in QT_LVRS_CONSUMERS
    def selected(path):
        return path/'qt' if use_qt and path.name == 'LVRS' else path
    env['CMAKE_PREFIX_PATH'] = ';'.join(['C:/Qt/6.8.3/msvc2022_64', (ROOT / 'Tools/vcpkg/installed/x64-windows').as_posix()] + [selected(path).as_posix() for path in prefixes])
    if qt_lvrs:
        env['LVRS_STATE_CAPTURE_DIR'] = str(build/'state-captures')
    env['PATH'] = os.pathsep.join([str(selected(path)/'bin') for path in prefixes if path.name in required]
        + [str(build)] + sorted({str(p.parent) for p in build.rglob('*.dll')})
        + [str(selected(path)/'bin') for path in prefixes if path.name not in required and path.name != Path(key).name]
        + [str(ROOT/'Tools/Rust/cargo/bin'), str(Path(env['ONNXRuntime_ROOT'])/'lib'), env['PATH']])
    if key == 'SDK/iiWebServer':
        env['PYTHONPATH'] = str(ROOT/'Tools/python-serverhost') + os.pathsep + env.get('PYTHONPATH', '')
        env['IIWEBSERVER_TEST_FTP_PYTHON'] = str(ROOT/'Tools/PythonServerHost/Scripts/python.exe')
        env['IIWEBSERVER_TEST_RCLONE'] = str(ROOT/'Tools/Rclone/rclone-v1.75.2-windows-amd64/rclone.exe')
        env['CURL_SSL_BACKEND'] = 'openssl'
    # npm prepends .bin paths for every nested script; repeated SDK/MSVC entries
    # can push the inherited PATH past cmd.exe's limit and hide node.exe.
    paths, seen = [], set()
    for entry in env['PATH'].split(os.pathsep):
        normalized = os.path.normcase(os.path.normpath(entry))
        if normalized not in seen:
            seen.add(normalized)
            paths.append(entry)
    env['PATH'] = os.pathsep.join(paths)
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

def cache_matches_release(cache, key, compiler, prefix=None):
    expected = {'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_CXX_COMPILER': Path(compiler).as_posix(),
                'CMAKE_INSTALL_PREFIX': (prefix or Path(os.environ['USERPROFILE']) / '.local' / key).as_posix(),
                'CMAKE_TOOLCHAIN_FILE': (ROOT/'Tools/vcpkg/scripts/buildsystems/vcpkg.cmake').as_posix(),
                'CMAKE_PROJECT_INCLUDE': (ROOT/'SDK/Windows/WindowsMsvcPrefix.cmake').as_posix()}
    for name, value in expected.items():
        actual = re.search(r'^'+re.escape(name)+r':[^=]+=(.*)$', cache, re.M)
        if not actual or actual[1].replace('\\', '/').casefold() != value.casefold():
            return False
    return True


def configure(key, extras, *, source=None, build=None, prefix=None, phase='configure', env=None):
    source = source or ROOT / key
    build = build or source / 'build'
    prefix = prefix or Path(os.environ['USERPROFILE']) / '.local' / key
    env = env or environment(key, build=build)
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8', errors='replace') if (build / 'CMakeCache.txt').exists() else ''
    compiler = Path(shutil.which('cl.exe') or '').resolve().as_posix()
    if not compiler.lower().endswith('/cl.exe'):
        raise RuntimeError('Enter the x64 MSVC development environment first')
    home = re.search(r'^CMAKE_HOME_DIRECTORY:[^=]+=(.*)$', cache, re.M)
    relocated = home and Path(home[1]).resolve() != source.resolve()
    fresh = ['--fresh'] if cache and (relocated or 'CMAKE_GENERATOR:INTERNAL=Ninja' not in cache or not re.search(r'CMAKE_CXX_COMPILER:[^=]+=.*[/\\]cl\.exe', cache)) else []
    command = ['cmake', *fresh, '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=cl.exe', '-DCMAKE_CXX_COMPILER=cl.exe', '-DBUILD_TESTING=ON', '-DCMAKE_INSTALL_PREFIX=' + prefix.as_posix(), '-DCMAKE_PREFIX_PATH=' + env['CMAKE_PREFIX_PATH'], '-DCMAKE_TOOLCHAIN_FILE=' + (ROOT / 'Tools/vcpkg/scripts/buildsystems/vcpkg.cmake').as_posix(), '-DVCPKG_TARGET_TRIPLET=x64-windows', '-DCMAKE_PROJECT_INCLUDE=' + (ROOT / 'SDK/Windows/WindowsMsvcPrefix.cmake').as_posix(), '-DPython3_EXECUTABLE=' + PYTHON.as_posix(), '-DPython_EXECUTABLE=' + PYTHON.as_posix(), '-DQt6_DIR=C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6']
    command = [('-DCMAKE_C_COMPILER='+compiler) if argument == '-DCMAKE_C_COMPILER=cl.exe' else ('-DCMAKE_CXX_COMPILER='+compiler) if argument == '-DCMAKE_CXX_COMPILER=cl.exe' else argument for argument in command]
    options = {
        'LVRS': ['LVRS_BACKEND=native', 'LVRS_ENABLE_IPO=OFF', 'LVRS_BUILD_TESTS=ON', 'LVRS_BUILD_EXAMPLES=ON'],
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
    if key in QT_LVRS_CONSUMERS:
        command += ['-DLVRS_DIR='+(Path(os.environ['USERPROFILE'])/'.local/SDK/LVRS/qt/lib/cmake/LVRS').as_posix()]
    command += extras
    result = run(key, phase, command, env=env)
    if result['exit_code'] == 0:
        actual_cache = (build/'CMakeCache.txt').read_text(encoding='utf-8', errors='replace')
        if not cache_matches_release(actual_cache, key, compiler, prefix):
            retry = [argument for argument in command if argument != '--fresh']
            retry.insert(1, '--fresh')
            result = run(key, phase+'-fresh', retry, env=env)
            if result['exit_code']:
                return result
            actual_cache = (build/'CMakeCache.txt').read_text(encoding='utf-8', errors='replace')
            if not cache_matches_release(actual_cache, key, compiler, prefix):
                raise RuntimeError('CMake did not retain the requested Release compiler/prefix/toolchain configuration')
        helper = run(key, phase+'-ninja-prefix', [PYTHON, ROOT / 'SDK/Windows/Configure-WindowsNinja.py', build], env=env)
        if helper['exit_code']:
            return helper
    return result

def test(key, regex=None, *, build=None, phase='test', env=None):
    stamp = datetime.datetime.now().strftime('%H%M%S%f')
    junit = RUN / (key.replace('/', '__') + '-' + phase + '-' + stamp + '-junit.xml')
    command = ['ctest', '--test-dir', str(build or ROOT / key / 'build'), '-C', 'Release', '--output-on-failure', '--parallel', '1', '--timeout', '300', '--no-tests=error', '--output-junit', str(junit)]
    if regex:
        command += ['-R', regex]
    result = run(key, phase, command, env=env)
    result['junit'] = str(junit)
    result['tests'] = {'total': 0, 'passed': 0, 'failed': [], 'skipped': []}
    if junit.exists():
        cases = list(ET.parse(junit).getroot().iter('testcase'))
        result['tests'] = {'total': len(cases), 'passed': sum(c.find('failure') is None and c.find('error') is None and c.find('skipped') is None for c in cases), 'failed': [c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None], 'skipped': [c.attrib['name'] for c in cases if c.find('skipped') is not None]}
    return result

def additional_configurations(key):
    source = ROOT/key
    prefix = Path(os.environ['USERPROFILE'])/'.local'/key
    if key == 'SDK/iiFileProvider':
        return [('compatibility', source/'Compatibility', source/'build/compatibility/build', prefix, [], True)]
    if key == 'SDK/LVRS':
        return [('qt', source, source/'build/qt/build', prefix/'qt', ['-DLVRS_BACKEND=qt'], True)]
    if key == 'SDK/iiSharedCanvas':
        return [('native', source, source/'build/native/build', prefix/'native', ['-DIISHAREDCANVAS_BUILD_QT_API=OFF'], True),
                ('native-consumer', source/'tests/native-consumer', source/'build/native/consumer/build', prefix/'native', [], False)]
    return []


def check_additional_configuration(key, configuration):
    phase, source, build, prefix, extras, install = configuration
    if build.name != 'build' or not build.resolve().is_relative_to(ROOT/key):
        raise RuntimeError('Additional SDK outputs must remain in their own build/ directories')
    env = environment(key, build=build, qt_lvrs=phase == 'qt')
    if phase == 'native-consumer':
        env['CMAKE_PREFIX_PATH'] = prefix.as_posix()+';'+env['CMAKE_PREFIX_PATH']
    steps = []
    actions = ['configure', 'build', 'test'] + (['install'] if install else [])
    for action in actions:
        if action == 'configure':
            step = configure(key, extras, source=source, build=build, prefix=prefix, phase=phase+'-configure', env=env)
        elif action == 'test':
            step = test(key, build=build, phase=phase+'-test', env=env)
        else:
            command = ['cmake', '--'+action, build, '--config', 'Release']
            command += ['--parallel', '4'] if action == 'build' else ['--prefix', prefix]
            step = run(key, phase+'-'+action, command, env=env)
        step['configuration'] = phase
        steps.append(step)
        if step['exit_code']:
            break
    return steps


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
            for configuration in additional_configurations(key):
                steps = check_additional_configuration(key, configuration)
                result['steps'].extend(steps)
                if steps[-1]['exit_code']:
                    result['status'] = steps[-1]['phase']+'-failed'
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
