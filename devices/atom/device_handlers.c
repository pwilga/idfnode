#include "esp_log.h"

#define TAG "device:atom"

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
#include "button_adapter.h"

static void device_button_handler(uint8_t button_idx, const char *button_name,
                                  button_event_t event) {
    (void)button_name;
    button_adapter_log_event(button_idx, event);
}
#endif // CONFIG_ENABLE_SUPERVISOR_BUTTON

void device_handlers_init(void) {
    ESP_LOGI(TAG, "Device handlers initialized");

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
    // Register custom button event handler
    button_adapter_register_callback(device_button_handler);
#endif
}
