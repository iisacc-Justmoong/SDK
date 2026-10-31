<a id="windows-sdk-검증"></a>
# Windows SDK Verification

The iiImageCodec module has been renamed to iiVisualCodec, and projects.json uses the new name. Historical verification and publication reports retain the name used on their recorded date. A complete Visual build requires separate LGPL FFmpeg and OpenUSD CMake/pkg-config installations; follow iiVisualCodec/docs/Packaging.md.
Select the compiler using the absolute `cl.exe` path found in the development shell. After configuration, inspect the actual cache for Release mode, compiler, installation prefix, toolchain and include settings. If CMake resets requested arguments while changing compilers, reconfigure the same `build/` with `--fresh`; stop verification if any mismatch remains.

Build the active SDKs in `projects.json` in dependency order, run every registered execution check, and install into the user SDK prefix. Deprecated and Utility projects are excluded. Directories without source build definitions do not count as successful executions.

The project inventory also includes the independent `iiCompression` and
`iiProjectManager` modules. Their presence in the inventory is not evidence
that their Windows builds or tests have been executed.

`iiSocialIntegration` replaces `iiSocialPublish` as the checkout and package
name. The legacy headers and package are source compatibility aliases of the
same module, so Windows verifies the new checkout once.

The iiFileProvider build leaves its Core I/O adapter disabled. Applications that
use it configure `SDK/iiFileProvider/example/CoreIO` separately after installing
iiFileProvider Native and iiSDKCore, and link the packages at the consumer boundary.
After the independent SDK builds, the full verification separately builds and
checks the Core I/O, audio-file, XML file-input, calendar-account, calendar-file, voice-file, camera-image, camera-media, MIDI file, MIDI async, JSON-Core value, bitmap-file, network load-balancer and decision GPU consumers, and compiles the
voice CLI and image-adjustment examples. It verifies that the moved contract tests are present;
a missing or failed composition keeps the full release gate closed. Vulkan
execution and shared-GPU concurrency cannot be counted as passed when skipped.
Partial `--only` runs execute a composition only when all of its SDKs are selected.
Detached consumer checks add the owning SDK's installed `bin` directory to their
runtime search path; other configurations keep the current build's path policy.

<a id="사용법"></a>
## Usage

Run from PowerShell at the Workspace root. Prepare the C++ build tools, Qt/vcpkg, Python/Node and project-specific external tools according to each repository's Windows instructions.

```powershell
. ./SDK/Windows/Enter-DevEnvironment.ps1
$python = Join-Path (Get-Location) 'Tools/Python314/tools/python.exe'
& $python ./SDK/Windows/Verify.py --report-dir ./SDK/Windows/resources/.state/verification/windows-sdk-verification
if ($LASTEXITCODE -ne 0) { throw '전체 SDK 검증 미완료' }
```

Reports, JUnit records and opt-in state captures are stored under `SDK/Windows/resources/.state/verification/`, not in a build tree. After fixing a failure, add `--resume` to the same command to reuse successful records whose sources, dependencies and verification tools are unchanged. `--only <SDK 이름...>` is a partial check and does not satisfy the conditions for a complete commit and push.

The batch run records consumers of a failed SDK as `dependency-failed` and continues checking independent SDKs.
It does not validate consumers against an older installation of a failed prerequisite. Any failure or blockage
makes the complete run fail. With `--only`, every selected SDK must pass before the command
exits successfully. Resuming after a fix also executes previously blocked SDKs again.

Use `BuildProject.py check SDK/<이름>` to run every stage for one SDK. All CMake output is generated under that project's `build/`. The tool uses Release MSVC x64 and applies Ninja's localized include output handling and a UTF-8 manifest.

<a id="검증-범위와-결과"></a>
## Verification scope and results

- CMake SDKs must complete configuration, build, the entire CTest suite and installation. Zero tests, unregistered executions and failed installations are not successes.
- LVRS-Svelte verifies dependency installation, Node checks, Svelte checks, the Vite build, an actual preview HTTP response and local package installation.
- ONNX is retired. Windows builds remove inherited runtime hints and retired SDK search paths, and clear legacy iiDecision cache options. iiDecision verifies its retired learned-model boundary and supported native decision interfaces. Unavailable Metal/MLX/OpenCL accelerator enumeration checks are recorded separately as skips and do not count as successful device execution. Skips outside the allowlist fail verification.
- Source or prerequisite SDK changes invalidate existing execution results. `all_projects_executed_successfully` and `commit_and_push_allowed` in `verification.json` are true only when the complete scope remains valid.
- This tool does not run Git commit, push, checkout or pull. Publishing verified changes through Git is a separate task.

