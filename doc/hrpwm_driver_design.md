# HRPWM 驱动设计说明

本文描述当前工程中 HRPWM (High-Performance PWM) 驱动的设计边界、接口契约、硬件映射和开发指南。

> **当前实现状态**：驱动已完成，支持双PWM实例（PWM0和PWM1）的四对通道互补输出。`Interface -> Driver` 解耦调用链已建立。
>
> **重要说明**：HPM5361 的 `PWM_SOC_HRPWM_SUPPORT = 0`，**不支持真正的亚时钟级 HRPWM**。当前 `hrpwm` 实现基于普通 `HPM_PWM0/1` API，命名保留为 `hrpwm` 是为了后续面向功率 PWM/高性能 PWM 的接口演进。

---

## 1. 分层目标

本设计遵循工程 `AGENTS.md` 中的"物理隔离 + 契约驱动"原则：

| 层级 | 当前文件 | 职责 |
|------|----------|------|
| `Interface/` | `intf_hrpwm.h` | 定义纯 C 契约，不暴露 `hpm_*` 类型 |
| `Interface/` | `intf_default.c` | 接口注册/分发实现，保存 ops 指针 |
| `Driver/hpm_impl/` | `drv_hrpwm.c` | 将接口调用映射到 HPM SDK PWM API |
| `App/Logic/` | `app_hrpwm.c/.h` | HRPWM App 封装，提供 pwm_pair_t 面向对象 API，仅通过 Interface 访问 |

当前保留的注册入口：

```c
void hpm_hrpwm_driver_register(void);
```

该函数位于 Driver 层，由各 App init 函数内部调用（如 `pwm_init()` 内部调用 `hpm_hrpwm_driver_register()`），遵循 `app_gpio.c` 已有的注册范例。`main.c` 不直接调用 Driver 层符号。

---

## 2. HRPWM 硬件特性

### 2.1 HPM5361 PWM 特性

**双PWM实例**：
- `HPM_PWM0` - 4通道（ch0-ch3），引脚 PA24-PA27
- `HPM_PWM1` - 4通道（ch4-ch7），引脚 PA28-PA31

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
typedef uint8_t intf_hrpwm_inst_t;  // PWM实例ID (0=PWM0, 1=PWM1)
typedef uint8_t intf_hrpwm_ch_t;    // 通道索引 (0-7, 0-3=PWM0, 4-7=PWM1)

typedef enum {
    INTF_HRPWM_ALIGN_EDGE = 0,
    INTF_HRPWM_ALIGN_CENTER,
} intf_hrpwm_align_t;

typedef struct {
    uint32_t frequency_hz;
    float duty;                     // 归一化占空比 [0.0-1.0]
    uint32_t deadtime_ns;           // 死区时间(ns)
    uint8_t jitter_cmp;             // 抖动计数器比较值
    intf_hrpwm_align_t align;       // 对齐模式（边沿/中心）
    bool invert_high_side;          // 高侧输出反相
    bool invert_low_side;           // 低侧输出反相
} intf_hrpwm_pair_cfg_t;

typedef struct {
    intf_hrpwm_fault_src_t source;
    intf_hrpwm_fault_mode_t mode;
    intf_hrpwm_fault_recovery_t recovery;
    bool active_low;
} intf_hrpwm_fault_cfg_t;

