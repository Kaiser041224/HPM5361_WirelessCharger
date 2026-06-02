# ADC 驱动设计说明

本文描述当前工程中 ADC16 驱动的设计边界、接口契约、硬件映射和开发指南。

> **当前实现状态**：驱动已完成。支持双 ADC 实例（ADC0/ADC1）、四种模式（Oneshot / Period / PMT / Sequence+DMA）、Watchdog 阈值告警、自动偏移校准（init 自动触发 + 运行时重校准）、VREF 动态配置。温度传感器作为独立外设，暂未集成。

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
| PMT DMA 缓冲区 | `ADC_SOC_PMT_MAX_DMA_BUFF_LEN` | 48 个 uint32_t |
| 完成事件 | `TRIG_CMPT` / `SEQ_CVC` / `SEQ_CMPT` | 触发组 / 单次 / 队列全部完成 |
| 中断向量 | `IRQn_ADC0` (58) / `IRQn_ADC1` (59) | ADC0/ADC1 各有独立 ISR |

### 2.4 分辨率选项

| 分辨率 | 枚举 | 转换时钟周期 | 最大原始值 |
|--------|------|-------------|-----------|
| 8-bit | `INTF_ADC_RES_8_BITS` | 9 | 255 |
| 10-bit | `INTF_ADC_RES_10_BITS` | 11 | 1023 |
| 12-bit | `INTF_ADC_RES_12_BITS` | 14 | 4095 |
| 16-bit | `INTF_ADC_RES_16_BITS` | 21 | 65535 |

### 2.5 时钟

- 时钟源：`clock_adc0` / `clock_adc1`（AHB 总线时钟，典型 120 MHz）
- 时钟分频：可配置 1–16，驱动自动遵守 **≤ 50 MHz** 手册限制（120 MHz 下最小分频 ≥ 3）
- `sample_cycle`：可配置 1–2³²（0 = 默认 20），影响每次转换的采样时长
- 单次转换耗时 ≈ (sample_cycles + conv_cycles) / ADC 时钟

| 配置 | ADC 时钟 | 16-bit 耗时 | 说明 |
|:---|:---|:---|:---|
| `clock_div=4`（默认） | 30 MHz | ~1.37 µs | 通用 |
| `clock_div=3`（最快） | 40 MHz | ~1.03 µs | 控制环极速 |
| `clock_div=8, sample_cycle=40` | 15 MHz | ~4.07 µs | 高精度/低噪声 |

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
| PB11 | 3 | ADC0 / ADC1 | 模拟输入 |
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

每次调用触发一次硬件转换，阻塞等待结果返回。
不需要显式 `start()` / `stop()`——这些调用在 Oneshot 模式下为无操作。

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
6. 使能 `adc16_event_trig_complete` 中断并注册 ISR（`intc_m_enable_irq_with_priority`）。

**ADC0/ADC1 各有独立 ISR**：

```c
SDK_DECLARE_EXT_ISR_M(IRQn_ADC0, isr_adc0)
void isr_adc0(void)
{
    adc_pmt_isr(0);  // 读 TRIG_CMPT，采集各通道 BUS_RESULT，调 pmt_cb
}

SDK_DECLARE_EXT_ISR_M(IRQn_ADC1, isr_adc1)
void isr_adc1(void)
{
    adc_pmt_isr(1);
}
```

`adc_pmt_isr()` 内部逻辑：
1. 读 `adc16_get_status_flags()` 检查 `TRIG_CMPT`。
2. 清除中断标志。
3. 遍历 `pmt_ch_list`，逐通道调用 `adc16_get_oneshot_result()` 读取 `BUS_RESULT`。
4. 若回调非空，调用 `pmt_cb(trig_ch, values[], count, user_data)` 将全部通道值一次性传给 App 层。

