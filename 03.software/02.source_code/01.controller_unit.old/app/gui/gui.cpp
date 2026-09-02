#include "gui.hpp"
#include "indev.hpp"
#include "console_service.hpp"
#include "display.hpp"
#include "env.hpp"
#include "config.hpp"
#include "ventilate_service.hpp"

namespace GUI {

enum SettingItem {
    SettingItem_A1,
    SettingItem_A2,
    SettingItem_P0,
    SettingItem_P1,
    SettingItem_P2,
    SettingItem_P3,
    SettingItem_P4,
    SettingItem_D1,
    SettingItem_D2,
    SettingItem_D3,
    SettingItem_D4,
    SettingItem_C1,
    SettingItem_S0,
    SettingItem_S2,
};

static const char setting_item_codes[][2] = { 
    [SettingItem_A1] = {'a','1'},
    [SettingItem_A2] = {'a','2'},
    [SettingItem_P0] = {'p','0'},
    [SettingItem_P1] = {'p','1'},
    [SettingItem_P2] = {'p','2'},
    [SettingItem_P3] = {'p','3'},
    [SettingItem_P4] = {'p','4'},
    [SettingItem_D1] = {'d','1'},
    [SettingItem_D2] = {'d','2'},
    [SettingItem_D3] = {'d','3'},
    [SettingItem_D4] = {'d','4'},
    [SettingItem_C1] = {'c','1'},
    [SettingItem_S0] = {'s','0'},
    [SettingItem_S2] = {'s','2'},
};

Page current_page = Page::Home;
bool config_modified_flag = false;
static SettingItem curr_setting_item = SettingItem_A1;
static Config_t config;
static unsigned int blink_count = 0;
static unsigned int anim_count = 0;
static bool blink_flag = false;
static int ventilator_timing_config_sub_index = 0;
static int setting_page_auto_exit_count = 0;


bool refresh_home_page(){
    Config_t config;
    ConfigService::get_config(config);
    /* 清屏. */
    Display::clear();

    /* 点亮装饰线. */
    Display::set_pixel(Display::PIX_BAR1,1);
    Display::set_pixel(Display::PIX_BAR2,1);

    /* 显示数据通信服务. */

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

    /* 显示报警信息. */
    Display::set_pixel(Display::PIX_OK_SYM,1);

    /* 显示电池电量. */

    /* 显示开度信息. */
    int opening_percentage = 0;
    VentilateService::get_opening_percentage(opening_percentage);
    if ( opening_percentage < 0 ){
        opening_percentage = 0;
    }else if ( opening_percentage > 100 ){
        opening_percentage = 100;
    }
    Display::set_num_seg(Display::NUM_SEG_OPENING_PERCENTAGE_S1, '0' + (opening_percentage % 1000) / 100);
    Display::set_num_seg(Display::NUM_SEG_OPENING_PERCENTAGE_S2, '0' + (opening_percentage % 100) / 10);
    Display::set_num_seg(Display::NUM_SEG_OPENING_PERCENTAGE_S3, '0' + (opening_percentage % 10));

    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_SYM_PERCENTAGE,1);
    
    /* 指示电机状态. */
    Motor::State motor_state;
    VentilateService::get_motor_state(motor_state);
    if ( blink_flag ){
        if ( motor_state == Motor::State::RUNNING_FORWARD ){
            Display::set_pixel(Display::PIX_MOTOR_STATUS_OPENING,1);
        }else if ( motor_state == Motor::State::RUNNING_REVERSE ){
            Display::set_pixel(Display::PIX_MOTOR_STATUS_CLOSING,1);
        }
    }

    Display::set_pixel(Display::PIX_OPENING_PERCENTAGE_TEXT,1);

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

    Display::flush();

    return true;
}

