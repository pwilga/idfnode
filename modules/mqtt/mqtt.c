#include "sdkconfig.h"

#if CONFIG_MQTT_ENABLE
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_event_base.h"
#include "esp_log.h"
#include "esp_random.h"
// #include "esp_timer.h"
// #include "driver/gpio.h"

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

static bool mqtt_skip_current_msg = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                               void *event_data) {
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);
        xEventGroupClearBits(app_event_group, MQTT_FAIL_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupSetBits(app_event_group, MQTT_FAIL_BIT);
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

    EventBits_t bits = xEventGroupWaitBits(app_event_group, MQTT_CONNECTED_BIT | MQTT_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & MQTT_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to MQTT Broker: %s", CONFIG_MQTT_BROKER_URI);
    } else if (bits & MQTT_FAIL_BIT) {
        ESP_LOGW(TAG, "Failed to connect to MQTT Broker: %s", CONFIG_MQTT_BROKER_URI);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    char topic[TOPIC_BUF_SIZE];
    MQTT_COMMAND_TOPIC(topic);

    if (esp_mqtt_client_subscribe(mqtt_client, topic, MQTT_QOS) < 0) {
        ESP_LOGE(TAG, "Unable to subscribe to MQTT topic '%s'", topic);
    }

    // Birth message
    MQTT_AVAILABILITY_TOPIC(topic);
    esp_mqtt_client_publish(mqtt_client, topic, "online", 0, MQTT_QOS, true);

    xTaskCreate(command_task, "mqtt_command", 4096, NULL, 10, &mqtt_command_task_handle);
    xTaskCreate(telemetry_task, "mqtt_telemetry", 4096, NULL, 5, &mqtt_telemetry_task_handle);
}

void mqtt_shutdown(void *args) {

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

    if (mqtt_client != NULL) {
        esp_mqtt_client_publish(mqtt_client, aval_buf_topic, "offline", 0, 1, true);
    }
    EventBits_t bits = xEventGroupWaitBits(app_event_group, MQTT_OFFLINE_PUBLISHED_BIT, pdTRUE,
                                           pdFALSE, pdMS_TO_TICKS(2000));

    if (bits & MQTT_OFFLINE_PUBLISHED_BIT) {
        ESP_LOGI(TAG, "Offline message confirmed published.");
    } else {
        ESP_LOGW(TAG, "Timeout waiting for offline publish.");
    }

    ESP_ERROR_CHECK(esp_mqtt_client_stop(mqtt_client));
    ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
    mqtt_client = NULL;

    xEventGroupClearBits(app_event_group,
                         MQTT_CONNECTED_BIT | MQTT_FAIL_BIT | MQTT_SHUTDOWN_INITIATED_BIT);
    // xEventGroupSetBits(app_event_group, MQTT_SHUTDOWN_DONE);

    vTaskDelay(pdMS_TO_TICKS(100));
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

#if CONFIG_HOME_ASSISTANT_MQTT_DISCOVERY_ENABLE
    cJSON *tasks = create_tasks_dict_json();
#endif

    cJSON_AddNumberToObject(json_root, "tempreture", t1);
    cJSON_AddNumberToObject(json_root, "onboard_led", get_onboard_led_state());
    cJSON_AddNumberToObject(json_root, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddStringToObject(json_root, "startup", get_boot_time());
#if CONFIG_HOME_ASSISTANT_MQTT_DISCOVERY_ENABLE
    cJSON_AddItemToObject(json_root, "tasks_dict", tasks);
#endif
    char *json_str = cJSON_PrintUnformatted(json_root);

    // cJSON_Delete(tasks);
    cJSON_Delete(json_root);

    return json_str;
}

void publish_telemetry(void) {

    char *payload = build_telemetry_json();
    if (!payload)
        return;

    char topic[TOPIC_BUF_SIZE];

    MQTT_TELEMETRY_TOPIC(topic);

    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, MQTT_QOS, false);

    free(payload);
}

void telemetry_task(void *args) {

    const int check_delay_ms = 10;
    int telemetry_timer = 0;

    while (!(xEventGroupGetBits(app_event_group) & MQTT_SHUTDOWN_INITIATED_BIT)) {
        if (telemetry_timer <= 0) {
            publish_telemetry();
            telemetry_timer = TELEMETRY_INTERVAL_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(check_delay_ms));
        telemetry_timer -= check_delay_ms;
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

        if (cmd_type == CMD_UNKNOWN) {
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

        // ESP_LOGI(TAG, "Matched topic: %s", topic);

        process_command_payload(payload);

        // if (!parse_payload(payload))
        //     goto cleanup;

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

#endif // CONFIG_MQTT_ENABLE