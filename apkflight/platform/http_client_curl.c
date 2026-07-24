/* Keep-alive esp_http_client shim over libcurl (tile downloads). */

#include "esp_http_client.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

struct esp_http_client {
    CURL *curl;                       /* NULL after close: reconnect lazily */
    esp_http_client_config_t cfg;
    char url[256];
    long status;
};

static size_t write_cb(char *data, size_t size, size_t nmemb, void *user)
{
    struct esp_http_client *c = user;
    if (c->cfg.event_handler != NULL) {
        esp_http_client_event_t evt = {
            .event_id = HTTP_EVENT_ON_DATA,
            .user_data = c->cfg.user_data,
            .data = data,
            .data_len = (int)(size * nmemb),
        };
        c->cfg.event_handler(&evt);
    }
    return size * nmemb;
}

static void ensure_curl(struct esp_http_client *c)
{
    if (c->curl != NULL) {
        return;
    }
    c->curl = curl_easy_init();
    curl_easy_setopt(c->curl, CURLOPT_TIMEOUT_MS,
                     (long)(c->cfg.timeout_ms > 0 ? c->cfg.timeout_ms : 10000));
    curl_easy_setopt(c->curl, CURLOPT_FOLLOWLOCATION,
                     c->cfg.disable_auto_redirect ? 0L : 1L);
    curl_easy_setopt(c->curl, CURLOPT_USERAGENT,
                     c->cfg.user_agent ? c->cfg.user_agent : "apkflight/1.0");
    curl_easy_setopt(c->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c->curl, CURLOPT_WRITEDATA, c);
    extern const char *apk_http_cainfo(void);
    if (apk_http_cainfo() != 0) {
        curl_easy_setopt(c->curl, CURLOPT_CAINFO, apk_http_cainfo());
    }
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    struct esp_http_client *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->cfg = *cfg;
    if (cfg->url != NULL) {
        snprintf(c->url, sizeof(c->url), "%s", cfg->url);
    }
    return c;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *url)
{
    snprintf(c->url, sizeof(c->url), "%s", url);
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    ensure_curl(c);
    curl_easy_setopt(c->curl, CURLOPT_URL, c->url);
    CURLcode rc = curl_easy_perform(c->curl);
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &c->status);
    return rc == CURLE_OK ? ESP_OK : ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t c)
{
    return (int)c->status;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t c)
{
    if (c->curl != NULL) {
        curl_easy_cleanup(c->curl);
        c->curl = NULL;
    }
    return ESP_OK;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{
    esp_http_client_close(c);
    free(c);
    return ESP_OK;
}
