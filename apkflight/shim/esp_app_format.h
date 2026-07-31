/* Shim for the shared web_server.c: image-header layout used by the OTA
 * variant guard. The app never receives ESP images, the code just needs
 * the types to compile. Layout mirrors esp-idf/components/bootloader_support. */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t  magic;
    uint8_t  segment_count;
    uint8_t  spi_mode;
    uint8_t  spi_speed: 4;
    uint8_t  spi_size: 4;
    uint32_t entry_addr;
    uint8_t  wp_pin;
    uint8_t  spi_pin_drv[3];
    uint16_t chip_id;
    uint8_t  min_chip_rev;
    uint16_t min_chip_rev_full;
    uint16_t max_chip_rev_full;
    uint8_t  reserved[4];
    uint8_t  hash_appended;
} __attribute__((packed)) esp_image_header_t;

typedef struct {
    uint32_t load_addr;
    uint32_t data_len;
} esp_image_segment_header_t;

#define ESP_APP_DESC_MAGIC_WORD 0xABCD5432
