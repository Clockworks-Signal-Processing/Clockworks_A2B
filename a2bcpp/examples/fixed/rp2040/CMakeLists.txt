# The fixed app on the RP2040, as its own Pico SDK project (examples/rp2040_example.cmake):
#   cmake -S a2bcpp/examples/fixed/rp2040 -B build/a2b-fixed-rp2040 -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/a2b-fixed-rp2040
# Output: a2b_fixed.elf, .uf2.
# ClockWorks Signal Processing LLC

cmake_minimum_required(VERSION 3.21)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy
include(${CMAKE_CURRENT_LIST_DIR}/../../rp2040_example.cmake)
project(a2b_fixed_rp2040 LANGUAGES C CXX ASM)
a2b_rp2040_init()

# The SigmaStudio+ import builds with the reference ADI headers, as the export does
# (examples/clockworks.cmake), with the RP2040 app's a2b/ headers.
set(A2B_SS_REFERENCE_HEADERS ${A2B_REPO_ROOT}/tools/SS+reference/adi)
set(A2B_SS_PLUS_HEADERS
    ${A2B_SS_REFERENCE_HEADERS}
    ${A2B_REPO_ROOT}/adi/headers/rp2040
    ${A2B_REPO_ROOT}/adi/headers/stack
    CACHE STRING "Include directories for ADI's adi_a2b_busconfig.h" FORCE)
add_subdirectory(${A2B_REPO_ROOT}/a2bcpp a2bcpp)
a2b_rp2040_clockworks_boards()

set(A2B_FIXED_EXPORT
    ${A2B_REPO_ROOT}/tools/SS+reference/AB0310_AB0312_EVM_with_2437/adi_a2b_busconfig.c)
set_source_files_properties(${A2B_FIXED_EXPORT} PROPERTIES COMPILE_OPTIONS "-w")
add_executable(a2b_fixed ../main_rp2040.cpp ../fixed_app.cpp ${A2B_FIXED_EXPORT})
target_link_libraries(a2b_fixed PRIVATE a2bcpp_ss_plus a2b_clockworks_boards)
a2b_rp2040_program(a2b_fixed)
