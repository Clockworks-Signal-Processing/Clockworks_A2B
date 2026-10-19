# The minimal example on the RP2040, as its own Pico SDK project (examples/rp2040_example.cmake):
#   cmake -S a2bcpp/examples/minimal/rp2040 -B build/a2b-minimal-rp2040 -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/a2b-minimal-rp2040
# Output: a2b_minimal.elf, .uf2.
# ClockWorks Signal Processing LLC

cmake_minimum_required(VERSION 3.21)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy
include(${CMAKE_CURRENT_LIST_DIR}/../../rp2040_example.cmake)
project(a2b_minimal_rp2040 LANGUAGES C CXX ASM)
a2b_rp2040_init()

# The SigmaStudio+ import builds with the RP2040 app's ADI headers, as its export does.
set(A2B_SS_PLUS_HEADERS
    ${A2B_REPO_ROOT}/adi/headers/stack_export
    ${A2B_REPO_ROOT}/adi/headers/rp2040
    ${A2B_REPO_ROOT}/adi/headers/stack
    CACHE STRING "Include directories for ADI's adi_a2b_busconfig.h" FORCE)
add_subdirectory(${A2B_REPO_ROOT}/a2bcpp a2bcpp)

set(A2B_MINIMAL_EXPORT ${A2B_REPO_ROOT}/adi/exports/rp2040/12_a2b_busconfig.c)
set_source_files_properties(${A2B_MINIMAL_EXPORT} PROPERTIES COMPILE_OPTIONS "-w")
add_executable(a2b_minimal ../main_rp2040.cpp ../minimal_app.cpp ${A2B_MINIMAL_EXPORT})
target_link_libraries(a2b_minimal PRIVATE a2bcpp_ss_plus)
a2b_rp2040_program(a2b_minimal)
