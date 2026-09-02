#ifndef VENTILATE_SERVICE_HPP
#define VENTILATE_SERVICE_HPP

#include "motor.hpp"

namespace VentilateService {

enum class Status {
    ALIGN,          /* 正在对齐. */
    CALIBRATING_STAGE1,    /* 正在校准. */
    CALIBRATING_STAGE2,
    CALIBRATING_STAGE3,
    STOPPED,        /* 未动作. */
    OPENING,        /* 正在打开. */
    CLOSING,        /* 正在关闭. */
    OCP,            /* 过流保护. */    
};

enum class ForceAction {
    None,
    ForceOpen,
    ForceClose,
};

bool init();
bool align();
bool calibrate();
void eventloop();
bool force(ForceAction force_action);
bool set_opening_percentage(int value);
bool get_opening_percentage(int &value);
bool get_status(Status &status);

bool get_motor_state(Motor::State &state);


}

#endif
