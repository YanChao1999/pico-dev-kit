/**
 * @file usb_transport.h
 * @brief USB CDC dual-port transport layer
 *
 * Port 0 – interactive console (text commands)
 * Port 1 – binary data stream (recorded frames sent to host)
 */

#ifndef USB_TRANSPORT_H
#define USB_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Initialise TinyUSB and both CDC interfaces. */
void usb_transport_init(void);

/* Must be called regularly from the main loop to drive USB tasks. */
void usb_transport_task(void);

/* ---- Console port (CDC 0) -------------------------------------------- */

/**
 * Write a NUL-terminated string to the console port.
 * Returns number of bytes actually written.
 */
uint32_t usb_console_write(const char *str);

/**
 * Write formatted output to the console port (printf-style).
 */
void usb_console_printf(const char *fmt, ...);

/**
 * Read bytes from the console port into buf (non-blocking).
 * Returns number of bytes read (0 if nothing available).
 */
uint32_t usb_console_read(uint8_t *buf, uint32_t max_len);

/* ---- Data port (CDC 1) ----------------------------------------------- */

/**
 * Send a binary buffer on the data port.
 * Returns number of bytes actually written.
 */
uint32_t usb_data_write(const uint8_t *buf, uint32_t len);

#endif /* USB_TRANSPORT_H */
