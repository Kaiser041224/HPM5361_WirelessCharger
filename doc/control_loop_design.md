# 控制环路设计说明

本文档描述 HPM5361 无线充电器项目的控制环路架构设计，包括 PWM-ADC 同步机制、控制算法选择、时序分析和实现方案。

> **设计状态**：设计讨论阶段
>
> **目标应用**：无线充电器功率控制（电流环/电压环）

---

## 1. 控制需求分析

### 1.1 控制目标

| 参数 | 要求 | 说明 |
|------|------|------|
| **控制对象** | 输出电流/电压 | 无线充电功率调节 |
| **PWM频率** | 200kHz / 148kHz | 已配置 |
| **控制周期** | 1个PWM周期 (5us / 6.75us) | 每个PWM周期更新一次占空比 |
| **响应时间** | < 10个PWM周期 | 快速响应负载变化 |
| **稳态精度** | < 1% | 电流/电压稳定 |

### 1.2 控制环路层级

```
┌─────────────────────────────────────────────────────────────────┐
│                        外环（电压环）                             │
│  输入：输出电压 Vout                                              │
│  输出：电流参考值 Iref                                            │
│  带宽：1-10kHz                                                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ↓ Iref
┌─────────────────────────────────────────────────────────────────┐
│                        内环（电流环）                             │
│  输入：电感电流 IL                                                │
│  输出：PWM占空比 D                                               │
│  带宽：10-50kHz                                                  │
│  执行频率：200kHz (每个PWM周期)                                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ↓ D
┌─────────────────────────────────────────────────────────────────┐
│                        PWM输出                                   │
│  频率：200kHz                                                    │
│  模式：中心对齐                                                  │
│  分辨率：600级 (9.2-bit)                                         │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 硬件资源配置

### 2.1 PWM资源

| 资源 | 实例 | 用途 | 引脚 |
|------|------|------|------|
| **PWM0** | HPM_PWM0 | 主功率PWM | PA24-PA27 |
| **PWM1** | HPM_PWM1 | 辅助PWM | PA28-PA31 |

**PWM配置**：
- 模式：中心对齐 (Center Aligned)
- 频率：200kHz (Reload=600 @120MHz)
- 死区：10ns
- 抖动：jitter_cmp=4

### 2.2 ADC采样通道分配

#### 2.2.1 硬件拓扑

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           四开关Buck-Boost (PWM0)                           │
│                                                                             │
│    Vin ──┤L1├── Vout                                                        │
│           │                                                                 │
│           ↓ IL_BUCK                                                         │
│    ┌──────────┐                                                             │
│    │ 检流电阻  │ → INA240A2 → PB08 (Buck-Boost输入电流)                      │
│    └──────────┘                                                             │
│                                                                             │
│    ┌──────────┐                                                             │
│    │ 检流电阻  │ → INA240A2 → PB10 (电感电流)                               │
│    └──────────┘                                                             │
│                                                                             │
│    PB14 → Buck-Boost输入电压                                                │
│    PB11 → Buck-Boost输出电压                                                │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                           全桥LCC (PWM1)                                    │
│                                                                             │
│    ┌──────────┐     ┌──────────┐                                            │
│    │ 电流互感器│ →   │   运放   │ → PB13 (LCC谐振电流)                        │
│    └──────────┘     └──────────┘                                            │
│                                                                             │
│    ┌──────────┐                                                             │
│    │ 线圈采样  │ → PB12 (线圈电流)                                          │
│    └──────────┘                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### 2.2.2 ADC通道映射表

| 引脚 | ADC实例 | ADC通道 | 物理量 | 采样电路 | 满量程 | 说明 |
|------|---------|---------|--------|----------|--------|------|
| **PB08** | ADC0 | CH11 | Buck-Boost输入电流 | 检流电阻 + INA240A2 | ±10A (示例) | 已依据 HPM5361 数据手册确认 |
| **PB10** | ADC0 | CH2 | 电感电流 IL | 检流电阻 + INA240A2 | ±10A (示例) | 已确认 |
| **PB11** | ADC0 | CH3 | Buck-Boost输出电压 | 电阻分压 | 0-30V (示例) | 已确认 |
| **PB12** | ADC0 | CH4 | 线圈电流 | 采样电路 | 待确认 | 已确认 |
| **PB13** | ADC0 | CH5 | LCC谐振电流 | 电流互感器 + 运放 | 待确认 | 已确认 |
| **PB14** | ADC0 | CH6 | Buck-Boost输入电压 | 电阻分压 | 0-30V (示例) | 已确认 |

#### 2.2.3 采样电路参数

| 采样电路 | 参数 | 典型值 | 说明 |
|----------|------|--------|------|
| **检流电阻** | 阻值 | 5-10mΩ | 低阻值减少功耗 |
| **INA240A2** | 增益 | 50V/V | 增益带宽积足够 |
| **INA240A2** | 带宽 | 400kHz | 满足200kHz采样 |
| **电流互感器** | 变比 | 1:50 或 1:100 | 根据电流范围选择 |
| **运放** | 带宽 | >1MHz | 信号调理 |
| **分压电阻** | 比例 | 1:10 或 1:20 | 匹配ADC量程 |

#### 2.2.4 ADC配置

```c
/* ADC通道定义（依据 HPM5361 数据手册）*/
#define ADC_CH_BUCK_BOOST_I_IN    (11U)   /* PB08: Buck-Boost输入母线电流  → ADC ch11 */
#define ADC_CH_INDUCTOR_I         (2U)    /* PB10: Buck-Boost电感电流     → ADC ch2  */
#define ADC_CH_BUCK_BOOST_V_OUT   (3U)    /* PB11: Buck-Boost输出电压     → ADC ch3  */
#define ADC_CH_COIL_I             (4U)    /* PB12: 发射线圈电流           → ADC ch4  */
#define ADC_CH_LCC_RESONANT_I     (5U)    /* PB13: LCC谐振电流            → ADC ch5  */
#define ADC_CH_BUCK_BOOST_V_IN    (6U)    /* PB14: Buck-Boost输入电压     → ADC ch6  */