Separate live integration with personal accounts, paid services, physical devices and large model weights from repository fixture and consumer execution checks. Record the execution scope in each project's instructions.

<a id="의존성-및-windows-조치"></a>
## Dependencies and Windows adjustments

The active SDKs currently do not use C++ module file sets. The shared CMake include sets `CMAKE_CXX_SCAN_FOR_MODULES=OFF` on Windows MSVC to disable unnecessary module dyndep generation. This avoids configurations on this machine that repeatedly produced LNK1104 because linking began before new objects were generated during module scanning, while retaining C++23 and `/showIncludes` header dependency tracking. Actual module file sets follow CMake's mandatory scanning rules. See [CMake's scanning rules](https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html).

`Enter-DevEnvironment.ps1` uses Workspace `Tools` and the user's `~/.local/SDK`. Do not add all of `/usr/bin` in a regular shell because similarly named Git tools can conflict with the MSVC linker. Python 3.14 is used for reproducible builds and remains separate from the system default Python.

`vcpkg-overlays/mdnsresponder` replaces a resource header that depends on MFC with a Windows SDK header. Record network service installation and physical device availability in the execution scope of the individual project.

<a id="이번-머신의-완료-기록"></a>
## Completion records for this machine

The active build list for the sources synchronized on 2026-10-24 contains 37 SDKs. It includes `LVUI` and `iiImageCodec`.
Native UI configuration follows iiSDKCore → LVUI → iiGUI → LVRS.
The list also records the prerequisites of the native iiSharedCanvas codec.
Judge the current run using the new `verification.json` and logs. Do not reuse completion counts from
earlier dates as proof that the current sources pass.
After a repository path changes, such as `iiServerHost` → `iiWebServer`, inspect the source path in the existing CMake cache
and reconfigure the same `build/` with `--fresh`. Do not delete existing output or user files
to conceal a path mismatch.
Build subprocesses receive a deduplicated PATH that preserves search order.
This fixes the Windows reproduction in which repeated development-shell and SDK prefix entries, combined with nested npm `.bin` paths,
caused `node` discovery to fail. The system PATH is not modified.
The related platform restriction is described in [Microsoft's command environment variable length documentation](https://learn.microsoft.com/en-us/troubleshoot/windows-client/shell-experience/command-line-string-limitation).

`check` also verifies the additional configurations used by actual consumers. iiFileProvider's
Compatibility package is installed into its default installation prefix. The default native LVRS configuration
and its existing Qt compatibility configuration each receive independent builds, complete test runs and installations. The Qt configuration
is installed in `~/.local/SDK/LVRS/qt`. Existing Qt consumers specify this prefix and LVRS_DIR
explicitly to avoid mixing native and Qt configurations. Qt state capture checks are also enabled and executed.
The Qt compatibility and Qt-free native iiSharedCanvas configurations are checked separately. The native
installation uses `~/.local/SDK/iiSharedCanvas/native`. A separate installed consumer discovers this package,
performs an actual file round trip and confirms that Qt targets are not imported.
Verification inspects the test records from every configuration; success in the first configuration alone does not pass the project.
If a previously verified prerequisite SDK changes during the run, its record is invalidated and its consumers are blocked.
At completion, failed and invalid states are also propagated to transitive consumers. Recording a new source fingerprint
must not allow an execution against an older installation to be reused as a successful resumed result.
Runtime SDK bin directories are also searched in dependency order. This fixes the reproduction in which
a higher-level SDK's old copy of iiSDKCore.dll loaded before the latest Core installation and stopped execution
before process entry. The current project prefers new build/ output and does not put its own old installation
ahead of build dependency DLLs. CTest uses a default 300-second timeout while retaining project-specific
explicit limits.
Search order is the installations of manifest-declared transitive SDK dependencies, the current SDK's build/ and
its Native output directories, then the remaining SDKs. Regression checks also ensure that DLLs copied by unrelated higher-level SDKs
do not hide the current SDK's new Native DLLs.

[The complete 2026-10-24 verification record](Verification-20261024.md) and [structured results](Verification-20261024.json) record actual execution counts by configuration, installation audits, corrections and exclusions for 37 active SDKs.

[The complete 2026-10-20 verification record](Verification-20261020.md) and [structured results](Verification-20261020.json) record execution counts, configurations, installation locations and remotely verified commits for 36 SDKs.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
