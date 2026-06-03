/*
 * ADC Driver - HPM ADC16 hardware implementation
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "intf_adc.h"

#include "hpm_adc16_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_interrupt.h"
#include "hpm_soc_irq.h"

#include <stddef.h>

#define ADC_DEFAULT_VREF_MV       INTF_ADC_DEFAULT_VREF_MV
#define ADC_DEFAULT_SAMPLE_CYCLE  INTF_ADC_DEFAULT_SAMPLE_CYCLE
#define ADC_DEFAULT_CLOCK_DIV     INTF_ADC_DEFAULT_CLOCK_DIV
#define ADC_MAX_CLOCK_DIV         (16U)
#define ADC_MAX_CLOCK_HZ          (50000000U)
#define ADC_CONV_CYCLES           (25U)
#define ADC_MAX_CHANNELS          (16U)
#define ADC_PMT_MAX_TRIG          (11U)
#define ADC_SEQ_MAX_LEN           16U

/* ============================================================================
 * Instance State
 * ============================================================================ */

typedef struct {
    bool       configured;
    bool       running;
} adc_ch_state_t;

typedef struct {
    bool                  initialized;
    intf_adc_resolution_t resolution;
    intf_adc_mode_t       mode;
    float                 vref_mv;
    ADC16_Type           *base;
    uint32_t              irq;
    adc_ch_state_t        channels[ADC_MAX_CHANNELS];
    /* PMT */
    struct {
        uint8_t            trig_ch;
        uint8_t            ch_count;
        uint8_t            ch_list[4];
        intf_adc_pmt_cb_t  cb;
        void              *cb_user_data;
    } pmt;
    /* Sequence */
    struct {
        bool               hw_trig;
        uint8_t            ch_count;
        uint8_t            ch_list[ADC_SEQ_MAX_LEN];
        intf_adc_seq_cb_t  cb;
        void              *cb_user_data;
    } seq;
    /* DMA (shared by PMT and Seq) */
    struct {
        bool      active;
        uint32_t *buff;
        uint32_t  len;
    } dma;
    /* Watchdog */
    struct {
        bool               enabled[ADC_MAX_CHANNELS];
        intf_adc_wdog_cb_t cb;
        void              *cb_user_data;
    } wdog;
} adc_inst_t;

static adc_inst_t adc_instances[INTF_ADC_INSTANCE_COUNT];

/* ============================================================================
 * Hardware Mapping Helpers
 * ============================================================================ */

static ADC16_Type *adc_get_base(uint8_t inst)
{
    switch (inst) {
    case 0: return HPM_ADC0;
    case 1: return HPM_ADC1;
    default: return NULL;
    }
}

static clock_name_t adc_get_clock(uint8_t inst)
{
    switch (inst) {
    case 0: return clock_adc0;
    case 1: return clock_adc1;
    default: return (clock_name_t)0;
    }
}

static uint32_t adc_get_irq(uint8_t inst)
{
    switch (inst) {
    case 0: return IRQn_ADC0;
    case 1: return IRQn_ADC1;
    default: return 0;
    }
}

static adc16_resolution_t adc_map_resolution(intf_adc_resolution_t res)
{
    switch (res) {
    case INTF_ADC_RES_8_BITS:  return adc16_res_8_bits;
    case INTF_ADC_RES_10_BITS: return adc16_res_10_bits;
    case INTF_ADC_RES_12_BITS: return adc16_res_12_bits;
    case INTF_ADC_RES_16_BITS:
    default:                    return adc16_res_16_bits;
    }
}

static uint16_t adc_resolution_max_value(intf_adc_resolution_t res)
{
    switch (res) {
    case INTF_ADC_RES_8_BITS:  return (uint16_t)0xFF;
    case INTF_ADC_RES_10_BITS: return (uint16_t)0x3FF;
    case INTF_ADC_RES_12_BITS: return (uint16_t)0xFFF;
    case INTF_ADC_RES_16_BITS:
    default:                    return (uint16_t)0xFFFF;
    }
}

