#include "config.h"
#include "debug.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "helpers.h"
#include "ota.h"
#include "udp_monitor.h"
#include "wifi.h"

#if CONFIG_MQTT_ENABLE
#include "mqtt.h"
#endif

void app_main(void) {

  TaskHandle_t ledBlinkTaskHandle = NULL;

  ESP_ERROR_CHECK(app_event_init());
  static const char *TAG = "app-main";

  // xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 0, NULL);
  // xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 0,
  //             &ledBlinkTaskHandle);
  xTaskCreate(memory_info_task, "memory_info_task", 4096, NULL, 0, NULL);

  // xTaskCreate(print_sys_info_task, "print_sys_info_task", 4096, NULL, 0,
  // NULL);

  ESP_ERROR_CHECK(full_nvs_flash_init());

  // Must be called before any task that uses network features.
  ESP_ERROR_CHECK(wifi_sta_init());
  ESP_ERROR_CHECK(full_mdns_init());

  xTaskCreate(tcp_ota_task, "tcp_ota_task", 8192, NULL, 0, NULL);
  xTaskCreate(udp_monitor_task, "udp_monitor_task", 4096, NULL, 5, NULL);

#if CONFIG_MQTT_ENABLE
  mqtt_init();
#endif

  uint32_t index = 0;

  for (;;) {
    index++;
    if (index == 100) {
      ESP_LOGE(TAG, "Closing Blinking Led Task");
      vTaskDelete(ledBlinkTaskHandle);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}
