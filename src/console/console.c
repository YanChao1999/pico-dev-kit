/**
 * @file console.c
 * @brief Interactive text console over USB CDC
 */

#include "console.h"
#include "usb_transport.h"
#include "i2c_interface.h"
#include "spi_interface.h"
#include "recorder.h"
#include "fault_inject.h"
#include "monitor.h"
#include "pio_monitor.h"
#include "pico_dev_kit.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Input line buffer
 * ---------------------------------------------------------------------- */
#define LINE_BUF_SIZE  128
#define MAX_ARGS        16

static char  s_line[LINE_BUF_SIZE];
static int   s_line_pos;

/* -------------------------------------------------------------------------
 * Helper: split line into argc/argv (modifies the buffer in place)
 * ---------------------------------------------------------------------- */
static int split_args(char *line, char **argv, int max_argc) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_argc) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        /* Advance to next space */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

/* -------------------------------------------------------------------------
 * Hex string → byte array helper (returns length, -1 on error)
 * ---------------------------------------------------------------------- */
static int parse_hex_bytes(char **argv, int argc, uint8_t *out, size_t max) {
    int n = 0;
    for (int i = 0; i < argc && (size_t)n < max; i++) {
        char *end;
        unsigned long v = strtoul(argv[i], &end, 16);
        if (end == argv[i] || v > 0xFF) return -1;
        out[n++] = (uint8_t)v;
    }
    return n;
}

/* -------------------------------------------------------------------------
 * Command handlers
 * ---------------------------------------------------------------------- */
static void cmd_help(void) {
    usb_console_write(
        "\r\nPico Dev Kit commands:\r\n"
        "  help\r\n"
        "  version\r\n"
        "  status\r\n"
        "  config i2c  master|slave|monitor [speed_kHz]\r\n"
        "  config spi  master|slave|monitor [speed_kHz] [cpol] [cpha]\r\n"
        "  record start|stop\r\n"
        "  inject i2c <addr_hex> <byte0> [byte1 …]\r\n"
        "  inject spi  <byte0> [byte1 …]\r\n"
        "  monitor start|stop\r\n"
        "\r\nIn monitor mode the PIO passively sniffs the bus without driving any lines.\r\n"
        "\r\n");
}

static void cmd_version(void) {
    usb_console_printf("Pico Dev Kit v%d.%d.%d\r\n",
                       PICO_DEV_KIT_VERSION_MAJOR,
                       PICO_DEV_KIT_VERSION_MINOR,
                       PICO_DEV_KIT_VERSION_PATCH);
}

static void cmd_status(void) {
    usb_console_printf("I2C   : %s\r\n", i2c_interface_is_active() ? "active" : "idle");
    usb_console_printf("SPI   : %s\r\n", spi_interface_is_active() ? "active" : "idle");
    usb_console_printf("Record: %s  frames=%lu\r\n",
                       recorder_is_active() ? "on" : "off",
                       (unsigned long)recorder_frame_count());
    usb_console_printf("Monitor: %s  errors=%lu\r\n",
                       monitor_is_active() ? "on" : "off",
                       (unsigned long)monitor_error_count());
    usb_console_printf("Inject count: %lu\r\n",
                       (unsigned long)fault_inject_count());
}

static void cmd_config(char **argv, int argc) {
    if (argc < 3) { usb_console_write("Usage: config i2c|spi ...\r\n"); return; }

    if (strcmp(argv[1], "i2c") == 0) {
        if (argc < 3) {
            usb_console_write("Usage: config i2c master|slave|monitor [speed_kHz]\r\n");
            return;
        }
        interface_role_t role;
        if (strcmp(argv[2], "slave") == 0)        role = ROLE_SLAVE;
        else if (strcmp(argv[2], "monitor") == 0) role = ROLE_MONITOR;
        else                                       role = ROLE_MASTER;

        uint32_t speed = (argc > 3) ? (uint32_t)atoi(argv[3]) : 0;
        if (speed == 0) speed = 100;

        i2c_interface_deinit();
        i2c_interface_init(role, speed);

        if (role == ROLE_MONITOR) {
            usb_console_write("I2C configured: monitor (PIO passive sniff)\r\n");
        } else {
            usb_console_printf("I2C configured: %s @ %lukHz\r\n",
                               role == ROLE_MASTER ? "master" : "slave",
                               (unsigned long)speed);
        }

    } else if (strcmp(argv[1], "spi") == 0) {
        if (argc < 3) {
            usb_console_write("Usage: config spi master|slave|monitor [speed_kHz] [cpol] [cpha]\r\n");
            return;
        }
        interface_role_t role;
        if (strcmp(argv[2], "slave") == 0)        role = ROLE_SLAVE;
        else if (strcmp(argv[2], "monitor") == 0) role = ROLE_MONITOR;
        else                                       role = ROLE_MASTER;

        uint32_t speed = (argc > 3) ? (uint32_t)atoi(argv[3]) : 0;
        uint8_t cpol = (argc > 4) ? (uint8_t)atoi(argv[4]) : 0;
        uint8_t cpha = (argc > 5) ? (uint8_t)atoi(argv[5]) : 0;
        if (speed == 0) speed = 1000;

        spi_interface_deinit();
        spi_interface_init(role, speed, cpol, cpha);

        if (role == ROLE_MONITOR) {
            usb_console_write("SPI configured: monitor (PIO passive sniff)\r\n");
        } else {
            usb_console_printf("SPI configured: %s @ %lukHz cpol=%d cpha=%d\r\n",
                               role == ROLE_MASTER ? "master" : "slave",
                               (unsigned long)speed, cpol, cpha);
        }
    } else {
        usb_console_write("Unknown interface. Use i2c or spi.\r\n");
    }
}

