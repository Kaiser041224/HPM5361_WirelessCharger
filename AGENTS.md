# AGENTS.md - 嵌入式 C17 解耦架构开发指南 (完全体)

## 1. 核心设计哲学 (Core Philosophy)
本工程采用 **“物理隔离 + 契约驱动”** 模式。其核心目标是实现业务逻辑（App）与芯片外设（SDK/HAL）的深度解耦。
- **原子目标**：App 层代码与硬件外设深度解耦，仅通过 Interface 层访问。
- **现代化 C**：强制使用 **C17** 标准（匿名结构体、泛型选择 `_Generic`、静态断言 `static_assert`）。
- **高性能**：针对 HPM 系列 RISC-V 架构优化，强制执行 L1 Cache 对齐与 ILM (RAMFUNC) 部署。

---

## 2. 目录分层与依赖守则 (Strict Layering)

| 目录层级 | 职责 (Responsibility) | 允许包含的头文件 | 禁止项 (Hard Bans) |
| :--- | :--- | :--- | :--- |
| **1. App/** | 纯业务逻辑、控制算法、状态机 | `Interface/`, `Module/`, `<stdint.h>` | 禁止包含任何 `hpm_*.h` |
| **2. Module/** | 传感器/执行器对象（如电机驱动器、IMU） | `Interface/`, `platform_defs.h` | 禁止包含具体的 `Driver/` 实现文件 |
| **3. Interface/** | **契约定义层**。定义硬件抽象对象 (HAL) | 纯 C 标准库头文件 | 禁止包含私有变量、静态函数或驱动实现 |
| **4. Driver/** | SDK 适配实现。将物理寄存器操作映射至接口 | `hpm_sdk.h`, `board_pins.h`, `Interface/` | 禁止在此处编写业务逻辑或控制算法 |
| **5. Board/** | 硬件底表。定义原始引脚引脚 (IOCFG) 与参数 | `hpm_soc.h` (仅宏定义) | 禁止包含任何函数实现或逻辑 |

---

## 3. C17 编码与性能规范 (Coding Standards)

### 3.1 接口对象化 (Object-Oriented C17)
接口定义必须使用 **匿名结构体**。所有物理参数需在驱动层完成归一化（例如：占空比统一为 `float` [0.0-1.0]）。
```c
// 示例：Interface/intf_pwm.h
typedef struct {
    uint8_t instance_id;
    struct {
        status_t (*init)(void);
        status_t (*set_duty)(float duty); 
    }; // 匿名结构体：允许对象通过 dev->set_duty() 直接调用
} pwm_if_t;