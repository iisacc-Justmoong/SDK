# SDK patch release — 2026-10-19

This maintenance release increments each installable SDK except LVRS by one patch.
LVRS remains 1.0.1; its separately requested installer repair follows this release.
LVRS-Svelte is an independent package and is included.
The iiFileProvider package also updates its independently versioned Native
component from 0.8.0 to 0.8.1 and Core I/O adapter from 0.1.0 to 0.1.1.

## Package versions

| Module | Previous | Release |
| --- | --- | --- |
| iiAcountManager | 0.2.9 | 0.2.10 |
| iiActorObject | 0.1.0 | 0.1.1 |
| iiAudioGen | 0.3.0 | 0.3.1 |
| iiCalendar | 0.2.0 | 0.2.1 |
| iiCameraIntergration | 0.4.0 | 0.4.1 |
| iiColorManager | 0.1.0 | 0.1.1 |
| iiCSMIDI | 0.3.0 | 0.3.1 |
| iiDecision | 0.0.4 | 0.0.5 |
| iiDeviceMultimedia | 0.2.0 | 0.2.1 |
| iiFileProvider | 0.7.1 | 0.7.2 |
| iiGeneralDocument | 0.4.0 | 0.4.1 |
| iiGraphicAdjust | 0.6.0 | 0.6.1 |
| iiGUI | 0.4.0 | 0.4.1 |
| iiHtmlBlock | 0.1.0 | 0.1.1 |
| iiJson | 0.1.0 | 0.1.1 |
| iiLicenseManager | 0.3.0 | 0.3.1 |
| iiLocalDiffusion | 0.6.1 | 0.6.2 |
| iiLocalLLM | 0.55.0 | 0.55.1 |
| iiPaintEngine | 0.5.0 | 0.5.1 |
| iiPhotoLibrary | 0.1.0 | 0.1.1 |
| iiSDKBuilder | 0.2.0 | 0.2.1 |
| iiSDKCore | 0.18.0 | 0.18.1 |
| iiWebServer | 0.6.0 | 0.6.1 |
| iiSharedCanvas | 0.31.0 | 0.31.1 |
| iiSocialPublish | 0.1.0 | 0.1.1 |
| iiSocietyContainer | 0.16.1 | 0.16.2 |
| iiSocietyGeneration | 0.1.0 | 0.1.1 |
| iiSocietyHelper | 0.9.3 | 0.9.4 |
| iiSocietyMonitor | 0.1.0 | 0.1.1 |
| iiSocietySync | 0.8.3 | 0.8.4 |
| iiUpdateManager | 0.3.0 | 0.3.1 |
| iiVoiceOver | 0.1.0 | 0.1.1 |
| iiWhatsNew | 0.1.0 | 0.1.1 |
| iiXml | 0.1.0 | 0.1.1 |
| LVRS-Svelte | 0.2.1 | 0.2.2 |

## Installation and verification

Host builds use each module's fixed `build/` directory and retain its enabled features.
The existing LVRS macOS package is reused for dependent SDKs during the batch.
iiSharedCanvas exports ExactVersion metadata, so its SDK consumers request 0.31.1.
The Decision cache was regenerated with its feature options preserved because a
previous SDK environment had incorrectly cached SDK headers as compiler defaults.

Per-module build, CTest, installation and Git results are collected in
`build/patch-release-20261019/` and `build/install-all/`. Runtime checks on other
operating systems and model generation requiring user assets are separate evidence.

Directories without a package version or install rule are outside this release:
Flamewall, LVUI, Silentlocked, iiComputerHistory, iiComputerUse and iiNetwork.

The iiGraphicAdjust feature implementation concurrently advanced to 0.6.0 after
the initial 0.5.2 patch build. That completed source is preserved, then incremented
by one patch to 0.6.1 and rebuilt, tested and reinstalled for this release.

## Compatibility repairs

- iiSDKBuilder now links its file-provider adapter only to targets that use it,
  preserving the embedded iiSDKCore foundation boundary.
- iiFileProvider supplies a relocatable, bundled SQLite provider for mobile and
  WebAssembly builds. It also retains standard cancellation on the Qt WebAssembly
  toolchain and handles time zones when that kit has no Qt time-zone database.
  The iOS, Android arm64 and multithreaded WebAssembly libraries were built and
  installed with their target Qt kits, without linking host libraries.
- iiSocietyMonitor consumes the extracted iiFileProvider Core I/O adapter.
  iiSocietySync distinguishes provider-owned presentation files and lease
  controls from replicated payloads, preserving confined filesystem operations.
- iiLocalDiffusion retains its enabled backends while fixing current Clang
  header compatibility, preview-aware model identities, known VAE manifest
  entries, and the ONNX initialization/shutdown telemetry issue.
- iiLocalLLM compiles its pinned llama backend with current Clang by providing
  required standard headers and correctly ordered graph specializations in
  build-local copies. The pinned dependency checkout remains unchanged.

Verification that exercises file synchronization, presentation generation and
large model fixtures is run serially across modules. Under concurrent disk-heavy
validation, otherwise successful synchronization operations exceeded their
existing test deadlines; the deadlines are retained for the final isolated run.

The iiLocalLLM release scope includes all 105 host contract tests and six
additional real-model agent scenarios: plan mode, user questions, project
memory, memory recall, memory extraction and session history. The host set also
contains the configured vision and audio inference checks. The other 34 optional
`inference`-label qualifications remain registered with their existing model
settings and are recorded as not run for this maintenance release. No runtime
backend or model configuration is disabled to obtain a passing result.

## Verified SDK delivery

All 35 source versions match the installed package metadata. Each module has a
clean worktree and a local commit matching its live remote branch. The workspace
repository manifest pins those verified module commits; LVRS remains outside
the version bump. LVRS-Svelte is installed as a package copy at
`~/.local/SDK/lib/node_modules/lvrs-svelte`; native package prefixes are under
`~/.local/SDK/<module>`.

| Package | Verified host evidence |
| --- | --- |
| iiFileProvider | 36 tests, including a relocated bundled-SQLite consumer |
| iiSharedCanvas | 63 source tests and 28 independent installed-consumer tests |
| iiLocalLLM | 105 host contracts and 6 additional real-model agent scenarios |
| iiLocalDiffusion | All 137 configured tests, including relocated installation |
| iiSocietySync | All 12 tests with existing deadlines retained |
| iiPhotoLibrary | All 8 tests, including installed consumers |
| iiSocietyHelper | All 12 tests, including delivery retry/rollback |

The other modules' CTest or npm test/check/build evidence is retained in their
individual installation reports. Fixes were followed by focused reruns; combined
validation records retain both the original failures and the resolving results.
The final local index is `build/patch-release-20261019/sdk-final-audit.json`.
Cross builds do not constitute device or other-operating-system runtime proof.
