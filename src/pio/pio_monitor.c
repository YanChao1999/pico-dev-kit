/**
 * @file pio_monitor.c
 * @brief PIO-based passive bus monitor (I2C and SPI)
 *
 * This file glues the generated PIO programs (from i2c_monitor.pio and
 * spi_monitor.pio) to the recorder so that passively-captured bus traffic
 * is automatically streamed over USB to the host.
 *
 * Generated headers included here:
 *   i2c_monitor.pio.h  – produced by pioasm from src/pio/i2c_monitor.pio
 *   spi_monitor.pio.h  – produced by pioasm from src/pio/spi_monitor.pio
 */

#include "pio_monitor.h"
#include "recorder.h"
#include "i2c_interface.h"   /* for pin defines */
#include "spi_interface.h"   /* for pin defines */

#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/time.h"

/* Generated PIO headers (created by pioasm via CMake) */
#include "i2c_monitor.pio.h"
#include "spi_monitor.pio.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Per-interface state
 * ---------------------------------------------------------------------- */
typedef struct {
    bool    active;
    PIO     pio;
    uint    sm_data;     /* data-sampler state machine  */
    uint    sm_event;    /* start/stop or CS state machine */
    uint    offset_data;
    uint    offset_event;
} pio_mon_state_t;

static pio_mon_state_t s_i2c;
static pio_mon_state_t s_spi;

/* Accumulators shared by data drain and event (START/STOP / CS) handlers */
static uint8_t  s_i2c_buf[FRAME_DATA_MAX];
static uint16_t s_i2c_count;
static uint8_t  s_spi_buf[FRAME_DATA_MAX];
static uint16_t s_spi_count;

/* Optional callback invoked for every decoded frame (before recorder push) */
static void (*s_frame_cb)(const frame_t *) = NULL;

/* -------------------------------------------------------------------------
 * Internal helper: deliver a decoded frame to callback + recorder
 * ---------------------------------------------------------------------- */
static void deliver_frame(const frame_t *f) {
    if (s_frame_cb) {
        s_frame_cb(f);
    }
    recorder_push(f);
}

static void flush_buf(interface_id_t iface, uint8_t *buf, uint16_t *count) {
    if (*count == 0) {
        return;
    }
    frame_t frame;
    frame.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
    frame.iface  = iface;
    frame.role   = ROLE_MONITOR;
    frame.length = *count;
    memcpy(frame.data, buf, *count);
    deliver_frame(&frame);
    *count = 0;
}

static void emit_sentinel(interface_id_t iface) {
    frame_t frame;
    frame.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
    frame.iface  = iface;
    frame.role   = ROLE_MONITOR;
    frame.length = 0;
    deliver_frame(&frame);
}

/* -------------------------------------------------------------------------
 * I2C monitor helpers
 * ---------------------------------------------------------------------- */

/*
 * The I2C data SM shifts 9 bits per push:
 *   bits[8:1] = data byte (MSB first)
 *   bit[0]    = ACK (0) / NACK (1)
 *
 * We unpack 8 data bits and the ACK flag and record the byte.
 */
static void i2c_drain_data(void) {
    while (!pio_sm_is_rx_fifo_empty(s_i2c.pio, s_i2c.sm_data)) {
        uint32_t word = pio_sm_get(s_i2c.pio, s_i2c.sm_data);
        /*
         * ISR shift-in is MSB-first, autopush at 9 bits into the top bits
         * of the 32-bit word.  The layout after autopush:
         *   bits[31:23] = 9 captured bits (bit 31 = first captured = MSB of byte)
         *   bits[22:0]  = undefined (zeroed by shift logic)
         *
         * Extract: data = word >> 23, ack = (word >> 23) & 1
         */
        uint16_t raw  = (uint16_t)(word >> 23);
        uint8_t  byte = (uint8_t)(raw >> 1);
        /* uint8_t ack  = (uint8_t)(raw & 1); */  /* available if needed */

        if (s_i2c_count < FRAME_DATA_MAX) {
            s_i2c_buf[s_i2c_count++] = byte;
        }

        /* Flush a frame whenever the buffer is full */
        if (s_i2c_count == FRAME_DATA_MAX) {
            flush_buf(IFACE_I2C, s_i2c_buf, &s_i2c_count);
        }
    }
}

