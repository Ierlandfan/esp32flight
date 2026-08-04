#include "ships.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "geo_math.h"
#include "settings.h"

static const char *TAG = "ships";

#define SHIP_TTL_US   (10LL * 60 * 1000 * 1000)
#define BOX_LAT       1.0
#define BOX_LON       1.6

static SemaphoreHandle_t s_mux;
static ship_t s_ships[MAX_SHIPS];
static char s_active_key[48];
static double s_home_lat, s_home_lon;
static double s_box_lat, s_box_lon;   /* box center the subscription used */
static volatile bool s_subscribed;
static volatile int64_t s_last_frame;   /* stream health watchdog */

/* transport shims, one set per build */
static bool tr_active(void);
static bool tr_connected(void);
static void tr_start(void);
static void tr_stop(void);
static bool tr_send(const char *buf, size_t len);

static void lock(void)
{
    if (s_mux == NULL) {
        s_mux = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_mux, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mux);
}

/* voyage data arrives separately; merge it into a known ship's slot */
static void store_static(const cJSON *ssd)
{
    const cJSON *id = cJSON_GetObjectItem(ssd, "UserID");
    if (!cJSON_IsNumber(id)) {
        return;
    }
    uint32_t mmsi = (uint32_t)id->valuedouble;
    lock();
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (s_ships[i].mmsi != mmsi || s_ships[i].seen_us == 0) {
            continue;
        }
        const cJSON *de = cJSON_GetObjectItem(ssd, "Destination");
        if (cJSON_IsString(de) && de->valuestring[0] != '\0') {
            strlcpy(s_ships[i].dest, de->valuestring, sizeof(s_ships[i].dest));
            for (int k = (int)strlen(s_ships[i].dest) - 1;
                 k >= 0 && s_ships[i].dest[k] == ' '; k--) {
                s_ships[i].dest[k] = '\0';
            }
        }
        const cJSON *ty = cJSON_GetObjectItem(ssd, "Type");
        if (cJSON_IsNumber(ty)) {
            s_ships[i].stype = (uint8_t)ty->valuedouble;
        }
        break;
    }
    unlock();
}

static void store_report(const cJSON *root, const char *raw)
{
    const cJSON *msg = cJSON_GetObjectItem(root, "Message");
    const cJSON *ssd = msg != NULL ? cJSON_GetObjectItem(msg, "ShipStaticData") : NULL;
    if (ssd != NULL) {
        store_static(ssd);
        return;
    }
    const cJSON *pr = msg != NULL ? cJSON_GetObjectItem(msg, "PositionReport") : NULL;
    const cJSON *meta = cJSON_GetObjectItem(root, "MetaData");
    const cJSON *la = pr != NULL ? cJSON_GetObjectItem(pr, "Latitude") : NULL;
    const cJSON *lo = pr != NULL ? cJSON_GetObjectItem(pr, "Longitude") : NULL;
    if (!cJSON_IsNumber(la) || !cJSON_IsNumber(lo)) {
        ESP_LOGW(TAG, "unexpected frame: %.120s", raw);
        return;
    }
    uint32_t mmsi = 0;
    const cJSON *id = pr != NULL ? cJSON_GetObjectItem(pr, "UserID") : NULL;
    if (cJSON_IsNumber(id)) {
        mmsi = (uint32_t)id->valuedouble;
    }

    lock();
    ship_t *slot = NULL;
    int64_t oldest = INT64_MAX;
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (s_ships[i].mmsi == mmsi && mmsi != 0) {
            slot = &s_ships[i];
            break;
        }
        if (s_ships[i].seen_us < oldest) {
            oldest = s_ships[i].seen_us;
            slot = &s_ships[i];
        }
    }
    if (slot->mmsi != mmsi) {
        slot->dest[0] = '\0';
        slot->stype = 0;
        slot->name[0] = '\0';
    }
    slot->mmsi = mmsi;
    slot->lat = la->valuedouble;
    slot->lon = lo->valuedouble;
    const cJSON *sog = pr != NULL ? cJSON_GetObjectItem(pr, "Sog") : NULL;
    const cJSON *cog = pr != NULL ? cJSON_GetObjectItem(pr, "Cog") : NULL;
    slot->sog_kt = cJSON_IsNumber(sog) ? (float)sog->valuedouble : 0;
    slot->cog_deg = cJSON_IsNumber(cog) ? (float)cog->valuedouble : 0;
    const cJSON *nm = meta != NULL ? cJSON_GetObjectItem(meta, "ShipName") : NULL;
    if (cJSON_IsString(nm) && nm->valuestring[0] != '\0') {
        strlcpy(slot->name, nm->valuestring, sizeof(slot->name));
        /* AIS pads names with trailing spaces */
        for (int k = (int)strlen(slot->name) - 1; k >= 0 && slot->name[k] == ' '; k--) {
            slot->name[k] = '\0';
        }
    }
    slot->dist_km = (float)geo_haversine_km(s_home_lat, s_home_lon, slot->lat, slot->lon);
    slot->seen_us = esp_timer_get_time();
    unlock();
}

