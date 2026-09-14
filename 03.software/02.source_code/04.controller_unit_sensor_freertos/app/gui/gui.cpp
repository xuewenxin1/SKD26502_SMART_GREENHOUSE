#include "gui.hpp"
#include "indev.hpp"
#include "console_logger.h"
#include "display.hpp"
#include "env.hpp"
#include "config.hpp"
#include "ventilate_service.hpp"
#include "end_stop_policy.hpp"
#include "iot_service.hpp"
#include "FreeRTOS.h"
#include "task.h"

/* 1 = 显示 D1-D4 定时设置；0 = 暂时隐藏. */
#ifndef GUI_ENABLE_D1_D4_SETTING
#define GUI_ENABLE_D1_D4_SETTING 0
#endif
/* 1 = 显示 P0/P3；0 = 暂时隐藏. */
#ifndef GUI_ENABLE_P0_P3_SETTING
#define GUI_ENABLE_P0_P3_SETTING 0
#endif

namespace GUI {

enum SettingItem {
    SettingItem_A1,
    SettingItem_A2,
    SettingItem_P0,
    SettingItem_P1,
    SettingItem_P2,
    SettingItem_P3,
    SettingItem_P4,
    SettingItem_P5,
    SettingItem_P6,
    SettingItem_D1,
    SettingItem_D2,
    SettingItem_D3,
    SettingItem_D4,
    SettingItem_C1,
    SettingItem_C2,
    SettingItem_S0,
    SettingItem_S2,
    SettingItem_Count,
};

static const char setting_item_codes[][2] = { 
    [SettingItem_A1] = {'a','1'},
    [SettingItem_A2] = {'a','2'},
    [SettingItem_P0] = {'p','0'},
    [SettingItem_P1] = {'p','1'},
    [SettingItem_P2] = {'p','2'},
    [SettingItem_P3] = {'p','3'},
    [SettingItem_P4] = {'p','4'},
    [SettingItem_P5] = {'p','5'},
    [SettingItem_P6] = {'p','6'},
    [SettingItem_D1] = {'d','1'},
    [SettingItem_D2] = {'d','2'},
    [SettingItem_D3] = {'d','3'},
    [SettingItem_D4] = {'d','4'},
    [SettingItem_C1] = {'c','1'},
    [SettingItem_C2] = {'c','2'},
    [SettingItem_S0] = {'s','0'},
    [SettingItem_S2] = {'s','2'},
};

Page current_page = Page::Home;
bool config_modified_flag = false;
static SettingItem curr_setting_item = SettingItem_A1;
static Config_t config;
static Config_t config_on_enter;
static unsigned int blink_count = 0;
static unsigned int anim_count = 0;
static bool blink_flag = false;
#if GUI_ENABLE_D1_D4_SETTING
static int ventilator_timing_config_sub_index = 0;
#endif
static int setting_page_auto_exit_count = 0;
static bool has_time_and_id = false;
static uint8_t g_year;
static uint8_t g_month;
static uint8_t g_day;
static uint8_t g_hour;
static uint8_t g_minute;
static uint8_t g_id[6];

void set_time_and_id(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t id[6]){
    g_year = year;
    g_month = month;
    g_day = day;
    g_hour = hour;
    g_minute = minute;
    for ( int n = 0; n < 6; n++ ){
        g_id[n] = id[n];
    }
    has_time_and_id = true;
}

bool refresh_home_page(){
    static unsigned int cnt = 0;
    static bool show_time = false;
    Config_t config;
    ConfigService::get_config(config);
    /* 清屏. */
    Display::clear();

    if ( cnt % 10 == 0 ){
        show_time = !show_time;
    }

    /* 点亮装饰线. */
    Display::set_pixel(Display::PIX_BAR1,1);
    Display::set_pixel(Display::PIX_BAR2,1);

    Motor::State motor_state = Motor::State::IDLE;
    VentilateService::get_motor_state(motor_state);
    if ( motor_state == Motor::State::ERROR_OVC ){
        Display::set_pixel(Display::PIX_ERR_SYM,1);
    }

    /* 显示数据通信服务. */
    if ( IOTService::get_network_status() ){
        Display::set_pixel(Display::PIX_DATA_SYM,1);
        Display::set_pixel(Display::PIX_DATA_LEVEL_0,1);
        Display::set_pixel(Display::PIX_DATA_LEVEL_1,1);
        Display::set_pixel(Display::PIX_DATA_LEVEL_2,1);
        Display::set_pixel(Display::PIX_DATA_LEVEL_3,1);
    }

    /* 显示工作模式. */
    if ( config.working_mode == WorkingMode_Manual ){
        Display::set_pixel(Display::PIX_MODE_MANUAL,1);
    }else if ( config.working_mode == WorkingMode_Auto ){
        Display::set_pixel(Display::PIX_MODE_AUTO,1);
        Display::set_pixel(Display::PIX_WATCHING_SYM,1);
    }else if ( config.working_mode == WorkingMode_Timing ){
        Display::set_pixel(Display::PIX_MODE_TIMING,1);
    }

    /* 显示雨雪状态. */
    bool rain_status = false;
    Env::get_rain_status(rain_status);
    if ( rain_status ){
        Display::set_pixel(Display::PIX_STATUS_RAIN,1);
    }

    /* 温度超出 A1/A2 时点亮告警. */
    if ( Env::is_temp_alarm() ){
        Display::set_pixel(Display::PIX_ALERT,1);
    }

    /* 温湿度计首页不显示 OK、开度、百分号. */

    /* Battery: 1.8V=空，2.1V=满。2.08V 应接近满格. */
    unsigned int battery_percent = 0;
    Env::get_battery_percent(battery_percent);
    Display::set_pixel(Display::PIX_BATTERY_0, 1);
    if ( battery_percent >= 25 ){
        Display::set_pixel(Display::PIX_BATTERY_1, 1);
    }
    if ( battery_percent >= 50 ){
        Display::set_pixel(Display::PIX_BATTERY_2, 1);
    }
    if ( battery_percent >= 70 ){
        Display::set_pixel(Display::PIX_BATTERY_3, 1);
    }

    /* 显示温度信息. */
    int int_value = 0;
    double temp_value = 0.0;
    if ( Env::get_temperature(temp_value) ){
        if ( temp_value < 0.0 ){
            Display::set_pixel(Display::PIX_TEMP_SYM_SUBZERO,1);
            temp_value = -temp_value;
        }
        temp_value = temp_value * 10.0;
        int_value = (int)temp_value;
        Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + (int_value % 1000) / 100);
        Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (int_value % 100) / 10);
        Display::set_num_seg(Display::NUM_SEG_TEMP_S3, '0' + (int_value % 10));
        Display::set_pixel(Display::PIX_TEMP_DOT,1);
        Display::set_pixel(Display::PIX_TEMP_SYM,1);
    }else{
        Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '-');
        Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '-');
        Display::set_num_seg(Display::NUM_SEG_TEMP_S3, '-');
    }
    

    /* 显示湿度信息. */
    double humi_value = 0.0;
    if ( Env::get_humidity(humi_value) ){
        if ( humi_value < 0.0 ){
            humi_value = 0.0;
        }
        int_value = (int)(humi_value * 10);
        Display::set_num_seg(Display::NUM_SEG_HUMI_S1, '0' + (int_value % 1000) / 100);
        Display::set_num_seg(Display::NUM_SEG_HUMI_S2, '0' + (int_value % 100) / 10);
        Display::set_num_seg(Display::NUM_SEG_HUMI_S3, '0' + (int_value % 10));
        Display::set_pixel(Display::PIX_HUMI_DOT,1);
        Display::set_pixel(Display::PIX_HUMI_SYM,1);
    }else{
        Display::set_num_seg(Display::NUM_SEG_HUMI_S1, '-');
        Display::set_num_seg(Display::NUM_SEG_HUMI_S2, '-');
        Display::set_num_seg(Display::NUM_SEG_HUMI_S3, '-');
    }

    /* 显示日期时间. */
    if ( has_time_and_id ){
        if ( show_time ){
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S1, '0' + (g_year % 100) / 10);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S2, '0' + (g_year % 10));
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S3, '0' + (g_month % 100) / 10);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S4, '0' + (g_month % 10));
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S5, '0' + (g_day % 100) / 10);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S6, '0' + (g_day % 10));
            Display::set_pixel(Display::PIX_DATE_YEAR_BAR_1,1);
            Display::set_pixel(Display::PIX_DATE_YEAR_BAR_2,1);
            Display::set_num_seg(Display::NUM_SEG_TIME_S1, '0' + (g_hour % 100) / 10);
            Display::set_num_seg(Display::NUM_SEG_TIME_S2, '0' + (g_hour % 10));
            Display::set_num_seg(Display::NUM_SEG_TIME_S3, '0' + (g_minute % 100) / 10);
            Display::set_num_seg(Display::NUM_SEG_TIME_S4, '0' + (g_minute % 10));
            Display::set_pixel(Display::PIX_TIME_DOT,1); 
        }else{
            /* 显示SN. */
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S1, '0' + g_id[0]);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S2, '0' + g_id[1]);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S3, '0' + g_id[2]);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S4, '0' + g_id[3]);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S5, '0' + g_id[4]);
            Display::set_num_seg(Display::NUM_SEG_DATE_YEAR_S6, '0' + g_id[5]);
            Display::set_pixel(Display::PIX_SN_SYM,1);
        }
    }

    Display::flush();

    cnt = (cnt + 1) % 1000;

    return true;
}

