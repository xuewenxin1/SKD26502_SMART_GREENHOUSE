#include "iot_service.hpp"
#include "console_logger.h"
#include "py32f071_hal_uart.h"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_cortex.h"
#include "py32f071_hal_gpio.h"
#include <cstring>
#include "systimer.hpp"
#include "at.hpp"
#include "urc_handler.hpp"
#include "ChecksumCalculator.hpp"
#include "base64.hpp"
#include "env.hpp"
#include "ventilate_service.hpp"
#include "config.hpp"
#include "iot_process.hpp"

#define PORT_IOT_TXD GPIOA
#define PIN_IOT_TXD GPIO_PIN_2
#define PORT_IOT_RXD GPIOA
#define PIN_IOT_RXD GPIO_PIN_3
#define UART_IOT USART2

char at_buffer[512];
char mp_urc_buffer[1024];

char rx_buffer[512] = {0};
unsigned int rx_length = 0;

static const char *ATCMD_TEST = "AT\r\n";
static const char *ATCMD_MCCID = "AT+MCCID\r\n";
static const char *ATCMD_CGSN = "AT+GSN=1\r\n";
static const char *ATCMD_CSQ = "AT+CSQ\r\n";
static const char *ATCMD_MQTT_CONNECT_FMT = "AT+MQTTCONN=0,\"8.130.47.7\",1883,\"%s\"\r\n";
static const char *ATCMD_MQTT_DISCONN = "AT+MQTTDISC=0\r\n";
static const char *ATCMD_MQTT_PUBLISH = "AT+MQTTPUB=0,\"%s\",0,0,0,%d,\"%s\"\r\n";
static const char *ATCMD_MQTT_SUB = "AT+MQTTSUB=0,\"%s\",0\r\n";

static UART_HandleTypeDef huart_iot;

static void at_test(){
    HAL_UART_Transmit(&huart_iot,(uint8_t*)ATCMD_TEST,strlen(ATCMD_TEST),0xffff);
}

static void mqtt_disconn(){
    HAL_UART_Transmit(&huart_iot,(uint8_t*)ATCMD_MQTT_DISCONN,strlen(ATCMD_MQTT_DISCONN),0xffff);
}

static bool IOTService_board_init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio_init_config.Pin = PIN_IOT_TXD;
    gpio_init_config.Mode = GPIO_MODE_AF_PP;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Alternate = GPIO_AF1_USART2;
    HAL_GPIO_Init(PORT_IOT_TXD, &gpio_init_config);

    gpio_init_config.Pin = PIN_IOT_RXD;
    gpio_init_config.Mode = GPIO_MODE_AF_PP;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init_config.Pull = GPIO_NOPULL;
    gpio_init_config.Alternate = GPIO_AF1_USART2;
    HAL_GPIO_Init(PORT_IOT_RXD, &gpio_init_config);

    huart_iot.Instance = UART_IOT;
    huart_iot.Init.BaudRate = 115200;
    huart_iot.Init.WordLength = UART_WORDLENGTH_8B;
    huart_iot.Init.Parity = UART_PARITY_NONE;
    huart_iot.Init.StopBits = UART_STOPBITS_1;
    huart_iot.Init.Mode = UART_MODE_TX_RX;
    huart_iot.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_iot.Init.OverSampling = UART_OVERSAMPLING_16;
    if ( HAL_UART_Init(&huart_iot) != HAL_OK ){
        return false;
    }

    __HAL_UART_ENABLE_IT(&huart_iot,UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART2_IRQn,1,0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    return true;
}

static AT *at = nullptr;
static MccidUrcHandler *mccid_urc_handler;
static MqttPublishUrcHandler *mqtt_publish_urc_handler;
static CgsnUrcHandler *cgsn_urc_handler;
static bool g_mccid_flag = false;
static bool g_imei_flag = false;
static char g_mccid[21] = {0};
static char g_imei[16] = {0};

