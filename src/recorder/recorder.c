/**
 * @file recorder.c
 * @brief Frame recorder – ring buffer → USB data port
 *
 * Wire format per frame (little-endian):
 *   [4]  magic      0x50 0x44 0x4B 0x46  ("PDKF")
 *   [4]  timestamp_us
 *   [1]  iface
 *   [1]  role
 *   [2]  length
 *   [N]  data
 */

#include "recorder.h"
#include "usb_transport.h"
#include "pico/critical_section.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * Ring buffer of serialised packets
 * ---------------------------------------------------------------------- */
#define RING_SIZE  (8192u)  /* must be a power of 2 */

static uint8_t  s_ring[RING_SIZE];
static uint32_t s_head;    /* written by recorder_push (may be IRQ) */
static uint32_t s_tail;    /* read  by recorder_task  (main loop)   */

static critical_section_t s_cs;

static bool     s_active;
static uint32_t s_frame_count;

/* Serialise magic header */
static const uint8_t MAGIC[4] = {0x50, 0x44, 0x4B, 0x46};

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */
static uint32_t ring_free(void) {
    return (RING_SIZE - 1) - ((s_head - s_tail) & (RING_SIZE - 1));
}

static void ring_write_byte(uint8_t b) {
    s_ring[s_head & (RING_SIZE - 1)] = b;
    s_head++;
}

static void ring_write(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ring_write_byte(data[i]);
    }
}

static void ring_write_u16le(uint16_t v) {
    ring_write_byte((uint8_t)(v & 0xFF));
    ring_write_byte((uint8_t)(v >> 8));
}

static void ring_write_u32le(uint32_t v) {
    ring_write_byte((uint8_t)(v & 0xFF));
    ring_write_byte((uint8_t)((v >> 8) & 0xFF));
    ring_write_byte((uint8_t)((v >> 16) & 0xFF));
    ring_write_byte((uint8_t)((v >> 24) & 0xFF));
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void recorder_init(void) {
    critical_section_init(&s_cs);
    s_head = s_tail = 0;
    s_active = false;
    s_frame_count = 0;
}

void recorder_start(void) {
    s_frame_count = 0;
    s_active = true;
}

void recorder_stop(void) {
    s_active = false;
}

bool recorder_is_active(void) {
    return s_active;
}

void recorder_push(const frame_t *frame) {
    if (!s_active) return;

    /* Packet size: 4 + 4 + 1 + 1 + 2 + data */
    size_t pkt_size = 4 + 4 + 1 + 1 + 2 + frame->length;

    critical_section_enter_blocking(&s_cs);

    if (ring_free() >= pkt_size) {
        ring_write(MAGIC, 4);
        ring_write_u32le(frame->timestamp_us);
        ring_write_byte((uint8_t)frame->iface);
        ring_write_byte((uint8_t)frame->role);
        ring_write_u16le(frame->length);
        ring_write(frame->data, frame->length);
        s_frame_count++;
    }
    /* else: silently drop if ring is full */

    critical_section_exit(&s_cs);
}

void recorder_task(void) {
    /* Drain available data to USB in chunks */
    while (s_tail != s_head) {
        /* Build a local chunk to avoid byte-at-a-time USB writes */
        uint8_t  chunk[64];
        uint32_t n = 0;

        critical_section_enter_blocking(&s_cs);
        while (s_tail != s_head && n < sizeof(chunk)) {
            chunk[n++] = s_ring[s_tail & (RING_SIZE - 1)];
            s_tail++;
        }
        critical_section_exit(&s_cs);

        usb_data_write(chunk, n);
    }
}

uint32_t recorder_frame_count(void) {
    return s_frame_count;
}
