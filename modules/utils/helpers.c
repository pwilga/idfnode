#include "helpers.h"
#include "config.h"
#include "driver/gpio.h"
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

void test(void *args) { ESP_LOGE("TEST", "TEST"); }

void onboard_led(void *args) {
  ESP_LOGI("LED", "toggle led: %s", (char *)args);
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, parse_bool_string((char *)args)));
}

const command_entry_t command_table[] = {{"onboard_led", onboard_led},
                                         {"restart", test},
                                         {NULL, NULL}};

void dispatch_command(const char *cmd, void *args) {

  const char *TAG = "command-dispatcher";

  for (int i = 0; command_table[i].command_name; ++i) {
    if (strcasecmp(cmd, command_table[i].command_name))
      continue;

    ESP_LOGI(TAG, "Executing: %s", cmd);
    command_table[i].handler(args);
    return;
  }
  ESP_LOGW(TAG, "Unknown command: %s", cmd);
}

bool parse_bool_string(const char *input) {
  return input && (!strcasecmp(input, "true") || !strcasecmp(input, "1") ||
                   !strcasecmp(input, "on") || !strcasecmp(input, "up"));
}
