#ifndef ADC_HPP
#define ADC_HPP

#include <cstdint>

namespace ADC {

enum class Channel {
    MOTOR_CURR,
    NTC,
    RAIN_SENSOR,
};

bool init();
bool capture();
bool get(Channel channel, uint16_t &adc_value);

}

#endif
