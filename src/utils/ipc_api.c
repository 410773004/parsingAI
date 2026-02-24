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

/*! \file ipc_api.c
 * @brief provide api for ipc, all communication between CPU is better to rely on this
 *
 * \addtogroup utils
 * \defgroup ipc_api
 * \ingroup utils
 * @{
 */

#ifdef MPC
#include "types.h"
#include "stdlib.h"
#include "string.h"
#include "bitops.h"
#include "cpu_msg.h"
#include "sect.h"
#include "queue.h"
#include "bf_mgr.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "ipc_api.h"
#include "erase.h"
#include "ftl_export.h"
#include "fc_export.h"
#include "srb.h"
#include "event.h"
#include "mpc.h"
#include "fw_download.h"
#include "btn_export.h"
#include "trim.h"
#include "read_error.h"
#include "ddr.h"
#include "read_retry.h"
#include "spin_lock.h"
#ifdef RDISK
#include "die_que.h"
#endif
//Andy_Crypto
#if defined(USE_CRYPTO_HW)
#include "crypto.h"
#endif

#if CPU_ID == CPU_BE_LITE
#include "scheduler.h"
#endif
#include "GList.h"

#ifdef TCG_NAND_BACKUP
#include "tcg_nf_mid.h"
#endif

/*! \cond PRIVATE */
#define __FILEID__ ipca
#include "trace.h"
/*! \endcond */

/*
 * IPC API naming rule:
 * 1. alias function
 *    "__" prefix should be added.
 *    Ex: ncl_cmd_submit should be "__ncl_cmd_submit"
 *
 * 2. ipc message handler function name
 *    ipc_xxx
 *
 * 3. callback function name of ipc message handler
 *    _ipc_xxx_done
 *
 * 4. non alias API function
 *    ipc_api_xxx
 */

/*! @brief header of ncl_cmd_submit from other cpu */
typedef struct _ncl_cmd_ipc_hdl_t {
	u8 tx;					///< command tx
	bool sync;				///< sync if original command is sync or not
	void *caller;				///< original command caller
	void (*completion)(struct ncl_cmd_t *);	///< original command completion callback
} ncl_cmd_ipc_hdl_t;

/*! @brief header of srb_load_mr_defect from other cpu */
typedef struct _srb_load_mr_defect_ipc_hdl_t {
	u8 *bmp;			///< defect bitmap
	u32 bbt_width;			///< bad block table width per spb
} srb_load_mr_defect_ipc_hdl_t;

/*! @brief header of ncl_scan_defect from other cpu */
typedef struct _ncl_scan_defect_ipc_hdl_t {
	u32 spb_id;		///< target spb
	u32 *defect;		///< defect bitmap
} ncl_scan_defect_ipc_hdl_t;

fast_data u8 evt_wait_btn_rst = 0xFF;

#if !defined(RDISK)
#ifndef CPU_FTL
#define CPU_FTL 5	/// make it error when it was called
#endif
#endif

// alias weak functions for share function name between cpu, please refer orignal function definition
extern void __attribute__((weak, alias("__ncl_cmd_submit"))) ncl_cmd_submit(struct ncl_cmd_t *ncl_cmd);
extern void __attribute__((weak, alias("__ncl_cmd_submit_insert_schedule"))) ncl_cmd_submit_insert_schedule(struct ncl_cmd_t *ncl_cmd, bool direct_handle);
extern void __attribute__((weak, alias("__ncl_cmd_block"))) ncl_cmd_block(void);
extern bool __attribute__((weak, alias("__ncl_cmd_empty"))) ncl_cmd_empty(bool rw);
extern void __attribute__((weak, alias("__ncl_cmd_rapid_single_du_read"))) ncl_cmd_rapid_single_du_read(struct ncl_cmd_t *ncl_cmd);
extern void __attribute__((weak, alias("__ncl_cmd_wait_completion"))) ncl_cmd_wait_completion(void);
extern void __attribute__((weak, alias("__ncl_handle_pending_cmd"))) ncl_handle_pending_cmd(void);
extern void __attribute__((weak, alias("__ftl_core_erase"))) ftl_core_erase(erase_ctx_t *ctx);
extern void __attribute__((weak, alias("__fcore_idle_flush"))) fcore_idle_flush(void);
extern void __attribute__((weak, alias("__ftl_core_reset_notify"))) ftl_core_reset_notify(void);
extern void __attribute__((weak, alias("__plp_cancel_die_que"))) plp_cancel_die_que(void);
extern void __attribute__((weak, alias("__ftl_reset_notify"))) ftl_reset_notify(void);
extern void __attribute__((weak, alias("__ftl_reset_done_notify"))) ftl_reset_done_notify(void);
extern void __attribute__((weak, alias("__ftl_core_reset_done_notify"))) ftl_core_reset_done_notify(void);
extern void __attribute__((weak, alias("__bm_data_copy"))) bm_data_copy(u64 src, u64 dst, u32 nbytes, dpe_cb_t cb, void *ctx);
extern ftl_err_t __attribute__((weak, alias("__ftl_core_flush"))) ftl_core_flush(ftl_flush_data_t *fctx);
extern void __attribute__((weak, alias("__bm_sha3_sm3_calc_part"))) bm_sha3_sm3_calc_part(void *mem, void *result, bool sm3, u32 count, u32 cur_count, dpe_cb_t callback, void *ctx, bool first);
extern void __attribute__((weak, alias("__ftl_core_format"))) ftl_core_format(ftl_format_t *format);
extern bool __attribute__((weak, alias("__ftl_core_flush_tbl"))) ftl_core_flush_tbl(ftl_flush_tbl_t *ctx);
extern void __attribute__((weak, alias("__srb_load_mr_defect"))) srb_load_mr_defect(u8 *ftl_bitmap, u32 bbt_width);
extern void __attribute__((weak, alias("__bm_err_commit"))) bm_err_commit(u32 du_ofst, u32 btag);
extern void __attribute__((weak, alias("__ipc_read_recoveried_commit"))) read_recoveried_commit(bm_pl_t *bm_pl, u16 pdu_bmp);
extern void __attribute__((weak, alias("__btn_semi_wait_switch"))) btn_semi_wait_switch(bool wait);
extern void __attribute__((weak, alias("__ftl_core_start"))) ftl_core_start(u32 nsid);
extern void __attribute__((weak, alias("__ftl_flush"))) ftl_flush(flush_ctx_t *ctx);
extern void __attribute__((weak, alias("__ftl_core_qbt_alloc"))) ftl_core_qbt_alloc(ns_start_t *ctx);
extern void __attribute__((weak, alias("__ftl_format"))) ftl_format(format_ctx_t *ctx);
extern bool __attribute__((weak, alias("__ftl_open"))) ftl_open(u32 nsid);

extern void __attribute__((weak, alias("__ftl_core_update_fence"))) ftl_core_update_fence(ftl_fence_t *fence);
extern void __attribute__((weak, alias("__ftl_core_restore_fence"))) ftl_core_restore_fence(ftl_fence_t *fence);
extern bool __attribute__((weak, alias("__ftl_core_flush_misc"))) ftl_core_flush_misc(ftl_flush_misc_t *misc);
extern bool __attribute__((weak, alias("__ftl_core_flush_blklist"))) ftl_core_flush_blklist(ftl_flush_misc_t *flush_misc);
extern void __attribute__((weak, alias("__ftl_core_restore_rt_flags"))) ftl_core_restore_rt_flags(spb_rt_flags_t *rt_flags);
extern void __attribute__((weak, alias("__ftl_core_gc_start"))) ftl_core_gc_start(gc_req_t *gc_req);
extern void __attribute__((weak, alias("__tzu_get_gc_info"))) tzu_get_gc_info();

#if(WARMBOOT_FTL_HANDLE == mENABLE)
extern void __attribute__((weak, alias("__ftl_warm_boot_handle"))) ftl_warm_boot_handle(void);
#endif

extern void __attribute__((weak, alias("__ftl_l2p_partial_reset"))) ftl_l2p_partial_reset(u32 ns_sec_id);

extern bool __attribute__((weak, alias("__spb_release"))) spb_release(spb_id_t spb_id);
extern bool __attribute__((weak, alias("__spb_clear_ec"))) spb_clear_ec(void);
extern bool __attribute__((weak, alias("__pbt_release"))) pbt_release(spb_id_t spb_id);



extern void __attribute__((weak, alias("__ftl_gc_reset"))) gc_re();
extern bool __attribute__((weak, alias("__ftl_core_spb_pad"))) ftl_core_spb_pad(ftl_spb_pad_t *pctx);
extern void __attribute__((weak, alias("__ncl_spb_defect_scan"))) ncl_spb_defect_scan(u32 spb, u32 *defect);	///< only used in RAWDISK
extern void __attribute__((weak, alias("__rawdisk_4k_read"))) rawdisk_4k_read(int btag);	///< only used in RAWDISK
extern void __attribute__((weak, alias("__raid_correct_push"))) raid_correct_push(rc_req_t *req);
extern void __attribute__((weak, alias("__read_done_rc_entry"))) read_done_rc_entry(struct ncl_cmd_t *ncl_cmd);
extern bool __attribute__((weak, alias("__ftl_core_gc_action"))) ftl_core_gc_action(gc_action_t *act);
#if(PLP_GC_SUSPEND == mDISABLE)
extern bool __attribute__((weak, alias("__ftl_core_gc_action2"))) ftl_core_gc_action2(gc_action_t *act);
#endif
extern bool __attribute__((weak, alias("__gc_action"))) gc_action(gc_action_t *act);
#if GC_SUSPEND_FWDL		//20210308-Eddie
extern void __attribute__((weak, alias("__FWDL_GC_Handle"))) FWDL_GC_Handle(u8 type);		//20210308-Eddie
#endif
extern void __attribute__((weak, alias("__fe_req_op"))) fe_log_read(fe_req_t *req);
extern void __attribute__((weak, alias("__fe_req_op"))) fe_log_flush(fe_req_t *req);
extern void __attribute__((weak, alias("__fe_req_op"))) fe_ns_info_save(fe_req_t *req);
extern void __attribute__((weak, alias("__fe_req_op"))) fe_ns_info_load(fe_req_t *req);
extern void __attribute__((weak, alias("__ftl_core_p2l_load"))) ftl_core_p2l_load(p2l_load_req_t *load_req);
extern void __attribute__((weak, alias("__read_recoveried_done"))) read_recoveried_done(dtag_t dtag);
extern void __attribute__((weak, alias("__pmu_swap_file_register"))) pmu_swap_file_register(void *loc, u32 size);
extern void __attribute__((weak, alias("__nvmet_core_btn_cmd_done"))) nvmet_core_btn_cmd_done(int btag, bool ret_cq, u32 cid);
extern bool __attribute__((weak, alias("__fwdl_op"))) fwdl_download(fwdl_req_t *req);
extern bool __attribute__((weak, alias("__fwdl_op"))) fwdl_commit(fwdl_req_t *req);
extern void __attribute__((weak, alias("__journal_update")))  journal_update(u16 evt_reason_id, u32 use_0);
#if epm_enable
extern void __attribute__((weak, alias("__epm_update"))) epm_update(u32 epm_sign, u32 cpu_id);
#if FRB_remap_enable
extern void __attribute__((weak, alias("__epm_remap_tbl_flush"))) epm_remap_tbl_flush(pda_t *pda_base);
#endif
#if epm_spin_lock_enable
extern void __attribute__((weak, alias("__get_epm_access_key"))) get_epm_access_key(u32 cpu_id, u32 epm_sign);
extern void __attribute__((weak, alias("__unlock_epm_ddr"))) unlock_epm_ddr(u32 epm_sign,u32 cpu_id);
#endif
#endif
#ifdef FWcfg_Rebuild	//20201008-Eddie
extern void __attribute__((weak, alias("__FW_CONFIG_Rebuild"))) FW_CONFIG_Rebuild(fw_config_set_t *fw_config);
#endif
extern bool __attribute__((weak, alias("__erase_srb"))) erase_srb(void);		//20201014-Eddie

