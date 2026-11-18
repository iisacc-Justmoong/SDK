# Consolidated Society Helper application consumers

This integration project compiles the actual Society account wrapper and
compatibility headers, Society's actual network/sync/host controller, encrypted
group-state and file-projection regressions, and
Dreamscapes' migrated generation controller against installed iiSocietyHelper
0.10.0. Core contains account/client access, Sync and Generation::Remote;
desktop generation explicitly selects `iiSocietyHelper::GenerationHost`.
PhotoLibrary is a lower-level dependency of Society's network controller.
The Society sources and include paths follow `Product/Society/App/Society/`,
including the account controller and its two broker implementations.

The executables are ordinary native tests. They do not create an `.app`,
reconfigure an existing product build, deploy a product or restart a process.
Compiler outputs for this CMake project belong beneath `SDK/tests/SocietyHelperConsumers/build/`.

The Society group-state and file-projection consumers use the product's CTest
runner without linking QtTest. Their disposable fixtures are created under
`Product/Society/resources/`. This consumer project no longer requests QtTest.

```sh
cmake -S SDK/tests/SocietyHelperConsumers -B SDK/tests/SocietyHelperConsumers/build \
    -DiiSocietyHelper_DIR=/path/to/iiSocietyHelper/lib/cmake/iiSocietyHelper \
    -DCMAKE_PREFIX_PATH='/path/to/qt;/path/to/dependency-prefixes'
cmake --build SDK/tests/SocietyHelperConsumers/build --parallel 4
ctest --test-dir SDK/tests/SocietyHelperConsumers/build --output-on-failure
```

The Dreamscapes cases use injected inference and a synthetic model. They verify
real application state and publication behavior, not model quality or device
inference. Each case uses its own temporary directory in this project's
`resources/`, including a separate container path and model-preferences file.
The three `SocietyHelperConsumers.dreamscapes_host.*` CTests each launch their
own process and select one case, so their lifetimes and timeouts are independent.
The preview case records pixels and publication state in its signal handler;
the synchronization case records completion notifications. Assertions run
after event delivery has returned. A source-compatible installed iiLocalDiffusion
package is required to compile the current Dreamscapes generation sources.
The SDK's own session tests separately prove authenticated LAN TLS,
relay and same-device native drive access.
