# ADC 驱动设计说明

本文描述当前工程中 ADC16 驱动的设计边界、接口契约、硬件映射和开发指南。

> **当前实现状态**：驱动已完成，通过硬件实测验证。
> - ADC0 PMT + PWM + TRGM 联动：✅ 正常工作（200kHz 触发，4 通道 DMA）
> - ADC1 PMT + PWM + TRGM 联动：✅ 正常工作（独立 `TRG1A`，2 通道 DMA）
> - Oneshot / Period / Seq+DMA / Watchdog / 自动校准 / VREF 动态配置均已可用

---

## 1. 分层目标

本设计遵循工程 `AGENTS.md` 中的"物理隔离 + 契约驱动"原则：

| 层级 | 当前文件 | 职责 |
|------|----------|------|
| `Interface/` | `intf_adc.h` | 定义纯 C 契约（`intf_adc_t` + 匿名结构体），不暴露 `hpm_*` 类型 |
| `Interface/` | `intf_default.c` | 接口注册/分发实现，多实例 ops 指针数组 |
| `Driver/hpm_impl/` | `drv_adc.c` | 将接口调用映射到 HPM SDK `adc16_*` API |
| `Board/` | `pinmux.c` | 模拟引脚配置（`init_analog_pins()`），仅物理引脚 |

驱动注册入口：

```c
void hpm_adc_driver_register(void);
```

该函数位于 Driver 层，在 App 初始化时调用，遵循 `app_gpio.c` 和 `app_hrpwm.c` 已有的注册范例。

---

## 2. ADC16 硬件特性

### 2.1 HPM5361 ADC16 规格

**双 ADC 实例**：
- `HPM_ADC0` — 基址 `0xF3080000`，通道 0–15
- `HPM_ADC1` — 基址 `0xF3084000`，通道 0–15

**SoC 特性**：

| 宏 | 值 | 说明 |
|----|-----|------|
| `ADC16_SOC_MAX_CH_NUM` | 15 | 最大模拟通道数 (0–14) |
| `ADC16_SOC_MAX_SAMPLE_VALUE` | 65535 | 16-bit 最大采样值 |
| `ADC16_SOC_MAX_CONV_CLK_NUM` | 21 | 16-bit 模式所需转换时钟周期数 |
| `ADC16_SOC_PARAMS_LEN` | 34 | 参数表长度 |
| `ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT` | 1 | 支持 Bus/Oneshot 模式 |

### 2.2 转换模式

| 模式 | 枚举 | 触发方式 | 说明 |
|------|------|---------|------|
| **Oneshot (Bus)** | `INTF_ADC_MODE_ONESHOT` | 软件 `read()` 触发 | 每次调用触发单次转换，结果立即可读 |
| **Period** | `INTF_ADC_MODE_PERIOD` | 自动连续 | 按配置周期连续采样，`read()` 返回最新值 |
| **PMT** | `INTF_ADC_MODE_PMT` | 硬件触发 (TRGM) | 由 PWM/外部信号触发，多通道顺序采样，完成中断回调 |
| **Seq** | `INTF_ADC_MODE_SEQ` | HW/SW 可选 | 多通道自动扫描，结果经 DMA 写入用户缓冲区 |

### 2.3 PMT / Seq 硬件能力

| 参数 | 值 | 说明 |
|------|-----|------|
| 每实例触发通道数 | 11 (TRG0A–TRG10A) | PMT 模式：可路由到不同 PWM 比较点 |
| 每触发转换通道数 (PMT) | 1–4 | 一次触发 → 顺序采样最多 4 个通道 |
| Seq 队列长度 | 1–16 | Sequence 模式：一次启动扫描最多 16 个通道 |
| Seq DMA 缓冲区 | `ADC_SOC_SEQ_MAX_DMA_BUFF_LEN` | ~16 MB 上限 |
| PMT DMA 缓冲区 | `ADC_SOC_PMT_MAX_DMA_BUFF_LEN` | 48 个 `uint32_t`，按 `pmt_trig_ch * 4` 分 slot |
| 完成事件 | `TRIG_CMPT` / `SEQ_CVC` / `SEQ_CMPT` | 触发组 / 单次 / 队列全部完成 |
| 中断向量 | `IRQn_ADC0` (58) / `IRQn_ADC1` (59) | 向量独立；ADC1 PMT 完成在 HPM5361 上需兼容 `IRQn_ADC0` 共享触发路径 |

### 2.4 分辨率选项

| 分辨率 | 枚举 | 转换时钟周期 | 最大原始值 |
|--------|------|-------------|-----------|
| 8-bit | `INTF_ADC_RES_8_BITS` | 9 | 255 |
| 10-bit | `INTF_ADC_RES_10_BITS` | 11 | 1023 |
| 12-bit | `INTF_ADC_RES_12_BITS` | 14 | 4095 |
| 16-bit | `INTF_ADC_RES_16_BITS` | 21 | 65535 |

### 2.5 时钟

- **时钟域说明**：当前工程 `CPU0` 主频实际配置为 **480MHz**，但 `clock_adc0` / `clock_adc1` 取自 **AHB / 外设时钟域**，当前按 **120MHz** 理解。以下表格中的 `120MHz` 指 ADC 上游总线时钟，不是 CPU 核心时钟。
- 时钟源：`clock_adc0` / `clock_adc1`（AHB 总线时钟，当前 120 MHz）
- 时钟分频：可配置 1–16，驱动自动遵守 **≤ 50 MHz** 手册限制（120 MHz 上游时钟下最小分频 ≥ 3）
- `sample_cycle`：可配置 1–2³²（0 = 默认 20），影响每次转换的采样时长
- 单次转换耗时 ≈ (sample_cycles + conv_cycles) / ADC 时钟

| 配置 | ADC 时钟 | 16-bit 耗时 | 说明 |
|:---|:---|:---|:---|
| `clock_div=4`（默认） | 30 MHz | ~1.37 µs | 通用 |
| `clock_div=3`（最快） | 40 MHz | ~1.03 µs | 控制环极速 |
| `clock_div=8, sample_cycle=40` | 15 MHz | ~4.07 µs | 高精度/低噪声 |