static void message(const char *topic, const char *payload, unsigned int length){
    LOG_INFO("MQTT topic:%s msg(%d):%s",topic,length,payload);
    if ( g_imei_flag ){
        const char *t = topic;
        if ( *t == '"' ){
            t++;
        }
        char expect[40] = {0};
        snprintf(expect, sizeof(expect), "greenhouse/%s/", g_imei);
        if ( strncmp(t, expect, strlen(expect)) != 0 ){
            LOG_WARN("Ignore foreign topic.");
            return;
        }
    }
    char payload_buffer[32] = {0};
    unsigned char buffer[32] = {0};

    ChecksumCalculator checksum_calculator;
    if ( length < 32 ){
        strcpy(payload_buffer,payload);
        for ( int n = 0; n < 32; n++ ){
            if ( payload_buffer[n] == '\r' || payload_buffer[n] == '\n' ){
                payload_buffer[n] = 0;
                length = strlen(payload_buffer);
                break;
            }
        }
        int len = base64_decode(buffer,(uint8_t*)payload_buffer,length);
        if ( len > 0 ){
            checksum_calculator.start();
            checksum_calculator.feed(buffer,len - 1);
            unsigned char f_checksum = checksum_calculator.get();
            if ( f_checksum == buffer[len - 1] ){
                /* 校验通过. */
                LOG_INFO("Decoded payload len:%d",len);
                process_iot_down(topic,buffer,len);
            }
        }
    }
}

static void mccid(const char *mccid, unsigned int length){
    LOG_INFO("MCCID(%d):%s",length,mccid);
    if ( g_mccid_flag == false ){
        strncpy(g_mccid,mccid,20);
        g_mccid_flag = true;
    }
}

static void cgsn(const char *imei, unsigned int length){
    LOG_INFO("IMEI(%d):%s",length,imei);
    if ( g_imei_flag == false ){
        strncpy(g_imei,imei,15);
        g_imei_flag = true;
    }
}

static void mqtt_publish(char *topic,char *message,int length){
    static char publish_at_buffer[256] = {0};
    if ( length > 200 ){
        return;
    }
    unsigned int len = snprintf(publish_at_buffer,255,ATCMD_MQTT_PUBLISH,topic,length,message);
    LOG_DEBUG("S:%s",publish_at_buffer);
    if ( at != nullptr ){
        at->send(publish_at_buffer,len);
    }
}

static void mqtt_publish_base64(char *topic,const unsigned char *data, int length){
    char base64_buffer[256] = {0};
    int len = base64_encode(base64_buffer,data,length);
    mqtt_publish(topic,base64_buffer,len);
}

static void mqtt_subscribe(char *topic) {
    static char subscribe_at_buffer[256] = {0};
    unsigned int len = snprintf(subscribe_at_buffer,255,ATCMD_MQTT_SUB,topic);
    if ( at != nullptr ){
        at->send(subscribe_at_buffer,len);
    }
}

static void send_callback(const char *buffer, unsigned int length){
    HAL_UART_Transmit(&huart_iot,(uint8_t *)buffer,length,0xffff);
}

bool IOTService::init(){
    if ( !IOTService_board_init() ){
        LOG_ERROR("Board init failed.");
        return false;
    }
    at = new AT((uint8_t*)at_buffer,512);
    at->set_send_callback(send_callback);
    mqtt_publish_urc_handler = new MqttPublishUrcHandler((uint8_t*)mp_urc_buffer,1024);
    mccid_urc_handler = new MccidUrcHandler();
    cgsn_urc_handler = new CgsnUrcHandler();
    at->add_urc_handler(mqtt_publish_urc_handler);
    at->add_urc_handler(mccid_urc_handler);
    at->add_urc_handler(cgsn_urc_handler);
    mqtt_publish_urc_handler->set_mqtt_message_callback(message);
    mccid_urc_handler->set_mccid_callback(mccid);
    cgsn_urc_handler->set_cgsn_callback(cgsn);
    LOG_INFO("IOT init complete.");
    g_mccid_flag = false;
    g_imei_flag = false;
    return true;
}

