# Native SDK performance probes

These are C++23 installed-package consumers. Build in `build/` with a Release
configuration and a prefix path containing iiSDKCore, iiColorManager,
iiPaintEngine, iiGraphicAdjust and iiFileProviderNative:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<staged prefixes>"
cmake --build build
ctest --test-dir build --output-on-failure
python ../../tools/run_performance_benchmarks.py --build-dir build --output build/results.json --samples 5
```

`SDK_PERFORMANCE_LEGACY=ON` additionally requires the existing iiLocalLLM, LVRS
and Qt packages. It adds nested-catalog, hierarchy and table probes. The hierarchy
probe deliberately compiles the repository's private projection implementation;
the other probes call library APIs. Each output line contains a case name,
elapsed milliseconds and a result checksum. Timing includes result consumption.
The smoke tests establish that calls execute successfully; they do not impose
machine-specific timing thresholds.

For the dated review, a saved source ZIP supplies before-change implementations
and a separate baseline Core build. `SDK_PERFORMANCE_BASELINE` and
`SDK_PERFORMANCE_BASELINE_CORE` enable those macOS development oracle targets.
They are never installed as SDK packages. Their public signatures/layouts are
unchanged by this refactor. Keep the source hashes and all raw paired samples.

The probes cover sparse dispatch/idle deadline lookup at two capacities, ASCII
and mixed Korean/emoji UTF-8, picker rail rendering, ARGB box blur, Gaussian
workspace reuse, color halftone, author metadata parsing, nested parameter
validation and LVRS projections. Pixel/ordering/selection/serialization/security
correctness belongs to the module regression suites as well as checksum parity.
