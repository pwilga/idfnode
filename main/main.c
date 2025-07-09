#include "freertos/FreeRTOS.h"

#include "platform_services.h"
#include "supervisor.h"

void app_main(void) {

    ESP_ERROR_CHECK(core_system_init());

    xTaskCreate(supervisor_task, "supervisor", CONFIG_SUPERVISOR_TASK_STACK_SIZE, NULL,
                CONFIG_SUPERVISOR_TASK_PRIORITY, NULL);

    for (;;) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
