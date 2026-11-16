#include "driver/gpio.h"
#include "esp_log.h"
#define TAG "device:bedroom_ceiling"

// Board has a stock WS2812 addressable LED on GPIO8, left floating - the data pin
// picks up noise as fake bits and lights up randomly/white. Holding GPIO8 solid LOW
// keeps it permanently off (WS2812 never sees a valid frame).
#define STRAY_WS2812_DATA_GPIO GPIO_NUM_8

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "button_adapter.h"
#include "cmnd.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mdns.h"

#define BUTTON_NAME_LEFT "left"
#define BUTTON_NAME_RIGHT "right"

#define LIGHT_NAME_MAINS "mains"

// TEST CODE: the buttons switch the onboard LED of other boards, found by name
#define H2DEV_NAME "h2dev"
#define C6DEV_NAME "c6dev"

#define LED_GREEN_ON "{\"onboard_led\":{\"on\":true,\"h\":120,\"s\":100,\"v\":30}}"
#define LED_BLUE_ON "{\"onboard_led\":{\"on\":true,\"h\":240,\"s\":100,\"v\":30}}"
#define LED_OFF "{\"onboard_led\":{\"on\":false}}"

#define COAP_TASK_STACK 3072
#define COAP_PORT 5683
#define COAP_URI_PATH "cmnd"
#define COAP_ACK_TIMEOUT_S 1
#define COAP_ATTEMPTS 2
#define COAP_PACKET_MAX 192
#define COAP_RESOLVE_TIMEOUT_MS 2000

_Static_assert(sizeof(COAP_URI_PATH) - 1 <= 12, "the option length has to fit in the 4-bit field");

// The name goes out as a AAAA query alone: getaddrinfo asks for the IPv4 address too and waits for
// an answer that never comes
static bool peer_coap_resolve(const char *name, struct in6_addr *address) {
    mdns_result_t *results = NULL;
    if (mdns_query(name, NULL, NULL, MDNS_TYPE_AAAA, COAP_RESOLVE_TIMEOUT_MS, 1, &results) !=
            ESP_OK ||
        results == NULL) {
        return false;
    }

    bool found = false;
    for (mdns_result_t *result = results; result && !found; result = result->next) {
        for (mdns_ip_addr_t *ip = result->addr; ip && !found; ip = ip->next) {
            if (ip->addr.type != ESP_IPADDR_TYPE_V6) {
                continue;
            }
            memcpy(address, ip->addr.u_addr.ip6.addr, sizeof(*address));
            bool link_local = address->s6_addr[0] == 0xfe && (address->s6_addr[1] & 0xc0) == 0x80;
            found = !link_local;
        }
    }

    mdns_query_results_free(results);
    return found;
}

// A confirmable POST with the body to /cmnd, true once the board answered with 2.04
static bool peer_coap_post(const struct in6_addr *address, const char *body) {
    size_t body_len = strlen(body);
    uint8_t packet[COAP_PACKET_MAX];
    if (6 + sizeof(COAP_URI_PATH) - 1 + body_len > sizeof(packet)) {
        return false;
    }

    uint16_t message_id = (uint16_t)esp_random();
    size_t len = 0;
    packet[len++] = 0x40; // version 1, confirmable, no token
    packet[len++] = 0x02; // POST
    packet[len++] = message_id >> 8;
    packet[len++] = message_id & 0xFF;
    packet[len++] = (11 << 4) | (sizeof(COAP_URI_PATH) - 1); // option Uri-Path
    memcpy(packet + len, COAP_URI_PATH, sizeof(COAP_URI_PATH) - 1);
    len += sizeof(COAP_URI_PATH) - 1;
    packet[len++] = 0xFF; // payload marker
    memcpy(packet + len, body, body_len);
    len += body_len;

    int sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return false;
    }
    struct timeval timeout = {.tv_sec = COAP_ACK_TIMEOUT_S};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in6 dest = {
        .sin6_family = AF_INET6, .sin6_port = htons(COAP_PORT), .sin6_addr = *address};
    bool acked = false;
    for (int attempt = 0; attempt < COAP_ATTEMPTS && !acked; attempt++) {
        if (sendto(sock, packet, len, 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            break;
        }
        uint8_t reply[COAP_PACKET_MAX];
        int got = recv(sock, reply, sizeof(reply), 0);
        // The acknowledgement carries the answer: type 2, code 2.04, the same message ID
        acked = got >= 4 && (reply[0] >> 6) == 1 && ((reply[0] >> 4) & 3) == 2 &&
                reply[1] == 0x44 && (uint16_t)((reply[2] << 8) | reply[3]) == message_id;
    }
    close(sock);
    return acked;
}

static struct {
    const char *name;
    const char *body;
} s_coap;
static volatile bool s_coap_busy;

static void peer_coap_task(void *arg) {
    int64_t started = esp_timer_get_time();
    struct in6_addr address;

    if (!peer_coap_resolve(s_coap.name, &address)) {
        ESP_LOGW(TAG, "CoAP to %s: no IPv6 address, %d ms", s_coap.name,
                 (int)((esp_timer_get_time() - started) / 1000));
    } else {
        int64_t resolved = esp_timer_get_time();
        bool acked = peer_coap_post(&address, s_coap.body);
        int64_t done = esp_timer_get_time();
        ESP_LOGI(TAG, "CoAP to %s: %s, resolved in %d ms, answered in %d ms", s_coap.name,
                 acked ? "ok" : "no answer", (int)((resolved - started) / 1000),
                 (int)((done - resolved) / 1000));
    }

    s_coap_busy = false;
    vTaskDelete(NULL);
}

// Off the button callback so a slow name or link does not stall it
static void peer_coap_send(const char *name, const char *body) {
    if (s_coap_busy) {
        ESP_LOGW(TAG, "Previous CoAP command still running, ignoring");
        return;
    }
    s_coap_busy = true;
    s_coap.name = name;
    s_coap.body = body;
    if (xTaskCreate(peer_coap_task, "peer_coap", COAP_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        s_coap_busy = false;
        ESP_LOGE(TAG, "No memory for the CoAP task");
    }
}

// What the last click on each button asked for
static bool s_h2dev_on;
static bool s_c6dev_on;

static void device_button_handler(uint8_t button_idx, const char *name, button_event_t event) {
    if (strcmp(name, BUTTON_NAME_LEFT) == 0) {

        if (event == BUTTON_SINGLE_CLICK) {
            s_h2dev_on = !s_h2dev_on;
            peer_coap_send(H2DEV_NAME, s_h2dev_on ? LED_GREEN_ON : LED_OFF);
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
            peer_coap_send(C6DEV_NAME, s_c6dev_on ? LED_BLUE_ON : LED_OFF);
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
