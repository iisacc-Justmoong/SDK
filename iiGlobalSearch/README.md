# iiGlobalSearch

File snapshot indexing is provided by `iiFileProvider::Catalog` through
`FileSearchIndex`; iiGlobalSearch owns Society traversal, access validation,
asynchronous lifecycle and result revalidation. Web search remains independent.

iiGlobalSearch builds a searchable, in-memory snapshot of iiFileProvider
classification and presentation information in one `.societycontainer`
directory package. A reusable iiSDKCore worker indexes asynchronously; callers
can query partial results, observe progress, cancel, and explicitly refresh.
Provider catalogs and portable companions are read through their public APIs.
User payloads and provider indexes are never modified by this SDK.

The separate web search API starts each queued task with a new crawl frontier,
visits live HTTP(S) pages through iiNetwork, follows links subject to robots
rules and ranks the resulting page content in memory. It does not query a
third-party search database or build a persistent search index. It returns
metadata, structured data, links and a flat DOM. An isolated Chromium session
renders JavaScript by default; an application can provide another renderer.
Coverage is bounded by the selected seeds and crawl limits, and is not the
whole web or Google's ranking. See [web search](docs/WebSearch.md).

Local `searchAnswer()` and web `WebTask::answer()` return recursive, structured
in-memory objects containing the data actually found. Values support integers,
floats, strings, heterogeneous lists, images, video, audio, numeric vectors and
vector graphics, spreadsheets, graphs and scenes. Remote and local media use
references until explicitly loaded into memory by the bounded asynchronous
`MediaLoader`. HTML tables and inline SVG can already be present in the page;
external spreadsheet and scene documents are references, not decoded models.
Existing vector-returning methods remain available at source level; C++ consumers
must rebuild for the changed public ABI. See [structured answers](docs/Answers.md).

The local and web APIs are separate CMake components. Local search uses
`iiGlobalSearch::iiGlobalSearch`; request the `Web` component and link
`iiGlobalSearch::Web` for network search. Both components and the common answer
library require iiSDKCore 0.18 or later. The web component requires iiNetwork
0.5 or later, iiJson, iiFileProviderNative, libxml2,
libcurl and OpenSSL 3 or later. JavaScript rendering additionally needs a
compatible Chromium executable. Set `OPENSSL_ROOT_DIR` when CMake cannot locate
OpenSSL. Local search retains its iiSocietyContainer and SQLite/FTS5 dependencies.

The index is scoped to visible, non-redirected files under the selected package.
It includes catalog entries (including unpublished metadata), independently
transported classification companions, and valid presentation companions. It
does not index arbitrary files without provider information. Presentation
verification reads a bounded sample of the source; classification alone does
not read its payload. Hidden directories and provider control files are skipped.
Results are a snapshot and should be refreshed after changes.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/.local/SDK/iiSDKCore;$HOME/.local/SDK/iiJson;$HOME/.local/SDK/iiFileProvider;$HOME/.local/SDK/iiSocietyContainer;$HOME/.local/SDK/iiNetwork"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/ii-global-search index /absolute/Library.societycontainer "campaign portrait" --limit 50
build/ii-global-search web "creative tools" --seed https://example.com/ --max-pages 10 --depth 2
```

The CLI emits JSON and waits for the background indexing job. Applications can
link `iiGlobalSearch::iiGlobalSearch` and use `iiGlobalSearch::Index` without Qt;
see [the indexing contract](docs/Indexing.md) and [the example](example/search.cpp).
`./install.sh` stages the package to `build/install` by default. A consumer must
explicitly start a refresh; this library does not start a service or change an
already running application.

Sources and adjacent headers are grouped under `src/Answers`, `src/Local`, `src/Web`, and `src/CLI`; installed public include paths remain stable. See [source layout](docs/SourceLayout.md).

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
