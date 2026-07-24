#pragma once
/* No mDNS on the app - the panel is reached by IP (shown in System tab). */
static inline int mdns_init(void) { return 0; }
static inline int mdns_hostname_set(const char *h) { (void)h; return 0; }
static inline int mdns_instance_name_set(const char *n) { (void)n; return 0; }
static inline int mdns_service_add(const char *a, const char *b, const char *c,
                                   int port, void *txt, int n)
{ (void)a; (void)b; (void)c; (void)port; (void)txt; (void)n; return 0; }