bool change_page(Page target){
    switch ( target ){
        case Page::Home: {
            LOG_DEBUG("Enter Home page.");
            if ( GUI::current_page == Page::MotorCali ){
                /* 自动校准已把行程写入 C1，回读以免设置页仍显示旧值. */
                ConfigService::get_config(config);
                GUI::config_modified_flag = false;
            }else if ( config_modified_flag ){
                Config_t live_config;
                ConfigService::get_config(live_config);
                const int old_stroke = live_config.motor_stroke_time;
                const int old_turn_sec = live_config.motor_turn_seconds;
                if ( config.motor_stroke_time == config_on_enter.motor_stroke_time ){
                    config.motor_stroke_time = live_config.motor_stroke_time;
                }
                if ( config.motor_turn_seconds == config_on_enter.motor_turn_seconds ){
                    config.motor_turn_seconds = live_config.motor_turn_seconds;
                }
                if ( config.temp_vent_upper_limit == config_on_enter.temp_vent_upper_limit ){
                    config.temp_vent_upper_limit = live_config.temp_vent_upper_limit;
                }
                if ( config.temp_vent_lower_limit == config_on_enter.temp_vent_lower_limit ){
                    config.temp_vent_lower_limit = live_config.temp_vent_lower_limit;
                }
                if ( config.temp_alert_upper_limit == config_on_enter.temp_alert_upper_limit ){
                    config.temp_alert_upper_limit = live_config.temp_alert_upper_limit;
                }
                if ( config.temp_alert_lower_limit == config_on_enter.temp_alert_lower_limit ){
                    config.temp_alert_lower_limit = live_config.temp_alert_lower_limit;
                }
                config.working_mode = live_config.working_mode;
                config.last_opening_percentage = live_config.last_opening_percentage;
                config.last_motor_timer_cnt = live_config.last_motor_timer_cnt;
                ConfigService::set_config(config);
                ConfigService::store();
                GUI::config_modified_flag = false;
                if ( (config.motor_stroke_time != old_stroke)
                     || (config.motor_turn_seconds != old_turn_sec) ){
                    VentilateService::home_after_stroke_change();
                }
            }
            GUI::current_page = Page::Home;
            break;
        }
        case Page::Setting: {
            /* 在主页下长按设置键进入设置页面. */
            LOG_DEBUG("Enter setting page.");
            ConfigService::get_config(config);
            config_on_enter = config;
            GUI::config_modified_flag = false;
            GUI::current_page = Page::Setting;
            GUI::setting_page_auto_exit_count = 0;
            break;
        }
        case Page::MotorCali: {
            LOG_DEBUG("Enter motor cali page.");
            /* 先保存其它已改参数，避免校准结束后被设置页旧快照覆盖 C1. */
            if ( config_modified_flag ){
                Config_t live_config;
                ConfigService::get_config(live_config);
                config.working_mode = live_config.working_mode;
                config.last_opening_percentage = live_config.last_opening_percentage;
                config.last_motor_timer_cnt = live_config.last_motor_timer_cnt;
                ConfigService::set_config(config);
                ConfigService::store();
                GUI::config_modified_flag = false;
            }
            VentilateService::calibrate();
            GUI::current_page = Page::MotorCali;
            anim_count = 0;
            break;
        }
    }
    return true;
}

