#include "sdkconfig.h"

#if CONFIG_MQTT_ENABLE

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/queue.h"
#include "helpers.h"
#include "mqtt.h"

#define MAX_TASKS 20
#define TAG "mqtt-simple"

TaskHandle_t mqtt_command_task_handle, mqtt_telemetry_task_handle;
esp_mqtt_client_handle_t mqtt_client;
QueueHandle_t mqtt_queue;

static bool mqtt_skip_current_msg = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);
    break;
  case MQTT_EVENT_DISCONNECTED:
    xEventGroupSetBits(app_event_group, MQTT_FAIL_BIT);
    break;
  case MQTT_EVENT_DATA:

    esp_mqtt_event_handle_t event = event_data;

    if (event->current_data_offset == 0) {

      mqtt_skip_current_msg = false;

      if (event->total_data_len > MQTT_RX_BUFFER_SIZE - 14) {
        ESP_LOGW(TAG, "Skipping oversized payload (%d > %d)",
                 event->total_data_len, MQTT_RX_BUFFER_SIZE - 14);
        mqtt_skip_current_msg = true;
        break;
      }
    }

    if (mqtt_skip_current_msg) {
      ESP_LOGW(TAG, "Ignoring fragment at offset: %d (message skipped)",
               event->current_data_offset);
      break;
    }

    const char *topic_ptr = event->topic;
    const char *payload_ptr = event->data;
    int topic_len = event->topic_len;
    int payload_len = event->data_len;

    // Calculate: topic\0payload\0
    size_t message_len = topic_len + 1 + payload_len + 1;
    char *message_buf = malloc(message_len);

    if (!message_buf) {
      ESP_LOGE(TAG, "Memory allocation failed — unable to allocate buffer");
      break;
    }

    // topic\0payload\0
    memcpy(message_buf, topic_ptr, topic_len);
    message_buf[topic_len] = '\0';

    memcpy(message_buf + topic_len + 1, payload_ptr, payload_len);
    message_buf[message_len - 1] = '\0';

    if (xQueueSend(mqtt_queue, &message_buf, portMAX_DELAY) != pdTRUE) {
      ESP_LOGW(TAG, "Queue limit exceeded — message not enqueued");
      free(message_buf);
    }

    break;
  default:
    break;
  }
}

void mqtt_init() {

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker =
          {
              .address.uri = CONFIG_MQTT_BROKER_URI,
          },
      .credentials =
          {
              .username = CONFIG_MQTT_USERNAME,
              .authentication.password = CONFIG_MQTT_PASSWORD,
          },
      .buffer.size = MQTT_RX_BUFFER_SIZE,
  };

  mqtt_queue = xQueueCreate(8, sizeof(void *));
  assert(mqtt_queue != NULL);

  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                 mqtt_event_handler, mqtt_client);
  esp_mqtt_client_start(mqtt_client);

  EventBits_t bits =
      xEventGroupWaitBits(app_event_group, MQTT_CONNECTED_BIT | MQTT_FAIL_BIT,
                          pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & MQTT_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to MQTT Broker: %s", CONFIG_MQTT_BROKER_URI);
  } else if (bits & MQTT_FAIL_BIT) {
    ESP_LOGW(TAG, "Failed to connect to MQTT Broker: %s",
             CONFIG_MQTT_BROKER_URI);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
  char command_topic[40];
  MQTT_COMMAND_TOPIC(command_topic);

  int msg_id = esp_mqtt_client_subscribe(mqtt_client, command_topic, 1);

  // ESP_LOGW(TAG, "sent subscribe successful, msg_id=%d", msg_id);

  xTaskCreate(command_task, "mqtt_command", 4096, NULL, 10,
              &mqtt_command_task_handle);
  xTaskCreate(telemetry_task, "mqtt_telemetry", 4096, NULL, 5,
              &mqtt_telemetry_task_handle);
}

