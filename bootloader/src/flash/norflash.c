#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include "flash_area.h"
#include "hpatchlite.h"
#include "meta_desc.h"

LOG_MODULE_REGISTER(external_flash, CONFIG_LOG_DEFAULT_LEVEL);

#if !DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(norflash1)) || \
    !DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(norflash2))
#error "Unsupported board: norflash1 or norflash2 devicetree alias is not defined"
#endif

#define META_PARTITION_A_ID     DT_FIXED_PARTITION_ID(DT_NODELABEL(meta_partition_a))
#define ACTIVE_BACKUP_ID        DT_FIXED_PARTITION_ID(DT_NODELABEL(active_backup_partition))
#define DOWNLOAD_SLOT_ID        DT_FIXED_PARTITION_ID(DT_NODELABEL(download_partition))
#define DIFF_FW_SLOT_ID         DT_FIXED_PARTITION_ID(DT_NODELABEL(diff_fw_partition))
#define FATFS_ID                DT_FIXED_PARTITION_ID(DT_NODELABEL(fatfs_partition))

#define STM32_APPLICATION_FLASH_BASE    0x08010000

K_MUTEX_DEFINE(norflash_action);

#define PARTITION_ONE_SYMBOL_DEFINE     (0x11111111)
#define PARTITION_TWO_SYMBOL_DEFINE     (0x22222222)
#define PARTITION_INFO_OFFSET           (1024 * 64)

static const struct device *flashes[] = {
    DEVICE_DT_GET(DT_ALIAS(norflash1)),
    DEVICE_DT_GET(DT_ALIAS(norflash2)),
};

static void flash_device_init(const struct device *flash)
{
    k_mutex_lock(&norflash_action, K_FOREVER);
    
    int rc;
    uint64_t size;

    if (!device_is_ready(flash)) {
        LOG_ERR("%s not ready", flash->name);
        return;
    }

    /* 获取Flash大小 */
    rc = flash_get_size(flash, &size);
    if (rc < 0) {
        LOG_ERR("%s flash_get_size faild: %d", flash->name, rc);
        return;
    }

    LOG_INF("%s size: %llu bytes, initialized successfully", flash->name, (unsigned long long)size);

    k_mutex_unlock(&norflash_action);
}

uint32_t get_norflash_partition_pointer(void)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    uint8_t buf[4];
    int ret;

    ret = flash_read(flashes[1], 0, buf, sizeof(uint32_t));
    if (ret != 0)
    {
        LOG_ERR("read norflash partition pointer info faild: %d", ret);
        k_mutex_unlock(&norflash_action);
        return 0;
    }

    k_mutex_unlock(&norflash_action);
    return *(uint32_t*)buf;
}

void set_norflash_partition_pointer(uint32_t partition)
{
    k_mutex_lock(&norflash_action, K_FOREVER);
    
    uint32_t *partitions = &partition;
    int ret;

    ret = flash_erase(flashes[0], 0, PARTITION_INFO_OFFSET);
    if (ret != 0)
    {
        LOG_ERR("erase norflash partition pointer info faild: %d", ret);
        k_mutex_unlock(&norflash_action);
        return;
    }

    ret = flash_write(flashes[0], 0, partitions, sizeof(uint32_t));
    if (ret != 0)
    {
        LOG_ERR("program norflash partition pointer info faild: %d", ret);
        k_mutex_unlock(&norflash_action);
        return;
    }

    LOG_INF("set norflash partition pointer info success");
    k_mutex_unlock(&norflash_action);
}

