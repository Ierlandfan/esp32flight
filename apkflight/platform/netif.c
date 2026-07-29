/* Local IP for the settings screen: connect a UDP socket to a public
 * address (no packet is sent) and read back the chosen source address.
 * Works on every Android version and on desktop, no permissions needed
 * beyond INTERNET. */

#include "esp_netif.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{
    (void)netif;
    if (info == NULL) {
        return ESP_FAIL;
    }
    memset(info, 0, sizeof(*info));

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        return ESP_FAIL;
    }
    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
    };
    inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr);

    struct sockaddr_in src;
    socklen_t len = sizeof(src);
    esp_err_t err = ESP_FAIL;
    if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) == 0 &&
        getsockname(s, (struct sockaddr *)&src, &len) == 0 &&
        src.sin_addr.s_addr != 0) {
        info->ip.addr = src.sin_addr.s_addr;   /* both network byte order */
        err = ESP_OK;
    }
    close(s);
    return err;
}

int wifi_mgr_last_reason(void)
{
    return 0;   /* the OS owns Wi-Fi; there is no ESP reason code */
}
