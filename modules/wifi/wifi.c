#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "platform_services.h"

#include "wifi.h"

static const char *TAG = "wifi-sta";
static uint8_t retry_counter = 0;

esp_netif_t *sta_netif, *ap_netif;
bool ignore_sta_disconnect_event = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {

    if (event_base == WIFI_EVENT) {

        switch (event_id) {

        case WIFI_EVENT_STA_START:
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;

        case WIFI_EVENT_STA_DISCONNECTED:

            if (ignore_sta_disconnect_event) {
                ignore_sta_disconnect_event = false;
                return;
            }

            if (retry_counter < CONFIG_WIFI_MAXIMUM_RETRY) {
                ESP_ERROR_CHECK(esp_wifi_connect());
                retry_counter++;
                ESP_LOGI(TAG, "Retry to connect to the AP");
            } else {
                ESP_LOGW(TAG, "Failed to connect to the AP after %d retries",
                         CONFIG_WIFI_MAXIMUM_RETRY);
                xEventGroupSetBits(app_event_group, WIFI_STA_FAIL_BIT);
                xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_AP_STARTED_BIT);
            }
            ESP_LOGI(TAG, "Connect to the AP fail");
            break;

        case WIFI_EVENT_AP_START:
            xEventGroupSetBits(app_event_group, WIFI_AP_STARTED_BIT);
            xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT);
            break;

        case WIFI_EVENT_AP_STOP:
            xEventGroupClearBits(app_event_group, WIFI_AP_STARTED_BIT);
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Client connected to AP: " MACSTR ", AID=%d", MAC2STR(event->mac),
                     event->aid);

            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Client disconnected from AP: " MACSTR ", AID=%d", MAC2STR(event->mac),
                     event->aid);
            break;
        }

        default:
            break;
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_counter = 0;

        xEventGroupSetBits(app_event_group, WIFI_STA_CONNECTED_BIT);
        xEventGroupClearBits(app_event_group, WIFI_STA_FAIL_BIT);
    }
}

void wifi_stack_init() {

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
}

void wifi_ensure_ap_mode() {

    ignore_sta_disconnect_event = true;
    if (xEventGroupGetBits(app_event_group) & WIFI_STA_CONNECTED_BIT) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }

    wifi_config_t ap_config = {.ap = {.ssid = "ESP32_Config",
                                      .ssid_len = 0,
                                      .password = "",
                                      .channel = 1,
                                      .max_connection = 4,
                                      .authmode = WIFI_AUTH_OPEN}};
    if (sta_netif) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ap_netif = esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(app_event_group, WIFI_AP_STARTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT);
}

void wifi_ensure_sta_mode() {

    if (xEventGroupGetBits(app_event_group) & WIFI_AP_STARTED_BIT) {
        esp_wifi_stop();
    }

    wifi_config_t sta_config = {
        .sta = {.ssid = CONFIG_WIFI_SSID, .password = CONFIG_WIFI_PASSWORD}};

    if (ap_netif) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    sta_netif = esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits =
        xEventGroupWaitBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT, pdFALSE,
                            pdFALSE, portMAX_DELAY);

    if (bits & WIFI_STA_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", CONFIG_WIFI_SSID);
    }
    if (bits & WIFI_STA_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s", CONFIG_WIFI_SSID);
    }

    xEventGroupClearBits(app_event_group, WIFI_AP_STARTED_BIT);
}
