#ifndef MOTOR_HPP
#define MOTOR_HPP

#include <cstdint>

class Motor {
public:
    class PortableStrategy {
    public:
        enum class Pin {
            FORWARD,
            REVERSE,
        };
    public:
        virtual bool init() = 0;
        virtual bool deinit() = 0;
        virtual bool set_output(Pin pin, bool level) = 0;
        virtual bool get_adc_value(uint16_t &value) = 0;
    };

    struct Parameter {
        double cs_k;
        double cs_b;
        double zero_threshold;
        double ovc_threshold;
    };

    enum class Action {
        STOP,
        FORWARD,
        REVERSE,
    };

    enum class State {
        IDLE,
        RUNNING_FORWARD,
        RUNNING_REVERSE,
        ERROR_OVC,
    };


public:
    explicit Motor(PortableStrategy &portable_strategy,Parameter param);

    bool init();
    bool deinit();

    bool execute_action(Action action);
    bool get_state(State &value);
    bool get_current(double &value);
    void timer_trigger(void);
    bool reset_timer(void);
    bool get_timer_cnt(int64_t &cnt);
    bool set_timer_cnt(int64_t cnt);
    void set_home_seek(bool enable);
    void set_calibrating(bool enable);
    void set_ignore_stall(bool enable);
    /* 寻机械限位：忽略假零流，但仍检测堵转. */
    void set_ignore_zero_current(bool enable);

    void eventloop();
private:
    void reset_motion_detect(void);
    PortableStrategy &portable_strategy;
    Parameter param;
    State state;
    unsigned int stop_cnt;
    unsigned int oc_cnt;
    unsigned int limit_ignore_cnt;
    unsigned int stall_cnt;
    unsigned int baseline_cnt;
    bool run_current_ready;
    double run_current;
    int64_t timer_cnt = 0;
    double current;
    bool home_seek;
    bool calibrating;
    bool ignore_stall;
    bool ignore_zero_current;
};

#endif