#if 0//defined(USE_CRYPTO_HW)
//Andy add change crypto info
extern void __attribute__((weak, alias("__crypto_change_mode_range"))) crypto_change_mode_range(u8 crypto_type, u8 NS_ID, u8 change_key, u8 cryptoID);
extern void __attribute__((weak, alias("__crypto_AES_EPM_Read"))) crypto_AES_EPM_Read(u8 cryptID, u8 mode);
#endif

#ifdef ERRHANDLE_ECCT
extern void __attribute__((weak, alias("__ECC_Table_Operation"))) ECC_Table_Operation(stECCT_ipc_t *ecct_req);
extern u16 __attribute__((weak, alias("__ECC_Build_Table"))) wECC_Build_Table(void);
extern void __attribute__((weak, alias("__rc_reg_ecct"))) rc_reg_ecct(bm_pl_t *bm_pl, u8 type); //register ECCT
#endif

extern void __attribute__((weak, alias("__get_program_and_erase_fail_count"))) get_program_and_erase_fail_count();
extern void __attribute__((weak, alias("__get_avg_erase_cnt"))) get_avg_erase_cnt(u32 *avg_erase, u32 *max_erase, u32 *min_erase, u32 *total_ec);
extern void __attribute__((weak, alias("__get_nand_byte_written"))) get_nand_byte_written();
extern void __attribute__((weak, alias("__rd_err_handling"))) rd_err_handling(struct ncl_cmd_t *ncl_cmd);

//for aer event trigger
extern void __attribute__((weak, alias("__nvmet_evt_aer_in"))) nvmet_evt_aer_in(u32 type_sts, u32 param);
/*core2 - 4*/
//extern __attribute__((weak)) void nvmet_evt_aer_in();
//nvmet_evt_aer_in(sts,param);

extern void __attribute__((weak, alias("__eccu_dufmt_switch"))) eccu_dufmt_switch(enum cmf_idx_t idx);
extern void __attribute__((weak, alias("__eccu_switch_cmf"))) eccu_switch_cmf(enum cmf_idx_t idx);

#ifdef TCG_NAND_BACKUP
extern void __attribute__((weak, alias("__tcg_nf_op")))  tcg_nf_Start(tcg_nf_params_t *param);
#endif

/*!
 * @brief check if pointer is shared between cpu
 *
 * @param _ptr	pointer to be checked
 *
 * @return	return true if remote cpu can access
 */
static inline bool is_ptr_sharable(void *_ptr)
{
	u32 ptr = (u32)_ptr;

	if (ptr >= SRAM_BASE && ptr < (SRAM_BASE + SRAM_SIZE))
		return true;

	if (ptr >= BTCM_SH_BASE && ptr < (BTCM_SH_BASE + BTCM_SH_SIZE))
		return true;

	return false;
}



/*!
 * @brief translate ncl command to dma address and shared between CPUs
 *
 * @note because there are some pointers in ncl command, all pointer should be in local tcm
 *
 * @param ncl_cmd	ncl command pointer, and it must in local tcm
 *
 * @return		translated ncl command address
 */
static inline struct ncl_cmd_t *make_ncmd_sharable(struct ncl_cmd_t *ncl_cmd)
{
    if(ncl_cmd->op_code != NCL_CMD_SET_GET_FEATURE)
    {
    	if (!is_ptr_sharable(ncl_cmd->addr_param.common_param.info_list)) {
    		ncl_cmd->addr_param.common_param.info_list = (struct info_param_t *)
    			tcm_local_to_share(ncl_cmd->addr_param.common_param.info_list);
    	}

    	if (!is_ptr_sharable(ncl_cmd->addr_param.common_param.pda_list)) {
    		ncl_cmd->addr_param.common_param.pda_list = (pda_t *)
    			tcm_local_to_share(ncl_cmd->addr_param.common_param.pda_list);
    	}

    	if (!is_ptr_sharable(ncl_cmd->user_tag_list))
    		ncl_cmd->user_tag_list = (bm_pl_t *) tcm_local_to_share(ncl_cmd->user_tag_list);
    }
    else
    {
        if (!is_ptr_sharable(ncl_cmd->addr_param.rw_raw_param.info_list)) {
        	ncl_cmd->addr_param.rw_raw_param.info_list = (struct info_param_t *)
        		tcm_local_to_share(ncl_cmd->addr_param.rw_raw_param.info_list);
        }

        if (!is_ptr_sharable(ncl_cmd->addr_param.rw_raw_param.pda_list)) {
        	ncl_cmd->addr_param.rw_raw_param.pda_list = (pda_t *)
        		tcm_local_to_share(ncl_cmd->addr_param.rw_raw_param.pda_list);
        }
        if (!is_ptr_sharable(ncl_cmd->addr_param.rw_raw_param.column)) {
        	ncl_cmd->addr_param.rw_raw_param.column = (struct raw_column_list *)
            tcm_local_to_share(ncl_cmd->addr_param.rw_raw_param.column);
        }
    }

	if (!is_ptr_sharable(ncl_cmd))
		ncl_cmd = (struct ncl_cmd_t *) tcm_local_to_share(ncl_cmd);

	return ncl_cmd;
}

/*!
 * @brief translate ncl command to local address
 *
 * @param ncl_cmd	ncl command pointer, and it must be dma address
 *
 * @return		local tcm pointer of ncl command
 */
static inline struct ncl_cmd_t *make_ncmd_local(struct ncl_cmd_t *ncl_cmd)
{
	if (is_ptr_tcm_share(ncl_cmd))
		ncl_cmd = (struct ncl_cmd_t *) tcm_share_to_local(ncl_cmd);

    if(ncl_cmd->op_code != NCL_CMD_SET_GET_FEATURE)
    {
    	if (is_ptr_tcm_share(ncl_cmd->addr_param.common_param.info_list)) {
    		ncl_cmd->addr_param.common_param.info_list = (struct info_param_t *)
    			tcm_share_to_local(ncl_cmd->addr_param.common_param.info_list);
    	}

    	if (is_ptr_tcm_share(ncl_cmd->addr_param.common_param.pda_list)) {
    		ncl_cmd->addr_param.common_param.pda_list = (pda_t *)
    			tcm_share_to_local(ncl_cmd->addr_param.common_param.pda_list);
    	}

    	if (is_ptr_tcm_share(ncl_cmd->user_tag_list))
    		ncl_cmd->user_tag_list = (bm_pl_t *) tcm_share_to_local(ncl_cmd->user_tag_list);
    }
    else
    {
    	if (is_ptr_tcm_share(ncl_cmd->addr_param.rw_raw_param.info_list)) {
    		ncl_cmd->addr_param.rw_raw_param.info_list = (struct info_param_t *)
    			tcm_share_to_local(ncl_cmd->addr_param.rw_raw_param.info_list);
    	}

    	if (is_ptr_tcm_share(ncl_cmd->addr_param.rw_raw_param.pda_list)) {
    		ncl_cmd->addr_param.rw_raw_param.pda_list = (pda_t *)
    			tcm_share_to_local(ncl_cmd->addr_param.rw_raw_param.pda_list);
    	}
        if (is_ptr_tcm_share(ncl_cmd->addr_param.rw_raw_param.column)) {
    		ncl_cmd->addr_param.rw_raw_param.column = (struct raw_column_list *)
    			tcm_share_to_local(ncl_cmd->addr_param.rw_raw_param.column);
    	}
    }

	return ncl_cmd;
}

/*!
 * @brief make rapid ncl command shared between cpu, translate it to dma address
 *
 * @param ncl_cmd	ncl command pointer, it must be local tcm
 *
 * @return		dma address of ncl command
 */
static inline struct ncl_cmd_t *make_rapid_ncmd_sharable(struct ncl_cmd_t *ncl_cmd)
{
	return (struct ncl_cmd_t *) tcm_local_to_share(ncl_cmd);
}

/*!
 * @brief make rapid ncl command to local, it must be dma address
 *
 * @param ncl_cmd	ncl command pointer, it must be dma address
 *
 * @return		local tcm address of ncl command
 */
static inline struct ncl_cmd_t *make_rapid_ncmd_local(struct ncl_cmd_t *ncl_cmd)
{
	return (struct ncl_cmd_t *) tcm_share_to_local(ncl_cmd);
}

static inline u32 get_dest_scheduler(struct ncl_cmd_t *ncl_cmd)
{
	u32 rx = CPU_BE - 1;
#if defined(DUAL_BE)
	extern volatile ftl_flags_t shr_ftl_flags;
	if (shr_ftl_flags.b.boot_cmpl) {
		u32 i;
		u32 ch_num = shr_nand_info.geo.nr_channels;
		pda_t pda = ncl_cmd->addr_param.common_param.pda_list[0];
		u32 ch = (pda >> shr_nand_info.pda_ch_shift) & (ch_num - 1);
		u32 grp = (ch < (ch_num >> 1)) ? 0 : 1;

		for (i = 1; i < ncl_cmd->addr_param.common_param.list_len; i++) {
			pda = ncl_cmd->addr_param.common_param.pda_list[i];
			ch = (pda >> shr_nand_info.pda_ch_shift) & (ch_num - 1);
			u32 _grp = (ch < (ch_num >> 1)) ? 0 : 1;
			sys_assert(grp == _grp);
		}

		if (grp == 1)
			rx = CPU_BE_LITE - 1;
	}
#endif
	return rx;
}

