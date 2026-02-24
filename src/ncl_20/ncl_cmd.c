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
/*! \file ncl_cmd.c
 * @brief NCL nand command handling
 *
 * \addtogroup ncl_20
 * \defgroup ncl_cmd
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
#include "ncl_raid.h"
#include "ncl_cache_read.h"
#include "srb.h"
#include "ftl_export.h"
#include "ncl_exports.h"
#include "spin_lock.h"
#include "srb.h"
#include "read_retry.h"
#include "die_que.h"
#include "nand_tsb.h"
#include "read_retry.h"
#include "rdisk.h"
#include "feature/ncl_onfi_dcc_training.h"
/*! \cond PRIVATE */
#define __FILEID__ cmd
#include "trace.h"
/*! \endcond */
/*! ncl block state */
fast_data_zi bool ncl_cmd_wait = false;
share_data_zi bool FW_ARD_EN;
share_data_zi u8   FW_ARD_CPU_ID;
fast_data ncl_cmd_queue ncl_cmds_wait_queue;

/* get_read_du_cnt and get_du_cnt frequently need to check if PDA list cross LUN boundary,
 * to speed up, define this global variable. E.g. 4 planes device, lun_bndry_check
 * will be 0xF, 0x***0F and 0x***10 is sequential, but they belong to different LUN
 * need seperate them in instruction.
 */
fast_data_zi u32 lun_ep_bndry_check;	// Erase/program boundary check
fast_data_zi u32 lun_read_bndry_check;	// Read boundary check
fast_data bool finst_info_in_sram = FINST_INFO_IN_SRAM;		///< F-inst info in SRAM or DTCM
share_data volatile u8 plp_trigger;
share_data volatile bool hostReset;
fast_data u32 refresh_read_start_time = 0;
#if POLL_CACHE_PROG
fast_data_zi u32 cache_prog_lun_bmp[MAX_CHANNEL] = {0};				///< Cache program status bitmap by LUN
#endif
#if OPEN_ROW == 1
fast_data u32 openRow[128];//4ch 8ce 2lun 2plane (per cpu)
#endif
#if HAVE_CACHE_READ
fast_data u32 ard_fcmd_cnt = 0;
extern ncl_cmd_queue lun_cache_ncl_cmd_queue[];
#endif

fast_data_zi u32 fcmd_outstanding_cnt = 0;
fast_data_zi u32 fcmd_rw_outstanding_cnt = 0; // Read/write fcmd (with data xfer) outstanding cnt
share_data_zi volatile u32 fw_ard_cnt;
fast_data u8 evt_refresh_read = 0xff;

#if OPEN_ROW == 1
fast_data u8 openRow_table_all_clear = false;
#endif

#if !FINST_INFO_IN_SRAM
# error ("only support finst info in sram")
#endif

fast_data du_dtag_ptr_t free_dtag_ptr = DTAG_PTR_INVALID;
fast_data_zi du_dtag_ptr_t free_dtag_list[DTAG_PTR_COUNT];
fast_data du_dtag_ptr_t free_dtag_tail = DTAG_PTR_INVALID;
fast_data_zi struct ncl_cmd_t* ncl_cmd_ptr[DTAG_PTR_COUNT];
fast_data_ni struct timer_list refresh_read_timer;
extern bool ucache_flush_flag;
extern u16 die;

share_data_zi u32 _max_capacity;	// For ECCT check LDA, DBG, LJ1-252, PgFalCmdTimeout


void refresh_read_cpl(struct ncl_cmd_t *ncl_cmd);
void read_refresh_poweron(u32 rfrd_times);

extern struct fspm_usage_t *fspm_usage_ptr;
extern struct ficu_cq_entry* ficu_cq_ptr;
extern bool ficu_cq_phase;
extern u32 ard_mode;
fast_data bool ncl_cmd_in = false;
extern bool all_init_done;
extern bool gFormatFlag;
#ifdef History_read
extern hist_tab_offset_t *hist_tab_ptr_st;
#endif

#ifdef REFR_RD
#define PWR_MAX_RFRD_DIE (32)
#define IDLE_MAX_RFRD_DIE (8)
fast_data u8 rerf_undone_cnt = 0;//calculate undone refresh cmd,CPU2/CPU4 use
fast_data u32 spb_start = 0;
fast_data u32 rfrd_time_start = 0;
struct rfrd_times_s rfrd_times_struct = {0};

struct finstr_format status_fins_tmpl;
ddr_data_ni pda_t rfrd_pda[2];
ddr_data_ni struct ncl_cmd_t rfrd_ncl_cmd_byCPU[2];//lun count
//#define REFR_TIME_TEST //test for refresh read time cost
#ifdef REFR_TIME_TEST
fast_data u64 rfrd_ticket_st;
fast_data u32 rfrd_ticket_ed;
#endif

struct ncl_cmd_t rfrd_ncl_cmd =
{
    .op_code      = NCL_CMD_READ_REFRESH,
    .flags        = NCL_CMD_SLC_PB_TYPE_FLAG,
    .addr_param.common_param.list_len = 1,
    //.addr_param.common_param.pda_list = rfrd_pda,
    .completion   = refresh_read_cpl
};
#endif

fast_data bool cpl_ref = true;

norm_ps_data struct finstr_format refresh_fins_templ = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
#if STABLE_REGRESSION
		.b.mlun_en		= 0,
#else
		.b.mlun_en		= 1,
#endif
		.b.mp_row_offset_en	= 1,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_READ_CMD,
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

        #if NCL_FW_RETRY
        .b.ard_schem_sel = ARD_DISABLE,
        #else
		.b.ard_schem_sel = ARD_TMPLT_SLC,
        #endif

		.b.du_fmt_sel		= 0,
		.b.du_num		= 0,

		//.b.scrc_en		= 0,
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

norm_ps_data struct finstr_format rawread_fins_templ = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 1,
		.b.lins			= 1,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
#if STABLE_REGRESSION
		.b.mlun_en		= 0,
#else
		.b.mlun_en		= 1,
#endif
		.b.mp_row_offset_en	= 1,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_READ_CMD,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_READ,
		.b.fins_fuse		= 0,

		.b.rsvd0		= 0,
		.b.pg_idx		= 0,
		.b.xlc_en		= 0,
		.b.raw_addr_en		= 1,
		.b.nvcmd_tag_ext	= 0,
		.b.no_eccu_path		= 0,
	},
	.dw1 = {
		.b.fcmd_id		= 0,
		.b.rsvd0		= 0,
		.b.du_dtag_ptr		= 0,
		.b.finst_info_loc	= FINST_INFO_IN_SRAM,
		.b.xfcnt_sel		= FINST_XFER_AUTO,
	},
	.dw2 = {
		.b.host_trx_dis		= 0,
		.b.ard_schem_sel	= 0,

		.b.du_fmt_sel		= DU_FMT_RAW_4K,
		.b.du_num		= 1,

		.b.scrc_en		= NCL_HAVE_HCRC,
		.b.meta_strip		= 1,
		.b.hc			= 0,

		.b.force_retry_en	= 0,
		.b.sd_mode		= 0,

		.b.raid_cmd          	= NO_RAID,
		.b.raid_sram_bank_id	= 7,
	},
	.dw3 = {
		.b.raid_id0 = 62,
		.b.raid_id1 = 62,
		.b.raid_id2 = 62,
		.b.raid_id3 = 62,
	},
};

fast_data struct finstr_format read_fins_templ = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
#if STABLE_REGRESSION
		.b.mlun_en		= 0,
#else
		.b.mlun_en		= 1,
#endif
		.b.mp_row_offset_en	= 1,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_READ_CMD,
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
		.b.xfcnt_sel		= FINST_XFER_AUTO,
	},
	.dw2 = {
		.b.host_trx_dis		= 0,
#if HAVE_CACHE_READ
		.b.ard_schem_sel	= ARD_DISABLE,
#else
        #if NCL_FW_RETRY
        .b.ard_schem_sel = ARD_DISABLE,
        #else
		.b.ard_schem_sel = ARD_TMPLT_SLC,
        #endif
#endif

		.b.du_fmt_sel		= DU_FMT_USER_4K,
		.b.du_num		= 0,

		.b.scrc_en		= NCL_HAVE_HCRC,
		.b.meta_strip		= 0,
		.b.hc			= 0,

		.b.force_retry_en	= 0,
		.b.sd_mode		= 0,

		.b.raid_cmd          	= NO_RAID,
		.b.raid_sram_bank_id	= 7,
	},
	.dw3 = {
		.b.raid_id0 = 62,
		.b.raid_id1 = 62,
		.b.raid_id2 = 62,
		.b.raid_id3 = 62,
	},
};

fast_data struct finstr_format read_streaming_fast_fins_templ = {
	.dw0 =
	{
		.b.fins_vld		       = 1,
		.b.fins			       = 1,
		.b.lins			       = 1,

		.b.poll_dis		       = 0,
		.b.susp_en		       = 0,
		.b.mlun_en		       = 1,
		.b.mp_row_offset_en	   = 1,

		.b.ndcmd_fmt_sel	   = FINST_NAND_FMT_SLC_READ_CMD,
		.b.vsc_en		       = 0,

		.b.mp_num		       = 0,
		.b.finst_type		   = FINST_TYPE_READ,
		.b.fins_fuse		   = 0,

		.b.rsvd0		       = 0,
		.b.pg_idx		       = 0,
		.b.xlc_en		       = 0,
		.b.raw_addr_en		   = 0,
		.b.nvcmd_tag_ext	   = 0,
		.b.no_eccu_path		   = 0,
	},
	.dw1 =
	{
		.b.fcmd_id		      = 0,
		.b.rsvd0		      = 0,
		.b.du_dtag_ptr		  = 0,
		.b.finst_info_loc	  = FINST_INFO_IN_SRAM,
		.b.xfcnt_sel		  = FINST_XFER_AUTO,
	},
	.dw2 =
	{
		.b.host_trx_dis		  = 0,
		#if HAVE_CACHE_READ
		.b.ard_schem_sel	= ARD_DISABLE,
		#else
        #if NCL_FW_RETRY
        .b.ard_schem_sel = ARD_DISABLE,
        #else
		.b.ard_schem_sel = ARD_TMPLT_SLC,
        #endif
		#endif

		.b.du_fmt_sel		  = DU_FMT_USER_4K,
		.b.du_num		      = 0,

		.b.scrc_en		      = NCL_HAVE_HCRC,
		.b.meta_strip		  = 0,
		.b.hc			      = 0,

		.b.force_retry_en	  = 0,
		.b.sd_mode		      = 0,

		.b.raid_cmd           = NO_RAID,
		.b.raid_sram_bank_id  = 4,
	},
	.dw3 =
	{
		.b.raid_id0          = 62,
		.b.raid_id1          = 62,
		.b.raid_id2          = 62,
		.b.raid_id3          = 62,
	},
};


norm_ps_data struct finstr_format rawwrite_fins_templ = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 1,
		.b.lins			= 1,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
#if STABLE_REGRESSION
		.b.mlun_en		= 0,
#else
		.b.mlun_en		= 1,
#endif
		.b.mp_row_offset_en	= 1,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_PROG,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_PROG,
		.b.fins_fuse		= 0,

		.b.rsvd0		= 0,
		.b.pg_idx		= 0,
		.b.xlc_en		= 0,
		.b.raw_addr_en		= 1,
		.b.nvcmd_tag_ext	= 0,
		.b.no_eccu_path		= 0,
	},
	.dw1 = {
		.b.fcmd_id		= 0,
		.b.rsvd0		= 0,
		.b.du_dtag_ptr		= 0,
		.b.finst_info_loc	= FINST_INFO_IN_SRAM,
		.b.xfcnt_sel		= FINST_XFER_AUTO,
	},
	.dw2 = {
		.b.host_trx_dis		= 0,
		.b.ard_schem_sel	= 0,

		.b.du_fmt_sel		= DU_FMT_RAW_4K,
		.b.du_num		= 1,

		//.b.scrc_en		= NCL_HAVE_HCRC,
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

static inline void ncl_cmd_chk_crc_buffer(struct finstr_format ins, struct ncl_cmd_t *ncl_cmd)
{
	if(ins.dw2.b.scrc_en == 1)
	{
		if((ncl_cmd->user_tag_list[0].pl.dtag & BIT23) && ((ncl_cmd->user_tag_list[0].pl.dtag & (BIT23 - 1)) >= max_ddr_dtag_cnt))
		{
			ncl_cmd_trace(LOG_ALW, 0xd06e, "panic!!!, ncl_cmd:0x%x, op_code:%d, op_type:%d, completion:0x%x", ncl_cmd, ncl_cmd->op_code, ncl_cmd->op_type, ncl_cmd->completion);
		}
	}
}

fast_code u32 ficu_get_feature(u8 ch, u8 ce, u16 fa, bool by_lun)
{
	u32 fcmd_id;
	struct finstr_format ins;
	struct finstr_format* pins;
	struct fda_dtag_format *fda_dtag;

	fcmd_outstanding_cnt++;

	// Can only support 1 DU
	fcmd_id = ncl_acquire_dtag_ptr();

	// Configure fda list
	fda_dtag = ficu_get_addr_dtag_ptr(fcmd_id);
	fda_dtag->hdr = 0;
	fda_dtag->fda_col= fa;
	fda_dtag->ch_id = ch;
	fda_dtag->dev_id = ce;

	// Configure instruction
	ins = rawwrite_fins_templ;
	ins.dw0.b.finst_type = FINST_TYPE_READ;
	if (by_lun) {
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
	} else {
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE;
	}
	ins.dw0.b.fins = 1;
	ins.dw0.b.lins = 1;
	ins.dw1.b.fcmd_id = fcmd_id;
	ins.dw1.b.du_dtag_ptr = fcmd_id;
	ins.dw0.b.raw_addr_en = 1;
	ins.dw1.b.xfcnt_sel = FINST_XFER_8B;
	ins.dw0.b.no_eccu_path = 1;
	pins = (struct finstr_format*)ficu_get_finstr_slot();
	*pins = ins;

	ncl_cmd_save(fcmd_id, NULL);
	ficu_submit_finstr(1);
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	struct param_fifo_format* param_info;
	param_info = get_param_fifo();
	if (param_info->fcmd_id != fcmd_id) {
		sys_assert(0);
	}
	if (param_info->finst_id != 0) {
		sys_assert(0);
	}
	return param_info->param[0];
}

#if HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT
fast_code u32 ficu_get_tempature(u8 ch, u8 ce, bool by_lun)
{
	u32 fcmd_id;
	struct finstr_format ins;
	struct finstr_format* pins;
	struct ncl_cmd_t ncl_cmd;
	struct fda_dtag_format *fda_dtag;

	fcmd_outstanding_cnt++;

	// Can only support 1 DU
	fcmd_id = ncl_acquire_dtag_ptr();
	ncl_cmd.dtag_ptr = fcmd_id;
	ncl_cmd.completion = NULL;
	ncl_cmd.flags = 0;

	fda_dtag = ficu_get_addr_dtag_ptr(fcmd_id);
	fda_dtag->pda = 0;

	ins = read_fins_templ;

	ins.dw0.b.fins = 1;
	ins.dw0.b.lins = 1;
	ins.dw0.b.mlun_en = 0;
	ins.dw0.b.mp_row_offset_en = 0;
	ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_GET_TEMPERATURE;
	ins.dw0.b.fins_fuse = 1;
	ins.dw0.b.no_eccu_path = 1;

	ins.dw1.b.fcmd_id = fcmd_id;
	ins.dw1.b.du_dtag_ptr = fcmd_id;
	ins.dw1.b.xfcnt_sel = FINST_XFER_1B;

	ins.dw2.all = 0;

	pins = (struct finstr_format*)ficu_get_finstr_slot();
	*pins = ins;
	ncl_cmd_save(fcmd_id, &ncl_cmd);
	ficu_submit_finstr(1);
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	struct param_fifo_format* param_info;
	param_info = get_param_fifo();
	if (param_info->fcmd_id != fcmd_id) {
		sys_assert(0);
	}
	if (param_info->finst_id != 0) {
		sys_assert(0);
	}
	return param_info->param[0];
}
#endif

fast_data struct finstr_format prog_fins_templ = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
#if STABLE_REGRESSION
		.b.mlun_en		= 0,
#else
		.b.mlun_en		= 1,
#endif
		.b.mp_row_offset_en	= 0,
		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_PROG,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_PROG,
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
		.b.xfcnt_sel		= FINST_XFER_AUTO,
	},
	.dw2 = {
		.b.host_trx_dis		= 0,
		.b.ard_schem_sel	= 0,

		.b.du_fmt_sel		= DU_FMT_USER_4K,
		.b.du_num		= DU_CNT_PER_PAGE - 1,

		.b.scrc_en		= NCL_HAVE_HCRC,
		.b.meta_strip		= 0,
		.b.hc			= 0,

		.b.force_retry_en	= 0,
		.b.sd_mode		= 0,

		.b.raid_cmd          	= NO_RAID,
		.b.raid_sram_bank_id	= 7,
	},
	.dw3 = {
		.b.raid_id0 = 62,
		.b.raid_id1 = 62,
		.b.raid_id2 = 62,
		.b.raid_id3 = 62,
	},
};

fast_data struct finstr_format erase_fins_tmpl = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
#if STABLE_REGRESSION
		.b.mlun_en		= 0,
#else
		.b.mlun_en		= 1,
#endif
		.b.mp_row_offset_en	= 0,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_ERASE,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_ERASE,
		.b.fins_fuse		= 0,

		.b.rsvd0		= 0,
		.b.pg_idx		= 0,
		.b.xlc_en		= 0,
#if NCL_CPDA
		.b.raw_addr_en		= 1,
#else
		.b.raw_addr_en		= 0,
#endif
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
		.b.ard_schem_sel	= 0,

		.b.du_fmt_sel		= DU_FMT_USER_4K,
		.b.du_num		= 0,

		//.b.scrc_en		= 0,
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

// Set feature template
norm_ps_data struct finstr_format set_feature_tmpl = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
		.b.mlun_en		= 0,
		.b.mp_row_offset_en	= 0,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_FEATURE,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_PROG,
		.b.fins_fuse		= 0,

		.b.rsvd0		= 0,
		.b.pg_idx		= 0,
		.b.xlc_en		= 0,
		.b.raw_addr_en		= 1,
		.b.nvcmd_tag_ext	= 0,
		.b.no_eccu_path		= 1,
	},
	.dw1 = {
		.b.fcmd_id		= 0,
		.b.rsvd0		= 0,
		.b.du_dtag_ptr		= 0,
		.b.finst_info_loc	= FINST_INFO_IN_SRAM,
		.b.xfcnt_sel		= FINST_XFER_4B,
	},
	.dw2.all = 0x10,// Default enable flag check
	.dw3 = {
		.b.raid_id0 = 62,
		.b.raid_id1 = 62,
		.b.raid_id2 = 62,
		.b.raid_id3 = 62,
	},
};

