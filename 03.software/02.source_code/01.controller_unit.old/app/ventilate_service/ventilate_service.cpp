#include "ventilate_service.hpp"
#include "motor_service.hpp"
#include "console_service.hpp"
#include "systimer.hpp"
#include "config.hpp"
#include "env.hpp"
#include <cmath>

static Motor *motor;
static VentilateService::Status ventilate_service_status;
static VentilateService::ForceAction force_action = VentilateService::ForceAction::None;
static double opening_percentage_curr = 0;
static double opening_percentage_set = 0;
static unsigned int timer_cnt = 0;
static int32_t calibrate_value = 0;

bool VentilateService::init(){
    motor = MotorService::get_motor();
    if ( motor == nullptr ){
        LOG_ERROR("Can't get motor.");
        return false;
    }
    motor->init();
    force_action = VentilateService::ForceAction::None;
    VentilateService::align();
    return true;
}

bool VentilateService::align(){
    ventilate_service_status = Status::ALIGN;
    /* 计算时间. */
    SysTimer::reset_timer(0);
    motor->execute_action(Motor::Action::REVERSE);
    SysTimer::start_timer(0);
    return true;
}

bool VentilateService::calibrate(){
    ventilate_service_status = Status::CALIBRATING_STAGE1;
    motor->execute_action(Motor::Action::REVERSE);
    return true;
}

void VentilateService::eventloop(){
    Config_t config;
    ConfigService::get_config(config);
    motor->eventloop();
    if ( force_action == ForceAction::None || ventilate_service_status == Status::ALIGN ){  /* 对齐过程不可打断. */
        /* 无强制动作. */
        if ( ventilate_service_status == Status::ALIGN ){
            Motor::State state = Motor::State::IDLE;
            motor->get_state(state);
            if ( state ==  Motor::State::IDLE ){
                int64_t motor_timer_cnt = 0;
                motor->get_timer_cnt(motor_timer_cnt);
                LOG_DEBUG("ALIGN timer cnt %ld.",motor_timer_cnt);
                if ( motor_timer_cnt < 0 ){
                    motor_timer_cnt = -motor_timer_cnt;
                }
                opening_percentage_set = ((double)motor_timer_cnt / 10.0) / (double)config.motor_stroke_time;
                opening_percentage_curr = 0;
                motor->reset_timer();
                ventilate_service_status = Status::STOPPED;
            }else if ( state == Motor::State::ERROR_OVC ){
                /* 过载保护. */
                LOG_ERROR("Over current protection.");
                ventilate_service_status = Status::OCP;
            }
        }else if ( ventilate_service_status == Status::CALIBRATING_STAGE1 ){
            /* 等待电机停止进入Stage2. */
            Motor::State state = Motor::State::IDLE;
            motor->get_state(state);
            if ( state == Motor::State::IDLE ){
                LOG_DEBUG("Enter stage 2.");
                ventilate_service_status = Status::CALIBRATING_STAGE2;
                calibrate_value = 0;
                motor->reset_timer();
                motor->execute_action(Motor::Action::FORWARD);
            }else if ( state == Motor::State::ERROR_OVC ){
                ventilate_service_status = Status::OCP;
            }
        }else if ( ventilate_service_status == Status::CALIBRATING_STAGE2 ){
            Motor::State state = Motor::State::IDLE;
            motor->get_state(state);
            if ( state == Motor::State::IDLE ){
                int64_t motor_timer_cnt = 0;
                motor->get_timer_cnt(motor_timer_cnt);
                if ( motor_timer_cnt < 0 ){
                    motor_timer_cnt = - motor_timer_cnt;
                }
                calibrate_value += motor_timer_cnt;
                ventilate_service_status = Status::CALIBRATING_STAGE3;
                motor->reset_timer();
                motor->execute_action(Motor::Action::REVERSE);
                LOG_DEBUG("Stage 2 cnt %d,Enter stage 3.", motor_timer_cnt);
            }else if ( state == Motor::State::ERROR_OVC ){
                ventilate_service_status = Status::OCP;
            }
        }else if ( ventilate_service_status == Status::CALIBRATING_STAGE3 ){
            Motor::State state = Motor::State::IDLE;
            motor->get_state(state);
            if ( state == Motor::State::IDLE ){
                int64_t motor_timer_cnt = 0;
                motor->get_timer_cnt(motor_timer_cnt);
                if ( motor_timer_cnt < 0 ){
                    motor_timer_cnt = - motor_timer_cnt;
                }
                calibrate_value += motor_timer_cnt;
                ventilate_service_status = Status::STOPPED;
                motor->reset_timer();
                motor->execute_action(Motor::Action::STOP);
                unsigned int time = calibrate_value / 2000;
                Config_t config;
                ConfigService::get_config(config);
                LOG_DEBUG("Stage 3 cnt %d, (%d) Save new motor stroke time %d.", (int)motor_timer_cnt, (int)calibrate_value, (int)time);
                config.motor_stroke_time = time;
                ConfigService::set_config(config);
                ConfigService::store();
            
            }else if ( state == Motor::State::ERROR_OVC ){
                ventilate_service_status = Status::OCP;
            }
        }else{
            LOG_INFO("current %f, set %f", opening_percentage_curr, opening_percentage_set);
            int64_t motor_timer_cnt = 0;
            motor->get_timer_cnt(motor_timer_cnt);
            opening_percentage_curr = ((double)motor_timer_cnt / 10.0) / (double)config.motor_stroke_time;
            /* 计算当前差值. */
            double delta_percentage = opening_percentage_curr - opening_percentage_set;
            if ( delta_percentage > 0.5 ){
                /* 关闭. */
                SysTimer::reset_timer(0);
                motor->execute_action(Motor::Action::REVERSE);
                SysTimer::start_timer(0);
            }else if ( delta_percentage < -0.5 ){
                /* 开启. */
                SysTimer::reset_timer(0);
                motor->execute_action(Motor::Action::FORWARD);
                SysTimer::start_timer(0);
            }else{
                motor->execute_action(Motor::Action::STOP);
            }
        }
    }else{
        int64_t motor_timer_cnt = 0;
        Motor::State motor_state;
        if ( motor_state == Motor::State::IDLE ){
            /* Force 已经停止. */
            if ( force_action == ForceAction::ForceClose ){
                motor->reset_timer();
            }
            force_action = ForceAction::None;
        }else if ( motor_state == Motor::State::ERROR_OVC ){
            force_action = ForceAction::None;
        }
        motor->get_timer_cnt(motor_timer_cnt);
        motor->get_state(motor_state);
        opening_percentage_curr = ((double)motor_timer_cnt / 10.0) / (double)config.motor_stroke_time;
        opening_percentage_set = opening_percentage_curr;
        
    }

    /* 监测. */
    if ( config.working_mode == WorkingMode_Auto ){
        double temp_curr = 0.0;
        Env::get_temperature(temp_curr);
        double upper_limit = ((double)config.target_central_temp / 10.0) + ((double)config.temp_return_diff_positive / 10.0);
        double lower_limit = ((double)config.target_central_temp / 10.0) - ((double)config.temp_return_diff_negative / 10.0);
        if ( lower_limit < upper_limit ){
            if ( temp_curr < lower_limit ){
                opening_percentage_set = 100;
            }else if ( temp_curr > upper_limit ){
                opening_percentage_set = 0;
            }
        }
    }
}

