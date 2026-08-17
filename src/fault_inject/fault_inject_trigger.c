/**
 * @file fault_inject_trigger.c
 * @brief PIO-monitored triggered fault injection
 *
 * Pattern matching is done inside a PIO monitor frame callback (main-loop
 * context, not ISR).  When a match is detected a pending-injection flag is
 * set; fault_inject_trigger_task() performs the actual bus write in the
 * main loop to avoid re-entrancy issues with the interface drivers.
 */

#include "fault_inject_trigger.h"
#include "fault_inject.h"
#include "pio_monitor.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static bool                       s_armed;
static fault_inject_trigger_config_t s_cfg;
static uint32_t                   s_match_count;

/*
 * Pending injection flag set by the frame callback, consumed by
 * fault_inject_trigger_task().  Using a flag instead of injecting directly
 * from the callback avoids recursive calls into the interface drivers.
 */
static volatile bool s_pending;

/* -------------------------------------------------------------------------
 * Pattern matching helpers
 * ---------------------------------------------------------------------- */

/*
 * Return true if pattern[0..pattern_len-1] appears in data[0..len-1]
 * starting at or after start_offset.
 *
 * When start_offset is 0 the pattern may appear at any byte position.
 * When start_offset > 0 the match is anchored to that exact byte index.
 */
static bool pattern_matches(const uint8_t *data, uint16_t len,
                             const uint8_t *pattern, uint16_t pattern_len,
                             uint16_t start_offset) {
    if (pattern_len == 0 || pattern_len > len) return false;

    if (start_offset > 0) {
        /* Anchored match: pattern must begin exactly at start_offset */
        if ((uint16_t)(start_offset + pattern_len) > len) return false;
        return memcmp(data + start_offset, pattern, pattern_len) == 0;
    }

    /* Sliding search: pattern may start anywhere in the frame */
    uint16_t last = (uint16_t)(len - pattern_len);
    for (uint16_t i = 0; i <= last; i++) {
        if (memcmp(data + i, pattern, pattern_len) == 0) return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * PIO monitor frame callback
 * ---------------------------------------------------------------------- */

static void frame_callback(const frame_t *frame) {
    if (!s_armed || s_pending) return;

    /* Skip sentinel frames (zero-length START/STOP / CS-edge markers) */
    if (frame->length == 0) return;

    /* Only inspect frames from the configured watch interface */
    if (frame->iface != s_cfg.iface) return;

    if (pattern_matches(frame->data, frame->length,
                        s_cfg.pattern, s_cfg.pattern_len,
                        s_cfg.match_offset)) {
        s_pending = true;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void fault_inject_trigger_init(void) {
    s_armed       = false;
    s_pending     = false;
    s_match_count = 0;
    memset(&s_cfg, 0, sizeof(s_cfg));
}

void fault_inject_trigger_set(const fault_inject_trigger_config_t *cfg) {
    if (!cfg || cfg->pattern_len == 0 || cfg->payload_len == 0) return;

    s_cfg     = *cfg;
    s_armed   = true;
    s_pending = false;

    pio_monitor_set_frame_callback(frame_callback);
}

void fault_inject_trigger_disarm(void) {
    s_armed   = false;
    s_pending = false;
    pio_monitor_set_frame_callback(NULL);
}

bool fault_inject_trigger_is_armed(void) {
    return s_armed;
}

void fault_inject_trigger_get_config(fault_inject_trigger_config_t *cfg) {
    if (cfg) {
        *cfg = s_cfg;
    }
}

uint32_t fault_inject_trigger_match_count(void) {
    return s_match_count;
}

void fault_inject_trigger_task(void) {
    if (!s_pending) return;
    s_pending = false;

    s_match_count++;

    /* Fire the injection */
    fault_inject_send(s_cfg.action_iface,
                      s_cfg.payload, s_cfg.payload_len,
                      s_cfg.addr);

    /* Disarm after first match if one-shot mode is selected */
    if (s_cfg.one_shot) {
        fault_inject_trigger_disarm();
    }
}
