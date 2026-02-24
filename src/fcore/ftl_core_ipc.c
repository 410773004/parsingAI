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

/*! \file ftl_core_ipc.c
 * @brief define ipc message handler in ftl core
 *
 * \addtogroup fcore
 * \defgroup ftl_core_ipc
 * \ingroup fcore
 * @{
 */
#include "fcprecomp.h"
#include "fc_export.h"
#include "ftl_export.h"
#include "erase.h"
#include "bf_mgr.h"
#include "ipc_api.h"
#include "cpu_msg.h"
#include "ncl_exports.h"
#include "die_que.h"
#include "ftl_core.h"
#include "pmu.h"
#include "GList.h"
#include "ncl.h"
#include "read_retry.h"

#define __FILEID__ ftlc
#include "trace.h"
/*! @brief header of ftl_core_erase from other cpu */
typedef struct _ftl_core_erase_ipc_hdl_t {
	erase_ctx_t ctx;		///< local erase context
	erase_ctx_t *ipc_ctx;		///< remote erase context from tx
	u8 tx;				///< command tx
} ftl_core_erase_ipc_hdl_t;
extern bool ucache_flush_flag;
extern volatile u8 plp_trigger;

share_data_zi volatile ResetHandle_t smResetHandle;

fast_code void ipc_ftl_core_erase_done(erase_ctx_t *ctx)
{
	ftl_core_erase_ipc_hdl_t *hdl = (ftl_core_erase_ipc_hdl_t *) ctx;
	u8 tx = hdl->tx;
	erase_ctx_t *ipc_ctx = hdl->ipc_ctx;

	// Transfer erFal flag in local ctx to original one (ipc_ctx), ISU, SPBErFalHdl
	//if (ctx->spb_ent.b.erFal)
	{
	//	ipc_ctx->spb_ent.b.erFal = true;	
	}
	ipc_ctx->spb_ent.b.erFal = ctx->spb_ent.b.erFal;
	
	sys_free(FAST_DATA, hdl);

	cpu_msg_issue(tx, CPU_MSG_SCHEDULE_ERASE_DONE, 0, (u32)ipc_ctx);
}

fast_code void ipc_scheudle_erase_exec(volatile cpu_msg_req_t *req)
{
	erase_ctx_t *ctx = (erase_ctx_t *)req->pl;
	ftl_core_erase_ipc_hdl_t *hdl;

	hdl = sys_malloc(FAST_DATA, sizeof(ftl_core_erase_ipc_hdl_t));
	sys_assert(hdl);
	hdl->tx = req->cmd.tx;
	hdl->ipc_ctx = ctx;

	hdl->ctx = *ctx;
	hdl->ctx.cmpl = ipc_ftl_core_erase_done;
	ftl_core_erase(&hdl->ctx);
}

fast_code void _ipc_ftl_core_ctx_done(ftl_core_ctx_t *ctx)
{
	ftl_core_union_t *hdl = (ftl_core_union_t *) ctx;
	#if (PLP_NO_DONE_DEBUG == mENABLE)
	u64 curr = get_tsc_64();
	if (plp_trigger) {
		ftl_core_trace(LOG_ALW, 0xaabc, "ctx done 0x%x-%x", curr>>32, curr&0xFFFFFFFF);
	}
	#endif
	cpu_msg_issue(hdl->tx, CPU_MSG_FC_CTX_DONE, 0, (u32) ctx->caller);
	ftl_core_put_ctx(ctx);
}

fast_code ftl_core_union_t *_ipc_ftl_core_ctx_prepare(u32 sz, u8 tx, void *caller)
{
	ftl_core_ctx_t *ctx = ftl_core_get_ctx();
	ftl_core_union_t *hdl = (ftl_core_union_t *) ctx;

	sys_assert(ctx);
	hdl->tx = tx;

	memcpy(ctx, caller, sz);
	ctx->caller = caller;
	ctx->cmpl = _ipc_ftl_core_ctx_done;
	return hdl;
}


fast_code void ipc_fcore_idle_flush(volatile cpu_msg_req_t *req)
{
    fcore_idle_flush();
}