fast_data struct finstr_format status_fins_tmpl = {
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
#if STABLE_REGRESSION
		.b.mlun_en		= 0,
#else
		.b.mlun_en		= 1,
#endif
		.b.mp_row_offset_en	= 1,
		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_READ_STATUS_ENH,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_READ_STS,
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
		.b.ard_schem_sel	= 0,

		.b.du_fmt_sel		= DU_FMT_USER_4K,
		.b.du_num		= 0,

		//.b.scrc_en		= NCL_HAVE_HCRC,
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

#if POLL_CACHE_PROG
static inline void cache_prog_start(u32 pda)
{
	u32 lun = pda2lun_id(pda);
	cache_prog_lun_bmp[lun >> 5] |= 1 << (lun & 0x1F);
}

static inline void cache_prog_end(u32 pda)
{
	u32 lun = pda2lun_id(pda);
	cache_prog_lun_bmp[lun >> 5] &= ~(1 << (lun & 0x1F));
}

static inline bool cache_prog_check(u32 pda)
{
	u32 lun = pda2lun_id(pda);
	return !!(cache_prog_lun_bmp[lun >> 5] & (1 << (lun & 0x1F)));
}
#endif

fast_code struct meta_descriptor *ficu_get_meta_ptr(u32 index)
{
	sys_assert(0);// TODO, HW not ready also
	return NULL;
}



/*!
 * @brief Acquire free du_dtag_ptr used for F-inst
 * The du_dtag_ptr of 1st F-inst within FCMD will also be multiplex as fcmd_id,
 * so we do not to allocate/free another resource.
 *
 * @return	free du_dtag_ptr
 */
fast_code du_dtag_ptr_t ncl_acquire_dtag_ptr(void)
{
	du_dtag_ptr_t ptr;
	while (free_dtag_ptr == free_dtag_tail) {
		//ncl_cmd_trace(LOG_ERR, 0, "dtag exhausted\n");
		ficu_done_wait();
	}
	int flags;
	flags = irq_save();
	ptr = free_dtag_ptr;
	free_dtag_ptr = free_dtag_list[free_dtag_ptr];
	irq_restore(flags);
	return ptr;
}

/*!
 * @brief Save ncl_cmd pointer which will be retrieved by fcmd_id later when error occurs or fcmd completion
 *
 * @param fcmd_id	FCMD ID
 * @param ncl_cmd	NCL command pointer
 *
 * @return	not used
 */
fast_code void ncl_cmd_save(u32 fcmd_id, struct ncl_cmd_t* ncl_cmd)
{
#if !PERF_NCL
	sys_assert(ncl_cmd_ptr[fcmd_id] == NULL);
#endif
	ncl_cmd_ptr[fcmd_id] = ncl_cmd;
}

/*!
 * @brief Retrieve ncl_cmd pointer by fcmd_id
 *
 * @param fcmd_id	FCMD ID
 *
 * @return	NCL command pointer
 */
fast_code struct ncl_cmd_t* ncl_cmd_retrieve(u32 fcmd_id)
{
	return ncl_cmd_ptr[fcmd_id];
}

/*!
 * @brief Debug code
 *
 * @return	not used
 */
fast_code void ncl_cmd_error_dump(struct ncl_cmd_t* ncl_cmd)
{
	u32 list_len = 0;
	struct info_param_t* info_list = NULL;
	pda_t* pda_list = NULL;

    ncl_cmd_trace(LOG_ERR, 0xf947, "ncl cmd %x(op %d) error", ncl_cmd, ncl_cmd->op_code);

	switch(ncl_cmd->op_code) {
	case NCL_CMD_READ_REFRESH:
	case NCL_CMD_READ_RAW:
		// No way, these command will not report error
		return;
	case NCL_CMD_OP_READ:
    case NCL_CMD_OP_READ_FW_ARD:    
    case NCL_CMD_OP_READ_RAW:  //Sean_test_220201
    case NCL_CMD_OP_READ_STREAMING_FAST:
    case NCL_CMD_PATROL_READ:
		if (ncl_cmd->flags & NCL_CMD_RAPID_PATH)

        	{
			list_len = ncl_cmd->addr_param.rapid_du_param.list_len;
			info_list = &ncl_cmd->addr_param.rapid_du_param.info;
			pda_list = &ncl_cmd->addr_param.rapid_du_param.pda;
		}
        else      //tony 20201218
        {
            list_len = ncl_cmd->addr_param.common_param.list_len;
            info_list = ncl_cmd->addr_param.common_param.info_list;
            pda_list = ncl_cmd->addr_param.common_param.pda_list;
        }
		break;

    #if (SPOR_FLOW == mENABLE)
    case NCL_CMD_SPOR_SCAN_FIRST_PG:
    case NCL_CMD_SPOR_SCAN_LAST_PG:
    case NCL_CMD_SPOR_SCAN_PG_AUX:
    case NCL_CMD_SPOR_P2L_READ:
    case NCL_CMD_SPOR_P2L_READ_POS:
    case NCL_CMD_SPOR_BLIST_READ:
    case NCL_CMD_SPOR_SCAN_BLANK_POS:
		list_len = ncl_cmd->addr_param.common_param.list_len;
		info_list = ncl_cmd->addr_param.common_param.info_list;
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		break;
    #endif
    case NCL_CMD_P2L_SCAN_PG_AUX:
	case NCL_CMD_OP_WRITE:
	case NCL_CMD_OP_ERASE:
		list_len = ncl_cmd->addr_param.common_param.list_len;
		info_list = ncl_cmd->addr_param.common_param.info_list;
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		break;
	case NCL_CMD_OP_CB_MODE0:
		list_len = ncl_cmd->addr_param.cb_param.list_len;
		info_list = ncl_cmd->addr_param.cb_param.info_list;
		//if (err == nand_err_enc) {
		if (1) {// Currently not support read error
			pda_list = ncl_cmd->addr_param.cb_param.addr->tlc_list_dst;
		} else {
			pda_list = ncl_cmd->addr_param.cb_param.addr->slc_list_src;
		}
		break;
	default:
		ncl_cmd_trace(LOG_ERR, 0x5f58, "op not found");
		return;
	}
    
	for (u16 i = 0; i < list_len; i++) {
		ncl_cmd_trace(LOG_ERR, 0xad66, "pda 0x%x, err %d", pda_list[i], info_list[i].status);
	}
}

/*!
 * @brief Notifier for a ncl_cmd that ARD occur
 *
 * @param fcmd_id	FCMD ID of FCMD that trigger ARD
 *
 * @return	not used
 */
fast_code void ncl_fcmd_ard_occur(u32 fcmd_id)
{
	struct ncl_cmd_t* ncl_cmd;
	ncl_cmd = ncl_cmd_retrieve(fcmd_id);
	ncl_cmd->status |= NCL_CMD_ARD_STATUS;
}

/*!
 * @brief NCL command completion handler within NCL module
 *
 * @param fcmd_id	FCMD ID of completed FCMD
 *
 * @return	not used
 */
fast_code void ncl_fcmd_completion(u32 fcmd_id)
{
	struct ncl_cmd_t* ncl_cmd;
#if !PERF_NCL
	sys_assert(fcmd_outstanding_cnt);
#endif
	fcmd_outstanding_cnt--;
	if (fcmd_outstanding_cnt == 0) {
		SET_NCL_IDLE();
	}

	ncl_cmd = ncl_cmd_retrieve(fcmd_id);
	ncl_cmd_ptr[fcmd_id] = NULL;
#if !PERF_NCL
	if (ncl_cmd == NULL) {
		sys_assert(0);
	}
#endif

	// Release used du_dtag_ptr list
	if (fcmd_id >= START_FCMD_ID) {
		free_dtag_list[free_dtag_tail] = fcmd_id;
		free_dtag_tail = ncl_cmd->dtag_ptr;

#if NCL_HAVE_ERD
	} else {
		extern u32 erd_id_bmp;
		erd_id_bmp |= 1 << fcmd_id;
		if (free_dtag_list[fcmd_id] != DTAG_PTR_INVALID) {
			free_dtag_list[free_dtag_tail] = free_dtag_list[fcmd_id];
			free_dtag_tail = ncl_cmd->dtag_ptr;
			free_dtag_list[fcmd_id] = DTAG_PTR_INVALID;
		}
#endif
	}

	//ncl_cmd_trace(LOG_ERR, 0, "ncl cmd %x cmpl %d -> %d\n", ncl_cmd, fcmd_id, ncl_cmd->dtag_ptr);
#if !PERF_NCL
	ncl_cmd_trace(LOG_DEBUG, 0x433a, "fcmd_id %d, sts %x, cnt %d",
		      fcmd_id, ncl_cmd->status, fcmd_outstanding_cnt);
#endif

	if (ncl_cmd->op_code == NCL_CMD_OP_READ_FW_ARD) {
		fw_ard_cnt--;
	}

	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
#if HAVE_CACHE_READ
		if (ncl_cmd->op_code == NCL_CMD_OP_READ || ncl_cmd->op_code == NCL_CMD_PATROL_READ) {
			if ((ncl_cmd->flags & NCL_CMD_NO_READ_RETRY_FLAG) == 0) {
				ard_fcmd_cnt++;
				ncl_cmd->op_code = NCL_CMD_OP_READ_ARD;
				if (ncl_cmd->flags & NCL_CMD_RAPID_PATH) {
					ncl_cmd_rapid_single_du_read(ncl_cmd);
				} else {
					ncl_cmd_submit(ncl_cmd);
				}
				return;
			}
		} else if (ncl_cmd->op_code == NCL_CMD_OP_READ_ARD) {
			ard_fcmd_cnt--;
			ncl_cmd->op_code = NCL_CMD_OP_READ;
		}
#endif

#if NCL_HAVE_ERD
		if (support_erd && (ncl_cmd->op_code == NCL_CMD_OP_READ || ncl_cmd->op_code == NCL_CMD_PATROL_READ) && ((ncl_cmd->flags & NCL_CMD_NO_READ_RETRY_FLAG) == 0)) {
			if (ncl_cmd_erd_add(ncl_cmd)) {
				return;
			}
		}
#endif
	}

	if ((1<< ncl_cmd->op_code) & ((1 << NCL_CMD_OP_READ) | (1 << NCL_CMD_READ_RAW) | (1 << NCL_CMD_OP_SSR_READ) | (1 << NCL_CMD_OP_WRITE) | (1 << NCL_CMD_PATROL_READ) | (1 << NCL_CMD_OP_READ_RAW))) {
		fcmd_rw_outstanding_cnt--;
	}

	if (ncl_cmd->completion) {
#if HAVE_CACHE_READ
		if (ncl_cmd->flags & NCL_CMD_CACHE_READ_FLAG) {
			QSIMPLEQ_REMOVE(&lun_cache_ncl_cmd_queue[ncl_cmd->lun_id], ncl_cmd, ncl_cmd_t, entry);
		}
#endif
#if !PERF_NCL
		ncl_cmd->flags |= NCL_CMD_COMPLETED_FLAG;
#endif

		ncl_cmd->completion(ncl_cmd);
	} else {
		ncl_cmd->flags |= NCL_CMD_COMPLETED_FLAG;
	}
}

fast_code bool ncl_cmd_find_pda_index(struct ncl_cmd_t* ncl_cmd, pda_t pda, u32 plane, enum ncb_err err)
{
	bool found = false;
	u32 i, duIdx;
	u32 list_len;
	pda_t* pda_list;
    #ifdef PREVENT_PDA_MISMATCH
    u32 mp_cnt;
    u32 du_cnt;
    u32 fins_cnt = 0;
    u32 du_idx = 0;
    ficu_status_reg2_t reg2 = { .all = ficu_readl(FICU_STATUS_REG2)};
    #endif
	struct info_param_t* info_list;

	ncl_w_req_t *ncl_req = ncl_cmd->caller_priv;	// DBG, LJ1-252, PgFalCmdTimeout

/*	// EH_TBD, from SDK code, DBG, LJ1-252, PgFalCmdTimeout
	// PPU error maybe reported in any DU in the page to program, but there are only 1 PDA
	if (err == cur_du_ppu_err) {
		pda &= ~(DU_CNT_PER_PAGE - 1);
	}
*/
	switch(ncl_cmd->op_code) {
	case NCL_CMD_READ_REFRESH:
	case NCL_CMD_READ_RAW:

		// Ignore error on these command, read raw may met erased error
		return true;
    case NCL_CMD_PATROL_READ:
        //ncl_cmd_trace(LOG_ERR, 0, "patrol read err");
        //ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
        //return true;
	case NCL_CMD_OP_READ:
    case NCL_CMD_OP_READ_RAW:  //Sean_test
	case NCL_CMD_OP_READ_FW_ARD:
	case NCL_CMD_OP_READ_STREAMING_FAST:
#ifdef NCL_FW_RETRY_BY_SUBMIT
    case NCL_CMD_FW_RETRY_READ:
#endif
		if (ncl_cmd->flags & NCL_CMD_RAPID_PATH) {
			list_len = ncl_cmd->addr_param.rapid_du_param.list_len;
			info_list = &ncl_cmd->addr_param.rapid_du_param.info;
			pda_list = &ncl_cmd->addr_param.rapid_du_param.pda;
			break;
		}
		else
        {
            list_len = ncl_cmd->addr_param.common_param.list_len;
            info_list = ncl_cmd->addr_param.common_param.info_list;
            pda_list = ncl_cmd->addr_param.common_param.pda_list;
			break;
        }

#if (SPOR_FLOW == mENABLE)
    case NCL_CMD_SPOR_SCAN_FIRST_PG:
    case NCL_CMD_SPOR_SCAN_LAST_PG:
    case NCL_CMD_SPOR_SCAN_PG_AUX:
    case NCL_CMD_SPOR_P2L_READ:
    case NCL_CMD_SPOR_P2L_READ_POS:
    case NCL_CMD_SPOR_BLIST_READ:
    case NCL_CMD_SPOR_SCAN_BLANK_POS:
        list_len = ncl_cmd->addr_param.common_param.list_len;
		info_list = ncl_cmd->addr_param.common_param.info_list;
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		break;
#endif

    case NCL_CMD_P2L_SCAN_PG_AUX:
		list_len = ncl_cmd->addr_param.common_param.list_len;
		info_list = ncl_cmd->addr_param.common_param.info_list;
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		break;

	case NCL_CMD_OP_WRITE:
	case NCL_CMD_OP_ERASE:
	case NCL_CMD_SET_GET_FEATURE:
	case NCL_CMD_OP_POLLING_STATUS:
    #ifdef NCL_FW_RETRY_BY_SUBMIT
    case NCL_CMD_FW_RETRY_SET_FEATURE:
    #endif
		list_len = ncl_cmd->addr_param.common_param.list_len;
		info_list = ncl_cmd->addr_param.common_param.info_list;
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		break;
	case NCL_CMD_OP_CB_MODE0:
		list_len = ncl_cmd->addr_param.cb_param.list_len;
		info_list = ncl_cmd->addr_param.cb_param.info_list;
		//if (err == nand_err_enc) {
		if (1) {// Currently not support read error
			pda_list = ncl_cmd->addr_param.cb_param.addr->tlc_list_dst;
		} else {
			pda_list = ncl_cmd->addr_param.cb_param.addr->slc_list_src;
		}
		break;
	case NCL_CMD_OP_READ_ERD:
		{
			u32 du, len;
			len = ncl_cmd->addr_param.rapid_du_param.list_len;
			// When single DU erd, column address not updated, HW will enhance in future
			if (len != 1) {
				du = ncl_cmd->addr_param.rapid_du_param.pda & (DU_CNT_PER_PAGE - 1);
				if ((du <= pda) && ((du + len) > pda)) {// Within range
					//ncl_cmd_trace(LOG_NCL_ERR, 0xe151, "cmd %d, pda 0x%x, err %d(op %d)", ncl_cmd, ncl_cmd->addr_param.rapid_du_param.pda, err, ncl_cmd->op_code);
					//ncl_cmd_trace(LOG_ERR, 0xd62b, "TODO detail error info\n");
				} else {
					//ncl_cmd_trace(LOG_ERR, 0xede5, "Not found pda\n");
				}
			}

			if ((ncl_cmd->status & NCL_CMD_ERROR_STATUS) == 0){
				ncl_cmd->addr_param.rapid_du_param.info.status = cur_du_good;
				ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
			}
			if (err > ncl_cmd->addr_param.rapid_du_param.info.status) {
				ncl_cmd->addr_param.rapid_du_param.info.status = err;
			}
			return true;
		}
		break;
	default:
		ncl_cmd_trace(LOG_NCL_ERR, 0xd306, "This op %d should check!!",ncl_cmd->op_code);
		pda_list = NULL;
		info_list = NULL;
		list_len = 0;
		sys_assert(0);
		break;
	}

	// Software part error handling
	if ((ncl_cmd->status & NCL_CMD_ERROR_STATUS) == 0) {
		// The first time met error, int all PDA status to good
		// There are only MAX_NCL_CMD_SZ(8) space for info_list, DBG, LJ1-252, PgFalCmdTimeout
		for (i = 0; i < list_len; i++) {
			info_list[i].status = cur_du_good;
		}
		ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
	}

	// Search error PDA in ncl_cmd PDA list
	u32 hit = list_len;
    for (i = 0; i < list_len; i++)
    {
    	// DBG, LJ1-252, PgFalCmdTimeout
    	// Handle encode error.
        if (ncl_cmd->op_code == NCL_CMD_OP_WRITE && err != nand_err)
        {
			if (pda_list[i] == (pda & ~(DU_CNT_PER_PAGE - 1)))
			{
				for (duIdx = 0; duIdx < DU_CNT_PER_PAGE; duIdx++)
				{
					if (pda == pda_list[i] + duIdx)
					{
						if (ncl_req->w.lda[i * DU_CNT_PER_PAGE + duIdx] <= _max_capacity)
						{
							ncl_req->w.lda[i * DU_CNT_PER_PAGE + duIdx] |= ECC_LDA_POISON_BIT;
						}
						hit = i;
						found = true;

						ncl_cmd_trace(LOG_NCL_ERR, 0xcad8, "enc err: pda_list[%d], duIdx=%d, LDA[%d]=[0x%x]", i, duIdx, i * DU_CNT_PER_PAGE + duIdx, ncl_req->w.lda[i * DU_CNT_PER_PAGE + duIdx]);

						break;
					}
				}
			}

			if (found)
				break;
        }
        else
        {
            if (pda_list[i] == pda)
            {
                hit = i;
                found = true;
                if (info_list[i].status != cur_du_good)
                   continue;
                else
                   break;
            }
        }
	}
	i = hit;

    #ifdef PREVENT_PDA_MISMATCH
    if(!found)
    {
        if((ncl_cmd->op_code == NCL_CMD_SET_GET_FEATURE)
            #ifdef NCL_FW_RETRY_BY_SUBMIT
            || (ncl_cmd->op_code == NCL_CMD_FW_RETRY_SET_FEATURE)
            #endif
            )
        {
            //reg2.all = ficu_readl(FICU_STATUS_REG2);
            if(ncl_cmd->flags & NCL_CMD_RAPID_PATH)
            {
                i = 0;
                found = true;
            }
        }
        else if((ncl_cmd->op_code == NCL_CMD_OP_READ)
                || (ncl_cmd->op_code == NCL_CMD_OP_READ_STREAMING_FAST)
                || (ncl_cmd->op_code == NCL_CMD_PATROL_READ)
                || (ncl_cmd->op_code == NCL_CMD_OP_READ_FW_ARD)
                || (ncl_cmd->op_code == NCL_CMD_OP_READ_RAW)
                #ifdef NCL_FW_RETRY_BY_SUBMIT
                || (ncl_cmd->op_code == NCL_CMD_FW_RETRY_READ)
                #endif
            )
        {
            if(ncl_cmd->flags & NCL_CMD_RAPID_PATH)
            {
                i = 0;
                found = true;
            }
            else
            {
                if(list_len <= DU_CNT_PER_PAGE * 2) //2plane
                {
                    do
                    {
                        if(fins_cnt == reg2.b.ficu_err_finst_id)
                        {
                            i = du_idx + reg2.b.ficu_err_du_id;
                            found = true;
                            break;
                        }

                        du_cnt = get_read_du_cnt(pda_list + du_idx, list_len - du_idx);

                        if (du_cnt <= DU_CNT_PER_PAGE)
                        {
                            mp_cnt = 1;
                        }
                        #if HAVE_TSB_SUPPORT
                        else if (nand_support_aipr())
                        {
                            // When AIPR enabled, MP read cannot be used
                            mp_cnt = 1;
                            du_cnt = DU_CNT_PER_PAGE;
                        }
                        #endif
                        else if ((du_cnt & (du_cnt - 1)) == 0)
                        {
                            mp_cnt = du_cnt / DU_CNT_PER_PAGE;
                            du_cnt = DU_CNT_PER_PAGE;
                        }
                        else
                        {
                            mp_cnt = du_cnt / DU_CNT_PER_PAGE;
                            if (mp_cnt == 3)
                            {
                                mp_cnt = 2;
                            }
                            du_cnt = DU_CNT_PER_PAGE;
                        }

                        du_idx += du_cnt * mp_cnt;
                        fins_cnt++;
                    }while(du_idx < list_len);
                }
            }
        }
        //ncl_cmd_trace(LOG_ERR, 0, "[DBG]PDA mismatch, ncl_cmd[0x%x] op[%d] ncl_pda[0x%x] pda[0x%x] err[%d] du_id[%d] fins_id[%d]", ncl_cmd, ncl_cmd->op_code, pda_list[i], pda, err, reg2.b.ficu_err_du_id, reg2.b.ficu_err_finst_id);
		ncl_cmd_trace(LOG_ERR, 0x4357, "[DBG]PDA mismatch, ncl_cmd[0x%x] op[%d] ncl_pda[0x%x] pda[0x%x] err[%d]", ncl_cmd, ncl_cmd->op_code, pda_list[i], pda, err);
    }
    #endif

	if (i < list_len)
    {
		if (err == nand_err)
        {
			i += plane;
		}

		if ((ncl_cmd->op_code == NCL_CMD_OP_READ) ||(ncl_cmd->op_code == NCL_CMD_OP_READ_RAW)|| (ncl_cmd->op_code == NCL_CMD_OP_READ_STREAMING_FAST)
            #ifdef NCL_FW_RETRY_BY_SUBMIT
            || (ncl_cmd->op_code == NCL_CMD_FW_RETRY_READ)
            #endif
            || (ncl_cmd->op_code == NCL_CMD_OP_READ_FW_ARD)
           )
        {
			ncl_cmd_trace(LOG_DEBUG, 0xbc79, "NCL err: cmd 0x%x(op %d), pda 0x%x(0x%x, type %d), err %d", ncl_cmd, ncl_cmd->op_code, pda_list[i], (i << 16) + list_len, info_list[i].pb_type, err);
		}
        else
        {
			#if (SPOR_FLOW == mENABLE)
		    if(ncl_cmd->op_code != NCL_CMD_SPOR_SCAN_FIRST_PG)
			#endif
            {
                if(ncl_cmd->op_code == NCL_CMD_OP_WRITE || ncl_cmd->op_code == NCL_CMD_OP_ERASE)
                {
				    ncl_cmd_trace(LOG_ERR, 0x29b6, "NCL err: cmd 0x%x(op %d, flag 0x%x), pda 0x%x(0x%x), err %d", ncl_cmd,
		                  ncl_cmd->op_code, ncl_cmd->flags, pda_list[i], (i << 16) + list_len, err);
                }
           	}
		}

        #if (SPOR_FLOW == mENABLE) // to disable log while spor scan first page, added by Sunny 20201211
        if(ncl_cmd->op_code != NCL_CMD_SPOR_SCAN_FIRST_PG)
		#endif
        {
            //ncl_cmd_trace(LOG_NCL_ERR, 0, "NCL err: ch-ce-lun-pln 0x%x, block %d, pg %d\n", ((pda2ch(pda) << 24) | (pda2ce(pda) << 16) | (pda2lun(pda) << 8) | (pda2plane(pda))), pda2blk(pda), pda2page(pda));	// DBG, PgFalVry
            ncl_cmd_trace(LOG_DEBUG, 0x5817, "NCL err: ch-ce-lun-pln 0x%x, block %d, pg %d\n", ((pda2ch(pda_list[i]) << 24) | (pda2ce(pda_list[i]) << 16) | (pda2lun(pda_list[i]) << 8) | (pda2plane(pda_list[i]))), pda2blk(pda_list[i]), pda2page(pda_list[i]));
        }

		if (err > info_list[i].status) {// Severer error
            #if 0 //NCL_FW_RETRY
            if ((ncl_cmd->op_code == NCL_CMD_OP_READ)
                #ifdef NCL_FW_RETRY_BY_SUBMIT
                || (ncl_cmd->op_code ==NCL_CMD_FW_RETRY_READ)
                #endif
               )
            {
                if (ncl_cmd->retry_step == last_retry_read)
                {
                    info_list[i].status = cur_du_1bit_retry_err;
                    ncl_cmd->retry_step = retry_end;
                }
                else
                {
                    info_list[i].status = err;
                }
            }
            else
            {
                info_list[i].status = err;
            }
            #else
            info_list[i].status = err;
            #endif
		}
	}
#if ONFI_DCC_TRAINING
		if ((err == nand_err) && (ncl_cmd->op_code == NCL_CMD_OP_READ_RAW) && (ncl_cmd->flags & NCL_CMD_FEATURE_DCC_TRAINING)){
			ncl_cmd_trace(LOG_INFO, 0x98bf, "This time DCC Training fail ch %d, ce %d\n", pda2ch(pda), pda2ce(pda));
            printk("DCC training fail \n");
            ncl_dcc_read(ncl_cmd->addr_param.common_param.pda_list,DU_CNT_PER_PAGE);
		}
#endif
	return found;
}

fast_code void ncl_fcmd_pda_error(u32 fcmd_id, pda_t pda, u32 plane, enum ncb_err err)
{
	bool found = false;
	struct ncl_cmd_t* ncl_cmd;

	ncl_cmd = ncl_cmd_retrieve(fcmd_id);

	found = ncl_cmd_find_pda_index(ncl_cmd, pda, plane, err);
#if HAVE_CACHE_READ
	if (ncl_cmd->flags & NCL_CMD_CACHE_READ_FLAG) {
		u32 lun = pda2lun_id(pda);
		QSIMPLEQ_FOREACH(ncl_cmd, &lun_cache_ncl_cmd_queue[lun], entry) {
			found |= ncl_cmd_find_pda_index(ncl_cmd, pda, plane, err);
		}
		if (!found) {
			ncl_cmd_trace(LOG_ERR, 0xc75c, "Not found pda 0x%x\n", pda);
			sys_assert(0);
		}
	} else
#endif
	{
		if (!found) {
			ncl_cmd_trace(LOG_ERR, 0xb24e, "Not found pda 0x%x fcmd_id:%d\n", pda, fcmd_id);
			ncl_cmd_error_dump(ncl_cmd);
			extern void ficu_dump_fcmd(u32 du_dtag_ptr);
			ficu_dump_fcmd(ncl_cmd->dtag_ptr);
			sys_assert(0);
		}
	}
}

