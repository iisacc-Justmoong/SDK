<a id="sdk-일괄-재설치"></a>

# SDK batch reinstallation

Model policy: ONNX model files and the retired ONNX runtime are unsupported. GGUF,
safetensors and other previously supported non-ONNX formats remain permitted.
Each backend must report which formats and model architectures it can actually run.

Project-owned fixed inputs belong in each module's `resources/` directory.
`build/` contains generated build outputs and temporary test data, not separately
provisioned model or asset resources.

[`iiGPU`](iiGPU/README.md) provides Qt-free C++23 native GPU buffers, compute
pipelines, command lists, explicit barriers and observable submission completion.
Metal and Vulkan share this contract; the optional `iiGPU::Runtime` target
observes completion using a scheduler supplied by the application. Applications
may compose it with iiSDKCore's EventLoop. Applications compose iiDecision's
GPU evidence adapter with iiGPU independently. See
[the GPU ownership boundary](docs/GPU.md).

[`iiCompression`](iiCompression/README.md) provides bounded in-memory gzip
compression and single-member decompression without Qt or other SDK
dependencies. Applications can compose it with their format and file modules.

Public headers live beside their implementations under each module's `src/`,
organized by responsibility without repeating the project name. Existing modules
are being migrated; their installed include names remain stable.

SDK modules are being migrated to independently built packages. Applications
compose them through caller-owned interfaces and link each selected module
directly; the transition and remaining dependency audit are documented in
[SDK independence](docs/SDKIndependence.md).
Separate authored `include/` trees are prohibited; public include names and
installed package layouts remain compatible. See [the source layout contract](docs/HeaderLayout.md)
and run `python3 -B tests/test_source_header_layout.py` from this directory.

Follow [Windows SDK verification](Windows/README.md) for dependency-ordered Windows builds, complete execution checks and installation. Each SDK README links to that project's Windows build and execution instructions.

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

[`iiGPU::WebGPU`](iiGPU/README.md) is the optional Qt-free C++23 WebGPU component.
It owns adapter/device negotiation, logical allocation budgets, aligned buffer
suballocation, resource lifetime, WGSL pipelines, command submission, mapping and
completion. The standard native C API remains available for surfaces, rendering,
indirect commands and queries. The pinned Dawn backend executes on native GPU
APIs; Core and inference SDKs remain independent. See [its execution and memory
contract](iiGPU/docs/WebGPUContract.md) and [platform verification](iiGPU/docs/WebGPUVerification.md).

[`iiSocialIntegration`](iiSocialIntegration/README.md) provides Qt-free C++23 publishing
objects for X, Facebook Pages, Instagram Professional, Threads, Pinterest,
Tumblr, Mastodon, LinkedIn and YouTube, with explicit unsupported Pixiv/ArtStation
publishing capabilities. It streams media through libcurl, handles provider processing,
and reports ambiguous publication without automatic retries. It also converts
supplied external posts into Society draft objects, and discovers and streams
selected media from web-page URLs. Dreamscapes owns
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

[`iiPaintEngine::BrushTools`](iiPaintEngine/README.md) is the C++23 brush authoring layer above
iiPaintEngine. It defines 22 controls and their tool availability, whole-gesture
size taper, bounded post correction, generated bristle tips and final pixel tiles.
The lower engine and canvas document keep their streaming/pixel contracts. It is
an optional component in the iiPaintEngine repository; the legacy iiBrushTools
package name remains available as a compatibility alias.
See [the authoring boundary and delivery contract](docs/BRUSH_AUTHORING.md).

The [module consolidation guide](docs/ModuleConsolidation.md) records component
ownership, compatibility aliases, remaining overlap and validation boundaries.

Iterate through the SDK in this directory with `./install.sh --qt-prefix /Volumes/Storage/Qt/6.8.3/macos` to update the host installation. Python 3 standard libraries and existing SDK installer, CMake /CTest are reused, and SDK name or installation order arrays are not embedded in code.

Automatically discover `install.sh` or `CMakeLists.txt` in the directory immediately below. Empty directories, hidden directories, and build outputs are not installation targets. CMake's `project`, `find_package`, `find_dependency`, and namespace targets read local dependencies and perform topological sorting. Dependencies calculated as variables or projects without CMake can declare additional dependency directories for each project one per line in `install-dependencies.txt`. Unknown declarations and circular dependencies are treated as errors before installation.

