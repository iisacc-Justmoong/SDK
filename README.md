<a id="sdk-일괄-재설치"></a>

# SDK batch reinstallation

[`iiFileProvider`](iiFileProvider/README.md) 0.6.0 owns common file creation,
opening, staging and provision. Its Qt-free native Catalog adds file-owned
metadata, keywords, ratings, colors, collections, saved filters and indexed
search without reading full payloads. Products and format SDKs must delegate
final file I/O to provider APIs; codecs retain format semantics. See
[the ownership contract](iiFileProvider/docs/FILE_OWNERSHIP.md).

[`iiDeviceMultimedia`](iiDeviceMultimedia/README.md) owns native USB/HID, BLE GATT,
float32 PCM and MIDI UMP device I/O extracted from iiSDKCore. Version 0.2 is an
independent peer of Core with no dependency in either direction. Applications
explicitly compose Core scheduling/frame/byte I/O with device `poll()` calls. See
[independence and migration](iiDeviceMultimedia/docs/independence.md).

[`iiSDKBuilder`](iiSDKBuilder/README.md) is the Qt-free C++23 host build module
for a single CMake application and its declared iiSDK dependencies. It uses
iiSDKCore for platform detection, dependency ordering, parallel kit execution,
and atomic reports, and requests Windows, macOS, Linux, Android, iOS, and WASM
in one matrix. Each kit builds and installs its own SDK packages before building
the application; unavailable/failed targets remain explicit in the final report.
The five OS targets require native PE, ELF, or Mach-O machine-code images;
WebAssembly is a separately labeled web-engine target. Post-link inspection
rejects script launchers, CLR-managed PE, and wrong CPU/deployment platforms.

[`iiSocietyHelper`](iiSocietyHelper/README.md) 0.9.1 consolidates the former
`iiSocietyClient` module into one application-facing SDK. Link only
`iiSocietyHelper::iiSocietyHelper` for local IPC, native storage/canvas operations,
account sessions, discovery/pairing and authenticated host-drive access through
`iiSocietyHelper::Client`. Sync remains a lower-level dependency with no reverse
reference. See [migration](iiSocietyHelper/docs/Migration.md).

[`iiSocietyMonitor`](iiSocietyMonitor/README.md) provides the Qt-free C++23
`SocietyContainerDaemon` and latest-state subscriptions for iisacc apps. It
monitors the selected Society container's public metadata, publishes atomic
credential-free snapshots and detects stale producers. `iiSocietyHelper` adapts
native subscriptions to its QObject/QML `containerState` API.

The SDK installer preserves Windows drive letters in `CMAKE_PREFIX_PATH`, reads
source and installation manifests as UTF-8, and selects native Git for Windows
Bash instead of the Windows System32 WSL shim. Set `BASH_BINARY` explicitly when
using another native Bash installation.

To clone or update all SDK, Product, and Service repositories into the same
relative paths under `~/Workspace`, use `sync_workspace.ps1` in Windows
PowerShell, or `python sync_workspace.py` on a host with Python 3.10 or later.
The Windows entry point requires only PowerShell and Git, so a missing Python
installation or Windows Python app execution alias does not prevent cloning.
See [Workspace synchronization](WorkspaceSync.md) for the bootstrap commands,
conflict handling, and verification report.

[`iiColorManager`](iiColorManager/README.md) provides nine color-picker modes,
native sRGB/HSB/generic-CMYK/D50-Lab conversion, web-safe selection and shared
picker state. It is a Qt-free leaf package; Dreamscapes uses its installed CMake
target through a small GUI adapter and LVRS controls.

[`iiSocialPublish`](iiSocialPublish/README.md) provides Qt-free C++23 publishing
objects for X, Facebook Pages, Instagram Professional, Threads, Pinterest,
Tumblr, Mastodon and LinkedIn, with explicit unsupported Pixiv/ArtStation
capabilities. It streams media through libcurl, handles provider processing,
and reports ambiguous publication without automatic retries. Dreamscapes owns
the separate asynchronous product adapter. The batch installer discovers the
module's CMake package automatically.

Local photo correction is split between the existing native C++23 packages:
`iiPaintEngine` owns brush input and editable float pixel masks;
`iiGraphicAdjust` owns tone, color, detail, grain, and masked correction
composition. Neither module depends on the other. The
[LocalPhotoBrush consumer](tests/LocalPhotoBrush/README.md) composes their public
installed APIs, verifies preview/commit/cancel and pixel undo/redo, and writes
synthetic source/mask/correction PNGs. Its builds and verification records belong
under `build/local-photo-processing`. These two packages require no Qt prefix.

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

## 2026-10-19 patch release

The [SDK patch release](PatchRelease-20261019.md) increments all 35 installable
packages except LVRS by one patch. LVRS-Svelte is included as an independent
package. The release records host builds, tests, reinstalls and remote commits
separately; LVRS installer recovery is tracked as a subsequent operation.
