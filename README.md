<a id="sdk-일괄-재설치"></a>

# SDK batch reinstallation

The SDK installer preserves Windows drive letters in `CMAKE_PREFIX_PATH`, reads
source and installation manifests as UTF-8, and selects native Git for Windows
Bash instead of the Windows System32 WSL shim. Set `BASH_BINARY` explicitly when
using another native Bash installation.

To clone or update all SDK, Product, and Service repositories into the same
relative paths under `~/Workspace`, run `python sync_workspace.py` after cloning
this SDK repository. See [Workspace synchronization](WorkspaceSync.md) for the
Windows bootstrap commands, conflict handling, and verification report.

Iterate through the SDK in this directory with `./install.sh --qt-prefix /Volumes/Storage/Qt/6.8.3/macos` to update the host installation. Python 3 standard libraries and existing SDK installer, CMake /CTest are reused, and SDK name or installation order arrays are not embedded in code.

Automatically discover `install.sh` or `CMakeLists.txt` in the directory immediately below. Empty directories, hidden directories, and build outputs are not installation targets. CMake's `project`, `find_package`, `find_dependency`, and namespace targets read local dependencies and perform topological sorting. Dependencies calculated as variables or projects without CMake can declare additional dependency directories for each project one per line in `install-dependencies.txt`. Unknown declarations and circular dependencies are treated as errors before installation.

If `install.sh` already exists, execute its install·package·verify rules. If not, process in the order: CMake configuration → build → CTest → install. The installer may skip tests; in such cases, run the full host test. If the installer has already run the full test, do not run it again. Also update the existing `platforms/<host>` install path with the same build, while keeping other ABI installers. Preserve the existing build's feature options, and update the CMake path of discovered dependencies to the new installer. Consumers of a failed SDK are not executed, but independent SDKs continue to install. If any step fails or is blocked, the exit code is not 0. Caches pointing to IDE inside Ninja/Make are restored to the same build tool in the current PATH. Generators and existing feature options are not changed, and if no alternative tool is available, the failure is recorded. Failure recovery using already-verified dependencies runs at `--only SDK --no-dependencies`. This option processes only the selected project, and the actual availability of installed dependencies is verified by CMake configuration and the project's tests. The dependency order between selected SDK is preserved.

```sh
./install.sh --dry-run
./install.sh --jobs 2 --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
./install.sh --only iiSocietyHelper --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
python3 -B -m unittest discover -s tests -p 'test_install_all.py' -v
```

`--cmake SDK` applies the standard CMake procedure instead of the existing installer for a specific SDK. Build concurrency is set to `--jobs` (default 2), and CTest concurrency is set to `--test-jobs` (default 1). Tests run serially by default to avoid resource contention with GUI·local network tests. Individual installers specifying their own `ctest --parallel` take precedence. For example, LVRS's existing CLI removes all platform installers and `build/`, so `--cmake LVRS` is used for host updates that preserve existing platform and task outputs. Additional CMake arguments are specified as `--define SDK=-DKEY=VALUE`, and installer arguments are specified as `--installer-arg SDK=ARGUMENT`. The default install path is `~/.local/SDK/<directory name>`. The existing installer preserves its prefix environment variable and platform selection policy, so it follows the SDK's documentation. `--prefix-root` applies to CMake direct install and dependency paths and does not override the existing installer's dedicated settings.

If using the existing multi-platform installer during host reinstallation, set environment variables such as `*_INSTALL_PLATFORMS=macos` for the SDK. Other ABIs must be built separately using the platform's toolchain, and host verification success does not imply mobile or Wasm verification.

Logs and execution results for each run are saved to `build/install-all/<run timestamp>/report.json`. Status per SDK, execution logs, install manifests, and file existence checks are recorded separately. Tests verify virtual SDK addition, dependency order, failure propagation, empty paths, and actual CMake installation, and temporary files are placed under `build/`. App and user model and storage data are not deleted.
