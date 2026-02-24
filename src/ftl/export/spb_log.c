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
/*! \file spb_log.c
 * @brief a log structure for spb information table, which included erase count and pb type
 *
 * \addtogroup spb_log
 *
 * @{
 *
 * SPB log uses physical nand block to store critical information, SPB info
 * table. This file defines the reconstruction method of SPB log, it allows
 * modules outside FTL to read erase count and pb type back.
 */
#include "sect.h"
#include "types.h"
#include "stdlib.h"
#include "ftltype.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl_exports.h"
#include "spb_log.h"
#include "sync_ncl_helper.h"
#include "assert.h"
#include "string.h"
#include "idx_meta.h"

/*! \cond PRIVATE */
#define __FILEID__ spblog
#include "trace.h"
/*! \endcond */

/*!
 * @brief Callback function to restore an old L2P table from latest page
 *
 * SPB log has a full L2P table in meta field, we can always get it.
 *
 * @param log_recon	log reconstruction parameter
 * @param meta		log meta of latest page
 * @param latest	PDA of latest page
 *
 * @return		Always return latest page PDA, since L2P table in meta
 */
static pda_t spb_log_read_l2pt(log_recon_t *log_recon, log_meta_t *meta,
	pda_t latest);

fast_data spb_log_t _spb_log = {
	LOG_INIT(&_spb_log.log),
	.spb_log_meta = NULL,  .meta_idx = 0,
	.ptr = NULL, .size = 0, .flags = 0,
	.flush_queue = 0
}; ///< spb log entity

init_code ftl_err_t spb_log_reconstruction(pblk_t pblk[2], void *ptr, u32 size)
{
	ftl_err_t ret = FTL_ERR_VIRGIN;
	struct target_geo_t *geo = &shr_nand_info.geo;
	u32 log_page_cnt;
	pda_t l2pt[NR_SPB_LOG_LUT];
	log_recon_t log_recon;
	bool res;

	log_page_cnt = occupied_by(size, shr_nand_info.page_sz);

	log_recon_init(&log_recon, l2pt, spb_log_read_l2pt, SPB_LOG_SIG);
	_spb_log.spb_log_meta = idx_meta_allocate(DU_CNT_PER_PAGE * log_page_cnt,
			DDR_IDX_META, &_spb_log.meta_idx);

	log_init(&_spb_log.log, log_page_cnt, geo->nr_pages / shr_nand_info.bit_per_cell, 0);

	_spb_log.ptr = ptr;
	_spb_log.size = size;

	if (pblk[0].spb_id == INV_SPB_ID && pblk[1].spb_id == INV_SPB_ID)
		goto end;	/* INV_SPB_ID */

	log_recon.pblk[0] = pblk[0];
	log_recon.pblk_cnt = 1;
	if (pblk[1].spb_id != INV_SPB_ID) {
		log_recon.pblk[1] = pblk[1];
		log_recon.pblk_cnt++;
	}
	//log_blk_decide(&_spb_log.log, &log_recon);

	res = log_reconstruction(&_spb_log.log, &log_recon);

	if (_spb_log.log.flush_id == 0 && res == false) { //spb log is virgin, not programed yet
		ftl_log_trace(LOG_ALW, 0xa94b, "spb log flush id is 0");
		panic("no this case");
		goto end;
	} else {
		sys_assert(res == true);
	}

	ret = FTL_ERR_OK;

	spb_log_recovery(&log_recon);

	log_blk_recycled(&_spb_log.log, &log_recon);
end:
	log_recon_rel(&log_recon);

	return ret;
}

init_code pda_t spb_log_read_l2pt(log_recon_t *log_recon, log_meta_t *meta,
	pda_t latest)
{
#if DU_CNT_PER_PAGE == 4
	spb_log_meta_t *spb_log_meta = (spb_log_meta_t*) meta;

	memcpy(log_recon->l2pt, spb_log_meta->l2pt.l2pt,
		sizeof(pda_t) * _spb_log.log.log_page_cnt);

	return latest;
#else
	return INV_PDA;
#endif
}

fast_code ftl_err_t spb_log_recovery(log_recon_t *log_recon)
{
	/* read all spb log back */
	u32 offset = 0;
	u32 total_size = _spb_log.size;
	pda_t *l2pt;
	void *data;
	u32 meta_idx;
	u32 i;

	l2pt = log_recon->l2pt;
	data = log_recon->data;
	meta_idx = log_recon->meta_idx;

	for (i = 0; i < _spb_log.log.log_page_cnt; i++) {
		u32 copy_size = min(total_size, (u32)NAND_PAGE_SIZE);
		nal_status_t ret;

		ret = ncl_read_one_page(l2pt[i], data, meta_idx);

		if (!ficu_du_data_good(ret)) {
			/* error handling */
		}

		memcpy(_spb_log.ptr + offset, data, copy_size);
		offset += copy_size;
		total_size -= copy_size;
	}

	/* update L2P table to meta field of each page*/
#if DU_CNT_PER_PAGE == 4
	for (i = 0; i < _spb_log.log.log_page_cnt; i++) {
		memcpy(&_spb_log.spb_log_meta[i].l2pt.l2pt[0], &l2pt[0],
			sizeof(pda_t) * _spb_log.log.log_page_cnt);
	}
#endif

	return FTL_ERR_OK;
}

/*! @} */
