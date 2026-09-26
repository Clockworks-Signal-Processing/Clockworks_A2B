#!/usr/bin/env bash
# Cross-build app/linux_rpi for 64-bit Raspberry Pi OS, in the Debian WSL distro.
# Run from the repository root (the VS Code "Linux arm64:" tasks do this):
#   app/linux_rpi/build.sh Debug     build in ~/a2b-build/Debug, copy to app/linux_rpi/build/
#   app/linux_rpi/build.sh Release   build in ~/a2b-build/Release, copy to app/linux_rpi/build/release/
#   app/linux_rpi/build.sh clean     remove both build folders and app/linux_rpi/build/
# The build folders live in the Linux home directory because CMake can't set file
# permissions on the Windows drive under WSL1. Compiler messages have /mnt/d/ paths
# rewritten to d:/ so VS Code can open them.
set -o pipefail

TYPE=${1:-Debug}
OUT=app/linux_rpi/build

case "$TYPE" in
    clean)
        rm -rf "$HOME/a2b-build/Debug" "$HOME/a2b-build/Release" "$OUT"
        echo "Removed ~/a2b-build/Debug, ~/a2b-build/Release and $OUT"
        exit 0
        ;;
    Debug)   DEST=$OUT ;;
    Release) DEST=$OUT/release ;;
    *)
        echo "usage: $0 Debug|Release|clean" >&2
        exit 2
        ;;
esac

B=$HOME/a2b-build/$TYPE
{
    cmake -S app/linux_rpi -B "$B" -DCMAKE_TOOLCHAIN_FILE="$PWD/app/linux_rpi/toolchain-pi.cmake" -DCMAKE_BUILD_TYPE="$TYPE" &&
    cmake --build "$B" -j4 &&
    mkdir -p "$DEST" &&
    cp "$B/a2b_pnp" "$DEST/" &&
    echo "Copied to $DEST/a2b_pnp"
} 2>&1 | sed -u 's#/mnt/\([a-z]\)/#\1:/#g'