void mqtt_shutdown() {

  vTaskDelete(mqtt_command_task_handle);
  mqtt_command_task_handle = NULL;

  vTaskDelete(mqtt_telemetry_task_handle);
  mqtt_telemetry_task_handle = NULL;

  vQueueDelete(mqtt_queue);
  mqtt_queue = NULL;

  ESP_ERROR_CHECK(esp_mqtt_client_stop(mqtt_client));
  ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
  mqtt_client = NULL;

  vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * @brief Builds a JSON array of all current FreeRTOS tasks.
 *
 * Each element in the array contains:
 * - name: task name
 * - prio: task priority
 * - stack: remaining stack space (high water mark)
 *
 * @return cJSON* Pointer to a JSON array. Must be freed using cJSON_Delete()
 *         or attached to another cJSON object via cJSON_AddItemToObject().
 *         Returns NULL on error.
 */
cJSON *create_task_array_json(void) {

  TaskStatus_t task_status_array[MAX_TASKS];
  UBaseType_t num_tasks =
      uxTaskGetSystemState(task_status_array, MAX_TASKS, NULL);

  cJSON *task_array = cJSON_CreateArray();
  if (!task_array)
    return NULL;

  for (int i = 0; i < num_tasks; i++) {
    cJSON *json_array_item = cJSON_CreateObject();
    if (!json_array_item)
      continue;

    cJSON_AddStringToObject(json_array_item, "name",
                            task_status_array[i].pcTaskName);
    cJSON_AddNumberToObject(json_array_item, "prio",
                            task_status_array[i].uxCurrentPriority);
    cJSON_AddNumberToObject(json_array_item, "stack",
                            task_status_array[i].usStackHighWaterMark);

    cJSON_AddItemToArray(task_array, json_array_item);
  }

  return task_array;
}

float random_float(float min, float max) {
  return min + ((float)esp_random() / UINT32_MAX) * (max - min);
}

char *build_telemetry_json(void) {

  cJSON *json_root = cJSON_CreateObject();

  if (!json_root)
    return NULL;

  float t1 = random_float(20.5f, 25.9f);
  float t2 = random_float(50.5f, 80.9f);

  cJSON *tasks = create_task_array_json();

  cJSON_AddNumberToObject(json_root, "tempreture", t1);
  cJSON_AddNumberToObject(json_root, "t2", t2);

  cJSON_AddItemToObject(json_root, "task_list", tasks);

  char *json_str = cJSON_PrintUnformatted(json_root);

  // cJSON_Delete(tasks);
  cJSON_Delete(json_root);

  return json_str;
}

void publish_telemetry(void) {

  char *payload = build_telemetry_json();
  if (!payload)
    return;

  char topic[40];

  MQTT_TELEMETRY_TOPIC(topic);
  esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);

  MQTT_AVAILABILITY_TOPIC(topic);
  esp_mqtt_client_publish(mqtt_client, topic, "online", 0, 1, 0);

  free(payload);
}

void telemetry_task(void *args) {
  while (1) {
    publish_telemetry();
    vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
  }
}

uint8_t parse_payload(const char *payload) {

  cJSON *json_root = cJSON_Parse(payload);

  if (!json_root) {
    ESP_LOGW(TAG, "Invalid JSON: Rejecting message.");
    cJSON_Delete(json_root);
    return 0;
  }

  cJSON *json_item = json_root->child;

  while (json_item) {
    if (json_item->string) {

      char *value_str = cJSON_PrintUnformatted(json_item);

      if (value_str) {
        ESP_LOGI(TAG, "Key: %s, Value: %s", json_item->string, value_str);
        dispatch_command(json_item->string, value_str);
        free(value_str);
      }
    }
    json_item = json_item->next;
  }

  cJSON_Delete(json_root);

  return 1;
}

void command_task(void *args) {

  char *msg = NULL; // ensure safe free() even if xQueueReceive fails

  while (1) {

    if (xQueueReceive(mqtt_queue, &msg, portMAX_DELAY) == pdFALSE)
      goto cleanup;

    char *topic = msg;
    char *payload = msg + strlen(topic) + 1;

    char commmand_topic[40];
    MQTT_COMMAND_TOPIC(commmand_topic);

    if (strcmp(topic, commmand_topic))
      goto cleanup;

    // ESP_LOGI(TAG, "Matched topic: %s", topic);

    if (!parse_payload(payload))
      goto cleanup;

  cleanup:
    free(msg);
  }
}

#endif // CONFIG_MQTT_ENABLE