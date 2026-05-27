# HRPWM 驱动设计说明

本文描述当前工程中 HRPWM (High-Performance PWM) 驱动的设计边界、接口契约、硬件映射和开发指南。

> **当前实现状态**：驱动骨架已完成，`Interface -> Driver` 解耦调用链已建立。HPM SDK 相关类型封装在 `Driver/hpm_impl` 内部。
>
> **重要说明**：HPM5361 的 `PWM_SOC_HRPWM_SUPPORT = 0`，**不支持真正的亚时钟级 HRPWM**。当前 `hrpwm` 实现基于普通 `HPM_PWM0` API，命名保留为 `hrpwm` 是为了后续面向功率 PWM/高性能 PWM 的接口演进。

---

## 1. 分层目标

本设计遵循工程 `AGENTS.md` 中的"物理隔离 + 契约驱动"原则：

| 层级 | 当前文件 | 职责 |
|------|----------|------|
| `Interface/` | `intf_hrpwm.h` | 定义纯 C 契约，不暴露 `hpm_*` 类型 |
| `Interface/` | `intf_default.c` | 接口注册/分发实现，保存 ops 指针 |
| `Driver/hpm_impl/` | `drv_hrpwm.c` | 将接口调用映射到 HPM SDK PWM API |
| `App/` | 暂无 HRPWM app wrapper | 当前阶段不引入 App 调用封装，专注驱动侧 |

当前保留的注册入口：

```c
void hpm_hrpwm_driver_register(void);
```

该函数位于 Driver 层。后续应由系统初始化层或专门的 driver bootstrap 调用，避免 App 直接依赖 Driver 符号。

---

## 2. HRPWM 硬件特性

### 2.1 HPM5361 PWM 特性

```c
#define HRPWM_BASE HPM_PWM0
#define HRPWM_CLOCK_NAME clock_mot0
#define HRPWM_CHANNEL_COUNT (4U)
```

**SoC 特性**：
- `PWM_SOC_HRPWM_SUPPORT = 0` - 不支持真正的高分辨率 PWM
- `PWM_SOC_PWM_MAX_COUNT = 8` - 最大 PWM 通道数
- `PWM_SOC_CMP_MAX_COUNT = 24` - 最大比较器数
- `PWM_SOC_TIMER_RESET_SUPPORT = 1` - 支持定时器复位

### 2.2 性能参数

| 参数 | 值 | 说明 |
|------|-----|------|
| **时钟源** | `clock_mot0` (AHB) | 120 MHz |
| **时间分辨率** | 8.33 ns | 1/120MHz |
| **PWM 周期 @200kHz** | 600 计数 | 9-bit 有效分辨率 |
| **PWM 周期 @148kHz** | 811 计数 | 9.7-bit 有效分辨率 |
| **死区最小步进** | 4.17 ns | 半周期单位 |
| **最大死区** | ~1063 ns | 255 个半周期 |

### 2.3 与 STM32G4 HRTIM 对比

| 指标 | HPM5361 PWM | STM32G4 HRTIM | 差距 |
|------|-------------|---------------|------|
| **时间分辨率** | 8.33 ns | 184 ps | 45 倍 |
| **DPWM 分辨率 @200kHz** | 9-bit | 14.4-bit | 16 倍 |
| **死区精度** | 4.17 ns | 0.735 ns | 5.7 倍 |
| **Fault 延迟** | ~100 ns (中断) | <10 ns (异步) | 10 倍 |

---

## 3. 对外接口契约

文件：`Interface/intf_hrpwm.h`

### 3.1 类型定义

