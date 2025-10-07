#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "cmnd.h"
#include "cmnd_basic_handlers.h"
#include "config_manager.h"
#include "debug.h"
#include "ha.h"
#include "https_server.h"
#include "json_parser.h"
#include "mqtt.h"
#include "platform_services.h"
#include "wifi.h"

#define TAG "cmnd-basic-handlers"

void shutdown_all_wifi_services() {

    mqtt_publish_offline_state();
    mqtt_shutdown();

    https_shutdown();
}

static void restart_handler(const char *args_json_str) {
    (void)args_json_str;

    esp_safe_restart();
}

static void set_ap_handler(const char *args_json_str) {
    (void)args_json_str;

    shutdown_all_wifi_services();
    wifi_init_ap_mode();
}

static void set_sta_handler(const char *args_json_str) {
    (void)args_json_str;

    shutdown_all_wifi_services();
    wifi_init_sta_mode();
}

static void onboard_led_handler(const char *args_json_str) {
    logic_state_t state = json_str_as_logic_state(args_json_str);
    bool new_state;

    if (state == STATE_TOGGLE) {
        new_state = !get_onboard_led_state();
    } else {
        new_state = (state == STATE_ON) ? true : false;
    }

    ESP_LOGI(TAG, "Setting LED to %s", new_state ? "ON" : "OFF");
    onboard_led_set_state(new_state);
    mqtt_trigger_telemetry();
}

static void help_handler(const char *args_json_str) {
    (void)args_json_str;

    size_t total = 0;
    const command_t *reg = cmnd_get_registry(&total);

    // ESP_LOGI(TAG, "=== Available commands (%zu total) ===", total);

    for (size_t i = 0; i < total; i++) {
        ESP_LOGI(TAG, "  %-15s - %s", reg[i].command_id, reg[i].description);
    }
    ESP_LOGI(TAG, "=======================================");
}

static void log_handler(const char *args_json_str) {
    (void)args_json_str;

    debug_print_sys_info();
    debug_print_config_summary();
}

static void set_conf_handler(const char *args_json_str) {
    cJSON *json_args = json_str_as_object(args_json_str);
    if (!json_args) {
        ESP_LOGW(TAG, "Command aborted: invalid JSON arguments: %s", args_json_str);
        return;
    }

    config_manager_set_from_json(json_args);
    cJSON_Delete(json_args);
}

static void reset_conf_handler(const char *args_json_str) {
    (void)args_json_str;

    reset_nvs_partition();
    esp_safe_restart();
}

static void https_handler(const char *args_json_str) {
    logic_state_t https_state = json_str_as_logic_state(args_json_str);

    if (https_state == STATE_ON) {
        ESP_LOGI(TAG, "Starting HTTPS server");
        https_init();
    } else if (https_state == STATE_OFF) {
        ESP_LOGI(TAG, "Stopping HTTPS server");
        https_shutdown();
    } else {
        ESP_LOGW(TAG, "Invalid HTTPS state");
    }
}

static void sntp_handler(const char *args_json_str) {
    logic_state_t sntp_state = json_str_as_logic_state(args_json_str);

    if (sntp_state == STATE_ON) {
        ESP_LOGI(TAG, "Starting SNTP service");
        esp_netif_sntp_deinit();
        sntp_service_init();
    } else if (sntp_state == STATE_OFF) {
        ESP_LOGI(TAG, "Stopping SNTP service");
        esp_netif_sntp_deinit();
    } else {
        ESP_LOGW(TAG, "Invalid SNTP state");
    }
}

static void ha_handler(const char *args_json_str) {
    logic_state_t force_empty_payload = json_str_as_logic_state(args_json_str);
    if (force_empty_payload == STATE_TOGGLE) {
        ESP_LOGE(TAG, "Toggling is not permitted for HA discovery");
        return;
    }

    ESP_LOGI(TAG, "Triggering Home Assistant MQTT discovery");
    publish_ha_mqtt_discovery(force_empty_payload == STATE_OFF);
}

void cmnd_basic_handlers_register(void) {

    // Basic system commands
    cmnd_register("restart", "Restart the device", restart_handler);
    cmnd_register("help", "Show available commands", help_handler);
    cmnd_register("log", "Print debug information", log_handler);

    // WiFi commands
    cmnd_register("ap", "Switch to AP mode", set_ap_handler);
    cmnd_register("sta", "Switch to STA mode", set_sta_handler);

    // Configuration commands
    cmnd_register("setconf", "Set configuration from JSON", set_conf_handler);
    cmnd_register("resetconf", "Reset configuration and restart", reset_conf_handler);

    // Hardware commands
    cmnd_register("onboard_led", "Set onboard LED state (on/off/toggle)", onboard_led_handler);

    // Service commands
    cmnd_register("https", "Control HTTPS server (on/off)", https_handler);
    cmnd_register("sntp", "Control SNTP service (on/off)", sntp_handler);

    // Home Assistant discovery command
    cmnd_register("ha", "Trigger Home Assistant MQTT discovery", ha_handler);
}
