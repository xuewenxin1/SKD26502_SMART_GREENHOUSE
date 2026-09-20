#ifndef ENV_HPP
#define ENV_HPP

namespace Env {

bool init();

bool start();
/* 在 Main 任务里周期调用；不再单独建 env 任务（堆不够会建失败→假传感器故障）. */
void eventloop(void);

bool get_temperature(double &temperature);
bool get_humidity(double &humidity);
bool get_rain_status(bool &rain_status);
bool get_battery_percent(unsigned int &percent);
bool get_error_code(unsigned int &error_code);
bool is_temp_alarm(void);

}

#endif
