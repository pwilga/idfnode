/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
// #include "esp_system.h"

#include "esp_log.h"
#include "driver/gpio.h"

#include "wifi.h"
#include "helpers.h"
#include "ota.h"

static const char *TAG = "MEM_INFO";

void memory_info_task(void *pvParameter)
{
    while (1)
    {
        // General free heap
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();

        // Internal (default) memory
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        // External (PSRAM) memory, if available
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

        ESP_LOGI(TAG, "==== Memory Info ====");
        ESP_LOGI(TAG, "Free heap:        %.2f KB", free_heap / 1024.0);
        ESP_LOGI(TAG, "Min. ever free:   %.2f KB", min_free_heap / 1024.0);
        ESP_LOGI(TAG, "Internal free:    %.2f KB", internal_free / 1024.0);
        ESP_LOGI(TAG, "External (PSRAM): %.2f KB", psram_free / 1024.0);
        ESP_LOGI(TAG, "=====================");

        vTaskDelay(pdMS_TO_TICKS(5000)); // print every 5 seconds
    }
}

void printSysInfoTask(void *args)
{
    static const char *TAG = "printSysInfoTask";

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Get flash size failed");
        return;
    }

    while (1)
    {

        ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ",
                 CONFIG_IDF_TARGET,
                 chip_info.cores,
                 (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
                 (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
                 (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
                 (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");
        ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);

        ESP_LOGI(TAG, "%" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
                 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

        vTaskDelay(10000 / portTICK_PERIOD_MS);
        print_hello();
    }
}

void ledBlinkTask(void *args)
{
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);

    bool state = false;

    while (1)
    {
        gpio_set_level(GPIO_NUM_2, state);
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        state = !state;
    }
}

void heartbeatTask(void *args)
{
    static const char *TAG = "heartbeatTask";

    while (1)
    {
        ESP_LOGW(TAG, "Computer-generated beating of human heart");
        vTaskDelay(1500 / portTICK_PERIOD_MS);
    }
}

TaskHandle_t heartbeatTaskHandle = NULL;
TaskHandle_t ledBlinkTaskHandle = NULL;
TaskHandle_t printSysInfoTaskHandle = NULL;

void app_main(void)
{
    static const char *TAG = "app_main";
    ESP_LOGI(TAG, "Create heartbeat task");

    xTaskCreate(
        heartbeatTask,
        "heartbeatTask",
        2048,
        NULL,
        0,
        &heartbeatTaskHandle);

    // xTaskCreate(printSysInfoTask, "printSysInfoTask", 4096, NULL, 0, &printSysInfoTaskHandle);

    full_nvs_flash_init();
    wifi_sta_init();

    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 0, NULL);
    xTaskCreate(ledBlinkTask, "ledBlinkTask", 2048, NULL, 0, &ledBlinkTaskHandle);
    xTaskCreate(memory_info_task, "memory_info_task", 4096, NULL, 0, NULL);

    uint32_t index = 0;
    for (;;)
    {
        if (index == 100)
        {
            ESP_LOGE(TAG, "Closing Blinking Led Task");
            vTaskDelete(ledBlinkTaskHandle);
        }
        printf("Loop counter: %lu\n", ++index);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
