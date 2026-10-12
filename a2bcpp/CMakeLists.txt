# a2bcpp: the C++ A2B PnP stack (design: a2bcpp/docs/decisions.md; what is left to do:
# docs/cpp-stack/ROADMAP.md).
# ClockWorks Signal Processing LLC
#
# Builds on its own for the host unit tests:
#   cmake -S a2bcpp -B build/a2bcpp-<host> -G Ninja
#   cmake --build build/a2bcpp-<host>
#   ctest --test-dir build/a2bcpp-<host> --output-on-failure
# or with a2bcpp/tools/run_tests.ps1 (Windows, MSYS2) and run_tests.sh (Linux, WSL).
# The platform builds will add it with add_subdirectory() once they use it.

cmake_minimum_required(VERSION 3.21)
project(a2bcpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy

option(A2B_CPP_TESTS "Build the a2bcpp unit tests" ${PROJECT_IS_TOP_LEVEL})

# Warnings and the no-exceptions, no-RTTI rule, for the stack and its tests.
add_library(a2bcpp_options INTERFACE)
target_compile_options(a2bcpp_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow -fno-exceptions -fno-rtti>)

add_library(a2bcpp STATIC
    src/chip_caps.cpp
    src/connection_table.cpp
    src/crc8.cpp
    src/discovery.cpp
    src/eeprom_source.cpp
    src/interrupt_poller.cpp
    src/link_profile.cpp
    src/module_info.cpp
    src/module_script.cpp
    src/network.cpp
    src/node_setup.cpp
    src/op_list.cpp
    src/register_image.cpp
    src/routing.cpp
    src/routing_planner.cpp
    src/script_runner.cpp
    src/sub_node.cpp
    src/transport.cpp
)
target_include_directories(a2bcpp
    PUBLIC include
    PRIVATE src
)
target_link_libraries(a2bcpp PRIVATE a2bcpp_options)

# Optional SigmaStudio+ import (import/ss_plus). It includes ADI's adi_a2b_busconfig.h,
# which needs an adi_a2b_datatypes.h and the stack's a2b/ headers. A compiled export and the
# import must see the same structures, so the default is the headers every export in
# tools/SS+reference is compiled with (tools/SS+reference/adi, ADI's header merged for
# SigmaStudio+ 2.3.0), with the Linux platform's a2b/ headers, which also build on the
# test hosts. A platform build that compiles only its own export may set
# A2B_SS_PLUS_HEADERS to its own headers (examples/minimal/rp2040).
option(A2B_CPP_SS_PLUS "Build the SigmaStudio+ import library" ON)
set(A2B_REPO_ROOT "${PROJECT_SOURCE_DIR}/..")
set(A2B_SS_REFERENCE_HEADERS "${A2B_REPO_ROOT}/tools/SS+reference/adi")
set(A2B_SS_PLUS_HEADERS
    ${A2B_SS_REFERENCE_HEADERS}
    ${A2B_REPO_ROOT}/adi/headers/linux
    ${A2B_REPO_ROOT}/adi/headers/stack
    CACHE STRING "Include directories for ADI's adi_a2b_busconfig.h and its headers")
if(A2B_CPP_SS_PLUS)
  add_library(a2bcpp_ss_plus STATIC import/ss_plus/ss_plus_import.cpp)
  target_include_directories(a2bcpp_ss_plus
      PUBLIC import/ss_plus ${A2B_SS_PLUS_HEADERS}
      PRIVATE src
  )
  target_link_libraries(a2bcpp_ss_plus PUBLIC a2bcpp PRIVATE a2bcpp_options)
endif()

# Desktop hosts (Linux, Windows): the platforms and tools that need a full C++ library.
set(A2B_DESKTOP_HOST OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32)
  set(A2B_DESKTOP_HOST ON)
endif()

# The SigmaStudio+ export read at run time (import/ss_plus/ss_plus_file.cpp), for host
# apps such as the bench console: it uses the heap and the C++ library.
if(A2B_CPP_SS_PLUS AND A2B_DESKTOP_HOST)
  add_library(a2bcpp_ss_plus_file STATIC import/ss_plus/ss_plus_file.cpp)
  target_link_libraries(a2bcpp_ss_plus_file PUBLIC a2bcpp_ss_plus PRIVATE a2bcpp_options)
endif()

# The ADI USBi platform (platforms/usbi). Its USB framing builds on any desktop host (the
# unit tests run it against a fake link); its libusb link needs libusb-1.0 through
# pkg-config: MSYS2's mingw-w64-ucrt-x86_64-libusb on Windows, libusb-1.0-0-dev on Debian.
# AUTO builds the link when libusb is found (a cross build finds only the target's).
set(A2B_CPP_USBI AUTO CACHE STRING "Build the USBi platform's libusb link: AUTO, ON or OFF")
set_property(CACHE A2B_CPP_USBI PROPERTY STRINGS AUTO ON OFF)
if(A2B_DESKTOP_HOST)
  add_library(a2bcpp_usbi STATIC platforms/usbi/usbi_platform.cpp)
  target_include_directories(a2bcpp_usbi PUBLIC platforms/usbi)
  target_link_libraries(a2bcpp_usbi PUBLIC a2bcpp PRIVATE a2bcpp_options)
  if(NOT A2B_CPP_USBI STREQUAL "OFF")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
      pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
    endif()
    if(LIBUSB_FOUND)
      message(STATUS "libusb ${LIBUSB_VERSION}: the USBi platform's libusb link is built")
      add_library(a2bcpp_usbi_libusb STATIC platforms/usbi/libusb_link.cpp)
      target_link_libraries(a2bcpp_usbi_libusb PUBLIC a2bcpp_usbi PkgConfig::LIBUSB
                            PRIVATE a2bcpp_options)
      if(WIN32)
        target_link_libraries(a2bcpp_usbi_libusb PRIVATE winmm)  # timeBeginPeriod()
      endif()
    elseif(A2B_CPP_USBI STREQUAL "ON")
      message(FATAL_ERROR "libusb-1.0 not found (pkg-config). Windows, in an MSYS2 UCRT64 shell: "
                          "pacman -S mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-pkgconf; "
                          "Debian: sudo apt install libusb-1.0-0-dev pkg-config")
    else()
      message(STATUS "libusb-1.0 not found: the USBi platform's libusb link is not built")
    endif()
  endif()
endif()

# The example apps: the minimal example on Linux (examples/minimal; its RP2040 build is a
# Pico SDK project of its own in examples/minimal/rp2040), the bench console on Linux
# and, with the USBi's libusb link, Windows (examples/bench), and the fixed app's library
# on the desktop hosts, for its tests (examples/fixed; its RP2040 build is in
# examples/fixed/rp2040).
option(A2B_CPP_EXAMPLES "Build the example apps (Linux, Windows)" ${PROJECT_IS_TOP_LEVEL})
if(A2B_CPP_EXAMPLES AND A2B_CPP_SS_PLUS AND A2B_DESKTOP_HOST)
  include(examples/clockworks.cmake)
  add_subdirectory(examples/fixed)
endif()
if(A2B_CPP_EXAMPLES AND A2B_CPP_SS_PLUS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # The Linux platform (platforms/linux): i2c-dev, for the Pi.
  add_library(a2bcpp_linux STATIC platforms/linux/linux_platform.cpp)
  target_include_directories(a2bcpp_linux PUBLIC platforms/linux)
  target_link_libraries(a2bcpp_linux PUBLIC a2bcpp PRIVATE a2bcpp_options)
  add_subdirectory(examples/minimal)
endif()
if(A2B_CPP_EXAMPLES AND TARGET a2bcpp_ss_plus_file AND
   (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR TARGET a2bcpp_usbi_libusb))
  add_subdirectory(examples/bench)
endif()

if(A2B_CPP_TESTS)
  enable_testing()
  add_subdirectory(sim)
  add_subdirectory(tests)
endif()
