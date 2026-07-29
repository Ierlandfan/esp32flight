#pragma once

#include <stddef.h>

/* Display-unit helpers honoring settings_get()->metric_units.
 * Each returns buf for easy inlining in printf-style callers. */

const char *units_alt(int ft, char *buf, size_t n);      /* 36000 ft | 10973 m  */
const char *units_speed(float kt, char *buf, size_t n);  /* 480 kt   | 889 km/h */
const char *units_vrate(int fpm, char *buf, size_t n);   /* +1472 fpm| +7.5 m/s */
