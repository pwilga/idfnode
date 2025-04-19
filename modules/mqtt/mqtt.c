#include "config.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define TAG "mqtt-simple"

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);
    break;

  case MQTT_EVENT_DISCONNECTED:
    xEventGroupSetBits(app_event_group, MQTT_FAIL_BIT);
    break;
  default:
    break;
  }
}

void mqtt_taks(void *args) {

  const esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = "rapi",
      .credentials.username = "mqttuser",
      .credentials.authentication.password = "55Torba66?"};

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 client);
  esp_mqtt_client_start(client);
}