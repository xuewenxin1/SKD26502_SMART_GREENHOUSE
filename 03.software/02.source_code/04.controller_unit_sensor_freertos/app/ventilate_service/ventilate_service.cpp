#include "ventilate_service.hpp"
#include "motor_service.hpp"
#include "console_logger.h"
#include "config.hpp"
#include "end_stop_policy.hpp"
#include "env.hpp"
#include <cmath>
#include "FreeRTOS.h"
#include "task.h"

static Motor *motor;
static VentilateService::Status ventilate_service_status;
static VentilateService::ForceAction force_action = VentilateService::ForceAction::None;
static double opening_percentage_curr = 0;
static double opening_percentage_set = 0;
static int32_t calibrate_value = 0;
static bool align_stay_closed = false;
static bool restore_saved_opening = false;
static int persisted_opening = -1;
static int pending_persist_opening = -1;
static unsigned int persist_idle_loops = 0;
static unsigned int align_loop_cnt = 0;
static unsigned int align_budget_loops = 300;
static unsigned int align_zero_curr_cnt = 0;
static bool align_wait_zero = false;
static bool end_stop_latched = false;
static bool auto_hold_pending = false;
static int auto_hold_baseline = -1;
static TaskHandle_t task_handle_ventilate = nullptr;

static double clamp_opening(double value);
static void abort_opening_persist(void);
static void try_persist_stopped_opening(void);

static void reset_align_track(){
    align_loop_cnt = 0;
    align_zero_curr_cnt = 0;
    align_wait_zero = false;
}

static void finish_align_home(const Config_t &config){
    motor->execute_action(Motor::Action::STOP);
    opening_percentage_curr = 0;
    motor->reset_timer();
    align_wait_zero = false;
    align_zero_curr_cnt = 0;
    if ( align_stay_closed ){
        opening_percentage_set = 0;
        align_stay_closed = false;
        restore_saved_opening = false;
    }else if ( restore_saved_opening ){
        int saved = config.last_opening_percentage;
        if ( saved < 0 ){
            saved = 0;
        }
        if ( saved > 100 ){
            saved = 100;
        }
        opening_percentage_set = (double)saved;
        restore_saved_opening = false;
        LOG_INFO("ALIGN restore saved opening %d percent.", saved);
    }else{
        opening_percentage_set = 0;
    }
    ventilate_service_status = VentilateService::Status::STOPPED;
    motor->set_home_seek(false);
    LOG_INFO("Zero point set, timer start from 0.");
}

static double clamp_opening(double value);

static int opening_to_int(double opening){
    int value = (int)(clamp_opening(opening) + 0.5);
    if ( value < 0 ){
        value = 0;
    }
    if ( value > 100 ){
        value = 100;
    }
    return value;
}

/* 自动模式：只算整数℃，小数直接丢掉不四舍五入。温差 0+X℃→0%，每高 1℃ 开 10%，≥10+X℃ 开 100%。 */
static int opening_from_temp_delta(double measured, double target, double hyst){
    int meas = (int)measured;
    int tgt = (int)target;
    int x = (int)hyst;
    if ( x < 0 ){
        x = 0;
    }
    if ( x > 5 ){
        x = 5;
    }
    int effective = meas - tgt - x;
    if ( effective <= 0 ){
        return 0;
    }
    if ( effective >= 10 ){
        return 100;
    }
    return effective * 10;
}

static void abort_opening_persist(void){
    pending_persist_opening = -1;
    persist_idle_loops = 0;
}

