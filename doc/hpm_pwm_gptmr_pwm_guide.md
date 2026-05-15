# HPM SDK PWM 与 GPTMR PWM 使用梳理

本文基于当前工程使用的 HPM SDK 1.11.0（`/workspace/hpm_sdk`）整理，目标是说明 HPM 单片机中两类可输出 PWM 的外设如何使用：

- 专用 PWM/HRPWM 外设：`hpm_pwm_drv.h`
- 通用定时器 GPTMR 的输出比较 PWM：`hpm_gptmr_drv.h`

参考入口：

| 类型 | SDK 驱动头文件 | 主要示例 |
|------|----------------|----------|
| PWM | `/workspace/hpm_sdk/drivers/inc/hpm_pwm_drv.h` | `/workspace/hpm_sdk/samples/drivers/pwm/pwm_output/src/pwm.c` |
| HRPWM | `/workspace/hpm_sdk/drivers/inc/hpm_pwm_drv.h` | `/workspace/hpm_sdk/samples/drivers/pwm/hrpwm/src/hrpwm.c` |
| GPTMR PWM | `/workspace/hpm_sdk/drivers/inc/hpm_gptmr_drv.h` | `/workspace/hpm_sdk/samples/drivers/gptmr/pwm_generate/src/pwm_generate.c` |
| GPTMR 文档 | `/workspace/hpm_sdk/samples/drivers/gptmr/index_zh.rst` | `GPTMR生成PWM` 小节 |

---

## 1. 两类 PWM 的定位差异

### 1.1 专用 PWM/HRPWM 外设

专用 PWM 外设面向电机、电源、半桥/全桥、无线充等需要精确 PWM 波形的场景。SDK 示例 `pwm_output` 展示了：

- 强制输出高/低电平
- 边沿对齐 PWM
- 带 fault 失效保护的边沿对齐 PWM
- 中心对齐 PWM
- 中心对齐互补 PWM
- 带 jitter 的边沿对齐 PWM
- HRPWM 高精度输出（部分 SoC 支持）

适合：

- 高频 PWM
- 互补输出与死区
- 中心对齐控制
- fault 保护
- shadow register 同步更新
- TRGM 触发同步
- HRPWM 高分辨率占空比/相位控制

### 1.2 GPTMR 输出比较 PWM

GPTMR 是通用定时器。每个 GPTMR 通道有独立 32 位计数器、reload 和两个 compare 寄存器。SDK 文档说明：每个通道可通过两个输出比较器生成边沿 PWM 和中心对齐 PWM。

适合：

- 普通低/中频 PWM
- 简单占空比输出
- 周期定时和 PWM 复用
- 输入捕获、PWM 测量、计数等通用定时器功能
- 对死区、fault、互补、高分辨率要求不高的场景

---

## 2. 当前工程相关引脚

当前板级 pinmux 已配置以下 PWM/GPTMR 相关引脚：

| 函数 | 引脚 | 复用功能 |
|------|------|----------|
| `init_pwm0_pins()` | PA24 | `PWM0_P_0` |
| `init_pwm0_pins()` | PA25 | `PWM0_P_1` |
| `init_pwm0_pins()` | PA26 | `PWM0_P_2` |
| `init_pwm0_pins()` | PA27 | `PWM0_P_3` |
| `init_pwm1_pins()` | PA28 | `PWM1_P_4` |
| `init_pwm1_pins()` | PA29 | `PWM1_P_5` |
| `init_pwm1_pins()` | PA30 | `PWM1_P_6` |
| `init_pwm1_pins()` | PA31 | `PWM1_P_7` |
| `init_gptmr0_pins()` | PA10 | `GPTMR0_COMP_2` |
| `init_gptmr0_pins()` | PB15 | `GPTMR0_COMP_3` |
| `init_gptmr0_pins()` | PB09 | `GPTMR0_CAPT_1` |

注意：GPTMR 的 `COMP_x` 是输出比较引脚；`CAPT_x` 是输入捕获引脚，不是 PWM 输出。

---

## 3. 专用 PWM 外设使用方法

### 3.1 核心数据结构

#### `pwm_config_t`

用于配置输出通道行为：

```c
typedef struct pwm_config {
    bool enable_output;
    bool invert_output;
    uint8_t force_cmd_shadow_update_trigger;
    uint8_t fault_mode;
    uint8_t fault_recovery_trigger;
    uint8_t force_source;
    uint32_t dead_zone_in_half_cycle;
} pwm_config_t;
```

关键字段：

