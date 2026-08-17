/**
 * @file fault_inject_trigger.h
 * @brief PIO-monitored triggered fault injection
 *
 * This module connects the PIO passive bus monitor to the fault injection
 * engine.  When armed, it watches the decoded frame stream from
 * pio_monitor_task() for a configurable byte pattern and automatically fires
 * a prepared injection payload whenever the pattern is matched.
 *
 * Typical usage:
 *   1. Configure the I2C or SPI interface in ROLE_MONITOR.
 *   2. Populate a fault_inject_trigger_config_t and call
 *      fault_inject_trigger_set().
 *   3. Call fault_inject_trigger_task() from the main loop (after
 *      pio_monitor_task() for the watched interface).
 *   4. Optionally call fault_inject_trigger_disarm() to cancel.
 */

#ifndef FAULT_INJECT_TRIGGER_H
#define FAULT_INJECT_TRIGGER_H

#include <stdint.h>
#include <stdbool.h>
#include "pico_dev_kit.h"

/* -------------------------------------------------------------------------
 * Trigger configuration
 * ---------------------------------------------------------------------- */

typedef struct {
    /** Interface whose PIO monitor output is inspected for the pattern. */
    interface_id_t  iface;

    /**
     * Byte sequence to match against incoming monitored frames.
     * Matching is performed against the decoded byte payload; zero-length
     * sentinel frames (START/STOP / CS edges) are ignored.
     */
    uint8_t         pattern[FRAME_DATA_MAX];

    /** Number of valid bytes in pattern[] (0 disarms the trigger). */
    uint16_t        pattern_len;

    /**
     * Byte offset within a monitored frame at which matching starts.
     * 0 means the pattern must appear anywhere within the frame.
     * A non-zero value anchors the match to that offset.
     */
    uint16_t        match_offset;

    /** Interface on which to inject when the pattern is matched. */
    interface_id_t  action_iface;

    /** Bytes to transmit when the pattern is matched. */
    uint8_t         payload[FRAME_DATA_MAX];

    /** Number of valid bytes in payload[]. */
    uint16_t        payload_len;

    /** I2C target address for the injected payload (ignored for SPI). */
    uint8_t         addr;

    /**
     * When true the trigger automatically disarms after the first match.
     * When false it re-fires on every matching frame until explicitly
     * disarmed with fault_inject_trigger_disarm().
     */
    bool            one_shot;
} fault_inject_trigger_config_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/** Initialise the trigger module (called once at startup). */
void fault_inject_trigger_init(void);

/**
 * Arm the trigger with the given configuration.
 *
 * Registers a frame callback with pio_monitor_set_frame_callback() so that
 * decoded frames are inspected as they arrive.  Any previously armed trigger
 * is replaced.
 *
 * @param cfg  Trigger configuration (copied internally).
 */
void fault_inject_trigger_set(const fault_inject_trigger_config_t *cfg);

/**
 * Disarm the trigger without firing.
 * The PIO monitor frame callback is deregistered.
 */
void fault_inject_trigger_disarm(void);

/** Return true while a trigger is armed. */
bool fault_inject_trigger_is_armed(void);

/**
 * Copy the current trigger configuration into *cfg.
 * Valid whether or not the trigger is armed.
 */
void fault_inject_trigger_get_config(fault_inject_trigger_config_t *cfg);

/** Return the total number of times the trigger has fired. */
uint32_t fault_inject_trigger_match_count(void);

/**
 * Service the trigger module; must be called from the main loop, after
 * pio_monitor_task() for the watched interface.
 *
 * Pattern matching is performed inside the frame callback registered with
 * pio_monitor, so this function only handles any deferred injection work
 * that must be serialised to the main loop (e.g., actual bus transmission).
 */
void fault_inject_trigger_task(void);

#endif /* FAULT_INJECT_TRIGGER_H */