```c
typedef uint8_t intf_hrpwm_ch_t;

typedef struct {
    uint32_t frequency_hz;
    float duty;                 // 归一化占空比 [0.0-1.0]
    bool invert_output;
} intf_hrpwm_cfg_t;

typedef struct {
    uint8_t instance_id;
    struct {
        int (*init)(intf_hrpwm_ch_t ch, const intf_hrpwm_cfg_t *cfg);
        int (*set_duty)(intf_hrpwm_ch_t ch, float duty);
        int (*set_frequency)(intf_hrpwm_ch_t ch, uint32_t frequency_hz);
        int (*start)(intf_hrpwm_ch_t ch);
        int (*stop)(intf_hrpwm_ch_t ch);
    };
} intf_hrpwm_t;
```

### 3.2 功能 API

```c
int intf_hrpwm_register(const intf_hrpwm_t *ops);
int intf_hrpwm_init(intf_hrpwm_ch_t ch, const intf_hrpwm_cfg_t *cfg);
int intf_hrpwm_set_duty(intf_hrpwm_ch_t ch, float duty);
int intf_hrpwm_set_frequency(intf_hrpwm_ch_t ch, uint32_t frequency_hz);
int intf_hrpwm_start(intf_hrpwm_ch_t ch);
int intf_hrpwm_stop(intf_hrpwm_ch_t ch);
```

### 3.3 参数约束

| 参数 | 约束 | 说明 |
|------|------|------|
| `frequency_hz` | `> 0` 且小于外设时钟 | 驱动会根据外设时钟计算 reload |
| `duty` | `0.0f <= duty <= 1.0f`，且不能是 NaN | 归一化占空比，驱动层负责转换 |
| `invert_output` | `true/false` | 反相输出控制 |
| `ch` | `0..3` | 对应当前板级 `PWM0_P_0..3` |

错误返回：`0` 表示成功，`-1` 表示参数、状态或底层 SDK 调用失败。

---

## 4. 板级通道映射

当前 `Board/HPM5361_WirelessCharger_board/pinmux.c` 中与 HRPWM 相关的配置：

| Interface 通道 | HPM 外设 | 板级引脚 | pinmux 函数 |
|----------------|----------|----------|-------------|
| `hrpwm ch0` | `HPM_PWM0 P0` | PA24 | `init_pwm0_pins()` |
| `hrpwm ch1` | `HPM_PWM0 P1` | PA25 | `init_pwm0_pins()` |
| `hrpwm ch2` | `HPM_PWM0 P2` | PA26 | `init_pwm0_pins()` |
| `hrpwm ch3` | `HPM_PWM0 P3` | PA27 | `init_pwm0_pins()` |

板上还配置了 PA28~PA31 为 `PWM1_P_4..7`，但当前 `drv_hrpwm.c` 固定使用 `HPM_PWM0`，因此未开放 `ch4..ch7`。

---

## 5. HRPWM 驱动设计

文件：`Driver/hpm_impl/drv_hrpwm.c`

### 5.1 状态模型

```c
typedef struct {
    bool configured;
    uint32_t frequency_hz;
    float duty;
    bool invert_output;
    uint32_t reload;
} hrpwm_state_t;

static hrpwm_state_t hrpwm_state[PWM_SOC_PWM_MAX_COUNT];
static uint32_t hrpwm_reload;
static uint32_t hrpwm_frequency_hz;
```

设计要点：

- `HPM_PWM0` 的 counter/reload 是外设实例级资源，不是单通道资源。
- 因此 `hrpwm_reload` 和 `hrpwm_frequency_hz` 作为全局实例状态保存。
- 每个通道仅保存自己的 duty、反相配置和 configured 状态。

### 5.2 初始化流程

`hrpwm_init(ch, cfg)`：

1. 检查 `cfg != NULL`。
2. 检查 `ch < HRPWM_CHANNEL_COUNT`。
3. 检查 `duty` 有效且非 NaN。
4. 根据 `frequency_hz` 计算 `hrpwm_reload`。
5. 调用 `pwm_set_reload()` 和 `pwm_set_start_count()`。
6. 通过 `pwm_get_default_pwm_config()` 获取默认配置。
7. 设置 `enable_output = false`，避免 init 阶段立即输出。
8. 设置 compare 为 output compare 模式。
9. 调用 `pwm_setup_waveform()` 绑定 PWM channel 和 compare。
10. 保存通道状态并应用 duty。

