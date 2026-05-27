# TODO - HPM5361 Wireless Charger

本文件依据 `AGENTS.md`（嵌入式 C17 解耦架构开发指南）梳理，列出当前代码与规范的差距及待实现功能。

---

## 1. 架构对齐（AGENTS.md 规范差距）

### 1.1 接口层重构：匿名结构体 + 参数归一化

AGENTS.md §3.1 要求接口使用匿名结构体，参数归一化。当前接口使用传统 ops 结构体 + `uint8_t` 占空比。

| 接口 | 当前 | 规范要求 | 文件 | 状态 |
|------|------|----------|------|------|
| GPIO | `intf_gpio_t` + `intf_gpio_cfg_t` | 已符合规范 | `Interface/intf_gpio.h` | ✅ 完成 |
| Clock | `intf_clock.h` | 已符合规范 | `Interface/intf_clock.h` | ✅ 完成 |
| PWM | `intf_pwm_ops_t` + `uint8_t duty_percent` | `pwm_if_t` 匿名结构体 + `float duty [0.0-1.0]` | `Interface/intf_pwm.h` | ❌ TODO |
| ADC | `intf_adc_ops_t` + `uint16_t raw` | `adc_if_t` + `float voltage` 归一化 | `Interface/intf_adc.h` | ❌ TODO |
| UART | `intf_uart_ops_t` | `uart_if_t` 匿名结构体 | `Interface/intf_uart.h` | ❌ TODO |
| SPI | `intf_spi_ops_t` | `spi_if_t` 匿名结构体 | `Interface/intf_spi.h` | ❌ TODO |
| I2C | `intf_i2c_ops_t` | `i2c_if_t` 匿名结构体 | `Interface/intf_i2c.h` | ❌ TODO |

### 1.2 缺失文件

| 文件 | 说明 | 优先级 | 状态 |
|------|------|--------|------|
| `Interface/platform_defs.h` | 平台公共定义（`status_t`, `bool`, 归一化类型） | 高 | ❌ TODO |
| `Module/` 目录 | 传感器/执行器对象层（如无线充电 IC 驱动） | 中 | ❌ TODO |

### 1.3 C17 特性应用

| 特性 | 应用场景 | 状态 |
|------|----------|------|
| `static_assert` | 编译期校验通道数、缓冲区大小、结构体对齐 | ❌ TODO |
| `_Generic` | 类型安全的接口宏（如 `intf_write(dev, ...)` 自动分发） | ❌ TODO |
| 匿名结构体 | 接口对象化（`dev->set_duty()` 直接调用） | ❌ TODO |
| `_Alignas` | DMA 缓冲区 L1 Cache 对齐（32 字节） | ❌ TODO |

### 1.4 性能优化

| 项目 | 说明 | 状态 |
|------|------|------|
| ILM 部署 | 将关键函数放入 ILM（`__attribute__((section(".ilm")))`） | ❌ TODO |
| Cache 对齐 | ADC DMA 缓冲区 `_Alignas(32)` | ❌ TODO |
| RAMFUNC | 中断处理函数、PID 控制器放入 RAM | ❌ TODO |

---

## 2. Driver 层实现

### 2.1 Clock 驱动 (`Driver/hpm_impl/drv_clock.c`)

- [x] `#include "hpm_clock_drv.h"`, `hpm_pllctlv2_drv.h`, `hpm_sysctl_drv.h`, `hpm_pcfg_drv.h`
- [x] `intf_clock_init()` - PLL 配置、时钟分频、外设时钟使能
- [x] `intf_clock_get_cpu_freq()` - 获取 CPU 频率
- [x] `intf_clock_get_ahb_freq()` - 获取 AHB 频率
- [x] 时钟配置：PLL0=480MHz, CPU0=480MHz, AHB=120MHz, MCHTMR0=24MHz

### 2.2 GPIO 驱动 (`Driver/hpm_impl/drv_gpio.c`)