typedef struct {
    uint8_t instance_id;
    struct {
        int (*init_pair)(intf_hrpwm_ch_t ch, const intf_hrpwm_pair_cfg_t *cfg);
        int (*set_duty)(intf_hrpwm_ch_t ch, float duty);
        int (*set_frequency)(uint32_t frequency_hz);
        int (*set_jitter)(intf_hrpwm_ch_t ch, uint8_t jitter_cmp);
        int (*start)(intf_hrpwm_ch_t ch);
        int (*stop)(intf_hrpwm_ch_t ch);
        int (*force_low)(intf_hrpwm_ch_t ch);
        int (*force_release)(intf_hrpwm_ch_t ch);
        int (*config_fault)(const intf_hrpwm_fault_cfg_t *cfg);
        int (*clear_fault)(void);
    };
} intf_hrpwm_t;
```

### 3.2 功能 API

```c
int intf_hrpwm_register(const intf_hrpwm_t *ops);
int intf_hrpwm_init_pair(intf_hrpwm_ch_t ch, const intf_hrpwm_pair_cfg_t *cfg);
int intf_hrpwm_set_duty(intf_hrpwm_ch_t ch, float duty);
int intf_hrpwm_set_frequency(intf_hrpwm_inst_t inst, uint32_t frequency_hz);
int intf_hrpwm_set_jitter(intf_hrpwm_ch_t ch, uint8_t jitter_cmp);
int intf_hrpwm_start(intf_hrpwm_ch_t ch);
int intf_hrpwm_stop(intf_hrpwm_ch_t ch);
int intf_hrpwm_force_low(intf_hrpwm_ch_t ch);
int intf_hrpwm_force_release(intf_hrpwm_ch_t ch);
int intf_hrpwm_config_fault(intf_hrpwm_inst_t inst, const intf_hrpwm_fault_cfg_t *cfg);
int intf_hrpwm_clear_fault(intf_hrpwm_inst_t inst);
```

### 3.3 参数约束

| 参数 | 约束 | 说明 |
|------|------|------|
| `frequency_hz` | `> 0` 且小于外设时钟 | 驱动会根据外设时钟计算 reload |
| `duty` | `0.0f <= duty <= 1.0f`，且不能是 NaN | 归一化占空比，驱动层负责转换 |
| `deadtime_ns` | `>= 0` | 死区时间，驱动会转换为时钟周期数 |
| `jitter_cmp` | `>= 0` | 抖动计数器比较值，提高 DPWM 有效分辨率 |
| `align` | `INTF_HRPWM_ALIGN_EDGE` 或 `INTF_HRPWM_ALIGN_CENTER` | PWM 对齐模式 |
| `ch` | `0..7` | 0-3 对应 PWM0，4-7 对应 PWM1 |
| `inst` | `0..1` | 0 对应 PWM0，1 对应 PWM1 |

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
| `hrpwm ch4` | `HPM_PWM1 P4` | PA28 | `init_pwm1_pins()` |
| `hrpwm ch5` | `HPM_PWM1 P5` | PA29 | `init_pwm1_pins()` |
| `hrpwm ch6` | `HPM_PWM1 P6` | PA30 | `init_pwm1_pins()` |
| `hrpwm ch7` | `HPM_PWM1 P7` | PA31 | `init_pwm1_pins()` |

---

## 5. HRPWM 驱动设计

文件：`Driver/hpm_impl/drv_hrpwm.c`

### 5.1 状态模型

```c
typedef struct {
    bool configured;
    float duty;
    uint32_t reload;
    uint8_t jitter_cmp;
    intf_hrpwm_align_t align;
} hrpwm_channel_state_t;

typedef struct {
    PWM_Type *base;
    clock_name_t clock_name;
    uint32_t frequency_hz;
    uint32_t reload;
    bool fault_configured;
    uint8_t force_mask;
    hrpwm_channel_state_t channels[HRPWM_CHANNELS_PER_INST];
} hrpwm_instance_state_t;

