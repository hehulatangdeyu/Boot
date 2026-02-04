#ifndef __META_DESC_H__
#define __META_DESC_H__

#include <stdint.h>

typedef enum
{
    NONE, // NULL
    NEW = 0x11, // to be tested
    STABLE, // stable
    BAD, // bad firmware
} firmware_state_t;

typedef struct
{
    uint32_t magic;
    uint32_t firmware_addr;
    uint32_t firmware_size;
    uint32_t firmware_crc;
    char firmware_version[8];

    uint32_t faild_count;
    firmware_state_t firmware_state;
    uint32_t Sequence_number; // number

    uint32_t download_len;
    uint32_t target_crc;
    uint32_t is_program;

    uint32_t struct_ccrc;
} meta_desc_info_t;

#define OFFSET_OF(strc, member) \
    (size_t)(&(((strc*)0)->member))

#endif