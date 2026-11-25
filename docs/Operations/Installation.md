# SDK host installation

Run `./install.sh --dry-run` before changing installed SDKs. The installer discovers immediate child projects with an installer or a CMake project, skips symlinks and non-project directories, and orders currently declared local dependencies. This static scan cannot detect every computed or runtime dependency and does not mean migration to independent SDKs is complete. A failed prerequisite blocks selected dependents; independent projects continue.

The installer is an application-level consumer of iiFileProvider. Its Python
adapter and a working native iiFileProvider library must already be available
before a non-dry run; if they are missing, bootstrap iiFileProvider first using
its own documented build and install procedure. Logs, directories and atomic
report publication are created through that provider. A short-lived helper
loads the provider for each file operation, so the main installer does not
keep the provider library loaded while reinstalling it. Build tools still own
their normal compiler-generated files.

The default host installation uses `~/.local/SDK/<module>`. Existing custom installers may use a different documented prefix or perform additional work; `--prefix-root` cannot force those installers to honor it. Review their scripts first. Use `--cmake <module>` for a standard CMake build, full CTest, and installation. LVRS always uses the CMake route in this bulk tool because its custom multi-platform installer can remove existing build or platform output. Existing `platforms/<host>` profiles are preserved by default because they can contain a different backend or ABI. `--refresh-host-prefix` explicitly overwrites an existing host profile with the current build; use it only after verifying that the backend and ABI match.

The ordinary CMake build enables tests and disables declared example, demo and benchmark build/install options by default; custom installers and unconditional targets need project-specific review. User `--define` options can override those defaults.

Use `--no-tests` for a production CMake build and installation without executing CTest or native custom installers. It configures `BUILD_TESTING=OFF` and disables this project's declared build/install options for tests, examples, benchmarks and demos, unless an explicit later `--define` overrides them. Other feature settings are retained. Options with unrelated names or unconditional extra targets require project-specific changes. This does not verify runtime behavior or installed consumers. Projects that only supply a custom installer fail explicitly in this mode and need a separate reviewed installation. It does not prevent CMake configuration or build tools from executing their own commands. Ordinary mode can execute project tests and installer-defined commands, including tests which open windows.

```sh
./install.sh --dry-run
./install.sh --no-tests --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
CMAKE_PREFIX_PATH="$HOME/.local/SDK/Vulkan" ./install.sh --no-tests --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
./install.sh --only iiJson --no-dependencies --no-tests --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
python3 -B -m unittest discover -s tests -p 'test_install_all.py' -v
```

`--qt-prefix` adds Qt; additional third-party prefixes such as an installed
Vulkan SDK must be supplied explicitly through `CMAKE_PREFIX_PATH` or the
project's own documented toolchain settings. Presence of Vulkan drivers alone
does not imply that CMake can find its development package.

Build and installation occur in place: back up existing package directories first and keep active applications and their data intact. A failed installation may leave a partially updated prefix; neither manifest checking nor this script is a transaction or rollback. Installing a changed public ABI may require rebuilding dependent packages and apps. Do not infer compatibility just because a file exists.

The recovery copies from the 2026-10-30 SDK refresh are preserved under
`resources/.recovery/sdk-install-20261030/`, outside the build tree. The JSON
records there identify the earlier iiJson, iiGUI, LVUI and LVRS profile recovery;
historical build-report paths in the summary may no longer exist after cleanup.

Each run stores one report under `resources/.state/install-all/<timestamp>/`, outside build output. Failed project logs are retained there; successful project logs are removed by default, with `--keep-logs` available for deliberate retention. Entries begin as `pending`, change to `running` before the operation, then become `installed`, `failed`, or `blocked`. The report is atomically replaced after each transition, so an interrupted `running` entry is not reported as success. Inspect the log and prefix before retrying. For each CMake install, the manifest must be nonempty, every listed path must exist, and at least one path must be under the requested prefix. The primary and optional host-specific installs are checked separately; CMake replaces the manifest with the last install's list. This does not prove content or ABI compatibility, and host success says nothing about Windows, Linux, Android, iOS, WebAssembly, or another host toolchain. A non-CMake installer is responsible for its own verification. `--only` retries the selected projects and their discovered prerequisites; `--no-dependencies` intentionally reuses installed prerequisites and does not independently verify them.

Before each configure, cached SDK package-directory variables and their variants are cleared so that an old build-tree package does not silently override current prefixes. This includes retired `ii*`, `LVRS*` and `LVUI*` package names. Third-party package cache entries outside those namespaces are left alone. A project-specific `--define` is applied afterwards and may deliberately select another package. These cache resets do not detect every absolute library path, runtime path, or dependency hidden outside CMake's package-directory variables.

An existing build may also contain compiler implicit include directories captured from an older `CPATH` or `CPLUS_INCLUDE_PATH`. Clearing the current environment alone does not remove those cached entries: CMake may then omit a required explicit SDK include even though the file exists. Inspect `build/CMakeFiles/<cmake-version>/CMakeCXXCompiler.cmake` and the failing target's generated flags when diagnosing a missing installed header. Preserve the generated file before any targeted repair or compiler re-detection, and verify the actual include flags after reconfiguration. Never assume every SDK-looking entry is accidental: a toolchain may deliberately provide one. This installer does not automatically rewrite compiler detection files.
