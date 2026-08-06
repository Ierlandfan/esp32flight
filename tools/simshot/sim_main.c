/* Headless screenshot harness: boots the real UI with deterministic fake
 * traffic and dumps PPM frames of each view at an arbitrary resolution.
 *
 *   simshot [--size WxH] [--views 0,1,2,3,4] [--out prefix]
 *
 * Renders <prefix>_view<N>.ppm per requested view. Used to verify the
 * 480x272 downscale and to prove 800x480 output stays untouched. */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "lvgl.h"
#include "app_port.h"
#include "sim_tick.h"

#include "airports.h"
#include "flight_model.h"
#include "logos.h"
#include "settings.h"
#include "tilemap.h"
#include "trails.h"
#include "ui.h"
#include "ui_map.h"
#include "ui_settings.h"

int sim_shot_ppm(const char *path);
void apk_paths_set_base(const char *dir);

static void pump(int ms)
{
    for (int t = 0; t < ms; t += 5) {
        sim_tick_advance(5);
        lv_timer_handler();
    }
}

static void add_ac(aircraft_list_t *l, const char *cs, const char *hex,
                   const char *type, const char *desc, const char *cat,
                   double dlat, double dlon, int alt, float gs, float trk,
                   float dist, float dir, int rate, bool mil)
{
    aircraft_t *a = &l->ac[l->count++];
    memset(a, 0, sizeof(*a));
    snprintf(a->callsign, sizeof(a->callsign), "%s", cs);
    snprintf(a->hex, sizeof(a->hex), "%s", hex);
    snprintf(a->type_icao, sizeof(a->type_icao), "%s", type);
    snprintf(a->type_desc, sizeof(a->type_desc), "%s", desc);
    snprintf(a->category, sizeof(a->category), "%s", cat);
    a->lat = 52.2297 + dlat;
    a->lon = 21.0122 + dlon;
    a->has_pos = true;
    a->alt_baro_ft = alt;
    a->gs_kts = gs;
    a->track_deg = trk;
    a->dist_nm = dist;
    a->dir_deg = dir;
    a->baro_rate_fpm = rate;
    a->military = mil;
}

static void fake_traffic(aircraft_list_t *l)
{
    memset(l, 0, sizeof(*l));
    add_ac(l, "RYR638T", "4d2201", "B738", "BOEING 737-800", "A3",
           0.10, 0.25, 36000, 447.f, 275.f, 8.2f, 63.f, 0, false);
    add_ac(l, "LOT3PR", "489789", "E195", "EMBRAER ERJ-195", "A3",
           -0.20, 0.05, 12500, 305.f, 92.f, 12.9f, 187.f, -1200, false);
    add_ac(l, "WZZ108", "471f2a", "A21N", "AIRBUS A-321neo", "A3",
           0.35, -0.42, 38000, 460.f, 310.f, 25.4f, 291.f, 0, false);
    add_ac(l, "DLH4CK", "3c6675", "A359", "AIRBUS A-350-900", "A5",
           0.55, 0.62, 41000, 488.f, 268.f, 41.0f, 45.f, 0, false);
    add_ac(l, "SP-KYS", "48a1b3", "C172", "CESSNA 172", "A1",
           -0.04, -0.09, 2400, 98.f, 140.f, 4.1f, 233.f, 300, false);
    add_ac(l, "HEMS32", "48c142", "EC35", "AIRBUS EC-135", "A7",
           0.02, 0.03, 1100, 120.f, 20.f, 1.9f, 51.f, 0, false);
    add_ac(l, "PLF101", "48d801", "C17", "BOEING C-17A", "A5",
           -0.48, 0.30, 28000, 410.f, 355.f, 33.7f, 156.f, 0, true);
    add_ac(l, "SP-8422", "48b077", "GLID", "SZD-48 Jantar", "B1",
           0.08, -0.15, 4600, 55.f, 200.f, 7.3f, 302.f, -100, false);
    add_ac(l, "EJU78MZ", "440172", "A320", "AIRBUS A-320", "A3",
           -0.31, -0.28, 34000, 431.f, 210.f, 27.6f, 221.f, 0, false);
    add_ac(l, "QTR27P", "06a2e4", "B77W", "BOEING 777-300ER", "A5",
           0.72, -0.10, 40000, 495.f, 285.f, 49.8f, 350.f, 0, false);
    l->fetched_at_ms = 1000;
}

int main(int argc, char **argv)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);

    int w = 800, h = 480;
    const char *prefix = "shot";
    const char *views = "0";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &w, &h);
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            prefix = argv[++i];
        } else if (strcmp(argv[i], "--views") == 0 && i + 1 < argc) {
            views = argv[++i];
        }
    }

    apk_paths_set_base("../../apkflight/assets");
    settings_load();
    app_port_set_size(w, h);
    if (app_port_init("simshot") != 0) {
        return 1;
    }

    logos_init();
    airports_init();
    tilemap_init();
    ui_map_get_image();
    ui_map_get_image_small();

    aircraft_list_t list;
    fake_traffic(&list);

    /* a few sim poll cycles so breadcrumb trails have history */
    for (int c = 0; c < 10; c++) {
        trails_update(&list);
        for (int i = 0; i < list.count; i++) {
            double rad = list.ac[i].track_deg * 3.14159265 / 180.0;
            list.ac[i].lat += 0.045 * cos(rad);
            list.ac[i].lon += 0.045 * sin(rad) / 0.62;
        }
    }

    lvgl_port_lock(-1);
    ui_init();
    ui_set_home(52.2297, 21.0122);
    ui_set_status("10 aircraft - simshot");
    ui_update(&list);
    lvgl_port_unlock();

    pump(3000);

    char path[256];
    for (const char *v = views; *v != '\0'; v++) {
        if (*v == 's') {   /* settings screen, tabs s0..s3 via next digit */
            int tab = (v[1] >= '0' && v[1] <= '9') ? *++v - '0' : 0;
            lvgl_port_lock(-1);
            ui_settings_open();
            ui_settings_show_tab(tab);
            lvgl_port_unlock();
            pump(2000);
            snprintf(path, sizeof(path), "%s_settings%d.ppm", prefix, tab);
            if (sim_shot_ppm(path) != 0) {
                return 1;
            }
            printf("wrote %s\n", path);
            continue;
        }
        if (*v < '0' || *v > '9') {
            continue;
        }
        lvgl_port_lock(-1);
        ui_set_view(*v - '0');
        lvgl_port_unlock();
        pump(2000);
        snprintf(path, sizeof(path), "%s_view%c.ppm", prefix, *v);
        if (sim_shot_ppm(path) != 0) {
            fprintf(stderr, "shot failed: %s\n", path);
            return 1;
        }
        printf("wrote %s\n", path);
    }
    return 0;
}
