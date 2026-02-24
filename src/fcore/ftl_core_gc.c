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

/*! \file ftl_core_gc.c
 * @brief ftl core GC module,
 *
 * \addtogroup fcore
 * \defgroup fcore_gc
 * \ingroup fcore
 * @{
 * handle PDA in to read GC data from nand, handle DATA in to write GC data to new location.
 * Each scheduler has one GC group resource,
 * A GC resource including one DTAG, META, PDA response entry.
 */

#include "fcprecomp.h"
#include "fc_export.h"
#include "bf_mgr.h"
#include "ncl_exports.h"
#include "ncl.h"
#include "die_que.h"
#include "ftl_core.h"
#include "event.h"
#include "idx_meta.h"
#include "bf_mgr.h"
#include "addr_trans.h"
#include "ddr.h"
#include "ipc_api.h"
#include "bitops.h"
#include "mpc.h"
#include "ftl_remap.h"
#include "spin_lock.h"
#include "ftl_p2l.h"
#include "../ftl/ErrorHandle.h"
#include "read_retry.h"
#include "ddr_top_register.h"

/*! \cond PRIVATE */
#define __FILEID__ fcgc
#include "trace.h"
/*! \endcond */

typedef struct _ftl_core_gc_t {
	fsm_ctx_t fsm;
	io_meta_t *meta;		///< index meta buffer for GC usage
	pda_t *pda_list;		///< cpda list buffer from l2pe
    pda_t *lda_list;   //gc lda list from p2l

	bm_pl_t *pend_buf;		///< when data in but can't submit to write payload, pend data entries here

	spb_id_t spb_id;	///< current GC spb
	spb_id_t suspend_spb;	///< suspended GC spb
	u32 spb_sn;		///< current GC spb sn
	u32 suspend_spb_sn;	///< suspended GC spb sn
	u16 suspend_fsm_st;	///< fsm state when suspended GC was resumed
	u16 pend_cnt;		///< pending PDA count
	u32 tmp_cnt;
	u32 write_cnt;		///< total write count
	u32 spb_ttl_du;		///< total du in GC spb

	u16 ttl_p2l_cnt;	///< ttl valid p2l count in this spb
	u16 nand_p2l_cnt;	///< p2l count which could load from nand
	u16 ddr_p2l_cnt;	///< p2l count which rebuild by meta and saved in ddr
	u16 p2l_ttl_done;	///< total gc done p2l count
	u16 ttl_grp_cnt;
	bool odd_p2l;
	u8 p2l_gen_idx;	///< p2l gen index in current p2l group
	u8 p2l_done_cnt;	///< p2l gc done count in current p2l group
	u16 grp_rd_done;	///< gc group count which complete vpda read
	
	u32 last_ok_page;	///< gc spb last programmed ok page
	u32 last_err_page;	///< gc spb last programmed page with error

	p2l_load_req_t p2l_load;	///< p2l load context
	p2l_build_req_t p2l_build;	///< p2l build context
	gc_action_t *gc_action;	///< gc action(stop/suspend/resume) context
	gc_req_t *pend_req;		///< pend gc req due to l2p seg not ready

	short p2l_ping_pong_cnt;			///< p2l ping pong
	bool p2l_not_ready;
	u16 vgen_p2l_cnt;
	u16 vgen_p2l_done_cnt;
	union {
		struct {
			u16 suspend : 1;	///< received suspend signal, start to suspend
			u16 suspend_done : 1;	///< suspend done, this GC is not finished normally
			u16 abort : 1;		///< received abort signal
			u16 abort_done : 1;	///< this gc was aborted
			u16 done : 1;	///< spb gc done
			u16 gc_plp : 1;
		} b;
		u16 all;
	} flags;

	union {
		struct {
			u16 slc : 1;	///< current SPB type
			u16 dslc : 1;	///< current SPB is dynamic slc
			u16 spb_open : 1;	///< current SPB is open
			u16 wl_suspend : 1;	///< current SPB is suspended by WL before
			u16 spb_spor_open : 1;
			u16 spb_warmboot_open : 1;
		} b;
		u16 all;
	} attr;
} ftl_core_gc_t;

typedef enum {
	SPB_SCAN_LAST_OK_PAGE = 0,
	SPB_SCAN_LAST_ERR_PAGE = 1,
} spb_scan_stage_t;

typedef struct _ftl_core_spb_scan_t {
	//scan para
	//u16 start_page;
	//u16 last_page;
	//u16 read_page;
	//u16 dtag_start;
	u16 scan_WL;
	u16 scan_start;
	//scan res
	pda_t *pda_list;
	bm_pl_t *bm_pl_list;
	struct info_param_t *info_list;
	struct ncl_cmd_t *ncl_cmd;

	//scan result
	u16 otf_read;
	u16 p2l_exit;
	u16 era_du_cnt;
	u16 scan_stage;

	//log_level_t log_lvl;
} ftl_core_spb_scan_t;

/*!
 * @brief ftl core gc object
 */
fast_data_zi bool p2l_gen_pend;
fast_data u8 evt_l2p_ready_chk = 0xff;
fast_data_zi static ftl_core_gc_t _ftl_core_gc;
fast_data_zi static ftl_core_spb_scan_t gc_spb_scan;
share_data_zi volatile ftl_flags_t shr_ftl_flags;
share_data_zi void *shr_ddtag_meta;				///< DDR dtag meta buffer
share_data_zi volatile u32 shr_gc_ddtag_start;
share_data_ni volatile gc_pda_grp_t gc_pda_grp[MAX_GC_PDA_GRP];		///< GC valid pda group resource
share_data_zi gc_src_data_que_t *gc_src_data_que;
share_data_zi volatile gc_res_free_que_t *gc_res_free_que;
slow_data_ni gc_src_data_que_t _gc_src_data_que;
slow_data_ni gc_res_free_que_t _gc_res_free_que;
extern struct du_meta_fmt dtag_meta[];				///< SRAM DTAG meta pointer
fast_data u8 evt_p2l_load = 0xFF;
fast_data u8 evt_pend_out = 0xFF;
fast_data_zi u8 gc_issue_load_p2l;
fast_data_zi u8 du_cnt_per_die;
//fast_data_zi u16 gc_max_alloc_wr;
share_data_zi u16 gc_gen_p2l_num;
share_data_zi u16 shr_max_alloc_wl_speed;
//fast_data_zi u8 gc_start_next_fc_cnt;
//fast_data_zi u32 gc_load_p2l_time;
fast_data_zi u64 gc_read_disturb_time;
slow_data_zi u32 gc_scan_dummy_dtag;
slow_data_zi struct du_meta_fmt *gc_scan_meta;  //gc open_blk scan ddr meta addr
slow_data_zi u32 gc_scan_meta_idx;  //gc open_blk scan ddr meta index
share_data_zi u16 gc_next_blk_cost;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
share_data_zi pda_t shr_plp_slc_start_pda;
#endif

extern fast_data u8 evt_gc_issue_load_p2l; 
share_data_zi volatile u8 shr_gc_fc_ctrl;
share_data_zi volatile u8 shr_gc_read_disturb_ctrl;

share_data_zi volatile u8 shr_no_host_write;
share_data_zi volatile u8 shr_gc_no_slow_down;
share_data_zi volatile u32 shr_hostw_perf;
share_data_zi volatile u32 shr_bew_perf;
extern volatile u32 shr_p2l_grp_cnt;
share_data_zi volatile int _fc_credit;
share_data_zi volatile u32 shr_gc_perf; 
share_data_zi volatile u32 shr_host_write_cnt;
share_data_zi volatile u32 pre_shr_host_write_cnt;
share_data_zi volatile u32 shr_gc_rel_du_cnt;
share_data_zi volatile bool shr_shutdownflag;
//fast_data_zi u32 p2l_scan_dtag;
share_data volatile u8 plp_trigger;
extern bool gc_suspend_start_wl;
fast_data_zi u16 gc_suspend_grp_id;
fast_data_zi u32 gc_suspend_spb_sn;
extern u32 global_gc_mode; // ISU, GCRdFalClrWeak

#define CE_HLIST_SHF            12			///< hash list size setting
#define CE_HLIST_SZE      (1 << CE_HLIST_SHF)		///< hash list size
#define CE_HLIST_MSK      (CE_HLIST_SZE - 1)		///< mask to get hash list position
#define CES_CNT 		(DDR_DTAG_CNT + 260)		///< number of ce
extern u32 cache_cnt;
share_data_zi ce_t * ftl_core_gc_ces;	///< ce entries
//static slow_data_ni merger_cat_ctx_t _cat_ctx[CAT_CTX_CNT];	///< concat context
//sram_sh_data ucache_mgr_t _umgr;		///< cache manager unit
//sram_sh_data ucache_mgr_t *umgr = &_umgr;	///< cache manager
share_data_zi volatile u16 * ftl_core_gc_umgr;
share_data_zi volatile u16 lock_ce_hash1;
share_data_zi volatile u16 lock_ce_hash0;
share_data_zi bool plp_gc_suspend_done;

share_data epm_info_t*  shr_epm_info;

fast_code ce_t* ftl_core_gc_cache_search(u32 lda)
{
    sys_assert(ftl_core_gc_umgr);
    sys_assert(ftl_core_gc_ces);
    u16 hash_code = (lda & CE_HLIST_MSK);
    spin_lock_take(SPIN_LOCK_KEY_CACHE,0,true);
    lock_ce_hash1 = hash_code;
	
#ifdef While_break
	u64 start = get_tsc_64();
#endif

	do
	{
#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif		
	}
    while(lock_ce_hash0 == lock_ce_hash1);
    spin_lock_release(SPIN_LOCK_KEY_CACHE);
    u16 ce_idx = ftl_core_gc_umgr[hash_code];

    if (ce_idx == INV_U16) {
        lock_ce_hash1 = INV_U16;
        return NULL;
    }
    sys_assert(ce_idx < cache_cnt);

    ce_t* ce = &ftl_core_gc_ces[ce_idx];

#ifdef While_break
	start = get_tsc_64();
#endif	

    while(1) {
        if (ce->lda == lda) {
            lock_ce_hash1 = INV_U16;
            return ce;
        }

        if (ce->entry_next == INV_U16) {
            lock_ce_hash1 = INV_U16;
            return NULL;
        }
        ce = &ftl_core_gc_ces[ce->entry_next];

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
    }
    lock_ce_hash1 = INV_U16;
    return NULL;
}

/*!
 * @brief handle gc data in from other CPU
 *
 * @return		not used
 */
static void ftl_core_gc_fetch_src_data(void);

/*!
 * @brief gc state function to get spb last programmed page
 *
 * @param fsm	gc state machine
 *
 * @return	return state machine behavior
 */
void evt_ftl_core_gc_p2l_next(u32 parm, u32 payload, u32 sts);
void evt_pend_out_check(u32 parm, u32 payload, u32 sts);

static fsm_res_t ftl_core_gc_st_get_end_page(fsm_ctx_t *fsm);

/*!
 * @brief gc state function to build open p2l grp's p2l
 *
 * @param fsm	gc state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t ftl_core_gc_st_build_p2l(fsm_ctx_t *fsm);

/*!
 * @brief gc state function to load p2l
 *
 * @param fsm	gc state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t ftl_core_gc_st_load_p2l(fsm_ctx_t *fsm);
static bool gc_suspend_abort_check(u16 state);
//static void ftl_core_gc_p2l_next();

/*!
 * @brief gc state function to gen vpda
 *
 * @param fsm	gc state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t ftl_core_gc_st_gen_vpda(fsm_ctx_t *fsm);

/*!
 * @brief gc state function to read vpda and write
 *
 * @param fsm	gc state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t ftl_core_gc_st_xfer_data(fsm_ctx_t *fsm);

/*!
 * @brief gc state function to pad open die
 *
 * @param fsm	gc state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t ftl_core_gc_st_pad_open_die(fsm_ctx_t *fsm);

/*!
 * @brief gc state function of spb done
 *
 * @param fsm	gc state machine
 *
 * @return	return state machine behavior
 */
static fsm_res_t ftl_core_gc_st_spb_done(fsm_ctx_t *fsm);

/*!
 * @brief run ftl core gc state machine
 *
 * @param fsm		gc fsm
 *
 * @return	not used
 */
static void ftl_core_gc_fsm_run(fsm_ctx_t *fsm);

/*!
 * @brief scan spb page
 *
 * @return	not used
 */
static void ftl_core_gc_scan_read(u32 start);

/*!
 * @brief check if all gc group has wr done
 *
 * @return	true if all gc grp wr done
 */
static bool ftl_core_gc_all_grp_done(void);

/*!
 * @brief go to next fsm state according current gc context
 *
 * @return	not used
 */
static void ftl_core_gc_exec_next_st(void);

enum {
	GC_ST_GET_END_PAGE	= 0,
	GC_ST_BUILD_P2L		= 1,
	GC_ST_LOAD_P2L		= 2,
	GC_ST_GEN_VPDA		= 3,
	GC_ST_XFER_DATA		= 4,
	GC_ST_PAD_OPEN_DIE	= 5,
	GC_ST_SPB_DONE		= 6,
	GC_ST_MAX
};

/*!
 * @brief gc state function pointer array
 */
fast_data static fsm_funs_t _gc_st_func[] = {
	{"get end page", ftl_core_gc_st_get_end_page},
	{"build p2l", ftl_core_gc_st_build_p2l},
	{"load p2l", ftl_core_gc_st_load_p2l},
	{"gen vpda", ftl_core_gc_st_gen_vpda},
	{"xfer data", ftl_core_gc_st_xfer_data},
	{"pad open", ftl_core_gc_st_pad_open_die},
	{"spb done", ftl_core_gc_st_spb_done},
};

