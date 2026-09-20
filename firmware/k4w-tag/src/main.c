/* Bench diagnostic: no radio TX, no flash/UICR writes, no BLE yet. */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/byteorder.h>

/* Locate with nm, inspect via SWD. Phase 3 means SPI reads are running;
 * it is not a claim that radio initialization or ranging has succeeded. */
volatile struct {
    uint32_t magic;
    uint32_t phase;
    int32_t error;
    uint32_t radio_id;
    uint32_t reads;
} tag_diag = { .magic = 0x42524447 };

static const struct spi_dt_spec radio =
    SPI_DT_SPEC_GET(DT_NODELABEL(uwb), SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec reset =
    GPIO_DT_SPEC_GET(DT_NODELABEL(uwb), reset_gpios);

int main(void)
{
    tag_diag.phase = 1;
    if (!spi_is_ready_dt(&radio) || !gpio_is_ready_dt(&reset)) {
        tag_diag.error = -ENODEV;
        return 0;
    }
    /* Assert active-low reset, then RELEASE it; do not drive RSTn high. */
    int rc = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_ACTIVE);
    if (rc) { tag_diag.error = rc; return 0; }
    k_msleep(2);
    rc = gpio_pin_configure_dt(&reset, GPIO_INPUT);
    if (rc) { tag_diag.error = rc; return 0; }
    k_msleep(10);
    tag_diag.phase = 2;
    while (true) {
        uint8_t tx[5] = {0}; /* Fast-access read, register file 0, offset 0. */
        uint8_t rx[5] = {0};
        struct spi_buf tx_buf = {.buf = tx, .len = sizeof(tx)};
        struct spi_buf rx_buf = {.buf = rx, .len = sizeof(rx)};
        struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
        struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
        rc = spi_transceive_dt(&radio, &tx_set, &rx_set);
        tag_diag.error = rc;
        if (!rc) {
            tag_diag.radio_id = sys_get_le32(rx + 1);
            tag_diag.reads++;
        }
        tag_diag.phase = 3;
        k_msleep(1000);
    }
}
