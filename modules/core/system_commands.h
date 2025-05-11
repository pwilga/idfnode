#ifndef SYSTEM_COMMANDS_H
#define SYSTEM_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"

#include "cJSON.h"

typedef void (*command_handler_t)(void *args);
typedef struct {
    const char *command_name;
    command_handler_t handler;
} command_entry_t;

esp_err_t command_dispatch(const char *cmd, void *args);

void restart(void *args);
void onboard_led(void *args);

bool parse_bool_json(cJSON *cmnd_param);
bool get_onboard_led_state(void);

// esp_err_t system_command_execute(const char *cmd);
// void system_command_reboot(void);
// void system_command_sleep(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_COMMANDS_H