bool VentilateService::set_opening_percentage(int value){
    if ( ventilate_service_status == VentilateService::Status::ALIGN || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE1 || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE2 || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE3  )
    opening_percentage_set = value;
    return true;
}

bool VentilateService::get_opening_percentage(int &value){
    value = opening_percentage_curr;
    return true;
}

bool VentilateService::get_status(Status &status){
    status = ventilate_service_status;
    return true;
}

bool VentilateService::get_motor_state(Motor::State &state){
    return motor->get_state(state);
}

bool VentilateService::force(ForceAction action){
    if ( action != ForceAction::None && force_action == ForceAction::None ){
        force_action = action;
        if ( force_action == ForceAction::ForceOpen ){
            motor->execute_action(Motor::Action::FORWARD);
        }else if ( force_action == ForceAction::ForceClose ){
            motor->execute_action(Motor::Action::REVERSE);
        }
    }else if ( action == ForceAction::None && force_action != ForceAction::None ){
        Config_t config;
        ConfigService::get_config(config);
        force_action = action;
        int64_t motor_timer_cnt = 0;
        motor->execute_action(Motor::Action::STOP);
        motor->get_timer_cnt(motor_timer_cnt);
        opening_percentage_curr = ((double)motor_timer_cnt / 10.0) / (double)config.motor_stroke_time;
        opening_percentage_set = opening_percentage_curr;
    }
    return true;
}