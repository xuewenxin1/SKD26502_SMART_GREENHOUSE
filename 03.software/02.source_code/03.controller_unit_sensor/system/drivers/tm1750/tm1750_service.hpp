#ifndef TM1750_SERVICE_HPP
#define TM1750_SERVICE_HPP

#include <cstdint>

namespace TM1750_Service {

bool init();
bool clear();
bool set_pixel(unsigned int seg, unsigned int com, uint8_t value);
bool flush();

}

#endif
