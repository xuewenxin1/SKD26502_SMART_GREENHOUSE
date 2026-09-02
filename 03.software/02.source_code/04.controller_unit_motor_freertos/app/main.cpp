#include "FreeRTOS.h"
#include "task.h"
#include "console_logger.h"
#include "config.hpp"
#include "env.hpp"
#include "gui.hpp"
#include "lte_service.hpp"
#include "iot_service.hpp"
#include "ventilate_service.hpp"
#include "systimer.hpp"

int main(){
    /* 系统初始化序列. */

    /* 初始化系统定时器. */
    if ( SysTimer::init() != true ){
        LOG_ERROR("Systimer init failed.");
        return -1;
    }else{
        LOG_INFO("Systimer init complete.");
    }

    /* 初始化配置. */
    if ( ConfigService::init() != true ){
        LOG_ERROR("Config service init failed.");
        return -1;
    }else{
        LOG_INFO("Config service init complete.");
    }

    /* 初始化环境监测服务. */
    if ( Env::init() != true ){
        LOG_ERROR("Env service init failed.");
        return -1;
    }else{
        LOG_INFO("Env service init complete.");
    }

    /* 初始化图形界面服务. */
    if ( GUI::init() != true ){
        LOG_ERROR("GUI init failed.");
        return -1;
    }else{
        LOG_INFO("GUI init complete.");
    }

    /* 初始化LTE服务. */
    if ( LTEService::init() != true ){
        LOG_ERROR("LTE service init failed.");
        return -1;
    }else{
        LOG_INFO("LTE service init complete.");
    }

    /* 初始化通风控制服务. */
    if ( VentilateService::init() != true ){
        LOG_ERROR("Ventilate service init failed.");
        return -1;
    }else{
        LOG_INFO("Ventilate service init complete.");
    }

    /* 初始化IOT服务. */
    if ( IOTService::init() != true ){
        LOG_ERROR("IOT service init failed.");
        return -1;
    }else{
        LOG_INFO("IOT service init complete.");
    }

    LOG_INFO("System init complete.");

    /* 启动服务. */
    if ( GUI::start() != true ){
        LOG_ERROR("GUI service start failed.");
        return -2;
    }

    if ( Env::start() != true ){
        LOG_ERROR("Env service start failed.");
        return -2;
    }

    if ( IOTService::start() != true ){
        LOG_ERROR("IOT service start failed.");
    }

    size_t free_heap_size = xPortGetFreeHeapSize();

    LOG_INFO("System startup complete.");
    LOG_INFO("Free Heap Size: %d.", free_heap_size);

    while ( true ){
        VentilateService::eventloop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return 0;
}
