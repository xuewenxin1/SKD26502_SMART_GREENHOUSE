#include "tm1750_service.hpp"
#include "tm1750.hpp"
#include "SoftTWI.hpp"
#include "py32f071_hal_gpio.h"
#include "py32f071_hal_rcc.h"
#include "console_logger.h"
#include "config.hpp"
#include "FreeRTOS.h"
#include "task.h"

#define PORT_TM1750_SCL GPIOB
#define PIN_TM1750_SCL GPIO_PIN_8
#define PORT_TM1750_SDA GPIOB
#define PIN_TM1750_SDA GPIO_PIN_9

class TM1750SoftTWIPortable : public SoftTWIMaster::PortableStrategy {
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
            case Pin::Pin_SCL: HAL_GPIO_WritePin(PORT_TM1750_SCL,PIN_TM1750_SCL,state); break;
            case Pin::Pin_SDA: HAL_GPIO_WritePin(PORT_TM1750_SDA,PIN_TM1750_SDA,state); break;
            default: return false;
        }
        return true;
    }

    virtual bool getPin(Pin pin){
        switch ( pin ){
            case Pin::Pin_SCL: return (HAL_GPIO_ReadPin(PORT_TM1750_SCL,PIN_TM1750_SCL) == GPIO_PIN_SET );
            case Pin::Pin_SDA: return (HAL_GPIO_ReadPin(PORT_TM1750_SDA,PIN_TM1750_SDA) == GPIO_PIN_SET );
        }
        return false;
    }

    virtual void delay(unsigned int tick){
        for ( volatile unsigned int cnt = 0; cnt < tick; cnt++ ){
            asm("nop");
        }
    }

};

static TM1750SoftTWIPortable tm1750_soft_twi_portable;
static SoftTWIMaster *soft_twi_master = nullptr;
static TM1750 *tm1750 = nullptr;

static void TM1750_board_init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init_config.Pin = PIN_TM1750_SCL;
    gpio_init_config.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_init_config.Pull = GPIO_PULLUP;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_TM1750_SCL, &gpio_init_config);
    gpio_init_config.Pin = PIN_TM1750_SDA;
    HAL_GPIO_Init(PORT_TM1750_SDA, &gpio_init_config);
    HAL_GPIO_WritePin(PORT_TM1750_SCL,PIN_TM1750_SCL,GPIO_PIN_SET);
    HAL_GPIO_WritePin(PORT_TM1750_SDA,PIN_TM1750_SDA,GPIO_PIN_SET);
}

bool TM1750_Service::init(){
    TM1750_board_init();
    soft_twi_master = new SoftTWIMaster(tm1750_soft_twi_portable);
    if ( soft_twi_master == nullptr ){
        LOG_ERROR("Can't create soft twi master");
        return false;
    }
    soft_twi_master->open();
    tm1750 = new TM1750(*soft_twi_master);
    if ( tm1750 == nullptr ){
        LOG_ERROR("Can't create tm1750");
        return false;
    }
    tm1750->open();
    return true;
}

bool TM1750_Service::clear(){
    if ( tm1750 == nullptr ){
        return false;
    }
    return tm1750->clear();
}

bool TM1750_Service::set_pixel(unsigned int seg, unsigned int com, uint8_t value){
    if ( tm1750 == nullptr ){
        return false;
    }
    return tm1750->set_pixel(seg,com,value);
}

bool TM1750_Service::flush(){
    if ( tm1750 == nullptr ){
        return false;
    }
    /* 刷屏绝不长时间堵死 GUI：Flash 占用时跳过本帧；短临界区发 I2C，不用 SuspendAll. */
    if ( ConfigService::try_lock_hw() == false ){
        return false;
    }
    taskENTER_CRITICAL();
    bool ok = tm1750->flush();
    taskEXIT_CRITICAL();
    ConfigService::unlock_hw();
    return ok;
}