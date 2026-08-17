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
#include "fault_inject_trigger.h"
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
#define MAX_ARGS        32

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
        "  inject i2c <addr_hex> <byte0> [byte1 …] [repeat N] [delay MS] [flip MASK]\r\n"
        "  inject spi  <byte0> [byte1 …]              [repeat N] [delay MS] [flip MASK]\r\n"
        "  inject config [repeat N] [delay MS] [flip MASK] [pre MS] [post MS]\r\n"
        "  inject config show\r\n"
        "  inject trigger set i2c|spi pattern <hex…> payload <hex…> [addr HH] [once]\r\n"
        "  inject trigger disarm\r\n"
        "  inject trigger show\r\n"
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
    usb_console_printf("PIO monitor I2C: %s\r\n",
                       pio_monitor_is_active(IFACE_I2C) ? "active" : "inactive");
    usb_console_printf("PIO monitor SPI: %s\r\n",
                       pio_monitor_is_active(IFACE_SPI) ? "active" : "inactive");
    usb_console_printf("Record: %s  frames=%lu\r\n",
                       recorder_is_active() ? "on" : "off",
                       (unsigned long)recorder_frame_count());
    usb_console_printf("Monitor: %s  errors=%lu\r\n",
                       monitor_is_active() ? "on" : "off",
                       (unsigned long)monitor_error_count());
    usb_console_printf("Inject count: %lu\r\n",
                       (unsigned long)fault_inject_count());

    fault_inject_config_t cfg;
    fault_inject_get_config(&cfg);
    usb_console_printf("Inject config: repeat=%u delay=%ums flip=0x%02X"
                       " offset=%u pre=%ums post=%ums\r\n",
                       cfg.repeat, cfg.repeat_delay_ms, cfg.bit_flip_mask,
                       cfg.byte_offset, cfg.pre_delay_ms, cfg.post_delay_ms);

    usb_console_printf("Trigger: %s  matches=%lu\r\n",
                       fault_inject_trigger_is_armed() ? "armed" : "disarmed",
                       (unsigned long)fault_inject_trigger_match_count());
    if (fault_inject_trigger_is_armed()) {
        fault_inject_trigger_config_t tcfg;
        fault_inject_trigger_get_config(&tcfg);
        usb_console_printf("  watch=%s action=%s pattern_len=%u"
                           " payload_len=%u one_shot=%s\r\n",
                           tcfg.iface == IFACE_I2C ? "i2c" : "spi",
                           tcfg.action_iface == IFACE_I2C ? "i2c" : "spi",
                           tcfg.pattern_len, tcfg.payload_len,
                           tcfg.one_shot ? "yes" : "no");
    }
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

/* -------------------------------------------------------------------------
 * Keyword-argument parsing helpers for the inject command
 *
 * These scan argv[start..argc-1] for "keyword value" pairs and return the
 * first argument index that does not look like a data hex byte.
 * ---------------------------------------------------------------------- */

/* Return index of the first argv[i] (starting at *pos) that equals keyword,
 * or -1 if not found.  If found, advances *pos past the value. */
static int find_kwarg(char **argv, int argc, int start,
                      const char *keyword, char **value_out) {
    for (int i = start; i < argc - 1; i++) {
        if (strcmp(argv[i], keyword) == 0) {
            *value_out = argv[i + 1];
            return i;
        }
    }
    return -1;
}

/*
 * Collect hex data bytes from argv[start] until a non-hex token or a
 * recognised keyword is encountered.  Returns count of bytes consumed.
 */
static const char *s_inject_keywords[] = {
    "repeat", "delay", "flip", "pre", "post",
    "pattern", "payload", "addr", "once",
    NULL
};

static bool is_inject_keyword(const char *s) {
    for (int i = 0; s_inject_keywords[i]; i++) {
        if (strcmp(s, s_inject_keywords[i]) == 0) return true;
    }
    return false;
}

static int collect_hex_bytes(char **argv, int argc, int start,
                              uint8_t *out, size_t max) {
    int n = 0;
    for (int i = start; i < argc && (size_t)n < max; i++) {
        if (is_inject_keyword(argv[i])) break;
        char *end;
        unsigned long v = strtoul(argv[i], &end, 16);
        if (end == argv[i] || v > 0xFF) return -1;
        out[n++] = (uint8_t)v;
    }
    return n;
}

/* -------------------------------------------------------------------------
 * inject config sub-command
 * ---------------------------------------------------------------------- */
