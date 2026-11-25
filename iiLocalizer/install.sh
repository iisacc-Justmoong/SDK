#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
prefix="${IILOCALIZER_PREFIX:-${HOME}/.local/SDK/iiLocalizer}"
cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$root/build" --parallel
ctest --test-dir "$root/build" --output-on-failure
cmake --install "$root/build"
