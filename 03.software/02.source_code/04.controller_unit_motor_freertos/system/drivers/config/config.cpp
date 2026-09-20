#include "config.hpp"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_flash.h"
#include "py32f071_hal_cortex.h"
#include "cstdint"
#include "console_logger.h"
#include <cstring>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

typedef struct StorageBlock {
    char magic_number[4];
    int index;
    Config_t config;
} StorageBlock_t;

static const char MAGIC_NUMBER[4] = {'C','F','3',0};
static const char MAGIC_NUMBER_LEGACY[4] = {'C','F','2',0};
static Config_t system_config;
static volatile bool store_dirty = false;
static SemaphoreHandle_t hw_mutex = nullptr;

static void hw_lock(void){
    if ( (hw_mutex != nullptr) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) ){
        xSemaphoreTake(hw_mutex, portMAX_DELAY);
    }
}

static void hw_unlock(void){
    if ( (hw_mutex != nullptr) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) ){
        xSemaphoreGive(hw_mutex);
    }
}

bool ConfigService::lock_hw(void){
    hw_lock();
    return true;
}

bool ConfigService::try_lock_hw(void){
    if ( (hw_mutex == nullptr) || (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) ){
        return true;
    }
    return (xSemaphoreTake(hw_mutex, 0) == pdTRUE);
}

bool ConfigService::unlock_hw(void){
    hw_unlock();
    return true;
}
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
    .motor_stroke_time = 3, /* 默认 3 圈. */
    .data_upload_interval = 5,
    .local_alarm_switch = 1,
    .working_mode = WorkingMode::WorkingMode_Manual,
    .device_valid = 1,
    .last_opening_percentage = 0,
    .temp_vent_upper_limit = 250, /* P5 默认 25.0℃（×10）. */
    .temp_vent_lower_limit = 200, /* P6 默认 20.0℃（×10）. */
    .motor_turn_seconds = MOTOR_TURN_SECONDS_DEFAULT, /* C2 默认 25 秒/圈. */
    .last_motor_timer_cnt = 0,
};

#define STORAGE_BLOCK_SIZE sizeof(StorageBlock_t)

#define PAGE_SIZE 512

static void clamp_turn_seconds(Config_t &config){
    if ( (config.motor_turn_seconds < MOTOR_TURN_SECONDS_MIN)
         || (config.motor_turn_seconds > MOTOR_TURN_SECONDS_MAX) ){
        config.motor_turn_seconds = MOTOR_TURN_SECONDS_DEFAULT;
    }
}

