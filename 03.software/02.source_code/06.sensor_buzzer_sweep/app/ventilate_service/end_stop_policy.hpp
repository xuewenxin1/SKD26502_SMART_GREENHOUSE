#ifndef END_STOP_POLICY_HPP
#define END_STOP_POLICY_HPP

/* Pure rules for close-end stop and motor lamps. Used by firmware and tests. */

namespace EndStopPolicy {

inline int shown_percent(double live){
    if ( live < 0.0 ){
        live = 0.0;
    }
    if ( live > 100.0 ){
        live = 100.0;
    }
    int value = (int)(live + 0.5);
    if ( value < 0 ){
        value = 0;
    }
    if ( value > 100 ){
        value = 100;
    }
    return value;
}

inline bool close_lamp_on(bool running_reverse, int shown){
    if ( shown <= 0 ){
        return false;
    }
    return running_reverse && (shown < 100);
}

inline bool open_lamp_on(bool running_forward, int shown){
    if ( shown <= 0 ){
        return false;
    }
    return running_forward && (shown < 100);
}

/* At 0%: always stop and cut power, unless already opening away from 0%. */
inline bool should_stop_close(bool force_open, bool closing, bool aligning,
        unsigned int align_loops, bool idle, int shown, bool opening_now = false){
    if ( force_open || opening_now ){
        return false;
    }
    if ( shown <= 0 ){
        return true;
    }
    if ( aligning && (idle || (align_loops >= 300U)) ){
        return true;
    }
    if ( closing && idle ){
        return true;
    }
    return false;
}

}

#endif
