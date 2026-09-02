#ifndef BUTTON_HPP
#define BUTTON_HPP


class Button {
public:
    class Adapter {
    public:
        virtual bool get(void) = 0;
    };

    struct Configuration {
        int long_press_tick_count;
        int short_press_tick_count;
        int idle_tick_count;
        int long_press_repeat_tick_count;
    };

    typedef void (*ClickCallback)(char status,int extra);
    typedef void (*LongPressCallback)(int extra);
    typedef void (*ShortPressCallback)(int extra);
    typedef void (*KeyValueCallback)(int count,char *status);

    static const int VALUE_SEQ_LENGTH = 10;
    
public:
    explicit Button(Adapter *adapter);
    explicit Button(Adapter *adapter,Configuration configuartion);
    virtual ~Button();

    bool get();

    bool setClickCallback(ClickCallback click_callback,int extra);
    bool setShortPressCallback(ShortPressCallback short_press_callback,int extra);
    bool setLongPressCallback(LongPressCallback long_press_callback,int extra);
    bool setLongPressRepeatTickCount(int tick_count);
    bool setKeyValueCallback(KeyValueCallback key_value_callback);
    void eventloop();
private:
    Adapter *adapter;
    Configuration configuration;
    ClickCallback click_callback;
    int click_callback_extra;
    int shortpress_callback_extra;
    int longpress_callback_extra;
    ShortPressCallback short_press_callback;
    LongPressCallback long_press_callback;
    KeyValueCallback key_value_callback;
    bool last_status;
    int pressed_tick;
    int released_tick;
    char value_seq[VALUE_SEQ_LENGTH];
    int n_value;
};

#endif