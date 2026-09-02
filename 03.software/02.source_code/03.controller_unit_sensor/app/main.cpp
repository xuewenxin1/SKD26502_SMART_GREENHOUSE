#include "console_service.hpp"
#include "lte_service.hpp"
#include "gui.hpp"
#include "env.hpp"
#include "config.hpp"
#include "systimer.hpp"
#include "ventilate_service.hpp"

extern "C"
int main(){
    if ( ConsoleService::init() ){
        LOG_INFO("Console Service init complete.");
    }
    SysTimer::init();
    ConfigService::init();
    Env::init();
    GUI::init();
    LTEService::init();
    VentilateService::init();

    SysTimer::regist_task(VentilateService::eventloop,100);
    SysTimer::regist_task(GUI::eventloop,100);
    SysTimer::regist_task(GUI::indev_eventloop,1);
    SysTimer::regist_task(Env::eventloop,500);
    while ( true ){
        SysTimer::eventloop();
    }
}