/* ADC 配置（本项目统一使用 16-bit 分辨率）*/
#define ADC_VREF_MV               (3300.0f)       /* 参考电压 mV */
#define ADC_RES_MAX               (65535U)        /* 16-bit 满量程 */
```

### 2.3 TRGM资源

#### 2.3.1 触发路由配置

| 信号源 | 目标 | 用途 | 说明 |
|--------|------|------|------|
| **PWM0_SYNCI** | ADC0_STRGI | PWM0中心点触发ADC序列采样 | Buck-Boost控制 |
| **PWM0_SYNCI** | ADCX_PTRGI0A | PWM0中心点触发ADC抢占采样 | 电流环快速响应 |
| **PWM1_SYNCI** | ADCX_PTRGI1A | PWM1中心点触发ADC抢占采样 | LCC控制 |

#### 2.3.2 双PWM触发策略

由于有两个独立的PWM输出（PWM0和PWM1），需要考虑ADC采样的同步策略：

```
方案A：独立触发（推荐）
┌─────────────────────────────────────────────────────────────────┐
│  PWM0中心点 ──→ ADC抢占触发0 ──→ 采样Buck-Boost相关通道          │
│  PWM1中心点 ──→ ADC抢占触发1 ──→ 采样LCC相关通道                │
└─────────────────────────────────────────────────────────────────┘
优点：两个拓扑独立控制，互不干扰
缺点：需要两组触发配置

方案B：交替触发
┌─────────────────────────────────────────────────────────────────┐
│  PWM0中心点 ──→ ADC抢占触发0 ──→ 采样所有通道                    │
│  PWM1中心点 ──→ ADC抢占触发1 ──→ 采样所有通道                    │
└─────────────────────────────────────────────────────────────────┘
优点：采样频率加倍
缺点：控制周期不一致

