#ifndef __BL_BUTTON_H
#define __BL_BUTTON_H


void bl_button_init(void);
void disable_gpio_interrupts(void);

extern struct k_sem button_trap;

#endif