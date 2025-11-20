#include "freertos/FreeRTOS.h"  // IWYU pragma: keep
#include "supervisor.h"

// Include platform adapters
#include "adapters/inet.h"
// #include "adapters/zigbee.h"  // Uncomment to enable Zigbee adapter

void app_main(void) {

    supervisor_init();
    supervisor_register_adapter(&inet_adapter);
    supervisor_platform_init();

    for (;;) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
