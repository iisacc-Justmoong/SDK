#!/usr/bin/env python3
"""Discover, order, test and reinstall the SDK projects next to this script."""
import argparse
import datetime
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys


def cmake_text(directory):
    files = [directory / "CMakeLists.txt"]
    if (directory / "cmake").is_dir():
        files += sorted((directory / "cmake").rglob("*.cmake*"))
    return "\n".join(re.sub(r"#[^\n]*", "", p.read_text(encoding="utf-8"))
                     for p in files if p.is_file())


def discover(root):
    projects, skipped = {}, []
    for directory in sorted(root.iterdir()):
        if not directory.is_dir() or directory.is_symlink() or directory.name.startswith("."):
            continue
        if not any((directory / marker).is_file() for marker in ("install.sh", "CMakeLists.txt")):
            if directory.name not in ("build", "resources", "tests"):
                skipped.append(directory.name)
            continue
        text = cmake_text(directory)
        match = re.search(r"\bproject\s*\(\s*([\w.+-]+)", text, re.I)
        projects[directory.name] = {"path": directory,
                                    "package": match[1] if match else directory.name,
                                    "text": text, "dependencies": set()}
    packages = {p["package"]: name for name, p in projects.items()}
    for name, project in projects.items():
        references = re.findall(r"\b(?:find_package|find_dependency)\s*\(\s*[\"]?([\w.+-]+)",
                                project["text"], re.I)
        references += re.findall(r"\b([\w.+-]+)::[\w.+-]+", project["text"])
        project["dependencies"] = {packages[ref] for ref in references
                                   if ref in packages and packages[ref] != name}
        # Variable-computed dependencies or non-CMake projects can declare only
        # their extra local edges; there is no central SDK name registry.
        extra = project["path"] / "install-dependencies.txt"
        if extra.is_file():
            for dependency in extra.read_text(encoding="utf-8").splitlines():
                dependency = dependency.split("#", 1)[0].strip()
                if dependency:
                    if dependency not in projects:
                        raise ValueError(f"{name}: unknown install dependency {dependency}")
                    project["dependencies"].add(dependency)
    return projects, skipped


def ordered(projects):
    remaining, result = set(projects), []
    while remaining:
        ready = sorted(name for name in remaining
                       if not projects[name]["dependencies"] & remaining)
        if not ready:
            raise ValueError("SDK dependency cycle: " + ", ".join(sorted(remaining)))
        result.extend(ready)
        remaining.difference_update(ready)
    return result


def parse_overrides(values, projects):
    result = {name: [] for name in projects}
    for value in values:
        name, separator, argument = value.partition("=")
        if not separator or name not in projects or not argument:
            raise ValueError(f"Expected SDK=ARGUMENT for a discovered SDK: {value}")
        result[name].append(argument)
    return result


def run(command, directory, environment, log):
    log.write("$ " + shlex.join(str(arg) for arg in command) + "\n")
    log.flush()
    subprocess.run(command, cwd=directory, env=environment, stdout=log,
                   stderr=subprocess.STDOUT, check=True)


def provider_operation(operation, path, content=""):
    """Use a short-lived provider process, including while its SDK is updated."""
    provider_python = Path(__file__).resolve().parent / "iiFileProvider" / "python"
    if not provider_python.is_dir():
        raise ValueError("iiFileProvider Python sources are required to create installer output")
    worker = '''import sys
import ii_file_provider as fp
operation, path = sys.argv[1:3]
if operation == "mkdir":
    fp.mkdir(path, parents=True, exist_ok=True)
elif operation == "create":
    with fp.open(path, "x", encoding="utf-8"):
        pass
elif operation == "write":
    fp.write_text(path, sys.stdin.read(), encoding="utf-8")
else:
    raise ValueError("unknown provider operation")
'''
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(provider_python) + (os.pathsep + environment["PYTHONPATH"] if environment.get("PYTHONPATH") else "")
    result = subprocess.run([sys.executable, "-B", "-c", worker, operation, str(path)],
                            input=content, text=True, capture_output=True, env=environment)
    if result.returncode:
        raise ValueError(f"iiFileProvider {operation} failed for {path}: {result.stderr.strip()}")


def save_report(logs, report):
    # The provider stages and publishes the complete replacement atomically.
    provider_operation("write", logs / "report.json", json.dumps(report, indent=2) + "\n")


