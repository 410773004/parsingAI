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
/*! \file
 * @brief rainier disk support
 *
 * \addtogroup dispatcher
 * \defgroup rainier
 * \ingroup dispatcher
 * @{
 *
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
#include "btn_export.h"
#include "bf_mgr.h"
#include "mod.h"
#include "event.h"
#include "assert.h"
#include "l2cache.h"
#include "l2p_mgr.h"
#include "mpc.h"
#include "rdisk.h"
#include "ftl_export.h"
#include "fc_export.h"
#include "ipc_api.h"
#include "ncl_exports.h"
#include "ncl.h"
#include "cbf.h"
#include "pmu.h"
#include "die_que.h"
#include "misc.h"
#include "ncl_cmd.h"
#include "GList.h"

#if TCG_WRITE_DATA_ENTRY_ABORT
#include "tcgcommon.h"
#endif

/*! \cond PRIVATE */
#define __FILEID__ gone
#include "trace.h"
/*! \endcond */
#include "btn_helper.h"

#if WR_DTAG_TYPE == DTAG_T_SRAM
#define DTAG_OTHER_AVAIL 	8	///< this setting can't be run anymore
#else
#define DTAG_OTHER_AVAIL	8	///< don't insert too much dtag to avoid free link list overflow
#endif
#ifdef STOP_BG_GC
share_data_zi volatile bool rd_gc_flag;
share_data_zi volatile u8 shr_gc_read_disturb_ctrl;
#endif

share_data_ni volatile draf_comt_t shr_dref_inc;			///< dtag reference count increase queue
share_data_ni volatile draf_comt_t shr_dref_dec;

fast_data_zi struct timer_list loop_feedback_timer;

fast_data_zi struct timer_list _fc_timer;				///< flow control feed timer
share_data_zi volatile u8 shr_gc_fc_ctrl;
share_data_zi volatile u32 shr_gc_perf;
share_data_zi volatile u32 shr_gc_rel_du_cnt;
share_data_zi volatile u32 shr_tbl_flush_perf;
share_data_zi volatile ftl_flags_t shr_ftl_flags;
share_data_zi volatile u32 *shr_l2p_ready_bmp;
fast_data_zi u32 free_pushed = 0;
fast_data_zi u32 cur_iops;
fast_data u8 evt_wr_exec_chk= 0xFF;
fast_data u8 evt_wr_pending_abort = 0xFF;
extern fast_data_ni struct timer_list refresh_read_timer;
extern fast_data u8 evt_gc_release_dtag_chk;
extern u32 shr_bew_perf;

share_data epm_info_t*  shr_epm_info;
share_data volatile bool smResetSemiDtag;
share_data_zi volatile bool smStopPushDdrDtag;

share_data_zi volatile bool shr_is_gc_force;
extern volatile u8 cpu_feedback[];

#ifdef ERRHANDLE_ECCT
share_data_zi volatile stECC_table *pECC_table;
share_data volatile u16 ecct_cnt;  //ecct cnt in sram  //tony 20201228
//extern u16 ecct_cnt;  //ecct cnt in sram  //tony 20201228
extern stECCT_ipc_t rc_ecct_info[MAX_RC_REG_ECCT_CNT];
extern u8 rc_ecct_cnt;
#endif
fast_data_zi u32 pg_cnt_in_xlc_spb;


#if DDR_DTAG_CNT > 3500 //??need fix
#error "setting error"
#endif
#define RDISK_FDTAG_SZ DDR_DTAG_CNT

fast_data_zi u32 rdisk_fdtag_pool[RDISK_FDTAG_SZ];
fast_data_zi u32 rdisk_fdtag_rptr;
fast_data_zi u32 rdisk_fdtag_wptr;

#if 0//TCG_WRITE_DATA_ENTRY_ABORT
extern tcg_io_chk_func_t tcg_io_chk_range;
extern u16 mWriteLockedStatus;
#endif

extern u32 btn_free_wr_entry[];
extern bool dref_comt_poll(void);
extern u32 bm_free_wr_get_wptr(void);
extern u32 bm_free_wr_get_rptr(void);
extern void bm_free_wr_set_wptr(u32 wptr);
static void bcmd_exec(btn_cmd_t *bcmd, int btag);

extern u32 global_gc_mode; // ISU, GCRdFalClrWeak

extern u32 shr_pstream_busy;
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
//static inline void rdisk_rdtag_ins_list(bool gc)
static inline void rdisk_rdtag_ins_list(u8 gc)
{
	if (smStopPushDdrDtag)
	{
		return;
	}

	if (rdisk_fdtag_wptr == rdisk_fdtag_rptr)
		return;

	u8 b_wptr = bm_free_wr_get_wptr();
	u8 b_rptr = bm_free_wr_get_rptr();
	u8 b_wptr_next = (b_wptr + 1) & 0xFF;
	#ifdef WCMD_DROP_SEMI
    u8 bypass=0;
    #endif
    //if(gc)
    //if(gc == true)
    if(gc == true)
	{
		if(shr_pstream_busy == true && !_fc_credit)  // when block erasing, using cache to supply
			_fc_credit = 1;
		#ifdef WCMD_DROP_SEMI
		extern u32 dropsemi;
        if((global_gc_mode==1)&&(dropsemi >=100)){
            bypass = 1;
        }
		while ((b_wptr_next != b_rptr) && (_fc_credit||bypass))
		#else
        while ((b_wptr_next != b_rptr) && _fc_credit)
        #endif
		{
            if(_fc_credit > 0)
				_fc_credit--;
            cur_iops++;
			btn_free_wr_entry[b_wptr] = rdisk_fdtag_pool[rdisk_fdtag_rptr];
			b_wptr = (b_wptr + 1) & 0xFF;
			b_wptr_next = (b_wptr + 1) & 0xFF;
			rdisk_fdtag_rptr++;
			if (rdisk_fdtag_rptr >= RDISK_FDTAG_SZ)
				rdisk_fdtag_rptr = 0;
			if (rdisk_fdtag_rptr == rdisk_fdtag_wptr)
				break;
		}
	}
	else
	{
	  while (b_wptr_next != b_rptr) 
	  {
          cur_iops++; 
		  btn_free_wr_entry[b_wptr] = rdisk_fdtag_pool[rdisk_fdtag_rptr];
		  b_wptr = (b_wptr + 1) & 0xFF;
		  b_wptr_next = (b_wptr + 1) & 0xFF;
      rdisk_fdtag_rptr++;
      if (rdisk_fdtag_rptr >= RDISK_FDTAG_SZ)
        rdisk_fdtag_rptr = 0;
		  if (rdisk_fdtag_rptr == rdisk_fdtag_wptr)
			  break;
	  }
	  if (gc == 2){  // clear after execution
	  	shr_gc_fc_ctrl = false;
		_fc_credit = 0;
	  	evlog_printk(LOG_ERR,"clear fc %d wptr %d rptr %d b_wptr_n %d b_rptr %d",_fc_credit,rdisk_fdtag_wptr,rdisk_fdtag_rptr,b_wptr_next,b_rptr);
	  }
  }

	bm_free_wr_set_wptr(b_wptr);
}
slow_code void btn_wcmd_timeout_dump(void)
{
	u32 sel, sts;
	disp_apl_trace(LOG_INFO, 0x367a, "[BTN] WD1|%x FW_ISU|%x SEMI|%x", readl((void *) 0xc0010164), readl((void *) 0xc0010094), readl((void *) 0xc0010074));
	disp_apl_trace(LOG_INFO, 0x81a2, "[BTN] LLST:  CTRL|0x%x ENTRY|0x%x", readl((void *)0xc0010080), readl((void *)0xc0010100));
	disp_apl_trace(LOG_INFO, 0x5b3e, "[FW ] ftl flag %x pend_list_empty %d", shr_ftl_flags.all, bcmd_list_empty(&bcmd_pending));
	disp_apl_trace(LOG_INFO, 0x9e98, "_fc_credit %d rdisk_fdtag_wptr %d  rdisk_fdtag_rptr %d ",_fc_credit,rdisk_fdtag_wptr,rdisk_fdtag_rptr);

	for (sel = 0; sel < 10; sel++)
	{
		writel(sel, (void *)(0xC0010104));
		sts = readl((void *)(0xC0010108));
		disp_apl_trace(LOG_INFO, 0x9e83, "[LLST] :%d %x", sel, sts);
	}

	AplDieQueue_DumpInfo(false);

	cpu_msg_issue(CPU_FE - 1, CPU_MSG_ONDEMAND_DUMP, 0, 1);
	cpu_msg_issue(CPU_BE - 1 , CPU_MSG_ONDEMAND_DUMP, 0, 1);
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_ONDEMAND_DUMP, 0, 1);
}