### 5.3 启停策略

`hrpwm_start(ch)`：

```c
pwm_enable_output(HRPWM_BASE, ch);
pwm_start_counter(HRPWM_BASE);
pwm_issue_shadow_register_lock_event(HRPWM_BASE);
```

`hrpwm_stop(ch)`：

```c
pwm_disable_output(HRPWM_BASE, ch);
```

注意：`stop(ch)` 只关闭该通道输出，不停止整个 `HPM_PWM0` counter，避免影响同一 PWM 实例上的其他通道。

### 5.4 改频语义

`intf_hrpwm_set_frequency(ch, frequency_hz)` 虽然保留了 `ch` 参数以匹配当前接口形态，但底层 `HPM_PWM0` 频率是实例级的：

- 任意通道改频都会改变 `HPM_PWM0` 的全局 reload。
- 驱动会遍历已配置通道并重新应用 duty。
- 后续若需要更准确的契约，应考虑将 HRPWM 改为 instance-level frequency API。

---

## 6. 注册与调用链

Driver 层提供注册函数：

```c
void hpm_hrpwm_driver_register(void)
{
    intf_hrpwm_register(&hrpwm_ops);
}
```

接口分发位于 `Interface/intf_default.c`：

```c
static const intf_hrpwm_t *hrpwm_ops = NULL;
```

典型调用链：

```text
系统初始化/Driver bootstrap
  -> hpm_hrpwm_driver_register()

上层模块
  -> intf_hrpwm_init()
  -> intf_hrpwm_set_duty()
  -> intf_hrpwm_start()
```

当前尚未设计最终 bootstrap 位置。为保持分层，后续不建议由 App 直接声明并调用 `hpm_*_driver_register()`。

---

## 7. 安全策略与限制

当前骨架已包含以下基本保护：

- 通道范围检查。
- 频率为 0 时拒绝初始化/改频。
- duty 必须在 `[0.0f, 1.0f]` 且不能是 NaN。
- 初始化阶段默认不使能输出，必须显式调用 `start()`。
- `hrpwm_stop(ch)` 不停止全局 counter，避免影响其他通道。

当前仍未完成的功率级安全能力：

- HRPWM 未配置 fault source。
- HRPWM 未配置 fault recovery 策略。
- HRPWM deadtime 暂固定为 0。
- 未封装互补输出 pair 模式。
- HRPWM 未设计 force-safe-low / brake API。

因此：**当前 `hrpwm` 骨架不应直接用于真实功率桥驱动闭环，只适合作为接口和驱动映射基础。**

---

## 8. HPM SDK PWM 示例参考

### 8.1 PWM 输出示例（包含抖动技术）

**位置**：`/workspace/hpm_sdk/samples/drivers/pwm/pwm_output/`

**关键功能**：
- ✅ **带抖动的边沿对齐 PWM** - DPWM 抖动技术
- ✅ 边沿对齐 PWM
- ✅ 中心对齐 PWM
- ✅ 互补 PWM + 死区
- ✅ 失效保护模式
- ✅ 强制输出

**抖动实现代码**：
```c
// 关键代码：配置 jitter_cmp 实现抖动
pwm_cmp_config_t cmp_config = {0};
cmp_config.mode = pwm_cmp_mode_output_compare;
cmp_config.cmp = reload + 1;
cmp_config.jitter_cmp = 4;  // 抖动比较值，实现 1/256 时钟周期精度
cmp_config.update_trigger = pwm_shadow_register_update_on_hw_event;
```

**抖动原理**：
- `jitter_cmp` 控制抖动计数器的比较值，范围 0-255
- 硬件自动在相邻 PWM 周期之间微调占空比
- 有效分辨率从 9-bit 提高到约 **12-bit**（增加 3-bit）