/*!
 * @brief Get DU count from beginning of PDA list (for read etc DU-unit operations, each PDA is a DU)
 *
 * @param pda_list	PDA list
 * @param pda_cnt	PDA count
 *
 * @return	Sequential DU count within a LUN
 */
fast_code int get_read_du_cnt(pda_t* pda_list, u32 pda_cnt)
{
	u32 cnt = 1;
	u32 i;
	for (i = 1; i < pda_cnt; i++) {
		if (pda_list[i] != (pda_list[i - 1] + 1)) {
			break;// Not DU sequential
		}
		if ((pda_list[i] & (DU_CNT_PER_PAGE - 1)) == 0) {// About to across page boundary
			if ((cnt & (DU_CNT_PER_PAGE - 1)) != 0) {// Not full page
				break;
			}
			if ((pda_list[i] & lun_read_bndry_check) == 0) {// About to across LUN boundary
				break;
			}
		}
		cnt++;
	}
	return cnt;
}

/*!
 * @brief Get MP count from beginning of PDA list (for erase/program etc page-unit operations, each PDA is a page)
 *
 * @param pda_list	PDA list
 * @param pda_cnt	PDA count
 *
 * @return	Sequential multi-plane count
 */
fast_code int get_mp_cnt(pda_t* pda_list, u32 pda_cnt)
{
	u32 cnt = 1;
	u32 i;
	for (i = 1; i < pda_cnt; i++) {
		pda_t pda1, pda2;
		pda1 = pda_list[i] & ((1 << nand_info.pda_block_shift) - 1);
		pda2 = pda_list[i - 1] & ((1 << nand_info.pda_block_shift) - 1);
		//if (pda1 != (pda2 + DU_CNT_PER_PAGE)) {
		//	break;// Not DU sequential
		//}
		//if ((pda_list[i] & lun_ep_bndry_check) == 0) {
		if (pda1 >> nand_info.pda_ch_shift != pda2 >> nand_info.pda_ch_shift){
			break;
		}
		cnt++;
	}
	return cnt;
}

#ifdef REFR_RD
fast_code void ncl_cmd_read_refresh(struct ncl_cmd_t *ncl_cmd)
{
    struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 fcmd_id = 0 ,fins_cnt = 0;
	u32 du_dtag_ptr, mp_cnt/*,times*/;
	u32 pl;
	#if OPEN_ROW == 1
	memset((void*)openRow, 0xFF, sizeof(openRow));
	#endif
    ///_GENE_20200104
    mp_cnt = nand_support_aipr() ? 1 : nand_plane_num();
    u8 rfrd_time = (ncl_cmd->addr_param.common_param.pda_list[0] & 0x7);//get refresh times
    //ncl_cmd_trace(LOG_ALW, 0xc43b, "[ncl_cmd_read_refresh] current_rfrd_die = %d rfrd_time =%d",rfrd_times_struct.current_rfrd_die,rfrd_time);
    ncl_cmd->addr_param.common_param.pda_list[0] &= (~0x07);//recover refresh pda
    u16 interleave_start = rfrd_time* nand_info.geo.nr_planes* rfrd_times_struct.current_rfrd_die;
    for (pl = 0;pl < nand_info.geo.nr_planes*rfrd_times_struct.current_rfrd_die ;pl += mp_cnt) 
    {
        #if (CPU_ID == 2)     
        if(pl & 0x10)  //if ch >= 4,pass
        {
            continue;
        }
        #elif(CPU_ID == 4)
        if(!(pl & 0x10))//if ch < 4,pass
        {
            continue;
        }
        #endif
        du_dtag_ptr = ncl_acquire_dtag_ptr();
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
        fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[0] | (interleave_start+pl)<<nand_info.pda_plane_shift;  
        /*if(pda2blk(fda_dtag->pda) == 0)
        {
            ncl_cmd_trace(LOG_ALW, 0x1c28, "[NCL_CMD]ncl_cmd_read_refresh pda = 0x%x ,PL[%d],CH[%d],CE[%d],LUN[%d]", 
            fda_dtag->pda,pda2plane(fda_dtag->pda),pda2ch(fda_dtag->pda),pda2ce(fda_dtag->pda),pda2lun(fda_dtag->pda));
        }*/
		ins = refresh_fins_templ;
		if (fins_cnt == 0) 
        {
			ins.dw0.b.fins = 1;
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}
		if (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG) 
        {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_REFRESH_READ_SLC;
		} 
        else
        {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_REFRESH_READ_LOW + pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[0]);
		}
        ins.dw0.b.mp_num = mp_cnt - 1;//_GENE_20200104
		if (fins_cnt == (nand_info.geo.nr_planes*rfrd_times_struct.current_rfrd_die>>1)-1) //CPU2 & CPU4 division,fins is reduced to half  
        {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
#if HAVE_T0
		ins.dw0.b.raw_addr_en = 1;
		fda_dtag->hdr = 0;
		fda_dtag->mp_num = nand_plane_num() - 1;
		fda_dtag->ch_id = pda2ch(fda_dtag->pda);
		fda_dtag->dev_id = pda2ce(fda_dtag->pda);
		fda_dtag->pda = pda2row(fda_dtag->pda, NAL_PB_TYPE_SLC);
#endif
#if HAVE_HW_CPDA
		convert_pda2cpda(&ins);
#endif
		ficu_fill_finst(ins);
		fins_cnt++;
	}
#if 0
    //for (pl = 0; pl <  nand_info.geo.nr_planes;pl += mp_cnt)
    for (pl = 0; pl <  (nand_info.geo.nr_planes>>1);pl += mp_cnt) 
    {
        du_dtag_ptr = ncl_acquire_dtag_ptr();

		// Configure fda list
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
        fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[0] | (pl << nand_info.pda_plane_shift);
        /*if(pda2blk(fda_dtag->pda) == 5)
        {
            ncl_cmd_trace(LOG_ALW, 0x1c28, "[NCL_CMD]ncl_cmd_read_refresh pda = 0x%x ,PL[%d],CH[%d],CE[%d],LUN[%d]", 
            fda_dtag->pda,pda2plane(fda_dtag->pda),pda2ch(fda_dtag->pda),pda2ce(fda_dtag->pda),pda2lun(fda_dtag->pda));
        }*/
		// Configure instruction
		ins = refresh_fins_templ;
		if (pl == 0)
        {
			ins.dw0.b.fins = 1;
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}

		if (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG)
        {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_REFRESH_READ_SLC;
		} else
        {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_REFRESH_READ_LOW + pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[0]);
		}
        ins.dw0.b.mp_num = mp_cnt - 1;//_GENE_20200104

		if (pl == (nand_info.geo.nr_planes>>1)-1) 
        {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
#if HAVE_T0
		ins.dw0.b.raw_addr_en = 1;
		fda_dtag->hdr = 0;
		fda_dtag->mp_num = nand_plane_num() - 1;
		fda_dtag->ch_id = pda2ch(fda_dtag->pda);
		fda_dtag->dev_id = pda2ce(fda_dtag->pda);
		fda_dtag->pda = pda2row(fda_dtag->pda, NAL_PB_TYPE_SLC);
#endif
#if HAVE_HW_CPDA
		convert_pda2cpda(&ins);
#endif
		ficu_fill_finst(ins);
		fins_cnt++;

	}
	// Final submit instruction(s)
    #endif
	ficu_submit_finstr(fins_cnt);
}
#endif

/*!
 * @brief Convert CPU memory address to HW used DMA memory address
 *
 * @param ptr	CPU address
 *
 * @return	DMA address
 */
fast_code u32 ptr2busmem(void* ptr)
{
	if ((u32)ptr >= SRAM_BASE) {
		return (u32)ptr;
	} else {
		sys_assert((u32)ptr >= BTCM_BASE);
		return btcm_to_dma(ptr);
	}
}

// Can only support 1 4KB DU, caller need prepare 2 dtags
fast_code void ncl_cmd_du_rawread(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	pda_t pda;

	// Can only support 1 DU
	sys_assert(ncl_cmd->addr_param.rw_raw_param.list_len == 1);
	//ncl_cmd_trace(LOG_ERR, 0, "du cnt %d\n", du_cnt);
	fcmd_id = ncl_acquire_dtag_ptr();

	// Configure fda list
	fda_dtag = ficu_get_addr_dtag_ptr(fcmd_id);
	fda_dtag->hdr = 0;
	pda = ncl_cmd->addr_param.common_param.pda_list[0];
	fda_dtag->fda_col= pda2column(pda);
	fda_dtag->ch_id = pda2ch(pda);
	fda_dtag->dev_id = pda2ce(pda);
	if (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG) {
		fda_dtag->row = pda2row(pda, NAL_PB_TYPE_SLC);
	} else {
		sys_assert(ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG);
		fda_dtag->row = pda2row(pda, NAL_PB_TYPE_XLC);
	}

	// Configure dtag
	fda_dtag->dtag.all = ncl_cmd->du_user_tag_list.all;
	fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list);

	// Configure instruction
	ins = rawread_fins_templ;
	if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pda2pg_idx(ncl_cmd->addr_param.rw_raw_param.pda_list[0]));
	} else {
		ins.dw0.b.xlc_en = 0;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD;
	}
	ins.dw2.b.btn_op_type = ncl_cmd->op_type;
	ncl_cmd_save(fcmd_id, ncl_cmd);
	ins.dw1.b.fcmd_id = fcmd_id;
	ins.dw1.b.du_dtag_ptr = fcmd_id;
	ficu_fill_finst(ins);

	ncl_cmd->dtag_ptr = fcmd_id;
	ficu_submit_finstr(1);
}

// This function only read page register, need work after ncl_cmd_read_refresh
fast_code void ncl_cmd_read_defect_mark(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 loop, nr_loop;
	u32 du_dtag_ptr;

#if DEFECT_MARK_USER
	nr_loop = 2;// Both 1st byte in user area and in spare area
#else
	nr_loop = 1;// Only 1st byte in spare area
#endif
	sys_assert(ncl_cmd->addr_param.fda_param.list_len == 1);
	for (loop = 0; loop < nr_loop; loop++) {
		du_dtag_ptr = ncl_acquire_dtag_ptr();
		// Configure fda list
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
		fda_dtag->hdr = 0;
		if (loop == (nr_loop - 1)) {
			fda_dtag->fda_col= nand_page_user_size();
		} else {
			fda_dtag->fda_col= 0;
		}
		fda_dtag->ch_id = ncl_cmd->addr_param.fda_param.ch;
		fda_dtag->dev_id = ncl_cmd->addr_param.fda_param.ce;
		fda_dtag->row = ncl_cmd->addr_param.fda_param.row;

		// Configure instruction
		ins = rawread_fins_templ;
		ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
		if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
		} else {
			ins.dw0.b.xlc_en = 0;
		}
		if (loop == 0) {
			ins.dw0.b.fins = 1;
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		} else {
			ins.dw0.b.fins = 0;
		}
		if (loop == (nr_loop - 1)) {
			ins.dw0.b.lins = 1;
		} else {
			ins.dw0.b.lins = 0;
		}
		ins.dw0.b.no_eccu_path = 1;
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		ins.dw1.b.xfcnt_sel = FINST_XFER_4B;
		ficu_fill_finst(ins);
	}

	ncl_cmd->dtag_ptr = du_dtag_ptr;
	ficu_submit_finstr(nr_loop);
}

fast_code void ncl_cmd_set_get_feature(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i;
	u32 du_dtag_ptr = 0;
	u32 fins_cnt = 0;

	for (i = 0; i < ncl_cmd->addr_param.rw_raw_param.list_len; i++) {
		du_dtag_ptr = ncl_acquire_dtag_ptr();
		// Configure fda list
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
		fda_dtag->hdr = 0;
		fda_dtag->ch_id = pda2ch(ncl_cmd->addr_param.rw_raw_param.pda_list[i]);
		fda_dtag->dev_id = pda2ce(ncl_cmd->addr_param.rw_raw_param.pda_list[i]);
		if (ncl_cmd->flags & NCL_CMD_FEATURE_LUN_FLAG) {
			u32 lun = pda2lun(ncl_cmd->addr_param.rw_raw_param.pda_list[i]);
			fda_dtag->fda_col = (ncl_cmd->addr_param.rw_raw_param.column[i].column << 8) + lun;
		} else {
			fda_dtag->fda_col = ncl_cmd->addr_param.rw_raw_param.column[i].column;
		}

		// Configure instruction
		ins = set_feature_tmpl;
		if (i == 0) {
			fcmd_id = du_dtag_ptr;
			ins.dw0.b.fins = 1;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}
		if ((i + 1) == ncl_cmd->addr_param.rw_raw_param.list_len) {
			ins.dw0.b.lins = 1;
		}
		if (ncl_cmd->flags & NCL_CMD_FEATURE_LUN_FLAG) {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		if (ncl_cmd->flags & NCL_CMD_FEATURE_GET_FLAG) {// Get feature
			ins.dw0.b.finst_type = FINST_TYPE_READ;
			ins.dw1.b.xfcnt_sel = FINST_XFER_8B;
		} else {// Set feature
			ins.dw2.all = ncl_cmd->sf_val;
		}
#if HAVE_HYNIX_SUPPORT
		if (ncl_cmd->flags & NCL_CMD_HYNIX_PARAM_FLAG) {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_PARAMETER;
			ins.dw1.b.xfcnt_sel = FINST_XFER_1B;
		}
#endif
		ficu_fill_finst(ins);
		fins_cnt++;
	}

	ncl_cmd->dtag_ptr = du_dtag_ptr;
	ficu_submit_finstr(fins_cnt);
}

static fast_code void ncl_cmd_read_fins_cfg(struct ncl_cmd_t *ncl_cmd, struct finstr_format* ins, u32 i) {
	u8 raid_cmd = get_raid_cmd(ncl_cmd);
	if (raid_cmd != NO_RAID){
		struct info_param_t *info = &ncl_cmd->addr_param.common_param.info_list[i];
		u32 mp_cnt = ins->dw0.b.mp_num + 1;
		ins->dw2.b.raid_cmd = raid_cmd;
		ins->dw2.b.raid_sram_bank_id = info[0].bank_id;
		if (mp_cnt ==1) {
			ins->dw3.b.raid_id0 = info[0].raid_id;
			ins->dw3.b.raid_id0_sram_bank_id = info[0].bank_id;
		} else if (mp_cnt == 2) {
			ins->dw3.b.raid_id0 = info[0].raid_id;
			ins->dw3.b.raid_id0_sram_bank_id = info[0].bank_id;

			ins->dw3.b.raid_id1 = info[1].raid_id;
			ins->dw3.b.raid_id1_sram_bank_id = info[1].bank_id;
		} else if (mp_cnt == 3) {
			ins->dw3.b.raid_id0 = info[0].raid_id;
			ins->dw3.b.raid_id0_sram_bank_id = info[0].bank_id;

			ins->dw3.b.raid_id1 = info[1].raid_id;
			ins->dw3.b.raid_id1_sram_bank_id = info[1].bank_id;

			ins->dw3.b.raid_id2 = info[2].raid_id;
			ins->dw3.b.raid_id2_sram_bank_id = info[2].bank_id;
		} else if (mp_cnt == 4) {
			ins->dw3.b.raid_id0 = info[0].raid_id;
			ins->dw3.b.raid_id0_sram_bank_id = info[0].bank_id;

			ins->dw3.b.raid_id1 = info[1].raid_id;
			ins->dw3.b.raid_id1_sram_bank_id = info[1].bank_id;

			ins->dw3.b.raid_id2 = info[2].raid_id;
			ins->dw3.b.raid_id2_sram_bank_id = info[2].bank_id;

			ins->dw3.b.raid_id3 = info[3].raid_id;
			ins->dw3.b.raid_id3_sram_bank_id = info[3].bank_id;
		}
	}

	ins->dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
#if !STABLE_REGRESSION
	ins->dw0.b.susp_en = !!(ncl_cmd->flags & NCL_CMD_HIGH_PRIOR_FLAG);
#endif
}
#if OPEN_ROW == 1
static fast_code void ncl_cmd_clear_openrow_table(void)
{
	if(openRow_table_all_clear == false)
	{
		memset((void*)openRow, 0xFF, 128*4);
		openRow_table_all_clear = true;
	}
}

static fast_code void open_row_handler(u8 via_sch ,u32 die_id, u32 pda, u32 mp_cnt)
{
	u32 actDiePlane;

		if(via_sch == VIA_SCH_TAG)
		{
			if (die_id >= MAX_DIE_MGR_CNT)
			{
				sys_assert(0);
			}

			if(mp_cnt > 1)
			{
		        for(u8 planenum = 0; planenum < mp_cnt; planenum++)
		        {
		            actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(die_id, planenum);
		            openRow[actDiePlane] = 0xFFFFFFFF;
				}
			}
			else
			{
				actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(die_id, pda2plane(pda));
		        openRow[actDiePlane] = 0xFFFFFFFF;
			}
		}
		else
		{
			evlog_printk(LOG_DEBUG,"[open-row] write/erase case clear all");
			ncl_cmd_clear_openrow_table();
		}

}
#endif
#if NCL_PDA
#if OPEN_ROW == 1
static fast_code void ncl_cmd_dus_read(struct ncl_cmd_t *ncl_cmd)
{
    //u32 CH = 0;  //TEST

	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	u32 du_dtag_ptr2 = 0;// For flag check set feature Finst
	u32 last_du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	struct fda_dtag_format *fda_dtag2;
	u32 i = 0;
	u32 mp_cnt;
	u32 du_cnt;
	u32 fins_cnt = 0;
    pda_t pda = 0;
    u32 actDiePlane;
	u8	die_id;
    u8	plane;
	u8  open_row_active = 0;

	die_id = ncl_cmd->die_id;
	do {
		if(ncl_cmd->via_sch == VIA_SCH_TAG)
		{
			du_cnt = ncl_cmd->addr_param.common_param.list_len;
			open_row_active = 1;
		}
		else
		{
			du_cnt = get_read_du_cnt(ncl_cmd->addr_param.common_param.pda_list + i, ncl_cmd->addr_param.common_param.list_len - i);
		}

		if (du_cnt <= DU_CNT_PER_PAGE) {
			mp_cnt = 1;
#if HAVE_TSB_SUPPORT
		} else if (nand_support_aipr()) {
			// When AIPR enabled, MP read cannot be used
			mp_cnt = 1;
			du_cnt = DU_CNT_PER_PAGE;
#endif
		} else if ((du_cnt & (du_cnt - 1)) == 0) {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			du_cnt = DU_CNT_PER_PAGE;
		} else {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			if (mp_cnt == 3) {
				mp_cnt = 2;
			}
			du_cnt = DU_CNT_PER_PAGE;
		}

		du_dtag_ptr = ncl_acquire_dtag_ptr();
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
        last_du_dtag_ptr = du_dtag_ptr;

		pda = ncl_cmd->addr_param.common_param.pda_list[i];
		// Configure fda list
		fda_dtag->pda = pda;
        //CH = (fda_dtag->pda >> nand_info.pda_ch_shift) & (nand_info.geo.nr_channels - 1);

#if POLL_CACHE_PROG
		if (cache_prog_check(ncl_cmd->addr_param.common_param.pda_list[i])) {
			cache_prog_end(ncl_cmd->addr_param.common_param.pda_list[i]);
			ins = status_fins_tmpl;
			if (fins_cnt == 0) {
				ins.dw0.b.fins = 1;
				fcmd_id = du_dtag_ptr;
				ncl_cmd_save(fcmd_id, ncl_cmd);
			}
			ins.dw0.b.mp_num = mp_cnt - 1;
			ins.dw1.b.fcmd_id = fcmd_id;
			ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
			ficu_fill_finst(ins);
			fins_cnt++;
		}
#endif

		if (fins_cnt == 0) {
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}

		/*
		 * Configure dtag
		 *
		 * Only when DU_CNT_PER_PAGE == 1 would du_cnt == 1 and mp_cnt >
		 * 1 happen at the same time, e.g. 4k pg size nand with multi-plane
		 * read.
		 */
#if (DU_CNT_PER_PAGE != 1)
		if (du_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[i].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}
#else
		if (mp_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[i].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}
#endif

		// Configure instruction
		ins = read_fins_templ;

		if (fins_cnt == 0)
		{
			ins.dw0.b.fins = 1;
		}

		plane = pda2plane(pda);
		//if(die_id != 0xFF)
		//{
		//	if(die_id >= 64)
		//	{
		//		//sys_assert(0);
		//		evlog_printk(LOG_INFO,"[open-row] read miss case clear all");
		//		memset((void*)openRow, 0xFF, 128*4);
		//		open_row_active = 0;
		//	}
		//	actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(die_id, plane);
		//}
		//else
		//{
		//	die_id = CHANGE_DIENUM_G2L(pda2die(pda));
		//	actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(die_id, plane);
		//}

		if(open_row_active)
		{
			if(die_id >= MAX_DIE_MGR_CNT)
			{
				sys_assert(0);
			}
			actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(die_id, plane);
		}
		else
		{
			evlog_printk(LOG_DEBUG,"[open-row] read case clear all");
			ncl_cmd_clear_openrow_table();
			open_row_active = 0;
			//sys_assert(0);
		}


		//if(ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG)
		if (ncl_cmd->addr_param.common_param.info_list[i].pb_type == NAL_PB_TYPE_XLC)
		{
			u32 pg_idx = pda2pg_idx(pda);
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
//#if HAVE_TSB_SUPPORT
//			if (mp_cnt > 1) {
//				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ_MP(pg_idx);
//			} else
//#endif
//			{
//				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
//			}
#if !HAVE_CACHE_READ
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
            #endif
            //eccu_set_ard_dec(ARD_MODE_MDEC);   //tony 20200818
#endif
			ins.dw0.b.pg_idx = pg_idx;
			if(open_row_active)
			{
				if(openRow[actDiePlane] == pda2ActDiePlane_Row(pda))
	            {//if this page had been read to page register,rand data out
	                //evlog_printk(LOG_INFO,"[R] hit CASE ,ch:%d, ce:%d, lun:%d, plane:%d, page:%d, du:%d",pda2ch(pda),pda2ce(pda),pda2lun(pda),pda2plane(pda),pda2page(pda),pda%4);

	                ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	    	        ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
	    	        ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
	            }
				else
				{
					if(plane == 0)
	                {
	                    u8 planeOffSet;
						//evlog_printk(LOG_INFO,"[R] p0 CASE ,ch:%d, ce:%d, lun:%d, plane:%d, page:%d, du:%d",pda2ch(pda),pda2ce(pda),pda2lun(pda),pda2plane(pda),pda2page(pda),pda%4);
	                    for(planeOffSet = 0; planeOffSet < nand_plane_num(); planeOffSet++)
	                    {
	                        if(planeOffSet)
	                        {
	                            ins = read_fins_templ;
	                            du_dtag_ptr2 = ncl_acquire_dtag_ptr();
	                            fda_dtag2 = ficu_get_addr_dtag_ptr(du_dtag_ptr2);
	                            last_du_dtag_ptr = du_dtag_ptr2;

	                            pda = (pda | 1 << nand_info.pda_plane_shift);
	                            fda_dtag2->pda = pda;
	                            actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(die_id, planeOffSet);
	                        	ins.dw1.b.du_dtag_ptr = du_dtag_ptr2;
	                        }else{
	                            ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	                        }
	                        ins.dw0.b.finst_type = FINST_TYPE_READ;
	                        ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
	                        ins.dw0.b.mp_num = 0;
	                        ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);


	                		ins.dw0.b.xlc_en = ROW_WL_ADDR;
	                		ins.dw0.b.pg_idx = pg_idx;
	                        ins.dw1.b.xfcnt_sel = FINST_XFER_ZERO;
	                        ins.dw1.b.fcmd_id = fcmd_id;

	                        openRow[actDiePlane] = pda2ActDiePlane_Row(pda);
							openRow_table_all_clear = false;
	                        ficu_fill_finst(ins);
	    		            fins_cnt++;
	                    }
	                    ins = read_fins_templ;

	        			//pg_idx = pda2pg_idx(pda);
	        			ins.dw0.b.xlc_en = ROW_WL_ADDR;
	        			ins.dw2.b.ard_schem_sel = ARD_DISABLE;
	        			ins.dw0.b.pg_idx = pg_idx;

	                    ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	    	            ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
	    	            ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
	                }
	                else
	                {//normal read and update openrow array
						//evlog_printk(LOG_INFO,"[R] p1 CASE ,ch:%d, ce:%d, lun:%d, plane:%d, page:%d, du:%d",pda2ch(pda),pda2ce(pda),pda2lun(pda),pda2plane(pda),pda2page(pda),pda%4);

						ins.dw0.b.finst_type = FINST_TYPE_READ;
	                    ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
	                    ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
						openRow[actDiePlane] = pda2ActDiePlane_Row(pda);
						openRow_table_all_clear = false;
	                }
				}
			}
			else
			{
				ins.dw0.b.finst_type = FINST_TYPE_READ;
                ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
                ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
			}

		}
        else
        {
			ins.dw0.b.pg_idx = 0;
#if HAVE_TSB_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD_MP;
			}
#endif
#if !HAVE_CACHE_READ
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_SLC;
            #endif
#endif

		}
		//tony 20200831
		//if (ard_mode == 2) {
		//	ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		//}
		ins.dw0.b.mp_num = mp_cnt - 1;

		// Group these flags that is not frequently used
		if (ncl_cmd->flags & (NCL_CMD_TAG_EXT_FLAG | NCL_CMD_RAID_FLAG | NCL_CMD_HIGH_PRIOR_FLAG)) {
			ncl_cmd_read_fins_cfg(ncl_cmd, &ins, i);
		}

		i += du_cnt * mp_cnt;

		if (i == ncl_cmd->addr_param.common_param.list_len)
        {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = last_du_dtag_ptr;
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		ins.dw2.b.du_num = du_cnt - 1;
		ins.dw2.b.meta_strip = !!(ncl_cmd->flags & NCL_CMD_META_DISCARD);
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
#if 1
		if ((ncl_cmd->du_format_no < DU_FMT_USER_4K) || (ncl_cmd->flags & NCL_CMD_DIS_ARD_FLAG)) {
			ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		}
#else
		ins.dw2.b.ard_schem_sel = ARD_DISABLE;
#endif
		ins.dw2.b.btn_op_type = ncl_cmd->op_type;
		if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
			ins.dw2.b.scrc_en = 0;
		ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
		ficu_fill_finst(ins);
		fins_cnt++;
	} while (i < ncl_cmd->addr_param.common_param.list_len);
	// Final submit instruction(s)
	ficu_submit_finstr(fins_cnt);
}

