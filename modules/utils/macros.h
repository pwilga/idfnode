
#ifndef MACROS_H
#define MACROS_H

#define ESP_TASK_CHECK(x)                                      \
    do                                                         \
    {                                                          \
        if ((x) != pdPASS)                                     \
        {                                                      \
            ESP_LOGE("TASK", "Error: Failed to create task!"); \
            abort();                                           \
        }                                                      \
    } while (0)

#define ESP_TASK_RETURN_ON_ERROR(x, tag, message) \
    do                                            \
    {                                             \
        if ((x) != pdPASS)                        \
        {                                         \
            ESP_LOGE(tag, "%s", message);         \
            return;                               \
        }                                         \
    } while (0)

#endif