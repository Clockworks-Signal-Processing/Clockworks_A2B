#!/usr/bin/env bash
# Builds the a2bcpp unit tests with the host g++ and runs them with CTest (Linux, WSL).
# From the repository root:
#   a2bcpp/tools/run_tests.sh          build in build/a2bcpp-linux and run the tests
#   a2bcpp/tools/run_tests.sh clean    remove build/a2bcpp-linux
# Needs cmake, ninja-build (or make) and g++. On Windows: wsl -d Debian bash a2bcpp/tools/run_tests.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/build/a2bcpp-linux"

if [[ "${1:-test}" == "clean" ]]; then
  rm -rf "$build"
  echo "Removed $build"
  exit 0
fi

generator=()
if command -v ninja > /dev/null; then generator=(-G Ninja); fi

cmake -S "$root/a2bcpp" -B "$build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
