# 控制环设计说明（基于当前工程实际配置）

本文档仅基于当前仓库 **已经存在的代码、配置和验证路径** 进行整理，目标是回答两个问题：

1. 当前 `HRPWM + TRGM + ADC PMT` 的同步采样链路是如何工作的；
2. 在不大改现有架构的前提下，电感电流快环应如何接入，以及多快的更新频率是现实可行的。

> **文档状态**：基于当前工程代码的现状整理
>
> **约束原则**：只写当前代码已经实现或能直接推导出的内容；不引入尚不存在的 `intf_control` / `drv_control` 一类抽象接口；对未来方案仅写成“建议下一步”。

---

## 1. 当前工程事实基线

### 1.1 App 层现状

当前 `App/` 目录已拆分为：

- `Application/`：业务编排预留层（当前仍是骨架）
- `Control/`：控制逻辑预留层（当前仍是骨架）
- `Algorithm/`：算法库预留层（当前仍是骨架）
- `Platform/`：已存在的应用级平台封装
- `Debug/`：当前实际运行入口和调试验证路径

当前 `App/main.c` 并未进入正式控制流程，而是默认执行：

```c
while (1) {
    app_debug_adc_pmt_run_tests();
    intf_clock_delay_ms(1000);
}
```

因此，当前仓库中 **真实跑通的 PWM-ADC 联动入口** 是 `App/Debug/Src/app_debug_rtt.c` 里的 PMT 测试路径，而不是正式控制器。

---

### 1.2 当前 PWM 配置

> **时钟域说明**：当前工程 `CPU0` 主频由 `PLL0` 配置为 **480MHz**，但 PWM 与 ADC 的时间预算仍应按外设侧时钟计算：当前 `AHB = 120MHz`，`clock_mot0 = 120MHz`，`clock_adc0/1` 上游也按 `120MHz` 计算，然后 ADC 再通过 `clock_div` 降频。

当前 `App/Platform/Src/app_hrpwm.c` 默认配置如下：

| Pair | PWM 实例 | 频率 | 对齐模式 | 初始 duty | 死区 |
|------|----------|------|----------|-----------|------|
| `PWM_PAIR_0` | `PWM0` | `200kHz` | 中心对齐 | `0.5` | `10ns` |
| `PWM_PAIR_1` | `PWM0` | `200kHz` | 中心对齐 | `0.3` | `10ns` |
| `PWM_PAIR_2` | `PWM1` | `148kHz` | 中心对齐 | `0.5` | `25ns` |
| `PWM_PAIR_3` | `PWM1` | `148kHz` | 中心对齐 | `0.4` | `25ns` |

当前控制讨论主要聚焦在 `PWM0 / ADC0 / I_L` 这一条链路，也就是 `200kHz` 电感电流快环。

---

### 1.3 当前 ADC 配置

当前 `App/Platform/Src/app_adc.c` / `app_sampling_sync.c` 使用的默认 ADC 参数为：

| 项目 | 当前值 | 来源 |
|------|--------|------|
| 分辨率 | `16-bit` | `INTF_ADC_RES_DEFAULT` |
| `sample_cycle` | `20` | `INTF_ADC_DEFAULT_SAMPLE_CYCLE` |
| `clock_div` | `3` | `INTF_ADC_DEFAULT_CLOCK_DIV` |
| `vref_mv` | `3300mV` | `INTF_ADC_DEFAULT_VREF_MV` |
| 采样模式 | `PMT` | `app_sampling_sync_init_adc0/1()` |

ADC 上游时钟来自 AHB `120MHz`，按 `clock_div = 3` 计算：

```text
ADC clock = 120MHz / 3 = 40MHz
ADC cycle = 25ns
```

16-bit 单通道转换时间近似为：

```text
t_conv ≈ (sample_cycle + conv_cycle_16bit) / f_adc
       ≈ (20 + 21) / 40MHz
       ≈ 1.025us
```

这个结论与当前 `doc/adc_driver_design.md` 中的表格一致。

---

## 2. 当前同步采样链路

### 2.1 ADC 通道映射

当前 `App/Platform/Inc/app_adc.h` 中定义的逻辑通道如下：

