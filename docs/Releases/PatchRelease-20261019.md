# SDK patch release — 2026-10-19

This maintenance release increments each installable SDK except LVRS by one patch.
LVRS remains 1.0.1; its separately requested installer repair follows this release.
LVRS-Svelte is an independent package and is included.
The iiFileProvider package also updates its independently versioned Native
component from 0.8.0 to 0.8.1 and Core I/O adapter from 0.1.0 to 0.1.1.

## Package versions

- Module: iiAccountManager; Previous: 0.2.9; Release: 0.2.10
- Module: iiActorObject; Previous: 0.1.0; Release: 0.1.1
- Module: iiAudioGen; Previous: 0.3.0; Release: 0.3.1
- Module: iiCalendar; Previous: 0.2.0; Release: 0.2.1
- Module: iiCameraIntegration; Previous: 0.4.0; Release: 0.4.1
- Module: iiColorManager; Previous: 0.1.0; Release: 0.1.1
- Module: iiCSMIDI; Previous: 0.3.0; Release: 0.3.1
- Module: iiDecision; Previous: 0.0.4; Release: 0.0.5
- Module: iiDeviceMultimedia; Previous: 0.2.0; Release: 0.2.1
- Module: iiFileProvider; Previous: 0.7.1; Release: 0.7.2
- Module: iiGeneralDocument; Previous: 0.4.0; Release: 0.4.1
- Module: iiGraphicAdjust; Previous: 0.6.0; Release: 0.6.1
- Module: iiGUI; Previous: 0.4.0; Release: 0.4.1
- Module: iiHtmlBlock; Previous: 0.1.0; Release: 0.1.1
- Module: iiJson; Previous: 0.1.0; Release: 0.1.1
- Module: iiLicenseManager; Previous: 0.3.0; Release: 0.3.1
- Module: iiLocalDiffusion; Previous: 0.6.1; Release: 0.6.2
- Module: iiLocalLLM; Previous: 0.55.0; Release: 0.55.1
- Module: iiPaintEngine; Previous: 0.5.0; Release: 0.5.1
- Module: iiPhotoLibrary; Previous: 0.1.0; Release: 0.1.1
- Module: iiSDKBuilder; Previous: 0.2.0; Release: 0.2.1
- Module: iiSDKCore; Previous: 0.18.0; Release: 0.18.1
- Module: iiWebServer; Previous: 0.6.0; Release: 0.6.1
- Module: iiSharedCanvas; Previous: 0.31.0; Release: 0.31.1
- Module: iiSocialPublish; Previous: 0.1.0; Release: 0.1.1
- Module: iiSocietyContainer; Previous: 0.16.1; Release: 0.16.2
- Module: iiSocietyGeneration; Previous: 0.1.0; Release: 0.1.1
- Module: iiSocietyHelper; Previous: 0.9.3; Release: 0.9.4
- Module: iiSocietyMonitor; Previous: 0.1.0; Release: 0.1.1
- Module: iiSocietySync; Previous: 0.8.3; Release: 0.8.4
- Module: iiUpdateManager; Previous: 0.3.0; Release: 0.3.1
- Module: iiVoiceOver; Previous: 0.1.0; Release: 0.1.1
- Module: iiWhatsNew; Previous: 0.1.0; Release: 0.1.1
- Module: iiXml; Previous: 0.1.0; Release: 0.1.1
- Module: LVRS-Svelte; Previous: 0.2.1; Release: 0.2.2

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

- Package: iiFileProvider; Verified host evidence: 36 tests, including a relocated bundled-SQLite consumer
- Package: iiSharedCanvas; Verified host evidence: 63 source tests and 28 independent installed-consumer tests
- Package: iiLocalLLM; Verified host evidence: 105 host contracts and 6 additional real-model agent scenarios
- Package: iiLocalDiffusion; Verified host evidence: All 137 configured tests, including relocated installation
- Package: iiSocietySync; Verified host evidence: All 12 tests with existing deadlines retained
- Package: iiPhotoLibrary; Verified host evidence: All 8 tests, including installed consumers
- Package: iiSocietyHelper; Verified host evidence: All 12 tests, including delivery retry/rollback

The other modules' CTest or npm test/check/build evidence is retained in their
individual installation reports. Fixes were followed by focused reruns; combined
validation records retain both the original failures and the resolving results.
The final local index is `build/patch-release-20261019/sdk-final-audit.json`.
Cross builds do not constitute device or other-operating-system runtime proof.
