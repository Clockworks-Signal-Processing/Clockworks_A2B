# What the RP2040 example projects (examples/<example>/rp2040) share: the Pico SDK set-up,
# included before project(); then, after it, a2b_rp2040_init() and a2b_rp2040_program() for
# the program. The Pico SDK and its Arm toolchain come from PICO_SDK_PATH (a -D option or the
# environment) if it is set, otherwise from the Pico VS Code extension's ~/.pico-sdk, with the
# versions pinned below (SDK 2.0.0, toolchain 13_2_Rel1, picotool 2.0.0). Every program runs
# on the AB0310 board's wiring (platforms/rp2040/ab0310_wiring.h) with its UART console on
# uart0 at 230400 baud.
# ClockWorks Signal Processing LLC

get_filename_component(A2B_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
set(A2B_RP2040_DIR ${A2B_REPO_ROOT}/a2bcpp/platforms/rp2040)

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
include(${CMAKE_CURRENT_LIST_DIR}/build_stamp.cmake)

# After project(): the SDK, and the console's speed for every target.
macro(a2b_rp2040_init)
  pico_sdk_init()
  add_compile_definitions(PICO_DEFAULT_UART_BAUD_RATE=230400)
endmacro()

# C++ options for every RP2040 target of the examples: no exceptions or RTTI, and no
# -Wpsabi notes. GCC notes that the Arm calling convention for some std::vector iterator
# arguments changed in GCC 7.1; that matters only when objects from an older GCC are linked
# in, and one compiler builds the whole program here.
set(A2B_RP2040_CXX_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -Wno-psabi>)

# The Clockworks board profiles (examples/clockworks.cmake) for an RP2040 program, built
# with the program's C++ options. A2B_SS_PLUS_HEADERS must include the reference ADI headers.
macro(a2b_rp2040_clockworks_boards)
  include(${A2B_REPO_ROOT}/a2bcpp/examples/clockworks.cmake)
  a2b_check_ss_reference()
  a2b_add_clockworks_boards()
  target_compile_options(a2b_clockworks_boards PRIVATE ${A2B_RP2040_CXX_OPTIONS})
endmacro()

# Makes @p target an RP2040 program: the RP2040 platform and heap helper, the example's own
# folder (..) and the platform's on the include path, stdio on the UART, the C++ options
# above, the build stamp, and the .uf2 beside the .elf.
function(a2b_rp2040_program target)
  target_sources(${target} PRIVATE ${A2B_RP2040_DIR}/rp2040_platform.cpp
                                   ${A2B_RP2040_DIR}/rp2040_heap.c)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${A2B_RP2040_DIR})
  target_link_libraries(${target} PRIVATE pico_stdlib hardware_i2c hardware_uart)
  target_compile_options(${target} PRIVATE ${A2B_RP2040_CXX_OPTIONS})
  pico_enable_stdio_uart(${target} 1)
  pico_enable_stdio_usb(${target} 0)
  a2b_add_build_stamp(${target})
  pico_add_extra_outputs(${target})
endfunction()
