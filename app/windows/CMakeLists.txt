cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# A2B PnP — 64-bit Windows console app (ADI USBi adapter through libusb)
#
# A standalone project, like app/linux_rpi (the root CMakeLists.txt belongs to
# the RP2040 / Pico SDK build). Built with MSYS2 UCRT64: gcc, cmake, ninja,
# pkgconf and libusb from <msys64>/ucrt64. From the repository root:
#   app\windows\build.ps1 Debug        (or Release, or clean; see README.md)
# or, with <msys64>\ucrt64\bin first on PATH:
#   cmake -S app/windows -B app/windows/build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build app/windows/build/Debug
# ---------------------------------------------------------------------------
project(a2b_pnp
    LANGUAGES C CXX
    VERSION 1.0.0
)

if(NOT WIN32)
    message(FATAL_ERROR "app/windows builds the Windows console app; use app/linux_rpi for Linux.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "This is a ${CMAKE_SIZEOF_VOID_P}-byte-pointer compiler; the Windows build is meant "
                    "to be 64-bit (MSYS2 UCRT64: <msys64>/ucrt64/bin first on PATH).")
endif()

# ---------------------------------------------------------------------------
# Build types
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# compile_commands.json for VS Code IntelliSense (the "Windows" configuration)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GNU C17, as the RP2040 (gcc 13) and Linux (gcc 14) compilers use by default. gcc 15+
# defaults to C23, where bool is a keyword and the stack's `typedef a2b_Bool bool`
# (a2bstack-protobuf/inc/pb_syshdr.h) no longer compiles.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# ---------------------------------------------------------------------------
# Root paths — relative to this CMakeLists.txt (app/windows/)
# ---------------------------------------------------------------------------
set(PROJECT_ROOT        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(A2B_STACK_ROOT      ${PROJECT_ROOT}/a2bstack)
set(A2B_PNP_ROOT        ${PROJECT_ROOT}/a2bpnp)
set(A2B_COMMCH_ROOT     ${PROJECT_ROOT}/a2bcommchannel)
set(APP_COMMON_ROOT     ${PROJECT_ROOT}/app/common)   # shared cmd_parse, cmd_queue, etc.
set(APP_TEST_ROOT       ${PROJECT_ROOT}/app/test)     # cJSON (used by a2bapp_common.c)
set(PAL_ROOT            ${CMAKE_CURRENT_SOURCE_DIR}/a2bstack-pal)

# ---------------------------------------------------------------------------
# libusb-1.0 (the USBi adapter), found through pkg-config.
# Install once, in an MSYS2 UCRT64 shell:  pacman -S mingw-w64-ucrt-x86_64-libusb
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB libusb-1.0)
if(NOT LIBUSB_FOUND)
    message(FATAL_ERROR
        "libusb-1.0 not found. In an MSYS2 UCRT64 shell:\n"
        "  pacman -S mingw-w64-ucrt-x86_64-libusb\n"
        "and build with <msys64>\\ucrt64\\bin first on PATH (app\\windows\\build.ps1 does this).")
endif()
message(STATUS "libusb ${LIBUSB_VERSION}: ${LIBUSB_LIBRARY_DIRS}")

# The DLL is copied next to a2b_pnp.exe so the program runs outside MSYS2.
find_file(LIBUSB_DLL libusb-1.0.dll
    HINTS ${LIBUSB_PREFIX}/bin ${LIBUSB_LIBRARY_DIRS}/../bin
    NO_DEFAULT_PATH)
if(NOT LIBUSB_DLL)
    message(FATAL_ERROR "libusb-1.0.dll not found next to ${LIBUSB_LIBRARY_DIRS}")
endif()

# ---------------------------------------------------------------------------
# Compile definitions (as for the RP2040 and Linux apps)
# ---------------------------------------------------------------------------
add_compile_definitions(
    A2B_STATIC_PLUGIN
    A2B_LINK_STATIC=1
    PB_FIELD_16BIT=1
    _CRT_SECURE_NO_WARNINGS
)

