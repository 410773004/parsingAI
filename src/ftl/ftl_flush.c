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
/*! \file ftl_flush.c
 * @brief define ftl flush state machine to flush a namespace
 *
 * \addtogroup ftl
 * \defgroup flush
 * \ingroup ftl
 * @{
 * to flush a namespace, we must flush all data in cache then flush l2p and misc
 *
 */
//=============================================================================

#include "ftlprecomp.h"
#include "ftl_ns.h"
#include "ftl_l2p.h"
#include "fsm.h"
#include "spb_mgr.h"
#include "mpc.h"
#include "gc.h"
#include "spb_log_flush.h"
#include "frb_log.h"
#include "fc_export.h"
#include "ftl_flash_geo.h"

/*! \cond PRIVATE */
#define __FILEID__ fflush
#include "trace.h"
/*! \endcond PRIVATE */

/*!
 * @brief definition state machine to flush
 */

share_data_zi volatile bool shr_shutdownflag;
share_data_zi volatile bool shr_qbtfilldone;
share_data volatile u8 plp_trigger;
share_data epm_info_t*  shr_epm_info;
fast_data_zi volatile u64 ffsm_start;
fast_data_zi volatile u64 ffsm_all_start;

extern spb_mgr_t _spb_mgr;			///< spb manager
extern spb_id_t last_spb_vcnt_zero;
extern volatile bool shr_qbt_prog_err;
extern volatile u16 spor_read_pbt_blk[4];
extern volatile u8 spor_read_pbt_cnt;
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
extern volatile bool delay_flush_spor_qbt; 
#endif 


typedef struct _flush_fsm_t {
	fsm_ctx_t fsm;
	u32 nsid;			///< namespace id
	struct list_head pend_que;	///< waiting flush req
	union {
		struct {
			u32 flushing : 1;
			u32 make_ins_dirty : 1;	///< if state machine was making internal ns to dirty state
			u32 hns_pad : 1;	///< if host namespace was padded
		} b;
		u32 all;
	} flags;
	union {
		gc_action_t gc_action;
		ns_start_t qbt_alloc;
		ftl_flush_tbl_t flush_tbl;	///< context to flush l2p table
		ftl_flush_misc_t flush_misc;	///< context to flush misc data (valid count/valid bitmap)
		ftl_spb_pad_t spb_pad;		///< context to pad active spb
	};
} flush_fsm_t;

///< each namespace has one flush state machine
fast_data_zi static flush_fsm_t _ffsm[FTL_NS_ID_END];

/*!
 * @brief flush state function to suspend trim
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */


#if 0//(TRIM_SUPPORT == DISABLE)
fsm_res_t flush_st_suspend_trim(fsm_ctx_t *fsm);
#endif

/*!
 * @brief flush state function to suspend gc
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t flush_st_suspend_gc(fsm_ctx_t *fsm);
static fsm_res_t flush_st_wait_qbt(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to wait for spb allocation
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
//static fsm_res_t flush_st_wait_alloc(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to get fence
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
//static fsm_res_t flush_st_get_hns_fence(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to flush l2p
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t flush_st_flush_l2p(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to pda all active spb
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t flush_st_pad_spb(fsm_ctx_t *fsm);
static fsm_res_t flush_st_new_close_qbt(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to flush misc data
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t flush_st_flush_misc(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to flush host namespace descriptor
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t flush_st_flush_hns_desc(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to flush internal namespace descriptor
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t flush_st_flush_ins_desc(fsm_ctx_t *fsm);

/*!
 * @brief flush state function to flush everything done
 *
 * @param fsm	flush state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t flush_st_flush_done(fsm_ctx_t *fsm);
enum {
	FTL_ST_SUSPEND_GC		= 0,
	FTL_ST_WAIT_QBT			= 1,
	FTL_ST_PAD_SPB			= 2,
	FTL_ST_FLUSH_L2P		= 3,
	FTL_ST_FLUSH_MISC		= 4,
	FTL_ST_CLOSE_QBT		= 5,
	FTL_ST_FLUSH_HNS_DESC	= 6,
	FTL_ST_FLUSH_INS_DESC	= 7,
	FTL_ST_FLUSH_DONE       = 8,
	FTL_ST_MAX
};

/*!
 * @brief flush state function pointer array
 */
