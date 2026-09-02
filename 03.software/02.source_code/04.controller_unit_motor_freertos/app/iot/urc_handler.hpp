#ifndef URC_HANDLER_HPP
#define URC_HANDLER_HPP

#include "at.hpp"

class MqttPublishUrcHandler : public UrcHandler {
public:
    typedef void (*MqttMessageCallback_t)(const char *topic, const char *payload, unsigned int length);
    
    explicit MqttPublishUrcHandler(uint8_t *buffer, unsigned int buffer_capacity);
    virtual UrcResult process_line(unsigned char *line_buffer, unsigned int length);
    void set_mqtt_message_callback(MqttMessageCallback_t callback);
private:
    uint8_t *buffer;
    char topic[128];
    unsigned int buffer_capacity;
    MqttMessageCallback_t mqtt_message_callback;
    unsigned int total_length;
    unsigned int received_length;
    bool receiving;
};

class MccidUrcHandler : public UrcHandler {
public:
    typedef void (*MccidCallback_t)(const char *mccid, unsigned int length);

    explicit MccidUrcHandler();
    virtual UrcResult process_line(unsigned char *line_buffer, unsigned int length);
    void set_mccid_callback(MccidCallback_t callback);
private:
    MccidCallback_t mccid_callback;
};

class CgsnUrcHandler : public UrcHandler {
public:
    typedef void (*CgsnCallback_t)(const char *imei, unsigned int length);

    explicit CgsnUrcHandler();
    virtual UrcResult process_line(unsigned char *line_buffer, unsigned int length);
    void set_cgsn_callback(CgsnCallback_t callback);
private:
    CgsnCallback_t cgsn_callback;
};

#endif
