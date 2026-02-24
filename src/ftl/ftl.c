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
/*! \file ftl.c
 * @brief ftl
 *
 * \addtogroup ftl
 * \defgroup ftl
 * \ingroup ftl
 * @{
 * initialize and start ftl module, it will create default namespace,
 * spb manager and spb pool.
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "ftlprecomp.h"
#include "mod.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "mpc.h"
#include "ipc_api.h"
#include "ftl_flash_geo.h"
#include "frb_log.h"
#include "defect_mgr.h"
#include "spb_info.h"
#include "spb_log.h"
#include "spb_log_flush.h"
#include "system_log.h"
#include "ddr.h"
#include "ftl_ns.h"
#include "ftl_l2p.h"
#include "spb_pool.h"
#include "spb_mgr.h"
#include "srb.h"
#include "console.h"
#include "gc.h"
#include "idx_meta.h"
#include "pmu.h"
#include "blk_pool.h"
#include "blk_log.h"
#include "sync_ncl_helper.h"
#include "l2cache.h"
#include "fe_info.h"
#include "bg_task.h"
#include "recon.h"
#include "die_que.h"
#include "scheduler.h"
#include "ftl_spor.h"
#include "ErrorHandle.h"
#include "GList.h"
#include "read_retry.h"
#include "fc_export.h"
#include "ddr_top_register.h"

#include "nand.h"
#include "dtag.h"
#include "misc.h"
#include "misc_register.h"
#include "rdisk.h"

#include "read_error.h"
#include "ncl.h"
// 20210304 Jamie
#include "epm.h"
#include "nvme_spec.h"
/*! \cond PRIVATE */
#define __FILEID__ ftl
#include "trace.h"
/*! \endcond */

#define MAX_SPB_LOG_BLK		(1 + 1)			///< current + next
#define MAX_SYS_LOG_BLK		(MAX_SPB_LOG_BLK * 2)	///< (previous + current + next) * 2 (backup)

#define FTL_L2P_UPDT_QID	1

typedef struct _ipc_fe_req_t {
	fe_req_t fe_req;
	u8 tx;
	bool sync;
	void *caller;
} ipc_fe_req_t;

typedef struct {
	pda_t pda[EACH_DIE_DTAG_CNT];
} ftl_updt_pda_t;

fast_data_ni ftl_updt_pda_t *ftl_updt_pda;
fast_data_zi struct du_meta_fmt *sram_dummy_meta;		///< dummy meta data
fast_data_zi struct du_meta_fmt *ddr_dummy_meta;		///< dummy meta data, not used now
fast_data_zi u32 sram_dummy_meta_idx;			///< meta index of sram dummy meta
fast_data_zi u32 ddr_dummy_meta_idx;			///< meta index of ddr dummy meta
fast_data_zi u16 epm_ftl_inv_idx = 0;

fast_data_zi ftl_flash_geo_t ftl_flash_geo;		///< ftl flash geometry data structure
//fast_data u8 srb_reserved_spb_cnt = 2;			///< it should be revised to a function, the first 2 spb was reserved by SRB
fast_data u8 srb_reserved_spb_cnt = 1;			///< it should be revised to a function, the first 2 spb was reserved by SRB
#if 1//def TCG_NAND_BACKUP
fast_data u8 tcg_reserved_spb_cnt = 3;   //512G :revserve the last 3 blk for TCG. 1T/2T: revserve the last 2 blk for TCG
#else
fast_data u8 tcg_reserved_spb_cnt = 0;   //Non-TCG, release to user data
#endif
init_data ftl_boot_lvl_t ftl_boot_lvl = 1;	///< ftl boot level, refer to ftl_boot_lvl_t
init_data bool ftl_virgin = false;					///< only used to init ns from virgin

fast_data_zi u32 ddr_buf = 0;				///< used for ftl IO buffer
fast_data_zi u32 ddr_buf_sz = 0;			///< indiciate ftl io buffer size
fast_data u8 evt_ftl_start = 0xff;
fast_data u8 evt_ftl_gc_check = 0xFF;
fast_data_ni pblk_t spb_log_pblk[MAX_SPB_LOG_BLK];
fast_data_ni pblk_t sys_log_pblk[MAX_SYS_LOG_BLK];
share_data volatile u16 need_page;
share_data_ni volatile l2p_updt_ntf_que_t l2p_updt_ntf_que;
share_data_ni volatile l2p_updt_que_t l2p_updt_que;
share_data_ni volatile l2p_updt_que_t l2p_updt_done_que;
fast_data bool l2p_updt_block = false;
share_data dtag_free_que_t dtag_free_que;
share_data_ni ncl_w_req_t ncl_w_reqs[RDISK_NCL_W_REQ_CNT];
extern bool gc_continue;
extern u32 evlog_next_index;

share_data_zi bool shr_ftl_save_qbt_flag;
share_data_zi spb_id_t       host_spb_close_idx;/// runtime close blk idx   Curry
share_data_zi spb_id_t       gc_spb_close_idx;/// runtime close blk idx   Curry
share_data_zi volatile u8 shr_lock_power_on;
share_data_zi volatile pbt_resume_t *pbt_resume_param;
fast_data_zi u8 patrol_time;
fast_data_zi bool unlock_power_on;

share_data volatile bool _fg_warm_boot;
share_data volatile u32 shr_avg_erase;
share_data volatile u32 shr_max_erase;
share_data volatile u32 shr_min_erase;
share_data volatile u32 shr_total_ec;
fast_data_ni struct timer_list patrol_read_timer;

fast_data_ni struct timer_list save_log_timer;

#if CO_SUPPORT_DEVICE_SELF_TEST
fast_data_ni struct timer_list DST_patrol_read_timer;
fast_data u8 evt_DST_patrol_read = 0xFF;
extern volatile bool DST_pard_fail_flag;
extern u32 DST_pard_fail_LBA[2];
extern volatile bool DST_pard_done_flag;
extern volatile bool DST_abort_flag;
extern u8 gCurrDSTOperationImmed;
extern volatile u32 DST_wl_index;
ddr_data bool open_blk_find = false;
#endif

fast_data_ni struct timer_list lock_power_on_timer;
//fast_data_ni struct timer_list patrol_read_timer_4;
share_data volatile enum du_fmt_t host_du_fmt;
share_data struct nand_info_t shr_nand_info;		///< shared nand information

share_data_zi volatile u32 shr_pa_rd_ddtag_start;
share_data_zi volatile bool shr_shutdownflag;
slow_data bool FTL_NO_LOG = false;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
extern volatile u8 SLC_init;
#endif
#if (FW_BUILD_VAC_ENABLE == mENABLE)
extern volatile u8 CPU3_spor_state;
extern volatile u8 fw_memory_not_enough;
#endif

#ifdef ERRHANDLE_ECCT
share_data_zi stECCT_ipc_t rc_ecct_info[MAX_RC_REG_ECCT_CNT];
share_data_zi tEcctReg ecct_pl_info_cpu2[MAX_RC_REG_ECCT_CNT];
share_data_zi tEcctReg ecct_pl_info_cpu4[MAX_RC_REG_ECCT_CNT];
share_data_zi volatile u8 rc_ecct_cnt;
share_data_zi u8 ecct_pl_cnt2;
share_data_zi u8 ecct_pl_cnt4;
#endif

#ifdef RD_FAIL_GET_LDA
share_data rd_get_lda_t *rd_err_lda_entry_0;  
share_data rd_get_lda_t *rd_err_lda_entry_1;
share_data rd_get_lda_t *rd_err_lda_entry_ua;
share_data u16 rd_err_lda_ptr_0;
share_data u16 rd_err_lda_ptr_1;
#endif

share_data volatile u8 plp_trigger;
extern bool all_init_done;
extern bool gFormatFlag;
extern u16 min_good_pl;
extern u32 CAP_NEED_PHYBLK_CNT;
extern u8 evt_wb_save_pb;
extern spb_id_t last_spb_vcnt_zero;

#if(SPOR_L2P_VC_CHK == mENABLE)
share_data volatile u8 shr_flag_vac_compare;
share_data volatile u8 shr_flag_vac_compare_result;
#endif

#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
extern volatile bool delay_flush_spor_qbt;  
#endif 

slow_data_zi volatile u16 spor_read_pbt_blk[4];
slow_data_zi volatile u8 spor_read_pbt_cnt = 0;

extern volatile bool PLP_IC_SGM41664;

#if GC_SUSPEND_FWDL
share_data_zi volatile u32 fwdl_gc_handle_dtag_start;
share_data_zi volatile u8 fwdl_gcsuspend_done;
#endif

fast_data_zi bool blklist_flush_query[2];
void host_pda_updt(void);
void gc_action_done(gc_action_t *action);
void fwdl_gc_action_done(gc_action_t *action);
void warmboot_gc_action_done(gc_action_t *action);
extern bool l2p_mgr_vcnt_rebuild(void);
extern void ipc_get_telemetry_ctrlr_from_U3(volatile cpu_msg_req_t *req);

fast_data ftl_setting_t ftl_setting = {
		.version = 3,
		.op = 10000, .tbl_op = OP_5000,
		.tbw = 65,
		.burst_wr_mb = 6,
		.slc_ep = 30000,
		.native_ep = 7000,
		.user_spare = 0,	/// not used
		.read_only_spare = 25,
		.wa = 2,
		.nat_rd_ret_thr = 0xffffffff,//adams temp for CDM
		.slc_rd_ret_thr = 0xffffffff,
		.max_wio_size = 0,
		.gc_retention_chk = 8,
		.alloc_retention_chk = 100,
		.avail_spare_thr = 10
};

// TBD_EH, should be move to header file?
#ifdef ERRHANDLE_GLIST
share_data_zi volatile bool	fgFail_Blk_Full;		// need define in sram
share_data_zi sGLTable *pGList;             // need define in Dram  // GL_mod, Paul_20201130
share_data_zi volatile u16*	wOpenBlk; // need define in Dram
share_data_zi volatile u8	bErrOpCnt;
share_data_zi volatile u8	bErrOpIdx;
share_data_zi volatile bool	FTL_INIT_DONE;

share_data_zi sGLEntry errInfo2;
share_data_zi sGLEntry errInfo4;
share_data_zi volatile sEH_Manage_Info sErrHandle_Info;
share_data_zi volatile MK_FailBlk_Info Fail_Blk_Info_Temp;
share_data_zi volatile u8 bFail_Blk_Cnt; 							  //need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];

share_data volatile bool hostReset;
extern volatile bool shr_shutdownflag;

#endif


#ifdef NCL_RETRY_PASS_REWRITE
extern retry_rewrite_t rd_rew_info[DDR_RD_REWRITE_TTL_CNT];
#endif
#ifdef Dynamic_OP_En
share_data epm_info_t*  shr_epm_info;
share_data_zi volatile u32 DYOPCapacity;
#endif
#ifdef History_read
extern u32 ddr_his_tab_start;
extern hist_tab_offset_t*hist_tab_ptr_st;
#endif
init_code void *ftl_get_io_buf(u32 size)
{
	u32 cnt = occupied_by(size, DTAG_SZE);
	u32 ddtag = ddr_buf;

	sys_assert(cnt <= ddr_buf_sz);
	ddr_buf += cnt;
	ddr_buf_sz -= cnt;

	return (void *) ddtag2mem(ddtag);
}

/*!
 * @brief initialize ftl flash I/O buffer
 * calculate fw needed flash size information
 *
 * @return	not used
 */
init_code void ftl_io_buf_init(void)
{
	u32 f = max(1, get_interleave() / 128);

	ddr_buf_sz = 512 * f;

	ddr_buf = ddr_dtag_register(ddr_buf_sz);
	sys_assert(ddr_buf != ~0);
}
extern u32 WUNC_DTAG;
#define L2P_UPDT_TRIM_ID_FLAG (BIT(19))
#define L2P_UPDT_TRIM_ID_MASK (L2P_UPDT_TRIM_ID_FLAG - 1)
#if(BG_TRIM == ENABLE)
typedef  struct{
    volatile u16 rptr;
    volatile u16 wptr;
    dtag_t idx[DDR_TRIM_RANGE_DTAG_CNT+1];
}trim_dtag_free_t;
extern trim_dtag_free_t bg_trim_free_dtag;
extern u8 host_sec_bitz;
extern u16 host_sec_size;

#endif
fast_code void ftl_dtag_free(ncl_w_req_t *ncl_req)
{
	u32 i;
	bool ret = false;
	u32 ttl_du_cnt = ncl_req->req.cnt << DU_CNT_SHIFT;

	for (i = 0; i < ttl_du_cnt; i++) {
        u32 avail;
        do {
            cbf_avail_ins_sz(avail, &dtag_free_que);
        } while (avail == 0);
		if ((ncl_req->w.lda[i] < TRIM_LDA) && (ncl_req->w.lda[i] != BLIST_LDA)) {
			if (ncl_req->w.pl[i].pl.type_ctrl == BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM) {
                #ifndef HMETA_SIZE
                if (ncl_req->w.pl[i].pl.btag == WVTAG_ID){
                    CBF_INS(&dtag_free_que, ret, ncl_req->w.lda[i] | WUNC_FLAG);
                }
                else
                #endif
                {
                    CBF_INS(&dtag_free_que, ret, ncl_req->w.pl[i].pl.btag | DTAG_IN_DDR_MASK);
                }
			}
			else {
                #ifdef HMETA_SIZE
                if (ncl_req->w.pl[i].pl.dtag == (WUNC_DTAG | DTAG_IN_DDR_MASK)) {
                    CBF_INS(&dtag_free_que, ret, ncl_req->w.lda[i] | WUNC_FLAG);
                }
                #else
                if(ncl_req->w.pl[i].pl.dtag == WVTAG_ID){
                    CBF_INS(&dtag_free_que, ret, ncl_req->w.lda[i] | WUNC_FLAG);
                }
                #endif
                else {
                    CBF_INS(&dtag_free_que, ret, ncl_req->w.pl[i].pl.dtag);
                }
			}
			sys_assert(ret == true);
		}
        #if(BG_TRIM == ENABLE)
        else if(ncl_req->w.lda[i] == TRIM_LDA){
            Host_Trim_Data *addr = (Host_Trim_Data *)ddtag2mem(ncl_req->w.pl[i].pl.dtag & DDTAG_MASK);
            if(addr->Validcnt && addr->Validtag == 0x12345678){
                CBF_INS(&dtag_free_que, ret, ncl_req->w.pl[i].pl.dtag | DTAG_FREE_RANGE_TRIM_DONE_BIT);
			    sys_assert(ret == true);
            }else{
                bg_trim_free_dtag.idx[bg_trim_free_dtag.wptr].dtag = ncl_req->w.pl[i].pl.dtag;
                sys_assert((bg_trim_free_dtag.idx[bg_trim_free_dtag.wptr].b.dtag - DDR_TRIM_RANGE_START) <= DDR_TRIM_RANGE_DTAG_CNT);
                dmb();
                bg_trim_free_dtag.wptr = (bg_trim_free_dtag.wptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);
            }
        }
        #endif
	}
}
/*
fast_code void Update_epm_ftl_invalid_data_idx(u32 blk_sn, u32 pda)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	if(epm_ftl_inv_idx == 2048)
	{

		//ftl_apl_trace(LOG_INFO, 0, "epm ftl record invalid data full back to 0 start");
		epm_ftl_inv_idx = 0;
		epm_ftl_data->epm_record_full = 1;
	}
	epm_ftl_data->blk_sn[epm_ftl_inv_idx] = blk_sn;
	epm_ftl_data->pda_list[epm_ftl_inv_idx] = pda;
	epm_ftl_data->epm_record_idx = epm_ftl_inv_idx;
	//ftl_apl_trace(LOG_INFO, 0, "epm ftl record idx %d blk_sn 0x%x pda 0x%x",epm_ftl_data->epm_record_idx,epm_ftl_data->blk_sn[epm_ftl_inv_idx],epm_ftl_data->pda_list[epm_ftl_inv_idx]);
	epm_ftl_inv_idx++;

}
*/
fast_code void host_l2p_pda_updt_done(u32 r0, u32 entry, u32 cnt)
{
	u32 i;
	ncl_w_req_t *ncl_req;
	updt_cpl_t *cpl = (updt_cpl_t *)entry;

	for (i = 0; i < cnt; i++, cpl++) {
		if (cpl->updt.req_type == 0) {
			u32 rid = cpl->updt.fw_seq_id;
			ncl_req = ncl_rid_to_req(rid);
			if (ncl_req->req.op_type.b.blklist == 1){
				//ftl_apl_trace(LOG_ERR, 0, "updt_cnt:%d, i:%d/%d req:%d, pad:%d, bdtag:%d",ncl_req->w.updt_cnt,i,cnt, ncl_req->req.cnt, ncl_req->req.padding_cnt,ncl_req->req.blklist_dtag_cnt);
			}
			sys_assert(ncl_req->w.updt_cnt);
			/*
			if(ncl_req->req.type == FTL_CORE_GC)
			{
				if(cpl->updt.status == 0)
				{
					u32 temp_pda = cpl->updt.new_val;
					u32 temp_blk_sn = spb_get_sn(temp_pda >> 21);
					//ftl_apl_trace(LOG_INFO, 0, "gc hit blk_sn 0x%x pda 0x%x",temp_blk_sn,temp_pda);
					Update_epm_ftl_invalid_data_idx(temp_blk_sn, temp_pda);
				}
			}
			*/
			if (--ncl_req->w.updt_cnt == 0) {
				if (ncl_req->req.type == FTL_CORE_GC)
					sys_free_aligned(FAST_DATA, ncl_req->w.old_pda);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
				if (ncl_req->req.type == FTL_CORE_NRM || ncl_req->req.type == FTL_CORE_SLC)
#else
				if (ncl_req->req.type == FTL_CORE_NRM)
#endif
				{
					ftl_dtag_free(ncl_req);
				}
				bool ret;
				CBF_INS(&l2p_updt_done_que, ret, (u32)ncl_req);
				sys_assert(ret == true);
			}

			if (l2p_updt_block) {
				l2p_updt_block = false;
				host_pda_updt();
			}
		} else {
			sys_assert(cpl->end_of_trim.old == ~0);
			sys_assert(cpl->end_of_trim.lda == ~0);
            #if(BG_TRIM == ENABLE)
			if((cpl->end_of_trim.fw_seq_id & L2P_UPDT_TRIM_ID_FLAG)){
					/* trim range done */
                u32 rid = cpl->updt.fw_seq_id & L2P_UPDT_TRIM_ID_MASK;
                ncl_req = ncl_rid_to_req(rid);
                sys_assert(ncl_req->w.updt_cnt);
					/* trim info done */
                if (--ncl_req->w.updt_cnt == 0){
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
                    sys_assert(ncl_req->req.type == FTL_CORE_NRM || ncl_req->req.type == FTL_CORE_SLC);
#else
                    sys_assert(ncl_req->req.type == FTL_CORE_NRM);
#endif
					ftl_dtag_free(ncl_req);
					bool ret;
    				CBF_INS(&l2p_updt_done_que, ret, (u32)ncl_req);
					sys_assert(ret == true);
				}
			}
            #endif

		}
	}
}
#if RECYCLE_QUE_CPU_ID == CPU_ID
slow_code void host_l2p_dtag_recycle(u32 r0, u32 r1, u32 r2)
{
    ftl_apl_trace(LOG_INFO, 0x01a5, " ");
}
#endif
#if 0//(TRIM_SUPPORT == DISABLE)
fast_code void ftl_core_trim(ncl_w_req_t *ncl_req)
{
	/* issue non-full-range trim */
	ftl_trim_t *ftl_trim = (ftl_trim_t *) dtag2mem(ncl_req->w.trim.dtag);
	u32 num = ftl_trim->num;
	u32 issue = 0;

	u32 i;
	for (i = 0; i < num; i ++)
	{
		u32 slda = ftl_trim->range[i].slda;
		u32 len = ftl_trim->range[i].len;
		sys_assert(len);

		int srange = (slda >> RDISK_RANGE_SHIFT);
		int erange = ((slda + len - 1) >> RDISK_RANGE_SHIFT);
		if (erange - srange > 1) {
			u32 slda_erange;
			u32 _len;

			_len = ((srange + 1) << RDISK_RANGE_SHIFT) - slda;
			l2p_updt_trim(slda, 0, true, _len, FTL_L2P_UPDT_QID, ncl_req->w.trim.dtag.dtag);

			slda_erange = (erange << RDISK_RANGE_SHIFT);
			_len = (slda + len) - slda_erange;
			l2p_updt_trim(slda_erange, 0, true, _len, FTL_L2P_UPDT_QID, ncl_req->w.trim.dtag.dtag);
			issue += 2;
		} else {
			if (len > RDISK_RANGE_SIZE) {
				l2p_updt_trim(slda, 0, true, RDISK_RANGE_SIZE, FTL_L2P_UPDT_QID, ncl_req->w.trim.dtag.dtag);
				len -= RDISK_RANGE_SIZE;
				slda += RDISK_RANGE_SIZE;
				issue++;
			}
			l2p_updt_trim(slda, 0, true, len, FTL_L2P_UPDT_QID, ncl_req->w.trim.dtag.dtag);
			issue++;
		}
	}

	if (issue) {
		ftl_trim->num = issue;
	} else {
		bool ret;
		CBF_INS(&dtag_free_que, ret, (u32)ftl_trim->caller | DTAG_FREE_TRIM_INFO_DONE_BIT);
		sys_assert(ret == true);
	}
}
#endif
fast_code bool _host_pda_updt(ncl_w_req_t *ncl_req)
{
	u32 i, j;
	u32 idx;
	u32 seq_cnt, lda_cnt;
	lda_t cur_lda = 0, last_lda;
	pda_t *old_pda = NULL;
	pda_t *new_pda = NULL;
	u32 *llist = ncl_req->w.lda;
#ifdef SKIP_MODE
		u32 *plist = ncl_req->w.pda;
#else
		u32 *plist = ncl_req->w.l2p_pda;
#endif

	bm_pl_t *bm_pl = ncl_req->w.pl;
	//u32 plane_cnt = shr_nand_info.geo.nr_planes * ncl_req->req.die_cnt;
	//u32 mp_du_cnt = plane_cnt << DU_CNT_SHIFT;
	u32 plane_cnt ;
	u32 mp_du_cnt ;
    u8 lda_ofset = 0;
#ifdef NCL_RETRY_PASS_REWRITE
    u32 dtag_id;
#endif

#ifdef SKIP_MODE

	if(ncl_req->req.op_type.b.pln_write)
	{	
		plane_cnt = ncl_req->req.op_type.b.pln_write ;
		mp_du_cnt = plane_cnt << DU_CNT_SHIFT; 
	}
	else
#endif
	{	
		plane_cnt = shr_nand_info.geo.nr_planes;
		mp_du_cnt = plane_cnt << DU_CNT_SHIFT; //16

	}

    ncl_req->w.updt_cnt = (ncl_req->req.cnt << DU_CNT_SHIFT) - ncl_req->req.padding_cnt; 
	//if (ncl_req->req.op_type.b.blklist) { 
        ncl_req->w.updt_cnt -= ncl_req->req.blklist_dtag_cnt; 
        //sys_assert(ncl_req->req.blklist_dtag_cnt != 0); 
    //} 
    if (ncl_req->req.op_type.b.p2l_nrm) { 
        ncl_req->w.updt_cnt -= ncl_req->req.p2l_dtag_cnt; 
        //ftl_apl_trace(LOG_DEBUG, 0, "p2l req update cnt:%d", ncl_req->w.updt_cnt); 
        sys_assert(ncl_req->req.p2l_dtag_cnt != 0); 
    } 

    if (ncl_req->req.op_type.b.parity_mix)
    { 
#if (PLP_SLC_BUFFER_ENABLE	== mENABLE)
		if(ncl_req->req.op_type.b.slc == 0)
			ncl_req->w.updt_cnt -= shr_nand_info.bit_per_cell * DU_CNT_PER_PAGE; 
		else
			ncl_req->w.updt_cnt -= DU_CNT_PER_PAGE; 
#else
		ncl_req->w.updt_cnt -= shr_nand_info.bit_per_cell * DU_CNT_PER_PAGE; 
#endif


    } 
	//allocate new pda list
	ncl_req->w.updt_pda = (pda_t *)&ftl_updt_pda[ncl_req->req.id];
	if (ncl_req->w.updt_pda == NULL)
		return false;

	new_pda = ncl_req->w.updt_pda;

	//allocate old pda list for gc
	if (ncl_req->req.type == FTL_CORE_GC) {
        //allocate new pda list
	    u32 list_size = sizeof(pda_t) * EACH_DIE_DTAG_CNT;
		ncl_req->w.old_pda = sys_malloc_aligned(FAST_DATA, list_size, 8);
		if (ncl_req->w.old_pda == NULL) {
			return false;
		}

		old_pda = ncl_req->w.old_pda;
	}

	for (i = 0; i < ncl_req->req.tpage; i++) {
        lda_ofset = 0;
		lda_cnt = 0;
		seq_cnt = 0;
		idx = i * mp_du_cnt;

		for (j = 0; j < mp_du_cnt; j++, last_lda = cur_lda) {
			cur_lda = llist[idx + j];
			if (cur_lda >= TRIM_LDA) {
                #if (BG_TRIM == ENABLE)
				if (cur_lda == TRIM_LDA) {
                    extern u8* TrimBlkBitamp;
				    set_bit(pda2blk(ncl_req->w.pda[0]), TrimBlkBitamp);
                    ncl_req->w.updt_cnt--;
                    if (lda_cnt) {
						#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
            			sys_assert(ncl_req->req.type == FTL_CORE_NRM || ncl_req->req.type == FTL_CORE_SLC);
						#else
            			sys_assert(ncl_req->req.type == FTL_CORE_NRM);
						#endif
            			lda_t *lda_list = NULL;
            			if (lda_cnt != seq_cnt)
            				lda_list = &llist[idx+lda_ofset];
            			l2p_updt_pda_bulk(&new_pda[idx+lda_ofset], llist[idx+lda_ofset], lda_list, lda_cnt, NULL, true, FTL_L2P_UPDT_QID, ncl_req->req.id);
                        lda_cnt = 0;
                        seq_cnt = 0;
            		}
                    Host_Trim_Data *addr = (Host_Trim_Data *)ddtag2mem((ncl_req->w.pl[idx + j].pl.dtag & DDTAG_MASK));
                    sys_assert(addr->Validtag == 0x12345678);
                    sys_assert(addr->Validcnt<=510);
                    u32 k = 0;
                    lda_t slda = 0;
                    lda_t elda = 0;
                    u32 length;
                    u32 maxlda = ((ns[0].cap - 1) >> (LDA_SIZE_SHIFT - host_sec_bitz));
                    #if 0 // added for dbg, Sunny 20210903
                    ftl_apl_trace(LOG_ALW, 0x5f80, "[SPOR Trim] Valid Cnt : 0x%x", addr->Validcnt);
                    #endif
                    for(k=0;k<addr->Validcnt;k++){
                        slda = addr->Ranges[k].sLDA;
                        length = addr->Ranges[k].Length;
						elda = slda + length ;
                        #if 0 // added for dbg, Sunny 20210903
                        ftl_apl_trace(LOG_ALW, 0x125b, "[SPOR Trim] LDA Start : 0x%x, length : %d", slda, length);
                        #endif
                        sys_assert(elda - 1 <= maxlda);
                        if(addr->all){
                            ncl_req->w.updt_cnt++;
                        	l2p_updt_trim(slda, 0, true, length, FTL_L2P_UPDT_QID, L2P_UPDT_TRIM_ID_FLAG|ncl_req->req.id);
                        }else{
    						if(slda & (BIT(10)-1)){
                                ncl_req->w.updt_cnt++;
                            	l2p_updt_trim(slda, 0, true, min((BIT(10)-(slda & (BIT(10)-1))), length), FTL_L2P_UPDT_QID, L2P_UPDT_TRIM_ID_FLAG|ncl_req->req.id);
    						}
    						if(elda & (BIT(10)-1) && (!(slda & (BIT(10)-1)) || (slda>>10 != elda>>10))){
                            	ncl_req->w.updt_cnt++;
                            	l2p_updt_trim((elda - (elda & (BIT(10)-1))), 0, true, (elda & (BIT(10)-1)), FTL_L2P_UPDT_QID, L2P_UPDT_TRIM_ID_FLAG|ncl_req->req.id);
    						}
                        }
                    }

				}
                #endif
				continue;
			}
			if (cur_lda == BLIST_LDA){
				ftl_apl_trace(LOG_ERR, 0x4c4a, "Catch blist du:%d", ncl_req->w.pl[idx + j].pl.du_ofst);
				continue;
			}
            #if 0//(TRIM_SUPPORT == DISABLE)
			sys_assert(!trim_info);
            #endif
			//new_pda[idx + j] = plist[i * plane_cnt] + j;
			new_pda[idx + j] = plist[i * plane_cnt + (j / DU_CNT_PER_PAGE)] + (j % DU_CNT_PER_PAGE); //allocate new pda list for l2p update

			if (ncl_req->req.type == FTL_CORE_GC) {
				u32 rsp_idx = bm_pl[idx + j].pl.du_ofst;
				u32 grp = bm_pl[idx + j].pl.nvm_cmd_id;

                #ifdef NCL_RETRY_PASS_REWRITE
                dtag_id = bm_pl[idx + j].pl.dtag & DDTAG_MASK;
                if((dtag_id >= DDR_RD_REWRITE_START) && (dtag_id < (DDR_RD_REWRITE_START + DDR_RD_REWRITE_TTL_CNT)))
                {
                    old_pda[idx + j] = rd_rew_info[dtag_id - DDR_RD_REWRITE_START].pda;
                }
                else
                #endif
                {
                    rsp_idx = DTAG_GRP_IDX_TO_IDX(rsp_idx, grp);
                    old_pda[idx + j] = gc_pda_list[rsp_idx];
                }
			}
            if(lda_cnt == 0)
                lda_ofset = j;
			if ((lda_cnt == 0) || (last_lda + 1 == cur_lda))
				seq_cnt++;

			lda_cnt++;
		}

		if (lda_cnt) {
			pda_t *old_list = NULL;
			if (ncl_req->req.type == FTL_CORE_GC){
				//ftl_apl_trace(LOG_ERR, 0, "lda_cnt %d idx %d k %d offset %d", lda_cnt, idx, j, lda_ofset);
				old_list = &old_pda[idx+lda_ofset];
                //sys_assert(lda_ofset == 0);
			}

			lda_t *lda_list = NULL;
			if (lda_cnt != seq_cnt)
                lda_list = &llist[idx+lda_ofset];

			u32 qid = FTL_L2P_UPDT_QID;
			u32 fwid = ncl_req->req.id;
			l2p_updt_pda_bulk(&new_pda[idx+lda_ofset], llist[idx+lda_ofset], lda_list, lda_cnt, old_list, true, qid, fwid);
		}


	}
	if (ncl_req->w.updt_cnt == 0){
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		sys_assert(ncl_req->req.type == FTL_CORE_NRM || ncl_req->req.type == FTL_CORE_SLC);
#else
		sys_assert(ncl_req->req.type == FTL_CORE_NRM);
#endif
		ftl_dtag_free(ncl_req);
		bool ret;
		CBF_INS(&l2p_updt_done_que, ret, (u32)ncl_req);
		sys_assert(ret == true);
	}

	return true;
}
#if 0
ddr_code void host_pda_updt_handle_pend_que(void)
{
	// u32 rid;
	// ncl_w_req_t *req;
	//
	// while (CBF_EMPTY(&l2p_updt_pend_que) == false) {
	// 	CBF_HEAD(&l2p_updt_pend_que, rid);
	// 	if (rid & L2P_UPDT_TIMR_RANGE) {
	// 		u32 range = (rid & ~L2P_UPDT_TIMR_RANGE);
	// 		u32 slda = (range << RDISK_RANGE_SHIFT);
	// 		l2p_updt_trim(slda, 0, true, RDISK_RANGE_SIZE, FTL_L2P_UPDT_QID, range | TRIM_FW_ID_RANGE_DONE);
	// 	} else {
	// 		req = ncl_rid_to_req(rid);
	// 		bool ret = _host_pda_updt(req);
	// 		sys_assert(ret);
	// 	}
	// 	CBF_REMOVE_HEAD(&l2p_updt_pend_que);
	// }
}
#endif
ddr_code void ftl_reset_gc_act_cmpl(gc_action_t *gc_act)
{
	if (gc_act->flags.b.set_stop)
		shr_ftl_flags.b.gc_stoped = true;
	else
		shr_ftl_flags.b.gc_stoped = false;

	sys_free(FAST_DATA, gc_act);
}

