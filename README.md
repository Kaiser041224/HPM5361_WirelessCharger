# HPM5361 Wireless Charger

HPM (HPMicro) 嵌入式开发工程模板，基于 **Board/Interface/Driver/App** 四层架构。

## 架构概览

```
┌─────────────────────────────────────────────────────────┐
│                    App (纯业务逻辑)                       │
│              禁止包含任何 hpm_* 头文件                     │
├─────────────────────────────────────────────────────────┤
│                Interface (C17 抽象接口)                   │
│    intf_hrpwm.h / intf_gpwm.h / intf_gpio.h / ...       │
├─────────────────────────────────────────────────────────┤
│              Driver/hpm_impl (SDK 适配层)                 │
│    drv_hrpwm.c / drv_gpwm.c / drv_gpio.c (唯一可调用     │
│                   hpm_sdk API 的地方)                    │
├─────────────────────────────────────────────────────────┤
│                Board (硬件配置层)                         │
│      pinmux.c (IOCFG) / board.c (时钟引脚)               │
│      HPM5361_WirelessCharger_board/ (板级 BSP)           │
└─────────────────────────────────────────────────────────┘
```

## 目录结构

```
HPM5361_WirelessCharger/
├── CMakeLists.txt              # 顶层构建配置
├── Makefile                    # 构建入口
├── AGENTS.md                   # 嵌入式 C17 解耦架构开发指南
├── Board/                      # 硬件配置层
│   └── HPM5361_WirelessCharger_board/
│       ├── board.c             # 板级初始化
│       ├── board.h             # 板级定义
│       ├── pinmux.c            # 引脚配置 (PWM0, GPTMR0, UART, GPIO...)
│       └── pinmux.h
├── Interface/                  # C17 抽象接口
│   ├── intf_hrpwm.h            # HRPWM 接口 (高性能 PWM)
│   ├── intf_gpwm.h             # GPWM 接口 (GPTMR PWM + 输入捕获)
│   ├── intf_pwm.h              # PWM 接口 (基础 PWM)
│   ├── intf_gpio.h             # GPIO 接口
│   ├── intf_clock.h            # Clock 接口
│   ├── intf_adc.h              # ADC 接口
│   ├── intf_uart.h             # UART 接口
│   ├── intf_spi.h              # SPI 接口
│   ├── intf_i2c.h              # I2C 接口
│   ├── intf_ws2812.h           # WS2812 接口
│   ├── intf_can.h               # CAN 接口 (MCAN/CAN-FD)
│   └── intf_default.c          # 注册分发实现
├── Driver/
│   └── hpm_impl/               # HPM SDK 适配
│       ├── drv_hrpwm.c         # HRPWM 驱动 (映射到 HPM_PWM0)
│       ├── drv_gpwm.c          # GPWM 驱动 (映射到 HPM_GPTMR0)
│       ├── drv_gpio.c          # GPIO 驱动
│       ├── drv_clock.c         # Clock 驱动
│       ├── drv_hrpwm.h          # HRPWM 驱动宏
│       ├── drv_mcan.c           # MCAN 驱动 (CAN-FD)
│       ├── drv_mcan.h           # MCAN 驱动声明
│       ├── drv_adc.c           # ADC 驱动
│       ├── drv_uart.c          # UART 驱动
│       ├── drv_spi.c           # SPI 驱动
│       ├── drv_i2c.c           # I2C 驱动
│       └── drv_ws2812.c        # WS2812 驱动
├── App/                        # 应用层
│   ├── main.c                  # 系统启动桥接
│   ├── Application/            # 应用编排 (app_entry/app_control/app_comm)
│   ├── Control/                # 面向对象控制器 (ctrl_buckboost/ctrl_lcc/ctrl_fault)
│   ├── Algorithm/              # 纯算法库 (PID/PLL/Ramp/RMS/FFD/Filter)
│   ├── Platform/               # 应用级平台封装 (app_hrpwm/app_adc/app_sampling_sync/app_can)
│   └── Debug/                  # 调试测试 (app_debug_rtt/app_debug_adc/app_debug_can/app_debug_hrpwm)
├── doc/                        # 文档
│   ├── hrpwm_driver_design.md  # HRPWM 驱动设计文档
│   └── adc_driver_design.md    # ADC 驱动设计文档
│   ├── hpm_pwm_gptmr_pwm_guide.md  # HPM PWM/GPTMR 使用指南
│   └── todo.md                 # TODO 清单
├── linkers/                    # 链接脚本
└── output/                     # 编译输出
```

## App 层分层说明

- `Application/`：放应用入口、业务编排、运行时调度，不直接承载具体外设访问。
- `Control/`：放控制状态机、闭环调节、功率级控制、保护策略。
- `Algorithm/`：放纯算法库，尽量不依赖 `app_*` 或 `hpm_*` 头文件。
- `Platform/`：放 `app_adc`、`app_hrpwm`、`app_sampling_sync`、`app_gpio` 等应用级平台能力封装。
- `Debug/`：放 RTT 输出、调试测试入口、参数验证辅助代码。

这样划分后：
- 平台访问和业务逻辑分离；
- 控制装配与纯算法分离；
- 调试代码不会继续堆入主业务目录；
- 后续新增算法库和控制器时不再依赖单一 `Logic/` 目录。

## 分层规则

