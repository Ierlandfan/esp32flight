#include "extras.h"

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
#include "http_util.h"
#include "settings.h"

static const char *TAG = "extras";

#define ISS_PERIOD_US   (30LL * 1000 * 1000)
#define SONDE_PERIOD_US (240LL * 1000 * 1000)
#define SONDE_RANGE_M   250000
#define EARTH_R_KM      6371.0

static SemaphoreHandle_t s_mux;
static iss_state_t s_iss;
static sonde_t s_sondes[MAX_SONDES];
static int s_sonde_count;
static int64_t s_iss_last = -ISS_PERIOD_US;
static int64_t s_sonde_last = -SONDE_PERIOD_US;

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

/* Elevation of a satellite at altitude alt_km whose ground track is
 * dist_km away: spherical-earth geometry. */
static float sat_elevation_deg(float dist_km, float alt_km)
{
    double c = dist_km / EARTH_R_KM; /* central angle */
    double e = atan2(cos(c) - EARTH_R_KM / (EARTH_R_KM + alt_km), sin(c));
    return (float)(e * 180.0 / M_PI);
}

static void fetch_iss(double home_lat, double home_lon)
{
    char *buf = malloc(1024);
    if (buf == NULL) {
        return;
    }
    iss_state_t st = { 0 };
    if (http_get_to_buffer("https://api.wheretheiss.at/v1/satellites/25544",
                           buf, 1024, NULL) == ESP_OK) {
        cJSON *root = cJSON_Parse(buf);
        const cJSON *la = cJSON_GetObjectItem(root, "latitude");
        const cJSON *lo = cJSON_GetObjectItem(root, "longitude");
        const cJSON *al = cJSON_GetObjectItem(root, "altitude");
        if (cJSON_IsNumber(la) && cJSON_IsNumber(lo) && cJSON_IsNumber(al)) {
            st.valid = true;
            st.lat = la->valuedouble;
            st.lon = lo->valuedouble;
            st.alt_km = (float)al->valuedouble;
            st.dist_km = (float)geo_haversine_km(home_lat, home_lon, st.lat, st.lon);
            st.az_deg = (float)geo_bearing_deg(home_lat, home_lon, st.lat, st.lon);
            st.elev_deg = sat_elevation_deg(st.dist_km, st.alt_km);
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
    }
    free(buf);
    lock();
    s_iss = st;
    unlock();
    if (st.valid && st.elev_deg > 0) {
        ESP_LOGI(TAG, "ISS above horizon: elev %.0f az %.0f dist %.0f km",
                 st.elev_deg, st.az_deg, st.dist_km);
    }
}

static void fetch_sondes(double home_lat, double home_lon)
{
    char url[160];
    snprintf(url, sizeof(url),
             "https://api.v2.sondehub.org/sondes?lat=%.4f&lon=%.4f&distance=%d&last=7200",
             home_lat, home_lon, SONDE_RANGE_M);
    char *buf = malloc(24 * 1024);
    if (buf == NULL) {
        return;
    }
    sonde_t found[MAX_SONDES];
    int n = 0;
    if (http_get_to_buffer(url, buf, 24 * 1024, NULL) == ESP_OK) {
        cJSON *root = cJSON_Parse(buf);
        for (cJSON *e = root != NULL ? root->child : NULL;
             e != NULL && n < MAX_SONDES; e = e->next) {
            const cJSON *la = cJSON_GetObjectItem(e, "lat");
            const cJSON *lo = cJSON_GetObjectItem(e, "lon");
            const cJSON *al = cJSON_GetObjectItem(e, "alt");
            if (!cJSON_IsNumber(la) || !cJSON_IsNumber(lo) || !cJSON_IsNumber(al)) {
                continue;
            }
            sonde_t *s = &found[n];
            memset(s, 0, sizeof(*s));
            strlcpy(s->serial, e->string != NULL ? e->string : "?", sizeof(s->serial));
            const cJSON *ty = cJSON_GetObjectItem(e, "subtype");
            if (!cJSON_IsString(ty)) {
                ty = cJSON_GetObjectItem(e, "type");
            }
            if (cJSON_IsString(ty)) {
                strlcpy(s->type, ty->valuestring, sizeof(s->type));
            }
            s->lat = la->valuedouble;
            s->lon = lo->valuedouble;
            s->alt_m = (float)al->valuedouble;
            const cJSON *vv = cJSON_GetObjectItem(e, "vel_v");
            s->vel_v = cJSON_IsNumber(vv) ? (float)vv->valuedouble : 0;
            s->dist_km = (float)geo_haversine_km(home_lat, home_lon, s->lat, s->lon);
            n++;
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
    }
    free(buf);
    lock();
    memcpy(s_sondes, found, sizeof(found));
    s_sonde_count = n;
    unlock();
    if (n > 0) {
        ESP_LOGI(TAG, "%d radiosonde(s) in range, nearest %.0f km", n, found[0].dist_km);
    }
}

void extras_poll(double home_lat, double home_lon)
{
    const settings_t *cfg = settings_get();
    int64_t now = esp_timer_get_time();

    if (cfg->iss_enabled) {
        int64_t period = ISS_PERIOD_US;
        if (s_iss.valid && s_iss.elev_deg < -20.0f) {
            period = 4 * ISS_PERIOD_US;   /* deep below the horizon: relax */
        }
        if (now - s_iss_last >= period) {
            s_iss_last = now;
            fetch_iss(home_lat, home_lon);
        }
    } else if (s_iss.valid) {
        lock();
        s_iss.valid = false;
        unlock();
    }

    if (cfg->sonde_enabled) {
        if (now - s_sonde_last >= SONDE_PERIOD_US) {
            s_sonde_last = now;
            fetch_sondes(home_lat, home_lon);
        }
    } else if (s_sonde_count > 0) {
        lock();
        s_sonde_count = 0;
        unlock();
    }
}

bool extras_get_iss(iss_state_t *out)
{
    lock();
    *out = s_iss;
    unlock();
    return out->valid;
}

int extras_get_sondes(sonde_t *out, int max)
{
    lock();
    int n = s_sonde_count < max ? s_sonde_count : max;
    memcpy(out, s_sondes, n * sizeof(sonde_t));
    unlock();
    return n;
}
