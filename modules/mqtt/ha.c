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
static bool empty_payload = false;

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
cJSON *build_ha_device(void) {

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

/**
 * @brief Builds a generic Home Assistant (HA) entity configuration for MQTT
 * Discovery.
 *
 * This function prepares a generic HA entity by:
 * - Constructing the appropriate MQTT discovery topic based on the entity name
 * and type.
 * - Creating the JSON payload with required fields for Home Assistant
 * auto-discovery.
 *
 * The entity structure (ha_entity_t) will be populated with the generated topic
 * and payload.
 *
 * @param entity Pointer to the ha_entity_t structure where the generated topic
 * and payload will be stored.
 * @param name Name of the entity (e.g., "living_room_light" or
 * "sensor_temperature").
 * @param entity_type Type of the entity (e.g., "sensor", "switch",
 * "binary_sensor").
 */
void build_ha_entity(ha_entity_t *entity, const char *entity_type,
                     const char *name) {

  const size_t BUF_LEN = 128;

  char unique_id[64];
  snprintf(unique_id, sizeof(unique_id), "%.6s_%s", get_client_id(), name);

  snprintf(entity->ha_config_topic, sizeof(entity->ha_config_topic),
           "%s/%s/%s/config", MQTT_DISCOVERY_PREFIX, entity_type, unique_id);

  if (empty_payload) {
    entity->ha_config_payload = NULL;
    has_sent_full_dev = false;
    return;
  }

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

  cJSON_AddItemToObject(json_root, "dev", build_ha_device());

  entity->ha_config_payload = json_root;
}

/**
 * @brief Frees the dynamically allocated Home Assistant entity configuration
 * payload.
 */
void free_ha_entity(ha_entity_t *entity) {

  if (entity->ha_config_payload) {
    cJSON_Delete(entity->ha_config_payload);
    entity->ha_config_payload = NULL;
  }
}

// only temporary
extern esp_mqtt_client_handle_t mqtt_client;

void publish_ha_entity(ha_entity_t *entity) {

  char *payload = NULL;

  if (entity->ha_config_payload)
    payload = cJSON_Print(entity->ha_config_payload);
  else
    payload = strdup("");

  ESP_LOGI(TAG, "Topic: %s", entity->ha_config_topic);
  ESP_LOGI(TAG, "Payload: %s", payload);

  esp_mqtt_client_publish(mqtt_client, entity->ha_config_topic, payload, 0, 1,
                          0);

  cJSON_free(payload);
}

void build_ha_button(ha_entity_t *entity, const char *name) {

  build_ha_entity(entity, "button", name);

  char payload_command_buf[20];
  snprintf(payload_command_buf, sizeof(payload_command_buf), "{\"%s\": null}",
           name);

  cJSON_AddStringToObject(entity->ha_config_payload, "command_template",
                          payload_command_buf);
}

void build_ha_switch(ha_entity_t *entity, const char *name) {

  build_ha_entity(entity, "switch", name);

  if (!entity->ha_config_payload)
    return;

  const uint8_t payload_buf_size = 64;
  char payload_on_buf[payload_buf_size], payload_off_buf[payload_buf_size];

  snprintf(payload_on_buf, sizeof(payload_on_buf), "{\"%s\":1}", name);
  snprintf(payload_off_buf, sizeof(payload_off_buf), "{\"%s\":0}", name);

  cJSON_AddStringToObject(entity->ha_config_payload, "payload_on",
                          payload_on_buf);
  cJSON_AddStringToObject(entity->ha_config_payload, "payload_off",
                          payload_off_buf);
  cJSON_AddNumberToObject(entity->ha_config_payload, "state_on", 1);
  cJSON_AddNumberToObject(entity->ha_config_payload, "state_off", 0);
}

// typedef struct {
//   const char *key;
//   const char *value;
// } KeyValue;

// KeyValue attrs[] = {
//     {"dev_cla", "temperature"},
//     {"unit_of_measurement", "°C"},
//     {"friendly_name", "Living Room Temp"},
// };

// void ha_entity_add_attributes(ha_entity_t *entity, const KeyValue *attrs,
//                               size_t count) {
//   for (size_t i = 0; i < count; ++i) {
//     ESP_LOGW(TAG, "%s: %s", attrs[i].key, attrs[i].value);
//     // cJSON_AddStringToObject(entity->json, attrs[i].key, attrs[i].value);
//   }
// }

void publish_ha_mqtt_discovery(void *args) {

  empty_payload = !parse_bool_json((cJSON *)args);

  ha_entity_t entity;

  build_ha_entity(&entity, "sensor", "tempreture");
  cJSON_AddStringToObject(entity.ha_config_payload, "dev_cla", "temperature");
  publish_ha_entity(&entity);
  free_ha_entity(&entity);

  build_ha_entity(&entity, "sensor", "uptime");
  cJSON_AddStringToObject(entity.ha_config_payload, "dev_cla", "duration");
  publish_ha_entity(&entity);
  free_ha_entity(&entity);

  build_ha_switch(&entity, "onboard_led");
  publish_ha_entity(&entity);
  free_ha_entity(&entity);

  build_ha_button(&entity, "restart");
  publish_ha_entity(&entity);
  free_ha_entity(&entity);
}
