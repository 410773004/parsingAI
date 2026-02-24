//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

#pragma once

/*!
 * \file stdlib.h
 * @brief define memory allocation and alignment api
 *
 * \addtogroup utility
 * @{
 */

/*!
 * @brief object to memory type
 */

typedef enum {
	SLOW_DATA = 0, 	///< SRAM
	FAST_DATA,     	///< TCM
	PS_DATA,     	///< PS_DATA

	MAX_MEM_TYPE,	///< MAX_MEM_TYPE
} mem_type_t;

/*! @brief panic dump function and link */
typedef struct _panic_dump_t {
	struct _panic_dump_t *next;	///< next dump
	void (*dump)(void);		///< dump function pointer
} panic_dump_t;

/*! @brief hints for the compiler that allows it to correctly optimize the branch */
#define likely(x)			__builtin_expect(!!(x), 1)
#define unlikely(x)			__builtin_expect(!!(x), 0)

/*! @brief return the number of trailing 0-bits in x, starting at the least significant bit position. If x is 0, the result is undefined */
#define ctz(v)				__builtin_ctz(v)
static inline u32 ctz64(u64 v)
{
	u32 *_v = (u32 *)&v;

	if (_v[0])
		return ctz(_v[0]);
	else
		return ctz(_v[1]) + 32;
}

/*! @brief take ceilling of x divided by r */
#define occupied_by(x, r)		(((x) + (r) - 1) / (r))
/*! @brief round up x by 2 to the power of r */
#define round_up_by_2_power(x, r)	(((x) + (r) - 1) & (~((r) - 1)))

#define aligned_up(x, r) (((x) + (r)-1) / (r) * (r))
#define aligned_down(x, r) ((x) / (r) * (r))


#define ptr_inc(addr, inc)		((void *)(((u8*)addr) + inc))

#define fast_div_3(x)	 (u32) ((((u64)x) * (u64)0xAAAAAAAB) >> 33)

void *sys_malloc(mem_type_t type, u32 size);
void sys_free(mem_type_t type, void *ptr);
void* sys_malloc_aligned(mem_type_t type, u32 size, u32 aligned);
void sys_free_aligned(mem_type_t type, void *ptr);
void panic_dump_add(panic_dump_t *dump);

static inline void *malloc(u32 size)
{
	return sys_malloc(SLOW_DATA, size);
}

static inline void free(void *ptr)
{
	sys_free(SLOW_DATA, ptr);
}

static inline u32 get_next_power_of_two(u32 x)
{
	x--;
	x |= (x >> 1);  /* handle  2 bit numbers */
	x |= (x >> 2);  /* handle  4 bit numbers */
	x |= (x >> 4);  /* handle  8 bit numbers */
	x |= (x >> 8);  /* handle 16 bit numbers */
	x |= (x >> 16); /* handle 32 bit numbers */
	x++;

	return x;
}

static inline bool is_power_of_two(u32 x)
{
	return (x & (x - 1)) == 0;
}

/**
 * This uses fewer arithmetic operations than any other known
 * implementation on machines with slow multiplication.
 * This algorithm uses 17 arithmetic operations.
 *
 * from: https://en.wikipedia.org/wiki/Hamming_weight
 */
static __attribute__((__unused__)) u32 pop32(u32 x)
{
	static u32 m1 = 0x55555555; //binary: 0101...
	static u32 m2 = 0x33333333; //binary: 00110011..
	static u32 m4 = 0x0f0f0f0f; //binary:  4 zeros,  4 ones ...

	x -= (x >> 1) & m1;         //put count of each 2 bits into those 2 bits
	x = (x & m2) + ((x >> 2) & m2); //put count of each 4 bits into those 4 bits
	x = (x + (x >> 4)) & m4;    //put count of each 8 bits into those 8 bits
	x += x >> 8;  //put count of each 16 bits into their lowest 8 bits
	x += x >> 16;  //put count of each 32 bits into their lowest 8 bits
	return x & 0x7f;
}

static __attribute__((__unused__)) u32 pop64(u64 x)
{
	u32 *_x = (u32*)&x;
	return pop32(_x[0]) + pop32(_x[1]);
}

static inline __attribute__((always_inline)) u32 fast_mod3(u32 x)
{
	x *= 0xAAAAAAAB;
	if ((x - 0x55555556) < 0x55555555)
		return 2;
	return x >> 31;
}

/*!
 * @brief control icache
 *
 * @param enable	true to enable icache
 *
 * @return		old setting
 */
bool icache_ctrl(bool enable);

/*! @} */
