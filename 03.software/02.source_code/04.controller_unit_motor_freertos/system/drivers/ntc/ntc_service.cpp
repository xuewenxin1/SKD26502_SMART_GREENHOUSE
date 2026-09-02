#include "ntc_service.hpp"
#include "adc.hpp"
#include "console_logger.h"
#include <math.h>

/*
 * NTC B 值法（分压：上拉电阻 + NTC 对地，ADC 采中点）:
 *   R_ntc = R_series * adc / (ADC_MAX - adc)
 *   T(K)  = 1 / (1/T25 + ln(R_ntc/R25) / B)
 *   T(℃)  = T(K) - 273.15
 * 供电 VDDA=3.86V（ADC 参考随 VDDA）；B 值法用 ADC 比值，与绝对电压无关。
 * 探头：R25=10kΩ，B=3950K。
 * 板上串联上拉 10kΩ（与 10kΩ NTC 分压）。
 * ADC 已做 HAL 校准 + PCLK/4（见 adc/py32f071_hal_msp.c），10k/10k 实测 raw≈2050.
 */
#define NTC_VDDA_MV        3860.0
#define NTC_B_VALUE        3950.0
#define NTC_R25_OHM        10000.0
#define NTC_T25_KELVIN     298.15
#define NTC_SERIES_OHM     10000.0
#define NTC_ADC_FULL_SCALE ((double)ADC_FULL_SCALE)

bool NTC_Service::init(){
    LOG_INFO("NTC driver B3950, series 10k, VDDA 3.86V.");
    return true;
}

static bool adc_to_temperature(uint16_t adc_value, double &temperature){
    if ( (adc_value < 120) || (adc_value > 3800) ){
        return false;
    }
    double adc = (double)adc_value;
    if ( adc >= (NTC_ADC_FULL_SCALE - 1.0) ){
        adc = NTC_ADC_FULL_SCALE - 1.0;
    }
    double denom = NTC_ADC_FULL_SCALE - adc;
    if ( denom < 1.0 ){
        return false;
    }
    double r_ntc = NTC_SERIES_OHM * adc / denom;
    if ( r_ntc < 1.0 ){
        return false;
    }
    double ln_ratio = log(r_ntc / NTC_R25_OHM);
    double inv_t = (1.0 / NTC_T25_KELVIN) + (ln_ratio / NTC_B_VALUE);
    if ( (inv_t < 0.001) || (inv_t > 0.01) ){
        return false;
    }
    temperature = (1.0 / inv_t) - 273.15;
    if ( (temperature < -20.0) || (temperature > 80.0) ){
        return false;
    }
    return true;
}

bool NTC_Service::get_temperature(double &temperature){
    uint16_t s0 = 0;
    uint16_t s1 = 0;
    uint16_t s2 = 0;
    if ( ADC::get(ADC::Channel::NTC, s0) != true ){
        return false;
    }
    if ( ADC::get(ADC::Channel::NTC, s1) != true ){
        return false;
    }
    if ( ADC::get(ADC::Channel::NTC, s2) != true ){
        return false;
    }

    uint16_t adc_min = s0;
    uint16_t adc_max = s0;
    if ( s1 < adc_min ){ adc_min = s1; }
    if ( s2 < adc_min ){ adc_min = s2; }
    if ( s1 > adc_max ){ adc_max = s1; }
    if ( s2 > adc_max ){ adc_max = s2; }
    /* 未插时 ADC 常飘忽，跳变过大当作无效. */
    if ( (adc_max - adc_min) > 80 ){
        LOG_INFO("NTC reject jump %u (adc %u,%u,%u).",
                 (unsigned int)(adc_max - adc_min),
                 (unsigned int)s0, (unsigned int)s1, (unsigned int)s2);
        return false;
    }

    uint16_t adc_value = (uint16_t)(((unsigned int)s0 + (unsigned int)s1 + (unsigned int)s2) / 3u);
    if ( adc_to_temperature(adc_value, temperature) != true ){
        return false;
    }
    double adc_cal = (double)adc_value;
    if ( adc_cal >= (NTC_ADC_FULL_SCALE - 1.0) ){
        adc_cal = NTC_ADC_FULL_SCALE - 1.0;
    }
    double r_ntc = NTC_SERIES_OHM * adc_cal / (NTC_ADC_FULL_SCALE - adc_cal);
    static unsigned int ntc_log_cnt = 0;
    if ( (ntc_log_cnt++ % 5u) == 0u ){
        unsigned int mv = (unsigned int)((adc_value * (unsigned int)NTC_VDDA_MV + (ADC_FULL_SCALE / 2u)) / ADC_FULL_SCALE);
        LOG_INFO("NTC adc %u (%u mV), R %u ohm, temp %.1f C.", (unsigned int)adc_value, mv, (unsigned int)r_ntc, temperature);
    }
    return true;
}
