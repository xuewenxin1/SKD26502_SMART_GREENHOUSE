#include "FreeRTOS.h"
#include "task.h"
#include <new>  // for std::nothrow_t
#include "console_logger.h"
#include <cstdio>
#include "py32f071_hal_uart.h"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_gpio.h"

/************ 内存分配器 ************/

// Single object new
void* operator new(size_t size) {
    return pvPortMalloc(size);
}

void* operator new(size_t size, std::nothrow_t) noexcept {
    return pvPortMalloc(size);
}

// Array new
void* operator new[](size_t size) {
    return pvPortMalloc(size);
}

void* operator new[](size_t size, std::nothrow_t) noexcept {
    return pvPortMalloc(size);
}

// Single object delete
void operator delete(void* ptr) noexcept {
    vPortFree(ptr);
}

void operator delete(void* ptr, std::nothrow_t) noexcept {
    vPortFree(ptr);
}

// Sized delete (C++14 and later)
void operator delete(void* ptr, size_t size) noexcept {
    vPortFree(ptr);
}

// Array delete
void operator delete[](void* ptr) noexcept {
    vPortFree(ptr);
}

void operator delete[](void* ptr, std::nothrow_t) noexcept {
    vPortFree(ptr);
}

// Sized array delete
void operator delete[](void* ptr, size_t size) noexcept {
    vPortFree(ptr);
}

/************ 系统打印初始化 ************/

#define PORT_CONSOLE_TXD GPIOB
#define PIN_CONSOLE_TXD GPIO_PIN_10
#define PORT_CONSOLE_RXD GPIOB
#define PIN_CONSOLE_RXD GPIO_PIN_11
#define UART_CONSOLE USART3

static UART_HandleTypeDef huart_console;

bool system_log_board_init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init_config.Pin = PIN_CONSOLE_TXD;
    gpio_init_config.Mode = GPIO_MODE_AF_PP;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Alternate = GPIO_AF4_USART3;
    HAL_GPIO_Init(PORT_CONSOLE_TXD, &gpio_init_config);

    huart_console.Instance = UART_CONSOLE;
    huart_console.Init.BaudRate = 115200;
    huart_console.Init.WordLength = UART_WORDLENGTH_8B;
    huart_console.Init.Parity = UART_PARITY_NONE;
    huart_console.Init.StopBits = UART_STOPBITS_1;
    huart_console.Init.Mode = UART_MODE_TX;
    huart_console.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_console.Init.OverSampling = UART_OVERSAMPLING_16;
    if ( HAL_UART_Init(&huart_console) != HAL_OK ){
        return false;
    }
    
    return true;
}

extern "C"
__attribute__((used)) int _write(int fd,char *buff,int size){
    HAL_UART_Transmit(&huart_console,(uint8_t*)buff,size,0xffff);
    return size;
}

/************ 系统入口 ************/

extern int main();

static TaskHandle_t system_main_task_handler = nullptr;

void system_main_task(void *param){
    int ret = main();
    LOG_INFO("Main task return with code %d", ret);
    vTaskDelete(NULL);
    return;
}

extern "C"
void system_entry(){
    system_log_board_init();

    if ( console_logger_init() != LOG_ERR_NONE ){
        printf("Console logger init failed.\r\n");
    }else{
        LOG_INFO("Console logger init complete.");
    }
    if ( xTaskCreate(system_main_task,"Main",512,nullptr,10,&system_main_task_handler) == pdPASS ){
        vTaskStartScheduler();  /* 启动调度器。 */
    }

    /* 主线程创建失败. */
    LOG_ERROR("Main task create failed. System dump.");
    while ( true ){

    }
}

