#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"
#include "cJSON.h"

#define MAX_JSON_ARGS_LEN 64

// X Macro
typedef enum {
    TELE_TYPE_BOOL,
    TELE_TYPE_STRING,
    TELE_TYPE_INT,
    TELE_TYPE_FLOAT,
    TELE_TYPE_NULL
} telemetry_field_type_t;

#define CMND_LIST                                                                                  \
    CMND(CMND_RESTART, "restart", "Restart the system")                                            \
    CMND(CMND_LED_SET, "onboard_led", "Turn onboard LED on/off")                                   \
    CMND(CMND_LOG_STATUS, "log", "Set internal value")                                             \
    CMND(CMND_SET_AP, "ap", "Receive nested JSON string")                                          \
    CMND(CMND_SET_MODE, "mode", "Setting modes of something")                                      \
    CMND(CMND_HA_DISCOVERY, "ha", "MQTT Discovery mode")                                           \
    CMND(CMND_HELP, "help", "Print help message on the terminal")

typedef char str32_t[32];

#define TELE_LIST                                                                                  \
    TELE(onboard_led, bool, TELE_TYPE_BOOL, false)                                                 \
    TELE(startup, str32_t, TELE_TYPE_STRING, "")                                                   \
    TELE(uptime, uint32_t, TELE_TYPE_INT, 0)                                                       \
    TELE(tempreture, float, TELE_TYPE_FLOAT, 0.0f)

typedef enum {

#define CMND(enum_id, id_str, desc) enum_id,
    CMND_LIST
#undef CMND
        CMND_UNKNOWN,
    CMND_COUNT = CMND_UNKNOWN
} supervisor_command_type_t;

typedef struct {
    supervisor_command_type_t type;
    char args_json_str[MAX_JSON_ARGS_LEN];
} supervisor_command_t;

typedef struct {
#define TELE(name, generic_type, telemetry_field_type_t, default_val) generic_type name;
    TELE_LIST
#undef TELE
} supervisor_state_t;

void supervisor_init();
void supervisor_task(void *args);
bool supervisor_schedule_command(supervisor_command_t *cmd);

const char *supervisor_command_id(supervisor_command_type_t cmd);
const char *supervisor_command_description(supervisor_command_type_t cmd);
supervisor_command_type_t supervisor_command_from_id(const char *id);

void supervisor_publish_mqtt(const char *topic, const char *payload, int qos, bool retain);
void supervisor_command_print_all(void);
void supervisor_state_to_json(cJSON *json_root);
void supervisor_set_onboard_led_state(bool new_state);

#ifdef __cplusplus
}
#endif

#endif // SUPERVISOR_H
