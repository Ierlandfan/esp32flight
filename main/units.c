#include "units.h"

#include <stdio.h>
#include "settings.h"

const char *units_alt(int ft, char *buf, size_t n)
{
    if (settings_get()->metric_units) {
        snprintf(buf, n, "%d m", (int)(ft * 0.3048 + 0.5));
    } else {
        snprintf(buf, n, "%d ft", ft);
    }
    return buf;
}

const char *units_speed(float kt, char *buf, size_t n)
{
    if (settings_get()->metric_units) {
        snprintf(buf, n, "%.0f km/h", (double)(kt * 1.852f));
    } else {
        snprintf(buf, n, "%.0f kt", (double)kt);
    }
    return buf;
}

const char *units_vrate(int fpm, char *buf, size_t n)
{
    if (settings_get()->metric_units) {
        snprintf(buf, n, "%+.1f m/s", fpm * 0.00508);
    } else {
        snprintf(buf, n, "%+d fpm", fpm);
    }
    return buf;
}