extern ftl_flush_data_t *fctx_addr;
extern TFtlPbftType tFtlPbt;
fast_code void ipc_flush_data(volatile cpu_msg_req_t *req)
{
	ftl_core_union_t *hdl;
	//u64 curr = get_tsc_64();
	hdl = _ipc_ftl_core_ctx_prepare(sizeof(ftl_flush_data_t), req->cmd.tx, (void *) req->pl);

    if(plp_trigger)
    {
        fctx_addr = &hdl->data;
        //tFtlPbt.force_pbt_halt = 1;
        //ftl_core_trace(LOG_ALW, 0, "start flush:0x%x-%x", curr>>32, curr&0xFFFFFFFF);
    }
	if (ftl_core_flush(&hdl->data) == FTL_ERR_OK) {
		hdl->data.ctx.cmpl(&hdl->data.ctx);
		ftl_core_trace(LOG_INFO, 0xd4a1, "fctx:0x%x done", &hdl->data);
	}
}

fast_code void ipc_ns_start(volatile cpu_msg_req_t *req)
{
	u32 nsid = req->pl;

	ftl_core_start(nsid);
}

fast_code void ipc_qbt_alloc_cmpl(ns_start_t *ctx)
{
	cpu_msg_issue(ctx->tx, CPU_MSG_OPEN_QBT_DONE, 0, (u32)ctx->caller);
	sys_free(FAST_DATA, ctx);
}
fast_code void ipc_qbt_alloc(volatile cpu_msg_req_t *req)
{
	ns_start_t *_ctx = (ns_start_t*)req->pl;
	ns_start_t *ctx = sys_malloc(FAST_DATA, sizeof(ns_start_t));
	sys_assert(ctx);
	ctx->tx = req->cmd.tx;
	ctx->nsid = _ctx->nsid;
	ctx->type = _ctx->type;
	ctx->caller = _ctx;
	ctx->cmpl = ipc_qbt_alloc_cmpl;
	ftl_core_qbt_alloc(ctx);
}
fast_code void ipc_ftl_power_loss(volatile cpu_msg_req_t *req)
{
	ftl_core_trace(LOG_ERR, 0x57b9, "\n ftl: %d\n", jiffies);
	ftl_core_trace(LOG_ERR, 0x6593, "\n\033[91m%s\x1b[0m\n", __func__);
}

fast_code void ipc_flush_table(volatile cpu_msg_req_t *req)
{
	ftl_core_union_t *hdl;

	hdl = _ipc_ftl_core_ctx_prepare(sizeof(ftl_flush_tbl_t), req->cmd.tx, (void *) req->pl);

	ftl_core_flush_tbl(&hdl->tbl);
}

fast_code void ipc_flush_misc(volatile cpu_msg_req_t *req)
{
	ftl_core_union_t *hdl;

	hdl = _ipc_ftl_core_ctx_prepare(sizeof(ftl_flush_misc_t), req->cmd.tx, (void *) req->pl);

	ftl_core_flush_misc(&hdl->misc);
}

fast_code void ipc_flush_blklist_handling(volatile cpu_msg_req_t *req)
{
	ftl_core_union_t *hdl;

	hdl = _ipc_ftl_core_ctx_prepare(sizeof(ftl_flush_misc_t), req->cmd.tx, (void *) req->pl);
	ftl_core_flush_blklist(&hdl->misc);
}

fast_code void ipc_force_close_pstream(volatile cpu_msg_req_t *req)	// ISU, Tx, PgFalClsNotDone(1)
{
	u16 spb_id = req->pl;
	pstream_force_close(spb_id);
}

fast_code void ipc_update_fence(volatile cpu_msg_req_t *req)
{
	ftl_fence_t *fence = (ftl_fence_t *) req->pl;
	u8 tx = req->cmd.tx;

	ftl_core_update_fence(fence);
	cpu_msg_sync_done(tx);
}

fast_code void ipc_spb_ack(volatile cpu_msg_req_t *req)
{
	nsid_type_t pl = { .all = req->pl };

	ftl_core_resume(pl.b.nsid, pl.b.type);
}


fast_code void ipc_restore_fence(volatile cpu_msg_req_t *req)
{
	ftl_fence_t *fence = (ftl_fence_t *) req->pl;
	u8 tx = req->cmd.tx;

	ftl_core_restore_fence(fence);
	cpu_msg_sync_done(tx);
}

