#include "freertos/FreeRTOS.h"

#include "platform_services.h"
#include "supervisor.h"

void app_main(void) {

    ESP_ERROR_CHECK(core_system_init());

    xTaskCreate(supervisor_task, "supervisor", 4096, NULL, 5, NULL);

    for (;;) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
