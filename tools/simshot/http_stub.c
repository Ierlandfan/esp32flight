/* Offline HTTP for the headless sim: every request fails cleanly. The UI
 * renders its placeholder/offline states, which is exactly what layout
 * verification needs - deterministic content, no network. */

#include "esp_http_client.h"
#include "http_util.h"

esp_err_t http_get_to_buffer(const char *url, char *buf, size_t buf_size, size_t *out_len)
{
    (void)url; (void)buf; (void)buf_size;
    if (out_len != NULL) {
        *out_len = 0;
    }
    return ESP_FAIL;
}

esp_err_t http_get_to_buffer_hdr(const char *url, char *buf, size_t buf_size, size_t *out_len,
                                 const char *hdr_key, const char *hdr_val)
{
    (void)hdr_key; (void)hdr_val;
    return http_get_to_buffer(url, buf, buf_size, out_len);
}

esp_err_t http_post_to_buffer(const char *url, const char *body,
                              char *buf, size_t buf_size)
{
    (void)url; (void)body; (void)buf; (void)buf_size;
    return ESP_FAIL;
}

esp_err_t http_post_text(const char *url, const char *body,
                         const char *hdr_key, const char *hdr_val)
{
    (void)url; (void)body; (void)hdr_key; (void)hdr_val;
    return ESP_FAIL;
}

esp_err_t http_get_keepalive(int slot, const char *url,
                             char *buf, size_t buf_size, size_t *out_len)
{
    (void)slot;
    return http_get_to_buffer(url, buf, buf_size, out_len);
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    (void)cfg;
    return NULL;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *url)
{
    (void)c; (void)url;
    return ESP_FAIL;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    (void)c;
    return ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t c)
{
    (void)c;
    return 0;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t c)
{
    (void)c;
    return ESP_OK;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{
    (void)c;
    return ESP_OK;
}