static fast_code void ncl_cmd_dus_streaming_fastread(struct ncl_cmd_t *ncl_cmd)
{
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	pda_t pda      = ncl_cmd->addr_param.common_param.pda_list[0];
	u32 flags      = ncl_cmd->flags;
	u32 fcmd_id    = 0;
	u32 fins_cnt   = 0;
	u32 du_dtag_ptr;
    u32 actDiePlane;


    actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(ncl_cmd->die_id, pda2plane(pda));

	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

	// Configure fda list
	fda_dtag->pda = pda;

	fcmd_id = du_dtag_ptr;
	ncl_cmd_save(fcmd_id, ncl_cmd);

		/*
		 * Configure dtag
		 *
		 * Only when DU_CNT_PER_PAGE == 1 would du_cnt == 1 and mp_cnt >
		 * 1 happen at the same time, e.g. 4k pg size nand with multi-plane
		 * read.
		 */
		fda_dtag->dtag.all = ncl_cmd->user_tag_list[0].all;

		// Configure instruction
		ins = read_streaming_fast_fins_templ;

		if (fins_cnt != 0)
		{
			ins.dw0.b.fins=0;
		}

		u32 pg_idx              = pda2pg_idx(pda);

		if(openRow[actDiePlane] == pda2ActDiePlane_Row(pda))
        {
		    //evlog_printk(LOG_INFO,"[4R]hit,ch:%d, ce:%d, lun:%d, plane:%d, page:%d, du:%d",pda2ch(pda),pda2ce(pda),pda2lun(pda),pda2plane(pda),pda2page(pda),pda%4);
	        ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	        ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
        }
        else
        {
            //evlog_printk(LOG_INFO,"[4R]1,ch:%d, ce:%d, lun:%d, plane:%d, page:%d, du:%d",pda2ch(pda),pda2ce(pda),pda2lun(pda),pda2plane(pda),pda2page(pda),pda%4);
            ins.dw0.b.finst_type = FINST_TYPE_READ;
            ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
            openRow[actDiePlane] = pda2ActDiePlane_Row(pda);
			openRow_table_all_clear = false;
        }

		ins.dw0.b.xlc_en        = ROW_WL_ADDR;
		ins.dw0.b.pg_idx        = pg_idx;
		ins.dw1.b.fcmd_id       = fcmd_id;
		ins.dw1.b.du_dtag_ptr   = du_dtag_ptr;
		//if ((ncl_cmd->du_format_no < DU_FMT_USER_4K) || (ncl_cmd->flags & NCL_CMD_DIS_ARD_FLAG))
		//{
		//	ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		//}
		//else
		//{
		//	ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
		//}

		ncl_cmd->dtag_ptr = du_dtag_ptr;

		// Group these flags that is not frequently used
		if (flags & (NCL_CMD_TAG_EXT_FLAG | NCL_CMD_RAID_FLAG | NCL_CMD_HIGH_PRIOR_FLAG))
		{
			ncl_cmd_read_fins_cfg(ncl_cmd, &ins, 0);
		}
		ins.dw2.b.meta_strip = !!(flags & NCL_CMD_META_DISCARD);
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		ins.dw2.b.btn_op_type = ncl_cmd->op_type;

		ficu_fill_finst(ins);

	// Final submit instruction(s)
	fins_cnt++;
	ficu_submit_finstr(fins_cnt);
}

#else
static fast_code void ncl_cmd_dus_read(struct ncl_cmd_t *ncl_cmd)
{
    //u32 CH = 0;  //TEST

	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
#if HAVE_MICRON_SUPPORT
	u32 du_dtag_ptr2 = 0;// For flag check set feature Finst
#endif
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0;
	u32 mp_cnt;
	u32 du_cnt;
	u32 fins_cnt = 0;

	do {
		du_cnt = get_read_du_cnt(ncl_cmd->addr_param.common_param.pda_list + i, ncl_cmd->addr_param.common_param.list_len - i);
		if (du_cnt <= DU_CNT_PER_PAGE) {
			mp_cnt = 1;
#if HAVE_TSB_SUPPORT
		} else if (nand_support_aipr()) {
			// When AIPR enabled, MP read cannot be used
			mp_cnt = 1;
			du_cnt = DU_CNT_PER_PAGE;
#endif
		} else if ((du_cnt & (du_cnt - 1)) == 0) {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			du_cnt = DU_CNT_PER_PAGE;
		} else {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			if (mp_cnt == 3) {
				mp_cnt = 2;
			}
			du_cnt = DU_CNT_PER_PAGE;
		}

		du_dtag_ptr = ncl_acquire_dtag_ptr();

		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

		// Configure fda list
		fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];
        //CH = (fda_dtag->pda >> nand_info.pda_ch_shift) & (nand_info.geo.nr_channels - 1);

#if POLL_CACHE_PROG
		if (cache_prog_check(ncl_cmd->addr_param.common_param.pda_list[i])) {
			cache_prog_end(ncl_cmd->addr_param.common_param.pda_list[i]);
			ins = status_fins_tmpl;
			if (fins_cnt == 0) {
				ins.dw0.b.fins = 1;
				fcmd_id = du_dtag_ptr;
				ncl_cmd_save(fcmd_id, ncl_cmd);
			}
			ins.dw0.b.mp_num = mp_cnt - 1;
			ins.dw1.b.fcmd_id = fcmd_id;
			ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
			ficu_fill_finst(ins);
			fins_cnt++;
		}
#endif

		if (fins_cnt == 0) {
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}

		/*
		 * Configure dtag
		 *
		 * Only when DU_CNT_PER_PAGE == 1 would du_cnt == 1 and mp_cnt >
		 * 1 happen at the same time, e.g. 4k pg size nand with multi-plane
		 * read.
		 */
#if (DU_CNT_PER_PAGE != 1)
		if (du_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[i].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}
#else
		if (mp_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[i].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}
#endif

		// Configure instruction
		ins = read_fins_templ;
		if (ncl_cmd->addr_param.common_param.info_list[i].pb_type == NAL_PB_TYPE_XLC)
        {
			u32 pg_idx = pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]);
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
#if HAVE_TSB_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ_MP(pg_idx);
			} else
#endif
			{
			    if (ncl_cmd->retry_step == last_1bit_vth_read)
                {         
				    ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_LNA_READ(pg_idx);
                }
                else
                {
                    ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
                }
			}
#if !HAVE_CACHE_READ
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
            #endif
            //eccu_set_ard_dec(ARD_MODE_MDEC);   //tony 20200818
#endif
			ins.dw0.b.pg_idx = pg_idx;
		}
        else
        {
			ins.dw0.b.pg_idx = 0;
#if HAVE_TSB_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD_MP;
			}
#endif
#if !HAVE_CACHE_READ
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_SLC;
            #endif
#endif

		}
		//tony 20200831
		//if (ard_mode == 2) {
		//	ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		//}
		ins.dw0.b.mp_num = mp_cnt - 1;
		if (fins_cnt == 0) {
			ins.dw0.b.fins = 1;
		}
		// Group these flags that is not frequently used
		if (ncl_cmd->flags & (NCL_CMD_TAG_EXT_FLAG | NCL_CMD_RAID_FLAG | NCL_CMD_HIGH_PRIOR_FLAG)) {
			ncl_cmd_read_fins_cfg(ncl_cmd, &ins, i);
		}

		i += du_cnt * mp_cnt;

		if (i == ncl_cmd->addr_param.common_param.list_len)
        {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		ins.dw2.b.du_num = du_cnt - 1;
		ins.dw2.b.meta_strip = !!(ncl_cmd->flags & NCL_CMD_META_DISCARD);
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
//        if (ncl_cmd->op_type == NCL_CMD_SPOR_SCAN_FIRST_PG)
//            evlog_printk(LOG_ALW, "du_fmt_sel %d", ins.dw2.b.du_fmt_sel);
#if 1
		if ((ncl_cmd->du_format_no < DU_FMT_USER_4K) || (ncl_cmd->flags & NCL_CMD_DIS_ARD_FLAG)) {
			ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		}
#else
		ins.dw2.b.ard_schem_sel = ARD_DISABLE;
#endif
		ins.dw2.b.btn_op_type = ncl_cmd->op_type;
		if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
			ins.dw2.b.scrc_en = 0;

#if defined(HMETA_SIZE)
        if (ncl_cmd->flags & NCL_CMD_DIS_HCRC_FLAG) {
            ins.dw2.b.scrc_en = 0;
        }
#endif
		ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
		ficu_fill_finst(ins);
		fins_cnt++;

	} while (i < ncl_cmd->addr_param.common_param.list_len);
	ficu_submit_finstr(fins_cnt);
}
static fast_code void ncl_cmd_read_data_training(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0;
	u32 mp_cnt;
	u32 du_cnt;
	u32 fins_cnt = 0;
	do {
		du_cnt = get_read_du_cnt(ncl_cmd->addr_param.common_param.pda_list + i, ncl_cmd->addr_param.common_param.list_len - i);
		if (du_cnt <= DU_CNT_PER_PAGE) {
			mp_cnt = 1;
#if HAVE_TSB_SUPPORT
		} else if (nand_support_aipr()) {
			mp_cnt = 1;
			du_cnt = DU_CNT_PER_PAGE;
#endif
		} else if ((du_cnt & (du_cnt - 1)) == 0) {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			du_cnt = DU_CNT_PER_PAGE;
		} else {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			if (mp_cnt == 3) {
				mp_cnt = 2;
			}
			du_cnt = DU_CNT_PER_PAGE;
		}
		du_dtag_ptr = ncl_acquire_dtag_ptr();
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
			// Disable flag check
        fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[0];
		if (fins_cnt == 0) {
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}
		if (du_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[0].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}

		// Configure instruction
		ins = read_fins_templ;

		ins.dw0.b.pg_idx = 0;
        ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		ins.dw0.b.mp_num = 0;
		if (fins_cnt == 0) {
			ins.dw0.b.fins = 1;
		}

		i += du_cnt * mp_cnt;
		if (i == ncl_cmd->addr_param.common_param.list_len)
        {
				ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
			}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		ins.dw2.b.du_num = du_cnt - 1;
		ins.dw2.b.meta_strip = !!(ncl_cmd->flags & NCL_CMD_META_DISCARD);
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;

		ins.dw2.b.btn_op_type = ncl_cmd->op_type;
		if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
			ins.dw2.b.scrc_en = 0;

#if ONFI_DCC_TRAINING
#if USE_TSB_NAND
	if (nand_is_bics3() || nand_is_bics4()) {// BiCS3 and BiCS4 use other DCC training method
		ncl_cmd->flags &= ~(NCL_CMD_RD_DQ_TRAINING | NCL_CMD_WR_DQ_TRAINING | NCL_CMD_FEATURE_DCC_TRAINING);
	}
#endif

	if (ncl_cmd->flags & NCL_CMD_RD_DQ_TRAINING) {
        ins.dw0.b.raw_addr_en = 1;
		fda_dtag->ch_id = pda2ch(ncl_cmd->addr_param.common_param.pda_list[0]);
		fda_dtag->dev_id = pda2ce(ncl_cmd->addr_param.common_param.pda_list[0]);
#if USE_FDA_FORMAT
		fda_dtag->vol_sel = fda_dtag->dev_id;
#endif
		fda_dtag->row = (0x82 << 24) | (0x5A << 16) | (0x35 << 8) | (pda2lun(ncl_cmd->addr_param.common_param.pda_list[i]));//this address pattern is refer onfi datasheet's data training
        fda_dtag->dtag = ncl_cmd->user_tag_list[0];
		ins.dw2.b.du_num = 0;
		ins.dw2.b.meta_strip = 0;
		ins.dw0.b.poll_dis	= 1;
		ins.dw0.b.mlun_en	= 0;
        ins.dw0.b.mp_enh    = 1;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DQ;
		ins.dw0.b.finst_type	= FINST_TYPE_READ_DATA;
		ins.dw1.b.xfcnt_sel	= FINST_XFER_AUTO;
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
	}

	if (ncl_cmd->flags & NCL_CMD_WR_DQ_TRAINING) {
		fda_dtag->ch_id = pda2ch(ncl_cmd->addr_param.common_param.pda_list[0]);
		fda_dtag->dev_id = pda2ce(ncl_cmd->addr_param.common_param.pda_list[0]);
#if USE_FDA_FORMAT
		fda_dtag->vol_sel = fda_dtag->dev_id;
#endif
		fda_dtag->row = pda2row(ncl_cmd->addr_param.common_param.pda_list[0], ncl_cmd->addr_param.common_param.info_list[0].pb_type);
		fda_dtag->dtag = ncl_cmd->user_tag_list[0];
		fda_dtag->fda_col = pda2lun(ncl_cmd->addr_param.common_param.pda_list[0]);
		ins.dw0.b.vsc_en = 1;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_VSC_WRITE_DQ_TRAINING_TX_READ;
		ins.dw0.b.mlun_en	= 0;
        ins.dw0.b.raw_addr_en = 1;
		ins.dw0.b.finst_type	= FINST_TYPE_READ;
		ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
		ins.dw2.b.du_num = 0;
		ins.dw2.b.meta_strip = 0;
		ins.dw0.b.poll_dis	= 1;
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		ins.dw2.b.scrc_en = 0;
	}

	if (ncl_cmd->flags & NCL_CMD_FEATURE_DCC_TRAINING) {
        ins.dw0.b.poll_dis = 0;
		ins.dw0.b.mlun_en		= 1;
		ins.dw0.b.raw_addr_en = 0;
		ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
		ins.dw0.b.fins_fuse = 1;
		ins.dw0.b.ndcmd_fmt_sel	= FINST_NAND_FMT_READ_DATA;
		ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
		ins.dw2.b.btn_op_type = INT_TABLE_READ_PRE_ASSIGN;
		ins.dw2.b.meta_strip = 0;
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		ins.dw2.b.du_num = 3;
		ins.dw2.b.host_trx_dis = 1;

#if 0
			ficu_fill_finst(ins);
			fins_cnt++;
		du_dtag_ptr = ncl_acquire_dtag_ptr();
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	    fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];
		fda_dtag->dtag = ncl_cmd->user_tag_list[i];
		ins = status_fins_tmpl;
		ins.dw0.b.mlun_en		= 0;
		ins.dw0.b.raw_addr_en = 0;
		ins.dw0.b.lins = 1;
        ins.dw0.b.ndcmd_fmt_sel	= FINST_NAND_FMT_READ_STATUS;
		ins.dw0.b.poll_dis	= 0;
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
        ins.dw2.b.du_fmt_sel = 0;
#endif
		}
#endif
		ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
		ficu_fill_finst(ins);
		fins_cnt++;
	} while (i < ncl_cmd->addr_param.common_param.list_len);
	// Final submit instruction(s)
	//ncl_cmd_trace(LOG_INFO, 0, "OP READ DIE_ID %d",((ncl_cmd->addr_param.common_param.pda_list[0]>>3)&0x7f));
	ficu_submit_finstr(fins_cnt);
}

static fast_code void ncl_cmd_dus_streaming_fastread(struct ncl_cmd_t *ncl_cmd)
{
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	pda_t pda      = ncl_cmd->addr_param.common_param.pda_list[0];
	u32 flags      = ncl_cmd->flags;
	u32 fcmd_id    = 0;
	u32 fins_cnt   = 0;
	u32 du_dtag_ptr;


	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

	// Configure fda list
	fda_dtag->pda = pda;

	fcmd_id = du_dtag_ptr;
	ncl_cmd_save(fcmd_id, ncl_cmd);

		/*
		 * Configure dtag
		 *
		 * Only when DU_CNT_PER_PAGE == 1 would du_cnt == 1 and mp_cnt >
		 * 1 happen at the same time, e.g. 4k pg size nand with multi-plane
		 * read.
		 */
		fda_dtag->dtag.all = ncl_cmd->user_tag_list[0].all;

		// Configure instruction
		ins = read_streaming_fast_fins_templ;

		if (fins_cnt != 0)
		{
			ins.dw0.b.fins=0;
		}

		u32 pg_idx              = pda2pg_idx(pda);
		ins.dw0.b.xlc_en        = ROW_WL_ADDR;
		ins.dw0.b.pg_idx        = pg_idx;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
		ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		ins.dw1.b.fcmd_id       = fcmd_id;
		ins.dw1.b.du_dtag_ptr   = du_dtag_ptr;

		ncl_cmd->dtag_ptr = du_dtag_ptr;

		// Group these flags that is not frequently used
		if (flags & (NCL_CMD_TAG_EXT_FLAG | NCL_CMD_RAID_FLAG | NCL_CMD_HIGH_PRIOR_FLAG))
		{
			ncl_cmd_read_fins_cfg(ncl_cmd, &ins, 0);
		}
		ins.dw2.b.meta_strip = !!(flags & NCL_CMD_META_DISCARD);
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		ins.dw2.b.btn_op_type = ncl_cmd->op_type;

		ficu_fill_finst(ins);

	// Final submit instruction(s)
	fins_cnt++;
	ficu_submit_finstr(fins_cnt);
}
#endif


#if HAVE_CACHE_READ
static fast_code void ncl_cmd_dus_ard(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0, j;
	u32 list_len;
	u32 mp_cnt;
	u32 du_cnt;
	u32 fins_cnt = 0;

	for (list_len = ncl_cmd->addr_param.common_param.list_len; list_len != 0; list_len--) {
		if (ficu_du_need_retry(ncl_cmd->addr_param.common_param.info_list[list_len - 1].status)) {
			break;
		}
	}
	if (list_len == 0) {
		sys_assert(0);
	}

	do {
		// Find first DU that need retry
		while (1) {
			if (ficu_du_need_retry(ncl_cmd->addr_param.common_param.info_list[i].status)) {
				break;
			}
			i++;
		}
		du_cnt = get_read_du_cnt(ncl_cmd->addr_param.common_param.pda_list + i, ncl_cmd->addr_param.common_param.list_len - i);
		// Only cover sequential DU that need retry
		for (j = 0; j < du_cnt; j++) {
			if (!ficu_du_need_retry(ncl_cmd->addr_param.common_param.info_list[i + j].status)) {
				break;
			}
		}
		du_cnt = j;
		if (du_cnt <= DU_CNT_PER_PAGE) {
			mp_cnt = 1;
		} else if ((du_cnt & (du_cnt - 1)) == 0) {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			du_cnt = DU_CNT_PER_PAGE;
		} else {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			if (mp_cnt == 3) {
				mp_cnt = 2;
			}
			du_cnt = DU_CNT_PER_PAGE;
		}

		du_dtag_ptr = ncl_acquire_dtag_ptr();

		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

		// Configure fda list
		fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];

#if POLL_CACHE_PROG
		if (cache_prog_check(ncl_cmd->addr_param.common_param.pda_list[i])) {
			cache_prog_end(ncl_cmd->addr_param.common_param.pda_list[i]);
			ins = status_fins_tmpl;
			if (fins_cnt == 0) {
				ins.dw0.b.fins = 1;
				fcmd_id = du_dtag_ptr;
				ncl_cmd_save(fcmd_id, ncl_cmd);
			}
			ins.dw0.b.mp_num = mp_cnt - 1;
			ins.dw1.b.fcmd_id = fcmd_id;
			ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
			ficu_fill_finst(ins);
			fins_cnt++;
		}
#endif

		if (fins_cnt == 0) {
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}

		/*
		 * Configure dtag
		 *
		 * Only when DU_CNT_PER_PAGE == 1 would du_cnt == 1 and mp_cnt >
		 * 1 happen at the same time, e.g. 4k pg size nand with multi-plane
		 * read.
		 */
#if (DU_CNT_PER_PAGE != 1)
		if (du_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[i].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}
#else
		if (mp_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[i].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}
#endif

		// Configure instruction
		ins = read_fins_templ;
		if (ncl_cmd->addr_param.common_param.info_list[i].pb_type == NAL_PB_TYPE_XLC) {
			u32 pg_idx = pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]);
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
#if HAVE_TSB_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ_MP(pg_idx);
			} else
