#include "esp_log.h"

#define TAG "device:cikonesp_mesh"

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
#include "button_adapter.h"
#include "cmnd.h"

static void device_button_handler(uint8_t button_idx, button_event_t event) {
    switch (event) {
    case BUTTON_SINGLE_CLICK:
        cmnd_submit("mesh", "{\"target\":\"glupol\",\"cmnd\":\"Hello from cikonesp\"}");
        break;

    case BUTTON_DOUBLE_CLICK:

        cmnd_submit("onboard_led", "\"toggle\"");
        break;

    case BUTTON_LONG_PRESS_START:

        cmnd_submit("help", NULL);
        break;

    default:
        ESP_LOGI(TAG, "Button %d: Unhandled event %d", button_idx, event);
        break;
    }
}
#endif // CONFIG_ENABLE_SUPERVISOR_BUTTON

void device_handlers_init(void) {
    ESP_LOGI(TAG, "Device handlers initialized");

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
    // Register custom button event handler
    button_adapter_register_callback(device_button_handler);
#endif
}