bool refresh_setting_page(){
    if ( GUI::setting_page_auto_exit_count < 150 ){
        GUI::setting_page_auto_exit_count++;
    }else{
        GUI::change_page(Page::Home);
        GUI::setting_page_auto_exit_count = 0;
    }
    /* 清屏. */
    Display::clear();

    /* 显示当前的设置代号 */
    const char *item_code = setting_item_codes[curr_setting_item];
    Display::set_num_seg(Display::NUM_SEG_HUMI_S1, item_code[0]);
    Display::set_num_seg(Display::NUM_SEG_HUMI_S2, item_code[1]);

    /* 根据当前设置项显示数据. */
    if ( SettingItem_A1 == curr_setting_item ){
        if ( blink_flag ){
            /* 当前报警上限. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.temp_alert_upper_limit % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.temp_alert_upper_limit % 100) / 10 );
        }
    }else if ( SettingItem_A2 == curr_setting_item ){
        if ( blink_flag ){
            /* 当前报警下限. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.temp_alert_lower_limit % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.temp_alert_lower_limit % 100) / 10 );
        }
    }else if ( SettingItem_P0 == curr_setting_item ){
        if ( blink_flag ){
            /* 当前放风机温控模式. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.temp_control_mode % 100) / 10) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.temp_control_mode % 10) );
        }
    }else if ( SettingItem_P1 == curr_setting_item ){
        if ( blink_flag ){
            /* 中心适宜目标温度. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.target_central_temp % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.target_central_temp % 100) / 10 );
        }
    }else if ( SettingItem_P2 == curr_setting_item ){
        if ( blink_flag ){
            /* 温度正回差. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.temp_return_diff_positive % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.temp_return_diff_positive % 100) / 10 );
        }
    }else if ( SettingItem_P3 == curr_setting_item ){
        if ( blink_flag ){
            /* 温度负回差. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.temp_return_diff_negative % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.temp_return_diff_negative % 100) / 10 );
        }
    }else if ( SettingItem_P4 == curr_setting_item ){
        if ( blink_flag ){
            int value = config.temp_compensation_value;
            if ( value < 0 ){
                Display::set_pixel(Display::PIX_TEMP_SYM_SUBZERO,1);
                value = -value;
            }
            /* 温度补偿值. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((value % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (value % 100) / 10 );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S3, '0' + (value % 10) );
            Display::set_pixel(Display::PIX_TEMP_DOT,1);
        }
    }else if ( SettingItem_P5 == curr_setting_item ){
        if ( blink_flag ){
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.temp_vent_upper_limit % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.temp_vent_upper_limit % 100) / 10 );
        }
    }else if ( SettingItem_P6 == curr_setting_item ){
        if ( blink_flag ){
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.temp_vent_lower_limit % 1000) / 100) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.temp_vent_lower_limit % 100) / 10 );
        }
#if GUI_ENABLE_D1_D4_SETTING
    }else if ( (SettingItem_D1 <= curr_setting_item) && (SettingItem_D4 >= curr_setting_item) ){
        int index = curr_setting_item - SettingItem_D1;
        if ( blink_flag || (ventilator_timing_config_sub_index != 0) ){
            /* 显示时间. */
            Display::set_num_seg(Display::NUM_SEG_TIME_S1, '0' + ((config.ventilator_timing_config[index].time) % 100) / 10 );
            Display::set_num_seg(Display::NUM_SEG_TIME_S2, '0' + (config.ventilator_timing_config[index].time) % 10 );
            Display::set_num_seg(Display::NUM_SEG_TIME_S3, '0');
            Display::set_num_seg(Display::NUM_SEG_TIME_S4, '0');
            Display::set_pixel(Display::PIX_TIME_DOT, 1);
        }

        if ( blink_flag || (ventilator_timing_config_sub_index != 1) ){
            /* 显示开度. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.ventilator_timing_config[index].opening_percentage) % 100) / 10);
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.ventilator_timing_config[index].opening_percentage) % 10);
        }
#endif
    }else if ( SettingItem::SettingItem_C1 == curr_setting_item ){
        if ( blink_flag ){
            /* 电机行程圈数 C1，范围 1-999 圈. */
            int stroke = config.motor_stroke_time;
            if ( stroke < MOTOR_STROKE_TURNS_MIN ){
                stroke = MOTOR_STROKE_TURNS_MIN;
            }else if ( stroke > MOTOR_STROKE_TURNS_MAX ){
                stroke = MOTOR_STROKE_TURNS_MAX;
            }
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + (stroke % 1000) / 100 );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (stroke % 100) / 10 );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S3, '0' + (stroke % 10) );
        }
    }else if ( SettingItem::SettingItem_C2 == curr_setting_item ){
        if ( blink_flag ){
            /* 一圈秒数 C2，范围 5-100 秒. */
            int sec = config.motor_turn_seconds;
            if ( sec < MOTOR_TURN_SECONDS_MIN ){
                sec = MOTOR_TURN_SECONDS_MIN;
            }else if ( sec > MOTOR_TURN_SECONDS_MAX ){
                sec = MOTOR_TURN_SECONDS_MAX;
            }
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + (sec % 1000) / 100 );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (sec % 100) / 10 );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S3, '0' + (sec % 10) );
        }
    }else if ( SettingItem::SettingItem_S0 == curr_setting_item ){
        if ( blink_flag ){
            /* 上报时间间隔. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.data_upload_interval % 100) / 10) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.data_upload_interval % 10) );
        }
    }else if ( SettingItem::SettingItem_S2 == curr_setting_item ){
        if ( blink_flag ){
            /* 本地报警开关. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.local_alarm_switch % 10) );
        }
    }

    Display::flush();
    return true;
}

bool refresh_motor_cali_page(){
    static const uint16_t anim_codes[10] = {
        0b0111111111,
        0b1011111111,
        0b1101111111,
        0b1110111111,
        0b1111011111,
        0b1111101111,
        0b1111110111,
        0b1111111011,
        0b1111111101,
        0b1111111110,
    };
    Display::clear();
    uint16_t anim_code = anim_codes[anim_count % 10];

    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_A,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S2_A,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_A,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_B,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_C,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_D,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S2_D,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_D,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_E,1);
    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_F,1);

    if ( (anim_code & (0x0000000001 << 0)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_A,0);
    if ( (anim_code & (0x0000000001 << 1)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S2_A,0);
    if ( (anim_code & (0x0000000001 << 2)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_A,0);
    if ( (anim_code & (0x0000000001 << 3)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_B,0);
    if ( (anim_code & (0x0000000001 << 4)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_C,0);
    if ( (anim_code & (0x0000000001 << 5)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S3_D,0);
    if ( (anim_code & (0x0000000001 << 6)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S2_D,0);
    if ( (anim_code & (0x0000000001 << 7)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_D,0);
    if ( (anim_code & (0x0000000001 << 8)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_E,0);
    if ( (anim_code & (0x0000000001 << 9)) == 0 ) Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_S1_F,0);

    Display::flush();

    VentilateService::Status ventilate_status;
    VentilateService::get_status(ventilate_status);

    if ( (ventilate_status != VentilateService::Status::CALIBRATING_STAGE1) &&  (ventilate_status != VentilateService::Status::CALIBRATING_STAGE2) && (ventilate_status != VentilateService::Status::CALIBRATING_STAGE3) ){
        change_page(GUI::Page::Home);
    }

    if ( anim_count < 10000 ){
        anim_count ++;
    }else{
        anim_count = 0;
    }
    return true;
}

bool refresh(){
    switch ( current_page ){
        case Page::Home: {
            if ( refresh_home_page() != true ){
                return false;
            }
            break;
        }
        case Page::Setting: {
            if ( refresh_setting_page() != true ){
                return false;
            }
            break;
        }
        case Page::MotorCali: {
            if ( refresh_motor_cali_page() != true ){
                return false;
            }
            break;
        }
        default: {
            return false;
        }
    }
    if ( current_page == Page::Setting ){
        /* About 0.2s: setting refresh is ~100ms, toggle every 2 refreshes. */
        if ( blink_count < 1 ){
            blink_count++;
        }else{
            blink_flag = !blink_flag;
            blink_count = 0;
        }
    }else if ( blink_count < 5 ){
        blink_count++;
    }else{
        blink_flag = !blink_flag;
        blink_count = 0;
    }
    return true;
}

