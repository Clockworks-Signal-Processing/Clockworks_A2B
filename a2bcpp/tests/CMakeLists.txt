# a2bcpp tests, run by CTest. Each test file is its own program (see unit/test_harness.h):
# unit/ tests one class against a fake platform, scenario/ runs against the simulator.
# ClockWorks Signal Processing LLC

function(a2b_add_unit_test name)
  add_executable(${name} unit/${name}.cpp)
  # Unit tests also reach the internal headers in src/.
  target_include_directories(${name} PRIVATE unit ${PROJECT_SOURCE_DIR}/src)
  # Tests may read reference files from the repository (e.g. adi/module_info/*.dat).
  target_compile_definitions(${name} PRIVATE A2B_REPO_ROOT="${PROJECT_SOURCE_DIR}/..")
  target_link_libraries(${name} PRIVATE a2bcpp a2bcpp_options)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

a2b_add_unit_test(test_types)
a2b_add_unit_test(test_chip_caps)
a2b_add_unit_test(test_transport)
a2b_add_unit_test(test_crc8)
a2b_add_unit_test(test_register_image)
a2b_add_unit_test(test_script_runner)
a2b_add_unit_test(test_module_info)
a2b_add_unit_test(test_interrupt_poller)
a2b_add_unit_test(test_module_script)
a2b_add_unit_test(test_response_cycles)
a2b_add_unit_test(test_connection_table)
a2b_add_unit_test(test_routing_planner)
a2b_add_unit_test(test_image_source)
# It builds its images with the simulator's helpers.
target_link_libraries(test_module_script PRIVATE a2bcpp_sim)

# The USBi platform's USB framing, against a fake link (no adapter, no libusb).
if(TARGET a2bcpp_usbi)
  a2b_add_unit_test(test_usbi_platform)
  target_link_libraries(test_usbi_platform PRIVATE a2bcpp_usbi)
endif()

function(a2b_add_scenario_test name)
  add_executable(${name} scenario/${name}.cpp)
  target_include_directories(${name} PRIVATE unit)
  # Tests may read reference files from the repository (e.g. adi/module_info/*.dat).
  target_compile_definitions(${name} PRIVATE A2B_REPO_ROOT="${PROJECT_SOURCE_DIR}/..")
  target_link_libraries(${name} PRIVATE a2bcpp_sim a2bcpp_options)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

a2b_add_scenario_test(test_sim_main_node)
a2b_add_scenario_test(test_sim_discovery)
a2b_add_scenario_test(test_sim_access)
a2b_add_scenario_test(test_sim_network)
a2b_add_scenario_test(test_sim_chain)
a2b_add_scenario_test(test_sim_module_info)
a2b_add_scenario_test(test_sim_node_hooks)
a2b_add_scenario_test(test_sim_drops)
a2b_add_scenario_test(test_sim_errors)
a2b_add_scenario_test(test_sim_events)
a2b_add_scenario_test(test_sim_two_buses)
a2b_add_scenario_test(test_sim_mixed_chips)
a2b_add_scenario_test(test_sim_mixed_routing)
a2b_add_scenario_test(test_sim_app_access)
a2b_add_scenario_test(test_sim_routing)
a2b_add_scenario_test(test_sim_mute)
a2b_add_scenario_test(test_sim_two_step)