/*!
 * @brief gc state machine states
 */
fast_data static fsm_state_t _gc_fsm_state = {
	.name = "gc_fsm",
	.fns = _gc_st_func,
	.max = ARRAY_SIZE(_gc_st_func)
};

fast_code void ftl_core_gc_l2p_ready_chk(u32 parm, u32 payload, u32 sts)
{
	gc_req_t *gc_req;

	if (shr_ftl_flags.b.l2p_all_ready) {
		gc_req = _ftl_core_gc.pend_req;
		_ftl_core_gc.pend_req = NULL;
		ftl_core_trace(LOG_WARNING, 0x5baf, "spb %d gc resumed ", gc_req->spb_id);

		ftl_core_gc_start(gc_req);
	} else {
		evt_set_cs(evt_l2p_ready_chk, 0, 0, CS_TASK);
	}
}

fast_code void evt_gc_load_p2l()
{
    ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	  if (gc_ctx->p2l_ttl_done > 0 && gc_ctx->p2l_ttl_done < gc_ctx->ttl_p2l_cnt){
	  	fsm_ctx_set(&gc_ctx->fsm, GC_ST_LOAD_P2L);  // load P2L from 6 12 .... to 1146
	    fsm_ctx_run(&gc_ctx->fsm);
	    gc_issue_load_p2l = false;
	    //shr_gc_rel_du_cnt = shr_host_write_cnt;  // cpu2
	    //shr_gc_rel_du_cnt += shr_host_write_cnt;  // cpu2
	    //ftl_core_trace(LOG_ALW, 0, "rel %d intv %d fc %d ttl %d  p2l %d ",shr_gc_rel_du_cnt,shr_host_write_cnt,shr_host_write_cnt,_fc_credit,gc_ctx->p2l_ttl_done,gc_ctx->p2l_ping_pong_cnt);
	  }
}

init_code void gc_scan_meta_init(void)	//dynamic grouping calculation
{	
	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan; 
	//caculate the best grp_cnt for the twice scanning
	spb_scan->scan_WL = shr_p2l_grp_cnt >> 1;
	u32 grp_cnt = 2;
	u32 prev_grp = shr_p2l_grp_cnt + 1;
	while((grp_cnt + spb_scan->scan_WL) < prev_grp)
	{
	    prev_grp = spb_scan->scan_WL + grp_cnt;
        grp_cnt = grp_cnt << 1;
        spb_scan->scan_WL = spb_scan->scan_WL >> 1;
	}
	spb_scan->scan_WL = max((spb_scan->scan_WL << 1), (grp_cnt >> 1));

    gc_scan_dummy_dtag = ddr_dtag_register(1);
    gc_scan_meta = idx_meta_allocate(spb_scan->scan_WL, DDR_IDX_META, &gc_scan_meta_idx);
}

init_code void ftl_core_gc_spb_scan_init(void)
{
	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;
	spb_scan->scan_stage = 0;
	spb_scan->era_du_cnt = 0;
	spb_scan->scan_start = 0;
	u32 ttl_size = sizeof(pda_t) * spb_scan->scan_WL;
	spb_scan->pda_list = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->pda_list);
	ttl_size = sizeof(bm_pl_t) * spb_scan->scan_WL;
	spb_scan->bm_pl_list = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->bm_pl_list);
	ttl_size = sizeof(struct info_param_t) * spb_scan->scan_WL;
	spb_scan->info_list = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->info_list);
	ttl_size = sizeof(struct ncl_cmd_t) * spb_scan->scan_WL;
	spb_scan->ncl_cmd = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->ncl_cmd);

	memset(spb_scan->info_list, 0, ttl_size); 
    memset(spb_scan->ncl_cmd, 0, ttl_size);

	u32 i;
	for (i = 0; i < spb_scan->scan_WL; i++)
	{
		struct ncl_cmd_t *ncl_cmd = &spb_scan->ncl_cmd[i];
		pda_t *pda_list = &spb_scan->pda_list[i];
		bm_pl_t *bm_pl_list = &spb_scan->bm_pl_list[i];
		struct info_param_t *info_list = &spb_scan->info_list[i];

		ncl_cmd->user_tag_list = bm_pl_list;
		ncl_cmd->addr_param.common_param.pda_list = pda_list;
		ncl_cmd->addr_param.common_param.info_list = info_list;
		ncl_cmd->addr_param.common_param.list_len = 1;

		ncl_cmd->status = 0;
		ncl_cmd->op_code = NCL_CMD_OP_READ;
		ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
		ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
		ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;

        #if RAID_SUPPORT_UECC
	    ncl_cmd->uecc_type = NCL_UECC_NORMAL_RD;
        #endif


		bm_pl_list[0].pl.btag = 0;
		bm_pl_list[0].pl.du_ofst = 0;
		bm_pl_list[0].pl.nvm_cmd_id = i + gc_scan_meta_idx;
		bm_pl_list[0].pl.dtag = gc_scan_dummy_dtag | DTAG_IN_DDR_MASK;;
		bm_pl_list[0].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
	}



	///////////////////////////////////////////////////////////////////////////////////////////////////
    //gc lda list from p2l
    /*
	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;
	u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;
	u32 intlv_du_cnt = nand_interleave_num() * DU_CNT_PER_PAGE;

	u32 ttl_size = sizeof(pda_t) * intlv_du_cnt;
	spb_scan->pda_list = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->pda_list);

	ttl_size = sizeof(bm_pl_t) * intlv_du_cnt;
	spb_scan->bm_pl_list = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->bm_pl_list);

	ttl_size = sizeof(struct info_param_t) * intlv_du_cnt;
	spb_scan->info_list = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->info_list);

	ttl_size = sizeof(struct ncl_cmd_t) * nand_info.lun_num;
	spb_scan->ncl_cmd = sys_malloc(SLOW_DATA, ttl_size);
	sys_assert(spb_scan->ncl_cmd);



	//one dtag per die
	spb_scan->dtag_start = ddr_dtag_register(nand_info.lun_num);
	sys_assert(rd_dummy_meta_idx != ~0);

	u32 i, j;
	for (i = 0; i < nand_info.lun_num; i++) {
		struct ncl_cmd_t *ncl_cmd = &spb_scan->ncl_cmd[i];
		pda_t *pda_list = &spb_scan->pda_list[i * mp_du_cnt];
		bm_pl_t *bm_pl_list = &spb_scan->bm_pl_list[i * mp_du_cnt];
		struct info_param_t *info_list = &spb_scan->info_list[i * mp_du_cnt];

		ncl_cmd->user_tag_list = bm_pl_list;
		ncl_cmd->addr_param.common_param.pda_list = pda_list;
		ncl_cmd->addr_param.common_param.info_list = info_list;
		ncl_cmd->addr_param.common_param.list_len = mp_du_cnt;

		ncl_cmd->status = 0;
		ncl_cmd->op_code = NCL_CMD_OP_READ;
		ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
		ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
		ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;


		for (j = 0; j < mp_du_cnt; j++) {
			bm_pl_list[j].pl.btag = i;
			bm_pl_list[j].pl.du_ofst = j;
			bm_pl_list[j].pl.nvm_cmd_id = rd_dummy_meta_idx;
			bm_pl_list[j].pl.dtag = (spb_scan->dtag_start + i) | DTAG_IN_DDR_MASK;
			bm_pl_list[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
		}
	}
    */
}

init_code void ftl_core_gc_init(void)
{
	switch(nand_interleave_num())
	{
		case 128:		//2T
		{
			gc_next_blk_cost = 1840;  //92k IOPS * 20ms [time from gc finish (pad open die) to next gc start]
			//gc_max_alloc_wr = 384;  // 8die
			gc_gen_p2l_num = NUM_P2L_GEN_PDA;
			break;
		}
		case 64:		//1T
		{
			gc_next_blk_cost = 1400;  //70k IOPS * 20ms
			//gc_max_alloc_wr = 192;  // 4die
			gc_gen_p2l_num = NUM_P2L_GEN_PDA;
			break;
		}
		case 32:		//512G
		{
			gc_next_blk_cost = 660;  //33k IOPS * 20ms
			//gc_max_alloc_wr = 96;  // 2die
			gc_gen_p2l_num = NUM_P2L_GEN_PDA / 2;
			break;
		}
		default:
		{
			panic("nand_interleave_num() not in range!");
			break;
		}
	}
	shr_max_alloc_wl_speed = DU_CNT_PER_P2L * get_p2l_per_grp() * 11 / 4;  //max WL speed: gc exec 1 P2L, host wr 2.75 P2L
	_ftl_core_gc.pend_req = NULL;
	_ftl_core_gc.gc_action = NULL;
	_ftl_core_gc.spb_id = INV_SPB_ID;
	_ftl_core_gc.suspend_spb = INV_SPB_ID;
	_ftl_core_gc.meta = &((io_meta_t *)shr_ddtag_meta)[shr_gc_ddtag_start];
	gc_meta = _ftl_core_gc.meta;

	u32 ttl_rsp = DTAG_CNT_PER_GRP * MAX_GC_PDA_GRP;
	_ftl_core_gc.pda_list = sys_malloc(SLOW_DATA, sizeof(pda_t) * ttl_rsp);
	sys_assert(_ftl_core_gc.pda_list);
	gc_pda_list = _ftl_core_gc.pda_list;
    //============gc lda list from p2l=====================
	_ftl_core_gc.lda_list = sys_malloc(SLOW_DATA, sizeof(pda_t) * ttl_rsp);
	sys_assert(_ftl_core_gc.lda_list);
	gc_lda_list = _ftl_core_gc.lda_list;
    //============gc lda list from p2l=====================

	_ftl_core_gc.tmp_cnt=0;
	_ftl_core_gc.pend_cnt=0;
	//_ftl_core_gc.pend_buf = sys_malloc(FAST_DATA, sizeof(bm_pl_t) * PEND_BUFF_CNT);
    _ftl_core_gc.pend_buf = (bm_pl_t *)ddtag2mem(ddr_dtag_register(((sizeof(bm_pl_t)*PEND_BUFF_CNT)/4096)+1));
    sys_assert(_ftl_core_gc.pend_buf);
	_ftl_core_gc.p2l_ping_pong_cnt=0;
	_ftl_core_gc.p2l_not_ready=true;

	//_ftl_core_gc.tmp_buf = sys_malloc(FAST_DATA, sizeof(bm_pl_t) * FCORE_GC_DTAG_CNT/2);
	//sys_assert(_ftl_core_gc.tmp_buf);

	//remote group src data queue init
	gc_src_data_que = &_gc_src_data_que;

	CBF_INIT(gc_src_data_que);
	gc_src_data_recv_hook((cbf_t *)gc_src_data_que, ftl_core_gc_fetch_src_data);

	//remote group resource free queue init
	gc_res_free_que = &_gc_res_free_que;

	u32 i;
	u32 p2l_dtag_start;
	p2l_load_req_t *p2l_load = &_ftl_core_gc.p2l_load;
	p2l_build_req_t *p2l_build = &_ftl_core_gc.p2l_build;
	u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;

	//alloc dtag for p2l load
	//p2l_dtag_start = ddr_dtag_register(mp_du_cnt);
	u32 tzu_pingpong_double_p2l = 12;
	p2l_dtag_start = ddr_dtag_register(tzu_pingpong_double_p2l);
	//ftl_core_trace(LOG_ALW, 0, "p2l load dtag start %d", p2l_dtag_start);
	//for (i = 0; i < mp_du_cnt; i++)
	for (i = 0; i < tzu_pingpong_double_p2l; i++)
		p2l_load->dtags[i].dtag = (p2l_dtag_start + i) | DTAG_IN_DDR_MASK;

	p2l_load->flags.all = 0;

	//alloc dtag for p2l build
	p2l_dtag_start = ddr_dtag_register(mp_du_cnt * 2);
	//ftl_core_trace(LOG_ALW, 0, "p2l build dtag start %d", p2l_dtag_start);
	for (i = 0; i < mp_du_cnt * 2; i++)
		p2l_build->dtags[i].dtag = (p2l_dtag_start + i) | DTAG_IN_DDR_MASK;

	//init spb scan resouce and context

    //p2l_scan_dtag = ddr_dtag_register(24);
    gc_scan_meta_init();
	ftl_core_gc_spb_scan_init();
	shr_gc_fc_ctrl = false;
	_fc_credit = 0;
	du_cnt_per_die = nand_plane_num() * nand_bits_per_cell() * DU_CNT_PER_PAGE;
	//gc_start_next_fc_cnt = 5;
	scheduler_gc_grp_init(0, &evt_pda_gen_rsp0_que0);
	evt_register(ftl_core_gc_l2p_ready_chk, 0, &evt_l2p_ready_chk);
    evt_register(evt_ftl_core_gc_p2l_next, 0, &evt_p2l_load);
    evt_register(evt_pend_out_check, 0, &evt_pend_out);
    evt_register(evt_gc_load_p2l, 0, &evt_gc_issue_load_p2l);
}
fast_code void tzu_get_gc_info()
{
	//ftl_core_trace(LOG_ERR, 0, "tzu use func to get info from cpu 2");
	ftl_core_trace(LOG_ERR, 0x6e44, "gc state %d, write cnt %d, avail cnt %d, pend cnt %d tmp:%d",_ftl_core_gc.fsm.state_cur, _ftl_core_gc.write_cnt, gc_scheduler.ddtag_avail,_ftl_core_gc.pend_cnt, _ftl_core_gc.tmp_cnt);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	ftl_core_trace(LOG_ERR, 0x8fe9, "grp 0 V %d D %d, grp 1 V %d D %d, dummy: %d, defect: %d",gc_pda_grp[0].vcnt,gc_pda_grp[0].done,gc_pda_grp[1].vcnt,gc_pda_grp[1].done,gc_pda_grp[0].dummy_cnt,gc_pda_grp[0].defect_cnt>>1);
#else
	ftl_core_trace(LOG_ERR, 0x027d, "grp 0 V %d D %d, grp 1 V %d D %d",gc_pda_grp[0].vcnt,gc_pda_grp[0].done,gc_pda_grp[1].vcnt,gc_pda_grp[1].done);
#endif

	//cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_TZU_GET, 0, 0);
}

