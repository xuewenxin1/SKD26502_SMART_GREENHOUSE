#include "ventilate_service.hpp"
#include "motor_service.hpp"
#include "console_logger.h"
#include "config.hpp"
#include "end_stop_policy.hpp"
#include "env.hpp"
#include <cmath>
#include <cstdint>
#include "FreeRTOS.h"
#include "task.h"

static Motor *motor;
static VentilateService::Status ventilate_service_status;
static VentilateService::ForceAction force_action = VentilateService::ForceAction::None;
static double opening_percentage_curr = 0;
static double opening_percentage_set = 0;
/* MQTT 精确圈数：目标计时 = 圈数×C2×1000（约 1ms/tick），按时间到位. */
static bool target_by_timer = false;
static int64_t target_timer_cnt = 0;
static int32_t calibrate_value = 0;
static bool align_stay_closed = false;
static bool restore_saved_opening = false;
static int persisted_opening = -1;
static int persisted_timer_sec = -1;
static int pending_persist_opening = -1;
static unsigned int persist_idle_loops = 0;
/* 主循环约 100ms，停稳 1s 再写 Flash；计时只按整秒存. */
static const unsigned int PERSIST_IDLE_LOOPS = 10;
static unsigned int align_loop_cnt = 0;
static unsigned int align_budget_loops = 300;
static unsigned int align_zero_curr_cnt = 0;
static bool align_wait_zero = false;
static bool end_stop_latched = false;
/* 真正反转/正转过之后才允许终点吸附清零或拉满；刚下发 0/最大圈数时不能瞬间跳变. */
static bool close_run_latched = false;
static bool open_run_latched = false;
/* 强制开/关期间是否真正转过；电流≈0 限位要等转过之后再认. */
static bool force_motion_seen = false;
static bool auto_hold_pending = false;
static int auto_hold_baseline = -1;
/* 自动模式下手动按键后锁定，直到再次切入自动才恢复温控. */
static bool auto_manual_override = false;
/* 定时：到点只下发一次（同小时内不重复）；手动打断只停当前动作. */
static int timing_done_hour[4] = {-1, -1, -1, -1};
static int timing_active_slot = -1;
static bool clock_valid = false;
static uint8_t clock_hour = 0;
static uint8_t clock_minute = 0;
static TaskHandle_t task_handle_ventilate = nullptr;

static double clamp_opening(double value);
static void abort_opening_persist(void);
static void try_persist_stopped_opening(void);
static void note_manual_override(void);
static void timing_mode_eventloop(const Config_t &config);
static bool in_calibrating(void);
static int effective_turn_seconds(int turn_seconds);

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

/* 自动模式：用浮点℃比较 P5/P6（22.8≤23.0 也要关）.
 * ≥P5 → 全开；≤P6 → 全关；中间保持上次开/关结论.
 * 是否到位看行程计时/开度，不看“整数圈”（未满 1 圈会误判成 0）. */
static void normalize_vent_limits(double &lo, double &hi){
    if ( hi < lo ){
        double tmp = hi;
        hi = lo;
        lo = tmp;
    }
}

static bool auto_nearly_closed(void){
    int64_t cnt = 0;
    if ( motor != nullptr ){
        motor->get_timer_cnt(cnt);
    }
    return (cnt <= 500); /* 约 0.5s 内视为已关到位. */
}

static bool auto_nearly_open(int stroke, int turn_sec){
    if ( (stroke <= 0) || (turn_sec <= 0) ){
        return false;
    }
    int64_t cnt = 0;
    if ( motor != nullptr ){
        motor->get_timer_cnt(cnt);
    }
    int64_t full = (int64_t)stroke * (int64_t)turn_sec * 1000LL;
    if ( full <= 0 ){
        return false;
    }
    /* 满行程差约 0.5s 内视为已开到位. */
    return (cnt + 500 >= full);
}

