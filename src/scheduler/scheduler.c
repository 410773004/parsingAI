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
/*! \file scheduler.c
 * @brief scheduler module
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "mod.h"
#include "types.h"
#include "bf_mgr.h"
#include "l2p_mgr.h"
#include "queue.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "ipc_api.h"
#include "die_que.h"
#include "erase.h"
#include "addr_trans.h"
#include "fc_export.h"
#include "mpc.h"
#include "event.h"
#include "string.h"
#include "idx_meta.h"
#include "ftl_remap.h"
#include "pmu.h"
#include "bitops.h"
#include "ddr.h"
#include "spin_lock.h"
#include "scheduler.h"
#include "read_error.h"
#include "rdisk.h"
#include "srb.h"
#include "ssstc_cmd.h"
#include "vic_register.h"
#include "vic_id.h"

#if defined(TCG_NAND_BACKUP)
#include "../tcg/tcg_nf_mid.h"
extern tcg_mid_info_t *tcg_mid_info;
#endif

/*! \cond PRIVATE */
#define __FILEID__ schl
#include "trace.h"
/*! \endcond */

//#define GC_DDTAG_OFF			DDR_DTAG_CNT			///< gc ddr dtag start offset
#define GC_DTAG_IN_DDR			DTAG_IN_DDR_MASK
#define GC_DTAG_META_LOC		META_DDR_DTAG

share_data volatile u16 ua_btag;							///< unalign btag
#if (CO_SUPPORT_READ_AHEAD == TRUE)
share_data u16 ra_btag;							///< RA btag
#endif
share_data volatile u32 shr_gc_ddtag_start;
share_data_ni volatile u32 shr_l2p_srch_nid[BTN_CMD_CNT];		///< bcmd nvmd id
share_data gc_src_data_que_t *gc_src_data_que;
share_data volatile gc_res_free_que_t *gc_res_free_que;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
share_data_zi pda_t shr_plp_slc_end_pda;
#endif

extern u8 Vu_temp;
extern u16 Vsc_on;
//extern u8 cur_ce;   //Sean_20220506

fast_data_ni gc_scheduler_t gc_scheduler;
fast_data_ni pda_gen_rsp_t _vpda_rsp_entry[PDA_RSP_PER_GRP] ALIGNED(32);

fast_data u32 rd_dummy_meta_idx = ~0;					///< dummy meta index
fast_data struct du_meta_fmt *dummy_meta = NULL;			///< dummy meta

fast_data u32 wr_dummy_meta_idx = ~0;					///< write dummy meta index
fast_data struct du_meta_fmt *wr_dummy_meta = NULL;			///< write dummy meta

fast_data u8 evt_sched_mon = 0xFF;					///< polling event to check all die mgr idle
fast_data u16 slc_parity_interleave = 0xFFFF;
fast_data_ni bool stream_drop;					///< flags to drop stream entries

share_data_ni dtag_t cache_read_dtags[];
#if (CO_SUPPORT_READ_AHEAD == TRUE)
share_data_zi u32 shr_ra_ddtag_start;
#endif

//for vu
extern AGING_TEST_MAP_t *MPIN;
extern stNOR_HEADER *InfoHeader;
extern AgingPlistBitmap_t *AgingPlistBitmap;
extern AgingPlistTable_t *AgingP1listTable;
extern AgingPlistTable_t *AgingP2listTable;
extern CTQ_t *Aging_CTQ;
extern AgingPlistBitmap_t *AgingPlistBitmapbkup;
extern DebugLogHeader_t *DebugLogHeader; //pochune
extern volatile u8 plp_trigger;
extern u16 gc_gen_p2l_num;
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
extern void  __attribute__((weak, alias("__ftl_core_gc_grp_rd_done"))) ftl_core_gc_grp_rd_done(u32 grp);
extern bool __attribute__((weak, alias("__ftl_core_gc_data_in"))) ftl_core_gc_data_in(u32 cnt, bm_pl_t *bm_pl_list, u32 *handled_cnt);
extern void __attribute__((weak, alias("__ftl_core_gc_xfer_data"))) ftl_core_gc_xfer_data(u32 grp);
extern void __attribute__((weak, alias("__ftl_core_reset_wait_sched_idle_done"))) ftl_core_reset_wait_sched_idle_done(u32 grp, ftl_reset_t *reset);
#if (FW_BUILD_VAC_ENABLE == mENABLE)
void l2p_vac_recon(volatile cpu_msg_req_t *req);
#endif
/*!
 * @brief l2p search response to pda
 *
 * @param rsp		response
 *
 * @return		not used
 */
fast_code pda_t _srch_rsp_2pda(l2p_srch_rsp_t *rsp)
{
	u32 blk;
	pda_t pda;

	blk = rsp->pda >> nand_info.cpda_blk_pg_shift;
	blk /= nand_info.geo.nr_pages;

	pda = blk << nand_info.pda_block_shift;
	pda |= rsp->pda_pg << nand_info.pda_page_shift;
	pda |= rsp->ch << nand_info.pda_ch_shift;
	pda |= rsp->ce << nand_info.pda_ce_shift;
	pda |= rsp->pda_ln << nand_info.pda_lun_shift;
	pda |= rsp->pda_pl << nand_info.pda_plane_shift;
	pda |= rsp->pda_du << nand_info.pda_du_shift;

	return pda;
}
#if (UART_L2P_SEARCH == 1)
extern u32 l2pSrchUart;
#endif
extern u8 gDieQueDieIdTable[8][8][2];

#if defined(TCG_NAND_BACKUP)
ddr_data mbr_scheduler_t *mbr_pending_queue = NULL;
fast_data u8 evt_mbr_rd_pending = 0xff;
extern volatile u32 mbr_rd_nand_cnt;
ddr_code bool scheduler_l2p_srch_for_mbr(mbr_rd_payload_t *mbr_pl_local, u32 nvm_cmd_id, u32 btag, u64 slda, u32 len)
{
	u32 die_id = (mbr_rd_nand_cnt>>1) % shr_nand_info.lun_num;
	u32 remained_entry_cnt = die_que_mbr_rd_remain_chk(die_id);

	if(remained_entry_cnt < len)
		return false;
			
	for (u32 ofst = 0; ofst < len; ofst++) {
		bm_pl_t bm_pl;
		die_ent_info_t info;
		pda_t pda;

		// MBR L2P search
		u32 laa_srch = (slda + ofst) >> DU_CNT_PER_PAGE_SHIFT;
		if(tcg_mid_info->l2p_tbl[laa_srch] == UNMAP_PDA)
		{
			//dtag_t dtag_unmap = {0};
			//dtag_unmap.dtag = RVTAG_ID;
			//bm_rd_dtag_commit(ofst, btag, dtag_unmap);
			continue;
		}

		pda = tcg_mid_info->l2p_tbl[laa_srch];
		pda += ((slda + ofst) % DU_CNT_PER_PAGE);
		schl_apl_trace(LOG_INFO, 0x7ed8, "BTAG[%d] search rslt ofst %d pda %x", btag, ofst, pda);
		
		bm_pl.pl.btag = btag;
		bm_pl.pl.du_ofst = ofst;
		bm_pl.pl.dtag = rd_dummy_meta_idx; // dynamic mode: it's meta index, discard meta method
		bm_pl.pl.type_ctrl = META_SRAM_IDX;

		info.all = 0;
		info.b.host = 1;
		info.b.slc = 1;

		bm_pl.pl.nvm_cmd_id = nvm_cmd_id;
		info.b.stream = 1;
		if(plp_trigger){
			continue;
		}

		bool ret = die_que_rd_ins(&bm_pl, pda, info, die_id, DIE_Q_RD, false);
		if (ret == false) {
			ret = rsv_die_que_rd_ins(&bm_pl, pda, info, die_id);
			sys_assert(ret == true);
		}
	}
	die_isu_handler(0, 0, 0);
	return true;
}

ddr_code void ipc_mbr_host_read(volatile cpu_msg_req_t *req)
{
	mbr_rd_payload_t *mbr_pl_local = (mbr_rd_payload_t *)req->pl;

	if(mbr_pending_queue != NULL)
	{
		u32 idx = 1;
		void *mem = sys_malloc(SLOW_DATA, sizeof(mbr_scheduler_t));
		sys_assert(mem);

		mbr_scheduler_t *ptr = mbr_pending_queue;
		while(ptr->next)
		{
			ptr = ptr->next;
			idx++;
		}
		ptr->next = mem;
		//ptr->next->value = (u32)req;
		ptr->next->value = req->pl;
		ptr->next->next  = NULL;

		schl_apl_trace(LOG_INFO, 0x7ed9, "[MBR] Add MBR RD to %dth entry in Q", idx);
	}
	else if(scheduler_l2p_srch_for_mbr(mbr_pl_local, mbr_pl_local->nvm_cmd_id, mbr_pl_local->btag, mbr_pl_local->slda, mbr_pl_local->du_cnt))
	{
		mbr_pl_local->nvm_cmd_id = 0xFFFF;
		return;
	}
	else
	{
		// put req to pending queue
		void *mem = sys_malloc(SLOW_DATA, sizeof(mbr_scheduler_t));
		sys_assert(mbr_pending_queue==NULL);
		sys_assert(mem);

		mbr_pending_queue = (mbr_scheduler_t *)mem;
		//mbr_pending_queue->value = (u32)req;
		mbr_pending_queue->value = req->pl;
		mbr_pending_queue->next  = NULL;

		schl_apl_trace(LOG_INFO, 0x7eda, "[MBR] Add MBR RD to pending queue first time");
	}

	evt_set_cs(evt_mbr_rd_pending, 0, 0, CS_TASK);
}

ddr_code void scheduler_mbr_read_pending(u32 param0, u32 r0, u32 r1)
{
	//cpu_msg_req_t *req = (cpu_msg_req_t *)(mbr_pending_queue->value);
	//mbr_rd_payload_t *mbr_pl_local = (mbr_rd_payload_t *)req->pl;
	mbr_rd_payload_t *mbr_pl_local = (mbr_rd_payload_t *)(mbr_pending_queue->value);

	schl_apl_trace(LOG_DEBUG, 0x7edb, "[MBR] Pending MBR RD q_addr:0x%x, pl_addr:0x%x", (u32)mbr_pending_queue, (u32)mbr_pl_local);

	if(scheduler_l2p_srch_for_mbr(mbr_pl_local, mbr_pl_local->nvm_cmd_id, mbr_pl_local->btag, mbr_pl_local->slda, mbr_pl_local->du_cnt))
	{
		mbr_pl_local->nvm_cmd_id = 0xFFFF;
		mbr_scheduler_t *rels_mem = mbr_pending_queue;
		mbr_pending_queue = mbr_pending_queue->next;
		rels_mem->value = 0;
		rels_mem->next  = NULL;
		sys_free(SLOW_DATA, rels_mem);
		schl_apl_trace(LOG_INFO, 0x7edd, "[MBR] submit to DieQ from pending Q");
		if(mbr_pending_queue!=NULL)
		{
			schl_apl_trace(LOG_INFO, 0x7ede, "[MBR] set evt for next: 0x%x", mbr_pending_queue->value);
			evt_set_cs(evt_mbr_rd_pending, 0, 0, CS_TASK);
		}
	}
	else
	{
		schl_apl_trace(LOG_WARNING, 0x7edc, "[MBR] DieQ not ready, pending again");
		evt_set_cs(evt_mbr_rd_pending, 0, 0, CS_TASK);
	}
}

#endif

/*!
 * @brief l2p search done handler
 *
 * @param param0	no used
 * @param _rsp		response queue
 * @param len		length to handle
 *
 * @return		not used
 */
