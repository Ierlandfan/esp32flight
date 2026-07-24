#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Assets live on the real filesystem here; mounting is a no-op. */

typedef struct {
    const char *base_path;
    const char *partition_label;
    int max_files;
    int format_if_mount_failed;
} esp_vfs_spiffs_conf_t;

static inline esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *c)
{
    (void)c;
    return ESP_OK;
}

static inline esp_err_t esp_spiffs_info(const char *label, size_t *total, size_t *used)
{
    (void)label;
    if (total != NULL) *total = 0;
    if (used != NULL) *used = 0;
    return ESP_OK;
}
