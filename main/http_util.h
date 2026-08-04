#pragma once

#include <stddef.h>
#include "esp_err.h"

/* GET url into caller-provided buffer; NUL-terminates. Follows redirects,
 * uses the mbedTLS cert bundle for https URLs. */
esp_err_t http_get_to_buffer(const char *url, char *buf, size_t buf_size, size_t *out_len);

/* Same, with a caller-chosen timeout: enrichment lookups (routes, airline
 * names, timezones) should give up quickly instead of stalling the cycle. */
esp_err_t http_get_to_buffer_t(const char *url, char *buf, size_t buf_size, size_t *out_len,
                               int timeout_ms);

/* Same, with one extra request header (e.g. an API key). */
esp_err_t http_get_to_buffer_hdr(const char *url, char *buf, size_t buf_size, size_t *out_len,
                                 const char *hdr_key, const char *hdr_val);

/* POST a JSON body and read the response into buf (NUL-terminated). */
esp_err_t http_post_to_buffer(const char *url, const char *body,
                              char *buf, size_t buf_size);

/* POST a small text body (used for ntfy notifications). */
esp_err_t http_post_text(const char *url, const char *body,
                         const char *hdr_key, const char *hdr_val);

/* Persistent keep-alive GET: one cached connection per slot, reused across
 * calls to the same host (the 8 s flight cycle). Single-task use per slot.
 * On any error the cached client is dropped and rebuilt next call. */
#define HTTP_KEEPALIVE_SLOTS 3
esp_err_t http_get_keepalive(int slot, const char *url,
                             char *buf, size_t buf_size, size_t *out_len);