### 2.6 硬件行为注意事项 (实测验证)

| 事项 | 说明 |
|:---|:---|
| **ADC 时钟使能** | `adc16_init()` 和校准完成后会关闭 `ANA_CTRL0.ADC_CLK_ON`，驱动在 init/calibrate 末尾显式重新打开，否则转换不工作 |
| **默认 nonblocking** | SDK `adc16_get_default_config()` 默认 `wait_dis=true`。当前驱动保持 nonblocking，避免 PMT ISR 读取结果时阻塞总线；Oneshot 通过重试读取稳定结果 |
| **Oneshot 单通道** | Bus 模式下 BUS_RESULT 共享同一转换输出，切换通道后前 2 次读数无效，第 3 次起稳定。多通道请用 Sequence 或 PMT |
| **校准自动触发** | `adc16_init()` 内部自动调用 `adc16_do_calibration()`，首次 init 即校准。重校准调用 `intf_adc_calibrate()` |
| **双实例独立** | ADC0 和 ADC1 完全独立：各自分辨率/模式/时钟分频。同一物理引脚可同时被两个实例采样 |
| **引脚≠通道号** | HPM5361 引脚后缀不等于 ADC 通道号（如 PB08→ch11），需查数据手册 |
| **PMT DMA slot** | PMT DMA 写入地址以 `pmt_trig_ch * 4` 为起始 slot，每个触发通道固定占 4 个 `adc16_pmt_dma_data_t`，不能总从 `dma_buff[0]` 读取 |
| **ADC1 PMT IRQ** | HPM5361/HPM5300 SDK motor sample 显示 ADC0/ADC1 PMT 触发完成存在 `IRQn_ADC0` 共享路径；驱动在 `isr_adc0()` 中仅当 ADC1 有 pending status 时兜底处理 ADC1 |

---

## 3. 对外接口契约

文件：`Interface/intf_adc.h`

### 3.1 通道编码

HPM5361 有 2 个 ADC16 实例，每个最多 16 通道。`intf_adc_ch_t` 是 `uint8_t`，编码方式：

```
bits [7:4] = 实例 (0 → ADC0, 1 → ADC1)
bits [3:0] = 物理通道索引 (0–15)
```

宏定义：

```c
typedef uint8_t intf_adc_ch_t;

#define INTF_ADC_CH(inst, idx)  ((intf_adc_ch_t)(((uint8_t)(inst) << 4) | ((uint8_t)(idx) & 0x0FU)))
#define INTF_ADC_CH_INST(ch)    ((uint8_t)((ch) >> 4))
#define INTF_ADC_CH_IDX(ch)     ((uint8_t)((ch) & 0x0FU))
#define INTF_ADC_INSTANCE_COUNT (2U)
```

使用示例：
```c
INTF_ADC_CH(0, 11)  // ADC0 通道 11 (PB08)
INTF_ADC_CH(0, 2)   // ADC0 通道 2  (PB10)
INTF_ADC_CH(1, 6)   // ADC1 通道 6  (PB14)
```

### 3.2 类型定义

```c
typedef enum {
    INTF_ADC_RES_8_BITS  = 8, INTF_ADC_RES_10_BITS = 10,
    INTF_ADC_RES_12_BITS = 12, INTF_ADC_RES_16_BITS = 16,
} intf_adc_resolution_t;
#define INTF_ADC_RES_DEFAULT  INTF_ADC_RES_16_BITS

typedef enum {
    INTF_ADC_MODE_ONESHOT = 0,  // 软件 read() 触发
    INTF_ADC_MODE_PERIOD  = 1,  // 连续周期采样
    INTF_ADC_MODE_PMT     = 2,  // 硬件触发 (TRGM) 抢占
    INTF_ADC_MODE_SEQ     = 3,  // 多通道序列扫描 + DMA
} intf_adc_mode_t;

typedef void (*intf_adc_pmt_cb_t) (intf_adc_ch_t trig_ch, const uint16_t *values, uint8_t count, void *user);
typedef void (*intf_adc_seq_cb_t) (intf_adc_ch_t trig_ch, void *user);
typedef void (*intf_adc_wdog_cb_t)(intf_adc_ch_t ch, uint16_t value, void *user);

typedef struct {
    intf_adc_resolution_t resolution;
    intf_adc_mode_t       mode;
    uint32_t              sample_rate_hz;   // target rate (Period) / 0 = default
    uint32_t              sample_cycle;     // cycles per sample (0 = default 20)
    uint32_t              clock_div;        // divider 1–16 (0 = auto)
    float                 vref_mv;          // reference mV (0 = default 3300)
    /* DMA (PMT / Seq) */
    bool                  dma_en;
    uint32_t             *dma_buff;
    uint32_t              dma_buff_len;
    /* PMT */
    uint8_t               pmt_trig_ch;      // 0–10
    uint8_t               pmt_ch_count;     // 1–4
    uint8_t               pmt_ch_list[4];
    intf_adc_pmt_cb_t     pmt_cb;
    void                 *pmt_cb_user_data;
    /* Sequence */
    bool                  seq_hw_trig;      // true=TRGM, false=SW
    uint8_t               seq_ch_count;     // 1–16
    uint8_t               seq_ch_list[16];
    intf_adc_seq_cb_t     seq_cb;
    void                 *seq_cb_user_data;
    /* Watchdog */
    bool                  wdog_en;
    uint16_t              wdog_thshd_high;
    uint16_t              wdog_thshd_low;
    intf_adc_wdog_cb_t    wdog_cb;
    void                 *wdog_cb_user_data;
} intf_adc_cfg_t;

typedef struct {
    uint8_t instance_id;
    struct {
        int  (*init)(intf_adc_ch_t ch, const intf_adc_cfg_t *cfg);
        int  (*read)(intf_adc_ch_t ch, uint16_t *value);
        int  (*read_voltage)(intf_adc_ch_t ch, float *voltage_mv);
        int  (*start)(intf_adc_ch_t ch);
        int  (*stop)(intf_adc_ch_t ch);
        void (*set_vref)(float vref_mv);
        int  (*calibrate)(void);
        void (*deinit)(intf_adc_ch_t ch);
    };
} intf_adc_t;
```