fast_code void __ncl_cmd_submit(struct ncl_cmd_t *ncl_cmd)
{
	bool sync = (ncl_cmd->flags & NCL_CMD_SYNC_FLAG) ? true : false;
	u32 rx;

	rx = get_dest_scheduler(ncl_cmd);

	ncl_cmd = make_ncmd_sharable(ncl_cmd);

	if (sync) {
		cpu_msg_sync_start();
		cpu_msg_issue(rx, CPU_MSG_NCMD, 0, (u32) ncl_cmd);
		//cpu_msg_sync_end();
		cpu_msg_sync_end2();
		ncl_cmd = make_ncmd_local(ncl_cmd);
	} else {
		cpu_msg_issue(rx, CPU_MSG_NCMD, 0, (u32) ncl_cmd);
	}
}

fast_code void __rd_err_handling(struct ncl_cmd_t *ncl_cmd)
{
	bool sync = (ncl_cmd->flags & NCL_CMD_SYNC_FLAG) ? true : false;
	u32 rx;

	rx = get_dest_scheduler(ncl_cmd);

	ncl_cmd = make_ncmd_sharable(ncl_cmd);

	if (sync) {
		cpu_msg_sync_start();
		cpu_msg_issue(rx, CPU_MSG_RD_ERR_HANDLING, 0, (u32) ncl_cmd);
		cpu_msg_sync_end();
		ncl_cmd = make_ncmd_local(ncl_cmd);
	} else {
		cpu_msg_issue(rx, CPU_MSG_RD_ERR_HANDLING, 0, (u32) ncl_cmd);
	}
}


fast_code void __ncl_cmd_submit_insert_schedule(struct ncl_cmd_t*ncl_cmd ,bool direct_handle)
{
    bool sync = (ncl_cmd->flags & NCL_CMD_SYNC_FLAG) ? true : false;
    u32 rx;
    rx = get_dest_scheduler(ncl_cmd);
    ncl_cmd = make_ncmd_sharable(ncl_cmd);

    if (sync) {
		cpu_msg_sync_start();
		cpu_msg_issue(rx, CPU_MSG_NCMD, 0, (u32) ncl_cmd);
		cpu_msg_sync_end();
		ncl_cmd = make_ncmd_local(ncl_cmd);
	}
    else if(direct_handle)
    {
        cpu_msg_issue(rx, CPU_MSG_NCMD, 0, (u32) ncl_cmd);
    }
    else
	{
        cpu_msg_issue(rx,CPU_MSG_NCMD_INSERT_SCH,0,(u32)ncl_cmd);
	}

}

ddr_code void __journal_update(u16 evt_reason_id, u32 use)
{
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_JOURNAL_UPDATE, (u32)evt_reason_id, use);
}

#if epm_enable
fast_code void __epm_update(u32 epm_sign,u32 cpu_id)
{
#if epm_spin_lock_enable
	set_clr_ddr_set_done(true,cpu_id,epm_sign);
#endif
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_EPM_UPDATE, cpu_id , epm_sign);
}

#if FRB_remap_enable
fast_code void __epm_remap_tbl_flush(pda_t *pda_base)
{
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_EPM_REMAP_tbl_UPDATE, 0 , (u32)*pda_base);
}
#endif

#if epm_spin_lock_enable
fast_code void __get_epm_access_key(u32 cpu_id, u32 epm_sign)
{
	if(epm_debug) utils_ipc_trace(LOG_ERR, 0x2fe8, "__get_epm_access_key send ipc to cpu4\n");
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_EPM_GET_KEY, cpu_id, epm_sign);

}
fast_code void __unlock_epm_ddr(u32 epm_sign,u32 cpu_id)
{
	if(epm_debug) utils_ipc_trace(LOG_ERR, 0xdd0a, "__unlock_epm_ddr send ipc to cpu4\n");
	set_clr_ddr_set_done(true,cpu_id,epm_sign);
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_EPM_UNLOCK, cpu_id , epm_sign);
}
#endif
#endif

fast_code void __get_program_and_erase_fail_count()
{
	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_GET_PROGRAM_AND_ERASE_FAIL_COUNT, 0, 0);
	cpu_msg_sync_end();
}

ddr_code void __get_avg_erase_cnt(u32 *avg_erase, u32 *max_erase, u32 *min_erase, u32 *total_ec)
{
	ec_ipc_hdl_t *ptr = (ec_ipc_hdl_t *)sys_malloc(SLOW_DATA, sizeof(ec_ipc_hdl_t));
	sys_assert(ptr != NULL);

	ptr->avg_erase = avg_erase;
	ptr->max_erase = max_erase;
	ptr->min_erase = min_erase;
	ptr->total_ec = total_ec;

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GET_EC_COUNT, 0, (u32)ptr);
	cpu_msg_sync_end();

	sys_free(SLOW_DATA, (void *)ptr);
}

fast_code void __get_nand_byte_written()
{
	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_GET_NAND_BYTE_WRITTEN, 0, 0);
	cpu_msg_sync_end();
}

fast_code void  __eccu_dufmt_switch(enum cmf_idx_t idx)
{
	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_SWITCH_DU_FMT, 0, (u32)idx);
	cpu_msg_sync_end();
}

fast_code void __eccu_switch_cmf(enum cmf_idx_t idx)
{
	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_SWITCH_CMF, 0, (u32)idx);
	cpu_msg_sync_end();
}

extern volatile u8 eccu_during_change;
ddr_code void eccu_switch_setting(u8 lbaf)
{
	eccu_during_change = true;
	switch(lbaf)
	{
		case 0 : //512
			eccu_dufmt_switch(1);
			eccu_switch_cmf(1);
			eccu_dufmt_switch(0);
			eccu_switch_cmf(0);
			break;
		case 1 : //512+8
			eccu_dufmt_switch(3);
			eccu_switch_cmf(3);
			break;
		case 2 : //4096
			eccu_dufmt_switch(2);
			eccu_switch_cmf(2);
			eccu_dufmt_switch(0);
			eccu_switch_cmf(0);
			break;
		case 3 : //4096+8
			eccu_dufmt_switch(4);
			eccu_switch_cmf(4);
			break;
		default :
			panic("imp");
			break;
	}
	eccu_during_change = false;

}


#ifdef FWcfg_Rebuild		//20201008-Eddie
fast_code void __FW_CONFIG_Rebuild(fw_config_set_t *fw_config)
{
	utils_ipc_trace(LOG_ERR, 0x3f42, "__FW_CONFIG_Rebuild send ipc to cpu2\n");
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_FW_CONFIG_Rebuild, 0, (u32)fw_config);
}

#endif

slow_code_ex bool __erase_srb(void)		//20201014-Eddie
{
	utils_ipc_trace(LOG_ERR, 0x5a8b, "__erase_srb send ipc to cpu2\n");
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_SRB_ERASE, 0, 0);
	return false;
}

fast_code void __nvmet_evt_aer_in(u32 type_sts, u32 param)
{
	//cpu_msg_sync_start();
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_AER, param, type_sts);
	//cpu_msg_sync_end();
}
//Andy_Crypto
///Andy AES crypto
#if 0//defined(USE_CRYPTO_HW)
fast_code void __crypto_change_mode_range(u8 crypto_type, u8 NS_ID, u8 change_key, u8 cryptoID)
{
	crypto_select_t cry_cfg;
	void *ptr;

	memset(&cry_cfg, 0, sizeof(cry_cfg));

	//////Set value to cry_cfg and set to ptr for share memory
	cry_cfg.crypto_config = crypto_type;
	cry_cfg.change_key = change_key;
	cry_cfg.cryptID = cryptoID;
	cry_cfg.NSID = NS_ID;
	utils_ipc_trace(LOG_ERR, 0x4bab, "[Andy] val1:%d\n",(u32)(cry_cfg.crypto_config));
	utils_ipc_trace(LOG_ERR, 0xc77d, "[Andy] val2:%d\n",(u32)(cry_cfg.change_key));
	utils_ipc_trace(LOG_ERR, 0x4cf6, "[Andy] val3:%d\n",(u32)(cry_cfg.cryptID));
	utils_ipc_trace(LOG_ERR, 0xafe9, "[Andy] val4:%d\n",(u32)(cry_cfg.NSID));

	ptr = tcm_local_to_share((void *)&cry_cfg);

	utils_ipc_trace(LOG_ERR, 0x5159, "[Andy] crypt:%x\n",*(u32 *)ptr);
	utils_ipc_trace(LOG_ERR, 0x4181, "[Andy] ptr2:%x\n",ptr);
	//cpu_msg_sync_start();
	cpu_msg_issue(0, CPU_MSG_crypto, 0, (u32)ptr);
	utils_ipc_trace(LOG_ERR, 0x2862, "[Andy] pchange end");
	//cpu_msg_sync_end();
}
fast_code void __crypto_AES_EPM_Read(u8 cryptID, u8 mode)
{
	crypto_update_t update_cfg;

	void *ptr;
	memset(&update_cfg, 0, sizeof(update_cfg));

	update_cfg.crypto_entry = cryptID;
	update_cfg.mode = mode;

	ptr = tcm_local_to_share((void *)&update_cfg);

	cpu_msg_issue(0, CPU_MSG_Loadcrypto, 0, (u32)ptr);
}
#endif

fast_code void __ncl_cmd_rapid_single_du_read(struct ncl_cmd_t *ncl_cmd)
{
	bool sync = (ncl_cmd->flags & NCL_CMD_SYNC_FLAG) ? true : false;
	bool is_shared = false;
	u32 rx = CPU_BE - 1;

#if defined(DUAL_BE)
	pda_t pda = ncl_cmd->addr_param.rapid_du_param.pda;
	u32 grp_id = (pda >> shr_nand_info.pda_ch_shift) & (shr_nand_info.geo.nr_channels - 1);

	if (grp_id >= 4)
		rx = CPU_BE_LITE - 1;
#endif

	if (!is_ptr_sharable((void *) ncl_cmd)) {
		ncl_cmd = make_rapid_ncmd_sharable(ncl_cmd);
		is_shared = true;
	}

	if (sync) {
		cpu_msg_sync_start();
		cpu_msg_issue(rx, CPU_MSG_RAPID_NCMD, 0, (u32) ncl_cmd);
		cpu_msg_sync_end();
		if (is_shared)
			ncl_cmd = make_rapid_ncmd_local(ncl_cmd);
	} else {
		cpu_msg_issue(rx, CPU_MSG_RAPID_NCMD, 0, (u32) ncl_cmd);
	}
}