static fast_code void remote_dtag_put(volatile cpu_msg_req_t *req)
{
	dtag_t dtag = { .dtag = req->pl };
	dref_comt_poll();
	int t = get_dtag_type(dtag);
	if ((t == DTAG_T_DDR) && (dtag_get_avail_cnt(DTAG_T_DDR) >= DTAG_OTHER_AVAIL)) {
#if defined(FREE_DTAG_PRELOAD)
		if (dtag_get_ref(DTAG_T_DDR, dtag) == 1) {
			rdisk_fdtag_pool[rdisk_fdtag_wptr] = dtag.dtag;
            rdisk_fdtag_wptr++;
            if (rdisk_fdtag_wptr >= RDISK_FDTAG_SZ)
              rdisk_fdtag_wptr = 0;

			sys_assert(rdisk_fdtag_rptr != rdisk_fdtag_wptr);
		} else
			dtag_ref_dec(DTAG_T_DDR, dtag);
#else
		dtag_put(DTAG_T_DDR, dtag);
#endif
	} else {
		dtag_put_ex(dtag);
	}
}

fast_code void ref_cnt_adder(volatile cpu_msg_req_t *req)
{
	dtag_t dtag = { .dtag = req->pl };

	dtag_ref_inc(DTAG_T_SRAM, dtag);
}

fast_code void dref_inc_comt(void)
{
	u32 rptr = shr_dref_inc.rptr;
	u32 end = shr_dref_inc.wptr;
	u32 size = shr_dref_inc.size;
	u32 cnt;

	if (rptr > end) {
		cnt = size - rptr;
		dtag_ref_inc_bulk(DTAG_T_MAX, (dtag_t *)&shr_dref_inc.buf[rptr], cnt);
		rptr = 0;
	}

	shr_dref_inc.rptr = rptr;

	if (rptr < end) {
		cnt = end - rptr;
		dtag_ref_inc_bulk(DTAG_T_MAX, (dtag_t *)&shr_dref_inc.buf[rptr], cnt);
		rptr = end;
	}

	shr_dref_inc.rptr = rptr;
}

fast_code void dref_dec_comt(void)
{
	extern u32 dref_dec_wptr_fence;
	u32 rptr = shr_dref_dec.rptr;
	u32 end = dref_dec_wptr_fence;
	u32 size = shr_dref_dec.size;
	u32 cnt;

	int i = 0;
	if (rptr > end) {
		cnt = size - rptr;
		for (i = 0; i < cnt; i++) {
			dtag_t dtag = { .dtag = shr_dref_dec.buf[rptr + i] };
			int t = get_dtag_type(dtag);
			if ((t == DTAG_T_DDR) && (dtag_get_avail_cnt(DTAG_T_DDR) >= DTAG_OTHER_AVAIL)) {
#if defined(FREE_DTAG_PRELOAD)
				if (dtag_get_ref(DTAG_T_DDR, dtag) == 1) {
					rdisk_fdtag_pool[rdisk_fdtag_wptr] = dtag.dtag;
                    rdisk_fdtag_wptr++;
                    if (rdisk_fdtag_wptr >= RDISK_FDTAG_SZ)
                      rdisk_fdtag_wptr = 0;

					sys_assert(rdisk_fdtag_rptr != rdisk_fdtag_wptr);
				} else
					dtag_ref_dec(DTAG_T_DDR, dtag);
#else
				dtag_put(DTAG_T_DDR, dtag);
#endif
			} else {
				dtag_put_ex(dtag);
			}
		}
		rptr = 0;
	}

	shr_dref_dec.rptr = rptr;

	if (rptr < end) {
		cnt = end - rptr;
		for (i = 0; i < cnt; i++) {
			dtag_t dtag = { .dtag = shr_dref_dec.buf[rptr + i] };
			int t = get_dtag_type(dtag);
			if ((t == DTAG_T_DDR) && (dtag_get_avail_cnt(DTAG_T_DDR) >= DTAG_OTHER_AVAIL)) {
#if defined(FREE_DTAG_PRELOAD)
				if (dtag_get_ref(DTAG_T_DDR, dtag) == 1) {
					rdisk_fdtag_pool[rdisk_fdtag_wptr] = dtag.dtag;
                    rdisk_fdtag_wptr++;
                    if (rdisk_fdtag_wptr >= RDISK_FDTAG_SZ)
                      rdisk_fdtag_wptr = 0;

					sys_assert(rdisk_fdtag_rptr != rdisk_fdtag_wptr);
				} else
					dtag_ref_dec(DTAG_T_DDR, dtag);
#else
				dtag_put(DTAG_T_DDR, dtag);
#endif
			} else {
				dtag_put_ex(dtag);
			}
		}

		rptr = end;
	}

	shr_dref_dec.rptr = rptr;

  rdisk_rdtag_ins_list(shr_gc_fc_ctrl);
}

