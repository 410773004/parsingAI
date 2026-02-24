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
/*! \file blk_log.c
 * @brief define physical block logs, the physical block table will be saved in this log
 *
 * \addtogroup ftl
 *
 * @{
 */
#include "ftlprecomp.h"
#include "log.h"
#include "log_flush.h"
#include "blk_pool.h"
#include "ftl_flash_geo.h"
#include "sync_ncl_helper.h"
#include "defect_mgr.h"
#include "idx_meta.h"
#include "ncl_cmd.h"
#include "blk_log.h"
#include "l2cache.h"

/*! \cond PRIVATE */
#define __FILEID__ pblk
#include "trace.h"
/*! \endcond */

#define BLK_LOG_SIG	 'LOG' //'BLOG' //AlanCC for wunc

/*! @brief block log data type */
typedef struct _blk_log_t {
	log_t log;			///< log type

	union {
		u32 all;
		struct {
			u32 flushing : 1;	///< blk log was flushing
			u32 pending : 1;	///< blk log flush was pended
			u32 erasing : 1;	///< root blk was erasing
		} b;
	} flags;
	void *buf;				///< io buffer
	u32 meta_idx;				///< meta index
	void *meta;				///< meta buffer
} blk_log_t;

fast_data_ni static ncl_page_res_t blk_log_cmd;	///< command resource of block log
fast_data_zi static blk_log_t _blk_log;		///< block log
fast_data u8 evt_flush_blk_log = 0xFF;		///< event to flush block log
ps_data pda_t pmu_pda = INV_PDA;		///< block log was limited to one page, need one pda for pmu

/*!
 * @brief block log flush event handler, flush block log
 *
 * @param p0	not used
 * @param p1	not used
 * @param p2	not used
 *
 * @return	not used
 */
static void blk_log_flush_evt(u32 p0, u32 p1, u32 p2);

init_code void blk_log_start(bool frb_valid)
{
	log_recon_t log_recon;
	pda_t pda[1];
	u32 log_page_cnt = occupied_by(get_blk_pool_size(), NAND_DU_SIZE * DU_CNT_PER_PAGE);
	phy_blk_t *buf;

	sys_assert(log_page_cnt == 1);

	evt_register(blk_log_flush_evt, 0, &evt_flush_blk_log);

	log_recon_init(&log_recon, pda, NULL, BLK_LOG_SIG);

	log_init(&_blk_log.log, log_page_cnt, get_slc_page_per_block(), LOG_F_SINGLE);

	log_recon.pblk[0] = get_pblk_root();	// it's fixed
	log_recon.pblk_cnt = 1;

	if (frb_valid == true){
	//if (frb_valid == true && log_blk_decide(&_blk_log.log, &log_recon)) {
		bool ret = log_reconstruction(&_blk_log.log, &log_recon);
		nal_status_t r;

		sys_assert(ret);
		pmu_pda = pda[0];

		r = ncl_read_one_page(pda[0], log_recon.data, log_recon.meta_idx);
		sys_assert(r == 0);
		// todo: EH
		buf = log_recon.data;
		blk_pool_recovery(buf);
		blk_pool_rescan(log_recon.data, log_recon.meta, log_recon.meta_idx);
	} else {
		// erased, use backup in FRB
		ftl_apl_trace(LOG_ALW, 0x1362, "blk log restore from frb");
		_blk_log.log.cur[0] = log_recon.pblk[0];
		_blk_log.log.next_page = 0;
		_blk_log.log.flush_id = 0;
		buf = get_pblk_tbl();
		blk_pool_recovery(buf);
		if (frb_valid == false)
			blk_pool_clean();
		else
			blk_pool_rescan(log_recon.data, log_recon.meta, log_recon.meta_idx);
	}

	log_recon_rel(&log_recon);

	_blk_log.buf = ftl_get_io_buf(DU_CNT_PER_PAGE);
	_blk_log.meta = idx_meta_allocate(DU_CNT_PER_PAGE, DDR_IDX_META, &_blk_log.meta_idx);
}

