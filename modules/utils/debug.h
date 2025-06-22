#ifndef DEBUG_H
#define DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

void debug_info_task(void *args);
void led_blink_task(void *args);
void heartbeat_task(void *args);
void print_sys_info_task(void *args);
void show_task_info(void *args);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_H
