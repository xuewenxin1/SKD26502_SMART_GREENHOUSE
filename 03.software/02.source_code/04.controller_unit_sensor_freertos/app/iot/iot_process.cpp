#include "iot_process.hpp"
#include "console_logger.h"
#include "cstring"
#include <cstdint>
#include "ChecksumCalculator.hpp"
#include "config.hpp"
#include "ventilate_service.hpp"
#include "gui.hpp"

extern void IOTService_up_config();
extern void IOTService_up_status();

void process_act_msg(uint8_t act, uint16_t param){
    LOG_INFO("MQTT ACT(%d,%d)",act,param);
    switch ( act ){
        case 0: {
            VentilateService::force(VentilateService::ForceAction::None);
            break;
        }
        case 1: {
            VentilateService::set_opening_percentage(param / 10);
            break;
        }
        case 2: {
            VentilateService::force(VentilateService::ForceAction::ForceOpen);
            break;
        }
        case 3: {
            VentilateService::force(VentilateService::ForceAction::ForceClose);
            break;
        }
        case 4: {
            IOTService_up_status();
            break;
        }
        case 5: {
            IOTService_up_config();
            break;
        }
    }
}

void process_config_msg(
    uint8_t mode,uint16_t temp_alarm_upper_limit,uint16_t temp_alarm_lower_limit, uint16_t target_temp,uint16_t temp_return_diff_positive,
    uint16_t temp_return_diff_negative, uint16_t temp_compensation_value)
{
    LOG_DEBUG("Config message. (%d,%d,%d,%d,%d,%d,%d)",mode,temp_alarm_upper_limit,temp_alarm_lower_limit,target_temp,temp_return_diff_positive,temp_return_diff_negative,temp_compensation_value);
    Config_t config;
    ConfigService::get_config(config);
    const int old_mode = config.working_mode;
    config.working_mode = mode;
    config.temp_alert_upper_limit = temp_alarm_upper_limit;
    config.temp_alert_lower_limit = temp_alarm_lower_limit;
    config.target_central_temp = target_temp;
    if ( temp_return_diff_positive > 50 ){
        temp_return_diff_positive = 50;
    }
    if ( temp_return_diff_negative > 50 ){
        temp_return_diff_negative = 50;
    }
    config.temp_return_diff_positive = (int)((temp_return_diff_positive / 10) * 10);
    config.temp_return_diff_negative = (int)((temp_return_diff_negative / 10) * 10);
    config.temp_compensation_value = temp_compensation_value;
    LOG_DEBUG("Set config and store.");
    ConfigService::set_config(config);
    ConfigService::store();
    if ( old_mode != config.working_mode ){
        VentilateService::on_mode_changed();
    }
    LOG_INFO("MQTT config update done.");
}

void process_time_and_id(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t id[6]){
    GUI::set_time_and_id(year,month,day,hour,minute,id);
}

void process_iot_down(const char *topic, unsigned char *buffer, unsigned int size){
    char __topic[64] = {0};
    strcpy(__topic,topic);
    strtok(__topic,"/");
    strtok(nullptr,"/");
    strtok(nullptr,"/");
    const char *pack_type = strtok(nullptr,"/");
    LOG_INFO("PACK:%s",pack_type);
    if ( strncmp(pack_type,"act",3) == 0 ){
        /* greenhouse/vid/down/act. */
        uint8_t act = buffer[0];
        uint16_t param = ((buffer[1]) | (buffer[2] << 8));
        process_act_msg(act,param);
    }else if ( strncmp(pack_type,"config",6) == 0 ){
        uint8_t mode = buffer[0];
        uint16_t temp_alarm_upper_limit = buffer[1] | (buffer[2] << 8);
        uint16_t temp_alarm_lower_limit = buffer[3] | (buffer[4] << 8);
        uint16_t target_temp = buffer[5] | (buffer[6] << 8);
        uint16_t temp_return_diff_positive = buffer[7] | (buffer[8] << 8);
        uint16_t temp_return_diff_negative = buffer[9] | (buffer[10] << 8);
        uint16_t temp_compensation_value = buffer[11] | (buffer[12] << 8);
        process_config_msg(mode,temp_alarm_upper_limit,temp_alarm_lower_limit,target_temp,temp_return_diff_positive,temp_return_diff_negative,temp_compensation_value);
    }else if ( strncmp(pack_type,"extra",5) == 0 ){
        process_time_and_id(buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],&buffer[5]);
    }
}
