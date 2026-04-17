#include <stdint.h>

#include "esp_log.h"

#define TAG "device:cikonesp"

#ifdef CONFIG_ENABLE_SUPERVISOR_RF433
#include "cmnd.h"
#include "rf433_adapter.h"

static void device_rf433_handler(uint32_t code, uint8_t bits) {
    switch (code) {
    case 0x5447C2:
        cmnd_submit("onboard_led", "\"toggle\"");
        break;

    case 0xB9F9C1:
        ESP_LOGE(TAG, "Received RF code for onboard LED toggle");
        cmnd_submit("onboard_led", "\"toggle\"");
        break;

    default:
        ESP_LOGW(TAG, "Unknown RF code: 0x%06" PRIX32, code);
        break;
    }
}
#endif // CONFIG_ENABLE_SUPERVISOR_RF433

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
#include "button_adapter.h"
#include "cmnd.h"

static void device_button_handler(uint8_t button_idx, button_event_t event) {
    switch (event) {
    case BUTTON_SINGLE_CLICK:
        cmnd_submit("mesh_send", "{\"target\":\"glupol\","
                                 "\"cmnd\":{\"onboard_led\":\"toggle\"}}");
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

#ifdef CONFIG_ENABLE_SUPERVISOR_RF433
    rf433_adapter_register_callback(device_rf433_handler);
#endif

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
    button_adapter_register_callback(device_button_handler);
#endif
}
