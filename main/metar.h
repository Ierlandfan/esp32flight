#pragma once

#include <stddef.h>

#include <stdbool.h>

/* Fetch the raw METAR for a station (aviationweather.gov). Blocking. */
bool metar_fetch(const char *icao);

/* Latest raw METAR string ("" when none fetched yet). */
const char *metar_get(void);

/* TAF, fetched separately (settings taf_enabled). Raw one-line form. */
bool taf_fetch(const char *icao);
const char *taf_get(void);

/* Human-readable one-liner built from the raw METAR (localized, unit-aware). */
const char *metar_decoded(char *out, size_t n);