# A SigmaStudio+ export (C) linked into a test, as an app links its own.
if(A2B_CPP_SS_PLUS)
  enable_language(C)
  function(a2b_add_export_test name dir source export)
    add_executable(${name} ${dir}/${name}.cpp ${A2B_REPO_ROOT}/${export})
    target_include_directories(${name} PRIVATE unit ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${name} PRIVATE A2B_REPO_ROOT="${A2B_REPO_ROOT}")
    target_link_libraries(${name} PRIVATE a2bcpp_ss_plus ${source} a2bcpp_options)
    # The export is generated code: no warnings, and C flags only.
    set_source_files_properties(${A2B_REPO_ROOT}/${export} PROPERTIES COMPILE_OPTIONS "-w")
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  # The Pi's export (no host peripherals), as the G13 golden ran it.
  a2b_add_export_test(test_ss_plus_import import a2bcpp adi/exports/rp2040/12_a2b_busconfig.c)
  # Scenario 13: the Windows export's AB0020 codec script, with its delay.
  a2b_add_export_test(test_sim_host_script scenario a2bcpp_sim adi/exports/windows/12_a2b_busconfig.c)
  # The G13 register image: the Pi's export, one sub-node, the golden's values.
  a2b_add_export_test(test_sim_g13_image scenario a2bcpp_sim adi/exports/rp2040/12_a2b_busconfig.c)

  # The export loader: the exports read at run time, against them compiled in (their
  # sBusDescription renamed by tests/import/export_*.c).
  if(TARGET a2bcpp_ss_plus_file)
    add_executable(test_ss_plus_file import/test_ss_plus_file.cpp import/export_rp2040.c
                                     import/export_windows.c import/export_ab0020.c
                                     import/export_ab0310.c)
    target_include_directories(test_ss_plus_file PRIVATE unit)
    target_compile_definitions(test_ss_plus_file PRIVATE A2B_REPO_ROOT="${A2B_REPO_ROOT}")
    target_link_libraries(test_ss_plus_file PRIVATE a2bcpp_ss_plus_file a2bcpp_options)
    set_source_files_properties(import/export_rp2040.c import/export_windows.c
                                import/export_ab0020.c import/export_ab0310.c
                                PROPERTIES COMPILE_OPTIONS "-w")
    add_test(NAME test_ss_plus_file COMMAND test_ss_plus_file)
  endif()

  # The bench console, run on the simulator.
  if(TARGET a2b_bench_console)
    add_executable(test_example_bench example/test_example_bench.cpp)
    target_include_directories(test_example_bench PRIVATE unit)
    target_compile_definitions(test_example_bench PRIVATE A2B_REPO_ROOT="${A2B_REPO_ROOT}"
                               A2B_TEST_OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(test_example_bench PRIVATE a2b_bench_console a2bcpp_sim a2bcpp_options)
    add_test(NAME test_example_bench COMMAND test_example_bench)
    add_executable(test_sigma_xml example/test_sigma_xml.cpp)
    target_include_directories(test_sigma_xml PRIVATE unit)
    target_compile_definitions(test_sigma_xml PRIVATE A2B_REPO_ROOT="${A2B_REPO_ROOT}")
    target_link_libraries(test_sigma_xml PRIVATE a2b_bench_console a2bcpp_options)
    add_test(NAME test_sigma_xml COMMAND test_sigma_xml)
    add_executable(test_clockworks_boards example/test_clockworks_boards.cpp)
    target_include_directories(test_clockworks_boards PRIVATE unit)
    target_link_libraries(test_clockworks_boards PRIVATE a2b_bench_console a2bcpp_options)
    add_test(NAME test_clockworks_boards COMMAND test_clockworks_boards)
    # Scenario 20: EEPROMs provisioned from the console, with the image tool's images.
    add_executable(test_eeprom_provisioning example/test_eeprom_provisioning.cpp)
    target_include_directories(test_eeprom_provisioning PRIVATE unit ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(test_eeprom_provisioning PRIVATE A2B_REPO_ROOT="${A2B_REPO_ROOT}"
                               A2B_TEST_OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(test_eeprom_provisioning PRIVATE a2b_bench_console a2bcpp_sim
                                                   a2bcpp_options)
    add_test(NAME test_eeprom_provisioning COMMAND test_eeprom_provisioning)
  endif()

  # The fixed app, run on the simulator with the export its RP2040 program has.
  if(TARGET a2b_fixed_app)
    a2b_add_export_test(test_example_fixed example a2bcpp_sim
                        tools/SS+reference/AB0310_AB0312_EVM_with_2437/adi_a2b_busconfig.c)
    target_link_libraries(test_example_fixed PRIVATE a2b_fixed_app)
  endif()

  # The minimal example app, run on the simulator.
  if(TARGET a2b_minimal_app)
    a2b_add_export_test(test_example_minimal example a2bcpp_sim adi/exports/rp2040/12_a2b_busconfig.c)
    target_link_libraries(test_example_minimal PRIVATE a2b_minimal_app)
  endif()
endif()
