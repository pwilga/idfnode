#include "esp_log.h"
#include "esp_random.h"

#include "cJSON.h"

#include "config_manager.h"
#include "debug.h"
#include "ha.h"
#include "https_server.h"
#include "json_parser.h"
#include "ota.h"
#include "supervisor.h"
#include "platform_services.h"
#include "udp_monitor.h"
#include "wifi.h"

#if CONFIG_MQTT_ENABLE
#include "mqtt.h"
#endif

const char *TAG = "cikon-supervisor";

typedef enum {
    SUPERVISOR_INTERVAL_1S,
    SUPERVISOR_INTERVAL_5S,
    SUPERVISOR_INTERVAL_60S,
    SUPERVISOR_INTERVAL_2H,
    SUPERVISOR_INTERVAL_12H,
    SUPERVISOR_INTERVAL_COUNT
} supervisor_interval_stage_t;

static const uint32_t supervisor_intervals_ms[SUPERVISOR_INTERVAL_COUNT] = {
    [SUPERVISOR_INTERVAL_1S] = 1000,
    [SUPERVISOR_INTERVAL_5S] = 5000,
    [SUPERVISOR_INTERVAL_60S] = 60000,
    [SUPERVISOR_INTERVAL_2H] = 2 * 60 * 60 * 1000,
    [SUPERVISOR_INTERVAL_12H] = 12 * 60 * 60 * 1000};

// static supervisor_state_t state;
static supervisor_state_t state = {
#define TELE(name, generic_type, telemetry_field_type_t, default_val) .name = default_val,
    TELE_LIST
#undef TELE
};

const supervisor_state_t *supervisor_get_state(void) { return &state; }

const char *supervisor_command_id(supervisor_command_type_t cmd) {
    switch (cmd) {
#define CMND(enum_id, id_str, desc)                                                                \
    case enum_id:                                                                                  \
        return id_str;
        CMND_LIST
#undef CMND
    default:
        return "<unknown>";
    }
}

const char *supervisor_command_description(supervisor_command_type_t cmd) {
    switch (cmd) {
#define CMND(enum_id, id_str, desc)                                                                \
    case enum_id:                                                                                  \
        return desc;
        CMND_LIST
#undef CMND
    default:
        return "<unknown>";
    }
}

supervisor_command_type_t supervisor_command_from_id(const char *id) {
#define CMND(enum_id, id_str, desc)                                                                \
    if (strcmp(id, id_str) == 0)                                                                   \
        return enum_id;
    CMND_LIST
#undef CMND
    return CMND_UNKNOWN;
}

bool supervisor_schedule_command(supervisor_command_t *cmd) {

    if (!supervisor_queue)
        return false;

    if (xQueueSend(supervisor_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "supervisor_schedule_command: queue full, freeing cmd=%p args_json_str=%p",
                 cmd, cmd->args_json_str);
        if (cmd->args_json_str)
            free(cmd->args_json_str);
        free(cmd);
        return false;
    }
    return true;
}

void command_dispatch(supervisor_command_t *cmd) {

    const char *TAG = "supervisor-command-dispatcher";

    switch (cmd->type) {
    case CMND_HA_DISCOVERY:
        logic_state_t force_empty_payload = json_str_as_logic_state(cmd->args_json_str);
        if (force_empty_payload == STATE_TOGGLE) {
            ESP_LOGE(TAG, "Toggling is not permitted for this module.");
            return;
        }
        publish_ha_mqtt_discovery(force_empty_payload == STATE_OFF);
        break;
    case CMND_RESTART:
        esp_safe_restart();
        break;

    case CMND_SET_MODE:

        break;

    case CMND_LOG_STATUS:
        ESP_LOGI(TAG, "Logic state: %d", json_str_as_logic_state(cmd->args_json_str));
        ESP_LOGI(TAG, "System running, heap: %u kB", esp_get_free_heap_size() / 1024);
        break;

    case CMND_LED_SET:
        logic_state_t state = json_str_as_logic_state(cmd->args_json_str);
        onboard_led_set_state(state);
        break;

    case CMND_SET_AP:
        mqtt_shutdown();
        wifi_ensure_ap_mode();

        break;

    case CMND_HELP:
        supervisor_command_print_all();
        break;

    case CMND_SET_CONF:
        cJSON *json_args = json_str_as_object(cmd->args_json_str);
        if (!json_args) {
            ESP_LOGW(TAG, "Command aborted: invalid JSON arguments: %s", cmd->args_json_str);
            return;
        }
        config_manager_set_from_json(json_args);

        // char *json_str = cJSON_Print(json_args);
        // ESP_LOGI(TAG, "Setting configuration from JSON: %s", json_str);
        // free(json_str);

        cJSON_Delete(json_args);

        break;
    case CMND_RESET_CONF:
        reset_nvs_partition();
        esp_safe_restart();
        break;
    case CMND_HTTPS:
        logic_state_t https_state = json_str_as_logic_state(cmd->args_json_str);

        if (https_state == STATE_ON) {
            https_init();
        } else if (https_state == STATE_OFF) {
            https_shutdown();
        }
        break;
    default:
        ESP_LOGW(TAG, "Unknown command type: %s", supervisor_command_id(cmd->type));
        break;
    }
}

