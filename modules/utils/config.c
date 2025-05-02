#include "config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#if CONFIG_MQTT_ENABLE
#include "mqtt.h"
#endif

EventGroupHandle_t app_event_group;

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;

/**
 * @brief Initializes global system-wide variables such as event groups.
 *
 * Must be called during application startup before any task attempts
 * to use `app_event_group` or register related event bits.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if event group creation failed
 */
esp_err_t app_event_init(void) {

  app_event_group = xEventGroupCreate();
  if (!app_event_group) {
    ESP_LOGE("cikon-systems", "Failed to create event group!");
    return ESP_FAIL;
  }

  // onbard_led
  ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_2));
  ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT));

  return ESP_OK;
}

/**
 * @brief Unregisters network-related event handlers and performs a full ESP
 * restart.
 *
 */
void esp_safe_restart(void *args) {

  // Argument 'args' is intentionally unused.
  // Required only to match the TaskFunction_t signature,
  // so that this function can be used as a FreeRTOS task.
  (void)args;

  ESP_LOGI("restart", "Restart command received - executing restart.");

  /* Just to avoid errors when esp32 close wifi connection */
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));

  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));

  /* also to avoid error during shutdown */
#if CONFIG_MQTT_ENABLE
  mqtt_shutdown();
#endif
  // mdns_free();

  // esp_wifi_disconnect();
  // esp_wifi_stop();

  esp_restart();
}
