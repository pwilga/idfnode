#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "button_adapter.h"
#include "inet_adapter.h"
#include "rf433_adapter.h"
#include "supervisor.h"
// #include "zigbee_adapter.h" // uncomment when zigbee is ready

void app_main(void) {
    // Initialize supervisor core
    supervisor_init();

    // Register adapters
    supervisor_register_adapter(&button_adapter);
    supervisor_register_adapter(&inet_adapter);
    supervisor_register_adapter(&rf433_adapter);
    // supervisor_register_adapter(&zigbee_adapter); // uncomment when zigbee is ready

    supervisor_platform_init();

    // Main loop
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
