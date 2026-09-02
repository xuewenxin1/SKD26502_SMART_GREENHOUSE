#ifndef GUI_HPP
#define GUI_HPP

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

}

#endif
