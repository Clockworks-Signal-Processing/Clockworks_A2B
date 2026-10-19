# The bench console on the RP2040, as its own Pico SDK project (examples/rp2040_example.cmake):
#   cmake -S a2bcpp/examples/bench/rp2040 -B build/a2b-bench-rp2040 -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/a2b-bench-rp2040
# Built without file commands (A2B_BENCH_FILES 0). Output: a2b_bench.elf, .uf2.
# ClockWorks Signal Processing LLC

cmake_minimum_required(VERSION 3.21)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy
include(${CMAKE_CURRENT_LIST_DIR}/../../rp2040_example.cmake)
project(a2b_bench_rp2040 LANGUAGES C CXX ASM)
a2b_rp2040_init()

# The SigmaStudio+ import builds with the reference ADI headers, as the exports do
# (examples/clockworks.cmake), with the RP2040 app's a2b/ headers.
set(A2B_SS_REFERENCE_HEADERS ${A2B_REPO_ROOT}/tools/SS+reference/adi)
set(A2B_SS_PLUS_HEADERS
    ${A2B_SS_REFERENCE_HEADERS}
    ${A2B_REPO_ROOT}/adi/headers/rp2040
    ${A2B_REPO_ROOT}/adi/headers/stack
    CACHE STRING "Include directories for ADI's adi_a2b_busconfig.h" FORCE)
add_subdirectory(${A2B_REPO_ROOT}/a2bcpp a2bcpp)
a2b_rp2040_clockworks_boards()

# The console's sources but the file-bound ones (bench_console_files, eeprom_job,
# trace_platform); eeprom_image names an image's chip in `status`. The built-in boards are
# the repository's exports, generated code: no warnings.
set(A2B_BENCH_BOARDS ../board_ref.c ../board_ab0020.c ../board_ab0020_2428.c ../board_ab0310.c)
set_source_files_properties(${A2B_BENCH_BOARDS} PROPERTIES COMPILE_OPTIONS "-w")
add_executable(a2b_bench ../main_rp2040.cpp ../bench_console.cpp ../console_text.cpp
                         ../eeprom_image.cpp ${A2B_BENCH_BOARDS})
target_compile_definitions(a2b_bench PRIVATE A2B_BENCH_FILES=0)
target_link_libraries(a2b_bench PRIVATE a2bcpp_ss_plus a2b_clockworks_boards)
a2b_rp2040_program(a2b_bench)
