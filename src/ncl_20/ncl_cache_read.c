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
/*! \file ncl_cache_read.c
 * @brief NCL cache read function
 *
 * \addtogroup ncl_20
 * \defgroup ncl_cache_read
 * \ingroup ncl_20
 *
 * @{
 */
#include "eccu.h"
#include "finstr.h"
#include "nand.h"
#include "ficu.h"
#include "ncl.h"
#include "ncl_err.h"
#include "ncl_cmd.h"
#include "ncl_erd.h"

#if HAVE_CACHE_READ
struct ncl_task_desc {
	struct ncl_cmd_t* ncl_cmd;
	pda_t pda;
	union {
		u32 all;
		struct {
			u32 idx : 13;    // Index of ncl_cmd pda list
			u32 du_cnt   : 3;// DU count within page, 0 based
			u32 mp_num   : 4;// MP number, 0 based
			u32 rsvd     : 12;
		} b;
	} param;
};

#define CACHE_READ_STARTED BIT0
#define NORMAL_READ_WAIT BIT1

fast_data_zi u32 lun_cache_read_flag[MAX_DIE];
fast_data_zi ncl_cmd_queue lun_cache_ncl_cmd_queue[MAX_DIE];
fast_data_zi struct ncl_task_desc lun_ready_task[MAX_DIE];
fast_data_zi u32 task_finst_cnt = 0;
fast_data u32 task_fcmd_id;
fast_data u8 evt_cache_read_flush = 0xFF;

fast_data struct finstr_format cache_read_fins_templ = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
		.b.mlun_en		= 1,
		.b.mp_row_offset_en	= 1,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_CACHE_READ_CMD,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_READ,
		.b.fins_fuse		= 0,

		.b.rsvd0		= 0,
		.b.pg_idx		= 0,
		.b.xlc_en		= 0,
		.b.raw_addr_en		= 0,
		.b.nvcmd_tag_ext	= 0,
		.b.no_eccu_path		= 0,
	},
	.dw1 = {
		.b.fcmd_id		= 0,
		.b.rsvd0		= 0,
		.b.du_dtag_ptr		= 0,
		.b.finst_info_loc	= FINST_INFO_IN_SRAM,
		.b.xfcnt_sel		= FINST_XFER_ZERO,
	},
	.dw2 = {
		.b.host_trx_dis		= 0,
		.b.ard_schem_sel	= ARD_DISABLE,

		.b.du_fmt_sel		= DU_FMT_USER_4K,
		.b.du_num		= 0,

		.b.scrc_en		= NCL_HAVE_HCRC,
		.b.meta_strip		= 0,
		.b.hc			= 0,

		.b.force_retry_en	= 0,
		.b.sd_mode		= 0,
	},
	.dw3 = {
		.b.raid_id0 = 62,
		.b.raid_id1 = 62,
		.b.raid_id2 = 62,
		.b.raid_id3 = 62,
	},
};

/*!
 * @brief Start cache read on specified LUN
 *
 * @param new_task	Task descriptor
 * @param lun		LUN
 *
 * @return	Not used
 */
