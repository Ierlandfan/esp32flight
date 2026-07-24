#pragma once
/* No OTA in the app (updates arrive as a new APK); the /ota endpoint will
 * report "no OTA partition" and the panel keeps the button locked. */
#include <stddef.h>
#include "esp_err.h"

typedef struct { const char *label; } esp_partition_t;
typedef int esp_ota_handle_t;
#define OTA_WITH_SEQUENTIAL_WRITES 0

static inline const esp_partition_t *esp_ota_get_next_update_partition(const void *p)
{ (void)p; return (const esp_partition_t *)0; }
static inline esp_err_t esp_ota_begin(const esp_partition_t *p, int sz, esp_ota_handle_t *h)
{ (void)p; (void)sz; (void)h; return ESP_FAIL; }
static inline esp_err_t esp_ota_write(esp_ota_handle_t h, const void *d, size_t n)
{ (void)h; (void)d; (void)n; return ESP_FAIL; }
static inline esp_err_t esp_ota_end(esp_ota_handle_t h) { (void)h; return ESP_FAIL; }
static inline esp_err_t esp_ota_abort(esp_ota_handle_t h) { (void)h; return ESP_FAIL; }
static inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{ (void)p; return ESP_FAIL; }
