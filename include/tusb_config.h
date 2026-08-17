/**
 * @file tusb_config.h
 * @brief TinyUSB configuration for Pico Dev Kit
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/* ---- Device stack -------------------------------------------------------- */
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE

/* Two CDC interfaces:
 *   0 – console
 *   1 – binary data stream
 */
#define CFG_TUD_CDC             2
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256

/* Unused classes */
#define CFG_TUD_HID             0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* ---- OS / MCU ------------------------------------------------------------ */
#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_OS             OPT_OS_PICO

/* Enable DMA for USB */
#define CFG_TUSB_MEM_SECTION    /* empty – use default SRAM */
#define CFG_TUSB_MEM_ALIGN      TU_ATTR_ALIGNED(4)

#endif /* TUSB_CONFIG_H */