static void clamp_stroke_time(Config_t &config){
    clamp_turn_seconds(config);
    /* 旧固件曾把 C1 存成秒(常见 30~300)。若明显偏大且像“秒”而非圈数，按 C2 换算.
     * 协议圈数上限 999；超过 999 一律钳到最大. */
    if ( config.motor_stroke_time > MOTOR_STROKE_TURNS_MAX ){
        config.motor_stroke_time = MOTOR_STROKE_TURNS_MAX;
    }else if ( config.motor_stroke_time < MOTOR_STROKE_TURNS_MIN ){
        config.motor_stroke_time = MOTOR_STROKE_TURNS_MIN;
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

/* P4 温度补偿/校准：−9.0～+9.0℃（内部 ×10）. */
static void clamp_temp_compensation(Config_t &config){
    if ( config.temp_compensation_value < -90 ){
        config.temp_compensation_value = -90;
    }else if ( config.temp_compensation_value > 90 ){
        config.temp_compensation_value = 90;
    }
}

/* S0 数据上报间隔：5-30 秒，写入 Flash. */
static void clamp_upload_interval(Config_t &config){
    if ( config.data_upload_interval < DATA_UPLOAD_INTERVAL_MIN ){
        config.data_upload_interval = DATA_UPLOAD_INTERVAL_MIN;
    }else if ( config.data_upload_interval > DATA_UPLOAD_INTERVAL_MAX ){
        config.data_upload_interval = DATA_UPLOAD_INTERVAL_MAX;
    }
}

/* P5/P6：协议×10（0.1℃），整度 0～55，上限至少比下限高 1℃；非法则默认 25/20.
 * 兼容曾按整度存的旧值（≤55）. */
static void clamp_vent_limits(Config_t &config){
    int hi = config.temp_vent_upper_limit;
    int lo = config.temp_vent_lower_limit;
    if ( hi <= TEMP_SETTING_MAX ){
        hi = hi * 10;
    }
    if ( lo <= TEMP_SETTING_MAX ){
        lo = lo * 10;
    }
    int hi_c = hi / 10;
    int lo_c = lo / 10;
    if ( (hi_c < TEMP_SETTING_MIN) || (hi_c > TEMP_SETTING_MAX)
         || (lo_c < TEMP_SETTING_MIN) || (lo_c > TEMP_SETTING_MAX) ){
        config.temp_vent_upper_limit = 250;
        config.temp_vent_lower_limit = 200;
        return;
    }
    if ( lo_c >= hi_c ){
        if ( lo_c >= TEMP_SETTING_MAX ){
            lo_c = TEMP_SETTING_MAX - 1;
            hi_c = TEMP_SETTING_MAX;
        }else{
            hi_c = lo_c + 1;
        }
    }
    config.temp_vent_upper_limit = hi_c * 10;
    config.temp_vent_lower_limit = lo_c * 10;
}

/* 上电位置：优先用行程计时（约 1ms/tick）；非法则由开度反推. */
static void clamp_last_position(Config_t &config){
    int op = config.last_opening_percentage;
    if ( op < 0 ){
        op = 0;
    }else if ( op > 100 ){
        op = 100;
    }
    config.last_opening_percentage = op;

    int turns = config.motor_stroke_time;
    int sec = config.motor_turn_seconds;
    if ( turns < MOTOR_STROKE_TURNS_MIN ){
        turns = MOTOR_STROKE_TURNS_MIN;
    }else if ( turns > MOTOR_STROKE_TURNS_MAX ){
        turns = MOTOR_STROKE_TURNS_MAX;
    }
    if ( (sec < MOTOR_TURN_SECONDS_MIN) || (sec > MOTOR_TURN_SECONDS_MAX) ){
        sec = MOTOR_TURN_SECONDS_DEFAULT;
    }
    const int stroke_sec = turns * sec;
    /* 满行程计时 ≈ stroke_sec×1000（与 hold_opening: opening×10×stroke_sec 一致）. */
    const int max_cnt = stroke_sec * 1000 + stroke_sec * 100;
    int cnt = config.last_motor_timer_cnt;
    if ( (cnt < 0) || (cnt > max_cnt) ){
        cnt = (int)((double)op * 10.0 * (double)stroke_sec + 0.5);
    }
    config.last_motor_timer_cnt = cnt;
}

bool ConfigService::init(){
    if ( hw_mutex == nullptr ){
        hw_mutex = xSemaphoreCreateMutex();
    }
    StorageBlock_t *storage_block = reinterpret_cast<StorageBlock_t*>(CONFIG_AREA_ADDRESS);
    bool need_upgrade_store = false;
    if ( strncmp((char*)(storage_block->magic_number),MAGIC_NUMBER,4) == 0 ){
        LOG_INFO("Config exists. load.");
        system_config = storage_block->config;
    }else if ( strncmp((char*)(storage_block->magic_number),MAGIC_NUMBER_LEGACY,4) == 0 ){
        /* 旧版无 last_motor_timer_cnt：按开度重建计时. */
        LOG_INFO("Config legacy. migrate timer.");
        system_config = storage_block->config;
        system_config.last_motor_timer_cnt = -1;
        need_upgrade_store = true;
    }else{
        LOG_WARN("Config not exists. Use default config.");
        system_config = default_system_config;
        need_upgrade_store = true;
    }
    clamp_stroke_time(system_config);
    clamp_hysteresis(system_config);
    clamp_temp_compensation(system_config);
    clamp_upload_interval(system_config);
    clamp_vent_limits(system_config);
    clamp_last_position(system_config);
    if ( need_upgrade_store ){
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
    clamp_temp_compensation(config);
    clamp_upload_interval(config);
    clamp_vent_limits(config);
    clamp_last_position(config);
    system_config = config;
    return true;
}

bool ConfigService::request_store(){
    store_dirty = true;
    return true;
}

bool ConfigService::flush_store(){
    if ( store_dirty == false ){
        return false;
    }
    return ConfigService::store();
}

bool ConfigService::store(){
    static uint32_t page_data[PAGE_SIZE / 4] = {0};
    FLASH_EraseInitTypeDef erase_init_config = {0};
    StorageBlock_t *storage_block = reinterpret_cast<StorageBlock_t*>(page_data);

    store_dirty = false;
    memcpy(storage_block->magic_number,MAGIC_NUMBER,4);
    storage_block->index = 0;
    storage_block->config = system_config;

    erase_init_config.TypeErase = FLASH_TYPEERASE_PAGEERASE;
    erase_init_config.PageAddress = CONFIG_AREA_ADDRESS;
    erase_init_config.NbPages = 1;
    uint32_t page_error = 0;

    /* 与 LCD 软件 I2C 互斥；不要 vTaskSuspendAll / 关总中断. */
    hw_lock();
    HAL_FLASH_Unlock();
    if ( HAL_FLASH_Erase(&erase_init_config,&page_error) == HAL_OK ){
        HAL_FLASH_PageProgram(CONFIG_AREA_ADDRESS,(uint32_t*)page_data);
    }
    HAL_FLASH_Lock();
    hw_unlock();
    /* 让出 CPU，避免连续落盘饿死刷屏. */
    if ( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ){
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}
