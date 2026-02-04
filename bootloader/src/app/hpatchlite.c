#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include "hpatch_lite.h"
#include "tuz_dec.h"
#include "flash_area.h"
#include "hpatchlite.h"

LOG_MODULE_REGISTER(hpatchlite, CONFIG_LOG_DEFAULT_LEVEL);

#ifndef kCompressType
    #define kCompressType_no  0
    #define kCompressType_tuz 1
#endif

struct __packed ota_custom_header {
    char magic[4]; uint32_t version; uint32_t new_size; uint32_t new_crc; uint8_t reserved[48];
};

#define WRITE_BUF_SIZE 256

struct patch_ctx {
    const struct flash_area *fa_old;
    const struct flash_area *fa_diff;
    const struct flash_area *fa_new;
    uint32_t write_addr_offset;
    uint32_t read_diff_offset;
    
    uint8_t  write_buf[WRITE_BUF_SIZE];
    uint16_t buf_fill;
};

typedef struct {
    hpi_TInputStreamHandle  raw_stream_handle;
    hpi_TInputStream_read   raw_read_cb;
    tuz_TStream             tuz_stream;
    uint8_t* dec_buffer; 
} tuz_adapter_ctx_t;

static hpi_BOOL cb_read_diff(hpi_TInputStreamHandle diff_data, hpi_byte* out_data, hpi_size_t* data_size) {
    struct patch_ctx *ctx = (struct patch_ctx *)diff_data;
    hpi_size_t to_read = *data_size;
    if (to_read == 0) return hpi_TRUE;
    int ret = flash_area_read(ctx->fa_diff, ctx->read_diff_offset, out_data, to_read);
    if (ret != 0) { *data_size = 0; return hpi_FALSE; }
    *data_size = to_read;
    ctx->read_diff_offset += to_read;
    return hpi_TRUE;
}
static hpi_BOOL hpi_read_diff_adapter(hpi_TInputStreamHandle diff_data, hpi_byte* out_data, hpi_size_t* data_size) {
    return cb_read_diff(diff_data, out_data, data_size);
}

static tuz_BOOL _tuz_read_code_cb(tuz_TInputStreamHandle handle, tuz_byte* buf, tuz_size_t* data_size) {
    tuz_adapter_ctx_t* ctx = (tuz_adapter_ctx_t*)handle;
    hpi_size_t read_len = *data_size;
    if (!ctx->raw_read_cb(ctx->raw_stream_handle, buf, &read_len)) return tuz_FALSE; 
    *data_size = read_len;
    return tuz_TRUE;
}

static hpi_BOOL _hpi_tuz_read_adapter(hpi_TInputStreamHandle handle, hpi_byte* out_data, hpi_size_t* data_size) {
    tuz_adapter_ctx_t* ctx = (tuz_adapter_ctx_t*)handle;
    tuz_size_t this_read_size = *data_size;
    tuz_TResult ret = tuz_TStream_decompress_partial(&ctx->tuz_stream, out_data, &this_read_size);
    *data_size = this_read_size;
    return (ret == tuz_OK || ret == tuz_STREAM_END) ? hpi_TRUE : hpi_FALSE;
}

static hpi_BOOL cb_read_old(hpatchi_listener_t* listener, hpi_pos_t read_from_pos, hpi_byte* out_data, hpi_size_t data_size) {
    struct patch_ctx *ctx = (struct patch_ctx *)listener->diff_data;
    return (flash_area_read(ctx->fa_old, (off_t)read_from_pos, out_data, data_size) == 0) ? hpi_TRUE : hpi_FALSE;
}

