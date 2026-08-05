#pragma once

#include <stdint.h>

/* Virtual millisecond clock driven by the sim main loop; LVGL reads it
 * through LV_TICK_CUSTOM_SYS_TIME_EXPR, so renders are deterministic. */
uint32_t sim_tick_ms(void);
void     sim_tick_advance(uint32_t ms);