bool bl_verify_external_norflash_firmware(void)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    const struct flash_area *fbck = NULL;
    uint32_t fwaddr = 0, fwsize = 0, fwcrc = 0;
    bool check = true;

    const uint32_t block = 4096;
    uint8_t *buf = (uint8_t *)k_malloc(sizeof(uint8_t) * block);
    if (buf == NULL) {
        check = false;
        goto cleanup;
    }

    check = bl_flash_get_arginfo(&fwaddr, &fwsize, &fwcrc);
    if (!check) {
        goto cleanup;
    }

    if (flash_area_open(ACTIVE_BACKUP_ID, &fbck)) {
        check = false;
        goto cleanup;
    }

    uint32_t size = fwsize;
    uint32_t offset = 0;
    uint32_t ccrc = 0;
    do {
        uint32_t chunk = size > block ? block : size;
        flash_area_read(fbck, offset, buf, chunk);
        ccrc = crc32_ieee_update(ccrc, buf, chunk);
        offset += chunk;
        size -= chunk;
    } while (size > 0);

    if (ccrc != fwcrc) {
        LOG_ERR("backup partition verify faild");
        check = false;
        goto cleanup;
    }

    LOG_INF("select backup partition recover");
    
    const struct flash_area *fapp = NULL;
    if (flash_area_open(FIXED_PARTITION_ID(application), &fapp)) {
        check = false;
        goto cleanup;
    }

    if (flash_area_erase(fapp, 0, fwsize)) {
        LOG_ERR("internal flash erase faild");
        check = false;
        goto cleanup;
    }

    offset = 0;
    size = fwsize;
    do {
        uint32_t chunk = size > block ? block : size;
        flash_area_read(fbck, offset, buf, chunk);
        flash_area_write(fapp, offset, buf, chunk);
        offset += chunk;
        size -= chunk;

        uint32_t progress = (offset * 100) / fwsize;
        if (progress % 10 == 0)
            LOG_INF("progress %u%%", progress);
    } while (size > 0);

    LOG_INF("recover success");

cleanup:
    if (buf) k_free(buf);
    if (fbck) flash_area_close(fbck);
    if (fapp) flash_area_close(fapp);
    k_mutex_unlock(&norflash_action);
    return check;
}

int nor_flash_erase_download_slot(void)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    const struct flash_area *fa, *fm;
    uint32_t ccrc = 0;
    int ret;

    ret = flash_area_open(META_PARTITION_A_ID, &fm);
    if (ret != 0) {
        LOG_ERR("opening meta_a slot partition faild, ret %d", ret);
        k_mutex_unlock(&norflash_action);
        return ret;
    }

    uint32_t write_len;
    uint32_t write_off = OFFSET_OF(meta_desc_info_t, download_len);
    ret = flash_area_read(fm, write_off, (void *)&write_len, sizeof(uint32_t));
    if (ret != 0) {
        LOG_ERR("read meta download_slot info faild, ret %d", ret);
        flash_area_close(fm);
        k_mutex_unlock(&norflash_action);
        return ret;
    }

    ret = flash_area_open(DOWNLOAD_SLOT_ID, &fa);
    if (ret != 0) {
        LOG_ERR("opening download_slot partition faild, ret = %d", ret);
        flash_area_close(fm);
        k_mutex_unlock(&norflash_action);
        return ret;
    }

    if (write_len != 0) {
        if (write_len > fa->fa_size) {
            LOG_ERR("write len of range download_slot size");
            flash_area_close(fm);
            flash_area_close(fa);
            k_mutex_unlock(&norflash_action);
            return -1;
        }
        uint8_t *user = (uint8_t*)k_malloc(sizeof(uint8_t) * 4096); 
        if (!user) {
            LOG_ERR("malloc faild");
            flash_area_close(fm);
            flash_area_close(fa);
            k_mutex_unlock(&norflash_action);
            return -ENOMEM;
        }

        uint32_t tp_len = write_len;
        uint32_t offset = 0;
        const uint32_t block = 4096;
        do
        {
            uint32_t size = tp_len > block ? block : tp_len;
            flash_area_read(fa, offset, user, size);
            ccrc = crc32_ieee_update(ccrc, user, (size_t)size);
            offset += size;
            tp_len -= size;
        } while (tp_len > 0);

        k_free(user);
    }

    uint32_t erase_offset;
    uint32_t erase_len;
    uint32_t sector_size = 4096;
    
    struct flash_pages_info info;
    const struct device *flash_dev = flash_area_get_device(fa);
    if (flash_dev && flash_get_page_info_by_offs(flash_dev, fa->fa_off + write_len, &info) == 0) {
        sector_size = info.size;
    }

    // starting position align next partition header
    // (offset + size - 1) & ~(size - 1)
    uint32_t aligned_start = (write_len + sector_size - 1) & ~(sector_size - 1);

    if (aligned_start >= fa->fa_size) {
        //If the aligned starting position exceeds the partition size, it indicates that there is less than one sector of remaining space, or it is already full
        //No need to erase, set directly to 0
        erase_len = 0;
        erase_offset = aligned_start; 
    } else {
        erase_offset = aligned_start;
        erase_len = fa->fa_size - erase_offset;
    }

    if (erase_len > 0) {
        LOG_INF("eraseing download slot: write len: %d, offset 0x%x, size %d", 
                 write_len, erase_offset, erase_len);
        
        ret = flash_area_erase(fa, erase_offset, erase_len);
        
        if (ret != 0) {
            LOG_ERR("erase faild ret %d", ret);
        } else {
            LOG_INF("erase success");
        }
    } else {
        LOG_INF("not needed erase write_len close to end of partition");
        ret = 0; 
    }

    flash_area_close(fa);
    flash_area_close(fm);
    k_mutex_unlock(&norflash_action);
    return ret;
}

