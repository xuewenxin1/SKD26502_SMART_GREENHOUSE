#ifndef ADC_HPP
#define ADC_HPP

#include <cstdint>

#define ADC_FULL_SCALE 4095u

namespace ADC {

enum class Channel {
    MOTOR_CURR,
    NTC,
    RAIN_SENSOR,
    BATTERY,
};

bool init();
bool capture();
bool get(Channel channel, uint16_t &adc_value);

}

#endif
