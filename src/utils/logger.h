#ifndef __LOGGER_H_
#define __LOGGER_H_

#ifdef __cplusplus
extern "C" {
#endif

int  logger_connect(int *socket);
void logger_disconnect(int socket);
void log_string(int sock, const char* str, char byte);
void log_byte(int sock, char byte);

void log_init(const char * ip);
void log_deinit(void);
void log_print(const char *str);
void log_printf(const char *format, ...);

void threadLog_init(const char * ip, int * log_socket);
void threadLog_deinit(int * log_socket);
void threadLog_print(const char *str, const int * log_socket);
void threadLog_printf(const int * log_socket, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif

