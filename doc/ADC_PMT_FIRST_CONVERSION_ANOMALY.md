# HPM5361 ADC16 PMT 首次转换结果异常

**发现日期：** 2026-06-09
**芯片型号：** HPM5361
**SDK 版本：** hpm_sdk (HPM5300 系列)
**影响范围：** ADC16 抢占（PMT）模式下，每个触发周期的第一个通道转换结果

---

## 现象

ADC16 工作在 PMT（Preemption）模式，由 PWM 比较事件通过 TRGM 触发。DMA 传输转换结果到内存缓冲区。

**症状：** PMT 通道列表中位置 0 的通道，其 DMA 转换结果始终为 ~0x8000（对应 VREF/2 ≈ 1650mV），不随实际输入电压变化。位置 1-3 的通道读数正常。

**复现条件：**
- ADC16 配置为 PMT 模式，DMA 使能
- PMT 通道列表包含 2-4 个通道
- 无论 ADC0 还是 ADC1 实例，现象一致
- 无论使用哪个物理通道（ch2-ch11），只要它在位置 0，读数即为 ~0x8000

**验证方法：** 交换通道列表中位置 0 和位置 1 的通道号，观察到原位置 0 的通道恢复正常，原位置 1 的通道变为 ~0x8000。确认问题与通道号无关，仅与 DMA 位置有关。

---

## 根因分析

HPM5361 ADC16 的 PMT 转换引擎在每次触发后的首次采样中，内部采样保持电容或 SAR ADC 逻辑处于未初始化/默认状态，导致首次转换输出为中点值（VREF/2）。

此行为在 SDK 官方示例中未被暴露，因为示例通常只使用单通道 PMT 或不关注首个通道的精度。

---

## 解决方案

在 PMT 通道列表的位置 0 放置一个虚拟通道（使用未连接的空闲通道号，如 ch15），真实采样通道从位置 1 开始排列。ISR 回调中跳过 `i=0`，仅处理 `i=1..3`。

**代价：** 每个 ADC 实例的有效 PMT 通道数从 4 降为 3。

### 代码示例

```c
/* PMT 通道列表：位置 0 为 dummy */
cfg.pmt_ch_list[0] = 15U;  /* dummy，结果丢弃 */
cfg.pmt_ch_list[1] = 6U;   /* 真实通道 1 */
cfg.pmt_ch_list[2] = 3U;   /* 真实通道 2 */
cfg.pmt_ch_list[3] = 4U;   /* 真实通道 3 */

/* 回调中跳过 position 0 */
for (uint8_t i = 1; i < count && i < 4U; i++) {
    /* 仅处理位置 1-3 的数据 */
}
```

---

## 调试过程中发现的相关问题

### 1. PTRGIxA 与 trig_ch 的映射关系

TRGM 输出到 ADC 的映射容易混淆：

| TRGM 输出 | trig_ch 值 | 说明 |
|:---|:---|:---|
| PTRGI0A | 0 | 广播到所有 ADC 实例 |
| PTRGI0B | 1 | |
| PTRGI0C | 2 | |
| PTRGI1A | 3 | 广播到所有 ADC 实例 |
| PTRGI1B | 4 | |
| PTRGI1C | 5 | |

`PTRGI1A` 对应 `trig_ch=3`，不是 0。配置 ADC1 使用 PTRGI1A 触发时，`pmt_trig_ch` 必须设为 3。

### 2. DMA 验证逻辑顺序

ISR 中读取 DMA 数据时，必须先验证再写入结果数组，否则脏数据会污染有效值：

```c
/* 错误：先写入再验证 */
values[valid] = dma[i].result;  // 脏数据已写入
if (dma[i].cycle_bit == 0) continue;  // 验证失败但数据已污染

/* 正确：先验证再写入 */
if (dma[i].cycle_bit == 0) continue;  // 验证失败直接跳过
values[valid] = dma[i].result;  // 只有有效数据才写入
valid++;
```

### 3. ISR 中不应 memset DMA 缓冲区

SDK 示例中 `memset(pmt_buff, 0, sizeof(pmt_buff))` 是在触发源停止后执行的。在 ISR 中直接 memset 会与 DMA 引擎产生竞争，导致数据损坏。应改为快照到局部变量后处理。

---

## 备注

- 该现象在 HPM5301 EVK 开发板上同样可复现
- 16 位分辨率和 12 位分辨率下表现一致
- ADC 校准（calibration）不影响此问题
- 增加 `ADC_PMT_STARTUP_DISCARD` 丢弃次数无效，因为问题是每个触发周期的首个通道，而非系统启动后的前几次触发