fast_code void scheduler_l2p_srch_done(u32 param0, u32 _rsp, u32 len)
{
	u32 **rsp = (u32 **)_rsp;
	u32 i;

	for (i = 0; i < len; i++) {
		l2p_srch_rsp_t *p = (l2p_srch_rsp_t *) dma_to_btcm((u32)*rsp);
		bm_pl_t bm_pl;
		u32 die_id;
		die_ent_info_t info;
		pda_t pda = p->pda;

		schl_apl_trace(LOG_DEBUG, 0x56fe, "search rslt ofst %d pda %x", p->ofst, p->pda);
        
        #if (UART_L2P_SEARCH == 1)
		if(l2pSrchUart == 1)
		{
			schl_apl_trace(LOG_ALW, 0xdddf, "search rslt ofst %d pda %x", p->ofst, p->pda);
			if(i == (len-1))
			l2pSrchUart = 0;
        }
        #endif
		bm_pl.pl.btag = p->btag;
		bm_pl.pl.du_ofst = p->ofst;
		bm_pl.pl.dtag = rd_dummy_meta_idx; // dynamic mode: it's meta index, discard meta method
		bm_pl.pl.type_ctrl = META_SRAM_IDX;

		info.all = 0;
		info.b.host = 1;
		#ifdef CO_SUPPORT_READ_REORDER
		info.b.pl = p->pda_pl;
		if (bm_pl.pl.du_ofst == SINGLE_SRCH_MARK) {
			info.b.single = 1;
			bm_pl.pl.du_ofst = 0;
		}
		#endif
        #if (CO_SUPPORT_READ_AHEAD == TRUE)
        if (p->btag == ra_btag)
        {
            info.b.ra = 1;
        }
		#endif

        info.b.slc = 0;

//		rd_cnt_inc(spb_id);

		//info.b.open = ftl_core_get_spb_open(spb_id);//CO_SUPPORT_READ_REORDER
//		die_id = to_die_id(p->ch, p->ce, p->pda_ln);
//        rd_cnt_inc(spb_id, die_id);
        //die_id = CHANGE_DIENUM_G2L((u8)die_id);
		die_id = gDieQueDieIdTable[p->ch][p->ce][p->pda_ln];

		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		if (p->btag != ua_btag && p->btag != ra_btag) {
		#else
		if (p->btag != ua_btag) {
		#endif
			bm_pl.pl.nvm_cmd_id = shr_l2p_srch_nid[p->btag];
			info.b.stream = 1;
            if(plp_trigger){
				rsp++;
                continue;
            }
		} else {
#if PLP_DEBUG
            if (plp_trigger == 0xEE)
            {
                extern u32 fill_up_dtag_start;
                info.all = 0;
                info.b.debug = 1;
                info.b.host = 1;
                info.b.gc = 1;
                bm_pl.pl.btag = 0;
                bm_pl.pl.dtag = fill_up_dtag_start + p->ofst + (CPU_ID >> 2) * 128 + DTAG_IN_DDR_MASK;
                bm_pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_DTAG;
                schl_apl_trace(LOG_DEBUG, 0x56e4, "plp trigger:%x, dtag:%x", info.all, bm_pl.pl.dtag);
            }
            else
#endif
            {
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
                if (plp_trigger)
                {
                    spb_id_t spb_id;
            		spb_id = nal_pda_get_block_id(pda);
                    info.b.slc = ftl_core_get_spb_type(spb_id);
                }    
#endif
                #if (CO_SUPPORT_READ_AHEAD == TRUE)
                if (p->btag != ra_btag)
                {
                    bm_pl.pl.dtag = cache_read_dtags[p->ofst].dtag;
                    bm_pl.pl.type_ctrl = (cache_read_dtags[p->ofst].b.in_ddr) ? META_DDR_DTAG : META_SRAM_DTAG;
                }
                else
                {
                    dtag_t dtag;
                    dtag.dtag = shr_ra_ddtag_start + p->ofst;

                    dtag.b.in_ddr = 1;
                    bm_pl.pl.dtag = dtag.dtag;
                    bm_pl.pl.type_ctrl = dtag.b.in_ddr ? META_DDR_DTAG : META_SRAM_DTAG;
                }
                #else
            	bm_pl.pl.dtag = cache_read_dtags[p->ofst].dtag;
            	bm_pl.pl.type_ctrl = (cache_read_dtags[p->ofst].b.in_ddr) ? META_DDR_DTAG : META_SRAM_DTAG;
                #endif
            }
			sys_assert(bm_pl.pl.dtag != DTAG_INV);
			schl_apl_trace(LOG_DEBUG, 0xa92d, "par pda %x", pda);
		}
#ifndef SKIP_MODE
		pda = ftl_remap_pda(pda);
#endif

		bool ret = die_que_rd_ins(&bm_pl, pda, info, die_id, DIE_Q_RD, false);
		if (ret == false) {
			ret = rsv_die_que_rd_ins(&bm_pl, pda, info, die_id);
			sys_assert(ret == true);
		}

		rsp++;
	}

	die_isu_handler(0, 0, 0);
}

ddr_code void scheduler_resume_host_streaming_read(void)
{
	stream_drop = false;
	pool_init(&gc_scheduler.rsp_pool, (void *)gc_scheduler.vpda_rsp_entry, sizeof(_vpda_rsp_entry),
			sizeof(pda_gen_rsp_t), PDA_RSP_PER_GRP);
	gc_scheduler.grp_res = 0;
	scheduler_gc_grp_refill(SCHEDULER - 1);
#if defined(DUAL_BE) && SCHEDULER == 1
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_SCHED_RESUME_STRM_READ, 0, 2);
#endif
}
#if PLP_TEST
#if (CPU_ID == 2 || CPU_ID == 4)
extern volatile u8 plp_trigger;
extern struct ncl_cmd_t* ncl_cmd_ptr[DTAG_PTR_COUNT];
//extern u32 fill_done_cnt,fill_req_cnt;
extern u32 fcmd_outstanding_cnt;
#endif
#endif

#if (CO_SUPPORT_READ_AHEAD == TRUE)
ddr_code void scheduler_abort_ra_payload(void)
{
	u32 die_cnt;
	u32 i;
	die_ent_info_t tar = { .all = 0 };
	tar.b.host = true;
	tar.b.ra = true;

	// abort read ahead in rsvd queue
	rsv_die_que_cancel(tar);

	// abort read ahead in die queue
	die_cnt = min(shr_nand_info.lun_num, MAX_DIE_MGR_CNT);
	for (i = 0; i < die_cnt; i++)
	{
		die_que_cancel(i, tar);
	}
}
#endif
ddr_code void scheduler_plp_cancel_die_que(volatile cpu_msg_req_t *req)
{
	die_ent_info_t tar = { .all = 0 };
	tar.b.host = true;
	tar.b.stream = true;
#if (CPU_ID == 2 || CPU_ID == 4)
	u32 read_count = 0, stream_read_count = 0, write_count = 0, erase_count = 0, gc_aux_read_count = 0;
	if (plp_trigger && fcmd_outstanding_cnt) 
	{
		for (int i = 0;i < DTAG_PTR_COUNT;i++) 
		{
			if (ncl_cmd_ptr[i] != NULL) 
			{
				switch(ncl_cmd_ptr[i]->op_code)
				{
					case NCL_CMD_OP_READ:
						read_count++;
					break;
					case NCL_CMD_OP_READ_STREAMING_FAST:
						stream_read_count++;
					break;
					case NCL_CMD_OP_WRITE:
						write_count++;
					break;
					case NCL_CMD_OP_ERASE:
						erase_count++;
					break;
					case NCL_CMD_P2L_SCAN_PG_AUX:
						gc_aux_read_count++;
					break;
					default:
					break;
				}
			}
		}
		schl_apl_trace(LOG_ALW, 0xd1f8, "fcmd:%d, read:%d, sm_read:%d, write:%d, erase:%d, gc_aux:%d", 
			fcmd_outstanding_cnt, read_count, stream_read_count, write_count, erase_count, gc_aux_read_count);		
	}
#endif

#if (CPU_ID == 2)
	extern volatile u8 CPU2_plp_step;
	CPU2_plp_step = 2;
#endif
	rsv_die_que_cancel(tar);
	u32 die_cnt = min(shr_nand_info.lun_num, MAX_DIE_MGR_CNT);
	for (u32 i = 0; i < die_cnt; i++)
	{
		die_que_cancel(i, tar);
	}

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	scheduler_abort_ra_payload();
	#endif

#if defined(DUAL_BE) && SCHEDULER == 1
	extern u8 cpu2_cancel_streaming;
	//cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_PLP_CANCEL_DIEQUE, SCHEDULER + 1, 0);
	cpu2_cancel_streaming = 0;
#else
	extern u8 cpu4_cancel_streaming;
	cpu4_cancel_streaming= 0;
#endif
#if (CPU_ID == 2)
	CPU2_plp_step = 3;
#endif
	extern volatile bool esr_err_fua_flag;
	if(esr_err_fua_flag == true)
	{
		while(1);//lock cpu2/4 to avoid imcomplete WL
	}

	return;
}

ddr_code void scheduler_abort_stream_payload(void)
{
	u32 die_cnt;
	u32 i;
	die_ent_info_t tar = { .all = 0 };
	tar.b.host = true;
	tar.b.stream = true;

	// abort host streaming read in rsvd queue
	rsv_die_que_cancel(tar);

	// abort host streaming read in die queue
	die_cnt = min(shr_nand_info.lun_num, MAX_DIE_MGR_CNT);
	for (i = 0; i < die_cnt; i++)
	{
		die_que_cancel(i, tar);
	}
}

ddr_code u32 scheduler_abort_host_streaming_read(ftl_reset_t *reset)
{
	// u32 i;
	// die_ent_info_t tar = { .all = 0 };
	u32 cnt = 1;
	//u32 die_cnt;
	//l2p_idle_ctrl_t ctrl;

	// tar.b.host = true;
	// tar.b.stream = true;
#if PLP_TEST
#if (CPU_ID == 2 || CPU_ID == 4)
    u32 read_count = 0,write_count = 0;
    if(plp_trigger && fcmd_outstanding_cnt){
        for(int i = 0;i < DTAG_PTR_COUNT;i++){
            if(ncl_cmd_ptr[i] != NULL){
                if(ncl_cmd_ptr[i]->op_code == 1 || ncl_cmd_ptr[i]->op_code == 0)
                    read_count++;
                if(ncl_cmd_ptr[i]->op_code == 14)
                    write_count++;
                /*schl_apl_trace(LOG_DEBUG, 0, "fcmd id %d, ncl cmd addr 0x%x", i, ncl_cmd_ptr[i]);
                schl_apl_trace(LOG_DEBUG, 0, "cpl 0x%x, flags %d, code %d, type %d",\
                    ncl_cmd_ptr[i]->completion, ncl_cmd_ptr[i]->flags, ncl_cmd_ptr[i]->op_code,
                    ncl_cmd_ptr[i]->op_type);      */
            }
        }
        schl_apl_trace(LOG_ERR, 0xb01a, " fcmd_outstanding_cnt %d read cnt %d write cnt %d",\
             fcmd_outstanding_cnt,read_count,write_count);
    }
#endif
#endif
    // LET L2P SRCH Q EMPTY
	//ctrl.all = 0;
	//ctrl.b.srch = 1;
	//ctrl.b.wait = 1;
	//wait_l2p_idle(ctrl);

	// filter streaming read in search done
	stream_drop = true;

	scheduler_abort_stream_payload();

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	scheduler_abort_ra_payload();
	#endif

	// delay monitor scheduler status
	evt_set_cs(evt_sched_mon, SCHEDULER, (u32)reset, CS_TASK);

#if defined(DUAL_BE) && SCHEDULER == 1
	reset = tcm_local_to_share((void*)reset);
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_SCHED_ABORT_STRM_READ, SCHEDULER + 1, (u32)reset);
	cnt++;
#endif
	return cnt;
}

fast_code void scheduler_gc_grp_refill(u32 grp)
{
#if CPU_ID == CPU_BE
	sys_assert(grp == 0);
#elif CPU_ID == CPU_BE_LITE
	sys_assert(grp == 1);
#endif

	pda_gen_rsp_t *rsp_addr;

	while (gc_scheduler.grp_res < MAX_GC_RES_PER_GRP) {
		rsp_addr = (pda_gen_rsp_t *)pool_get_ex(&gc_scheduler.rsp_pool);
		if (rsp_addr) {
			u32 ptr = btcm_to_dma(rsp_addr);
			l2p_pda_grp_rsp_ret(&ptr, 1, grp);
			gc_scheduler.grp_res++;
		} else {
			break;
		}
	}
}

