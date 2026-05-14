#ifndef INTF_CLOCK_H
#define INTF_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void intf_clock_init(void);
uint32_t intf_clock_get_cpu_freq(void);
uint32_t intf_clock_get_ahb_freq(void);

#ifdef __cplusplus
}
#endif

#endif /* INTF_CLOCK_H */
