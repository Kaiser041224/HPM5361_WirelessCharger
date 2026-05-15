# HRPWM / GPWM 驱动设计说明

本文描述当前工程中 `hrpwm` 与 `gpwm` 两套 PWM 输出驱动的设计边界、接口契约、硬件映射和后续开发约束。

> 当前实现处于驱动骨架阶段，目标是先建立 `Interface -> Driver` 的解耦调用链，并把 HPM SDK 相关类型封装在 `Driver/hpm_impl` 内部。

---

## 1. 分层目标

本设计遵循工程 `AGENTS.md` 中的“物理隔离 + 契约驱动”原则：

| 层级 | 当前文件 | 职责 |
|------|----------|------|
| `Interface/` | `intf_hrpwm.h`, `intf_gpwm.h` | 定义纯 C 契约，不暴露 `hpm_*` 类型 |
| `Interface/` | `intf_default.c` | 当前工程既有接口注册/分发实现，保存 ops 指针 |
| `Driver/hpm_impl/` | `drv_hrpwm.c`, `drv_gpwm.c` | 将接口调用映射到 HPM SDK PWM/GPTMR API |
| `App/` | 暂无 PWM app wrapper | 当前阶段不引入 App 调用封装，专注驱动侧 |

当前保留的注册入口：

```c
void hpm_hrpwm_driver_register(void);
void hpm_gpwm_driver_register(void);
```

这两个函数位于 Driver 层。后续应由系统初始化层或专门的 driver bootstrap 调用，避免 App 直接依赖 Driver 符号。

---

## 2. 命名与职责

### 2.1 `hrpwm`

`hrpwm` 表示“高性能/功率 PWM 输出通道”的抽象。目前硬件映射为：

```c
#define HRPWM_BASE HPM_PWM0
#define HRPWM_CLOCK_NAME clock_mot0
#define HRPWM_CHANNEL_COUNT (4U)
```

当前 HPM5361 的 SDK 特性中并未启用真正 high-resolution PWM 能力，因此现阶段 `hrpwm` 实现基于普通 `HPM_PWM0` API。命名保留为 `hrpwm` 是为了后续面向功率 PWM/高性能 PWM 的接口演进，但本文明确记录：**当前实现不是亚时钟级 HRPWM**。

### 2.2 `gpwm`

`gpwm` 表示“GPTMR based general PWM”。除 GPTMR 输出比较 PWM 外，当前也承载 GPTMR 输入捕获能力。目前硬件映射为：

```c
#define GPWM_BASE HPM_GPTMR0
#define GPWM_CLOCK_NAME clock_gptmr0
#define GPWM_CHANNEL_COUNT (4U)
#define GPWM_FIRST_OUTPUT_CHANNEL (2U)
#define GPWM_CAPTURE_CHANNEL (1U)
```

当前板级 pinmux 仅将 GPTMR0 的 `COMP2`、`COMP3` 作为输出比较 PWM 引脚，因此 `gpwm` 只允许通道 `2..3`。
`GPTMR0_CAPT_1` 被路由到 PB09，因此输入捕获只开放 `gpwm ch1`。

---

## 3. 对外接口契约

### 3.1 HRPWM 接口

文件：`Interface/intf_hrpwm.h`

```c
typedef uint8_t intf_hrpwm_ch_t;

typedef struct {
    uint32_t frequency_hz;
    float duty;
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

功能 API：

```c
int intf_hrpwm_register(const intf_hrpwm_t *ops);
int intf_hrpwm_init(intf_hrpwm_ch_t ch, const intf_hrpwm_cfg_t *cfg);
int intf_hrpwm_set_duty(intf_hrpwm_ch_t ch, float duty);
int intf_hrpwm_set_frequency(intf_hrpwm_ch_t ch, uint32_t frequency_hz);
int intf_hrpwm_start(intf_hrpwm_ch_t ch);
int intf_hrpwm_stop(intf_hrpwm_ch_t ch);
```

### 3.2 GPWM 接口

文件：`Interface/intf_gpwm.h`

`gpwm` 与 `hrpwm` 保持同构 API，便于上层统一理解：

```c
typedef uint8_t intf_gpwm_ch_t;

