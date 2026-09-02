#include "ChecksumCalculator.hpp"

ChecksumCalculator::ChecksumCalculator(){
    this->checksum = 0x00;
}

void ChecksumCalculator::start(void){
    this->checksum = 0x00;
}

void ChecksumCalculator::feed(uint8_t byte){
    this->checksum = (this->checksum + byte) & 0xff;
}

void ChecksumCalculator::feed(void *buffer,unsigned int size){
    uint8_t *u8_buffer = reinterpret_cast<uint8_t*>(buffer);
    for ( int n = 0; n < size; n++ ){
        this->checksum = (this->checksum + u8_buffer[n]) & 0xff;
    }
}

uint8_t ChecksumCalculator::get(void){
    return this->checksum;
}