fast_code void ipc_restore_rt_flags(volatile cpu_msg_req_t *req)
{
	spb_rt_flags_t *rt_flags = (spb_rt_flags_t *) req->pl;
	u8 tx = req->cmd.tx;

	ftl_core_restore_rt_flags(rt_flags);
	cpu_msg_sync_done(tx);
}

fast_code void ipc_gc_start(volatile cpu_msg_req_t *req)
{
	gc_req_t *gc_req = (gc_req_t*)req->pl;

	ftl_core_gc_start(gc_req);
}

fast_code void ipc_alloc_rc_dtag_done(volatile cpu_msg_req_t *req)
{
	u32 dtag = req->pl;

	raid_correct_got_dtag(dtag);
}

fast_code void ipc_ftl_format_done(volatile cpu_msg_req_t *req)
{
	format_ctx_t *ctx = (format_ctx_t *)req->pl;

	ctx = tcm_share_to_local(ctx);
	ctx->cmpl(ctx);
}

fast_code void ipc_spb_pad(volatile cpu_msg_req_t *req)
{
	ftl_core_union_t *hdl;
	ftl_core_trace(LOG_INFO, 0x13c9, "cpu2 get flush msg: cpu3->cpu2");
	hdl = _ipc_ftl_core_ctx_prepare(sizeof(ftl_spb_pad_t), req->cmd.tx, (void *) req->pl);
	sys_assert(hdl->pad.param.end_nsid < TOTAL_NS);
	sys_assert(hdl->pad.param.start_nsid < TOTAL_NS);
	
	extern ftl_spb_pad_t *get_free_spb_pad();
	ftl_spb_pad_t *spb_pad = get_free_spb_pad();

	spb_pad->spb_id = hdl->pad.spb_id;
	spb_pad->pad_attribution = hdl->pad.pad_attribution;
	spb_pad->param.cwl_cnt = hdl->pad.param.cwl_cnt;
	spb_pad->param.pad_all = hdl->pad.param.pad_all;
	spb_pad->param.start_type = hdl->pad.param.start_type;
	spb_pad->param.end_type = hdl->pad.param.end_type;
	spb_pad->param.start_nsid = hdl->pad.param.start_nsid;
	spb_pad->param.end_nsid = hdl->pad.param.end_nsid;
	spb_pad->cur.cwl_cnt = hdl->pad.cur.cwl_cnt;
	spb_pad->cur.type = hdl->pad.cur.type;
	spb_pad->cur.nsid = hdl->pad.cur.nsid;
	spb_pad->ctx.caller = (void *)hdl; 
	
	extern void ipc_fill_dummy_done(ftl_core_ctx_t *ctx);
	spb_pad->ctx.cmpl = ipc_fill_dummy_done;
	// ftl_core_trace(LOG_ERR, 0, "pad :%d, %d, %d, %d, %d, %d, %d\n",spb_pad->param.cwl_cnt, spb_pad->param.pad_all, spb_pad->param.type, spb_pad->param.nsid, spb_pad->cur.cwl_cnt, 
	// spb_pad->cur.type, spb_pad->cur.nsid);
	ftl_core_spb_pad(spb_pad);
}

fast_code void ipc_wr_era_done(volatile cpu_msg_req_t *req)
{
	ncl_w_req_t *ncl_req = (ncl_w_req_t*)req->pl;

	ncl_req->req.cmpl(ncl_req);
}

fast_code void __raid_correct_done(rc_req_t *rc_req)
{
	sys_assert(rc_req->flags.b.remote);
	cpu_msg_issue(rc_req->ack_cpu, CPU_MSG_RAID_CORRECT_DONE, 0, (u32)rc_req);
}

fast_code void ipc_read_done_rc_entry(volatile cpu_msg_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *) req->pl;
    read_done_rc_entry(ncl_cmd);  
}

fast_code void ipc_raid_correct_push(volatile cpu_msg_req_t *req)
{
	rc_req_t *rc_req = (rc_req_t*)req->pl;

	rc_req->flags.b.remote = true;
	rc_req->ack_cpu = req->cmd.tx;
	raid_correct_push(rc_req);
}

fast_code void ipc_gc_grp_rd_done(volatile cpu_msg_req_t *req)
{
	u32 gc_grp = req->pl;
	ftl_core_gc_grp_rd_done(gc_grp);
}