#endif
			{
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
			}
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
            #endif

			ins.dw0.b.pg_idx = pg_idx;
		} else {
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_SLC
            #endif

			ins.dw0.b.pg_idx = 0;
#if HAVE_TSB_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD_MP;
			}
#endif
		}
		ins.dw0.b.mp_num = mp_cnt - 1;
		if (fins_cnt == 0) {
			ins.dw0.b.fins = 1;
		}

		i += du_cnt * mp_cnt;

		if (i == list_len) {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		ins.dw2.b.du_num = du_cnt - 1;
		// Group these flags that is not frequently used
		if (ncl_cmd->flags & (NCL_CMD_TAG_EXT_FLAG | NCL_CMD_RAID_FLAG | NCL_CMD_HIGH_PRIOR_FLAG)) {
			ncl_cmd_read_fins_cfg(ncl_cmd, &ins);
		}
		ins.dw2.b.meta_strip = !!(ncl_cmd->flags & NCL_CMD_META_DISCARD);
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		ins.dw2.b.btn_op_type = ncl_cmd->op_type;
		if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
			ins.dw2.b.scrc_en = 0;
		ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
		ficu_fill_finst(ins);
		fins_cnt++;
	} while (i < list_len);
	// Final submit instruction(s)
	ficu_submit_finstr(fins_cnt);
}
#endif

fast_code void ncl_cmd_pages_write(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0;
	u32 mp_cnt = 0;
	u32 fins_cnt = 0;
	u8 raid_cmd = get_raid_cmd(ncl_cmd);

	#if OPEN_ROW == 1
    u8 die_id;
	u8 via_sch;
	u32 pda;
    die_id = ncl_cmd->die_id;
	via_sch = ncl_cmd->via_sch;
	#endif

	while (i < ncl_cmd->addr_param.common_param.list_len) {
		mp_cnt = get_mp_cnt(ncl_cmd->addr_param.common_param.pda_list + i, ncl_cmd->addr_param.common_param.list_len - i);

		du_dtag_ptr = ncl_acquire_dtag_ptr();

		// Configure fda list
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
		if (mp_cnt == 1) {
			fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];
		} else {
			fda_dtag->pda_list = (pda_t*)ptr2busmem(&ncl_cmd->addr_param.common_param.pda_list[i]);
		}
		// Configure dtag
#if defined(TSB_XL_NAND) || defined(USE_8K_DU)
		if ((mp_cnt == 1) &&
			(((ncl_cmd->du_format_no == DU_2K_MR_MODE) && (SRB_MR_DU_CNT_PAGE == 1)) ||
				((ncl_cmd->du_format_no != DU_2K_MR_MODE) && (DU_CNT_PER_PAGE == 1)))) {
			fda_dtag->dtag = ncl_cmd->user_tag_list[i * DU_CNT_PER_PAGE];
		} else
#endif
		{
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i*DU_CNT_PER_PAGE);
		}
		#if OPEN_ROW == 1
		pda = ncl_cmd->addr_param.common_param.pda_list[i];

		//the mp_cmt insert to open_row_handler muster be 2
		//because the nand fetuure on open plane write the other plane's cache register will be clear.
		open_row_handler(via_sch, die_id, pda, 2);
		#endif

		// Configure instruction
		ins = prog_fins_templ;
		if (fins_cnt == 0) {
			ins.dw0.b.fins = 1;
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}
		ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		if (mp_cnt > 1) {
			ins.dw0.b.mp_num = mp_cnt - 1;
		}
		if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
			u32 pg_idx = pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]);
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
			ins.dw0.b.pg_idx = pg_idx;
#if HAVE_SAMSUNG_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_MP(pg_idx);
			} else {
				extern u32 pda2plane(pda_t pda);
				if (pda2plane(ncl_cmd->addr_param.common_param.pda_list[i]) == 0) {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_PL0(pg_idx);
				} else {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_PL1(pg_idx);
				}
			}
#else
			if (ncl_cmd->flags & NCL_CMD_CACHE_PROGRAM_FLAG) {
#if POLL_CACHE_PROG
				cache_prog_start(ncl_cmd->addr_param.common_param.pda_list[i]);
#endif
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_CACHE(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
			} else {
#if MULTI_PROG_STEPS
				if (ncl_cmd->addr_param.common_param.info_list[i].xlc.cb_step == PROG_2ND_STEP) {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
				} else {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_QLC_PROG_1ST(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
				}
#else
#if (Synology_case)	
				if (ncl_cmd->flags & NCL_CMD_GC_PROG_FLAG){
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_GC(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
				}else
#endif				
				{
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
				}
#endif
			}
#endif
		} else {
			ins.dw0.b.pg_idx = 0;
			if (ncl_cmd->flags & NCL_CMD_CACHE_PROGRAM_FLAG) {
#if POLL_CACHE_PROG
				cache_prog_start(ncl_cmd->addr_param.common_param.pda_list[i]);
#endif
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_CACHE_PROG;
			}
		}

		struct info_param_t *info = &ncl_cmd->addr_param.common_param.info_list[i];
		if (raid_cmd != NO_RAID) {
			if (raid_cmd == XOR_W_DATA || raid_cmd == POUT_W_DATA || raid_cmd == XOR_WO_DATA) {
                #ifndef SKIP_MODE
				sys_assert(mp_cnt && ((mp_cnt % 2) == 0));
                #endif
			}
			else
				sys_assert(mp_cnt == 1);

			ins.dw2.b.raid_cmd = raid_cmd;
			ins.dw2.b.raid_sram_bank_id = info[0].bank_id;

			if (mp_cnt ==1) {
				ins.dw3.b.raid_id0 = info[0].raid_id;
				ins.dw3.b.raid_id0_sram_bank_id = info[0].bank_id;
			} else if (mp_cnt == 2) {
				ins.dw3.b.raid_id0 = info[0].raid_id;
				ins.dw3.b.raid_id0_sram_bank_id = info[0].bank_id;

				ins.dw3.b.raid_id1 = info[1].raid_id;
				ins.dw3.b.raid_id1_sram_bank_id = info[1].bank_id;
			} else if (mp_cnt == 3) {
				ins.dw3.b.raid_id0 = info[0].raid_id;
				ins.dw3.b.raid_id0_sram_bank_id = info[0].bank_id;

				ins.dw3.b.raid_id1 = info[1].raid_id;
				ins.dw3.b.raid_id1_sram_bank_id = info[1].bank_id;

				ins.dw3.b.raid_id2 = info[2].raid_id;
				ins.dw3.b.raid_id2_sram_bank_id = info[2].bank_id;
			} else if (mp_cnt == 4) {
				ins.dw3.b.raid_id0 = info[0].raid_id;
				ins.dw3.b.raid_id0_sram_bank_id = info[0].bank_id;

				ins.dw3.b.raid_id1 = info[1].raid_id;
				ins.dw3.b.raid_id1_sram_bank_id = info[1].bank_id;

				ins.dw3.b.raid_id2 = info[2].raid_id;
				ins.dw3.b.raid_id2_sram_bank_id = info[2].bank_id;

				ins.dw3.b.raid_id3 = info[3].raid_id;
				ins.dw3.b.raid_id3_sram_bank_id = info[3].bank_id;
			}
		}

		if (ncl_cmd->du_format_no == DU_FMT_MR_4K) {
			ins.dw2.b.du_num = SRB_MR_DU_CNT_PAGE - 1;
#if DU_PADDING
			sys_assert(0);// DU padding & page padding cannot co-exist
#endif
		} else {
#if DU_PADDING
			if (ncl_cmd->du_format_no >= DU_FMT_USER_4K) {
				ins.dw2.b.phy_page_pad_en = 1;
				ins.dw1.b.xfcnt_sel = FINST_XFER_PAGE_PAD;
			}
#endif
			ins.dw2.b.du_num = DU_CNT_PER_PAGE - 1;
		}

		if (!(ncl_cmd->flags & NCL_CMD_HOST_INTERNAL_MIX)) {
			ins.dw2.b.btn_op_type = ncl_cmd->op_type;
			ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
#if defined(HMETA_SIZE)
			if (ncl_cmd->op_type == NCL_CMD_PROGRAM_TABLE || ncl_cmd->op_type == NCL_CMD_PROGRAM_DATA) {
				ins.dw2.b.scrc_en = 0;
			}
#else
            if (ncl_cmd->op_type == NCL_CMD_PROGRAM_TABLE) {
                ins.dw2.b.scrc_en = 0;
            }
#endif
            else if((ncl_cmd->op_type == NCL_CMD_PROGRAM_HOST) && (ncl_cmd->dis_hcrc & (BIT(i/mp_cnt)))){
                ins.dw2.b.scrc_en = 0;
            }
		} else {
			ins.dw2.b.btn_op_type = ncl_cmd->addr_param.common_param.info_list[i].op_type;
			//ins.dw2.b.btn_op_type = ncl_cmd->op_type;
#if defined(HMETA_SIZE)
            if (ins.dw2.b.btn_op_type == NCL_CMD_PROGRAM_TABLE || ins.dw2.b.btn_op_type == NCL_CMD_PROGRAM_DATA) {
                ins.dw2.b.scrc_en = 0;
            }
#else
            if (ins.dw2.b.btn_op_type == NCL_CMD_PROGRAM_TABLE) {
                ins.dw2.b.scrc_en = 0;
            }
#endif
            else if((ncl_cmd->op_type == NCL_CMD_PROGRAM_HOST) && (ncl_cmd->dis_hcrc & (BIT(i/mp_cnt)))){
                ins.dw2.b.scrc_en = 0;
            }
            ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		}
#if ONFI_DCC_TRAINING
		if (ncl_cmd->flags & NCL_CMD_WR_DQ_TRAINING) {
			fda_dtag->ch_id = pda2ch(ncl_cmd->addr_param.common_param.pda_list[i]);
			fda_dtag->dev_id = pda2ce(ncl_cmd->addr_param.common_param.pda_list[i]);
			fda_dtag->vol_sel = fda_dtag->dev_id;
			fda_dtag->row = 0;
			fda_dtag->fda_col = pda2lun(ncl_cmd->addr_param.common_param.pda_list[i]);
			fda_dtag->dtag = ncl_cmd->user_tag_list[i];
			ins.dw0.b.lins = 1;
			ins.dw0.b.raw_addr_en = 1;
			ins.dw0.b.poll_dis = 1;
			ins.dw0.b.mlun_en	= 0;
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_WRITE_DQ_PROG;
			ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
			ins.dw2.b.btn_op_type = INT_TABLE_WRITE_PRE_ASSIGN;
			ins.dw2.b.ard_schem_sel = 0;
			ins.dw2.b.du_num = 0;
			ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
			ins.dw2.b.scrc_en = 0;
			ins.dw2.b.phy_page_pad_en = 0;
		}
#endif
		i += mp_cnt;
		if (i == ncl_cmd->addr_param.common_param.list_len) {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
		ficu_fill_finst(ins);
		fins_cnt++;
	}
    //if(WUNC_bits){
    //    ncl_cmd_trace(LOG_ERR, 0, "wuncbits:0x%x,op_type:%d,mp_cnt:%d ", WUNC_bits,ncl_cmd->op_type,mp_cnt);
    //}
	// Final submit instruction(s)
	ficu_submit_finstr(fins_cnt);
	#if (PLP_NO_DONE_DEBUG == mENABLE)
	static bool print = true;
	if(plp_trigger && print)
	{
		print = false;
		ncl_cmd_trace(LOG_PLP, 0x9e76, "be Ok die id %d", ncl_cmd->die_id);
	}
	#endif

    #if PLP_DEBUG
    if(plp_trigger && ucache_flush_flag){
        ncl_cmd_trace(LOG_PLP, 0x1ca3, "die id %d die %d ", ncl_cmd->die_id, die);
    }
    #endif
}

fast_code void ncl_cmd_blocks_erase(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0;
	u32 mp_cnt;
	u32 fins_cnt = 0;
	#if OPEN_ROW == 1
	pda_t pda;
    u32 die_id;
	u8 via_sch;

	die_id = ncl_cmd->die_id;
	via_sch = ncl_cmd->via_sch;
	#endif
	while (i < ncl_cmd->addr_param.common_param.list_len) {
		mp_cnt = get_mp_cnt(ncl_cmd->addr_param.common_param.pda_list + i, ncl_cmd->addr_param.common_param.list_len - i);

		du_dtag_ptr = ncl_acquire_dtag_ptr();

		// Configure fda list
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
		if (mp_cnt == 1) {
			fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];
		} else {
			fda_dtag->pda_list = (pda_t*)ptr2busmem(&ncl_cmd->addr_param.common_param.pda_list[i]);
		}
		#if OPEN_ROW == 1
		pda = ncl_cmd->addr_param.common_param.pda_list[i];
		//evlog_printk(LOG_INFO,"[erase] mp_cnt:%d, ch:%d, ce:%d, lun:%d, plane:%d, page:%d",mp_cnt,pda2ch(pda),pda2ce(pda),pda2lun(pda),pda2plane(pda),pda2page(pda));
		open_row_handler(via_sch, die_id, pda, mp_cnt);
		#endif
#if POLL_CACHE_PROG
		if (cache_prog_check(ncl_cmd->addr_param.common_param.pda_list[i])) {
			cache_prog_end(ncl_cmd->addr_param.common_param.pda_list[i]);

			ins = status_fins_tmpl;
			if (fins_cnt == 0) {
				ins.dw0.b.fins = 1;
				fcmd_id = du_dtag_ptr;
				ncl_cmd_save(fcmd_id, ncl_cmd);
			}
			ins.dw0.b.mp_num = mp_cnt - 1;
			ins.dw0.b.mp_row_offset_en = 0;
			ins.dw1.b.fcmd_id = fcmd_id;
			ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
			ficu_fill_finst(ins);
			fins_cnt++;
		}
#endif

		// Configure instruction
		ins = erase_fins_tmpl;
		if (fins_cnt == 0) {
			ins.dw0.b.fins = 1;
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_ERASE;
		} else {
			/* Please make sure you set either SLC or XLC_PB_TYPE_FLAG at this stage.
			  I assume we don't have SLC/TLC mix erase in same ncl_cmd. So I don't refer
			  to info_list in ncl_cmd.
			*/
			//sys_assert(ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG);
		}
		if (mp_cnt > 1) {
			ins.dw0.b.mp_num = mp_cnt - 1;
			ins.dw0.b.mp_row_offset_en = 0;
		}

		i += mp_cnt;
		if (i == ncl_cmd->addr_param.common_param.list_len) {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ficu_fill_finst(ins);
		fins_cnt++;
	}

	// Final submit instruction(s)
	ficu_submit_finstr(fins_cnt);
}
#endif

#if NCL_CPDA
// Temporary debug code to check all FDA within same die
fast_code void ncl_cmd_within_die_check(struct ncl_cmd_t *ncl_cmd)
{
	u32 ch, ce, lun;
	u32 i;
	sys_assert(ncl_cmd->op_code == NCL_CMD_OP_ERASE);

	ch = ncl_cmd->addr_param.fda_param.fda_list[0].b.ch;
	ce = ncl_cmd->addr_param.fda_param.fda_list[0].b.ce;
	lun = ncl_cmd->addr_param.fda_param.fda_list[0].b.row >> nand_info.row_lun_shift;

	for (i = 1; i < ncl_cmd->addr_param.fda_param.list_len; i++) {
		if (ch != ncl_cmd->addr_param.fda_param.fda_list[i].b.ch) {
			sys_assert(0);
		}
		if (ce != ncl_cmd->addr_param.fda_param.fda_list[i].b.ce) {
			sys_assert(0);
		}
		if (lun != (ncl_cmd->addr_param.fda_param.fda_list[0].b.row >> nand_info.row_lun_shift)) {
			sys_assert(0);
		}
	}
}

fast_code int get_mp_cnt_by_fda(Fda_t* fda_list, u32 fda_cnt)
{
	u32 cnt = 1;
	u32 i;
	for (i = 1; i < fda_cnt; i++) {
		if (fda_list[i].b.row != (fda_list[i - 1].b.row + (1 << nand_info.row_block_shift))) {
			break;
		}
		cnt++;
		if (cnt == nand_plane_num()) {
			break;
		}
	}
	return cnt;
}

static fast_code void ncl_cmd_dus_read(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0;
	u32 mp_cnt;
	u32 du_cnt;
	u32 fins_cnt = 0;

	ncl_cmd_within_die_check(ncl_cmd);

	do {
		du_cnt = get_read_du_cnt(ncl_cmd->addr_param.common_param.pda_list + i, ncl_cmd->addr_param.common_param.list_len - i);
		if (du_cnt <= DU_CNT_PER_PAGE) {
			mp_cnt = 1;
#if HAVE_TSB_SUPPORT
		} else if (nand_support_aipr()) {
			// When AIPR enabled, MP read cannot be used
			mp_cnt = 1;
			du_cnt = DU_CNT_PER_PAGE;
#endif		} else if ((du_cnt & (du_cnt - 1)) == 0) {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			du_cnt = DU_CNT_PER_PAGE;
		} else {
			mp_cnt = du_cnt / DU_CNT_PER_PAGE;
			if (mp_cnt == 3) {
				mp_cnt = 2;
			}
			du_cnt = DU_CNT_PER_PAGE;
		}

		du_dtag_ptr = ncl_acquire_dtag_ptr();
		if (fins_cnt == 0) {
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}

		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

		// Configure fda list
		fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];
		pda_t pda;
		extern pda_t cpda2pda(pda_t cpda);
		pda = cpda2pda(ncl_cmd->addr_param.common_param.pda_list[i]);
#if POLL_CACHE_PROG
		if (cache_prog_check(pda)) {
			cache_prog_end(pda);
			ins = status_fins_tmpl;
			if (fins_cnt == 0) {
				ins.dw0.b.fins = 1;
				fcmd_id = du_dtag_ptr;
				ncl_cmd_save(fcmd_id, ncl_cmd);
			}
			ins.dw1.b.fcmd_id = fcmd_id;
			ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
			ficu_fill_finst(ins);
			fins_cnt++;
		}
#endif

		// Configure dtag
		if (du_cnt == 1) {
			fda_dtag->dtag.all = ncl_cmd->user_tag_list[i].all;
		} else {
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i);
		}

		// Configure instruction
		ins = read_fins_templ;
		if (ncl_cmd->addr_param.common_param.info_list[i].pb_type == NAL_PB_TYPE_XLC)
        {
		//if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
			u32 pg_idx = pda2pg_idx(pda);
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
#if HAVE_TSB_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ_MP(pg_idx);
			} else
#endif
			{
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
			}
#if !HAVE_CACHE_READ
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
            #endif
#endif
			ins.dw0.b.pg_idx = pg_idx;
		}
        else
        {
			ins.dw0.b.pg_idx = 0;
#if HAVE_TSB_SUPPORT
			if (mp_cnt > 1)
            {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD_MP;
			}
#endif
#if !HAVE_CACHE_READ
            #if NCL_FW_RETRY
            ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            #else
    		ins.dw2.b.ard_schem_sel = ARD_TMPLT_SLC;
            #endif
#endif
		}

		ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
		ins.dw0.b.mp_num = mp_cnt - 1;
		if (fins_cnt == 0) {
			ins.dw0.b.fins = 1;
		}
		i += du_cnt * mp_cnt;

		if (i == ncl_cmd->addr_param.common_param.list_len) {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		ins.dw2.b.btn_op_type = ncl_cmd->op_type;
		if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
			ins.dw2.b.scrc_en = 0;
		ins.dw2.b.du_num = du_cnt - 1;
		if (ncl_cmd->flags & NCL_CMD_META_DISCARD) {
			ins.dw2.b.meta_strip = 1;
		}
#if !STABLE_REGRESSION
		if (ncl_cmd->flags & NCL_CMD_HIGH_PRIOR_FLAG) {
			ins.dw0.b.susp_en = 1;
		}
#endif
		ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
#if 1
		if (ncl_cmd->du_format_no < DU_FMT_USER_4K) {
			ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		}
#else
		ins.dw2.b.ard_schem_sel = ARD_DISABLE;
#endif
		ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
		ficu_fill_finst(ins);
		fins_cnt++;
	} while (i < ncl_cmd->addr_param.common_param.list_len);

	// Final submit instruction(s)
	ficu_submit_finstr(fins_cnt);
}

