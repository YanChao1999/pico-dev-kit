/**
 * @file monitor.c
 * @brief Interface health monitor – periodic checks and error logging
 */

#include "monitor.h"
#include "usb_transport.h"
#include "i2c_interface.h"
#include "spi_interface.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

/* Health-check interval */
#define MONITOR_INTERVAL_MS  1000

static bool            s_active;
static uint32_t        s_error_count;
static absolute_time_t s_next_check;

/* Lightweight error log (last N errors) */
#define ERROR_LOG_ENTRIES  8
#define ERROR_DESC_LEN     48

static struct {
    uint32_t timestamp_us;
    char     desc[ERROR_DESC_LEN];
} s_error_log[ERROR_LOG_ENTRIES];
static uint32_t s_log_head;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void monitor_init(void) {
    s_active = false;
    s_error_count = 0;
    s_log_head = 0;
    s_next_check = get_absolute_time();
    memset(s_error_log, 0, sizeof(s_error_log));
}

void monitor_start(void) {
    s_active = true;
    s_error_count = 0;
    s_log_head = 0;
    s_next_check = make_timeout_time_ms(MONITOR_INTERVAL_MS);
}

void monitor_stop(void) {
    s_active = false;
}

bool monitor_is_active(void) {
    return s_active;
}

void monitor_record_error(const char *description) {
    /* Safe to call from interrupt context */
    uint32_t idx = s_log_head % ERROR_LOG_ENTRIES;
    s_error_log[idx].timestamp_us =
        (uint32_t)to_us_since_boot(get_absolute_time());
    strncpy(s_error_log[idx].desc, description, ERROR_DESC_LEN - 1);
    s_error_log[idx].desc[ERROR_DESC_LEN - 1] = '\0';
    s_log_head++;
    s_error_count++;
}

uint32_t monitor_error_count(void) {
    return s_error_count;
}

void monitor_task(void) {
    if (!s_active) return;
    if (!time_reached(s_next_check)) return;

    s_next_check = make_timeout_time_ms(MONITOR_INTERVAL_MS);

    /* Basic liveness checks */
    bool i2c_ok = i2c_interface_is_active();
    bool spi_ok = spi_interface_is_active();

    usb_console_printf("[monitor] I2C:%s SPI:%s errors:%lu\r\n",
                       i2c_ok ? "up" : "down",
                       spi_ok ? "up" : "down",
                       (unsigned long)s_error_count);

    /* Print recent errors (if any new ones since last check) */
    uint32_t entries = s_error_count < ERROR_LOG_ENTRIES
                       ? s_error_count : ERROR_LOG_ENTRIES;
    if (entries > 0) {
        for (uint32_t i = 0; i < entries; i++) {
            uint32_t idx = (s_log_head - entries + i) % ERROR_LOG_ENTRIES;
            usb_console_printf("  [err] t=%luus %s\r\n",
                               (unsigned long)s_error_log[idx].timestamp_us,
                               s_error_log[idx].desc);
        }
    }
}
