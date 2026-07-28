#!/bin/sh
# Host-side unit tests for the pure logic that has bitten us before:
# geo math (corridors, antimeridian, sun times) and the HH:MM parser.
# Run before a release; no ESP toolchain needed.
set -e
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# ---- geo_math suite: links the real source file ----
cat > "$tmp/geo_tests.c" <<'EOF'
#include <math.h>
#include <stdio.h>
#include "geo_math.h"

static int fails;

static void ok(int cond, const char *name)
{
    if (!cond) {
        printf("FAIL %s\n", name);
        fails++;
    }
}

int main(void)
{
    /* longitude unwrap around the antimeridian */
    ok(fabs(geo_lon_unwrap(170.0, -170.0) - 190.0) < 1e-9, "unwrap +");
    ok(fabs(geo_lon_unwrap(-170.0, 170.0) - (-190.0)) < 1e-9, "unwrap -");
    ok(fabs(geo_lon_unwrap(10.0, 20.0) - 20.0) < 1e-9, "unwrap noop");
    ok(fabs(geo_lon_unwrap(-122.4, 114.2) - (-245.8)) < 0.01, "unwrap SFO-HKG");

    /* great-circle endpoints */
    double la, lo;
    geo_gc_point(50.0, 10.0, 60.0, 20.0, 0.0, &la, &lo);
    ok(fabs(la - 50.0) < 1e-6 && fabs(lo - 10.0) < 1e-6, "gc t=0");
    geo_gc_point(50.0, 10.0, 60.0, 20.0, 1.0, &la, &lo);
    ok(fabs(la - 60.0) < 1e-6 && fabs(lo - 20.0) < 1e-6, "gc t=1");

    /* route corridor: WAW-GDN flight near Lodz is plausible,
       the same callsign near Lisbon is not */
    ok(geo_route_plausible(52.17, 20.97, 54.38, 18.47, 52.2, 19.5), "corridor in");
    ok(!geo_route_plausible(52.17, 20.97, 54.38, 18.47, 38.7, -9.1), "corridor out");

    /* haversine sanity: Warsaw-Gdansk ~283 km */
    double d = geo_haversine_km(52.23, 21.01, 54.35, 18.65);
    ok(d > 270 && d < 300, "haversine WAW-GDN");

    /* sun times, Gdansk in late July (UTC+2): rise ~4:30-5:15, set ~20:45-21:30 */
    int rise = 0, set = 0;
    ok(geo_sun_times(54.35, 18.65, 1785300000LL, 7200, &rise, &set), "sun returns");
    ok(rise > 4 * 60 && rise < 5 * 60 + 20, "sunrise plausible");
    ok(set > 20 * 60 + 30 && set < 21 * 60 + 40, "sunset plausible");

    /* polar night: Longyearbyen in January has no sunrise */
    ok(!geo_sun_times(78.2, 15.6, 1767225600LL, 3600, &rise, &set), "polar night");

    if (fails == 0) {
        printf("geo_math: all ok\n");
    }
    return fails;
}
EOF
cc -I main -o "$tmp/geo_tests" "$tmp/geo_tests.c" main/geo_math.c -lm
"$tmp/geo_tests"

# ---- HH:MM parser (extracted from ui_settings.c) ----
cat > "$tmp/hhmm_tests.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int parse_hhmm(const char *txt, int fallback);
int main(void)
{
    struct { const char *in; int want; } t[] = {
        {"21:00",1260},{"21.00",1260},{"2100",1260},{"830",510},{"06:30",390},
        {"0630",390},{"21",1260},{"",-1},{"25:00",-1},{"12:75",-1},
        {"abc",-1},{"12345",-1},{" 930 ",570},{"0",0},{"23:59",1439},{"2359",1439}
    };
    int fails = 0;
    for (unsigned i = 0; i < sizeof(t)/sizeof(t[0]); i++) {
        int got = parse_hhmm(t[i].in, -1);
        if (got != t[i].want) {
            printf("FAIL hhmm '%s': got %d want %d\n", t[i].in, got, t[i].want);
            fails++;
        }
    }
    if (fails == 0) {
        printf("parse_hhmm: all ok\n");
    }
    return fails;
}
EOF
sed -n '/^static int parse_hhmm/,/^}/p' main/ui_settings.c >> "$tmp/hhmm_tests.c"
cc -o "$tmp/hhmm_tests" "$tmp/hhmm_tests.c"
"$tmp/hhmm_tests"

echo "host tests passed"