fast_code bool is_gc_write_done_when_plp()
{
    #if 0
	if(gc_pda_grp[0].vcnt == gc_pda_grp[0].done && gc_pda_grp[1].vcnt == gc_pda_grp[1].done)
	{
		//ftl_core_trace(LOG_INFO, 0, "gc write is truly done");
		return true;
	}
    #endif
    if((plp_gc_suspend_done) || ((gc_pda_grp[0].done + gc_pda_grp[1].done) == _ftl_core_gc.write_cnt))
    {
		return true;
    }
	return false;
}
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
slow_code void ftl_core_gc_slc_rd(void)
{
	fsm_ctx_set(&_ftl_core_gc.fsm, GC_ST_GEN_VPDA);
	u8	* df_ptr;
	df_ptr = get_spb_defect(PLP_SLC_BUFFER_BLK_ID);
	gc_slc_raid_calculate(df_ptr);
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_GC_PLP_SLC, 0, 1);
	issue_gc_read_slc(0);
}
#endif


fast_code spb_id_t fc_get_gc_spb(void)
{
	return _ftl_core_gc.spb_id;
}

fast_code void ftl_core_gc_start(gc_req_t *gc_req)
{
	u32 i;
	volatile gc_pda_grp_t *gc_grp;
	u32 sn = gc_req->sn;
	spb_id_t spb_id = gc_req->spb_id;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	p2l_load_req_t *p2l_load = &gc_ctx->p2l_load;

	//To prevent panic when GC suspend to do WL, but enter other mode
	if(gc_suspend_start_wl)
		gc_suspend_start_wl = false;

	if (shr_ftl_flags.b.l2p_all_ready == false) {
		gc_ctx->pend_req = gc_req;
		evt_set_cs(evt_l2p_ready_chk, 0, 0, CS_TASK);
		//ftl_core_trace(LOG_WARNING, 0, "spb %d gc pend as l2p not ready ", gc_req->spb_id);
		return;
	}

//	_ftl_core_gc.attr.all = 0;
//	_ftl_core_gc.flags.all = 0;
//	_ftl_core_gc.write_cnt = 0;

	_ftl_core_gc.spb_sn = sn;
	_ftl_core_gc.spb_id = spb_id;
	_ftl_core_gc.attr.b.dslc = gc_req->flags.b.dslc;
	_ftl_core_gc.attr.b.spb_open = gc_req->flags.b.spb_open;
	_ftl_core_gc.attr.b.spb_warmboot_open = gc_req->flags.b.spb_warmboot_open;
	_ftl_core_gc.attr.b.spb_spor_open = gc_req->flags.b.spb_spor_open;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	_ftl_core_gc.attr.b.slc = (ftl_core_get_spb_type(spb_id) || spb_id == PLP_SLC_BUFFER_BLK_ID);
#else
	_ftl_core_gc.attr.b.slc = ftl_core_get_spb_type(spb_id);
#endif
	_ftl_core_gc.p2l_not_ready=true;
	//_ftl_core_gc.spb_ttl_du = nand_interleave_num() * DU_CNT_PER_PAGE;
	_ftl_core_gc.vgen_p2l_cnt = 0;
	_ftl_core_gc.vgen_p2l_done_cnt = 0;
    shr_gc_rel_du_cnt = shr_host_write_cnt;
    //gc_load_p2l_time = jiffies;
    ftl_core_trace(LOG_ALW, 0xb8d1, "hostw %d bew %d fc %d intv %d h flag %d",shr_hostw_perf,shr_bew_perf,_fc_credit,shr_host_write_cnt,shr_no_host_write);
	if(shr_gc_read_disturb_ctrl)
		gc_read_disturb_time=get_tsc_64();//gc_read_disturb_time= jiffies;//
    shr_no_host_write = 0;

	/*if (_ftl_core_gc.attr.b.slc && gc_req->flags.b.dslc == false)
		_ftl_core_gc.spb_ttl_du *= nand_page_num_slc();
	else
		_ftl_core_gc.spb_ttl_du *= nand_page_num();*/

	for (i = 0; i < MAX_GC_PDA_GRP; i++) {
		gc_grp = &gc_pda_grp[i];
		gc_grp->vcnt = 0;
		gc_grp->done = 0;
		gc_grp->reading = 0;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		gc_grp->dummy_cnt = 0;
		gc_grp->defect_cnt = 0;
#endif
		gc_grp->spb_id = spb_id;
		gc_grp->flags.all = 0;
		gc_grp->flags.b.slc = _ftl_core_gc.attr.b.slc;
	}
	gc_ctx->suspend_spb = INV_SPB_ID;
	gc_issue_load_p2l = false;
/*#ifdef WCMD_DROP_SEMI
	extern u32 dropsemi;
	if (dropsemi)
		gc_start_next_fc_cnt = 50;	// ISU, QD1 performance, to cover the time of gc start next p2l 
	else
#endif		
		gc_start_next_fc_cnt = 5;*/
	if ((gc_ctx->suspend_spb != spb_id) || (gc_ctx->suspend_spb_sn != sn))
	{
        _ftl_core_gc.write_cnt = 0;
		p2l_load->p2l_cnt = 0;
		p2l_load->spb_id = spb_id;
		p2l_load->flags.all = 0;
		p2l_load->p2l_dummy_grp_idx = 0xFF;
		gc_ctx->p2l_done_cnt = 0;

		if(gc_req->flags.b.wl_suspend && gc_suspend_spb_sn == sn) // add sn check to avoid mismatch between grp_id and SPB
		{
			_ftl_core_gc.p2l_ping_pong_cnt = gc_suspend_grp_id;
			p2l_load->grp_id = gc_suspend_grp_id;
			p2l_load->p2l_id = gc_suspend_grp_id * 3;
			gc_ctx->p2l_ttl_done = gc_suspend_grp_id * 3;
			_ftl_core_gc.attr.b.wl_suspend = true;
		}
		else
		{
			_ftl_core_gc.p2l_ping_pong_cnt = 0;
			p2l_load->grp_id = 0;
			p2l_load->p2l_id = 0;
			gc_ctx->p2l_ttl_done = 0;
		}
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(spb_id != PLP_SLC_BUFFER_BLK_ID)
			ftl_core_gc_fsm_run(&_ftl_core_gc.fsm);
		else
		{
			gc_pda_grp[0].slc_pda = shr_plp_slc_start_pda;
			gc_pda_grp[1].slc_pda = shr_plp_slc_start_pda;
			fsm_ctx_init(&_ftl_core_gc.fsm, &_gc_fsm_state, NULL, NULL);
			ftl_core_gc_slc_rd();
		}
#else
		ftl_core_gc_fsm_run(&_ftl_core_gc.fsm);
#endif
	} else {
		gc_ctx->suspend_spb = INV_SPB_ID;
		//ftl_core_trace(LOG_ALW, 0, "GC resume %d st %d %d/%d",
		//		spb_id, gc_ctx->suspend_fsm_st, gc_ctx->p2l_gen_idx, gc_ctx->p2l_load.grp_id);
		fsm_ctx_set(&_ftl_core_gc.fsm, gc_ctx->suspend_fsm_st);
		fsm_ctx_run(&_ftl_core_gc.fsm);
	}
}

fast_code void ftl_core_gc_fsm_run(fsm_ctx_t *fsm)
{
	fsm_ctx_init(fsm, &_gc_fsm_state, NULL, NULL);
	fsm_ctx_run(fsm);
}

fast_code void ftl_core_gc_p2l_load_done(void *ctx)
{
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	++gc_ctx->p2l_ping_pong_cnt;

	#if NUM_P2L_GEN_PDA == 1
	if( gc_ctx->p2l_not_ready==true)
	#else
	if( gc_ctx->p2l_not_ready==true && gc_ctx->p2l_ping_pong_cnt == gc_ctx->p2l_load.grp_id)
	#endif
	{
		gc_ctx->p2l_not_ready=false;
		fsm_ctx_t *fsm = &gc_ctx->fsm;

		if(gc_suspend_abort_check(GC_ST_GEN_VPDA)){
			fsm_ctx_set(fsm, GC_ST_PAD_OPEN_DIE);
			fsm_ctx_run(fsm);
		}
		else{
			fsm_ctx_next(fsm);

		    fsm_ctx_run(fsm);
		    if(gc_ctx->p2l_load.grp_id < gc_ctx->ttl_grp_cnt)	// ISU, GC_mod_for_EH
		    {
			    evt_ftl_core_gc_p2l_next(0, 0 ,0);
			}
		}
	}
}


slow_code void ftl_core_gc_get_ttl_p2l(void)
{
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;

	//closed spb
	if (gc_ctx->attr.b.spb_open == false) 
	{
		gc_ctx->ttl_p2l_cnt = get_xlc_p2l_cnt();;

		gc_ctx->ddr_p2l_cnt = 0;
		gc_ctx->nand_p2l_cnt = gc_ctx->ttl_p2l_cnt;
		return;
	}

	//no valid data on spb
	if ((gc_ctx->last_ok_page == ~0) && (gc_ctx->last_err_page == ~0)) {
		gc_ctx->ttl_p2l_cnt = 0;
		gc_ctx->ddr_p2l_cnt = 0;
		gc_ctx->nand_p2l_cnt = 0;
		return;
	}

	//get ttl p2l cnt by last page
	if (gc_ctx->last_err_page != ~0)
		gc_ctx->ttl_p2l_cnt = page_to_p2l_id(gc_ctx->last_err_page) + 1;
	else
		gc_ctx->ttl_p2l_cnt = page_to_p2l_id(gc_ctx->last_ok_page) + 1;

	if (gc_ctx->last_ok_page == ~0) {
		gc_ctx->nand_p2l_cnt = 0;
	} else {
		u32 grp_id = page_to_p2l_grp_id(gc_ctx->last_ok_page);
		u32 grp_last_page = p2l_get_grp_last_page(grp_id, gc_ctx->attr.b.slc);
		//p2l grp closed
		if (gc_ctx->last_ok_page == grp_last_page) {
			gc_ctx->nand_p2l_cnt = page_to_p2l_id(gc_ctx->last_ok_page) + 1;
		} else {
			if (grp_id == 0) {
				gc_ctx->nand_p2l_cnt = 0;
			} else {
				grp_last_page = p2l_get_grp_last_page(grp_id - 1, gc_ctx->attr.b.slc);
				gc_ctx->nand_p2l_cnt = page_to_p2l_id(grp_last_page) + 1;
			}
		}
	}

	gc_ctx->ddr_p2l_cnt = gc_ctx->ttl_p2l_cnt - gc_ctx->nand_p2l_cnt;
	//ftl_core_trace(LOG_INFO, 0, "open spb ttl p2l %d nand p2l %d ddr p2l %d",
	//	gc_ctx->ttl_p2l_cnt, gc_ctx->nand_p2l_cnt, gc_ctx->ddr_p2l_cnt);
}

