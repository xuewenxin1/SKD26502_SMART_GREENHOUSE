#include "console_service.hpp"
#include "lte_service.hpp"
#include "gui.hpp"
#include "env.hpp"
#include "config.hpp"
#include "systimer.hpp"
#include "ventilate_service.hpp"
#include "iot_service.hpp"

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
    IOTService::init();

    LOG_INFO("System init complete.");

    SysTimer::regist_task(VentilateService::eventloop,100);
    SysTimer::regist_task(GUI::eventloop,100);
    SysTimer::regist_task(GUI::indev_eventloop,1);
    SysTimer::regist_task(Env::eventloop,500);
    SysTimer::regist_task(IOTService::eventloop,2000);
    while ( true ){
        SysTimer::eventloop();
    }
}