If `install.sh` already exists, execute its install·package·verify rules, except that LVRS uses the CMake route to preserve other builds and platforms. Otherwise, process in the order: CMake configuration → build → CTest → install. The installer may skip tests; in such cases, run the full host test. If the installer has already run the full test, do not run it again. Existing `platforms/<host>` profiles are preserved by default: use `--refresh-host-prefix` only after checking their backend and ABI. Preserve the existing build's feature options, and update the CMake path of discovered dependencies to the new installer. Consumers of a failed SDK are not executed, but independent SDKs continue to install. If any step fails or is blocked, the exit code is not 0. Caches pointing to IDE inside Ninja/Make are restored to the same build tool in the current PATH. Generators and existing feature options are not changed, and if no alternative tool is available, the failure is recorded. Failure recovery using already-verified dependencies runs at `--only SDK --no-dependencies`. This option processes only the selected project, and the actual availability of installed dependencies is verified by CMake configuration and the project's tests. The dependency order between selected SDK is preserved.

```sh
./install.sh --dry-run
./install.sh --jobs 2 --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
./install.sh --only iiSocietyHelper --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
python3 -B -m unittest discover -s tests -p 'test_install_all.py' -v
```

`--cmake SDK` applies the standard CMake procedure instead of the existing installer for a specific SDK. Build concurrency is set to `--jobs` (default 2), and CTest concurrency is set to `--test-jobs` (default 1). Tests run serially by default to avoid resource contention with GUI·local network tests. Individual installers specifying their own `ctest --parallel` take precedence. For example, LVRS's existing CLI removes all platform installers and `build/`, so `--cmake LVRS` is used for host updates that preserve existing platform and task outputs. Additional CMake arguments are specified as `--define SDK=-DKEY=VALUE`, and installer arguments are specified as `--installer-arg SDK=ARGUMENT`. The default install path is `~/.local/SDK/<directory name>`. The existing installer preserves its prefix environment variable and platform selection policy, so it follows the SDK's documentation. `--prefix-root` applies to CMake direct install and dependency paths and does not override the existing installer's dedicated settings.

If using the existing multi-platform installer during host reinstallation, set environment variables such as `*_INSTALL_PLATFORMS=macos` for the SDK. Other ABIs must be built separately using the platform's toolchain, and host verification success does not imply mobile or Wasm verification.

Each run writes one status report under `.state/install-all/<run timestamp>/`, outside build output. Failed project logs are kept there; successful project logs are removed by default, or retained with `--keep-logs`. The report records install manifests and file existence checks. Tests verify virtual SDK addition, dependency order, failure propagation, empty paths, and actual CMake installation. App and user model and storage data are not deleted.

For a host refresh without starting CTest executables or custom native installers,
use `./install.sh --no-tests --qt-prefix /Volumes/Storage/Qt/6.8.3/macos`.
This uses CMake for each native project, including LVRS, disables CMake's
test targets and recognized example, benchmark and demo options, and records
`tests: not_run`; it is build and installation evidence only. Non-CMake
installers cannot be controlled by this switch and are reported as failures;
run them separately after reviewing their behavior. A manifest proves that its
listed files exist, not that every consumer is ABI-compatible. The installed
tree may be partially changed if an install fails; keep a backup before an
in-place refresh. The report is saved before each operation with `pending` and
`running` states, so interrupted runs can be identified and retried. Discovery
is a static approximation of remaining legacy dependencies, not proof that SDKs
are independent. See [Installation operations](docs/Installation.md).

## 2026-10-19 patch release

The [SDK patch release](PatchRelease-20261019.md) increments all 35 installable
packages except LVRS by one patch. LVRS-Svelte is included as an independent
package. The release records host builds, tests, reinstalls and remote commits
separately; LVRS installer recovery is tracked as a subsequent operation.

<a id="2026-10-20-windows-sdk-검증"></a>
## Windows SDK verification, 2026-10-20