#if defined(FREE_DTAG_PRELOAD)
static inline void rdisk_reload_free_dtag(void)
{
	u32 total = 0;
	while (dtag_get_avail_cnt(WR_DTAG_TYPE) > DTAG_OTHER_AVAIL) {
		dtag_t dtag = dtag_get(WR_DTAG_TYPE, NULL);
		sys_assert(dtag.dtag != _inv_dtag.dtag);
		rdisk_fdtag_pool[rdisk_fdtag_wptr] = dtag.dtag;
		rdisk_fdtag_wptr++;
        if (rdisk_fdtag_wptr >= RDISK_FDTAG_SZ)
          rdisk_fdtag_wptr = 0;

		total++;
	}
	disp_apl_trace(LOG_ERR, 0x82a7, "prepare rdisk fdtag %d\n", total);
	rdisk_rdtag_ins_list(false);
}
#else
fast_code void rdisk_load_free_dtag(u32 ndu)
{
	load_free_dtag(ndu);
}
#endif

static fast_code void wr_credit_handler(volatile cpu_msg_req_t *req)
{
#if defined(FREE_DTAG_PRELOAD)
	rdisk_reload_free_dtag();
#else
	load_free_dtag(req->pl);
#endif
}

/*!
 * @1 second record backend write IO
 *
 * @param data		not used
 *
 * @return		not used
 */
fast_code void rdisk_fc_timer(void *data)
{
  shr_bew_perf = cur_iops;  // bew IOPS
  cur_iops = 0;
  mod_timer(&_fc_timer, jiffies + HZ);//adams 1s
}

slow_code void CPU4_send_loop_feedback_timer(void *data)
{
	//disp_apl_trace(LOG_ALW, 0xaec7, "CPU4 timer trigger %d %d %d %d",cpu_feedback[0],cpu_feedback[1],cpu_feedback[2],cpu_feedback[3]);

	u32 all_feedback = 0;
	extern volatile bool all_init_done;
	extern volatile u8 plp_trigger;
	extern void Save_info_before_error(u8 type);
	
	if(all_init_done == false || plp_trigger )
	{
		mod_timer(&loop_feedback_timer, jiffies + 60*HZ);//1 min
		return;
	}

	for(u8 i = 0; i < MPC; i++)
	{
		all_feedback += cpu_feedback[i];
	}
	
	if(all_feedback != MPC)
	{
		disp_apl_trace(LOG_ALW, 0x8576, "[ERR] %d %d %d %d",cpu_feedback[0],cpu_feedback[1],cpu_feedback[2],cpu_feedback[3]);
		Save_info_before_error(SAVE_MODE_FEEDBACK_CHK);
		flush_to_nand(EVT_CPU_FEEDBACK_FAIL);
		return;//only once , avoid fill up NAND log
	}
	
	cpu_feedback[0] = cpu_feedback[1] = cpu_feedback[2] = cpu_feedback[3] = false;

	//cpu_feedback[CPU_ID] = false;
	cpu_msg_issue(CPU_FE - 1,   CPU_MSG_LOOP_FEEDBACK, 0, 1);
	cpu_msg_issue(CPU_BE - 1,   CPU_MSG_LOOP_FEEDBACK, 0, 1);
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_LOOP_FEEDBACK, 0, 1);
	cpu_feedback[3] = true;
	mod_timer(&loop_feedback_timer, jiffies + 60*HZ);//1 min	
}

