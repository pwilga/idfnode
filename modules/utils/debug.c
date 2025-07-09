#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "platform_services.h"
#include "string.h"
#include "supervisor.h"
#include "config_manager.h"

static const char *TAG = "cikon-debug";

void debug_print_config_summary(void) {
    const config_t *cfg = config_get();
    ESP_LOGI(TAG, "| * CONFIG *");
#define PRINT_STR(field, size, defval) ESP_LOGI(TAG, "| %-16s | %-36.36s |", #field, cfg->field);
#define PRINT_U8(field, defval) ESP_LOGI(TAG, "| %-16s | %-36u |", #field, (unsigned)cfg->field);
#define PRINT_U16(field, defval) ESP_LOGI(TAG, "| %-16s | %-36u |", #field, (unsigned)cfg->field);
    CONFIG_FIELDS(PRINT_STR, PRINT_U8, PRINT_U16)
#undef PRINT_STR
#undef PRINT_U8
#undef PRINT_U16
}

void debug_print_tasks_summary(void) {

    TaskStatus_t *task_status_array;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    task_status_array = malloc(task_count * sizeof(TaskStatus_t));
    ESP_LOGI(TAG, "| * TASKS *");
    if (task_status_array) {
        UBaseType_t real_count = uxTaskGetSystemState(task_status_array, task_count, NULL);
        char line[128] = "";
        int col = 0;
        for (UBaseType_t i = 0; i < real_count; i++) {
            char entry[40];
            snprintf(entry, sizeof(entry), "| %-14s %6u ", task_status_array[i].pcTaskName,
                     (unsigned)(task_status_array[i].usStackHighWaterMark * sizeof(StackType_t)));
            strcat(line, entry);
            col++;
            if (col == 3) {
                strcat(line, "|");
                ESP_LOGI(TAG, "%s", line);
                line[0] = '\0';
                col = 0;
            }
        }
        if (col > 0 && line[0]) {
            strcat(line, "|");
            ESP_LOGI(TAG, "%s", line);
        }
        free(task_status_array);
    }
}

void debug_info_task(void *args) {

    while (1) {

        size_t free_heap = esp_get_free_heap_size();
        EventBits_t bits = xEventGroupGetBits(app_event_group);

        ESP_LOGI(TAG, "Free heap: %.2f KB", free_heap / 1024.0);

        char bits_str[128] = "";
        if (bits & WIFI_STA_CONNECTED_BIT)
            strcat(bits_str, "STA ");
        if (bits & WIFI_AP_STARTED_BIT)
            strcat(bits_str, "AP ");
        if (bits & MQTT_CONNECTED_BIT)
            strcat(bits_str, "MQTT ");
        if (bits & MQTT_FAIL_BIT)
            strcat(bits_str, "MQTT_F ");
        if (bits & HTTPS_SHUTDOWN_INITIATED_BIT)
            strcat(bits_str, "HTTPS_SHUT ");
        if (bits & TELEMETRY_TRIGGER_BIT)
            strcat(bits_str, "TEL ");
        // if (bits & WIFI_STA_FAIL_BIT)
        //     strcat(bits_str, "STA_F ");
        if (bits & MQTT_OFFLINE_PUBLISHED_BIT)
            strcat(bits_str, "MQTT_OFF ");
        if (bits & MQTT_SHUTDOWN_INITIATED_BIT)
            strcat(bits_str, "MQTT_SHUT ");
        if (bits & HTTPS_SERVER_STARTED_BIT)
            strcat(bits_str, "HTTPS ");
        if (bits & INTERNET_REACHABLE_BIT)
            strcat(bits_str, "INET ");

        ESP_LOGI(TAG, "Set bits: %s", bits_str[0] ? bits_str : "(none)");

        ESP_LOGI(TAG, "Uptime: %lu s", (unsigned long)state_get()->uptime);
        ESP_LOGI(TAG, "IP: %s", get_device_ip());

        debug_print_tasks_summary();
        // debug_print_config_summary();

        ESP_LOGI(TAG, "=====================");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void print_sys_info_task(void *args) {

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Get flash size failed");
        return;
    }

    while (1) {

        ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ", CONFIG_IDF_TARGET,
                 chip_info.cores, (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
                 (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
                 (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
                 (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)"
                                                                : "");
        ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);

        ESP_LOGI(TAG, "%" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
                 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void led_blink_task(void *args) {

    // gpio_reset_pin(GPIO_NUM_2);
    // gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);

    bool state = false;

    while (1) {
        ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, state));
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        state = !state;
    }
}

void heartbeat_task(void *args) {
    static const char *TAG = "heartbeatTask";

    while (1) {
        ESP_LOGW(TAG, "Computer-generated beating of human heart");
        vTaskDelay(1500 / portTICK_PERIOD_MS);
    }
}