static int click_cnt = 0;
static bool click_cnt_change_flag = false;

bool button_callback_home_page(Button button, ButtonEvent event){
    
    Config_t config;
    ConfigService::get_config(config);
    if ( button == Button::Setting ){
        if ( event == ButtonEvent::LongPress ){
            return change_page(Page::Setting);
        }else if ( event == ButtonEvent::Click ){
            VentilateService::force(VentilateService::ForceAction::None);
            click_cnt += 1;
            click_cnt_change_flag = true;
            if ( click_cnt >= 5 ){
                LOG_INFO("Setting key pressed 5 times.");
            }
        }
    }else if ( button == Button::Plus ){
        if ( event == ButtonEvent::Click ){
            VentilateService::force(VentilateService::ForceAction::ForceOpen);
        }
    }else if ( button == Button::Minus ){
        if ( event == ButtonEvent::Click ){
            VentilateService::force(VentilateService::ForceAction::ForceClose);
        }
    }else if ( button == Button::Mode ){
        /* 手动 → 自动 → 定时 → 手动（与 D1-D4 设置项是否显示无关）. */
        switch ( config.working_mode ){
            case WorkingMode_Manual: config.working_mode = WorkingMode_Auto; break;
            case WorkingMode_Auto: config.working_mode = WorkingMode_Timing; break;
            default: config.working_mode = WorkingMode_Manual; break;
        }
        ConfigService::set_config(config);
        ConfigService::store();
        VentilateService::on_mode_changed();
    }
    return true;
}

