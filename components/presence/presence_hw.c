#include "presence_hw.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "presence_hw";
static bool s_installed;

fd_err_t presence_hw_init(int rx_pin, int tx_pin)
{
    if (s_installed) presence_hw_deinit();

    const uart_config_t cfg = {
        .baud_rate  = PRESENCE_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(PRESENCE_UART_NUM, 1024, 0, 0, NULL, 0) != ESP_OK)
        return FD_ERR_NO_MEM;
    s_installed = true;
    if (uart_param_config(PRESENCE_UART_NUM, &cfg) != ESP_OK ||
        uart_set_pin(PRESENCE_UART_NUM, tx_pin, rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        presence_hw_deinit();
        return FD_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "UART%d %d baud, RX=GPIO%d TX=GPIO%d",
             PRESENCE_UART_NUM, PRESENCE_BAUD, rx_pin, tx_pin);
    return FD_OK;
}

void presence_hw_deinit(void)
{
    if (s_installed) {
        uart_driver_delete(PRESENCE_UART_NUM);
        s_installed = false;
    }
}

int presence_hw_read(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    if (!s_installed || !buf || max == 0) return 0;
    int n = uart_read_bytes(PRESENCE_UART_NUM, buf, max, pdMS_TO_TICKS(timeout_ms));
    return n < 0 ? 0 : n;
}
