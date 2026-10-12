#!/usr/bin/env bash
# Cross-builds the bench console (a2b_bench) for 64-bit Raspberry Pi OS, in the Debian WSL
# distro. Run from the repository root (the VS Code "Pi bench:" tasks do this):
#   a2bcpp/tools/build_pi.sh Debug     build in ~/a2b-cpp-build/a2bcpp-Debug, copy to build/a2b-bench-pi/Debug/
#   a2bcpp/tools/build_pi.sh Release   build in ~/a2b-cpp-build/a2bcpp-Release, copy to build/a2b-bench-pi/Release/
#   a2bcpp/tools/build_pi.sh clean     remove the build folders and build/a2b-bench-pi
# The build folders live in the Linux home directory because CMake can't set file
# permissions on the Windows drive under WSL1. Needs
# crossbuild-essential-arm64 and cmake (a2bcpp/docs/setup_pi.md). Compiler messages have
# /mnt/d/ paths rewritten to d:/ so VS Code can open them.
set -o pipefail

TYPE=${1:-Debug}
OUT=build/a2b-bench-pi

case "$TYPE" in
    clean)
        rm -rf "$HOME/a2b-cpp-build/a2bcpp-Debug" "$HOME/a2b-cpp-build/a2bcpp-Release" "$OUT"
        echo "Removed ~/a2b-cpp-build/a2bcpp-Debug and a2bcpp-Release, and $OUT"
        exit 0
        ;;
    Debug | Release) ;;
    *)
        echo "usage: $0 Debug|Release|clean" >&2
        exit 2
        ;;
esac

B=$HOME/a2b-cpp-build/a2bcpp-$TYPE
{
    cmake -S a2bcpp -B "$B" -DCMAKE_TOOLCHAIN_FILE="$PWD/a2bcpp/tools/toolchain-pi.cmake" \
          -DCMAKE_BUILD_TYPE="$TYPE" -DA2B_CPP_TESTS=OFF &&
    cmake --build "$B" --target a2b_bench -j4 &&
    mkdir -p "$OUT/$TYPE" &&
    cp "$B/examples/bench/a2b_bench" "$OUT/$TYPE/" &&
    echo "Copied to $OUT/$TYPE/a2b_bench"
} 2>&1 | sed -u 's#/mnt/\([a-z]\)/#\1:/#g'
