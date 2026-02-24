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
/*! \file fe_info.c
 * @brief define all fe information which was stored in ftl
 *
 * \addtogroup ftl
 *
 * @{
 */
#include "ftlprecomp.h"
#include "log.h"
#include "log_flush.h"
#include "l2cache.h"
#include "blk_pool.h"
#include "sync_ncl_helper.h"
#include "ftl_flash_geo.h"
#include "idx_meta.h"
#include "ncl_cmd.h"
#include "system_log.h"

/*! \cond PRIVATE */
#define __FILEID__ felog
#include "trace.h"
/*! \endcond */

#define FE_LOG_SIG	'olf' //'golf'  //AlanCC for wunc

#define MAX_FE_LOG_PAGE_SIZE	NAND_PAGE_SIZE

typedef struct _fe_log_t {
	log_t log;

	pda_t pda[1];	///< power on latest version
	void *data;
	void *meta;
	u32 meta_idx;

	union {
		struct {
			u32 busy : 1;
		} b;
		u32 all;
	} flags;

	ncl_page_res_t log_cmd;	///< command resource of block log
} fe_log_t;

fast_data_zi static fe_log_t _fe_log;
share_data_zi volatile void *shr_fe_info;

fast_data_zi sys_log_desc_t _fe_ns_info_desc;		///< fe namespace info system log descriptor
fast_data_zi sld_flush_t _fe_ns_flush;			///< flush descriptor for fe namespace info

init_code void fe_log_init(void)
{
	log_recon_t log_recon;

	log_recon_init(&log_recon, _fe_log.pda, NULL, FE_LOG_SIG);
	log_recon.pblk_cnt = blk_pool_search(log_recon.pblk, FE_LOG_SIG, 2);
	log_init(&_fe_log.log, 1, get_slc_page_per_block(), 0);
	_fe_log.data = (void *) ftl_get_io_buf(DU_CNT_PER_PAGE);
	_fe_log.meta = (void *) idx_meta_allocate(DU_CNT_PER_PAGE, DDR_IDX_META, &_fe_log.meta_idx);
	_fe_log.flags.all = 0;

	shr_fe_info = _fe_log.data;

	if (log_recon.pblk_cnt == 0) {
		// no fe log blk
	virgin:
		//blk_pool_get_pblk(_fe_log.log.cur); //Curry
		ftl_apl_trace(LOG_ALW, 0xedf2, "fe log virgin %x", _fe_log.log.cur[0].pblk);
		_fe_log.pda[0] = INV_PDA;
		_fe_log.log.next_page = 0;
		_fe_log.log.flush_id = 0;
	} else {
		//log_blk_decide(&_fe_log.log, &log_recon);

		bool ret = log_reconstruction(&_fe_log.log, &log_recon);

		if (ret == false) {
			_fe_log.pda[0] = INV_PDA;
			blk_pool_put_pblk(_fe_log.log.cur[0]);
			goto virgin;
		}

		ftl_apl_trace(LOG_ALW, 0xa8da, "fe log %x", _fe_log.pda[0]);
	}
}

fast_code void fe_log_flush_done(struct ncl_cmd_t *ncl_cmd)
{
	fe_req_t *req = (fe_req_t *) ncl_cmd->caller_priv;

	if (ncl_cmd->status != 0)
		panic("todo");

	_fe_log.flags.b.busy = 0;
	req->cmpl(req);
	_fe_log.pda[0] = _fe_log.log_cmd.pda[0];
}

fast_code void fe_log_flush(fe_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd;
	u32 ret = log_block_switch_check(&_fe_log.log, 1);

	if (ret == 0) {
		panic("imp");
		return;
	}

	_fe_log.flags.b.busy = 1;

	ncl_cmd = &_fe_log.log_cmd.ncl_cmd;
	ncl_cmd->addr_param.common_param.info_list = _fe_log.log_cmd.info;
	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->addr_param.common_param.pda_list = _fe_log.log_cmd.pda;

	memset(&_fe_log.log_cmd.info[0], 0, sizeof(struct info_param_t));
	u32 i;
	dtag_t dtag = mem2dtag(_fe_log.data);

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		bm_pl_t *bm_pl = &_fe_log.log_cmd.bm_pl[i];

		ftl_wr_bm_pl_setup(bm_pl, dtag.dtag + i);
		ftl_prea_idx_meta_setup(bm_pl, _fe_log.meta_idx + i);
	}

	_fe_log.log_cmd.info[0].pb_type = NAL_PB_TYPE_SLC;

	log_page_allocate(&_fe_log.log, _fe_log.log_cmd.pda, NULL, 1);

	log_meta_setup((log_meta_t*)_fe_log.meta, FE_LOG_SIG, _fe_log.log.flush_id, _fe_log.log.cur[0],
			0, 0, 0, _fe_log.log_cmd.pda[0]);