fast_code void __ftl_core_gc_grp_rd_done(u32 grp)
{
	gc_scheduler.rd_done = false;
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_GC_GRP_RD_DONE, 0, grp);
}

fast_code bool __ftl_core_gc_data_in(u32 cnt, bm_pl_t *bm_pl_list, u32 *handled_cnt)
{
	u32 ins_cnt;

again:
	CBF_INS_LIST(gc_src_data_que, ins_cnt, bm_pl_list, cnt);

	if (ins_cnt != cnt) {
		bm_pl_list += ins_cnt;
		cnt -= ins_cnt;
		goto again;
	}
	return true;
}

fast_code void __ftl_core_gc_xfer_data(u32 grp)
{

	//scheduler_gc_vpda_read(grp);
	//if ((gc_scheduler.vpda_pending == 0) && (gc_pda_grp[1].reading == 0))
	if ((gc_scheduler.vpda_pending == 0) && (gc_scheduler.rd_done))
		ftl_core_gc_grp_rd_done(1);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	else if(gc_pda_grp[1].spb_id != PLP_SLC_BUFFER_BLK_ID)
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
	else
		issue_gc_read_slc(1);
#else
	else
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
#endif
	
}

u32 scheduler_gc_get_dtagid(void)
{
	u16 gc_wptr,gc_rptr;
    u32 dtag_idx;

	gc_rptr = gc_scheduler.rptr;
	gc_wptr = gc_scheduler.wptr;
	sys_assert(gc_rptr != gc_wptr);

	dtag_idx = gc_scheduler.free_gc_dtagid_q[gc_rptr];
	if (++gc_rptr == (DTAG_CNT_PER_GRP+1))
	{
		gc_rptr = 0;
	}
	gc_scheduler.rptr = gc_rptr;

	return dtag_idx;
}


fast_code void ftl_core_gc_push_dtagid(u32 dtag_id)
{
	u16 gc_wptr,gc_rptr;
	u16 next;
	gc_rptr = gc_scheduler.rptr;
	gc_wptr = gc_scheduler.wptr;
	next = gc_wptr + 1;

	if (next >= (DTAG_CNT_PER_GRP+1))
	{
		next = 0;
	}
	sys_assert(next != gc_rptr);


	gc_scheduler.free_gc_dtagid_q[gc_wptr] = (u16)dtag_id;

	gc_scheduler.wptr = next;

}

fast_code void scheduler_gc_free_res(void)
{
	gc_res_free_t res;
	//pda_gen_rsp_t *rsp;


	while (CBF_EMPTY(gc_res_free_que) == false) {

		CBF_FETCH_GC(gc_res_free_que, res);

		//rsp = &gc_scheduler.vpda_rsp_entry[res.rsp_idx];
		//pool_put_ex(&gc_scheduler.rsp_pool, rsp);
		ftl_core_gc_push_dtagid(res.dtag_idx);
		//int ret = test_and_set_bit(res.dtag_idx, gc_scheduler.ddtag_bmp);
		//sys_assert(ret == 0);
		gc_scheduler.ddtag_avail++;
	}
	scheduler_gc_grp_refill(GC_LOCAL_PDA_GRP);

	if (gc_scheduler.ddtag_avail && gc_scheduler.vpda_pending)
	{
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(gc_pda_grp[GC_LOCAL_PDA_GRP].spb_id != PLP_SLC_BUFFER_BLK_ID)
			scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
		else
			issue_gc_read_slc(GC_LOCAL_PDA_GRP);
#else
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
#endif
	
		
	}
}


static inline void scheduler_gc_vpda_push(pda_gen_rsp_t rsp)
{
	vpda_rsp_que_t *vpda_rsp_que = &gc_scheduler.vpda_rsp_que;
	u32 wptr = vpda_rsp_que->wptr;


	vpda_rsp_que->rsp_idx[wptr] = rsp;
	vpda_rsp_que->wptr++;
	//evlog_printk(LOG_INFO,"push rsp %d %d %d",vpda_rsp_que->wptr,rsp.ent.lda,rsp.ent.cpda);
	if(vpda_rsp_que->wptr == vpda_rsp_que->size)
		vpda_rsp_que->wptr=0;
	sys_assert(vpda_rsp_que->wptr != vpda_rsp_que->rptr);
	sys_assert(vpda_rsp_que->wptr < vpda_rsp_que->size);
}

static inline pda_gen_rsp_t scheduler_gc_vpda_pop(void)
{

	vpda_rsp_que_t *vpda_rsp_que = &gc_scheduler.vpda_rsp_que;
	u32 rptr = vpda_rsp_que->rptr;
	//sys_assert(vpda_rsp_que->rptr < vpda_rsp_que->wptr);

	pda_gen_rsp_t rsp = vpda_rsp_que->rsp_idx[rptr];
	vpda_rsp_que->rptr++;
	//evlog_printk(LOG_INFO,"push rsp %d %d %d",vpda_rsp_que->rptr,rsp.ent.lda,rsp.ent.cpda);
	if(vpda_rsp_que->rptr == vpda_rsp_que->size)
		vpda_rsp_que->rptr=0;
	return rsp;
}

#if skip_mode
u8 skip_defect_pda(u32 spb_id, u32 pda)
{
	//(bitmap[index >> 5] & (1 << (index & 0x1f)));
	u32 interleave = ((pda >> 2) & (shr_nand_info.interleave - 1));

	u32 idx = interleave >> 3;
	u32 off = interleave & (7);

	u32 index = (spb_id * shr_nand_info.interleave) >> 3;
	u8 *ftl_df = &gl_pt_defect_tbl[index];

	return ((ftl_df[idx] >> off)&1);
}
#endif

/*!
 * @brief l2p gc pda in handler
 *
 * @param qid		group id
 * @param q		response queue
 * @param cnt		count to handle
 *
 * @return		not used
 */
fast_code void scheduler_gc_pda_in(u32 qid, u32 q, u32 cnt)
{
	u32 i;
	u32 rsp_idx;
	u32 *que = (u32*)q;
	pda_gen_rsp_t *rsp;

    #if (BTN_WCMD_CPU == CPU_ID) && !defined(WCMD_USE_IRQ)// CPU4 need speed up to service incoming write cmd.  QD1 [53,95,174]
    extern __attribute__((weak)) void btn_w_cmd_in();
    extern u32 shr_hostw_perf;
    extern u8 shr_gc_fc_ctrl;
	if (shr_gc_fc_ctrl && (shr_hostw_perf < 140000))//only for QD1, improve [99.9%] QoS
	{
		u32 wcmd_coming = vic_readl(RAW_INTERRUPT_STATUS) & (1<<VID_NCMD_RECV_Q2);
		if(wcmd_coming)
			btn_w_cmd_in();
	}
    #endif
	sys_assert(gc_scheduler.grp_res >= cnt);
	gc_scheduler.grp_res = gc_scheduler.grp_res - cnt;

	if (gc_scheduler.vpda_pending == 0)
	{
		gc_scheduler.vpda_rsp_que.rptr = 0;
		gc_scheduler.vpda_rsp_que.wptr = 0;
	}

	for (i = 0; i < cnt; i++)
	{
		rsp = (pda_gen_rsp_t *)tcm_share_to_local((void *)que[i]);
		rsp_idx = rsp - gc_scheduler.vpda_rsp_entry;
		//pda_gen_rsp_t *t = &gc_scheduler.vpda_rsp_entry[rsp_idx];
		//evlog_printk(LOG_INFO,"%d ce%d ch%d pga%d lun%d pl%d du%d",cnt,t->ent.pda_ce,t->ent.pda_ch,t->ent.pda_pga,t->ent.pda_lun,t->ent.pda_pl,t->ent.pda_du);
		//sys_assert(rsp_idx >= 0 && rsp_idx < PDA_RSP_PER_GRP);
		sys_assert(rsp_idx < PDA_RSP_PER_GRP);

		if (rsp->end.end_of_mark != ~0)
		{
			if (plp_trigger == 0)
			{
				gc_pda_grp[qid].vcnt++;
				//gc_scheduler.vpda_pending++;
				issue_gc_read(rsp);
			}
			//scheduler_gc_vpda_push(rsp_idx);
		}
		else
		{
			//evlog_printk(LOG_INFO,"tzu gen ttl_vld %d",rsp->end.ttl_vld);
			gc_scheduler.rd_done = true;
			gc_pda_grp[qid].pda_in=true;
			ftl_core_gc_xfer_data(qid);
		}
		pool_put_ex(&gc_scheduler.rsp_pool, rsp);
	}

	scheduler_gc_grp_refill(qid);
}
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)

ddr_code static u8* get_spb_defect(u32 spb_id)
{
	//u32 interleave = get_interleave();
	u32 df_width = occupied_by(shr_nand_info.interleave, 8) << 3;
	u32 index = (spb_id * df_width) >> 3;
	return (gl_pt_defect_tbl + index);
}

#if (CPU_ID == 4)
fast_code void ipc_issue_gc_read_slc(volatile cpu_msg_req_t *req)
{
	u8	* df_ptr;
	df_ptr = get_spb_defect(PLP_SLC_BUFFER_BLK_ID);
	gc_slc_raid_calculate(df_ptr);
	issue_gc_read_slc(1);
}
#endif

ddr_code bool chk_gc_slc_defect(u8 * df_ptr, pda_t pda)
{
    u32 die_pn_idx;
    u32 idx, off;

    die_pn_idx = (pda2die(pda) << ctz(shr_nand_info.geo.nr_planes)) | pda2plane(pda);
    idx = die_pn_idx >> 3;
    off = die_pn_idx & (7);

    return (((df_ptr[idx] >> off) & 1) || die_pn_idx == slc_parity_interleave);
}

#ifdef SKIP_MODE
share_data_zi u8* gl_pt_defect_tbl;
#endif

ddr_code u8 slc_get_defect_pl_pair(u8* ftl_df, u32 interleave)
{
	//(bitmap[index >> 5] & (1 << (index & 0x1f)));
	if(interleave%shr_nand_info.geo.nr_planes!=0) panic("interleave not pl 0\n");
	u32 idx = interleave >> 3;
	u32 off = interleave & (7);
	if (shr_nand_info.geo.nr_planes == 4)
		return ((ftl_df[idx] >> off)&0xf);
	else
		return (((ftl_df[idx] >> off)&0x3));
}