static void cmd_inject_config(char **argv, int argc) {
    /* argv[0]="inject" argv[1]="config" argv[2..] = options */
    if (argc >= 3 && strcmp(argv[2], "show") == 0) {
        fault_inject_config_t cfg;
        fault_inject_get_config(&cfg);
        usb_console_printf("inject config: repeat=%u delay=%ums flip=0x%02X"
                           " offset=%u pre=%ums post=%ums\r\n",
                           cfg.repeat, cfg.repeat_delay_ms, cfg.bit_flip_mask,
                           cfg.byte_offset, cfg.pre_delay_ms, cfg.post_delay_ms);
        return;
    }

    fault_inject_config_t cfg;
    fault_inject_get_config(&cfg);

    char *val;
    if (find_kwarg(argv, argc, 2, "repeat", &val) >= 0)
        cfg.repeat = (uint16_t)atoi(val);
    if (find_kwarg(argv, argc, 2, "delay", &val) >= 0)
        cfg.repeat_delay_ms = (uint16_t)atoi(val);
    if (find_kwarg(argv, argc, 2, "flip", &val) >= 0)
        cfg.bit_flip_mask = (uint8_t)strtoul(val, NULL, 16);
    if (find_kwarg(argv, argc, 2, "offset", &val) >= 0)
        cfg.byte_offset = (uint16_t)atoi(val);
    if (find_kwarg(argv, argc, 2, "pre", &val) >= 0)
        cfg.pre_delay_ms = (uint16_t)atoi(val);
    if (find_kwarg(argv, argc, 2, "post", &val) >= 0)
        cfg.post_delay_ms = (uint16_t)atoi(val);

    fault_inject_configure(&cfg);
    usb_console_printf("inject config updated: repeat=%u delay=%ums flip=0x%02X"
                       " offset=%u pre=%ums post=%ums\r\n",
                       cfg.repeat, cfg.repeat_delay_ms, cfg.bit_flip_mask,
                       cfg.byte_offset, cfg.pre_delay_ms, cfg.post_delay_ms);
}

/* -------------------------------------------------------------------------
 * inject trigger sub-command
 * ---------------------------------------------------------------------- */
static void cmd_inject_trigger(char **argv, int argc) {
    /* argv[0]="inject" argv[1]="trigger" argv[2]= set|disarm|show */
    if (argc < 3) {
        usb_console_write("Usage: inject trigger set|disarm|show\r\n");
        return;
    }

    if (strcmp(argv[2], "disarm") == 0) {
        fault_inject_trigger_disarm();
        usb_console_write("Trigger disarmed.\r\n");
        return;
    }

    if (strcmp(argv[2], "show") == 0) {
        usb_console_printf("Trigger: %s  matches=%lu\r\n",
                           fault_inject_trigger_is_armed() ? "armed" : "disarmed",
                           (unsigned long)fault_inject_trigger_match_count());
        if (fault_inject_trigger_is_armed()) {
            fault_inject_trigger_config_t tcfg;
            fault_inject_trigger_get_config(&tcfg);

            /* Print pattern bytes */
            usb_console_printf("  watch=%s  pattern(%u):",
                               tcfg.iface == IFACE_I2C ? "i2c" : "spi",
                               tcfg.pattern_len);
            for (uint16_t i = 0; i < tcfg.pattern_len; i++) {
                usb_console_printf(" %02X", tcfg.pattern[i]);
            }
            usb_console_write("\r\n");

            /* Print payload bytes */
            usb_console_printf("  action=%s  payload(%u):",
                               tcfg.action_iface == IFACE_I2C ? "i2c" : "spi",
                               tcfg.payload_len);
            for (uint16_t i = 0; i < tcfg.payload_len; i++) {
                usb_console_printf(" %02X", tcfg.payload[i]);
            }
            usb_console_write("\r\n");

            if (tcfg.action_iface == IFACE_I2C) {
                usb_console_printf("  addr=0x%02X\r\n", tcfg.addr);
            }
            usb_console_printf("  match_offset=%u  one_shot=%s\r\n",
                               tcfg.match_offset,
                               tcfg.one_shot ? "yes" : "no");
        }
        return;
    }

    if (strcmp(argv[2], "set") != 0 || argc < 4) {
        usb_console_write("Usage: inject trigger set i2c|spi pattern <hex…>"
                          " payload <hex…> [addr HH] [once]\r\n");
        return;
    }

    /* argv[3] = watch interface */
    fault_inject_trigger_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));

    if (strcmp(argv[3], "i2c") == 0)      tcfg.iface = IFACE_I2C;
    else if (strcmp(argv[3], "spi") == 0) tcfg.iface = IFACE_SPI;
    else {
        usb_console_write("Unknown interface. Use i2c or spi.\r\n");
        return;
    }

    /* Default: inject on the same interface as the watch */
    tcfg.action_iface = tcfg.iface;

    /* Find "pattern" keyword and collect bytes until next keyword */
    char *dummy;
    int pat_idx = find_kwarg(argv, argc, 4, "pattern", &dummy);
    if (pat_idx < 0) {
        usb_console_write("Missing 'pattern' keyword.\r\n");
        return;
    }
    int n = collect_hex_bytes(argv, argc, pat_idx + 1,
                              tcfg.pattern, FRAME_DATA_MAX);
    if (n <= 0) {
        usb_console_write("No valid pattern bytes.\r\n");
        return;
    }
    tcfg.pattern_len = (uint16_t)n;

    /* Find "payload" keyword and collect bytes */
    int pay_idx = find_kwarg(argv, argc, 4, "payload", &dummy);
    if (pay_idx < 0) {
        usb_console_write("Missing 'payload' keyword.\r\n");
        return;
    }
    n = collect_hex_bytes(argv, argc, pay_idx + 1,
                          tcfg.payload, FRAME_DATA_MAX);
    if (n <= 0) {
        usb_console_write("No valid payload bytes.\r\n");
        return;
    }
    tcfg.payload_len = (uint16_t)n;

    /* Optional: addr */
    char *val;
    if (find_kwarg(argv, argc, 4, "addr", &val) >= 0) {
        unsigned long a = strtoul(val, NULL, 16);
        if (a > 0x7F) {
            usb_console_write("Invalid I2C address.\r\n");
            return;
        }
        tcfg.addr = (uint8_t)a;
        tcfg.action_iface = IFACE_I2C;
    }

    /* Optional: offset */
    if (find_kwarg(argv, argc, 4, "offset", &val) >= 0) {
        tcfg.match_offset = (uint16_t)atoi(val);
    }

    /* Optional: once */
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "once") == 0) {
            tcfg.one_shot = true;
            break;
        }
    }

    fault_inject_trigger_set(&tcfg);
    usb_console_printf("Trigger armed: watch=%s pattern_len=%u"
                       " action=%s payload_len=%u one_shot=%s\r\n",
                       tcfg.iface == IFACE_I2C ? "i2c" : "spi",
                       tcfg.pattern_len,
                       tcfg.action_iface == IFACE_I2C ? "i2c" : "spi",
                       tcfg.payload_len,
                       tcfg.one_shot ? "yes" : "no");
}

