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

/*! \file idx_meta.h
 * @brief define index meta initialization and allocation
 *
 * \addtogroup utils
 * \defgroup idx_meta
 * \ingroup idx_meta
 * @{
 */

#ifdef MPC

#define PAD_META_TOTAL	16			///< total number of padding meta, used in scheduler
#ifndef DUAL_BE
#define PDA_META_IN_GRP PAD_META_TOTAL
#else
#define PDA_META_IN_GRP	(PAD_META_TOTAL / 2)
#endif
enum {
	SRAM_IDX_META = 0,	///< sram index buffer, used for SRAM index mode
	DDR_IDX_META,     	///< ddr index buffer, used for DDR index mode
	MAX_IDX_META      	///<
};

extern void *shr_idx_meta[MAX_IDX_META];

/*!
 * @brief initialize index meta buffer, and set it to NCL
 *
 * @return	none
 */
void idx_meta_init(void);

/*!
 * @brief allocate index meta buffer, this function has spin lock protection
 *
 * @param req	required count
 * @param type	index meta type, SRAM_IDX_META or DDR_IDX_META
 * @param idx	return start index of meta buffer
 *
 * @return	return pointer of allocated meta buffer
 */
void *idx_meta_allocate(u32 req, u32 type, u32 *idx);
#if (SPOR_FLOW == mENABLE)

/*!
 * @brief get shared meta buffer alloc count
 *
 * @param type	index meta type, SRAM_IDX_META or DDR_IDX_META
 *
 * @return	return shared meta buffer alloc count
 */
u32 idx_meta_get_shr_alloc_cnt(u32 type);
#endif
#endif

/*! @} */
