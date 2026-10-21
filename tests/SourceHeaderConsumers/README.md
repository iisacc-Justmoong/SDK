# Source header package consumers

This C++23 project consumes the installed public targets of every package in
the source-header migration fixture. Each public header is compiled in its own
translation unit. No source directory is added to a consumer's include path.
An executable per package also checks that its runtime dependencies load.

Configure with `cmake -S tests/SourceHeaderConsumers -B tests/SourceHeaderConsumers/build`
from the SDK directory, providing staged package prefixes in `CMAKE_PREFIX_PATH`.
Set `HEADER_STAGE_ROOT` to require package discovery below that staging root.
Then build and run CTest in that `build/` directory. Native source-only adapters
are covered separately by `tests/NativeHeaderLayout`.
