#include "sdkconfig.h"

#if CONFIG_MQTT_ENABLE
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_event_base.h"
#include "esp_log.h"

#include "cJSON.h"

#include "mqtt.h"
#include "platform_services.h"
#include "supervisor.h"

#if CONFIG_HOME_ASSISTANT_MQTT_DISCOVERY_ENABLE
#include "ha.h"
#endif

#define TAG "cikon-mqtt"
#define TOPIC_BUF_SIZE 128

TaskHandle_t mqtt_command_task_handle, mqtt_telemetry_task_handle;

esp_mqtt_client_handle_t mqtt_client;
QueueHandle_t mqtt_queue;

static uint8_t mqtt_retry_counter = 0;
static bool mqtt_skip_current_msg = false;

static void mqtt_shutdown_task(void *args) {
    mqtt_shutdown();
    vTaskDelete(NULL);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                               void *event_data) {
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);
        xEventGroupClearBits(app_event_group, MQTT_FAIL_BIT);

        ESP_LOGI(TAG, "Connected to MQTT Broker: %s", CONFIG_MQTT_BROKER_URI);

        char topic[TOPIC_BUF_SIZE];
        MQTT_COMMAND_TOPIC(topic);

        if (esp_mqtt_client_subscribe(mqtt_client, topic, MQTT_QOS) < 0) {
            ESP_LOGE(TAG, "Unable to subscribe to MQTT topic '%s'", topic);
        }

        // Birth message
        MQTT_AVAILABILITY_TOPIC(topic);
        esp_mqtt_client_publish(mqtt_client, topic, "online", 0, MQTT_QOS, true);
        break;
    case MQTT_EVENT_ERROR:
        xEventGroupSetBits(app_event_group, MQTT_FAIL_BIT);
        xEventGroupClearBits(app_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:

        xEventGroupSetBits(app_event_group, MQTT_FAIL_BIT);
        xEventGroupClearBits(app_event_group, MQTT_CONNECTED_BIT);

        if (mqtt_retry_counter < CONFIG_MQTT_MAXIMUM_RETRY) {
            mqtt_retry_counter++;
            ESP_LOGI(TAG, "Retry to connect to the MQTT Broker (%d/%d)", mqtt_retry_counter,
                     CONFIG_MQTT_MAXIMUM_RETRY);
        } else {
            ESP_LOGE(TAG,
                     "Failed to connect to MQTT Broker '%s' after %d retries, shutting down MQTT "
                     "subsystem...",
                     CONFIG_MQTT_BROKER_URI, CONFIG_MQTT_MAXIMUM_RETRY);

            // From this context, starting a new task is the only way to destroy MQTT.
            xTaskCreate(mqtt_shutdown_task, "mqtt_shutdown", 2048, NULL, 10, NULL);
        }
        break;
    case MQTT_EVENT_DATA:

        esp_mqtt_event_handle_t event = event_data;

        if (event->current_data_offset == 0) {
            mqtt_skip_current_msg = false;

            if (event->total_data_len > MQTT_RX_BUFFER_SIZE - 14) {
                ESP_LOGW(TAG, "Skipping oversized payload (%d > %d)", event->total_data_len,
                         MQTT_RX_BUFFER_SIZE - 14);
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

        if (mqtt_queue != NULL &&
            !(xEventGroupGetBits(app_event_group) & MQTT_SHUTDOWN_INITIATED_BIT)) {

            if (xQueueSend(mqtt_queue, &message_buf, portMAX_DELAY) != pdTRUE) {
                ESP_LOGW(TAG, "Queue limit exceeded — message not enqueued");
                free(message_buf);
            }
        }

        break;
    case MQTT_EVENT_PUBLISHED:
        xEventGroupSetBits(app_event_group, MQTT_OFFLINE_PUBLISHED_BIT);
        break;
    default:
        break;
    }
}

void mqtt_init() {

    static char avail_topic_buf[TOPIC_BUF_SIZE];

    MQTT_AVAILABILITY_TOPIC(avail_topic_buf);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker =
            {
                .address.uri = CONFIG_MQTT_BROKER_URI,
            },
        .credentials =
            {
                .client_id = get_client_id(),
                .username = CONFIG_MQTT_USERNAME,
                .authentication.password = CONFIG_MQTT_PASSWORD,
            },
        .buffer.size = MQTT_RX_BUFFER_SIZE,
        .session =
            {
                .keepalive = 15,
                .last_will =
                    {
                        .topic = avail_topic_buf,
                        .msg = "offline",
                        .qos = 0,
                        .retain = true,
                    },
            },
    };

    mqtt_queue = xQueueCreate(8, sizeof(void *));
    assert(mqtt_queue != NULL);

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);

    xTaskCreate(command_task, "mqtt_command", 4096, NULL, 10, &mqtt_command_task_handle);
    xTaskCreate(telemetry_task, "mqtt_telemetry", 4096, NULL, 5, &mqtt_telemetry_task_handle);
}