static int clamp_add(int value, int step, int min_v, int max_v){
    int next = value + step;
    if ( next < min_v ){
        return min_v;
    }
    if ( next > max_v ){
        return max_v;
    }
    return next;
}

#if GUI_ENABLE_D1_D4_SETTING
static int wrap_hour(int value, int step){
    int next = (value + step) % 24;
    if ( next < 0 ){
        next += 24;
    }
    return next;
}
#endif

bool setting_page_plus_button_callback(int step){
    if ( step < 1 ){
        step = 1;
    }
    config_modified_flag = true;
    if ( SettingItem_A1 == curr_setting_item ){
        int temp_value = (config.temp_alert_upper_limit / 10);
        temp_value = clamp_add(temp_value, step, (config.temp_alert_lower_limit / 10) + 1, TEMP_SETTING_MAX);
        config.temp_alert_upper_limit = temp_value * 10;
    }else if ( SettingItem_A2 == curr_setting_item ){
        int temp_value = (config.temp_alert_lower_limit / 10);
        temp_value = clamp_add(temp_value, step, TEMP_SETTING_MIN, (config.temp_alert_upper_limit / 10) - 1);
        config.temp_alert_lower_limit = temp_value * 10;
    }else if ( SettingItem_P0 == curr_setting_item ){
        switch ( config.temp_control_mode ){
            case 1: config.temp_control_mode = 2; break;
            case 2: config.temp_control_mode = 1; break;
            default: config.temp_control_mode = 2; break;
        }
    }else if ( SettingItem_P1 == curr_setting_item ){
        int temp_value = (config.target_central_temp / 10);
        temp_value = clamp_add(temp_value, step, TEMP_SETTING_MIN, TEMP_SETTING_MAX);
        config.target_central_temp = temp_value * 10;
    }else if ( SettingItem_P2 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_positive / 10);
        temp_value = clamp_add(temp_value, 1, 0, 5);
        config.temp_return_diff_positive = temp_value * 10;
    }else if ( SettingItem_P3 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_negative / 10);
        temp_value = clamp_add(temp_value, 1, 0, 5);
        config.temp_return_diff_negative = temp_value * 10;
    }else if ( SettingItem_P4 == curr_setting_item ){
        config.temp_compensation_value = clamp_add(config.temp_compensation_value, step, -90, 90);
    }else if ( SettingItem_P5 == curr_setting_item ){
        int temp_value = (config.temp_vent_upper_limit / 10);
        temp_value = clamp_add(temp_value, step, (config.temp_vent_lower_limit / 10) + 1, TEMP_SETTING_MAX);
        config.temp_vent_upper_limit = temp_value * 10;
    }else if ( SettingItem_P6 == curr_setting_item ){
        int temp_value = (config.temp_vent_lower_limit / 10);
        temp_value = clamp_add(temp_value, step, TEMP_SETTING_MIN, (config.temp_vent_upper_limit / 10) - 1);
        config.temp_vent_lower_limit = temp_value * 10;
#if GUI_ENABLE_D1_D4_SETTING
    }else if ( (SettingItem_D1 <= curr_setting_item) && (SettingItem_D4 >= curr_setting_item) ){
        int index = curr_setting_item - SettingItem_D1;
        if ( ventilator_timing_config_sub_index == 0 ){
            config.ventilator_timing_config[index].time = wrap_hour(config.ventilator_timing_config[index].time, step);
        }else if ( ventilator_timing_config_sub_index == 1 ){
            config.ventilator_timing_config[index].opening_percentage = clamp_add(
                config.ventilator_timing_config[index].opening_percentage, step, 0, 100);
        }
#endif
    }else if ( SettingItem_C1 == curr_setting_item ){
        config.motor_stroke_time = clamp_add(config.motor_stroke_time, step, MOTOR_STROKE_TURNS_MIN, MOTOR_STROKE_TURNS_MAX);
    }else if ( SettingItem_C2 == curr_setting_item ){
        config.motor_turn_seconds = clamp_add(config.motor_turn_seconds, step, MOTOR_TURN_SECONDS_MIN, MOTOR_TURN_SECONDS_MAX);
    }else if ( SettingItem_S0 == curr_setting_item ){
        config.data_upload_interval = clamp_add(config.data_upload_interval, step, DATA_UPLOAD_INTERVAL_MIN, DATA_UPLOAD_INTERVAL_MAX);
    }else if ( SettingItem_S2 == curr_setting_item ){
        config.local_alarm_switch = (config.local_alarm_switch == 0) ? 1 : 0;
    }
    return true;
}

