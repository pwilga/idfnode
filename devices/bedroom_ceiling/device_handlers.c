#include "driver/gpio.h"
#include "esp_log.h"
#define TAG "device:bedroom_ceiling"

// Board has a stock WS2812 addressable LED on GPIO8, left floating - the data pin
// picks up noise as fake bits and lights up randomly/white. Holding GPIO8 solid LOW
// keeps it permanently off (WS2812 never sees a valid frame).
#define STRAY_WS2812_DATA_GPIO GPIO_NUM_8

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "button_adapter.h"
#include "cmnd.h"
#include "esp_timer.h"

#define BUTTON_NAME_LEFT "left"
#define BUTTON_NAME_RIGHT "right"

#define LIGHT_NAME_MAINS "mains"

// TEST CODE: the buttons switch the onboard LED of other boards. Over WiFi the OMR addresses are
// used (reachable through the border router); on Thread these become the ML-EID (tele:
// thread.addresses, type ml-eid), which a board keeps as long as its NVS stays.
#define H2DEV_ADDRESS "fd69:ed26:f62a:1:ed34:1752:4471:4a82"
#define C6DEV_ADDRESS "fd69:ed26:f62a:1:bb45:999e:60f0:8841"

#define LED_GREEN_ON "{\"onboard_led\":{\"on\":true,\"h\":120,\"s\":100,\"v\":30}}"
#define LED_BLUE_ON "{\"onboard_led\":{\"on\":true,\"h\":240,\"s\":100,\"v\":30}}"
#define LED_OFF "{\"onboard_led\":{\"on\":false}}"

#define PEER_HTTP_PORT 80
#define PEER_TIMEOUT_S 3
#define PEER_TASK_STACK 3072

static struct {
    const char *address;
    const char *body;
} s_peer;
static volatile bool s_peer_busy;

// One POST /cmnd to a board, off the button callback so a slow link does not stall it
static void peer_cmnd_task(void *arg) {
    const char *address = s_peer.address;
    const char *body = s_peer.body;
    int64_t started = esp_timer_get_time();
    const char *result = "failed";
    char code[4] = "";
    int sock = -1;

    struct sockaddr_in6 dest = {.sin6_family = AF_INET6, .sin6_port = htons(PEER_HTTP_PORT)};
    if (inet_pton(AF_INET6, address, &dest.sin6_addr) != 1) {
        result = "bad address";
        goto done;
    }

    sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_IPV6);
    if (sock < 0) {
        result = "no socket";
        goto done;
    }

    // Connect without blocking, so the wait has a limit
    fcntl(sock, F_SETFL, O_NONBLOCK);
    int rc = connect(sock, (struct sockaddr *)&dest, sizeof(dest));
    if (rc < 0 && errno != EINPROGRESS) {
        ESP_LOGW(TAG, "connect: errno %d (%s)", errno, strerror(errno));
        result = "connect failed";
        goto done;
    }
    if (rc < 0) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(sock, &writable);
        struct timeval wait = {.tv_sec = PEER_TIMEOUT_S};
        if (select(sock + 1, NULL, &writable, NULL, &wait) <= 0) {
            result = "connect timeout";
            goto done;
        }
        int err = 0;
        socklen_t err_len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &err_len);
        if (err != 0) {
            result = "connect refused";
            goto done;
        }
    }
    fcntl(sock, F_SETFL, 0);

    struct timeval timeout = {.tv_sec = PEER_TIMEOUT_S};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char request[256];
    int len = snprintf(request, sizeof(request),
                       "POST /cmnd HTTP/1.1\r\nHost: [%s]\r\nContent-Type: application/json\r\n"
                       "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                       address, (int)strlen(body), body);
    if (len <= 0 || len >= (int)sizeof(request) || send(sock, request, len, 0) != len) {
        result = "send failed";
        goto done;
    }

    // The status line is enough: "HTTP/1.1 200 OK"
    char reply[16] = "";
    int got = recv(sock, reply, sizeof(reply) - 1, 0);
    if (got >= 12 && strncmp(reply, "HTTP/1.", 7) == 0) {
        memcpy(code, reply + 9, 3);
        code[3] = '\0';
        result = code;
    } else {
        result = "no reply";
    }

done:
    if (sock >= 0) {
        close(sock);
    }
    ESP_LOGI(TAG, "POST /cmnd to [%s]: %s, %d ms", address, result,
             (int)((esp_timer_get_time() - started) / 1000));
    s_peer_busy = false;
    vTaskDelete(NULL);
}

static void peer_cmnd(const char *address, const char *body) {
    if (s_peer_busy) {
        ESP_LOGW(TAG, "Previous command still running, ignoring");
        return;
    }
    s_peer_busy = true;
    s_peer.address = address;
    s_peer.body = body;
    if (xTaskCreate(peer_cmnd_task, "peer_cmnd", PEER_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        s_peer_busy = false;
        ESP_LOGE(TAG, "No memory for the command task");
    }
}

// What the last click on each button asked for
static bool s_h2dev_on;
static bool s_c6dev_on;

static void device_button_handler(uint8_t button_idx, const char *name, button_event_t event) {
    if (strcmp(name, BUTTON_NAME_LEFT) == 0) {

        if (event == BUTTON_SINGLE_CLICK) {
            s_h2dev_on = !s_h2dev_on;
            peer_cmnd(H2DEV_ADDRESS, s_h2dev_on ? LED_GREEN_ON : LED_OFF);
        }
        if (event == BUTTON_DOUBLE_CLICK) {
            cmnd_submit(LIGHT_NAME_MAINS, "\"toggle\"");
        }
        if (event == BUTTON_LONG_PRESS_START) {
            cmnd_submit("help", NULL);
        }
        button_adapter_log_event(button_idx, event);

    } else if (strcmp(name, BUTTON_NAME_RIGHT) == 0) {

        if (event == BUTTON_SINGLE_CLICK) {
            s_c6dev_on = !s_c6dev_on;
            peer_cmnd(C6DEV_ADDRESS, s_c6dev_on ? LED_BLUE_ON : LED_OFF);
        }
        if (event == BUTTON_LONG_PRESS_START) {
            cmnd_submit("help", NULL);
        }
        button_adapter_log_event(button_idx, event);

    } else {
        ESP_LOGW(TAG, "Unknown button '%s' (idx %d)", name, button_idx);
    }
}
#endif // CONFIG_ENABLE_SUPERVISOR_BUTTON

void device_handlers_init(void) {
    ESP_LOGI(TAG, "Device handlers initialized");

    gpio_reset_pin(STRAY_WS2812_DATA_GPIO);
    gpio_set_direction(STRAY_WS2812_DATA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(STRAY_WS2812_DATA_GPIO, 0);

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
    button_adapter_register_callback(device_button_handler);
#endif
}
