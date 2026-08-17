/**
 * @file fault_inject.c
 * @brief Fault injection – transmit crafted frames on I2C or SPI
 */

#include "fault_inject.h"
#include "i2c_interface.h"
#include "spi_interface.h"
#include "recorder.h"
#include "pico/time.h"
#include "pico/rand.h"
#include <string.h>

static uint32_t              s_inject_count;
static fault_inject_config_t s_config;

void fault_inject_init(void) {
    s_inject_count = 0;
    memset(&s_config, 0, sizeof(s_config));
}

void fault_inject_configure(const fault_inject_config_t *cfg) {
    if (cfg) {
        s_config = *cfg;
    } else {
        memset(&s_config, 0, sizeof(s_config));
    }
}

void fault_inject_get_config(fault_inject_config_t *cfg) {
    if (cfg) {
        *cfg = s_config;
    }
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* Fill buf[0..len-1] with random bytes using the hardware RNG. */
static void fill_random(uint8_t *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint32_t r = get_rand_32();
        size_t chunk = len - i;
        if (chunk > 4) chunk = 4;
        memcpy(buf + i, &r, chunk);
        i += chunk;
    }
}

/* Copy data into buf, optionally re-randomise, then apply bit_flip_mask. */
static void prepare_buf(uint8_t *buf, const uint8_t *data, size_t len,
                        bool randomise) {
    if (randomise) {
        fill_random(buf, len);
    } else {
        memcpy(buf, data, len);
    }
    if (s_config.bit_flip_mask != 0) {
        for (size_t i = s_config.byte_offset; i < len; i++) {
            buf[i] ^= s_config.bit_flip_mask;
        }
    }
}

/* -------------------------------------------------------------------------
 * Shared transmission loop (used by both send variants)
 * ---------------------------------------------------------------------- */
static int do_send(interface_id_t iface, const uint8_t *data, size_t len,
                   uint8_t addr, bool randomise) {
    if (len == 0 || len > FRAME_DATA_MAX) return -1;

    uint8_t buf[FRAME_DATA_MAX];

    if (s_config.pre_delay_ms > 0) {
        sleep_ms(s_config.pre_delay_ms);
    }

    int rc = 0;
    uint16_t total = (uint16_t)(s_config.repeat + 1);

    for (uint16_t i = 0; i < total; i++) {
        if (i > 0 && s_config.repeat_delay_ms > 0) {
            sleep_ms(s_config.repeat_delay_ms);
        }

        /* Prepare working buffer (re-randomise every iteration when set) */
        prepare_buf(buf, data, len, randomise || s_config.random_payload);

        int res;
        switch (iface) {
            case IFACE_I2C:
                res = i2c_master_write(addr, buf, len);
                break;
            case IFACE_SPI:
                res = spi_master_transfer(buf, NULL, len);
                break;
            default:
                return -1;
        }

        if (res != 0 && rc == 0) {
            rc = res; /* capture first error but complete remaining repeats */
        }

        if (res == 0) {
            s_inject_count++;
            /* Individual interface modules already push to the recorder. */
        }
    }

    if (s_config.post_delay_ms > 0) {
        sleep_ms(s_config.post_delay_ms);
    }

    return rc;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int fault_inject_send(interface_id_t iface,
                      const uint8_t *data, size_t len,
                      uint8_t addr) {
    if (!data || len == 0 || len > FRAME_DATA_MAX) return -1;
    return do_send(iface, data, len, addr, false);
}

int fault_inject_send_random(interface_id_t iface, size_t len, uint8_t addr) {
    if (len == 0 || len > FRAME_DATA_MAX) return -1;
    return do_send(iface, NULL, len, addr, true);
}

uint32_t fault_inject_count(void) {
    return s_inject_count;
}