ddr_code void gc_slc_raid_calculate(u8 * df_ptr)
{
	u8 parity_die = shr_nand_info.lun_num;
    u8 parity_die_pln_pair = BLOCK_NO_BAD;
    if (fcns_raid_enabled(HOST_NS_ID)) {
        parity_die = shr_nand_info.lun_num - 1;
        u32 plane_idx = nand_interleave_num() - nand_plane_num();
        while(1)
		{
			u8 pln_pair = slc_get_defect_pl_pair(df_ptr, plane_idx);

            if ((pln_pair == BLOCK_ALL_BAD) && (plane_idx / nand_plane_num() == parity_die)) {
                parity_die--;
            } else {
                parity_die_pln_pair = pln_pair;
                break;
            }

            if (plane_idx == 0)
                break;
            plane_idx -= nand_plane_num();
		}
        sys_assert(parity_die != 0xFF);
        sys_assert(parity_die != 0); //ftl_core_flush_blklist_done
    }

    u8 slc_parity_die_pln_idx = shr_nand_info.geo.nr_planes;
    if(fcns_raid_enabled(HOST_NS_ID)){
        bool ret = false;
        for (u8 pl = 0; pl < nand_plane_num(); pl++){
            if ((parity_die_pln_pair & (1 << pl)) == 0){ //good plane
                slc_parity_die_pln_idx = pl;
                ret = true;
                break;
            }
        }
        sys_assert(ret);
    }
	slc_parity_interleave = parity_die * shr_nand_info.geo.nr_planes + slc_parity_die_pln_idx;
    schl_apl_trace(LOG_INFO, 0xc1f8, "slc blk parity info(die:%d pln_pair %u pln_idx %u)",
        parity_die, parity_die_pln_pair, slc_parity_die_pln_idx);
}
ddr_code void issue_gc_read_slc(u32 qid)
{
	u32 die_id, pda_ch;
	gc_scheduler.vpda_pending = 1;
	//shr_plp_slc_end_pda = 0x100003;
	//schl_apl_trace(LOG_INFO, 0, "start_pda 0x%x, end_pda 0x%x", gc_pda_grp[qid].slc_pda, shr_plp_slc_end_pda);
	
#ifdef SKIP_MODE
	u8	* df_ptr;
	df_ptr = get_spb_defect(PLP_SLC_BUFFER_BLK_ID);
#endif

	while(gc_pda_grp[qid].slc_pda <= shr_plp_slc_end_pda){
		pda_ch = (gc_pda_grp[qid].slc_pda >> nand_info.pda_ch_shift) & (nand_info.geo.nr_channels - 1);	//pda2ch()
#if (CPU_ID == 2)
		if(pda_ch > 3 || chk_gc_slc_defect(df_ptr, gc_pda_grp[qid].slc_pda))
#elif (CPU_ID == 4)
		if(pda_ch < 4 || chk_gc_slc_defect(df_ptr, gc_pda_grp[qid].slc_pda))
#endif
		{
			if(chk_gc_slc_defect(df_ptr, gc_pda_grp[qid].slc_pda))
				gc_pda_grp[0].defect_cnt += DU_CNT_PER_PAGE;
			gc_pda_grp[qid].slc_pda += DU_CNT_PER_PAGE;
			continue;
		}

		die_id = (gc_pda_grp[qid].slc_pda >> nand_info.pda_ch_shift) & nand_info.pda_lun_mask;	//need to fix to SLC
    	die_id = CHANGE_DIENUM_G2L((u8)die_id);
		//schl_apl_trace(LOG_INFO, 0, "gc_pda_grp[%d].slc_pda 0x%x, die_idx %d, pda_ch %d", qid, gc_pda_grp[qid].slc_pda, die_id, pda_ch);

		if (die_que_read_avail(die_id) && gc_scheduler.ddtag_avail)
		{
			bm_pl_t bm_pl;
		    pda_t pda = gc_pda_grp[qid].slc_pda;
		    u32 grp = (pda_ch/4);
			u32 dtag_idx = scheduler_gc_get_dtagid();
		    die_ent_info_t info;
			
			//dtag_idx = find_next_bit(gc_scheduler.ddtag_bmp, DTAG_CNT_PER_GRP, 0);
			//sys_assert(dtag_idx < DTAG_CNT_PER_GRP);
			//clear_bit(dtag_idx, gc_scheduler.ddtag_bmp);
			gc_scheduler.ddtag_avail--;
			//gc_scheduler.vpda_pending--;
			gc_scheduler.pda_list[dtag_idx] = pda;
	        gc_scheduler.lda_list[dtag_idx] = INV_LDA;
			bm_pl.all = 0;    //btag = 0
			bm_pl.pl.du_ofst = dtag_idx;
			bm_pl.pl.nvm_cmd_id = grp;
			bm_pl.pl.dtag = (gc_scheduler.ddtag_start + dtag_idx) | GC_DTAG_IN_DDR;
			bm_pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | GC_DTAG_META_LOC;

			info.all = 0;
			info.b.gc = 1;
			info.b.host = 1;
			info.b.slc = 1;
			info.b.pl = pda2plane(pda);
#ifndef SKIP_MODE
			pda = ftl_remap_pda(pda);
#endif
			bool sts = die_que_rd_ins(&bm_pl, pda, info, die_id, DIE_Q_GC, true);
			sys_assert(sts == true);
			gc_pda_grp[grp].reading++;
			gc_pda_grp[grp].vcnt++;
			gc_pda_grp[qid].slc_pda++;
		}
		else
			break;
	}
	
	if(gc_pda_grp[qid].slc_pda <= shr_plp_slc_end_pda)
		gc_scheduler.vpda_pending = 1;
	else
	{
		gc_scheduler.vpda_pending = 0;
		gc_scheduler.rd_done = true;
		gc_pda_grp[qid].pda_in=true;
		ftl_core_gc_xfer_data(qid);
	}
}

#endif
fast_code void issue_gc_read(pda_gen_rsp_t *rsp)
{
	u32 die_id;
	//u32 dtag_idx;

    die_id = gDieQueDieIdTable[rsp->ent.pda_ch][rsp->ent.pda_ce][rsp->ent.pda_lun];
	//die_id = to_die_id(rsp->ent.pda_ch, rsp->ent.pda_ce, rsp->ent.pda_lun);

    //die_id = CHANGE_DIENUM_G2L((u8)die_id);

	if (!die_que_read_avail(die_id) || gc_scheduler.ddtag_avail==0)
	{
		gc_scheduler.vpda_pending++;
		test_and_set_bit(die_id, gc_scheduler.pend_die_bmp);
		scheduler_gc_vpda_push(*rsp);
		//scheduler_gc_vpda_return(rsp_idx);
	}
	else
	{
	    bm_pl_t bm_pl;
	    pda_t pda = rsp->ent.cpda;
	    u32 grp = (rsp->ent.pda_ch/4);
		u32 dtag_idx = scheduler_gc_get_dtagid();
	    die_ent_info_t info;

		//dtag_idx = find_next_bit(gc_scheduler.ddtag_bmp, DTAG_CNT_PER_GRP, 0);
		//sys_assert(dtag_idx < DTAG_CNT_PER_GRP);
		//clear_bit(dtag_idx, gc_scheduler.ddtag_bmp);
		gc_scheduler.ddtag_avail--;
		//gc_scheduler.vpda_pending--;
		gc_scheduler.pda_list[dtag_idx] = pda;
        gc_scheduler.lda_list[dtag_idx] = rsp->ent.lda; //gc lda list from p2l
		bm_pl.all = 0;    //btag = 0
		bm_pl.pl.du_ofst = dtag_idx;
		bm_pl.pl.nvm_cmd_id = grp;
		bm_pl.pl.dtag = (gc_scheduler.ddtag_start + dtag_idx) | GC_DTAG_IN_DDR;
		bm_pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | GC_DTAG_META_LOC;

		info.all = 0;
		info.b.gc = 1;
		info.b.host = 1;
		info.b.slc = gc_pda_grp[grp].flags.b.slc;
		info.b.pl = rsp->ent.pda_pl;

        #ifndef SKIP_MODE  // only for replace mode
		pda = ftl_remap_pda(pda);
		#endif

		bool sts = die_que_rd_ins(&bm_pl, pda, info, die_id, DIE_Q_GC, true);
		sys_assert(sts == true);
		gc_pda_grp[grp].reading++;
	}
}

fast_code void scheduler_gc_read_done(u32 grp, bm_pl_t *bm_pl, u32 cnt)
{
	volatile gc_pda_grp_t *gc_grp = &gc_pda_grp[grp];

	sys_assert(gc_grp->reading >= cnt);
	gc_grp->reading = gc_grp->reading - cnt;

    if(plp_trigger == 0){
	    ftl_core_gc_data_in(cnt, bm_pl, NULL);
    }

	//grp read done
	//if ((gc_scheduler.vpda_pending == 0) && (gc_grp->reading == 0))
	if ((gc_scheduler.vpda_pending == 0) && (gc_scheduler.rd_done))
		ftl_core_gc_grp_rd_done(grp);
}
fast_code void scheduler_gc_vpda_read(u32 grp)
{
	pda_t pda;
	u32 die_id;
	u32 dtag_idx;
	bm_pl_t bm_pl;
	die_ent_info_t info;
	pda_gen_rsp_t rsp;

	if (gc_pda_grp[grp].flags.b.abort||plp_trigger) {
		scheduler_gc_grp_aborted(grp);
		return;
	}

	dtag_idx = 0;
	while (gc_scheduler.vpda_pending && gc_scheduler.ddtag_avail) {
		rsp = scheduler_gc_vpda_pop();
		pda = rsp.ent.cpda;

        die_id = gDieQueDieIdTable[rsp.ent.pda_ch][rsp.ent.pda_ce][rsp.ent.pda_lun];
		//die_id = to_die_id(rsp.ent.pda_ch, rsp.ent.pda_ce, rsp.ent.pda_lun);
        //die_id = CHANGE_DIENUM_G2L((u8)die_id);
		if (!die_que_read_avail(die_id)) {
			test_and_set_bit(die_id, gc_scheduler.pend_die_bmp);
			scheduler_gc_vpda_push(rsp);
			break;
		}

			//dtag_idx = find_next_bit(gc_scheduler.ddtag_bmp, DTAG_CNT_PER_GRP, dtag_idx);
			//sys_assert(dtag_idx < DTAG_CNT_PER_GRP);
			dtag_idx = scheduler_gc_get_dtagid();

			gc_scheduler.ddtag_avail--;
			gc_scheduler.vpda_pending--;
			gc_scheduler.pda_list[dtag_idx] = pda;
			gc_scheduler.lda_list[dtag_idx] = rsp.ent.lda;	//gc lda list from p2l
			//clear_bit(dtag_idx, gc_scheduler.ddtag_bmp);

		bm_pl.all = 0;   // btag = 0,  willis2
		bm_pl.pl.du_ofst = dtag_idx;
		bm_pl.pl.nvm_cmd_id = grp;
		bm_pl.pl.dtag = (gc_scheduler.ddtag_start + dtag_idx) | GC_DTAG_IN_DDR;
		bm_pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | GC_DTAG_META_LOC;

		info.all = 0;
		info.b.gc = 1;
		info.b.host = 1;
		info.b.slc = gc_pda_grp[grp].flags.b.slc;
		info.b.pl = rsp.ent.pda_pl;
		
#ifndef SKIP_MODE
		pda = ftl_remap_pda(pda);
#endif
		bool sts = die_que_rd_ins(&bm_pl, pda, info, die_id, DIE_Q_GC, true);
		sys_assert(sts == true);
		gc_pda_grp[grp].reading++;
		//dtag_idx++;
	}

	//volatile gc_pda_grp_t *gc_grp = &gc_pda_grp[grp];
	//if ((gc_scheduler.vpda_pending == 0) && (gc_grp->reading == 0))
	if ((gc_scheduler.vpda_pending == 0) && (gc_scheduler.rd_done))
		ftl_core_gc_grp_rd_done(grp);
}


fast_code void scheduler_gc_grp_aborted(u32 grp)
{
	//u32 rsp_idx;
	//pda_gen_rsp_t rsp;
	//volatile gc_pda_grp_t *gc_grp = &gc_pda_grp[grp];

	//release resource
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(gc_pda_grp[grp].spb_id != PLP_SLC_BUFFER_BLK_ID) {
#endif
		while (gc_scheduler.vpda_pending) {
			//rsp = scheduler_gc_vpda_pop();
			//rsp = &gc_scheduler.vpda_rsp_entry[rsp_idx];
			//pool_put_ex(&gc_scheduler.rsp_pool, rsp);

			gc_scheduler.vpda_pending--;
			gc_pda_grp[grp].vcnt--;
		}
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	}
	else {
		gc_scheduler.vpda_pending = 0;
		gc_scheduler.rd_done = true;
	}
#endif

	//if ((gc_scheduler.vpda_pending == 0) && (gc_grp->reading == 0))
	if ((gc_scheduler.vpda_pending == 0) && (gc_scheduler.rd_done))
		ftl_core_gc_grp_rd_done(grp);
}