ddr_code void ftl_reset_notify(void)
{
	gc_action_t *gc_act = sys_malloc(FAST_DATA, sizeof(gc_action_t));
	sys_assert(gc_act);

	shr_ftl_flags.b.gc_stoped = false;
	gc_act->caller = NULL;
	gc_act->flags.all = 0;
	gc_act->flags.b.set_stop = true;
	gc_act->act = GC_ACT_SUSPEND;
	gc_act->cmpl = ftl_reset_gc_act_cmpl;
	if (gc_action(gc_act))
	{
		gc_act->cmpl(gc_act);
	}
// 	ftl_apl_trace(LOG_ERR, 0, "[DEBG] GC STOP");
}

ddr_code void ftl_reset_done_notify(void)
{
	gc_action_t *gc_act = sys_malloc(FAST_DATA, sizeof(gc_action_t));
	sys_assert(gc_act);

	gc_act->caller = NULL;
	gc_act->flags.all = 0;
	gc_act->flags.b.clr_stop = true;
	gc_act->act = GC_ACT_RESUME;
	gc_act->cmpl = ftl_reset_gc_act_cmpl;
	if (gc_action(gc_act))
		gc_act->cmpl(gc_act);
	// ftl_apl_trace(LOG_ERR, 0, "[DEBG] GC DONE");
}

fast_code void ipc_ftl_reset_notify(volatile cpu_msg_req_t *req)
{
	ftl_reset_notify();
}

fast_code void ipc_ftl_reset_done_notify(volatile cpu_msg_req_t *req)
{
	ftl_reset_done_notify();
}
#ifdef LJ1_WUNC
#define WUNC_MAX_CACHE  (192)
typedef struct wunc_t{
    u16 ceidxs[WUNC_MAX_CACHE];
    lda_t startlda0;
    lda_t startlda1;
    lda_t endlda0;
    lda_t endlda1;
    u32 cross_cnt;
}wunc_t;
#define ABT_BIT (0x8000)
share_data_zi wunc_t WUNC_lda2ce;
fast_code void add_WUNC_abrt_bit(u32 did)
{
    lda_t lda = (did & 0x7FFFFFFF);
    if((lda >= WUNC_lda2ce.startlda0) && (lda <= WUNC_lda2ce.endlda0)){
        u32 idx = lda - WUNC_lda2ce.startlda0;
        WUNC_lda2ce.ceidxs[idx] = WUNC_lda2ce.ceidxs[idx]|ABT_BIT;
        return;
    }
    if(WUNC_lda2ce.cross_cnt){
        if((lda >= WUNC_lda2ce.startlda1) && (lda <= WUNC_lda2ce.endlda1)){
            u32 idx = lda - WUNC_lda2ce.startlda1 + WUNC_lda2ce.cross_cnt;
            WUNC_lda2ce.ceidxs[idx] = WUNC_lda2ce.ceidxs[idx]|ABT_BIT;
        }
    }
}
#endif

fast_code void host_pda_updt(void)
{
	u32 rid;
	ncl_w_req_t *req;

	CBF_MAKE_EMPTY(&l2p_updt_ntf_que);

	while (CBF_EMPTY(&l2p_updt_que) == false) {
		CBF_HEAD(&l2p_updt_que, rid);
        #if(TRIM_SUPPORT == ENABLE)
		if (rid & BIT(31)) {
            bg_trim_free_dtag.idx[bg_trim_free_dtag.wptr].dtag = rid & (~(BIT31));
            sys_assert((bg_trim_free_dtag.idx[bg_trim_free_dtag.wptr].b.dtag - DDR_TRIM_RANGE_START) <= DDR_TRIM_RANGE_DTAG_CNT);
            dmb();
            bg_trim_free_dtag.wptr = (bg_trim_free_dtag.wptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);

        } else 
        #endif
            {
			req = ncl_rid_to_req(rid);

			if (req->req.op_type.b.ncl_done == 0)
				break;

			if (req->req.op_type.b.skip_updt) {
				u32 *llist = req->w.lda;
				u32 i;
				u32 ttl_du_cnt = req->req.cnt << DU_CNT_SHIFT;
				ftl_apl_trace(LOG_ERR, 0xa723, "irq %d updt bypass:Lda(0/1/2)[0x%x/0x%x/0x%x]", req->req.id, llist[0], llist[1], llist[2]);	// ISU, LJ1-337, PgFalClsNotDone (1)

				for (i = 0; i < ttl_du_cnt; i++) {
					//ftl_apl_trace(LOG_DEBUG, 0, "l %x", llist[i]);	// Paul_20210907 // ISU, BakupPBTLat
					#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
					if ((llist[i] < TRIM_LDA) && (req->req.type == FTL_CORE_NRM || req->req.type == FTL_CORE_SLC))
					#else
					if ((llist[i] < TRIM_LDA) && (req->req.type == FTL_CORE_NRM))
					#endif
					{
						// ISU, LJ1-195, PgFalReWrMisComp
                        u32 ret = 0;
                        u32 avail = 0;
						do {
            				cbf_avail_ins_sz(avail, &dtag_free_que);
            			} while (avail == 0);

            			if (req->w.pl[i].pl.type_ctrl == BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM) {
                            #ifndef HMETA_SIZE
                            if (req->w.pl[i].pl.btag == WVTAG_ID){
                                add_WUNC_abrt_bit(req->w.lda[i]);
                                CBF_INS(&dtag_free_que, ret, req->w.lda[i] | WUNC_FLAG);
                            }
                            else 
                            #endif
                            {
                                CBF_INS(&dtag_free_que, ret, req->w.pl[i].pl.btag | DTAG_IN_DDR_MASK | DTAG_FREE_UPDT_ABORT_BIT);
                            }
            			}
            			else {
                            #ifdef HMETA_SIZE
                            if (req->w.pl[i].pl.dtag == (WUNC_DTAG | DTAG_IN_DDR_MASK)) {
                                add_WUNC_abrt_bit(req->w.lda[i]);
                                CBF_INS(&dtag_free_que, ret, req->w.lda[i] | WUNC_FLAG);
                            }
                            #else
                            if(req->w.pl[i].pl.dtag == WVTAG_ID){
                                add_WUNC_abrt_bit(req->w.lda[i]);
                                CBF_INS(&dtag_free_que, ret, req->w.lda[i] | WUNC_FLAG);
                            }
                            #endif
                            else {
                                CBF_INS(&dtag_free_que, ret, req->w.pl[i].pl.dtag | DTAG_FREE_UPDT_ABORT_BIT);
                            }
            			}
            			sys_assert(ret == true);
					}
                    #if(BG_TRIM == ENABLE)
                    else if(llist[i] == TRIM_LDA)
                    {

                        bg_trim_free_dtag.idx[bg_trim_free_dtag.wptr].dtag = req->w.pl[i].pl.dtag;
                        sys_assert((bg_trim_free_dtag.idx[bg_trim_free_dtag.wptr].b.dtag - DDR_TRIM_RANGE_START) <= DDR_TRIM_RANGE_DTAG_CNT);
                        dmb();
                        bg_trim_free_dtag.wptr = (bg_trim_free_dtag.wptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);
                    }
                    #endif
				}
				//ftl_apl_trace(LOG_ERR, 0, "\n");	// ISU, LJ1-337, PgFalClsNotDone (1)
				bool ret;
				CBF_INS(&l2p_updt_done_que, ret, (u32)req);
				sys_assert(ret == true);
			} else {
				bool ret = _host_pda_updt(req);
				if (ret == false) {
					l2p_updt_block = true;
				}
			}
		}

		CBF_REMOVE_HEAD(&l2p_updt_que);
	}
}

fast_code void get_avg_erase_cnt(u32 *avg_erase, u32 *max_erase, u32 *min_erase, u32 *total_ec)
{
	u32 i;
	u32 sum = 0;
	u32 spb_cnt = 0;

    *max_erase = 0;
    *min_erase = INV_U32;

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		u8 id = spb_get_poolid(i);

		if (!(id == SPB_POOL_UNALLOC)) {
			sum += spb_info_get(i)->erase_cnt;
			spb_cnt++;
            if(*max_erase < spb_info_get(i)->erase_cnt)
            {
                *max_erase = spb_info_get(i)->erase_cnt;
//				shr_max_erase = spb_info_get(i)->erase_cnt;
                max_ec_blk = i;
            }
            if(*min_erase > spb_info_get(i)->erase_cnt)
            {
                *min_erase = spb_info_get(i)->erase_cnt;
//				shr_min_erase = spb_info_get(i)->erase_cnt;
            }
		}
	}
    *total_ec = sum;
//	shr_total_ec = sum;
	*avg_erase = sum / spb_cnt;
//	shr_avg_erase = sum / spb_cnt;

    ftl_apl_trace(LOG_INFO, 0xe82b, "A: %d, Max: %d, Min: %d, t: %d", *avg_erase, *max_erase, *min_erase, *total_ec);
}
share_data_zi volatile u16 ps_open[3];

ddr_code void get_avg_erase_cnt_1(u32 flags, u32 vu_sm_pl)
{
//	u64 init_time_start = get_tsc_64();
    extern tencnet_smart_statistics_t *tx_smart_stat;
	spb_ec_tbl = (Ec_Table* )shr_ec_tbl_addr;

    get_avg_erase_cnt(&spb_ec_tbl->header.AvgEC, &spb_ec_tbl->header.MaxEC, &spb_ec_tbl->header.MinEC, &spb_ec_tbl->header.TotalEC);

	ftl_apl_trace(LOG_INFO, 0x1663, "A: %d, Max: %d, Min: %d, t: %d", spb_ec_tbl->header.AvgEC, spb_ec_tbl->header.MaxEC, spb_ec_tbl->header.MinEC, spb_ec_tbl->header.TotalEC);
//	ftl_apl_trace(LOG_ALW, 0, "EC done, time cost : %d us", time_els);

	if(flags == 2)
	{
        #if (MIN_EC_STRATEGY == mENABLE)
        if(spb_ec_tbl->header.MinEC < tx_smart_stat->wear_levelng_count[0])
        {
            spb_ec_tbl->header.MinEC = tx_smart_stat->wear_levelng_count[0];
        }
        #endif
		cpu_msg_issue(CPU_FE - 1, CPU_MSG_GET_NEW_EC_SMART_DONE, 0, vu_sm_pl);
	}
	else
	{
		cpu_msg_issue(CPU_FE - 1, CPU_MSG_GET_NEW_EC_DONE, 0, vu_sm_pl);
	}
}

fast_code void Scan_All_BLK_4_Nand_Written()
{
	u32 i;
    u32 scan_written = 0;
	u64 sum = 0;
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		u8 id = spb_get_poolid(i);

		if (!(id == SPB_POOL_UNALLOC || id == SPB_POOL_QBT_ALLOC || id == SPB_POOL_QBT_FREE))
        {
			sum += (PHY_BLK_SIZE*256* spb_info_get(i)->erase_cnt);
		}
	}

    scan_written = ((sum >> 5) && 0xFFFFFFFF);
    //cpu_msg_issue(CPU_BE - 1, CPU_MSG_INIT_WRITTEN, 0, scan_written);
    //extern tencnet_smart_statistics_t *tx_smart_stat;
	//tx_smart_stat->nand_bytes_written = scan_written;

    ftl_apl_trace(LOG_INFO, 0x8990, "scan_written: %d", scan_written);

}
fast_code void ftl_set_spb_query(u32 nsid, u32 type)
{
	ftl_ns_t *ns = ftl_ns_get(nsid);
	bool empty = CBF_EMPTY(ns->spb_queue[type]);

    if (plp_trigger)
    {
        if (!(nsid == FTL_NS_ID_START && type == FTL_CORE_NRM))
        {
            return;
        }
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
        else if(SLC_init == true)
        {
            return;
        }
#endif
    }
	if (empty && (ns->flags.b.spb_queried & (1 << type)) == 0) {
		if(nsid == 2 && type == 2){
			if(pbt_query_ready == false){
				if(pbt_query_need_resume == false){
					pbt_query_need_resume = true;	//fix TableSN update unpredictable issue
				}
				ftl_apl_trace(LOG_INFO, 0xcf6c, "pbt_query_suspend %d %x/%x ns:%d/%d", empty, ns->flags.b.spb_queried,(ns->flags.b.spb_queried & (1 << type)),nsid,type);
				return;
			}
			pbt_query_ready = false;
		}
		ftl_apl_trace(LOG_INFO, 0xc96e, "query match %d %x/%x ns:%d/%d", empty, ns->flags.b.spb_queried,(ns->flags.b.spb_queried & (1 << type)),nsid,type);
		ns->flags.b.spb_queried |= 1 << type;
		ftl_ns_spb_trigger(nsid, type);
	} else {
		ftl_apl_trace(LOG_ERR, 0xbe60, "query miss %d %x/%x ns:%d/%d", empty, ns->flags.b.spb_queried,(ns->flags.b.spb_queried & (1 << type)),nsid,type);
	}
}

fast_data_zi ftl_flush_misc_t blklist_tbl[2];

slow_code void ftl_free_blist_dtag(u16 type){
    sys_assert(type < 2);
	if(blklist_tbl[type].type != INV_U16){	
		dtag_t dtag = { .dtag = blklist_tbl[type].dtag_start[1] };
		#if(PLP_SUPPORT == 1)//plp	
		ftl_l2p_put_vcnt_buf(dtag, blklist_tbl[type].dtag_cnt[1], false);
		#else
		dtag_cont_put(DTAG_T_SRAM,  dtag, 1);
		#endif
	}
	blklist_tbl[type].type = INV_U16;
}

slow_code void ftl_free_flush_blklist_dtag(ftl_core_ctx_t *ctx){
	ftl_flush_misc_t *ctx_tmp = (ftl_flush_misc_t *) ctx;
	ftl_free_blist_dtag(ctx_tmp->type);
}
extern u8* TrimBlkBitamp;
fast_code bool ftl_blklist_copy(u32 nsid, u32 type){
	if(blklist_flush_query[type]){
	ftl_flush_misc_t *flush_blklist = &blklist_tbl[type];
	u32 dtag_cnt;
#if(PLP_SUPPORT == 1)//plp	
	dtag_t dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, NULL);
#else//nonplp
	dtag_cnt = 1;
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
	memcpy(dtag2mem(dtag),TrimBlkBitamp,256);
#endif
	//lda_t* smem = sdtag2mem(p2l->sdtag.b.dtag);
	spb_mgr_flush_desc();
    #if 0 //(DEBUG_SPOR == mENABLE) // for dbg, by Sunny Lin
    spb_pool_table();
    ftl_vc_tbl_print();
    #endif
	FTL_CopyFtlBlkDataToBuffer(type);
	flush_blklist->dtag_start[0] = shr_blklistbuffer_start[type];
	flush_blklist->dtag_cnt[0] 	 = shr_blklistbuffer_need;
	flush_blklist->dtag_start[1] = dtag.dtag;
	flush_blklist->dtag_cnt[1] 	 = dtag_cnt;
	flush_blklist->seg_cnt 		 = ftl_l2p_misc_cnt();
	flush_blklist->idx 			 = 0;
	flush_blklist->io_cnt 		 = 0;
	flush_blklist->ctx.cmpl 	 = ftl_free_flush_blklist_dtag;
	flush_blklist->nsid			 = nsid;
	flush_blklist->type			 = type;
	flush_blklist->blklist_dtag_cnt = 0;
	flush_blklist->ctx.caller 	 = flush_blklist;

		//ftl_apl_trace(LOG_INFO, 0, "segttl:%d, vac start 0x%x, cnt:%d list start 0x%x, cnt:%d",
			//flush_blklist->seg_cnt, dtag.dtag, dtag_cnt, shr_blklistbuffer_start[type], shr_blklistbuffer_need);
	//ftl_apl_trace(LOG_ERR, 0, "send:0x%x", flush_blklist);
	ftl_core_flush_blklist(flush_blklist);
	//ftl_apl_trace(LOG_INFO, 0, "misc crc %x", ftl_l2p_misc_crc(&dtag));
		blklist_flush_query[type] = false;
		return true;
	}
	return false;
}

slow_code bool ftl_set_flush_blklist(u32 nsid, u32 type)
{
	blklist_flush_query[type] = true;
	return false;
}
ddr_code void ftl_spb_weak_retire(spb_weak_retire_t weak_retire)
{
	u32 nsid;

	nsid = spb_get_nsid(weak_retire.b.spb_id);
	if (nsid == 0) {
		ftl_apl_trace(LOG_ERR, 0x2fe9, "retire spb %d ns %d", weak_retire.b.spb_id, nsid);
		return;
	}

	spb_set_flag(weak_retire.b.spb_id, SPB_DESC_F_WEAK);

	//if (weak_retire.b.type != SPB_READ_WEAK) {    // Paul_20201202
	// DBG, ISU, LJ1-337, PgFalClsNotDone (1)
	#if 0
	if (weak_retire.b.type > SPB_WEAK_HNDL_THRSHLD) {   // TBC_EH, Retire whole SPB, could be checked when marking bad.
		u8 type;
		switch (weak_retire.b.type) {
		case SPB_RETIRED_BY_ERASE:
			type = GRWN_DEF_TYPE_ERASE;
			break;
		case SPB_RETIRED_BY_PROG:
			type = is_spb_slc(weak_retire.b.spb_id) ? GRWN_DEF_TYPE_PROG_SLC : GRWN_DEF_TYPE_PROG_XLC;
			break;
		case SPB_RETIRED_BY_READ:
			type = GRWN_DEF_TYPE_READ;
			break;
		case SPB_RETIRED_BY_READ_GC:
			type = GRWN_DEF_TYPE_READ_GC;
			break;
		default:
			type = GRWN_DEF_TYPE_OTHR;
			break;
		}
		ins_grwn_err_info(type, nal_make_pda(weak_retire.b.spb_id, 0));
		spb_set_flag(weak_retire.b.spb_id, SPB_DESC_F_RETIRED);
	}
	#endif

	// trigger GC
	// Let ErrHandle GC go through set_spb_weak_retire, Paul_20201202
	ftl_ns_weak_spb_gc(nsid, weak_retire.b.spb_id);
}

