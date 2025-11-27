#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

#include "rf433_receiver.h"

#define TAG "cikon-rf433-receiver"

#ifndef CONFIG_RF433_RMT_RESOLUTION_HZ
#define CONFIG_RF433_RMT_RESOLUTION_HZ 1000000
#endif

#ifndef CONFIG_RF433_RMT_MEM_BLOCK_SYMBOLS
#define CONFIG_RF433_RMT_MEM_BLOCK_SYMBOLS 64
#endif

#ifndef CONFIG_RF433_DEBOUNCE_MS
#define CONFIG_RF433_DEBOUNCE_MS 150
#endif

#ifndef CONFIG_RF433_TASK_STACK_SIZE
#define CONFIG_RF433_TASK_STACK_SIZE 3072
#endif

#ifndef CONFIG_RF433_TASK_PRIORITY
#define CONFIG_RF433_TASK_PRIORITY 10
#endif

// RC Switch Protocol 1
#define P1_SHORT_MIN 250
#define P1_SHORT_MAX 450
#define P1_LONG_MIN 950
#define P1_LONG_MAX 1200

// RC Switch Protocol 2
#define P2_SHORT_MIN 200
#define P2_SHORT_MAX 350
#define P2_LONG_MIN 600
#define P2_LONG_MAX 850

#define SYNC_MIN 2000
#define RF_CODE_BITS 24

typedef struct {
    uint32_t code;
    uint8_t bits;
} rf_code_t;

static gpio_num_t rf433_rx_pin = GPIO_NUM_NC;
static const rf433_handler_t *rf433_handlers = NULL;

static uint32_t last_code = 0;
static TickType_t last_code_time = 0;

static TaskHandle_t rf433_task_handle = NULL;
static rmt_channel_handle_t rx_channel_handle = NULL;
static QueueHandle_t rx_queue_handle = NULL;

static bool decode_rc_switch(const rmt_symbol_word_t *symbols, size_t num_symbols,
                             rf_code_t *result) {
    if (num_symbols < RF_CODE_BITS)
        return false;

    uint32_t code = 0;
    uint8_t bit_count = 0;

    for (size_t i = 0; i < num_symbols && bit_count < RF_CODE_BITS; i++) {
        uint32_t duration0 = symbols[i].duration0;
        uint32_t duration1 = symbols[i].duration1;

        if (duration0 > SYNC_MIN || duration1 > SYNC_MIN)
            continue;

        bool p1_d0_long = (duration0 >= P1_LONG_MIN && duration0 <= P1_LONG_MAX);
        bool p1_d1_long = (duration1 >= P1_LONG_MIN && duration1 <= P1_LONG_MAX);
        bool p1_d0_short = (duration0 >= P1_SHORT_MIN && duration0 <= P1_SHORT_MAX);
        bool p1_d1_short = (duration1 >= P1_SHORT_MIN && duration1 <= P1_SHORT_MAX);

        bool p2_d0_long = (duration0 >= P2_LONG_MIN && duration0 <= P2_LONG_MAX);
        bool p2_d1_long = (duration1 >= P2_LONG_MIN && duration1 <= P2_LONG_MAX);
        bool p2_d0_short = (duration0 >= P2_SHORT_MIN && duration0 <= P2_SHORT_MAX);
        bool p2_d1_short = (duration1 >= P2_SHORT_MIN && duration1 <= P2_SHORT_MAX);

        bool p1_valid = (p1_d0_long && p1_d1_short) || (p1_d0_short && p1_d1_long);
        bool p2_valid = (p2_d0_long && p2_d1_short) || (p2_d0_short && p2_d1_long);

        if (!p1_valid && !p2_valid)
            continue;

        code <<= 1;

        if ((p1_d0_long && p1_d1_short) || (p2_d0_long && p2_d1_short)) {
            code |= 1;
        }

        bit_count++;
    }

    if (bit_count == RF_CODE_BITS) {
        result->code = code;
        result->bits = bit_count;
        return true;
    }

    return false;
}