fast_code void blk_log_op_done(struct ncl_cmd_t *ncl_cmd)
{
	if (ncl_cmd->status == 0) {
		if (ncl_cmd->op_code == NCL_CMD_PROGRAM_TABLE) {
			_blk_log.flags.b.flushing = 0;
		} else {
			_blk_log.flags.b.erasing = 0;
			_blk_log.log.next_page = 0;
			_blk_log.flags.b.pending = 1;
		}

		if (_blk_log.flags.b.pending)
			evt_set_cs(evt_flush_blk_log, 0, 0, CS_TASK);
	} else {
		panic("todo");
	}
}

fast_code void blk_log_erase(void)
{
	struct ncl_cmd_t *ncl_cmd;
	ncl_cmd = &blk_log_cmd.ncl_cmd;
	ncl_cmd->addr_param.common_param.info_list = blk_log_cmd.info;
	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->addr_param.common_param.pda_list = blk_log_cmd.pda;
	blk_log_cmd.info[0].pb_type = NAL_PB_TYPE_SLC;
	blk_log_cmd.pda[0] = blk_page_make_pda(_blk_log.log.cur[0].spb_id, _blk_log.log.cur[0].iid, 0);
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->completion = blk_log_op_done;
	ncl_cmd->flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;

	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->status = 0;

	ncl_cmd_submit(ncl_cmd);
}

fast_code void blk_log_flush(void)
{
	struct ncl_cmd_t *ncl_cmd;

	if (_blk_log.flags.b.flushing || _blk_log.flags.b.erasing) {
		_blk_log.flags.b.pending = 1;
		return;
	}

	_blk_log.flags.b.pending = 0;

	ncl_cmd = &blk_log_cmd.ncl_cmd;
	ncl_cmd->addr_param.common_param.info_list = blk_log_cmd.info;
	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->addr_param.common_param.pda_list = blk_log_cmd.pda;

	memset(&blk_log_cmd.info[0], 0, sizeof(struct info_param_t));
	u32 i;
	dtag_t dtag = mem2dtag(_blk_log.buf);

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		bm_pl_t *bm_pl = &blk_log_cmd.bm_pl[i];

		ftl_wr_bm_pl_setup(bm_pl, dtag.dtag + i);
		ftl_prea_idx_meta_setup(bm_pl, _blk_log.meta_idx + i);
	}
	blk_log_cmd.info[0].pb_type = NAL_PB_TYPE_SLC;

	u32 ret = log_block_switch_check(&_blk_log.log, 1);

	if (ret == 0) {
		// update pblk table in defect map
		update_pblk_tbl();
		blk_log_erase();
		_blk_log.flags.b.erasing = 1;
		return;
	}

	_blk_log.flags.b.flushing = 1;
	blk_pool_copy(_blk_log.buf);
	log_page_allocate(&_blk_log.log, blk_log_cmd.pda, NULL, 1);

	log_meta_setup((log_meta_t*)_blk_log.meta, BLK_LOG_SIG, _blk_log.log.flush_id, _blk_log.log.cur[0],
			0, 0, 0, blk_log_cmd.pda[0]);

	_blk_log.log.flush_id++;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->completion = blk_log_op_done;
	ncl_cmd->flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;
	ncl_cmd->user_tag_list = blk_log_cmd.bm_pl;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->status = 0;

	ftl_apl_trace(LOG_INFO, 0xb494, "blk log flush at %x", blk_log_cmd.pda[0]);
#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush(_blk_log.buf, DU_CNT_PER_PAGE * DTAG_SZE);
	l2cache_mem_flush(_blk_log.meta, sizeof(log_meta_t));
#endif
	ncl_cmd_submit(ncl_cmd);
}

fast_code void blk_log_flush_evt(u32 p0, u32 p1, u32 p2)
{
	blk_log_flush();
}

/*! @} */
