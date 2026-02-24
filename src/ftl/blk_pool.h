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

//=============================================================================
//
/*! \file blk_pool.h
 * @brief define physical block pool
 *
 * \addtogroup ftl
 *
 * @{
 */
#pragma once

#define FTL_PBLK_MAX	128			///< max block in pool
#define FTL_PBLK_MIN	32			///< min block in pool

typedef struct _phy_blk_t {
	pblk_t pblk;				///< physical block spb and interleave id

	union {
		u32 all;
		struct {
			u32 erase_cnt : 24;	///< erase count
			u32 rsvd : 5;
			u32 erasing : 1;	///< this block is erasing
			u32 used : 1;		///< this block is using
			u32 defect : 1;		///< this block can't be used anymore
		} b;
	} attr;
} phy_blk_t;

struct _log_meta_t;

/*!
 * @brief get block pool size to be saved
 *
 * @return	byte number
 */
u32 get_blk_pool_size(void);

/*!
 * @brief get root block
 *
 * @return	root block spb and interleave id
 */
pblk_t get_pblk_root(void);

/*!
 * @brief block pool initialization, allocate memory in tcm
 *
 * @param cnt		physical block count
 * @param pblk		union u32 of pblk, including spb_id and interleave id
 *
 * @return		not used
 */
void blk_pool_init(u32 cnt, u32 pblk);

/*!
 * @brief erase all block in block pool, only be called when FRB was virgin, all block will be valid
 *
 * @note non-virgin boot, blk_pool_scan should be used to find valid block
 *
 * @return	not used
 */
void blk_pool_clean(void);

/*!
 * @brief copy block pool table to buffer, called from we would like to save it to nand
 *
 * @param buf	buffer to be copied to
 *
 * @return	not used
 */
void blk_pool_copy(void *buf);

/*!
 * @brief copy block pool table into runtime tcm table
 *
 * @param buf	buffer to be copied, it should be read from nand
 *
 * @return	not used
 */
void blk_pool_recovery(phy_blk_t *buf);

/*!
 * @brief scan all block in block pool to get signature, expect last one
 *
 * @param data		data pointer
 * @param meta		meta pointer
 * @param meta_idx	index meta id
 *
 * @return		not used
 */
void blk_pool_rescan(void *data, struct _log_meta_t *meta, u32 meta_idx);

/*!
 * @brief search block pool for specific
 * @param pblk		pblk buffer list
 * @param sig		signature to be searched
 * @param cnt		buffer list length
 *
 * @return		found count
 */
u32 blk_pool_search(pblk_t *pblk, u32 sig, u32 cnt);

/*!
 * @brief drop all not used blocks
 *
 * @param erase		erase immediately
 *
 * @return		not used
 */
void blk_pool_drop_all(bool erase);

/*!
 * @brief drop all useless physical blocks
 *
 * @note blk_pool_rescan will find all using block in pool, and owner should use blk_pool_search
 * to handle them all, for those pblk without handled, will be dropped in this function call
 *
 * @return		not used
 */
void blk_pool_drop_useless(void);

/*! @} */