static hrpwm_instance_state_t hrpwm_instances[HRPWM_INSTANCE_COUNT];
```

设计要点：

- `HPM_PWM0` 和 `HPM_PWM1` 各自独立，有独立的 counter/reload。
- 每个实例有 4 个通道，支持互补输出。
- 每个通道保存自己的 duty 和 configured 状态。
- 频率是实例级的，改频会影响该实例的所有通道。

### 5.2 初始化流程

`hrpwm_init_pair(ch, cfg)`：

1. 检查 `cfg != NULL`。
2. 检查 `ch < HRPWM_CHANNEL_COUNT`。
3. 检查 `duty` 有效且非 NaN。
4. 检查 `align` 为合法枚举值。
5. 根据 `frequency_hz` 计算 `reload`，校验 `reload < HRPWM_RELOAD_MAX_VALUE`（24-bit 位宽）。
6. 计算死区周期数：`deadtime_cycles = deadtime_ns / (1000000000 / clock_hz)`。
7. 调用 `pwm_get_default_pwm_pair_config()` 获取默认配置。
8. 配置高侧和低侧输出（反相、死区）。
9. 配置比较器初始值和 jitter。
10. 调用 `pwm_setup_waveform_in_pair()` 绑定 PWM pair。
11. 保存通道状态（duty、reload、jitter_cmp、align）。
12. 调用 `hrpwm_apply_duty()` 根据 align 模式计算并写入 compare 值。

### 5.3 启停策略

`hrpwm_start(ch)`：

```c
pwm_enable_output(base, local_ch);
pwm_start_counter(base);
pwm_issue_shadow_register_lock_event(base);
```

`hrpwm_stop(ch)`：

```c
pwm_disable_output(base, local_ch);
```

注意：`stop(ch)` 只关闭该通道输出，不停止整个 PWM counter，避免影响同一 PWM 实例上的其他通道。

### 5.4 改频语义

`intf_hrpwm_set_frequency(inst, frequency_hz)`：

- 改频会影响该实例的所有通道。
- 驱动会遍历已配置通道并重新应用 duty。
- 频率是实例级的，不是通道级的。

### 5.5 Duty 更新与对齐模式

`hrpwm_apply_duty()` 根据 `align` 模式计算两个 compare 值，直接写入 `CMP[start]` / `CMP[start+1]`：

- **中心对齐**：`cmp_begin = (reload - target) >> 1`，`cmp_end = (reload + target) >> 1`
  - 脉冲在周期内居中，两个边沿对称移动
- **边沿对齐**：`cmp_begin = reload - target`，`cmp_end = reload`
  - 脉冲从 reload 处开始，仅一个边沿移动

`hrpwm_set_jitter(ch, jitter_cmp)`：

- 直接更新两个 compare index 的 jitter 字段，不破坏 CMP/XCMP 值。
- 更新后重新调用 `hrpwm_apply_duty()` 保持 compare 值一致。

### 5.6 Fault 保护

`hrpwm_config_fault(cfg)`：

- 先校验 `mode`、`recovery`、`source` 三个枚举参数，任一非法立即返回 `-1`，不修改任何寄存器。
- 然后对每个 PWM instance 的每个 channel 写入 `PWMCFG` 的 `FAULTMODE` 和 `FAULTRECTIME` 位（保留其它位）。
- 最后调用 `pwm_config_fault_source()` 配置 fault source。

`hrpwm_clear_fault()`：

- 调用 SDK `pwm_clear_fault(base)` 清除 fault latch。
- 再调用 `pwm_clear_status()` 清除状态位。

### 5.6 Force-low / Force-release

`hrpwm_force_low(ch)`：

```c
pwm_enable_pwm_sw_force_output(base, local_ch);
pwm_set_force_output(base, PWM_FORCE_OUTPUT(local_ch, pwm_output_0));
```

`hrpwm_force_release(ch)`：

```c
pwm_disable_pwm_sw_force_output(base, local_ch);
```

---

## 6. 注册与调用链

Driver 层提供注册函数：

```c
void hpm_hrpwm_driver_register(void)
{
    hrpwm_init_instances();
    intf_hrpwm_register(&hrpwm_ops_pwm0);
    intf_hrpwm_register(&hrpwm_ops_pwm1);
}
```

接口分发位于 `Interface/intf_default.c`：

```c
#define HRPWM_INSTANCE_COUNT (2U)
static const intf_hrpwm_t *hrpwm_ops[HRPWM_INSTANCE_COUNT] = {NULL};
```

典型调用链：

```text
系统初始化/Driver bootstrap
  -> hpm_hrpwm_driver_register()

