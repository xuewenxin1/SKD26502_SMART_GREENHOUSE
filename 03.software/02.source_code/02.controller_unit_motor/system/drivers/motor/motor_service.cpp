#include "motor_service.hpp"
#include "py32f071_hal_gpio.h"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_tim.h"
#include "py32f071_hal_cortex.h"
#include "console_service.hpp"
#include "adc.hpp"

#define PORT_FORWARD GPIOA
#define PIN_FORWARD GPIO_PIN_6
#define PORT_REVERSE GPIOA
#define PIN_REVERSE GPIO_PIN_7

static TIM_HandleTypeDef htim6;

static const Motor::Parameter motor_param = {
    .cs_k = 0.00109,
    .cs_b = 0,
    .zero_threshold = 0.2,
    .ovc_threshold = 4,
};

static Motor *motor = nullptr;

class MotorPortableStrategy : public Motor::PortableStrategy {
public:
    virtual bool init(){
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_ADC_CLK_ENABLE();
        __HAL_RCC_TIM6_CLK_ENABLE();

        GPIO_InitTypeDef gpio_init_config = {0};
        gpio_init_config.Pin = PIN_FORWARD;
        gpio_init_config.Mode = GPIO_MODE_OUTPUT_PP;
        gpio_init_config.Speed = GPIO_SPEED_FREQ_LOW;
        gpio_init_config.Pull = GPIO_NOPULL;
        gpio_init_config.Alternate = 0;
        HAL_GPIO_WritePin(PORT_FORWARD,PIN_FORWARD,GPIO_PIN_RESET);
        HAL_GPIO_Init(PORT_FORWARD, &gpio_init_config);
        gpio_init_config.Pin = PIN_REVERSE;
        HAL_GPIO_WritePin(PORT_REVERSE,PIN_REVERSE,GPIO_PIN_RESET);
        HAL_GPIO_Init(PORT_REVERSE, &gpio_init_config);
        
        htim6.Instance = TIM6;
        htim6.Init.Period = 1000;
        htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        htim6.Init.Prescaler = 8;
        htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
        htim6.Init.RepetitionCounter = 0;
        htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
        if ( HAL_TIM_Base_Init(&htim6) != HAL_OK ){
            return false;
        }
        __HAL_TIM_ENABLE_IT(&htim6,TIM_IT_UPDATE);
        HAL_NVIC_EnableIRQ(TIM6_LPTIM1_DAC_IRQn);
        HAL_NVIC_SetPriority(TIM6_LPTIM1_DAC_IRQn,1,1);
        __HAL_TIM_ENABLE(&htim6);
        return true;

        
        return true;
    }

    virtual bool deinit(){
        return true;
    }

    virtual bool set_output(Pin pin, bool level){
        GPIO_PinState state = level ? GPIO_PIN_SET : GPIO_PIN_RESET;
        switch (pin) {
            case Pin::FORWARD: {
                HAL_GPIO_WritePin(PORT_FORWARD,PIN_FORWARD,state);
                break;
            }
            case Pin::REVERSE: {
                HAL_GPIO_WritePin(PORT_REVERSE,PIN_REVERSE,state);
                break;
            }
            default: {
                return false;
            }
        }
        return true;
    }

    virtual bool get_adc_value(uint16_t &value){
        ADC::capture();
        ADC::get(ADC::Channel::MOTOR_CURR,value);
        return true;
    }

};

Motor *MotorService::get_motor(){
    static MotorPortableStrategy motor_portable_strategy;
    if ( motor == nullptr ){
        motor = new Motor(motor_portable_strategy,motor_param);
    }
    return motor;
}

extern "C"
void TIM6_LPTIM1_DAC_IRQHandler(void){
    if ( __HAL_TIM_GET_FLAG(&htim6,TIM_FLAG_UPDATE) != RESET ){
        if ( motor != nullptr ){
            motor->timer_trigger();
        }
    }
    __HAL_TIM_CLEAR_IT(&htim6,TIM_IT_UPDATE);
}