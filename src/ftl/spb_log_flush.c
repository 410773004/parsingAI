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
/*! \file spb_log_flush.c
 * @brief a log structure for spb information table, which included erase count and pb type
 *
 * \addtogroup spb_log
 *
 * @{
 *
 * SPB log uses physical nand block to store critical information, SPB info
 * table. This file defines the reconstruction method of SPB log, it allows
 * modules outside FTL to read erase count and pb type back.
 *
 * Flush method of SPB log is not exported.
 */
#include "ftlprecomp.h"
#include "queue.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "ncl_helper.h"
#include "spb_log.h"
#include "log_flush.h"
#include "spb_log_flush.h"
#include "spb_mgr.h"
#include "ftl_flash_geo.h"
#include "frb_log.h"
#include "ftl_ns.h"
#include "task.h"
#include "sync_ncl_helper.h"
#include "l2cache.h"

/*! \cond PRIVATE */
#define __FILEID__ spblogf
#include "trace.h"
/*! \endcond */

/*! @brief definition of SPB log flush resource */
typedef struct _spb_log_io_res_t {
	pda_t *pda;	///< pda list
	bm_pl_t *bm_pl_list;		///< bm payload list
	struct info_param_t *info_list;	///< ncl info list
	u32 page_id;			///< ~0 for flush all SPB log page, other is flushing specific page
	struct ncl_cmd_t *ncl_cmd;	///< ncl command resource
} spb_log_io_res_t;

ps_data pda_t pmu_l2pt[NR_SPB_LOG_LUT];	///< save l2p table of spb log when pmu enter

/*!
 * @brief Setup DTAG for all SPB log page
 *
 * @param bm_pl_list	BM payload buffer list
 * @param ptr		should be pointer of SPB info
 * @param page_cnt	number of SPB log page
 *
 * @return		none
 */
//static void spb_log_page_dtag_setup(bm_pl_t *bm_pl_list, void *ptr, u32 page_cnt); // Curry

/*!
 * @brief Setup META for all SPB log page
 *
 * @param meta		meta buffer pointer
 * @param cnt		page count of a single flush command
 * @param id		flush command id, increased by each flush commands
 * @param page_id	SPB log page ID
 * @param seed		start seed
 *
 * @return		not used
 */
static void spb_log_page_meta_setup(spb_log_meta_t *meta, u8 cnt, u32 id,
		u8 page_id, u32 seed);

/*!
 * @brief Callback function of SPB log flush command
 *
 * @param ncl_cmd	pointer to NCL command
 *
 * @return		not used
 */
static void spb_log_flush_done(struct ncl_cmd_t *ncl_cmd);

fast_data static spb_log_io_res_t _spb_log_io_res;	///< entity of flush resource

extern spb_log_t _spb_log;	///< entity in spb_log.c
/*
init_code void spb_log_flush_init(void)
{
	bm_pl_t *bm_pl_list;
	ncl_page_res_t *res;
	u32 i;

	res = share_malloc(sizeof(*res));
	bm_pl_list = res->bm_pl;
	spb_log_page_dtag_setup(bm_pl_list, _spb_log.ptr,
		_spb_log.log.log_page_cnt);

	_spb_log_io_res.bm_pl_list = bm_pl_list;
	_spb_log_io_res.info_list = res->info;
	_spb_log_io_res.pda = res->pda;
	_spb_log_io_res.ncl_cmd = &res->ncl_cmd;

	for (i = 0; i < NR_SPB_LOG_LUT_DU; i++) {
		_spb_log_io_res.info_list[i].pb_type = NAL_PB_TYPE_SLC;
		_spb_log_io_res.info_list[i].xlc.slc_idx = 0;
	}

	memset(_spb_log_io_res.ncl_cmd, 0, sizeof(*_spb_log_io_res.ncl_cmd));
}
*/
fast_code void spb_log_flush(u32 log_page_id)
{
	struct ncl_cmd_t *ncl_cmd;
	spb_log_meta_t *meta;
	bm_pl_t *bm_pl_list;
	u32 flush_page_cnt;
	u32 i;
	u32 flags;
	u32 meta_idx;

	if (_spb_log.flags & SPB_LOG_F_FLUSHING) {
		/* queue this log page ID because flushing or waiting new SPB  */
		if (_spb_log.flags & SPB_LOG_F_PENDED) {
			/* force to flush all, because there are two flush commands queued */
			_spb_log.flush_queue = ~0;
		} else {
			_spb_log.flags |= SPB_LOG_F_PENDED;
			_spb_log.flush_queue = log_page_id;
		}
		return;
	}

	if (_spb_log.log.cur[0].spb_id == INV_SPB_ID) {
		/* Curry
		blk_pool_get_pblk(&_spb_log.log.cur[0]);
		sys_assert(_spb_log.log.cur[0].spb_id != INV_SPB_ID);
		*/
	}

	ncl_cmd = _spb_log_io_res.ncl_cmd;

	flush_page_cnt = 1;
	if ((log_page_id == ~0) || (_spb_log.log.flags & LOG_F_FLUSH_ALL)){
		_spb_log.log.flags &= ~LOG_F_FLUSH_ALL;
		flush_page_cnt = _spb_log.log.log_page_cnt;
	}

	/* check if page resource is available */
	flush_page_cnt = log_block_switch_check(&_spb_log.log, flush_page_cnt);

	if (flush_page_cnt == 0) {
		_spb_log.flush_queue = ~0;
		panic("impossible");
	}

	if (flush_page_cnt > 1) {
		/* flush all */
		flush_page_cnt = _spb_log.log.log_page_cnt;
		log_page_id = 0;
		meta_idx = _spb_log.meta_idx;
		meta = _spb_log.spb_log_meta;
		bm_pl_list = _spb_log_io_res.bm_pl_list;
		_spb_log_io_res.page_id = ~0;
	} else {
		if (log_page_id == ~0)
			log_page_id = 0;

		meta_idx = _spb_log.meta_idx + log_page_id * DU_CNT_PER_PAGE;
		meta = &_spb_log.spb_log_meta[log_page_id];
		bm_pl_list = &_spb_log_io_res.bm_pl_list[page2du(log_page_id)];
		_spb_log_io_res.page_id = log_page_id;
	}

	for (i = 0; i < page2du(flush_page_cnt); i++) {
		bm_pl_list[i].pl.du_ofst = i;
		ftl_prea_idx_meta_setup(&bm_pl_list[i], i + meta_idx);
	}

	log_page_allocate(&_spb_log.log, _spb_log_io_res.pda, NULL, flush_page_cnt);

	spb_log_page_meta_setup(meta, flush_page_cnt,
			_spb_log.log.flush_id, log_page_id, _spb_log_io_res.pda[0]);

#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush((void *) _spb_log.spb_log_meta, _spb_log.log.log_page_cnt * sizeof(spb_log_meta_t));
	l2cache_mem_flush((void *) _spb_log.ptr, _spb_log.log.log_page_cnt * NAND_PAGE_SIZE);
#endif
	_spb_log.log.flush_id++;
	_spb_log.flags |= SPB_LOG_F_FLUSHING;

	ncl_cmd_setup_addr_common(&ncl_cmd->addr_param.common_param,
		_spb_log_io_res.pda, flush_page_cnt, _spb_log_io_res.info_list);

	flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd_prog_setup_helper(ncl_cmd, NCL_CMD_PROGRAM_TABLE,
			flags, bm_pl_list, spb_log_flush_done,
			&_spb_log_io_res);

	ncl_cmd_submit(ncl_cmd);
}