fast_data static fsm_funs_t _flush_st_func[] = {

	{"suspend gc", flush_st_suspend_gc},
	{"wait qbt alloc", flush_st_wait_qbt},
	//{"wait alloc", flush_st_wait_alloc},
	{"pad spb", flush_st_pad_spb},
	{"flush_l2p", flush_st_flush_l2p},
	{"flush misc", flush_st_flush_misc},
	{"close qbt", flush_st_new_close_qbt},
	{"flush hns", flush_st_flush_hns_desc},
	{"flush ins", flush_st_flush_ins_desc},
	{"flush done", flush_st_flush_done}
};

/*!
 * @brief flush state machine states
 */
fast_data static fsm_state_t _fsm_state = {
	.name = "flushfsm",
	.fns = _flush_st_func,
	.max = ARRAY_SIZE(_flush_st_func)
};

/*!
 * @brief runtime table flush state function pointer array
 */
/*
fast_data static fsm_funs_t _tbl_flush_st_func[] = {
	{"get fence", flush_st_get_hns_fence},
	{"flush l2p", flush_st_flush_l2p},
	{"flush hns", flush_st_flush_hns_desc},
	{"flush ins", flush_st_flush_ins_desc},
	{"flush done", flush_st_flush_done}
};
*/
/*!
 * @brief runtime table flush state machine states
 */
 /*
fast_data static fsm_state_t _tbl_fsm_state = {
	.name = "flushfsm",
	.fns = _tbl_flush_st_func,
	.max = ARRAY_SIZE(_tbl_flush_st_func)
};
*/
init_code void flush_fsm_init(u32 nsid)
{
	flush_fsm_t *ffsm;

	ffsm = &_ffsm[nsid];
	ffsm->nsid = nsid;
	ffsm->flags.all = 0;
	INIT_LIST_HEAD(&ffsm->pend_que);
}

extern void pop_shuttle_back_to_gc(void);
fast_code int flush_fsm_done(void *ctx)
{
	fsm_ctx_t *fsm = (fsm_ctx_t*)ctx;
	flush_fsm_t *ffsm = (flush_fsm_t*)fsm;
	flush_ctx_t *flush_ctx = (flush_ctx_t *)fsm->done_priv;
	
	ftl_apl_trace(LOG_INFO, 0x6ad3, "flush_fsm_done in %d ms", time_elapsed_in_ms(ffsm_all_start));
	
	pop_shuttle_back_to_gc();	//pop shuttle blk back to GC_POOL, prevent shut down without power off
	
	shr_ftl_flags.b.flushing = false;
	ffsm->flags.b.flushing = 0;
	flush_ctx->cmpl(flush_ctx);

	if (!list_empty(&ffsm->pend_que)) {
		flush_ctx = list_first_entry(&ffsm->pend_que, flush_ctx_t, entry);
		list_del(&flush_ctx->entry);

		//if (flush_ctx->flags.b.spb_close)
			//tbl_flush_fsm_run(flush_ctx);
		//else
			flush_fsm_run(flush_ctx);
	}

	return 0;
}

