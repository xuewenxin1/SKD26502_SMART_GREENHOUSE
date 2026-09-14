#include "env.hpp"
#include "aht2415_service.hpp"
#include "adc.hpp"
#include "rain_sensor_service.hpp"
#include "console_logger.h"
#include "ntc_service.hpp"
#include "config.hpp"
#include "py32f071_hal_gpio.h"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_tim.h"
#include "py32f071_hal_cortex.h"
#include "system_py32f0xx.h"
#include "FreeRTOS.h"
#include "task.h"

#define PORT_BAT_C GPIOB
#define PIN_BAT_C GPIO_PIN_15
/* PA12 无硬件 PWM 通道，用 TIM14 翻转方波. */
#define PORT_BUZZER GPIOA
#define PIN_BUZZER GPIO_PIN_12
#define BATTERY_SAMPLE_INTERVAL_LOOPS 2
#define ADC_VREF_MV 3600u
#define ADC_EMPTY_MV 1800u
#define ADC_FULL_MV 2100u

/* 蜂鸣器扫频测试：各频率响 2s，循环. */
static const uint16_t BUZZER_SWEEP_HZ[] = {50u, 100u, 200u, 300u, 500u, 666u, 900u};
static const unsigned int BUZZER_SWEEP_MS = 2000u;

/* P4 温度补偿：内部 0.1℃，显示/报警/自动模式/MQTT 统一加在实测温度上. */
static double apply_temp_compensation(double raw_temp){
    Config_t config;
    ConfigService::get_config(config);
    return raw_temp + ((double)config.temp_compensation_value / 10.0);
}

static struct EnvStatus {
    struct {
        double temperature;
        double humidity;
        bool rain_status;
        unsigned int battery_percent;
    } value;
    struct {
        bool temperature;
        bool humidity;
    } valid_status;
} env_status = {
    .value = {
        .temperature = 20.1,
        .humidity = 20,
        .rain_status = false,
        .battery_percent = 0,
    },
    .valid_status = {
        .temperature = false,
        .humidity = false,
    },
};

static TaskHandle_t task_handle_env = nullptr;
static bool temp_alarm_active = false;
static TIM_HandleTypeDef htim_buzzer;
static volatile bool buzzer_pwm_running = false;

static void buzzer_apply_freq(uint32_t freq_hz){
    if ( freq_hz < 1u ){
        freq_hz = 1u;
    }
    SystemCoreClockUpdate();
    /* 方波翻转频率 = 2 * 音调频率. */
    uint32_t toggle_hz = freq_hz * 2u;
    uint32_t ticks = SystemCoreClock / toggle_hz;
    if ( ticks < 2u ){
        ticks = 2u;
    }
    /* TIM14 ARR 16 位；低频用分频. */
    uint32_t psc = (ticks + 65535u) / 65536u;
    if ( psc < 1u ){
        psc = 1u;
    }
    uint32_t arr = ticks / psc;
    if ( arr < 2u ){
        arr = 2u;
    }
    __HAL_TIM_DISABLE(&htim_buzzer);
    __HAL_TIM_SET_PRESCALER(&htim_buzzer, psc - 1u);
    __HAL_TIM_SET_AUTORELOAD(&htim_buzzer, arr - 1u);
    __HAL_TIM_SET_COUNTER(&htim_buzzer, 0);
    if ( buzzer_pwm_running ){
        __HAL_TIM_ENABLE(&htim_buzzer);
    }
}

static void buzzer_set(bool on){
    if ( on ){
        if ( buzzer_pwm_running == false ){
            buzzer_pwm_running = true;
            __HAL_TIM_SET_COUNTER(&htim_buzzer, 0);
            HAL_TIM_Base_Start_IT(&htim_buzzer);
        }
    }else{
        if ( buzzer_pwm_running ){
            buzzer_pwm_running = false;
            HAL_TIM_Base_Stop_IT(&htim_buzzer);
            HAL_GPIO_WritePin(PORT_BUZZER, PIN_BUZZER, GPIO_PIN_RESET);
        }
    }
}