float random_float(float min, float max) {
    return min + ((float)esp_random() / UINT32_MAX) * (max - min);
}

static void supervisor_execute_stage(supervisor_interval_stage_t stage) {

    switch (stage) {
    case SUPERVISOR_INTERVAL_1S:
        state.uptime = esp_timer_get_time() / 1000000ULL;
        break;

    case SUPERVISOR_INTERVAL_5S:
        state.tempreture = random_float(20.5f, 25.9f);

        break;

    case SUPERVISOR_INTERVAL_60S:
        // np. RSSI, heap, NTP sync
        break;
    case SUPERVISOR_INTERVAL_2H:
        // kod co 2h
        break;
    case SUPERVISOR_INTERVAL_12H:
        // kod co 12h
        break;
    default:
        break;
    }
}

void supervisor_task(void *args) {

    supervisor_init();
    ESP_LOGI(TAG, "Supervisor task started.");

    TickType_t last_stage[SUPERVISOR_INTERVAL_COUNT];
    for (int i = 0; i < SUPERVISOR_INTERVAL_COUNT; ++i)
        last_stage[i] = xTaskGetTickCount();

    supervisor_command_t *cmd;

    // SUPERVISOR_INIT_ONLY, no need to update this field later
    snprintf(state.startup, sizeof(state.startup), "%s", get_boot_time());

    while (1) {
        if (xQueueReceive(supervisor_queue, &cmd, pdMS_TO_TICKS(100))) {
            ESP_LOGI(TAG, "Received command: %s", supervisor_command_id(cmd->type));
            command_dispatch(cmd);

            if (cmd->args_json_str) {
                free(cmd->args_json_str);
                cmd->args_json_str = NULL;
            }
            free(cmd);
        }

        // Main cyclic stage execution, for each interval stage.
        TickType_t now = xTaskGetTickCount();
        for (int interval_idx = 0; interval_idx < SUPERVISOR_INTERVAL_COUNT; ++interval_idx) {
            if (now - last_stage[interval_idx] >=
                pdMS_TO_TICKS(supervisor_intervals_ms[interval_idx])) {
                supervisor_execute_stage((supervisor_interval_stage_t)interval_idx);
                last_stage[interval_idx] = now;
            }
        }
    }
}

void supervisor_command_print_all(void) {

    ESP_LOGI(TAG, "Available supervisor commands:");

    for (supervisor_command_type_t cmd = 0; cmd < CMND_COUNT; cmd++) {

        const char *id = supervisor_command_id(cmd);
        const char *desc = supervisor_command_description(cmd);

        ESP_LOGI(TAG, "  %-15s - %s", id, desc);
    }
}