fast_code void flush_fsm_run(flush_ctx_t *ctx)
{
	u32 nsid = ctx->nsid;
	flush_fsm_t *ffsm = &_ffsm[nsid];

	if (ffsm->flags.b.flushing) {
		INIT_LIST_HEAD(&ctx->entry);
		list_add_tail(&ctx->entry, &ffsm->pend_que);
		return;
	}

	ffsm_all_start = get_tsc_64();
	ffsm->flags.all = 0;
	ffsm->flags.b.flushing = 1;
	shr_ftl_flags.b.flushing = true;
	fsm_ctx_init(&ffsm->fsm, &_fsm_state, flush_fsm_done, ctx);
	fsm_ctx_run(&ffsm->fsm);
}
/*
fast_code void tbl_flush_fsm_run(flush_ctx_t *ctx)
{
	u32 nsid = ctx->nsid;
	flush_fsm_t *ffsm = &_ffsm[nsid];

	if (ffsm->flags.b.flushing) {
		INIT_LIST_HEAD(&ctx->entry);
		list_add_tail(&ctx->entry, &ffsm->pend_que);
		return;
	}

	ffsm->flags.all = 0;
	ffsm->flags.b.flushing = 1;
	shr_ftl_flags.b.flushing = true;
	fsm_ctx_init(&ffsm->fsm, &_tbl_fsm_state, flush_fsm_done, ctx);
	fsm_ctx_run(&ffsm->fsm);
}
*/
/*
 * @brief completion callback when l2p table was flushed, continue to next step
 *
 * @param flush_tbl	flush table object
 *
 * @return		not used
 */

fast_code void gc_suspend_done(gc_action_t* action)
{
	flush_fsm_t *fsm_ctx = (flush_fsm_t*)action->caller;

	ftl_apl_trace(LOG_INFO, 0xbdd8, "gc suspended done");

	fsm_ctx_next(&fsm_ctx->fsm);
	fsm_ctx_run(&fsm_ctx->fsm);
}

fast_code void ftl_open_qbt_done(ns_start_t *ctx)
{
	flush_fsm_t* fsm_ctx = (flush_fsm_t*)ctx->caller;

	ftl_apl_trace(LOG_ERR, 0x4936, "open qbt done");
    if (shr_qbt_prog_err)
    {
        ftl_apl_trace(LOG_ERR, 0x1bc1, "skip filling dummy for PBT,Host and GC, start flush l2p");
        fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_FLUSH_L2P);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
    else
    {
    	fsm_ctx_next(&fsm_ctx->fsm);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
}

/*!
 * @brief completion callback when l2p table was flushed, continue to next step
 *
 * @param flush_tbl	flush table object
 *
 * @return		not used
 */
fast_code void flush_l2p_done(ftl_core_ctx_t *ctx)
{
	ftl_flush_tbl_t *flush_tbl = (ftl_flush_tbl_t *) ctx;
	ftl_ns_t *ftl_ns = ftl_ns_get(flush_tbl->nsid);
	flush_fsm_t *fsm_ctx = (flush_fsm_t *) ctx->caller;

    if (shr_qbt_prog_err)
    {
        //ftl_apl_trace(LOG_INFO, 0, "qbt prog err, jump to FTL_ST_CLOSE_QBT");
        fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_CLOSE_QBT);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
    else
    {
    	ftl_apl_trace(LOG_INFO, 0x2314, "l2p %d flushed done in %d ms", flush_tbl->nsid, time_elapsed_in_ms(ffsm_start));
    	sys_assert(ftl_ns->flags.b.flushing);
    	ftl_ns->flags.b.flushing = 0;

    	fsm_ctx_next(&fsm_ctx->fsm);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
}

/*!
 * @brief completion callback when misc data was flushed, continue to next step
 *
 * @param flush_misc	flush misc object
 *
 * @return		not used
 */
fast_code void flush_misc_done(ftl_core_ctx_t *ctx)
{
	flush_fsm_t *fsm_ctx = (flush_fsm_t *) ctx->caller;
	ftl_flush_misc_t *flush_misc = (ftl_flush_misc_t *) ctx;
	dtag_t dtag = { .dtag = flush_misc->dtag_start[0] };

	ftl_l2p_put_vcnt_buf(dtag, flush_misc->dtag_cnt[0], false);

    if (shr_qbt_prog_err)
    {
        //ftl_apl_trace(LOG_INFO, 0, "qbt prog err, jump to FTL_ST_CLOSE_QBT");
        fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_CLOSE_QBT);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
    else
    {
    	ftl_apl_trace(LOG_INFO, 0x4bd3, "misc flushed done");
    	fsm_ctx_next(&fsm_ctx->fsm);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
}

/*!
 * @brief completion callback when descriptor was flushed continue to next step
 *
 * @param _ctx	should be state machine pointer
 *
 * @return	not used
 */
fast_code void flush_desc_done(void *_ctx)
{
	fsm_ctx_t *fsm = (fsm_ctx_t *) _ctx;
	flush_fsm_t *ctx = (flush_fsm_t *) _ctx;

	if (ctx->flags.b.make_ins_dirty == 0)
		fsm_ctx_next(fsm);
	else
		ctx->flags.b.make_ins_dirty = 0;

	fsm_ctx_run(fsm);
}
#if 0//(TRIM_SUPPORT == DISABLE)
fast_code void flush_st_suspend_trim_done(u32 nsid)
{
	ftl_apl_trace(LOG_INFO, 0xa1ec, "trim suspended");

	flush_fsm_t *ffsm = &_ffsm[nsid];
	fsm_ctx_next(&ffsm->fsm);
	fsm_ctx_run(&ffsm->fsm);
}


fast_code fsm_res_t flush_st_suspend_trim(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *)fsm;

	if (ftl_ns_is_clean(ctx->nsid)) {
		fsm->done(fsm);
		return FSM_QUIT;
	}
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_UCACHE_SUSP_TRIM, 0, 0);

	ftl_apl_trace(LOG_INFO, 0x24b6, "ffsm[%d]: suspend trim", fsm->state_cur);
	return FSM_PAUSE;
}

#endif



fast_code fsm_res_t flush_st_suspend_gc(fsm_ctx_t *fsm)
{
#if CROSS_TEMP_OP
	extern struct timer_list spb_scan_over_temp_blk_timer;
	del_timer(&spb_scan_over_temp_blk_timer);
#endif
	flush_fsm_t *ctx = (flush_fsm_t *)fsm;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
	gc_action_t *action = &ctx->gc_action;

	if(plp_trigger)
	{
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	shr_shutdownflag = true;
	gc_suspend_stop_next_spb = true;
	ftl_apl_trace(LOG_INFO, 0x6bd1, "set flag %d/%d", gc_suspend_stop_next_spb, shr_shutdownflag);

	if (flush_ctx->flags.b.format)
		action->act = GC_ACT_ABORT;
	else
		action->act = GC_ACT_SUSPEND;

	action->caller = ctx;
	action->cmpl = gc_suspend_done;

	if (gc_action(action))
		return FSM_CONT;

	ftl_apl_trace(LOG_INFO, 0x69d3, "ffsm[%d]: suspend gc", fsm->state_cur);
	return FSM_PAUSE;
}

slow_code fsm_res_t flush_st_wait_qbt(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *)fsm;
	ns_start_t *alloc = &ctx->qbt_alloc;
	extern volatile u32 dropsemi;

	spb_id_t qbt_tar;
	u8 loop;
	u8 cnt ;

	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	shr_qbtflag = true;
	dropsemi = 0;

	
#if (PLP_SUPPORT == 0)
	if(delay_flush_spor_qbt == false)//POR save ec table
	{
		extern void ftl_save_non_plp_ec_table(u8);
		ftl_save_non_plp_ec_table(1);
		//epm_update(FTL_sign,(CPU_ID-1)); 
	}
#endif
	
	/*Push SPB from SPB_POOL_FREE to SPB_POOL_QBT_FREE*/
	extern u32 min_build_sn;
	extern u8* TrimBlkBitamp;
	for (loop = 0; loop < FTL_QBT_BLOCK_CNT; loop++){	
		if(delay_flush_spor_qbt == true){
			again:
			qbt_tar = FTL_BlockPopHead(SPB_POOL_FREE);
			if(spb_get_sn(qbt_tar) > min_build_sn && test_bit(qbt_tar, TrimBlkBitamp)){
				//cur free is spor trim blk,can't use it
				  FTL_BlockPushList(SPB_POOL_FREE, qbt_tar, FTL_SORT_NONE);
				  goto again;				
			}
			for(cnt = 0; cnt < spor_read_pbt_cnt; cnt++){
				if(qbt_tar == spor_read_pbt_blk[cnt]){
				  FTL_BlockPushList(SPB_POOL_FREE, qbt_tar, FTL_SORT_NONE);
				  goto again;
				}					
			}
		}
		else{
		    qbt_tar = FTL_BlockPopHead(SPB_POOL_FREE);
            if (qbt_tar == last_spb_vcnt_zero)
            {
                spb_id_t tmp_tar = qbt_tar;
                qbt_tar = FTL_BlockPopHead(SPB_POOL_FREE);
                sys_assert(qbt_tar!=INV_SPB_ID);
                ftl_apl_trace(LOG_INFO, 0xc22d, "[spb]last_spb_vcnt_zero:%u, qbt_tar:%u", last_spb_vcnt_zero, qbt_tar);
    			FTL_BlockPushList(SPB_POOL_FREE, tmp_tar, FTL_SORT_BY_EC);
            }
		}
		sys_assert(qbt_tar != INV_U16);
		FTL_BlockPushList(SPB_POOL_QBT_FREE, qbt_tar, FTL_SORT_NONE);
		ftl_apl_trace(LOG_INFO, 0x2967, "[FTL]qbtblk %d to qbt free cnt %d sw_flag %d", qbt_tar, spb_get_free_cnt(SPB_POOL_QBT_FREE), spb_get_sw_flag(qbt_tar));
	}
	
	ftl_core_start(FTL_NS_ID_INTERNAL);// open qbt Curry 20201013
	alloc->type = FTL_CORE_NRM;
	alloc->nsid = FTL_NS_ID_INTERNAL;
	alloc->caller = ctx;
	alloc->cmpl = ftl_open_qbt_done;
	ftl_core_qbt_alloc(alloc);
	ftl_apl_trace(LOG_INFO, 0x9c07, "ffsm[%d]: wait_qbt", fsm->state_cur);
	return FSM_PAUSE;
}

fast_code fsm_res_t flush_st_flush_l2p(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *) fsm;
	ftl_flush_tbl_t *flush_tbl = &ctx->flush_tbl;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
	ffsm_start = get_tsc_64();
    shr_qbt_prog_err = false;
	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	extern u16 host_dummy_start_wl;
	host_dummy_start_wl = INV_U16;
	//if (flush_ctx->flags.b.format == false) {
		ftl_apl_trace(LOG_INFO, 0xed73, "ffsm[%d]: flush l2p %d", fsm->state_cur, ctx->nsid);
		flush_tbl->flags.all = 0;
		if(flush_ctx->flags.b.shutdown)
			flush_tbl->flags.b.shutdown = 1;

		
		flush_tbl->gc_spb = INV_SPB_ID;
		flush_tbl->nsid = ctx->nsid;
		flush_tbl->ctx.caller = ctx;
		flush_tbl->ctx.cmpl = flush_l2p_done;
		ftl_l2p_flush(flush_tbl);

		return FSM_PAUSE;
	//}

	//return FSM_CONT;
}

