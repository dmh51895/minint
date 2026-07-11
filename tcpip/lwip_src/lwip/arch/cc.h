#ifndef __LWIP_ARCH_CC_H__
#define __LWIP_ARCH_CC_H__

#include <stdint.h>
#include <stddef.h>

typedef uint8_t     u8_t;
typedef uint16_t    u16_t;
typedef uint32_t    u32_t;
typedef uint64_t    u64_t;
typedef int8_t      s8_t;
typedef int16_t     s16_t;
typedef int32_t     s32_t;
typedef int64_t     s64_t;

#define U16_F "hu"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "zu"
#define U8_F "u"
#define S8_F "d"

#define LWIP_ERR_T int
#define LWIP_RAND() 0xDEADBEEF

#define lwip_htons(n) (__builtin_bswap16(n))
#define lwip_htonl(n) (__builtin_bswap32(n))
#define LWIP_ALLOW_MEMFREE 0
#define LWIP_NO_CTYPE_H 1
typedef int sys_prot_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif