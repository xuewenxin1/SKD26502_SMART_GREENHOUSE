#ifndef IOT_SERVICE_HPP
#define IOT_SERVICE_HPP

namespace IOTService {

bool init();
bool start();
bool get_network_status();
void eventloop();

}

#endif