fast_code void ncl_cmd_pages_write(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0;
	u32 mp_cnt;
	u32 fins_cnt = 0;

	ncl_cmd_within_die_check(ncl_cmd);

	while (i < ncl_cmd->addr_param.fda_param.list_len) {
		mp_cnt = get_mp_cnt_by_fda(ncl_cmd->addr_param.fda_param.fda_list + i, ncl_cmd->addr_param.fda_param.list_len - i);

		du_dtag_ptr = ncl_acquire_dtag_ptr();

		// Configure cpda list
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
		if (mp_cnt == 1) {
			fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];
		} else {
			fda_dtag->pda_list = (pda_t*)ptr2busmem(&ncl_cmd->addr_param.common_param.pda_list[i]);
		}
		// Configure dtag
#if defined(TSB_XL_NAND) || defined(USE_8K_DU)
		if ((mp_cnt == 1) &&
			(((ncl_cmd->du_format_no == DU_2K_MR_MODE) && (SRB_MR_DU_CNT_PAGE == 1)) ||
				((ncl_cmd->du_format_no != DU_2K_MR_MODE) && (DU_CNT_PER_PAGE == 1)))) {
			fda_dtag->dtag = ncl_cmd->user_tag_list[i * DU_CNT_PER_PAGE];
		} else
#endif
		{
			fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + i*DU_CNT_PER_PAGE);
		}

		// Configure instruction
		ins = prog_fins_templ;
		if (i == 0) {
			ins.dw0.b.fins = 1;
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		if (mp_cnt > 1) {
			ins.dw0.b.mp_num = mp_cnt - 1;
		}
		if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
			u32 pg_idx = ncl_cmd->addr_param.fda_param.fda_list[i].b.tsb_prefix;
			ins.dw0.b.xlc_en = ROW_WL_ADDR;
			ins.dw0.b.pg_idx = pg_idx;
#if HAVE_SAMSUNG_SUPPORT
			if (mp_cnt > 1) {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_MP(pg_idx);
			} else {
				extern u32 pda2plane(pda_t pda);
				if (((fda_dtag->row >> nand_info.row_pl_shift) & (nand_plane_num() - 1)) == 0) {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_PL0(pg_idx);
				} else {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_PL1(pg_idx);
				}
			}
#else
			if (ncl_cmd->flags & NCL_CMD_CACHE_PROGRAM_FLAG) {
#if POLL_CACHE_PROG
				u32 lun = fda_dtag->row >> nand_info.row_lun_shift;
				u32 lun_id = ((lun * nand_target_num()) + fda_dtag->dev_id) * nand_channel_num() + fda_dtag->ch_id;
				cache_prog_lun_bmp[lun_id >> 5] |= 1 << (lun_id & 0x1F);
#endif
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG_CACHE(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
			} else {
#if MULTI_PROG_STEPS
				if (ncl_cmd->addr_param.common_param.info_list[i].xlc.cb_step == PROG_2ND_STEP) {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
				} else {
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_QLC_PROG_1ST(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
				}
#else
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG(pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[i]));
#endif
			}
#endif
		} else {
			ins.dw0.b.pg_idx = 0;
			if (ncl_cmd->flags & NCL_CMD_CACHE_PROGRAM_FLAG) {
#if POLL_CACHE_PROG
				u32 lun = fda_dtag->row >> nand_info.row_lun_shift;
				u32 lun_id = ((lun * nand_target_num()) + fda_dtag->dev_id) * nand_channel_num() + fda_dtag->ch_id;
				cache_prog_lun_bmp[lun_id >> 5] |= 1 << (lun_id & 0x1F);
#endif
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_CACHE_PROG;
			}
		}

		if (ncl_cmd->du_format_no == DU_FMT_MR_4K) {
			ins.dw2.b.du_num = SRB_MR_DU_CNT_PAGE - 1;
		} else {
			ins.dw2.b.du_num = DU_CNT_PER_PAGE - 1;
		}
		if (!(ncl_cmd->flags & NCL_CMD_HOST_INTERNAL_MIX)) {
			ins.dw2.b.btn_op_type = ncl_cmd->op_type;
			ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		} else {
			ins.dw2.b.btn_op_type = ncl_cmd->addr_param.common_param.info_list[i].op_type;
			if (ins.dw2.b.btn_op_type == NCL_CMD_PROGRAM_DATA)
				ins.dw2.b.du_fmt_sel = DU_4K_DEFAULT_MODE;
			else
				ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
		}
#if ONFI_DCC_TRAINING
		if (ncl_cmd->flags & NCL_CMD_WR_DQ_TRAINING) {
			fda_dtag->ch_id = pda2ch(ncl_cmd->addr_param.common_param.pda_list[i]);
			fda_dtag->dev_id = pda2ce(ncl_cmd->addr_param.common_param.pda_list[i]);
			fda_dtag->vol_sel = fda_dtag->dev_id;
			fda_dtag->row = 0;
			fda_dtag->fda_col = pda2lun(ncl_cmd->addr_param.common_param.pda_list[i]);
			fda_dtag->dtag = ncl_cmd->user_tag_list[i];
			ins.dw0.b.lins = 1;
			ins.dw0.b.raw_addr_en = 1;
			ins.dw0.b.poll_dis = 1;
			ins.dw0.b.mlun_en	= 0;
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_WRITE_DQ_PROG;
			//ins.dw0.b.no_eccu_path = 1;
			ins.dw1.b.xfcnt_sel = FINST_XFER_AUTO;
			ins.dw2.b.btn_op_type = INT_TABLE_WRITE_PRE_ASSIGN;
			ins.dw2.b.ard_schem_sel = 0;
			ins.dw2.b.du_num = 0;
			ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
			ins.dw2.b.scrc_en = 0;
			ins.dw2.b.phy_page_pad_en = 0;
		}
#endif
		i += mp_cnt;
		if (i == ncl_cmd->addr_param.fda_param.list_len) {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}

		if (ncl_cmd->op_type == NCL_CMD_PROGRAM_TABLE)
			ins.dw2.b.scrc_en = 0;
		ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
		ficu_fill_finst(ins);
		fins_cnt++;
	}

	// Final submit instruction(s)
	ficu_submit_finstr(fins_cnt);
}

fast_code void ncl_cmd_blocks_erase(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i = 0;
	u32 mp_cnt;
	u32 fins_cnt = 0;

	ncl_cmd_within_die_check(ncl_cmd);

	while (i < ncl_cmd->addr_param.fda_param.list_len) {
		mp_cnt = get_mp_cnt_by_fda(ncl_cmd->addr_param.fda_param.fda_list + i, ncl_cmd->addr_param.fda_param.list_len - i);

		du_dtag_ptr = ncl_acquire_dtag_ptr();

		// Configure cpda list
		fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
		if (mp_cnt == 1) {
			fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[i];
		} else {
			fda_dtag->pda_list = (pda_t*)ptr2busmem(&ncl_cmd->addr_param.common_param.pda_list[i]);
		}

#if POLL_CACHE_PROG
		u32 lun = fda_dtag->row >> nand_info.row_lun_shift;
		u32 lun_id = ((lun * nand_target_num()) + fda_dtag->dev_id) * nand_channel_num() + fda_dtag->ch_id;
		//if (cache_prog_check(ncl_cmd->addr_param.fda_param.pda_list[i])) {
			//cache_prog_end(ncl_cmd->addr_param.fda_param.pda_list[i]);
		if (cache_prog_lun_bmp[lun_id >> 5] & (1 << (lun_id & 0x1F))) {
			cache_prog_lun_bmp[lun_id >> 5] &= ~(1 << (lun_id & 0x1F));
			ins = status_fins_tmpl;
			if (fins_cnt == 0) {
				ins.dw0.b.fins = 1;
				fcmd_id = du_dtag_ptr;
				ncl_cmd_save(fcmd_id, ncl_cmd);
			}
			ins.dw0.b.raw_addr_en = 1;
			ins.dw1.b.fcmd_id = fcmd_id;
			ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
			ficu_fill_finst(ins);
			fins_cnt++;
		}
#endif

		// Configure instruction
		ins = erase_fins_tmpl;
		if (i == 0) {
			ins.dw0.b.fins = 1;
			fcmd_id = du_dtag_ptr;
			ncl_cmd_save(fcmd_id, ncl_cmd);
		}
		ins.dw1.b.fcmd_id = fcmd_id;
		ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
		if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_ERASE;
		} else {
			/* Please make sure you set either SLC or XLC_PB_TYPE_FLAG at this stage.
			  I assume we don't have SLC/TLC mix erase in same ncl_cmd. So I don't refer
			  to info_list in ncl_cmd.
			*/
			//sys_assert(ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG);
		}
		if (mp_cnt > 1) {
			ins.dw0.b.mp_num = mp_cnt - 1;
		}

		i += mp_cnt;
		if (i == ncl_cmd->addr_param.fda_param.list_len) {
			ins.dw0.b.lins = 1;
			ncl_cmd->dtag_ptr = du_dtag_ptr;
		}
		ficu_fill_finst(ins);
		fins_cnt++;
	}

	// Final submit instruction(s)
	ficu_submit_finstr(fins_cnt);
}
#endif

ddr_code void ncl_cmd_dus_read_ard(struct ncl_cmd_t *ncl_cmd)
{
//Revised in 2023.06.02
    u32 fcmd_id = 0;
    u32 du_dtag_ptr;
    struct fda_dtag_format *fda_dtag;

    u32 finst_cnt = 0;
    struct finstr_format sf_ins; //For set feature.
    struct finstr_format rd_ins; //For HD read and SD read.

    pda_t pda = ncl_cmd->addr_param.common_param.pda_list[0];
	fw_ard_cnt++;

    ncl_cmd_trace(LOG_ALW, 0xa0de, "Enable FW ARD, 2Bit Retry!!!, pda = 0x%x", pda);

    u8 lun_id = pda2lun(pda);
    u8 pg_idx = pda2pg_idx(pda);
    u8 interleave = (pda >> nand_info.pda_interleave_shift) & (nand_info.interleave - 1); //Interleave 0~127
    FW_ARD_EN = true;

    //set feature for normal read
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fcmd_id = du_dtag_ptr;
    ncl_cmd_save(fcmd_id, ncl_cmd);     //register fcmd_id for ncl_cmd

    fda_dtag->hdr = 0;
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);

    if (nand_support_aipr() && (pda2plane(pda) == 1)) {
    	// When AIPR enabled, for plane 1 ,89h/8Ah/8Bh changed to 85h/86h/87h
    	fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] - 4) << 8);
    }
    else if(nand_support_aipr() && (pda2plane(pda) == 2)){
        fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] +0x20) << 8);
    }
    else if(nand_support_aipr() && (pda2plane(pda) == 3)){
        fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] +0x20- 4) << 8);
    }
    else {
        fda_dtag->fda_col = lun_id | (Retry_feature_addr[pg_idx] << 8);
    }

    extern struct finstr_format set_feature_tmpl;
    sf_ins = set_feature_tmpl;
    sf_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
    sf_ins.dw0.b.fins = 1;      //First FINST
    sf_ins.dw0.b.lins = 0;
    sf_ins.dw0.b.fins_fuse = 1;
    sf_ins.dw1.b.fcmd_id = fcmd_id;
    sf_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;


    if (pg_idx != 2){
        sf_ins.dw2.all = current_shift_value[interleave][0];
    }
    else{
        sf_ins.dw2.all = current_shift_value[interleave][1];
    }

    ficu_fill_finst(sf_ins);
    finst_cnt++;

    //Normal read
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->fda_col= pda2column(pda);
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);
    fda_dtag->pda = pda;

    fda_dtag->dtag.all = ncl_cmd->user_tag_list[0].all;

    // Configure instruction
    rd_ins = read_fins_templ;

    rd_ins.dw0.b.xlc_en = ROW_WL_ADDR;
    rd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
    rd_ins.dw0.b.pg_idx = pg_idx;
    rd_ins.dw0.b.fins = 0;
    rd_ins.dw0.b.lins = 0;
    rd_ins.dw0.b.mp_num = 0;
    rd_ins.dw0.b.mlun_en = 0;    //close interleaving operation
    rd_ins.dw0.b.raw_addr_en = 0;
    rd_ins.dw0.b.fins_fuse = 1;
    rd_ins.dw0.b.mp_enh = 0;
    rd_ins.dw1.b.fcmd_id = fcmd_id;
    rd_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;

    rd_ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
    rd_ins.dw2.b.btn_op_type = ncl_cmd->op_type;
    rd_ins.dw2.b.ard_schem_sel = 1; //FW_ARD
    rd_ins.dw2.b.du_num = 0;

    //Add FINST settings 230727
    rd_ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
    rd_ins.dw2.b.meta_strip = !!(ncl_cmd->flags & NCL_CMD_META_DISCARD);

    if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
    {
        rd_ins.dw2.b.scrc_en = 0;
    }

#if defined(HMETA_SIZE)
    if (ncl_cmd->flags & NCL_CMD_DIS_HCRC_FLAG)
    {
        rd_ins.dw2.b.scrc_en = 0;
    }
#endif

    ficu_fill_finst(rd_ins);
    finst_cnt++;

    //set feature Positive for FW ARD.
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);

    if(pg_idx == 2){
        fda_dtag->fda_col = lun_id | (0x95<< 8);
    }
    else{
        fda_dtag->fda_col = lun_id | (0x94<< 8);
    }

    sf_ins = set_feature_tmpl;
    sf_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
    sf_ins.dw0.b.fins = 0;
    sf_ins.dw0.b.lins = 0;
    sf_ins.dw0.b.fins_fuse = 1;
    sf_ins.dw1.b.fcmd_id = fcmd_id;

    sf_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;

    //set Positive value for vary pg_idx.
    //SB_POSITIVE_SHIFT_VALUE = 10
    if(pg_idx==0){
        sf_ins.dw2.all = ((0xFF<<16) & (current_shift_value[interleave][0]+(SB_POSITIVE_SHIFT_VALUE<<16)));
    }
    else if(pg_idx==1){
        sf_ins.dw2.all =   ((0xFF) & (current_shift_value[interleave][0]+SB_POSITIVE_SHIFT_VALUE))
                          |((0xFF<<8) & (current_shift_value[interleave][0]+(SB_POSITIVE_SHIFT_VALUE<<8)))
                          |((0xFF<<24) & (current_shift_value[interleave][0]+(SB_POSITIVE_SHIFT_VALUE<<24)));
    }
    else{
        sf_ins.dw2.all =   ((0xFF) & (current_shift_value[interleave][1]+SB_POSITIVE_SHIFT_VALUE))
                          |((0xFF<<8) & (current_shift_value[interleave][1]+(SB_POSITIVE_SHIFT_VALUE<<8)))
                          |((0xFF<<16) & (current_shift_value[interleave][1]+(SB_POSITIVE_SHIFT_VALUE<<16)));
    }

    ficu_fill_finst(sf_ins);
    finst_cnt++;

    //set feature Negative for FW ARD.
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);

    if(pg_idx==2){
        fda_dtag->fda_col = lun_id | (0x93<< 8);
    }
    else{
        fda_dtag->fda_col = lun_id | (0x92<< 8);
    }

    sf_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;

    //set Negative value for vary pg_idx.
    //SB_NEGATIVE_SHIFT_VALUE = -10
    if(pg_idx==0){
        sf_ins.dw2.all = ((0xFF<<16) & (current_shift_value[interleave][0]+(SB_NEGATIVE_SHIFT_VALUE<<16)));
    }
    else if(pg_idx==1){
        sf_ins.dw2.all =   ((0xFF) & (current_shift_value[interleave][0]+SB_NEGATIVE_SHIFT_VALUE))
                          |((0xFF<<8) & (current_shift_value[interleave][0]+(SB_NEGATIVE_SHIFT_VALUE<<8)))
                          |((0xFF<<24) & (current_shift_value[interleave][0]+(SB_NEGATIVE_SHIFT_VALUE<<24)));
    }
    else{
        sf_ins.dw2.all =   ((0xFF) & (current_shift_value[interleave][1]+SB_NEGATIVE_SHIFT_VALUE))
                          |((0xFF<<8) & (current_shift_value[interleave][1]+(SB_NEGATIVE_SHIFT_VALUE<<8)))
                          |((0xFF<<16) & (current_shift_value[interleave][1]+(SB_NEGATIVE_SHIFT_VALUE<<16)));
    }

    ficu_fill_finst(sf_ins);
    finst_cnt++;

    //Soft bit read (c2h cmd)
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->fda_col= pda2column(pda);
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);
    fda_dtag->pda = pda;

    // Configure dtag
    fda_dtag->dtag.all = ncl_cmd->user_tag_list[0].all;

    // Configure instruction
    rd_ins = read_fins_templ;

    rd_ins.dw0.b.xlc_en = ROW_WL_ADDR;
    rd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_SBN_READ(pg_idx);
    rd_ins.dw0.b.pg_idx = pg_idx;
    rd_ins.dw0.b.fins = 0;
    rd_ins.dw0.b.lins = 0;
    rd_ins.dw0.b.mlun_en = 0;   //close interleaving operation
    rd_ins.dw0.b.raw_addr_en = 0;
    rd_ins.dw0.b.fins_fuse = 1;
    rd_ins.dw0.b.mp_num = 0;
    rd_ins.dw1.b.fcmd_id = fcmd_id;
    rd_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
    rd_ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
    rd_ins.dw2.b.btn_op_type = ncl_cmd->op_type;
    rd_ins.dw2.b.ard_schem_sel = 1;     //FW_ARD
    rd_ins.dw2.b.du_num = 0;

    //Add FINST settings 230727
    rd_ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
    rd_ins.dw2.b.meta_strip = !!(ncl_cmd->flags & NCL_CMD_META_DISCARD);

    if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
    {
        rd_ins.dw2.b.scrc_en = 0;
    }

#if defined(HMETA_SIZE)
    if (ncl_cmd->flags & NCL_CMD_DIS_HCRC_FLAG)
    {
        rd_ins.dw2.b.scrc_en = 0;
    }
#endif
	ncl_cmd_chk_crc_buffer(rd_ins, ncl_cmd);
    ficu_fill_finst(rd_ins);
    finst_cnt++;

    //reset feature (HD read)
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);

    if (nand_support_aipr() && (pda2plane(pda) == 1)) {
    	// When AIPR enabled, for plane 1 ,89h/8Ah/8Bh changed to 85h/86h/87h
    	fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] - 4) << 8);
    }
    else if(nand_support_aipr() && (pda2plane(pda) == 2)){
        fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] +0x20) << 8);
    }
    else if(nand_support_aipr() && (pda2plane(pda) == 3)){
        fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] +0x20- 4) << 8);
    }
    else {
        fda_dtag->fda_col = lun_id | (Retry_feature_addr[pg_idx] << 8);
    }

    sf_ins = set_feature_tmpl;
    sf_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
    sf_ins.dw0.b.fins = 0;
    sf_ins.dw0.b.lins = 0;
    sf_ins.dw0.b.fins_fuse = 1;
    sf_ins.dw1.b.fcmd_id = fcmd_id;
    sf_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
    sf_ins.dw2.all=0; //reset shift value

    ficu_fill_finst(sf_ins);
    finst_cnt++;

    //reset feature Positive
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);

    if(pg_idx==2){
        fda_dtag->fda_col = lun_id | (0x95<< 8);
    }
    else{
        fda_dtag->fda_col = lun_id | (0x94<< 8);
    }

    sf_ins = set_feature_tmpl;
    sf_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
    sf_ins.dw0.b.fins = 0;
    sf_ins.dw0.b.lins = 0;
    sf_ins.dw0.b.fins_fuse = 1;
    sf_ins.dw1.b.fcmd_id = fcmd_id;
    sf_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
    sf_ins.dw2.all=0; //reset shift value

    ficu_fill_finst(sf_ins);
    finst_cnt++;

    //reset feature Negative
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);

    if(pg_idx==2){
        fda_dtag->fda_col = lun_id | (0x93<< 8);
    }
    else{
        fda_dtag->fda_col = lun_id | (0x92<< 8);
    }

    sf_ins = set_feature_tmpl;
    sf_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
    sf_ins.dw0.b.fins = 0;
    sf_ins.dw0.b.lins = 1;      //Last FINST
    sf_ins.dw0.b.fins_fuse = 0;     //last fused operation
    sf_ins.dw1.b.fcmd_id = fcmd_id;
    sf_ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
    sf_ins.dw2.all=0; //reset shift value

    ficu_fill_finst(sf_ins);
    finst_cnt++;

    ncl_cmd->dtag_ptr = du_dtag_ptr; //last fcmd_id
    ficu_submit_finstr(finst_cnt);
}

/*
Copyback reference
fda header 203069E0: 10000000 00007150.dtag 00000000.INS 203017E0: 14192443 003E001F 00196204
fda header 203069F0: 10000000 00007158.dtag 00000000.INS 203017F0: 14192445 003F001F 00196204
fda header 20306A00: 10000000 00006FE0.dtag 00000000.INS 20301800: 00290A43 00400020 00186224
fda header 20306A10: 10000000 00006FE8.dtag 00000000.INS 20301810: 00290A45 00410020 00186224
*/
static fast_code void ncl_cmd_idm0(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i;
	u32 pda_idx = 0;
	u32 pg_idx;// Low/middle/upper index
	u32 mp_cnt;
	u32 fins_cnt = 0;
	bool tlc;
	u32 first_idx, last_idx;
	u32 first_mode, last_mode;

	first_idx = 0;
	last_idx = ncl_cmd->addr_param.cb_param.list_len;
	first_mode = 0;
	last_mode = 1;
	sys_assert(ncl_cmd->addr_param.cb_param.addr->width * nand_bits_per_cell() == ncl_cmd->addr_param.cb_param.list_len);
	for (pg_idx = 0; pg_idx < nand_bits_per_cell(); pg_idx++) {
		for (tlc = 0; tlc < 2; tlc++) {
			i = 0;
			while (i < ncl_cmd->addr_param.cb_param.addr->width) {
				mp_cnt = get_mp_cnt(ncl_cmd->addr_param.cb_param.addr->slc_list_src + i, ncl_cmd->addr_param.cb_param.list_len - i);
				du_dtag_ptr = ncl_acquire_dtag_ptr();

				// Configure fda list
				fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
				if (tlc) {
					fda_dtag->pda = ncl_cmd->addr_param.cb_param.addr->tlc_list_dst[pda_idx+i];
				} else {
					fda_dtag->pda = ncl_cmd->addr_param.cb_param.addr->slc_list_src[pda_idx+i];
				}

				// Configure instruction
				if (tlc) {
					ins = prog_fins_templ;
					ins.dw0.b.xlc_en = ROW_WL_ADDR;
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_CB_PROG(pg_idx);
				} else {
					ins = read_fins_templ;
					ins.dw0.b.xlc_en = 0;
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_CB_READ_CMD;
				}
				ins.dw0.b.mp_row_offset_en = 1;
				ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
				ins.dw0.b.pg_idx = pg_idx;
				if (mp_cnt > 1) {
					ins.dw0.b.mp_num = mp_cnt - 1;
				}
				if (((pda_idx + i) == first_idx) && (first_mode == tlc)) {
					ins.dw0.b.fins = 1;
					fcmd_id = du_dtag_ptr;
					ncl_cmd_save(fcmd_id, ncl_cmd);
				} else {
					ins.dw0.b.fins = 0;
				}
				i += mp_cnt;
				if (((pda_idx+ i) == last_idx) && (last_mode == tlc)) {
					ins.dw0.b.lins = 1;
					ncl_cmd->dtag_ptr = du_dtag_ptr;
				} else {
					ins.dw0.b.lins = 0;
				}
				ins.dw1.b.fcmd_id = fcmd_id;
				ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
				ins.dw1.b.xfcnt_sel = FINST_XFER_ZERO;
				ins.dw2.b.du_num = 0;
				ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
#if HAVE_HW_CPDA
				convert_pda2cpda(&ins);
#endif
				ficu_fill_finst(ins);
				fins_cnt++;
			}
		}
		pda_idx += ncl_cmd->addr_param.cb_param.addr->width;
	}
	ficu_submit_finstr(fins_cnt);
}

