/**
 * @file main.c
 * @brief Pico Dev Kit – application entry point
 */

#include "pico/stdlib.h"
#include "tusb.h"
#include "usb_transport.h"
#include "console.h"
#include "i2c_interface.h"
#include "spi_interface.h"
#include "recorder.h"
#include "fault_inject.h"
#include "fault_inject_trigger.h"
#include "monitor.h"

int main(void) {
    /* Initialise USB transport first so other modules can use the console. */
    usb_transport_init();

    /* Wait until the USB host has enumerated the device. */
    while (!tud_cdc_n_connected(0)) {
        usb_transport_task();
        sleep_ms(10);
    }

    /* Initialise application modules. */
    recorder_init();
    fault_inject_init();
    fault_inject_trigger_init();
    monitor_init();
    console_init();

    /* Main loop: service all modules. */
    while (true) {
        usb_transport_task();
        console_task();
        i2c_interface_task();
        spi_interface_task();
        recorder_task();
        monitor_task();
        fault_inject_trigger_task();
    }

    return 0;
}
