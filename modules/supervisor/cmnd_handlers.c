#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "cmnd.h"
#include "config_manager.h"
#include "debug.h"
#include "ha.h"
#include "https_server.h"
#include "json_parser.h"
#include "platform_services.h"
#include "supervisor.h"
#include "wifi.h"

#include "cmnd_handlers.h"

#define TAG "cmnd-handlers"

// Declare the missing function if not available in included headers
void supervisor_shutdown_all_wifi_services(void);

static void restart_handler(const char *args_json_str) {
    ESP_LOGI(TAG, "Executing restart command");
    esp_safe_restart();
}

static void set_ap_handler(const char *args_json_str) {
    ESP_LOGI(TAG, "Switching to AP mode");
    supervisor_shutdown_all_wifi_services();
    wifi_init_ap_mode();
}

static void set_sta_handler(const char *args_json_str) {
    ESP_LOGI(TAG, "Switching to STA mode");
    supervisor_shutdown_all_wifi_services();
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
    supervisor_set_onboard_led_state(new_state);
}

static void help_handler(const char *args_json_str) {
    (void)args_json_str;

    size_t total = 0;
    const command_t *reg = cmnd_get_registry(&total);

    ESP_LOGI(TAG, "=== Available commands (%zu total) ===", total);

    for (size_t i = 0; i < total; i++) {
        ESP_LOGI(TAG, "  %-15s - %s", reg[i].command_id, reg[i].description);
    }
    ESP_LOGI(TAG, "=======================================");
}

static void log_handler(const char *args_json_str) {
    ESP_LOGI(TAG, "Printing debug information");
    debug_print_sys_info();
    debug_print_config_summary();
}

static void set_conf_handler(const char *args_json_str) {
    cJSON *json_args = json_str_as_object(args_json_str);
    if (!json_args) {
        ESP_LOGW(TAG, "Command aborted: invalid JSON arguments: %s", args_json_str);
        return;
    }

    ESP_LOGI(TAG, "Setting configuration from JSON");
    config_manager_set_from_json(json_args);
    cJSON_Delete(json_args);
}

static void reset_conf_handler(const char *args_json_str) {
    ESP_LOGI(TAG, "Resetting configuration and restarting");
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

void cmnd_handlers_register(void) {

    // Basic system commands
    cmnd_register_command("restart", "Restart the device", restart_handler);
    cmnd_register_command("help", "Show available commands", help_handler);
    cmnd_register_command("log", "Print debug information", log_handler);

    // WiFi commands
    cmnd_register_command("ap", "Switch to AP mode", set_ap_handler);
    cmnd_register_command("sta", "Switch to STA mode", set_sta_handler);

    // Configuration commands
    cmnd_register_command("setconf", "Set configuration from JSON", set_conf_handler);
    cmnd_register_command("resetconf", "Reset configuration and restart", reset_conf_handler);

    // Hardware commands
    cmnd_register_command("onboard_led", "Set onboard LED state (on/off/toggle)",
                          onboard_led_handler);

    // Service commands
    cmnd_register_command("https", "Control HTTPS server (on/off)", https_handler);
    cmnd_register_command("sntp", "Control SNTP service (on/off)", sntp_handler);

    // Home Assistant discovery command
    cmnd_register_command("ha", "Trigger Home Assistant MQTT discovery", ha_handler);
}