static hpi_BOOL cb_write_new(hpatchi_listener_t* listener, const hpi_byte* data, hpi_size_t data_size) {
    struct patch_ctx *ctx = (struct patch_ctx *)listener->diff_data;
    hpi_size_t len = data_size;
    const uint8_t *p = data;

    while (len > 0) {
        uint16_t space = WRITE_BUF_SIZE - ctx->buf_fill;
        uint16_t to_copy = (len < space) ? len : space;

        memcpy(&ctx->write_buf[ctx->buf_fill], p, to_copy);
        ctx->buf_fill += to_copy;
        p += to_copy;
        len -= to_copy;

        // if buffer is full, program in Flash
        if (ctx->buf_fill == WRITE_BUF_SIZE) {
            if (flash_area_write(ctx->fa_new, ctx->write_addr_offset, ctx->write_buf, WRITE_BUF_SIZE) != 0) {
                LOG_ERR("flash write failed at %d", ctx->write_addr_offset);
                return hpi_FALSE;
            }
            ctx->write_addr_offset += WRITE_BUF_SIZE;
            ctx->buf_fill = 0; // reset buffer
        }
    }
    return hpi_TRUE;
}

static hpi_BOOL cb_read_old_tuz(hpatchi_listener_t* listener, hpi_pos_t read_from_pos, hpi_byte* out_data, hpi_size_t data_size) {
    // get TinyUZ info
    tuz_adapter_ctx_t* tuz_ctx = (tuz_adapter_ctx_t*)listener->diff_data;
    struct patch_ctx *ctx = (struct patch_ctx *)tuz_ctx->raw_stream_handle;
    
    return (flash_area_read(ctx->fa_old, (off_t)read_from_pos, out_data, data_size) == 0) ? hpi_TRUE : hpi_FALSE;
}

static hpi_BOOL cb_write_new_tuz(hpatchi_listener_t* listener, const hpi_byte* data, hpi_size_t data_size) {
    tuz_adapter_ctx_t* tuz_ctx = (tuz_adapter_ctx_t*)listener->diff_data;
    struct patch_ctx *ctx = (struct patch_ctx *)tuz_ctx->raw_stream_handle;
    
    hpi_size_t len = data_size;
    const uint8_t *p = data;

    while (len > 0) {
        uint16_t space = WRITE_BUF_SIZE - ctx->buf_fill;
        uint16_t to_copy = (len < space) ? len : space;

        memcpy(&ctx->write_buf[ctx->buf_fill], p, to_copy);
        ctx->buf_fill += to_copy;
        p += to_copy;
        len -= to_copy;

        if (ctx->buf_fill == WRITE_BUF_SIZE) {
            if (flash_area_write(ctx->fa_new, ctx->write_addr_offset, ctx->write_buf, WRITE_BUF_SIZE) != 0) {
                LOG_ERR("flash write faild at %d", ctx->write_addr_offset);
                return hpi_FALSE;
            }
            ctx->write_addr_offset += WRITE_BUF_SIZE;
            ctx->buf_fill = 0;
        }
    }
    return hpi_TRUE;
}

// add refresh function api
static int flush_write_buffer(struct patch_ctx *ctx) {
    if (ctx->buf_fill > 0) {
        // fill remaining part is 0xFF
        memset(&ctx->write_buf[ctx->buf_fill], 0xFF, WRITE_BUF_SIZE - ctx->buf_fill);
        
        if (flash_area_write(ctx->fa_new, ctx->write_addr_offset, ctx->write_buf, WRITE_BUF_SIZE) != 0) {
            LOG_ERR("flash flush failed");
            return -EIO;
        }
        ctx->write_addr_offset += ctx->buf_fill; // update effective length
        ctx->buf_fill = 0;
    }
    return 0;
}