/*!
 * @brief callback function when all active spb was padded, continue to next step
 *
 * @param spb_pad	pad spb context
 *
 * @return		not used
 */
fast_code void spb_pad_done(ftl_core_ctx_t *ctx)
{
	flush_fsm_t *fsm_ctx = (flush_fsm_t *) ctx->caller;

	ftl_apl_trace(LOG_INFO, 0x1c1e, "open stripe & xlc spb pad done in %d ms", time_elapsed_in_ms(ffsm_start));

	fsm_ctx_next(&fsm_ctx->fsm);
	fsm_ctx_run(&fsm_ctx->fsm);
}

fast_code fsm_res_t flush_st_pad_spb(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *) fsm;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
	ftl_spb_pad_t *spb_pad = &ctx->spb_pad;
	ffsm_start = get_tsc_64();
	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	
	    gFtlMgr.TableSN = gFtlMgr.SerialNumber;
    gFtlMgr.PrevTableSN = gFtlMgr.SerialNumber;

	if (flush_ctx->flags.b.format == true) {
		return FSM_CONT;
	}
	
	ftl_apl_trace(LOG_INFO, 0x57ec, "ffsm[%d]: pad open stripe & xlc spb", fsm->state_cur);
	
	memset(spb_pad, 0, sizeof(ftl_spb_pad_t));
	spb_pad->spb_id = 0xFFFF;
	spb_pad->pad_attribution = pad_por;
	spb_pad->param.cwl_cnt = 0;
	spb_pad->param.pad_all = true;

	spb_pad->param.start_type = FTL_CORE_NRM;
	spb_pad->param.end_type = FTL_CORE_GC;
	spb_pad->param.start_nsid = 1;
	spb_pad->param.end_nsid = 1;

	spb_pad->cur.cwl_cnt = 0;
	spb_pad->cur.type = FTL_CORE_NRM;
	spb_pad->cur.nsid = 1;

	spb_pad->ctx.caller = ctx;
	spb_pad->ctx.cmpl = spb_pad_done;

	ftl_core_spb_pad(spb_pad);
	return FSM_PAUSE;
}