fast_code void __ncl_cmd_wait_completion(void)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_WAIT_NCL_IDLE, 0, 0);
#if defined(DUAL_BE)
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_WAIT_NCL_IDLE, 0, 0);
	while(!IS_NCL_IDLE()) {
		// keep waiting;
	}
#endif
}

ddr_code void __ncl_cmd_block(void)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_NCL_CMD_BLOCK, 0, 0);
#if defined(DUAL_BE)
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCL_CMD_BLOCK, 0, 0);
#endif
}

fast_code bool __ncl_cmd_empty(bool rw)
{
	u32 ret0;
	void *ptr = tcm_local_to_share((void *) &ret0);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_NCL_CMD_EMPTY, rw, (u32)ptr);
	cpu_msg_sync_end();
#if defined(DUAL_BE)
	u32 ret1;
	ptr = tcm_local_to_share((void *) &ret1);
	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCL_CMD_EMPTY, rw, (u32)ptr);
	cpu_msg_sync_end();
	ret0 &= ret1;
#endif
	return ret0;
}

fast_code void __ncl_handle_pending_cmd(void)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_NCL_HANDLE_PENDING_CMD, 0, 0);
#if defined(DUAL_BE)
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCL_HANDLE_PENDING_CMD, 0, 0);
#endif
}

fast_code void __ftl_core_erase(erase_ctx_t *ctx)
{
	if (!is_ptr_sharable((void *)ctx))
		ctx = (erase_ctx_t *) tcm_local_to_share((void *) ctx);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_SCHEDULE_ERASE, 0, (u32) ctx);
}
fast_code void __ftl_l2p_partial_reset(u32 ns_sec_id)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_L2P_RESET, 0, (u32)ns_sec_id);
}
fast_code void __ftl_gc_reset(void)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GC_RESET, 0, 0);
}

ddr_code void __ftl_core_reset_notify(void)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_PERST_FTL_NOTIFY, 0, 0);
}

ddr_code void __plp_cancel_die_que(void)
{
	//cpu_msg_issue(CPU_BE - 1, CPU_MSG_PLP_CANCEL_DIEQUE, 0, 0);
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_PLP_CANCEL_DIEQUE, 0, 0);
}

ddr_code void __ftl_reset_notify(void)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_PERST_FTL_NOTIFY, 0, 0);
}

ddr_code void __ftl_core_reset_done_notify(void)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_PERST_DONE_FTL_NOTIFY, 0, 0);
}

ddr_code void __ftl_reset_done_notify(void)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_PERST_DONE_FTL_NOTIFY, 0, 0);
}

fast_code void __ftl_flush(flush_ctx_t *ctx)
{
	if (!is_ptr_sharable((void *) ctx))
		ctx = (flush_ctx_t *) tcm_local_to_share((void *) ctx);

	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FTL_FLUSH, 0, (u32) ctx);
}

#if(WARMBOOT_FTL_HANDLE == mENABLE)
fast_code void __ftl_warm_boot_handle(void)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_WARM_BOOT_FTL_RESET, 0, 0);
}
#endif
#if 0
slow_code void __ftl_full_trim(void *req)
{
    cpu_msg_sync_start();
    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FTL_FULL_PART_TRIM, 0, (u32) req);
    cpu_msg_sync_end();
}
#endif
fast_code void __ftl_format(format_ctx_t *ctx)
{
	if (!is_ptr_sharable((void *) ctx))
		ctx = (format_ctx_t *) tcm_local_to_share((void *) ctx);

	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FTL_FORMAT, 0, (u32) ctx);
}

fast_code void __read_done_rc_entry(struct ncl_cmd_t *ncl_cmd)
{
	void *ptr = (void*)ncl_cmd;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

    cpu_msg_issue(CPU_BE - 1, CPU_MSG_READ_DONE_RC_ENTRY, 0, (u32)ptr);
}

fast_code void __raid_correct_push(rc_req_t *req)
{
	sys_assert((u32)req >= SRAM_BASE);
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_RAID_CORRECT_PUSH, 0, (u32)req);
}

fast_code void __bm_data_copy(u64 src, u64 dst, u32 nbytes, dpe_cb_t cb, void *ctx)
{
	bm_copy_ipc_hdl_t *hdl = sys_malloc(FAST_DATA, sizeof(bm_copy_ipc_hdl_t));
	sys_assert(hdl);

	hdl->src = src;
	hdl->dst = dst;
	hdl->len = nbytes;
	hdl->cb = cb;
	hdl->ctx = ctx;

	void *ptr = tcm_local_to_share((void *)hdl);
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_BM_COPY, 0, (u32)ptr);
}

fast_code void ipc_bm_copy_done(volatile cpu_msg_req_t *req)
{
	bm_copy_ipc_hdl_t *hdl = (bm_copy_ipc_hdl_t*)req->pl;

	hdl = tcm_share_to_local((void *)hdl);
	hdl->cb(hdl->ctx, NULL);

	sys_free(FAST_DATA, hdl);
}

fast_code void __bm_sha3_sm3_calc_part(void *mem, void *result, bool sm3,
		u32 count, u32 cur_count, dpe_cb_t callback, void *ctx, bool first)
{
	sha3_sm3_calc_ipc_hdl_t *ptr = (sha3_sm3_calc_ipc_hdl_t *)sys_malloc(SLOW_DATA, sizeof(sha3_sm3_calc_ipc_hdl_t));
	sys_assert(ptr != NULL);

	ptr->mem = mem;
	ptr->result = result;
	ptr->sm3 = sm3;
	ptr->count = count;
	ptr->cur_count = cur_count;
	ptr->callback = callback;
	ptr->ctx = ctx;
	ptr->first = first;

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_SHA3_SM3_CALC, 0, (u32)ptr);
	cpu_msg_sync_end();
	if (count != cur_count)
		sys_free(SLOW_DATA, (void *)ptr);
}

fast_code void ipc_bm_sha3_sm3_calc_done(volatile cpu_msg_req_t *req)
{
	sha3_sm3_calc_ipc_hdl_t *ptr = (sha3_sm3_calc_ipc_hdl_t *) req->pl;

	if (ptr->callback)
		ptr->callback((void *)ptr, NULL);

	sys_free(SLOW_DATA, (void *)ptr);
}

fast_code void wait_btn_rst_handler(u32 p0, u32 p1, u32 p2)
{
	l2p_idle_ctrl_t ctrl;
	bool btn_reset = p2;
	UNUSED dtag_t dtag;
	UNUSED u32 dtag_cnt;
	sys_assert(cpu_init_bmp[CPU_ID_0].b.wait_btn_rst == 0);

#if (CPU_ID_0 + 1) < MPC
	// issue to next CPU
	if (p1 == 0) {
		cpu_msg_issue(CPU_ID_0 + 1, CPU_MSG_WAIT_BTN_RESET, 0, btn_reset);
	}

	if (cpu_init_bmp[CPU_ID_0 + 1].b.wait_btn_rst == 0) {
		p1 = (p1 == ~0) ? 1 : p1 + 1;
		evt_set_cs(evt_wait_btn_rst, p1, p2, CS_TASK);
		return;
	}

#endif
	utils_apl_trace(LOG_WARNING, 0xdae3, "cpu %d locked", CPU_ID);

	ctrl.all = 0;
	ctrl.b.all = 1;
	ctrl.b.wait = 1;
	wait_l2p_idle(ctrl);
#if CPU_ID == CPU_FTL
	if (btn_reset) {
		extern dtag_t ftl_l2p_get_vcnt_buf(u32 *cnt, void **buf);

		dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, NULL);
	}
#endif

	cpu_init_bmp[CPU_ID_0].b.wait_btn_rst = true;
	if (btn_reset) {
		wait_remote_item_done_no_poll(btn_rst);
	} else {
		wait_remote_item_done(btn_rst);
	}

	if (btn_reset) {
		l2p_mgr_reset_resume();
		extern __attribute__((weak)) void btn_reset_resume(void);
		if (btn_reset_resume)
		{
			//utils_apl_trace(LOG_WARNING, 0, "cpu %d ", CPU_ID);
			btn_reset_resume();
		}

#if CPU_ID == CPU_FTL
		extern void ftl_l2p_put_vcnt_buf(dtag_t dtag, u32 cnt, bool restore);
		ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, true);
#endif
	}

}

fast_code void ipc_wait_btn_rst(volatile cpu_msg_req_t *req)
{
	evt_set_cs(evt_wait_btn_rst, 0, req->pl, CS_TASK);
}

fast_code ftl_err_t __ftl_core_flush(ftl_flush_data_t *fctx)
{
	void *ptr = tcm_local_to_share((void *) fctx);

	#if 0//PLP_TEST == 1
	u64 curr = get_tsc_64();
    extern volatile u8 plp_trigger;
    if(plp_trigger)
        utils_ipc_trace(LOG_ALW, 0x599e, "cpu1->cpu2 flush msg:0x%x-%x", curr>>32, curr&0xFFFFFFFF);
    #endif
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FLUSH_DATA, 0, (u32) ptr);
	return FTL_ERR_BUSY;
}

fast_code void __fcore_idle_flush(void)
{
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_FORCE_FLUSH, 0, 0);
}


fast_code void __ftl_core_format(ftl_format_t *format)
{
	void *ptr = tcm_local_to_share((void *) format);
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FORMAT, 0, (u32) ptr);
}

fast_code bool __ftl_core_spb_pad(ftl_spb_pad_t *pctx)
{
	void *ptr = (void *)pctx;
	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);
	utils_ipc_trace(LOG_INFO, 0x6146, "cpu3 send flush msg: cpu3->cpu2");
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_SPB_PAD, 0, (u32)ptr);
	return false;
}

fast_code bool __ftl_core_flush_tbl(ftl_flush_tbl_t *tbl)
{
	void *ptr = (void *) tbl;
	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FLUSH_TABLE, 0, (u32) ptr);
	return false;
}

fast_code void __ftl_core_start(u32 nsid)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_NS_START, 0, nsid);
}

fast_code void __ftl_core_qbt_alloc(ns_start_t *ctx)
{
	void *ptr = (void *) ctx;
	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_OPEN_QBT, 0, (u32) ptr);
}
fast_code void __pmu_swap_file_register(void *loc, u32 size)
{
	swap_mem_t range;
	void *ptr;

	range.loc = loc;
	range.size = size;
	ptr = tcm_local_to_share((void *) &range);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_PMU_SWAP_REG, 0, (u32)ptr);
	cpu_msg_sync_end();
}

fast_code void __srb_load_mr_defect(u8 *ftl_bitmap, u32 bbt_width)
{
	srb_load_mr_defect_ipc_hdl_t hdl;
	void *ptr;

	hdl.bbt_width = bbt_width;
	hdl.bmp = ftl_bitmap;

	ptr = tcm_local_to_share((void *) &hdl);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_SRB_READ_DEFECT, 0, (u32)ptr);
	cpu_msg_sync_end();
}

fast_code void __ncl_spb_defect_scan(u32 spb, u32 *defect)
{
	ncl_scan_defect_ipc_hdl_t hdl;
	u32 *temp;
	u32 temp_sz;
	void *ptr;
	u32 half_interleave;
	u32 half_dws;

	temp_sz = sizeof(u32) * occupied_by(shr_nand_info.interleave, 32);
	temp = sys_malloc(SLOW_DATA, temp_sz);
	sys_assert(temp);

	hdl.spb_id = spb;
	memset(temp, 0, temp_sz);
	hdl.defect = temp;

	ptr = tcm_local_to_share((void *)&hdl);
	// assume 2 ncb has the same layout
	half_interleave = shr_nand_info.interleave;

	half_dws = occupied_by(half_interleave, 32);
	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_DEFECT_SCAN, 0, (u32)ptr);
	cpu_msg_sync_end();

	memcpy(defect, temp, half_dws * sizeof(u32));

	sys_free(SLOW_DATA, temp);
}

fast_code void __rawdisk_4k_read(int btag)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_RAWDISK_4K_READ, 0, (u32)btag);
}

fast_code void __bm_err_commit(u32 du_ofst, u32 btag)
{
	bm_pl_t bm_pl;
	void *ptr;

	bm_pl.pl.du_ofst = du_ofst;
	bm_pl.pl.btag = btag;

	ptr = tcm_local_to_share((void *) &bm_pl);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_RD_ERR, 0, (u32)ptr);
	cpu_msg_sync_end();
}

fast_code void __ipc_read_recoveried_commit(bm_pl_t *bm_pl, u16 pdu_bmp)
{
    #ifdef RETRY_COMMIT_EVENT_TRIGGER
    cpu_msg_issue(CPU_FE - 1, CPU_MSG_READ_RECOVERIED_COMMIT, pdu_bmp, (u32)bm_pl);

    #if 1 //def NCL_FW_RETRY_EX
    u8 ch_id = 0;
    //ch_id = ((bm_pl->pl.dtag & DDTAG_MASK) - DDR_RTY_RECOVERY_EX_START) / RTY_DTAG_CNT_PER_CH;
    ch_id = ((bm_pl->pl.dtag & DDTAG_MASK) - DDR_RD_RECOVERY_EX_START) / RTY_DTAG_CNT_PER_CH;
    #if(CPU_ID == CPU_BE_LITE)
    sys_assert(ch_id >= RTY_QUEUE_CH_CNT);
    ch_id -= 4;
    #endif
    utils_ipc_trace(LOG_INFO, 0x6a9c, "ipc Rcvr cmit issue return, dtag[0x%x] ch_id[%d] wunc[%d]", (bm_pl->pl.dtag & DDTAG_MASK), ch_id, pdu_bmp);
    //hst_rd_err_cmpl(ch_id);
    #endif

    //utils_ipc_trace(LOG_INFO, 0, "ipc Rcvr cmit issue return, dtag: 0x%x, wunc: %d",(bm_pl->pl.dtag & 0x3FFFFF), pdu_bmp);

    return;
    #else
	if (!is_ptr_sharable(bm_pl))
        bm_pl = tcm_local_to_share(bm_pl);

    cpu_msg_sync_start();
    cpu_msg_issue(CPU_FE - 1, CPU_MSG_READ_RECOVERIED_COMMIT, pdu_bmp, (u32)bm_pl);
    cpu_msg_sync_end();
    #endif
}

fast_code void __btn_semi_wait_switch(bool wait)
{
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_BTN_SEMI_WAIT_SWITCH, 0, (u32) wait);
}

#ifdef CPU_FTL
fast_code bool __ftl_open(u32 nsid)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_NS_OPEN, 0, nsid);
	return false;
}
#endif



fast_code bool __spb_release(spb_id_t spb_id)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_RELEASE_SPB, 0, spb_id);
	return false;
}

fast_code bool __pbt_release(spb_id_t spb_id)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_RELEASE_PBT_SPB, 0, spb_id);
	return false;
}

fast_code void __ftl_core_update_fence(ftl_fence_t *fence)
{
	void *ptr = (void *) fence;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_UPDATE_FENCE, 0, (u32) ptr);
	cpu_msg_sync_end();
}

fast_code void __ftl_core_restore_fence(ftl_fence_t *fence)
{
	void *ptr = (void *) fence;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_RESTORE_FENCE, 0, (u32) ptr);
	cpu_msg_sync_end();
}

fast_code bool __ftl_core_flush_misc(ftl_flush_misc_t *flush_misc)
{
	void *ptr = (void *) flush_misc;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FLUSH_MISC, 0, (u32) ptr);
	return false;
}

fast_code bool __ftl_core_flush_blklist(ftl_flush_misc_t *flush_misc)
{
	void *ptr = (void *) flush_misc;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FLUSH_BLKLIST_HANDLING, 0, (u32) ptr);
	return false;
}

fast_code void __pstream_force_close(u16 close_blk)	// ISU, Tx, PgFalClsNotDone(1)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FORCE_CLOSE_PSTREAM, 0, close_blk);
}


fast_code void __ftl_core_restore_rt_flags(spb_rt_flags_t *spb_rt_flags)
{
	void *ptr = (void *) spb_rt_flags;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_RESTORE_RT_FLAGS, 0, (u32) ptr);
	cpu_msg_sync_end();
}

fast_code void __ftl_core_gc_start(gc_req_t *gc_req)
{
	void *ptr = (void*)gc_req;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_GC_START, 0, (u32)ptr);
}

fast_code void __tzu_get_gc_info()
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_TZU_GET, 0, 0);
}

fast_code void __spb_clear_ec()
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_CLEAN_EC_TBL, 0, 0);
}
fast_code void __ftl_core_p2l_load(p2l_load_req_t *load_req)
{
	void *ptr = (void *)load_req;
	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_LOAD_P2L, 0, (u32) ptr);
	cpu_msg_sync_end();
}

fast_code void ipc_ncl_rapid_cmd_submit_done(volatile cpu_msg_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *) req->pl;

	if (is_ptr_tcm_share(ncl_cmd)) {
		ncl_cmd = make_rapid_ncmd_local(ncl_cmd);
	}

	ncl_cmd->completion(ncl_cmd);
}

fast_code void ipc_scheudle_erase_done(volatile cpu_msg_req_t *req)
{
	erase_ctx_t *ctx = (erase_ctx_t *) req->pl;

	if (is_ptr_tcm_share((void *) ctx))
		ctx = tcm_share_to_local((void *) ctx);

	ctx->cmpl(ctx);
}

fast_code void ipc_be_power_loss(volatile cpu_msg_req_t *req)
{
	utils_ipc_trace(LOG_ERR, 0x134f, "\n ncb: %d\n", jiffies);
	utils_ipc_trace(LOG_ERR, 0x6e6e, "\n\033[91m%s\x1b[0m\n", __func__);
}

fast_code void ipc_nvmet_aer_event_trigger(volatile cpu_msg_req_t *req)
{
	//u8 tx = req->cmd.tx;
	utils_ipc_trace(LOG_INFO, 0x9666, "IPC AER EVENT IN");
	nvmet_evt_aer_in(req->pl, req->cmd.flags);
	//cpu_msg_sync_done(tx);
}
//Andy_Crypto
#if 0//defined(USE_CRYPTO_HW)
ddr_code void ipc_change_crypto(volatile cpu_msg_req_t *req)
{
	crypto_select_t *cfg = (crypto_select_t *) req->pl;

	utils_ipc_trace(LOG_ERR, 0xe0a5, "Andy IPC change");

	utils_ipc_trace(LOG_ERR, 0xa591, "cfg:%d\n",cfg->crypto_config);
	utils_ipc_trace(LOG_ERR, 0xb9bd, "ID:%d\n",cfg->NSID);
	utils_ipc_trace(LOG_ERR, 0xbe96, "change key:%d\n",cfg->change_key);
	utils_ipc_trace(LOG_ERR, 0x5cb8, "key size:%\nd",cfg->key_size);
	crypto_change_mode_range(cfg->crypto_config, cfg->NSID, cfg->change_key, cfg->cryptID);

	//cpu_msg_sync_done(req->cmd.tx);
}
ddr_code void ipc_Load_crypto(volatile cpu_msg_req_t *req)
{

	crypto_update_t *cfg = (crypto_update_t *) req->pl;

	crypto_AES_EPM_Read(cfg->crypto_entry,cfg->mode);
	//cpu_msg_sync_done(req->cmd.tx);
}
#endif

fast_code void ipc_ncl_cmd_submit_done(volatile cpu_msg_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *) req->pl;

	ncl_cmd = make_ncmd_local(ncl_cmd);
    //utils_ipc_trace(LOG_INFO, 0, " pda 0x%x", ncl_cmd->addr_param.common_param.pda_list[0]);
	ncl_cmd->completion(ncl_cmd);
}

fast_code void ipc_ftl_ns_flush_done(volatile cpu_msg_req_t *req)
{
	flush_ctx_t *ctx = (flush_ctx_t *) req->pl;

	if (is_ptr_tcm_share((void *) ctx))
		ctx = (flush_ctx_t *) tcm_share_to_local((void *) ctx);

	utils_ipc_trace(LOG_ERR, 0x0fe4, "ipc_ftl_ns_flush_done");
	ctx->cmpl(ctx);
}

#if CPU_BE == CPU_ID || CPU_BE_LITE == CPU_ID
fast_code void ipc_srb_read_defect(volatile cpu_msg_req_t *req)
{
	srb_load_mr_defect_ipc_hdl_t *hdl = (srb_load_mr_defect_ipc_hdl_t*) req->pl;
	u8 tx = req->cmd.tx;

	srb_load_mr_defect(hdl->bmp, hdl->bbt_width);
	cpu_msg_sync_done(tx);
}
#endif

