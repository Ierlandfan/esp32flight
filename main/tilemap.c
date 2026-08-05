#include "tilemap.h"
#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "extra/libs/png/lodepng.h"
#include "rainviewer.h"
#include "settings.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "tilemap";

/* Only one tile render at a time: concurrent renders (radar + ambient +
 * full map) each open a TLS session and the combined memory peak starves
 * mbedTLS into alloc failures. */
static SemaphoreHandle_t s_render_mux;

static QueueHandle_t s_job_q;

static void tile_worker(void *arg)
{
    tile_job_fn job;
    for (;;) {
        if (xQueueReceive(s_job_q, &job, portMAX_DELAY) == pdTRUE && job != NULL) {
            job();
        }
    }
}

bool tilemap_worker_submit(tile_job_fn job)
{
    if (s_job_q == NULL) {
        return false;
    }
    return xQueueSend(s_job_q, &job, 0) == pdTRUE;
}

void tilemap_init(void)
{
    s_render_mux = xSemaphoreCreateMutex();
    s_job_q = xQueueCreate(8, sizeof(tile_job_fn));
    if (s_job_q != NULL &&
        xTaskCreatePinnedToCore(tile_worker, "tile_worker", 10240,
                                NULL, 3, NULL, 0) != pdPASS) {
        vQueueDelete(s_job_q);
        s_job_q = NULL;
        ESP_LOGE(TAG, "tile worker create FAILED");
    }
}

#define TILE_PX     256
#define MAX_ZOOM    11
#define TILE_BUF    (96 * 1024)

/* LRU cache of compressed tile PNGs in PSRAM: repeat map opens render
 * instantly and survive short network outages. Guarded by s_render_mux
 * (all access happens inside a render). */
#define TCACHE_N       128
#define TCACHE_BUDGET  (768 * 1024)   /* PSRAM got crowded; transient buffers need headroom */

typedef struct {
    uint32_t key;           /* (z << 26) | (tx << 13) | ty */
    uint8_t *data;
    uint32_t len;
    uint32_t used_at;
} tcache_entry_t;

static tcache_entry_t s_tc[TCACHE_N];
static size_t s_tc_bytes;
static uint32_t s_tc_tick;

static uint8_t *tcache_get(uint32_t key, uint32_t *len)
{
    for (int i = 0; i < TCACHE_N; i++) {
        if (s_tc[i].data != NULL && s_tc[i].key == key) {
            s_tc[i].used_at = ++s_tc_tick;
            *len = s_tc[i].len;
            return s_tc[i].data;
        }
    }
    return NULL;
}

static void tcache_drop(int i)
{
    free(s_tc[i].data);
    s_tc_bytes -= s_tc[i].len;
    s_tc[i].data = NULL;
    s_tc[i].len = 0;
}

static void tcache_put(uint32_t key, const uint8_t *data, uint32_t len)
{
    if (len == 0 || len > TCACHE_BUDGET / 8) {
        return;
    }
    while (s_tc_bytes + len > TCACHE_BUDGET) {
        int lru = -1;
        for (int i = 0; i < TCACHE_N; i++) {
            if (s_tc[i].data != NULL &&
                (lru < 0 || s_tc[i].used_at < s_tc[lru].used_at)) {
                lru = i;
            }
        }
        if (lru < 0) {
            return;
        }
        tcache_drop(lru);
    }
    int slot = -1;
    for (int i = 0; i < TCACHE_N; i++) {
        if (s_tc[i].data == NULL) {
            slot = i;
            break;
        }
        if (slot < 0 || s_tc[i].used_at < s_tc[slot].used_at) {
            slot = i;
        }
    }
    if (s_tc[slot].data != NULL) {
        tcache_drop(slot);
    }
    uint8_t *copy = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, data, len);
    s_tc[slot].key = key;
    s_tc[slot].data = copy;
    s_tc[slot].len = len;
    s_tc[slot].used_at = ++s_tc_tick;
    s_tc_bytes += len;
}

/* WGS84 -> normalized web mercator (0..1) */
static void merc_norm(double lat, double lon, double *nx, double *ny)
{
    if (lat > 85.0511) lat = 85.0511;
    if (lat < -85.0511) lat = -85.0511;
    double rad = lat * M_PI / 180.0;
    *nx = (lon + 180.0) / 360.0;
    *ny = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0;
}

