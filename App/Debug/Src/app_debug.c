#include "app_debug.h"

#include "app_debug_internal.h"

void app_debug_init(void)
{
    app_debug_can_loopback_test();
    app_debug_adc_pmt_init();
}

void app_debug_run_once(void)
{
    app_debug_adc_pmt_run_tests();
    app_debug_can_run_tests();
}
