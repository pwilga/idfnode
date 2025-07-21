#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "esp_wifi.h"
#include "platform_services.h"
#include "config_manager.h"
#include "net.h"

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

bool is_tcp_port_reachable(const char *host, uint16_t port) {

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    if (!(mode & WIFI_MODE_STA) ||
        !(xEventGroupGetBits(app_event_group) & WIFI_STA_CONNECTED_BIT)) {
        return false;
    }

    struct sockaddr_in addr;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_aton(host, &addr.sin_addr) == 0) {
        struct hostent *he = gethostbyname(host);
        if (!he) {
            close(sock);
            return false;
        }
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int res = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (res == 0) {
        close(sock);
        return true;
    } else if (errno != EINPROGRESS) {
        close(sock);
        return false;
    }

    struct timeval tv = {.tv_sec = 0, .tv_usec = 500 * 1000};
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    res = select(sock + 1, NULL, &wfds, NULL, &tv);
    if (res > 0) {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        close(sock);
        return so_error == 0;
    } else {
        close(sock);
        return false;
    }
}

// Helper: parse URI like mqtts://host:port
static bool parse_mqtt_broker_uri(const char *uri, char *host, size_t host_len, uint16_t *port) {
    // Find "://"
    const char *p = strstr(uri, "://");
    if (!p)
        return false;
    p += 3; // skip scheme
    const char *colon = strrchr(p, ':');
    const char *slash = strchr(p, '/');
    if (!colon || (slash && colon > slash))
        return false;
    size_t len = colon - p;
    if (len >= host_len)
        return false;
    strncpy(host, p, len);
    host[len] = '\0';
    *port = (uint16_t)atoi(colon + 1);
    return true;
}

bool is_mqtt_broker_reachable(void) {
    char host[128];
    uint16_t port;
    const char *uri = config_get()->mqtt_broker;
    if (!parse_mqtt_broker_uri(uri, host, sizeof(host), &port)) {
        return false;
    }
    return is_tcp_port_reachable(host, port);
}