static void try_persist_stopped_opening(void){
    if ( ventilate_service_status != VentilateService::Status::STOPPED ){
        abort_opening_persist();
        return;
    }
    if ( force_action != VentilateService::ForceAction::None ){
        abort_opening_persist();
        return;
    }
    Motor::State state = Motor::State::IDLE;
    motor->get_state(state);
    if ( state != Motor::State::IDLE ){
        abort_opening_persist();
        return;
    }
    double delta = opening_percentage_curr - opening_percentage_set;
    if ( delta < 0.0 ){
        delta = -delta;
    }
    if ( delta > 0.5 ){
        abort_opening_persist();
        return;
    }
    int value = opening_to_int(opening_percentage_curr);
    if ( value == persisted_opening ){
        abort_opening_persist();
        return;
    }
    if ( value != pending_persist_opening ){
        pending_persist_opening = value;
        persist_idle_loops = 0;
    }
    persist_idle_loops++;
    if ( persist_idle_loops < 5 ){
        return;
    }
    persist_idle_loops = 0;
    Config_t saved;
    ConfigService::get_config(saved);
    saved.last_opening_percentage = value;
    ConfigService::set_config(saved);
    ConfigService::store();
    persisted_opening = value;
    pending_persist_opening = -1;
    LOG_INFO("Save opening %d percent after motor stop.", value);
}

static double clamp_opening(double value){
    if ( value < 0.0 ){
        return 0.0;
    }
    if ( value > 100.0 ){
        return 100.0;
    }
    return value;
}

static double calc_opening_percent(int64_t motor_timer_cnt, int stroke_time){
    double stroke = (double)stroke_time;
    if ( stroke < (double)MOTOR_STROKE_TIME_MIN ){
        stroke = (double)MOTOR_STROKE_TIME_MIN;
    }
    return ((double)motor_timer_cnt / 10.0) / stroke;
}

static int stroke_seconds(int stroke_time){
    if ( stroke_time < MOTOR_STROKE_TIME_MIN ){
        return MOTOR_STROKE_TIME_MIN;
    }
    return stroke_time;
}

static void hold_opening(double opening, int stroke_time){
    opening = clamp_opening(opening);
    motor->execute_action(Motor::Action::STOP);
    if ( opening <= 0.0 ){
        motor->reset_timer();
        opening_percentage_curr = 0.0;
    }else{
        int stroke = stroke_seconds(stroke_time);
        int64_t cnt = (int64_t)(opening * 10.0 * (double)stroke + 0.5);
        motor->set_timer_cnt(cnt);
        opening_percentage_curr = opening;
    }
    opening_percentage_set = opening_percentage_curr;
}

static void heal_negative_timer(void){
    int64_t cnt = 0;
    motor->get_timer_cnt(cnt);
    if ( cnt < 0 ){
        motor->reset_timer();
    }
}

static void drive(Motor::Action action){
    Motor::State state = Motor::State::IDLE;
    motor->get_state(state);
    if ( (action == Motor::Action::FORWARD) && (state == Motor::State::RUNNING_FORWARD) ){
        return;
    }
    if ( (action == Motor::Action::REVERSE) && (state == Motor::State::RUNNING_REVERSE) ){
        return;
    }
    if ( (action == Motor::Action::STOP) && (state == Motor::State::IDLE) ){
        return;
    }
    motor->execute_action(action);
}

static bool in_calibrating(void){
    return (ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE1)
        || (ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE2)
        || (ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE3);
}

