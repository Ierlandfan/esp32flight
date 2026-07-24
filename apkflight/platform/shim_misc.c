#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "esp_timer.h"
#include "esp_app_desc.h"

int64_t esp_timer_get_time(void)
{
    static int64_t t0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    if (t0 == 0) {
        t0 = now;
    }
    return now - t0;
}

const esp_app_desc_t *esp_app_get_description(void)
{
    #ifdef APK_VERSION
    static esp_app_desc_t d = { .version = APK_VERSION };
#else
    static esp_app_desc_t d = { .version = "0.0.0" };
#endif
    return &d;
}
