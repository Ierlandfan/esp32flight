cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.4.4")
# Board-variant suffix: the 7B and Tab5 builds are not interchangeable with
# the standard 800x480 binary, and the OTA handler refuses a mismatched
# image based on this suffix.
if(DEFINED SDKCONFIG AND SDKCONFIG MATCHES "tab5")
    set(PROJECT_VER "${PROJECT_VER}-tab5")
elseif((DEFINED SDKCONFIG_DEFAULTS AND SDKCONFIG_DEFAULTS MATCHES "7b") OR (DEFINED SDKCONFIG AND SDKCONFIG MATCHES "7b"))
    set(PROJECT_VER "${PROJECT_VER}-7b")
elseif(DEFINED SDKCONFIG_DEFAULTS AND SDKCONFIG_DEFAULTS MATCHES "crowpanel50")
    set(PROJECT_VER "${PROJECT_VER}-crowpanel")
elseif(DEFINED SDKCONFIG_DEFAULTS AND SDKCONFIG_DEFAULTS MATCHES "sunton43r")
    set(PROJECT_VER "${PROJECT_VER}-sunton43r")
elseif(DEFINED SDKCONFIG_DEFAULTS AND SDKCONFIG_DEFAULTS MATCHES "sunton43")
    set(PROJECT_VER "${PROJECT_VER}-sunton43")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
# LVGL object allocations go to PSRAM (internal RAM is reserved for task
# stacks and Wi-Fi buffers); falls back to internal when PSRAM is full.
add_compile_options("-DLV_MEM_CUSTOM_ALLOC(s)=heap_caps_malloc_prefer(s,2,(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),MALLOC_CAP_8BIT)")
add_compile_options("-DLV_MEM_CUSTOM_FREE(p)=heap_caps_free(p)")
add_compile_options("-DLV_MEM_CUSTOM_REALLOC(p,s)=heap_caps_realloc_prefer(p,s,2,(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),MALLOC_CAP_8BIT)")
project(esp32flight)
