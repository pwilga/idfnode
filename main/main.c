#include "freertos/FreeRTOS.h"
#include "supervisor.h"

void app_main(void) {

    supervisor_init();

    for (;;) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
