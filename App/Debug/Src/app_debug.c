#include "app_debug.h"
#include "app_debug_adc.h"

void app_debug_init(void) { }

void app_debug_run_once(void)
{
    app_debug_adc_dump_pmt();
    app_debug_adc_dump_analog_signal();
}
