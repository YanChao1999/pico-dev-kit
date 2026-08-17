cmake_minimum_required(VERSION 3.13)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_dev_kit C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Generate PIO headers from .pio source files
pico_generate_pio_header(pico_dev_kit
    ${CMAKE_CURRENT_LIST_DIR}/src/pio/i2c_monitor.pio
)
pico_generate_pio_header(pico_dev_kit
    ${CMAKE_CURRENT_LIST_DIR}/src/pio/spi_monitor.pio
)

add_executable(pico_dev_kit
    src/main.c
    src/usb/usb_transport.c
    src/usb/usb_descriptors.c
    src/console/console.c
    src/interfaces/i2c_interface.c
    src/interfaces/spi_interface.c
    src/recorder/recorder.c
    src/fault_inject/fault_inject.c
    src/fault_inject/fault_inject_trigger.c
    src/monitor/monitor.c
    src/pio/pio_monitor.c
)

target_include_directories(pico_dev_kit PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(pico_dev_kit
    pico_stdlib
    pico_unique_id
    hardware_i2c
    hardware_spi
    hardware_gpio
    hardware_timer
    hardware_dma
    hardware_pio
    pico_rand
    tinyusb_device
    tinyusb_board
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(pico_dev_kit 0)
pico_enable_stdio_uart(pico_dev_kit 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_dev_kit)
