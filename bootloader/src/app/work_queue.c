#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include "bl_uart.h"

#define STACK_SIZE 2048
#define UART_THREAD_PRIORITY 5

K_MSGQ_DEFINE(uart_msgq, sizeof(uint8_t), 256, sizeof(char));

K_SEM_DEFINE(pkt_sem, 0, 1);

extern bool bl_received_handler(uint8_t data);
extern void bl_print_log(void);
extern bool bl_pkt_handler(void);
extern void bl_pkt_reset(void);

static void upgrade_callback_handler(uint8_t data)
{
    k_msgq_put(&uart_msgq, &data, K_NO_WAIT);
}

void upgrade_rx_onebyte_thread(void *p1, void *p2, void *p3)
{
    uint8_t rx_data;
    static bool timer_flag = false;
    bl_upgrade_callback_register(upgrade_callback_handler); //register callbacks
    while (1)
    {
        k_msgq_get(&uart_msgq, &rx_data, K_FOREVER);

        if (timer_flag == false)
        {
            timer_flag = true;
        }

        bool ret = bl_received_handler(rx_data);
        if (ret)
        {
            // bl_print_log();
            k_sem_give(&pkt_sem); // Notice packet received
        }
    }
}

void packet_received_handler(void *p1, void *p2, void *p3)
{
    while (1)
    {
        k_sem_take(&pkt_sem, K_FOREVER);
        bl_pkt_handler();
        bl_pkt_reset();
    }
}

K_THREAD_DEFINE(packet_thread_id, 4096, packet_received_handler, NULL, NULL, NULL,
                UART_THREAD_PRIORITY, 0, 0);

K_THREAD_DEFINE(one_data_thread_id, STACK_SIZE, upgrade_rx_onebyte_thread, NULL, NULL, NULL,
                UART_THREAD_PRIORITY, 0, 0);