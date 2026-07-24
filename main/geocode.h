#pragma once

#include "esp_err.h"

typedef struct {
    char   name[40];
    char   region[32];
    char   country[8];      /* ISO code */
    double lat;
    double lon;
} geocode_result_t;

/* Search cities by name via the Open-Meteo geocoding API (free, no key).
 * Blocking; call from a worker task. */
esp_err_t geocode_search(const char *query, geocode_result_t *results, int max, int *count);

/* Reverse: nearest town/city name for coordinates, via OSM Nominatim
 * (free, no key; called once per location change, well within fair use).
 * Blocking; call from a worker task. */
esp_err_t geocode_reverse(double lat, double lon, char *city, size_t city_len);
