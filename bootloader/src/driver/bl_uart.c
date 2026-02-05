#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "bl_uart.h"

LOG_MODULE_REGISTER(uart, CONFIG_LOG_DEFAULT_LEVEL);

void serial_irq_handler(const struct device *dev, void *user_data);
static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));
static upgrade_rx_callback_t cb;

void bl_upgrade_callback_register(upgrade_rx_callback_t callback) {
    cb = callback;
}

void bl_upgrade_packet_send(uint8_t *data, uint32_t length)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("upgrade uart device not found!");
        return;
    }

    if (length == 0 || data == NULL) {
        LOG_ERR("invalid data or length for uart send");
        return;
    }

    // int ret = uart_tx(uart_dev, data, length, SYS_FOREVER_MS);
    for (uint32_t i = 0; i< length; i++)
    {
        uart_poll_out(uart_dev, data[i]);
    }
    LOG_DBG("response successfully %u bytes", length);
}

void bl_upgrade_uart_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("upgrade uart device not found!");
        return;
    }

    struct uart_config uart_cfg = {
        .baudrate = 115200,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    uart_configure(uart_dev, &uart_cfg);

    int ret = uart_irq_callback_user_data_set(uart_dev, serial_irq_handler, NULL);

    if (ret < 0) {
        if (ret == -ENOTSUP) {
            LOG_ERR("interrupt driven uart api support not enabled");
        } else if (ret == -ENOSYS) {
            LOG_ERR("uart device does not support interrupt-driven API");
        } else {
            LOG_ERR("setting uart callback: %d", ret);
        }
        return;
    }
    uart_irq_rx_enable(uart_dev);
    LOG_INF("uart initialized successfully");
}

void bl_upgrade_uart_deinit(void)
{
    const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));
    
    if (uart_dev != NULL && device_is_ready(uart_dev)) {
        uart_irq_rx_disable(uart_dev);
        uart_irq_tx_disable(uart_dev);
        
        uart_irq_callback_set(uart_dev, NULL);
        
        LOG_WRN("UART device deinitialized");
    }
}

void serial_irq_handler(const struct device *dev, void *user_data)
{
    if (!uart_irq_update(dev)) 
        return;

    if (uart_irq_rx_ready(dev))
    {
        uint8_t rx_data;
        // Read data from the serial port receiving register in a loop, read the data in the buffer, avoid data loss,
        // and prevent redundancy caused by interrupts entering the loop
        while (uart_fifo_read(dev, &rx_data, 1) == 1)
        {
            if (cb != NULL) {
                cb(rx_data);
            }
        }
    }
}

void disable_uart_peripherals(void)
{
    // USART1->CR1 &= ~USART_CR1_UE;
    // USART3->CR1 &= ~USART_CR1_UE;
}