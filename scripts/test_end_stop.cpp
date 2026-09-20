#include <cstdio>
#include <cstdlib>
#include "../03.software/02.source_code/04.controller_unit_motor_freertos/app/ventilate_service/end_stop_policy.hpp"

static int fails = 0;

static void expect(bool ok, const char *name){
    if ( ok ){
        std::printf("PASS %s\n", name);
    }else{
        std::printf("FAIL %s\n", name);
        fails++;
    }
}

int main(){
    using namespace EndStopPolicy;

    /* 关到一半因假零流 IDLE：与开窗一样不停死、不吸成 0%，下一拍继续关. */
    expect(should_stop_close(false, true, false, 0, true, 40) == false,
           "close_mid_idle_keep_going");
    expect(close_lamp_on(false, 40) == false,
           "close_mid_idle_lamp_off");

    /* 关、屏幕已到 0%、电机还在反转：必须停，灯灭. */
    expect(shown_percent(0.2) == 0, "display_0_from_0p2");
    expect(should_stop_close(false, true, false, 0, false, 0) == true,
           "close_shown_0_running_stop");
    expect(close_lamp_on(true, 0) == false,
           "close_shown_0_lamp_off");

    /* 关、开度 40%、正在反转、未到限位：不停，灯亮. */
    expect(should_stop_close(false, true, false, 0, false, 40) == false,
           "close_mid_keep_running");
    expect(close_lamp_on(true, 40) == true,
           "close_mid_lamp_on");

    /* 从 0% 再开：不要按关限位停. */
    expect(should_stop_close(true, false, false, 0, false, 0) == false,
           "force_open_from_zero_not_stop_close");
    expect(should_stop_close(false, false, false, 0, true, 0) == false,
           "idle_at_zero_but_target_open_keep");

    /* 开灯：0% 和 100% 不亮，中间亮. */
    expect(open_lamp_on(true, 0) == false, "open_lamp_off_at_0");
    expect(open_lamp_on(true, 1) == true, "open_lamp_on_at_1");
    expect(open_lamp_on(true, 100) == false, "open_lamp_off_at_100");
    expect(open_lamp_on(false, 50) == false, "open_lamp_off_when_idle");

    /* 开机找零：已是 0% 立刻停. */
    expect(should_stop_close(false, true, true, 1, false, 0) == true,
           "align_shown_0_stop");
    /* 开机找零：未到 0 且未停，1 秒后停. */
    expect(should_stop_close(false, true, true, 5, false, 50) == false,
           "align_mid_before_timeout");
    expect(should_stop_close(false, true, true, 10, false, 50) == true,
           "align_timeout_stop");

    if ( fails != 0 ){
        std::printf("%d test(s) failed.\n", fails);
        return 1;
    }
    std::printf("All end-stop tests passed.\n");
    return 0;
}