static void buzzer_pwm_init(void){
    GPIO_InitTypeDef gpio_init_config = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM14_CLK_ENABLE();

    gpio_init_config.Pin = PIN_BUZZER;
    gpio_init_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_BUZZER, &gpio_init_config);
    HAL_GPIO_WritePin(PORT_BUZZER, PIN_BUZZER, GPIO_PIN_RESET);

    SystemCoreClockUpdate();
    htim_buzzer.Instance = TIM14;
    htim_buzzer.Init.Prescaler = 0;
    htim_buzzer.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_buzzer.Init.Period = 999;
    htim_buzzer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_buzzer.Init.RepetitionCounter = 0;
    htim_buzzer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if ( HAL_TIM_Base_Init(&htim_buzzer) != HAL_OK ){
        LOG_ERROR("Buzzer TIM14 init failed.");
        return;
    }
    HAL_NVIC_SetPriority(TIM14_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM14_IRQn);
    buzzer_apply_freq(BUZZER_SWEEP_HZ[0]);
    LOG_INFO("Buzzer PA12 sweep test ready, clk %u.", (unsigned int)SystemCoreClock);
}

extern "C" void TIM14_IRQHandler(void){
    if ( __HAL_TIM_GET_FLAG(&htim_buzzer, TIM_FLAG_UPDATE) != RESET ){
        __HAL_TIM_CLEAR_IT(&htim_buzzer, TIM_IT_UPDATE);
        if ( buzzer_pwm_running ){
            HAL_GPIO_TogglePin(PORT_BUZZER, PIN_BUZZER);
        }
    }
}

static void battery_gpio_init(void){
    GPIO_InitTypeDef gpio_init_config = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio_init_config.Pin = PIN_BAT_C;
    gpio_init_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_BAT_C, &gpio_init_config);
    HAL_GPIO_WritePin(PORT_BAT_C, PIN_BAT_C, GPIO_PIN_SET);
    LOG_INFO("BAT_C PB15 hold high.");
}

static unsigned int sample_battery_percent(void){
    uint32_t acc = 0;
    uint16_t raw = 0;
    for ( int n = 0; n < 8; n++ ){
        if ( ADC::get(ADC::Channel::BATTERY, raw) != true ){
            return env_status.value.battery_percent;
        }
        acc += raw;
    }

    raw = (uint16_t)(acc / 8);
    uint32_t adc_mv = ((uint32_t)raw * ADC_VREF_MV + (ADC_FULL_SCALE / 2u)) / ADC_FULL_SCALE;
    unsigned int percent = 0;
    if ( adc_mv <= ADC_EMPTY_MV ){
        percent = 0;
    }else if ( adc_mv >= ADC_FULL_MV ){
        percent = 100;
    }else{
        percent = (unsigned int)((adc_mv - ADC_EMPTY_MV) * 100u / (ADC_FULL_MV - ADC_EMPTY_MV));
    }
    LOG_INFO("Battery %u percent, adc raw %u, %u mV.", percent, (unsigned int)raw, (unsigned int)adc_mv);
    return percent;
}

