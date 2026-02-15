cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(superdro C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(superdro
    src/main.c
    src/config.c
    src/encoder.c
    src/stepper.c
    src/els.c
    src/protocol.c
    src/safety.c
)

# PIO programs
pico_generate_pio_header(superdro ${CMAKE_CURRENT_LIST_DIR}/pio/quadrature.pio)
pico_generate_pio_header(superdro ${CMAKE_CURRENT_LIST_DIR}/pio/stepper.pio)

# Pico W compile definitions (PICO_BOARD is set by cmake -DPICO_BOARD=pico_w)
target_compile_definitions(superdro PRIVATE
    CYW43_HOST_NAME="SuperDRO"
)

target_include_directories(superdro PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(superdro
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_flash
    hardware_sync
    hardware_adc
    hardware_watchdog
    pico_cyw43_arch_none
    pico_unique_id
)

# USB serial output
pico_enable_stdio_usb(superdro 1)
pico_enable_stdio_uart(superdro 0)

pico_add_extra_outputs(superdro)