slow_code void gc_scan_read_done(struct ncl_cmd_t *ncl_cmd)
{
	struct info_param_t *info_list = ncl_cmd->addr_param.common_param.info_list;
	pda_t pda = ncl_cmd->addr_param.common_param.pda_list[0];
	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	p2l_load_req_t *load = &gc_ctx->p2l_load;
	u32 meta_idx = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;

	meta_idx -= gc_scan_meta_idx;
	spb_scan->otf_read--;
	if(ncl_cmd->flags & NCL_CMD_SCH_FLAG)
    {
        decrease_otf_cnt(ncl_cmd->die_id);
        ncl_cmd->flags &= ~NCL_CMD_SCH_FLAG;
    }
	static u8 dummy_cnt = 0;
	static bool exit_plp_tag = false;
	//evlog_printk(LOG_ERR,"tzu gc_scan_read_done status %d",info_list[0].status);
	//ftl_core_trace(LOG_INFO, 0, "status 0x%x 0x%x pda 0x%x, wl 0x%x", ncl_cmd->status, info_list[0].status,
    //    pda, ((pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask)/shr_nand_info.bit_per_cell);
	if(info_list[0].status != cur_du_erase)
	{
		spb_scan->p2l_exit++;
		//ftl_core_trace(LOG_INFO, 0, "gc_scan_meta[%d].lda------0x%x",meta_idx, gc_scan_meta[meta_idx].lda);
		//if (info_list[0].status == ficu_err_du_erased)//blank
		//spb_scan->era_du_cnt++;
		if(info_list[0].status) 
			ftl_core_trace(LOG_INFO, 0xe22c, "scan read error, skip it! pda: %d, status: %d", pda, info_list[0].status); 
		if(gc_scan_meta[meta_idx].lda != P2L_LDA && gc_scan_meta[meta_idx].lda != FTL_PLP_TAG && !info_list[0].status) 
		{ 
			dummy_cnt ++; 
		}
		else if(gc_scan_meta[meta_idx].lda == FTL_PLP_TAG)
		{
			exit_plp_tag = true;
		}
	}


	if(spb_scan->otf_read == 0)
	{
		//evlog_printk(LOG_INFO,"tzu no cmd otf p2l exit %d start %d",spb_scan->p2l_exit,spb_scan->scan_start);
		ftl_core_trace(LOG_INFO, 0x4220, "tzu gc scan p2l exit %d start %d dummy %d",spb_scan->p2l_exit,spb_scan->scan_start,dummy_cnt);
		if(spb_scan->scan_stage == 0)
		{
			if(gc_ctx->attr.b.spb_spor_open)
				spb_scan->scan_start = (spb_scan->p2l_exit - dummy_cnt) * spb_scan->scan_WL;
			else if(dummy_cnt > 0)
				spb_scan->scan_start = (spb_scan->p2l_exit - dummy_cnt) * spb_scan->scan_WL + spb_scan->scan_WL / 2;
			else
				spb_scan->scan_start = spb_scan->p2l_exit * spb_scan->scan_WL;
			spb_scan->scan_stage++;			
			ftl_core_gc_scan_read(spb_scan->scan_start);
			spb_scan->p2l_exit = 0;
			dummy_cnt = 0;
		}
		else
		{
			gc_ctx->ttl_grp_cnt = spb_scan->scan_start + spb_scan->p2l_exit - dummy_cnt; // ISU, EH_GC_not_done  //valid total p2l cnt
#if(PLP_SUPPORT == 0)
    		epm_FTL_t* epm_ftl_data; 
    		epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag); 
			if(gc_ctx->spb_id == epm_ftl_data->host_open_blk[0])
				load->p2l_dummy_grp_idx = epm_ftl_data->host_aux_group;
			else if(gc_ctx->spb_id == epm_ftl_data->gc_open_blk[0])
				load->p2l_dummy_grp_idx = epm_ftl_data->gc_aux_group;
			else
#endif 
				load->p2l_dummy_grp_idx = gc_ctx->ttl_grp_cnt;   //the first p2l which need aux build or load p2l in last grp
			if(gc_ctx->attr.b.spb_spor_open)
			{
				load->flags.b.last_wl_p2l = true;
			}
			else if((!exit_plp_tag) && (!gc_ctx->attr.b.spb_warmboot_open))
			{
				load->flags.b.p2l_dummy = true;
#if(PLP_SUPPORT == 1 && SPOR_FILL_DUMMY == mENABLE)
				if(dummy_cnt != 0)
#else
				if(gc_ctx->ttl_grp_cnt < shr_p2l_grp_cnt)
#endif
					gc_ctx->ttl_grp_cnt++;
			}
			if(gc_ctx->ttl_grp_cnt < shr_p2l_grp_cnt)
			{
				if(gc_ctx->attr.b.spb_warmboot_open)
				{
					u16 adjust_grp_cnt = gc_ctx->ttl_grp_cnt + ((shr_p2l_grp_cnt - gc_ctx->ttl_grp_cnt) >> 2);
					shr_host_write_cnt = shr_host_write_cnt * adjust_grp_cnt / gc_ctx->ttl_grp_cnt;  //readjust warmboot blk speed ctrl
				}
				else if(gc_ctx->attr.b.spb_spor_open || !exit_plp_tag)  //For have not fill dummy to next p2l case
					gc_ctx->ttl_grp_cnt++;
			}
			spb_scan->p2l_exit = 0;
			spb_scan->scan_stage = 0;
			spb_scan->scan_start = 0;
			dummy_cnt = 0;
			exit_plp_tag = false;
			if(gc_ctx->ttl_grp_cnt > 0 && gc_ctx->ttl_grp_cnt <= shr_p2l_grp_cnt)	// For scan all P2L case, ISU, EH_GC_not_done
			{
				fsm_ctx_set(&gc_ctx->fsm, GC_ST_LOAD_P2L);
				fsm_ctx_run(&gc_ctx->fsm);
			}
			else
			{
				ftl_core_trace(LOG_INFO, 0xaf17, "no p2l exit %d abort gc",gc_ctx->ttl_grp_cnt);//gc_ctx->ttl_grp_cnt == -1
				gc_ctx->flags.b.abort_done = true;
				fsm_ctx_set(&gc_ctx->fsm, GC_ST_SPB_DONE);
				fsm_ctx_run(&gc_ctx->fsm);
			}
		}

	}


	//ficu_err_du_erased

	/*
	if (info_list[0].status)
	{

	}
	else
	{

	}
//////////////////////////////
	u32 i;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;
	struct info_param_t *info_list = ncl_cmd->addr_param.common_param.info_list;

	u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;
	u32 intlv_du_cnt = nand_interleave_num() * DU_CNT_PER_PAGE;

	for (i = 0; i < mp_du_cnt; i++) {
		if (info_list[i].status) {
			spb_scan->err_du_cnt++;
			if (info_list[i].status == ficu_err_du_erased)
				spb_scan->era_du_cnt++;
		}
	}

	sys_assert(spb_scan->otf_read);
	spb_scan->otf_read--;
	if (spb_scan->otf_read)
		return;
*/
      /*
       * open spb gc is not a normal case, no need to suspend precisely, just abort it
	*/
	/*
	if (gc_suspend_abort_check(GC_ST_GET_END_PAGE))
	{
		fsm_ctx_set(&gc_ctx->fsm, GC_ST_SPB_DONE);
		fsm_ctx_run(&gc_ctx->fsm);
		return;
	}

	if (spb_scan->scan_stage == SPB_SCAN_LAST_OK_PAGE) {
		//update scan range
		if (spb_scan->err_du_cnt == 0)
			gc_ctx->last_ok_page = spb_scan->read_page;

		//continue to scan
		if ((spb_scan->last_page - spb_scan->start_page) > 1) {
			//update read page
			if (spb_scan->err_du_cnt == 0)
				spb_scan->start_page = spb_scan->read_page;
			else
				spb_scan->last_page = spb_scan->read_page;

			spb_scan->read_page = (spb_scan->start_page + spb_scan->last_page) >> 1;
		} else {
			//last ok page scan done, scan last err page
			spb_scan->scan_stage = SPB_SCAN_LAST_ERR_PAGE;

			if (gc_ctx->last_ok_page == ~0)
				spb_scan->read_page = 0;
			else
				spb_scan->read_page = gc_ctx->last_ok_page + 1;

			if (gc_ctx->attr.b.slc)
				spb_scan->last_page = nand_page_num_slc() - 1;
			else
				spb_scan->last_page = nand_page_num() - 1;
		}

		ftl_core_gc_scan_page();
	} else {
		sys_assert(spb_scan->err_du_cnt);
		//scan empty page or last page, scan done
		if ((spb_scan->era_du_cnt == intlv_du_cnt)
			|| (spb_scan->read_page == spb_scan->last_page)) {
			//restore log level
			ipc_api_log_level_chg(spb_scan->log_lvl);

			//ftl_core_trace(LOG_INFO, 0, "open spb last ok page 0x%x, last err page 0x%x",
			//	gc_ctx->last_ok_page, gc_ctx->last_err_page);

			ftl_core_gc_get_ttl_p2l();
			//fsm_ctx_set(&gc_ctx->fsm, GC_ST_BUILD_P2L);
			fsm_ctx_set(&gc_ctx->fsm, GC_ST_LOAD_P2L);
			fsm_ctx_run(&gc_ctx->fsm);
		} else {
			if (spb_scan->err_du_cnt < intlv_du_cnt)
				gc_ctx->last_err_page = spb_scan->read_page;

			spb_scan->read_page++;
			ftl_core_gc_scan_page();
		}
	}
	*/
}

extern enum du_fmt_t host_du_fmt;
slow_code void ftl_core_gc_scan_read(u32 start)
{

		ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
		ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;
		pda_t p2l_pda;
    	pda_t pgsn_pda;
		u32 total_WL=spb_scan->scan_WL;
		u32 i = 0;
		u32 j = 0;
		u32 grp_id = 0;
		u32 die = 0;
		//u32 jump[]={63,127,191,255,319};
		if(plp_trigger)
		{
			gc_ctx->flags.b.suspend_done = true;
			fsm_ctx_set(&_ftl_core_gc.fsm, GC_ST_SPB_DONE);
			fsm_ctx_run(&_ftl_core_gc.fsm);
			return;
		}
		if(spb_scan->scan_stage == 0)
		{
			//total_WL=15;
			total_WL=(shr_p2l_grp_cnt / spb_scan->scan_WL - 1);
			if(shr_p2l_grp_cnt % spb_scan->scan_WL)
				total_WL ++; 
			sys_assert(total_WL <= spb_scan->scan_WL);
		}

		for (i = start; i < start+total_WL; i++)
		{
				struct ncl_cmd_t *ncl_cmd = &spb_scan->ncl_cmd[j];
				pda_t *pda_list = ncl_cmd->addr_param.common_param.pda_list;
				struct info_param_t *info_list = ncl_cmd->addr_param.common_param.info_list;


				info_list[0].status = 0;
				info_list[0].pb_type = NAL_PB_TYPE_XLC;
				grp_id = i;
				if(spb_scan->scan_stage == 0)
				{
					//p2l=((i+1)*24)-1;
					grp_id=((i+1)*spb_scan->scan_WL)-1;
				}
				sys_assert(grp_id < shr_p2l_grp_cnt);
				p2l_get_grp_pda(grp_id, gc_ctx->spb_id, 1, &p2l_pda, &pgsn_pda);
				pda_list[0] = p2l_pda;//ftl_remap_pda(pda + j);
				die = pda2die(p2l_pda);

		        ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
				ncl_cmd->caller_priv = (void*)spb_scan;
				ncl_cmd->completion = gc_scan_read_done;
				ncl_cmd->du_format_no = host_du_fmt;
				ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
				ncl_cmd->flags |= NCL_CMD_SCH_FLAG;
				ncl_cmd->die_id = die;

            	#if RAID_SUPPORT_UECC
	            ncl_cmd->uecc_type = NCL_UECC_NORMAL_RD;
                #endif		

				spb_scan->otf_read++;
				j++;
				//evlog_printk(LOG_ERR,"tzu submit scan %d op %d pda %d",i,ncl_cmd->op_code,p2l_pda);
				//ftl_core_trace(LOG_INFO, 0, "gc open_blk submit scan %d op %d pda 0x%x", grp_id, ncl_cmd->op_code, p2l_pda);
				//if (get_target_cpu(die) == CPU_ID)
				//	ncl_cmd_submit(ncl_cmd);
				//else
				//	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD, 0, (u32)ncl_cmd);
				if (get_target_cpu(die) == CPU_ID)
					ncl_cmd_submit_insert_schedule(ncl_cmd, false);
				else
					cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD_INSERT_SCH, 0, (u32)ncl_cmd);
		}
//////////////////////////////////////////////////////////////////////
/*
	u32 i, j;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;

	u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;
	u32 intlv_du_cnt = nand_interleave_num() * DU_CNT_PER_PAGE;
	pda_t start_pda = nal_make_pda(gc_ctx->spb_id, spb_scan->read_page * intlv_du_cnt);

	u32 spb_type = NAL_PB_TYPE_XLC;
	if (gc_ctx->attr.b.slc)
		spb_type = NAL_PB_TYPE_SLC;

	spb_scan->err_du_cnt = 0;
	spb_scan->era_du_cnt = 0;
	spb_scan->otf_read = nand_info.lun_num;

	for (i = 0; i < nand_info.lun_num; i++) {
		struct ncl_cmd_t *ncl_cmd = &spb_scan->ncl_cmd[i];
		pda_t *pda_list = ncl_cmd->addr_param.common_param.pda_list;
		struct info_param_t *info_list = ncl_cmd->addr_param.common_param.info_list;

		pda_t pda = start_pda + i * mp_du_cnt;
		for (j = 0; j < mp_du_cnt; j++) {
			info_list[j].status = 0;
			info_list[j].pb_type = spb_type;
			pda_list[j] = ftl_remap_pda(pda + j);
		}
        ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
		ncl_cmd->caller_priv = (void*)spb_scan;
		ncl_cmd->completion = gc_scan_read_done;


		if (get_target_cpu(i) == CPU_ID)
			ncl_cmd_submit(ncl_cmd);
		else
			cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD, 0, (u32)ncl_cmd);
	}
	*/
}


slow_code void ftl_core_gc_p2l_build_cmpl(void *ctx)
{
	u16 next;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;

	if (gc_ctx->nand_p2l_cnt)
		next = GC_ST_LOAD_P2L;
	else
		next = GC_ST_GEN_VPDA;

	if (gc_ctx->flags.b.abort || gc_ctx->flags.b.suspend || plp_trigger || gc_suspend_start_wl) {
		if (gc_ctx->flags.b.suspend || gc_suspend_start_wl) {
			gc_ctx->flags.b.suspend_done = true;
			gc_ctx->suspend_fsm_st = next;
			gc_ctx->suspend_spb = gc_ctx->spb_id;
			gc_ctx->suspend_spb_sn = gc_ctx->spb_sn;
		}

		next = GC_ST_SPB_DONE;
	}

	fsm_ctx_set(&gc_ctx->fsm, next);
	fsm_ctx_run(&gc_ctx->fsm);
}

