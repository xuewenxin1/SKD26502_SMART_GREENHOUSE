#ifndef CONFIG_HPP
#define CONFIG_HPP

#define CONFIG_AREA_ADDRESS 0x801e000
#define CONFIG_AREA_LENGTH 0x2000

#define MOTOR_STROKE_TIME_MIN 30
#define MOTOR_STROKE_TIME_MAX 999
#define DATA_UPLOAD_INTERVAL_MIN 5
#define DATA_UPLOAD_INTERVAL_MAX 30

enum TempControlMode {
    TempControlMode_Invalid = 0,
    TempControlMode_Central = 1,
    TempControlMode_Limit = 2,
};

enum WorkingMode {
    WorkingMode_Invalid = 0,
    WorkingMode_Manual = 1,
    WorkingMode_Auto = 2,
    WorkingMode_Timing = 3,
};

typedef struct Ventilator_Timing {
    int time;
    int opening_percentage;
} Ventilator_Timing_t;

typedef struct Config {
    int temp_alert_upper_limit;      /* 温度报警上限(A1). */
    int temp_alert_lower_limit;      /* 温度报警下限(A2). */
    int temp_control_mode;              /* 温控模式(P0). */
    int target_central_temp;         /* 目标中心温度(P1). */
    int temp_return_diff_positive;   /* 温度正回差(P2)，0-5℃ 整数，内部 0.1℃. */
    int temp_return_diff_negative;   /* 温度负回差(P3)，0-5℃ 整数，内部 0.1℃. */
    int temp_compensation_value;     /* 温度传感器补偿值(P4). */
    Ventilator_Timing_t ventilator_timing_config[4];    /* 四段定时时间与开度设置(D1-D4). */
    int motor_stroke_time;              /* 电机行程时间(C1). */
    int data_upload_interval;           /* 数据上报时间间隔(S0). */
    int local_alarm_switch;             /* 本地报警开关(S2). */
    int working_mode;                   /* 工作模式(手动,自动,定时). */
    int last_opening_percentage;        /* 上次开度 0-100，开机回零后恢复. */
} Config_t;

namespace ConfigService {

bool init();
bool get_config(Config_t &config);
bool set_config(Config_t config);
bool store();

}

#endif
