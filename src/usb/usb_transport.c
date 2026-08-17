/**
 * @file usb_transport.c
 * @brief USB CDC dual-port transport layer (TinyUSB back-end)
 *
 * CDC interface 0  – interactive console (text)
 * CDC interface 1  – binary data stream  (recorded frames)
 */

#include "usb_transport.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * TinyUSB configuration is provided by tusb_config.h (project root or
 * an include path directory).  The file must define:
 *   CFG_TUD_CDC   2   (two CDC interfaces)
 * ---------------------------------------------------------------------- */

#define CONSOLE_PORT  0
#define DATA_PORT     1

/* Small printf buffer */
#define PRINTF_BUF_SIZE  256

void usb_transport_init(void) {
    tusb_init();
}

void usb_transport_task(void) {
    tud_task();
}

/* ---- Console port -------------------------------------------------------- */

uint32_t usb_console_write(const char *str) {
    if (!tud_cdc_n_connected(CONSOLE_PORT)) {
        return 0;
    }
    uint32_t len = (uint32_t)strlen(str);
    uint32_t written = tud_cdc_n_write(CONSOLE_PORT, str, len);
    tud_cdc_n_write_flush(CONSOLE_PORT);
    return written;
}

void usb_console_printf(const char *fmt, ...) {
    char buf[PRINTF_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    usb_console_write(buf);
}

uint32_t usb_console_read(uint8_t *buf, uint32_t max_len) {
    if (!tud_cdc_n_available(CONSOLE_PORT)) {
        return 0;
    }
    return tud_cdc_n_read(CONSOLE_PORT, buf, max_len);
}

/* ---- Data port ----------------------------------------------------------- */

uint32_t usb_data_write(const uint8_t *buf, uint32_t len) {
    if (!tud_cdc_n_connected(DATA_PORT)) {
        return 0;
    }
    uint32_t written = tud_cdc_n_write(DATA_PORT, buf, len);
    tud_cdc_n_write_flush(DATA_PORT);
    return written;
}
