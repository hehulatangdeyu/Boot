#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>
#include "bl_uart.h"

#define STACK_SIZE 2048
#define UART_TEMP_BUF 64

K_SEM_DEFINE(pkt_sem, 0, 1);
K_SEM_DEFINE(rx_data_sem, 0, 1);

RING_BUF_DECLARE(uart_ringbuf, 512);

extern bool bl_received_handler(uint8_t data);
extern void bl_print_log(void);
extern bool bl_pkt_handler(void);
extern void bl_pkt_reset(void);

static void upgrade_callback_handler(uint8_t data)
{
    if (ring_buf_put(&uart_ringbuf, &data, sizeof(uint8_t)) > 0) {
        k_sem_give(&rx_data_sem);
    }
}

void upgrade_rx_thread(void *p1, void *p2, void *p3)
{
    static bool timer_flag = false;
    uint16_t read_len = 0;
    uint8_t buf[UART_TEMP_BUF] = {0};

    bl_upgrade_callback_register(upgrade_callback_handler); //register callbacks
    while (1)
    {
        k_sem_take(&rx_data_sem, K_FOREVER);

        if (timer_flag == false)
        {
            timer_flag = true;
        }

        while ((read_len = ring_buf_get(&uart_ringbuf, buf, sizeof(buf))) > 0) {
            for (uint16_t i = 0; i < read_len; i++) {
                bool packet_finish = bl_received_handler(buf[i]);
                if (packet_finish) {
                    // bl_print_log();
                    k_sem_give(&pkt_sem);
                }
            }
        }
    }
}

void packet_rx_thread(void *p1, void *p2, void *p3)
{
    while (1)
    {
        k_sem_take(&pkt_sem, K_FOREVER);
        bl_pkt_handler();
        bl_pkt_reset();
    }
}

#define PACKET_RX_THREAD_PRIORITY   4
#define UPGRADE_RX_THREAD_PRIORITY  5

K_THREAD_DEFINE(packet_thread_id, 4096, packet_rx_thread, NULL, NULL, NULL,
                PACKET_RX_THREAD_PRIORITY, 0, 0);

K_THREAD_DEFINE(one_data_thread_id, STACK_SIZE, upgrade_rx_thread, NULL, NULL, NULL,
                UPGRADE_RX_THREAD_PRIORITY, 0, 0);