- `enable_output`：是否使能 PWM 输出。
- `invert_output`：是否反相输出。
- `dead_zone_in_half_cycle`：死区，单位是半个 PWM 时钟周期。
- `fault_mode`：fault 生效时输出行为，如强制 0、强制 1、高阻。
- `fault_recovery_trigger`：fault 后恢复时机。

#### `pwm_cmp_config_t`

用于配置 compare 点：

```c
typedef struct pwm_cmp_config {
    uint32_t cmp;
    bool enable_ex_cmp;
    uint8_t mode;
    uint8_t update_trigger;
    uint8_t ex_cmp;
    uint8_t half_clock_cmp;
    uint8_t jitter_cmp;
} pwm_cmp_config_t;
```

关键字段：

- `cmp`：比较值。
- `mode`：`pwm_cmp_mode_output_compare` 或 `pwm_cmp_mode_input_capture`。
- `update_trigger`：shadow register 更新触发源。
- `jitter_cmp`：抖动配置，SDK 示例用于演示 jitter PWM。

#### `pwm_pair_config_t`

互补/成对输出使用：

```c
typedef struct pwm_pair_config {
    pwm_config_t pwm[2];
} pwm_pair_config_t;
```

---

### 3.2 常用 API

| API | 用途 |
|-----|------|
| `pwm_get_default_pwm_config()` | 获取默认 PWM 输出配置 |
| `pwm_get_default_pwm_pair_config()` | 获取默认 pair 输出配置 |
| `pwm_get_default_cmp_config()` | 获取默认 compare 配置 |
| `pwm_set_reload()` | 设置周期 reload |
| `pwm_set_start_count()` | 设置计数起始值 |
| `pwm_setup_waveform()` | 配置单通道 PWM 波形 |
| `pwm_setup_waveform_in_pair()` | 配置互补/成对 PWM 波形 |
| `pwm_start_counter()` / `pwm_stop_counter()` | 启停 PWM 计数器 |
| `pwm_issue_shadow_register_lock_event()` | 触发 shadow register 生效 |
| `pwm_update_duty_edge_aligned()` | 更新边沿对齐 PWM 占空比 |
| `pwm_update_duty_central_aligned()` | 更新中心对齐 PWM 占空比 |
| `pwm_update_raw_cmp_edge_aligned()` | 直接更新边沿对齐 compare 原始值 |
| `pwm_update_raw_cmp_central_aligned()` | 直接更新中心对齐 compare 原始值 |
| `pwm_config_fault_source()` | 配置 fault 源 |
| `pwm_set_force_output()` | 软件强制输出高/低 |

---

### 3.3 边沿对齐 PWM 基本流程

SDK 示例来源：`samples/drivers/pwm/pwm_output/src/pwm.c::generate_edge_aligned_waveform()`。

步骤：

1. 初始化板级与 PWM 引脚。
2. 获取 PWM 时钟频率，计算 reload。
3. 停止 PWM 计数器。
4. 设置 reload 和 start count。
5. 配置 `pwm_config_t`。
6. 配置 compare 点。
7. 调用 `pwm_setup_waveform()` 绑定 PWM 输出通道和 compare。
8. 启动计数器。
9. 触发 shadow register 生效。
10. 运行时调用 `pwm_update_duty_edge_aligned()` 更新占空比。

示例骨架：

```c
#include "hpm_pwm_drv.h"
#include "hpm_clock_drv.h"

void pwm_edge_init(PWM_Type *pwm, clock_name_t clock_name,
                   uint8_t pwm_index, uint8_t cmp_index,
                   uint32_t freq_hz, float duty)
{
    uint32_t pwm_clk = clock_get_frequency(clock_name);
    uint32_t reload = pwm_clk / freq_hz - 1U;

    pwm_config_t pwm_config = {0};
    pwm_cmp_config_t cmp_config = {0};

    pwm_stop_counter(pwm);

    pwm_get_default_pwm_config(pwm, &pwm_config);
    pwm_config.enable_output = true;
    pwm_config.invert_output = false;
    pwm_config.dead_zone_in_half_cycle = 0;

    pwm_set_reload(pwm, 0, reload);
    pwm_set_start_count(pwm, 0, 0);

    cmp_config.mode = pwm_cmp_mode_output_compare;
    cmp_config.cmp = reload + 1U;
    cmp_config.update_trigger = pwm_shadow_register_update_on_hw_event;

    if (pwm_setup_waveform(pwm, pwm_index, &pwm_config,
                           cmp_index, &cmp_config, 1) != status_success) {
        while (1) {
        }
    }

    pwm_start_counter(pwm);
    pwm_issue_shadow_register_lock_event(pwm);
    pwm_update_duty_edge_aligned(pwm, cmp_index, duty);
}
```