fast_code void rdisk_fc_rel_du()
{
	if (shr_gc_rel_du_cnt){
	  _fc_credit += shr_gc_rel_du_cnt;
	  shr_gc_rel_du_cnt = 0;//cpu4
	}

#if defined(FREE_DTAG_PRELOAD)
	if (_fc_credit >= 0)
		rdisk_rdtag_ins_list(shr_gc_fc_ctrl);
#else
	reload_free_dtag(NULL);
#endif
}

ps_code bool rdisk_be_suspend(enum sleep_mode_t mode)
{
	dtag_t dtag;
	u32 cnt = 0;

	do {
		dtag = bm_pop_dtag_llist(FREE_WR_DTAG_LLIST);

		if (dtag.dtag != DTAG_INV) {
			dtag_put(WR_DTAG_TYPE, dtag);
			cnt++;
		}

	} while (dtag.dtag != DTAG_INV);
	disp_apl_trace(LOG_ERR, 0xb9af, "restore %d\n", cnt);
	return true;
}

ps_code void rdisk_be_resume(enum sleep_mode_t mode)
{
	disp_apl_trace(LOG_ERR, 0x46b6, "ins %d\n", dtag_get_avail_cnt(WR_DTAG_TYPE) - DTAG_OTHER_AVAIL);
	ins_free_dtag_for_wr(WR_DTAG_TYPE, dtag_get_avail_cnt(WR_DTAG_TYPE) - DTAG_OTHER_AVAIL);
}

fast_code void rdisk_be_wr_exec_chk(u32 r0, u32 r1, u32 r2)
{
	if (!(shr_ftl_flags.b.l2p_all_ready && shr_ftl_flags.b.ftl_ready)) {
		evt_set_cs(evt_wr_exec_chk, 0, 0, CS_TASK);
		return;
	}

	u16 btag;
	btn_cmd_t *bcmd;
	bcmd_list_t local = { .head = 0xFFFF, .tail = 0xFFFF};

	if (bcmd_list_empty(&bcmd_pending))
		return;

	bcmd_list_move(&local, &bcmd_pending);

	do {
		btag = bcmd_list_pop_head(&local);
		bcmd = btag2bcmd(btag);

		bcmd_exec(bcmd, btag);
	} while (!bcmd_list_empty(&local));
}

fast_code void rdisk_be_wr_pending_abort(u32 r0, u32 r1, u32 r2)
{
	//disp_apl_trace(LOG_INFO, 0, "[BCMD] w pend chk");
	while (!bcmd_list_empty(&bcmd_pending))
	{
		bcmd_list_pop_head(&bcmd_pending);
	}
}

