#ifndef STDINT_H
#define STDINT_H

/*
 * stdint.h — integer types and limits
 *
 * Based on POSIX / SUSv3:
 * https://pubs.opengroup.org/onlinepubs/009695399/basedefs/stdint.h.html
 *
 * Sized for x86-64 (LP64): long long = 64-bit, pointers = 64-bit.
 * int_fast16_t is widened to 32-bit; all other exact-width types match
 * their native size.
 */

// 8-bit
typedef signed char            int8_t;           // from -128 to 127
typedef unsigned char          uint8_t;          // from 0 to 255
typedef signed char            int_least8_t;     // from -128 to 127
typedef unsigned char          uint_least8_t;    // from 0 to 255
typedef signed char            int_fast8_t;      // from -128 to 127
typedef unsigned char          uint_fast8_t;     // from 0 to 255

// 16-bit
typedef short                  int16_t;          // from -32768 to 32767
typedef unsigned short         uint16_t;         // from 0 to 65535
typedef short                  int_least16_t;    // from -32768 to 32767
typedef unsigned short         uint_least16_t;   // from 0 to 65535
typedef int                    int_fast16_t;     // from -2147483648 to 2147483647 (fast, so 32-bit)
typedef unsigned int           uint_fast16_t;    // from 0 to 4294967295 (fast, so 32-bit)

// 32-bit
typedef int                    int32_t;          // from -2147483648 to 2147483647
typedef unsigned int           uint32_t;         // from 0 to 4294967295
typedef int                    int_least32_t;    // from -2147483648 to 2147483647
typedef unsigned int           uint_least32_t;   // from 0 to 4294967295
typedef int                    int_fast32_t;     // from -2147483648 to 2147483647
typedef unsigned int           uint_fast32_t;    // from 0 to 4294967295

// 64-bit
typedef long long              int64_t;          // from -9223372036854775808 to 9223372036854775807
typedef unsigned long long     uint64_t;         // from 0 to 18446744073709551615
typedef long long              int_least64_t;    // from -9223372036854775808 to 9223372036854775807
typedef unsigned long long     uint_least64_t;   // from 0 to 18446744073709551615
typedef long long              int_fast64_t;     // from -9223372036854775808 to 9223372036854775807
typedef unsigned long long     uint_fast64_t;    // from 0 to 18446744073709551615

// pointers
typedef long long              intptr_t;
typedef unsigned long long     uintptr_t;

// max
typedef long long              intmax_t;         // from -9223372036854775808 to 9223372036854775807
typedef unsigned long long     uintmax_t;        // from 0 to 18446744073709551615

// 8-bit limits
#define INT8_MIN          (-128)
#define INT8_MAX          127
#define UINT8_MAX         255
#define INT_LEAST8_MIN    (-128)
#define INT_LEAST8_MAX    127
#define UINT_LEAST8_MAX   255
#define INT_FAST8_MIN     (-128)
#define INT_FAST8_MAX     127
#define UINT_FAST8_MAX    255

// 16-bit limits
#define INT16_MIN         (-32768)
#define INT16_MAX         32767
#define UINT16_MAX        65535
#define INT_LEAST16_MIN   (-32768)
#define INT_LEAST16_MAX   32767
#define UINT_LEAST16_MAX  65535
// int_fast16_t is 32-bit
#define INT_FAST16_MIN    (-2147483647 - 1)
#define INT_FAST16_MAX    2147483647
#define UINT_FAST16_MAX   4294967295U

// 32-bit limits
#define INT32_MIN         (-2147483647 - 1)
#define INT32_MAX         2147483647
#define UINT32_MAX        4294967295U
#define INT_LEAST32_MIN   (-2147483647 - 1)
#define INT_LEAST32_MAX   2147483647
#define UINT_LEAST32_MAX  4294967295U
#define INT_FAST32_MIN    (-2147483647 - 1)
#define INT_FAST32_MAX    2147483647
#define UINT_FAST32_MAX   4294967295U

// 64-bit limits
#define INT64_MIN         (-9223372036854775807LL - 1)
#define INT64_MAX         9223372036854775807LL
#define UINT64_MAX        18446744073709551615ULL
#define INT_LEAST64_MIN   (-9223372036854775807LL - 1)
#define INT_LEAST64_MAX   9223372036854775807LL
#define UINT_LEAST64_MAX  18446744073709551615ULL
#define INT_FAST64_MIN    (-9223372036854775807LL - 1)
#define INT_FAST64_MAX    9223372036854775807LL
#define UINT_FAST64_MAX   18446744073709551615ULL

// pointer limits (LP64: pointers are 64-bit)
#define INTPTR_MIN        (-9223372036854775807LL - 1)
#define INTPTR_MAX        9223372036854775807LL
#define UINTPTR_MAX       18446744073709551615ULL

// greatest-width limits
#define INTMAX_MIN        (-9223372036854775807LL - 1)
#define INTMAX_MAX        9223372036854775807LL
#define UINTMAX_MAX       18446744073709551615ULL

#endif
