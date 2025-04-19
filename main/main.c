#include "debug.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "helpers.h"
#include "ota.h"
#include "udp_monitor.h"
#include "wifi.h"

EventGroupHandle_t app_event_group;

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;

TaskHandle_t ledBlinkTaskHandle = NULL;

void app_main(void) {

  app_event_group = xEventGroupCreate();

  static const char *TAG = "app-main";
  // ESP_LOGI(TAG, "Create heartbeat task");

  // xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 0,
  //             &heartbeatTaskHandle);

  xTaskCreate(print_sys_info_task, "print_sys_info_task", 4096, NULL, 0, NULL);

  full_nvs_flash_init();
  wifi_sta_init();

  xTaskCreate(tcp_ota_task, "tcp_ota_task", 8192, NULL, 0, NULL);
  xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 0,
              &ledBlinkTaskHandle);
  xTaskCreate(memory_info_task, "memory_info_task", 4096, NULL, 0, NULL);
  xTaskCreate(udp_monitor_task, "udp_monitor", 4096, NULL, 5, NULL);

  initialise_mdns();

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