bool setting_page_minus_button_callback(int step){
    if ( step < 1 ){
        step = 1;
    }
    config_modified_flag = true;
    if ( SettingItem_A1 == curr_setting_item ){
        int temp_value = (config.temp_alert_upper_limit / 10);
        temp_value = clamp_add(temp_value, -step, (config.temp_alert_lower_limit / 10) + 1, TEMP_SETTING_MAX);
        config.temp_alert_upper_limit = temp_value * 10;
    }else if ( SettingItem_A2 == curr_setting_item ){
        int temp_value = (config.temp_alert_lower_limit / 10);
        temp_value = clamp_add(temp_value, -step, TEMP_SETTING_MIN, (config.temp_alert_upper_limit / 10) - 1);
        config.temp_alert_lower_limit = temp_value * 10;
    }else if ( SettingItem_P0 == curr_setting_item ){
        switch ( config.temp_control_mode ){
            case 1: config.temp_control_mode = 2; break;
            case 2: config.temp_control_mode = 1; break;
            default: config.temp_control_mode = 2; break;
        }
    }else if ( SettingItem_P1 == curr_setting_item ){
        int temp_value = (config.target_central_temp / 10);
        temp_value = clamp_add(temp_value, -step, TEMP_SETTING_MIN, TEMP_SETTING_MAX);
        config.target_central_temp = temp_value * 10;
    }else if ( SettingItem_P2 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_positive / 10);
        temp_value = clamp_add(temp_value, -1, 0, 5);
        config.temp_return_diff_positive = temp_value * 10;
    }else if ( SettingItem_P3 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_negative / 10);
        temp_value = clamp_add(temp_value, -1, 0, 5);
        config.temp_return_diff_negative = temp_value * 10;
    }else if ( SettingItem_P4 == curr_setting_item ){
        config.temp_compensation_value = clamp_add(config.temp_compensation_value, -step, -90, 90);
    }else if ( SettingItem_P5 == curr_setting_item ){
        int temp_value = (config.temp_vent_upper_limit / 10);
        temp_value = clamp_add(temp_value, -step, (config.temp_vent_lower_limit / 10) + 1, TEMP_SETTING_MAX);
        config.temp_vent_upper_limit = temp_value * 10;
    }else if ( SettingItem_P6 == curr_setting_item ){
        int temp_value = (config.temp_vent_lower_limit / 10);
        temp_value = clamp_add(temp_value, -step, TEMP_SETTING_MIN, (config.temp_vent_upper_limit / 10) - 1);
        config.temp_vent_lower_limit = temp_value * 10;
#if GUI_ENABLE_D1_D4_SETTING
    }else if ( (SettingItem_D1 <= curr_setting_item) && (SettingItem_D4 >= curr_setting_item) ){
        int index = curr_setting_item - SettingItem_D1;
        if ( ventilator_timing_config_sub_index == 0 ){
            config.ventilator_timing_config[index].time = wrap_hour(config.ventilator_timing_config[index].time, -step);
        }else if ( ventilator_timing_config_sub_index == 1 ){
            config.ventilator_timing_config[index].opening_percentage = clamp_add(
                config.ventilator_timing_config[index].opening_percentage, -step, 0, 100);
        }
#endif
    }else if ( SettingItem_C1 == curr_setting_item ){
        config.motor_stroke_time = clamp_add(config.motor_stroke_time, -step, MOTOR_STROKE_TURNS_MIN, MOTOR_STROKE_TURNS_MAX);
    }else if ( SettingItem_C2 == curr_setting_item ){
        config.motor_turn_seconds = clamp_add(config.motor_turn_seconds, -step, MOTOR_TURN_SECONDS_MIN, MOTOR_TURN_SECONDS_MAX);
    }else if ( SettingItem_S0 == curr_setting_item ){
        config.data_upload_interval = clamp_add(config.data_upload_interval, -step, DATA_UPLOAD_INTERVAL_MIN, DATA_UPLOAD_INTERVAL_MAX);
    }else if ( SettingItem_S2 == curr_setting_item ){
        config.local_alarm_switch = (config.local_alarm_switch == 0) ? 1 : 0;
    }
    return true;
}

