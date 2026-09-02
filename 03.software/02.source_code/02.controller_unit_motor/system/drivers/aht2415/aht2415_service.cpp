#include "aht2415_service.hpp"
#include "py32f071_hal_gpio.h"
#include "py32f071_hal_rcc.h"
#include "aht2415.hpp"
#include "SoftTWI.hpp"
#include "console_service.hpp"

#define PORT_AHT_SCL GPIOB
#define PIN_AHT_SCL GPIO_PIN_6
#define PORT_AHT_SDA GPIOB
#define PIN_AHT_SDA GPIO_PIN_7

class SoftTWIPortable : public SoftTWIMaster::PortableStrategy {
public:
    virtual bool open(){
        return true;
    }

    virtual bool close(){
        return true;
    }

    virtual bool setPin(Pin pin,bool status){
        GPIO_PinState state = status ? GPIO_PIN_SET : GPIO_PIN_RESET;
        switch ( pin ){
            case Pin::Pin_SCL: HAL_GPIO_WritePin(PORT_AHT_SCL,PIN_AHT_SCL,state); break;
            case Pin::Pin_SDA: HAL_GPIO_WritePin(PORT_AHT_SDA,PIN_AHT_SDA,state); break;
            default: return false;
        }
        return true;
    }

    virtual bool getPin(Pin pin){
        switch ( pin ){
            case Pin::Pin_SCL: return (HAL_GPIO_ReadPin(PORT_AHT_SCL,PIN_AHT_SCL) == GPIO_PIN_SET );
            case Pin::Pin_SDA: return (HAL_GPIO_ReadPin(PORT_AHT_SDA,PIN_AHT_SDA) == GPIO_PIN_SET );
        }
        return false;
    }

    virtual void delay(unsigned int tick){
        for ( volatile unsigned int cnt = 0; cnt < tick; cnt++ ){
            asm("nop");
        }
    }

};

class AHT2415Portable : public AHT2415::PortableStrategy {
public:
    virtual void delay_ms(unsigned int ms){
        for ( volatile int n = 0; n < ms; n++ ){
            for ( volatile int m = 0; m < 1000; m++ ){
                asm("nop");
            }
        }
    }
};

static SoftTWIPortable soft_twi_portable;
static AHT2415Portable aht2415_portable;
static SoftTWIMaster *soft_twi_master = nullptr;
static AHT2415 *aht2415 = nullptr;

static void AHT2415_Service_board_init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init_config.Pin = PIN_AHT_SCL;
    gpio_init_config.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_init_config.Pull = GPIO_PULLUP;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_AHT_SCL, &gpio_init_config);
    gpio_init_config.Pin = PIN_AHT_SDA;
    HAL_GPIO_Init(PORT_AHT_SDA, &gpio_init_config);
}

bool AHT2415_Service::init(){
    AHT2415_Service_board_init();
    soft_twi_master = new SoftTWIMaster(soft_twi_portable);
    if ( soft_twi_master == nullptr ){
        LOG_ERROR("Can't create soft twi master");
        return false;
    }
    soft_twi_master->open();
    aht2415 = new AHT2415(*soft_twi_master,aht2415_portable);
    if ( aht2415 == nullptr ){
        LOG_ERROR("Can't create aht2415");
        return false;
    }

    aht2415->open();
    return true;
}


bool AHT2415_Service::get(double &temperature, double &humidity){
    if ( aht2415 == nullptr ){
        LOG_ERROR("AHT2415 not init.");
        return false;
    }
    return aht2415->read(temperature,humidity);
}
