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

static void device_button_handler(uint8_t button_idx, const char *button_name,
                                  button_event_t event) {
    (void)button_name;
    button_adapter_log_event(button_idx, event);
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