fast_code void ftl_gc_done(u32 spb_id, u32 free_du_cnt)
{
	/*
        * free_du_cnt == ~0 indicates gc was aborted,
        * so the SPB_DESC_F_GCED flag can't be set,
        * but how to update gc perf ? refine this later
	*/

	if (free_du_cnt != ~0)
		spb_set_flag(spb_id, SPB_DESC_F_GCED);

	gc_end(spb_id, free_du_cnt);

}

fast_code void ftl_flush(flush_ctx_t *ctx)
{
	sys_assert(ctx->nsid != 0);

	//ftl_open_qbt(ctx);
	flush_fsm_run(ctx);
}
#if 0
fast_code void trim_resume(void)
{
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_UCACHE_RESM_TRIM, 0, 0);
}
#endif
fast_code void ftl_open_cmpl(void *ctx)
{
	u32 nsid = (u32) ctx;
    #if 0
	trim_resume();
    #endif
	//gc_resume();
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_NS_OPEN_DONE, 0, nsid);
}

fast_code bool ftl_open(u32 nsid)
{
	bool dirty = false;

	sys_assert(nsid != FTL_NS_ID_INTERNAL);
	dirty = ftl_ns_make_dirty(nsid, (void*)nsid, ftl_open_cmpl, false);
	if (dirty)
		ftl_open_cmpl((void*)nsid);

	return dirty;
}
#if GC_SUSPEND_FWDL		//20210308-Eddie
extern struct ns_array_manage *ns_array_menu;
slow_code_ex void FWDL_GC_Handle(u8 type)
{
	gc_action_t *action;

	action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	evlog_printk(LOG_ALW,"Enter FWDL GC handle Type: %d, action size: 0x%x",type,sizeof(gc_action_t));

	sys_assert(action);
	action->act = type;
	action->caller = NULL;
	action->cmpl = fwdl_gc_action_done;	//No need  to free memory since gc_action_t is located in registered ddtag

	if (gc_action(action)){
		evlog_printk(LOG_ALW,"No SPB need to GC suspend/GC resume done");
		fwdl_gcsuspend_done = 1;
		sys_free(FAST_DATA, action);
	}
	else
	{
		u8 lbaf;
		lbaf = ns_array_menu->ns_attr[0].hw_attr.lbaf;
		if(lbaf == 0 || lbaf == 2)
		{
			evlog_printk(LOG_ALW,"skip Wait GC suspend handle done");
			fwdl_gcsuspend_done = 1;
		}
		else
		{
			evlog_printk(LOG_ALW,"Wait GC suspend handle done");
		}
	}

}
#endif
#if(WARMBOOT_FTL_HANDLE == mENABLE)
#if(WA_FW_UPDATE == DISABLE)
slow_code_ex void ftl_warm_boot_spb_handle_done(ftl_core_ctx_t *ctx)
{
	sys_free(FAST_DATA, ctx);
    ftl_apl_trace(LOG_WARNING, 0x90ff, "[FTL]warm boot close open blk done");

    //judge whether a new set of PBT is being written
    pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt = mFALSE;
    if ((spb_get_free_cnt(SPB_POOL_PBT) % QBT_BLK_CNT != 0) || ps_open[FTL_CORE_PBT] != INV_U16)
    {   
        pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt = mTRUE;
        cpu_msg_issue(CPU_BE - 1,  CPU_MSG_SAVE_PBT_PARAM, 0, 0);
    }
    evt_set_cs(evt_wb_save_pb, 0, 0, CS_NOW);
}

slow_code_ex void warmboot_save_pbt_done(u32 r0, u32 r1, u32 r2)
{
    u32 pbt_blk;
    u32 *vc;
    u32 dtag_cnt;
    dtag_t dtag;
    u32 *ddr_vac_buffer = (u32*)ddtag2mem(shr_vac_drambuffer_start);
#if(PLP_SUPPORT == 1) 
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
 #endif   
    if(plp_trigger){
        return ;
    }

    if (pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt)
    {
        evt_set_cs(evt_wb_save_pb, 0, 0, CS_TASK);
        return;
    }
    dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
    memcpy((u8*)ddr_vac_buffer, (u8*)vc, dtag_cnt*4096);
#if(PLP_SUPPORT == 1)
    memcpy(epm_ftl_data->epm_vc_table, vc, get_total_spb_cnt()*sizeof(u32));

	epm_ftl_data->spor_tag = FTL_EPM_SPOR_TAG;
	epm_ftl_data->last_close_host_blk = host_spb_close_idx;
	epm_ftl_data->last_close_gc_blk = gc_spb_close_idx;
	epm_update(FTL_sign, (CPU_ID - 1));
#endif
	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
	gFtlMgr.GlobalPageSN = shr_page_sn;
    gFtlMgr.pbt_cur_loop = shr_pbt_loop;
    gFtlMgr.qbt_cur_loop = shr_qbt_loop;
	gFtlMgr.evlog_index = evlog_next_index;
    if (ps_open[FTL_CORE_PBT] != INV_U16)
    {
        pbt_blk = ps_open[FTL_CORE_PBT];
        pbt_resume_param->sw_flag = spb_get_sw_flag(pbt_blk);
        ftl_apl_trace(LOG_INFO, 0x2580, "[FTL]ftl_pbt_cnt: %u, shr_pbt_loop: %u, pbt_blk: %u, flag: 0x%x sw_flags: 0x%x", 
            ftl_pbt_cnt, shr_pbt_loop, pbt_blk, spb_get_flag(pbt_blk), spb_get_sw_flag(pbt_blk));
    }

	/*if(QBT_BLK_CNT == 2)// 8T
	{
		if((ftl_pbt_idx%2) == 0)
		{
			shr_pbt_loop = ftl_pbt_idx;
		}
		else
		{
			shr_pbt_loop = ftl_pbt_idx - 1;
		}
	}
	else if(QBT_BLK_CNT == 1)// 4T
	{
		shr_pbt_loop = ftl_pbt_idx;
	}
	//blk_shutdown_handle(SPB_WARMBOOT);
	gFtlMgr.pbt_cur_loop = shr_pbt_loop;

	if(FTL_MAX_QBT_CNT == 4)
	{
		if((spb_get_free_cnt(SPB_POOL_PBT)%QBT_BLK_CNT)!=0){
		FTL_BlockPopPushList(SPB_POOL_FREE, spb_mgr_get_tail(SPB_POOL_PBT), FTL_SORT_BY_EC);
		}
	}

	ftl_pbt_cnt = spb_get_free_cnt(SPB_POOL_PBT);*/
	pbt_query_ready			= 1;
	pbt_query_need_resume 	= 0;

	//spb_PurgePool2Free(SPB_POOL_PBT_ALLOC,FTL_SORT_BY_EC);
	//spb_PurgePool2Free(SPB_POOL_EMPTY,FTL_SORT_BY_EC);
	FTL_SearchGCDGC();

	chk_close_blk_push();
#if (OTF_TIME_REDUCE == ENABLE)
	blk_shutdown_handle(SPB_WARMBOOT);
#endif

	FTL_CopyFtlBlkDataToBuffer(0);
	spb_pool_table();
	ftl_apl_trace(LOG_INFO, 0xb839, "[FTL]pgsn 0x%x%x", (u32)(shr_page_sn>>32), (u32)(shr_page_sn));
	shr_shutdownflag = false;
	local_item_done(warm_boot_ftl_handle);
	#ifdef OTF_MEASURE_TIME
    	ftl_apl_trace(LOG_INFO, 0xd5b7, "[M_T]warm boot close open blk %8d us\n", time_elapsed_in_us(global_time_start[19]));
	#endif
	return;
}

slow_code_ex void ftl_warm_boot_handle(void)
{
	ftl_apl_trace(LOG_WARNING, 0x20b8, "[FTL]ftl_warm_boot_handle");

	//20210520-Eddie CA3 ==> OTF Updates ==> delete patrol read
	del_timer(&patrol_read_timer);

	if(plp_trigger){
		return ;
	}

	shr_shutdownflag = true;
	chk_close_blk_push();

	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));
	sys_assert(action);
	action->act = GC_ACT_SUSPEND;
	action->caller = NULL;
	action->cmpl = warmboot_gc_action_done;

	if (gc_action(action))
	{
		//sys_free(FAST_DATA, action);
		action->cmpl(action);
	}
/*
	ftl_spb_pad_t *spb_pad = sys_malloc(FAST_DATA, sizeof(ftl_spb_pad_t));
	sys_assert(spb_pad);
	spb_pad->spb_id = 0xFFFF;
	spb_pad->ctx.caller = NULL;
	spb_pad->ctx.cmpl = ftl_warm_boot_spb_handle_done;
#if (OTF_TIME_REDUCE == ENABLE)
	spb_pad->param.cwl_cnt = 4;
	spb_pad->param.pad_all = false;
#else
	spb_pad->param.cwl_cnt = 0;// 4 dummy WL
	spb_pad->param.pad_all = true;
#endif
	spb_pad->param.type = FTL_CORE_GC;
    spb_pad->param.nsid = 1;
    spb_pad->cur.cwl_cnt = 0;
	spb_pad->cur.type = FTL_CORE_NRM;
	spb_pad->cur.nsid = 1;
	ftl_apl_trace(LOG_WARNING, 0, "[FTL]warm boot close open blk");
#ifdef OTF_MEASURE_TIME
	global_time_start[19] = get_tsc_64();
#endif
	ftl_core_spb_pad(spb_pad);
*/
}
#endif
#endif
init_code u32 rda_2_pb(rda_t rda, u32 *spb_id)
{
	row_t row = rda.row;
	u16 ch = rda.ch;
	u16 dev = rda.dev;
	u16 lun = row >> shr_nand_info.row_lun_shift;
	row = (row & ((1U << shr_nand_info.row_lun_shift) - 1));
	u32 blk = row >> shr_nand_info.row_block_shift;

	*spb_id = blk; // SPB id

	row = (row & ((1U << shr_nand_info.row_block_shift) - 1));
	u16 pln = row >> shr_nand_info.row_pl_shift;

	u32 dpos = pln+ (ch << FTL_CH_SHF) + (lun << FTL_LUN_SHF) + (dev << FTL_CE_SHF);
	return dpos;
}

fast_code bool ftl_suspend(enum sleep_mode_t mode)
{
	return true;
}

fast_code void ftl_resume(enum sleep_mode_t mode)
{
	defect_mgr_resume();
	spb_log_resume();
	sys_log_resume();

     /*
	* only load vcnt tbl to accelerate resume,
	* l2p tbl will be loaded in the background or hit by host read or write after pmu resumed
	*/
/*
#if (SPOR_FLOW == mENABLE)
    ftl_misc_reload(L2P_LAST_PDA);
#else
	ftl_misc_reload();
#endif
*/
	ftl_l2p_resume();
	gc_resume();
	bg_task_resume();
	pmu_swap_file_order(need_page);
}
fast_code void ipc_qbt_alloc_done(volatile cpu_msg_req_t *req)
{
	ns_start_t *_ctx = (ns_start_t*)req->pl;
	if (is_ptr_tcm_share((void*)_ctx))
		_ctx = (ns_start_t*)tcm_share_to_local((void*)_ctx);
	_ctx->cmpl(_ctx);
}

fast_code void ipc_ftl_open(volatile cpu_msg_req_t *req)
{
	ftl_open(req->pl);
}



fast_code void ipc_spb_clear_ec(volatile cpu_msg_req_t *req)
{
	spb_clear_ec();
}

fast_code void ipc_get_new_ec(volatile cpu_msg_req_t *req)
{
	get_avg_erase_cnt_1(req->cmd.flags, req->pl);
}

fast_code void ipc_spb_query(volatile cpu_msg_req_t *req)
{
	nsid_type_t pl = { .all = req->pl };

	ftl_set_spb_query(pl.b.nsid, pl.b.type);
}

fast_code void ipc_flush_blklist(volatile cpu_msg_req_t *req)
{
	nsid_type_t pl = { .all = req->pl };
	ftl_set_flush_blklist(pl.b.nsid, pl.b.type);
}

fast_code void ipc_free_blklist(volatile cpu_msg_req_t *req)
{
	ftl_free_blist_dtag(req->pl);
}


fast_code void ipc_pstream_ack(volatile cpu_msg_req_t *req)
{
	spb_clr_sw_flag(req->pl, SPB_SW_F_ALLOCATING);
}

fast_code void ipc_free_flush_blklist_dtag(volatile cpu_msg_req_t *req)
{
	//ftl_free_flush_blklist_dtag();
}



fast_code void ipc_spb_weak_retire(volatile cpu_msg_req_t *req)
{
	spb_weak_retire_t weak_retire = { .all = req->pl };

	ftl_spb_weak_retire(weak_retire);
}

fast_code void ipc_spb_rd_cnt_updt(volatile cpu_msg_req_t *req)
{
	spb_mgr_rd_cnt_upd(req->pl);
//	ipc_spb_rd_cnt_upd_ack(req->cmd.tx, ~0);
}

fast_code void ipc_fe_req_op_done(fe_req_t *req)
{
	ipc_fe_req_t *ipc_fe_req = (ipc_fe_req_t *) req;
	u8 tx = ipc_fe_req->tx;
	u32 pl = (u32) ipc_fe_req->caller;

	if (ipc_fe_req->sync)
		cpu_msg_sync_done(tx);
	else
		cpu_msg_issue(tx, CPU_MSG_FE_REQ_OP_DONE, 0, pl);

	sys_free(FAST_DATA, req);
}

fast_code void ipc_fe_req_op(volatile cpu_msg_req_t *req)
{
	fe_req_t *remote_req = (fe_req_t *) req->pl;
	ipc_fe_req_t *local_req;

	local_req = sys_malloc(FAST_DATA, sizeof(ipc_fe_req_t));
	sys_assert(local_req);

	local_req->fe_req = *remote_req;
	local_req->fe_req.cmpl = ipc_fe_req_op_done;
	local_req->tx = req->cmd.tx;
	local_req->caller = remote_req;
	local_req->sync = (remote_req->cmpl) ? false : true;

	if (local_req->fe_req.write == false) {
		if (local_req->fe_req.type == FE_REQ_TYPE_LOG)
			fe_log_read(&local_req->fe_req);
		else if (local_req->fe_req.type == FE_REQ_TYPE_NS)
			fe_ns_info_load(&local_req->fe_req);
		else
			panic("no support");
	} else {
		if (local_req->fe_req.type == FE_REQ_TYPE_LOG)
			fe_log_flush(&local_req->fe_req);
		else if (local_req->fe_req.type == FE_REQ_TYPE_NS)
			fe_ns_info_save(&local_req->fe_req);
		else
			panic("no support");
	}
}

fast_code void ipc_fe_ns_info_op(volatile cpu_msg_req_t *req)
{
	fe_req_t *remote_req = (fe_req_t *) req->pl;
	ipc_fe_req_t *local_req;

	local_req = sys_malloc(FAST_DATA, sizeof(ipc_fe_req_t));
	sys_assert(local_req);

	local_req->fe_req = *remote_req;
	local_req->fe_req.cmpl = ipc_fe_req_op_done;
	local_req->tx = req->cmd.tx;
	local_req->caller = remote_req;
	local_req->sync = (remote_req->cmpl) ? false : true;

	if (local_req->fe_req.write == false)
		fe_log_read(&local_req->fe_req);
	else
		fe_log_flush(&local_req->fe_req);
}

ddr_code void ipc_get_spare_avg_erase_cnt(volatile cpu_msg_req_t *req)
{
	u32 *buf = (u32 *) req->pl;
	u32 local[7];
	//get_avg_erase_cnt(&local[0], &local[1], &local[2], &local[3]);
	get_host_ns_spare_cnt(1, &local[4], &local[5], &local[6]);
	memcpy(&buf[0], &local[0], sizeof(u32) * 7);
    cpu_msg_issue(req->cmd.tx, CPU_MSG_GET_SPARE_AVG_ERASE_CNT_DONE, 0, (u32)buf);
}

#if 1//PLP_SUPPORT == 0
ddr_code void ipc_update_smart_avg_erase_cnt(volatile cpu_msg_req_t *req)
{
	extern smart_statistics_t *smart_stat;

	u32 used;
	u32 local[7];

	get_avg_erase_cnt(&local[0], &local[1], &local[2], &local[3]);
	get_host_ns_spare_cnt(1, &local[4], &local[5], &local[6]);

	smart_stat->available_spare = 100 * local[4] / local[5];
	smart_stat->available_spare_threshold = local[6];

	used = 100 * local[0] / MAX_AVG_EC;   // fix the issue of mismatch witch smart_log
	if (used > 254)
		used = 255;
	smart_stat->percentage_used = (u8)used;

	extern __attribute__((weak)) bool is_system_read_only(void);
	if (is_system_read_only)
	{
	 	smart_stat->critical_warning.bits.read_only = is_system_read_only();
	}
#if PLP_SUPPORT == 0
	extern void ftl_save_non_plp_ec_table(u8);
	ftl_save_non_plp_ec_table(1);
	epm_update(EPM_NON_PLP, (CPU_ID - 1));
#endif
}
#endif

ddr_code void ipc_get_additional_smart_info(volatile cpu_msg_req_t *req)
{
	u32 *buf = (u32 *) req->pl;
	u32 local[4];
	get_avg_erase_cnt(&local[0], &local[1], &local[2], &local[3]);
	memcpy(&buf[0], &local[0], sizeof(u32) * 4);
	cpu_msg_issue(req->cmd.tx, CPU_MSG_GET_ADDITIONAL_SMART_INFO_DONE, 0, (u32)buf);
}

ddr_code void ipc_compare_vac(volatile cpu_msg_req_t *req)
{

    u64 addr;
    u8  win = 0, old_win = 0;
    lda_t lda;
    //u16   spb_id;
    u32   vc_tbl_dtag_cnt;
    u32   *vc;
    u32   *temp_vc_table;
	u8 error = false;
    extern u32 _max_capacity;

	u32 cached_cap = _max_capacity;
	//dtag_t vac_dtag;

	bool win_change = false;
	pda_t curr_pda;
    u32 spb_id;
	u64 start = get_tsc_64(), end;//, curr;

    ftl_apl_trace(LOG_ALW, 0x6dcf, "start l2p build vac:0x%x-%x", start>>32, start&0xFFFFFFFF);
	u64 time_start = get_tsc_64();
    u64 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000);
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};

    temp_vc_table = sys_malloc(SLOW_DATA, sizeof(u32)*shr_nand_info.geo.nr_blocks);
    sys_assert(temp_vc_table);
    memset(temp_vc_table, 0, (sizeof(u32)*shr_nand_info.geo.nr_blocks));

    // save old win
    old_win = ctrl0.b.cpu3_ddr_window_sel;
    win = 0;
	//curr = get_tsc_64();
    // don't add any uart log during building vac table
    for(lda = 0; lda < cached_cap; lda++)
    {
//    	if (get_tsc_64() - curr > 800*1000*30000) {
//			ftl_apl_trace(LOG_ALW, 0, "30s curr_lda:0x%x, target_lda:0x%x", lda, cached_cap);
//			curr = get_tsc_64();
//		}
        if (lda > 0) {
            addr += 4;
            if (addr >= 0xC0000000) {
                addr -= 0x80000000;
                win++;
                win_change = true;
            }
            if (win_change) {
                ctrl0.b.cpu3_ddr_window_sel = win;
                writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                win_change = false;
            }
        } else {
            addr = addr_base;
            while (addr >= 0xC0000000) {
                addr -= 0x80000000;
                win++;
            }
            ctrl0.b.cpu3_ddr_window_sel = win;
            writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
        }

        curr_pda = *((u32*)(u32)addr);
        if (curr_pda != INV_U32)
        {
            temp_vc_table[pda2blk(curr_pda)]++;
        }
    }

    // revert old win
    ctrl0.b.cpu3_ddr_window_sel = old_win;
    writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));

	end = get_tsc_64();
    ftl_apl_trace(LOG_ALW, 0x0a3f, "l2p build vac end:0x%x-%x", end>>32, end&0xFFFFFFFF);

#if 1
    // Compare ftl vc table with L2P vc, just used for uart debug, runtime not open
    dtag_t dtag = ftl_l2p_get_vcnt_buf(&vc_tbl_dtag_cnt, (void **)&vc);
    for (spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
    {
        if (vc[spb_id] != temp_vc_table[spb_id])
        {
        	error = true;
            ftl_apl_trace(LOG_ALW, 0xdeee, "VAC mismatch, block = 0x%x, hw vac = %d, L2P vac = %d",
                           spb_id, vc[spb_id], temp_vc_table[spb_id]);
        }
    }

    ftl_l2p_put_vcnt_buf(dtag, vc_tbl_dtag_cnt, false);
#endif

    #if(SPOR_L2P_VC_CHK == mENABLE)
    shr_flag_vac_compare = error;
	shr_flag_vac_compare_result = mTRUE;
    #endif

	ftl_apl_trace(LOG_ALW, 0xb12b, "compare finish !!!! err:%d ", error);
	ftl_apl_trace(LOG_ALW, 0x6707, "Function time cost : %d us", time_elapsed_in_us(time_start));

    sys_free(SLOW_DATA, temp_vc_table);

    cpu_msg_issue(CPU_FE - 1, CPU_MSG_PLP_DONE, 0, 0);  //only for debug
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_ACTIVE_GC, 0, 0);
    //extern u8 cal_done;
    //cal_done = 1;

}


fast_code void ipc_ftl_core_ctx_done(volatile cpu_msg_req_t *req)
{
	ftl_core_ctx_t *ctx = (ftl_core_ctx_t *) req->pl;

	if (is_ptr_tcm_share((void *) ctx))
		ctx = (ftl_core_ctx_t *) tcm_share_to_local((void *) ctx);

	ctx->cmpl(ctx);
}

fast_code void ipc_spb_gc_done(volatile cpu_msg_req_t *req)
{
	u16 spb_id = req->cmd.flags;
	u32 free_du_cnt = req->pl;

	ftl_gc_done(spb_id, free_du_cnt);
}

fast_code void _ipc_ftl_flush_done(flush_ctx_t *ctx)
{
	flush_ctx_ipc_hdl_t *hdl = (flush_ctx_ipc_hdl_t *) ctx;
	flush_ctx_t *remote_ctx = (flush_ctx_t *) hdl->ctx.caller;
	/*
	if(hdl->tx == 0)
	{
		shr_ftl_save_qbt_flag = true;
		ftl_apl_trace(LOG_INFO, 0, "NS_DEL_Flush %d", shr_ftl_save_qbt_flag);
		cpu_msg_issue(hdl->tx, CPU_MSG_FTL_NS_FLUSH_DONE, 0, (u32)remote_ctx);
	}
	*/
	if(hdl->tx == 1)
	{
		ftl_apl_trace(LOG_INFO, 0xa82a, "NORMAL_Flush");
		cpu_msg_issue(hdl->tx, CPU_MSG_FTL_FLUSH_DONE, 0, (u32)remote_ctx);
	}
	sys_free(FAST_DATA, hdl);
}

fast_code void ipc_ftl_flush(volatile cpu_msg_req_t *req)
{
	flush_ctx_ipc_hdl_t *hdl = sys_malloc(FAST_DATA, sizeof(flush_ctx_ipc_hdl_t));
    flush_ctx_t *remote_ctx = (flush_ctx_t *) req->pl;

    sys_assert(hdl);
    hdl->ctx = *remote_ctx;
    hdl->ctx.cmpl = _ipc_ftl_flush_done;
    hdl->ctx.caller = remote_ctx;
    hdl->tx = req->cmd.tx;
    ftl_flush(&hdl->ctx);
}
ddr_code void ftl_full_trim_done(ftl_core_ctx_t * ctx)
{
    void *req = ctx->caller;
    sys_free(FAST_DATA, ctx);
    epm_format_state_update(0, 0);
    epm_update(FTL_sign, (CPU_ID-1));     // update epm in time to prevent plp not done
    if(req)
    {
    	if(req == (void*)INV_U32)
    	{
			req = 0;//this is for special case(nvme format for all drive)
		}

        cpu_msg_issue(CPU_FE - 1, CPU_MSG_FTL_FULL_TRIM_HANDLE_DONE, 0, (u32)req);
    }
}

typedef struct _ftl_hns_t {
	ftl_ns_t *ns;		///< parent ftl namespace object

	l2p_ele_t l2p_ele;	///< l2p element, describe l2p of this namespace
} ftl_hns_t;

extern ftl_hns_t _ftl_hns[FTL_NS_ID_END];	///< ftl host namespace objects

ddr_code void ftl_full_trim(void *req)
{
	ftl_format_t *format;
    ftl_hns_t *hns = &_ftl_hns[FTL_NS_ID_START];
    ftl_l2p_reset(&hns->l2p_ele);

	format = sys_malloc(FAST_DATA, sizeof(ftl_format_t));
	sys_assert(format);

	format->ctx.caller = req;
	format->ctx.cmpl = ftl_full_trim_done;
	format->flags.all = 0;
	format->nsid = FTL_NS_ID_START;
	format->flags.b.full_trim = 1;

	ftl_core_format(format);
}

ddr_code void ipc_ftl_full_trim(volatile cpu_msg_req_t *req)
{
    ftl_full_trim((void *)req->pl);
}

