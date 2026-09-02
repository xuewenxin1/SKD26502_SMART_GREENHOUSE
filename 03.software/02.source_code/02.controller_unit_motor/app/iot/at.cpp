#include "at.hpp"
#include <cstdio>
#include <cstring>

AT::AT(uint8_t *buffer, unsigned int buffer_capacity) :
    buffer(buffer),
    buffer_capacity(buffer_capacity) 
{
    for ( int n = 0; n < 16; n++ ){
        urc_handler_list[n] = nullptr;
    }
    current_urc_handler = nullptr;
    rx_size = 0;
    status = Status::IDLE;
    last_status = Status::IDLE;
    command_return = CommandReturn::NoReturn;
    send_callback = nullptr;
}

void AT::send(const char *buffer, unsigned int length){
    /* 发送. */
    if ( send_callback != nullptr ){
        send_callback(buffer,length);
    }
    this->status = Status::WaitingResponse;
    command_return = CommandReturn::NoReturn;
}

AT::CommandReturn AT::get_command_return(){
    return this->command_return;
}

void AT::feed_byte(uint8_t byte){
    this->buffer[this->rx_size] = byte;
    this->rx_size += 1;
    if ( byte == '\n' ){
        this->buffer[this->rx_size] = 0;
        this->process_line(this->buffer, this->rx_size);
        this->rx_size = 0;
    }
}

void AT::process_line(uint8_t *line_buffer, unsigned int length){
    if ( this->status == Status::UrcWaitingNextLine ){
        /* URC处理器正在等待下一行. */
        if ( current_urc_handler != nullptr ){
            UrcHandler::UrcResult urc_result = current_urc_handler->process_line(line_buffer,length);
            if ( urc_result == UrcHandler::UrcResult::None ){
                /* 出错，复位状态机. */
                if ( this->last_status != Status::UrcWaitingNextLine ){
                    this->status = this->last_status;
                }else{
                    this->status = Status::IDLE;
                }
                return;
            }else if ( urc_result == UrcHandler::UrcResult::Ok ){
                /* URC处理结束，且该行也被消耗. */
                if ( this->last_status != Status::UrcWaitingNextLine ){
                    this->status = this->last_status;
                }else{
                    this->status = Status::IDLE;
                }
                return;
            }else if ( urc_result == UrcHandler::UrcResult::WaitingNextLine ){
                this->status = Status::UrcWaitingNextLine;
                return;
            }
        }else{
            /* 出错，复位状态机. */
            if ( this->last_status != Status::UrcWaitingNextLine ){
                this->status = this->last_status;
            }else{
                this->status = Status::IDLE;
            }
            return;
        }
    }else{
        if ( line_buffer[0] == '+' ){
            /* 可能是URC消息. */
            for ( int n = 0; n < 16; n++ ){
                if ( urc_handler_list[n] != nullptr ){
                    UrcHandler::UrcResult urc_result = urc_handler_list[n]->process_line(line_buffer,length);
                    if ( urc_result == UrcHandler::UrcResult::Ok ){
                        /* 该行被URC处理器消耗. 无需额外处理. */
                        return;
                    }else if ( urc_result == UrcHandler::UrcResult::WaitingNextLine ){
                        /* 该行被URC处理器消耗,且URC处理器需要下一行. */
                        this->current_urc_handler = urc_handler_list[n];
                        this->last_status = this->status;
                        this->status = Status::UrcWaitingNextLine;
                        return;
                    }
                }
            }
            /* 如果没有URC处理器能够处理这个URC消息则当成日志打印. */
            // printf("%s",line_buffer);
        }else{
            /* 如果不是以+开头的行. */
            if ( this->status == Status::WaitingResponse ){
                /* 有命令等待响应. */
                if ( strstr((char*)line_buffer,"OK") == (const char*)line_buffer ){
                    /* 该行是以OK起始的. */
                    this->command_return = CommandReturn::Ok;
                    this->status = Status::IDLE;
                }else if ( strstr((char*)line_buffer,"ERROR") == (const char*)line_buffer ){
                    /* 该行是以ERROR起始的. */
                    this->command_return = CommandReturn::Error;
                    this->status = Status::IDLE;
                }else{
                    /* 都不是，输出日志. */
                    // printf("%s",line_buffer);
                }
            }else{
                /* 空闲状态，输出日志. */
                // printf("%s",line_buffer);
            }
        }
    }
}

bool AT::add_urc_handler(UrcHandler *urc_handler){
    for ( int n = 0; n < 16; n++ ){
        if ( this->urc_handler_list[n] == nullptr ){
            this->urc_handler_list[n] = urc_handler;
            return true;
        }
    }
    return false;
}

bool AT::set_send_callback(SendCallback callback){
    this->send_callback = callback;
    return true;
}