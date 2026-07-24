#pragma once

#include "esp_err.h"
#include "flight_model.h"

/* Fetch aircraft within radius_nm of lat/lon. Tries airplanes.live first,
 * falls back to adsb.lol (same JSON schema). Result sorted by distance. */
esp_err_t flight_fetch_nearby(double lat, double lon, int radius_nm, aircraft_list_t *out);

/* Airline-shaped traffic heuristic (AAA123 callsign + non-light category). */
bool flight_is_airline(const aircraft_t *ac);

/* Coarse class for the user filter / list markers. */
flight_class_t flight_class(const aircraft_t *ac);
