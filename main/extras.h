#pragma once

#include <stdbool.h>

/* Optional extra radar objects: the ISS and weather radiosondes. Both are
 * polled from free keyless APIs, rate-limited inside extras_poll. */

typedef struct {
    bool   valid;
    double lat, lon;
    float  alt_km;
    float  dist_km;    /* great-circle distance of the ground track from home */
    float  elev_deg;   /* elevation above the horizon at home, <0 = not visible */
    float  az_deg;     /* azimuth from home, 0 = north */
} iss_state_t;

#define MAX_SONDES 6

typedef struct {
    char   serial[20];
    char   type[16];
    double lat, lon;
    float  alt_m;
    float  vel_v;      /* m/s, positive = ascending */
    float  dist_km;
} sonde_t;

/* Call from the poll loop; fetches whatever is enabled and due. */
void extras_poll(double home_lat, double home_lon);

bool extras_get_iss(iss_state_t *out);
int  extras_get_sondes(sonde_t *out, int max);
