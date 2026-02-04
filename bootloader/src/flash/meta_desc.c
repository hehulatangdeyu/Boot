#include <zephyr/sys/crc.h>
#include <string.h>
#include "meta_desc.h"

void meta_desc_init(meta_desc_info_t *meta)
{
    meta->magic = 0x1A2B3C4D;
    meta->firmware_addr = 0;
    meta->firmware_size = 0;
    meta->firmware_crc = 0;

    strcpy(meta->firmware_version, "NULL");
    meta->faild_count = 0;
    meta->firmware_state = NONE;
    meta->Sequence_number = 0;
    meta->download_len = 0;
    meta->target_crc = 0;
    meta->is_program = 0;

    uint16_t ccrc = crc16_itu_t(0, (const uint8_t*)meta, &meta->is_program - meta);
    meta->struct_ccrc = ccrc;
}