bool change_page(Page target){
    switch ( target ){
        case Page::Home: {
            LOG_DEBUG("Enter Home page.");
            /* 保存设置. */
            if ( config_modified_flag ){
                ConfigService::set_config(GUI::config);
                ConfigService::store();
            }
            GUI::current_page = Page::Home;
            break;
        }
        case Page::Setting: {
            /* 在主页下长按设置键进入设置页面. */
            LOG_DEBUG("Enter setting page.");
            ConfigService::get_config(GUI::config);
            GUI::config_modified_flag = false;
            GUI::current_page = Page::Setting;
            GUI::setting_page_auto_exit_count = 0;
            break;
        }
        case Page::MotorCali: {
            LOG_DEBUG("Enter motor cali page.");
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
    }else if ( SettingItem::SettingItem_C1 == curr_setting_item ){
        if ( blink_flag ){
            /* 电机行程时间. */
            Display::set_num_seg(Display::NUM_SEG_TEMP_S1, '0' + ((config.motor_stroke_time % 100) / 10) );
            Display::set_num_seg(Display::NUM_SEG_TEMP_S2, '0' + (config.motor_stroke_time % 10) );
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
    if ( blink_count < 5 ){
        blink_count++;
    }else{
        blink_flag = !blink_flag;
        blink_count = 0;
    }
    return true;
}

bool button_callback_home_page(Button button, ButtonEvent event){
    Config_t config;
    ConfigService::get_config(config);
    VentilateService::Status ventilate_service_status;
    VentilateService::get_status(ventilate_service_status);
    if ( button == Button::Setting ){
        if ( event == ButtonEvent::LongPress ){
            return change_page(Page::Setting);
        }else if ( event == ButtonEvent::Click ){
            if ( config.working_mode == WorkingMode_Manual ){
                VentilateService::force(VentilateService::ForceAction::None);
            }
        }
    }else if ( button == Button::Plus ){
        if ( event == ButtonEvent::Click ){
            if ( config.working_mode == WorkingMode_Manual && ventilate_service_status != VentilateService::Status::ALIGN ){
                VentilateService::force(VentilateService::ForceAction::ForceOpen);
            }
        }
    }else if ( button == Button::Minus ){
        if ( event == ButtonEvent::Click ){
            if ( config.working_mode == WorkingMode_Manual && ventilate_service_status != VentilateService::Status::ALIGN ){
                VentilateService::force(VentilateService::ForceAction::ForceClose);
            }
        }
    }else if ( button == Button::Mode ){
        switch ( config.working_mode ){
            case WorkingMode_Manual: { 
                config.working_mode = WorkingMode_Auto; 
                VentilateService::force(VentilateService::ForceAction::None);
                break; 
            }
            case WorkingMode_Auto: config.working_mode = WorkingMode_Timing; break;
            default: config.working_mode = WorkingMode_Manual; break;
        }
        ConfigService::set_config(config);
        ConfigService::store();
    }
    return true;
}

bool setting_page_plus_button_callback(){
    config_modified_flag = true;
    if ( SettingItem_A1 == curr_setting_item ){
        int temp_value = (config.temp_alert_upper_limit / 10);
        if ( temp_value < 0 ){
            temp_value = 0;
        }else if ( temp_value < 55 ){
            temp_value += 1;
        }else{
            temp_value = 55;
        }
        config.temp_alert_upper_limit = temp_value * 10;
    }else if ( SettingItem_A2 == curr_setting_item ){
        int temp_value = (config.temp_alert_lower_limit / 10);
        if ( temp_value < 0 ){
            temp_value = 0;
        }else if ( temp_value < (config.temp_alert_upper_limit / 10) ){
            temp_value += 1;
        }else{
            temp_value = (config.temp_alert_upper_limit / 10);
        }
        config.temp_alert_lower_limit = temp_value * 10;
    }else if ( SettingItem_P0 == curr_setting_item ){
        switch ( config.temp_control_mode ){
            case 1: config.temp_control_mode = 2; break;
            case 2: config.temp_control_mode = 1; break;
            default: config.temp_control_mode = 2; break;
        }
    }else if ( SettingItem_P1 == curr_setting_item ){
        int temp_value = (config.target_central_temp / 10);
        if ( temp_value < 0 ){
            temp_value = 0;
        }else if ( temp_value < 55 ){
            temp_value += 1;
        }else{
            temp_value = 55;
        }
        config.target_central_temp = temp_value * 10;
    }else if ( SettingItem_P2 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_positive / 10);
        if ( temp_value < 1 ){
            temp_value = 1;
        }else if ( temp_value < 15 ){
            temp_value += 1;
        }else{
            temp_value = 15;
        }
        config.temp_return_diff_positive = temp_value * 10;
    }else if ( SettingItem_P3 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_negative / 10);
        if ( temp_value < 1 ){
            temp_value = 1;
        }else if ( temp_value < 15 ){
            temp_value += 1;
        }else{
            temp_value = 15;
        }
        config.temp_return_diff_negative = temp_value * 10;
    }else if ( SettingItem_P4 == curr_setting_item ){
        if ( config.temp_compensation_value < -100 ){
            config.temp_compensation_value = -100;
        }else if ( config.temp_compensation_value < 100 ){
            config.temp_compensation_value += 1;
        }else{
            config.temp_compensation_value = 100;
        }
    }else if ( (SettingItem_D1 <= curr_setting_item) && (SettingItem_D4 >= curr_setting_item) ){
        int index = curr_setting_item - SettingItem_D1;
        if ( ventilator_timing_config_sub_index == 0 ){
            /* 设置时间. */
            config.ventilator_timing_config[index].time = (config.ventilator_timing_config[index].time + 1) % 24;
        }else if ( ventilator_timing_config_sub_index == 1 ){
            /* 设置开度. */
            if ( config.ventilator_timing_config[index].opening_percentage < 0 ){
                config.ventilator_timing_config[index].opening_percentage = 0;
            }else if ( config.ventilator_timing_config[index].opening_percentage < 100 ){
                config.ventilator_timing_config[index].opening_percentage += 1;
            }else {
                config.ventilator_timing_config[index].opening_percentage = 100;
            }
        }
    }else if ( SettingItem_C1 == curr_setting_item ){
        if ( config.motor_stroke_time < 0 ){
            config.motor_stroke_time = 0;
        }else if ( config.motor_stroke_time < 999 ){
            config.motor_stroke_time += 1;
        }else {
            config.motor_stroke_time = 999;
        }
    }else if ( SettingItem_S0 == curr_setting_item ){
        if ( config.data_upload_interval < 0 ){
            config.data_upload_interval = 0;
        }else if ( config.data_upload_interval < 30 ){
            config.data_upload_interval += 1;
        }else {
            config.data_upload_interval = 30;
        }
    }else if ( SettingItem_S2 == curr_setting_item ){
        config.local_alarm_switch = (config.local_alarm_switch == 0) ? 1 : 0;
    }
    return true;
}

bool setting_page_minus_button_callback(){
    config_modified_flag = true;
    if ( SettingItem_A1 == curr_setting_item ){
        int temp_value = (config.temp_alert_upper_limit / 10);
        if ( temp_value > 55 ){
            temp_value = 55;
        }else if ( temp_value > (config.temp_alert_lower_limit / 10) ){
            temp_value -= 1;
        }else{
            temp_value = (config.temp_alert_lower_limit / 10);
        }
        config.temp_alert_upper_limit = temp_value * 10;
    }else if ( SettingItem_A2 == curr_setting_item ){
        int temp_value = (config.temp_alert_lower_limit / 10);
        if ( temp_value > 55 ){
            temp_value = 55;
        }else if ( temp_value > 0 ){
            temp_value -= 1;
        }else{
            temp_value = 0;
        }
        config.temp_alert_lower_limit = temp_value * 10;
    }else if ( SettingItem_P0 == curr_setting_item ){
        switch ( config.temp_control_mode ){
            case 1: config.temp_control_mode = 2; break;
            case 2: config.temp_control_mode = 1; break;
            default: config.temp_control_mode = 2; break;
        }
    }else if ( SettingItem_P1 == curr_setting_item ){
        int temp_value = (config.target_central_temp / 10);
        if ( temp_value > 55 ){
            temp_value = 55;
        }else if ( temp_value > 0 ){
            temp_value -= 1;
        }else{
            temp_value = 0;
        }
        config.target_central_temp = temp_value * 10;
    }else if ( SettingItem_P2 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_positive / 10);
        if ( temp_value > 15 ){
            temp_value = 15;
        }else if ( temp_value > 1 ){
            temp_value -= 1;
        }else{
            temp_value = 1;
        }
        config.temp_return_diff_positive = temp_value * 10;
    }else if ( SettingItem_P3 == curr_setting_item ){
        int temp_value = (config.temp_return_diff_negative / 10);
        if ( temp_value > 15 ){
            temp_value = 15;
        }else if ( temp_value > 1 ){
            temp_value -= 1;
        }else{
            temp_value = 1;
        }
        config.temp_return_diff_negative = temp_value * 10;
    }else if ( SettingItem_P4 == curr_setting_item ){
        if ( config.temp_compensation_value > 100 ){
            config.temp_compensation_value = 100;
        }else if ( config.temp_compensation_value > -100 ){
            config.temp_compensation_value -= 1;
        }else{
            config.temp_compensation_value = -100;
        }
    }else if ( (SettingItem_D1 <= curr_setting_item) && (SettingItem_D4 >= curr_setting_item) ){
        int index = curr_setting_item - SettingItem_D1;
        if ( ventilator_timing_config_sub_index == 0 ){
            /* 设置时间. */
            config.ventilator_timing_config[index].time = (config.ventilator_timing_config[index].time - 1) % 24;
        }else if ( ventilator_timing_config_sub_index == 1 ){
            /* 设置开度. */
            if ( config.ventilator_timing_config[index].opening_percentage > 100 ){
                config.ventilator_timing_config[index].opening_percentage = 100;
            }else if ( config.ventilator_timing_config[index].opening_percentage > 0 ){
                config.ventilator_timing_config[index].opening_percentage -= 1;
            }else {
                config.ventilator_timing_config[index].opening_percentage = 0;
            }
        }
    }else if ( SettingItem_C1 == curr_setting_item ){
        if ( config.motor_stroke_time > 999 ){
            config.motor_stroke_time = 999;
        }else if ( config.motor_stroke_time > 0 ){
            config.motor_stroke_time -= 1;
        }else {
            config.motor_stroke_time = 0;
        }
    }else if ( SettingItem_S0 == curr_setting_item ){
        if ( config.data_upload_interval > 30 ){
            config.data_upload_interval = 30;
        }else if ( config.data_upload_interval > 0 ){
            config.data_upload_interval -= 1;
        }else {
            config.data_upload_interval = 0;
        }
    }else if ( SettingItem_S2 == curr_setting_item ){
        config.local_alarm_switch = (config.local_alarm_switch == 0) ? 1 : 0;
    }
    return true;
}

