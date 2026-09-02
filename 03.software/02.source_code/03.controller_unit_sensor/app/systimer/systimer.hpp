#ifndef SYS_TIMER_HPP
#define SYS_TIMER_HPP

namespace SysTimer {

bool init();

bool regist_task(void (*eventloop)(void),int interval);
void eventloop();

bool start_timer(int timer_num);
bool stop_timer(int timer_num);
bool reset_timer(int timer_num);
bool get_timer_cnt(int timer_num, unsigned int &cnt);

}

#endif
