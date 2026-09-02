#include "rain_sensor_service.hpp"
#include "py32l090_hal_rcc.h"
#include "py32l090_hal_gpio.h"

#define PORT_RAIN_SENSOR GPIOA
#define PIN_RAIN_SENSOR GPIO_PIN_1
#define CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define ACTIVE_PIN_LEVEL GPIO_PIN_RESET

bool RainSensorService::init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    CLK_ENABLE();
    gpio_init_config.Pin = PIN_RAIN_SENSOR;
    gpio_init_config.Mode = GPIO_MODE_INPUT;
    gpio_init_config.Pull = GPIO_PULLUP;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_RAIN_SENSOR, &gpio_init_config);

    return true;
}

bool RainSensorService::get_rain_status(){
    if ( HAL_GPIO_ReadPin(PORT_RAIN_SENSOR,PIN_RAIN_SENSOR) == ACTIVE_PIN_LEVEL ){
        return true;
    }
    return false;
}