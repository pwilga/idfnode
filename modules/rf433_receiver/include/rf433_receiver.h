#ifndef RF433_RECEIVER_H
#define RF433_RECEIVER_H

#include <soc/gpio_num.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rf433_callback_t)(uint32_t code);

typedef struct {
    uint32_t code;
    rf433_callback_t callback;
} rf433_handler_t;

void rf433_receiver_configure(gpio_num_t rx_pin, const rf433_handler_t *handlers);
void rf433_receiver_init(void);
void rf433_receiver_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // RF433_RECEIVER_H