/* 按温度下发自动开/关；force=true（刚切入自动）时超限必重新下发. */
static void apply_auto_by_temp(double temp_curr, double lower, double upper,
                               int stroke, int turn_sec, bool force){
    double lo = lower;
    double hi = upper;
    normalize_vent_limits(lo, hi);
    if ( stroke < MOTOR_STROKE_TURNS_MIN ){
        stroke = MOTOR_STROKE_TURNS_MIN;
    }else if ( stroke > MOTOR_STROKE_TURNS_MAX ){
        stroke = MOTOR_STROKE_TURNS_MAX;
    }
    turn_sec = effective_turn_seconds(turn_sec);
    const int t10 = (int)(temp_curr * 10.0);
    const int lo10 = (int)(lo * 10.0);
    const int hi10 = (int)(hi * 10.0);

    if ( temp_curr >= hi ){
        /* 切入自动或未开到位：必须下发全开. */
        const bool need = force
            || (auto_hold_baseline != stroke)
            || (auto_nearly_open(stroke, turn_sec) == false);
        auto_hold_baseline = stroke;
        if ( need ){
            VentilateService::set_target_turns(stroke);
            LOG_INFO("Auto open %d turns (P5=%d.%d T=%d.%d)%s.",
                     stroke, hi10 / 10, hi10 % 10, t10 / 10, (t10 < 0 ? -t10 : t10) % 10,
                     force ? " enter" : "");
        }
        return;
    }
    if ( temp_curr <= lo ){
        /* 切入自动或未关到位：必须下发全关. */
        const bool need = force
            || (auto_hold_baseline != 0)
            || (auto_nearly_closed() == false);
        auto_hold_baseline = 0;
        if ( need ){
            VentilateService::set_target_turns(0);
            LOG_INFO("Auto close 0 turns (P6=%d.%d T=%d.%d)%s.",
                     lo10 / 10, lo10 % 10, t10 / 10, (t10 < 0 ? -t10 : t10) % 10,
                     force ? " enter" : "");
        }
        return;
    }
    /* 中间温区：记住当前偏开/偏关，不改目标. */
    if ( (auto_hold_baseline < 0) || force ){
        if ( auto_nearly_closed() ){
            auto_hold_baseline = 0;
        }else if ( auto_nearly_open(stroke, turn_sec) ){
            auto_hold_baseline = stroke;
        }else if ( opening_percentage_curr >= 50.0 ){
            auto_hold_baseline = stroke;
        }else{
            auto_hold_baseline = 0;
        }
        LOG_INFO("Auto hold mid T=%d.%d P6=%d P5=%d base %d.",
                 t10 / 10, (t10 < 0 ? -t10 : t10) % 10, lo10 / 10, hi10 / 10, auto_hold_baseline);
    }
}

static void abort_opening_persist(void){
    pending_persist_opening = -1;
    persist_idle_loops = 0;
}

static void note_manual_override(void){
    Config_t cfg;
    ConfigService::get_config(cfg);
    if ( cfg.working_mode == WorkingMode_Auto ){
        auto_manual_override = true;
        auto_hold_pending = false;
        LOG_INFO("Auto manual override, stop temp control.");
    }
    if ( (cfg.working_mode == WorkingMode_Timing) && (timing_active_slot >= 0) ){
        timing_active_slot = -1;
    }
}

static int turns_from_opening_percent(int percent, int stroke){
    if ( percent <= 0 ){
        return 0;
    }
    if ( percent >= 100 ){
        return stroke;
    }
    int turns = (percent * stroke + 50) / 100;
    if ( turns < 0 ){
        turns = 0;
    }
    if ( turns > stroke ){
        turns = stroke;
    }
    return turns;
}

static void timing_mode_eventloop(const Config_t &config){
    if ( clock_valid == false ){
        return;
    }
    if ( (ventilate_service_status == VentilateService::Status::ALIGN)
         || in_calibrating() ){
        return;
    }
    const int hour = (int)clock_hour;
    int stroke = config.motor_stroke_time;
    if ( stroke < MOTOR_STROKE_TURNS_MIN ){
        stroke = MOTOR_STROKE_TURNS_MIN;
    }else if ( stroke > MOTOR_STROKE_TURNS_MAX ){
        stroke = MOTOR_STROKE_TURNS_MAX;
    }

    for ( int i = 0; i < 4; i++ ){
        const int slot_hour = config.ventilator_timing_config[i].time;
        if ( (slot_hour < 0) || (slot_hour > 23) ){
            continue;
        }
        /* 离开设定小时后清标记，下次到点可再触发. */
        if ( hour != slot_hour ){
            if ( timing_done_hour[i] == slot_hour ){
                timing_done_hour[i] = -1;
            }
            continue;
        }
        if ( timing_done_hour[i] == hour ){
            continue;
        }
        const int pct = config.ventilator_timing_config[i].opening_percentage;
        const int turns = turns_from_opening_percent(pct, stroke);
        timing_done_hour[i] = hour;
        timing_active_slot = i;
        LOG_INFO("Timing D%d hour %d -> %d%% (%d turns).",
                 i + 1, hour, pct, turns);
        VentilateService::set_target_turns(turns);
        break; /* 同一设定小时只发一次，避免连发. */
    }

    if ( (timing_active_slot >= 0) && (target_by_timer == false) ){
        timing_active_slot = -1;
    }
}