typedef struct {
    uint32_t frequency_hz;
    float duty;
    bool invert_output;
} intf_gpwm_cfg_t;

typedef enum {
    INTF_GPWM_CAPTURE_EDGE_RISING = 0,
    INTF_GPWM_CAPTURE_EDGE_FALLING,
    INTF_GPWM_CAPTURE_EDGE_BOTH,
} intf_gpwm_capture_edge_t;

typedef struct {
    intf_gpwm_capture_edge_t edge;
} intf_gpwm_capture_cfg_t;

typedef struct {
    bool captured;
    uint32_t count;
    uint32_t period_ticks;
} intf_gpwm_capture_t;
```

功能 API：

```c
int intf_gpwm_register(const intf_gpwm_t *ops);
int intf_gpwm_init(intf_gpwm_ch_t ch, const intf_gpwm_cfg_t *cfg);
int intf_gpwm_set_duty(intf_gpwm_ch_t ch, float duty);
int intf_gpwm_set_frequency(intf_gpwm_ch_t ch, uint32_t frequency_hz);
int intf_gpwm_start(intf_gpwm_ch_t ch);
int intf_gpwm_stop(intf_gpwm_ch_t ch);
int intf_gpwm_capture_init(intf_gpwm_ch_t ch, const intf_gpwm_capture_cfg_t *cfg);
int intf_gpwm_capture_start(intf_gpwm_ch_t ch);
int intf_gpwm_capture_stop(intf_gpwm_ch_t ch);
int intf_gpwm_capture_poll(intf_gpwm_ch_t ch, intf_gpwm_capture_t *capture);
```

### 3.3 参数约束

| 参数 | 约束 | 说明 |
|------|------|------|
| `frequency_hz` | `> 0` 且小于/不超过外设时钟 | 驱动会根据外设时钟计算 reload |
| `duty` | `0.0f <= duty <= 1.0f`，且不能是 NaN | 归一化占空比，驱动层负责转换到 SDK 所需单位 |
| `invert_output` | `true/false` | 反相输出控制 |
| `ch` for hrpwm | `0..3` | 对应当前板级 `PWM0_P_0..3` |
| `ch` for gpwm | `2..3` | 对应当前板级 `GPTMR0_COMP_2/3` |
| `ch` for gpwm capture | `1` | 对应当前板级 `GPTMR0_CAPT_1` |

错误返回：当前统一使用 `0` 表示成功，`-1` 表示参数、状态或底层 SDK 调用失败。

---

## 4. 板级通道映射

当前 `Board/HPM5361_WirelessCharger_board/pinmux.c` 中与 PWM 相关的配置如下。

### 4.1 HRPWM / PWM0 输出

| Interface 通道 | HPM 外设 | 板级引脚 | pinmux 函数 |
|----------------|----------|----------|-------------|
| `hrpwm ch0` | `HPM_PWM0 P0` | PA24 | `init_pwm0_pins()` |
| `hrpwm ch1` | `HPM_PWM0 P1` | PA25 | `init_pwm0_pins()` |
| `hrpwm ch2` | `HPM_PWM0 P2` | PA26 | `init_pwm0_pins()` |
| `hrpwm ch3` | `HPM_PWM0 P3` | PA27 | `init_pwm0_pins()` |

板上还配置了 PA28~PA31 为 `PWM1_P_4..7`，但当前 `drv_hrpwm.c` 固定使用 `HPM_PWM0`，因此未开放 `ch4..ch7`。

### 4.2 GPWM / GPTMR0 输出

| Interface 通道 | HPM 外设 | 板级引脚 | pinmux 函数 |
|----------------|----------|----------|-------------|
| `gpwm ch2` | `HPM_GPTMR0 COMP2` | PA10 | `init_gptmr0_pins()` |
| `gpwm ch3` | `HPM_GPTMR0 COMP3` | PB15 | `init_gptmr0_pins()` |

`PB09` 是 `GPTMR0_CAPT_1`，属于输入捕获，不作为 `gpwm` 输出通道开放。

### 4.3 GPWM / GPTMR0 输入捕获

| Interface 通道 | HPM 外设 | 板级引脚 | pinmux 函数 |
|----------------|----------|----------|-------------|
| `gpwm capture ch1` | `HPM_GPTMR0 CAPT1` | PB09 | `init_gptmr0_pins()` |

当前输入捕获采用 polling 模式，不在驱动内注册 GPTMR IRQ ISR。

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

## 6. GPWM 驱动设计

文件：`Driver/hpm_impl/drv_gpwm.c`

### 6.1 状态模型

```c
typedef struct {
    bool configured;
    uint32_t frequency_hz;
    float duty;
    bool invert_output;
    uint32_t reload;
} gpwm_state_t;

