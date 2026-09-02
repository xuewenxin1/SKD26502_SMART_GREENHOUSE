#include "systimer.hpp"
#include "py32f071_hal_tim.h"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_cortex.h"
#include "console_logger.h"
#include "gui.hpp"

#define MAX_TASK_COUNT 16
#define MAX_TIMER_COUNT 2

static TIM_HandleTypeDef htim;

typedef struct TimerInfo {
    bool enable;
    unsigned int cnt;
} TimerInfo_t;

TimerInfo_t timers[MAX_TIMER_COUNT] = {
    {
        .enable = false,
        .cnt = 0,
    }
};

bool SysTimer::init(){
    __HAL_RCC_TIM16_CLK_ENABLE();

    htim.Instance = TIM16;
    htim.Init.Period = 1000;
    htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim.Init.Prescaler = 32;
    htim.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim.Init.RepetitionCounter = 0;
    htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if ( HAL_TIM_Base_Init(&htim) != HAL_OK ){
        return false;
    }

    HAL_NVIC_SetPriority(TIM16_IRQn,0,1);
    HAL_NVIC_EnableIRQ(TIM16_IRQn);
    __HAL_TIM_ENABLE_IT(&htim,TIM_IT_UPDATE);
    __HAL_TIM_ENABLE(&htim);
    return true;
}

bool __SysTimer_check_timer_num(int timer_num){
    if ( timer_num < 0 || timer_num >= MAX_TIMER_COUNT ){
        return false;
    }else{
        return true;
    }
}

bool SysTimer::start_timer(int timer_num){
    if ( __SysTimer_check_timer_num(timer_num) != true ){
        return false;
    }
    timers[timer_num].enable = true;
    return true;
}

bool SysTimer::stop_timer(int timer_num){
    if ( __SysTimer_check_timer_num(timer_num) != true ){
        return false;
    }
    timers[timer_num].enable = false;
    return true;
}

bool SysTimer::reset_timer(int timer_num){
    if ( __SysTimer_check_timer_num(timer_num) != true ){
        return false;
    }
    timers[timer_num].cnt = 0;
    return true;
}

bool SysTimer::get_timer_cnt(int timer_num, unsigned int &cnt){
    if ( __SysTimer_check_timer_num(timer_num) != true ){
        return false;
    }
    cnt = timers[timer_num].cnt;
    return true;
}

extern "C" {
void TIM16_IRQHandler(void){
    static volatile unsigned long count = 0;
    if ( count < 1000 * 10 * 1000 ){
        count++;
    }else{
        count = 0;
    }
    for ( int n_timer = 0; n_timer < MAX_TIMER_COUNT; n_timer++ ){
        if ( timers[n_timer].enable == true ){
            timers[n_timer].cnt++;
        }
    }
    __HAL_TIM_CLEAR_IT(&htim,TIM_IT_UPDATE);
}
}