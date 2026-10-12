# A build time stamp for an example executable, so that a program started from an old build
# says so: every build of the target regenerates a2b_build_stamp.h (A2B_BUILD_STAMP, local
# time, "YYYY-MM-DD HH:MM:SS"), which the program prints as its first line. The program's
# main file is compiled again each time (and the target linked again), which is cheap.
# ClockWorks Signal Processing LLC

set(A2B_BUILD_STAMP_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/build_stamp_write.cmake)

# Gives @p target the header "a2b_build_stamp.h", rewritten at the start of every build.
function(a2b_add_build_stamp target)
  set(dir ${CMAKE_CURRENT_BINARY_DIR}/${target}_stamp)
  file(MAKE_DIRECTORY ${dir})
  add_custom_target(${target}_stamp
      COMMAND ${CMAKE_COMMAND} -DOUT=${dir}/a2b_build_stamp.h -P ${A2B_BUILD_STAMP_SCRIPT}
      BYPRODUCTS ${dir}/a2b_build_stamp.h
      COMMENT "Time stamp for ${target}"
      VERBATIM)
  add_dependencies(${target} ${target}_stamp)
  target_include_directories(${target} PRIVATE ${dir})
endfunction()
