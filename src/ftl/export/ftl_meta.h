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
/*! \file ftl_meta.h
 * @brief export ftl interface, define FTL meta setup
 *
 * \addtogroup ftl_export
 * @{
 */
#pragma once
#include "eccu.h"
#include "ncl.h"
#include "mpc.h"

#define DU_CNT_MASK			(DU_CNT_PER_PAGE - 1)		///< mask for DU count
#define META_SEED_MASK			(0xF7FF & (~DU_CNT_MASK))	///< meta seed mask, 4K DU is 0x7FC
#define META_SEED_ERASE_BIT_SFT		(11)

/*!
 * @brief this API is how we choice meta seed
 *
 * @param pda		address of DU
 *
 * @return		return seed
 */
static inline u16 meta_seed_setup(u32 pda)
{
	extern struct nand_info_t shr_nand_info;
	u16 seed;
	//spb_id_t spb_id = nal_pda_get_block_id(pda);

	seed = (pda >> shr_nand_info.pda_interleave_shift) & META_SEED_MASK;
	seed |= pda & DU_CNT_MASK;
	//seed |= spb_info_get(spb_id)->erase_cnt << META_SEED_ERASE_BIT_SFT;
	return seed;
}
#ifdef LJ1_WUNC 
static inline void set_pdu_bmp(dtag_t dtag, u8 bmp) 
{
    extern void *shr_ddtag_meta;
	extern void *shr_dtag_meta;
	struct du_meta_fmt *meta;
	
	meta = dtag.b.in_ddr ? shr_ddtag_meta : shr_dtag_meta;
    
     meta[dtag.b.dtag].wunc.WUNC = bmp;
}
static inline u8 *get_pdu_bmp(dtag_t dtag)
{
	extern void *shr_ddtag_meta;
	extern void *shr_dtag_meta;
	struct du_meta_fmt *meta;
	
	meta = dtag.b.in_ddr ? shr_ddtag_meta : shr_dtag_meta;
	
	return &meta[dtag.b.dtag].wunc.WUNC;
}

#endif
/*! @} */
