#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix=${IISDK_PREFIX:-"$HOME/.local/SDK/iiJob"}
cmake -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$project_dir/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$project_dir/build" --output-on-failure
cmake --install "$project_dir/build" --prefix "$prefix"
