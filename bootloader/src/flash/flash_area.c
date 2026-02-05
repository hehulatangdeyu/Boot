#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include "flash_area.h"
#include "bitos.h"

LOG_MODULE_REGISTER(internal_flash, CONFIG_LOG_DEFAULT_LEVEL);

K_MUTEX_DEFINE(flash_action);

#if DT_NODE_EXISTS(DT_NODELABEL(flash0))
    static const uint32_t flash_base = DT_REG_ADDR(DT_NODELABEL(flash0));
#else
    LOG_ERR("flash0 device not found in device tree");
    flash_area_close(fa);
    k_mutex_unlock(&flash_action);
    return;
#endif

#define ARG_INFO_BYTE_NUMBER            16
#define DEVICE_UPGRADE_VERIFY_MAGIC     0x1A2B3C4D

bool bl_flash_get_arginfo(uint32_t *fwaddr, uint32_t *fwsize, uint32_t *fwcrc)
{
    k_mutex_lock(&flash_action, K_FOREVER);

    const struct flash_area *farg;
    uint32_t fw_magic;

    if (flash_area_open(FIXED_PARTITION_ID(arg_info), &farg) != 0) {
        LOG_ERR("faild to open arg_info partition");
        k_mutex_unlock(&flash_action);
        return false;
    }

    if (flash_area_read(farg, 0, &fw_magic, sizeof(uint32_t)) != 0) {
        LOG_ERR("faild to read device arg info");
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return false;
    }

    if (fw_magic != DEVICE_UPGRADE_VERIFY_MAGIC) {
        LOG_ERR("faild arg info magic mismatch 0x%08x != 0x%08x", fw_magic, 
            DEVICE_UPGRADE_VERIFY_MAGIC);
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return false;
    }

    if (flash_area_read(farg, sizeof(uint32_t), fwaddr, sizeof(uint32_t)) != 0) {
        LOG_ERR("faild to read firmware address info");
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return false;
    }

    if (flash_area_read(farg, 2 * sizeof(uint32_t), fwsize, sizeof(uint32_t)) != 0) {
        LOG_ERR("faild to read firmware size info");
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return false;
    }

    if (flash_area_read(farg, 3 * sizeof(uint32_t), fwcrc, sizeof(uint32_t)) != 0) {
        LOG_ERR("faild to read firmware CRC info");
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return false;
    }

    flash_area_close(farg);
    k_mutex_unlock(&flash_action);

    return true;
}

bool bl_diff_info_copy(uint32_t fwsize, uint32_t fwcrc)
{
    k_mutex_lock(&flash_action, K_FOREVER);

    const struct flash_area *farg, *fapp;
    if (flash_area_open(FIXED_PARTITION_ID(arg_info), &farg) != 0) {
        k_mutex_unlock(&flash_action);
        return false;
    }

    if (flash_area_open(FIXED_PARTITION_ID(application), &fapp) != 0) {
        k_mutex_unlock(&flash_action);
        flash_area_close(farg);
        return false;
    }

    if (flash_area_erase(farg, 0, farg->fa_size) != 0) {
        k_mutex_unlock(&flash_action);
        flash_area_close(farg);
        flash_area_close(fapp);
        return false;
    }

    uint8_t *info = (uint8_t *)k_malloc(sizeof(uint8_t) * ARG_INFO_BYTE_NUMBER);
    uint8_t *pinfo = info;
    put_u32_inc(&pinfo, DEVICE_UPGRADE_VERIFY_MAGIC);
    put_u32_inc(&pinfo, flash_base + fapp->fa_off);
    put_u32_inc(&pinfo, fwsize);
    put_u32_inc(&pinfo, fwcrc);

    if (flash_area_write(farg, 0, info, pinfo - info) != 0) {
        k_mutex_unlock(&flash_action);
        flash_area_close(farg);
        flash_area_close(fapp);
        k_free(info);
        return false;
    }

    flash_area_close(farg);
    flash_area_close(fapp);
    k_mutex_unlock(&flash_action);
    return true;
}

void get_device_flash_info(device_flash_info_t *device)
{
    k_mutex_lock(&flash_action, K_FOREVER);

    const struct flash_area *fboot;
    const struct flash_area *farg;
    const struct flash_area *fapp;

    if (flash_area_open(FIXED_PARTITION_ID(bootloader), &fboot) != 0) {
        LOG_ERR("get boot info faild to open boot partition");
        k_mutex_unlock(&flash_action);
        return;
    }

    if (flash_area_open(FIXED_PARTITION_ID(arg_info), &farg) != 0) {
        LOG_ERR("get arg info faild to open arg partition");
        k_mutex_unlock(&flash_action);
        return;
    }

    if (flash_area_open(FIXED_PARTITION_ID(application), &fapp) != 0) {
        LOG_ERR("get app info faild to open app partition");
        k_mutex_unlock(&flash_action);
        return;
    }

    device->boot_base_addr = flash_base + fboot->fa_off;
    device->boot_flash_size = fboot->fa_size;  // 48K
    device->arg_base_addr = flash_base + farg->fa_off;
    device->arg_flash_size = farg->fa_size;  // 16K
    device->app_base_addr = flash_base + fapp->fa_off;
    device->app_flash_size = fapp->fa_size;  // 448K

    k_mutex_unlock(&flash_action);
}