static gpwm_state_t gpwm_state[GPWM_CHANNEL_COUNT];
```

GPTMR 每个通道有独立 counter/reload，因此 `gpwm` 可以按通道保存频率和 reload。

输入捕获额外维护：

```c
typedef struct {
    bool configured;
    bool started;
    bool has_first_edge;
    intf_gpwm_capture_edge_t edge;
    uint32_t first_count;
    uint32_t period_ticks;
} gpwm_capture_state_t;
```

其中 `period_ticks` 表示相邻两次捕获边沿之间的 GPTMR tick 差值。

### 6.2 通道限制

```c
static bool gpwm_is_valid_channel(intf_gpwm_ch_t ch)
{
    return (ch >= GPWM_FIRST_OUTPUT_CHANNEL) && (ch < GPWM_CHANNEL_COUNT);
}
```

当前只允许 `ch2` 和 `ch3`，因为只有这两个通道在板级被路由为 `COMP` 输出。

输入捕获单独使用：

```c
static bool gpwm_is_valid_capture_channel(intf_gpwm_ch_t ch)
{
    return ch == GPWM_CAPTURE_CHANNEL;
}
```

当前只允许 `ch1`，因为 PB09 被路由为 `GPTMR0_CAPT_1`。

### 6.3 初始化流程

`gpwm_init(ch, cfg)`：

1. 检查 `cfg != NULL`。
2. 检查通道为 `2..3`。
3. 检查 `frequency_hz > 0`。
4. 检查 `duty` 有效且非 NaN。
5. 打开/加入 GPTMR0 时钟组。
6. 计算 `reload = clock_hz / frequency_hz`。
7. 获取 `gptmr_channel_get_default_config()`。
8. 设置 `mode = gptmr_work_mode_no_capture`。
9. 设置 `cmp_initial_polarity_high = cfg->invert_output`。
10. 设置 `enable_cmp_output = false`，避免 init 立即输出。
11. `gptmr_channel_config(..., false)` 配置但不启动。
12. `gptmr_channel_reset_count()`。
13. 保存状态并应用 duty。

### 6.4 占空比计算

```c
cmp = (uint32_t)((float)gpwm_state[ch].reload * duty);
gptmr_update_cmp(GPWM_BASE, ch, 0, cmp);
gptmr_update_cmp(GPWM_BASE, ch, 1, gpwm_state[ch].reload);
```

GPTMR 使用 `cmp0` 和 `cmp1` 生成边沿对齐 PWM。

### 6.5 启停策略

`gpwm_start(ch)`：

```c
gptmr_enable_cmp_output(GPWM_BASE, ch);
gptmr_start_counter(GPWM_BASE, ch);
```

`gpwm_stop(ch)`：

```c
gptmr_disable_cmp_output(GPWM_BASE, ch);
gptmr_stop_counter(GPWM_BASE, ch);
```

GPTMR 通道之间相对独立，因此 stop 会同时关闭该通道输出和该通道 counter。

### 6.6 输入捕获流程

`gpwm_capture_init(ch, cfg)`：

1. 检查 `cfg != NULL`。
2. 检查通道为 `ch1`。
3. 检查捕获边沿配置合法。
4. 打开/加入 GPTMR0 时钟组。
5. 获取 `gptmr_channel_get_default_config()`。
6. 将 `config.mode` 配置为：
   - `gptmr_work_mode_capture_at_rising_edge`
   - `gptmr_work_mode_capture_at_falling_edge`
   - `gptmr_work_mode_capture_at_both_edge`
7. 关闭 compare 输出。
8. 停止计数器、关闭 CAP IRQ、清除 CAP status。
9. `gptmr_channel_config(..., false)` 配置但不启动。
10. 复位计数器并清空捕获状态。

`gpwm_capture_start(ch)`：

```c
gptmr_clear_status(GPWM_BASE, GPTMR_CH_CAP_STAT_MASK(ch));
gptmr_channel_reset_count(GPWM_BASE, ch);
gptmr_start_counter(GPWM_BASE, ch);
```

`gpwm_capture_poll(ch, capture)`：

1. 检查 CAP status。
2. 若无捕获事件，返回 `capture->captured = false`。
3. 若有捕获事件，读取对应 edge counter。
4. 第一次边沿只记录 `first_count`。
5. 第二次及以后边沿计算 `period_ticks`，并返回 `capture->captured = true`。

当前 polling API 适合上层周期查询：

```c
intf_gpwm_capture_t cap;