### 3.3 功能 API

```c
int  intf_adc_register(const intf_adc_t *ops);
int  intf_adc_init(intf_adc_ch_t ch, const intf_adc_cfg_t *cfg);
int  intf_adc_read(intf_adc_ch_t ch, uint16_t *value);
int  intf_adc_read_voltage(intf_adc_ch_t ch, float *voltage_mv);
int  intf_adc_start(intf_adc_ch_t ch);
int  intf_adc_stop(intf_adc_ch_t ch);
void intf_adc_set_vref(intf_adc_ch_t ch, float vref_mv);
int  intf_adc_calibrate(intf_adc_ch_t ch);
void intf_adc_wdog_reenable(intf_adc_ch_t ch);
```
```

### 3.4 参数约束

| 参数 | 约束 | 说明 |
|------|------|------|
| `resolution` | 8/10/12/16 | 实例级参数，首次 init 设置后不可更改 |
| `mode` | `ONESHOT` / `PERIOD` / `PMT` | 实例级参数，首次 init 设置后不可更改 |
| `sample_rate_hz` | `> 0`（Period 模式） | PMT/Oneshot 模式忽略 |
| `vref_mv` | `> 0` 或 `0` | `0` 表示使用驱动默认值 3300mV |
| `dma_en` | bool | PMT / Seq 模式下启用 DMA 自动搬运，Oneshot / Period 模式不支持（返回 -1） |
| `pmt_trig_ch` | 0–10 | PMT 触发通道索引 |
| `pmt_ch_count` | 1–4 | 该触发组采样的通道数 |
| `pmt_ch_list` | 有效通道号 | 每个元素 `0..15` |
| `pmt_cb` | NULL 或函数指针 | PMT 模式下允许为 NULL（仅用中断标记） |
| `ch` | 0–31（编码含实例） | PMT/Seq 模式下仅用于实例选择，通道部分忽略 |

错误返回：`0` 表示成功，`-1` 表示参数、状态或底层 SDK 调用失败。

---

## 4. 板级模拟通道

### 4.1 引脚映射原理

HPM5361 的物理引脚与 ADC 通道之间的映射由**硅片硬连线决定，不可通过软件更改**。注意：**引脚后缀号不等于 ADC 通道号**——需查阅芯片数据手册获取精确映射。

`IOC_PAD_FUNC_CTL_ANALOG_MASK` 的作用是**打开模拟开关**（将引脚从数字模式切换到模拟透传），而非选择通道——通道号由芯片内部模拟总线决定。

### 4.2 两层可变性

```
物理引脚 ──→ ADC 通道          固定（硅片硬连线）
ADC 通道 ──→ ADC 实例 (0/1)    可变（软件选择）
```

两个 ADC 实例共享同一模拟总线，**均可独立、同时访问任意已使能的通道**：

```
PB08 ──→ ch11 ──┬── ADC0  (16-bit, 高频)  ← INTF_ADC_CH(0, 11)
                 └── ADC1  (12-bit, 低频)  ← INTF_ADC_CH(1, 11)
```

### 4.3 当前已配置的模拟引脚（依据 HPM5361 数据手册）

| 引脚 | ADC 通道号 | 可接入的 ADC 实例 | 说明 |
|------|-----------|-------------------|------|
| PB08 | 11 | ADC0 / ADC1 | 模拟输入 |
| PB10 | 2 | ADC0 / ADC1 | 模拟输入 |
| PB11 | 3 | ADC0 / ADC1 | V_LINK (Buck-Boost输出 / LCC全桥输入) |
| PB12 | 4 | ADC0 / ADC1 | 模拟输入 |
| PB13 | 5 | ADC0 / ADC1 | 模拟输入 |
| PB14 | 6 | ADC0 / ADC1 | 模拟输入 |

如需添加新引脚（如 PB09），需先查阅数据手册确认其 ADC 通道号，然后在 `pinmux.c` 中增加：
```c
HPM_IOC->PAD[IOC_PAD_PB09].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
```
然后在 App 层通过 `INTF_ADC_CH(实例, 通道号)` 选择由哪个实例读取。

---

## 5. ADC 驱动设计

文件：`Driver/hpm_impl/drv_adc.c`

### 5.1 状态模型

```c
typedef struct {
    bool       configured;
    bool       running;
} adc_ch_state_t;

typedef struct {
    bool                  initialized;
    intf_adc_resolution_t resolution;
    intf_adc_mode_t       mode;
    float                 vref_mv;
    ADC16_Type           *base;
    adc_ch_state_t        channels[ADC_MAX_CHANNELS];
    struct {
        uint8_t            trig_ch;
        uint8_t            ch_count;
        uint8_t            ch_list[4];
        intf_adc_pmt_cb_t  cb;
        void              *cb_user_data;
    } pmt;
} adc_inst_t;

