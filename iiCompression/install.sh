#!/bin/sh
set -eu
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "$@"
cmake --build build
ctest --test-dir build --output-on-failure --no-tests=error
cmake --install build
