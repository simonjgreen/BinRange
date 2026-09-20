/* One polling owner of the DW3110; its IRQ is not connected to a handler. */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include "dw3000_device_api.h"
#include "radio_port.h"

volatile int radio_port_error;
static const spi_dt_spec radio =
    SPI_DT_SPEC_GET(DT_NODELABEL(uwb), SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const gpio_dt_spec reset = GPIO_DT_SPEC_GET(DT_NODELABEL(uwb), reset_gpios);

int radio_port_init() {
    if (!spi_is_ready_dt(&radio) || !gpio_is_ready_dt(&reset)) return -ENODEV;
    int rc = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_ACTIVE);
    if (rc) return rc;
    k_msleep(2);
    rc = gpio_pin_configure_dt(&reset, GPIO_INPUT);
    k_msleep(10);
    return rc;
}

static int checked(int rc) {
    if (rc) {
        radio_port_error = rc;
        /* The vendor driver discards SPI return codes. Fail closed instead
         * of letting it use partial data or continue a radio transaction. */
        k_panic();
    }
    return rc;
}

int writetospi(uint16_t hn, uint8_t *h, uint16_t bn, uint8_t *b) {
    spi_buf buffers[] = {{h, hn}, {b, bn}};
    spi_buf_set tx = {buffers, bn ? 2u : 1u};
    return checked(spi_write_dt(&radio, &tx));
}
int writetospiwithcrc(uint16_t hn, const uint8_t *h, uint16_t bn,
                     const uint8_t *b, uint8_t crc) {
    spi_buf buffers[] = {{const_cast<uint8_t *>(h), hn},
                         {const_cast<uint8_t *>(b), bn}, {&crc, 1}};
    spi_buf_set tx = {buffers, 3};
    return checked(spi_write_dt(&radio, &tx));
}
int readfromspi(uint16_t hn, uint8_t *h, uint16_t bn, uint8_t *b) {
    spi_buf tb[] = {{h, hn}, {nullptr, bn}};
    spi_buf rb[] = {{nullptr, hn}, {b, bn}};
    spi_buf_set tx = {tb, 2}, rx = {rb, 2};
    return checked(spi_transceive_dt(&radio, &tx, &rx));
}
void deca_sleep(uint8_t ms) { k_msleep(ms); }
void deca_usleep(uint8_t us) { k_busy_wait(us); }
/* The only caller is the main thread. No radio ISR or second radio owner:
 * leave the MCU's SPI-completion interrupt enabled during driver operations. */
decaIrqStatus_t decamutexon() { return 0; }
void decamutexoff(decaIrqStatus_t) {}
void wakeup_device_with_io() {
    checked(gpio_pin_configure_dt(&radio.config.cs.gpio, GPIO_OUTPUT_INACTIVE));
    checked(gpio_pin_set_dt(&radio.config.cs.gpio, 1));
    k_busy_wait(600);
    checked(gpio_pin_set_dt(&radio.config.cs.gpio, 0));
    k_msleep(5);
}