int start_firmware_patch(uint32_t *out_new_size, uint32_t *out_new_crc) {
    struct ota_custom_header header;
    hpatchi_listener_t listener = {0};
    hpi_compressType compress_type = kCompressType_no;
    
    // define pointer, all use heap
    struct patch_ctx* p_main_ctx = NULL;
    tuz_adapter_ctx_t* p_tuz_ctx = NULL;
    uint8_t* p_temp_cache = NULL;
    
    hpi_TInputStreamHandle final_diff_handle = NULL;
    hpi_TInputStream_read  final_diff_read   = NULL;

    hpi_pos_t alg_new_size = 0;
    hpi_pos_t alg_uncompress_size = 0;
    int ret = -EFTYPE;

    p_main_ctx = k_malloc(sizeof(struct patch_ctx));
    if (!p_main_ctx) { LOG_ERR("om: main ctx"); return -ENOMEM; }
    memset(p_main_ctx, 0, sizeof(struct patch_ctx));

    // open the partition
    if (flash_area_open(FIXED_PARTITION_ID(application), &p_main_ctx->fa_old) != 0 ||
        flash_area_open(FIXED_PARTITION_ID(download_partition), &p_main_ctx->fa_diff) != 0 ||
        flash_area_open(FIXED_PARTITION_ID(diff_fw_partition), &p_main_ctx->fa_new) != 0) {
        LOG_ERR("faild to open flash partitions");
        ret = -ENODEV; goto cleanup;
    }

    // read header
    flash_area_read(p_main_ctx->fa_diff, 0, &header, sizeof(header));
    if (memcmp(header.magic, "DOTA", sizeof(header.magic)) == 0) {
        LOG_WRN("parse package magic header: %s, select diff update", header.magic);
    } else {
        LOG_WRN("parse package magic header: %s, select full update", header.magic);
        ret = FULL_PACKAGE_FLAG; goto cleanup;
    }

    p_main_ctx->read_diff_offset = sizeof(header);

    // open patch
    if (!hpatch_lite_open(p_main_ctx, hpi_read_diff_adapter, &compress_type, &alg_new_size, &alg_uncompress_size)) {
        LOG_ERR("hpatch open faild!"); goto cleanup;
    }

    uint32_t final_new_size = (uint32_t)(alg_new_size & 0xFFFFFFFF);
    LOG_INF("hpatch open, size: %u, type: %d", final_new_size, compress_type);

    if (compress_type == kCompressType_tuz) {
        LOG_INF("type: tinyuz");
        
        p_tuz_ctx = k_malloc(sizeof(tuz_adapter_ctx_t));
        if (!p_tuz_ctx) { LOG_ERR("oom: tuz ctx"); ret = -ENOMEM; goto cleanup; }
        memset(p_tuz_ctx, 0, sizeof(tuz_adapter_ctx_t));

        p_tuz_ctx->raw_stream_handle = (hpi_TInputStreamHandle)p_main_ctx; 
        p_tuz_ctx->raw_read_cb       = hpi_read_diff_adapter;      

        // skip 4 byte header
        uint8_t dummy[4]; hpi_size_t dummy_len = 4;
        if (!p_tuz_ctx->raw_read_cb(p_tuz_ctx->raw_stream_handle, dummy, &dummy_len)) { ret = -EIO; goto cleanup; }
        LOG_INF("skip header: %02x %02x %02x %02x", dummy[0], dummy[1], dummy[2], dummy[3]);

        tuz_size_t dict_size = 4096;
        tuz_size_t cache_size = 1024; 
        
        p_tuz_ctx->dec_buffer = k_malloc(dict_size + cache_size);
        if (!p_tuz_ctx->dec_buffer) { LOG_ERR("oom: tuz buffer"); ret = -ENOMEM; goto cleanup; }

        if (tuz_TStream_open(&p_tuz_ctx->tuz_stream, (tuz_TInputStreamHandle)p_tuz_ctx, 
                           _tuz_read_code_cb, p_tuz_ctx->dec_buffer, dict_size, cache_size) != tuz_OK) {
            LOG_ERR("tinyuz open error"); ret = -EIO; goto cleanup;
        }

        final_diff_handle = (hpi_TInputStreamHandle)p_tuz_ctx;
        final_diff_read   = _hpi_tuz_read_adapter;
    } else {
        final_diff_handle = (hpi_TInputStreamHandle)p_main_ctx;
        final_diff_read   = hpi_read_diff_adapter;
    }

    LOG_INF("eraseing backup partition...");
    flash_area_erase(p_main_ctx->fa_new, 0, p_main_ctx->fa_new->fa_size);
    p_main_ctx->write_addr_offset = 0;
    
    listener.diff_data = final_diff_handle;  
    listener.read_diff = final_diff_read;    
    if (compress_type == kCompressType_tuz) {
        LOG_INF("using tinyuz mode");
        listener.read_old  = cb_read_old_tuz;
        listener.write_new = cb_write_new_tuz;
    } else {
        listener.read_old  = cb_read_old;
        listener.write_new = cb_write_new;
    }  

    p_temp_cache = k_malloc(4096);
    if (!p_temp_cache) { ret = -ENOMEM; goto cleanup; }

    if (hpatch_lite_patch(&listener, (hpi_pos_t)final_new_size, p_temp_cache, 4096)) {
        if (flush_write_buffer(p_main_ctx) != 0) {
            ret = -EIO;
        } else {
            LOG_INF("patch success!");
            *out_new_size = final_new_size;
            *out_new_crc = header.new_crc;
            ret = 0;
        }
    } else {
        LOG_ERR("patch error!");
        ret = -EIO;
    }

cleanup:
    if (p_temp_cache) k_free(p_temp_cache);
    if (p_tuz_ctx) {if (p_tuz_ctx->dec_buffer) k_free(p_tuz_ctx->dec_buffer); k_free(p_tuz_ctx);}
    if (p_main_ctx) {
        if (p_main_ctx->fa_old) flash_area_close(p_main_ctx->fa_old);
        if (p_main_ctx->fa_diff) flash_area_close(p_main_ctx->fa_diff);
        if (p_main_ctx->fa_new) flash_area_close(p_main_ctx->fa_new);
        k_free(p_main_ctx);
    }
    return ret;
}

