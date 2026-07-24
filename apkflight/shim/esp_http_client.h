#pragma once

/* esp_http_client subset used by tilemap.c, backed by libcurl. */

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define HTTP_EVENT_ON_DATA 1

typedef struct {
    int   event_id;
    void *user_data;
    char *data;
    int   data_len;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);

typedef struct {
    const char *url;
    http_event_handle_cb event_handler;
    void *user_data;
    int timeout_ms;
    void *crt_bundle_attach;
    bool keep_alive_enable;
    bool disable_auto_redirect;
    const char *user_agent;
    int method;
} esp_http_client_config_t;

typedef struct esp_http_client *esp_http_client_handle_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *url);
esp_err_t esp_http_client_perform(esp_http_client_handle_t c);
int       esp_http_client_get_status_code(esp_http_client_handle_t c);
esp_err_t esp_http_client_close(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