static void stop_at_travel_limits(const Config_t &config){
    if ( in_calibrating() ){
        return;
    }
    if ( ventilate_service_status == VentilateService::Status::ALIGN ){
        /* Home is current==0, not software 0 percent. */
        return;
    }
    int64_t cnt = 0;
    Motor::State state = Motor::State::IDLE;
    motor->get_timer_cnt(cnt);
    motor->get_state(state);
    const int shown = opening_to_int(calc_opening_percent(cnt, config.motor_stroke_time));
    const bool force_open = (force_action == VentilateService::ForceAction::ForceOpen);
    const bool opening_now = (state == Motor::State::RUNNING_FORWARD)
        || force_open
        || (opening_percentage_set > 0.5);
    if ( shown <= 0 ){
        if ( (force_open == false) && (state != Motor::State::RUNNING_FORWARD)
             && (opening_percentage_set <= 0.5) ){
            const bool first_cut = (end_stop_latched == false) || (state != Motor::State::IDLE);
            hold_opening(0.0, config.motor_stroke_time);
            force_action = VentilateService::ForceAction::None;
            motor->set_ignore_stall(false);
            end_stop_latched = true;
            if ( first_cut ){
                LOG_INFO("Stop at 0 percent, cut power.");
            }
            return;
        }
    }
    const bool closing = (force_action == VentilateService::ForceAction::ForceClose)
        || (ventilate_service_status == VentilateService::Status::ALIGN)
        || ((opening_percentage_set <= 0.5) && (opening_now == false));
    const bool aligning = (ventilate_service_status == VentilateService::Status::ALIGN);
    const bool idle = (state == Motor::State::IDLE);
    if ( EndStopPolicy::should_stop_close(force_open, closing, aligning, align_loop_cnt, idle, shown, opening_now) ){
        hold_opening(0.0, config.motor_stroke_time);
        force_action = VentilateService::ForceAction::None;
        motor->set_ignore_stall(false);
        end_stop_latched = true;
        LOG_INFO("Stop at 0 percent.");
    }else if ( (state == Motor::State::RUNNING_FORWARD) && (shown >= 100) ){
        if ( force_action != VentilateService::ForceAction::ForceClose ){
            hold_opening(100.0, config.motor_stroke_time);
            force_action = VentilateService::ForceAction::None;
            motor->set_ignore_stall(false);
            end_stop_latched = true;
            LOG_INFO("Stop at 100 percent.");
        }
    }
}

static bool try_snap_end_stop(Motor::State state, double current, int stroke_time){
    if ( end_stop_latched ){
        if ( (opening_percentage_set > 0.5)
             || (state == Motor::State::RUNNING_FORWARD)
             || (force_action == VentilateService::ForceAction::ForceOpen) ){
            end_stop_latched = false;
            return false;
        }
        if ( (opening_percentage_set < 99.5) && (opening_percentage_curr >= 99.5)
             && ((state == Motor::State::RUNNING_REVERSE)
                 || (force_action == VentilateService::ForceAction::ForceClose)) ){
            end_stop_latched = false;
            return false;
        }
        if ( (state == Motor::State::RUNNING_FORWARD) || (state == Motor::State::RUNNING_REVERSE) ){
            drive(Motor::Action::STOP);
        }
        return true;
    }
    if ( state != Motor::State::IDLE ){
        return false;
    }
    if ( current >= 0.2 ){
        return false;
    }
    if ( (opening_percentage_set >= 100.0) && (opening_percentage_curr >= 90.0) ){
        hold_opening(100.0, stroke_time);
        end_stop_latched = true;
        LOG_INFO("End stop, set 100 percent.");
        return true;
    }
    if ( (opening_percentage_set <= 0.0) && (opening_percentage_curr <= 10.0) ){
        hold_opening(0.0, stroke_time);
        end_stop_latched = true;
        LOG_INFO("End stop, set 0 percent.");
        return true;
    }
    return false;
}

static void stop_force_and_hold(double opening){
    Config_t cfg;
    ConfigService::get_config(cfg);
    motor->set_ignore_stall(false);
    hold_opening(opening, cfg.motor_stroke_time);
    force_action = VentilateService::ForceAction::None;
    LOG_INFO("Force stop at %d percent.", (int)opening_percentage_curr);
}

bool VentilateService::init(){
    motor = MotorService::get_motor();
    if ( motor == nullptr ){
        LOG_ERROR("Can't get motor.");
        return false;
    }
    motor->init();
    force_action = VentilateService::ForceAction::None;
    {
        Config_t boot_config;
        ConfigService::get_config(boot_config);
        persisted_opening = opening_to_int((double)boot_config.last_opening_percentage);
    }
    VentilateService::align();
    return true;
}

