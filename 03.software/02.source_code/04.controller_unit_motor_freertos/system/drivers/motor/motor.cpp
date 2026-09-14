#include "motor.hpp"
#include "console_logger.h"

#define MOTOR_LIMIT_IGNORE_TICKS 10
#define MOTOR_LIMIT_CONFIRM_TICKS 8
#define MOTOR_BASELINE_TICKS 5
#define MOTOR_STALL_CONFIRM_TICKS 6
#define MOTOR_STALL_RATIO 1.2
#define MOTOR_STALL_DELTA 0.15

Motor::Motor(PortableStrategy &portable_strategy,Parameter param) : portable_strategy(portable_strategy), param(param) {
    this->state = State::IDLE;
    this->current = 0.0;
    this->limit_ignore_cnt = 0;
    this->home_seek = false;
    this->calibrating = false;
    this->ignore_stall = false;
    this->reset_motion_detect();
}

void Motor::reset_motion_detect(void){
    this->stop_cnt = 0;
    this->oc_cnt = 0;
    this->stall_cnt = 0;
    this->baseline_cnt = 0;
    this->run_current = 0.0;
    this->run_current_ready = false;
}

bool Motor::init(){
    return this->portable_strategy.init();
}

bool Motor::deinit(){
    return this->portable_strategy.deinit();
}

bool Motor::execute_action(Action action){
    switch ( action ){
        case Action::FORWARD: {
            this->portable_strategy.set_output(PortableStrategy::Pin::REVERSE,false);
            this->portable_strategy.set_output(PortableStrategy::Pin::FORWARD,true);
            this->reset_motion_detect();
            this->limit_ignore_cnt = MOTOR_LIMIT_IGNORE_TICKS;
            this->state = State::RUNNING_FORWARD;
            break;
        }
        case Action::REVERSE: {
            this->portable_strategy.set_output(PortableStrategy::Pin::FORWARD,false);
            this->portable_strategy.set_output(PortableStrategy::Pin::REVERSE,true);
            this->reset_motion_detect();
            this->limit_ignore_cnt = MOTOR_LIMIT_IGNORE_TICKS;
            this->state = State::RUNNING_REVERSE;
            break;
        }
        default: {
            this->portable_strategy.set_output(PortableStrategy::Pin::FORWARD,false);
            this->portable_strategy.set_output(PortableStrategy::Pin::REVERSE,false);
            this->reset_motion_detect();
            this->state = State::IDLE;
            break;
        }
    }
    return true;
}

bool Motor::get_state(State &value){
    value = this->state;
    return true;
}

bool Motor::get_current(double &value){
    value = this->current;
    return true;
}

void Motor::eventloop(){
    uint16_t adc_value = 0;
    if ( this->portable_strategy.get_adc_value(adc_value) != true ){
        return;
    }

    if ( this->limit_ignore_cnt > 0 ){
        this->limit_ignore_cnt--;
    }

    double current = ((double)adc_value) * this->param.cs_k + this->param.cs_b;
    if ( (this->state == State::RUNNING_REVERSE) && (this->timer_cnt <= 0)
         && (this->home_seek == false) && (this->calibrating == false) ){
        this->timer_cnt = 0;
        this->execute_action(Motor::Action::STOP);
        this->current = current;
        return;
    }
    /* Overcurrent stop disabled: do not trip ERROR_OVC. */
    this->oc_cnt = 0;
    if ( current < this->param.zero_threshold ){
        if ( this->home_seek ){
            /* 找零：不停机，由上层连续 3 次电流 0 再停. */
            this->stop_cnt = 0;
        }else if ( this->ignore_stall ){
            /* 定点/强制行程中途：忽略假零流，避免反复停机把调度/日志打爆、屏停住. */
            this->stop_cnt = 0;
        }else if ( this->limit_ignore_cnt > 0 ){
            this->stop_cnt = 0;
        }else if ( stop_cnt < MOTOR_LIMIT_CONFIRM_TICKS ){
            stop_cnt++;
        }else{
            stop_cnt = 0;
            if ( (this->state == State::RUNNING_FORWARD) || (this->state == State::RUNNING_REVERSE) ){
                LOG_INFO("Motor current limit, stop.");
                this->execute_action(Motor::Action::STOP);
                this->state = State::IDLE;
            }
        }
    }else{
        this->stop_cnt = 0;
        if ( this->home_seek || this->calibrating || this->ignore_stall ){
            this->stall_cnt = 0;
        }else if ( this->limit_ignore_cnt > 0 ){
            this->stall_cnt = 0;
        }else if ( (this->state == State::RUNNING_FORWARD) || (this->state == State::RUNNING_REVERSE) ){
            if ( this->run_current_ready == false ){
                this->run_current += current;
                this->baseline_cnt++;
                if ( this->baseline_cnt >= MOTOR_BASELINE_TICKS ){
                    this->run_current = this->run_current / (double)MOTOR_BASELINE_TICKS;
                    this->run_current_ready = true;
                    this->stall_cnt = 0;
                }
            }else{
                double trip = this->run_current * MOTOR_STALL_RATIO;
                if ( trip < (this->run_current + MOTOR_STALL_DELTA) ){
                    trip = this->run_current + MOTOR_STALL_DELTA;
                }
                if ( current >= trip ){
                    this->stall_cnt++;
                    if ( this->stall_cnt >= MOTOR_STALL_CONFIRM_TICKS ){
                        this->stall_cnt = 0;
                        LOG_INFO("Motor stall, stop.");
                        this->execute_action(Motor::Action::STOP);
                        this->state = State::IDLE;
                    }
                }else{
                    this->stall_cnt = 0;
                    if ( current < this->run_current ){
                        this->run_current = this->run_current * 0.9 + current * 0.1;
                    }
                }
            }
        }
    }
    this->current = current;
}

void Motor::timer_trigger(void){
    if ( this->calibrating ){
        /* 校准往返都累计时间，反向时不能按开度倒计时. */
        if ( (this->state == State::RUNNING_FORWARD) || (this->state == State::RUNNING_REVERSE) ){
            this->timer_cnt += 1;
        }
        return;
    }
    if ( this->state == State::RUNNING_FORWARD ){
        this->timer_cnt += 1;
    }else if ( this->state == State::RUNNING_REVERSE ){
        if ( this->timer_cnt > 0 ){
            this->timer_cnt -= 1;
        }
        if ( (this->timer_cnt <= 0) && (this->home_seek == false) ){
            this->timer_cnt = 0;
            this->execute_action(Motor::Action::STOP);
        }
    }
}

bool Motor::reset_timer(void){
    this->timer_cnt = 0;
    return true;
}

bool Motor::get_timer_cnt(int64_t &cnt){
    cnt = this->timer_cnt;
    return true;
}

bool Motor::set_timer_cnt(int64_t cnt){
    this->timer_cnt = cnt;
    return true;
}

void Motor::set_home_seek(bool enable){
    this->home_seek = enable;
}

void Motor::set_calibrating(bool enable){
    this->calibrating = enable;
}

void Motor::set_ignore_stall(bool enable){
    this->ignore_stall = enable;
    if ( enable ){
        this->stall_cnt = 0;
    }
}