fast_code void ncl_task_cache_read_start(struct ncl_task_desc* new_task, u32 lun)
{
	pda_t pda;
	u32 du_dtag_ptr;
	u32 du_dtag_ptr2;
	struct fda_dtag_format *fda_dtag;
	struct ncl_cmd_t* ncl_cmd;
	struct ncl_cmd_t* ncl_cmd2;
	u32 idx, idx2;
	struct finstr_format ins;

	idx = lun_ready_task[lun].param.b.idx;
	ncl_cmd = lun_ready_task[lun].ncl_cmd;
	ncl_cmd->task_cnt--;
	ncl_cmd2 = new_task->ncl_cmd;
	idx2 = new_task->param.b.idx;
	pda = lun_ready_task[lun].pda;

	// Normal read finst
	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->pda = pda;
	if (lun_ready_task[lun].param.b.du_cnt || lun_ready_task[lun].param.b.mp_num) {
		fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + idx);
	} else {
		fda_dtag->dtag.all = ncl_cmd->user_tag_list[idx].all;
	}

	ins = cache_read_fins_templ;
	if (ncl_cmd->addr_param.common_param.info_list[idx].pb_type == NAL_PB_TYPE_XLC) {
		u32 pg_idx = pda2pg_idx(pda);
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
		ins.dw0.b.pg_idx = pg_idx;
	} else {
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD;
		ins.dw0.b.pg_idx = 0;
	}
	ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
	ins.dw0.b.mp_num = lun_ready_task[lun].param.b.mp_num;
	if (task_finst_cnt == 0) {
		ins.dw0.b.fins = 1;
		task_fcmd_id = du_dtag_ptr;
	}

	ins.dw1.b.fcmd_id = task_fcmd_id;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw1.b.xfcnt_sel = FINST_XFER_ZERO;
	ficu_fill_finst(ins);
	ins.dw0.b.fins = 0;

	// Cache read finst
	du_dtag_ptr2 = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr2);
	fda_dtag->pda = new_task->pda;
	ins.dw0.b.mp_num = new_task->param.b.mp_num;
	if (ncl_cmd2->addr_param.common_param.info_list[idx2].pb_type == NAL_PB_TYPE_XLC) {
		u32 pg_idx = pda2pg_idx(new_task->pda);
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_CACHE_READ(pg_idx);
		ins.dw0.b.pg_idx = pg_idx;
	} else {
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_CACHE_READ_CMD;
	}
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr2;
	ficu_fill_finst(ins);

	// Read data finst
	ins.dw0.b.mp_num = lun_ready_task[lun].param.b.mp_num;
	ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
	ins.dw2.b.btn_op_type = ncl_cmd->op_type;
	ins.dw2.b.du_num = lun_ready_task[lun].param.b.du_cnt;
	ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
	if (ncl_cmd->flags & NCL_CMD_META_DISCARD) {
		ins.dw2.b.meta_strip = 1;
	}
	if (ncl_cmd->task_cnt == 0) {
		ins.dw0.b.lins = 1;
		ncl_cmd_save(task_fcmd_id, ncl_cmd);
		ncl_cmd->dtag_ptr = du_dtag_ptr2;
	}
	ficu_fill_finst(ins);
	task_finst_cnt += 3;

	if (ncl_cmd->task_cnt == 0) {
		// Final submit instruction(s)
		ficu_submit_finstr(task_finst_cnt);
		task_finst_cnt = 0;
	}
}

/*!
 * @brief Continue cache read on specified LUN
 *
 * @param new_task	Task descriptor
 * @param lun		LUN
 *
 * @return	Not used
 */
fast_code void ncl_task_cache_read_continue(struct ncl_task_desc* new_task, u32 lun)
{
	pda_t pda;
	u32 du_dtag_ptr;
	struct fda_dtag_format *fda_dtag;
	struct ncl_cmd_t* ncl_cmd;
	u32 idx;
	struct finstr_format ins;

	ncl_cmd = new_task->ncl_cmd;
	idx = new_task->param.b.idx;

	// Cache read command finst
	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->pda = new_task->pda;
	ins = cache_read_fins_templ;
	if (ncl_cmd->addr_param.common_param.info_list[idx].pb_type == NAL_PB_TYPE_XLC) {
		u32 pg_idx = pda2pg_idx(new_task->pda);
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_CACHE_READ(pg_idx);
		ins.dw0.b.pg_idx = pg_idx;
	} else {
		ins.dw0.b.pg_idx = 0;
	}
	ins.dw0.b.mp_num = new_task->param.b.mp_num;
	if (task_finst_cnt == 0) {
		ins.dw0.b.fins = 1;
		task_fcmd_id = du_dtag_ptr;
	}
	ins.dw1.b.fcmd_id = task_fcmd_id;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ficu_fill_finst(ins);
	task_finst_cnt++;
	ins.dw0.b.fins = 0;

	// Read data xfer finst
	idx = lun_ready_task[lun].param.b.idx;
	ncl_cmd = lun_ready_task[lun].ncl_cmd;
	ncl_cmd->task_cnt--;
	pda = lun_ready_task[lun].pda;

	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->pda = pda;
	if (lun_ready_task[lun].param.b.du_cnt || lun_ready_task[lun].param.b.mp_num) {
		fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + idx);
	} else {
		fda_dtag->dtag.all = ncl_cmd->user_tag_list[idx].all;
	}
	if (ncl_cmd->addr_param.common_param.info_list[idx].pb_type == NAL_PB_TYPE_XLC) {
		u32 pg_idx = pda2pg_idx(pda);
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.pg_idx = pg_idx;
	} else {
		ins.dw0.b.pg_idx = 0;
	}
	ins.dw2.b.btn_op_type = ncl_cmd->op_type;
	if (ncl_cmd->flags & NCL_CMD_META_DISCARD) {
		ins.dw2.b.meta_strip = 1;
	}
	ins.dw0.b.mp_num = lun_ready_task[lun].param.b.mp_num;
	ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
	ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
	ins.dw2.b.du_num = lun_ready_task[lun].param.b.du_cnt;
	ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
	if (ncl_cmd->task_cnt == 0) {
		ins.dw0.b.lins = 1;
		ncl_cmd_save(task_fcmd_id, ncl_cmd);
		ncl_cmd->dtag_ptr = du_dtag_ptr;
	}
	ficu_fill_finst(ins);
	task_finst_cnt++;
	if (ins.dw0.b.lins) {
		// Final submit instruction(s)
		ficu_submit_finstr(task_finst_cnt);
		task_finst_cnt = 0;
	}
}