fast_code fsm_res_t ftl_core_gc_st_get_end_page(fsm_ctx_t *fsm)
{
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	//ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;

	//gc_ctx->last_ok_page = ~0;
	//gc_ctx->last_err_page = ~0;

	if (gc_ctx->attr.b.spb_open == false) 
	{
		//gc_ctx->last_ok_page = nand_page_num() - 1;
		gc_ctx->ttl_grp_cnt = shr_p2l_grp_cnt;//ftl_core_gc_get_ttl_p2l(); adams 64 die : 384 / 2 = 192
		fsm_ctx_set(fsm, GC_ST_LOAD_P2L);
		return FSM_JMP;
	}

	//change log level as scan will report much err log
	//spb_scan->log_lvl =  ipc_api_log_level_chg(LOG_ALW);

	//spb_scan->start_page = 0;
	//spb_scan->last_page = nand_page_num() - 1;
	//if (gc_ctx->attr.b.slc)
	//	spb_scan->last_page = nand_page_num_slc() - 1;

	//spb_scan->scan_stage = SPB_SCAN_LAST_OK_PAGE;
	//spb_scan->read_page = (spb_scan->start_page + spb_scan->last_page) >> 1;
	ftl_core_gc_scan_read(0);
	return FSM_PAUSE;
}

ddr_code fsm_res_t ftl_core_gc_st_build_p2l(fsm_ctx_t *fsm)
{
	//ftl_core_trace(LOG_ERR, 0, "gc not matain this state");
	sys_assert(0);
	/*
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	p2l_build_req_t *p2l_build = &gc_ctx->p2l_build;

	//no valid data
	if (gc_ctx->ttl_p2l_cnt== 0) {
		//ftl_core_trace(LOG_ERR, 0, "gc spb %d no valid data", gc_ctx->spb_id);
		fsm_ctx_set(&gc_ctx->fsm, GC_ST_SPB_DONE);
		return FSM_JMP;
	}

	//no need to build
	if (gc_ctx->ddr_p2l_cnt == 0) {
		fsm_ctx_set(&gc_ctx->fsm, GC_ST_LOAD_P2L);
		return FSM_JMP;
	}

	//get p2l build range
	p2l_build->spb_id = gc_ctx->spb_id;
	p2l_build->start_page = gc_ctx->nand_p2l_cnt * get_page_per_p2l();
	if (gc_ctx->last_err_page != ~0)
		p2l_build->last_page = gc_ctx->last_err_page;
	else
		p2l_build->last_page = gc_ctx->last_ok_page;

	p2l_build->ctx = NULL;
	p2l_build->cmpl = ftl_core_gc_p2l_build_cmpl;
	p2l_build_push(p2l_build);
	*/
	return FSM_PAUSE;
}

fast_code bool gc_suspend_abort_check(u16 state)
{
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(gc_ctx->spb_id == PLP_SLC_BUFFER_BLK_ID)
	{
		if(plp_trigger || gc_pro_fail)
		{
			gc_ctx->flags.b.suspend_done = true;
			gc_ctx->suspend_fsm_st = state;//gc_ctx->fsm.state_cur;//fsm->state_cur + 1;
			gc_ctx->suspend_spb = gc_ctx->spb_id;
			gc_ctx->suspend_spb_sn = gc_ctx->spb_sn;
			return true;
		}
		else
			return false;
	}
	else
#endif
	if(gc_ctx->flags.b.suspend || plp_trigger || gc_suspend_start_wl)
	{
		//if(!plp_trigger)		
		//	ftl_core_trace(LOG_ALW, 0, "gc %d %d %d suspend check state %d",gc_ctx->spb_id,gc_ctx->p2l_ttl_done,gc_ctx->write_cnt,state);		

		gc_ctx->flags.b.suspend_done = true;
		gc_ctx->suspend_fsm_st = state;//gc_ctx->fsm.state_cur;//fsm->state_cur + 1;
		gc_ctx->suspend_spb = gc_ctx->spb_id;
		gc_ctx->suspend_spb_sn = gc_ctx->spb_sn;
		return true;
	}
	else if (gc_ctx->flags.b.abort)
	{
		gc_ctx->flags.b.abort_done = true;
		return true;
	}

	return false;
}

fast_code fsm_res_t ftl_core_gc_st_load_p2l(fsm_ctx_t *fsm)
{
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	p2l_load_req_t *load = &gc_ctx->p2l_load;
	//sys_assert(gc_ctx->flags.b.suspend == false);
	sys_assert(gc_ctx->flags.b.abort == false);
    //evlog_printk(LOG_INFO,"tzu grp %d",load->grp_id);
	if(gc_suspend_abort_check(GC_ST_LOAD_P2L))
	{
		fsm_ctx_set(fsm, GC_ST_PAD_OPEN_DIE);
		fsm_ctx_run(fsm);
	}
	else if(load->grp_id ==0 || gc_ctx->attr.b.wl_suspend)
	{
		//gc_ctx->ttl_grp_cnt = gc_ctx->ttl_p2l_cnt/get_p2l_per_grp();
		gc_ctx->ttl_p2l_cnt = gc_ctx->ttl_grp_cnt*3;
		ftl_core_trace(LOG_ALW, 0x682f, "[GC] p2l load total cnt %d, cur:%d, continue:%d",gc_ctx->ttl_grp_cnt, load->grp_id, gc_ctx->attr.b.wl_suspend);
		//evlog_printk(LOG_INFO,"tzu ttl grp %d %d",gc_ctx->ttl_grp_cnt,gc_ctx->ttl_p2l_cnt);
		gc_ctx->odd_p2l = false;
		gc_ctx->attr.b.wl_suspend = false;
		evt_ftl_core_gc_p2l_next(0, 0, 0);
	}
	else if(load->grp_id == gc_ctx->p2l_ping_pong_cnt)
	{
		fsm_ctx_next(fsm);
		fsm_ctx_run(fsm);

		if(gc_ctx->p2l_load.grp_id < gc_ctx->ttl_grp_cnt)	// ISU, GC_mod_for_EH
		{
			evt_ftl_core_gc_p2l_next(0, 0 ,0);
		}
		else {  // last P2L
			//if (shr_hostw_perf < 140000)  // only for QD1, multi QD will cause in GC emergency
		  #ifdef WCMD_DROP_SEMI
		  extern u32 dropsemi;
		  if (dropsemi)
			  shr_gc_rel_du_cnt += (2 * shr_host_write_cnt);	// ISU, QD1 performance, to cover the time of CPU3 pick up min VAC block, fc will clear to 0 when gc start
		  else
		  #endif		
		  	shr_gc_rel_du_cnt += (shr_host_write_cnt);
		}
	}
	else
	{
		//ftl_core_trace(LOG_INFO, 0, "p2l not ready %d %d\n",load->grp_id,gc_ctx->p2l_ping_pong_cnt);
		//set flag to continue gc when p2l load done
		gc_ctx->p2l_not_ready=true;
	}

	return FSM_PAUSE;
}

slow_code bool ftl_core_gc_chk_stop()
{
    return (_ftl_core_gc.flags.b.abort || _ftl_core_gc.flags.b.suspend || plp_trigger || gc_suspend_start_wl);
}

extern bool ncl_cmd_in;
fast_code void evt_ftl_core_gc_p2l_next(u32 parm, u32 payload, u32 sts)
{
    ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
    p2l_load_req_t *load = &gc_ctx->p2l_load;
    //sys_assert(gc_ctx->flags.b.suspend == false);
    sys_assert(gc_ctx->flags.b.abort == false);
    if (ncl_cmd_in) {
        evt_set_cs(evt_p2l_load, 0, 0, CS_TASK);
    }
    else {			
        gc_ctx->p2l_gen_idx = 0;
        gc_ctx->p2l_done_cnt = 0;
        gc_ctx->grp_rd_done = 0;
        //shr_gc_rel_du_cnt += shr_host_write_cnt;	// ISU, QD1 performance
		if (load->grp_id > 0)
			shr_gc_rel_du_cnt += shr_host_write_cnt;  // cpu2

        u32 i = 0;
        for(i=0;i<gc_gen_p2l_num;i++)
        {
        	if(load->grp_id < gc_ctx->ttl_grp_cnt)//384) // ISU, EH_GC_not_done
        	{
	            load->p2l_id = load->grp_id * get_p2l_per_grp();
				//evlog_printk(LOG_INFO,"p2lid %d grp %d",load->p2l_id,load->grp_id);
	            load->p2l_cnt = p2l_get_grp_p2l_cnt(load->grp_id, gc_ctx->attr.b.slc);
	            sys_assert(load->p2l_cnt);
	            //load->caller = fsm;//tzu need check
	            load->cmpl = ftl_core_gc_p2l_load_done;
	            ftl_core_p2l_load(load);
				++load->grp_id;
	        }
			else
			{
				//only when odd number p2l
				gc_ctx->odd_p2l = true;
			}
        }
    }
}

fast_code fsm_res_t ftl_core_gc_st_gen_vpda(fsm_ctx_t *fsm)
{
	u32 p2l_id;
	dtag_t dtag;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	p2l_load_req_t *p2l_load = &gc_ctx->p2l_load;
	//p2l_build_req_t *p2l_build = &gc_ctx->p2l_build;

	sys_assert(gc_ctx->flags.b.suspend == false);
	sys_assert(gc_ctx->flags.b.abort == false);
	u16 num_dtags = (gc_gen_p2l_num*3);
	if(gc_ctx->odd_p2l)
	{num_dtags=3;}
	//if (gc_ctx->p2l_ttl_done < gc_ctx->nand_p2l_cnt)
	//{
		#if NUM_P2L_GEN_PDA == 1
		dtag = p2l_load->dtags[((p2l_load->grp_id-1)%2)*3];
		#else
		if(gc_gen_p2l_num == 1){
			dtag = p2l_load->dtags[((p2l_load->grp_id-1)%2)*3];
		}else{
			if((p2l_load->grp_id+gc_ctx->odd_p2l)&BIT1)
				dtag = p2l_load->dtags[0];
			else
				dtag = p2l_load->dtags[gc_gen_p2l_num*3];
		}
		//dtag = p2l_load->dtags[((((p2l_load->grp_id+gc_ctx->odd_p2l)/NUM_P2L_GEN_PDA)-1)%2)*(NUM_P2L_GEN_PDA*3)];
		#endif
	//}
	//else
		//dtag = p2l_build->dtags[gc_ctx->p2l_ttl_done - gc_ctx->nand_p2l_cnt];

	gc_ctx->grp_rd_done = 0;
	p2l_id = gc_ctx->p2l_ttl_done;
	gc_ctx->vgen_p2l_cnt += num_dtags/3;
	//evlog_printk(LOG_ERR, "tzu gen %d %d %d\n",p2l_load->grp_id,(((p2l_load->grp_id/NUM_P2L_GEN_PDA)-1)%2)*(NUM_P2L_GEN_PDA*3),p2l_id);
	//ftl_core_trace(LOG_DEBUG, 0, "gen vpda for spb %d p2l %d", p2l_load->spb_id, p2l_id);
	p2l_valid_pda_gen(dtag, num_dtags, p2l_id, _ftl_core_gc.spb_id, p2l_id, 0);
	return FSM_PAUSE;
}


fast_code fsm_res_t ftl_core_gc_st_xfer_data(fsm_ctx_t *fsm)
{
	//scheduler_gc_vpda_read(0);
	//if ((gc_scheduler.vpda_pending == 0) && (gc_pda_grp[0].reading == 0))
	if ((gc_scheduler.vpda_pending == 0) && (gc_scheduler.rd_done))
		ftl_core_gc_grp_rd_done(0);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	else if(gc_pda_grp[0].spb_id != PLP_SLC_BUFFER_BLK_ID)
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
	else
		issue_gc_read_slc(0);
#else
	else
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
#endif

	return FSM_PAUSE;
}

fast_code fsm_res_t ftl_core_gc_st_pad_open_die(fsm_ctx_t *fsm)
{
	u16 busy;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	u32 ttl_vcnt = 0;
	u32 i;
    if(plp_trigger){
        if((gc_pda_grp[0].done + gc_pda_grp[1].done)!= _ftl_core_gc.write_cnt){
            fsm_ctx_set(fsm, GC_ST_XFER_DATA);	// back to transfer state
		    return FSM_PAUSE;
        }
    }
    else{
    	for (i = 0; i < MAX_GC_PDA_GRP; i++)
    		ttl_vcnt += gc_pda_grp[i].vcnt;

    	// wait all gc data in
    	if ((_ftl_core_gc.write_cnt != ttl_vcnt)||(_ftl_core_gc.vgen_p2l_cnt>_ftl_core_gc.vgen_p2l_done_cnt))
    	{
    		//printk("ftl_core_gc_st_pad_open_die %d %d\n",_ftl_core_gc.write_cnt,ttl_vcnt);
    		fsm_ctx_set(fsm, GC_ST_XFER_DATA);	// back to transfer state
    		return FSM_PAUSE;	// should be resumed in ftl_core_gc_data_in
    	}
		else if(_ftl_core_gc.p2l_ping_pong_cnt != _ftl_core_gc.p2l_load.grp_id)
		{
    		gc_ctx->p2l_not_ready=true;  //set flag to continue gc handle when p2l load done
    		fsm_ctx_set(fsm, GC_ST_LOAD_P2L);	// back to load P2L state
    		return FSM_PAUSE;	// should be resumed in ftl_core_gc_p2l_load_done
		}
    }
	//printk("close open die and delay 5s\n");
	//mdelay(5000);
	//ftl_core_trace(LOG_DEBUG, 0, "gc pad open die");
	gc_ctx->flags.b.done = true;

	busy = ftl_core_close_open_die(1, FTL_CORE_GC, ftl_core_gc_wr_done);

    //ftl_core_trace(LOG_INFO, 0, "busy is:%d",busy);
	_fc_credit += gc_next_blk_cost;  // add more for the period of picking next GC source block, measure ~20ms

	if (busy == 0 && ftl_core_gc_all_grp_done())
		return FSM_CONT; //GC_ST_SPB_DONE

	return FSM_PAUSE;
}
extern u32 global_gc_mode; // ISU, GCRdFalClrWeak

