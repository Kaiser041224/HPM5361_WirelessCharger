#include "intf_pwm.h"
#include "intf_hrpwm.h"
#include "intf_gpwm.h"
#include "intf_adc.h"
#include "intf_uart.h"
#include "intf_spi.h"
#include "intf_i2c.h"
#include "intf_gpio.h"
#include "intf_ws2812.h"

#include <stddef.h>

/* ============================================================================
 * PWM Interface
 * ============================================================================ */

static const intf_pwm_ops_t *pwm_ops = NULL;

int intf_pwm_register(const intf_pwm_ops_t *ops)
{
    if (ops == NULL) return -1;
    pwm_ops = ops;
    return 0;
}

int intf_pwm_init(intf_pwm_ch_t ch, const intf_pwm_cfg_t *cfg)
{
    if (pwm_ops && pwm_ops->init) return pwm_ops->init(ch, cfg);
    return -1;
}

int intf_pwm_set_duty(intf_pwm_ch_t ch, uint8_t duty_percent)
{
    if (pwm_ops && pwm_ops->set_duty) return pwm_ops->set_duty(ch, duty_percent);
    return -1;
}

int intf_pwm_set_frequency(intf_pwm_ch_t ch, uint32_t freq_hz)
{
    if (pwm_ops && pwm_ops->set_frequency) return pwm_ops->set_frequency(ch, freq_hz);
    return -1;
}

int intf_pwm_start(intf_pwm_ch_t ch)
{
    if (pwm_ops && pwm_ops->start) return pwm_ops->start(ch);
    return -1;
}

int intf_pwm_stop(intf_pwm_ch_t ch)
{
    if (pwm_ops && pwm_ops->stop) return pwm_ops->stop(ch);
    return -1;
}

/* ============================================================================
 * HRPWM Interface
 * ============================================================================ */

static const intf_hrpwm_t *hrpwm_ops = NULL;

int intf_hrpwm_register(const intf_hrpwm_t *ops)
{
    if (ops == NULL) return -1;
    hrpwm_ops = ops;
    return 0;
}

int intf_hrpwm_init(intf_hrpwm_ch_t ch, const intf_hrpwm_cfg_t *cfg)
{
    if (hrpwm_ops && hrpwm_ops->init) return hrpwm_ops->init(ch, cfg);
    return -1;
}

int intf_hrpwm_set_duty(intf_hrpwm_ch_t ch, float duty)
{
    if (hrpwm_ops && hrpwm_ops->set_duty) return hrpwm_ops->set_duty(ch, duty);
    return -1;
}

int intf_hrpwm_set_frequency(intf_hrpwm_ch_t ch, uint32_t frequency_hz)
{
    if (hrpwm_ops && hrpwm_ops->set_frequency) return hrpwm_ops->set_frequency(ch, frequency_hz);
    return -1;
}

int intf_hrpwm_start(intf_hrpwm_ch_t ch)
{
    if (hrpwm_ops && hrpwm_ops->start) return hrpwm_ops->start(ch);
    return -1;
}

int intf_hrpwm_stop(intf_hrpwm_ch_t ch)
{
    if (hrpwm_ops && hrpwm_ops->stop) return hrpwm_ops->stop(ch);
    return -1;
}

/* ============================================================================
 * GPWM Interface
 * ============================================================================ */

static const intf_gpwm_t *gpwm_ops = NULL;

int intf_gpwm_register(const intf_gpwm_t *ops)
{
    if (ops == NULL) return -1;
    gpwm_ops = ops;
    return 0;
}

int intf_gpwm_init(intf_gpwm_ch_t ch, const intf_gpwm_cfg_t *cfg)
{
    if (gpwm_ops && gpwm_ops->init) return gpwm_ops->init(ch, cfg);
    return -1;
}

int intf_gpwm_set_duty(intf_gpwm_ch_t ch, float duty)
{
    if (gpwm_ops && gpwm_ops->set_duty) return gpwm_ops->set_duty(ch, duty);
    return -1;
}

int intf_gpwm_set_frequency(intf_gpwm_ch_t ch, uint32_t frequency_hz)
{
    if (gpwm_ops && gpwm_ops->set_frequency) return gpwm_ops->set_frequency(ch, frequency_hz);
    return -1;
}

int intf_gpwm_start(intf_gpwm_ch_t ch)
{
    if (gpwm_ops && gpwm_ops->start) return gpwm_ops->start(ch);
    return -1;
}

int intf_gpwm_stop(intf_gpwm_ch_t ch)
{
    if (gpwm_ops && gpwm_ops->stop) return gpwm_ops->stop(ch);
    return -1;
}

