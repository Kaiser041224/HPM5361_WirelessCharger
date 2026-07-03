#include "app_control.h"
#include "app_debug_rtt.h"
#include "app_entry.h"
#include "intf_adc.h"
#include "intf_clock.h"

/* [TEMP DIAG] GPTMR1 CH0(电压环,全局ch=4)/CH1(功率环,全局ch=5) 实际触发计数，
 * 定义于 Driver/hpm_impl/drv_gptmr.c，核验 main_loop 饿死是否由 GPTMR 触发频率
 * 异常引起。定位完成后应移除。 */
extern volatile uint32_t g_gptmr_cb_count[16];

/* [TEMP DIAG] ISR 累计 cycle，用于精确算 CPU 占用率(增量/墙钟增量) */
extern volatile uint64_t g_adc_isr_total_cycles[2];
extern volatile uint64_t g_gptmr_isr_total_cycles;

/* [TEMP DIAG] 嵌套感知的真实中断占用(墙钟 cycle，物理 ≤100%，无重复计入) */
extern volatile uint64_t g_irq_busy_cycles;

int main(void) {
    app_init();

    app_debug_printf("\r\n[MAIN] HPM5361 WirelessCharger started\r\n");

    uint32_t last_print_ms = 0;
    uint32_t loop_cnt = 0;
    uint32_t last_loop_cnt = 0;
    uint32_t last_gptmr_voltage_cnt = 0; /* [TEMP DIAG] */
    uint32_t last_gptmr_power_cnt = 0;   /* [TEMP DIAG] */
    uint32_t last_adc0_irq_cnt = 0;      /* [TEMP DIAG] ADC0(LCC,设计148kHz) 真实触发频率核验 */
    uint32_t last_adc1_irq_cnt = 0;      /* [TEMP DIAG] ADC1(buckboost,设计200kHz) 真实触发频率核验 */
    /* [TEMP DIAG] 单轮循环体内部耗时分布：区分"整体系统性变慢"和"极少数轮次
     * 异常耗时拖长窗口"两种可能——窗口拉长倍数(1.1~1.75x)与循环次数骤降倍数
     * (30~70x)不匹配，暗示可能是离群值而非均匀变慢。取每窗口内最大值。 */
    uint32_t max_run_once_cycles = 0;
    uint32_t max_delay_cycles = 0;
    uint32_t max_iter_cycles = 0;
    /* [TEMP DIAG] [LOOP] 打印本身(print2)的耗时只能在打印完成后才知道，
     * 故跨轮传递到下一次打印时一起报告。 */
    uint32_t last_print2_cycles = 0;
    /* [TEMP DIAG] CPU 占用率核算：上一窗口的 ISR 累计 cycle 和墙钟基准 */
    uint64_t last_adc0_isr_cycles = 0;
    uint64_t last_adc1_isr_cycles = 0;
    uint64_t last_gptmr_isr_cycles = 0;
    uint64_t last_irq_busy_cycles = 0;
    uint32_t last_wall_cycle = 0;

    while (1) {
        uint32_t t_iter_start = intf_clock_get_cycle(); /* [TEMP DIAG] */
        app_run_once();
        uint32_t t_after_run_once = intf_clock_get_cycle(); /* [TEMP DIAG] */
        loop_cnt++;

        /* [TEMP DIAG] */
        uint32_t run_once_cycles = t_after_run_once - t_iter_start;
        if (run_once_cycles > max_run_once_cycles) {
            max_run_once_cycles = run_once_cycles;
        }

        uint32_t now_ms = intf_clock_get_cycle() / (intf_clock_get_cpu_freq() / 1000U);
        if (now_ms - last_print_ms >= 250U) {
            /* [TEMP DIAG] 用真实经过的 ms 数换算 Hz，而非硬编码假设间隔恰好为 250ms。
             * 主循环严重饿死时，本次检查可能被推迟远超 250ms 才执行到，之前用固定
             * *4U(即假设 1000ms/250ms)换算会把"检查被推迟"误读成"GPTMR 超频"。 */
            uint32_t elapsed_ms = now_ms - last_print_ms;
            last_print_ms = now_ms;

            /* [TEMP DIAG] 阶段拆分计时：定位打印判断代码块内部具体哪一段吃时间 */
            uint32_t t_snap_start = intf_clock_get_cycle();

            intf_adc_diag_snapshot_t diag;
            intf_adc_get_diag_snapshot(&diag);

            uint32_t t_after_snap = intf_clock_get_cycle(); /* [TEMP DIAG] */
            // ctrl_diag_t ctrl_diag = g_ctrl_diag;

            uint32_t loops_per_250ms = loop_cnt - last_loop_cnt;
            last_loop_cnt = loop_cnt;

            /* [TEMP DIAG] GPTMR1 实际触发次数，按真实 elapsed_ms 换算 Hz */
            uint32_t gptmr_voltage_cnt = g_gptmr_cb_count[4];
            uint32_t gptmr_power_cnt = g_gptmr_cb_count[5];
            uint32_t gptmr_voltage_hz =
                (gptmr_voltage_cnt - last_gptmr_voltage_cnt) * 1000U / elapsed_ms;
            uint32_t gptmr_power_hz =
                (gptmr_power_cnt - last_gptmr_power_cnt) * 1000U / elapsed_ms;
            last_gptmr_voltage_cnt = gptmr_voltage_cnt;
            last_gptmr_power_cnt = gptmr_power_cnt;

            /* [TEMP DIAG] ADC0/ADC1 真实 IRQ 触发频率，核验是否远超设计值
             * (148kHz/200kHz)——GPTMR 已排除后，本次核验中断风暴是否来自 ADC。 */
            uint32_t adc0_irq_cnt = diag.irq_entry[0];
            uint32_t adc1_irq_cnt = diag.irq_entry[1];
            uint32_t adc0_irq_hz = (adc0_irq_cnt - last_adc0_irq_cnt) * 1000U / elapsed_ms;
            uint32_t adc1_irq_hz = (adc1_irq_cnt - last_adc1_irq_cnt) * 1000U / elapsed_ms;
            last_adc0_irq_cnt = adc0_irq_cnt;
            last_adc1_irq_cnt = adc1_irq_cnt;

            uint32_t t_after_calc = intf_clock_get_cycle(); /* [TEMP DIAG] */

            app_debug_printf(
                "[ISR] full=%lu/%lu | main_loop=%lu/%lums | cpu_freq=%luHz | GPTMR v=%luHz "
                "p=%luHz | ADC irq0=%luHz irq1=%luHz | ADC1 miss: cycle=%lu trig=%lu ch=%lu "
                "invalid=%lu\r\n",
                (unsigned long)diag.isr_cycles_max[0],
                (unsigned long)diag.isr_cycles_max[1], (unsigned long)loops_per_250ms,
                (unsigned long)elapsed_ms, (unsigned long)intf_clock_get_cpu_freq(),
                (unsigned long)gptmr_voltage_hz, (unsigned long)gptmr_power_hz,
                (unsigned long)adc0_irq_hz, (unsigned long)adc1_irq_hz,
                (unsigned long)diag.pmt_invalid_cycle[1], (unsigned long)diag.pmt_invalid_trig[1],
                (unsigned long)diag.pmt_invalid_channel[1], (unsigned long)diag.pmt_invalid[1]);

            uint32_t t_after_print1 = intf_clock_get_cycle(); /* [TEMP DIAG] */

            /* [TEMP DIAG] 单轮循环体最坏耗时(cycles)，换算 us 便于阅读；
             * @480MHz 1us=480cycles。核验是否只是极少数轮次异常拖长窗口。
             * print2 是上一次[LOOP]打印本身的耗时(本次才能报告)。 */
            app_debug_printf(
                "[LOOP] max run_once=%luus delay=%luus iter=%luus (target 1000us) | "
                "stage snap=%luus calc=%luus print1=%luus print2(prev)=%luus\r\n",
                (unsigned long)(max_run_once_cycles / 480U),
                (unsigned long)(max_delay_cycles / 480U),
                (unsigned long)(max_iter_cycles / 480U),
                (unsigned long)((t_after_snap - t_snap_start) / 480U),
                (unsigned long)((t_after_calc - t_after_snap) / 480U),
                (unsigned long)((t_after_print1 - t_after_calc) / 480U),
                (unsigned long)(last_print2_cycles / 480U));
            last_print2_cycles = intf_clock_get_cycle() - t_after_print1; /* [TEMP DIAG] */
            max_run_once_cycles = 0;
            max_delay_cycles = 0;
            max_iter_cycles = 0;

            /* [TEMP DIAG] 真实 CPU 占用率：本窗口内各 ISR 累计执行 cycle 增量，
             * 除以墙钟 cycle 增量。ADC0/ADC1 分开打印，定位过载来自哪个实例。 */
            uint32_t wall_now = intf_clock_get_cycle();
            uint32_t wall_delta = wall_now - last_wall_cycle;
            uint64_t adc0_delta = g_adc_isr_total_cycles[0] - last_adc0_isr_cycles;
            uint64_t adc1_delta = g_adc_isr_total_cycles[1] - last_adc1_isr_cycles;
            uint64_t gptmr_delta = g_gptmr_isr_total_cycles - last_gptmr_isr_cycles;
            uint64_t busy_delta = g_irq_busy_cycles - last_irq_busy_cycles;
            last_wall_cycle = wall_now;
            last_adc0_isr_cycles = g_adc_isr_total_cycles[0];
            last_adc1_isr_cycles = g_adc_isr_total_cycles[1];
            last_gptmr_isr_cycles = g_gptmr_isr_total_cycles;
            last_irq_busy_cycles = g_irq_busy_cycles;
            if (wall_delta > 0U) {
                app_debug_printf(
                    "[CPU] adc0=%lu%% adc1=%lu%% gptmr=%lu%% sum=%lu%% | REAL=%lu%% (wall=%luus)\r\n",
                    (unsigned long)(adc0_delta * 100U / wall_delta),
                    (unsigned long)(adc1_delta * 100U / wall_delta),
                    (unsigned long)(gptmr_delta * 100U / wall_delta),
                    (unsigned long)((adc0_delta + adc1_delta + gptmr_delta) * 100U / wall_delta),
                    (unsigned long)(busy_delta * 100U / wall_delta),
                    (unsigned long)(wall_delta / 480U));
            }
            // app_debug_printf(
            //     "[PWR] raw vin=%u iin=%u il=%u vlink=%u | phy vin=%.3f iin=%.3f il=%.3f "
            //     "vlink=%.3f\r\n",
            //     (unsigned)ctrl_diag.raw_adc.v_in, (unsigned)ctrl_diag.raw_adc.i_in,
            //     (unsigned)ctrl_diag.raw_adc.i_l, (unsigned)ctrl_diag.raw_adc.v_link,
            //     ctrl_diag.raw.v_in_v, ctrl_diag.raw.i_in_a, ctrl_diag.raw.i_l_a,
            //     ctrl_diag.raw.v_link_v);
            // app_debug_printf(
            //     "[PWR] filt vin=%.3f iin_adc=%.3f iin_calc=%.3f pin_calc=%.3fW "
            //     "target=%.3fW pid=%.3f\r\n",
            //     ctrl_diag.filt.v_in_v, ctrl_diag.filt.i_in_a, ctrl_diag.ff.i_in_calc_a,
            //     ctrl_diag.ff.p_in_w, ctrl_diag.ff.p_target_w, ctrl_diag.ff.power_pid_out);
            intf_adc_reset_diag_max();
        }

        uint32_t t_before_delay = intf_clock_get_cycle(); /* [TEMP DIAG] */
        intf_clock_delay_ms(1);
        uint32_t t_after_delay = intf_clock_get_cycle(); /* [TEMP DIAG] */

        /* [TEMP DIAG] */
        uint32_t delay_cycles = t_after_delay - t_before_delay;
        if (delay_cycles > max_delay_cycles) {
            max_delay_cycles = delay_cycles;
        }
        uint32_t iter_cycles = t_after_delay - t_iter_start;
        if (iter_cycles > max_iter_cycles) {
            max_iter_cycles = iter_cycles;
        }
    }
}
