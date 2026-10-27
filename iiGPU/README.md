# iiGPU

iiGPU is a Qt-free C++23 GPU command and compute foundation for iiSDK. It owns
native adapters, queues, shared/device buffers, reusable compute pipelines,
command recording, explicit memory dependencies and observable completion.
Metal serves Apple platforms; Vulkan serves native desktop and Android targets.
An optional `iiGPU::Runtime` adapter delivers completion on an iiSDKCore 0.20+ EventLoop.

WebGPU device negotiation, resources, WGSL and standard WebGPU C access are
integrated as the optional `iiGPU::WebGPU` component. The existing
`iiWebGPU::iiWebGPU` package name is a compatibility alias for that component.
The base target does not link Dawn or import WebGPU types. `IIGPU_WEBGPU` accepts
`AUTO`, `ON`, or `OFF`; `ON` requires a target-native Dawn distribution in
`resources/dawn/<target>` or the explicitly configured `IIGPU_DAWN_ROOT`. For
example, macOS arm64 and Windows x64 resources live in different directories;
the configured compiler target selects the directory. The pinned
`tools/fetch_dawn.py` prepares that resource through iiFileProvider. No GPU
backend or shader format is silently substituted for another.

The base library does not depend on iiGUI, iiDecision, iiSDKCore or an interpreter.
iiDecision uses it for exact integer evidence filtering. Graphics widgets,
presentation, model runtimes and probability calculations remain in their own
modules. `native_access` permits platform extensions through borrowed native
handles with a strictly bounded lifetime.

## Build and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DiiSDKCore_DIR="$HOME/.local/SDK/iiSDKCore/lib/cmake/iiSDKCore" \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/SDK/iiGPU"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build
```

`IIGPU_METAL` and `IIGPU_VULKAN` accept `AUTO`, `ON` or `OFF`. `ON` fails if the
SDK is unavailable; a backend disabled at build time returns `unsupported` at
runtime. Vulkan needs the platform's native loader and driver; Apple Vulkan
validation uses an externally installed MoltenVK. Set `Vulkan_INCLUDE_DIR` and
`Vulkan_LIBRARY` if discovery requires explicit paths. No vendor SDK is fetched
or bundled automatically. `IIGPU_BUILD_RUNTIME=OFF` removes the Core adapter.
`install.sh` runs the same build/test/install sequence.

To build the optional WebGPU component, prepare the locked host distribution
first, then configure it explicitly:

```sh
python3 -B tools/fetch_dawn.py
cmake -S . -B build -DIIGPU_WEBGPU=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

For cross compilation, `--platform windows-x64` or another key in the lock file
fetches the target's distribution into its own directory. Supply a matching
target toolchain. Unsupported target architectures and universal binaries
require an explicitly prepared `IIGPU_DAWN_ROOT`.

## Consume the installed package

```cmake
find_package(iiGPU 0.1 CONFIG REQUIRED)
target_link_libraries(MyCompute PRIVATE iiGPU::iiGPU)
# Request only when EventLoop completion integration is needed:
find_package(iiGPU 0.1 CONFIG REQUIRED COMPONENTS Runtime)
target_link_libraries(MyApp PRIVATE iiGPU::Runtime)
# Request WebGPU only when used; the compatibility iiWebGPU package also works.
find_package(iiGPU 0.1 CONFIG REQUIRED COMPONENTS WebGPU)
target_link_libraries(MyWebGPUApp PRIVATE iiGPU::WebGPU)
```

The [acceleration example](example/Acceleration/main.cpp) uploads 257 integers,
dispatches a shader, waits for completion and checks every output. Run
`build/iiGPUAcceleration metal` or `build/iiGPUAcceleration vulkan`. It requires
a physical GPU by default and does not silently run a CPU implementation.

See the Korean [API and lifetime contract](docs/Contracts.md),
[platform and verification record](docs/Verification.md), and
[agent document index](docs/agent-index.json).

WebGPU source, migration and verification: [contract](docs/WebGPUContract.md), [verification](docs/WebGPUVerification.md).

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