上层模块
  -> intf_hrpwm_init_pair(ch0, &cfg)  // 初始化 PWM0 互补对
  -> intf_hrpwm_set_duty(ch0, 0.5f)   // 设置占空比
  -> intf_hrpwm_start(ch0)            // 启动输出
  -> intf_hrpwm_config_fault(0, &fault_cfg)  // 配置 fault 保护
```

---

## 7. 安全策略与限制

当前实现包含以下保护：

- 通道范围检查。
- 频率为 0 时拒绝初始化/改频。
- duty 必须在 `[0.0f, 1.0f]` 且不能是 NaN。
- `align` 枚举合法性校验，非法值返回 `-1`。
- `reload` 24-bit 位宽校验（`< HRPWM_RELOAD_MAX_VALUE`），防止 compare sentinel 溢出。
- `static_assert` 编译期校验 compare 资源映射不越界。
- 初始化阶段默认不使能输出，必须显式调用 `start()`。
- `hrpwm_stop(ch)` 不停止全局 counter，避免影响其他通道。
- Fault 保护配置（force low/high/high-z）。
- Fault config 先完整校验所有枚举参数，再写入寄存器，非法值不会产生部分副作用。
- Fault recovery 策略（immediately/reload/hw event/fault clear）。
- 死区时间配置。
- Force-low / force-release 强制输出。

**当前仍未完成的功率级安全能力**：

- ADC 同步采样触发。
- 相移控制（用于全桥拓扑）。

---

## 8. 使用示例

### 8.1 面向对象 API 设计

```c
typedef enum {
    PWM_PAIR_0 = 0,  // PWM0: ch0/ch1
    PWM_PAIR_1,      // PWM0: ch2/ch3
    PWM_PAIR_2,      // PWM1: ch4/ch5
    PWM_PAIR_3,      // PWM1: ch6/ch7
    PWM_PAIR_COUNT,
} pwm_pair_t;

static const intf_hrpwm_ch_t pair_to_ch[PWM_PAIR_COUNT] = {0, 2, 4, 6};
static const intf_hrpwm_inst_t pair_to_inst[PWM_PAIR_COUNT] = {0, 0, 1, 1};
```

### 8.2 初始化（带对齐模式和抖动）

```c
void pwm_init(void)
{
    intf_hrpwm_pair_cfg_t cfg[PWM_PAIR_COUNT] = {
        [PWM_PAIR_0] = { .frequency_hz = 200000, .duty = 0.5f, .deadtime_ns = 10, .jitter_cmp = 4, .align = INTF_HRPWM_ALIGN_CENTER, .invert_high_side = false, .invert_low_side = false },
        [PWM_PAIR_1] = { .frequency_hz = 200000, .duty = 0.3f, .deadtime_ns = 10, .jitter_cmp = 4, .align = INTF_HRPWM_ALIGN_CENTER, .invert_high_side = false, .invert_low_side = false },
        [PWM_PAIR_2] = { .frequency_hz = 148000, .duty = 0.5f, .deadtime_ns = 25, .jitter_cmp = 4, .align = INTF_HRPWM_ALIGN_CENTER, .invert_high_side = false, .invert_low_side = false },
        [PWM_PAIR_3] = { .frequency_hz = 148000, .duty = 0.4f, .deadtime_ns = 25, .jitter_cmp = 4, .align = INTF_HRPWM_ALIGN_CENTER, .invert_high_side = false, .invert_low_side = false },
    };

    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        intf_hrpwm_init_pair(pair_to_ch[pair], &cfg[pair]);
        intf_hrpwm_start(pair_to_ch[pair]);
    }
}
```

注意：`hpm_hrpwm_driver_register()` 在 `pwm_init()` 内部调用，遵循 App init 函数自行完成驱动注册的范例。`main.c` 不直接调用 Driver 层符号。

### 8.3 统一控制接口

```c
void pwm_set_duty(pwm_pair_t pair, float duty)
{
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_set_duty(pair_to_ch[pair], duty);
    }
}

void pwm_set_frequency(pwm_pair_t pair, uint32_t freq_hz)
{
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_set_frequency(pair_to_inst[pair], freq_hz);
    }
}

