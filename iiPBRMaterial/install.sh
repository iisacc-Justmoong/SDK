#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix=${1:-"$HOME/.local/SDK/iiPBRMaterial"}
cmake -S "$project_dir" -B "$project_dir/build" -DBUILD_TESTING=ON
cmake --build "$project_dir/build" --parallel
ctest --test-dir "$project_dir/build" --output-on-failure
cmake --install "$project_dir/build" --prefix "$prefix"