/*
 * START/STOP SM pushes 2-bit words: {SCL, SDA}.
 * When SCL=1 and SDA transitions: START (1→0) or STOP (0→1).
 * Flush any buffered bytes on each START/STOP, then emit a sentinel.
 */
static void i2c_drain_events(void) {
    while (!pio_sm_is_rx_fifo_empty(s_i2c.pio, s_i2c.sm_event)) {
        uint32_t word = pio_sm_get_blocking(s_i2c.pio, s_i2c.sm_event);
        (void)word; /* condition type decoded by C if needed */

        /* Pull any bytes still sitting in the data FIFO first */
        i2c_drain_data();
        flush_buf(IFACE_I2C, s_i2c_buf, &s_i2c_count);
        emit_sentinel(IFACE_I2C);
    }
}

/* -------------------------------------------------------------------------
 * SPI monitor helpers
 * ---------------------------------------------------------------------- */

/*
 * Each 32-bit autopush word contains 8 nibbles (one per SCK edge).
 * Within a nibble after shift-left IN PINS,4 from base=MISO:
 *   bit3=MISO, bit2=CS, bit1=SCK, bit0=MOSI
 * Frame data = [MOSI_byte0, MISO_byte0, MOSI_byte1, MISO_byte1, …]
 */
static void spi_drain_data(void) {
    while (!pio_sm_is_rx_fifo_empty(s_spi.pio, s_spi.sm_data)) {
        uint32_t word = pio_sm_get(s_spi.pio, s_spi.sm_data);

        uint8_t mosi = 0, miso = 0;
        for (int i = 7; i >= 0; i--) {
            uint8_t nibble = (uint8_t)((word >> (4 * i)) & 0xF);
            mosi = (uint8_t)((mosi << 1) | (nibble & 1));
            miso = (uint8_t)((miso << 1) | ((nibble >> 3) & 1));
        }

        if (s_spi_count + 2 <= FRAME_DATA_MAX) {
            s_spi_buf[s_spi_count++] = mosi;
            s_spi_buf[s_spi_count++] = miso;
        }

        if (s_spi_count >= FRAME_DATA_MAX - 1) {
            flush_buf(IFACE_SPI, s_spi_buf, &s_spi_count);
        }
    }
}

