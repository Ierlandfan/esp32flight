#include "ships.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "geo_math.h"
#include "settings.h"

static const char *TAG = "ships";

#ifdef APKFLIGHT

/* The Android build has no websocket client; the option is hidden there. */
void ships_poll(double home_lat, double home_lon)
{
    (void)home_lat;
    (void)home_lon;
}

int ships_get(ship_t *out, int max)
{
    (void)out;
    (void)max;
    return 0;
}

#else

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SHIP_TTL_US   (10LL * 60 * 1000 * 1000)
#define BOX_LAT       1.0
#define BOX_LON       1.6

static SemaphoreHandle_t s_mux;
static ship_t s_ships[MAX_SHIPS];
static esp_websocket_client_handle_t s_ws;
static char s_active_key[48];
static double s_home_lat, s_home_lon;
static double s_box_lat, s_box_lon;   /* box center the subscription used */

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

static void store_report(const cJSON *root)
{
    const cJSON *msg = cJSON_GetObjectItem(root, "Message");
    const cJSON *pr = msg != NULL ? cJSON_GetObjectItem(msg, "PositionReport") : NULL;
    const cJSON *meta = cJSON_GetObjectItem(root, "MetaData");
    const cJSON *la = pr != NULL ? cJSON_GetObjectItem(pr, "Latitude") : NULL;
    const cJSON *lo = pr != NULL ? cJSON_GetObjectItem(pr, "Longitude") : NULL;
    if (!cJSON_IsNumber(la) || !cJSON_IsNumber(lo)) {
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

static void ws_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ev = event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        char sub[320];
        snprintf(sub, sizeof(sub),
                 "{\"APIKey\":\"%s\",\"BoundingBoxes\":[[[%.3f,%.3f],[%.3f,%.3f]]],"
                 "\"FilterMessageTypes\":[\"PositionReport\"]}",
                 s_active_key,
                 s_box_lat - BOX_LAT, s_box_lon - BOX_LON,
                 s_box_lat + BOX_LAT, s_box_lon + BOX_LON);
        esp_websocket_client_send_text(s_ws, sub, strlen(sub), pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "connected, subscribed around %.2f,%.2f", s_box_lat, s_box_lon);
    } else if (event_id == WEBSOCKET_EVENT_DATA && ev->op_code == 0x1 &&
               ev->data_len > 0 && ev->payload_offset == 0) {
        /* frames are small JSON messages; ignore fragmented oversize ones */
        char *json = malloc(ev->data_len + 1);
        if (json != NULL) {
            memcpy(json, ev->data_ptr, ev->data_len);
            json[ev->data_len] = '\0';
            cJSON *root = cJSON_Parse(json);
            if (root != NULL) {
                store_report(root);
                cJSON_Delete(root);
            }
            free(json);
        }
    }
}

static void ws_stop(void)
{
    if (s_ws != NULL) {
        esp_websocket_client_close(s_ws, pdMS_TO_TICKS(2000));
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
        ESP_LOGI(TAG, "stopped");
    }
    lock();
    memset(s_ships, 0, sizeof(s_ships));
    unlock();
}

static void ws_start(void)
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

void ships_poll(double home_lat, double home_lon)
{
    const settings_t *cfg = settings_get();
    bool want = cfg->ships_enabled && cfg->ais_key[0] != '\0';
    s_home_lat = home_lat;
    s_home_lon = home_lon;

    bool moved = s_ws != NULL &&
                 geo_haversine_km(s_box_lat, s_box_lon, home_lat, home_lon) > 30.0;
    bool rekeyed = s_ws != NULL && strcmp(s_active_key, cfg->ais_key) != 0;

    if (!want || moved || rekeyed) {
        if (s_ws != NULL) {
            ws_stop();
        }
        if (!want) {
            return;
        }
    }
    if (s_ws == NULL) {
        strlcpy(s_active_key, cfg->ais_key, sizeof(s_active_key));
        s_box_lat = home_lat;
        s_box_lon = home_lon;
        ws_start();
    }
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

#endif /* APKFLIGHT */
