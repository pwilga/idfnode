#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "wifi-sta";
static uint8_t retry_counter = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_ERROR_CHECK(esp_wifi_connect());
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {

    if (retry_counter < CONFIG_WIFI_MAXIMUM_RETRY) {
      ESP_ERROR_CHECK(esp_wifi_connect());
      retry_counter++;
      ESP_LOGI(TAG, "Retry to connect to the AP");
    } else {
      xEventGroupSetBits(app_event_group, NETWORK_FAIL_BIT);
    }
    ESP_LOGI(TAG, "Connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    retry_counter = 0;
    xEventGroupSetBits(app_event_group, NETWORK_CONNECTED_BIT);
  }
}

/**
 * Initializes Wi-Fi in station mode and waits until the device is connected to
 * a network. This function must be called before starting any task that relies
 * on network connectivity.
 */
esp_err_t wifi_sta_init() {

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = CONFIG_WIFI_SSID, .password = CONFIG_WIFI_PASSWORD,
              /* Authmode threshold resets to WPA2 as default if password
               * matches WPA2 standards (password len => 8). If you want to
               * connect the device to deprecated WEP/WPA networks, Please set
               * the threshold value to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set
               * the password with length and format matching to
               * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
               */
              // .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
              // .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
              // .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // ESP_LOGI(TAG, "Wifi radio started.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(
      app_event_group, NETWORK_CONNECTED_BIT | NETWORK_FAIL_BIT, pdFALSE,
      pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & NETWORK_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to AP SSID: %s", CONFIG_WIFI_SSID);
    return ESP_OK;
  } else if (bits & NETWORK_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID: %s", CONFIG_WIFI_SSID);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
  return ESP_FAIL;
}
