/**
 * @file spi_interface.h
 * @brief SPI master / slave interface module
 */

#ifndef SPI_INTERFACE_H
#define SPI_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "pico_dev_kit.h"

/* Default pin assignments (can be overridden at compile-time) */
#ifndef SPI_SCK_PIN
#define SPI_SCK_PIN   2
#endif
#ifndef SPI_TX_PIN
#define SPI_TX_PIN    3
#endif
#ifndef SPI_RX_PIN
#define SPI_RX_PIN    0
#endif
#ifndef SPI_CS_PIN
#define SPI_CS_PIN    1
#endif

/**
 * Configure the SPI hardware.
 *
 * @param role      ROLE_MASTER or ROLE_SLAVE
 * @param speed_khz Bus speed in kHz (master mode only; ignored in slave mode)
 * @param cpol      Clock polarity (0 or 1)
 * @param cpha      Clock phase    (0 or 1)
 */
void spi_interface_init(interface_role_t role, uint32_t speed_khz,
                        uint8_t cpol, uint8_t cpha);

/** De-initialise and release SPI hardware. */
void spi_interface_deinit(void);

/**
 * (Master only) Perform a full-duplex transfer.
 * tx_data may be NULL (sends 0xFF fill).
 * rx_data may be NULL (discards received bytes).
 * Returns 0 on success.
 */
int spi_master_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

/**
 * Service pending slave receive events (call from main loop).
 * Any received frame is passed to the recorder automatically.
 */
void spi_interface_task(void);

/** Return true if the SPI interface is currently active. */
bool spi_interface_is_active(void);

#endif /* SPI_INTERFACE_H */
