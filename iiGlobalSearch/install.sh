#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix=${1:-"$root/build/install"}
dependencies=${CMAKE_PREFIX_PATH:-"$HOME/.local/SDK/iiSDKCore;$HOME/.local/SDK/iiJson;$HOME/.local/SDK/iiFileProvider;$HOME/.local/SDK/iiSocietyContainer;$HOME/.local/SDK/iiNetwork"}
if [ -z "${OPENSSL_ROOT_DIR:-}" ] && [ -f "$HOME/.local/SDK/OpenSSL-3.5.9/lib/libssl.a" ]; then
    OPENSSL_ROOT_DIR="$HOME/.local/SDK/OpenSSL-3.5.9"
    export OPENSSL_ROOT_DIR
fi
cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$dependencies" -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$root/build" --parallel
ctest --test-dir "$root/build" --output-on-failure
cmake --install "$root/build"
