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
bool start();

/* GUI 刷屏心跳：Main 监视，约 10s 不动则软复位. */
unsigned int heartbeat_tick(void);

bool button_callback(Button button, ButtonEvent event);
void set_time_and_id(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t id[6]);

}

#endif
