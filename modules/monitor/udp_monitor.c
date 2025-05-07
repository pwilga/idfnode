#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "platform_services.h"
#include "udp_monitor.h"

#define TAG "udp-monitor"

static int udp_sock = -1;
static struct sockaddr_in client_addr;
static bool client_connected = false;

int udp_monitor_vprintf(const char *fmt, va_list args) {
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);

    sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr,
           sizeof(client_addr));

    return vprintf(fmt, args); // optional: also keep UART output
}

void udp_monitor_task(void *arg) {

    const uint8_t timeout_sec = 30;

    struct sockaddr_in recv_addr;
    int64_t last_seen_us = 0;
    char dummy_buf[8];

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_UDP_MONITOR_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    if (bind(udp_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed");
        close(udp_sock);
        udp_sock = -1;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Waiting for UDP client on port: %d", CONFIG_UDP_MONITOR_PORT);

    socklen_t addr_len = sizeof(recv_addr);
    while (1) {
        // Non-blocking recvfrom
        int len = recvfrom(udp_sock, dummy_buf, sizeof(dummy_buf), MSG_DONTWAIT,
                           (struct sockaddr *)&recv_addr, &addr_len);

        if (len > 0) {
            last_seen_us = esp_timer_get_time();

            // Only need to update the timer if it was connect before
            if (client_connected)
                continue;

            /*
             * Add an additional log handler to forward logs over UDP,
             * while keeping the default UART logging enabled.
             * This allows logs to be visible both locally (UART) and remotely (UDP).
             */
            esp_log_set_vprintf(udp_monitor_vprintf);

            memcpy(&client_addr, &recv_addr, sizeof(client_addr));
            client_connected = true;

            ESP_LOGI(TAG, "Client connected: %s:%d", inet_ntoa(client_addr.sin_addr),
                     ntohs(client_addr.sin_port));
        }

        if (client_connected && (esp_timer_get_time() - last_seen_us > timeout_sec * 1000000LL)) {
            ESP_LOGW(TAG, "Client timed out after %d seconds", timeout_sec);
            client_connected = false;

            /*
             * UDP client disconnected — stop sending logs over UDP.
             * Logs will continue to be printed to the UART console.
             */
            esp_log_set_vprintf(&vprintf);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
