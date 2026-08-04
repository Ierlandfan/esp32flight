/* http_util.h implemented over libcurl: same API as the ESP32 build. */

#include "http_util.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "http";

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    bool   overflow;
} sink_t;

static size_t write_cb(char *data, size_t size, size_t nmemb, void *user)
{
    sink_t *s = user;
    size_t n = size * nmemb;
    size_t space = s->cap - 1 - s->len;
    size_t take = n > space ? space : n;
    if (take < n) {
        s->overflow = true;
    }
    memcpy(s->buf + s->len, data, take);
    s->len += take;
    return n;   /* claim it all or curl aborts the transfer */
}

static void do_global_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

static void global_init_once(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, do_global_init);
}

static CURL *mk_handle(const char *url, sink_t *sink)
{
    global_init_once();
    CURL *c = curl_easy_init();
    if (c == NULL) {
        return NULL;
    }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 12000L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "apkflight/1.0");
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "identity");
    extern const char *apk_http_cainfo(void);
    if (apk_http_cainfo() != 0) {
        curl_easy_setopt(c, CURLOPT_CAINFO, apk_http_cainfo());
    }
    if (sink != NULL) {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, sink);
    }
    return c;
}

static esp_err_t finish(CURL *c, struct curl_slist *hdrs, sink_t *sink,
                        const char *url)
{
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    if (hdrs != NULL) {
        curl_slist_free_all(hdrs);
    }
    if (sink != NULL) {
        sink->buf[sink->len] = '\0';
    }
    if (rc != CURLE_OK) {
        ESP_LOGW(TAG, "%s failed: %s", url, curl_easy_strerror(rc));
        return ESP_FAIL;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "%s -> HTTP %ld", url, status);
        return ESP_ERR_HTTP_BASE + (int)status;
    }
    if (sink != NULL && sink->overflow) {
        ESP_LOGW(TAG, "%s: response truncated at %u bytes", url,
                 (unsigned)sink->len);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t http_get_to_buffer_t(const char *url, char *buf, size_t buf_size,
                               size_t *out_len, int timeout_ms)
{
    (void)timeout_ms;   /* desktop libcurl path keeps its own timeouts */
    return http_get_to_buffer_hdr(url, buf, buf_size, out_len, NULL, NULL);
}

esp_err_t http_get_to_buffer(const char *url, char *buf, size_t buf_size,
                             size_t *out_len)
{
    return http_get_to_buffer_hdr(url, buf, buf_size, out_len, NULL, NULL);
}

esp_err_t http_get_to_buffer_hdr(const char *url, char *buf, size_t buf_size,
                                 size_t *out_len,
                                 const char *hdr_key, const char *hdr_val)
{
    sink_t sink = { .buf = buf, .cap = buf_size };
    CURL *c = mk_handle(url, &sink);
    if (c == NULL) {
        return ESP_FAIL;
    }
    struct curl_slist *hdrs = NULL;
    if (hdr_key != NULL && hdr_val != NULL) {
        char line[256];
        snprintf(line, sizeof(line), "%s: %s", hdr_key, hdr_val);
        hdrs = curl_slist_append(NULL, line);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    }
    esp_err_t err = finish(c, hdrs, &sink, url);
    if (out_len != NULL) {
        *out_len = sink.len;
    }
    return err;
}

esp_err_t http_post_to_buffer(const char *url, const char *body,
                              char *buf, size_t buf_size)
{
    sink_t sink = { .buf = buf, .cap = buf_size };
    CURL *c = mk_handle(url, &sink);
    if (c == NULL) {
        return ESP_FAIL;
    }
    struct curl_slist *hdrs =
        curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, body);
    return finish(c, hdrs, &sink, url);
}

esp_err_t http_post_text(const char *url, const char *body,
                         const char *hdr_key, const char *hdr_val)
{
    CURL *c = mk_handle(url, NULL);
    if (c == NULL) {
        return ESP_FAIL;
    }
    struct curl_slist *hdrs = NULL;
    if (hdr_key != NULL && hdr_val != NULL) {
        char line[256];
        snprintf(line, sizeof(line), "%s: %s", hdr_key, hdr_val);
        hdrs = curl_slist_append(NULL, line);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    }
    curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, body);
    return finish(c, hdrs, NULL, url);
}

/* The device build keeps per-slot connections alive between cycles; here
 * curl's own connection cache would need a persistent handle. Plain GET is
 * fine on a phone-class CPU. */
esp_err_t http_get_keepalive(int slot, const char *url,
                             char *buf, size_t buf_size, size_t *out_len)
{
    (void)slot;
    return http_get_to_buffer(url, buf, buf_size, out_len);
}