**抖动效果**：
```
传统 PWM (9-bit @ 200kHz):
├── 周期 1: 占空比 = 300/600 = 50.000%
├── 周期 2: 占空比 = 300/600 = 50.000%
└── 分辨率: 1/600 = 0.167%

带抖动 PWM (12-bit @ 200kHz):
├── 周期 1: 占空比 = 300/600 = 50.000%
├── 周期 2: 占空比 = 301/600 = 50.167%
├── 周期 3: 占空比 = 300/600 = 50.000%
├── 周期 4: 占空比 = 301/600 = 50.167%
└── 平均占空比 = 50.083% (有效分辨率提高 4 倍)
```

### 8.2 HRPWM 输出示例

**位置**：`/workspace/hpm_sdk/samples/drivers/pwm/hrpwm/`

**关键功能**：
- ✅ 强制输出模式
- ✅ 失效模式 + 自动恢复
- ✅ 边沿对齐 PWM（占空比 0-100%）
- ✅ 中心对齐 PWM
- ✅ 频率可变 PWM

**Fault 保护配置**：
```c
void config_pwm_fault_capture(void)
{
    pwm_cmp_config_t cmp_config;
    trgm_output_t trgm_output_cfg;

    cmp_config.mode = pwm_cmp_mode_input_capture;
    pwm_config_cmp(HRPWM, BOARD_APP_HRPWM_FAULT_CAP_CMP_INDEX, &cmp_config);
    intc_m_enable_irq_with_priority(BOARD_APP_HRPWM_IRQ, 1);
    pwm_enable_irq(HRPWM, PWM_IRQ_CMP(BOARD_APP_HRPWM_FAULT_CAP_CMP_INDEX));

    trgm_output_cfg.invert = false;
    trgm_output_cfg.type   = trgm_output_same_as_input;
    trgm_output_cfg.input  = BOARD_APP_HRPWM_FAULT_TRGM_SRC;
    trgm_output_config(TRGM, BOARD_APP_HRPWM_FAULT_TRGM_OUTPUT, &trgm_output_cfg);
}
```

**Fault 恢复 ISR**：
```c
SDK_DECLARE_EXT_ISR_M(BOARD_APP_HRPWM_IRQ, isr_pwm)
void isr_pwm(void)
{
    uint32_t status;
    status = pwm_get_status(HRPWM);
    pwm_clear_status(HRPWM, status);
    if ((status & PWM_IRQ_CMP(BOARD_APP_HRPWM_FAULT_CAP_CMP_INDEX))) {
        pwm_recovery_hrpwm_output(HRPWM);
    }
}
```

### 8.3 HRPWM 校准示例

**位置**：`/workspace/hpm_sdk/samples/drivers/pwmv2/hrpwm_calibrate/`

**关键功能**：
- ✅ 温度补偿校准
- ✅ 延迟链校准
- ✅ 定期自动校准

**校准初始化**：
```c
void hrpwm_init_calibration(TRGM_Type *trgm)
{
    uint8_t times = 0;
    trgm_pwmv2_calibration_mode_t calibration_mode = trgm_pwmv2_calibration_mode_begin;
    
    while (calibration_mode != trgm_pwmv2_calibration_mode_end) {
        trgm_pwmv2_calibrate_delay_chain(trgm, &calibration_mode);
        board_delay_us(10);
        times++;
        if (times > TEST_LOOP) {
            printf("calibration failed\n");
            while (1);
        }
    }
}
```

**定期校准（使用 GPTMR 定时器）**：
```c
void tick_ms_isr(void)
{
    if (gptmr_check_status(GPTMR, GPTMR_CH_RLD_STAT_MASK(GPTMR_CH))) {
        gptmr_clear_status(GPTMR, GPTMR_CH_RLD_STAT_MASK(GPTMR_CH));
        trgm_pwmv2_calibrate_delay_chain(TRGM, &calibration_mode);
        
        if (calibration_mode == trgm_pwmv2_calibration_mode_end) {
            gptmr_disable_irq(GPTMR, GPTMR_CH_RLD_IRQ_MASK(GPTMR_CH));
            printf("calibration done\n");
        }
    }
}
```