fast_code void ipc_fcore_gc_action_cmpl(gc_action_t *action)
{
	cpu_msg_issue(action->tx, CPU_MSG_FCORE_GC_ACT_DONE, 0, (u32)action->caller);
	sys_free(FAST_DATA, action);
}


fast_code void ipc_fcore_gc_action(volatile cpu_msg_req_t *req)
{
	gc_action_t *_action = (gc_action_t*)req->pl;
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = _action->act;
	action->tx = req->cmd.tx;
	action->caller = _action;
	action->cmpl = ipc_fcore_gc_action_cmpl;

	if (ftl_core_gc_action(action))
		action->cmpl(action);
}

fast_code void force_spb_pad_done(ftl_core_ctx_t *ctx)
{
	 ftl_core_union_t *hdl = (ftl_core_union_t *)(ctx->caller);
	 sys_free(FAST_DATA, ctx);
	 ftl_core_format(&hdl->format);
}

fast_code void ipc_format(volatile cpu_msg_req_t *req)
{
	ftl_core_union_t *hdl;

	hdl = _ipc_ftl_core_ctx_prepare(sizeof(ftl_format_t), req->cmd.tx, (void *) req->pl);
	/*
	ftl_spb_pad_t *spb_pad = sys_malloc(FAST_DATA, sizeof(ftl_spb_pad_t));
	sys_assert(spb_pad);

	spb_pad->spb_id = 0xFFFF;
	spb_pad->ctx.caller = (void *)hdl;
	spb_pad->param.cwl_cnt = 0;
	spb_pad->param.pad_all = true;
	spb_pad->param.type = FTL_CORE_GC;
	spb_pad->param.nsid = 1;
	spb_pad->cur.cwl_cnt = 0;
	spb_pad->cur.type = FTL_CORE_NRM;
	spb_pad->cur.nsid = 1;
	spb_pad->ctx.cmpl = force_spb_pad_done;
	
	ftl_core_spb_pad(spb_pad);
	*/
	ftl_core_format(&hdl->format);
}

fast_code void ipc_load_p2l_done(void *ctx)
{
	p2l_load_req_t *load_req = (p2l_load_req_t*)ctx;

	cpu_msg_sync_done(load_req->tx);
}

fast_code void ipc_load_p2l(volatile cpu_msg_req_t *req)
{
	p2l_load_req_t *load_req;

	load_req = (p2l_load_req_t*)req->pl;
	load_req->tx = req->cmd.tx;
	load_req->cmpl = ipc_load_p2l_done;
	ftl_core_p2l_load(load_req);
}

fast_code void ipc_ftl_flush_done(volatile cpu_msg_req_t *req)
{
	flush_ctx_t *ctx = (flush_ctx_t *) req->pl;

	if (is_ptr_tcm_share((void *) ctx))
		ctx = (flush_ctx_t *) tcm_share_to_local((void *) ctx);

	ctx->cmpl(ctx);
}

fast_code void ipc_ns_open_done(volatile cpu_msg_req_t *req)
{
	ftl_core_open_done(req->pl);
}

fast_code void ipc_pmu_swap_file_register(volatile cpu_msg_req_t *req)
{
	swap_mem_t *range = (swap_mem_t *)req->pl;
	u8 tx = req->cmd.tx;

	pmu_swap_file_register(range->loc, range->size);
	cpu_msg_sync_done(tx);
}

fast_code void ipc_gc_action_done(volatile cpu_msg_req_t *req)
{
	gc_action_t *action = (gc_action_t*)req->pl;

	if (is_ptr_tcm_share((void*)action))
		action = (gc_action_t*)tcm_share_to_local((void*)action);
	if(action->cmpl == NULL)
		sys_free(FAST_DATA, action);
	else
		action->cmpl(action);
}

#if XOR_CMPL_BY_PDONE
fast_code void ipc_stripe_xor_done(volatile cpu_msg_req_t *req)
{
	u32 stripe_id = req->pl;

	ftl_core_parity_flush(stripe_id);
}
#endif
fast_code void ipc_fc_ready_chk(volatile cpu_msg_req_t *req)
{
	u32 ns_bmp = req->pl;
	ftl_core_ready(0, ns_bmp, 0);
}