static adc16_conversion_mode_t adc_map_mode(intf_adc_mode_t mode)
{
    switch (mode) {
    case INTF_ADC_MODE_PERIOD:     return adc16_conv_mode_period;
    case INTF_ADC_MODE_PMT:        return adc16_conv_mode_preemption;
    case INTF_ADC_MODE_SEQ:        return adc16_conv_mode_sequence;
    case INTF_ADC_MODE_ONESHOT:
    default:                       return adc16_conv_mode_oneshot;
    }
}

static uint32_t adc_calc_clock_div(uint8_t inst, uint32_t sample_rate_hz, uint32_t user_div)
{
    if (user_div >= 1 && user_div <= ADC_MAX_CLOCK_DIV) {
        uint32_t bus_freq = clock_get_frequency(adc_get_clock(inst));
        uint32_t adc_clk = bus_freq / user_div;
        if (adc_clk > ADC_MAX_CLOCK_HZ) {
            /* enforce datasheet limit: ADC clock ≤ 50 MHz */
            return (bus_freq + ADC_MAX_CLOCK_HZ - 1) / ADC_MAX_CLOCK_HZ;
        }
        return user_div;
    }

    if (sample_rate_hz == 0) {
        return ADC_DEFAULT_CLOCK_DIV;
    }

    uint32_t bus_freq = clock_get_frequency(adc_get_clock(inst));
    uint32_t needed_clk = sample_rate_hz * ADC_CONV_CYCLES;
    uint32_t div = bus_freq / needed_clk;

    if (div < 1)  div = 1;
    if (div > ADC_MAX_CLOCK_DIV) div = ADC_MAX_CLOCK_DIV;

    /* enforce datasheet limit: ADC clock ≤ 50 MHz */
    uint32_t min_safe_div = (bus_freq + ADC_MAX_CLOCK_HZ - 1) / ADC_MAX_CLOCK_HZ;
    if (div < min_safe_div) div = min_safe_div;

    return div;
}

static void adc_enable_instance_irq(uint8_t inst)
{
    adc_inst_t *ai = &adc_instances[inst];
    uint32_t irq = ai->irq;
    if (irq != 0) {
        intc_m_enable_irq_with_priority(irq, 1);
    }
}

/* ============================================================================
 * ISR
 * ============================================================================ */

static void adc_generic_isr(uint8_t inst)
{
    adc_inst_t *ai = &adc_instances[inst];
    if (!ai->initialized) return;

    ADC16_Type *base = ai->base;
    uint32_t status = adc16_get_status_flags(base);
    adc16_clear_status_flags(base, status);

    /* PMT trigger complete */
    if (ADC16_INT_STS_TRIG_CMPT_GET(status) && ai->mode == INTF_ADC_MODE_PMT) {
        if (ai->pmt.cb && ai->pmt.ch_count > 0) {
            uint16_t values[4];
            uint8_t  valid = 0;
            for (uint8_t i = 0; i < ai->pmt.ch_count && i < 4; i++) {
                if (adc16_get_oneshot_result(base, ai->pmt.ch_list[i], &values[i]) == status_success) {
                    valid++;
                }
            }
            if (valid > 0) {
                ai->pmt.cb(INTF_ADC_CH(inst, ai->pmt.trig_ch), values, valid, ai->pmt.cb_user_data);
            }
        }
    }

    /* Sequence single conversion complete */
    if (ADC16_INT_STS_SEQ_CVC_GET(status) && ai->mode == INTF_ADC_MODE_SEQ) {
        if (ai->seq.cb) {
            ai->seq.cb(INTF_ADC_CH(inst, 0), ai->seq.cb_user_data);
        }
    }

    /* Sequence full queue complete */
    if (ADC16_INT_STS_SEQ_CMPT_GET(status) && ai->mode == INTF_ADC_MODE_SEQ) {
        if (ai->seq.cb) {
            ai->seq.cb(INTF_ADC_CH(inst, 0), ai->seq.cb_user_data);
        }
    }

    /* Watchdog threshold violation */
    uint32_t wdog_status = ADC16_INT_STS_WDOG_GET(status);
    if (wdog_status) {
        for (uint8_t ch = 0; ch < ADC_MAX_CHANNELS; ch++) {
            if ((wdog_status & (1u << ch)) && ai->wdog.enabled[ch]) {
                uint16_t val;
                if (adc16_get_oneshot_result(base, ch, &val) == status_success) {
                    if (ai->wdog.cb) {
                        ai->wdog.cb(INTF_ADC_CH(inst, ch), val, ai->wdog.cb_user_data);
                    }
                }
                adc16_disable_interrupts(base, (uint32_t)(1u << ch));
            }
        }
    }
}

