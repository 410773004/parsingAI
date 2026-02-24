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
/*! \file spb_pool.h
 * @brief define spb pool
 *
 * \addtogroup spb_pool
 *
 * @{
 * We only have four pool for spb, reserved, slc, native, mix.
 * 1. Reserved pool, is not allocated, not used spb pool
 * 2. slc is only be used in slc mode
 * 3. native is only be used in native mode
 * 4. mix can be used as slc or native which was determined when allocated
 */
#pragma once

#define FTL_QBT_TAG    (0x05142500)
enum {
	SPB_POOL_UNALLOC = 0,
	SPB_POOL_USER,		//1
	SPB_POOL_GC,		//2
	SPB_POOL_GCD,		//3
	SPB_POOL_FREE,		//4
	SPB_POOL_QBT_ALLOC, //5 used QBT
	SPB_POOL_QBT_FREE,  //6 not used QBT
	SPB_POOL_PBT,		//7
	SPB_POOL_PBT_ALLOC,	//8
	SPB_POOL_EMPTY,		//9
	SPB_POOL_MAX    	//10
};

enum
{
    FTL_SORT_NONE=0,
    FTL_SORT_BY_EC,
    FTL_SORT_BY_SN,
    FTL_SORT_BY_IDX,
    FTL_SORT_MAX
};

#define FTL_ZERO_DEFECT_BLOCK_CNT   (1)


/*!
 * @brief initial spb pool
 * Seperate and classify spb by defect count
 *
 * @return	not used
 */
void spb_pool_init(void);

/*!
 * @brief allocate spb from reserved pool to pool pool_id
 *
 * @param min_spb_cnt	minimal spb to be allocated
 * @param max_spb_cnt	maximal spb to be allocated
 * @param pool_id	pool id to be allocated
 *
 * @return		allocated count
 */
u32 spb_pool_alloc(u32 min_spb_cnt, u32 max_spb_cnt, u8 pool_id);
u32 spb_pool_unalloc(u32 min_spb_cnt, u32 max_spb_cnt, u8 pool_id);

/*!
 * @brief api to get good spb count in pool
 *
 * @param pool_id	pool id to be got
 *
 * @return		good spb count of pool
 */
u32 spb_pool_get_good_spb_cnt(u32 pool_id);

/*!
 * @brief api to get total spb count in pool
 *
 * @param pool_id	pool id to be got
 *
 * @return		total spb count of pool
 */
u32 spb_pool_get_ttl_spb_cnt(u32 pool_id);

/*! @} */
