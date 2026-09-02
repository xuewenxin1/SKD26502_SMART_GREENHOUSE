"""Host tests for close-end stop and lamp rules. Mirrors end_stop_policy.hpp."""

def shown_percent(live):
    live = 0.0 if live < 0.0 else live
    live = 100.0 if live > 100.0 else live
    value = int(live + 0.5)
    if value < 0:
        value = 0
    if value > 100:
        value = 100
    return value


def close_lamp_on(running_reverse, shown):
    if shown <= 0:
        return False
    return running_reverse and (shown < 100)


def open_lamp_on(running_forward, shown):
    if shown <= 0:
        return False
    return running_forward and (shown < 100)


def should_stop_close(force_open, closing, aligning, align_loops, idle, shown, opening_now=False):
    if force_open or opening_now:
        return False
    if shown <= 0:
        return True
    if aligning and (idle or align_loops >= 300):
        return True
    if closing and idle:
        return True
    return False


def expect(ok, name):
    if ok:
        print("PASS", name)
        return 0
    print("FAIL", name)
    return 1


def main():
    fails = 0
    fails += expect(should_stop_close(False, True, False, 0, True, 40) is True, "close_hit_limit_idle_stop")
    fails += expect(close_lamp_on(False, 40) is False, "close_hit_limit_idle_lamp_off")
    fails += expect(shown_percent(0.2) == 0, "display_0_from_0p2")
    fails += expect(should_stop_close(False, True, False, 0, False, 0) is True, "close_shown_0_running_stop")
    fails += expect(close_lamp_on(True, 0) is False, "close_shown_0_lamp_off")
    fails += expect(should_stop_close(False, True, False, 0, False, 40) is False, "close_mid_keep_running")
    fails += expect(close_lamp_on(True, 40) is True, "close_mid_lamp_on")
    fails += expect(should_stop_close(True, False, False, 0, False, 0) is False, "force_open_from_zero_not_stop_close")
    fails += expect(should_stop_close(False, False, False, 0, True, 0) is True, "at_zero_always_stop")
    fails += expect(open_lamp_on(True, 0) is False, "open_lamp_off_at_0")
    fails += expect(open_lamp_on(True, 1) is True, "open_lamp_on_at_1")
    fails += expect(open_lamp_on(True, 100) is False, "open_lamp_off_at_100")
    fails += expect(open_lamp_on(False, 50) is False, "open_lamp_off_when_idle")
    fails += expect(should_stop_close(False, True, True, 1, False, 0) is True, "align_shown_0_stop")
    fails += expect(should_stop_close(False, True, True, 5, False, 50) is False, "align_mid_before_timeout")
    fails += expect(should_stop_close(False, True, True, 300, False, 50) is True, "align_timeout_stop")
    fails += expect(should_stop_close(False, True, False, 0, False, 0, True) is False, "open_from_zero_not_stop")
    fails += expect(open_lamp_on(True, 0) is False, "open_from_zero_lamp_off_until_1")
    if fails:
        print("%d test(s) failed." % fails)
        return 1
    print("All end-stop tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
