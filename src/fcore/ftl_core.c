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

/*! \file ftl_core.c
 * @brief ftl core module, receive write data entries and save it to nand
 *
 * \addtogroup fcore
 * \defgroup fcore
 * \ingroup fcore
 * @{
 * receive write data entries, assign page resource and update mapping
 * manage GC data moving, RAID, ..., etc
 */
#include "fcprecomp.h"
#include "bitops.h"
#include "queue.h"
#include "event.h"
#include "bf_mgr.h"
#include "ncl_exports.h"
#include "die_que.h"
#include "scheduler.h"
#include "pstream.h"
#include "ftl_core.h"
#include "l2p_mgr.h"
#include "cbf.h"
#include "mpc.h"
#include "addr_trans.h"
#include "ftl_meta.h"
#include "ftl_export.h"
#include "rdisk.h"
#include "ipc_api.h"
#include "ftl_raid.h"
#include "idx_meta.h"
#include "l2cache.h"
#include "ftl_p2l.h"
#include "ns.h"
#include "srb.h"
#include "fw_download.h"
#include "erase.h"
#include "../ftl/ftl_l2p.h"
#include "../ftl/spb_mgr.h"
#include "console.h"
#include "pstream.h"    // ERRHANDLE_GLIST
#include "ftl_remap.h"
#include "../ftl/ErrorHandle.h"
#include "../ncl_20/read_retry.h"
#include "event.h"
#ifdef CPU_OFF_FOR_OTP
#include "misc.h" //_GENE_20211012
#include "misc_register.h" //_GENE_20211012
#endif
#include "nvme_spec.h"//joe add 20200610  need add the path of .h  in the CMAKE file in fcore folder
/*! \cond PRIVATE */
#define __FILEID__ fcore
#include "trace.h"
/*! \endcond */

#if MAX_SPBQ_PER_NS < FTL_CORE_MAX
#error "wrong"
#endif
extern bool fill_dummy_flag;

#ifdef ERRHANDLE_GLIST
share_data_zi volatile bool  FTL_INIT_DONE;
#endif

#ifdef NCL_RETRY_PASS_REWRITE
extern retry_rewrite_t rd_rew_info[DDR_RD_REWRITE_TTL_CNT];
#endif

#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
share_data_zi volatile bool delay_flush_spor_qbt; 
extern volatile u32 spor_qbtsn_for_epm; 
#endif

extern volatile bool PLA_FLAG;
extern volatile bool PLN_FLAG;
extern volatile bool PLN_in_low;
extern volatile bool PLN_flush_cache_end;
extern volatile bool PLP_IC_SGM41664;
extern volatile u32 SGM_data;
#if SYNOLOGY_SETTINGS
extern volatile bool gc_wcnt_sw_flag;
#endif


#define FTL_CORE_RES_TTL	((TOTAL_NS - 1) * FTL_CORE_MAX)

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL 
extern volatile u16 stripe_pdone_cnt[MAX_STRIPE_ID];
#endif
/*! @brief ftl core data structure */
/*	// Move to ftl_core.h, FET, EHDbgMsg
typedef struct _ftl_core_t {
	pstream_t ps;				///< physical stream
	core_wl_t cwl;				///< ftl core word-line
	cur_wr_pl_t cur_wr_pl;			///< current write payload
	stripe_user_t su;			///< raid stripe user
	struct timer_list idle_timer;		///< idle timer
	u32 data_in;				///< data in record
	u32 last_data_in;			///< last record for idle check

	u32 sn;					///< serial number
	u32 wreq_cnt;				///< write request count
    u32 parity_wreq_cnt;
	union {
		u8 all;
		struct {
			u8 prog_err_blk_in : 1;		///< data in blocked by program error
			u8 prog_err_blk_updt : 1;	///< update blocked by program error
			u8 wreq_pend : 1;		///< get wreq fail
		} b;
	} err;
} ftl_core_t;
*/
/*! @brief ftl core namesapce, each namespace has serveral ftl_core */
/* // DBG, RdErrGCHalt
typedef struct _ftl_core_ns_t {
	ftl_core_t *ftl_core[FTL_CORE_MAX];			///< ftl core object
	union {
		u32 all;
		struct {
			u32 clean : 1;				///< if table was clean
			u32 opening : 1;			///< if opening ns
		} b;
	} flags;

	u32 wreq_cnt;						///< write request count
	u32 parity_wreq_cnt;				///< raid parity write request count
	QSIMPLEQ_HEAD(_ftl_core_ctx_list_t, _ftl_core_ctx_t) pending;	///< pending flushing context
	ftl_flush_data_t *flushing_ctx;				///< outstanding flush context
	ftl_format_t *format_ctx;				///< outstanding format context
	QSIMPLEQ_HEAD(ns_pad_list_t, _ftl_core_ns_pad_t) ns_pad_list;	///< outstanding padding context to close a core wl
} ftl_core_ns_t;
*/
//#define MAX_WREQ_PER_SEG		2	///< max write request per segment
//#define CLOSE_WL_EXPIRE      60   // ERRHANDLE_GLIST, move to ftl_core.h
//#define CLOSE_LAYER_EXPIRE   5
//#define CLOSE_BLK_EXPIRE     144


// ISU, LJ1-337, PgFalClsNotDone (1)
/* Move to ftl_core.h
typedef struct {
	u8 wl_filled;
	u8 layer_filled;
	u8 blk_filled;
	u8 blk_filling;
	u16 wl_timer;			///< idle 1 min close open wl
	u16 layer_timer;		///< idle 5 min close open layer
	u16 blk_timer;			///< idle 12 hour close open block
	u32 wptr;
	spb_id_t spb_id;		///< spb id
}idle_chk_t;

#define MAX_SPBPAD_CNT 6  // max support to 32, since using_bmp is 32bit
typedef struct {
	idle_chk_t chk[FTL_CORE_MAX];
    ftl_spb_pad_t spb_pad[MAX_SPBPAD_CNT]; //[3][2]
    u32 using_bmp;
    u8 free_cnt;
    u8 evt_fill_dummy_task;
}ftl_core_open_blk_handle_t;
*/


#define MAX_WREQ_PER_SEG		3	///< max write request per segment

/*! @brief table resource data structure */
typedef struct _tbl_res_t {
	void *caller;				///< caller
	//u16 io_issued;				///< io issued in this resource
	//u16 io_in_res;				///< io in this resource
	//ncl_w_req_t *wreq[MAX_WREQ_PER_SEG];	///< finished write request
	u32 seg_bit;				///< flushing segment
	io_meta_t *io_meta;			///< meta pointer
	u32 meta_idx;				///< meta index
	void *restbl[QBT_SKIP_MODE_2SEG];
} tbl_res_t;

typedef CBF(u32, 256) wreq_seq_que_t;	///< write req sequence queue
share_data u32 WUNC_DTAG;

share_data_zi volatile ns_t ns[INT_NS_ID];			///< global nvme namespaces
share_data_zi spb_queue_t spb_queue[NS_SUPPORT][MAX_SPBQ_PER_NS];	///< free spb queues for host namespace, ftl core use this core to get allocated spb from FTL

share_data_zi spb_queue_t int_spb_queue[2];		///< free spb queue for internal namespace
share_data_zi volatile pda_t *shr_ins_lut;			///< lut of internal namespace
share_data_zi volatile u32 shr_dtag_ins_cnt;		///< how many dtag was inserted to l2p

fast_data_zi ftl_core_ns_t _fcns[INT_NS_ID];	///< ftl core namespace
fast_data_zi ftl_core_ns_t *fcns[INT_NS_ID + 1];	///< 0 is not used, last one is internal
fast_data_zi ftl_core_open_blk_handle_t open_ctrl;
share_data_zi volatile fcns_attr_t fcns_attr[INT_NS_ID + 1];///< fc namespace attribute

fast_data bool plp_ask_close_open_wl = false;
fast_data_zi u32 pg_cnt_in_slc_spb;		///< pda number in SLC SPB
fast_data_zi u32 pg_cnt_in_xlc_spb;		///< pda number in XLC SPB
fast_data_zi u32 width_nominal;			///< interleave, #CH *#DIE * #PLN
fast_data_zi u16 spb_total;			///< total SPB number
fast_data_zi u8 width_in_dws;			///< hook to Dword
fast_data_zi u8 tbl_io_per_seg;			///< ncl write request per segment
fast_data_zi u32 die_id_mask;			///< mask for PDA die id
share_data volatile enum du_fmt_t host_du_fmt;		///< host du format, init in FTL
share_data_zi spb_rt_flags_t *spb_rt_flags;	///< runtime flags of SPB
share_data_zi volatile spb_id_t       host_spb_close_idx;/// runtime close blk idx   Curry
share_data_zi volatile spb_id_t       gc_spb_close_idx;/// runtime close blk idx   Curry
share_data_zi volatile u8  QBT_BLK_CNT;

fast_data_ni ftl_core_union_t fc_unions[MAX_FTL_CORE_CTX];
fast_data_ni pool_t ftl_core_ctx_pool;		///< ftl core context for operations
fast_data_ni u32 ftl_core_ctx_cnt;

fast_data_zi static tbl_res_t _tbl_res[MAX_FTL_TBL_IO];	///< table flush resource data array
fast_data_zi static pool_t _tbl_res_pool;		///< table flush resource pool
share_data_ni volatile l2p_updt_que_t l2p_updt_que;
share_data_ni volatile l2p_updt_ntf_que_t l2p_updt_ntf_que;
share_data_ni volatile l2p_updt_que_t l2p_updt_done_que;
share_data dtag_free_que_t dtag_free_que;
fast_data_zi ftl_core_t ftl_core_res[FTL_CORE_RES_TTL];
fast_data_zi pool_t ftl_core_res_pool;
share_data volatile u16 need_page;

fast_data_zi fc_flags_t fc_flags;
fast_data u8 reset_ack_req_tx = 0xFF;		///< record reset ack req tx in ipc

fast_data u8 evt_fc_ready_chk = 0xFF;			///< event to check fc status
fast_data u8 evt_fc_qbt_ready_chk = 0xFF;
fast_data u8 evt_fc_qbt_resume = 0xFF;
fast_data u8 evt_fc_misc_resume = 0xFF; // Howard
fast_data u8 evt_fc_shutdown_chk = 0xFF;
extern volatile bool shr_qbt_prog_err;
fast_data ftl_core_ctx_t *qbt_core_ctx = NULL;
fast_data_zi bool tbl_flush_block = false;
fast_data_zi ftl_flush_tbl_t *tbl_flush_ctx = NULL;
fast_data_ni wreq_seq_que_t wreq_seq_que[FTL_CORE_MAX];
share_data_zi volatile bool shr_format_fulltrim_flag;

fast_data u8 evt_save_pbt_param = 0xff;
fast_data u8 evt_format_wreq = 0xff;
tbl_res_t *qbt_restbl_tmp[QBT_SKIP_MODE_2SEG];
tbl_res_t *misc_restbl_tmp[QBT_SKIP_MODE_2SEG];
extern u32 dtag_recv;				///< how many dtag were received in ftl_core
share_data_zi volatile u8 plp_trigger;
share_data_zi volatile u8 plp_epm_update_done;
share_data_zi volatile u8 plp_epm_back_flag;
share_data volatile bool shr_shutdownflag;
share_data volatile bool _fg_warm_boot;
fast_data_zi static u64 plp_time_start = 0;
fast_data_zi u32 parity_allocate_cnt = 0;
fast_data u8  t_cpage[TOTAL_NS][FTL_CORE_MAX];
fast_data u8  t_tpage[TOTAL_NS][FTL_CORE_MAX];
fast_data u8  meta_ofst = 2;
fast_data u8  blist_getwpl = false;
fast_data_zi ftl_flush_misc_t *blist_flush_ctx[2];
share_data_ni volatile close_blk_ntf_que_t close_host_blk_que;
share_data_ni volatile close_blk_ntf_que_t close_gc_blk_que;
fast_data_zi ftl_flush_data_t *fctx_addr;

share_data_zi volatile ResetHandle_t smResetHandle;

fast_data u8 evt_reset_ack = 0xFF;		///< event to trigger gc pend out
fast_data u8 evt_force_pbt = 0xFF;
fast_data u8 evt_plp = 0xFF;
fast_data u8 evt_plp_done_chk = 0xFF;

//fast_data u8 evt_seg_flush = 0xFF;
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
fast_data u8 evt_save_fake_qbt = 0xFF; 
#endif
fast_data u8 evt_pout_issue = 0xFF;
share_data    epm_info_t*  shr_epm_info;
slow_data_zi  dtag_t       fmt_vac_dtag;
extern volatile u8 shr_format_copy_vac_flag;
extern volatile bool shr_trim_vac_save_done;


extern u16 host_dummy_start_wl;

#if PLP_TEST
extern bool ucache_flush_flag;
extern u16 die;
#endif
share_data_zi volatile bool shr_qbtfilldone;
share_data_zi volatile bool plp_gc_suspend_done;
share_data_zi volatile spb_id_t rd_open_close_spb[FTL_CORE_GC + 1];

typedef struct
{
    fsm_ctx_t fsm;

    plp_spb_pad_t plp_spb_pad;
    ftl_spb_pad_t spb_pad;
    erase_ctx_t erase_ctx;

}plp_fsm_t;
fast_data_zi bool plp_close_wl_done;
fast_data_zi bool plp_save_epm_done;
fast_data_zi u8 pending_parity_cnt;
fast_data_zi plp_fsm_t plp_fsm;
fast_data_zi u32 plp_gc_pad_status = 0;

#if (PLP_FORCE_FLUSH_P2L == mENABLE)
fast_data_zi u32 plp_lda_dtag_w_idx = 0; 
fast_data_zi u8  plp_p2l_group_idx  = INV_U8;  
extern  volatile spb_id_t plp_spb_flush_p2l;
#endif
fast_data_zi u32 seg_w_blist[3]; //add by Jay to record pbt seg in first page
fast_data    u32 cur_pbt_sn = INV_U32;

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
fast_data u32 write_type   = FTL_CORE_NRM; 
fast_data u32 write_nsid   = 1; 
fast_data u16 SLC_dummy_wl = 0xFFFF;
fast_data u32 SLC_gc_blk_sn = INV_U32;
extern volatile u8 SLC_init;
extern volatile u32 max_usr_blk_sn;

#endif
#if (PLP_SUPPORT == 0)
extern volatile bool shr_nonplp_gc_state;
#endif
#if 0//def ERRHANDLE_GLIST    // Should be move into header file later.
share_data_zi bool  fgFail_Blk_Full;        // need define in sram
share_data_zi sGLTable *pGList;             // need define in Dram  // GL_mod, Paul_20201130
share_data_zi u16   wOpenBlk[MAX_OPEN_CNT]; // need define in Dram
share_data_zi u8    bErrOpCnt;
share_data_zi u8    bErrOpIdx;
share_data_zi bool  FTL_INIT_DONE;

share_data_zi ncl_w_req_t *CloseReq;
share_data_zi u32 *CloseAspb;
share_data_zi u32 ClSBLK_H;									//host close blk

share_data_zi sGLEntry errInfo2;
share_data_zi sGLEntry errInfo4;
share_data_zi sEH_Manage_Info sErrHandle_Info;
share_data_zi MK_FailBlk_Info Fail_Blk_Info_Temp;
share_data_zi u8 bFail_Blk_Cnt;                               //need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];
#endif

share_data void *shr_ddtag_meta;
fast_data_zi io_meta_t temp_meta;    //temp meta( in BTCM) for meta input
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
share_data    epm_info_t*  shr_epm_info;
extern u32 shr_spb_info_ddtag;
#endif

fast_data_zi u8 avoid_fcore_flush_hang;

#if PLP_DEBUG
share_data volatile u32 fill_up_dtag_start;
share_data volatile io_meta_t * fill_up_dtag_meta;


void plp_debug_dtag_alloc(void);
bool plp_init(void);
#endif
void cwl_init(core_wl_t *cwl);
void plp_evt_check(u32 p0, u32 p1, u32 p2);
void plp_evt_done_check(u32 p0, u32 p1, u32 p2);

void evt_flush_spor_qbt(u32 p0, u32 p1, u32 p2); 


fast_code bool fill_dummy_check(void);

#if (PBT_OP == mENABLE)
share_data_zi volatile pbt_resume_t *pbt_resume_param; 
fast_data_ni TFtlPbftType tFtlPbt;
void ftl_pbt_init(bool force_mode);
#endif


static void fcore_idle_check(void *data);
static void fcore_gc_idle_check(void* data);

/*!
 * @brief setup meta buffer for ncl request
 *
 * @param req		ncl request for write request
 * @param total		total du count in request
 * @param sn		serial number of this request
 *
 * @return		padding count
 */
#ifdef LJ_Meta
static u32 ftl_core_setup_meta(ncl_w_req_t *req, cur_wr_pl_t *wr_pl, u8 ftl_blk_loop);
#else
static u32 ftl_core_setup_meta(ncl_w_req_t *req, u32 total, u32 sn);
#endif

/*!
 * @brief flush table segments, the progress was described in ctx
 *
 * @param ctx	flush table context
 *
 * @return	always return false
 */
//static bool ftl_core_seg_flush(ftl_flush_tbl_t *ctx);

/*!
 * @brief get table operation resource
 *
 * @return	return table operation resource or NULL
 */
static inline tbl_res_t *ftl_get_tbl_res(void)
{
#if (PBT_OP == mENABLE)
	tFtlPbt.tbl_res_pool_cnt --;
#endif
	return (tbl_res_t *) pool_get_ex(&_tbl_res_pool);
}

/*!
 * @brief put table operation resource
 *
 * @param res	table operation resource to be put
 *
 * @return	not used
 */
static inline void ftl_put_tbl_res(tbl_res_t *res)
{
	#if (PBT_OP == mENABLE)
	tFtlPbt.tbl_res_pool_cnt ++;
	#endif
	pool_put_ex(&_tbl_res_pool, (void *) res);
}

/*!
 * @brief insert pda read to reserved die queue
 *
 * @param r0		not used
 * @param entry	L2P Engine update cmpl entry
 * @param cnt		cmpl entry count
 *
 * @return             not used
 */
void host_l2p_pda_updt_done(u32 r0, u32 entry, u32 cnt);

/*!
 * @brief table valid count update done
 *
 * @param p0		not used
 * @param entries	entries pointer
 * @param count	cmpl entry count
 *
 * @return		not used
 */
void tbl_vcnt_upd_done(u32 p0, u32 entries, u32 count);

/*!
 * @brief host data l2p update
 *
 * @return		not used
 */
void host_pda_updt(void);

#if (PBT_OP == mENABLE)
share_data_ni volatile pbt_updt_ntf_que_t pbt_updt_ntf_que;
slow_data_zi struct du_meta_fmt *ddtag_tbl_meta;
fast_data u64 tickupt = 0;
fast_data u32 ftl_io_ctl = 1;
fast_data u32 ftl_wcnt_ctl = 6;
fast_data u32 flush_time_gap = 0;
fast_data u32 flush_io_each  = 0;
fast_data u32 raid_cnt = 0;
fast_data u32 raidR = 0;
#if(BG_TRIM == ENABLE)
share_data_zi bool BG_TRIM_HANDERING;
#endif

bool ftl_core_pbt_seg_flush(void);
bool ftl_is_ftltbl_ready_dump(void);



slow_code void pbt_updt_init(void)
{
	CBF_INIT(&pbt_updt_ntf_que);
	pbt_updt_recv_hook((cbf_t *) &pbt_updt_ntf_que, pbt_flush_proc);

}

fast_code inline void FTL_PBT_Update(u32 req_cnt)
{
	extern volatile u8 plp_trigger;
	if(tFtlPbt.force_pbt_halt || plp_trigger){
		return;
	}
    tFtlPbt.total_write_tu_cnt += req_cnt;
	if((nand_info.interleave<<2) <= tFtlPbt.total_write_tu_cnt){
		tFtlPbt.total_write_page_cnt++;
		tFtlPbt.total_write_tu_cnt -= (nand_info.interleave<<2);
	}

	return;
	/*
	if(tFtlPbt.pbt_flush_page_cnt <= tFtlPbt.total_write_page_cnt){
		//if(ftl_is_ftltbl_ready_dump())
		{
			u64 tick_tmp = time_elapsed_in_ms(tickupt);
			if(tick_tmp > flush_time_gap)
			{
				bool ret;
				CBF_INS(&pbt_updt_ntf_que, ret, 1);
				sys_assert(ret == true);
				tickupt = get_tsc_64();
			}
		}
	}
	*/

}

slow_code void set_evt_force_pbt(u32 mode)
{
    evt_set_cs(evt_force_pbt, true, mode, CS_TASK);
}

fast_data u8 force_close_flag = true;
fast_data u8 force_closing_flag = false;

// ISU, LJ1-337, PgFalClsNotDone (2)
fast_code void err_force_close_pbt_done(ftl_core_ctx_t *ctx)
{
    force_closing_flag = false;
    force_close_flag = false;
	fill_dummy_flag = 0;

    // ISU, EHPerfImprove (2)
    ftl_spb_pad_t *spb_pad = (ftl_spb_pad_t *)ctx;
    u32 mode = (u32)spb_pad->ctx.caller;  // ERRHANDLE_GLIST
	evt_set_cs(evt_force_pbt, true, mode, CS_TASK);
}

slow_code void fcore_force_pbt_timer(void* data){

	if(tFtlPbt.force_dump_pbt){
		bool ret;
		CBF_INS(&pbt_updt_ntf_que, ret, 1);
		if(ret == false){
		}
		if(tFtlPbt.force_dump_pbt_mode == DUMP_PBT_SHUTTLE	|| \
			tFtlPbt.force_dump_pbt_mode == DUMP_PBT_RD		|| \
			tFtlPbt.force_dump_pbt_mode == DUMP_PBT_BGTRIM  || \
			tFtlPbt.force_dump_pbt_mode == DUMP_PBT_WARMBOOT){	// ISU, EHPerfImprove (2)
			mod_timer(&fcns[2]->ftl_core[2]->idle_timer, jiffies + HZ/5);
		}else{
			mod_timer(&fcns[2]->ftl_core[2]->idle_timer, jiffies + 5*HZ);
		}
	}
}

fast_data_zi bool flag_need_clear_api;
slow_code void ftl_set_force_dump_pbt(u32 param, u32 force_start, u32 mode){
	if(force_start == true){
        if (shr_shutdownflag) 
        { 
            return; 
        } 
	    //if(tFtlPbt.force_dump_pbt && tFtlPbt.force_dump_pbt_mode == mode && (mode == DUMP_PBT_RD || mode == DUMP_PBT_SHUTTLE)){// ISU, EHPerfImprove (2)
	    if(tFtlPbt.force_dump_pbt && tFtlPbt.force_dump_pbt_mode == mode && (mode == DUMP_PBT_RD || mode == DUMP_PBT_BGTRIM)){
        	return;
        }
		//if(mode == DUMP_PBT_FORCE_FINISHED_CUR_GROUP || mode == DUMP_PBT_RD || mode == DUMP_PBT_SHUTTLE){	//other mode not support // ISU, EHPerfImprove (2)
		if(mode == DUMP_PBT_FORCE_FINISHED_CUR_GROUP || mode == DUMP_PBT_RD || mode == DUMP_PBT_WARMBOOT){
			tFtlPbt.force_dump_pbt = true;
			tFtlPbt.force_dump_pbt_mode = mode;
			if(mode == DUMP_PBT_RD){
                flag_need_clear_api = true;
            }
            else if (mode == DUMP_PBT_WARMBOOT)
            {
                ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT];
                u8 force_wl = 2;
                tFtlPbt.pbt_force_end_wptr = fc->ps.aspb[fc->ps.curr].wptr + force_wl * nand_info.interleave * nand_info.bit_per_cell;
                if ((tFtlPbt.pbt_force_end_wptr > fc->ps.aspb[fc->ps.curr].max_ptr) && (fc->ps.aspb[fc->ps.curr].max_ptr != 0)){
                    tFtlPbt.pbt_force_end_wptr = fc->ps.aspb[fc->ps.curr].max_ptr;
                }
                ftl_core_trace(LOG_INFO, 0x5bbb, "[PBT]force_mode:%u, current wptr:%u, max_ptr:%u, pbt_force_end_wptr:%u",
                    tFtlPbt.force_dump_pbt_mode, fc->ps.aspb[fc->ps.curr].wptr, fc->ps.aspb[fc->ps.curr].max_ptr, tFtlPbt.pbt_force_end_wptr);
            }
			ftl_core_trace(LOG_INFO, 0x2009, "[PBT]force_mode:%d",tFtlPbt.force_dump_pbt_mode);
			mod_timer(&fcns[2]->ftl_core[2]->idle_timer, jiffies + HZ/10);
		} else if (mode == DUMP_PBT_NORMAL_PARTIAL || mode == DUMP_PBT_SHUTTLE || mode == DUMP_PBT_BGTRIM){
			//bool ret;
			ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT];
			if(fc->ps.aspb[fc->ps.curr].spb_id != INV_U16 || fc->ps.aspb[fc->ps.curr].wptr != 0){
				if(force_close_flag == true){
					if(!force_closing_flag){
						force_closing_flag = true;
						ftl_set_pbt_halt(true);
                        if(tFtlPbt.isfilldummy)
                        {
                            // Keep this set of PBT spb
						    fcore_fill_dummy(FTL_CORE_PBT, FTL_CORE_PBT,FILL_TYPE_BLK, err_force_close_pbt_done, (void*)mode);
                        }
                        else
                        {
						    fcore_fill_dummy(FTL_CORE_PBT, FTL_CORE_PBT,FILL_TYPE_LAYER, err_force_close_pbt_done, (void*)mode);
                        }
					}
					//evt_set_cs(evt_force_pbt, true, true, CS_TASK);	// ISU, LJ1-337, PgFalClsNotDone (2)
					return;
				}
				if(fc->wreq_cnt != 0){
					evt_set_cs(evt_force_pbt, true, mode, CS_TASK); 
					return;
				}
				pbt_release(fc->ps.aspb[fc->ps.curr].spb_id);
				cwl_init(&fc->cwl);
				fc->cwl.cur.mpl_qt = 0;
				ftl_core_clean(INT_NS_ID);
				fc->wreq_cnt = 0;
				pstream_reset(&fc->ps);
				if(CBF_EMPTY(fc->ps.spb_que) && fc->ps.flags.b.queried == 0 && fc->ps.flags.b.query_nxt == 0 && pbt_query_ready){
					fc->ps.flags.b.query_nxt = 1;
					pstream_rsvd(&fc->ps);
				}
			}
			ftl_set_pbt_halt(false);
			force_close_flag = true;
			ftl_set_pbt_filldummy_done();
            ftl_core_trace(LOG_INFO, 0x4376, "[Prev] pbt_cur_loop:%u, isfilldummy:%u, cur_seg_ofst:%u", 
                tFtlPbt.pbt_cur_loop, tFtlPbt.isfilldummy, tFtlPbt.cur_seg_ofst);

            // Keep this set of PBT spb, which is also to prevent spb_release() from releasing the previous set of PBT.
            if (!tFtlPbt.isfilldummy)
            {
    			if(tFtlPbt.pbt_cur_loop != 0)
    				tFtlPbt.pbt_cur_loop = FTL_PBT_BLOCK_CNT * ((tFtlPbt.pbt_cur_loop -1) / FTL_PBT_BLOCK_CNT);// 0 2

    			shr_pbt_loop = tFtlPbt.pbt_cur_loop;
            }
            
			ftl_pbt_init(true);

			tFtlPbt.force_dump_pbt = true;
			tFtlPbt.force_dump_pbt_mode = mode;
			//flush_io_each = tFtlPbt.pbt_flush_total_cnt/ftl_io_ctl/6;
			//CBF_INS(&pbt_updt_ntf_que, ret, 1);
			//if(ret == false){
			//	//sys_assert(ret == true);
			//}

			// Speed up backup PBT for SPOR, ISU, EHPerfImprove, Paul_20220221
			if (mode == DUMP_PBT_SHUTTLE)
				flush_io_each = nand_info.geo.nr_channels;									// Prog 8 dice a round.

			mod_timer(&fcns[2]->ftl_core[2]->idle_timer, jiffies + HZ/10);
		}
	}else{
		tFtlPbt.force_dump_pbt = false;
		tFtlPbt.force_dump_pbt_mode = DUMP_PBT_REGULAR;

        //flush_io_each = tFtlPbt.pbt_flush_total_cnt/ftl_io_ctl;
		if(otf_forcepbt){
			otf_forcepbt  = false;
			//shr_host_write_cnt = shr_host_write_cnt_old;
		}
		tFtlPbt.force_cnt = 0;

		CBF_MAKE_EMPTY(&pbt_updt_ntf_que);
		//ftl_core_trace(LOG_INFO, 0, "[PBT]force_clear:%d",tFtlPbt.force_dump_pbt_mode);
		if(flag_need_clear_api){
            flag_need_clear_api = false;
    	    cpu_msg_issue(2, CPU_MSG_CLEAR_API, 0, 0);
        }
        #if(BG_TRIM == ENABLE)
        if(BG_TRIM_HANDERING){
            cpu_msg_issue(0, CPU_MSG_FORCE_PBT_CPU1_ACK, 0, 0);
        }
        #endif
		// Set it back, ISU, EHPerfImprove, Paul_20220221
		flush_io_each = ftl_io_ctl;
	}
	//ftl_core_trace(LOG_INFO, 0, "[PBT]set_force:%d, mode:%d",force_start,tFtlPbt.force_dump_pbt_mode);

	/*enable/disable spor use 2 group pbt build*/
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(epm_ftl_data->pbt_force_flush_flag != mode)
	{
		epm_ftl_data->pbt_force_flush_flag = mode;
		epm_update(FTL_sign,(CPU_ID-1)); 
	}


	ftl_core_trace(LOG_INFO, 0x9163, "[PBT]set_force:%d, force_mode:%d, flush_io_each:%d loop_idx:%d", force_start, tFtlPbt.force_dump_pbt_mode, flush_io_each,tFtlPbt.pbt_cur_loop);
}

fast_code inline void pbt_flush_proc(void){
	u32 tmp;
	CBF_FETCH(&pbt_updt_ntf_que,tmp);
	sys_assert(tmp);

	if((tFtlPbt.force_dump_pbt == mTRUE)&&(ftl_is_ftltbl_ready_dump() == mTRUE)){
		tFtlPbt.dump_pbt_start = mTRUE;
		ftl_core_pbt_seg_flush();
		tFtlPbt.dump_pbt_start = mFALSE;
		if ((tFtlPbt.force_dump_pbt_mode == DUMP_PBT_NORMAL_PARTIAL) || (tFtlPbt.force_dump_pbt_mode == DUMP_PBT_WARMBOOT)){
			bool ret;
	 		CBF_INS(&pbt_updt_ntf_que, ret, 1);
	 		if(ret == false){}

            if (tFtlPbt.force_dump_pbt_mode == DUMP_PBT_WARMBOOT)
            {
                ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT];
                if (tFtlPbt.pbt_force_end_wptr <= fc->ps.aspb[fc->ps.curr].wptr)
                {
                    ftl_set_force_dump_pbt(0,false,DUMP_PBT_REGULAR);
                    ftl_core_trace(LOG_INFO, 0x445e, "spb:%u, wptr:%u(wl:%u offset:%u), pbt_force_end_wptr:%u", 
                        fc->ps.aspb[fc->ps.curr].spb_id, fc->ps.aspb[fc->ps.curr].wptr,
                        fc->ps.aspb[fc->ps.curr].wptr/(nand_info.interleave*nand_info.bit_per_cell), 
                        fc->ps.aspb[fc->ps.curr].wptr%(nand_info.interleave*nand_info.bit_per_cell),tFtlPbt.pbt_force_end_wptr);
                }
            }
            
		}
	}else{
		if(tFtlPbt.pbt_flush_page_cnt <= tFtlPbt.total_write_page_cnt){
			if(ftl_is_ftltbl_ready_dump())
			{
				tFtlPbt.dump_pbt_start = mTRUE;
				ftl_core_pbt_seg_flush();
				tFtlPbt.dump_pbt_start = mFALSE;
			}else{
				bool ret;
				CBF_INS(&pbt_updt_ntf_que, ret, 3);
				if(ret == false){
				}
			}
		}
	}
}

slow_code void ftl_core_pbt_fill_done(ftl_core_ctx_t *ctx){
	ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT];
	ftl_set_pbt_filldummy_done();
	spb_release(fc->ps.aspb[fc->ps.curr].spb_id);
	fc->cwl.cur.mpl_qt = 0;
	ftl_core_clean(INT_NS_ID);
	delay();
	sys_assert(fc->wreq_cnt == 0);
	pstream_reset(&fc->ps);
	if(CBF_EMPTY(fc->ps.spb_que) && fc->ps.flags.b.queried == 0 && fc->ps.flags.b.query_nxt == 0){
		fc->ps.flags.b.query_nxt = 1;
		pstream_rsvd(&fc->ps);
	}
}

#if RAID_SUPPORT
    extern raid_id_mgr_t raid_id_mgr;
#endif
fast_code bool ftl_core_pbt_seg_flush(void)
{
	u32 seg_end = ns[0].seg_end;
	u32 dtag_base = 0;
	cur_wr_pl_t *wr_pl = NULL;
    u8	loop = 0;
	u8  i;
	u8  flushdone = false, flushed_flag = true, start_new_wl = true;
    pda_t pda = INV_PDA;
	#ifndef LJ_Meta
	pda_t pda;
	#endif

#ifdef While_break
	u64 start = get_tsc_64();
#endif
    if (pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt)
    {
        start_new_wl = false;
    }

	do {
		dtag_base = shr_l2p_entry_start + shr_l2pp_per_seg * tFtlPbt.cur_seg_ofst;

		for (i = 0; i < shr_l2pp_per_seg; i++) {
			if (wr_pl == NULL){
				wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_PBT, WPL_V_MODE, start_new_wl, NULL, NULL);
				if (wr_pl == NULL){
                    flushed_flag = false;
					goto flush_end;
				}
			}

			if(tFtlPbt.filldummy_done == mTRUE && tFtlPbt.isfilldummy == mFALSE){
                tFtlPbt.fill_wl_cnt = 0; 
				tFtlPbt.cur_seg_ofst 	= 0;
				tFtlPbt.filldummy_done 	= mFALSE;
				dtag_base = shr_l2p_entry_start + shr_l2pp_per_seg * tFtlPbt.cur_seg_ofst;
			}
			/*
			if(tFtlPbt.cur_seg_ofst<2){
				u32 page_idx = wr_pl->cnt >> DU_CNT_SHIFT;
				u32 du_offst = wr_pl->cnt & (DU_CNT_PER_PAGE - 1);
				pda_t pda = wr_pl->pda[page_idx] + du_offst;
				ftl_core_trace(LOG_INFO, 0, "[PBT]pda:0x%x",pda);
			}
			*/

			#ifndef LJ_Meta
			//res->io_meta[i].sn = 0;
			u32 page_idx = wr_pl->cnt >> DU_CNT_SHIFT;
			u32 du_offst = wr_pl->cnt & (DU_CNT_PER_PAGE - 1);
			pda = wr_pl->pda[page_idx] + du_offst;
			#endif
			wr_pl->lda[wr_pl->cnt] = PBT_LDA;
			wr_pl->pl[wr_pl->cnt].pl.du_ofst = i;

#if RAID_SUPPORT
            if(wr_pl->flags.b.parity_mix)
                wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (raid_id_mgr.pad_meta_idx[wr_pl->stripe_id] + wr_pl->cnt);
            else
#endif
                wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (_tbl_res[0].meta_idx + wr_pl->cnt);

			if(tFtlPbt.isfilldummy){
			wr_pl->pl[wr_pl->cnt].pl.dtag = WVTAG_ID;
			}else{
			wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | (dtag_base + i);
			}
			wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			wr_pl->cnt++;
			wr_pl->flags.b.ftl = 1;

            if (wr_pl->cnt == wr_pl->max_cnt) { 
				ncl_w_req_t *req = ftl_core_submit(wr_pl, NULL, NULL);  
                pda = wr_pl->pda[0] + (DU_CNT_PER_PAGE - 1);
				wr_pl = NULL;  
                //if (req && ((pda2die(pda) + 1) % nand_info.geo.nr_channels == 0))
                if (req)    
                {
                    loop++;
                }
			} 

		}
		tFtlPbt.cur_seg_ofst++;
		if((tFtlPbt.cur_seg_ofst % (tFtlPbt.pbt_flush_total_cnt) == 0)){
			flushdone = true;
		}

		if(tFtlPbt.isfilldummy == mFALSE){
			//tFtlPbt.cur_seg_ofst++;
			if(tFtlPbt.cur_seg_ofst >= seg_end){
				tFtlPbt.isfilldummy = mTRUE; 
                 ftl_core_trace(LOG_INFO, 0x1cd1, "last table pda: 0x%x, wl: %u, die: %u, fill_wl_cnt: %u", pda, pda2page(pda) / 3, pda2die(pda), tFtlPbt.fill_wl_cnt);
				//fcore_fill_dummy(FTL_CORE_PBT, FTL_CORE_PBT,FILL_TYPE_LAYER, ftl_core_pbt_fill_done, NULL);
				//break;
			}
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	} while (loop < flush_io_each);
flush_end:
	if((tFtlPbt.cur_seg_ofst % (tFtlPbt.pbt_flush_total_cnt) == 0) || flushdone){
		if(tFtlPbt.force_dump_pbt == false)
			tFtlPbt.total_write_page_cnt -= tFtlPbt.pbt_flush_page_cnt;
        #if (PBT_DEBUG == mENABLE) 
		ftl_core_trace(LOG_INFO, 0xf232, "[PBT] cur_seg_ofst:%u, total_write_page_cnt:%d, page: %u, die:%u",
		    tFtlPbt.cur_seg_ofst, tFtlPbt.total_write_page_cnt, pda2page(pda), pda2die(pda));  
        #endif
	}else{
		bool ret;
		CBF_INS(&pbt_updt_ntf_que, ret, 5);
		if(ret == false){
		}
	}
    return flushed_flag;
}


slow_code void ftl_pbt_supply_init(void){
	if(tFtlPbt.isfilldummy == mTRUE){
		tFtlPbt.cur_seg_ofst 		 = 0;
        tFtlPbt.fill_wl_cnt          = 0; 
		tFtlPbt.total_write_tu_cnt	 = 0;
		tFtlPbt.total_write_page_cnt = 0;
        ftl_clear_pbt_cover_usr_cnt();
	}
}


slow_code void ftl_pbt_init(bool force_mode){
    tFtlPbt.cur_seg_ofst            = 0;
    tFtlPbt.force_dump_pbt          = mFALSE;
    tFtlPbt.force_dump_pbt_mode     = DUMP_PBT_REGULAR;
    tFtlPbt.pbt_force_end_wptr      = INV_U32;
    tFtlPbt.dump_pbt_start          = mFALSE;
    tFtlPbt.pbt_res_ready           = mTRUE;
    tFtlPbt.isfilldummy             = mFALSE;
    tFtlPbt.filldummy_done          = mFALSE;
    tFtlPbt.fill_wl_cnt             = 0; 
    tFtlPbt.pbt_spb_cur             = INV_U16;
    tFtlPbt.pbt_cover_usr_cnt       = 0;
    tFtlPbt.pbt_flush_IO_cnt        = 0;
    tFtlPbt.total_write_page_cnt    = 0;
    tFtlPbt.total_write_tu_cnt      = 0;
    tFtlPbt.force_cnt               = 0;
	otf_forcepbt 					= mFALSE;
    if(force_mode == false){
        tFtlPbt.pbt_flush_total_cnt     = ((nand_info.interleave*4*3/shr_l2pp_per_seg)*ftl_wcnt_ctl);
        tFtlPbt.pbt_flush_page_cnt      = PBT_RATIO*ftl_wcnt_ctl;
        tFtlPbt.tbl_res_pool_cnt        = ftl_io_ctl;
        tFtlPbt.force_pbt_halt          = false;
        flush_io_each                   = ftl_io_ctl;
        #if 0 //(SPOR_FLOW == mENABLE)
        //tFtlPbt.pbt_cur_loop            = (shr_pbt_loop!=INV_U8)? shr_pbt_loop:0;
        #else
        tFtlPbt.pbt_cur_loop            = 0;    // 1 base
        if (_fg_warm_boot == false)
        {
            shr_pbt_loop = tFtlPbt.pbt_cur_loop;
        }
        #endif
	 if(FTL_INIT_DONE == false){
	    pbt_updt_init();
		tickupt = get_tsc_64();
        raidR = nand_info.lun_num * tFtlPbt.pbt_flush_page_cnt; 
	    evt_register(ftl_set_force_dump_pbt, 0, &evt_force_pbt);
	    //pbt_Pg_fail_blk = INV_U16;	// FET, RmUselessVar
	    //Pg_fail_blk = INV_U16;
	  }
	ftl_set_force_dump_pbt(0,false,DUMP_PBT_REGULAR);
    }
    ftl_core_trace(LOG_INFO, 0xbf91, "[PBT]ttl_seg:%d, ratio:%d, gap:%d, res_cnt:%d, l2pp_per_seg:%d, flushio:%d",ns[0].seg_end,PBT_RATIO,tFtlPbt.pbt_flush_page_cnt,tFtlPbt.tbl_res_pool_cnt,shr_l2pp_per_seg,tFtlPbt.pbt_flush_total_cnt);
}


static ddr_code int pbt_main(int argc, char *argv[])
{
	ftl_core_trace(LOG_INFO, 0xd83a, "ofst:%d, dump_pbt_start:%d, dummy:%d, res_ready:%d",tFtlPbt.cur_seg_ofst,tFtlPbt.dump_pbt_start,tFtlPbt.isfilldummy,tFtlPbt.pbt_res_ready);
	ftl_core_trace(LOG_INFO, 0xca79, "flushio:%d, dummydone:%d,tbl_res_pool_cnt:%d",tFtlPbt.pbt_flush_IO_cnt,tFtlPbt.filldummy_done,tFtlPbt.tbl_res_pool_cnt);
	return 0;
}

static DEFINE_UART_CMD(pbtinfo, "pbt","pbt","dump pbt info",0, 0, pbt_main);

#if 0
static slow_code int pbt_gap_main(int argc, char *argv[])
{
	u64 gap_tmp = flush_time_gap;
	flush_time_gap = (u64) atoi(argv[1]);
	ftl_core_trace(LOG_INFO, 0xddd1, "modify gap: %d -> %d",gap_tmp,flush_time_gap);
	return 0;
}

static DEFINE_UART_CMD(pbtgap, "pbtgap","pbt","modify pbt flush gap",1, 1, pbt_gap_main);

static slow_code int pbt_io_main(int argc, char *argv[])
{
	u64 io_tmp		= ftl_io_ctl;
	ftl_io_ctl		= (u64) atoi(argv[1]);
	flush_io_each	= tFtlPbt.pbt_flush_total_cnt/ftl_io_ctl;
	ftl_core_trace(LOG_INFO, 0xed17, "modify io: %d -> %d, each:%d",io_tmp,ftl_io_ctl,flush_io_each);
	return 0;
}

static DEFINE_UART_CMD(pbtio, "pbtio","pbt","modify pbt io",1, 1, pbt_io_main);


static slow_code int pbt_wcnt_main(int argc, char *argv[])
{
	u64 io_tmp = ftl_wcnt_ctl;
	ftl_wcnt_ctl = (u64) atoi(argv[1]);
	tFtlPbt.pbt_flush_total_cnt = ((ns[0].seg_end/(1152)))*ftl_wcnt_ctl;
	tFtlPbt.pbt_flush_page_cnt  = (PBT_RATIO)*ftl_wcnt_ctl;
	flush_io_each	= tFtlPbt.pbt_flush_total_cnt/ftl_io_ctl;
	ftl_core_trace(LOG_INFO, 0xbf80, "modify wcnt: %d -> %d flush_total:%d, pg_ratio:%d, each:%d",io_tmp,ftl_wcnt_ctl,tFtlPbt.pbt_flush_total_cnt,tFtlPbt.pbt_flush_page_cnt,flush_io_each);
	return 0;
}

static DEFINE_UART_CMD(pbtwcnt, "pbtwcnt","pbt","modify pbt wcnt",1, 1, pbt_wcnt_main);
#endif

static slow_code int pbt_stop_main(int argc, char *argv[])
{
	tFtlPbt.force_pbt_halt = (u64) atoi(argv[1]);
	ftl_core_trace(LOG_ERR, 0x7ba9, "pbt stop: %d \n",tFtlPbt.force_pbt_halt);
	return 0;
}

static DEFINE_UART_CMD(pbtstop, "pbtstop","pbt","stop flush pbt",1, 1, pbt_stop_main);

static slow_code int pbt_force_dump_main(int argc, char *argv[])
{
	//u64 force_mode = 0;
	//force_mode = (u64) atoi(argv[1]);
	//ftl_set_force_dump_pbt(true,DUMP_PBT_FORCE_FINISHED_CUR_GROUP);
	ftl_set_force_dump_pbt(0,true,1);
	ftl_core_trace(LOG_INFO, 0xbe1f, "[PBT]force_mode:%d",tFtlPbt.force_dump_pbt_mode);
	return 0;
}

static DEFINE_UART_CMD(pbtforce, "pbtforce","pbt","force dump pbt",0, 0, pbt_force_dump_main);



inline bool ftl_is_pbt_start(void){
	return tFtlPbt.dump_pbt_start;
}

slow_code inline void ftl_set_pbt_halt(bool mode){
	tFtlPbt.force_pbt_halt = mode;
	ftl_core_trace(LOG_INFO, 0xb2be, "[PBT]halt:%d",tFtlPbt.force_pbt_halt);
}
inline bool ftl_is_pbt_filldummy(void){
	return tFtlPbt.isfilldummy;
}

inline void ftl_clear_pbt_filldummy(void){
	if(tFtlPbt.isfilldummy == mTRUE){
		#if (PBT_DEBUG == mENABLE)
		ftl_core_trace(LOG_INFO, 0xd492, "[PBT]clear_pbt_filldummy!");
		#endif
		tFtlPbt.isfilldummy		= mFALSE;
		tFtlPbt.filldummy_done 	= mTRUE;
	}
}

fast_code inline void ftl_set_pbt_filldummy_done(void){
	if(tFtlPbt.isfilldummy == mTRUE){
		#if (PBT_DEBUG == mENABLE)
		pstream_t *ps = &fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps;
		ftl_core_trace(LOG_INFO, 0xa6bc, "[PBT]filldummy done! cur pbt:%d!",ps->aspb[ps->curr].spb_id);
		#endif
		tFtlPbt.filldummy_done 	= mTRUE;
		//tFtlPbt.isfilldummy =mFALSE;
	}
}
fsm_res_t plp_suspend_gc(fsm_ctx_t *fsm);
fsm_res_t vu_control_gc(u8 gc_act);


fast_code void ipc_suspend_gc(volatile cpu_msg_req_t *req)
{
    plp_suspend_gc(NULL);
}

ddr_code void stop_gc_cmpl(struct _gc_action_t *s)
{
    extern u8 stop_gc_done;
    stop_gc_done = 1;
}

ddr_code void ipc_suspend_gc_for_vac_cal(volatile cpu_msg_req_t *req)
{
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_SUSPEND;
	action->caller = NULL;
	action->cmpl = stop_gc_cmpl;

	if (gc_action(action))
	{
		sys_free(FAST_DATA, action);
	}
}
ddr_code void ipc_active_gc(volatile cpu_msg_req_t *req)
{
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_RESUME;
	action->caller = NULL;
	action->cmpl = NULL;

	if (gc_action(action))
	{
		sys_free(FAST_DATA, action);
	}
}

ddr_code void ipc_control_gc(volatile cpu_msg_req_t *req)
{
    vu_control_gc((u8)req->pl);
}

/*!
 * @brief save the current location and pstream information of PBT during fw update
 *
 * @return 	not used
 */
ddr_code void evt_fcore_save_pbt_param(u32 r0, u32 r1, u32 r2)
{
    pstream_t *ps = &fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps;
    core_wl_t *cwl = &fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->cwl;
    ftl_core_t *fcore = fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]; 
    aspb_t *aspb = &ps->aspb[ps->curr];
    spb_id_t spb_id = aspb->spb_id;
    u32 before_wreq_cnt = fcore->wreq_cnt;
    u32 pre_wptr = aspb->wptr;
    u8 flushed_flag = true;
    //close cwl
    if (tFtlPbt.force_pbt_halt == false)
        ftl_set_pbt_halt(true);

    if ((aspb->spb_id != INV_SPB_ID) && (aspb->spb_id != tFtlPbt.pbt_spb_cur)){
        tFtlPbt.pbt_spb_cur = aspb->spb_id;
        tFtlPbt.pbt_cur_loop++;
        if(tFtlPbt.pbt_cur_loop == (FTL_MAX_PBT_CNT+1))
        {
            tFtlPbt.pbt_cur_loop = 1;
        }
        shr_pbt_loop = tFtlPbt.pbt_cur_loop;
    }

    if (cwl->cur.mpl_qt > 0)
    {
        while (cwl->cur.mpl_qt) 
        {
            tFtlPbt.dump_pbt_start = mTRUE; 
            flushed_flag = ftl_core_pbt_seg_flush(); 
            tFtlPbt.dump_pbt_start = mFALSE; 
            if ((flushed_flag == false) && (cwl->cur.mpl_qt))
            {
                ftl_core_trace(LOG_ALW, 0xfda4, "before_wreq_cnt:%u, cur wreq_cnt:%u, pre_wptr:%u, cur wptr:%u mpl_qt:%u", 
                    before_wreq_cnt, fcore->wreq_cnt, pre_wptr, aspb->wptr, cwl->cur.mpl_qt);
                evt_set_cs(evt_save_pbt_param, 0, 0, CS_TASK);
                return;
            }
        }
    }
    else if(fcore->wreq_cnt == 0)
    {
        pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt = mFALSE;
        ftl_core_trace(LOG_ALW, 0xfbe3, "no need to wait");
    }else{
        ftl_core_trace(LOG_ALW, 0xb74d, "Need to wait. fcore->wreq_cnt:%u", fcore->wreq_cnt);
    }

    //save tFtlPbt
    pbt_resume_param->cur_seg_ofst = tFtlPbt.cur_seg_ofst;
    pbt_resume_param->pbt_cover_usr_cnt = tFtlPbt.pbt_cover_usr_cnt;   
    pbt_resume_param->fill_wl_cnt = tFtlPbt.fill_wl_cnt; 

    //save pbt pstream information
    if (ps->avail_cnt == 0) 
    { 
        pbt_resume_param->pbt_info.spb_id = INV_SPB_ID; 
        return; 
    } 
    pbt_resume_param->pbt_info.spb_id = spb_id;
    pbt_resume_param->pbt_info.flags.b.slc = aspb->flags.b.slc;
    pbt_resume_param->pbt_info.flags.b.valid = aspb->flags.b.valid;
    pbt_resume_param->pbt_info.open_skip_cnt = aspb->open_skip_cnt;
    pbt_resume_param->pbt_info.total_bad_die_cnt = aspb->total_bad_die_cnt;
#ifdef RAID_SUPPORT
    pbt_resume_param->pbt_info.parity_die = aspb->parity_die;
    pbt_resume_param->pbt_info.parity_die_pln_idx = aspb->parity_die_pln_idx;
    pbt_resume_param->pbt_info.flags.b.parity_mix = aspb->flags.b.parity_mix;
#endif

    pbt_resume_param->pbt_info.ptr = aspb->wptr;
    pbt_resume_param->pbt_info.sn = aspb->sn;

    ftl_core_trace(LOG_ALW, 0xab02, "blk: %u, wptr: %u, avail_cnt: %u, fill_wl_cnt: %u, wreq: %u", 
        spb_id, aspb->wptr, ps->avail_cnt, tFtlPbt.fill_wl_cnt, fcore->wreq_cnt); 
}

/*!
 * @brief restore the PBT information after fw update, and continue to write this PBT
 *
 * @return 	not used
 */
ddr_code void ftl_core_restore_pbt_param(void)
{
    pstream_t *ps = &fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps;
    aspb_t *aspb;
    bool ret;

    tFtlPbt.pbt_cur_loop = shr_pbt_loop;
    if (pbt_resume_param->pbt_info.flags.b.resume_pbt == false)
    {
        ftl_core_trace(LOG_ALW, 0xba20, "PBT information does not need to be restored");
        return;
    }

    //restore tFtlPbt
    tFtlPbt.cur_seg_ofst = pbt_resume_param->cur_seg_ofst;
    tFtlPbt.pbt_cover_usr_cnt = pbt_resume_param->pbt_cover_usr_cnt;
    tFtlPbt.pbt_spb_cur = pbt_resume_param->pbt_info.spb_id;
    if (pbt_resume_param->pbt_info.spb_id == INV_SPB_ID)
    {
        tFtlPbt.pbt_spb_cur = INV_SPB_ID;
        return;
    }
    if (tFtlPbt.cur_seg_ofst >= ns[0].seg_end)
    {
        tFtlPbt.isfilldummy = mTRUE;
        tFtlPbt.fill_wl_cnt = pbt_resume_param->fill_wl_cnt;  
    }

    //restore pbt pstream information
    ps->curr = 0;
    aspb = &ps->aspb[ps->curr];
    aspb->spb_id = pbt_resume_param->pbt_info.spb_id;
    aspb->flags.all = 0;
    aspb->flags.b.valid = pbt_resume_param->pbt_info.flags.b.valid;  
    aspb->open_skip_cnt = pbt_resume_param->pbt_info.open_skip_cnt;
    aspb->total_bad_die_cnt = pbt_resume_param->pbt_info.total_bad_die_cnt;
#ifdef RAID_SUPPORT
    aspb->parity_die = pbt_resume_param->pbt_info.parity_die;
    aspb->parity_die_pln_idx = pbt_resume_param->pbt_info.parity_die_pln_idx;
    aspb->flags.b.parity_mix = pbt_resume_param->pbt_info.flags.b.parity_mix;
#endif
    aspb->wptr = pbt_resume_param->pbt_info.ptr;
    aspb->sn = pbt_resume_param->pbt_info.sn;
    aspb->flags.b.slc = pbt_resume_param->pbt_info.flags.b.slc;

    ret = ftl_get_remap_tbl(aspb->spb_id, aspb->remap_tbl);
	if (ret == false) {
		u32 j;

		for (j = 0; j < nand_interleave_num(); j++)
			ps->aspb[ps->curr].remap_tbl[j] = aspb->spb_id;
	}


    u32 spage = aspb->wptr >> ctz(nand_interleave_num());
    u32 cwl_page = aspb->wptr & (nand_interleave_num() - 1);
#ifdef SKIP_MODE
    u32 defect_cnt = 0;
    u32 cwl_defect_cnt = 0;
    u32 k;
    u8* ftl_df_ptr = get_spb_defect(aspb->spb_id);
    for(k=0; k < nand_interleave_num(); k++)
    {
        if(defect_check(ftl_df_ptr,k)) {
            defect_cnt++;
        }
        if (k == cwl_page -1)
            cwl_defect_cnt = defect_cnt;
    }    
#endif

    if (aspb->flags.b.slc)
    {
        aspb->max_ptr = pg_cnt_in_slc_spb;
        ps->avail_cnt = aspb->max_ptr - aspb->wptr;
#ifdef SKIP_MODE
        aspb->defect_max_ptr = pg_cnt_in_slc_spb - (defect_cnt * (nand_page_num()/nand_bits_per_cell()));
        aspb->cmpl_cnt = spage * (nand_interleave_num() - defect_cnt) + (cwl_page - cwl_defect_cnt);
#else
        aspb->cmpl_cnt = aspb->wptr;
#endif
    }
    else
    {
        aspb->max_ptr = pg_cnt_in_xlc_spb;
        ps->avail_cnt = aspb->max_ptr - aspb->wptr - cwl_page * (nand_bits_per_cell() - 1);
#ifdef SKIP_MODE
        aspb->defect_max_ptr = pg_cnt_in_xlc_spb - (defect_cnt* nand_page_num());
        aspb->cmpl_cnt = spage * (nand_interleave_num() - defect_cnt) + nand_bits_per_cell() * (cwl_page - cwl_defect_cnt);
#else
        aspb->cmpl_cnt = aspb->wptr + (cwl_page * (nand_bits_per_cell() - 1));
#endif
    }
    sys_assert(ps->avail_cnt > 0);

    ps_open[FTL_CORE_PBT] = aspb->spb_id;
    ftl_core_set_spb_type(aspb->spb_id, aspb->flags.b.slc);
    ftl_core_set_spb_open(aspb->spb_id);

    ftl_core_trace(LOG_ALW, 0x5262, "blk: %u, wptr: %u, avail_cnt: %u, cmpl_cnt: %u, fill_wl_cnt: %u", aspb->spb_id, aspb->wptr, ps->avail_cnt, aspb->cmpl_cnt, tFtlPbt.fill_wl_cnt); 
}

fast_code inline bool ftl_is_ftltbl_ready_dump(void){
	if(fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps.flags.b.query_nxt == true){
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}

	//if((tFtlPbt.cur_seg_ofst == 0) && ((blist_flush_ctx[0] != NULL)||(blist_flush_ctx[1] != NULL))){
	if(((blist_flush_ctx[0] != NULL)||(blist_flush_ctx[1] != NULL))){ //for spor build
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}

	if(fcns[HOST_NS_ID]->ftl_core[FTL_CORE_NRM]->ps.flags.b.closing
        || fcns[HOST_NS_ID]->ftl_core[FTL_CORE_GC]->ps.flags.b.closing){
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}

	if(tFtlPbt.dump_pbt_start == true){
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}

	if(tFtlPbt.force_pbt_halt == true){
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}

	if(shr_shutdownflag == true){
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}
#if PLP_SUPPORT == 1
	if(plp_trigger){
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}
#else
	if(shr_nonplp_gc_state){
		tFtlPbt.pbt_res_ready = false;
		return tFtlPbt.pbt_res_ready;
	}
#endif	
	tFtlPbt.pbt_res_ready = true;
	return tFtlPbt.pbt_res_ready;
}

slow_code inline void ftl_clear_pbt_cover_usr_cnt(void){
	tFtlPbt.pbt_cover_usr_cnt = 0;
}

slow_code inline void ftl_pbt_cover_usr(void){
	if(tFtlPbt.cur_seg_ofst != 0)
		tFtlPbt.pbt_cover_usr_cnt++;
}

slow_code inline u32 ftl_get_pbt_cover_usr_cnt(void){
	return tFtlPbt.pbt_cover_usr_cnt;
}

slow_code inline u8 ftl_get_pbt_cover_percent(void){
	pstream_t *ps = &fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps;
	return ((ps->aspb[ps->curr].wptr/nand_info.interleave)*100/nand_info.geo.nr_pages); 
	//return tFtlPbt.cur_seg_ofst*100/ns[0].seg_end;
}


#endif


/*!
 * @brief core WL initialization
 *
 * @param cwl	core WL context
 *
 * @return	not used
 */
slow_code void cwl_init(core_wl_t *cwl)
{
	u32 i;
	for (i = 0; i < nand_info.lun_num; i ++)
		cwl->rid[i] = CWL_FREE_RID;

	cwl->flags.all = 0;
	memset(&cwl->cur, 0, sizeof(cwl->cur));
}

fast_code void evt_format_wreq_chk(u32 par, u32 format, u32 p2);
fast_code void evt_ftl_qbt_alloc_chk(u32 par, u32 ctx, u32 p2);
fast_code void evt_ftl_core_shutdown_chk(u32 par, u32 ctx, u32 p2);
fast_code void evt_ftl_qbt_resume(u32 par, u32 ctx, u32 p2);
fast_code void evt_ftl_misc_resume(u32 par, u32 ctx, u32 p2);


/*!
 * @brief pmu callback for ftl core when pmu suspend
 *
 * @param mode		sleep mode
 *
 * @return		always return true
 */
ps_code bool ftl_core_pmu_suspend(enum sleep_mode_t mode)
{
	extern bool rtos_core_pmu_suspend(enum sleep_mode_t mode);

	scheduler_suspend(0);
	rtos_core_pmu_suspend(mode);
	pmu_swap_register_other_cpu_memory();
	bool save_ret = pmu_swap_file_save();
	if (save_ret == false) {
		//ftl_core_trace(LOG_ERR, 0, "swap file save fail");
		pmu_swap_file_restore(false);
		return false;
	}
	return true;
}

static fast_code void l2p_updt_done(void)
{
	u32 req;

    #ifdef NCL_RETRY_PASS_REWRITE
    u32 i;
    #if 0
    u32 j;
    u32 idx;
    u32 mp_du_cnt, plane_cnt;
    #endif
    u32 dtag_id;
    bm_pl_t *bm_pl;

    u32 du_cnt;
    #endif
    ncl_w_req_t * ncl_req;

	while (CBF_EMPTY(&l2p_updt_done_que) == false) {
		CBF_HEAD(&l2p_updt_done_que, req);
        ncl_req = (ncl_w_req_t *)req;
		ncl_req->req.cmpl(ncl_req);
        #ifdef NCL_RETRY_PASS_REWRITE
        if (ncl_req->req.type == FTL_CORE_GC)
        {
            bm_pl = ncl_req->w.pl;
            #if 0
            #ifdef SKIP_MODE
            if(ncl_req->req.op_type.b.one_pln)
            {
                plane_cnt = shr_nand_info.geo.nr_planes / 2;
                mp_du_cnt = plane_cnt << DU_CNT_SHIFT;
            }
            else
            #endif
            {
                plane_cnt = shr_nand_info.geo.nr_planes;
                mp_du_cnt = plane_cnt << DU_CNT_SHIFT;
            }

            for (i = 0; i < ncl_req->req.tpage; i++)
            {
                idx = i * mp_du_cnt;
                for (j = 0; j < mp_du_cnt; j++)
                {
                    dtag_id = bm_pl[idx + j].pl.dtag & DDTAG_MASK;
                    if((dtag_id >= DDR_RD_REWRITE_START) && (dtag_id < (DDR_RD_REWRITE_START + DDR_RD_REWRITE_TTL_CNT)))
                    {
                        rd_rew_info[dtag_id - DDR_RD_REWRITE_START].flag = 0x5A5A;
                    }
                }
            }
            #else
            du_cnt = ncl_req->req.cnt << DU_CNT_SHIFT;
            for (i = 0; i < du_cnt; i++)
            {
                dtag_id = bm_pl[i].pl.dtag & DDTAG_MASK;
                if((dtag_id >= DDR_RD_REWRITE_START) && (dtag_id < (DDR_RD_REWRITE_START + DDR_RD_REWRITE_TTL_CNT)))
                {
                    rd_rew_info[dtag_id - DDR_RD_REWRITE_START].flag = 0x5A5A;
                }
            }
            #endif
        }
        #endif

		CBF_REMOVE_HEAD(&l2p_updt_done_que);
	}
}

static ps_code void l2p_updt_done_que_resume(void)
{
	CBF_INIT(&l2p_updt_done_que);
	l2p_updt_done_recv_hook((cbf_t *) &l2p_updt_done_que, l2p_updt_done);
}

/*!
 * @brief pmu callback for ftl core when pmu resume
 *
 * @param mode		sleep mode
 *
 * @return		not used
 */
ps_code void ftl_core_pmu_resume(enum sleep_mode_t mode)
{
	extern download_fw_t upgrade_fw;

	pmu_swap_file_restore(true);
	sys_assert(ncl_enter_mr_mode());
	if (srb_image_loader_cpu34_atcm(upgrade_fw.fwb_pri_rda) == false)
		srb_image_loader_cpu34_atcm(upgrade_fw.fwb_sec_rda);
	ncl_leave_mr_mode();

	pool_init(&ftl_core_ctx_pool, (void *)fc_unions, sizeof(fc_unions), sizeof(fc_unions[0]), MAX_FTL_CORE_CTX);
	ftl_core_ctx_cnt = MAX_FTL_CORE_CTX;
	scheduler_resume(0);
	ftl_core_gc_resume();
	l2p_updt_done_que_resume();

	CBF_INIT(&wreq_seq_que[FTL_CORE_NRM]);
	CBF_INIT(&wreq_seq_que[FTL_CORE_GC]);

	u32 i, j;
	ftl_core_t *fc;
	for (i = 0; i < INT_NS_ID + 1; i++) {
		if (fcns[i] == NULL)
			continue;

		for (j = 0; j < FTL_CORE_MAX; j++) {
			if (fcns[i]->ftl_core[j] == NULL)
				continue;

			fc = fcns[i]->ftl_core[j];
			pstream_resume(&fc->ps);
		}
	}
}

/*!
 * @brief pad all du to finish a core WL
 *
 * @param nsid	namespace id
 * @param type	ftl core type
 * @param pctx	spb pda context
 *
 * @return	return true for done
 */
//static bool ftl_core_cwl_pad(ftl_core_ns_pad_t *ns_pad);
init_code void ftl_core_tbl_res_init(void)
{
	u32 i,j;
	u32 meta_idx;
	io_meta_t *tbl_meta;
	u32 io_cnt_per_seg;

	sys_assert(shr_l2pp_per_seg);

    #if 1
	tbl_meta = idx_meta_allocate(shr_l2pp_per_seg * FTL_TBL_SRAM, SRAM_IDX_META, &meta_idx);

	for (i = 0; i < FTL_TBL_SRAM; i++) {
		_tbl_res[i].io_meta = tbl_meta + shr_l2pp_per_seg * i;
		_tbl_res[i].meta_idx = meta_idx + shr_l2pp_per_seg * i;
		for(j = 0; j<shr_l2pp_per_seg; j++){
			_tbl_res[i].io_meta[j].fmt = 0;
		}
	}

	tbl_meta = idx_meta_allocate(shr_l2pp_per_seg * FTL_TBL_DDR, DDR_IDX_META, &meta_idx);
	ddtag_tbl_meta = tbl_meta;

	for (i = FTL_TBL_SRAM; i < MAX_FTL_TBL_IO; i++) {
		_tbl_res[i].io_meta = tbl_meta + shr_l2pp_per_seg * i;
		_tbl_res[i].meta_idx = meta_idx + shr_l2pp_per_seg * i;
		for(j = 0; j<shr_l2pp_per_seg; j++){
			_tbl_res[i].io_meta[j].fmt = 0;
		}
	}
    #else
	tbl_meta = idx_meta_allocate(shr_l2pp_per_seg * FTL_TBL_DDR, DDR_IDX_META, &meta_idx);
	ddtag_tbl_meta = tbl_meta;

	for (i = 0; i < FTL_TBL_DDR; i++) {
	//for (i = FTL_TBL_SRAM; i < MAX_FTL_TBL_IO; i++) {
		_tbl_res[i].io_meta = tbl_meta + shr_l2pp_per_seg * i;
		_tbl_res[i].meta_idx = meta_idx + shr_l2pp_per_seg * i;
		for(j = 0; j<shr_l2pp_per_seg; j++){
			_tbl_res[i].io_meta[j].fmt = 0;
		}
	}

	tbl_meta = idx_meta_allocate(shr_l2pp_per_seg * FTL_TBL_SRAM, SRAM_IDX_META, &meta_idx);

	for (i = FTL_TBL_DDR; i < MAX_FTL_TBL_IO; i++) {
	//for (i = 0; i < FTL_TBL_SRAM; i++) {
		_tbl_res[i].io_meta = tbl_meta + shr_l2pp_per_seg * i;
		_tbl_res[i].meta_idx = meta_idx + shr_l2pp_per_seg * i;
		for(j = 0; j<shr_l2pp_per_seg; j++){
			_tbl_res[i].io_meta[j].fmt = 0;
		}
	}
	#endif

	io_cnt_per_seg = shr_l2pp_per_seg / (nand_plane_num() * DU_CNT_PER_PAGE);
	//sys_assert(io_cnt_per_seg <= MAX_WREQ_PER_SEG);
	tbl_io_per_seg = io_cnt_per_seg;

	// flush unit is segment
	pool_init(&_tbl_res_pool, (void*)_tbl_res, sizeof(_tbl_res), sizeof(tbl_res_t), MAX_FTL_TBL_IO);
}

ddr_code void ftl_core_reset_ack(u32 p0, u32 p1, u32 p2)
{
	sys_assert(fc_flags.b.reseting);

	// TODO check BACKEND all host related data done or abort

	if (shr_ftl_flags.b.gc_stoped)
	{
		fc_flags.b.reseting = false;
		if (fc_flags.b.reset_ack_req == true)
		{
			sys_assert(reset_ack_req_tx != 0xFF);
			//cpu_msg_sync_done(reset_ack_req_tx);
			smResetHandle.abortMedia = false;
			reset_ack_req_tx = 0xFF;
			fc_flags.b.reset_ack_req = false;
		}
		else
		{
			fc_flags.b.reset_ack = true;
		}
	} else {
		// continue wait gc_stoped
		evt_set_cs(evt_reset_ack, 0, 0, CS_TASK);
	}
}

init_code void fcore_open_handle_init(void)
{
	u32 i;
	for (i = 0; i < MAX_SPBPAD_CNT; ++i) {
		open_ctrl.spb_pad[i].spb_pad_idx = i;
	}
    memset(&open_ctrl.using_bmp, 0, occupied_by(MAX_SPBPAD_CNT, 32)*sizeof(u32));
    open_ctrl.free_cnt = MAX_SPBPAD_CNT;
	return;
}
extern void ftl_core_seg_flush_evt(u32 p0,u32 p1,u32 p2);

init_code void ftl_core_init(void)
{
	u32 i;

	ftl_core_t *fc;

	fcns[0] = NULL; // not used

	width_nominal = nand_plane_num() * nand_lun_num() * nal_get_channels() * nal_get_targets();
	pg_cnt_in_xlc_spb = nand_page_num() * width_nominal;
	pg_cnt_in_slc_spb = pg_cnt_in_xlc_spb / nand_bits_per_cell();  /* no u64 __aeabi_uldivmod, so u32 */
	spb_total = nand_block_num();

	width_in_dws = occupied_by(width_nominal, 32);
	die_id_mask = nand_info.lun_num - 1;

	spb_rt_flags = share_malloc(sizeof(spb_rt_flags_t) * spb_total);
	sys_assert(spb_rt_flags);
	memset(spb_rt_flags, 0, sizeof(spb_rt_flags_t) * spb_total);

	p2l_init();
	pool_init(&ftl_core_res_pool, (void*)ftl_core_res,
		sizeof(ftl_core_res), sizeof(ftl_core_res[0]), FTL_CORE_RES_TTL);
    parity_allocate_cnt = 0;
	plp_ask_close_open_wl = false;
	for (i = 0; i < INT_NS_ID + 1; i++) {
		u32 j;
		spb_queue_t *spbq;

		if (i == 0) {
			fcns[i] = NULL;
			continue;
		}

		fcns[i] = &_fcns[i - 1];
		fcns[i]->flags.b.clean = true;

		if (i != INT_NS_ID) {
			fcns_attr[i].b.p2l = 1;	///< host namespace has P2L

		}
		fcns_attr[i].b.raid = RAID_SUPPORT;

		for (j = 0; j < FTL_CORE_MAX; j++) {
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if((i == INT_NS_ID && j == FTL_CORE_GC)/* ||(i == HOST_NS_ID && j == FTL_CORE_PBT)*/)
#else
			if((i == INT_NS_ID && j == FTL_CORE_GC) || (i == HOST_NS_ID && j == FTL_CORE_PBT))
#endif
			{
				continue;
			}

			//spbq = (i == INT_NS_ID) ? &int_spb_queue : &spb_queue[i - 1][j];
			if(i == INT_NS_ID){
				if(j == FTL_CORE_PBT){
					spbq = &int_spb_queue[1];
				}else{
					spbq = &int_spb_queue[0];
				}
			}else{
				spbq = &spb_queue[i - 1][j];
			}
			CBF_INIT(spbq);

			fc = (ftl_core_t*)pool_get_ex(&ftl_core_res_pool);
			sys_assert(fc);
			memset(fc, 0, sizeof(ftl_core_t));

			fcns[i]->ftl_core[j] = fc;
            if(i == INT_NS_ID)
            {
				//pstream_init(&fc->ps, DU_CNT_PER_PAGE * nand_plane_num(), &spb_queue[i - 1][j], i, j);
				if(j == FTL_CORE_PBT){
					pstream_init(&fc->ps, DU_CNT_PER_PAGE, &int_spb_queue[1], i, j);
				}else{
					pstream_init(&fc->ps, DU_CNT_PER_PAGE, &int_spb_queue[0], i, j);
				}
            }
            else
            {
                pstream_init(&fc->ps, DU_CNT_PER_PAGE, &spb_queue[i - 1][j], i, j);
            }
			cwl_init(&fc->cwl);
			fc->cur_wr_pl.cnt = 0;
			fc->cur_wr_pl.nsid = i;
			fc->cur_wr_pl.type = j;
			fc->sn = 0;	// reset after power cycle?
			fc->wreq_cnt = 0;
			fc->parity_wreq_cnt = 0;
			fc->err.all = 0;
			stripe_user_init(&fc->su);

			//if (i == INT_NS_ID)
				//break;	// no GC for internal NS
		}
		fcns[i]->wreq_cnt = 0;
		fcns[i]->parity_wreq_cnt = 0;
		QSIMPLEQ_INIT(&fcns[i]->pending);

		fcns[i]->flushing_ctx = NULL;
		fcns[i]->format_ctx = NULL;
		QSIMPLEQ_INIT(&fcns[i]->ns_pad_list);
	}

	avoid_fcore_flush_hang = 0;
	extern u16* plp_host_open_die;
	u16* host_open_die_addr = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_NRM]->cwl.cur.open_die;
	plp_host_open_die = tcm_local_to_share(host_open_die_addr);

    fcns[1]->ftl_core[1]->idle_timer.function = fcore_gc_idle_check;
	fcns[1]->ftl_core[1]->idle_timer.data = "fcore_gc_idle_flush";
	fcns[1]->ftl_core[1]->data_in = 0;
	fcns[1]->ftl_core[1]->last_data_in = 0;
	mod_timer(&fcns[1]->ftl_core[1]->idle_timer, jiffies + HZ/10);

	fcns[1]->ftl_core[0]->idle_timer.function = fcore_idle_check;
	fcns[1]->ftl_core[0]->idle_timer.data = "fcore_idle_flush";
	fcns[1]->ftl_core[0]->data_in = 0;
	fcns[1]->ftl_core[0]->last_data_in = 0;
#if(PLP_SUPPORT == 1)
	mod_timer(&fcns[1]->ftl_core[0]->idle_timer, jiffies + HZ);
#else
	mod_timer(&fcns[1]->ftl_core[0]->idle_timer, jiffies + 3*HZ/10);
#endif
 	// ISU, EHPerfImprove (2)
    fcns[2]->ftl_core[2]->idle_timer.function = fcore_force_pbt_timer;
	fcns[2]->ftl_core[2]->idle_timer.data = "fcore_force_pbt";
	fcns[2]->ftl_core[2]->data_in = 0;
	fcns[2]->ftl_core[2]->last_data_in = 0;
	//mod_timer(&fcns[2]->ftl_core[2]->idle_timer, jiffies + HZ/10);

	ftl_core_tbl_res_init();

	CBF_INIT(&wreq_seq_que[FTL_CORE_NRM]);
	CBF_INIT(&wreq_seq_que[FTL_CORE_GC]);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	CBF_INIT(&wreq_seq_que[FTL_CORE_SLC]);
#endif
	pool_init(&ftl_core_ctx_pool, (void *)fc_unions, sizeof(fc_unions), sizeof(fc_unions[0]), MAX_FTL_CORE_CTX);
	ftl_core_ctx_cnt = MAX_FTL_CORE_CTX;
	ftl_core_gc_init();

    evt_register(evt_format_wreq_chk, 0, &evt_format_wreq);
	//l2p table updt mgr init
	l2p_updt_done_que_resume();
	evt_register(tbl_vcnt_upd_done, 0, &evt_dvl_info_updt_rslt);
	evt_register(ftl_core_ready, 0, &evt_fc_ready_chk);
	evt_register(evt_ftl_qbt_alloc_chk, 0, &evt_fc_qbt_ready_chk);
	evt_register(evt_ftl_qbt_resume, 0, &evt_fc_qbt_resume);
	evt_register(evt_ftl_misc_resume, 0, &evt_fc_misc_resume);
	evt_register(evt_ftl_core_shutdown_chk, 0, &evt_fc_shutdown_chk);
	evt_register(fcore_fill_dummy_task, 0, &open_ctrl.evt_fill_dummy_task);
	evt_register(ftl_core_reset_ack, 0, &evt_reset_ack);
	evt_register(plp_evt_done_check, 0, &evt_plp_done_chk);
	evt_register(plp_evt_check, 0, &evt_plp);
	evt_register(evt_flush_spor_qbt, 0, &evt_save_fake_qbt); 
    evt_register(evt_fcore_save_pbt_param, 0, &evt_save_pbt_param); 
    evt_register(evt_wr_pout_issue, 0, &evt_pout_issue);
    //evt_register(ftl_core_seg_flush_evt, 0, &evt_seg_flush);
	l2p_mgr_trim_cmpl_filter(true, L2P_UPDT_QID);
	l2p_mgr_dtag_recycle_que_filter(true);
#if RAID_SUPPORT
	ftl_raid_init();
#endif
	ftl_core_ipc_init();
	pmu_swap_init();


	/* open blk handle init */
	fcore_open_handle_init();

#if PLP_DEBUG
    plp_init();  //gpio init in cpu 2
#endif

	pmu_register_handler(SUSPEND_COOKIE_FTL, ftl_core_pmu_suspend,
			RESUME_COOKIE_FTL, ftl_core_pmu_resume);
}

ddr_code void tbl_vcnt_upd_done(u32 p0, u32 entries, u32 count)
{
	// todo: we should completion tbl write here
}

/*!
 * @brief update new mapping into internal l2p
 *
 * @param req	completed write request to write l2p table
 *
 * @return	not used
 */
slow_code void tbl_pda_updt(ncl_w_req_t *req)
{
	if (req->w.lda[0] == INV_LDA)
		return;	// pad req

	//u32 seg = req->w.lda[0] >> shr_l2pp_per_seg_shf;
	u32 seg = (req->w.lda[0]/12);
	pda_t old;

	old = shr_ins_lut[seg];
#ifdef SKIP_MODE
	shr_ins_lut[seg] = req->w.pda[0];
	dvl_info_updt(_inv_dtag, req->w.pda[0], false);
#else
	shr_ins_lut[seg] = req->w.l2p_pda[0];
	dvl_info_updt(_inv_dtag, req->w.l2p_pda[0], false);
#endif

	if (is_normal_pda(old))
		dvl_info_updt(_inv_dtag, old, true);

#if 1
	u32 lda_cnt = req->req.cnt * DU_CNT_PER_PAGE;
	u32 i;
	for (i = 1; i < lda_cnt; i++) {
		sys_assert(req->w.lda[i] == req->w.lda[i - 1] + 1);
	}
	sys_assert(req->req.cnt == nand_plane_num());
	for (i = 1; i < req->req.cnt; i++) {
#ifdef SKIP_MODE
				sys_assert(req->w.pda[i] == req->w.pda[i - 1] + DU_CNT_PER_PAGE);
#else
				sys_assert(req->w.l2p_pda[i] == req->w.l2p_pda[i - 1] + DU_CNT_PER_PAGE);
#endif

	}
#endif
}

fast_data_zi ftl_flush_data_t * shutdown_flush_ctx;

slow_code void ftl_core_flush_shutdown(flush_ctx_t *ctx)
{
	u32 nsid = ctx->nsid;
    sys_assert(shutdown_flush_ctx);
	ftl_flush_data_t *fctx = shutdown_flush_ctx;

	if (ctx->flags.b.shutdown) {
        u8 i;
        for (i = 0; i < FTL_CORE_MAX; i++) {
    		ftl_core_t *fc = fcns[HOST_NS_ID]->ftl_core[i];
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(SLC_init == true && i == FTL_CORE_SLC) //when shundown , don't reset slc pstream
				continue;
#else
			if(i == FTL_CORE_PBT)
				continue;
#endif	
			ftl_core_trace(LOG_INFO, 0x503d, "ns:%d(%d), wreq_cnt:%d",HOST_NS_ID,i,fc->wreq_cnt);
			if(fc->wreq_cnt !=0)
			{
				ftl_core_trace(LOG_WARNING, 0x62e1, "ns:%d(%d)not ready chk again",HOST_NS_ID,i);
				evt_set_cs(evt_fc_shutdown_chk, (u32)ctx, 0, CS_TASK);
				return;
			}
    		sys_assert(fc->wreq_cnt == 0);
    		pstream_reset(&fc->ps);
    		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
    	}

    	for (i = 0; i < FTL_CORE_MAX; i++) {
    		ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[i];

    		if (fc == NULL)
    			continue;

			ftl_core_trace(LOG_INFO, 0x0d3c, "ns:%d(%d), wreq_cnt:%d",INT_NS_ID,i,fc->wreq_cnt);
			if(fc->wreq_cnt !=0)
			{
				ftl_core_trace(LOG_WARNING, 0xb274, "ns:%d(%d)not ready chk again",INT_NS_ID,i);
				evt_set_cs(evt_fc_shutdown_chk, (u32)ctx, 0, CS_TASK);
				return;
			}
    		sys_assert(fc->wreq_cnt == 0);
    		pstream_reset(&fc->ps);
    		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
    	}
		ftl_core_clean(nsid);
		// todo: we may have more than 1 ns share one internal ns, consider to make it clean or not
		ftl_core_clean(INT_NS_ID);
#if (PBT_OP == mENABLE)
		ftl_pbt_init(false);
		ftl_set_pbt_halt(false);
#endif

		ftl_core_trace(LOG_ALW, 0xccb6, "system clean!");
	}

#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
	if(delay_flush_spor_qbt == true){
		extern volatile u8 shr_lock_power_on;        
		epm_FTL_t* epm_ftl_data;
		epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)  
		ftl_core_trace(LOG_ALW, 0x7930, "[Pre]epm_ftl_data->spor_last_rec_blk_sn : 0x%x", epm_ftl_data->spor_last_rec_blk_sn);
		//gFtlMgr.LastQbtSN = gFtlMgr.SerialNumber;
		//epm_ftl_data->spor_last_rec_blk_sn = gFtlMgr.LastQbtSN;		
		epm_ftl_data->spor_last_rec_blk_sn = spor_qbtsn_for_epm;
		ftl_core_trace(LOG_ALW, 0x217e, "[After]epm_ftl_data->spor_last_rec_blk_sn : 0x%x", epm_ftl_data->spor_last_rec_blk_sn);
#endif        
		//ftl_core_trace(LOG_ALW, 0, "save fake qbt done!	gFtlMgr.SerialNumber:0x%x, gFtlMgr.LastQbtSN:0x%x",gFtlMgr.SerialNumber,gFtlMgr.LastQbtSN);
		ftl_core_trace(LOG_ALW, 0x3fa2, "save fake qbt done  !!");
        //spor_read_pbt_cnt = 0;
        #if (PLP_SUPPORT == 0)
		shr_nonplp_gc_state = SPOR_NONPLP_GC_START;
		dsb();//make sure gc state visible to cpu1
        #endif
		delay_flush_spor_qbt = false;
		
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SCAN_GLIST_TRG_HANDLE, 0, EH_BUILD_GL_TRIG_EHTASK);	
        if (epm_ftl_data->format_tag == FTL_FULL_TRIM_TAG){
            epm_format_state_update(0, 0);
            epm_update(FTL_sign, (CPU_ID-1));	
        }

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)//slc buffer
 		extern volatile u8 shr_slc_flush_state;
		//extern u32 shr_plp_slc_need_gc;
		if(epm_ftl_data->plp_slc_gc_tag == FTL_PLP_SLC_NEED_GC_TAG)	
		{
			shr_lock_power_on |= SLC_LOCK_POWER_ON; // to stop host write , slc_lock ++
			shr_slc_flush_state = SLC_FLOW_GC_START;
        	cpu_msg_issue(2, CPU_MSG_CHK_GC, 0, 1);	
		}
		/*
			//mask, plp no ready ,can't trigger force gc . plp test will call gc chk
		else
		{
			shr_slc_flush_state = SLC_FLOW_DONE;
			cpu_msg_issue(2, CPU_MSG_CHK_GC, 0, 0); //avoid in RO and lock gc
		}
		*/

#elif (PLP_SUPPORT == 0)	//nonplp

#if (BG_TRIM == ENABLE)
		cpu_msg_issue(CPU_FE - 1, CPU_MSG_CHK_BG_TRIM, 0, 0);
#endif
		shr_lock_power_on |= NON_PLP_LOCK_POWER_ON; // to stop host write , non_plp ++
        cpu_msg_issue(2, CPU_MSG_CHK_GC, 0, 0);	
#else //plp ,without slc buffer
		cpu_msg_issue(2, CPU_MSG_CHK_GC, 0, 0);
#endif		
       shr_lock_power_on &= (~FQBT_LOCK_POWER_ON);  // fqbt_lock --
	} 
#endif 
	epm_FTL_t* epm_ftl_data;
	epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	epm_ftl_data->pbt_force_flush_flag = 0;

    if((PLA_FLAG == false)&&(PLN_FLAG == false)){
        cpu_msg_issue(CPU_FE - 1, CPU_MSG_PLA_PULL, 0, 0);
        //cpu_msg_issue(CPU_FE - 1, CPU_MSG_IO_CMD_SWITCH, 0, (u32)false);
        ftl_core_trace(LOG_ALW, 0x3bc2, "PLA  end !");

    }
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	max_usr_blk_sn = INV_U32;
#endif
	sys_assert(ctx->caller == fctx);
    shutdown_flush_ctx = NULL;
	fctx->ctx.cmpl(&fctx->ctx);
	sys_free(FAST_DATA, ctx);
	ftl_core_flush_resume(nsid);
}

/*!
 * @brief check if flush done
 *
 * @param nsid	ns id
 *
 * @return	return true if flush was done
 */

slow_code bool ftl_core_flush_done(u32 nsid, void *caller)
{
	ftl_flush_data_t *fctx = fcns[nsid]->flushing_ctx;
	bool dtag_gc = (fctx->flags.b.dtag_gc /*&& (caller == fctx)*/);
	u32 wreq_cnt;
	if (fctx->flags.b.finished)	/// wait for callback of ftl_flush
    {
		return false;
    }

//	wreq_cnt = fcns[nsid]->wreq_cnt - fcns[nsid]->ftl_core[FTL_CORE_GC]->wreq_cnt;
//    if(plp_trigger && fctx_addr == fctx)
//              ftl_core_trace(LOG_PLP, 0, "wreq_cnt %d dtag_gc %d gc wreq %d",
//              wreq_cnt, dtag_gc, fcns[nsid]->ftl_core[FTL_CORE_GC]->wreq_cnt);
	/*
	if (plp_trigger) {
		ftl_core_trace(LOG_PLP, 0, "total:%d, parity:%d", fcns[nsid]->ftl_core[FTL_CORE_NRM]->wreq_cnt,  fcns[nsid]->ftl_core[FTL_CORE_NRM]->parity_wreq_cnt);
	}
	*/
	wreq_cnt = fcns[nsid]->ftl_core[FTL_CORE_NRM]->wreq_cnt - fcns[nsid]->ftl_core[FTL_CORE_NRM]->parity_wreq_cnt; 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)    
	if(write_type == FTL_CORE_SLC)
	{		
		u32 slc_wreq_cnt = fcns[1]->ftl_core[FTL_CORE_SLC]->wreq_cnt - fcns[nsid]->ftl_core[FTL_CORE_SLC]->parity_wreq_cnt; 
		if(wreq_cnt == 0 && slc_wreq_cnt == 1)
		{
			ftl_core_close_open_die(write_nsid, write_type,NULL);
		}
		/*
		{
			ftl_core_trace(LOG_ALW, 0x2105,"slc wreq_cnt:%d parity:%d",fcns[1]->ftl_core[FTL_CORE_SLC]->wreq_cnt,fcns[nsid]->ftl_core[FTL_CORE_SLC]->parity_wreq_cnt);
		}
		*/
		wreq_cnt += slc_wreq_cnt;
	}
#endif
	if ((wreq_cnt == 0) || dtag_gc) {
		avoid_fcore_flush_hang = 0;

		if (fctx->flags.b.shutdown) {

			flush_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(flush_ctx_t));

			fctx->flags.b.finished = 1;
			sys_assert(ctx);
			ctx->nsid = nsid;
			ctx->caller = fctx;
			ctx->flags.all = 0;
			if (fctx->flags.b.format)
				ctx->flags.b.format = 1;
			else
				ctx->flags.b.shutdown = 1;
			ctx->cmpl = ftl_core_flush_shutdown;

            shutdown_flush_ctx = fcns[nsid]->flushing_ctx; 
            fcns[nsid]->flushing_ctx = NULL;
			if (plp_trigger) {
                if (PLN_in_low)
                {
                    PLN_flush_cache_end = false;
                    ftl_core_trace(LOG_INFO, 0x106e, "pln+plp fctx:0x%x done, flag 0x%x", fctx, fctx->flags.all);
                }
				sys_free(FAST_DATA, ctx);
				return true;
			} else {
                PLN_flush_cache_end = false;
				ftl_flush(ctx);
				return false;
			}
		}
        fcns[nsid]->flushing_ctx = NULL;
		fctx->ctx.cmpl(&fctx->ctx);
		ftl_core_trace(LOG_INFO, 0xfda1, "fctx:0x%x done, flag 0x%x", fctx, fctx->flags.all);
		return true;
	}
	return false;
}

slow_code void ftl_core_err_handle_done(ncl_w_req_t *req, u32 spb_id)
{
	u32 type = req->req.type;
	fcns[req->req.nsid]->ftl_core[type]->err.b.prog_err_blk_in  = false;
	fcns[req->req.nsid]->ftl_core[type]->err.b.prog_err_blk_updt= false; 	// For no need fill dummy case, ISU, EH_GC_not_done
	fcns[req->req.nsid]->ftl_core[type]->err.b.rd_open_close 	= false;	// ISU, RdOpBlkHdl
	rdOpBlkFal[type] = INV_U16;

	ftl_core_trace(LOG_ALW, 0xe414, "spb %d nsid %d type %d err handle done, err flag 0x%x", spb_id, req->req.nsid, type, fcns[req->req.nsid]->ftl_core[type]->err.all);

	// For fill few dummy WLs and spb closed case, ISU, SetGCSuspFlag
	#ifdef EH_FORCE_CLOSE_SPB
	#ifndef EH_GCOPEN_HALT_DATA_IN
	if (type == FTL_CORE_GC){	// apply gc suspend when gc open prog failed
		ftl_core_trace(LOG_INFO, 0xb5a9, "[EH] resume gc");

		gc_action_t *gc_act = sys_malloc(FAST_DATA, sizeof(gc_action_t));
		gc_act->act = GC_ACT_RESUME;
		gc_act->caller = NULL;
		gc_act->cmpl = NULL;
		if (gc_action(gc_act))
			sys_free(FAST_DATA, gc_act);
	} else
	#endif
	if (type == FTL_CORE_PBT){	// ISU, HdlPBTAftForceCls
		// Close done, trigger mark bad.
		MK_FailBlk_Info failBlkInfo;
		failBlkInfo.all = 0;
		failBlkInfo.b.wBlkIdx = spb_id;
		failBlkInfo.b.need_gc = false;
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SET_ERR_TASK, 0, failBlkInfo.all);
	}
	#endif
}

slow_code void ftl_core_gc_prog_err_done(gc_action_t *gc_act)
{
	u32 spb_id = (u32)gc_act->caller;
	ftl_core_trace(LOG_ERR, 0x3b43, "set spb %d prog err retire with ipc gc dst abort done", spb_id);
	sys_free(SLOW_DATA, gc_act);

	#ifndef EH_FORCE_CLOSE_SPB	// Clear in pstream_force_close, ISU, SetGCSuspFlag
	if ((fcns[1]->ftl_core[FTL_CORE_GC]->err.b.prog_err_blk_updt) &&
			(fcns[1]->ftl_core[FTL_CORE_GC]->wreq_cnt == 0))
    {
		fcns[1]->ftl_core[FTL_CORE_GC]->err.b.prog_err_blk_updt = false;
	}
	#endif
}

slow_code void ftl_core_gc_prog_err(u32 spb_id)
{
	gc_action_t *gc_act = sys_malloc(SLOW_DATA, sizeof(gc_action_t));
#ifdef ERRHANDLE_GLIST
	gc_act->act = GC_ACT_SUSPEND;
    gc_pro_fail = true;
#else
	gc_act->act = GC_ACT_ABORT;
#endif
	gc_act->caller = (void *)spb_id;
	gc_act->cmpl = ftl_core_gc_prog_err_done;

	ftl_core_trace(LOG_ERR, 0x0e60, "gc dst %d prog err abort\n", spb_id);

	// ISU, SetGCSuspFlag
	//if (ftl_core_gc_action(gc_act))
	//	gc_act->cmpl(gc_act);
	if (gc_action(gc_act))
		gc_act->cmpl(gc_act);
}


fast_code inline void fcore_check_fill_dummy_req_done(ncl_w_req_t *req)	// ISU, EH_GC_not_done (3)
{
	// just handle spb pad request when spb pad task actually oning, for better performance
	if (open_ctrl.free_cnt != MAX_SPBPAD_CNT) {
		for (u8 i = 0; i < MAX_SPBPAD_CNT; ++i) {
			ftl_spb_pad_t *cur = &open_ctrl.spb_pad[i];
			if (req->req.caller == (void*)cur) {
				cur->private_cmpl(cur);
			}
		}
	}
}

fast_code void ftl_core_wreq_updt_done(ncl_w_req_t *req)
{
	u32 nsid = req->req.nsid;
	u32 type = req->req.type;
	ftl_core_t *fcore = fcns[nsid]->ftl_core[type]; 
    if(req->req.op_type.b.last_req){
        fcore->ps.flags.b.closing = 0; 
    }

	pstream_done(&fcore->ps, req);

	fcns[nsid]->wreq_cnt--;
	fcore->wreq_cnt--;

	if (req->req.op_type.b.parity) {
		fcns[nsid]->parity_wreq_cnt--;
		fcns[nsid]->ftl_core[type]->parity_wreq_cnt--;
	}

#if (PBT_OP == mENABLE)
	if(nsid != INT_NS_ID && type != FTL_CORE_PBT){
		FTL_PBT_Update((req->req.cnt << DU_CNT_SHIFT));
	}

    //fw update PBT close cwl done
    if (nsid == INT_NS_ID && type == FTL_CORE_PBT)
    {
        if (fcore->wreq_cnt == 0 && pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt == mTRUE)
        {
            pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt = mFALSE;
            ftl_core_trace(LOG_INFO, 0x2aec, "PBT current wl close done");
        }
    }
#endif


	if (fcns[nsid]->flushing_ctx) {
//        if(plp_trigger){
//            ftl_core_trace(LOG_PLP, 0, "req:0x%x, req fctx:0x%x, fctx:0x%x", req, req->req.caller, fcns[nsid]->flushing_ctx);
//        }
        ftl_flush_data_t *fctx = fcns[nsid]->flushing_ctx;
		if (ftl_core_flush_done(nsid, req->req.caller)){
            if (fctx->flags.b.shutdown && plp_trigger && PLN_in_low)
            {
                PLN_in_low = false;
                fctx->ctx.cmpl(&fctx->ctx);
            }
			ftl_core_flush_resume(nsid);
        }
	}
	/* use for spb close fill dummy callback */
//	if (plp_trigger) {
//		if (req->req.caller) {
//			ftl_core_trace(LOG_ALW, 0, "wr_req done, caller:0x%x, req:0x%x, pad free cnt:%d", req->req.caller, req, open_ctrl.free_cnt);
//		}
//	}
	fcore_check_fill_dummy_req_done(req);	// ISU, EH_GC_not_done (3)
	put_ncl_w_req(req);

	//resume gc after wreq put
	ftl_core_t *gc_fcore = fcns[HOST_NS_ID]->ftl_core[FTL_CORE_GC];
	if (gc_fcore->err.b.wreq_pend) {
		gc_fcore->err.b.wreq_pend = false;
		ftl_core_gc_pend_out();
	}

    #ifdef ERRHANDLE_GLIST
    if((fcore->err.b.prog_err_blk_updt || fcore->err.b.rd_open_close) && (fcore->wreq_cnt == 0) && fcore->err.b.dummy_done)// For fill dummy case, ISU, EH_GC_not_done (1)
	{
		// ISU, Tx, PgFalClsNotDone (1)
		#ifdef EH_FORCE_CLOSE_SPB
		pstream_t *ps = &fcore->ps;
		aspb_t *aspb  = &ps->aspb[ps->curr];
		u16 spb_id    = aspb->spb_id;	// ISU, SetGCSuspFlag
		//if (aspb->flags.b.err)
		//{
		// Force close NRM/ GC only, FTL will be handled by ftl_set_force_dump_pbt
		pstream_force_close(aspb->spb_id);
		rdOpBlkFal[fcore->ps.type] = INV_U16;

		if (fcore->ps.type == FTL_CORE_GC && nsid == HOST_NS_ID){
			// Resume gc after gc open blk closed, ISU, SetGCSuspFlag
			ftl_core_trace(LOG_INFO, 0x9627, "resume gc");

			gc_action_t *gc_act = sys_malloc(FAST_DATA, sizeof(gc_action_t));
			gc_act->act = GC_ACT_RESUME;
			gc_act->caller = NULL;
			gc_act->cmpl = NULL;
			if (gc_action(gc_act))
				sys_free(FAST_DATA, gc_act);
		}
		else if (fcore->ps.type == FTL_CORE_PBT){
			// Trigger mark bad after pbt open blk closed, ISU, HdlPBTAftForceCls
			MK_FailBlk_Info failBlkInfo;
			failBlkInfo.all = 0;
			failBlkInfo.b.wBlkIdx = spb_id;
			failBlkInfo.b.need_gc = false;
			cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SET_ERR_TASK, 0, failBlkInfo.all);
		}
		//}
		#endif

		fcore->err.b.prog_err_blk_updt = false;
		fcore->err.b.prog_err_blk_in = false;	// ISU, SetGCSuspFlag
		fcore->err.b.rd_open_close = false;
		ftl_core_trace(LOG_ALW, 0xd81e, "spb %u nsid %u type %u err handle, err flag 0x%x", spb_id, fcore->ps.nsid, fcore->ps.type, fcore->err.all);
	}
	#endif
}

fast_data ncl_req_cmpl_t cpl;
extern u32 fcmd_outstanding_cnt;

#if 0//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
fast_code void ftl_core_dtag_free(ncl_w_req_t *ncl_req)
{
	u32 i;
	bool ret = false;
	u32 ttl_du_cnt = ncl_req->req.cnt << DU_CNT_SHIFT;

	for (i = 0; i < ttl_du_cnt; i++) {
		if ((ncl_req->w.lda[i] < TRIM_LDA) && (ncl_req->w.lda[i] != BLIST_LDA)) {
			u32 avail;

			do {
				cbf_avail_ins_sz(avail, &dtag_free_que);
			} while (avail == 0);

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
        #if 0//(BG_TRIM == ENABLE)
        else if(ncl_req->w.lda[i] == TRIM_LDA){
            bg_trim_free_dtag.idx[bg_trim_free_dtag.wptr].dtag = ncl_req->w.pl[i].pl.dtag;
            dmb();
            bg_trim_free_dtag.wptr = (bg_trim_free_dtag.wptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);
        }
        #endif
	}
}
#endif

fast_code void ftl_core_done(ncl_w_req_t *req)
{
	u32 nsid = req->req.nsid;
	u32 type = req->req.type;
	ftl_core_t *fcore = fcns[nsid]->ftl_core[type];
	aspb_t *aspb = &fcore->ps.aspb[req->req.aspb_id];
	p2l_user_t *pu = &fcore->ps.pu[req->req.aspb_id];
	u16 spb_id    = aspb->spb_id;
    u32 valid_du_cnt = (req->req.cnt << DU_CNT_SHIFT) - req->req.padding_cnt;

#ifdef ERRHANDLE_GLIST	// FET, ReduceEHCode
	//if (req->req.op_type.b.prog_err && (aspb->flags.b.err == 0)) // ISU, RdOpBlkHdl
	if ((aspb->flags.b.err == 0) && (req->req.op_type.b.prog_err || rdOpBlkFal[type] == spb_id))
	{
		aspb->flags.b.err = true;
		fcore->err.b.prog_err_blk_in = true;		// (1) Bypass host commit BE (2) Notice dummy done
		if (req->req.op_type.b.prog_err)			// RdOpFal need not skip update, ISU, RdOpBlkHdl
        {    
			fcore->err.b.prog_err_blk_updt = true; 	// Skip update L2P
			if (nsid == INT_NS_ID && type == FTL_CORE_NRM)
            {
                shr_qbt_prog_err = true;          
                sys_assert(qbt_core_ctx);
                qbt_core_ctx->cmpl(qbt_core_ctx);
                qbt_core_ctx = NULL;
            }
        }
		else
		{
			fcore->err.b.rd_open_close = true;		// Notice following handler after dummy done.
		}
		fcore->err.b.dummy_done = false; 			// Notice dummy done to trigger force close, ISU, EH_GC_not_done

		#ifndef EH_GCOPEN_HALT_DATA_IN				// Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
		if (type == FTL_CORE_GC)
			ftl_core_gc_prog_err(aspb->spb_id);
		#endif
	}

    if((rd_open_close_spb[FTL_CORE_NRM] == spb_id || rd_open_close_spb[FTL_CORE_GC] == spb_id) && (fcore->err.b.rd_open_close == 0)){
        fcore->err.b.rd_open_close = true;
        fcore->err.b.prog_err_blk_in = true;
        fcore->err.b.dummy_done = false;
    }

	if (fcore->err.b.prog_err_blk_updt)
		req->req.op_type.b.skip_updt = true;  // TBD_EH, bypass re-write due to CmdTimeout issue, Paul_20201202
#endif

    //wreq first time core_done
	if (req->req.op_type.b.ncl_done == 0) {
		req->req.op_type.b.ncl_done = true;
#if RAID_SUPPORT
        pstream_t* ps= &fcore->ps;
        stripe_user_t *su = &fcns[nsid]->ftl_core[type]->su;
        if (req->req.nsid == 1) {
            sys_assert(req->req.op_type.b.raid);
        }
        if (req->req.op_type.b.raid) {
            if(req->req.op_type.b.parity_mix){
                pending_parity_cnt --;
                if(plp_trigger){
        		    //ftl_raid_trace(LOG_INFO, 0, "pending_parity_cnt %u, page %u", pending_parity_cnt, pda2page(req->w.pda[0]));
                    if(!pending_parity_cnt){
                        if(plp_close_wl_done&&plp_save_epm_done){
                            fsm_ctx_next(&plp_fsm.fsm);
                            fsm_ctx_run(&plp_fsm.fsm);
                        }
                    }
                }
            }
            if(req->req.nsid == INT_NS_ID && req->req.type == FTL_CORE_PBT && req->req.op_type.b.parity_mix){
                extern pbt_dtag_mgr_t pbt_dtag_mgr;
                u8 dtag_id = req->req.dtag_id;
                if(dtag_id < DTAG_RES_CNT){
                    pbt_dtag_mgr.req[dtag_id] = NULL;
                    // ftl_raid_trace(LOG_INFO, 0, "dtag_id:%d id_bmp:%x", dtag_id, pbt_dtag_mgr.id_bmp);
                    u32 set = test_and_clear_bit(dtag_id, &pbt_dtag_mgr.id_bmp);
                    sys_assert(set == 1);
                }
            }
#if XOR_CMPL_BY_FDONE && !XOR_CMPL_BY_CMPL_CTRL
            if (req->req.op_type.b.xor) {
                stripe_pdone_cnt[req->req.stripe_id] --;
                if (stripe_pdone_cnt[req->req.stripe_id] == 0) {
                    ftl_core_pout_and_parity_flush(req->req.stripe_id);
                }
            }
#endif
    		stripe_update(su, req->req.stripe_id, ps);
    	}
#endif

		if (req->req.op_type.b.p2l_nrm)
			p2l_grp_nrm_done(&pu->grp[req->req.p2l_grp_idx_nrm], false);

		if (req->req.op_type.b.p2l_pad)
			p2l_grp_pad_done(&pu->grp[req->req.p2l_grp_idx_pad]);

		valid_du_cnt = req->req.cnt << DU_CNT_SHIFT; 
		valid_du_cnt -= req->req.padding_cnt; 
		valid_du_cnt -= req->req.blklist_dtag_cnt; 
		//p2l table & pgsn table total 5 dtag, p2l lda is not INV_LDA, so 
        // p2l dtag is not included into padding_cnt, we should dec then here to avoid pda update 
        if (req->req.op_type.b.p2l_nrm) { 
            valid_du_cnt -= req->req.p2l_dtag_cnt; 
            //ftl_core_trace(LOG_DEBUG, 0, "p2l req valid cnt:%d", valid_du_cnt); 
        } 

		if (req->req.op_type.b.parity_mix){ 
			if(req->req.op_type.b.slc == 0)
            	valid_du_cnt -= nand_info.bit_per_cell * DU_CNT_PER_PAGE; 
            else
				valid_du_cnt -= DU_CNT_PER_PAGE; 
        } 

		if (req->req.op_type.b.pout) 
			valid_du_cnt = 0; 

		if (valid_du_cnt) { 
			if (nsid == HOST_NS_ID) { 

				bool ret; 
				CBF_INS(&l2p_updt_ntf_que, ret, 0); 
				if(ret == false); 
                { 
					//ftl_core_trace(LOG_INFO, 0, "q full"); 
                } 
				return; 
			} else if (req->req.op_type.b.tbl_updt) { 
				tbl_pda_updt(req);						// TBC_EH 
			} 
		} 
	} 

	req->req.op_type.b.upt_done = true;
 #if 0//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(nsid == INT_NS_ID && type == FTL_CORE_SLC)
		ftl_core_dtag_free(req); // free dtag because slc no need l2p update
#endif	
	if (nsid != INT_NS_ID) {
		u32 _req;
		while (!CBF_EMPTY(&wreq_seq_que[type])) {
			CBF_HEAD(&wreq_seq_que[type], _req);
			ncl_w_req_t *wreq = (ncl_w_req_t*)_req;
			if (wreq->req.op_type.b.upt_done) {
				ftl_core_wreq_updt_done(wreq);
				CBF_REMOVE_HEAD(&wreq_seq_que[type]);
			} else {
				break;
			}
		}
	} else {
		ftl_core_wreq_updt_done(req);
	}
	ftl_core_flush_blklist_need_resume();
}

/*!
 * @brief close current write payload
 *
 * @praam cur_wr_pl	pinter to payload to be closed
 *
 * @return		not used
 */
static inline void ftl_core_force_cur_pl_close(cur_wr_pl_t *cur_wr_pl)
{
	sys_assert(cur_wr_pl->cnt != 0);
	u32 padding_cnt = cur_wr_pl->max_cnt - cur_wr_pl->cnt;
	//ftl_core_trace(LOG_DEBUG, 0, "die Q[%d] pad %d", cur_wr_pl->die, padding_cnt);
	while (padding_cnt) {
		cur_wr_pl->lda[cur_wr_pl->cnt] = INV_LDA;
		cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = WVTAG_ID;
		cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
		cur_wr_pl->cnt++;
		padding_cnt--;
	}

	ftl_core_submit(cur_wr_pl, NULL, NULL);
}
share_data_zi u32 trim_data_send_cnt;
share_data_zi u32 trim_data_recev_cnt;

fast_code ALWAYS_INLINE bool chk_l2p_updt_que_cnt_full(void)
{
    u32 wptr = l2p_updt_que.wptr, rptr = l2p_updt_que.rptr, size = l2p_updt_que.size;
    if ((wptr > rptr && wptr - rptr > 200) || (wptr < rptr && wptr + size - rptr > 200))
        return true;

    return 0;
}


fast_code u32 ftl_core_host_ins_dcomt(u32 *lda, dtag_comt_entry_t *ent, u32 cnt)
{
	u32 i = 0;
	cur_wr_pl_t *cur_wr_pl = NULL;
    //bool FUA_Dtag = false;
    extern volatile u8 plp_trigger;  

	while (i < cnt) {
        if(chk_l2p_updt_que_cnt_full()){
			break;
        }
		fcns[1]->ftl_core[FTL_CORE_NRM]->data_in++;
		if (cur_wr_pl == NULL && ncl_w_reqs_free_cnt > 8 ) {
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(plp_trigger && SLC_init == true && write_type != FTL_CORE_SLC)   
			{
				//SLC_init = true;
				ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_NRM, NULL);
				//ftl_slc_pstream_enable();
				write_nsid = HOST_NS_ID; 
				write_type = FTL_CORE_SLC; 
			}
			cur_wr_pl = ftl_core_get_wpl_mix_mode(write_nsid, write_type,WPL_V_MODE, true, NULL, NULL); 
#else
			cur_wr_pl = ftl_core_get_wpl_mix_mode(1, FTL_CORE_NRM,WPL_V_MODE, true, NULL, NULL);
#endif		
		}

		if (cur_wr_pl == NULL)
			break;
        #if(BG_TRIM == ENABLE)
		if (lda[i] == TRIM_LDA) {
			/* trim info commit */
			if (cur_wr_pl->cnt) {
                if(cur_wr_pl->lda[cur_wr_pl->cnt-1] == TRIM_LDA){
                    Host_Trim_Data *trim_old = (Host_Trim_Data *)ddtag2mem((cur_wr_pl->pl[cur_wr_pl->cnt-1].pl.dtag & DDTAG_MASK));
                    Host_Trim_Data *trim_new = (Host_Trim_Data *)ddtag2mem((ent[i].dtag.dtag & DDTAG_MASK));
                    if((trim_old->all == trim_new->all)
                        && (trim_old->Validtag == trim_new->Validtag)
                        && (trim_old->Validcnt + trim_new->Validcnt <= 4)){
                        u32 discard_dtag = ent[i].dtag.dtag;
                        if(trim_old < trim_new){
                            Host_Trim_Data * temp = trim_old;
                            trim_old = trim_new;
                            trim_new = temp;
                            discard_dtag = cur_wr_pl->pl[cur_wr_pl->cnt-1].pl.dtag;
                            cur_wr_pl->pl[cur_wr_pl->cnt-1].pl.dtag = ent[i].dtag.dtag;
                            cur_wr_pl->pl[cur_wr_pl->cnt-1].pl.btag = ent[i].dtag.dtag;
                        }
                        trim_old->valid_bmp |= trim_new->valid_bmp << trim_old->Validcnt;
                        for(u32 j = 0;j < trim_new->Validcnt; j++){
                            trim_old->Ranges[trim_old->Validcnt].sLDA = trim_new->Ranges[j].sLDA;
                            trim_old->Ranges[trim_old->Validcnt].Length = trim_new->Ranges[j].Length;
                            trim_old->Validcnt ++;
                        }
                        trim_new->Validtag = trim_new->valid_bmp = trim_new->Validcnt = 0;
                        // call CPU3 to free trim dtag
                        bool ret;
                        CBF_INS(&l2p_updt_que, ret, (discard_dtag | BIT(31)));
                        sys_assert(ret == true);
                        CBF_INS(&l2p_updt_ntf_que, ret, 0);
                        // sys_assert(ret == true);

                        trim_data_recev_cnt++;
                        i++;
                        continue;
                    }
                }

				ftl_core_force_cur_pl_close(cur_wr_pl);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
				cur_wr_pl = ftl_core_get_wpl_mix_mode(write_nsid, write_type,WPL_V_MODE, true, NULL, NULL); 
#else
				cur_wr_pl = ftl_core_get_wpl_mix_mode(1, FTL_CORE_NRM,WPL_V_MODE, true, NULL, NULL); 
#endif

				if (cur_wr_pl == NULL)
					break;
			}
			cur_wr_pl->lda[cur_wr_pl->cnt] = TRIM_LDA;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = ent[i].dtag.dtag;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.btag = ent[i].dtag.dtag;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			cur_wr_pl->cnt++;
            // ftl_core_force_cur_pl_close(cur_wr_pl);
			trim_data_recev_cnt++;
			//trim_data=true;
// 			cur_wr_pl = NULL;
			i++;
			continue;
		}else if(cur_wr_pl->cnt && cur_wr_pl->lda[cur_wr_pl->cnt-1] == TRIM_LDA){
            ftl_core_force_cur_pl_close(cur_wr_pl);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			cur_wr_pl = ftl_core_get_wpl_mix_mode(write_nsid, write_type,WPL_V_MODE, true, NULL, NULL); 
#else
			cur_wr_pl = ftl_core_get_wpl_mix_mode(1, FTL_CORE_NRM,WPL_V_MODE, true, NULL, NULL); 
#endif
			if (cur_wr_pl == NULL)
				break;
        }
        #endif
		cur_wr_pl->lda[cur_wr_pl->cnt] = lda[i];
		if (ent[i].dtag.b.type_ctrl & BTN_SEMI_STREAMING_MODE) {
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = (ent[i].dtag.dtag & SEMI_SDTAG_MASK);
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.btag = (ent[i].dtag.dtag >> SEMI_DDTAG_SHIFT);
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM;
		} else {
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = ent[i].dtag.dtag;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.btag = ent[i].dtag.dtag;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
		}

		if (++cur_wr_pl->cnt == cur_wr_pl->max_cnt) {
			ftl_core_submit(cur_wr_pl, NULL, NULL);
			cur_wr_pl = NULL;
		}

		i++;
	}

    //if((cur_wr_pl != NULL) && (FUA_Dtag))
    {
        //ftl_core_trace(LOG_ERR, 0, "force flush call close open die\n");
        //ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_NRM, NULL);
    }
	return i;
}
#if 0
fast_code cur_wr_pl_t *ftl_core_get_wpl(u32 nsid, u32 type)
{
	ncl_w_req_t *req;
	core_wl_t *cwl = &fcns[nsid]->ftl_core[type]->cwl;
	pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;
	cur_wr_pl_t *wr_pl = &fcns[nsid]->ftl_core[type]->cur_wr_pl;
	stripe_user_t *su = &fcns[nsid]->ftl_core[type]->su;
    bool again_flag = false;
    u32 tpage = 0;
    u32 cpage = 0;

	if (wr_pl->cnt != 0)
		return wr_pl;

again:
	if (cwl->cur.mpl_qt == 0) {
		if (ps->avail_cnt == 0) {
			if (pstream_rsvd(ps))
				goto again;
			return NULL;
		}

		pstream_get_cwl(ps, cwl);
	}

	if (cwl->cur.open_die) {
		sys_assert(cwl->rid[cwl->cur.start_die] != CWL_FREE_RID);
		cwl->cur.die = cwl->cur.start_die;
	} else if (cwl->cur.die_qt) {
		sys_assert(cwl->rid[cwl->cur.next_open] == CWL_FREE_RID);
		req = get_ncl_w_req(nsid, false);
		if (req == NULL) {
			if (nsid == HOST_NS_ID && type == FTL_CORE_GC)
				fcns[nsid]->ftl_core[type]->err.b.wreq_pend = true;

			//ftl_core_trace(LOG_WARNING, 0, "ns %d type %d die %d get wreq fail",
			//	nsid, type, cwl->cur.next_open);
			return NULL;
		}

		fcns[nsid]->wreq_cnt++;
		fcns[nsid]->ftl_core[type]->wreq_cnt++;



		req->req.padding_cnt = 0;
		req->req.cnt = 0;
		req->req.cpage = 0;
		req->req.tpage = cwl->cur.page;
		req->req.nsid = nsid;
		req->req.die_id = cwl->cur.next_open;
		req->req.op_type.all = 0;
		req->req.op_type.b.raid = fcns_raid_enabled(nsid);
        req->req.parity_die = ps->aspb[ps->curr].parity_die;
		req->req.wunc_cnt = 0;
        req->req.bad_die_cnt = ps->aspb[ps->curr].total_bad_die_cnt;

        if (nsid != INT_NS_ID) {
            if (req->req.op_type.b.raid && (req->req.die_id != ps->aspb[ps->curr].parity_die)) {
                bool ret;
                CBF_INS(&wreq_seq_que[type], ret, (u32)req);
                sys_assert(ret);
            } else if (!req->req.op_type.b.raid) {
                bool ret;
                CBF_INS(&wreq_seq_que[type], ret, (u32)req);
                sys_assert(ret);
            }
        }
		cwl->cur.open_die++;
		cwl->cur.die = cwl->cur.next_open;
		cwl->rid[cwl->cur.die] = req->req.id;

#if 0
		if ((cwl->flags.b.slc) && (type == FTL_CORE_NRM) && (nsid == 1))
			exp_fact = ftl_core_get_exp_factor(cwl, ps);
#endif

		//req->req.mp_du_cnt = ftl_core_next_write(nsid, type) * exp_fact;
		cwl->cur.die_qt -= 1;
		cwl->cur.next_open += 1;

#ifdef SKIP_MODE
		cwl->cur.skip_die_cnt=0;
		req->req.aspb_id = ps->curr;
		cwl->cur.skip_die_cnt = pstream_get_pda(ps, req->w.pda, req->w.l2p_pda, cwl->cur.page, 1, req);
        if (req->req.op_type.b.one_pln) {
            req->req.tpage >>= 1;
            cwl->cur.mpl_qt -= nand_info.bit_per_cell;
            cwl->cur.usr_mpl_qt -= nand_info.bit_per_cell;
        }
#else
		req->req.aspb_id = pstream_get_pda(ps, req->w.pda, req->w.l2p_pda, cwl->cur.page, 1);
#endif

#ifdef SKIP_MODE
		//req->req.mp_du_cnt = ftl_core_next_write(nsid, type, req->req.op_type.b.one_pln);
		req->req.mp_du_cnt = ftl_core_next_write(nsid, type);
#else
		req->req.mp_du_cnt = ftl_core_next_write(nsid, type);
#endif

		req->req.type = type;
		//req->req.die_cnt = exp_fact;
	}

	sys_assert(cwl->rid[cwl->cur.die] != CWL_FREE_RID);
	req = ncl_rid_to_req(cwl->rid[cwl->cur.die]);
	sys_assert(req->req.cpage < req->req.tpage);

	u32 du_ofst = req->req.mp_du_cnt * req->req.cpage;
	wr_pl->pda = req->w.pda + (du_ofst >> DU_CNT_SHIFT);
	wr_pl->lda = req->w.lda + du_ofst;
	wr_pl->pl = req->w.pl + du_ofst;
	wr_pl->cnt = 0;
	wr_pl->die = cwl->cur.die;
	wr_pl->flags.all = 0;
	wr_pl->flags.b.slc = cwl->flags.b.slc;
#ifdef SKIP_MODE
	wr_pl->flags.b.one_pln = req->req.op_type.b.one_pln;
#endif
	wr_pl->flags.b.raid = req->req.op_type.b.raid;
	wr_pl->flags.b.trim_info = false;
	wr_pl->nsid = nsid;
	wr_pl->max_cnt = req->req.mp_du_cnt;

	req->req.cpage++;
	cwl->cur.mpl_qt -= 1;

    if (nsid == HOST_NS_ID) {
        //ftl_core_trace(LOG_INFO, 0, "req:%x cp:%d die:%d pda:%x", req, req->req.cpage, req->req.die_id, wr_pl->pda[0]);
        if (cwl->cur.die == nand_info.lun_num)
            cwl->cur.die = cwl->cur.start_die;
    } else {
        if (cwl->cur.die == nand_info.lun_num)
            cwl->cur.die = cwl->cur.start_die;
    }

    tpage = req->req.tpage;  //fix p2l dump bug, after p2l submit to ncl,tpage maybe change
    cpage = req->req.cpage;

    if ((cpage & 0x01) == 0)
	    cwl->cur.die += 1;

	if (cpage == tpage) {
		cwl->cur.start_die += 1;
	}

    aspb_t *aspb = &ps->aspb[req->req.aspb_id];
    if ((fcns_p2l_enabled(nsid)) && (req->req.die_id == aspb->p2l_die)) {
        u16 page = (u16)pda2page(wr_pl->pda[0]);
        u8 plane = (u8)pda2plane(wr_pl->pda[0]);
        if (((page == aspb->p2l_page) && (plane == aspb->p2l_plane)) || ((page == aspb->pgsn_page) && (plane == aspb->pgsn_plane))) {
            cwl->cur.usr_mpl_qt--;
            req->req.op_type.b.p2l_nrm = true;
            if ((page == aspb->p2l_page) && (plane == aspb->p2l_plane)) {
                req->req.p2l_page = page % nand_info.bit_per_cell;
                p2l_req_pl_ins(aspb->p2l_grp_id, &ps->pu[req->req.aspb_id], req, true);     //ins p2l table
            } else if ((page == aspb->pgsn_page) && (plane == aspb->pgsn_plane)) {
                req->req.pgsn_page = page % nand_info.bit_per_cell;
                p2l_req_pl_ins(aspb->p2l_grp_id, &ps->pu[req->req.aspb_id], req, false);    //ins pgsn table
            } else {
                panic("impossiable");
            }
#ifdef SKIP_MODE
            wr_pl->cnt += ftl_core_next_write(nsid, type);
#else
            wr_pl->cnt += ftl_core_next_write(nsid, type);
#endif
            if (type == FTL_CORE_GC)
                ftl_core_submit(wr_pl, NULL, ftl_core_gc_wr_done);
            else
                ftl_core_submit(wr_pl, NULL, NULL);
            if ((page == max(aspb->p2l_page, aspb->pgsn_page)) && (plane == max(aspb->p2l_plane, aspb->pgsn_plane))) {
                p2l_get_next_pos(aspb, &ps->pu[req->req.aspb_id], nsid);
            }
            again_flag = true;
            goto check_skip;
        }
    }

	if (req->req.op_type.b.raid) {
		wr_pl->stripe_id = su->active->stripe_id;

		//skip parity die & get next wr_pl

		if (aspb->parity_die == wr_pl->die) {
			if (su->active->ncl_req == NULL) {
				su->active->ncl_req = req;
				if (su->active->wait == false) {
					QSIMPLEQ_INSERT_TAIL(&su->wait_que, su->active, link);
					su->active->wait = true;
				}
			}
			//wr_pl->cnt += req->req.mp_du_cnt;
			if (req->req.cpage == req->req.tpage)
			    cwl->cur.open_die--;
            again_flag = true;
			goto check_skip;
		} else {
			cwl->cur.usr_mpl_qt -= 1;
		}
	} else {
		cwl->cur.usr_mpl_qt -= 1;
	}

check_skip:
#ifdef SKIP_MODE
	if (tpage == cpage)
	{
		if(cwl->cur.skip_die_cnt!=0)
		{
			cwl->cur.next_open += cwl->cur.skip_die_cnt;
			cwl->cur.start_die += cwl->cur.skip_die_cnt;
			cwl->cur.die_qt -= cwl->cur.skip_die_cnt;

			if((cwl->cur.skip_die_cnt * cwl->cur.page) > cwl->cur.mpl_qt)
			{
				cwl->cur.mpl_qt=0;
			}
			else
			{
				cwl->cur.mpl_qt -= (cwl->cur.skip_die_cnt * cwl->cur.page);
			}

			if((cwl->cur.skip_die_cnt * cwl->cur.page) > cwl->cur.usr_mpl_qt)
			{
				cwl->cur.usr_mpl_qt=0;
			}
			else
			{
				cwl->cur.usr_mpl_qt -= (cwl->cur.skip_die_cnt * cwl->cur.page);
			}

            //ftl_core_trace(LOG_DEBUG, 0, "bad skip mpl_qt:%d", cwl->cur.usr_mpl_qt);
		}
        if ((cwl->cur.mpl_qt == 0) && fcns_raid_enabled(nsid) && su->active) {
    		req = ncl_rid_to_req(cwl->rid[req->req.parity_die]);
    		cwl->rid[req->req.parity_die] = CWL_FREE_RID;
    		//xor haven't done, so wait
    		if (su->active->wait == false) {
    			QSIMPLEQ_INSERT_TAIL(&su->wait_que, su->active, link);
    			su->active->wait = true;
    		}
    		su->active = NULL;
    	}
	}
#endif

    if (again_flag) {
        again_flag = false;
        goto again;
    }
	return wr_pl;
}
#endif

fast_code void ftl_core_set_wreq_pend(u32 nsid, u32 type, bool value)
{
    fcns[nsid]->ftl_core[type]->err.b.wreq_pend = value;
}

fast_code void ftl_core_host_data_in(u32 nsid, u32 type, bool operation)
{
    if(operation)
        fcns[nsid]->ftl_core[type]->data_in++;
    else
        fcns[nsid]->ftl_core[type]->data_in--;
}

fast_code cur_wr_pl_t *ftl_core_get_wpl_mix_mode(u32 nsid, u32 type, bool wpl_mode, bool start_new_blk, void* caller, u8 *reason)
{
	ncl_w_req_t *req;
	core_wl_t *cwl = &fcns[nsid]->ftl_core[type]->cwl;
	pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;
	cur_wr_pl_t *wr_pl = &fcns[nsid]->ftl_core[type]->cur_wr_pl;
	stripe_user_t *su = &fcns[nsid]->ftl_core[type]->su;
    bool again_flag = false;
	u32 tpage = 0;
	u32 cpage = 0;

	if (wr_pl->cnt != 0)
		return wr_pl;
	*reason = no_reason;
again:
	if (cwl->cur.mpl_qt == 0) {
		if (ps->avail_cnt == 0) {
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
			if(delay_flush_spor_qbt == true && ((nsid != INT_NS_ID) || (type != FTL_CORE_NRM))){ 
				if (reason) { 
					*reason = spor_flush_qbt; 
				} 
				return NULL; 
			} 
#endif
			if(CBF_EMPTY(ps->spb_que) && ps->flags.b.queried == 0 && ps->flags.b.query_nxt == 0){
				//if(ps->type != FTL_CORE_GC && ps->type != FTL_CORE_PBT){
				if(ps->type != FTL_CORE_PBT){  //To improve speed of GC open new blk
					ps->flags.b.query_nxt = 1;
				}
			}
			if (pstream_rsvd(ps))
				goto again;

			if (reason) {
				*reason = ps_not_avail;
			}
			return NULL;
		}

		if(blist_getwpl == false && ps->flags.b.flush_blklist_start){
			if (reason) {
				*reason = blk_list_flushing;
			}
			return NULL;
		}

        if (parity_allocate_cnt >= MAX_STRIPE_ID) { // 31 / 3 = 10
			if (reason) {
				*reason = parity_allocate_not_enough;
			}
            return NULL;
        }
        #if 0
        if (nsid == HOST_NS_ID) {       //host nsid each pstream max 2 stripe
            if (ps->parity_cnt >= 2) {
                //extern u32 ncl_w_reqs_free_cnt;
                //ftl_core_trace(LOG_INFO, 0, "nsid:%d type:%d parity abort req:%d", nsid, type, ncl_w_reqs_free_cnt);
                return NULL;
            }
        }
        else if (ps->parity_cnt >= 1) { //ftl nsid each pstream max 1 stripe,modify here for qbt case
            return NULL;
        }
        #endif
		pstream_get_cwl(ps, cwl);
	}

	if(blist_getwpl == false && ps->flags.b.flush_blklist_start){
		if (reason) {
			*reason = blk_list_flushing;
		}
		return NULL;
	}
#ifdef SKIP_MODE
    if ((wpl_mode == WPL_H_MODE) && (cwl->cur.open_die == (MAX_CHANNEL - cwl->cur.total_skip_cnt_in_ce) || cwl->cur.handle_ce_die !=0)){
        u8 bitnum;
        if( cwl->cur.die % MAX_CHANNEL == 0){
            cwl->cur.die = (cwl->cur.die - MAX_CHANNEL);
            cwl->cur.handle_ce_die+= 1;
            cwl->cur.die +=find_next_bit(&cwl->cur.good_die_bm[(cwl->cur.handle_ce_die - 1)%2], MAX_CHANNEL, 0);
        }else{
            if((bitnum = find_next_bit(&cwl->cur.good_die_bm[(cwl->cur.handle_ce_die - 1)%2], MAX_CHANNEL, cwl->cur.die % MAX_CHANNEL)) >= 8)
            {
                cwl->cur.handle_ce_die+= 1;
                bitnum = find_next_bit(&cwl->cur.good_die_bm[(cwl->cur.handle_ce_die - 1)%2], MAX_CHANNEL, 0);
            }
            cwl->cur.die = cwl->cur.die & 0xFFF8;
            cwl->cur.die+= bitnum;
        }
        //if(show_log)
        //{
        //    if(cwl->cur.handle_ce_die == 1)
        //    {
        //        ftl_core_trace(LOG_ERR, 0, "[J]BM0[%x],BM1[%x]\n",cwl->cur.good_die_bm[0],cwl->cur.good_die_bm[1]);
        //    }
        //    ftl_core_trace(LOG_ERR, 0, "[J]die num[%d],open die[%d],ce_die[%d]\n",cwl->cur.die,cwl->cur.open_die,cwl->cur.handle_ce_die);
        //}
	}
#endif
    else if ((wpl_mode == WPL_V_MODE) && cwl->cur.open_die) {
        //if(show_log)
        //{
        //    ftl_core_trace(LOG_ERR, 0, "s_id:%d open_die:%d\n",cwl->cur.start_die,cwl->cur.open_die);
        //}
		sys_assert(cwl->rid[cwl->cur.start_die] != CWL_FREE_RID);
		cwl->cur.die = cwl->cur.start_die;
	} else if (cwl->cur.die_qt) {
	    //if(show_log )//&&cwl->rid[cwl->cur.next_open] != CWL_FREE_RID)
        //{
        //   ftl_core_trace(LOG_ERR, 0, "n_open[%x],open_die[%x],sk_in_ce[%x]\n",cwl->cur.next_open,cwl->cur.open_die,cwl->cur.total_skip_cnt_in_ce);
        //}
		sys_assert(cwl->rid[cwl->cur.next_open] == CWL_FREE_RID);
		req = get_ncl_w_req(nsid, type, false);
		if (req == NULL) {
			//if (nsid == HOST_NS_ID && type == FTL_CORE_GC)
				//fcns[nsid]->ftl_core[type]->err.b.wreq_pend = true;

			//ftl_core_trace(LOG_WARNING, 0, "ns %d type %d die %d get wreq fail",
			//	nsid, type, cwl->cur.next_open);
			if (reason) {
				*reason = wreqs_not_enough;
			}
			return NULL;
		}

		fcns[nsid]->wreq_cnt++;
		fcns[nsid]->ftl_core[type]->wreq_cnt++;
		req->req.caller = NULL;
        req->req.p2l_page = 0xFF;
        req->req.pgsn_page = 0xFF;
        req->w.spb_sn = ps->aspb[ps->curr].sn;  // added by Sunny, for meta block sn
		req->req.padding_cnt = 0;
		req->req.cnt = 0;
		req->req.cpage = 0;
		req->req.tpage = cwl->cur.page;
		req->req.nsid = nsid;
		req->req.die_id = cwl->cur.next_open;
		req->req.op_type.all = 0;
		req->req.op_type.b.raid = fcns_raid_enabled(nsid);
        req->req.parity_die = ps->aspb[ps->curr].parity_die;
		req->req.wunc_cnt = 0;
        req->req.bad_die_cnt = ps->aspb[ps->curr].total_bad_die_cnt;
        req->req.p2l_dtag_cnt = 0;
        req->req.trim_page = 0;
        req->req.pad_sidx = 0;
        req->req.op_type.b.plp_tag = ps->aspb[ps->curr].flags.b.plp_tag; 
		t_cpage[nsid][type]=req->req.cpage;
		t_tpage[nsid][type]=req->req.tpage;
		req->req.blklist_dtag_cnt = 0; 
		u32 i=0;
		for(i=0;i<3;i++){
			req->req.blklist_pg[i] = 0xFF;
		}

        if (nsid != INT_NS_ID) {
            if (req->req.op_type.b.raid && ((req->req.die_id != req->req.parity_die)
                || (ps->aspb[ps->curr].flags.b.parity_mix))) {
                bool ret;
                CBF_INS(&wreq_seq_que[type], ret, (u32)req);
                sys_assert(ret);
            } else if (!req->req.op_type.b.raid) {
                bool ret;
                CBF_INS(&wreq_seq_que[type], ret, (u32)req);
                sys_assert(ret);
            }
        }
		cwl->cur.open_die++;
		cwl->cur.die = cwl->cur.next_open;
		cwl->rid[cwl->cur.die] = req->req.id;

#if 0
		if ((cwl->flags.b.slc) && (type == FTL_CORE_NRM) && (nsid == 1))
			exp_fact = ftl_core_get_exp_factor(cwl, ps);
#endif

		//req->req.mp_du_cnt = ftl_core_next_write(nsid, type) * exp_fact;
		cwl->cur.die_qt -= 1;
		cwl->cur.next_open += 1;

#ifdef SKIP_MODE
		cwl->cur.skip_die_cnt=0;
		req->req.aspb_id = ps->curr;
		cwl->cur.skip_die_cnt = pstream_get_pda(ps, req->w.pda, req->w.pda, cwl->cur.page, 1, req);

		if (req->req.op_type.b.pln_write) {
        u8 bad_page_cnt = (shr_nand_info.geo.nr_planes - req->req.op_type.b.pln_write) * nand_info.bit_per_cell;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(cwl->flags.b.slc)
        		bad_page_cnt /= nand_info.bit_per_cell;
#endif		
            req->req.tpage -= bad_page_cnt;
            cwl->cur.mpl_qt -= bad_page_cnt;
            cwl->cur.usr_mpl_qt -= bad_page_cnt;
        }

		t_tpage[nsid][type]=req->req.tpage;
        //set_bit(cwl->cur.die % MAX_CHANNEL, &cwl->cur.good_die_bm);
#else
		req->req.aspb_id = pstream_get_pda(ps, req->w.pda, req->w.l2p_pda, cwl->cur.page, 1);
		req->req.op_type.b.pln_st = 0;
#endif

        if(!ps->avail_cnt && ps->nsid == HOST_NS_ID){
            req->req.op_type.b.last_req = 1;
            ps->flags.b.closing = 1; 
        }
#ifdef SKIP_MODE
		//req->req.mp_du_cnt = ftl_core_next_write(nsid, type, req->req.op_type.b.one_pln);
		req->req.mp_du_cnt = ftl_core_next_write(nsid, type);
#else
		req->req.mp_du_cnt = ftl_core_next_write(nsid, type);
#endif

		req->req.type = type;
		//req->req.die_cnt = exp_fact;
		#if (PBT_OP == mENABLE)
		if(ps->nsid == INT_NS_ID && ps->type == FTL_CORE_PBT){
			if(tFtlPbt.dump_pbt_start == mTRUE){
				pstream_t *ps = &fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps;
				if((ps->aspb[req->req.aspb_id].spb_id != tFtlPbt.pbt_spb_cur) && (ps->aspb[req->req.aspb_id].spb_id > 0)){
					tFtlPbt.pbt_spb_cur = ps->aspb[req->req.aspb_id].spb_id;
                    tFtlPbt.pbt_cur_loop++;
                    if(tFtlPbt.pbt_cur_loop == (FTL_MAX_PBT_CNT+1))
                    {
                        tFtlPbt.pbt_cur_loop = 1;
                    }
                    shr_pbt_loop = tFtlPbt.pbt_cur_loop;

					sys_assert(tFtlPbt.pbt_spb_cur);
					//ftl_core_trace(LOG_INFO, 0, "[PBT]set pbt_cur_spb:%d, dummyflag:%d",tFtlPbt.pbt_spb_cur,tFtlPbt.isfilldummy);
					if(tFtlPbt.isfilldummy == mTRUE){
						//ftl_core_trace(LOG_INFO, 0, "[PBT]filldummy done cus switch block");
						ftl_set_pbt_filldummy_done();
						tFtlPbt.isfilldummy = mFALSE;
						if(tFtlPbt.force_dump_pbt == true && \
						(tFtlPbt.force_dump_pbt_mode == DUMP_PBT_FORCE_FINISHED_CUR_GROUP || \
						tFtlPbt.force_dump_pbt_mode == DUMP_PBT_NORMAL_PARTIAL || \
						tFtlPbt.force_dump_pbt_mode == DUMP_PBT_RD || \
						tFtlPbt.force_dump_pbt_mode == DUMP_PBT_BGTRIM || \
						tFtlPbt.force_dump_pbt_mode == DUMP_PBT_SHUTTLE)){	// ISU, EHPerfImprove
							ftl_set_force_dump_pbt(0,false,DUMP_PBT_REGULAR);
						}
					}else{
						//ftl_core_trace(LOG_INFO, 0, "[PBT]switch new aspb:0x%x",tFtlPbt.pbt_spb_cur);
					}

                    ftl_core_trace(LOG_ALW, 0xf33a, "[PBT]pbt_cur_loop: 0x%x",tFtlPbt.pbt_cur_loop); // added by Sunny for dbg
				}
			}
		}
		#endif
	}

	sys_assert(cwl->rid[cwl->cur.die] != CWL_FREE_RID);
	req = ncl_rid_to_req(cwl->rid[cwl->cur.die]);


    aspb_t *aspb = &ps->aspb[req->req.aspb_id];

	sys_assert(req->req.cpage < req->req.tpage);

	u32 du_ofst = req->req.mp_du_cnt * req->req.cpage;
#ifdef SKIP_MODE
		wr_pl->pda = req->w.pda + (du_ofst >> DU_CNT_SHIFT);
#else
		if (req->w.pda[0] != INV_PDA)
			wr_pl->pda = req->w.pda + (du_ofst >> DU_CNT_SHIFT);
		else
			wr_pl->pda = req->w.l2p_pda + (du_ofst >> DU_CNT_SHIFT);
#endif

	wr_pl->lda = req->w.lda + du_ofst;
	wr_pl->pl = req->w.pl + du_ofst;
	wr_pl->cnt = 0;
	wr_pl->die = cwl->cur.die;
	wr_pl->flags.all = 0;
	wr_pl->flags.b.slc = cwl->flags.b.slc;
    wr_pl->flags.b.parity_mix = (req->req.parity_die == cwl->cur.die && aspb->flags.b.parity_mix);

#ifdef SKIP_MODE
		wr_pl->flags.b.one_pln = req->req.op_type.b.one_pln; //After plane raid successul should be delete
		wr_pl->flags.b.pln_write = req->req.op_type.b.pln_write;
#else
		wr_pl->flags.b.pln_st = req->req.op_type.b.pln_st;
#endif
	wr_pl->flags.b.raid = req->req.op_type.b.raid;
	wr_pl->flags.b.trim_info = false;
	wr_pl->nsid = nsid;
	wr_pl->max_cnt = req->req.mp_du_cnt;

	req->req.cpage++;
	t_cpage[nsid][type]=req->req.cpage;
	cwl->cur.mpl_qt -= 1;

    if (nsid == HOST_NS_ID) {
        //ftl_core_trace(LOG_INFO, 0, "req:%x cp:%d die:%d pda:%x", req, req->req.cpage, req->req.die_id, wr_pl->pda[0]);
        if (cwl->cur.die == nand_info.lun_num)
            cwl->cur.die = cwl->cur.start_die;
    } else {
        if (cwl->cur.die == nand_info.lun_num)
            cwl->cur.die = cwl->cur.start_die;
    }

	tpage = req->req.tpage;
	cpage = req->req.cpage;


	cwl->cur.die += 1;

	if ((cpage == 1 && wpl_mode == WPL_H_MODE) || (cpage == tpage && wpl_mode == WPL_V_MODE))
	{
		cwl->cur.start_die += 1;//Don move this code position down,if p2l execute  ftl_core_submit the req->req.tpage will change to 3 and the condition (req->req.cpage == req->req.tpage) is not established (v-mode)
	}
    if (req->req.op_type.b.raid) { 
        wr_pl->stripe_id = su->active->stripe_id;

            //skip parity die & get next wr_pl 

            if (aspb->parity_die == wr_pl->die) { 
                if (su->active->ncl_req == NULL) { 
                    su->active->ncl_req = req; 

                    if(!ps->aspb[ps->curr].flags.b.parity_mix){
                        req->req.op_type.b.parity = 1; 
                        fcns[nsid]->parity_wreq_cnt++; 
                        fcns[nsid]->ftl_core[type]->parity_wreq_cnt++; 
                    }

                    if (su->active->wait == false) { 
                        QSIMPLEQ_INSERT_TAIL(&su->wait_que, su->active, link); 
                        su->active->wait = true; 
                            /* 
                            if(ps->nsid == INT_NS_ID) 
                            { 
                                u32 page_idx = wr_pl->cnt >> DU_CNT_SHIFT; 
                                u32 du_offst = wr_pl->cnt & (DU_CNT_PER_PAGE - 1); 
                                pda_t pda = wr_pl->pda[page_idx] + du_offst; 
                                ftl_core_trace(LOG_INFO, 0, "[%d]pda:0x%x",ps->type,pda); 
                            } 
                            */ 
                    } 
                } 
                if(!ps->aspb[ps->curr].flags.b.parity_mix){
                    //wr_pl->cnt += req->req.mp_du_cnt; 
			        if (req->req.cpage == req->req.tpage)
                        cwl->cur.open_die--;
                    again_flag = true; 
                    goto check_skip; 
                }else{
                    if (pda2plane(wr_pl->pda[0]) == ps->aspb[ps->curr].parity_die_pln_idx) {
                        u32 pl_idx = (req->req.cpage - 1) * DU_CNT_PER_PAGE;
                        cwl_stripe_t *stripe = su->active;
                        u8 plane_num = req->req.op_type.b.pln_write ? req->req.op_type.b.pln_write : nand_plane_num();
                        for (u32 i = 0; i < DU_CNT_PER_PAGE; i++) {  //total 4 dtag. 1 plane . 3 PAGE
                            req->w.pl[pl_idx + i].all = 0;
                            req->w.pl[pl_idx + i].pl.dtag = stripe->parity_buffer_dtag[(pl_idx/(DU_CNT_PER_PAGE*plane_num))*DU_CNT_PER_PAGE + i].dtag;
                            req->w.lda[pl_idx + i] = PARITY_LDA;
                            req->w.pl[pl_idx + i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
                        }
                        wr_pl->flags.b.parity = 1;
                        wr_pl->cnt += ftl_core_next_write(nsid, type);
                        ncl_w_req_t * req2 = ftl_core_submit(wr_pl, NULL, NULL);
                        if(req2){ // p2l page is last page of thie die.
                        	if (open_ctrl.free_cnt != MAX_SPBPAD_CNT) {
                        		for (u8 i = 0; i < MAX_SPBPAD_CNT; ++i) {
                        			ftl_spb_pad_t *cur = &open_ctrl.spb_pad[i];
                        			if (req->req.caller == (void*)cur) {
                                //         if(plp_trigger){
                                // 		    ftl_core_trace(LOG_INFO, 0, "req %x, die %u, page %u, spb_pad %x dummy_done_cnt %u, dummy_req_cnt %u",req, req->req.die_id, pda2page(req->w.pda[0]),
                                //                 cur, cur->dummy_done_cnt+1, cur->dummy_req_cnt);
                                //         }
                                        cur->dummy_req_cnt++;
                        			}
                        		}
                        	}
                        }

                        again_flag = true;
                        goto check_skip;
                    }
                }
            } else { 
                cwl->cur.usr_mpl_qt -= 1; 
            } 
        } else { 
        cwl->cur.usr_mpl_qt -= 1; 
    }

#if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)
    if (ps->flags.b.p2l_enable ||plp_spb_flush_p2l) && (req->req.die_id == aspb->p2l_die))
#else
	if (ps->flags.b.p2l_enable && (req->req.die_id == aspb->p2l_die))
#endif
	{
        u16 page = (u16)pda2page(wr_pl->pda[0]);
        u8 plane = (u8)pda2plane(wr_pl->pda[0]);
        if (((page == aspb->p2l_page) && (plane == aspb->p2l_plane)) || ((page == aspb->pgsn_page) && (plane == aspb->pgsn_plane))) {
            // cwl->cur.usr_mpl_qt--;
            req->req.op_type.b.p2l_nrm = true;
            if ((page == aspb->p2l_page) && (plane == aspb->p2l_plane)) {
                req->req.p2l_page = page % nand_info.bit_per_cell;
                p2l_req_pl_ins(aspb->p2l_grp_id, &ps->pu[req->req.aspb_id], req, true);     //ins p2l table
            } else if ((page == aspb->pgsn_page) && (plane == aspb->pgsn_plane)) {
                req->req.pgsn_page = page % nand_info.bit_per_cell;
                p2l_req_pl_ins(aspb->p2l_grp_id, &ps->pu[req->req.aspb_id], req, false);    //ins pgsn table
            } else {
                panic("impossiable");
            }
#ifdef SKIP_MODE
            wr_pl->cnt += ftl_core_next_write(nsid, type);
#else
            wr_pl->cnt += ftl_core_next_write(nsid, type);
#endif
            ncl_w_req_t * req2 = NULL;

            if (type == FTL_CORE_GC)
                req2 = ftl_core_submit(wr_pl, caller, ftl_core_gc_wr_done);
            else
                req2 = ftl_core_submit(wr_pl, caller, NULL);

            if(req2){ // p2l page is last page of thie die.
                if (open_ctrl.free_cnt != MAX_SPBPAD_CNT) {
                    for (u8 i = 0; i < MAX_SPBPAD_CNT; ++i) {
                        ftl_spb_pad_t *cur = &open_ctrl.spb_pad[i];
                        if (req->req.caller == (void*)cur) {
                            // if(plp_trigger){
                            //     ftl_core_trace(LOG_INFO, 0, "req %x, die %u, page %u, spb_pad %x dummy_done_cnt %u, dummy_req_cnt %u",req, req->req.die_id, pda2page(req->w.pda[0]),
                            //         cur, cur->dummy_done_cnt+1, cur->dummy_req_cnt);
                            // }
                            cur->dummy_req_cnt++;
                        }
                    }
                }
            }

            if ((page == max(aspb->p2l_page, aspb->pgsn_page))
                && (plane == max(aspb->p2l_plane, aspb->pgsn_plane))) {
                p2l_get_next_pos(aspb, &ps->pu[req->req.aspb_id], nsid);
            }
            again_flag = true;
            goto check_skip;
        }
    }

check_skip:
#ifdef SKIP_MODE
    if ((cpage == 1 && wpl_mode == WPL_H_MODE) || (cpage == tpage && wpl_mode == WPL_V_MODE))
	{
		//if(cwl->cur.skip_die_cnt!=0 && cwl->cur.next_open < 128)
		if(cwl->cur.skip_die_cnt!=0 && cwl->cur.next_open < shr_nand_info.lun_num)
		{
		    //if(show_log)
            //{
            //    ftl_core_trace(LOG_ERR, 0, "skip_die_cnt[%x]\n",cwl->cur.skip_die_cnt);
            //}
			cwl->cur.next_open += cwl->cur.skip_die_cnt;
			cwl->cur.start_die += cwl->cur.skip_die_cnt;
			cwl->cur.die_qt -= cwl->cur.skip_die_cnt;
#if(H_MODE_WRITE == mENABLE) 
			u32 over_ce_num = 0;
		    u32 start_bit = cwl->cur.die % MAX_CHANNEL;
            if((wpl_mode == WPL_H_MODE) && (start_bit != 0))
            {
                 u32 changeSkipCnt = false;
                 u32 skipCnt;

				if(MAX_CHANNEL - start_bit < cwl->cur.skip_die_cnt)
				{
					over_ce_num = cwl->cur.skip_die_cnt - (MAX_CHANNEL - start_bit);
				}
                cwl->cur.total_skip_cnt_in_ce+= (cwl->cur.skip_die_cnt - over_ce_num);

                for(skipCnt = 0; skipCnt < cwl->cur.skip_die_cnt; skipCnt++)
                {
                    u32 bad_die_id;
                    bad_die_id = start_bit + skipCnt;
                    if(bad_die_id < MAX_CHANNEL)
                    {
                        clear_bit(bad_die_id, &cwl->cur.good_die_bm[0]);
                        clear_bit(bad_die_id, &cwl->cur.good_die_bm[1]);
                    }
                }

				//if(cwl->cur.die + cwl->cur.skip_die_cnt > 128)
				if(cwl->cur.die + cwl->cur.skip_die_cnt > shr_nand_info.lun_num)
				{
					changeSkipCnt = true;
				}

                if(cwl->cur.skip_die_cnt >= (MAX_CHANNEL - start_bit))
                {
                    cwl->cur.die += (MAX_CHANNEL- start_bit);
                }else{
                    cwl->cur.die += cwl->cur.skip_die_cnt;
                }

				if(changeSkipCnt)
				{
					cwl->cur.skip_die_cnt -= over_ce_num;
				}

            }
#endif
			if((cwl->cur.skip_die_cnt * cwl->cur.page) > cwl->cur.mpl_qt)
			{
				cwl->cur.mpl_qt=0;
			}
			else
			{
				cwl->cur.mpl_qt -= (cwl->cur.skip_die_cnt * cwl->cur.page);
			}

			if((cwl->cur.skip_die_cnt * cwl->cur.page) > cwl->cur.usr_mpl_qt)
			{
				cwl->cur.usr_mpl_qt=0;
			}
			else
			{
				cwl->cur.usr_mpl_qt -= (cwl->cur.skip_die_cnt * cwl->cur.page);
			}

            //ftl_core_trace(LOG_DEBUG, 0, "bad skip mpl_qt:%d", cwl->cur.usr_mpl_qt);
		}
        //if ((cwl->cur.mpl_qt == 0) && fcns_raid_enabled(nsid) && su->active) {
    	//	req = ncl_rid_to_req(cwl->rid[req->req.parity_die]);
    	//	cwl->rid[req->req.parity_die] = CWL_FREE_RID;
    		//xor haven't done, so wait
    	//	if (su->active->wait == false) {
    	//		QSIMPLEQ_INSERT_TAIL(&su->wait_que, su->active, link);
    	//		su->active->wait = true;
    	//	}
    	//	su->active = NULL;
    	//}
	}
#endif
    if(cpage == tpage)
    {
        if ((cwl->cur.mpl_qt == 0) && fcns_raid_enabled(nsid) && su->active 
            && (!ps->aspb[ps->curr].flags.b.parity_mix)) {
        		req = ncl_rid_to_req(cwl->rid[req->req.parity_die]);
        		cwl->rid[req->req.parity_die] = CWL_FREE_RID;
        		//xor haven't done, so wait
        		if (su->active->wait == false) {
        			QSIMPLEQ_INSERT_TAIL(&su->wait_que, su->active, link);
        			su->active->wait = true;
        		}
        		su->active = NULL;
        }
    }

    if (again_flag) {
        if (start_new_blk == false && (cpage == tpage)) {
	        //do not start a new block/wl while start_new_blk is false
	        if (cwl->cur.mpl_qt == 0) {
				if (reason) {
					*reason = cwl_finished_not_start_new;
				}
	            return NULL;
	        }
		}
        again_flag = false;
        goto again;
    }
	return wr_pl;
}

/*!
 * @brief get request to flush RAID parity
 *
 * @param nsid		namespace id
 * @param type		type, GC or NORMAL
 *
 * @return		return ncl request pointer
 */
slow_code ncl_w_req_t *ftl_core_get_parity_req(u32 nsid, u32 type)
{
	ncl_w_req_t *req;
	core_wl_t *cwl = &fcns[nsid]->ftl_core[type]->cwl;
	pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;
	cur_wr_pl_t *wr_pl = &fcns[nsid]->ftl_core[type]->cur_wr_pl;
	stripe_user_t *su = &fcns[nsid]->ftl_core[type]->su;
	aspb_t *aspb = &ps->aspb[ps->curr];

	sys_assert(su->active);
	//sys_assert(cwl->cur.die_qt == 1);
	//sys_assert(cwl->cur.usr_mpl_qt == 0);
	//sys_assert(cwl->cur.mpl_qt == cwl->cur.page);
	//sys_assert(cwl->cur.page == su->active->page_cnt);
	sys_assert(cwl->cur.next_open == aspb->parity_die);
	sys_assert(cwl->rid[cwl->cur.next_open] == CWL_FREE_RID);

	req = get_ncl_w_req(nsid, type, true);
	sys_assert(req);
	fcns[nsid]->wreq_cnt++;
	fcns[nsid]->ftl_core[type]->wreq_cnt++;
	fcns[nsid]->ftl_core[type]->parity_wreq_cnt++;

	req->req.caller = NULL;
	req->req.cnt = 0;
	req->req.nsid = nsid;
	req->req.type = type;
	req->req.tpage = cwl->cur.page; 
      //req->req.mp_du_cnt = ftl_core_next_write(nsid, type);
	req->req.die_id = cwl->cur.next_open;
    req->req.pad_sidx = 0;
	req->req.op_type.all = 0;

#ifdef SKIP_MODE
	cwl->cur.skip_die_cnt=0;
	req->req.aspb_id = ps->curr;
	cwl->cur.skip_die_cnt = pstream_get_pda(ps, req->w.pda, req->w.pda, cwl->cur.page, 1, req);

    // lucas todo clear_bit(cwl->cur.die % MAX_CHANNEL, &cwl->cur.good_die_bm[0]);
    if(req->req.op_type.b.pln_write){
        u8 bad_page_cnt = (shr_nand_info.geo.nr_planes - req->req.op_type.b.pln_write) * nand_info.bit_per_cell;
        req->req.tpage -= bad_page_cnt;
        cwl->cur.mpl_qt -= bad_page_cnt;
        cwl->cur.usr_mpl_qt -= bad_page_cnt;
    }

    // sys_assert(req->req.op_type.b.one_pln == BLOCK_NO_BAD);
    req->req.mp_du_cnt = ftl_core_next_write(nsid, type);
#else
	req->req.mp_du_cnt = ftl_core_next_write(nsid, type);
	req->req.aspb_id = pstream_get_pda(ps, req->w.pda, req->w.l2p_pda, cwl->cur.page, 1);
    sys_assert(0); 
#endif
	req->req.op_type.b.raid = 1;
	req->req.op_type.b.parity = 1;
	fcns[nsid]->parity_wreq_cnt++;

	cwl->cur.die_qt--;
	cwl->cur.open_die++;
	cwl->cur.die = cwl->cur.next_open;
	cwl->rid[cwl->cur.die] = req->req.id;
	cwl->cur.next_open++;

	req->req.cpage = cwl->cur.page / nand_plane_num(); 
    sys_assert(req->req.cpage == req->req.tpage); 

	wr_pl->cnt = 0;
	wr_pl->nsid = nsid;
	wr_pl->type = type;

	wr_pl->pl = req->w.pl;
	wr_pl->lda = req->w.lda;
#ifdef SKIP_MODE
		wr_pl->pda = req->w.pda;
#else
		if (req->w.pda[0] != INV_PDA)
			wr_pl->pda = req->w.pda;
		else
			wr_pl->pda = req->w.l2p_pda;
#endif


	wr_pl->die = cwl->cur.die;
	wr_pl->flags.b.slc = cwl->flags.b.slc;
	wr_pl->stripe_id = su->active->stripe_id;

	cwl->cur.mpl_qt -= nand_info.bit_per_cell;
	cwl->cur.die++;

	if (req->req.cpage == req->req.tpage) {
		cwl->cur.start_die++;
		cwl->cur.open_die--;
	}

	if (cwl->cur.die == nand_info.lun_num)
		cwl->cur.die = cwl->cur.start_die;

	if(cwl->cur.skip_die_cnt!=0 && cwl->cur.next_open < shr_nand_info.lun_num)
	{
		cwl->cur.next_open += cwl->cur.skip_die_cnt;
		cwl->cur.start_die += cwl->cur.skip_die_cnt;
		cwl->cur.die_qt -= cwl->cur.skip_die_cnt;

		if((cwl->cur.skip_die_cnt * cwl->cur.page) > cwl->cur.mpl_qt)
		{
			cwl->cur.mpl_qt=0;
		}
		else
		{
			cwl->cur.mpl_qt -= (cwl->cur.skip_die_cnt * cwl->cur.page);
		}

		if((cwl->cur.skip_die_cnt * cwl->cur.page) > cwl->cur.usr_mpl_qt)
		{
			cwl->cur.usr_mpl_qt=0;
		}
		else
		{
			cwl->cur.usr_mpl_qt -= (cwl->cur.skip_die_cnt * cwl->cur.page);
		}
	}

    sys_assert(cwl->cur.usr_mpl_qt == 0);
    sys_assert(cwl->cur.mpl_qt == 0);

	return req;
}
//fcns[1]->ftl_core[0]
slow_code void wr_pout_issue_done(struct ncl_cmd_t *ncl_cmd)
{
    if(ncl_cmd->status)
        ftl_core_trace(LOG_ERR, 0x4788, "ncl_cmd->flags 0x%x ncl_cmd->status 0x%x", 
            ncl_cmd->flags, ncl_cmd->status); 

#if XOR_CMPL_BY_FDONE && !XOR_CMPL_BY_CMPL_CTRL
    ncl_w_req_t *req = ncl_cmd->caller_priv;
    cwl_stripe_t *stripe = get_stripe_by_id(req->req.stripe_id);
    if(stripe->parity_req != NULL){
    	stripe_user_t *su = ftl_core_get_su(stripe->nsid, stripe->type);
        stripe->pout_done = true;

        sys_assert(!QSIMPLEQ_EMPTY(&su->wait_que));
        cwl_stripe_t *stripe_first = QSIMPLEQ_FIRST(&su->wait_que);
        if(stripe_first->stripe_id == stripe->stripe_id){
            cwl_stripe_t *next;
	        QSIMPLEQ_FOREACH_SAFE(stripe, &su->wait_que, link, next) {
                if(stripe->pout_done){
                    QSIMPLEQ_REMOVE(&su->wait_que, stripe, _cwl_stripe_t, link);
                    QSIMPLEQ_INSERT_TAIL(&su->cmpl_que, stripe, link);
                    sys_assert(stripe->parity_req != NULL);
                    ncl_w_req_t *parity_req = stripe->parity_req;
                    die_que_wr_era_ins(&parity_req->req, parity_req->req.die_id);
                    stripe->parity_req = NULL;
                }else{
                    break;
                }
            }
        }
    }else{
        ftl_core_trace(LOG_ERR, 0x6561, "PANIC parity_req == NULL");
    }
#endif
}

slow_code bool wr_pout_issue(ncl_w_req_t * req)
{
	extern bool ncl_cmd_in;
	if(ncl_cmd_in == true)
	{
		evt_set_cs(evt_pout_issue, (u32)req, 0, CS_TASK);
		return false;
	}
    pda_t *pda_list;
    bm_pl_t *bm_pl_list;
    struct info_param_t *info_list;
    struct ncl_cmd_t *ncl_cmd;
    u8 pn_cnt = req->req.cnt / req->req.tpage;
    bm_pl_t *bm_pl_list_src = req->w.pl;
    pda_t *pda_list_src = req->w.pda;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    u8 len = DU_CNT_PER_PAGE * req->req.tpage;
#else
    u8 len = DU_CNT_PER_PAGE * nand_info.bit_per_cell;
#endif
    cwl_stripe_t *stripe = get_stripe_by_id(req->req.stripe_id);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    u8 pb_type = req->req.op_type.b.slc ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;
#endif	
    ncl_cmd = &stripe->ncl_raid_cmd.ncl_cmd_pout;

    // ftl_core_trace(LOG_ERR, 0, "ncl_cmd 0x%x, req 0x%x  stripe_id %u parity_req 0x%x",
    //     ncl_cmd, req, req->req.stripe_id, get_stripe_by_id(req->req.stripe_id)->parity_req);

    bm_pl_list = ncl_cmd->user_tag_list;
    pda_list = ncl_cmd->addr_param.common_param.pda_list;
    info_list = ncl_cmd->addr_param.common_param.info_list;

    ncl_cmd->caller_priv = req;
    ncl_cmd->completion = wr_pout_issue_done;

    ncl_cmd->addr_param.common_param.list_len = len;

    ncl_cmd->status = 0;
    ncl_cmd->op_code = NCL_CMD_OP_READ;
    ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
#if NCL_FW_RETRY
    ncl_cmd->retry_step = default_read;
#endif
    ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG|NCL_CMD_RAID_POUT_FLAG_SET;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(req->req.op_type.b.slc)
        ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
    else
#endif	
        ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;

    u16 i, k = 0, j;
    for(i = 0; i < req->req.tpage;i++){ 
            u16 idx = i * pn_cnt; 
            memcpy((void *)(&bm_pl_list[k * DU_CNT_PER_PAGE]), (void *)(&bm_pl_list_src[idx * DU_CNT_PER_PAGE]), sizeof(bm_pl_t)*DU_CNT_PER_PAGE); 
            for(j = 0; j < DU_CNT_PER_PAGE; j++){
                bm_pl_list[k * DU_CNT_PER_PAGE+j].pl.type_ctrl &= ~BTN_NCL_QID_TYPE_MASK;
                bm_pl_list[k * DU_CNT_PER_PAGE+j].pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
                if(bm_pl_list[k * DU_CNT_PER_PAGE+j].pl.type_ctrl != (BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_DTAG)){
                    ftl_core_trace(LOG_ERR, 0x61a6, "panic nsid %u type_ctrl 0x%x",
                        req->req.nsid, bm_pl_list[k * DU_CNT_PER_PAGE+j].pl.type_ctrl);
                    bm_pl_list[k * DU_CNT_PER_PAGE+j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_DTAG;
                }
                pda_list[k * DU_CNT_PER_PAGE+j] = pda_list_src[idx] + j;
                info_list[k * DU_CNT_PER_PAGE+j].raid_id = req->w.raid_id[i][0].raid_id;
                info_list[k * DU_CNT_PER_PAGE+j].bank_id = req->w.raid_id[i][0].bank_id;
                info_list[k * DU_CNT_PER_PAGE+j].op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG; 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
                info_list[k * DU_CNT_PER_PAGE+j].pb_type = pb_type;
#else
				info_list[k * DU_CNT_PER_PAGE+j].pb_type = NAL_PB_TYPE_XLC;

#endif
            }

#if defined(HMETA_SIZE)
            ddr_sh_data u8 cmf_idx;
            if(cmf_idx == 4){ // workaround for raid recover PI error with 4K + 8
                void * saddr = get_raid_sram_buff_addr(req->w.raid_id[i][0].raid_id);
                if(saddr != NULL){
                    saddr += sizeof(io_meta_t) + DTAG_SZE;
                    memcpy(saddr + HMETA_SIZE, saddr, HMETA_SIZE);
                }
            }
#endif

            k ++; 
    }
    ncl_cmd_submit(ncl_cmd);
	return true;
}

slow_code void evt_wr_pout_issue(u32 param, u32 req_addr, u32 param2)
{
	ncl_w_req_t * req = (ncl_w_req_t *)req_addr;
	wr_pout_issue(req);
}

slow_code void wr_xor_only_issue_done(struct ncl_cmd_t *ncl_cmd)
{
    if(ncl_cmd->status)
        ftl_core_trace(LOG_ERR, 0x638e, "ncl_cmd_new->flags 0x%x ncl_cmd->status 0x%x", 
            ncl_cmd->flags, ncl_cmd->status); 

#if XOR_CMPL_BY_FDONE && !XOR_CMPL_BY_CMPL_CTRL
    ncl_w_req_t *req = ncl_cmd->caller_priv;
    cwl_stripe_t *stripe = get_stripe_by_id(req->req.stripe_id);
    stripe_pdone_cnt[req->req.stripe_id] --;

    if (stripe_pdone_cnt[req->req.stripe_id]  == 0) {
        sys_assert(stripe->parity_req != NULL);
        ncl_w_req_t *parity_req = stripe->parity_req;
        wr_pout_issue(parity_req);
    }
#endif
}

extern u8 set_pad_meta(ncl_w_req_t *ncl_req, u32 sidx, u32 type);
extern raid_id_mgr_t raid_id_mgr;
slow_code bool wr_xor_only_issue(ncl_w_req_t * req)
{
    pda_t *pda_list;
    bm_pl_t *bm_pl_list;
    struct info_param_t *info_list;
    struct ncl_cmd_t *ncl_cmd;
    u8 pn_cnt = req->req.cnt / req->req.tpage;
    bm_pl_t *bm_pl_list_src = req->w.pl;
    pda_t *pda_list_src = req->w.pda;
    u8 plane_num = nand_plane_num();
    u8 len = req->req.cnt - req->req.tpage;
    cwl_stripe_t *stripe = get_stripe_by_id(req->req.stripe_id);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    u8 pb_type = req->req.op_type.b.slc ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;
#endif
    ncl_cmd = &stripe->ncl_raid_cmd.ncl_cmd;

    bm_pl_list = ncl_cmd->user_tag_list;
    pda_list = ncl_cmd->addr_param.common_param.pda_list;
    info_list = ncl_cmd->addr_param.common_param.info_list;

    ncl_cmd->die_id = req->req.die_id;
    ncl_cmd->du_format_no = req->req.op_type.b.host ? host_du_fmt : DU_4K_DEFAULT_MODE;
    ncl_cmd->caller_priv = req;
    ncl_cmd->completion = wr_xor_only_issue_done;
    ncl_cmd->addr_param.common_param.list_len = len;
    ncl_cmd->status = 0;
    ncl_cmd->op_code = NCL_CMD_OP_WRITE;
#if defined(HMETA_SIZE)
	ncl_cmd->op_type = NCL_CMD_PROGRAM_DATA;
#else
	ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;
#endif
    ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG|NCL_CMD_RAID_XOR_ONLY_FLAG_SET|NCL_CMD_HOST_INTERNAL_MIX; 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(req->req.op_type.b.slc)
        ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
    else
#endif
        ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;

    /* for write req with padding, set dummy meta seed*/
    u32 WUNC_bits = 0;
    if ((req->req.op_type.b.host == 1) && (!req->req.op_type.b.erase)
            && ((req->req.padding_cnt) || req->req.wunc_cnt)) {
        req->req.pad_sidx = PDA_META_IN_GRP;
        WUNC_bits = set_pad_meta((ncl_w_req_t *)&req->req, raid_id_mgr.pad_meta_idx[req->req.stripe_id], DDR_IDX_META);
    }
    ncl_cmd->dis_hcrc = WUNC_bits | req->req.trim_page;

    u16 i, k, j, l;
    k = 0;
	for (i = 0; i < req->req.tpage; i++) {
		for (j = 1; j < pn_cnt; j++) {
            u16 idx = i * pn_cnt + j; 
			info_list[k].raid_id = req->w.raid_id[i][j % plane_num].raid_id;
			info_list[k].bank_id = req->w.raid_id[i][j % plane_num].bank_id;
#if defined(HMETA_SIZE) 
			info_list[k].op_type = NCL_CMD_PROGRAM_DATA; 
#else 
			info_list[k].op_type = NCL_CMD_PROGRAM_TABLE; 
#endif 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
            info_list[k].pb_type = pb_type;
#else
			info_list[k].pb_type = NAL_PB_TYPE_XLC;
#endif
            pda_list[k] = pda_list_src[idx];
            // pda_list[k] = set_die2pda22(pda_list_src[idx],0);
            memcpy((void *)(&bm_pl_list[k * DU_CNT_PER_PAGE]), (void *)(&bm_pl_list_src[idx * DU_CNT_PER_PAGE]), sizeof(bm_pl_t)*DU_CNT_PER_PAGE); 
            for(l = 0;l<DU_CNT_PER_PAGE;l++){
                bm_pl_list[k*DU_CNT_PER_PAGE+l].pl.type_ctrl &= ~BTN_NCL_QID_TYPE_MASK;
                bm_pl_list[k*DU_CNT_PER_PAGE+l].pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
            }
			k++;
		}
	}


    ncl_cmd_submit(ncl_cmd);
	return true;
}

fast_code ncl_w_req_t *ftl_core_submit(cur_wr_pl_t *wr_pl, void *caller, ncl_req_cmpl_t cmpl)
{
	u32 nsid = wr_pl->nsid;
	u32 type = wr_pl->type;
	core_wl_t *cwl = &fcns[nsid]->ftl_core[type]->cwl;
	pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;
	ncl_w_req_t *req = ncl_rid_to_req(cwl->rid[wr_pl->die]);
	ftl_flush_misc_t * ctx = (ftl_flush_misc_t *)caller;
	//spb_id_t  spb = ps->aspb[ps->curr].spb_id;
	req->req.op_type.b.slc = wr_pl->flags.b.slc;
	if(wr_pl->flags.b.flush_blklist)
	{
#ifdef SKIP_MODE
		u32 plane = (wr_pl->flags.b.pln_write > 0) ? wr_pl->flags.b.pln_write : shr_nand_info.geo.nr_planes;
		if (req->req.cpage % plane == 0){
			req->req.blklist_pg[(req->req.cpage / plane) - 1] = 1;
			//ftl_core_trace(LOG_INFO, 0, "set blklist_pg:%d cpage:%d",(req->req.cpage / plane) - 1,req->req.cpage);
		}
#else
		if(wr_pl->flags.b.pln_st == 0){  // adams??
			if(req->req.cpage%2 == 0){
				req->req.blklist_pg[(req->req.cpage>>1) - 1] = 1;
				//ftl_core_trace(LOG_INFO, 0, "set blklist_pg:%d",(req->req.cpage>>1) - 1);
			}
		}else{
			req->req.blklist_pg[req->req.cpage - 1] = 1;
			//ftl_core_trace(LOG_INFO, 0, "set blklist_pg:%d",req->req.cpage - 1);
		}
#endif

		//req->req.blklist_dtag_cnt += ctx->blklist_dtag_cnt;
		//req->req.blklist_dtag_cnt += ftl_core_next_write(nsid, type);
		req->req.op_type.b.blklist = 1;
		wr_pl->flags.b.dummy = 0;
	}

    if (wr_pl->flags.b.dummy) {
        req->req.op_type.b.dummy = 1;
    }

	if (nsid != INT_NS_ID) {
		req->req.op_type.b.host = 1;

	// modified by sunny, for meta data
#ifdef LJ_Meta
        req->req.padding_cnt += ftl_core_setup_meta(req, wr_pl, INV_U8);
#else
		req->req.padding_cnt += ftl_core_setup_meta(req, wr_pl->cnt, fcns[nsid]->ftl_core[type]->sn);
#endif
	// modified by sunny, for meta data
#ifndef LJ_Meta
        req->w.meta_sn = fcns[nsid]->ftl_core[type]->sn;
#endif
		fcns[nsid]->ftl_core[type]->sn++;
        if (wr_pl->flags.b.dummy) {
            //req->req.op_type.b.host = 0;
        }
	} else {

		req->req.op_type.b.host = 0;
        req->req.op_type.b.ftl = 1; // To notify this is a ftl block.


        #if 0  // for dbg, by Sunny
        u8 i;
        ftl_core_trace(LOG_ALW, 0x32a9, "FTL block fill meta");
        for (i = 0; i < wr_pl->cnt; i++)
        {
            ftl_core_trace(LOG_ALW, 0xc6f1, "wr_pl->lda[%d] = 0x%x", i, wr_pl->lda[i]);
        }
        #endif

        if(wr_pl->flags.b.ftl == 1){
			//ftl_flush_tbl_t * ctx = (ftl_flush_tbl_t *)caller;
			//ftl_core_trace(LOG_INFO, 0, "l2p seg:%d pad_meta",ctx->seg_cnt);
            #ifdef LJ_Meta
            if(wr_pl->lda[0] == QBT_LDA)
            {
                //ftl_core_trace(LOG_ALW, 0, "QBT setup meta");
                req->req.padding_cnt += ftl_core_setup_meta(req, wr_pl, shr_qbt_loop);
            }
            else if(wr_pl->lda[0] == PBT_LDA)
            {
                //ftl_core_trace(LOG_ALW, 0, "PBT setup meta");
                req->req.padding_cnt += ftl_core_setup_meta(req, wr_pl, (tFtlPbt.pbt_cur_loop-1));
            }
            else
            {
                ftl_core_trace(LOG_WARNING, 0xe9d4, "1 warning: meta is not set!!!!!!!");
            }

            #else
			req->req.padding_cnt += ftl_core_setup_meta(req, wr_pl->cnt, fcns[nsid]->ftl_core[type]->sn);
            req->w.meta_sn = fcns[nsid]->ftl_core[type]->sn;
			fcns[nsid]->ftl_core[type]->sn++;
            #endif
        }else if(wr_pl->lda[0] == PARITY_LDA){
            req->req.padding_cnt += ftl_core_setup_meta(req, wr_pl, 0);
        }    
#if 0//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
		else if(req->req.type == FTL_CORE_SLC)
        {
			req->req.padding_cnt += ftl_core_setup_meta(req, wr_pl, INV_U8);
        }
#endif		      
        else
        {
            ftl_core_trace(LOG_WARNING, 0x1dc6, "2 warning: meta is not set!!!!!!!");
        }
	}

#if 0
	if (wr_pl->flags.b.trim_info) {
		req->req.op_type.b.trim_info = true;
		req->req.trim_page = req->req.cpage - 1;  //TODO
		fcns[1]->ftl_core[FTL_CORE_NRM]->ps.aspb[req->req.aspb_id].flags.b.rinfo = true;
	} else {
        //req->req.trim_page = INV_U8;
    }
#endif
#if 1//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(ps->flags.b.p2l_enable)
#else
    if (fcns_p2l_enabled(nsid)) //&&(plp_spb_flush_p2l != spb) 
#endif
    {
		pagesn_update(ps, req);
	}

	req->req.cnt += wr_pl->cnt;
	wr_pl->cnt = 0;
    if(req->req.cpage == 1){
    	req->req.caller = caller; 
    	req->req.cmpl = (cmpl == NULL) ? ftl_core_done : cmpl;
    }else{
        if(caller != NULL){
    	    req->req.caller = caller;
        }
        if(cmpl == NULL){
            if(req->req.cmpl == NULL){
    	        req->req.cmpl = ftl_core_done;
            }
        }else{
    	    req->req.cmpl = cmpl;
        }
    }

	if (req->req.cpage != req->req.tpage)
		return NULL;

	cwl->cur.open_die--;
#ifdef SKIP_MODE
#if (H_MODE_WRITE == mENABLE) 
    clear_bit(wr_pl->die% MAX_CHANNEL, &cwl->cur.good_die_bm[0]);
    clear_bit(wr_pl->die% MAX_CHANNEL, &cwl->cur.good_die_bm[1]);

    if(cwl->cur.open_die == 0)
    {
        u32 start_ce = cwl->cur.start_die % MAX_CHANNEL;  //willis
        cwl->cur.total_skip_cnt_in_ce = 0;
        cwl->cur.handle_ce_die = 0;
        //if(cwl->cur.good_die_bm[0] != 0)
        //{
        //    sys_assert(0);
        //}
        cwl->cur.good_die_bm[0] = 0XFF;
        cwl->cur.good_die_bm[1] = 0XFF;
        if(start_ce)
        {
            u32 i;
            cwl->cur.total_skip_cnt_in_ce = (u8)start_ce;
            for(i = 0; i < start_ce; i++)
            {
                clear_bit(i, &cwl->cur.good_die_bm[0]);
                clear_bit(i, &cwl->cur.good_die_bm[1]);
            }
        }
    }
#endif
#endif
    //if (req->req.tpage == nand_plane_num() * nand_info.bit_per_cell) { 
    	//ftl_core_trace(LOG_ALW, 0, "reqcnt %d open die %d", req->req.cnt, cwl->cur.open_die); 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(wr_pl->flags.b.slc == 0) 
        	req->req.tpage = nand_info.bit_per_cell;  // 3 
    	else 
    		req->req.tpage = 1;  // 1 
#else
        req->req.tpage = nand_info.bit_per_cell;  // 3 
#endif

        req->req.mp_du_cnt *= nand_plane_num();  // 16 
    //} 

	u32 page_cnt = req->req.cnt >> DU_CNT_SHIFT;  // 48 >> 2 

	if (ps->type != FTL_CORE_GC) 
		pstream_rsvd(ps); 

	req->req.cnt = page_cnt; 
    if (req->req.op_type.b.dummy) { 
        if (req->req.op_type.b.p2l_nrm)  { 
            req->req.op_type.b.dummy = 0; 
        } 
    } 
	//only insert the req which need to update l2p table 
	if ((req->req.op_type.b.host == 1) && (req->req.op_type.b.pout == 0)) { 
        u32 padding_cnt = req->req.padding_cnt; 
		if(wr_pl->flags.b.flush_blklist){ 
			if(ctx->blklist_dtag_cnt){ 
				padding_cnt += ctx->blklist_dtag_cnt; 
				req->req.blklist_dtag_cnt = ctx->blklist_dtag_cnt; 
				//ftl_core_trace(LOG_INFO, 0, "ctx:%d, padding_cnt%d",req->req.blklist_dtag_cnt,padding_cnt); 
	    	} 
		} 
        if (req->req.op_type.b.p2l_nrm) { 
          padding_cnt += req->req.p2l_dtag_cnt; 
        } 

		if (req->req.parity_die == req->req.die_id){ 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(wr_pl->flags.b.slc == 0)
            	padding_cnt += nand_info.bit_per_cell * DU_CNT_PER_PAGE; 
            else
            	padding_cnt += DU_CNT_PER_PAGE;
#else
			padding_cnt += nand_info.bit_per_cell * DU_CNT_PER_PAGE; 
#endif

		}

		if (padding_cnt < (req->req.cnt << DU_CNT_SHIFT)) { 
			bool ret; 
			CBF_INS(&l2p_updt_que, ret, req->req.id); 
			sys_assert(ret == true); 
        }  
	} 
#if 1//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(ps->flags.b.p2l_enable)
#else    
	if (fcns_p2l_enabled(nsid))//&&(plp_spb_flush_p2l != spb) 
#endif
	{

		p2l_update(ps, req);
	}    
    #if PLP_DEBUG
    if(plp_trigger){
        if(ucache_flush_flag){
            die = req->req.die_id;
            ftl_core_trace(LOG_PLP, 0x16b6, "ucache flush, req 0x%x, req.die %d wlpl.die %d",req, \
            req->req.die_id,wr_pl->die);
        }
    }
    #endif
	if (req->req.op_type.b.raid) {
#if RAID_SUPPORT
        u32 lda_cnt = page_cnt << DU_CNT_SHIFT;
        cwl_stripe_t *stripe = get_stripe_by_id(wr_pl->stripe_id);
        bool parity_mix = req->req.parity_die == req->req.die_id && stripe->parity_mix;
        set_ncl_req_raid_info(req, get_stripe_by_id(wr_pl->stripe_id), lda_cnt, parity_mix);
        raid_sched_push(req);
#else
        sys_assert(0);
#endif
	} else {
		die_que_wr_era_ins(&req->req, req->req.die_id);
	}

	if(raid_cnt % raidR == 0){ 
		bool ret; 
#if (PBT_DEBUG == mENABLE) 
        pstream_t *ps = &fcns[2]->ftl_core[2]->ps; 
        aspb_t *aspb = &ps->aspb[ps->curr]; 
        ftl_core_trace(LOG_INFO, 0x26c3, "raid_cnt:%u, cur_seg_ofst:%u, write_page_cnt:%u, pbt_flush_page_cnt:%u, wptr page:%u, off:%u",  
            raid_cnt, tFtlPbt.cur_seg_ofst, tFtlPbt.total_write_page_cnt, tFtlPbt.pbt_flush_page_cnt, aspb->wptr / nand_info.interleave, aspb->wptr % nand_info.interleave); 
#endif 
		CBF_INS(&pbt_updt_ntf_que, ret, 1); 
		if(ret == false){ 
			//sys_assert(ret == true); 
		} 
        raid_cnt = 0;				  
	} 
	raid_cnt++; 

	cwl->rid[req->req.die_id] = CWL_FREE_RID;
	cwl->cur.issue_cnt += 1;

	return req;
}

fast_code u16 ftl_core_close_open_die(u32 nsid, u32 type, ncl_req_cmpl_t cmpl)
{
	u16 busy = 0;
	//u32 wpl_mode;
	core_wl_t *cwl = &fcns[nsid]->ftl_core[type]->cwl;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    if (type == FTL_CORE_GC && nsid == HOST_NS_ID) 
#else
    if (type == FTL_CORE_GC) 
#endif
	{
        sys_assert(cmpl == ftl_core_gc_wr_done);
    }

	if (cwl->cur.open_die && (!plp_trigger || cmpl == ftl_core_gc_wr_done)) {
        ftl_core_trace(LOG_INFO, 0xfac5, "close open nsid:%d type:%d mp_qt:%x", nsid, type, cwl->cur.mpl_qt);
    }

//	if(nsid == INT_NS_ID){
//        wpl_mode = WPL_V_MODE;
//    }else{
//        wpl_mode = WPL_V_MODE;
//		if(type == FTL_CORE_GC)
//		{
//			wpl_mode = WPL_V_MODE;
//		}
//    }

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (cwl->cur.open_die) {
		cur_wr_pl_t *cur_wr_pl = ftl_core_get_wpl_mix_mode(nsid, type, WPL_V_MODE, false, NULL, NULL);

		if (cur_wr_pl == NULL) {
			sys_assert(cwl->cur.open_die == 0);
			busy = busy + 1;
			break;
		}

		u32 padding_cnt;
		padding_cnt = cur_wr_pl->max_cnt - cur_wr_pl->cnt;
		busy += padding_cnt;
		//ftl_core_trace(LOG_DEBUG, 0, "die Q[%d] pad %d", cur_wr_pl->die, padding_cnt);
		while (padding_cnt) {
            if(nsid == INT_NS_ID && type == FTL_CORE_NRM){
				cur_wr_pl->lda[cur_wr_pl->cnt] = QBT_LDA;
				cur_wr_pl->flags.b.ftl = 1;
			}else if(nsid == INT_NS_ID && type == FTL_CORE_PBT){
				cur_wr_pl->lda[cur_wr_pl->cnt] = PBT_LDA;
				cur_wr_pl->flags.b.ftl = 1;
			}else{
				cur_wr_pl->lda[cur_wr_pl->cnt] = INV_LDA;
			}
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = WVTAG_ID;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			cur_wr_pl->cnt++;
			padding_cnt--;
		}

		ftl_core_submit(cur_wr_pl, NULL, cmpl);

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

	return busy;
}

fast_code void fcore_idle_flush_done(ftl_core_ctx_t *ctx)
{
	ftl_core_trace(LOG_INFO, 0x17cc, "fctx:0x%x done", ctx);
	sys_free(FAST_DATA, ctx);
}

fast_code void fcore_idle_flush(void)
{
	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));
	sys_assert(fctx);
	fctx->ctx.caller = NULL;
	fctx->ctx.cmpl = fcore_idle_flush_done;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->flags.b.dtag_gc = 1;
	if (ftl_core_flush(fctx) == FTL_ERR_OK) {
		avoid_fcore_flush_hang++;
		ftl_core_trace(LOG_INFO, 0xec32, "fctx:0x%x done avoid_fcore_flush_hang:%d", fctx,avoid_fcore_flush_hang);
		sys_free(FAST_DATA, fctx);
		if(avoid_fcore_flush_hang >= 2){
			avoid_fcore_flush_hang = 0;
			ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_NRM, NULL);
		}

	}
}

//fast_code void ftl_core_gc_data_in_update(void)
fast_code inline void ftl_core_gc_data_in_update(void)  //willis2
{
    fcns[1]->ftl_core[FTL_CORE_GC]->data_in++;
}

fast_code bool ftl_core_idle_open_blk_handle(idle_chk_t *chk, u8 core_type, aspb_t *aspb);
static fast_code void fcore_gc_idle_check(void* data)
{
    ftl_core_t *fcore_nrm = fcns[1]->ftl_core[FTL_CORE_GC];
    idle_chk_t *chk = &open_ctrl.chk[FTL_CORE_GC];

    if ((fcore_nrm->last_data_in == 0) ||
        (fcore_nrm->last_data_in != fcore_nrm->data_in)) {
        fcore_nrm->last_data_in = fcore_nrm->data_in;
        /* not idle, reset all timer */
        mod_timer(&fcns[1]->ftl_core[FTL_CORE_GC]->idle_timer, jiffies + HZ);  // 1 adams HZ
        chk->wl_timer = chk->layer_timer = chk->blk_timer = 0;
        chk->wl_filled = chk->layer_filled = chk->blk_filled = chk->blk_filling = false;
        chk->spb_id = INV_SPB_ID;
        return;
    }

#if SYNOLOGY_SETTINGS
	gc_wcnt_sw_flag = false;
#endif
	/* 1s flush gc data */
    if ((fcore_nrm->cur_wr_pl.cnt || fcore_nrm->cwl.cur.open_die) && (fill_dummy_check() == false)) {
        ftl_core_trace(LOG_INFO, 0x091c, "ftl_core_idle_close gc. wr_pl_cnt:%u spb:%u wptr:%u", 
            fcore_nrm->cur_wr_pl.cnt, fcore_nrm->ps.aspb[fcore_nrm->ps.curr].spb_id, fcore_nrm->ps.aspb[fcore_nrm->ps.curr].wptr);
        ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_GC, ftl_core_gc_wr_done);
    }

    ftl_core_idle_open_blk_handle(&open_ctrl.chk[FTL_CORE_GC], FTL_CORE_GC, &fcore_nrm->ps.aspb[fcore_nrm->ps.curr]);
    mod_timer(&fcns[1]->ftl_core[FTL_CORE_GC]->idle_timer, jiffies + HZ);  // 1 adams HZ
}


static fast_code void fcore_idle_check(void *data)
{
	ftl_core_t *fcore_nrm = fcns[1]->ftl_core[FTL_CORE_NRM];
	idle_chk_t *chk = &open_ctrl.chk[FTL_CORE_NRM];

	if ((fcore_nrm->last_data_in == 0) ||
		(fcore_nrm->last_data_in != fcore_nrm->data_in)) {
		fcore_nrm->last_data_in = fcore_nrm->data_in;
		/* not idle, reset all timer */
#if(PLP_SUPPORT == 1)
		mod_timer(&fcns[1]->ftl_core[0]->idle_timer, jiffies + HZ);
#else
		mod_timer(&fcns[1]->ftl_core[0]->idle_timer, jiffies + 3*HZ/10);
#endif
		chk->wl_timer = chk->layer_timer = chk->blk_timer = 0;
		chk->wl_filled = chk->layer_filled = chk->blk_filled = chk->blk_filling = false;
		chk->spb_id = INV_SPB_ID;

        #if (RD_NO_OPEN_BLK == mENABLE)
        host_idle = false;
        #endif
		return;
	}

	/* 1s flush host data */
	if ((fcore_nrm->cur_wr_pl.cnt || fcore_nrm->cwl.cur.open_die) && (fill_dummy_check() == false))
    {
        ftl_core_trace(LOG_INFO, 0x1bed, "open die:%u, die %u, wreq_cnt:%u, wptr:%u, shr_dtag_ins_cnt:%u, dtag_recv:%u",
	        fcore_nrm->cwl.cur.open_die, fcore_nrm->cwl.cur.die - 1, fcns[1]->ftl_core[FTL_CORE_NRM]->wreq_cnt, fcore_nrm->ps.aspb[fcore_nrm->ps.curr].wptr, shr_dtag_ins_cnt, dtag_recv);
		fcore_idle_flush();
    }

    #if (RD_NO_OPEN_BLK == mENABLE)
    host_idle = true;
    #endif

	ftl_core_idle_open_blk_handle(&open_ctrl.chk[FTL_CORE_NRM], FTL_CORE_NRM, &fcore_nrm->ps.aspb[fcore_nrm->ps.curr]);
#if(PLP_SUPPORT == 1)
	mod_timer(&fcns[1]->ftl_core[0]->idle_timer, jiffies + HZ);
#else
	mod_timer(&fcns[1]->ftl_core[0]->idle_timer, jiffies + 3*HZ/10);
#endif
}

fast_code ftl_err_t ftl_core_flush(ftl_flush_data_t *fctx)
{
	u32 padding_cnt;
	u32 nsid = fctx->nsid;
	core_wl_t *cwl = &fcns[nsid]->ftl_core[FTL_CORE_NRM]->cwl;
	cur_wr_pl_t *cur_wr_pl;
	u16 *target;
	extern void be_dtag_comt(void);

	be_dtag_comt();

    if (fctx->flags.b.finished){
        ftl_core_trace(LOG_INFO, 0x0231, "fctx finished:0x%x, flag:0x%x", fctx, fctx->flags.all);
        return FTL_ERR_OK;
    }else if (fctx->flags.b.dtag_gc) {
		/* dtag_gc doesn't interrupt flush command */
		if (fcns[nsid]->flushing_ctx) {
			return FTL_ERR_OK;
    	}
	} else if ((shr_dtag_ins_cnt != dtag_recv) || (fcns[nsid]->flushing_ctx) || (shutdown_flush_ctx && fctx->flags.b.shutdown)) {
	    if (shutdown_flush_ctx && fctx->flags.b.shutdown){
           	ftl_core_trace(LOG_INFO, 0xd5a4, "fctx pending flag:0x%x, flushing flag:0x%x", fctx->flags.all, shutdown_flush_ctx->flags.all);
            fctx->flags.b.finished = 1;
        }
		/* dtag committing or another flushing */
		QSIMPLEQ_INSERT_TAIL(&fcns[nsid]->pending, &fctx->ctx, link);
		u32 wreq_cnt = fcns[nsid]->ftl_core[FTL_CORE_NRM]->wreq_cnt - fcns[nsid]->ftl_core[FTL_CORE_NRM]->parity_wreq_cnt;
		ftl_core_trace(LOG_ALW, 0xcf64, "fctx pending:0x%x, ins:%d, recv:%d, flushing:0x%x, shutdown_flush_ctx:0x%x h_wreq_cnt:%d",
			fctx, shr_dtag_ins_cnt, dtag_recv, fcns[nsid]->flushing_ctx, shutdown_flush_ctx,wreq_cnt);
		ftl_core_trace(LOG_ALW, 0xa731, "open %d, host pl %d gc pl %d, next %u die %u partiry_cnt:%d",
	        cwl->cur.open_die, fcns[nsid]->ftl_core[FTL_CORE_NRM]->cur_wr_pl.cnt,fcns[nsid]->ftl_core[FTL_CORE_GC]->cur_wr_pl.cnt,
	        cwl->cur.next_open, cwl->cur.die - 1,fcns[nsid]->ftl_core[FTL_CORE_NRM]->parity_wreq_cnt);	
		return FTL_ERR_PENDING;
	}

	target = &cwl->cur.open_die;
	if(plp_trigger){
		extern volatile u8 CPU2_plp_step;
		CPU2_plp_step = 4;
		ftl_core_trace(LOG_INFO, 0x7b76, "open %d, host pl %d gc pl %d, fctx 0x%x next %u die %u",
	        cwl->cur.open_die, fcns[nsid]->ftl_core[FTL_CORE_NRM]->cur_wr_pl.cnt,fcns[nsid]->ftl_core[FTL_CORE_GC]->cur_wr_pl.cnt, fctx,
	        cwl->cur.next_open, cwl->cur.die - 1);
	}
	else{
		ftl_core_trace(LOG_INFO, 0xa408, "open die:%d, host cur_wr_pl:%d, gc cur_wr_pl:%d, fctx:0x%x next_open %u die %u",
	        cwl->cur.open_die, fcns[nsid]->ftl_core[FTL_CORE_NRM]->cur_wr_pl.cnt,fcns[nsid]->ftl_core[FTL_CORE_GC]->cur_wr_pl.cnt, fctx,
	        cwl->cur.next_open, cwl->cur.die - 1);
        ftl_core_trace(LOG_INFO, 0x78ce, "shr_dtag_ins_cnt:%u dtag_recv:%u", shr_dtag_ins_cnt, dtag_recv);
	}

	//ftl_core_trace(LOG_INFO, 0, "mp quota %d, die quota %d", cwl->cur.mpl_qt, cwl->cur.die_qt);
	//ftl_core_trace(LOG_INFO, 0, "open die %d, start die %d", cwl->cur.open_die, cwl->cur.start_die);
    if(plp_trigger && (fctx_addr == fctx))
    {
        ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_GC, ftl_core_gc_wr_done);
    }

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    if(write_type == FTL_CORE_SLC)
    {
		ftl_core_close_open_die(write_nsid, write_type, NULL);//close die for update vac
    }
#endif	

	if ((*target == 0) && (fcns[nsid]->ftl_core[FTL_CORE_NRM]->cur_wr_pl.cnt == 0)) {
		if (fctx->flags.b.shutdown) {
			// issue ftl flush
			sys_assert(fcns[nsid]->flushing_ctx == NULL);
			fcns[nsid]->flushing_ctx = fctx;
			bool ret = ftl_core_flush_done(nsid, NULL);
			//sys_assert(ret == false);
			if (ret == true) {
				sys_assert(plp_trigger);
				return FTL_ERR_OK;
			} else {
				return FTL_ERR_BUSY;
			}
		}
		#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
        if(fcns[nsid]->ftl_core[FTL_CORE_NRM]->wreq_cnt != 0 || fcns[nsid]->ftl_core[FTL_CORE_SLC]->wreq_cnt != 0)
		#else
        if(fcns[nsid]->ftl_core[FTL_CORE_NRM]->wreq_cnt != 0)
		#endif
        {
            sys_assert(fcns[nsid]->flushing_ctx == NULL);
	        fcns[nsid]->flushing_ctx = fctx;
            return FTL_ERR_BUSY;
        }
		return FTL_ERR_OK;
	}
    ftl_core_trace(LOG_PLP, 0x0219, "fctx:0x%x start pad", fctx);
again:
	cur_wr_pl = ftl_core_get_wpl_mix_mode(nsid, FTL_CORE_NRM, WPL_V_MODE, false, fctx, NULL);

	if (cur_wr_pl) {
		padding_cnt = cur_wr_pl->max_cnt - cur_wr_pl->cnt;
		//ftl_core_trace(LOG_INFO, 0, "die Q[%d] pad %d", cur_wr_pl->die, padding_cnt);
		while (padding_cnt) {
			cur_wr_pl->lda[cur_wr_pl->cnt] = INV_LDA;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = WVTAG_ID;
			cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			cur_wr_pl->cnt++;
			padding_cnt--;
		}
		ftl_core_submit(cur_wr_pl, fctx, NULL);
	} else {
		sys_assert(*target == 0);
	}

	/// padding all
	if (*target != 0) {
		goto again;
	}

	sys_assert(fcns[nsid]->flushing_ctx == NULL);
	fcns[nsid]->flushing_ctx = fctx;

	return FTL_ERR_BUSY;
}

//fast_code bool ftl_core_spb_close_fill_dummy(ftl_spb_pad_t* spb_pad);
fast_code void fcore_fill_dummy_done(ftl_core_ctx_t *ctx)
{
    fill_dummy_flag = 0;
}

fast_code void ipc_fill_dummy_done(ftl_core_ctx_t *ctx)
{
	// find hdl and call cpu3
	ftl_core_ctx_t *hdl = (ftl_core_ctx_t *)(ctx->caller);

	ftl_core_trace(LOG_ERR, 0x0650, "ipc fill done:0x%x\n",(u32)hdl);
	/* call _ipc_ftl_core_ctx_done */
	hdl->cmpl((ftl_core_ctx_t *)hdl);
	//sys_assert(0);
}

fast_code bool is_spb_pad_allocated(ftl_spb_pad_t *spb_pad)
{
	sys_assert(spb_pad->spb_pad_idx < MAX_SPBPAD_CNT);
	if ((1 << spb_pad->spb_pad_idx) & open_ctrl.using_bmp) {
		return true;
	}
	return false;
}

fast_code ftl_spb_pad_t *get_free_spb_pad()
{
	u32 spb_pad_idx;
	ftl_spb_pad_t *free_spb_pad = NULL;
	spb_pad_idx = find_first_zero_bit(&open_ctrl.using_bmp, MAX_SPBPAD_CNT);
	if (spb_pad_idx == MAX_SPBPAD_CNT) {
		panic("no free spb_pad");
	}
	set_bit(spb_pad_idx, &open_ctrl.using_bmp);
	free_spb_pad = &open_ctrl.spb_pad[spb_pad_idx];
	free_spb_pad->pad_done = false;
	sys_assert(free_spb_pad->spb_pad_idx == spb_pad_idx);
    sys_assert(open_ctrl.free_cnt != 0);
    open_ctrl.free_cnt--;
	return free_spb_pad;
}

fast_code void put_free_spb_pad(ftl_spb_pad_t *free_pad)
{
    sys_assert(free_pad->spb_pad_idx < MAX_SPBPAD_CNT);
    //free_pad->padding = false;
//    free_pad->pad_done = true;
    clear_bit(free_pad->spb_pad_idx, &open_ctrl.using_bmp);
    sys_assert(open_ctrl.free_cnt < MAX_SPBPAD_CNT);
    open_ctrl.free_cnt++;
	return;
}

fast_code bool fill_dummy_check(void)
{
    return open_ctrl.free_cnt == MAX_SPBPAD_CNT ? false : true;
}

fast_code void launch_fill_dummy(ftl_spb_pad_t *spb_pad);
fast_code void fcore_fill_dummy(u8 start_type, u32 end_type,u8 fill_type, void* cmpl, void* caller)
{
	ftl_spb_pad_t *spb_pad = get_free_spb_pad();
	sys_assert(is_spb_pad_allocated(spb_pad));
	switch (fill_type) {
	case FILL_TYPE_WL:
		spb_pad->param.cwl_cnt = 2;
		spb_pad->param.pad_all = false;
		break;
	case FILL_TYPE_LAYER:
		spb_pad->param.cwl_cnt = 4;
		spb_pad->param.pad_all = false;
		break;
	case FILL_TYPE_BLK:
		spb_pad->param.cwl_cnt = 0;
		spb_pad->param.pad_all = true;
		break;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	case FILL_TYPE_CLOSE_WL:
		spb_pad->param.cwl_cnt = 1;
		spb_pad->param.pad_all = false;
		break;
#endif

	default:
		panic("");
	}
	spb_pad->spb_id = 0xFFFF;
	spb_pad->pad_attribution = pad_normal;

	// please set attribute to "pad_plp" only when plp ask to close open wl,
	// to make sure plp fill dummy own the highest priority, do not use plp_trigger
	if (plp_ask_close_open_wl) 
	{
		spb_pad->pad_attribution = pad_plp;
		#if MDOT2_SUPPORT == 1
		extern bool save_evlog_start;
		if(save_evlog_start)
		{
			//avoid incomplete ,just fill one wl
			spb_pad->param.cwl_cnt = 1;
		}
		if(power_on_update_epm_flag != POWER_ON_EPM_UPDATE_DONE)
		{
			//init done + plp case ,plp cap may not enough , avoid incomplete wl
			spb_pad->param.cwl_cnt = 1;
		}
		#endif
	}
	spb_pad->param.start_type = start_type;
	spb_pad->param.end_type = end_type;
	spb_pad->cur.type = start_type;

	spb_pad->param.start_nsid = 1;
	spb_pad->param.end_nsid = 1;
	spb_pad->cur.nsid = 1;

	spb_pad->cur.cwl_cnt = 0;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if (start_type == FTL_CORE_PBT && fill_type != FILL_TYPE_CLOSE_WL) 
#else
	if (start_type == FTL_CORE_PBT) 
#endif
	{
		spb_pad->param.start_nsid = 2;
		spb_pad->param.end_nsid = 2;
		spb_pad->cur.nsid = 2;
	}

	//spb_pad->start_nsid = spb_pad->cur.nsid; // ISU, EH_GC_not_done (3)
	//spb_pad->start_type = spb_pad->cur.type;

	spb_pad->dummy_req_cnt = 0;
	spb_pad->dummy_done_cnt = 0;

    spb_pad->ctx.caller = caller;    // ERRHANDLE_GLIST
	spb_pad->ctx.cmpl = cmpl == NULL? fcore_fill_dummy_done : cmpl;
	if(!plp_trigger)
    	ftl_core_trace(LOG_INFO, 0xb192, "fill dummy reg,spb_pad:0x%x, start:%u end:%d type:%u caller:0x%x",spb_pad, start_type, end_type, fill_type, caller);
	launch_fill_dummy(spb_pad);

}

slow_code bool ftl_core_idle_open_blk_handle(idle_chk_t *chk, u8 core_type, aspb_t *aspb)
{
    //u32 mul = 1;
    //if (core_type == FTL_CORE_GC) {  //idle check, host every 1s check/gc every 100ms check, GC change to every 1s check
    //    mul = HZ;
    //}
#if(PLP_SUPPORT == 1)
	u16 close_wl_threshold = CLOSE_WL_EXPIRE;  // CLOSE_WL_EXPIRE * mul
#else
	u16 close_wl_threshold = (core_type == FTL_CORE_NRM) ? CLOSE_WL_EXPIRE_NRM : CLOSE_WL_EXPIRE;  //non_plp, host check every 300ms
#endif
    if(chk->blk_filling && !chk->blk_filled){  // fill dummy time max < 192 / 60 min
#if(PLP_SUPPORT == 0)
    	if(!((core_type == FTL_CORE_NRM) && (++chk->wl_timer) < 4))
		{
#endif
	        if(chk->spb_id == aspb->spb_id && aspb->spb_id != INV_SPB_ID){
				fcore_fill_dummy(core_type, core_type,FILL_TYPE_WL, NULL, NULL);
	        }else{
	            chk->blk_filled = true;
	            chk->spb_id = INV_SPB_ID;
	        }
        	ftl_core_trace(LOG_INFO, 0xf469, "idle 12h close type:%u spb %u wptr %u max_ptr %u blk_filled %u", core_type, aspb->spb_id, aspb->wptr, aspb->max_ptr, chk->blk_filled);
#if(PLP_SUPPORT == 0)
			chk->wl_timer = 0;
    	}
#endif
    } else if ((++chk->wl_timer) >= close_wl_threshold) {
        if ((++chk->layer_timer) >= CLOSE_LAYER_EXPIRE) {
            u8 host_close_exp = 0;
            if(core_type == FTL_CORE_NRM)
                host_close_exp = 2;
			if ((++chk->blk_timer) >= CLOSE_BLK_EXPIRE - host_close_exp) {
				/* close open block */
				if (!chk->blk_filled) {
                    ftl_core_trace(LOG_INFO, 0x2b4d, "idle 12h close type:%u spb %u wptr %u max_ptr %u blk_filled %u", core_type, aspb->spb_id, aspb->wptr, aspb->max_ptr, chk->blk_filled);
                    chk->blk_filling = true;
                    chk->spb_id = aspb->spb_id;
                    //if(core_type == FTL_CORE_NRM)
                        //rd_gc_flag = true;
					fcore_fill_dummy(core_type, core_type,FILL_TYPE_WL, NULL, NULL);
				}
				chk->blk_timer = 0;
			} else {
				/* close open layer */
				if (!chk->layer_filled) {
					ftl_core_trace(LOG_INFO, 0x8778, "idle 5 min close layer.type:%u spb:%u, wptr:%u", core_type, aspb->spb_id, aspb->wptr);
					chk->layer_filled = true;
					fcore_fill_dummy(core_type, core_type,FILL_TYPE_LAYER, NULL, NULL);
				}
			}
			chk->layer_timer = 0;
		} else {
			if (!chk->wl_filled) {
				/* close open wl */
				ftl_core_trace(LOG_INFO, 0xf97d, "idle 1 min close wl.type:%u spb:%u, wptr:%u", core_type, aspb->spb_id, aspb->wptr);
				chk->wl_filled = true;
				fcore_fill_dummy(core_type, core_type,FILL_TYPE_WL, NULL, NULL);
			}
		}
		chk->wl_timer = 0;
	}
	return true;
}

fast_code bool is_spb_fill_dummy_done(ftl_spb_pad_t* spb_pad)
{
    if ((spb_pad->cur.nsid == spb_pad->param.end_nsid) && (spb_pad->cur.type > spb_pad->param.end_type)){
		if (plp_trigger && (spb_pad->dummy_req_cnt - spb_pad->dummy_done_cnt < 2)) {
//        	// ftl_core_trace(LOG_ERR, 0, "fill req done, wait ncl done:%d-%d-%d-%d\n", spb_pad->cur.nsid, spb_pad->param.nsid, spb_pad->cur.type, spb_pad->param.type);
			//ftl_core_trace(LOG_ALW, 0, "fill req done, wait ncl done, req cnt:%d, done cnt:%d", spb_pad->dummy_req_cnt, spb_pad->dummy_done_cnt);
		}
		spb_pad->pad_done = true;
        return true;
    }
//    if(plp_trigger && !(spb_pad->dummy_done_cnt % 20))
//        ftl_core_trace(LOG_INFO, 0, "req cnt %d done cnt %d cur_type %d param_type %d", spb_pad->dummy_req_cnt,
//                                 spb_pad->dummy_done_cnt, spb_pad->cur.type, spb_pad->param.type);
    return false;
}
typedef enum pad_state{
    pad_done = 0,
    next_wl,
    next_blk,
}pad_state;
slow_code pad_state spb_fill_dummy_check_stop(ftl_spb_pad_t *spb_pad, pstream_t *ps, bool check_block_close_only)
{
    if (is_ps_no_need_to_pad(ps)) {
        //no need fill dummy
        if(!plp_trigger)
        	ftl_core_trace(LOG_INFO, 0x1484, "no need fill dummy nsid:%d type:%d", spb_pad->cur.nsid, spb_pad->cur.type);
		if(spb_pad->cur.nsid == 2 && spb_pad->cur.type == 0){
			//ftl_core_trace(LOG_INFO, 0, "set shr_qbtfilldone");
			shr_qbtfilldone = 1;
		}
        spb_pad->cur.type++;
        if (spb_pad->cur.type > spb_pad->param.end_type) {
			spb_pad->pad_done = true;
			/*if (plp_trigger) {
				ftl_core_trace(LOG_ALW, 0, "fill req done, wait ncl done, req cnt:%d, done cnt:%d", spb_pad->dummy_req_cnt, spb_pad->dummy_done_cnt);
			}*/
			if (spb_pad->dummy_done_cnt == spb_pad->dummy_req_cnt){
                spb_pad->ctx.cmpl(&spb_pad->ctx);
				put_free_spb_pad(spb_pad);
            }
            return pad_done;
        }
        spb_pad->cur.cwl_cnt = 0;
        return next_blk;
    } else if (check_block_close_only == false) {
        //spb_pad->cur.cwl_cnt++;
		//ftl_core_trace(LOG_INFO, 0, "spb_pad:0x%x, type:%d, cur cwl_cnt:%d", spb_pad, spb_pad->cur.type, spb_pad->cur.cwl_cnt);
		if (spb_pad->param.pad_all) {
			// do not add cwl_cnt, to fill whold spb
			return next_wl;
		}
        if (spb_pad->cur.cwl_cnt == spb_pad->param.cwl_cnt) {
			if(!plp_trigger){
	            ftl_core_trace(LOG_INFO, 0x8651, "nsid:%d type:%d spb:%u, wptr:%u cwl cnt:%d done,check next", spb_pad->cur.nsid,
	            spb_pad->cur.type, ps->aspb[ps->curr].spb_id, ps->aspb[ps->curr].wptr, spb_pad->param.cwl_cnt);
			}
			if(spb_pad->cur.nsid == 2 && spb_pad->cur.type == 0){
				//ftl_core_trace(LOG_INFO, 0, "set shr_qbtfilldone2");
				shr_qbtfilldone = 1;
			}
            spb_pad->cur.type++;
			if(!plp_trigger)
				ftl_core_trace(LOG_INFO, 0x169e, "cur.type:%d param.type:%d", spb_pad->cur.type, spb_pad->param.end_type);
            if (spb_pad->cur.type > spb_pad->param.end_type) {
				spb_pad->pad_done = true;
				if(!plp_trigger)
					ftl_core_trace(LOG_ALW, 0x6a60, "fill req done, wait ncl done, req cnt:%d, done cnt:%d", spb_pad->dummy_req_cnt, spb_pad->dummy_done_cnt);
                if (spb_pad->dummy_done_cnt == spb_pad->dummy_req_cnt){
					ftl_core_trace(LOG_INFO, 0xc2bc, "spb_pad_idx:%d es %d str[nsid/typ](%d/%d)", \
						spb_pad->spb_pad_idx, time_elapsed_in_ms(spb_pad->start), spb_pad->param.start_nsid, spb_pad->param.start_type);
                    spb_pad->ctx.cmpl(&spb_pad->ctx);
					put_free_spb_pad(spb_pad);
                }
                return pad_done;
            }
            spb_pad->cur.cwl_cnt = 0;
            return next_blk;
        }
    }
    return next_wl;
}

//extern u32 fill_done_cnt,fill_req_cnt;

fast_code void req_fill_dummy_done(ftl_spb_pad_t *spb_pad)
{
	spb_pad->dummy_done_cnt++;
	if (is_spb_fill_dummy_done(spb_pad)) {
		if (spb_pad->dummy_done_cnt == spb_pad->dummy_req_cnt) {
			//ftl_core_trace(LOG_INFO, 0, "spb_pad_idx:%d es %d", spb_pad->spb_pad_idx, time_elapsed_in_ms(spb_pad->start));
			#ifdef ERRHANDLE_GLIST  			// ISU, EH_GC_not_done (3)
			if(!plp_trigger){
				ftl_core_trace(LOG_INFO, 0x971f, "id:%d  %d ms (%d/%d)", \
					spb_pad->spb_pad_idx, time_elapsed_in_ms(spb_pad->start), spb_pad->param.start_nsid, spb_pad->param.start_type);
			}

			ftl_core_t *fcore = fcns[spb_pad->param.start_nsid]->ftl_core[spb_pad->param.start_type];
        	if (fcore->err.b.prog_err_blk_in || fcore->err.b.rd_open_close)  	// Consider read disturb force close
        	{
        		fcore->err.b.dummy_done = true;
        	}
        	#endif

			spb_pad->ctx.cmpl(&spb_pad->ctx);
			put_free_spb_pad(spb_pad);
			sys_assert(!is_spb_pad_allocated(spb_pad));
		}
       // if(plp_trigger)
          //  ftl_core_trace(LOG_INFO, 0, "done cnt %d req cnt %d",spb_pad->dummy_done_cnt, spb_pad->dummy_req_cnt);
        //fill_done_cnt = spb_pad->dummy_done_cnt;
        //fill_req_cnt = spb_pad->dummy_req_cnt;
		return;
	} else if (get_ncl_w_cnt() > 8) {
		//ftl_core_trace(LOG_INFO, 0, "fcore_fill_dummy_task() startup");
		//fcore_fill_dummy_task();
	}

}

slow_code void jump_to_last_wl(pstream_t *ps)  
{  
    aspb_t *aspb = &ps->aspb[ps->curr];  
    u32 curr_wptr = aspb->wptr;  
    u16 skip_wl_num = occupied_by(aspb->max_ptr - aspb->wptr, nand_info.interleave * nand_info.bit_per_cell) - 1;   
#ifdef SKIP_MODE 
    u16 good_plane_cnt = nand_info.interleave - (aspb->max_ptr - aspb->defect_max_ptr) / nand_page_num();  
#else  
    u16 good_plane_cnt = nand_info.interleave;  
#endif  
    u32 skip_page_num = skip_wl_num * good_plane_cnt * nand_info.bit_per_cell;  

    aspb->cmpl_ptr += skip_page_num;   
    aspb->cmpl_cnt += skip_page_num;  
    aspb->wptr += skip_wl_num * nand_info.bit_per_cell * nand_info.interleave;	//aspb->wptr points to the last wl   
    ps->avail_cnt -= skip_wl_num * nand_info.bit_per_cell * nand_info.interleave;   

    ftl_core_trace(LOG_INFO, 0xbb90, "spb: %u jump from wl:%u offset:%u to wl:%u offset:%u",    
				 aspb->spb_id, curr_wptr/(nand_info.interleave * nand_info.bit_per_cell), curr_wptr&(nand_info.interleave - 1),    
				 aspb->wptr/(nand_info.interleave * nand_info.bit_per_cell), aspb->wptr&(nand_info.interleave - 1));   
}  
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
slow_code void jump_to_next_wl(pstream_t *ps , u16 skip_wl_num)  //just for SLC
{  
    aspb_t *aspb = &ps->aspb[ps->curr];  
    u32 curr_wptr = aspb->wptr;  
    //u16 skip_wl_num = occupied_by(aspb->max_ptr - aspb->wptr, nand_info.interleave * nand_info.bit_per_cell) - 1;   
#ifdef SKIP_MODE 
    u16 good_plane_cnt = nand_info.interleave - (aspb->max_ptr - aspb->defect_max_ptr) / nand_page_num_slc();  
#else  
    u16 good_plane_cnt = nand_info.interleave;  
#endif  
    u32 skip_page_num = skip_wl_num * good_plane_cnt ;  

    aspb->cmpl_ptr += skip_page_num;   
    aspb->cmpl_cnt += skip_page_num;  
    aspb->wptr += skip_wl_num  * nand_info.interleave;	//aspb->wptr points to the last wl   
    ps->avail_cnt -= skip_wl_num  * nand_info.interleave;   

    ftl_core_trace(LOG_INFO, 0x8074, "spb: %u jump from wl:%u offset:%u to wl:%u offset:%u",    
				 aspb->spb_id, curr_wptr/(nand_info.interleave ), curr_wptr&(nand_info.interleave - 1),    
				 aspb->wptr/(nand_info.interleave ), aspb->wptr&(nand_info.interleave - 1));   
}  
#endif
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
slow_code u32 get_plp_lda_dtag_w_idx(void) 
{ 
	return plp_lda_dtag_w_idx; 
} 
#endif

#define FILL_DUMMY_LOG 0

fast_code void fcore_fill_dummy_task(void)
{
	ftl_spb_pad_t *cur;
    cur_wr_pl_t *cur_wr_pl;
    core_wl_t *cwl;
    pstream_t *ps;
    u32 pad_cnt;
//  u32 wpl_mode;
	u8 i;
    ncl_req_cmpl_t cpl = NULL;
	u8 reason;

	aspb_t *aspb; 
	u32 page; 

    //cpu_msg_isr();

	for (i = 0; i < MAX_SPBPAD_CNT; ++i) {
		cur = &open_ctrl.spb_pad[i];
		// only handle allocated spb_pad
		if (is_spb_pad_allocated(cur)) {

			if (plp_trigger && (cur->pad_attribution != pad_plp)) {
				// don't handle any other pad, until plp done
				continue;
			}

			if (cur->pad_done) {
				// current spb pad is done, start next
				continue;
			}

			// just continue set task until all spb pad has been free
			evt_set_cs(open_ctrl.evt_fill_dummy_task, 0, 0, CS_TASK);

			if (get_ncl_w_cnt() <= (shr_nand_info.lun_num)) {
				return;
			}
pre_check:
			if (cur->cur.nsid == HOST_NS_ID && cur->cur.type == FTL_CORE_GC) {
				cpl = ftl_core_gc_wr_done;
			} else {
				cpl = NULL;
			}

			if(ftl_core_close_open_die(cur->cur.nsid, cur->cur.type, cpl))
			{
				return;
			}
			cwl = &fcns[cur->cur.nsid]->ftl_core[cur->cur.type]->cwl;
			ps = &fcns[cur->cur.nsid]->ftl_core[cur->cur.type]->ps;


			aspb = &ps->aspb[ps->curr]; 
			page = aspb->wptr / nand_info.interleave; 

			 if (plp_trigger && (page / nand_info.bit_per_cell == nand_info.geo.nr_pages / nand_info.bit_per_cell - 1) && (ps->aspb[ps->curr].flags.b.plp_tag != 2)) { 
                ps->aspb[ps->curr].flags.b.plp_tag = 1; 
            } 


			if (cwl->cur.mpl_qt == 0) {
                pad_state pads = spb_fill_dummy_check_stop(cur, ps, false);
				if (pads == pad_done) {
					return;
				} else if(pads == next_blk) {
					sys_assert(cur->cur.cwl_cnt == 0); // cwl_cnt == 0-->ps changed
					goto pre_check;
				}
			}

#if FILL_DUMMY_LOG
			ftl_core_trace(LOG_INFO, 0xfd23, "ns %d type %d spb %d page %d ptr:%d max ptr:%d",
				 cur->cur.nsid, cur->cur.type, aspb->spb_id, page, aspb->wptr, aspb->max_ptr);
#endif

again:
			if(plp_trigger && (cur->pad_attribution == pad_por)){
				// Skip Close Blk During Shutdown dlow if plp happend
				cur->ctx.cmpl(&cur->ctx);
				u32	spb_pad_idx = find_first_zero_bit(&open_ctrl.using_bmp, MAX_SPBPAD_CNT);
				//don't free this pad idx , dummy ncl req still no finish
				//if free it , plp fill dummy req may never complete
				if (spb_pad_idx == MAX_SPBPAD_CNT) 
				{				
					put_free_spb_pad(cur);//resource not enough
				}
				return;
			}
			cwl = &fcns[cur->cur.nsid]->ftl_core[cur->cur.type]->cwl;
			ps = &fcns[cur->cur.nsid]->ftl_core[cur->cur.type]->ps; 
			cur_wr_pl = ftl_core_get_wpl_mix_mode(cur->cur.nsid, cur->cur.type, WPL_V_MODE, false, NULL, &reason);
			if (cur_wr_pl == NULL) {
				/* bug fix for fill dummy never stop, if no good plane after parity die, cwl->cur.usr_mpl_qt will
				   never decrese to 0, since "if (req)" && "if (cwl->cur.usr_mpl_qt == 0)" will never meet together */
				sys_assert(reason != no_reason);
				if ((cwl->cur.mpl_qt == 0) && (reason == cwl_finished_not_start_new)) {
					cur->cur.cwl_cnt++;
					if (plp_trigger) { 
                        //extern volatile fcns_attr_t fcns_attr[INT_NS_ID + 1]; 
            			//fcns_attr[HOST_NS_ID].b.p2l = true; 
						ftl_core_trace(LOG_ALW, 0x368a, "1 --- blk:%d type:%d, fill %d wl", 
						     ps->aspb[0].spb_id, cur->cur.type, cur->cur.cwl_cnt); 
					} 
					pad_state pads = spb_fill_dummy_check_stop(cur, ps, false);
					if (pads == pad_done) {
						return;
					} else if(pads == next_blk) {
						sys_assert(cur->cur.cwl_cnt == 0);
						goto pre_check;
					}

                    //if QBT has filled a layer, go to the last wl to fill it  
                    if (cur->cur.nsid == INT_NS_ID && cur->cur.type == FTL_CORE_NRM && cur->cur.cwl_cnt == 4)  
                    {  
                        aspb_t *aspb = &ps->aspb[ps->curr];  
                        if ((aspb->wptr / nand_info.interleave) < (nand_info.geo.nr_pages - 3))  
                        {  
				            jump_to_last_wl(ps);  
                        }  
                    } 
				}
				return;
			}
			pad_cnt = ftl_core_next_write(cur->cur.nsid, cur->cur.type) - cur_wr_pl->cnt;
			while (pad_cnt) {
				if(cur->cur.nsid == INT_NS_ID && cur->cur.type == FTL_CORE_NRM){
					cur_wr_pl->lda[cur_wr_pl->cnt] = QBT_LDA;
					cur_wr_pl->flags.b.ftl = 1;
				}else if(cur->cur.nsid == INT_NS_ID && cur->cur.type == FTL_CORE_PBT){
					cur_wr_pl->lda[cur_wr_pl->cnt] = PBT_LDA;
					cur_wr_pl->flags.b.ftl = 1;
				}else{
					cur_wr_pl->lda[cur_wr_pl->cnt] = INV_LDA;
				}
				cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = WVTAG_ID;
				cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
				cur_wr_pl->cnt++;
				pad_cnt--;
			}
			cur_wr_pl->flags.b.dummy = 1;
			ncl_w_req_t *req = ftl_core_submit(cur_wr_pl, (void*)cur, NULL);
			if (req) {
				cur->dummy_req_cnt++;

				/* if parity die is not the last die, cwl_cnt add here */
				if (cwl->cur.mpl_qt == 0) { 
					cur->cur.cwl_cnt++; 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
					if (plp_trigger && !cwl->flags.b.slc && cur->cur.cwl_cnt == 1) 
#else
					if (plp_trigger) 
#endif					
					{
//					for (u8 i = 0; i < 6; ++i) { 
//								ftl_core_trace(LOG_ALW, 0, "req:0x%x, pda[%d]=0x%x", req, i, req->w.pda[i]); 
//					} 
//					u32 blk = req->w.pda[0] >> 21 & ((1 << 11) - 1); 
//					u32 wl = (req->w.pda[0] >> 10 & ((1 << 11) - 1))/3; 
					ps->aspb[ps->curr].flags.b.plp_tag = 1;

					u16 wl     =  pda2page(req->w.pda[0])/3;
					u16 p2l_wl =  ps->aspb[ps->curr].p2l_page/3;
					//u32 w_page = ps->aspb[ps->curr].wptr/nand_info.interleave;  

					//ftl_core_trace(LOG_ALW, 0, "wptr %u max_ptr %u page %u, interleave %u",  
					//	ps->aspb[ps->curr].wptr, ps->aspb[ps->curr].max_ptr, w_page, nand_info.interleave);  
					//ftl_core_trace(LOG_ALW, 0, "2 --- blk:%d wl:%d type:%d, fill %d wl, plp_tag %u",  
					 //  ps->aspb[0].spb_id,wl, cur->cur.type, cur->cur.cwl_cnt,  ps->aspb[ps->curr].flags.b.plp_tag);  

					ftl_core_trace(LOG_INFO, 0x101f, "2 - b %d wl %d t %d f %d p2l %d %d", 
					 			ps->aspb[0].spb_id, wl, cur->cur.type, cur->cur.cwl_cnt, p2l_wl,ps->aspb[ps->curr].p2l_die); 

					#if 0 //mark by Jay--- if jump to last wl,spor fill dummy will UNCR
					u16 cur_wl = (ps->aspb[ps->curr].wptr / nand_info.interleave) / nand_info.bit_per_cell; 
					if((cur_wl != ps->aspb[ps->curr].p2l_page/3) && (cur->cur.cwl_cnt == 1) ) 
					{	 
						/* 
							1.set plp force p2l special meta tag for spor->FTL_PLP_FORCE_TAG 
							2.set fcns_attr.p2l false,close p2l function(p2l update and pagesn update), 
							  reset when fill last wl complete 
							3.record spb_id in plp_spb_flush_p2l ,when host and gc need to save p2l this flag will be use 
							4.set ps jump to last wl,change aspb->wptr 
							5.get last p2l position , but need to reset p2l group 
							6.set special dummy meta for last wl,when power on scan last page can distinguish this 
							7.record partial lda dtag(buffer) write idx for init   
						*/ 
						ps->aspb[ps->curr].flags.b.plp_tag = 2; 
						extern volatile fcns_attr_t fcns_attr[INT_NS_ID + 1]; 
						fcns_attr[HOST_NS_ID].b.p2l = false; 
						ftl_core_trace(LOG_ALW, 0xb36f, "[pre]p2l info->>spb:%d cur_wl:%d p2l_wl:%d grp_id:%d fcns.p2l:%d",  
							ps->aspb[ps->curr].spb_id,cur_wl,ps->aspb[ps->curr].p2l_page/3,ps->aspb[ps->curr].p2l_grp_id,fcns_attr[HOST_NS_ID].b.p2l);  

						plp_spb_flush_p2l = ps->aspb[ps->curr].spb_id; 
						jump_to_last_wl(ps);  
						p2l_get_next_pos(&ps->aspb[ps->curr], &ps->pu[ps->curr], ps->nsid); 
						ps->aspb[ps->curr].p2l_grp_id--; 
						u32 id      = w_page >> p2l_para.pg_p2l_shift; 
						u16 p2l_idx = id/3; 
						u16 p2l_off = id%3; 
						u16 page_off  = w_page%p2l_para.pg_per_p2l; 
						plp_lda_dtag_w_idx = (DU_CNT_PER_P2L*page_off)/p2l_para.pg_per_p2l; 
						plp_p2l_group_idx  = ps->aspb[ps->curr].p2l_grp_id; 
						ftl_core_trace(LOG_ALW, 0x8e29, "id:%d p2l_idx:%d p2l_off:%d page_off:%d plp_lda_idx:%d",id,p2l_idx,p2l_off,page_off,plp_lda_dtag_w_idx); 

					} 
					#endif 
					} 
					pad_state pads = spb_fill_dummy_check_stop(cur, ps, false);
					if (pads == pad_done) {
						return;
					} else if(pads == next_blk) {
						sys_assert(cur->cur.cwl_cnt == 0);
						goto pre_check;
					}

                    //if QBT has filled a layer, go to the last wl to fill it  
                    if (cur->cur.nsid == INT_NS_ID && cur->cur.type == FTL_CORE_NRM && cur->cur.cwl_cnt == 4)  
                    {  
                        aspb_t *aspb = &ps->aspb[ps->curr];  
                        if ((aspb->wptr / nand_info.interleave) < (nand_info.geo.nr_pages - 3))  
                        {  
				            jump_to_last_wl(ps);  
                        }  
                    } 

				}

				if (get_ncl_w_cnt() <= (shr_nand_info.lun_num)) {
					return;
				}
				if (cur->dummy_req_cnt - cur->dummy_done_cnt >= RDISK_NCL_W_REQ_CNT-(shr_nand_info.lun_num)) {
					return;
				}
			}

			if (cwl->cur.mpl_qt)
				goto again;
			sys_assert(cwl->cur.open_die == 0);
		}
	}
}


/* function use to fill dummy/close spb, only handle current open? */
fast_code void launch_fill_dummy(ftl_spb_pad_t *spb_pad)
{
	spb_pad->private_cmpl = req_fill_dummy_done;
    spb_pad->dummy_req_cnt = 0;
    spb_pad->dummy_done_cnt = 0;
	spb_pad->pad_done = false;
	spb_pad->start = get_tsc_64();
	evt_set_imt(open_ctrl.evt_fill_dummy_task, 0, 0);
}

fast_code void ipc_fill_dummy_done(ftl_core_ctx_t *ctx);

fast_code bool ftl_core_spb_pad(ftl_spb_pad_t *spb_pad)
{
    pstream_t *ps;
	aspb_t *aspb;
	u8 i, j;
	bool found = true;

	if (spb_pad->spb_id != 0xFFFF) {
		found = false;
		for (i = 1; i < TOTAL_NS; ++i) {
			for (j = FTL_CORE_NRM; j < FTL_CORE_MAX; ++j) {
			    ps = &fcns[i]->ftl_core[j]->ps;
    			aspb = &ps->aspb[ps->curr];
				if (aspb->spb_id == spb_pad->spb_id) {
					//spb_pad->param.cwl_cnt = 0;	// ISU, LJ1-337, PgFalClsNotDone (1)
					//spb_pad->param.pad_all = true;
					// When request spb_pad, cwl_cnt = 0 for pad_all = true.
					//spb_pad->param.cwl_cnt = 0;
					spb_pad->param.pad_all = (spb_pad->param.cwl_cnt != 0) ? false : true;
					spb_pad->param.start_type = j;
					spb_pad->param.end_type = j;
					spb_pad->param.start_nsid = i;
					spb_pad->param.end_nsid = i;
					//spb_pad->param.type = j;
					//spb_pad->param.nsid = i;
					spb_pad->cur.cwl_cnt = 0;
					spb_pad->cur.type = j;
					spb_pad->cur.nsid = i;

					//spb_pad->start_nsid = spb_pad->cur.nsid; // ISU, EH_GC_not_done (3)
					//spb_pad->start_type = spb_pad->cur.type;

					found = true;
					ftl_core_trace(LOG_INFO, 0x4017, "search find spb:%d, ns:%d, type:%d",aspb->spb_id, i, j);
					goto skip_check;
				}
			}
		}
	} else {
		// Cosider close block request through ipc_spb_pad w/ spb_id = 0xFFFF.
		spb_pad->param.start_nsid = spb_pad->cur.nsid; // ISU, EH_GC_not_done (3)
		spb_pad->param.start_type = spb_pad->cur.type;
		if(spb_pad->pad_attribution == pad_por && spb_pad->param.start_nsid == HOST_NS_ID)
		{
			//por fill dummy
			pstream_t *ps = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_NRM]->ps;
			host_dummy_start_wl = ps->aspb[0].wptr/(nand_info.interleave * nand_info.bit_per_cell);//cur wl
			ftl_core_trace(LOG_ALW, 0x81c6,"cur host wl:%d",host_dummy_start_wl);
		}
	}
skip_check:
	if (!found) {
		ftl_core_trace(LOG_WARNING, 0x125f, "close not found spb id:%d, may be closed done", spb_pad->spb_id);
		/* return resource */
		spb_pad->ctx.cmpl(&spb_pad->ctx);
		put_free_spb_pad(spb_pad);
	}
	else
	{
		launch_fill_dummy(spb_pad);
	}
	return true;
}

fast_code u32 ftl_core_next_write(u32 nsid, u32 type)
{
	pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;

	// todo: more advance
	return ps->du_cnt_per_cmd;
}

fast_code void ftl_core_resume(u32 nsid, u32 type)
{
	pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;

	ps->flags.b.query_nxt = 0;

	pstream_rsvd(ps);
	//pstream_supply(ps);
	ps->flags.b.queried = 0;


	if (nsid == HOST_NS_ID && type == FTL_CORE_GC)
		ftl_core_gc_pend_out();
	/*
	if (nsid == INT_NS_ID && type == FTL_CORE_NRM && tbl_flush_block) {
		//tbl_flush_block = false;
		ftl_core_seg_flush(tbl_flush_ctx);
	}
	*/

	if(nsid == HOST_NS_ID){
		ftl_core_flush_blklist_need_resume();
	}
}

slow_code void ftl_core_start(u32 nsid)
{
	u32 i;

	for (i = 0; i < FTL_CORE_MAX; i++) {
		if (i == FTL_CORE_GC)
			continue;

		if(FTL_INIT_DONE == false){
			if(nsid == 2 && i != FTL_CORE_PBT){
				continue;
			}
		}

		#if (PBT_OP == mENABLE)
		if (i == FTL_CORE_PBT){
			if((nsid == 1)){
				continue;
				//ftl_pbt_init();
			}
			else{
				if(FTL_INIT_DONE == false){
					ftl_pbt_init(false);
				}
				else{
					continue;
				}
			}
		}
		#endif

		#if (POWER_ON_OPEN == DISABLE) //mark for not open block before data in pis20220211
		if(FTL_INIT_DONE == true)
		#endif
		{

		if (fcns[nsid]->ftl_core[i]) {
			pstream_t *ps = &fcns[nsid]->ftl_core[i]->ps;

			pstream_rsvd(ps);
		}
		}
	}
	ftl_core_trace(LOG_INFO, 0x8a7b, "NS %d start", nsid);
	if((nsid == 2) && (_fg_warm_boot == true))
	{
	    ftl_core_restore_pbt_param();
        evt_set_cs(evt_force_pbt, true, DUMP_PBT_WARMBOOT, CS_TASK);
		//ftl_core_trace(LOG_INFO, 0, "warmboot trigger force pbt");
		//ftl_set_force_dump_pbt(0,true,DUMP_PBT_NORMAL_PARTIAL);
	}

#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)	 
	if((nsid == 2) && (delay_flush_spor_qbt == true) && (FTL_INIT_DONE == false)) 
	{ 
		ftl_core_trace(LOG_INFO, 0x8a4c, "spor save qbt task"); 
		evt_set_cs(evt_save_fake_qbt, 0, 0, CS_TASK); 
	}
#endif 


}

fast_code void ftl_core_ready(u32 p0, u32 ns_bmp, u32 p2)
{
	u32 i, j;
	bool ready = true;

	for (j = 0; j < INT_NS_ID + 1; j++) {
		if (!(ns_bmp & (1 << j)))
			continue;
		for (i = 0; i < FTL_CORE_MAX; i++) {
			if (i == FTL_CORE_GC)
				continue;

			if (i == FTL_CORE_NRM && j ==INT_NS_ID)
				continue;

			if (fcns[j]->ftl_core[i]) {
				pstream_t *ps = &fcns[j]->ftl_core[i]->ps;
				ready &= pstream_ready(ps);
			}
		}
	}

	#if (POWER_ON_OPEN == ENABLE) //mark for not open block before data in pis20220211
	if(ready)
	#else
	if(1)
	#endif
	{
		ftl_core_trace(LOG_INFO, 0x0b83, "fc 0x%x ready", ns_bmp);
		//if(ns_bmp == 0x2){
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FC_READY_DONE, 0, 0);
		//}
	}
	else
	{
		evt_set_cs(evt_fc_ready_chk, ns_bmp, 0, CS_TASK);
	}
}

fast_code void qbt_alloc_fill_pbt_layer_done(ftl_core_ctx_t *ctx)
{
	ns_start_t *ns_ctx = ctx->caller;
	ftl_pbt_init(false);
	ftl_set_pbt_halt(true);
	ftl_core_trace(LOG_INFO, 0xd84d, "qbt fill done:0x%x", ns_ctx);
	ns_ctx->cmpl(ns_ctx);
}

fast_code void ftl_core_qbt_alloc(ns_start_t *ctx)
{
	u16 nsid = ctx->nsid;
	u32 ns_bmp = (1 << nsid);
	bool ready = true;
	#if (POWER_ON_OPEN == ENABLE)
	u16 type = ctx->type;
	pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;
	ready &= pstream_ready(ps);
	#else
	pstream_t *ps = &fcns[INT_NS_ID]->ftl_core[FTL_CORE_NRM]->ps;
	if(ps->aspb->spb_id == INV_SPB_ID){
		ready = false;
	}
	#endif
	if(plp_trigger)
	{
		ctx->cmpl(ctx);
	}
	else
	{
		if(!ready)
		{
			evt_set_cs(evt_fc_qbt_ready_chk, (u32)ctx, 0, CS_TASK);
			return;
		}
		else if(ready && (shr_qbt_prog_err == false))
		{
    		ftl_core_trace(LOG_INFO, 0xfb15, "fc 0x%x ready", ns_bmp);
#if (PBT_OP == mENABLE)
    		ftl_set_pbt_halt(true);
    		fcore_fill_dummy(FTL_CORE_PBT, FTL_CORE_PBT, FILL_TYPE_LAYER, qbt_alloc_fill_pbt_layer_done, ctx);
#endif
    		//ctx->cmpl(ctx);
		}
        else if (shr_qbt_prog_err)
        {
            ctx->cmpl(ctx);
        }
	}
}

fast_code inline void ftl_core_set_spb_close(u16 spb_id, u8 core_type, u8 nsid)
{
	extern spb_rt_flags_t *spb_rt_flags;
	extern volatile spb_id_t host_spb_close_idx;
	extern volatile spb_id_t gc_spb_close_idx;

	spb_rt_flags[spb_id].b.open = false;

	if((core_type == FTL_CORE_NRM) && (nsid == 1))
	{
		bool ret;
		CBF_INS(&close_host_blk_que, ret, spb_id);
		sys_assert(ret == true);
		host_spb_close_idx = spb_id;
	}

	if((core_type == FTL_CORE_GC) && (nsid == 1))
	{
		bool ret;
		CBF_INS(&close_gc_blk_que, ret, spb_id);
		sys_assert(ret == true);
		gc_spb_close_idx = spb_id;
	}
}

slow_code void pstream_force_close(u16 close_blk){	// ISU, Tx, PgFalClsNotDone(1)
	ftl_core_t *fc = NULL;
	if (close_blk == INV_SPB_ID){	// ISU, SetGCSuspFlag
	    ftl_core_trace(LOG_INFO, 0xaa13, "[EH] inv spb id");
		return;
	}else if(close_blk == ps_open[FTL_CORE_NRM]){
		fc = fcns[HOST_NS_ID]->ftl_core[FTL_CORE_NRM];
	}else if(close_blk == ps_open[FTL_CORE_GC]){
		fc = fcns[HOST_NS_ID]->ftl_core[FTL_CORE_GC];
	}else{
		return;
	}
	fc->cwl.cur.mpl_qt = 0;
	ftl_core_clean(HOST_NS_ID);
	//sys_assert(fc->wreq_cnt == 0);	// for raid parity write request, Paul_20210914
	//if (fc->wreq_cnt)
	//	ftl_core_trace(LOG_INFO, 0, "fc->wreq_cnt=%d", fc->wreq_cnt);

	//fc->err.b.prog_err_blk_in = false;
	//ftl_core_trace(LOG_ALW, 0, "spb %d err handle done, wreq_cnt %d", close_blk, fc->wreq_cnt);	// ISU, SetGCSuspFlag

	pstream_reset(&fc->ps);
	if(CBF_EMPTY(fc->ps.spb_que) && fc->ps.flags.b.queried == 0 && fc->ps.flags.b.query_nxt == 0){
		fc->ps.flags.b.query_nxt = 1;
		pstream_rsvd(&fc->ps);
	}
}

#ifdef NS_MANAGE
share_data volatile struct ns_section_id ns_sec_id[32];
#define NS_SIZE_GRANULARITY_FTL1	(0x200000000 >> LBA_SIZE_SHIFT1)//joe 20200819
#define NS_SIZE_GRANULARITY_FTL_BITOP1 (24)
#define NS_SIZE_GRANULARITY_FTL2	(0x200000000 >> LBA_SIZE_SHIFT2)//joe 20200819
#define NS_SIZE_GRANULARITY_FTL_BITOP2 (21)
extern u16 *sec_order_point;//joe add hcrc faster 20200825
share_data volatile struct ns_array_manage *ns_array_menu;
//share_data u16 ns_sec_order[1024];
share_data volatile u16 *sec_order_p;
share_data volatile u16 drive_total_sector;//joe 20200528
share_data volatile u16 ns_order;
share_data volatile u16 ns_valid_sec;
share_data volatile u8 full_1ns;
#endif
extern u16 host_sec_size;//joe add change sec size 20200819
extern u8 host_sec_bitz;//joe add change sec size  20200819





#ifdef LJ_Meta
fast_code u32 ftl_core_setup_meta(ncl_w_req_t *req, cur_wr_pl_t *wr_pl, u8 ftl_blk_loop)
#else
fast_code u32 ftl_core_setup_meta(ncl_w_req_t *req, u32 total, u32 sn)
#endif
{
	bm_pl_t *bm_pl = req->w.pl;
#ifndef LJ_Meta
	pda_t *pda = req->w.pda;
#endif
	lda_t *lda = req->w.lda;
	u32 ofst = req->req.cnt;
	u32 i;
	u32 ret = 0;

#ifdef NS_MANAGE//joe add define 20200916
	// u16 nsid00=0;//joe add 20200803
	u64 lda_secid1=0;
	 //u16 lda_secid2=0;//joe add 20200825 for hcrc
	//u64 ns_lbaa=0;
	u64 ns_lba_temp=0;
#endif
#if (BG_TRIM == ENABLE)
    u8 mp_cnt = get_mp_cnt(req->w.pda, req->req.cnt << DU_CNT_SHIFT);
#endif
    //req->req.trim_page = 0;
#if 0//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
	bool tbl_meta = (req->req.nsid == INT_NS_ID && req->req.type != FTL_CORE_SLC) ? true : false;   //willis2
#else
	bool tbl_meta = (req->req.nsid == INT_NS_ID) ? true : false;   //willis2
#endif
    if(lda[ofst] == PARITY_LDA && tbl_meta){
        tbl_meta = false;
    }
	for (i = 0; i < wr_pl->cnt; i++)
    {
#ifndef LJ_Meta
		pda_t p = pda[ofst >> DU_CNT_SHIFT] + (ofst & (DU_CNT_PER_PAGE - 1));
#endif
		dtag_t dtag;
		io_meta_t *meta;
		lda_t cur_lda = lda[ofst];
		if (bm_pl[ofst].pl.type_ctrl == BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM) {
			dtag.dtag = (bm_pl[ofst].pl.dtag & SEMI_SDTAG_MASK);
		} else {
			dtag.dtag = bm_pl[ofst].pl.dtag;
		}
		extern struct du_meta_fmt dtag_meta[];
		extern struct du_meta_fmt *ddtag_meta;
		extern struct du_meta_fmt *ddtag_tbl_meta;
		extern struct du_meta_fmt *wr_dummy_meta;

		if (cur_lda >= INV_LDA){
            ret++;
            //if (!req->req.op_type.b.dummy)
            {
                bm_pl[ofst].pl.type_ctrl |= META_SRAM_IDX;
        		// meta index mode, use nvm_cmd_id as dtag index
        		bm_pl[ofst].pl.nvm_cmd_id = wr_dummy_meta_idx; // todo:
        		wr_dummy_meta->lda = DUMMY_LDA;
        		ofst++;
                //break;
                continue;
            }
        }
        #if(BG_TRIM == ENABLE)
        if(cur_lda  == TRIM_LDA){
            req->req.trim_page |= BIT(ofst/(mp_cnt*DU_CNT_PER_PAGE));
            //ftl_core_trace(LOG_INFO, 0, "[TRIM]pda : 0x%x", req->w.pda[ofst >> DU_CNT_SHIFT] + (ofst & (DU_CNT_PER_PAGE - 1)));
        }
        #endif
		if (dtag.b.dtag == EVTAG_ID) {
			req->req.wunc_cnt++;
			bm_pl[ofst].pl.type_ctrl |= META_SRAM_IDX;
			bm_pl[ofst].pl.nvm_cmd_id = wr_dummy_meta_idx;
			ofst++;
			continue;
		}

		if (tbl_meta && dtag.b.dtag == WVTAG_ID) {
			bm_pl[ofst].pl.type_ctrl |= META_SRAM_IDX;
			bm_pl[ofst].pl.nvm_cmd_id = wr_dummy_meta_idx;
			wr_dummy_meta->lda = DUMMY_LDA;
			ofst++;
			continue;
		}

		if (dtag.b.in_ddr) {
			if(tbl_meta == true){
#if RAID_SUPPORT
                if(wr_pl->flags.b.parity_mix){
                    meta = (io_meta_t *)shr_idx_meta[DDR_IDX_META];
                    meta = (io_meta_t *)&meta[bm_pl[ofst].pl.nvm_cmd_id];
                }
                else
#endif
				    meta = (io_meta_t *) &ddtag_tbl_meta[i];
				bm_pl[ofst].pl.type_ctrl |= META_DDR_IDX;
				//ftl_core_trace(LOG_INFO, 0, "pbt pda : 0x%x, nvm_cmd_id:0x%x", pda[bm_pl[ofst].pl.du_ofst], bm_pl[ofst].pl.nvm_cmd_id);
			}
			else{
	        	sys_assert(dtag.b.dtag < MAX_DDR_DTAG_CNT); // for P2L, by Vito
			    //sys_assert(dtag.b.dtag < (DDR_GC_DTAG_START + FCORE_GC_DTAG_CNT));  // for GC, by Hengtzu
		    	meta = (io_meta_t *) &ddtag_meta[dtag.b.dtag];
		    	bm_pl[ofst].pl.type_ctrl |= META_DDR_DTAG;
            }
		} else {
			sys_assert(dtag.b.dtag < SRAM_IN_DTAG_CNT);
			meta = (io_meta_t *) &dtag_meta[dtag.b.dtag];
			bm_pl[ofst].pl.type_ctrl |= META_SRAM_DTAG;
		}

#ifdef LJ_Meta
      extern u32 shr_wunc_meta_flag;
      if (shr_wunc_meta_flag == true) {
        temp_meta.wunc.WUNC = meta->wunc.WUNC;  // WUNC not set here, refer to set_pdu_bmp
        //temp_meta.wunc = meta->wunc;
	  }
        switch (i%DU_CNT_PER_PAGE)
    	{
    		case 0:
    			temp_meta.fmt1.page_sn_L = (shr_page_sn&0xFFFFFF);
    			break;
    		case 1:
				temp_meta.fmt2.page_sn_H = ((shr_page_sn>>24)&0xFFFFFF);
    			break;
    		case 2:
				temp_meta.fmt3.blk_sn_L = (req->w.spb_sn&0xFFFF);
                if(req->req.type == FTL_CORE_NRM)
                {
                    if(req->req.nsid == HOST_NS_ID)
                    {
						temp_meta.fmt3.blk_type = FTL_BLK_TYPE_HOST;
                    }
                    else if(req->req.nsid == INT_NS_ID)
                    {
						temp_meta.fmt3.blk_type = FTL_BLK_TYPE_FTL;
                    }
                }
                else if(req->req.type == FTL_CORE_GC)
                {
					temp_meta.fmt3.blk_type = FTL_BLK_TYPE_GC;
                }
                else if(req->req.type == FTL_CORE_PBT)
                {
					temp_meta.fmt3.blk_type = FTL_BLK_TYPE_FTL;
                }

    			break;
    		case 3:
                temp_meta.fmt4.blk_sn_H = ((req->w.spb_sn>>16) & 0xFFFF);
    			temp_meta.fmt4.Debug = 0xFF;
    			break;
    		default:
    			panic("meta !=0~3\n");
    	}
#endif

#ifndef LJ_Meta
		//meta->sn = sn;
		temp_meta.sn = sn;
#endif


         if((cur_lda == P2L_LDA) && !wr_pl->flags.b.parity && plp_trigger ) 
        { 
            // P2L during PLP 
            if(req->req.op_type.b.plp_tag == 1) 
				temp_meta.lda = FTL_PLP_TAG; 
			#if (PLP_FORCE_FLUSH_P2L == mENABLE)
            else if(req->req.op_type.b.plp_tag == 2) 
				temp_meta.lda = FTL_PLP_FORCE_TAG | plp_p2l_group_idx; 
        	#endif

			else 
				temp_meta.lda = P2L_LDA; 
			//if(i == 0)	
            //	ftl_core_trace(LOG_INFO, 0, "FTL_PLP_TAG %d pda:0x%x",req->req.op_type.b.plp_tag,req->w.pda[ofst>>DU_CNT_SHIFT]+i); 
            #if 0 //(DEBUG_SPOR == mENABLE) 
            ftl_core_trace(LOG_INFO, 0xa114, "[PLP]P2L pda : 0x%x, 0x%x", pda[i], req); 
			extern ncl_w_req_t * plp_p2l_req;  
			plp_p2l_req = req; 
            #endif 
        } 
        /*
		else if((cur_lda == P2L_LDA) && !wr_pl->flags.b.parity  && _fg_warm_boot)
		{
		    // P2L during WARM BOOT
	        temp_meta.lda = FTL_PLP_TAG;
		}
		else if((cur_lda == P2L_LDA) && !wr_pl->flags.b.parity  && (fcns[wr_pl->nsid]->ftl_core[wr_pl->type]->err.b.prog_err_blk_in))	// ISU, EH_GC_not_done (3)
		{
			// Notice SPOR, this is force closed block
			temp_meta.lda = FTL_PLP_TAG;
			//ftl_core_trace(LOG_INFO, 0, "[DBG] force close set PLP Tag");
		}
		*/
        else
        {
            if(wr_pl->lda[i] == QBT_LDA)
            {
				temp_meta.lda  = (FTL_QBT_TABLE_TAG|ftl_blk_loop);
            }
            else if(wr_pl->lda[i] == PBT_LDA)
            {
				temp_meta.lda  = (FTL_PBT_TABLE_TAG|ftl_blk_loop);
                #if 0 // for dbg, by Sunny
                ftl_core_trace(LOG_INFO, 0xc660, "PBT meta : 0x%x", meta->lda);
                #endif
            }
            else if(req->w.lda[i] == BLIST_LDA)
            {
				temp_meta.lda   = FTL_BLIST_TAG; 
				if(i == 0)
				{
					if (seg_w_blist[req->req.type] == INV_U32)
                    {
                    	if(req->w.spb_sn > cur_pbt_sn || tFtlPbt.cur_seg_ofst != 0)
                    	{
                    		temp_meta.fmt1.page_sn_L   = (u16)tFtlPbt.cur_seg_ofst; 
	                        seg_w_blist[req->req.type] = tFtlPbt.cur_seg_ofst;
                    	}
                    	else
                    	{
							//avoid spor choose this blk
							//pbt can't flush if blist no done,so pbt_seg == 0
	                        temp_meta.fmt1.page_sn_L   = (u16)(ns[0].seg_end); 
                        	seg_w_blist[req->req.type] = ns[0].seg_end;
                        	/*
                        	ftl_core_trace(LOG_ALW, 0x60e3,"[DBG] cur_blk_sn:0x%x pbt_sn:0x%x seg:%d total_seg:%d tFtlPbt.pbt_cur_loop:%d",
                        		req->w.spb_sn,cur_pbt_sn,tFtlPbt.cur_seg_ofst,ns[0].seg_end,tFtlPbt.pbt_cur_loop);
							if(tFtlPbt.pbt_cur_loop != 0)
							{
								ftl_core_trace(LOG_ALW, 0x0431,"[Jay] hit!!!!!!!");
							}
							*/
                    	}

                    }
                }
                #if 0 // for dbg, by Sunny
                ftl_core_trace(LOG_ALW, 0xdc5a, "BLIST meta : 0x%x, PDA : 0x%x", meta->lda, pda[i]);
                #endif
            }
            else
            {
                // Host Data
				temp_meta.lda = cur_lda;
            }
        }


#ifdef NS_MANAGE //joe add define 20200916
		//ftl_core_trace(LOG_DEBUG, 0, "lda[%d]:0x%x",ofst, lda[ofst]);
		 //ns_array_menu->sec_order
		 ns_lba_temp = cur_lda;//joe 20201026  if here shift 3 bits will over flow, cause lda is u32!!
	//if(!(ns_order==1&&(drive_total_sector==ns_valid_sec))){
//if(1){
	if(!full_1ns){
		u64 secorder=0;
		ftl_core_trace(LOG_DEBUG, 0x1d3a, " ftl doer lda:0x%x  ns_lba_temp:0x%x" ,lda[ofst],ns_lba_temp);
		if(host_sec_bitz==9){	//joe add sec size 20200819
			ns_lba_temp=ns_lba_temp<<3;
			//lda_secid1=(ns_lba_temp)/NS_SIZE_GRANULARITY_FTL1;//joe add 20200819
			lda_secid1=(ns_lba_temp)>>NS_SIZE_GRANULARITY_FTL_BITOP1;
			//secorder=ns_array_menu->sec_order[lda_secid1];
			//secorder=ns_sec_order[lda_secid1];
			secorder=sec_order_p[lda_secid1];
			//meta->hlba=(ns_lba_temp-lda_secid1*NS_SIZE_GRANULARITY_FTL1)+sec_order_point[lda_secid1]*NS_SIZE_GRANULARITY_FTL1;//ns_lbaa+	sec_order_point[lda_secid1]*NS_SIZE_GRANULARITY_FTL1 ;
			//meta->hlba=(ns_lba_temp-(lda_secid1<<NS_SIZE_GRANULARITY_FTL_BITOP1))+(secorder<<NS_SIZE_GRANULARITY_FTL_BITOP1);
			temp_meta.hlba=(ns_lba_temp-(lda_secid1<<NS_SIZE_GRANULARITY_FTL_BITOP1))+(secorder<<NS_SIZE_GRANULARITY_FTL_BITOP1);
		}else{
			//lda_secid1=lda[ofst]/NS_SIZE_GRANULARITY_FTL2;
			lda_secid1=(ns_lba_temp)>>NS_SIZE_GRANULARITY_FTL_BITOP2;
			//secorder=ns_array_menu->sec_order[lda_secid1];
			//secorder=ns_sec_order[lda_secid1];
			secorder=sec_order_p[lda_secid1];
			//meta->hlba=(ns_lba_temp-lda_secid1*NS_SIZE_GRANULARITY_FTL2)+	sec_order_point[lda_secid1]*NS_SIZE_GRANULARITY_FTL2;
			//meta->hlba=(ns_lba_temp-(lda_secid1<<NS_SIZE_GRANULARITY_FTL_BITOP2))+(secorder<<NS_SIZE_GRANULARITY_FTL_BITOP2);
			temp_meta.hlba=(ns_lba_temp-(lda_secid1<<NS_SIZE_GRANULARITY_FTL_BITOP2))+(secorder<<NS_SIZE_GRANULARITY_FTL_BITOP2);
		}
		ftl_core_trace(LOG_DEBUG, 0x13a6, "NS ns_lbaa1: H:0x%x  L:0x%x   ", (ns_lba_temp>>32),ns_lba_temp);
		ftl_core_trace(LOG_DEBUG, 0xeee6, " lda-secid1:%d " ,lda_secid1);
		ftl_core_trace(LOG_DEBUG, 0x102b, "  lda-secid2:%d " ,ns_array_menu->sec_order[lda_secid1]);
		//ftl_core_trace(LOG_DEBUG, 0, "NS ns_lbaa2: H:0x%x  L:0x%x   ", (meta->hlba>>32),meta->hlba);
		ftl_core_trace(LOG_DEBUG, 0xfd37, "NS ns_lbaa2: H:0x%x  L:0x%x   ", (temp_meta.hlba>>32),temp_meta.hlba);
		if(ns_array_menu->sec_order[lda_secid1]==drive_total_sector){
			ftl_core_trace(LOG_INFO, 0x67bb, "NS ns_lbaa1: H:0x%x  L:0x%x   ", (ns_lba_temp>>32),ns_lba_temp);
			ftl_core_trace(LOG_INFO, 0xb802, " lda-secid1:%d " ,lda_secid1);
			ftl_core_trace(LOG_INFO, 0x2c1d, "  lda-secid2:%d " ,ns_array_menu->sec_order[lda_secid1]);
			//ftl_core_trace(LOG_INFO, 0, "NS ns_lbaa2: H:0x%x  L:0x%x   ", (meta->hlba>>32),meta->hlba);
			ftl_core_trace(LOG_INFO, 0x99a4, "NS ns_lbaa2: H:0x%x  L:0x%x   ", (temp_meta.hlba>>32),temp_meta.hlba);
		}
	}else{
		temp_meta.hlba = (host_sec_bitz==9) ? (ns_lba_temp<<3) : ns_lba_temp;

			ftl_core_trace(LOG_DEBUG, 0x8c31, " ftl doer lda:0x%x  hlba:0x%x%x  ofst:%d" ,lda[ofst],meta->hlba>>32,meta->hlba,ofst);
	}
#else

		temp_meta.hlba = LDA_TO_LBA(cur_lda);

#endif

#ifndef LJ_Meta
		//meta->seed = meta_seed_setup(p);
		temp_meta.seed = meta_seed_setup(p);
#endif
		memcpy((void *)meta, (void *)&temp_meta, 16);  // only need 16 bytes
		ofst++;

#if defined(ENABLE_L2CACHE)
		if (dtag.b.in_ddr)
			l2cache_mem_flush((void *) meta, sizeof(io_meta_t));
#endif
	}

	return ret;
}

fast_code void ftl_core_flush_resume(u32 nsid)
{
	QSIMPLEQ_HEAD(_ftl_core_ctx_list_t, _ftl_core_ctx_t) local;

	QSIMPLEQ_MOVE(&local, &fcns[nsid]->pending);
	while (!QSIMPLEQ_EMPTY(&local)) {
		ftl_flush_data_t *fctx = (ftl_flush_data_t *) QSIMPLEQ_FIRST(&local);

		QSIMPLEQ_REMOVE_HEAD(&local, link);
		ftl_core_trace(LOG_INFO, 0x8d3e, "pop fctx:0x%x from pending", fctx);
		if (ftl_core_flush(fctx) == FTL_ERR_OK) {
			fctx->ctx.cmpl(&fctx->ctx);
			ftl_core_trace(LOG_INFO, 0xa2fa, "fctx:0x%x done", fctx);
		}
	}
}

/*!
 * @brief callback function of table segment request
 *
 * @note continue to flush table segment
 *
 * @param req		table segment write ncl request
 *
 * @return		not used
 */
slow_code void ftl_core_seg_flush_done(ncl_w_req_t *req)
{

	ftl_flush_tbl_t *ctx = (ftl_flush_tbl_t*)req->req.caller;

	ftl_core_done(req);
    #if 1
	if(ctx->seg_cnt >= ns[0].seg_end){

	}
	else{
		if((get_ncl_w_cnt() >= 8+2)&&tbl_flush_block){
			if(plp_trigger){
			}
			else{
			ftl_core_seg_flush(ctx);
			}
		}
	}
    #endif
}
/*
fast_code bool ftl_core_seg_submit(tbl_res_t *tbl)
{
	u32 i;
#ifndef LJ_Meta
	pda_t pda;
#endif
	u32 l2p_id;
	u32 dtag_base;
	u32 wr_du_cnt;
	cur_wr_pl_t *wr_pl;

	if (tbl_io_per_seg > ncl_w_reqs_free_cnt)
		return false;

	l2p_id = tbl->seg_bit * shr_l2pp_per_seg;
	dtag_base = shr_l2p_entry_start + shr_l2pp_per_seg * tbl->seg_bit;
	//tbl->io_in_res = 0;
	//tbl->io_issued = 0;

	wr_du_cnt = 0;

	wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_NRM, WPL_V_MODE, true, tbl);
	if (wr_pl == NULL)
		return false;

	for (i = 0; i < shr_l2pp_per_seg; i++, l2p_id++) {

		tbl->io_meta[i].lda = l2p_id;
		tbl->io_meta[i].hlba = LDA_TO_LBA(l2p_id);
#ifdef LJ1_WUNC
		tbl->io_meta[i].fmt = 0;
#endif
#ifndef LJ_Meta
		tbl->io_meta[i].sn = 0;
		u32 page_idx = wr_pl->cnt >> DU_CNT_SHIFT;
		u32 du_offst = wr_pl->cnt & (DU_CNT_PER_PAGE - 1);
		pda = wr_pl->pda[page_idx] + du_offst;
		//pda = ftl_remap_pda(pda);
		tbl->io_meta[i].seed_index = meta_seed_setup(pda);
#endif
		wr_pl->lda[wr_pl->cnt] = l2p_id;
		wr_pl->pl[wr_pl->cnt].pl.du_ofst = i;
		wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = tbl->meta_idx + i;
		wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | (dtag_base + i);
		wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
		wr_pl->cnt++;
		if (wr_pl->cnt == wr_pl->max_cnt) {
			//ncl_w_req_t *wreq;

			wr_du_cnt += wr_pl->cnt;
			ftl_core_submit(wr_pl, tbl, ftl_core_seg_flush_done);
			//tbl->wreq[tbl->io_issued] = wreq;
			//tbl->io_in_res++;
			//tbl->io_issued++;
			if (wr_du_cnt != shr_l2pp_per_seg) {
				wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_NRM, WPL_V_MODE, true, tbl);
				sys_assert(wr_pl);
			}
		}
	}

	//sys_assert(wr_du_cnt == shr_l2pp_per_seg);

	return true;
}
*/

slow_code bool ftl_core_seg_flush(ftl_flush_tbl_t *ctx)
{

	cur_wr_pl_t *wr_pl= NULL;
	u32 seg_end = ns[ctx->nsid - 1].seg_end;
	u8  i;//, j;
    qbt_core_ctx = &ctx->ctx;
	//ensure enough ftl res
	if((ctx->seg_cnt >= seg_end)||(tbl_flush_ctx == NULL)){

		//ftl_core_trace(LOG_INFO, 0, "l2p_seg_cnt %d", ctx->seg_cnt);

		return 0;
	}

	do {

		u32 l2p_id;
		u32 dtag_base;
		u8	wr_du_cnt = 0;
#ifndef LJ_Meta
		pda_t pda;
#endif

		tbl_flush_ctx = NULL;
		if(plp_trigger){
			//ftl_core_trace(LOG_INFO, 0, "plp seg_cnt %d", ctx->seg_cnt);
			ctx->ctx.cmpl(&ctx->ctx);
			return 1;
		}
		if(ctx->seg_cnt >= seg_end){

			//ftl_core_trace(LOG_INFO, 0, "l2p_seg_cnt %d", ctx->seg_cnt);

			break;
		}

		if(ctx->seg_cnt == 0){
			ctx->tbl_meta = 0xAB;
		}

		l2p_id = ctx->seg_cnt * shr_l2pp_per_seg;
		dtag_base = shr_l2p_entry_start + shr_l2pp_per_seg * ctx->seg_cnt;

		/*
		if(get_ncl_w_cnt()<(shr_l2pp_per_seg/ftl_core_next_write(INT_NS_ID, FTL_CORE_NRM))){

			//ftl_core_trace(LOG_INFO, 0, "[FTL]break wcnt %d,%d<%d", ctx->seg_cnt,get_ncl_w_cnt(),(shr_l2pp_per_seg/ftl_core_next_write(INT_NS_ID, FTL_CORE_NRM)));
			return 0;
		}
		*/
		for (i = 0; i < shr_l2pp_per_seg; i++, l2p_id++) {
            if (shr_qbt_prog_err)
            {
                return 0;
            }
			if (wr_pl == NULL){
				wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_NRM, WPL_V_MODE, false, NULL, NULL);
				if (wr_pl == NULL)
				{
					tbl_flush_ctx = ctx;
					evt_set_cs(evt_fc_qbt_resume, (u32)ctx, 0, CS_TASK);
					return 0;
				}
			}

#ifdef LJ1_WUNC
				//res->io_meta[i].fmt = 0;
#endif

#ifndef LJ_Meta
				//res->io_meta[i].sn = 0;
				u32 page_idx = wr_pl->cnt >> DU_CNT_SHIFT;
				u32 du_offst = wr_pl->cnt & (DU_CNT_PER_PAGE - 1);
				pda = wr_pl->pda[page_idx] + du_offst;
				//res->io_meta[i].seed_index = meta_seed_setup(pda);
#endif
                /*
				if(ctx->seg_cnt<2){
					u32 page_idx = wr_pl->cnt >> DU_CNT_SHIFT;
					u32 du_offst = wr_pl->cnt & (DU_CNT_PER_PAGE - 1);
					pda_t pda = wr_pl->pda[page_idx] + du_offst;
					ftl_core_trace(LOG_INFO, 0, "[QBT]pda:0x%x",pda);
				}
                */

				wr_pl->lda[wr_pl->cnt] = QBT_LDA;
				wr_pl->pl[wr_pl->cnt].pl.du_ofst = i;
				//wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (res->meta_idx + i + wl_pl_du_ofst);
				wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | (dtag_base + i);
				wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;

#if RAID_SUPPORT
                if(wr_pl->flags.b.parity_mix)
                    wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (raid_id_mgr.pad_meta_idx[wr_pl->stripe_id] + wr_pl->cnt);
                else
#endif
                    wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (_tbl_res[0].meta_idx + wr_pl->cnt);

				wr_pl->flags.b.ftl = 1;
				wr_pl->cnt++;
				wr_du_cnt++;

				if (wr_pl->cnt == wr_pl->max_cnt){
					ftl_core_submit(wr_pl, ctx, ftl_core_seg_flush_done);
					wr_pl = NULL;
				}
		}

		ctx->seg_cnt++;
		//qbt_seg_bit++;
		//ftl_core_trace(LOG_INFO, 0, "l2p_seg_cnt %d", ctx->seg_cnt);


		if (ctx->seg_cnt >= seg_end)
		{
			ftl_core_trace(LOG_INFO, 0xcf93, "[FTL]all seg done %d", ctx->seg_cnt);
			tbl_flush_block = false;

			ctx->ctx.cmpl(&ctx->ctx);
			return 1;
		}
	} while (1);
	return 0;
}
//slow_code void ftl_core_seg_flush_evt(u32 p0,u32 p1,u32 p2)
//{
//   sys_assert(tbl_flush_ctx);
//    sys_assert(tbl_flush_block);
//    if(get_ncl_w_cnt()>8+2){
//        ftl_core_seg_flush(tbl_flush_ctx);
//    }
//    else{
//        evt_set_cs(evt_seg_flush, 0, 0, CS_TASK);
//    }
//}


/*!
 * @brief ftl core operation to flush l2p table
 *
 * @param ctx	table flush context
 *
 * @return	return true if table was flushed
 */
fast_code bool ftl_core_flush_tbl(ftl_flush_tbl_t *ctx)
{
	//bool ret;
	u32 nsid = ctx->nsid;
	u32 start = ns[nsid - 1].seg_off;
	u32 end = ns[nsid - 1].seg_end;

	tbl_flush_block = true;
	tbl_flush_ctx   = ctx;
	shr_qbtfilldone = 0;

	//ftl_core_trace(LOG_INFO, 0, "[PBT]return res:%d",tFtlPbt.tbl_res_pool_cnt);

	ftl_core_trace(LOG_INFO, 0xbf74, "[FTL]seg_start %d, seg_end %d", start, end);
#if (FTL_L2P_SEG_BMP == mENABLE)
	if (ctx->flags.b.flush_all) {
		memset(shr_l2p_seg_dirty_bmp, 0, shr_l2p_seg_bmp_sz);
		memset(shr_l2p_seg_flush_bmp, 0xff, shr_l2p_seg_bmp_sz);
	} else {
		memset(shr_l2p_seg_dirty_bmp, 0, shr_l2p_seg_bmp_sz);
	}
#endif

	//ctx->io_cnt = 0;
	ctx->seg_cnt = 0;
	ctx->next_bit = start;

	if(plp_trigger){
		ctx->ctx.cmpl(&ctx->ctx);
	}
	else{
		ftl_core_seg_flush(ctx);
	}
	//if(qbt_seg_bit >= ns[0].seg_end){
		//ctx->ctx.cmpl(&ctx->ctx);
		//return true;
	//}

	return false;
}

/*!
 * @brief callback function when misc(vcnt/vbmp) data was flushed
 *
 * @note continue to flush misc
 *
 * @param req		ncl write request
 *
 * @return		not used
 */
slow_code void ftl_core_flush_misc_done(ncl_w_req_t *req)
{
	tbl_res_t *res = (tbl_res_t *) req->req.caller;
	ftl_flush_misc_t *ctx = (ftl_flush_misc_t *) res->caller;


	if(ctx->idx >= ctx->seg_cnt){

	}
	else{
		if((get_ncl_w_cnt()>=(RDISK_NCL_W_REQ_CNT/8))){
			ftl_core_flush_misc(ctx);
		}
	}
}

/*!
 * @brief ftl core operation to flush misc (valid count and valid bitmap)
 *
 * @param ctx	ftl flush misc context
 *
 * @return	return true if completed
 */
slow_code bool ftl_core_flush_misc(ftl_flush_misc_t *ctx)
{
	u32 dtag_off;
	u8  dtag_sec;
	u8  cur_seg_cnt = 0;
	u16 misc_du_cnt = 0;
	cur_wr_pl_t *wr_pl= NULL;

    qbt_core_ctx = &ctx->ctx;
	if (ctx->idx >= ctx->seg_cnt)
		return 0;

	do{
		u16 du_per_seg;
		u32 dtag;
		//lda_t loff;

		if(plp_trigger){
			ctx->ctx.cmpl(&ctx->ctx);
			return 0;
		}

		if(ctx->idx >= ctx->seg_cnt)
		{
			ftl_core_trace(LOG_INFO, 0xa1dd, "misc_seg_cnt %d", ctx->idx);
			break;
		}

			if(get_ncl_w_cnt()<(shr_l2pp_per_seg/ftl_core_next_write(INT_NS_ID, FTL_CORE_NRM))){
				return 0;
			}
		if(ctx->idx > 0)
		{
			cur_seg_cnt = 1;
		}

		for (du_per_seg = 0; du_per_seg < shr_l2pp_per_seg; du_per_seg++) {

			if(misc_du_cnt == (ctx->dtag_cnt[0]+ctx->dtag_cnt[1]))
			{
				ftl_core_trace(LOG_INFO, 0x1330, "misc_du_cnt:%d", misc_du_cnt);
				break;
			}

			if (wr_pl == NULL){
				wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_NRM, WPL_V_MODE, true, NULL, NULL);
				if (wr_pl == NULL)
				{

					evt_set_cs(evt_fc_misc_resume, (u32)ctx, 0, CS_TASK);
					return 0;
				}
			}

			if(cur_seg_cnt == 0)
			{
			dtag_sec = ((du_per_seg+1) > ctx->dtag_cnt[0])? 1 : 0;
			dtag_off = (dtag_sec > 0)? (du_per_seg - ctx->dtag_cnt[0]) : du_per_seg;
			}
			else
			{
				dtag_sec = 1;
				dtag_off = shr_l2pp_per_seg - ctx->dtag_cnt[0] + du_per_seg;
			}
	        dtag = ctx->dtag_start[dtag_sec] + dtag_off;
			//loff = (ctx->seg_start * shr_l2pp_per_seg);

			if(dtag_sec == 0)
            {
				wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
				wr_pl->pl[wr_pl->cnt].pl.dtag = dtag;

				ftl_core_trace(LOG_INFO, 0xcf56, "vac dtag_sec %d dtag_off %d dtag 0x%x", dtag_sec, dtag_off, dtag);
				ftl_core_trace(LOG_INFO, 0xbef2, "pda:0x%x", wr_pl->pda[0]);

			}
			else
			{
				wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
				wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | dtag;
                wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (_tbl_res[0].meta_idx + wr_pl->cnt);

				ftl_core_trace(LOG_INFO, 0xafcf, "blklist dtag_sec %d dtag_off %d dtag 0x%x", dtag_sec, dtag_off, wr_pl->pl[wr_pl->cnt].pl.dtag);
				ftl_core_trace(LOG_INFO, 0x0cc5, "pda:0x%x", wr_pl->pda[0]);

			}
			wr_pl->lda[wr_pl->cnt] = QBT_LDA;
			wr_pl->pl[wr_pl->cnt].pl.du_ofst = du_per_seg;
			//wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = meta_idx;
			wr_pl->flags.b.ftl = 1;
			wr_pl->cnt++;

			if (wr_pl->cnt == wr_pl->max_cnt) {
				ftl_core_submit(wr_pl, ctx, NULL);
				wr_pl = NULL;
			}
			misc_du_cnt++;
		}
		ctx->idx++;
		//qbt_seg_bit++;

		if (ctx->idx >= ctx->seg_cnt)
		{
			ftl_core_trace(LOG_INFO, 0x23e2, "[FTL]misc seg done %d", ctx->idx);
			ctx->ctx.cmpl(&ctx->ctx);
			return 1;
		}

	}while (1);
	return 0;
}

/*!
 * @brief ftl core operation to flush misc (valid count and valid bitmap)
 *
 * @param ctx	ftl flush misc context
 *
 * @return	return true if completed
 */
 /*
slow_code bool ftl_core_flush_misc(ftl_flush_misc_t *ctx)
{
	u32 dtag_off;
	u8  dtag_sec;
	tbl_res_t *res;

	if (ctx->idx >= ctx->seg_cnt)
		return true;

	do{
	//for (i = ctx->idx; i < ctx->seg_cnt; i++) {
		u32 du_per_seg;
		u32 dtag;
		lda_t loff;
		u32 meta_idx;
		io_meta_t *meta;
		cur_wr_pl_t *wr_pl;
		u8  du_cnt = 0;
		u16 dtag_temp_cnt = ctx->dtag_cnt[0];
		u8  res_idx = 0;
		u8  wl_pl_du_ofst = 0;
		u32 misc_seg_cnt = ctx->idx;

		if(misc_seg_cnt >= ctx->seg_cnt)
		{
			ftl_core_trace(LOG_ERR, 0, "misc_seg_cnt %d\n", misc_seg_cnt);
			break;
		}

		wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_NRM, WPL_V_MODE, false, NULL);

#if skip_mode
two_seg:
#endif
		if(misc_seg_cnt >= ctx->seg_cnt)
		{
			if(res_idx == 1)
			{
				ftl_core_trace(LOG_ERR, 0, "fill 2 pln dummy end pda 0x%x \n", wr_pl->pda[1]);
			}
		}
		res = ftl_get_tbl_res();
		if (res == NULL)
			break;

		res->seg_bit = misc_seg_cnt + ctx->seg_start;
		ftl_core_trace(LOG_ERR, 0, "seg_bit %d \n", res->seg_bit);
		res->caller = ctx;
		meta = res->io_meta;
		meta_idx = res->meta_idx;

		dtag_sec = (misc_seg_cnt*12 > ctx->dtag_cnt[0])? 1 : 0;
		if(ctx->dtag_cnt[0] < 12)
		{
			dtag_temp_cnt = 12;
		}
		//loff = res->seg_bit << shr_l2pp_per_seg_shf;
		dtag_off = (ctx->dtag_cnt[dtag_sec] > 12)?  ((misc_seg_cnt*12) - dtag_temp_cnt) : 0;
		loff = (res->seg_bit * 12);
		//dtag = ctx->dtag_start + (i << shr_l2pp_per_seg_shf);
        dtag = ctx->dtag_start[dtag_sec] + dtag_off;

#if(QBT_TLC_MODE_DBG == mENABLE)
		if((misc_seg_cnt*12) >= (ctx->dtag_cnt[0]))
		{
			ftl_core_trace(LOG_ERR, 0, "blklist start dtag_idx 0x%x\n", dtag);
		}
        else
		{
			ftl_core_trace(LOG_ERR, 0, "vcnt start dtag_idx 0x%x\n", dtag);
		}
#endif

#if skip_mode
		if(wr_pl->flags.b.one_pln == 0)
		{
			for (du_per_seg = 0; du_per_seg < shr_l2pp_per_seg; du_per_seg++, dtag++, loff++) {
#ifndef LJ_Meta
				pda_t pda;
				pda = wr_pl->pda[wr_pl->cnt >> DU_CNT_SHIFT] + (wr_pl->cnt & (DU_CNT_PER_PAGE - 1));
				meta->seed_index = meta_seed_setup(pda);
				meta->sn = 0; // todo;
#endif
#ifdef LJ1_WUNC
				meta->fmt = 0;
#endif
				meta->lda = loff;
				meta->hlba = LDA_TO_LBA(loff);
	            if(dtag_sec == 0)
	            {
					wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
					wr_pl->pl[wr_pl->cnt].pl.dtag = dtag;
				}
				else
				{
					wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
					wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | dtag;
				}
				wr_pl->lda[wr_pl->cnt] = loff;
				wr_pl->pl[wr_pl->cnt].pl.du_ofst = (du_per_seg + wl_pl_du_ofst);
				wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = meta_idx;
				wr_pl->cnt++;
				meta++;
				meta_idx++;
				du_cnt++;

				if (wr_pl->cnt == wr_pl->max_cnt) {

					if((du_cnt%shr_l2pp_per_seg) == 0)
					{
						misc_restbl_tmp[res_idx] = res;
						if((du_cnt == (shr_l2pp_per_seg*QBT_SKIP_MODE_2SEG)) && (res_idx == 1))
						{
							u8  res_loop;
					        for (res_loop = 0; res_loop < QBT_SKIP_MODE_2SEG; res_loop++)
							{
								res->restbl[res_loop] = misc_restbl_tmp[res_loop];
								misc_restbl_tmp[res_loop] = NULL;
							}
						}
					}
					ftl_core_submit(wr_pl, res, ftl_core_flush_misc_done);
					if (du_cnt != (shr_l2pp_per_seg * QBT_SKIP_MODE_2SEG)) {
						wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_NRM, WPL_V_MODE, false, NULL);
						sys_assert(wr_pl);
					}

					if((du_cnt == shr_l2pp_per_seg) && (res_idx == 0))
					{
						res_idx = 1;
						misc_seg_cnt++;
						ctx->idx++;
						wl_pl_du_ofst = du_cnt;
						goto two_seg;
					}
				}
			}
		ctx->idx++;
		ctx->io_cnt+=2;
		}
		else
#endif
		{
			for (du_per_seg = 0; du_per_seg < shr_l2pp_per_seg; du_per_seg++, dtag++, loff++) {
#ifndef LJ_Meta
				pda_t pda;
				pda = wr_pl->pda[wr_pl->cnt >> DU_CNT_SHIFT] + (wr_pl->cnt & (DU_CNT_PER_PAGE - 1));
				meta->seed_index = meta_seed_setup(pda);
				meta->sn = 0; // todo;
#endif
#ifdef LJ1_WUNC
				meta->fmt = 0;
#endif
				meta->lda = loff;
				meta->hlba = LDA_TO_LBA(loff);
	            if(dtag_sec == 0)
	            {
					wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
					wr_pl->pl[wr_pl->cnt].pl.dtag = dtag;
				}
				else
				{
					wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
					wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | dtag;
				}
				wr_pl->lda[wr_pl->cnt] = loff;
				wr_pl->pl[wr_pl->cnt].pl.du_ofst = du_per_seg;
				wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = meta_idx;
				wr_pl->cnt++;
				meta++;
				meta_idx++;
				if (wr_pl->cnt == wr_pl->max_cnt) {
					du_cnt += wr_pl->cnt;
					ftl_core_submit(wr_pl, res, ftl_core_flush_misc_done);
					if (du_cnt != shr_l2pp_per_seg) {
						wr_pl = ftl_core_get_wpl_mix_mode(INT_NS_ID, FTL_CORE_NRM, WPL_V_MODE, false, NULL);
						sys_assert(wr_pl);
					}
				}
			}
		ctx->idx++;
		ctx->io_cnt++;
		}
	}while (ctx->io_cnt < MAX_FTL_TBL_IO);
	return false;
}
*/

share_data_zi volatile ftl_flush_misc_t *flush_blklist;
fast_data u8 host_flush_blklist_done[2];
fast_data bool blklist_log = true;
fast_code bool ftl_core_flush_blklist_need_resume(void){
	u8 type_idx = 0;
	bool ret = false;
	for(type_idx = 0; type_idx<2; type_idx++){
		pstream_t *ps_tmp = &fcns[HOST_NS_ID]->ftl_core[type_idx]->ps;
		if(ps_tmp->flags.b.flush_blklist_start == 1){
			if(ps_tmp->flags.b.flush_blklist_suspend){
				//ftl_core_trace(LOG_INFO, 0, "resume_need_wcnt:%d", get_ncl_w_cnt());
				if(get_ncl_w_cnt() > 8+6){
					if(blklist_log)
						ftl_core_trace(LOG_INFO, 0x72dd, "resume:%d suspend", ps_tmp->type);
					if(plp_trigger)
						blklist_log = false;
					ps_tmp->flags.b.flush_blklist_suspend = 0;
					ftl_core_flush_blklist_porc(ps_tmp->type);
				}
			}
			ret = true;
		}
	}
	return ret;
}

slow_code void ftl_core_return_blklist_ctx(u16 type){
	if (host_flush_blklist_done[type]) { //make sure flush block list all complete
		ftl_flush_misc_t *ctx = blist_flush_ctx[type];
		host_flush_blklist_done[type] = 0;
        ftl_core_trace(LOG_INFO, 0x205d, "blk %d(%d) flush blklist done, record pbt seg: %u",ps_open[ctx->type],ctx->type, seg_w_blist[ctx->type]);
		ctx->ctx.cmpl(&ctx->ctx);
		blist_flush_ctx[type]		  = NULL;
	}
}

slow_code void ftl_core_flush_blklist_done(ncl_w_req_t *req){
	ftl_flush_misc_t *ctx = (ftl_flush_misc_t*)req->req.caller;
	u8 ttl_cnt = ctx->dtag_cnt[0] + ctx->dtag_cnt[1];
	ctx->io_cnt-- ;
	if (ctx->idx >=  ttl_cnt && host_flush_blklist_done[ctx->type]) {
		host_flush_blklist_done[ctx->type] = 0;
        ftl_core_trace(LOG_INFO, 0xdec5, "blk %d(%d) flush blklist done, record pbt seg: %u",ps_open[ctx->type],ctx->type, seg_w_blist[ctx->type]);
		ctx->ctx.cmpl(&ctx->ctx);
		blist_flush_ctx[ctx->type]		   = NULL;
	}
	if(ctx->type == FTL_CORE_GC){
		ftl_core_gc_wr_done(req);
	}else{
		ftl_core_done(req);
	}
}


fast_code bool ftl_core_flush_blklist_porc(u16 type){
	u8 dummy_flag = 0;
	//u16 type = ctx->type;
	cur_wr_pl_t *wr_pl = NULL;
	pstream_t *ps = &fcns[1]->ftl_core[type]->ps;
	if(blist_flush_ctx[type]==NULL){
		//wait blist ctx ready
		ps->flags.b.flush_blklist_suspend = 1;
		//ftl_core_trace(LOG_INFO, 0, "break1 %d/",type);
		return 0;
	}
	ftl_flush_misc_t *ctx = blist_flush_ctx[type];
	//blist_flush_ctx[type] = ctx;
	u8 nsid = ctx->nsid;	
	u8 ttl_cnt = ctx->dtag_cnt[0] + ctx->dtag_cnt[1];
	if (ctx->idx >= ttl_cnt){

		return 1;
	}

	if(get_ncl_w_cnt()< 8+6){
		//ftl_core_trace(LOG_INFO, 0, "too few wreq break %d", get_ncl_w_cnt());
		ps->flags.b.flush_blklist_suspend = 1;
		return 0;
	}

	//ftl_core_trace(LOG_INFO, 0, "flush nisd:%d, type:%d",nsid,type);

#ifdef While_break
	u64 start = get_tsc_64();
#endif
	bool trigger_close_gc = false;
	if(type == FTL_CORE_NRM)
		trigger_close_gc = true;

	while(1){
		if (wr_pl == NULL){
			blist_getwpl = true;
			wr_pl = ftl_core_get_wpl_mix_mode(nsid, type, WPL_V_MODE, false, NULL, NULL);
			blist_getwpl = false;
			if (wr_pl == NULL)
			{
				//ftl_core_trace(LOG_INFO, 0, "break2 %d/%d",ctx->idx,ctx->seg_cnt);
				ps->flags.b.flush_blklist_suspend = 1;
				return 0;
			}
		}


		if(ctx->idx == 0 && wr_pl->cnt != 0){
			//ftl_core_trace(LOG_INFO, 0, "set dummy flag1");
			dummy_flag = 1;
			//wr_pl->flags.b.dummy = 1;
		}

		if(ctx->idx == ttl_cnt && (wr_pl->cnt != wr_pl->max_cnt))
		{
			//ftl_core_trace(LOG_INFO, 0, "set dummy flag2");
			wr_pl->flags.b.flush_blklist  = 1;
			dummy_flag = 1;
		}

		if(dummy_flag)
		{
			wr_pl->lda[wr_pl->cnt] = INV_LDA;
			wr_pl->pl[wr_pl->cnt].pl.dtag = WVTAG_ID;
			wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			//ftl_core_trace(LOG_INFO, 0, "dummy(%d-%d)",ctx->idx,wr_pl->cnt);
#ifdef SKIP_MODE
			if(t_cpage[ctx->nsid][ctx->type] == t_tpage[ctx->nsid][ctx->type])
			{
				if (wr_pl->cnt == wr_pl->max_cnt - 1){
					dummy_flag = 0;
				}
			}
#else
			if(wr_pl->flags.b.pln_st == 0){
				//if(t_cpage[ctx->nsid][ctx->type] %2 == 0){
				if(t_cpage[ctx->nsid][ctx->type] == t_tpage[ctx->nsid][ctx->type])
				{

					//ftl_core_trace(LOG_INFO, 0, "wcnt :%d/%d",wr_pl->cnt,wr_pl->max_cnt - 1);
					if (wr_pl->cnt == wr_pl->max_cnt - 1){
						dummy_flag = 0;
					}
				}
			}
			else
			{
				//ftl_core_trace(LOG_INFO, 0, "wcnt :%d/%d",wr_pl->cnt,wr_pl->max_cnt - 1);
				if(t_cpage[ctx->nsid][ctx->type] == t_tpage[ctx->nsid][ctx->type])
				{
					if (wr_pl->cnt == wr_pl->max_cnt - 1){
						dummy_flag = 0;
					}
				}
			}
#endif
		}

		else if(ctx->idx < ctx->dtag_cnt[1]){
			wr_pl->lda[wr_pl->cnt] = BLIST_LDA;
			wr_pl->pl[wr_pl->cnt].pl.du_ofst = ctx->idx;
			wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			//wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = meta_ofst + ctx->idx;
			//wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (_tbl_res[FTL_TBL_DDR].meta_idx + ctx->idx);
			wr_pl->pl[wr_pl->cnt].pl.dtag = (ctx->dtag_start[1] +  ctx->idx);
			wr_pl->flags.b.flush_blklist  = 1;
			//ftl_core_trace(LOG_INFO, 0, "meta set sram:0x%x(%d-%d)",wr_pl->pl[wr_pl->cnt].pl.dtag,(ctx->idx),wr_pl->cnt);
			ctx->idx++;
			ctx->blklist_dtag_cnt++;
		}

		else if(ctx->idx < ctx->dtag_cnt[0] + ctx->dtag_cnt[1]){
        	wr_pl->lda[wr_pl->cnt] = BLIST_LDA;
			wr_pl->pl[wr_pl->cnt].pl.du_ofst = ctx->idx;
			wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			//wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = meta_ofst + ctx->idx;
			//wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (_tbl_res[meta_ofst].meta_idx + ctx->idx);
			wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | (ctx->dtag_start[0] + (ctx->idx - ctx->dtag_cnt[1]));
			wr_pl->flags.b.flush_blklist  = 1;
			//ftl_core_trace(LOG_INFO, 0, "meta set ddr:0x%x(%d(%d)-%d)",wr_pl->pl[wr_pl->cnt].pl.dtag,ctx->idx,(ctx->idx - ctx->dtag_cnt[1]),wr_pl->cnt);
			ctx->idx++;
			ctx->blklist_dtag_cnt++;
		}
		else{
			sys_assert(0);
		}

		//ftl_core_trace(LOG_INFO, 0, "page:(%d/%d)",t_cpage[ctx->nsid][ctx->type],t_tpage[ctx->nsid][ctx->type]);

		wr_pl->cnt++;
		if (wr_pl->cnt == wr_pl->max_cnt) {
			ctx->io_cnt++;
			ftl_core_submit(wr_pl, ctx, ftl_core_flush_blklist_done);
			//ctx->blklist_dtag_cnt = 0;
			//ftl_core_trace(LOG_INFO, 0, "submit:%d, idx:%d",ctx->io_cnt,ctx->idx);
			if(ctx->idx >= ttl_cnt && dummy_flag == 0)
			//if((ctx->io_cnt == (nand_info.bit_per_cell * nand_info.geo.nr_planes)) && (dummy_flag == 0))  // NG
			{
				break;
			}
			wr_pl = NULL;
			if(trigger_close_gc && blist_flush_ctx[FTL_CORE_GC] == NULL)
			{
				ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_GC, ftl_core_gc_wr_done);//for gc open die update l2p 
				trigger_close_gc = false;
			}
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

	if (ctx->idx >=  ttl_cnt) {
		pstream_t *ps = &fcns[nsid]->ftl_core[type]->ps;
		ps->flags.b.flush_blklist_start = 0;
		//ftl_core_trace(LOG_INFO, 0, "seg flush done:%d, idx:%d",ctx->io_cnt,ctx->idx);
		host_flush_blklist_done[ctx->type]  = true;
	}
	return 1;
}

slow_code bool ftl_core_flush_blklist(ftl_flush_misc_t *ctx){
	blist_flush_ctx[ctx->type] = ctx;
	return 0;
}

fast_code bool ftl_core_open(u32 nsid)
{
	if (fcns[nsid]->flags.b.clean) {
		if (fcns[nsid]->flags.b.opening == 0) {
			fcns[nsid]->flags.b.opening = 1;
			ftl_open(nsid);
		}
		return false;
	}
	fcns[nsid]->flags.b.opening = 0;
	return true;
}

fast_code bool ftl_core_nrm_bypass(u32 nsid)
{
	return fcns[nsid]->ftl_core[FTL_CORE_NRM]->err.b.prog_err_blk_in;
}

fast_code ALWAYS_INLINE bool ftl_core_gc_bypass(u32 nsid)	// Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
{
	return fcns[nsid]->ftl_core[FTL_CORE_GC]->err.b.prog_err_blk_in;
}

fast_code void ftl_core_open_done(u32 nsid)
{
	fcns[nsid]->flags.b.clean = 0;
	fcns[nsid]->flags.b.opening = 0;
	ftl_core_trace(LOG_INFO, 0xd7d4, "ftl_core_open done clean bit %d", fcns[nsid]->flags.b.clean);
	if (nsid == HOST_NS_ID) {
		ftl_core_gc_pend_out();
		if (fcns[nsid]->format_ctx) {
			ftl_format_t *ctx = fcns[nsid]->format_ctx;

			fcns[nsid]->format_ctx = NULL;
			ftl_core_format(ctx);
		}
	}
}

fast_code void ftl_core_clean(u32 nsid)
{
	fcns[nsid]->flags.b.clean = 1;
}

fast_code void ftl_core_update_fence(ftl_fence_t *fence)
{
	u32 i, j;
	aspb_t *aspb;
	pstream_t *ps;
	spb_fence_t *spb_fence;

	for (i = 0; i < FTL_CORE_MAX; i++) {
		ps = &fcns[fence->nsid]->ftl_core[i]->ps;

		for (j = 0; j < MAX_ASPB_IN_PS; j++) {
			aspb = &ps->aspb[j];
			spb_fence = &fence->spb_fence[i][j];
			spb_fence->flags.all = 0;

			if (aspb->flags.b.valid) {
				spb_fence->spb_id = aspb->spb_id;
				spb_fence->ptr = aspb->cmpl_ptr;
				spb_fence->sn = aspb->sn;

				spb_fence->flags.b.valid = true;
				spb_fence->flags.b.slc = aspb->flags.b.slc;
				if (aspb->cmpl_cnt == aspb->max_ptr)
					spb_fence->flags.b.closed = true;
			} else {
				spb_fence->spb_id = INV_SPB_ID;
			}
		}
	}
}

init_code void ftl_core_restore_rt_flags(spb_rt_flags_t *src)
{
	u32 sz = sizeof(spb_rt_flags_t) * spb_total;

	memcpy(spb_rt_flags, src, sz);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	spb_rt_flags[PLP_SLC_BUFFER_BLK_ID].b.type = 1;
#endif
}

init_code void ftl_core_restore_fence(ftl_fence_t *fence)
{
	u32 i;

	for (i = 0; i < FTL_CORE_MAX; i++) {
		pstream_t *ps = &fcns[fence->nsid]->ftl_core[i]->ps;
		u32 j;

		for (j = 0; j < MAX_ASPB_IN_PS; j++)
			pstream_restore(ps, j,  &fence->spb_fence[i][j]);

		if (fence->nsid == INT_NS_ID)
			break;
	}

	if (fence->flags.b.clean)
		ftl_core_clean(fence->nsid);
}

fast_code stripe_user_t* ftl_core_get_su(u32 nsid, u32 type)
{
	return &fcns[nsid]->ftl_core[type]->su;
}


slow_code bool stripe_in_fill_dummy(cwl_stripe_t *stripe, ftl_spb_pad_t *spb_pad)
{
	if ((stripe->nsid >= spb_pad->param.start_nsid) && (stripe->nsid <= spb_pad->param.end_nsid) \
		&& (stripe->type >= spb_pad->param.start_type) && (stripe->type <= spb_pad->param.end_type)) {
		return true;
	}
	return false;
}

slow_code void ftl_core_parity_flush(u32 stripe_id)
{ 
	ncl_w_req_t *req; 
	cwl_stripe_t *stripe = get_stripe_by_id(stripe_id); 
	pstream_t *ps = &fcns[stripe->nsid]->ftl_core[stripe->type]->ps; 
	core_wl_t *cwl = &fcns[stripe->nsid]->ftl_core[stripe->type]->cwl; 
	stripe_user_t *su = &fcns[stripe->nsid]->ftl_core[stripe->type]->su;

	if (stripe->ncl_req == NULL) { 
		stripe->ncl_req = ftl_core_get_parity_req(stripe->nsid, stripe->type); 
		QSIMPLEQ_INSERT_TAIL(&su->wait_que, su->active, link); 
		cwl->rid[stripe->ncl_req->req.die_id] = CWL_FREE_RID; 
		su->active = NULL; 
	}

	req = stripe->ncl_req;
	//ftl_core_trace(LOG_ALW, 0, "flush parity:0x%x", req);

	/* new req should attach to spb_pad */
	if (open_ctrl.free_cnt != MAX_SPBPAD_CNT) {
		for (u8 i = 0; i < MAX_SPBPAD_CNT; ++i) {
			ftl_spb_pad_t *cur = &open_ctrl.spb_pad[i];
			if (is_spb_pad_allocated(cur)) {
				if (stripe_in_fill_dummy(stripe, cur)) {
					if (req->req.caller != NULL) {
						ftl_core_trace(LOG_ALW, 0x8f9f, "attach req cnt:%d, spb_pad:0x%x, caller:0x%x", cur->dummy_req_cnt, cur, req->req.caller);
					}
					sys_assert(req->req.caller == NULL);
					//ftl_core_trace(LOG_ALW, 0, "flush parity:0x%x to 0x%x, req:%d, done:%d", req, cur, cur->dummy_req_cnt, cur->dummy_done_cnt);
					req->req.caller = cur;
					cur->dummy_req_cnt++;
					break;
				}
			}
		}
	}

	/*
	 * Sometimes ftl_core_parity_flush() may be earlier than ftl_core_get_wpl_mix_mode(), then req->req.tpage will be set to 3,
	 * and fw will hit assert(req->req.cpage < req->req.tpage) (cpage==5 && tpage==3).
	 * So we pick out such a situation and force to get a new cwl in ftl_core_get_wpl_mix_mode(), remember to clear cwl->rid.
	 *
	 */

	if (req->req.cpage != req->req.tpage) {  // cpage==5 && tpage==6
		cwl->cur.mpl_qt = 0;
		cwl->rid[req->req.die_id] = CWL_FREE_RID;
		su->active = NULL;
	}

    if (req->req.nsid != INT_NS_ID) {
        bool ret;
        CBF_INS(&wreq_seq_que[stripe->type], ret, (u32)stripe->ncl_req);
        sys_assert(ret);
    } else {	// ISU, PBTNotOpen
		req->req.op_type.b.ftl = 1;	// Notice ErrHandle this is ftl spb.
	}

	QSIMPLEQ_REMOVE(&su->wait_que, stripe, _cwl_stripe_t, link);
	QSIMPLEQ_INSERT_TAIL(&su->cmpl_que, stripe, link);
	pstream_rsvd(ps);

	//req->req.caller = stripe;
	req->req.cmpl = ftl_core_done;
	req->req.cnt = stripe->page_cnt;

    if (req->req.tpage == nand_plane_num() * nand_info.bit_per_cell) {
        req->req.tpage = nand_info.bit_per_cell;
        req->req.mp_du_cnt *= nand_plane_num();
    }
	req->req.op_type.b.host = 0; // parity is internal data, host flag can't be set
#ifdef SKIP_MODE
		req->req.op_type.b.slc = ftl_core_get_spb_type(nal_pda_get_block_id(req->w.pda[0]));
#else
		req->req.op_type.b.slc = ftl_core_get_spb_type(nal_pda_get_block_id(req->w.l2p_pda[0]));
#endif

	set_ncl_req_raid_info(req, stripe, 0, 0);
	//if(req->req.nsid == INT_NS_ID)
	//ftl_core_trace(LOG_INFO, 0, "stripe %d flush parity, wr %d", stripe_id, stripe->fly_wr);
	raid_sched_push(req);
}

fast_code void ftl_core_pout_and_parity_flush(u32 stripe_id)
{ 
    cwl_stripe_t *stripe = get_stripe_by_id(stripe_id);
    if(stripe->parity_mix){
        sys_assert(stripe->parity_req != NULL);
        ncl_w_req_t *parity_req = stripe->parity_req;
        wr_pout_issue(parity_req);
    }else{
        ftl_core_parity_flush(stripe_id);
    }
}

#if 0 //def Dynamic_OP_En
void DYOP_FRB_erase()
{
	///////////////////////////
	//frb_pb_erase(_frb_log.log.cur_blk, NULL);
	//frb_pb_erase(_frb_log.log.next_blk, NULL);
	srb_t *srb = (srb_t *) SRAM_BASE;
	pda_t pda_list[2];

	pda_list[0] = nal_rda_to_pda(&srb->ftlb_pri_pos);
	pda_list[1] = nal_rda_to_pda(&srb->ftlb_sec_pos);

#if FRB_remap_enable
	extern epm_info_t* shr_epm_info;
	epm_remap_tbl_t *epm_remap_tbl;

	epm_remap_tbl = (epm_remap_tbl_t *)ddtag2mem(shr_epm_info->epm_remap_tbl_info_ddtag);

	if( epm_remap_tbl->frb_remap[0] != 0xFFFFFFFF )
	{
		pda_list[0] = epm_remap_tbl->frb_remap[0];
		ftl_core_trace(LOG_ALW, 0x038d, "FRBremap pda_list[0]  0x%x , frb_remap[0] 0x%x",pda_list[0] , epm_remap_tbl->frb_remap[0]);
	}

	if(epm_remap_tbl->frb_remap[1] != 0xFFFFFFFF)
	{
		pda_list[1] = epm_remap_tbl->frb_remap[1];
		ftl_core_trace(LOG_ALW, 0x7099, "FRBremap pda_list[1]  0x%x , frb_remap[1] 0x%x", pda_list[1] , epm_remap_tbl->frb_remap[1]);
	}
#endif

	int i;
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info_list[32];

	ncl_cmd.addr_param.common_param.info_list = info_list;
	ncl_cmd.addr_param.common_param.list_len = 2;
	ncl_cmd.addr_param.common_param.pda_list = pda_list;

	memset(info_list, 0, sizeof(struct info_param_t) * 1);

	for (i = 0; i < 2; i++)
		info_list[i].pb_type = NAL_PB_TYPE_SLC;

	ncl_cmd.op_type = 0;

	ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_SYNC_FLAG ;
	ncl_cmd.du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd.op_code = NCL_CMD_OP_ERASE;
	ncl_cmd.user_tag_list = NULL;
	ncl_cmd.completion = NULL;
	ncl_cmd.caller_priv = NULL;
	ncl_cmd.status = 0;
	ncl_cmd_submit(&ncl_cmd);
}
#endif

#ifdef Dynamic_OP_En
extern u8 DYOP_FRB_Erase_flag;
#endif

//fast_code void gc_action_done(gc_action_t *action)
//{
//	ftl_core_trace(LOG_ALW, 0, "uart GC action");
//	sys_free(FAST_DATA, action);
//}

#if 1
fast_code void ftl_format_done(format_ctx_t *format_ctx)
{
	ftl_format_t *format = format_ctx->caller;

	shr_format_fulltrim_flag = false;
	ftl_core_trace(LOG_ERR, 0xe572, "[FTL]format_fulltrim ok open new blk %d", shr_format_fulltrim_flag);

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(epm_ftl_data->slc_format_tag == SLC_ERASE_FORMAT_TAG)//slc erase when preformat
	{	
		ftl_core_trace(LOG_ERR, 0x6ed5, "[SLC] slc has been erase when preformat");
		SLC_init = false;
		ftl_slc_pstream_enable();
		epm_ftl_data->slc_format_tag = 0;
	}
#endif


#if (PBT_OP == mENABLE)
	ftl_set_pbt_halt(false);
#endif


    #if 0 // marked by Curry, for after preformat don't open new blk
	ftl_core_start(format->nsid);
    #endif

	//ftl_core_start(INT_NS_ID);

	sys_free(FAST_DATA, format_ctx);
	gGCInfo.mode.all16 = 0;
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_RESUME;
	action->caller = NULL;
	action->cmpl = NULL;//gc_action_done;

	if (gc_action(action))
	{
		sys_free(FAST_DATA, action);
	}

#if 0//def Dynamic_OP_En //move to vsc_preformat_done() for onthefly DYOP
	if(DYOP_FRB_Erase_flag == 1)
	{
		//DYOP_FRB_erase();
		DYOP_FRB_Erase_flag = 0;
	}
#endif

	format->ctx.cmpl(&format->ctx);
}

fast_code void ftl_core_format_handler(ftl_format_t *format)
{
	u32 ns_id = format->nsid;
	bool ret;

	ret = ftl_core_open(ns_id);
	if (ret == false) {
		sys_assert(fcns[ns_id]->format_ctx == NULL);
		fcns[ns_id]->format_ctx = format;
		ftl_core_trace(LOG_ERR, 0x3190, "core clean no need format\n");
		return;
	}

	ftl_core_trace(LOG_INFO, 0x2787, "ftl core format start");

	// re-init ftl_core
	u32 i;
	format_ctx_t *ctx;

	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_core_t *fc = fcns[ns_id]->ftl_core[i];
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		//when format , don't reset slc pstream . if preformat , need erase slc , and set SLC_init false;
		if(SLC_init == true && i == FTL_CORE_SLC) 
			continue;
#else
		if(ns_id == 1 && i == FTL_CORE_PBT)
			continue;
#endif

		ftl_core_trace(LOG_INFO, 0x0bb7, "ns : %d core type : %d cnt : %d", ns_id, i, fc->wreq_cnt);
		sys_assert(fc->wreq_cnt == 0);
		pstream_reset(&fc->ps);
		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
	}

	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[i];

		if (fc == NULL)
			continue;

		ftl_core_trace(LOG_INFO, 0xe19c, "ns : %d core type : %d cnt : %d", INT_NS_ID, i, fc->wreq_cnt);
		sys_assert(fc->wreq_cnt == 0);
		pstream_reset(&fc->ps);
		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
	}
	#if (PBT_OP == mENABLE)
	ftl_pbt_init(false);
	#endif
	ctx = sys_malloc(FAST_DATA, sizeof(format_ctx_t));
	sys_assert(ctx);
	ctx->all = 0;
	ctx->b.ns_id = ns_id;
	ctx->b.host_meta = format->flags.b.host_meta;
	ctx->b.preformat_erase = format->flags.b.preformat_erase;
	ctx->b.full_trim = format->flags.b.full_trim;
	ctx->b.del_ns = format->flags.b.del_ns;
	ctx->b.ins_format = 1;
	ctx->cmpl = ftl_format_done;
	ctx->caller = format;
	ftl_format(ctx);
}


fast_code void ftl_core_format_check_wreq(ftl_format_t *format)
{
    u8 i, nsid;
    if (shr_shutdownflag){
        evt_set_cs(evt_format_wreq, (u32)format, 0, CS_TASK);
        return;
    }

    //check wreq cnt == 0
    for(nsid = 1; nsid < TOTAL_NS; nsid++){
            for (i = 0; i < FTL_CORE_MAX; i++) {
            ftl_core_t *fc = fcns[nsid]->ftl_core[i];



            if(nsid == 1 && i == FTL_CORE_PBT)
                continue;



            if(nsid == INT_NS_ID && i != FTL_CORE_PBT)
                continue;


            ftl_core_trace(LOG_INFO, 0xcb8c, "ns:%u(%u), wreq_cnt:%u",nsid,i,fc->wreq_cnt);
            if (fc->wreq_cnt != 0) {
                evt_set_cs(evt_format_wreq, (u32)format, 0, CS_TASK);
                return;
            }
        }
    }
    ftl_core_format_handler(format);
}


fast_code void ftl_core_format_close_open_done(ftl_core_ctx_t *ctx)
{
    ftl_spb_pad_t *spb_pad = (ftl_spb_pad_t *)ctx;
    ftl_format_t *format = (ftl_format_t *)spb_pad->ctx.caller;  // ERRHANDLE_GLIST
    ftl_core_trace(LOG_INFO, 0x4b94, "spb_pad:0x%x, format:0x%x, nsid:%u", spb_pad, format, format->nsid);
    ftl_core_format_check_wreq(format);
}

fast_code void evt_format_wreq_chk(u32 par, u32 format, u32 p2)
{
    ftl_core_format_check_wreq((ftl_format_t *)format);
}

fast_code void evt_ftl_qbt_alloc_chk(u32 par, u32 ctx, u32 p2)
{
    ftl_core_qbt_alloc((ns_start_t *)ctx);
}

fast_code void evt_ftl_core_shutdown_chk(u32 par, u32 ctx, u32 p2)
{
    ftl_core_flush_shutdown((flush_ctx_t *)ctx);
}

fast_code void evt_ftl_qbt_resume(u32 par, u32 ctx, u32 p2)
{
    ftl_core_seg_flush((ftl_flush_tbl_t *)ctx);
}

fast_code void evt_ftl_misc_resume(u32 par, u32 ctx, u32 p2)
{
	ftl_core_flush_misc((ftl_flush_misc_t *)ctx);
}

ddr_code void ftl_core_gc_action_done(gc_action_t *action)
{
	ftl_core_trace(LOG_ALW, 0x60e7, "uart GC action");
    ftl_core_format_close_open(action->caller);
	sys_free(FAST_DATA, action);
}

fast_code void ftl_core_format_close_open(ftl_format_t *format)
{
    u8 nsid = format->nsid;
    if (nsid == HOST_NS_ID)
        fcore_fill_dummy(FTL_CORE_NRM, FTL_CORE_GC, FILL_TYPE_BLK, ftl_core_format_close_open_done, (void*)format);
    else
        ftl_core_format_handler(format);
}

fast_code void ftl_core_format(ftl_format_t *format)
{
	shr_format_fulltrim_flag = true;
#if (PBT_OP == mENABLE)
	ftl_set_pbt_halt(true);
	fcore_fill_dummy(FTL_CORE_PBT, FTL_CORE_PBT,FILL_TYPE_WL, NULL, NULL);
#endif

    gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

    action->act = GC_ACT_SUSPEND;
    action->caller = format;
    action->cmpl = ftl_core_gc_action_done;

	if (gc_action(action))
    {
    	ftl_core_trace(LOG_INFO, 0xf62d, "gc no action");
        ftl_core_format_close_open(format);
    	sys_free(FAST_DATA, action);
    }
}

ddr_code void ftl_core_reset_wait_sched_idle_done(u32 grp, ftl_reset_t *reset)
{
	//ftl_core_trace(LOG_WARNING, 0, "grp %d reset %x", grp, reset);
	reset->scheduler_idle++;
	if (reset->scheduler_cnt == reset->scheduler_idle)
	{
		//ftl_core_trace(LOG_WARNING, 0, "S %d I%x", reset->scheduler_cnt,reset->scheduler_idle);
		ftl_core_put_ctx(&reset->ctx);
		ftl_core_trace(LOG_INFO, 0x5352, "rst put ctx:0x%x, cnt:%d", reset, ftl_core_ctx_cnt);
		evt_set_cs(evt_reset_ack, 0, 0, CS_TASK);
	}
}

ddr_code void ftl_core_reset_done_notify(void)
{
	sys_assert(fc_flags.b.reseting == false);
	scheduler_resume_host_streaming_read();
}

share_data_zi volatile bool reseting;
ddr_code void ftl_core_reset_notify(void)
{
	ftl_core_ctx_t *ctx = ftl_core_get_ctx();
	ftl_reset_t *reset = (ftl_reset_t *) ctx;
	//ftl_core_trace(LOG_INFO, 0, "rst get ctx:0x%x, cnt:%d", ctx, ftl_core_ctx_cnt);

	// pcie should be reset, let's discard to host streaming read

	//sys_assert(fc_flags.b.reseting == false);
	if(fc_flags.b.reseting == true){
	    ftl_core_trace(LOG_ERR, 0x661b, "reseting err %d", fc_flags.b.reseting);
        reseting = 1;
        return;
	}
    reseting = 0;
	fc_flags.b.reset_ack = false;
	fc_flags.b.reseting = true;


	reset->scheduler_idle = 0;
	reset->scheduler_cnt = scheduler_abort_host_streaming_read(reset);
}

ddr_code void plp_cancel_die_que(void)
{
    // cpu2 start cancel die que, and then msg cpu4 to cancel its die que
	scheduler_plp_cancel_die_que(NULL);
}

#else
fast_code void ftl_format_done(format_ctx_t *format_ctx)
{
	u8 i;
	ftl_format_t *format = format_ctx->caller;

	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_core_t *fc = fcns[format->nsid]->ftl_core[i];

		sys_assert(fc->wreq_cnt == 0);
		pstream_reset(&fc->ps);
		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
	}

	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[i];

		if (fc == NULL)
			continue;

		sys_assert(fc->wreq_cnt == 0);
		pstream_reset(&fc->ps);
		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
	}

	ftl_core_start(format->nsid);
	ftl_core_start(INT_NS_ID);

	sys_free(FAST_DATA, format_ctx);

#if 0//def Dynamic_OP_En //move to vsc_preformat_done() for onthefly DYOP
	if(DYOP_FRB_Erase_flag == 1)
	{
		//DYOP_FRB_erase();
		DYOP_FRB_Erase_flag = 0;
	}
#endif

	host_du_fmt = DU_FMT_USER_4K;
#if defined(HMETA_SIZE)
	if (format->flags.b.host_meta)
		host_du_fmt = DU_FMT_USER_4K_HMETA;
#endif

	format->ctx.cmpl(&format->ctx);
}

fast_code void ftl_core_format(ftl_format_t *format)
{
	u32 ns_id = format->nsid;
	bool ret;

	ret = ftl_core_open(ns_id);
	if (ret == false) {
		sys_assert(fcns[ns_id]->format_ctx == NULL);
		fcns[ns_id]->format_ctx = format;
		ftl_core_trace(LOG_ERR, 0xe858, "core clean no need format\n");
		return;
	}

	ftl_core_trace(LOG_INFO, 0x65df, "ftl core format start");
	format_ctx_t *ctx;
	// re-init ftl_core
	/*
	u8 i;

	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_core_t *fc = fcns[ns_id]->ftl_core[i];

		sys_assert(fc->wreq_cnt == 0);
		pstream_reset(&fc->ps);
		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
	}

	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[i];

		if (fc == NULL)
			continue;

		sys_assert(fc->wreq_cnt == 0);
		pstream_reset(&fc->ps);
		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
	}
	*/
	ctx = sys_malloc(FAST_DATA, sizeof(format_ctx_t));
	sys_assert(ctx);
	ctx->all = 0;
	ctx->b.ns_id = ns_id;
	ctx->b.host_meta = format->flags.b.host_meta;
	ctx->b.preformat_erase = format->flags.b.preformat_erase;
	ctx->b.ins_format = 1;
	ctx->cmpl = ftl_format_done;
	ctx->caller = format;
	ftl_format(ctx);
}
#endif
#if PLP_DEBUG
fast_data fsm_res_t plp_debug_fill_up(fsm_ctx_t* fsm)
{
    //ipc to cpu1 to do read
    cpu_msg_issue(CPU_FE - 1, CPU_MSG_PLP_DEBUG_FILL, 0, 0);
    return FSM_PAUSE;
}

fast_data void plp_debug_fill_done(void)
{
    extern u32 cpu2_read_cnt;
    extern u32 cpu4_read_cnt;

    cpu2_read_cnt = 0;
    cpu4_read_cnt = 0;
    ftl_core_trace(LOG_INFO, 0x7706, "plp debug fill done");
    #if PLP_DEBUG_GPIO
    gpio_set_gpio15(1);
    #endif
    fsm_ctx_next(&plp_fsm.fsm);
	fsm_ctx_run(&plp_fsm.fsm);
}
#endif

#if 0
fast_code bool is_plp_fill_dummy_done(plp_spb_pad_t* spb_pad)
{
    if ((spb_pad->cur.nsid == NS_SUPPORT) && (spb_pad->cur.type > FTL_CORE_GC)){
        ftl_core_trace(LOG_DEBUG, 0xf811, "plp req done, wait ncl done")
        return true;
    }

    return false;
}

fast_code bool plp_check_fill_stop(plp_spb_pad_t* spb_pad, pstream_t *ps, bool check_block_close_only)
{
    if (is_ps_no_need_to_pad(ps)){
        //no need fill dummy
        ftl_core_trace(LOG_INFO, 0x76d1, "no need fill dummy nsid:%d type:%d", spb_pad->cur.nsid, spb_pad->cur.type);
        spb_pad->cur.type++;
        if (spb_pad->cur.type > FTL_CORE_GC){
            if (spb_pad->dummy_done_cnt == spb_pad->dummy_req_cnt){
                spb_pad->ctx.cmpl(&spb_pad->ctx);
            }
            return true;
        }
        spb_pad->cur.cwl_cnt = 0;
        return false;
    }
    else if (check_block_close_only == false){
        spb_pad->cur.cwl_cnt++;
        if (spb_pad->cur.cwl_cnt == spb_pad->param.cwl_cnt){
            ftl_core_trace(LOG_INFO, 0x28f0, "nsid:%d type:%d cwl cnt:%d done, check next", spb_pad->cur.nsid,
            spb_pad->cur.type, spb_pad->cur.cwl_cnt);
            spb_pad->cur.type++;
            if (spb_pad->cur.type > FTL_CORE_GC){
                if (spb_pad->dummy_done_cnt == spb_pad->dummy_req_cnt){
                    spb_pad->ctx.cmpl(&spb_pad->ctx);
                }
                return true;
            }
            spb_pad->cur.cwl_cnt = 0;
            return false;
        }
    }
    return false;
}

fast_code void plp_fill_dummy_done(plp_spb_pad_t* spb_pad)
{
    ftl_core_trace(LOG_DEBUG, 0x8e7a, "plp_fill dummy done nsid:%d type:%d cnt:%d done cnt:%d req cnt:%d", spb_pad->cur.nsid, spb_pad->cur.type,
        spb_pad->cur.cwl_cnt, spb_pad->dummy_done_cnt, spb_pad->dummy_req_cnt);

    spb_pad->dummy_done_cnt++;
    if (is_plp_fill_dummy_done(spb_pad)) {
        //cpl function
        if (spb_pad->dummy_done_cnt == spb_pad->dummy_req_cnt) {
            spb_pad->ctx.cmpl(&spb_pad->ctx);
        }
        return;
    }

    if (spb_pad->task_fill){
        return;
    }

    //toal 128 ncl req,make sure ncl 128 ncl_req work
    if (spb_pad->dummy_req_cnt - spb_pad->dummy_done_cnt >= 128) {
        return;
    }
    if ((get_ncl_w_cnt(0) < 5) || (get_ncl_w_cnt(4) < 2)) {
        return;
    }

    u32 pad_cnt;
    cur_wr_pl_t *cur_wr_pl;
    core_wl_t *cwl;
    pstream_t *ps;
	aspb_t *aspb;
	u32 page;

pre_check:
    cwl = &fcns[spb_pad->cur.nsid]->ftl_core[spb_pad->cur.type]->cwl;
    ps = &fcns[spb_pad->cur.nsid]->ftl_core[spb_pad->cur.type]->ps;
    aspb = &ps->aspb[ps->curr];
    page = aspb->wptr / nand_info.interleave;

    ftl_core_trace(LOG_INFO, 0x800e, "ns %d type %d spb %d page %d ptr:%d max ptr:%d",
		spb_pad->cur.nsid, spb_pad->cur.type, aspb->spb_id, page, aspb->wptr, aspb->max_ptr);
    if (is_ps_no_need_to_pad(ps) || cwl->cur.usr_mpl_qt == 0){
        if (plp_check_fill_stop(spb_pad, ps, false)) {
            return;
        }else{
            if (spb_pad->cur.cwl_cnt == 0) // cwl_cnt == 0-->ps changed
                goto pre_check;
        }
    }
    cwl = &fcns[spb_pad->cur.nsid]->ftl_core[spb_pad->cur.type]->cwl;

again:
    //cur_wr_pl = ftl_core_get_wpl(spb_pad->cur.nsid, spb_pad->cur.type);
    cur_wr_pl = ftl_core_get_wpl(spb_pad->cur.nsid, spb_pad->cur.type);
    if (cur_wr_pl == NULL){
        sys_assert(cwl->cur.usr_mpl_qt == 0);
        if (plp_check_fill_stop(spb_pad, ps, true) == false) {
            goto pre_check;
        }
        return;
    }
	pad_cnt = ftl_core_next_write(spb_pad->cur.nsid, spb_pad->cur.type) - cur_wr_pl->cnt;

	while (pad_cnt) {
		cur_wr_pl->lda[cur_wr_pl->cnt] = INV_LDA;
		cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = WVTAG_ID;
		cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
		cur_wr_pl->cnt++;
		pad_cnt--;
	}

    cur_wr_pl->flags.b.dummy = 1;
	ncl_w_req_t *req = ftl_core_submit(cur_wr_pl, (void*)spb_pad, NULL);

    if (req){
        ftl_core_trace(LOG_DEBUG, 0x3c35, "ns %d type %d page %d die:%d",
            spb_pad->cur.nsid, spb_pad->cur.type, pda2page(cur_wr_pl->pda[0]), cwl->cur.die);
        spb_pad->dummy_req_cnt++;
        if (get_ncl_w_cnt(cwl->cur.next_open) < 2) {
            return;
        }

        if (spb_pad->dummy_req_cnt - spb_pad->dummy_done_cnt >= 128) {
            return;
        }
        //check finish
        if (plp_check_fill_stop(spb_pad, ps, true)) {
            return;
        }
    }
    //check fill dummy done
	if (cwl->cur.usr_mpl_qt)
		goto again;

}

fast_code void plp_fill_dummy(plp_spb_pad_t* spb_pad)
{
    u32 pad_cnt;
    cur_wr_pl_t *cur_wr_pl;
    core_wl_t *cwl;
    pstream_t *ps;
	aspb_t *aspb;
	u32 page;

    spb_pad->private_cmpl = plp_fill_dummy_done;
    spb_pad->dummy_req_cnt = 0;
    spb_pad->dummy_done_cnt = 0;

pre_check:
    cwl = &fcns[spb_pad->cur.nsid]->ftl_core[spb_pad->cur.type]->cwl;
    ps = &fcns[spb_pad->cur.nsid]->ftl_core[spb_pad->cur.type]->ps;
    aspb = &ps->aspb[ps->curr];
    page = aspb->wptr / nand_info.interleave;

    ftl_core_trace(LOG_INFO, 0x1218, "ns %d type %d spb %d page %d ptr:%d max ptr:%d",
		spb_pad->cur.nsid, spb_pad->cur.type, aspb->spb_id, page, aspb->wptr, aspb->max_ptr);


    if (is_ps_no_need_to_pad(ps) || cwl->cur.usr_mpl_qt == 0){
        if (plp_check_fill_stop(spb_pad, ps, false)) {
            return;
        }else{
            if (spb_pad->cur.cwl_cnt == 0) // cwl_cnt == 0-->ps changed
                goto pre_check;
        }
    }
    cwl = &fcns[spb_pad->cur.nsid]->ftl_core[spb_pad->cur.type]->cwl;
    spb_pad->task_fill = true;
    again:

	//cur_wr_pl = ftl_core_get_wpl(spb_pad->cur.nsid, spb_pad->cur.type);
	cur_wr_pl = ftl_core_get_wpl(spb_pad->cur.nsid, spb_pad->cur.type);
    if (cur_wr_pl == NULL){
        sys_assert(cwl->cur.usr_mpl_qt == 0);
        if (plp_check_fill_stop(spb_pad, ps, true) == false) {
            goto pre_check;
        }else{
            spb_pad->task_fill = false;
            return;
        }
    }
	pad_cnt = ftl_core_next_write(spb_pad->cur.nsid, spb_pad->cur.type) - cur_wr_pl->cnt;

	while (pad_cnt) {
		cur_wr_pl->lda[cur_wr_pl->cnt] = INV_LDA;
		cur_wr_pl->pl[cur_wr_pl->cnt].pl.dtag = WVTAG_ID;
		cur_wr_pl->pl[cur_wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
		cur_wr_pl->cnt++;
		pad_cnt--;
	}
    cur_wr_pl->flags.b.dummy = 1;
	ncl_w_req_t *req = ftl_core_submit(cur_wr_pl, (void*)spb_pad, NULL);
	if (req){
        ftl_core_trace(LOG_DEBUG, 0x650c, "ns %d type %d page %d die:%d",
            spb_pad->cur.nsid, spb_pad->cur.type, pda2page(cur_wr_pl->pda[0]), cwl->cur.die);
        spb_pad->dummy_req_cnt++;
        if (get_ncl_w_cnt(cwl->cur.next_open) < 2) {
            spb_pad->task_fill = false;
            return;
        }

        if (spb_pad->dummy_req_cnt - spb_pad->dummy_done_cnt >= 128){
            spb_pad->task_fill = false;
            return;
        }
        //check fill dummy done
        if (plp_check_fill_stop(spb_pad, ps, true)) {
            spb_pad->task_fill = false;
            return;
        }else if (!cwl->cur.usr_mpl_qt){
            goto pre_check;
        }
    }

	if (cwl->cur.usr_mpl_qt)
		goto again;
    spb_pad->task_fill = false;
}
#endif
fast_code void plp_spb_pad_done(ftl_core_ctx_t *ctx)
{
	//ftl_core_trace(LOG_INFO, 0, "plp_spb_pad_done, time : %d", time_elapsed_in_us(plp_time_start));
    fcore_fill_dummy_done(ctx);
#if PLP_DEBUG_GPIO
    gpio_set_gpio15(1);
#endif
	plp_close_wl_done = 1;
	if(plp_save_epm_done && !pending_parity_cnt){
	fsm_ctx_next(&plp_fsm.fsm);
	fsm_ctx_run(&plp_fsm.fsm);
	}
}
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
slow_code void plp_slc_pad_done(ftl_core_ctx_t *ctx)
{
	#if(PLP_NO_DONE_DEBUG == mENABLE)
	ftl_core_trace(LOG_INFO, 0xbdbc, "plp_slc_pad_done");
	#endif
    //fcore_fill_dummy_done(ctx);
}
#endif


#if 0
fast_code fsm_res_t plp_flush_pad(fsm_ctx_t* fsm)
{
    fcore_fill_dummy(FTL_CORE_NRM, FTL_CORE_GC, FILL_TYPE_WL, plp_spb_pad_done, NULL);
    return FSM_PAUSE;
}
#endif
#if(PLP_GC_SUSPEND == mENABLE)
slow_code fsm_res_t plp_suspend_gc(fsm_ctx_t *fsm)
{
	extern volatile u8 CPU2_plp_step;
	//extern volatile bool ucache_flush_flag;
	tFtlPbt.force_pbt_halt = 1;
	ftl_core_trace(LOG_INFO, 0x7479, "suspend step:%d flush:%d",CPU2_plp_step,ucache_flush_flag);
	if(CPU2_plp_step == 0)
		CPU2_plp_step = 1;
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_SUSPEND;
	action->caller = NULL;
	action->cmpl = NULL;

	if (gc_action(action))
	{
		sys_free(FAST_DATA, action);
	}
	plp_cancel_die_que();
	/*fsm_ctx_next(fsm);
	fsm_ctx_run(fsm);*/
	return FSM_QUIT;
}
#endif
ddr_code void vu_gc_action_done(gc_action_t *action)
{
	ftl_core_trace(LOG_ALW, 0xce45, "vu GC action done");
	sys_free(FAST_DATA, action);
}


ddr_code fsm_res_t vu_control_gc(u8 gc_act)
{
	ftl_core_trace(LOG_INFO, 0xa247, "vu_control_gc, gc_act %d", gc_act);
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));
	action->act = gc_act;
	action->caller = NULL;
	action->cmpl = vu_gc_action_done;

	if(gc_action(action))
		sys_free(FAST_DATA, action);

	return FSM_QUIT;
}
fsm_res_t plp_Save_EPM(fsm_ctx_t *fsm);
fsm_res_t plp_done(fsm_ctx_t *fsm);

slow_code void flush_qbt_done(ftl_core_ctx_t *ctx)
{
	ftl_core_trace(LOG_INFO, 0xcd21, "fctx:0x%x done", ctx);
	sys_free(FAST_DATA, ctx);
}

#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
ddr_code void vc_print(void)  
{
#if 1  
	u8  fail_flag = mFALSE;
	u16 spb_id;
	//u32 cnt;  
	u32 *vc;
	u32 dtag_cnt = occupied_by((shr_nand_info.geo.nr_blocks) * sizeof(u32), DTAG_SZE);
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
	//dtag_t dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
	u32 du_per_tcl_sblock = (shr_nand_info.geo.nr_pages * shr_nand_info.interleave) << DU_CNT_SHIFT; // FTL_DU_PER_TLC_SBLOCK

    ftl_core_trace(LOG_ALW, 0x0346, "du_per_tcl_sblock %u, shr_nand_info.geo.nr_pages %u shr_nand_info.interleave %u",
        du_per_tcl_sblock, shr_nand_info.geo.nr_pages, shr_nand_info.interleave);

	l2p_mgr_vcnt_move(0, dtag2mem(dtag), dtag_cnt * DTAG_SZE);
	vc = dtag2mem(dtag);

	ftl_core_trace(LOG_ALW, 0x023c, "[IN] vc_print");

	spb_id = 0;

	while (spb_id < shr_nand_info.geo.nr_blocks)
	{
        if(spb_id + 4 < shr_nand_info.geo.nr_blocks){
            ftl_core_trace(LOG_ALW, 0x580c, "Spb : %d, Vac : 0x%x 0x%x 0x%x 0x%x 0x%x",
                spb_id, vc[spb_id], vc[spb_id+1], vc[spb_id+2], vc[spb_id+3], vc[spb_id+4]);
        }else{
            ftl_core_trace(LOG_ALW, 0x8edd, "Spb : %d, Vac : 0x%x",spb_id, vc[spb_id]);
        }

        if ((vc[spb_id] > du_per_tcl_sblock) ||
            ((spb_id + 4 < shr_nand_info.geo.nr_blocks) && ((vc[spb_id+1] > du_per_tcl_sblock)
                || (vc[spb_id+2] > du_per_tcl_sblock) || (vc[spb_id+3] > du_per_tcl_sblock)
                || (vc[spb_id+4] > du_per_tcl_sblock))))
        {
            fail_flag = mTRUE;
            ftl_core_trace(LOG_ALW, 0x26c6, "Vac abnormal!!");
        }

        if (spb_id + 4 < shr_nand_info.geo.nr_blocks)
            spb_id += 5;
        else
            spb_id++;
    }
	//ftl_core_trace(LOG_ALW, 0, "Spb : %d, Vac : 0x%x", spb_id, vc[spb_id]);
	dtag_cont_put(DTAG_T_SRAM, dtag, dtag_cnt);

	//ftl_l2p_put_vcnt_buf(dtag, cnt, false);

	if (fail_flag)
	{
		sys_assert(0);
	}
#endif  
}  

slow_code void evt_flush_spor_qbt(u32 p0, u32 p1, u32 p2) 
{ 
    //cpu_msg_issue(CPU_FE - 1, CPU_MSG_BTN_WR_SWITCH, 0, (u32)false);
	vc_print();  
	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t)); 
	sys_assert(fctx); 
	fctx->ctx.caller = fctx; 
	fctx->ctx.cmpl = flush_qbt_done; 
	fctx->nsid = 1; 
	fctx->flags.all = 0; 
	fctx->flags.b.shutdown = 1; 
	ftl_core_trace(LOG_INFO, 0x8f2a, "[IN]evt_flush_spor_qbt"); 
	if (ftl_core_flush(fctx) == FTL_ERR_OK) { 
        //cpu_msg_issue(CPU_FE - 1, CPU_MSG_BTN_WR_SWITCH, 0, (u32)true);
		ftl_core_trace(LOG_INFO, 0x834d, "fctx:0x%x done", fctx); 
		sys_free(FAST_DATA, fctx); 
	} 
	//ftl_slc_pstream_enable(); 

} 
#endif 


slow_code void fplp_trigger_start(void) 
{
    cpu_msg_issue(CPU_FE - 1, CPU_MSG_BTN_WR_SWITCH, 0, (u32)false); 
	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t)); 
	sys_assert(fctx); 
	fctx->ctx.caller = fctx; 
	fctx->ctx.cmpl = flush_qbt_done; 
	fctx->nsid = 1; 
	fctx->flags.all = 0; 
	fctx->flags.b.shutdown = 1; 
	ftl_core_trace(LOG_INFO, 0x387c, "[IN]ipc_fplp_trigger_start"); 
	if (ftl_core_flush(fctx) == FTL_ERR_OK) { 
        cpu_msg_issue(CPU_FE - 1, CPU_MSG_BTN_WR_SWITCH, 0, (u32)true); 
		ftl_core_trace(LOG_INFO, 0xc17d, "fctx:0x%x done", fctx); 
		sys_free(FAST_DATA, fctx);
    }


}

fast_code void plp_evt_done_check(u32 p0, u32 p1, u32 p2)
{
    plp_done(&plp_fsm.fsm);
}


fast_code void plp_evt_check(u32 p0, u32 p1, u32 p2)
{
    plp_Save_EPM(&plp_fsm.fsm);
	if(plp_close_wl_done&&plp_save_epm_done&&!pending_parity_cnt){
	    fsm_ctx_next(&plp_fsm.fsm);
		fsm_ctx_run(&plp_fsm.fsm);
	}
}
fast_code fsm_res_t plp_close_wl(fsm_ctx_t *fsm)
{
    //ftl_core_trace(LOG_DEBUG, 0, "plp debug write pad start");
    fill_dummy_flag = 1;
	plp_ask_close_open_wl = true;
	u8 start_type = FTL_CORE_NRM;
	u8 end_type   = FTL_CORE_GC;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE) 
	if(write_nsid == HOST_NS_ID && write_type == FTL_CORE_SLC )
	{
		pstream_t *ps  = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_SLC]->ps;
		u32 wptr = ps->aspb[0].wptr;
		SLC_dummy_wl =  wptr / nand_info.interleave;
		fcore_fill_dummy(FTL_CORE_SLC, FTL_CORE_SLC, FILL_TYPE_CLOSE_WL, plp_slc_pad_done, NULL);
	}

	if(ps_open[FTL_CORE_GC] != INV_U16 && power_on_update_epm_flag != POWER_ON_EPM_UPDATE_DONE)
	{
		pstream_t *ps = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_GC]->ps;
		if(SLC_gc_blk_sn == ps->aspb[0].sn)
		{
			end_type = FTL_CORE_NRM;//skip fill GC
			goto FILL_START;
		}
	}
#endif	

	#if MDOT2_SUPPORT == 1
	if(host_dummy_start_wl != INV_U16)
	{
		//por fill dummy task , may no need fill host blk
		pstream_t *ps = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_NRM]->ps;
		u16 cur_host_wl = ps->aspb[0].wptr/(nand_info.interleave * nand_info.bit_per_cell);//cur wl
		ftl_core_trace(LOG_ALW, 0xf1bf,"[PLP] host start wl:%d cur wl:%d",host_dummy_start_wl,cur_host_wl);
		if(cur_host_wl - host_dummy_start_wl >= 2 && cur_host_wl != 0)
		{
			ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_NRM, NULL);
			start_type = FTL_CORE_GC;//skip fill host
		}

		host_dummy_start_wl = INV_U16;
	}
	#endif
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE) 
FILL_START:
#endif
	//normal case
    fcore_fill_dummy(start_type, end_type, FILL_TYPE_WL, plp_spb_pad_done, NULL);

	plp_ask_close_open_wl = false;
    return FSM_PAUSE;
}

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
extern volatile u8 shr_slc_flush_state;
extern volatile bool slc_call_epm_update;
slow_code void slc_gc_open_pad_done(ftl_core_ctx_t *ctx)
{
	shr_slc_flush_state = SLC_FLOW_GC_OPEN_PAD_DONE;
	slc_erase_block();
	//record cur slc gc blk,if plp trigger soon,don't fill dummy
	if(ps_open[FTL_CORE_GC] != INV_U16)
	{
		pstream_t *ps = &fcns[1]->ftl_core[FTL_CORE_GC]->ps;
		SLC_gc_blk_sn = ps->aspb[0].sn;
	}
}

slow_code void slc_erase_block_done(erase_ctx_t * ctx)
{
	if(ctx != NULL)
		sys_free(FAST_DATA,ctx);
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	epm_ftl_data->slc_eraseing_tag = false;
    epm_ftl_data->plp_pre_slc_wl  = 0;
    epm_ftl_data->plp_slc_wl      = 0;
    //epm_ftl_data->plp_slc_times   = 0;
    //epm_ftl_data->plp_slc_gc_tag  = 0;
    epm_ftl_data->plp_slc_disable = 0;
    shr_slc_flush_state = SLC_FLOW_DONE;
	//if(SLC_init == true)
	//{
	//	ftl_slc_pstream_enable();
	//}
	ftl_core_trace(LOG_INFO, 0x04db, "update epm SLC info");
    //pstream_clear_epm_vac();


}

slow_code void slc_erase_block(void)
{
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    u16 SLC_wl    = epm_ftl_data->plp_slc_wl;
	ftl_core_trace(LOG_INFO, 0x0308, "[IN] slc_erase_block SLC_wl:%d",SLC_wl);
	//SLC_call_force_gc = true;
    if(SLC_wl >=  SLC_BUFFER_MAX_WL )//need erase
    {
		epm_ftl_data->plp_slc_gc_tag  = 0;
    	epm_ftl_data->slc_eraseing_tag = true;
    	shr_slc_flush_state = SLC_FLOW_START_ERASE;
    	slc_call_epm_update = true;
    	// update slc vac to gc open blk--------------------------------
		u32 dtag_cnt = occupied_by(nand_block_num() * sizeof(u32), DTAG_SZE);
		u32    *vc;
		dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
		sys_assert(dtag.dtag != _inv_dtag.dtag);
		l2p_mgr_vcnt_move(0, dtag2mem(dtag), dtag_cnt * DTAG_SZE);
		vc = dtag2mem(dtag);

		memcpy(epm_ftl_data->epm_vc_table, vc, nand_block_num()*sizeof(u32));
		dtag_cont_put(DTAG_T_SRAM, dtag, dtag_cnt);
    	// update slc vac to gc open blk--------------------------------

		epm_update(FTL_sign,(CPU_ID-1));//force update epm , avoid power off	
		while(slc_call_epm_update)
		{
			//wait epm update done!!!
		};
		erase_ctx_t* ctx =  sys_malloc(FAST_DATA,sizeof(erase_ctx_t));
	    ctx->spb_ent.b.spb_id = PLP_SLC_BUFFER_BLK_ID;
	    ctx->spb_ent.b.slc = true;
	    ctx->cmpl_cnt = 0;
	    ctx->caller = NULL;
	    ctx->issue_cnt = 0;
	    ctx->cmpl = slc_erase_block_done;

	    ftl_core_trace(LOG_INFO, 0x38f4, "slc_erase_block erase blk:%d", ctx->spb_ent.b.spb_id);
	    ftl_core_erase(ctx);

    }
	else
	{
		shr_slc_flush_state = SLC_FLOW_DONE;	
		epm_ftl_data->plp_slc_gc_tag = 0;
    	//pstream_clear_epm_vac();
	}

}
#endif

#if PLP_DEBUG
fast_code void plp_debug_erase_done(erase_ctx_t *ctx)
{
    plp_fsm_t* fsm_ctx = (plp_fsm_t*)ctx->caller;
    ftl_core_trace(LOG_INFO, 0x0831, "plp debug erase done blk:%d", ctx->spb_ent.b.spb_id);
#if PLP_DEBUG_GPIO
    gpio_set_gpio15(0);
#endif
    //sys_free(FAST_DATA, ctx);
    fsm_ctx_next(&fsm_ctx->fsm);
	fsm_ctx_run(&fsm_ctx->fsm);
}

fast_code fsm_res_t plp_erase_block(fsm_ctx_t *fsm)
{
    erase_ctx_t* ctx =  &((plp_fsm_t*) fsm)->erase_ctx;
    ctx->spb_ent.b.spb_id = 400;
    ctx->spb_ent.b.slc = false;
    ctx->cmpl_cnt = 0;
    ctx->caller = fsm;
    ctx->issue_cnt = 0;
    ctx->cmpl = plp_debug_erase_done;

    ftl_core_trace(LOG_INFO, 0x9b9c, "plp debug erase blk:%d", ctx->spb_ent.b.spb_id);
    ftl_core_erase(ctx);
    return FSM_QUIT;
}
#endif


fast_code fsm_res_t plp_done(fsm_ctx_t *fsm)
{
	static bool print_log = true;
	static bool trigger_flush_nand = true;
	if(print_log)
	{	
		print_log = false;
    	ftl_core_trace(LOG_INFO, 0x00e6, "plp done cost time:%d us fcmd:%d epm_done:%d pending_parity_cnt:%d", 
    		time_elapsed_in_us(plp_time_start),fcmd_outstanding_cnt,plp_epm_update_done,pending_parity_cnt);
	}
//#if ASSERT_LVL != ASSERT_LVL_CUST
    //cpu_msg_issue(CPU_FE - 1, CPU_MSG_PLP_DONE, 0, 0);  //only for debug
//#endif
	if(fcmd_outstanding_cnt == 0 && plp_epm_update_done && !pending_parity_cnt)
	{
		//plp done , do next
	}
	else
	{
		evt_set_cs(evt_plp_done_chk, 0, 0, CS_TASK);
		return FSM_PAUSE;
	}

	if(trigger_flush_nand)
	{
		trigger_flush_nand = false;
		flush_to_nand(EVT_PLP_HANDLE_DONE);
		evt_set_cs(evt_plp_done_chk, 0, 0, CS_TASK);
		return FSM_PAUSE;
	}
	else
	{
		extern bool check_evlog_flush_done();
		if(check_evlog_flush_done() == false)
		{
			evt_set_cs(evt_plp_done_chk, 0, 0, CS_TASK);
			return FSM_PAUSE;
		}
	}

#if PLP_DEBUG_GPIO
    gpio_set_gpio15(0);
    delay_us(1800);
    gpio_set_gpio15(1);
#endif

#if ASSERT_LVL == ASSERT_LVL_CUST
    log_level_chg(LOG_INFO);
#endif



    ftl_core_trace(LOG_INFO, 0x5739, "[EH] parity_alloc_cnt[%d] pending_parity_cnt[%u] ps.parity_cnt(host/gc/pbt)[%d/%d/%d]", \
		parity_allocate_cnt, pending_parity_cnt, \
		fcns[HOST_NS_ID]->ftl_core[FTL_CORE_NRM]->ps.parity_cnt, fcns[HOST_NS_ID]->ftl_core[FTL_CORE_GC]->ps.parity_cnt,  fcns[INT_NS_ID]->ftl_core[FTL_CORE_PBT]->ps.parity_cnt);


#if 0//(DEBUG_SPOR == mENABLE) // for dbg
    u32 laa = 0;
    u64 addr_base;
    u64 addr;
    u64 l2p_base = ddtag2off(shr_l2p_entry_start);
    ftl_core_trace(LOG_ALW, 0xa3d2, "[PLP L2P Print]");
    addr_base = (l2p_base | 0x40000000);
    while(laa < 5)
    {
        addr = addr_base + ((u64)laa * sizeof(lda_t));
        ftl_core_trace(LOG_ALW, 0x7935, "LAA : 0x%x, L2P PDA : 0x%x", laa, *((u32*)(u32)addr));
        laa++;
    }
#endif
	#ifdef CPU_OFF_FOR_OTP
	extern misc_register_regs_t *misc_reg;
	#if defined(MPC)
	u32 i;
//	u8 idx;
	while(1)
	{
	    {
	    	extern volatile u8 plp_PWRDIS_flag;
			if(plp_PWRDIS_flag){
				plp_PWRDIS_flag = 2; // cpu2 in plp_done
				mdelay(500);
			}
//			if(plp_PWRDIS_flag == 1){
//				printk("\nIn plp_done, plp_PWRDIS_flag: %d\n", plp_PWRDIS_flag);
//				cpu_msg_issue(CPU_FE - 1, CPU_MSG_PLP_SET_ENA, 0, PLP_ENA_DISABLE);
//				plp_PWRDIS_flag = 0;
//				idx = 1; // keep cpu1 to do i2c setting 
//			}
//			else{
//				idx = 0;
//			}

	        plp_close_wl_done = 0;
            plp_save_epm_done = 0;

			evlog_printk(LOG_ALW,"cpu off start");
			mdelay(10);
	        for (i = 1; i < MPC; i++)
	        {
	            cpu_halt(i, true);  //disable other cpu    
	            evlog_printk(LOG_ALW,"turn off cpu %d ",i);
	        }
	        break;
	    }
	}
	#endif
	clk_dis_ctrl_t clk_ctrl = {.all = readl(&misc_reg->clk_dis_ctrl.all),};
	clk_ctrl.b.clk_dis = 0xFFFF;
	writel(clk_ctrl.all, &misc_reg->clk_dis_ctrl.all);
	evlog_printk(LOG_ALW,"cpu off end");
#endif
	mdelay(1000);
	fctx_addr = NULL;
	plp_gc_suspend_done = false;
	tFtlPbt.force_pbt_halt = false;
    plp_trigger = 0;
    shr_shutdownflag = false;
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_RESUME;
	action->caller = NULL;
	action->cmpl = NULL;//gc_action_done;

	if (gc_action(action))
	{
		sys_free(FAST_DATA, action);
	}

    return FSM_QUIT;
}

ddr_code void format_save_vac_table(bool type)
{
	u32 dtag_cnt = occupied_by(nand_block_num() * sizeof(u32), DTAG_SZE);
	//dtag_t dtag ;
	ftl_core_trace(LOG_ALW, 0x7812," type:%d save_done:%d copy:%d",
			type,shr_trim_vac_save_done,shr_format_copy_vac_flag);
	if(type)//copy
	{
		fmt_vac_dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
		sys_assert(shr_trim_vac_save_done == false);
		l2p_mgr_vcnt_move(0, dtag2mem(fmt_vac_dtag), dtag_cnt * DTAG_SZE);
		shr_trim_vac_save_done = true;
	}
	else	//release
	{
		shr_format_copy_vac_flag = false;
    	dtag_cont_put(DTAG_T_SRAM, fmt_vac_dtag, dtag_cnt);
	}
}

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
fast_code void plp_copy_ftl_data_to_epm(void)
{
    u32    *vc;
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    u32 dtag_cnt = occupied_by(nand_block_num() * sizeof(u32), DTAG_SZE);
	dtag_t dtag ;

    //ftl_core_trace(LOG_ALW, 0, "[IN] plp_copy_ftl_data_to_epm");

#if 0//(SPOR_TIME_COST == mENABLE)
    u64 time_start = get_tsc_64();
#endif

	if(shr_format_copy_vac_flag)
	{
		sys_assert(fmt_vac_dtag.dtag != _inv_dtag.dtag);
		dtag = fmt_vac_dtag;
		//vc = dtag2mem(fmt_vac_dtag);
	}
	else
	{
		dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
		sys_assert(dtag.dtag != _inv_dtag.dtag);
		l2p_mgr_vcnt_move(0, dtag2mem(dtag), dtag_cnt * DTAG_SZE);
	}

	vc = dtag2mem(dtag);
    // ===== vac table area =====
    memcpy(epm_ftl_data->epm_vc_table, vc, nand_block_num()*sizeof(u32));
    dtag_cont_put(DTAG_T_SRAM, dtag, dtag_cnt);

    // dbg log
    #if 0
    u16 blk_idx = fc_get_gc_spb(); 
    if(blk_idx != 0xFFFF){ 
        ftl_core_trace(LOG_ALW, 0xbcac, "[GC] Spb ID : %d, VAC : 0x%x",\
                               blk_idx, epm_ftl_data->epm_vc_table[blk_idx]); 
    } 
    blk_idx = ps_open[FTL_CORE_GC]; 
    if(blk_idx != 0xFFFF){ 
        ftl_core_trace(LOG_ALW, 0xe4d0, "[GCOpen]  Spb ID : %d, VAC : 0x%x",\
                               blk_idx, epm_ftl_data->epm_vc_table[blk_idx]); 
    } 
    blk_idx = ps_open[FTL_CORE_NRM]; 
    if(blk_idx != 0xFFFF){ 
        ftl_core_trace(LOG_ALW, 0x8ba2, "[USROpen] Spb ID : %d, VAC : 0x%x",\
                               blk_idx, epm_ftl_data->epm_vc_table[blk_idx]); 
    } 
    #else  
    spb_id_t gc_open = 0,host_open = 0,gc_done = 0;  
    if(fc_get_gc_spb() != 0xFFFF)  
    	gc_done = fc_get_gc_spb();  
    if(ps_open[FTL_CORE_GC] != 0xFFFF)  
    	gc_open = ps_open[FTL_CORE_GC];  
    if(ps_open[FTL_CORE_NRM] != 0xFFFF)  
    	host_open = ps_open[FTL_CORE_NRM];  

    ftl_core_trace(LOG_INFO, 0x9e8e, "usr %d vc 0x%x, gc %d vc 0x%x, gcd %d vc 0x%x",  
    		host_open,epm_ftl_data->epm_vc_table[host_open],gc_open,epm_ftl_data->epm_vc_table[gc_open],gc_done,epm_ftl_data->epm_vc_table[gc_done]);  
    #endif  

    // ===== ec table area =====
    spb_info_t* spb_info_tbl = (spb_info_t*)ddtag2mem(shr_spb_info_ddtag);
    //ftl_core_trace(LOG_ALW, 0, "spb_info_tbl : 0x%x, shr_spb_info_ddtag : 0x%x",
    //               spb_info_tbl, shr_spb_info_ddtag);

    u16    spb_id;
    for(spb_id = 0; spb_id < nand_block_num();spb_id++)
    {
        #if 0
        if(spb_id <= 20)
        {
            ftl_core_trace(LOG_ALW, 0x4812, "[EPM PLP] Spb ID : 0x%x, Spb ec : 0x%x, EPM ec : 0x%x",\
                           spb_id, spb_info_tbl[spb_id].erase_cnt, epm_ftl_data->epm_ec_table[spb_id]);
        }
		#endif

        epm_ftl_data->epm_ec_table[spb_id] = spb_info_tbl[spb_id].erase_cnt;
    }

	//ftl_core_trace(LOG_ALW, 0, " [FTL]last pbt:%d  seg info:%d",tFtlPbt.pbt_spb_cur,tFtlPbt.cur_seg_ofst); 
#if(MDOT2_SUPPORT == 1) 
#if(PLP_SUPPORT == 1) 
    if(PLP_IC_SGM41664)
    {
        epm_ftl_data->SGM_data_F_10 = SGM_data;
    }
#endif  
#endif
    epm_ftl_data->spor_tag = FTL_EPM_SPOR_TAG;
	epm_ftl_data->last_close_host_blk = host_spb_close_idx;
	epm_ftl_data->last_close_gc_blk = gc_spb_close_idx;
	epm_ftl_data->panic_build_vac = false;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
/*---------------------------SLC INFO-----------------------*/
	if(write_nsid == HOST_NS_ID && write_type == FTL_CORE_SLC )
	{
		core_wl_t *cwl = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_SLC]->cwl;
		pstream_t *ps  = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_SLC]->ps;
		u32 wptr = ps->aspb[0].wptr;
		epm_ftl_data->plp_pre_slc_wl = epm_ftl_data->plp_slc_wl;
		epm_ftl_data->plp_slc_times++;
		epm_ftl_data->plp_slc_wl = wptr / nand_info.interleave + 1;//record next blank wl
		if(SLC_dummy_wl != 0xFFFF)//has fill dummy , wptr is new wl
			epm_ftl_data->plp_slc_wl = SLC_dummy_wl + 1;	
		ftl_core_trace(LOG_ALW, 0x02c7, "[SLC]wptr:0x%x wl:%d dummy_wl:%x die:%d vc:0x%x", 
				wptr,wptr / nand_info.interleave,SLC_dummy_wl  , cwl->cur.die,epm_ftl_data->epm_vc_table[1]);
		epm_ftl_data->plp_slc_gc_tag =  FTL_PLP_SLC_NEED_GC_TAG;
		//if(host_open == 0 && gc_open == 0)
		//	epm_ftl_data->plp_last_blk_sn = INV_U32;
		//else
			epm_ftl_data->plp_last_blk_sn = max_usr_blk_sn;
	}

/*---------------------------SLC INFO-----------------------*/
#endif	

#if 0 //(SPOR_TIME_COST == mENABLE)
    ftl_core_trace(LOG_ALW, 0x8621, "Function time cost : %d us", time_elapsed_in_us(time_start));
#endif
}
#endif

extern u8 has_log;
fast_code fsm_res_t plp_Save_EPM(fsm_ctx_t *fsm)
{
    if(fc_get_gc_spb() != INV_SPB_ID)
    {
//      if(!gc_suspend_done)
        if(!is_gc_write_done_when_plp())
        {
            evt_set_cs(evt_plp, 0, 0, CS_TASK);
			if (!has_log) {
				has_log = 1;
            ftl_core_trace(LOG_ALW, 0x1ee0, "gc suspend not done abort save epm");
			}
            return FSM_CONT;
        }else {
			has_log = 0;
        }
    }
#if EPM_NOT_SAVE_Again
    extern u8 EPM_NorShutdown;
	if(EPM_NorShutdown == 1)
	{
        ftl_core_trace(LOG_ALW, 0xd0aa, " undo PLPEPMS");
		return FSM_CONT;
	}
#endif

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
    plp_copy_ftl_data_to_epm();
#endif

 	//ftl_core_trace(LOG_ALW, 0, "PLPEPMS");
	epm_update(EPM_PLP1,(CPU_ID-1));
 	//ftl_core_trace(LOG_ALW, 0, "PLPEPME");

#if PLP_DEBUG_GPIO
    gpio_set_gpio15(1);
#endif
	plp_save_epm_done = 1;
    return FSM_CONT;
}

#if 0
fast_code fsm_res_t plp_st_pad_spb(fsm_ctx_t *fsm)
{
	plp_fsm_t *ctx = (plp_fsm_t *) fsm;
	ftl_spb_pad_t *spb_pad = &ctx->spb_pad;

	ftl_core_trace(LOG_INFO, 0x3cf4, "plp fsm[%d]: pad open stripe & xlc spb", fsm->state_cur);
	memset(spb_pad, 0, sizeof(ftl_spb_pad_t));

	spb_pad->pad_cwl_cnt = 0;
#if defined(USE_TSB_NAND)
	spb_pad->pad_cwl_cnt = 4;
#else
	spb_pad->pad_cwl_cnt = 1;
#endif
    spb_pad->ns_bitmap = (1 << HOST_NS_ID);

	spb_pad->start_type = FTL_CORE_NRM;
	spb_pad->type_cnt = FTL_CORE_MAX;
	spb_pad->pad_to_end_spb_id = INV_SPB_ID;

	spb_pad->ctx.caller = ctx;
	spb_pad->ctx.cmpl = plp_spb_pad_done;
	ftl_core_spb_pad(spb_pad);

	return FSM_PAUSE;
}
#endif


fast_data static fsm_funs_t _plp_st_func[] = {
#if 0//(PLP_GC_SUSPEND == mENABLE)
	{"suspend gc", plp_suspend_gc},
#endif
#if PLP_DEBUG
    {"fill up", plp_debug_fill_up},
#endif
#if PLP_DEBUG
    {"erase block", plp_erase_block},
#endif
	{"Save EPM", plp_Save_EPM},
	{"close wl", plp_close_wl},
#if PLP_DEBUG
    //{"flush dummy", plp_add_dummy_wl},
    {"erase block", plp_erase_block},
#endif
	{"plp done", plp_done},
};

fast_data static fsm_state_t _plp_fsm_state = {
	.name = "plpfsm",
	.fns = _plp_st_func,
	.max = ARRAY_SIZE(_plp_st_func)
};

fast_data void plp_force_flush_done(void)
{
	extern volatile u8 CPU2_plp_step;
	CPU2_plp_step = 5;
    #if ASSERT_LVL == ASSERT_LVL_CUST
    log_level_chg(LOG_DEBUG);
    #endif
    plp_time_start = get_tsc_64();
    //ftl_core_trace(LOG_ALW, 0, "start fsm:0x%x-%x", plp_time_start>>32, plp_time_start&0xFFFFFFFF);
    ucache_flush_flag = 0;
	plp_save_epm_done = 0;
	plp_close_wl_done = 0;
    fsm_ctx_init(&plp_fsm.fsm, &_plp_fsm_state, NULL, NULL);

    fsm_ctx_run(&plp_fsm.fsm);
}

#if PLP_DEBUG
init_code void plp_debug_dtag_alloc(void)
{
    fill_up_dtag_start = ddr_dtag_register(256);
    fill_up_dtag_meta = &((io_meta_t *)shr_ddtag_meta)[fill_up_dtag_start];
}

init_code void plp_init(void)
{
    //fill up read debug dtag pool allocate
    plp_debug_dtag_alloc();
}
#endif

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
slow_code void ftl_slc_pstream_enable(void)
{
	extern volatile u8 shr_lock_power_on;
	shr_lock_power_on &= (~SLC_LOCK_POWER_ON);  // slc_lock --
	/*
	if(SLC_call_force_gc == true)
	{
		cpu_msg_issue(2, CPU_MSG_CHK_GC, 0, 0); //avoid in RO and lock gc
	#if (BG_TRIM == ENABLE)
        cpu_msg_issue(CPU_FE - 1, CPU_MSG_CHK_BG_TRIM, 0, 0);
	#endif
		SLC_call_force_gc = false;
	}
	*/
	if(SLC_init != false)
	{
		ftl_core_trace(LOG_INFO, 0x4886, "SLC_init != false SLC_init:0x%x ",SLC_init);
		return;
	}
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    if(epm_ftl_data->plp_slc_disable || epm_ftl_data->plp_slc_gc_tag == FTL_PLP_SLC_NEED_GC_TAG)
    {
		ftl_core_trace(LOG_INFO, 0x71c7, "slc blk may error!! no use it pre_wl:%d,cur_wl:%d slc_times:%d gc_tag:0x%x disable:%d",
				epm_ftl_data->plp_pre_slc_wl,epm_ftl_data->plp_slc_wl,epm_ftl_data->plp_slc_times,epm_ftl_data->plp_slc_gc_tag,epm_ftl_data->plp_slc_disable);
		SLC_init = 0xFF;//error
		epm_ftl_data->plp_slc_disable = true;
		epm_ftl_data->plp_slc_gc_tag  = 0;//avoid fail
		return;
    }

	u32 df_width = occupied_by(nand_info.interleave, 8) << 3; 
	u32 df_byte = (PLP_SLC_BUFFER_BLK_ID * df_width) >> 3; 
	u8* ftl_df_ptr = gl_pt_defect_tbl + df_byte;
	u32 k;
	u32 good_plane_ttl_cnt = nand_info.interleave;
	for(k = 0; k < df_byte; k++)
	{
		if ((k == df_byte - 1) && (nand_info.interleave%8 != 0))
		{
			u8 last_byte_pl = nand_info.interleave%8;
			good_plane_ttl_cnt -= pop32(ftl_df_ptr[k] & ((1 << last_byte_pl) - 1));
		}
		else
		{
			good_plane_ttl_cnt -= pop32(ftl_df_ptr[k]);
		}
	}
	if (good_plane_ttl_cnt < 10)
	{
		SLC_init = 0xFF;
		epm_ftl_data->plp_slc_disable = true;
		epm_ftl_data->plp_slc_gc_tag  = 0;//avoid fail
		ftl_core_trace(LOG_INFO, 0xc39d,"slc good plane:%d ,gc aux may fail!!",good_plane_ttl_cnt);
		return;
	}


	core_wl_t *cwl = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_SLC]->cwl;
	pstream_t *ps = &fcns[HOST_NS_ID]->ftl_core[FTL_CORE_SLC]->ps;	
	cwl->cur.mpl_qt = 0;
	ps->aspb[0].spb_id = INV_SPB_ID;
	ps->avail_cnt = 0;
	ps->flags.all = 0;
	pstream_supply(ps);
	sys_assert(ps->flags.b.p2l_enable == 0);
	/* ================change cur pstream wptr====================*/
    if(epm_ftl_data->plp_slc_wl != 0 && epm_ftl_data->plp_slc_wl != INV_U16)
    	jump_to_next_wl(ps, epm_ftl_data->plp_slc_wl);
	ftl_core_trace(LOG_INFO, 0x02bd, "open slc pstream , pre_wl:%d,cur_wl:%d slc_times:%d",
			epm_ftl_data->plp_pre_slc_wl,epm_ftl_data->plp_slc_wl,epm_ftl_data->plp_slc_times);
	SLC_init = true;
}
#endif

#if 0
static slow_code int close_main(int argc, char *argv[])
{
	u32 mode = strtol(argv[1], (void *)0, 10);
	switch(mode) {
		case 0:
			ftl_core_trace(LOG_ERR, 0x9840, "fill blk\n");
			fcore_fill_dummy(0, 0,FILL_TYPE_BLK, NULL, NULL);
			break;
		case 1:
			ftl_core_trace(LOG_ERR, 0xc126, "fill layer\n");
			fcore_fill_dummy(0, 0,FILL_TYPE_LAYER, NULL, NULL);
			break;
		case 2:
			ftl_core_trace(LOG_ERR, 0xfc9d, "fill wl\n");
			fcore_fill_dummy(0, 0,FILL_TYPE_WL, NULL, NULL);
			break;
	}
    return 0;
}
static DEFINE_UART_CMD(close_main, "close", "close",
		"ucache status", 1, 3, close_main);
#endif


#if (epm_enable && epm_uart_enable)
//#include "console.h"
extern epm_info_t*  shr_epm_info;
fast_code int epm_update_cpu2(int argc, char *argv[])
{
	u32 i;
	ftl_core_trace(LOG_ERR, 0xa599, "epm_update_cpu2\n");
#if epm_spin_lock_enable
	check_and_set_ddr_lock(FTL_sign);
#endif
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	for(i=0;i<(262144/4);i++) epm_ftl_data->data[i]=0x20000000+(FTL_sign*0x100000)+i;
	epm_update(FTL_sign,(CPU_ID-1));
	return 0;
}
static DEFINE_UART_CMD(epm_update2, "epm_update2", "epm_update2", "epm_update2", 0, 0, epm_update_cpu2);
#endif
//Andy_Crypto
#if 0//defined(USE_CRYPTO_HW)
ddr_code int change_crypto_cpu2(int argc, char *argv[])
{
	ftl_core_trace(LOG_ERR, 0xb347, "change_range_cpu3\n");

	crypto_change_mode_range(2,1,1,1);
	return 0;
}

static DEFINE_UART_CMD(change_crypto2, "change_crypto2", "change_crypto2", "change_crypto2", 0, 0, change_crypto_cpu2);
#endif
#if 0//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
ddr_code int show_slc_info_CPU2(int argc, char *argv[]) 
{	 
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	ftl_core_trace(LOG_ALW, 0x9d35, "pre_wl:%d wl:%d times:%d gc_tag:%x f_tag:%x",
		epm_ftl_data->plp_pre_slc_wl,epm_ftl_data->plp_slc_wl,epm_ftl_data->plp_slc_times,epm_ftl_data->plp_slc_gc_tag,epm_ftl_data->slc_format_tag);

	pstream_t *ps = &fcns[2]->ftl_core[1]->ps;
	ftl_core_trace(LOG_ALW, 0x1e32, "wptr:%x wl:%d avail_cnt:%x slc_init:%x",
		ps->aspb[0].wptr,ps->aspb[0].wptr / nand_info.interleave,ps->avail_cnt,SLC_init);		 
	return 0; 
} 
static DEFINE_UART_CMD(slc_info , "slc_info","slc_info","slc_info",0,0,show_slc_info_CPU2); 
#endif
/*
ddr_code int trim_blk_bitmap_debug_main(int argc, char *argv[])
{
	//epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	u32 type = strtol(argv[1], (void *)0, 10);
	epm_trim_t* epm_trim_data = (epm_trim_t*)ddtag2mem(shr_epm_info->epm_trim.ddtag);
	if(type == 0)
	{
		for(u8 i = 0;i<256/4;i++)
		{
			ftl_core_trace(LOG_ALW, 0, "idx:%d bitmap:0x%x",i,epm_trim_data->TrimBlkBitamp[i]);
		}
	}
	else if(type < nand_info.geo.nr_blocks)
	{
		extern u8* TrimBlkBitamp;
		set_bit(type, TrimBlkBitamp);

		ftl_core_trace(LOG_ALW, 0, "blk:%d",type);
	}
	return 0;
}
static DEFINE_UART_CMD(trimbit , "trimbit","[0] show | [1] set","slc_info",1,1,trim_blk_bitmap_debug_main); 
*/
/*! @} */
