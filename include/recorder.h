/**
 * @file recorder.h
 * @brief Frame recorder – captures interface traffic and streams it to USB
 *
 * When recording is active every frame_t pushed via recorder_push() is
 * serialised and sent over the USB data port (CDC 1) so the host can
 * decode and summarise the traffic.
 *
 * Wire format (little-endian):
 *   [4] magic  0x50 0x44 0x4B 0x46  ("PDKF")
 *   [4] timestamp_us
 *   [1] iface  (interface_id_t)
 *   [1] role   (interface_role_t)
 *   [2] length
 *   [N] data   (length bytes)
 */

#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>
#include <stdbool.h>
#include "pico_dev_kit.h"

/** Initialise the recorder (must be called after usb_transport_init). */
void recorder_init(void);

/** Enable / disable frame capture. */
void recorder_start(void);
void recorder_stop(void);

/** Return true while recording is active. */
bool recorder_is_active(void);

/**
 * Push a captured frame into the recorder.
 * If recording is active the frame is serialised and sent to USB.
 * Safe to call from IRQ context (uses a lock-free ring buffer).
 */
void recorder_push(const frame_t *frame);

/** Drain the outgoing ring-buffer; call from the main loop. */
void recorder_task(void);

/** Return number of frames recorded since last start. */
uint32_t recorder_frame_count(void);

#endif /* RECORDER_H */