| 逻辑通道 | ADC实例 | 物理通道 | 引脚 | 说明 |
|----------|---------|----------|------|------|
| `ADC_CH_V_IN` | ADC0 | ch6 | PB14 | Buck-Boost 输入电压 |
| `ADC_CH_I_IN` | ADC0 | ch11 | PB08 | 输入母线电流 |
| `ADC_CH_I_L` | ADC0 | ch2 | PB10 | **电感电流** |
| `ADC_CH_V_LINK` | ADC0 | ch3 | PB11 | 级联母线电压 |
| `ADC_CH_I_COIL` | ADC1 | ch4 | PB12 | 线圈电流 |
| `ADC_CH_I_LF` | ADC1 | ch5 | PB13 | LCC 谐振电流 |

---

### 2.2 当前 PMT 分组配置

`App/Platform/Src/app_sampling_sync.c` 当前固定配置为：

#### ADC0（由 PWM0 触发）

```text
pmt_trig_ch = 0
channels     = { ch6, ch11, ch2, ch3 }
            = { V_IN, I_IN, I_L, V_LINK }
```

#### ADC1（由 PWM1 触发）

```text
pmt_trig_ch = 3
channels     = { ch4, ch5 }
            = { I_COIL, I_LF }
```

因此当前 PMT 并不是“只采一个关键量”的 fast path，而是“多通道同步监测/调试路径”。

---

### 2.3 当前触发与回调调用链

当前已经实际跑通的调用链是：

```text
main.c
  -> app_debug_adc_pmt_run_tests()
      -> pwm_init()
      -> app_sampling_sync_get_default_config()
      -> app_sampling_sync_init(&sync_cfg)
          -> intf_hrpwm_config_trigger_cmp(PWM_INST_0, cmp8, 0.5f)
          -> intf_hrpwm_config_trigger_cmp(PWM_INST_1, cmp8, 0.5f)
          -> intf_trgm_connect(PWM0_CH8REF -> ADC_PTRGI0A)
          -> intf_trgm_connect(PWM1_CH8REF -> ADC_PTRGI1A)
          -> intf_adc_init(...PMT for ADC0...)
          -> intf_adc_init(...PMT for ADC1...)
      -> app_sampling_sync_start()
          -> app_adc_pmt_start_inst(APP_ADC_INST_0)
          -> app_adc_pmt_start_inst(APP_ADC_INST_1)
```

硬件运行后，中断回流链路为：

```text
PWM compare trigger
  -> TRGM
    -> ADC PMT queue
      -> ADC conversion complete
        -> drv_adc.c: adc_generic_isr(inst)
          -> ai->pmt.cb(...)
            -> App callback (当前是 Debug 中的 pmt_cb_adc0/pmt_cb_adc1)
```

当前 `drv_adc.c` 在 PMT 回调中会：

- DMA 模式下，从 `pmt_trig_ch * 4` 对应 slot 读取结果；
- 非 DMA 模式下，从 `BUS_RESULT[]` 取回结果；
- 再调用上层注册的 `pmt_cb`。

---

## 3. 当前可用于快环的两个实时入口

### 3.1 ADC PMT callback

当前工程已经具备 ADC PMT 完成回调能力。

特点：

- 触发点与 PWM compare 同步
- 回调发生在 **采样转换完成后**
- 是“数据可用事件”，不是“PWM 周期边界事件”

适合做：

- 锁存关键样本
- 更新采样邮箱
- 轻量计数/统计

不适合直接承载大量逻辑，除非该逻辑极短且样本路径极简。

---

### 3.2 PWM reload IRQ

当前工程也已经具备 PWM reload 中断能力：

- `Interface/intf_hrpwm.h`
  - `intf_hrpwm_config_reload_irq()`
  - `intf_hrpwm_enable_reload_irq()`
  - `intf_hrpwm_disable_reload_irq()`
- `Driver/hpm_impl/drv_hrpwm.c`
  - `IRQn_PWM0` / `IRQn_PWM1`
  - `PWM_IRQ_RELOAD`
  - `hrpwm_reload_callback[]`
- `App/Debug/Src/app_debug_rtt.c`
  - 已验证实例级 reload callback 注册与启停

特点：

- 每个 PWM 周期边界都会进入
- 更适合作为“更新下一周期占空比”的边界同步点
- 与 compare shadow 更新语义天然一致

---

## 4. 当前配置下的时间预算分析

### 4.1 PWM 周期

以 `PWM0 = 200kHz` 为例：

```text
T_pwm = 1 / 200000 = 5us
CPU cycles @480MHz = 5us * 480 = 2400 cycles
PWM ticks  @120MHz = 5us * 120 = 600 ticks
```

