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
#include <string.h>

#include "button_adapter.h"
#include "cmnd.h"

// Names come from CONFIG_BUTTON_GPIO_LIST="0:0:Toggle Leds"
// "Color Strip" is the RGBCW light from this profile's config/cikon_light/lights.yaml
// button.c/light.c both sanitize names (lowercase, spaces -> '_') before storing/registering,
// so match/submit using the sanitized forms, not the raw config labels.
#define BUTTON_NAME_TOGGLE_LEDS "toggle_leds"
#define LIGHT_NAME_COLOR_STRIP "color_strip"

static void device_button_handler(uint8_t button_idx, const char *button_name,
                                  button_event_t event) {
    if (strcmp(button_name, BUTTON_NAME_TOGGLE_LEDS) == 0) {
        if (event == BUTTON_SINGLE_CLICK) {
            cmnd_submit(LIGHT_NAME_COLOR_STRIP, "\"toggle\"");
        }
        button_adapter_log_event(button_idx, event);
    } else {
        ESP_LOGW(TAG, "Unknown button '%s' (idx %d)", button_name, button_idx);
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
