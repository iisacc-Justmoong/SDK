# iiCompression

iiCompression provides bounded, in-memory gzip operations for C++23
applications. It has no Qt or other iiSDK dependency; its implementation uses
zlib. Applications can compose it with independent format and storage SDKs.
See [the gzip contract](docs/Gzip.md) and [dependency notice](NOTICE.md).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The default build includes the library and its tests. To build only the library,
configure with `-DBUILD_TESTING=OFF`.

The installed CMake package exports `iiCompression::iiCompression` and the
public header `<iiCompression/Gzip.h>`. Installation to a local prefix is an
explicit step; a stage inside `build/` does not update that prefix. A fresh
configuration defaults to `~/.local` when `HOME` is available and accepts an
explicit `CMAKE_INSTALL_PREFIX`, including when invoked through `install.sh`.
The gzip interface and implementation live together directly under `src/`;
CMake generates a build-only forwarding header to preserve the public include.

## License

SPDX-License-Identifier: Apache-2.0

This module is part of the iiSDK tree and is licensed under the
[Apache License, Version 2.0](LICENSE). The copyright notice is
`Copyright (c) 2026 iisacc`. [NOTICE.md](NOTICE.md) records that notice and
the separate zlib license of the external zlib dependency.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
