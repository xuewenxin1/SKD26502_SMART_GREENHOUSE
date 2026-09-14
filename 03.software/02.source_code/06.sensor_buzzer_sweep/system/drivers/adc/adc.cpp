#include "adc.hpp"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_adc.h"
#include "py32f071_hal_adc_ex.h"
#include "py32f071_hal_gpio.h"
#include "console_logger.h"
#include "py32f071_hal_cortex.h"
#include "FreeRTOS.h"
#include "semphr.h"

#define PORT_ADC_MOTOR_CURR GPIOB
#define PIN_ADC_MOTOR_CURR GPIO_PIN_0
#define PORT_ADC_NTC GPIOA
#define PIN_ADC_NTC GPIO_PIN_5
#define PORT_ADC_RAIN_SENSOR GPIOB
#define PIN_ADC_RAIN_SENSOR GPIO_PIN_1
#define PORT_ADC_BATTERY GPIOA
#define PIN_ADC_BATTERY GPIO_PIN_4

#define ADC_CH_MOTOR_CURR ADC_CHANNEL_8
#define ADC_CH_NTC ADC_CHANNEL_5
#define ADC_CH_RAIN_SENSOR ADC_CHANNEL_9
#define ADC_CH_BATTERY ADC_CHANNEL_4

static ADC_HandleTypeDef hadc;
static SemaphoreHandle_t adc_mutex = nullptr;

static void adc_recover(void){
    HAL_ADC_Stop(&hadc);
    hadc.State = HAL_ADC_STATE_READY;
    __HAL_UNLOCK(&hadc);
}

static bool read_adc_channel(uint32_t channel, uint16_t &value){
    ADC_ChannelConfTypeDef ch_conf = {0};

    HAL_ADC_Stop(&hadc);
    ch_conf.Channel = channel;
    ch_conf.Rank = ADC_REGULAR_RANK_1;
    ch_conf.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if ( HAL_ADC_ConfigChannel(&hadc, &ch_conf) != HAL_OK ){
        adc_recover();
        return false;
    }
    if ( HAL_ADC_Start(&hadc) != HAL_OK ){
        adc_recover();
        return false;
    }
    if ( HAL_ADC_PollForConversion(&hadc, 50) != HAL_OK ){
        adc_recover();
        return false;
    }
    value = (uint16_t)HAL_ADC_GetValue(&hadc);
    HAL_ADC_Stop(&hadc);
    return true;
}

bool ADC::init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    if ( adc_mutex == nullptr ){
        adc_mutex = xSemaphoreCreateMutex();
        if ( adc_mutex == nullptr ){
            LOG_ERROR("ADC mutex create failed.");
            return false;
        }
    }

    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init_config.Pin = PIN_ADC_NTC;
    gpio_init_config.Mode = GPIO_MODE_ANALOG;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_ADC_NTC, &gpio_init_config);

    gpio_init_config.Pin = PIN_ADC_BATTERY;
    HAL_GPIO_Init(PORT_ADC_BATTERY, &gpio_init_config);

    gpio_init_config.Pin = PIN_ADC_MOTOR_CURR;
    HAL_GPIO_Init(PORT_ADC_MOTOR_CURR, &gpio_init_config);

    gpio_init_config.Pin = PIN_ADC_RAIN_SENSOR;
    HAL_GPIO_Init(PORT_ADC_RAIN_SENSOR, &gpio_init_config);

    hadc.Instance = ADC1;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;

    if ( HAL_ADC_Init(&hadc) != HAL_OK ){
        return false;
    }
    if ( HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK ){
        LOG_ERROR("ADC calibration failed.");
        return false;
    }

    LOG_DEBUG("ADC init complete.");
    return true;
}

bool ADC::capture(){
    return true;
}

bool ADC::get(Channel channel, uint16_t &adc_value){
    uint32_t adc_ch = 0;

    if ( adc_mutex == nullptr ){
        return false;
    }
    switch ( channel ){
        case Channel::MOTOR_CURR: adc_ch = ADC_CH_MOTOR_CURR; break;
        case Channel::NTC: adc_ch = ADC_CH_NTC; break;
        case Channel::RAIN_SENSOR: adc_ch = ADC_CH_RAIN_SENSOR; break;
        case Channel::BATTERY: adc_ch = ADC_CH_BATTERY; break;
        default: return false;
    }

    if ( xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(100)) != pdPASS ){
        return false;
    }
    bool ok = read_adc_channel(adc_ch, adc_value);
    xSemaphoreGive(adc_mutex);
    return ok;
}
