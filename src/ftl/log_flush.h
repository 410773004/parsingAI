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
/*! \file log_flush.h
 * @brief define log data structure for system log and spb log
 *
 * \addtogroup log_flush
 * @{
 *
 * Log is a special area to use physical block to save critical data, it support
 * dual copy or not, this module defines the flush method
 */
#pragma once
#include "ftl_meta.h"

/*!
 * @brief Check if this log block had free page to be programmed
 *
 * If free page of current log block is not enough for flush_page_cnt,
 * log will switch to next log block, and return total page count in log to
 * flush all pages.
 *
 * @param log			log object
 * @param flush_page_cnt	how many pages to be flushed
 *
 * @return			return updated flush page count, or flush_page_cnt
 */
u32 log_block_switch_check(log_t *log, u32 flush_page_cnt);

/*!
 * @brief Assign PDA to be programmed in log block
 *
 * @param log		log object
 * @param pda_list	pda list to be assigned
 * @param backup	backup pda list if this block has dual copy
 * @param cnt		page count to be allocated
 *
 * @return		none
 */
void log_page_allocate(log_t *log, pda_t *pda_list, pda_t *backup, u32 cnt);

/*!
 * @brief Setup meta of log page, it should be header of log page meta
 *
 * @param meta		meta pointer
 * @param sig		signature of this log
 * @param id		flush ID of this flush
 * @param backup	backup block
 * @param idx		index of page in this flush
 * @param cnt		total flushed page in this flush
 * @param log_page_id	log page ID of this log page
 * @param seed		start seed
 *
 * @return		none
 */
static inline void log_meta_setup(log_meta_t *meta, u32 sig, u32 id,
	pblk_t backup, u32 idx, u32 cnt, u32 log_page_id, u32 seed)
{
	meta->meta0.seed = meta_seed_setup(seed);
	++seed;
	meta->meta0.signature = sig;
	meta->meta0.flush_id = id;
	meta->meta0.backup = backup;

	meta->meta1.seed = meta_seed_setup(seed);
	++seed;
	meta->meta1.flush_idx = idx;
	meta->meta1.flush_cnt = cnt;
	meta->meta1.log_page_id = log_page_id;
#if DU_CNT_PER_PAGE == 4
	meta->meta2.seed = meta_seed_setup(seed);
	++seed;
	meta->meta3.seed = meta_seed_setup(seed);
	++seed;
#endif

#ifdef LJ1_WUNC
		meta->meta1._rsvd = 0;
		meta->meta2.payload[0] = 0;
		meta->meta3.payload[0] = 0;
#endif

}

/*!
 * @brief Helper function for log flush done callback
 *
 * If flush was for SPB switch, handle SPB switch done in this callback
 *
 * @param log	log object
 *
 * @return	none
 */
void log_flush_done(log_t *log);

/*! @} */
