#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Wi-Fi belongs to the OS here; the core only ever asks for RSSI/SSID
 * (web panel info) and scan results (settings screen). */

typedef struct {
    uint8_t ssid[33];
    int8_t  rssi;
    uint8_t primary;
} wifi_ap_record_t;

static inline esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap)
{
    (void)ap;
    return ESP_FAIL;
}