void tilemap_project(const tile_view_t *v, double lat, double lon, int *x, int *y)
{
    double nx, ny;
    merc_norm(lat, lon, &nx, &ny);
    double world = (double)TILE_PX * (1 << v->z);
    /* views that cross the antimeridian have px0 outside [0, world);
       pick the copy of the world that lands nearest the view center */
    double px = nx * world - v->px0;
    double center = v->w / 2.0;
    if (px - center > world / 2.0) {
        px -= world;
    } else if (center - px > world / 2.0) {
        px += world;
    }
    *x = (int)px;
    *y = (int)(ny * world - v->py0);
}

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
} tile_sink_t;

static esp_err_t tile_http_cb(esp_http_client_event_t *evt)
{
    tile_sink_t *s = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && s != NULL) {
        size_t n = evt->data_len;
        if (n > s->cap - s->len) {
            n = s->cap - s->len;
        }
        memcpy(s->buf + s->len, evt->data, n);
        s->len += n;
    }
    return ESP_OK;
}

/* Fetch one tile (reusing the keep-alive connection) and blit it into dst
 * at (ox, oy). Returns false on failure. */
static bool blit_tile(esp_http_client_handle_t client, tile_sink_t *sink,
                      uint16_t *dst, int dst_w, int dst_h,
                      int z, int tx, int ty, int ox, int oy)
{
    uint32_t key = ((uint32_t)z << 26) | ((uint32_t)tx << 13) | (uint32_t)ty;
    uint32_t clen = 0;
    const uint8_t *fetch_buf = tcache_get(key, &clen);
    size_t len = clen;
    bool from_cache = fetch_buf != NULL;

    if (!from_cache) {
        char url[96];
        snprintf(url, sizeof(url),
                 "https://basemaps.cartocdn.com/dark_all/%d/%d/%d.png", z, tx, ty);
        sink->len = 0;
        esp_http_client_set_url(client, url);
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (err != ESP_OK || status != 200 || sink->len < 8) {
            ESP_LOGW(TAG, "tile %d/%d/%d: %s http=%d len=%u",
                     z, tx, ty, esp_err_to_name(err), status, (unsigned)sink->len);
            /* after a TLS-level error the session context may be gone; reusing
             * the connection crashes inside mbedtls_ssl_write. Force a clean
             * reconnect for the next tile. */
            esp_http_client_close(client);
            return false;
        }
        tcache_put(key, sink->buf, sink->len);
        fetch_buf = sink->buf;
        len = sink->len;
    }

    unsigned char *rgba = NULL;
    unsigned w = 0, h = 0;
    if (lodepng_decode32(&rgba, &w, &h, fetch_buf, len) != 0 || rgba == NULL) {
        free(rgba);   /* lodepng may allocate even when it reports an error */
        return false;
    }

    const bool light = settings_get()->map_light;
    /* clip the x-range once per tile instead of testing every pixel */
    int x0 = ox < 0 ? -ox : 0;
    int x1 = ox + (int)w > dst_w ? dst_w - ox : (int)w;
    for (int y = 0; y < (int)h; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= dst_h) {
            continue;
        }
        const unsigned char *src = rgba + (size_t)y * w * 4;
        for (int x = x0; x < x1; x++) {
            const unsigned char *p = src + x * 4;
            int r = p[0], g = p[1], b = p[2];
            if (light) {
                /* one-time lift at decode: v += 30% of headroom (#10) */
                r += ((255 - r) * 77) >> 8;
                g += ((255 - g) * 77) >> 8;
                b += ((255 - b) * 77) >> 8;
            }
            dst[(size_t)dy * dst_w + (ox + x)] =
                ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
    free(rgba);
    return true;
}

/* Fetch one RainViewer tile and alpha-blend it over dst. Failures are
 * non-fatal: rain is a garnish, not the map. */
/* RainViewer serves real data only up to this zoom; above it the API
 * returns a "Zoom Level Not Supported" placeholder tile (verified
 * empirically: z8+ responses are a constant 1.4 KB plate). For closer
 * views we fetch the z7 parent and magnify the right quadrant. */
#define RAIN_MAX_Z 7

static void blit_rain_tile(esp_http_client_handle_t client, tile_sink_t *sink,
                           const char *frame, uint16_t *dst, int dst_w, int dst_h,
                           int z, int tx, int ty, int ox, int oy)
{
    int shift = z > RAIN_MAX_Z ? z - RAIN_MAX_Z : 0;
    int zc = z - shift;
    int ptx = tx >> shift;
    int pty = ty >> shift;
    /* this tile's quadrant inside the parent tile, in parent pixels */
    int sub_x = (tx - (ptx << shift)) * (TILE_PX >> shift);
    int sub_y = (ty - (pty << shift)) * (TILE_PX >> shift);

    uint32_t key = 0x80000000u |
                   (((uint32_t)rainviewer_generation() & 0xF) << 27) |
                   (((uint32_t)zc & 0xF) << 23) |
                   (((uint32_t)ptx & 0x7FF) << 12) | ((uint32_t)pty & 0xFFF);
    uint32_t clen = 0;
    const uint8_t *fetch_buf = tcache_get(key, &clen);
    size_t len = clen;

    if (fetch_buf == NULL) {
        char url[224];
        snprintf(url, sizeof(url), "%s/256/%d/%d/%d/2/1_1.png", frame, zc, ptx, pty);
        sink->len = 0;
        esp_http_client_set_url(client, url);
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (err != ESP_OK || status != 200 || sink->len < 8) {
            esp_http_client_close(client);
            return;
        }
        tcache_put(key, sink->buf, sink->len);
        fetch_buf = sink->buf;
        len = sink->len;
    }

    unsigned char *rgba = NULL;
    unsigned w = 0, h = 0;
    if (lodepng_decode32(&rgba, &w, &h, fetch_buf, len) != 0 || rgba == NULL) {
        free(rgba);   /* lodepng may allocate even when it reports an error */
        return;
    }
    for (int y = 0; y < TILE_PX; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= dst_h) {
            continue;
        }
        /* source row in parent pixels, 8.8 fixed point for bilinear */
        int fy = (sub_y << 8) + ((y << 8) >> shift);
        int sy = fy >> 8;
        if (sy >= (int)h) {
            continue;
        }
        int wy = fy & 0xFF;
        int sy1 = sy + 1 < (int)h ? sy + 1 : sy;
        const unsigned char *row0 = rgba + (size_t)sy * w * 4;
        const unsigned char *row1 = rgba + (size_t)sy1 * w * 4;
        for (int x = 0; x < TILE_PX; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= dst_w) {
                continue;
            }
            int fx = (sub_x << 8) + ((x << 8) >> shift);
            int sx = fx >> 8;
            if (sx >= (int)w) {
                continue;
            }
            int a, cr, cg, cb;
            if (shift == 0) {
                const unsigned char *pc = row0 + sx * 4;
                a = pc[3];
                cr = pc[0];
                cg = pc[1];
                cb = pc[2];
            } else {
                /* bilinear with premultiplied color so transparent
                   neighbours do not darken the echo edges */
                int wx = fx & 0xFF;
                int sx1 = sx + 1 < (int)w ? sx + 1 : sx;
                const unsigned char *p00 = row0 + sx * 4;
                const unsigned char *p01 = row0 + sx1 * 4;
                const unsigned char *p10 = row1 + sx * 4;
                const unsigned char *p11 = row1 + sx1 * 4;
                int w00 = (256 - wx) * (256 - wy);
                int w01 = wx * (256 - wy);
                int w10 = (256 - wx) * wy;
                int w11 = wx * wy;
                a = (p00[3] * w00 + p01[3] * w01 + p10[3] * w10 + p11[3] * w11) >> 16;
                if (a > 0) {
                    cr = (p00[0] * p00[3] * (w00 >> 4) + p01[0] * p01[3] * (w01 >> 4) +
                          p10[0] * p10[3] * (w10 >> 4) + p11[0] * p11[3] * (w11 >> 4)) /
                         ((a << 12) + 1);
                    cg = (p00[1] * p00[3] * (w00 >> 4) + p01[1] * p01[3] * (w01 >> 4) +
                          p10[1] * p10[3] * (w10 >> 4) + p11[1] * p11[3] * (w11 >> 4)) /
                         ((a << 12) + 1);
                    cb = (p00[2] * p00[3] * (w00 >> 4) + p01[2] * p01[3] * (w01 >> 4) +
                          p10[2] * p10[3] * (w10 >> 4) + p11[2] * p11[3] * (w11 >> 4)) /
                         ((a << 12) + 1);
                    if (cr > 255) cr = 255;
                    if (cg > 255) cg = 255;
                    if (cb > 255) cb = 255;
                } else {
                    cr = cg = cb = 0;
                }
            }
            a = a * 3 / 4;      /* keep the basemap readable */
            if (a < 10) {
                continue;
            }
            uint16_t bg = dst[(size_t)dy * dst_w + dx];
            int br = (bg >> 11) << 3, bgc = ((bg >> 5) & 0x3F) << 2, bb = (bg & 0x1F) << 3;
            int r = (cr * a + br * (255 - a)) / 255;
            int g = (cg * a + bgc * (255 - a)) / 255;
            int b = (cb * a + bb * (255 - a)) / 255;
            dst[(size_t)dy * dst_w + dx] =
                ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
    free(rgba);
}

#define TM_LAYER_BASE 1
#define TM_LAYER_RAIN 2

static bool render_impl(uint16_t *dst, int dst_w, int dst_h,
                        double lat_min, double lat_max,
                        double lon_min, double lon_max,
                        tile_view_t *out_view, int layers)
{
    double nx0, ny0, nx1, ny1;
    merc_norm(lat_max, lon_min, &nx0, &ny0);   /* top-left */
    merc_norm(lat_min, lon_max, &nx1, &ny1);   /* bottom-right */
    if (nx1 <= nx0) {
        nx1 = nx0 + 1e-6;
    }
    if (ny1 <= ny0) {
        ny1 = ny0 + 1e-6;
    }

    /* Highest zoom at which the bbox still fits the destination */
    int z = MAX_ZOOM;
    while (z > 0) {
        double world = (double)TILE_PX * (1 << z);
        if ((nx1 - nx0) * world <= dst_w && (ny1 - ny0) * world <= dst_h) {
            break;
        }
        z--;
    }

    double world = (double)TILE_PX * (1 << z);
    double cx = (nx0 + nx1) / 2.0 * world;
    double cy = (ny0 + ny1) / 2.0 * world;
    double px0 = cx - dst_w / 2.0;
    double py0 = cy - dst_h / 2.0;
    if (py0 < 0) {
        py0 = 0;
    }
    if (py0 + dst_h > world) {
        py0 = world - dst_h;
    }
    /* no X clamp: longitude wraps, and antimeridian views need px0
       outside [0, world); the tile loop wraps tile indexes instead */

    /* dark background for any gaps (pure black in rain-only mode) */
    uint16_t bgcol = (layers & TM_LAYER_BASE) ? 0x10A2 : 0x0000;
    for (size_t i = 0; i < (size_t)dst_w * dst_h; i++) {
        dst[i] = bgcol;
    }

    tile_sink_t sink = {
        .buf = heap_caps_malloc(TILE_BUF, MALLOC_CAP_SPIRAM),
        .cap = TILE_BUF,
        .len = 0,
    };
    if (sink.buf == NULL) {
        return false;
    }
    esp_http_client_config_t cfg = {
        .url = "https://basemaps.cartocdn.com/dark_all/0/0/0.png",
        .event_handler = tile_http_cb,
        .user_data = &sink,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .user_agent = "esp32flight/1.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(sink.buf);
        return false;
    }

    int tx0 = (int)floor(px0 / TILE_PX);
    int ty0 = (int)floor(py0 / TILE_PX);
    int tx1 = (int)floor((px0 + dst_w - 1) / TILE_PX);
    int ty1 = (int)floor((py0 + dst_h - 1) / TILE_PX);
    int tiles_max = 1 << z;
    int ok = 0, total = 0;
    bool offline = false;
    struct { int16_t tx, txw, ty; } failed[32];
    int failed_n = 0;
    if (!(layers & TM_LAYER_BASE)) {
        ok = 1;         /* rain-only: nothing mandatory below */
        total = 1;
        goto rain_pass;
    }
    for (int ty = ty0; ty <= ty1 && !offline; ty++) {
        if (ty < 0 || ty >= tiles_max) {
            continue;
        }
        for (int tx = tx0; tx <= tx1; tx++) {
            /* wrap around the antimeridian instead of skipping */
            int txw = ((tx % tiles_max) + tiles_max) % tiles_max;
            total++;
            if (blit_tile(client, &sink, dst, dst_w, dst_h, z, txw, ty,
                          (int)(tx * TILE_PX - px0), (int)(ty * TILE_PX - py0))) {
                ok++;
            } else if (failed_n < 32) {
                failed[failed_n].tx = tx;
                failed[failed_n].txw = txw;
                failed[failed_n].ty = ty;
                failed_n++;
            }
            /* many straight failures with zero successes: likely offline,
             * stop the first pass but still give the retry pass a chance */
            if (total >= 5 && ok == 0) {
                offline = true;
                break;
            }
        }
    }
    /* one retry pass: early requests occasionally fail while the TLS
     * connection warms up */
    for (int i = 0; i < failed_n; i++) {
        if (blit_tile(client, &sink, dst, dst_w, dst_h, z, failed[i].txw, failed[i].ty,
                      (int)(failed[i].tx * TILE_PX - px0),
                      (int)(failed[i].ty * TILE_PX - py0))) {
            ok++;
        }
    }
rain_pass:
    /* optional precipitation layer on top (RainViewer, same tile grid) */
    if ((layers & TM_LAYER_RAIN) && ok > 0) {
        const char *frame = rainviewer_frame_path();
        if (frame[0] != '\0') {
            for (int ty = ty0; ty <= ty1; ty++) {
                if (ty < 0 || ty >= tiles_max) {
                    continue;
                }
                for (int tx = tx0; tx <= tx1; tx++) {
                    int txw = ((tx % tiles_max) + tiles_max) % tiles_max;
                    blit_rain_tile(client, &sink, frame, dst, dst_w, dst_h, z, txw, ty,
                                   (int)(tx * TILE_PX - px0), (int)(ty * TILE_PX - py0));
                }
            }
        }
    }

    esp_http_client_cleanup(client);
    free(sink.buf);

    ESP_LOGI(TAG, "z%d: %d/%d tiles (dst %dx%d, bbox %.3f..%.3f / %.3f..%.3f)",
             z, ok, total, dst_w, dst_h, lat_min, lat_max, lon_min, lon_max);
    if (ok == 0) {
        return false;
    }
    out_view->z = z;
    out_view->px0 = px0;
    out_view->py0 = py0;
    out_view->w = dst_w;
    out_view->h = dst_h;
    out_view->missing = (layers & TM_LAYER_BASE) ? total - ok : 0;
    return true;
}

bool tilemap_render(uint16_t *dst, int dst_w, int dst_h,
                    double lat_min, double lat_max,
                    double lon_min, double lon_max,
                    tile_view_t *out_view)
{
    if (s_render_mux != NULL) {
        xSemaphoreTake(s_render_mux, portMAX_DELAY);
    }
    int layers = TM_LAYER_BASE;
    if (settings_get()->rain_overlay) {
        layers |= TM_LAYER_RAIN;
    }
    bool ok = render_impl(dst, dst_w, dst_h,
                          lat_min, lat_max, lon_min, lon_max, out_view, layers);
    if (s_render_mux != NULL) {
        xSemaphoreGive(s_render_mux);
    }
    return ok;
}

bool tilemap_render_rain(uint16_t *dst, int dst_w, int dst_h,
                         double lat_min, double lat_max,
                         double lon_min, double lon_max,
                         tile_view_t *out_view)
{
    if (s_render_mux != NULL) {
        xSemaphoreTake(s_render_mux, portMAX_DELAY);
    }
    bool ok = render_impl(dst, dst_w, dst_h,
                          lat_min, lat_max, lon_min, lon_max, out_view,
                          TM_LAYER_RAIN);
    if (s_render_mux != NULL) {
        xSemaphoreGive(s_render_mux);
    }
    return ok;
}
