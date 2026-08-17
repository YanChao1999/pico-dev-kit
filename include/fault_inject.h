/**
 * @file fault_inject.h
 * @brief Fault injection – inject crafted frames onto an interface
 *
 * Allows the host (via the USB console) to send arbitrary byte sequences
 * on the configured interface so that the system under test can be
 * stressed (robustness / fault-tolerance testing).
 *
 * Extended configuration (fault_inject_config_t) lets the host control:
 *   - repeat count and inter-repeat delay
 *   - bit-flip mask (XOR applied per byte) with a configurable start offset
 *   - quiescent delays before and after the injection burst
 */

#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#include <stdint.h>
#include <stdbool.h>
#include "pico_dev_kit.h"

/* -------------------------------------------------------------------------
 * Injection configuration
 * ---------------------------------------------------------------------- */

/**
 * Persistent configuration applied by every fault_inject_send() call.
 *
 * All fields have zero-value defaults that reproduce the original behaviour:
 *   repeat=0, repeat_delay_ms=0, bit_flip_mask=0, byte_offset=0,
 *   pre_delay_ms=0, post_delay_ms=0, random_payload=false.
 */
typedef struct {
    /** Number of *extra* transmissions after the first (0 = send once). */
    uint16_t repeat;

    /** Milliseconds to wait between consecutive repeat transmissions. */
    uint16_t repeat_delay_ms;

    /**
     * Bitmask XORed into every byte at index >= byte_offset before sending.
     * 0x00 disables bit-flipping.
     */
    uint8_t  bit_flip_mask;

    /**
     * Byte index from which bit_flip_mask is applied (0 = all bytes).
     * Bytes before this offset are sent unmodified.
     */
    uint16_t byte_offset;

    /** Quiescent delay (ms) inserted before the first transmission. */
    uint16_t pre_delay_ms;

    /** Quiescent delay (ms) inserted after the last transmission. */
    uint16_t post_delay_ms;

    /**
     * When true, fault_inject_send() re-randomises the working copy of the
     * payload on every repeat iteration (including the first).  The caller's
     * original buffer is never modified.  The len parameter still controls
     * how many random bytes are generated.
     */
    bool random_payload;
    uint16_t post_delay_ms;
} fault_inject_config_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/** Initialise the fault-inject module (resets config to defaults). */
void fault_inject_init(void);

/**
 * Update the active injection configuration.
 * A NULL pointer resets all fields to their zero-value defaults.
 */
void fault_inject_configure(const fault_inject_config_t *cfg);

/** Copy the active configuration into *cfg. */
void fault_inject_get_config(fault_inject_config_t *cfg);

/**
 * Inject a raw byte sequence on the given interface.
 *
 * The active fault_inject_config_t is honoured:
 *   - pre_delay_ms is observed before the first transmission
 *   - bit_flip_mask is XORed into bytes at index >= byte_offset
 *   - the frame is transmitted (repeat + 1) times total
 *   - repeat_delay_ms is observed between successive transmissions
 *   - post_delay_ms is observed after the last transmission
 *
 * @param iface  Target interface (IFACE_I2C or IFACE_SPI)
 * @param data   Bytes to transmit (not modified; a working copy is used)
 * @param len    Number of bytes
 * @param addr   I2C target address (ignored for SPI)
 * @return       0 on success, negative on first error encountered
 */
int fault_inject_send(interface_id_t iface,
                      const uint8_t *data, size_t len,
                      uint8_t addr);

/** Return the total number of injection operations performed. */
uint32_t fault_inject_count(void);

/**
 * Generate a random byte sequence of the given length and inject it.
 *
 * The active fault_inject_config_t (pre/post delay, repeat, repeat_delay)
 * is honoured.  bit_flip_mask is applied on top of the random bytes when
 * non-zero.  random_payload in the config is implicitly true for this call.
 *
 * @param iface  Target interface (IFACE_I2C or IFACE_SPI)
 * @param len    Number of random bytes to generate and send (1..FRAME_DATA_MAX)
 * @param addr   I2C target address (ignored for SPI)
 * @return       0 on success, negative on first error encountered
 */
int fault_inject_send_random(interface_id_t iface, size_t len, uint8_t addr);

#endif /* FAULT_INJECT_H */