/* -------------------------------------------------------------------------
 * inject top-level command
 * ---------------------------------------------------------------------- */
static void cmd_inject(char **argv, int argc) {
    if (argc < 2) {
        usb_console_write("Usage: inject i2c|spi|config|trigger ...\r\n");
        return;
    }

    /* inject config ... */
    if (strcmp(argv[1], "config") == 0) {
        cmd_inject_config(argv, argc);
        return;
    }

    /* inject trigger ... */
    if (strcmp(argv[1], "trigger") == 0) {
        cmd_inject_trigger(argv, argc);
        return;
    }

    uint8_t buf[FRAME_DATA_MAX];

    if (strcmp(argv[1], "i2c") == 0) {
        if (argc < 4) {
            usb_console_write("Usage: inject i2c <addr_hex> <byte…>"
                              " [repeat N] [delay MS] [flip MASK]\r\n");
            return;
        }
        char *end;
        unsigned long addr = strtoul(argv[2], &end, 16);
        if (end == argv[2] || addr > 0x7F) {
            usb_console_write("Invalid I2C address.\r\n");
            return;
        }

        /* Collect data bytes starting at argv[3] */
        int n = collect_hex_bytes(argv, argc, 3, buf, sizeof(buf));
        if (n <= 0) { usb_console_write("No valid data bytes.\r\n"); return; }

        /* Apply per-command overrides to a temporary config copy */
        fault_inject_config_t cfg;
        fault_inject_get_config(&cfg);
        char *val;
        if (find_kwarg(argv, argc, 3, "repeat", &val) >= 0)
            cfg.repeat = (uint16_t)atoi(val);
        if (find_kwarg(argv, argc, 3, "delay", &val) >= 0)
            cfg.repeat_delay_ms = (uint16_t)atoi(val);
        if (find_kwarg(argv, argc, 3, "flip", &val) >= 0)
            cfg.bit_flip_mask = (uint8_t)strtoul(val, NULL, 16);
        fault_inject_configure(&cfg);

        int rc = fault_inject_send(IFACE_I2C, buf, (size_t)n, (uint8_t)addr);
        usb_console_printf("inject i2c: %s (%d bytes)\r\n",
                           rc == 0 ? "ok" : "error", n);

    } else if (strcmp(argv[1], "spi") == 0) {
        if (argc < 3) {
            usb_console_write("Usage: inject spi <byte…>"
                              " [repeat N] [delay MS] [flip MASK]\r\n");
            return;
        }

        int n = collect_hex_bytes(argv, argc, 2, buf, sizeof(buf));
        if (n <= 0) { usb_console_write("No valid data bytes.\r\n"); return; }

        fault_inject_config_t cfg;
        fault_inject_get_config(&cfg);
        char *val;
        if (find_kwarg(argv, argc, 2, "repeat", &val) >= 0)
            cfg.repeat = (uint16_t)atoi(val);
        if (find_kwarg(argv, argc, 2, "delay", &val) >= 0)
            cfg.repeat_delay_ms = (uint16_t)atoi(val);
        if (find_kwarg(argv, argc, 2, "flip", &val) >= 0)
            cfg.bit_flip_mask = (uint8_t)strtoul(val, NULL, 16);
        fault_inject_configure(&cfg);

        int rc = fault_inject_send(IFACE_SPI, buf, (size_t)n, 0);
        usb_console_printf("inject spi: %s (%d bytes)\r\n",
                           rc == 0 ? "ok" : "error", n);

    } else {
        usb_console_write("Unknown sub-command. Use: inject i2c|spi|config|trigger\r\n");
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