static adc_inst_t adc_instances[INTF_ADC_INSTANCE_COUNT];
```

设计要点：

- `HPM_ADC0` 和 `HPM_ADC1` 各自独立，有独立的 resolution / mode / vref。
- 每个实例有 16 个通道，独立的 `configured` / `running` 状态。
- `resolution` 和 `mode` 是实例级的，第一个 `init()` 调用锁定全局配置。
- `vref_mv` 是实例级的，首次 init 设置，后续可通过 `set_vref()` 动态更新。
- `pmt` 子结构仅在 PMT 模式下使用，保存触发通道、采样列表和回调。每实例一个触发组。

### 5.2 初始化流程

`adc_init(ch, cfg)`：

1. 解析通道编码：`inst = INTF_ADC_CH_INST(ch)`，`ch_idx = INTF_ADC_CH_IDX(ch)`。
2. 检查 `inst` / `ch_idx` / `cfg` 合法性。
3. **首次初始化该实例**（`!initialized`）：
   - 调用 `adc16_get_default_config()` 获取 SDK 默认配置。
   - 映射分辨率：`adc_map_resolution(cfg->resolution)` → `adc16_res_*`。
   - 映射模式：`adc_map_mode(cfg->mode)` → `adc16_conv_mode_*`。
   - 根据 `sample_rate_hz` 计算时钟分频；Oneshot 模式使用默认分频 4。
   - 调用 `adc16_init(base, &adc_cfg)` 初始化硬件。
   - 存储实例级配置（resolution / mode / vref / base）。
4. **后续初始化同一实例**：校验 `resolution` / `mode` 与首次一致，不一致返回 `-1`。
5. 配置通道：
   - 调用 `adc16_get_channel_default_config()` 获取默认通道配置。
   - 设置通道号和采样周期 (`ADC_DEFAULT_SAMPLE_CYCLE = 20`)。
   - 调用 `adc16_init_channel(base, &ch_cfg)`。
6. **Oneshot 模式**：
   - 调用 `adc16_set_nonblocking_read()` 启用 Bus 模式。
   - 调用 `adc16_enable_oneshot_mode()` 启用 oneshot 模式。
7. 标记通道 `configured = true`。

### 5.3 Oneshot 读取

`adc_read(ch, value)`：

```c
adc16_get_oneshot_result(adc_inst->base, ch_idx, value);
```

每次调用触发一次硬件转换，**阻塞等待**结果返回（`wait_dis=false`）。

**重要限制：Oneshot bus 模式仅支持反复读取同一通道。** 切换通道后再读，前 1-2 次为残留值，第 3 次起稳定。这是 HPM ADC16 硬件特性——BUS_RESULT 寄存器在 bus 模式下共享同一个转换输出。SDK 官方示例也只用单通道。多通道扫描请使用 Sequence 或 PMT 模式。

App 层 `adc_init()` 末尾会自动冲刷所有通道的残留值（dummy read × 6）。运行时如需切换通道读取，建议每条通道连续读 3 次、取最后一次：

```c
adc_read_raw(ch);   // discard: channel switch
adc_read_raw(ch);   // discard: conversion settle
val = adc_read_raw(ch);  // keep: valid
```

### 5.4 Period 读取

`adc_read(ch, value)`：

```c
adc16_get_prd_result(adc_inst->base, ch_idx, value);
```

`adc_start(ch)` 在 Period 模式下会配置 `adc16_prd_config_t` 并启动连续采样：

```c
adc16_prd_config_t prd_cfg;
prd_cfg.ch           = ch_idx;
prd_cfg.prescale     = 20;   // 2^20 ADC 时钟分频
prd_cfg.period_count = 5;    // 6 个 ADP 周期
adc16_set_prd_config(adc_inst->base, &prd_cfg);
```

### 5.5 电压转换

`adc_read_voltage(ch, voltage_mv)`：

1. 调用 `adc_read(ch, &raw)` 获取原始值。
2. 查表获取当前分辨率对应的最大值（255/1023/4095/65535）。
3. 使用实例存储的 `vref_mv` 计算：
   ```
   voltage_mv = raw * vref_mv / max_value
   ```

### 5.6 VREF 动态配置

**初始化时**：通过 `intf_adc_cfg_t.vref_mv` 传入，若为 `0` 则使用驱动默认值 `ADC_DEFAULT_VREF_MV (3300.0f)`。

**运行时**：调用 `intf_adc_set_vref(ch, vref_mv)`，传入 `0` 恢复默认值。
该函数更新全局所有实例的 VREF（实现为遍历 `adc_instances[0..1]`）。

### 5.7 自动偏移校准

HPM ADC16 的校准由 SDK 的 `adc16_do_calibration()` 完成（静态函数，不公开）。`adc16_init()` 内部自动调用，因此**每次首次 `init` 即触发校准**，无需手动干预。

校准流程（SDK 内部实现）：
1. 临时设时钟分频为 1，使能 ADC 时钟和 bandgap
2. 执行 4 次校准循环，每次 poll `CALON` 状态等待完成
3. 从 `ADC16_PARAMS[0..33]` 读取参数，取平均值
4. 参数后处理，写入校准寄存器

**运行时重校准**：调用 `intf_adc_calibrate(ch)`，内部遍历所有已初始化的实例重新执行 `adc16_init()`。不破坏已有通道配置。

```c
intf_adc_calibrate(INTF_ADC_CH(0, 0));  // 重校准 ADC0+ADC1 全部已初始化实例
```

### 5.8 PMT 模式

`adc_init(ch, cfg)` 在 `mode == INTF_ADC_MODE_PMT` 时的额外流程：

1. 校验 `pmt_trig_ch < 11`、`pmt_ch_count` 在 1–4 范围内。
2. 保存 PMT 状态到 `adc_inst->pmt`（触发通道、通道列表、回调指针）。
3. 逐通道调用 `adc16_init_channel()` 初始化 `pmt_ch_list` 中的每个 ADC 通道。
4. 配置 `adc16_pmt_config_t`：
   - `trig_ch = cfg->pmt_trig_ch`
   - `trig_len = cfg->pmt_ch_count`
   - `adc_ch[i] = cfg->pmt_ch_list[i]`
   - `inten[i]`：仅最后一通道使能（`i == pmt_ch_count - 1`），触发组完成时产生一次 `TRIG_CMPT` 中断。
5. 调用 `adc16_set_pmt_config()` 和 `adc16_enable_pmt_queue()`。
6. 若 `dma_en=true`，调用 `adc16_init_pmt_dma()` 将 ADC 内部 PMT DMA 写地址指向用户提供的非缓存缓冲区。
7. 校验 PMT DMA buffer 长度必须覆盖 `pmt_trig_ch * 4 + pmt_ch_count`，避免读取非 0 触发通道时越界。
8. 使能 `adc16_event_trig_complete` 中断并注册 ISR（`intc_m_enable_irq_with_priority`）。

**ADC0/ADC1 ISR 当前处理方式**：

```c
SDK_DECLARE_EXT_ISR_M(IRQn_ADC0, isr_adc0)
void isr_adc0(void)
{
    adc_generic_isr(0);
    if (adc_has_pending_status(1)) {
        adc_generic_isr(1);  // ADC1 PMT 在 HPM5361 上存在 ADC0 IRQ 共享路径
    }
}