void mqtt_shutdown() {

    xEventGroupSetBits(app_event_group, MQTT_SHUTDOWN_INITIATED_BIT);

    int timeout = 100;
    while ((mqtt_command_task_handle != NULL || mqtt_telemetry_task_handle != NULL) && timeout--) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (mqtt_command_task_handle != NULL && mqtt_telemetry_task_handle != NULL) {
        ESP_LOGW(TAG, "Timeout waiting for both command and telemetry tasks to finish!");
    } else {
        ESP_LOGI(TAG, "All tasks finished cleanly.");
    }

    char aval_buf_topic[TOPIC_BUF_SIZE];
    MQTT_AVAILABILITY_TOPIC(aval_buf_topic);

    if (mqtt_client != NULL && !(xEventGroupGetBits(app_event_group) & MQTT_FAIL_BIT)) {
        esp_mqtt_client_publish(mqtt_client, aval_buf_topic, "offline", 0, 1, true);

        EventBits_t bits = xEventGroupWaitBits(app_event_group, MQTT_OFFLINE_PUBLISHED_BIT, pdTRUE,
                                               pdFALSE, pdMS_TO_TICKS(2000));

        if (bits & MQTT_OFFLINE_PUBLISHED_BIT) {
            ESP_LOGI(TAG, "Offline message confirmed published.");
        } else {
            ESP_LOGW(TAG, "Timeout waiting for offline publish.");
        }
    }

    if (mqtt_client != NULL) {
        ESP_ERROR_CHECK(esp_mqtt_client_stop(mqtt_client));
        ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
        mqtt_client = NULL;
    }

    xEventGroupClearBits(app_event_group,
                         MQTT_CONNECTED_BIT | MQTT_FAIL_BIT | MQTT_SHUTDOWN_INITIATED_BIT);

    vTaskDelay(pdMS_TO_TICKS(100));
}

void publish_telemetry(void) {

    if (!(xEventGroupGetBits(app_event_group) & MQTT_CONNECTED_BIT)) {
        // Prevent console spam on repeated connection failures.
        static int not_connected_counter = 0;
        not_connected_counter++;
        if (not_connected_counter >= 5) {
            ESP_LOGW(TAG, "MQTT not connected, skipping telemetry publish");
            not_connected_counter = 0;
        }
        return;
    }

    cJSON *json = cJSON_CreateObject();
    supervisor_state_to_json(json);

    char *json_str = cJSON_PrintUnformatted(json);

    char topic[TOPIC_BUF_SIZE];
    MQTT_TELEMETRY_TOPIC(topic);

    esp_mqtt_client_publish(mqtt_client, topic, json_str, 0, MQTT_QOS, false);

    free(json_str);
    cJSON_Delete(json);
}

void telemetry_task(void *args) {

    while (!(xEventGroupGetBits(app_event_group) & MQTT_SHUTDOWN_INITIATED_BIT)) {

        publish_telemetry();

        EventBits_t bits = xEventGroupWaitBits(
            app_event_group, TELEMETRY_TRIGGER_BIT | MQTT_SHUTDOWN_INITIATED_BIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));

        if (bits & TELEMETRY_TRIGGER_BIT) {
            xEventGroupClearBits(app_event_group, TELEMETRY_TRIGGER_BIT);
        }

        if (bits & MQTT_SHUTDOWN_INITIATED_BIT) {
            break;
        }
    }

    ESP_LOGE(TAG, "telemetry_task: exiting");
    mqtt_telemetry_task_handle = NULL;
    vTaskDelete(NULL);
}

void process_command_payload(const char *payload) {

    cJSON *json_root = cJSON_Parse(payload);

    if (!json_root || !cJSON_IsObject(json_root)) {
        ESP_LOGW(TAG, "Invalid JSON: Rejecting message.");
        cJSON_Delete(json_root);
        return;
    }

    for (cJSON *item = json_root->child; item != NULL; item = item->next) {
        if (!item->string) {
            continue;
        }

        supervisor_command_type_t cmd_type = supervisor_command_from_id(item->string);

        if (cmd_type == CMND_UNKNOWN) {
            ESP_LOGW(TAG, "Unknown command: %s", item->string);
            continue;
        }

        const char *desc = supervisor_command_description(cmd_type);

        ESP_LOGI(TAG, "Dispatching command: %s - %s", item->string, desc);

        supervisor_command_t cmd = {
            .type = cmd_type,
        };

        char *json_val = cJSON_PrintUnformatted(item);
        snprintf(cmd.args_json_str, sizeof(cmd.args_json_str), "%s", json_val);
        free(json_val);

        supervisor_schedule_command(&cmd);
        publish_telemetry();
    }

    cJSON_Delete(json_root);
}

void command_task(void *args) {

    char *msg = NULL; // ensure safe free() even if xQueueReceive fails

    while (!(xEventGroupGetBits(app_event_group) & MQTT_SHUTDOWN_INITIATED_BIT)) {

        if (xQueueReceive(mqtt_queue, &msg, pdMS_TO_TICKS(100)) == pdFALSE)
            goto cleanup;

        char *topic = msg;
        char *payload = msg + strlen(topic) + 1;

        char commmand_topic[40];
        MQTT_COMMAND_TOPIC(commmand_topic);

        if (strcmp(topic, commmand_topic))
            goto cleanup;

        process_command_payload(payload);

    cleanup:
        if (msg) {
            free(msg);
            msg = NULL;
        }
    }

    ESP_LOGE(TAG, "command_task: exiting and cleaning mqtt_queue");

    if (mqtt_queue) {
        vQueueDelete(mqtt_queue);
        mqtt_queue = NULL;
    }

    mqtt_command_task_handle = NULL;
    vTaskDelete(NULL);
}

void mqtt_publish(const char *topic, const char *payload, int qos, bool retain) {
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, qos, retain);
}

#endif // CONFIG_MQTT_ENABLE