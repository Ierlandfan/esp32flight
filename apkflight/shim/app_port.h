#pragma once

/* Platform-side SDL port API (the core uses the device lvgl_port.h). */

#include <stdbool.h>
#include "lvgl.h"

int  app_port_init(const char *title);
void app_port_set_size(int w, int h);   /* desktop --size WxH test override */
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);
int  app_port_run(int shot_after_ms, const char *shot_path);