### 8.4 电机控制 FOC 示例

**位置**：`/workspace/hpm_sdk/samples/motor_ctrl/bldc_foc/`

**关键配置**：
```c
#define PWM_FREQUENCY               (20000)      // 20 kHz PWM
#define PWM_RELOAD                  ((motor_clock_hz/PWM_FREQUENCY) - 1)
#define PWM_DEAD_AREA_TICK          (100)        // 死区 100 个时钟周期
```

**适用场景**：
- 电机控制中的 PWM 配置
- 死区配置
- ADC 同步采样

---

## 9. HRPWM 高级功能开发指南

### 9.1 抖动技术实现（提高 DPWM 分辨率）

**API**：
```c
// 更新抖动比较值
static inline void pwm_cmp_update_jitter_value(PWM_Type *pwm_x, uint8_t index, uint8_t jitter)
{
    pwm_x->CMP[index] = (pwm_x->CMP[index] & ~PWM_CMP_CMPJIT_MASK) | PWM_CMP_CMPJIT_SET(jitter);
}
```

**实现代码**：
```c
void setup_pwm_with_jitter(PWM_Type *pwm, uint8_t ch, uint32_t frequency_hz, float duty)
{
    uint32_t clock_hz = clock_get_frequency(clock_mot0);
    uint32_t reload = clock_hz / frequency_hz - 1;
    
    // 配置 PWM
    pwm_config_t pwm_config = {0};
    pwm_get_default_pwm_config(pwm, &pwm_config);
    pwm_config.enable_output = true;
    pwm_config.dead_zone_in_half_cycle = 0;
    
    // 配置比较器，启用抖动
    pwm_cmp_config_t cmp_config = {0};
    cmp_config.mode = pwm_cmp_mode_output_compare;
    cmp_config.cmp = (uint32_t)(reload * duty);
    cmp_config.jitter_cmp = 4;  // 抖动值，范围 0-255
    cmp_config.update_trigger = pwm_shadow_register_update_on_modify;
    
    // 应用配置
    pwm_set_reload(pwm, 0, reload);
    pwm_set_start_count(pwm, 0, 0);
    pwm_setup_waveform(pwm, ch, &pwm_config, ch, &cmp_config, 1);
}
```

**抖动值选择建议**：
| jitter_cmp | 抖动幅度 | 有效分辨率提升 | 适用场景 |
|------------|----------|----------------|----------|
| 0 | 无抖动 | 0-bit | 对分辨率要求不高 |
| 1 | 最小抖动 | ~1-bit | 轻微改善 |
| 4 | 中等抖动 | ~2-bit | 通用推荐 |
| 16 | 较大抖动 | ~3-bit | 高分辨率需求 |
| 255 | 最大抖动 | ~4-bit | 极致分辨率 |

### 9.2 Fault 保护配置

**Fault 源定义**：
```c
typedef enum pwm_fault_source {
    pwm_fault_source_internal_0 = PWM_GCR_FAULTI0EN_MASK,  // FAULTI0
    pwm_fault_source_internal_1 = PWM_GCR_FAULTI1EN_MASK,  // FAULTI1
    pwm_fault_source_internal_2 = PWM_GCR_FAULTI2EN_MASK,  // FAULTI2
    pwm_fault_source_internal_3 = PWM_GCR_FAULTI3EN_MASK,  // FAULTI3
    pwm_fault_source_external_0 = PWM_GCR_FAULTE0EN_MASK,  // EXFAULTI0
    pwm_fault_source_external_1 = PWM_GCR_FAULTE1EN_MASK,  // EXFAULTI1
    pwm_fault_source_debug = PWM_GCR_DEBUGFAULT_MASK,      // Debug fault
} pwm_fault_source_t;
```

