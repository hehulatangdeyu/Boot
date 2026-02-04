#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    k_sleep(K_MSEC(5000));
}
