#ifndef VENTILATE_SERVICE_HPP
#define VENTILATE_SERVICE_HPP

#include "motor.hpp"
#include <cstdint>

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
    ForceStop,
    ForceOpen,
    ForceClose,
};

bool init();
bool start();
void eventloop();

bool align();
bool home_after_stroke_change(); /* 行程圈数变更后先回 0 位. */
bool calibrate();
bool force(ForceAction force_action);
bool on_mode_changed(void); /* 切换工作模式：保持当前开度，不强制归零. */
bool set_opening_percentage(int value);
/* 精确目标圈数（按 C1 换成开度%，保留小数，避免整数截断提前停）. */
bool set_target_turns(int turns);
/* APP/模组下发的时钟，定时模式用. */
bool set_clock(uint8_t hour, uint8_t minute, bool valid);
bool get_opening_percentage(int &value);
/* 当前已走完的整数圈数；未满一圈为 0. */
bool get_current_turns(int &turns);
bool get_status(Status &status);
/* 限位确认中：开/关灯保持亮，直到电机 IDLE 满 5s 判到头. */
bool end_confirm_lamp(bool &open_lamp, bool &close_lamp);

bool get_motor_state(Motor::State &state);


}

#endif