SDK_DECLARE_EXT_ISR_M(IRQn_ADC0, isr_adc0)
void isr_adc0(void)
{
    adc_generic_isr(0);
}

SDK_DECLARE_EXT_ISR_M(IRQn_ADC1, isr_adc1)
void isr_adc1(void)
{
    adc_generic_isr(1);
}

/* ============================================================================
 * WDOG re-enable helper (public for App manual re-arm)
 * ============================================================================ */

void adc_wdog_reenable(uint8_t inst, uint8_t ch)
{
    if (inst >= INTF_ADC_INSTANCE_COUNT || ch >= ADC_MAX_CHANNELS) return;
    adc_inst_t *ai = &adc_instances[inst];
    if (!ai->initialized || !ai->wdog.enabled[ch]) return;
    adc16_enable_interrupts(ai->base, (uint32_t)(1u << ch));
}

/* ============================================================================
 * HPM ADC16 Implementation
 * ============================================================================ */

static int adc_init(intf_adc_ch_t ch, const intf_adc_cfg_t *cfg)
{
    uint8_t inst   = INTF_ADC_CH_INST(ch);
    uint8_t ch_idx = INTF_ADC_CH_IDX(ch);

    if (inst >= INTF_ADC_INSTANCE_COUNT || cfg == NULL) return -1;
    if (cfg->mode != INTF_ADC_MODE_PMT && cfg->mode != INTF_ADC_MODE_SEQ
        && ch_idx >= ADC_MAX_CHANNELS) return -1;

    adc_inst_t *ai = &adc_instances[inst];
    uint32_t sample_cycle = (cfg->sample_cycle > 0) ? cfg->sample_cycle : ADC_DEFAULT_SAMPLE_CYCLE;

    /* --- global instance init (first call) --- */
    if (!ai->initialized) {
        ADC16_Type *base = adc_get_base(inst);
        if (base == NULL) return -1;

        adc16_config_t adc_cfg;
        adc16_get_default_config(&adc_cfg);

        adc_cfg.res        = adc_map_resolution(cfg->resolution);
        adc_cfg.conv_mode  = adc_map_mode(cfg->mode);
        adc_cfg.adc_clk_div = (adc16_clock_divider_t)adc_calc_clock_div(inst, cfg->sample_rate_hz, cfg->clock_div);
        adc_cfg.wait_dis   = false;   /* blocking mode for multi-channel oneshot */

        if (adc_cfg.conv_mode == adc16_conv_mode_oneshot) {
            adc_cfg.sel_sync_ahb = true;
            adc_cfg.adc_ahb_en   = false;
        } else {
            adc_cfg.adc_ahb_en   = true;
        }

        if (adc16_init(base, &adc_cfg) != status_success) return -1;

        base->ANA_CTRL0 |= ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;

        ai->initialized = true;
        ai->resolution  = cfg->resolution;
        ai->mode        = cfg->mode;
        ai->base        = base;
        ai->irq         = adc_get_irq(inst);
        ai->vref_mv     = (cfg->vref_mv > 0.0f) ? cfg->vref_mv : ADC_DEFAULT_VREF_MV;
    } else {
        if (ai->resolution != cfg->resolution || ai->mode != cfg->mode) return -1;
    }

    /* DMA hardware support: only PMT and Sequence modes have DMA engines.
     * Oneshot/Period modes must read results via CPU (BUS_RESULT/PRD_RESULT). */
    if (cfg->dma_en && ai->mode != INTF_ADC_MODE_PMT && ai->mode != INTF_ADC_MODE_SEQ) {
        return -1;
    }

    /* --- per-mode init --- */

    if (ai->mode == INTF_ADC_MODE_PMT) {
        if (cfg->pmt_ch_count == 0 || cfg->pmt_ch_count > 4
            || cfg->pmt_trig_ch >= ADC_PMT_MAX_TRIG) return -1;

        ai->pmt.trig_ch      = cfg->pmt_trig_ch;
        ai->pmt.ch_count     = cfg->pmt_ch_count;
        ai->pmt.cb           = cfg->pmt_cb;
        ai->pmt.cb_user_data = cfg->pmt_cb_user_data;

        for (uint8_t i = 0; i < cfg->pmt_ch_count; i++) {
            ai->pmt.ch_list[i] = cfg->pmt_ch_list[i];
        }

        adc16_channel_config_t ch_cfg;
        adc16_get_channel_default_config(&ch_cfg);
        ch_cfg.sample_cycle = sample_cycle;

        for (uint8_t i = 0; i < cfg->pmt_ch_count; i++) {
            ch_cfg.ch = cfg->pmt_ch_list[i];
            if (adc16_init_channel(ai->base, &ch_cfg) != status_success) return -1;
            ai->channels[cfg->pmt_ch_list[i]].configured = true;
        }

        adc16_pmt_config_t pmt_cfg;
        pmt_cfg.trig_ch  = cfg->pmt_trig_ch;
        pmt_cfg.trig_len = cfg->pmt_ch_count;
        for (uint8_t i = 0; i < cfg->pmt_ch_count; i++) {
            pmt_cfg.adc_ch[i] = cfg->pmt_ch_list[i];
            pmt_cfg.inten[i]  = ((i + 1) == cfg->pmt_ch_count);
        }

        if (adc16_set_pmt_config(ai->base, &pmt_cfg) != status_success) return -1;
        adc16_enable_pmt_queue(ai->base, cfg->pmt_trig_ch);
        adc16_enable_interrupts(ai->base, adc16_event_trig_complete);

        if (cfg->dma_en && cfg->dma_buff != NULL) {
            ai->dma.active = true;
            ai->dma.buff   = cfg->dma_buff;
            ai->dma.len    = cfg->dma_buff_len;
            adc16_init_pmt_dma(ai->base, (uint32_t)cfg->dma_buff);
        }

        adc_enable_instance_irq(inst);
        return 0;
    }

    if (ai->mode == INTF_ADC_MODE_SEQ) {
        if (cfg->seq_ch_count == 0 || cfg->seq_ch_count > ADC_SEQ_MAX_LEN) return -1;
        if (cfg->dma_en && (cfg->dma_buff == NULL || cfg->dma_buff_len == 0)) return -1;

        ai->seq.hw_trig      = cfg->seq_hw_trig;
        ai->seq.ch_count     = cfg->seq_ch_count;
        ai->seq.cb           = cfg->seq_cb;
        ai->seq.cb_user_data = cfg->seq_cb_user_data;

        for (uint8_t i = 0; i < cfg->seq_ch_count; i++) {
            ai->seq.ch_list[i] = cfg->seq_ch_list[i];
        }

        adc16_channel_config_t ch_cfg;
        adc16_get_channel_default_config(&ch_cfg);
        ch_cfg.sample_cycle = sample_cycle;

        for (uint8_t i = 0; i < cfg->seq_ch_count; i++) {
            ch_cfg.ch = cfg->seq_ch_list[i];
            if (adc16_init_channel(ai->base, &ch_cfg) != status_success) return -1;
            ai->channels[cfg->seq_ch_list[i]].configured = true;
        }

        adc16_seq_config_t seq_cfg;
        seq_cfg.seq_len    = cfg->seq_ch_count;
        seq_cfg.restart_en = false;
        seq_cfg.cont_en    = true;
        seq_cfg.hw_trig_en = cfg->seq_hw_trig;
        seq_cfg.sw_trig_en = !cfg->seq_hw_trig;

        for (uint8_t i = 0; i < cfg->seq_ch_count; i++) {
            seq_cfg.queue[i].seq_int_en = ((i + 1) == cfg->seq_ch_count);
            seq_cfg.queue[i].ch         = cfg->seq_ch_list[i];
        }

        if (adc16_set_seq_config(ai->base, &seq_cfg) != status_success) return -1;

        if (cfg->dma_en) {
            ai->dma.active = true;
            ai->dma.buff   = cfg->dma_buff;
            ai->dma.len    = cfg->dma_buff_len;

            adc16_dma_config_t dma_cfg;
            dma_cfg.start_addr         = cfg->dma_buff;
            dma_cfg.buff_len_in_4bytes = cfg->dma_buff_len;
            dma_cfg.stop_en            = false;
            dma_cfg.stop_pos           = 0;

            if (adc16_init_seq_dma(ai->base, &dma_cfg) != status_success) return -1;
        }

        adc16_enable_interrupts(ai->base,
            adc16_event_seq_single_complete | adc16_event_seq_full_complete);
        adc_enable_instance_irq(inst);
        return 0;
    }

    /* Oneshot / Period */
    adc16_channel_config_t ch_cfg;
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch           = ch_idx;
    ch_cfg.sample_cycle = sample_cycle;

    if (cfg->wdog_en) {
        ch_cfg.wdog_int_en = true;
        ch_cfg.thshdh      = cfg->wdog_thshd_high;
        ch_cfg.thshdl      = cfg->wdog_thshd_low;
    }

    if (adc16_init_channel(ai->base, &ch_cfg) != status_success) return -1;

    if (ai->mode == INTF_ADC_MODE_ONESHOT) {
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
        adc16_enable_oneshot_mode(ai->base);
#endif
    }

    ai->channels[ch_idx].configured = true;
    ai->channels[ch_idx].running    = false;

    if (cfg->wdog_en) {
        ai->wdog.enabled[ch_idx] = true;
        ai->wdog.cb              = cfg->wdog_cb;
        ai->wdog.cb_user_data    = cfg->wdog_cb_user_data;
        adc16_enable_interrupts(ai->base, (uint32_t)(1u << ch_idx));
        adc_enable_instance_irq(inst);
    }

    return 0;
}

