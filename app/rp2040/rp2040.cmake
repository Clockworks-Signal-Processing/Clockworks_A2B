# RP2040 build of the A2B PnP console application.
# include()d from the root CMakeLists.txt (after pico_sdk_init()), so all
# outputs (.elf/.uf2/.bin) land directly in build/, where the Pico VS Code
# extension expects build/<project>.elf.

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(A2B_TARGET ${PROJECT_NAME})
set(A2B_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

file(GLOB_RECURSE SRC_FILES_EXEC
    ${CMAKE_CURRENT_LIST_DIR}/a2bapp_rp2040.c
    ${CMAKE_CURRENT_LIST_DIR}/12_a2b_busconfig.c
    ${CMAKE_CURRENT_LIST_DIR}/platform_inits.c
    ${CMAKE_CURRENT_LIST_DIR}/a2bstack-pal/*.c
    ${CMAKE_CURRENT_LIST_DIR}/a2bstack-pal/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/a2bstack-pal/*.hpp
)

file(GLOB_RECURSE SRC_FILES_APP_COMMON
    ${A2B_ROOT}/app/common/*.c
)

file(GLOB_RECURSE SRC_FILES_PNP_LIB
    ${A2B_ROOT}/a2bpnp/*.c
    ${A2B_ROOT}/a2bpnp/*.cpp
)

file(GLOB_RECURSE SRC_FILES_LIB
    ${A2B_ROOT}/a2bstack/a2bstack/src/*.c
    ${A2B_ROOT}/a2bcommchannel/src/*.c
    ${A2B_ROOT}/a2bstack/a2bplugin-master/src/*.c
    ${A2B_ROOT}/a2bstack/a2bplugin-slave/src/*.c
    ${A2B_ROOT}/a2bstack/a2bstack-protobuf/src/*.c
)

file(GLOB_RECURSE SRC_FILES_APP_TEST
    ${A2B_ROOT}/app/test/*.c
    ${A2B_ROOT}/app/test/cJSON-master/*.c
)

add_definitions(-DA2B_STATIC_PLUGIN -DPB_FIELD_16BIT=1)

add_executable(${A2B_TARGET}
    ${SRC_FILES_EXEC}
    ${SRC_FILES_APP_COMMON}
    ${SRC_FILES_PNP_LIB}
    ${SRC_FILES_APP_TEST}
    ${SRC_FILES_LIB})


include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${A2B_ROOT}/app/common
    ${CMAKE_CURRENT_LIST_DIR}/a2bstack-pal
    ${CMAKE_CURRENT_LIST_DIR}/a2bstack-pal/platform
    ${A2B_ROOT}/a2bpnp
    ${A2B_ROOT}/a2bstack/a2bstack/inc
    ${A2B_ROOT}/a2bstack/a2bstack/inc/a2b
    ${A2B_ROOT}/a2bstack/a2bstack/src
    ${A2B_ROOT}/a2bcommchannel/inc
    ${A2B_ROOT}/a2bstack/a2bplugin-master/inc
    ${A2B_ROOT}/a2bstack/a2bplugin-master/src
    ${A2B_ROOT}/a2bstack/a2bplugin-slave/inc
    ${A2B_ROOT}/a2bstack/a2bplugin-slave/src
    ${A2B_ROOT}/a2bstack/a2bstack-protobuf/inc
)

pico_generate_pio_header(${A2B_TARGET} ${CMAKE_CURRENT_LIST_DIR}/spi_pio/clocked_input.pio)
pico_generate_pio_header(${A2B_TARGET} ${CMAKE_CURRENT_LIST_DIR}/spi_pio/spi.pio)

target_link_libraries(${A2B_TARGET} PRIVATE
        pico_stdlib
        pico_stdio
        pico_unique_id
        hardware_pio
        pico_multicore
        hardware_adc
        hardware_spi
        hardware_pwm
        hardware_uart
        hardware_gpio
        hardware_dma
        hardware_i2c )

# Enable USB and set the USB stack to use
pico_enable_stdio_usb(${A2B_TARGET} 1)

target_compile_definitions(${A2B_TARGET} PUBLIC
                                _CRT_SECURE_NO_WARNINGS)

pico_add_extra_outputs(${A2B_TARGET})
