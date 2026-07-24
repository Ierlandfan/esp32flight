/* Small blocking HTTP server behind the esp_http_server API so the shared
 * web panel (web_server.c) runs unchanged in the app. One worker thread,
 * one connection at a time - plenty for a LAN panel. */

#include "esp_http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"

static const char *TAG = "httpd";

#define MAX_ROUTES 20
#define HDR_MAX    8192

typedef struct {
    httpd_uri_t routes[MAX_ROUTES];
    int n_routes;
    int port;
    int listen_fd;
} server_t;

static server_t s_srv;

/* ---- response side ---- */

static void send_all(int sock, const void *buf, size_t len)
{
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(sock, p, len, 0);
        if (n <= 0) {
            return;
        }
        p += n;
        len -= (size_t)n;
    }
}

static void send_headers(httpd_req_t *req, ssize_t content_len)
{
    if (req->headers_sent) {
        return;
    }
    req->headers_sent = 1;
    char h[1024];
    int o = snprintf(h, sizeof(h),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nConnection: close\r\n",
                     req->status ? req->status : "200 OK",
                     req->ctype ? req->ctype : "text/html");
    for (int i = 0; i < req->n_extra; i++) {
        o += snprintf(h + o, sizeof(h) - o, "%s\r\n", req->extra_hdrs[i]);
    }
    if (content_len >= 0) {
        o += snprintf(h + o, sizeof(h) - o, "Content-Length: %zd\r\n", content_len);
    } else {
        req->chunked = 1;
        o += snprintf(h + o, sizeof(h) - o, "Transfer-Encoding: chunked\r\n");
    }
    o += snprintf(h + o, sizeof(h) - o, "\r\n");
    send_all(req->sock, h, o);
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    req->ctype = type;
    return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status)
{
    req->status = status;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *val)
{
    if (req->n_extra < 2) {
        snprintf(req->extra_hdrs[req->n_extra], sizeof(req->extra_hdrs[0]),
                 "%s: %s", key, val);
        req->n_extra++;
    }
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t len)
{
    if (len == HTTPD_RESP_USE_STRLEN) {
        len = buf != NULL ? (ssize_t)strlen(buf) : 0;
    }
    send_headers(req, len);
    if (buf != NULL && len > 0) {
        send_all(req->sock, buf, (size_t)len);
    }
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str)
{
    return httpd_resp_send(req, str, HTTPD_RESP_USE_STRLEN);
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buf, ssize_t len)
{
    if (len == HTTPD_RESP_USE_STRLEN) {
        len = buf != NULL ? (ssize_t)strlen(buf) : 0;
    }
    send_headers(req, -1);
    char pre[16];
    if (buf != NULL && len > 0) {
        int po = snprintf(pre, sizeof(pre), "%zx\r\n", (size_t)len);
        send_all(req->sock, pre, po);
        send_all(req->sock, buf, (size_t)len);
        send_all(req->sock, "\r\n", 2);
    } else {
        send_all(req->sock, "0\r\n\r\n", 5);
    }
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t *req, int code, const char *msg)
{
    switch (code) {
    case 400: req->status = "400 Bad Request"; break;
    case 403: req->status = "403 Forbidden"; break;
    case 404: req->status = "404 Not Found"; break;
    default:  req->status = "500 Internal Server Error"; break;
    }
    req->ctype = "text/plain";
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

/* ---- request side ---- */

int httpd_req_recv(httpd_req_t *req, char *buf, size_t len)
{
    if (len > req->body_left) {
        len = req->body_left;
    }
    if (len == 0) {
        return 0;
    }
    /* first drain the bytes that arrived together with the headers */
    size_t pre = req->body_pre_len - req->body_pre_off;
    if (pre > 0) {
        if (pre > len) {
            pre = len;
        }
        memcpy(buf, req->body_pre + req->body_pre_off, pre);
        req->body_pre_off += pre;
        req->body_left -= pre;
        return (int)pre;
    }
    ssize_t n = recv(req->sock, buf, len, 0);
    if (n > 0) {
        req->body_left -= (size_t)n;
    }
    return (int)n;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *key,
                                      char *val, size_t val_size)
{
    size_t klen = strlen(key);
    for (char *p = req->hdrs; p != NULL && *p; ) {
        char *eol = strstr(p, "\r\n");
        size_t linelen = eol != NULL ? (size_t)(eol - p) : strlen(p);
        if (linelen > klen + 1 && strncasecmp(p, key, klen) == 0 &&
            p[klen] == ':') {
            const char *v = p + klen + 1;
            while (*v == ' ') {
                v++;
            }
            size_t vlen = linelen - (size_t)(v - p);
            if (vlen >= val_size) {
                return ESP_FAIL;
            }
            memcpy(val, v, vlen);
            val[vlen] = '\0';
            return ESP_OK;
        }
        p = eol != NULL ? eol + 2 : NULL;
    }
    return ESP_FAIL;
}

/* ---- connection handling ---- */

static void handle_conn(int sock)
{
    static char hdrs[HDR_MAX];
    size_t got = 0;
    char *body = NULL;
    while (got < sizeof(hdrs) - 1) {
        ssize_t n = recv(sock, hdrs + got, sizeof(hdrs) - 1 - got, 0);
        if (n <= 0) {
            return;
        }
        got += (size_t)n;
        hdrs[got] = '\0';
        body = strstr(hdrs, "\r\n\r\n");
        if (body != NULL) {
            break;
        }
    }
    if (body == NULL) {
        return;
    }
    *body = '\0';
    body += 4;

    char method[8] = "", uri[256] = "";
    if (sscanf(hdrs, "%7s %255s", method, uri) != 2) {
        return;
    }
    char *q = strchr(uri, '?');
    if (q != NULL) {
        *q = '\0';
    }

    httpd_req_t req = { .sock = sock, .hdrs = hdrs };
    char clen[16];
    if (httpd_req_get_hdr_value_str(&req, "Content-Length", clen,
                                    sizeof(clen)) == ESP_OK) {
        req.content_len = (size_t)strtoul(clen, NULL, 10);
    }
    req.body_pre = body;
    req.body_pre_len = got - (size_t)(body - hdrs);
    if (req.body_pre_len > req.content_len) {
        req.body_pre_len = req.content_len;
    }
    req.body_left = req.content_len;

    httpd_method_t m = strcmp(method, "POST") == 0 ? HTTP_POST : HTTP_GET;
    for (int i = 0; i < s_srv.n_routes; i++) {
        if (s_srv.routes[i].method == m &&
            strcmp(s_srv.routes[i].uri, uri) == 0) {
            s_srv.routes[i].handler(&req);
            return;
        }
    }
    httpd_resp_send_err(&req, HTTPD_404_NOT_FOUND, "not found");
}

static void *server_thread(void *arg)
{
    (void)arg;
    while (1) {
        int c = accept(s_srv.listen_fd, NULL, NULL);
        if (c < 0) {
            continue;
        }
        struct timeval tv = { .tv_sec = 10 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        handle_conn(c);
        close(c);
    }
    return NULL;
}

esp_err_t httpd_start(httpd_handle_t *out, const httpd_config_t *cfg)
{
    s_srv.port = cfg->server_port > 0 ? cfg->server_port : HTTPD_APP_PORT;

    s_srv.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_srv.listen_fd < 0) {
        return ESP_FAIL;
    }
    int one = 1;
    setsockopt(s_srv.listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons((unsigned short)s_srv.port),
    };
    if (bind(s_srv.listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(s_srv.listen_fd, 4) != 0) {
        ESP_LOGE(TAG, "bind/listen on :%d failed", s_srv.port);
        close(s_srv.listen_fd);
        return ESP_FAIL;
    }

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, server_thread, NULL);
    pthread_attr_destroy(&attr);
    ESP_LOGI(TAG, "listening on :%d", s_srv.port);
    if (out != NULL) {
        *out = (httpd_handle_t)&s_srv;
    }
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *uri)
{
    (void)h;
    if (s_srv.n_routes >= MAX_ROUTES) {
        return ESP_FAIL;
    }
    s_srv.routes[s_srv.n_routes++] = *uri;
    return ESP_OK;
}
