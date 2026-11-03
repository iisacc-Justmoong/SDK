# Windows Documentation

## Windows build configuration

The project inventory includes the independent `ii3DGen` and `iiJob` CMake
projects. A user's SDK directory can be absent on a first installation. The
build driver uses the current Python interpreter and retains caller-supplied
CMake search paths and `Qt6_DIR`. `VCPKG_ROOT` selects the x64 package prefix,
toolchain and verification fingerprint; set custom values after sourcing the
development-shell helper, which initializes defaults. It forwards `DOKAN_ROOT` only for
iiSocietyContainer when provided; otherwise that project's own discovery and
errors apply. Existing test and installation failure conditions are unchanged.
LVRS-Svelte uses Node.js on `PATH`; `NPM_CLI_JS` can select an npm CLI outside
the Node.js installation directory.

The default build inventory orders iiJson before iiActorObject and
iiAccountManager, iiJob before iiSocietyContainer, and iiColorManager before
LVUI and iiGUI. These entries record dependencies already required by their
current configurations; native LVRS also requires iiJson, iiNetwork and
iiVisualCodec. Optional features can require additional packages.
`iiSocialPublish` is a compatibility symlink to iiSocialIntegration and is
built through that canonical project rather than as a duplicate installation.

The current Society monitoring configuration requires the separately built
iiFileProvider Core I/O adapter. In a complete run, its C++ consumer project
is configured, built, tested and installed after iiFileProvider and iiSDKCore
and before iiSocietyHelper. Failure blocks that consumer and its dependents.
Partial runs that omit the adapter prerequisites continue to use whatever
adapter installation the selected consumer can discover.

The ii3DGen test-enabled configuration requires the prepared mlx-swift CPU
port; select its directory with `II3DGEN_MLX_TEST_SOURCE` before configuring,
or set the CMake cache path explicitly. The optional inference backend and its
model weights have additional separate requirements. iiSDKBuilder's HLS tests
discover clang from `LLVM_ROOT/bin`, common Windows LLVM installations, the
Visual Studio toolchain or `PATH`; `IISDK_HLS_CLANG` remains an explicit
override. Neither prerequisite is downloaded by the build driver.

On 2026-11-03 the isolated iiVirtualizer test host was stopped, so the Windows
guest build did not start. Previously observed storage stalls and memory
fluctuations were not established as resolved. These changes have no Windows
compilation, test, or installation result; project-specific dependencies and
actual execution remain separate prerequisites.

<!-- workspace-docs:document-map:start -->
## Human and agent document map

Markdown is the maintained human source. Agent JSON is a structured, source-traceable projection.

| Document | Agent JSON |
| --- | --- |
| [`AGENT.md`](../AGENT.md) | [JSON](entrypoints/AGENT.agent.json) |
| [`Publication-20261025.md`](../Publication-20261025.md) | [JSON](entrypoints/Publication-20261025.agent.json) |
| [`README.md`](../README.md) | [JSON](entrypoints/README.agent.json) |
| [`Verification-20261020.md`](../Verification-20261020.md) | [JSON](entrypoints/Verification-20261020.agent.json) |
| [`Verification-20261024.md`](../Verification-20261024.md) | [JSON](entrypoints/Verification-20261024.agent.json) |
| [`docs/GPU.md`](GPU.md) | [JSON](GPU.agent.json) |
| [`docs/Publication-20261025.md`](Publication-20261025.md) | [JSON](Publication-20261025.agent.json) |
| [`docs/RuntimeRetirement.md`](RuntimeRetirement.md) | [JSON](RuntimeRetirement.agent.json) |
| [`docs/Verification-20261020.md`](Verification-20261020.md) | [JSON](Verification-20261020.agent.json) |
| [`docs/Verification-20261024.md`](Verification-20261024.md) | [JSON](Verification-20261024.agent.json) |
| [`docs/documentation-structure.md`](documentation-structure.md) | [JSON](documentation-structure.agent.json) |
<!-- workspace-docs:document-map:end -->
