#include "ntc_service.hpp"
#include "adc.hpp"
#include "console_service.hpp"

static struct NTC_Parameter {
    double k;
    double b;
} ntc_parameter = {
    .k = -0.0151315789,
    .b = 57.976973,
};

bool NTC_Service::init(){
    return true;
}

bool NTC_Service::get_temperature(double &temperature){
    uint16_t adc_value = 0;
    if ( ADC::get(ADC::Channel::NTC,adc_value) != true ){
        return false;
    }
    temperature = ((double)adc_value) * ntc_parameter.k + ntc_parameter.b;
    LOG_DEBUG("NTC value %d",adc_value);
    return true;
}
