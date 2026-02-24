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

/*!
 * @file ncl_exports.h
 *
 * @date 1 Feb 2017
 * @brief API header file for function exports.
 *
 */

#ifndef _NCL_EXPORTS_
#define _NCL_EXPORTS_

#include "ncl.h"
#include "ncl_err.h"
#include "eccu.h"
#include "ndcu.h"
#include "nand.h"
#if HAVE_MICRON_SUPPORT
#include "vendor/mu/mu_write_round.h"
#endif
#if HAVE_TSB_SUPPORT
#include "vendor/tsb/tsb_write_round.h"
#endif
#define nal_get_interleave		nand_interleave_num
#define nal_get_channels		nand_channel_num
#define nal_get_targets			nand_target_num
#define nal_plane_count_per_lun		nand_plane_num
#define nal_lun_count_per_dev		nand_lun_num
#define nal_get_tlc_pg_idx_in_wl	pda2pg_idx
#define nal_pda_to_tgt(pda)	        pda2ce(pda)
#define nal_pda_get_block_id(pda)	pda2blk(pda)


#if defined(MPC)
#if CPU_BE == CPU_ID || CPU_BE2 == CPU_ID
extern u32 pda2blk(pda_t pda);
extern pda_t nal_make_pda(u32 spb_id, u32 index);
extern u32 nal_pda_offset_in_spb(pda_t pda);
#else // CPU_BE == CPU_ID || CPU_BE2 == CPU_ID
extern struct nand_info_t shr_nand_info;
#undef nal_get_tlc_pg_idx_in_wl
static inline u8 nal_get_tlc_pg_idx_in_wl(pda_t pda)
{
	u32 page;
	page = (pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask;
#if QLC_SUPPORT
	return page & 0x3;
#else // QLC_SUPPORT
#if HAVE_SAMSUNG_SUPPORT
	if (page < 8) {
		return (page % 2) + 1;
	} else if (page < 0x2F0) {
		return (page - 8) % 3;
	} else if (page < 0x2F8) {
		return ((page - 0x2F0) % 2) + 1;
	} else {
		return 2;
	}
#else // HAVE_SAMSUNG_SUPPORT
	return page % 3;
#endif // HAVE_SAMSUNG_SUPPORT
#endif // QLC_SUPPORT
}
static inline u32 pda2blk(pda_t pda)
{
	u32 block = (pda >> shr_nand_info.pda_block_shift) & shr_nand_info.pda_block_mask;
	return block;
}

static inline pda_t nal_make_pda(u32 spb_id, u32 index)
{
	pda_t pda = spb_id << shr_nand_info.pda_block_shift;

	pda += index;
	return pda;
}

static inline u32 nal_pda_offset_in_spb(pda_t pda)
{
	return pda & ((1U << shr_nand_info.pda_block_shift) - 1);
}
#undef nal_get_interleave
static inline u32 nal_get_interleave(void)
{
	return shr_nand_info.interleave;
}
#endif // CPU_BE == CPU_ID || CPU_BE2 == CPU_ID
#else
extern u32 pda2blk(pda_t pda);
extern u32 pda2plane(pda_t pda);
extern pda_t nal_make_pda(u32 spb_id, u32 index);
#endif //defined(MPC)

#define ficu_cmpl_fcmd_handling(a, b, c)	/* Don't you call me */

/*!
 * @brief get XLC core WL number for Micron B16/B17
 *
 * @param page		page number in physical block
 *
 * @return		1 or 2
 */
static inline u32 mu_get_xlc_cwl(u32 page)
{
	u32 cwl_pg_num;
	u32 remainder;
#if defined(MU_B27B)
#if 1
	if (page < 36) {
		if (page < 12)					//0 ~ 11
			cwl_pg_num = 1;
		else						//12 ~ 35
			cwl_pg_num = 2;
	} else if (page < 1608) {
		if (page < 60)					//36 ~ 59
			cwl_pg_num = 1;
		else {						//60 ~ 1607
			remainder = page % 3;
			cwl_pg_num = (remainder == 2) ? 1 : 2;
		}
	} else if (page < 1740) {
		if (page >= 1656 && page < 1692) {		//1656 ~  1691
			remainder = page % 3;
			cwl_pg_num = (remainder == 2) ? 1 : 2;
		} else						//1608 ~ 1655, 1692 ~ 1739
			cwl_pg_num = 2;
	} else if (page < 1788) {
		if (page >= 1752 && page < 1776)		//1752 ~ 1775
			cwl_pg_num = 2;
		else						//1740 ~ 1751, 1776 ~ 1787
			cwl_pg_num = 1;
	} else {
		if (page >= 3372 && page < 3420)		//3372 ~ 3419
			cwl_pg_num = 2;
		else {						//1788 ~ 3371, 3420 ~ 3455
			remainder = page % 3;
			cwl_pg_num = (remainder == 2) ? 1 : 2;
		}
	}
#else
	if (page < 36) {
		if (page < 12)					//0 ~ 11
			cwl_pg_num = 1;
		else						//12 ~ 35
			cwl_pg_num = 2;
	} else if (page < 1644) {
		if (page < 60)					//36 ~ 59
			cwl_pg_num = 1;
		else {						//60 ~ 1607
			remainder = page % 3;
			cwl_pg_num = (remainder == 2) ? 1 : 2;
		}
	} else if (page < 1716) {
		cwl_pg_num = 2;
	} else if (page < 1728) {
		cwl_pg_num = 1;
	} else if (page < 1752) {
		cwl_pg_num = 2;
	} else if (page < 1764) {
		cwl_pg_num = 1;
	} else if (page < 1790) {
		cwl_pg_num = 2;
	} else if (page < 3408) {
		remainder = page % 3;
		cwl_pg_num = (remainder == 2) ? 1 : 2;
	} else {
		cwl_pg_num = 2;
	}
#endif
#else
	if (page < 12 || page >= 2292) {
		cwl_pg_num = 1;
	} else {
		if (page < 36 || page >= 2220) {
			cwl_pg_num = 2;
		} else {
			if (page < 60) {
				cwl_pg_num = 1;
			} else {
				remainder = page % 3;
				cwl_pg_num = (remainder == 2) ? 1 : 2;
			}
		}
	}
#endif
	return cwl_pg_num;
}

#endif