static void advance_setting_item(void){
    for (;;) {
        curr_setting_item = (SettingItem)((curr_setting_item + 1) % SettingItem_Count);
        bool skip = false;
#if !GUI_ENABLE_D1_D4_SETTING
        if ( (curr_setting_item >= SettingItem_D1) && (curr_setting_item <= SettingItem_D4) ){
            skip = true;
        }
#endif
#if !GUI_ENABLE_P0_P3_SETTING
        if ( (curr_setting_item == SettingItem_P0) || (curr_setting_item == SettingItem_P3) ){
            skip = true;
        }
#endif
        if ( skip == false ){
            break;
        }
    }
#if GUI_ENABLE_D1_D4_SETTING
    ventilator_timing_config_sub_index = 0;
#endif
}

bool button_callback_setting_page(Button button, ButtonEvent event){
    GUI::setting_page_auto_exit_count = 0;
    if ( button == Button::Setting ){
        if ( event == ButtonEvent::LongPress ){
            /* 在设置页面下长按设置键进入主页. */
            return change_page(Page::Home);
        }else if ( event == ButtonEvent::Click ){
            /* 在设置页面下短按切换设置的选项. */
            advance_setting_item();
        }
    }else if ( button == Button::Mode ){
        if ( event == ButtonEvent::LongPress ){
            /* 进入电机校准. */
            return change_page(Page::MotorCali);
        }else if ( event == ButtonEvent::Click ){
#if GUI_ENABLE_D1_D4_SETTING
            /* 如果当前处于设置定时任务的选项下. */
            if ( (SettingItem_D1 <= curr_setting_item) && (SettingItem_D4 >= curr_setting_item) ){
                ventilator_timing_config_sub_index = (ventilator_timing_config_sub_index + 1) % 2;
            }
#endif
        }
    }else if ( button == Button::Plus ){
        if ( event == ButtonEvent::Click ){
            setting_page_plus_button_callback(1);
        }else if ( event == ButtonEvent::LongPress ){
            setting_page_plus_button_callback(10);
        }
    }else if ( button == Button::Minus ){
        if ( event == ButtonEvent::Click ){
            setting_page_minus_button_callback(1);
        }else if ( event == ButtonEvent::LongPress ){
            setting_page_minus_button_callback(10);
        }
    }
    return true;
}

}

