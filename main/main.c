#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "debug.h"
#include "ota.h"
#include "platform_services.h"
#include "udp_monitor.h"
#include "wifi.h"

#if CONFIG_MQTT_ENABLE
#include "mqtt.h"
#endif

void app_main(void) {

    TaskHandle_t ledBlinkTaskHandle = NULL;

    ESP_ERROR_CHECK(app_event_init());
    // static const char *TAG = "app-main";

    // xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 0, NULL);
    // xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 0,
    //             &ledBlinkTaskHandle);
    // xTaskCreate(memory_info_task, "memory_info_task", 4096, NULL, 0, NULL);

    // xTaskCreate(show_task_info, "show_task_info", 4096, NULL, 0, NULL);

    ESP_ERROR_CHECK(nvs_flash_safe_init());

    wifi_stack_init();
    wifi_ensure_sta_mode();

    ESP_ERROR_CHECK(init_mdns_service());

    xTaskCreate(tcp_ota_task, "tcp_ota", 8192, NULL, 0, NULL);
    xTaskCreate(udp_monitor_task, "udp_monitor", 4096, NULL, 5, NULL);

#if CONFIG_MQTT_ENABLE
    mqtt_init();
#endif

    for (;;) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