static fast_code void ncl_cmd_idm2(struct ncl_cmd_t *ncl_cmd)
{
	u32 fcmd_id = 0;
	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 i;
	u32 pda_idx = 0;
	u32 pg_idx;// Low/middle/upper index
	u32 mp_cnt;
	u32 fins_cnt = 0;
	bool tlc;
	u32 first_idx, last_idx;
	u32 last_mode;

	first_idx = 0;
	last_idx = ncl_cmd->addr_param.cb_param.list_len;
	last_mode = 1;
	sys_assert(ncl_cmd->addr_param.cb_param.addr->width * nand_bits_per_cell() == ncl_cmd->addr_param.cb_param.list_len);
	for (pg_idx = 0; pg_idx < nand_bits_per_cell(); pg_idx++) {
		// All read command stage parallel to save tR time
		for (i = 0; i < ncl_cmd->addr_param.cb_param.addr->width;) {
			mp_cnt = get_mp_cnt(ncl_cmd->addr_param.cb_param.addr->slc_list_src + i, ncl_cmd->addr_param.cb_param.list_len - i);
			du_dtag_ptr = ncl_acquire_dtag_ptr();
			// Configure fda list
			fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
			fda_dtag->pda = ncl_cmd->addr_param.cb_param.addr->slc_list_src[pda_idx+i];
			ins = refresh_fins_templ;
			ins.dw0.b.xlc_en = 0;
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD;
			ins.dw0.b.mp_row_offset_en = 1;
			if (mp_cnt > 1) {
				ins.dw0.b.mp_num = mp_cnt - 1;
			}
			if ((pda_idx + i) == first_idx) {
				ins.dw0.b.fins = 1;
				fcmd_id = du_dtag_ptr;
				ncl_cmd_save(fcmd_id, ncl_cmd);
			} else {
				ins.dw0.b.fins = 0;
			}
			i += mp_cnt;
			ins.dw1.b.fcmd_id = fcmd_id;
			ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
			ins.dw2.b.du_num = 0;
#if HAVE_HW_CPDA
			convert_pda2cpda(&ins);
#endif
			ficu_fill_finst(ins);
			fins_cnt++;
		}

		// Random data out and program back into nand
		for (tlc = 0; tlc < 2; tlc++) {
			i = 0;
			while (i < ncl_cmd->addr_param.cb_param.addr->width) {
				mp_cnt = get_mp_cnt(ncl_cmd->addr_param.cb_param.addr->slc_list_src + i, ncl_cmd->addr_param.cb_param.list_len - i);
				du_dtag_ptr = ncl_acquire_dtag_ptr();

				// Configure fda list
				fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
				if (tlc) {
					fda_dtag->pda = ncl_cmd->addr_param.cb_param.addr->tlc_list_dst[pda_idx+i];
				} else {
					fda_dtag->pda = ncl_cmd->addr_param.cb_param.addr->slc_list_src[pda_idx+i];
				}
				u32 ch = pda2ch(fda_dtag->pda);
				fda_dtag->dtag_ptr = ptr2busmem(ncl_cmd->user_tag_list + DU_CNT_PER_PAGE * nand_plane_num() * ch);

				// Configure instruction
				if (tlc) {
					ins = prog_fins_templ;
					ins.dw0.b.xlc_en = ROW_WL_ADDR;
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_PROG(pg_idx);
					ins.dw0.b.pg_idx = pg_idx;
				} else {
					ins = read_fins_templ;
					ins.dw0.b.xlc_en = 0;
					ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
					ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
					ins.dw0.b.fins_fuse = 1;
					ins.dw0.b.pg_idx = 0;
				}
				ins.dw0.b.mp_row_offset_en = 1;
				ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
				if (mp_cnt > 1) {
					ins.dw0.b.mp_num = mp_cnt - 1;
				}
				ins.dw0.b.fins = 0;
				i += mp_cnt;
				if (((pda_idx+ i) == last_idx) && (last_mode == tlc)) {
					ins.dw0.b.lins = 1;
					ncl_cmd->dtag_ptr = du_dtag_ptr;
				} else {
					ins.dw0.b.lins = 0;
				}
				ins.dw1.b.fcmd_id = fcmd_id;
				ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
				ins.dw2.b.du_num = DU_CNT_PER_PAGE - 1;
				ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
				ins.dw2.b.btn_op_type = ncl_cmd->op_type;
#if HAVE_HW_CPDA
				convert_pda2cpda(&ins);
#endif
				ficu_fill_finst(ins);
				fins_cnt++;
			}
		}
		pda_idx += ncl_cmd->addr_param.cb_param.addr->width;
	}
	ficu_submit_finstr(fins_cnt);
}

/*!
 * @brief Rapid path for single DU read to speed up random 4KB IOPS
 * May be removed if ncl_cmd_submit is good & fast enough
 *
 * @param ncl_cmd	NCL command pointer
 *
 * @return	not used
 */
slow_code void ncl_cmd_rapid_single_du_read(struct ncl_cmd_t *ncl_cmd)
{
	fcmd_rw_outstanding_cnt++;
	if (!(ncl_cmd->flags & NCL_CMD_RAPID_PATH)) {
		// ncl_cmd_trace(LOG_ERR, 0, "Pls set NCL_CMD_RAPID_PATH\n");
		ncl_cmd_trace(LOG_WARNING, 0x5f10, "Pls set NCL_CMD_RAPID_PATH");

		ncl_cmd->flags |= NCL_CMD_RAPID_PATH;
	}
	if (unlikely(ncl_cmd_wait)) {
		sys_assert(ncl_cmd->completion != NULL);// Don't submit sync cmd at this time
		QSIMPLEQ_INSERT_TAIL(&ncl_cmds_wait_queue, ncl_cmd, entry);
		return;
	}

	sys_assert(!(ncl_cmd->flags & NCL_CMD_COMPLETED_FLAG));
#if HAVE_CACHE_READ
	fcmd_outstanding_cnt++;
	SET_NCL_BUSY();
	ncl_cmd_split_task(ncl_cmd);
	return;
#endif

	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	fcmd_outstanding_cnt++;
	SET_NCL_BUSY();
	sys_assert(eccu_cmf_is_rom() == false);// This path only support 4KB DU format

	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	// Configure fda list
	fda_dtag->pda = ncl_cmd->addr_param.rapid_du_param.pda;
	// Configure dtag
	fda_dtag->dtag.all = ncl_cmd->du_user_tag_list.all;

#if OPEN_ROW == 1
    ncl_cmd_clear_openrow_table();
#endif
	// Configure instruction
	ins = read_fins_templ;
	//if (ncl_cmd->addr_param.common_param.info_list[i].pb_type == NAL_PB_TYPE_XLC) {
	if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
		u32 pg_idx = pda2pg_idx(ncl_cmd->addr_param.rapid_du_param.pda);
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_idx);
#if HAVE_CACHE_READ
		if (ncl_cmd->op_code == NCL_CMD_OP_READ_ARD) {
			ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
		} else {
			ins.dw2.b.ard_schem_sel = ARD_DISABLE;
		}
#else
	    ins.dw2.b.ard_schem_sel = ARD_DISABLE;
#endif
		ins.dw0.b.pg_idx = pg_idx;
	} else {
		ins.dw0.b.pg_idx = ncl_cmd->addr_param.rapid_du_param.info.xlc.slc_idx;
        ins.dw2.b.ard_schem_sel = ARD_DISABLE;  //tony 20201112
        //ins.dw2.b.ard_schem_sel = ARD_DISABLE;  //tony 20201110
	}
	ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
	ncl_cmd_save(du_dtag_ptr, ncl_cmd);
	ins.dw0.b.fins = 1;
	ins.dw0.b.lins = 1;
	ncl_cmd->dtag_ptr = du_dtag_ptr;
	ins.dw1.b.fcmd_id = du_dtag_ptr;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw2.b.btn_op_type = ncl_cmd->op_type;
	if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
	    ins.dw2.b.scrc_en = 0;
	if (ncl_cmd->flags & NCL_CMD_META_DISCARD) {
		ins.dw2.b.meta_strip = 1;
	}
#if !STABLE_REGRESSION
	if (ncl_cmd->flags & NCL_CMD_HIGH_PRIOR_FLAG) {
		ins.dw0.b.susp_en = 1;
	}
#endif
	ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
#if HAVE_HW_CPDA
	convert_pda2cpda(&ins);
#endif
	ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
	ficu_fill_finst(ins);
	// Final submit instruction
	ficu_submit_finstr(1);

	if (ncl_cmd->completion == NULL) {
		while(NCL_CMD_PENDING(ncl_cmd)) {
			ficu_done_wait();
		}
	}
}

#if NCL_FW_RETRY
slow_code void ncl_cmd_read_retry(struct ncl_cmd_t *ncl_cmd)
{
    #ifdef History_read
    hist_tab_offset_t *hist_tab_ptr;
    #endif
    u8 lun_id, die_idx, interleave;
	u32 fins_cnt = 0, shift_value = 0;

    fcmd_rw_outstanding_cnt++;
    #if 0
	if (!(ncl_cmd->flags & NCL_CMD_RAPID_PATH)) {
		ncl_cmd_trace(LOG_WARNING, 0x8eb1, "Pls set NCL_CMD_RAPID_PATH");

		ncl_cmd->flags |= NCL_CMD_RAPID_PATH;
	}
    #endif
	sys_assert(!(ncl_cmd->flags & NCL_CMD_COMPLETED_FLAG));

	u32 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;

    memset(&ins, 0, sizeof(struct finstr_format));  //test

    #ifdef NCL_FW_RETRY_BY_SUBMIT
    if((ncl_cmd->op_code != NCL_CMD_FW_RETRY_SET_FEATURE) && (ncl_cmd->op_code != NCL_CMD_FW_RETRY_READ))
    #endif
    {
	    fcmd_outstanding_cnt++;
        SET_NCL_BUSY();
    }
    //sys_assert(eccu_cmf_is_rom() == false);// This path only support 4KB DU format

	du_dtag_ptr = ncl_acquire_dtag_ptr();
    ncl_cmd_save(du_dtag_ptr, ncl_cmd);

	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	// Configure fda list
	fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[0];
    lun_id = pda2lun(ncl_cmd->addr_param.common_param.pda_list[0]);
    die_idx = pda2die(ncl_cmd->addr_param.common_param.pda_list[0]);
    interleave = (ncl_cmd->addr_param.common_param.pda_list[0] >> nand_info.pda_interleave_shift) & (nand_info.interleave - 1); //Interleave 0~127
	fda_dtag->ch_id = pda2ch(ncl_cmd->addr_param.common_param.pda_list[0]);
	fda_dtag->dev_id = pda2ce(ncl_cmd->addr_param.common_param.pda_list[0]);
    u32 pg_idx = pda2pg_idx(ncl_cmd->addr_param.common_param.pda_list[0]);

	if (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG)  
    {
        pg_idx = 3;
    }

    if ((ncl_cmd->retry_step == retry_set_shift_value) || (ncl_cmd->retry_step == last_1bit_vth_step)
         || (ncl_cmd->retry_step == last_1bit_step) || (ncl_cmd->retry_step == last_2bit_step)
         || (ncl_cmd->retry_step == abort_retry_step)
        #ifdef History_read
         || (ncl_cmd->retry_step == retry_history_step)
        #endif
        #if SPOR_RETRY
        || (ncl_cmd->retry_step == spor_retry_step)
        #endif
    )
    {
    	if (nand_support_aipr() && (pda2plane(ncl_cmd->addr_param.common_param.pda_list[0]) == 1)) {
    		// When AIPR enabled, for plane 1 ,89h/8Ah/8Bh changed to 85h/86h/87h
    		fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] - 4) << 8);
    	}
        else if(nand_support_aipr() && (pda2plane(ncl_cmd->addr_param.common_param.pda_list[0]) == 2)){
            fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] +0x20) << 8);
        }
        else if(nand_support_aipr() && (pda2plane(ncl_cmd->addr_param.common_param.pda_list[0]) == 3)){
             fda_dtag->fda_col = lun_id | ((Retry_feature_addr[pg_idx] +0x20- 4) << 8);
        }
        else {
    		fda_dtag->fda_col = lun_id | (Retry_feature_addr[pg_idx] << 8);
    	}

        if (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG) {
    		fda_dtag->row = pda2row(ncl_cmd->addr_param.common_param.pda_list[0], NAL_PB_TYPE_SLC);
    	} else {
    		sys_assert(ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG);
    		fda_dtag->row = pda2row(ncl_cmd->addr_param.common_param.pda_list[0], NAL_PB_TYPE_XLC);
    	}

    	// Set feature to change Vref
    	set_feature_tmpl.dw1.b.fcmd_id = du_dtag_ptr;
    	set_feature_tmpl.dw1.b.du_dtag_ptr = du_dtag_ptr;
    	ins = set_feature_tmpl;
        ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
    	ins.dw0.b.fins = 1;
        ins.dw0.b.lins = 1;

        if (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG)        // SLC page
        {
            if (ncl_cmd->retry_step == last_1bit_vth_step || ncl_cmd->retry_step == last_2bit_step)
            {
                ins.dw2.all = 0;        //reset retry value
            }
            else
            {
        	    ins.dw2.all = tsb_slc_8b_rr_offset[ncl_cmd->retry_cnt];
            }
        }
        else
        {
            if(ncl_cmd->retry_step == last_1bit_vth_step)
            {
                //current_shift_value is calculated by vth_tracking
                if ((pg_idx == 0) || (pg_idx == 1))
                {
                    shift_value = current_shift_value[interleave][0];
                }
                else if (pg_idx == 2)
                {
                    shift_value = current_shift_value[interleave][1];
                }
            }
#ifdef History_read
            else if(ncl_cmd->retry_step == retry_history_step)
            {
                //current_shift_value obtained from history table
                u16 blk_index = pda2blk(ncl_cmd->addr_param.common_param.pda_list[0]);
                sys_assert(blk_index < shr_nand_info.geo.nr_blocks);
                hist_tab_ptr = hist_tab_ptr_st + blk_index;     //"hist_tab_ptr" shift by SPB index
                //ncl_cmd_trace(LOG_ALW, 0, "blk_addr address :0x%x",hist_tab_ptr);
                u8 shift_index = hist_tab_ptr->shift_index[die_idx][pg_idx/Group_num][pg_idx%3];

                if ((pg_idx == 0)||(pg_idx == 1)) 
                {
                    shift_value = tsb_tlc_low_mid_89_rr_offset_512Gb[shift_index];
                }
                else if (pg_idx == 2) 
                {
                    shift_value = tsb_tlc_upr_8a_rr_offset_512Gb[shift_index];
                }

            }
#endif
#if 0
            else if(ncl_cmd->retry_step == spor_retry_step)
            {
                if ((pg_idx == 0)||(pg_idx == 1))
                {
                    shift_value = tsb_tlc_low_mid_89_rr_offset_512Gb[shift_index];
                }
                else if (pg_idx == 2)
                {
                    shift_value = tsb_tlc_upr_8a_rr_offset_512Gb[shift_index];
                }

            }
#endif
            else if(ncl_cmd->retry_step == abort_retry_step)
            {
                shift_value = 0;
            }
            else
            {
                #if SPOR_RETRY
                if(ncl_cmd->retry_step == spor_retry_step && ncl_cmd->retry_cnt > 0)
                {
                    ncl_cmd->retry_cnt += (OPEN_BLK_RR_VALUE_IDX - 1);
                }
                #endif
                
                if (pg_idx == 0) 
                {
                    shift_value = tsb_tlc_low_mid_89_rr_offset_512Gb[ncl_cmd->retry_cnt];
                    shift_value &= 0x00FF0000;
                    shift_value |= (current_shift_value[interleave][0] & 0xFF00FFFF);
                    current_shift_value[interleave][0] = shift_value;
                }
                else if (pg_idx == 1)    // middle page
                {
                    shift_value = tsb_tlc_low_mid_89_rr_offset_512Gb[ncl_cmd->retry_cnt];
                    shift_value &= 0xFF00FFFF;
                    shift_value |= (current_shift_value[interleave][0] & 0x00FF0000);
                    current_shift_value[interleave][0] = shift_value;
                }
                else    // upper page
                {
                	shift_value = current_shift_value[interleave][1] = tsb_tlc_upr_8a_rr_offset_512Gb[ncl_cmd->retry_cnt];
        	    }

                #if SPOR_RETRY
                if(ncl_cmd->retry_step == spor_retry_step && ncl_cmd->retry_cnt > 0)
                {
                    ncl_cmd->retry_cnt -= (OPEN_BLK_RR_VALUE_IDX - 1);
                }
                #endif
            }
            ins.dw2.all = shift_value;
        }
        if(!plp_trigger){
            ncl_cmd_trace(LOG_INFO, 0x599c, "ncl_cmd_read_retry, retry_cnt: %d, value: 0x%x, pda:0x%x, re_step:%d", ncl_cmd->retry_cnt, ins.dw2.all, ncl_cmd->addr_param.common_param.pda_list[0], ncl_cmd->retry_step);  //test
        }
    }
    else
    {
    	#if OPEN_ROW == 1
    	ncl_cmd_clear_openrow_table();
		#endif
        fda_dtag->dtag.all = ncl_cmd->user_tag_list[0].all;
    	fda_dtag->pda = ncl_cmd->addr_param.common_param.pda_list[0];
    	// Configure instruction
    	ins = read_fins_templ;

    	//ncl_cmd_save(du_dtag_ptr, ncl_cmd);
    	if (ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
    		ins.dw0.b.xlc_en = ROW_WL_ADDR;
            ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ_RETRY(pg_idx);

            //#ifdef EH_ENABLE_2BIT_RETRY
            #if 0
            if((ncl_cmd->flags & NCL_CMD_RCED_FLAG) && (ncl_cmd->retry_step == last_2bit_read))  //turn on 2bit ARD when the ncl_cmd is last 1bit retry step & raid correct read
            {
                ins.dw2.b.ard_schem_sel = ARD_TMPLT_XLC(pg_idx);
                ncl_cmd_trace(LOG_ALW, 0xf926, "Enable ARD, 2Bit Retry!!!");  //test
            }
            else
            #endif
            {
                ins.dw2.b.ard_schem_sel = ARD_DISABLE;
            }
            ins.dw0.b.pg_idx = pg_idx;
    	} else {
    		ins.dw0.b.pg_idx = 0;

            if(ncl_cmd->retry_step == last_1bit_vth_read)
            {
                ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_LNA_READ;
            }
            else
            {
                ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_REFRESH_READ_SLC;
            }

    		ins.dw2.b.ard_schem_sel = ARD_DISABLE;
    	}
    	ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
        ins.dw0.b.fins = 1;
    	ins.dw0.b.lins = 1;
    	ins.dw1.b.fcmd_id = du_dtag_ptr;
    	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
    	ins.dw2.b.btn_op_type = ncl_cmd->op_type;

        if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
            ins.dw2.b.scrc_en = 0;

#if defined(HMETA_SIZE)
        if (ncl_cmd->flags & NCL_CMD_DIS_HCRC_FLAG) {
            ins.dw2.b.scrc_en = 0;
        }
#endif

    	if (ncl_cmd->flags & NCL_CMD_META_DISCARD) {
    		ins.dw2.b.meta_strip = 1;
    	}

    	ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
    }
    ncl_cmd->dtag_ptr = du_dtag_ptr;
	ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
    ficu_fill_finst(ins);
    fins_cnt++;
	ficu_submit_finstr(1);

	if (ncl_cmd->completion == NULL) {
		while(NCL_CMD_PENDING(ncl_cmd)) {
			ficu_done_wait();
		}
	}
}
#endif

slow_code void ncl_cmd_ssr_read(struct ncl_cmd_t *ncl_cmd)
{
    u32 fcmd_id = 0;
    u32 du_dtag_ptr;
    struct finstr_format ins;
    struct finstr_format ins1;
    struct fda_dtag_format *fda_dtag;
    pda_t pda;
    pda = ncl_cmd->addr_param.common_param.pda_list[0];

    //Set feature for SSR (0xC9)
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fcmd_id = du_dtag_ptr;
    fda_dtag = ficu_get_addr_dtag_ptr(fcmd_id);
    fda_dtag->hdr = 0;
    fda_dtag->fda_col = (u32)ncl_cmd->read_level;
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);
    fda_dtag->row = pda2row(pda, NAL_PB_TYPE_XLC);

    //extern struct finstr_format set_feature_tmpl;
    ins1 = set_feature_tmpl;
    ins1.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SingleStatusRead;
    ins1.dw0.b.poll_dis = 1;
    ins1.dw0.b.fins = 1;
    ins1.dw0.b.lins = 0;
    ins1.dw1.b.xfcnt_sel = 0;
    ncl_cmd_save(fcmd_id, ncl_cmd);
    ins1.dw1.b.fcmd_id = fcmd_id;
    ins1.dw1.b.du_dtag_ptr = du_dtag_ptr;
    ins1.dw2.all=0;
    ficu_fill_finst(ins1);

    sys_assert(ncl_cmd->addr_param.common_param.list_len == 1);

    //SSR (Normal read without page prefix)
    du_dtag_ptr = ncl_acquire_dtag_ptr();
    fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);

    fda_dtag->hdr = 0;
    fda_dtag->fda_col= pda2column(pda);
    fda_dtag->ch_id = pda2ch(pda);
    fda_dtag->dev_id = pda2ce(pda);
    fda_dtag->pda=pda;

    // Configure dtag
    fda_dtag->dtag.all = ncl_cmd->user_tag_list[0].all;

    // Configure instruction
    ins = read_fins_templ;

    ins.dw0.b.xlc_en = ROW_WL_ADDR;
    ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SingleStatusRead_read;
    ins.dw0.b.pg_idx = pda2pg_idx(pda);
    ins.dw0.b.mp_num = 0;
    ins.dw2.b.du_num = 0;
    ins.dw0.b.fins = 0;
    ins.dw0.b.lins = 1;
    ins.dw0.b.poll_dis = 0;
    ins.dw0.b.raw_addr_en=0;
    ins.dw1.b.fcmd_id = fcmd_id;
    ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
    ins.dw2.b.du_fmt_sel = ncl_cmd->du_format_no;
    ins.dw2.b.btn_op_type = ncl_cmd->op_type;
    ins.dw2.b.ard_schem_sel = ARD_DISABLE;

    //Add FINST settings 230727
    ins.dw0.b.nvcmd_tag_ext = !!(ncl_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
    if (ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ncl_cmd->op_type == NCL_CMD_FW_TABLE_READ_DA_DTAG)
    {
        ins.dw2.b.scrc_en = 0;
    }
	ncl_cmd_chk_crc_buffer(ins, ncl_cmd);
    ficu_fill_finst(ins);
    ncl_cmd->dtag_ptr = du_dtag_ptr;
    ficu_submit_finstr(2);
}

/*!
 * @brief NCL command submit main function
 *
 * @param ncl_cmd	NCL command pointer
 *
 * @return	not used
 */