#if CPU_BE == CPU_ID || CPU_BE2 == CPU_ID || CPU_BE_LITE == CPU_ID
fast_code void ipc_wait_ncl_idle(volatile cpu_msg_req_t *req)
{
	ncl_cmd_wait_completion();
}

ddr_code void ipc_ncl_cmd_block(volatile cpu_msg_req_t *req)
{
	ncl_cmd_block();
}

fast_code void ipc_ncl_handle_cmd(volatile cpu_msg_req_t *req)
{
	ncl_handle_pending_cmd();
}

fast_code void ipc_ncmd_exec_done(struct ncl_cmd_t *ncl_cmd)
{
	ncl_cmd_ipc_hdl_t *hdl;
	u8 tx;
	bool sync = false;

	hdl = ncl_cmd->caller_priv;
	tx = hdl->tx;
	if (hdl->sync) {
		ncl_cmd->flags |= NCL_CMD_SYNC_FLAG;
		sync = true;
	}

    if(ncl_cmd->flags & NCL_CMD_SCH_FLAG)
    {
        decrease_otf_cnt(ncl_cmd->die_id);
        ncl_cmd->flags &= ~NCL_CMD_SCH_FLAG;
    }

	ncl_cmd->caller_priv = hdl->caller;
	ncl_cmd->completion = hdl->completion;
     //utils_ipc_trace(LOG_INFO, 0, "to cpu%d pda 0x%x",tx+1, ncl_cmd->addr_param.common_param.pda_list[0]);
	sys_free(FAST_DATA, hdl);
	if (sync)
		cpu_msg_sync_done(tx);
	else
		cpu_msg_issue(tx, CPU_MSG_NCMD_DONE, 0, (u32) ncl_cmd);
}

fast_code void ipc_rapid_ncmd_exec_done(struct ncl_cmd_t *ncl_cmd)
{
	ncl_cmd_ipc_hdl_t *hdl;
	u8 tx;
	bool sync = false;

	hdl = ncl_cmd->caller_priv;
	tx = hdl->tx;
	if (hdl->sync) {
		ncl_cmd->flags |= NCL_CMD_SYNC_FLAG;
		sync = true;
	}

	ncl_cmd->caller_priv = hdl->caller;
	ncl_cmd->completion = hdl->completion;

	sys_free(FAST_DATA, hdl);
	if (sync)
		cpu_msg_sync_done(tx);
	else
		cpu_msg_issue(tx, CPU_MSG_RAPID_NCMD_DONE, 0, (u32) ncl_cmd);
}

//fast_code void ipc_ncl_cmd_empty(volatile cpu_msg_req_t *req)
//{
//	void *result = (void *) req->pl;
//	bool ret = ncl_cmd_empty();
//
//	writel((u32)ret, result);
//	cpu_msg_sync_done(req->cmd.tx);
//}

fast_code void ipc_ncl_cmd_empty(volatile cpu_msg_req_t *req)
{
	void *result = (void *) req->pl;
	bool rw = req->cmd.flags;
	//utils_ipc_trace(LOG_ERR, 0, "rw:%d", rw);
	bool ret = ncl_cmd_empty(rw);

	writel((u32)ret, result);
	__dmb();
	cpu_msg_sync_done(req->cmd.tx);
}

fast_code void ipc_ncmd_exec(volatile cpu_msg_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *) req->pl;
	ncl_cmd_ipc_hdl_t *hdl;

	// try to avoid this
	hdl = sys_malloc(FAST_DATA, sizeof(ncl_cmd_ipc_hdl_t));
	sys_assert(hdl);

	hdl->caller = ncl_cmd->caller_priv;
	hdl->completion = ncl_cmd->completion;
	hdl->tx = req->cmd.tx;

	if (ncl_cmd->flags & NCL_CMD_SYNC_FLAG)
		hdl->sync = true;
	else
		hdl->sync = false;

	ncl_cmd->flags &= ~NCL_CMD_SYNC_FLAG;

	ncl_cmd->caller_priv = hdl;
	ncl_cmd->completion = ipc_ncmd_exec_done;
	ncl_cmd_submit(ncl_cmd);
}


fast_code void ipc_rd_err_handling(volatile cpu_msg_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *) req->pl;
	ncl_cmd_ipc_hdl_t *hdl;

	// try to avoid this
	hdl = sys_malloc(FAST_DATA, sizeof(ncl_cmd_ipc_hdl_t));
	sys_assert(hdl);

	hdl->caller = ncl_cmd->caller_priv;
	hdl->completion = ncl_cmd->completion;
	hdl->tx = req->cmd.tx;

	if (ncl_cmd->flags & NCL_CMD_SYNC_FLAG)
		hdl->sync = true;
	else
		hdl->sync = false;

	ncl_cmd->flags &= ~NCL_CMD_SYNC_FLAG;

	ncl_cmd->caller_priv = hdl;
	ncl_cmd->completion = ipc_ncmd_exec_done;
	rd_err_handling(ncl_cmd);
}
fast_code void ipc_ncmd_exec_insert_sch(volatile cpu_msg_req_t *req)
{
    struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *) req->pl;
	ncl_cmd_ipc_hdl_t *hdl;

	// try to avoid this
	hdl = sys_malloc(FAST_DATA, sizeof(ncl_cmd_ipc_hdl_t));
	sys_assert(hdl);

	hdl->caller = ncl_cmd->caller_priv;
	hdl->completion = ncl_cmd->completion;
	hdl->tx = req->cmd.tx;

	if (ncl_cmd->flags & NCL_CMD_SYNC_FLAG)
		hdl->sync = true;
	else
		hdl->sync = false;

	ncl_cmd->flags &= ~NCL_CMD_SYNC_FLAG;

	ncl_cmd->caller_priv = hdl;
	ncl_cmd->completion = ipc_ncmd_exec_done;

    ncl_cmd_submit_insert_schedule(ncl_cmd,false);
}

ddr_code void ipc_journal_update(volatile cpu_msg_req_t *req)
{
	u32 evt_reason_id = req->cmd.flags;
	u32 use_0 = req->pl;
	journal_update(evt_reason_id,use_0);
}

#if epm_enable
fast_code void ipc_epm_update(volatile cpu_msg_req_t *req)
{
	u32 epm_sign = req->pl;
	u32 cpu_id = req->cmd.flags;
	//u64 end, start=get_tsc_64();
	//utils_ipc_trace(LOG_ALW, 0, "update req:0x%x-%x", start>>32, start&0xFFFFFFFF);
	epm_update(epm_sign,cpu_id);
	//end = get_tsc_64();
	//utils_ipc_trace(LOG_ALW, 0, "update done:0x%x-%x", end>>32, end&0xFFFFFFFF);
}

#if FRB_remap_enable
fast_code void ipc_epm_remap_tbl_update(volatile cpu_msg_req_t *req)
{
	pda_t pda_base = req->pl;
	epm_remap_tbl_flush(&pda_base);
}
#endif

#if epm_spin_lock_enable
fast_code void ipc_epm_get_key(volatile cpu_msg_req_t *req)
{
	u32 epm_sign = req->pl;
	u32 cpu_id = req->cmd.flags;
	if(epm_debug) utils_ipc_trace(LOG_ERR, 0xc2a9, "ipc_epm_get_key cpu_id=%d epm_sign=%d\n",cpu_id,epm_sign);
	get_epm_access_key(cpu_id, epm_sign);
}
fast_code void ipc_epm_unlock(volatile cpu_msg_req_t *req)
{
	u32 epm_sign = req->pl;
	u32 cpu_id = req->cmd.flags;
	if(epm_debug) utils_ipc_trace(LOG_ERR, 0xf227, "ipc_epm_unlock epm_sign=%d\n",epm_sign);
	unlock_epm_ddr(epm_sign,cpu_id);
}
#endif
#endif

fast_code void ipc_get_program_and_erase_fail_count(volatile cpu_msg_req_t *req)
{
	// u32 *program_fail_count = (u32 *)req->pl;
	get_program_and_erase_fail_count();
	u32 *buf = (u32 *) req->pl;
	
	#if (Xfusion_case)
		cpu_msg_issue(req->cmd.tx, CPU_MSG_GET_INTEL_SMART_INFO_DONE, 0, (u32)buf);
	#else
		cpu_msg_issue(req->cmd.tx, CPU_MSG_GET_ADDITIONAL_SMART_INFO_DONE, 0, (u32)buf);
	#endif
	//cpu_msg_sync_done(req->cmd.tx);
}
fast_code void ipc_get_nand_byte_written(volatile cpu_msg_req_t *req)
{
	// u32 *program_fail_count = (u32 *)req->pl;
	get_nand_byte_written();
	cpu_msg_sync_done(req->cmd.tx);
}

fast_code void ipc_eccu_dufmt_switch(volatile cpu_msg_req_t *req)
{
	eccu_dufmt_switch(req->pl);
	cpu_msg_sync_done(req->cmd.tx);
}

fast_code void ipc_eccu_switch_cmf(volatile cpu_msg_req_t *req)
{
	eccu_switch_cmf(req->pl);
	cpu_msg_sync_done(req->cmd.tx);
}

#ifdef FWcfg_Rebuild	//20201008-Eddie
fast_code void ipc_FW_CONFIG_Rebuild(volatile cpu_msg_req_t *req)
{
	fw_config_set_t *fw_config = (fw_config_set_t *)req->pl;
	//utils_ipc_trace(LOG_ERR, 0, "ipc_epm_update epm_sign=%d\n",epm_sign);
	FW_CONFIG_Rebuild(fw_config);
}
#endif

fast_code void ipc_erase_srb(volatile cpu_msg_req_t *req)	//20201014-Eddie
{
	erase_srb();
}

#if CPU_ID == CPU_BE_LITE
fast_code void ipc_SystemInfo_update(volatile cpu_msg_req_t *req)
{
	SysInfo_update();
}
#endif

fast_code void ipc_rapid_ncmd_exec(volatile cpu_msg_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *) req->pl;
	ncl_cmd_ipc_hdl_t *hdl;

	// try to avoid this
	hdl = sys_malloc(FAST_DATA, sizeof(ncl_cmd_ipc_hdl_t));
	sys_assert(hdl);

	hdl->caller = ncl_cmd->caller_priv;
	hdl->completion = ncl_cmd->completion;
	hdl->tx = req->cmd.tx;

	if (ncl_cmd->flags & NCL_CMD_SYNC_FLAG)
		hdl->sync = true;
	else
		hdl->sync = false;

	ncl_cmd->flags &= ~NCL_CMD_SYNC_FLAG;

	ncl_cmd->caller_priv = hdl;
	ncl_cmd->completion = ipc_rapid_ncmd_exec_done;
	ncl_cmd->status = 0;
	ncl_cmd_rapid_single_du_read(ncl_cmd);
}


