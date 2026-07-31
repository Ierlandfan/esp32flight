/* Desktop/Android stubs for device-only services: Wi-Fi belongs to the OS,
 * the web panel and MQTT publishing are v2 material. */

#include <string.h>

#include "mqtt_pub.h"
#include "wifi_mgr.h"

esp_err_t wifi_mgr_start(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_scan(wifi_ap_record_t *records, uint16_t *count)
{
    (void)records;
    if (count != NULL) {
        *count = 0;
    }
    return ESP_OK;
}

bool wifi_mgr_wait_connected(int timeout_ms)
{
    (void)timeout_ms;
    return true;   /* the OS manages connectivity */
}

bool wifi_mgr_is_connected(void)
{
    return true;
}

void mqtt_pub_start(void)
{
}

void mqtt_pub_state(const char *json)
{
    (void)json;
}

/* waveshare_rgb_lcd_port.h API: the SDL window has no backlight */
esp_err_t waveshare_esp32_s3_rgb_lcd_init(void) { return ESP_OK; }
esp_err_t waveshare_rgb_lcd_bl_on(void) { return ESP_OK; }
esp_err_t waveshare_rgb_lcd_bl_off(void) { return ESP_OK; }

/* board name for /api/state; the app is its own hardware */
const char *waveshare_lcd_board_name(void)
{
#ifdef __ANDROID__
    return "Android app";
#else
    return "Desktop test build";
#endif
}
