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
/* PA12 无硬件 PWM 通道，用 TIM14 翻转做 4kHz 方波. */
#define PORT_BUZZER GPIOA
#define PIN_BUZZER GPIO_PIN_12
#define BUZZER_FREQ_HZ 4000u
#define BUZZER_TOGGLE_HZ (BUZZER_FREQ_HZ * 2u)
#define BATTERY_SAMPLE_INTERVAL_LOOPS 2
/* 电量 ADC 仍按原 3.6V 基准换算；NTC 见 ntc_service（供电 3.86V，比值法）. */
#define ADC_VREF_MV 3600u
/* ADC_IN4 脚电压：1.8V=空，2.1V=满. */
#define ADC_EMPTY_MV 1800u
#define ADC_FULL_MV 2100u

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
static bool buzzer_on = false;
static TIM_HandleTypeDef htim_buzzer;
static volatile bool buzzer_pwm_running = false;

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
    uint32_t arr = 0;

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
    arr = SystemCoreClock / BUZZER_TOGGLE_HZ;
    if ( arr < 2u ){
        arr = 2u;
    }
    arr = arr - 1u;

    htim_buzzer.Instance = TIM14;
    htim_buzzer.Init.Prescaler = 0;
    htim_buzzer.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_buzzer.Init.Period = arr;
    htim_buzzer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_buzzer.Init.RepetitionCounter = 0;
    htim_buzzer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if ( HAL_TIM_Base_Init(&htim_buzzer) != HAL_OK ){
        LOG_ERROR("Buzzer TIM14 init failed.");
        return;
    }
    HAL_NVIC_SetPriority(TIM14_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM14_IRQn);
    LOG_INFO("Buzzer PA12 PWM %u Hz, clk %u.", (unsigned int)BUZZER_FREQ_HZ, (unsigned int)SystemCoreClock);
}

static void update_temp_alarm(void){
    Config_t config;
    ConfigService::get_config(config);
    bool out_of_range = false;
    int meas = 0;
    /* 无有效温度（显示 ---）：也报警；有温度则按 A1/A2 判断. */
    if ( env_status.valid_status.temperature != true ){
        out_of_range = true;
    }else{
        meas = (int)env_status.value.temperature;
        int hi = config.temp_alert_upper_limit / 10;
        int lo = config.temp_alert_lower_limit / 10;
        if ( (meas > hi) || (meas < lo) ){
            out_of_range = true;
        }
    }
    temp_alarm_active = out_of_range;
    bool want_beep = out_of_range && (config.local_alarm_switch != 0);
    if ( want_beep != buzzer_on ){
        buzzer_on = want_beep;
        buzzer_set(want_beep);
        if ( want_beep ){
            if ( env_status.valid_status.temperature != true ){
                LOG_INFO("Temp alarm buzzer on (no sensor).");
            }else{
                LOG_INFO("Temp alarm buzzer on, %d C.", meas);
            }
        }else{
            LOG_INFO("Temp alarm buzzer off.");
        }
    }
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
    /* BAT_C(PB15) 上电后一直拉高. */
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
    /* ADC_IN4 1.8V=0%，2.1V=100%。12 位 ADC，基准 3.6V. */
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
    LOG_INFO("Init env service.");
    /* 初始化ADC服务. */
    if ( ADC::init() != true ){
        LOG_ERROR("ADC init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("ADC init success.");
    }
    battery_gpio_init();
    /* 初始化AHT2415. */
    if ( AHT2415_Service::init() != true ){
        LOG_ERROR("AHT2415 init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("AHT2415 init success.");
    }
    /* 初始化NTC. */
    if ( NTC_Service::init() != true ){
        LOG_ERROR("NTC init failed. env init abort.");
        return false;
    }else{
        LOG_INFO("NTC init success.");
    }
    /* 初始化雨雪传感器. */
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
    LOG_INFO("env service running.");
    while ( true ){
        /* 双插：NTC 温度 + 温湿度计湿度；只插温湿度计用其温湿度；只插 NTC 仅温度. */
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
        {
            static unsigned int temp_src_log_cnt = 0;
            if ( (temp_src_log_cnt++ % 10u) == 0u ){
                Config_t cfg;
                ConfigService::get_config(cfg);
                const char *src = "none";
                if ( ntc_ok && aht_ok ){
                    src = "NTC";
                }else if ( aht_ok ){
                    src = "AHT";
                }else if ( ntc_ok ){
                    src = "NTC_only";
                }
                LOG_INFO("Temp src %s, ntc %d, aht %d, P4 %d, show %d C.",
                         src,
                         (int)ntc_temp,
                         (int)aht_temp,
                         cfg.temp_compensation_value,
                         (int)env_status.value.temperature);
            }
        }
        /* 更新雨滴状态. */
        env_status.value.rain_status = RainSensorService::get_rain_status();
        if ( battery_loop_cnt >= BATTERY_SAMPLE_INTERVAL_LOOPS ){
            env_status.value.battery_percent = sample_battery_percent();
            battery_loop_cnt = 0;
        }else{
            battery_loop_cnt++;
        }
        update_temp_alarm();
        vTaskDelay(pdMS_TO_TICKS(500));
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
    /* MQTT status 故障码：2=传感器故障，3=温度超限（相对 A1/A2）. */
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