fast_code void bcmd_exec(btn_cmd_t *bcmd, int btag)
{
	sys_assert(bcmd->dw0.b.cmd_type == NVM_WRITE ||
			bcmd->dw0.b.cmd_type == IO_MGR);

    btn_cmd_ex_t *cmd_ex = btag2bcmd_ex(btag);
    u64 slba = bcmd_get_slba(bcmd);

#ifdef STOP_BG_GC
    if(bcmd->dw0.b.cmd_type == NVM_WRITE){ // && !(global_gc_mode == 4) // ISU, EHPerfImprove
        if(shr_is_gc_force){
            if(shr_gc_read_disturb_ctrl){
                shr_gc_read_disturb_ctrl = false;
                shr_gc_fc_ctrl = true;
                _fc_credit = 0;
            }
        }
    }
#endif

#ifdef ERRHANDLE_ECCT
    //epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    //pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
    //if(pECC_table->ecc_table_cnt)     //Search same idx of lda in ecct
    if(ecct_cnt)
    {
        //stECCT_ipc_t ecct_info;
        rc_ecct_info[rc_ecct_cnt].lba       = slba;
        rc_ecct_info[rc_ecct_cnt].source    = ECC_REG_VU;
        if(host_sec_bitz == 9)
        {
            rc_ecct_info[rc_ecct_cnt].total_len = (u32)(cmd_ex->ndu << (LDA_SIZE_SHIFT - host_sec_bitz));
        }
        else
        {
            rc_ecct_info[rc_ecct_cnt].total_len = (u32)cmd_ex->ndu;
        }
        rc_ecct_info[rc_ecct_cnt].type      = VSC_ECC_unreg;

        //disp_apl_trace(LOG_INFO, 0, "[BCMD] Bcmd_exec in write cmd, ecct_cnt: 0x%x, slba: 0x%x", ecct_cnt, slba);
        if(rc_ecct_cnt >= MAX_RC_REG_ECCT_CNT - 1)
        {
            rc_ecct_cnt = 0;
            ECC_Table_Operation(&rc_ecct_info[MAX_RC_REG_ECCT_CNT-1]);
        }
        else
        {
            rc_ecct_cnt++;
            ECC_Table_Operation(&rc_ecct_info[rc_ecct_cnt-1]);
        }
    }
#endif

	if (!bcmd_list_empty(&bcmd_pending)) {
		bcmd_list_ins(btag, &bcmd_pending);

		return;
	}

	if (!(shr_ftl_flags.b.l2p_all_ready && shr_ftl_flags.b.ftl_ready)) {
		bcmd_list_ins(btag, &bcmd_pending);
		evt_set_cs(evt_wr_exec_chk, 0, 0, CS_TASK);

		return;
	}

	if (bcmd->dw0.b.cmd_type != NVM_WRITE)
	{
		btn_iom_cmd_rels(bcmd);
	}
	else
	{
		//extern vu32 Wrcmd;
		//Wrcmd ++;
		//disp_apl_trace(LOG_ERR, 0, "write cmd cnt %d \n", Wrcmd);
		//btn_cmd_ex_t *cmd_ex = btag2bcmd_ex(btag);
		//u64 slba = bcmd_get_slba(bcmd);
#if TCG_WRITE_DATA_ENTRY_ABORT
		// todo tcg_wr_abrt = 1 or 0
		if(mTcgActivated) //if(mTcgStatus)
		{
			if(tcg_io_chk_range(slba, bcmd->dw3.b.xfer_lba_num, mWriteLockedStatus))
				cmd_ex->flags.b.tcg_wr_abrt = true;
		}
#endif
		extern bool esr_err_fua_flag;
		if(esr_err_fua_flag)// Ensure fua write done when feature 06h switch (Eric 20240502)
		{
			bcmd->dw1.b.fua =1;
		}
		else
		{
			#if(PLP_SUPPORT == 1) // [PCBaher] Not support cmd FUA mode in PLP model avoid delete Q issue
			if (bcmd->dw1.b.fua)
			{
				bcmd->dw1.b.fua = 0;
			}
			#endif
		}
		cmd_ex->flags.b.fua = bcmd->dw1.b.fua;
		cmd_ex->flags.b.wr_err = ts_io_block ? true : false;
		cmd_ex->ndu = calc_du_cnt(slba, bcmd->dw3.b.xfer_lba_num);
		cmd_ex->du_xfer_left = cmd_ex->ndu;

#if !defined(FREE_DTAG_PRELOAD)
		extern __attribute__((weak)) void rdisk_load_free_dtag(u32);
		if (rdisk_load_free_dtag)
			rdisk_load_free_dtag(cmd_ex->ndu);
#else
		#ifndef WCMD_USE_IRQ
		rdisk_rdtag_ins_list(shr_gc_fc_ctrl);  // Polling mode force supply free Dtag when receive write command, especially in erase SPB, and cache will control
		#endif
#endif
	}
}

fast_code void rc_dtag_evt(void *ctx)
{
	dtag_t dtag = dtag_get(DTAG_T_SRAM, NULL);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
    disp_apl_trace(LOG_ERR, 0x5e17, "ipc alloc rc get dtag:%d\n", dtag.dtag);
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_RC_DTAG_ALLOCED, 0, dtag.dtag);
}


fast_code void ipc_alloc_rc_dtag(volatile cpu_msg_req_t *req)
{
	dtag_t dtag = dtag_get(DTAG_T_SRAM, NULL);
    disp_apl_trace(LOG_ERR, 0xc259, "ipc alloc rc get dtag:%d\n", dtag.dtag);
	if (dtag.dtag != _inv_dtag.dtag)
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_RC_DTAG_ALLOCED, 0, dtag.dtag);
	else
		dtag_register_evt(DTAG_T_SRAM, rc_dtag_evt, NULL, false);
}

fast_code void ipc_free_rc_dtag(volatile cpu_msg_req_t *req)
{
	dtag_t dtag;

	dtag.dtag = req->pl;
	dtag_put(DTAG_T_SRAM, dtag);
}