[The complete Windows verification record](Windows/Verification-20261020.md) records Windows Release builds, execution, installation and remote commit verification for 36 active SDKs. It includes project-specific Windows instructions and shared verification tools. Deprecated and Utility projects and directories without source are excluded from the success count.

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

## License

SPDX-License-Identifier: MIT

The original code and documentation in this repository are licensed under the
[MIT License](LICENSE). Third-party code, libraries, fonts, tools, and model
weights retain their respective licenses.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).

### Child projects

- [`SDK/Flamewall`](Flamewall/README.md)
- [`SDK/LVRS`](LVRS/README.md)
- [`SDK/LVRS-Svelte`](LVRS-Svelte/README.md)
- [`SDK/LVUI`](LVUI/README.md)
- [`SDK/Silentlocked`](Silentlocked/README.md)
- [`SDK/Windows`](Windows/README.md)
- [`SDK/ii3DGen`](ii3DGen/README.md)
- [`SDK/iiAccountManager`](iiAccountManager/README.md)
- [`SDK/iiActorObject`](iiActorObject/README.md)
- [`SDK/iiAudioGen`](iiAudioGen/README.md)
- [`SDK/iiCSMIDI`](iiCSMIDI/README.md)
- [`SDK/iiCalendar`](iiCalendar/README.md)
- [`SDK/iiCameraIntegration`](iiCameraIntegration/README.md)
- [`SDK/iiColorManager`](iiColorManager/README.md)
- [`SDK/iiCompression`](iiCompression/README.md)
- [`SDK/iiComputerHistory`](iiComputerHistory/README.md)
- [`SDK/iiComputerUse`](iiComputerUse/README.md)
- [`SDK/iiDecision`](iiDecision/README.md)
- [`SDK/iiDeviceMultimedia`](iiDeviceMultimedia/README.md)
- [`SDK/iiEmailClient`](iiEmailClient/README.md)
- [`SDK/iiFileProvider`](iiFileProvider/README.md)
- [`SDK/iiGPU`](iiGPU/README.md)
- [`SDK/iiGUI`](iiGUI/README.md)
- [`SDK/iiGeneralDocument`](iiGeneralDocument/README.md)
- [`SDK/iiGlobalSearch`](iiGlobalSearch/README.md)
- [`SDK/iiGraphicAdjust`](iiGraphicAdjust/README.md)
- [`SDK/iiJson`](iiJson/README.md)
- [`SDK/iiLicenseManager`](iiLicenseManager/README.md)
- [`SDK/iiLocalDiffusion`](iiLocalDiffusion/README.md)
- [`SDK/iiLocalLLM`](iiLocalLLM/README.md)
- [`SDK/iiMultimodalAgent`](iiMultimodalAgent/README.md)
- [`SDK/iiNetwork`](iiNetwork/README.md)
- [`SDK/iiPBRMaterial`](iiPBRMaterial/README.md)
- [`SDK/iiPaintEngine`](iiPaintEngine/README.md)
- [`SDK/iiPhotoLibrary`](iiPhotoLibrary/README.md)
- [`SDK/iiSDKBuilder`](iiSDKBuilder/README.md)
- [`SDK/iiSDKCore`](iiSDKCore/README.md)
- [`SDK/iiSharedCanvas`](iiSharedCanvas/README.md)
- [`SDK/iiSocialIntegration`](iiSocialIntegration/README.md)
- [`SDK/iiSocietyContainer`](iiSocietyContainer/README.md)
- [`SDK/iiSocietyHelper`](iiSocietyHelper/README.md)
- [`SDK/iiUpdateManager`](iiUpdateManager/README.md)
- [`SDK/iiVirtualizer`](iiVirtualizer/README.md)
- [`SDK/iiVisualCodec`](iiVisualCodec/README.md)
- [`SDK/iiVoiceOver`](iiVoiceOver/README.md)
- [`SDK/iiVolumetricSolidMesh`](iiVolumetricSolidMesh/README.md)
- [`SDK/iiWebServer`](iiWebServer/README.md)
- [`SDK/iiWhatsNew`](iiWhatsNew/README.md)
- [`SDK/iiXml`](iiXml/README.md)
<!-- workspace-docs:readme:end -->
