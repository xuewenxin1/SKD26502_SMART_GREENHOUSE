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
#include "py32f071_hal_cortex.h"

/* Main 周期 100ms；GUI 心跳停满该拍数则软复位. */
#define GUI_HEARTBEAT_STALL_LOOPS 100u /* 10s */

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

    /* 启动服务：IOT 先建，堆只剩约 1KB 时后建会失败导致无法联网. */
    if ( IOTService::start() != true ){
        LOG_ERROR("IOT service start failed.");
    }

    if ( GUI::start() != true ){
        LOG_ERROR("GUI service start failed.");
        return -2;
    }

    if ( Env::start() != true ){
        LOG_ERROR("Env service start failed.");
        return -2;
    }

    size_t free_heap_size = xPortGetFreeHeapSize();

    LOG_INFO("System startup complete.");
    LOG_INFO("Free Heap Size: %d.", free_heap_size);

    unsigned int last_gui_hb = GUI::heartbeat_tick();
    unsigned int gui_stall_loops = 0;
    bool gui_hb_seen = false;

    while ( true ){
        /* 先电机后采样：避免 AHT 忙等拖住本拍驱动. */
        VentilateService::eventloop();
        Env::eventloop();

        /* GUI 刷屏心跳：首次见到后再计时，停满 10s 停电机并软复位. */
        {
            unsigned int hb = GUI::heartbeat_tick();
            if ( hb != last_gui_hb ){
                last_gui_hb = hb;
                gui_stall_loops = 0;
                gui_hb_seen = true;
            }else if ( gui_hb_seen ){
                gui_stall_loops++;
                if ( gui_stall_loops >= GUI_HEARTBEAT_STALL_LOOPS ){
                    LOG_ERROR("GUI heartbeat stall 10s, soft reset.");
                    VentilateService::force(VentilateService::ForceAction::None);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    HAL_NVIC_SystemReset();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return 0;
}
