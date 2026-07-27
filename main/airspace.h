#pragma once

#include <stdbool.h>

/* Optional airspace outlines (CTR/TMA/danger/restricted) from openAIP.
 * Needs the user's free openAIP API key. Fetched once per location. */

#define MAX_AIRSPACES     28
#define MAX_ASP_POINTS    48

typedef struct {
    char  name[40];
    int   type;         /* openAIP numeric type, see airspace_type_str */
    int   n_points;
    float lat[MAX_ASP_POINTS];
    float lon[MAX_ASP_POINTS];
} airspace_t;

/* Fetch when enabled, keyed and due (moved / never fetched). Rate-limited. */
void airspace_poll(double home_lat, double home_lon);

int  airspace_count(void);
const airspace_t *airspace_get(int idx);
const char *airspace_type_str(int type);
