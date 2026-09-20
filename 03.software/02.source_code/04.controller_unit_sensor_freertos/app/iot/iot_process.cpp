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
extern void IOTService_request_up_status();
extern void IOTService_request_up_config();

void process_act_msg(uint8_t act, uint16_t param){
    LOG_INFO("MQTT ACT(%d,%d)",act,param);
    switch ( act ){
        case 0: {
            VentilateService::force(VentilateService::ForceAction::None);
            IOTService_request_up_status();
            break;
        }
        case 1: {
            /* param 为精确目标圈数（不×10）；内部按 C1 算小数开度. */
            Config_t cfg;
            ConfigService::get_config(cfg);
            int stroke = cfg.motor_stroke_time;
            if ( stroke < MOTOR_STROKE_TURNS_MIN ){
                stroke = MOTOR_STROKE_TURNS_MIN;
            }else if ( stroke > MOTOR_STROKE_TURNS_MAX ){
                stroke = MOTOR_STROKE_TURNS_MAX;
            }
            int turns = (int)param;
            if ( turns < 0 ){
                turns = 0;
            }
            if ( turns > stroke ){
                turns = stroke;
            }
            LOG_INFO("MQTT set turns %d (C1=%d C2=%d).",
                     turns, stroke, cfg.motor_turn_seconds);
            if ( VentilateService::set_target_turns(turns) == false ){
                LOG_WARN("MQTT set turns rejected (ALIGN/calibrate).");
            }
            IOTService_request_up_status();
            break;
        }
        case 2: {
            if ( VentilateService::force(VentilateService::ForceAction::ForceOpen) == false ){
                LOG_WARN("MQTT force open rejected.");
            }
            IOTService_request_up_status();
            break;
        }
        case 3: {
            if ( VentilateService::force(VentilateService::ForceAction::ForceClose) == false ){
                LOG_WARN("MQTT force close rejected.");
            }
            IOTService_request_up_status();
            break;
        }
        case 4: {
            IOTService_request_up_status();
            break;
        }
        case 5: {
            IOTService_request_up_config();
            break;
        }
    }
}

static int clamp_temp_compensation(int value){
    if ( value < -90 ){
        return -90;
    }
    if ( value > 90 ){
        return 90;
    }
    return value;
}

void process_config_msg(
    uint8_t mode,uint16_t temp_alarm_upper_limit,uint16_t temp_alarm_lower_limit, uint16_t target_temp,uint16_t temp_return_diff_positive,
    uint16_t temp_return_diff_negative, int16_t temp_compensation_value,
    uint16_t stroke_turns, uint16_t turn_seconds, uint16_t vent_upper, uint16_t vent_lower,
    bool has_ext_fields)
{
    LOG_DEBUG("Config message. (%d,%d,%d,%d,%d,%d,%d)",mode,temp_alarm_upper_limit,temp_alarm_lower_limit,target_temp,temp_return_diff_positive,temp_return_diff_negative,(int)temp_compensation_value);
    Config_t config;
    ConfigService::get_config(config);
    const int old_mode = config.working_mode;
    const int old_stroke = config.motor_stroke_time;
    const int old_turn_sec = config.motor_turn_seconds;
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
    config.temp_compensation_value = clamp_temp_compensation((int)temp_compensation_value);
    if ( has_ext_fields ){
        config.motor_stroke_time = (int)stroke_turns;
        config.motor_turn_seconds = (int)turn_seconds;
        /* P5/P6 协议×10（如 250=25.0℃）. */
        config.temp_vent_upper_limit = (int)vent_upper;
        config.temp_vent_lower_limit = (int)vent_lower;
    }
    if ( (mode != WorkingMode_Manual)
         && (mode != WorkingMode_Auto)
         && (mode != WorkingMode_Timing) ){
        LOG_WARN("MQTT mode %u invalid, keep %d.", (unsigned int)mode, old_mode);
        config.working_mode = old_mode;
    }
    LOG_DEBUG("Set config (defer flash).");
    ConfigService::set_config(config);
    ConfigService::request_store();
    if ( old_mode != config.working_mode ){
        VentilateService::on_mode_changed();
    }
    ConfigService::get_config(config);
    if ( has_ext_fields
         && ( (config.motor_stroke_time != old_stroke)
              || (config.motor_turn_seconds != old_turn_sec) ) ){
        VentilateService::home_after_stroke_change();
    }
    LOG_INFO("MQTT config update done. mode %d C1=%d C2=%d P5=%d P6=%d P4=%d.",
             config.working_mode, config.motor_stroke_time, config.motor_turn_seconds,
             config.temp_vent_upper_limit / 10, config.temp_vent_lower_limit / 10,
             config.temp_compensation_value);
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
        uint8_t act = buffer[0];
        uint16_t param = ((buffer[1]) | (buffer[2] << 8));
        process_act_msg(act,param);
    }else if ( strncmp(pack_type,"config",6) == 0 ){
        if ( size < 15 ){
            LOG_WARN("MQTT config too short %u.", size);
            return;
        }
        uint8_t mode = buffer[0];
        uint16_t temp_alarm_upper_limit = buffer[1] | (buffer[2] << 8);
        uint16_t temp_alarm_lower_limit = buffer[3] | (buffer[4] << 8);
        uint16_t target_temp = buffer[5] | (buffer[6] << 8);
        uint16_t temp_return_diff_positive = buffer[7] | (buffer[8] << 8);
        uint16_t temp_return_diff_negative = buffer[9] | (buffer[10] << 8);
        int16_t temp_compensation_value = (int16_t)(buffer[11] | (buffer[12] << 8));
        /* buffer[13] device_valid：温湿度计忽略 */
        uint16_t stroke_turns = 0;
        uint16_t turn_seconds = 0;
        uint16_t vent_upper = 0;
        uint16_t vent_lower = 0;
        bool has_ext = false;
        if ( size >= 22 ){
            stroke_turns = buffer[14] | (buffer[15] << 8);
            turn_seconds = buffer[16] | (buffer[17] << 8);
            vent_upper = buffer[18] | (buffer[19] << 8);
            vent_lower = buffer[20] | (buffer[21] << 8);
            has_ext = true;
            LOG_INFO("MQTT config ext C1=%u C2=%u P5=%u P6=%u size=%u",
                     (unsigned)stroke_turns, (unsigned)turn_seconds,
                     (unsigned)vent_upper, (unsigned)vent_lower, size);
        }else{
            LOG_WARN("MQTT config no ext fields, size %u (need >=22).", size);
        }
        if ( size >= 24 ){
            temp_compensation_value = (int16_t)(buffer[22] | (buffer[23] << 8));
            LOG_INFO("MQTT config temp cal(P4)=%d size=%u",
                     (int)temp_compensation_value, size);
        }
        process_config_msg(mode,temp_alarm_upper_limit,temp_alarm_lower_limit,target_temp,
                           temp_return_diff_positive,temp_return_diff_negative,temp_compensation_value,
                           stroke_turns, turn_seconds, vent_upper, vent_lower, has_ext);
    }else if ( strncmp(pack_type,"extra",5) == 0 ){
        process_time_and_id(buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],&buffer[5]);
    }
}
