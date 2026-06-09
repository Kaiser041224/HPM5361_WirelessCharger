# HPM5361 Wireless Charger

基于 HPMicro HPM5361 (RISC-V, 480MHz) 的无线充电器控制系统。采用 Buck-Boost + LCC 级联拓扑，支持闭环功率控制、CAN 通信和故障保护。

---

## 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | HPM5361 (RISC-V, 双核 480MHz) |
| PWM | 2× HRPWM (PWM0 + PWM1)，28-bit 扩展计数器 |
| ADC | 2× ADC16 (ADC0 + ADC1)，16-bit，PMT 模式 |
| 通信 | CAN 2.0B (MCAN) |
| 调试 | SEGGER RTT + JTAG |

### 拓扑结构

```
V_IN ──→ [Buck-Boost] ──→ V_LINK ──→ [LCC 全桥] ──→ 线圈 ──→ 接收端
           PWM0                          PWM1
```

---

## 软件架构

采用 **Board / Interface / Driver / App** 四层解耦架构，App 层与芯片外设完全隔离。

```
┌─────────────────────────────────────────────────────────┐
│                    App (纯业务逻辑)                       │
│              禁止包含任何 hpm_* 头文件                     │
├─────────────────────────────────────────────────────────┤
│                Interface (C17 抽象接口)                   │
│    intf_hrpwm.h / intf_adc.h / intf_trgm.h / ...        │
├─────────────────────────────────────────────────────────┤
│              Driver/hpm_impl (SDK 适配层)                 │
│    drv_hrpwm.c / drv_adc.c / drv_trgm.c (唯一可调用     │
│                   hpm_sdk API 的地方)                    │
├─────────────────────────────────────────────────────────┤
│                Board (硬件配置层)                         │
│      pinmux.c (IOCFG) / board.c (时钟引脚)               │
│      HPM5361_WirelessCharger_board/ (板级 BSP)           │
└─────────────────────────────────────────────────────────┘
```

依赖方向：

```
Application -> Control -> Platform -> Interface -> Driver -> Board
Control     -> Algorithm
Debug       -> Platform / Control / Interface
```

---

## 目录结构

```
HPM5361_WirelessCharger/
├── CMakeLists.txt
├── Makefile
├── AGENTS.md                           # 开发规范
├── README.md
│
├── Board/
│   └── HPM5361_WirelessCharger_board/
│       ├── board.c / board.h           # 板级初始化
│       └── pinmux.c / pinmux.h         # 引脚配置
│
├── Interface/                          # C17 抽象接口层
│   ├── intf_hrpwm.h                    # HRPWM 接口
│   ├── intf_adc.h                      # ADC 接口
│   ├── intf_trgm.h                     # TRGM 触发路由接口
│   ├── intf_gpwm.h                     # GPTMR PWM 接口
│   ├── intf_gpio.h / intf_can.h / ...  # 其他外设接口
│   └── intf_default.c                  # 注册分发实现
│
├── Driver/
│   ├── hpm_impl/                       # HPM SDK 适配层
│   │   ├── drv_hrpwm.c / .h           # HRPWM 驱动
│   │   ├── drv_adc.c                   # ADC16 PMT 驱动
│   │   ├── drv_trgm.c                  # TRGM 驱动
│   │   ├── drv_mcan.c / .h            # MCAN 驱动
│   │   └── drv_*.c                     # 其他驱动
│   └── WS2812/                         # WS2812 协议实现
│
├── App/
│   ├── main.c                          # 程序入口
│   │
│   ├── Application/                    # 应用编排层
│   │   ├── Inc/
│   │   │   ├── app_entry.h             # 系统入口声明
│   │   │   ├── app_control.h           # 状态机 + 模式管理
│   │   │   └── app_comm.h              # CAN 通信
│   │   └── Src/
│   │       ├── app_entry.c             # 初始化顺序编排
│   │       ├── app_control.c           # 状态机实现
│   │       └── app_comm.c              # CAN 协议处理
│   │
│   ├── Control/                        # 控制器层
│   │   ├── Inc/
│   │   │   ├── ctrl_buckboost.h        # Buck-Boost 控制器
│   │   │   ├── ctrl_lcc.h              # LCC 控制器
│   │   │   ├── ctrl_fault.h            # 故障管理
│   │   │   └── ctrl_types.h            # 控制类型定义
│   │   └── Src/
│   │       ├── ctrl_buckboost.c
│   │       ├── ctrl_lcc.c
│   │       └── ctrl_fault.c
│   │
│   ├── Platform/                       # 硬件能力封装层
│   │   ├── Inc/
│   │   │   ├── app_adc.h               # ADC 平台 API
│   │   │   ├── app_hrpwm.h             # HRPWM 平台 API
│   │   │   ├── app_gpio.h              # GPIO 平台 API
│   │   │   ├── app_can.h               # CAN 平台 API
│   │   │   └── app_analog_signal.h     # 模拟信号处理
│   │   └── Src/
│   │       ├── app_adc.c               # ADC PMT 初始化 + 回调
│   │       ├── app_hrpwm.c             # HRPWM 初始化 + 控制
│   │       ├── app_buzzer.c            # 蜂鸣器驱动
│   │       └── app_ws2812.c            # LED 驱动
│   │
│   ├── Algorithm/                      # 纯算法库
│   │   ├── Inc/
│   │   │   ├── algo_pid.h              # PID 控制器
│   │   │   ├── algo_pll.h              # PLL 锁相环
│   │   │   ├── algo_ramp.h             # 斜坡函数
│   │   │   ├── algo_rms.h              # RMS 计算
│   │   │   ├── algo_ffd.h              # 前馈
│   │   │   ├── algo_filter.h           # 滤波器
│   │   │   └── algo_hyst.h             # 迟滞比较
│   │   └── Src/
│   │       └── algo_*.c
│   │
│   └── Debug/                          # 调试辅助层
│       ├── Inc/
│       │   ├── app_debug.h
│       │   ├── app_debug_adc.h
│       │   ├── app_debug_hrpwm.h
│       │   └── app_debug_rtt.h
│       └── Src/
│           ├── app_debug.c             # 调试命令分发
│           ├── app_debug_adc.c         # ADC 调试输出
│           ├── app_debug_hrpwm.c       # PWM 调试输出
│           └── app_debug_rtt.c         # RTT 输出封装
│
├── doc/                                # 设计文档
│   ├── control_loop_design.md          # 控制环路设计（含触发链路）
│   ├── hrpwm_driver_design.md          # HRPWM 驱动设计
│   ├── adc_driver_design.md            # ADC 驱动设计
│   ├── ADC_PMT_FIRST_CONVERSION_ANOMALY.md  # ADC PMT 首次转换问题记录
│   ├── build_flash_debug_guide.md      # 构建烧录调试指南
│   └── todo.md                         # 开发计划
│
└── linkers/                            # 链接脚本
```

