#ifndef INDEV_HPP
#define INDEV_HPP

#include "gui.hpp"

namespace Indev {

bool init();
void eventloop();
bool get_button_status(GUI::Button button);

}

#endif
