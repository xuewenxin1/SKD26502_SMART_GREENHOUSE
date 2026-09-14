#ifndef ENV_HPP
#define ENV_HPP

namespace Env {

bool init();

bool start();

bool get_temperature(double &temperature);
bool get_humidity(double &humidity);
bool get_rain_status(bool &rain_status);
bool get_battery_percent(unsigned int &percent);
bool get_error_code(unsigned int &error_code);
bool is_temp_alarm(void);

}

#endif