bool VentilateService::align(){
    /* 开机关窗直到电流为 0，才把该位置当零点；不是看屏幕 0%. */
    Config_t config;
    ConfigService::get_config(config);
    int saved = config.last_opening_percentage;
    if ( saved < 0 ){
        saved = 0;
    }
    if ( saved > 100 ){
        saved = 100;
    }
    end_stop_latched = false;
    reset_align_track();
    align_stay_closed = false;
    restore_saved_opening = (saved > 0);
    ventilate_service_status = Status::ALIGN;
    int stroke = stroke_seconds(config.motor_stroke_time);
    /* 超时只作安全上限：必须关到机械限位。完成仍看电流 0 且电机已停. */
    align_budget_loops = 10U * (unsigned int)stroke + 80U;
    if ( align_budget_loops < 80U ){
        align_budget_loops = 80U;
    }
    int start_show = saved;
    if ( start_show < 1 ){
        start_show = 1;
    }
    motor->set_home_seek(true);
    motor->set_timer_cnt((int64_t)((double)start_show * 10.0 * (double)stroke + 0.5));
    opening_percentage_curr = (double)start_show;
    opening_percentage_set = (double)saved;
    motor->execute_action(Motor::Action::REVERSE);
    LOG_INFO("ALIGN home reverse, wait current zero. saved %d percent.", saved);
    return true;
}

bool VentilateService::home_after_stroke_change(){
    /* 手动改完 C1 后先关到 0，停在 0 位，后续按新行程走. */
    Config_t config;
    ConfigService::get_config(config);
    end_stop_latched = false;
    reset_align_track();
    restore_saved_opening = false;
    align_stay_closed = true;
    ventilate_service_status = Status::ALIGN;
    int stroke = stroke_seconds(config.motor_stroke_time);
    align_budget_loops = 10U * (unsigned int)stroke + 50U;
    motor->set_home_seek(true);
    motor->execute_action(Motor::Action::REVERSE);
    LOG_INFO("ALIGN home reverse start.");
    return true;
}

bool VentilateService::calibrate(){
    end_stop_latched = false;
    force_action = ForceAction::None;
    motor->set_home_seek(false);
    motor->set_calibrating(true);
    ventilate_service_status = Status::CALIBRATING_STAGE1;
    motor->execute_action(Motor::Action::REVERSE);
    LOG_INFO("Calibrate start, ignore timer stop.");
    return true;
}

