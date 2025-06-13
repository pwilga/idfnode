#ifndef HTTPS_SERVER_H
#define HTTPS_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

void https_init(void);
void https_shutdown(void);

void https_server_task(void *args);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_SERVER_H
