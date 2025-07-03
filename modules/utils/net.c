#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include "esp_wifi.h"
#include "platform_services.h"

bool is_internet_reachable(void) {

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    // Only check internet if we are in STA mode and connected
    // If mode is not STA or STA_CONNECTED_BIT is not set, skip check
    if (mode != WIFI_MODE_STA || !(xEventGroupGetBits(app_event_group) & WIFI_STA_CONNECTED_BIT)) {
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53); // DNS
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;

    int res = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
    return (res == 0);
}