SDK_DECLARE_EXT_ISR_M(IRQn_ADC1, isr_adc1)
void isr_adc1(void)
{
    adc_generic_isr(1);
}
```

`adc_generic_isr()` 的 PMT 分支逻辑：
1. 读 `adc16_get_status_flags()` 检查 `TRIG_CMPT`。
2. 清除中断标志。
3. 若 `dma.active=true`，按 `dma_offset = pmt_trig_ch * 4` 定位 PMT DMA slot，并从该 slot 起读取 `pmt_ch_count` 个结果。
4. 若未启用 DMA，则遍历 `pmt_ch_list` 读取 `BUS_RESULT` 作为 fallback。
5. 若回调非空，调用 `pmt_cb(trig_ch, values[], count, user_data)` 将全部通道值一次性传给 App 层。

> **注意**：TRGM 路由（PWM 比较器 → ADC 触发输入）由 App 层通过 `intf_trgm_connect()` 配置，不属于 ADC 驱动的职责范围。Driver 只消费已经进入 ADC 的 PMT 触发事件和 DMA 结果。

### 5.9 ADC1 PMT DMA 问题复盘（已修复）

历史现象：ADC0 的 4 个 PMT 通道正常刷新，ADC1 的 2 个 PMT 通道回调能触发，但读数长期不更新或看起来像旧数据。

根因由三个因素叠加造成：

1. **PMT DMA slot 读取错误**  
   HPM ADC16 PMT DMA buffer 不是简单从 `dma_buff[0]` 连续写所有触发通道。SDK motor sample 使用 `adc_buff[TRIG_CH * 4]` 读取结果，说明每个 `pmt_trig_ch` 固定占 4 个 `adc16_pmt_dma_data_t`。旧驱动无论 `pmt_trig_ch` 是多少都从 `dma_buff[0]` 读取，因此 ADC1 使用 `TRG0B` / `TRG1A` 等非 0 触发通道时会读错 slot。

2. **ADC1 触发路由与 ADC0 过于接近**  
   调试代码曾将 ADC1 接到 `ADCX_PTRGI0B`（`pmt_trig_ch=1`），容易误判为与 ADC0 共用同一组 PMT/DMA 资源。当前调试与推荐设计改为：
   - ADC0：`PWM0_CH8REF → ADCX_PTRGI0A`，`pmt_trig_ch=0`
   - ADC1：`PWM1_CH8REF → ADCX_PTRGI1A`，`pmt_trig_ch=3`

3. **ADC1 时钟与中断路径需要显式兜底**  
   Driver 现在在每个 ADC 实例首次初始化时显式配置 `clock_adc0/clock_adc1` 的 ADC source；同时参考 HPM SDK motor sample，在 `IRQn_ADC0` 中仅当 ADC1 有 pending status 时处理 ADC1，避免 ADC1 PMT 完成但回调链未执行。

当前状态：ADC1 PMT DMA 已通过硬件验证能正确更新，推荐继续保持 ADC0/ADC1 使用不同 `PTRGI` 组和独立 DMA buffer。

---

## 6. 注册与调用链

Driver 层提供注册函数：

```c
void hpm_adc_driver_register(void)
{
    intf_adc_register(&adc_ops_adc0);
    intf_adc_register(&adc_ops_adc1);
}
```

接口分发位于 `Interface/intf_default.c`：

```c
static const intf_adc_t *adc_ops[INTF_ADC_INSTANCE_COUNT] = {NULL};

static const intf_adc_t *adc_get_ops_by_ch(intf_adc_ch_t ch)
{
    uint8_t inst = INTF_ADC_CH_INST(ch);
    if (inst >= INTF_ADC_INSTANCE_COUNT) return NULL;
    return adc_ops[inst];
}
```

典型调用链：

```text
App 初始化
  -> hpm_adc_driver_register()                      // 注册 ADC0/ADC1 ops

  // Oneshot:
  -> intf_adc_init(INTF_ADC_CH(0, 11), &cfg)        // 初始化 ADC0 ch11 (PB08)
  -> intf_adc_read(INTF_ADC_CH(0, 11), &raw)        // 读取原始值
  -> intf_adc_read_voltage(ch, &mv)                 // 读取电压(mV)
  -> intf_adc_set_vref(ch, 3260.0f)                 // 动态校准 VREF
  -> intf_adc_calibrate(ch)                         // 运行时重校准 (所有实例)

  // PMT:
  -> intf_hrpwm_config_trigger_cmp(0/1, 8, 0.5f)    // PWM 中点产生 CH8REF
  -> intf_trgm_connect(PWMx_CH8REF, ADCX_PTRGIxA)   // 路由到 ADC PMT 触发输入
  -> intf_adc_init(INTF_ADC_CH(inst, 0), &pmt_cfg)  // 配置 ADCx PMT + DMA
  [PWM CMP → TRGM → ADC PMT → ADC internal DMA]
  -> isr_adc0()/isr_adc1() → adc_generic_isr()
  -> dma_buff[pmt_trig_ch * 4 + i] → pmt_cb(values)
