#include "env.hpp"
#include "aht2415_service.hpp"
#include "adc.hpp"
#include "rain_sensor_service.hpp"
#include "console_service.hpp"
#include "ntc_service.hpp"

static struct EnvStatus {
    struct {
        double temperature;
        double humidity;
        bool rain_status;
    } value;
    struct {
        bool temperature;
        bool humidity;
    } valid_status;
} env_status = {
    .value = {
        .temperature = 20.1,
        .humidity = 20,
        .rain_status = false,
    },
    .valid_status = {
        .temperature = false,
        .humidity = false,
    },
};

bool Env::init(){
    LOG_INFO("Init env service.");
    /* 初始化ADC服务. */
    if ( ADC::init() != true ){
        LOG_ERROR("ADC init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("ADC init success.");
    }
    /* 初始化AHT2415. */
    if ( AHT2415_Service::init() != true ){
        LOG_ERROR("AHT2415 init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("AHT2415 init success.");
    }
    /* 初始化NTC. */
    if ( NTC_Service::init() != true ){
        LOG_ERROR("NTC init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("NTC init success.");
    }
    /* 初始化雨雪传感器. */
    if ( RainSensorService::init() != true ){
        LOG_ERROR("Rain sensor init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("Rain sensor init success.");
    }
    LOG_INFO("Env service init success.");
    return true;
}

void Env::eventloop(){
    /* 更新温湿度. */
    if ( AHT2415_Service::get(env_status.value.temperature,env_status.value.humidity) == true ){
        env_status.valid_status.temperature = true;
        env_status.valid_status.humidity = true;
    }else{
        /* AHT2415无法获取数据，试图从NTC获取. */
        if ( NTC_Service::get_temperature(env_status.value.temperature) == true ){
            env_status.valid_status.temperature = true;
        }else{
            env_status.valid_status.temperature = false;
        }
        env_status.valid_status.humidity = false;
    }
    /* 更新雨滴状态. */
    env_status.value.rain_status = RainSensorService::get_rain_status();
}

bool Env::get_temperature(double &temperature){
    if (env_status.valid_status.temperature != true ){
        return false;
    }
    temperature = env_status.value.temperature;
    return true;
}

bool Env::get_humidity(double &humidity){
    if ( env_status.valid_status.humidity != true ){
        return false;
    }
    humidity = env_status.value.humidity;
    return true;
}

bool Env::get_rain_status(bool &rain_status){
    rain_status = env_status.value.rain_status;
    return true;
}

bool Env::get_error_code(unsigned int &error_code){
    
}