static void VentilateService_eventloop(void *param){
    
    while ( true ){
        VentilateService::eventloop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void VentilateService::eventloop(){
    Config_t config;
    ConfigService::get_config(config);
    motor->eventloop();
    heal_negative_timer();
    stop_at_travel_limits(config);
    if ( force_action == VentilateService::ForceAction::None ){
        /* 无强制动作. */
        if ( ventilate_service_status == VentilateService::Status::ALIGN ){
            Motor::State state = Motor::State::IDLE;
            double current = 0.0;
            int64_t timer_cnt = 0;
            motor->get_state(state);
            motor->get_current(current);
            motor->get_timer_cnt(timer_cnt);
            if ( align_loop_cnt < 20000 ){
                align_loop_cnt++;
            }
            if ( state == Motor::State::ERROR_OVC ){
                LOG_ERROR("Over current protection.");
                motor->set_home_seek(false);
                motor->execute_action(Motor::Action::STOP);
                ventilate_service_status = VentilateService::Status::OCP;
            }else if ( align_wait_zero ){
                motor->execute_action(Motor::Action::STOP);
                motor->reset_timer();
                opening_percentage_curr = 0.0;
                if ( state == Motor::State::IDLE ){
                    LOG_INFO("ALIGN home done, power off.");
                    finish_align_home(config);
                }
            }else{
                if ( state != Motor::State::RUNNING_REVERSE ){
                    motor->execute_action(Motor::Action::REVERSE);
                    align_zero_curr_cnt = 0;
                }else if ( align_loop_cnt < 12U ){
                    align_zero_curr_cnt = 0;
                }else if ( current < 0.2 ){
                    if ( align_zero_curr_cnt < 20000 ){
                        align_zero_curr_cnt++;
                    }
                    if ( align_zero_curr_cnt >= 3U ){
                        motor->execute_action(Motor::Action::STOP);
                        align_wait_zero = true;
                        LOG_INFO("ALIGN current zero 3 times, cut power.");
                    }
                }else{
                    align_zero_curr_cnt = 0;
                }
            }
        }else if ( ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE1 ){
            /* 等待电机停止进入Stage2. */
            Motor::State state = Motor::State::IDLE;
            motor->get_state(state);
            if ( state == Motor::State::IDLE ){
                LOG_DEBUG("Enter stage 2.");
                ventilate_service_status = VentilateService::Status::CALIBRATING_STAGE2;
                calibrate_value = 0;
                motor->reset_timer();
                motor->execute_action(Motor::Action::FORWARD);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }else if ( state == Motor::State::ERROR_OVC ){
                motor->set_calibrating(false);
                ventilate_service_status = VentilateService::Status::OCP;
            }
        }else if ( ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE2 ){
            Motor::State state = Motor::State::IDLE;
            motor->get_state(state);
            if ( state == Motor::State::IDLE ){
                int64_t motor_timer_cnt = 0;
                motor->get_timer_cnt(motor_timer_cnt);
                if ( motor_timer_cnt < 0 ){
                    motor_timer_cnt = - motor_timer_cnt;
                }
                calibrate_value += motor_timer_cnt;
                ventilate_service_status = VentilateService::Status::CALIBRATING_STAGE3;
                motor->reset_timer();
                motor->execute_action(Motor::Action::REVERSE);
                LOG_DEBUG("Stage 2 cnt %d,Enter stage 3.", motor_timer_cnt);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }else if ( state == Motor::State::ERROR_OVC ){
                motor->set_calibrating(false);
                ventilate_service_status = VentilateService::Status::OCP;
            }
        }else if ( ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE3 ){
            Motor::State state = Motor::State::IDLE;
            motor->get_state(state);
            if ( state == Motor::State::IDLE ){
                int64_t motor_timer_cnt = 0;
                motor->get_timer_cnt(motor_timer_cnt);
                if ( motor_timer_cnt < 0 ){
                    motor_timer_cnt = - motor_timer_cnt;
                }
                calibrate_value += motor_timer_cnt;
                ventilate_service_status = VentilateService::Status::STOPPED;
                motor->set_calibrating(false);
                motor->reset_timer();
                motor->execute_action(Motor::Action::STOP);
                /* 往返计时单位约 1ms，有效行程 = (T1+T2)/2 秒. */
                unsigned int time = calibrate_value / 2000;
                if ( time < MOTOR_STROKE_TIME_MIN ){
                    time = MOTOR_STROKE_TIME_MIN;
                }else if ( time > MOTOR_STROKE_TIME_MAX ){
                    time = MOTOR_STROKE_TIME_MAX;
                }
                Config_t saved_config;
                ConfigService::get_config(saved_config);
                LOG_DEBUG("Stage 3 cnt %d, (%d) Save new motor stroke time %d.", (int)motor_timer_cnt, (int)calibrate_value, (int)time);
                saved_config.motor_stroke_time = (int)time;
                ConfigService::set_config(saved_config);
                ConfigService::store();
                /* 校准结束电机已在 0 位，按新行程重新计算开度. */
                opening_percentage_curr = 0;
                opening_percentage_set = 0;
            
            }else if ( state == Motor::State::ERROR_OVC ){
                motor->set_calibrating(false);
                ventilate_service_status = VentilateService::Status::OCP;
            }
        }else{
            // LOG_INFO("current %f, set %f", opening_percentage_curr, opening_percentage_set);
            int64_t motor_timer_cnt = 0;
            Motor::State motor_state = Motor::State::IDLE;
            double current = 0.0;
            motor->get_timer_cnt(motor_timer_cnt);
            motor->get_state(motor_state);
            motor->get_current(current);
            opening_percentage_curr = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time));
            opening_percentage_set = clamp_opening(opening_percentage_set);
            if ( try_snap_end_stop(motor_state, current, config.motor_stroke_time) ){
                /* 停稳后再记忆开度. */
            }else if ( opening_percentage_set >= 100.0 ){
                if ( opening_to_int(opening_percentage_curr) >= 100 ){
                    hold_opening(100.0, config.motor_stroke_time);
                }else{
                    drive(Motor::Action::FORWARD);
                }
            }else if ( opening_percentage_set <= 0.0 ){
                if ( opening_to_int(opening_percentage_curr) <= 0 ){
                    hold_opening(0.0, config.motor_stroke_time);
                }else{
                    drive(Motor::Action::REVERSE);
                }
            }else{
                double delta_percentage = opening_percentage_curr - opening_percentage_set;
                if ( delta_percentage > 0.5 ){
                    drive(Motor::Action::REVERSE);
                }else if ( delta_percentage < -0.5 ){
                    drive(Motor::Action::FORWARD);
                }else{
                    drive(Motor::Action::STOP);
                }
            }
        }
    }else{
        int64_t motor_timer_cnt = 0;
        Motor::State motor_state = Motor::State::IDLE;
        double current = 0.0;
        motor->get_timer_cnt(motor_timer_cnt);
        motor->get_state(motor_state);
        motor->get_current(current);
        double opening = calc_opening_percent(motor_timer_cnt, config.motor_stroke_time);
        opening_percentage_curr = clamp_opening(opening);
        if ( force_action == VentilateService::ForceAction::ForceOpen ){
            opening_percentage_set = 100.0;
        }else if ( force_action == VentilateService::ForceAction::ForceClose ){
            opening_percentage_set = 0.0;
        }

        if ( (force_action == VentilateService::ForceAction::ForceOpen) && (opening_to_int(opening) >= 100) ){
            stop_force_and_hold(100.0);
        }else if ( (force_action == VentilateService::ForceAction::ForceClose) && (opening_to_int(opening) <= 0) ){
            stop_force_and_hold(0.0);
        }else if ( motor_state == Motor::State::IDLE ){
            if ( force_action == VentilateService::ForceAction::ForceOpen ){
                if ( (current < 0.2) && (opening >= 90.0) ){
                    stop_force_and_hold(100.0);
                }else if ( opening_to_int(opening) >= 100 ){
                    stop_force_and_hold(100.0);
                }else{
                    /* 未开到位：继续开，避免退出强制后被自动模式改回. */
                    opening_percentage_set = 100.0;
                    motor->set_ignore_stall(true);
                    motor->execute_action(Motor::Action::FORWARD);
                }
            }else if ( force_action == VentilateService::ForceAction::ForceClose ){
                if ( (current < 0.2) && (opening <= 10.0) ){
                    stop_force_and_hold(0.0);
                }else if ( opening_to_int(opening) <= 0 ){
                    stop_force_and_hold(0.0);
                }else{
                    /* 未关到位：继续关，避免退出强制后自动模式又把百分比开上去. */
                    opening_percentage_set = 0.0;
                    motor->set_ignore_stall(true);
                    motor->execute_action(Motor::Action::REVERSE);
                }
            }else{
                force_action = VentilateService::ForceAction::None;
            }
        }else if ( motor_state == Motor::State::ERROR_OVC ){
            motor->set_ignore_stall(false);
            force_action = VentilateService::ForceAction::None;
        }
    }

    /* 自动模式：实测高于目标越多，开度越大.
     * 刚切换到自动时先保持当前开度，等温差算出的目标相对切入时变化后再跟调，避免一切换就被打到 0%. */
    if ( (config.working_mode == WorkingMode_Auto)
         && (force_action == VentilateService::ForceAction::None)
         && (ventilate_service_status != VentilateService::Status::ALIGN)
         && (in_calibrating() == false) ){
        double temp_curr = 0.0;
        if ( Env::get_temperature(temp_curr) == true ){
            double target = (double)config.target_central_temp / 10.0;
            double hyst = (double)config.temp_return_diff_positive / 10.0;
            int auto_opening = opening_from_temp_delta(temp_curr, target, hyst);
            if ( auto_hold_pending ){
                auto_hold_baseline = auto_opening;
                auto_hold_pending = false;
            }else if ( auto_opening != auto_hold_baseline ){
                auto_hold_baseline = auto_opening;
                if ( opening_to_int(opening_percentage_set) != auto_opening ){
                    LOG_INFO("Auto opening %d percent.", auto_opening);
                }
                opening_percentage_set = (double)auto_opening;
            }
        }
    }else{
        auto_hold_pending = false;
        auto_hold_baseline = -1;
    }

    try_persist_stopped_opening();
    if ( (ventilate_service_status != VentilateService::Status::ALIGN)
         && (ventilate_service_status != VentilateService::Status::CALIBRATING_STAGE1)
         && (ventilate_service_status != VentilateService::Status::CALIBRATING_STAGE2)
         && (ventilate_service_status != VentilateService::Status::CALIBRATING_STAGE3) ){
        int64_t live_cnt = 0;
        Motor::State live_state = Motor::State::IDLE;
        motor->get_timer_cnt(live_cnt);
        motor->get_state(live_state);
        int shown = opening_to_int(calc_opening_percent(live_cnt, config.motor_stroke_time));
        const bool leaving_zero = (force_action == VentilateService::ForceAction::ForceOpen)
            || (live_state == Motor::State::RUNNING_FORWARD)
            || (opening_percentage_set > 0.5);
        const bool leaving_hundred = (force_action == VentilateService::ForceAction::ForceClose)
            || (opening_percentage_set < 99.5)
            || (live_state == Motor::State::RUNNING_REVERSE);
        if ( (shown <= 0) && (leaving_zero == false) ){
            hold_opening(0.0, config.motor_stroke_time);
            force_action = VentilateService::ForceAction::None;
            motor->set_ignore_stall(false);
            end_stop_latched = true;
        }else if ( (shown >= 100) && (leaving_hundred == false) ){
            hold_opening(100.0, config.motor_stroke_time);
            force_action = VentilateService::ForceAction::None;
            motor->set_ignore_stall(false);
            end_stop_latched = true;
        }
    }
}