# ---------------------------------------------------------------------------
# Sources
# Not built: adi_a2b_multimain.c (ADI's multi-main-node example, all #if 0) and
# usb-driver-sel.c (an unused driver-switching experiment).
# ---------------------------------------------------------------------------
set(SRC_PLATFORM
    ${CMAKE_CURRENT_SOURCE_DIR}/a2bapp_win32.c        # main()
    ${CMAKE_CURRENT_SOURCE_DIR}/a2bapp_server.c       # console, PnP and output threads
    ${CMAKE_CURRENT_SOURCE_DIR}/12_a2b_busconfig.c    # compiled-in bus configuration (configType 0)
    ${PAL_ROOT}/adi_a2b_pal.c
    ${PAL_ROOT}/pal_i2c_usbi.c
    ${PAL_ROOT}/pal_timer.c
)

file(GLOB SRC_COMMON ${APP_COMMON_ROOT}/*.c)
file(GLOB SRC_TEST   ${APP_TEST_ROOT}/*.c ${APP_TEST_ROOT}/cJSON-master/cJSON.c)

file(GLOB SRC_PNP
    ${A2B_PNP_ROOT}/*.c
    ${A2B_PNP_ROOT}/*.cpp
)

file(GLOB SRC_STACK
    ${A2B_STACK_ROOT}/a2bstack/src/*.c
    ${A2B_STACK_ROOT}/a2bplugin-master/src/*.c
    ${A2B_STACK_ROOT}/a2bplugin-slave/src/*.c
    ${A2B_STACK_ROOT}/a2bstack-protobuf/src/*.c
    ${A2B_COMMCH_ROOT}/src/*.c
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}                 # Windows app headers (a2bapp_platform.h, cmd_platform.h)
    ${PAL_ROOT}                                 # Windows PAL headers
    ${PAL_ROOT}/platform                        # "a2b/conf.h", "pnp/a2bpnp_conf.h"
    ${APP_COMMON_ROOT}
    ${A2B_PNP_ROOT}
    ${A2B_STACK_ROOT}/a2bstack/inc
    ${A2B_STACK_ROOT}/a2bstack/inc/a2b
    ${A2B_STACK_ROOT}/a2bstack/src
    ${A2B_STACK_ROOT}/a2bplugin-master/inc
    ${A2B_STACK_ROOT}/a2bplugin-master/src
    ${A2B_STACK_ROOT}/a2bplugin-slave/inc
    ${A2B_STACK_ROOT}/a2bplugin-slave/src
    ${A2B_STACK_ROOT}/a2bstack-protobuf/inc
    ${A2B_COMMCH_ROOT}/inc
    ${LIBUSB_INCLUDE_DIRS}
)

add_executable(a2b_pnp ${SRC_PLATFORM} ${SRC_COMMON} ${SRC_TEST} ${SRC_PNP} ${SRC_STACK})

target_include_directories(a2b_pnp PRIVATE ${INCLUDE_DIRS})
target_link_directories(a2b_pnp PRIVATE ${LIBUSB_LIBRARY_DIRS})
target_link_libraries(a2b_pnp PRIVATE ${LIBUSB_LIBRARIES} winmm)

# gcc's runtime libraries linked in, so the only DLL a2b_pnp.exe needs besides
# Windows' own is libusb-1.0.dll (kept as a DLL: libusb is LGPL).
# (--whole-archive: the static libstdc++ is linked after this and needs winpthread too)
target_link_options(a2b_pnp PRIVATE -static-libgcc -static-libstdc++
    -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)

target_compile_options(a2b_pnp PRIVATE
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<CONFIG:Release>:-O2>
    -Wall -Wextra
)

add_custom_command(TARGET a2b_pnp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBUSB_DLL} $<TARGET_FILE_DIR:a2b_pnp>
    COMMENT "Copying libusb-1.0.dll next to a2b_pnp.exe")