static void IOTService_serv_init(){
    int cnt = 0;
    if ( at == nullptr ) return;

    for ( int n = 0; n < 10; n++ ){
        at->send(ATCMD_TEST,strlen(ATCMD_TEST));
        SysTimer::delay(200);
    }

    /* 获取IMEI. */
    at->send(ATCMD_CGSN,strlen(ATCMD_CGSN));
    cnt = 0;
    while ( true ){
        SysTimer::delay(100);
        AT::CommandReturn ret = at->get_command_return();
        if ( ret == AT::CommandReturn::Ok ){
            LOG_INFO("Get IMEI OK.");
            break;
        }
        if ( cnt < 50 ){
            cnt ++;
        }else{
            LOG_ERROR("Get IMEI failed:%d",(int)ret);
            break;
        }
    }

    at->send(ATCMD_MCCID,strlen(ATCMD_MCCID));
    /* 获取MCCID. */
    cnt = 0;
    while ( true ){
        SysTimer::delay(100);
        AT::CommandReturn ret = at->get_command_return();
        if ( ret == AT::CommandReturn::Ok ){
            LOG_INFO("Get MCCID OK.");
            break;
        }
        if ( cnt < 50 ){
            cnt ++;
        }else{
            LOG_ERROR("Get MCCID failed:%d",(int)ret);
            break;
        }
    }

    if ( (g_imei_flag == false) || (g_mccid_flag == false) ){
        LOG_ERROR("MQTT service init failed.");
        return;
    }

    at->send(ATCMD_CSQ, strlen(ATCMD_CSQ));
    SysTimer::delay(500);

    at->send(ATCMD_MQTT_DISCONN, strlen(ATCMD_MQTT_DISCONN));
    SysTimer::delay(1000);

    char mqtt_conn_at[80] = {0};
    unsigned int conn_len = (unsigned int)snprintf(mqtt_conn_at, sizeof(mqtt_conn_at),
                                                   ATCMD_MQTT_CONNECT_FMT, g_imei);
    LOG_INFO("MQTT connect client_id=%s", g_imei);
    at->send(mqtt_conn_at, conn_len);
    cnt = 0;
    while ( true ){
        SysTimer::delay(100);
        if ( at->get_command_return() == AT::CommandReturn::Ok ){
            LOG_INFO("MQTT connect OK.");
            break;
        }
        if ( cnt < 50 ){
            cnt ++;
        }else{
            LOG_ERROR("MQTT connect failed.");
            break;
        }
    }

    SysTimer::delay(3000);
    if ( g_imei_flag && g_mccid_flag ){
        LOG_INFO("Subscribe topics.");
        char str_buffer[64] = {0};
        for ( int n = 0; n < 1; n++ ){
            snprintf(str_buffer,63,"greenhouse/%s/down/act",g_imei);
            mqtt_subscribe(str_buffer);
            if ( at->get_command_return() == AT::CommandReturn::Ok ){
                LOG_INFO("Subscribe act OK.");
                break;
            }
        }
        for ( int n = 0; n < 1; n++ ){
            SysTimer::delay(100);
            snprintf(str_buffer,63,"greenhouse/%s/down/config",g_imei);
            mqtt_subscribe(str_buffer);
            if ( at->get_command_return() == AT::CommandReturn::Ok ){
                LOG_INFO("Subscribe config OK.");
                break;
            }
        }
        for ( int n = 0; n < 1; n++ ){
            SysTimer::delay(100);
            snprintf(str_buffer,63,"greenhouse/%s/down/extra",g_imei);
            mqtt_subscribe(str_buffer);
            if ( at->get_command_return() == AT::CommandReturn::Ok ){
                LOG_INFO("Subscribe extra OK.");
                break;
            }
        }
    }
    LOG_INFO("MQTT service init OK.");
}

/* 主题:greenhouse/vid/up/dev */
static void IOTService_up_devinfo(){
    char topic[64] = {0};
    char payload[64] = {0};
    ChecksumCalculator checksum_calculator;

    memcpy(payload,g_imei,15);
    memcpy(payload + 15,g_mccid,20);
    checksum_calculator.start();
    checksum_calculator.feed(payload,35);
    payload[35] = checksum_calculator.get();

    snprintf(topic,63,"greenhouse/%s/up/dev",g_imei);

    mqtt_publish_base64(topic,(unsigned char*)payload,36);

}

void IOTService_up_status(){
    char topic[64] = {0};
    char buffer[32] = {0};

    int opening_percentage = 0;
    double f_temp = 0.0;
    double f_humi = 0.0;
    
    Env::get_temperature(f_temp);
    Env::get_humidity(f_humi);
    VentilateService::get_opening_percentage(opening_percentage);

    int16_t i16_temp = static_cast<int16_t>(f_temp * 10.0);
    int16_t i16_humi = static_cast<int16_t>(f_humi * 10.0);
    opening_percentage = opening_percentage * 10;
    ChecksumCalculator checksum_calculator;

    snprintf(topic,63,"greenhouse/%s/up/status",g_imei);

    buffer[0] = (i16_temp & 0xff);
    buffer[1] = (i16_temp >> 8) & 0xff;
    buffer[2] = (i16_humi & 0xff);
    buffer[3] = (i16_humi >> 8) & 0xff;
    buffer[4] = opening_percentage & 0xff;
    buffer[5] = (opening_percentage >> 8) & 0xff;
    checksum_calculator.start();
    checksum_calculator.feed(buffer,6);
    buffer[6] = checksum_calculator.get();

    mqtt_publish_base64(topic,(unsigned char*)buffer,7);
}

void IOTService_up_config(){
    char topic[64] = {0};
    char payload[32] = {0};
    ChecksumCalculator checksum_calculator;

    Config_t config;
    ConfigService::get_config(config);
    payload[0] = (uint8_t)config.working_mode;
    payload[1] = (uint8_t)(config.temp_alert_upper_limit & 0xff);
    payload[2] = (uint8_t)((config.temp_alert_upper_limit >> 8) & 0xff);
    payload[3] = (uint8_t)(config.temp_alert_lower_limit & 0xff);
    payload[4] = (uint8_t)((config.temp_alert_lower_limit >> 8) & 0xff);
    payload[5] = (uint8_t)(config.target_central_temp & 0xff);
    payload[6] = (uint8_t)((config.target_central_temp >> 8) & 0xff);
    payload[7] = (uint8_t)(config.temp_return_diff_positive & 0xff);
    payload[8] = (uint8_t)((config.temp_return_diff_positive >> 8) & 0xff);
    payload[9] = (uint8_t)(config.temp_return_diff_negative & 0xff);
    payload[10] = (uint8_t)((config.temp_return_diff_negative >> 8) & 0xff);
    payload[11] = (uint8_t)(config.temp_compensation_value & 0xff);
    payload[12] = (uint8_t)((config.temp_compensation_value >> 8) & 0xff);
    checksum_calculator.start();
    checksum_calculator.feed(payload,13);
    payload[13] = checksum_calculator.get();


    snprintf(topic,63,"greenhouse/%s/up/config",g_imei);

    mqtt_publish_base64(topic,(unsigned char*)payload,14);
}

static void message_loop(){
    int cnt = 0;
    if ( (cnt % 10) == 0 ){
        LOG_INFO("Message loop.");
        IOTService_up_devinfo();
        SysTimer::delay(100);
        IOTService_up_status();
        SysTimer::delay(100);
        IOTService_up_config();
        SysTimer::delay(100);
    }
    cnt = (cnt + 1) % 10000;
}

void IOTService::eventloop(){
    static int cnt = 0;
    // if ( rx_length >= 0 ){
    //     for ( int n = 0; n < rx_length; n++) {
    //         if ( at != nullptr ){
    //             at->feed_byte(rx_buffer[n]);
    //         }
    //     }
    //     rx_length = 0;
    // }
    if ( cnt < 5 ){
        cnt += 1;
    }else if ( cnt == 5 ){
        IOTService_serv_init();
        cnt += 1;
    }else {
        /* 主要事件循环. */
        if ( g_imei_flag && g_mccid_flag ){
            message_loop();
        }
        
    }
}

bool IOTService::get_network_status(){
    if ( g_imei_flag && g_mccid_flag ){
        return true;
    }else{
        return false;
    }
}

extern "C"
void USART2_IRQHandler(void){
    uint8_t temp = 0;
    temp = huart_iot.Instance->DR;
    // if ( at != nullptr ){
    //     at->feed_byte(temp);
    // }
    rx_buffer[rx_length++] = temp;
    if ( rx_length >= 512 || temp == '\n' ){
        if ( at != nullptr ){
            at->process_line((uint8_t*)rx_buffer,rx_length);
        }
        rx_length = 0;
    }
    // rx_buffer[rx_length] = temp;
    // if ( rx_length < 63 ){
    //     rx_length += 1;
    // }else{
    //     rx_length = 0;
    // }
    __HAL_UART_CLEAR_NEFLAG(&huart_iot);
}