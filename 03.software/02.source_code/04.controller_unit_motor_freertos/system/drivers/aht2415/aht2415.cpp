#include "aht2415.hpp"
#include <cstdint>

static const uint8_t AHT2415_HWADDR = (0x70 >> 1);

AHT2415::AHT2415(TWIMaster &twi_master, AHT2415::PortableStrategy &portable_strategy) : twi_master(twi_master), portable_strategy(portable_strategy){

}

bool AHT2415::open(){
    uint8_t status = 0;
    this->twi_master.open();
    this->portable_strategy.delay_ms(100);
    this->twi_master.read(AHT2415_HWADDR,&status,1);
    if ( status & 0x18 != 0x18 ){
        /* 初始化流程. */
    }
    return true;
}

bool AHT2415::close(){
    return true;
}

bool AHT2415::read(double &temperature, double &humidity){
    unsigned int timeout = 0;
    uint8_t buffer[8] = {0};
    buffer[0] = 0xac;
    buffer[1] = 0x33;
    buffer[2] = 0x00;
    this->twi_master.write(AHT2415_HWADDR,buffer,3);
    /* 最多等 2 拍：未插探头时旧逻辑可堵满 ~800ms，拖死 Main 电机调度. */
    do {
        this->portable_strategy.delay_ms(80);
        this->twi_master.read(AHT2415_HWADDR,buffer,7);
    } while ( (buffer[0] & 0x80) && (timeout++ < 2) );
    if ( timeout >= 2 ){
        return false;
    }
    /* 未插温湿度计：总线常读到全 0 或全 0xFF，当作无效. */
    if ( ((buffer[1] | buffer[2] | buffer[3] | buffer[4] | buffer[5]) == 0)
         || ((buffer[1] & buffer[2] & buffer[3] & buffer[4] & buffer[5]) == 0xFF) ){
        return false;
    }

    uint32_t raw_humidity = ((static_cast<uint32_t>(buffer[1]) << 12) | (static_cast<uint32_t>(buffer[2]) << 4) | (static_cast<uint32_t>(buffer[3]) >> 4));
    humidity = (static_cast<double>(raw_humidity) * 100.0) / 1048576.0; // 2^20 = 1048576
    uint32_t raw_temperature = (((static_cast<uint32_t>(buffer[3]) & 0x0F) << 16) | (static_cast<uint32_t>(buffer[4]) << 8) | (static_cast<uint32_t>(buffer[5])));
    temperature = (static_cast<double>(raw_temperature) * 200.0 / 1048576.0) - 50.0; // 2^20 = 1048576
    return true;
}
