#ifndef HELPERS_H
#define HELPERS_H
#include <stdbool.h>

#include "cJSON.h"

/**
 * @brief Returns a sanitized lowercase copy of the input string.
 *
 * Spaces are replaced with underscores, uppercase letters are lowercased.
 *
 * @param s Input null-terminated C string.
 * @return char* Newly allocated sanitized string. Must be freed by caller.
 */
char *sanitize(const char *s);

// bool parse_bool_string(const char *input);
bool parse_bool_json(cJSON *cmnd_param);
bool get_onboard_led_state(void);
void dispatch_command(const char *cmd, void *args);

typedef void (*command_handler_t)(void *args);
typedef struct {
    const char *command_name;
    command_handler_t handler;
} command_entry_t;

#endif // HELPERS_H