bool VentilateService::start(){
    if ( xTaskCreate(VentilateService_eventloop,"venti",256,nullptr,10,&task_handle_ventilate) != pdPASS ){
        LOG_ERROR("Venti task create failed.");
        return false;
    }
    return true;
}

bool VentilateService::set_opening_percentage(int value){
    if ( ventilate_service_status == VentilateService::Status::ALIGN || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE1 || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE2 || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE3  ){
        return false;
    }
    end_stop_latched = false;
    abort_opening_persist();
    opening_percentage_set = clamp_opening((double)value);
    return true;
}

bool VentilateService::get_opening_percentage(int &value){
    /* 开机找零过程屏幕保持 0%；零点确认后再按计时从 0% 往上走. */
    if ( ventilate_service_status == Status::ALIGN ){
        value = 0;
        return true;
    }
    Config_t config;
    ConfigService::get_config(config);
    int64_t motor_timer_cnt = 0;
    if ( motor != nullptr ){
        motor->get_timer_cnt(motor_timer_cnt);
    }
    double shown = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time));
    value = (int)(shown + 0.5);
    if ( value < 0 ){
        value = 0;
    }
    if ( value > 100 ){
        value = 100;
    }
    return true;
}

bool VentilateService::get_status(Status &status){
    status = ventilate_service_status;
    return true;
}

