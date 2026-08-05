/* Headless app_port: renders the real UI into a memory framebuffer at any
 * resolution - no SDL, no window. Used by sim_main.c to screenshot the
 * layout at 480x272 (Sunton 4827S043) and 800x480 (regression baseline). */

#include "app_port.h"
#include "sim_tick.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t s_lvgl_mux;

static int s_lw = 800;
static int s_lh = 480;

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1;
static lv_color_t *s_fb;

static uint32_t s_tick_ms;

uint32_t sim_tick_ms(void)
{
    return s_tick_ms;
}

void sim_tick_advance(uint32_t ms)
{
    s_tick_ms += ms;
}

/* Fixed wall clock (2026-08-04 12:34:56 UTC) so the header clock and any
 * date text render identically run-to-run; advances with the virtual tick. */
time_t time(time_t *out)
{
    time_t t = 1785587696 + s_tick_ms / 1000;
    if (out != NULL) {
        *out = t;
    }
    return t;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    for (int y = area->y1; y <= area->y2; y++) {
        lv_color_t *dst = &s_fb[y * s_lw + area->x1];
        int w = area->x2 - area->x1 + 1;
        memcpy(dst, px, w * sizeof(lv_color_t));
        px += w;
    }
    lv_disp_flush_ready(drv);
}

void app_port_set_size(int w, int h)
{
    /* unlike the SDL port, any size is allowed: small panels are the point */
    if (w > 0 && h > 0) {
        s_lw = w;
        s_lh = h;
    }
}

int app_port_init(const char *title)
{
    (void)title;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_lvgl_mux, &a);

    s_buf1 = malloc((size_t)s_lw * s_lh * sizeof(lv_color_t));
    s_fb = calloc((size_t)s_lw * s_lh, sizeof(lv_color_t));
    if (s_buf1 == NULL || s_fb == NULL) {
        fprintf(stderr, "framebuffer alloc failed\n");
        return -1;
    }

    lv_init();
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, s_lw * s_lh);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = s_lw;
    disp_drv.ver_res = s_lh;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);
    return 0;
}

bool lvgl_port_lock(int timeout_ms)
{
    (void)timeout_ms;
    return pthread_mutex_lock(&s_lvgl_mux) == 0;
}

void lvgl_port_unlock(void)
{
    pthread_mutex_unlock(&s_lvgl_mux);
}

int app_port_run(int shot_after_ms, const char *shot_path)
{
    (void)shot_after_ms;
    (void)shot_path;
    return 0;   /* sim_main drives its own pump/shot loop */
}

int sim_shot_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", s_lw, s_lh);
    for (int i = 0; i < s_lw * s_lh; i++) {
        uint16_t c = *(uint16_t *)&s_fb[i];
        uint8_t rgb[3] = {
            (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
            (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
            (uint8_t)((c & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/* Live RGB565 framebuffer (web panel API surface, unused headlessly) */
void *waveshare_lcd_get_fb(void)
{
    return s_fb;
}

void waveshare_lcd_get_res(int *w, int *h)
{
    *w = s_lw;
    *h = s_lh;
}

const char *apk_clipboard_text(void)
{
    return "";
}