ddr_code void ipc_sched_abort_strm_read_done(volatile cpu_msg_req_t *req)
{
	ftl_reset_t *ctx = (ftl_reset_t *) tcm_share_to_local((void *) req->pl);

	ftl_core_reset_wait_sched_idle_done(req->cmd.flags, ctx);
}

ddr_code void ipc_ftl_core_reset_notify(volatile cpu_msg_req_t *req)
{
	ftl_core_reset_notify();
}

ddr_code void ipc_plp_cancel_die_que() {
	plp_cancel_die_que();
}

ddr_code void ipc_ftl_core_chk_reset_ack(volatile cpu_msg_req_t *req)
{
	sys_assert(reset_ack_req_tx == 0xFF);
	if (fc_flags.b.reset_ack == false)
	{
		reset_ack_req_tx = req->cmd.tx;
		//ftl_core_trace(LOG_ERR, 0, "tx:%x\n",reset_ack_req_tx);
		fc_flags.b.reset_ack_req = true;
		return;
	}

	fc_flags.b.reset_ack = false;
	fc_flags.b.reseting = false;
	//cpu_msg_sync_done(req->cmd.tx);
	smResetHandle.abortMedia = false;
}

fast_code void ipc_ftl_core_reset_done_notify(volatile cpu_msg_req_t *req)
{
	ftl_core_reset_done_notify();
}

ddr_code void ipc_ondeman_dump(volatile cpu_msg_req_t *req)
{
    extern u32 dtag_recv;
    extern u32 shr_dtag_ins_cnt;
	extern u32 parity_allocate_cnt;	// FET, EHDbgMsg
	extern ftl_core_ns_t *fcns[INT_NS_ID + 1];


    AplDieQueue_DumpInfo(false);
    ftl_core_trace(LOG_INFO, 0x1c30, "[DTAG] recv %d/%d", shr_dtag_ins_cnt, dtag_recv);
    ftl_core_trace(LOG_INFO, 0x9514, "[DTAG] %d/%d", shr_dtag_comt.que.wptr, shr_dtag_comt.que.rptr);
	ftl_core_trace(LOG_INFO, 0x0f5c, "[EH] parity_alloc_cnt[%d] ps.parity_cnt(host/gc/pbt)[%d/%d/%d]", \
		parity_allocate_cnt, \
		fcns[HOST_NS_ID]->ftl_core[FTL_CORE_NRM]->ps.parity_cnt, fcns[HOST_NS_ID]->ftl_core[FTL_CORE_GC]->ps.parity_cnt,  fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps.parity_cnt);
}

extern volatile u8 cpu_feedback[];
ddr_code void ipc_feedback_set(volatile cpu_msg_req_t *req)
{
    cpu_feedback[CPU_ID_0] = true;
}

fast_code void ipc_api_plp_debug_fill_done(volatile cpu_msg_req_t *req)
{
	plp_debug_fill_done();
}

fast_code void ipc_api_plp_force_flush_done(volatile cpu_msg_req_t *req)
{
	plp_force_flush_done();
}

fast_code void ipc_fplp_trigger(volatile cpu_msg_req_t *req)
{
	fplp_trigger_start();
}

#if defined(SAVE_CDUMP)
ddr_code void ipc_ulink_da_mode2(volatile cpu_msg_req_t *req)
{
	extern u32 ulink_da_mode(void);
	ulink_da_mode();
}
#endif
extern void ipc_suspend_gc_for_vac_cal(volatile cpu_msg_req_t *req);
extern void ipc_active_gc(volatile cpu_msg_req_t *req);
#if (FW_BUILD_VAC_ENABLE == mENABLE)
void l2p_vac_recon(volatile cpu_msg_req_t *req);
#endif
ddr_code void ipc_force_pbt(volatile cpu_msg_req_t *req)
{
    extern void set_evt_force_pbt();
    set_evt_force_pbt(req->pl);
    // evt_set_cs(evt_force_pbt, true, true, CS_TASK);
}
ddr_code void ipc_save_pbt_param(volatile cpu_msg_req_t *req)
{
    evt_fcore_save_pbt_param(0, 0, 0);
}

ddr_code void ipc_format_save_vac(volatile cpu_msg_req_t *req)
{
	bool type = (bool)req->pl;
	extern void format_save_vac_table();
	format_save_vac_table(type);
}

