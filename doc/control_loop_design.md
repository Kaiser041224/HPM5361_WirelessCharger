# 控制环设计说明

本文档整合了系统架构定义和基于当前硬件配置的控制环路设计，
包含分层架构、状态机、控制器设计、硬件基线、时间预算和控制策略。

---

## 1. 系统分层与调用方向

```
Application -> Control -> Platform -> Interface -> Driver -> Board
Control     -> Algorithm
Debug       -> Platform / Control / Interface
```

| 层 | 职责 |
|----|------|
| `App/Application/` | 决定"做什么、什么时候做、处于什么模式"。`app_entry` 为唯一入口，`app_control` 负责状态机编排，`app_comm` 负责 CAN 通信 |
| `App/Control/` | 决定"控制怎么算、如何保护"。`ctrl_buckboost` / `ctrl_lcc` / `ctrl_fault` |
| `App/Algorithm/` | 纯数学计算模块 (PID / PLL / Ramp / RMS / FFD / Filter)，不依赖 Driver/Board |
| `App/Platform/` | 硬件能力封装 (HRPWM / ADC / SamplingSync / CAN / GPIO) |
| `App/Debug/` | 验证辅助层，不作为正式运行路径 |

---

## 2. 硬件基线

### 2.1 时钟域

CPU0 主频由 PLL0 配置为 **480MHz**。PWM 与 ADC 按外设侧时钟计算：
AHB = 120MHz，clock_mot0 = 120MHz，clock_adc0/1 上游 120MHz。

### 2.2 PWM 配置

`App/Platform/Src/app_hrpwm.c` 默认配置：

| Pair | PWM 实例 | 频率 | 对齐模式 | 初始 duty | 死区 |
|------|----------|------|----------|-----------|------|
| `HRPWM_PAIR_0` | PWM0 | 200kHz | 中心对齐 | 0.0 | 10ns |
| `HRPWM_PAIR_1` | PWM0 | 200kHz | 中心对齐 | 0.0 | 10ns |
| `HRPWM_PAIR_2` | PWM1 | 148kHz | 中心对齐 | 0.0 | 25ns |
| `HRPWM_PAIR_3` | PWM1 | 148kHz | 中心对齐 | 0.0 | 25ns |

### 2.3 ADC 配置

| 项目 | 值 |
|------|-----|
| 分辨率 | 16-bit |
| sample_cycle | 20 |
| clock_div | 3 |
| 采样模式 | PMT |
| ADC clock | 120MHz / 3 = 40MHz |
| 单通道转换时间 | ≈ 1.025μs |

### 2.4 ADC 通道映射

| 逻辑通道 | ADC 实例 | 物理通道 | 引脚 | 说明 |
|----------|---------|----------|------|------|
| `ADC_CH_V_IN` | ADC0 | ch6 | PB14 | Buck-Boost 输入电压 |
| `ADC_CH_I_IN` | ADC0 | ch11 | PB08 | 输入母线电流 |
| `ADC_CH_I_L` | ADC0 | ch2 | PB10 | 电感电流 |
| `ADC_CH_V_LINK` | ADC0 | ch3 | PB11 | 级联母线电压 |
| `ADC_CH_I_COIL` | ADC1 | ch4 | PB12 | 线圈电流 |
| `ADC_CH_I_LF` | ADC1 | ch5 | PB13 | LCC 谐振电流 |

### 2.5 硬件资源映射

| 硬件资源 | 控制对象 |
|----------|---------|
| PWM0 pair 0 (主半桥) | Buck-Boost |
| PWM0 pair 1 (同步整流) | Buck-Boost (可选) |
| PWM1 pair 2 (半桥 A) | LCC 全桥 |
| PWM1 pair 3 (半桥 B) | LCC 全桥 |

---

## 3. 系统状态机

### 3.1 状态定义

| 状态 | 枚举 | 说明 |
|------|------|------|
| `SYS_INIT` | 0 | 初始化，自检 |
| `SYS_IDLE` | 1 | 空闲，等待命令 |
| `SYS_RUN` | 2 | 正常运行，闭环控制 |
| `SYS_FAULT` | 3 | 故障，PWM 锁定低电平 |

