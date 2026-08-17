/**
 * @file console.h
 * @brief Interactive console – parses text commands from the USB console port
 *
 * Supported commands
 * ------------------
 *   help                    – list available commands
 *   version                 – print firmware version
 *   config i2c master|slave <speed_kHz>
 *   config spi  master|slave <speed_kHz> <cpol> <cpha>
 *   record start|stop
 *   inject  i2c|spi <hex_bytes…>
 *   monitor start|stop
 *   status                  – print current configuration and stats
 */

#ifndef CONSOLE_H
#define CONSOLE_H

/* Initialise the console module (must be called after usb_transport_init). */
void console_init(void);

/* Process any pending console input; must be called from the main loop. */
void console_task(void);

#endif /* CONSOLE_H */