fast_code void ipc_defect_scan(volatile cpu_msg_req_t *req)
{
	ncl_scan_defect_ipc_hdl_t *hdl = (ncl_scan_defect_ipc_hdl_t *) req->pl;

	ncl_spb_defect_scan(hdl->spb_id, hdl->defect);

	cpu_msg_sync_done(req->cmd.tx);
}
#endif

slow_code void ipc_log_level_chg(volatile cpu_msg_req_t *req)
{
	log_level_t lvl = (log_level_t) req->pl;
	log_level_chg(lvl);
}

fast_code void ipc_sirq_ctrl(volatile cpu_msg_req_t *req)
{
	if (req->cmd.flags)
		misc_sys_isr_enable(req->pl);
	else
		misc_sys_isr_disable(req->pl);
}

fast_code void ipc_heap_alloc(volatile cpu_msg_req_t *req)
{
	volatile u32 *sz = (u32 *)req->pl;
	u32 _sz = sz[0];
	void *ret;

	ret = sys_malloc(SLOW_DATA, _sz);
	sz[8] = (u32) ret;
	cpu_msg_sync_done(req->cmd.tx);
	__dmb();
}

#if defined(RDISK)
fast_code void ipc_api_spb_ack(u32 nsid, u32 type)
{
	nsid_type_t pl;

	pl.b.nsid = nsid;
	pl.b.type = type;
	// only FTL to scheduler
	sys_assert(CPU_ID == CPU_FTL);
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_SPB_ACK, 0, pl.all);
}

fast_code void ipc_api_spb_query(u32 nsid, u32 type)
{
	nsid_type_t pl;
	// only scheduler to FTL
	sys_assert(CPU_ID == CPU_BE);
	pl.b.nsid = nsid;
	pl.b.type = type;
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SPB_QUERY, 0, pl.all);
}

fast_code void ipc_api_pstream_get_open_ack(u16 blk_idx)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_PSTREAM_ACK, 0, blk_idx);
}

fast_code void ipc_api_flush_blklist(u32 nsid, u32 type)
{
	nsid_type_t pl;
	// only scheduler to FTL
	sys_assert(CPU_ID == CPU_BE);
	pl.b.nsid = nsid;
	pl.b.type = type;
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FLUSH_BLKLIST, 0, pl.all);
}

fast_code void ipc_api_free_blklist_dtag(u32 type)
{
	// only scheduler to FTL
	sys_assert(CPU_ID == CPU_BE);
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FREE_BLKLIST, 0, type);
}


fast_code void ipc_api_free_flush_blklist_sdtag(void)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FREE_FLUSH_BLKLIST_SDTAG, 0, 0);
}



fast_code void ipc_spb_rd_cnt_upd(u32 spb_id)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SPB_RD_CNT_UPDT, 0, spb_id);
}

fast_code void ipc_spb_rd_cnt_upd_ack(u8 rx, u32 spb_id)
{
	cpu_msg_issue(rx, CPU_MSG_SPB_RD_CNT_UPDT_ACK, 0, spb_id);
}

fast_code void ipc_api_gc_done(u32 spb_id, u32 ttl_du_cnt)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GC_DONE, spb_id, ttl_du_cnt);
}

#if (FW_BUILD_VAC_ENABLE == mENABLE)
#if (CPU_ID != CPU_FTL)
extern u32 _max_capacity;
extern volatile bool vc_recon_busy[4];
extern u32 *recon_vc;
extern volatile u8 fw_memory_not_enough;

fast_code void l2p_vac_recon(volatile cpu_msg_req_t *req)
{
#if (CPU_ID == 1)
	u32 start_lda = 0;
	u32 end_lda = aligned_down(_max_capacity / 4, 8192);
#elif (CPU_ID == 2)
	u32 start_lda = aligned_down(_max_capacity / 4, 8192);
	u32 end_lda = aligned_down(_max_capacity / 4 * 2, 8192);
#elif (CPU_ID == 4)
	u32 start_lda = aligned_down(_max_capacity / 4 * 3, 8192);
	u32 end_lda = _max_capacity;
#endif

	utils_apl_trace(LOG_ALW, 0xeb7b, "CPU:%d build start:0x%x, end:0x%x", CPU_ID, start_lda, end_lda);

	u64 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000);
	u32 pda_blk_shift = shr_nand_info.pda_block_shift;
	u32 pda_blk_mask = shr_nand_info.pda_block_mask;
	u32 spb_cnt = shr_nand_info.geo.nr_blocks;

	u32 size = spb_cnt * 4;

	u32 *l2p = NULL;
	u32 *vc  = NULL;
#if (CPU_ID != 1)
	l2p = sys_malloc_aligned(FAST_DATA, 4096, 32);
	if( l2p == NULL )
	{
		fw_memory_not_enough = CPU_ID;
		return;
	}
	vc = (u32 *)sys_malloc_aligned(FAST_DATA, size, 4);
	if( vc == NULL )
	{
		sys_free_aligned(FAST_DATA, l2p);
		fw_memory_not_enough = CPU_ID;
		return;
	}
#else//CPU1 use ucache btcm resource temporarily
	extern u32* CPU1_cache_sourece;
	l2p = (u32*)(((u32)(CPU1_cache_sourece) + 32) & (~(32-1)));
	vc  = (u32*)((u32)l2p + 4096 );
	
	utils_apl_trace(LOG_ALW, 0xd80a,"l2p:0x%x vc:0x%x CPU1_cache_sourece:0x%x",l2p,vc,CPU1_cache_sourece);
#endif
	sys_assert(l2p);
	sys_assert(vc);
	
	memset(vc, 0, size);

	u32 curr_lda = start_lda;
	u64 addr = addr_base + (u64)start_lda * 4;

	while (curr_lda < end_lda) 
	{
		sync_dpe_copy(addr, (u64)(u32)l2p, 4096);
		for (u32 i = 0; i < 1024; ++i) 
		{
			u32 lda = curr_lda + i;
			if (lda >= end_lda)
				break;

			pda_t pda = l2p[i];
			if (pda != INV_U32) 
			{
				u32 spb_id = ((pda >> pda_blk_shift) & pda_blk_mask);
				vc[spb_id]++;
			}
		}
		curr_lda += 1024;
		addr += 4096;
	}

	while (vc_recon_busy[2])
		;
	dsb();
	u32 *core3_vc = recon_vc;
	spin_lock_take(SPIN_LOCK_KEY_VC_RECON, 0, true);
	for (u32 spb_id = 0; spb_id < spb_cnt; spb_id++) 
	{
		core3_vc[spb_id] = core3_vc[spb_id] + vc[spb_id];
		//utils_apl_trace(LOG_ALW, 0xcddc, "CPU:%d spb:%d vc:%d tot:%d", CPU_ID, spb_id, vc[spb_id], core3_vc[spb_id]);
	}
	spin_lock_release(SPIN_LOCK_KEY_VC_RECON);

	dsb();

	#if CPU_ID == 1
	memset(l2p , 0x0, sizeof(u32) * spb_cnt + 4096 + 50);
	extern void ucache_resume();
	ucache_resume();
	#else
	sys_free_aligned(FAST_DATA, l2p);
	sys_free_aligned(FAST_DATA, vc);
	#endif
	
	vc_recon_busy[CPU_ID_0] = false;
}
#endif//CPU_ID != CPU_FTL
#endif//FW_BUILD_VAC_ENABLE == mENABLE


#endif

fast_code u32 ipc_api_log_level_chg(u32 new_level)
{
	u32 i;
	log_level_t old = LOG_INFO;

	for (i = 0; i < MPC; i++) {
		if (i == CPU_ID_0) {
			old = log_level_chg(new_level);
			continue;
		}

		cpu_msg_issue(i, CPU_MSG_LOG_LEVEL_CHG, 0, new_level);
	}

	return (u32) old;
}

fast_code void *ipc_api_remote_heap_malloc(u32 rx, u32 sz)
{
	volatile u32 * volatile t = sys_malloc(SLOW_DATA, sizeof(u32) * 16);
	void *ret;

	sys_assert(t);
	t[0] = sz;
	cpu_msg_sync_start();
	cpu_msg_issue(rx - 1, CPU_MSG_HEAP_ALLOC, 0, (u32)t);
	cpu_msg_sync_end();

	ret = (void *) t[8];
	sys_free(SLOW_DATA, (void *)t);
	return ret;
}

fast_code void ipc_api_remote_dtag_get(u32 *dtag, bool sync, bool ddr)
{
	sys_assert(CPU_DTAG != CPU_ID);
	if (sync) {
		dtag = tcm_local_to_share(dtag);
		cpu_msg_sync_start();
		cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_DTAG_GET_SYNC, ddr, (u32) dtag);
		cpu_msg_sync_end();
	} else {
		cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_DTAG_GET_ASYNC, ddr, 0);
	}

	return;
}

fast_code void ipc_api_ucache_read_error_data_in(int ofst, int status)
{
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_UCACHE_READ_ERROR, status, (u32)ofst);
}

#if (CO_SUPPORT_READ_AHEAD == TRUE)
fast_code void ipc_api_ra_err_data_in(int ofst, int status)
{
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_RA_READ_ERROR, status, (u32)ofst);
}
#endif

fast_code void ipc_api_get_btn_get_smart_io(btn_smart_io_t *smart_io, u32 rx, bool wr)
{
	void *ptr = (void *) smart_io;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_sync_start();
	cpu_msg_issue(rx, wr ? CPU_MSG_GET_BTN_WR_SMART_IO : CPU_MSG_GET_BTN_RD_SMART_IO, 0, (u32) ptr);
	cpu_msg_sync_end();
}

ddr_code void ipc_get_avg_erase_cnt(volatile cpu_msg_req_t *req)
{
	ec_ipc_hdl_t *hdl = (ec_ipc_hdl_t *) req->pl;
	get_avg_erase_cnt(hdl->avg_erase, hdl->max_erase, hdl->min_erase, hdl->total_ec);
	cpu_msg_sync_done(req->cmd.tx);
}


fast_code void ipc_api_misc_sys_isr_ctrl(u32 rx, u32 sirq, bool ctrl)
{
	cpu_msg_issue(rx, CPU_MSG_SIRQ_CTRL, ctrl, sirq);
}