slow_code void ftl_del_ns_done(ftl_core_ctx_t * ctx)
{
	shr_ftl_save_qbt_flag = true;
	ftl_apl_trace(LOG_INFO, 0xf1f4, "ftl_ns_del_done %d", shr_ftl_save_qbt_flag);
	sys_free(FAST_DATA, ctx);
}

fast_code void ipc_ftl_del_ns(volatile cpu_msg_req_t *req)
{
	ftl_format_t *format = sys_malloc(FAST_DATA, sizeof(ftl_format_t));
	sys_assert(format);

	//format->ctx.caller = req;
	format->ctx.cmpl = ftl_del_ns_done;
	format->flags.all = 0;
	format->nsid = FTL_NS_ID_START;
	format->flags.b.del_ns = 1;

	ftl_core_format(format);
}

fast_code void ipc_ftl_update_frb_info(volatile cpu_msg_req_t *req)
{
#if (QBT_TLC_MODE == mENABLE)
	u32 qbt_sn;
	u16 qbt_blk;
	ftl_apl_trace(LOG_INFO, 0xa55a, "update ftl qbt tag");
	qbt_blk = spb_mgr_get_head(SPB_POOL_QBT_ALLOC);
	if(qbt_blk != INV_U16)
	{
		ftl_apl_trace(LOG_INFO, 0x5f95, "valid qbt blk idx %d", qbt_blk);
		qbt_sn = spb_get_sn(qbt_blk);
		frb_log_set_qbt_info(0x1, qbt_blk, qbt_sn);
		frb_log_type_update(FRB_TYPE_HEADER);
	}
	else
	{
		ftl_apl_trace(LOG_INFO, 0xa138, "invalid qbt blk idx %d", qbt_blk);
	}
#endif
}

slow_code void FTL_GLOVARINIT(ftl_initial_mode_t mode)
{
	spb_info_init(mode);
	spb_mgr_init(mode);
	spb_pool_init();
}
#ifdef Dynamic_OP_En
extern u8 DYOP_FRB_Erase_flag;
#endif

#if (PLP_SUPPORT == 0) 
ddr_code void ftl_save_non_plp_ec_table(u8 method)
{
	ftl_apl_trace(LOG_INFO, 0xddb1, "[IN] ftl_save_non_plp_ec_table method:%d", method);
	spb_id_t spb_id;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(method)//blist to epm
	{
		for(spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
		{
			if( spb_info_tbl[spb_id].erase_cnt  > epm_ftl_data->epm_ec_table[spb_id])
			{
				epm_ftl_data->epm_ec_table[spb_id] = spb_info_tbl[spb_id].erase_cnt;
			}
		}
	}
	else//epm to blist
	{
	    for(spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
	    {
			if(epm_ftl_data->epm_ec_table[spb_id] > spb_info_tbl[spb_id].erase_cnt)
	    	{
				spb_info_tbl[spb_id].erase_cnt = epm_ftl_data->epm_ec_table[spb_id];
			}
	    }		
	}
}
#endif

ddr_code void ftl_format(format_ctx_t *ctx)
{

#if defined(ENABLE_L2CACHE)
	l2cache_flush_all();
#endif

	FTL_GLOVARINIT(FTL_INITIAL_PREFORMAT);

	if (ctx->b.ins_format)
	{
		ftl_ns_format(FTL_NS_ID_INTERNAL, false);
	}
	ftl_ns_format(ctx->b.ns_id, ctx->b.host_meta);

    ftl_apl_trace(LOG_ERR, 0xa343, "[Preformat] init spor_last_rec_blk_sn");
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    epm_ftl_data->pbt_force_flush_flag = 0;
    epm_ftl_data->record_PrevTableSN = 0;
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)     
    epm_ftl_data->spor_last_rec_blk_sn = INV_U32;
    epm_ftl_data->panic_build_vac = false;
    extern volatile u32 spor_qbtsn_for_epm;
    spor_qbtsn_for_epm = 0;
    
    epm_ftl_data->max_shuttle_gc_blk_sn = 0;
	epm_ftl_data->max_shuttle_gc_blk = INV_SPB_ID;
#endif
#if (PLP_SUPPORT == 0) 
    for(u8 i_idx = 0;i_idx<2;i_idx++) 
    { 
        epm_ftl_data->host_open_blk[i_idx] = INV_U16; 
        epm_ftl_data->gc_open_blk[i_idx]   = INV_U16; 
    } 
    for(u8 i=0;i<SPOR_CHK_WL_CNT;i++) 
    { 
        epm_ftl_data->host_open_wl[i] = INV_U16; 
        epm_ftl_data->host_die_bit[i] = 0; 
        epm_ftl_data->gc_open_wl[i]   = INV_U16; 
        epm_ftl_data->gc_die_bit[i]   = 0; 
    }  
	epm_ftl_data->host_aux_group 	  = INV_U16;
	epm_ftl_data->gc_aux_group   	  = INV_U16;

	epm_ftl_data->non_plp_gc_tag 	  = 0;
	epm_ftl_data->non_plp_last_blk_sn = INV_U32;

	ftl_save_non_plp_ec_table(1);//restore ec table    from blist to epm

    extern volatile u8 non_plp_format_type;
    if(non_plp_format_type != NON_PLP_PREFORMAT)
    	non_plp_format_type = NON_PLP_FORMAT; 

#endif 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	epm_ftl_data->plp_slc_gc_tag = 0;
	epm_ftl_data->plp_pre_slc_wl = 0;
    epm_ftl_data->plp_slc_wl     = 0;
    epm_ftl_data->plp_slc_times  = 0;
    epm_ftl_data->slc_format_tag = SLC_ERASE_FORMAT_TAG;

	epm_ftl_data->plp_slc_disable = 0;
	epm_ftl_data->plp_last_blk_sn = INV_U32;
	epm_ftl_data->esr_lock_slc_block = false;
	extern volatile u32 max_usr_blk_sn;
	max_usr_blk_sn = INV_U32;
	ftl_apl_trace(LOG_ERR, 0xa593, "[SLC] epm_ftl_data->slc_format_tag:0x%x",epm_ftl_data->slc_format_tag);
#endif

    epm_update(FTL_sign, (CPU_ID-1));

	epm_error_warn_t* epm_error_warn_data = (epm_error_warn_t*)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
	if(epm_error_warn_data->need_init)
	{
		u16* epm_addr_p = &epm_error_warn_data->need_init;
		memset(epm_addr_p, 0x0 ,EPM_ERROR_WARN_SIZE*4);
    	epm_update(ERROR_WARN_sign, (CPU_ID-1));
	}
}

ddr_code void ftl_format_flush_done(flush_ctx_t *ctx)
{
	flush_ctx_ipc_hdl_t *hdl = (flush_ctx_ipc_hdl_t *) ctx;
	void *remote_ctx = hdl->ctx.caller;
	cpu_msg_issue(hdl->tx, CPU_MSG_FTL_FORMAT_DONE, 0, (u32)remote_ctx);
	sys_free(FAST_DATA, hdl);
}

//20200910 kevin add preformat_erase
share_data volatile u32 er_blk_cnt;
ddr_code void  spb_preformat_erase_continue(void)
{
	u16 erase_index;
	extern epm_info_t *shr_epm_info;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    ftl_apl_trace(LOG_INFO, 0x4407, "epm_fmt_not_finish:0x%x, format_tag:0x%x",epm_ftl_data->epm_fmt_not_finish, epm_ftl_data->format_tag);
    if (epm_ftl_data->format_tag == FTL_PREFORMAT_TAG)
    {
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    	if((srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT <= epm_ftl_data->epm_fmt_not_finish && epm_ftl_data->epm_fmt_not_finish < get_total_spb_cnt() - tcg_reserved_spb_cnt ) || epm_ftl_data->epm_fmt_not_finish == 0xffffffff )
#else
    	if((srb_reserved_spb_cnt <= epm_ftl_data->epm_fmt_not_finish && epm_ftl_data->epm_fmt_not_finish < get_total_spb_cnt() - tcg_reserved_spb_cnt ) || epm_ftl_data->epm_fmt_not_finish == 0xffffffff )
#endif
    	{
    		if(epm_ftl_data->epm_fmt_not_finish == 0xffffffff)//epm_fmt_not_finish == 0xffffffff previous format not finish(plp happend before erase block)
    		{
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    			epm_ftl_data->epm_fmt_not_finish = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT;//skip psb block 0
#else
    			epm_ftl_data->epm_fmt_not_finish = srb_reserved_spb_cnt;//skip psb block 0
#endif
    		}

    		for(erase_index = epm_ftl_data->epm_fmt_not_finish;erase_index<get_total_spb_cnt() - tcg_reserved_spb_cnt;erase_index++){
                ftl_sblk_erase(erase_index);
    		}
#if(PLP_SUPPORT == 1)
            ftl_clear_vac_ec_in_epm();
#endif
    	}
    }
    else if ((epm_ftl_data->format_tag != FTL_FULL_TRIM_TAG) && ((epm_ftl_data->format_tag != 0) || (epm_ftl_data->epm_fmt_not_finish != 0)))
    {
        ftl_apl_trace(LOG_WARNING, 0xcbf2, "format may error!!!!!");
    
    }
}

void spb_preformat_erase(flush_ctx_t* flush);
ddr_code void spb_preformat_erase_done(erase_ctx_t *ctx)
{
	u32 spb_cnt = get_total_spb_cnt() - tcg_reserved_spb_cnt;
	flush_ctx_t* flush = (flush_ctx_t*) ctx->caller;
	sys_free(FAST_DATA, ctx);

	if(plp_trigger)
	{
        u32 first_erase_spb;
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)
        first_erase_spb = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT;
#else
        first_erase_spb = srb_reserved_spb_cnt;
#endif
		epm_format_state_update((er_blk_cnt < first_erase_spb)?first_erase_spb:er_blk_cnt, FTL_PREFORMAT_TAG);
		flush->cmpl(flush);
		return;
	}

	er_blk_cnt++;

	if(er_blk_cnt < spb_cnt)
	{
		// ISU, TSSP, PushGCRelsFalInfo
		// Handle mark bad when a spb erase done.
		#ifdef ERRHANDLE_GLIST  
		if (ctx->spb_ent.b.erFal)
		{
			MK_FailBlk_Info failBlkInfo;
			failBlkInfo.all = 0;
			failBlkInfo.b.wBlkIdx = ctx->spb_ent.b.spb_id;
			failBlkInfo.b.need_gc = false;
			ErrHandle_Task(failBlkInfo);
		}	
		#endif

		spb_preformat_erase(flush);
	}
	else
	{
		ftl_apl_trace(LOG_ERR, 0x5032, "erase end er_blk_cnt=%d",er_blk_cnt);
		ftl_apl_trace(LOG_ERR, 0x0b50, "spb_preformat_erase_done to flush");

		epm_format_state_update(er_blk_cnt, FTL_PREFORMAT_TAG);
		er_blk_cnt = 0;

		memset(&gFtlMgr, 0x00, sizeof(stFTL_MANAGER));
        gFtlMgr.last_host_blk = INV_U16;
        gFtlMgr.last_gc_blk   = INV_U16;
		shr_page_sn = gFtlMgr.GlobalPageSN;

		flush->cmpl(flush);
		//ftl_flush(flush);
	}
}

ddr_code void spb_preformat_erase(flush_ctx_t* flush)
{

	erase_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(erase_ctx_t));
	sys_assert(ctx);
	ctx->spb_ent.all = er_blk_cnt;

	ctx->spb_ent.b.slc = 0;


	ctx->cmpl_cnt = 0;
	ctx->issue_cnt = 0;
	ctx->caller = flush;
	ctx->cmpl = spb_preformat_erase_done;

    if((spb_info_tbl[er_blk_cnt].pool_id == SPB_POOL_FREE) || (spb_info_tbl[er_blk_cnt].pool_id == SPB_POOL_QBT_FREE))
	{
		//spb_info_get(er_blk_cnt)->erase_cnt++;
		//ftl_apl_trace(LOG_ERR, 0, "Erase spb[%d] pool_id[%d] EC[0x%x]\n",er_blk_cnt,spb_info_tbl[er_blk_cnt].pool_id,spb_info_tbl[er_blk_cnt].erase_cnt);
		//Ec_tbl_update(er_blk_cnt);
		ftl_core_erase(ctx);
	}
	else
	{
		spb_preformat_erase_done(ctx);
	}
}

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
ddr_code void slc_buffer_format_erase_done(erase_ctx_t *ctx)
{
	sys_free(FAST_DATA, ctx);
	ftl_apl_trace(LOG_ALW, 0x869f, "[SLC]slc_buffer_format_erase_done");
}
ddr_code void slc_buffer_format_erase(flush_ctx_t* flush)
{	
	ftl_apl_trace(LOG_ALW, 0x8ca8, "[SLC] IN slc_buffer_format_erase");
	erase_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(erase_ctx_t));
	sys_assert(ctx);
	ctx->spb_ent.all = PLP_SLC_BUFFER_BLK_ID;

	ctx->spb_ent.b.slc = 1;
	ctx->cmpl_cnt = 0;
	ctx->issue_cnt = 0;
	ctx->caller = flush;
	ctx->cmpl = slc_buffer_format_erase_done;	
	ftl_core_erase(ctx);
}	
#endif


#if defined(SAVE_CDUMP)
ddr_code void ipc_ulink_da_mode3(volatile cpu_msg_req_t *req)
{
	extern u32 ulink_da_mode(void);
	ulink_da_mode();
}
#endif

fast_code void ipc_ftl_format(volatile cpu_msg_req_t *req)
{
	format_ctx_t *remote_ctx = (format_ctx_t *) req->pl;

	if(!(remote_ctx->b.del_ns))
	{
		ftl_format(remote_ctx);
	    last_spb_vcnt_zero = INV_SPB_ID;
	}

    ftl_pbt_cnt = 0;
	flush_ctx_ipc_hdl_t *hdl = sys_malloc(FAST_DATA, sizeof(flush_ctx_ipc_hdl_t));
	flush_ctx_t *flush = &hdl->ctx;

	flush->caller = remote_ctx;
	flush->cmpl = ftl_format_flush_done;
	flush->flags.all = 0;
	flush->flags.b.format = 1;
	flush->nsid = remote_ctx->b.ns_id;
	hdl->tx = req->cmd.tx;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	slc_buffer_format_erase(flush);//when format ,reset slc buffer
#endif
	if(remote_ctx->b.preformat_erase)
	{
		ftl_apl_trace(LOG_INFO, 0xf4d7, "[FTL]preformat erase");
#ifdef Dynamic_OP_En
		if(DYOP_FRB_Erase_flag == 1)
		{
			ftl_flush(flush);		
		}
		else
#endif			
		{
			spb_preformat_erase(flush);
		}
	}

	if(remote_ctx->b.full_trim)
	{
		ftl_apl_trace(LOG_INFO, 0x1516, "[FTL]full trim format no erase");
		ftl_flush(flush);
	}

	if(remote_ctx->b.del_ns)
	{
		ftl_apl_trace(LOG_INFO, 0xe89e, "[FTL]del ns save qbt");
		ftl_flush(flush);
	}	

}

fast_code void ipc_gc_action_cmpl(gc_action_t *act)
{
	cpu_msg_issue(act->tx, CPU_MSG_GC_ACT_DONE, 0, (u32)act->caller);
	sys_free(FAST_DATA, act);
}

fast_code void ipc_gc_action(volatile cpu_msg_req_t *req)
{
	gc_action_t *_act = (gc_action_t*)req->pl;
	gc_action_t *act = sys_malloc(FAST_DATA, sizeof(gc_action_t));
    sys_assert(act);
	act->act = _act->act;
	act->tx = req->cmd.tx;
	act->caller = _act;
	act->cmpl = ipc_gc_action_cmpl;

	if (gc_action(act))
		act->cmpl(act);
}

fast_code void ipc_fcore_gc_action_done(volatile cpu_msg_req_t *req)
{
	gc_action_t *action = (gc_action_t*)req->pl;

	if (is_ptr_tcm_share((void*)action))
		action = (gc_action_t*)tcm_share_to_local((void*)action);

	action->cmpl(action);
}
#if GC_SUSPEND_FWDL		//20210308-Eddie
slow_code_ex void ipc_fwdl_gc_handle (volatile cpu_msg_req_t *req)
{
	u8 type = req->pl;
	FWDL_GC_Handle(type);
}
#endif
fast_code void ipc_l2p_load(volatile cpu_msg_req_t *req)
{
	u32 seg_id = req->pl;
	u32 tx = req->cmd.tx;

	ftl_l2p_urgent_load(seg_id, tx);
}

#if(WARMBOOT_FTL_HANDLE == mENABLE)
slow_code_ex void ipc_warm_boot_ftl_reset(volatile cpu_msg_req_t *req)
{
	ftl_warm_boot_handle();
}
#endif

fast_code void ipc_l2p_partial_reset(volatile cpu_msg_req_t *req)//curry add for ns del 202011
{
	u32 ns_sec_id = req->pl;
	ftl_l2p_partial_reset(ns_sec_id);
}
// #if CO_SUPPORT_DEVICE_SELF_TEST
/*fast_code void ipc_self_device_test_delay(volatile cpu_msg_req_t *req){
	//ftl_apl_trace(LOG_INFO, 0x12f8, "ipc_self_device_test_delay 3s");
	//mdelay(3000);
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_DT_DELAY_DONE, 0, 0);
}*/
// #endif

fast_code void ipc_gc_stop(volatile cpu_msg_req_t *req)
{
	ftl_l2p_gc_suspend();
}

fast_code void ipc_gc_reset(volatile cpu_msg_req_t *req)//curry add for ns del 202011
{
	//u32 ns_sec_id = req->pl;
	gc_re();
}
#if 0//(TRIM_SUPPORT == DISABLE)
fast_code void ipc_ucache_suspend_trim_done(volatile cpu_msg_req_t *req)
{
	flush_st_suspend_trim_done(1);
}
#endif

fast_code void ipc_release_spb(volatile cpu_msg_req_t *req)
{
	spb_id_t spb_id = req->pl;
	spb_release(spb_id);
}

fast_code void ipc_release_pbt_spb(volatile cpu_msg_req_t *req)
{
	spb_id_t spb_id = req->pl;
	pbt_release(spb_id);
}

#ifdef ERRHANDLE_VERIFY	// FET, RelsP2AndGL
EXTERN ddr_code int frb_drop_P2GL(void);
ddr_code void ipc_frb_drop_P2GL(volatile cpu_msg_req_t *req)
{
	frb_drop_P2GL();
}
#endif


fast_code void _ipc_raid_correct_done(volatile cpu_msg_req_t *req)
{
	rc_req_t *rc_req = (rc_req_t*)req->pl;

	rc_req->cmpl(rc_req);
}

ddr_code void ipc_ondeman_dump(volatile cpu_msg_req_t *req)
{
    ftl_apl_trace(LOG_INFO, 0x5826, "l2p_updt:  rptr [%d] wptr [%d]", l2p_updt_que.rptr, l2p_updt_que.wptr);
    ftl_apl_trace(LOG_INFO, 0x67b4, "dtag_free: rptr [%d] wptr [%d]", dtag_free_que.rptr, dtag_free_que.wptr);
    ftl_apl_trace(LOG_INFO, 0x760b, "[FTL]Free blk cnt %d",spb_get_free_cnt(SPB_POOL_FREE));
    ftl_apl_trace(LOG_INFO, 0xccb3, "[GC] blk:0x%x", get_gc_blk());
    tzu_get_gc_info();
}

extern volatile u8 cpu_feedback[];
ddr_code void ipc_feedback_set(volatile cpu_msg_req_t *req)
{
    cpu_feedback[CPU_ID_0] = true;
}

fast_code void ipc_clear_api(volatile cpu_msg_req_t *req)
{
    extern void clear_api(void);
    clear_api();
}

fast_code void ipc_gc_chk(volatile cpu_msg_req_t *req)
{
	extern smart_statistics_t *smart_stat;
	extern read_only_t read_only_flags;
	if((smart_stat->critical_warning.raw == 0x8) && (spb_get_free_cnt(SPB_POOL_FREE) > GC_BLKCNT_READONLY))
		read_only_flags.b.no_free_blk = 1;

    GC_Mode_Assert();
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	extern u32 shr_plp_slc_need_gc;

	if(shr_plp_slc_need_gc == FTL_PLP_SLC_NEED_GC_TAG && req->pl == 1)
	{
		GC_MODE_SETTING(GC_PLP_SLC);
		shr_plp_slc_need_gc = false;
	}
#endif
#if (PLP_SUPPORT == 0)
	GC_MODE_SETTING(GC_NON_PLP);
#endif
    ftl_ns_gc_start_chk(FTL_NS_ID_START);
}

slow_code void fake_ipc_gc_chk(u32 p0, u32 p1, u32 p2)
{
	extern smart_statistics_t *smart_stat;
	extern read_only_t read_only_flags;
	if((smart_stat->critical_warning.raw == 0x8) && (spb_get_free_cnt(SPB_POOL_FREE) > GC_BLKCNT_READONLY))
		read_only_flags.b.no_free_blk = 1;

	GC_Mode_Assert();
	u32 type = p2;
	ftl_apl_trace(LOG_INFO, 0x2bc8,"enter fake ipc chk , type:%d",type);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)  
    if(type == 1)
    {
    	extern u32 shr_plp_slc_need_gc;
    	GC_MODE_SETTING(GC_PLP_SLC);
    	shr_plp_slc_need_gc = false;
    }
#endif
#if (PLP_SUPPORT == 0)
	if(type == 2)
	{
		GC_MODE_SETTING(GC_NON_PLP);
	}
#endif
	ftl_ns_gc_start_chk(FTL_NS_ID_START);

}

#if PATROL_READ

#define LOG_PARD 0
#define WORST_WL 379

ddr_data_ni bool pard_switch;
ddr_data_ni u8 du_count;
ddr_data_ni pard_mgr_t pard_mgr;
init_code void pard_mgr_init(pard_mgr_t *pard_mgr)
{
    pard_mgr->prev_blk = 0xFFFF; //0xFFFF:get the first spb in user pool
    pard_mgr->do_gc = false; //1:todo gc
    pard_mgr->sblk_change = false; //1:the pool id or sn has changed while patrol read
    pard_mgr->pard_undone_cnt = 0; //0:patrol read ncl cmds have totally done
    pard_mgr->patrol_times = 0; //0:prepare to patrol read a spb wl
    pard_mgr->pool_id = 0;//the pool id of current spb
    pard_mgr->patrol_blk = 0;
    pard_mgr->prev_sn = 0; //the sn of spb
    pard_mgr->pwron_max_sn = spb_get_sn(spb_mgr_get_tail(SPB_POOL_USER));
};


#if (PARD_DIE_AIPR == 64)
#define PARD_READ_CYCLE 7*HZ/100
pard_cmd_t pard_cmd_mgr[64];
dw_t pard_table[4] = {{.w.l = 0, .w.h= 64}, {.w.l = 64, .w.h= 128}, {.w.l = 128, .w.h= 192}, {.w.l = 192, .w.h= 256}};

#elif (PARD_DIE_AIPR == 32)
#define PARD_READ_CYCLE 4*HZ/100
pard_cmd_t pard_cmd_mgr[32];
dw_t pard_table[8] = {{.w.l = 0, .w.h= 32}, {.w.l = 32, .w.h= 64}, {.w.l = 64, .w.h= 96}, {.w.l = 96, .w.h= 128} , \
                    {.w.l = 128, .w.h= 160}, {.w.l = 160, .w.h= 192}, {.w.l = 192, .w.h= 224}, {.w.l = 224, .w.h= 256}};
#endif

#if CO_SUPPORT_DEVICE_SELF_TEST

ddr_code void DST_meta_search(u32 * LDA_addr, u8 * bmp_addr, struct ncl_cmd_t *ncl_cmd, u32 list_index)
{
	io_meta_t *meta;
	extern void *shr_ddtag_meta;	
	meta = shr_ddtag_meta;

	bm_pl_t bm_pl = ncl_cmd->user_tag_list[list_index];

	dtag_t dtag;
	dtag.dtag = (u32)bm_pl.pl.dtag;

	*LDA_addr = meta[dtag.b.dtag].lda;

	*bmp_addr = meta[dtag.b.dtag].wunc.WUNC;

	ftl_apl_trace(LOG_INFO, 0xd298, "Max chk LDA|0x%x - bmp|0x%x",*LDA_addr, *bmp_addr); 

}

