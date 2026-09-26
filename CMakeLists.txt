
# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico CACHE STRING "Board type")

# Build type comes from the configure command (-DCMAKE_BUILD_TYPE=Debug or Release; the
# "RP2040:" tasks use build/ for Debug and build_release/ for Release). Default: Debug.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
include(pico_sdk_import.cmake)

# Project name must match the RP2040 executable name: the Pico VS Code extension
# looks for build/<project>.elf when flashing/debugging.
project(A2B_PnP_RP2040 LANGUAGES C CXX ASM)
#not sure this next line is needed, but I keep getting a windows one
set(CMAKE_SYSTEM_PROCESSOR "cortex-m0plus")

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_definitions(A2B_LINK_STATIC=1)
add_compile_definitions(A2B_STATIC_PLUGIN PB_FIELD_16BIT=1)
#decide which library & compiler combination to tuse
set(LIBUSB_MSVC OFF)
set(CYPRESS_MSVC OFF)
set(LIBUSB_MINGW OFF)

if(CYPRESS_MSVC)
add_compile_definitions(A2B_USBI_DOT_NET_DRIVER=1)
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

set( A2B_STACK_ROOT  ${PROJECT_SOURCE_DIR}/a2bstack )
set( A2B_COMMCH_ROOT  ${PROJECT_SOURCE_DIR}/a2bcommchannel )
set( A2B_PNP_ROOT  ${PROJECT_SOURCE_DIR}/a2bpnp)
set( A2B_APP_ROOT   ${PROJECT_SOURCE_DIR}/app )
# not clear how the SDK is deciding on baud rate...seems to be a compiler command line thing vs something in a call the user could do.
add_compile_definitions(PICO_DEFAULT_UART_BAUD_RATE=230400)

#Name of the USBi dot net library
#set( A2B_USBI_LIB "USBi_lib")

# Debug: no optimization and all SDK parameter checks. Release uses the SDK's optimized flags.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(PARAM_ASSERTIONS_ENABLE_ALL=1)
    add_compile_options(-O0)
endif()

message("CMAKE_SYSTEM_PROCESSOR ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Pico SDK version ${PICO_SDK_VERSION_STRING}")
# This file builds the RP2040 only. app/linux_rpi and app/windows are standalone
# CMake projects (see their README.md files and docs/porting/).
include(app/rp2040/rp2040.cmake)
