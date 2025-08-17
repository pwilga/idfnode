#include "freertos/FreeRTOS.h"

#include "platform_services.h"
#include "supervisor.h"

void app_main(void) {

    ESP_ERROR_CHECK(core_system_init());
    supervisor_init();

    for (;;) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
