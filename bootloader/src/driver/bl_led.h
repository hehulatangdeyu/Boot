#ifndef __BL_LED_H
#define __BL_LED_H

#include <stdint.h>
#include <zephyr/device.h>

void bl_led_init(void);
void bl_led_on(uint32_t led_num);
void bl_led_off(uint32_t led_num);


#endif