fast_code void qbt_close_done(ftl_core_ctx_t *ctx)
{
	flush_fsm_t *fsm_ctx = (flush_fsm_t *) ctx->caller;
	//ftl_apl_trace(LOG_INFO, 0, "[IN]qbt_close_done");
	if(plp_trigger){
		shr_qbtflag = false;
		fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_FLUSH_DONE);
		fsm_ctx_run(&fsm_ctx->fsm);
	}
	else if (shr_qbt_prog_err)
    {
        //ftl_apl_trace(LOG_INFO, 0, "qbt prog err, jump to FTL_ST_WAIT_QBT");
        fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_WAIT_QBT);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
    else if(qbt_done_cnt == QBT_BLK_CNT || shr_qbtfilldone == 1){
		ftl_apl_trace(LOG_INFO, 0xbe14, "qbt close done");
		fsm_ctx_next(&fsm_ctx->fsm);
		fsm_ctx_run(&fsm_ctx->fsm);
		qbt_done_cnt = 0;
	}
	else{
		//ftl_apl_trace(LOG_WARNING, 0, "qbt close done delay");
	}
}

fast_code fsm_res_t flush_st_new_close_qbt(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *) fsm;
	ftl_spb_pad_t *spb_pad = &ctx->spb_pad;

	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	ftl_apl_trace(LOG_INFO, 0x93ed, "ffsm[%d]: close_qbt start", fsm->state_cur);
	memset(spb_pad, 0, sizeof(ftl_spb_pad_t));
	spb_pad->spb_id = 0xFFFF;
	spb_pad->pad_attribution = pad_por;   // skip save qbt to avoid plp not done

	spb_pad->param.cwl_cnt = 0;
	spb_pad->param.pad_all = true;

	spb_pad->param.start_type = FTL_CORE_NRM;
	spb_pad->param.end_type = FTL_CORE_NRM;
	spb_pad->param.start_nsid = 2;
	spb_pad->param.end_nsid = 2;

	spb_pad->cur.cwl_cnt = 0;
	spb_pad->cur.type = FTL_CORE_NRM;
	spb_pad->cur.nsid = 2;

	spb_pad->ctx.caller = ctx;
	spb_pad->ctx.cmpl = qbt_close_done;

	ftl_core_spb_pad(spb_pad);
	return FSM_PAUSE;
}
fast_code fsm_res_t flush_st_flush_misc(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *) fsm;
	ftl_flush_misc_t *flush_misc = &ctx->flush_misc;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
    spb_id_t qbt_spb;
    
	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	if (flush_ctx->flags.b.format == false) {
		chk_close_blk_push();
		blk_shutdown_handle(SPB_SHUTDOWN);
	}
	
	/*Release SPB from SPB_POOL_QBT_ALLOC to SPB_POOL_FREE*/
	while(spb_get_free_cnt(SPB_POOL_QBT_ALLOC) > QBT_BLK_CNT){
        qbt_spb = spb_mgr_get_head(SPB_POOL_QBT_ALLOC);
        FTL_BlockPopPushList(SPB_POOL_FREE, qbt_spb, FTL_SORT_BY_EC);
		_spb_mgr.spb_desc[qbt_spb].ns_id = 0;
		_spb_mgr.spb_desc[qbt_spb].flags = 0;
		_spb_mgr.sw_flags[qbt_spb] = 0;
    }

	ftl_apl_trace(LOG_INFO, 0xec4a, "[FTL]release qbt, qbt alloc cnt %u, head:%u", spb_get_free_cnt(SPB_POOL_QBT_ALLOC), spb_mgr_get_head(SPB_POOL_QBT_ALLOC));

	spb_PurgePool2Free(SPB_POOL_PBT, FTL_SORT_BY_EC);		//TODO pis
	spb_PurgePool2Free(SPB_POOL_PBT_ALLOC, FTL_SORT_BY_EC);
	spb_PurgePool2Free(SPB_POOL_EMPTY, FTL_SORT_BY_EC);
	FTL_SearchGCDGC();
	
	spb_mgr_flush_desc();// update rd cnt

    gFtlMgr.GlobalPageSN = shr_page_sn;
	ftl_apl_trace(LOG_INFO, 0x69da, " gFtlMgr.GlobalPageSN:0x%x", shr_page_sn);