- [x] `#include "hpm_gpio_drv.h"`, `hpm_gpiom_drv.h"`, `hpm_interrupt.h"`, `hpm_soc_irq.h"`
- [x] `hpm_gpio_init()` - 配置 GPIO 方向、电平、中断
- [x] `hpm_gpio_set_level()` - 设置输出电平
- [x] `hpm_gpio_get_level()` - 读取输入电平
- [x] `hpm_gpio_toggle()` - 翻转输出电平
- [x] GPIO 中断支持 - 下降沿触发、回调函数注册
- [x] PLIC 中断使能 - IRQn_GPIO0_A, IRQn_GPIO0_B
- [ ] GPIO 消抖配置 - 需要直接操作 FILTER 寄存器

### 2.3 HRPWM 驱动 (`Driver/hpm_impl/drv_hrpwm.c`)

> **注意**：HPM5361 的 `PWM_SOC_HRPWM_SUPPORT = 0`，不支持真正的亚时钟级 HRPWM。当前实现基于普通 `HPM_PWM0` API，命名保留为 `hrpwm` 用于接口演进。

- [x] `intf_hrpwm.h` 接口定义 - 匿名结构体 ops、float duty [0.0-1.0]（符合 AGENTS.md 规范）
- [x] `drv_hrpwm.c` 驱动实现 - 映射到 HPM_PWM0，165 行完整实现
- [x] `intf_default.c` 注册分发 - `hrpwm_ops` 指针保存与分发
- [x] 边沿对齐 PWM 输出（ch0..3，PA24-PA27 → PWM0_P_0..3）
- [x] 频率/占空比动态调整（实例级频率共享，改频后重新应用所有通道 duty）
- [x] 启停控制（`stop` 只关闭单通道输出，不停全局 counter）
- [x] NaN duty 防护（`duty == duty` 检测 NaN）
- [x] 通道范围校验（`ch < HRPWM_CHANNEL_COUNT`）
- [x] 时钟配置（`clock_mot0`，`clock_add_to_group` 使能）
- [ ] fault source / fault recovery 配置
- [ ] deadtime 配置（当前固定为 0）
- [ ] 互补输出 pair 模式（需使用 `pwm_setup_waveform_in_pair`）
- [ ] force-safe-low / brake API（参考 GPWM 的 `force_low`/`force_release`）
- [ ] shadow register 同步更新策略优化

### 2.4 GPWM 驱动 (`Driver/hpm_impl/drv_gpwm.c`)

- [x] `intf_gpwm.h` 接口定义 - 匿名结构体 ops、float duty [0.0-1.0]
- [x] `drv_gpwm.c` 驱动实现 - 映射到 HPM_GPTMR0
- [x] `intf_default.c` 注册分发
- [x] PWM 输出（ch2=PA10, ch3=PB15）
- [x] 频率/占空比动态调整
- [x] 启停控制（start 时复位计数器，确保首周期完整）
- [x] force_low / force_release 强制输出（CMP0/CMP1=0xFFFFFFFF）
- [x] 输入捕获（ch1=PB09，polling 模式，rising/falling/both edge）
- [x] NaN duty 防护
- [x] 通道范围校验（输出 ch2..3，捕获 ch1）

---

## 4. HRPWM 驱动架构总结

### 4.1 当前实现状态

| 组件 | 文件 | 状态 | 说明 |
|------|------|------|------|
| 接口定义 | `Interface/intf_hrpwm.h` | ✅ 完成 | 匿名结构体 ops、float duty 归一化 |
| 驱动实现 | `Driver/hpm_impl/drv_hrpwm.c` | ✅ 完成 | 165 行，映射到 HPM_PWM0 |
| 注册分发 | `Interface/intf_default.c` | ✅ 完成 | `hrpwm_ops` 指针保存与分发 |
| 板级引脚 | `Board/.../pinmux.c` | ✅ 完成 | PA24-PA27 → PWM0_P_0..3 |
| 设计文档 | `doc/hrpwm_driver_design.md` | ✅ 完成 | 包含 SDK 示例参考和高级功能指南 |

