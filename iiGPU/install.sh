#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake -S "$task_root" -B "$task_root/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DIIGPU_BUILD_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX="${IIGPU_INSTALL_PREFIX:-$HOME/.local/SDK/iiGPU}" "$@"
cmake --build "$task_root/build" --parallel 4
ctest --test-dir "$task_root/build" --output-on-failure
cmake --install "$task_root/build"