---

### 4.2 当前 ADC0 一次 PMT 触发的总转换时间

当前 ADC0 一次 PMT 触发采 4 路：

- `V_IN`
- `I_IN`
- `I_L`
- `V_LINK`

按单通道约 `1.025us` 估算：

```text
t_adc0_group ≈ 4 * 1.025us = 4.10us
```

这还不包含：

- 中断响应延迟
- ISR 入口/出口
- callback 分发

因此实际可粗略看作：

```text
t_pmt_callback_arrival ≈ 4.1us ~ 4.4us
```

---

### 4.3 对 200kHz 周期的影响

如果仍沿用当前 ADC0 四通道 PMT 分组，则：

```text
T_pwm = 5.0us
t_pmt_callback_arrival ≈ 4.1us ~ 4.4us
```

剩余时间仅约：

```text
0.6us ~ 0.9us
≈ 72 ~ 108 PWM/外设 ticks
≈ 288 ~ 432 CPU cycles @480MHz
```

这段时间需要完成：

- callback 取数
- 电流换算
- 单 PI 计算
- duty 限幅
- `set_duty()` 调用
- compare shadow 更新

### 判断

在当前多通道 PMT 配置下，**几乎不建议尝试“同一 PWM 周期内完成采样、转换、计算、更新下一周期 duty”**，余量过小，不适合作为稳定设计基础。

---

### 4.4 对 148kHz 周期的影响

`PWM1 = 148kHz` 时：

```text
T_pwm ≈ 6.76us
剩余时间 ≈ 6.76 - 4.10 = 2.66us
≈ 319 PWM/外设 ticks @120MHz
≈ 1277 CPU cycles @480MHz
```

虽然比 `200kHz` 宽裕，但若仍沿用当前多通道 PMT + 通用 `set_duty()` 路径，仍然偏紧，尤其在 ISR 中使用 float 时不够从容。

---

## 5. 基于当前配置的方案收敛

### 5.1 不建议的方案

#### 方案 A：保持当前 4 通道 PMT 组，并在 ADC callback 中直接单 PI + `set_duty()`

| 条件 | 结论 |
|------|------|
| `200kHz` / 当前 4 通道 PMT | **不建议** |
| `148kHz` / 当前 4 通道 PMT | 可做实验，但不建议作为正式设计 |

原因很简单：回调回得太晚，给控制更新留下的时间窗口过窄。

这里需要特别说明：

- 问题的主矛盾不是 `CPU0 = 480MHz` 不够算；
- 问题主要在于当前 ADC0 的 PMT 分组一次采了 `4` 路，导致 **ADC callback 到达时刻本身就已经太靠近周期末端**；
- 即使 CPU 运算预算比之前按 `120MHz` 估计的更大，当前多通道 PMT 方案在 `200kHz` 下仍然不适合做真正的逐周期闭环更新。

---

### 5.2 PWM0 + ADC0（Buck-Boost）采用的方案

结合当前工程配置与时间预算，`PWM0 + ADC0` 这条链路的设计决定收敛为：

## **每个 PWM 周期都同步采样，但控制器每 4 个 PWM 周期更新一次**

也就是：

- 每个周期都采 `I_L`
- 每个周期都进入 ADC PMT callback 并更新样本统计
- 每个周期都进入 PWM reload IRQ
- 仅当 `subcycle_div == 4` 时执行一次 PI 并更新 duty

它本质上是一个“伪逐周期”的电感电流快环。

以 `200kHz` 为例，若按 `4` 周期更新：

| 更新间隔 | 控制更新频率 | 控制更新周期 |
|----------|--------------|--------------|
| 每 4 周期 | 50kHz | 20us |

当前明确选择 `N = 4` 的原因：

- 继续复用现有 PMT 多通道链路，避免第一版就拆 fast path；
- `20us` 控制预算足以容纳样本平均、单 PI、限幅和当前通用 `set_duty()` 路径；
- 对调试和参数整定更友好；
- 仍然保留较高的快环更新频率（`50kHz`）。

---

### 5.3 PWM0 + ADC0 的推荐实现方式

#### 推荐模式：每周期采样 + 每 4 周期更新

```text
每个 PWM 周期：
  ADC PMT callback
    -> 读取 I_L 样本
    -> 参与 4 点简单均值滤波

每个 PWM reload IRQ：
  if (++subcycle >= 4)
      执行一次电流环 PI
      更新 duty
```

