#include <stdint.h>

#include "esp_log.h"

#include "adapters/rf433.h"
#include "rf433_receiver.h"

void sonoff(uint32_t code) { ESP_LOGI("RF433", "Sonoff received code: 0x%06X", code); }

void blue_button(uint32_t code) { ESP_LOGI("RF433", "Blue Button received code: 0x%06X", code); }

void rf433_adapter_init(void) {

    static const rf433_handler_t rf_handlers[] = {{.code = 0x5447C2, .callback = sonoff},
                                                  {.code = 0xB9F9C1, .callback = blue_button},
                                                  {.code = 0, .callback = NULL}};

    rf433_receiver_configure(GPIO_NUM_23, rf_handlers);
    rf433_receiver_init();
}

// void rf433_adapter_shutdown(void) {}

static void rf433_adapter_on_event(EventBits_t bits) {}

static void rf433_adapter_on_interval(supervisor_interval_stage_t stage) {}

supervisor_platform_adapter_t rf433_adapter = {.init = rf433_adapter_init,
                                               .shutdown = rf433_receiver_shutdown,
                                               .on_event = rf433_adapter_on_event,
                                               .on_interval = rf433_adapter_on_interval};
