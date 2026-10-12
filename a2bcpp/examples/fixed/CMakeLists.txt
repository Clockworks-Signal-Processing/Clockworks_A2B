# The fixed app (examples/fixed/fixed_app.cpp) as a library on the desktop hosts, for its
# test on the simulator (tests/example/test_example_fixed.cpp). Its program runs on the
# RP2040: rp2040/, a Pico SDK project of its own.
# ClockWorks Signal Processing LLC

a2b_check_ss_reference()
a2b_add_clockworks_boards()
add_library(a2b_fixed_app STATIC fixed_app.cpp)
target_include_directories(a2b_fixed_app PUBLIC .)
target_link_libraries(a2b_fixed_app PUBLIC a2bcpp_ss_plus a2b_clockworks_boards
                                    PRIVATE a2bcpp_options)
