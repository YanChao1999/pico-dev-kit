/**
 * @file pico_dev_kit.h
 * @brief Pico Dev Kit – top-level configuration and shared types
 *
 * Raspberry Pi Pico multi-interface development kit.
 * Features:
 *   - USB CDC console for runtime configuration
 *   - I2C master / slave
 *   - SPI master / slave
 *   - Frame recorder (capture and stream to USB host)
 *   - Fault injection
 *   - Interface monitor / health check
 */

#ifndef PICO_DEV_KIT_H
#define PICO_DEV_KIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Version
 * ---------------------------------------------------------------------- */
#define PICO_DEV_KIT_VERSION_MAJOR  1
#define PICO_DEV_KIT_VERSION_MINOR  0
#define PICO_DEV_KIT_VERSION_PATCH  0

/* -------------------------------------------------------------------------
 * Interface identifiers
 * ---------------------------------------------------------------------- */
typedef enum {
    IFACE_NONE   = 0,
    IFACE_I2C    = 1,
    IFACE_SPI    = 2,
} interface_id_t;

/* -------------------------------------------------------------------------
 * Interface role
 * ---------------------------------------------------------------------- */
typedef enum {
    ROLE_MASTER = 0,
    ROLE_SLAVE  = 1,
} interface_role_t;

/* -------------------------------------------------------------------------
 * Generic frame passed between modules
 * ---------------------------------------------------------------------- */
#define FRAME_DATA_MAX  256

typedef struct {
    uint32_t        timestamp_us;   /* capture time (us since boot)      */
    interface_id_t  iface;          /* originating interface              */
    interface_role_t role;          /* role at capture time               */
    uint16_t        length;         /* number of valid bytes in data[]    */
    uint8_t         data[FRAME_DATA_MAX];
} frame_t;

#endif /* PICO_DEV_KIT_H */
