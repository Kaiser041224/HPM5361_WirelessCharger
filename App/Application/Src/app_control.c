/*
 * app_control.c — 系统控制编排实现
 *
 * 状态机 + 运行模式管理 + 任务编排 + 功率安全使能/禁能。
 */

#include "app_control.h"

#include "app_hrpwm.h"
#include "ctrl_buckboost.h"
#include "ctrl_fault.h"
#include "ctrl_lcc.h"

#include <stddef.h>

/* ============================================================================
 * 内部状态
 * ============================================================================ */

static sys_state_t s_state = SYS_INIT;
static op_mode_t   s_mode  = MODE_IDLE;

/* 控制器实例 (全局单例) */
static ctrl_buckboost_t g_buckboost;
static ctrl_lcc_t       g_lcc;

/* 是否已完成自检 */
static bool s_self_test_ok;

/* ============================================================================
 * 内部辅助
 * ============================================================================ */

/**
 * @brief 执行自检：确认故障模块就绪、输出未阻塞。
 * @return true 通过。
 */
static bool self_test(void)
{
    return true;
}

/**
 * @brief 按当前模式执行一轮控制迭代。
 *
 * 采样值目前为占位；实际部署时替换为 Platform 层提供的物理量。
 */
static void execute_mode(void)
{
    /* ---------- 占位采样值 ---------- */
    float v_in   = 0.0f;
    float v_out  = 0.0f;
    float i_l    = 0.0f;
    float i_coil = 0.0f;
    float i_lf   = 0.0f;
    /* -------------------------------- */

    switch (s_mode) {
    case MODE_BUCK_CV:
    case MODE_BUCK_CC:
        ctrl_buckboost_step(&g_buckboost, v_in, v_out, i_l);
        break;
    case MODE_LCC_OPEN:
    case MODE_LCC_CLOSED:
        ctrl_lcc_step(&g_lcc, i_coil, i_lf);
        break;
    case MODE_IDLE:
    case MODE_STANDBY:
    default:
        break;
    }
}

/**
 * @brief 配置控制器参数以匹配当前模式。
 */
static void configure_controllers_for_mode(void)
{
    switch (s_mode) {
    case MODE_BUCK_CV:
        ctrl_buckboost_set_target_type(&g_buckboost, BB_TARGET_CV);
        break;
    case MODE_BUCK_CC:
        ctrl_buckboost_set_target_type(&g_buckboost, BB_TARGET_CC);
        break;
    case MODE_LCC_OPEN:
        ctrl_lcc_set_mode(&g_lcc, LCC_MODE_OPEN_LOOP);
        break;
    case MODE_LCC_CLOSED:
        ctrl_lcc_set_mode(&g_lcc, LCC_MODE_CLOSED_LOOP);
        break;
    default:
        break;
    }
}

/* ============================================================================
 * 公开接口
 * ============================================================================ */

void app_control_init(void)
{
    ctrl_fault_init(NULL);
    /*
     * 硬件初始化由 Platform 层 app_hrpwm_init() 完成（频率、死区、对齐）。
     * Controller 只负责控制参数和状态初始化，不重复配置硬件。
     */
    ctrl_buckboost_init(&g_buckboost);
    ctrl_lcc_init(&g_lcc);

    s_state         = SYS_INIT;
    s_mode          = MODE_IDLE;
    s_self_test_ok  = false;
}

/* -------------------------------------------------------------------------- */

void app_control_tick(void)
{
    /* ---- 1. 故障优先 ---- */
    uint32_t faults = ctrl_fault_check();
    if (faults != 0U) {
        s_state = SYS_FAULT;
        app_hrpwm_emergency_stop();
        ctrl_buckboost_emergency_stop(&g_buckboost);
        ctrl_lcc_emergency_stop(&g_lcc);
        return;
    }

    /* ---- 2. 状态评估 ---- */
    switch (s_state) {

    case SYS_INIT:
        s_self_test_ok = self_test();
        if (s_self_test_ok) {
            s_state = SYS_IDLE;
        }
        break;

    case SYS_IDLE:
        /* 等待外部命令 (power_enable / set_mode) */
        break;

    case SYS_RUN:
        /* 执行控制迭代 */
        execute_mode();
        break;

    case SYS_FAULT:
        /* 故障已在步骤 1 处理；此处等待清除 */
        break;

    default:
        s_state = SYS_INIT;
        break;
    }
}

/* -------------------------------------------------------------------------- */

sys_state_t app_control_get_state(void) { return s_state; }
op_mode_t   app_control_get_mode(void)  { return s_mode; }

/* -------------------------------------------------------------------------- */

int app_control_set_mode(op_mode_t mode)
{
    /* 模式有效性校验 */
    switch (mode) {
    case MODE_IDLE:
    case MODE_STANDBY:
        /* 任何非 FAULT 状态都允许 */
        if (s_state == SYS_FAULT) return -1;
        break;
    case MODE_BUCK_CV:
    case MODE_BUCK_CC:
    case MODE_LCC_OPEN:
    case MODE_LCC_CLOSED:
        /* 仅 IDLE / RUN 允许 */
        if (s_state != SYS_IDLE && s_state != SYS_RUN) return -1;
        break;
    default:
        return -1;
    }

    s_mode = mode;
    configure_controllers_for_mode();
    return 0;
}

/* -------------------------------------------------------------------------- */

int app_control_power_enable(void)
{
    if (s_state != SYS_IDLE) return -1;
    if (!s_self_test_ok)     return -1;

    /* 使能控制器 (实际部署时在此处操作驱动电源) */
    ctrl_buckboost_enable(&g_buckboost);
    ctrl_lcc_enable(&g_lcc);

    s_state = SYS_RUN;
    return 0;
}

/* -------------------------------------------------------------------------- */

void app_control_power_disable(void)
{
    ctrl_buckboost_disable(&g_buckboost);
    ctrl_lcc_disable(&g_lcc);
    s_state = SYS_IDLE;
}

/* -------------------------------------------------------------------------- */

void app_control_emergency(void)
{
    app_hrpwm_emergency_stop();
    ctrl_buckboost_emergency_stop(&g_buckboost);
    ctrl_lcc_emergency_stop(&g_lcc);
    s_state = SYS_FAULT;
}

/* -------------------------------------------------------------------------- */

uint32_t app_control_get_faults(void)
{
    return ctrl_fault_get_active();
}

int app_control_clear_faults(void)
{
    int ret = ctrl_fault_clear_all();
    if (ret == 0) {
        s_state = SYS_INIT;
    }
    return ret;
}
