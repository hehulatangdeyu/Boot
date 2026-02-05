#ifndef __BITOS_H
#define __BITOS_H

#include <stdint.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

static inline uint32_t get_u32(const uint8_t *ptr) {
    return sys_get_le32(ptr);
}

static inline uint32_t get_u32_inc(uint8_t **ptr) {
    uint32_t val = sys_get_le32(*ptr);
    *ptr += 4;
    return val;
}

static inline uint16_t get_u16(const uint8_t *ptr) {
    return sys_get_le16(ptr);
}

static inline uint16_t get_u16_inc(uint8_t **ptr) {
    uint16_t val = sys_get_le16(*ptr);
    *ptr += 2;
    return val;
}

static inline uint8_t get_u8(const uint8_t *ptr) {
    return *ptr;
}

static inline uint8_t get_u8_inc(uint8_t **ptr) {
    uint8_t val = **ptr;
    *ptr += 1;
    return val;
}

static inline void put_u8(uint8_t *ptr, uint8_t val) {
    *ptr = val;
}

static inline void put_u8_inc(uint8_t **ptr, uint8_t val) {
    **ptr = val;
    *ptr += 1;
}

static inline void put_u32(uint8_t *ptr, uint32_t val) {
    sys_put_le32(val, ptr);
}

static inline void put_u32_inc(uint8_t **ptr, uint32_t val) {
    sys_put_le32(val, *ptr);
    *ptr += 4;
}

static inline void put_u16(uint8_t *ptr, uint16_t val) {
    sys_put_le16(val, ptr);
}

static inline void put_u16_inc(uint8_t **ptr, uint16_t val) {
    sys_put_le16(val, *ptr);
    *ptr += 2;
}

static inline void get_bytes(const uint8_t *ptr, uint8_t* buffer, uint32_t size)
{
    memcpy(buffer, ptr, size);
}

static inline void get_bytes_inc(const uint8_t** ptr, uint8_t* buffer, uint32_t size)
{
    memcpy(buffer, *ptr, size);
    *ptr += size;
}

static inline void put_bytes(uint8_t *ptr, const uint8_t* buffer, uint32_t size)
{
    memcpy(ptr, buffer, size);
}

static inline void put_bytes_inc(uint8_t **ptr, const uint8_t* buffer, uint32_t size)
{
    memcpy(*ptr, buffer, size);
    *ptr += size;
}

#endif
