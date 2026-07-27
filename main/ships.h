#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Optional AIS ship layer via the aisstream.io websocket (user API key).
 * Not available in the Android build (no websocket client there). */

#define MAX_SHIPS 24

typedef struct {
    char     name[24];
    uint32_t mmsi;
    double   lat, lon;
    float    sog_kt;   /* speed over ground */
    float    cog_deg;  /* course over ground */
    float    dist_km;
    int64_t  seen_us;
} ship_t;

/* Reconcile the websocket with current settings; call once per poll cycle
 * after wifi is up. Starts, restarts (key/home change) or stops the stream. */
void ships_poll(double home_lat, double home_lon);

int ships_get(ship_t *out, int max);
