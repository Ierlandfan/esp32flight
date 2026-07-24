#pragma once

#include <stdlib.h>

#ifdef __ANDROID__
/* Relaunch the activity (settings save-and-restart). */
void android_restart(void);
#define esp_restart() android_restart()
#else
#define esp_restart() exit(0)   /* desktop: save-and-quit */
#endif

#define esp_get_free_heap_size() (64u * 1024 * 1024)
