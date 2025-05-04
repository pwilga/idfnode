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

void build_ha_entity(ha_entity_t *entity, const char *entity_type,
                     const char *name) {

  char *sanitized_name = sanitize(name);
  const size_t BUF_LEN = 128;

  char unique_id[64];
  snprintf(unique_id, sizeof(unique_id), "%.6s_%s", get_client_id(),
           sanitized_name);

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

  snprintf(buf, sizeof(buf), "{{ value_json.%s  }}", sanitized_name);
  cJSON_AddStringToObject(json_root, "val_tpl", buf);

  cJSON_AddItemToObject(json_root, "dev", build_ha_device());

  entity->ha_config_payload = json_root;

  free(sanitized_name);
}

void free_ha_entity(ha_entity_t *entity) {

  if (entity->ha_config_payload) {
    cJSON_Delete(entity->ha_config_payload);
    entity->ha_config_payload = NULL;
  }
}

// only temporary
extern esp_mqtt_client_handle_t mqtt_client;

/**
 * @brief Publishes a Home Assistant entity configuration via MQTT and frees its
 * resources.
 *
 * This function serializes the entity's JSON configuration payload (if present)
 * and publishes it to the associated MQTT topic using QoS 0. After publishing,
 * it frees the payload string and the internal resources held by the entity.
 *
 * @param entity Pointer to the ha_entity_t structure containing topic and
 * payload data.
 */
void submit_ha_entity(ha_entity_t *entity) {

  char *payload = NULL;

  if (entity->ha_config_payload)
    payload = cJSON_Print(entity->ha_config_payload);
  else
    payload = strdup("");

  ESP_LOGI(TAG, "Topic: %s", entity->ha_config_topic);
  ESP_LOGI(TAG, "Payload: %s", payload);

  esp_mqtt_client_publish(mqtt_client, entity->ha_config_topic, payload, 0, 0,
                          0);

  cJSON_free(payload);

  free_ha_entity(entity);
}

void register_ha_button(const char *name) {

  ha_entity_t entity;
  build_ha_entity(&entity, "button", name);

  if (entity.ha_config_payload) {

    char *sanitized_name = sanitize(name);
    char payload_command_buf[20];

    snprintf(payload_command_buf, sizeof(payload_command_buf), "{\"%s\": null}",
             sanitized_name);
    free(sanitized_name);

    cJSON_AddStringToObject(entity.ha_config_payload, "command_template",
                            payload_command_buf);
  }
  submit_ha_entity(&entity);
}

void register_ha_switch(const char *name) {

  ha_entity_t entity;
  build_ha_entity(&entity, "switch", name);

  if (entity.ha_config_payload) {

    char *sanitized_name = sanitize(name);
    char payload_onoff_buf[64];

    snprintf(payload_onoff_buf, sizeof(payload_onoff_buf), "{\"%s\":1}",
             sanitized_name);
    cJSON_AddStringToObject(entity.ha_config_payload, "payload_on",
                            payload_onoff_buf);

    snprintf(payload_onoff_buf, sizeof(payload_onoff_buf), "{\"%s\":0}",
             sanitized_name);
    cJSON_AddStringToObject(entity.ha_config_payload, "payload_off",
                            payload_onoff_buf);
    free(sanitized_name);

    cJSON_AddNumberToObject(entity.ha_config_payload, "state_on", 1);
    cJSON_AddNumberToObject(entity.ha_config_payload, "state_off", 0);
  }
  submit_ha_entity(&entity);
}

void register_ha_tasks_dict_sensor(const char *name) {

  ha_entity_t entity;
  build_ha_entity(&entity, "sensor", name);

  char *sanitized_name = sanitize(name);

  if (entity.ha_config_payload) {

    char val_buf[64];

    snprintf(val_buf, sizeof(val_buf), "{{ value_json.%s | count }}",
             sanitized_name);
    cJSON_ReplaceItemInObject(entity.ha_config_payload, "val_tpl",
                              cJSON_CreateString(val_buf));

    MQTT_TELEMETRY_TOPIC(val_buf);
    cJSON_AddStringToObject(entity.ha_config_payload, "json_attributes_topic",
                            val_buf);

    snprintf(val_buf, sizeof(val_buf), "{{ value_json.%s | tojson }}",
             sanitized_name);
    cJSON_AddStringToObject(entity.ha_config_payload,
                            "json_attributes_template", val_buf);
    free(sanitized_name);
  }
  submit_ha_entity(&entity);
}

void publish_ha_mqtt_discovery(void *args) {

  empty_payload = !parse_bool_json((cJSON *)args);

  ha_entity_t entity;

  build_ha_entity(&entity, "sensor", "Tempreture");
  cJSON_AddStringToObject(entity.ha_config_payload, "dev_cla", "temperature");
  submit_ha_entity(&entity);

  build_ha_entity(&entity, "sensor", "Uptime");
  cJSON_AddStringToObject(entity.ha_config_payload, "dev_cla", "duration");
  submit_ha_entity(&entity);

  register_ha_switch("Onboard Led");
  register_ha_button("Restart");
  register_ha_tasks_dict_sensor("Tasks Dict");
}

cJSON *create_tasks_dict_json(void) {

  UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
  TaskStatus_t *task_status_array = calloc(num_tasks, sizeof(TaskStatus_t));

  if (!task_status_array)
    return NULL;

  cJSON *task_dict = cJSON_CreateObject();
  if (!task_dict) {
    free(task_status_array);
    return NULL;
  }

  uint32_t total_runtime = 0;
  UBaseType_t real_task_count =
      uxTaskGetSystemState(task_status_array, num_tasks, &total_runtime);

  for (UBaseType_t i = 0; i < real_task_count; i++) {
    cJSON *json_task = cJSON_CreateObject();
    if (!json_task)
      continue;

    cJSON_AddNumberToObject(json_task, "prio",
                            task_status_array[i].uxCurrentPriority);
    cJSON_AddNumberToObject(json_task, "stack",
                            task_status_array[i].usStackHighWaterMark);
    cJSON_AddNumberToObject(json_task, "runtime_ticks",
                            task_status_array[i].ulRunTimeCounter);
    cJSON_AddNumberToObject(json_task, "task_number",
                            task_status_array[i].xTaskNumber);

    // Map eTaskState enum to human-readable string
    const char *state_str = "unknown";
    switch (task_status_array[i].eCurrentState) {
    case eRunning:
      state_str = "running";
      break;
    case eReady:
      state_str = "ready";
      break;
    case eBlocked:
      state_str = "blocked";
      break;
    case eSuspended:
      state_str = "suspended";
      break;
    case eDeleted:
      state_str = "deleted";
      break;
    default:
      break;
    }
    cJSON_AddStringToObject(json_task, "state", state_str);

#if (INCLUDE_xTaskGetAffinity == 1)
    cJSON_AddNumberToObject(json_task, "core", task_status_array[i].xCoreID);
#endif

    cJSON_AddItemToObject(task_dict, task_status_array[i].pcTaskName,
                          json_task);
  }

  free(task_status_array);
  return task_dict;
}
