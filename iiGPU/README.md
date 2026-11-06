# iiGPU

iiGPU is a Qt-free C++23 GPU command and compute foundation for iiSDK. It owns
native adapters, queues, shared/device buffers, reusable compute pipelines,
command recording, explicit memory dependencies and observable completion.
Metal serves Apple platforms; Vulkan serves native desktop and Android targets.
`ii::gpu::Session` retains validated device-specific dispatch configurations.
Prepare a pipeline and bindings once, then use the returned token to record
subsequent dispatches with new constants without revalidating those fixed inputs.
Sessions are bounded, explicit, and independent of iiSDKCore; recorded or
submitted work retains the resources it needs after a session closes.
The optional `iiGPU::Runtime` component provides a bounded, persistent
`DeviceThread` for GPU command preparation and submission, separate from CPU
worker pools. Applications explicitly post GPU work to it and can attach its
lifetime to an independently linked iiSDKCore Runtime. Direct calls to `Device`
continue to execute on the caller's thread.
The component also provides a bounded completion service
that waits for native GPU completion without periodic polling. Its fixed worker
pool is independent of the application's CPU workers. It also retains the
caller-supplied scheduler observer for existing consumers. An application can
post completion to an iiSDKCore EventLoop or another executor without making
iiGPU depend on either implementation.
The base API lives under `src/`, with platform backends in `src/Backends/`,
completion observation in `src/Runtime/`, and WebGPU in `src/WebGPUComponent/`. Existing
`<iiGPU/...>` and `<iiWebGPU/...>` public include names remain unchanged.

WebGPU device negotiation, resources, WGSL and standard WebGPU C access are
integrated as the optional `iiGPU::WebGPU` component. The existing
`iiWebGPU::iiWebGPU` package name is a compatibility alias for that component.
The base target does not link Dawn or import WebGPU types. `IIGPU_WEBGPU` accepts
`AUTO`, `ON`, or `OFF`; `ON` requires a target-native Dawn distribution in
`resources/dawn/<target>` or the explicitly configured `IIGPU_DAWN_ROOT`. For
example, macOS arm64 and Windows x64 resources live in different directories;
the configured compiler target selects the directory. The pinned
The separate workspace utility `../tools/fetch_gpu_dawn.py` supplies
iiFileProvider to the resource preparation API. No GPU
backend or shader format is silently substituted for another.

The base library does not depend on iiGUI, iiDecision, iiSDKCore or an interpreter.
iiDecision uses it for exact integer evidence filtering. Graphics widgets,
presentation, model runtimes and probability calculations remain in their own
modules. `native_access` permits platform extensions through borrowed native
handles with a strictly bounded lifetime.

## Build and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
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
or bundled automatically. `IIGPU_BUILD_RUNTIME=OFF` removes the optional
completion observer.
`install.sh` builds and tests the module. Tests are enabled by default; use
`-DBUILD_TESTING=OFF` for a library-only build. Examples are opt-in via
`-DIIGPU_BUILD_EXAMPLES=ON`. Use a fresh `build/` when returning to the
default configuration.

WebGPU bootstrap fixtures and temporary relocated packages are kept under
`resources/.state/` and removed after verification. Compiled consumer test
outputs use `build/` and are also removed afterwards. To check package
relocation without running a hardware GPU, pass `--compile-only` to
`tests/webgpu/relocation.py` after its usual source and build arguments.
These fixture tests use the installed iiFileProvider Python adapter; set
`IIFILEPROVIDER_PYTHON_DIRECTORY` if it is installed elsewhere. The iiGPU
library itself does not require that adapter.

To build the optional WebGPU component, prepare the locked host distribution
first, then configure it explicitly:

```sh
python3 -B ../tools/fetch_gpu_dawn.py
cmake -S . -B build -DIIGPU_WEBGPU=ON -DBUILD_TESTING=ON
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
# Request only when scheduled completion observation is needed:
find_package(iiGPU 0.1 CONFIG REQUIRED COMPONENTS Runtime)
target_link_libraries(MyApp PRIVATE iiGPU::Runtime)
# If the application uses Core, it finds and links it independently:
find_package(iiSDKCore 0.20 CONFIG REQUIRED COMPONENTS Runtime)
target_link_libraries(MyApp PRIVATE iiSDKCore::Runtime)
# Request WebGPU only when used; the compatibility iiWebGPU package also works.
find_package(iiGPU 0.1 CONFIG REQUIRED COMPONENTS WebGPU)
target_link_libraries(MyWebGPUApp PRIVATE iiGPU::WebGPU)
```

For event-driven GPU completion, use `CompletionService`. A caller that
deliberately requires periodic checks can use
`CompletionWatch::create_polling(loop, submission, callback, interval)` or
`observe_polling` with an explicit positive interval. The older `create` and
`observe` entry points return `unsupported` and never schedule a timer, including
calls from existing binaries that used their formerly implicit default interval.
Cancellation stops observing; it does not cancel submitted GPU work.

`<iiGPU/DeviceThread.h>` provides `DeviceThread::post(work)`. A device thread
executes accepted work in FIFO order on one fixed thread, waits without polling
when idle, and drains work and captures on close. It does not wait for GPU
completion by itself; submit results must remain alive until observed.
`<iiGPU/CompletionService.h>` provides `CompletionService::watch(submission, callback)`,
which waits on an explicitly owned,
bounded pool and invokes the callback once after native completion. Canceling a
ticket suppresses an unclaimed callback; it does not cancel GPU execution.
Close the service outside its callbacks and host UI thread: shutdown drains
submitted GPU work and can block. The independent consumer fixture at
`tests/consumer/acceleration.cpp` composes Core's CPU workers, an EventLoop and
iiGPU. Its default test uses a controlled wait; `--hardware` is an explicit
physical-GPU compute run, not part of that test.

The [acceleration example](example/Acceleration/main.cpp) uploads 257 integers,
dispatches a shader, waits for completion and checks every output. Configure
with `-DIIGPU_BUILD_EXAMPLES=ON` to build it. Run
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