ddr_code void DST_patrol_read_done(struct ncl_cmd_t *ncl_cmd)
{
    pda_t pda = *ncl_cmd->addr_param.common_param.pda_list;
    u32 cnt = ncl_cmd->addr_param.common_param.list_len;
    struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
    u32 block = pda2blk(pda);
    u32 i = 0;
//    u32 nsid = spb_get_nsid(block);
	u32 LBA = 0;
    ftl_apl_trace(LOG_DEBUG, 0x3cb9, "ncl_cmd 0x%x pda 0x%x %dcmd not done", ncl_cmd, pda, pard_mgr.pard_undone_cnt);

    /*mark the spb that needs to do gc*/
    if(ncl_cmd->status)
    {
        for(i = 0; i < cnt; i++)
        {
        	if(plp_trigger)
        	{
				//ftl_apl_trace(LOG_INFO, 0xf06c, "PLP_trigger When DST read scan");
				return;
        	}
            //ftl_apl_trace(LOG_INFO, 0xf2b3, "DST_patrol_read_done,[PDA]=0x%x [status]=0x%x",ncl_cmd->addr_param.common_param.pda_list[i], info[i].status);

			if(info[i].status == ficu_err_par_err)
			{
				#if 1
				u32 LDA = 0;
				u8 bmp = 0;

				DST_meta_search(&LDA, &bmp, ncl_cmd, i);
				ftl_apl_trace(LOG_INFO, 0x8515, "[Chk] LDA0x%x - bmp|0x%x", LDA, bmp); 
				if(LDA > _max_capacity)
				{
					ftl_apl_trace(LOG_ERR, 0xf3aa, "[Chk] Skip this LDA 0x%x _max_capacity 0x%x", LDA, _max_capacity); 
					continue;
				}				
				#else
				io_meta_t *meta;
				extern void *shr_ddtag_meta;	
				meta = shr_ddtag_meta;

				bm_pl_t bm_pl = ncl_cmd->user_tag_list[i];

				dtag_t dtag;
				dtag.dtag = (u32)bm_pl.pl.dtag;

				u32 LDA = meta[dtag.b.dtag].lda;

				u8 bmp = meta[dtag.b.dtag].wunc.WUNC;

				ftl_apl_trace(LOG_INFO, 0x89f2, "Max chk LDA|0x%x - bmp|0x%x",LDA,bmp); 
				#endif
				pda_t PDA_result = DST_L2P_search((lda_t)LDA);

				ftl_apl_trace(LOG_INFO, 0xfbf2, "Max chk curr_PDA|0x%x - l2p_PDA|0x%x",ncl_cmd->addr_param.common_param.pda_list[i],PDA_result); 

				u8 position = ctz(bmp);
				LBA = (LDA<<3) + position;

				if(PDA_result == ncl_cmd->addr_param.common_param.pda_list[i])
				{
					//u8 position = ctz(bmp);
					//LBA = (LDA<<3) + position;
					DST_pard_fail_flag = true;
					DST_pard_fail_LBA[0] = LBA;
					ftl_apl_trace(LOG_INFO, 0x80d5, "ficu_err_par_err LBA|0x%x - PDA|0x%x - l2p_PDA|0x%x",DST_pard_fail_LBA[0],ncl_cmd->addr_param.common_param.pda_list[i],PDA_result); 
				}
			}
        }
    }
    //ftl_apl_trace(LOG_INFO, 0, "ncl_cmd 0x%x pda 0x%x %dcmd not done", ncl_cmd, pda, pard_mgr.pard_undone_cnt);
    pard_mgr.pard_undone_cnt--;

    /*the spb has patrol read done and determines whether to do gc*/
    if(pard_mgr.pard_undone_cnt == 0)
	{
		if(pard_mgr.pool_id != spb_get_poolid(pard_mgr.patrol_blk) || pard_mgr.prev_sn != spb_get_sn(pard_mgr.patrol_blk))
		{
			ftl_apl_trace(LOG_INFO, 0xdf77, "[DST]patrol read abort the spb %d", block);
			pard_mgr.patrol_times = 0;
			pard_mgr.sblk_change = true;
			//pard_mgr.do_gc = false;
            evt_set_imt(evt_DST_patrol_read,0,0);

            return;
		}
		if(pard_mgr.patrol_times == 0)
			ftl_apl_trace(LOG_PARD, 0x471e, "[DST]patrol read done spb %d",block);

		evt_set_imt(evt_DST_patrol_read,0,0);
	}
}

ddr_code void DST_pard_ncl_cmd_submit(pda_t* pda_list, enum ncl_cmd_op_t op, bm_pl_t *bm_pl, u32 count, u32 interleave_id)
{

    u32 interleave = interleave_id & (PARD_DIE_AIPR - 1);

    memset(pard_cmd_mgr[interleave].pard_info_list, 0, sizeof(struct info_param_t) * count);

    for(u8 i = 0;i < count;i++){
        pard_cmd_mgr[interleave].pard_pda[i] = pda_list[i];
        pard_cmd_mgr[interleave].pard_pl[i] = bm_pl[i];
        pard_cmd_mgr[interleave].pard_info_list[i].pb_type = NAL_PB_TYPE_XLC;
        //ftl_apl_trace(LOG_INFO, 0, "patrol read pda[%d] 0x%x",i,pda[i]);
    }
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.addr_param.common_param.info_list = pard_cmd_mgr[interleave].pard_info_list;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.addr_param.common_param.list_len = count;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.addr_param.common_param.pda_list = pard_cmd_mgr[interleave].pard_pda;

    pard_cmd_mgr[interleave].pard_read_ncl_cmd.retry_step = default_read;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.caller_priv = NULL;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.completion = DST_patrol_read_done;
#if defined(HMETA_SIZE)
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG | NCL_CMD_DIS_HCRC_FLAG;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
#else
    pard_cmd_mgr[interleave].pard_read_ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
    pard_cmd_mgr[interleave].pard_read_ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
#endif
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.op_code = op;


	pard_cmd_mgr[interleave].pard_read_ncl_cmd.user_tag_list = pard_cmd_mgr[interleave].pard_pl;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.du_format_no = DU_FMT_USER_4K_PATROL_READ;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.status = 0;
    #if OPEN_ROW
    pard_cmd_mgr[interleave].pard_read_ncl_cmd.via_sch = 0;
    #endif

	#if RAID_SUPPORT_UECC
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.uecc_type = NCL_UECC_NORMAL_RD;
	#endif

	ncl_cmd_submit_insert_schedule(&pard_cmd_mgr[interleave].pard_read_ncl_cmd, true);

    return;
}

#endif
ddr_code void patrol_read_done(struct ncl_cmd_t *ncl_cmd)
{
#ifdef History_read
    extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
    bool default_flag = (ncl_cmd->retry_step==default_read)?true:false;
#endif
    pda_t pda = *ncl_cmd->addr_param.common_param.pda_list;
    u32 cnt = ncl_cmd->addr_param.common_param.list_len;
    struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
    u32 block = pda2blk(pda);
    u32 i = 0;
    u32 nsid = spb_get_nsid(block);
    ftl_apl_trace(LOG_DEBUG, 0x0fea, "ncl_cmd 0x%x pda 0x%x %dcmd not done", ncl_cmd, pda, pard_mgr.pard_undone_cnt);

    /*mark the spb that needs to do gc*/

    if(ncl_cmd->status)
    {
        for(i = 0; i < cnt; i++)
        {
            ftl_apl_trace(LOG_INFO, 0x1bdd, "patrol_read_done,[PDA]=0x%x",ncl_cmd->addr_param.common_param.pda_list[i]); 
            if(info[i].status == ficu_err_du_uc)
            {
                pard_mgr.do_gc = true;
            }
            #ifdef History_read  
            else if(info[i].status == ficu_err_du_ovrlmt && default_flag)
            {     
            	rd_err_handling(ncl_cmd);
                return;
            }
            #endif 
        }
    }
    //ftl_apl_trace(LOG_INFO, 0, "ncl_cmd 0x%x pda 0x%x %dcmd not done", ncl_cmd, pda, pard_mgr.pard_undone_cnt);
    pard_mgr.pard_undone_cnt--;

    /*the spb has patrol read done and determines whether to do gc*/
    if(pard_mgr.pard_undone_cnt == 0){
        if(pard_mgr.pool_id != spb_get_poolid(pard_mgr.patrol_blk) || pard_mgr.prev_sn != spb_get_sn(pard_mgr.patrol_blk))
        {
            ftl_apl_trace(LOG_INFO, 0x28de, "patrol read abort the spb %d", block);
            pard_mgr.patrol_times = 0;
            pard_mgr.sblk_change = true;
            pard_mgr.do_gc = false;
            return;
        }
        if(pard_mgr.patrol_times == 0)
            ftl_apl_trace(LOG_PARD, 0x4ade, "patrol read done spb %d",block);

        if(pard_mgr.do_gc){
            ftl_apl_trace(LOG_INFO, 0x6a78, "spb%d push to gc",block);
            pard_mgr.patrol_times = 0;
            spb_set_flag(block, (SPB_DESC_F_PATOAL_RD|SPB_DESC_F_NO_NEED_CLOSE));
            ftl_ns_weak_spb_gc(nsid, block);
            pard_mgr.do_gc = false;
        }

    }
}

ddr_code void pard_ncl_cmd_submit(pda_t* pda_list, enum ncl_cmd_op_t op, bm_pl_t *bm_pl, u32 count, u32 interleave_id)
{

    u32 interleave = interleave_id & (PARD_DIE_AIPR - 1);

    memset(pard_cmd_mgr[interleave].pard_info_list, 0, sizeof(struct info_param_t) * count);

    for(u8 i = 0;i < count;i++){
        pard_cmd_mgr[interleave].pard_pda[i] = pda_list[i];
        pard_cmd_mgr[interleave].pard_pl[i] = bm_pl[i];
        pard_cmd_mgr[interleave].pard_info_list[i].pb_type = NAL_PB_TYPE_XLC;
        //ftl_apl_trace(LOG_INFO, 0, "patrol read pda[%d] 0x%x",i,pda[i]);
    }
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.addr_param.common_param.info_list = pard_cmd_mgr[interleave].pard_info_list;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.addr_param.common_param.list_len = count;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.addr_param.common_param.pda_list = pard_cmd_mgr[interleave].pard_pda;

    pard_cmd_mgr[interleave].pard_read_ncl_cmd.retry_step = default_read;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.caller_priv = NULL;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.completion = patrol_read_done;
#if defined(HMETA_SIZE)
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG | NCL_CMD_DIS_HCRC_FLAG;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
#else
    pard_cmd_mgr[interleave].pard_read_ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
    pard_cmd_mgr[interleave].pard_read_ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
#endif
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.op_code = op;


	pard_cmd_mgr[interleave].pard_read_ncl_cmd.user_tag_list = pard_cmd_mgr[interleave].pard_pl;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.du_format_no = DU_FMT_USER_4K_PATROL_READ;
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.status = 0;
    #if OPEN_ROW
    pard_cmd_mgr[interleave].pard_read_ncl_cmd.via_sch = 0;
    #endif

	#if RAID_SUPPORT_UECC
	pard_cmd_mgr[interleave].pard_read_ncl_cmd.uecc_type = NCL_UECC_NORMAL_RD;
	#endif

	ncl_cmd_submit_insert_schedule(&pard_cmd_mgr[interleave].pard_read_ncl_cmd, true);

    return;
}

ddr_code pda_t pard_get_pda(u16 block, u32 interleave, u8 page_shift, u32 wl)
{
    pda_t pda;
    pda = block << shr_nand_info.pda_block_shift;
    pda |= ((wl * XLC) + page_shift) << shr_nand_info.pda_page_shift;
    pda |= interleave << shr_nand_info.pda_plane_shift;
    return pda;
}

 ddr_code bool pard_get_spb(pard_mgr_t* pard_mgr)
{
    /*take a spb with the smallest SN*/

    if(pard_mgr->patrol_times == 0){
        /*pool id or sn of sblk has changed*/
        if(pard_mgr->sblk_change)
        {
            pard_mgr->patrol_blk = find_min_sn_spb(pard_mgr->prev_sn);
        }
        else
        {
			if(pard_mgr->prev_blk == INV_U16)
			{
				pard_mgr->patrol_blk = spb_mgr_get_head(SPB_POOL_USER);
			}
			else if((spb_get_poolid(pard_mgr->prev_blk) == SPB_POOL_USER) && (spb_get_sn(pard_mgr->prev_blk) == pard_mgr->prev_sn))
			{
				pard_mgr->patrol_blk = spb_mgr_get_next_spb(pard_mgr->prev_blk);
			}
			else
			{
				pard_mgr->patrol_blk = find_min_sn_spb(pard_mgr->prev_sn);
			}
        }

		u32 sn =  spb_get_sn( pard_mgr->patrol_blk);
        ftl_apl_trace(LOG_DEBUG, 0x55e3, "sn %d prev_sn %d max_sn %d spb %d", sn, pard_mgr->prev_sn, pard_mgr->pwron_max_sn, pard_mgr->patrol_blk);

		if((sn > pard_mgr->pwron_max_sn) || (pard_mgr->patrol_blk == INV_U16))
		{
			pard_mgr->prev_sn = pard_mgr->pwron_max_sn;
			ftl_apl_trace(LOG_INFO, 0x2323, "patrol read stop");
            return false;
		}

        /*
        if(pard_mgr->patrol_blk == INV_U16){
            pard_mgr->prev_sn = 0;
            return false;
        }
		*/
		if(sn <= pard_mgr->prev_sn){

            u8 prev_pool_id = spb_get_poolid(pard_mgr->prev_blk);
            u8 pool_id = spb_get_poolid(pard_mgr->patrol_blk);
            ftl_apl_trace(LOG_INFO, 0x69bf, "patrol_blk %d pool_id %d, prev_blk %d pool_id %d", pard_mgr->patrol_blk, pool_id, pard_mgr->prev_blk, prev_pool_id);
            if(spb_info_tbl[pard_mgr->prev_blk].block != pard_mgr->patrol_blk){

                //ftl_apl_trace(LOG_INFO, 0, "spb change");
                pard_mgr->patrol_blk = find_min_sn_spb(pard_mgr->prev_sn);
                sn =  spb_get_sn( pard_mgr->patrol_blk);
            }
        }
        //sys_assert(sn >= pard_mgr->prev_sn);
        pard_mgr->prev_blk = pard_mgr->patrol_blk;
        pard_mgr->sblk_change = false;
        pard_mgr->pool_id = spb_get_poolid(pard_mgr->patrol_blk);
        pard_mgr->prev_sn = spb_get_sn(pard_mgr->patrol_blk);
        //ftl_apl_trace(LOG_INFO, 0, "patrol read spb%d",pard_mgr->patrol_blk);
    }
    return true;
}

#if CO_SUPPORT_DEVICE_SELF_TEST

ddr_code bool DST_pard_get_spb(pard_mgr_t* pard_mgr)
{
    /*take a spb with the smallest SN*/
	if(pard_mgr->patrol_times == 0 )
	{

		//DST_wl_index = DST_wl_index%(shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);

		ftl_apl_trace(LOG_DEBUG, 0x60c6, "[DST]DST_wl_index|%d",DST_wl_index);

		if(DST_wl_index == 0)
		{		
			ftl_apl_trace(LOG_DEBUG, 0x6581, "[DST]patrol_blk|%d prev_blk|%d open_blk_find|%d",pard_mgr->patrol_blk ,pard_mgr->prev_blk, open_blk_find);
			/*
			if((pard_mgr->patrol_blk == pard_mgr->prev_blk) && (!open_blk_find))
			{
				ftl_apl_trace(LOG_INFO, 0xcc0f, "[DST]Finding open blk ...");

				pard_mgr->patrol_blk = ps_open[FTL_CORE_NRM];

				if(pard_mgr->patrol_blk == INV_U16)
				{							
					ftl_apl_trace(LOG_INFO, 0x0175, "[DST]blk not found |%d",pard_mgr->patrol_blk);
					goto handle_read_stop; 
				}
				else    
				{
					open_blk_find = true;
					ftl_apl_trace(LOG_INFO, 0x1985, "[DST]open blk find|%d",pard_mgr->patrol_blk);
					goto handle_read_stop; 
				}
			}
			*/
			/*pool id or sn of sblk has changed*/
			if(pard_mgr->sblk_change)
	        {
	            pard_mgr->patrol_blk = find_min_sn_spb(pard_mgr->prev_sn);
				ftl_apl_trace(LOG_DEBUG, 0xa1aa, "[DST]Max chk 1 patrol_blk|%d",pard_mgr->patrol_blk);
			}
	        else
	        {
				if(pard_mgr->prev_blk == INV_U16)
				{					
					pard_mgr->patrol_blk = spb_mgr_get_head(SPB_POOL_USER);

					if(pard_mgr->patrol_blk == INV_U16)
					{
						goto Finding_open_blk;
						/*
						pard_mgr->patrol_blk = ps_open[FTL_CORE_NRM];

						if(pard_mgr->patrol_blk == INV_U16)
						{							
							ftl_apl_trace(LOG_INFO, 0xbbce, "[DST]blk not found |%d",pard_mgr->patrol_blk);
							goto handle_read_stop; 
						}
						else    
						{
							open_blk_find = true;
							ftl_apl_trace(LOG_INFO, 0xd7e1, "[DST]User blk not found -> Finding open blk|%d",pard_mgr->patrol_blk);
							goto handle_read_stop; 
						}
						*/
					}
					else
					{
						open_blk_find = false;
						ftl_apl_trace(LOG_INFO, 0x55ef, "[DST]User blk found |%d",pard_mgr->patrol_blk);
					}
				}
				else if((spb_get_poolid(pard_mgr->prev_blk) == SPB_POOL_USER) && (spb_get_sn(pard_mgr->prev_blk) == pard_mgr->prev_sn))
				{
					pard_mgr->patrol_blk = spb_mgr_get_next_spb(pard_mgr->prev_blk);
					//open_blk_find = false;
					ftl_apl_trace(LOG_DEBUG, 0x1887, "[DST]Max chk 3 patrol_blk|%d",pard_mgr->patrol_blk);

					if((!open_blk_find) && (pard_mgr->patrol_blk == INV_U16))
					{
Finding_open_blk:
						ftl_apl_trace(LOG_DEBUG, 0xcc0f, "[DST]Finding open blk ...");

						pard_mgr->patrol_blk = ps_open[FTL_CORE_NRM];

						if(pard_mgr->patrol_blk == INV_U16)
						{							
							ftl_apl_trace(LOG_DEBUG, 0x0175, "[DST]blk not found |%d",pard_mgr->patrol_blk);
							goto handle_read_stop; 
						}
						else    
						{
							open_blk_find = true;
							ftl_apl_trace(LOG_DEBUG, 0x1985, "[DST]open blk find|%d",pard_mgr->patrol_blk);
							goto handle_read_stop; 
						}
					}
				}
				else if(open_blk_find == true)
				{		
					open_blk_find = false;
					pard_mgr->patrol_blk = spb_mgr_get_next_spb(pard_mgr->prev_blk);		
					ftl_apl_trace(LOG_DEBUG, 0x95dc, "[DST]Max chk 3.5 patrol_blk|%d",pard_mgr->patrol_blk);
				}
				else
				{
				u16 tmp_poolid = spb_get_poolid(pard_mgr->prev_blk);
				u16 tmp_sn = spb_get_sn(pard_mgr->prev_blk); 

					ftl_apl_trace(LOG_DEBUG, 0x0ed2, "[DST]tmp_poolid|%x tmp_sn|%x prev_sn|%x",tmp_poolid,tmp_sn,pard_mgr->prev_sn);
					pard_mgr->patrol_blk = find_min_sn_spb(pard_mgr->prev_sn);
					ftl_apl_trace(LOG_DEBUG, 0x36e3, "[DST]Max chk 4 patrol_blk|%d",pard_mgr->patrol_blk);
				}
	        }

			u32 sn = 0;

handle_read_stop:

			sn = spb_get_sn(pard_mgr->patrol_blk);
	        ftl_apl_trace(LOG_INFO, 0xb249, "sn %d prev_sn %d max_sn %d spb %d", sn, pard_mgr->prev_sn, pard_mgr->pwron_max_sn, pard_mgr->patrol_blk);

			if(((sn > pard_mgr->pwron_max_sn) || (pard_mgr->patrol_blk == INV_U16)) && (!open_blk_find))
			{
				pard_mgr->prev_sn = pard_mgr->pwron_max_sn;
				ftl_apl_trace(LOG_INFO, 0x2773, "patrol read stop");
	            return false;
			}

	        /*
	        if(pard_mgr->patrol_blk == INV_U16){
	            pard_mgr->prev_sn = 0;
	            return false;
	        }
			*/
			if(sn <= pard_mgr->prev_sn){

	            //u8 prev_pool_id = spb_get_poolid(pard_mgr->prev_blk);
	            //u8 pool_id = spb_get_poolid(pard_mgr->patrol_blk);
	            //ftl_apl_trace(LOG_INFO, 0, "patrol_blk %d pool_id %d, prev_blk %d pool_id %d", pard_mgr->patrol_blk, pool_id, pard_mgr->prev_blk, prev_pool_id);
	            if(spb_info_tbl[pard_mgr->prev_blk].block != pard_mgr->patrol_blk){				
	                //ftl_apl_trace(LOG_INFO, 0, "spb change");
	                pard_mgr->patrol_blk = find_min_sn_spb(pard_mgr->prev_sn);
	                sn =  spb_get_sn( pard_mgr->patrol_blk);
	            }
	        }
	        //sys_assert(sn >= pard_mgr->prev_sn);
	        pard_mgr->prev_blk = pard_mgr->patrol_blk;
	        pard_mgr->sblk_change = false;
	        pard_mgr->pool_id = spb_get_poolid(pard_mgr->patrol_blk);
	        pard_mgr->prev_sn = spb_get_sn(pard_mgr->patrol_blk);
		}


	}

	return true;
}

#endif

#ifdef SKIP_MODE

share_data_zi u8* gl_pt_defect_tbl;

ddr_code u8* get_spb_defect(u32 spb_id)
{
	//u32 interleave = get_interleave();
	u32 df_width = occupied_by(shr_nand_info.interleave, 8) << 3;
	u32 index = (spb_id * df_width) >> 3;
	return (gl_pt_defect_tbl + index);
}

ddr_code u8 get_defect_pl_pair(u8* ftl_df, u32 interleave)
{
	//(bitmap[index >> 5] & (1 << (index & 0x1f)));
	if(interleave%shr_nand_info.geo.nr_planes!=0) panic("interleave not pl 0\n");
	u32 idx = interleave >> 3;
	u32 off = interleave & (7);
	if (shr_nand_info.geo.nr_planes == 4)
		return (((ftl_df[idx] >> off)&0xf));
	else
		return (((ftl_df[idx] >> off)&0x3));
}

#endif
extern volatile bool FTL_INIT_DONE;
extern bool _fg_fwupgrade_stopPTRD;
extern volatile bool pard_flag;

#if CO_SUPPORT_DEVICE_SELF_TEST

ddr_code void DST_patrol_read_start(u32 param0, u32 param1, u32 param2)
{
	DST_patrol_read(NULL);
}

ddr_code void DST_patrol_read(void *data)
{

//ftl_apl_trace(LOG_INFO, 0x8a3f, "DST_patrol_read_1 WL_index|%d",DST_wl_index);

#if 1 //STOP_PRRD_WARMBOOT-20210302-Eddie	20210426-Eddie-mod
    if(misc_is_STOP_BGREAD()){
		del_timer(&patrol_read_timer);
		ftl_apl_trace(LOG_INFO, 0x88eb, "WARM BOOT STOP patrol read");
		return;
   	}
#endif

    if(shr_shutdownflag || plp_trigger || hostReset || gFormatFlag || _fg_fwupgrade_stopPTRD || DST_abort_flag || DST_pard_fail_flag || !gCurrDSTOperationImmed){
		if(delay_flush_spor_qbt)
			mod_timer(&DST_patrol_read_timer, jiffies + HZ);
		else
        	ftl_apl_trace(LOG_INFO, 0xece4, "patrol read stop special flag");
        return;
    }

    if(!all_init_done || pard_mgr.pard_undone_cnt || pard_switch || !FTL_INIT_DONE){
    //if(!all_init_done || pard_switch || !FTL_INIT_DONE){
		mod_timer(&DST_patrol_read_timer, jiffies+20);
        ftl_apl_trace(LOG_INFO, 0xccf8, "not ready, pard_done cnt %d",pard_mgr.pard_undone_cnt);
        return;
    }

    if(!DST_pard_get_spb(&pard_mgr)){
        //mod_timer(&patrol_read_timer, jiffies+PARD_READ_CYCLE);
        ftl_apl_trace(LOG_INFO, 0x6d21, "[DST] patrol read success");
        DST_pard_done_flag = true;
        ftl_apl_trace(LOG_INFO, 0x0dcb, "[DST] no block to read");
        return;
    }

    u32 flag = spb_get_flag(pard_mgr.patrol_blk);
    if(((flag & (SPB_DESC_F_OPEN | SPB_DESC_F_BUSY)) != 0) && ((flag & SPB_DESC_F_CLOSED) == 0))
	{
        //ftl_apl_trace(LOG_INFO, 0x4d73, "blank block flag 0x%x, skip it", flag);
        //mod_timer(&DST_patrol_read_timer, jiffies+PARD_READ_CYCLE);
        //return;
    }
	//ftl_apl_trace(LOG_INFO, 0x8bf5, "DST_patrol_read_2 WL_index|%d",DST_wl_index);

	//for(u32 wl_index = 0; wl_index < shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell ; wl_index++){

    	u32 interleave = 0;
    	u32 dtag_cnt = 0;
    	bm_pl_t pl[XLC];
    	pda_t pda[XLC];
    	u32 basedtag = shr_pa_rd_ddtag_start;

		//ftl_apl_trace(LOG_INFO, 0x9522, "DST_wl_index|%d total wl|%d", DST_wl_index, shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);

    	/*configure payload*/
		u16 max_interleave = (pard_mgr.patrol_times + 1) * min(shr_nand_info.interleave, PARD_DIE_AIPR);
    	for(interleave = pard_mgr.patrol_times * PARD_DIE_AIPR; interleave < max_interleave; interleave++)
		{
    	//for(interleave = pard_table[pard_mgr.patrol_times].w.l; interleave < pard_table[pard_mgr.patrol_times].w.h; interleave++){
        	//ftl_apl_trace(LOG_DEBUG, 0, "patrol read die_id %d spb_id %d ",interleave>>1, pard_mgr.patrol_blk);
        	#ifdef SKIP_MODE
        	u8* ftl_df_ptr = get_spb_defect(pard_mgr.patrol_blk);
        	u8 pln_pair = get_defect_pl_pair(ftl_df_ptr, interleave&(~(shr_nand_info.geo.nr_planes - 1)));
        	if(pln_pair & (1 << (interleave & (shr_nand_info.geo.nr_planes - 1)))){
             //ftl_apl_trace(LOG_INFO, 0, "patrol read skip defect spb %d interleave %d ", pard_mgr.patrol_blk, interleave);
            	continue;
        	}
        	#endif
        	if(shr_shutdownflag || plp_trigger || hostReset || gFormatFlag){
            	ftl_apl_trace(LOG_INFO, 0x017e, "patrol read stop");
            	return;
        	}

	        for(u8 page = 0; page < XLC; page++){
	           	pda[page] = pard_get_pda(pard_mgr.patrol_blk, interleave, page, DST_wl_index);
	           	//ftl_apl_trace(LOG_INFO, 0x4c52, "patrol read pda[%d] 0x%x", page, pda[page]);
	           	pl[page].pl.dtag = (DTAG_IN_DDR_MASK | (basedtag + dtag_cnt));
	           	pl[page].pl.du_ofst = 0;
	           	pl[page].pl.btag = 0;
	           	pl[page].pl.nvm_cmd_id = 0;
	           	pl[page].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_DTAG;
	           	dtag_cnt++;
	        }
			pard_mgr.pard_undone_cnt++;
	        DST_pard_ncl_cmd_submit(pda, NCL_CMD_PATROL_READ, pl, XLC, interleave);

		}

		DST_wl_index++;

		if(DST_wl_index == shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell - 1)	
		{
			DST_wl_index = 0;
			pard_mgr.patrol_times++;
		}

		if(pard_mgr.patrol_times == patrol_time)
        	pard_mgr.patrol_times = 0;

		//evt_set_imt(evt_DST_patrol_read,0,0);
    	//mod_timer(&DST_patrol_read_timer, jiffies);
	//}

}

