#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "mdns.h"
#include "nvs_flash.h"

void full_nvs_flash_init() {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

void initialise_mdns(void) {

  xEventGroupWaitBits(app_event_group, NETWORK_CONNECTED_BIT, pdFALSE, pdFALSE,
                      portMAX_DELAY);

  ESP_ERROR_CHECK(mdns_init());
  ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_MDNS_HOSTNAME));
  ESP_ERROR_CHECK(mdns_instance_name_set(CONFIG_MDNS_INSTANCE_NAME));

  ESP_LOGE("mdns", "mDNS started with hostname: %s.local",
           CONFIG_MDNS_HOSTNAME);
}