方案C：主从触发
┌─────────────────────────────────────────────────────────────────┐
│  PWM0中心点 ──→ ADC抢占触发0 ──→ 采样所有通道（主）              │
│  PWM1中心点 ──→ 不触发ADC（从）                                  │
└─────────────────────────────────────────────────────────────────┘
优点：简化配置
缺点：PWM1控制延迟
```

**推荐方案A**：独立触发，两个拓扑独立控制。

#### 2.3.3 ADC采样分组

| 触发源 | ADC通道 | 物理量 | 控制环路 |
|--------|---------|--------|----------|
| **PWM0触发 (Buck-Boost)** | CH8 | Buck-Boost输入电流 | 输入电流保护 |
| | CH10 | 电感电流 | **电流环主反馈** |
| | CH11 | Buck-Boost输出电压 | **电压环主反馈** |
| | CH14 | Buck-Boost输入电压 | 前馈/保护 |
| **PWM1触发 (LCC)** | CH12 | 线圈电流 | 副边电流控制 |
| | CH13 | LCC谐振电流 | **谐振电流监测** |

---

## 3. 控制环路时序

### 3.1 单周期时序图

```
PWM周期 = 5us (@200kHz)

时间轴: 0        1        2        3        4        5us
        |--------|--------|--------|--------|--------|
        
Counter: 0 ──────→ 300 ──────→ 600 ──────→ 300 ──────→ 0
                   │         │         │         │
PWM输出:   Low     │  High   │  High   │  High   │  Low
                   │         │         │         │
                   │         ↑         │         │
                   │    PWM中心点      │         │
                   │    (Reload)       │         │
                   │         │         │         │
ADC触发:   ────────┴─────────↑─────────┴─────────┘
                            硬件触发
                   │         │         │         │
ADC采样:           │    ┌────┴────┐    │         │
                   │    │  ~1us  │    │         │
                   │    └────────┘    │         │
                   │         │         │         │
控制计算:          │         │    ┌────┴────┐    │
                   │         │    │  ~1us  │    │
                   │         │    └────────┘    │
                   │         │         │         │
PWM更新:           │         │         │    ┌────┴────┐
                   │         │         │    │ 下周期  │
                   │         │         │    │  生效   │
                   │         │         │    └────────┘
```

### 3.2 控制延迟分析

| 阶段 | 时间 | 说明 |
|------|------|------|
| PWM中心点 → ADC触发 | ~0ns | 硬件直连，无延迟 |
| ADC采样 | ~1us | 取决于采样周期配置 |
| ADC完成 → 中断响应 | ~100ns | 中断延迟 |
| 控制计算 | ~1us | PID计算 + 查表 |
| PWM更新 | 1个周期 | 下个周期生效 |
| **总延迟** | **~1个PWM周期** | 从采样到生效 |

---

## 4. 控制算法设计

### 4.1 增量式PID

```c
/*
 * 增量式PID控制器
 * 优点：无积分饱和问题，输出平滑
 */
typedef struct {
    float kp;              /* 比例系数 */
    float ki;              /* 积分系数 */
    float kd;              /* 微分系数 */
    float error[3];        /* error[0]=当前, error[1]=上次, error[2]=上上次 */
    float output;          /* 当前输出 */
    float output_max;      /* 输出上限 */
    float output_min;      /* 输出下限 */
} pid_incremental_t;

