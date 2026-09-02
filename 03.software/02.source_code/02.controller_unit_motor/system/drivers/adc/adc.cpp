#include "adc.hpp"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_adc.h"
#include "py32f071_hal_gpio.h"
#include "console_service.hpp"
#include "py32f071_hal_cortex.h"

#define PORT_ADC_MOTOR_CURR GPIOB
#define PIN_ADC_MOTOR_CURR GPIO_PIN_0
#define PORT_ADC_NTC GPIOA
#define PIN_ADC_NTC GPIO_PIN_5
#define PORT_ADC_RAIN_SENSOR GPIOB
#define PIN_ADC_RAIN_SENSOR GPIO_PIN_1

#define ADC_CH_MOTOR_CURR ADC_CHANNEL_8
#define ADC_CH_NTC ADC_CHANNEL_5
#define ADC_CH_RAIN_SENSOR ADC_CHANNEL_9

static ADC_HandleTypeDef hadc;
static uint32_t adc_values[3];

bool ADC::init(){
    GPIO_InitTypeDef gpio_init_config = {0};
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init_config.Pin = PIN_ADC_MOTOR_CURR;
    gpio_init_config.Mode = GPIO_MODE_ANALOG;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_ADC_MOTOR_CURR, &gpio_init_config);

    /* 初始化ADC. */
    ADC_ChannelConfTypeDef adc_channel_config = {0};
    hadc.Instance = ADC1;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc.Init.ContinuousConvMode = ENABLE;
    hadc.Init.NbrOfConversion = 3;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;

    if ( HAL_ADC_Init(&hadc) != HAL_OK ){
        return false;
    }

    adc_channel_config.Channel = ADC_CH_MOTOR_CURR;
    adc_channel_config.Rank = ADC_REGULAR_RANK_1;
    adc_channel_config.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;

    if ( HAL_ADC_ConfigChannel(&hadc, &adc_channel_config) != HAL_OK ){
        return false;
    }

    adc_channel_config.Channel = ADC_CH_NTC;
    adc_channel_config.Rank = ADC_REGULAR_RANK_2;
    adc_channel_config.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;

    if ( HAL_ADC_ConfigChannel(&hadc, &adc_channel_config) != HAL_OK ){
        return false;
    }

    adc_channel_config.Channel = ADC_CH_RAIN_SENSOR;
    adc_channel_config.Rank = ADC_REGULAR_RANK_3;
    adc_channel_config.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;

    if ( HAL_ADC_ConfigChannel(&hadc, &adc_channel_config) != HAL_OK ){
        return false;
    }

    if ( HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK ){
        return false;
    }

    __HAL_ADC_ENABLE_IT(&hadc,ADC_IT_EOC);
    HAL_NVIC_SetPriority(ADC_COMP_IRQn,1,0);
    HAL_NVIC_EnableIRQ(ADC_COMP_IRQn);

    HAL_ADC_Start_IT(&hadc);
    LOG_DEBUG("ADC init complete.");
    return true;
}

bool ADC::capture(){
    return true;
}

bool ADC::get(Channel channel, uint16_t &adc_value){
    switch ( channel ){
        case Channel::MOTOR_CURR: {
            adc_value = adc_values[0];
            break;
        }
        case Channel::NTC: {
            adc_value = adc_values[1];
            break;
        }
        case Channel::RAIN_SENSOR: {
            adc_value = adc_values[2];
            break;
        }
        default: return false;
    }
    return true;
}

extern "C"
void ADC_COMP_IRQHandler(void){
    HAL_ADC_IRQHandler(&hadc);
}

extern "C"
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc){
    static uint8_t idx = 0;
    if ( hadc->Instance == ADC1 ){
        adc_values[idx] = HAL_ADC_GetValue(hadc);
        idx = (idx + 1) % 3;
        HAL_ADC_Start_IT(hadc);
    }
}