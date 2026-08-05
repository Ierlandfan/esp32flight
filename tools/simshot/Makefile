# Headless UI screenshot harness (no SDL, no curl - pure CPU render).
# Reuses the apkflight shim + platform glue; see sim_main.c for usage.

ESPF := ../..
APK  := $(ESPF)/apkflight
LVGL := $(ESPF)/managed_components/lvgl__lvgl

CC      ?= gcc
CFLAGS  := -O2 -std=gnu11 -Wno-format \
           -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
           -DAPKFLIGHT_NO_WIFI -DAPKFLIGHT -DSIMSHOT \
           -include $(APK)/shim/path_remap.h \
           -I. -I$(APK) -I$(APK)/shim -I$(APK)/vendor -I$(ESPF)/main \
           -I$(LVGL) -I$(LVGL)/src
LDLIBS  := -lpthread -lm

LVGL_SRC := $(shell find $(LVGL)/src -name '*.c')

# Device-only modules replaced by platform/ or local equivalents, plus the
# network-facing ones simshot stubs out.
CORE_EXCLUDE := main.c lvgl_port.c waveshare_rgb_lcd_port.c tab5_lcd_port.c wifi_mgr.c \
                mqtt_pub.c settings.c http_util.c
CORE_SRC := $(filter-out $(addprefix $(ESPF)/main/, $(CORE_EXCLUDE)), \
                         $(wildcard $(ESPF)/main/*.c))

PLAT_EXCLUDE := main.c sdl_port.c http_curl.c http_client_curl.c android_glue.c
PLAT_SRC := $(filter-out $(addprefix $(APK)/platform/, $(PLAT_EXCLUDE)), \
                         $(wildcard $(APK)/platform/*.c)) $(APK)/vendor/cJSON.c

APKVER := $(shell sed -n 's/.*set(PROJECT_VER "\([0-9.]*\)").*/\1/p' $(ESPF)/CMakeLists.txt | head -1)
$(shell new='#define APK_VERSION "$(APKVER)"'; \
        [ -f apk_version.h ] && [ "$$(cat apk_version.h)" = "$$new" ] || echo "$$new" > apk_version.h)

SIM_SRC := sim_main.c headless_port.c http_stub.c sim_compat.c

simshot: $(LVGL_SRC) $(CORE_SRC) $(PLAT_SRC) $(SIM_SRC) lv_conf.h sim_tick.h
	$(CC) $(CFLAGS) $(LVGL_SRC) $(CORE_SRC) $(PLAT_SRC) $(SIM_SRC) -o $@ $(LDLIBS)

clean:
	rm -f simshot *.ppm *.png

.PHONY: clean
