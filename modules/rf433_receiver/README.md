# RF433 Receiver

433MHz RF receiver module using ESP32 RMT peripheral for decoding RC Switch protocols.

## Features

- Supports RC Switch Protocol 1 and Protocol 2
- Hardware-based signal decoding using RMT peripheral
- Callback-based code handling
- Automatic debouncing (150ms)
- Decodes 24-bit RF codes

## API

### Configuration

```c
void rf433_receiver_configure(gpio_num_t rx_pin, const rf433_handler_t *handlers);
```

Configure the receiver with GPIO pin and callback handlers.

**Parameters:**
- `rx_pin` - GPIO pin connected to RF receiver (e.g., GPIO_NUM_23)
- `handlers` - Array of code/callback pairs, terminated with `{0, NULL}`

### Initialization

```c
void rf433_receiver_init(void);
```

Start the receiver task. Must be called after `rf433_receiver_configure()`.

### Shutdown

```c
void rf433_receiver_shutdown(void);
```

Stop the receiver and free all resources.

## Usage Example

```c
#include "rf433_receiver.h"

void button1_handler(uint32_t code) {
    ESP_LOGI("APP", "Button 1 pressed: 0x%06X", code);
}

void button2_handler(uint32_t code) {
    ESP_LOGI("APP", "Button 2 pressed: 0x%06X", code);
}

void app_main(void) {
    static const rf433_handler_t handlers[] = {
        {.code = 0x5447C2, .callback = button1_handler},
        {.code = 0xB9F9C1, .callback = button2_handler},
        {.code = 0, .callback = NULL}  // sentinel
    };

    rf433_receiver_configure(GPIO_NUM_23, handlers);
    rf433_receiver_init();
}
```

## Types

### rf433_callback_t

```c
typedef void (*rf433_callback_t)(uint32_t code);
```

Callback function invoked when a registered RF code is received.

### rf433_handler_t

```c
typedef struct {
    uint32_t code;
    rf433_callback_t callback;
} rf433_handler_t;
```

Maps RF code to callback function.

## Hardware

- **Receiver Module:** SYN480R or similar 433MHz ASK/OOK receiver
- **Connection:** Data pin → ESP32 GPIO (e.g., GPIO 23)
- **Power:** 5V or 3.3V depending on module

## Supported Protocols

- **RC Switch Protocol 1:** Short 250-450µs, Long 950-1200µs
- **RC Switch Protocol 2:** Short 200-350µs, Long 600-850µs

Both protocols use Manchester-like encoding: long-short=1, short-long=0