static void try_persist_stopped_opening(void){
    if ( ventilate_service_status != VentilateService::Status::STOPPED ){
        abort_opening_persist();
        return;
    }
    if ( (force_action != VentilateService::ForceAction::None)
         && (force_action != VentilateService::ForceAction::ForceStop) ){
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
    int64_t timer_cnt = 0;
    motor->get_timer_cnt(timer_cnt);
    if ( timer_cnt < 0 ){
        timer_cnt = 0;
    }
    int timer_sec = (int)(timer_cnt / 1000LL);
    if ( timer_sec > 2000000 ){
        timer_sec = 2000000;
    }
    if ( (value == persisted_opening) && (timer_sec == persisted_timer_sec) ){
        abort_opening_persist();
        return;
    }
    if ( value != pending_persist_opening ){
        pending_persist_opening = value;
        persist_idle_loops = 0;
    }
    persist_idle_loops++;
    if ( persist_idle_loops < PERSIST_IDLE_LOOPS ){
        return;
    }
    persist_idle_loops = 0;
    Config_t saved;
    ConfigService::get_config(saved);
    saved.last_opening_percentage = value;
    saved.last_motor_timer_cnt = timer_sec * 1000;
    ConfigService::set_config(saved);
    ConfigService::request_store();
    persisted_opening = value;
    persisted_timer_sec = timer_sec;
    pending_persist_opening = -1;
    LOG_INFO("Save opening %d percent, timer %d s after motor stop.",
             value, timer_sec);
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

/* C2 越界时按默认秒数计时. */
static int effective_turn_seconds(int turn_seconds){
    if ( (turn_seconds < MOTOR_TURN_SECONDS_MIN) || (turn_seconds > MOTOR_TURN_SECONDS_MAX) ){
        return MOTOR_TURN_SECONDS_DEFAULT;
    }
    return turn_seconds;
}

/* C1 圈数 × C2 秒/圈 = 满行程秒数. */
static int stroke_seconds(int stroke_turns, int turn_seconds){
    int turns = stroke_turns;
    if ( turns < MOTOR_STROKE_TURNS_MIN ){
        turns = MOTOR_STROKE_TURNS_MIN;
    }else if ( turns > MOTOR_STROKE_TURNS_MAX ){
        turns = MOTOR_STROKE_TURNS_MAX;
    }
    return turns * effective_turn_seconds(turn_seconds);
}

static double calc_opening_percent(int64_t motor_timer_cnt, int stroke_turns, int turn_seconds){
    double stroke = (double)stroke_seconds(stroke_turns, turn_seconds);
    return ((double)motor_timer_cnt / 10.0) / stroke;
}

static void hold_opening(double opening, int stroke_turns, int turn_seconds){
    opening = clamp_opening(opening);
    motor->execute_action(Motor::Action::STOP);
    target_by_timer = false;
    if ( opening <= 0.0 ){
        motor->reset_timer();
        opening_percentage_curr = 0.0;
        close_run_latched = false;
    }else{
        int stroke = stroke_seconds(stroke_turns, turn_seconds);
        int64_t cnt = (int64_t)(opening * 10.0 * (double)stroke + 0.5);
        motor->set_timer_cnt(cnt);
        opening_percentage_curr = opening;
        if ( opening >= 100.0 ){
            open_run_latched = false;
        }
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
    if ( action == Motor::Action::FORWARD ){
        close_run_latched = false;
        if ( opening_percentage_set >= 99.5 ){
            open_run_latched = true;
        }
    }else if ( action == Motor::Action::REVERSE ){
        open_run_latched = false;
        if ( opening_percentage_set <= 0.5 ){
            close_run_latched = true;
        }
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
    const int shown = opening_to_int(calc_opening_percent(cnt, config.motor_stroke_time, config.motor_turn_seconds));
    const bool force_open = (force_action == VentilateService::ForceAction::ForceOpen);
    const bool opening_now = (state == Motor::State::RUNNING_FORWARD)
        || force_open
        || (opening_percentage_set > 0.5);
    if ( shown <= 0 ){
        if ( (force_open == false) && (state != Motor::State::RUNNING_FORWARD)
             && (opening_percentage_set <= 0.5) ){
            const bool first_cut = (end_stop_latched == false) || (state != Motor::State::IDLE);
            hold_opening(0.0, config.motor_stroke_time, config.motor_turn_seconds);
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
        || close_run_latched
        || ((opening_percentage_set <= 0.5) && (opening_now == false)
            && (state == Motor::State::RUNNING_REVERSE));
    const bool aligning = (ventilate_service_status == VentilateService::Status::ALIGN);
    const bool idle = (state == Motor::State::IDLE);
    if ( EndStopPolicy::should_stop_close(force_open, closing, aligning, align_loop_cnt, idle, shown, opening_now) ){
        hold_opening(0.0, config.motor_stroke_time, config.motor_turn_seconds);
        force_action = VentilateService::ForceAction::None;
        motor->set_ignore_stall(false);
        end_stop_latched = true;
        LOG_INFO("Stop at 0 percent.");
    }else if ( (state == Motor::State::RUNNING_FORWARD) && (shown >= 100) ){
        if ( force_action != VentilateService::ForceAction::ForceClose ){
            hold_opening(100.0, config.motor_stroke_time, config.motor_turn_seconds);
            force_action = VentilateService::ForceAction::None;
            motor->set_ignore_stall(false);
            end_stop_latched = true;
            LOG_INFO("Stop at 100 percent.");
        }
    }
}

static bool near_end_for_current_snap(bool want_open, int stroke_turns, int turn_seconds){
    /* 仅在接近目标/端点时认电流≈0 为限位，避免行程中途误停并吸附开度. */
    int64_t cnt = 0;
    motor->get_timer_cnt(cnt);
    int sec = effective_turn_seconds(turn_seconds);
    int64_t near = (int64_t)sec * 1000LL; /* 约 1 圈 */
    if ( near < 1000LL ){
        near = 1000LL;
    }
    double shown = calc_opening_percent(cnt, stroke_turns, turn_seconds);
    if ( want_open ){
        if ( shown >= 90.0 ){
            return true;
        }
        if ( target_by_timer ){
            int64_t d = cnt - target_timer_cnt;
            if ( d < 0 ){
                d = -d;
            }
            return d <= near;
        }
        return false;
    }
    if ( shown <= 10.0 ){
        return true;
    }
    if ( target_by_timer ){
        int64_t d = cnt - target_timer_cnt;
        if ( d < 0 ){
            d = -d;
        }
        return d <= near;
    }
    return false;
}

static bool try_snap_end_stop(Motor::State state, double current, int stroke_turns, int turn_seconds){
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
    /* 电流≈0 判机械限位：必须已接近目标/端点，否则中途假零流会提前吸附. */
    if ( (open_run_latched != false)
         && (opening_percentage_set >= 99.5)
         && near_end_for_current_snap(true, stroke_turns, turn_seconds) ){
        hold_opening(100.0, stroke_turns, turn_seconds);
        end_stop_latched = true;
        LOG_INFO("End stop (current), set 100 percent.");
        return true;
    }
    if ( (close_run_latched != false)
         && (opening_percentage_set <= 0.5)
         && near_end_for_current_snap(false, stroke_turns, turn_seconds) ){
        hold_opening(0.0, stroke_turns, turn_seconds);
        end_stop_latched = true;
        LOG_INFO("End stop (current), set 0 percent.");
        return true;
    }
    return false;
}

static void stop_force_and_hold(double opening){
    Config_t cfg;
    ConfigService::get_config(cfg);
    motor->set_ignore_stall(false);
    force_motion_seen = false;
    hold_opening(opening, cfg.motor_stroke_time, cfg.motor_turn_seconds);
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
        int saved = boot_config.last_opening_percentage;
        if ( saved < 0 ){
            saved = 0;
        }
        if ( saved > 100 ){
            saved = 100;
        }
        persisted_opening = saved;
        persisted_timer_sec = boot_config.last_motor_timer_cnt / 1000;
        if ( persisted_timer_sec < 0 ){
            persisted_timer_sec = 0;
        }
        /* 上电不主动转、不归零：恢复上次停稳的行程计时与开度，电机保持不动. */
        end_stop_latched = false;
        restore_saved_opening = false;
        align_stay_closed = false;
        motor->set_home_seek(false);
        motor->execute_action(Motor::Action::STOP);
        int64_t cnt = (int64_t)boot_config.last_motor_timer_cnt;
        if ( cnt < 0 ){
            cnt = 0;
        }
        motor->set_timer_cnt(cnt);
        double opening = clamp_opening(calc_opening_percent(cnt, boot_config.motor_stroke_time, boot_config.motor_turn_seconds));
        opening_percentage_curr = opening;
        opening_percentage_set = opening;
        if ( (opening_to_int(opening) <= 0) || (opening_to_int(opening) >= 100) ){
            end_stop_latched = true;
        }
        /* 自动模式上电先记住当前温控结论，不立刻改开度去转电机. */
        auto_hold_pending = true;
        auto_hold_baseline = -1;
        ventilate_service_status = Status::STOPPED;
        int turns_now = 0;
        int sec_boot = effective_turn_seconds(boot_config.motor_turn_seconds);
        if ( sec_boot > 0 ){
            turns_now = (int)(cnt / (1000LL * (int64_t)sec_boot));
            if ( turns_now > boot_config.motor_stroke_time ){
                turns_now = boot_config.motor_stroke_time;
            }
        }
        LOG_INFO("Boot keep timer %d ms, opening %d percent, turns %d, no move.",
                 (int)cnt, opening_to_int(opening), turns_now);
    }
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
    int stroke = stroke_seconds(config.motor_stroke_time, config.motor_turn_seconds);
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
    int stroke = stroke_seconds(config.motor_stroke_time, config.motor_turn_seconds);
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
                /* 往返计时单位约 1ms，有效行程秒 = (T1+T2)/2，再按 C2 秒/圈换算圈数. */
                unsigned int time_sec = calibrate_value / 2000;
                Config_t cal_cfg;
                ConfigService::get_config(cal_cfg);
                unsigned int sec_per = (unsigned int)effective_turn_seconds(cal_cfg.motor_turn_seconds);
                unsigned int turns = (time_sec + sec_per / 2U) / sec_per;
                if ( turns < (unsigned int)MOTOR_STROKE_TURNS_MIN ){
                    turns = (unsigned int)MOTOR_STROKE_TURNS_MIN;
                }else if ( turns > (unsigned int)MOTOR_STROKE_TURNS_MAX ){
                    turns = (unsigned int)MOTOR_STROKE_TURNS_MAX;
                }
                Config_t saved_config;
                ConfigService::get_config(saved_config);
                LOG_INFO("Calibrate done, stroke %u turns (%u sec).", turns, time_sec);
                LOG_DEBUG("Stage 3 cnt %d, (%d) Save new motor stroke turns %u.", (int)motor_timer_cnt, (int)calibrate_value, turns);
                saved_config.motor_stroke_time = (int)turns;
                ConfigService::set_config(saved_config);
                ConfigService::request_store();
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
            opening_percentage_curr = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time, config.motor_turn_seconds));
            opening_percentage_set = clamp_opening(opening_percentage_set);
            if ( try_snap_end_stop(motor_state, current, config.motor_stroke_time, config.motor_turn_seconds) ){
                /* 停稳后再记忆开度. */
            }else if ( target_by_timer ){
                /* 精确圈数（含 0/最大）：一律按目标计时走，避免 0%/100% 分支瞬间吸附. */
                int sec = effective_turn_seconds(config.motor_turn_seconds);
                int64_t deadband = (int64_t)sec * 100LL; /* 约 0.1 圈 */
                if ( deadband < 50LL ){
                    deadband = 50LL;
                }
                int64_t near = (int64_t)sec * 1000LL; /* 约 1 圈内才允许电流限位 */
                if ( near < 1000LL ){
                    near = 1000LL;
                }
                int64_t delta = motor_timer_cnt - target_timer_cnt;
                int64_t abs_delta = (delta < 0) ? -delta : delta;
                /* 远离目标时忽略假零流；接近目标后恢复，便于机械限位吸附. */
                motor->set_ignore_stall(abs_delta > near);
                if ( delta > deadband ){
                    drive(Motor::Action::REVERSE);
                }else if ( delta < -deadband ){
                    drive(Motor::Action::FORWARD);
                }else{
                    motor->set_ignore_stall(false);
                    motor->execute_action(Motor::Action::STOP);
                    motor->set_timer_cnt(target_timer_cnt);
                    opening_percentage_curr = clamp_opening(
                        calc_opening_percent(target_timer_cnt, config.motor_stroke_time, config.motor_turn_seconds));
                    opening_percentage_set = opening_percentage_curr;
                    target_by_timer = false;
                    close_run_latched = false;
                    open_run_latched = false;
                    ventilate_service_status = VentilateService::Status::STOPPED;
                    /* 停稳约 1s 后再按整秒落盘，避免刚停就擦 Flash. */
                    persisted_opening = -1;
                    persisted_timer_sec = -1;
                    pending_persist_opening = -1;
                    LOG_INFO("Turns target reached, timer %d (≈%d s).",
                             (int)target_timer_cnt, (int)(target_timer_cnt / 1000LL));
                }
            }else if ( opening_percentage_set >= 100.0 ){
                if ( opening_to_int(opening_percentage_curr) >= 100 ){
                    hold_opening(100.0, config.motor_stroke_time, config.motor_turn_seconds);
                }else{
                    drive(Motor::Action::FORWARD);
                }
            }else if ( opening_percentage_set <= 0.0 ){
                if ( opening_to_int(opening_percentage_curr) <= 0 ){
                    hold_opening(0.0, config.motor_stroke_time, config.motor_turn_seconds);
                }else{
                    drive(Motor::Action::REVERSE);
                }
            }else{
                /* 到位死区按约 0.1 圈换算成开度%. */
                int stroke_turns = config.motor_stroke_time;
                if ( stroke_turns < MOTOR_STROKE_TURNS_MIN ){
                    stroke_turns = MOTOR_STROKE_TURNS_MIN;
                }
                double deadband = 10.0 / (double)stroke_turns;
                if ( deadband < 0.05 ){
                    deadband = 0.05;
                }
                if ( deadband > 0.5 ){
                    deadband = 0.5;
                }
                double delta_percentage = opening_percentage_curr - opening_percentage_set;
                if ( delta_percentage > deadband ){
                    drive(Motor::Action::REVERSE);
                }else if ( delta_percentage < -deadband ){
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
        double opening = calc_opening_percent(motor_timer_cnt, config.motor_stroke_time, config.motor_turn_seconds);
        opening_percentage_curr = clamp_opening(opening);
        if ( force_action == VentilateService::ForceAction::ForceOpen ){
            target_by_timer = false;
            opening_percentage_set = 100.0;
            open_run_latched = true;
        }else if ( force_action == VentilateService::ForceAction::ForceClose ){
            target_by_timer = false;
            opening_percentage_set = 0.0;
            close_run_latched = true;
        }

        if ( (motor_state == Motor::State::RUNNING_FORWARD)
             || (motor_state == Motor::State::RUNNING_REVERSE) ){
            force_motion_seen = true;
        }

        if ( (force_action == VentilateService::ForceAction::ForceOpen) && (opening_to_int(opening) >= 100) ){
            stop_force_and_hold(100.0);
        }else if ( (force_action == VentilateService::ForceAction::ForceClose) && (opening_to_int(opening) <= 0) ){
            stop_force_and_hold(0.0);
        }else if ( motor_state == Motor::State::IDLE ){
            if ( force_action == VentilateService::ForceAction::ForceOpen ){
                /* 转过之后电流≈0：须接近开端才认限位. */
                if ( (force_motion_seen != false) && (current < 0.2)
                     && near_end_for_current_snap(true, config.motor_stroke_time, config.motor_turn_seconds) ){
                    stop_force_and_hold(100.0);
                }else if ( opening_to_int(opening) >= 100 ){
                    stop_force_and_hold(100.0);
                }else{
                    opening_percentage_set = 100.0;
                    open_run_latched = true;
                    motor->set_ignore_stall(true);
                    motor->execute_action(Motor::Action::FORWARD);
                }
            }else if ( force_action == VentilateService::ForceAction::ForceClose ){
                if ( (force_motion_seen != false) && (current < 0.2)
                     && near_end_for_current_snap(false, config.motor_stroke_time, config.motor_turn_seconds) ){
                    stop_force_and_hold(0.0);
                }else if ( opening_to_int(opening) <= 0 ){
                    stop_force_and_hold(0.0);
                }else{
                    opening_percentage_set = 0.0;
                    close_run_latched = true;
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

    /* 自动模式：P5 上限全开、P6 下限全关，中间保持；浮点℃比较.
     * 切到自动若已超限立即动作；手动按键后 auto_manual_override 直到再次切入自动. */
    if ( (config.working_mode == WorkingMode_Auto)
         && (auto_manual_override == false)
         && (force_action == VentilateService::ForceAction::None)
         && (ventilate_service_status != VentilateService::Status::ALIGN)
         && (in_calibrating() == false) ){
        double temp_curr = 0.0;
        if ( Env::get_temperature(temp_curr) == true ){
            double lower = (double)config.temp_vent_lower_limit / 10.0;
            double upper = (double)config.temp_vent_upper_limit / 10.0;
            const bool force_enter = auto_hold_pending;
            if ( auto_hold_pending ){
                auto_hold_pending = false;
                auto_manual_override = false;
            }
            apply_auto_by_temp(temp_curr, lower, upper,
                               config.motor_stroke_time, config.motor_turn_seconds,
                               force_enter);
        }
    }else if ( (config.working_mode == WorkingMode_Timing)
                && (force_action == VentilateService::ForceAction::None)
                && (ventilate_service_status != VentilateService::Status::ALIGN)
                && (in_calibrating() == false) ){
        auto_hold_pending = false;
        auto_hold_baseline = -1;
        auto_manual_override = false;
        timing_mode_eventloop(config);
    }else if ( config.working_mode != WorkingMode_Auto ){
        auto_hold_pending = false;
        auto_hold_baseline = -1;
        auto_manual_override = false;
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
        int shown = opening_to_int(calc_opening_percent(live_cnt, config.motor_stroke_time, config.motor_turn_seconds));
        const bool leaving_zero = (force_action == VentilateService::ForceAction::ForceOpen)
            || (live_state == Motor::State::RUNNING_FORWARD)
            || (opening_percentage_set > 0.5);
        const bool leaving_hundred = (force_action == VentilateService::ForceAction::ForceClose)
            || (opening_percentage_set < 99.5)
            || (live_state == Motor::State::RUNNING_REVERSE);
        if ( (shown <= 0) && (leaving_zero == false) ){
            hold_opening(0.0, config.motor_stroke_time, config.motor_turn_seconds);
            force_action = VentilateService::ForceAction::None;
            motor->set_ignore_stall(false);
            end_stop_latched = true;
        }else if ( (shown >= 100) && (leaving_hundred == false) ){
            hold_opening(100.0, config.motor_stroke_time, config.motor_turn_seconds);
            force_action = VentilateService::ForceAction::None;
            motor->set_ignore_stall(false);
            end_stop_latched = true;
        }
    }
}

bool VentilateService::start(){
    if ( xTaskCreate(VentilateService_eventloop,"venti",256,nullptr,3,&task_handle_ventilate) != pdPASS ){
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
    target_by_timer = false;
    opening_percentage_set = clamp_opening((double)value);
    close_run_latched = false;
    open_run_latched = false;
    return true;
}

bool VentilateService::set_target_turns(int turns){
    if ( ventilate_service_status == VentilateService::Status::ALIGN || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE1 || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE2 || ventilate_service_status == VentilateService::Status::CALIBRATING_STAGE3  ){
        return false;
    }
    Config_t cfg;
    ConfigService::get_config(cfg);
    int stroke = cfg.motor_stroke_time;
    if ( stroke < MOTOR_STROKE_TURNS_MIN ){
        stroke = MOTOR_STROKE_TURNS_MIN;
    }else if ( stroke > MOTOR_STROKE_TURNS_MAX ){
        stroke = MOTOR_STROKE_TURNS_MAX;
    }
    int sec = cfg.motor_turn_seconds;
    sec = effective_turn_seconds(sec);
    if ( turns < 0 ){
        turns = 0;
    }
    if ( turns > stroke ){
        turns = stroke;
    }
    /* 行程时间 = 目标圈数 × C2(秒/圈)；计时约 1ms/tick. */
    int64_t full_cnt = (int64_t)stroke * (int64_t)sec * 1000LL;
    target_timer_cnt = (int64_t)turns * (int64_t)sec * 1000LL;
    if ( turns >= stroke ){
        target_timer_cnt = full_cnt;
        opening_percentage_set = 100.0;
    }else if ( turns <= 0 ){
        target_timer_cnt = 0;
        opening_percentage_set = 0.0;
    }else if ( full_cnt > 0 ){
        opening_percentage_set = clamp_opening(((double)target_timer_cnt * 100.0) / (double)full_cnt);
    }else{
        opening_percentage_set = 0.0;
    }
    end_stop_latched = false;
    abort_opening_persist();
    /* 新目标：先清 latch，真正转动后再允许终点吸附. */
    close_run_latched = false;
    open_run_latched = false;
    target_by_timer = true;
    LOG_INFO("Set target turns %d -> %d s (C2=%d), timer %d, opening %.2f%%.",
             turns, turns * sec, sec, (int)target_timer_cnt, opening_percentage_set);
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
    double shown = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time, config.motor_turn_seconds));
    value = (int)(shown + 0.5);
    if ( value < 0 ){
        value = 0;
    }
    if ( value > 100 ){
        value = 100;
    }
    return true;
}

bool VentilateService::get_current_turns(int &turns){
    Config_t config;
    ConfigService::get_config(config);
    int stroke = config.motor_stroke_time;
    if ( stroke < MOTOR_STROKE_TURNS_MIN ){
        stroke = MOTOR_STROKE_TURNS_MIN;
    }else if ( stroke > MOTOR_STROKE_TURNS_MAX ){
        stroke = MOTOR_STROKE_TURNS_MAX;
    }
    int sec = config.motor_turn_seconds;
    sec = effective_turn_seconds(sec);

    if ( ventilate_service_status == Status::ALIGN ){
        turns = 0;
        return true;
    }

    int opening = 0;
    get_opening_percentage(opening);
    if ( opening <= 0 ){
        turns = 0;
        return true;
    }
    if ( opening >= 100 ){
        turns = stroke;
        return true;
    }

    int64_t cnt = 0;
    if ( motor != nullptr ){
        motor->get_timer_cnt(cnt);
    }
    if ( cnt < 0 ){
        cnt = 0;
    }
    /* timer_cnt 约 1ms/tick；已转秒=cnt/1000。整数圈=秒/C2，未满 1 圈为 0.
     * 例：C1=10 C2=25 → 满行程 250s 才到 10 圈，不是几秒. */
    int t = (int)(cnt / (1000LL * (int64_t)sec));
    if ( t > stroke ){
        t = stroke;
    }
    if ( t < 0 ){
        t = 0;
    }
    turns = t;
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
        opening = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time, config.motor_turn_seconds));
    }else{
        motor->reset_timer();
    }
    opening_percentage_curr = opening;
    opening_percentage_set = opening;
    /* 切入自动：清手动锁定，超 P5/P6 立即全开/全关. */
    auto_manual_override = false;
    if ( config.working_mode == WorkingMode_Auto ){
        auto_hold_pending = true;
        auto_hold_baseline = -1;
        double temp_curr = 0.0;
        if ( (ventilate_service_status != Status::ALIGN)
             && (in_calibrating() == false)
             && (Env::get_temperature(temp_curr) == true) ){
            apply_auto_by_temp(temp_curr,
                               (double)config.temp_vent_lower_limit / 10.0,
                               (double)config.temp_vent_upper_limit / 10.0,
                               config.motor_stroke_time, config.motor_turn_seconds,
                               true);
            /* 已按温度下发；仍留 pending，下一拍再强制一次，防止被 STOP 冲掉. */
            auto_hold_pending = true;
        }
    }else{
        auto_hold_pending = false;
        auto_hold_baseline = -1;
    }
    timing_active_slot = -1;
    for ( int i = 0; i < 4; i++ ){
        timing_done_hour[i] = -1;
    }
    if ( (ventilate_service_status != Status::ALIGN) && (in_calibrating() == false) ){
        ventilate_service_status = Status::STOPPED;
    }
    LOG_INFO("Mode change -> %d, keep opening %d percent.",
             config.working_mode, opening_to_int(opening));
    return true;
}

bool VentilateService::set_clock(uint8_t hour, uint8_t minute, bool valid){
    if ( valid == false ){
        clock_valid = false;
        return true;
    }
    if ( hour > 23 ){
        hour = 23;
    }
    if ( minute > 59 ){
        minute = 59;
    }
    clock_hour = hour;
    clock_minute = minute;
    clock_valid = true;
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
        /* 手动开/关：按 C1 满开圈数或 0 圈定点，到圈停（不再一路跑到电流限位）. */
        Config_t cfg;
        ConfigService::get_config(cfg);
        int stroke = cfg.motor_stroke_time;
        if ( stroke < MOTOR_STROKE_TURNS_MIN ){
            stroke = MOTOR_STROKE_TURNS_MIN;
        }else if ( stroke > MOTOR_STROKE_TURNS_MAX ){
            stroke = MOTOR_STROKE_TURNS_MAX;
        }
        force_action = ForceAction::None;
        force_motion_seen = false;
        motor->set_ignore_stall(false);
        end_stop_latched = false;
        const int target_turns = (action == ForceAction::ForceOpen) ? stroke : 0;
        /* 自动：按键后锁定温控；定时：按键只停当前动作. */
        note_manual_override();
        auto_hold_pending = false;
        auto_hold_baseline = target_turns;
        LOG_INFO("Manual %s -> target turns %d.",
                 (action == ForceAction::ForceOpen) ? "open" : "close", target_turns);
        return set_target_turns(target_turns);
    }else if ( action == ForceAction::None || action == ForceAction::ForceStop ){
        LOG_INFO("Motor stopped.");
        Config_t config;
        ConfigService::get_config(config);
        /* 取消强制与精确圈数目标，否则下一拍 eventloop 会继续往 target 跑. */
        force_action = ForceAction::None;
        target_by_timer = false;
        close_run_latched = false;
        open_run_latched = false;
        force_motion_seen = false;
        abort_opening_persist();
        note_manual_override();
        auto_hold_pending = false;
        if ( auto_manual_override == false ){
            auto_hold_baseline = -1;
        }
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
            opening_percentage_curr = clamp_opening(calc_opening_percent(motor_timer_cnt, config.motor_stroke_time, config.motor_turn_seconds));
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