#ifndef APP_GPIO_H
#define APP_GPIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pin definitions (must match pinmux.c) */
#define PIN_COMM_1    ((0 << 5) | 2)   /* PA02 */
#define PIN_COMM_2    ((0 << 5) | 3)   /* PA03 */
#define PIN_DRVPWR    ((0 << 5) | 8)   /* PA08 */
#define PIN_BUTTON    ((0 << 5) | 9)   /* PA09 */

void app_gpio_init(void);
void app_gpio_set(uint16_t pin, uint8_t on);
void app_gpio_toggle(uint16_t pin);
uint8_t app_gpio_read(uint16_t pin);

#ifdef __cplusplus
}
#endif

#endif /* APP_GPIO_H */
