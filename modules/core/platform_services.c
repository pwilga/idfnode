#include <sys/time.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "driver/gpio.h"
#include "mdns.h"
#include "nvs_flash.h"

#if CONFIG_MQTT_ENABLE
#include "mqtt.h"
#endif
#include "config_manager.h"
#include "platform_services.h"
#include "supervisor.h"
#include "wifi.h"

EventGroupHandle_t app_event_group;
QueueHandle_t supervisor_queue;

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;

esp_err_t core_system_init(void) {

    app_event_group = xEventGroupCreate();
    if (!app_event_group) {
        ESP_LOGE(SYSTAG, "Failed to create event group!");
        return ESP_FAIL;
    }

    supervisor_queue = xQueueCreate(DEFAULT_QUEUE_LEN, sizeof(supervisor_command_t *));

    if (!supervisor_queue) {
        ESP_LOGE(SYSTAG, "Failed to create supervisor dispatcher queue!");
        return ESP_FAIL;
    }

    // onbard_led
    ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_2));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(nvs_flash_safe_init());

    config_manager_init();

    wifi_stack_init();
    wifi_ensure_sta_mode();

    ESP_ERROR_CHECK(init_mdns_service());

    return ESP_OK;
}

void esp_safe_restart() {

    ESP_LOGI(SYSTAG, "Restart command received - executing restart.");

    /* Just to avoid errors when esp32 close wifi connection */
    ESP_ERROR_CHECK(
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));

/* also to avoid error during shutdown */
#if CONFIG_MQTT_ENABLE
    mqtt_shutdown();
#endif

    esp_restart();
}

esp_err_t nvs_flash_safe_init() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t init_mdns_service() {
    esp_err_t ret =
        (mdns_init() != ESP_OK || mdns_hostname_set(config_get()->mdns_host) != ESP_OK ||
         mdns_instance_name_set(config_get()->mdns_instance) != ESP_OK)
            ? ESP_FAIL
            : ESP_OK;

    if (ret == ESP_OK)
        ESP_LOGI("mdns", "mDNS started with hostname: %s.local", config_get()->mdns_host);

    return ret;
}

const char *get_client_id(void) {
    static char buf[13];
    static bool initialized = false;

    if (initialized)
        return buf;

    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK)
        return NULL;

    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);

    initialized = true;
    return buf;
}

const char *get_boot_time(void) {

    static char iso8601[32] = {0};
    static bool initialized = false;

    if (!initialized) {
        struct timeval now;
        gettimeofday(&now, NULL);

        int64_t uptime_us = esp_timer_get_time();
        time_t boot_time = now.tv_sec - (uptime_us / 1000000);

        strftime(iso8601, sizeof(iso8601), "%Y-%m-%dT%H:%M:%SZ", gmtime(&boot_time));
        initialized = true;
    }

    return iso8601;
}

static bool onboard_led_state = false;

bool get_onboard_led_state(void) { return onboard_led_state; }

void onboard_led_set_state(logic_state_t state) {

    bool new_state;

    if (state == STATE_TOGGLE)
        new_state = !onboard_led_state;
    else
        new_state = state == STATE_ON ? true : false;

    if (onboard_led_state != new_state) {
        ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, !new_state));
        onboard_led_state = new_state;
    }

    supervisor_set_onboard_led_state(new_state);
}

void reset_nvs_partition(void) {
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) {
        ESP_LOGI(SYSTAG, "NVS ERASE: All keys erased successfully.");
        ESP_ERROR_CHECK(nvs_flash_safe_init());
    } else {
        ESP_LOGE(SYSTAG, "NVS ERASE: Failed to erase NVS: %s", esp_err_to_name(err));
    }
}