> **注意**：HPM5361 的 `PWM_SOC_HRPWM_SUPPORT = 0`，不支持真正的亚时钟级 HRPWM。当前实现基于普通 `HPM_PWM0` API，命名保留为 `hrpwm` 用于接口演进。

### 4.2 SDK 示例参考

| 示例 | 位置 | 关键内容 |
|------|------|----------|
| **抖动技术** | `samples/drivers/pwm/pwm_output/` | `jitter_cmp` 配置，提高 DPWM 分辨率 |
| **HRPWM 输出** | `samples/drivers/pwm/hrpwm/` | 高分辨率 PWM + Fault 保护 |
| **HRPWM 校准** | `samples/drivers/pwmv2/hrpwm_calibrate/` | 温度补偿校准 |
| **电机控制** | `samples/motor_ctrl/bldc_foc/` | PWM + ADC 同步采样 |

### 4.3 后续开发优先级

| 优先级 | 功能 | 说明 |
|--------|------|------|
| 高 | fault 保护 | fault source、fault recovery、safe output state |
| 高 | deadtime | 功率桥驱动必需，当前固定为 0 |
| 高 | 抖动技术集成 | 将 `jitter_cmp` 集成到 drv_hrpwm.c，提高分辨率 |
| 中 | 互补输出 | `pwm_setup_waveform_in_pair` 封装 |
| 中 | force-safe-low | 参考 SDK 示例实现功率级安全关断 |
| 中 | bootstrap | 统一驱动注册入口 `hpm_drivers_register_all()` |

详细设计文档请参考：`doc/hrpwm_driver_design.md`

---

### 2.5 Clock 驱动扩展 (`Driver/hpm_impl/drv_clock.c`)

- [x] `intf_clock_delay_ms()` - 封装 `clock_cpu_delay_ms()`
- [x] `intf_clock_delay_us()` - 封装 `clock_cpu_delay_us()`

### 2.6 ADC 驱动 (`Driver/hpm_impl/drv_adc.c`)

- [ ] `#include "hpm_adc16_drv.h"`
- [ ] `hpm_adc_init()` - 配置 ADC 时钟、采样率、分辨率
- [ ] `hpm_adc_read()` - 软件触发 + 读取结果
- [ ] `hpm_adc_read_voltage()` - 原始值 → mV 转换
- [ ] `hpm_adc_start()` / `hpm_adc_stop()` - 连续采样控制
- [ ] DMA 传输配置（大吞吐量场景）
- [ ] 硬件触发源配置（GPTMR/PWM 同步采样）

### 2.5 UART 驱动 (`Driver/hpm_impl/drv_uart.c`)

- [ ] `#include "hpm_uart_drv.h"`
- [ ] `hpm_uart_init()` - 波特率、数据位、停止位配置
- [ ] `hpm_uart_transmit()` - 阻塞发送
- [ ] `hpm_uart_receive()` - 阻塞接收
- [ ] `hpm_uart_register_rx_callback()` - IRQ 异步接收
- [ ] FIFO 配置与中断管理

### 2.6 SPI 驱动 (`Driver/hpm_impl/drv_spi.c`)

- [ ] `#include "hpm_spi_drv.h"`
- [ ] `hpm_spi_init()` - 时钟极性/相位、速率配置
- [ ] `hpm_spi_transfer()` - 全双工传输
- [ ] `hpm_spi_transmit()` / `hpm_spi_receive()` - 单向传输
- [ ] DMA 传输配置

### 2.7 I2C 驱动 (`Driver/hpm_impl/drv_i2c.c`)

- [ ] `#include "hpm_i2c_drv.h"`
- [ ] `hpm_i2c_init()` - 时钟、地址模式配置
- [ ] `hpm_i2c_master_transmit()` / `hpm_i2c_master_receive()`
- [ ] `hpm_i2c_write_reg()` / `hpm_i2c_read_reg()` - 寄存器读写
- [ ] 10-bit 地址支持