/* one complete websocket message in, regardless of the transport */
static void ships_ingest(const char *json)
{
    s_last_frame = esp_timer_get_time();
    cJSON *root = cJSON_Parse(json);
    if (root != NULL) {
        store_report(root, json);
        cJSON_Delete(root);
    }
}

static void clear_fleet(void)
{
    lock();
    memset(s_ships, 0, sizeof(s_ships));
    unlock();
}

int ships_get(ship_t *out, int max)
{
    int n = 0;
    int64_t now = esp_timer_get_time();
    lock();
    for (int i = 0; i < MAX_SHIPS && n < max; i++) {
        if (s_ships[i].seen_us != 0 && now - s_ships[i].seen_us < SHIP_TTL_US) {
            out[n++] = s_ships[i];
        }
    }
    unlock();
    return n;
}

void ships_poll(double home_lat, double home_lon)
{
    const settings_t *cfg = settings_get();
    bool want = cfg->ships_enabled && cfg->ais_key[0] != '\0';
    s_home_lat = home_lat;
    s_home_lon = home_lon;

    bool moved = tr_active() &&
                 geo_haversine_km(s_box_lat, s_box_lon, home_lat, home_lon) > 30.0;
    bool rekeyed = tr_active() && strcmp(s_active_key, cfg->ais_key) != 0;

    if (!want || moved || rekeyed) {
        if (tr_active()) {
            tr_stop();
        }
        if (!want) {
            return;
        }
    }
    if (!tr_active()) {
        strlcpy(s_active_key, cfg->ais_key, sizeof(s_active_key));
        s_box_lat = home_lat;
        s_box_lon = home_lon;
        tr_start();
    }
    /* a healthy subscription near a coast never goes quiet for long;
       silence means the server dropped us without closing - reconnect */
    if (tr_active() && s_subscribed &&
        esp_timer_get_time() - s_last_frame > 5LL * 60 * 1000 * 1000) {
        ESP_LOGW(TAG, "stream quiet for 5 min, reconnecting");
        tr_stop();
    }
    if (tr_active() && !s_subscribed && tr_connected()) {
        char sub[320];
        snprintf(sub, sizeof(sub),
                 "{\"APIKey\":\"%s\",\"BoundingBoxes\":[[[%.3f,%.3f],[%.3f,%.3f]]],"
                 "\"FilterMessageTypes\":[\"PositionReport\",\"ShipStaticData\"]}",
                 s_active_key,
                 s_box_lat - BOX_LAT, s_box_lon - BOX_LON,
                 s_box_lat + BOX_LAT, s_box_lon + BOX_LON);
        if (tr_send(sub, strlen(sub))) {
            s_subscribed = true;
            s_last_frame = esp_timer_get_time();
            ESP_LOGI(TAG, "subscribed around %.2f,%.2f", s_box_lat, s_box_lon);
        }
    }
}

#if defined(SIMSHOT)
/* ------------- headless sim: no AIS transport, fleet stays empty ------------- */

static bool tr_active(void)    { return false; }
static bool tr_connected(void) { return false; }
static void tr_stop(void)      { clear_fleet(); }
static void tr_start(void)     { }
static bool tr_send(const char *buf, size_t len) { (void)buf; (void)len; return false; }

#elif !defined(APKFLIGHT)
/* ---------------- ESP-IDF transport: esp_websocket_client ---------------- */

#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"

static esp_websocket_client_handle_t s_ws;

static void ws_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ev = event_data;
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_ERROR) {
        s_subscribed = false;
        ESP_LOGW(TAG, "websocket %s", event_id == WEBSOCKET_EVENT_ERROR ? "error" : "disconnected");
    }
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        /* the subscription is sent from ships_poll: sending from this
           handler runs inside the client task and the send can deadlock */
        s_subscribed = false;
        ESP_LOGI(TAG, "connected");
    }
    /* aisstream serves JSON in binary frames (opcode 2) */
    if (event_id == WEBSOCKET_EVENT_DATA && (ev->op_code == 0x1 || ev->op_code == 0x2) &&
        ev->data_len > 0 && ev->payload_offset == 0) {
        /* frames are small JSON messages; ignore fragmented oversize ones */
        char *json = malloc(ev->data_len + 1);
        if (json != NULL) {
            memcpy(json, ev->data_ptr, ev->data_len);
            json[ev->data_len] = '\0';
            ships_ingest(json);
            free(json);
        }
    }
}

