# RF433 Receiver

433MHz RF receiver module using ESP32 RMT peripheral for decoding RC Switch protocols.

## Features

- Supports RC Switch Protocol 1 and Protocol 2
- Hardware-based signal decoding using RMT peripheral
- Event-based architecture using ESP-IDF event loop
- Automatic debouncing (150ms)
- Decodes 24-bit RF codes
- Configurable via Kconfig

## API

### Configuration

```c
void rf433_receiver_configure(gpio_num_t rx_pin);
```

Configure the receiver GPIO pin.

**Parameters:**
- `rx_pin` - GPIO pin connected to RF receiver (e.g., GPIO_NUM_23)

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

## Events

### Event Base

```c
ESP_EVENT_DECLARE_BASE(RF433_EVENTS);
```

### Event IDs

```c
typedef enum {
    RF433_CODE_RECEIVED,
} rf433_event_id_t;
```

### Event Data

```c
typedef struct {
    uint32_t code;  // 24-bit RF code
    uint8_t bits;   // Number of bits decoded (always 24)
} rf433_event_data_t;
```

## Usage Example

```c
#include "rf433_receiver.h"
#include "esp_event.h"

static void rf433_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    rf433_event_data_t *event = (rf433_event_data_t*)data;

    ESP_LOGI("APP", "RF code received: 0x%06X (%d bits)", event->code, event->bits);

    switch(event->code) {
        case 0x5447C2:
            ESP_LOGI("APP", "Button 1 pressed");
            // Handle button 1
            break;

        case 0xB9F9C1:
            ESP_LOGI("APP", "Button 2 pressed");
            // Handle button 2
            break;
    }
}

void app_main(void) {
    // Register event handler
    esp_event_handler_register(RF433_EVENTS, RF433_CODE_RECEIVED, rf433_event_handler, NULL);

    // Configure and start receiver
    rf433_receiver_configure(GPIO_NUM_23);
    rf433_receiver_init();
}
```

## Configuration (Kconfig)

Available options in `menuconfig`:
- `RF433_RX_GPIO` - GPIO pin (default: 23)
- `RF433_TASK_STACK_SIZE` - Task stack size (default: 3072)
- `RF433_TASK_PRIORITY` - Task priority (default: 10)
- `RF433_DEBOUNCE_MS` - Debounce time (default: 150ms)
- `RF433_RMT_RESOLUTION_HZ` - RMT resolution (default: 1MHz)
- `RF433_RMT_MEM_BLOCK_SYMBOLS` - RMT buffer size (default: 64)

## Hardware

- **Receiver Module:** SYN480R or similar 433MHz ASK/OOK receiver
- **Connection:** Data pin → ESP32 GPIO (e.g., GPIO 23)
- **Power:** 5V or 3.3V depending on module

## Supported Protocols

- **RC Switch Protocol 1:** Short 250-450µs, Long 950-1200µs
- **RC Switch Protocol 2:** Short 200-350µs, Long 600-850µs

Both protocols use Manchester-like encoding: long-short=1, short-long=0