#endif


ddr_code void save_log_for_plp_not_done(void *data)
{
	flush_to_nand(EVT_PLP_NOT_DONE);
}

ddr_code void patrol_read(void *data)
{
#if 1 //STOP_PRRD_WARMBOOT-20210302-Eddie	20210426-Eddie-mod
    if(misc_is_STOP_BGREAD()){
		del_timer(&patrol_read_timer);
		ftl_apl_trace(LOG_INFO, 0x95bf, "WARM BOOT STOP patrol read");
		return;
   	}
#endif

    if(shr_shutdownflag || plp_trigger || hostReset || gFormatFlag || _fg_fwupgrade_stopPTRD){
		if(delay_flush_spor_qbt)
			mod_timer(&patrol_read_timer, jiffies + HZ);
		else
        	ftl_apl_trace(LOG_INFO, 0x7985, "patrol read stop special flag");
        return;
    }

    if(!all_init_done || pard_mgr.pard_undone_cnt || pard_switch || !FTL_INIT_DONE){
        mod_timer(&patrol_read_timer, jiffies+20);
        ftl_apl_trace(LOG_INFO, 0xbad4, "not ready, pard_done cnt %d",pard_mgr.pard_undone_cnt);
        return;
    }

    if(!pard_get_spb(&pard_mgr)){
        //mod_timer(&patrol_read_timer, jiffies+PARD_READ_CYCLE);
        ftl_apl_trace(LOG_INFO, 0xa4df, "patrol read success");
        pard_flag = true;
        ftl_apl_trace(LOG_INFO, 0x6277, "no block to read");
        return;
    }

    u32 flag = spb_get_flag(pard_mgr.patrol_blk);
    if(((flag & (SPB_DESC_F_OPEN | SPB_DESC_F_BUSY)) != 0) && ((flag & SPB_DESC_F_CLOSED) == 0))
    {
        ftl_apl_trace(LOG_INFO, 0xa9cd, "blank block flag 0x%x, skip it", flag);
        mod_timer(&patrol_read_timer, jiffies+PARD_READ_CYCLE);
        return;
    }

    u32 interleave = 0;
    u32 dtag_cnt = 0;
    bm_pl_t pl[XLC];
    pda_t pda[XLC];
    u32 basedtag = shr_pa_rd_ddtag_start;

    /*configure payload*/
    for(interleave = pard_table[pard_mgr.patrol_times].w.l; interleave < pard_table[pard_mgr.patrol_times].w.h; interleave++){
        //ftl_apl_trace(LOG_DEBUG, 0, "patrol read die_id %d spb_id %d ",interleave>>1, pard_mgr.patrol_blk);
        #ifdef SKIP_MODE
        u8* ftl_df_ptr = get_spb_defect(pard_mgr.patrol_blk);
        u8 pln_pair = get_defect_pl_pair(ftl_df_ptr, interleave&(~(shr_nand_info.geo.nr_planes - 1)));
        if(pln_pair & (1 << (interleave & (shr_nand_info.geo.nr_planes - 1)))){
             //ftl_apl_trace(LOG_INFO, 0, "patrol read skip defect spb %d interleave %d ", pard_mgr.patrol_blk, interleave);
            continue;
        }
        #endif
        if(shr_shutdownflag || plp_trigger || hostReset || gFormatFlag){
            ftl_apl_trace(LOG_INFO, 0x6233, "patrol read stop");
            return;
        }
        for(u8 page = 0; page < XLC; page++){
            pda[page] = pard_get_pda(pard_mgr.patrol_blk, interleave, page, WORST_WL);
            //ftl_apl_trace(LOG_DEBUG, 0, "patrol read pda[%d] 0x%x", page, pda[page]);
            pl[page].pl.dtag = (DTAG_IN_DDR_MASK | (basedtag + dtag_cnt));
            pl[page].pl.du_ofst = 0;
            pl[page].pl.btag = 0;
            pl[page].pl.nvm_cmd_id = 0;
            pl[page].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_DTAG;
            dtag_cnt++;
        }
        pard_mgr.pard_undone_cnt++;
        pard_ncl_cmd_submit(pda, NCL_CMD_PATROL_READ, pl, XLC, interleave);
    }

    pard_mgr.patrol_times++;
    if(pard_mgr.patrol_times == patrol_time)
        pard_mgr.patrol_times = 0;

    mod_timer(&patrol_read_timer, jiffies+PARD_READ_CYCLE);
}

ddr_code void ipc_call_patrol(volatile cpu_msg_req_t *req)
{
	pard_mgr.pwron_max_sn = spb_get_sn(spb_mgr_get_tail(SPB_POOL_USER));
	mod_timer(&patrol_read_timer, jiffies+1*HZ / 10);
}

#if CO_SUPPORT_DEVICE_SELF_TEST

ddr_code void ipc_call_DST_patrol(volatile cpu_msg_req_t *req)
{
	pard_mgr_init(&pard_mgr);
	DST_wl_index = 0;
	pard_mgr.pwron_max_sn = spb_get_sn(spb_mgr_get_tail(SPB_POOL_USER));
	mod_timer(&DST_patrol_read_timer, jiffies+1*HZ / 10);

}
#endif

#endif


init_code void ftl_api_init(void)
{
	ipc_api_init();

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
    cpu_msg_register(CPU_MSG_GET_ERRHANDLEVAR_CPU_FTL, __get_telemetry_ErrHandleVar_CPU_FTL);
#endif

    cpu_msg_register(CPU_MSG_FW_ARD_HANDLER, __fw_ard_handler);
	cpu_msg_register(CPU_MSG_COMPARE_VAC, ipc_compare_vac);
	cpu_msg_register(CPU_MSG_FC_CTX_DONE, ipc_ftl_core_ctx_done);
	cpu_msg_register(CPU_MSG_NS_OPEN, ipc_ftl_open);
	cpu_msg_register(CPU_MSG_SPB_QUERY, ipc_spb_query);
	cpu_msg_register(CPU_MSG_GC_DONE, ipc_spb_gc_done);
	cpu_msg_register(CPU_MSG_FTL_FLUSH, ipc_ftl_flush);
	cpu_msg_register(CPU_MSG_FTL_FULL_TRIM, ipc_ftl_full_trim);
	cpu_msg_register(CPU_MSG_FTL_FORMAT, ipc_ftl_format);
	cpu_msg_register(CPU_MSG_GC_ACT, ipc_gc_action);
	cpu_msg_register(CPU_MSG_DEL_NS_FTL_HANDLE, ipc_ftl_del_ns);
#if GC_SUSPEND_FWDL
	cpu_msg_register(CPU_MSG_GC_HANDLE_FWDL, ipc_fwdl_gc_handle);	//< gc hamdle in FWDL 20210308-Eddie
#endif
#if PATROL_READ
	cpu_msg_register(CPU_MSG_PATROL, ipc_call_patrol);
#endif

#if CO_SUPPORT_DEVICE_SELF_TEST
	cpu_msg_register(CPU_MSG_DST_PATROL, ipc_call_DST_patrol);
#endif

	cpu_msg_register(CPU_MSG_FCORE_GC_ACT_DONE, ipc_fcore_gc_action_done);
	cpu_msg_register(CPU_MSG_SPB_WEAK_RETIRE, ipc_spb_weak_retire);
	cpu_msg_register(CPU_MSG_SPB_RD_CNT_UPDT, ipc_spb_rd_cnt_updt);
	cpu_msg_register(CPU_MSG_FE_REQ_OP, ipc_fe_req_op);
	cpu_msg_register(CPU_MSG_GET_SPARE_AVG_ERASE_CNT, ipc_get_spare_avg_erase_cnt);
	cpu_msg_register(CPU_MSG_GET_ADDITIONAL_SMART_INFO, ipc_get_additional_smart_info);
	cpu_msg_register(CPU_MSG_L2P_LOAD, ipc_l2p_load);
	//cpu_msg_register(CPU_MSG_FRB_UPDATE_QBTINFO, ipc_ftl_update_frb_info);
	cpu_msg_register(CPU_MSG_OPEN_QBT_DONE, ipc_qbt_alloc_done);
	cpu_msg_register(CPU_MSG_GET_CPU3_TELEMETRY_CTRLR_DATA, ipc_get_telemetry_ctrlr_from_U3);

#if(WARMBOOT_FTL_HANDLE == mENABLE)
	cpu_msg_register(CPU_MSG_WARM_BOOT_FTL_RESET, ipc_warm_boot_ftl_reset);
#endif

	cpu_msg_register(CPU_MSG_L2P_RESET, ipc_l2p_partial_reset);
// #if CO_SUPPORT_DEVICE_SELF_TEST
	//cpu_msg_register(CPU_MSG_DT_DELAY, ipc_self_device_test_delay);
// #endifc

	cpu_msg_register(CPU_MSG_GC_STOP, ipc_gc_stop);
	cpu_msg_register(CPU_MSG_GC_RESET,ipc_gc_reset);
	cpu_msg_register(CPU_MSG_FLUSH_BLKLIST, ipc_flush_blklist);
	cpu_msg_register(CPU_MSG_FREE_BLKLIST, ipc_free_blklist);
	cpu_msg_register(CPU_MSG_PSTREAM_ACK, ipc_pstream_ack);
	cpu_msg_register(CPU_MSG_FREE_FLUSH_BLKLIST_SDTAG, ipc_free_flush_blklist_dtag);
	cpu_msg_register(CPU_MSG_RELEASE_SPB, ipc_release_spb);
	cpu_msg_register(CPU_MSG_RELEASE_PBT_SPB, ipc_release_pbt_spb);

    cpu_msg_register(CPU_MSG_CLEAN_EC_TBL, ipc_spb_clear_ec);
    #if 0//(TRIM_SUPPORT == DISABLE)
	cpu_msg_register(CPU_MSG_UCACHE_SUSP_TRIM_DONE, ipc_ucache_suspend_trim_done);
    #endif
	cpu_msg_register(CPU_MSG_FC_READY_DONE, ftl_check_fc_done);
	cpu_msg_register(CPU_MSG_GET_NEW_EC, ipc_get_new_ec);
	#ifdef ERRHANDLE_GLIST

    cpu_msg_register(CPU_MSG_SET_ERR_TASK, __ErrHandle_Task);
	cpu_msg_register(CPU_MSG_CLR_GLIST, ipc_eh_clear_glist);    // Paul_20210105
	cpu_msg_register(CPU_MSG_DUMP_GLIST, ipc_eh_dump_glist);	// FET, DumpGLLog

    cpu_msg_register(CPU_MSG_SCAN_WRITTEN, Scan_All_BLK_4_Nand_Written); //20210303 Niklaus
	cpu_msg_register(CPU_MSG_SCAN_GLIST_TRG_HANDLE, __eh_scan_glist_trig_handle);
	cpu_msg_register(CPU_MSG_PERST_FTL_NOTIFY, ipc_ftl_reset_notify);
	cpu_msg_register(CPU_MSG_PERST_DONE_FTL_NOTIFY, ipc_ftl_reset_done_notify);
	cpu_msg_register(CPU_MSG_ONDEMAND_DUMP, ipc_ondeman_dump);
	cpu_msg_register(CPU_MSG_LOOP_FEEDBACK, ipc_feedback_set);

	cpu_msg_register(CPU_MSG_FRB_DROP_P2GL, ipc_frb_drop_P2GL);	// FET, RelsP2AndGL

	#endif
#if 0 //RAID_SUPPORT_UECC
    cpu_msg_register(CPU_MSG_UECC_FILL_SMART_INFO, ipc_fill_smart_uecc_info);
#endif
#if defined(SAVE_CDUMP)
        cpu_msg_register(CPU_MSG_ULINK, ipc_ulink_da_mode3);
#endif
	cpu_msg_register(CPU_MSG_RAID_CORRECT_DONE, _ipc_raid_correct_done);


	cpu_msg_register(CPU_MSG_CLEAR_API, ipc_clear_api);
	cpu_msg_register(CPU_MSG_CHK_GC, ipc_gc_chk);

#if 1//PLP_SUPPORT == 0
	cpu_msg_register(CPU_MSG_UPDATE_SPARE_AVG_ERASE_CNT, ipc_update_smart_avg_erase_cnt); //smart update and flush 10mins
#endif
}

static init_code void l2p_updt_init(void)
{
	CBF_INIT(&l2p_updt_que);
	CBF_INIT(&l2p_updt_ntf_que);
	l2p_updt_recv_hook((cbf_t *) &l2p_updt_ntf_que, host_pda_updt);

	u32 size = sizeof(ftl_updt_pda_t) * (RDISK_NCL_W_REQ_CNT);
	ftl_updt_pda = sys_malloc_aligned(FAST_DATA, size, 8);
}

/*!
 * @brief default fe namespace info inititlization function
 *
 * @param desc		system log descriptor
 *
 * @return		not used
 */
init_code void fe_ns_info_desc_init(sys_log_desc_t *desc)
{
	u32 sz = desc->element_cnt * desc->element_size;

	memset(desc->ptr, 0, sz);
}
/*
init_code static void ftl_update_cfg(void)
{
	ftl_cfg_t *cfg;

	cfg = fw_config_get_ftl();

	if (cfg) {
		if (cfg->version >= ftl_setting.version) {
			// fw has lower or equal version
			memcpy(&ftl_setting, cfg, sizeof(ftl_setting));
			ftl_apl_trace(LOG_ALW, 0, "apply CFG ver %d", cfg->version);
		} else {
			memcpy(&ftl_setting, cfg, offsetof(ftl_cfg_t, reserved));
			ftl_apl_trace(LOG_ALW, 0, "apply part of CFG ver %d < %d", cfg->version, ftl_setting.version);
		}
	} else {
		ftl_apl_trace(LOG_ALW, 0, "no CFG, default using");
	}
}
*/

#if (UART_L2P_SEARCH == 1)
extern u32 l2pSrchUart;
#endif
/*!
 * @brief ftl initial
 * Initialize of ftl layer
 *
 * @return	not used
 */
init_code static void cal_res_blk_for_smart(void)
{
	extern volatile u32 resver_blk;
	//u32 ftl_srb_blk = 0;

	/*if(shr_ftl_smart.model == 3840)
	{
		ftl_srb_blk = 3 + 1;
	}
	else if(shr_ftl_smart.model == 7680)
	{
		ftl_srb_blk = 4 * 2 + 1;
	}*/
    u32 ftl_blk = 2 + 1;    //2:PBT blk num, 1:QBT blk num

	// ISU, EHSmartNorValAdj
	if (shr_ftl_smart.good_phy_spb < CAP_NEED_PHYBLK_CNT + ftl_blk * shr_nand_info.interleave)
		resver_blk = 0;
	else
		resver_blk = shr_ftl_smart.good_phy_spb - CAP_NEED_PHYBLK_CNT - ftl_blk * shr_nand_info.interleave;

	ftl_apl_trace(LOG_ALW, 0x6592, "[FTL] resver_blk cnt 0x%x, shr_ftl_smart.good_phy_spb %d", resver_blk, shr_ftl_smart.good_phy_spb);
}

#ifdef RD_FAIL_GET_LDA
init_code void rd_err_get_lda_init(void)
{
    rd_err_lda_entry_0 = (rd_get_lda_t*)ddtag2mem(ddr_dtag_register(1));
    memset(rd_err_lda_entry_0, 0, DTAG_SZE);
    rd_err_lda_entry_1 = &rd_err_lda_entry_0[(DTAG_SZE/sizeof(rd_get_lda_t))/2];
    rd_err_lda_entry_ua = &rd_err_lda_entry_0[(DTAG_SZE/sizeof(rd_get_lda_t))-1];  //the last one
    //rd_err_lda_entry_1 = (rd_get_lda_t*)ddtag2mem(ddr_dtag_register(1));
    //memset(rd_err_lda_entry_1, 0, DTAG_SZE);
    rd_err_lda_ptr_0 = 0;
    rd_err_lda_ptr_1 = 0;
}
#endif

