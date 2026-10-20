# SDK performance review

Performance work preserves observable output, ordering, cancellation, ownership,
durability and security boundaries. Source-pattern findings are review leads,
not proof that a loop, lock, allocation or model reset is unnecessary. Independent
work must remain parallel where its API permits it; serialization belongs at
shared-state and non-reentrant backend boundaries.

## Reproducible source inventory

`tools/performance_audit.py` inventories repository-owned C/C++/Objective-C,
QML/JavaScript/TypeScript, Python, Rust and Swift declarations. It records source
paths, line locations, hashes, object/member/function declarations, callbacks,
loop/call structure and review leads. Qt properties are extracted separately
from annotations; QML IDs scope properties and functions. Vendored dependencies,
generated builds (including Svelte package output), tests and development tools are distinguished from runtime
sources. Svelte script blocks are inspected; template markup is not a complete
semantic component analysis.
The top-level Windows build/verification toolkit is cataloged as platform
tooling rather than counted as another runtime SDK module.

Use a Python 3.12 development environment with the pinned parser requirements.
The QML grammar is upstream `yuja/tree-sitter-qmljs` revision
`de96ed62abded51fcdfcbeaaa120e0dd0d20c697`. Its Python binding is a development
dependency only. The 2026-10-20 run retains its bootstrap, revision and test logs
under `build/performance-20261020/tools/`.

```sh
python -m pip install -r tools/performance-audit-requirements.txt
python tools/performance_audit.py --sdk "$PWD" --output build/performance-audit
python tests/test_performance_audit.py
```

An optional `--inventory` pins file coverage. `--source-archive` reads the same
module/path entries from a saved ZIP for before/after analysis. Syntax recovery
locations are explicit in `coverage.json`. This is a source index, not a C++
compiler/preprocessor, full cross-language call graph, concurrency proof or
automatic performance-bug classifier. Conditional compilation, Objective-C
fallback selectors and recovered syntax require contract inspection.

## Measurement and validation

`benchmarks/performance` builds C++23 consumer executables against installed
packages. The default probes have no Qt dependency. Optional legacy probes
exercise the existing Qt-based LLM catalog and LVRS models. The application
runtime gains no Python, Tree-sitter or interpreter dependency.

Record compiler/optimization flags, platform, input sizes, repetitions and result
checksums. Compare paired runs after build activity, retain raw samples, and
report medians as microbenchmarks. A capacity-sensitive dispatch probe, a filter
kernel or a catalog-validation probe does not establish whole-application FPS,
launch time or memory savings. Persistent model indexes and unchanged-file
versions require behavioral tests independently of throughput.

Changed modules require their relevant regression suites and staged package
consumers. Keep source/build/test evidence separate from global installation,
active-process adoption and measurements in a real consuming application.
Do not overwrite concurrent work or attribute another task's edits to this
review. The dated report records those boundaries and retained initial failures.
