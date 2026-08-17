/**
 * @file fault_inject.c
 * @brief Fault injection – transmit crafted frames on I2C or SPI
 */

#include "fault_inject.h"
#include "i2c_interface.h"
#include "spi_interface.h"
#include "recorder.h"
#include "pico/time.h"
#include <string.h>

static uint32_t s_inject_count;

void fault_inject_init(void) {
    s_inject_count = 0;
}

int fault_inject_send(interface_id_t iface,
                      const uint8_t *data, size_t len,
                      uint8_t addr) {
    if (!data || len == 0 || len > FRAME_DATA_MAX) return -1;

    int rc;

    switch (iface) {
        case IFACE_I2C:
            rc = i2c_master_write(addr, data, len);
            break;

        case IFACE_SPI:
            rc = spi_master_transfer(data, NULL, len);
            break;

        default:
            return -1;
    }

    if (rc == 0) {
        s_inject_count++;
        /* The individual interface modules already push to the recorder,
         * so we do not double-record here. */
    }

    return rc;
}

uint32_t fault_inject_count(void) {
    return s_inject_count;
}
