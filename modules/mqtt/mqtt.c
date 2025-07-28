#include "sdkconfig.h"

#if CONFIG_MQTT_ENABLE
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_event_base.h"
#include "esp_log.h"

#include "cJSON.h"

#include "mqtt.h"
#include "net.h"
#include "platform_services.h"
#include "supervisor.h"

#if CONFIG_HOME_ASSISTANT_MQTT_DISCOVERY_ENABLE
#include "ha.h"
#endif

#define TAG "cikon-mqtt"
#define TOPIC_BUF_SIZE 128

static TaskHandle_t mqtt_command_task_handle, mqtt_telemetry_task_handle;

static esp_mqtt_client_handle_t mqtt_client;
static QueueHandle_t mqtt_queue;

static bool mqtt_skip_current_msg = false;

extern const uint8_t ca_crt_start[] asm("_binary_ca_crt_start");
extern const uint8_t cikonesp_crt_start[] asm("_binary_cikonesp_crt_start");
extern const uint8_t cikonesp_key_start[] asm("_binary_cikonesp_key_start");

static void mqtt_shutdown_task(void *args) {
    mqtt_shutdown();
    vTaskDelete(NULL);
}

void mqtt_publish_telemetry(void) {

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

void mqtt_publish_offline_state(void) {

    if (!(xEventGroupGetBits(app_event_group) & MQTT_CONNECTED_BIT)) {

        ESP_LOGW(TAG, "Client not connected, skipping offline state publish");
        return;
    }

    char aval_buf_topic[TOPIC_BUF_SIZE];
    MQTT_AVAILABILITY_TOPIC(aval_buf_topic);

    esp_mqtt_client_publish(mqtt_client, aval_buf_topic, "offline", 0, 1, true);

    EventBits_t bits = xEventGroupWaitBits(app_event_group, MQTT_OFFLINE_PUBLISHED_BIT, pdTRUE,
                                           pdFALSE, pdMS_TO_TICKS(1000));

    if (bits & MQTT_OFFLINE_PUBLISHED_BIT) {
        ESP_LOGI(TAG, "Offline message confirmed published.");
    } else {
        ESP_LOGW(TAG, "Timeout waiting for offline publish.");
    }
}

void mqtt_telemetry_task(void *args) {

    // Birth message
    {
        char topic[TOPIC_BUF_SIZE];

        MQTT_AVAILABILITY_TOPIC(topic);
        esp_mqtt_client_publish(mqtt_client, topic, "online", 0, MQTT_QOS, true);
    }

    while (!(xEventGroupGetBits(app_event_group) & MQTT_TASKS_SHUTDOWN_BIT)) {

        mqtt_publish_telemetry();

        EventBits_t bits = xEventGroupWaitBits(
            app_event_group, MQTT_TELEMETRY_TRIGGER_BIT | MQTT_TASKS_SHUTDOWN_BIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));

        if (bits & MQTT_TELEMETRY_TRIGGER_BIT) {
            xEventGroupClearBits(app_event_group, MQTT_TELEMETRY_TRIGGER_BIT);
        }

        if (bits & MQTT_TASKS_SHUTDOWN_BIT) {
            break;
        }
    }

    ESP_LOGE(TAG, "telemetry_task: exiting");
    xEventGroupClearBits(app_event_group, MQTT_TELEMETRY_TRIGGER_BIT);
    mqtt_telemetry_task_handle = NULL;
    vTaskDelete(NULL);
}

