#include "config.hpp"
#include "py32l090_hal_rcc.h"
#include "py32l090_hal_flash.h"
#include "cstdint"
#include "console_service.hpp"
#include <cstring>

typedef struct StorageBlock {
    char magic_number[4];
    int index;
    Config_t config;
} StorageBlock_t;

static const char MAGIC_NUMBER[4] = {'C','F','1',0};
static Config_t system_config;
static const Config_t default_system_config = {
    .temp_alert_upper_limit = 450,
    .temp_alert_lower_limit = 100,
    .temp_control_mode = TempControlMode_Limit,
    .target_central_temp = 250,
    .temp_return_diff_positive = 50,
    .temp_return_diff_negative = 50,
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
};

#define STORAGE_BLOCK_SIZE sizeof(StorageBlock_t)

#define PAGE_SIZE 512

bool ConfigService::init(){
    StorageBlock_t *storage_block = reinterpret_cast<StorageBlock_t*>(CONFIG_AREA_ADDRESS);
    if ( strncmp((char*)(storage_block->magic_number),MAGIC_NUMBER,4) == 0 ){
        /* 存在配置. */
        LOG_INFO("Config exists. load.");
        system_config = storage_block->config;
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
    system_config = config;
    return true;
}

bool ConfigService::store(){
    uint32_t page_data[PAGE_SIZE / 4] = {0};
    FLASH_EraseInitTypeDef erase_init_config = {0};
    StorageBlock_t *storage_block = reinterpret_cast<StorageBlock_t*>(page_data);
    memcpy(storage_block->magic_number,MAGIC_NUMBER,4);
    storage_block->index = 0,
    storage_block->config = system_config;

    erase_init_config.TypeErase = FLASH_TYPEERASE_PAGEERASE;
    erase_init_config.PageAddress = CONFIG_AREA_ADDRESS;
    erase_init_config.NbPages = 1;
    uint32_t page_error = 0;
    HAL_FLASH_Unlock();
    HAL_FLASH_Erase(&erase_init_config,&page_error);
    HAL_FLASH_Unlock();
    HAL_FLASH_PageProgram(CONFIG_AREA_ADDRESS,(uint32_t*)page_data);
    HAL_FLASH_Lock();
    return true;
}

