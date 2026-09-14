#ifndef CONFIG_HPP
#define CONFIG_HPP

#define CONFIG_AREA_ADDRESS 0x801e000
#define CONFIG_AREA_LENGTH 0x2000

/* C1 行程圈数；C2 一圈秒数. */
#define MOTOR_STROKE_TURNS_MIN 1
#define MOTOR_STROKE_TURNS_MAX 999
#define MOTOR_TURN_SECONDS_MIN 10
#define MOTOR_TURN_SECONDS_MAX 100
#define MOTOR_TURN_SECONDS_DEFAULT 25
#define TEMP_SETTING_MIN 0
#define TEMP_SETTING_MAX 55
/* 兼容旧宏名：范围按圈数. */
#define MOTOR_STROKE_TIME_MIN MOTOR_STROKE_TURNS_MIN
#define MOTOR_STROKE_TIME_MAX MOTOR_STROKE_TURNS_MAX
#define MOTOR_STROKE_TURN_SECONDS MOTOR_TURN_SECONDS_DEFAULT
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
    int motor_stroke_time;              /* 电机行程圈数(C1). */
    int data_upload_interval;           /* 数据上报时间间隔(S0). */
    int local_alarm_switch;             /* 本地报警开关(S2). */
    int working_mode;                   /* 工作模式(手动,自动,定时). */
    int last_opening_percentage;        /* 上次开度 0-100，开机保持该开度不归零. */
    int temp_vent_upper_limit;       /* 开启温度上限(P5)，内部 0.1℃（协议×10），≥此温度全开. */
    int temp_vent_lower_limit;       /* 关闭温度下限(P6)，内部 0.1℃（协议×10），≤此温度全关. */
    int motor_turn_seconds;          /* 一圈秒数(C2). */
    int last_motor_timer_cnt;        /* 上次行程计时(0.1s)，上电恢复已转时间/圈数. */
} Config_t;

namespace ConfigService {

bool init();
bool get_config(Config_t &config);
bool set_config(Config_t config);
bool store();
/* 延迟落盘：先改 RAM，避免在 MQTT 收包路径里擦 Flash 导致 UART 溢出丢指令. */
bool request_store();
bool flush_store();

}

#endif