if ((intf_gpwm_capture_poll(1, &cap) == 0) && cap.captured) {
    /* cap.period_ticks 为两次捕获边沿之间的 GPTMR tick 数 */
}
```

注意：当前 `INTF_GPWM_CAPTURE_EDGE_BOTH` 仍使用 rising counter 读取捕获值，主要用于记录混合边沿事件；如需精确区分高/低电平时间，应后续增加“边沿类型”输出或测宽模式封装。

### 6.7 强制输出

GPWM 接口提供强制输出功能，用于将 PWM 引脚强制输出确定电平：

```c
int intf_gpwm_force_low(intf_gpwm_ch_t ch);
int intf_gpwm_force_release(intf_gpwm_ch_t ch);
```

驱动实现原理（基于 HPM SDK GPTMR 特性）：

- `force_low`：将 CMP0 和 CMP1 都设为 `0xFFFFFFFF`，GPTMR 计数器永远无法匹配，输出保持低电平。
- `force_release`：恢复 CMP0/CMP1 到正常 PWM 占空比值，重新启动计数器，恢复正常 PWM 输出。

典型用途：蜂鸣器静音、功率级安全关断。

---

## 7. App 层应用：app_buzzer

文件：`App/Logic/Inc/app_buzzer.h`，`App/Logic/Src/app_buzzer.c`

### 7.1 API

```c
void app_buzzer_init(void);
int  app_buzzer_set(bool enabled, uint32_t frequency_hz);
```

- `app_buzzer_init()`：注册 GPWM 驱动，初始化 GPWM ch3（PB15 / GPTMR0_COMP_3），默认 4kHz 50% 占空比，初始化后立即 `force_low` 确保引脚为低。
- `app_buzzer_set(true, freq)`：设置频率并恢复 PWM 输出。
- `app_buzzer_set(false, 0)`：`force_low` 强制引脚输出低电平。
- `enabled=true` 时 `frequency_hz` 传 0，使用默认 4kHz。

### 7.2 调用链

```c
app_buzzer_init();
  -> hpm_gpwm_driver_register()
  -> intf_gpwm_init(ch3, {4kHz, 50%})
  -> intf_gpwm_force_low(ch3)

app_buzzer_set(true, 1000);
  -> intf_gpwm_set_frequency(ch3, 1000)
  -> intf_gpwm_force_release(ch3)

app_buzzer_set(false, 0);
  -> intf_gpwm_force_low(ch3)
```

---

## 8. intf_clock 延迟封装

文件：`Interface/intf_clock.h`，`Driver/hpm_impl/drv_clock.c`

App 层不直接调用 HPM SDK 的 `clock_cpu_delay_ms()` / `clock_cpu_delay_us()`，通过接口层封装：

```c
void intf_clock_delay_ms(uint32_t ms);
void intf_clock_delay_us(uint32_t us);
```

---

## 9. 注册与调用链

Driver 层提供注册函数：

```c
void hpm_hrpwm_driver_register(void)
{
    intf_hrpwm_register(&hrpwm_ops);
}

