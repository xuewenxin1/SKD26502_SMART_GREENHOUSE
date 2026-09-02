#include "console_service.hpp"
#include "py32f071_hal_uart.h"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_gpio.h"

#define PORT_CONSOLE_TXD GPIOB
#define PIN_CONSOLE_TXD GPIO_PIN_10
#define PORT_CONSOLE_RXD GPIOB
#define PIN_CONSOLE_RXD GPIO_PIN_11
#define UART_CONSOLE USART3

static UART_HandleTypeDef huart_console;

bool ConsoleService::init(){
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
