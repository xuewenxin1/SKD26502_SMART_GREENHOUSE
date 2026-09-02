#include "py32f071_hal_uart.h"
#include "py32f071_hal_gpio.h"
#include "py32f071_hal_rcc.h"
#include <stdio.h>

/* 与 motor 放风机板一致：USART3 TX = PB10，115200. */
#define PORT_CONSOLE_TX GPIOB
#define PIN_CONSOLE_TX GPIO_PIN_10
#define UART_CONSOLE USART3

static UART_HandleTypeDef huart_console;

void USART_Board_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = PIN_CONSOLE_TX;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_USART3;
    HAL_GPIO_Init(PORT_CONSOLE_TX, &gpio);

    huart_console.Instance = UART_CONSOLE;
    huart_console.Init.BaudRate = 115200;
    huart_console.Init.WordLength = UART_WORDLENGTH_8B;
    huart_console.Init.StopBits = UART_STOPBITS_1;
    huart_console.Init.Parity = UART_PARITY_NONE;
    huart_console.Init.Mode = UART_MODE_TX;
    huart_console.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_console.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart_console);
}

/* 与 motor system.cpp 一致：printf 走 _write -> USART3 PB10. */
__attribute__((used)) int _write(int fd, char *buff, int size)
{
    (void)fd;
    HAL_UART_Transmit(&huart_console, (uint8_t *)buff, (uint16_t)size, 0xffff);
    return size;
}

int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart_console, &c, 1, 1000);
    return ch;
}
