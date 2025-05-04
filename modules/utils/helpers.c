#include "helpers.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "ha.h"
#include "mdns.h"
// #include "mqtt5_client.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <string.h>

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

  esp_err_t ret = (mdns_init() != ESP_OK ||
                   mdns_hostname_set(CONFIG_MDNS_HOSTNAME) != ESP_OK ||
                   mdns_instance_name_set(CONFIG_MDNS_INSTANCE_NAME) != ESP_OK)
                      ? ESP_FAIL
                      : ESP_OK;

  if (ret == ESP_OK)
    ESP_LOGI("mdns", "mDNS started with hostname: %s.local",
             CONFIG_MDNS_HOSTNAME);

  return ret;
}

void restart(void *args) {
  xTaskCreate(esp_safe_restart, "restart", 2048, NULL, configMAX_PRIORITIES - 1,
              NULL);
}

static bool onboard_led_state = false;

bool get_onboard_led_state(void) { return onboard_led_state; }

void onboard_led(void *args) {

  bool new_state = parse_bool_json((cJSON *)args);

  if (onboard_led_state != new_state) {
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, !new_state));
    onboard_led_state = new_state;
  }
}

const command_entry_t command_table[] = {{"onboard_led", onboard_led},
                                         {"restart", restart},
                                         {"ha", publish_ha_mqtt_discovery},
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

bool valid_bool_param(cJSON *cmnd_param) {
  return cJSON_IsString(cmnd_param) || cJSON_IsNumber(cmnd_param) ||
         cJSON_IsBool(cmnd_param) || cJSON_IsNull(cmnd_param);
}

bool parse_bool_string(const char *input) {
  return input && (!strcasecmp(input, "true") || !strcasecmp(input, "1") ||
                   !strcasecmp(input, "on") || !strcasecmp(input, "up"));
}

bool parse_bool_json(cJSON *cmnd_param) {

  const char *TAG = "parse-json-bool";

  // cJSON *cmnd_param = cJSON_Parse(input);
  if (!valid_bool_param(cmnd_param)) {
    ESP_LOGE(TAG, "Invalid command parameter: expected a JSON string, number, "
                  "boolean or null.");
    // cJSON_Delete(cmnd_param);
    return false;
  }

  bool state = false;
  if (cJSON_IsString(cmnd_param)) {
    state = parse_bool_string(cmnd_param->valuestring);
  } else if (cJSON_IsNumber(cmnd_param)) {
    state = cmnd_param->valuedouble;
  } else if (cJSON_IsBool(cmnd_param)) {
    state = cJSON_IsTrue(cmnd_param);
  }

  // cJSON_Delete(cmnd_param);
  return state;
}

const char *get_client_id(void) {

  static char buf[13];
  static bool initialized = false;

  if (initialized)
    return buf;

  uint8_t mac[6];
  esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (err != ESP_OK)
    return NULL;

  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);

  initialized = true;
  return buf;
}

char *sanitize(const char *s) {
  size_t len = strlen(s);
  char *out = malloc(len + 1);
  if (!out)
    return NULL;

  for (size_t i = 0; i < len; i++) {
    if (s[i] == ' ')
      out[i] = '_';
    else if (isupper((unsigned char)s[i]))
      out[i] = tolower((unsigned char)s[i]);
    else
      out[i] = s[i];
  }
  out[len] = '\0';
  return out;
}