fast_code bool __ftl_core_gc_action(gc_action_t *action)
{
	void *ptr = (void*)action;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FCORE_GC_ACT, 0, (u32)ptr);
	return false;
}
fast_code bool __ftl_core_gc_action2(gc_action_t *action)
{
	void *ptr = (void*)action;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FCORE_GC_ACT, 0, (u32)ptr);
	return false;
}
fast_code void __fe_req_op(fe_req_t *req)
{
	bool sync = req->cmpl ? false : true;

	if (!is_ptr_sharable(req))
		req = tcm_local_to_share(req);

	if (sync) {
		cpu_msg_sync_start();
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FE_REQ_OP, 0, (u32) req);
		cpu_msg_sync_end();
	} else {
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FE_REQ_OP, 0, (u32) req);
	}
}


fast_code bool __gc_action(gc_action_t *gc_act)
{
	void *ptr = (void*)gc_act;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GC_ACT, 0, (u32)ptr);
	return false;
}
#if GC_SUSPEND_FWDL		//20210308-Eddie
fast_code void __FWDL_GC_Handle(u8 type)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GC_HANDLE_FWDL, 0, (u8)type);
}
#endif
fast_code void __read_recoveried_done(dtag_t dtag)
{
	u32 rx;

	//if (dtag.b.dtag == DDR_RD_RECOVERY_0)
	//else if (dtag.b.dtag == DDR_RD_RECOVERY_1)
	if ((dtag.b.dtag >= DDR_RD_RECOVERY_EX_START) && (dtag.b.dtag < (DDR_RD_RECOVERY_EX_START + (DDR_RD_RECOVERY_EX_CNT/2))))
		rx = CPU_BE - 1;
#if defined(DUAL_BE)
	else if((dtag.b.dtag >= (DDR_RD_RECOVERY_EX_START + (DDR_RD_RECOVERY_EX_CNT/2))) && (dtag.b.dtag < (DDR_RD_RECOVERY_EX_START + DDR_RD_RECOVERY_EX_CNT)))
		rx = CPU_BE_LITE - 1;
#endif
	else
		rx = MPC;

    //utils_ipc_trace(LOG_INFO, 0, "__read_recoveried_done, dtag:0x%x, rx_CPU: %d, ch_id[%d]", dtag.b.dtag, rx, (dtag.b.dtag - DDR_RD_RECOVERY_EX_START)/8);

	sys_assert(rx < MPC);

	cpu_msg_issue(rx, CPU_MSG_READ_RECOVERIED_DONE, 0, dtag.dtag);
}

fast_code void __nvmet_core_btn_cmd_done(int btag, bool ret_cq, u32 cid)
{
	sys_assert((btag >> 15) == 0);
	btag = btag | ((ret_cq ? 1 : 0) << 15);

	cpu_msg_sync_start();
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_BTN_CMD_DONE, btag, cid);
	cpu_msg_sync_end();
}

fast_code bool __fwdl_op(fwdl_req_t *req)
{
	if ((req->op == FWDL_DOWNLOAD) && (!is_ptr_sharable(req->field.download.dtags)))
		req->field.download.dtags = tcm_local_to_share(req->field.download.dtags);

	if (!is_ptr_sharable(req))
		req = tcm_local_to_share(req);

	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FWDL_OP, 0, (u32) req);
	return false;
}

fast_code void ipc_api_wait_btn_rst(u32 cpu_idx, bool btn_reset)
{
	cpu_msg_issue(cpu_idx, CPU_MSG_WAIT_BTN_RESET, 0, btn_reset);
}

#ifdef ERRHANDLE_ECCT
fast_code void __ECC_Table_Operation(stECCT_ipc_t *ecct_req)
{

	void *ptr = (void*)ecct_req;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

    cpu_msg_issue(CPU_BE - 1, CPU_MSG_ECCT_OPERATION, 0, (u32)ptr);
}

fast_code u16 __ECC_Build_Table(void)
{
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_ECCT_BUILD, 0, 0);
    return 0;
}

fast_code void __rc_reg_ecct(bm_pl_t *bm_pl, u8 type)
{
	//tEcctReg *ecct_reg;
	//void *ptr;

	//ecct_reg.bm_pl = bm_pl;
	//ecct_reg.type  = type;
	//ptr = tcm_local_to_share((void *) &ecct_reg);
	//cpu_msg_sync_start();
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_RC_REG_ECCT, (u16)type, (u32)bm_pl);
    return;
	//cpu_msg_sync_end();
}
#endif

#ifdef TCG_NAND_BACKUP
ddr_code void __tcg_nf_op(tcg_nf_params_t *param)
{
	bool sync = param->sync;
	
	if (!(is_ptr_sharable(param) || is_ptr_tcm_share(param)))
		param = tcm_local_to_share(param);

	if (sync) {
		cpu_msg_sync_start();
		cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_TCG_NAND_API, 0, (u32) param);
		//cpu_msg_sync_end();
		cpu_msg_sync_end();
	}
	else
		cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_TCG_NAND_API, 0, (u32) param);
}
#endif

#if (_TCG_)
ddr_code void ipc_tcg_change_chkfunc_BTN_wr(u32 sts)
{
	//u32 sts = mTcgStatus;
	//cpu_msg_sync_start();
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_TCG_CHANGE_CHKFUNC_API, 0, sts);
	//cpu_msg_sync_end();
}
#endif

init_code void ipc_api_init(void)
{
#if CPU_ID != 1
	extern void ipc_pmu_suspend(volatile cpu_msg_req_t *req);
       extern void ipc_cpu_wb_lock(volatile cpu_msg_req_t *req);
	cpu_msg_register(CPU_MSG_WB_SYNC_LOCK, ipc_cpu_wb_lock);
	cpu_msg_register(CPU_MSG_PMU_SYNC_SUSPEND, ipc_pmu_suspend);
	cpu_msg_register(CPU_MSG_SHA3_SM3_CALLBACK, ipc_bm_sha3_sm3_calc_done);
	cpu_msg_register(CPU_MSG_WAIT_BTN_RESET, ipc_wait_btn_rst);
	evt_register(wait_btn_rst_handler, 0, &evt_wait_btn_rst);
#endif

	cpu_msg_register(CPU_MSG_NCMD_DONE, ipc_ncl_cmd_submit_done);

#if CPU_BE == CPU_ID
	cpu_msg_register(CPU_MSG_SRB_READ_DEFECT, ipc_srb_read_defect);
#endif

#if CPU_BE != CPU_ID && CPU_BE2 != CPU_ID && CPU_BE_LITE != CPU_ID
	cpu_msg_register(CPU_MSG_RAPID_NCMD_DONE, ipc_ncl_rapid_cmd_submit_done);
	cpu_msg_register(CPU_MSG_SCHEDULE_ERASE_DONE, ipc_scheudle_erase_done);
#else
	cpu_msg_register(CPU_MSG_NCL_CMD_EMPTY, ipc_ncl_cmd_empty);
	cpu_msg_register(CPU_MSG_NCMD, ipc_ncmd_exec);
    cpu_msg_register(CPU_MSG_NCMD_INSERT_SCH, ipc_ncmd_exec_insert_sch);
	cpu_msg_register(CPU_MSG_RAPID_NCMD, ipc_rapid_ncmd_exec);
	cpu_msg_register(CPU_MSG_DEFECT_SCAN, ipc_defect_scan);
	cpu_msg_register(CPU_MSG_WAIT_NCL_IDLE, ipc_wait_ncl_idle);
	cpu_msg_register(CPU_MSG_NCL_HANDLE_PENDING_CMD, ipc_ncl_handle_cmd);
	cpu_msg_register(CPU_MSG_NCL_CMD_BLOCK, ipc_ncl_cmd_block);
    cpu_msg_register(CPU_MSG_RD_ERR_HANDLING, ipc_rd_err_handling);
    cpu_msg_register(CPU_MSG_JOURNAL_UPDATE, ipc_journal_update);

	#if epm_enable
	cpu_msg_register(CPU_MSG_EPM_UPDATE, ipc_epm_update);

#if FRB_remap_enable
	cpu_msg_register(CPU_MSG_EPM_REMAP_tbl_UPDATE, ipc_epm_remap_tbl_update);
#endif

#if epm_spin_lock_enable
	cpu_msg_register(CPU_MSG_EPM_GET_KEY, ipc_epm_get_key);
	cpu_msg_register(CPU_MSG_EPM_UNLOCK, ipc_epm_unlock);
#endif
	#endif
	cpu_msg_register(CPU_MSG_GET_PROGRAM_AND_ERASE_FAIL_COUNT, ipc_get_program_and_erase_fail_count);
	cpu_msg_register(CPU_MSG_GET_NAND_BYTE_WRITTEN, ipc_get_nand_byte_written);
	cpu_msg_register(CPU_MSG_SWITCH_DU_FMT, ipc_eccu_dufmt_switch);
	cpu_msg_register(CPU_MSG_SWITCH_CMF, ipc_eccu_switch_cmf);
	#ifdef FWcfg_Rebuild 	//20201008-Eddie
	cpu_msg_register(CPU_MSG_FW_CONFIG_Rebuild, ipc_FW_CONFIG_Rebuild);
	#endif
    cpu_msg_register(CPU_MSG_SRB_ERASE, ipc_erase_srb);	//20201014-Eddie
#endif

#if CPU_BE_LITE == CPU_ID
	cpu_msg_register(CPU_MSG_SysInfo_UPDATE, ipc_SystemInfo_update);
#endif
#if CPU_BE == CPU_ID
	extern void be_do_vu_command_CPU2(volatile cpu_msg_req_t *req);
	cpu_msg_register(CPU_MSG_EVT_VUCMD_SEND_CPU2, be_do_vu_command_CPU2);
#endif
	cpu_msg_register(CPU_MSG_SIRQ_CTRL, ipc_sirq_ctrl);
	cpu_msg_register(CPU_MSG_LOG_LEVEL_CHG, ipc_log_level_chg);
	cpu_msg_register(CPU_MSG_HEAP_ALLOC, ipc_heap_alloc);
	cpu_msg_register(CPU_MSG_BE_PL, ipc_be_power_loss);
//Andy_Crypto
#if 0//defined(USE_CRYPTO_HW)
	cpu_msg_register(CPU_MSG_crypto, ipc_change_crypto);
	cpu_msg_register(CPU_MSG_Loadcrypto, ipc_Load_crypto);
#endif
#if CPU_FTL == CPU_ID
	cpu_msg_register(CPU_MSG_GET_EC_COUNT, ipc_get_avg_erase_cnt);
#endif
	cpu_msg_register(CPU_MSG_AER, ipc_nvmet_aer_event_trigger);
}
#endif

/*! @} */
