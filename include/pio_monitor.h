/**
 * @file pio_monitor.h
 * @brief PIO-based passive bus monitor for I2C and SPI
 *
 * In monitor mode the Pico does NOT drive any bus lines.  Two PIO state
 * machines sample the bus pins and push decoded bytes into a FIFO that
 * pio_monitor_task() drains into the recorder.
 *
 * I2C monitor (two PIO SMs):
 *   SM0 – bit sampler  : samples SDA on every SCL rising edge, auto-pushes
 *                        9-bit words (8 data + 1 ACK) to FIFO.
 *   SM1 – event detect : detects START / STOP conditions.
 *
 * SPI monitor (two PIO SMs):
 *   SM0 – data sampler : samples MOSI+MISO on every SCK rising edge,
 *                        auto-pushes 16-bit words (8 MOSI bits + 8 MISO bits).
 *   SM1 – CS detector  : tracks chip-select to delimit frames.
 *
 * Pin assignments follow the same defaults as the active interfaces; they
 * can be changed by defining the macros before including this header.
 */

#ifndef PIO_MONITOR_H
#define PIO_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "pico_dev_kit.h"

/**
 * Initialise PIO monitor for the given interface.
 *
 * @param iface   IFACE_I2C or IFACE_SPI
 * @return        0 on success, negative on error (e.g. no free PIO SM)
 */
int pio_monitor_init(interface_id_t iface);

/** Stop and release PIO resources. */
void pio_monitor_deinit(interface_id_t iface);

/**
 * Drain PIO FIFOs and push decoded frames to the recorder (and to any
 * registered frame callback).
 * Must be called from the main loop.
 */
void pio_monitor_task(interface_id_t iface);

/** Return true if the given interface's PIO monitor is active. */
bool pio_monitor_is_active(interface_id_t iface);

/**
 * Register a callback invoked for every decoded frame before it is pushed
 * to the recorder.  Pass NULL to deregister.
 *
 * The callback is called from the main-loop context (not from an ISR).
 * Only one callback may be registered at a time; a second call replaces the
 * previous registration.
 *
 * @param cb  Function receiving a pointer to the decoded frame, or NULL.
 */
void pio_monitor_set_frame_callback(void (*cb)(const frame_t *frame));

#endif /* PIO_MONITOR_H */