**Fault 模式定义**：
```c
typedef enum pwm_fault_mode {
    pwm_fault_mode_force_output_0 = 0,      // fault 时强制输出低
    pwm_fault_mode_force_output_1 = 1,      // fault 时强制输出高
    pwm_fault_mode_force_output_highz = 2,  // fault 时输出高阻
} pwm_fault_mode_t;
```

**Fault 恢复触发**：
```c
typedef enum pwm_fault_recovery_trigger {
    pwm_fault_recovery_immediately = 0,      // 立即恢复
    pwm_fault_recovery_on_reload = 1,        // PWM 周期结束后恢复
    pwm_fault_recovery_on_hw_event = 2,      // 硬件事件触发恢复
    pwm_fault_recovery_on_fault_clear = 3,   // 软件清除 fault 后恢复
} pwm_fault_recovery_trigger_t;
```

**配置示例**：
```c
void config_fault_protection(PWM_Type *pwm, uint8_t ch)
{
    // 配置 PWM 输出
    pwm_config_t pwm_config = {0};
    pwm_get_default_pwm_config(pwm, &pwm_config);
    pwm_config.enable_output = true;
    pwm_config.fault_mode = pwm_fault_mode_force_output_0;  // fault 时强制输出低
    pwm_config.fault_recovery_trigger = pwm_fault_recovery_on_fault_clear;
    
    // 配置 fault 源
    pwm_fault_source_config_t fault_config = {0};
    fault_config.source_mask = pwm_fault_source_external_0;
    fault_config.fault_recover_at_rising_edge = false;
    fault_config.fault_external_0_active_low = true;
    pwm_config_fault_source(pwm, &fault_config);
}
```

### 9.3 Deadtime 配置

**API**：
```c
// dead_zone_in_half_cycle: 死区时间，单位为半个 PWM 时钟周期
pwm_config.dead_zone_in_half_cycle = 100;  // 死区 100 个半周期 = 833 ns @ 120MHz
```

**死区计算**：
```
死区时间 = dead_zone_in_half_cycle × (1 / (2 × PWM_CLK))
示例: 100 × (1 / (2 × 120MHz)) = 100 × 4.17ns = 417ns
```

**典型死区值**：
| 应用 | 死区需求 | dead_zone_in_half_cycle |
|------|----------|-------------------------|
| 低功率 MOSFET | 50-100 ns | 12-24 |
| 中功率 IGBT | 200-500 ns | 48-120 |
| 高功率 SiC | 100-200 ns | 24-48 |

### 9.4 互补输出 Pair 模式

**API**：
```c
pwm_setup_waveform_in_pair(PWM_Type *pwm_x, uint8_t pin_pair, 
                           const pwm_pair_config_t *config,
                           uint8_t cmp_start_index, 
                           const pwm_cmp_config_t *cmp_config, 
                           uint8_t cmp_count);
```

**配置示例**：
```c
void setup_complementary_pwm(PWM_Type *pwm, uint8_t ch, uint32_t frequency_hz, float duty)
{
    uint32_t clock_hz = clock_get_frequency(clock_mot0);
    uint32_t reload = clock_hz / frequency_hz - 1;
    
    // 配置 PWM pair
    pwm_pair_config_t pair_config = {0};
    pwm_get_default_pwm_pair_config(pwm, &pair_config);
    
    // 通道 0 配置
    pair_config.pwm[0].enable_output = true;
    pair_config.pwm[0].dead_zone_in_half_cycle = 100;  // 死区
    pair_config.pwm[0].invert_output = false;
    
    // 通道 1 配置（互补）
    pair_config.pwm[1].enable_output = true;
    pair_config.pwm[1].dead_zone_in_half_cycle = 100;  // 死区
    pair_config.pwm[1].invert_output = true;  // 反相输出
    
    // 配置比较器
    pwm_cmp_config_t cmp_config[2] = {0};
    cmp_config[0].mode = pwm_cmp_mode_output_compare;
    cmp_config[0].cmp = (uint32_t)(reload * duty);
    cmp_config[0].update_trigger = pwm_shadow_register_update_on_modify;
    
    cmp_config[1].mode = pwm_cmp_mode_output_compare;
    cmp_config[1].cmp = reload;
    cmp_config[1].update_trigger = pwm_shadow_register_update_on_modify;
    
    // 应用配置
    pwm_set_reload(pwm, 0, reload);
    pwm_set_start_count(pwm, 0, 0);
    pwm_setup_waveform_in_pair(pwm, ch, &pair_config, 0, cmp_config, 2);
}
```

