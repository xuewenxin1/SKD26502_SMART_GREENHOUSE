#ifndef SOFT_TWI_HPP
#define SOFT_TWI_HPP

#include "TWI.hpp"

class SoftTWIMaster : public TWIMaster {
public:
    class PortableStrategy {
    public:
        enum Pin {
            Pin_SCL,
            Pin_SDA,
        };
    public:
        virtual bool open() = 0;
        virtual bool close() = 0;
        virtual bool setPin(Pin pin,bool status) = 0;
        virtual bool getPin(Pin pin) = 0;
        virtual void delay(unsigned int tick) = 0;
    };
public:
    explicit SoftTWIMaster(PortableStrategy &adapter);
    virtual ~SoftTWIMaster() = default;

    virtual bool open();
    virtual bool close();
    virtual unsigned int read(unsigned int address,uint8_t *data,unsigned int length);
    virtual unsigned int write(unsigned int address,const uint8_t *data,unsigned int length);

    virtual bool generateStart();
    virtual bool generateStop();
    virtual bool generateACK(bool ack);
    virtual bool waitingACK();

    virtual uint8_t readByte(void);
    virtual void writeByte(uint8_t byte);
    virtual unsigned int readBytes(uint8_t *data,unsigned int length);
    virtual unsigned int writeBytes(const uint8_t *data,unsigned int length);
protected:
    PortableStrategy &portable_strategy;
};

#endif