#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"

#include "config_manager.h"
#include "platform_services.h"
#include "wifi.h"

#define TAG "cikon-wifi"
#define TAG_STA TAG "-sta"
#define TAG_AP TAG "-ap"

esp_netif_t *sta_netif, *ap_netif;
bool ignore_sta_disconnect_event = false;
static bool ap_has_client = false;
static TaskHandle_t wifi_sta_connection_task_handle = NULL;

const char *wifi_disconnect_reason_str(uint8_t reason) {
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
        return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE:
        return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE:
        return "ASSOC_EXPIRE";
    case WIFI_REASON_AUTH_FAIL:
        return "AUTH_FAIL";
    case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "BEACON_TIMEOUT";
    default:
        return "UNKNOWN";
    }
}

#define WIFI_RETRY_INITIAL_MS 60000 // 1 min
#define WIFI_RETRY_MAX_MS 600000    // 10 min
#define WIFI_BURST_RETRY_COUNT 5
#define WIFI_BURST_RETRY_DELAY_MS 5000 // 5 sec

/**
 * @brief WiFi STA connection retry task with exponential backoff.
 *
 * After each burst of connection attempts, the interval before the next burst is multiplied
 * (doubled) to reduce network and power usage if the network is unavailable for a long time. This
 * prevents constant retries and allows for quick reconnection if the network returns soon, while
 * spacing out attempts over time if the outage persists.
 *
 * @param args Unused.
 */
static void wifi_sta_connection_task(void *args) {

    int interval = WIFI_RETRY_INITIAL_MS;

    while (1) {
        bool connected = false;
        for (int i = 0; i < WIFI_BURST_RETRY_COUNT; ++i) {
            ESP_LOGI(TAG_STA, "Connection attempt %d/%d in group (delay: %d ms)", i + 1,
                     WIFI_BURST_RETRY_COUNT, WIFI_BURST_RETRY_DELAY_MS);
            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_ERROR_CHECK(esp_wifi_connect());

            EventBits_t bits =
                xEventGroupWaitBits(app_event_group, WIFI_STA_CONNECTED_BIT, pdFALSE, pdFALSE,
                                    pdMS_TO_TICKS(WIFI_BURST_RETRY_DELAY_MS));

            if (bits & WIFI_STA_CONNECTED_BIT) {
                connected = true;
                break;
            }
            // else {
            //     xEventGroupSetBits(app_event_group, WIFI_STA_FAIL_BIT);
            //     // xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT);
            // }
        }

        if (connected) {
            // ESP_LOGI(TAG_STA, "Failed to connect to WiFi network (SSID: %s).",
            //          config_get()->wifi_ssid);
            // xEventGroupClearBits(app_event_group, WIFI_STA_FAIL_BIT);
            break;
        }

        ESP_LOGI(TAG_STA, "Waiting %d ms before next group of connection attempts", interval);
        vTaskDelay(pdMS_TO_TICKS(interval));
        interval *= 2;
        if (interval > WIFI_RETRY_MAX_MS)
            interval = WIFI_RETRY_MAX_MS;
    }
    ESP_LOGE(TAG_STA, "WiFi STA retry task exiting");
    wifi_sta_connection_task_handle = NULL;
    vTaskDelete(NULL);
}

void wifi_sta_connection_task_ensure_running(void) {
    if (wifi_sta_connection_task_handle == NULL) {
        xTaskCreate(wifi_sta_connection_task, "sta_con", 2048, NULL, 1,
                    &wifi_sta_connection_task_handle);
    }
}

void wifi_sta_connection_task_shutdown(void) {
    if (wifi_sta_connection_task_handle != NULL) {
        vTaskDelete(wifi_sta_connection_task_handle);
        wifi_sta_connection_task_handle = NULL;
        ESP_LOGI(TAG_STA, "WiFi STA connection task killed from outside");
    }
}

static void wifi_ap_timeout_task(void *args) {
    int seconds_without_clients = 0;
    const int timeout_seconds = CONFIG_WIFI_AP_INACTIVITY_TIMEOUT_MINUTES * 60;

    while (xEventGroupGetBits(app_event_group) & WIFI_AP_STARTED_BIT) {
        if (ap_has_client) {
            seconds_without_clients = 0;
        } else {
            seconds_without_clients++;
            if (seconds_without_clients >= timeout_seconds) {
                ESP_LOGI(
                    TAG_AP,
                    "AP inactivity timeout: no clients for %d minute(s), disabling Access Point "
                    "(AP).",
                    CONFIG_WIFI_AP_INACTIVITY_TIMEOUT_MINUTES);

                ESP_ERROR_CHECK(safe_wifi_stop());
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

            // case WIFI_EVENT_STA_START:
            //     ESP_ERROR_CHECK(esp_wifi_connect());
            //     break;

        case WIFI_EVENT_STA_DISCONNECTED:

            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGI(TAG_STA, "Disconnected, reason: %d (%s)", disconn->reason,
                     wifi_disconnect_reason_str(disconn->reason));

            if (ignore_sta_disconnect_event) {
                ignore_sta_disconnect_event = false;
                return;
            }

            xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT);
            wifi_sta_connection_task_ensure_running();

            break;

        case WIFI_EVENT_AP_START:
            xEventGroupSetBits(app_event_group, WIFI_AP_STARTED_BIT);
            // xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT);
            ap_has_client = false;
            xTaskCreate(wifi_ap_timeout_task, "ap_timeout", 3072, NULL, 1, NULL);
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG_AP, "Access Point (AP) has been stopped.");
            xEventGroupClearBits(app_event_group, WIFI_AP_STARTED_BIT);
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG_AP, "A client has connected to the Access Point (AP).");
            ap_has_client = true;
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG_AP, "A client has disconnected from the Access Point (AP).");
            ap_has_client = false;
            break;

        default:
            break;
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG_STA, "Successfully connected to WiFi network (SSID: %s).",
                 config_get()->wifi_ssid);

        xEventGroupSetBits(app_event_group, WIFI_STA_CONNECTED_BIT);
        // xEventGroupClearBits(app_event_group, WIFI_STA_FAIL_BIT);
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
}

void wifi_ensure_sta_mode() {

    ESP_ERROR_CHECK(safe_wifi_stop());

    wifi_config_t sta_config = {0};

    strncpy((char *)sta_config.sta.ssid, config_get()->wifi_ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, config_get()->wifi_pass,
            sizeof(sta_config.sta.password));

    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    sta_netif = esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_sta_connection_task_ensure_running();
}

esp_err_t safe_wifi_stop() {

    wifi_sta_connection_task_shutdown();

    uint8_t timeout = 100;
    while (wifi_sta_connection_task_handle != NULL && timeout--)
        vTaskDelay(pdMS_TO_TICKS(10));

    ignore_sta_disconnect_event = true;

    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_stop();

    xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT | WIFI_AP_STARTED_BIT);

    if (sta_netif) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }

    if (ap_netif) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }

    return err;
}
