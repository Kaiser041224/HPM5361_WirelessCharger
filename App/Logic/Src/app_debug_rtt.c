/*
 * Debug RTT - SEGGER RTT wrapper for debug output
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_debug_rtt.h"

#include "board.h"
#include "hpm_pwm_drv.h"
#include "intf_hrpwm.h"
#include "SEGGER_RTT.h"

#include <stdarg.h>
#include <stdio.h>

void app_debug_init(void)
{
}

void app_debug_write(const char *str)
{
    SEGGER_RTT_WriteString(0, str);
}

int app_debug_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    SEGGER_RTT_WriteString(0, buf);
    return len;
}

#define APP_DBG_CMP_START_INDEX(pwm_index) ((uint8_t)((pwm_index) * 2U))

typedef struct {
    PWM_Type *base;
    const char *name;
    uint8_t pwm_index;
} app_dbg_hrpwm_probe_t;

static const app_dbg_hrpwm_probe_t app_dbg_hrpwm_probes[] = {
    {.base = BOARD_APP_HRPWM0, .name = "PWM0_PAIR0", .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR0_OUT},
    {.base = BOARD_APP_HRPWM0, .name = "PWM0_PAIR1", .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR1_OUT},
    {.base = BOARD_APP_HRPWM1, .name = "PWM1_PAIR0", .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR0_OUT},
    {.base = BOARD_APP_HRPWM1, .name = "PWM1_PAIR1", .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR1_OUT},
};

static const char *app_dbg_detect_alignment(uint32_t reload, uint32_t cmp_begin, uint32_t cmp_end)
{
    if (cmp_end == reload) {
        return "EDGE-like";
    }

    if ((cmp_begin + cmp_end == reload) || (cmp_begin + cmp_end + 1U == reload)) {
        return "CENTER-like";
    }

    return "UNKNOWN";
}

void app_debug_dump_hrpwm_cmp(void)
{
    app_debug_write("[RTT] HRPWM compare register snapshot\r\n");

    for (size_t i = 0; i < sizeof(app_dbg_hrpwm_probes) / sizeof(app_dbg_hrpwm_probes[0]); i++) {
        const app_dbg_hrpwm_probe_t *probe = &app_dbg_hrpwm_probes[i];
        uint8_t cmp_start = APP_DBG_CMP_START_INDEX(probe->pwm_index);
        uint32_t reload = pwm_get_reload_val(probe->base);
        uint32_t cmp_begin = pwm_cmp_get_cmp_value(probe->base, cmp_start);
        uint32_t cmp_end = pwm_cmp_get_cmp_value(probe->base, cmp_start + 1U);
        const char *align = app_dbg_detect_alignment(reload, cmp_begin, cmp_end);

        app_debug_printf("[RTT] %s: reload=%lu cmp[%u]=%lu cmp[%u]=%lu => %s\r\n",
                         probe->name,
                         (unsigned long)reload,
                         (unsigned int)cmp_start,
                         (unsigned long)cmp_begin,
                         (unsigned int)(cmp_start + 1U),
                         (unsigned long)cmp_end,
                         align);
    }
}

/* ============================================================================
 * PWM中断测试 + 用户回调链
 * ============================================================================ */

#define PWM_IRQ_INSTANCE_COUNT (2U)

/* 中断测试变量 */
static volatile uint32_t pwm_irq_count[PWM_IRQ_INSTANCE_COUNT] = {0};
static volatile bool pwm_irq_enabled[PWM_IRQ_INSTANCE_COUNT] = {0};

/* 用户回调函数指针 */
static pwm_irq_user_callback_t pwm_user_callback[PWM_IRQ_INSTANCE_COUNT] = {NULL};

/* PWM0中心点中断回调（内部，包含计数 + 用户回调） */
static void pwm0_center_irq_callback(void)
{
    pwm_irq_count[0]++;
    
    /* 调用用户注册的业务回调 */
    if (pwm_user_callback[0] != NULL) {
        pwm_user_callback[0]();
    }
}

/* PWM1中心点中断回调（内部，包含计数 + 用户回调） */
static void pwm1_center_irq_callback(void)
{
    pwm_irq_count[1]++;
    
    /* 调用用户注册的业务回调 */
    if (pwm_user_callback[1] != NULL) {
        pwm_user_callback[1]();
    }
}

int app_debug_pwm_irq_register_callback(uint8_t inst, pwm_irq_user_callback_t callback)
{
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return -1;
    }
    pwm_user_callback[inst] = callback;
    app_debug_printf("[IRQ] PWM%d user callback registered\r\n", inst);
    return 0;
}

void app_debug_pwm_irq_unregister_callback(uint8_t inst)
{
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return;
    }
    pwm_user_callback[inst] = NULL;
    app_debug_printf("[IRQ] PWM%d user callback unregistered\r\n", inst);
}

void app_debug_pwm_irq_enable(uint8_t inst)
{
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return;
    }

    int ret;
    if (inst == 0) {
        ret = intf_hrpwm_config_reload_irq(0, pwm0_center_irq_callback);
        if (ret == 0) {
            ret = intf_hrpwm_enable_reload_irq(0);
        }
    } else {
        ret = intf_hrpwm_config_reload_irq(1, pwm1_center_irq_callback);
        if (ret == 0) {
            ret = intf_hrpwm_enable_reload_irq(1);
        }
    }

    if (ret == 0) {
        pwm_irq_enabled[inst] = true;
        pwm_irq_count[inst] = 0;
        app_debug_printf("[IRQ] PWM%d center IRQ enabled\r\n", inst);
    } else {
        app_debug_printf("[IRQ] PWM%d center IRQ enable FAILED\r\n", inst);
    }
}

void app_debug_pwm_irq_disable(uint8_t inst)
{
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return;
    }

    if (inst == 0) {
        intf_hrpwm_disable_reload_irq(0);
    } else {
        intf_hrpwm_disable_reload_irq(1);
    }

    pwm_irq_enabled[inst] = false;
    app_debug_printf("[IRQ] PWM%d center IRQ disabled, count=%lu\r\n", 
                     inst, (unsigned long)pwm_irq_count[inst]);
}

uint32_t app_debug_pwm_irq_get_count(uint8_t inst)
{
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return 0;
    }
    return pwm_irq_count[inst];
}

void app_debug_pwm_irq_reset_count(uint8_t inst)
{
    if (inst < PWM_IRQ_INSTANCE_COUNT) {
        pwm_irq_count[inst] = 0;
    }
}

void app_debug_pwm_irq_dump_status(void)
{
    app_debug_write("[IRQ] PWM IRQ Status:\r\n");
    for (uint8_t i = 0; i < PWM_IRQ_INSTANCE_COUNT; i++) {
        app_debug_printf("[IRQ]   PWM%d: %s, count=%lu\r\n", 
                         i, 
                         pwm_irq_enabled[i] ? "enabled" : "disabled",
                         (unsigned long)pwm_irq_count[i]);
    }
}
