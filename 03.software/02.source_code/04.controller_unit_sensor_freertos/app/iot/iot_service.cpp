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
#include "FreeRTOS.h"
#include "task.h"

#define PORT_IOT_TXD GPIOA
#define PIN_IOT_TXD GPIO_PIN_2
#define PORT_IOT_RXD GPIOA
#define PIN_IOT_RXD GPIO_PIN_3
#define UART_IOT USART2

static const uint8_t DEVICE_TYPE = 0x02;    /* 传感器 */

static char at_buffer[256];
static char mp_urc_buffer[256];
/* Flash 擦写期间任务挂起，115200 约 1ms≈11 字节；512 可撑约 45ms，兼顾 RAM. */
#define RX_RING_SIZE 512
#define RX_RING_MASK (RX_RING_SIZE - 1)
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_w = 0;
static volatile uint16_t rx_r = 0;
static volatile uint8_t pending_up_status = 0;
static volatile uint8_t pending_up_config = 0;

static const char *ATCMD_TEST = "AT\r\n";
static const char *ATCMD_MCCID = "AT+MCCID\r\n";
static const char *ATCMD_CGSN = "AT+GSN=1\r\n";
static const char *ATCMD_CSQ = "AT+CSQ\r\n";
/* Client ID 必须每台唯一；原固定 SmartGH 会导致多机互踢、串 topic、607. */
static const char *ATCMD_MQTT_CONNECT_FMT = "AT+MQTTCONN=0,\"8.130.47.7\",1883,\"%s\"\r\n";
static const char *ATCMD_MQTT_DISCONN = "AT+MQTTDISC=0\r\n";
static const char *ATCMD_MQTT_PUBLISH = "AT+MQTTPUB=0,\"%s\",0,0,0,%d,\"%s\"\r\n";
static const char *ATCMD_MQTT_SUB = "AT+MQTTSUB=0,\"%s\",0\r\n";

static TaskHandle_t task_handle_iot = nullptr;
static UART_HandleTypeDef huart_iot;