int nor_flash_program_download_slot(uint32_t address, uint32_t size, uint8_t *src)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    const struct flash_area *fa;
    int ret;

    ret = flash_area_open(DOWNLOAD_SLOT_ID, &fa);
    if (ret != 0) {
        LOG_ERR("opening download_slot partition faild, ret = %d", ret);
        k_mutex_unlock(&norflash_action);
        return ret;
    }

    uint32_t offset = address - STM32_APPLICATION_FLASH_BASE;
    LOG_INF("begin program download slot: offset 0x%08lx, size %zu byte", 
             (long)fa->fa_off + offset, size);
    
    ret = flash_area_write(fa, offset, src, size);
    if (ret != 0) {
        LOG_ERR("program faild, ret %d", ret);
    }

    flash_area_close(fa);
    k_mutex_unlock(&norflash_action);
    return ret;
}

int nor_flash_program_meta_slot(meta_desc_info_t *meta)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    const struct flash_area *fa;
    int ret;

    ret = flash_area_open(META_PARTITION_A_ID, &fa);
    if (ret != 0)
        LOG_ERR("meta partition open faild, ret %d", ret);
    
    ret = flash_area_erase(fa, 0, fa->fa_size); // 擦
    if (ret != 0)
        LOG_ERR("meta partition erase faild, ret %d", ret);

    ret = flash_area_write(fa, 0, meta, sizeof(meta_desc_info_t));
    if (ret != 0)
        LOG_ERR("meta partition program faild, ret %d", ret);

    flash_area_close(fa);
    k_mutex_unlock(&norflash_action);
    return ret;
}

uint32_t download_slot_verify(uint32_t address, uint32_t size)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    const struct flash_area *fa;
    int ret;
    
    ret = flash_area_open(DOWNLOAD_SLOT_ID, &fa);
    if (ret != 0) {
        LOG_ERR("meta partition open faild %d", ret);
        k_mutex_unlock(&norflash_action);
        return 1;
    }

    const uint32_t block = 4096;
    uint32_t offset = address - STM32_APPLICATION_FLASH_BASE;
    uint32_t fw_crc = 0;
    uint8_t *user = (uint8_t *)k_malloc(block);
    uint8_t *puser = user;
    if (user == NULL) {
        flash_area_close(fa);
        k_mutex_unlock(&norflash_action);
        return 1;
    }

    uint32_t remaining = size;
    do
    {
        uint32_t chunk = ( remaining > block ) ? block : remaining;
        flash_area_read(fa, offset, puser, chunk);
        fw_crc = crc32_ieee_update(fw_crc, (const uint8_t*)puser, (size_t)chunk);
        offset += chunk;
        remaining -= chunk;
    } while (remaining > 0);

    k_free(user);
    flash_area_close(fa);
    k_mutex_unlock(&norflash_action);
    return fw_crc;
}

