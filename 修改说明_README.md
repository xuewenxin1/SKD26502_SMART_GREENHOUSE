# SKD26502 近期修改说明

这份文档说明：**最近改了什么问题、主要在哪个文件夹、对应哪些源文件**。  
日常怎么烧录、怎么用机器，请看根目录的 [`README.md`](README.md)。

---

## 1. 主要改在哪个文件夹？

**日常开发只动下面两个固件工程**（FreeRTOS 版）：

| 文件夹 | 是什么 | 烧录按键 |
|--------|--------|----------|
| `03.software/02.source_code/04.controller_unit_motor_freertos/` | **放风机** 固件 | **F7** |
| `03.software/02.source_code/04.controller_unit_sensor_freertos/` | **温湿度计** 固件 | **F8** |

辅助脚本：

| 路径 | 作用 |
|------|------|
| `scripts/build_firmware.ps1` | 编译 / 烧录脚本（F7、F8 会调用） |

下面这些是旧工程或资料，**一般不用改**：

- `01.controller_unit.old/`、`02.controller_unit_motor/`、`03.controller_unit_sensor/` → 旧代码
- `01.requirements_analysis_and_system_design/` → 需求和设计文档
- `02.hardware/` → 硬件原理图 / PCB

---

## 2. 每个工程里常改的子目录

两个固件结构差不多，改功能时主要看：

```
04.controller_unit_xxx_freertos/
├── app/
│   ├── env/                 ← 温度采样、蜂鸣器、电池（温湿度计）
│   ├── gui/                 ← 屏幕显示、设置菜单（A1/P1/P2…）
│   ├── ventilate_service/   ← 电机开合、自动模式开度
│   └── iot/                 ← MQTT 上报 / 下发配置
└── system/drivers/config/   ← 参数默认值、Flash 保存、范围限制
```

---

## 3. 最近改了哪些问题？（按功能）

### ① 自动模式：温度越高，风机开度越大

**问题/需求：** 实测温度比目标温度（P1）高得越多，放风机开得越大。  
**规则：** 只算整数℃，小数直接丢掉（不四舍五入）。回差 X（P2）把整条曲线往后挪。

| 有效温差（实测 − 目标 − X） | 开度 |
|------------------------------|------|
| ≤ 0℃ | 0% |
| 2℃ | 20% |
| 4℃ | 40% |
| … | … |
| ≥ 10℃ | 100% |

**主要文件：**

- `app/ventilate_service/ventilate_service.cpp` → 函数 `opening_from_temp_delta`
- `system/drivers/config/config.cpp` → 回差默认值、限制 0～5℃

两套固件都有同样逻辑。

---

### ② 回差 P2 / P3：只允许 0～5℃ 整数，默认 0

**问题/需求：** 开度已经跟温差绑定，回差作用变小，所以缩小范围，避免和开度曲线“抢活”。

- 只能设：0、1、2、3、4、5
- 加减每次只变 1（长按也不会跳 10）
- 新板出厂默认 0℃
- 旧板若 Flash 里还是 5，开机加载时会被限制到合法范围；也可手动改

**主要文件：**

- `app/gui/gui.cpp` → 设置页加减
- `system/drivers/config/config.cpp` → `clamp_hysteresis`、默认值
- `app/iot/iot_process.cpp` → 云端下发配置时也限制 0～5

---

### ③ 设置页暂时隐藏 D1～D4、P0、P3

**问题/需求：** 四段定时与 P0/P3 暂时不用，菜单里不要出现。

- D1～D4：`#define GUI_ENABLE_D1_D4_SETTING 0`
- P0、P3：`#define GUI_ENABLE_P0_P3_SETTING 0`
- 改成 `1` 再编译，就会重新显示

**主要文件：** `app/gui/gui.cpp`

---

### ③b S0 数据上报间隔 5～30 秒（写 Flash）

**问题/需求：** 设置页 S0 可调 5～30 秒，退出设置后写入 Flash；开机按保存值上报。

- **5～30**：每隔 N 秒整包上报（设备信息 + 状态 + 配置）
- 默认 **5** 秒；加载/保存时限制在 5～30