### 3.2 状态迁移

```
INIT ──(自检通过)──→ IDLE ──(power_enable)──→ RUN
  ↑                      ↑                        │
  │                (power_disable) ←─────────(正常停机)
  │                      │
  └──(fault_clear)── FAULT ←──(任意状态检测到故障)
```

迁移校验由 `app_control_tick()` 执行：故障优先检查 → 状态评估 → 模式执行。

### 3.3 运行模式

| 模式 | 说明 | 激活的控制器 |
|------|------|-------------|
| `MODE_IDLE` | 无功率输出 | 无 |
| `MODE_BUCK_CV` | Buck-Boost 恒压 | `ctrl_buckboost` (CV) |
| `MODE_BUCK_CC` | Buck-Boost 恒流 | `ctrl_buckboost` (CC) |
| `MODE_LCC_OPEN` | LCC 开环 | `ctrl_lcc` (开环) |
| `MODE_LCC_CLOSED` | LCC 闭环 | `ctrl_lcc` (闭环) |
| `MODE_STANDBY` | 待机监测 | 仅采样 |

---

## 4. 控制器架构

### 4.1 Buck-Boost 控制器 (`ctrl_buckboost`)

对象 `ctrl_buckboost_t { params, state }` 封装四开关拓扑的控制。

| 项目 | 说明 |
|------|------|
| 硬件 | PWM0 pair 0/1 |
| 控制模式 | Buck / Boost / BuckBoost (自动) |
| 控制目标 | CV (电压外环→电流内环) / CC (仅电流内环) |
| 输入 | V_IN, V_LINK, I_L |
| 输出 | 主半桥占空比 [0, 1]，直接调用 `hrpwm_set_duty()` |
| 算法 | 电压 PID + 电流 PID + 前馈 |
| 软启动 | `algo_ramp` 从 0 ramp 到目标 |

### 4.2 LCC 控制器 (`ctrl_lcc`)

对象 `ctrl_lcc_t { params, state }` 封装全桥 LCC 拓扑的控制。

| 项目 | 说明 |
|------|------|
| 硬件 | PWM1 pair 2/3 |
| 控制模式 | 开环 / 闭环电流 / PLL 频率跟踪 |
| 输入 | I_COIL, I_LF |
| 输出 | 频率 + 移相角，直接调用 `hrpwm_set_frequency()` / `hrpwm_set_phase()` |
| 算法 | PLL 锁频 + 线圈电流 PID |

### 4.3 故障管理 (`ctrl_fault`)

统一管理保护阈值与故障锁存：

- `ctrl_fault_init(thresholds)` — 加载阈值，清零锁存
- `ctrl_fault_check()` — 比对阈值 → 锁存 → 回调通知 → 返回活跃故障
- `ctrl_fault_clear()` / `ctrl_fault_clear_all()` — 条件满足后清除

---

## 5. 故障码定义

| 位 | 故障码 | 说明 |
|----|--------|------|
| 0 | `FAULT_OV_VIN` | V_IN 过压 |
| 1 | `FAULT_UV_VIN` | V_IN 欠压 |
| 2 | `FAULT_OC_IIN` | I_IN 过流 |
| 3 | `FAULT_OC_IL` | I_L 过流 |
| 4 | `FAULT_OV_VLINK` | V_LINK 过压 |
| 5 | `FAULT_UV_VLINK` | V_LINK 欠压 |
| 6 | `FAULT_OC_ICOIL` | I_COIL 过流 |
| 7 | `FAULT_OC_ILF` | I_LF 过流 |
| 8 | `FAULT_OT` | 过温 |
| 9 | `FAULT_ADC` | ADC 数据异常 |
| 10 | `FAULT_PWM` | PWM 故障 |
| 11 | `FAULT_CAN_BUSOFF` | CAN 总线关闭 |
| 12 | `FAULT_DRVPWR` | 驱动电源异常 |
| 13 | `FAULT_HARDWARE` | 外部硬件故障输入 |
| 14 | `FAULT_TIMEOUT` | 通讯超时 |