static void cmd_record(char **argv, int argc) {
    if (argc < 2) { usb_console_write("Usage: record start|stop\r\n"); return; }
    if (strcmp(argv[1], "start") == 0) {
        recorder_start();
        usb_console_write("Recording started.\r\n");
    } else if (strcmp(argv[1], "stop") == 0) {
        recorder_stop();
        usb_console_printf("Recording stopped. Frames captured: %lu\r\n",
                           (unsigned long)recorder_frame_count());
    } else {
        usb_console_write("Usage: record start|stop\r\n");
    }
}

static void cmd_inject(char **argv, int argc) {
    if (argc < 3) {
        usb_console_write("Usage: inject i2c <addr_hex> <byte…>\r\n"
                          "       inject spi <byte…>\r\n");
        return;
    }

    uint8_t buf[FRAME_DATA_MAX];

    if (strcmp(argv[1], "i2c") == 0) {
        /* argv[2] = address, argv[3..] = data bytes */
        char *end;
        unsigned long addr = strtoul(argv[2], &end, 16);
        if (end == argv[2] || addr > 0x7F) {
            usb_console_write("Invalid I2C address.\r\n");
            return;
        }
        int n = parse_hex_bytes(argv + 3, argc - 3, buf, sizeof(buf));
        if (n < 0) { usb_console_write("Invalid hex bytes.\r\n"); return; }
        int rc = fault_inject_send(IFACE_I2C, buf, (size_t)n, (uint8_t)addr);
        usb_console_printf("inject i2c: %s (%d bytes)\r\n",
                           rc == 0 ? "ok" : "error", n);

    } else if (strcmp(argv[1], "spi") == 0) {
        int n = parse_hex_bytes(argv + 2, argc - 2, buf, sizeof(buf));
        if (n < 0) { usb_console_write("Invalid hex bytes.\r\n"); return; }
        int rc = fault_inject_send(IFACE_SPI, buf, (size_t)n, 0);
        usb_console_printf("inject spi: %s (%d bytes)\r\n",
                           rc == 0 ? "ok" : "error", n);

    } else {
        usb_console_write("Unknown interface. Use i2c or spi.\r\n");
    }
}

static void cmd_monitor(char **argv, int argc) {
    if (argc < 2) { usb_console_write("Usage: monitor start|stop\r\n"); return; }
    if (strcmp(argv[1], "start") == 0) {
        monitor_start();
        usb_console_write("Monitor started.\r\n");
    } else if (strcmp(argv[1], "stop") == 0) {
        monitor_stop();
        usb_console_write("Monitor stopped.\r\n");
    } else {
        usb_console_write("Usage: monitor start|stop\r\n");
    }
}

/* -------------------------------------------------------------------------
 * Dispatch
 * ---------------------------------------------------------------------- */
static void dispatch(char *line) {
    char *argv[MAX_ARGS];
    int argc = split_args(line, argv, MAX_ARGS);
    if (argc == 0) return;

    if (strcmp(argv[0], "help")    == 0) { cmd_help(); }
    else if (strcmp(argv[0], "version") == 0) { cmd_version(); }
    else if (strcmp(argv[0], "status")  == 0) { cmd_status(); }
    else if (strcmp(argv[0], "config")  == 0) { cmd_config(argv, argc); }
    else if (strcmp(argv[0], "record")  == 0) { cmd_record(argv, argc); }
    else if (strcmp(argv[0], "inject")  == 0) { cmd_inject(argv, argc); }
    else if (strcmp(argv[0], "monitor") == 0) { cmd_monitor(argv, argc); }
    else {
        usb_console_printf("Unknown command '%s'. Type 'help'.\r\n", argv[0]);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void console_init(void) {
    s_line_pos = 0;
    memset(s_line, 0, sizeof(s_line));
    usb_console_write("\r\nPico Dev Kit ready. Type 'help' for commands.\r\n> ");
}

void console_task(void) {
    uint8_t ch;
    while (usb_console_read(&ch, 1) == 1) {
        if (ch == '\r' || ch == '\n') {
            usb_console_write("\r\n");
            s_line[s_line_pos] = '\0';
            if (s_line_pos > 0) {
                dispatch(s_line);
            }
            s_line_pos = 0;
            usb_console_write("> ");
        } else if ((ch == '\b' || ch == 0x7F) && s_line_pos > 0) {
            s_line_pos--;
            usb_console_write("\b \b");
        } else if (ch >= 0x20 && s_line_pos < (LINE_BUF_SIZE - 1)) {
            s_line[s_line_pos++] = (char)ch;
            /* Echo */
            char echo[2] = {(char)ch, '\0'};
            usb_console_write(echo);
        }
    }
}