fast_code fsm_res_t ftl_core_gc_st_spb_done(fsm_ctx_t *fsm)
//slow_code fsm_res_t ftl_core_gc_st_spb_done(fsm_ctx_t *fsm)
{
	u32 write_cnt;
	spb_id_t spb_id;
	//u32 p2l_du_cnt = 0;
	//u32 raid_du_cnt = 0;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;

	sys_assert(gc_ctx->spb_id != INV_SPB_ID);
	spb_id = gc_ctx->spb_id;
	gc_ctx->spb_id = INV_SPB_ID;

	if (gc_ctx->flags.b.suspend_done)
	{
		ftl_core_trace(LOG_ALW, 0xc0a6, "[GC] spb %d, fsm %d, p2l %d/%d suspend, write_cnt %d",
				spb_id, gc_ctx->suspend_fsm_st, gc_ctx->p2l_gen_idx, gc_ctx->p2l_load.grp_id, gc_ctx->write_cnt);
		write_cnt = ~0;
		if(plp_trigger || gc_ctx->flags.b.suspend)
		{
			shr_gc_fc_ctrl = 2;
		   	_fc_credit = 0;
	        global_gc_mode = 0; // ISU, GCRdFalClrWeak
			if(plp_trigger)
			{
				plp_gc_suspend_done = true;
			}
		}
		if(gc_suspend_start_wl)	//the recorded suspend_grp_id must decrease 2*NUM_P2L_GEN_PDA, and must be an even number when NUM_P2L_GEN_PDA==2
		{
			gc_suspend_grp_id = gc_ctx->p2l_load.grp_id > (gc_gen_p2l_num*2) ? ((gc_ctx->p2l_load.grp_id-(gc_gen_p2l_num*2))&(INV_U16-gc_gen_p2l_num+1)) : 0;
			gc_suspend_spb_sn = gc_ctx->spb_sn; // add sn check to avoid mismatch between grp_id and SPB
		}
	}
	else if (gc_ctx->flags.b.abort_done) {
		ftl_core_trace(LOG_ALW, 0x363c, "GC %d, abort", spb_id);	// ISU, GC_mod_for_EH
		shr_gc_fc_ctrl = 2;
	   	_fc_credit = 0;
        write_cnt = ~0;
	}
	else
	{
		/*
		if (fcns_p2l_enabled(1))
		{
			if (gc_ctx->attr.b.slc && gc_ctx->attr.b.dslc == false)
				p2l_du_cnt = get_slc_p2l_cnt();
			else
				p2l_du_cnt = get_xlc_p2l_cnt();
		}

		if (fcns_raid_enabled(1))
		{
			u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;
			if (gc_ctx->attr.b.slc && gc_ctx->attr.b.dslc == false)
				raid_du_cnt = nand_page_num_slc() * mp_du_cnt;
			else
				raid_du_cnt = nand_page_num() * mp_du_cnt;
		}
		*/
		write_cnt = gc_ctx->write_cnt;
		//free_du = gc_ctx->spb_ttl_du - gc_ctx->write_cnt - p2l_du_cnt - raid_du_cnt;
		//ftl_core_trace(LOG_INFO, 0, "spb %d free %d wr %d", spb_id, free_du, gc_ctx->write_cnt);

		//ftl_core_trace(LOG_INFO, 0, "ttl %d p2l %d raid %d", gc_ctx->spb_ttl_du, p2l_du_cnt, raid_du_cnt);

		//ftl_core_trace(LOG_INFO, 0, "spb %d grp 0 vcnt %d, grp 1 vcnt %d, ttl p2l %d",
		//	spb_id, gc_pda_grp[0].vcnt, gc_pda_grp[1].vcnt, gc_ctx->p2l_ttl_done);
		//sys_assert(gc_ctx->ttl_p2l_cnt == gc_ctx->p2l_ttl_done);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		extern volatile u8  shr_slc_flush_state;
		if(shr_slc_flush_state == SLC_FLOW_GC_START)
		{
			shr_slc_flush_state = SLC_FLOW_GC_DONE;
			
			shr_slc_flush_state =SLC_FLOW_GC_OPEN_PAD_START;
			fcore_fill_dummy(FTL_CORE_GC, FTL_CORE_GC, FILL_TYPE_WL, slc_gc_open_pad_done, NULL);	
			//shr_slc_flush_state =SLC_FLOW_GC_OPEN_PAD_START; don't set here , if slc vac 0 ,gc will no open blk
			//shr_slc_flush_state = SLC_FLOW_DONE;//CPU1 can turn to init done
			//slc_erase_block();plp no ready
#if 0//(BG_TRIM == ENABLE)
            cpu_msg_issue(CPU_FE - 1, CPU_MSG_CHK_BG_TRIM, 0, 0);
#endif
		}
#endif
	}

	_ftl_core_gc.attr.all = 0;
	_ftl_core_gc.flags.all = 0;
	_ftl_core_gc.write_cnt = 0;
	ipc_api_gc_done(spb_id, write_cnt);
	_ftl_core_gc.spb_id = INV_SPB_ID;

	if (gc_ctx->gc_action)
	{
		gc_action_t *gc_action;

		gc_action = gc_ctx->gc_action;

		if(gc_action->cmpl != NULL)
        {      
			gc_action->cmpl(gc_action);
        }
		else
			sys_free(FAST_DATA, gc_ctx->gc_action);

		gc_ctx->gc_action = NULL;
	}

	return FSM_QUIT;
}

fast_code void ftl_core_gc_xfer_data(u32 grp)
{
	fsm_ctx_t *fsm = &_ftl_core_gc.fsm;
	//if(fsm->state_cur != GC_ST_GEN_VPDA)	
	//ftl_core_trace(LOG_ERR, 0, "[GC] err state %d for bid 180",fsm->state_cur);
	sys_assert(fsm->state_cur == GC_ST_GEN_VPDA);
	fsm_ctx_next(fsm);
	fsm_ctx_run(fsm);
}

fast_code bool ftl_core_gc_if_exec_next_st(void)
//slow_code bool ftl_core_gc_if_exec_next_st(void)
{

	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;

	/*
         * we can only gen vpda for next p2l when there's enough
         * entry to save L2PE's vpda reponse, otherwise L2PE may
         * deadlock by gen vpda and l2p update simultaneously,
         * this is the L2PE's design limitation
	 */

	if (gc_ctx->fsm.state_cur != GC_ST_XFER_DATA)
		return false;

	if(gc_suspend_abort_check(GC_ST_XFER_DATA))
	{
		return true;
	}
	else
	{
		//current P2L's vpda has gen done and read done
		if (gc_ctx->grp_rd_done < 4)//MAX_GC_PDA_GRP)
		return false;
	}

	//check if free response resource of gc group is enough for next P2L
	/*
	u32 grp;
	for (grp = 0; grp < MAX_GC_PDA_GRP; grp++) {
		u32 vcnt = gc_pda_grp[grp].vcnt;
		u32 done = gc_pda_grp[grp].done;

		if ((vcnt - done) > (200))
			return false;
	}*/

	return true;
}

fast_code void ftl_core_gc_exec_next_st(void)
{
	u16 next;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	//p2l_load_req_t *p2l_load = &gc_ctx->p2l_load;
/*
	if (gc_ctx->p2l_ttl_done < gc_ctx->ttl_p2l_cnt) {
		if (gc_ctx->p2l_ttl_done < gc_ctx->nand_p2l_cnt) {
			if (gc_ctx->p2l_done_cnt < p2l_load->p2l_cnt) {
				next = GC_ST_GEN_VPDA;
			} else {
				//p2l_load->grp_id++;
				next = GC_ST_LOAD_P2L;
			}
		} else {
			next = GC_ST_GEN_VPDA;
		}
	} else {
		next = GC_ST_PAD_OPEN_DIE;
	}
*/
	if (gc_ctx->p2l_ttl_done < gc_ctx->ttl_p2l_cnt)
	{
		if(shr_gc_read_disturb_ctrl)
			gc_read_disturb_time=get_tsc_64();//gc_read_disturb_time= jiffies;//
		//if (shr_gc_no_slow_down == true)  // free block too few, do not slow down GC speed
		//  next = GC_ST_LOAD_P2L;
		//else{
		  gc_issue_load_p2l = true;  // adams
          //gc_load_p2l_time = jiffies;
		  return;
	    //}
	}
	else
	{
		next = GC_ST_PAD_OPEN_DIE;
	}
	// suspend GC
	//if(gc_suspend_abort_check(next))
	//{
	//	next = GC_ST_PAD_OPEN_DIE;
	//}
	/*
	if (gc_ctx->flags.b.suspend && next != GC_ST_PAD_OPEN_DIE) {
		gc_ctx->flags.b.suspend_done = true;
		gc_ctx->suspend_fsm_st = next;
		gc_ctx->suspend_spb = gc_ctx->spb_id;
		gc_ctx->suspend_spb_sn = gc_ctx->spb_sn;
		next = GC_ST_PAD_OPEN_DIE;
	} else if (gc_ctx->flags.b.abort_done) {
		// abort GC
		next = GC_ST_PAD_OPEN_DIE;
	}
	*/
	fsm_ctx_set(&gc_ctx->fsm, next);
	fsm_ctx_run(&gc_ctx->fsm);
}

fast_code void ftl_core_gc_grp_rd_done(u32 grp)
{
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;

	if(gc_pda_grp[grp].pda_in==true)
	{
		if(grp == 0)
		{
			gc_scheduler.rd_done = false;
			gc_pda_grp[0].pda_in=false;
			gc_ctx->grp_rd_done |= 0x1;
		}
		else
		{
			gc_pda_grp[1].pda_in=false;
			gc_ctx->grp_rd_done |= 0x2;
		}
	}
	if (gc_ctx->grp_rd_done == 3)//MAX_GC_PDA_GRP)
	{
		++gc_ctx->grp_rd_done;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(gc_pda_grp[0].spb_id == PLP_SLC_BUFFER_BLK_ID)
			gc_ctx->p2l_ttl_done = gc_ctx->ttl_p2l_cnt = 0;
		else
#endif
		{
			gc_ctx->p2l_gen_idx+=(gc_gen_p2l_num*3);
			gc_ctx->p2l_done_cnt+=(gc_gen_p2l_num*3);
			gc_ctx->p2l_ttl_done+=(gc_gen_p2l_num*3);
			gc_ctx->vgen_p2l_done_cnt+=gc_gen_p2l_num;
		}

	}

	if (ftl_core_gc_if_exec_next_st())
		ftl_core_gc_exec_next_st();
}

fast_code void ftl_core_gc_pend_in(u32 cnt, bm_pl_t *bm_pl_list)
{
	//ftl_core_trace(LOG_ERR, 0, "tzu pend in %d %d %d\n",_ftl_core_gc.pend_cnt,_ftl_core_gc.tmp_cnt,cnt);

	u32 pend_cnt = _ftl_core_gc.pend_cnt;
	if(pend_cnt + cnt > PEND_BUFF_CNT)
	{

		_ftl_core_gc.pend_cnt = pend_cnt + cnt-PEND_BUFF_CNT;
		u32 fill = (PEND_BUFF_CNT-pend_cnt);
		memcpy(&_ftl_core_gc.pend_buf[pend_cnt], bm_pl_list, sizeof(bm_pl_t) * fill);
		memcpy(&_ftl_core_gc.pend_buf[0], (&bm_pl_list[fill]), sizeof(bm_pl_t) * (cnt-fill));
	}
	else
	{
		_ftl_core_gc.pend_cnt += cnt;
		memcpy(&_ftl_core_gc.pend_buf[pend_cnt], bm_pl_list, sizeof(bm_pl_t) * cnt);
	}
	sys_assert(_ftl_core_gc.pend_cnt != _ftl_core_gc.tmp_cnt);

	/*
	u32 pend_cnt = _ftl_core_gc.pend_cnt;
	sys_assert(pend_cnt + cnt <= FCORE_GC_DTAG_CNT);
	memcpy(&_ftl_core_gc.pend_buf[pend_cnt], bm_pl_list, sizeof(bm_pl_t) * cnt);
	_ftl_core_gc.pend_cnt += cnt;
	*/
}