int flash_copy_to_internal(size_t new_fw_size) {
    const struct flash_area *fa_ext = NULL;
    const struct flash_area *fa_int = NULL;
    int ret = 0;

    flash_area_open(FIXED_PARTITION_ID(diff_fw_partition), &fa_ext);
    flash_area_open(FIXED_PARTITION_ID(application), &fa_int);

    LOG_INF("erasing internal app flash...");
    flash_area_erase(fa_int, 0, fa_int->fa_size);

    uint8_t *copy_buf = k_malloc(4096);
    if (!copy_buf) {
        ret = -ENOMEM;
        goto exit;
    }

    uint32_t processed = 0;
    while (processed < new_fw_size) {
        uint32_t chunk = (new_fw_size - processed > 4096) ? 4096 : (new_fw_size - processed);
        flash_area_read(fa_ext, processed, copy_buf, chunk);
        flash_area_write(fa_int, processed, copy_buf, chunk);
        processed += chunk;
        if (processed % (32 * 1024) == 0) {
            LOG_INF("internal copy: %d / %d", processed, (uint32_t)new_fw_size);
        }
    }
    k_free(copy_buf);

exit:
    flash_area_close(fa_ext);
    flash_area_close(fa_int);
    return ret;
}

int verify_internal_firmware(uint32_t fw_size, uint32_t crc) {
    const struct flash_area *fa_int;
    uint32_t ccrc = 0;
    uint8_t verify_buf[1024];
    uint32_t offset = 0;

    flash_area_open(FIXED_PARTITION_ID(application), &fa_int);
    LOG_INF("verifying internal firmware crc...");

    while (offset < fw_size) {
        uint32_t len = (fw_size - offset > sizeof(verify_buf)) ? sizeof(verify_buf) : (fw_size - offset);
        flash_area_read(fa_int, offset, verify_buf, len);
        ccrc = crc32_ieee_update(ccrc, verify_buf, len);
        offset += len;
    }
    flash_area_close(fa_int);

    if (ccrc == crc) {
        LOG_INF("verify success 0x%08X", ccrc);
        return 0;
    }
    LOG_ERR("verify err expected: 0x%08X, got: 0x%08X", crc, ccrc);
    return -EFAULT;
}

int ota_update_task(void) {
    uint32_t restored_size = 0;
    uint32_t restored_crc = 0;
    int ret;

    LOG_INF("check diff or full package...");

    ret = start_firmware_patch(&restored_size, &restored_crc);
    if (ret != 0) return ret;

    ret = flash_copy_to_internal(restored_size);
    if (ret != 0) return ret;

    ret = verify_internal_firmware(restored_size, restored_crc);
    if (ret != 0) {
        return ret;
    }

    if (!bl_diff_info_copy(restored_size, restored_crc)) {
        LOG_INF("diff package info program err");
        return ret;
    }

    LOG_INF("diff package update success!");

    return ret;
}