void hpm_gpwm_driver_register(void)
{
    intf_gpwm_register(&gpwm_ops);
}
```

接口分发位于 `Interface/intf_default.c`：

```c
static const intf_hrpwm_t *hrpwm_ops = NULL;
static const intf_gpwm_t *gpwm_ops = NULL;
```

典型调用链：

```text
系统初始化/Driver bootstrap
  -> hpm_hrpwm_driver_register()
  -> hpm_gpwm_driver_register()

上层模块
  -> intf_hrpwm_init()/intf_gpwm_init()
  -> intf_hrpwm_set_duty()/intf_gpwm_set_duty()
  -> intf_hrpwm_start()/intf_gpwm_start()
```

当前尚未设计最终 bootstrap 位置。为保持分层，后续不建议由 App 直接声明并调用 `hpm_*_driver_register()`。

---

## 10. 安全策略与限制

当前骨架已包含以下基本保护：

- 通道范围检查。
- 频率为 0 时拒绝初始化/改频。
- duty 必须在 `[0.0f, 1.0f]` 且不能是 NaN。
- 初始化阶段默认不使能输出，必须显式调用 `start()`。
- `hrpwm_stop(ch)` 不停止全局 counter，避免影响其他 PWM0 channel。
- `gpwm` 不开放未路由或输入捕获通道。
- `gpwm capture` 不开放未路由捕获通道，且默认不启用 IRQ。

当前仍未完成的功率级安全能力：

- HRPWM 未配置 fault source。
- HRPWM 未配置 fault recovery 策略。
- HRPWM deadtime 暂固定为 0。
- 未封装互补输出 pair 模式。
- HRPWM 未设计 force-safe-low / brake API。（GPWM 已实现 force_low）
- 输入捕获当前为 polling 模式，尚未提供 callback/IRQ 事件通知。

因此：**当前 `hrpwm` 骨架不应直接用于真实功率桥驱动闭环，只适合作为接口和驱动映射基础。**

---

## 11. 后续开发计划建议

1. **补充 driver bootstrap**  
   增加统一驱动注册入口，例如 `hpm_drivers_register_all()`，由系统启动流程调用，避免 App 直接依赖 Driver。

2. **修正 HRPWM 命名或增加能力检测**  
   HPM5361 当前不是实际高分辨率 HRPWM。可考虑：
   - 保留 `hrpwm` 作为“high-performance PWM”项目内命名；或
   - 重命名为 `motpwm` / `power_pwm`；或
   - 使用 `#if PWM_SOC_HRPWM_SUPPORT` 明确区分 HRPWM 能力。

3. **将 HRPWM 频率改为 instance-level 契约**  
   当前接口为 `set_frequency(ch, freq)`，但底层频率全局共享。后续建议调整为实例级 API 或在文档/代码注释中进一步强调共享频率。

4. **增加 fault/deadtime/互补输出配置**  
   面向无线充功率级时必须补齐：
   - deadtime
   - complementary pair
   - fault source
   - safe output state
   - fault recovery

5. **完善 GPTMR 输入捕获能力**  
   当前仅支持相邻捕获边沿 tick 差值。后续可增加：
   - IRQ/callback 模式
   - 输入频率换算
   - PWM 高电平/低电平时间测量
   - `gptmr_work_mode_measure_width` 封装

6. **完善板级映射表**  
   将通道、外设、引脚、用途集中定义到 Board 或 Driver 私有映射表中，避免散落在宏和注释中。

7. **增加硬件验证用例**  
   用示波器验证：
   - `hrpwm ch0..3` 输出频率/占空比
   - `gpwm ch2..3` 输出频率/占空比
   - `gpwm capture ch1` 对外部 PWM 的周期捕获 tick
   - `stop()` 后输出是否进入预期安全态
