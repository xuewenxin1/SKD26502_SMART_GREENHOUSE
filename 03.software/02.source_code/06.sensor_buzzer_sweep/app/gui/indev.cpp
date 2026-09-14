#include "indev.hpp"
#include "Button.hpp"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_gpio.h"

#define PORT_BUTTON_MODE GPIOF
#define PIN_BUTTON_MODE GPIO_PIN_6
#define PORT_BUTTON_SETTING GPIOB
#define PIN_BUTTON_SETTING GPIO_PIN_12
#define PORT_BUTTON_PLUS GPIOC
#define PIN_BUTTON_PLUS GPIO_PIN_13
#define PORT_BUTTON_MINUS GPIOB
#define PIN_BUTTON_MINUS GPIO_PIN_2
#define ACTIVE_PIN_LEVEL GPIO_PIN_RESET
#define CLK_ENABLE() __HAL_RCC_GPIOF_CLK_ENABLE(); \
                    __HAL_RCC_GPIOB_CLK_ENABLE(); \
                    __HAL_RCC_GPIOC_CLK_ENABLE()

class ButtonAdapter : public Button::Adapter {
public:
    explicit ButtonAdapter(GUI::Button button_number){
        this->button_number = button_number;
    }

    virtual bool get(void){
        GPIO_TypeDef *port;
        uint16_t pin;
        switch ( this->button_number ){
            case GUI::Button::Mode: {
                port = PORT_BUTTON_MODE;
                pin = PIN_BUTTON_MODE;
                break;
            }
            case GUI::Button::Setting: {
                port = PORT_BUTTON_SETTING;
                pin = PIN_BUTTON_SETTING;
                break;
            }
            case GUI::Button::Plus: {
                port = PORT_BUTTON_PLUS;
                pin = PIN_BUTTON_PLUS;
                break;
            }
            case GUI::Button::Minus: {
                port = PORT_BUTTON_MINUS;
                pin = PIN_BUTTON_MINUS;
                break;
            }
            default: {
                return false;
            }
        }
        if ( HAL_GPIO_ReadPin(port,pin) == ACTIVE_PIN_LEVEL ){
            return true;
        }else{
            return false;
        }
    }
protected:
    GUI::Button button_number = GUI::Button::Invalid;
};

static Button *button_mode = nullptr;
static Button *button_setting = nullptr;
static Button *button_plus = nullptr;
static Button *button_minus = nullptr;
static ButtonAdapter *button_adapter_mode = nullptr;
static ButtonAdapter *button_adapter_setting = nullptr;
static ButtonAdapter *button_adapter_plus = nullptr;
static ButtonAdapter *button_adapter_minus = nullptr;

static void button_click_callbacks(char status,int extra){
    if ( status == '.' ){
        GUI::Button button = static_cast<GUI::Button>(extra);
        GUI::button_callback(button,GUI::ButtonEvent::Click);
    }
}

static void button_long_press_callbacks(int extra){
    GUI::Button button = static_cast<GUI::Button>(extra);
    GUI::button_callback(button,GUI::ButtonEvent::LongPress);
}

static void Indev_board_init(){
    GPIO_InitTypeDef gpio_init_config = {0};

    CLK_ENABLE();

    gpio_init_config.Pin = PIN_BUTTON_MODE;
    gpio_init_config.Mode = GPIO_MODE_INPUT;
    gpio_init_config.Pull = GPIO_PULLUP;
    gpio_init_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init_config.Alternate = 0;
    HAL_GPIO_Init(PORT_BUTTON_MODE, &gpio_init_config);
    gpio_init_config.Pin = PIN_BUTTON_SETTING;
    HAL_GPIO_Init(PORT_BUTTON_SETTING, &gpio_init_config);
    gpio_init_config.Pin = PIN_BUTTON_PLUS;
    HAL_GPIO_Init(PORT_BUTTON_PLUS, &gpio_init_config);
    gpio_init_config.Pin = PIN_BUTTON_MINUS;
    HAL_GPIO_Init(PORT_BUTTON_MINUS, &gpio_init_config);
}

bool Indev::init(){
    Indev_board_init();
    /* 模式按键. */
    button_adapter_mode = new ButtonAdapter(GUI::Button::Mode);
    if ( button_adapter_mode == nullptr ){
        return false;
    }
    button_mode = new Button(button_adapter_mode);
    if ( button_mode == nullptr ){
        return false;
    }
    button_mode->setClickCallback(button_click_callbacks,static_cast<int>(GUI::Button::Mode));
    button_mode->setLongPressCallback(button_long_press_callbacks,static_cast<int>(GUI::Button::Mode));

    /* 设置按键. */
    button_adapter_setting = new ButtonAdapter(GUI::Button::Setting);
    if ( button_adapter_setting == nullptr ){
        return false;
    }
    button_setting = new Button(button_adapter_setting);
    if ( button_setting == nullptr ){
        return false;
    }
    button_setting->setClickCallback(button_click_callbacks,static_cast<int>(GUI::Button::Setting));
    button_setting->setLongPressCallback(button_long_press_callbacks,static_cast<int>(GUI::Button::Setting));

    /* 加按键. */
    button_adapter_plus = new ButtonAdapter(GUI::Button::Plus);
    if ( button_adapter_plus == nullptr ){
        return false;
    }
    button_plus = new Button(button_adapter_plus);
    if ( button_plus == nullptr ){
        return false;
    }
    button_plus->setClickCallback(button_click_callbacks,static_cast<int>(GUI::Button::Plus));
    button_plus->setLongPressCallback(button_long_press_callbacks,static_cast<int>(GUI::Button::Plus));
    button_plus->setLongPressRepeatTickCount(40);

    /* 减按键. */
    button_adapter_minus = new ButtonAdapter(GUI::Button::Minus);
    if ( button_adapter_minus == nullptr ){
        return false;
    }
    button_minus = new Button(button_adapter_minus);
    if ( button_minus == nullptr ){
        return false;
    }
    button_minus->setClickCallback(button_click_callbacks,static_cast<int>(GUI::Button::Minus));
    button_minus->setLongPressCallback(button_long_press_callbacks,static_cast<int>(GUI::Button::Minus));
    button_minus->setLongPressRepeatTickCount(40);

    return true;
}

void Indev::eventloop(){
    button_mode->eventloop();
    button_setting->eventloop();
    button_plus->eventloop();
    button_minus->eventloop();
}

bool Indev::get_button_status(GUI::Button button){
    GPIO_TypeDef *port;
    uint16_t pin;
    switch ( button ){
        case GUI::Button::Mode: {
            port = PORT_BUTTON_MODE;
            pin = PIN_BUTTON_MODE;
            break;
        }
        case GUI::Button::Setting: {
            port = PORT_BUTTON_SETTING;
            pin = PIN_BUTTON_SETTING;
            break;
        }
        case GUI::Button::Plus: {
            port = PORT_BUTTON_PLUS;
            pin = PIN_BUTTON_PLUS;
            break;
        }
        case GUI::Button::Minus: {
            port = PORT_BUTTON_MINUS;
            pin = PIN_BUTTON_MINUS;
            break;
        }
        default: {
            return false;
        }
    }
    if ( HAL_GPIO_ReadPin(port,pin) == ACTIVE_PIN_LEVEL ){
        return true;
    }else{
        return false;
    }
}