---

## PWM-ADC 触发链路

双 ADC 独立触发架构，PWM CMP8 比较事件通过 TRGM 触发 ADC PMT 转换：

```
PWM0_CH8 (200kHz, CMP8) ──TRGM──→ PTRGI0A (trig_ch=0) ──→ ADC0
PWM1_CH8 (148kHz, CMP8) ──TRGM──→ PTRGI1A (trig_ch=3) ──→ ADC1
```

| ADC 实例 | 触发源 | 有效通道 | 用途 |
|:---|:---|:---|:---|
| ADC0 | PWM0 (200kHz) | V_LINK, I_L, I_IN | Buck-Boost 控制 |
| ADC1 | PWM1 (148kHz) | V_IN, I_COIL, I_LF | LCC 控制 + 输入监测 |

每个 ADC 实例 PMT 队列 4 通道，位置 0 为 dummy（规避 HPM5361 ADC16 首次转换结果异常），有效通道 3 个。

详细设计见 `doc/control_loop_design.md` 第 2.6 节。

---

## 系统状态机

```
INIT ──(自检通过)──→ IDLE ──(power_enable)──→ RUN
  ↑                      ↑                        │
  │                (power_disable) ←─────────(正常停机)
  │                      │
  └──(fault_clear)── FAULT ←──(任意状态检测到故障)
```

运行模式：

| 模式 | 控制器 | 说明 |
|:---|:---|:---|
| `MODE_IDLE` | 无 | 无功率输出 |
| `MODE_BUCK_CV` | ctrl_buckboost | Buck-Boost 恒压 |
| `MODE_BUCK_CC` | ctrl_buckboost | Buck-Boost 恒流 |
| `MODE_LCC_OPEN` | ctrl_lcc | LCC 开环 |
| `MODE_LCC_CLOSED` | ctrl_lcc | LCC 闭环电流 |
| `MODE_STANDBY` | 无 | 仅采样监测 |

---

## 构建与烧录

```bash
# 构建
make build BOARD=HPM5361_WirelessCharger_board

# 烧录
make flash BOARD=HPM5361_WirelessCharger_board

# RTT 调试输出
# 使用 J-Link RTT Viewer 连接
```

详见 `doc/build_flash_debug_guide.md`。

---

## 故障保护

统一故障管理 (`ctrl_fault`)，支持 15 种故障码：

| 故障码 | 说明 | 响应 |
|:---|:---|:---|
| `FAULT_OV_VIN` | 输入过压 | PWM 锁定 |
| `FAULT_UV_VIN` | 输入欠压 | PWM 锁定 |
| `FAULT_OC_IIN` | 输入过流 | PWM 锁定 |
| `FAULT_OC_IL` | 电感过流 | PWM 锁定 |
| `FAULT_OV_VLINK` | V_LINK 过压 | PWM 锁定 |
| `FAULT_OC_ICOIL` | 线圈过流 | PWM 锁定 |
| `FAULT_OC_ILF` | 谐振过流 | PWM 锁定 |
| `FAULT_OT` | 过温 | PWM 锁定 |
| `FAULT_ADC` | ADC 异常 | 暂停更新 |
| `FAULT_PWM` | PWM 故障 | PWM 锁定 |

---

## 通信协议

- 物理层：CAN 2.0B, 1Mbps
- 命令帧：START / STOP / SET_MODE / SET_TARGET / SET_PARAM / GET_STATUS / CLEAR_FAULT
- 遥测帧：周期性上报状态、ADC 值、PWM 参数、故障码
- 超时保护：主机长时间无心跳 → 自动进入安全态

---

## 文档索引

| 文档 | 内容 |
|:---|:---|
| `doc/control_loop_design.md` | 控制环路设计、硬件基线、触发链路详解、时间预算 |
| `doc/hrpwm_driver_design.md` | HRPWM 驱动设计 |
| `doc/adc_driver_design.md` | ADC 驱动设计 |
| `doc/ADC_PMT_FIRST_CONVERSION_ANOMALY.md` | HPM5361 ADC PMT 首次转换异常问题记录 |
| `doc/build_flash_debug_guide.md` | 构建烧录调试指南 |
| `doc/hpm_pwm_gptmr_pwm_guide.md` | PWM/GPTMR 使用指南 |
| `AGENTS.md` | 嵌入式 C17 解耦架构开发规范 |

---

## 开发规范

- C 语言标准：C17
- 架构约束：App 层禁止包含 `hpm_*.h`，仅通过 Interface 层访问硬件
- 编码风格：详见 `AGENTS.md`
- 分支策略：feature 分支开发，PR 合并
