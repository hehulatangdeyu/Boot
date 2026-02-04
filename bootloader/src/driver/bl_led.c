#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *leds = DEVICE_DT_GET(DT_NODELABEL(users_led));
static char *led_label[] = {DT_FOREACH_CHILD_SEP_VARGS(DT_NODELABEL(users_led), DT_PROP_OR, (,), label, NULL)};


void bl_led_init(void)
{
    if (!device_is_ready(leds)) 
    {
        LOG_ERR("led device %s is not ready", leds->name);
        return;
    }
    LOG_INF("led initialized successfully on %d", ARRAY_SIZE(led_label));
}

void bl_led_on(uint32_t led_num)
{
    if (led_num >= ARRAY_SIZE(led_label)) 
    {
        LOG_ERR("led number %d is out of range", led_num);
        return;
    }

    int ret = led_on(leds, led_num);
    if (ret < 0) 
    {
        LOG_ERR("failed to turn on led %s, error code: %d", led_label[led_num], ret);
    }
}

void bl_led_off(uint32_t led_num)
{
    if (led_num >= ARRAY_SIZE(led_label)) 
    {
        LOG_ERR("led number %d is out of range", led_num);
        return;
    }

    int ret = led_off(leds, led_num);
    if (ret < 0) 
    {
        LOG_ERR("failed to turn on led %s, error code: %d", led_label[led_num], ret);
    }
}