void mqtt_command_task(void *args) {

    // Command topic subscription
    {
        char topic[TOPIC_BUF_SIZE];
        MQTT_COMMAND_TOPIC(topic);

        if (esp_mqtt_client_subscribe(mqtt_client, topic, MQTT_QOS) < 0) {
            ESP_LOGE(TAG, "Unable to subscribe to MQTT topic '%s'", topic);
            vTaskDelete(NULL);
        }
    }

    char *msg = NULL; // ensure safe free() even if xQueueReceive fails

    while (!(xEventGroupGetBits(app_event_group) & MQTT_TASKS_SHUTDOWN_BIT)) {

        if (xQueueReceive(mqtt_queue, &msg, pdMS_TO_TICKS(100)) == pdFALSE)
            goto cleanup;

        char *topic = msg;
        char *payload = msg + strlen(topic) + 1;

        char commmand_topic[40];
        MQTT_COMMAND_TOPIC(commmand_topic);

        if (strcmp(topic, commmand_topic))
            goto cleanup;

        supervisor_process_command_payload(payload);
        xEventGroupSetBits(app_event_group, MQTT_TELEMETRY_TRIGGER_BIT);

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

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                               void *event_data) {

    static uint8_t mqtt_retry_counter = 0;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:

        mqtt_retry_counter = 0;
        xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);
        xEventGroupClearBits(app_event_group, MQTT_TASKS_SHUTDOWN_BIT);

        mqtt_queue = xQueueCreate(8, sizeof(void *));
        assert(mqtt_queue != NULL);

        uint8_t timeout = 100;
        while ((mqtt_command_task_handle != NULL || mqtt_telemetry_task_handle != NULL) &&
               timeout--) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        xTaskCreate(mqtt_command_task, "mqtt_command", CONFIG_MQTT_COMMAND_TASK_STACK_SIZE, NULL,
                    CONFIG_MQTT_COMMAND_TASK_PRIORITY, &mqtt_command_task_handle);

        xTaskCreate(mqtt_telemetry_task, "mqtt_telemetry", CONFIG_MQTT_TELEMETRY_TASK_STACK_SIZE,
                    NULL, CONFIG_MQTT_TELEMETRY_TASK_PRIORITY, &mqtt_telemetry_task_handle);

        ESP_LOGI(TAG, "Connected to MQTT Broker: %s", config_get()->mqtt_broker);

        break;
    case MQTT_EVENT_ERROR:
        xEventGroupClearBits(app_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:

        xEventGroupClearBits(app_event_group, MQTT_CONNECTED_BIT);
        xEventGroupSetBits(app_event_group, MQTT_TASKS_SHUTDOWN_BIT);

        if (mqtt_retry_counter < config_get()->mqtt_max_retry) {
            ESP_LOGW(TAG, "MQTT disconnected, retrying connection (%d/%d)", mqtt_retry_counter + 1,
                     config_get()->mqtt_max_retry);
            mqtt_retry_counter++;
        } else {
            ESP_LOGE(TAG,
                     "Failed to connect to MQTT Broker '%s' after %d retries, shutting down MQTT "
                     "subsystem...",
                     config_get()->mqtt_broker, config_get()->mqtt_max_retry);
            mqtt_retry_counter = 0;

            // We create a separate task for MQTT shutdown because ESP-IDF does not allow
            // esp_mqtt_client_stop() or esp_mqtt_client_destroy() to be called from the MQTT event
            // handler or any MQTT-related task. This avoids runtime errors and ensures proper
            // resource cleanup.
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
            !(xEventGroupGetBits(app_event_group) & MQTT_TASKS_SHUTDOWN_BIT)) {

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

    if (mqtt_client != NULL) {
        ESP_LOGW(TAG, "MQTT client already initialized, skipping re-initialization.");
        return;
    }

    if (is_network_connected() == false) {
        ESP_LOGE(TAG, "No network connection, cannot initialize MQTT client.");
        return;
    }

    ESP_LOGW(TAG, "Initializing MQTT client...");

    bool secure = config_get()->mqtt_mtls_en;

    static char avail_topic_buf[TOPIC_BUF_SIZE];
    MQTT_AVAILABILITY_TOPIC(avail_topic_buf);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker =
            {
                .address.uri = config_get()->mqtt_broker,
                .verification.certificate = secure ? (const char *)ca_crt_start : NULL,
            },
        .credentials =
            {
                .client_id = get_client_id(),
                .username = secure ? NULL : config_get()->mqtt_user,
                .authentication =
                    {
                        .password = secure ? NULL : config_get()->mqtt_pass,
                        .certificate = secure ? (const char *)cikonesp_crt_start : NULL,
                        .key = secure ? (const char *)cikonesp_key_start : NULL,
                    },
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

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_shutdown() {

    if (mqtt_client == NULL)
        return;

    xEventGroupSetBits(app_event_group, MQTT_TASKS_SHUTDOWN_BIT);

    ESP_ERROR_CHECK(esp_mqtt_client_stop(mqtt_client));
    ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
    mqtt_client = NULL;

    xEventGroupClearBits(app_event_group, MQTT_CONNECTED_BIT);
}

#endif // CONFIG_MQTT_ENABLE