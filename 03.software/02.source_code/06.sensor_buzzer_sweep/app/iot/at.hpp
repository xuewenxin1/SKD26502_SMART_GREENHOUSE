#ifndef AT_HPP
#define AT_HPP

#include <cstdint>

class UrcHandler {
public:
    enum class UrcResult {
        None,
        Ok,
        WaitingNextLine,
    };

public:
    virtual UrcResult process_line(unsigned char *line_buffer, unsigned int length) = 0;
};

class AT {
public:
    enum class Status {
        IDLE,
        WaitingResponse,
        UrcWaitingNextLine,
    };

    enum class CommandReturn {
        NoReturn,
        Ok,
        Error,
    };

    typedef void (*SendCallback)(const char *buffer, unsigned int length);
public:
    explicit AT(uint8_t *buffer, unsigned int buffer_capacity);

    void send(const char *buffer, unsigned int length);
    CommandReturn get_command_return();

    void feed_byte(uint8_t byte);
    void process_line(uint8_t *line_buffer, unsigned int length);
    bool add_urc_handler(UrcHandler *urc_handler);
    bool set_send_callback(SendCallback callback);
private:
    uint8_t *buffer;
    unsigned int buffer_capacity;
    unsigned int rx_size;
    UrcHandler* urc_handler_list[16];
    UrcHandler* current_urc_handler;
    Status status;
    Status last_status;
    SendCallback send_callback;
    CommandReturn command_return;
};

#endif
