#include "lte_service.hpp"
#include "py32l090_hal_gpio.h"
#include "py32l090_hal_rcc.h"

#define PORT_PWR GPIOB
#define PIN_PWR GPIO_PIN_6

static void LTEService_board_init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init_config.Pin = PIN_PWR;
    gpio_init_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_PWR, &gpio_init_config);
}

bool LTEService::init(){
    LTEService_board_init();
    HAL_GPIO_WritePin(PORT_PWR,PIN_PWR,GPIO_PIN_SET);
    return true;
}