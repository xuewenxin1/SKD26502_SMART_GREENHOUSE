#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

namespace GUI {

enum class Page {
    Home,   /* 主页. */
    Setting,    /* 设置页 */
    MotorCali,  /* 电机校准页. */
};

enum class Button {
    Invalid,
    Mode,
    Setting,
    Plus,
    Minus,
};

enum class ButtonEvent {
    Click,
    LongPress,
};


bool init();
bool button_callback(Button button, ButtonEvent event);
void eventloop();
void indev_eventloop();
void set_time_and_id(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t id[6]);

}

#endif
