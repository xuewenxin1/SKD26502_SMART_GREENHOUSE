#include "py32f071_hal_adc.h"
#include "py32f071_hal_gpio.h"
#include "py32f071_hal_rcc.h"

/* 与 motor adc.cpp 引脚一致：PA5 NTC，PB0 电流，PB1 雨雪. */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clk = {0};

    (void)hadc;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_ADC_CLK_ENABLE();

    clk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    clk.ADCClockSelection = RCC_ADCCLKSOURCE_PCLK_DIV4;
    HAL_RCCEx_PeriphCLKConfig(&clk);

    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);
}
