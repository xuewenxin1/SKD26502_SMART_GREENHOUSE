#ifndef AHT2415_HPP
#define AHT2415_HPP

#include "TWI.hpp"

class AHT2415 {
public:
    class PortableStrategy {
    public:
        virtual void delay_ms(unsigned int nms) = 0;
    };
public:
    explicit AHT2415(TWIMaster &twi_master, PortableStrategy &portable_strategy);

    bool open();
    bool close();

    bool read(double &temperature, double &humidity);
protected:
    TWIMaster &twi_master;
    PortableStrategy &portable_strategy;
};

#endif
