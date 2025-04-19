#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "mdns.h"
#include "nvs_flash.h"

esp_err_t full_nvs_flash_init() {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  return ret;
}

esp_err_t full_mdns_init() {

  // xEventGroupWaitBits(app_event_group, NETWORK_CONNECTED_BIT, pdFALSE,
  // pdFALSE,
  //                     portMAX_DELAY);

  // ESP_ERROR_CHECK(mdns_init());
  // ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_MDNS_HOSTNAME));
  // ESP_ERROR_CHECK(mdns_instance_name_set(CONFIG_MDNS_INSTANCE_NAME));

  esp_err_t ret = (mdns_init() != ESP_OK ||
                   mdns_hostname_set(CONFIG_MDNS_HOSTNAME) != ESP_OK ||
                   mdns_instance_name_set(CONFIG_MDNS_INSTANCE_NAME) != ESP_OK)
                      ? ESP_FAIL
                      : ESP_OK;

  if (ret == ESP_OK)
    ESP_LOGE("mdns", "mDNS started with hostname: %s.local",
             CONFIG_MDNS_HOSTNAME);

  return ret;
}