#if (WA_FW_UPDATE == ENABLE)
	spb_blk_info();
	spb_blk_list();
	spb_ftl_mgr_info();
#endif
	
#if (QBT_TLC_MODE == mENABLE)
		FTL_CopyFtlBlkDataToBuffer(0);
#endif

	//if (flush_ctx->flags.b.format == false) {
		ftl_apl_trace(LOG_INFO, 0xddd9, "ffsm[%d]: flush misc", fsm->state_cur);
		flush_misc->seg_start = ftl_int_ns_get_misc_pos();
	    //ftl_apl_trace(LOG_ERR, 0, "seg_start %d\n", flush_misc->seg_start);
		flush_misc->idx = 0;
		flush_misc->io_cnt = 0;
		flush_misc->ctx.caller = ctx;
		flush_misc->ctx.cmpl = flush_misc_done;
		ftl_l2p_misc_flush(flush_misc);
		return FSM_PAUSE;
	//}

	//return FSM_CONT;
}

fast_code fsm_res_t flush_st_flush_hns_desc(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *) fsm;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
	bool ret;
	l2p_idle_ctrl_t ctrl;
	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	ftl_apl_trace(LOG_INFO, 0xdb86, "ffsm[%d]: flush hns desc", fsm->state_cur);

	ctrl.all = 0;
	ctrl.b.wait = 1;
	ctrl.b.misc = 1;
	if (flush_ctx->flags.b.shutdown) {
		ctrl.b.all = 1;
		wait_l2p_idle(ctrl);
		ftl_host_ns_update_desc(ctx->nsid);
		ret = ftl_ns_make_clean(ctx->nsid, ctx, flush_desc_done);
		if (ret == true)
			return FSM_CONT;
	} else if (flush_ctx->flags.b.format) {
		ctrl.b.all = 1;
		wait_l2p_idle(ctrl);
		ftl_host_ns_update_desc(ctx->nsid);
		//ftl_ns_make_dirty(ctx->nsid, ctx, flush_desc_done, true);
		return FSM_CONT;
	} else if (flush_ctx->flags.b.spb_close) {
		wait_l2p_idle(ctrl);
		//ftl_ns_flush_desc(ctx->nsid, ctx, flush_desc_done); // Curry
	} else {
		wait_l2p_idle(ctrl);
		//ftl_apl_trace(LOG_INFO, 0, "ftl flush done");
		fsm->done(fsm);
		return FSM_QUIT;
	}

	return FSM_PAUSE;
}

