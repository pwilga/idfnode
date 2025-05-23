#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"

#define MAX_JSON_ARGS_LEN 64

// X Macro

#define CMD_LIST                                                                                   \
    CMD(CMD_RESTART, "restart", "Restart the system")                                              \
    CMD(CMD_LED_SET, "onboard_led", "Turn onboard LED on/off")                                     \
    CMD(CMD_LOG_STATUS, "log", "Set internal value")                                               \
    CMD(CMD_SET_AP, "ap", "Receive nested JSON string")                                            \
    CMD(CMD_SET_MODE, "mode", "Setting modes of something")                                        \
    CMD(CMD_HA_DISCOVERY, "ha", "MQTT Discovery mode")                                             \
    CMD(CMD_HELP, "help", "Print help message on the terminal")

typedef enum {

#define CMD(enum_id, id_str, desc) enum_id,
    CMD_LIST
#undef CMD
        CMD_UNKNOWN,
    CMD_COUNT = CMD_UNKNOWN
} supervisor_command_type_t;

typedef struct {
    supervisor_command_type_t type;
    char args_json_str[MAX_JSON_ARGS_LEN];
} supervisor_command_t;

void supervisor_task(void *args);
bool supervisor_schedule_command(supervisor_command_t *cmd);

const char *supervisor_command_id(supervisor_command_type_t cmd);
const char *supervisor_command_description(supervisor_command_type_t cmd);
supervisor_command_type_t supervisor_command_from_id(const char *id);

void supervisor_command_print_all(void);

#ifdef __cplusplus
}
#endif

#endif // SUPERVISOR_H
