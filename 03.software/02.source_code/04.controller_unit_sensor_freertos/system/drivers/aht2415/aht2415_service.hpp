#ifndef AHT2415_SERVICE_HPP
#define AHT2415_SERVICE_HPP

namespace AHT2415_Service {

bool init();

bool get(double &temperature, double &humidity);

}

#endif
