#include "driver/gpio.h"
#include "esp_log.h"
#define TAG "device:bedroom_ceiling"

// Board has a stock WS2812 addressable LED on GPIO8, left floating - the data pin
// picks up noise as fake bits and lights up randomly/white. Holding GPIO8 solid LOW
// keeps it permanently off (WS2812 never sees a valid frame).
#define STRAY_WS2812_DATA_GPIO GPIO_NUM_8

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
#include <string.h>

#include "button_adapter.h"
#include "cmnd.h"

// Names come from CONFIG_BUTTON_GPIO_LIST="18:0:button_left,19:0:button_right"
#define BUTTON_NAME_LEFT "button_left"
#define BUTTON_NAME_RIGHT "button_right"

static void device_button_handler(uint8_t button_idx, const char *name, button_event_t event) {
    if (strcmp(name, BUTTON_NAME_LEFT) == 0) {

        if (event == BUTTON_LONG_PRESS_START) {
            cmnd_submit("help", NULL);
        }
        button_adapter_log_event(button_idx, event);

    } else if (strcmp(name, BUTTON_NAME_RIGHT) == 0) {

        if (event == BUTTON_LONG_PRESS_START) {
            cmnd_submit("help", NULL);
        }
        button_adapter_log_event(button_idx, event);

    } else {
        ESP_LOGW(TAG, "Unknown button '%s' (idx %d)", name, button_idx);
    }
}
#endif // CONFIG_ENABLE_SUPERVISOR_BUTTON

void device_handlers_init(void) {
    ESP_LOGI(TAG, "Device handlers initialized");

    gpio_reset_pin(STRAY_WS2812_DATA_GPIO);
    gpio_set_direction(STRAY_WS2812_DATA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(STRAY_WS2812_DATA_GPIO, 0);

#ifdef CONFIG_ENABLE_SUPERVISOR_BUTTON
    button_adapter_register_callback(device_button_handler);
#endif
}