fast_code fsm_res_t flush_st_flush_ins_desc(fsm_ctx_t *fsm)
{
	//flush_fsm_t *ctx = (flush_fsm_t *) fsm;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;

	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	if (is_spb_mgr_allocating(fsm)) {
		ftl_apl_trace(LOG_WARNING, 0x41d4, "wait for allocating");
		return FSM_QUIT;
	}

	ftl_apl_trace(LOG_INFO, 0x90cd, "ffsm[%d]: flush ins desc", fsm->state_cur);

	// need check if every host ns is clean, then flush all sys log
	ftl_int_ns_update_desc();
	//sys_log_cache_on();
	ftl_int_ns_flush_l2p();

	if (flush_ctx->flags.b.shutdown || flush_ctx->flags.b.spb_close)
		ftl_ns_dirty_spb_remove(flush_ctx->nsid);

	//spb_mgr_flush_desc();
	/*
	if (flush_ctx->flags.b.format)
		//ftl_ns_make_dirty(FTL_NS_ID_INTERNAL, ctx, flush_desc_done, true);

	else
		ftl_ns_make_clean(FTL_NS_ID_INTERNAL, ctx, flush_desc_done);
	*/
	//sys_log_cache_off();
	//return FSM_PAUSE;
	return FSM_CONT;
}

fast_code void flush_done_dump(void)
{
	spb_mgr_dump();
	ftl_ns_dump();
}

fast_code fsm_res_t flush_st_flush_done(fsm_ctx_t *fsm)
{
	//flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
    epm_FTL_t* epm_ftl_data;

	if(plp_trigger)
	{
		shr_qbtflag = false;
		return FSM_QUIT;
	}
	else
	{
        #ifdef Dynamic_OP_En
        // need to add EPM flag to indicate set op is ended, Sunny 20211028
        epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
        epm_ftl_data->Set_OP_Start = INV_U32;
        #endif

#if (QBT_TLC_MODE == mENABLE)
#if (SPOR_FLOW == mDISABLE)	
	u32 qbt_sn;
	u16 qbt_blk;		
	qbt_blk = spb_mgr_get_head(SPB_POOL_QBT_ALLOC);
	qbt_sn = spb_get_sn(qbt_blk);
	frb_log_set_qbt_info(0x1, qbt_blk, qbt_sn);
	frb_log_type_update(FRB_TYPE_HEADER);
	ftl_apl_trace(LOG_INFO, 0x8265, "update ftl qbt tag, valid qbt blk idx %d", qbt_blk);
#endif
#endif
		epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		u16    spb_id;
	    for(spb_id = 0; spb_id < get_total_spb_cnt(); spb_id++)
	    {
	        epm_ftl_data->epm_ec_table[spb_id] = spb_info_tbl[spb_id].erase_cnt;
	    }
#if PLP_SUPPORT
	extern volatile u32 spor_qbtsn_for_epm;
	extern volatile bool delay_flush_spor_qbt;
	if(spor_qbtsn_for_epm != 0 && delay_flush_spor_qbt == false)
	{
		epm_ftl_data->spor_last_rec_blk_sn = spor_qbtsn_for_epm;//record cur qbt sn 
		spor_qbtsn_for_epm = 0;
	}
#endif
	ftl_apl_trace(LOG_INFO, 0xafc6, "ftl flush done");

#if (SPB_BLKLIST == mENABLE)
	spb_pool_table();
#endif

#if CROSS_TEMP_OP
	extern bool is_scan_over_temp_timer_del;
	is_scan_over_temp_timer_del = true;
#endif
    gc_spb_last_idx = INV_U16;
	host_spb_last_idx = INV_U16;
	shr_shutdownflag = false;
	gc_suspend_stop_next_spb = false;
    ftl_pbt_cnt = 0;
	qbt_done_cnt = 0;
	shr_qbtfilldone = 0;

	shr_qbtflag = false;
	ftl_apl_trace(LOG_INFO, 0x56b0, "initial gc_suspend_stop_next_spb %d shr_shutdownflag %d", gc_suspend_stop_next_spb, shr_shutdownflag);
	fsm->done(fsm);
	return FSM_QUIT;
	}
}

/*! @} */
