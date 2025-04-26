#include "ha.h"
#include "cJSON.h"
#include "esp_log.h"
#include "helpers.h"
#include "mqtt.h"
#include "stdbool.h"

/* Homeassistant MQTT Discovery */

// Home Assistant MQTT Discovery prefix
#define MQTT_DISCOVERY_PREFIX "homeassistant"
#define TAG "home-assistant"

static bool has_sent_full_dev = false;

/**
 * @brief Builds the Home Assistant MQTT Discovery "device" object.
 *
 * On the first call, returns the full device metadata including name,
 * model, software and hardware version, and configuration URL.
 * On subsequent calls, only the minimal "ids" field is returned.
 *
 * This allows devices to avoid sending duplicate metadata after
 * the initial discovery message, while still including required identifiers.
 *
 * @return A cJSON object representing the "device" field of the ha payload.
 *         Caller is responsible for freeing the object with cJSON_Delete().
 */
cJSON *build_ha_device_json(void) {

  const char *cliend_id = get_client_id();
  cJSON *device = cJSON_CreateObject();

  cJSON_AddStringToObject(device, "ids", cliend_id);

  if (has_sent_full_dev) {
    return device;
  }

  has_sent_full_dev = true;

  cJSON_AddStringToObject(device, "name", "Cikon MQTT Device");
  cJSON_AddStringToObject(device, "mf", "Cikon Systems");
  cJSON_AddStringToObject(device, "mdl", "ESP32 DevKit");
  cJSON_AddStringToObject(device, "hw", "ESP-IDF");
  cJSON_AddStringToObject(device, "sw", "0.0.1");

  //   const char *url = get_local_ip_url();
  //   if (url) {
  //     cJSON_AddStringToObject(device, "cu", url);
  //   }

  return device;
}

void build_ha_entity(ha_entity_t *entity, const char *name,
                     const char *entity_type) {

  const size_t BUF_LEN = 128;

  char unique_id[64];
  snprintf(unique_id, sizeof(unique_id), "%.6s_%s", get_client_id(), name);

  snprintf(entity->ha_config_topic, sizeof(entity->ha_config_topic),
           "%s/%s/%s/config", MQTT_DISCOVERY_PREFIX, entity_type, unique_id);

  char buf[BUF_LEN];

  cJSON *json_root = cJSON_CreateObject();
  cJSON_AddStringToObject(json_root, "name", name);
  cJSON_AddStringToObject(json_root, "uniq_id", unique_id);

  MQTT_TELEMETRY_TOPIC(buf);
  cJSON_AddStringToObject(json_root, "stat_t", buf);

  MQTT_COMMAND_TOPIC(buf);
  cJSON_AddStringToObject(json_root, "cmd_t", buf);

  MQTT_AVAILABILITY_TOPIC(buf);
  cJSON_AddStringToObject(json_root, "avty_t", buf);

  snprintf(buf, sizeof(buf), "{{ value_json.%s  }}", name);
  cJSON_AddStringToObject(json_root, "val_tpl", buf);

  // to musi isc poza entity
  // cJSON_AddStringToObject(json_root, "dev_cla", "temperature");
  // i to
  cJSON_AddStringToObject(json_root, "unit_of_measurement", "C");
  cJSON_AddItemToObject(json_root, "dev", build_ha_device_json());

  entity->ha_config_payload = json_root;
}

void free_ha_entity(ha_entity_t *entity) {

  if (entity->ha_config_payload) {
    cJSON_Delete(entity->ha_config_payload);
    entity->ha_config_payload = NULL;
  }
}

// only temporary
extern esp_mqtt_client_handle_t mqtt_client;

void publish_ha_mqtt_discovery(void *args) {

  cJSON *cmnd_param = cJSON_Parse((char *)args);

  if (!cmnd_param || !cJSON_IsString(cmnd_param)) {
    ESP_LOGE(TAG, "Invalid command parameter: expected a JSON string.");
    cJSON_Delete(cmnd_param);
    return;
  }

  bool empty_payload = !parse_bool_string(cmnd_param->valuestring);
  cJSON_Delete(cmnd_param);

  ha_entity_t sensor;
  build_ha_entity(&sensor, "tempreture", "sensor");

  char *payload = NULL;

  if (empty_payload) {
    payload = strdup("");
    has_sent_full_dev = false;
  } else {
    payload = cJSON_Print(sensor.ha_config_payload);
  }

  if (payload) {
    ESP_LOGI(TAG, "Topic: %s\n", sensor.ha_config_topic);
    ESP_LOGI(TAG, "Payload: %s\n", payload);

    esp_mqtt_client_publish(mqtt_client, sensor.ha_config_topic, payload, 0, 1,
                            0);

    cJSON_free(payload);
  }

  free_ha_entity(&sensor);
}
