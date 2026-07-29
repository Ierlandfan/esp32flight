#include "logos.h"

#include "extra/libs/png/lodepng.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_util.h"
#include "lvgl_port.h"

static const char *TAG = "logos";

#define LOGO_CACHE_SIZE 24

/* Logos missing from the bundled set (small-flash builds carry only the
 * most common carriers) are fetched on demand from the companion repo and
 * live in the same RAM cache. Misses are remembered so an airline without
 * any logo is asked about exactly once per session. */
#define LOGO_FETCH_URL  "https://raw.githubusercontent.com/theqkash/esp32flight-logos/main/logos/%s.png"
#define LOGO_MISS_MAX   64
#define LOGO_MAX_BYTES  (64 * 1024)

static char s_miss[LOGO_MISS_MAX][4];
static int  s_miss_n;
static char s_fetching[4];
static volatile bool s_fetch_busy;

/* Index of logos available in the online repo (bundled as
 * /assets/logos/index.txt). Fetches are gated on it, so a callsign prefix
 * that is no airline at all (SP-xxx and friends) never hits the network.
 * Without the file (older asset sets) every code is assumed fetchable. */
static char (*s_index)[4];
static int  s_index_n = -1;   /* -1 = not loaded yet */

static void index_load(void)
{
    s_index_n = 0;
    FILE *f = fopen("/assets/logos/index.txt", "r");
    if (f == NULL) {
        s_index_n = -2;       /* no index bundled: allow everything */
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    int cap = (int)(size / 4) + 8;
    s_index = heap_caps_malloc((size_t)cap * 4, MALLOC_CAP_SPIRAM);
    if (s_index == NULL) {
        fclose(f);
        s_index_n = -2;
        return;
    }
    char line[16];
    while (fgets(line, sizeof(line), f) != NULL && s_index_n < cap) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 3) {
            strlcpy(s_index[s_index_n++], line, 4);
        }
    }
    fclose(f);
    ESP_LOGI(TAG, "online logo index: %d entries", s_index_n);
}

