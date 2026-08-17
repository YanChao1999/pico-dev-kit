/**
 * @file usb_descriptors.c
 * @brief TinyUSB USB descriptors for Pico Dev Kit (two CDC interfaces)
 */

#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * Device descriptor
 * ---------------------------------------------------------------------- */
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x2E8A,  /* Raspberry Pi */
    .idProduct          = 0x000A,  /* CDC example */
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* -------------------------------------------------------------------------
 * Configuration descriptor (two CDC interfaces, each needs IAD + 3 ifaces)
 * ---------------------------------------------------------------------- */
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

/* Endpoint numbers */
#define EP_CDC0_NOTIF  0x81
#define EP_CDC0_OUT    0x02
#define EP_CDC0_IN     0x82
#define EP_CDC1_NOTIF  0x83
#define EP_CDC1_OUT    0x04
#define EP_CDC1_IN     0x84

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    /* CDC 0 – console */
    TUD_CDC_DESCRIPTOR(0, 4, EP_CDC0_NOTIF, 8, EP_CDC0_OUT, EP_CDC0_IN, 64),

    /* CDC 1 – data */
    TUD_CDC_DESCRIPTOR(2, 5, EP_CDC1_NOTIF, 8, EP_CDC1_OUT, EP_CDC1_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* -------------------------------------------------------------------------
 * String descriptors
 * ---------------------------------------------------------------------- */
static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  /* 0: English (0x0409)   */
    "Raspberry Pi",              /* 1: Manufacturer        */
    "Pico Dev Kit",              /* 2: Product             */
    NULL,                        /* 3: Serial (generated)  */
    "Console",                   /* 4: CDC 0 interface     */
    "Data",                      /* 5: CDC 1 interface     */
};

static uint16_t desc_str[64];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count;
    const char *str;

    if (index == 0) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == 3) {
        /* Generate serial number from Pico unique ID */
        static char serial[17];
        pico_unique_board_id_t uid;
        pico_get_unique_board_id(&uid);
        snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 uid.id[0], uid.id[1], uid.id[2], uid.id[3],
                 uid.id[4], uid.id[5], uid.id[6], uid.id[7]);
        str = serial;
        chr_count = 16;
        for (uint8_t i = 0; i < chr_count; i++) {
            desc_str[1 + i] = str[i];
        }
        desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) |
                                 (2 * chr_count + 2));
        return desc_str;
    } else {
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }
        str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 63) chr_count = 63;
        for (uint8_t i = 0; i < chr_count; i++) {
            desc_str[1 + i] = str[i];
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