void pwm_set_jitter(pwm_pair_t pair, uint8_t jitter_cmp)
{
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_set_jitter(pair_to_ch[pair], jitter_cmp);
    }
}

void pwm_stop(pwm_pair_t pair)
{
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_stop(pair_to_ch[pair]);
        intf_hrpwm_stop(pair_to_ch[pair] + 1);
    }
}

void pwm_stop_all(void)
{
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_stop(pair);
    }
}

void pwm_force_low(pwm_pair_t pair)
{
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_force_low(pair_to_ch[pair]);
        intf_hrpwm_force_low(pair_to_ch[pair] + 1);
    }
}

void pwm_force_release(pwm_pair_t pair)
{
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_force_release(pair_to_ch[pair]);
        intf_hrpwm_force_release(pair_to_ch[pair] + 1);
    }
}

void pwm_emergency_stop(void)
{
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_force_low(pair);
    }
}

void pwm_resume(void)
{
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_force_release(pair);
    }
}
```

### 8.4 使用示例

```c
int main(void)
{
    pwm_init();

    // 动态调整占空比
    pwm_set_duty(PWM_PAIR_0, 0.7f);  // pair 0 占空比改为 70%
    pwm_set_duty(PWM_PAIR_2, 0.3f);  // pair 2 占空比改为 30%

    // 动态调整频率
    pwm_set_frequency(PWM_PAIR_0, 250000);  // PWM0 频率改为 250kHz
    pwm_set_frequency(PWM_PAIR_2, 180000);  // PWM1 频率改为 180kHz

    // 动态调整抖动值
    pwm_set_jitter(PWM_PAIR_0, 16);  // pair 0 抖动值改为 16

    // 停止单个通道对
    pwm_stop(PWM_PAIR_1);  // 停止 pair 1

    // 紧急停止
    pwm_emergency_stop();

    // 恢复输出
    pwm_resume();

    return 0;
}
```

---

## 9. 抖动技术（Jitter）

### 9.1 原理

抖动技术通过硬件自动在相邻 PWM 周期之间微调占空比，提高有效分辨率。

**传统 PWM (无抖动)**：
```
周期 1: 占空比 = 300/600 = 50.000%
周期 2: 占空比 = 300/600 = 50.000%
分辨率: 1/600 = 0.167% (9-bit)
```

**带抖动 PWM**：
```
周期 1: 占空比 = 300/600 = 50.000%
周期 2: 占空比 = 301/600 = 50.167%
周期 3: 占空比 = 300/600 = 50.000%
周期 4: 占空比 = 301/600 = 50.167%
平均占空比 = 50.083% (有效分辨率提高 4 倍)
```

### 9.2 API

```c
// 初始化时配置抖动值
intf_hrpwm_pair_cfg_t cfg = {
    .frequency_hz = 200000,
    .duty = 0.5f,
    .deadtime_ns = 500,
    .jitter_cmp = 4,        // 抖动值，范围 0-255
    .invert_high_side = false,
    .invert_low_side = true,
};
intf_hrpwm_init_pair(0, &cfg);

