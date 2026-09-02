#include "console_logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static const char *ESCAPE_CODE_DEFAULT = "\033[0m";
static const char *ESCAPE_CODE_DEBUG = "\033[36m";    // Cyan
static const char *ESCAPE_CODE_INFO = "\033[32m";     // Green
static const char *ESCAPE_CODE_WARN = "\033[33m";  // Yellow
static const char *ESCAPE_CODE_ERROR = "\033[31m";    // Red

// static SemaphoreHandle_t console_logger_mutex = NULL;
static char console_logger_buffer[CONSOLE_LOGGER_BUFFER_SIZE] = {0};

int console_logger_init(){
    return LOG_ERR_NONE;
}

void console_logger_print_banner(){
    printf("┌────────────────────┐\r\n");
    printf("│ Maple General PMU  │\r\n");
    printf("└────────────────────┘\r\n");
    printf("Version: 1.0.0 beta\r\n");
    printf("Author: Leo Isaac J.\r\n");
    printf("\r\n");
}

int debug_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    size_t offset = 0;
    va_list args;
    va_start(args, fmt);
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"%s[DEBUG][%s:%d (%s)] ",ESCAPE_CODE_DEBUG,filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n%s",console_logger_buffer,ESCAPE_CODE_DEFAULT);
    va_end(args);
    return LOG_ERR_NONE;
}

int info_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    size_t offset = 0;
    va_list args;
    va_start(args, fmt);
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"%s[INFO][%s:%d (%s)] ",ESCAPE_CODE_INFO,filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n%s",console_logger_buffer,ESCAPE_CODE_DEFAULT);
    va_end(args);
    return LOG_ERR_NONE;
}

int warn_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    size_t offset = 0;
    va_list args;
    va_start(args, fmt);
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"%s[WARN][%s:%d (%s)] ",ESCAPE_CODE_WARN,filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n%s",console_logger_buffer,ESCAPE_CODE_DEFAULT);
    va_end(args);
    return LOG_ERR_NONE;
}

int error_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    size_t offset = 0;
    va_list args;
    va_start(args, fmt);
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"%s[ERROR][%s:%d (%s)] ",ESCAPE_CODE_ERROR,filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n%s",console_logger_buffer,ESCAPE_CODE_DEFAULT);
    va_end(args);
    return LOG_ERR_NONE;
}
