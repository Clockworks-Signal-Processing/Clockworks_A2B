# toolchain-pi.cmake
# CMake cross-compilation toolchain for 64-bit Raspberry Pi OS (arm64)
# Target: any 64-bit Pi (Zero 2 W, 3, 4, 5, CM3/4) running 64-bit Raspberry Pi OS
# Host:   x86_64 Debian (WSL) with crossbuild-essential-arm64
#
# Build on a Debian release that matches the Pi's (same glibc): Debian 13
# "trixie" for Raspberry Pi OS based on trixie.  See README.md.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compiler binaries installed by crossbuild-essential-arm64.
# Fail early with the package name rather than CMake's generic
# "compiler not found" error.
find_program(A2B_ARM64_GCC aarch64-linux-gnu-gcc)
find_program(A2B_ARM64_GXX aarch64-linux-gnu-g++)
if(NOT A2B_ARM64_GCC OR NOT A2B_ARM64_GXX)
    message(FATAL_ERROR
        "aarch64-linux-gnu-gcc/g++ not found on PATH.\n"
        "Install the arm64 cross toolchain (Debian):\n"
        "  sudo apt-get install crossbuild-essential-arm64 cmake pkg-config\n"
        "then delete the build directory and configure again.")
endif()
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Debian multiarch: arm64 libraries (libgpiod-dev:arm64 etc.) install to
# /usr/lib/aarch64-linux-gnu, found through pkg-config's arm64 directory.
set(A2B_MULTIARCH_TRIPLET aarch64-linux-gnu)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")

# Search the host for build tools only; libraries and headers come from the
# cross toolchain's sysroot and the arm64 multiarch paths.
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ARMv8-A baseline: runs on every 64-bit Pi (Cortex-A53 in the Zero 2 W / Pi 3
# up to Cortex-A76 in the Pi 5).
set(CMAKE_C_FLAGS_INIT   "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a")