#if CPU_ID == CPU_DTAG
fast_code void semi_ddrdtag_resume(volatile cpu_msg_req_t *req)
{
	#if !defined(RAMDISK_FULL)
	extern u32 ddr_cache_cnt;
	if (CPU_DTAG == CPU_ID) {

		#ifdef RAMDISK
		u32 sz = DDR_DTAG_CNT * 2;
		#else
		u32 sz = ddr_cache_cnt;
		#endif

		disp_apl_trace(LOG_INFO, 0xbb86, "reset semi ddrdtag ddr_cache_cnt:%d",ddr_cache_cnt);
		dtag_mgr_init(DTAG_T_DDR, sz, 4);
		dtag_add(DTAG_T_DDR, (void *) ddtag2mem(DDR_IO_DTAG_START), sz << DTAG_SHF);
	}
	#endif
	rdisk_fdtag_rptr = rdisk_fdtag_wptr = 0;

	//for jira_10. solution(12/12).shengbin yang 2023/11/15
	CBF_INIT(&shr_dref_inc);
	CBF_INIT(&shr_dref_dec);

	smStopPushDdrDtag = false;
	rdisk_reload_free_dtag();
	smResetSemiDtag = false;
}
#endif

/*!
 * @brief rdisk backend initialize
 * Register all buffer manager event handler functions.
 *
 * @return	not used
 */
init_code static void rdisk_init_be(void)
{
	CBF_INIT(&shr_dref_inc);
	CBF_INIT(&shr_dref_dec);
	dref_inc_comt_recv_hook((cbf_t *)&shr_dref_inc, dref_inc_comt);
	dref_dec_comt_recv_hook((cbf_t *)&shr_dref_dec, dref_dec_comt);

	evt_register(rdisk_be_wr_exec_chk, 0, &evt_wr_exec_chk);
	evt_register(rdisk_fc_rel_du, 0, &evt_gc_release_dtag_chk);
//	evt_register(rdisk_fc_timer, 0, &evt_gc_release_dtag_chk);
	evt_register(rdisk_be_wr_pending_abort, 0, &evt_wr_pending_abort);
	cpu_msg_register(CPU_MSG_DTAG_PUT, remote_dtag_put);
	cpu_msg_register(CPU_MSG_ADD_REF, ref_cnt_adder);
	cpu_msg_register(CPU_MSG_WR_CREDIT, wr_credit_handler);
    cpu_msg_register(CPU_MSG_RC_DTAG_ALLOC, ipc_alloc_rc_dtag);
	cpu_msg_register(CPU_MSG_RC_DTAG_FREE, ipc_free_rc_dtag);
	#if CPU_ID == CPU_DTAG
	cpu_msg_register(CPU_MSG_RESUME_SEMI_DDR_DTAG, semi_ddrdtag_resume);
	#endif
#ifdef ERRHANDLE_ECCT
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
    ecct_cnt = pECC_table->ecc_table_cnt;     //Search same idx of lda in ecct
    //disp_apl_trace(LOG_INFO, 0, "[DBG] rdisk_init_be: INIT ECCT cnt: 0x%x", ecct_cnt);
#endif

	btn_cmd_hook(bcmd_exec, NULL);

	_fc_credit = 0;
	shr_pstream_busy = false;

	pmu_register_handler(SUSPEND_COOKIE_CACHE, rdisk_be_suspend, RESUME_COOKIE_CACHE, rdisk_be_resume);
	INIT_LIST_HEAD(&_fc_timer.entry);
	_fc_timer.function = rdisk_fc_timer;
	_fc_timer.data = NULL;
	mod_timer(&_fc_timer, jiffies);

	INIT_LIST_HEAD(&loop_feedback_timer.entry);
	loop_feedback_timer.function = CPU4_send_loop_feedback_timer;
	loop_feedback_timer.data = NULL;
	mod_timer(&loop_feedback_timer, jiffies + 60*HZ);//1 min	
	
	cpu_feedback[0] = cpu_feedback[1] = cpu_feedback[2] = cpu_feedback[3] = true;

#if defined(FREE_DTAG_PRELOAD)
	rdisk_reload_free_dtag();
#endif

}

/*! \cond PRIVATE */
module_init(rdisk_init_be, DISP_APL);
/*! \endcond */
/*! @} */
