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
│   └── intf_default.c          # 注册分发实现
├── Driver/
│   └── hpm_impl/               # HPM SDK 适配
│       ├── drv_hrpwm.c         # HRPWM 驱动 (映射到 HPM_PWM0)
│       ├── drv_gpwm.c          # GPWM 驱动 (映射到 HPM_GPTMR0)
│       ├── drv_gpio.c          # GPIO 驱动
│       ├── drv_clock.c         # Clock 驱动
│       ├── drv_adc.c           # ADC 驱动
│       ├── drv_uart.c          # UART 驱动
│       ├── drv_spi.c           # SPI 驱动
│       ├── drv_i2c.c           # I2C 驱动
│       └── drv_ws2812.c        # WS2812 驱动
├── App/                        # 应用层
│   ├── main.c                  # 入口 (桥接初始化)
│   └── Logic/
│       ├── Inc/
│       │   ├── app_gpio.h
│       │   ├── app_buzzer.h
│       │   ├── app_ws2812.h
│       │   ├── app_hrpwm.h
│       │   └── app_debug_rtt.h
│       └── Src/
│           ├── app_gpio.c      # GPIO 业务逻辑
│           ├── app_buzzer.c    # 蜂鸣器业务逻辑
│           ├── app_ws2812.c    # WS2812 业务逻辑
│           ├── app_hrpwm.c     # HRPWM App 封装
│           └── app_debug_rtt.c # RTT 调试与HRPWM测试
├── doc/                        # 文档
│   ├── hrpwm_driver_design.md  # HRPWM 驱动设计文档
│   └── adc_driver_design.md    # ADC 驱动设计文档
│   ├── hpm_pwm_gptmr_pwm_guide.md  # HPM PWM/GPTMR 使用指南
│   └── todo.md                 # TODO 清单
├── linkers/                    # 链接脚本
└── output/                     # 编译输出
```

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

## HRPWM 驱动说明

> **重要**：HPM5361 的 `PWM_SOC_HRPWM_SUPPORT = 0`，不支持真正的亚时钟级 HRPWM。当前实现基于普通 `HPM_PWM0` API。

- **接口**：`intf_hrpwm.h` - 匿名结构体 ops、float duty [0.0-1.0]
- **驱动**：`drv_hrpwm.c` - 映射到 `HPM_PWM0/1`，支持四对互补输出
- **引脚**：PA24-PA31 → PWM0/PWM1 四对输出
- **功能**：频率/占空比动态调整、启停控制、变频后移相重放、运行态 89° 边界保护

### HRPWM 调试验证

- `App/Logic/Src/app_debug_rtt.c` 提供 `app_debug_hrpwm_run_tests()` 综合测试入口。
- `main.c` 默认调用该入口，覆盖以下场景：
  - 静态初始移相：start 前验证 `0~180°`
  - 运行态连续移相：限制并验证 `0~89°`
  - 带移相的变频重放验证
  - 占空比分辨率验证
- 运行态若请求 `>89°` 的连续移相，Driver 会返回 `-1` 进行边界保护。

详细设计文档请参考：
- `doc/hrpwm_driver_design.md`
- `doc/adc_driver_design.md`

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