static int adc_read(intf_adc_ch_t ch, uint16_t *value)
{
    uint8_t inst   = INTF_ADC_CH_INST(ch);
    uint8_t ch_idx = INTF_ADC_CH_IDX(ch);

    if (inst >= INTF_ADC_INSTANCE_COUNT || ch_idx >= ADC_MAX_CHANNELS || value == NULL) return -1;

    adc_inst_t *ai = &adc_instances[inst];
    if (!ai->initialized || !ai->channels[ch_idx].configured) return -1;

    hpm_stat_t stat;
    switch (ai->mode) {
    case INTF_ADC_MODE_PERIOD:
        stat = adc16_get_prd_result(ai->base, ch_idx, value);
        break;
    case INTF_ADC_MODE_PMT:
    case INTF_ADC_MODE_ONESHOT:
    default:
        stat = adc16_get_oneshot_result(ai->base, ch_idx, value);
        break;
    }

    return (stat == status_success) ? 0 : -1;
}

static int adc_read_voltage(intf_adc_ch_t ch, float *voltage_mv)
{
    uint16_t raw;
    if (adc_read(ch, &raw) != 0) return -1;

    uint8_t inst = INTF_ADC_CH_INST(ch);
    if (inst >= INTF_ADC_INSTANCE_COUNT || !adc_instances[inst].initialized || voltage_mv == NULL) return -1;

    intf_adc_resolution_t res = adc_instances[inst].resolution;
    uint16_t max_val = adc_resolution_max_value(res);
    float vref = adc_instances[inst].vref_mv;
    *voltage_mv = (float)raw * vref / (float)max_val;

    return 0;
}