当前推荐的具体处理方式为：

- `N = 4`；
- PI 输入取最近 `4` 个周期样本的简单均值；
- duty 更新点建议放在 PWM reload IRQ，而不是放在 ADC callback 中。

#### 每 4 周期更新时：取数、计算、应用结果分别发生在什么时候？

这个问题的关键是区分三个动作：

1. **取数**：每个 PWM 周期都在 ADC PMT callback 中进行；
2. **计算**：每个 PWM 周期结束时都会进入一次 reload IRQ，但只有累计满 `4` 个样本后的那个周期边界才真正执行 PI；
3. **应用结果**：在该周期边界的 reload IRQ 中写入 compare shadow，新的 duty 从**紧接着开始的下一 PWM 周期**生效。

可以把一个“4 周期快环更新窗口”理解为下图：

```text
以 PWM0 = 200kHz 为例：每周期 5us，4周期 = 20us

周期:            k            k+1          k+2          k+3          k+4
时间:         0~5us        5~10us       10~15us      15~20us      20~25us

周期内部:      [采样1]       [采样2]       [采样3]       [采样4]       [采样5]
                 │             │             │             │
ADC回调:         I_L1          I_L2          I_L3          I_L4
                 │             │             │             │
样本累计:       acc+=I_L1     acc+=I_L2     acc+=I_L3     acc+=I_L4
                 cnt=1         cnt=2         cnt=3         cnt=4

周期边界:         |             |             |             |             |
reload IRQ:     end(k)       end(k+1)      end(k+2)      end(k+3)      end(k+4)
                 │             │             │             │
控制决策:        跳过          跳过          跳过          avg=(I_L1+I_L2+I_L3+I_L4)/4
                                                             -> PI(avg)
                                                             -> duty_next
                                                             -> 写 compare shadow
                                                                           │
新 duty 生效:                                                             从周期 k+4 开始生效
```

这里必须注意一个隐含前提：

- 第 `k+3` 个周期内的采样点和 ADC callback 必须发生在 `end(k+3)` 之前；
- 当前采用 `trigger_position_ratio = 0.5f`，即采样点位于该周期中部附近，因此样本 `I_L4` 会先到达，再进入该周期末的 `reload IRQ`；
- 也正因为如此，`reload IRQ @ end(k+3)` 才能使用 `I_L1 ~ I_L4` 这四个样本计算出 `duty_next`。

更直观地说：

- 周期 `k ~ k+3`：只采样、累积、计数；
- 周期 `k+3` 结束时的 `reload IRQ`：完成 `4` 个样本的均值计算和 PI；
- 周期 `k+4`：新的 duty 生效；
- 然后重新开始下一组 `4` 周期的采样累积。

因此这里虽然不是“每周期都更新 duty”，但它依然保持了两个关键特性：

- **采样始终与 PWM 同步**；
- **占空比更新始终在 PWM 周期边界生效**。

这就是它被称为“伪逐周期”快环的原因：

- 不是每个周期都算；
- 但一旦更新，仍然遵循同步采样 + 边界更新的控制语义。

#### 为什么伪逐周期并不是“单次计算窗口突然变大”

这一点需要特别澄清：

如果实现方式是：

```text
第 4 个 ADC callback 到来
  -> 做 4 点均值
  -> 做 PI
  -> set_duty()
```

那么你对它的质疑是成立的：

- **最后那一次控制计算本身的尾部窗口并没有显著变大**；
- 它仍然受制于“ADC callback 回来得太靠近周期末端”这个事实；
- 只是把“每周期都做一次重计算”变成了“每 4 个周期做一次重计算”。

因此，伪逐周期方案要想真正体现“更容易实现”的优势，关键不在于 `N = 4` 本身，而在于：

## **把每周期必须完成的工作，拆成“轻量采样累计”和“稀疏 full control update”两部分**

推荐实现是：

```text
每个 ADC callback：
  只做
    - 读取 I_L
    - acc += I_L
    - cnt++

每个 reload IRQ：
  if (cnt < 4)
      return
  else
      avg = acc / 4
      PI(avg)
      set_duty()
      清 acc / cnt
```

这样之后，伪逐周期相比真逐周期真正“省”的地方是：

1. **每个 PWM 周期不再都要完成一次 full control update**  
   每周期只要求“采样成功并累计”，不要求每次都完成 PI + duty 更新。