init_code static void ftl_init(void)
{
	srb_t *srb = (srb_t *) SRAM_BASE;
	u32 spb_id0;
	//u32 spb_id1;
#if(SPB_BLKLIST == mDISABLE)
	u32 min_spb_cnt;
	u32 max_spb_cnt;
	bool pure_slc = false;
	bool pure_xlc = true;
#endif
    epm_FTL_t* epm_ftl_data;
    u64 init_time_start = get_tsc_64();

	bool isSave = false;

	///ftl_update_cfg();

	shr_ftl_flags.all = 0;

	wait_remote_item_done(ddr_init);

#ifdef OTF_MEASURE_TIME
	global_time_start[2] = get_tsc_64();	//NCL init
#endif
	wait_remote_item_done(nand_geo_init);
#ifdef OTF_MEASURE_TIME
	ftl_apl_trace(LOG_ERR, 0x35d2, "[M_T]nand_geo_init %d us", time_elapsed_in_us(global_time_start[2]));
#endif
	//l2p para init must before ftl_core_int
	ftl_l2p_para_init();
	local_item_done(l2p_para_init);
#ifdef OTF_MEASURE_TIME
	global_time_start[3] = get_tsc_64();	//BE init
#endif
	wait_remote_item_done(be_init);
#ifdef OTF_MEASURE_TIME
	ftl_apl_trace(LOG_ERR, 0xc8c8, "[M_T]BE INIT %d us", time_elapsed_in_us(global_time_start[3]));
#endif

#ifdef OTF_MEASURE_TIME
	global_time_start[4] = get_tsc_64();	//FTL init
#endif

	//evt_register(ftl_start, 0, &evt_ftl_start);
	sram_dummy_meta = idx_meta_allocate(DU_CNT_PER_PAGE, SRAM_IDX_META, &sram_dummy_meta_idx);
	ddr_dummy_meta = idx_meta_allocate(DU_CNT_PER_PAGE, DDR_IDX_META, &ddr_dummy_meta_idx);

	evt_register(host_l2p_pda_updt_done, 0, &evt_l2p_updt1);
	evt_register(fake_ipc_gc_chk, 0, &evt_ftl_gc_check);
    #if RECYCLE_QUE_CPU_ID == CPU_ID
    extern  void host_l2p_dtag_recycle(u32 r0, u32 r1, u32 r2);
    evt_register(host_l2p_dtag_recycle,0,&evt_l2p_recycle_que);
    #endif
	ftl_api_init();
	ftl_flash_geo_init();
	ftl_io_buf_init();
	bg_task_init();

	frb_log_res_init();
	sys_log_init();
	gc_init();
	sys_assert(srb->srb_hdr.srb_signature == SRB_SIGNATURE);

	u32 blk0 = rda_2_pb(srb->ftlb_pri_pos, &spb_id0);
	//u32 blk1 = rda_2_pb(srb->ftlb_sec_pos, &spb_id1);
	ftl_apl_trace(LOG_ERR, 0x0d9c, "blk0 %d", blk0);
	//sys_assert(spb_id0 == spb_id1);

	//frb_log_init(blk0, blk1, spb_id0);
	frb_log_init(blk0, spb_id0);

	bool frb_valid = frb_log_start();
	//bool frb_valid = false;
	ftl_init_defect(&frb_valid);

    host_du_fmt = DU_FMT_USER_4K; // jamie 20210408
#if defined(HMETA_SIZE)
    // 20210304 Jamie
    epm_namespace_t *epm_ns_data = (epm_namespace_t *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
    struct ns_array_manage * ns_manage = (struct ns_array_manage *)(epm_ns_data->data);

	eccu_switch_setting(ns_manage->ns_attr[0].hw_attr.lbaf);
    // set btn_control_reg for host meta
    u32 reg_val = readl((void *)(0xc0010044));
    if (host_du_fmt == 4) {
        reg_val |= BIT(3); // hmeta_enable
        if (HMETA_SIZE > 8) {
            reg_val |= BIT(11); // hmeta_size
        }
        writel(reg_val, (void *)(0xc0010044));
    }
    ftl_apl_trace(LOG_ERR, 0x4dee, "ftl_init host_du_fmt 0x%x %d", &host_du_fmt, host_du_fmt);
#endif

#if(QBT_TLC_MODE_DBG == mENABLE)
	u32 ftl_tag;
	//u32 qbt_sn;
	u16 qbt_blk_idx;
	ftl_tag = frb_log_get_ftl_tag();
	qbt_blk_idx = frb_log_get_qbt_blk();
	//qbt_sn = frb_log_get_qbt_sn();
	//ftl_apl_trace(LOG_ALW, 0, "[FTL] ftl_tag 0x%x qbt_blk_idx %d qbt_sn 0x%x", ftl_tag, qbt_blk_idx, qbt_sn);
	if(ftl_tag == 1)
	{
		QBT_TAG = true;
		QBT_BLK_IDX = qbt_blk_idx;
	}
#endif
#if(QBT_TLC_MODE == mENABLE)
#if (SPOR_FLOW == mDISABLE)
	frb_log_set_qbt_info(INV_U32, INV_U16, INV_U32);
	frb_log_type_update(FRB_TYPE_HEADER);
#endif
#endif


    ftl_virgin = true; // Curry
    FTL_GLOVARINIT(FTL_INITIAL_POWER_ON);

    FTL_CACL_QBTPBT_CNT();

	cal_res_blk_for_smart();//add by suda

#if (OTF_TIME_REDUCE == ENABLE)
	if(_fg_warm_boot == false)
#endif
	{
    #if (SPOR_FLOW == mENABLE)
    ftl_spor_resource_init();
    #endif
	}

	//recon_pre_init();//recon init must be prior to ftl ns init to make l2p ddr dtag at last
	#if 1
    cpu4_glist_dtag = ddr_dtag_register(3);
    Err_Info_Cpu4_Addr = (sGLEntry *)ddtag2mem(cpu4_glist_dtag);
    Err_Info_size = 256;
    wOpenBlk = (u16*)ddtag2mem((cpu4_glist_dtag+2));
    ftl_apl_trace(LOG_INFO, 0xe31f, "Err_Info_Cpu4_Addr 0x%x,cpu4_glist_dtag 0x%x", Err_Info_Cpu4_Addr,cpu4_glist_dtag);
    //total IPC 128, so over 128 shall be ok.
    #endif

    #if RAID_SUPPORT_UECC
    uecc_smart_info_init();
    #endif

    #ifdef RD_FAIL_GET_LDA
    rd_err_get_lda_init();
    #endif

	ftl_ns_init();
	l2p_updt_init();

#if (SPB_BLKLIST == mENABLE)
	   u32 free_pool_cnt = spb_pool_get_good_spb_cnt(SPB_POOL_UNALLOC);// - FTL_MAX_QBT_CNT;
	   ftl_apl_trace(LOG_ERR, 0x4935, "[FTL]free_pool_cnt %d", free_pool_cnt);
	   ftl_ns_alloc(1, _max_capacity, false, true);
	   ftl_ns_link_pool(1, SPB_POOL_UNALLOC, 0, 0);
	   ftl_ns_link_pool(1, SPB_POOL_FREE, CAP_NEED_SPBBLK_CNT, free_pool_cnt);
	   ftl_ns_link_pool(1, SPB_POOL_USER, 0, 0);
#endif

	sys_log_desc_register(&_fe_ns_info_desc, FE_NS_MAX_CNT, FE_NS_INFO_MAX_SZ_PER_NS, fe_ns_info_desc_init);
	fe_log_init();
	sys_log_start(sys_log_pblk);
	spb_mgr_start();

    //writel(0x4, (void*)0xC000001C);
    //evlog_printk(LOG_ALW, "%d tx_count = %d", readl((void*)0xC000001C), readl((void*)0xC0000034)&0xFFFF);
    //evlog_printk(LOG_ALW,"host_du_fmt %d lbaf %d", host_du_fmt, ns_manage->ns_attr[0].hw_attr.lbaf);

    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
#if (WARMBOOT_FTL_HANDLE == mENABLE)
	#if(WA_FW_UPDATE == DISABLE)
	if(_fg_warm_boot == true)
	{
		ftl_ns_post();
	}
	else
	#endif
#endif
	{
	spb_preformat_erase_continue();	
#if (SPOR_FLOW == mENABLE)
#if (SPOR_TIME_COST == mENABLE)
    u64 spor_time_start = get_tsc_64();
#endif
    shr_page_sn  = 0;
    shr_qbt_loop = INV_U8;
    shr_pbt_loop = 0;
    ftl_scan_all_blk_info();
    ftl_scan_info_handle();
    ftl_last_pbt_seg_handle(); 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    ftl_chk_slc_buffer_done();
#endif
#if (PLP_SUPPORT == 0)
	ftl_chk_non_plp_init();
#endif
    ftl_ns_post(); // load L2P/Blklist
    ftl_spor_blk_sn_sync();

    if (epm_ftl_data->format_tag == FTL_FULL_TRIM_TAG)
    {
        ftl_apl_trace(LOG_ALW, 0x64dd, "FULL Trim Skip SPOR");
#if(PLP_SUPPORT == 1)
        ftl_clear_vac_ec_in_epm();
#endif
        ftl_save_fake_qbt();    // QBT is saved to prevent : plp is triggered when FULL_Trim,and if plp after power-on,spor will be run.
        goto SKIP_SPOR_FLOW;
    }

    #ifdef Dynamic_OP_En
    // need to chk EPM flag to see if need to init tables, Sunny 20211028

    ftl_apl_trace(LOG_ALW, 0x54eb, "epm_ftl_data->Set_OP_Start : 0x%x", epm_ftl_data->Set_OP_Start);

    if(epm_ftl_data->Set_OP_Start == EPM_SET_OP_TAG)
    {
        ftl_apl_trace(LOG_ALW, 0xb38c, "Set OP not done");
        ftl_save_fake_qbt();
        goto SKIP_SPOR_FLOW;
    }
    #endif

	/*
	if((ftl_need_p2l_rebuild() == mFALSE) && ftl_get_1bit_data_error_flag())
	{
		ftl_apl_trace(LOG_ALW, 0, "No new data but 1 bit fail");
        ftl_save_fake_qbt();
        goto SKIP_SPOR_FLOW;
	}
	*/

#if(SPOR_QBT_MAX_ONLY == mDISABLE)

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(epm_ftl_data->plp_slc_gc_tag == FTL_PLP_SLC_NEED_GC_TAG && epm_ftl_data->plp_slc_disable == 0)
	{
		//1.if SPOR , wait fqbt done then trigger slc gc
		//2.if POR	, trigger slc gc after ftl init done 
		ftl_set_trigger_slc_gc(true);
		ftl_calculate_slc_build_range();
	}
#endif

	if(ftl_is_host_data_exist() && ftl_need_p2l_rebuild() && !get_skip_spor_build())//if skip flag is true,just load QBT
    { 
		#if (FW_BUILD_VAC_ENABLE == mENABLE)
    	CPU3_spor_state = SPOR_STATE_NEED_BUILD;   	
		#endif
        #ifdef ERRHANDLE_GLIST
        ftl_check_glist_last_blk();
        #endif

        // reset L2P if EPM vac has error
        if(ftl_is_epm_vac_error() && ftl_is_spor_user_build())
        {
            ftl_apl_trace(LOG_ALW, 0x0437, "EPM Vac error, reset L2P table");
            ftl_hns_t *hns = &_ftl_hns[FTL_NS_ID_START];
            ftl_l2p_reset(&hns->l2p_ele);
        }
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
		ftl_restore_vac_from_epm();
#endif

#if (SPOR_VAC_CMP == mENABLE)
		ftl_vac_buffer_copy(); // added for cmp vac
#endif

		user_build_setRO();
    	ftl_p2l_build_table();
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE) 
		if(ftl_get_trigger_slc_gc())
		{
			//ftl_calculate_slc_build_range();
			ftl_aux_build_slc_l2p();
			ftl_slc_build_l2p();
		}

#endif
#if ((SPOR_VAC_CMP == mENABLE)||(PLP_SUPPORT == 0)) 
		if(1)
#else
		if (is_need_l2p_build_vac()) 
#endif
		{
			// reset vac table
			u32 i;
			u32 cnt;
			u32 *vc;
			dtag_t dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
			for (i = 0; i < get_total_spb_cnt(); i++) {
				vc[i] = 0;
			}
			ftl_l2p_put_vcnt_buf(dtag, cnt, true);
			//ftl_l2p_build_vac();
			#if (FW_BUILD_VAC_ENABLE == mDISABLE)
			l2p_mgr_vcnt_rebuild();
			#else
			u32 *vc_recon;
			dtag_t dtag_recon = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
			vc_recon = dtag2mem(dtag_recon);  
			
			fw_vac_rebuild(vc_recon);
			if(fw_memory_not_enough == false)
			{
				ftl_l2p_put_vcnt_buf(dtag_recon, cnt, true);
			}
			else
			{
				l2p_mgr_vcnt_rebuild();
			}
			#endif
		}

        ftl_check_all_blk_status();
        spb_ReleaseEmptyPool();
        ftl_spor_pbt_push_free();
		ftl_free_vac_zero_blk(SPB_POOL_USER, FTL_SORT_NONE);
		ftl_spor_spb_desc_sync();
		ftl_set_spor_dummy_blk_flag();//set open flag for dummy blk

#if(PLP_SUPPORT == 0) 
		ftl_update_non_plp_info();
		ftl_show_non_plp_info();
#endif

        #if (SPOR_VAC_CMP == mENABLE)
        ftl_vac_compare();     // cmp vac
        #endif

		#if (PLP_SUPPORT == 1 && SPOR_DUMP_ERROR_INFO == mENABLE)
    	ftl_plp_no_done_dbg();
   		ftl_vac_error_dbg();
        #endif
        ftl_save_fake_qbt();
        
		#if (FW_BUILD_VAC_ENABLE == mENABLE)
		CPU3_spor_state = SPOR_STATE_BUILD_DONE;
		#endif
		
    }
	#if (PLP_SLC_BUFFER_ENABLE  == mENABLE) 
    else if(ftl_get_trigger_slc_gc())//QBT + SLC
    {
    	/*
    		1.QBT sn > SLC max sn----------QBT record SLC data
    		2.QBT sn < SLC max sn----------no usr blk but flush SLC buffer
    		  this need build SLC buffer and save fake QBT again
    	*/
		//bool ret = get_skip_spor_build();
		//if(epm_ftl_data->plp_last_blk_sn == INV_U32)//case 2 QBT sn < SLC max sn
		//{
			ftl_apl_trace(LOG_WARNING, 0x06c5,"QBT sn < SLC max sn!! need build SLC buffer");
			ftl_qbt_slc_mode_dbg();
			ftl_restore_vac_from_epm();//epm record SLC vac
			ftl_aux_build_slc_l2p();
			ftl_slc_build_l2p();
			ftl_save_fake_qbt();
		//}
    }
	#endif

#endif


SKIP_SPOR_FLOW:
	if(delay_flush_spor_qbt == true)//SPOR
	{
		shr_lock_power_on |= FQBT_LOCK_POWER_ON;  // fqbt_lock ++
	}
	else//POR
	{
		gFtlMgr.LastQbtSN = gFtlMgr.SerialNumber;
		#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)  
	    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	    //ftl_apl_trace(LOG_ALW, 0, "[Pre]epm_ftl_data->spor_last_rec_blk_sn : 0x%x", epm_ftl_data->spor_last_rec_blk_sn);
	    epm_ftl_data->spor_last_rec_blk_sn = gFtlMgr.LastQbtSN;
	    ftl_apl_trace(LOG_ALW, 0xe431, "[After]epm_ftl_data->spor_last_rec_blk_sn : 0x%x", epm_ftl_data->spor_last_rec_blk_sn);
	    #endif 
	}

#if (OTF_TIME_REDUCE == ENABLE)
	if(_fg_warm_boot == false)
	{
		#if(SPOR_CMD_ON_DDR == mDISABLE)
    		ftl_spor_resource_recycle();
		#endif
	}

	//_fg_warm_boot = false;
#else
#if (SPOR_CMD_ON_DDR == mDISABLE)
    ftl_spor_resource_recycle();
#endif
#endif

    isSave = ftl_spor_error_save_log();

#if (SPB_BLKLIST == mENABLE)
	//check blklist & push the wrong blk to the correct pool //Howard
	spb_id_t qbt_idx = ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC);

	while(qbt_idx != INV_U16){
		if(spb_info_tbl[qbt_idx].pool_id != SPB_POOL_QBT_ALLOC)
		{
			FTL_BlockPopPushList(SPB_POOL_QBT_ALLOC, qbt_idx, FTL_SORT_NONE);
		}
		qbt_idx = ftl_spor_get_next_blk(qbt_idx);
	}
#endif

#if (SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0x7b3b, "SPOR total time cost : %d us", time_elapsed_in_us(spor_time_start));
#endif
#if(MDOT2_SUPPORT == 1)
#if(PLP_SUPPORT == 1) 
    if(PLP_IC_SGM41664)
    {
        ftl_apl_trace(LOG_INFO, 0xdc2d, "SGM data 0x%x",epm_ftl_data->SGM_data_F_10);
    }
#endif    
#endif

    #if 1 // for dbg, by Sunny
    ftl_l2p_print();
	if(delay_flush_spor_qbt == mFALSE)
	{ 
        ftl_vc_tbl_print();
	}
    #endif
#else
    ftl_ns_post();
#endif
	}

	gFtlMgr.GlobalPageSN++;
    shr_page_sn = gFtlMgr.GlobalPageSN;
#if (FW_BUILD_VAC_ENABLE == mENABLE)
	CPU3_spor_state = SPOR_STATE_BUILD_DONE;
#endif
#if (SPOR_FLOW == mENABLE)

	if(_fg_warm_boot == false)
	{
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)  
		ftl_restore_ec_from_epm();
#endif

#if (PLP_SUPPORT == 0)
		ftl_save_non_plp_ec_table(0);
#endif
		//gFtlMgr.TableSN     = gFtlMgr.SerialNumber;
    	//gFtlMgr.PrevTableSN = gFtlMgr.SerialNumber;

        extern struct ftl_spor_info_t *ftl_spor_info;
        if (!ftl_spor_info->existHostData || (epm_ftl_data->format_tag == FTL_FULL_TRIM_TAG) || (epm_ftl_data->format_tag == FTL_PREFORMAT_TAG))     // SPB_POOL_FREE may be sorted by blk_idx now, and it needs to be reordered by ec
        {
            ftl_apl_trace(LOG_ALW, 0x1aab, "reorder SPB_POOL_FREE by ec value");
            FTL_NO_LOG = true;
        	u16 blk_idx, pool_id;
        	for (blk_idx = 0; blk_idx < get_total_spb_cnt(); blk_idx++)
            {
                pool_id = spb_info_get(blk_idx)->pool_id;
        		if(pool_id == SPB_POOL_FREE)
        		{
        			FTL_BlockPopPushList(SPB_POOL_FREE, blk_idx, FTL_SORT_BY_EC);
                }
        	}

        	FTL_NO_LOG = false;
        	//need format TCG
#if 0//(PLP_SUPPORT == 0)
			//epm_ftl_data->epm_fmt_not_finish = 0;
			//epm_update(FTL_sign,(CPU_ID-1));//if plp enable,format flag will reset in clear_epm_vac.
#endif
        }

	}

#endif

    init_max_rd_cnt_tbl();
	spb_poh_init();
	wait_remote_item_done(wait_ns_restore);
#if (PLP_SUPPORT == 1)
	epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    if( _fg_warm_boot == false && ((epm_ftl_data->epm_sign != FTL_sign) || (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG)))
    {
    	if(epm_ftl_data->spor_tag != 0)
    	{
			u16 loop_idx = !!(epm_ftl_data->epm_record_loop);
			epm_ftl_data->epm_record_plp_not_done[loop_idx] = pc_cnt;
			epm_ftl_data->epm_record_max_blk_sn[loop_idx] = gFtlMgr.SerialNumber;
			ftl_apl_trace(LOG_ALW, 0x4239," plp not done, loop %d  cur power cycle:0x%x , max sn:0x%x",loop_idx,pc_cnt,gFtlMgr.SerialNumber);
			epm_ftl_data->epm_record_loop ^= 1;
			
			if(!isSave)
			{
				flush_to_nand(EVT_PLP_NOT_DONE);
			}

			save_log_timer.function = save_log_for_plp_not_done;
			save_log_timer.data = NULL;
			mod_timer(&save_log_timer, time_elapsed_in_ms(init_time_start)/100 + 25*HZ);//power on 25s save nand log
    	}
    }
#else//for nonplp compile
	if(isSave)
	{
	}
#endif

#if (SPOR_FLOW == mENABLE)		
	if(_fg_warm_boot == false)
	{
		//qbt_retire_handle(INV_U16);// chk qbt ec over 5k
		// just call delay save qbt , don't use function ftl_flush_qbt anymore
		if(ftl_get_1bit_data_error_flag() || (replace_qbt == true))
		{
			ftl_apl_trace(LOG_ALW, 0x0b95, "QBT 1 bit fail / qbt replace need save FQBT");
			ftl_save_fake_qbt();
			ftl_set_1bit_data_error_flag(false);
		}
		gFtlMgr.TableSN 	= gFtlMgr.SerialNumber;
		gFtlMgr.PrevTableSN = gFtlMgr.SerialNumber;
	}
#endif	

	
    ftl_ns_start();//delay fqbt evt will be called in ftl_core_start

	pmu_swap_pblk_init();
	pmu_register_handler(SUSPEND_COOKIE_FTL, ftl_suspend, RESUME_COOKIE_FTL, ftl_resume);

    #ifdef ERRHANDLE_GLIST
	eh_dump_glist();
    if(delay_flush_spor_qbt == mFALSE)//wait FQBT done, CPU2 will call eh_scan_glist_trig_handle again
    {
    	eh_scan_glist_trig_handle(EH_BUILD_GL_TRIG_EHTASK);
    }
    #endif
    #ifdef ERRHANDLE_ECCT
    wECC_Build_Table();
	rc_ecct_cnt = 0;
    ecct_pl_cnt2 = 0;
    ecct_pl_cnt4 = 0;
    memset(rc_ecct_info, 0, MAX_RC_REG_ECCT_CNT * sizeof(stECCT_ipc_t));
    memset(ecct_pl_info_cpu2, 0, MAX_RC_REG_ECCT_CNT * sizeof(tEcctReg));
    memset(ecct_pl_info_cpu4, 0, MAX_RC_REG_ECCT_CNT * sizeof(tEcctReg));
    #endif


    Ec_tbl_update(INV_U16);
#if (SPB_BLKLIST == mENABLE)
	spb_pool_table();
#endif

	if(!unlock_power_on)
	{
		if(!_fg_warm_boot)
		{
			if(spb_get_free_cnt(SPB_POOL_FREE) <= GC_BLKCNT_EMER_SPD_CTL)
			{
				shr_lock_power_on |= GC_LOCK_POWER_ON;  // gc_lock ++
				lock_power_on_timer.function = lock_power_on_gc_ready;
				lock_power_on_timer.data = NULL;
				mod_timer(&lock_power_on_timer, time_elapsed_in_ms(init_time_start)/100 + 4*HZ); // jiffies havn't init until cpu init done
			}
		}
		else if(spb_get_free_cnt(SPB_POOL_FREE) <= (GC_BLKCNT_EMER_SPD_CTL - 1))
		{
			shr_lock_power_on |= GC_LOCK_POWER_ON;  // gc_lock ++
			lock_power_on_timer.function = lock_power_on_cancle;
			lock_power_on_timer.data = NULL;
			mod_timer(&lock_power_on_timer, jiffies + 4*HZ);  // warmboot init doesn't cost too many time, jiffies is similar
		}
	}
#ifdef History_read
    shr_avg_erase = spb_ec_tbl->header.AvgEC;
    u32 ddr_his_tab_start = (u32)&__his_tab_start;
    hist_tab_ptr_st = (hist_tab_offset_t*)ddr_his_tab_start;
    ftl_apl_trace(LOG_INFO, 0xc624, "hist_tab_ptr_st:0x%x",hist_tab_ptr_st);
    bm_scrub_ddr(ddr_his_tab_start - DDR_BASE, shr_nand_info.geo.nr_blocks*sizeof(hist_tab_offset_t), 0);
#endif

#if PATROL_READ
    pard_mgr_init(&pard_mgr);
    patrol_read_timer.function = patrol_read;
	patrol_read_timer.data = NULL;
	if(shr_nand_info.interleave < PARD_DIE_AIPR)
	{
		pard_table[0].w.h = shr_nand_info.interleave;
		patrol_time = 1;
	}
	else
	{
		patrol_time = shr_nand_info.interleave / PARD_DIE_AIPR;
		sys_assert((shr_nand_info.interleave % PARD_DIE_AIPR) == 0);
	}
    mod_timer(&patrol_read_timer, jiffies+6*HZ);
    ftl_apl_trace(LOG_INFO, 0x358d, "enter ftl core init and patrol read timer init ");
#endif

#if CO_SUPPORT_DEVICE_SELF_TEST
    evt_register(DST_patrol_read_start,0,&evt_DST_patrol_read);
	DST_patrol_read_timer.function = DST_patrol_read;
	DST_patrol_read_timer.data = NULL;
#endif

#ifdef OTF_MEASURE_TIME
	ftl_apl_trace(LOG_ERR, 0xf5f6, "[M_T]FTL init %d us\n", time_elapsed_in_us(global_time_start[4]));
#endif

    ftl_apl_trace(LOG_ALW, 0x8829, "gFtlMgr.GlobalPageSN : 0x%x%x, gFtlMgr.SerialNumber : 0x%x", (u32)(gFtlMgr.GlobalPageSN>>32), (u32)(gFtlMgr.GlobalPageSN), gFtlMgr.SerialNumber);
	ftl_apl_trace(LOG_ALW, 0x0932, "gFtlMgr.TableSN : 0x%x, gFtlMgr.PrevTableSN : 0x%x, shr_qbt_loop:%u", gFtlMgr.TableSN, gFtlMgr.PrevTableSN, shr_qbt_loop);
    ftl_apl_trace(LOG_ALW, 0x740b, "gFtlMgr.LastQbtSN : 0x%x, gFtlMgr.last_host_blk : 0x%x, gFtlMgr.last_gc_blk : 0x%x", gFtlMgr.LastQbtSN, gFtlMgr.last_host_blk, gFtlMgr.last_gc_blk);
	ftl_apl_trace(LOG_ALW, 0xa616, "FTL+BE done, time cost : %d us", time_elapsed_in_us(init_time_start));
#if (SPOR_FLOW == mENABLE)
    shr_ftl_flags.b.l2p_all_ready = true;
#endif
	first_usr_open = false;

#ifdef ERRHANDLE_GLIST
	FTL_INIT_DONE = true;
#endif
	epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	epm_ftl_data->POR_tag = 0;
    #if (UART_L2P_SEARCH == 1)
	l2pSrchUart = 0;  //only for uart cmd l2psrch use
    #endif

	if(_fg_warm_boot)
    {
		//fake_ipc_gc_chk(0);				//1. warm_boot trigger gc
    	evt_set_cs(evt_ftl_gc_check, 0, 0, CS_TASK);
    }
	else
	{
#if (PLP_SLC_BUFFER_ENABLE	== mENABLE) 		
		if(ftl_get_trigger_slc_gc())
		{	
			extern volatile u8 shr_slc_flush_state;
			extern volatile u8 shr_slc_need_trigger;

			shr_lock_power_on |= SLC_LOCK_POWER_ON; //no enter fqbt flow, slc_lock ++
			shr_slc_flush_state = SLC_FLOW_GC_START;
			shr_slc_need_trigger = true;//CPU1 main trigger
			//fake_ipc_gc_chk(1);			//2.if slc gc no done , trigger gc flow in por flow
			//evt_set_cs(evt_ftl_gc_check, 0, 1, CS_TASK);
		}
#endif
#if (PLP_SUPPORT == 0)	
		if(is_need_trigger_non_plp_gc())
		{
			extern volatile u8 shr_lock_power_on;
			extern volatile bool shr_nonplp_gc_state;
			shr_lock_power_on |= NON_PLP_LOCK_POWER_ON;//no enter fqbt flow, nonplp_lock ++
			shr_nonplp_gc_state = SPOR_NONPLP_GC_START;
			dsb();
			//fake_ipc_gc_chk(2);			//3. if non-plp gc no done , trigger gc flow in por flow
    		evt_set_cs(evt_ftl_gc_check, 0, 2, CS_TASK);
		}
		else if(delay_flush_spor_qbt == mFALSE)
		{
			//fake_ipc_gc_chk(0);			//4. non-plp POR flow , trigger gc here
    		evt_set_cs(evt_ftl_gc_check, 0, 0, CS_TASK);
		}
#endif
		spor_clean_tag();
	}

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
		cpu_msg_issue(	0, CPU_MSG_REQUIRE_UPDATE_TELEMETRY_CTRLR_DATA, 0, 0 );
#endif

}

/*! \cond PRIVATE */
module_init(ftl_init, z.1);
/*! \endcond */
/*! @} */
#if (FW_BUILD_VAC_ENABLE == mENABLE)
 extern volatile bool vc_recon_busy[4];
 extern u32 *recon_vc;

fast_code u32 *l2p_vac_recon_cpu3(void)
{
    u32 start_lda = aligned_down(_max_capacity / 4 * 2, 8192);
    u32 end_lda = aligned_down(_max_capacity / 4 * 3, 8192);
    ftl_apl_trace(LOG_ALW, 0x7963, "CPU:%d build start:0x%x, end:0x%x", CPU_ID, start_lda, end_lda);

    u64 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000);

    u32 size = shr_nand_info.geo.nr_blocks * 4;
    u32 *vc = (u32 *)sys_malloc_aligned(FAST_DATA, size, 4);
    sys_assert(vc);
    memset(vc, 0, size);

    u32 *l2p = sys_malloc_aligned(FAST_DATA, 4096, 32);
    sys_assert(l2p);

    u32 pda_blk_shift = shr_nand_info.pda_block_shift;
    u32 pda_blk_mask  = shr_nand_info.pda_block_mask;

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

            // debug
            //	pta_t expect = get_l2p_entry(lda);
            //	if (expect != pta) {
            //	    ftl_apl_trace(LOG_ALW, 0xd4b6, "-i:%d exp:0x%x act:0x%x", i, expect, pta);
            //	}

            if (pda != INV_U32) 
            {
                u32 spb_id = ((pda >> pda_blk_shift) & pda_blk_mask);
                vc[spb_id]++;
            }
        }
        curr_lda += 1024;
        addr += 4096;
    }

    recon_vc = tcm_local_to_share(vc);
    dsb();    // make recon_vc visible to all core2 before busy reset
    vc_recon_busy[CPU_ID_0] = false;

    sys_free_aligned(FAST_DATA, l2p);
    // vc will be free outside
    return vc;
}

slow_code void fw_vac_rebuild(u32 *vc_buff)
{
	u64 fw_build_cost = get_tsc_64();
    for (u8 i = 0; i < 4; ++i) 
    {
        vc_recon_busy[i] = true;
    }
    
	//CPU1 is still in function rdisk_fe_init , don't submit cpu msg to avoid unexpected issue 
    if(CPU3_spor_state == SPOR_STATE_BUILD_DONE)
    {
    	cpu_msg_issue(CPU_FE - 1, CPU_MSG_VC_RECON, 0, 0);
    }
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_VC_RECON, 0, 0);
    cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_VC_RECON, 0, 0);
    u32 *recon_vc = l2p_vac_recon_cpu3();

    // wait all core done
    while (vc_recon_busy[0] || vc_recon_busy[1] || vc_recon_busy[2] || vc_recon_busy[3])
    {
		if(fw_memory_not_enough != 0)
		{
			ftl_apl_trace(LOG_ALW, 0x4c2f, "fw rebuild vac fail , CPU%d memory not enough",fw_memory_not_enough);
			
			sys_free_aligned(FAST_DATA, recon_vc);
			return;
		}
    }

    if (vc_buff)
    {
		memcpy(vc_buff, recon_vc, (sizeof(u32) * shr_nand_info.geo.nr_blocks));
    }
    
    sys_free_aligned(FAST_DATA, recon_vc);
    
	ftl_apl_trace(LOG_ALW, 0x9f00, "fw rebuild vac pass , cost %d ms",time_elapsed_in_ms(fw_build_cost));
}
#endif