**主要文件：** `app/gui/gui.cpp`、`system/drivers/config/config.cpp`、`app/iot/iot_service.cpp`

---

### ④ 温度超报警上下限 → 蜂鸣器响（约 4kHz）

**问题/需求：** 温度高于 A1 或低于 A2 时蜂鸣器响。

- 引脚：**PA12**
- 方式：TIM14 定时翻转，输出约 **4kHz** 方波（这颗芯片 PA12 没有硬件 PWM 通道）
- **S2 本地报警**关掉则不响
- **放风机、温湿度计都保留蜂鸣器**

**主要文件：**

- `app/env/env.cpp` → `buzzer_pwm_init`、`update_temp_alarm`、`TIM14_IRQHandler`
- `app/gui/gui.cpp` → 超限时点亮告警符号

---

### ⑦ MQTT 状态故障码 2 / 3

**需求：** 状态上报带故障码：`2`=传感器故障，`3`=温度超限。

| 故障码 | 含义 | 判定 |
|--------|------|------|
| 0 | 正常 | — |
| 1 | 电机过流 | 电机状态 `ERROR_OVC`（保留） |
| 2 | 传感器故障 | 无有效温度（显示 `---`） |
| 3 | 温度超限 | 有温度，且超出 A1/A2 |

优先级：**1 > 2 > 3**。写入 MQTT `up/status` 第 8 字节（`buffer[7]`）。

**主要文件：**

- `app/env/env.cpp` → `Env::get_error_code`
- `app/iot/iot_service.cpp` → `IOTService_up_status`

两套固件都已改。

---

### ⑤ 温湿度计电池显示（仅传感器工程）

**问题/需求：** 温湿度计首页显示电量格。

- BAT_C：**PB15** 一直拉高
- ADC：**PA4**，基准按 3.6V；约 1.8V=空、2.1V=满
- 首页不显示开度 / `%` / OK

**主要文件（只在 sensor 工程）：**

- `app/env/env.cpp`
- `app/gui/gui.cpp`
- `system/drivers/adc/adc.cpp`

---

### ⑥ 传感器插拔选用规则

| 插法 | 温度 | 湿度 |
|------|------|------|
| NTC + 温湿度计都插 | **NTC** | **温湿度计** |
| 只插温湿度计 | 温湿度计 | 温湿度计 |
| 只插 NTC | NTC | 无（屏幕 `--`） |
| 都不插 | 无（屏幕 `---`） | 无（屏幕 `---`）；**蜂鸣 + 亮告警** |

**主要文件：** `app/env/env.cpp`、`system/drivers/ntc/ntc_service.cpp`、`system/drivers/aht2415/aht2415.cpp`

NTC 已改为 **B 值法**（B=3950、25℃ 阻值 10kΩ、板上串联上拉 **10kΩ**）；NTC 分压供电 **3.86V**（ADC 比值法）。**电量 ADC 仍按 3.6V 基准**，未改。

**P4 温度补偿已生效**：在实测温度上加 P4（单位 0.1℃），影响显示、报警、自动开度、MQTT。

**电阻箱 10k 校准**：10k/10k 中点理论 raw≈2048。放风机/温湿度计均已做 **ADC 校准 + PCLK/4**（`adc/py32f071_hal_msp.c`），实测 raw≈2050，**已去掉** `NTC_ADC_GAIN`。温湿度计若漏配 ADC 时钟，NTC 会读失败，双插时仍显示 AHT 温度（看起来像“插 NTC 温度不变”）。

---

### ⑦ 校准电机提前停（已修）

**原因：** 校准反向时软件计时为 0 会立刻停；堵转检测也可能误停；反向段几乎量不到时间导致 C1 偏短。

**改法：** 校准模式 `set_calibrating(true)` —— 不按开度倒计时停、关闭堵转误停、往返都累计时间，仍靠电流≈0 判限位。

**主要文件：** `system/drivers/motor/motor.cpp`、`app/ventilate_service/ventilate_service.cpp`

---

### ⑧ App 下发「开」后中途停住（已修）

