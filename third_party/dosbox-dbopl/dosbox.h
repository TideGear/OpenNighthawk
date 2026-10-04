/* Minimal DOSBox type/macro definitions used by the standalone DBOPL core. */
#ifndef F117R_DBOPL_TYPES_H
#define F117R_DBOPL_TYPES_H
#include <stdint.h>
typedef uint8_t Bit8u;
typedef int8_t Bit8s;
typedef uint16_t Bit16u;
typedef int16_t Bit16s;
typedef uint32_t Bit32u;
typedef int32_t Bit32s;
typedef uintptr_t Bitu;
typedef intptr_t Bits;
#define INLINE inline
#define GCC_UNLIKELY(x) (x)
#define DB_FASTCALL
#endif
