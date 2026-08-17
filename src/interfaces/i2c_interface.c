/**
 * @file i2c_interface.c
 * @brief I2C master / slave interface module
 */

#include "i2c_interface.h"
#include "recorder.h"
#include "monitor.h"
#include "pio_monitor.h"
#include "usb_transport.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

/* Use I2C0 */
#define I2C_HW  i2c0

static bool             s_active = false;
static interface_role_t s_role;

/* -------------------------------------------------------------------------
 * Slave receive buffer (filled by IRQ handler)
 * ---------------------------------------------------------------------- */
#define SLAVE_BUF_SIZE  256
static volatile uint8_t  s_slave_buf[SLAVE_BUF_SIZE];
static volatile uint16_t s_slave_len;
static volatile bool     s_slave_frame_ready;

/* -------------------------------------------------------------------------
 * I2C slave IRQ handler
 * ---------------------------------------------------------------------- */
static void i2c_slave_irq_handler(void) {
    i2c_hw_t *hw = i2c0_hw;

    uint32_t status = hw->intr_stat;

    if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        /* Read a byte from the FIFO */
        uint32_t raw = hw->data_cmd;
        uint8_t byte = (uint8_t)(raw & 0xFF);
        if (s_slave_len < SLAVE_BUF_SIZE) {
            s_slave_buf[s_slave_len++] = byte;
        }
    }

    if (status & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        /* STOP condition – frame is complete */
        hw->clr_stop_det;  /* clear the interrupt */
        if (s_slave_len > 0) {
            s_slave_frame_ready = true;
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void i2c_interface_init(interface_role_t role, uint32_t speed_khz) {
    s_role   = role;
    s_active = true;
    s_slave_len = 0;
    s_slave_frame_ready = false;

    if (role == ROLE_MONITOR) {
        /* PIO-based passive sniff: pins are plain GPIO inputs */
        gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_SIO);
        gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_SIO);
        gpio_set_dir(I2C_SDA_PIN, GPIO_IN);
        gpio_set_dir(I2C_SCL_PIN, GPIO_IN);
        pio_monitor_init(IFACE_I2C);
        return;
    }

    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    if (role == ROLE_MASTER) {
        i2c_init(I2C_HW, speed_khz * 1000);
    } else {
        /* Slave mode */
        i2c_init(I2C_HW, speed_khz * 1000);
        i2c_set_slave_mode(I2C_HW, true, I2C_SLAVE_ADDR);

        /* Enable RX_FULL and STOP_DET interrupts */
        i2c0_hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS |
                              I2C_IC_INTR_MASK_M_STOP_DET_BITS;
        irq_set_exclusive_handler(I2C0_IRQ, i2c_slave_irq_handler);
        irq_set_enabled(I2C0_IRQ, true);
    }
}

void i2c_interface_deinit(void) {
    if (!s_active) return;
    if (s_role == ROLE_MONITOR) {
        pio_monitor_deinit(IFACE_I2C);
        gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_NULL);
        gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_NULL);
        s_active = false;
        return;
    }
    if (s_role == ROLE_SLAVE) {
        irq_set_enabled(I2C0_IRQ, false);
    }
    i2c_deinit(I2C_HW);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_NULL);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_NULL);
    s_active = false;
}

int i2c_master_write(uint8_t addr, const uint8_t *data, size_t len) {
    if (!s_active || s_role != ROLE_MASTER) return -1;

    int rc = i2c_write_timeout_us(I2C_HW, addr, data, len, false, 10000);
    if (rc == PICO_ERROR_GENERIC || rc == PICO_ERROR_TIMEOUT) {
        monitor_record_error("I2C master write error");
        return -1;
    }

    /* Record the transmitted frame */
    frame_t frame;
    frame.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
    frame.iface  = IFACE_I2C;
    frame.role   = ROLE_MASTER;
    frame.length = (uint16_t)(len < FRAME_DATA_MAX ? len : FRAME_DATA_MAX);
    memcpy(frame.data, data, frame.length);
    recorder_push(&frame);

    return 0;
}

int i2c_master_read(uint8_t addr, uint8_t *data, size_t len) {
    if (!s_active || s_role != ROLE_MASTER) return -1;

    int rc = i2c_read_timeout_us(I2C_HW, addr, data, len, false, 10000);
    if (rc == PICO_ERROR_GENERIC || rc == PICO_ERROR_TIMEOUT) {
        monitor_record_error("I2C master read error");
        return -1;
    }

    /* Record received frame */
    frame_t frame;
    frame.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
    frame.iface  = IFACE_I2C;
    frame.role   = ROLE_MASTER;
    frame.length = (uint16_t)(rc < FRAME_DATA_MAX ? rc : FRAME_DATA_MAX);
    memcpy(frame.data, data, frame.length);
    recorder_push(&frame);

    return rc;
}

void i2c_interface_task(void) {
    if (!s_active) return;

    if (s_role == ROLE_MONITOR) {
        pio_monitor_task(IFACE_I2C);
        return;
    }

    if (s_role != ROLE_SLAVE) return;

    if (s_slave_frame_ready) {
        /* Snapshot atomically */
        uint32_t saved = save_and_disable_interrupts();
        uint16_t len = s_slave_len;
        uint8_t  tmp[SLAVE_BUF_SIZE];
        memcpy(tmp, (const uint8_t *)s_slave_buf, len);
        s_slave_len = 0;
        s_slave_frame_ready = false;
        restore_interrupts(saved);

        frame_t frame;
        frame.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
        frame.iface  = IFACE_I2C;
        frame.role   = ROLE_SLAVE;
        frame.length = (uint16_t)(len < FRAME_DATA_MAX ? len : FRAME_DATA_MAX);
        memcpy(frame.data, tmp, frame.length);
        recorder_push(&frame);
    }
}

bool i2c_interface_is_active(void) {
    return s_active;
}
