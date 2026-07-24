#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Minimal esp_netif surface; the local IP is resolved for real in
 * platform/netif.c (UDP-connect trick - works on Android and desktop). */

typedef struct esp_netif_s esp_netif_t;

typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip, gw, netmask;
} esp_netif_ip_info_t;

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(a) (int)((a)->addr & 0xff), (int)(((a)->addr >> 8) & 0xff), \
                  (int)(((a)->addr >> 16) & 0xff), (int)(((a)->addr >> 24) & 0xff)

static inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{
    (void)key;
    return (esp_netif_t *)1;   /* non-NULL: callers gate get_ip_info on it */
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);

static inline esp_err_t esp_netif_init(void) { return ESP_OK; }
