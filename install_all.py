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
    return "\n".join(re.sub(r"#[^\n]*", "", p.read_text())
                     for p in files if p.is_file())


def discover(root):
    projects, skipped = {}, []
    for directory in sorted(root.iterdir()):
        if not directory.is_dir() or directory.is_symlink() or directory.name.startswith("."):
            continue
        if not any((directory / marker).is_file() for marker in ("install.sh", "CMakeLists.txt")):
            if directory.name not in ("build", "tests"):
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
            for dependency in extra.read_text().splitlines():
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


def install_project(name, project, projects, args, environment, log, definitions, installer_args):
    directory, build = project["path"], project["path"] / "build"
    prefix = args.prefix_root / name
    environment = environment.copy()
    environment["INSTALL_PREFIX"] = str(prefix)
    config = [f"-DCMAKE_BUILD_TYPE={args.configuration}", "-DBUILD_TESTING=ON",
              f"-DCMAKE_INSTALL_PREFIX={prefix}",
              f"-DCMAKE_PREFIX_PATH={environment['CMAKE_PREFIX_PATH']}"]
    if args.qt_prefix:
        config.append(f"-DQt6_DIR={args.qt_prefix}/lib/cmake/Qt6")
    cache_file = build / "CMakeCache.txt"
    if cache_file.is_file():
        cache = dict(re.findall(r"^([\w.+-]+):[^=\n]*=([^\n]*)$", cache_file.read_text(), re.M))
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
    if native:
        # Refresh dependency cache entries even when an existing installer later
        # reconfigures: old build-tree package paths must not win over installation.
        run(["cmake", "-S", str(directory), "-B", str(build), *config], directory, environment, log)
    installer = directory / "install.sh"
    if installer.is_file() and name not in args.cmake:
        run(["bash", str(installer), *installer_args], directory, environment, log)
    else:
        if not native:
            raise ValueError(f"{name}: CMake fallback requires CMakeLists.txt")
        run(["cmake", "--build", str(build), "--config", args.configuration,
             "--parallel", str(args.jobs)], directory, environment, log)
        run(["ctest", "--test-dir", str(build), "-C", args.configuration,
             "--output-on-failure", "--no-tests=error"], directory, environment, log)
        run(["cmake", "--install", str(build), "--config", args.configuration,
             "--prefix", str(prefix)], directory, environment, log)
    if native:
        # Installers sometimes omit tests (or filter them). Always verify the
        # current complete host suite after the installation, against fresh SDKs.
        installer_text = installer.read_text() if installer.is_file() else ""
        if (installer.is_file() and name not in args.cmake
                and ("ctest" not in installer_text or re.search(r"\bctest[^\n]*\s-E\s", installer_text))):
            run(["cmake", "--build", str(build), "--config", args.configuration,
                 "--parallel", str(args.jobs)], directory, environment, log)
            run(["ctest", "--test-dir", str(build), "-C", args.configuration,
                 "--output-on-failure", "--no-tests=error"], directory, environment, log)
        host = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}.get(platform.system())
        host_prefix = prefix / "platforms" / (host or "unknown")
        if host and host_prefix.is_dir():
            # Some consumers deliberately resolve platforms/<host>. Refresh that
            # existing layout too without deleting any other installed ABI.
            run(["cmake", "--install", str(build), "--config", args.configuration,
                 "--prefix", str(host_prefix)], directory, environment, log)
        manifest = build / "install_manifest.txt"
        if manifest.is_file():
            files = [Path(line) for line in manifest.read_text().splitlines() if line]
            missing = [str(path) for path in files if not path.exists()]
            if not files or missing:
                raise ValueError(f"{name}: installed files missing: {missing}")
            return {"manifest": str(manifest), "installed_file_count": len(files)}
    return {}


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
    logs = args.root / "build" / "install-all" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    logs.mkdir(parents=True)
    environment = os.environ.copy()
    for variable in ("DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
                     "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_QML_IMPORT_PATH", "CPATH",
                     "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"):
        environment.pop(variable, None)
    prefixes = ([str(args.qt_prefix)] if args.qt_prefix else [])
    prefixes += [str(args.prefix_root / name) for name in ordered(projects)]
    prefixes += [value for value in environment.get("CMAKE_PREFIX_PATH", "").replace(":", ";").split(";") if value]
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
            try:
                with logfile.open("w") as log:
                    details = install_project(name, projects[name], projects, args, environment,
                                              log, definitions[name], installer_args[name])
                result = {"status": "installed", "log": str(logfile), **details}
            except (subprocess.CalledProcessError, OSError, ValueError) as error:
                result = {"status": "failed", "error": str(error), "log": str(logfile)}
                print(logfile.read_text()[-3000:], flush=True)
        report["projects"][name] = result
        print(f"{name}: {result['status']}", flush=True)
        (logs / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    report["finished_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report["success"] = all(result["status"] == "installed" for result in report["projects"].values())
    (logs / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Report: {logs / 'report.json'}", flush=True)
    return 0 if report["success"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