int intf_gpwm_force_low(intf_gpwm_ch_t ch)
{
    if (gpwm_ops && gpwm_ops->force_low) return gpwm_ops->force_low(ch);
    return -1;
}

int intf_gpwm_force_release(intf_gpwm_ch_t ch)
{
    if (gpwm_ops && gpwm_ops->force_release) return gpwm_ops->force_release(ch);
    return -1;
}

int intf_gpwm_capture_init(intf_gpwm_ch_t ch, const intf_gpwm_capture_cfg_t *cfg)
{
    if (gpwm_ops && gpwm_ops->capture_init) return gpwm_ops->capture_init(ch, cfg);
    return -1;
}

int intf_gpwm_capture_start(intf_gpwm_ch_t ch)
{
    if (gpwm_ops && gpwm_ops->capture_start) return gpwm_ops->capture_start(ch);
    return -1;
}

int intf_gpwm_capture_stop(intf_gpwm_ch_t ch)
{
    if (gpwm_ops && gpwm_ops->capture_stop) return gpwm_ops->capture_stop(ch);
    return -1;
}

int intf_gpwm_capture_poll(intf_gpwm_ch_t ch, intf_gpwm_capture_t *capture)
{
    if (gpwm_ops && gpwm_ops->capture_poll) return gpwm_ops->capture_poll(ch, capture);
    return -1;
}

/* ============================================================================
 * ADC Interface
 * ============================================================================ */

static const intf_adc_ops_t *adc_ops = NULL;

int intf_adc_register(const intf_adc_ops_t *ops)
{
    if (ops == NULL) return -1;
    adc_ops = ops;
    return 0;
}

int intf_adc_init(intf_adc_ch_t ch, const intf_adc_cfg_t *cfg)
{
    if (adc_ops && adc_ops->init) return adc_ops->init(ch, cfg);
    return -1;
}

int intf_adc_read(intf_adc_ch_t ch, uint16_t *value)
{
    if (adc_ops && adc_ops->read) return adc_ops->read(ch, value);
    return -1;
}

int intf_adc_read_voltage(intf_adc_ch_t ch, float *voltage_mv)
{
    if (adc_ops && adc_ops->read_voltage) return adc_ops->read_voltage(ch, voltage_mv);
    return -1;
}

int intf_adc_start(intf_adc_ch_t ch)
{
    if (adc_ops && adc_ops->start) return adc_ops->start(ch);
    return -1;
}

int intf_adc_stop(intf_adc_ch_t ch)
{
    if (adc_ops && adc_ops->stop) return adc_ops->stop(ch);
    return -1;
}

/* ============================================================================
 * UART Interface
 * ============================================================================ */

static const intf_uart_ops_t *uart_ops = NULL;

int intf_uart_register(const intf_uart_ops_t *ops)
{
    if (ops == NULL) return -1;
    uart_ops = ops;
    return 0;
}

int intf_uart_init(intf_uart_port_t port, const intf_uart_cfg_t *cfg)
{
    if (uart_ops && uart_ops->init) return uart_ops->init(port, cfg);
    return -1;
}

int intf_uart_transmit(intf_uart_port_t port, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (uart_ops && uart_ops->transmit) return uart_ops->transmit(port, data, len, timeout_ms);
    return -1;
}

int intf_uart_receive(intf_uart_port_t port, uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (uart_ops && uart_ops->receive) return uart_ops->receive(port, data, len, timeout_ms);
    return -1;
}

int intf_uart_register_rx_callback(intf_uart_port_t port, intf_uart_rx_cb_t cb)
{
    if (uart_ops && uart_ops->register_rx_callback) return uart_ops->register_rx_callback(port, cb);
    return -1;
}

/* ============================================================================
 * SPI Interface
 * ============================================================================ */

static const intf_spi_ops_t *spi_ops = NULL;

int intf_spi_register(const intf_spi_ops_t *ops)
{
    if (ops == NULL) return -1;
    spi_ops = ops;
    return 0;
}

int intf_spi_init(intf_spi_port_t port, const intf_spi_cfg_t *cfg)
{
    if (spi_ops && spi_ops->init) return spi_ops->init(port, cfg);
    return -1;
}

int intf_spi_transfer(intf_spi_port_t port, const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms)
{
    if (spi_ops && spi_ops->transfer) return spi_ops->transfer(port, tx, rx, len, timeout_ms);
    return -1;
}

int intf_spi_transmit(intf_spi_port_t port, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (spi_ops && spi_ops->transmit) return spi_ops->transmit(port, data, len, timeout_ms);
    return -1;
}

