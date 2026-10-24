"""Build, execute and install the active SDK dependency graph; never modify Git."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
import urllib.request

HERE = Path(__file__).resolve().parent
SDK = HERE.parent
WORKSPACE = SDK.parent
OPTIONAL_SKIPS = {'iiDecision': {
    'iiDecision.Acceleration.metal', 'iiDecision.Acceleration.mlx',
    'iiDecision.Acceleration.opencl', 'iiDecision.Acceleration.onnx'}}


def dependency_order(projects):
    pending, ordered = set(projects), []
    for name, project in projects.items():
        if name.casefold() in {'deprecated', 'utility'}:
            raise ValueError('Excluded directory in SDK scope: '+name)
        if set(project['dependencies']) - set(projects):
            raise ValueError('Unknown dependency for '+name)
    while pending:
        ready = sorted(name for name in pending if not (set(projects[name]['dependencies']) & pending))
        if not ready:
            raise ValueError('Cyclic SDK dependencies: '+', '.join(sorted(pending)))
        # Bootstrap the native foundation before the optional inference toolchain.
        ready.sort(key=lambda name: (name != 'iiSDKCore', name == 'iiDecision', name))
        ordered.extend(ready)
        pending.difference_update(ready)
    return ordered


def runtime_passed(name, result):
    if result.get('status') != 'installed' or not result.get('steps'):
        return False
    if any(step.get('exit_code') != 0 for step in result['steps']):
        return False
    suites = [step['tests'] for step in result['steps'] if 'tests' in step]
    if not suites:
        return False
    for tests in suites:
        if tests.get('passed', 0) < 1 or tests.get('failed'):
            return False
        if tests.get('total') != tests['passed'] + len(tests.get('skipped', [])):
            return False
        if set(tests.get('skipped', [])) - OPTIONAL_SKIPS.get(name, set()):
            return False
    return True


def all_passed(projects, results):
    return bool(projects) and set(projects) == set(results) and all(runtime_passed(name, results[name]) for name in projects)


def failed_dependencies(project, results):
    return [name for name in project['dependencies']
            if name in results and not runtime_passed(name, results[name])]


def propagate_invalid_dependencies(projects, results):
    for name in dependency_order(projects):
        if name not in results:
            continue
        blocked = failed_dependencies(projects[name], results)
        if blocked:
            results[name]['status'] = 'dependency-failed'
            results[name]['blocked_by'] = blocked


def fingerprint(name):
    root = SDK/name
    git = lambda *args: subprocess.check_output(['git', '-C', str(root), *args])
    digest = hashlib.sha256(git('rev-parse', 'HEAD'))
    digest.update(git('diff', '--binary', 'HEAD', '--', '.', ':(exclude)*.md'))
    for value in git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0'):
        if value and not value.lower().endswith(b'.md'):
            path = root/os.fsdecode(value)
            if path.is_file():
                digest.update(value)
                digest.update(path.read_bytes())
    return digest.hexdigest()


def verify_npm(builder, key):
    npm = ['C:/Program Files/nodejs/node.exe', 'C:/Program Files/nodejs/node_modules/npm/bin/npm-cli.js']
    result = {'steps': []}
    for phase, command in [('npm-ci', npm+['ci', '--no-audit', '--no-fund'])] + [(phase, npm+['run', phase]) for phase in ['test', 'check', 'build']]:
        step = builder.run(key, phase, command)
        if phase == 'test' and step['exit_code'] == 0:
            content = Path(step['log']).read_text(encoding='utf-8')
            match = re.search(r'(?:#|ℹ)\s+tests\s+(\d+)', content)
            failures = re.search(r'(?:#|ℹ)\s+fail\s+(\d+)', content)
            step['tests'] = {'total': int(match[1]) if match else 0, 'passed': int(match[1]) if match else 0,
                             'failed': ['npm'] if not failures or int(failures[1]) else [], 'skipped': []}
        result['steps'].append(step)
        if step['exit_code']:
            result['status'] = phase+'-failed'
            return result
    env = builder.environment(key)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    log = builder.RUN/'SDK__LVRS-Svelte-preview.log'
    with log.open('w', encoding='utf-8') as output:
        child = subprocess.Popen([npm[0], str(SDK/'LVRS-Svelte/node_modules/vite/bin/vite.js'), 'preview', '--host', '127.0.0.1', '--port', str(port), '--strictPort'], cwd=SDK/'LVRS-Svelte', env=env, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+30
            while time.monotonic() < deadline and child.poll() is None:
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/', timeout=2) as response:
                        content = response.read()
                        assert response.status == 200 and b'<html' in content.lower()
                    result['steps'].append({'phase': 'preview', 'exit_code': 0, 'http_status': 200, 'log': str(log)})
                    break
                except OSError:
                    time.sleep(.1)
            else:
                raise RuntimeError('The built Svelte catalog did not start')
        finally:
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], stdout=output, stderr=subprocess.STDOUT)
            child.wait(timeout=15)
    step = builder.run(key, 'pack', npm+['pack', '--ignore-scripts', '--pack-destination', 'build'])
    result['steps'].append(step)
    if step['exit_code']:
        result['status'] = 'pack-failed'
        return result
    package = max((SDK/'LVRS-Svelte/build').glob('lvrs-svelte-*.tgz'), key=lambda path: path.stat().st_mtime)
    step = builder.run(key, 'install', npm+['install', '--global', '--ignore-scripts', '--no-audit', '--no-fund', '--prefix', str(Path(os.environ['USERPROFILE'])/'.local/SDK'), str(package)])
    result['steps'].append(step)
    result['status'] = 'installed' if step['exit_code'] == 0 else 'install-failed'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-dir', type=Path)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--only', nargs='+')
    args = parser.parse_args()
    projects = json.loads((HERE/'projects.json').read_text(encoding='utf-8'))
    discovered = {path.name for path in SDK.iterdir() if path.is_dir() and path.name.casefold() not in {'deprecated', 'utility'} and ((path/'CMakeLists.txt').is_file() or (path/'package.json').is_file())}
    if discovered != set(projects):
        raise RuntimeError(f'Update projects.json to match buildable SDKs: {discovered ^ set(projects)}')
    order = dependency_order(projects)
    report_directory = (args.report_dir or WORKSPACE/'build/maintenance'/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-sdk-verification')).resolve()
    if not report_directory.is_relative_to(WORKSPACE/'build'):
        raise RuntimeError('Reports must stay inside the workspace build/ directory')
    report_directory.mkdir(parents=True, exist_ok=True)
    os.environ['SDK_VERIFICATION_DIR'] = str(report_directory)
    spec = importlib.util.spec_from_file_location('sdk_builder', HERE/'BuildProject.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    report_file = report_directory/'verification.json'
    prior = json.loads(report_file.read_text(encoding='utf-8')) if args.resume and report_file.exists() else {}
    toolchain = hashlib.sha256()
    for path in [HERE/'BuildProject.py', HERE/'Verify.py', HERE/'projects.json', HERE/'WindowsMsvcPrefix.cmake', HERE/'Configure-WindowsNinja.py', HERE/'Enter-DevEnvironment.ps1', HERE/'windows-utf8.manifest', WORKSPACE/'Tools/vcpkg/installed/vcpkg/status']:
        toolchain.update(path.read_bytes())
    report = {'started_at': datetime.datetime.now().astimezone().isoformat(), 'scope': list(projects), 'order': order,
              'toolchain': toolchain.hexdigest(), 'projects': {}, 'all_projects_executed_successfully': False, 'commit_and_push_allowed': False}
    def save():
        report_file.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    save()
    for name in order:
        if args.only and name not in args.only:
            continue
        stamp = fingerprint(name)
        dependency_stamps = {dependency: fingerprint(dependency) for dependency in projects[name]['dependencies']}
        for dependency, current_stamp in dependency_stamps.items():
            parent = report['projects'].get(dependency)
            if parent and parent.get('source') != current_stamp:
                parent['status'] = 'stale-source'
        blocked = failed_dependencies(projects[name], report['projects'])
        if blocked:
            report['projects'][name] = {'status': 'dependency-failed', 'blocked_by': blocked,
                                        'steps': [], 'source': stamp, 'dependencies': dependency_stamps}
            print('BLOCKED', name, ', '.join(blocked), flush=True)
            save()
            continue
        previous = prior.get('projects', {}).get(name, {})
        if prior.get('toolchain') == report['toolchain'] and previous.get('source') == stamp and previous.get('dependencies') == dependency_stamps and runtime_passed(name, previous):
            report['projects'][name] = previous
            print('VERIFIED', name, '(unchanged receipt)', flush=True)
            save()
            continue
        key = 'SDK/'+name
        print('VERIFY', name, flush=True)
        if projects[name]['kind'] == 'npm':
            result = verify_npm(builder, key)
        else:
            completed = subprocess.run([sys.executable, str(HERE/'BuildProject.py'), 'check', key], env=os.environ.copy())
            result_file = report_directory/(key.replace('/', '__')+'-check-result.json')
            result = json.loads(result_file.read_text(encoding='utf-8')) if result_file.exists() else {'status': 'failed', 'steps': []}
            if completed.returncode and result.get('status') == 'installed':
                result['status'] = 'failed'
        result.update(source=stamp, dependencies=dependency_stamps)
        if fingerprint(name) != stamp:
            result['status'] = 'source-changed-during-verification'
        report['projects'][name] = result
        save()
        if not runtime_passed(name, result):
            print('FAILED', name, result['status'], flush=True)
    report['finished_at'] = datetime.datetime.now().astimezone().isoformat()
    for name, result in report['projects'].items():
        if result['source'] != fingerprint(name) or any(stamp != fingerprint(dependency) for dependency, stamp in result['dependencies'].items()):
            result['status'] = 'stale-source'
    propagate_invalid_dependencies(projects, report['projects'])
    report['all_projects_executed_successfully'] = all_passed(projects, report['projects'])
    report['commit_and_push_allowed'] = report['all_projects_executed_successfully']
    save()
    print('ALL SDKs PASSED' if report['commit_and_push_allowed'] else 'PARTIAL VERIFICATION; commit/push gate remains closed', flush=True)
    selected = {name: project for name, project in projects.items() if not args.only or name in args.only}
    return 0 if all_passed(selected, report['projects']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