bool button_callback_setting_page(Button button, ButtonEvent event){
    GUI::setting_page_auto_exit_count = 0;
    if ( button == Button::Setting ){
        if ( event == ButtonEvent::LongPress ){
            /* 在设置页面下长按设置键进入主页. */
            return change_page(Page::Home);
        }else if ( event == ButtonEvent::Click ){
            /* 在设置页面下短按切换设置的选项. */
            GUI::curr_setting_item = (GUI::SettingItem)((GUI::curr_setting_item + 1) % 14);
            ventilator_timing_config_sub_index = 0;
        }
    }else if ( button == Button::Mode ){
        if ( event == ButtonEvent::LongPress ){
            /* 进入电机校准. */
            return change_page(Page::MotorCali);
        }else if ( event == ButtonEvent::Click ){
            /* 如果当前处于设置定时任务的选项下. */
            if ( (SettingItem_D1 <= curr_setting_item) && (SettingItem_D4 >= curr_setting_item) ){
                ventilator_timing_config_sub_index = (ventilator_timing_config_sub_index + 1) % 2;
            }
        }
    }else if ( button == Button::Plus ){
        if ( event == ButtonEvent::Click ){
            setting_page_plus_button_callback();
        }
    }else if ( button == Button::Minus ){
        if ( event == ButtonEvent::Click ){
            setting_page_minus_button_callback();
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

void GUI::eventloop(){
    GUI::refresh();
}

void GUI::indev_eventloop(){
    Indev::eventloop();
}