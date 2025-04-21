#include "sdkconfig.h"

#if CONFIG_MQTT_ENABLE

#include "config.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "mqtt.h"

#include "cJSON.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"

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
    ESP_LOGI(TAG, "Connected to MQTT Broker: %s", "jasns");
    // return ESP_OK;
  } else if (bits & MQTT_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to MQTT Broker: %s", "jasns");
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }

  int msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_COMMAND_TOPIC, 1);
  ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

  xTaskCreate(command_task, "mqtt_task", 4096, NULL, 10,
              &mqtt_command_task_handle);
  xTaskCreate(telemetry_task, "telemetry_task", 4096, NULL, 5,
              &mqtt_telemetry_task_handle);
}

void mqtt_shutdown() {

  vTaskDelete(mqtt_command_task_handle);
  vTaskDelete(mqtt_telemetry_task_handle);
  vQueueDelete(mqtt_queue);

  mqtt_command_task_handle = NULL;
  mqtt_telemetry_task_handle = NULL;
  mqtt_queue = NULL;

  ESP_ERROR_CHECK(esp_mqtt_client_stop(mqtt_client));
  ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));

  mqtt_client = NULL;

  vTaskDelay(pdMS_TO_TICKS(100));
}

float random_float(float min, float max) {
  return min + ((float)esp_random() / UINT32_MAX) * (max - min);
}

static char *build_telemetry_json(void) {

  cJSON *json_root = cJSON_CreateObject();

  if (!json_root)
    return NULL;

  float t1 = random_float(20.5f, 25.9f);
  float t2 = random_float(50.5f, 80.9f);

  cJSON_AddNumberToObject(json_root, "t1", t1);
  cJSON_AddNumberToObject(json_root, "t2", t2);

  char *json_str = cJSON_PrintUnformatted(json_root);
  cJSON_Delete(json_root);

  return json_str;
}

void publish_telemetry_and_status(void) {

  char *payload = build_telemetry_json();
  if (!payload)
    return;

  esp_mqtt_client_publish(mqtt_client, MQTT_TELEMETRY_TOPIC, payload, 0, 1, 0);
  esp_mqtt_client_publish(mqtt_client, MQTT_AVAILABILITY_TOPIC, "online", 0, 1,
                          0);

  free(payload);
}

void telemetry_task(void *args) {
  while (1) {
    publish_telemetry_and_status();
    vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
  }
}

uint8_t parse_payload(const char *payload) {

  cJSON *json_root = cJSON_Parse(payload);

  if (!json_root) {
    ESP_LOGW(TAG, "Invalid JSON – rejecting message");
    return 0;
  }

  cJSON *led_status = cJSON_GetObjectItem(json_root, "led_status");

  if (!cJSON_IsString(led_status)) {
    ESP_LOGW(TAG, "Missing or invalid 'led_status' field in JSON");
    cJSON_Delete(json_root);
    return 0;
    // return LED_UNKNOWN;
  }
  return 1;
}

void command_task(void *args) {
  char *msg;

  while (1) {

    if (xQueueReceive(mqtt_queue, &msg, portMAX_DELAY) == pdFALSE)
      goto cleanup;

    char *topic = msg;
    char *payload = msg + strlen(topic) + 1;

    if (strcmp(topic, MQTT_COMMAND_TOPIC))
      goto cleanup;

    ESP_LOGI(TAG, "Matched topic: %s", topic);

    if (!parse_payload(payload))
      goto cleanup;

  cleanup:
    free(msg);
  }
}

/* Homeassistant MQTT Discovery */

static void get_mac_string(char *out, size_t len) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA); // ESP_MAC_BT, ESP_MAC_ETH
  snprintf(out, len, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
}

static cJSON *build_ha_device_info(const char *mac_str) {
  cJSON *device = cJSON_CreateObject();
  if (!device)
    return NULL;

  cJSON_AddStringToObject(device, "name", "Cikon ESP32 Node");

  cJSON *ids = cJSON_CreateArray();
  cJSON_AddItemToArray(ids, cJSON_CreateString(mac_str));
  cJSON_AddItemToObject(device, "ids", ids);

  cJSON_AddStringToObject(device, "mf", "Cikon Systems");
  cJSON_AddStringToObject(device, "mdl", "ESP32");
  cJSON_AddStringToObject(device, "sw", "1.0.0");

  return device;
}

void publish_ha_discovery_sensor(esp_mqtt_client_handle_t client) {

  char mac_str[13];
  get_mac_string(mac_str, sizeof(mac_str));

  // Stwórz discovery topic: np.
  // homeassistant/sensor/esp32_xxxxxx_temp/config
  char topic[128];
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/temperature/config",
           mac_str);

  // Tworzymy JSON
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", "Temperature");

  char uniq_id[64];
  // snprintf(uniq_id, sizeof(uniq_id), "%s", mac_str);
  strlcpy(uniq_id, mac_str, sizeof(uniq_id));

  cJSON_AddStringToObject(root, "uniq_id", uniq_id);

  cJSON_AddStringToObject(root, "stat_t", "esp32/sensors/temp");
  cJSON_AddStringToObject(root, "unit_of_meas", "°C");
  cJSON_AddStringToObject(root, "dev_cla", "temperature");
  cJSON_AddStringToObject(root, "val_tpl", "{{ value | float }}");

  cJSON *device = build_ha_device_info(mac_str);
  if (device) {
    cJSON_AddItemToObject(root, "device", device);
  }

  char *json_str = cJSON_PrintUnformatted(root);
  if (json_str) {
    esp_mqtt_client_publish(client, topic, json_str, 0, 1, true);
    ESP_LOGI(TAG, "Discovery sent: %s", topic);
    cJSON_free(json_str);
  }

  cJSON_Delete(root);
}

#endif // CONFIG_MQTT_ENABLE