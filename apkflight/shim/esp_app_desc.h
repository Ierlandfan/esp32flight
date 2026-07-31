#pragma once
#include <stdint.h>

/* Mirrors the head of esp-idf's esp_app_desc_t closely enough for the
 * shared code (version display and the OTA variant guard). */
typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    uint32_t reserv1[2];
    char     version[32];
    char     project_name[32];
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);
