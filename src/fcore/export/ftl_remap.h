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

/*! \file ftl_remap.h
 * @brief define ftl remap spb translation
 *
 * FTL will compose partial SPB which has some bad blocks in it to a good SPB.
 * A good SPB which mean has no defect block in it.
 *
 * \addtogroup fcore
 * \defgroup ftl_remap
 * \ingroup fcore
 * @{
 */
#pragma once

#include "bf_mgr.h"
#include "ncl_exports.h"
#include "fc_export.h"
#include "ftl_export.h"
#include "string.h"

/*!
 * @brief cached ftl remap table for a spb
 */
typedef struct _ftl_cache_remap_tbl_t {
	spb_id_t remap_tbl[MAX_INTERLEAVE_CNT_IN_FTL];	///< remap table for cached SPB
	spb_id_t cache_spb;				///< cached SPB
} ftl_cache_remap_tbl_t;

extern ftl_remap_t ftl_remap;				///< ftl remap table is global shared

/*!
 * @brief to check if a spb was remapped or not
 *
 * @param spb_id	target spb to be checked
 *
 * @return		return true if this spb is remapped
 */
static inline bool is_remapped_spb(spb_id_t spb_id)
{
	return (ftl_remap.remap_idx[spb_id] == 0xFFFF) ? false : true;
}

/*!
 * @brief remap a pda if that block was remapped
 *
 * @param pda		target pda
 *
 * @return		return accessible pda
 */
static inline pda_t ftl_remap_pda(pda_t pda)
{
	spb_id_t spb_id = nal_pda_get_block_id(pda);
	u32 remap_idx = ftl_remap.remap_idx[spb_id];

	if (remap_idx == 0xFFFF)
		return pda;

	u32 iid = pda >> DU_CNT_SHIFT;

	iid &= (ftl_remap.interleave - 1);
	spb_id_t t = ftl_remap.remap_tbl[remap_idx * ftl_remap.interleave + iid];
	if (t == spb_id)
		return pda;

	//ftl_core_trace(LOG_ERR, 0, "pda %x -> %x \n", pda, (t << pda2spb_shf) | (pda & ((1 << pda2spb_shf) - 1)));
	return nal_make_pda(t, nal_pda_offset_in_spb(pda));
}

/*!
 * @brief translate remapped PDA to origin PDA if possible
 *
 * @param pda	target PDA
 *
 * @return	return origin PDA
 */
static inline pda_t ftl_re_remap_pda(pda_t pda)
{
	spb_id_t spb_id = nal_pda_get_block_id(pda);
	u32 iid = pda >> DU_CNT_SHIFT;
	u32 i;

	// check if this block was shown in remap table
	iid &= (ftl_remap.interleave - 1);
	for (i = 0; i < ftl_remap.remap_cnt; i++) {
		if (ftl_remap.remap_tbl[ftl_remap.interleave * i + iid] == spb_id) {
			// the first block should be origin SPB
			spb_id = ftl_remap.remap_tbl[ftl_remap.interleave * i];
			//ftl_core_trace(LOG_ERR, 0, "re-remap %x -> %x \n", pda, nal_make_pda(spb_id, nal_pda_offset_in_spb(pda)));
			return nal_make_pda(spb_id, nal_pda_offset_in_spb(pda));
		}
	}

	return pda;
}

/*!
 * @brief get remap table for a given spb if it had
 *
 * @param spb_id	target spb_id
 * @param remap_tbl	remap table buffer, can't be NULL
 *
 * @return		return true if it had remap table, or return false
 */
static inline bool ftl_get_remap_tbl(spb_id_t spb_id, spb_id_t *remap_tbl)
{
	u32 remap_idx = ftl_remap.remap_idx[spb_id];
	u32 sz;

	if (remap_idx == 0xFFFF)
		return false;

	sz = ftl_remap.interleave * sizeof(spb_id_t);
	memcpy(remap_tbl, &ftl_remap.remap_tbl[remap_idx * ftl_remap.interleave], sz);
	return true;
}

/*!
 * @brief setup cached remap table for a given spb
 *
 * @note even this spb was not remapped, cached remap table will be initialized also
 *
 * @param spb_id	cached SPB
 * @param tbl		cached remap table
 *
 * @return		not used
 */
static inline void ftl_get_cached_remap_tbl(spb_id_t spb_id, ftl_cache_remap_tbl_t *tbl)
{
	u32 remap_idx;

	if (tbl->cache_spb == spb_id)
		return;

	remap_idx = ftl_remap.remap_idx[spb_id];
	if (remap_idx == 0xFFFF) {
		u32 i = 0;

		do {
			tbl->remap_tbl[i] = spb_id;
		} while (++i < ftl_remap.interleave);
	} else {
		u32 sz = ftl_remap.interleave * sizeof(spb_id_t);

		memcpy(tbl->remap_tbl, &ftl_remap.remap_tbl[remap_idx * ftl_remap.interleave], sz);
	}
	tbl->cache_spb = spb_id;
}

/*!
 * @brief translated to accessible pda from cached remap table
 *
 * @note if cache miss, cached remap table will be updated
 *
 * @param pda	target pda
 * @param tbl	cached remap table
 *
 * @return	accessible pda
 */
static inline pda_t ftl_get_cached_remap_pda(pda_t pda, ftl_cache_remap_tbl_t *tbl)
{
	spb_id_t spb_id = nal_pda_get_block_id(pda);
#ifndef SKIP_MODE
    return pda;
#endif
	if (!is_remapped_spb(spb_id))
		return pda;

	if (spb_id != tbl->cache_spb) {
		bool ret = ftl_get_remap_tbl(spb_id, tbl->remap_tbl);
		sys_assert(ret);
		tbl->cache_spb = spb_id;
	}

	u32 index = nal_pda_offset_in_spb(pda);
	u32 blk;

	blk = index >> DU_CNT_SHIFT;
	blk &= nal_get_interleave() - 1;
	spb_id = tbl->remap_tbl[blk];

	return nal_make_pda(spb_id, index);
}

/*!
 * @brief translate a pda list to accessible pda list via cached remap table
 *
 * @param pda_list	pda list
 * @param cnt		list length
 * @param tbl		cached remaped table
 *
 * @return		not used
 */
static inline void ftl_get_cached_remap_pda_list(pda_t *pda_list, u32 cnt, ftl_cache_remap_tbl_t *tbl)
{
	u32 i;

	for (i = 0; i < cnt; i++)
		pda_list[i] = ftl_get_cached_remap_pda(pda_list[i], tbl);
}

/*! @} */
