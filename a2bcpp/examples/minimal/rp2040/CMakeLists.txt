# The minimal example app on the RP2040, as its own Pico SDK project:
#   cmake -S a2bcpp/examples/minimal/rp2040 -B build/a2b-minimal-rp2040 -G Ninja
#   cmake --build build/a2b-minimal-rp2040
# The Pico SDK and its Arm toolchain come from PICO_SDK_PATH (a -D option or the
# environment) if it is set, otherwise from the Pico VS Code extension's ~/.pico-sdk, with
# the versions pinned below (SDK 2.0.0, toolchain 13_2_Rel1, picotool 2.0.0).
# The UART console runs at 230400 baud, (the console speed of the AB0310 board's tools). Output: a2b_minimal.uf2.
# ClockWorks Signal Processing LLC

cmake_minimum_required(VERSION 3.21)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy
set(A2B_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)

# The extension's file sets PICO_SDK_PATH, PICO_TOOLCHAIN_PATH and the picotool and pioasm
# folders from these versions. It must come before pico_sdk_import.cmake and project().
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
  else()
    set(USERHOME $ENV{HOME})
  endif()
  set(sdkVersion 2.0.0)
  set(toolchainVersion 13_2_Rel1)
  set(picotoolVersion 2.0.0)
  set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
  if(EXISTS ${picoVscode})
    include(${picoVscode})
  endif()
endif()
set(PICO_BOARD pico CACHE STRING "Board type")
include(${A2B_REPO_ROOT}/pico_sdk_import.cmake)
project(a2b_minimal_rp2040 LANGUAGES C CXX ASM)
pico_sdk_init()
add_compile_definitions(PICO_DEFAULT_UART_BAUD_RATE=230400)  # the console speed

# The SigmaStudio+ import builds with the RP2040 app's ADI headers, as its export does.
set(A2B_SS_PLUS_HEADERS
    ${A2B_REPO_ROOT}/adi/headers/stack_export
    ${A2B_REPO_ROOT}/adi/headers/rp2040
    ${A2B_REPO_ROOT}/adi/headers/stack
    CACHE STRING "Include directories for ADI's adi_a2b_busconfig.h" FORCE)
add_subdirectory(${A2B_REPO_ROOT}/a2bcpp a2bcpp)

set(A2B_MINIMAL_EXPORT ${A2B_REPO_ROOT}/adi/exports/rp2040/12_a2b_busconfig.c)
set_source_files_properties(${A2B_MINIMAL_EXPORT} PROPERTIES COMPILE_OPTIONS "-w")

add_executable(a2b_minimal
    ../main_rp2040.cpp
    ../minimal_app.cpp
    ${A2B_REPO_ROOT}/a2bcpp/platforms/rp2040/rp2040_platform.cpp
    ${A2B_MINIMAL_EXPORT}
)
target_include_directories(a2b_minimal PRIVATE .. ${A2B_REPO_ROOT}/a2bcpp/platforms/rp2040)
target_link_libraries(a2b_minimal PRIVATE a2bcpp_ss_plus pico_stdlib hardware_i2c)
target_compile_options(a2b_minimal PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
pico_enable_stdio_uart(a2b_minimal 1)
pico_enable_stdio_usb(a2b_minimal 0)
include(${A2B_REPO_ROOT}/a2bcpp/examples/build_stamp.cmake)
a2b_add_build_stamp(a2b_minimal)
pico_add_extra_outputs(a2b_minimal)
