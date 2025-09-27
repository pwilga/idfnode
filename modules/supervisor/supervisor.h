#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"
#include "stdbool.h"

typedef enum {
    TELE_TYPE_BOOL,
    TELE_TYPE_STRING,
    TELE_TYPE_INT,
    TELE_TYPE_FLOAT,
    TELE_TYPE_NULL
} telemetry_field_type_t;

typedef char str32_t[32];

#define TELE_LIST                                                                                  \
    TELE(onboard_led, bool, TELE_TYPE_BOOL, false)                                                 \
    TELE(startup, str32_t, TELE_TYPE_STRING, "")                                                   \
    TELE(uptime, uint32_t, TELE_TYPE_INT, 0)                                                       \
    TELE(tempreture, float, TELE_TYPE_FLOAT, 0.0f)

typedef struct {
#define TELE(name, generic_type, telemetry_field_type_t, default_val) generic_type name;
    TELE_LIST
#undef TELE
} supervisor_state_t;

void supervisor_init();

void supervisor_state_to_json(cJSON *json_root);
void supervisor_set_onboard_led_state(bool new_state);

const supervisor_state_t *state_get(void);

#ifdef __cplusplus
}
#endif

#endif // SUPERVISOR_H