/*!
 * @brief End cache read on specified LUN
 *
 * @param lun	LUN
 *
 * @return	Not used
 */
fast_code void ncl_task_cache_read_end(u32 lun)
{
	pda_t pda;
	u32 du_dtag_ptr;
	struct fda_dtag_format *fda_dtag;
	struct ncl_cmd_t* ncl_cmd;
	u32 idx;
	struct finstr_format ins;

	idx = lun_ready_task[lun].param.b.idx;
	ncl_cmd = lun_ready_task[lun].ncl_cmd;
	ncl_cmd->task_cnt--;
	pda = lun_ready_task[lun].pda;

	// Cache read last finst
	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->pda = pda;
	if (lun_ready_task[lun].param.b.du_cnt || lun_ready_task[lun].param.b.mp_num) {
		fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + idx);
	} else {
		fda_dtag->dtag.all = ncl_cmd->user_tag_list[idx].all;
	}

	ins = cache_read_fins_templ;
	if (ncl_cmd->addr_param.common_param.info_list[idx].pb_type == NAL_PB_TYPE_XLC) {
		u32 pg_idx = pda2pg_idx(pda);
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.pg_idx = pg_idx;
	} else {
		ins.dw0.b.pg_idx = 0;
	}
	ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_CACHE_READ_END_CMD;
	ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
	//ins.dw0.b.mp_num = 0;
	if (task_finst_cnt == 0) {
		ins.dw0.b.fins = 1;
		task_fcmd_id = du_dtag_ptr;
	}
	ins.dw1.b.fcmd_id = task_fcmd_id;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw2.b.btn_op_type = ncl_cmd->op_type;
	ins.dw2.b.du_num = lun_ready_task[lun].param.b.du_cnt;
	if (ncl_cmd->flags & NCL_CMD_META_DISCARD) {
		ins.dw2.b.meta_strip = 1;
	}
	ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
	ficu_fill_finst(ins);
	task_finst_cnt++;
	ins.dw0.b.fins = 0;


	// Read data finst
	ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
	ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
	if (ncl_cmd->task_cnt == 0) {
		ins.dw0.b.lins = 1;
		ncl_cmd_save(task_fcmd_id, ncl_cmd);
		ncl_cmd->dtag_ptr = du_dtag_ptr;
	}
	ficu_fill_finst(ins);
	task_finst_cnt++;

	if (ncl_cmd->task_cnt == 0) {
		// Final submit instruction(s)
		ficu_submit_finstr(task_finst_cnt);
		task_finst_cnt = 0;
	}
}

extern struct finstr_format set_feature_tmpl;
/*!
 * @brief Normal read on specified LUN
 *
 * @param lun	LUN
 *
 * @return	Not used
 */
