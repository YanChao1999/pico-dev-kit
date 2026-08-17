/**
 * @file i2c_interface.h
 * @brief I2C master / slave interface module
 */

#ifndef I2C_INTERFACE_H
#define I2C_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "pico_dev_kit.h"

/* Default pin assignments (can be overridden at compile-time) */
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN  4
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN  5
#endif
#ifndef I2C_SLAVE_ADDR
#define I2C_SLAVE_ADDR 0x42
#endif

/**
 * Configure the I2C hardware.
 *
 * @param role       ROLE_MASTER or ROLE_SLAVE
 * @param speed_khz  Bus speed in kHz (100 or 400 typical)
 */
void i2c_interface_init(interface_role_t role, uint32_t speed_khz);

/** De-initialise and release I2C hardware. */
void i2c_interface_deinit(void);

/**
 * (Master only) Write bytes to a target address.
 * Returns 0 on success, negative on error.
 */
int i2c_master_write(uint8_t addr, const uint8_t *data, size_t len);

/**
 * (Master only) Read bytes from a target address.
 * Returns number of bytes read, negative on error.
 */
int i2c_master_read(uint8_t addr, uint8_t *data, size_t len);

/**
 * Service pending slave events (call from main loop).
 * Any received frame is passed to the recorder automatically.
 */
void i2c_interface_task(void);

/** Return true if the I2C interface is currently active. */
bool i2c_interface_is_active(void);

#endif /* I2C_INTERFACE_H */
