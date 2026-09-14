#include "SoftTWI.hpp"

SoftTWIMaster::SoftTWIMaster(PortableStrategy &portable_strategy) : portable_strategy(portable_strategy) {

}

bool SoftTWIMaster::open(){
    if ( !this->portable_strategy.open() ) return false;
    return true;
}

bool SoftTWIMaster::close(){
    if ( !this->portable_strategy.close() ) return false;
    return true;
}

unsigned int SoftTWIMaster::read(unsigned int address,uint8_t *data,unsigned int length){
    if ( !this->generateStart() ) return 0;
    this->writeByte((address << 1) | 0x01);
    if ( !this->waitingACK() ) {
        this->generateStop();
        return 0;
    }
    unsigned int read_length = this->readBytes(data,length);
    this->generateStop();
    return read_length;
}

unsigned int SoftTWIMaster::write(unsigned int address,const uint8_t *data,unsigned int length){
    if ( !this->generateStart() ) return 0;
    this->writeByte((address << 1) & 0xFE);
    if ( !this->waitingACK() ) {
        this->generateStop();
        return 0;
    }
    unsigned int write_length = this->writeBytes(data,length);
    this->generateStop();
    return write_length;
}

bool SoftTWIMaster::generateStart(){
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SDA,true) ) return false;
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,true) ) return false;
    this->portable_strategy.delay(1);
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SDA,false) ) return false;
    this->portable_strategy.delay(1);
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,false) ) return false;
    this->portable_strategy.delay(1);
    return true;
}

bool SoftTWIMaster::generateStop(){
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SDA,false) ) return false;
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,true) ) return false;
    this->portable_strategy.delay(1);
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SDA,true) ) return false;
    this->portable_strategy.delay(1);
    return true;
    
}

bool SoftTWIMaster::generateACK(bool ack){
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SDA,!ack) ) return false;
    this->portable_strategy.delay(1);
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,true) ) return false;
    this->portable_strategy.delay(1);
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,false) ) return false;
    this->portable_strategy.delay(1);
    return true;
}

bool SoftTWIMaster::waitingACK(){
    unsigned int timeout = 100;
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SDA,true) ) return false;
    this->portable_strategy.delay(1);
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,true) ) return false;
    this->portable_strategy.delay(1);
    while ( timeout-- > 0 ){
        if ( !this->portable_strategy.getPin(PortableStrategy::Pin_SDA) ){  
            if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,false) ) return false;
            this->portable_strategy.delay(1);
            return true;
        }
    }
    return false;
}

uint8_t SoftTWIMaster::readByte(void){
    uint8_t byte = 0x00;
    this->portable_strategy.setPin(PortableStrategy::Pin_SDA,true);
    for ( uint8_t n = 0; n < 8; n++ ){
        byte <<= 1;
        if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,false) ) return 0x00;
        this->portable_strategy.delay(1);
        if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,true) ) return 0x00;
        this->portable_strategy.delay(1);
        if ( this->portable_strategy.getPin(PortableStrategy::Pin_SDA) ){
            byte |= 0x01;
        }
    }
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,false) ) return 0x00;
    return byte;
}

void SoftTWIMaster::writeByte(uint8_t byte){
    for ( uint8_t n = 0; n < 8; n++ ){
        if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,false) ) return;
        this->portable_strategy.delay(1);
        if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SDA,((byte & 0x80) != 0) ? true : false) ) return;
        byte <<= 1;
        this->portable_strategy.delay(1);
        if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,true) ) return;
        this->portable_strategy.delay(1);
    }
    if ( !this->portable_strategy.setPin(PortableStrategy::Pin_SCL,false) ) return;
    return;
}

unsigned int SoftTWIMaster::readBytes(uint8_t *data,unsigned int length){
    for ( unsigned int n = 0; n < length; n++ ){
        data[n] = this->readByte();
        if ( n < (length - 1) ){
            if ( !this->generateACK(true) ) return n;
        } else {
            if ( !this->generateACK(false) ) return n;
        }
    }
    return length;
}

unsigned int SoftTWIMaster::writeBytes(const uint8_t *data,unsigned int length){
    for ( unsigned int n = 0; n < length; n++ ){
        this->writeByte(data[n]);
        if ( !this->waitingACK() ) return n;
    }
    return length;
}