int download_slot_to_intflash(void)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    const struct flash_area *fa, *fb;
    int ret;
    
    ret = flash_area_open(META_PARTITION_A_ID, &fa);
    if (ret != 0) {
        LOG_ERR("meta partition open faild");
        k_mutex_unlock(&norflash_action);   
        return -1;
    }
    
    ret = flash_area_open(DOWNLOAD_SLOT_ID, & fb);
    if (ret != 0) {
        flash_area_close(fa);
        LOG_ERR("download partition open faild");
        k_mutex_unlock(&norflash_action);
        return -1;
    }

    uint8_t *desc = (uint8_t *)k_malloc(sizeof(uint8_t) * 1024);
    if (desc == NULL) {
        flash_area_close(fa);
        flash_area_close(fb);
        k_mutex_unlock(&norflash_action);
        return -1;
    }

    flash_area_read(fa, 0, desc, 1024);
    meta_desc_info_t *meta = (meta_desc_info_t*)desc;

    const uint32_t block = 4096;
    uint32_t fw_size = meta->firmware_size;
    uint8_t *user = (uint8_t *)k_malloc(sizeof(uint8_t) * block);
    uint8_t *puser = user;
    if (user == NULL) {
        k_free(user);
        k_free(desc);
        flash_area_close(fa);
        flash_area_close(fb);
        LOG_ERR("k malloc faild");
        k_mutex_unlock(&norflash_action);
        return -1;
    }

    LOG_INF("begin erase internal flash, addr %08x, size %d", meta->firmware_addr, meta->firmware_size);
    bl_flash_erase(meta->firmware_addr, meta->firmware_size);

    uint32_t offset = 0;
    do
    {
        uint32_t chunk = fw_size > block ? block : fw_size;
        flash_area_read(fb, offset, puser, chunk);
        bl_flash_program(meta->firmware_addr + offset, chunk, puser);
        offset += chunk;
        fw_size -= chunk;
    } while (fw_size > 0);

    k_free(user);
    k_free(desc);
    flash_area_close(fa);
    flash_area_close(fb);
    k_mutex_unlock(&norflash_action);
    return 0;
}

int select_slot_to_active_backup_partition(int flag)
{
    k_mutex_lock(&norflash_action, K_FOREVER);

    const struct flash_area *fa = NULL, *fb = NULL, *fc = NULL;
    int ret = 0;
    uint8_t *user = NULL;
    uint32_t fw_size = 0;
    uint8_t *desc = NULL;

    ret = flash_area_open(META_PARTITION_A_ID, &fa);
    if (ret != 0) {
        LOG_ERR("meta partition open faild");
        goto cleanup;
    }
    
    if (flag == FULL_PACKAGE_FLAG) {
        ret = flash_area_open(DOWNLOAD_SLOT_ID, &fb);
        if (ret != 0) {
            LOG_ERR("download partition open faild");
            goto cleanup;
        }
    } else if (flag == DIFF_PACKAGE_FLAG) {
        ret = flash_area_open(DIFF_FW_SLOT_ID, &fb);
        if (ret != 0) {
            LOG_ERR("download partition open faild");
            goto cleanup;
        }
    }
    
    ret = flash_area_open(ACTIVE_BACKUP_ID, &fc);
    if (ret != 0) {
        LOG_ERR("active back partition open faild");
        goto cleanup;
    }

    meta_desc_info_t *meta = NULL;
    if (flag == FULL_PACKAGE_FLAG) {
        desc = (uint8_t *)k_malloc(sizeof(uint8_t) * 1024);
        if (desc == NULL) {
            LOG_ERR("k malloc faild");
            goto cleanup;
        }
        flash_area_read(fa, 0, desc, 1024);
        meta= (meta_desc_info_t*)desc;
        fw_size = meta->firmware_size;
    } else if(flag == DIFF_PACKAGE_FLAG) {
        const struct flash_area *farg = NULL;
        ret = flash_area_open(FIXED_PARTITION_ID(arg_info), &farg);
        if (ret != 0)
            goto cleanup;
        ret = flash_area_read(farg, 2 * sizeof(uint32_t), &fw_size, sizeof(uint32_t));
        if (ret != 0)
            goto cleanup;
        flash_area_close(farg);
    }

    const uint32_t block = 4096;
    user = (uint8_t *)k_malloc(sizeof(uint8_t) * block);
    if (user == NULL) {
        LOG_ERR("k malloc faild");
        goto cleanup;
    }

    LOG_INF("begin erase active backup flash, size %zu", fc->fa_size);
    flash_area_erase(fc, 0, fc->fa_size);

    uint32_t offset = 0;
    do
    {
        uint32_t chunk = fw_size > block ? block : fw_size;
        flash_area_read(fb, offset, user, chunk);
        flash_area_write(fc, offset, user, chunk);
        offset += chunk;
        fw_size -= chunk;
    } while (fw_size > 0);

    LOG_INF("active backup success");

cleanup:
    if (user) k_free(user);
    if (desc) k_free(desc);
    if (fa)   flash_area_close(fa);
    if (fb)   flash_area_close(fb);
    if (fc)   flash_area_close(fc);
    k_mutex_unlock(&norflash_action);
    return ret;
}

void norflash_init(void)
{
    flash_device_init(flashes[0]);
    
    k_msleep(100);
    
    flash_device_init(flashes[1]);
}