void supervisor_add_json_field(cJSON *json_root, const char *name, const void *value,
                               telemetry_field_type_t type) {
    if (value == NULL || type == TELE_TYPE_NULL) {
        cJSON_AddNullToObject(json_root, name);
        return;
    }

    switch (type) {
    case TELE_TYPE_BOOL:
        cJSON_AddBoolToObject(json_root, name, *(const bool *)value);
        break;
    case TELE_TYPE_STRING:
        cJSON_AddStringToObject(json_root, name, (const char *)value);
        break;
    case TELE_TYPE_INT:
        cJSON_AddNumberToObject(json_root, name, (double)(*(const uint32_t *)value));
        break;
    case TELE_TYPE_FLOAT:
        cJSON_AddNumberToObject(json_root, name, (double)(*(const float *)value));
        break;
    default:
        cJSON_AddStringToObject(json_root, name, "<unsupported>");
    }
}

/**
 * @brief Generates a JSON object describing current FreeRTOS tasks.
 *
 * This function collects runtime information about all currently active
 * FreeRTOS tasks and serializes it into a JSON dictionary using cJSON.
 * Each task is represented as a sub-object with fields such as priority,
 * stack watermark, runtime counter, task number, state, and core affinity
 * (if supported).
 *
 * @note Intended for diagnostic/debug use — prioritizes clarity over
 * efficiency.
 *
 * @return cJSON* Root JSON object representing the task dictionary,
 *                or NULL on allocation failure.
 */

void supervisor_append_task_info(cJSON *json_root) {
    if (!json_root)
        return;

    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_status_array = calloc(num_tasks, sizeof(TaskStatus_t));
    if (!task_status_array)
        return;

    cJSON *task_dict = cJSON_CreateObject();
    if (!task_dict) {
        free(task_status_array);
        return;
    }

    uint32_t total_runtime = 0;
    UBaseType_t real_task_count =
        uxTaskGetSystemState(task_status_array, num_tasks, &total_runtime);

    for (UBaseType_t i = 0; i < real_task_count; i++) {
        cJSON *json_task = cJSON_CreateObject();
        if (!json_task)
            continue;

        cJSON_AddNumberToObject(json_task, "prio", task_status_array[i].uxCurrentPriority);
        cJSON_AddNumberToObject(json_task, "stack", task_status_array[i].usStackHighWaterMark);
        cJSON_AddNumberToObject(json_task, "runtime_ticks", task_status_array[i].ulRunTimeCounter);
        cJSON_AddNumberToObject(json_task, "task_number", task_status_array[i].xTaskNumber);

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

        cJSON_AddItemToObject(task_dict, task_status_array[i].pcTaskName, json_task);
    }

    free(task_status_array);
    cJSON_AddItemToObject(json_root, "tasks_dict", task_dict);
}

void supervisor_state_to_json(cJSON *json_root) {
#define TELE(name, generic_type, telemetry_field_type_t, default_val)                              \
    supervisor_add_json_field(json_root, #name, &state.name, telemetry_field_type_t);
    TELE_LIST
#undef TELE
    /**
     * Handling of pointers to JSON structures must be done manually,
     * because automatic parsing is not flexible enough.
     * If you need to add more pointers to JSON structures,
     * do it explicitly in this section to maintain full
     * control over the data structure.
     */
    supervisor_append_task_info(json_root);
}

void supervisor_set_onboard_led_state(bool new_state) {

    if (state.onboard_led == new_state)
        return;
    state.onboard_led = new_state;
    xEventGroupSetBits(app_event_group, TELEMETRY_TRIGGER_BIT);
}

void supervisor_init() {

#if CONFIG_MQTT_ENABLE
    mqtt_init(config_get()->mqtt_mtls_en);
#endif

    xTaskCreate(tcp_ota_task, "tcp_ota", 8192, NULL, 0, NULL);
    xTaskCreate(udp_monitor_task, "udp_monitor", 4096, NULL, 5, NULL);

    xTaskCreate(memory_info_task, "memory_info_task", 4096, NULL, 0, NULL);

    // xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 0, NULL);
    // xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 0,
    //             &ledBlinkTaskHandle);
}

void supervisor_publish_mqtt(const char *topic, const char *payload, int qos, bool retain) {
    if (!(xEventGroupGetBits(app_event_group) & MQTT_FAIL_BIT)) {
        mqtt_publish(topic, payload, qos, retain);
    } else {
        ESP_LOGW(TAG, "No connection to the MQTT broker, skipping publish to topic: %s", topic);
    }
}
