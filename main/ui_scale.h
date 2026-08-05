#pragma once

/* Downscale-only virtual coordinate system for sub-800x480 panels
 * (Sunton ESP32-4827S043, 480x272).
 *
 * The UI keeps composing in its original 800x480 design space; on a
 * smaller panel every geometry call maps design -> physical through
 * UISX/UISY at the call site. On >=800x480 displays all macros are
 * identity, so the supported boards render pixel-identical to before.
 *
 * The 4827S043 glass is the same 4.3" as the supported Waveshare 4.3;
 * a proportional 0.6 x 0.567 downscale therefore reproduces that
 * board's physical layout exactly, only at the panel's lower DPI.
 *
 * Rules for UI code:
 *  - sizes/positions/align offsets/pads: pass design-space values,
 *    wrapped in UISX()/UISY() (x-ish vs y-ish respectively)
 *  - LV_PCT() and LV_SIZE_CONTENT are never wrapped
 *  - fonts: UIFONT(big, small) picks the downscaled tier at runtime on
 *    builds that carry the small fonts, and compiles to `big` elsewhere
 *    so the small tiers aren't linked into the 800x480 firmware
 */

#include "lvgl.h"
#include "sdkconfig.h"

#define UI_DESIGN_W 800
#define UI_DESIGN_H 480

#define UI_DOWNSCALE (LV_HOR_RES < UI_DESIGN_W || LV_VER_RES < UI_DESIGN_H)

#define UISX(v) ((lv_coord_t)(LV_HOR_RES < UI_DESIGN_W \
        ? (int32_t)(v) * LV_HOR_RES / UI_DESIGN_W : (int32_t)(v)))
#define UISY(v) ((lv_coord_t)(LV_VER_RES < UI_DESIGN_H \
        ? (int32_t)(v) * LV_VER_RES / UI_DESIGN_H : (int32_t)(v)))

/* lv_img zoom values (256 = 1.0). Default-pivot zooms render around the
 * widget center and keep the nominal box, so centering offsets that pair
 * with a UIZOOM stay in raw bitmap pixels - never wrap those in UISX/Y. */
#define UIZOOM(z) ((uint16_t)(LV_HOR_RES < UI_DESIGN_W \
        ? (uint32_t)(z) * LV_HOR_RES / UI_DESIGN_W : (uint32_t)(z)))

#if !defined(ESP_PLATFORM) || CONFIG_CANFLIGHT_BOARD_SUNTON_4827S043 || CONFIG_CANFLIGHT_BOARD_SUNTON_4827S043R
#define UIFONT(big, small) (UI_DOWNSCALE ? (small) : (big))
#else
#define UIFONT(big, small) (big)
#endif
