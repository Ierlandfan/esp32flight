#pragma once

#include "esp_err.h"

/* System clock is already correct on desktop/Android. */

typedef struct { const char *server; } esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(srv) (esp_sntp_config_t){ .server = (srv) }

static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c)
{
    (void)c;
    return ESP_OK;
}

static inline esp_err_t esp_netif_sntp_sync_wait(unsigned ticks)
{
    (void)ticks;
    return ESP_OK;
}
