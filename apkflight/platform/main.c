#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "lvgl.h"
#include "app_port.h"

#ifdef __ANDROID__
#include <SDL.h>
#include <SDL_main.h>   /* remaps main -> SDL_main for the SDL activity */
void android_glue_init(void);
#define STEP(...) SDL_Log("apkflight: " __VA_ARGS__)
#else
#define STEP(...)
#endif

#include "airports.h"
#include "flight_task.h"
#include "logos.h"
#include "settings.h"
#include "tilemap.h"
#include "ui.h"
#include "ui_map.h"
#include "ui_settings.h"
#include "web_server.h"
#include "wifi_mgr.h"

/* Same boot sequence as esp32flight's app_main, on SDL. */

int main(int argc, char **argv)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    int shot_ms = 0;
    int view = -1;
    const char *shot_path = "apkflight-shot.bmp";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0) {
            shot_ms = 35000;   /* radar needs a moment to fetch aircraft */
            if (i + 1 < argc) {
                shot_path = argv[++i];
            }
        } else if (strcmp(argv[i], "--view") == 0 && i + 1 < argc) {
            view = atoi(argv[++i]);   /* 0 detail .. 4 retro, for screenshots */
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            int w = 0, h = 0;   /* e.g. --size 1280x800: tablet layout test */
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2) {
                app_port_set_size(w, h);
            }
        }
    }

#ifdef __ANDROID__
    STEP("extracting assets");
    android_glue_init();   /* extract APK assets, point fopen + TLS at them */
#endif

    STEP("settings_load");
    settings_load();
    STEP("app_port_init");
    if (app_port_init("esp32flight") != 0) {
        STEP("app_port_init FAILED");
        return 1;
    }

    STEP("logos_init");
    logos_init();
    STEP("airports_init");
    airports_init();
    STEP("tilemap_init");
    tilemap_init();
    STEP("ui_map decode");
    ui_map_get_image();
    ui_map_get_image_small();

    STEP("ui_init");
    if (lvgl_port_lock(-1)) {
        ui_init();
        if (view >= 0) {
            ui_set_view(view);
        }
        lvgl_port_unlock();
    }

    STEP("start tasks");
    wifi_mgr_start();
    web_server_start();
    flight_task_start();

    STEP("entering run loop");
    return app_port_run(shot_ms, shot_path);
}