float pid_incremental_calculate(pid_incremental_t *pid, float target, float measured)
{
    pid->error[2] = pid->error[1];
    pid->error[1] = pid->error[0];
    pid->error[0] = target - measured;
    
    /* 增量计算 */
    float delta = pid->kp * (pid->error[0] - pid->error[1])
                + pid->ki * pid->error[0]
                + pid->kd * (pid->error[0] - 2.0f * pid->error[1] + pid->error[2]);
    
    pid->output += delta;
    
    /* 输出限幅 */
    if (pid->output > pid->output_max) {
        pid->output = pid->output_max;
    } else if (pid->output < pid->output_min) {
        pid->output = pid->output_min;
    }
    
    return pid->output;
}
```

### 4.2 位置式PID（带积分限幅）

```c
/*
 * 位置式PID控制器
 * 优点：响应快，易于理解
 * 缺点：需要积分限幅
 */
typedef struct {
    float kp;              /* 比例系数 */
    float ki;              /* 积分系数 */
    float kd;              /* 微分系数 */
    float integral;        /* 积分累积 */
    float integral_max;    /* 积分限幅 */
    float error_last;      /* 上次误差 */
    float output;          /* 当前输出 */
    float output_max;      /* 输出上限 */
    float output_min;      /* 输出下限 */
} pid_position_t;

float pid_position_calculate(pid_position_t *pid, float target, float measured)
{
    float error = target - measured;
    
    /* 积分累加 */
    pid->integral += error * pid->ki;
    
    /* 积分限幅 */
    if (pid->integral > pid->integral_max) {
        pid->integral = pid->integral_max;
    } else if (pid->integral < -pid->integral_max) {
        pid->integral = -pid->integral_max;
    }
    
    /* 微分项 */
    float derivative = error - pid->error_last;
    
    /* PID输出 */
    pid->output = pid->kp * error + pid->integral + pid->kd * derivative;
    
    /* 输出限幅 */
    if (pid->output > pid->output_max) {
        pid->output = pid->output_max;
    } else if (pid->output < pid->output_min) {
        pid->output = pid->output_min;
    }
    
    pid->error_last = error;
    
    return pid->output;
}
```

### 4.3 算法选择建议

| 算法 | 适用场景 | 优点 | 缺点 |
|------|----------|------|------|
| **增量式PID** | 电流环 | 无积分饱和，输出平滑 | 响应稍慢 |
| **位置式PID** | 电压环 | 响应快，易于调参 | 需要积分限幅 |
| **PI控制器** | 大多数场景 | 简单可靠 | 无微分项 |

**推荐方案**：
- 内环（电流环）：增量式PID或PI
- 外环（电压环）：位置式PI

---

## 5. 软件架构设计

### 5.1 分层架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    App/Logic/app_control.c                       │
│  控制环路业务逻辑：PID参数、控制策略、故障处理                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Interface/intf_control.h                       │
│  控制环路接口：init, start, stop, set_target                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Driver/hpm_impl/drv_control.c                 │
│  硬件配置：TRGM路由、ADC触发、中断配置                            │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ↓                     ↓                     ↓
┌───────────────┐    ┌───────────────┐    ┌───────────────┐
│  drv_hrpwm.c  │    │   drv_adc.c   │    │ drv_trgm.c    │
│  PWM驱动      │    │   ADC驱动     │    │ TRGM驱动      │
└───────────────┘    └───────────────┘    └───────────────┘
```

### 5.2 接口定义

