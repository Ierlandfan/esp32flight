#include "types.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "types";

static char *s_db;      /* whole TSV in PSRAM, sorted, newline-separated */
static size_t s_len;

void types_init(void)
{
#ifdef ESP_PLATFORM
    /* embedded in the app image so it travels with every OTA (the
     * assets partition is only written by a full USB flash) */
    extern const char tsv_start[] asm("_binary_types_tsv_start");
    extern const char tsv_end[] asm("_binary_types_tsv_end");
    s_db = (char *)tsv_start;
    s_len = (size_t)(tsv_end - tsv_start);
#else
    FILE *f = fopen("/assets/types.tsv", "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "types.tsv missing; feed desc only");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return;
    }
    s_db = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    if (s_db == NULL) {
        fclose(f);
        return;
    }
    s_len = fread(s_db, 1, size, f);
    s_db[s_len] = '\0';
    fclose(f);
#endif

    int lines = 0;
    for (size_t i = 0; i < s_len; i++) {
        if (s_db[i] == '\n') {
            lines++;
        }
    }
    ESP_LOGI(TAG, "%d aircraft types loaded (%u KB)", lines,
             (unsigned)(s_len / 1024));
}

bool types_lookup(const char *icao, char *out, size_t out_size)
{
    if (s_db == NULL || icao == NULL || icao[0] == '\0' || out_size == 0) {
        return false;
    }
    size_t klen = strlen(icao);
    const char *p = s_db;
    while (p != NULL && *p != '\0') {
        /* DESIGNATOR \t MANUFACTURER Model */
        if (strncmp(p, icao, klen) == 0 && p[klen] == '\t') {
            const char *v = p + klen + 1;
            const char *e = strchr(v, '\n');
            size_t n = e != NULL ? (size_t)(e - v) : strlen(v);
            if (n >= out_size) {
                n = out_size - 1;
            }
            memcpy(out, v, n);
            out[n] = '\0';
            return true;
        }
        int c = strncmp(p, icao, klen);
        if (c > 0) {
            return false;   /* sorted file: past the key already */
        }
        p = strchr(p, '\n');
        if (p != NULL) {
            p++;
        }
    }
    return false;
}