/* CS events flush the current byte accumulator, then emit a sentinel */
static void spi_drain_cs(void) {
    while (!pio_sm_is_rx_fifo_empty(s_spi.pio, s_spi.sm_event)) {
        pio_sm_get(s_spi.pio, s_spi.sm_event); /* consume */

        spi_drain_data();
        flush_buf(IFACE_SPI, s_spi_buf, &s_spi_count);
        emit_sentinel(IFACE_SPI);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
int pio_monitor_init(interface_id_t iface) {
    if (iface == IFACE_I2C) {
        if (s_i2c.active) return 0;  /* already running */

        /* Data SM waits on IN_BASE+1 for SCL – pins must be consecutive */
        if (I2C_SCL_PIN != I2C_SDA_PIN + 1) {
            return -1;
        }

        PIO pio = pio0;

        /* Claim state machines */
        int sm_data  = pio_claim_unused_sm(pio, false);
        int sm_event = pio_claim_unused_sm(pio, false);
        if (sm_data < 0 || sm_event < 0) {
            if (sm_data  >= 0) pio_sm_unclaim(pio, (uint)sm_data);
            if (sm_event >= 0) pio_sm_unclaim(pio, (uint)sm_event);
            return -1;
        }

        /* Load PIO programs */
        uint off_data  = pio_add_program(pio, &i2c_monitor_data_program);
        uint off_event = pio_add_program(pio, &i2c_monitor_startstop_program);

        /* Configure pins as inputs (no pull – external pull-ups on I2C) */
        gpio_set_dir(I2C_SDA_PIN, GPIO_IN);
        gpio_set_dir(I2C_SCL_PIN, GPIO_IN);

        /* Initialise state machines */
        i2c_monitor_data_program_init(pio, (uint)sm_data, off_data,
                                      I2C_SDA_PIN);
        i2c_monitor_startstop_program_init(pio, (uint)sm_event, off_event,
                                           I2C_SDA_PIN);

        s_i2c_count        = 0;
        s_i2c.active       = true;
        s_i2c.pio          = pio;
        s_i2c.sm_data      = (uint)sm_data;
        s_i2c.sm_event     = (uint)sm_event;
        s_i2c.offset_data  = off_data;
        s_i2c.offset_event = off_event;
        return 0;

    } else if (iface == IFACE_SPI) {
        if (s_spi.active) return 0;

        /*
         * Data SM expects a contiguous block:
         *   RX (MISO), CS, SCK, TX (MOSI)  — Pico spi0 defaults.
         */
        if (SPI_CS_PIN  != SPI_RX_PIN + 1 ||
            SPI_SCK_PIN != SPI_RX_PIN + 2 ||
            SPI_TX_PIN  != SPI_RX_PIN + 3) {
            return -1;
        }

        PIO pio = pio1;  /* use pio1 to avoid conflicts with I2C monitor */

        int sm_data  = pio_claim_unused_sm(pio, false);
        int sm_event = pio_claim_unused_sm(pio, false);
        if (sm_data < 0 || sm_event < 0) {
            if (sm_data  >= 0) pio_sm_unclaim(pio, (uint)sm_data);
            if (sm_event >= 0) pio_sm_unclaim(pio, (uint)sm_event);
            return -1;
        }

        uint off_data  = pio_add_program(pio, &spi_monitor_data_program);
        uint off_event = pio_add_program(pio, &spi_monitor_cs_program);

        gpio_set_dir(SPI_RX_PIN,  GPIO_IN);
        gpio_set_dir(SPI_TX_PIN,  GPIO_IN);
        gpio_set_dir(SPI_SCK_PIN, GPIO_IN);
        gpio_set_dir(SPI_CS_PIN,  GPIO_IN);

        spi_monitor_data_program_init(pio, (uint)sm_data, off_data,
                                      SPI_RX_PIN, SPI_SCK_PIN, SPI_TX_PIN);
        spi_monitor_cs_program_init(pio, (uint)sm_event, off_event,
                                    SPI_CS_PIN);

        s_spi_count        = 0;
        s_spi.active       = true;
        s_spi.pio          = pio;
        s_spi.sm_data      = (uint)sm_data;
        s_spi.sm_event     = (uint)sm_event;
        s_spi.offset_data  = off_data;
        s_spi.offset_event = off_event;
        return 0;
    }

    return -1;
}

void pio_monitor_deinit(interface_id_t iface) {
    pio_mon_state_t *s = (iface == IFACE_I2C) ? &s_i2c : &s_spi;
    if (!s->active) return;

    pio_sm_set_enabled(s->pio, s->sm_data,  false);
    pio_sm_set_enabled(s->pio, s->sm_event, false);
    pio_sm_unclaim(s->pio, s->sm_data);
    pio_sm_unclaim(s->pio, s->sm_event);
    pio_remove_program(s->pio, (iface == IFACE_I2C)
                                ? &i2c_monitor_data_program
                                : &spi_monitor_data_program,
                       s->offset_data);
    pio_remove_program(s->pio, (iface == IFACE_I2C)
                                ? &i2c_monitor_startstop_program
                                : &spi_monitor_cs_program,
                       s->offset_event);
    s->active = false;

    if (iface == IFACE_I2C) {
        s_i2c_count = 0;
    } else {
        s_spi_count = 0;
    }
}

void pio_monitor_task(interface_id_t iface) {
    if (iface == IFACE_I2C && s_i2c.active) {
        i2c_drain_data();
        i2c_drain_events();
    } else if (iface == IFACE_SPI && s_spi.active) {
        spi_drain_data();
        spi_drain_cs();
    }
}

bool pio_monitor_is_active(interface_id_t iface) {
    return (iface == IFACE_I2C) ? s_i2c.active : s_spi.active;
}

void pio_monitor_set_frame_callback(void (*cb)(const frame_t *frame)) {
    s_frame_cb = cb;
}
