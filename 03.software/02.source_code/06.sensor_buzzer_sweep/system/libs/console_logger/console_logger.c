#include "console_logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t console_logger_mutex = NULL;
static char console_logger_buffer[CONSOLE_LOGGER_BUFFER_SIZE] = {0};

int console_logger_init(){
    console_logger_mutex = xSemaphoreCreateMutex();
    if ( console_logger_mutex == NULL ){
        return LOG_ERR_ERR;
    }
    return LOG_ERR_NONE;
}

int debug_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    if ( console_logger_mutex == NULL ){
        return LOG_ERR_NOINIT;
    }
    if ( xSemaphoreTake(console_logger_mutex,portMAX_DELAY) != pdPASS ){
        return LOG_ERR_ERR;
    }
    va_list args;
    va_start(args,fmt);
    unsigned int offset = 0;
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"[DEBUG][%s:%d (%s)] ",filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n",console_logger_buffer);
    va_end(args);
    xSemaphoreGive(console_logger_mutex);
    return LOG_ERR_NONE;
}

int info_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    if ( console_logger_mutex == NULL ){
        return LOG_ERR_NOINIT;
    }
    if ( xSemaphoreTake(console_logger_mutex,portMAX_DELAY) != pdPASS ){
        return LOG_ERR_ERR;
    }
    va_list args;
    va_start(args,fmt);
    unsigned int offset = 0;
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"[INFO][%s:%d (%s)] ",filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n",console_logger_buffer);
    va_end(args);
    xSemaphoreGive(console_logger_mutex);
    return LOG_ERR_NONE;
}

int warn_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    if ( console_logger_mutex == NULL ){
        return LOG_ERR_NOINIT;
    }
    if ( xSemaphoreTake(console_logger_mutex,portMAX_DELAY) != pdPASS ){
        return LOG_ERR_ERR;
    }
    va_list args;
    va_start(args,fmt);
    unsigned int offset = 0;
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"[WARN][%s:%d (%s)] ",filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n",console_logger_buffer);
    va_end(args);
    xSemaphoreGive(console_logger_mutex);
    return LOG_ERR_NONE;
}

int error_with_trace(const char *filename,const char *function_name,int line_number,const char *fmt,...){
    if ( console_logger_mutex == NULL ){
        return LOG_ERR_NOINIT;
    }
    if ( xSemaphoreTake(console_logger_mutex,portMAX_DELAY) != pdPASS ){
        return LOG_ERR_ERR;
    }
    va_list args;
    va_start(args,fmt);
    unsigned int offset = 0;
    snprintf(console_logger_buffer,sizeof(console_logger_buffer),"[ERROR][%s:%d (%s)] ",filename,line_number,function_name);
    offset = strlen(console_logger_buffer);
    vsnprintf(console_logger_buffer + offset, sizeof(console_logger_buffer) - offset,fmt,args);
    printf("%s\r\n",console_logger_buffer);
    va_end(args);
    xSemaphoreGive(console_logger_mutex);
    return LOG_ERR_NONE;
}
