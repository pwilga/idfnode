#include "esp_log.h"
#define TAG "device:c6dev"

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
#include <string.h>

#include "button_adapter.h"
#include "cmnd.h"

#define BUTTON_NAME_AP "ap"

static void device_button_handler(uint8_t button_idx, const char *name, button_event_t event) {
    if (strcmp(name, BUTTON_NAME_AP) == 0) {

        if (event == BUTTON_SINGLE_CLICK) {
            cmnd_submit("ap", "\"on\"");
        }
        button_adapter_log_event(button_idx, event);

    } else {
        ESP_LOGW(TAG, "Unknown button '%s' (idx %d)", name, button_idx);
    }
}
#endif // CONFIG_ENABLE_SUPERVISOR_BUTTON

void device_handlers_init(void) {
    ESP_LOGI(TAG, "Device handlers initialized");

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
    button_adapter_register_callback(device_button_handler);
#endif
}
