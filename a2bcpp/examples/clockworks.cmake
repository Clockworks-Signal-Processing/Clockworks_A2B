# What the examples that use Clockworks boards share: the checks on tools/SS+reference and
# the clockworks_boards library (examples/clockworks: clockworks_boards.cpp, sigma_xml.cpp and
# the codec programs built in as text). Included by a2bcpp/CMakeLists.txt for the desktop
# examples and by the bench and fixed RP2040 projects. Needs A2B_REPO_ROOT, A2B_SS_REFERENCE_HEADERS and
# A2B_SS_PLUS_HEADERS (a2bcpp/CMakeLists.txt) and the a2bcpp and a2bcpp_options targets.
# ClockWorks Signal Processing LLC

set(A2B_SS_REFERENCE ${A2B_REPO_ROOT}/tools/SS+reference)
set(A2B_CLOCKWORKS_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/clockworks)

# A program that compiles tools/SS+reference's exports must build the SigmaStudio+ import
# with the reference ADI headers (tools/SS+reference/adi), or the import would read their
# structures with another layout. A reference project folder holds only its project's
# files: its own copy of an ADI header would be the one its export includes.
function(a2b_check_ss_reference)
  if(NOT A2B_SS_REFERENCE_HEADERS IN_LIST A2B_SS_PLUS_HEADERS)
    message(FATAL_ERROR "tools/SS+reference's exports are compiled in, so "
                        "A2B_SS_PLUS_HEADERS must include ${A2B_SS_REFERENCE_HEADERS}")
  endif()
  file(GLOB shadows
       ${A2B_SS_REFERENCE}/*/adi_a2b_busconfig.h ${A2B_SS_REFERENCE}/*/adi_a2b_datatypes.h
       ${A2B_SS_REFERENCE}/*/adi_a2b_graphdata.h)
  list(FILTER shadows EXCLUDE REGEX "/adi/[^/]+$")
  if(shadows)
    message(FATAL_ERROR "ADI's generic headers belong in ${A2B_SS_REFERENCE}/adi only; "
                        "remove ${shadows}")
  endif()
endfunction()

# The library a2b_clockworks_boards: the Clockworks board profiles, their module-info
# images and BoardModuleSource, with the codec programs built in as text
# (clockworks_files.cpp.in, generated here). A changed codec program reconfigures.
function(a2b_add_clockworks_boards)
  if(TARGET a2b_clockworks_boards)
    return()
  endif()
  set(ab0020 ${A2B_SS_REFERENCE}/2x_AB0020_EVM_with_2437)
  file(READ ${ab0020}/AB0020-sub-node-ADAU1761.xml A2B_AB0020_SUB_NODE_XML)
  configure_file(${A2B_CLOCKWORKS_SOURCE_DIR}/clockworks_files.cpp.in
                 ${CMAKE_CURRENT_BINARY_DIR}/clockworks_files.cpp @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${ab0020}/AB0020-sub-node-ADAU1761.xml)
  add_library(a2b_clockworks_boards STATIC
      ${A2B_CLOCKWORKS_SOURCE_DIR}/clockworks_boards.cpp
      ${A2B_CLOCKWORKS_SOURCE_DIR}/sigma_xml.cpp
      ${CMAKE_CURRENT_BINARY_DIR}/clockworks_files.cpp)
  target_include_directories(a2b_clockworks_boards PUBLIC ${A2B_CLOCKWORKS_SOURCE_DIR})
  target_link_libraries(a2b_clockworks_boards PUBLIC a2bcpp PRIVATE a2bcpp_options)
endfunction()
