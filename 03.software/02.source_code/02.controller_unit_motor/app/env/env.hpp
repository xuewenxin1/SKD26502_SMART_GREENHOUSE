#ifndef ENV_HPP
#define ENV_HPP

namespace Env {

bool init();

void eventloop();

bool get_temperature(double &temperature);
bool get_humidity(double &humidity);
bool get_rain_status(bool &rain_status);
bool get_error_code(unsigned int &error_code);

}

#endif