// 动态调整抖动值
intf_hrpwm_set_jitter(0, 16);   // ch0 抖动值改为 16
```

### 9.3 抖动值选择

| jitter_cmp | 抖动幅度 | 有效分辨率提升 | 适用场景 |
|------------|----------|----------------|----------|
| 0 | 无抖动 | 0-bit | 对分辨率要求不高 |
| 1 | 最小抖动 | ~1-bit | 轻微改善 |
| 4 | 中等抖动 | ~2-bit | 通用推荐 |
| 16 | 较大抖动 | ~3-bit | 高分辨率需求 |
| 255 | 最大抖动 | ~4-bit | 极致分辨率 |

### 9.4 分辨率对比

| 配置 | PWM@200kHz | PWM@148kHz |
|------|------------|------------|
| 无抖动 | 600 级 (9.2-bit) | 810 级 (9.7-bit) |
| jitter_cmp=4 | ~2400 级 (11.2-bit) | ~3240 级 (11.7-bit) |
| jitter_cmp=16 | ~9600 级 (13.2-bit) | ~12960 级 (13.7-bit) |

---

## 10. 已知问题与解决方案

### 10.1 100%占空比窄脉冲问题 (已修复)

**问题现象**：
- 当占空比从0%逐渐增加到100%时，在接近100%时会出现极窄的反向脉冲
- 表现为大部分周期保持高电平，但周期中出现极短暂的低电平（针尖状脉冲）
- 示波器可观测到1个时钟周期宽度的低电平脉冲

**根因分析**：

原代码中CMP边界处理逻辑错误：

```c
// 错误代码 (drv_hrpwm.c)
if (cmp.cmp_begin == 0U) {
    cmp.cmp_begin = reload + 1U;  // 错误：将0改为reload+1
}
```

当duty接近100%时：
1. `target_cmp ≈ reload`
2. `cmp_begin = (reload - target_cmp) >> 1 ≈ 0`
3. 代码将 `cmp_begin = 0` 改为 `reload + 1`
4. 交换逻辑导致 `cmp_begin = reload`, `cmp_end = reload + 1`
5. 在中心对齐模式下，Counter到达reload时触发Compare Match
6. 输出被意外拉低，产生极窄低脉冲

**修复方案**：

```c
// 修复后代码 (drv_hrpwm.c)
static hrpwm_cmp_pair_t hrpwm_calc_center_aligned_cmp(uint32_t reload, float duty)
{
    hrpwm_cmp_pair_t cmp;

    // 100%占空比：CMP设为reload+1（永远不匹配），输出保持高
    if (duty >= 1.0f) {
        cmp.cmp_begin = reload + 1U;
        cmp.cmp_end = reload + 1U;
        return cmp;
    }

    // 0%占空比：CMP设为0（立即匹配），输出保持低
    if (duty <= 0.0f) {
        cmp.cmp_begin = 0U;
        cmp.cmp_end = 0U;
        return cmp;
    }

    // 正常占空比：确保CMP值至少为1
    uint32_t target_cmp = hrpwm_duty_to_cmp_count(reload, duty);
    cmp.cmp_begin = (reload - target_cmp) >> 1;
    cmp.cmp_end = (reload + target_cmp) >> 1;

    if (cmp.cmp_begin == 0U) {
        cmp.cmp_begin = 1U;  // 使用1而不是reload+1
    }
    if (cmp.cmp_end == 0U) {
        cmp.cmp_end = 1U;
    }

    return cmp;
}
```

**CMP值对照表**：

| 占空比 | 原CMP值 | 修复后CMP值 | 效果 |
|--------|---------|-------------|------|
| 100% | cmp_begin=reload, cmp_end=reload+1 | cmp_begin=reload+1, cmp_end=reload+1 | Counter永远不匹配，输出保持高 |
| 0% | cmp_begin=reload/2, cmp_end=reload/2 | cmp_begin=0, cmp_end=0 | Counter在0时匹配，输出保持低 |
| 正常 | cmp_begin可能为0 | cmp_begin至少为1 | 避免与reload边界冲突 |

**注意事项**：
1. 100%占空比时，CMP值必须大于reload，确保Counter永远不匹配
2. 0%占空比时，CMP值设为0，Counter在周期开始时立即匹配
3. 正常占空比时，CMP值不能为0，否则会在Counter=0时产生意外匹配
4. 此修复同时应用于中心对齐和边沿对齐模式

---

## 11. PWM中断机制

### 11.1 中断源

HPM5361 PWM支持以下中断源：

| 中断源 | 宏定义 | 触发时机 | 用途 |
|--------|--------|----------|------|
| **RELOAD** | `PWM_IRQ_RELOAD` | Counter到达Reload值 | 中心对齐模式中心点 |
| **HALF_RELOAD** | `PWM_IRQ_HALF_RELOAD` | Counter到达Reload/2 | 半周期点 |
| **CMP(x)** | `PWM_IRQ_CMP(x)` | Compare Match | 自定义比较点 |
| **FAULT** | `PWM_IRQ_FAULT` | 故障触发 | 保护响应 |

### 11.2 中心对齐模式中断时序

```
Counter:  0 ──→ Reload ──→ 0 ──→ Reload ──→ 0
               ↑              ↑
          PWM_IRQ_RELOAD 触发点
          (中心对齐的"中心点")
