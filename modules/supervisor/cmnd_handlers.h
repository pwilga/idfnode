#ifndef CMND_HANDLERS_H
#define CMND_HANDLERS_H

#ifdef __cplusplus
extern "C" {
#endif

// Register all built-in command handlers (call after cmnd_init)
void cmnd_handlers_register(void);

#ifdef __cplusplus
}
#endif

#endif // CMND_HANDLERS_H