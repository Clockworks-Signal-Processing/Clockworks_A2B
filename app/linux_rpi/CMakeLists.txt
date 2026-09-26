cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# A2B PnP — 64-bit Linux / Raspberry Pi (arm64)
#
# A standalone project, separate from the RP2040 build at the repo root (the
# root CMakeLists.txt belongs to the Pico SDK / Pico VS Code extension).
# From the repository root, in Debian (see README.md):
#   cmake -S app/linux_rpi -B app/linux_rpi/build \
#         -DCMAKE_TOOLCHAIN_FILE=app/linux_rpi/toolchain-pi.cmake -DCMAKE_BUILD_TYPE=Debug
#   cmake --build app/linux_rpi/build -j
# ---------------------------------------------------------------------------
project(a2b_pnp
    LANGUAGES C CXX
    VERSION 1.0.0
)

if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(WARNING
        "CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}', expected 'aarch64'. "
        "Did you forget -DCMAKE_TOOLCHAIN_FILE=app/linux_rpi/toolchain-pi.cmake?")
endif()

# ---------------------------------------------------------------------------
# Build types
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------------------
# Root paths — relative to this CMakeLists.txt (app/linux_rpi/)
# ---------------------------------------------------------------------------
set(PROJECT_ROOT        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(A2B_STACK_ROOT      ${PROJECT_ROOT}/a2bstack)
set(A2B_PNP_ROOT        ${PROJECT_ROOT}/a2bpnp)
set(A2B_COMMCH_ROOT     ${PROJECT_ROOT}/a2bcommchannel)
set(APP_COMMON_ROOT     ${PROJECT_ROOT}/app/common)   # shared cmd_parse, cmd_queue, etc.
set(APP_TEST_ROOT       ${PROJECT_ROOT}/app/test)     # cJSON (used by a2bapp_common.c)

# ---------------------------------------------------------------------------
# libgpiod v2 (RESETn / IRQ), found through pkg-config's arm64 directory
# (set by toolchain-pi.cmake).  Install once:  sudo apt-get install libgpiod-dev:arm64
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD libgpiod>=2.0)
if(NOT GPIOD_FOUND)
    message(FATAL_ERROR
        "libgpiod v2 for arm64 not found.  Install it with:\n"
        "  sudo dpkg --add-architecture arm64 && sudo apt-get update\n"
        "  sudo apt-get install libgpiod-dev:arm64\n"
        "(If pkg-config reports 1.x you are seeing a v1 library; this port uses the v2 API.)")
endif()
message(STATUS "libgpiod ${GPIOD_VERSION}: ${GPIOD_LIBRARY_DIRS}")

# ---------------------------------------------------------------------------
# Compile definitions (carry forward from the RP2040 app)
# ---------------------------------------------------------------------------
add_compile_definitions(
    A2B_STATIC_PLUGIN
    A2B_LINK_STATIC=1
    PB_FIELD_16BIT=1
)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SRC_PAL
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/adi_a2b_pal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pal_timer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pal_gpio.c
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

# Bus configuration (sBusDescription) — SigmaStudio output in C struct form,
# shared with the RP2040 build; it has no Pico SDK dependencies.
set(SRC_BCF ${PROJECT_ROOT}/app/rp2040/12_a2b_busconfig.c)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}                 # Linux platform headers
    ${CMAKE_CURRENT_SOURCE_DIR}/platform        # "a2b/conf.h", "pnp/a2bpnp_conf.h"
    # NOTE: do NOT add platform/a2b itself.  Its features.h would shadow
    # glibc's <features.h> (GCC -I paths serve <> includes too) and break stdio.h.
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
    ${GPIOD_INCLUDE_DIRS}
)

add_executable(a2b_pnp ${SRC_PAL} ${SRC_COMMON} ${SRC_TEST} ${SRC_PNP} ${SRC_STACK} ${SRC_BCF})

target_include_directories(a2b_pnp PRIVATE ${INCLUDE_DIRS})
target_compile_definitions(a2b_pnp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_directories(a2b_pnp PRIVATE ${GPIOD_LIBRARY_DIRS})
target_link_libraries(a2b_pnp PRIVATE ${GPIOD_LIBRARIES} m)

target_compile_options(a2b_pnp PRIVATE
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<CONFIG:Release>:-O2>
    -Wall -Wextra
)
