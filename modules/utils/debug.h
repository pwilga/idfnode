#ifndef DEBUG_H
#define DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

void memory_info_task(void *pvParameter);
void led_blink_task(void *args);
void heartbeat_task(void *args);
void print_sys_info_task(void *args);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_H