#if (PLP_SUPPORT == 1)   
ddr_code void ipc_init_done_save_epm(volatile cpu_msg_req_t *req)
{
	//pstream_clear_epm_vac();//init done save epm + enable SLC buffer 
	//ftl_slc_pstream_enable();//init done enable SLC buffer
	bool is_update = (bool)req->pl;
	if(is_update)
	{
		pstream_clear_epm_vac();
	}
	else
	{
		#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		ftl_slc_pstream_enable();
		#endif
	}
}
#endif

init_code void ftl_core_ipc_init(void)
{
	cpu_msg_register(CPU_MSG_SCHEDULE_ERASE, ipc_scheudle_erase_exec);
	cpu_msg_register(CPU_MSG_FLUSH_DATA, ipc_flush_data);  
    cpu_msg_register(CPU_MSG_SUSPEND_GC, ipc_suspend_gc);
    cpu_msg_register(CPU_MSG_STOP_GC_FOR_VAC_CAL, ipc_suspend_gc_for_vac_cal);    
    cpu_msg_register(CPU_MSG_ACTIVE_GC, ipc_active_gc);    
    cpu_msg_register(CPU_MSG_CONTROL_GC, ipc_control_gc);
    cpu_msg_register(CPU_MSG_FORCE_FLUSH, ipc_fcore_idle_flush);   
	cpu_msg_register(CPU_MSG_NS_START, ipc_ns_start);
	cpu_msg_register(CPU_MSG_NS_OPEN_DONE, ipc_ns_open_done);
	cpu_msg_register(CPU_MSG_FLUSH_TABLE, ipc_flush_table);
	cpu_msg_register(CPU_MSG_FLUSH_MISC, ipc_flush_misc);
	cpu_msg_register(CPU_MSG_FORMAT, ipc_format);
	cpu_msg_register(CPU_MSG_SPB_ACK, ipc_spb_ack);
	cpu_msg_register(CPU_MSG_UPDATE_FENCE, ipc_update_fence);
	cpu_msg_register(CPU_MSG_RESTORE_FENCE, ipc_restore_fence);
	cpu_msg_register(CPU_MSG_RESTORE_RT_FLAGS, ipc_restore_rt_flags);
	cpu_msg_register(CPU_MSG_GC_START, ipc_gc_start);
	cpu_msg_register(CPU_MSG_TZU_GET, tzu_get_gc_info);
	cpu_msg_register(CPU_MSG_RC_DTAG_ALLOCED, ipc_alloc_rc_dtag_done);
	cpu_msg_register(CPU_MSG_SPB_PAD, ipc_spb_pad);
	cpu_msg_register(CPU_MSG_BM_COPY_DONE, ipc_bm_copy_done);
	cpu_msg_register(CPU_MSG_FTL_FORMAT_DONE, ipc_ftl_format_done);
	cpu_msg_register(CPU_MSG_FCORE_GC_ACT, ipc_fcore_gc_action);
	cpu_msg_register(CPU_MSG_LOAD_P2L, ipc_load_p2l);
	cpu_msg_register(CPU_MSG_FTL_FLUSH_DONE, ipc_ftl_flush_done);
	cpu_msg_register(CPU_MSG_FTL_PL, ipc_ftl_power_loss);
	cpu_msg_register(CPU_MSG_PMU_SWAP_REG, ipc_pmu_swap_file_register);
	cpu_msg_register(CPU_MSG_GC_ACT_DONE, ipc_gc_action_done);
	cpu_msg_register(CPU_MSG_OPEN_QBT, ipc_qbt_alloc);
	cpu_msg_register(CPU_MSG_FLUSH_BLKLIST_HANDLING, ipc_flush_blklist_handling);
	cpu_msg_register(CPU_MSG_FORCE_CLOSE_PSTREAM, ipc_force_close_pstream);	// ISU, Tx, PgFalClsNotDone(1)
#ifdef XOR_CMPL_BY_PDONE
	cpu_msg_register(CPU_MSG_STRIPE_XOR_DONE, ipc_stripe_xor_done);
#endif

	cpu_msg_register(CPU_MSG_SCHED_ABORT_STRM_READ_DONE, ipc_sched_abort_strm_read_done);
	cpu_msg_register(CPU_MSG_PERST_FTL_NOTIFY, ipc_ftl_core_reset_notify);
	cpu_msg_register(CPU_MSG_PLP_CANCEL_DIEQUE, ipc_plp_cancel_die_que);
	cpu_msg_register(CPU_MSG_PERST_FTL_CHK_ACK, ipc_ftl_core_chk_reset_ack);
	cpu_msg_register(CPU_MSG_PERST_DONE_FTL_NOTIFY, ipc_ftl_core_reset_done_notify);
	cpu_msg_register(CPU_MSG_ONDEMAND_DUMP, ipc_ondeman_dump);
	cpu_msg_register(CPU_MSG_LOOP_FEEDBACK, ipc_feedback_set);

	cpu_msg_register(CPU_MSG_FC_READY_CHK, ipc_fc_ready_chk);
#ifdef DUAL_BE
	cpu_msg_register(CPU_MSG_WR_ERA_DONE, ipc_wr_era_done);

#ifdef ERRHANDLE_GLIST
	cpu_msg_register(CPU_MSG_REG_GLIST, ipc_wGListRegBlock);
	cpu_msg_register(CPU_MSG_SET_FAILBLK,__EH_SetFailBlk);
	cpu_msg_register(CPU_MSG_DEL_FAILBLK, __EH_DelFailBlk);
	cpu_msg_register(CPU_MSG_SET_FAIL_OP_BLK, __Set_Fail_OP_Blk);
    cpu_msg_register(CPU_MSG_DEL_FAIL_OP_CNT, __Del_Err_OP_Cnt);

    cpu_msg_register(CPU_MSG_INIT_WRITTEN, ipc_Init_Nand_Written);
    cpu_msg_register(CPU_MSG_ECCT_MARK, __Register_ECCT_By_Raid_Recover);
    cpu_msg_register(CPU_MSG_ECCT_RECORD, __wECC_Record_Table);

	cpu_msg_register(CPU_MSG_INIT_EH_MGR, ipc_init_eh_mgr);	// FET, PfmtHdlGL
#endif

#if (CPU_ID == CPU_BE)
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
		cpu_msg_register(CPU_MSG_GET_ERRHANDLEVAR_CPU_BE_SMART, __get_telemetry_ErrHandleVar_SMART_CPU_BE);
#endif
#endif

	cpu_msg_register(CPU_MSG_RAID_CORRECT_PUSH, ipc_raid_correct_push);
    cpu_msg_register(CPU_MSG_READ_DONE_RC_ENTRY, ipc_read_done_rc_entry);
	cpu_msg_register(CPU_MSG_GC_GRP_RD_DONE, ipc_gc_grp_rd_done);
#endif
#ifdef ERRHANDLE_ECCT
    cpu_msg_register(CPU_MSG_ECCT_OPERATION, ipc_ECC_Table_Operation);
    cpu_msg_register(CPU_MSG_ECCT_BUILD, ipc_ECC_Build_Table);
#endif
#ifdef NCL_RETRY_PASS_REWRITE
    cpu_msg_register(CPU_MSG_RETRY_REWRITE, ipc_host_read_retry_add_rewrite);
#endif
#if PLP_DEBUG
    cpu_msg_register(CPU_MSG_PLP_FILL_DONE, ipc_api_plp_debug_fill_done);
#endif
    cpu_msg_register(CPU_MSG_PLP_TRIGGER, ipc_api_plp_force_flush_done);
    cpu_msg_register(CPU_MSG_FPLP_TRIGGER, ipc_fplp_trigger);
#if defined(SAVE_CDUMP)
        cpu_msg_register(CPU_MSG_ULINK, ipc_ulink_da_mode2);
#endif
	cpu_msg_register(CPU_MSG_FORCE_PBT, ipc_force_pbt);
    cpu_msg_register(CPU_MSG_SAVE_PBT_PARAM, ipc_save_pbt_param);

    cpu_msg_register(CPU_MSG_TRIM_CPY_VAC, ipc_format_save_vac);

#if (PLP_SUPPORT == 1)   
    cpu_msg_register(CPU_MSG_INIT_DONE_SAVE_EPM, ipc_init_done_save_epm);
#endif
#if (FW_BUILD_VAC_ENABLE == mENABLE)
    cpu_msg_register(CPU_MSG_VC_RECON, l2p_vac_recon);
#endif
}

/*! @} */