static int adc_start(intf_adc_ch_t ch)
{
    uint8_t inst   = INTF_ADC_CH_INST(ch);
    uint8_t ch_idx = INTF_ADC_CH_IDX(ch);

    if (inst >= INTF_ADC_INSTANCE_COUNT || ch_idx >= ADC_MAX_CHANNELS) return -1;

    adc_inst_t *ai = &adc_instances[inst];
    if (!ai->initialized || !ai->channels[ch_idx].configured) return -1;

    if (ai->mode == INTF_ADC_MODE_SEQ) {
        if (ai->seq.hw_trig) {
            adc16_seq_enable_hw_trigger(ai->base);
        } else {
            adc16_trigger_seq_by_sw(ai->base);
        }
        return 0;
    }

    if (ai->mode == INTF_ADC_MODE_PMT || ai->mode == INTF_ADC_MODE_ONESHOT) {
        ai->channels[ch_idx].running = true;
        return 0;
    }

    adc16_prd_config_t prd_cfg;
    prd_cfg.ch           = ch_idx;
    prd_cfg.prescale     = 20;
    prd_cfg.period_count = 5;
    if (adc16_set_prd_config(ai->base, &prd_cfg) != status_success) return -1;

    ai->channels[ch_idx].running = true;
    return 0;
}