```

### 11.3 接口定义

```c
/* intf_hrpwm.h */
typedef void (*intf_hrpwm_irq_callback_t)(void);

int intf_hrpwm_config_reload_irq(intf_hrpwm_inst_t inst, intf_hrpwm_irq_callback_t callback);
int intf_hrpwm_enable_reload_irq(intf_hrpwm_inst_t inst);
int intf_hrpwm_disable_reload_irq(intf_hrpwm_inst_t inst);
```

### 11.4 使用示例

```c
/* 业务回调函数 */
static void my_control_loop(void)
{
    /* ADC采样、PID计算、更新占空比 */
}

/* 初始化 */
intf_hrpwm_config_reload_irq(0, my_control_loop);  /* 注册回调 */
intf_hrpwm_enable_reload_irq(0);                    /* 使能中断 */
```

### 11.5 调试接口

```c
/* app_debug_rtt.h */
void app_debug_pwm_irq_enable(uint8_t inst);
void app_debug_pwm_irq_disable(uint8_t inst);
uint32_t app_debug_pwm_irq_get_count(uint8_t inst);
void app_debug_pwm_irq_dump_status(void);
int app_debug_pwm_irq_register_callback(uint8_t inst, pwm_irq_user_callback_t callback);
```

### 11.6 验证结果

| PWM实例 | 频率 | 1秒中断计数 | 状态 |
|---------|------|-------------|------|
| PWM0 | 200kHz | ~200,422 | ✓ |
| PWM1 | 148kHz | ~148,311 | ✓ |

---

## 12. 开发检查清单

开发 HRPWM 驱动新功能时，请按以下清单检查：

- [x] 接口定义符合 AGENTS.md 规范（匿名结构体、参数归一化）
- [x] 驱动实现不暴露 `hpm_*` 类型到 Interface 层
- [x] 通道范围校验完整
- [x] duty 参数包含 NaN 防护
- [x] 初始化阶段不使能输出，需显式调用 `start()`
- [x] `stop()` 不影响同实例其他通道
- [x] 频率变化时重新计算并应用所有已配置通道的 duty
- [x] 配置 Fault 保护（功率应用必需）
- [x] 配置死区时间（互补输出必需）
- [x] Force-low / force-release 强制输出
- [x] 使用抖动技术提高分辨率（数字电源必需）
- [ ] 配置 ADC 同步采样（闭环控制必需）

---

## 11. 后续开发计划

### 11.1 高优先级

1. **ADC 同步采样触发**
   - 配置 PWM 触发 ADC 采样。
   - 参考 SDK 示例：`samples/motor_ctrl/bldc_foc/`

### 11.2 中优先级

2. **相移控制**
   - 实现 PWM0 和 PWM1 之间的相移控制。
   - 用于全桥拓扑的移相控制。

3. **补充 driver bootstrap**
   - 增加统一驱动注册入口，例如 `hpm_drivers_register_all()`。

### 11.3 低优先级

4. **增加硬件验证用例**
   - 用示波器验证：
     - `hrpwm ch0..7` 输出频率/占空比
     - 互补输出死区
     - 抖动技术效果
     - Fault 保护响应
     - `stop()` 后输出是否进入预期安全态

5. **C17 特性应用**
   - `static_assert`：编译期校验通道数、缓冲区大小
   - `_Generic`：类型安全的接口宏
   - `_Alignas`：DMA 缓冲区 L1 Cache 对齐

6. **性能优化**
   - ILM 部署：将关键函数放入 ILM
   - Cache 对齐：ADC DMA 缓冲区 `_Alignas(32)`
   - RAMFUNC：中断处理函数、PID 控制器放入 RAM
