#include "urc_handler.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>

MqttPublishUrcHandler::MqttPublishUrcHandler(uint8_t *buffer, unsigned int buffer_capacity)
    : buffer(buffer),
    buffer_capacity(buffer_capacity)
{
    this->mqtt_message_callback = nullptr;
    this->total_length = 0;
    this->received_length = 0;
    this->receiving = false;
}

UrcHandler::UrcResult MqttPublishUrcHandler::process_line(unsigned char *line_buffer, unsigned int length){
    if ( this->receiving == false ){
        /* 检查新URC消息. */
        if ( strncmp((const char*)line_buffer + 1,"MQTTURC",7) == 0 ){
            /* 确实是MQTTURC. */
            
            strtok((char*)line_buffer,":");
            char *content = strtok(nullptr,":");
            const char *str_type = strtok(content,",");
            const char *str_conn_id = strtok(nullptr,",");
            const char *str_mid = strtok(nullptr,",");
            const char *str_topic = strtok(nullptr,",");
            const char *str_total_len = strtok(nullptr,",");
            const char *str_payload_len = strtok(nullptr,",");
            const char *payload = strtok(nullptr,",");
            if ( payload != nullptr ){
                strcpy(this->topic,str_topic);
                unsigned int line_payload_length = length - (unsigned int)(payload - (const char*)line_buffer);
                memcpy(this->buffer,payload,line_payload_length);
                this->received_length = line_payload_length;
                this->total_length = atoi(str_total_len);
                if ( this->received_length < this->total_length ){
                    this->receiving = true;
                    return UrcHandler::UrcResult::WaitingNextLine;
                }else{
                    /* 接收完成，处理. */
                    if ( this->mqtt_message_callback != nullptr ){
                        this->mqtt_message_callback(this->topic,(const char*)this->buffer,this->received_length);
                    }
                    this->total_length = 0;
                    this->received_length = 0;
                    this->receiving = false;
                    return UrcHandler::UrcResult::Ok;
                }
            }
        }else{
            return UrcHandler::UrcResult::None;
        }
    }else{
        /* 正在接收. */
        memcpy(this->buffer + this->received_length, line_buffer, length);
        this->received_length += length;
        if ( this->received_length < this->total_length ){
            this->receiving = true;
            return UrcHandler::UrcResult::WaitingNextLine;
        }else{
            /* 接收完成，处理. */
            if ( this->mqtt_message_callback != nullptr ){
                this->buffer[this->total_length] = 0;
                this->mqtt_message_callback(this->topic,(const char*)this->buffer,this->total_length);
            }
            this->total_length = 0;
            this->received_length = 0;
            this->receiving = false;
            return UrcHandler::UrcResult::Ok;
        }
    }
    
    return UrcHandler::UrcResult::None;
}

void MqttPublishUrcHandler::set_mqtt_message_callback(MqttMessageCallback_t callback){
    this->mqtt_message_callback = callback;
}

MccidUrcHandler::MccidUrcHandler(){
    this->mccid_callback = nullptr;
}

UrcHandler::UrcResult MccidUrcHandler::process_line(unsigned char *line_buffer, unsigned int length){
    if ( strncmp((const char*)line_buffer + 1,"MCCID",5) == 0 ){
        strtok((char*)line_buffer,":");
        char *mccid = strtok(nullptr,":");
        while ( *mccid != 0 ){
            if ( *mccid == ' ' ){
                mccid += 1;
            }else{
                break;
            }
        }
        int str_len = strlen(mccid);
        for ( int n = 0; n < str_len; n++ ){
            if ( mccid[n] == '\r' || mccid[n] == '\n' ){
                mccid[n] = 0;
                break;
            }
        }
        
        if ( this->mccid_callback != nullptr ){
            this->mccid_callback(mccid,strlen(mccid));
        }
        return UrcHandler::UrcResult::Ok;
    }
    return UrcHandler::UrcResult::None;
}

void MccidUrcHandler::set_mccid_callback(MccidUrcHandler::MccidCallback_t callback){
    this->mccid_callback = callback;
}


CgsnUrcHandler::CgsnUrcHandler(){
    this->cgsn_callback = nullptr;
}

UrcHandler::UrcResult CgsnUrcHandler::process_line(unsigned char *line_buffer, unsigned int length){
    if ( strncmp((const char*)line_buffer + 1,"GSN",3) == 0 ){
        strtok((char*)line_buffer,":");
        char *imei = strtok(nullptr,":");
        while ( *imei != 0 ){
            if ( *imei == ' ' ){
                imei += 1;
            }else{
                break;
            }
        }
        int str_len = strlen(imei);
        for ( int n = 0; n < str_len; n++ ){
            if ( imei[n] == '\r' || imei[n] == '\n' ){
                imei[n] = 0;
                break;
            }
        }
        if ( this->cgsn_callback != nullptr ){
            this->cgsn_callback(imei,strlen(imei));
        }
        return UrcHandler::UrcResult::Ok;
    }
    return UrcHandler::UrcResult::None;
}

void CgsnUrcHandler::set_cgsn_callback(CgsnCallback_t callback){
    this->cgsn_callback = callback;
}