### 9.5 Force-safe-low / Brake API

**实现代码**：
```c
int intf_hrpwm_force_low(intf_hrpwm_ch_t ch)
{
    if (!hrpwm_is_valid_channel(ch)) return -1;
    
    // 使用软件强制输出
    pwm_enable_pwm_sw_force_output(HRPWM_BASE, ch);
    pwm_set_force_output(HRPWM_BASE, 
        PWM_FORCE_OUTPUT(ch, pwm_output_0));
    
    return 0;
}

int intf_hrpwm_force_release(intf_hrpwm_ch_t ch)
{
    if (!hrpwm_is_valid_channel(ch)) return -1;
    
    // 禁用软件强制输出
    pwm_disable_pwm_sw_force_output(HRPWM_BASE, ch);
    
    return 0;
}
```

### 9.6 ADC 同步采样

**配置 PWM 触发 ADC**：
```c
void config_pwm_adc_trigger(PWM_Type *pwm, uint8_t cmp_index, uint32_t trigger_point)
{
    // 配置比较器用于 ADC 触发
    pwm_cmp_config_t cmp_config = {0};
    cmp_config.mode = pwm_cmp_mode_output_compare;
    cmp_config.cmp = trigger_point;
    cmp_config.update_trigger = pwm_shadow_register_update_on_modify;
    pwm_config_cmp(pwm, cmp_index, &cmp_config);
    
    // 配置 TRGM 连接 PWM 到 ADC
    trgm_output_t trgm_output_cfg = {0};
    trgm_output_cfg.invert = false;
    trgm_output_cfg.type = trgm_output_same_as_input;
    trgm_output_cfg.input = TRGM_INPUT_PWM_CMP0 + cmp_index;
    trgm_output_config(TRGM, TRGM_OUTPUT_ADC_TRIG0, &trgm_output_cfg);
}
```

---

## 10. 数字电源控制应用指南

### 10.1 200kHz Buck-Boost 控制

**配置示例**：
```c
void init_buckboost_pwm(void)
{
    uint32_t frequency_hz = 200000;  // 200kHz
    float duty = 0.5f;               // 50% 占空比
    
    // 启用抖动提高分辨率
    setup_pwm_with_jitter(HPM_PWM0, 0, frequency_hz, duty);
    
    // 配置互补输出 + 死区
    setup_complementary_pwm(HPM_PWM0, 0, frequency_hz, duty);
    
    // 配置故障保护
    config_fault_protection(HPM_PWM0, 0);
    
    // 配置 ADC 同步采样
    uint32_t reload = 120000000 / frequency_hz - 1;
    config_pwm_adc_trigger(HPM_PWM0, 4, reload / 2);  // 在占空比中间采样
}
```

**分辨率分析**：
| 配置 | 有效分辨率 | 说明 |
|------|-----------|------|
| 无抖动 | 9-bit (600级) | 边缘可用 |
| jitter_cmp=4 | ~11-bit (2400级) | 推荐配置 |
| jitter_cmp=16 | ~12-bit (9600级) | 高分辨率需求 |