def check_manifest(build, name, expected_prefix):
    manifest = build / "install_manifest.txt"
    if not manifest.is_file():
        raise ValueError(f"{name}: missing install manifest {manifest}")
    files = [Path(line) for line in manifest.read_text(encoding="utf-8").splitlines() if line]
    missing = [str(path) for path in files if not path.exists()]
    if not files or missing:
        raise ValueError(f"{name}: installed files missing: {missing}")
    if not any(path.is_relative_to(expected_prefix) for path in files):
        raise ValueError(f"{name}: manifest contains no files under requested prefix {expected_prefix}")
    return len(files)


def installer_bash(environment):
    """Select a host Bash; Windows' System32 shim starts WSL instead."""
    explicit = environment.get("BASH_BINARY")
    candidate = explicit or shutil.which("bash", path=environment.get("PATH"))
    if os.name != "nt":
        return candidate
    def is_wsl_shim(path):
        return path and Path(path).parent.name.lower() in {"system32", "sysnative", "syswow64"}
    if explicit:
        if is_wsl_shim(explicit):
            raise ValueError("BASH_BINARY must point to a native Windows Bash, not WSL")
        return explicit
    git = shutil.which("git", path=environment.get("PATH"))
    choices = ([Path(git).resolve().parent.parent / "bin/bash.exe"] if git else [])
    choices.append(Path(environment.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe")
    for path in choices:
        if path.is_file():
            return str(path)
    return None if is_wsl_shim(candidate) else candidate


def install_project(name, project, projects, args, environment, log, definitions, installer_args):
    directory, build = project["path"], project["path"] / "build"
    prefix = args.prefix_root / name
    environment = environment.copy()
    environment["INSTALL_PREFIX"] = str(prefix)
    config = [f"-DCMAKE_BUILD_TYPE={args.configuration}", f"-DBUILD_TESTING={'OFF' if args.no_tests else 'ON'}",
              f"-DCMAKE_INSTALL_PREFIX={prefix}",
              f"-DCMAKE_PREFIX_PATH={environment['CMAKE_PREFIX_PATH']}"]
    # Product builds retain tests by default; only declared example, demo and
    # benchmark switches are disabled. The explicit no-tests mode also disables
    # declared test switches. Later user definitions can override these defaults.
    declared = re.findall(r"\boption\s*\(\s*([A-Za-z_]\w*)", project["text"], re.I)
    for option in sorted(set(declared)):
        match = re.fullmatch(r"(?:[A-Z0-9]+_)*(?:BUILD|INSTALL)_(?:[A-Z0-9]+_)*(TESTS?|EXAMPLES?|BENCHMARKS?|DEMOS?)", option.upper())
        if match and (args.no_tests or not match.group(1).startswith("TEST")):
            config.append(f"-D{option}=OFF")
    # Package variants such as iiFileProviderNative_DIR can otherwise retain
    # a stale build-tree path even after the primary package is refreshed.
    # Also remove retired or renamed ii/LV package cache entries.
    config.extend(["-Uii*_DIR", "-ULVRS*_DIR", "-ULVUI*_DIR"])
    for peer in sorted(set(projects) | {entry["package"] for entry in projects.values()}):
        config.append(f"-U{peer}*_DIR")
    if args.qt_prefix:
        config.append(f"-DQt6_DIR={args.qt_prefix}/lib/cmake/Qt6")
    cache_file = build / "CMakeCache.txt"
    if cache_file.is_file():
        cache = dict(re.findall(r"^([\w.+-]+):[^=\n]*=([^\n]*)$", cache_file.read_text(encoding="utf-8"), re.M))
        old_tool = cache.get("CMAKE_MAKE_PROGRAM", "")
        if old_tool and not shutil.which(old_tool):
            generator = cache.get("CMAKE_GENERATOR", "")
            replacement = "ninja" if generator.startswith("Ninja") else (
                "make" if generator == "Unix Makefiles" else None)
            if replacement and shutil.which(replacement):
                config.append(f"-DCMAKE_MAKE_PROGRAM={shutil.which(replacement)}")
            else:
                raise ValueError(f"{name}: missing cached build tool {old_tool} ({generator})")
    for dependency in sorted(project["dependencies"]):
        package = projects[dependency]["package"]
        config.append(f"-D{package}_DIR={args.prefix_root}/{dependency}/lib/cmake/{package}")
    config.extend(definitions)
    native = (directory / "CMakeLists.txt").is_file()
    if args.no_tests and not native:
        raise ValueError(f"{name}: --no-tests cannot control a non-CMake installer")
    if native:
        # Refresh dependency cache entries even when an existing installer later
        # reconfigures: old build-tree package paths must not win over installation.
        run(["cmake", "-S", str(directory), "-B", str(build), *config], directory, environment, log)
    installer = directory / "install.sh"
    # The LVRS multi-platform CLI may remove build and other platform outputs.
    use_installer = installer.is_file() and name != "LVRS" and name not in args.cmake and not args.no_tests
    if use_installer:
        bash = installer_bash(environment)
        if not bash:
            raise ValueError(f"{name}: bash is required to run install.sh")
        run([bash, str(installer), *installer_args], directory, environment, log)
    else:
        if not native:
            raise ValueError(f"{name}: CMake fallback requires CMakeLists.txt")
        run(["cmake", "--build", str(build), "--config", args.configuration,
             "--parallel", str(args.jobs)], directory, environment, log)
        if not args.no_tests:
            run(["ctest", "--test-dir", str(build), "-C", args.configuration,
                 "--output-on-failure", "--no-tests=error"], directory, environment, log)
        run(["cmake", "--install", str(build), "--config", args.configuration,
             "--prefix", str(prefix)], directory, environment, log)
    if native:
        # Installers sometimes omit tests (or filter them). Always verify the
        # current complete host suite after the installation, against fresh SDKs.
        installer_text = installer.read_text(encoding="utf-8") if installer.is_file() else ""
        if (use_installer and not args.no_tests
                and ("ctest" not in installer_text or re.search(r"\bctest[^\n]*\s-E\s", installer_text))):
            run(["cmake", "--build", str(build), "--config", args.configuration,
                 "--parallel", str(args.jobs)], directory, environment, log)
            run(["ctest", "--test-dir", str(build), "-C", args.configuration,
                 "--output-on-failure", "--no-tests=error"], directory, environment, log)
        primary_count = check_manifest(build, name, prefix)
        host = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}.get(platform.system())
        host_prefix = prefix / "platforms" / (host or "unknown")
        if args.refresh_host_prefix and host and host_prefix.is_dir():
            # Some consumers deliberately resolve platforms/<host>. Refresh that
            # existing layout too without deleting any other installed ABI.
            run(["cmake", "--install", str(build), "--config", args.configuration,
                 "--prefix", str(host_prefix)], directory, environment, log)
            host_count = check_manifest(build, name, host_prefix)
        else:
            host_count = None
        return {"manifest": str(build / "install_manifest.txt"),
                "primary_installed_file_count": primary_count, "host_installed_file_count": host_count,
                "tests": "not_run" if args.no_tests else "installer_or_ctest"}
    return {"tests": "installer_managed"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--prefix-root", type=Path, default=Path.home() / ".local/SDK",
                        help="CMake fallback prefix; existing installers retain their documented prefix variables")
    parser.add_argument("--qt-prefix", type=Path, default=os.environ.get("QT_PREFIX_PATH"))
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--test-jobs", type=int, default=1,
                        help="CTest concurrency; default 1 avoids contention in GUI/network suites")
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--cmake", action="append", default=[], metavar="SDK",
                        help="Use standard CMake build/test/install instead of this SDK's installer")
    parser.add_argument("--define", action="append", default=[], metavar="SDK=-DKEY=VALUE")
    parser.add_argument("--installer-arg", action="append", default=[], metavar="SDK=ARGUMENT")
    parser.add_argument("--only", action="append", default=[], metavar="SDK",
                        help="Install selected projects plus their discovered local dependencies")
    parser.add_argument("--no-dependencies", action="store_true",
                        help="With --only, reuse installed prerequisites instead of reinstalling them")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-tests", action="store_true",
                        help="Build and install CMake projects without executing tests or custom installers")
    parser.add_argument("--refresh-host-prefix", action="store_true",
                        help="Also overwrite an existing platforms/<host> prefix after verifying its ABI and backend")
    parser.add_argument("--keep-logs", action="store_true",
                        help="Keep successful project logs in addition to failure logs")
    args = parser.parse_args(argv)
    if args.jobs < 1 or args.test_jobs < 1:
        parser.error("--jobs and --test-jobs must be positive")
    if args.no_dependencies and not args.only:
        parser.error("--no-dependencies requires --only")
    args.root, args.prefix_root = args.root.expanduser().resolve(), args.prefix_root.expanduser().resolve()
    if args.qt_prefix:
        args.qt_prefix = Path(args.qt_prefix).expanduser().resolve()
    projects, skipped = discover(args.root)
    if not projects:
        parser.error("No installable SDK projects found")
    for name in args.cmake + args.only:
        if name not in projects:
            parser.error(f"Unknown SDK: {name}")
    definitions = parse_overrides(args.define, projects)
    installer_args = parse_overrides(args.installer_arg, projects)
    order = ordered(projects)
    if args.only:
        selected = set(args.only)
        pending = list(selected)
        while pending and not args.no_dependencies:
            for dependency in projects[pending.pop()]["dependencies"] - selected:
                selected.add(dependency)
                pending.append(dependency)
        order = [name for name in order if name in selected]
    report = {"started_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "root": str(args.root), "platform": platform.system(), "order": order,
              "skipped_directories": skipped, "projects": {}}
    for name in order:
        print(f"{name}: dependencies={','.join(sorted(projects[name]['dependencies'])) or '-'}", flush=True)
    if skipped:
        print("No installer/CMake project: " + ", ".join(skipped), flush=True)
    if args.dry_run:
        return 0
    for path in (args.root / "resources", args.root / "resources/.state", args.root / "resources/.state/install-all"):
        if path.is_symlink():
            raise ValueError("SDK install state cannot be a symlink")
    logs = args.root / "resources/.state/install-all" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    provider_operation("mkdir", logs)
    report["projects"] = {name: {"status": "pending"} for name in order}
    save_report(logs, report)
    environment = os.environ.copy()
    for variable in ("DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
                     "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_QML_IMPORT_PATH", "CPATH",
                     "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"):
        environment.pop(variable, None)
    prefixes = ([str(args.qt_prefix)] if args.qt_prefix else [])
    prefixes += [str(args.prefix_root / name) for name in ordered(projects)]
    prefixes += [value for value in environment.get("CMAKE_PREFIX_PATH", "").replace(os.pathsep, ";").split(";") if value]
    environment.update(CMAKE_PREFIX_PATH=";".join(dict.fromkeys(prefixes)),
                       CMAKE_BUILD_PARALLEL_LEVEL=str(args.jobs), CTEST_PARALLEL_LEVEL=str(args.test_jobs))
    if args.qt_prefix:
        environment.update(QT_PREFIX_PATH=str(args.qt_prefix), Qt6_DIR=str(args.qt_prefix / "lib/cmake/Qt6"))
    print(f"Logs and report: {logs}", flush=True)
    for name in order:
        failed_dependencies = [dep for dep in projects[name]["dependencies"]
                               if dep in report["projects"] and report["projects"][dep]["status"] != "installed"]
        if failed_dependencies:
            result = {"status": "blocked", "dependencies": failed_dependencies}
        else:
            print(f"Installing {name}…", flush=True)
            logfile = logs / f"{name}.log"
            report["projects"][name] = {"status": "running", "log": str(logfile)}
            save_report(logs, report)
            try:
                provider_operation("create", logfile)
                # No O_CREAT: only the provider is allowed to create the file.
                with os.fdopen(os.open(logfile, os.O_WRONLY | os.O_APPEND), "a", encoding="utf-8") as log:
                    details = install_project(name, projects[name], projects, args, environment,
                                              log, definitions[name], installer_args[name])
                result = {"status": "installed", **details}
                if args.keep_logs:
                    result["log"] = str(logfile)
                else:
                    logfile.unlink()
            except (subprocess.CalledProcessError, OSError, ValueError) as error:
                result = {"status": "failed", "error": str(error), "log": str(logfile)}
                if logfile.is_file():
                    print(logfile.read_text(encoding="utf-8", errors="replace")[-3000:], flush=True)
                else:
                    print(str(error), flush=True)
        report["projects"][name] = result
        print(f"{name}: {result['status']}", flush=True)
        save_report(logs, report)
    report["finished_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report["success"] = all(result["status"] == "installed" for result in report["projects"].values())
    save_report(logs, report)
    print(f"Report: {logs / 'report.json'}", flush=True)
    return 0 if report["success"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
