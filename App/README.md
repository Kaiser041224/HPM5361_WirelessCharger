# App 目录说明

`App/` 层用于组织应用级代码，当前划分为以下子层：

- `Application/`：应用入口、业务编排、运行时调度。
- `Control/`：控制状态机、闭环控制、保护逻辑。
- `Algorithm/`：纯算法库，尽量不依赖 `app_*` 或 `hpm_*`。
- `Platform/`：应用级平台能力封装，如 `app_adc`、`app_hrpwm`、`app_can`、`app_sampling_sync`。
- `Debug/`：RTT 输出、调试测试入口、验证辅助代码。

当前 `main.c` 仍保留在 `App/` 根目录，作为系统启动与应用桥接入口。