```

---

## 7. 安全策略与限制

当前实现包含以下保护：

- 通道范围检查：`inst < INTF_ADC_INSTANCE_COUNT` 且 `ch_idx < ADC_MAX_CHANNELS`。
- `cfg` 空指针检查。
- 同一实例上后续 `init()` 必须与首次的 `resolution` / `mode` 一致，否则拒绝。
- Resolution / Mode 枚举非法值 fallback 到 16-bit / Oneshot。
- 时钟分频范围限制（1–16），**自动强制 ADC 时钟 ≤ 50 MHz**（手册限制）。
- `sample_cycle` / `clock_div` 为 0 时使用安全默认值（20 cycles / 4 分频）。
- DMA 仅在 PMT / Seq 模式下有效，Oneshot / Period 模式开启 DMA 返回 `-1`。
- PMT 参数校验：`pmt_trig_ch < 11`，`pmt_ch_count` 1–4。
- PMT DMA buffer 长度校验：`dma_buff_len >= pmt_trig_ch * 4 + pmt_ch_count`。
- ISR 中断优先级默认 1，可调整。
- WDOG 回调后自动关闭该通道中断，防止 flooding，需手动 `intf_adc_wdog_reenable()` 重新使能。
- ADC 时钟显式使能：`adc16_init()` 会关闭时钟，驱动在 init/calibrate 后重新打开 `ANA_CTRL0.ADC_CLK_ON`。
- PMT ISR 使用 nonblocking 访问，避免高频 PWM 触发下 ISR 内总线等待导致死锁；Oneshot 读取失败时执行有限重试。

**当前仍未完成的 ADC 能力**：

- 温度传感器通道（作为独立外设驱动单独实现）。

---

## 8. 使用示例

### 8.1 调用流程总览

```
注册 (仅一次)
  hpm_adc_driver_register()
    → intf_adc_register(&adc_ops_adc0)    ← 注册 ADC0
    → intf_adc_register(&adc_ops_adc1)    ← 注册 ADC1

初始化 (每种模式各自配置)
  intf_adc_init(INTF_ADC_CH(inst, ch), &cfg)
    → Interface: adc_get_ops_by_ch(ch)    ← 解析实例号
    → Driver:   adc_init()               ← 硬件配置 + 通道初始化

读取 (Oneshot / Period)
  intf_adc_read(INTF_ADC_CH(0, 11), &raw)
    → Interface: ops->read(ch, &val)
    → Driver:   adc16_get_oneshot_result() / adc16_get_prd_result()

读取 (PMT - 中断自动触发，DMA 优先)
  [硬件 PWM → TRGM → ADC PMT 触发]
  → ISR: isr_adc0() / isr_adc1()
    → adc_generic_isr(inst)
      → dma_offset = pmt_trig_ch * 4
      → 读取 dma_buff[dma_offset + i].result
      → pmt.cb(trig_ch, values, count, user_data)
```

### 8.2 Oneshot 单次采样

适用于低频巡检、温度检测、手动触发场景。

```c
#include "intf_adc.h"

extern void hpm_adc_driver_register(void);

void app_adc_init_oneshot(void)
{
    hpm_adc_driver_register();

    intf_adc_cfg_t cfg = {
        .resolution     = INTF_ADC_RES_DEFAULT,   // = 16-bit
        .mode           = INTF_ADC_MODE_ONESHOT,
        .sample_rate_hz = 0,
        .vref_mv        = 0,                      // 0 = 默认 3300mV
    };

    /* 逐一初始化各通道 (同一实例共享 resolution/mode) */
    intf_adc_init(INTF_ADC_CH(0, 11), &cfg);  // PB08 → ch11  输入母线电流
    intf_adc_init(INTF_ADC_CH(0, 2),  &cfg);  // PB10 → ch2   电感电流
    intf_adc_init(INTF_ADC_CH(0, 3),  &cfg);  // PB11 → ch3   V_LINK
    intf_adc_init(INTF_ADC_CH(0, 4),  &cfg);  // PB12 → ch4   线圈电流
    intf_adc_init(INTF_ADC_CH(0, 5),  &cfg);  // PB13 → ch5   LCC谐振电流
    intf_adc_init(INTF_ADC_CH(0, 6),  &cfg);  // PB14 → ch6   输入电压
}

/* 读取原始值 */
uint16_t app_read_raw(intf_adc_ch_t ch)
{
    uint16_t raw;
    intf_adc_read(ch, &raw);          // 触发一次转换 → 获取结果
    return raw;                       // 0–65535
}

/* 读取电压 (mV) */
float app_read_mv(intf_adc_ch_t ch)
{
    float mv;
    intf_adc_read_voltage(ch, &mv);   // 自动换算: raw × vref / 65535
    return mv;                        // 0.0f – 3300.0f
}
```

### 8.3 Period 连续采样

适用于独立周期采样，无需中断参与。

```c
void app_adc_init_period(void)
{
    hpm_adc_driver_register();

    intf_adc_cfg_t cfg = {
        .resolution     = INTF_ADC_RES_DEFAULT,
        .mode           = INTF_ADC_MODE_PERIOD,
        .sample_rate_hz = 10000,       // 10 kHz 连续采样
        .vref_mv        = 3300.0f,
    };

    intf_adc_init(INTF_ADC_CH(0, 6), &cfg);   // PB14 → ch6
    intf_adc_start(INTF_ADC_CH(0, 6));        // 开始连续转换
}

/* 直接读取最新结果 (不阻塞) */
uint16_t app_read_period(void)
{
    uint16_t raw;
    if (intf_adc_read(INTF_ADC_CH(0, 6), &raw) == 0) {
        return raw;
    }
    return 0;   // 读取失败 (如数据尚未就绪)
}
```

调用流程：

```
intf_adc_init()  →  adc16_init(HPM_ADC0, mode=period)
                 →  adc16_init_channel(HPM_ADC0, ch=6)

intf_adc_start() →  adc16_set_prd_config()     ← 配置周期参数
                 →  硬件自动开始连续转换

intf_adc_read()  →  adc16_get_prd_result()      ← 读最近一次结果 (不触发新转换)
```

### 8.4 PMT 多通道同步采样

适用于控制环：一次 PWM 触发同时采集电流+电压，结果通过回调送达。

```
硬件链路:  PWM0 CH8REF → TRGM → ADCX_PTRGI0A → ADC0 PMT TRG0A → PMT DMA slot0
软件链路:  isr_adc0() → adc_generic_isr(0) → pmt.cb(values, count)
```

```c
#include "intf_adc.h"

extern void hpm_adc_driver_register(void);