fast_code void ncl_cmd_submit(struct ncl_cmd_t *ncl_cmd)
{
	if (unlikely(ncl_cmd_wait)) {
		sys_assert(ncl_cmd->completion != NULL);// Don't submit sync cmd at this time
		QSIMPLEQ_INSERT_TAIL(&ncl_cmds_wait_queue, ncl_cmd, entry);
		return;
	}
    if (FW_ARD_EN && (ncl_cmd->op_code == NCL_CMD_OP_READ || ncl_cmd->op_code == NCL_CMD_OP_READ_STREAMING_FAST) && FW_ARD_CPU_ID == CPU_ID) {
		sys_assert(ncl_cmd->completion != NULL);// Don't submit sync cmd at this time
		QSIMPLEQ_INSERT_TAIL(&ncl_cmds_wait_queue, ncl_cmd, entry);
		return;
	}
#if !RAID_SUPPORT
    if(ncl_cmd->flags & NCL_CMD_RAID_FLAG){
        ncl_cmd_trace(LOG_ALW, 0x636a, "panic ncl_cmd 0x%x flags 0x%x opcode 0x%x", ncl_cmd, ncl_cmd->flags, ncl_cmd->op_code);
    }
#endif

    #if 0//NCL_FW_RETRY
    if (is_target_die_under_retry(ncl_cmd))
    {
		sys_assert(ncl_cmd->completion != NULL);// Don't submit sync cmd at this time
		QSIMPLEQ_INSERT_TAIL(&ncl_cmds_wait_queue, ncl_cmd, entry);
		return;
    }
    #endif

#if !PERF_NCL
	sys_assert(!(ncl_cmd->flags & NCL_CMD_COMPLETED_FLAG));
#endif
	fcmd_outstanding_cnt++;
	SET_NCL_BUSY();

#if HAVE_CACHE_READ
	// Current cache read design requires each ncl cmd within die
	bool same_die = true;
	u32 lun;
	u32 i;
	lun = pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[0]);
	for (i = 1; i < ncl_cmd->addr_param.common_param.list_len; i++) {
		if (pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[i]) != lun) {
			same_die = false;
			break;
		}
	}
	if (same_die && ((ncl_cmd->op_code == NCL_CMD_OP_READ) || (ncl_cmd->op_code == NCL_CMD_PATROL_READ)) && (ard_fcmd_cnt == 0)) {
		ncl_cmd_split_task(ncl_cmd);
		return;
	} else {
		ncl_tasks_clear();
	}
#endif

    sys_assert(ncl_cmd_in == false);
    ncl_cmd_in = true;
	switch(ncl_cmd->op_code) {
#ifdef REFR_RD
	case NCL_CMD_READ_REFRESH:
		ncl_cmd_read_refresh(ncl_cmd);
		break;
#endif
	case NCL_CMD_OP_READ:
    case NCL_CMD_PATROL_READ:
		fcmd_rw_outstanding_cnt++;
    case NCL_CMD_P2L_SCAN_PG_AUX:
		ncl_cmd_dus_read(ncl_cmd);
		break;
	case NCL_CMD_OP_READ_FW_ARD:
        sys_assert(fw_ard_cnt == 0);
		ncl_cmd_dus_read_ard(ncl_cmd);
		break;
    case NCL_CMD_OP_READ_STREAMING_FAST:
		ncl_cmd_dus_streaming_fastread(ncl_cmd);
		break;
	case NCL_CMD_READ_RAW:
		fcmd_rw_outstanding_cnt++;
		ncl_cmd_du_rawread(ncl_cmd);
		break;
    case NCL_CMD_OP_READ_RAW:
        fcmd_rw_outstanding_cnt++;
        ncl_cmd_read_data_training(ncl_cmd);
		break;
	case NCL_CMD_OP_ERASE:
		ncl_cmd_blocks_erase(ncl_cmd);
		break;
	case NCL_CMD_OP_WRITE:
		fcmd_rw_outstanding_cnt++;
#if !defined(PROGRAMMER)
		/*TODU
		ftl_stat.total_write_sector_cnt += ncl_cmd->addr_param.common_param.list_len << (DU_CNT_SHIFT + 3)
		du = 4k, include 8 sector */
#endif
		ncl_cmd_pages_write(ncl_cmd);
		break;
	case NCL_CMD_OP_CB_MODE0:
		ncl_cmd_idm0(ncl_cmd);
		break;
	case NCL_CMD_OP_CB_MODE2:
		ncl_cmd_idm2(ncl_cmd);
		break;
	case NCL_CMD_READ_DEFECT:
		ncl_cmd_read_defect_mark(ncl_cmd);
		break;
	case NCL_CMD_SET_GET_FEATURE:
		ncl_cmd_set_get_feature(ncl_cmd);
		break;
#if HAVE_CACHE_READ
	case NCL_CMD_OP_READ_ARD:
		ncl_cmd_dus_ard(ncl_cmd);
		break;
#endif
#if NCL_HAVE_ERD
	case NCL_CMD_OP_READ_ERD:
		ncl_cmd_sp_erd(ncl_cmd);
		break;
#endif
#if (SPOR_FLOW == mENABLE)
    case NCL_CMD_SPOR_SCAN_FIRST_PG:
    case NCL_CMD_SPOR_SCAN_LAST_PG:
    case NCL_CMD_SPOR_SCAN_PG_AUX:
    case NCL_CMD_SPOR_P2L_READ:
    case NCL_CMD_SPOR_P2L_READ_POS:
    case NCL_CMD_SPOR_BLIST_READ:
    case NCL_CMD_SPOR_SCAN_BLANK_POS:
		ncl_cmd_dus_read(ncl_cmd);
		break;
#endif
#ifdef NCL_FW_RETRY_BY_SUBMIT
    case NCL_CMD_FW_RETRY_SET_FEATURE:
    case NCL_CMD_FW_RETRY_READ:
         ncl_cmd_read_retry(ncl_cmd);
        break;
#endif
    case NCL_CMD_OP_SSR_READ:
        fcmd_rw_outstanding_cnt++;
        ncl_cmd_ssr_read(ncl_cmd);
    break;
	default:
		sys_assert(0);
		break;
	}
    ncl_cmd_in = false;
	if (ncl_cmd->completion == NULL) {
		while(NCL_CMD_PENDING(ncl_cmd)) {
			ficu_done_wait();
		}

        if ((ncl_cmd->op_code == NCL_CMD_OP_READ) && (ncl_cmd->status != 0) && !((ncl_cmd->flags & NCL_CMD_NO_READ_RETRY_FLAG)))
        {
            System_Blk_read_retry(ncl_cmd);
        }
	}

}

/*!
 * @brief NCL command initialization before any NCL cmd handling
 *
 * @return	not used
 */
init_code void ncl_cmd_init(void)
{
	u32 i;

	for (i = 0; i < START_FCMD_ID; i++) {
		free_dtag_list[i] = DTAG_PTR_INVALID;
	}
#if defined(DUAL_BE)
	free_dtag_ptr = DTAG_PTR_STR_SQ;
	free_dtag_tail = DTAG_PTR_END_SQ - 1;

	for (i = START_FCMD_ID; i < DTAG_PTR_END_SQ; i++)
		free_dtag_list[i] = i + 1;
	free_dtag_list[DTAG_PTR_END_SQ - 1] = DTAG_PTR_INVALID;

	ncl_busy[NRM_SQ_IDX] = false;
#else
	free_dtag_ptr = START_FCMD_ID;
	free_dtag_tail = DTAG_PTR_COUNT - 1;

	for (i = START_FCMD_ID; i < DTAG_PTR_COUNT; i++)
		free_dtag_list[i] = i + 1;

#endif
	refresh_fins_templ.dw0.b.mp_num = nand_plane_num() - 1;
	lun_ep_bndry_check = (DU_CNT_PER_PAGE * nand_plane_num()) - 1;
	if (nand_support_aipr()) {
		lun_read_bndry_check = DU_CNT_PER_PAGE - 1;
	} else {
		lun_read_bndry_check = (DU_CNT_PER_PAGE * nand_plane_num()) - 1;
	}

	QSIMPLEQ_INIT(&ncl_cmds_wait_queue);

#if NCL_HAVE_ERD && (CPU_ID == CPU_BE || CPU_ID == CPU_BE_LITE)
	ncl_erd_init();
#endif
#if HAVE_CACHE_READ
	ncl_cache_read_init();
#endif

#if HAVE_TSB_SUPPORT
	if (nand_support_aipr()) {
		read_fins_templ.dw0.b.mp_enh = 1;
        refresh_fins_templ.dw0.b.mp_enh = 1;
        read_streaming_fast_fins_templ.dw0.b.mp_enh = 1;
	}
#endif

#if ONFI_DCC_TRAINING
    extern void ficu_vsc_template_init(void);
	ficu_vsc_template_init();
#endif
#ifdef REFR_RD
    rfrd_times_struct.rfrd_all_die = nand_target_num() * nand_channel_num() * nand_lun_num() ;
    if(rfrd_times_struct.rfrd_all_die > PWR_MAX_RFRD_DIE)
    {
        rfrd_times_struct.power_on_rfrd_die = PWR_MAX_RFRD_DIE;
    }
    else
    {
        rfrd_times_struct.power_on_rfrd_die = rfrd_times_struct.rfrd_all_die;
    }
    if(rfrd_times_struct.rfrd_all_die > IDLE_MAX_RFRD_DIE)
    {
        rfrd_times_struct.idle_rfrd_die = IDLE_MAX_RFRD_DIE;
    }
    else
    {
        rfrd_times_struct.idle_rfrd_die = rfrd_times_struct.rfrd_all_die;
    }
    rfrd_times_struct.power_on_rfrd_times = (rfrd_times_struct.rfrd_all_die / rfrd_times_struct.power_on_rfrd_die);
    rfrd_times_struct.idle_rfrd_times = (rfrd_times_struct.rfrd_all_die / rfrd_times_struct.idle_rfrd_die);
    rfrd_times_struct.current_rfrd_die =  rfrd_times_struct.power_on_rfrd_die;
    ncl_cmd_trace(LOG_INFO, 0x54d7, "power_on_rfrd_times = %d idle_rfrd_times = %d", rfrd_times_struct.power_on_rfrd_times,rfrd_times_struct.idle_rfrd_times );

#if (OTF_TIME_REDUCE == ENABLE)
	if (!misc_is_warm_boot())
#endif
	{
	    //strpg_pullLOW_test(10);
	    read_refresh_poweron( rfrd_times_struct.power_on_rfrd_times);
        //strpg_pullLOW_test(10);
	}
    rfrd_times_struct.current_rfrd_die = rfrd_times_struct.idle_rfrd_die;
    extern u8 evt_refresh_read;
    evt_register(_refresh_read_task, 0, &evt_refresh_read);
    refresh_read_timer.function = refresh_read_task;
    refresh_read_timer.data = NULL;

    mod_timer(&refresh_read_timer, jiffies+1);
    //ncl_cmd_trace(LOG_INFO, 0, "enter ftl core init and refresh read timer init ");
#endif


}



#if HAVE_TSB_SUPPORT

#ifdef REFR_RD
ucmd_code void refresh_read_cpl(struct ncl_cmd_t *ncl_cmd)
{
    /*u32 pda_log = ncl_cmd->addr_param.common_param.pda_list[0];
    ncl_cmd_trace(LOG_ALW, 0xc701, "[refresh_read_cpl]pda = 0x%x ,PL[%d],CH[%d],CE[%d],LUN[%d],rerf_undone_cnt=%d", 
            pda_log,pda2plane(pda_log),pda2ch(pda_log),pda2ce(pda_log),pda2lun(pda_log),rerf_undone_cnt);*/

    rerf_undone_cnt--;
    decrease_otf_cnt(ncl_cmd->die_id);
    if(rerf_undone_cnt == 0)
    {
        cpl_ref = true;
	    if (rfrd_times_struct.idle_rfrd_times  != 1) //(1)2210 m.2 4T (2)U.2 4T (3)U.2 8T 
	    {
	        //srb_t *srb = (srb_t *)SRAM_BASE;
	        //if((srb->cap_idx != 1) && (rfrd_time_start % (rfrd_times_struct.idle_rfrd_times>>1)))
	        if(rfrd_time_start % (rfrd_times_struct.idle_rfrd_times >>1))
	        {
	            //ncl_cmd_trace(LOG_INFO, 0x99b3, "over refresh_read_cpl cpa_idx %d, rfrd time %d ", srb->cap_idx, rfrd_time_start);
	            //if(ncl_cmd_in == true)
	                refresh_read_start_time = get_tsc_lo();
	                evt_set_cs(evt_refresh_read, 0, 0, CS_TASK);
	           // elses
	                //refresh_read_task(NULL);
	        }
	    }
    }
}

/*!
 * @brief Power on read refresh required by BiCS3
 * Each block should do read after power on or periodic 3mins
 *
 * @return	not used
 */
#if 0
fast_code pda_t get_refresh_pda(u8 var,u32 spb)
{

    /*(1)blk and pg index is correct information
      (2)Pl_DU bit store times information      */
    //---------------(1)calculate CH CE LUN index
    u8 ch_id ,ce_id, lun_id;
    #if (CPU_ID == 2)
    ch_id = var % ((nand_info.geo.nr_channels >> 1));
    #elif(CPU_ID == 4)
    ch_id = var % (nand_info.geo.nr_channels >> 1) + 0x4;
    #endif
    ce_id = var / (nand_info.geo.nr_channels >> 1);
    lun_id = var / ((nand_info.geo.nr_channels >> 1) * nand_info.geo.nr_targets);
    //---------------(2)calculate pda
    pda_t pda = spb << nand_info.pda_block_shift;//set spb index
    pda |= (nand_page_num_slc()-1) << nand_info.pda_page_shift; //the last WL,SLC
    pda |= (ch_id << nand_info.pda_ch_shift | ce_id << nand_info.pda_ce_shift | lun_id << nand_info.pda_lun_shift);

    //pda &= (~((1 << nand_info.pda_ch_shift) - 1));
    //ncl_cmd_trace(LOG_INFO, 0, "[pda]:0x%x,[spb]:%d,[var]:%d",pda,spb,var);
    return pda;
}
#else
fast_code pda_t get_refresh_pda(u8 var,u32 spb)
{
    
    /*(1)blk and pg index is correct information
      (2)Pl_DU bit store times information      */
    //---------------(1)calculate CH CE LUN index
    u8 ch_id ,ce_id, lun_id;
    #if (CPU_ID == 2)     
    ch_id = var % ((nand_info.geo.nr_channels >> 1));
    #elif(CPU_ID == 4)
    ch_id = var % (nand_info.geo.nr_channels >> 1) + 0x4;
    #endif
    ce_id = var / (nand_info.geo.nr_channels >> 1);
    lun_id = var / ((nand_info.geo.nr_channels >> 1) * nand_info.geo.nr_targets);
    //---------------(2)calculate pda
    pda_t pda = spb << nand_info.pda_block_shift;//set spb index
    pda |= (nand_page_num_slc()-1) << nand_info.pda_page_shift; //the last WL,SLC
    pda |= (ch_id << nand_info.pda_ch_shift | ce_id << nand_info.pda_ce_shift | lun_id << nand_info.pda_lun_shift|rfrd_time_start);
    
    //pda &= (~((1 << nand_info.pda_ch_shift) - 1));
    //ncl_cmd_trace(LOG_INFO, 0xcee7, "[pda]:0x%x,[spb]:%d,[var]:%d",pda,spb,var);
    return pda;
}
#endif
fast_code void read_refresh_poweron(u32 rfrd_times)
{   
    //ncl_cmd_trace(LOG_INFO, 0x7fb3, "read_refresh_poweron start");
	u32 spb_id,times;
	struct ncl_cmd_t ncl_cmd;
	pda_t pda;
	ncl_cmd.op_code = NCL_CMD_READ_REFRESH;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.completion = NULL;

	for (spb_id = 0; spb_id < nand_block_num(); spb_id++)
    {
        for(times = 0; times < rfrd_times; times++ )
        {
            
            ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
            pda = spb_id << nand_info.pda_block_shift;
            pda |= (nand_page_num_slc()-1) << nand_info.pda_page_shift; //the last WL,SLC
            pda |= times;          
            ncl_cmd_submit_insert_schedule(&ncl_cmd, true);
        }
	}
}

static slow_code int refresh_read(int argc, char *argv[])
{
    u32 num_die = atoi(argv[1]);
    u32 rfrd_time = rfrd_times_struct.rfrd_all_die / num_die;
    read_refresh_poweron(rfrd_time);
    return 0;
}

/*fast_code int refresh_read_normal(int argc, char *argv[])
{
    rfrd_ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
    u32 times = atoi(argv[1]);
    if(spb_start == nand_block_num())
		spb_start = 0;
    for(spb_start = 0; spb_start < times; spb_start++){
        rfrd_ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
        rfrd_pda = spb_start << nand_pda_block_shift();
        ncl_cmd_trace(LOG_INFO, 0, "enter refresh read circulation,spb_id = %d",spb_start);
    	ncl_cmd_submit(&rfrd_ncl_cmd);
    }
       return 0;
}*/


fast_code void refresh_read_task(void *data)
{
    //u8 jiffies_times;
    //ncl_cmd_trace(LOG_INFO, 0x5df6, "refresh_read_task");
#if 1 //STOP_PRRD_WARMBOOT-20210302-Eddie	20210426-Eddie-mod
    if(misc_is_STOP_BGREAD())
    {
		del_timer(&refresh_read_timer);
		evlog_printk(LOG_ALW,"WARM BOOT STOP refresh read");
		return;
   	}
#endif
	extern bool fgdumplogpi;
    if(all_init_done == false || !cpl_ref || plp_trigger || hostReset || gFormatFlag || fgdumplogpi)
    {
        mod_timer(&refresh_read_timer, jiffies+HZ/10);
        /*ncl_cmd_trace(LOG_ERR, 0, "all_init_done %d cpl_ref %d plp_trigger %d hostReset %d gFormatFlag %d fgdumplogpi %d",
            all_init_done, cpl_ref, plp_trigger,hostReset, gFormatFlag,fgdumplogpi);*/
        return;
    }
    #ifdef REFR_TIME_TEST
    if(spb_start == 0)
    {
        rfrd_ticket_st = get_tsc_64();
    }
    #endif
    if(rfrd_time_start == rfrd_times_struct.idle_rfrd_times )
    {
        rfrd_time_start = 0;
        spb_start++;
    }
    if(spb_start == nand_block_num())
    {
        #ifdef REFR_TIME_TEST
        rfrd_ticket_ed = time_elapsed_in_ms(rfrd_ticket_st);
        ncl_cmd_trace(LOG_INFO, 0x996b, "scan all SPB cost[%d]ms",rfrd_ticket_ed);
        #else
        ncl_cmd_trace(LOG_INFO, 0xd180, "have touched all spb and start touch again\n");
        #endif
        spb_start = 0;
    }
    //scan CPU2/CPU4 all DIE
    rfrd_ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
    #if 0
    u32 die_start = (rfrd_times_struct.idle_rfrd_die >> 1) * rfrd_time_start ;
    if(cpl_ref)
    {
        rfrd_time_start++;
        cpl_ref = false;


        for(u32 i = die_start; i < die_start + (rfrd_times_struct.idle_rfrd_die >> 1); i++)
        {
            rerf_undone_cnt++;
            pda_t pda_refread = get_refresh_pda(i,spb_start); 
            /*if(pda2blk(pda_refread) == 5)
            {
                ncl_cmd_trace(LOG_ALW, 0x72ce, "[refresh_read_task]ncl_cmd_read_refresh pda = 0x%x ,CH[%d],CE[%d],LUN[%d]", 
                pda_refread,pda2ch(pda_refread),pda2ce(pda_refread),pda2lun(pda_refread));
            }*/
            u32 die_index = pda2die(pda_refread);
            rfrd_ncl_cmd_bylun[die_index] = rfrd_ncl_cmd;
            rfrd_pda[die_index] = pda_refread;
            rfrd_ncl_cmd_bylun[die_index].die_id = die_index;
            rfrd_ncl_cmd_bylun[die_index].addr_param.common_param.pda_list = &rfrd_pda[die_index];
            ncl_cmd_submit_insert_schedule(&rfrd_ncl_cmd_bylun[die_index], false);
        }

    }
    #else

    u32 die_start = (rfrd_times_struct.idle_rfrd_die >> 1) * rfrd_time_start ;
    if(cpl_ref)
    {        
        cpl_ref = false;   
        rerf_undone_cnt++;
        pda_t pda_refread = get_refresh_pda(die_start,spb_start); 
        rfrd_time_start++;  
        //u32 die_index = pda2die(pda_refread); 
        #if (CPU_ID ==2)
            rfrd_pda[0] = pda_refread;
            rfrd_ncl_cmd_byCPU[0] = rfrd_ncl_cmd;
            rfrd_ncl_cmd_byCPU[0].die_id = 0;
            rfrd_ncl_cmd_byCPU[0].addr_param.common_param.pda_list = &rfrd_pda[0];  
            ncl_cmd_submit_insert_schedule(&rfrd_ncl_cmd_byCPU[0], false);    
        #elif (CPU_ID == 4)
            rfrd_pda[1] = pda_refread;
            rfrd_ncl_cmd_byCPU[1] = rfrd_ncl_cmd;
            rfrd_ncl_cmd_byCPU[1].die_id = rfrd_times_struct.rfrd_all_die - 1;
            rfrd_ncl_cmd_byCPU[1].addr_param.common_param.pda_list = &rfrd_pda[1];  
            ncl_cmd_submit_insert_schedule(&rfrd_ncl_cmd_byCPU[1], false);   
        #endif
                                
    } 
    #endif
    //In order to match NPD standard(per 200s scan all SPB)
    if(rfrd_times_struct.idle_rfrd_times  != 1) 
    {
        //srb_t *srb = (srb_t *)SRAM_BASE; 
        if(rfrd_time_start / (rfrd_times_struct.idle_rfrd_times >>1))
        {
            mod_timer(&refresh_read_timer,jiffies + 2*HZ/10); 
        }
    }
    else
    { 
        //ncl_cmd_trace(LOG_INFO, 0x7127, "[mod_timer]rfrd_time_start = %d ",rfrd_time_start);
        mod_timer(&refresh_read_timer,jiffies + 4*HZ/10); 
        //cost 200ms*blk_cnt(831) = 166s < 200s
    }
    
}

fast_code void _refresh_read_task(u32 param, u32 payload, u32 count)
{
			u32 timer_lo = get_tsc_lo();
		if (((refresh_read_start_time < timer_lo) && (timer_lo - refresh_read_start_time > (CYCLE_PER_MS*10))) || \
			((refresh_read_start_time > timer_lo) && (INV_U32 - refresh_read_start_time + timer_lo >  \
            (CYCLE_PER_MS*10)))){               
                //ncl_cmd_trace(LOG_DEBUG, 0, "10ms delay");
                refresh_read_task(NULL);
                //refresh_read_start_time = get_tsc_lo();
            }
     else{
        evt_set_cs(evt_refresh_read, 0, 0, CS_TASK);
     }
}

static DEFINE_UART_CMD(refresh_read, "refresh_read",
	"refresh_read power on",
	"refresh_read power on",
	0, 1, refresh_read);
/*static DEFINE_UART_CMD(refresh_read_normal, "refresh_read_normal",
	"refresh_read_normal power on",
	"refresh_read_normal power on",
	0, 1, refresh_read_normal);*/
#endif

#endif
/*! @} */