fast_code void ftl_core_gc_pend_out(void)
{
	//ftl_core_trace(LOG_ERR, 0, "tzu pend out %d %d\n",_ftl_core_gc.pend_cnt,_ftl_core_gc.tmp_cnt);
	u32 handled_cnt = 0;
	u32 pend_cnt = _ftl_core_gc.pend_cnt;
	u32 tmp_cnt = _ftl_core_gc.tmp_cnt;
	if(pend_cnt != tmp_cnt)
	{
		if(pend_cnt > tmp_cnt)
		{
            //_ftl_core_gc.tmp_cnt = pend_cnt;
			ftl_core_gc_data_in(pend_cnt-tmp_cnt,&( _ftl_core_gc.pend_buf[tmp_cnt]), &handled_cnt);
			_ftl_core_gc.tmp_cnt += handled_cnt;
		}
		else
		{
            //_ftl_core_gc.tmp_cnt = 0;
			ftl_core_gc_data_in(PEND_BUFF_CNT-tmp_cnt, &( _ftl_core_gc.pend_buf[tmp_cnt]), &handled_cnt);
			if(handled_cnt == PEND_BUFF_CNT-tmp_cnt)
			{
				_ftl_core_gc.tmp_cnt = 0;
			}
			else
			{
				_ftl_core_gc.tmp_cnt += handled_cnt;
				return;
			}

			if(pend_cnt > 0)
			{
                //_ftl_core_gc.tmp_cnt = pend_cnt;
				handled_cnt = 0;
				ftl_core_gc_data_in(pend_cnt,&( _ftl_core_gc.pend_buf[0]), &handled_cnt);
				_ftl_core_gc.tmp_cnt += handled_cnt;
			}
		}


	}

	/*
		u32 pend_cnt = _ftl_core_gc.pend_cnt;

		if (pend_cnt) {
			//memcpy(_ftl_core_gc.tmp_buf, _ftl_core_gc.pend_buf, sizeof(bm_pl_t) * pend_cnt);
			_ftl_core_gc.pend_cnt = 0;
			ftl_core_gc_data_in(pend_cnt, _ftl_core_gc.pend_buf);
		}
	*/
	/*
	u32 pend_cnt = _ftl_core_gc.pend_cnt;

	if (pend_cnt) {
		memcpy(_ftl_core_gc.tmp_buf, _ftl_core_gc.pend_buf, sizeof(bm_pl_t) * pend_cnt);
		_ftl_core_gc.pend_cnt = 0;
		ftl_core_gc_data_in(pend_cnt, _ftl_core_gc.tmp_buf);
	}
	*/
}

fast_data void evt_pend_out_check(u32 parm, u32 payload, u32 sts)
{
    cpu_msg_isr();
    //ftl_core_trace(LOG_INFO, 0, "evt pend out0 pend:%d, tmp:%d",_ftl_core_gc.pend_cnt, _ftl_core_gc.tmp_cnt);
    ftl_core_gc_pend_out();
    //ftl_core_trace(LOG_INFO, 0, "evt pend out1 pend:%d, tmp:%d",_ftl_core_gc.pend_cnt, _ftl_core_gc.tmp_cnt);
}
extern void ftl_core_gc_push_dtagid(u32 dtag_id);

fast_code void ftl_core_gc_data_drop(u32 cnt, bm_pl_t *bm_pl_list)
{
	u32 i;
	u32 grp;
	//u32 rsp_idx;
	u32 dtag_idx;

	for (i = 0; i < cnt; i++) {
		//rsp_idx = bm_pl_list[i].pl.btag;
		dtag_idx = bm_pl_list[i].pl.du_ofst;
		grp = bm_pl_list[i].pl.nvm_cmd_id;

		gc_pda_grp[grp].done++;

		//sys_assert(rsp_idx < PDA_RSP_PER_GRP);
		sys_assert(dtag_idx < DTAG_CNT_PER_GRP);

		if (gc_pda_grp[grp].attr.b.remote) {
			bool ret;
			gc_res_free_t res;
			//res.rsp_idx = rsp_idx;
			res.dtag_idx = dtag_idx;
			CBF_INS_GC(gc_res_free_que, ret, res);
			sys_assert(ret == true);

		} else {
			//rsp = &gc_scheduler.vpda_rsp_entry[rsp_idx];
			//pool_put_ex(&gc_scheduler.rsp_pool, rsp); 
			ftl_core_gc_push_dtagid(dtag_idx);
			//int ret = test_and_set_bit(dtag_idx, gc_scheduler.ddtag_bmp);
			//sys_assert(ret == 0);
			gc_scheduler.ddtag_avail++;
		}
	}
	if (_ftl_core_gc.flags.b.abort || _ftl_core_gc.flags.b.suspend || plp_trigger || gc_suspend_start_wl) 
	{
		if (gc_scheduler.vpda_pending)
			scheduler_gc_grp_aborted(GC_LOCAL_PDA_GRP);
	}
}

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
extern u64 l2p_base_addr;
ddr_code lda_t ftl_core_slc_chk_pda(lda_t lda, pda_t pda)
{
    u64 addr;
    u8  win = 0;
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
    u8 old = ctrl0.b.cpu3_ddr_window_sel;
	pda_t old_pda = INV_PDA;
    addr = (l2p_base_addr | 0x40000000) + ((u64)lda * 4);
	while (addr >= 0xC0000000) {
		addr -= 0x80000000;
		win++;
	}
    if(old != win){
    	ctrl0.b.cpu3_ddr_window_sel = win;
    	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
        __dmb();
        old_pda = *((u32*)((u32)addr));
    	ctrl0.b.cpu3_ddr_window_sel = old;
    	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
        __dmb();
    }
    else{
        old_pda = *((u32*)(u32)addr);
    }	
    if(old_pda == pda)
    {
		return lda;
    }
    else
    {
    	//ftl_core_trace(LOG_INFO, 0xbad4,"lda:0x%x pda:0x%x l2p_pda:0x%x",lda,pda,old_pda);
		return DUMMY_LDA;
    }
}

ddr_code void ftl_core_gc_dtag_free(bm_pl_t *bm_pl, u8 type)
{
	u32 grp = bm_pl->pl.nvm_cmd_id;
	gc_pda_grp[grp].vcnt--;
	sys_assert((grp == 0) || (grp == 1));
	if(type == 0)
		gc_pda_grp[0].dummy_cnt++;
	else
		gc_pda_grp[0].defect_cnt++;
	u32 dtag_grp_idx = bm_pl->pl.du_ofst;
	sys_assert(dtag_grp_idx < DTAG_CNT_PER_GRP);
	if (gc_pda_grp[grp].attr.b.remote)
	{
		bool ret;
		gc_res_free_t res;
		res.dtag_idx = dtag_grp_idx;
		CBF_INS_GC(gc_res_free_que, ret, res);  //if gc_res_free_que size is not power of 2, need to use CBF_INS_GC
		sys_assert(ret == true);
	}
	else
	{
		ftl_core_gc_push_dtagid(dtag_grp_idx);
		gc_scheduler.ddtag_avail++;
	}
	scheduler_gc_grp_refill(0);
	if (gc_scheduler.ddtag_avail && gc_scheduler.vpda_pending)
	{
		issue_gc_read_slc(0);
	}
}
#endif
//extern fast_code bool ftl_core_gc_bypass(u32 nsid);	// Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
//extern fast_code bool chk_l2p_updt_que_cnt_full(void);

extern u32 _max_capacity;
fast_code bool ftl_core_gc_data_in(u32 cnt, bm_pl_t *bm_pl_list, u32 *handled_cnt)
{
	u32 i = 0;
	//u32 handled = 0;
	bm_pl_t *bm_pl = bm_pl_list;
	cur_wr_pl_t *cur_wr_pl = NULL;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(gc_ctx->spb_id == PLP_SLC_BUFFER_BLK_ID)
	{
		if(plp_trigger || gc_pro_fail)
		{
			_ftl_core_gc.write_cnt += cnt;
			i = cnt;
			ftl_core_gc_data_drop(cnt, bm_pl_list);
			goto next;
		}
	}
	else
#endif
	if (gc_ctx->flags.b.abort || gc_ctx->flags.b.suspend ||plp_trigger)
    {
		_ftl_core_gc.write_cnt += cnt;
		i = cnt;
		ftl_core_gc_data_drop(cnt, bm_pl_list);
		goto next;
	}

	bool ns_dirty = ftl_core_open(HOST_NS_ID);
	//if ((ns_dirty == false) || ftl_core_ns_padding(HOST_NS_ID)) {
	#ifndef EH_GCOPEN_HALT_DATA_IN
	if (ns_dirty == false || chk_l2p_updt_que_cnt_full() || ncl_cmd_in == true) 
	#else
	bool gc_bypass = ftl_core_gc_bypass(HOST_NS_ID);	// Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
	if (ns_dirty == false || gc_bypass == true || chk_l2p_updt_que_cnt_full() || ncl_cmd_in == true) 
	#endif	
    {
    	if(handled_cnt == NULL)
			ftl_core_gc_pend_in(cnt, bm_pl);
		else
			*handled_cnt = i;
		//ftl_core_set_wreq_pend(1, FTL_CORE_GC, true);
        evt_set_cs(evt_pend_out, 0, 0, CS_TASK);
		return false;
	}

	u32 cnt1, lda, dtag_idx;
	io_meta_t *meta;
	for (i = 0; i < cnt; i++) {

		dtag_idx = DTAG_GRP_IDX_TO_IDX(bm_pl->pl.du_ofst, bm_pl->pl.nvm_cmd_id);
	    meta = &gc_ctx->meta[dtag_idx];
	    lda = meta->lda;
//		sys_assert(meta->lda != INV_LDA);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(gc_ctx->spb_id == PLP_SLC_BUFFER_BLK_ID && lda < _max_capacity)
			lda = ftl_core_slc_chk_pda(meta->lda,gc_ctx->pda_list[dtag_idx]);
		
		if(lda == INV_LDA || lda == DUMMY_LDA || lda == TRIM_LDA)
        {
        	if(gc_ctx->spb_id != PLP_SLC_BUFFER_BLK_ID)
		    	ftl_core_trace(LOG_ALW, 0x52cb, "[GC]abnormal meta: 0x%x, dtag idx: %d" ,meta->lda, dtag_idx);
			else
			{
				ftl_core_gc_dtag_free(bm_pl, 0);
				bm_pl++;
				//handled++;
				continue;
			}
        }
        
#else
		if(lda == INV_LDA)
        {
		    ftl_core_trace(LOG_ALW, 0xf841, "[GC]abnormal meta: 0x%x, dtag idx: %d" ,meta->lda, dtag_idx);
        }
#endif

		/*
		if(ftl_core_gc_cache_search(gc_ctx->lda_list[dtag_idx]))
		{
			//ftl_core_trace(LOG_INFO, 0, "gc hit cache LDA:0x%x, drop data",gc_ctx->lda_list[dtag_idx]);
			ftl_core_gc_data_drop(1, bm_pl);
			_ftl_core_gc.write_cnt++;
			bm_pl++;
			handled++;
			continue;
		*/
		if (chk_l2p_updt_que_cnt_full() || plp_trigger){
            evt_set_cs(evt_pend_out, 0, 0, CS_TASK);
    		if(handled_cnt == NULL)
            	ftl_core_gc_pend_in(cnt - i, bm_pl);
			else
				*handled_cnt = i;
            return false;
        }
		if (cur_wr_pl == NULL) {
			cur_wr_pl = ftl_core_get_wpl_mix_mode(1, FTL_CORE_GC, WPL_V_MODE, true, NULL, NULL);
			if (cur_wr_pl == NULL) {
                //ftl_core_set_wreq_pend(1, FTL_CORE_GC, true);  //gc abort, set wreq_pend flag
                evt_set_cs(evt_pend_out, 0, 0, CS_TASK);
    			if(handled_cnt == NULL)
					ftl_core_gc_pend_in(cnt - i, bm_pl);
				else
					*handled_cnt = i;
				return false;
			}
		}
		cnt1 = cur_wr_pl->cnt;
        ftl_core_gc_data_in_update();
		//cur_wr_pl->lda[cur_wr_pl->cnt] = meta->lda;  //gc lda list from p2l
		//============gc lda list from p2l=====================
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(bm_pl->pl.btag == 0xFFF)
		{
			if(gc_ctx->spb_id != PLP_SLC_BUFFER_BLK_ID)
				cur_wr_pl->lda[cnt1] = gc_ctx->lda_list[dtag_idx];
			else
			{
				// If slc buffer read fail, there is no lda_list from p2l
				ftl_core_trace(LOG_ALW, 0x26ba, "[GC] slc buffer read fail");
				ftl_core_gc_dtag_free(bm_pl, 1);
				ftl_core_host_data_in(HOST_NS_ID, FTL_CORE_GC, false);
				bm_pl++;
				//handled++;
				continue;
			}
		}
		else
			cur_wr_pl->lda[cnt1] = lda;
#else
		if(bm_pl->pl.btag == 0xFFF)
			cur_wr_pl->lda[cnt1] = gc_ctx->lda_list[dtag_idx];
		else
			cur_wr_pl->lda[cnt1] = lda;
#endif
		//============gc lda list from p2l=====================
        sys_assert(cur_wr_pl->lda[cnt1] != INV_LDA);
		cur_wr_pl->pl[cnt1].all = bm_pl->all;
		//l2p_mgr_set_seg_dirty(meta->lda);  //gc lda list from p2l
		cur_wr_pl->cnt = cnt1 + 1;
		_ftl_core_gc.write_cnt++;
		bm_pl++;
		//handled++;
#ifdef SKIP_MODE  //need to check
		if (cur_wr_pl->cnt == cur_wr_pl->max_cnt) // ftl_core_next_write(1, FTL_CORE_GC)
#else
		if (cur_wr_pl->cnt == cur_wr_pl->max_cnt) // ftl_core_next_write(1, FTL_CORE_GC)  // every 4
#endif
    	{
			ftl_core_submit(cur_wr_pl, NULL, ftl_core_gc_wr_done);
			cur_wr_pl = NULL;
		}
	}
next:
	if(handled_cnt != NULL)
		*handled_cnt = i;
	if (ftl_core_gc_if_exec_next_st())
		ftl_core_gc_exec_next_st();

	return true;
}