```c
/* intf_control.h */

#ifndef INTF_CONTROL_H
#define INTF_CONTROL_H

#include <stdint.h>

/* 控制环路配置 */
typedef struct {
    uint32_t pwm_frequency_hz;      /* PWM频率 */
    float current_kp;               /* 电流环Kp */
    float current_ki;               /* 电流环Ki */
    float current_kd;               /* 电流环Kd */
    float voltage_kp;               /* 电压环Kp */
    float voltage_ki;               /* 电压环Ki */
    float current_max;              /* 最大电流 (mA) */
    float voltage_max;              /* 最大电压 (mV) */
    float duty_max;                 /* 最大占空比 */
    float duty_min;                 /* 最小占空比 */
} intf_control_cfg_t;

/* 控制环路状态 */
typedef struct {
    float target_current;           /* 目标电流 (mA) */
    float target_voltage;           /* 目标电压 (mV) */
    float measured_current;         /* 测量电流 (mA) */
    float measured_voltage;         /* 测量电压 (mV) */
    float duty_cycle;               /* 当前占空比 */
    uint32_t cycle_count;           /* 控制周期计数 */
} intf_control_status_t;

/* 控制环路回调 */
typedef void (*intf_control_callback_t)(const intf_control_status_t *status);

/* API */
int intf_control_init(const intf_control_cfg_t *cfg);
int intf_control_start(void);
int intf_control_stop(void);
int intf_control_set_target_current(float current_ma);
int intf_control_set_target_voltage(float voltage_mv);
int intf_control_get_status(intf_control_status_t *status);
int intf_control_register_callback(intf_control_callback_t callback);

#endif /* INTF_CONTROL_H */
```

---

## 6. 关键设计决策

### 6.1 触发方式选择

| 方案 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| **方案A：PWM中断触发ADC** | 实现简单 | 延迟大，抖动大 | ★★☆ |
| **方案B：硬件触发ADC** | 延迟小，确定性好 | 需要TRGM配置 | ★★★ |
| **方案C：DMA传输ADC** | CPU占用最低 | 实现复杂 | ★★☆ |

**推荐方案B**：使用TRGM将PWM信号路由到ADC触发输入。

### 6.2 ADC采样模式

| 模式 | 说明 | 适用场景 |
|------|------|----------|
| **序列模式** | 按顺序采样多个通道 | 低速采样 |
| **抢占模式** | 高优先级触发打断低优先级 | 电流环（推荐） |

**推荐抢占模式**：允许紧急触发打断常规采样。

### 6.3 控制周期选择

| 方案 | 控制周期 | 优点 | 缺点 |
|------|----------|------|------|
| **每个PWM周期** | 5us | 响应最快 | 计算压力大 |
| **每2个PWM周期** | 10us | 平衡方案 | 响应稍慢 |
| **每4个PWM周期** | 20us | 计算宽松 | 响应慢 |

**推荐每个PWM周期**：电流环需要快速响应。

---

## 7. 待讨论问题

### 7.1 硬件相关

- [x] ADC引脚分配已确认（PB08/PB10/PB11/PB12/PB13/PB14）
- [ ] ADC通道号需要根据原理图最终确认
- [ ] 检流电阻阻值和INA240A2增益需要确认
- [ ] 电流互感器变比和运放增益需要确认
- [ ] 电压分压电阻比例需要确认
- [ ] ADC参考电压选择（内部/外部）？
- [ ] 是否需要ADC校准？

### 7.2 软件相关

- [ ] 两个PWM的ADC触发策略确认（独立触发/交替触发/主从触发）
- [ ] PID参数如何整定？
- [ ] 是否需要前馈控制？
- [ ] 故障保护策略（过流、过压、欠压）？
- [ ] 软启动策略？
- [ ] Buck-Boost和LCC是否需要独立控制？

### 7.3 测试相关

- [ ] 如何验证控制环路稳定性？
- [ ] 阶跃响应测试方法？
- [ ] 负载扰动测试方法？

---

## 8. 参考资料

1. **HPM5361 TRM** - PWM、ADC、TRGM寄存器说明
2. **hpm_sdk/samples/motor_ctrl/bldc_foc/** - 电机控制示例
3. **hpm_sdk/drivers/inc/hpm_adc16_drv.h** - ADC驱动API
4. **hpm_sdk/drivers/inc/hpm_trgm_drv.h** - TRGM驱动API

---

## 9. 修订记录

| 日期 | 版本 | 说明 |
|------|------|------|
| 2026-05-31 | v0.1 | 初始设计文档 |
| 2026-05-31 | v0.2 | 添加ADC采样通道分配表格，明确硬件拓扑和采样电路 |
