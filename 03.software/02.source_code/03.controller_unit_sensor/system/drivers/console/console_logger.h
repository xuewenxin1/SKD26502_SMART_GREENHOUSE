#ifndef CONSOLE_LOGGER_H
#define CONSOLE_LOGGER_H

#include <string.h>

#define CONSOLE_LOGGER_BUFFER_SIZE 512

#define GET_FILENAME() (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_ERR_NONE 0
#define LOG_ERR_ERR -1
#define LOG_ERR_NOINIT -2

int console_logger_init();
void console_logger_print_banner();

int debug_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...);
int info_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...);
int warn_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...);
int error_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...);

#ifdef __cplusplus
}
#endif

#define LOG_DEBUG(fmt,...) debug_with_trace(GET_FILENAME(),__FUNCTION__,__LINE__,fmt,##__VA_ARGS__)
#define LOG_INFO(fmt,...) info_with_trace(GET_FILENAME(),__FUNCTION__,__LINE__,fmt,##__VA_ARGS__)
#define LOG_WARN(fmt,...) warn_with_trace(GET_FILENAME(),__FUNCTION__,__LINE__,fmt,##__VA_ARGS__)
#define LOG_ERROR(fmt,...) error_with_trace(GET_FILENAME(),__FUNCTION__,__LINE__,fmt,##__VA_ARGS__)

#endif