static int adc_stop(intf_adc_ch_t ch)
{
    uint8_t inst   = INTF_ADC_CH_INST(ch);
    uint8_t ch_idx = INTF_ADC_CH_IDX(ch);

    if (inst >= INTF_ADC_INSTANCE_COUNT || ch_idx >= ADC_MAX_CHANNELS) return -1;

    adc_inst_t *ai = &adc_instances[inst];
    if (!ai->initialized) return -1;

    if (ai->mode == INTF_ADC_MODE_SEQ) {
        adc16_seq_disable_hw_trigger(ai->base);
        return 0;
    }

    ai->channels[ch_idx].running = false;
    return 0;
}

static void adc_set_vref(float vref_mv)
{
    for (uint8_t inst = 0; inst < INTF_ADC_INSTANCE_COUNT; inst++) {
        adc_instances[inst].vref_mv = (vref_mv > 0.0f) ? vref_mv : ADC_DEFAULT_VREF_MV;
    }
}

static int adc_calibrate(void)
{
    int ret = 0;
    for (uint8_t inst = 0; inst < INTF_ADC_INSTANCE_COUNT; inst++) {
        adc_inst_t *ai = &adc_instances[inst];
        if (!ai->initialized || ai->base == NULL) continue;

        adc16_config_t adc_cfg;
        adc16_get_default_config(&adc_cfg);
        adc_cfg.res        = adc_map_resolution(ai->resolution);
        adc_cfg.conv_mode  = adc_map_mode(ai->mode);
        adc_cfg.adc_clk_div = (adc16_clock_divider_t)ADC_DEFAULT_CLOCK_DIV;
        adc_cfg.wait_dis   = false;

        if (adc_cfg.conv_mode == adc16_conv_mode_oneshot) {
            adc_cfg.sel_sync_ahb = true;
            adc_cfg.adc_ahb_en   = false;
        } else {
            adc_cfg.adc_ahb_en   = true;
        }

        if (adc16_init(ai->base, &adc_cfg) != status_success) {
            ret = -1;
        } else {
            ai->base->ANA_CTRL0 |= ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;
        }
    }
    return ret;
}

static void adc_deinit(intf_adc_ch_t ch)
{
    uint8_t inst   = INTF_ADC_CH_INST(ch);
    uint8_t ch_idx = INTF_ADC_CH_IDX(ch);

    if (inst >= INTF_ADC_INSTANCE_COUNT || ch_idx >= ADC_MAX_CHANNELS) return;

    adc_inst_t *ai = &adc_instances[inst];
    if (!ai->initialized) return;

    ai->channels[ch_idx].configured = false;
    ai->channels[ch_idx].running    = false;
    ai->wdog.enabled[ch_idx]        = false;
}

/* ============================================================================
 * Operations Structures & Registration
 * ============================================================================ */

static const intf_adc_t adc_ops_adc0 = {
    .instance_id  = 0,
    .init         = adc_init,
    .read         = adc_read,
    .read_voltage = adc_read_voltage,
    .start        = adc_start,
    .stop         = adc_stop,
    .set_vref     = adc_set_vref,
    .calibrate    = adc_calibrate,
    .deinit       = adc_deinit,
};

static const intf_adc_t adc_ops_adc1 = {
    .instance_id  = 1,
    .init         = adc_init,
    .read         = adc_read,
    .read_voltage = adc_read_voltage,
    .start        = adc_start,
    .stop         = adc_stop,
    .set_vref     = adc_set_vref,
    .calibrate    = adc_calibrate,
    .deinit       = adc_deinit,
};

void hpm_adc_driver_register(void)
{
    intf_adc_register(&adc_ops_adc0);
    intf_adc_register(&adc_ops_adc1);
}