> **注意**：TRGM 路由（PWM 比较器 → ADC 触发输入）由 App 层通过 `trgm_output_config()` 等 API 配置，不属于 ADC 驱动的职责范围。

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
  -> intf_adc_init(INTF_ADC_CH(0, 0), &pmt_cfg)     // 配置 ADC0 PMT 触发组
                                                     //   (内部自动使能 ISR)
  [PWM CMP → TRGM 硬件触发]
  -> isr_adc0() → adc_pmt_isr() → pmt_cb(values)    // 中断回调中执行控制算法
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
- ISR 中断优先级默认 1，可调整。
- WDOG 回调后自动关闭该通道中断，防止 flooding，需手动 `intf_adc_wdog_reenable()` 重新使能。

**当前仍未完成的 ADC 能力**：

- 温度传感器通道（作为独立外设驱动单独实现）。
- 温度传感器通道。

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

读取 (PMT - 中断自动触发)
  [硬件 PWM → TRGM → ADC PMT 触发]
  → ISR: isr_adc0() / isr_adc1()
    → adc_pmt_isr(inst)
      → 遍历 pmt_ch_list → adc16_get_oneshot_result()
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
    intf_adc_init(INTF_ADC_CH(0, 3),  &cfg);  // PB11 → ch3   输出电压
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
硬件链路:  PWM CMP → TRGM → ADC0 PMT TRG0 → 采样 ch11,ch2,ch4 → TRIG_CMPT 中断
软件链路:  isr_adc0() → adc_pmt_isr(0) → pmt.cb(values, count)
```

```c
#include "intf_adc.h"

extern void hpm_adc_driver_register(void);

/* PMT 完成回调 — ISR 上下文中执行，直接运行控制算法 */
static void control_isr(intf_adc_ch_t trig_ch, const uint16_t *values, uint8_t count, void *user)
{
    (void)trig_ch;
    (void)user;

    if (count < 3) return;

    /* values[0] = ch11 (PB08: 输入母线电流)
     * values[1] = ch2  (PB10: 电感电流)
     * values[2] = ch4  (PB12: 线圈电流) */
    float I_in   = (float)values[0] * 3300.0f / 65535.0f;
    float I_L    = (float)values[1] * 3300.0f / 65535.0f;
    float I_coil = (float)values[2] * 3300.0f / 65535.0f;

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
        .pmt_ch_count    = 3,
        .pmt_ch_list     = {11, 2, 4},     /* 一次触发采集 3 个通道 */
        .pmt_cb          = control_isr,
        .pmt_cb_user_data = NULL,
    };

    /* ch 参数仅用于实例选择 (inst=0 → ADC0)，通道部分忽略 */
    intf_adc_init(INTF_ADC_CH(0, 0), &cfg);

    /* 需 App 层额外配置:
     *   trgm_output_config(TRGM0, PWM_CMP_REF, TRGM_OUT_ADC0_PMT_TRG)
     *   PWM 比较器在期望采样时刻产生触发 */
}
```

ISR 内部执行流程：

```
isr_adc0()
  └─ adc_pmt_isr(0)
       ├─ status = adc16_get_status_flags(HPM_ADC0)
       ├─ if (!TRIG_CMPT) return
       ├─ adc16_clear_status_flags(HPM_ADC0, status)
       ├─ for i in 0..2:
       │    adc16_get_oneshot_result(HPM_ADC0, ch_list[i], &values[i])
       └─ pmt.cb(INTF_ADC_CH(0,0), values, 3, user)
            └─ control_isr()   ← 用户代码: 读值、算 PID、改占空比
```

### 8.5 双实例 PMT（内环电流 + 外环电压）

ADC0 做高速电流环（每 PWM 周期），ADC1 做低速电压环（降频）。

```
          PWM0 CMP_A                PWM0 CMP_B
             │                          │
        TRGM → ADC0 PMT            TRGM → ADC1 PMT
             │                          │
      采样 ch11 (16-bit)          采样 ch6 (16-bit)
             │                          │
       IRQn_ADC0 (每周期)          IRQn_ADC1 (每周期, 可软件降频)
             │                          │
      current_isr()               voltage_isr()
      执行 PID → 更新 duty         执行 PID → 更新电流 setpoint