/* PMT 完成回调 — ISR 上下文中执行，直接运行控制算法 */
static void control_isr(intf_adc_ch_t trig_ch, const uint16_t *values, uint8_t count, void *user)
{
    (void)trig_ch;
    (void)user;

    if (count < 4) return;

    /* values[0] = ch6  (PB14: Buck-Boost 输入电压)
     * values[1] = ch11 (PB08: Buck-Boost 输入母线电流)
     * values[2] = ch2  (PB10: 电感电流)
     * values[3] = ch3  (PB11: V_LINK) */
    float V_in   = (float)values[0] * 3300.0f / 65535.0f;
    float I_in   = (float)values[1] * 3300.0f / 65535.0f;
    float I_L    = (float)values[2] * 3300.0f / 65535.0f;
    float V_link = (float)values[3] * 3300.0f / 65535.0f;

    (void)V_in;
    (void)I_in;
    (void)V_link;

    /* 在此执行电流环 PID 计算，更新 PWM 占空比 */
}

void app_adc_init_pmt_multich(void)
{
    hpm_adc_driver_register();

    intf_adc_cfg_t cfg = {
        .resolution      = INTF_ADC_RES_DEFAULT,
        .mode            = INTF_ADC_MODE_PMT,
        .vref_mv         = 3300.0f,
        .pmt_trig_ch     = 0,              /* TRG0A */
        .pmt_ch_count    = 4,
        .pmt_ch_list     = {6, 11, 2, 3},  /* 一次触发采集 4 个通道 */
        .pmt_cb          = control_isr,
        .pmt_cb_user_data = NULL,
    };

    /* ch 参数仅用于实例选择 (inst=0 → ADC0)，通道部分忽略 */
    intf_adc_init(INTF_ADC_CH(0, 0), &cfg);

    /* 需 App 层额外配置:
     *   intf_hrpwm_config_trigger_cmp(0, 8, 0.5f)
     *   intf_trgm_connect(INTF_TRGM_SRC_PWM0_CH8REF, INTF_TRGM_DST_ADC_PTRGI0A) */
}
```

ISR 内部执行流程：

```
isr_adc0()
  └─ adc_generic_isr(0)
       ├─ status = adc16_get_status_flags(HPM_ADC0)
       ├─ if (!TRIG_CMPT) return
       ├─ adc16_clear_status_flags(HPM_ADC0, status)
       ├─ if dma.active:
       │    dma = &dma_buff[pmt_trig_ch * 4]
       │    values[i] = dma[i].result
       ├─ else:
       │    values[i] = BUS_RESULT[ch_list[i]]
       └─ pmt.cb(INTF_ADC_CH(0,0), values, 3, user)
            └─ control_isr()   ← 用户代码: 读值、算 PID、改占空比
```

### 8.5 双实例 PMT（Buck-Boost + LCC 独立触发）

当前硬件实测路径使用 ADC0/ADC1 独立 PMT 触发组和独立 DMA buffer：

```
PWM0 CH8REF ──TRGM──> ADCX_PTRGI0A ──> ADC0 PMT trig_ch=0
                                      ├─ DMA slot: pmt_dma0[0..3]
                                      └─ ch6/ch11/ch2/ch3

PWM1 CH8REF ──TRGM──> ADCX_PTRGI1A ──> ADC1 PMT trig_ch=3
                                      ├─ DMA slot: pmt_dma1[12..13]
                                      └─ ch4/ch5
```

```c
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(4) static uint32_t pmt_dma0[48];
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(4) static uint32_t pmt_dma1[48];

static void buck_boost_cb(intf_adc_ch_t trig_ch, const uint16_t *values, uint8_t count, void *user)
{
    (void)trig_ch; (void)user; (void)count;
    /* values: V_IN(ch6), I_IN(ch11), I_L(ch2), V_LINK(ch3) */
}

static void lcc_cb(intf_adc_ch_t trig_ch, const uint16_t *values, uint8_t count, void *user)
{
    (void)trig_ch; (void)user; (void)count;
    /* values: I_COIL(ch4), I_LF(ch5) */
}

