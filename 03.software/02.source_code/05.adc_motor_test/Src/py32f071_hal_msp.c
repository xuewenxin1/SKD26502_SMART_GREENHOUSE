#include "main.h"

/* 参考 PY32F071 官方 ADC_SingleConversion_TriggerSW_IT 示例. */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

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

    /* PA5 = NTC (CH5), PB0 = 电机电流 (CH8), PB1 = 雨雪 (CH9). */
    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);
}
