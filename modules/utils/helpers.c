#include <ctype.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"

#include "helpers.h"
#include "platform_services.h"

#if CONFIG_HOME_ASSISTANT_MQTT_DISCOVERY_ENABLE
#include "ha.h"
#endif

void restart(void *args) {
    xTaskCreate(esp_safe_restart, "restart", 2048, NULL, configMAX_PRIORITIES - 1, NULL);
}

static bool onboard_led_state = false;

bool get_onboard_led_state(void) { return onboard_led_state; }

void onboard_led(void *args) {

    bool new_state = parse_bool_json((cJSON *)args);

    if (onboard_led_state != new_state) {
        ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, !new_state));
        onboard_led_state = new_state;
    }
}

const command_entry_t command_table[] = {{"onboard_led", onboard_led},
                                         {"restart", restart},
#if CONFIG_HOME_ASSISTANT_MQTT_DISCOVERY_ENABLE
                                         {"ha", publish_ha_mqtt_discovery},
#endif
                                         {NULL, NULL}};

void dispatch_command(const char *cmd, void *args) {

    const char *TAG = "command-dispatcher";

    for (int i = 0; command_table[i].command_name; ++i) {
        if (strcasecmp(cmd, command_table[i].command_name))
            continue;

        ESP_LOGI(TAG, "Executing: %s", cmd);
        command_table[i].handler(args);
        return;
    }
    ESP_LOGW(TAG, "Unknown command: %s", cmd);
}

bool valid_bool_param(cJSON *cmnd_param) {
    return cJSON_IsString(cmnd_param) || cJSON_IsNumber(cmnd_param) || cJSON_IsBool(cmnd_param) ||
           cJSON_IsNull(cmnd_param);
}

bool parse_bool_string(const char *input) {
    return input && (!strcasecmp(input, "true") || !strcasecmp(input, "1") ||
                     !strcasecmp(input, "on") || !strcasecmp(input, "up"));
}

bool parse_bool_json(cJSON *cmnd_param) {
    const char *TAG = "parse-json-bool";

    // cJSON *cmnd_param = cJSON_Parse(input);
    if (!valid_bool_param(cmnd_param)) {
        ESP_LOGE(TAG, "Invalid command parameter: expected a JSON string, number, "
                      "boolean or null.");
        // cJSON_Delete(cmnd_param);
        return false;
    }

    bool state = false;
    if (cJSON_IsString(cmnd_param)) {
        state = parse_bool_string(cmnd_param->valuestring);
    } else if (cJSON_IsNumber(cmnd_param)) {
        state = cmnd_param->valuedouble;
    } else if (cJSON_IsBool(cmnd_param)) {
        state = cJSON_IsTrue(cmnd_param);
    }

    // cJSON_Delete(cmnd_param);
    return state;
}

char *sanitize(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    for (size_t i = 0; i < len; i++) {
        if (s[i] == ' ')
            out[i] = '_';
        else if (isupper((unsigned char)s[i]))
            out[i] = tolower((unsigned char)s[i]);
        else
            out[i] = s[i];
    }
    out[len] = '\0';
    return out;
}
