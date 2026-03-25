cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(heidenhain_glass_scale C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(heidenhain_glass_scale main.c)
pico_generate_pio_header(heidenhain_glass_scale ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)
target_link_libraries(heidenhain_glass_scale
    pico_stdlib
    hardware_pio
    hardware_uart
    hardware_gpio
    hardware_timer
)
pico_add_extra_outputs(heidenhain_glass_scale)