fast_code void spb_log_flush_done(struct ncl_cmd_t *ncl_cmd)
{
	if (ncl_cmd->status != 0) {
		// todo: error handling
		ftl_log_trace(LOG_ERR, 0xf2b2, "spb log flush err %x, %d",
				ncl_cmd->addr_param.common_param.pda_list[0],
				ncl_cmd->addr_param.common_param.list_len);
	}

	/* update LUT in each meta of SPB log page */
#if DU_CNT_PER_PAGE == 4
	u32 i;
	spb_log_io_res_t *res = (spb_log_io_res_t*) ncl_cmd->caller_priv;

	for (i = 0; i < _spb_log.log.log_page_cnt; i++) {
		u32 j;
		pda_t *l2pt = _spb_log.spb_log_meta[i].l2pt.l2pt;

		if (res->page_id == ~0) {
			for (j = 0; j < _spb_log.log.log_page_cnt; j++) {
				l2pt[j] = res->pda[j];
				pmu_l2pt[j] = l2pt[j];
			}
		} else {
			j = res->page_id;
			l2pt[j] = res->pda[0];
			pmu_l2pt[j] = res->pda[0];
		}
	}
#endif

	_spb_log.flags &= ~(SPB_LOG_F_FLUSHING);

	log_flush_done(&_spb_log.log);

	if (_spb_log.flags & SPB_LOG_F_PENDED) {
		u32 log_page_id = _spb_log.flush_queue;

		_spb_log.flags &= ~SPB_LOG_F_PENDED;
		spb_log_flush(log_page_id);
	}
}
/*
init_code void spb_log_page_dtag_setup(bm_pl_t *bm_pl_list, void *ptr,
	u32 page_cnt)
{
	u32 dtag_cnt = page2du(page_cnt);
	u32 valid_dtag_cnt = ftl_sram_sz_in_dtag(_spb_log.size);
	u32 i;
	u8 *p = (u8*) ptr;

	for (i = 0; i < valid_dtag_cnt; i++) {
		dtag_t dtag;

		dtag = mem2dtag(p);

		ftl_wr_bm_pl_setup(&bm_pl_list[i], dtag.dtag);

		p += NAND_DU_SIZE;
	}

	
	 // this loop is for simulation, because dtag may not be sequential
	 
	for ( ; i < dtag_cnt; i++) {
		dtag_t dtag;

		dtag = mem2dtag(p);

		ftl_wr_bm_pl_setup(&bm_pl_list[i], dtag.dtag);
		p += NAND_DU_SIZE;
	}
}
*/
inline void spb_log_page_meta_setup(spb_log_meta_t *meta, u8 cnt, u32 id,
		u8 page_id, u32 seed)
{
	u32 idx = 0;

	do {
		log_meta_setup((log_meta_t*)&meta[idx], SPB_LOG_SIG, id, _spb_log.log.cur[0],
				idx, cnt, page_id, seed);
		seed += DU_CNT_PER_PAGE;
	} while (++idx < cnt);
}

fast_code void spb_log_resume(void)
{
	log_recon_t log_recon;

	log_recon_init(&log_recon, pmu_l2pt, NULL, SPB_LOG_SIG);
	spb_log_recovery(&log_recon);
	log_recon_rel(&log_recon);
}

fast_code void spb_log_dump(void)
{
	ftl_syslog_trace(LOG_ALW, 0x1148, "system log, %x, next %x, next page %d",
			_spb_log.log.cur[0].pblk, _spb_log.log.next[0].pblk,
			_spb_log.log.next_page);
}

/*! @} */
