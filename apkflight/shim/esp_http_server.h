#pragma once

/* esp_http_server subset used by web_server.c, implemented over POSIX
 * sockets in platform/httpd_posix.c. Unprivileged port: the panel lives
 * at http://<ip>:8080 on the app. */

#include <stddef.h>
#include <sys/types.h>
#include "esp_err.h"

#define HTTPD_APP_PORT 8080

typedef enum { HTTP_GET = 1, HTTP_POST = 3 } httpd_method_t;

typedef struct httpd_req httpd_req_t;
typedef void *httpd_handle_t;

typedef struct {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *req);
} httpd_uri_t;

typedef struct {
    int server_port;
    int stack_size;
    int lru_purge_enable;
    int max_uri_handlers;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() (httpd_config_t){ .server_port = HTTPD_APP_PORT }

#define HTTPD_RESP_USE_STRLEN (-1)

#define HTTPD_400_BAD_REQUEST           400
#define HTTPD_403_FORBIDDEN             403
#define HTTPD_404_NOT_FOUND             404
#define HTTPD_500_INTERNAL_SERVER_ERROR 500

struct httpd_req {
    size_t content_len;
    /* private */
    int   sock;
    char *hdrs;            /* raw header block */
    char *body_pre;        /* body bytes read together with the headers */
    size_t body_pre_len, body_pre_off, body_left;
    const char *status;    /* "200 OK" style */
    const char *ctype;
    char  extra_hdrs[2][160];
    int   n_extra;
    int   headers_sent, chunked;
};

esp_err_t httpd_start(httpd_handle_t *out, const httpd_config_t *cfg);
esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *uri);

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *val);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t len);
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str);
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buf, ssize_t len);
esp_err_t httpd_resp_send_err(httpd_req_t *req, int code, const char *msg);
int       httpd_req_recv(httpd_req_t *req, char *buf, size_t len);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *key,
                                      char *val, size_t val_size);