#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush(_fe_log.data, DU_CNT_PER_PAGE * DTAG_SZE);
	l2cache_mem_flush(_fe_log.meta, sizeof(log_meta_t));
#endif

	_fe_log.log.flush_id++;
	ncl_cmd->caller_priv = req;
	ncl_cmd->completion = fe_log_flush_done;
	ncl_cmd->flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;
	ncl_cmd->user_tag_list = _fe_log.log_cmd.bm_pl;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->status = 0;

	ncl_cmd_submit(ncl_cmd);
}

fast_code void fe_log_read_done(struct ncl_cmd_t *ncl_cmd)
{
	fe_req_t *req = (fe_req_t *) ncl_cmd->caller_priv;

	if (ncl_cmd->status != 0) {
		memset(req->mem, req->size, 0xff);
		ftl_apl_trace(LOG_ERR, 0x81d6, "fe log read error");
		return;
	}

	_fe_log.flags.b.busy = 0;
	if (req->cmpl)
		req->cmpl(req);
}

fast_code void fe_log_read(fe_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd;
	u32 du_cnt;

	if (_fe_log.pda[0] == INV_PDA) {
		memset(req->mem, req->size, 0xff);
		req->cmpl(req);
		return;
	}
	_fe_log.flags.b.busy = 1;

	du_cnt = occupied_by(req->size, NAND_DU_SIZE);

	ncl_cmd = &_fe_log.log_cmd.ncl_cmd;
	ncl_cmd->addr_param.common_param.info_list = _fe_log.log_cmd.info;
	ncl_cmd->addr_param.common_param.list_len = du_cnt;
	ncl_cmd->addr_param.common_param.pda_list = _fe_log.log_cmd.pda;

	memset(&_fe_log.log_cmd.info[0], 0, du_cnt * sizeof(struct info_param_t));
	u32 i;
	dtag_t dtag = mem2dtag(req->mem);

	for (i = 0; i < du_cnt; i++) {
		bm_pl_t *bm_pl = &_fe_log.log_cmd.bm_pl[i];

		ftl_wr_bm_pl_setup(bm_pl, dtag.dtag + i);
		ftl_prea_idx_meta_setup(bm_pl, _fe_log.meta_idx + i);
		_fe_log.log_cmd.pda[i] = _fe_log.pda[0] + i;
		_fe_log.log_cmd.info[i].pb_type = NAL_PB_TYPE_SLC;
	}

	ncl_cmd->caller_priv = req;
	ncl_cmd->flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	if (req->cmpl) {
		ncl_cmd->completion = fe_log_read_done;
	} else {
		ncl_cmd->completion = NULL;
		ncl_cmd->flags |= NCL_CMD_SYNC_FLAG;
	}
	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
	ncl_cmd->user_tag_list = _fe_log.log_cmd.bm_pl;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->status = 0;


#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush(_fe_log.data, DU_CNT_PER_PAGE * DTAG_SZE);
	l2cache_mem_flush(_fe_log.meta, sizeof(log_meta_t));
#endif
	ncl_cmd_submit(ncl_cmd);

	if (req->cmpl == NULL)
		fe_log_read_done(ncl_cmd);
}

fast_code void fe_ns_info_load(fe_req_t *req)
{
	sys_assert(req->write == false);
	sys_assert(req->size <= FE_NS_INFO_MAX_SZ_PER_NS * FE_NS_MAX_CNT);

	memcpy(req->mem, _fe_ns_info_desc.ptr, req->size);

	req->cmpl(req);
}

fast_code void fe_ns_info_save_done(sld_flush_t *sld_flush)
{
	fe_req_t *req = (fe_req_t *) sld_flush->caller;

	sld_flush->caller = NULL;
	req->cmpl(req);
}

fast_code void fe_ns_info_save(fe_req_t *req)
{
	sys_assert(req->write == true);
	sys_assert(req->size <= FE_NS_INFO_MAX_SZ_PER_NS * FE_NS_MAX_CNT);
	sys_assert(_fe_ns_flush.caller == NULL); // no pending, so panic

	memcpy(_fe_ns_info_desc.ptr, req->mem, req->size);
	// Curry
	/*
	_fe_ns_flush.caller = req;
	_fe_ns_flush.cmpl = fe_ns_info_save_done;
	sys_log_desc_flush(&_fe_ns_info_desc, ~0, &_fe_ns_flush);
	*/
}

/*! @} */
