#ifndef TM1750_HPP
#define TM1750_HPP

#include "TWI.hpp"
#include <cstdint>

class TM1750 {
public:
    explicit TM1750(TWIMaster &twi_master);

    bool open();
    bool close();

    bool set_pixel(unsigned int seg, unsigned int com, uint8_t value);
    bool clear();
    bool flush();

protected:
    TWIMaster &twi_master;
    uint8_t data_buffer[64];
};

#endif