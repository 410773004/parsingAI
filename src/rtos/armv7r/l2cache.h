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
#include "assert.h"
#include "spin_lock.h"

typedef enum {
	CACHE_LINE_64B = 0,
	CACHE_LINE_32B = 1,
} l2cache_line_t;

typedef enum {
	WRITE_BACK_AND_READ_WRITE_ALLOCATE = 2,
	WRITE_BACK_AND_READ_ALLOCATE = 3,		// no write allocate
	WRITE_THROUGH_AND_READ_WRITE_ALLOCATE = 4,
	WRITE_THROUGH_AND_READ_ALLOCATE = 5,	// no write allocate
} l2cache_type_t;

extern volatile u8 cache_line;

/*! @brief flush/invalidate type */
typedef enum {
	ADDRESS = 0,	///< DDR address format, 64B address
	WAYSET  = 1,	///< cache way index and set index format
} l2c_flush_type_t;

/*!
 * @brief l2 cache initialization
 *
 * @param l2cache_line		l2cache line size
 * @param l2cache_type		l2cache type
 *
 * @return			not used
 */
extern void l2cache_init(l2cache_line_t l2cache_line, l2cache_type_t l2cache_type);

/*!
 * @brief l2 cache enable or disable
 *
 * @param enable	true to enable l2 cache
 *
 * @return		return last status of l2 cache
 */
extern bool l2cache_enable(bool enable);

/*!
 * @brief flush all l2cache
 *
 * @return		not used
 */
extern void l2cache_flush_all(void);

/*!
 * @brief preload data from addr to L2cache until l2cache was full
 *
 * @param addr		ddr address offset
 *
 * @return		not used
 */
extern void l2cache_preload_all(u32 addr);

/*!
 * @brief invalidate all cache way
 *
 * @return		not used
 */
extern void l2cache_invalidate_all(void);

/*!
 * @brief submit l2cache flush command, the data in l2cache will be saved to ddr
 *
 * @param adr_type	address type, support DDR address or way address
 * @param addr_set	if adr_type == ADDRESS, it was DDR_ADDR, otherwise set index
 * @param way		if adr_type == ADDRESS, DONTCARE, otherwise way index
 *
 * @return		not used
 */
extern void l2c_submit_flush(l2c_flush_type_t adr_type, u32 addr_set, u32 way);

/*!
 * @brief submit l2cache invalidate command, the data in l2cache will be dropped
 *
 * @param adr_type	address type, support DDR address or way address
 * @param addr_set	if adr_type == ADDRESS, it was DDR_ADDR, otherwise set index
 * @param way		if adr_type == ADDRESS, DONTCARE, otherwise way index
 *
 * @return		not used
 */
extern void l2c_submit_invalidate(u32 adr_type, u32 addr_set, u32 way);

/*!
 * @brief api to make sure DDR address was flushed
 *
 * @param addr		DDR offset
 * @param len		bytes length
 *
 * @return		not used
 */
static inline void l2cache_flush(u64 addr, u32 len)
{
	u32 cline_shift = (CACHE_LINE_32B == cache_line) ? 5 : 6;
	u32 _addr;
	u32 _len = (len + (1 << cline_shift) - 1) >> cline_shift;
	u32 i = 0;

	if (CACHE_LINE_32B == cache_line)
		_addr = addr >> 5;
	else
		_addr = addr >> 6;

#if defined(MPC)
	spin_lock_take(SPIN_LOCK_KEY_L2C_OP, 0, true);
#endif
	do {
		l2c_submit_flush(ADDRESS, _addr, /*DONTCARE*/0);
		_addr++;
	} while (++i < _len);
#if defined(MPC)
	spin_lock_release(SPIN_LOCK_KEY_L2C_OP);
#endif
}

/*!
 * @brief api to make sure DDR memory pointer was flushed
 *
 * @param mem		must be DDR pointer
 * @param len		bytes length
 *
 * @return		not used
 */
static inline void l2cache_mem_flush(void *mem, u32 len)
{
	u32 addr = (u32) mem;

	sys_assert(addr >= DDR_BASE);
	addr -= DDR_BASE;

	l2cache_flush((u64)addr, len);
}

/*!
 * @brief api to invalidate DDR region
 *
 * @param addr		DDR offset
 * @param len		bytes length
 *
 * @return		not used
 */
static inline void l2cache_invalidate(u64 addr, u32 len)
{
	u32 cline_shift = (CACHE_LINE_32B == cache_line) ? 5 : 6;
	u32 _addr;
	u32 _len = (len + (1 << cline_shift) - 1) >> cline_shift;
	u32 i = 0;

	if (CACHE_LINE_32B == cache_line)
		_addr = addr >> 5;
	else
		_addr = addr >> 6;

	do {
		l2c_submit_invalidate(ADDRESS, _addr, /*DONTCARE*/0);
		_addr++;
	} while (++i < _len);
}

/*!
 * @brief api to invalidate DDR memory region
 *
 * @param mem		must be DDR pointer
 * @param len		bytes length
 *
 * @return		not used
 */
static inline void l2cache_mem_invalidate(void *mem, u32 len)
{
	u32 addr = (u32) mem;

	sys_assert(addr >= DDR_BASE);
	addr -= DDR_BASE;
	l2cache_invalidate(addr, len);
}

