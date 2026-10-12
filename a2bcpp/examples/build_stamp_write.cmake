# Writes the build time stamp header: cmake -DOUT=FILE -P build_stamp_write.cmake
# ClockWorks Signal Processing LLC

string(TIMESTAMP now "%Y-%m-%d %H:%M:%S")
file(WRITE ${OUT} "#define A2B_BUILD_STAMP \"${now}\"\n")