占空比更新：

```c
pwm_update_duty_edge_aligned(PWM, cmp_index, 50.0f); /* 50% */
```

---

### 3.4 中心对齐 PWM

SDK 示例来源：`generate_central_aligned_waveform()`。

中心对齐通常使用两个 compare 点表示有效脉冲的两个边沿，SDK 提供专用更新 API：

```c
pwm_update_duty_central_aligned(PWM, cmp_index0, cmp_index1, duty);
```

配置要点：

- `pwm_setup_waveform()` 的 `cmp_num` 传入 2。
- `cmp_index0` 通常为偶数，`cmp_index1` 通常为奇数。
- 更新 duty 时同时更新两个 compare。

---

### 3.5 互补 PWM 与死区

SDK 示例来源：`generate_central_aligned_waveform_in_pair()`。

使用 `pwm_pair_config_t` 和 `pwm_setup_waveform_in_pair()`：

```c
pwm_pair_config_t pair = {0};
pwm_get_default_pwm_pair_config(PWM, &pair);

pair.pwm[0].enable_output = true;
pair.pwm[0].dead_zone_in_half_cycle = 8000;
pair.pwm[0].invert_output = false;

pair.pwm[1].enable_output = true;
pair.pwm[1].dead_zone_in_half_cycle = 16000;
pair.pwm[1].invert_output = false;

pwm_setup_waveform_in_pair(PWM, PWM_OUTPUT_PIN1, &pair,
                           cmp_index, cmp_config, 2);
```

适合半桥/全桥驱动。无线充功率级如果需要互补驱动，应优先考虑专用 PWM，而不是 GPTMR。

---

### 3.6 强制输出

SDK 示例来源：`test_pwm_force_output()`。

```c
pwm_config_force_cmd_timing(PWM, pwm_force_immediately);
pwm_enable_pwm_sw_force_output(PWM, PWM_OUTPUT_PIN1);

pwm_set_force_output(PWM, PWM_FORCE_OUTPUT(PWM_OUTPUT_PIN1, pwm_output_1));
pwm_enable_sw_force(PWM);

/* 改为低电平 */
pwm_set_force_output(PWM, PWM_FORCE_OUTPUT(PWM_OUTPUT_PIN1, pwm_output_0));

pwm_disable_sw_force(PWM);
pwm_disable_pwm_sw_force_output(PWM, PWM_OUTPUT_PIN1);
```

用途：上电安全态、调试、fault 前后控制输出态。

---

### 3.7 fault 失效保护

SDK 示例 `generate_edge_aligned_waveform_fault_mode()` 里使用：

```c
pwm_fault_source_config_t fault_config = {0};

pwm_config.fault_mode = pwm_fault_mode_force_output_0;
pwm_config.fault_recovery_trigger = pwm_fault_recovery_immediately;

fault_config.source_mask = pwm_fault_source_debug;
pwm_config_fault_source(PWM, &fault_config);
```

常见 fault 输出模式：

- `pwm_fault_mode_force_output_0`
- `pwm_fault_mode_force_output_1`
- `pwm_fault_mode_force_output_highz`

常见恢复触发：

- `pwm_fault_recovery_immediately`
- `pwm_fault_recovery_on_reload`
- `pwm_fault_recovery_on_hw_event`
- `pwm_fault_recovery_on_fault_clear`

---

### 3.8 HRPWM

若 SoC 定义 `PWM_SOC_HRPWM_SUPPORT`，`hpm_pwm_drv.h` 会暴露 HRPWM 相关字段和 API。SDK 示例 `samples/drivers/pwm/hrpwm/src/hrpwm.c` 展示：

- HRPWM 边沿对齐输出
- HRPWM 中心对齐输出
- 频率动态变化
- fault capture 后调用 `pwm_recovery_hrpwm_output()` 恢复输出

HRPWM 比普通 PWM 多出高分辨率 compare/reload 能力，适合需要亚时钟级占空比分辨率的电源控制。

---

## 4. GPTMR 生成 PWM 使用方法

### 4.1 核心数据结构

GPTMR 通道配置结构：

```c
typedef struct gptmr_channel_cfg {
    gptmr_work_mode_t mode;
    gptmr_dma_request_event_t dma_request_event;
    gptmr_synci_edge_t synci_edge;
    uint32_t cmp[GPTMR_CH_CMP_COUNT];
    uint32_t reload;
    bool cmp_initial_polarity_high;
    bool enable_cmp_output;
    bool enable_sync_follow_previous_channel;
    bool enable_software_sync;
    bool debug_mode;
} gptmr_channel_config_t;
```