fast_code void ncl_task_normal_read(u32 lun)
{
	pda_t pda;
	u32 du_dtag_ptr;
	struct fda_dtag_format *fda_dtag;
	struct ncl_cmd_t* ncl_cmd;
	u32 idx;
	struct finstr_format ins;

	idx = lun_ready_task[lun].param.b.idx;
	ncl_cmd = lun_ready_task[lun].ncl_cmd;
	ncl_cmd->task_cnt--;
	pda = lun_ready_task[lun].pda;

	// Submit read finst
	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->pda = pda;
#if (DU_CNT_PER_PAGE != 1)
	if (lun_ready_task[lun].param.b.du_cnt == 0) {
		if (ncl_cmd->flags & NCL_CMD_RAPID_PATH)
			fda_dtag->dtag.all = ncl_cmd->du_user_tag_list.all;
		else
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[idx].all;
	} else {
		fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + idx);
	}
#else
	if (lun_ready_task[lun].param.b.mp_num == 0) {
		fda_dtag->dtag.all = ncl_cmd->user_tag_list[idx].all;
	} else {
		fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + idx);
	}
#endif

	extern struct finstr_format read_fins_templ;
	ins = read_fins_templ;
	if (ncl_cmd->addr_param.common_param.info_list[idx].pb_type == NAL_PB_TYPE_XLC) {
		u32 pg_idx = pda2pg_idx(pda);
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
		ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
		ins.dw0.b.pg_idx = pg_idx;
	} else {
		ins.dw0.b.pg_idx = 0;
	}
	ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
	ins.dw0.b.mp_num = lun_ready_task[lun].param.b.mp_num;
	if (task_finst_cnt == 0) {
		ins.dw0.b.fins = 1;
		task_fcmd_id = du_dtag_ptr;
	}

	ins.dw1.b.fcmd_id = task_fcmd_id;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw2.b.btn_op_type = ncl_cmd->op_type;
	ins.dw2.b.du_num = lun_ready_task[lun].param.b.du_cnt;
	if (ncl_cmd->flags & NCL_CMD_META_DISCARD) {
		ins.dw2.b.meta_strip = 1;
	}
	ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;

	if (ncl_cmd->task_cnt == 0) {
		ins.dw0.b.lins = 1;
		ncl_cmd_save(task_fcmd_id, ncl_cmd);
		ncl_cmd->dtag_ptr = du_dtag_ptr;
	}
        
	ficu_fill_finst(ins);
	task_finst_cnt++;

	if (ncl_cmd->task_cnt == 0) {
		// Final submit instruction(s)
		ficu_submit_finstr(task_finst_cnt);
		task_finst_cnt = 0;
	}
}

/*!
 * @brief Submit all buffered tasks
 *
 * @return	Not used
 */
fast_code void ncl_tasks_clear(void)
{
	u32 lun;

	for (lun = 0; lun < nand_info.lun_num; lun++) {
		if (lun_cache_read_flag[lun] == 0) {
			continue;
		}
		if (lun_cache_read_flag[lun] == NORMAL_READ_WAIT) {
			ncl_task_normal_read(lun);
		} else {
			ncl_task_cache_read_end(lun);
		}
		lun_cache_read_flag[lun] = 0;
	}
}

/*!
 * @brief Submit task to specified LUN
 *
 * @param new_task	Task descriptor
 * @param lun		LUN
 *
 * @return	Not used
 */
fast_code void ncl_task_submit(struct ncl_task_desc* new_task, u32 lun)
{
	switch(lun_cache_read_flag[lun]) {
	case 0:
		lun_cache_read_flag[lun] = NORMAL_READ_WAIT;
		lun_ready_task[lun] = *new_task;
		return;
		break;
	case NORMAL_READ_WAIT:
		ncl_task_cache_read_start(new_task, lun);
		lun_cache_read_flag[lun] = CACHE_READ_STARTED;
		break;
	case CACHE_READ_STARTED:
		ncl_task_cache_read_continue(new_task, lun);
		break;
	}

	lun_ready_task[lun] = *new_task;
}

/*!
 * @brief Split NCL command into 1 or several small tasks
 *
 * @param ncl_cmd	NCL command
 *
 * @return	Not used
 */
