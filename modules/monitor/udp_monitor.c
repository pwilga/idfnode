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

#include "config_manager.h"
#include "platform_services.h"
#include "udp_monitor.h"

#define TAG "udp-monitor"

static TaskHandle_t monitor_task_handle = NULL;
static int udp_sock = -1;
static struct sockaddr_in client_addr;
static bool client_connected = false;
static volatile bool monitor_shutdown_requested = false;

int udp_monitor_vprintf(const char *fmt, va_list args) {
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    // Only try to send if socket is valid and client is connected
    if (udp_sock >= 0 && client_connected) {
        sendto(udp_sock, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr,
               sizeof(client_addr));
    }

    return vprintf(fmt, args); // optional: also keep UART output
}

void udp_monitor_task(void *arg) {

    const uint8_t timeout_sec = 30;

    struct sockaddr_in recv_addr;
    int64_t last_seen_us = 0;
    char dummy_buf[8];

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(config_get()->udp_mon_port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        udp_sock = -1;
        monitor_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (bind(udp_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed");
        close(udp_sock);
        udp_sock = -1;
        monitor_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Waiting for UDP client on port: %d", config_get()->udp_mon_port);

    socklen_t addr_len = sizeof(recv_addr);
    while (!monitor_shutdown_requested) {
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

    // Cleanup
    ESP_LOGI(TAG, "UDP monitor task cleaning up");

    if (client_connected) {
        esp_log_set_vprintf(&vprintf);
        client_connected = false;
    }

    if (udp_sock >= 0) {
        close(udp_sock);
        udp_sock = -1;
    }

    monitor_shutdown_requested = false;
    monitor_task_handle = NULL;
    ESP_LOGE(TAG, "UDP monitor task exiting");
    vTaskDelete(NULL);
}

void udp_monitor_init(void) {
    if (monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "UDP monitor task already running");
        return;
    }

    monitor_shutdown_requested = false;
    xTaskCreate(udp_monitor_task, "udp_monitor", CONFIG_UDP_MONITOR_TASK_STACK_SIZE, NULL,
                CONFIG_UDP_MONITOR_TASK_PRIORITY, &monitor_task_handle);
}

void udp_monitor_shutdown(void) {
    if (monitor_task_handle == NULL) {
        ESP_LOGW(TAG, "UDP monitor task not running");
        return;
    }

    // ESP_LOGI(TAG, "Shutting down UDP monitor service");

    // Restore normal logging IMMEDIATELY before signaling shutdown
    // This prevents any logs during shutdown from trying to use the UDP socket
    if (client_connected) {
        esp_log_set_vprintf(&vprintf);
        client_connected = false;
    }

    // Signal task to stop
    monitor_shutdown_requested = true;

    // Wait for task to finish cleanup and set handle to NULL (max 1 second)
    int wait_count = 0;
    while (monitor_task_handle != NULL && wait_count < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;
    }
}