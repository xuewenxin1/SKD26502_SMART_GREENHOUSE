#include "tm1750.hpp"

static const uint8_t TM1750_HWADDR = 0b0111110;

TM1750::TM1750(TWIMaster &twi_master) : twi_master(twi_master){
    for ( int n = 0; n < 64; n++ ){
        this->data_buffer[n] = 0x00;
    }
}

bool TM1750::open(){
    uint8_t cmd_buffer[16] = {0};
    for ( int n = 0; n < 10000; n++ ){
        asm("nop");
    }
    cmd_buffer[0] = 0xf2;
    cmd_buffer[1] = 0xe2;
    cmd_buffer[2] = 0xc0;
    this->twi_master.write(TM1750_HWADDR,cmd_buffer,3);
    cmd_buffer[0] = 0xf1;
    this->twi_master.write(TM1750_HWADDR,cmd_buffer,1);
    return true;
}

bool TM1750::close(){
    return true;
}

bool TM1750::set_pixel(unsigned int seg, unsigned int com, uint8_t value){
    if ( seg > 35 ){
        return false;
    }
    if ( com > 7 ){
        return false;
    }
    if ( value == 0 ){
        this->data_buffer[seg + 1] &= ~(0x80 >> com);
    }else{
        this->data_buffer[seg + 1] |= (0x80 >> com);
    }
    
    return true;
}

bool TM1750::clear(){
    for ( int n = 0; n < 64; n++ ){
        this->data_buffer[n] = 0x00;
    }
    return true;
}

bool TM1750::flush(){
    uint8_t cmd_buffer = 0xf1;
    this->data_buffer[0] = 0x00;
    this->twi_master.write(TM1750_HWADDR, this->data_buffer, 36);
    this->twi_master.write(TM1750_HWADDR,&cmd_buffer,1);
    return true;
}

