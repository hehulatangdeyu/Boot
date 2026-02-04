#ifndef __BL_UART_H
#define __BL_UART_H

typedef void (*upgrade_rx_callback_t) (uint8_t data);
void bl_upgrade_uart_init(void);
void bl_upgrade_uart_deinit(void);
void bl_upgrade_callback_register(upgrade_rx_callback_t callback);
void bl_upgrade_packet_send(uint8_t *data, uint32_t length);
void disable_uart_peripherals(void);

#endif 