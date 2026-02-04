#ifndef __BITOS_H
#define __BITOS_H

#include <stdint.h>
#include <string.h>

#define TAMPATE_GET(_nick, _type) \
    static inline _type get_##_nick(uint8_t* ptr){ \
        return *(_type *)ptr; \
    }

#define TAMPATE_GET_INC(_nick, _type) \
    static inline _type get_##_nick##_inc(uint8_t** ptr){ \
        uint8_t* p = *ptr; \
        *ptr += sizeof(_type); \
        return get_##_nick(p); \
    }

#define TAMPATE_PUT(_nick, _type) \
    static inline void put_##_nick(uint8_t* ptr, _type value) { \
        *(_type *)ptr = value; \
    }

#define TAMPATE_PUT_INC(_nick, _type) \
    static inline void put_##_nick##_inc(uint8_t** ptr, _type value) { \
        uint8_t* p = *ptr; \
        *ptr += sizeof(_type); \
        put_##_nick(p, value); \
    }

#define __VALUE_OPERATION_TAMPATE(_micro) \
_micro(u8, uint8_t) \
_micro(u16, uint16_t) \
_micro(u32, uint32_t) \
_micro(i8, int8_t) \
_micro(i16, int16_t) \
_micro(i32, int32_t) \
_micro(float, float) \
_micro(double, double)

__VALUE_OPERATION_TAMPATE(TAMPATE_GET)
__VALUE_OPERATION_TAMPATE(TAMPATE_GET_INC)
__VALUE_OPERATION_TAMPATE(TAMPATE_PUT)
__VALUE_OPERATION_TAMPATE(TAMPATE_PUT_INC)

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