static void IOTService_wait_ms(unsigned int ms);
static void IOTService_drain_rx(void);

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
    /* 新 config 包 23 字节 → Base64 正好 32 字符；缓冲需大于 32. */
    char payload_buffer[64] = {0};
    unsigned char buffer[64] = {0};

    ChecksumCalculator checksum_calculator;
    if ( length >= sizeof(payload_buffer) ){
        LOG_WARN("MQTT payload too long %u.", length);
        return;
    }
    memcpy(payload_buffer, payload, length);
    payload_buffer[length] = 0;
    for ( unsigned int n = 0; n < length; n++ ){
        if ( payload_buffer[n] == '\r' || payload_buffer[n] == '\n' ){
            payload_buffer[n] = 0;
            length = (unsigned int)strlen(payload_buffer);
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
            process_iot_down(topic,buffer,(unsigned int)len);
        }else{
            LOG_WARN("MQTT checksum fail, len %d.", len);
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
    static char base64_buffer[256] = {0};
    int len = base64_encode(base64_buffer,data,length);
    mqtt_publish(topic,base64_buffer,len);
}

static bool mqtt_subscribe_ok(char *topic) {
    static char subscribe_at_buffer[256] = {0};
    unsigned int len = snprintf(subscribe_at_buffer,255,ATCMD_MQTT_SUB,topic);
    if ( at == nullptr ){
        return false;
    }
    for ( int try_n = 0; try_n < 3; try_n++ ){
        at->send(subscribe_at_buffer,len);
        for ( int n = 0; n < 20; n++ ){
            IOTService_wait_ms(100);
            if ( at->get_command_return() == AT::CommandReturn::Ok ){
                LOG_INFO("MQTT SUB OK: %s", topic);
                return true;
            }
        }
        LOG_WARN("MQTT SUB retry %d: %s", try_n + 1, topic);
    }
    LOG_ERROR("MQTT SUB failed: %s", topic);
    return false;
}

static void send_callback(const char *buffer, unsigned int length){
    HAL_UART_Transmit(&huart_iot,(uint8_t *)buffer,length,0xffff);
}

bool IOTService::init(){
    if ( !IOTService_board_init() ){
        LOG_ERROR("Board init failed.");
        return false;
    }
    at = new AT((uint8_t*)at_buffer,256);
    at->set_send_callback(send_callback);
    mqtt_publish_urc_handler = new MqttPublishUrcHandler((uint8_t*)mp_urc_buffer,256);
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
    rx_w = 0;
    rx_r = 0;
    return true;
}

static void IOTService_drain_rx(void){
    if ( at == nullptr ){
        rx_r = rx_w;
        return;
    }
    while ( rx_r != rx_w ){
        uint16_t r = rx_r;
        uint8_t byte = rx_ring[r];
        rx_r = (uint16_t)((r + 1) & RX_RING_MASK);
        at->feed_byte(byte);
    }
}

static void IOTService_wait_ms(unsigned int ms){
    TickType_t start = xTaskGetTickCount();
    TickType_t wait = pdMS_TO_TICKS(ms);
    if ( wait < 1 ){
        wait = 1;
    }
    while ( (xTaskGetTickCount() - start) < wait ){
        IOTService_drain_rx();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    IOTService_drain_rx();
}

static bool IOTService_serv_init(){
    int cnt = 0;
    if ( at == nullptr ){
        return false;
    }

    /* Same AT sequence as original firmware. RX bytes are stored in ISR and parsed in this task. */
    LOG_INFO("Wait modem.");
    for ( int n = 0; n < 10; n++ ){
        at->send(ATCMD_TEST,strlen(ATCMD_TEST));
        IOTService_wait_ms(200);
    }

    at->send(ATCMD_CGSN,strlen(ATCMD_CGSN));
    cnt = 0;
    while ( true ){
        IOTService_wait_ms(100);
        AT::CommandReturn ret = at->get_command_return();
        if ( ret == AT::CommandReturn::Ok ){
            LOG_INFO("Get IMEI OK.");
            break;
        }
        if ( cnt < 50 ){
            cnt++;
        }else{
            LOG_ERROR("Get IMEI failed.");
            break;
        }
    }

    at->send(ATCMD_MCCID,strlen(ATCMD_MCCID));
    cnt = 0;
    while ( true ){
        IOTService_wait_ms(100);
        AT::CommandReturn ret = at->get_command_return();
        if ( ret == AT::CommandReturn::Ok ){
            LOG_INFO("Get MCCID OK.");
            break;
        }
        if ( cnt < 50 ){
            cnt++;
        }else{
            LOG_ERROR("Get MCCID failed.");
            break;
        }
    }

    if ( (g_imei_flag == false) || (g_mccid_flag == false) ){
        LOG_ERROR("MQTT service init failed.");
        return false;
    }

    at->send(ATCMD_CSQ, strlen(ATCMD_CSQ));
    IOTService_wait_ms(500);

    at->send(ATCMD_MQTT_DISCONN, strlen(ATCMD_MQTT_DISCONN));
    IOTService_wait_ms(1000);

    char mqtt_conn_at[80] = {0};
    unsigned int conn_len = (unsigned int)snprintf(mqtt_conn_at, sizeof(mqtt_conn_at),
                                                   ATCMD_MQTT_CONNECT_FMT, g_imei);
    LOG_INFO("MQTT connect client_id=%s", g_imei);
    at->send(mqtt_conn_at, conn_len);
    cnt = 0;
    bool mqtt_ok = false;
    while ( true ){
        IOTService_wait_ms(100);
        if ( at->get_command_return() == AT::CommandReturn::Ok ){
            LOG_INFO("MQTT connect OK.");
            mqtt_ok = true;
            break;
        }
        if ( cnt < 50 ){
            cnt++;
        }else{
            LOG_ERROR("MQTT connect failed.");
            break;
        }
    }
    if ( mqtt_ok == false ){
        return false;
    }
    IOTService_wait_ms(3000);

    LOG_INFO("Subscribe topics.");
    char str_buffer[64] = {0};
    bool sub_ok = true;
    snprintf(str_buffer,63,"greenhouse/%s/down/act",g_imei);
    if ( mqtt_subscribe_ok(str_buffer) == false ){
        sub_ok = false;
    }
    snprintf(str_buffer,63,"greenhouse/%s/down/config",g_imei);
    if ( mqtt_subscribe_ok(str_buffer) == false ){
        sub_ok = false;
    }
    snprintf(str_buffer,63,"greenhouse/%s/down/extra",g_imei);
    if ( mqtt_subscribe_ok(str_buffer) == false ){
        sub_ok = false;
    }
    if ( sub_ok == false ){
        LOG_ERROR("MQTT subscribe incomplete.");
        return false;
    }
    LOG_INFO("MQTT service init OK.");
    return true;
}

/* 主题:greenhouse/vid/up/dev */
static void IOTService_up_devinfo(){
    static char topic[64] = {0};
    static char payload[64] = {0};
    ChecksumCalculator checksum_calculator;

    payload[0] = DEVICE_TYPE;
    memcpy(payload + 1,g_imei,15);
    memcpy(payload + 16,g_mccid,20);
    checksum_calculator.start();
    checksum_calculator.feed(payload,36);
    payload[36] = checksum_calculator.get();

    snprintf(topic,63,"greenhouse/%s/up/dev",g_imei);

    mqtt_publish_base64(topic,(unsigned char*)payload,37);
    LOG_INFO("Report device info, type %d.", (int)DEVICE_TYPE);

}

void IOTService_up_status(){
    static char topic[64] = {0};
    static char buffer[32] = {0};

    int opening_percentage = 0;
    double f_temp = 0.0;
    double f_humi = 0.0;
    
    Env::get_temperature(f_temp);
    Env::get_humidity(f_humi);
    VentilateService::get_opening_percentage(opening_percentage);

    Config_t cfg;
    ConfigService::get_config(cfg);
    /* 当前整数圈数（未满 1 圈为 0）；例：走完 1 圈才显示 1. */
    int current_turns = 0;
    VentilateService::get_current_turns(current_turns);
    if ( current_turns < 0 ){
        current_turns = 0;
    }
    if ( current_turns > 65535 ){
        current_turns = 65535;
    }

    int16_t i16_temp = static_cast<int16_t>(f_temp * 10.0);
    int16_t i16_humi = static_cast<int16_t>(f_humi * 10.0);
    uint8_t fault_code = 0; /* 无故障 / 故障消失：固定报 0. */
    uint8_t motor_running_state = 0;
    Motor::State motor_state = Motor::State::IDLE;
    VentilateService::get_motor_state(motor_state);
    /* 故障码：0=正常，1=电机过流，2=传感器故障，3=温度超限；优先级 1>2>3. */
    if ( motor_state == Motor::State::ERROR_OVC ){
        fault_code = 1;
        motor_running_state = 0;
    }else{
        unsigned int env_fault = 0;
        Env::get_error_code(env_fault);
        if ( env_fault == 2u ){
            fault_code = 2;
        }else if ( env_fault == 3u ){
            fault_code = 3;
        }else{
            fault_code = 0;
        }
        if ( motor_state == Motor::State::RUNNING_FORWARD ){
            motor_running_state = 1;
        }else if ( motor_state == Motor::State::RUNNING_REVERSE ){
            motor_running_state = 2;
        }else{
            motor_running_state = 0;
        }
    }
    
    ChecksumCalculator checksum_calculator;

    snprintf(topic,63,"greenhouse/%s/up/status",g_imei);

    buffer[0] = (i16_temp & 0xff);
    buffer[1] = (i16_temp >> 8) & 0xff;
    buffer[2] = (i16_humi & 0xff);
    buffer[3] = (i16_humi >> 8) & 0xff;
    buffer[4] = current_turns & 0xff;
    buffer[5] = (current_turns >> 8) & 0xff;
    buffer[6] = motor_running_state;
    buffer[7] = fault_code;
    unsigned int battery_percent = 0;
    if ( Env::get_battery_percent(battery_percent) != true ){
        battery_percent = 0;
    }
    if ( battery_percent > 100 ){
        battery_percent = 100;
    }
    buffer[8] = (uint8_t)battery_percent;
    checksum_calculator.start();
    checksum_calculator.feed(buffer,9);
    buffer[9] = checksum_calculator.get();

    mqtt_publish_base64(topic,(unsigned char*)buffer,10);
    LOG_INFO("Report turns %d (%d%% of %d), fault %u.",
             current_turns, opening_percentage, cfg.motor_stroke_time, (unsigned int)fault_code);
}

void IOTService_up_config(){
    static char topic[64] = {0};
    static char payload[32] = {0};
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
    payload[13] = 1; /* 温湿度计无 device_valid 字段 */
    payload[14] = (uint8_t)(config.motor_stroke_time & 0xff);
    payload[15] = (uint8_t)((config.motor_stroke_time >> 8) & 0xff);
    payload[16] = (uint8_t)(config.motor_turn_seconds & 0xff);
    payload[17] = (uint8_t)((config.motor_turn_seconds >> 8) & 0xff);
    payload[18] = (uint8_t)(config.temp_vent_upper_limit & 0xff);
    payload[19] = (uint8_t)((config.temp_vent_upper_limit >> 8) & 0xff);
    payload[20] = (uint8_t)(config.temp_vent_lower_limit & 0xff);
    payload[21] = (uint8_t)((config.temp_vent_lower_limit >> 8) & 0xff);
    /* 温度校准：与 P4 温度补偿相同，协议×10（有符号）. */
    payload[22] = (uint8_t)(config.temp_compensation_value & 0xff);
    payload[23] = (uint8_t)((config.temp_compensation_value >> 8) & 0xff);
    checksum_calculator.start();
    checksum_calculator.feed(payload,24);
    payload[24] = checksum_calculator.get();

    snprintf(topic,63,"greenhouse/%s/up/config",g_imei);

    mqtt_publish_base64(topic,(unsigned char*)payload,25);
}

void IOTService_request_up_status(){
    pending_up_status = 1;
}

void IOTService_request_up_config(){
    pending_up_config = 1;
}

static void message_loop(){
    static struct {
        int last_opening;
        unsigned char full_sec;
        unsigned char status_sec;
        unsigned char last_running;
        unsigned char first_full;
    } up = { -1, 0, 0, 0, 1 };

    int opening = 0;
    Motor::State motor_state = Motor::State::IDLE;
    VentilateService::get_opening_percentage(opening);
    VentilateService::get_motor_state(motor_state);
    unsigned char running = 0;
    if ( (motor_state == Motor::State::RUNNING_FORWARD)
         || (motor_state == Motor::State::RUNNING_REVERSE) ){
        running = 1;
    }

    Config_t config;
    ConfigService::get_config(config);
    int full_interval = config.data_upload_interval;
    if ( full_interval < DATA_UPLOAD_INTERVAL_MIN ){
        full_interval = DATA_UPLOAD_INTERVAL_MIN;
    }else if ( full_interval > DATA_UPLOAD_INTERVAL_MAX ){
        full_interval = DATA_UPLOAD_INTERVAL_MAX;
    }

    if ( up.full_sec < 250 ){
        up.full_sec = (unsigned char)(up.full_sec + 1);
    }
    if ( up.status_sec < 250 ){
        up.status_sec = (unsigned char)(up.status_sec + 1);
    }

    int opening_delta = opening - up.last_opening;
    if ( opening_delta < 0 ){
        opening_delta = -opening_delta;
    }

    unsigned char need_status = 0;
    if ( running != 0 ){
        if ( up.last_running == 0 ){
            need_status = 1;
        }else if ( (up.last_opening >= 0) && (opening_delta >= 10) ){
            need_status = 1;
        }else if ( up.status_sec >= 5 ){
            need_status = 1;
        }
    }else if ( up.last_running != 0 ){
        need_status = 1;
    }else if ( (up.last_opening >= 0) && (opening != up.last_opening)
                && ((opening <= 0) || (opening >= 100) || (opening_delta >= 10)) ){
        need_status = 1;
    }
    if ( (up.first_full != 0) || (up.full_sec >= full_interval) ){
        up.first_full = 0;
        IOTService_up_devinfo();
        IOTService_wait_ms(100);
        IOTService_up_status();
        IOTService_wait_ms(100);
        IOTService_up_config();
        IOTService_wait_ms(100);
        up.full_sec = 0;
        up.status_sec = 0;
        up.last_opening = opening;
        up.last_running = running;
        return;
    }

    if ( need_status != 0 ){
        IOTService_up_status();
        IOTService_wait_ms(100);
        up.status_sec = 0;
        up.last_opening = opening;
    }
    up.last_running = running;
}

void IOTService::eventloop(){
    static unsigned char boot_wait = 0;
    static unsigned char retry_wait = 0;
    static unsigned char net_ok = 0;

    IOTService_drain_rx();
    /* 先落盘再上报，避免下发路径里擦 Flash 丢串口. */
    if ( ConfigService::flush_store() ){
        IOTService_drain_rx();
        LOG_INFO("Config flash flushed.");
    }
    if ( pending_up_status != 0 ){
        pending_up_status = 0;
        IOTService_up_status();
        IOTService_wait_ms(50);
    }
    if ( pending_up_config != 0 ){
        pending_up_config = 0;
        IOTService_up_config();
        IOTService_wait_ms(50);
    }

    if ( boot_wait < 10 ){
        boot_wait++;
        return;
    }
    if ( net_ok == 0 ){
        if ( retry_wait > 0 ){
            retry_wait--;
            return;
        }
        if ( IOTService_serv_init() ){
            net_ok = 1;
        }else{
            LOG_ERROR("MQTT init failed, retry in 15s.");
            retry_wait = 15;
        }
        return;
    }
    if ( g_imei_flag && g_mccid_flag ){
        message_loop();
    }
}

bool IOTService::get_network_status(){
    if ( g_imei_flag && g_mccid_flag ){
        return true;
    }else{
        return false;
    }
}

static void IOTService_task(void *param){
    LOG_INFO("IOT service running.");
    while ( true ){
        IOTService::eventloop();
        IOTService_wait_ms(1000);
    }
}

bool IOTService::start(){
    if ( xTaskCreate(IOTService_task,"iot",384,nullptr,10,&task_handle_iot) != pdPASS ){
        LOG_ERROR("IOT task start failed.");
        return false;
    }
    return true;
}

extern "C"
void USART2_IRQHandler(void){
    uint8_t temp = (uint8_t)huart_iot.Instance->DR;
    uint16_t next = (uint16_t)((rx_w + 1) & RX_RING_MASK);
    if ( next != rx_r ){
        rx_ring[rx_w] = temp;
        rx_w = next;
    }
    __HAL_UART_CLEAR_NEFLAG(&huart_iot);
}