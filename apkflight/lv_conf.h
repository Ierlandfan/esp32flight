/* LVGL 8.3 config for the apkflight desktop/Android build.
 * Everything not set here falls back to lv_conf_internal.h defaults. */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <SDL.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (SDL_GetTicks())

#define LV_FONT_MONTSERRAT_8 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_44 1   /* big-screen detail view */
#define LV_USE_FONT_COMPRESSED 1

#define LV_USE_PNG 1
#define LV_USE_SJPG 1

#define LV_SHADOW_CACHE_SIZE 0
#define LV_IMG_CACHE_DEF_SIZE 8

#endif
