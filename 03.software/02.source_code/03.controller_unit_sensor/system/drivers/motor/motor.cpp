#include "motor.hpp"
#include "console_service.hpp"

Motor::Motor(PortableStrategy &portable_strategy,Parameter param) : portable_strategy(portable_strategy), param(param) {
    this->state = State::IDLE;
    this->current = 0.0;
    this->stop_cnt = 0;
    this->oc_cnt = 0;
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
            this->stop_cnt = 0;
            this->state = State::RUNNING_FORWARD;
            break;
        }
        case Action::REVERSE: {
            this->portable_strategy.set_output(PortableStrategy::Pin::FORWARD,false);
            this->portable_strategy.set_output(PortableStrategy::Pin::REVERSE,true);
            this->stop_cnt = 0;
            this->state = State::RUNNING_REVERSE;
            break;
        }
        default: {
            this->portable_strategy.set_output(PortableStrategy::Pin::FORWARD,false);
            this->portable_strategy.set_output(PortableStrategy::Pin::REVERSE,false);
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
    /* 获取当前的电流. */
    uint16_t adc_value = 0;
    if ( this->portable_strategy.get_adc_value(adc_value) != true ){
        return;
    }

    double current = ((double)adc_value) * this->param.cs_k + this->param.cs_b;
    if ( current > this->param.ovc_threshold ){
        /* 电流过大. */
        if ( oc_cnt < 2 ){
            oc_cnt++;
        }else{
            oc_cnt = 0;
            this->execute_action(Motor::Action::STOP);
            this->state = State::ERROR_OVC;
        }
    }else if ( current < this->param.zero_threshold ){
        /* 电流过小，认为限位. */
        if ( stop_cnt < 3 ){
            stop_cnt++;
        }else{
            stop_cnt = 0;
            if ( (this->state == State::RUNNING_FORWARD) || (this->state == State::RUNNING_REVERSE) ){
                this->execute_action(Motor::Action::STOP);
                this->state = State::IDLE;
            }

        }
    }
    this->current = current;
}

void Motor::timer_trigger(void){
    if ( this->state == State::RUNNING_FORWARD ){
        this->timer_cnt += 1;
    }else if ( this->state == State::RUNNING_REVERSE ){
        this->timer_cnt -= 1;
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

