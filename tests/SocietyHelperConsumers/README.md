# Consolidated Society Helper application consumers

This integration project compiles the actual Society account wrapper and
compatibility headers, Society's encrypted group-state regressions, and
Dreamscapes' migrated generation controller against installed iiSocietyHelper
0.9.0. Only the unified Helper target is linked for its account and host APIs.

The executables are ordinary native tests. They do not create an `.app`,
reconfigure an existing product build, deploy a product or restart a process.
Fixtures and compiler outputs belong beneath `SDK/build/`.

```sh
cmake -S SDK/tests/SocietyHelperConsumers -B SDK/build/society-helper-consumers \
    -DiiSocietyHelper_DIR=/path/to/iiSocietyHelper/lib/cmake/iiSocietyHelper \
    -DCMAKE_PREFIX_PATH='/path/to/qt;/path/to/dependency-prefixes'
cmake --build SDK/build/society-helper-consumers --parallel 4
ctest --test-dir SDK/build/society-helper-consumers --output-on-failure
```

The Dreamscapes cases use injected inference and a synthetic model. They verify
real application state and publication behavior, not model quality or device
inference. The SDK's own session tests separately prove authenticated LAN TLS,
relay and same-device native drive access.
