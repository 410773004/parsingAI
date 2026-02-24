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
/*! \file helper.h
 * @brief helper function for dtag and meta in dispatcher
 *
 * \addtogroup dispatcher
 * \defgroup helper
 * \ingroup dispatcher
 * @{
 * help to get meta pointer or setup write/read payload
 */
#pragma once
#include "bf_mgr.h"
#include "queue.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "l2cache.h"

extern struct du_meta_fmt *dummy_meta;		///< dummy meta data
extern struct du_meta_fmt dtag_meta[];		///< dtag meta array
extern struct du_meta_fmt *ddtag_meta;		///< ddr dtag meta array
//extern struct du_meta_fmt *hdtag_meta;

//joe add sec size 20200817
extern u8 host_sec_bitz;
extern u16 host_sec_size;
//joe add sec size 20200817

#define get_meta_by_idx(idx)	(&dummy_meta[(idx)])	///< get index meta pointer by index

/*!
 * @brief get meta pointer by dtag id (24 bits)
 *
 * @param dtagid	dtag id
 *
 * @return		memory pointer of dtag, be careful ddr, only 1G
 */
static inline struct du_meta_fmt *get_meta_by_dtag(u32 dtagid)
{
#ifdef DDR
	if (dtagid & DTAG_IN_DDR_MASK)
		return (&ddtag_meta[dtagid & DDTAG_MASK]);
#endif
#ifdef HMB_DTAG
	if (dtagid & DTAG_IN_HMB_MASK)
		return (&dtag_meta[dtagid & HDTAG_MASK]);
#endif
	return (&dtag_meta[(dtagid)]);
}

/*!
 * @brief setup bm payload meta type: sram or ddr
 *
 * @param bm_pl		bm payload to be set
 *
 * @return		not used
 */
static inline void setup_dtag_meta_type(bm_pl_t *bm_pl)
{
#ifdef DDR
	if (bm_pl->pl.dtag & DTAG_IN_DDR_MASK) {
		bm_pl->pl.type_ctrl |= META_DDR_DTAG;
		return;
	}
#endif
	bm_pl->pl.type_ctrl |= META_SRAM_DTAG;
}

/*!
 * @brief api to setup write bm payload which using dtag meta
 *
 * @param bm_pl		bm payload to be setup
 * @param lda		lda
 * @param pda		pda for seed
 *
 * @return		not used
 */
static inline void dtag_meta_w_setup(bm_pl_t *bm_pl, lda_t lda, pda_t pda)
{
	struct du_meta_fmt *meta;

	if (bm_pl->pl.type_ctrl == BTN_NCB_QID_TYPE_CTRL_DDR_COPY_SEMI_STREAM) {
		meta = get_meta_by_dtag(bm_pl->semi_ddr_pl.semi_sram);
	} else {
		meta = get_meta_by_dtag(bm_pl->pl.dtag);
	}

	meta->seed_index = 0;
	meta->seed = pda;
	meta->lda = lda;
	meta->sn = 0x12345678;
	//meta->hlba = lda * NR_LBA_PER_LDA;
	if(host_sec_bitz==9)//joe add sec size 20200818
	meta->hlba=lda*8;//joe add sec size 20200817
	else
	meta->hlba=lda*1;//joe add sec size 20200817	
#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush((void *) meta, sizeof(struct du_meta_fmt));
#endif
	setup_dtag_meta_type(bm_pl);
}

/*!
 * @brief api to setup write bm payload which using index meta
 *
 * @param bm_pl			bm payload to be setup
 * @param lda			lda
 * @param pda			pda for seed
 * @param nvm_cmd_id		nvm cmd id
 */
static inline void idx_meta_w_setup(bm_pl_t *bm_pl, lda_t lda, pda_t pda, u32 nvm_cmd_id)
{
	struct du_meta_fmt *meta;

	meta = get_meta_by_idx(nvm_cmd_id);
	meta->seed_index = 0;
	meta->seed = pda;
	meta->lda = lda;
	meta->sn = 0x12345678;
	//meta->hlba = lda * NR_LBA_PER_LDA;
	if(host_sec_bitz==9)//joe add sec size 20200818
	meta->hlba=lda*8;//joe add sec size 20200817
	else
	meta->hlba=lda*1;//joe add sec size 20200817
	bm_pl->pl.nvm_cmd_id = nvm_cmd_id;
	bm_pl->pl.type_ctrl = META_SRAM_IDX;
}

/*!
 * @brief api to setup read bm payload which using dtag meta, pre-assign mode using
 *
 * @param bm_pl		bm payload to be setup
 *
 * @return		not used
 */
static inline void dtag_meta_r_setup(bm_pl_t *bm_pl)
{
	setup_dtag_meta_type(bm_pl);
	bm_pl->pl.type_ctrl |= DTAG_QID_DROP;
}

/*!
 * @brief api to setup bm payload which using index meta, steaming mode using
 *
 * @param bm_pl		bm payload to be setup
 * @param nvm_cmd_id	nvm cmd id of original command
 *
 * @return		not used
 */
static inline void idx_meta_r_setup(bm_pl_t *bm_pl, u32 nvm_cmd_id)
{
	bm_pl->pl.nvm_cmd_id = nvm_cmd_id;
	bm_pl->pl.type_ctrl = META_SRAM_IDX | DTAG_QID_DROP;
}

/*! @} */

