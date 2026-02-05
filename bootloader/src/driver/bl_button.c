#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>
#include "bl_led.h"
#include "bl_button.h"

LOG_MODULE_REGISTER(button, CONFIG_LOG_DEFAULT_LEVEL);

#define SLEEP_TIME_MS	1

K_SEM_DEFINE(button_trap, 0, 1);

#define KEY0_NODE	DT_ALIAS(key0)
#if !DT_NODE_HAS_STATUS_OKAY(KEY0_NODE)
#error "Unsupported board: key0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(KEY0_NODE, gpios, {0});
static struct gpio_callback button_cb_data;

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{

    k_sem_give(&button_trap);
    LOG_INF("button pressed trap into bootloader");
}

void  bl_button_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&button)) 
    {
        LOG_ERR("button device %s is not ready", button.port->name);
        return;
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0) 
    {
        LOG_ERR("failed to configure %s pin %d",
               button.port->name, button.pin);
        return;
    }

    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) 
    {
        LOG_ERR("failed to configure interrupt on %s pin %d",
                button.port->name, button.pin);
        return;
    }
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    LOG_INF("button initialized successfully");
}

void disable_gpio_interrupts(void)
{
    if (gpio_is_ready_dt(&button)) 
    {
        return;
    }

    gpio_remove_callback(button.port, &button_cb_data);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_DISABLE);
}