bool VentilateService::get_motor_state(Motor::State &state){
    return motor->get_state(state);
}

bool VentilateService::on_mode_changed(void){
    Config_t config;
    ConfigService::get_config(config);
    force_action = ForceAction::None;
    restore_saved_opening = false;
    align_stay_closed = false;
    motor->set_home_seek(false);
    motor->set_ignore_stall(false);
    motor->execute_action(Motor::Action::STOP);
    abort_opening_persist();
    end_stop_latched = false;

    int64_t motor_timer_cnt = 0;
    motor->get_timer_cnt(motor_timer_cnt);
    double opening = 0.0;
    if ( motor_timer_cnt > 0 ){
        opening = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time));
    }else{
        motor->reset_timer();
    }
    opening_percentage_curr = opening;
    opening_percentage_set = opening;
    auto_hold_pending = true;
    auto_hold_baseline = -1;
    if ( (ventilate_service_status != Status::ALIGN) && (in_calibrating() == false) ){
        ventilate_service_status = Status::STOPPED;
    }
    LOG_INFO("Mode change, keep opening %d percent.", opening_to_int(opening));
    return true;
}

bool VentilateService::force(ForceAction action){
    if ( action == ForceAction::ForceOpen || action == ForceAction::ForceClose ){
        if ( ventilate_service_status == Status::CALIBRATING_STAGE1
             || ventilate_service_status == Status::CALIBRATING_STAGE2
             || ventilate_service_status == Status::CALIBRATING_STAGE3 ){
            return false;
        }
        if ( ventilate_service_status == Status::ALIGN ){
            restore_saved_opening = false;
            align_stay_closed = false;
            motor->set_home_seek(false);
            ventilate_service_status = Status::STOPPED;
        }
        if ( force_action == action ){
            motor->set_ignore_stall(true);
            if ( action == ForceAction::ForceOpen ){
                if ( opening_to_int(opening_percentage_curr) < 100 ){
                    int64_t cnt = 0;
                    motor->get_timer_cnt(cnt);
                    if ( cnt <= 0 ){
                        motor->reset_timer();
                        opening_percentage_curr = 0.0;
                    }
                    opening_percentage_set = 100.0;
                    motor->execute_action(Motor::Action::FORWARD);
                }
            }else if ( (action == ForceAction::ForceClose)
                       && (opening_to_int(opening_percentage_curr) > 0) ){
                motor->execute_action(Motor::Action::REVERSE);
            }
            return true;
        }
        end_stop_latched = false;
        abort_opening_persist();
        force_action = action;
        motor->set_ignore_stall(true);
        if ( force_action == ForceAction::ForceOpen ){
            if ( opening_to_int(opening_percentage_curr) >= 100 ){
                stop_force_and_hold(100.0);
            }else{
                int64_t cnt = 0;
                motor->get_timer_cnt(cnt);
                if ( cnt <= 0 ){
                    motor->reset_timer();
                    opening_percentage_curr = 0.0;
                }
                opening_percentage_set = 100.0;
                motor->execute_action(Motor::Action::FORWARD);
            }
        }else{
            if ( opening_to_int(opening_percentage_curr) <= 0 ){
                stop_force_and_hold(0.0);
            }else{
                opening_percentage_set = 0.0;
                motor->execute_action(Motor::Action::REVERSE);
            }
        }
    }else if ( action == ForceAction::None ){
        Config_t config;
        ConfigService::get_config(config);
        force_action = ForceAction::None;
        restore_saved_opening = false;
        align_stay_closed = false;
        motor->set_home_seek(false);
        motor->set_ignore_stall(false);
        int64_t motor_timer_cnt = 0;
        motor->execute_action(Motor::Action::STOP);
        motor->get_timer_cnt(motor_timer_cnt);
        ventilate_service_status = VentilateService::Status::STOPPED;
        if ( motor_timer_cnt <= 0 ){
            motor->reset_timer();
            opening_percentage_curr = 0.0;
            opening_percentage_set = 0.0;
        }else{
            opening_percentage_curr = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time));
            opening_percentage_set = opening_percentage_curr;
            if ( opening_to_int(opening_percentage_curr) <= 0 ){
                motor->reset_timer();
                opening_percentage_curr = 0.0;
                opening_percentage_set = 0.0;
            }
        }
    }
    return true;
}