# The minimal example app on Linux (the Raspberry Pi); the RP2040 build is in rp2040/.
# Built when a2bcpp is the top-level project on Linux (A2B_CPP_EXAMPLES). For the Pi,
# cross-build with a2bcpp/tools/toolchain-pi.cmake (a2bcpp/docs/quick_start.md).
# ClockWorks Signal Processing LLC

enable_language(C)

# The Pi's SigmaStudio+ export, as the Pi uses it.
set(A2B_MINIMAL_EXPORT ${A2B_REPO_ROOT}/adi/exports/rp2040/12_a2b_busconfig.c)
set_source_files_properties(${A2B_MINIMAL_EXPORT} PROPERTIES COMPILE_OPTIONS "-w")

add_library(a2b_minimal_app STATIC minimal_app.cpp)
target_include_directories(a2b_minimal_app PUBLIC .)
target_link_libraries(a2b_minimal_app PUBLIC a2bcpp_ss_plus PRIVATE a2bcpp_options)

add_executable(a2b_minimal main_linux.cpp ${A2B_MINIMAL_EXPORT})
include(${CMAKE_CURRENT_SOURCE_DIR}/../build_stamp.cmake)
a2b_add_build_stamp(a2b_minimal)
target_link_libraries(a2b_minimal PRIVATE a2b_minimal_app a2bcpp_linux a2bcpp_options)