2. **重计算发生频率下降**  
   `PI + set_duty()` 从“每 5us 一次”变成“每 20us 一次”（以 200kHz、4 周期更新为例）。

3. **对 callback 到达抖动的敏感度下降**  
   单个周期 callback 的轻微波动，只影响样本累计时刻，不再意味着该周期必须马上完成闭环更新。

4. **可以继续复用当前多通道 PMT 路径**  
   在不立即拆出单通道 fast path 的前提下，先得到一个可工作的快环版本。

5. **当前通用 `set_duty()` 路径更容易先跑通**  
   不必一开始就把 `set_duty()` 优化成极限 fast path。

所以更准确的表达应该是：

> **伪逐周期更容易实现，不是因为“最后一次计算的单次 ISR 尾部时间大很多”，而是因为“每个 PWM 周期都必须完成一次完整闭环更新”的硬约束被放松了。**

原因：

- 更新边界更整齐；
- 便于后续切换为更严格的逐周期控制；
- 对当前通用 `set_duty()` 路径更宽容；
- 允许继续复用当前 PMT 多通道链路做监测与调试。

#### 为什么采用简单均值滤波

当前阶段选择“4 点简单均值”而不是更复杂滤波器，原因如下：

- 计算开销小，适合放在快环路径里；
- 对采样噪声有一定抑制作用；
- 不会显著增加实现复杂度；
- 便于后续根据波形和带宽需求替换成更合适的滤波策略。

---

### 5.4 PWM1 + ADC1（无线充电全桥发射）当前方案

对于 `PWM1 + ADC1` 这条链路，当前设计收敛为：

## **保留同步采样，暂不定义闭环控制算法**

当前 ADC1 采样通道为：

- `I_COIL`
- `I_LF`

当前仅将其视作：

- 无线充电全桥发射部分的同步观测通道；
- 后续控制策略研究、建模、波形分析的基础数据来源；
- 暂不进入“每 N 周期更新 duty”或“逐周期 PI”一类闭环控制实现。

这样做的原因：

- 当前 `PWM1 + ADC1` 的控制目标和控制量尚未最终明确；
- 采样链路已经具备，可以先保留观测能力；
- 避免在算法目标不清晰时过早固化控制框架。

---

### 5.5 单通道 `I_L` 真逐周期方案讨论

虽然当前最终决策是 `PWM0 + ADC0` 先采用“每 4 周期更新一次”的伪逐周期快环，但在当前平台上，仍然有必要明确“如果未来只采一个关键电感电流通道，真逐周期有没有可能”。

#### 前提变化

如果未来从当前 ADC0 四通道 PMT 分组：

- `V_IN`
- `I_IN`
- `I_L`
- `V_LINK`

切换为只采一个关键通道：

- `I_L`

则单次转换耗时仍约为：

```text
t_single_channel ≈ 1.025us
```

即使再加上：

- 中断响应
- callback 分发
- 软件读数与轻量处理

也可粗略按：

```text
t_callback_arrival ≈ 1.1us ~ 1.4us
```

估算。

#### 对 200kHz / 5us 的意义

若只采 `I_L`，则在 `200kHz` 下从 callback 到当前周期结束大约还剩：

```text
5.0us - 1.2us ≈ 3.8us
≈ 456 PWM/外设 ticks @120MHz
≈ 1824 CPU cycles @480MHz
```

这个预算已经明显大于当前 4 通道 PMT 分组下的剩余窗口。

#### 结论

在 `CPU0 = 480MHz`、`PWM/ADC` 仍运行于 `120MHz / 40MHz` 外设域的前提下：

- **4 通道 PMT 当前方案**：仍不适合 `200kHz` 真逐周期快环；
- **单通道 `I_L` fast path**：存在实现真逐周期快环的现实可能。

但它仍然有工程前提：

1. PMT 必须真正只采 `I_L`，不能复用当前 4 通道组；
2. ADC callback 中只允许保留最小必要控制链；
3. `set_duty()` 路径最好进一步轻量化，必要时演化为 fast path 版本；
4. `trigger_position_ratio` 需要重新按“采样质量 + 更新时间余量”联合优化；
5. 需要通过 GPIO 打点或示波器验证“采样完成 → 控制计算 → shadow 生效”的完整时序。

因此，单通道 `I_L` 真逐周期方案当前结论应写为：