int intf_spi_receive(intf_spi_port_t port, uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (spi_ops && spi_ops->receive) return spi_ops->receive(port, data, len, timeout_ms);
    return -1;
}

/* ============================================================================
 * I2C Interface
 * ============================================================================ */

static const intf_i2c_ops_t *i2c_ops = NULL;

int intf_i2c_register(const intf_i2c_ops_t *ops)
{
    if (ops == NULL) return -1;
    i2c_ops = ops;
    return 0;
}

int intf_i2c_init(intf_i2c_port_t port, const intf_i2c_cfg_t *cfg)
{
    if (i2c_ops && i2c_ops->init) return i2c_ops->init(port, cfg);
    return -1;
}

int intf_i2c_master_transmit(intf_i2c_port_t port, uint16_t addr, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (i2c_ops && i2c_ops->master_transmit) return i2c_ops->master_transmit(port, addr, data, len, timeout_ms);
    return -1;
}

int intf_i2c_master_receive(intf_i2c_port_t port, uint16_t addr, uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (i2c_ops && i2c_ops->master_receive) return i2c_ops->master_receive(port, addr, data, len, timeout_ms);
    return -1;
}

int intf_i2c_write_reg(intf_i2c_port_t port, uint16_t addr, uint16_t reg, uint8_t reg_size, const uint8_t *data, size_t len)
{
    if (i2c_ops && i2c_ops->write_reg) return i2c_ops->write_reg(port, addr, reg, reg_size, data, len);
    return -1;
}

int intf_i2c_read_reg(intf_i2c_port_t port, uint16_t addr, uint16_t reg, uint8_t reg_size, uint8_t *data, size_t len)
{
    if (i2c_ops && i2c_ops->read_reg) return i2c_ops->read_reg(port, addr, reg, reg_size, data, len);
    return -1;
}

/* ============================================================================
 * GPIO Interface
 * ============================================================================ */

static const intf_gpio_t *gpio_ops = NULL;

int intf_gpio_register(const intf_gpio_t *ops)
{
    if (ops == NULL) return -1;
    gpio_ops = ops;
    return 0;
}

int intf_gpio_init(const intf_gpio_cfg_t *cfg)
{
    if (gpio_ops && gpio_ops->init) return gpio_ops->init(cfg);
    return -1;
}

int intf_gpio_set_level(intf_gpio_pin_t pin, intf_gpio_level_t level)
{
    if (gpio_ops && gpio_ops->set_level) return gpio_ops->set_level(pin, level);
    return -1;
}

int intf_gpio_get_level(intf_gpio_pin_t pin, intf_gpio_level_t *level)
{
    if (gpio_ops && gpio_ops->get_level) return gpio_ops->get_level(pin, level);
    return -1;
}

int intf_gpio_toggle(intf_gpio_pin_t pin)
{
    if (gpio_ops && gpio_ops->toggle) return gpio_ops->toggle(pin);
    return -1;
}

/* ============================================================================
 * WS2812 Interface
 * ============================================================================ */

static const intf_ws2812_ops_t *ws2812_ops = NULL;

int intf_ws2812_register(const intf_ws2812_ops_t *ops)
{
    if (ops == NULL) return -1;
    ws2812_ops = ops;
    return 0;
}

int intf_ws2812_init(const intf_ws2812_cfg_t *cfg)
{
    if (ws2812_ops && ws2812_ops->init) return ws2812_ops->init(cfg);
    return -1;
}

int intf_ws2812_set_pixel(intf_ws2812_pixel_t index, intf_ws2812_rgb_t color)
{
    if (ws2812_ops && ws2812_ops->set_pixel) return ws2812_ops->set_pixel(index, color);
    return -1;
}

int intf_ws2812_set_pixels(const intf_ws2812_rgb_t *colors, uint32_t count)
{
    if (ws2812_ops && ws2812_ops->set_pixels) return ws2812_ops->set_pixels(colors, count);
    return -1;
}

int intf_ws2812_update(bool blocking)
{
    if (ws2812_ops && ws2812_ops->update) return ws2812_ops->update(blocking);
    return -1;
}

int intf_ws2812_clear(void)
{
    if (ws2812_ops && ws2812_ops->clear) return ws2812_ops->clear();
    return -1;
}

bool intf_ws2812_is_busy(void)
{
    if (ws2812_ops && ws2812_ops->is_busy) return ws2812_ops->is_busy();
    return false;
}

void intf_ws2812_deinit(void)
{
    if (ws2812_ops && ws2812_ops->deinit) ws2812_ops->deinit();
}