fast_code void scheduler_gc_grp_resume(u8 gid)
{
	volatile gc_pda_grp_t *grp = &gc_pda_grp[gid];
	vpda_rsp_que_t *vpda_rsp_que = &gc_scheduler.vpda_rsp_que;

	grp->attr.all = 0;
#if CPU_ID == CPU_BE_LITE
	grp->attr.b.remote = true;
#endif
	gc_scheduler.vpda_pending = 0;
	gc_scheduler.vpda_rsp_entry = _vpda_rsp_entry;
	gc_scheduler.grp_res = 0;
	gc_scheduler.ddtag_avail = (shr_nand_info.lun_num == 8 ? (DTAG_CNT_PER_GRP>>1) : DTAG_CNT_PER_GRP);
	gc_scheduler.ddtag_start = shr_gc_ddtag_start + DTAG_CNT_PER_GRP * gid;
	//gc_scheduler.free_gc_dtagid_q = sys_malloc(FAST_DATA, sizeof(u16) * (DTAG_CNT_PER_GRP+1));
//	gc_scheduler.free_gc_dtagid_q = (u16*)ddtag2mem(ddr_dtag_register((sizeof(u16)*(DTAG_CNT_PER_GRP+1)/4096)+1));
	gc_scheduler.free_gc_dtagid_q = (u16*)ddtag2mem(ddr_dtag_register((sizeof(u16)*(DTAG_MAX_CNT_PER_GRP+1)/4096)+1));

	gc_scheduler.rptr = 0;
	gc_scheduler.wptr = DTAG_CNT_PER_GRP;

	for(u32 gc_dtag_num = 0; gc_dtag_num < DTAG_CNT_PER_GRP; gc_dtag_num++)
	{
		gc_scheduler.free_gc_dtagid_q[gc_dtag_num] = gc_dtag_num;
	}

	gc_scheduler.pda_list = gc_pda_list + DTAG_CNT_PER_GRP * gid;
    gc_scheduler.lda_list = gc_lda_list + DTAG_CNT_PER_GRP * gid;    //gc lda list from p2l
	gc_scheduler.meta_list = gc_meta + DTAG_CNT_PER_GRP * gid;
	gc_scheduler.rd_done = false;
	gc_pda_grp[gid].pda_in = false;
	pool_init(&gc_scheduler.rsp_pool, (void *)gc_scheduler.vpda_rsp_entry, sizeof(_vpda_rsp_entry),
			sizeof(pda_gen_rsp_t), PDA_RSP_PER_GRP);

	vpda_rsp_que->rptr = 0;
	vpda_rsp_que->wptr = 0;
//	vpda_rsp_que->size = (DTAG_CNT_PER_GRP+1)*NUM_P2L_GEN_PDA;
	vpda_rsp_que->size = (VPDA_RSP_QUE_SIZE+1)*gc_gen_p2l_num;

	//vpda_rsp_que->rsp_idx = sys_malloc(SLOW_DATA, vpda_rsp_que->size * sizeof(u16));
	if(vpda_rsp_que->rsp_idx == NULL)
	vpda_rsp_que->rsp_idx = (pda_gen_rsp_t *)ddtag2mem(ddr_dtag_register(((sizeof(pda_gen_rsp_t)*vpda_rsp_que->size)/4096)+1));

	sys_assert(vpda_rsp_que->rsp_idx);

	//memset(gc_scheduler.ddtag_bmp, 0xFF, sizeof(gc_scheduler.ddtag_bmp));
	memset(gc_scheduler.pend_die_bmp, 0, sizeof(gc_scheduler.pend_die_bmp));
	scheduler_gc_grp_refill(gid);
}

ps_code void scheduler_suspend(u32 gid)
{
	sys_assert(gc_scheduler.vpda_pending == 0);
	//sys_free(SLOW_DATA, gc_scheduler.vpda_rsp_que.rsp_idx);
}

ps_code void scheduler_resume(u32 gid)
{
	die_mgr_resume(gid);
	scheduler_gc_grp_resume(gid);
	read_error_resume();
}

init_code void scheduler_gc_grp_init(u8 gid, u8 *evt_pda_gen)
{
	evt_register(scheduler_gc_pda_in, gid, evt_pda_gen);
	gc_scheduler.vpda_rsp_que.rsp_idx = NULL;
	scheduler_gc_grp_resume(gid);
}

ddr_code void scheduler_chk_stream_rd_idle(u32 p0, u32 p1, u32 p2)
{
	u32 i;
	u32 die_cnt;
	bool idle = true;

	sys_assert(p0 == (p1 - 1));
	die_cnt = min(shr_nand_info.lun_num, MAX_DIE_MGR_CNT);

	// TODO read retry for host read
	evt_set_imt(evt_errhandle_mon, 0, 0);

	for (i = 0; i < die_cnt; i++)
	{
		idle &= die_mgr_chk_stream_rd_idle(i);
	}

	if (idle == false)
	{
        evt_set_cs(evt_sched_mon, p1, p2, CS_TASK);
	}
	else
	{
		ftl_core_reset_wait_sched_idle_done(p1, (ftl_reset_t *) p2);
	}

}


init_code void scheduler_init(u8 *srch_evt, u32 gid)
{
	dummy_meta = idx_meta_allocate(1, SRAM_IDX_META, &rd_dummy_meta_idx);
	wr_dummy_meta = idx_meta_allocate(1, SRAM_IDX_META, &wr_dummy_meta_idx);
	wr_dummy_meta->lda = DUMMY_LDA;
	schl_apl_trace(LOG_ERR, 0x0d19, "rd_dummy_meta_idx %d, wr_dummy_meta_idx %d",rd_dummy_meta_idx,wr_dummy_meta_idx);
	evt_register(scheduler_l2p_srch_done, gid, srch_evt);
	evt_register(scheduler_chk_stream_rd_idle, gid, &evt_sched_mon);
#ifndef DUAL_BE
	evt_register(scheduler_l2p_srch_done, 1, &evt_l2p_srch1);
#endif

#if defined(TCG_NAND_BACKUP)
	evt_register(scheduler_mbr_read_pending, 0, &evt_mbr_rd_pending);
	cpu_msg_register(CPU_MSG_MBR_RD, ipc_mbr_host_read);
#endif

	die_mgr_init(shr_nand_info.lun_num, gid);
	read_error_init();
}

fast_code void __ftl_core_reset_wait_sched_idle_done(u32 grp, ftl_reset_t *reset)
{
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_SCHED_ABORT_STRM_READ_DONE, grp, (u32)reset);
}

#if (CPU_ID == 2 || CPU_ID == 4)
 typedef enum{
    fill_Agingtest = 0,
	fill_Bitmap,
	fill_P1list,
	fill_P2list,
	fill_CTQ,
	fill_HEADER

} VuSRBType;
extern int ncl_access_mr(rda_t *rda_list, enum ncl_cmd_op_t op, bm_pl_t *dtag, u32 count);

fast_code row_t nda2row(u32 pgn, u8 pln, u16 blk, u8 lun)
{
	return (pgn << nand_row_page_shift()) | (pln << nand_row_plane_shift()) | (blk << nand_row_blk_shift()) | (lun << nand_row_lun_shift());
}

