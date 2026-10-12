# a2bcpp_sim: the simulated A2B system the host tests run the stack against.
# Host only; it uses the standard library freely.
# ClockWorks Signal Processing LLC

add_library(a2bcpp_sim STATIC
    eeprom.cpp
    module_info.cpp
    simulator.cpp
)
# Includes are "sim/<name>.h"; the simulator also uses the stack's internal headers.
target_include_directories(a2bcpp_sim PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(a2bcpp_sim PUBLIC a2bcpp PRIVATE a2bcpp_options)