关键字段：

- `reload`：周期计数值。
- `cmp[0]`、`cmp[1]`：两个输出比较值。
- `cmp_initial_polarity_high`：输出比较初始极性。
- `enable_cmp_output`：是否输出比较波形。
- `mode`：PWM 输出时一般使用 `gptmr_work_mode_no_capture`。
- `enable_software_sync`：是否使用软件同步。

SDK 文档提示：默认 `enable_cmp_output = true`，如果要定时但不输出，需显式置为 `false`。

---

### 4.2 常用 API

| API | 用途 |
|-----|------|
| `gptmr_channel_get_default_config()` | 获取通道默认配置 |
| `gptmr_channel_config()` | 配置 GPTMR 通道 |
| `gptmr_start_counter()` / `gptmr_stop_counter()` | 启停通道计数器 |
| `gptmr_channel_reset_count()` | 复位通道计数器 |
| `gptmr_update_cmp()` | 更新 compare 值 |
| `gptmr_channel_config_update_reload()` | 更新 reload 值 |
| `gptmr_enable_cmp_output()` / `gptmr_disable_cmp_output()` | 使能/关闭比较输出 |

注意：`gptmr_update_cmp()` 和 `gptmr_channel_config_update_reload()` 内部会对非 0、非 `0xFFFFFFFF` 的值做 `value--` 处理。因此应用层通常按“期望计数周期”传入即可，不需要自己再减 1。

---

### 4.3 GPTMR 边沿 PWM 基本流程

SDK 示例来源：`samples/drivers/gptmr/pwm_generate/src/pwm_generate.c`。

步骤：

1. `board_init()`。
2. 初始化 GPTMR 输出比较引脚，例如 `init_gptmr_pins(APP_BOARD_PWM)`。
3. 获取或初始化 GPTMR 时钟，例如 `board_init_gptmr_clock()` 或 `clock_get_frequency(clock_gptmr0)`。
4. `gptmr_channel_get_default_config()` 获取默认配置。
5. 设置 `config.reload = gptmr_freq / pwm_freq`。
6. 设置 `config.cmp_initial_polarity_high`。
7. 停止计数器。
8. `gptmr_channel_config(..., false)` 配置但不立即启动。
9. `gptmr_channel_reset_count()`。
10. `gptmr_start_counter()`。
11. 用 `gptmr_update_cmp()` 设置 duty。

SDK 示例核心代码：

```c
static void set_pwm_waveform_edge_aligned_frequency(uint32_t freq)
{
    gptmr_channel_config_t config;
    uint32_t gptmr_freq;

    gptmr_freq = board_init_gptmr_clock(APP_BOARD_PWM);
    gptmr_channel_get_default_config(APP_BOARD_PWM, &config);
    current_reload = gptmr_freq / freq;
    config.reload = current_reload;
    config.cmp_initial_polarity_high = false;

    gptmr_stop_counter(APP_BOARD_PWM, APP_BOARD_PWM_CH);
    gptmr_channel_config(APP_BOARD_PWM, APP_BOARD_PWM_CH, &config, false);
    gptmr_channel_reset_count(APP_BOARD_PWM, APP_BOARD_PWM_CH);
    gptmr_start_counter(APP_BOARD_PWM, APP_BOARD_PWM_CH);
}

static void set_pwm_waveform_edge_aligned_duty(uint8_t duty)
{
    uint32_t cmp;

    if (duty > 100) {
        duty = 100;
    }

    cmp = (current_reload * duty) / 100;
    gptmr_update_cmp(APP_BOARD_PWM, APP_BOARD_PWM_CH, 0, cmp);
    gptmr_update_cmp(APP_BOARD_PWM, APP_BOARD_PWM_CH, 1, current_reload);
}
```

---

### 4.4 GPTMR 周期和占空比计算

频率：

```c
reload = gptmr_clk_hz / pwm_freq_hz;
```

占空比：

```c
cmp0 = reload * duty_percent / 100;
cmp1 = reload;
```

然后：

```c
gptmr_update_cmp(GPTMR, ch, 0, cmp0);
gptmr_update_cmp(GPTMR, ch, 1, cmp1);
```

若要修改频率：

```c
gptmr_stop_counter(GPTMR, ch);
gptmr_channel_config_update_reload(GPTMR, ch, new_reload);
gptmr_channel_reset_count(GPTMR, ch);
gptmr_start_counter(GPTMR, ch);
```