void Aging_Program_SRB(void *record, u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page,u8 flag)
{
    u32 i;
#if 0
    Fda_t fda = {
        .b.row = nda2row(0, 0, 0, 1),
        .b.du = 0,
        .b.ch = 0,
        .b.ce = 3
    };
    erase_blks(&fda, 1, NAL_PB_TYPE_SLC, &erase_info);
#endif
    dtag_t dbase[SRB_MR_DU_CNT_PAGE];   //SRB_MR_DU_CNT_PAGE??
    schl_apl_trace(LOG_DEBUG, 0xad1b, "SRB_MR_DU_CNT_PAGE1  dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase):%d",SRB_MR_DU_CNT_PAGE);
    sys_assert(dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);
    bm_pl_t pl[4] = {
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#if SRB_MR_DU_CNT_PAGE == 3
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#endif
	};
    rda_t aging_rda = {
        .ch = Ch,
        .dev = CE,
        .pb_type = NAL_PB_TYPE_SLC,
        .row = nda2row(page,plane,0,Lun)
    };
    rda_t PRCS_rda[SRB_MR_DU_CNT_PAGE];
    if(flag==FLAG_AGINGTESTMAP) {
        memcpy(dtag2mem(dbase[0]), record, sizeof(AGING_TEST_MAP_t));//1
    } else if(flag==FLAG_AGINGBITMAP|| flag==FLAG_AGINGP1LISTTABLE || flag==FLAG_AGINGP2LISTTABLE || flag==FLAG_ECCTABLE) {
   for(i=0;i<SRB_MR_DU_CNT_PAGE;i++){
             memcpy(dtag2mem(dbase[i]), (void *)(record+i*SRB_MR_DU_SZE),SRB_MR_DU_SZE);
    }
    } else if(flag==FLAG_AGINGCTQMAP) {
		memcpy(dtag2mem(dbase[0]), record, sizeof(CTQ_t));
}
	else if(flag==FLAG_AGINGHEADER) {
		memcpy(dtag2mem(dbase[0]), record, sizeof(stNOR_HEADER));
	}
   for(i=0;i<SRB_MR_DU_CNT_PAGE;i++){
			PRCS_rda[i] = aging_rda;
			PRCS_rda[i].du_off = i;
			pl[i].pl.dtag = dbase[i].dtag;
   }
   if(ncl_access_mr(PRCS_rda, NCL_CMD_OP_WRITE, pl, 1)==0) {
        //schl_apl_trace(LOG_ERR, 0, "Program ok \n");
    #if 1
        if(flag==FLAG_AGINGTESTMAP) {
            schl_apl_trace(LOG_DEBUG, 0x6f69, "Program AgingTestMap, page: %d \n",page);
        } else if(flag==FLAG_AGINGBITMAP) {
            schl_apl_trace(LOG_DEBUG, 0xb42e, "Program AgingBitMap, page: %d \n",page);
        } else if(flag == FLAG_AGINGP1LISTTABLE) {                                           //if(flag==FLAG_AGINGP1LISTTABLE)
            schl_apl_trace(LOG_DEBUG, 0x61be, "Program P1list Table, page: %d \n",page);
        } else if(flag ==FLAG_AGINGP2LISTTABLE) {
            schl_apl_trace(LOG_DEBUG, 0x90bd, "Program P2list Table, page: %d \n",page);
        } else if(flag == FLAG_AGINGCTQMAP) {
	    schl_apl_trace(LOG_DEBUG, 0xd27f, "Program AgingCTQMap, page: %d \n",page);
        }
	  else if(flag == FLAG_ECCTABLE) {
            schl_apl_trace(LOG_DEBUG, 0x4703, "Program ECC_Table, page: %d \n",page);
    	}
    #endif

    }else {
        schl_apl_trace(LOG_ERR, 0x0d0a, "Program fail \n");
    }
   dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
}
 inline void fill_SRB(u32 SRBtype)
{
	int i = 0;
	int page = 0;
	switch(SRBtype)
	{
		case fill_Agingtest:
			Aging_Program_SRB((void *) MPIN,4,0,0,0,AGINGTESTMAP_STARTPAGE,FLAG_AGINGTESTMAP);
				break;

		case fill_Bitmap:
			page = sizeof(AgingPlistBitmap_t)/(SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			for(i=0;i<page;i++)
			{
				Aging_Program_SRB((void *)((u32)AgingPlistBitmap + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,0,0, 0,(AGINGBITMAP_STARTPAGE + i), FLAG_AGINGBITMAP);
			}
			break;

		case fill_P1list:
			page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
            for(i=0;i<page;i++)
			{
               Aging_Program_SRB((void *)((u32)AgingP1listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,0,0, 0, (AGINGP1LISTTABLE_STARTPAGE + i), FLAG_AGINGP1LISTTABLE);
            }
			break;

		case fill_P2list:
			page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			for(i=0;i<page;i++)
			{
			   Aging_Program_SRB((void *)((u32)AgingP2listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,0,0, 0, (AGINGP2LISTTABLE_STARTPAGE+i), FLAG_AGINGP2LISTTABLE);
			}
			break;

		case fill_CTQ:
			Aging_Program_SRB((void *) Aging_CTQ,4,0, 0, 0, AGINGCTQMAP_STARTPAGE, FLAG_AGINGCTQMAP);
			break;

		case fill_HEADER:
			Aging_Program_SRB((void *) InfoHeader,4,0,0, 0, AGINGHEADER_STARTPAGE, FLAG_AGINGHEADER);
			break;

		default:
			schl_apl_trace(LOG_ERR, 0x743b, "fill error space in SRB\n");
			break;
	}
	return;

}
 inline void fill_cur_SRB(u32 SRBtype, u8 pl)					//Sean_20220705
 {
	 int i = 0;
	 int page = 0;
	 switch(SRBtype)
	 {
		 case fill_Agingtest:
			 Aging_Program_SRB((void *) MPIN, 4, 0, 0, pl, AGINGTESTMAP_STARTPAGE,FLAG_AGINGTESTMAP);  //Sean_20220511 ONLY CPU2
				 break;
		 case fill_Bitmap:
			 page = sizeof(AgingPlistBitmap_t)/(SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			 for(i = 0; i < page; i++)
			 {
				 Aging_Program_SRB((void *)((u32)AgingPlistBitmap + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)), 4, 0, 0, pl, (AGINGBITMAP_STARTPAGE + i), FLAG_AGINGBITMAP);
			 }
			 break;
		 case fill_P1list:
			 page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			 for(i = 0; i < page; i++)
			 {
				Aging_Program_SRB((void *)((u32)AgingP1listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)), 4, 0, 0, pl, (AGINGP1LISTTABLE_STARTPAGE + i), FLAG_AGINGP1LISTTABLE);
			 }
			 break;
		 case fill_P2list:
			 page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			 for(i = 0; i < page; i++)
			 {
				Aging_Program_SRB((void *)((u32)AgingP2listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)), 4, 0, 0, pl, (AGINGP2LISTTABLE_STARTPAGE+i), FLAG_AGINGP2LISTTABLE);
			 }
			 break;
		 case fill_CTQ:
			 Aging_Program_SRB((void *) Aging_CTQ, 4, 0, 0, pl, AGINGCTQMAP_STARTPAGE, FLAG_AGINGCTQMAP);
			 break;
		 case fill_HEADER:
			 Aging_Program_SRB((void *) InfoHeader, 4, 0, 0, pl, AGINGHEADER_STARTPAGE, FLAG_AGINGHEADER);
			 break;
		 default:
			 schl_apl_trace(LOG_ERR, 0x0c66, "fill error space in SRB\n");
			 break;
	 }
	 return;
 }
 void AgingSRB_Erase(u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page)
{
	struct ncl_cmd_t _ncl_cmd;
	struct ncl_cmd_t *ncl_cmd = &_ncl_cmd;
	pda_t *pda;
    u32 pda_value,pda_index;
	struct info_param_t *info;
    schl_apl_trace(LOG_ERR, 0x92a4, "SRB Erase: \n");
	pda = sys_malloc(FAST_DATA, sizeof(pda_t));
	sys_assert(pda);
	info = sys_malloc(FAST_DATA, sizeof(*info));
	sys_assert(info);
	memset(info, 0, sizeof(*info));
    pda_index = (page*nand_info.geo.nr_luns*nand_info.geo.nr_targets*nand_info.geo.nr_channels*nand_info.geo.nr_planes
            + Lun*nand_info.geo.nr_targets*nand_info.geo.nr_channels*nand_info.geo.nr_planes
            + CE*nand_info.geo.nr_channels*nand_info.geo.nr_planes + Ch*nand_info.geo.nr_planes + plane);
    pda_value = pda_index * DU_CNT_PER_PAGE;
    *pda = (pda_t) pda_value;
    info->pb_type = NAL_PB_TYPE_XLC;
	ncl_cmd->completion = NULL;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
	ncl_cmd->addr_param.common_param.list_len = 1;  //width
	ncl_cmd->addr_param.common_param.pda_list = pda;
	ncl_cmd->addr_param.common_param.info_list = info;
	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd_submit(ncl_cmd);
	sys_free(FAST_DATA, pda);
	sys_free(FAST_DATA, info);
}

#if (CPU_ID == 2)
fast_code void be_do_vu_command_CPU2(volatile cpu_msg_req_t *req)
{
	ipc_evt_t *evt = (ipc_evt_t *) req->pl;
	schl_apl_trace(LOG_ERR, 0x7d1c, "in be do vu cpu2\n");

	switch (evt->evt_opc) {

		case VSC_Refresh_SysInfo:
		{
			u8 pl = 0;
			for (pl = 0; pl < 2; pl++){
				ncl_enter_mr_mode();

				#if defined(HMETA_SIZE)
				extern u8 cmf_idx;
				if (cmf_idx == 3 || cmf_idx == 4) {
					eccu_dufmt_switch(0);
					eccu_switch_cmf(0);
				}
				#endif

					AgingSRB_Erase(4, 0, 0, pl, 0);
					fill_cur_SRB(fill_Agingtest, pl);
					fill_cur_SRB(fill_Bitmap, pl);
					fill_cur_SRB(fill_P1list, pl);
					fill_cur_SRB(fill_P2list, pl);
					fill_cur_SRB(fill_CTQ, pl);
					fill_cur_SRB(fill_HEADER, pl);

				#if defined(HMETA_SIZE)
				while (fcmd_outstanding_cnt != 0) {
					ficu_done_wait();
				}
				evlog_printk(LOG_ALW, "Leave MR, cmfidx %d", cmf_idx);
				if (cmf_idx == 3) {
					eccu_dufmt_switch(3);
					eccu_switch_cmf(3);
				} else if (cmf_idx == 4) {
					eccu_dufmt_switch(4);
					eccu_switch_cmf(4);
				}
				#endif

				ncl_leave_mr_mode();
			}
			schl_apl_trace(LOG_ERR, 0xfcbe, "Refresh_SysInfo DOWN! CPU2\n");
			break;
		}

		default:
			schl_apl_trace(LOG_ERR, 0x49e3, "not support yet CPU2\n");
			break;
	}

        cpu_msg_sync_done(req->cmd.tx);
}


#endif
#endif

#if SCHEDULER == 2

/*!
 * @brief ipc insert remote write/erase request
 *
 * @param req		ipc request
 *
 * @return		not used
 */
fast_code void ipc_wr_era_ins(volatile cpu_msg_req_t *req)
{
	ncl_req_t *ncl_req = (ncl_req_t*)req->pl;

	die_que_wr_era_ins(ncl_req, ncl_req->die_id);
}
fast_code void ipc_tzu_get()
{
	schl_apl_trace(LOG_ERR, 0xa26e, "tzu cpu4 gc scheduler avail cnt %d",gc_scheduler.ddtag_avail);
}

/*!
 * @brief ipc raid correct done handler
 *
 * @param req		ipc request
 *
 * @return		not used
 */
fast_code void ipc_raid_correct_done(volatile cpu_msg_req_t *req)
{
	rc_req_t *rc_req = (rc_req_t*)req->pl;

	rc_req->cmpl(rc_req);
}

fast_code void ipc_gc_grp_suspend(volatile cpu_msg_req_t *req)
{
	u32 grp = req->pl;

	gc_pda_grp[grp].flags.b.abort = true;
}

/*!
 * @brief ipc api to abort stream read in scheduler
 */
fast_code void ipc_abort_stream_read(volatile cpu_msg_req_t *req)
{
	//printk("[reset] %x.%x\n",req->cmd.flags,SCHEDULER);
	sys_assert(req->cmd.flags == SCHEDULER);
	scheduler_abort_host_streaming_read((ftl_reset_t *)req->pl);
}

/*!
 * @brief ipc api to resume stream read in scheduler
 */
fast_code void ipc_resume_stream_read(volatile cpu_msg_req_t *req)
{
	sys_assert(req->pl == SCHEDULER);
	scheduler_resume_host_streaming_read();
}

/*!
 * @brief light back-end scheduler suspend
 *
 * @param mode		not used
 *
 * @return		not used
 */
ps_code bool be_lite_suspend(enum sleep_mode_t mode)
{
	scheduler_suspend(1);
	return true;
}

/*!
 * @brief light back-end scheduler resume
 *
 * @param mode		not used
 *
 * @return		not used
 */
ps_code void be_lite_resume(enum sleep_mode_t mode)
{
	scheduler_resume(1);
}

init_code void gc_grp_res_free_init(void)
{
	CBF_INIT(gc_res_free_que);
	gc_res_free_recv_hook((cbf_t *)gc_res_free_que, scheduler_gc_free_res);
}

/*!
 * @brief be lite initialize
 *
 * @return	not used
 */

 #if 0
 typedef enum{
    fill_Agingtest = 0,
	fill_Bitmap,
	fill_P1list,
	fill_P2list,
	fill_CTQ,
	fill_HEADER

} VuSRBType;
extern int ncl_access_mr(rda_t *rda_list, enum ncl_cmd_op_t op, bm_pl_t *dtag, u32 count);

fast_code row_t nda2row(u32 pgn, u8 pln, u16 blk, u8 lun)
{
	return (pgn << nand_row_page_shift()) | (pln << nand_row_plane_shift()) | (blk << nand_row_blk_shift()) | (lun << nand_row_lun_shift());
}

void Aging_Program_SRB(void *record, u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page,u8 flag)
{
    u32 i;
#if 0
    Fda_t fda = {
        .b.row = nda2row(0, 0, 0, 1),
        .b.du = 0,
        .b.ch = 0,
        .b.ce = 3
    };
    erase_blks(&fda, 1, NAL_PB_TYPE_SLC, &erase_info);
#endif
    dtag_t dbase[SRB_MR_DU_CNT_PAGE];   //SRB_MR_DU_CNT_PAGE??
    schl_apl_trace(LOG_INFO, 0x6a50, "SRB_MR_DU_CNT_PAGE1  dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase):%d",SRB_MR_DU_CNT_PAGE);
    sys_assert(dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);
    bm_pl_t pl[4] = {
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#if SRB_MR_DU_CNT_PAGE == 3
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#endif
	};
    rda_t aging_rda = {
        .ch = Ch,
        .dev = CE,
        .pb_type = NAL_PB_TYPE_SLC,
        .row = nda2row(page,plane,0,Lun)
    };
    rda_t PRCS_rda[SRB_MR_DU_CNT_PAGE];
    if(flag==FLAG_AGINGTESTMAP) {
        memcpy(dtag2mem(dbase[0]), record, sizeof(AGING_TEST_MAP_t));//1
    } else if(flag==FLAG_AGINGBITMAP|| flag==FLAG_AGINGP1LISTTABLE || flag==FLAG_AGINGP2LISTTABLE || flag==FLAG_ECCTABLE) {
   for(i=0;i<SRB_MR_DU_CNT_PAGE;i++){
             memcpy(dtag2mem(dbase[i]), (void *)(record+i*SRB_MR_DU_SZE),SRB_MR_DU_SZE);
    }
    } else if(flag==FLAG_AGINGCTQMAP) {
		memcpy(dtag2mem(dbase[0]), record, sizeof(CTQ_t));
}
	else if(flag==FLAG_AGINGHEADER) {
		memcpy(dtag2mem(dbase[0]), record, sizeof(stNOR_HEADER));
	}
   for(i=0;i<SRB_MR_DU_CNT_PAGE;i++){
			PRCS_rda[i] = aging_rda;
			PRCS_rda[i].du_off = i;
			pl[i].pl.dtag = dbase[i].dtag;
   }
   if(ncl_access_mr(PRCS_rda, NCL_CMD_OP_WRITE, pl, 1)==0) {
        //schl_apl_trace(LOG_ERR, 0, "Program ok \n");
    #if 1
        if(flag==FLAG_AGINGTESTMAP) {
            schl_apl_trace(LOG_ERR, 0x8236, "Program AgingTestMap, page: %d \n",page);
        } else if(flag==FLAG_AGINGBITMAP) {
            schl_apl_trace(LOG_ERR, 0xccb5, "Program AgingBitMap, page: %d \n",page);
        } else if(flag == FLAG_AGINGP1LISTTABLE) {                                           //if(flag==FLAG_AGINGP1LISTTABLE)
            schl_apl_trace(LOG_ERR, 0x5e30, "Program P1list Table, page: %d \n",page);
        } else if(flag ==FLAG_AGINGP2LISTTABLE) {
            schl_apl_trace(LOG_ERR, 0x4d8d, "Program P2list Table, page: %d \n",page);
        } else if(flag == FLAG_AGINGCTQMAP) {
	    schl_apl_trace(LOG_ERR, 0x1cba, "Program AgingCTQMap, page: %d \n",page);
        }
	  else if(flag == FLAG_ECCTABLE) {
            schl_apl_trace(LOG_ERR, 0x20d3, "Program ECC_Table, page: %d \n",page);
    	}
    #endif

    }else {
        schl_apl_trace(LOG_ERR, 0x6585, "Program fail \n");
    }
   dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
}

inline void fill_SRB(u32 SRBtype)
{
	int i = 0;
	int page = 0;
	switch(SRBtype)
	{
		case fill_Agingtest:
			Aging_Program_SRB((void *) MPIN,4,0,0,0,AGINGTESTMAP_STARTPAGE,FLAG_AGINGTESTMAP);
				break;

		case fill_Bitmap:
			page = sizeof(AgingPlistBitmap_t)/(SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			for(i=0;i<page;i++)
			{
				Aging_Program_SRB((void *)((u32)AgingPlistBitmap + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,0,0, 0,(AGINGBITMAP_STARTPAGE + i), FLAG_AGINGBITMAP);
			}
			break;

		case fill_P1list:
			page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
            for(i=0;i<page;i++)
			{
               Aging_Program_SRB((void *)((u32)AgingP1listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,0,0, 0, (AGINGP1LISTTABLE_STARTPAGE + i), FLAG_AGINGP1LISTTABLE);
            }
			break;

		case fill_P2list:
			page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			for(i=0;i<page;i++)
			{
			   Aging_Program_SRB((void *)((u32)AgingP2listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,0,0, 0, (AGINGP2LISTTABLE_STARTPAGE+i), FLAG_AGINGP2LISTTABLE);
			}
			break;

		case fill_CTQ:
			Aging_Program_SRB((void *) Aging_CTQ,4,0, 0, 0, AGINGCTQMAP_STARTPAGE, FLAG_AGINGCTQMAP);
			break;

		case fill_HEADER:
			Aging_Program_SRB((void *) InfoHeader,4,0,0, 0, AGINGHEADER_STARTPAGE, FLAG_AGINGHEADER);
			break;

		default:
			schl_apl_trace(LOG_ERR, 0x72ed, "fill error space in SRB\n");
			break;
	}
	return;

}

 inline void fill_cur_SRB(u32 SRBtype, u8 ce)                   //Sean_20220512
{
	int i = 0;
	int page = 0;
	switch(SRBtype)
	{
		case fill_Agingtest:
			Aging_Program_SRB((void *) MPIN,4,ce,0,0,AGINGTESTMAP_STARTPAGE,FLAG_AGINGTESTMAP);  //Sean_20220511
				break;

		case fill_Bitmap:
			page = sizeof(AgingPlistBitmap_t)/(SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			for(i=0;i<page;i++)
			{
				Aging_Program_SRB((void *)((u32)AgingPlistBitmap + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,ce,0, 0,(AGINGBITMAP_STARTPAGE + i), FLAG_AGINGBITMAP);
			}
			break;

		case fill_P1list:
			page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
            for(i=0;i<page;i++)
			{
               Aging_Program_SRB((void *)((u32)AgingP1listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,ce,0, 0, (AGINGP1LISTTABLE_STARTPAGE + i), FLAG_AGINGP1LISTTABLE);
            }
			break;

		case fill_P2list:
			page = sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1;
			for(i=0;i<page;i++)
			{
			   Aging_Program_SRB((void *)((u32)AgingP2listTable + (i*SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE)),4,ce,0, 0, (AGINGP2LISTTABLE_STARTPAGE+i), FLAG_AGINGP2LISTTABLE);
			}
			break;

		case fill_CTQ:
			Aging_Program_SRB((void *) Aging_CTQ,4,ce, 0, 0, AGINGCTQMAP_STARTPAGE, FLAG_AGINGCTQMAP);
			break;

		case fill_HEADER:
			Aging_Program_SRB((void *) InfoHeader,4,ce,0, 0, AGINGHEADER_STARTPAGE, FLAG_AGINGHEADER);
			break;

		default:
			schl_apl_trace(LOG_ERR, 0x5f28, "fill error space in SRB\n");
			break;
	}
	return;

}

 void AgingSRB_Erase(u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page)
{
	struct ncl_cmd_t _ncl_cmd;
	struct ncl_cmd_t *ncl_cmd = &_ncl_cmd;
	pda_t *pda;
    u32 pda_value,pda_index;
	struct info_param_t *info;
    schl_apl_trace(LOG_ERR, 0x08eb, "SRB Erase: \n");
	pda = sys_malloc(FAST_DATA, sizeof(pda_t));
	sys_assert(pda);
	info = sys_malloc(FAST_DATA, sizeof(*info));
	sys_assert(info);
	memset(info, 0, sizeof(*info));
    pda_index = (page*nand_info.geo.nr_luns*nand_info.geo.nr_targets*nand_info.geo.nr_channels*nand_info.geo.nr_planes
            + Lun*nand_info.geo.nr_targets*nand_info.geo.nr_channels*nand_info.geo.nr_planes
            + CE*nand_info.geo.nr_channels*nand_info.geo.nr_planes + Ch*nand_info.geo.nr_planes + plane);
    pda_value = pda_index * DU_CNT_PER_PAGE;
    *pda = (pda_t) pda_value;
    info->pb_type = NAL_PB_TYPE_XLC;
	ncl_cmd->completion = NULL;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
	ncl_cmd->addr_param.common_param.list_len = 1;  //width
	ncl_cmd->addr_param.common_param.pda_list = pda;
	ncl_cmd->addr_param.common_param.info_list = info;
	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd_submit(ncl_cmd);
	sys_free(FAST_DATA, pda);
	sys_free(FAST_DATA, info);
}
#endif

 typedef struct
{
	union {
	u32 all;
	struct{
    u32  ch        :4;   ///< DW13 Byte 0 CECH
    u32  ce        :4;
    u32  lun       :4;   ///< DW13 Byte 1 Mode
    u32  plane     :4;
    u32  Dw12Byte2 :8;
    u32  Dw12Byte3 :8;
	}b;
	};
}tVSC_evt1_Mode;//albert 20200622 for VU

 slow_code u8 reg_nand_temp(int data13)   ///static????
{
	u8 data[2];
    int ch;
    int ce;
	tVSC_evt1_Mode dw13;

	dw13.all = data13;
	ce = dw13.b.ce;
	ch = dw13.b.ch;

    ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 0,
		.cle_mode = 0,
		.reg1.b.ind_byte0 = 0xB9,
		.xfcnt = 0,
	};

	ndcu_ind_t ctrl_r2 ={
		.write = false,
		.cmd_num = 0,
		.cle_mode = 0,
		.reg1.b.ind_byte0 = 0x7C,
		.xfcnt = 4,
		.buf = (u8 *)&data,
	};

    ficu_mode_disable();
	ndcu_en_reg_control_mode();

    //schl_apl_trace(LOG_ERR, 0, "0xB9\n");
    ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
    ndcu_close(&ctrl);
    ndcu_delay(30);

   /* ndcu_ind_t ctrl_r = {
		.write = false,
		.cmd_num = 0,
		.cle_mode = 0,
		.reg1.b.ind_byte0 = 0x70,
		.xfcnt = 1,
		.buf = &data,
	};

    schl_apl_trace(LOG_ERR, 0, "0x70\n");
	ndcu_open(&ctrl_r, ch, ce);
	ndcu_start(&ctrl_r);
	ndcu_delay(5);
	do {
		if (ndcu_xfer(&ctrl_r))
			break;
	} while (1);
	ndcu_close(&ctrl_r);

    schl_apl_trace(LOG_ERR, 0, "CH[%2d] CE[%2d] Output Nand Temperature = %d \n",ch,ce,data);
    */

    //schl_apl_trace(LOG_ERR, 0, "0x7C\n");
	ndcu_open(&ctrl_r2, ch, ce);
	ndcu_start(&ctrl_r2);
	ndcu_delay(250);
	do {
		if (ndcu_xfer(&ctrl_r2))
			break;
	} while (1);
	ndcu_close(&ctrl_r2);
    ndcu_dis_reg_control_mode();
    ficu_mode_enable();
	if(!MPIN->PD_LED_Flag)
    schl_apl_trace(LOG_ERR, 0x7601, "CH[%2d] CE[%2d] Output Nand Temperature = %d \n",ch,ce,data[0]);
	//..Aging_CTQ->NAND_Temperature = data[0];
    return data[0];
}

fast_code void be_do_vu_command(volatile cpu_msg_req_t *req)//20200310-maurice VU cmd ipc
{
	ipc_evt_t *evt = (ipc_evt_t *) req->pl;
	schl_apl_trace(LOG_ERR, 0xf87b, "in be do vu\n");


	switch (evt->evt_opc) {
		case VSC_ERASE_NAND:
			schl_apl_trace(LOG_ERR, 0x3e1f, "Erase NAND\n");
			break;

		case VSC_SCAN_DEFECT://albert 20200727
        {

            schl_apl_trace(LOG_ERR, 0xb2a0, "scan defect down\n");
            break;
        }
        case VSC_AGING_BATCH://albert 20200529
        {

            schl_apl_trace(LOG_ERR, 0x915b, "aging loop will run after restart\n");
            break;
        }

		case VSC_Read_FlashID:
		{

			break;
		}

		//case VSC_Read_NorID:
		//{
		    //shr_NorID = spi_read_id();
			//break;
		//}

		#if 0
			case VSC_DRAMtag_Clear:
			{
				u8 status;
				ncl_enter_mr_mode();

				status = Aging_Read_SRB((void *)AgingTest,4,0,0,0,0,0,FLAG_AGINGTESTMAP);
				if(status == false)
				{
					schl_apl_trace(LOG_ERR, 0xca06, "Can't read DRAM Tag !!\n");
					break;
				}
				AgingSRB_Erase(4,0,0, 0, 0);
				AgingTest->bDramExecuted = 0x00;

				fill_SRB(fill_Agingtest);
				fill_SRB(fill_Bitmap);
				fill_SRB(fill_P1list);
				fill_SRB(fill_P2list);
				fill_SRB(fill_CTQ);
				fill_SRB(fill_HEADER);

				ncl_leave_mr_mode();

				schl_apl_trace(LOG_ERR, 0x5850, "clean DRAM tag DOWN\n");

				break;
			}

		#endif

		case VSC_Plist_OP:
		{
			schl_apl_trace(LOG_ERR, 0x6fc7, "clean PLIST DOWN\n");
			break;

		}


        case VSC_Read_Tempture://albert 20200724
        {
        	//u16 temperature;
			switch(evt->r1){
			case 1:
				Vu_temp = reg_nand_temp(evt->r0);
				schl_apl_trace(LOG_ERR, 0x3e80, "NandTemp : %d\n",Vu_temp);
				break;
			case 0:
			case 2:
			default:
			schl_apl_trace(LOG_ERR, 0x5fef, "no define or not supported\n");
			break;
			}
			break;
        }
/*
		case VSC_Read_Agingtest://albert 20200806
        {
        	bool ret = 0;
			schl_apl_trace(LOG_ERR, 0, "prepare to srb load\n");
			ncl_enter_mr_mode();
			ret = pull_SRB(fill_Agingtest);
			//ret = pull_SRB(fill_CTQ);
			if(ret)
				{schl_apl_trace(LOG_ERR, 0, "read Agingtest Down\n");}
			else
				{schl_apl_trace(LOG_ERR, 0, "read Agingtest Fail\n");}
			ncl_leave_mr_mode();
			break;

        }

		case VSC_Read_Bitmap://albert 20200806
        {
        	bool ret = 0;
			schl_apl_trace(LOG_ERR, 0, "prepare to srb load\n");
			ncl_enter_mr_mode();
			ret = pull_SRB(fill_Bitmap);
			//ret = pull_SRB(fill_CTQ);
			if(ret)
				{schl_apl_trace(LOG_ERR, 0, "read Bitmap Down\n");}
			else
				{schl_apl_trace(LOG_ERR, 0, "read Bitmap Fail\n");}
			ncl_leave_mr_mode();
				break;

			}

		case VSC_Read_P1list://albert 20200806
        {
        	bool ret = 0;
			schl_apl_trace(LOG_ERR, 0, "prepare to srb load\n");
			ncl_enter_mr_mode();
			ret = pull_SRB(fill_P1list);
			//ret = pull_SRB(fill_CTQ);
			if(ret)
				{schl_apl_trace(LOG_ERR, 0, "read P1list Down\n");}
			else
				{schl_apl_trace(LOG_ERR, 0, "read P1list Fail\n");}
			ncl_leave_mr_mode();
			break;

			}

		case VSC_Read_P2list://albert 20200806
        {
        	bool ret = 0;
			schl_apl_trace(LOG_ERR, 0, "prepare to srb load\n");
			ncl_enter_mr_mode();
			ret = pull_SRB(fill_P2list);
			//ret = pull_SRB(fill_CTQ);
			if(ret)
				{schl_apl_trace(LOG_ERR, 0, "read P2list Down\n");}
			else
				{schl_apl_trace(LOG_ERR, 0, "read P2list Fail\n");}
			ncl_leave_mr_mode();
			break;

        }


		case VSC_Read_CTQ://albert 20200624
        {
        	bool ret = 0;
			schl_apl_trace(LOG_ERR, 0, "prepare to srb load\n");
			ncl_enter_mr_mode();
			ret = pull_SRB(fill_CTQ);
			//ret = pull_SRB(fill_CTQ);
			if(ret)
				{schl_apl_trace(LOG_ERR, 0, "read CTQ Down\n");}
			else
				{schl_apl_trace(LOG_ERR, 0, "read CTQ Fail\n");}
			ncl_leave_mr_mode();
					break;

        }


		case VSC_AGING_ERASEALL://albert 20200730
        {
			u32 spb_id;
    		u32 startblk = 0;
    		u32 endblk = nand_info.geo.nr_blocks;
			u8 slcmode = 0;

			for (spb_id = startblk; spb_id < endblk; spb_id++)
			{
				erase_spb_test(spb_id,slcmode,1);//XLC
			}
			schl_apl_trace(LOG_ERR, 0, "erase all NAND  DOWN !I\n");

			break;

        }

		case VSC_AGING_ERASENAND://albert 20200730
        {
			u32 spb_id;
    		u32 startblk = 2;
    		u32 endblk = nand_info.geo.nr_blocks;
			u8 slcmode = 0;

			for (spb_id = startblk; spb_id < endblk; spb_id++)
			{  //!!!!check startblk DO NOT erase SPB0
				erase_spb_test(spb_id,slcmode,1);//XLC
			}
			schl_apl_trace(LOG_ERR, 0, "erase NAND from BLK 2 DOWN !I\n");
			break;

        }

		case VSC_PREFORMAT://albert 20200730
        {
			u32 spb_id;
    		u32 startblk = 2;
    		u32 endblk = nand_info.geo.nr_blocks;
			u8 slcmode = 0;

			AgingSRB_Erase(6, 0, 0, 0, 0);//erase ECC
			for (spb_id = startblk; spb_id < endblk; spb_id++)
			{  //!!!!check startblk DO NOT erase SPB0
				erase_spb_test(spb_id,slcmode,1);//XLC
			}
			schl_apl_trace(LOG_ERR, 0, "erase NAND from BLK 2 DOWN !I\n");
			break;

        }
*/
#if 0    // move to be_do_vu_command_CPU2
		case VSC_Refresh_SysInfo:
		{
			ncl_enter_mr_mode();

			AgingSRB_Erase(4,0, 0, 0, 0);

			fill_SRB(fill_Agingtest);
			fill_SRB(fill_Bitmap);
			fill_SRB(fill_P1list);
			fill_SRB(fill_P2list);
			fill_SRB(fill_CTQ);
			fill_SRB(fill_HEADER);

            ncl_leave_mr_mode();
			schl_apl_trace(LOG_ERR, 0x271c, "Refresh_SysInfo DOWN! \n");
			break;
		}
#endif
		case VSC_GBBPlus:
		{
			schl_apl_trace(LOG_ERR, 0xc7d7, "add bad block in Plist & Bitmap\n");
			u32 dft[FACTORY_DEFECT_DWORD_LEN] = {0};
			u8 Blk, Ch, CE, Lun, plane;
			u32 temp;
			u32 offset,index;
			pda_t pda = evt->r0;
			Ch = pda2ch(pda);
			CE = pda2ce(pda);
			Lun = pda2lun(pda);
			Blk = pda2blk(pda);
			plane = pda2plane(pda);
			memcpy((void *)dft,(void *)((u32)AgingPlistBitmap+Blk*sizeof(dft)),sizeof(dft));
			//fill P2list
			temp = Lun * nand_info.geo.nr_targets * nand_info.geo.nr_channels * nand_info.geo.nr_planes \
			    + CE * nand_info.geo.nr_channels * nand_info.geo.nr_planes + Ch * nand_info.geo.nr_planes + plane;
			index = temp>>5;
			offset = temp % 32;
			dft[index] |= (1<<offset);
			memcpy((void *)((u32)AgingPlistBitmap + Blk*sizeof(dft)),(void *)dft,sizeof(dft));
			if(AgingP2listTable->AgingPlistCount[Ch][CE]>=(MAX_DEFECT_BLOCK_NUMBER-1))
			{
				schl_apl_trace(LOG_ERR, 0x9240, "P2list overlimit!");
			}
			else
			{
				AgingP2listTable->AgingPlistTable[Ch][CE][AgingP2listTable->AgingPlistCount[Ch][CE]] = (Lun << (nand_info.row_lun_shift-nand_info.row_pl_shift)) | (Blk * 2 + plane);
			}
			AgingP2listTable->AgingPlistCount[Ch][CE]++;
			break;
		}

		default:
			schl_apl_trace(LOG_ERR, 0x1e86, "not support yet\n");
			break;
	}

        cpu_msg_sync_done(req->cmd.tx);
}

#if defined(SAVE_CDUMP)
ddr_code void ipc_ulink_da_mode4(volatile cpu_msg_req_t *req)
{
	extern u32 ulink_da_mode(void);
	ulink_da_mode();
}
#endif

void SysInfo_update(void)
{
	extern epm_info_t* shr_epm_info;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	if(MPIN->Disk_PhyOP != 0)
	{
		return;
	}
	else
	{
		//MPIN->Disk_PhyOP = epm_ftl_data->Phyblk_OP; //change to epm_ftl_data->PhyOP = 10*epm_ftl_data->Phyblk_OP
		MPIN->Disk_PhyOP = epm_ftl_data->PhyOP;
	}

	ncl_enter_mr_mode();

	#if defined(HMETA_SIZE)
	extern u8 cmf_idx;
	if (cmf_idx == 3 || cmf_idx == 4) {
		eccu_dufmt_switch(0);
		eccu_switch_cmf(0);
	}
	#endif

	AgingSRB_Erase(4, 0, 0, 0, 0);

	fill_SRB(fill_Agingtest);
	fill_SRB(fill_Bitmap);
	fill_SRB(fill_P1list);
	fill_SRB(fill_P2list);
	fill_SRB(fill_CTQ);
	fill_SRB(fill_HEADER);

	#if defined(HMETA_SIZE)
	while (fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	if (cmf_idx == 3) {
		eccu_dufmt_switch(3);
		eccu_switch_cmf(3);
	} else if (cmf_idx == 4) {
		eccu_dufmt_switch(4);
		eccu_switch_cmf(4);
	}
	#endif

	ncl_leave_mr_mode();
}


ddr_code void SysInfo_bkup(u8 pl)    //Sean_20220705 ONLY CPU4
{

	ncl_enter_mr_mode();

	AgingSRB_Erase(4, 0, 0, pl, 0);

	fill_cur_SRB(fill_Agingtest, pl);
	fill_cur_SRB(fill_Bitmap, pl);
	fill_cur_SRB(fill_P1list, pl);
	fill_cur_SRB(fill_P2list, pl);
	fill_cur_SRB(fill_CTQ, pl);
	fill_cur_SRB(fill_HEADER, pl);

	ncl_leave_mr_mode();
}

ddr_code void ipc_ondeman_dump(volatile cpu_msg_req_t *req)
{
	AplDieQueue_DumpInfo(false);
}

init_code static void be_lite_init(void)
{
	wait_remote_item_done(nand_geo_init);
	wait_remote_item_done(ddr_init);
	wait_remote_item_done(idx_meta_init);
	wait_remote_item_done(fcore_init);

	nand_info = shr_nand_info;
	scheduler_init(&evt_l2p_srch1, 1);

	l2p_mgr_init();
	gc_grp_res_free_init();
	scheduler_gc_grp_init(1, &evt_pda_gen_rsp1_que0);

	ficu_isr_init();
	ficu_isr_resume(SUSPEND_INIT);
	ncl_cmd_init();
	ipc_api_init();
	cpu_msg_register(CPU_MSG_WR_ERA_INS, ipc_wr_era_ins);
	cpu_msg_register(CPU_MSG_TZU_GET, ipc_tzu_get);
	cpu_msg_register(CPU_MSG_RAID_CORRECT_DONE, ipc_raid_correct_done);
	cpu_msg_register(CPU_MSG_GC_GRP_SUSPEND, ipc_gc_grp_suspend);
	cpu_msg_register(CPU_MSG_SCHED_ABORT_STRM_READ, ipc_abort_stream_read);
	cpu_msg_register(CPU_MSG_SCHED_RESUME_STRM_READ, ipc_resume_stream_read);
	cpu_msg_register(CPU_MSG_PLP_CANCEL_DIEQUE, scheduler_plp_cancel_die_que);
#if (FW_BUILD_VAC_ENABLE == mENABLE)
	cpu_msg_register(CPU_MSG_VC_RECON, l2p_vac_recon);
#endif
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	cpu_msg_register(CPU_MSG_GC_PLP_SLC, ipc_issue_gc_read_slc);
#endif	
#if 1 //for vu
	cpu_msg_register(CPU_MSG_EVT_VUCMD_SEND,be_do_vu_command); //_GENE_
#endif
	cpu_msg_register(CPU_MSG_ONDEMAND_DUMP, ipc_ondeman_dump);
#if defined(SAVE_CDUMP)
        cpu_msg_register(CPU_MSG_ULINK, ipc_ulink_da_mode4);
#endif

	ts_start();

	pmu_register_handler(SUSPEND_COOKIE_NCB, NULL,
			RESUME_COOKIE_NCB, ficu_isr_resume);

	pmu_register_handler(SUSPEND_COOKIE_FTL, be_lite_suspend,
			RESUME_COOKIE_FTL, be_lite_resume);

	schl_apl_trace(LOG_ALW, 0x8882, "BE_LITE init done");
	schl_apl_trace(LOG_ERR, 0xc2d0, "tzu gc start %d\n",gc_scheduler.ddtag_start);
//EMP init after ficu and ncl init
#if	epm_enable
	epm_init();
	mpin_init();
#endif

	local_item_done(be_lite_init);

	#if (_TCG_)
	extern void tcg_nf_onetime_init(void);
	tcg_nf_onetime_init();
	local_item_done(tcg_nf_init);
	schl_apl_trace(LOG_INFO, 0x4c63, "be_lite: tcg_nf_onetime_init Done!!");
	#endif
}
/*! \cond PRIVATE */
module_init(be_lite_init, z.1);
#endif
/*! \endcond */
/*! @} */