static void dispatch_code(uint32_t code) {
    if (rf433_handlers == NULL)
        return;

    for (const rf433_handler_t *h = rf433_handlers; h->code != 0; h++) {
        if (code == h->code && h->callback != NULL) {
            h->callback(code);
        }
    }
}

static bool IRAM_ATTR rfrx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata,
                                void *udata) {
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t rx_queue = (QueueHandle_t)udata;

    xQueueSendFromISR(rx_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void rf433_receiver_task(void *param) {

    rmt_symbol_word_t symbols[CONFIG_RF433_RMT_MEM_BLOCK_SYMBOLS];
    rmt_rx_done_event_data_t rx_data;

    rmt_receive_config_t rx_config = {
        .signal_range_min_ns = 1000,    // 1µs minimum
        .signal_range_max_ns = 5000000, // 5ms maximum - triggers end of frame
    };

    rmt_rx_channel_config_t rx_ch_conf = {
        .gpio_num = rf433_rx_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = CONFIG_RF433_RMT_RESOLUTION_HZ,
        .mem_block_symbols = CONFIG_RF433_RMT_MEM_BLOCK_SYMBOLS,
        .flags.invert_in = false,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_ch_conf, &rx_channel_handle));

    rx_queue_handle = xQueueCreate(1, sizeof(rx_data));
    if (rx_queue_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create RX queue");
        rmt_del_channel(rx_channel_handle);
        return;
    }

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rfrx_done,
    };

    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel_handle, &cbs, rx_queue_handle));
    ESP_ERROR_CHECK(rmt_enable(rx_channel_handle));
    ESP_ERROR_CHECK(rmt_receive(rx_channel_handle, symbols, sizeof(symbols), &rx_config));

    ESP_LOGI(TAG, "RMT receiver ready on GPIO %d", rf433_rx_pin);

    for (;;) {
        if (xQueueReceive(rx_queue_handle, &rx_data, pdMS_TO_TICKS(1000)) != pdPASS)
            continue;

        size_t len = rx_data.num_symbols;
        rmt_symbol_word_t *cur = rx_data.received_symbols;

        if (len >= RF_CODE_BITS) {
            rf_code_t rf_code;

            if (decode_rc_switch(cur, len, &rf_code)) {
                TickType_t now = xTaskGetTickCount();
                if (rf_code.code != last_code ||
                    (now - last_code_time) > pdMS_TO_TICKS(CONFIG_RF433_DEBOUNCE_MS)) {
                    ESP_LOGI(TAG, "0x%06lX (%d bits) frame received", (unsigned long)rf_code.code,
                             rf_code.bits);
                    dispatch_code(rf_code.code);
                    last_code = rf_code.code;
                    last_code_time = now;
                }
            }
        }

        esp_err_t ret = rmt_receive(rx_channel_handle, symbols, sizeof(symbols), &rx_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "rmt_receive restart failed: %s", esp_err_to_name(ret));
        }
    }
}

void rf433_receiver_configure(gpio_num_t rx_pin, const rf433_handler_t *handlers) {
    rf433_rx_pin = rx_pin;
    rf433_handlers = handlers;
}

void rf433_receiver_init(void) {

    if (rf433_rx_pin == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "RF433 receiver not configured!");
        return;
    }

    ESP_LOGI(TAG, "Init RF433 RMT receiver on GPIO %d", rf433_rx_pin);
    xTaskCreate(rf433_receiver_task, "rf433_receiver", CONFIG_RF433_TASK_STACK_SIZE, NULL,
                CONFIG_RF433_TASK_PRIORITY, &rf433_task_handle);
}

void rf433_receiver_shutdown(void) {

    ESP_LOGI(TAG, "Shutting down RF433 receiver");

    if (rf433_task_handle != NULL) {
        vTaskDelete(rf433_task_handle);
        rf433_task_handle = NULL;
    }

    if (rx_channel_handle != NULL) {
        rmt_disable(rx_channel_handle);
        rmt_del_channel(rx_channel_handle);
        rx_channel_handle = NULL;
    }

    if (rx_queue_handle != NULL) {
        vQueueDelete(rx_queue_handle);
        rx_queue_handle = NULL;
    }
}