fast_code void ncl_cmd_split_task(struct ncl_cmd_t *ncl_cmd)
{
	pda_t* pda_list;
	u32 i, j;
	u32 du_cnt;
	u32 du_off;
	u32 lun;
	struct ncl_task_desc task;

	ncl_cmd->task_cnt = 0;

	if (ncl_cmd->addr_param.common_param.list_len == 1) {// Specially speed up for single DU case
		task.ncl_cmd = ncl_cmd;
		task.param.all = 0;
		if (ncl_cmd->flags & NCL_CMD_RAPID_PATH) {
			task.pda = ncl_cmd->addr_param.rapid_du_param.pda;
		} else {
			task.pda = ncl_cmd->addr_param.common_param.pda_list[0];
		}
		ncl_cmd->task_cnt++;
		lun = pda2lun_id(task.pda);
		ncl_task_submit(&task, lun);
	} else {
		lun = 0;
		for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i+=du_cnt) {
			pda_list = ncl_cmd->addr_param.common_param.pda_list + i;
			task.ncl_cmd = ncl_cmd;
			task.pda = pda_list[0];
			du_off = pda_list[0] & (DU_CNT_PER_PAGE - 1);
			du_cnt = 1;
			for (j = i + 1; j < ncl_cmd->addr_param.common_param.list_len; j++) {
				if (pda_list[du_cnt] == pda_list[0] + du_cnt) {
					du_cnt++;
				}
			}
			if (du_cnt == 1) {// Single DU
				task.param.all = i;
			} else if (((du_cnt | du_off) & (DU_CNT_PER_PAGE - 1)) == 0) {// Full aligned SP or MP read
				task.param.b.idx = i;
				task.param.b.mp_num = (du_cnt >> DU_CNT_SHIFT) - 1;
				task.param.b.du_cnt = DU_CNT_PER_PAGE - 1;
			} else if ((du_cnt + du_off) <= DU_CNT_PER_PAGE) {// Partial page read
				task.param.b.du_cnt = du_cnt - 1;
				task.param.b.mp_num = 0;
				task.param.b.idx = i;
			} else {
				// Unaligned MP, cut short into SP, may support in future
				task.param.b.du_cnt = DU_CNT_PER_PAGE - 1 - du_off;
				task.param.b.mp_num = 0;
				task.param.b.idx = i;
				du_cnt = DU_CNT_PER_PAGE - du_off;
			}
			ncl_cmd->task_cnt++;
			lun = pda2lun_id(pda_list[0]);
			ncl_task_submit(&task, lun);
		}
	}

	ncl_cmd->flags |= NCL_CMD_CACHE_READ_FLAG;
	if (unlikely(ncl_cmd->completion == NULL)) {
		if (lun_cache_read_flag[lun] == NORMAL_READ_WAIT) {
			ncl_task_normal_read(lun);
		} else {
			ncl_task_cache_read_end(lun);
		}
		lun_cache_read_flag[lun] = 0;
		while(NCL_CMD_PENDING(ncl_cmd)) {
			ficu_done_wait();
		}
	} else {
		ncl_cmd->lun_id = lun;
		QSIMPLEQ_INSERT_TAIL(&lun_cache_ncl_cmd_queue[lun], ncl_cmd, entry);
		evt_set_cs(evt_cache_read_flush, 1, 0, CS_TASK);
	}
}

fast_code void ncl_cache_read_flush(u32 param, u32 poll, u32 count)
{
	u32 lun;

	for (lun = 0; lun < nand_info.lun_num; lun++) {
		if (lun_cache_read_flag[lun] == 0) {
			continue;
		}
		if (lun_cache_read_flag[lun] == NORMAL_READ_WAIT) {
			ncl_task_normal_read(lun);
		} else {
			ncl_task_cache_read_end(lun);
		}
		evt_set_cs(evt_cache_read_flush, 1, 0, CS_TASK);
		lun_cache_read_flag[lun] = 0;
		break;
	}
}

/*!
 * @brief NCL cache read Initialization module
 *
 * @return	Not used
 */
init_code void ncl_cache_read_init(void)
{
	u32 lun;

	sys_assert(nand_info.lun_num > 1);
	for (lun = 0; lun < nand_info.lun_num; lun++) {
		QSIMPLEQ_INIT(&lun_cache_ncl_cmd_queue[lun]);
		lun_cache_read_flag[lun] = 0;
	}
	evt_register(ncl_cache_read_flush, 0, &evt_cache_read_flush);
}
#endif
/*! @} */
