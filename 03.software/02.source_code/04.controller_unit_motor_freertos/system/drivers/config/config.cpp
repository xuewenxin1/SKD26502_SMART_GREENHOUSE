#include "config.hpp"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_flash.h"
#include "py32f071_hal_cortex.h"
#include "cstdint"
#include "console_logger.h"
#include <cstring>
#include "FreeRTOS.h"
#include "task.h"

typedef struct StorageBlock {
    char magic_number[4];
    int index;
    Config_t config;
} StorageBlock_t;

static const char MAGIC_NUMBER[4] = {'C','F','2',0};
static Config_t system_config;
static const Config_t default_system_config = {
    .temp_alert_upper_limit = 450,
    .temp_alert_lower_limit = 100,
    .temp_control_mode = TempControlMode_Limit,
    .target_central_temp = 250,
    .temp_return_diff_positive = 0,
    .temp_return_diff_negative = 0,
    .temp_compensation_value = 0,
    .ventilator_timing_config = {
        [0] = {
            .time = 9,
            .opening_percentage = 50,
        },
        [1] = {
            .time = 11,
            .opening_percentage = 100,
        },
        [2] = {
            .time = 16,
            .opening_percentage = 50,
        },
        [3] = {
            .time = 17,
            .opening_percentage = 0,
        },
    },
    .motor_stroke_time = 30,
    .data_upload_interval = 5,
    .local_alarm_switch = 1,
    .working_mode = WorkingMode::WorkingMode_Manual,
    .device_valid = 1,
    .last_opening_percentage = 0,
};

#define STORAGE_BLOCK_SIZE sizeof(StorageBlock_t)

#define PAGE_SIZE 512

static void clamp_stroke_time(Config_t &config){
    if ( config.motor_stroke_time < MOTOR_STROKE_TIME_MIN ){
        config.motor_stroke_time = MOTOR_STROKE_TIME_MIN;
    }else if ( config.motor_stroke_time > MOTOR_STROKE_TIME_MAX ){
        config.motor_stroke_time = MOTOR_STROKE_TIME_MAX;
    }
}

/* 回差只允许 0-5℃ 整数，内部按 0.1℃ 存储. */
static void clamp_hysteresis(Config_t &config){
    int pos = config.temp_return_diff_positive / 10;
    int neg = config.temp_return_diff_negative / 10;
    if ( pos < 0 ){
        pos = 0;
    }else if ( pos > 5 ){
        pos = 5;
    }
    if ( neg < 0 ){
        neg = 0;
    }else if ( neg > 5 ){
        neg = 5;
    }
    config.temp_return_diff_positive = pos * 10;
    config.temp_return_diff_negative = neg * 10;
}

/* S0 数据上报间隔：5-30 秒，写入 Flash. */
static void clamp_upload_interval(Config_t &config){
    if ( config.data_upload_interval < DATA_UPLOAD_INTERVAL_MIN ){
        config.data_upload_interval = DATA_UPLOAD_INTERVAL_MIN;
    }else if ( config.data_upload_interval > DATA_UPLOAD_INTERVAL_MAX ){
        config.data_upload_interval = DATA_UPLOAD_INTERVAL_MAX;
    }
}

bool ConfigService::init(){
    StorageBlock_t *storage_block = reinterpret_cast<StorageBlock_t*>(CONFIG_AREA_ADDRESS);
    if ( strncmp((char*)(storage_block->magic_number),MAGIC_NUMBER,4) == 0 ){
        /* 存在配置. */
        LOG_INFO("Config exists. load.");
        system_config = storage_block->config;
        clamp_stroke_time(system_config);
        clamp_hysteresis(system_config);
        clamp_upload_interval(system_config);
    }else{
        LOG_WARN("Config not exists. Use default config.");
        system_config = default_system_config;
        ConfigService::store();
    }
    return true;
}

bool ConfigService::get_config(Config_t &config){
    config = system_config;
    return true;
}

bool ConfigService::set_config(Config_t config){
    clamp_stroke_time(config);
    clamp_hysteresis(config);
    clamp_upload_interval(config);
    system_config = config;
    return true;
}

bool ConfigService::store(){
    static uint32_t page_data[PAGE_SIZE / 4] = {0};
    FLASH_EraseInitTypeDef erase_init_config = {0};
    StorageBlock_t *storage_block = reinterpret_cast<StorageBlock_t*>(page_data);
    const bool scheduler_running = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);

    memcpy(storage_block->magic_number,MAGIC_NUMBER,4);
    storage_block->index = 0;
    storage_block->config = system_config;

    erase_init_config.TypeErase = FLASH_TYPEERASE_PAGEERASE;
    erase_init_config.PageAddress = CONFIG_AREA_ADDRESS;
    erase_init_config.NbPages = 1;
    uint32_t page_error = 0;

    /* 不可关总中断：Flash 等待依赖 HAL_GetTick/SysTick。
     * 关中断后写配置（设置回差退出等）会导致 ADC/任务超时卡死，温度不再更新. */
    if ( scheduler_running ){
        vTaskSuspendAll();
    }
    HAL_FLASH_Unlock();
    if ( HAL_FLASH_Erase(&erase_init_config,&page_error) == HAL_OK ){
        HAL_FLASH_PageProgram(CONFIG_AREA_ADDRESS,(uint32_t*)page_data);
    }
    HAL_FLASH_Lock();
    if ( scheduler_running ){
        (void)xTaskResumeAll();
    }
    return true;
}

