#include "esp_log.h"

#include "cJSON.h"

#include "supervisor.h"
#include "json_parser.h"
#include "platform_services.h"
#include "wifi.h"
#include "mqtt.h"
#include "ha.h"

const char *supervisor_command_id(supervisor_command_type_t cmd) {
    switch (cmd) {
#define CMD(enum_id, id_str, desc)                                                                 \
    case enum_id:                                                                                  \
        return id_str;
        CMD_LIST
#undef CMD
    default:
        return "<unknown>";
    }
}

const char *supervisor_command_description(supervisor_command_type_t cmd) {
    switch (cmd) {
#define CMD(enum_id, id_str, desc)                                                                 \
    case enum_id:                                                                                  \
        return desc;
        CMD_LIST
#undef CMD
    default:
        return "<unknown>";
    }
}

supervisor_command_type_t supervisor_command_from_id(const char *id) {
#define CMD(enum_id, id_str, desc)                                                                 \
    if (strcmp(id, id_str) == 0)                                                                   \
        return enum_id;
    CMD_LIST
#undef CMD
    return CMD_UNKNOWN;
}

bool supervisor_schedule_command(supervisor_command_t *cmd) {
    if (!supervisor_queue)
        return false;
    return xQueueSend(supervisor_queue, cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

void command_dispatch(supervisor_command_t *cmd) {

    const char *TAG = "supervisor-command-dispatcher";

    switch (cmd->type) {
    case CMD_HA_DISCOVERY:
        logic_state_t force_empty_payload = json_str_as_logic_state(cmd->args_json_str);
        if (force_empty_payload == STATE_TOGGLE) {
            ESP_LOGE(TAG, "Toggling is not permitted for this module.");
            return;
        }
        publish_ha_mqtt_discovery(force_empty_payload == STATE_OFF);
        break;
    case CMD_RESTART:
        ESP_LOGW(TAG, "Restarting system...");
        esp_safe_restart(NULL);
        break;

    case CMD_SET_MODE:

        break;

    case CMD_LOG_STATUS:
        ESP_LOGI(TAG, "Logic state: %d", json_str_as_logic_state(cmd->args_json_str));
        ESP_LOGI(TAG, "System running, heap: %u", esp_get_free_heap_size());
        break;

    case CMD_LED_SET:
        logic_state_t state = json_str_as_logic_state(cmd->args_json_str);
        onboard_led_set_state(state);

        ESP_LOGI(TAG, "LED CO: %s", cmd->args_json_str);
        break;

    case CMD_SET_AP:
        mqtt_shutdown();
        wifi_ensure_ap_mode();
        break;

    case CMD_HELP:
        supervisor_command_print_all();
        break;

    default:
        ESP_LOGW(TAG, "Unknown command type: %s", supervisor_command_id(cmd->type));
        break;
    }
}

void supervisor_task(void *args) {

    const char *TAG = "cikon-supervisor";
    ESP_LOGI(TAG, "Supervisor task started.");

    supervisor_command_t cmd;

    while (1) {
        if (xQueueReceive(supervisor_queue, &cmd, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Received command: %s", supervisor_command_id(cmd.type));
            command_dispatch(&cmd);
        }
    }
}

void supervisor_command_print_all(void) {

    const char *TAG = "cikon-supervisor";
    ESP_LOGI(TAG, "Available supervisor commands:");

    for (supervisor_command_type_t cmd = 0; cmd < CMD_COUNT; cmd++) {

        const char *id = supervisor_command_id(cmd);
        const char *desc = supervisor_command_description(cmd);

        ESP_LOGI(TAG, "  %-15s - %s", id, desc);
    }
}