fast_code void ftl_core_gc_res_free(ncl_w_req_t *req)
{
	u32 i;
	u32 grp;
	//u32 rsp_idx;
	u32 dtag_idx;
	//pda_gen_rsp_t *rsp;
	u32 du_cnt = req->req.cnt << DU_CNT_SHIFT;

    #ifdef NCL_RETRY_PASS_REWRITE
    u32 common_dtag_id;
    #endif

	for (i = 0; i < du_cnt; i++) {
		/*if ((req->w.lda[i] == INV_LDA) ||
            (req->w.lda[i] == BLIST_LDA) ||
            (req->w.lda[i] == P2L_LDA))*/
        if (req->w.lda[i] >= BLIST_LDA)
        {
			continue;
        }

        #ifdef NCL_RETRY_PASS_REWRITE
        common_dtag_id = req->w.pl[i].pl.dtag & DDTAG_MASK;
        if((common_dtag_id >= DDR_RD_REWRITE_START) && (common_dtag_id < (DDR_RD_REWRITE_START + DDR_RD_REWRITE_TTL_CNT)))
        {
            continue;
        }
        #endif

		grp = req->w.pl[i].pl.nvm_cmd_id;
		gc_pda_grp[grp].done++;

		//rsp_idx = req->w.pl[i].pl.btag;
		dtag_idx = req->w.pl[i].pl.du_ofst;

		//sys_assert(rsp_idx < PDA_RSP_PER_GRP);
		sys_assert(dtag_idx < DTAG_CNT_PER_GRP);

		if (gc_pda_grp[grp].attr.b.remote) {
			bool ret;
			gc_res_free_t res;
			//res.rsp_idx = rsp_idx;
			res.dtag_idx = dtag_idx;
			CBF_INS_GC(gc_res_free_que, ret, res);
			sys_assert(ret == true);
		} else {
			//rsp = &gc_scheduler.vpda_rsp_entry[rsp_idx];
			//pool_put_ex(&gc_scheduler.rsp_pool, rsp);
			ftl_core_gc_push_dtagid(dtag_idx);
			//int ret = test_and_set_bit(dtag_idx, gc_scheduler.ddtag_bmp);
			//sys_assert(ret == 0);

			gc_scheduler.ddtag_avail++;
		}
	}

	scheduler_gc_grp_refill(0);

	if (gc_scheduler.ddtag_avail && gc_scheduler.vpda_pending)
	{
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(gc_pda_grp[GC_LOCAL_PDA_GRP].spb_id != PLP_SLC_BUFFER_BLK_ID)
			scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
		else
			issue_gc_read_slc(0);
#else
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
#endif

	}
}
fast_code bool ftl_core_gc_all_grp_done(void)
{
	u32 i;

	if(plp_trigger)
	{
        if((gc_pda_grp[0].done + gc_pda_grp[1].done)!= _ftl_core_gc.write_cnt){
            return false;
        }else{
		    return true;
        }
	}
	for (i = 0; i < MAX_GC_PDA_GRP; i++) {
		if (gc_pda_grp[i].done < gc_pda_grp[i].vcnt)
			return false;
	}

	return true;
}

fast_code void ftl_core_gc_wr_done(ncl_w_req_t *req)
{
	/*
	 * for each host ncl write req, the callback will execute twice.
	 * first time is ncl cmd done, second time is l2p update done
	 */
	if (req->req.op_type.b.ncl_done && req->req.op_type.b.host)
    {
        #ifdef NCL_RETRY_PASS_REWRITE 
        u32 idx;
        u32 dtag_id;
        u32 du_cnt = req->req.cnt << DU_CNT_SHIFT;
        bool req_include_gc_du = false;
        for(idx = 0; idx < du_cnt; idx++)
        {
            dtag_id = req->w.pl[idx].pl.dtag & DDTAG_MASK;
            if((dtag_id < DDR_RD_REWRITE_START) || (dtag_id >= (DDR_RD_REWRITE_START + DDR_RD_REWRITE_TTL_CNT)))
            {
                req_include_gc_du = true;
            }
        }
        #endif
        //ftl_core_trace(LOG_INFO, 0, "[DBG]gc wr done: dtag: 0x%x", req->w.pl[0].pl.dtag & DDTAG_MASK);
		ftl_core_gc_res_free(req);

        #ifdef NCL_RETRY_PASS_REWRITE
        if(req_include_gc_du == true)
        #endif
        {	
        	if(_ftl_core_gc.flags.b.done && ftl_core_gc_all_grp_done() && _ftl_core_gc.flags.b.gc_plp == 0) 
            {
    			fsm_ctx_t *fsm = &_ftl_core_gc.fsm;
    			fsm_ctx_set(fsm, GC_ST_SPB_DONE);
    			fsm_ctx_run(fsm);
				if(plp_trigger)
        		{
					_ftl_core_gc.flags.b.gc_plp = true;
				}
    		} 
            else if (ftl_core_gc_if_exec_next_st())
    			ftl_core_gc_exec_next_st();
        }
	}

	ftl_core_done(req);
}

fast_code void ftl_core_gc_fetch_src_data(void)
{
	if (CBF_EMPTY(gc_src_data_que))
		return;

	u32 cnt[2];
	u32 pcnt;
	u32 i;
	bm_pl_t *p[2];
	u32 fetched_cnt = 0;

	CBF_FETCH_LIST(gc_src_data_que, cnt, p, pcnt);

	for (i = 0; i < pcnt; i++) {
		//bool ret = ftl_core_gc_data_in(cnt[i], p[i]);
		ftl_core_gc_data_in(cnt[i], p[i], NULL);

		fetched_cnt += cnt[i];
		//if (ret == false)
		//	break;
	}

	CBF_FETCH_LIST_DONE(gc_src_data_que, fetched_cnt);
}

slow_code bool ftl_core_gc_abort(void)
{
	u32 i;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	fsm_ctx_t *gc_fsm = &gc_ctx->fsm;

	//gc has not been started
	if (gc_ctx->spb_id == INV_SPB_ID)
		return true;

	gc_ctx->flags.b.abort = true;
	/*
	 * GC_ST_GET_END_PAGE -> abort at scan cmd done
	 * GC_ST_BUILD_P2L -> abort at p2l build done
	 * GC_ST_LOAD_P2L -> abort at load done
	 * GC_ST_GEN_VPDA -> abort at scheduler_gc_vpda_read, drop everything and resume at gc_grp_done
	 * GC_ST_XFER_DATA -> abort at scheduler_gc_vpda_read, drop everything and resume at gc_grp_done
	 *                    drop data_in
	 * GC_ST_PAD_OPEN_DIE -> no action
	 * GC_ST_SPB_DONE -> no action
	 */

	// let it done
	if (gc_fsm->state_cur >= GC_ST_PAD_OPEN_DIE)
		return false;

	for (i = 0; i < MAX_GC_PDA_GRP; i++) {
		if (gc_pda_grp[i].attr.b.remote)
			cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_GC_GRP_SUSPEND, 0, i); // need IPC or not
		else
			gc_pda_grp[i].flags.b.abort = true;
	}
	_ftl_core_gc.flags.b.abort_done = true;

	return false;
}

slow_code bool ftl_core_gc_suspend(void)
{
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
	fsm_ctx_t *gc_fsm = &gc_ctx->fsm;
	u16 state = gc_fsm->state_cur;

	if (gc_ctx->spb_id == INV_SPB_ID)
		return true;

	gc_ctx->flags.b.suspend = true;
	shr_no_host_write = true;
	ftl_core_trace(LOG_INFO, 0xac81, "gc suspend at fsm %d", state);
    //tzu_get_gc_info();
	/*
	 * GC_ST_LOAD_P2L -> suspend at load done
	 * GC_ST_GEN_VPDA -> suspend at state function
	 * GC_ST_XFER_DATA -> suspend at ftl_core_gc_exec_next_st function
	 * GC_ST_PAD_OPEN_DIE -> no action
	 * GC_ST_SPB_DONE -> no action
	 */
	return false;
}

fast_code bool ftl_core_gc_action(gc_action_t *action)
{
	bool done = false;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;
#if(PLP_GC_SUSPEND == mENABLE)
    if(gc_ctx->gc_action != NULL)
	{

		//ftl_core_trace(LOG_INFO, 0, "gc_ctx->gc_action != NULL");
		//evlog_printk(LOG_ERR,"gc act !=NULL");
		return true;
	}
	sys_assert(action->act != GC_ACT_RESUME);
#else
	sys_assert(gc_ctx->gc_action == NULL);
	sys_assert(action->act != GC_ACT_RESUME);
#endif
	if (action->act == GC_ACT_ABORT)
		done = ftl_core_gc_abort();
	else
		done = ftl_core_gc_suspend();

	if (done == false)
		gc_ctx->gc_action = action;

	return done;
}

#if(PLP_GC_SUSPEND == mDISABLE)
fast_code bool ftl_core_gc_action2(gc_action_t *action)//joe add test 20201119
{
	bool done = false;
	ftl_core_gc_t *gc_ctx = &_ftl_core_gc;

	if(gc_ctx->gc_action != NULL){
		printk("gc act2 !=NULL");
		return false;
	}
	sys_assert(action->act != GC_ACT_RESUME);

	if (action->act == GC_ACT_ABORT)
		done = ftl_core_gc_abort();
	else
		done = ftl_core_gc_suspend();

	if (done == false)
		gc_ctx->gc_action = action;

	return done;
}
#endif

fast_code void ftl_core_gc_spb_scan_resume(void)
{

	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;
	u32 tatal_WL = (shr_nand_info.geo.nr_pages / shr_nand_info.bit_per_cell);

	u32 i;
	for (i = 0; i < tatal_WL; i++)
	{
		struct ncl_cmd_t *ncl_cmd = &spb_scan->ncl_cmd[i];
		pda_t *pda_list = &spb_scan->pda_list[i];
		bm_pl_t *bm_pl_list = &spb_scan->bm_pl_list[i];
		struct info_param_t *info_list = &spb_scan->info_list[i];

		ncl_cmd->user_tag_list = bm_pl_list;
		ncl_cmd->addr_param.common_param.pda_list = pda_list;
		ncl_cmd->addr_param.common_param.info_list = info_list;
		ncl_cmd->addr_param.common_param.list_len = 1;

		ncl_cmd->status = 0;
		ncl_cmd->op_code = NCL_CMD_OP_READ;
		ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
		ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
		ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;

		bm_pl_list[0].pl.btag = 0;
		bm_pl_list[0].pl.du_ofst = 0;
		bm_pl_list[0].pl.nvm_cmd_id = rd_dummy_meta_idx;
		bm_pl_list[0].pl.dtag = WVTAG_ID;
		bm_pl_list[0].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
	}

/*
	ftl_core_spb_scan_t *spb_scan = &gc_spb_scan;
	u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;

	u32 i, j;
	for (i = 0; i < nand_info.lun_num; i++) {
		struct ncl_cmd_t *ncl_cmd = &spb_scan->ncl_cmd[i];
		pda_t *pda_list = &spb_scan->pda_list[i * mp_du_cnt];
		bm_pl_t *bm_pl_list = &spb_scan->bm_pl_list[i * mp_du_cnt];
		struct info_param_t *info_list = &spb_scan->info_list[i * mp_du_cnt];

		ncl_cmd->user_tag_list = bm_pl_list;
		ncl_cmd->addr_param.common_param.pda_list = pda_list;
		ncl_cmd->addr_param.common_param.info_list = info_list;
		ncl_cmd->addr_param.common_param.list_len = mp_du_cnt;

		ncl_cmd->status = 0;
		ncl_cmd->op_code = NCL_CMD_OP_READ;
		ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
		ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
		ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
        #if NCL_FW_RETRY
        ncl_cmd->retry_step = default_read;
        #endif
		for (j = 0; j < mp_du_cnt; j++) {
			bm_pl_list[j].pl.btag = i;
			bm_pl_list[j].pl.du_ofst = j;
			bm_pl_list[j].pl.nvm_cmd_id = rd_dummy_meta_idx;
			bm_pl_list[j].pl.dtag = (spb_scan->dtag_start + i) | DTAG_IN_DDR_MASK;
			bm_pl_list[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
		}
	}
	*/
}

fast_code void ftl_core_gc_resume(void)
{
	CBF_INIT(gc_src_data_que);
	CBF_INIT(gc_res_free_que);
	ftl_core_gc_spb_scan_resume();

     /*
       * 1. for pmu suspend gc, as the p2l data already lost,
       *     we must set gc fsm to a safe state before resume
       * 2. as CPU_FTL & CPU_BE_LITE resume after CPU_BE,
       *     so it's safe to resume the suspend gc from CPU_FTL
	*/
	u16 resume_fsm_st;
	if (_ftl_core_gc.suspend_spb != INV_SPB_ID) {
		if (_ftl_core_gc.attr.b.spb_open) {
			//open spb gc isn't normal case, resume from start
			resume_fsm_st = GC_ST_GET_END_PAGE;
		} else {
			/*
			 * GC_ST_LOAD_P2L -> GC_ST_LOAD_P2L
			 * GC_ST_GEN_VPDA -> GC_ST_LOAD_P2L
			 * GC_ST_XFER_DATA -> impossible suspend state
			 */
			resume_fsm_st = GC_ST_LOAD_P2L;
			if (_ftl_core_gc.suspend_fsm_st == GC_ST_GEN_VPDA)
				_ftl_core_gc.p2l_ttl_done -= _ftl_core_gc.p2l_done_cnt;
		}

		_ftl_core_gc.suspend_fsm_st = resume_fsm_st;
	}
}
/*
fast_code int scan_main(int argc, char *argv[])
{
	_ftl_core_gc.spb_id = (spb_id_t)strtol(argv[1], (void *)0, 10);
	ftl_core_gc_scan_read(0);
	//_ftl_core_gc.spb_id=INV_SPB_ID;
	return 0;
}

static DEFINE_UART_CMD(scan, "scan", "scan", "scan", 1, 1, scan_main);
*/

/*! @} */