### 10.2 148kHz 无线充电全桥控制

**配置示例**：
```c
void init_wireless_charging_pwm(void)
{
    uint32_t frequency_hz = 148000;  // 148kHz
    
    // 配置 PWM0 用于原边逆变
    setup_pwm_with_jitter(HPM_PWM0, 0, frequency_hz, 0.5f);
    setup_complementary_pwm(HPM_PWM0, 0, frequency_hz, 0.5f);
    
    // 配置 PWM1 用于副边整流（相移控制）
    setup_pwm_with_jitter(HPM_PWM1, 0, frequency_hz, 0.5f);
    
    // 使用 TRGM 同步 PWM0 和 PWM1
    trgm_output_update_source(TRGM, TRGM_TRGOCFG_PWM_SYNCI, 1);
    trgm_output_update_source(TRGM, TRGM_TRGOCFG_PWM_SYNCI, 0);
    
    // 配置故障保护
    config_fault_protection(HPM_PWM0, 0);
    config_fault_protection(HPM_PWM1, 0);
}
```

**相移控制**：
```c
void set_phase_shift(uint32_t phase_shift_deg)
{
    // 将相移角度转换为计数值
    uint32_t reload = 120000000 / 148000 - 1;
    uint32_t phase_shift_count = (uint32_t)((float)reload * phase_shift_deg / 360.0f);
    
    // 配置 PWM1 的起始计数值
    pwm_set_start_count(HPM_PWM1, 0, phase_shift_count);
}
```

---

## 11. 开发检查清单

开发 HRPWM 驱动新功能时，请按以下清单检查：

- [ ] 接口定义符合 AGENTS.md 规范（匿名结构体、参数归一化）
- [ ] 驱动实现不暴露 `hpm_*` 类型到 Interface 层
- [ ] 通道范围校验完整
- [ ] duty 参数包含 NaN 防护
- [ ] 初始化阶段不使能输出，需显式调用 `start()`
- [ ] `stop()` 不影响同实例其他通道
- [ ] 频率变化时重新计算并应用所有已配置通道的 duty
- [ ] 配置 Fault 保护（功率应用必需）
- [ ] 配置死区时间（互补输出必需）
- [ ] 使用抖动技术提高分辨率（数字电源必需）
- [ ] 配置 ADC 同步采样（闭环控制必需）

---

## 12. 后续开发计划

### 12.1 高优先级

1. **增加 fault/deadtime/互补输出配置**
   面向无线充功率级时必须补齐：
   - deadtime
   - complementary pair
   - fault source
   - safe output state
   - fault recovery

2. **HRPWM force-safe-low / brake API**
   实现功率级安全关断功能。

3. **抖动技术集成**
   将抖动技术集成到 drv_hrpwm.c，提高 DPWM 有效分辨率。

### 12.2 中优先级

4. **补充 driver bootstrap**
   增加统一驱动注册入口，例如 `hpm_drivers_register_all()`。

5. **将 HRPWM 频率改为 instance-level 契约**
   当前接口为 `set_frequency(ch, freq)`，但底层频率全局共享。

6. **完善板级映射表**
   将通道、外设、引脚、用途集中定义到 Board 或 Driver 私有映射表中。

### 12.3 低优先级

7. **增加硬件验证用例**
   用示波器验证：
   - `hrpwm ch0..3` 输出频率/占空比
   - 互补输出死区
   - Fault 保护响应
   - `stop()` 后输出是否进入预期安全态

8. **C17 特性应用**
   - `static_assert`：编译期校验通道数、缓冲区大小
   - `_Generic`：类型安全的接口宏
   - `_Alignas`：DMA 缓冲区 L1 Cache 对齐

9. **性能优化**
   - ILM 部署：将关键函数放入 ILM
   - Cache 对齐：ADC DMA 缓冲区 `_Alignas(32)`
   - RAMFUNC：中断处理函数、PID 控制器放入 RAM
