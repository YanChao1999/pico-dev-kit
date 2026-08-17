/**
 * @file spi_interface.c
 * @brief SPI master / slave interface module
 */

#include "spi_interface.h"
#include "recorder.h"
#include "monitor.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

/* Use SPI0 */
#define SPI_HW  spi0

static bool             s_active = false;
static interface_role_t s_role;

/* -------------------------------------------------------------------------
 * Slave receive ring buffer (filled via DMA in slave mode)
 * ---------------------------------------------------------------------- */
#define SLAVE_RX_BUF_SIZE  256
static uint8_t  s_slave_rx[SLAVE_RX_BUF_SIZE];
static int      s_slave_dma_chan = -1;

/* -------------------------------------------------------------------------
 * Helper: configure DMA for continuous slave receive
 * ---------------------------------------------------------------------- */
static void slave_dma_start(void) {
    s_slave_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(s_slave_dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, spi_get_dreq(SPI_HW, false));

    dma_channel_configure(
        s_slave_dma_chan,
        &cfg,
        s_slave_rx,               /* write destination  */
        &spi_get_hw(SPI_HW)->dr,  /* read source (SPI DR register) */
        SLAVE_RX_BUF_SIZE,
        true                       /* start immediately  */
    );
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void spi_interface_init(interface_role_t role, uint32_t speed_khz,
                        uint8_t cpol, uint8_t cpha) {
    s_role   = role;
    s_active = true;

    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_RX_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_CS_PIN,  GPIO_FUNC_SPI);

    spi_cpol_t pol  = cpol ? SPI_CPOL_1 : SPI_CPOL_0;
    spi_cpha_t phase = cpha ? SPI_CPHA_1 : SPI_CPHA_0;

    if (role == ROLE_MASTER) {
        spi_init(SPI_HW, speed_khz * 1000);
        spi_set_format(SPI_HW, 8, pol, phase, SPI_MSB_FIRST);
        /* CS managed manually */
        gpio_set_function(SPI_CS_PIN, GPIO_FUNC_SIO);
        gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
        gpio_put(SPI_CS_PIN, 1);
    } else {
        /* Slave: speed ignored (driven by master) */
        spi_init(SPI_HW, 1000000);
        spi_set_format(SPI_HW, 8, pol, phase, SPI_MSB_FIRST);
        spi_set_slave(SPI_HW, true);
        slave_dma_start();
    }
}

void spi_interface_deinit(void) {
    if (!s_active) return;
    if (s_slave_dma_chan >= 0) {
        dma_channel_abort(s_slave_dma_chan);
        dma_channel_unclaim(s_slave_dma_chan);
        s_slave_dma_chan = -1;
    }
    spi_deinit(SPI_HW);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_NULL);
    gpio_set_function(SPI_TX_PIN,  GPIO_FUNC_NULL);
    gpio_set_function(SPI_RX_PIN,  GPIO_FUNC_NULL);
    gpio_set_function(SPI_CS_PIN,  GPIO_FUNC_NULL);
    s_active = false;
}

int spi_master_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    if (!s_active || s_role != ROLE_MASTER) return -1;

    static const uint8_t fill = 0xFF;
    uint8_t              dummy;

    gpio_put(SPI_CS_PIN, 0);

    for (size_t i = 0; i < len; i++) {
        uint8_t tx_byte = tx_data ? tx_data[i] : fill;
        uint8_t rx_byte;
        spi_write_read_blocking(SPI_HW, &tx_byte, &rx_byte, 1);
        if (rx_data) rx_data[i] = rx_byte;
    }
    (void)dummy;

    gpio_put(SPI_CS_PIN, 1);

    /* Record TX frame */
    if (tx_data) {
        frame_t frame;
        frame.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
        frame.iface  = IFACE_SPI;
        frame.role   = ROLE_MASTER;
        frame.length = (uint16_t)(len < FRAME_DATA_MAX ? len : FRAME_DATA_MAX);
        memcpy(frame.data, tx_data, frame.length);
        recorder_push(&frame);
    }

    return 0;
}

void spi_interface_task(void) {
    if (!s_active || s_role != ROLE_SLAVE) return;
    if (s_slave_dma_chan < 0) return;

    /* Check how many bytes the DMA has written so far */
    uint32_t remaining = dma_channel_hw_addr(s_slave_dma_chan)->transfer_count;
    uint32_t received  = SLAVE_RX_BUF_SIZE - remaining;

    if (received > 0 && !dma_channel_is_busy(s_slave_dma_chan)) {
        /* DMA completed: flush the buffer as a frame */
        frame_t frame;
        frame.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
        frame.iface  = IFACE_SPI;
        frame.role   = ROLE_SLAVE;
        frame.length = (uint16_t)(received < FRAME_DATA_MAX ?
                                  received : FRAME_DATA_MAX);
        memcpy(frame.data, s_slave_rx, frame.length);
        recorder_push(&frame);

        /* Restart DMA */
        slave_dma_start();
    }
}

bool spi_interface_is_active(void) {
    return s_active;
}