更稳妥的方式是参考 SDK 示例：重新 `gptmr_channel_config()` 后复位并启动。

---

## 5. PWM 与 GPTMR PWM 对比

| 对比项 | 专用 PWM/HRPWM | GPTMR PWM |
|--------|----------------|-----------|
| 主要用途 | 电机、电源、功率级控制 | 普通定时/PWM 输出 |
| 波形能力 | 边沿对齐、中心对齐、互补、死区、jitter | 通过 compare 生成基础 PWM |
| 高精度 | 支持 HRPWM 的 SoC 可亚时钟分辨率 | 32 位计数器分辨率，通常无 HRPWM 能力 |
| 安全保护 | fault 输入、强制输出、高阻等 | 通用定时器级别，保护能力弱 |
| 同步能力 | shadow register、TRGM、reload/synci | 通道同步、软件同步、部分 SoC 支持 opmode/burst |
| 动态占空比 | `pwm_update_duty_*()` 封装好 | 手动计算 compare 后 `gptmr_update_cmp()` |
| 动态频率 | 更新 reload + shadow 机制 | 更新 reload 或重配通道 |
| 复杂度 | 较高，但能力强 | 简单直接 |
| 推荐场景 | 无线充功率 PWM、半桥、互补驱动 | 蜂鸣器、LED 调光、简单控制信号 |

---

## 6. 在本工程中的建议

### 6.1 无线充功率控制优先使用专用 PWM

本工程是无线充项目，如果 PWM 用于功率级驱动，建议优先使用 `PWM0/PWM1` 专用 PWM 外设，原因：

- 可做互补输出和死区控制。
- 可接 fault 保护链路。
- 可用 shadow register 保证同步更新。
- 后续如果需要高分辨率或中心对齐，迁移成本更低。

当前板级已配置：

- `PWM0_P_0` ~ `PWM0_P_3`：PA24~PA27
- `PWM1_P_4` ~ `PWM1_P_7`：PA28~PA31

### 6.2 普通辅助 PWM 可使用 GPTMR

如果只是辅助信号，比如普通 LED 调光、蜂鸣器、低速控制输出，可使用 GPTMR：

- `GPTMR0_COMP_2`：PA10
- `GPTMR0_COMP_3`：PB15

GPTMR 使用更简单，但不建议承担需要互补死区和 fault 的功率 PWM。

---

## 7. 常见踩坑

1. **忘记初始化 pinmux**  
   PWM 必须调用对应 `init_pwm*_pins()` 或 `init_gptmr*_pins()`，否则外设内部工作但引脚无输出。

2. **忘记打开外设时钟**  
   需要通过 board 层或 clock driver 确保 PWM/GPTMR 时钟已加入 clock group。

3. **reload 计算差异**  
   PWM 示例里常见：`reload = freq / 1000 * period_ms - 1`。  
   GPTMR 示例里传入 `config.reload = gptmr_freq / freq`，驱动内部会处理寄存器减 1。

4. **shadow register 未生效**  
   专用 PWM 配置后通常需要：

   ```c
   pwm_issue_shadow_register_lock_event(PWM);
   ```

5. **GPTMR compare 输出极性反了**  
   调整 `cmp_initial_polarity_high`，或检查引脚外部电路是否反相。

6. **把 GPTMR CAPT 当作 PWM 输出**  
   `CAPT_x` 是输入捕获；PWM 输出应使用 `COMP_x`。

7. **在 App 层直接 include `hpm_*`**  
   本工程分层规范禁止 App 直接依赖 HPM SDK。实际落地时应在 `Driver/hpm_impl` 内封装 PWM/GPTMR，向上提供 `Interface` 抽象。

---

## 8. 后续封装建议

如果要在本工程继续实现 PWM 抽象，建议接口层统一暴露：

```c
typedef enum {
    INTF_PWM_ALIGN_EDGE,
    INTF_PWM_ALIGN_CENTER,
} intf_pwm_align_t;

typedef struct {
    uint8_t instance;
    uint8_t channel;
    uint32_t freq_hz;
    float duty;
    intf_pwm_align_t align;
    bool complementary;
    uint32_t deadtime_ns;
} intf_pwm_cfg_t;
```

Driver 层可根据配置选择：

- 功率控制：映射到 `HPM_PWMx`。
- 简单输出：映射到 `HPM_GPTMRx`。

但建议不要在同一个接口里暴露 HPM SDK 的 `PWM_Type`、`GPTMR_Type`、`clock_name_t`，以保持 App 与 SDK 解耦。
