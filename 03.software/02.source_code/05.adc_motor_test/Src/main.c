/**
 * ADC 对比测试：基于官方 ADC_SingleConversion_TriggerSW_IT，
 * 读取与 motor 相同的三个通道并串口打印。
 *
 * 官方示例路径：
 * PY32F071_Firmware_V1.1.3/Projects/PY32F071-STK/Example/ADC/ADC_SingleConversion_TriggerSW_IT
 */
#include "main.h"
#include <stdio.h>

#define VDDA_MV 3860u
#define ADC_MAX 4095u

/* 与 motor adc.cpp 一致. */
#define ADC_CH_NTC         ADC_CHANNEL_5
#define ADC_CH_MOTOR_CURR  ADC_CHANNEL_8
#define ADC_CH_RAIN        ADC_CHANNEL_9

static ADC_HandleTypeDef hadc;
static const char *ch_name[] = { "NTC_CH5", "MOTOR_CH8", "RAIN_CH9" };
static const uint32_t ch_list[] = { ADC_CH_NTC, ADC_CH_MOTOR_CURR, ADC_CH_RAIN };

extern void USART_Board_Init(void);

static void APP_SystemClockConfig(void);
static void APP_AdcConfig(void);
static uint16_t APP_ReadChannel(uint32_t channel, uint32_t sample_time);

static uint16_t APP_ReadChannel(uint32_t channel, uint32_t sample_time)
{
    ADC_ChannelConfTypeDef cfg = {0};

    cfg.Channel = channel;
    cfg.Rank = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = sample_time;
    if ( HAL_ADC_ConfigChannel(&hadc, &cfg) != HAL_OK ){
        APP_ErrorHandler();
    }
    HAL_ADC_Start(&hadc);
    HAL_ADC_PollForConversion(&hadc, 100);
    return (uint16_t)HAL_ADC_GetValue(&hadc);
}

static void APP_AdcConfig(void)
{
    hadc.Instance = ADC1;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.NbrOfDiscConversion = 1;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;

    if ( HAL_ADC_Init(&hadc) != HAL_OK ){
        APP_ErrorHandler();
    }
    if ( HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK ){
        APP_ErrorHandler();
    }
}

static void APP_SystemClockConfig(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSIDiv = RCC_HSI_DIV1;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_8MHz;
    osc.PLL.PLLState = RCC_PLL_OFF;
    if ( HAL_RCC_OscConfig(&osc) != HAL_OK ){
        APP_ErrorHandler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSISYS;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if ( HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK ){
        APP_ErrorHandler();
    }
}

static void APP_PrintBlock(const char *tag, uint32_t sample_time)
{
    unsigned int i;

    printf("\r\n[%s] sample=0x%lx\r\n", tag, (unsigned long)sample_time);
    for ( i = 0; i < 3u; i++ ){
        uint16_t raw = APP_ReadChannel(ch_list[i], sample_time);
        unsigned int mv = ((unsigned int)raw * VDDA_MV + (ADC_MAX / 2u)) / ADC_MAX;
        printf("  %s raw=%u mV=%u\r\n", ch_name[i], (unsigned int)raw, mv);
    }
}

int main(void)
{
    HAL_Init();
    APP_SystemClockConfig();
    USART_Board_Init();
    APP_AdcConfig();

    printf("\r\n=== ADC motor channel test (official HAL style) ===\r\n");
    printf("SYSCLK=%lu Hz PCLK1=%lu Hz ADCclk=%lu Hz\r\n",
           (unsigned long)HAL_RCC_GetHCLKFreq(),
           (unsigned long)HAL_RCC_GetPCLK1Freq(),
           (unsigned long)HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC));
    printf("VDDA assume %u mV for mV column\r\n", (unsigned int)VDDA_MV);

    while ( 1 ){
        /* 官方示例：28.5 cycles + 校准 + PCLK/4. */
        APP_PrintBlock("official_28cyc", ADC_SAMPLETIME_28CYCLES_5);
        /* motor 当前：239.5 cycles，无 MSP 时钟/无校准. */
        APP_PrintBlock("motor_239cyc", ADC_SAMPLETIME_239CYCLES_5);
        HAL_Delay(1000);
    }
}

void APP_ErrorHandler(void)
{
    while ( 1 ){
    }
}
