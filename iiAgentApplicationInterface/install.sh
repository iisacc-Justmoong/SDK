#!/bin/sh
# Build and test in build/, then install to an explicit prefix or build/install.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix=${1:-${INSTALL_PREFIX:-"$root/build/install"}}
config=${IIAGENTAPPLICATIONINTERFACE_BUILD_CONFIG:-}
if [ -z "$config" ] && [ -f "$root/build/CMakeCache.txt" ]; then
    config=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$root/build/CMakeCache.txt" | head -n 1)
fi
config=${config:-Release}
cmake -S "$root" -B "$root/build" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE="$config"
cmake --build "$root/build" --config "$config"
ctest --test-dir "$root/build" -C "$config" --output-on-failure
cmake --install "$root/build" --config "$config" --prefix "$prefix"