bool GUI::init(){
    LOG_INFO("Init gui.");
    Indev::init();
    Display::init();
    GUI::current_page = Page::Home;
    return true;
}

bool GUI::button_callback(Button button, ButtonEvent event){
    switch ( GUI::current_page ){
        case Page::Home: {
            GUI::button_callback_home_page(button,event);
            break;
        }
        case Page::Setting: {
            GUI::button_callback_setting_page(button,event);
            break;
        }
        default: {
            return false;
        }
    }
    return true;
}

static TaskHandle_t task_handle_gui = nullptr;
static TaskHandle_t task_handle_indev = nullptr;

static void GUI_eventloop(void *param){
    int cnt = 0;
    while ( true ){
        int refresh_every = 20;
        if ( GUI::current_page == GUI::Page::Setting ){
            refresh_every = 20;
        }
        if ( ++cnt > refresh_every ){
            cnt = 0;
            GUI::refresh();
        }
        if ( GUI::click_cnt_change_flag == true ){
            cnt = 0;
            GUI::click_cnt_change_flag = false;
        }else{
            cnt += 1;
            if ( cnt > 100 ){
                GUI::click_cnt = 0;
            }
        }
        Indev::eventloop();
        vTaskDelay(pdMS_TO_TICKS(5));   
    }
}


bool GUI::start(){
    if ( xTaskCreate(GUI_eventloop,"gui",512,nullptr,10,&task_handle_gui) != pdPASS ){
        LOG_ERROR("GUI task start failed.");
        return false;
    }
    
    return true;
}