bool Env::init(){
    LOG_INFO("Init env service (buzzer sweep).");
    if ( ADC::init() != true ){
        LOG_ERROR("ADC init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("ADC init success.");
    }
    battery_gpio_init();
    if ( AHT2415_Service::init() != true ){
        LOG_ERROR("AHT2415 init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("AHT2415 init success.");
    }
    if ( NTC_Service::init() != true ){
        LOG_ERROR("NTC init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("NTC init success.");
    }
    if ( RainSensorService::init() != true ){
        LOG_ERROR("Rain sensor init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("Rain sensor init success.");
    }
    buzzer_pwm_init();
    LOG_INFO("Env service init success.");
    return true;
}

void Env_eventloop(void *param){
    unsigned int battery_loop_cnt = 0;
    unsigned int sweep_idx = 0;
    const unsigned int sweep_count =
        (unsigned int)(sizeof(BUZZER_SWEEP_HZ) / sizeof(BUZZER_SWEEP_HZ[0]));
    TickType_t tone_deadline = 0;

    LOG_INFO("env service running (buzzer sweep).");
    buzzer_apply_freq(BUZZER_SWEEP_HZ[0]);
    buzzer_set(true);
    tone_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BUZZER_SWEEP_MS);
    LOG_INFO("Buzzer %u Hz for %u ms.", (unsigned int)BUZZER_SWEEP_HZ[0], BUZZER_SWEEP_MS);

    while ( true ){
        double aht_temp = 0.0;
        double aht_humi = 0.0;
        double ntc_temp = 0.0;
        bool aht_ok = AHT2415_Service::get(aht_temp, aht_humi);
        bool ntc_ok = NTC_Service::get_temperature(ntc_temp);

        if ( aht_ok ){
            if ( (aht_humi < 0.0) || (aht_humi > 100.0)
                 || (aht_temp < -40.0) || (aht_temp > 85.0) ){
                aht_ok = false;
            }
        }

        if ( ntc_ok && aht_ok ){
            env_status.value.temperature = apply_temp_compensation(ntc_temp);
            env_status.value.humidity = aht_humi;
            env_status.valid_status.temperature = true;
            env_status.valid_status.humidity = true;
        }else if ( aht_ok ){
            env_status.value.temperature = apply_temp_compensation(aht_temp);
            env_status.value.humidity = aht_humi;
            env_status.valid_status.temperature = true;
            env_status.valid_status.humidity = true;
        }else if ( ntc_ok ){
            env_status.value.temperature = apply_temp_compensation(ntc_temp);
            env_status.valid_status.temperature = true;
            env_status.valid_status.humidity = false;
        }else{
            env_status.valid_status.temperature = false;
            env_status.valid_status.humidity = false;
        }

        env_status.value.rain_status = RainSensorService::get_rain_status();
        if ( battery_loop_cnt >= BATTERY_SAMPLE_INTERVAL_LOOPS ){
            env_status.value.battery_percent = sample_battery_percent();
            battery_loop_cnt = 0;
        }else{
            battery_loop_cnt++;
        }

        /* 温度报警蜂鸣关闭；改为扫频测试. */
        temp_alarm_active = false;
        if ( (int32_t)(xTaskGetTickCount() - tone_deadline) >= 0 ){
            sweep_idx++;
            if ( sweep_idx >= sweep_count ){
                sweep_idx = 0;
            }
            buzzer_apply_freq(BUZZER_SWEEP_HZ[sweep_idx]);
            buzzer_set(true);
            tone_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BUZZER_SWEEP_MS);
            LOG_INFO("Buzzer %u Hz for %u ms.",
                     (unsigned int)BUZZER_SWEEP_HZ[sweep_idx], BUZZER_SWEEP_MS);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool Env::start(){
    if ( xTaskCreate(Env_eventloop,"env",256,nullptr,10,&task_handle_env) != pdPASS ){
        LOG_ERROR("Can't create task for env service.");
        return false;
    }
    return true;
}

bool Env::get_temperature(double &temperature){
    if (env_status.valid_status.temperature != true ){
        return false;
    }
    temperature = env_status.value.temperature;
    return true;
}

bool Env::get_humidity(double &humidity){
    if ( env_status.valid_status.humidity != true ){
        return false;
    }
    humidity = env_status.value.humidity;
    return true;
}

bool Env::get_rain_status(bool &rain_status){
    rain_status = env_status.value.rain_status;
    return true;
}

bool Env::get_battery_percent(unsigned int &percent){
    percent = env_status.value.battery_percent;
    if ( percent > 100 ){
        percent = 100;
    }
    return true;
}

bool Env::get_error_code(unsigned int &error_code){
    if ( env_status.valid_status.temperature != true ){
        error_code = 2;
        return true;
    }
    if ( temp_alarm_active ){
        error_code = 3;
        return true;
    }
    error_code = 0;
    return true;
}

bool Env::is_temp_alarm(void){
    return temp_alarm_active;
}
