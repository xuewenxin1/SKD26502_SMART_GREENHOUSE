#ifndef TWI_HPP
#define TWI_HPP

#include <stdint.h>

class TWIMaster {
public:
    virtual ~TWIMaster() = default;

    virtual bool open() = 0;
    virtual bool close() = 0;
    virtual unsigned int read(unsigned int address,uint8_t *data,unsigned int length) = 0;
    virtual unsigned int write(unsigned int address,const uint8_t *data,unsigned int length) = 0;

    virtual bool generateStart() = 0;
    virtual bool generateStop() = 0;
    virtual bool generateACK(bool ack) = 0;
    virtual bool waitingACK() = 0;

    virtual uint8_t readByte(void) = 0;
    virtual void writeByte(uint8_t byte) = 0;
    virtual unsigned int readBytes(uint8_t *data,unsigned int length) = 0;
    virtual unsigned int writeBytes(const uint8_t *data,unsigned int length) = 0;
};

#endif