**原因：** 强制开窗时，堵转误判或中途电流≈0 会先让电机 IDLE；上层又把「中途停」当成放弃，开度停在半路。

**改法：**
- 强制开/关期间关闭堵转停机（`set_ignore_stall(true)`），减少中途误停
- 中途若已停住：**不再自动重试**，保持当前开度
- 仍靠开度到 100%/0%，或「电流≈0 + 开度已高/低」判到位

**仍会真正停住的情况：** 到 100%/0%、到位电流≈0、用户按停 / App 下发停、中途其它原因停住后保持开度（过流保护已关闭）

**主要文件：** 同上 `motor.cpp`、`ventilate_service.cpp`

---

### ⑨ 其它已落地的基础行为（两套工程）

| 内容 | 大致位置 |
|------|----------|
| 开机找零：连续 3 次电流≈0 才断电 | `ventilate_service` |
| 开度只在电机停稳约 0.5 秒后写入 Flash | `ventilate_service` |
| C1 行程 30～999 秒 | `gui` + `config` |
| MQTT `up/dev`：放风机类型 1、温湿度计类型 2 | `iot` |

---

## 4. 对照表：功能 → 文件夹 → 文件

| 功能 | 主要文件夹 | 关键文件 |
|------|------------|----------|
| 自动开度随温差 | `.../app/ventilate_service/` | `ventilate_service.cpp` |
| 回差 0～5、默认 0 | `.../app/gui/` + `.../system/drivers/config/` | `gui.cpp`、`config.cpp` |
| 隐藏 D1～D4 / P0 / P3 | `.../app/gui/` | `gui.cpp`（`GUI_ENABLE_D1_D4_SETTING`、`GUI_ENABLE_P0_P3_SETTING`） |
| S0 上报间隔 5～30 写 Flash | `.../app/gui/` + `.../system/drivers/config/` + `.../app/iot/` | `gui.cpp`、`config.cpp`、`iot_service.cpp` |
| 蜂鸣器 4kHz | `.../app/env/` | `env.cpp` |
| 电池电量（仅温湿度计） | `.../app/env/` + `.../app/gui/` | `env.cpp`、`gui.cpp` |
| 传感器插拔选用 | `.../app/env/` + `.../system/drivers/ntc/` | `env.cpp`、`ntc_service.cpp` |
| MQTT 故障码 2/3 | `.../app/env/` + `.../app/iot/` | `env.cpp`、`iot_service.cpp` |
| NTC B 值测温 | `.../system/drivers/ntc/` | `ntc_service.cpp`（B=3950） |
| 校准提前停修复 | `.../system/drivers/motor/` + `.../app/ventilate_service/` | `motor.cpp`、`ventilate_service.cpp` |
| App强制开中途停 | `.../system/drivers/motor/` + `.../app/ventilate_service/` | `motor.cpp`、`ventilate_service.cpp` |
| 关闭软件过流保护 | `.../system/drivers/motor/` | `motor.cpp` |
| MQTT 配置钳位 | `.../app/iot/` | `iot_process.cpp` |
| 使用说明 / 烧录 | 项目根目录 | `README.md` |
| 本文（改动说明） | 项目根目录 | `修改说明_README.md` |

路径里的 `...` =  
`03.software/02.source_code/04.controller_unit_motor_freertos`  
或  
`03.software/02.source_code/04.controller_unit_sensor_freertos`

---

## 5. 烧录提醒

1. 改放风机相关 → 按 **F7**
2. 改温湿度计相关 → 按 **F8**
3. 两边都改了同一功能（如回差、蜂鸣器、自动开度）→ **F7 和 F8 都要烧一次**
4. 板子要先上电，再接调试器烧录

---

## 6. 以后可能继续改的方向

- 需要定时功能时：把 `GUI_ENABLE_D1_D4_SETTING` 改回 `1`
- 需要 P0/P3 时：把 `GUI_ENABLE_P0_P3_SETTING` 改回 `1`
- 蜂鸣器若要改频率：改 `env.cpp` 里的 `BUZZER_FREQ_HZ`（当前 4000）
- 自动开度曲线若要再调：改 `ventilate_service.cpp` 里的 `opening_from_temp_delta`