故障分类：
- **硬件故障**：立即锁存，PWM 强制拉低
- **参数越限**：锁存并降级，可恢复
- **通讯故障**：降级运行，超时后保护态
- **数据故障**：暂停控制更新，保持上次有效值

---

## 6. 数据流

### 6.1 控制数据流

```
ADC PMT (Platform)
  → V_IN, V_LINK, I_L, I_COIL, I_LF 测量值
  → ctrl_buckboost_step() / ctrl_lcc_step() (Control)
  → 计算占空比 / 频率 / 相位
  → hrpwm_set_duty() / hrpwm_set_frequency() / hrpwm_set_phase() (Platform)
  → PWM 硬件
```

### 6.2 状态与故障数据流

```
app_comm_tick() (Application) 接收 CAN 命令
  → app_control_set_mode() / app_control_power_enable()
  → 驱动状态机迁移

ctrl_fault_check() (Control)
  → 发现越限 → hrpwm_emergency_stop()
  → 通知 app_control → 状态迁移到 FAULT
```

---

## 7. 任务调度

| 层级 | 触发源 | 典型频率 | 典型任务 |
|------|--------|----------|---------|
| Fast | PWM reload IRQ | ~200kHz | 电流环 step |
| Medium | `app_run_once()` | ~1kHz | 状态机推进、故障检查 |
| Slow | 分频 | ~10Hz | CAN 通信、遥测上报 |

`app_run_once()` 内部：
```
app_control_tick()    // 故障检查 → 状态评估 → 执行控制
app_comm_tick()       // CAN 帧处理 + 遥测
```

---

## 8. 时间预算与控制策略

### 8.1 PWM0 (200kHz) 周期分析

T_pwm = 5μs。当前 ADC0 一次 PMT 采 4 路，总转换时间约 4.1μs。
callback 到达时刻已非常靠近周期末端，剩余约 0.6~0.9μs。

### 8.2 推荐策略

**PWM0 + ADC0 (Buck-Boost)：每周期采样，每 4 周期更新一次**

- 每个周期都进入 ADC PMT callback，锁存 I_L 样本
- 每个周期都进入 PWM reload IRQ
- 每 4 个周期执行一次 PI 并更新 duty
- 更新频率 = 50kHz，周期 = 20μs

以 200kHz、N=4 为例：

| 更新间隔 | 控制更新频率 | 控制更新周期 |
|----------|--------------|--------------|
| 每 4 周期 | 50kHz | 20μs |

### 8.3 PWM1 (148kHz) 周期分析

T_pwm ≈ 6.76μs，剩余约 2.66μs。比 200kHz 宽裕但多通道 PMT 下仍偏紧。

### 8.4 不建议的方案

在 200kHz 下使用当前 4 通道 PMT 做同一周期内"采样→转换→计算→更新 duty"——余量过小，不适合作为稳定设计。

---

## 9. 主机通信

- 物理层：CAN 2.0B, 1Mbps
- 命令帧：START / STOP / SET_MODE / SET_TARGET / SET_PARAM / GET_STATUS / CLEAR_FAULT
- 遥测帧：周期性上报状态、ADC 值、PWM 参数、故障码
- 超时保护：主机长时间无心跳 → 自动进入安全态

---

## 10. 文件索引

| 层 | 文件 | 说明 |
|----|------|------|
| Application | `app_entry.c` | 唯一入口 |
| Application | `app_control.c` | 状态机 + 任务编排 |
| Application | `app_comm.c` | CAN 通信 |
| Control | `ctrl_buckboost.c` | Buck-Boost 控制器 |
| Control | `ctrl_lcc.c` | LCC 控制器 |
| Control | `ctrl_fault.c` | 故障 + 保护 |
| Algorithm | `algo_pid/pll/ramp/rms/ffd/filter` | 算法库 |
| Platform | `app_hrpwm/app_adc/app_sampling_sync/app_can` | 硬件能力 |
