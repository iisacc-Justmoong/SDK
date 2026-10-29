# iiCompression

iiCompression provides bounded, in-memory gzip operations for C++23
applications. It has no Qt or other iiSDK dependency; its implementation uses
zlib. Applications can compose it with independent format and storage SDKs.
See [the gzip contract](docs/Gzip.md) and [dependency notice](NOTICE.md).

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The installed CMake package exports `iiCompression::iiCompression` and the
public header `<iiCompression/Gzip.h>`. Installation to a local prefix is an
explicit step; a stage inside `build/` does not update that prefix. A fresh
configuration defaults to `~/.local` when `HOME` is available and accepts an
explicit `CMAKE_INSTALL_PREFIX`, including when invoked through `install.sh`.
The gzip interface and implementation live together directly under `src/`;
CMake generates a build-only forwarding header to preserve the public include.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