void bl_flash_read(uint32_t address, uint8_t *buf, uint32_t size)
{
    k_mutex_lock(&flash_action, K_FOREVER);

    const struct flash_area *fapp;
    
    if (flash_area_open(FIXED_PARTITION_ID(application), &fapp) != 0) {
        LOG_ERR("faild to open app partition");
        k_mutex_unlock(&flash_action);
        return;
    }

    const uint32_t block = 4096;
    uint32_t offset = address - (flash_base + fapp->fa_off);
    uint32_t chunk = size;
    do
    {
        chunk = size > block ? block : size;
        flash_area_read(fapp, offset, buf, size);
        offset += chunk;
        buf += chunk;
        size -= chunk;
    } while (size > 0);

    k_mutex_unlock(&flash_action);
}

int bl_flash_erase(uint32_t address, uint32_t size)
{
    k_mutex_lock(&flash_action, K_FOREVER);

    const struct flash_area *fapp;
    const struct flash_area *farg;
    const struct flash_area *ft;

    if (flash_area_open(FIXED_PARTITION_ID(application), &fapp) != 0) {
        LOG_ERR("faild to open app partition");
        k_mutex_unlock(&flash_action);
        return -1;
    }

    if (flash_area_open(FIXED_PARTITION_ID(arg_info), &farg) != 0) {
        LOG_ERR("faild to open arg partition");
        k_mutex_unlock(&flash_action);
        flash_area_close(fapp);
        return -1;
    }

    if (address >= flash_base + fapp->fa_off)
        ft = fapp;
    else
        ft = farg;

    uint32_t part_abs_start = flash_base + ft->fa_off;  // 0x08000000 + 0x10000 = 0x08010000
    uint32_t part_abs_end = part_abs_start + ft->fa_size - 1;  // 0x0807FFFF

    if ((address < part_abs_start) || ((address + size - 1) > part_abs_end)) {
        LOG_ERR("erase range out of app partition!");
        LOG_ERR("req range: 0x%08x - 0x%08x", address, address + size - 1);
        flash_area_close(fapp);
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return -1;
    }

    uint32_t partition_offset = address - part_abs_start;  // 0x08010000 - 0x08010000 = 0

    if (flash_area_erase(ft, partition_offset, size) != 0) {
        LOG_ERR("erase faild flash offset 0x%08x, size 0x%08x", partition_offset, size);
        flash_area_close(fapp);
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return -1;
    }

    LOG_DBG("flash erase success!");
    LOG_INF("erase addr: 0x%08x - 0x%08x, partition offset: 0x%08x, size: %u bytes",
           address, address + size - 1, partition_offset, size);

    flash_area_close(fapp);
    flash_area_close(farg);
    k_mutex_unlock(&flash_action);

    return 0;
}

int bl_flash_program(uint32_t address, uint32_t size, uint8_t *data)
{
    k_mutex_lock(&flash_action, K_FOREVER);

    const struct flash_area *fapp;
    const struct flash_area *farg;
    const struct flash_area *ft;

    if (flash_area_open(FIXED_PARTITION_ID(application), &fapp) != 0) {
        LOG_ERR("faild to open app partition");
        k_mutex_unlock(&flash_action);
        return -1;
    }

    if (flash_area_open(FIXED_PARTITION_ID(arg_info), &farg) != 0) {
        LOG_ERR("faild to open arg partition");
        flash_area_close(fapp);
        k_mutex_unlock(&flash_action);
        return -1;
    }

    if (address >= flash_base + fapp->fa_off)
        ft = fapp;
    else
        ft = farg;

    uint32_t part_abs_start = flash_base + ft->fa_off;  // 0x08000000 + 0x10000 = 0x08010000
    uint32_t part_abs_end = part_abs_start + ft->fa_size - 1;  // 0x0807FFFF

    if ((address < part_abs_start) || ((address + size - 1) > part_abs_end)) {
        LOG_ERR("program range out of app partition!");
        LOG_ERR("req range: 0x%08x - 0x%08x", address, address + size - 1);
        flash_area_close(fapp);
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return -1;
    }

    uint32_t partition_offset = address - part_abs_start;  // 0x08010000 - 0x08010000 = 0

    if (flash_area_write(ft, partition_offset, data, size) != 0) {
        LOG_ERR("faild to program flash offset 0x%08x, size 0x%08x)", partition_offset, size);
        flash_area_close(fapp);
        flash_area_close(farg);
        k_mutex_unlock(&flash_action);
        return -1;
    }

    LOG_DBG("program flash success!");
    LOG_INF("program addr: 0x%08x - 0x%08x, partition offset: 0x%08x, size: %u bytes",
           address, address + size - 1, partition_offset, size);

    flash_area_close(fapp);
    flash_area_close(farg);
    k_mutex_unlock(&flash_action);

    return 0;
}
