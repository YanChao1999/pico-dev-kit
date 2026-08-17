/**
 * @file fault_inject.h
 * @brief Fault injection – inject crafted frames onto an interface
 *
 * Allows the host (via the USB console) to send arbitrary byte sequences
 * on the configured interface so that the system under test can be
 * stressed (robustness / fault-tolerance testing).
 */

#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#include <stdint.h>
#include <stdbool.h>
#include "pico_dev_kit.h"

/** Initialise the fault-inject module. */
void fault_inject_init(void);

/**
 * Inject a raw byte sequence on the given interface.
 *
 * @param iface  Target interface (IFACE_I2C or IFACE_SPI)
 * @param data   Bytes to transmit
 * @param len    Number of bytes
 * @param addr   I2C target address (ignored for SPI)
 * @return       0 on success, negative on error
 */
int fault_inject_send(interface_id_t iface,
                      const uint8_t *data, size_t len,
                      uint8_t addr);

/** Return the total number of injection operations performed. */
uint32_t fault_inject_count(void);

#endif /* FAULT_INJECT_H */
