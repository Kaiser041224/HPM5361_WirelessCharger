# 项目 ADC/PWM/TRGM/PMT 调查报告

> 生成日期：2026-06-09
> 目的：完整梳理 ADC16 PMT 抢占转换、PWM/TRGM 触发、DMA 数据读取，以及 PMT slot0 首次转换异常的真实实现细节
> 约束：仅代码阅读/检索/分析，未修改任何项目文件

---

## A. 项目结构摘要

### A.1 分层结构

| 层级 | 主要职责 | 关键路径 |
|:---|:---|:---|
| **App/Application/** | 系统启动编排、状态机、通信组织 | `App/Application/Src/app_entry.c`, `app_control.c`, `app_comm.c` |
| **App/Control/** | 闭环控制算法占位 (Buck-Boost/LCC) | `App/Control/Src/ctrl_buckboost.c`, `ctrl_lcc.c`, `ctrl_fault.c` |
| **App/Algorithm/** | 纯数学库 (PID/PLL/RMS/滤波) | `App/Algorithm/Src/algo_pid.c`, `algo_pll.c`, `algo_filter.c` |
| **App/Platform/** | 硬件能力封装 (ADC/PWM/CAN/GPIO) | `App/Platform/Src/app_adc.c`, `app_hrpwm.c`, `app_analog_signal.c` |
| **App/Debug/** | 调试辅助 (RTT/ADC调试) | `App/Debug/Src/app_debug_adc.c`, `app_debug_rtt.c` |
| **Interface/** | 契约定义层 (C17 匿名结构体) | `Interface/intf_adc.h`, `intf_trgm.h`, `intf_hrpwm.h` |
| **Driver/hpm_impl/** | HPM SDK 适配实现 | `Driver/hpm_impl/drv_adc.c`, `drv_trgm.c`, `drv_hrpwm.c` |
| **Board/** | 引脚复用、板级配置 | `Board/HPM5361_WirelessCharger_board/board.c`, `pinmux.c` |

### A.2 系统入口

```
App/main.c:5  →  main()
    └── App/Application/Src/app_entry.c:17  →  app_init()
        ├── board_init()                        // 引脚时钟初始化
        ├── intf_clock_init()                   // 系统时钟
        ├── hrpwm_init()                        // PWM 配置 (不启动)
        ├── app_adc_init()                      // ADC 校准+PMT+TRGM (PWM未启动)
        │   ├── hpm_adc_driver_register()
        │   ├── hpm_trgm_driver_register()
        │   ├── intf_adc_init() ×2              // ADC0/ADC1 PMT 配置
        │   ├── intf_trgm_connect() ×2          // PWM→TRGM→ADC 路由
        │   └── intf_hrpwm_config_trigger_cmp() ×2  // CMP8 配置
        ├── hrpwm_start_all()                   // PWM 启动 (CMP8开始触发)
        ├── app_analog_signal_init()
        └── app_control_init()
            └── app_run_once()  → app_control_tick() + app_comm_tick()
```

### A.3 当前控制环状态 (重要)

**当前控制环是完全的占位实现 (stub)**：
- `ctrl_buckboost.c` 所有函数体均为 `(void)param;` 空操作
- `ctrl_lcc.c` 所有函数体均为空操作
- `ctrl_fault.c` 所有函数返回 0 / false
- `app_control.c:48-56` 的采样值全部为 `0.0f` 占位

这意味着 **当前 slot0 异常不会对控制产生实际影响**，但必须认识到一旦控制环实装就会产生影响。

---

## B. ADC0/ADC1 PMT 配置表

### B.1 全局参数

| 参数 | 值 | 来源 |
|:---|:---|:---|
| 分辨率 | 16-bit | `INTF_ADC_RES_16_BITS` → `Interface/intf_adc.h:49` |
| Clock div | 3 (120/3 = 40 MHz) | `INTF_ADC_DEFAULT_CLOCK_DIV` → `Interface/intf_adc.h:53` |
| Sample cycle | 21 | `INTF_ADC_DEFAULT_SAMPLE_CYCLE` → `Interface/intf_adc.h:52` |
| Conversion cycles | 25 (固定) | `ADC_CONV_CYCLES` → `Driver/hpm_impl/drv_adc.c:32` |
| VREF | 3300 mV | `INTF_ADC_DEFAULT_VREF_MV` → `Interface/intf_adc.h:54` |
| DMA buffer 长度 | 48 uint32_t | `APP_ADC_PMT_DMA_BUFF_LEN` → `App/Platform/Inc/app_adc.h:51` |
| PMT队列长度 | 4 (1 dummy + 3 real) | `APP_ADC_PMT_ADC0/1_CH_COUNT` → `app_adc.h:49-50` |
| CMP8 索引 | 8 | `APP_ADC_PMT_TRIGGER_CMP_INDEX` → `app_adc.h:45` |
| CMP8 位置比例 | 0.5 (50%, 中心) | `APP_ADC_PMT_POSITION_RATIO` → `app_adc.h:46` |
| Startup discard | 8 帧 | `ADC_PMT_STARTUP_DISCARD` → `drv_adc.c:39` |

### B.2 ADC0 PMT 配置明细

| ADC实例 | trig_ch | PMT Slot | ADC Channel | 逻辑信号 | 引脚 | Dummy? | 配置位置 |
|:---|:---|:---|:---|:---|:---|:---|:---|
| ADC0 | 0 (PTRGI0A) | 0 | **15** | (dummy) | — | ✅ YES | `app_adc.c:164` |
| ADC0 | 0 | 1 | 3 | V_LINK | PB11 | ❌ | `app_adc.c:165` |
| ADC0 | 0 | 2 | 2 | I_L | PB10 | ❌ | `app_adc.c:166` |
| ADC0 | 0 | 3 | 11 | I_IN | PB08 | ❌ | `app_adc.c:167` |

DMA buffer: `pmt_dma0[48]` at `app_adc.c:84`, DMA offset = trig_ch×4 = **0**, 数据在 `pmt_dma0[0..3]`

### B.3 ADC1 PMT 配置明细

| ADC实例 | trig_ch | PMT Slot | ADC Channel | 逻辑信号 | 引脚 | Dummy? | 配置位置 |
|:---|:---|:---|:---|:---|:---|:---|:---|
| ADC1 | 3 (PTRGI1A) | 0 | **15** | (dummy) | — | ✅ YES | `app_adc.c:188` |
| ADC1 | 3 | 1 | 6 | V_IN | PB14 | ❌ | `app_adc.c:189` |
| ADC1 | 3 | 2 | 4 | I_COIL | PB12 | ❌ | `app_adc.c:190` |
| ADC1 | 3 | 3 | 5 | I_LF | PB13 | ❌ | `app_adc.c:191` |

DMA buffer: `pmt_dma1[48]` at `app_adc.c:85`, DMA offset = trig_ch×4 = **12**, 数据在 `pmt_dma1[12..15]`

### B.4 ADC 初始化、Calibration 与 PMT enable 关键代码路径

```
app_adc_init()                                    // app_adc.c:136
  └── intf_adc_init(INTF_ADC_CH(0,0), &cfg)      // app_adc.c:168
      └── adc_init()                              // drv_adc.c:349
          ├── adc_init_clock()                    // drv_adc.c:119
          ├── adc16_init(&adc_cfg)                // drv_adc.c:387  (包含自校准)
          │   └── (HPM SDK 内部: adc16_self_calibration)
          ├── base->ANA_CTRL0 |= ADC_CLK_ON       // drv_adc.c:390
          ├── adc16_init_channel() ×4             // drv_adc.c:430
          ├── adc16_set_pmt_config()              // drv_adc.c:443
          ├── adc16_enable_pmt_queue(trig_ch=0)   // drv_adc.c:445
          ├── for t=0..10: disable 其他 trig_ch   // drv_adc.c:451-454  (隔离)
          ├── adc16_enable_interrupts(TRIG_CMPT)  // drv_adc.c:457
          └── adc16_init_pmt_dma()                // drv_adc.c:467

ADC Calibration 时机:
  - adc16_init() 内部执行自校准 (SDK 行为)
  - 校准发生在 app_adc_init() 内，此时 PWM 未启动 (噪声最小)
  - 还有一个独立的 adc_calibrate() 函数 (drv_adc.c:687)，但仅被注册到 ops 表，无显式调用
```

---

## C. PWM → TRGM → ADC 触发链路

### C.1 PWM 配置

| Pair | PWM实例 | 频率 | 对齐模式 | CMP8 位置 | 引脚 |
|:---|:---|:---|:---|:---|:---|
| PAIR_0 | PWM0 | **200 kHz** | Center-aligned | reload × 0.5 | PA24/PA25 |
| PAIR_1 | PWM0 | **200 kHz** | Center-aligned | reload × 0.5 | PA26/PA27 |
| PAIR_2 | PWM1 | **148 kHz** | Center-aligned | reload × 0.5 | PA28/PA29 |
| PAIR_3 | PWM1 | **148 kHz** | Center-aligned | reload × 0.5 | PA30/PA31 |

配置来源: `App/Platform/Src/app_hrpwm.c:30-63`

### C.2 CMP8 配置细节

```
hrpwm_config_trigger_cmp_impl()                   // drv_hrpwm.c:1119
  └── reload = hrpwm_get_full_reload(inst)        // drv_hrpwm.c:1126
  └── cmp_val = reload × 0.5                       // drv_hrpwm.c:1127
  └── pwm_config_cmp(base, 8, &cmp_cfg)            // drv_hrpwm.c:1135
      ├── mode = pwm_cmp_mode_output_compare       // drv_hrpwm.c:1132
      ├── update_trigger = pwm_shadow_register_update_on_shlk  // drv_hrpwm.c:1133
      └── cmp = reload × 0.5
  └── pwm_config_output_channel(base, 8, &out)    // drv_hrpwm.c:1141
      ├── cmp_start_index = 8, cmp_end_index = 8
      └── invert_output = false
  └── pwm_issue_shadow_register_lock_event()      // drv_hrpwm.c:1143
```

CMP8 在中心对齐模式下的行为：
- 计数器从 0→reload 时，在 reload×0.5 处匹配 (上升沿)
- 计数器从 reload→0 时，在 reload×0.5 处再次匹配 (下降沿)
- CH8REF 输出在每个匹配点切换电平
- **结论：每个 PWM 周期产生 2 次匹配，CH8REF 产生上升沿 1 次 (可用于触发)**

### C.3 TRGM 路由

```
drv_trgm.c:36-41:
  cfg.type = trgm_output_same_as_input;  // ← 电平透传 (非 edge-to-pulse)
  cfg.invert = false;
  trgm_output_config(HPM_TRGM0, dst_map[dst], &cfg);
```

### C.4 完整触发链路图

```
【ADC0 链路】
  PWM0 CMP8 (at reload×0.5)
    → PWM0_CH8REF (HPM_TRGM0_INPUT_SRC_PWM0_CH8REF)    [drv_trgm.c:14]
    → TRGM0 output ADCX_PTRGI0A                         [drv_trgm.c:25]
    → ADC0 PMT trig_ch=0                                 [app_adc.h:47]
    → PMT queue: [ch15(dummy), ch3(V_LINK), ch2(I_L), ch11(I_IN)]
    → DMA buffer: pmt_dma0[0..3]                        [app_adc.c:84]
    → ISR isr_adc0()                                     [drv_adc.c:321]
      → adc_generic_isr(0)                               [drv_adc.c:322]
        → DMA快照→验证→回调 app_adc_pmt_cb_adc0()        [drv_adc.c:262-283]
          → 写入 pmt_raw_cache[V_LINK/I_L/I_IN]          [app_adc.c:105]

【ADC1 链路】
  PWM1 CMP8 (at reload×0.5)
    → PWM1_CH8REF (HPM_TRGM0_INPUT_SRC_PWM1_CH8REF)    [drv_trgm.c:18]
    → TRGM0 output ADCX_PTRGI1A                         [drv_trgm.c:28]
    → ADC1 PMT trig_ch=3                                 [app_adc.h:48]
    → PMT queue: [ch15(dummy), ch6(V_IN), ch4(I_COIL), ch5(I_LF)]
    → DMA buffer: pmt_dma1[12..15]                      [app_adc.c:85]
    → ISR isr_adc0() 或 isr_adc1()                      [drv_adc.c:321/329]
      → adc_generic_isr(1)                               [drv_adc.c:325/330]
        → DMA快照→验证→回调 app_adc_pmt_cb_adc1()        [drv_adc.c:262-283]
          → 写入 pmt_raw_cache[V_IN/I_COIL/I_LF]         [app_adc.c:124]
```

### C.5 TRGM 输出模式分析

| 项目 | 当前值 | 可选替代 |
|:---|:---|:---|
| TRGM 输出类型 | `trgm_output_same_as_input` (电平透传) | `trgm_output_pulse` (边沿转脉冲) |
| 代码位置 | `drv_trgm.c:38` | SDK 类型: `trgm_output_pulse` |
| 修改点 | 改 `drv_trgm.c:38` `cfg.type` | 1行改动 |

当前使用电平透传模式：CH8REF 高电平期间 PTRGIxA 持续为高，PMT 仅在下一次 rising edge 触发（符合需求）。但如果 CH8REF 在下降沿后立即变高（因 CMP8 在 down-count 时匹配），可能产生 spurious trigger。当前 CH8REF 行为：up-count 匹配→高，down-count 匹配→低，所以每个 PWM 周期仅 1 次上升沿触发。

---

## D. DMA buffer 与 ISR 读取路径

### D.1 DMA Buffer 定义

```c
// app_adc.c:84-85
static uint32_t pmt_dma0[48] __attribute__((section(".noncacheable"), aligned(4)));
static uint32_t pmt_dma1[48] __attribute__((section(".noncacheable"), aligned(4)));
```

DMA buffer 布局:
```
offset = trig_ch × ADC_PMT_DMA_SLOT_LEN (4)
  → ADC0: trig_ch=0 → offset=0  → dma0[0..3]
  → ADC1: trig_ch=3 → offset=12 → dma1[12..15]
```

### D.2 `adc16_pmt_dma_data_t` 结构 (SDK定义)

| 位域 | 说明 |
|:---|:---|
| `result[15:0]` | ADC 转换结果 |
| `adc_ch[24:20]` | ADC 物理通道号 |
| `trig_ch[28:25]` | 触发通道号 |
| `cycle_bit[31]` | 周期位 (1=新数据) |

### D.3 ISR 数据读取流程

```c
// drv_adc.c:215-285
adc_generic_isr(inst):
  1. 读 + 清除中断状态 (TRIG_CMPT)             // line 221-222
  2. frame_cnt++                                 // line 226
  3. if frame_cnt < ADC_PMT_STARTUP_DISCARD(8)  // line 227
       → return;  // 丢弃前8次触发
  4. dma_offset = trig_ch × 4                   // line 236
  5. 关全局中断                                   // line 239
  6. snap[0..3] = dma_hw[0..3]  (快照!)          // line 241-245
  7. 开全局中断                                   // line 247
  8. 验证循环 for i=0..3:                       // line 262
     ✅ 先验证 cycle_bit == 0? → continue       // line 263
     ✅ 先验证 trig_ch ≠ pmt.trig_ch? → continue // line 265
     ✅ 先验证 adc_ch ≠ ch_list[i]? → continue  // line 267
     ✅ values[valid] = result; valid++          // line 269-270
  9. if valid == ch_count → 调用回调               // line 282-284
```

### D.4 ISR 正确性判断

| 验证项 | 实现 | 判断 |
|:---|:---|:---|
| DMA 快照 (防竞争) | ✅ snap[4] + 关中断快照 | ✅ 正确 |
| 先验证再写入 | ✅ if continue → 跳过脏数据 | ✅ 正确 |
| ISR 中 memset | ❌ **没有** memset | ✅ 正确 (不写DMA buffer) |
| cycle_bit 验证 | ✅ line 263 | ✅ 正确 |
| trig_ch 验证 | ✅ line 265 | ✅ 正确 |
| adc_ch 验证 | ✅ line 267 | ✅ 正确 |
| valid==ch_count gating | ⚠️ line 282 | ⚠️ **见下文分析** |
| DMA buffer 对齐 | ✅ aligned(4) + noncacheable | ✅ 正确 |

### D.5 `valid == ch_count` 闸门分析 ⚠️

当前 `ch_count = 4` (包含dummy slot0)。ISR 期望所有 4 个 slot 都通过验证后才调用回调。

**风险场景：** 如果 ch15 的 cycle_bit 为 0（或 trig_ch/adc_ch 不匹配），则 valid = 3 < ch_count = 4，**回调不会被调用，所有 3 个真实通道数据全部丢失**。

**实际上：** 目前 ch15 会产生有效 conversion (只是值错误)，cycle_bit/adc_ch/trig_ch 均匹配，所以 valid=4，闸门通过。但这是脆弱的——依赖一个 dummy channel 产生 "有效" 的 DMA 元数据。

### D.6 回调中的 slot0 跳过

```c
// app_adc.c:102 (ADC0 回调)
for (uint8_t i = 1; i < count && i < 4U; i++) {    // 从1开始，跳过0
    uint8_t hw_ch = (i == 1) ? 3U : (i == 2) ? 2U : 11U;
    pmt_raw_cache[hw_to_logic[hw_ch]] = values[i];  // values[0] = dummy 被忽略
}
```

回调中硬编码了 `i → hw_ch` 的映射，与 `pmt_ch_list` 顺序一致。

---

## E. PMT slot0 异常相关证据

### E.1 文档记录

- **`doc/ADC_PMT_FIRST_CONVERSION_ANOMALY.md`** (100行，2026-06-09)
  - 明确描述现象、根因、解决方案
  - 确认症状：位置0恒为 ~0x8000，不随输入变化
  - 确认交换通道后异常跟随位置0 (非物理通道)
  - 确认 ADC0/ADC1 均有此现象
  - 确认 `ADC_PMT_STARTUP_DISCARD` 无效 (因为是每触发周期的首个)

- **`doc/control_loop_design.md:186-190`** 同样的记录，引用上述文档

- **`doc/adc_driver_design.md:678`** 记录了完整硬件链路

### E.2 代码实现中的规避措施

| 措施 | 位置 | 状态 |
|:---|:---|:---|
| slot0 = ch15 (dummy) | `app_adc.c:164, 188` | ✅ 已实现 |
| 回调跳过 i=0 | `app_adc.c:102, 121` | ✅ 已实现 |
| `ADC_PMT_STARTUP_DISCARD=8` | `drv_adc.c:227-228` | ⚠️ **已证实无效**，但仍保留 |
| 非dummy trig_ch 禁用 | `drv_adc.c:451-454` | ✅ 隔离正确 |

### E.3 预留的 `ADC_PMT_STARTUP_DISCARD` 分析

```c
// drv_adc.c:38-39, 226-228
#define ADC_PMT_STARTUP_DISCARD (8U)

if (ai->pmt.frame_cnt < ADC_PMT_STARTUP_DISCARD) {
    return;  // 丢弃前8次触发
}
```

**判断：此机制对 slot0 异常无效。** 文档已确认："增加 `ADC_PMT_STARTUP_DISCARD` 丢弃次数无效，因为问题是每个触发周期的首个通道，而非系统启动后的前几次触发"（`ADC_PMT_FIRST_CONVERSION_ANOMALY.md:100`）。但仍保留在代码中可能对清除其他 startup transient 有益。

---

## F. 当前规避方案是否完整

### F.1 逐项检查

| 检查项 | 状态 | 详细 |
|:---|:---|:---|
| ADC0 是否使用 dummy slot0? | ✅ | ch15 @ `app_adc.c:164` |
| ADC1 是否使用 dummy slot0? | ✅ | ch15 @ `app_adc.c:188` |
| ISR 回调是否跳过 slot0? | ✅ | `for(i=1;...)` @ `app_adc.c:102, 121` |
| 控制环是否可能收到 slot0 脏数据? | ✅ 当前安全 | 控制环为占位空实现 |
| `app_adc_get_pmt_raw()` 是否可能返回 slot0 数据? | ✅ 安全 | 读取的是 `pmt_raw_cache[]`，由回调写入，回调正确跳过 slot0 |
| FAULT_ADC 能否检测异常? | ❌ 不能 | `ctrl_fault.c` 全部为 stub 空函数 |
| `app_debug_adc_dump_pmt()` 会暴露 slot0 吗? | ✅ 不会 | 读取 `pmt_raw_cache[]`，回调已过滤 |

### F.2 关键发现

**当前规避方案在代码层面已经正确实现：**
1. PMT 队列位置0配置为 ch15 (dummy)
2. 回调从 i=1 开始处理，跳过 values[0]
3. 数据正确写入 `pmt_raw_cache[]`

**但存在两个值得关注的问题：**

#### 问题 1：ISR 闸门 `valid == ch_count` 的脆弱性

```c
// drv_adc.c:282
if (valid == ai->pmt.ch_count) {  // valid必须是4，否则回调完全不触发
```

如果 ch15 因任何原因导致验证失败，所有3个真实通道的数据都会丢失。`ch_count` 包含 dummy channel，这是冗余的验证失败点。

**风险等级：低** — 目前 ch15 通过了所有验证。

#### 问题 2：PMT 数据与控制环断连

当前 PMT ISR 回调写入 `pmt_raw_cache[]`，但 `app_analog_signal` 模块使用自己的 `s_raw_cache[]`（通过 `app_analog_signal_update_raw()` 写入）。**没有任何代码将 PMT 数据桥接到 `analog_signal` 模块。**

这意味着当控制环最终实现时，如果直接从 `analog_signal` 读取滤波/物理值，会得到的是从未更新过的缓存数据。必须添加桥接代码。

#### 问题 3：`FAULT_ADC` 枚举定义但未实现

```c
// ctrl_types.h:36
FAULT_ADC = (1 << 9),  /**< ADC 数据异常 */
```

`ctrl_fault_check()` 永远返回 0。不存在任何 ADC 数据有效性检查。如果未来出现任何 ADC 异常（包括 slot0 规避失效、DMA 损坏），故障检测不会响应。

### F.3 总体判断

**✅ 当前实现已经安全规避 slot0 异常，在 PMT 数据路径上是完整的。**

**⚠️ 风险存在于未来集成时：**
- 控制环实装后需确保数据路径正确
- 需添加 `FAULT_ADC` 实际检测逻辑
- 建议降低 `valid == ch_count` 的耦合度

---

## G. 可能根因排序

根据文档和代码分析，slot0 异常的最可能根因排序如下：

| 排序 | 可能根因 | 证据 | 排除方法 |
|:---|:---|:---|:---|
| **1** | **HPM5361 ADC16 PMT 引擎内部缺陷**：首次采样时 SAR 电容未预充/放至正确电压 | ①跟随slot0而非物理通道；②ADC0/ADC1均有；③所有分辨率表现一致；④calibration无效；⑤startup discard无效 | 见实验1/2 |
| 2 | **CMP8 → TRGM → PTRGI 时序问题**：触发边沿到达时 ADC 采样保持电路尚未就绪 | ①始终发生在首个转换位置；②中心对齐模式有对称匹配 | 见实验3/4 |
| 3 | **DMA 与 SAR 竞争**：DMA 在首次转换完成前读取了默认值 | ①结果恒为VREF/2 (约中点值)；②cycle_bit仍为1 | 可能性较低 (cycle_bit=1说明转换确实完成) |
| 4 | **PMT 状态机初始化错误**：首次转换使用了错位的通道配置 | ①交换通道后异常跟随slot0 | 可能性较低 (ch_list配置已正确) |

---

## H. 建议验证实验

### 实验1：同通道重复 (排除通道切换影响)

- **目的：** 确认异常是"每触发的首个slot"而非"通道切换"导致
- **修改文件：** `App/Platform/Src/app_adc.c`
- **修改函数：** `app_adc_init()`
- **临改：**
  ```c
  // ADC0: 所有slot填同一通道
  cfg.pmt_ch_list[0] = 3U;  // 不用dummy
  cfg.pmt_ch_list[1] = 3U;
  cfg.pmt_ch_list[2] = 3U;
  cfg.pmt_ch_list[3] = 3U;
  ```
  同时临时去除回调中的 `i=1` 跳过逻辑
- **预期：** slot0 读数为 ~0x8000，slot1-3 读数正常且一致
- **结论：** 若 slot1-3 全部正确且一致，排除通道切换干扰，确认为首个转换问题

### 实验2：软件触发 vs 硬件触发对比

- **目的：** 排除 TRGM/PWM 触发边沿质量因素
- **修改文件：** `Driver/hpm_impl/drv_adc.c` (或 SDK 层面的 PMT 触发选择)
- **修改函数：** PMT 初始化 (约 line 435-445)
- **临改：** 改用软件触发 (`adc16_trigger_pmt_sw()` 或 SDK 等价 API) 替代 TRGM 输入
- **预期：** 若软件触发 slot0 仍异常 → 排除 TRGM/CH8REF 边沿问题；若正常 → 问题在 TRGM 链路
- **结论：** 区分是 ADC 内部还是触发链路问题

### 实验3：增大 sample_cycle

- **目的：** 确认是否是采样时间不足导致首个转换采样不充分
- **修改文件：** `App/Platform/Src/app_adc.c:153` (两处)
- **修改函数：** `app_adc_init()`
- **临改：**
  ```c
  .sample_cycle = 40U,  // 原 21 → 增大到 40
  ```
- **预期：** 若 slot0 恢复 → 是采样时间不足；若仍异常 → 排除采样时间因素
- **结论：** 确认是否是 S/H 电容充电不充分

### 实验4：改 TRGM 输出为 edge-to-pulse

- **目的：** 确认是否是电平透传产生的边沿质量问题
- **修改文件：** `Driver/hpm_impl/drv_trgm.c:38`
- **修改函数：** `trgm_connect_impl()`
- **临改：**
  ```c
  cfg.type = trgm_output_pulse;  // 原 trgm_output_same_as_input
  ```
- **预期：** 若 slot0 恢复 → 问题在 TRGM 边沿质量；若不变 → 排除
- **结论：** edge-to-pulse 可避免电平抖动导致的多次触发

### 实验5：关闭 dummy 确认异常重现

- **目的：** 证明 dummy 方案确实在规避问题（反向验证）
- **修改文件：** `App/Platform/Src/app_adc.c:164,188`
- **修改函数：** `app_adc_init()`
- **临改：**
  ```c
  cfg.pmt_ch_list[0] = 3U;  // 真实通道回到 slot0
  cfg.pmt_ch_list[1] = 2U;  // 移位
  cfg.pmt_ch_list[2] = 11U;
  cfg.pmt_ch_list[3] = 15U;  // dummy 移到末尾
  ```
  修改回调中的 hw_ch 映射顺序
- **预期：** slot0 (ch3=V_LINK) 读数异常 ~0x8000
- **结论：** 确认异常仍在，验证 dummy 方案必要性

### 实验6：Sequence 模式首个通道对比

- **目的：** 确认是否是 PMT 模式特有，还是所有多通道模式首个都异常
- **修改文件：** `App/Platform/Src/app_adc.c` (或新建测试)
- **修改函数：** 测试代码中改用 `INTF_ADC_MODE_SEQ` + `seq_ch_list`
- **临改：** 配置 ADC0 为 sequence 模式，ch_list = ch3,ch2,ch11
- **预期：** 若 seq 模式首个正常 → 确认是 PMT 模式特有；若也异常 → 可能是更底层问题
- **结论：** 确认问题范围

---

## I. 建议修改点

### I.1 高优先级

1. **降低 `valid == ch_count` 耦合** (`drv_adc.c:282`)
   - 当前：`if (valid == ai->pmt.ch_count)` — 任何 slot 验证失败导致全部丢弃
   - 建议：改为 `if (valid > 0)` 至少传递有效数据，由上层决定是否足够
   - 或者：在 `ai->pmt` 中增加 `ch_count_valid` 表示有效通道数，`valid >= ch_count_valid` 即触发回调

2. **添加 PMT → analog_signal 数据桥接**
   - 在 PMT 回调中调用 `app_analog_signal_update_raw()` 确保数据进入滤波/物理换算链路

3. **实现 `FAULT_ADC` 检测逻辑** (`ctrl_fault.c`)
   - 至少检查 `pmt_raw_cache` 是否为合理的物理值范围
   - 若 slot0 异常值 (~0x8000) 意外出现在真实通道中，应能检测

### I.2 中优先级

4. **考虑使用 edge-to-pulse TRGM 输出** (`drv_trgm.c:38`)
   - 将 `trgm_output_same_as_input` 改为 `trgm_output_pulse`
   - 更稳定的触发边沿，避免电平信号中的毛刺

5. **用 pmt_ch_list 推导回调映射**
   - 当前回调硬编码 `(i==1)?3U:(i==2)?2U:11U`
   - 建议从 `pmt_ch_list[]` 直接读取，减少重复配置风险

### I.3 低优先级

6. **评估移除 `ADC_PMT_STARTUP_DISCARD`**
   - 已确认对 slot0 问题无效
   - 如非用于其他目的可以移除，减少困惑

---

## J. 仍不确定的问题

| 问题 | 无法通过代码审查确定的原因 |
|:---|:---|
| ch15 在 HPM5361 上是否真的存在可转换的模拟通道？ | 需查阅芯片数据手册确认引脚表 |
| `adc16_init()` 执行的自校准是否完整？ | SDK 内部实现，需查阅 SDK 源码 |
| CH8REF 在中心对齐模式下的精确波形（是否会因 CMP8 配置产生毛刺？） | 需示波器实测 |
| 若 TRGM 改为 `trgm_output_pulse` 是否会因两次匹配产生两次 PMT 触发？ | 需示波器验证 CH8REF 实际波形 |
| `adc16_self_calibration` 是否会影响 PMT 首个 slot？ | 文档已说 calibration 无效，但需确认 |
| slot0 异常是否可通过在 CHnREF 和 PTRGI 之间加 TRGM 滤波器解决？ | TRGM filter 配置未在当前代码中使用 |

---

## K. 最终结论

**当前代码已经通过 dummy channel (slot0=ch15) + 回调跳过 i=0 的方式正确规避了 PMT slot0 异常。控制环当前为占位实现，因此不存在实际风险。**

但在控制环实装前，需要解决以下问题：

1. **ISR 闸门耦合**（I.1.1）
2. **PMT → analog_signal 桥接**（I.1.2）
3. **FAULT_ADC 实现**（I.1.3）

---

> **附录：所有引用的关键文件**
>
> - `App/main.c` — 系统入口
> - `App/Application/Src/app_entry.c` — 初始化编排
> - `App/Application/Src/app_control.c` — 控制循环 (占位)
> - `App/Platform/Src/app_adc.c` — ADC PMT 配置 + 回调
> - `App/Platform/Src/app_hrpwm.c` — PWM 频率/通道配置
> - `App/Platform/Src/app_analog_signal.c` — 模拟量换算模块
> - `App/Control/Src/ctrl_buckboost.c` — Buck-Boost 控制 (占位)
> - `App/Control/Src/ctrl_lcc.c` — LCC 控制 (占位)
> - `App/Control/Src/ctrl_fault.c` — 故障检测 (stub)
> - `App/Control/Inc/ctrl_types.h` — 故障码定义 (含 FAULT_ADC)
> - `Driver/hpm_impl/drv_adc.c` — ADC16 驱动实现 (初始化+ISR+DMA)
> - `Driver/hpm_impl/drv_trgm.c` — TRGM 路由实现
> - `Driver/hpm_impl/drv_hrpwm.c` — HRPWM 驱动实现 (含 CMP8)
> - `Interface/intf_adc.h` — ADC 接口契约
> - `Interface/intf_trgm.h` — TRGM 接口契约
> - `Interface/intf_hrpwm.h` — HRPWM 接口契约
> - `Board/HPM5361_WirelessCharger_board/pinmux.c` — 引脚复用
> - `doc/ADC_PMT_FIRST_CONVERSION_ANOMALY.md` — slot0 异常文档
> - `doc/control_loop_design.md` — 控制环设计文档
> - `doc/adc_driver_design.md` — ADC 驱动设计文档
