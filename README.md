<a id="sdk-일괄-재설치"></a>

# SDK batch reinstallation

Windows의 의존성 순서 빌드·전체 실행 검증·설치는 [Windows SDK 검증](Windows/README.md)을 따른다. 각 SDK의 README에는 해당 프로젝트의 Windows 빌드 및 실행 지침을 연결한다.

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

[`iiSocietyHelper`](iiSocietyHelper/README.md) 0.10.0 is the single Society
integration SDK for accounts, client access, local IPC, native storage/canvas,
remote generation, monitoring and device synchronization. Core uses
`iiSocietyHelper::iiSocietyHelper`; native monitoring uses
`iiSocietyHelper::Monitoring`; desktop inference uses
`iiSocietyHelper::GenerationHost`. See [the consolidation guide](iiSocietyHelper/docs/Consolidation.md).

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

## 2026-10-20 Windows SDK 검증

활성 SDK 36개의 Windows Release 빌드·실행·설치 및 원격 commit 검증 결과는 [Windows 전체 검증 기록](Windows/Verification-20261020.md)에 기록한다. 프로젝트별 Windows 지침과 공통 검증 도구를 포함하며, Deprecated와 Utility 및 소스가 없는 디렉터리는 성공 수에서 제외한다.

## Multimodal agent ownership

[iiMultimodalAgent](iiMultimodalAgent/README.md) owns the extracted agent/MCP harness
and iiDecision integration, with image, audio and speech adapters for iiLocalDiffusion,
iiAudioGen and iiVoiceOver. iiLocalLLM 0.56 provides inference without an agent or
iiDecision dependency. The installer discovers the module and orders its six SDK
dependencies automatically. Windows build metadata includes the same dependency edges.
The independent repository is registered in `workspace-repositories.json`, alongside
`iiNetwork` and `LVUI`, so Git-based workspace synchronization includes their sources.

## Source publication coverage

The clone manifest includes every implemented SDK module. Empty project placeholders
remain directories. Shared performance inventory tools, review documents and C++23
benchmark sources are tracked in this repository; generated `build/` trees and Python
bytecode remain excluded. Verify module registration and source/output exclusions with:

```sh
python3 -B -m unittest discover -s tests -p 'test_sdk_publication.py' -v
```

Helper consumer checks accept package requirements at 0.10.0 or newer, including
compatible patch releases, while rejecting retired package names and targets.
