#include "config.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

EventGroupHandle_t app_event_group = NULL;

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
  if (app_event_group == NULL) {
    ESP_LOGE("cikon-systems", "Failed to create event group!");
    return ESP_FAIL;
  }

  return ESP_OK;
}

/**
 * @brief Unregisters network-related event handlers and performs a full ESP
 * restart.
 *
 * This function ensures proper cleanup of Wi-Fi and IP event handlers,
 * deinitializes mDNS, and then restarts the system.
 */
void full_esp_restart() {

  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));

  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));

  mdns_free();
  esp_restart();
}
