#!/usr/bin/env bash
set -euo pipefail

root="${HOME}/vxwm/shell"
build="${root}/.screenshot-build"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --target screenshot-tool --parallel