---

## 3. App 层业务逻辑

### 3.1 GPIO 业务封装 (`App/Logic/Src/app_gpio.c`)

- [x] `app_gpio_init()` - GPIO 驱动注册、引脚初始化
- [x] `app_gpio_set()` - 设置输出电平
- [x] `app_gpio_toggle()` - 翻转输出电平
- [x] `app_gpio_read()` - 读取输入电平
- [x] 按键中断回调 - `button_isr()`、`button_press_count`
- [ ] 按键中断触发问题排查 - 需要在调试器中验证

### 3.2 Buzzer 业务封装 (`App/Logic/Src/app_buzzer.c`)

- [x] `app_buzzer_init()` - GPWM 驱动注册、ch3 初始化、默认 4kHz、force_low
- [x] `app_buzzer_set(bool enabled, uint32_t frequency_hz)` - 控制蜂鸣器开关和音调
- [x] 使用 GPWM force_low/force_release 实现确定低电平静音

### 3.3 充电状态机（待实现）

| 状态 | 功能 | 优先级 | 状态 |
|------|------|--------|------|
| `IDLE` | 等待使能信号或自动启动 | 高 | ❌ TODO |
| `INIT` | 自检、外设配置 | 高 | ❌ TODO |
| `DETECTING` | 接收端检测、FOD（异物检测） | 高 | ❌ TODO |
| `CHARGING` | CC/CV 充电控制 | 高 | ❌ TODO |
| `COMPLETE` | 涓流充电或空闲 | 中 | ❌ TODO |
| `FAULT` | 故障处理、输出关断、错误日志 | 高 | ❌ TODO |

### 3.3 控制算法（待实现）

- [ ] PI/PID 控制器（电压环/电流环）
- [ ] 占空比限幅与软启动
- [ ] 温度补偿

### 3.4 保护逻辑（待实现）

- [ ] 过压保护（OVP）
- [ ] 过流保护（OCP）
- [ ] 过温保护（OTP）
- [ ] 短路保护（SCP）
- [ ] 故障恢复逻辑

### 3.5 通信协议（待实现）

- [ ] UART 调试命令解析
- [ ] 充电状态上报
- [ ] 参数在线调整

---

## 4. Module 层（待创建）

依据 AGENTS.md §2，Module 层用于封装传感器/执行器对象。

### 4.1 建议模块

| 模块 | 文件 | 说明 | 状态 |
|------|------|------|------|
| 无线充电 IC | `Module/wireless_charger/` | Qi/PMA 协议封装 | ❌ TODO |
| 电源管理 | `Module/power_stage/` | 全桥/半桥驱动封装 | ❌ TODO |
| 采样模块 | `Module/sensing/` | 电压/电流/温度采样封装 | ❌ TODO |

---

## 5. 构建与验证

- [x] 编译通过（Debug 模式，-O0）
- [x] 编译通过（Release 模式，-O2）
- [x] 链接脚本配置（使用 SDK 默认）
- [x] 调试路径映射（容器内 → 容器外）
- [ ] 编译零警告（`-Wall -Wextra -Werror`）
- [ ] 静态分析通过（clang-tidy）
- [ ] 单元测试框架搭建（如 Unity）
- [ ] 硬件在环测试（HIL）

---

## 6. 调试与烧录

### 6.1 烧录工具

- [x] OpenOCD 安装 - `/workspace/tools/openocd-hpm/`
- [x] J-Link 工具安装 - `/workspace/tools/jlink-V8.40/`
- [x] Makefile 烧录配置 - `FLASH_TOOL=openocd` 或 `FLASH_TOOL=jlink`

### 6.2 Ozone 调试

- [x] ELF 路径映射 - `HOST_WORKSPACE_DIR` 宏配置
- [x] 链接脚本修复 - 使用 SDK 默认 `flash_xip.ld`
- [x] `.nor_cfg_option` 启用 - Flash 配置正确
- [x] JTAG TRST 引脚修复 - PA08 配置为 GPIO 输出高电平
- [ ] J-Link 固件升级 - 当前 V1 有已知问题
- [ ] 按键中断触发问题 - 需要在调试器中验证

