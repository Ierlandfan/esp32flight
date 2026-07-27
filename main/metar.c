#include "metar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "http_util.h"

static const char *TAG = "metar";

static char s_metar[160];

bool metar_fetch(const char *icao)
{
    char url[128];
    snprintf(url, sizeof(url),
             "https://aviationweather.gov/api/data/metar?ids=%s&format=json", icao);
    char *buf = malloc(4096);
    if (buf == NULL) {
        return false;
    }
    bool ok = false;
    if (http_get_to_buffer(url, buf, 4096, NULL) == ESP_OK) {
        cJSON *root = cJSON_Parse(buf);
        const cJSON *first = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
        const cJSON *raw = first ? cJSON_GetObjectItem(first, "rawOb") : NULL;
        if (cJSON_IsString(raw)) {
            strlcpy(s_metar, raw->valuestring, sizeof(s_metar));
            ok = true;
            ESP_LOGI(TAG, "%s", s_metar);
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
    }
    free(buf);
    return ok;
}

const char *metar_get(void)
{
    return s_metar;
}

static char s_taf[400];

bool taf_fetch(const char *icao)
{
    char url[128];
    snprintf(url, sizeof(url),
             "https://aviationweather.gov/api/data/taf?ids=%s&format=raw", icao);
    char *buf = malloc(2048);
    if (buf == NULL) {
        return false;
    }
    bool ok = false;
    if (http_get_to_buffer(url, buf, 2048, NULL) == ESP_OK && buf[0] != '\0') {
        /* collapse the pretty-printed continuation lines into one string */
        int o = 0;
        bool sp = false;
        for (const char *c = buf; *c != '\0' && o < (int)sizeof(s_taf) - 1; c++) {
            if (*c == '\n' || *c == '\r' || *c == ' ') {
                sp = o > 0;
                continue;
            }
            if (sp) {
                s_taf[o++] = ' ';
                sp = false;
            }
            if (o < (int)sizeof(s_taf) - 1) {
                s_taf[o++] = *c;
            }
        }
        s_taf[o] = '\0';
        ok = o > 0;
        if (ok) {
            ESP_LOGI(TAG, "TAF: %s", s_taf);
        }
    }
    free(buf);
    return ok;
}

const char *taf_get(void)
{
    return s_taf;
}
