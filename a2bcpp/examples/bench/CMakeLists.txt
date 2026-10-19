# The bench console (a2bcpp/docs/bench_console.md): Windows over the ADI USBi, Linux over
# the host's I2C (i2c-dev) or the USBi. Built when a2bcpp is the top-level project
# (A2B_CPP_EXAMPLES) on Linux, and on Windows when libusb-1.0 is found.
# ClockWorks Signal Processing LLC

enable_language(C)
find_package(Threads REQUIRED)

# The console's commands, host-independent, so that the tests run them on the simulator.
# The built-in boards are the repository's exports, each renamed by its board_*.c:
# adi/exports' (board_ref.c) and tools/SS+reference's (board_ab0020.c, board_ab0020_2428.c,
# board_ab0310.c), the latter compiled with the reference ADI headers
# (examples/clockworks.cmake).
a2b_check_ss_reference()
a2b_add_clockworks_boards()
add_library(a2b_bench_console STATIC bench_console.cpp bench_console_files.cpp console_text.cpp
                                     eeprom_image.cpp eeprom_job.cpp trace_platform.cpp
                                     board_ref.c board_ab0020.c board_ab0020_2428.c board_ab0310.c)
target_include_directories(a2b_bench_console PUBLIC .)
target_link_libraries(a2b_bench_console PUBLIC a2bcpp_ss_plus_file a2b_clockworks_boards
                                        PRIVATE a2bcpp_options)
# The exports are generated code: no warnings.
set_source_files_properties(board_ref.c board_ab0020.c board_ab0020_2428.c board_ab0310.c
                            PROPERTIES COMPILE_OPTIONS "-w")

# The console as the RP2040 builds it (A2B_BENCH_FILES 0: no file commands;
# examples/bench/rp2040), compiled here as well, so that a change to the console that breaks
# that build shows without the Pico SDK. Nothing links it.
add_library(a2b_bench_console_no_files OBJECT bench_console.cpp console_text.cpp)
target_compile_definitions(a2b_bench_console_no_files PRIVATE A2B_BENCH_FILES=0)
target_include_directories(a2b_bench_console_no_files PRIVATE .)
target_link_libraries(a2b_bench_console_no_files PRIVATE a2bcpp_ss_plus a2b_clockworks_boards
                                                         a2bcpp_options)

add_executable(a2b_bench main_bench.cpp)
include(${CMAKE_CURRENT_SOURCE_DIR}/../build_stamp.cmake)
a2b_add_build_stamp(a2b_bench)
target_link_libraries(a2b_bench PRIVATE a2b_bench_console a2bcpp_options Threads::Threads)
if(TARGET a2bcpp_linux)
  target_link_libraries(a2b_bench PRIVATE a2bcpp_linux)
endif()
if(TARGET a2bcpp_usbi_libusb)
  target_link_libraries(a2b_bench PRIVATE a2bcpp_usbi_libusb)
  target_compile_definitions(a2b_bench PRIVATE A2B_CPP_USBI=1)
endif()

if(WIN32)
  # gcc's runtime linked in, so the only DLL a2b_bench.exe
  # needs besides Windows' own is libusb-1.0.dll (kept a DLL: libusb is LGPL), copied
  # next to it.
  target_link_options(a2b_bench PRIVATE -static-libgcc -static-libstdc++
      -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
  find_file(A2B_LIBUSB_DLL libusb-1.0.dll
      HINTS ${LIBUSB_PREFIX}/bin ${LIBUSB_LIBRARY_DIRS}/../bin
      NO_DEFAULT_PATH)
  if(A2B_LIBUSB_DLL)
    add_custom_command(TARGET a2b_bench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${A2B_LIBUSB_DLL} $<TARGET_FILE_DIR:a2b_bench>
        COMMENT "Copying libusb-1.0.dll next to a2b_bench.exe")
  else()
    message(WARNING "libusb-1.0.dll not found next to ${LIBUSB_LIBRARY_DIRS}: copy it next to "
                    "a2b_bench.exe, or run it from an MSYS2 UCRT64 shell")
  endif()
endif()
