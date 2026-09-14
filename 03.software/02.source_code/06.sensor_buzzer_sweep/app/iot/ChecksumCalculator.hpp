#ifndef CHECKSUM_CALCULATOR_HPP
#define CHECKSUM_CALCULATOR_HPP

#include <cstdint>

class ChecksumCalculator {
public:
    explicit ChecksumCalculator();
    virtual ~ChecksumCalculator() = default;
    virtual void start(void);
    virtual void feed(uint8_t byte);
    virtual void feed(void *buffer,unsigned int size);
    virtual uint8_t get(void);
protected:
    uint8_t checksum = 0x00;
};

#endif
