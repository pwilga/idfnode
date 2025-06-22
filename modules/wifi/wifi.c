#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"

#include "config_manager.h"
#include "platform_services.h"
#include "wifi.h"
#include "freertos/timers.h"

#define WIFI_AP_INACTIVITY_TIMEOUT_MINUTES 1

static const char *TAG = "cikon-wifi";
static uint8_t retry_counter = 0;

esp_netif_t *sta_netif, *ap_netif;
bool ignore_sta_disconnect_event = false;
static bool ap_has_client = false;

static void wifi_ap_timeout_task(void *args) {
    int seconds_without_clients = 0;
    const int timeout_seconds = WIFI_AP_INACTIVITY_TIMEOUT_MINUTES * 60;

    while (xEventGroupGetBits(app_event_group) & WIFI_AP_STARTED_BIT) {
        if (ap_has_client) {
            seconds_without_clients = 0;
        } else {
            seconds_without_clients++;
            if (seconds_without_clients >= timeout_seconds) {
                ESP_LOGI(TAG,
                         "AP inactivity timeout: no clients for %d minutes, disabling Access Point "
                         "(AP).",
                         WIFI_AP_INACTIVITY_TIMEOUT_MINUTES);
                wifi_ensure_sta_mode();
                seconds_without_clients = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

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

            if (retry_counter < config_get()->wifi_max_retry) {
                ESP_ERROR_CHECK(esp_wifi_connect());
                retry_counter++;
                ESP_LOGI(TAG, "Retrying connection to WiFi network (STA)...");
            } else {
                ESP_LOGW(TAG, "Failed to connect to WiFi network (STA) after %d retries.",
                         config_get()->wifi_max_retry);
                xEventGroupSetBits(app_event_group, WIFI_STA_FAIL_BIT);
                xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_AP_STARTED_BIT);
            }
            ESP_LOGI(TAG, "Failed to connect to WiFi network (STA).");
            break;

        case WIFI_EVENT_AP_START:
            xEventGroupSetBits(app_event_group, WIFI_AP_STARTED_BIT);
            xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT);
            ap_has_client = false;
            xTaskCreate(wifi_ap_timeout_task, "ap_timeout", 3072, NULL, 1, NULL);
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "Access Point (AP) has been stopped.");
            xEventGroupClearBits(app_event_group, WIFI_AP_STARTED_BIT);
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "A client has connected to the Access Point (AP).");
            ap_has_client = true;
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "A client has disconnected from the Access Point (AP).");
            ap_has_client = false;
            break;

        default:
            break;
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

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

    ESP_ERROR_CHECK(safe_wifi_stop());

    wifi_config_t ap_config = {.ap = {.ssid_len = 0,
                                      .password = "",
                                      .channel = 1,
                                      .max_connection = 1,
                                      .authmode = WIFI_AUTH_OPEN}};

    strncpy((char *)ap_config.ap.ssid, config_get()->wifi_ap_ssid, sizeof(ap_config.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ap_netif = esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(app_event_group, WIFI_AP_STARTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT);
}

void wifi_ensure_sta_mode() {

    ESP_ERROR_CHECK(safe_wifi_stop());

    wifi_config_t sta_config = {0};

    strncpy((char *)sta_config.sta.ssid, config_get()->wifi_ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, config_get()->wifi_pass,
            sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    sta_netif = esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits =
        xEventGroupWaitBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT, pdFALSE,
                            pdFALSE, portMAX_DELAY);

    if (bits & WIFI_STA_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Successfully connected to WiFi network (SSID: %s).",
                 config_get()->wifi_ssid);
    }
    if (bits & WIFI_STA_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to WiFi network (SSID: %s).", config_get()->wifi_ssid);
    }

    xEventGroupClearBits(app_event_group, WIFI_AP_STARTED_BIT);
}

esp_err_t safe_wifi_stop() {

    ignore_sta_disconnect_event = true;

    EventBits_t bits = xEventGroupGetBits(app_event_group);
    esp_err_t err = ESP_OK;
    bool any_action = false;

    if (bits & WIFI_STA_CONNECTED_BIT) {
        err = esp_wifi_disconnect();
        ESP_LOGI(TAG, "Disconnecting from WiFi network (STA): %s", esp_err_to_name(err));
        any_action = true;
    }
    if ((bits & WIFI_STA_CONNECTED_BIT) || (bits & WIFI_AP_STARTED_BIT)) {
        err = esp_wifi_stop();
        ESP_LOGI(TAG, "Stopping WiFi (STA/AP): %s", esp_err_to_name(err));
        xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_AP_STARTED_BIT);
        any_action = true;
    }

    if (sta_netif) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }

    if (ap_netif) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }

    if (!any_action) {
        ESP_LOGI(TAG, "WiFi is already stopped.");
    }
    return err;
}
