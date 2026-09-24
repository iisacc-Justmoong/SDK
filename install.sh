#!/usr/bin/env bash
set -euo pipefail
sdk_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 -B "${sdk_directory}/install_all.py" "$@"