static bool tr_active(void)
{
    return s_ws != NULL;
}

static bool tr_connected(void)
{
    return s_ws != NULL && esp_websocket_client_is_connected(s_ws);
}

static void tr_stop(void)
{
    if (s_ws != NULL) {
        esp_websocket_client_close(s_ws, pdMS_TO_TICKS(2000));
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
        ESP_LOGI(TAG, "stopped");
    }
    s_subscribed = false;
    clear_fleet();
}

static void tr_start(void)
{
    esp_websocket_client_config_t cfg = {
        .uri = "wss://stream.aisstream.io/v0/stream",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 15000,
        .network_timeout_ms = 10000,
        .buffer_size = 4096,
    };
    s_ws = esp_websocket_client_init(&cfg);
    if (s_ws == NULL) {
        ESP_LOGE(TAG, "client init failed");
        return;
    }
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    esp_websocket_client_start(s_ws);
}

static bool tr_send(const char *buf, size_t len)
{
    return esp_websocket_client_send_text(s_ws, buf, len, pdMS_TO_TICKS(5000)) >= 0;
}

#else
/* ------------- app transport: libcurl websocket + reader thread ------------- */

#include <curl/curl.h>
#include <pthread.h>
#include <unistd.h>

const char *apk_http_cainfo(void);

static CURL *s_ws;
static pthread_t s_reader;
static volatile bool s_run;
static volatile bool s_thread_live;

static void *reader_main(void *arg)
{
    uint8_t buf[4096];
    static char acc[16384];
    size_t acc_len = 0;
    bool poisoned = false;

    while (s_run) {
        size_t rlen = 0;
        const struct curl_ws_frame *meta = NULL;
        CURLcode r = curl_ws_recv(s_ws, buf, sizeof(buf), &rlen, &meta);
        if (r == CURLE_AGAIN) {
            usleep(80 * 1000);
            continue;
        }
        if (r != CURLE_OK || meta == NULL) {
            ESP_LOGW(TAG, "websocket read failed (%d)", (int)r);
            s_subscribed = false;
            break;
        }
        if (meta->flags & CURLWS_PING) {
            size_t sent = 0;
            curl_ws_send(s_ws, buf, rlen, &sent, 0, CURLWS_PONG);
            continue;
        }
        if (meta->flags & CURLWS_CLOSE) {
            ESP_LOGW(TAG, "websocket closed by server");
            s_subscribed = false;
            break;
        }
        if (!(meta->flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT))) {
            continue;
        }
        if (acc_len + rlen < sizeof(acc) - 1) {
            memcpy(acc + acc_len, buf, rlen);
            acc_len += rlen;
        } else {
            poisoned = true;   /* oversized message: drop it whole */
        }
        if (meta->bytesleft == 0) {
            if (!poisoned && acc_len > 0) {
                acc[acc_len] = '\0';
                ships_ingest(acc);
            }
            acc_len = 0;
            poisoned = false;
        }
    }
    s_thread_live = false;
    return NULL;
}

static bool tr_active(void)
{
    return s_ws != NULL;
}

static bool tr_connected(void)
{
    return s_ws != NULL && s_run && s_thread_live;
}

static void tr_stop(void)
{
    if (s_ws != NULL) {
        s_run = false;
        pthread_join(s_reader, NULL);
        curl_easy_cleanup(s_ws);
        s_ws = NULL;
        ESP_LOGI(TAG, "stopped");
    }
    s_subscribed = false;
    clear_fleet();
}

static void tr_start(void)
{
    s_ws = curl_easy_init();
    if (s_ws == NULL) {
        return;
    }
    curl_easy_setopt(s_ws, CURLOPT_URL, "wss://stream.aisstream.io/v0/stream");
    curl_easy_setopt(s_ws, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(s_ws, CURLOPT_CONNECTTIMEOUT, 10L);
    const char *ca = apk_http_cainfo();
    if (ca != NULL) {
        curl_easy_setopt(s_ws, CURLOPT_CAINFO, ca);
    }
    if (curl_easy_perform(s_ws) != CURLE_OK) {
        ESP_LOGW(TAG, "websocket connect failed");
        curl_easy_cleanup(s_ws);
        s_ws = NULL;
        return;
    }
    s_run = true;
    s_thread_live = true;
    if (pthread_create(&s_reader, NULL, reader_main, NULL) != 0) {
        s_run = false;
        s_thread_live = false;
        curl_easy_cleanup(s_ws);
        s_ws = NULL;
        return;
    }
    ESP_LOGI(TAG, "connected");
}

static bool tr_send(const char *buf, size_t len)
{
    size_t sent = 0;
    return curl_ws_send(s_ws, buf, len, &sent, 0, CURLWS_TEXT) == CURLE_OK;
}

#endif /* APKFLIGHT */
