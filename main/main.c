#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "supervisor.h"

#ifdef HAS_DEVICE_HANDLERS
extern void device_handlers_init(void);
#endif

#if CONFIG_ENABLE_SUPERVISOR_BUTTON
#include "button_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_DEBUG
#include "debug_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_DS18B20
#include "ds18b20_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_ESP_NOW_RX
#include "esp_now_rx_adapter.h"
#endif
#if CONFIG_ENABLE_SUPERVISOR_INET
#include "inet_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_INET_MESH
#include "inet_mesh_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_LED
#include "led_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_NEOPIXEL
#include "neopixel_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_LED_INDICATOR
#include "led_indicator_adapter.h"
#endif

#if CONFIG_ENABLE_SUPERVISOR_RF433
#include "rf433_adapter.h"
#endif

void app_main(void) {

    supervisor_init();

#if CONFIG_ENABLE_SUPERVISOR_BUTTON
    supervisor_register_adapter(&button_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_DEBUG
    supervisor_register_adapter(&debug_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_INET
    supervisor_register_adapter(&inet_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_INET_MESH
    supervisor_register_adapter(&inet_mesh_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_RF433
    supervisor_register_adapter(&rf433_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_LED
    supervisor_register_adapter(&led_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_NEOPIXEL
    supervisor_register_adapter(&neopixel_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_LED_INDICATOR
    supervisor_register_adapter(&led_indicator_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_DS18B20
    supervisor_register_adapter(&ds18b20_adapter);
#endif

#if CONFIG_ENABLE_SUPERVISOR_ESP_NOW_RX
    supervisor_register_adapter(&esp_now_rx_adapter);
#endif

    supervisor_platform_init();

#ifdef HAS_DEVICE_HANDLERS
    // Call device-specific initialization
    device_handlers_init();
#endif

    // Main loop
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