### 6.3 已解决问题

| 问题 | 解决方案 |
|------|----------|
| Ozone 找不到源文件 | 添加 `-fdebug-prefix-map` 路径重映射 |
| Ozone 断点无法命中 | 修复链接脚本、启用 `.nor_cfg_option` |
| 时钟初始化不稳定 | 调整 PLL 初始化顺序（先 PLL 后分频） |
| PA08 影响 JTAG 调试 | 恢复 PA08 为 GPIO 输出高电平 |

---

## 7. 优先级排序

**P0 - 立即**：
1. 按键中断触发问题排查
2. PWM 驱动实现（充电核心）
3. ADC 驱动实现（采样核心）

**P1 - 短期**：
1. 充电状态机框架
2. 接口层匿名结构体重构
3. platform_defs.h 创建
4. 保护逻辑实现

**P2 - 中期**：
1. Module 层创建
2. C17 特性应用
3. 性能优化（ILM/Cache）

**P3 - 长期**：
1. 单元测试
2. 硬件在环测试（HIL）

---

## 8. 当前目录结构

```
HPM5361_WirelessCharger/
├── App/
│   ├── main.c
│   └── Logic/
│       ├── Inc/
│       │   └── app_gpio.h
│       ├── Src/
│       │   └── app_gpio.c
│       └── app_logic.c
├── Board/
│   ├── HPM5361_WirelessCharger_board/
│   │   ├── board.c
│   │   ├── board.h
│   │   ├── pinmux.c
│   │   └── pinmux.h
│   └── hpm5301evklite_board/
├── Driver/
│   └── hpm_impl/
│       ├── drv_clock.c
│       ├── drv_gpio.c
│       ├── drv_pwm.c      (TODO)
│       ├── drv_adc.c      (TODO)
│       ├── drv_uart.c     (TODO)
│       ├── drv_spi.c      (TODO)
│       └── drv_i2c.c      (TODO)
├── Interface/
│   ├── intf_clock.h
│   ├── intf_gpio.h
│   ├── intf_pwm.h         (TODO)
│   ├── intf_adc.h         (TODO)
│   ├── intf_uart.h        (TODO)
│   ├── intf_spi.h         (TODO)
│   ├── intf_i2c.h         (TODO)
│   └── intf_default.c
├── linkers/
├── doc/
│   └── todo.md
├── CMakeLists.txt
└── Makefile
```

---

## 9. 关键配置

### 9.1 时钟配置

| 时钟 | 频率 | 说明 |
|------|------|------|
| PLL0 | 480 MHz | 主 PLL |
| CPU0 | 480 MHz | cpu_div = 1 |
| AHB | 120 MHz | ahb_sub_div = 4 |
| MCHTMR0 | 24 MHz | OSC24M |

### 9.2 GPIO 引脚配置

| 引脚 | 功能 | 方向 | 初始值 | 上拉/下拉 | 特殊配置 |
|------|------|------|--------|-----------|----------|
| PA02 | PIN_COMM_1 | 输出 | 低电平 | 下拉 | - |
| PA03 | PIN_COMM_2 | 输出 | 低电平 | 下拉 | - |
| PA08 | PIN_DRVPWR | 输出 | 高电平 | 上拉 | 保持高避免影响 JTAG TRST |
| PA09 | PIN_BUTTON | 输入 | - | 上拉 | 施密特触发器，下降沿中断 |

### 9.3 中断配置

| 中断源 | IRQ 号 | 优先级 | 回调函数 |
|--------|--------|--------|----------|
| GPIO0_A (GPIOA PA0-PA31) | IRQn_GPIO0_A | 1 | isr_gpio0_a |
| GPIO0_B (GPIOB PB0-PB31) | IRQn_GPIO0_B | 1 | isr_gpio0_b |
