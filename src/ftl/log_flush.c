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
/*! \file log_flush.c
 * @brief define log data structure for system log and spb log
 *
 * \addtogroup log_flush
 * @{
 *
 * Log is a special area to use physical block to save critical data, it support
 * dual copy or not, this module defines the flush method
 */
#include "ftlprecomp.h"
#include "queue.h"
#include "ncl_exports.h"
#include "log.h"
#include "ncl_helper.h"
#include "log_flush.h"
#include "spb_mgr.h"
#include "ftl_flash_geo.h"

fast_code u32 log_block_switch_check(log_t *log, u32 flush_page_cnt)
{
	u32 remain_page = log->nr_page_per_blk - log->next_page;
	u32 loop = (log->flags & LOG_F_BACKUP) ? 2 : 1;
	u32 i;

	if (remain_page >= flush_page_cnt)
		return flush_page_cnt;

	if (log->flags & LOG_F_SINGLE)
		return 0;

	for (i = 0; i < loop; i++) {
		//blk_pool_get_pblk(&log->next[i]); // Curry
		sys_assert(log->next[i].spb_id != INV_SPB_ID);
	}

	pblk_t tmp[2];

	for (i = 0; i < loop; i++) {
		tmp[i] = log->cur[i];
		log->cur[i] = log->next[i];
		log->next[i] = tmp[i];
	}
	log->flags |= LOG_F_SWITCHED;
	log->next_page = 0;
	return log->log_page_cnt;
}

fast_code void log_page_allocate(log_t *log, pda_t *pda_list, pda_t *backup, u32 cnt)
{
	pda_t p = blk_page_make_pda(log->cur[0].spb_id, log->cur[0].iid, log->next_page);
	pda_t b = INV_PDA;
	u32 stripe = page2du(nal_get_interleave());
	u32 i = 0;

	if (backup)
		b = blk_page_make_pda(log->cur[1].spb_id, log->cur[1].iid, log->next_page);

	do {
		pda_list[i] = p;
		if (backup) {
			backup[i] = b;
			b += stripe;
		}
		p += stripe;
	} while (++i < cnt);
	log->next_page += cnt;
}

fast_code void log_flush_done(log_t *log)
{
	if (log->flags & LOG_F_SWITCHED) {
		log->flags &= ~LOG_F_SWITCHED;

		blk_pool_put_pblk(log->next[0]);
		log->next[0].spb_id = INV_SPB_ID;

		if (log->flags & LOG_F_BACKUP) {
			blk_pool_put_pblk(log->next[1]);
			log->next[1].spb_id = INV_SPB_ID;
		}
	}
}

/*! @} */