static bool index_has(const char *icao)
{
    if (s_index_n == -1) {
        index_load();
    }
    if (s_index_n < 0) {
        return true;
    }
    for (int i = 0; i < s_index_n; i++) {
        if (s_index[i][0] == icao[0] && strcmp(s_index[i], icao) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct {
    char         icao[4];
    uint8_t     *data;
    lv_img_dsc_t dsc;
    uint32_t     last_used;
    bool         used;
} logo_entry_t;

static logo_entry_t s_cache[LOGO_CACHE_SIZE];
static uint32_t s_tick;

esp_err_t logos_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/assets",
        .partition_label = "assets",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spiffs mount failed: %s", esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info("assets", &total, &used);
    ESP_LOGI(TAG, "assets fs: %u/%u KB used", (unsigned)(used / 1024), (unsigned)(total / 1024));
    return ESP_OK;
}

static bool miss_known(const char *icao)
{
    for (int i = 0; i < s_miss_n; i++) {
        if (strcmp(s_miss[i], icao) == 0) {
            return true;
        }
    }
    return false;
}

static void miss_remember(const char *icao)
{
    if (s_miss_n < LOGO_MISS_MAX && !miss_known(icao)) {
        strlcpy(s_miss[s_miss_n++], icao, 4);
    }
}

static void cache_put(const char *icao, uint8_t *data, size_t size);
static int cache_evictable_slot(void);

static void logo_fetch_task(void *arg)
{
    char url[128];
    snprintf(url, sizeof(url), LOGO_FETCH_URL, s_fetching);

    char *buf = heap_caps_malloc(LOGO_MAX_BYTES, MALLOC_CAP_SPIRAM);
    size_t len = 0;
    esp_err_t err = buf == NULL ? ESP_ERR_NO_MEM
                                : http_get_to_buffer(url, buf, LOGO_MAX_BYTES, &len);
    if (err == ESP_OK && len > 8 && buf[0] == (char)0x89) {   /* PNG magic */
        uint8_t *data = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
        if (data != NULL) {
            memcpy(data, buf, len);
            /* the UI reads the cache under the LVGL lock; insert under it
             * too, or an eviction could free a descriptor mid-render */
            if (lvgl_port_lock(-1)) {
                cache_put(s_fetching, data, len);
                lvgl_port_unlock();
            }
            ESP_LOGI(TAG, "%s fetched (%u B)", s_fetching, (unsigned)len);
        }
    } else {
        miss_remember(s_fetching);   /* 404 or offline: don't retry this one */
    }
    free(buf);
    s_fetch_busy = false;
    vTaskDelete(NULL);
}

static void logo_fetch_want(const char *icao)
{
    if (s_fetch_busy || miss_known(icao)) {
        return;
    }
    if (!index_has(icao)) {
        miss_remember(icao);   /* not in the online set either */
        return;
    }
    s_fetch_busy = true;
    strlcpy(s_fetching, icao, sizeof(s_fetching));
    if (xTaskCreate(logo_fetch_task, "logo_fetch", 6144, NULL, 3, NULL) != pdPASS) {
        s_fetch_busy = false;
    }
}

const lv_img_dsc_t *logos_get(const char *airline_icao)
{
    if (airline_icao == NULL || strlen(airline_icao) != 3) {
        return NULL;
    }

    s_tick++;
    for (int i = 0; i < LOGO_CACHE_SIZE; i++) {
        if (s_cache[i].used && strcmp(s_cache[i].icao, airline_icao) == 0) {
            s_cache[i].last_used = s_tick;
            return &s_cache[i].dsc;
        }
    }

    if (cache_evictable_slot() < 0) {
        return NULL;   /* cache pinned by the current pass: no I/O storm */
    }
    char path[48];
    snprintf(path, sizeof(path), "/assets/logos/%s.png", airline_icao);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        logo_fetch_want(airline_icao);   /* not bundled: try the online set */
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 64 * 1024) {
        fclose(f);
        return NULL;
    }

    uint8_t *data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (data == NULL) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(data, 1, size, f);
    fclose(f);
    if (rd != (size_t)size) {
        free(data);
        return NULL;
    }

    cache_put(airline_icao, data, size);
    for (int i = 0; i < LOGO_CACHE_SIZE; i++) {
        if (s_cache[i].used && strcmp(s_cache[i].icao, airline_icao) == 0) {
            return &s_cache[i].dsc;
        }
    }
    return NULL;
}

/* Decode the PNG once and keep an LVGL-native bitmap: scrolling then
 * blits pixels instead of running lodepng on every visible logo each
 * frame. 3 bytes per pixel (RGB565 + A8). */
static uint8_t *png_to_bitmap(const uint8_t *png, size_t len,
                              unsigned *w, unsigned *h)
{
    unsigned char *rgba = NULL;
    if (lodepng_decode32(&rgba, w, h, png, len) != 0 || rgba == NULL) {
        free(rgba);
        return NULL;
    }
    size_t px = (size_t)*w * *h;
    uint8_t *out = heap_caps_malloc(px * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out != NULL) {
        for (size_t i = 0; i < px; i++) {
            const unsigned char *c = rgba + i * 4;
            uint16_t c16 = (uint16_t)(((c[0] & 0xF8) << 8) |
                                      ((c[1] & 0xFC) << 3) | (c[2] >> 3));
            out[i * 3] = (uint8_t)(c16 & 0xFF);
            out[i * 3 + 1] = (uint8_t)(c16 >> 8);
            out[i * 3 + 2] = c[3];
        }
    }
    free(rgba);
    return out;
}

/* an entry touched within the last render pass or two must not be
 * replaced: rows on screen still point at its pixel buffer */
#define LOGO_HOT_WINDOW 64

static int cache_evictable_slot(void)
{
    int slot = -1;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < LOGO_CACHE_SIZE; i++) {
        if (!s_cache[i].used) {
            return i;
        }
        if (s_cache[i].last_used + LOGO_HOT_WINDOW <= s_tick &&
            s_cache[i].last_used < oldest) {
            oldest = s_cache[i].last_used;
            slot = i;
        }
    }
    return slot;
}

static void cache_put(const char *icao, uint8_t *data, size_t size)
{
    unsigned w = 0, h = 0;
    uint8_t *bmp = png_to_bitmap(data, size, &w, &h);
    free(data);   /* the raw PNG is no longer needed either way */
    if (bmp == NULL) {
        return;
    }
    data = bmp;
    size = (size_t)w * h * 3;

    int slot = cache_evictable_slot();
    if (slot < 0) {
        free(data);   /* everything is on screen; better a letter badge
                         than repainting somebody else's row */
        return;
    }
    if (s_cache[slot].used && s_cache[slot].data != NULL) {
        free(s_cache[slot].data);
    }

    logo_entry_t *e = &s_cache[slot];
    memset(e, 0, sizeof(*e));
    strlcpy(e->icao, icao, sizeof(e->icao));
    e->data = data;
    e->used = true;
    e->last_used = s_tick;
    e->dsc.header.always_zero = 0;
    e->dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    e->dsc.header.w = w;
    e->dsc.header.h = h;
    e->dsc.data = e->data;
    e->dsc.data_size = size;
}
