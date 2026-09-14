#include "Button.hpp"

static const Button::Configuration __Button_default_configuration = {
    .long_press_tick_count = 300,
    .short_press_tick_count = 1,
    .idle_tick_count = 40,
    .long_press_repeat_tick_count = 0,
};

Button::Button(Adapter *adapter){
    this->adapter = adapter;
    this->last_status = false;
    this->configuration = __Button_default_configuration;
    this->short_press_callback = nullptr;
    this->long_press_callback = nullptr;
    this->click_callback = nullptr;
    this->key_value_callback = nullptr;
    this->n_value = 0;
    this->pressed_tick = 0;
    this->released_tick = 0;
}

Button::Button(Adapter *adapter,Configuration configuration){
    this->adapter = adapter;
    this->last_status = false;
    this->configuration = configuration;
    this->short_press_callback = nullptr;
    this->long_press_callback = nullptr;
    this->click_callback = nullptr;
    this->key_value_callback = nullptr;
    this->n_value = 0;
    this->pressed_tick = 0;
    this->released_tick = 0;
}

Button::~Button(){ }

bool Button::get(){
    if ( this->adapter != nullptr ){
        return this->adapter->get();
    }
    return false;
}

bool Button::setClickCallback(ClickCallback click_callback,int extra){
    this->click_callback = click_callback;
    this->click_callback_extra = extra;
    return true;
}

bool Button::setShortPressCallback(ShortPressCallback short_press_callback,int extra){
    this->short_press_callback = short_press_callback;
    this->shortpress_callback_extra = extra;
    return true;
}

bool Button::setLongPressCallback(LongPressCallback long_press_callback,int extra){
    this->long_press_callback = long_press_callback;
    this->longpress_callback_extra = extra;
    return true;
}

bool Button::setLongPressRepeatTickCount(int tick_count){
    if ( tick_count < 0 ){
        tick_count = 0;
    }
    this->configuration.long_press_repeat_tick_count = tick_count;
    return true;
}

bool Button::setKeyValueCallback(KeyValueCallback key_value_callback){
    this->key_value_callback = key_value_callback;
    return true;
}

void Button::eventloop(){
    if ( this->adapter == nullptr ) return;
    bool current_status = this->adapter->get();
    if ( current_status == false && last_status == false ){
        if ( this->n_value > 0 ){
            if ( this->released_tick > this->configuration.idle_tick_count ){
                if ( this->key_value_callback != nullptr ){
                    int value_count = this->n_value;
                    if ( value_count > VALUE_SEQ_LENGTH ){
                        value_count = VALUE_SEQ_LENGTH;
                    }
                    this->key_value_callback(value_count, this->value_seq);
                }
                this->n_value = 0;
                this->released_tick = 0;
            }else{
                this->released_tick += 1;
            }
        }else{
            this->released_tick = 0;
        }
    }else if ( current_status == true && last_status == false ){
        this->pressed_tick = 0;
    }else if ( current_status == true && last_status == true ){
        if ( this->pressed_tick == this->configuration.short_press_tick_count ){
            if ( this->short_press_callback != nullptr ){
                this->short_press_callback(this->shortpress_callback_extra);
            }
        }
        if ( this->pressed_tick == this->configuration.long_press_tick_count ){
            if ( this->long_press_callback != nullptr ){
                this->long_press_callback(this->longpress_callback_extra);
            }
        }else if ( (this->configuration.long_press_repeat_tick_count > 0)
                && (this->pressed_tick > this->configuration.long_press_tick_count)
                && (((this->pressed_tick - this->configuration.long_press_tick_count)
                    % this->configuration.long_press_repeat_tick_count) == 0) ){
            if ( this->long_press_callback != nullptr ){
                this->long_press_callback(this->longpress_callback_extra);
            }
        }
        this->pressed_tick += 1;
    }else if ( current_status == false && last_status == true ){
        char status = 0;
        if ( this->pressed_tick > this->configuration.long_press_tick_count ){
            status = '-';
            if ( this->click_callback != nullptr ) this->click_callback(status,this->click_callback_extra);
        }else if ( this->pressed_tick > this->configuration.short_press_tick_count ){
            status = '.';
            if ( this->click_callback != nullptr ) this->click_callback(status,this->click_callback_extra);
        }
        if ( (status != 0)
                && (this->key_value_callback != nullptr)
                && (this->n_value >= 0)
                && (this->n_value < VALUE_SEQ_LENGTH) ){
            this->value_seq[this->n_value++] = status;
        }
        this->released_tick = 0;
    }
    this->last_status = current_status;
}
