#ifndef __FLASH_AREA_H
#define __FLASH_AREA_H

#include <stdint.h>

typedef struct
{
    uint32_t boot_base_addr;
    uint32_t arg_base_addr;
    uint32_t app_base_addr;
    uint32_t boot_flash_size;
    uint32_t arg_flash_size;
    uint32_t app_flash_size;
} device_flash_info_t;

void get_device_flash_info(device_flash_info_t *device);
int bl_flash_erase(uint32_t address, uint32_t size);
bool bl_diff_info_copy(uint32_t fwsize, uint32_t fwcrc);
int bl_flash_program(uint32_t address, uint32_t size, uint8_t *data);
void bl_flash_read(uint32_t address, uint8_t *buf, uint32_t size);
bool bl_flash_get_arginfo(uint32_t *fwaddr, uint32_t *fwsize, uint32_t *fwcrc);

#endif