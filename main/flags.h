#pragma once

#include "lvgl.h"

/* Small country-flag PNG from SPIFFS for a 2-letter ISO code, NULL if absent.
 * Flags are 20 px tall, width varies (usually 27-30 px). */
const lv_img_dsc_t *flags_get(const char *cc);

/* Real pixel width of a flag (parsed from the PNG header; dsc->header.w
 * is zero for raw-PNG descriptors). */
int flags_width(const lv_img_dsc_t *dsc);
