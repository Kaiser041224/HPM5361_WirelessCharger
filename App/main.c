#include "board.h"

#include "intf_clock.h"

#include "app_buzzer.h"
#include "app_debug_rtt.h"
#include "app_gpio.h"
#include "app_hrpwm.h"
#include "app_ws2812.h"

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, false);
    app_buzzer_init();
    app_ws2812_init();

    app_debug_init();
    app_debug_write("\r\n[RTT] HPM5361 WirelessCharger started\r\n");
    app_debug_write("[RTT] Initializing HRPWM...\r\n");
    pwm_init();
    app_debug_dump_hrpwm_cmp();
    app_debug_write("[RTT] PWM center-aligned output active on PA24-PA31\r\n");

    /* 使能PWM0和PWM1中心点中断 */
    app_debug_write("[RTT] Enabling PWM0 & PWM1 center IRQ...\r\n");
    app_debug_pwm_irq_enable(0);
    app_debug_pwm_irq_enable(1);

    /* 设置初始占空比50% */
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_set_duty(pair, 0.5f);
    }

    /* 等待2秒后开始测试 */
    app_debug_write("[RTT] Waiting 2s before test...\r\n");
    intf_clock_delay_ms(2000);

    /* 测试1: 变频测试 - PWM0从100kHz扫到300kHz再回到200kHz */
    app_debug_write("\r\n[RTT] === Test 1: PWM0 Frequency Sweep ===\r\n");
    app_debug_pwm_test_frequency_sweep(0, 100000, 300000, 50000, 1000);
    pwm_set_frequency(PWM_PAIR_0, 200000); /* 恢复200kHz */
    intf_clock_delay_ms(100);

    /* 测试2: 移相测试 - PWM0 Pair1相对于Pair0从0度扫到180度 */
    app_debug_write("\r\n[RTT] === Test 2: PWM0 Phase Sweep ===\r\n");
    app_debug_pwm_test_phase_sweep(0, 0, 1, 0.0f, 180.0f, 10.0f, 500);
    pwm_set_phase(0, 0, 1, 0.0f); /* 恢复0度移相 */
    intf_clock_delay_ms(100);

    /* 测试3: 移相测试 - PWM0 Pair1相对于Pair0从180度扫到0度 */
    app_debug_write("\r\n[RTT] === Test 3: PWM0 Phase Sweep Reverse ===\r\n");
    app_debug_pwm_test_phase_sweep(0, 0, 1, 180.0f, 0.0f, 10.0f, 500);
    pwm_set_phase(0, 0, 1, 0.0f); /* 恢复0度移相 */
    intf_clock_delay_ms(100);

    /* 测试4: 变频测试 - PWM1从100kHz扫到200kHz */
    app_debug_write("\r\n[RTT] === Test 4: PWM1 Frequency Sweep ===\r\n");
    app_debug_pwm_test_frequency_sweep(1, 100000, 200000, 20000, 1000);
    pwm_set_frequency(PWM_PAIR_2, 148000); /* 恢复148kHz */
    intf_clock_delay_ms(100);

    /* 测试5: 移相测试 - PWM1 Pair1相对于Pair0从0度扫到180度 */
    app_debug_write("\r\n[RTT] === Test 5: PWM1 Phase Sweep ===\r\n");
    app_debug_pwm_test_phase_sweep(1, 0, 1, 0.0f, 180.0f, 10.0f, 500);
    pwm_set_phase(1, 0, 1, 0.0f); /* 恢复0度移相 */
    intf_clock_delay_ms(100);

    /* 测试6: 占空比分辨率测试 - PWM0 Pair0，以0.01%步进扫描 */
    app_debug_write("\r\n[RTT] === Test 6: PWM0 Duty Resolution Test ===\r\n");
    app_debug_pwm_test_duty_resolution(0, 0, 0.45f, 0.55f, 0.0001f, 100);
    pwm_set_duty(PWM_PAIR_0, 0.5f); /* 恢复50%占空比 */
    intf_clock_delay_ms(100);

    /* 测试完成 */
    app_debug_write("\r\n[RTT] === All tests completed ===\r\n");
    app_debug_write("[RTT] Entering idle loop...\r\n");

    /* 空闲循环 */
    while (1) {
        intf_clock_delay_ms(1000);
        app_debug_pwm_irq_dump_status();
    }

    return 0;
}