void app_adc_dual_pmt_init(void)
{
    hpm_adc_driver_register();

    intf_trgm_connect(INTF_TRGM_SRC_PWM0_CH8REF, INTF_TRGM_DST_ADC_PTRGI0A);
    intf_trgm_connect(INTF_TRGM_SRC_PWM1_CH8REF, INTF_TRGM_DST_ADC_PTRGI1A);

    intf_adc_cfg_t cfg0 = {
        .resolution  = INTF_ADC_RES_DEFAULT,
        .mode        = INTF_ADC_MODE_PMT,
        .dma_en      = true,
        .dma_buff    = pmt_dma0,
        .dma_buff_len = 48,
        .pmt_trig_ch = 0,
        .pmt_ch_count = 4,
        .pmt_ch_list = {6, 11, 2, 3},
        .pmt_cb      = buck_boost_cb,
    };
    intf_adc_init(INTF_ADC_CH(0, 0), &cfg0);

    intf_adc_cfg_t cfg1 = {
        .resolution  = INTF_ADC_RES_DEFAULT,
        .mode        = INTF_ADC_MODE_PMT,
        .dma_en      = true,
        .dma_buff    = pmt_dma1,
        .dma_buff_len = 48,
        .pmt_trig_ch = 3,
        .pmt_ch_count = 2,
        .pmt_ch_list = {4, 5},
        .pmt_cb      = lcc_cb,
    };
    intf_adc_init(INTF_ADC_CH(1, 0), &cfg1);
}
```

### 8.6 VREF 运行时校准

```c
/* 场景：PB10 外接 1.200V 精密基准源 */
void app_adc_calibrate_vref(void)
{
    uint16_t raw;
    intf_adc_read(INTF_ADC_CH(0, 2), &raw);   // PB10 → ch2

    /* 反算真实 VREF: VREF = V_known × 65535 / raw */
    float vref = 1200.0f * 65535.0f / (float)raw;

    /* 更新全局 VREF (所有实例同步) */
    intf_adc_set_vref(INTF_ADC_CH(0, 0), vref);
}
```

### 8.7 运行时重校准

ADC 初次初始化时自动校准。运行中若温度漂移等因素导致精度下降，可随时重校准：

```c
void app_adc_recalibrate(void)
{
    /* 重新校准所有已初始化的 ADC 实例 (ADC0 + ADC1) */
    intf_adc_calibrate(INTF_ADC_CH(0, 0));
}
```

### 8.8 满量程换算参考

本项目统一使用 16-bit 分辨率，VREF 默认 3300mV。ADC Driver 只负责返回 raw 或 ADC 引脚电压，以下前端采样电路换算供 App/控制算法层使用。

```text
Vadc = raw × vref_mv / 65535 / 1000    // 单位: V
```

| 前端类型 | 用途通道 | 参数 | 物理量换算公式 | 3.3V 理想满量程 |
|:---|:---|:---|:---|:---|
| ADC 引脚电压 | 全部通道 | `Vref=3.3V` | `Vadc = raw × 3.3 / 65535` | 3.3 V |
| 电流采样 | `I_IN` / `I_L` | `Rsense=2mΩ`, `INA240A2 Gain=50` | `I = Vadc / (0.002 × 50) = Vadc / 0.1` | 33.0 A |
| 电压采样 | `V_IN` / `V_LINK` | `Rtop=100kΩ`, `Rbot=3.3kΩ`, `1%` | `Vin = Vadc × (100k + 3.3k) / 3.3k = Vadc × 31.303` | ≈103.3 V |
| 互感器采样 | `I_COIL` / `I_LF` | `CST2-100L`, `Rburden=5.1Ω` | `Ipri = Vadc / (5.1 / 100) = Vadc / 0.051` | ≈64.7 A |

> 若 INA240A2 或互感器后级存在中点偏置，控制算法应先做零点扣除：`Vsignal = Vadc - Vbias`，再代入电流公式。

### 8.9 通道速查表

| 物理引脚 | 当前代码实例 | ADC 通道 | App 枚举 | 采样前端 | 典型用途 |
|:---|:---|:---|:---|:---|:---|
| PB14 | ADC0 | 6 | `ADC_CH_V_IN` | 100k + 3.3k 分压 | Buck-Boost 输入电压 |
| PB08 | ADC0 | 11 | `ADC_CH_I_IN` | 2mΩ + INA240A2 | Buck-Boost 输入母线电流 |
| PB10 | ADC0 | 2 | `ADC_CH_I_L` | 2mΩ + INA240A2 | 电感电流 (电流内环) |
| PB11 | ADC0 | 3 | `ADC_CH_V_LINK` | 100k + 3.3k 分压 | V_LINK (Buck-Boost输出 / LCC全桥输入) |
| PB12 | ADC1 | 4 | `ADC_CH_I_COIL` | CST2-100L + 5.1Ω burden | 线圈电流 |
| PB13 | ADC1 | 5 | `ADC_CH_I_LF` | CST2-100L + 5.1Ω burden | LCC 谐振电流 |

### 8.10 Sequence 多通道扫描 + DMA

硬件/软件触发 → 通道队列自动采集 → DMA 写入用户缓冲区 → 中断回调通知。

```c
#include "hpm_adc16_drv.h"   /* adc16_seq_dma_data_t */

/* DMA 缓冲区：要求 non-cacheable + 4 字节对齐 */
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(4) static uint32_t seq_dma_buf[128];

static void seq_done(intf_adc_ch_t trig_ch, void *user)
{
    (void)trig_ch; (void)user;

    /* 从 DMA 缓冲区解析各通道结果 */
    adc16_seq_dma_data_t *dma = (adc16_seq_dma_data_t *)seq_dma_buf;
    for (int i = 0; i < 6; i++) {
        float mv = (float)dma[i].result * 3300.0f / 65535.0f;
        /* dma[i].adc_ch  = 通道号, dma[i].seq_num = 序列号 */
    }
}

void app_adc_init_seq(void)
{
    hpm_adc_driver_register();

    intf_adc_cfg_t cfg = {
        .resolution      = INTF_ADC_RES_DEFAULT,
        .mode            = INTF_ADC_MODE_SEQ,
        .seq_hw_trig     = true,                   /* TRGM 硬件触发 */
        .seq_ch_count    = 6,
        .seq_ch_list     = {11, 2, 3, 4, 5, 6},    /* 全部 6 通道 */
        .seq_dma_buff    = seq_dma_buf,
        .seq_dma_buff_len = 128,
        .seq_cb          = seq_done,
    };
    intf_adc_init(INTF_ADC_CH(0, 0), &cfg);
    intf_adc_start(INTF_ADC_CH(0, 0));             /* 使能 HW trigger */
}

/* SW 触发版本: cfg.seq_hw_trig = false, start() 调用 adc16_trigger_seq_by_sw() */
```

### 8.11 Watchdog 过流/过压保护

通道值超出阈值区间时自动产生中断回调。回调中 ISR 自动屏蔽该通道中断（防 flooding），需手动 re-enable。

```c
static void overcurrent_isr(intf_adc_ch_t ch, uint16_t value, void *user)
{
    (void)user;
    /* 紧急保护：立即关闭 PWM 输出 */
    extern void pwm_force_low(void);
    pwm_force_low();
    /* 重新使能 watchdog（在保护恢复后调用） */
    intf_adc_wdog_reenable(ch);
}

void app_adc_init_wdog(void)
{
    hpm_adc_driver_register();

    intf_adc_cfg_t cfg = {
        .resolution      = INTF_ADC_RES_DEFAULT,
        .mode            = INTF_ADC_MODE_ONESHOT,
        .wdog_en         = true,
        .wdog_thshd_high = 60000,     /* ~3.0V, 超过则触发 */
        .wdog_thshd_low  = 5000,      /* ~0.25V, 低于则触发 */
        .wdog_cb         = overcurrent_isr,
    };
    intf_adc_init(INTF_ADC_CH(0, 11), &cfg);   /* PB08 电感电流通道 */
    /* WDOG 对 Oneshot/Period 模式均可用，PMT/SEQ 模式不支持 */
}
```