```

```c
/* ADC0 ISR: 电流环 — 每 PWM 周期触发一次，直接更新占空比 */
static void current_isr(intf_adc_ch_t trig_ch, const uint16_t *values, uint8_t count, void *user)
{
    (void)trig_ch; (void)user; (void)count;
    float I_L = (float)values[0] * 3300.0f / 65535.0f;    /* PB10 → ch2 */
    /* 电流环 PID → hrpwm_set_duty() */
}

/* ADC1 ISR: 电压环 — 每 10 个 PWM 周期触发一次 */
static uint8_t v_cycle = 0;
static void voltage_isr(intf_adc_ch_t trig_ch, const uint16_t *values, uint8_t count, void *user)
{
    (void)trig_ch; (void)user; (void)count;
    if (++v_cycle < 10) return;
    v_cycle = 0;
    float V_out = (float)values[0] * 3300.0f / 65535.0f;  /* PB11 → ch3 */
    /* 电压环 PID → 更新电流环 setpoint */
}

void app_adc_dual_pmt_init(void)
{
    hpm_adc_driver_register();

    /* ADC0: 16-bit PMT — 电感电流 (PB10 → ch2) */
    intf_adc_cfg_t cfg_i = {
        .resolution  = INTF_ADC_RES_DEFAULT,
        .mode        = INTF_ADC_MODE_PMT,
        .pmt_trig_ch = 0,
        .pmt_ch_count = 1,  .pmt_ch_list = {2},
        .pmt_cb      = current_isr,
    };
    intf_adc_init(INTF_ADC_CH(0, 0), &cfg_i);

    /* ADC1: 16-bit PMT — 输出电压 (PB11 → ch3) */
    intf_adc_cfg_t cfg_v = {
        .resolution  = INTF_ADC_RES_DEFAULT,
        .mode        = INTF_ADC_MODE_PMT,
        .pmt_trig_ch = 0,
        .pmt_ch_count = 1,  .pmt_ch_list = {3},
        .pmt_cb      = voltage_isr,
    };
    intf_adc_init(INTF_ADC_CH(1, 0), &cfg_v);
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

本项目统一使用 16-bit 分辨率，VREF 默认 3300mV。

| 物理量 | 原始值范围 | 换算公式 | 满量程 |
|:---|:---|:---|:---|
| 电压 (mV) | 0–65535 | `mv = raw × vref_mv / 65535` | 3300 mV |
| 电流 (INA240A2, Gain=50, Rs=5mΩ) | 0–65535 | `I = (raw × vref / 65535) / (50 × 0.005)` | 13.2 A |
| 电流 (INA240A2, Gain=20, Rs=10mΩ) | 0–65535 | `I = (raw × vref / 65535) / (20 × 0.010)` | 16.5 A |
| 电压 (电阻分压 1:10) | 0–65535 | `V = (raw × vref / 65535) × 10` | 33.0 V |

### 8.9 通道速查表

| 物理引脚 | ADC 通道 | 接口宏 | 典型用途 |
|:---|:---|:---|:---|
| PB08 | 11 | `INTF_ADC_CH(0,11)` / `INTF_ADC_CH(1,11)` | 输入母线电流 |
| PB10 | 2 | `INTF_ADC_CH(0,2)` / `INTF_ADC_CH(1,2)` | 电感电流 (电流内环) |
| PB11 | 3 | `INTF_ADC_CH(0,3)` / `INTF_ADC_CH(1,3)` | 输出电压 (电压外环) |
| PB12 | 4 | `INTF_ADC_CH(0,4)` / `INTF_ADC_CH(1,4)` | 线圈电流 |
| PB13 | 5 | `INTF_ADC_CH(0,5)` / `INTF_ADC_CH(1,5)` | LCC 谐振电流 |
| PB14 | 6 | `INTF_ADC_CH(0,6)` / `INTF_ADC_CH(1,6)` | 输入电压 |

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