extern bool format_gc_suspend_done;
fast_code void gc_action_done(gc_action_t *action)
{
	format_gc_suspend_done = 1;
	ftl_apl_trace(LOG_ALW, 0xee15, "uart GC action");
	sys_free(FAST_DATA, action);
}

fast_code void fwdl_gc_action_done(gc_action_t *action)
{
   fwdl_gcsuspend_done = 1;
   ftl_apl_trace(LOG_ALW, 0xc176, "FWDL GC handle DONE");
   sys_free(FAST_DATA, action);
}


extern volatile u8 STOP_BG_GC_flag;
slow_code_ex void warmboot_gc_action_done(gc_action_t *action)
{
	format_gc_suspend_done = 1;
	ftl_apl_trace(LOG_ALW, 0xd123, "warmboot GC action done");
	sys_free(FAST_DATA, action);

	if(plp_trigger){
		return ;
	}

    ftl_spb_pad_t *spb_pad = sys_malloc(FAST_DATA, sizeof(ftl_spb_pad_t));
    sys_assert(spb_pad);
    spb_pad->spb_id = 0xFFFF;
	spb_pad->ctx.caller = NULL;
	spb_pad->ctx.cmpl = ftl_warm_boot_spb_handle_done;
#if (OTF_TIME_REDUCE == ENABLE)
    if (shr_nand_info.lun_num == 8)
    {
	    spb_pad->param.cwl_cnt = 9;
    }
    else
    {
        spb_pad->param.cwl_cnt = 6;	// TBC, original method takes pad_all, ISU, LJ1-337, PgFalClsNotDone (1)
    }
	spb_pad->param.pad_all = false;
#else
	spb_pad->param.cwl_cnt = 0;// 4 dummy WL
	spb_pad->param.pad_all = true;
#endif
	spb_pad->param.start_type = FTL_CORE_NRM;
	spb_pad->param.end_type = FTL_CORE_GC;
    spb_pad->param.start_nsid = 1;
    spb_pad->param.end_nsid = 1;
    spb_pad->cur.cwl_cnt = 0;
	spb_pad->cur.type = FTL_CORE_NRM;
	spb_pad->cur.nsid = 1;

#ifdef OTF_MEASURE_TIME
	global_time_start[19] = get_tsc_64();
#endif
	ftl_core_spb_pad(spb_pad);
}
slow_code int gc_main(int argc, char *argv[])
{
	u32 pool;
	u32 type;
	spb_id_t can = INV_SPB_ID;

	type = strtol(argv[1], (void *)0, 10);
	pool = strtol(argv[2], (void *)0, 10);
    gc_action_t *action = NULL;
	switch (type)
	{
		case 0:
			spb_mgr_find_gc_candidate(1, pool, SPB_GC_CAN_SHOW);
			break;
		case 1:
			can = spb_mgr_find_gc_candidate(1, pool, SPB_GC_CAN_POLICY_MIN_VC);
			gc_continue = true;
			break;
		case 2:
			if (pool <= GC_ACT_RESUME)
			{
				action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

				action->act = pool;
				action->caller = NULL;
				action->cmpl = gc_action_done;

				if (gc_action(action))
				sys_free(FAST_DATA, action);
			}
			break;
		case 3:
			tzu_get_gc_info();
#if(PLP_GC_SUSPEND == mENABLE)
			gc_continue = ~gc_continue;
			//evlog_printk(LOG_ERR,"%d",gc_continue);
#endif
			break;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		case 4:
			GC_MODE_SETTING(GC_PLP_SLC);
			ftl_ns_gc_start_chk(1);
			can = INV_SPB_ID;
			break;
#endif		
        #if 0
        case 4:
            action = sys_malloc(FAST_DATA, sizeof(gc_action_t));
            sys_assert(action);
			action->act = GC_ACT_STOP_BG_GC;
			action->caller = NULL;
			action->cmpl = gc_action_done;

			if (gc_action(action)){
			    sys_free(FAST_DATA, action);
			}
            ftl_apl_trace(LOG_INFO, 0x7a15, "gc_suspend_stop_next_spb:%d, STOP_BG_GC_flag:%d",gc_suspend_stop_next_spb,STOP_BG_GC_flag);
            action = sys_malloc(FAST_DATA, sizeof(gc_action_t));
            sys_assert(action);
			action->act = GC_ACT_SUSPEND;
			action->caller = NULL;
			action->cmpl = gc_action_done;

			if (gc_action(action)){
			    sys_free(FAST_DATA, action);
			}
            ftl_apl_trace(LOG_INFO, 0xe02b, "gc_suspend_stop_next_spb:%d, STOP_BG_GC_flag:%d",gc_suspend_stop_next_spb,STOP_BG_GC_flag);
            break;
        case 5:
            action = sys_malloc(FAST_DATA, sizeof(gc_action_t));
            sys_assert(action);
			action->act = GC_ACT_SUSPEND;
			action->caller = NULL;
			action->cmpl = gc_action_done;

			if (gc_action(action)){
			    sys_free(FAST_DATA, action);
			}
            ftl_apl_trace(LOG_INFO, 0x1bad, "gc_suspend_stop_next_spb:%d, STOP_BG_GC_flag:%d",gc_suspend_stop_next_spb,STOP_BG_GC_flag);
            action = sys_malloc(FAST_DATA, sizeof(gc_action_t));
            sys_assert(action);
			action->act = GC_ACT_STOP_BG_GC;
			action->caller = NULL;
			action->cmpl = gc_action_done;

			if (gc_action(action)){
			    sys_free(FAST_DATA, action);
			}
            ftl_apl_trace(LOG_INFO, 0xc58f, "gc_suspend_stop_next_spb:%d, STOP_BG_GC_flag:%d",gc_suspend_stop_next_spb,STOP_BG_GC_flag);
            break;
        #endif
		default:
			can = strtol(argv[2], (void *)0, 10);
			break;

	}

	if (can != INV_SPB_ID)
	{
		gc_start(can, 1, INV_U32, NULL);
	}
	else
	{
		ftl_apl_trace(LOG_INFO, 0x08b2, "no can");
    }

	return 0;
}

/*! @brief help command declaration */
static DEFINE_UART_CMD(gc, "gc", "gc", "trigger gc", 2, 2, gc_main);

slow_code int flush_main(int argc, char *argv[])
{
	ftl_apl_trace(LOG_ERR, 0x7539, "l2p crc %x\n", ftl_l2p_crc());
	ftl_apl_trace(LOG_ERR, 0xe275, "vcnt crc %x\n", ftl_l2p_misc_crc(NULL));
	return 0;
}

/*! @brief help command declaration */
static DEFINE_UART_CMD(flush, "flush",
					   "flush",
					   "trigger table flush",
					   0, 0, flush_main);

slow_code int vc_main(int argc, char *argv[])
{
	spb_id_t spb;
	u32 cnt;
	u32 i;
	u32 *vc = sys_malloc_aligned(SLOW_DATA, (get_total_spb_cnt() + 1) * sizeof(u32), 32);

	if (vc == NULL)
		return 0;

	if (argc == 2) {
		spb = strtol(argv[1], (void *)0, 10);
		cnt = 1;
	} else {
		spb = 0;
		cnt = get_total_spb_cnt();
	}

	memset(vc, 0, (get_total_spb_cnt() + 1) * sizeof(u32));
	l2p_mgr_vcnt_move(false, vc, (get_total_spb_cnt() + 1) * sizeof(u32));
	for (i = 0; i < cnt; i++) {
		if(vc[spb] != 0)
		{
			//ftl_apl_trace(LOG_ALW, 0, "spb %d vc %d", spb, vc[spb]);
            ftl_apl_trace(LOG_ALW, 0x8529, "spb 0x%x, vc 0x%x spb %d vc %d", spb, vc[spb],spb , vc[spb]);
		}
		spb++;
	}
	sys_free_aligned(SLOW_DATA, vc);

	return 0;
}

static DEFINE_UART_CMD(vc, "vc", "vc", "get spb valid cnt", 0, 1, vc_main);
slow_code int ddr_vc_main(int argc, char *argv[])
{
	u16 i;
	u32 *ddr_vac_buffer = (u32*)ddtag2mem(shr_vac_drambuffer_start);
	for(i=0; i<get_total_spb_cnt(); i++)
	{
		ftl_apl_trace(LOG_ALW, 0x4f75, "dbg vcnt buffer spb[%d]:%d", i, ddr_vac_buffer[i]);
	}
	return 0;
}
static DEFINE_UART_CMD(ddr_vc, "ddr_vc", "ddr_vc", "get ddr spb valid cnt", 0, 0, ddr_vc_main);

slow_code int pda_srch_main(int argc, char *argv[])
{
	u32 lda;
	spb_id_t spb;
	u32 capacity = ftl_ns_get_capacity(1);
    u32 pda;
	spb_id_t srch_spb = strtol(argv[1], (void *)0, 10);
	u64 l2p = ddtag2mem(shr_l2p_entry_start);
    u8 win = 0;
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
    u8 old = ctrl0.b.cpu3_ddr_window_sel;
	bool found = false;
	for (lda = 0; lda < capacity; lda++) {
		//if ((lda % 1000) == 0)
			//ftl_apl_trace(LOG_ALW, 0, "srch from %d to %d", lda, lda + 1000);
        if(l2p >= 0xC0000000)
        {
        	l2p -= 0x80000000;
            win++;
            ctrl0.b.cpu3_ddr_window_sel = win;
            writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
            __dmb();
        }
        pda = *((u32*)((u32)l2p));
        l2p += sizeof(lda_t);
		spb = pda >> shr_nand_info.pda_block_shift;
		if (spb == srch_spb) {
            if(old!=win)
            {
                ctrl0.b.cpu3_ddr_window_sel = old;
                writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                __dmb();
            }
			ftl_apl_trace(LOG_ALW, 0xa6d7, "got you: lda 0x%x -- pda 0x%x", lda, pda);
            if(old!=win)
            {
                ctrl0.b.cpu3_ddr_window_sel = win;
                writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                __dmb();
            }
			found = true;
			//break;
		}
	}
     if(old!=win)
    {
        ctrl0.b.cpu3_ddr_window_sel = old;
        writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
        __dmb();
    }
	ftl_apl_trace(LOG_ALW, 0xf7b8, "srch done %d", found);
	return 0;
}

static DEFINE_UART_CMD(pda_srch, "pda_srch",
		"pda_srchs",
		"srch spb 's pda & lda",
		1, 1, pda_srch_main);


slow_code int spb_meta_dump_main(int argc, char *argv[])
{
#if 0 // not used function, marked for more SRAM space at CPU3
	u32 i;
	spb_id_t spb = strtol(argv[1], (void *)0, 10);
	nal_pb_type_t pb_type = strtol(argv[2], (void *)0, 10);
	u32 nr_du = pb_type == NAL_PB_TYPE_SLC ? get_page_cnt_in_slc_spb() : get_page_cnt_in_native_spb();
	log_level_t old = ipc_api_log_level_chg(LOG_ALW);
	void *mem;
	dtag_t dtag;
    bm_pl_t *bm_pl_list;
	pda_t *pda_list;
	struct info_param_t *info_list;
	ncl_page_res_t *page_res;
	ftl_apl_trace(LOG_ERR, 0x1b1d, "spb_meta_dump_main");

	page_res = share_malloc(sizeof(*page_res));
	bm_pl_list = page_res->bm_pl;
	pda_list = page_res->pda;
	info_list = page_res->info;

	dtag = dtag_get(DTAG_T_SRAM, &mem);

	if (dtag.dtag == DTAG_INV) {
		ftl_apl_trace(LOG_ERR, 0xb65d, "oom");
		return 1;
	}

	ftl_apl_trace(LOG_ALW, 0xeaf9, "dump spb %d meta, type %d", spb, pb_type);
	nr_du = nr_du * DU_CNT_PER_PAGE;
	for (i = 0; i < nr_du; i += 1) {
        bm_pl_list[0].pl.dtag = dtag.dtag;
        bm_pl_list[0].pl.nvm_cmd_id = sram_dummy_meta_idx,
    	bm_pl_list[0].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX,
		memset(&info_list[0], 0, sizeof(struct info_param_t));
    	info_list[0].status = ficu_err_good;
    	info_list[0].pb_type = pb_type;
        pda_list[0] = nal_make_pda(spb, i);
        ncl_read_dus(pda_list, 1, bm_pl_list,
    			info_list, NAL_PB_TYPE_SLC, true, false);

#ifndef LJ_Meta
		ftl_apl_trace(LOG_ERR, 0x88a1, "[%d] %x st %d %x %x %x %x \n", i,  pda_list[0], info_list[0].status,
				sram_dummy_meta->seed_index,
				sram_dummy_meta->lda,
				sram_dummy_meta->hlba,
				sram_dummy_meta->sn);
#else
		ftl_apl_trace(LOG_ERR, 0x0d41, "[%d] %x st %d %x %x %x %x \n", i,  pda_list[0], info_list[0].status,
				sram_dummy_meta->lda,
				sram_dummy_meta->hlba);
#endif
	}
	share_free(page_res);

	ipc_api_log_level_chg(old);
    return 0;
#else
	return 0;
#endif
}

static DEFINE_UART_CMD(spb_meta_dump, "spb_meta_dump",
		"spb_meta_dump",
		"dump spb meta ",
		2, 2, spb_meta_dump_main);

#if (epm_enable && epm_uart_enable)
extern epm_info_t*  shr_epm_info;
slow_code int epm_update_cpu3(int argc, char *argv[])
{   u32 i;
	ftl_apl_trace(LOG_ERR, 0xbd2b, "epm_update_cpu3\n");
	ftl_apl_trace(LOG_ERR, 0x10dc, "FTL_sign=%d\n",FTL_sign);
#if epm_spin_lock_enable
	check_and_set_ddr_lock(FTL_sign);
#endif
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	ftl_apl_trace(LOG_ERR, 0x2c27, "epm_ftl_data addr=0x%x\n",epm_ftl_data);
	for(i=0;i<(262144/4);i++)
	{
		epm_ftl_data->data[i]=0x10000000+(FTL_sign*0x100000)+i;
	}
	epm_update(FTL_sign,(CPU_ID-1));
	return 0;
}

static DEFINE_UART_CMD(epm_update3, "epm_update3", "epm_update3", "epm_update3", 0, 0, epm_update_cpu3);
#endif
//Andy_Crypto
#if 0//defined(USE_CRYPTO_HW)
slow_code int change_crypto_cpu3(int argc, char *argv[])
{
	ftl_apl_trace(LOG_ERR, 0x7b2e, "change_range_cpu3\n");

	crypto_change_mode_range(3,1,0,1);
	return 0;
}

static DEFINE_UART_CMD(change_crypto3, "change_crypto3", "change_crypto3", "change_crypto3", 0, 0, change_crypto_cpu3);
#endif

#if PATROL_READ
slow_code_ex int uart_patrol_read(int argc, char *argv[])
{
    pard_switch = strtol(argv[1], (void *)0, 10);
    pard_mgr.sblk_change = strtol(argv[2], (void *)0, 10);
    ftl_apl_trace(LOG_INFO, 0x543a, "pard_switch %d blk_change %d", pard_switch , pard_mgr.sblk_change);
    return 0;
}

static DEFINE_UART_CMD(patrol, "patrol", "patrol", "patrol", 0, 2, uart_patrol_read);
#endif

#if (SPOR_FLOW == mENABLE)
slow_code_ex int l2p_build_vac(int argc, char *argv[]) {  
	//ftl_l2p_build_vac();  
	u32 i;  
	u32 cnt;  
	u32 *vc;  
	dtag_t dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);  
	u32 *vc_old = NULL/*,*vc_new = NULL*/;  
	dtag_t dtag_old_vc = dtag_cont_get(DTAG_T_SRAM, cnt);  
	//dtag_t dtag_new_vc = dtag_cont_get(DTAG_T_SRAM, cnt);  

	vc_old = dtag2mem(dtag_old_vc);  
	//vc_new = dtag2mem(dtag_new_vc);  

	memcpy(vc_old, vc, shr_nand_info.geo.nr_blocks*sizeof(u32));  
	  
	for (i = 0; i < get_total_spb_cnt(); i++) {  
		vc[i] = 0;  
	}  
	ftl_l2p_put_vcnt_buf(dtag, cnt, true);  
	l2p_mgr_vcnt_rebuild();  
	dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);  
	//memcpy(vc_new, vc, shr_nand_info.geo.nr_blocks*sizeof(u32));  
	bool vac_error = false;  
	extern u8* ftl_build_tbl_type;
	extern u32* ftl_init_sn_tbl;
	for (i = 0; i < get_total_spb_cnt(); i++)   
	{  
		if(vc_old[i] != vc[i])  
		{  
			vac_error = true;  
			if(argv != NULL)
				ftl_apl_trace(LOG_ALW, 0xbbb2, "spb_id : %d  vac_old : 0x%x vac_new : 0x%x",i,vc_old[i],vc[i]);  
			else
				ftl_apl_trace(LOG_ALW, 0x04b8, "spb_id : %d  vac_old : 0x%x vac_new : 0x%x<|>Type : %d SN:0x%x",i,vc_old[i],vc[i],ftl_build_tbl_type[i],ftl_init_sn_tbl[i]);  
		}  
	}  
	if(!vac_error)  
	{  
		ftl_apl_trace(LOG_ALW, 0xed1b, "It's OK!!!!");  
	}  

	bool save_new_vac = false;
	if(argc == 2)
	{
		save_new_vac = !!strtol(argv[1], (void*)(0),10);
	}
	extern bool SPOR_need_vac_rebuild;
	if(save_new_vac || SPOR_need_vac_rebuild)
	{
		ftl_l2p_put_vcnt_buf(dtag, cnt, true);  
		dtag_cont_put(DTAG_T_SRAM, dtag_old_vc,cnt);  
	}
	else
	{
		ftl_l2p_put_vcnt_buf(dtag_old_vc, cnt, true);  
		dtag_cont_put(DTAG_T_SRAM, dtag,cnt);  
	}

	//dtag_cont_put(DTAG_T_SRAM, dtag,cnt);  
	ftl_apl_trace(LOG_ALW, 0x9553, "save_new_vac:%d",save_new_vac);	

	//dtag_cont_put(DTAG_T_SRAM, dtag_old_vc,cnt);  
	//dtag_cont_put(DTAG_T_SRAM, dtag_new_vc,cnt);  
	return 0;  
}  

static DEFINE_UART_CMD(l2p_build_vac, "l2p_build_vac", "[0] show vac info [1] save new vac", "l2p_build_vac", 0, 1, l2p_build_vac);
#endif

#if (FW_BUILD_VAC_ENABLE == mENABLE)
ddr_code int FW_rebuild_vac_main(int argc, char *argv[]) 
{	 
	u32 cnt;  
	
	u32 *vc; 
	u32 *vc_recon;
	
	dtag_t dtag_old = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);  
	dtag_t dtag_recon = dtag_cont_get(DTAG_T_SRAM, cnt);

	vc_recon = dtag2mem(dtag_recon);  

	fw_vac_rebuild(vc_recon);
	
	//ftl_apl_trace(LOG_ALW, 0x8bc6, "fw rebuild cost : %d ms",time_elapsed_in_ms(time_cost));

	for(u16 i = 0; i < shr_nand_info.geo.nr_blocks; i++)
	{
		ftl_apl_trace(LOG_ALW, 0x4bd0, "spb : %d , recon_vac : 0x%x vc : 0x%x",i,vc_recon[i],vc[i]);
		if(vc_recon[i] != vc[i])
		{
			ftl_apl_trace(LOG_ALW, 0x0d5b,"[ERR] block : %d",i);
		}
	}

	dtag_cont_put(DTAG_T_SRAM, dtag_old, cnt);
	dtag_cont_put(DTAG_T_SRAM, dtag_recon, cnt);

	u64 time_cost = get_tsc_64();
	l2p_mgr_vcnt_rebuild();
	ftl_apl_trace(LOG_ALW, 0x3a16, "HW rebuild cost : %d ms",time_elapsed_in_ms(time_cost));
	return 0; 
} 
static DEFINE_UART_CMD(vc_build , "vc_build","vc_build","vc_build",0,0,FW_rebuild_vac_main); 
#endif

ddr_code int uart_vac_modify(int argc, char *argv[]) // vac test code, by Sunny
{
	if (argc >= 3)
    {
		u16 spb_id  = strtol(argv[1], (void *)0, 0);
		u32 vac_new = strtol(argv[2], (void *)0, 0);
        u32 vac_ori = 0;

        u32    *vc, cnt;
        dtag_t dtag;
        dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);

        vac_ori = vc[spb_id];
        vc[spb_id] = vac_new;

        ftl_apl_trace(LOG_ALW, 0xdf6e, "spb_id : 0x%x, original vac : 0x%x, modified vac : 0x%x",\
                      spb_id, vac_ori, vc[spb_id]);

        ftl_l2p_put_vcnt_buf(dtag, cnt, true);

        ftl_apl_trace(LOG_ALW, 0xb08b, "uart_vac_modify done");
    }
	else
	{
		ftl_apl_trace(LOG_ALW, 0x267b, "Invalid number of argument");
	}

	return 0;
}

static DEFINE_UART_CMD(vac_mod, "vac_mod", "vac_mod [BLK] [VAC]", "vac_mod",
					   0, 2, uart_vac_modify);

share_data_zi volatile u32 shr_p2l_grp_cnt; 
share_data_zi volatile u32 shr_wl_per_p2l; 

ddr_code int show_nand_info(int argc, char *argv[]) 
{	 

	ftl_apl_trace(LOG_ALW, 0xb4ce, "MAX NUM >>>blk %d page:%d ch:%d ce:%d lun:%d plane:%d", 
				shr_nand_info.geo.nr_blocks,shr_nand_info.geo.nr_pages,shr_nand_info.geo.nr_channels, 
				shr_nand_info.geo.nr_targets,shr_nand_info.geo.nr_luns,shr_nand_info.geo.nr_planes); 
	ftl_apl_trace(LOG_ALW, 0xa54d, "  COUNT >>>die:%d interleave:%d wl:%d group:%d shr_wl_per_p2l:%d", 
				shr_nand_info.lun_num,shr_nand_info.interleave,FTL_WL_CNT,shr_p2l_grp_cnt,shr_wl_per_p2l);	 
	ftl_apl_trace(LOG_ALW, 0x44e3, "SHIFT MASK>>>plane:%d,ch:%d,ce:%d,lun:%d,page:%d,block:%d", 
					shr_nand_info.pda_plane_shift,shr_nand_info.pda_ch_shift,shr_nand_info.pda_ce_shift, 
					shr_nand_info.pda_lun_shift,shr_nand_info.pda_page_shift,shr_nand_info.pda_block_shift);
	//ftl_apl_trace(LOG_ALW, 0xb826,"cpu msg idx:%d",CPU_MSG_PLP_CANCEL_DIEQUE);
	return 0; 
} 
static DEFINE_UART_CMD(show_nand_info , "show_nand","show_nand","num shift",0,0,show_nand_info); 
 #if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
ddr_code int show_slc_info(int argc, char *argv[]) 
{	 
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	ftl_apl_trace(LOG_ALW, 0x7aed, "pre_wl:%d wl:%d times:%d tag:%x format:%x power_on:%d",
		epm_ftl_data->plp_pre_slc_wl,epm_ftl_data->plp_slc_wl,epm_ftl_data->plp_slc_times,epm_ftl_data->plp_slc_gc_tag,epm_ftl_data->slc_format_tag,shr_lock_power_on);	 
	return 0; 
} 
static DEFINE_UART_CMD(slc_info , "slc_info","slc_info","slc_info",0,0,show_slc_info); 
#endif

ddr_code int epm_diebit(int argc, char *argv[])  
{	  
    ftl_apl_trace(LOG_ALW, 0x1d1a, "[IN] epm_diebit");
#if (PLP_SUPPORT == 0)     
    epm_FTL_t* epm_ftl_data; 
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag); 
    if(epm_ftl_data->host_open_blk[0] != INV_U16)
    {
        for(u16 i=0;i<SPOR_CHK_WL_CNT;i++) 
        { 
            if(epm_ftl_data->host_open_wl[i] != INV_U16)
            {
                ftl_apl_trace(LOG_ALW, 0x2b94, "host blk %d wl :%d die 0x%x",epm_ftl_data->host_open_blk[0],epm_ftl_data->host_open_wl[i],epm_ftl_data->host_die_bit[i]); 
            }
        }
    }
    if(epm_ftl_data->gc_open_blk[0] != INV_U16)
    {
        for(u16 i=0;i<SPOR_CHK_WL_CNT;i++) 
        { 
            if(epm_ftl_data->gc_open_wl[i] != INV_U16)
            {
                ftl_apl_trace(LOG_ALW, 0x60f6, "gc blk %d wl :%d die 0x%x",epm_ftl_data->gc_open_blk[0],epm_ftl_data->gc_open_wl[i],epm_ftl_data->gc_die_bit[i]);         
            }
       }
    }
#endif    
    ftl_apl_trace(LOG_ALW, 0x0390, "----------end----------"); 
	return 0;  
}  
static DEFINE_UART_CMD(diebit,"diebit","diebit","num diebit",0,0,epm_diebit); 