| 层级 | 可包含头文件 | 职责 |
|------|-------------|------|
| **App** | `intf_*.h`, 标准 C | 纯业务逻辑，硬件无关 |
| **Interface** | 标准 C | 定义抽象接口，ops 注册机制 |
| **Driver** | `intf_*.h`, `hpm_*.h` | 实现接口，调用 SDK API |
| **Board** | `hpm_*.h` | 时钟、引脚初始化 |

## 快速开始

```bash
# 编译
make build

# 指定板级
make BOARD=HPM5361_WirelessCharger_board build

# 导出产物
make artifacts

# 烧录
make flash
```

## 已实现驱动

| 驱动 | 接口 | 状态 | 说明 |
|------|------|------|------|
| HRPWM | `intf_hrpwm.h` | ✅ 完成 | 双实例四对互补 PWM (ch0..7, PA24-PA31) |
| GPWM | `intf_gpwm.h` | ✅ 完成 | GPTMR PWM (ch2..3) + 输入捕获 (ch1) |
| GPIO | `intf_gpio.h` | ✅ 完成 | GPIO 输入/输出/中断 |
| Clock | `intf_clock.h` | ✅ 完成 | PLL 配置、延迟函数 |
| ADC | `intf_adc.h` | ✅ 完成 | ADC16 双实例 (Oneshot/Period/PMT)，含中断回调 |
| UART | `intf_uart.h` | ❌ TODO | UART 通信 |
| SPI | `intf_spi.h` | ❌ TODO | SPI 通信 |
| I2C | `intf_i2c.h` | ❌ TODO | I2C 通信 |
| WS2812 | `intf_ws2812.h` | ✅ 完成 | WS2812 LED 驱动 |
| CAN | `intf_can.h` | ✅ 完成 | MCAN 驱动，支持 CAN 2.0 / CAN FD，中断 + 非阻塞收发 |

## HRPWM 驱动说明

> **重要**：HPM5361 的 `PWM_SOC_HRPWM_SUPPORT = 0`，不支持真正的亚时钟级 HRPWM。当前实现基于普通 `HPM_PWM0` API。

- **接口**：`intf_hrpwm.h` - 匿名结构体 ops、float duty [0.0-1.0]
- **驱动**：`drv_hrpwm.c` - 映射到 `HPM_PWM0/1`，支持四对互补输出
- **引脚**：PA24-PA31 → PWM0/PWM1 四对输出
- **功能**：频率/占空比动态调整、启停控制、变频后移相重放、运行态 89° 边界保护

### HRPWM 调试验证

- `App/Debug/Src/app_debug_rtt.c` 提供 `app_debug_hrpwm_run_tests()` 综合测试入口。
- 当前 `main.c` 默认调用的是 `app_debug_adc_pmt_run_tests()`；如需执行 HRPWM 综合验证，可切换到 `app_debug_hrpwm_run_tests()`。
- `app_debug_hrpwm_run_tests()` 覆盖以下场景：
  - 静态初始移相：start 前验证 `0~180°`
  - 运行态连续移相：限制并验证 `0~89°`
  - 带移相的变频重放验证
  - 占空比分辨率验证
- 运行态若请求 `>89°` 的连续移相，Driver 会返回 `-1` 进行边界保护。

详细设计文档请参考：
- `doc/hrpwm_driver_design.md`
- `doc/adc_driver_design.md`

## CAN 驱动说明

- **接口**：`intf_can.h` — C17 匿名结构体，14 函数指针覆盖 MCAN 全功能
- **驱动**：`drv_mcan.c` — 薄映射层，最大化复用 SDK（`mcan_get_default_config` / `mcan_init` / `mcan_parse_protocol_status`）
- **App 封装**：`app_can.c/h` — 中断驱动 + Ring Buffer + 非阻塞收发
- **引脚**：PA00→MCAN0_TXD, PA01→MCAN0_RXD
- **时钟**：PLL1_CLK0 ÷ 10 = 80MHz，由 `intf_clock_init()` 统一配置
- **功能**：经典 CAN 2.0 / CAN FD，ID+Mask 过滤器，总线状态查询，内部回环测试

### CAN 使用示例

```c
#include "app_can.h"

void on_msg(const app_can_msg_t *msg) { /* ISR 上下文 */ }

app_can_init();
app_can_set_rx_callback(on_msg);
app_can_add_filter(0x114, 0x7FF);       // 只接收 ID=0x114
app_can_send(0x114, data, 8);           // 非阻塞发送
app_can_msg_t msg;
if (app_can_receive(&msg) == 0) { ... } // 轮询接收
```

### CAN 调试验证

- `app_debug_can_loopback_test()` — 内部回环验证 MCAN 控制器
- `app_debug_can_run_tests()` — 周期性发送 + RTT 打印接收

## 添加新外设接口

1. **Interface**: 创建 `intf_xxx.h`，定义 `intf_xxx_ops_t` 和 `intf_xxx_register()`
2. **Interface**: 在 `intf_default.c` 中添加桩实现
3. **Driver**: 创建 `drv_xxx.c`，实现 ops 并调用 `intf_xxx_register()`
4. **Board**: 在 `pinmux.c` 中添加引脚定义
5. **App**: 在 `main.c` 中调用注册函数

## 可用板级

| 板级 | 说明 |
|------|------|
| `user_board` | 自定义板级模板 (默认) |
| `hpm5301evklite_board` | HPM5301 EVK Lite 官方开发板 |

```bash
make list-boards
```

## 许可证

BSD-3-Clause