> **在当前平台上“有条件可行”，但需要单独拆出 fast path，不能直接把现有多通道 PMT 调试链路当作真逐周期控制路径。**

---

## 6. 当前阶段的模块落点建议

在不改 `main.c` 主体行为的前提下，建议未来将电流快环落在以下层次：

### 6.1 Platform

- `app_adc`：ADC 采样与基础换算
- `app_sampling_sync`：PWM→TRGM→ADC PMT 触发配置
- `app_hrpwm`：占空比/频率/相位与 reload IRQ 封装

这层只负责**采样能力和执行能力**，不负责控制算法。

---

### 6.2 Algorithm

建议未来新增：

- `algo_pi.h/.c`
- `algo_limit.h/.c`

只放纯算法：

- PI 计算
- anti-windup
- 限幅
- 轻量滤波（如果需要）

---

### 6.3 Control

建议未来新增：

- `ctrl_current_loop.h/.c`

负责：

- 保存 `i_ref`
- 保存 `latest_i_l_raw` / 累积平均状态
- 保存 `subcycle_div`
- 在 reload IRQ 中决定是否执行本次 `4` 周期 PI 更新
- 最终调用 `pwm_set_duty(...)`

推荐接口示意：

```c
void ctrl_current_loop_init(void);
void ctrl_current_loop_enable(bool en);
void ctrl_current_loop_set_reference(float i_ref);

void ctrl_current_loop_on_adc_sample(uint16_t i_l_raw);
void ctrl_current_loop_on_pwm_reload(void);
```

其中：

- `on_adc_sample()`：每周期调用，收样本
- `on_pwm_reload()`：每周期调用，但仅每 `4` 次真正执行控制更新

---

## 7. 当前结论与下一步建议

### 7.1 当前严格结论

基于当前工程的真实配置：

1. **当前 4 通道 ADC0 PMT 分组不适合直接支撑 200kHz 真逐周期电流环**；
2. **对于 PWM0 + ADC0（Buck-Boost），当前明确采用“每周期采样、每 4 周期均值 + PI 更新 duty”的伪逐周期方案**；
3. **对于 PWM1 + ADC1（无线充全桥发射），当前仅保留同步采样，暂不定义闭环控制算法**；
4. PWM0 电流快环第一版建议把更新点放在 **PWM reload IRQ**，而不是 ADC callback；
5. 如果后续要冲击真逐周期，需要进一步拆出 **单通道 fast path**，只采 `I_L`。

---

### 7.2 当前待确认事项

- [ ] `trigger_position_ratio = 0.5f` 是否是最终最优采样点，需要结合示波器验证
- [ ] `200kHz` 下每 4 周期更新一次时，PI 参数整定策略
- [ ] 4 点简单均值是否足够，还是需要更适合的快环滤波策略
- [ ] 当前 `set_duty()` 路径是否需要后续补一个 fast version
- [ ] 是否需要将 PWM0 快环与现有多通道监测链路分离为独立 fast path / slow path
- [ ] PWM1 + ADC1 后续应采用哪种控制目标（功率、线圈电流、谐振状态或其它）
- [ ] 若未来改成单通道 `I_L` fast path，是否推进真逐周期控制实现

---

## 8. 参考依据

1. `App/Platform/Src/app_hrpwm.c`：当前 PWM 频率、对齐模式、pair 配置
2. `App/Platform/Src/app_sampling_sync.c`：当前 PMT 分组、TRGM 触发链、默认采样点位置
3. `App/Platform/Src/app_adc.c`：当前 ADC 默认配置
4. `Driver/hpm_impl/drv_adc.c`：PMT callback 回流机制
5. `Driver/hpm_impl/drv_hrpwm.c`：reload IRQ 与 compare shadow 更新机制
6. `doc/adc_driver_design.md`：ADC 时钟与转换耗时说明
7. `doc/hrpwm_driver_design.md`：PWM 时钟与周期参数说明

---

## 9. 修订记录

| 日期 | 版本 | 说明 |
|------|------|------|
| 2026-05-31 | v0.1 | 初始控制环讨论文档 |
| 2026-06-04 | v0.2 | 根据当前工程实际重写，删除猜测性接口，补充 PMT/IRQ/时间预算分析 |
| 2026-06-04 | v0.3 | 修正 CPU 480MHz / AHB 120MHz 表述，并补充单通道 `I_L` 真逐周期可行性讨论 |
