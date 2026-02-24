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
/*! \file rdisk.c
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
#include "console.h"
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
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
#include "ncl_cmd.h"
#include "cbf.h"
#include "pmu.h"
#include "die_que.h"
#include "ftl_remap.h"
#include "bg_training.h"
#include "misc.h"
#include "srb.h"
#include "fw_download.h"
#include "spi.h" //albert 20200624 for Nor readID
#include "GList.h"
#include "nvmet.h" //joe add for flrmat 20200908
#include "plp.h"
//#include "misc_register.h" //suda add for led
#include "smb_registers.h"		   //shane add for BM data update
#include "pcie_wrapper_register.h" //shane add for BM data update
#include "misc.h"
#include "ssstc_cmd.h"
#include "types.h"
#include "io.h"
#include "mod.h"
#include "sect.h"
#include "stdio.h"
#include "smbus.h"
#include "string.h"
#include "nvme_spec.h"
#include "ficu.h" //tony reARD 20201019
#include "epm.h"
#include "ddr.h"
#include "ucache.h"
#include "thermal.h"


#if TCG_WRITE_DATA_ENTRY_ABORT
#include "tcgcommon.h"

#ifdef TCG_NAND_BACKUP
#include "tcg_nf_mid.h"

extern tcg_mid_info_t *tcg_mid_info;

fast_data_zi volatile mbr_rd_payload_t mbr_pl[256];
share_data_zi volatile u32 mbr_rd_nand_cnt;
#endif
#endif

/*! \cond PRIVATE */
#define __FILEID__ rone
#include "trace.h"
/*! \endcond */
#include "ucache.h"
#include "btn_helper.h"
#include "trim.h"
#include "cmd_proc.h"
#include "btn_export.h"
#include "read_retry.h"
#if (CO_SUPPORT_READ_AHEAD == TRUE)
#include "ra.h"
#endif

#define RDISK_W_STREAMING_MODE_DTAG_CNT (4 * 8 * 4) ///<refine for max channel
#define RDISK_R_STREAMING_MODE_DTAG_CNT (8 * 4)		///<refine for max channel

#define RDISK_L2P_FE_SRCH_QUE 0 ///< l2p search queue id for rdisk
#define RDISK_L2P_FE_UPDT_QUE 0 ///< l2p update queue id for rdisk

#define WDE_NRM_INS_MAX 33
#define WCMD_RLS_MAX 16

// total 4 users allowed
#define PATTERN_GEN_USER_CNT 4
#define Feature_save

#define FE_INFO_SIG 0x05200320
#define FE_INFO_VER 1

typedef struct _fe_info_t
{
	u32 sig;
	u32 version;

	nvme_feature_saved_t nvme_feat;
	smart_statistics_t smart;
	ftl_stat_t ftl_stat;
} fe_info_t;

enum
{
	WDE_PARSER_IDLE,
	WDE_PARSER_BUSY,
	WDE_PARSER_ERR,
};

typedef struct _wde_parser_t
{
	u8 state;
	u8 rsvd;
	union
	{
		u16 all;
		struct
		{
			u16 atomic : 1;
			u16 head : 1;
			u16 tail : 1;
			u16 fua : 1;
			u16 fw_cmd : 1;
			u16 wait_idle : 1;
		} b;
	} flags;
	u16 btag;
	u16 off;
	btn_cmd_t *bcmd;
	btn_cmd_ex_t *bcmd_ex;
	u32 left;
	u32 total;
	lda_t start;
	lda_t cross_lda;//joe add 20200917 for write performance
	dtag_t dtags[WDE_NRM_INS_MAX];
	lda_t ldas[WDE_NRM_INS_MAX];
	u16 btags[WDE_NRM_INS_MAX];
	r_par_t par[WDE_NRM_INS_MAX];

	int ins;

	btn_cmd_t *rls_bcmds[WCMD_RLS_MAX];
	int rls_cmd_cnt;
} wde_parser_t;

#define is_par_wde(bm_pl) (((bm_pl->pl.type_ctrl & BTN_NVM_TYPE_CTRL_NRM_PAR_MASK) == BTN_NVM_TYPE_CTRL_PAR) ? true : false)

// AES-ed / HCRC-ed data generation callback after generation completed
typedef void (*pat_gen_cb)(u32 cmd_slot, dtag_t *dtags, u32 cnt);

typedef struct _rdisk_req_statistics_t
{
	u64 req_rcv_ttl;
	u64 rd_rcv_ttl;
	u64 wr_du_ttl;
} rdisk_req_statistics_t;

fast_data_zi static req_t *nvme_save_req = NULL;				  ///< used for nvme_save_info req parameter
fast_data_zi static rdisk_fe_flags_t rdisk_fe_flags = {.all = 0}; ///< rdisk fe flag
fast_data static u8 evt_wait_wcmd_idle = 0xFF;					  ///< event to resume task for waiting write command idle
fast_data static u8 evt_rdisk_wunc_part = 0xFF;
share_data_zi volatile u32 global_gc_mode; // ISU, GCRdFalClrWeak
share_data_zi volatile u32 shr_gc_op;
share_data_zi volatile u8 has_log;
share_data_zi volatile bool pard_flag;

#if CO_SUPPORT_DEVICE_SELF_TEST
share_data_zi volatile bool DST_pard_done_flag;
share_data_zi volatile bool DST_abort_flag;
share_data_zi volatile u32 DST_wl_index;
share_data_zi volatile bool DST_pard_fail_flag;
share_data_zi u32 DST_pard_fail_LBA[2];
fast_data_zi struct timer_list DST_timer; ///< control DST speed
#endif

#if PLP_TEST
share_data_zi volatile ncl_w_req_t *req_debug;
share_data_zi volatile u16 die;
share_data_zi volatile bool ucache_flush_flag;
share_data_zi volatile bool fill_dummy_flag;

//share_data_zi volatile u32 fill_done_cnt,fill_req_cnt;
#endif

share_data u32 LEDparam = 1;
#if (Synology_case)
share_data u32 host_unc_err_cnt =0;
#endif

share_data_zi volatile int temperature;

fast_data static LIST_HEAD(_req_pending);						  ///< pending request

fast_data_zi bool dtag_gcing = false; ///< dtag gc ongoing
fast_data_zi u8 cur_power_state = 0;
fast_data_zi u32 rdisk_free_wd_rec = 0;
fast_data_zi u32 rdisk_free_wd_req = 0;
fast_data_zi u8 mi_cmd0_g[256];

share_data_zi volatile u32 shr_dtag_ins_cnt = 0;
share_data_zi volatile ns_t ns[INT_NS_ID]; ///< nvme namespace attribute, only updated in FTL, nsid was 0 base
share_data_zi volatile ftl_flags_t shr_ftl_flags;
share_data_zi volatile read_only_t read_only_flags;
share_data_zi volatile none_access_mode_t noneaccess_mode_flags;
share_data_zi volatile u8  cur_ro_status;
share_data_zi volatile u8  eccu_during_change;

share_data_zi volatile fcns_attr_t fcns_attr[INT_NS_ID + 1]; ///< shared ftl core attributes
share_data_zi volatile u16 ua_btag;
share_data_zi volatile u16 wunc_btag;
share_data_ni volatile lda_t wunc_ua[2];
share_data_ni volatile u8 wunc_bmp[2];

share_data_zi volatile u8 shr_is_gc_emgr;
share_data_zi volatile bool shr_is_gc_force;
share_data_zi volatile u8 otf_forcepbt;
share_data_zi volatile pbt_resume_t *pbt_resume_param;

share_data_zi volatile u32 shr_gc_perf;
share_data_zi volatile u32 shr_host_write_cnt;
share_data_zi volatile u32 shr_host_write_cnt_old;
share_data_zi volatile u32 pre_shr_host_write_cnt;
share_data_zi volatile u32 shr_gc_rel_du_cnt;
share_data_zi volatile u8 shr_gc_fc_ctrl;
share_data_zi volatile u8 shr_gc_read_disturb_ctrl;
share_data_zi volatile u8 shr_no_host_write;
share_data_zi volatile u8 shr_gc_no_slow_down;
share_data_zi volatile u32 shr_tbl_flush_perf;
share_data_zi volatile u32 shr_host_perf;
share_data_zi volatile u32 shr_hostw_perf;
share_data_zi volatile u32 shr_bew_perf;
share_data_zi void *shr_ddtag_meta;
share_data_zi volatile u32 shr_gc_ddtag_start;
share_data_ni volatile u32 shr_l2p_srch_nid[BTN_CMD_CNT];
share_data_zi volatile rc_host_queue_t shr_rc_host_queue;
share_data_zi spb_queue_t spb_queue[NS_SUPPORT][MAX_SPBQ_PER_NS];
share_data_zi spb_queue_t int_spb_queue[2];
share_data_zi pda_t *shr_ins_lut;
share_data_zi volatile bool shr_ftl_save_qbt_flag;
share_data_zi volatile bool shr_format_copy_vac_flag;
share_data_zi volatile bool shr_trim_vac_save_done;

share_data_zi volatile bool stop_host_ncl;

share_data_zi volatile u32 shr_pa_rd_ddtag_start;

share_data_zi spb_rt_flags_t *spb_rt_flags;
share_data_zi volatile spb_id_t host_spb_close_idx; /// runtime close blk idx   Curry
share_data_zi volatile spb_id_t gc_spb_close_idx;   /// runtime close blk idx   Curry
share_data_zi volatile p2l_ext_para_t p2l_ext_para;
share_data_zi volatile u32 shr_p2l_grp_cnt;//ray
share_data_zi volatile u32 shr_wl_per_p2l;
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)
share_data_zi volatile bool delay_flush_spor_qbt;
share_data_zi volatile u32 spor_qbtsn_for_epm;
#endif

share_data_zi volatile bool save_evlog_start;

share_data_zi volatile bool PLA_FLAG;
share_data_zi volatile bool PLN_FLAG;
share_data_zi volatile bool PLN_evt_trigger;
share_data_zi volatile bool PLN_keep_50ms;
share_data_zi volatile bool PLN_in_low;
share_data_zi volatile bool PLN_FORMAT_SANITIZE_FLAG;
share_data_zi volatile bool PLN_GPIOISR_FORMAT_SANITIZE_FLAG;
share_data_zi volatile bool PLN_open_flag;
share_data_zi volatile bool PLN_flush_cache_end;
share_data_zi volatile bool PWRDIS_open_flag;
share_data_zi volatile bool PWRDIS_664;
share_data_zi volatile bool PLP_IC_SGM41664;
share_data_zi volatile u32 SGM_data;
share_data_zi volatile bool PLP_IC_SYTC88_664;

fast_data u8 evt_pln_format_sanitize = 0xff;
fast_data_zi u64 pln_loop_time;

fast_data_zi u64 plp_cache_tick;

share_data volatile u16 host_dummy_start_wl = INV_U16;

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
share_data_zi pda_t shr_plp_slc_start_pda;
share_data_zi pda_t shr_plp_slc_end_pda;
share_data_zi u32   shr_plp_slc_need_gc;
share_data_zi volatile u8  shr_slc_flush_state; //0 is finish , 1/2 in gc flow , 3/4 in slc erasa flow
share_data_zi volatile u8  shr_slc_need_trigger;
share_data_zi u64  l2p_base_addr;
share_data_zi volatile bool slc_call_epm_update;
#endif

#if (PLP_SUPPORT == 0)
share_data_zi volatile u8 non_plp_format_type; //
share_data_zi volatile bool shr_nonplp_gc_state;
#endif

#if (FW_BUILD_VAC_ENABLE == mENABLE)
share_data_zi volatile u8 CPU3_spor_state;
share_data_zi volatile bool vc_recon_busy[4];
share_data_zi u32* recon_vc;
share_data_zi volatile u8 fw_memory_not_enough;
share_data_zi u32* CPU1_cache_sourece;
#endif

//For DST Eric 20240103
share_data_zi tDST_LOG *smDSTInfo;
fast_data u8 evt_dev_self_test = 0xFF;
fast_data u8 evt_dev_self_test_delay = 0xFF;

share_data_zi pda_t *gc_pda_list;
share_data_zi lda_t *gc_lda_list;   //gc lda list from p2l
share_data_zi io_meta_t *gc_meta;
share_data_zi u16 gc_next_blk_cost;
share_data_zi u16 gc_gen_p2l_num;
share_data_zi u16 shr_max_alloc_wl_speed;
share_data_zi u16 global_capacity;
share_data_ni volatile gc_pda_grp_t gc_pda_grp[MAX_GC_PDA_GRP];
#ifdef DUAL_BE
share_data_zi volatile bool ncl_busy[2];
share_data_zi volatile struct finst_fifo_t finst_fifo[2];
share_data_zi struct sram_finst_info_t *sram_finst_info;
share_data_zi volatile read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];
#else
share_data_zi volatile read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];
#endif
share_data volatile u32 cache_handle_dtag_cnt = 0;
share_data volatile u32 otf_fua_cmd_cnt = 0;
fast_data_ni TFtlPbftType tFtlPbt;
share_data_zi volatile u16 ps_open[3];
share_data_zi volatile u8 QBT_BLK_CNT;
share_data_zi volatile u8 ftl_pbt_cnt;
//share_data_zi volatile u8 ftl_pbt_idx;
share_data_zi volatile bool pbt_query_ready;
share_data_zi volatile bool shr_qbtfilldone;
share_data_ni volatile pbt_updt_ntf_que_t pbt_updt_ntf_que;
share_data_ni volatile close_blk_ntf_que_t close_host_blk_que;
share_data_ni volatile close_blk_ntf_que_t close_gc_blk_que;
share_data_ni dtag_free_que_t dtag_free_que; ///< dtag release queue after program and updt done
share_data_ni volatile l2p_updt_que_t l2p_updt_que;
share_data_ni volatile l2p_updt_ntf_que_t l2p_updt_ntf_que;
share_data_ni volatile l2p_updt_que_t l2p_updt_done_que;
share_data_zi volatile int _fc_credit; ///< flow control credit, control how many du could be pushed to btn for write
share_data_zi gc_src_data_que_t *gc_src_data_que;
share_data_zi volatile gc_res_free_que_t *gc_res_free_que;
share_data_zi volatile void *shr_fe_info; ///< fe info buffer, should be allocated by FTL, and nand page size
share_data_zi ftl_stat_t ftl_stat;
fast_data_zi wde_parser_t _wde_parser = {.state = 0, .ins = 0, .rls_cmd_cnt = 0};
fast_data_zi ftl_flush_data_t _flush_ctx = {.nsid = 0}; ///< used for req flush, use nsid as resource busy or idle
fast_data_zi ftl_flush_data_t vwc_flush_ctx = {.nsid = 0}; ///< 20240126 Eric for VWC flush
share_data_ni volatile enum du_fmt_t host_du_fmt;				///< host du format, init in FTL
share_data volatile pblk_t fc_pmu_pblk;							///< pmu swap save space use nand block
share_data volatile u16 cur_page;								///< pmu swap use nand block current write page
share_data volatile u16 need_page;								///< pmu swap next save need nand page
share_data volatile u32 cpu1_btcm_free_start;					///< share cpu1 btcm heap address for pmu swap
share_data volatile u8 shr_total_die;
share_data volatile u32 shr_program_fail_count;
share_data volatile u32 shr_erase_fail_count;
share_data volatile u32 shr_die_fail_count;
share_data_zi u32 shr_wunc_meta_flag;
share_data_zi u32 shr_pstream_busy;
share_data volatile u32 shr_avg_erase;
share_data volatile u32 shr_max_erase;
share_data volatile u32 shr_min_erase;
share_data volatile u32 shr_total_ec;
//share_data u32 shr_end_to_end_detection_count;	// Separate to 2B * 3 for different error type in PI, DBG, SMARTVry
share_data volatile u16 shr_E2E_RefTag_detection_count = 0;
share_data volatile u16 shr_E2E_AppTag_detection_count = 0;
share_data volatile u16 shr_E2E_GuardTag_detection_count=0;

share_data volatile u64 shr_nand_bytes_written;
share_data_zi volatile u64 shr_unit_byte_nw;
share_data volatile u32 shr_host_bytes_written;
share_data volatile u32 shr_bad_block_failure_rate;
share_data volatile ResetHandle_t smResetHandle;
share_data volatile bool smResetSemiDtag = false;
share_data volatile bool smStopPushDdrDtag = false;

#if XOR_CMPL_BY_PDONE
share_data_zi volatile u16 stripe_pdone_cnt[MAX_STRIPE_ID];
#endif
fast_data_zi static u8 _pat_gen_test_id;
fast_data_zi static pat_gen_cb pat_gen_users[PATTERN_GEN_USER_CNT];
fast_data_zi static UNUSED u8 pat_gen_user_oft = 0;
share_data_zi volatile u32 *shr_l2p_ready_bmp;
fast_data u8 evt_ftl_ready_chk = 0xff;
fast_data u8 evt_req_exec_chk = 0xff;
#if (CO_SUPPORT_READ_AHEAD == TRUE)
fast_data u8 evt_otf_readcmd_chk = 0xff;
#endif
fast_data u8 evt_flush_done = 0xFF;
fast_data u8 evt_l2p_swq_chk = 0xff;
fast_data_ni static rdisk_req_statistics_t req_statistics;
share_data_ni ncl_w_req_t ncl_w_reqs[RDISK_NCL_W_REQ_CNT];
share_data_ni struct timer_list GetSensorTemp_timer;
fast_data_zi u16 Temp_CMD0_Update;
fast_data_zi bool I2C_read_lock;
share_data_zi volatile bool cc_en_set = false;
//share_data_zi volatile bool gc_wl_speed_flag = false;
share_data_zi volatile bool ctl_wl_flag;
share_data_zi volatile bool gc_suspend_start_wl;
share_data_zi volatile bool rd_gc_flag;
share_data_zi volatile spb_id_t rd_open_close_spb[FTL_CORE_GC + 1];
share_data_zi volatile u8 shr_lock_power_on;
#if SYNOLOGY_SETTINGS
share_data_zi volatile bool gc_wcnt_sw_flag = false;
#endif
#if(PLP_SLC_BUFFER_ENABLE == mENABLE)
//share_data_zi volatile u8 shr_read_lock_power_on;
#endif
share_data_zi u32 wl_cnt;
share_data_zi u32 rd_cnt;
share_data_zi u32 dr_cnt;
share_data_zi u32 gc_cnt;

share_data_zi volatile u8 power_on_update_epm_flag;
share_data_zi volatile bool first_usr_open;
share_data_zi volatile bool shr_qbtflag;
share_data volatile bool shr_qbt_prog_err = false;

#if (RD_NO_OPEN_BLK == mENABLE)
share_data_zi volatile bool host_idle = false;
#endif

//extern u8 evt_reset_disk;
//share_data u8 show_log;

#if epm_enable
share_data epm_info_t *shr_epm_info; //only pointer  need epm.c init
#endif

share_data volatile u32 GrowPhyDefectCnt; //for SMART growDef Use
share_data volatile bool all_init_done = false;
#ifdef ERRHANDLE_GLIST
share_data_zi volatile bool fgFail_Blk_Full = false;		// need define in sram
share_data_zi volatile bool gc_suspend_done_flag = false;		// need define in sram
share_data_zi sGLTable *pGList;                 // need define in Dram  // GL_mod, Paul_20201130
share_data_zi volatile u16* wOpenBlk; // need define in Dram
share_data_zi volatile u8 bErrOpCnt = 0;
share_data_zi volatile u8 bErrOpIdx = 0;

share_data_zi volatile bool FTL_INIT_DONE = false;
share_data_zi volatile u32 EPM_LBLK = 0; //Tony 20201006
share_data_zi u32 _max_capacity;
share_data_zi u32 cache_cnt;

share_data_zi volatile u32 fw_ard_cnt;
share_data_zi bool FW_ARD_EN;
share_data_zi u8   FW_ARD_CPU_ID;

share_data_zi volatile ncl_w_req_t *CloseReq;
share_data_zi volatile u32 *CloseAspb;
share_data_zi volatile u32 ClSBLK_H = 0;									//TBC_EH, host close blk layer

share_data_zi sGLEntry errInfo2;
share_data_zi sGLEntry errInfo4;
share_data_zi volatile u32 cpu4_glist_dtag;

share_data_zi volatile sGLEntry* Err_Info_Cpu4_Addr;
share_data_zi volatile u32 Err_Info_Wptr;
share_data_zi volatile u32 Err_Info_Rptr;
share_data_zi volatile u32 Err_Info_size;

share_data_zi volatile sEH_Manage_Info sErrHandle_Info;
share_data_zi volatile MK_FailBlk_Info Fail_Blk_Info_Temp;
share_data_zi volatile u8 bFail_Blk_Cnt = 0; //need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT] = {0}; // Record SPB under error handling, to avoid trigger ErrHandle_Task by phyBlks. // DBG, PgFalVry
share_data_zi volatile bool profail = false;  // TBD_EH, Temp use for ASSERT Prog failed after mark defect.
share_data_zi volatile bool gc_pro_fail = false;
share_data_zi volatile spb_id_t rdOpBlkFal[FTL_CORE_GC + 1];			// need define in sram, refered in ftl_core_done, ISU, RdOpBlkHdl
//share_data_zi volatile bool Ftl_Pg_fail = false;	// FET, RmUselessVar
//share_data_zi volatile u16 pbt_Pg_fail_blk = INV_U16;
//share_data_zi volatile u16 Pg_fail_blk = INV_U16;

share_data_zi volatile u64 Nand_Written = 0;
#ifdef ERR_GC_PRO_FAIL
share_data_zi volatile bool trigger_gc_pro_fail = false;
share_data_zi volatile u8 trigger_gc_cnt = 0;
share_data_zi volatile u32 blk_gc = 0;
#endif
#endif

#if RAID_SUPPORT_UECC
share_data_zi volatile u32 nand_ecc_detection_cnt;      //host + internal 1bit fail detection cnt
share_data_zi volatile u32 host_uecc_detection_cnt;     //host 1bit fail detection cnt
share_data_zi volatile u32 internal_uecc_detection_cnt; //internal 1bit fail detection cnt
share_data_zi volatile u32 uncorrectable_sector_count;  //host raid recovery fail cnt
share_data_zi volatile u32 internal_rc_fail_cnt;        //internal raid recovery fail cnt
share_data_zi volatile u32 host_prog_fail_cnt;          //host program fail count

share_data volatile SMART_uecc_info_t smart_uecc_info[smart_uecc_info_cnt];
share_data volatile u16 uecc_smart_ptr;
#endif

#ifdef ERRHANDLE_ECCT
share_data_zi volatile stECC_table *pECC_table;  //tony 20201030
share_data_zi volatile u16 ecct_cnt = 0;  //ecct cnt in sram  //tony 20201228
share_data_zi stECCT_ipc_t rc_ecct_info[MAX_RC_REG_ECCT_CNT];
share_data_zi tEcctReg ecct_pl_info_cpu2[MAX_RC_REG_ECCT_CNT];
share_data_zi tEcctReg ecct_pl_info_cpu4[MAX_RC_REG_ECCT_CNT];
share_data_zi volatile u8 rc_ecct_cnt;
share_data_zi u8 ecct_pl_cnt2;
share_data_zi u8 ecct_pl_cnt4;
#endif

#ifdef RETRY_COMMIT_EVENT_TRIGGER
#ifdef NCL_FW_RETRY_EX
share_data volatile recover_commit_t recommit_cpu2[RTY_QUEUE_CH_CNT];
share_data volatile recover_commit_t recommit_cpu4[RTY_QUEUE_CH_CNT];
#else
share_data volatile recover_commit_t recommit_cpu2;
share_data volatile recover_commit_t recommit_cpu4;
#endif
#endif
#ifdef NCL_RETRY_PASS_REWRITE
share_data volatile u32 rd_rewrite_ptr;
share_data volatile retry_rewrite_t rd_rew_info[DDR_RD_REWRITE_TTL_CNT];
share_data volatile u32 rd_rew_start_dtag;
share_data volatile u8 rd_rew_level = 0;
#endif
#ifdef DBG_NCL_SET_FEA_BE4_READ
share_data volatile struct ncl_cmd_t setfea_ncl_cmd[SETFEA_NCL_CMD_MAX_CNT];
share_data volatile u8 setfea_nclcmd_cnt;
#endif

//joe add NS 20200813
//joe 20200525
#ifdef NS_MANAGE						 //joe add define 20200916
extern struct ns_section_id ns_sec_id[]; //joe 20200525
extern struct ns_array_manage *ns_array_menu;
share_data volatile u8 full_1ns;
//joe add sec size 20200817
#define NS_SIZE_GRANULARITY_RDISK1 (0x200000000 >> LBA_SIZE_SHIFT1) //joe 20200525
#define NS_SIZE_GRAN_LDA_RDISK1 (NS_SIZE_GRANULARITY_RDISK1 >> 3)	//joe add 20200525
#define NS_SIZE_GRANULARITY_BITOP1 (24) //joe 20200525
#define NS_SIZE_GRANULARITY_RDISK2 (0x200000000 >> LBA_SIZE_SHIFT2) //joe 20200525
#define NS_SIZE_GRAN_LDA_RDISK2 (NS_SIZE_GRANULARITY_RDISK2 >> 3)	//joe add 20200525
#define NS_SIZE_GRANULARITY_BITOP2 (21) //joe 20200525
share_data volatile u16 ns_order;
//joe 20200525
//joe add NS 20200813
#endif
//joe add sec size 20200817
extern u8 host_sec_bitz;
extern u16 host_sec_size;
//joe add sec size 20200817

share_data_zi volatile stGC_GEN_INFO gGCInfo;

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
share_data_ni struct timer_list telemetry_log_update_timer;
share_data bool needupdate_flag = true;
#endif

#if GC_SUSPEND_FWDL
share_data_zi volatile u32 fwdl_gc_handle_dtag_start;
#endif
u64 LedDetectTime = 0;
fast_data u8 evt_led_set = 0xFF;
extern btn_smart_io_t wr_io;
extern btn_smart_io_t rd_io;
share_data u16 _SensorTemp[3];
u64 SensorDetectTime = 0;
u8 Detect_FLAG = 0;

#ifdef MDOT2_SUPPORT
u8 addr_w[1] = {0x90}; //PJ1 only one temp sensor, WillWu 2023.6.5
#else
u8 addr_w[3] = {0x92, 0x96, 0x94};
#endif

u8 SenIdx = 0;
u8 SenRetryCnt = 0;
u8 BadSenTrk = 0;
u8 LogPrtCnt = 0;
u8 shift_cnt =0;
u8 TempShiftCnt = 0;
u8 TempShiftInit= 0;
u16 TempShiftCheck[3] = {303, 303, 303};    //degree K
//inflight cmd print
share_data_ni struct timer_list print_timer;
#if PLP_SUPPORT == 0
//update smart and flush
share_data_ni struct timer_list smart_timer;
#endif
share_data u32 WUNC_DTAG = DDR_WUNC_DDUMMY_DTAG_START;
void rdisk_wunc(req_t *req);
//for led
share_data volatile bool plperror = false;
extern Ec_Table *EcTbl;
extern u16 ns_valid_sec;
extern u16 drive_total_sector;
//for sn
share_data volatile char ascll_num[] = {"0123456789"};
share_data volatile char ascll_alp[] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZ"};
share_data volatile char dri_sn[32] = {""};
extern AGING_TEST_MAP_t *MPIN;
share_data_zi  volatile u8 stop_gc_done;
share_data volatile bool hostReset = false;
extern u32 gRstFsm;

//for vu
//extern u32 shr_NorID;
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
share_data_zi volatile spb_id_t plp_spb_flush_p2l = 0;
#endif

share_data_zi volatile u8 plp_evlog_trigger;
share_data_zi volatile u8 plp_trigger = 0;
share_data_zi volatile u8 plp_epm_update_done;
share_data_zi volatile u8 plp_epm_back_flag;

share_data_zi volatile u8 CPU1_plp_step;
share_data_zi volatile u8 CPU2_plp_step;
share_data u16* plp_host_open_die = NULL;
share_data_zi volatile u32 plp_log_number_start;
share_data_zi volatile u32 plp_record_cpu1_lr;
//share_data_zi volatile u8 CPU3_plp_step;
share_data_zi volatile u8 cpu_feedback[4];

share_data_zi volatile u32 cpu2_streaming_read_cnt __attribute__ ((aligned(64)));
share_data_zi volatile u32 cpu4_streaming_read_cnt __attribute__ ((aligned(64)));
share_data_zi volatile u8 cpu2_cancel_streaming;
share_data_zi volatile u8 cpu4_cancel_streaming;
#if PLP_DEBUG
share_data volatile u32 cpu2_read_cnt = 0;
share_data volatile u32 cpu4_read_cnt = 0;
share_data volatile u32 fill_up_dtag_start;
share_data volatile io_meta_t *fill_up_dtag_meta;
#endif
share_data volatile bool plp_test_flag = false;
share_data volatile u64  plp_down_time = 0;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
share_data volatile u8 SLC_init = false;
share_data volatile u32 max_usr_blk_sn = INV_U32;
#endif

#ifdef History_read
ddr_sh_data hist_tab_offset_t *hist_tab_ptr_st;
#endif
ddr_sh_data ard_que_t fw_ard_que;  //FW ARD queue
ddr_sh_data u8 epm_Glist_updating;  //Indicates that epm is updating Glist.

share_data_zi volatile bool in_ppu_check;
share_data_zi volatile u8 ppu_cnt;
ddr_sh_data volatile ppu_list_t ppu_list[MAX_PPU_LIST];
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
static void io_mgr_cmd_exec(req_t *req, btn_cmd_t *bcmd);
static void req_resume(void);
static void bcmd_resume(void);

static inline u32 pcie_wrap_readl(u32 reg)
{
	return readl((void *)(PCIE_WRAP_BASE + reg));
}

static void rdisk_alloc_streaming_rw_dtags(void);
#if (FW_BUILD_VAC_ENABLE == mENABLE)
void l2p_vac_recon(volatile cpu_msg_req_t *req);
#endif
/*!
 * register a callback function for data pattern generation
 * @param cb_func	callback function for generated data pattern
 *
 * @ return u8		user_id, used by caller to distinguish itself
 */
init_code u8 pat_gen_user_register(pat_gen_cb func)
{
	sys_assert(PATTERN_GEN_USER_CNT > pat_gen_user_oft);
	pat_gen_users[pat_gen_user_oft] = func;

	return pat_gen_user_oft++;
}

/*!
 * @brief save nvme info to nand
 *
 * @param req	should be shutdown request
 *
 * @return	not used
 */
void nvme_info_save(req_t *req);

/*!
 * @brief rdisk version ncl command empty function for FE
 *
 * @return	check if NCL was idle
 */
// fast_code bool rdisk_ncl_cmd_empty(void)
// {
// 	if (!IS_NCL_IDLE())
// 		return false;
//
// 	return true;
// }

ddr_code void pat_gen_req_done(u16 btag, dtag_t *dtag, u32 cnt)
{
	btn_cmd_t *bcmd = btag2bcmd(btag);
	u32 nvm_slot = bcmd->dw0.b.nvm_cmd_id;
	u32 user_idx = MAX_FW_SQ_ID - bcmd->dw1.b.cmd_sqid;
	u32 i;

	for (i = 0; i < cnt; i++)
	{
		if (dtag[i].b.type_ctrl & BTN_SEMI_STREAMING_MODE)
		{
			dtag_t sdtag = {.dtag = dtag[i].dtag};
			dtag_t ddtag = {.dtag = dtag[i].dtag};

			sdtag.b.type_ctrl = 0;
			ddtag.b.type_ctrl = 0;
			sdtag.dtag = smdtag2sdtag(sdtag.dtag);
			bm_free_semi_write_load(&sdtag, 1, 0);

			ddtag.dtag = smdtag2ddtag(ddtag.dtag);
			dtag[i].dtag = ddtag.dtag;
			disp_apl_trace(LOG_ERR, 0xde66, "FW: sdtag %x ddtag %x\n", sdtag.dtag, ddtag.dtag);
		}
		else
		{
			dtag[i].b.type_ctrl = 0;
			disp_apl_trace(LOG_ERR, 0x9c02, "FW: ddtag %x\n", dtag[i].dtag);
		}
	}

	// identify caller and inform it
	if (pat_gen_users[user_idx])
		pat_gen_users[user_idx](nvm_slot, dtag, cnt);

	// release fw btn command
	fw_btag_release(btag);

	// release nvm_slot to data pattern mechanism
	pat_gen_rel(nvm_slot);
}

fast_code void read_recoveried_commit(bm_pl_t *bm_pl, u16 pdu_bmp)
{
	#if (Synology_case)
    extern synology_smart_statistics_t *synology_smart_stat;
    synology_smart_stat->data_read_retry_cnt++;
    #endif

	btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
	lda_t lda;
	u8 num_lba = 0;
	if (host_sec_bitz == 9) //joe add sec size 20200818
		num_lba = 8;
	else
		num_lba = 1;
	u8 lba_mask = num_lba - 1;
	bm_pl->pl.nvm_cmd_id = bcmd->dw0.b.nvm_cmd_id;
	bm_pl->pl.type_ctrl = BTN_NVME_QID_TYPE_CTRL_RDONE;
	if (pdu_bmp == 0)
	{
	readable:
        //disp_apl_trace(LOG_INFO, 0, "[DBG]read_recoveried_commit, dtag: 0x%x", (bm_pl->pl.dtag & 0x3FFFFF) );
		bm_rd_de_push(bm_pl);
		return;
		// return DTAG in RDONE
	}

	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_pl->pl.btag);
	u16 ndu = bcmd_ex->ndu;
	u64 slba = bcmd_get_slba(bcmd);
	u16 bmp = 0xFFFF;

	//lda = LBA_TO_LDA(slba) + bm_pl->pl.du_ofst;
	lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)) + bm_pl->pl.du_ofst; //joe add sec size 20200820
	disp_apl_trace(LOG_WARNING, 0xafab, "lda %x pdu %x", lda, pdu_bmp);
	if (bm_pl->pl.du_ofst == 0)
	{
		//u32 head = slba & NR_LBA_PER_LDA_MASK;//joe add sec size 20200817
		//u32 cnt = min(NR_LBA_PER_LDA - head, bcmd->dw3.b.xfer_lba_num);
		u32 head = slba & lba_mask;
		u32 cnt = min(num_lba - head, bcmd->dw3.b.xfer_lba_num);

		bmp = ((1 << cnt) - 1) << head;
		disp_apl_trace(LOG_WARNING, 0x88f8, "head %x", bmp);
	}
	else if (bm_pl->pl.du_ofst == ndu - 1)
	{
		u64 elba = slba + bcmd->dw3.b.xfer_lba_num;
		//u32 tail = elba & NR_LBA_PER_LDA_MASK;//joe add sec size 20200817
		u32 tail = elba & lba_mask;

		bmp = (1 << tail) - 1;
		disp_apl_trace(LOG_WARNING, 0xaddf, "tail %x", bmp);
	}

	if ((pdu_bmp & bmp) == 0)
		goto readable;

	bm_err_commit(bm_pl->pl.du_ofst, bm_pl->pl.btag);
	extern void read_recoveried_done(dtag_t dtag);
	dtag_t dtag = {.dtag = bm_pl->pl.dtag};

	read_recoveried_done(dtag);
}

fast_code void ipc_read_recoveried_commit(volatile cpu_msg_req_t *req)
{
	bm_pl_t *bm_pl = (bm_pl_t *)req->pl;

	read_recoveried_commit(bm_pl, req->cmd.flags);
	//cpu_msg_sync_done(req->cmd.tx);
}
#ifdef NS_MANAGE //joe add define 20200916
extern bool esr_err_fua_flag;
fast_code void rdisk_fua_done(u16 btag, dtag_t dtag)
{
	if (btag & FUA_BTAG_TAG)
	{
		btn_cmd_t *bcmd = btag2bcmd(btag & BTAG_MASK);
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag & BTAG_MASK);

		bool fua = is_fua_bcmd(bcmd);
		if(!esr_err_fua_flag)
			sys_assert(fua);

		sys_assert(bcmd_ex->du_xfer_left);
		if ((--bcmd_ex->du_xfer_left == 0) && (bcmd_ex->flags.b.wr_err == 0))
		{
            otf_fua_cmd_cnt--;
			btn_cmd_rels_push_bulk(&bcmd, 1);
		}
	}
    #ifdef LJ1_WUNC
	else
	{
        if(dtag.dtag != EVTAG_ID){
            _reset_pdu_bmp(dtag);
        }
        if(btag & BTAG_MASK){
    		req_t *req = list_first_entry(&_req_pending, req_t, entry2);

    		sys_assert(req->opcode == REQ_T_WUNC);
    		sys_assert(req->op_fields.wucc.left);
    		req->op_fields.wucc.left--;
    		if (req->op_fields.wucc.left == 0)
    		{
                wunc_ua[0] = INV_U32;
                wunc_bmp[0] =  0;
                wunc_ua[1] = INV_U32;
                wunc_bmp[1] =  0;
                otf_fua_cmd_cnt--;
                if (req->lba.srage.nlb != 0) {
                    //rdisk_wunc(req);
                    evt_set_cs(evt_rdisk_wunc_part, 0, 0, CS_TASK);
                }
                else {
        			disp_apl_trace(LOG_INFO, 0x9071, "wunc done");
        			req->completion(req);
        			list_del(&req->entry2);

                    flush_to_nand(EVT_HOST_WUNC);

        			if (rdisk_fe_flags.b.io_fetch_stop)
        			{
        				nvmet_io_fetch_ctrl(false); // enable io fetch
        				rdisk_fe_flags.b.io_fetch_stop = 0;
        			}
                    if(!list_empty(&_req_pending))
                        evt_set_cs(evt_wait_wcmd_idle, 0, 0, CS_TASK);
                }
    		}
	    }
	}
    #endif
}


fast_code u32 wde_discard_single(wde_parser_t *wde_parser, bm_pl_t *bm_pl)
{
	dtag_t dtag;

	dtag.dtag = bm_pl->pl.dtag;
	dtag.b.type_ctrl = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
#ifdef LJ1_WUNC
	reset_pdu_bmp(dtag);
#endif
	dtag.b.type_ctrl = 0;
	if (bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK)
		smdtag_recycle(dtag);
	else
		rdisk_dref_dec(&dtag.dtag, 1);

	return 1;
}

fast_code static void wde_parser_done(wde_parser_t *wde_parser, u32 cnt)
{
	sys_assert(wde_parser->left >= cnt);
	wde_parser->left -= cnt;

	if (wde_parser->left == 0)
	{
		sys_assert(wde_parser->bcmd_ex->flags.b.wr_err == 0);
		if (wde_parser->flags.b.fw_cmd)
		{
			pat_gen_req_done(wde_parser->btag, wde_parser->dtags, wde_parser->ins);
			wde_parser->ins = 0;
		}
		else if (wde_parser->flags.b.fua == 0)
		{
			wde_parser->rls_bcmds[wde_parser->rls_cmd_cnt] = wde_parser->bcmd;
			wde_parser->rls_cmd_cnt++;
			if (wde_parser->rls_cmd_cnt >= WCMD_RLS_MAX)
			{
				btn_cmd_rels_push_bulk(wde_parser->rls_bcmds, wde_parser->rls_cmd_cnt);
				wde_parser->rls_cmd_cnt = 0;
			}
		}

		wde_parser->state = WDE_PARSER_IDLE;
	}
}

fast_code static inline void rdisk_single_in(wde_parser_t *wde_parser, bm_pl_t *bm_pl, r_par_t *par)
{
	dtag_t dtag = {.dtag = bm_pl->pl.dtag};
	dtag.b.type_ctrl = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
#ifdef LJ1_WUNC
	reset_pdu_bmp(dtag);
#endif
//if(!(ns_order==1&&(drive_total_sector==ns_valid_sec))){
#if 1
	{
		#if TCG_WRITE_DATA_ENTRY_ABORT
		if (wde_parser->bcmd_ex->flags.b.tcg_wr_abrt)
		{
			wde_parser->off++;
			wde_discard_single(wde_parser, bm_pl);
		}
		else
		#endif
		{
			//disp_apl_trace(LOG_DEBUG, 0, "JOE: RDISK single in");

			//joe add NS 20200813
			//joe add 20200730  NRM write transfer NS LBA to GLOBAL LBA
			btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
			//joe add 20200525 NS
			u64 slba = bcmd_get_slba(bcmd);
			u64 slba_last; //joe 20200525
			u64 sec_id;//joe use bit op need use a big enough Unum
			u64 sec_tran;
			//disp_apl_trace(LOG_DEBUG, 0, "JOE:  slba:  H: 0x%x  L:0x%x ", (slba>>32),slba );
			if (host_sec_bitz == 9)
			{													  //joe add  sec size 20200817
				slba += bm_pl->pl.du_ofst <<3;					  //joe add sec size 20200817
				//u16 sec_id = (slba) / NS_SIZE_GRANULARITY_RDISK1; //joe add 20200520
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: nsid:%d now slba:0x%x sec_id:%d",bcmd->dw1.b.ns_id,slba,sec_id);
				//slba_last = slba - (sec_id)*NS_SIZE_GRANULARITY_RDISK1;															 //joe add 20200520
				sec_id = (slba) >> NS_SIZE_GRANULARITY_BITOP1; //joe add 20200520
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: nsid:%d now slba:0x%x sec_id:%d",bcmd->dw1.b.ns_id,slba,sec_id);
				slba_last = slba - ((sec_id)<<NS_SIZE_GRANULARITY_BITOP1);
				//slba = slba_last + (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]) * NS_SIZE_GRANULARITY_RDISK1; //joe add 20200610
				//slba = slba_last +( (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]) << NS_SIZE_GRANULARITY_BITOP1); //joe add 20200610
				sec_tran=(ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]);
				slba = slba_last +( (sec_tran) <<NS_SIZE_GRANULARITY_BITOP1);
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: sec_id:%d    ns_sec_id[%d].sec_id[%d]:%d ",sec_id,bcmd->dw1.b.ns_id - 1,sec_id,ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]);//joe 20200610
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: sec_id:%d    ns_array_menu->ns_array[%d].sec_id[%d]:%d",sec_id,bcmd->dw1.b.ns_id - 1,sec_id,ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]);
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: transfer slba:  H: 0x%x  L:0x%x   slba_last:0x%x", (slba >> 32), slba, slba_last);
			}
			else
			{
				slba += bm_pl->pl.du_ofst;					  //joe add sec size 20200817
				//u16 sec_id = (slba) / NS_SIZE_GRANULARITY_RDISK2; //joe add 20200520
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: nsid:%d now slba:0x%x sec_id:%d",bcmd->dw1.b.ns_id,slba,sec_id);
				//slba_last = slba - (sec_id)*NS_SIZE_GRANULARITY_RDISK2;																															   //joe add 20200520
				//slba = slba_last + (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]) * NS_SIZE_GRANULARITY_RDISK2;																   //joe add 20200610
				sec_id = (slba) >> NS_SIZE_GRANULARITY_BITOP2; //joe add 20200520
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: nsid:%d now slba:0x%x sec_id:%d",bcmd->dw1.b.ns_id,slba,sec_id);
				slba_last = slba -( (sec_id)<<NS_SIZE_GRANULARITY_BITOP2);																															   //joe add 20200520
				//slba = slba_last + ((ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]) << NS_SIZE_GRANULARITY_BITOP2);
				sec_tran=(ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]);
				slba = slba_last +( (sec_tran) <<NS_SIZE_GRANULARITY_BITOP2);
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: sec_id:%d    ns_sec_id[%d].sec_id[%d]:%d ", sec_id, bcmd->dw1.b.ns_id - 1, sec_id, ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]); //joe 20200610
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: transfer slba:  H: 0x%x  L:0x%x   slba_last:0x%x", (slba >> 32), slba, slba_last);
			}

			lda_t lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)); //joe add sec size 20200817
			//joe add NS 20200813
			//disp_apl_trace(LOG_DEBUG, 0, "JOE: transfer lda:  0x%x", lda);
			wde_parser->dtags[wde_parser->ins] = dtag;
			//wde_parser->ldas[wde_parser->ins] = wde_parser->start;
			wde_parser->ldas[wde_parser->ins] = lda; //joe add NS 20200813
			wde_parser->par[wde_parser->ins].all = 0;

			if (par && par->all)
				wde_parser->par[wde_parser->ins].all = par->all;

			if (wde_parser->flags.b.fua)
				wde_parser->btags[wde_parser->ins] = FUA_BTAG_TAG|wde_parser->btag;
			else
				wde_parser->btags[wde_parser->ins] = 0;

			wde_parser->ins++;
			wde_parser->off++;
			//wde_parser->start++;
			#if 0//(TRIM_SUPPORT == ENABLE)
			if(IsCMDinTrimTable(lda, lda,1)){
				RegOrUnregTrimTable(lda,1,Unregister);
				UpdtTrimInfo(lda, 1, Unregister);
			}
			#endif
			if (wde_parser->ins >= WDE_NRM_INS_MAX)
			{
				ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
									   wde_parser->btags, wde_parser->par, wde_parser->ins);
				wde_parser->ins = 0;
			}
		}
	}
#else
	{
		wde_parser->dtags[wde_parser->ins] = dtag;//for the case of 1ns and fulldisk
		wde_parser->ldas[wde_parser->ins] = wde_parser->start;
		wde_parser->par[wde_parser->ins].all = 0;
		//disp_apl_trace(LOG_DEBUG, 0, "JOE single: lda:  0x%x  bm_pl->pl.du_ofst:%d",wde_parser->start,bm_pl->pl.du_ofst);
		if (par && par->all)
			wde_parser->par[wde_parser->ins].all = par->all;

		if (wde_parser->flags.b.fua)
			wde_parser->btags[wde_parser->ins] = FUA_BTAG_TAG|wde_parser->btag;
		else
			wde_parser->btags[wde_parser->ins] = 0;
		wde_parser->ins++;
		wde_parser->off++;
		wde_parser->start++;
		#if(TRIM_SUPPORT == ENABLE)
		if(IsCMDinTrimTable(wde_parser->start, wde_parser->start)){
			RegOrUnregTrimTable(wde_parser->start,1,Unregister);
			UpdtTrimInfo(wde_parser->start, 1, Unregister);
		}
		#endif
		if (wde_parser->ins >= WDE_NRM_INS_MAX)
		{
			ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
								   wde_parser->btags, wde_parser->par, wde_parser->ins);
			wde_parser->ins = 0;
		}
	}
#endif
}

fast_code NOINLINE void wde_parser_drop(wde_parser_t *wde_parser)
{
	int drop_cnt = wde_parser->total - wde_parser->left;
	int dropped = 0;

	while (wde_parser->ins && drop_cnt)
	{
		bm_pl_t bm_pl = {.all = 0};
		dtag_t dtag;

		wde_parser->ins--;
		dtag = wde_parser->dtags[wde_parser->ins];
		bm_pl.pl.type_ctrl = dtag.b.type_ctrl;
		bm_pl.pl.dtag = dtag.dtag & 0xFFFFFF;
		wde_discard_single(wde_parser, &bm_pl);
		drop_cnt--;

		dropped++;
	}

	if (dropped)
		disp_apl_trace(LOG_ERR, 0xc3a5, "bcmd %d dropped %d", wde_parser->btag, dropped);

	wde_parser->state = WDE_PARSER_IDLE;
}

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
/*!
 * @brief delete telemetry log update timer in 10mins since last cmd and reset flag
 *
 * @return	not used
 */
static ddr_code void telemetry_log_update_timer_reset(void *data){

	//extern bool needupdate_flag;
	extern struct timer_list telemetry_log_update_timer;

	if(!needupdate_flag){
		needupdate_flag = true;
		nvme_apl_trace(LOG_ALW, 0x066a,"telemetry log update flag set to %d",needupdate_flag);
	}

	del_timer(&telemetry_log_update_timer);

}
#endif

fast_code NOINLINE void wde_parser_reset(void)
{
	if (_wde_parser.state == WDE_PARSER_BUSY)
		wde_parser_drop(&_wde_parser);

	if (_wde_parser.rls_cmd_cnt) {
		disp_apl_trace(LOG_INFO, 0x5f19, "wde bcmd rst %d", _wde_parser.rls_cmd_cnt);
		_wde_parser.rls_cmd_cnt = 0;
	}
}

fast_code static u32 rdisk_par_in(wde_parser_t *wde_parser, bm_pl_t *bm_pl)
{
	btn_cmd_t *bcmd = wde_parser->bcmd;
	u64 slba = bcmd_get_slba(bcmd);
	int du_ofst = bm_pl->pl.du_ofst;
	int nlba;
	int ofst;
	u8 num_lba = 0;
	if (host_sec_bitz == 9) //joe add sec size 20200818
		num_lba = 8;
	else
		num_lba = 1;
	u8 lba_mask = num_lba - 1;

	if (bm_pl->pl.btag != wde_parser->btag || bm_pl->pl.du_ofst != wde_parser->off)

		return 0;

	if (du_ofst == 0)
	{
		//ofst = LBA_OFST_LDA(slba);//joe add sec size 20200817
		//nlba = NR_LBA_PER_LDA - ofst;//joe add sec size 20200817
		ofst = ((slba)&lba_mask);
		nlba = num_lba - ofst;
		nlba = min(nlba, bcmd->dw3.b.xfer_lba_num);
	}
	else
	{
		u64 elba = slba + bcmd->dw3.b.xfer_lba_num;

		//nlba = elba & NR_LBA_PER_LDA_MASK;//joe add 20200817 sec size 20200817
		nlba = elba & lba_mask;
		ofst = 0;
	}

	r_par_t par = {.ofst = ofst, .nlba = nlba};
	rdisk_single_in(&_wde_parser, bm_pl, &par);

	return 1;
}

static inline u32 rdisk_nrm_in(wde_parser_t *wde_parser, u32 *addr, u32 cnt)
{
	int i = 0;
	int idx = wde_parser->ins;
//if(!(ns_order==1&&(drive_total_sector==ns_valid_sec))){
	#if 1
	#if TCG_WRITE_DATA_ENTRY_ABORT
	if (wde_parser->bcmd_ex->flags.b.tcg_wr_abrt)
	{
		for (i = 0; i < cnt; i++) {

			bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(addr[i]);

			if (bm_pl->pl.btag != wde_parser->btag || wde_parser->off != bm_pl->pl.du_ofst)
			{
				wde_parser->ins = idx;
				return i;
			}
			wde_parser->off++;
			wde_discard_single(wde_parser, bm_pl);
		}
		return cnt;
	}
	else
	#endif
	{
		lda_t lda=0;//joe add 20200917 write performance ns
		u16 cross_lbacnt=0,cross_du=0;
		u8 cross_flag_write=0;//joe add 20200917 write performance ns
		u64 sec_tran;
		u64 sec_tran2;
		u64 secid1=0,secid2=0;
		for (i = 0; i < cnt; i++) {
			bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(addr[i]);
			dtag_t dtag = {.dtag = bm_pl->pl.dtag};

			if (bm_pl->pl.btag != wde_parser->btag || wde_parser->off != bm_pl->pl.du_ofst)
			{
				wde_parser->ins = idx;

				return i;
			}

			wde_parser->off++;
			dtag.b.type_ctrl = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
	#ifdef LJ1_WUNC
			reset_pdu_bmp(dtag);
	#endif
		if(i==0){//joe add for write performance ns 20200917   just transfer first bm_pl in nrm case
			//joe add NS 20200813
			//joe add 20200730  NRM write transfer NS LBA to GLOBAL LBA
			btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
			//joe add 20200525  transfer NS
			u64 slba = bcmd_get_slba(bcmd);
			u64 slba2 = bcmd_get_slba(bcmd);
			u64 slba_last; //joe 20200525

			if(host_sec_bitz==9){
				bm_pl_t *bm_pl2 = (bm_pl_t *)dma_to_btcm(addr[cnt-1]);
				slba+=bm_pl->pl.du_ofst<<3;//8 means DU_size/sec_size=8
				slba2+=bm_pl2->pl.du_ofst<<3;// *8fix bug for edevx 20201020 joe
				//secid1=slba/NS_SIZE_GRANULARITY_RDISK1;
				//secid2=(slba2)/NS_SIZE_GRANULARITY_RDISK1;//joe T0 is xfer_lba_num
				secid1=slba>>NS_SIZE_GRANULARITY_BITOP1;
				secid2=(slba2)>>NS_SIZE_GRANULARITY_BITOP1;//joe T0 is xfer_lba_num
				disp_apl_trace(LOG_DEBUG, 0x8257, "JOE: secid:%d  secid2:%d  slba:0x%x  slba2:0x%x", secid1,secid2, slba,slba2);
				if(secid1 != secid2){
					cross_flag_write=1;
				//cross_lbacnt=(secid2)*NS_SIZE_GRANULARITY_RDISK1 - slba;
				cross_lbacnt=((secid2)<<NS_SIZE_GRANULARITY_BITOP1) - slba;
				if(cross_lbacnt%8==0)
					cross_du=(cross_lbacnt)>>3;
				else
					cross_du=(cross_lbacnt+8)>>3;	// +8 is prevent the count is not divided by eight!!! important(for 512 host size)
				}

				//slba_last = slba-(secid1)*NS_SIZE_GRANULARITY_RDISK1;
				//slba =slba_last+(ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid1])*NS_SIZE_GRANULARITY_RDISK1;
				slba_last = slba-((secid1)<<NS_SIZE_GRANULARITY_BITOP1);
				//slba =slba_last+((ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid1])<<NS_SIZE_GRANULARITY_BITOP1);
				sec_tran=(ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[secid1]);
				slba = slba_last +( (sec_tran) <<NS_SIZE_GRANULARITY_BITOP1);
				disp_apl_trace(LOG_DEBUG, 0x1845, "JOE: transfer slba:  H: 0x%x  L:0x%x   slba_last:0x%x",(slba>>32),slba,slba_last);
				//slba =(ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid1])*NS_SIZE_GRANULARITY_RDISK1;


			}else{
				bm_pl_t *bm_pl3 = (bm_pl_t *)dma_to_btcm(addr[cnt-1]);
				slba+=bm_pl->pl.du_ofst;// 1 means DU_size/sec_size=1
				slba2+=bm_pl3->pl.du_ofst;
				//secid1=slba/NS_SIZE_GRANULARITY_RDISK2;
				//secid2=(slba2)/NS_SIZE_GRANULARITY_RDISK2;//joe T0 is xfer_lba_num
				secid1=slba>>NS_SIZE_GRANULARITY_BITOP2;
				secid2=(slba2)>>NS_SIZE_GRANULARITY_BITOP2;//joe T0 is xfer_lba_num
				disp_apl_trace(LOG_DEBUG, 0x2530, "JOE: secid:%d  secid2:%d  slba:0x%x  slba2:0x%x", secid1,secid2, slba,slba2);
				if(secid1 != secid2){
					cross_flag_write=1;
					//cross_lbacnt=(secid2)*NS_SIZE_GRANULARITY_RDISK2 - slba;
					cross_lbacnt=((secid2)<<NS_SIZE_GRANULARITY_BITOP2) - slba;
					cross_du=(cross_lbacnt);
				}
				//slba_last = slba-(secid1)*NS_SIZE_GRANULARITY_RDISK2;
				//slba =slba_last+(ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid1])*NS_SIZE_GRANULARITY_RDISK2;
				slba_last = slba-((secid1)<<NS_SIZE_GRANULARITY_BITOP2);
				//slba =slba_last+((ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid1])<<NS_SIZE_GRANULARITY_BITOP2);
				sec_tran=(ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[secid1]);
				slba = slba_last +( (sec_tran) <<NS_SIZE_GRANULARITY_BITOP2);
				disp_apl_trace(LOG_DEBUG, 0xa0d5, "JOE: transfer slba:  H: 0x%x  L:0x%x   slba_last:0x%x", (slba >> 32), slba, slba_last);
			}

			lda =( (slba) >> (LDA_SIZE_SHIFT - host_sec_bitz));

			wde_parser->start=lda;
			sec_tran2=ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[secid2];
			if(cross_flag_write==1)
				//wde_parser->cross_lda=(ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid2])*NS_SIZE_GRANULARITY_RDISK2;
				//wde_parser->cross_lda=(ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid2])<<NS_SIZE_GRANULARITY_BITOP2;
				wde_parser->cross_lda=(sec_tran2<<NS_SIZE_GRANULARITY_BITOP2);
			#if 0//(TRIM_SUPPORT == ENABLE)
			if(cross_flag_write==1){
				if(IsCMDinTrimTable(wde_parser->start, wde_parser->start + cross_du - 1,1)){
					RegOrUnregTrimTable(wde_parser->start,cross_du,Unregister);
					UpdtTrimInfo(wde_parser->start, cross_du, Unregister);
				}
				if(IsCMDinTrimTable(wde_parser->cross_lda, wde_parser->cross_lda + cnt - cross_du - 1,1)){
					RegOrUnregTrimTable(wde_parser->cross_lda,cnt - cross_du,Unregister);
					UpdtTrimInfo(wde_parser->cross_lda, cnt - cross_du, Unregister);
				}
			}else{
				if(IsCMDinTrimTable(wde_parser->start, wde_parser->start + cnt - 1,1)){
					RegOrUnregTrimTable(wde_parser->start,cnt,Unregister);
					UpdtTrimInfo(wde_parser->start, cnt, Unregister);
				}
			}
			#endif
		}
		if(cross_flag_write==1){//joe add for write performance 20200917
			if(i<cross_du)
				lda=wde_parser->start+i;
			else
				lda=wde_parser->cross_lda+i-cross_du;
			disp_apl_trace(LOG_DEBUG, 0xa648, "JOE: transfer ldaa:  0x%x  bm_pl->pl.du_ofst:%d",lda,bm_pl->pl.du_ofst);
		}else{          //no cross case
			 lda=wde_parser->start+i;
			 disp_apl_trace(LOG_DEBUG, 0x4986, "JOE: transfer ldaa:  0x%x  bm_pl->pl.du_ofst:%d",lda,bm_pl->pl.du_ofst);
		}


		wde_parser->dtags[idx] = dtag;
		//wde_parser->ldas[idx] = wde_parser->start;
		wde_parser->ldas[idx] = lda; //joe add NS 20200813
		wde_parser->par[idx].all = 0;

		if (wde_parser->flags.b.fua)
			wde_parser->btags[idx] = FUA_BTAG_TAG|wde_parser->btag;
		else
			wde_parser->btags[idx] = 0;
			//wde_parser->start++;
		idx++;

		if (idx == WDE_NRM_INS_MAX)
		{
			ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
			wde_parser->btags, wde_parser->par, idx);
			idx = 0;
		}
		}
		wde_parser->ins = idx;
		return cnt;
	}
	#else
	{
	for (i = 0; i < cnt; i++)//for the case just one ns and fulldisk  of nrm_in
	{
		bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(addr[i]);
		dtag_t dtag = {.dtag = bm_pl->pl.dtag};

		if (bm_pl->pl.btag != wde_parser->btag || wde_parser->off != bm_pl->pl.du_ofst)
		{
			wde_parser->ins = idx;
			return i;
		}
		wde_parser->off++;
		dtag.b.type_ctrl = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
#ifdef LJ1_WUNC
		reset_pdu_bmp(dtag);
#endif
		wde_parser->dtags[idx] = dtag;
		wde_parser->ldas[idx] = wde_parser->start;
		wde_parser->par[idx].all = 0;
		disp_apl_trace(LOG_DEBUG, 0xdbcd, "JOE nrm: lda:  0x%x  bm_pl->pl.du_ofst:%d",wde_parser->start,bm_pl->pl.du_ofst);
		if (wde_parser->flags.b.fua)
			wde_parser->btags[idx] = FUA_BTAG_TAG|wde_parser->btag;
		else
			wde_parser->btags[idx] = 0;
		wde_parser->start++;
		idx++;

		if (idx == WDE_NRM_INS_MAX)
		{
			ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
								   wde_parser->btags, wde_parser->par, idx);
			idx = 0;
		}
	}

	wde_parser->ins = idx;
	return cnt;
	}
	#endif
}

fast_code static inline u32 wde_parser_first(wde_parser_t *wde_parser, bm_pl_t *bm_pl)
{
	btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_pl->pl.btag);
	u64 slba = bcmd_get_slba(bcmd);
	if (_wde_parser.ins)
	{
		ucache_nrm_par_data_in(_wde_parser.ldas, _wde_parser.dtags,
							   _wde_parser.btags, _wde_parser.par, _wde_parser.ins);
		_wde_parser.ins = 0;
	}

#if defined(FREE_DTAG_PRELOAD)
	volatile short *xfer = (volatile short *)&bcmd_ex->du_xfer_left;
	while (*xfer == 0)
	{

		if(plp_trigger)
		{
			evlog_printk(LOG_ERR,"plp parser break");
			wde_parser->state = WDE_PARSER_ERR;
			wde_parser->flags.all = 0;
			wde_parser->btag = 0;
			return 1;
		}

		if (gResetFlag & (BIT(cNvmeSubsystemReset) |BIT(cNvmeFlrPfReset)  |BIT(cNVMeLinkReqRstNot) |BIT(cNvmePCIeReset)) )
		{
			if (gPerstInCmd && (bcmd_ex->flags.b.bcmd_abort == 1))
			{
				disp_apl_trace(LOG_INFO, 0x9468, "[WDE] Abort WR:%d",bm_pl->pl.btag);
				wde_parser->state = WDE_PARSER_ERR;
				wde_parser->btag = bm_pl->pl.btag;
				return 1;
			}
			if(gPerstInCmd && (bcmd_ex->flags.b.wr_err == 1) && (gResetFlag & BIT(cNvmeFlrPfReset))) // 23/11/24 shengbin add for FLR hang
			{
				disp_apl_trace(LOG_INFO, 0x7663, "[WDE] wr_err: btag %d; op %d; cmd_id %d;",bm_pl->pl.btag, bcmd->dw0.b.cmd_type, bm_pl->pl.nvm_cmd_id);
				wde_parser->state = WDE_PARSER_ERR;
				wde_parser->btag = bm_pl->pl.btag;
				return 1;
			}
		}
		if(bcmd_ex->flags.b.wr_err == 1)
		{
			disp_apl_trace(LOG_INFO, 0xe518, "[WDE] wr_err: btag %d; op %d; cmd_id %d;",bm_pl->pl.btag, bcmd->dw0.b.cmd_type, bm_pl->pl.nvm_cmd_id);
			wde_parser->state = WDE_PARSER_ERR;
			wde_parser->btag = bm_pl->pl.btag;
			wde_parser->left = wde_parser->total = bcmd_ex->du_xfer_left;
			wde_parser->left--;
			return 1;
		}
		if(bcmd_ex->flags.b.bcmd_abort == 1)
		{
			disp_apl_trace(LOG_INFO, 0x03cb, "[WDE] bcmd_abort: btag %d; op %d; cmd_id %d;",bm_pl->pl.btag, bcmd->dw0.b.cmd_type, bm_pl->pl.nvm_cmd_id);
		}
		btn_rw_cmd_in();
		cpu_msg_isr();
        extern void bm_handle_rd_err(void);
		extern void bm_isr_com_free(void);
		bm_handle_rd_err();
		bm_isr_com_free();
	}
#endif
    //disp_apl_trace(LOG_INFO, 0, "fua cmd in:%x btag:%x, left:%d",bcmd, bm_pl->pl.btag, bcmd_ex->du_xfer_left);
	wde_parser->state = WDE_PARSER_BUSY;
	wde_parser->flags.all = 0;

	if(esr_err_fua_flag)
		wde_parser->flags.b.fua = 1;
	else
		wde_parser->flags.b.fua = is_fua_bcmd(bcmd);

	wde_parser->flags.b.fw_cmd = is_wzero_bcmd(bcmd) && bcmd->dw0.b.nvm_cmd_id < NUM_FW_NVM_CMD;
	//wde_parser->start = LBA_TO_LDA(bcmd_get_slba(bcmd));//joe 202008  we don't use this start value
	wde_parser->start = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)); //joe add 20200820
	wde_parser->left =
		wde_parser->total = bcmd_ex->du_xfer_left;
    cache_handle_dtag_cnt += bcmd_ex->du_xfer_left;
    if (wde_parser->flags.b.fua) {
        otf_fua_cmd_cnt++;
    }
	wde_parser->bcmd = bcmd;
	wde_parser->bcmd_ex = bcmd_ex;
    wde_parser->btag = bm_pl->pl.btag;
	wde_parser->off = 0;

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	//if (ra_range_chk(wde_parser->start, wde_parser->total))
	{
		ra_disable_time(0);
	}
	#endif

	if (bm_pl->pl.du_ofst != 0)
	{
		wde_parser->state = WDE_PARSER_ERR;
		wde_parser->left--;
		wde_discard_single(wde_parser, bm_pl);
		disp_apl_trace(LOG_ERR, 0xb96b, "btag %d drop %d off %d", wde_parser->btag,
					   wde_parser->total - wde_parser->left, bm_pl->pl.du_ofst);
		return 1;
	}

	if (wde_parser->flags.b.fw_cmd)
	{
		if (wde_parser->rls_cmd_cnt)
		{
			btn_cmd_rels_push_bulk(wde_parser->rls_bcmds, wde_parser->rls_cmd_cnt);
			wde_parser->rls_cmd_cnt = 0;
		}

		if (wde_parser->ins)
		{
			ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
								   wde_parser->btags, wde_parser->par, wde_parser->ins);
			wde_parser->ins = 0;
		}
	}

	if (wde_parser->flags.b.fw_cmd)
	{
		sys_assert(is_par_wde(bm_pl));
		/* the fw command must be partial, but we still use single in to handle it */
		rdisk_single_in(&_wde_parser, bm_pl, NULL);
		wde_parser_done(&_wde_parser, 1);
		return 1;
	}

	if (is_par_wde(bm_pl))
	{
		rdisk_par_in(&_wde_parser, bm_pl);
		wde_parser_done(&_wde_parser, 1);
		return 1;
	}
	else if (wde_parser->left == 1)
	{
		rdisk_single_in(&_wde_parser, bm_pl, NULL);
		wde_parser_done(&_wde_parser, 1);
		return 1;
	}

	return 0;
}

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL
fast_code void rdisk_wd_semi_to_non_semi(u32 *addr, u32 cnt)
{
	u32 i;
	dtag_t dtag;
	dtag_t smdtag;
	bm_pl_t *bm_pl;

	/*
       * In Rainier A0, RAID Engine xor completion report can not work because of
       * xor done count update error issue, then we workaround this with pdone.
       * When enable pdone in bm, semi can not support, so we recycle sram dtag
       * and only put ddr dtag in bm_pl.
	*/

	for (i = 0; i < cnt; i++)
	{
		bm_pl = (bm_pl_t *)dma_to_btcm(addr[i]);
		if (bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK)
		{
			smdtag.dtag = bm_pl->pl.dtag;
			dtag.dtag = smdtag2sdtag(smdtag.dtag);
			bm_free_semi_write_load(&dtag, 1, 0);

			dtag.dtag = smdtag2ddtag(smdtag.dtag);
			bm_pl->pl.dtag = dtag.dtag;
			bm_pl->pl.type_ctrl &= ~BTN_SEMI_MODE_MASK;
		}
	}
}
#endif

UNUSED fast_code static void rdisk_wd_updt_nrm_par(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32 *)payload;
	req_statistics.wr_du_ttl += count;

#if 0 //SEMI_WRITE_ENABLE && RAID_SUPPORT && defined(XOR_CMPL_BY_PDONE)
	rdisk_wd_semi_to_non_semi(addr, count);
#endif
#ifdef POWER_APST
	apst_transfer_to_last_ps();
#endif
	while (count)
	{
		if (_wde_parser.state == WDE_PARSER_ERR)
		{
			bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(*addr);

			if (bm_pl->pl.btag == _wde_parser.btag)
			{
				wde_discard_single(&_wde_parser, bm_pl);
				if (gResetFlag & (BIT(cNvmeSubsystemReset) |BIT(cNvmeFlrPfReset) |BIT(cNVMeLinkReqRstNot) |BIT(cNvmePCIeReset)) )
				{
					if (gPerstInCmd && (gRstFsm == 1)) // 1 = RST_STATE_CACHE
					{
						disp_apl_trace(LOG_DEBUG, 0xab04, "btag %d drop %d off %d", _wde_parser.btag,
						_wde_parser.total - _wde_parser.left, bm_pl->pl.du_ofst);
					}
				}
				else
				{
					_wde_parser.left--;
					//disp_apl_trace(LOG_ERR, 0, "btag %d drop %d off %d", _wde_parser.btag,
					//_wde_parser.total - _wde_parser.left, bm_pl->pl.du_ofst);
				}

				addr++;
				count--;
				continue;
			}
			else
			{
				//disp_apl_trace(LOG_INFO, 0, "btag drop done", _wde_parser.btag);
				_wde_parser.state = WDE_PARSER_IDLE;
			}
		}

		if (_wde_parser.state == WDE_PARSER_IDLE)
		{
			bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(*addr);
			u32 ret = wde_parser_first(&_wde_parser, bm_pl);

			if (ret == 1)
			{
				addr++;
				count--;
				continue;
			}
		}

		u32 c = min(count, _wde_parser.left);
		u32 nrm_c = c;
		bm_pl_t *last = NULL;
		u32 handled = 0;

		if (c == _wde_parser.left)
		{
			last = (bm_pl_t *)dma_to_btcm(addr[c - 1]);

			if (is_par_wde(last))
				nrm_c -= 1;
			else
				last = NULL;
		}

		if (nrm_c)
		{
			handled += rdisk_nrm_in(&_wde_parser, addr, nrm_c);

			if (handled != nrm_c) ///< broken
				last = NULL;
		}

		if (last)
		{
			handled += rdisk_par_in(&_wde_parser, last);
		}

		if (handled == c)
		{
			wde_parser_done(&_wde_parser, c);
		}
		else
		{
			_wde_parser.left -= handled;
			disp_apl_trace(LOG_ERR, 0x5e13, "bcmd %d broken at %d/%d(%d)",
						   _wde_parser.btag, _wde_parser.left, _wde_parser.total, handled);
			// drop current btag
			wde_parser_drop(&_wde_parser);
			_wde_parser.state = WDE_PARSER_ERR;
			c = handled;
		}

		addr += c;
		count -= c;
	}

	if (_wde_parser.ins &&(_wde_parser.ins >= 5 || _wde_parser.state == WDE_PARSER_IDLE))
	{
		ucache_nrm_par_data_in(_wde_parser.ldas, _wde_parser.dtags,
							   _wde_parser.btags, _wde_parser.par, _wde_parser.ins);
		_wde_parser.ins = 0;
	}

	if (_wde_parser.rls_cmd_cnt)
	{
		btn_cmd_rels_push_bulk(_wde_parser.rls_bcmds, _wde_parser.rls_cmd_cnt);
		_wde_parser.rls_cmd_cnt = 0;
	}
}

#else
fast_code void rdisk_fua_done(u16 btag, dtag_t dtag)
{
	if (btag & FUA_BTAG_TAG)
	{
		btn_cmd_t *bcmd = btag2bcmd(btag&BTAG_MASK);
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag&BTAG_MASK);


		bool fua = is_fua_bcmd(bcmd);
		if(!esr_err_fua_flag)
			sys_assert(fua);
		sys_assert(bcmd_ex->du_xfer_left);

		if ((--bcmd_ex->du_xfer_left == 0) && (bcmd_ex->flags.b.wr_err == 0))
		{
            //disp_apl_trace(LOG_INFO, 0, "fua cmd done:%x btag:%x, left:%d",bcmd, btag, bcmd_ex->du_xfer_left);
            otf_fua_cmd_cnt--;
			btn_cmd_rels_push_bulk(&bcmd, 1);
		}
	}
    #ifdef LJ1_WUNC
	else
	{
        if(dtag.dtag != EVTAG_ID){
            _reset_pdu_bmp(dtag);
        }
        if(btag & BTAG_MASK){
    		req_t *req = list_first_entry(&_req_pending, req_t, entry2);

    		sys_assert(req->opcode == REQ_T_WUNC);
    		sys_assert(req->op_fields.wucc.left);
    		req->op_fields.wucc.left--;
    		if (req->op_fields.wucc.left == 0)
    		{
                wunc_ua[0] = INV_U32;
                wunc_bmp[0] =  0;
                wunc_ua[1] = INV_U32;
                wunc_bmp[1] =  0;
                otf_fua_cmd_cnt--;
                if (req->lba.srage.nlb != 0) {
                    //rdisk_wunc(req);
                    evt_set_cs(evt_rdisk_wunc_part, 0, 0, CS_TASK);
                }
                else {
                    disp_apl_trace(LOG_INFO, 0x0fa3, "wunc done");
                    req->completion(req);
                    list_del(&req->entry2);

                    flush_to_nand(EVT_HOST_WUNC);

                    if (rdisk_fe_flags.b.io_fetch_stop)
                    {
                        nvmet_io_fetch_ctrl(false); // enable io fetch
                        rdisk_fe_flags.b.io_fetch_stop = 0;
                    }
                    if(!list_empty(&_req_pending))
                        evt_set_cs(evt_wait_wcmd_idle, 0, 0, CS_TASK);
                }
    		}
    	}
	}
    #endif
}

fast_code u32 wde_discard_single(wde_parser_t *wde_parser, bm_pl_t *bm_pl)
{
	dtag_t dtag;

	dtag.dtag = bm_pl->pl.dtag;
	dtag.b.type_ctrl = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
#ifdef LJ1_WUNC
	reset_pdu_bmp(dtag);
#endif
	dtag.b.type_ctrl = 0;
	if (bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK)
		smdtag_recycle(dtag);
	else
		rdisk_dref_dec(&dtag.dtag, 1);

	return 1;
}

fast_code static void wde_parser_done(wde_parser_t *wde_parser, u32 cnt)
{
	sys_assert(wde_parser->left >= cnt);
	wde_parser->left -= cnt;

	if (wde_parser->left == 0)
	{
		sys_assert(wde_parser->bcmd_ex->flags.b.wr_err == 0);
		if (wde_parser->flags.b.fw_cmd)
		{
			pat_gen_req_done(wde_parser->btag, wde_parser->dtags, wde_parser->ins);
			wde_parser->ins = 0;
		}
		else if (wde_parser->flags.b.fua == 0)
		{
			wde_parser->rls_bcmds[wde_parser->rls_cmd_cnt] = wde_parser->bcmd;
			wde_parser->rls_cmd_cnt++;
			if (wde_parser->rls_cmd_cnt >= WCMD_RLS_MAX)
			{
				btn_cmd_rels_push_bulk(wde_parser->rls_bcmds, wde_parser->rls_cmd_cnt);
				wde_parser->rls_cmd_cnt = 0;
			}
		}

		wde_parser->state = WDE_PARSER_IDLE;
	}
}

fast_code static inline void rdisk_single_in(wde_parser_t *wde_parser, bm_pl_t *bm_pl, r_par_t *par)
{
	dtag_t dtag = {.dtag = bm_pl->pl.dtag};
	dtag.b.type_ctrl = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
#ifdef LJ1_WUNC
	reset_pdu_bmp(dtag);
#endif

	wde_parser->dtags[wde_parser->ins] = dtag;
	wde_parser->ldas[wde_parser->ins] = wde_parser->start;
	wde_parser->par[wde_parser->ins].all = 0;

	if (par && par->all)
		wde_parser->par[wde_parser->ins].all = par->all;

	if (wde_parser->flags.b.fua)
		wde_parser->btags[wde_parser->ins] = FUA_BTAG_TAG|wde_parser->btag;
	else
		wde_parser->btags[wde_parser->ins] = 0;

	wde_parser->ins++;
	wde_parser->off++;
	wde_parser->start++;

	if (wde_parser->ins >= WDE_NRM_INS_MAX)
	{
		ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
							   wde_parser->btags, wde_parser->par, wde_parser->ins);
		wde_parser->ins = 0;
	}
}

fast_code NOINLINE void wde_parser_drop(wde_parser_t *wde_parser)
{
	int drop_cnt = wde_parser->total - wde_parser->left;
	int dropped = 0;

	while (wde_parser->ins && drop_cnt)
	{
		bm_pl_t bm_pl = {.all = 0};
		dtag_t dtag;

		wde_parser->ins--;
		dtag = wde_parser->dtags[wde_parser->ins];
		bm_pl.pl.type_ctrl = dtag.b.type_ctrl;
		bm_pl.pl.dtag = dtag.dtag & 0xFFFFFF;
		wde_discard_single(wde_parser, &bm_pl);
		drop_cnt--;

		dropped++;
	}

	if (dropped)
		disp_apl_trace(LOG_ERR, 0xd6a8, "bcmd %d dropped %d", wde_parser->btag, dropped);

	wde_parser->state = WDE_PARSER_IDLE;
}


fast_code NOINLINE void wde_parser_reset(void)
{
	if (_wde_parser.state == WDE_PARSER_BUSY)
		wde_parser_drop(&_wde_parser);

	if (_wde_parser.rls_cmd_cnt) {
		disp_apl_trace(LOG_INFO, 0x2ed0, "wde bcmd rst %d", _wde_parser.rls_cmd_cnt);
		_wde_parser.rls_cmd_cnt = 0;
	}
}

// fast_code bool rdisk_check_wde_idle(void)
// {
// 	if (_wde_parser.state == WDE_PARSER_IDLE)
// 		return true;
//
// 	_wde_parser.flags.b.wait_idle = true;
// 	return false;
// }

fast_code static u32 rdisk_par_in(wde_parser_t *wde_parser, bm_pl_t *bm_pl)
{
	btn_cmd_t *bcmd = wde_parser->bcmd;
	u64 slba = bcmd_get_slba(bcmd);
	int du_ofst = bm_pl->pl.du_ofst;
	int nlba;
	int ofst;

	if (bm_pl->pl.btag != wde_parser->btag || bm_pl->pl.du_ofst != wde_parser->off)
		return 0;

	if (du_ofst == 0)
	{
		ofst = LBA_OFST_LDA(slba);
		nlba = NR_LBA_PER_LDA - ofst;
		nlba = min(nlba, bcmd->dw3.b.xfer_lba_num);
	}
	else
	{
		u64 elba = slba + bcmd->dw3.b.xfer_lba_num;

		nlba = elba & NR_LBA_PER_LDA_MASK;
		ofst = 0;
	}

	r_par_t par = {.ofst = ofst, .nlba = nlba};
	rdisk_single_in(&_wde_parser, bm_pl, &par);

	return 1;
}

static inline u32 rdisk_nrm_in(wde_parser_t *wde_parser, u32 *addr, u32 cnt)
{
	int i = 0;
	int idx = wde_parser->ins;

	for (i = 0; i < cnt; i++)
	{
		bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(addr[i]);
		dtag_t dtag = {.dtag = bm_pl->pl.dtag};

		if (bm_pl->pl.btag != wde_parser->btag || wde_parser->off != bm_pl->pl.du_ofst)
		{
			wde_parser->ins = idx;
			return i;
		}
		wde_parser->off++;
		dtag.b.type_ctrl = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
#ifdef LJ1_WUNC
		reset_pdu_bmp(dtag);
#endif
		wde_parser->dtags[idx] = dtag;
		wde_parser->ldas[idx] = wde_parser->start;
		wde_parser->par[idx].all = 0;

		if (wde_parser->flags.b.fua)
			wde_parser->btags[idx] = FUA_BTAG_TAG|wde_parser->btag;
		else
			wde_parser->btags[idx] = 0;
		wde_parser->start++;
		idx++;

		if (idx == WDE_NRM_INS_MAX)
		{
			ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
								   wde_parser->btags, wde_parser->par, idx);
			idx = 0;
		}
	}

	wde_parser->ins = idx;
	return cnt;
}

fast_code static inline u32 wde_parser_first(wde_parser_t *wde_parser, bm_pl_t *bm_pl)
{
	btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_pl->pl.btag);

	if (_wde_parser.ins)
	{
		ucache_nrm_par_data_in(_wde_parser.ldas, _wde_parser.dtags,
							   _wde_parser.btags, _wde_parser.par, _wde_parser.ins);
		_wde_parser.ins = 0;
	}

#if defined(FREE_DTAG_PRELOAD)
	volatile short *xfer = (volatile short *)&bcmd_ex->du_xfer_left;
	while (*xfer == 0)
	{
		btn_rw_cmd_in();
		cpu_msg_isr();
	}
#endif

	wde_parser->state = WDE_PARSER_BUSY;
	wde_parser->flags.all = 0;
	wde_parser->flags.b.fua = is_fua_bcmd(bcmd);
	wde_parser->flags.b.fw_cmd = is_wzero_bcmd(bcmd) && bcmd->dw0.b.nvm_cmd_id < NUM_FW_NVM_CMD;
	//wde_parser->start = LBA_TO_LDA(bcmd_get_slba(bcmd));
	wde_parser->start = ((bcmd_get_slba(bcmd)) >> (LDA_SIZE_SHIFT - host_sec_bitz));
	wde_parser->left =
		wde_parser->total = bcmd_ex->du_xfer_left;
    cache_handle_dtag_cnt += bcmd_ex->du_xfer_left;
    if (wde_parser->flags.b.fua) {
        otf_fua_cmd_cnt++;
    }
	wde_parser->bcmd = bcmd;
	wde_parser->bcmd_ex = bcmd_ex;
	wde_parser->btag = bm_pl->pl.btag;
	wde_parser->off = 0;
    //need_force_flush_timer = get_tsc_lo();

	if (bm_pl->pl.du_ofst != 0)
	{
		wde_parser->state = WDE_PARSER_ERR;
		wde_parser->left--;
		wde_discard_single(wde_parser, bm_pl);
		disp_apl_trace(LOG_ERR, 0x6490, "btag %d drop %d off %d", wde_parser->btag,
					   wde_parser->total - wde_parser->left, bm_pl->pl.du_ofst);
		return 1;
	}
    #if 0// (TRIM_SUPPORT == ENABLE)
    if(IsCMDinTrimTable(wde_parser->start, wde_parser->start + wde_parser->total - 1,1)){
        RegOrUnregTrimTable(wde_parser->start,wde_parser->total,Unregister);
        UpdtTrimInfo(wde_parser->start, wde_parser->total, Unregister);
    }
    #endif
	if (wde_parser->flags.b.fw_cmd)
	{
		if (wde_parser->rls_cmd_cnt)
		{
			btn_cmd_rels_push_bulk(wde_parser->rls_bcmds, wde_parser->rls_cmd_cnt);
			wde_parser->rls_cmd_cnt = 0;
		}

		if (wde_parser->ins)
		{
			ucache_nrm_par_data_in(wde_parser->ldas, wde_parser->dtags,
								   wde_parser->btags, wde_parser->par, wde_parser->ins);
			wde_parser->ins = 0;
		}
	}

	if (wde_parser->flags.b.fw_cmd)
	{
		sys_assert(is_par_wde(bm_pl));
		/* the fw command must be partial, but we still use single in to handle it */
		rdisk_single_in(&_wde_parser, bm_pl, NULL);
		wde_parser_done(&_wde_parser, 1);
		return 1;
	}

	if (is_par_wde(bm_pl))
	{
		rdisk_par_in(&_wde_parser, bm_pl);
		wde_parser_done(&_wde_parser, 1);
		return 1;
	}
	else if (wde_parser->left == 1)
	{
		rdisk_single_in(&_wde_parser, bm_pl, NULL);
		wde_parser_done(&_wde_parser, 1);
		return 1;
	}

	return 0;
}

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL
fast_code void rdisk_wd_semi_to_non_semi(u32 *addr, u32 cnt)
{
	u32 i;
	dtag_t dtag;
	dtag_t smdtag;
	bm_pl_t *bm_pl;

	/*
       * In Rainier A0, RAID Engine xor completion report can not work because of
       * xor done count update error issue, then we workaround this with pdone.
       * When enable pdone in bm, semi can not support, so we recycle sram dtag
       * and only put ddr dtag in bm_pl.
	*/

	for (i = 0; i < cnt; i++)
	{
		bm_pl = (bm_pl_t *)dma_to_btcm(addr[i]);
		if (bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK)
		{
			smdtag.dtag = bm_pl->pl.dtag;
			dtag.dtag = smdtag2sdtag(smdtag.dtag);
			bm_free_semi_write_load(&dtag, 1, 0);

			dtag.dtag = smdtag2ddtag(smdtag.dtag);
			bm_pl->pl.dtag = dtag.dtag;
			bm_pl->pl.type_ctrl &= ~BTN_SEMI_MODE_MASK;
		}
	}
}
#endif

UNUSED fast_code static void rdisk_wd_updt_nrm_par(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32 *)payload;
	req_statistics.wr_du_ttl += count;

#if 0 //SEMI_WRITE_ENABLE && RAID_SUPPORT && defined(XOR_CMPL_BY_PDONE)
	rdisk_wd_semi_to_non_semi(addr, count);
#endif
#ifdef POWER_APST
	apst_transfer_to_last_ps();
#endif
	while (count)
	{
		if (_wde_parser.state == WDE_PARSER_ERR)
		{
			bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(*addr);

			if (bm_pl->pl.btag == _wde_parser.btag)
			{
				wde_discard_single(&_wde_parser, bm_pl);
				_wde_parser.left--;
				disp_apl_trace(LOG_ERR, 0xeb68, "btag %d drop %d off %d", _wde_parser.btag,
							   _wde_parser.total - _wde_parser.left, bm_pl->pl.du_ofst);
				addr++;
				count--;
				continue;
			}
			else
			{
				disp_apl_trace(LOG_INFO, 0x6620, "btag drop done", _wde_parser.btag);
				_wde_parser.state = WDE_PARSER_IDLE;
			}
		}

		if (_wde_parser.state == WDE_PARSER_IDLE)
		{
			bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(*addr);
			u32 ret = wde_parser_first(&_wde_parser, bm_pl);

			if (ret == 1)
			{
				addr++;
				count--;
				continue;
			}
		}

		u32 c = min(count, _wde_parser.left);
		u32 nrm_c = c;
		bm_pl_t *last = NULL;
		u32 handled = 0;

		if (c == _wde_parser.left)
		{
			last = (bm_pl_t *)dma_to_btcm(addr[c - 1]);

			if (is_par_wde(last))
				nrm_c -= 1;
			else
				last = NULL;
		}

		if (nrm_c)
		{
			handled += rdisk_nrm_in(&_wde_parser, addr, nrm_c);

			if (handled != nrm_c) ///< broken
				last = NULL;
		}

		if (last)
		{
			handled += rdisk_par_in(&_wde_parser, last);
		}

		if (handled == c)
		{
			wde_parser_done(&_wde_parser, c);
		}
		else
		{
			_wde_parser.left -= handled;
			disp_apl_trace(LOG_ERR, 0x7524, "bcmd %d broken at %d/%d(%d)",
						   _wde_parser.btag, _wde_parser.left, _wde_parser.total, handled);
			// drop current btag
			wde_parser_drop(&_wde_parser);
			_wde_parser.state = WDE_PARSER_ERR;
			c = handled;
		}

		addr += c;
		count -= c;
	}

	if (_wde_parser.ins)
	{
		ucache_nrm_par_data_in(_wde_parser.ldas, _wde_parser.dtags,
							   _wde_parser.btags, _wde_parser.par, _wde_parser.ins);
		_wde_parser.ins = 0;
	}

	if (_wde_parser.rls_cmd_cnt)
	{
		btn_cmd_rels_push_bulk(_wde_parser.rls_bcmds, _wde_parser.rls_cmd_cnt);
		_wde_parser.rls_cmd_cnt = 0;
	}
}
#endif
fast_code static void rdisk_wd_updt_pdone(u32 param, u32 payload, u32 count)
{
#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL
	u32 i;
	u32 stripe_id;
	bm_pl_t *bm_pl;
	u32 *addr = (u32 *)payload;

	for (i = 0; i < count; i++)
	{
		bm_pl = (bm_pl_t *)dma_to_btcm(addr[i]);

		//return semi dtag when pdone
		if ((bm_pl->pl.du_ofst & DU_SEMI_MASK) == BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM)
		{
			dtag_t sdtag = {.dtag = bm_pl->pl.dtag};
			bm_free_semi_write_load(&sdtag, 1, 0);
		}

		stripe_id = bm_pl->pl.du_ofst >> DU_SEMI_OFST;
		sys_assert(stripe_id < MAX_STRIPE_ID);
		sys_assert(stripe_pdone_cnt[stripe_id]);

		stripe_pdone_cnt[stripe_id]--;
		if (stripe_pdone_cnt[stripe_id] == 0)
			cpu_msg_issue(CPU_BE - 1, CPU_MSG_STRIPE_XOR_DONE, 0, stripe_id);
	}
#endif
}

/*!
 * @brief common free queue updated handler in pre-assigned mode
 *
 * In pre-assigned mode, only read done dtags will be recycled to common free
 * queue, and we just dropped them, because they should be upper 1MB memory,
 * they are not in free dtag pool.
 *
 * @param[in] param	not used
 * @param[in] payload	should be BM payload list
 * @param[in] count	length of BM payload list
 *
 * @return		None
 */
fast_code static void rdisk_com_free_updt_pasg(u32 param, u32 payload, u32 count)
{
	u32 *dtag = (u32 *)payload;
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	bool ra_in = false;
	#endif
	int i = 0;
	for (i = 0; i < count; i++)
	{
#if 0//((TCG_WRITE_DATA_ENTRY_ABORT) && defined(TCG_NAND_BACKUP))
		if(otf_tcg_mbr_dtag)
		{
			u32 dtag_buffer_tcg = mem2ddtag(tcgTmpBuf);
			if((dtag[i] & DTAG_IN_DDR_MASK) && (dtag[i] >= dtag_buffer_tcg))
			{
				//disp_apl_trace(LOG_ERR, 0, "[TCG] otf_tcg_mbr_dtag: %d, Put dtag: 0x%x", otf_tcg_mbr_dtag, dtag[i]);
				--otf_tcg_mbr_dtag;
				if(otf_tcg_mbr_dtag == 0)
				{
					//mbr_rd_cid = 0xFFFF;
					disp_apl_trace(LOG_INFO, 0x08f1, "[TCG] MBR reset cid: %d", mbr_rd_cid);
				}
				continue;
			}
		}
#endif
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		if (!is_ra_dtag(dtag[i]))
		{
			rdisk_dref_dec(&dtag[i], 1);
		}
		else
		{
			ra_in = true;
		}
		#else
		rdisk_dref_dec(&dtag[i], 1);
		#endif
	}

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	if (ra_in)
	{
		ra_data_out_cmpl();
	}
	#endif
}

ddr_code static void rdisk_format_done(ftl_core_ctx_t *ctx)
{
	req_t *req = (req_t *)ctx->caller;

	disp_apl_trace(LOG_INFO, 0xdd44, "ns %d format done", req->nsid);

	list_del(&req->entry2);
	req->completion(req);

	sys_free(FAST_DATA, ctx);

	if (rdisk_fe_flags.b.io_fetch_stop)
	{
		nvmet_io_fetch_ctrl(false); // enable io fetch
		rdisk_fe_flags.b.io_fetch_stop = 0;
	}

	req_resume();
}

ddr_code static void rdisk_format_flush_done(ftl_core_ctx_t *ctx)
{
	ftl_flush_data_t *fctx = (ftl_flush_data_t *)ctx;
	req_t *caller = (req_t *)ctx->caller;
	ftl_format_t *format;

	fctx->nsid = 0;
	format = sys_malloc(FAST_DATA, sizeof(ftl_format_t));
	sys_assert(format);

	format->ctx.caller = (void *)caller;
	format->ctx.cmpl = rdisk_format_done;
	format->flags.all = 0;
	if (caller->op_fields.format.meta_enable)
		format->flags.b.host_meta = true;

	if (caller->op_fields.format.erase_type == 3) //preformat erase
	{
		format->flags.b.preformat_erase = 1;
	}
	format->nsid = caller->nsid;

	if (caller->op_fields.format.erase_type == 3) //preformat erase
	{
		ftl_core_format(format);
	}
	else // crypto erase
	{
		format->ctx.cmpl(&format->ctx);
	}
}

/*!
 * @brief rdisk format function
 *
 * Format the entire disk. If pi_enable is set, reset the lba mapping also.
 *
 * @param req			nvme request
 *
 * @return			not used
 */
ddr_code void rdisk_format(req_t *req)
{
	ftl_flush_data_t *fctx = &_flush_ctx;
	sys_assert(fctx->nsid == 0); /// nsid must be zero

	fctx->ctx.caller = req;
	fctx->nsid = req->nsid;
	fctx->flags.all = 0;
	fctx->flags.b.format = 1;
	//fctx->flags.b.shutdown = 1;
	fctx->ctx.cmpl = rdisk_format_flush_done;
    #if(BG_TRIM == ENABLE)
    SetBgTrimAbort();
    #endif
	ucache_flush(fctx);
#if ((defined(USE_CRYPTO_HW)) && (_TCG_ == TCG_NONE))
	///Andy change new key
	//mode=1, NSID=1, change key=1, crypt entry =1
	crypto_change_mode_range(3, 1, 1, 1);
#endif
}

/*!
 * @brief ramdisk trim function
 *
 * @param[in] req	trim request
 *
 * @return		None
 *
 */

#ifdef ERRHANDLE_ECCT
ddr_code void rdisk_ECCT_op(u64 slba, u64 nlb ,u8 op)
{
            //if(pECC_table->ecc_table_cnt)     //Search same idx of lda in ecct
    if(ecct_cnt)
    {
        //stECCT_ipc_t ecct_info;
        rc_ecct_info[rc_ecct_cnt].lba       = slba;
        rc_ecct_info[rc_ecct_cnt].source    = ECC_REG_VU;
        rc_ecct_info[rc_ecct_cnt].total_len = nlb;
        rc_ecct_info[rc_ecct_cnt].type      = op;

        disp_apl_trace(LOG_INFO, 0x3ba9, "[Trim] rdisk_trim unregister ECCT");
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
}
#endif
#if(BG_TRIM == ENABLE)
extern unalign_LDA TrimUnalignLDA;
extern volatile trim_dtag_free_t bg_trim_free_dtag;
#endif
extern bool Full_TRIM_IPC_SEND;

static slow_code void rdisk_trim(req_t *req)
{
	dtag_t dtag;
#if(TRIM_SUPPORT == ENABLE)
	if (req->op_fields.trim.att & BIT(2)) //Bit(2) means deallocate
	{
    	u16 list_cnt;
    	u16 i;
    	lda_t slda = 0;
    	lda_t eldanext = 0;
    	u32 count = 0;
    	u8 ns_id = req->nsid; //joe add 20200914
    // 	disp_apl_trace(LOG_ERR, 0, "[Trim] req:0x%x,stime:0x%x", req,jiffies);
    	struct nvme_dsm_range *dsmr;
    	u64 slba = 0;
    	u64 nlb = 0;
    	u64 Nextlba = 0;
    	u32 RecordRangeIdx = 0;
    	list_cnt = req->op_fields.trim.nr;
    		dsmr = (struct nvme_dsm_range *)req->op_fields.trim.dsmr;
    	slba = dsmr[0].starting_lba;
        while(!CBF_EMPTY(&bg_trim_free_dtag)){
            dtag_t dtag = bg_trim_free_dtag.idx[bg_trim_free_dtag.rptr];
            put_trim_dtag(dtag);
            bg_trim_free_dtag.rptr = (bg_trim_free_dtag.rptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);
    	}
    	dtag_t dtag = get_trim_dtag(false);
    	sys_assert(dtag.dtag!=INV_U32);
    	Host_Trim_Data *trim_data = (Host_Trim_Data *)ddtag2mem(dtag.b.dtag);
        trim_data->all = 0;
    	trim_data->Validcnt = 0;
    	trim_data->Validtag = 0;
        trim_data->valid_bmp = 0;
    	trim_data->nsid = req->nsid; //joe add 20200914
    	u32 lbanum;
        if(host_sec_bitz == 9){
    	    lbanum=8;
        }
    	else{
    		lbanum=1;
    	}
    	for (i = 0; i < list_cnt - 1; i++)
    	{
    		Nextlba = dsmr[i + 1].starting_lba;
    		nlb = dsmr[i].length + nlb;

    		if (slba + nlb == Nextlba)
    		{
    			continue;
    		}
    		else
    		{
                #ifdef ERRHANDLE_ECCT
                if(ecct_cnt)
                {
                    rdisk_ECCT_op(slba, nlb, VSC_ECC_unreg);
                }
                #endif
                // #if NS_MANAGE
                //     UpdtTrimInfo_with_NS(slba, nlb,req->nsid);
                // #else
                //     UpdtTrimInfo(slba, nlb, Register);
                // #endif

                #if (BG_TRIM == ENABLE)
                if(host_sec_bitz == 9){
                    if((TrimUnalignLDA.LBA != INV_U64)&&(TrimUnalignLDA.LBA == slba)){
                        slba = slba & ~(7);
                        nlb += TrimUnalignLDA.LBA&7;
                    }
                    u64 tail = (slba+nlb)&7;
                    if(tail&&(nlb >= tail)){
                        TrimUnalignLDA.LBA = slba+nlb;
                    }else{
                        TrimUnalignLDA.LBA = INV_U64;
                    }
                }
                #endif
    			if (nlb >> (LDA_SIZE_SHIFT - host_sec_bitz))
    			{
    				RecordRangeIdx = trim_data->Validcnt;
    				slda = (slba + lbanum - 1) >> (LDA_SIZE_SHIFT - host_sec_bitz);
    				eldanext = (nlb + slba) >> (LDA_SIZE_SHIFT - host_sec_bitz);
    				count = eldanext - slda;
#if 1 //(TRIM_DEBUG ==ENABLE)
    				disp_apl_trace(LOG_INFO, 0xeeed, "[Trim] NSID|%d  LBA|0x%x-%x NLB|0x%x-%x", ns_id, (u32)(slba >> 32), (u32)slba, (u32)(nlb >> 32), (u32)nlb);
#endif
    				trim_data->Ranges[RecordRangeIdx].sLDA = slda;
    				trim_data->Ranges[RecordRangeIdx].Length = count;
                    if(count)
    				    trim_data->Validcnt++;
    			}
    			slba = Nextlba;
    			nlb = 0;
    		}
    	}

        #ifdef ERRHANDLE_ECCT
        if(ecct_cnt)
        {
            rdisk_ECCT_op(slba, nlb, VSC_ECC_unreg);
        }
        #endif


    	nlb = dsmr[list_cnt - 1].length + nlb;
        // #if NS_MANAGE
        //     UpdtTrimInfo_with_NS(slba, nlb,req->nsid);
        // #else
        //     UpdtTrimInfo(slba, nlb, Register);
        // #endif



        #if(BG_TRIM == ENABLE)
        if(host_sec_bitz == 9){
            if((TrimUnalignLDA.LBA != INV_U64)&&(TrimUnalignLDA.LBA == slba)){
                slba = slba & ~(7);
                nlb += TrimUnalignLDA.LBA&7;
            }
            u64 tail = (slba+nlb)&7;
            if(tail&&(nlb >= tail)){
                TrimUnalignLDA.LBA = slba+nlb;
            }else{
                TrimUnalignLDA.LBA = INV_U64;
            }
        }
        #endif
    	if (nlb >> (LDA_SIZE_SHIFT - host_sec_bitz))
    	{
    		RecordRangeIdx = trim_data->Validcnt;
    		slda = (slba + lbanum - 1) >> (LDA_SIZE_SHIFT - host_sec_bitz);
    		eldanext = (nlb + slba) >> (LDA_SIZE_SHIFT - host_sec_bitz);
    		count = eldanext - slda;
#if 1 //(TRIM_DEBUG ==ENABLE)
    		disp_apl_trace(LOG_INFO, 0x0ac8, "[Trim] NSID|%d  LBA|0x%x-%x NLB|0x%x-%x", ns_id, (u32)(slba >> 32), (u32)slba, (u32)(nlb >> 32), (u32)nlb);
#endif
    		trim_data->Ranges[RecordRangeIdx].sLDA = slda;
    		trim_data->Ranges[RecordRangeIdx].Length = count;
            if(count)
    		    trim_data->Validcnt++;
    		slba = Nextlba;
    		nlb = 0;
    	}
        if(trim_data->Validcnt){
#if (FULL_TRIM == ENABLE)
        	if (IsFullTrimTriggered())
        	{
                if(Full_TRIM_IPC_SEND == false){
    			    FullTrimHandle(req, true);
                    return;
                }
    		}else
#endif
            {
        		if (Trim_handle(trim_data))
        		{
        			//trim_data->Validtag = 0xFFFFFFFF;
        			//trim_data->Validcnt = 0;
        		} else {
                    put_trim_dtag(dtag);
        		}
            }
        }else{
            put_trim_dtag(dtag);
        }
    }
#endif

    if (req->completion)
    {
    	req->completion(req);
    }

	dtag = mem2dtag(req->op_fields.trim.dsmr);
	dtag_put(DTAG_T_SRAM, dtag);
}
/*
ddr_code int trim_main(int argc, char *argv[])
{
    u32 slbah, slbal, cnt_ldah, cnt_ldal;
    if (argc ==3) {
        slbah = 0;
        slbal = strtol(argv[1], (void *)0, 0);
        cnt_ldah = 0;
        cnt_ldal = strtol(argv[2], (void *)0, 0);
    }else if (argc == 5) {
        slbah = strtol(argv[1], (void *)0, 0);
        slbal = strtol(argv[2], (void *)0, 0);
        cnt_ldah = strtol(argv[3], (void *)0, 0);
        cnt_ldal = strtol(argv[4], (void *)0, 0);
    }else{
        disp_apl_trace(LOG_ERR, 0, "\nInvalid number of argument\n");
        return 1;
    }
    u64 slba = (u64)slbah<<32|slbal;
    u64 cnt_lba = (u64)cnt_ldah<<32|cnt_ldal;

	void *mem = NULL;
    req_t *req = sys_malloc(SLOW_DATA, sizeof(req_t));
	if (req == NULL) {
		disp_apl_trace(LOG_INFO, 0, "get req fail, return",req);
		return 1;
	}

	dtag_get(DTAG_T_SRAM, &mem);
	if (mem == NULL) {
        sys_free(SLOW_DATA, req);
		disp_apl_trace(LOG_INFO, 0, "get dtag fail",req);
		return 1;
	}

	memset(req, 0, sizeof(req_t));
    req->op_fields.trim.att |= BIT(2);
    req->nsid = 0;
	req->op_fields.trim.nr = 1;
	req->op_fields.trim.dsmr = mem;

	struct nvme_dsm_range *dsmr = (struct nvme_dsm_range *)mem;
	dsmr[0].starting_lba = slba;
	dsmr[0].length = cnt_lba;

	rdisk_trim(req);

	sys_free(SLOW_DATA, req);
	return 0;
}

static DEFINE_UART_CMD(trim, "trim", "trim", "trim", 2, 4, trim_main);
*/
/*!
 * @brief 4k fast read command handler, for short call stack
 *
 * @param bcmd	btn command
 * @param btag	command tag
 *
 * @return	return true if handled
 */
ddr_code bool bcmd_fast_read(btn_cmd_t *bcmd, int btag)
{
	if (!bcmd_list_empty(&bcmd_pending))
		return false;

	if (!ucache_clean())
		return false;

	req_statistics.rd_rcv_ttl++;
	//lda_t lda = bcmd_get_slba(bcmd) >> NR_LBA_PER_LDA_SHIFT;
	lda_t lda = bcmd_get_slba(bcmd) >> (12 - host_sec_bitz); //joe add sec size 20200817
	u32 ret = l2p_single_srch(lda, 0, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);

	if (ret == 0)
	{
		disp_apl_trace(LOG_ERR, 0x7cb1, "srch full\n");
		return false;
	}

	return true;
}

#if 0//TCG_WRITE_DATA_ENTRY_ABORT
fast_code void rdisk_tcg_mbr_buf_rdy(u32 p0, u32 p1, u32 p2)
{
	disp_apl_trace(LOG_ERR, 0xe506, "[TCG] otf MBR cmd ID: %d", mbr_pl.nvm_cmd_id != 0xFFFF);
	if (mbr_pl.nvm_cmd_id != 0xFFFF)
		evt_set_cs(evt_tcg_mbr_buf_rdy, 0, 0, CS_TASK);
	else
		bcmd_resume();
}
#endif

fast_code void rdisk_ftl_ready_chk(u32 p0, u32 p1, u32 p2)
{
	if (shr_ftl_flags.b.ftl_ready)
		bcmd_resume();
	else
		evt_set_cs(evt_ftl_ready_chk, 0, 0, CS_TASK);
}

fast_code void rdisk_l2p_swq_chk(u32 p0, u32 p1, u32 p2)
{
	if (l2p_srch_swq_check() == false)
		bcmd_resume();
	else
		evt_set_cs(evt_l2p_swq_chk, 0, 0, CS_TASK);
}

static inline bool rdisk_l2p_ready_chk(lda_t lda, u32 cnt)
{
	u32 seg;
	u32 seg_id_0;
	u32 seg_id_1;

	if (shr_ftl_flags.b.l2p_all_ready)
		return true;

	//seg_id_0 = lda >> shr_ent_l2p_seg_shf;
	//seg_id_1 = (lda + cnt - 1) >> shr_ent_l2p_seg_shf;
	seg_id_0 = lda / (DTAG_SZE * 24 * sizeof(pda_t));
	seg_id_1 = (lda + cnt - 1) / (DTAG_SZE * 24 * sizeof(pda_t));
	for (seg = seg_id_0; seg <= seg_id_1; seg++)
	{
		if (test_bit(seg, shr_l2p_ready_bmp) == 0)
			return false;
	}

	return true;
}

fast_code void rdisk_l2p_urgent_load(lda_t lda, u32 cnt)
{
	u32 seg;
	//u32 seg_id_0 = lda >> shr_ent_l2p_seg_shf;
	//u32 seg_id_1 = (lda + cnt - 1) >> shr_ent_l2p_seg_shf;
	u32 seg_id_0 = lda / (DTAG_SZE * 24 * sizeof(pda_t));
	u32 seg_id_1 = (lda + cnt - 1) / (DTAG_SZE * 24 * sizeof(pda_t));

	for (seg = seg_id_0; seg <= seg_id_1; seg++)
	{
		if (test_bit(seg, shr_l2p_ready_bmp))
			continue;

		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_L2P_LOAD, 0, seg);
	}
}

ddr_code void rdisk_dump(void)
{
    disp_apl_trace(LOG_INFO, 0xfc77, "[FW ] ftl flag 0x%x pend_list_empty %d", shr_ftl_flags.all, bcmd_list_empty(&bcmd_pending));
    disp_apl_trace(LOG_INFO, 0x65d6, "[FW ] shr_lock_power_on %x", shr_lock_power_on);
    ucache_dump();
}

ddr_code void btn_rcmd_timeout_dump(void)
{
    disp_apl_trace(LOG_INFO, 0xcb0c, "[BTN] CmdQ:  NRD|0x%x PRD|0x%x NWR|0x%x", readl((void *)0xc0010368), readl((void *)0xc0010374), readl((void *)0xc0010380));
    disp_apl_trace(LOG_INFO, 0xa66d, "[BTN] DtagQ: com|0x%x rd_upd[0][1]|0x%x.0x%x, hst_rd 0x%x", readl((void *) 0xc00101e8), readl((void *)0xc0010194), readl((void *)0xc00101ac), readl((void *)0xc0010110));
    disp_apl_trace(LOG_INFO, 0x4c05, "[BTN] LLST:  CTRL|0x%x ENTRY|0x%x", readl((void *)0xc0010080), readl((void *)0xc0010100));
    disp_apl_trace(LOG_INFO, 0xa8cb, "[L2P] SrchQ: nrm|0x%x unmap|%x", readl((void *)0xc00d0104), readl((void *)0xc00d0200));
    bm_pop_all_rd_entry_list();
    rdisk_dump();
    cpu_msg_issue(1, CPU_MSG_ONDEMAND_DUMP, 0, 1);
    cpu_msg_issue(3, CPU_MSG_ONDEMAND_DUMP, 0, 1);
}

#ifdef ERRHANDLE_ECCT
fast_code u16 rdisk_ecct_search_idx(u32 lda, u16 left_margin, u16 right_matgin)
{
    u32 dErr_LDA;
    u16 wMid_idx;

    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);

    if (pECC_table->ecc_table_cnt == 0)
        return 0xFFFF;


#ifdef While_break
	u64 start = get_tsc_64();
#endif

    while (left_margin <= right_matgin)
    {
        wMid_idx = (left_margin + right_matgin) / 2;
        dErr_LDA = pECC_table->ecc_entry[wMid_idx].err_lda;

        if (dErr_LDA > lda)
        {
            if (wMid_idx != 0) right_matgin = wMid_idx  - 1;
            else break;
        }
        else if (dErr_LDA < lda)
        {
            left_margin = wMid_idx + 1;
        }
        else if (dErr_LDA == lda)
        {
            return wMid_idx;
        }

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
    }
    return 0XFFFF;
}

fast_code void ecct_rls_bcmd(u32 btag, u16 du_cnt)
{
    u16 i = 0;
    for(i = 0; i < du_cnt; i++)
    {
        bm_err_commit(i, btag);
    }
    return;
}

fast_code bool bcmd_ecct_check(lda_t slda, u32 ttl_cnt)
{
    lda_t temp_lda = slda;
    u32 du_ofst = 0;
    u16 widx = 0xFFFF;

    //btn_cmd_t *bcmd = btag2bcmd(btag);
    //btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
	//u64 slba = bcmd_get_slba(bcmd);

    for(du_ofst = 0; du_ofst < ttl_cnt; du_ofst++)
    {
        //widx = rdisk_ecct_search_idx(slda, 0, ecct_cnt-1);
        widx = rdisk_ecct_search_idx(temp_lda, 0, ecct_cnt-1);
        if(widx != 0xFFFF)    //found out lda idx in ecct & return
        {
            return true;
        }

        temp_lda++;
    }
    return false;
}
#endif

#if SHOW_PERFORMANCE_READ
fast_data u32 read_iops = 0;
fast_data u32 time_read = 0;
#endif
extern u8* TrimTable;
extern Trim_Info TrimInfo;

#if TCG_WRITE_DATA_ENTRY_ABORT
extern struct tcg_rdlk_list* tcg_rdcmd_list;
#endif

#ifdef NS_MANAGE //joe add define 20200916
fast_code void bcmd_exec(btn_cmd_t *bcmd, int btag)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
	lda_t lda = 0;
	u64 slba;
	u32 cnt = 0;
#if TCG_WRITE_DATA_ENTRY_ABORT && defined(TCG_NAND_BACKUP)
	dtag_t dtag_unmap;
#endif
#ifdef POWER_APST
	apst_transfer_to_last_ps();
#endif
	req_statistics.rd_rcv_ttl++;
	if (!bcmd_list_empty(&bcmd_pending))
	{
		bcmd_list_ins(btag, &bcmd_pending);
		return;
	}

	if (shr_ftl_flags.b.ftl_ready == false)
	{
		bcmd_list_ins(btag, &bcmd_pending);
		evt_set_cs(evt_ftl_ready_chk, 0, 0, CS_TASK);
		return;
	}

	if (srhq_avail_cnt < L2P_SRCHQ_AVAIL_CNT_MIN){
		bcmd_list_ins(btag, &bcmd_pending);
		evt_set_cs(evt_l2p_swq_chk, 0, 0, CS_TASK);
		return;
	}

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_clearReadPend();
	#endif

	//extern vu32 Rdcmd;
	//Rdcmd ++;

	slba = bcmd_get_slba(bcmd);
	bcmd_ex->ndu = calc_du_cnt(slba, bcmd->dw3.b.xfer_lba_num);
	//disp_apl_trace(LOG_DEBUG, 0, "JOE:read du_cnt:%d   slba:0x%x",bcmd_ex->ndu,slba);

    #if SHOW_PERFORMANCE_READ
    read_iops += bcmd_ex->ndu;
    if (jiffies - time_read >= 10*HZ/10) {
        time_read = jiffies;
        #if SHOW_PERFORMANCE_RLOG
        disp_apl_trace(LOG_INFO, 0x686b, "read performace:%d MB/s", read_iops>>8);
        #endif
        read_iops = 0;
    }
    #endif
	//joe add NS 20200813
	//joe add NS 20200813
	//disp_apl_trace(LOG_DEBUG, 0, "JOE:read du_cnt:%d   cross_du:%d cross_lbacnt:%d",bcmd_ex->ndu,cross_du,cross_lbacnt);//joe add 20200722

	switch (bcmd->dw0.b.cmd_type)
	{
	case PRI_READ:
	case NVM_READ:
#if TCG_WRITE_DATA_ENTRY_ABORT
		if(mTcgActivated) //if(mTcgStatus)
		{
			u16 rslt = tcg_io_chk_range(slba, bcmd->dw3.b.xfer_lba_num, mReadLockedStatus);
			switch (rslt)
			{
#ifdef TCG_NAND_BACKUP
				case TCG_DOMAIN_DUMMY:
					// Force to unmapped, return all 0 !!
					// commit DTAG
					dtag_unmap.dtag = RVTAG_ID;
					for(u32 i=0; i<(bcmd_ex->ndu); i++)
					{
						bm_rd_dtag_commit(i, btag, dtag_unmap);
					}
					return;

				case TCG_DOMAIN_SHADOW:
					// Return data in MBR table
					/*
					if (mbr_pl.nvm_cmd_id != 0xFFFF)
					{
						evt_set_cs(evt_tcg_mbr_buf_rdy, 0, 0, CS_TASK);
						return;
					}
					*/
					// Update payload
					mbr_pl[btag].slda = slba >> (DTAG_SHF - host_sec_bitz);
					mbr_pl[btag].du_cnt = bcmd_ex->ndu;
					mbr_pl[btag].nvm_cmd_id = bcmd->dw0.b.nvm_cmd_id;
					mbr_pl[btag].btag = btag;

					// L2P search
					bool need_rd_nand = false;
					for(u32 i=0; i<(bcmd_ex->ndu); i++)
					{
						u16 laa_srch = (mbr_pl[btag].slda + i) >> (DU_CNT_PER_PAGE_SHIFT);
						if(tcg_mid_info->l2p_tbl[laa_srch] == UNMAP_PDA)
						{
							dtag_unmap.dtag = RVTAG_ID;
							bm_rd_dtag_commit(i, btag, dtag_unmap);
							continue;
						}
						else
							need_rd_nand = true;
					}
					// push into die queue
					if(need_rd_nand){
						mbr_rd_payload_t *xfer_ptr_to_sh = tcm_local_to_share((void *)(mbr_pl + btag));
						u32 idx = mbr_rd_nand_cnt % 2;
						cpu_msg_issue(((idx<<1)+1), CPU_MSG_MBR_RD, 0, (u32)(xfer_ptr_to_sh));
						mbr_rd_nand_cnt++;
					}
					else
						mbr_pl[btag].nvm_cmd_id = 0xFFFF;
					return;
#endif
				case TCG_DOMAIN_ERROR:
					bcmd_ex->flags.b.tcg_wr_abrt = true;
					extern void tcg_rdcmd_list_add(u32 val);
					tcg_rdcmd_list_add(bcmd->dw0.b.nvm_cmd_id);
					for(u32 i=0; i<(bcmd_ex->ndu); i++)
					{
						bm_err_commit(i, btag);
					}
					//btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
					return;

				default:
					break;
			}
		}
#endif
//if(!(ns_order==1&&(drive_total_sector==ns_valid_sec))){
//if(1){
	if(!full_1ns){
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable();
		#endif
		//disp_apl_trace(LOG_DEBUG, 0, "[BCMD] case1");
		//lda = LBA_TO_LDA(slba);   //tony 20201111
		//cnt = bcmd_ex->ndu;
		//joe add NS 20200813
		//joe add NS 20200813
		lda_t lda_cross = 0, lda_left = 0; //joe 20200810
		u8 cross_flag = 0;
		u16  cross_lbacnt = 0, cross_du = 0, left_du = 0; //joe add 20200730
		u64 section_id1 = 0, section_id2 = 0;
		u64 sec_tran1=0,sec_tran2=0;
		//joe add NS 20200813
		if (host_sec_bitz == 9)
		{ //joe add sec size 20200817
			//section_id1 = slba / NS_SIZE_GRANULARITY_RDISK1;
			//section_id2 = (slba + bcmd->dw3.b.xfer_lba_num) / NS_SIZE_GRANULARITY_RDISK1; //joe T0 is xfer_lba_num
			section_id1 = slba >>NS_SIZE_GRANULARITY_BITOP1;
			section_id2 = (slba + bcmd->dw3.b.xfer_lba_num)>>NS_SIZE_GRANULARITY_BITOP1; //joe T0 is xfer_lba_num
			//disp_apl_trace(LOG_DEBUG, 0, "JOE: secid:%d  secid2:%d  slba:0x%x  bcmd->dw3.b.xfer_lba_num:%d", section_id1,section_id2, slba,bcmd->dw3.b.xfer_lba_num);
			if (section_id1 != section_id2)
			{
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: secid:%d  secid2:%d  slba:0x%x  bcmd->dw3.b.xfer_lba_num:%d", section_id1,section_id2, slba,bcmd->dw3.b.xfer_lba_num);
				cross_flag = 1;
				//cross_lbacnt = (section_id2) * NS_SIZE_GRANULARITY_RDISK1 - slba; //joe 20200610
				cross_lbacnt = ((section_id2) <<NS_SIZE_GRANULARITY_BITOP1) - slba;
				if (cross_lbacnt % 8 == 0)
					cross_du = (cross_lbacnt) >> 3;
				else
					cross_du = (cross_lbacnt + 8) >> 3; //joe 20200526 +8 is prevent the count is not divided by eight!!! important(for 512 host size)
			}
		}
		else
		{
			//section_id1 = slba / NS_SIZE_GRANULARITY_RDISK2;
			//section_id2 = (slba + bcmd->dw3.b.xfer_lba_num) / NS_SIZE_GRANULARITY_RDISK2; //joe T0 is xfer_lba_num
			section_id1 = slba >>NS_SIZE_GRANULARITY_BITOP2;
			section_id2 = (slba + bcmd->dw3.b.xfer_lba_num)>>NS_SIZE_GRANULARITY_BITOP2; //joe T0 is xfer_lba_num
			if (section_id1 != section_id2)
			{
				cross_flag = 1;
				//cross_lbacnt = (section_id2) * NS_SIZE_GRANULARITY_RDISK2 - slba; //joe 20200610
				cross_lbacnt = ((section_id2) <<NS_SIZE_GRANULARITY_BITOP2) - slba;
				cross_du = (cross_lbacnt);
			}
		}

		if (ts_io_block)
		{
			bcmd_ex->flags.b.err = true;
			btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
			return;
		}
	//totalorder start
	//if(ns_order!=1){
		if (cross_flag == 1)
		{ //joe add NS 20200813
			if (host_sec_bitz == 9)
			{ //joe add sec size 20200817
				//part1
				//  disp_apl_trace(LOG_INFO, 0, "JOE:read part1 nsid:%d read cmd origin lba:0x%x",bcmd->dw1.b.ns_id,slba);
				u64 slba_last;																										  //joe 20200525																												  //joe add 20200526
				sec_tran1=ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[section_id1];
				sec_tran2=ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[section_id2];
				//slba_last = slba - (section_id1)*NS_SIZE_GRANULARITY_RDISK1;														  //joe add 20200520
				slba_last = slba - ((section_id1)<<NS_SIZE_GRANULARITY_BITOP1);
				//slba = slba_last + (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[section_id1]) * NS_SIZE_GRANULARITY_RDISK1; //joe 20200610
				slba = slba_last + (sec_tran1<<NS_SIZE_GRANULARITY_BITOP1);
				lda_cross = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz));															  //joe add sec size 20200817																												  //disp_apl_trace(LOG_INFO, 0, "JOE:READ part2 nsid:%d read cmd origin lba:0x%x",bcmd->dw1.b.ns_id,slba);
				//slba = (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[section_id2]) * NS_SIZE_GRANULARITY_RDISK1;			  //joe 20200610																												  //disp_apl_trace(LOG_INFO, 0, "JOE: 2 read cmd shift ftl lba2:0x%x",slba);
				slba = (sec_tran2)<<NS_SIZE_GRANULARITY_BITOP1;
				left_du = bcmd_ex->ndu - cross_du;
				lda_left = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)); //joe add sec size 20200817
				//disp_apl_trace(LOG_DEBUG, 0, "lda_cross:0x%x   lda_left:0x%x",lda_cross,lda_left);
				//disp_apl_trace(LOG_DEBUG, 0, "cross_du:0x%d   left_du:0x%d  bcmd_ex->ndu:%d",cross_du,left_du,bcmd_ex->ndu);
			}
			else
			{
				//part1
				//  disp_apl_trace(LOG_INFO, 0, "JOE:read part1 nsid:%d read cmd origin lba:0x%x",bcmd->dw1.b.ns_id,slba);
				u64 slba_last;
				sec_tran1=ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[section_id1];
				sec_tran2=ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[section_id2];//joe 20200525																												  //joe add 20200526
				//slba_last = slba - (section_id1)*NS_SIZE_GRANULARITY_RDISK2;														  //joe add 20200520
				slba_last = slba - ((section_id1)<<NS_SIZE_GRANULARITY_BITOP2);
				//slba = slba_last + (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[section_id1]) * NS_SIZE_GRANULARITY_RDISK2; //joe 20200610																												  //disp_apl_trace(LOG_INFO, 0, "JOE:1 read cmd shift ftl lba1:0x%x",slba);
				slba = slba_last + (sec_tran1<<NS_SIZE_GRANULARITY_BITOP2);
				lda_cross = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz));															  //joe add sec size 20200817																												  //disp_apl_trace(LOG_INFO, 0, "JOE:READ part2 nsid:%d read cmd origin lba:0x%x",bcmd->dw1.b.ns_id,slba);
				//slba = (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[section_id2]) * NS_SIZE_GRANULARITY_RDISK2;			  //joe 20200610																												  //disp_apl_trace(LOG_INFO, 0, "JOE: 2 read cmd shift ftl lba2:0x%x",slba);
				slba = (sec_tran2)<<NS_SIZE_GRANULARITY_BITOP2;
				left_du = bcmd_ex->ndu - cross_du;
				lda_left = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)); //joe add sec size 20200817
			}

			if (rdisk_l2p_ready_chk(lda_cross, cross_du) == false)
			{
				bcmd_list_ins(btag, &bcmd_pending);
				rdisk_l2p_urgent_load(lda_cross, cross_du);
				return;
			}
			if (rdisk_l2p_ready_chk(lda_left, left_du) == false)
			{
				bcmd_list_ins(btag, &bcmd_pending);
				rdisk_l2p_urgent_load(lda_left, left_du);
				return;
			}
		}
		else
		{
			//NOT cross case
			//joe add NS 20200813
			//disp_apl_trace(LOG_INFO, 0, "JOE:nsid:%d read cmd origin lba:0x%x",bcmd->dw1.b.ns_id,slba);
			//joe add 20200525 NS
			u64 slba_last; //joe 20200525
			if (host_sec_bitz == 9)
			{																													 //joe add sec size 20200817
				//u16 sec_id = slba / NS_SIZE_GRANULARITY_RDISK1;																	 //joe add 20200520
				//slba_last = slba - (sec_id)*NS_SIZE_GRANULARITY_RDISK1;															 //joe add 20200520
				u64 sec_id = slba >>NS_SIZE_GRANULARITY_BITOP1;																	 //joe add 20200520
				slba_last = slba - ((sec_id)<<NS_SIZE_GRANULARITY_BITOP1);
				sec_tran1=ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[sec_id];
				//slba = slba_last + (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]) * NS_SIZE_GRANULARITY_RDISK1; //joe 20200610
				slba = slba_last +(sec_tran1 <<NS_SIZE_GRANULARITY_BITOP1);
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: read cmd shift lba:H0x%x L0x%x",slba>>32,slba);
				lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)); //joe add sec size 20200817
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: read cmd shift lda:0x%x",lda);
				//disp_apl_trace(LOG_DEBUG, 0, " bcmd_ex->ndu:%d",bcmd_ex->ndu);
			}
			else
			{
				//u16 sec_id = slba / NS_SIZE_GRANULARITY_RDISK2;																	 //joe add 20200520
				//slba_last = slba - (sec_id)*NS_SIZE_GRANULARITY_RDISK2;															 //joe add 20200520
				u64 sec_id = slba >>NS_SIZE_GRANULARITY_BITOP2;																	 //joe add 20200520
				slba_last = slba - ((sec_id)<<NS_SIZE_GRANULARITY_BITOP2);
				sec_tran1=ns_sec_id[bcmd->dw1.b.ns_id - 1].sec_id[sec_id];
				//slba = slba_last + (ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[sec_id]) * NS_SIZE_GRANULARITY_RDISK2; //joe 20200610
				slba = slba_last +( (sec_tran1) <<NS_SIZE_GRANULARITY_BITOP2);
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: read cmd shift lba:0x%x",slba);
				lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)); //joe add sec size 20200817
				//disp_apl_trace(LOG_DEBUG, 0, "JOE: read cmd shift lda:0x%x",lda);
				//disp_apl_trace(LOG_DEBUG, 0, " bcmd_ex->ndu:%d",bcmd_ex->ndu);
			}
			cnt = bcmd_ex->ndu;
			if (rdisk_l2p_ready_chk(lda, cnt) == false)
			{
				bcmd_list_ins(btag, &bcmd_pending);
				rdisk_l2p_urgent_load(lda, cnt);
				return;
			}
		}


#ifdef ERRHANDLE_ECCT
        //epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
        //pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
        //if(pECC_table->ecc_table_cnt)     //Search same idx of lda in ecct
        if(ecct_cnt)
        {
            u32 cross_cnt = (cross_flag == 1) ? cross_du : cnt;
            lda_t lda_srh_cross = (cross_flag == 1) ? lda_cross : lda;
            //u32 cross_idx;
            //u16 widx_cross = 0xFFFF;

            //for(cross_idx = 0; cross_idx < cross_cnt; cross_idx++)
            //{
            //    widx_cross = rdisk_ecct_search_idx(lda_srh_cross, 0, ecct_cnt-1);
            //    if(widx_cross != 0xFFFF)    //found out lda idx in ecct & return
            //    {
            //        disp_apl_trace(LOG_INFO, 0, "[DBG] RD abort, ecct_cnt[%d] lda[0x%x] btag[%d], du_ofst[%d]", ecct_cnt, lda_srh_cross, btag, cross_idx);
            //    	//bm_err_commit(cross_idx, btag);
			//		bcmd_ex->flags.b.err = true;
            //        bcmd_ex->flags.b.bcmd_ecc_hit = true;
            //        btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
            //        return;
            //    }
            //    lda_srh_cross++;
            //}

            if(bcmd_ecct_check(lda_srh_cross, cross_cnt))
            {
				#if (Synology_case)
				host_unc_err_cnt ++;// for Synology case log page C0h Host UNC Error Count (1 cnt for 1 cmd)
                #endif
				disp_apl_trace(LOG_INFO, 0x07e7, "[DBG] RD abort, ecct_cnt[%d] lda[0x%x] btag[%d]", ecct_cnt, lda_srh_cross, btag);
                ecct_rls_bcmd(btag, bcmd_ex->ndu);
                return;
            }

            if (cross_flag == 1)
            {
                //lda_t lda_srh_left = lda_left;
                //u32 left_idx;
                //u16 widx_left = 0xFFFF;

                //for(left_idx = 0; left_idx < left_du; left_idx++)
                //{
                //    widx_left = rdisk_ecct_search_idx(lda_srh_left, 0, ecct_cnt-1);
                //    if(widx_left != 0xFFFF)    //found out lda idx in ecct & return
                //    {
				//		disp_apl_trace(LOG_INFO, 0, "[DBG] RD abort, ecct_cnt[%d] lda[0x%x] btag[%d], du_ofst[%d]", ecct_cnt, lda_srh_left, btag, left_idx + cross_du);
                //        //bm_err_commit(left_idx + cross_du, btag);
				//		bcmd_ex->flags.b.err = true;
                //        bcmd_ex->flags.b.bcmd_ecc_hit = true;
                //        btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
                //        return;
                //    }
                //    lda_srh_left++;
                //}

                if(bcmd_ecct_check(lda_left, left_du))
                {
                    disp_apl_trace(LOG_INFO, 0xd4bb, "[DBG] RD abort, ecct_cnt[%d] slda[0x%x] btag[%d]", ecct_cnt, lda_left, btag);
                    ecct_rls_bcmd(btag, bcmd_ex->ndu);
                    return;
                }
            }
        }
#endif

		//disp_apl_trace(LOG_DEBUG, 0, "srch s %d l %d btag %d", lda, cnt, btag);
#if (TRIM_SUPPORT == ENABLE)
		if (cross_flag == 1)
		{ //joe add NS 20200813

			//cross section1 start
			if (TrimInfo.Dirty && IsCMDinTrimTable(lda_cross, lda_cross + (cross_du - 1),0))
			{
				u32 i = 0;
				u32 nottrimhit = 0;
				for (i = 0; i < cross_du; i++)
				{
					if (!SrchTrimTable(lda_cross + i))
					{
						nottrimhit++;
						continue;
					}

					if (nottrimhit)
					{
						shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
						if (ucache_clean())
						{
							l2p_srch_ofst(lda_cross + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
						}
						else
						{
							ucache_read_data_out(btag, lda_cross + i - nottrimhit, nottrimhit, (i - nottrimhit));
						}
						nottrimhit = 0;
					}
					dtag_t dtag = {.dtag = RVTAG_ID};

					bm_rd_dtag_commit(i, btag, dtag);

				}
				if (nottrimhit)
				{
					shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
					if (ucache_clean())
					{
						l2p_srch_ofst(lda_cross + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
					}
					else
					{
						ucache_read_data_out(btag, lda_cross + i - nottrimhit, nottrimhit, (i - nottrimhit));
					}
					nottrimhit = 0;
				}
			}
			else
			{
				shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
				//disp_apl_trace(LOG_DEBUG, 0, "srch lda cross 0x%x l %d btag %d", lda_cross,cross_du, btag);
				if (ucache_clean())
				{
					u32 ret = l2p_srch(lda_cross, cross_du, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
					sys_assert(ret == cross_du);
				}
				else
				{
					//disp_apl_trace(LOG_DEBUG, 0, "sec1 cache out srch lda cross 0x%x l %d btag %d", lda_cross,cross_du, btag);
					ucache_read_data_out(btag, lda_cross, cross_du, 0);
				}
			}
			//cross section1 end
			//cross section2 start
			if (left_du > 0)
			{
				if (TrimInfo.Dirty && IsCMDinTrimTable(lda_left, lda_left + (left_du - 1),0))
				{
					u32 i = 0;
					u32 nottrimhit = 0;
					for (i = 0; i < left_du; i++)
					{
						if (!SrchTrimTable(lda_left + i))
						{
							nottrimhit++;
							continue;
						}

						if (nottrimhit)
						{
							shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
							if (ucache_clean())
							{
								l2p_srch_ofst(lda_left + i - nottrimhit, nottrimhit, btag, i - nottrimhit + cross_du, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
							}
							else
							{
								ucache_read_data_out(btag, lda_left + i - nottrimhit, nottrimhit, (i - nottrimhit + cross_du));
							}
							nottrimhit = 0;
						}
						dtag_t dtag = {.dtag = RVTAG_ID};

						bm_rd_dtag_commit(i+ cross_du, btag, dtag);
					}
					if (nottrimhit)
					{
						shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
						if (ucache_clean())
						{
							l2p_srch_ofst(lda_left + i - nottrimhit, nottrimhit, btag, i - nottrimhit + cross_du, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
						}
						else
						{
							ucache_read_data_out(btag, lda_left + i - nottrimhit, nottrimhit, (i - nottrimhit + cross_du));
						}
						nottrimhit = 0;
					}
				}
				else
				{
					shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
					//disp_apl_trace(LOG_DEBUG, 0, "srch lda left 0x%x l %d btag %d", lda_left,left_du, btag);
					if (ucache_clean())
					{
						u32 ret = l2p_srch_ofst(lda_left, left_du, btag, cross_du, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
						sys_assert(ret == left_du);
					}
					else
					{
						//disp_apl_trace(LOG_DEBUG, 0, "sec2 cache out srch lda left 0x%x l %d btag %d  cross_du:%d", lda_left,left_du, btag,cross_du);
						ucache_read_data_out(btag, lda_left, left_du, cross_du);
					}
				}
			}
			//cross section2 end
		}
		else
		{ //cross flag joe 20200914
			//disp_apl_trace(LOG_DEBUG, 0, "no cross case lda:0x%x  cnt:%d",lda,cnt);
			if (TrimInfo.Dirty && IsCMDinTrimTable(lda, lda + (cnt - 1),0))
			{
				u32 i = 0;
				u32 nottrimhit = 0;
				for (i = 0; i < cnt; i++)
				{
					if (!SrchTrimTable(lda + i))
					{
						nottrimhit++;
						continue;
					}

					if (nottrimhit)
					{
						shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
						if (ucache_clean())
						{
							l2p_srch_ofst(lda + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
						}
						else
						{
							ucache_read_data_out(btag, lda + i - nottrimhit, nottrimhit, (i - nottrimhit));
						}
						nottrimhit = 0;
					}
					dtag_t dtag = {.dtag = RVTAG_ID};

					bm_rd_dtag_commit(i, btag, dtag);

				}
				if (nottrimhit)
				{
					shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
					if (ucache_clean())
					{
						l2p_srch_ofst(lda + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
					}
					else
					{
						ucache_read_data_out(btag, lda + i - nottrimhit, nottrimhit, (i - nottrimhit));
					}
					nottrimhit = 0;
				}
			}
			else
			{
				shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;

				if (ucache_clean())
				{
					u32 ret = l2p_srch(lda, cnt, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
					sys_assert(ret == cnt);
				}
				else
				{
					ucache_read_data_out(btag, lda, cnt, 0);
				}
			}

		} //cross flag else end

#else
		if (cross_flag == 1){
			shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;

			if (ucache_clean())
			{
				u32 ret = l2p_srch(lda_cross, cross_du, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
				sys_assert(ret == cross_du);
			}
			else
			{
				ucache_read_data_out(btag, lda_cross, cross_du, 0);
			}
			if (left_du > 0)
			{
				shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
				if (ucache_clean())
				{
					u32 ret = l2p_srch_ofst(lda_left, left_du, btag, cross_du, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
					sys_assert(ret == left_du);
				}
				else
				{
					ucache_read_data_out(btag, lda_left, left_du, cross_du);
				}
			}
		}else{
			shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
			if (ucache_clean())
			{
				u32 ret = l2p_srch(lda, cnt, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
				sys_assert(ret == cnt);
			}
			else
			{
				ucache_read_data_out(btag, lda, cnt, 0);
			}
		}
#endif
		break;
	}else{//for the case 1ns and fulldisk
		//bcmd read case 2
		//disp_apl_trace(LOG_DEBUG, 0, "[BCMD] case2");
		if (host_sec_bitz == 9)
			lda =slba>>3;
		else
			lda=slba;
		cnt = bcmd_ex->ndu;
		//disp_apl_trace(LOG_DEBUG, 0, "srch s %x%x lda:%x l %d btag %d", (u32)(slba>>32), (u32)slba,lda, bcmd->dw3.b.xfer_lba_num, btag);

#ifdef ERRHANDLE_ECCT
        //epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
        //pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
        //if(pECC_table->ecc_table_cnt)     //Search same idx of lda in ecct
        if(ecct_cnt)
        {
            //lda_t lda_srh = lda;
            //u32 i;
            //u16 widx = 0xFFFF;
            //for(i = 0; i < cnt; i++)
            //{
            //    widx = rdisk_ecct_search_idx(lda_srh, 0, ecct_cnt-1);
            //    if(widx != 0xFFFF)    //found out lda idx in ecct & return
            //    {
			//		disp_apl_trace(LOG_INFO, 0, "[DBG] RD abort, ecct_cnt[%d] lda[0x%x] btag[%d], du_ofst[%d]", ecct_cnt, lda_srh, btag, i);
        	//	    //bm_err_commit(i, btag);
			//		bcmd_ex->flags.b.err = true;
            //      bcmd_ex->flags.b.bcmd_ecc_hit = true;
        	//	    btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
        	//	    return;
            //    }
            //    lda_srh++;
            //}

            if(bcmd_ecct_check(lda, cnt))
            {
				#if (Synology_case)
				host_unc_err_cnt ++;// for Synology case log page C0h Host UNC Error Count (1 cnt for 1 cmd)
                #endif
				disp_apl_trace(LOG_INFO, 0x6f16, "[DBG] RD abort, ecct_cnt[%d] slda[0x%x] btag[%d]", ecct_cnt, lda, btag);
                ecct_rls_bcmd(btag, cnt);
                return;
            }
        }
#endif

		if (ts_io_block)
		{
			bcmd_ex->flags.b.err = true;
			btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
			return;
		}

		if (rdisk_l2p_ready_chk(lda, cnt) == false)
		{
			bcmd_list_ins(btag, &bcmd_pending);
			rdisk_l2p_urgent_load(lda, cnt);
			return;
		}

#if (TRIM_SUPPORT == ENABLE)
		if (TrimInfo.Dirty && IsCMDinTrimTable(lda, lda + (cnt - 1),0))
		//if(0)
		{
			#if (CO_SUPPORT_READ_AHEAD == TRUE)
			if (ra_range_chk(lda, cnt))
			{
				ra_disable();
			}

			// TODO DISCUSS //force abort all??
			#endif

			u32 i = 0;
			u32 nottrimhit = 0;
			for (i = 0; i < cnt; i++)
			{
				if (!SrchTrimTable(lda + i))
				{
					nottrimhit++;
					continue;
				}

				if (nottrimhit)
				{
					shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
					if (ucache_clean())
					{
						l2p_srch_ofst(lda + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
					}
					else
					{
						ucache_read_data_out(btag, lda + i - nottrimhit, nottrimhit, (i - nottrimhit));
					}
					nottrimhit = 0;
				}
				dtag_t dtag = {.dtag = RVTAG_ID};

				bm_rd_dtag_commit(i, btag, dtag);
			}
			if (nottrimhit)
			{
				shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
				if (ucache_clean())
				{
					l2p_srch_ofst(lda + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
				}
				else
				{
					ucache_read_data_out(btag, lda + i - nottrimhit, nottrimhit, (i - nottrimhit));
				}
				nottrimhit = 0;
			}
		}
		else
		{

			#if (CO_SUPPORT_READ_AHEAD == TRUE)
				if (!ra_whole_hit(lda, cnt))
				{
					shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;

					if (ucache_clean())
					{
						#ifdef CO_SUPPORT_READ_REORDER
						if (bcmd_ex->ndu == 1)
						{
							u32 ret = l2p_single_srch(lda, SINGLE_SRCH_MARK, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
							sys_assert(ret == 1);
						}
						else
						#endif
						{
							u32 ret = l2p_srch(lda, cnt, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
							sys_assert(ret == cnt);
						}
					}
					else
					{
						ucache_read_data_out(btag, lda, cnt, 0);
					}
				}
				else
				{
					if (!ra_data_out(btag, lda, cnt))
					{
						bcmd_list_ins(btag, &bcmd_pending);
						//evt_set_cs(evt_otf_readcmd_chk, 0, 0, CS_TASK);
						return;
					}
				}
			#else
				shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;

				if (ucache_clean())
				{
					#ifdef CO_SUPPORT_READ_REORDER
					if (bcmd_ex->ndu == 1)
					{
						u32 ret = l2p_single_srch(lda, SINGLE_SRCH_MARK, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
						sys_assert(ret == 1);
					}
					else
					#endif
					{
						u32 ret = l2p_srch(lda, cnt, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
						sys_assert(ret == cnt);
					}
				}
				else
				{
					ucache_read_data_out(btag, lda, cnt, 0);
				}
			#endif

		}
#else
		shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
		if (ucache_clean())
		{
			#ifdef CO_SUPPORT_READ_REORDER
			if (bcmd_ex->ndu == 1)
			{
				u32 ret = l2p_single_srch(lda, SINGLE_SRCH_MARK, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
				sys_assert(ret == 1);
			}
			else
			#endif
			{
				u32 ret = l2p_srch(lda, cnt, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
				sys_assert(ret == cnt);
			}
		}
		else
		{
			ucache_read_data_out(btag, lda, cnt, 0);
		}
#endif

		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		//if (shr_ftl_flags.b.l2p_all_ready && full_1ns) // prevent large QD and job performance drop
		{
			ra_forecast(lda, cnt);
		}
		#endif

		break;
	}
	case NVM_WRITE:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable();
		#endif
#if (CPU_DTAG != CPU_ID) && defined(MPC)
		rdisk_free_wd_req += bcmd_ex->ndu;
		cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_WR_CREDIT, 0, bcmd_ex->ndu);
#endif
		bcmd_ex->du_xfer_left += (short)bcmd_ex->ndu;
		if (bcmd_ex->du_xfer_left == 0 && bcmd_ex->flags.b.wr_err == 0)
			btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
		//disp_apl_trace(LOG_DEBUG, 0, "(W) btag(%d) NDU(%d)", btag, bcmd_ex->ndu);
		break;
	case IO_MGR:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable();
		#endif
#if 0 // there is no ns id in btn io mgr command
		if (bcmd->dw1.b.ns_id != 1) {
			disp_apl_trace(LOG_ERR, 0x5888, "wrong NS %d btag %d", bcmd->dw1.b.ns_id, btag);
			btn_iom_cmd_rels(bcmd);
			break;
		}
#endif
		if (!list_empty(&_req_pending))
		{
			req_t *req = list_first_entry(&_req_pending, req_t, entry2);

			if (rdisk_free_wd_req == rdisk_free_wd_rec)
				io_mgr_cmd_exec(req, bcmd);
		}
		bcmd_list_ins(btag, &bcmd_pending);
		break;
	default:
		panic("not support");
		break;
	};
}
#else
fast_code void bcmd_exec(btn_cmd_t *bcmd, int btag)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
	lda_t lda;
	u64 slba;
#if TCG_WRITE_DATA_ENTRY_ABORT && defined(TCG_NAND_BACKUP)
	dtag_t dtag_unmap;
#endif
#ifdef POWER_APST
	apst_transfer_to_last_ps();
#endif
	req_statistics.rd_rcv_ttl++;
	if (!bcmd_list_empty(&bcmd_pending))
	{
		bcmd_list_ins(btag, &bcmd_pending);
		return;
	}

	if (shr_ftl_flags.b.ftl_ready == false)
	{
		bcmd_list_ins(btag, &bcmd_pending);
		evt_set_cs(evt_ftl_ready_chk, 0, 0, CS_TASK);
		return;
	}

	if (srhq_avail_cnt < L2P_SRCHQ_AVAIL_CNT_MIN){
		bcmd_list_ins(btag, &bcmd_pending);
		evt_set_cs(evt_l2p_swq_chk, 0, 0, CS_TASK);
		return;
	}

	//extern vu32 Rdcmd;
	//Rdcmd ++;

	slba = bcmd_get_slba(bcmd);
	bcmd_ex->ndu = calc_du_cnt(slba, bcmd->dw3.b.xfer_lba_num);

	switch (bcmd->dw0.b.cmd_type)
	{
	case PRI_READ:
	case NVM_READ:
#if TCG_WRITE_DATA_ENTRY_ABORT
		if(mTcgActivated) //if(mTcgStatus)
		{
			u16 rslt = tcg_io_chk_range(slba, bcmd->dw3.b.xfer_lba_num, mReadLockedStatus);
			switch (rslt)
			{
#ifdef TCG_NAND_BACKUP
				case TCG_DOMAIN_DUMMY:
					// Force to unmapped, return all 0 !!
					// commit DTAG
					dtag_unmap.dtag = RVTAG_ID;
					for(u32 i=0; i<(bcmd_ex->ndu); i++)
					{
						bm_rd_dtag_commit(i, btag, dtag_unmap);
					}
					return;

				case TCG_DOMAIN_SHADOW:
					// Return data in MBR table
					if (mbr_pl.nvm_cmd_id != 0xFFFF)
					{
						evt_set_cs(evt_tcg_mbr_buf_rdy, 0, 0, CS_TASK);
						return;
					}

					// Update payload
					mbr_pl.slda = slba >> (DTAG_SHF - host_sec_bitz);
					mbr_pl.du_cnt = bcmd_ex->ndu;
					mbr_pl.nvm_cmd_id = bcmd->dw0.b.nvm_cmd_id;
					mbr_pl.btag = btag;

					// L2P search
					bool need_rd_nand = false;
					for(u32 i=0; i<(bcmd_ex->ndu); i++)
					{
						u16 laa_srch = (mbr_pl.slda + i) >> (DU_CNT_PER_PAGE_SHIFT);
						if(tcg_mid_info->l2p_tbl[laa_srch] == UNMAP_PDA)
						{
							dtag_unmap.dtag = RVTAG_ID;
							bm_rd_dtag_commit(i, btag, dtag_unmap);
							continue;
						}
						else
							need_rd_nand = true;
					}
					// push into die queue
					if(need_rd_nand){
						mbr_rd_payload_t *xfer_ptr_to_sh = tcm_local_to_share((void *)(mbr_pl + btag));
						u32 idx = mbr_rd_nand_cnt % 2;
						cpu_msg_issue(((idx<<1)+1), CPU_MSG_MBR_RD, 0, (u32)(xfer_ptr_to_sh));
						mbr_rd_nand_cnt++;
					}
					else
						mbr_pl[btag].nvm_cmd_id = 0xFFFF;
					return;
#endif
				case TCG_DOMAIN_ERROR:
					bcmd_ex->flags.b.tcg_wr_abrt = true;
					extern void tcg_rdcmd_list_add(u32 val);
					tcg_rdcmd_list_add(bcmd->dw0.b.nvm_cmd_id);
					for(u32 i=0; i<(bcmd_ex->ndu); i++)
					{
						bm_err_commit(i, btag);
					}
					//btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
					return;

				default:
					break;
			}
		}
#endif

		lda = LBA_TO_LDA(slba);
		u32 cnt = bcmd_ex->ndu;
        //disp_apl_trace(LOG_INFO, 0, "srch s %x%x lda:%x l %d btag %d", (u32)(slba>>32), (u32)slba,lda, bcmd->dw3.b.xfer_lba_num, btag);
#ifdef ERRHANDLE_ECCT
        //epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
        //pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
        //if(pECC_table->ecc_table_cnt)     //Search same idx of lda in ecct
		if(ecct_cnt)
        {
            //u32 i;
            //u16 widx = 0xFFFF;
            //lda_t lda_srh = lda;
            //for(i = 0; i < cnt; i++)
            //{
            //    widx = rdisk_ecct_search_idx(lda_srh, 0, ecct_cnt-1);
            //    if(widx != 0xFFFF)    //found out lda idx in ecct & return
            //    {
			//		disp_apl_trace(LOG_INFO, 0, "[DBG] RD abort, ecct_cnt[%d] lda[0x%x] btag[%d], du_ofst[%d]", ecct_cnt, lda_srh, btag, i);
            //        //bm_err_commit(i, btag);
        	//	    bcmd_ex->flags.b.err = true;
            //        bcmd_ex->flags.b.bcmd_ecc_hit = true;
			//		btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
        	//	    return;
            //    }
            //    lda_srh++;
            //}

            if(bcmd_ecct_check(lda, cnt))
            {
                disp_apl_trace(LOG_INFO, 0xc1ff, "[DBG] RD abort, ecct_cnt[%d] slda[0x%x] btag[%d]", ecct_cnt, lda, btag);
                ecct_rls_bcmd(btag, cnt);
                return;
            }
        }
#endif

		if (ts_io_block)
		{
			bcmd_ex->flags.b.err = true;
			btn_cmd_rels_push(bcmd, RLS_T_READ_FW_ABORT);
			return;
		}

		if (rdisk_l2p_ready_chk(lda, cnt) == false)
		{
			bcmd_list_ins(btag, &bcmd_pending);
			rdisk_l2p_urgent_load(lda, cnt);
			return;
		}

#if (TRIM_SUPPORT == ENABLE)
		if (IsCMDinTrimTable(lda, lda + (cnt - 1),0))
		{
			u32 i = 0;
			u32 nottrimhit = 0;
			for (i = 0; i < cnt; i++)
			{
				if (!SrchTrimTable(lda + i))
				{
					nottrimhit++;
					continue;
				}

				if (nottrimhit)
				{
					shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
					if (ucache_clean())
					{
						l2p_srch_ofst(lda + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
					}
					else
					{
						ucache_read_data_out(btag, lda + i - nottrimhit, nottrimhit, (i - nottrimhit));
					}
					nottrimhit = 0;
				}
				dtag_t dtag = {.dtag = RVTAG_ID};
				bm_rd_dtag_commit(i, btag, dtag);
			}
			if (nottrimhit)
			{
				shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
				if (ucache_clean())
				{
					l2p_srch_ofst(lda + i - nottrimhit, nottrimhit, btag, i - nottrimhit, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
				}
				else
				{
					ucache_read_data_out(btag, lda + i - nottrimhit, nottrimhit, (i - nottrimhit));
				}
				nottrimhit = 0;
			}
		}
		else
		{
			shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;

			if (ucache_clean())
			{
				u32 ret = l2p_srch(lda, cnt, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
				sys_assert(ret == cnt);
			}
			else
			{
				ucache_read_data_out(btag, lda, cnt, 0);
			}
		}
#else
		shr_l2p_srch_nid[btag] = bcmd->dw0.b.nvm_cmd_id;
		if (ucache_clean())
		{
			u32 ret = l2p_srch(lda, cnt, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
			sys_assert(ret == cnt);
		}
		else
		{
			ucache_read_data_out(btag, lda, cnt, 0);
		}
#endif
		break;
	case NVM_WRITE:
#if (CPU_DTAG != CPU_ID) && defined(MPC)
		rdisk_free_wd_req += bcmd_ex->ndu;
		cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_WR_CREDIT, 0, bcmd_ex->ndu);
#endif
		bcmd_ex->du_xfer_left += (short)bcmd_ex->ndu;
		if (bcmd_ex->du_xfer_left == 0 && bcmd_ex->flags.b.wr_err == 0)
			btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
		disp_apl_trace(LOG_DEBUG, 0xae7c, "(W) btag(%d) NDU(%d)", btag, bcmd_ex->ndu);
		break;
	case IO_MGR:
#if 0 // there is no ns id in btn io mgr command
		if (bcmd->dw1.b.ns_id != 1) {
			disp_apl_trace(LOG_ERR, 0x664f, "wrong NS %d btag %d", bcmd->dw1.b.ns_id, btag);
			btn_iom_cmd_rels(bcmd);
			break;
		}
#endif
		if (!list_empty(&_req_pending))
		{
			req_t *req = list_first_entry(&_req_pending, req_t, entry2);

			if (rdisk_free_wd_req == rdisk_free_wd_rec)
				io_mgr_cmd_exec(req, bcmd);
		}
		bcmd_list_ins(btag, &bcmd_pending);
		break;
	default:
		panic("not support");
		break;
	};
}
#endif
fast_code void bcmd_resume(void)
{
	bcmd_list_t local = {.head = 0xFFFF, .tail = 0xFFFF};
	u16 btag;
	btn_cmd_t *bcmd;

	if (bcmd_list_empty(&bcmd_pending))
		return;

	disp_apl_trace(LOG_DEBUG, 0x0a5b, "bcmd resume");
	bcmd_list_move(&local, &bcmd_pending);

	do
	{
		btag = bcmd_list_pop_head(&local);
		bcmd = btag2bcmd(btag);

		bcmd_exec(bcmd, btag);
	} while (!bcmd_list_empty(&local));
}

#if (CO_SUPPORT_READ_AHEAD == TRUE)
fast_code static void rdisk_ra_bcmd_resume(u32 param0, u32 payload, u32 count)
{
	bcmd_resume();
}
#endif

fast_code void rdisk_dtag_gc_done(ftl_core_ctx_t *fctx)
{
	sys_free(FAST_DATA, fctx);
	dtag_gcing = false;
}

fast_code void rdisk_dtag_gc(u32 *need)
{
	if (dtag_gcing)
	{
		//disp_apl_trace(LOG_DEBUG, 0, "dtag_gcing");
		return;
	}
	else
	{
		dtag_gcing = true;
	}

	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));

	sys_assert(fctx);

	fctx->ctx.caller = NULL;
	fctx->ctx.cmpl = rdisk_dtag_gc_done;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->flags.b.dtag_gc = 1;
	//ftl_core_flush(fctx);
	if (ftl_core_flush(fctx) == FTL_ERR_OK)
	{
		disp_apl_trace(LOG_INFO, 0x2650, "fctx:0x%x done", fctx);
		fctx->ctx.cmpl(&fctx->ctx);
	}
}

fast_code void rdisk_flush_done(ftl_core_ctx_t *ctx)
{
	evt_set_cs(evt_flush_done, (u32)ctx, 0, CS_TASK);
    //disp_apl_trace(LOG_INFO, 0xb151, "ctx:0x%x", ctx);
}

fast_code void evt_rdisk_flush_done(u32 param, u32 data, u32 r1)
{
    ftl_core_ctx_t *ctx = (ftl_core_ctx_t *)data;
    //disp_apl_trace(LOG_INFO, 0xb6ec, "ctx:0x%x", ctx);
	req_t *req = (req_t *)ctx->caller;
	ftl_flush_data_t *fctx = (ftl_flush_data_t *)ctx;

	fctx->nsid = 0;
	list_del(&req->entry2);

	evt_set_imt(evt_cmd_done, (u32)req, 0);

	btn_de_wr_enable();
	bcmd_resume();
	req_resume();
}
//callback for set feature 06 VWC
fast_code void VWC_flush_done(ftl_core_ctx_t *ctx)
{

	ftl_flush_data_t *fctx = (ftl_flush_data_t *)ctx;
	req_t *req = (req_t *)ctx->caller;
	fctx->nsid = 0;
	evt_set_imt(evt_cmd_done, (u32)req, 0);//return CQ
	if(_flush_ctx.nsid == 0) //Avoid the wr_disable of the shutdown flow being opened
	{
		btn_de_wr_enable();
	}
	disp_apl_trace(LOG_ALW, 0x58e5,"[VWC] VWC flush done");
}

fast_code static void rdisk_shutdown(ftl_core_ctx_t *ctx)
{
	req_t *req = (req_t *)ctx->caller;
	ftl_flush_data_t *fctx = (ftl_flush_data_t *)ctx;

	fctx->nsid = 0;
	disp_apl_trace(LOG_INFO, 0xef51, "rdisk shutdown %x", req);

	#if (TRIM_SUPPORT == ENABLE)
		TrimPowerLost(true);
	#endif

	nvme_info_save(req);

	if(req->op_fields.flush.shutdown)
	{
		epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		epm_ftl_data->POR_tag = FTL_EPM_POR_TAG;
	}

	epm_update(EPM_POR, (CPU_ID - 1));  //SAVE ALL EPM data
}

ddr_code void rdisk_reset_shutdown_state()
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	if(epm_ftl_data->POR_tag == FTL_EPM_POR_TAG)
	{
		epm_ftl_data->POR_tag = 0;
		disp_apl_trace(LOG_INFO, 0x2a7c, "Reset POR Tag");
	}
}

slow_code void flush_pln_done(ftl_core_ctx_t *ctx)
{
	disp_apl_trace(LOG_INFO, 0x81c2, "pln fctx:0x%x done", ctx);
	sys_free(FAST_DATA, ctx);

    if(plp_trigger)
    {
        ipc_plp_flush_done(NULL);
    }
}

fast_code void pln_flush(void)
{
	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));
	sys_assert(fctx);
	fctx->ctx.caller = fctx;
	fctx->ctx.cmpl = flush_pln_done;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->flags.b.shutdown = 1;
    ucache_flush(fctx);
    //void *ptr = (void *) fctx;
    //cpu_msg_issue(CPU_BE - 1, CPU_MSG_PLN_TRIGGER, 0, (u32)fctx);
	disp_apl_trace(LOG_INFO, 0x73f4, "[IN]pln_flush");

}

fast_code void rdisk_flush(req_t *req)
{
	if (req->op_fields.flush.issued)
		return;

	ftl_flush_data_t *fctx = &_flush_ctx;
	sys_assert(fctx->nsid == 0); /// nsid must be zero

	btn_de_wr_disable();
	fctx->ctx.caller = req;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->ctx.cmpl = rdisk_flush_done;

	if (req->op_fields.flush.shutdown)
	{
		fctx->flags.b.shutdown = 1;
		fctx->ctx.cmpl = rdisk_shutdown;

		/*
		** 1. host shutdown, disable bg to keep ns clean
		** 2. ftl flush fsm will suspend gc, so only disable bg here
		*/
		if (req->req_from != REQ_Q_BG)
			bg_disable();
	}
    #if(BG_TRIM == ENABLE)
    SetBgTrimSuspend(true);
    #endif

    req->op_fields.flush.issued = true;

	ucache_flush(fctx);
}

fast_code void rdisk_reset_flush_done(ftl_core_ctx_t *ctx)
{
	ftl_flush_data_t *fctx = (ftl_flush_data_t *) ctx;

	fctx->nsid = 0;
}

ftl_flush_data_t rst_fctx;
fast_code void rdisk_reset_flush(void)
{
	ftl_flush_data_t *fctx = &rst_fctx;
	//bool ret;
	sys_assert(fctx->nsid == 0);	/// nsid must be zero

	fctx->ctx.caller = NULL;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->ctx.cmpl = rdisk_reset_flush_done;

	ucache_flush(fctx);
}

//fast_code bool rdisk_is_ftl_reset_acked(void)
ddr_code bool rdisk_is_ftl_reset_acked(void)
{
	// TODO need to use sync ??
	//cpu_msg_sync_start();
	smResetHandle.abortMedia = true;
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_PERST_FTL_CHK_ACK, 0, 0);

	//extern cpu_msg_t cpu_msg;
	//while ((!cpu_msg.sync_done[CPU_MSG_TX_CPU_IDX])&&(plp_trigger == 0))
	while (smResetHandle.abortMedia && (plp_trigger == 0))
	{
		#if defined(RDISK)
		sw_ipc_poll();
		#endif
		cpu_msg_isr();
		// OTHER TYPE?
		btn_feed_rd_dtag();
	}

	return true;
}

ddr_code void rdisk_reset_disk_handler(void)
{
	// rdisk_fe_flags.b.pcie_reset = true;
	// cancel pending cmd
	// TODO NO other type cmd?
	while (!bcmd_list_empty(&bcmd_pending))
	{
		bcmd_list_pop_head(&bcmd_pending);
	}

	// rx cmd
	rxcmd_abort();

	// RW CMD
	btn_abort_all();

	// TODO admin cmds


	// notify FTL gc
	ftl_reset_notify();

	// streaming read suspend
	ftl_core_reset_notify();

	// wait FTL idle
	rdisk_is_ftl_reset_acked();

	smStopPushDdrDtag = true;
}

ddr_code void rdisk_reset_l2p_handler(void)
{
	l2p_idle_ctrl_t ctrl;
	ctrl.all = 0;
	ctrl.b.all = 1;
	ctrl.b.wait = 1;
	l2p_mgr_reset();
	wait_l2p_idle(ctrl);
	rtos_lock_other_cpu(true, true);
	cpu_msg_isr();
extern u16 dpe_ctxs_allocated_cnt;
    sys_assert(!dpe_ctxs_allocated_cnt);
}

ddr_code void rdisk_reset_resume_handler(void)
{
	l2p_mgr_reset_resume();
	//
	rtos_unlock_other_cpu();
	//
	// resume FTL
	ftl_core_reset_done_notify();
	ftl_reset_done_notify();

	smResetSemiDtag = true;
	cpu_msg_issue((CPU_DTAG - 1), CPU_MSG_RESUME_SEMI_DDR_DTAG, 0, 1);
	while (true)
	{
		if (smResetSemiDtag == false)
		{
			break;
		}
	}
	//
	rdisk_alloc_streaming_rw_dtags();
	// rdisk_fe_flags.b.pcie_reset = false;
	//ucache_resume();
	ucache_reset_resume();
	#if(BG_TRIM == ENABLE)
	SetBgTrimSuspend(false);
	#endif

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_resume();
	#endif

	otf_fua_cmd_cnt = 0;
}

fast_code void rdisk_trigger_gc_resume(void)
{
	ftl_reset_done_notify();
}

static CacheFlushStatus_t Cache_FlushIssue(void);
static CacheFlushStatus_t Cache_FlushWait(void);
static CacheFlushStatus_t Cache_FlushPolling(void);
static CacheFlushStatus_t Cache_FlushDone(void);
static CacheFlushStatus_t Cache_FlushRelease(void);

static CacheFlushStatus_t (*gProcCacheFlush[CACHE_FLUSH_COUNT])(void) =
{
    Cache_FlushIssue,
    Cache_FlushWait,
    Cache_FlushPolling,
    Cache_FlushDone,
    Cache_FlushRelease,
};

fast_data u32 gCacheRstState = 0;

fast_code void rdisk_forcefillup(void)
{
	if (ucache_reader_check() == false)
	{
		extern void btn_de_rd_grp0(void);
		extern void l2p_isr_q0_srch(void);

		btn_de_rd_grp0();
		l2p_isr_q0_srch();
		cpu_msg_isr();
		dpe_isr();
	}
}

fast_code void rdisk_reset_cache(void)
{
	CacheFlushStatus_t ret;

	do {
		ret = gProcCacheFlush[gCacheRstState]();

		if (ret != CACHE_RET_CONTINUE)
		{
			if (ret == CACHE_RET_ESCAPE)
			{
				gCacheRstState = CACHE_FLUSH_ISSUE;
				break;
			}
		}

	} while(true);
}

static CacheFlushStatus_t Cache_FlushIssue(void)
{
	#if(BG_TRIM == ENABLE)
	SetBgTrimSuspend(true);
	#endif
	if (ucache_clean())
	{
		gCacheRstState = CACHE_FLUSH_DONE;
		return CACHE_RET_CONTINUE;
	}
	else
	{
		rdisk_forcefillup();
		rdisk_reset_flush();
		gCacheRstState = CACHE_FLUSH_WAIT;
		return CACHE_RET_CONTINUE;
	}
}

static CacheFlushStatus_t Cache_FlushWait(void)
{
	// Means it has job to wait flush
	if (flush_que_chk())
	{
		extern void l2p_isr_q0_srch(void);
		extern void bm_isr_slow(void);
		extern void btn_data_in_isr(void);
		bm_isr_slow();
		btn_data_in_isr();
		l2p_isr_q0_srch();
		cpu_msg_isr();
		extern bool evt_task_process_one(void);
		evt_task_process_one();
		gCacheRstState = CACHE_FLUSH_WAIT;
		return CACHE_RET_CONTINUE;
	}
	else
	{
		gCacheRstState = CACHE_FLUSH_POLLING;
		return CACHE_RET_CONTINUE;
	}
}

static CacheFlushStatus_t Cache_FlushPolling(void)
{
	while (rst_fctx.nsid != 0)
	{
		#if defined(RDISK)
		sw_ipc_poll();
		#endif
		cpu_msg_isr();

		if (rst_fctx.nsid == 0)
		{
			break;
		}
	}

	gCacheRstState = CACHE_FLUSH_DONE;
	return CACHE_RET_CONTINUE;
}

static CacheFlushStatus_t Cache_FlushDone(void)
{

	wde_parser_reset();
	_wde_parser.state = WDE_PARSER_IDLE;

	if (ucache_clean())
	{
		gCacheRstState = CACHE_FLUSH_RELEASE;
		return CACHE_RET_CONTINUE;
	}
	else
	{
		gCacheRstState = CACHE_FLUSH_ISSUE;
		return CACHE_RET_CONTINUE;
	}
}

static CacheFlushStatus_t Cache_FlushRelease(void)
{

	// memset(&_wde_parser, 0, sizeof(wde_parser_t));
	ucache_free_unused_dtag();
	return CACHE_RET_ESCAPE;
}

fast_code void rdisk_power_loss_flush_done(ftl_core_ctx_t *fctx)
{
//	u32 t = (jiffies - flush_time) * (1000 / HZ);

	//disp_apl_trace(LOG_ERR, 0, "\n\033[91m%s\x1b[0m ms:%d\n", __func__, t);

	sys_free(FAST_DATA, fctx);
}

fast_code void plp_wait_streaming_finish_evt(u32 r0, u32 r1, u32 r2)
{
	if (cpu2_streaming_read_cnt || cpu4_streaming_read_cnt) {
		/* if there are any read errors, we need to return to cpu1 main loop,
		   otherwise streaming cnt will never decrese. */
		btn_feed_rd_dtag();
	    urg_evt_set(evt_check_streaming, 0, 0);
		return;
	}

	//u64 step2 = get_tsc_64();
	//disp_apl_trace(LOG_ALW, 0, "wait streaming done:0x%x-%x", step2>>32, step2&0xFFFFFFFF);

	CPU1_plp_step = 5;
#if (TRIM_SUPPORT == ENABLE)
	TrimPowerLost(true);
#endif
    ucache_flush_flag = true;
    if(PLN_in_low&&PLN_flush_cache_end)
    {
        disp_apl_trace(LOG_ALW, 0x62dd, " pln+plp");
    }
    else if(host_dummy_start_wl != INV_U16 && shr_dtag_comt.que.rptr == shr_dtag_comt.que.wptr)
    {
    	//shutdown running , cache empty , no need wait shutdown fill host dummy.
    	//disp_apl_trace(LOG_ALW, 0x0885,"[Jay] plp no call ucache flush when shutdown running");
		ipc_plp_flush_done(NULL);
    }
    else
    {
    	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));
    	sys_assert(fctx);
    	fctx->ctx.caller = NULL;
    	fctx->ctx.cmpl = ipc_plp_flush_done;
    	fctx->nsid = 1;
    	fctx->flags.all = 0;
        plp_cache_tick = get_tsc_64();
    	ucache_flush(fctx);
    }
	return;
}



fast_code void plp_evt(u32 r0, u32 r1, u32 r2)
{
	/*
	 * stop host xfer here, otherwise ncl will not done if there are pending host read.
	 * fake plp need double comfirm. TODO add resume xfer after plp finish.
	 */
	CPU1_plp_step = 3;
    nvmet_update_smart_stat(NULL);
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_disable_time(20); //2s
	#endif
	u64 start = get_tsc_64();
	u8  tick = 0;
	while (cpu2_cancel_streaming || cpu4_cancel_streaming)
	{
		dpe_isr();  	// for PBT raid bm_copy_done
        cpu_msg_isr();  // for PBT raid bm_data_copy
		if((get_tsc_64() - start)>=30*CYCLE_PER_MS)
		{
			//scheduler cancel die que may error!!!
			cpu_msg_show(CPU_BE-1);
			cpu_msg_show(CPU_BE_LITE-1);
			start += 20*CYCLE_PER_MS;
			tick++;
			if(tick >= 2)
				break;//force break!!
		}
	}
	CPU1_plp_step = 4;
	plp_wait_streaming_finish_evt(0, 0, 0);
}


share_data_zi bool reseting;

fast_code void rdisk_power_loss_flush(void)
{
	u64 step1 = get_tsc_64();
	CPU1_plp_step = 1;
	disp_apl_trace(LOG_ALW, 0x7aa6, "start:0x%x-%x, cpu2:%d, cpu4:%d", step1>>32, step1&0xFFFFFFFF, cpu2_streaming_read_cnt, cpu4_streaming_read_cnt);

	cpu2_cancel_streaming = cpu4_cancel_streaming = 1;
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_SUSPEND_GC, 0, 0);
	plp_cancel_die_que();

    nvmet_io_fetch_ctrl(true);
	btn_de_wr_disable();
    hal_nvmet_abort_xfer(true);
    CPU1_plp_step = 2;
    log_level_chg(LOG_ERR);
    urg_evt_set(evt_plp_flush, 0, 0);
}

/*!
 * @brief interface to start force flush, before flush, host write is not enable
 *
 * @param req: not used, please input NULL
 *
 * @return	not used
 */
ftl_flush_data_t plp_fctx;
fast_code void plp_force_flush(void)
{
	/*disp_apl_trace(LOG_INFO, 0, "ipc force flush start");

	btn_de_wr_disable(); //disable host wr data entry

	ftl_flush_data_t *fctx = &plp_fctx; //sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));

	plp_trigger = 0xEE;

	fctx->ctx.caller = NULL;
	fctx->ctx.cmpl = ipc_plp_flush_done;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	//fctx->flags.b.dtag_gc = 1;
	fctx->flags.b.plp = 1;
	//disp_apl_trace(LOG_INFO, 0, "fctx:%x cmpl:%x", fctx, fctx->ctx.cmpl);
	ucache_flush(fctx);*/
}

//fast_code void io_mgr_cmd_exec(req_t *req, btn_cmd_t *bcmd)
ddr_code void io_mgr_cmd_exec(req_t *req, btn_cmd_t *bcmd)
{
	btn_io_mgr_cmd_t *io_bcmd = (btn_io_mgr_cmd_t *)bcmd;

	sys_assert(req->fe.nvme.cid == io_bcmd->dw2.b.cmd_cid);
	switch (req->opcode)
	{
	case REQ_T_FLUSH:
		req->op_fields.flush.btag = bcmd2btag(bcmd, 1);
		rdisk_flush(req);
		break;
	default:
		panic("not support");
		break;
	}
}

fast_code static void rdisk_fe_info_flush_done(fe_req_t *fe_req)
{
	req_t *req = (req_t *)fe_req->caller;
	extern bool set_hctm_flag;

	rdisk_fe_flags.b.info_flushing = 0;
	set_hctm_flag = 0;

	disp_apl_trace(LOG_INFO, 0x7cc8, "fe info flushed %x(%d)", req, req->req_id);
	sys_free(FAST_DATA, fe_req);

	if (req)
	{
		list_del(&req->entry2);
		req->completion(req);

		btn_de_wr_enable();
		bcmd_resume();
		req_resume();
	}

	if (rdisk_fe_flags.b.need_flush_info)
	{
		//req_t *req = nvme_save_req; // Curry

		rdisk_fe_flags.b.need_flush_info = 0;
		nvme_save_req = NULL;
		//nvme_info_save(req);
	}
}

fast_code void nvme_info_save(req_t *req)
{
	fe_info_t *info = (fe_info_t *)shr_fe_info;
	fe_req_t *fe_req;

	if (rdisk_fe_flags.b.info_flushing)
	{
		sys_assert(nvme_save_req == NULL);
		nvme_save_req = req;
		rdisk_fe_flags.b.need_flush_info = 1;
		return;
	}

	rdisk_fe_flags.b.info_flushing = 1;

	fe_req = sys_malloc(FAST_DATA, sizeof(fe_req_t));
	sys_assert(fe_req);
	info->sig = FE_INFO_SIG;
	info->version = FE_INFO_VER;

	nvmet_update_feat(&info->nvme_feat);
	nvmet_update_smart_stat(&info->smart);
	ftl_stat_update(&info->ftl_stat);

	fe_req->caller = req;
	fe_req->cmpl = rdisk_fe_info_flush_done;
	fe_req->write = true;
	fe_req->type = FE_REQ_TYPE_LOG;
	fe_req->cmpl(fe_req); //Curry
						  //fe_log_flush(fe_req);
}

fast_code bool rdisk_check_wr_cmd_idle(void)
{
	if (rdisk_fe_flags.b.io_fetch_stop == 0)
	{
#if CO_SUPPORT_SANITIZE
		extern epm_info_t *shr_epm_info;
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates != sSanitize_FW_InProgress)
#endif
			nvmet_io_fetch_ctrl(true);
		rdisk_fe_flags.b.io_fetch_stop = 1;
	}
	return btn_wr_cmd_idle();
}

ddr_code void rdisk_wait_wcmd_idle(u32 p0, u32 p1, u32 p2)
{
	if (rdisk_check_wr_cmd_idle())
		req_resume();
	else
		evt_set_cs(evt_wait_wcmd_idle, 0, 0, CS_TASK);
}

fast_code void rdisk_req_exec_chk(u32 p0, u32 p1, u32 p2)
{
	if (shr_ftl_flags.b.ftl_ready && shr_ftl_flags.b.l2p_all_ready)
		req_resume();
	else
		evt_set_cs(evt_req_exec_chk, 0, 0, CS_TASK);
}

#if (CO_SUPPORT_READ_AHEAD == TRUE)
fast_code void rdisk_otf_readcmd_chk(u32 p0, u32 p1, u32 p2)
{
	if (shr_ftl_flags.b.ftl_ready)
		bcmd_resume();
	else
		evt_set_cs(evt_otf_readcmd_chk, 0, 0, CS_TASK);
}
#endif

#if 1  //3.1.7.4 merged 20201201 Eddie
slow_code_ex void fwdl_op_done(fwdl_req_t *fwdl_req)
{
	req_t *_req = (req_t *) fwdl_req->ctx;
	fwdl_req_op_t op = fwdl_req->op;
	u32 status = fwdl_req->status;

	sys_free(SLOW_DATA, fwdl_req);

	if (op == FWDL_DOWNLOAD) {
		status = 0;
		bm_wait_dpe_data_process_done();
	} else if (op == FWDL_COMMIT)
	{

	}
	dwnld_com_lock = false;
	evt_set_imt(evt_cmd_done, (u32)_req, status);
}
#else
fast_code void fwdl_op_done(fwdl_req_t *fwdl_req)
{
	req_t *_req = (req_t *)fwdl_req->ctx;
	fwdl_req_op_t op = fwdl_req->op;
	u32 status = fwdl_req->status;

	sys_free(SLOW_DATA, fwdl_req);

	if (op == FWDL_DOWNLOAD)
	{
		status = 0;
		bm_wait_dpe_data_process_done();
		dtag_put_bulk(DTAG_T_SRAM, _req->req_prp.mem_sz, (dtag_t *)_req->req_prp.mem);
		sys_free(SLOW_DATA, _req->req_prp.mem);
		sys_free(SLOW_DATA, _req->req_prp.prp);
	}
	else if (op == FWDL_COMMIT)
	{
	}
	evt_set_imt(evt_cmd_done, (u32)_req, status);
}
#endif
fast_code void ipc_fwdl_op_done(volatile cpu_msg_req_t *req)
{
	fwdl_req_t *fwdl_req = (fwdl_req_t *)req->pl;

	fwdl_op_done(fwdl_req);
}
//extern volatile u8 fw_update_flag;
fast_code void rdisk_fw_download(u32 p0, u32 _req, u32 not_used)
{
	req_t *req = (req_t *)_req;
	dtag_t *dtags = (dtag_t *)req->req_prp.mem;
	u32 count = 0;
	fwdl_req_t *fwdl_req = sys_malloc(SLOW_DATA, sizeof(fwdl_req_t));
	sys_assert(fwdl_req);

	if(plp_trigger)
	{
		return ;
	}

	if(req->req_prp.fg_prp_list == true)	//20201222-Eddie
		count = req->req_prp.sec_got_dtag_cnts;
	else
		count = req->req_prp.mem_sz;

	fwdl_req->op = FWDL_DOWNLOAD;
	fwdl_req->ctx = (void *)req;
	fwdl_req->field.download.count = count;
	fwdl_req->field.download.dtags = dtags;
	//fw_update_flag = 1;

	bool ret = fwdl_download(fwdl_req);
	//fw_update_flag = 0;

	if (ret == true){		//20201222-Eddie
		if(req->req_prp.fg_prp_list == true){//prp list case
		#if 1
			if (req->req_prp.transferred < req->req_prp.nprp){
				evlog_printk(LOG_ALW, "TxFER<NRPR");
				}	//do nothing until nprp counts == sec idx counts
			else
				fwdl_op_done(fwdl_req);
		#endif
		}
		else
		fwdl_op_done(fwdl_req);
}
}

fast_code void rdisk_fw_commit(u32 p0, u32 fs_ca, u32 _req)
{
	u32 fs = fs_ca >> 16;
	u32 ca = fs_ca & 0xFFFF;
	bool ret;
	req_t *req = (req_t *)_req;
	fwdl_req_t *fwdl_req = sys_malloc(SLOW_DATA, sizeof(fwdl_req_t));
	sys_assert(fwdl_req);

	fwdl_req->field.commit.ca = ca;
	fwdl_req->field.commit.slot = fs;
	fwdl_req->ctx = (void *)req;
	fwdl_req->op = FWDL_COMMIT;

	ret = fwdl_commit(fwdl_req);
	if (ret == true)
		fwdl_op_done(fwdl_req);
}

static inline bool req_pend_chk(req_t *req)
{
	u32 op = req->opcode;

	if ((op != REQ_T_FORMAT) && (op != REQ_T_FLUSH) && (op != REQ_T_TRIM))
		return false;

	if (shr_ftl_flags.b.ftl_ready && shr_ftl_flags.b.l2p_all_ready)
		return false;

	return true;
}
extern wunc_t WUNC_lda2ce;
fast_code void rdisk_wunc_next_part(u32 p0, u32 p1, u32 p2)
{
    if(plp_trigger)
        return;
    req_t *req = list_first_entry(&_req_pending, req_t, entry2);
    sys_assert(req->opcode == REQ_T_WUNC);
    sys_assert(req->op_fields.wucc.left == 0);
    rdisk_wunc(req);
}

#ifdef NS_MANAGE
//#if 0
ddr_code void rdisk_wunc(req_t *req)//joe 20201109  fast-->slow
{
	u64 slba = req->lba.srage.slba;
	u32 nlb = req->lba.srage.nlb;
	if(req->lba.srage.nlb == 0){
		nlb = INV_U16 + 1;
	}
	dtag_t dtag = {.dtag = EVTAG_ID};
    r_par_t par = {.all = 0};
	//dtag_t dtag = {.dtag = EVTAG_ID};
	u8 lba_num = 0;
	if (host_sec_bitz == 9) //joe add sec size 20200818
		lba_num = 8;
	else
		lba_num = 1;
	u8 lba_mask = lba_num - 1;
    u8 loops = 0;
#if 0
	u16 btag = wunc_btag;
#else
	u16 btag = WUNC_BTAG_TAG|wunc_btag;//Q depth not over 0x7FFF
#endif


	u32 body = 0;
    //u32 D = 0xDDDD;
	again:
	body = calc_du_cnt(slba, nlb);
	if (body > WUNC_MAX_CACHE) {
		nlb = (lba_num - (slba & lba_mask) + (WUNC_MAX_CACHE - 1)*lba_num);
		req->lba.srage.slba = slba + nlb;
		//sys_assert(req->lba.srage.nlb != 0);
		goto again;
	}
	if(req->lba.srage.nlb == 0){
		req->lba.srage.nlb =0x10000 - nlb;
	}
	else{
		req->lba.srage.nlb -= nlb;
	}
	req->op_fields.wucc.left = body;
	cache_handle_dtag_cnt += body;
	req->op_fields.wucc.issued = true;
	otf_fua_cmd_cnt++;
    u8 NS_SIZE_GRANULARITY_BITOP = 0;
    if(host_sec_bitz==9){
        NS_SIZE_GRANULARITY_BITOP = NS_SIZE_GRANULARITY_BITOP1;
    }else{
        NS_SIZE_GRANULARITY_BITOP = NS_SIZE_GRANULARITY_BITOP2;
    }
    u64 ns_slba[2];
    ns_slba[0] = slba;
    ns_slba[1] = slba + nlb - 1;//joe test cal cross sec 20201202  1cause +1 at nvmet_setup_write_unc  IOL2.5
	u16 secid[2];
	secid[0] = ns_slba[0] >> NS_SIZE_GRANULARITY_BITOP;
	secid[1] = ns_slba[1] >> NS_SIZE_GRANULARITY_BITOP;//joe T0 is xfer_lba_num
    u16 lbacnt[2];
    lbacnt[0] = nlb;
    lbacnt[1] = 0;
	u8 cross_flag_write = 0; //joe add 20200917 write performance ns
	u64 slba_last = 0; //joe 20200525
	disp_apl_trace(LOG_INFO, 0xca2d, "JOE:wunc,req:0x%x,req->nsid:%d, slba:0x%x-%x, nlb:%d",req,req->nsid,(u32)(slba>>32),(u32)(slba),nlb);
	disp_apl_trace(LOG_INFO, 0x2273, "JOE: secid:%d  secid2:%d  slba:0x%x  slba2:0x%x nlb:%d", secid[0],secid[1], ns_slba[0],ns_slba[1],nlb);
	if(secid[0] != secid[1]){
		// printk("different sec write\n");
		cross_flag_write = 1;
		lbacnt[0] = (((u64)secid[1]) << NS_SIZE_GRANULARITY_BITOP) - ns_slba[0];

	}
	slba_last = ns_slba[0] - (((u64)secid[0]) << NS_SIZE_GRANULARITY_BITOP);
	ns_slba[0] = slba_last + (((u64)ns_array_menu->ns_array[req->nsid-1].sec_id[secid[0]]) << NS_SIZE_GRANULARITY_BITOP);
	if(cross_flag_write)
		ns_slba[1] = ((u64)ns_array_menu->ns_array[req->nsid-1].sec_id[secid[1]]) << NS_SIZE_GRANULARITY_BITOP;
		//disp_apl_trace(LOG_DEBUG, 0, "JOE: transfer slba:  H: 0x%x  L:0x%x   slba_last:0x%x",(slba>>32),slba,slba_last);
		//slba =(ns_array_menu->ns_array[bcmd->dw1.b.ns_id - 1].sec_id[secid1])*NS_SIZE_GRANULARITY_RDISK1;
	lbacnt[1] = nlb - lbacnt[0];
	//sec1
	u32 lda,lda_tail,head,tail = 0;
	for(loops = 0;loops <= cross_flag_write; loops++){
        sys_assert(loops <= 1);
	    lda = ((ns_slba[loops]) >> (LDA_SIZE_SHIFT - host_sec_bitz));
	    lda_tail = ((ns_slba[loops] + lbacnt[loops]) >> (LDA_SIZE_SHIFT - host_sec_bitz));
	    head = ns_slba[loops] & lba_mask;
	    tail = (ns_slba[loops] + lbacnt[loops]) & lba_mask;
    	if (head && tail && (lda == lda_tail)) //joe add sec size 20200820
    		tail = 0;
    	body = calc_du_cnt(ns_slba[loops], lbacnt[loops]);
    	//req->op_fields.wucc.left = body1;
    	#if 0//(TRIM_SUPPORT == ENABLE)
    	if(IsCMDinTrimTable(lda,lda+(body-1),1))
    	{
        	RegOrUnregTrimTable(lda,body,Unregister);
        	UpdtTrimInfo(lda,body,Unregister);
    	}
        #endif
    	if (tail)
    		body--;
        if(loops == 0){
            WUNC_lda2ce.startlda0 = lda;
            WUNC_lda2ce.cross_cnt = 0;
            WUNC_lda2ce.endlda0 = ((ns_slba[loops] + lbacnt[loops] - 1) >> (LDA_SIZE_SHIFT - host_sec_bitz));
            WUNC_lda2ce.startlda1 = INV_U32;
            WUNC_lda2ce.endlda1 = INV_U32;

        }else{
            WUNC_lda2ce.cross_cnt = WUNC_lda2ce.endlda0 - WUNC_lda2ce.startlda0 + 1;
            WUNC_lda2ce.startlda1 = lda;
            WUNC_lda2ce.endlda1 = ((ns_slba[loops] + lbacnt[loops] - 1) >> (LDA_SIZE_SHIFT - host_sec_bitz));
        }
    	if (head)
    	{
    		u32 cnt = min(lba_num - head, lbacnt[loops]);
    		par.ofst = head;
            par.nlba = cnt;
    		wunc_ua[0] = lda;
    		wunc_bmp[0] = (1 << cnt) - 1;
    		wunc_bmp[0] = wunc_bmp[0] << head;
    		//cache_handle_dtag_cnt++;
    		ucache_nrm_par_data_in(&lda, &dtag, &btag, &par, 1);
    		lbacnt[loops] -= cnt;
    		lda++;
    		body--;
    	}

        par.all = 0;
    	for (; lda < lda_tail; lda++)
    	{
    		ucache_nrm_par_data_in(&lda, &dtag, &btag, &par, 1);
    	}
        lbacnt[loops] -= lba_num*body;

    	if (tail)
    	{
    		wunc_ua[1] = lda;
    		wunc_bmp[1] = (1 << tail) - 1;
    		par.ofst = 0;
            par.nlba = lbacnt[loops];
    		ucache_nrm_par_data_in(&lda, &dtag, &btag, &par, 1);
    		lbacnt[loops] -= tail;
    	}

	    sys_assert(lbacnt[loops] == 0);
	}
	shr_wunc_meta_flag = true;
}
#else
ddr_code void rdisk_wunc(req_t *req)
{
	u64 slba = req->lba.srage.slba;
	u32 nlb = req->lba.srage.nlb;
    if(req->lba.srage.nlb == 0){
        nlb = INV_U16 + 1;
    }
	//lda_t lda = LBA_TO_LDA(slba);
	lda_t lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz));
	lda_t lda_tail = ((slba + nlb) >> (LDA_SIZE_SHIFT - host_sec_bitz));
	u32 head;
	u32 tail;
	u32 body;
	u32 i;
	dtag_t dtag = {.dtag = EVTAG_ID};
	u8 lba_num = 0;
    #if 0
    u16 btag = wunc_btag;
    #else
    u16 btag = WUNC_BTAG_TAG|wunc_btag;//Q depth not over 0x7FFF
    #endif
	if (host_sec_bitz == 9) //joe add sec size 20200818
		lba_num = 8;
	else
		lba_num = 1;
    u8 lba_mask = lba_num - 1;
again:

	head = slba & lba_mask;
	tail = (slba + nlb) & lba_mask;
	if (head && tail && (lda == lda_tail)) //joe add sec size 20200820
		tail = 0;
	body = calc_du_cnt(slba, nlb);
    if (body > WUNC_MAX_CACHE) {
        req->lba.srage.slba = (lda + WUNC_MAX_CACHE) * lba_num ;
        if (head)
            nlb = (lba_num - head + (WUNC_MAX_CACHE - 1)*lba_num);
        else
            nlb = WUNC_MAX_CACHE * lba_num;
        //sys_assert(req->lba.srage.nlb != 0);
        goto again;
    }
    if(req->lba.srage.nlb == 0){
        req->lba.srage.nlb =0x10000 - nlb;
    }
    else{
        req->lba.srage.nlb -= nlb;
    }
	req->op_fields.wucc.left = body;
    WUNC_lda2ce.startlda0 = lda;
    WUNC_lda2ce.cross_cnt = 0;
    WUNC_lda2ce.endlda0 = lda + body - 1;
    WUNC_lda2ce.startlda1 = INV_U32;
    WUNC_lda2ce.endlda1 = INV_U32;
    #if 0// (TRIM_SUPPORT == ENABLE)
    if(IsCMDinTrimTable(lda,lda+(body-1),1))
    {
        RegOrUnregTrimTable(lda,body,Unregister);
        UpdtTrimInfo(lda,body,Unregister);
    }
    #endif
	if (tail)
		body--;
	req->op_fields.wucc.issued = true;
    otf_fua_cmd_cnt++;
    disp_apl_trace(LOG_INFO, 0xf792, "WUNC CMD Handle req:%x,slba:%x%x, nlba:%d,body:%d", req,(u32)(slba>>32),(u32)(slba),nlb,body);
	cache_handle_dtag_cnt += body;

	if (head)
	{
		u32 cnt = min(lba_num - head, nlb);

		r_par_t par = {.ofst = head,.nlba = cnt,};
		wunc_ua[0] = lda;
		wunc_bmp[0] = (1 << cnt) - 1;
		wunc_bmp[0] = wunc_bmp[0] << head;
		//cache_handle_dtag_cnt++;
		ucache_nrm_par_data_in(&lda, &dtag, &btag, &par, 1);
		nlb -= cnt;
		lda++;
		body--;
	}

	for (i = 0; i < body; i++)
	{
		r_par_t par = {.all = 0};
		ucache_nrm_par_data_in(&lda, &dtag, &btag, &par, 1);
		nlb -= lba_num;
		lda += 1;
	}

	if (tail)
	{
		wunc_ua[1] = lda;
		wunc_bmp[1] = (1 << tail) - 1;
		r_par_t par = {.ofst = 0,.nlba = nlb,};
        cache_handle_dtag_cnt++;
		ucache_nrm_par_data_in(&lda, &dtag, &btag, &par, 1);
		nlb -= tail;
	}
	sys_assert(nlb == 0);
    shr_wunc_meta_flag = true;
}
#endif
/*!
 * @brief rdisk request execution, flush and format function.
 *
 * req_exec is the core routine which execution commands excluding read, write
 * and format operation.
 *
 * @param[in] req	request to be executed.
 *
 * @return		not used
 */
fast_code bool req_exec(req_t *req)
{
	if (req->req_id != 0xFF) // dont add internal req
		req_statistics.req_rcv_ttl++;

	if (!list_empty(&_req_pending))
	{
		sys_assert(req->req_id != 0xFF);
		list_add_tail(&req->entry2, &_req_pending);
		return true;
	}

	if (req_pend_chk(req))
	{
		list_add_tail(&req->entry2, &_req_pending);
		evt_set_cs(evt_req_exec_chk, 0, 0, CS_TASK);
		return true;
	}

	switch (req->opcode)
	{
	case REQ_T_READ:
	case REQ_T_WRITE:
	case REQ_T_COMPARE:
		panic("not support");
		break;
	case REQ_T_FORMAT:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable();
		#endif
		list_add_tail(&req->entry2, &_req_pending);
		if (rdisk_check_wr_cmd_idle())
			rdisk_format(req);
		else
			evt_set_cs(evt_wait_wcmd_idle, 0, 0, CS_TASK);
		break;
	case REQ_T_FLUSH:
		req->op_fields.flush.issued = false;
        list_add_tail(&req->entry2, &_req_pending);
		rdisk_flush(req);
		break;
	case REQ_T_TRIM:
        #if(TRIM_SUPPORT == ENABLE)
		 while(!CBF_EMPTY(&bg_trim_free_dtag)){
	        dtag_t dtag = bg_trim_free_dtag.idx[bg_trim_free_dtag.rptr];
	        put_trim_dtag(dtag);
	        bg_trim_free_dtag.rptr = (bg_trim_free_dtag.rptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);
    	}
		if(get_trim_dtag_cnt() && ((shr_dtag_comt.que.wptr + 1 ) % shr_dtag_comt.que.size) != shr_dtag_comt.que.rptr){
		    rdisk_trim(req);
		}else{
			list_add_tail(&req->entry2, &_req_pending);
			evt_set_cs(evt_req_exec_chk, 0, 0, CS_TASK);
		}
        #else
        rdisk_trim(req);
        #endif
		break;
	case REQ_T_WZEROS:
		break;
	case REQ_T_WUNC:
		req->op_fields.wucc.issued = false;
		list_add_tail(&req->entry2, &_req_pending);
		if (rdisk_check_wr_cmd_idle())
			rdisk_wunc(req);
		else
			evt_set_cs(evt_wait_wcmd_idle, 0, 0, CS_TASK);
		break;
	default:
		sys_assert(0);
		break;
	}

	return true;
}

fast_code void req_resume(void)
{
	if (list_empty(&_req_pending))
		return;

	LIST_HEAD(local);
	list_splice(&_req_pending, &local);
	INIT_LIST_HEAD(&_req_pending);

	do
	{
		req_t *req = list_first_entry(&local, req_t, entry2);

		list_del(&req->entry2);
		req_exec(req);
	} while (!list_empty(&local));
}


ddr_code void rxcmd_abort(void)
{
	if (list_empty(&_req_pending))
	{
		return;
	}

	LIST_HEAD(local);
	list_splice(&_req_pending, &local);
	INIT_LIST_HEAD(&_req_pending);

	do
	{
		req_t *req = list_first_entry(&local, req_t, entry2);
		nvmet_abort_running_req(req);
	} while (!list_empty(&local));
}

fast_code static void rdisk_l2p_srch_unmap(u32 param0, u32 rsp, u32 cnt)
{
	l2p_srch_unmap_t *p = (l2p_srch_unmap_t *)rsp;
	u32 i;
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	bool ra_in = false;
	#endif

	for (i = 0; i < cnt; i++, p++)
	{
		#ifdef CO_SUPPORT_READ_REORDER
		if (p->ofst == SINGLE_SRCH_MARK)
			p->ofst = 0;
		#endif
		dtag_t dtag;
        #if 0
		if (p->dtag)
		{
			disp_apl_trace(LOG_ERR, 0x906a, "srch done dtag %x btag %d ofst %d", p->lda, p->btag, p->ofst);
			dtag.dtag = p->lda; /// convert rdtag to real dtag
#if (CPU_DTAG == CPU_ID)
			dtag_ref_inc_ex(dtag);
#else
			rdisk_dref_inc((u32 *)&dtag, 1);
#endif
		}
		else
        #endif
		{
			//disp_apl_trace(LOG_DEBUG, 0, "srch done unmap btag %d ofst %d", p->btag, p->ofst);
			dtag.dtag = RVTAG_ID;
		}
        //sys_assert(p->dtag == 0);
		if (p->btag != ua_btag){
			// skip data entry commit for host nomapping read
			if (plp_trigger) {
				continue;
			}

			#if (CO_SUPPORT_READ_AHEAD == TRUE)
			if (p->btag == ra_btag)
			{
				ra_in = true;
				ra_unmap_data_in(p->ofst);
			}
			else
			#endif
			{
				bm_rd_dtag_commit(p->ofst, p->btag, dtag);
			}
		}else{
			ucache_unmap_data_in(p->ofst, true, false);
		}
	}

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	if (ra_in)
	{
		ra_validate();
	}
	#endif
}
#if (CPU_DTAG == CPU_ID)
fast_code void rdisk_l2p_pda_updt_done(u32 param0, u32 entry, u32 cnt)
{
	updt_cpl_t *cpl = (updt_cpl_t *)entry;
	u32 i;

	/* to make sure hit dtag ref + before - */
	l2p_srch_unmap_poll();

	for (i = 0; i < cnt; i++, cpl++)
	{
		if (cpl->updt.fw_seq_id < TOTAL_DTAG)
		{
			ucache_pda_updt_done(cpl->updt.fw_seq_id);

			dtag_t dtag = {.dtag = (cpl->updt.fw_seq_id | DTAG_IN_DDR_MASK)};
			dtag_put_ex(dtag);
		}
	}
}
#else
fast_code void rdisk_l2p_pda_updt_done(u32 param0, u32 entry, u32 cnt)
{
	updt_cpl_t *cpl = (updt_cpl_t *)entry;
	u32 i;
	u32 dlist[32];
	u32 idx = 0;
	/* to make sure hit dtag ref + before - */
	l2p_srch_unmap_poll();

	for (i = 0; i < cnt; i++, cpl++)
	{
		if (cpl->updt.fw_seq_id < TOTAL_DTAG)
		{
			ucache_pda_updt_done(cpl->updt.fw_seq_id);

			dlist[idx] = cpl->updt.fw_seq_id;
			idx++;
			if (idx == 32)
			{
				rdisk_dref_dec(dlist, 32);
				idx = 0;
			}
		}
	}

	if (idx)
	{
		rdisk_dref_dec(dlist, idx);
	}
}
#endif

init_code static void rdisk_hw_l2p_fe_init(void)
{
	l2p_mgr_dtag_init(TOTAL_DTAG);
	l2p_mgr_init();

	evt_register(rdisk_l2p_srch_unmap, 0, &evt_l2p_srch_umap);
}

static inline void rdisk_pa_rd_updt(btn_rd_de_t *de)
{
	ucache_fr_data_in((bm_pl_t *)de, 1, false);
}

fast_code static void rdisk_rd_updt(u32 param0, u32 payload, u32 count)
{
	u32 *addr = (u32 *)payload;
	u32 i;
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	bool ra_in = false;
	#endif

	for (i = 0; i < count; i++)
	{
		btn_rd_de_t *de = dma_to_btcm(addr[i]);

		if (de->b.type == BTN_NCB_TYPE_CTRL_PREASSIGN)
		{
			#if (CO_SUPPORT_READ_AHEAD == TRUE)
			if (de->b.btag == ua_btag)
			{
				rdisk_pa_rd_updt(de);
			}
			else
			{
				ra_in = true;
				sys_assert(de->b.btag == ra_btag);
				dtag_t dtag = { .dtag = de->b.dtag };
				ra_nrm_data_in(de->b.du_ofst, dtag);
			}
			#else
			rdisk_pa_rd_updt(de);
			#endif
		}
		else if (de->b.type == BTN_NVME_TYPE_CTRL_RDONE)
		{
			dtag_t dtag = {.dtag = de->b.dtag};
			read_recoveried_done(dtag);
		}
		else
		{
			panic("stop");
		}
	}

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	if (ra_in)
	{
		ra_validate();
	}
	#endif
}

/*!
 * @brief handler of bm read error data entry
 *
 * @param bm_pl		error data entry
 *
 * @return		not used
 */
ddr_code void rdisk_bm_read_error(bm_pl_t *bm_pl)
{
	dtag_t dtag;

	dtag.dtag = bm_pl->pl.dtag;

	if (bm_pl->pl.btag < BTN_CMD_CNT)
	{
		bm_free_aurl_return(&dtag, 1);
	}
	else if (bm_pl->pl.btag == ua_btag)
	{
		disp_apl_trace(LOG_ERR, 0x7d4d, "ucache pa dtag %x", dtag.dtag);
	}
	// else if (bm_pl->pl.btag == ra_btag)
	// {
	// 	disp_apl_trace(LOG_INFO, 0, "ra pa dtag %x", dtag.dtag);
	// }
	else
	{
		panic("impossible");
	}
}

/*!
 * @brief handle btn write error data entries in rdisk
 *
 * @param bm_pl		error btn write payload
 *
 * @return		not used
 */
fast_code void rdisk_err_wde_handle(bm_pl_t *bm_pl)
{
	u32 dtag = bm_pl->pl.dtag;

	sys_assert(bm_pl->pl.type_ctrl == 0);

	rdisk_dref_dec(&dtag, 1);
}

ddr_code static void rdisk_pmu_shutdown_done(ftl_core_ctx_t *ctx)
{
	enum sleep_mode_t mode;

	btn_de_wr_enable();
	disp_apl_trace(LOG_ERR, 0x59db, "rdisk pmu shutdown %x\n", ctx);
	sys_free(FAST_DATA, ctx);
	if (cur_power_state == 4)
		mode = SLEEP_MODE_PS4;
	else if (cur_power_state == 3)
		mode = SLEEP_MODE_PS3_L12;
	else
		return;

	sys_sleep(mode);
}

ddr_code static void rdisk_pmu_shutdown(void)
{
	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));
	;
	sys_assert(fctx);

	btn_de_wr_disable();
	fctx->ctx.caller = NULL;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->flags.b.shutdown = 1;
	fctx->ctx.cmpl = rdisk_pmu_shutdown_done;
    #if(BG_TRIM == ENABLE)
    SetBgTrimSuspend(true);
    #endif
	ucache_flush(fctx);
}

UNUSED fast_code static void rdisk_power_state_change(u32 param, u32 ps, u32 count)
{
	cur_power_state = ps;

	if (ps < 3)
		sys_sleep_cancel();

	if (0 == ps)
	{
		//ncl_set_ncb_clk(BIT0, 533); ///< BIT0 for CLK_TYPE_ECC
	}
	else if (1 == ps)
	{
		//ncl_set_ncb_clk(BIT0, 200);
	}
	else if (2 == ps)
	{
		//ncl_set_ncb_clk(BIT0, 100);
	}
	else if (3 == ps || 4 == ps)
	{
		rdisk_pmu_shutdown();
	}
	disp_apl_trace(LOG_WARNING, 0x9a17, "change power state [%d] complete", ps);
	return;
}

/*!
 * @brief rdisk suspned function
 *
 * @param mode	sleep mode
 *
 * @return		always true
 */
ps_code bool rdisk_suspend(enum sleep_mode_t mode)
{
	ucache_suspend();
	return true;
}

/*!
 * @brief rdisk allocate streaming read/write
 *
 *
 * @return		None
 */
fast_code void rdisk_alloc_streaming_rw_dtags(void)
{
#if defined(SEMI_WRITE_ENABLE)
	dtag_t dtag_res[RDISK_W_STREAMING_MODE_DTAG_CNT];
	u32 start;
	u32 end;
	u32 sz;
	u32 cnt;
	u32 i;
#endif
#if defined(ENABLE_EXTERNAL_STREAMING_READ_DTAG)
	start = (u32)&__dtag_stream_read_start;
	end = (u32)&__dtag_stream_read_end;
	sz = end - start;
	cnt = sz / DTAG_SZE;
	disp_apl_trace(LOG_INFO, 0x6922, "alloc Streaming Read #Dtags(%d) start %x end %x", cnt, start, end);
	sys_assert(cnt == RDISK_R_STREAMING_MODE_DTAG_CNT);
	sys_assert((start & 0xFFF) == 0);
	sys_assert((end & 0xFFF) == 0);

	for (i = 0; i < cnt; i++)
		dtag_res[i] = mem2dtag((void *)start + i * DTAG_SZE);

	bm_free_aurl_load(dtag_res, cnt);
#endif
#if defined(SEMI_WRITE_ENABLE)
	start = (u32)&__dtag_stream_write_start;
	end = (u32)&__dtag_stream_write_end;
	sz = end - start;
	cnt = sz / DTAG_SZE;
	disp_apl_trace(LOG_INFO, 0xf3db, "alloc Streaming Write #Dtags(%d) start %x end %x", cnt, start, end);
	sys_assert(cnt == RDISK_W_STREAMING_MODE_DTAG_CNT);
	sys_assert((start & 0xFFF) == 0);
	sys_assert((end & 0xFFF) == 0);

	for (i = 0; i < cnt; i++)
		dtag_res[i] = mem2dtag((void *)start + i * DTAG_SZE);

	btn_semi_write_ctrl(true);
	bm_free_semi_write_load(dtag_res, cnt, 0);

	start = (u32)&__dtag_stream_write_ex_start;
	end = (u32)&__dtag_stream_write_ex_end;
	sz = end - start;
	cnt = sz / DTAG_SZE;
	disp_apl_trace(LOG_INFO, 0x27c1, "alloc Streaming Write Ex #Dtags(%d) start %x end %x", cnt, start, end);
	sys_assert(cnt <= RDISK_W_STREAMING_MODE_DTAG_CNT);
	sys_assert((start & 0xFFF) == 0);
	sys_assert((end & 0xFFF) == 0);

	for (i = 0; i < cnt; i++)
		dtag_res[i] = mem2dtag((void *)start + i * DTAG_SZE);

	btn_semi_write_ctrl(true);
	bm_free_semi_write_load(dtag_res, cnt, 0);
#else
	disp_apl_trace(LOG_INFO, 0x3dff, "disable Streaming Write");
#endif
}

/*!
 * @brief prepare ddr dtag for host and gc io
 *
 * This function can be called after nand geo initialized to count dtag for gc.
 * Must make sure gc dtag start right after io dtag for dtag meta usage
 *
 * @return		not used
 */
init_code static void rdisk_ddtag_prep(void)
{
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	shr_ra_ddtag_start = DDR_RA_DTAG_START;
	#endif
	shr_gc_ddtag_start = DDR_GC_DTAG_START;
	sys_assert(FCORE_GC_DTAG_CNT <= MAX_DDR_DTAG_CNT - DDR_GC_DTAG_START);

	//u32 ttl_ddr_dtag = shr_gc_ddtag_start + FCORE_GC_DTAG_CNT;
	u32 ttl_ddr_dtag = MAX_DDR_DTAG_CNT;
	u32 ddr_meta_size = ttl_ddr_dtag * sizeof(struct du_meta_fmt);
	u32 ddr_meta_dtag = occupied_by(ddr_meta_size, DTAG_SZE);
	u32 ddr_meta_start = ddr_dtag_register(ddr_meta_dtag);
	shr_ddtag_meta = (void *)ddtag2mem(ddr_meta_start);
	memset(shr_ddtag_meta, 0x00, ttl_ddr_dtag * sizeof(struct du_meta_fmt));
}

init_code void patrol_read_init(void)
{
    shr_pa_rd_ddtag_start = DDR_PA_RD_DTAG_START;
    sys_assert(DDR_PA_RD_DTAG_CNT <= (MAX_DDR_DTAG_CNT - DDR_PA_RD_DTAG_START));

    /*u32 ttl_ddr_dtag = DDR_PA_RD_DTAG_CNT;
	u32 ddr_meta_size = ttl_ddr_dtag * sizeof(struct du_meta_fmt);
	u32 ddr_meta_dtag = occupied_by(ddr_meta_size, DTAG_SZE);
	u32 ddr_meta_start = ddr_dtag_register(ddr_meta_dtag);
	PardMeta = (void *)ddtag2mem(ddr_meta_start);
	memset((void *)PardMeta, 0xFF, ttl_ddr_dtag * sizeof(struct du_meta_fmt));*/
}


fast_code static void ipc_ftl_core_ctx_done(volatile cpu_msg_req_t *req)
{
	//u64 start = get_tsc_64();
	ftl_core_ctx_t *ctx = (ftl_core_ctx_t *)req->pl;
//	if (plp_trigger) {
//		disp_apl_trace(LOG_ERR, 0, "get flush done:0x%x-%x", start>>32, start&0xFFFFFFFF);
//	}

	if (is_ptr_tcm_share((void *)ctx))
		ctx = tcm_share_to_local((void *)ctx);

	ctx->cmpl(ctx);
}

fast_code void ipc_xfer_rc_data(volatile cpu_msg_req_t *req)
{
    bm_pl_t bm_pl;
    CBF_HEAD(&shr_rc_host_queue, bm_pl.all);
    bm_radj_push_rel((bm_pl_t *)&bm_pl, 1);
    disp_apl_trace(LOG_ALW, 0xfa0c, "xfer data, nvm cmd id %d, btag %d, off %d, dtag %d",
        bm_pl.pl.nvm_cmd_id, bm_pl.pl.btag, bm_pl.pl.du_ofst, bm_pl.pl.dtag);
    CBF_REMOVE_HEAD(&shr_rc_host_queue);
}

fast_data_zi struct timer_list pln_high_check_timer;

ddr_code void pln_high_check(void *data)
{
    u32 gpio_in = 0, gpio0_value = 0;

    gpio_in = readl((void *)(MISC_BASE + GPIO_PAD_CTRL));
    gpio0_value = (gpio_in >> (GPIO_OUT_SHIFT + GPIO_PLN_SHIFT)) & 0x1;
    //disp_apl_trace(LOG_INFO, 0, "--check pln  %d",gpio0_value);
    pln_high_check_timer.function = pln_high_check;
    pln_high_check_timer.data = NULL;
    mod_timer(&pln_high_check_timer,jiffies + HZ/10);

    if((PLA_FLAG == true)&&(PLN_FLAG == true)&&(gpio0_value == 1)&&(PLN_in_low == true)){
         disp_apl_trace(LOG_INFO, 0x80d8, "-- pln high 0x%x",gpio_in);
         //PLN_keep_50ms = false;
         PLN_in_low = false;
         nvmet_io_fetch_ctrl(false); //enable fetch io
         btn_de_wr_enable();//enable btn wr
         del_timer(&pln_high_check_timer);
     }
}

ddr_code void ipc_pla_pull(volatile cpu_msg_req_t *req)
{
    u32 PLA;
    PLA = readl((void *)(MISC_BASE + GPIO_OUT));

    if((PLA_FLAG == false)&&(PLN_FLAG == false)){
        // writel((PLA | BIT(GPIO_PLA_SHIFT + GPIO_OUT_SHIFT)) | BIT(GPIO_PLA_SHIFT) , (void *)(MISC_BASE + GPIO_OUT));//pull low
        writel((PLA ^ BIT(GPIO_PLA_SHIFT + GPIO_OUT_SHIFT)) | BIT(GPIO_PLA_SHIFT) , (void *)(MISC_BASE + GPIO_OUT));//pull high
        PLA_FLAG = true;
        PLN_FLAG = true;

        pln_high_check_timer.function = pln_high_check;
        pln_high_check_timer.data = NULL;
        mod_timer(&pln_high_check_timer,jiffies + HZ);
        disp_apl_trace(LOG_INFO, 0xdda0, "gpio_12_isr PLA high",PLA);
    }
}

ddr_code void ipc_io_cmd_switch(volatile cpu_msg_req_t *req)
{
    disp_apl_trace(LOG_INFO, 0x6a4c, "[IN]ipc_io_cmd_switch %d",req->pl);
    bool flag = (bool)req->pl;
    nvmet_io_fetch_ctrl(flag);
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


fast_code void ipc_fe_req_op_done(volatile cpu_msg_req_t *req)
{
	fe_req_t *fe_req = (fe_req_t *)req->pl;

	if (is_ptr_tcm_share((void *)fe_req))
		fe_req = (fe_req_t *)tcm_share_to_local((void *)fe_req);

	fe_req->cmpl(fe_req);
}

fast_code static void ipc_l2p_load_done(volatile cpu_msg_req_t *req)
{
	if (!bcmd_list_empty(&bcmd_pending))
		bcmd_resume();
}

fast_code void ipc_btn_cmd_done(volatile cpu_msg_req_t *req)
{
	u32 cid = req->pl;
	u32 btag = req->cmd.flags & 0x7FFF;
	bool ret_cq = (req->cmd.flags >> 15) ? true : false;

	nvmet_core_btn_cmd_done(btag, ret_cq, cid);
	cpu_msg_sync_done(req->cmd.tx);
}

ddr_code void ipc_enter_read_only_handle(volatile cpu_msg_req_t *req)
{
	disp_apl_trace(LOG_INFO, 0xa9e1, "IN RO %x", read_only_flags.all);
	cmd_proc_read_only_setting(true);
}

ddr_code void ipc_leave_read_only_handle(volatile cpu_msg_req_t *req)
{
	disp_apl_trace(LOG_INFO, 0x10ba, "OUT RO %x", read_only_flags.all);
	if(read_only_flags.all == 0)
	{
		cmd_proc_read_only_setting(false);
	}
}

ddr_code void ipc_disable_btn(volatile cpu_msg_req_t *req)
{
	cmd_disable_btn(-1,1);
}

ddr_code void err_cmd_flush_done(ftl_core_ctx_t *ctx)
{
    ftl_flush_data_t *fctx = (ftl_flush_data_t *)ctx;
    u32 btag = (u32)fctx->ctx.caller;
    sys_free(FAST_DATA, ctx);
	btn_de_wr_enable();
    btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
    btn_cmd_t *bcmd = btag2bcmd(btag);
	if(!esr_err_fua_flag)
    	sys_assert(is_fua_bcmd(bcmd));
    bcmd_ex->du_xfer_left = 0;
    disp_apl_trace(LOG_ERR, 0x83ca, "Err ABT FUA CMD");
    btn_cmd_rels_push(bcmd, RLS_T_WRITE_ABT_RSP);
}
ddr_code void err_cmd_flush(u32 btag)
{
	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA,sizeof(ftl_flush_data_t));
	sys_assert(fctx);
	btn_de_wr_disable();
	fctx->ctx.caller = (void *)btag;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->ctx.cmpl = err_cmd_flush_done;
	ucache_flush(fctx);
}

fast_code void fe_dtag_free(void)
{
	u32 did;
	u32 cnt[2];
	u32 pcnt;
	u32 *p[2];
	u32 i;
	u32 fetched_cnt;

	if (CBF_EMPTY(&dtag_free_que))
		return;
    extern u32 sw_ipc_poll_cnt;
    if (sw_ipc_poll_cnt > 1) {
        return;
    }
	CBF_FETCH_LIST(&dtag_free_que, cnt, p, pcnt);
	fetched_cnt = 0;
	for (i = 0; i < pcnt; i++)
	{
		u32 j;

		for (j = 0; j < cnt[i]; j++)
		{
			did = p[i][j];

#if 0 //(TRIM_SUPPORT == DISABLE)
			if (did & DTAG_FREE_TRIM_INFO_DONE_BIT) {
				/* trim info update done */
				rdisk_trim_info_done((req_t *)(did & DTAG_FREE_VALID_MASK));
			} else

#endif
            //disp_apl_trace(LOG_INFO, 0, "fe free:%x", did);
            #ifdef LJ1_WUNC
            if (did & WUNC_FLAG) {//wunc bit
                //check ce list, st_l2p update
                // lda -> last wunc
                wunc_handle_done(did);
            }
            else
            #endif
            {
                if (did & DTAG_FREE_RANGE_TRIM_DONE_BIT) {
                    dtag_t dtag = {.dtag = ((did & DTAG_FREE_VALID_MASK) & DDTAG_MASK)};
                    put_trim_dtag(dtag);
                } else if (did & DTAG_FREE_UPDT_ABORT_BIT)
                {
                    ucache_pda_updt_abort(did & DTAG_FREE_VALID_MASK);
                }
                else
                {
                    /* entry update done */

                    rdisk_dref_dec(&did, 1);
                    ucache_pda_updt_done(did);
                }
            }
		}
		fetched_cnt += cnt[i];
	}
	CBF_FETCH_LIST_DONE(&dtag_free_que, fetched_cnt);
}

static init_code void dtag_free_init(void)
{
	CBF_INIT(&dtag_free_que);
	dtag_free_recv_hook((cbf_t *)&dtag_free_que, fe_dtag_free);
}

/*!
 * @brief get total ecc error count
 *
 * @return	total ecc error count
 */
ddr_code u64 get_ecc_err_cnt(void)
{
	return ftl_stat.total_ecc_err_cnt;
}

/*!
 * @brief get ungraceful shutdown count
 *
 * @return	total ungraceful shutdown count
 */
ddr_code u64 get_spor_cnt(void)
{
	return ftl_stat.total_spor_cnt;
}

ddr_code u64 get_erase_err_cnt(void)
{
	return (u64)ftl_stat.total_era_err_cnt;
}

ddr_code u64 get_program_err_cnt(void)
{
	return (u64)ftl_stat.total_pro_err_cnt;
}

ddr_code u32 get_fatory_defect_blk_cnt(void)
{
	return (u32)ftl_stat.total_factory_defect_blk_cnt;
}

ddr_code void dma_error_cnt_incr(void)
{
	ftl_stat.total_dma_error_cnt++;
}

/*!
 * @brief get read only flag
 *
 * @return	return true if read only was set
 */
ddr_code bool is_system_read_only(void)
{
	shr_ftl_flags.b.read_only = (read_only_flags.all > 0) ? true : false;
	return shr_ftl_flags.b.read_only;
}

ddr_code u64 get_req_statistics(void)
{
	u32 ttl = 0;

	ttl += req_statistics.wr_du_ttl;
	ttl += req_statistics.rd_rcv_ttl;
	ttl += req_statistics.req_rcv_ttl;

	req_statistics.wr_du_ttl = 0;
	req_statistics.rd_rcv_ttl = 0;
	req_statistics.req_rcv_ttl = 0;

	return ttl;
}

/*!
 * @brief restore fe info from latest fe block
 *
 * @note if read fail, use default value
 *
 * @return	not used
 */
ddr_code static void rdisk_fe_info_restore(void)
{
	u32 du_cnt = occupied_by(sizeof(fe_info_t), DTAG_SZE);
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, du_cnt);
	fe_req_t fe_req;
	fe_info_t *fe_info;
	nvme_feature_saved_t *nvme_feat = NULL;
	smart_statistics_t *smart = NULL;
	ftl_stat_t *ftl = NULL;

	sys_assert(dtag.dtag != _inv_dtag.dtag);

	fe_req.cmpl = NULL;
	fe_req.mem = dtag2mem(dtag);
	fe_req.size = sizeof(fe_info_t);
	fe_req.write = false;
	fe_req.type = FE_REQ_TYPE_LOG;
	fe_log_read(&fe_req);

	fe_info = (fe_info_t *)fe_req.mem;
	if (fe_info->sig == FE_INFO_SIG && fe_info->version == FE_INFO_VER)
	{
		nvme_feat = &fe_info->nvme_feat;
		smart = &fe_info->smart;
		ftl = &fe_info->ftl_stat;
	}
	else
	{
		disp_apl_trace(LOG_ERR, 0x651a, "fe sig %x %x, ver %d %d",
					   fe_info->sig, FE_INFO_SIG, fe_info->version, FE_INFO_VER);
	}

	nvmet_restore_feat(nvme_feat);
	wait_remote_item_done(be_lite_init);
	nvmet_restore_smart_stat(smart);

	bool ret = ftl_stat_restore(ftl);
	if (ret == false)
		disp_apl_trace(LOG_ALW, 0x036f, "def ftl stat %d", FTL_STAT_VER);

	if ((shr_ftl_flags.b.spor))
		ftl_stat.total_spor_cnt = 1;

	nvme_info_save(NULL);
}
#ifdef NS_MANAGE
//joe add NS 20200813
extern u16 ns_valid_sector;
//extern u8 total_order_now;//joe 20200608
extern struct ns_array_manage *ns_array_menu; //joe 20200616
extern u16 *sec_order_point;
extern u16 ns_sec_order[];
extern u16 *sec_order_p;
extern void nvmet_create_ns_section_array(u32 ns_id, u64 nsze); //joe add 20200908
ddr_code void ns_array_managa_init(void)						//joe 20200616 init the para
{

	//joe test epm 20200827
	epm_namespace_t *epm_ns_data = (epm_namespace_t *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
	//disp_apl_trace(LOG_ERR, 0, "joe: cool \n");
	ns_array_menu = (struct ns_array_manage *)(epm_ns_data->data);
	u16 a = 0;
	u16 b =0;
	for (a = 0; a < 256; a++)
	{ //joe 20200827 add ns_array_menu
		//disp_apl_trace(LOG_DEBUG, 0, "create ns_sec_array[%d]:%d", a, ns_array_menu->ns_sec_array[a]);
	}
	//joe test epm 20200827

	if (ns_array_menu->drive_flag == 0)
	{											   //joe add flag to judge new or old drive 20200827
		ns_array_menu->free_start_array_point = 0; //joe 20200616 marked test
		u32 i = 0;
		u32 a = 0;
		u32 p = 0;
		for (i = 0; i < 32; i++)
			ns_array_menu->array_order[i] = 0xff;

		for (a = 0; a < 1024; a++)
			ns_array_menu->ns_sec_array[a] = 0xffff;

		for (p = 0; p < 1024; p++)
			ns_array_menu->valid_sec2[p] = 0;

		ns_array_menu->total_order_now = 0;
	}

	sec_order_point = ns_array_menu->sec_order; //joe add 20200825  for hcrc faster
	for(b=0;b<1024;b++){
		ns_sec_order[b]=ns_array_menu->sec_order[b];
	}
	sec_order_p=ns_sec_order;
}

//joe=========20200610=============//
//joe add NS 20200813
extern void format_sec(u8, u8, u8);			 //joe add 20200908 for format
extern u16 ns_trans_sec[1024];//joe add 20201217
extern u16 drive_total_sector;//joe add 20201217
#ifdef Dynamic_OP_En
extern u32 DYOP_LBA_L;
extern u32 DYOP_LBA_H;
#define EPM_SET_OP_TAG    ( 0x534F5053 ) // 'SOPS'
#endif
ddr_code void rdisk_fe_ns_restore(void) //joe change to slow 20200901
{

	if (ns_array_menu->drive_flag == 0)
	{ //joe add epm test 20200827
		disp_apl_trace(LOG_INFO, 0xdecd, "new drive fe ns\n");
		nvme_ns_attr_t *attr;
		//fe_req_t req;
		ns_array_menu->drive_flag = 1; //joe add epm 20200827
		//attr = sys_malloc_aligned(SLOW_DATA, sizeof(nvme_ns_attr_t), 32);
		disp_apl_trace(LOG_INFO, 0xc058, "sizeof(nvme_ns_attr_t):%d\n", sizeof(nvme_ns_attr_t));
		attr = &ns_array_menu->ns_attr[0];
		sys_assert(attr);
		ns_array_menu->host_sec_bits = host_sec_bitz;
		/*req.cmpl = NULL;	/// sync
		req.mem = attr;
		req.size = sizeof(nvme_ns_attr_t);
		req.type = FE_REQ_TYPE_NS;
		req.write = false;*/

		//fe_ns_info_load(&req);

		//if (attr->hw_attr.nsid == 0) {
		//memset((void*)attr, 0, sizeof(*attr));
		//#ifndef NS_MANAGE
		attr->hw_attr.nsid = 1;
		attr->hw_attr.pad_pat_sel = 1;

#if defined(PI_SUPPORT)
		attr->fw_attr.support_lbaf_cnt = 4;
		attr->fw_attr.support_pit_cnt = 3;
#else
		attr->fw_attr.support_lbaf_cnt = 2; //joe test 20200817  1--->2  //joe 2--->4 20200911
		attr->fw_attr.support_pit_cnt = 0;
#endif

		attr->fw_attr.type = NSID_TYPE_ACTIVE;

		attr->fw_attr.ncap = ns[0].cap;
		attr->fw_attr.nsz = ns[0].cap;
		attr->hw_attr.lb_cnt = ns[0].cap;
		attr->hw_attr.lbaf =0;//joe add 20200105//joe add 20210107  cause new for ns the sector always 512/0
		//disp_apl_trace(LOG_ALW, 0, "use DEFAULT NS");
		//req.write = true;
		//fe_ns_info_save(&req);
		//#endif
		//}

#ifndef NS_MANAGE
		nvmet_set_ns_attrs(attr, true);
#else
		nvmet_set_ns_attrs(attr, true); //joe test NS 20200813
							//nvmet_set_ns_attrs_init(attr, true);

		nvmet_set_drive_capacity(ns[0].cap);
		//u64 test_cap=0;
		//joe add NS 20200813
		/*  if(host_sec_bitz==9){//joe add sec size 20200817
		test_cap=NS_SIZE_GRANULARITY_RDISK1+ns[1 - 1].cap;//joe test 20200526 for windows format //joe marked test 20200729
		}else{
		test_cap=NS_SIZE_GRANULARITY_RDISK2+ns[1 - 1].cap;//joe test 20200526 for windows format //joe marked test 20200729
		}*/
		//test_cap=ctrlr->drive_capacity;
		// nvmet_create_ns_section_array_rdisk(0,test_cap);//joe 20200610  //joe marked test 20200729//joe marked test 20200814
		//nvmet_create_ns_section_array(0, ns[0].cap); //joe add 20200908
		nvmet_create_ns_section_array(0, ns[0].cap); //joe add 20200908
		epm_update(NAMESPACE_sign, (CPU_ID - 1));	 //joe add update epm 20200908

		epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		if(epm_ftl_data->OPFlag == 1)
		{
			epm_ftl_data->OP_LBA_L = 0;
			epm_ftl_data->OP_LBA_H = 0;
			epm_ftl_data->OP_CAP = 0 ;
			epm_ftl_data->OPValue = 0 ;
			epm_ftl_data->OPFlag = 0;
			epm_update(FTL_sign, (CPU_ID - 1));
		}
			disp_apl_trace(LOG_INFO, 0x1af3, "ocan20 CAP 0x%x, LBA 0x%x,0x%x, Value %d, Flag %d \n",epm_ftl_data->OP_CAP, epm_ftl_data->OP_LBA_H, epm_ftl_data->OP_LBA_L, epm_ftl_data->OPValue, epm_ftl_data->OPFlag);
		//joe add NS 20200813
		//cmd_proc_load_ns_struct();
#endif

		//sys_free_aligned(SLOW_DATA, attr);
	}
#ifdef Dynamic_OP_En
	else if(ns_array_menu->drive_flag == 0xFF)//for OP Change
	{
		u8 lbaff = 0;
		nvme_ns_attr_t *attr;
		disp_apl_trace(LOG_INFO, 0x1c8b, "OP Change\n");
		ns_array_menu->drive_flag = 1;
		attr = &ns_array_menu->ns_attr[0];
		sys_assert(attr);
		host_sec_bitz = 9;
		nvmet_set_drive_capacity(ns[0].cap);

		attr->hw_attr.nsid = 1;
		attr->hw_attr.pad_pat_sel = 1;

#if defined(PI_SUPPORT)
		attr->fw_attr.support_lbaf_cnt = 4;
		attr->fw_attr.support_pit_cnt = 3;
#else
		attr->fw_attr.support_lbaf_cnt = 2;
		attr->fw_attr.support_pit_cnt = 0;
#endif

		attr->fw_attr.type = NSID_TYPE_ACTIVE;

		attr->fw_attr.ncap = ns[0].cap;
		attr->fw_attr.nsz = ns[0].cap;
		attr->hw_attr.lb_cnt = ns[0].cap;//512 byte lba count

		nvmet_set_ns_attrs_init(&ns_array_menu->ns_attr[0], true);

		lbaff=ns_array_menu->ns_attr[0].hw_attr.lbaf;
		ns_array_menu->host_sec_bits=_lbaf_tbl[lbaff].lbads;
		format_sec(ns_array_menu->host_sec_bits, lbaff, false);

#ifndef NS_MANAGE
		nvmet_set_ns_attrs(attr, true);
#else
		nvmet_set_ns_attrs(attr, true);

		nvmet_create_ns_section_array(0, attr->fw_attr.ncap);
		epm_update(NAMESPACE_sign, (CPU_ID - 1));

		epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		epm_ftl_data->OP_LBA_L = DYOP_LBA_L;
		epm_ftl_data->OP_LBA_H = DYOP_LBA_H;
		epm_ftl_data->OP_CAP = (u32)(((u64)epm_ftl_data->OP_LBA_L + (((u64)(epm_ftl_data->OP_LBA_H))<<32))/8) ;
		epm_ftl_data->OPFlag = 1;
        // need to add EPM flag to indicate set op is started, Sunny 20211028
        epm_ftl_data->Set_OP_Start = EPM_SET_OP_TAG;

		disp_apl_trace(LOG_INFO, 0xab69, "ocan7 OP_CAP 0x%x, OP_LBA 0x%x 0x%x \n",epm_ftl_data->OP_CAP, epm_ftl_data->OP_LBA_H, epm_ftl_data->OP_LBA_L);
		epm_update(FTL_sign, (CPU_ID - 1));
	#endif

	}
#endif
	else
	{
		disp_apl_trace(LOG_INFO, 0xbf93, "old drive fe ns\n");
		disp_apl_trace(LOG_INFO, 0xb4b6, "sizeof(nvme_ns_attr_t):%d\n", sizeof(nvme_ns_attr_t));
		u32 ss = 0;
		u32 iii=0;
		u32 free_start_p;
		nvmet_set_drive_capacity(ns[0].cap);
		for(iii=0;iii<drive_total_sector;iii++)//joe add performance test 20201217
		{
			ns_trans_sec[iii]=ns_array_menu->ns_sec_array[iii];
		}
		ns_order=ns_array_menu->total_order_now;
		ns_valid_sec=ns_array_menu->ns_valid_sector;
		for (ss = 0; ss < 32; ss++)
		{
			free_start_p=ns_array_menu->ns_array[ss].array_start;
			disp_apl_trace(LOG_DEBUG, 0xc4b0, "ns:%d free_start_p:%d",ss,free_start_p);
			ns_array_menu->ns_array[ss].sec_id = &ns_array_menu->ns_sec_array[free_start_p];//joe add 20200119 ns_epm issue
			ns_sec_id[ss].sec_id = &ns_trans_sec[free_start_p];//joe add for performance test 20201218
			//disp_apl_trace(LOG_ERR, 0, "ns type:%d\n",ns_array_menu->ns_attr[ss].fw_attr.type);
			//if(ns_array_menu->ns_attr[ss].fw_attr.type==NSID_TYPE_ACTIVE)
			//nvmet_set_ns_attrs(&ns_array_menu->ns_attr[ss], true);
			#if defined(PI_SUPPORT)
			ns_array_menu->ns_attr[ss].fw_attr.support_lbaf_cnt = 4;//joe add 20200105 to control old drive lbaf cnt
			ns_array_menu->ns_attr[ss].fw_attr.support_pit_cnt = 3;
			#else
			ns_array_menu->ns_attr[ss].fw_attr.support_lbaf_cnt = 2;//joe add 20200105 to control old drive lbaf cnt
			ns_array_menu->ns_attr[ss].fw_attr.support_pit_cnt = 0;
			#endif

			if (ns_array_menu->ns_attr[ss].fw_attr.type != NSID_TYPE_UNALLOCATED) //joe add for detached ns case 20200831
				nvmet_set_ns_attrs_init(&ns_array_menu->ns_attr[ss], true);
		}
		//joe add for 4K 20200908
		u8 lbaff = 0; //joe add for 4K recover 20200908
		disp_apl_trace(LOG_INFO, 0xb1b7, "ns reload lbaf:%d",ns_array_menu->ns_attr[0].hw_attr.lbaf);

		//if (ns_array_menu->host_sec_bits != host_sec_bitz)
		if(ns_array_menu->ns_attr[0].hw_attr.lbaf != 0)
		{
			u8 nsid = 0;
			for (nsid = 0; nsid < 32; nsid++)
			{ //joe add change id-ns ncap,nsze,lbaf//joe 20200909 this case must be 4K sector so ,need change the ns_array_menu->cap
				if (ctrlr->nsid[nsid].type != NSID_TYPE_UNALLOCATED)
				{
					disp_apl_trace(LOG_ERR, 0xe786, "ctrlr->nsid[%d].ns->ncap1:%x  ", nsid, ctrlr->nsid[nsid].ns->ncap);
					#if defined(PI_SUPPORT)
					if(ns_array_menu->ns_attr[0].hw_attr.lbaf != 1)//joe 20210107 0 & 1 are 512 sec in pi fw
					#endif
					{
						ctrlr->nsid[nsid].ns->ncap = ctrlr->nsid[nsid].ns->ncap << (3); //joe cause here record the lba_cnt of 4KB, so need*8 back to 512
						ctrlr->nsid[nsid].ns->nsze = ctrlr->nsid[nsid].ns->nsze << (3);
					}
				}
			}
			disp_apl_trace(LOG_ERR, 0x0b71, "ns_array_menu->host_sec_bits:%d    ns_array_menu->ns_attr[0].hw_attr.lbaf:%d\n", ns_array_menu->host_sec_bits, ns_array_menu->ns_attr[0].hw_attr.lbaf);
			/*u8 a = 0;
			for (a = 0; a < MAX_LBAF; a++)
			{
				if (ns_array_menu->host_sec_bits == _lbaf_tbl[a].lbads)
				{
					lbaff = a;
					break;
				}
			}*/
			lbaff=ns_array_menu->ns_attr[0].hw_attr.lbaf;//joe 20210105 add to assign lbaf
			ns_array_menu->host_sec_bits=_lbaf_tbl[lbaff].lbads;
			format_sec(ns_array_menu->host_sec_bits, lbaff, false);

		} //joe add 20200908 for4K recovery
		else
		{
			u32 ss = 0;
			for (ss = 0; ss < 32; ss++)
			{
				if (ns_array_menu->ns_attr[ss].fw_attr.type == NSID_TYPE_ACTIVE)
				{
					nvmet_set_ns_attrs(&ns_array_menu->ns_attr[ss], true);
					disp_apl_trace(LOG_ERR, 0x2055, "ns %d active\n", ss+1);
				}
				if (ns_array_menu->ns_attr[ss].fw_attr.type == NSID_TYPE_ALLOCATED) //joe add for detached ns case 20200831
				{
					nvmet_set_ns_attrs_init(&ns_array_menu->ns_attr[ss], true);
					disp_apl_trace(LOG_ERR, 0xee4a, "ns %d allocated\n", ss+1);
				}
			}
		}
	}
	if((ns_order==1&&(drive_total_sector==ns_valid_sec)))
		full_1ns=1;
	else
		full_1ns=0;

	local_item_done(wait_ns_restore);

	#if defined(HMETA_SIZE)
		if(_lbaf_tbl[ns_array_menu->ns_attr[0].hw_attr.lbaf ].ms==0)
			host_du_fmt = DU_FMT_USER_4K;
		else
			host_du_fmt = DU_FMT_USER_4K_HMETA;
	#else
		host_du_fmt = DU_FMT_USER_4K;
	#endif
}

ddr_code static int re_init_ns(int argc, char *argv[]) //joe add reinit ns para 20200901
{
	u32 i = 0;
	u32 a = 0;
	u32 p = 0;

	ns_array_menu->free_start_array_point = 0; //joe 20200616 marked test

	for (i = 0; i < 32; i++)
		ns_array_menu->array_order[i] = 0xff;

	for (a = 0; a < 1024; a++)
		ns_array_menu->ns_sec_array[a] = 0xffff;

	for (p = 0; p < 1024; p++)
		ns_array_menu->valid_sec2[p] = 0;

	ns_array_menu->total_order_now = 0;

	ns_array_menu->ns_valid_sector = 0;

	ns_array_menu->drive_flag = 0;

	epm_update(NAMESPACE_sign, (CPU_ID - 1)); //joe add update epm
	return 0;
}

static DEFINE_UART_CMD(re_initns, "re_init_ns", "re_init_ns", "re_init_ns", 0, 1, re_init_ns);
#else
norm_ps_code void rdisk_fe_ns_restore(void)
{
	nvme_ns_attr_t *attr;
	fe_req_t req;

	attr = sys_malloc_aligned(SLOW_DATA, sizeof(nvme_ns_attr_t), 32);
	sys_assert(attr);

	req.cmpl = NULL; /// sync
	req.mem = attr;
	req.size = sizeof(nvme_ns_attr_t);
	req.type = FE_REQ_TYPE_NS;
	req.write = false;

	fe_ns_info_load(&req);

	if (attr->hw_attr.nsid == 0)
	{
		memset((void *)attr, 0, sizeof(*attr));
#ifndef NS_MANAGE
		attr->hw_attr.nsid = 1;
		attr->hw_attr.pad_pat_sel = 1;

#if defined(PI_SUPPORT)
		attr->fw_attr.support_lbaf_cnt = 2;
		attr->fw_attr.support_pit_cnt = 3;
#else
		attr->fw_attr.support_lbaf_cnt = 1;
		attr->fw_attr.support_pit_cnt = 0;
#endif

		attr->fw_attr.type = NSID_TYPE_ACTIVE;

		attr->fw_attr.ncap = ns[0].cap;
		attr->fw_attr.nsz = ns[0].cap;
		attr->hw_attr.lb_cnt = ns[0].cap;

		//disp_apl_trace(LOG_ALW, 0, "use DEFAULT NS");			//atcm code overlimit Niklaus mark in 20201118
		req.write = true;
		//fe_ns_info_save(&req); //Curry 20200804
#endif
	}

	nvmet_set_ns_attrs(attr, true);

	sys_free_aligned(SLOW_DATA, attr);
}

#endif
/*!
 * @brief rdisk resume function
 *
 * @param mode	sleep mode
 *
 * @return		None
 */
norm_ps_code void rdisk_resume(enum sleep_mode_t mode)
{
	rdisk_alloc_streaming_rw_dtags();

	rdisk_fe_ns_restore();
	ucache_resume();
	memset(&req_statistics, 0, sizeof(rdisk_req_statistics_t));
}

ddr_code static void uart_pat_gen_done(u32 cmd_slot, dtag_t *dtags, u32 cnt)//joe slow->ddr 20201124
{
	//disp_apl_trace(LOG_ALW, 0, "Data generated in Dtag: s-%x, %d", cmd_slot, cnt);

	u32 *rel_dtag = (u32 *)dtags;
	rdisk_dref_dec(rel_dtag, cnt);
}

/*slow_code static int pat_gen_main(int argc, char *argv[])//joe fast-->slow 20200904
{
	u64 slba = 0;
	u32 len = 0;
	u32 nsid = 1;

	if (argc > 1)
		slba = (u64) strtol(argv[1], (void *)0, 10);
	if (argc > 2)
		len = strtol(argv[2], (void *)0, 10);

	u32 ret = pat_gen_req(slba, len, nsid, _pat_gen_test_id);
	if (ret == ~0)
		disp_apl_trace(LOG_ERR, 0, "issue fail");

	return 0;
}

static DEFINE_UART_CMD (pat_gen, "pat_gen", "gen_pattern", "gen_data_pattern", 0, 3, pat_gen_main);*/

/*!
 * @brief ramdisk initialize
 * Register all buffer manager event handler functions.
 *
 * @return	None
 */
#if 0
fast_code void be_do_vu_command(volatile cpu_msg_req_t *req){
	disp_apl_trace(LOG_ERR, 0x02f3, "be do vu");
	ipc_evt_t *evt = (ipc_evt_t *)req->pl;

	switch(evt->evt_opc){
		case VSC_ERASE_NAND:
			break;
		case VSC_SCAN_DEFECT:
			break;
		case VSC_AGING_BATCH:
			break;
		case VSC_Read_FlashID:
			break;
		case VSC_Read_NorID:
			disp_apl_trace(LOG_ERR, 0x35da, "get shr_NorID");
			shr_NorID = spi_read_id();
			break;
		//#ifdef BC_DRAM_TEST
		case VSC_DRAMtag_Clear:
			break;
		//endif
		case VSC_Plist_OP:
			break;
		case VSC_Read_Tempture:
			break;
		case VSC_Read_CTQ:
			break;
		case VSC_AGING_ERASEALL:
			break;
		case VSC_AGING_ERASENAND:
			break;
		case VSC_Read_Dram_Tag:
			break;
		default:
			disp_apl_trace(LOG_ERR, 0xcb4b, "not support yet\n");
			break;
	}
	cpu_msg_sync_done(req->cmd.tx);
}
#endif

extern smb_registers_regs_t *smb_slv;
pcie_core_status_t pcie_core_status;
u8 MIBMCMDCrc8(u8 *data, u8 len)
{
	u32 crc = 0;
	u8 i, j;
	for (j = len; j; j--, data++)
	{
		//disp_apl_trace(LOG_ERR, 0, "CRC data %x\n",*data);
		crc ^= (u32)(*data << 8);
		for (i = 8; i; i--)
		{
			if (crc & 0x8000)
			{
				crc ^= (0x1070 << 3);
			}
			crc <<= 1;
		}
	}
	//disp_apl_trace(LOG_ERR, 0, "smbus CRC  %x\n",crc);
	return (u8)(crc >> 8);
}
typedef union SFLGS_1
{
	u8 all[1];
	struct
	{
		u8 Bit0_2 : 3;
		u8 PCIeLinkActive : 1;
		u8 ResetNotRequired : 1;
		u8 DriveFunctional : 1;
		u8 PoweredUp : 1;
		u8 SMBusArbitration : 1;
	} b;
} tSFLGS;
typedef union // edit smart bit
{
	u8 all;
	struct
	{
		u8 Bit0 : 1;
		u8 Bit1 : 1;
		u8 Bit2 : 1;
		u8 Bit3 : 1;
		u8 Bit4 : 1;
		u8 Bit5 : 1;
		u8 Bit6_7 : 2;
	} b;
} tSMARTWar;
typedef union
{
	u8 all[8];
	struct
	{
		tSFLGS SFLGS;
		tSMARTWar SMARTWar;
		u8 CTemp;
		u8 PDLU;
		u8 WCTemp;
		u8 CPower;
		u8 PEC;
		u8 RSV_0;
	} d;
	struct
	{
		u32 DEV_STS_DATA_1;
		u32 DEV_STS_DATA_2;
	} reg;
} tBMCMD0;
/* CRC Data Setup for CMD0/CMD8 Calculation */
	u8 CRCData1[0xC] = {0xD4, 0x00, 0xD5, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; //cmd 0
	u8 CRCData2[0x1C] = {0xD4, 0x08, 0xD5, 0x16, 0x1E, 0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00}; //cmd 8
	u8 CRCData3[0x16] = {0xD4, 0x20, 0xD5, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; //cmd 32
	u8 CRCData4[0x3E] = {0xD4, 0x60, 0xD5, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00}; //cmd 96
	u8 CRCData5[0xA] = {0xD4, 0xF2, 0xD5, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};//cmd 242
	u8 CRCData6[0xC] = {0xD4, 0xF8, 0xD5, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; //cmd 248
	u8 CRCData7[0x2C] = {0xD4, 0x32, 0xD5, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; //cmd 50
	u8 CRCData8[0x08] = {0xD4, 0x5A, 0xD5, 0x04, 0x00, 0x00, 0x00, 0x00}; //cmd 90
	u8 CRCData9[0xF] = {0xD4, 0x9A, 0xD5, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00}; //cmd 154
	u8 CRCData10[0x27] = {0xD4, 0xA7, 0xD5, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; //cmd 167
	u8 CRCData11[8] = {0xD4, 0xC9, 0xD5, 0x02, 0x00, 0x00, 0x00, 0x00};
	u8 CRCData12[8] = {0xD4, 0xCD, 0xD5, 0x01, 0x00, 0x00, 0x00, 0x00};
	u8 CRCData13[8] = {0xD4, 0xD0, 0xD5, 0x02, 0x00, 0x00, 0x00, 0x00};
	u8 CRCData14[14] = {0xD4, 0xD4, 0xD5, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
//slow_code void mi_basic_cmd0_update(u16 temp_data)
ddr_code void mi_basic_cmd0_update(u16 temp_data)
{
	//struct nvme_health_information_page *health;
	extern smart_statistics_t *smart_stat;
	extern __attribute__((weak)) void get_avg_erase_cnt(u32 * avg_erase, u32 * max_erase, u32 * min_erase, u32 * total_ec);
	extern struct nvme_registers nvmet_regs;
	extern  ts_mgr_t ts_mgr;
	extern const char* getMnValue(int cap_idx);
	srb_t *srb = (srb_t *) SRAM_BASE;
	u16 i = 0;
	const char* MN = getMnValue(srb->cap_idx);
	char MN_M[41] = {0};
	strncpy(MN_M, MN, sizeof(MN_M) - 1); // Copy at most sizeof(MN_M) - 1 characters to leave space for the null terminator
	MN_M[sizeof(MN_M) - 1] = '\0'; // Ensure null termination
    memset(MN_M + strlen(MN), 0, sizeof(MN_M) - strlen(MN) - 1);  // Fill the remaining space with 0
	/* SMART Warning Checking
	//Bit 7:5 Reserved
	health->critical_warning.bits.reserved = 0;

	//Bit 4 Volatile Memory Backup
	health->critical_warning.bits.volatile_memory_backup

	//Bit 3 Read-Only Mode
	health->critical_warning.bits.read_only

	//Bit 2 NVMe subsystem reliability degraded
	health->critical_warning.bits.device_reliability

	//Bit 1 Temperature
	health->critical_warning.bits.temperature

	//Bit 0 Available Spare
	health->critical_warning.bits.available_spare

	*/

	pcie_core_status2_t link_sts;
	
	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_UPDATE_SPARE_AVG_ERASE_CNT, 0, 1); //update *smart_stat
	u64 tnvmcap=0;
	if (host_sec_bitz == 9)
	{
		tnvmcap=(u64)(ctrlr->drive_capacity * LBA_SIZE1); /* in bytes */
	}
	else
	{
		tnvmcap=(u64)(ctrlr->drive_capacity * LBA_SIZE2); /* in bytes */
	}
	/* PDLU Calculation */			//Need Modify
	/*
	u32 avg_erase = 100;
	u32 max_erase = 1400;
	u32 min_erase = 0;
	u32 total_ec = 0;
	u32 used;
	if (get_avg_erase_cnt)
	{
		get_avg_erase_cnt(&avg_erase, &max_erase, &min_erase, &total_ec);
	}
	used = 100 * avg_erase / max_erase;
	if (used > 254)
	{
		used = 255;
	}
	*/
	tBMCMD0 BMCMD0;
	BMCMD0.d.SFLGS.b.Bit0_2 = 0x03;
		BMCMD0.d.SFLGS.b.PCIeLinkActive = !pcie_core_status.b.smlh_link_up_18;
	BMCMD0.d.SFLGS.b.ResetNotRequired = 1;
	BMCMD0.d.SFLGS.b.DriveFunctional = 1;
		BMCMD0.d.SFLGS.b.PoweredUp = !nvmet_regs.csts.bits.rdy;
	BMCMD0.d.SFLGS.b.SMBusArbitration = 1;
	BMCMD0.d.SMARTWar.all = ~smart_stat->critical_warning.raw;
	BMCMD0.d.CTemp = temp_data;
	BMCMD0.d.PDLU = smart_stat->percentage_used;
	BMCMD0.d.WCTemp = 0;
	BMCMD0.d.CPower = 0;
	BMCMD0.d.PEC = 0;
	BMCMD0.d.RSV_0 = 0;
	/* Set Info-Ready Register (HW Mode) */
	/*
	smb_dev_stc_data_6_t smb_dev_stc_data_6;
	smb_dev_stc_data_6.b.smb_dev_inf_ry = 0x0;
    writel(smb_dev_stc_data_6.all, &smb_slv->smb_dev_stc_data_6);
	*/

	/* MI Basic CMD Update Global Variables (SW Mode) */

	/*CMD0*/

	mi_cmd0_g[0] = 0x06; //status flag
	mi_cmd0_g[1] = BMCMD0.d.SFLGS.all[0]; //status flag
	mi_cmd0_g[2] = BMCMD0.d.SMARTWar.all;
	mi_cmd0_g[3] = BMCMD0.d.CTemp; //Temperature
	mi_cmd0_g[4] = BMCMD0.d.PDLU;  //Drive Life Used

	mi_cmd0_g[5] = BMCMD0.d.WCTemp; //Reserved: Critical Warning Composite Temperature
	mi_cmd0_g[6]= BMCMD0.d.CPower; //Reserved: Power
	memcpy(CRCData1 + 4, BMCMD0.all, 7);
	mi_cmd0_g[7] = MIBMCMDCrc8(CRCData1, 0x0A);

	/*CMD8*/
	extern AGING_TEST_MAP_t *MPIN;
	u8 BM_SN[20];
	u8 *SN_num = (u8 *)MPIN->drive_serial_number;
	memcpy((s8 *)&BM_SN[0], (s8 *)SN_num, 20);
	mi_cmd0_g[8] = 0x16;
	mi_cmd0_g[9] = (VID >> 8) & 0xFF;
	mi_cmd0_g[10] = VID & 0xFF;
	mi_cmd0_g[11] = BM_SN[0];
	mi_cmd0_g[12] = BM_SN[1];
	mi_cmd0_g[13] = BM_SN[2];
	mi_cmd0_g[14] = BM_SN[3];
	mi_cmd0_g[15] = BM_SN[4];
	mi_cmd0_g[16] = BM_SN[5];
	mi_cmd0_g[17] = BM_SN[6];
	mi_cmd0_g[18] = BM_SN[7];
	mi_cmd0_g[19] = BM_SN[8];
	mi_cmd0_g[20] = BM_SN[9];
	mi_cmd0_g[21] = BM_SN[10];
	mi_cmd0_g[22] = BM_SN[11];
	mi_cmd0_g[23] = BM_SN[12];
	mi_cmd0_g[24] = BM_SN[13];
	mi_cmd0_g[25] = BM_SN[14];
	mi_cmd0_g[26] = BM_SN[15];
	mi_cmd0_g[27] = BM_SN[16];
	mi_cmd0_g[28] = BM_SN[17];
	mi_cmd0_g[29] = BM_SN[18];
	mi_cmd0_g[30] = BM_SN[19];
	memcpy(CRCData2 + 3, mi_cmd0_g + 8, 23);
	mi_cmd0_g[31] = MIBMCMDCrc8(CRCData2, 0x1A);
	/*CMD32*/
	//pj1 does not have uuid

	/*CMD32*/
	mi_cmd0_g[32] = 0x10;
	for(i = 33; i < 49; i++){
		mi_cmd0_g[i] = 0x00;
	}
	memcpy(CRCData3 + 4, mi_cmd0_g + 33, 16);
	mi_cmd0_g[49] = MIBMCMDCrc8(CRCData3, 0x14);

	/*CMD50*/
	mi_cmd0_g[50] = 0x26;
	if(ts_mgr.attr.b.training_enable)
	{
		mi_cmd0_g[51] = 0xFF;
	}else{
		mi_cmd0_g[51] = 0x00;
	}
	mi_cmd0_g[52] = 0x08;
	for(i = 53; i < 85; i++){
		mi_cmd0_g[i] = 0x00;
	}
	#if PURE_SLC == 1
		if(srb->cap_idx == CAP_SIZE_2T)	{
			mi_cmd0_g[85] = 0x80;
			mi_cmd0_g[86] = 0x20;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_1T) {
			mi_cmd0_g[85] = 0x40;
			mi_cmd0_g[86] = 0x01;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_512G) {
			mi_cmd0_g[85] = 0xA0;
			mi_cmd0_g[86] = 0x00;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_4T) {
			mi_cmd0_g[85] = 0x00;
			mi_cmd0_g[86] = 0x05;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_8T) {
			mi_cmd0_g[85] = 0x00;
			mi_cmd0_g[86] = 0x0A;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
	#else
		if(srb->cap_idx == CAP_SIZE_2T)	{
			mi_cmd0_g[85] = 0x80;
			mi_cmd0_g[86] = 0x07;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_1T) {
			mi_cmd0_g[85] = 0xC0;
			mi_cmd0_g[86] = 0x03;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_512G) {
			mi_cmd0_g[85] = 0xE0;
			mi_cmd0_g[86] = 0x01;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_4T) {
			mi_cmd0_g[85] = 0x00;
			mi_cmd0_g[86] = 0x0F;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
		else if(srb->cap_idx == CAP_SIZE_8T) {
			mi_cmd0_g[85] = 0x00;
			mi_cmd0_g[86] = 0x1E;
			mi_cmd0_g[87] = 0x00;
			mi_cmd0_g[88] = 0x00;
		}
	#endif
	memcpy(CRCData7 + 4, mi_cmd0_g + 51, 4);
	mi_cmd0_g[89] = MIBMCMDCrc8(CRCData7, 0x2A);
	/*CMD90*/
	mi_cmd0_g[90] = 0x04;
	mi_cmd0_g[91] = 0x00;
	for(i = 92; i < 95; i++){
		mi_cmd0_g[i] = 0x00;
	}
	memcpy(CRCData8 + 4, mi_cmd0_g + 91, 4);
	mi_cmd0_g[95] = MIBMCMDCrc8(CRCData7, 0x06);
	/*CMD96*/
	mi_cmd0_g[96] = 0x38;
	mi_cmd0_g[97] = (u8)FR[0];
	mi_cmd0_g[98] = (u8)FR[1];
	mi_cmd0_g[99] = (u8)FR[2];
	mi_cmd0_g[100] = (u8)FR[3];
	mi_cmd0_g[101] = (u8)FR[4];
	mi_cmd0_g[102] = (u8)FR[5];
	mi_cmd0_g[103] = (u8)FR[6];
	mi_cmd0_g[104] = 0x00; //do not show the small version FR[7]
	mi_cmd0_g[105] = (FW_SECURITY_VERSION_H >> 24) & 0xFF;
	mi_cmd0_g[106] = (FW_SECURITY_VERSION_H >> 16) & 0xFF;
	mi_cmd0_g[107] = (FW_SECURITY_VERSION_H >> 8) & 0xFF;
	mi_cmd0_g[108] = FW_SECURITY_VERSION_H & 0xFF;
	mi_cmd0_g[109] = (FW_SECURITY_VERSION_L >> 24) & 0xFF;
	mi_cmd0_g[110] = (FW_SECURITY_VERSION_L >> 16) & 0xFF;
	mi_cmd0_g[111] = (FW_SECURITY_VERSION_L >> 8) & 0xFF;
	mi_cmd0_g[112] = FW_SECURITY_VERSION_L & 0xFF;
	for(i = 109; i < 113; i++){
		mi_cmd0_g[i] = 0x00;
	}
	for(i = 0; i < 40; i++){
		mi_cmd0_g[i+113] = MN_M[i];
	}
	memcpy(CRCData4 + 4, mi_cmd0_g + 97, 56);
	mi_cmd0_g[153] = MIBMCMDCrc8(CRCData4, 0x3C);

	/*CMD154*/
	mi_cmd0_g[154] = 0x0A;
	for(i = 155; i < 166; i++){
		mi_cmd0_g[i] = 0x00;
	}
	memcpy(CRCData9 + 4, mi_cmd0_g + 155, 11);
	mi_cmd0_g[166] = MIBMCMDCrc8(CRCData9, 0xD);

	/*CMD167*/
	mi_cmd0_g[167] = 0x20;
	for(i = 168; i < 200; i++){
		mi_cmd0_g[i] = 0x00;
	}
	memcpy(CRCData10 + 4, mi_cmd0_g + 168, 32);
	mi_cmd0_g[200] = MIBMCMDCrc8(CRCData10, 0x25);
#if 0	
	for(i = 201; i < 242; i++){
		mi_cmd0_g[i] = 0x00;
	}
#else
	/*CMD201*/
	mi_cmd0_g[201] = 2;
	mi_cmd0_g[202] = (DID >> 8) & 0xFF;
	mi_cmd0_g[203] = DID & 0xFF;
	memcpy(CRCData11 + 4, mi_cmd0_g + 202, 2);
	mi_cmd0_g[204] = MIBMCMDCrc8(CRCData11, 6);

	/*CMD205*/
	mi_cmd0_g[205] = 1;
	mi_cmd0_g[206] = smart_stat->available_spare;
	memcpy(CRCData12 + 4, mi_cmd0_g + 206, 1);
	mi_cmd0_g[207] = MIBMCMDCrc8(CRCData12, 5);

	/*CMD208*/
	mi_cmd0_g[208] = 2;
	mi_cmd0_g[209] = link_sts.b.neg_link_speed;
	mi_cmd0_g[210] = 4; //support link speed, Gen4
	memcpy(CRCData13 + 4, mi_cmd0_g + 209, 2);
	mi_cmd0_g[211] = MIBMCMDCrc8(CRCData13, 6);

	/*CMD212*/
	mi_cmd0_g[212] = 8;
	memcpy(&mi_cmd0_g[213], &tnvmcap, 8);
	memcpy(CRCData14 + 4, mi_cmd0_g + 213, 8);
	mi_cmd0_g[221] = MIBMCMDCrc8(CRCData14, 12);

	
	for(i = 222; i < 242; i++){
		mi_cmd0_g[i] = 0x00;
	}
#endif
	/*CMD242*/
	mi_cmd0_g[242] = 0x04;
	mi_cmd0_g[243] = 0x00;
	mi_cmd0_g[244] = 0x00;
	mi_cmd0_g[245] = 0x00;
	mi_cmd0_g[246] = 0x00;
	memcpy(CRCData5 + 4, mi_cmd0_g + 243, 4);
	mi_cmd0_g[247] = MIBMCMDCrc8(CRCData5, 0x8);
	/*CMD248*/
	mi_cmd0_g[248] = 0x06;
	mi_cmd0_g[249] = 0x00;
	mi_cmd0_g[250] = 0x04;
	mi_cmd0_g[251] = 0x00;
	mi_cmd0_g[252] = 0x00;
	mi_cmd0_g[253] = 0x00;
	mi_cmd0_g[254] = 0x00;
	memcpy(CRCData6 + 4, mi_cmd0_g + 249, 6);
	mi_cmd0_g[255] = MIBMCMDCrc8(CRCData6, 0x0A);


	/* Set All Control Registers (HW Mode) */


	// smb_control_register.b.smb_clk_stretch_en = 0x01;
	// smb_control_register.b.smb_tran_tp_sel = 0x05;
	//  bit [16]
	// smb_control_register.b.smb_slv_timer_opt = 0x01;
	// smb_control_register.b.smb_tran_i2c_en = 0x01;
	// writel(smb_control_register.all, &smb_slv->smb_control_register);
}
ddr_code void GetSensorTemp(void *data)//joe slow->ddr 20201124
{
//	if (Detect_FLAG == 0)
//		disp_apl_trace(LOG_ALW, 0, "[Enter GetSensorTemp] Stage 1, Detect_FLAG: %d, I2C_read_lock: %d",Detect_FLAG, I2C_read_lock);
//	else if(Detect_FLAG == 1)
//		disp_apl_trace(LOG_ALW, 0, "[Enter GetSensorTemp] Stage 2, Detect_FLAG: %d, I2C_read_lock: %d",Detect_FLAG, I2C_read_lock);

	if(plp_trigger)
		return;
	extern smart_statistics_t *smart_stat;
	u8 Mask= 0x01;

	if(I2C_read_lock == false)    	//1210Shane  Thermal Sensor Read Only when I2C_read not in action
	{
		if (Detect_FLAG == 0)
		{
			//disp_apl_trace(LOG_ALW, 0, "Shane0 Entering GetSensorTemp, SenIdx:%d, smart_stat.temperature: %d, Temp_CMD0_Update:%d", SenIdx,smart_stat.temperature -273, Temp_CMD0_Update-273); //shane debug 0106
			if(BadSenTrk & (Mask << SenIdx))		// Fail Case, Immediate Return To Check Next Sensor
			{
				SenIdx++;
				if (SenIdx >= (MAX_TEMP_SENSOR))		//Sensor Index Loop
					SenIdx = 0;
				mod_timer(&GetSensorTemp_timer, jiffies + HZ/10);
				return;
			}
			else
			{
				Send_Temp_Read(addr_w[SenIdx], addr_w[SenIdx] + 1);
//				disp_apl_trace(LOG_ALW, 0, "[Send_Temp_Read done]");
				Detect_FLAG = 1;
    		    mod_timer(&GetSensorTemp_timer, jiffies + HZ/10);
			}
		}
		//for AER
#if 0 //this if for aer cmd wait for temp error determined
		if(temp != 255+273){
			if(temp < ctrlr->cur_feat.temp_feat.tmpth[TempCnt + 1][1] || temp > ctrlr->cur_feat.temp_feat.tmpth[TempCnt + 1][0]){
				if(ctrlr->cur_feat.aec_feat.b.smart & 0x02){
					extern void nvmet_evt_aer_in();
					nvmet_evt_aer_in(((NVME_EVENT_TYPE_SMART_HEALTH << 16)|SMART_STS_TEMP_THRESH),0);
				}
			}
		}
#endif
		else				//1210Shane  Detect_FLAG == 1
		{
			u16 temp = 0;
			u16 shift_temp;
			//if(SenIdx == 1)
			temp = Read_Temp_data(_SensorTemp, SenIdx);
//			disp_apl_trace(LOG_ALW, 0, "[Read_Temp_data done, Temperature: %d]", temp-273);
			//else
			//temp = 0xFFFF;		//shane debug error testing 0112
			//disp_apl_trace(LOG_ALW, 0, "Shane1: Sensor %d temp: 0x%x, BadSenTrk:%x", SenIdx, temp, BadSenTrk); //shane debug 0201
TempShiftRetry:
			if(temp == (0xFFFF) )   //Read_Temp_Data error return case
      		{
            	if(SenRetryCnt < 5)		//1210Shane  Sensor Read Retry
            	{
                	mod_timer(&GetSensorTemp_timer, jiffies + 1);
					disp_apl_trace(LOG_ALW, 0xf2bf, "temp sensor %d err retry cnt %d",SenIdx,SenRetryCnt);
					if(SenRetryCnt == 4)
					flush_to_nand(EVT_ThermalSensor_Read_Error);
					Detect_FLAG = 0;
                	SenRetryCnt ++;
                	return;
            	}
            	else
            	{
					// Bypass Dysfunctional Sensor		Shane20201215
					BadSenTrk |= (Mask << SenIdx);
					disp_apl_trace(LOG_ALW, 0x93ff, "Sen %d retry fail, Track Value:%x", SenIdx, BadSenTrk);
					if(BadSenTrk == SenAllFail)
					{
						disp_apl_trace(LOG_ALW, 0x41aa, "All Sensor Fail, Sending 0x80 + 273");
						smart_stat->temperature = 0x80 + 273;//_SensorTemp[TempCnt];
						Temp_CMD0_Update = 0x80 + 273;
					}
					SenRetryCnt = 0;
		TempShiftCnt = 0;
				}
			}
			else
        	{
				if(temp >= (0x7F + 273)  && temp <= (0xC3 + 273)) //127-195 => report 0x7F
				temp = 0x7F + 273;
				//disp_apl_trace(LOG_INFO, 0, "current sensor %d temp is %d",TempCnt,smart_stat.temperature_sensor[TempCnt] - 273);

				if(temp >= (0xC4 + 273) && temp <= (0xD8 + 273)) //  C4h(-60C) - D8h(-40C) => report 0xD8
				temp = 0xD8 + 273;
				//	disp_apl_trace(LOG_INFO, 0, "Thermal Sensor [%d] < -40 degC, Send 0xD8 + 273", SenIdx);
				if(temp > TempShiftCheck[SenIdx])
				{
					 shift_temp	= (temp - TempShiftCheck[SenIdx]);
            	}
				else
				{
					 shift_temp = (TempShiftCheck[SenIdx] - temp);
				}

				//disp_apl_trace(LOG_ALW, 0, "shift_temp:%d  TempShiftCheck[SenIdx]: %d, SenIdx: %d, temp: %d ",shift_temp,TempShiftCheck[SenIdx],SenIdx, temp);
				if(shift_temp > 5 )
      			{
            		if(TempShiftCnt < 3)
            		{
						temp = smb_tmp102_read(addr_w[SenIdx], addr_w[SenIdx] + 1);
						TempShiftCnt ++;
						goto TempShiftRetry;
            		}
            		else
            		{
						TempShiftCheck[SenIdx] = temp;//_SensorTemp[TempCnt];
                		TempShiftCnt = 0;
						if(TempShiftInit & (1 << SenIdx))
						{
							disp_apl_trace(LOG_ALW, 0x0857, "temp sensor %d err 3-time retry to cover temp shift",SenIdx);
            			}
						TempShiftInit |= 1 << SenIdx;
            		}
				}

				TempShiftCheck[SenIdx] = temp;
				if(SenIdx == 0 || ( SenIdx == 1 && BadSenTrk == 1))//Reporting sensor 0 unless bypassed
				{
					smart_stat->temperature = TempShiftCheck[SenIdx];
					Temp_CMD0_Update = TempShiftCheck[SenIdx];
				}

				LogPrtCnt++;
				if(LogPrtCnt == 15) //15s
				{
					u32 soc_temp = ts_get();
					//disp_apl_trace(LOG_ALW, 0, "Idx:0,Temp:%d, Idx:1,Temp:%d soc: %d", TempShiftCheck[0]-273, TempShiftCheck[1]-273, soc_temp);
					disp_apl_trace(LOG_ALW, 0x9cd2, "Idx:0,Temp:%d, soc: %d", TempShiftCheck[0]-273, soc_temp);
                    temperature = TempShiftCheck[0] - 273;
					LogPrtCnt = 0;
				}

    			SenRetryCnt = 0;
				TempShiftCnt = 0;
			}
			ddr_modify_refresh_time(Temp_CMD0_Update);
			SenIdx++;
			if (SenIdx >= (MAX_TEMP_SENSOR))		//Sensor Index Loop
				SenIdx = 0;
			Detect_FLAG = 0;
        	mod_timer(&GetSensorTemp_timer, jiffies + HZ);

		}
	}
}
extern smart_statistics_t *smart_stat;
extern volatile u32 GrowPhyDefectCnt; //for SMART growDef Use
slow_code void LedCtl(u32 param, u32 payload, u32 count)//joe slow->ddr 20201124
{
	//extern struct evt_data *eds = &eds_array[evt_led_set];
	u32 temp; //temp1;
	extern bool fgLEDBlink;
	extern u16 fgLEDBlink_cnt;

	if ((LEDparam != 0)&&(param != 0))
	{ //uart set to close or open led
		u64 cur_tsc = get_tsc_64();
		//u32 Badblk = 0;
		temp = readl((void *)(MISC_BASE + GPIO_OUT));
		//eds->param = LedSwitch; to do
		if (((cur_tsc - LedDetectTime) > (CPU_CLK >> 2)) || ((cur_tsc < LedDetectTime) && (cur_tsc > (CPU_CLK >> 2))))//DAS(LED) frequency 250ms
		{
			LedDetectTime = cur_tsc;
			if ((ctrlr->admin_running_cmds > ctrlr->aer_outstanding) || (ctrlr->cmd_proc_running_cmds > 0) || (rd_io.running_cmd > 0) || (wr_io.running_cmd > 0)
				|| ((pGList->wGL_Mark_Cnt > 100) || (pGList->dCycle)) || (EcTbl->header.AvgEC > MAX_AVG_EC) || smart_stat->critical_warning.bits.volatile_memory_backup || fgLEDBlink)
			{
				if(fgLEDBlink_cnt == 0)
				{
					writel((temp) | (BIT(2 + GPIO_OUT_SHIFT)) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT)); //light
				}
				else
				{
					writel((temp & (~(BIT(2 + GPIO_OUT_SHIFT)))) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT));//dark
				}

				if(fgLEDBlink_cnt > 2)
				{
					fgLEDBlink_cnt = 0;
				}
				else
				{
					fgLEDBlink_cnt++;
				}

				fgLEDBlink = false;
			}
			else
			{

				writel((temp & (~(BIT(2 + GPIO_OUT_SHIFT)))) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT));//always dark
				fgLEDBlink_cnt = 0;
			}
		}
	}
	else
	{
		//eds->param = LedSwitch;
		temp = readl((void *)(MISC_BASE + GPIO_OUT));
		writel((temp & (~(BIT(2 + GPIO_OUT_SHIFT)))) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT));
		fgLEDBlink_cnt = 0;
	}
	evt_set_cs(evt_led_set, 0, 0, CS_TASK);
}

#if (UART_L2P_SEARCH == 1)
extern u32 l2pSrchUart;
ddr_code int uart_l2psrch(int argc, char *argv[]) // 20210413 Jamie slow_code -> ddr_code
{
	if (argc >= 2) {
		lda_t lda = strtol(argv[1], (void *)0, 0);
		u32 ldacnt = 1	;

		if (argc >= 3)
		{
			ldacnt = strtol(argv[2], (void *)0, 0);
		}
			disp_apl_trace(LOG_ALW, 0xeb41, "SSSTC1 lda 0x%x, ldacnt %d \n", lda, ldacnt);

			l2pSrchUart = 1;
			l2p_srch(lda, ldacnt, 0, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
		}
		else
		{
			disp_apl_trace(LOG_ALW, 0xb90d, "\nInvalid number of argument\n");
		}
	return 0;
}

static DEFINE_UART_CMD(l2p, "l2psrch",
					   "l2p [LDA] [srch count]",
					   "l2psrch",
					   1, 2, uart_l2psrch);
#endif
fast_data_zi u8 inflight_cmd_print_cnt;
//ddr_code void inflight_cmd_print(void* data)
fast_code void inflight_cmd_print(void* data)
{
    if(inflight_cmd_print_cnt == 59){
        u64 data_units_written = smart_stat->data_units_written + wr_io.host_du_cnt;
        u64 data_units_read = smart_stat->data_units_read + rd_io.host_du_cnt;

    	disp_apl_trace(LOG_ALW, 0x897b, "data_units_written %x %08x(x512B) data_units_read %x %08x(x512B)",
                data_units_written>>32,data_units_written,
                data_units_read>>32,data_units_read);
    }else if(inflight_cmd_print_cnt == 60){
        u64 host_write_commands = smart_stat->host_write_commands + wr_io.cmd_recv_cnt;
        u64 host_read_commands = smart_stat->host_read_commands + rd_io.cmd_recv_cnt;
        
    	disp_apl_trace(LOG_ALW, 0x1367, "host_write_commands %x %08x host_read_commands %x %08x poh 0x%x power cycle cnt 0x%x",
                host_write_commands>>32,host_write_commands,
                host_read_commands>>32,host_read_commands,
        		(poh + (jiffies / 36000)),
        		pc_cnt);
            inflight_cmd_print_cnt = 0;
    }else{
    	disp_apl_trace(LOG_ALW, 0xa2af, "inflight_cmd:%d-%d-%d-%d",
    		ctrlr->admin_running_cmds,
    		ctrlr->cmd_proc_running_cmds,
    		wr_io.running_cmd,
    		rd_io.running_cmd);
    }
    
    inflight_cmd_print_cnt++;
	mod_timer(&print_timer, jiffies + 8*HZ);
}

ddr_code void ipc_ondeman_dump(volatile cpu_msg_req_t *req)
{
    extern void btn_de_wr_dump(void);
    ucache_dump();
    btn_de_wr_dump();
}

ddr_code void ipc_feedback_set(volatile cpu_msg_req_t *req)
{
    cpu_feedback[CPU_ID_0] = true;
}

ddr_code void ascll_to_string(u8* data,u8 cnt)
{
	u8* temp;
	//char sn[32] = "";
	u8 i,j,k;
	for(temp=data, i=0; i < cnt; i++)
	{
		if((0x2f<temp[i]) && (temp[i]<0x3a))//num
		{
			j = temp[i] - 0x30;
			dri_sn[i] = ascll_num[j];
		}
		else if((0x40<temp[i]) && (temp[i]<0x5B))//alh
		{
			k = temp[i] - 0x41;
			dri_sn[i] = ascll_alp[k];
		}
	}
	evlog_printk(LOG_ALW, "SN: %s ",dri_sn);
}

ddr_code int sn_print(int argc, char *argv[]) // 20210413 Jamie slow_code -> ddr_code
{
	//ascll_to_string(MPIN->drive_serial_number,32);
	evlog_printk(LOG_ALW, "SN: %s ",dri_sn);
	return 0;
}

ddr_code int eeff(int argc, char *argv[]) // 20210413 Jamie slow_code -> ddr_code
{
	//ascll_to_string(MPIN->drive_serial_number,32);
	if(esr_err_fua_flag == 0)
		esr_err_fua_flag = 1;
	else
		esr_err_fua_flag = 0;

	evlog_printk(LOG_INFO, "esr_err_fua_flag: %d", esr_err_fua_flag);
	return 0;
}


static DEFINE_UART_CMD(sn, "sn_print",
					   "sn_print sn_print",
					   "sn_print",
					   0, 1, sn_print);

static DEFINE_UART_CMD(eeff, "eeff",
					   "eeff eeff",
					   "eeff",
					   0, 1, eeff);



init_code void LedInit(void)
{
	u32 temp2, temp1, temp;

	temp1 = readl((void *)(MISC_BASE + GPIO_PAD_CTRL));
	temp2 = readl((void *)(MISC_BASE + GPIO_CTRL));
	writel((temp2 & 0xFFFE0000) | BIT(GPIO_REG_EN_SHIFT) | (temp1 >> GPIO_IN_SHIFT), (void *)(MISC_BASE + GPIO_CTRL));
	temp = readl((void *)(MISC_BASE + GPIO_OUT));
	writel((temp | (BIT(2 + GPIO_OUT_SHIFT))) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT));
}
extern void ipc_get_spare_avg_erase_cnt_done(volatile cpu_msg_req_t *msg_req);
extern void ipc_get_additional_smart_info_done(volatile cpu_msg_req_t *msg_req);
extern void ipc_get_intel_smart_info_done(volatile cpu_msg_req_t *msg_req);
extern void ipc_get_new_ec_done(volatile cpu_msg_req_t *msg_req);
extern void ipc_get_new_ec_smart_done(volatile cpu_msg_req_t *msg_req);
extern void ipc_evlog_dump_ack(volatile cpu_msg_req_t *req);
extern void ipc_self_device_test_delay_done(volatile cpu_msg_req_t *req);


#if defined(SAVE_CDUMP) //_GENE_ 4cpu cdump
ddr_code void ipc_ulink_da_mode1(volatile cpu_msg_req_t *req)
{
	extern u32 ulink_da_mode(void);
	ulink_da_mode();
}
#endif

#if CO_SUPPORT_DEVICE_SELF_TEST

ddr_code void NvmeDeviceSelfTestInit(void)
{
    gCurrDSTOperation      = 0;
	gCurrDSTOperationNSID  = 0;
    gCurrDSTCompletion     = 0;
    gCurrDSTTime           = 0;
    gCurrDSTOperationImmed = 0;
    gDeviceSelfTest        = 0;

    //gDSTScratchBuffer = (u8*)MEM_AllocBuffer(8 * KBYTE, 4);
    //memset(gDSTScratchBuffer, 8 * KBYTE);

    if (smDSTInfo->Tag != 0x44535400)  //DST0
    {
        u8 idx;

        memset((u8*)&smDSTInfo->Tag, 0, sizeof(tDST_LOG));
        disp_apl_trace(LOG_ALW, 0x03ee, "[NVMe] DstInit");

        for (idx = 0; idx < 20; idx++)
        {
           smDSTInfo->DSTLogEntry[idx].DSTResult = cDSTEntryNotUsed;
        }
        smDSTInfo->Tag = 0x44535400;  //DST0
    }

    if (smDSTInfo->DSTResult.DSTCode)
    {
		disp_apl_trace(LOG_ALW, 0xee36, "[NVMe] resune DST|%x",smDSTInfo->DSTResult.DSTCode);

		if (smDSTInfo->DSTResult.DSTCode == cDST_SHORT)
        {
            smDSTInfo->DSTResult.DSTResult = cDSTAbortReset;
            HandleSaveDSTLog();
        }
        else
        {
            gDeviceSelfTest = cDST_EXTEN_DATA_INTEGRITY;
			gLastDSTTime=smDSTInfo->DSTSpendTime;
            DST_Operation(cDST_EXTENDED, smDSTInfo->DSTResult.NSID);
            DST_ProcessCenter(0,0,0);
        }
    }
}

//-----------------------------------------------------------------------------
/**
    DST OPERATION - 1h 2h

    @param[in]
**/
//-----------------------------------------------------------------------------
ddr_code void DST_Operation(u8 stc, u32 nsid)
{
    gCurrDSTOperation = stc;
	gCurrDSTOperationNSID = nsid;
    smDSTInfo->DSTResult.DSTCode = stc;
	smDSTInfo->DSTResult.NSID = nsid;
    smDSTInfo->DSTResult.DSTResult = 0;
    smDSTInfo->DSTResult.Segment = 0;
    gCurrDSTOperationImmed = 1;
    gCurrDSTCompletion = 1;
    gCurrDSTTime = get_tsc_64();
	DST_abort_flag = false;
	DST_pard_fail_flag = false;
	memset((void *)DST_pard_fail_LBA, 0, sizeof(u32)*2);
}

//-----------------------------------------------------------------------------
/**
    SMART Self Test handle

    @param[in]
**/
//-----------------------------------------------------------------------------
/*ddr_code void DST_ProcessCenter_delay(u32 p0, u32 p1, u32 p2){
	for (u32 i = 0; i < 3000; i++){
		mdelay(1);
		cpu_msg_isr();
	}
	evt_set_cs(evt_dev_self_test, 0, 0, CS_TASK);
}

ddr_code void ipc_self_device_test_delay_done(volatile cpu_msg_req_t *msg_req)
{
	if (gDeviceSelfTest != cDST_INIT){
		DST_ProcessCenter(0, 0, 0);
	}
}*/
ddr_code void DST_speed_control(void *data)
{
	disp_apl_trace(LOG_INFO, 0x74bc,"DST speed control");
	DST_ProcessCenter(0, 0, 0);
}


ddr_code void DST_ProcessCenter(u32 p0, u32 p1, u32 p2)
{
    u8  segmentFail = 0;
	extern bool esr_err_flags;
	extern bool ddr_rw_device_self_test(void);
	extern u64 gDST_total_timer;
	//u64 cost_time;
    switch (gDeviceSelfTest)  //1-5 short, 6-9 extended
    {
//--------------------------Short Test------------------------------------------------//
        case cDST_SHORT_RAM_CHECK:  // 1 RAM Check
        	gDST_total_timer = get_tsc_64();//Short test clock
#if (PLP_SUPPORT == 0)
			epm_update(SMART_sign, (CPU_ID-1));//for non-PLP DST short test log
#endif

			mod_timer(&DST_timer, jiffies + 10*HZ);

			if (ddr_rw_device_self_test())
			{
				disp_apl_trace(LOG_INFO, 0x1758,"[DST] dram error");
			    segmentFail = cDST_SHORT_RAM_CHECK;
			    gDeviceSelfTest = cDST_DONE;
			}
			else
			{
				disp_apl_trace(LOG_INFO, 0xe1df,"[DST] dram pass");
			    gDeviceSelfTest = cDST_SHORT_SMART_CHECK;
			}
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ShortDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0x6d37,"[DST] Hard time out Abort a short DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
			gCurrDSTCompletion = 25;
            break;
        case cDST_SHORT_SMART_CHECK:  // 2 SMART Check
			mod_timer(&DST_timer, jiffies + 10*HZ);

            if (smart_stat->critical_warning.raw)//Eric 20240103
            {
            	disp_apl_trace(LOG_INFO, 0x167a,"[DST] Smart error");
                segmentFail = cDST_SHORT_SMART_CHECK;
                gDeviceSelfTest = cDST_DONE;
            }
            else
            {
            	disp_apl_trace(LOG_INFO, 0x7d0e,"[DST] smart pass");
                gDeviceSelfTest = cDST_SHORT_VOLATILE_MEMORY;
            }
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ShortDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0x826a,"[DST] Hard time out Abort a short DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
			gCurrDSTCompletion = 50;
            break;
        case cDST_SHORT_VOLATILE_MEMORY:  // 3 Volatile memory backup
			mod_timer(&DST_timer, jiffies + 10*HZ);

		 	if (esr_err_flags)//Eric 20240103
            {
            	disp_apl_trace(LOG_INFO, 0x7df4,"[DST] PLP error");
                segmentFail = cDST_SHORT_VOLATILE_MEMORY;
                gDeviceSelfTest = cDST_DONE;
            }
            else
            {
            	disp_apl_trace(LOG_INFO, 0x59de,"[DST] PLP pass");
                gDeviceSelfTest = cDST_SHORT_METADATA_VALIDATION;
            }
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ShortDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0x058e,"[DST] Hard time out Abort a short DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
			gCurrDSTCompletion = 75;
            break;

        case cDST_SHORT_METADATA_VALIDATION:  // 4 Metadata validation
			mod_timer(&DST_timer, jiffies + 10*HZ);

			if(pGList->wGL_ECC_Mark_Cnt >= GL_TOTAL_ECCT_ENTRY_CNT){
				disp_apl_trace(LOG_INFO, 0xea41,"[DST] Glist error");
				segmentFail = cDST_SHORT_METADATA_VALIDATION;
				gDeviceSelfTest = cDST_DONE;
			}
			else{
            	gDeviceSelfTest = cDST_SHORT_NVM_INTEGRITY;
				disp_apl_trace(LOG_INFO, 0xd5e6,"[DST] Glist pass");
			}
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ShortDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0x8e46,"[DST] Hard time out Abort a short DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
			gCurrDSTCompletion = 90;
            break;

        case cDST_SHORT_NVM_INTEGRITY:  // 5 NVM integrity
			disp_apl_trace(LOG_INFO, 0x7b33,"[DST] NVM Intergrity No support");
			mod_timer(&DST_timer, jiffies + 10*HZ);
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ShortDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0xf5cb,"[DST] Hard time out Abort a short DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
			gDeviceSelfTest = cDST_DONE;
            break;
//--------------------------Extended Test------------------------------------------------//
        case cDST_EXTEN_DATA_INTEGRITY:  // 6 Data Integrity
        	gDST_total_timer = get_tsc_64();//Extended test clock
#if (PLP_SUPPORT == 0)
        	epm_update(SMART_sign, (CPU_ID-1));//for non-PLP DST extended test resume
#endif
			disp_apl_trace(LOG_INFO, 0x9f1f,"[DST] Extended start");
			mod_timer(&DST_timer, jiffies + 45*HZ);
			gDeviceSelfTest = cDST_EXTEN_MEDIA_CHECK;
			gCurrDSTCompletion = 25;
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ExtenDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0x20e8,"[DST] Hard time out Abort a DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
            break;

        case cDST_EXTEN_MEDIA_CHECK:  // 7 Media Check
			if (gCurrDSTOperationNSID)
			{
				
				disp_apl_trace(LOG_INFO, 0xbc41,"[DST] Patrol testing flag:%d", DST_pard_done_flag);
				//==============hard time out ===============//
				if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ExtenDSTTime*1000)
					{
						disp_apl_trace(LOG_ALW, 0x98c5,"[DST] Hard time out Abort a DST");
						DST_Completion(cDSTFatalErr);
						return;
					}
				//===========================================//
				DST_pard_done_flag = false;
				extern bool _fg_fwupgrade_stopPTRD;
				_fg_fwupgrade_stopPTRD = false;

				cpu_msg_issue(CPU_FTL - 1, CPU_MSG_DST_PATROL, 0, 0);

				if(!DST_pard_done_flag)
				{
					disp_apl_trace(LOG_INFO, 0x640b,"[DST] Patrol 7 up flag:%d", DST_pard_done_flag);
					gDeviceSelfTest = cDST_pass_patrol;
				}
				else
				{
					disp_apl_trace(LOG_INFO, 0x6350,"[DST] Patrol 7 down flag:%d", DST_pard_done_flag);
					gDeviceSelfTest = cDST_EXTEN_DRIVE_LIFE;
				}
				mod_timer(&DST_timer, jiffies + 40*HZ);
				gCurrDSTCompletion = 49;
	            break;
			}
			else //Leslie nsid0
			{
				mod_timer(&DST_timer, jiffies + 1*HZ);
				disp_apl_trace(LOG_INFO, 0x78b3,"[DST] nsid0 skip patrol read");
				gDeviceSelfTest = cDST_EXTEN_DRIVE_LIFE;
				gCurrDSTCompletion = 50;
				//==============hard time out ===============//
				if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ExtenDSTTime*1000)
				{
					disp_apl_trace(LOG_ALW, 0x10bb,"[DST] Hard time out Abort a DST");
					DST_Completion(cDSTFatalErr);
					return;
				}
				//===========================================//
	            break;
			}

		case cDST_pass_patrol:  // 7..5 Media Check
			//disp_apl_trace(LOG_INFO, 0x91c2,"[DST] pass patrol");
			//==============hard time out ===============//
			//if(time_elapsed_in_ms(gDST_total_timer)>3600000)
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ExtenDSTTime*1000)
				{
					disp_apl_trace(LOG_ALW, 0x0fe1,"[DST] Hard time out Abort a DST");
					DST_Completion(cDSTFatalErr);
					return;
				}
			//===========================================//
			 if(!DST_pard_done_flag)
			 	{
			 	disp_apl_trace(LOG_INFO, 0x017d,"[DST] Patrol 7.5 up flag:%d", DST_pard_done_flag);
				 gDeviceSelfTest = cDST_pass_patrol;
			 	}
			 else
			 	{
			 	disp_apl_trace(LOG_INFO, 0xdab7,"[DST] Patrol 7.5 down flag:%d", DST_pard_done_flag);
				 gDeviceSelfTest = cDST_EXTEN_DRIVE_LIFE;
			 	}

			if(DST_pard_fail_flag)
			{
				disp_apl_trace(LOG_INFO, 0xfa3a,"[DST] Media Check error");
				segmentFail = cDST_EXTEN_MEDIA_CHECK;

				smDSTInfo->DSTResult.FLBA[0] = DST_pard_fail_LBA[0];
				smDSTInfo->DSTResult.CodeType = NVME_SCT_MEDIA_ERROR;
				smDSTInfo->DSTResult.StatusCode = NVME_SC_UNRECOVERED_READ_ERROR;
				gDeviceSelfTest = cDST_DONE;
			}
			mod_timer(&DST_timer, jiffies + 50*HZ);
			gCurrDSTCompletion = 50;
            break;

        case cDST_EXTEN_DRIVE_LIFE:  // 8 Drive Life
        	mod_timer(&DST_timer, jiffies + 45*HZ);

			if (smart_stat->percentage_used > 254)//Eric 20240103
            {
            	disp_apl_trace(LOG_INFO, 0x8a58,"[DST] drive life error");
                segmentFail = cDST_EXTEN_DRIVE_LIFE;
                gDeviceSelfTest = cDST_DONE;
            }
            else
            {
            	disp_apl_trace(LOG_INFO, 0xaffb,"[DST] dive life pass");
                gDeviceSelfTest = cDST_EXTEN_SMART_CHECK;
            }
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ExtenDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0x7098,"[DST] Hard time out Abort a DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
			gCurrDSTCompletion = 75;
            break;

        case cDST_EXTEN_SMART_CHECK:  // 9 SMART Check
        	mod_timer(&DST_timer, jiffies + 10*HZ);

			if (smart_stat->critical_warning.raw)
            {
            	disp_apl_trace(LOG_INFO, 0x3d07,"[DST] extended smart error");
                segmentFail = cDST_EXTEN_SMART_CHECK;
            }
			else{
				disp_apl_trace(LOG_INFO, 0xca1d,"[DST] extended smart pass");
			}
			//==============hard time out ===============//
			if(time_elapsed_in_ms(gDST_total_timer)+gLastDSTTime>ExtenDSTTime*1000)
			{
				disp_apl_trace(LOG_ALW, 0x3002,"[DST] Hard time out Abort a DST");
				DST_Completion(cDSTFatalErr);
				return;
			}
			//===========================================//
			gCurrDSTCompletion = 90;
            gDeviceSelfTest = cDST_DONE;
            break;
        default:
            gDeviceSelfTest = cDST_INIT;
    }

    if (gDeviceSelfTest == cDST_DONE)
    {
		disp_apl_trace(LOG_INFO, 0x29ce,"DST Done");
        gDeviceSelfTest = cDST_INIT;
        smDSTInfo->DSTResult.Segment = segmentFail;
		if(segmentFail == 0){		
			disp_apl_trace(LOG_INFO, 0x6dd8,"[DST] DST Complete");
        	DST_Completion(cDSTCompleted);
		}
		else{		
			disp_apl_trace(LOG_INFO, 0xf11a,"[DST] DST Complete but segment error|%x",segmentFail);
			DST_Completion(cDSTCompletedSegmentNum);
		}
        gCurrDSTOperation  = 0;
		gCurrDSTOperationNSID  = 0;
        gCurrDSTCompletion = 0;
    }

}


//-----------------------------------------------------------------------------
/**
    DST Cmd Abort

    @param[in]
**/
//-----------------------------------------------------------------------------
ddr_code void DST_CmdAbort(req_t *req, u8 stc)
{
    if (gCurrDSTOperation)
    {
    	DST_abort_flag = true;
        DST_Completion(cDSTAbort);
		disp_apl_trace(LOG_ALW, 0x6c24,"[DST] Abort a DST");
        // nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_DEVICE_SELF_TEST_IN_PROGRESS);
    }
}

//-----------------------------------------------------------------------------
/**
    DST COMPLETION

    @param[in]
**/
//-----------------------------------------------------------------------------
ddr_code void DST_Completion(u8 Result)
{
    if (gCurrDSTOperation)
    {
        if ((smDSTInfo->DSTResult.DSTCode == cDST_SHORT) || (Result != cDSTAbortReset))
        {
            smDSTInfo->DSTResult.DSTResult = Result;
            gCurrDSTOperationImmed  = 0;// in process or not
            gDeviceSelfTest         = 0;
            disp_apl_trace(LOG_ALW, 0xe566,"DSTCompletion Result=%x",Result);
            HandleSaveDSTLog();
        }
		else
		{
			
			smDSTInfo->DSTSpendTime += time_elapsed_in_ms(gCurrDSTTime);
			gLastDSTTime=smDSTInfo->DSTSpendTime;

		}
    }
}

//-----------------------------------------------------------------------------
/**
    Handle Save DST Log

    @param[in]
**/
//-----------------------------------------------------------------------------
extern void smart_update_power_on_time(void);

ddr_code void HandleSaveDSTLog(void)
{
    u8  DSTIdx;
    u8  Valid = 0;

    smDSTInfo->TotalDSTCnt++;

    for (DSTIdx = (20 - 1); DSTIdx > 0; DSTIdx--)
    {
        memcpy((u8*)&smDSTInfo->DSTLogEntry[DSTIdx], (u8*)&smDSTInfo->DSTLogEntry[(DSTIdx - 1)], sizeof(tDST_LOG_ENTRY));
    }

    if(smDSTInfo->DSTResult.Segment != 0)
	{
    	//if (smDSTInfo->DSTResult.NSID)           Valid |= 0x01;
    	//if (smDSTInfo->DSTResult.FLBA[0])        Valid |= 0x02;
    	if (DST_pard_fail_flag)        			 Valid |= 0x03;	  //FLBA & NSID
		if (smDSTInfo->DSTResult.CodeType)       Valid |= 0x04;
    	if (smDSTInfo->DSTResult.StatusCode)     Valid |= 0x08;
	}

    smDSTInfo->DSTResult.ValidDiagnosticInfo  = Valid;

	smart_update_power_on_time();	//update power_on_minutes

	smDSTInfo->DSTResult.POH[0] = smart_stat->power_on_minutes / 60;

    memcpy((u8*)&smDSTInfo->DSTLogEntry[0], (u8*)&smDSTInfo->DSTResult, sizeof(tDST_LOG_ENTRY));

    memset((u8*)&smDSTInfo->DSTResult, 0, sizeof(tDST_LOG_ENTRY));
	smDSTInfo->DSTSpendTime = 0;

    epm_update(SMART_sign, (CPU_ID - 1));

    gCurrDSTOperation = 0;
	gCurrDSTOperationNSID  = 0;
    gCurrDSTCompletion = 0;
	gLastDSTTime=0;

	//disp_apl_trace(LOG_ALW, 0xe076,"[chk] NSID|%x ValidDiagnosticInfo|%x",smDSTInfo->DSTResult.NSID, smDSTInfo->DSTResult.ValidDiagnosticInfo);
}

//-----------------------------------------------------------------------------
/**
    SMART Self Test handle

    @param[in]
**/
//-----------------------------------------------------------------------------
ddr_code void DSTOperationImmediate()
{
    // U32 CurrentTestTime;
    // U32 TestTime = 0;
    // u8 TimeOutFlag = FALSE;
	//
    // if (!gCurrDSTOperationImmed) return;
	//
    // CurrentTestTime = HalTimer_GetElapsedTimeMsec(gCurrDSTTime);
	//
    // if (gCurrDSTOperation == cDST_SHORT)
    // {
    //     TestTime = (ShortDSTTime * 1000);
    // }
    // else if (gCurrDSTOperation == cDST_EXTENDED)
    // {
    //     TestTime = (ExtenDSTTime * 1000);
    // }
	//
    // if (CurrentTestTime >= TestTime)
    // {
    //     TimeOutFlag = TRUE;
    // }
	//
    // if (smDSTInfo->DSTResult.Segment)
    // {
    //     disp_apl_trace(LOG_ALW,"[DST ] Segment|%8X\n",smDSTInfo->DSTResult.Segment);
	//
    //     DST_Completion(cDSTCompletedSegmentNum);
    // }
    // else if (smDSTInfo->DSTErr)
    // {
    //     DST_Completion(smDSTInfo->DSTErr);
    // }
    // else if (TimeOutFlag)
    // {
    //     //disp_apl_trace(LOG_ALW,"DSTOperation  TimeOut  %8X  \n",CurrentTestTime);
    //     DST_Completion(cDSTCompleted);
    // }
    // else
    // {
    //     gCurrDSTCompletion = (100 * CurrentTestTime / TestTime);
	//
    //     if (!gCurrDSTCompletion) gCurrDSTCompletion = 1;
    // }
}
#endif

init_code static void rdisk_init_fe(void)
{
	nvme_ctrl_attr_t ctrl_attr;

	wait_remote_item_done(nand_geo_init);


	rdisk_ddtag_prep();
    patrol_read_init();
	share_heap_init();
	ipc_api_init();
	local_item_done(tag_meta_init);

	// wait BE init done
	wait_remote_item_done(be_init);

	//ipc_api_init();  //moveup alan
	cpu_msg_register(CPU_MSG_READ_RECOVERIED_COMMIT, ipc_read_recoveried_commit);
	cpu_msg_register(CPU_MSG_FC_CTX_DONE, ipc_ftl_core_ctx_done);
	cpu_msg_register(CPU_MSG_XFER_RC_DATA, ipc_xfer_rc_data);
	cpu_msg_register(CPU_MSG_GC_ACT_DONE, ipc_gc_action_done);
	cpu_msg_register(CPU_MSG_FE_REQ_OP_DONE, ipc_fe_req_op_done);
	cpu_msg_register(CPU_MSG_L2P_LOAD_DONE, ipc_l2p_load_done);
	cpu_msg_register(CPU_MSG_BTN_CMD_DONE, ipc_btn_cmd_done);
	cpu_msg_register(CPU_MSG_FWDL_OP_DONE, ipc_fwdl_op_done);
	cpu_msg_register(CPU_MSG_GET_SPARE_AVG_ERASE_CNT_DONE, ipc_get_spare_avg_erase_cnt_done);
	cpu_msg_register(CPU_MSG_GET_NEW_EC_DONE, ipc_get_new_ec_done);
	cpu_msg_register(CPU_MSG_GET_NEW_EC_SMART_DONE, ipc_get_new_ec_smart_done);
	cpu_msg_register(CPU_MSG_GET_ADDITIONAL_SMART_INFO_DONE, ipc_get_additional_smart_info_done);
#if (Xfusion_case)
	cpu_msg_register(CPU_MSG_GET_INTEL_SMART_INFO_DONE, ipc_get_intel_smart_info_done);
#endif
    cpu_msg_register(CPU_MSG_PLA_PULL, ipc_pla_pull);
    cpu_msg_register(CPU_MSG_IO_CMD_SWITCH, ipc_io_cmd_switch);
#if(FULL_TRIM == ENABLE)
	cpu_msg_register(CPU_MSG_FTL_FULL_TRIM_HANDLE_DONE, ipc_ftl_full_trim_handle_done);
#endif
/*#if CO_SUPPORT_DEVICE_SELF_TEST
		cpu_msg_register(CPU_MSG_DT_DELAY_DONE, ipc_self_device_test_delay_done);
#endif*/
#if (((PLP_SLC_BUFFER_ENABLE == mENABLE) || (SPOR_FTLINITDONE_SAVE_QBT == mENABLE)) && (BG_TRIM == ENABLE))
	cpu_msg_register(CPU_MSG_CHK_BG_TRIM, ipc_chk_bg_trim);
#endif

#if 0//RAID_SUPPORT_UECC
	cpu_msg_register(CPU_MSG_UPDATE_SMART_STATE, ipc_nvmet_update_smart_stat);
#endif

#if PLP_DEBUG
	cpu_msg_register(CPU_MSG_PLP_DEBUG_FILL, ipc_plp_debug);
#endif
	//sw_ipc_msg_register(CPU_MSG_PLP_FORCE_FLUSH, ipc_plp_force_flush);
#if 0 //for vu
	cpu_msg_register(CPU_MSG_EVT_VUCMD_SEND,be_do_vu_command); //_GENE_
#endif
	cpu_msg_register(CPU_MSG_PLP_DONE, ipc_plp_done_ack);
	cpu_msg_register(CPU_MSG_ENTER_READ_ONLY_MODE, ipc_enter_read_only_handle);
	cpu_msg_register(CPU_MSG_LEAVE_READ_ONLY_MODE, ipc_leave_read_only_handle);
	cpu_msg_register(CPU_MSG_DISABLE_BTN, ipc_disable_btn);
    cpu_msg_register(CPU_MSG_EVLOG_DUMP_ACK, ipc_evlog_dump_ack);
#if (FW_BUILD_VAC_ENABLE == mENABLE)
	cpu_msg_register(CPU_MSG_VC_RECON, l2p_vac_recon);
#endif
#if defined(SAVE_CDUMP) //_GENE_ 4cpu cdump
        cpu_msg_register(CPU_MSG_ULINK, ipc_ulink_da_mode1);
#endif
	cpu_msg_register(CPU_MSG_ONDEMAND_DUMP, ipc_ondeman_dump);
	cpu_msg_register(CPU_MSG_LOOP_FEEDBACK, ipc_feedback_set);

	rdisk_alloc_streaming_rw_dtags();

	evt_register(rdisk_com_free_updt_pasg, 0, &evt_com_free_upt);
	evt_register(rdisk_wd_updt_nrm_par, 0, &evt_wd_grp0_nrm_par_upt);
#if(PLP_SUPPORT == 1)
    evt_register(mtp_check_task, 0, &evt_mtp_check);
#endif
	evt_register(plp_set_ENA, 0, &evt_plp_set_ENA);
	cpu_msg_register(CPU_MSG_PLP_SET_ENA, ipc_plp_set_ENA);
	evt_register(rdisk_pln_format_sanitize, 0, &evt_pln_format_sanitize);

	evt_register(rdisk_wd_updt_pdone, 0, &evt_wd_grp0_pdone_upt);
	evt_register(rdisk_rd_updt, 0, &evt_rd_ent_upt);
	//evt_register(rdisk_power_state_change, 0, &evt_change_ps);
	evt_register(rdisk_wait_wcmd_idle, 0, &evt_wait_wcmd_idle);
	evt_register(rdisk_ftl_ready_chk, 0, &evt_ftl_ready_chk);
	evt_register(rdisk_l2p_swq_chk, 0, &evt_l2p_swq_chk);
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	evt_register(rdisk_otf_readcmd_chk, 0, &evt_otf_readcmd_chk);
	#endif
	evt_register(rdisk_req_exec_chk, 0, &evt_req_exec_chk);
	evt_register(rdisk_fw_download, 0, &evt_fw_dwnld);
	evt_register(rdisk_fw_commit, 0, &evt_fw_commit);
	//evt_register(rdisk_reset_disk_handler, 0, &evt_reset_disk);
	evt_register(LedCtl, 1, &evt_led_set);//led mask
	evt_register(rdisk_wunc_next_part, 0, &evt_rdisk_wunc_part);
	//evt_register(rdisk_fe_wr_exec_chk, 0, &evt_r_exec_chk);
	evt_register(evt_rdisk_flush_done, 0, &evt_flush_done);
#if CO_SUPPORT_DEVICE_SELF_TEST
	evt_register(DST_ProcessCenter, 0, &evt_dev_self_test);
	//evt_register(DST_ProcessCenter_delay, 0, &evt_dev_self_test_delay);
#endif
	#if CPU_ID == 1
	#if CO_SUPPORT_PANIC_DEGRADED_MODE
	assert_evt_register(Host_ServiceLoopAssert, 0, evt_degradedMode);
	#endif
	#endif
#if 0//TCG_WRITE_DATA_ENTRY_ABORT
	evt_register(rdisk_tcg_mbr_buf_rdy, 0, &evt_tcg_mbr_buf_rdy);
#endif
	evt_set_cs(evt_led_set, 0, 0, CS_TASK);//led mask

	btn_callbacks_t callbacks = {
		.hst_strm_rd_err = rdisk_bm_read_error,
		.write_err = rdisk_err_wde_handle};
	btn_callback_register(&callbacks);

	rdisk_hw_l2p_fe_init();

	dtag_free_init();

	ua_btag = UA_OFF;
	wunc_btag = WUCC_OFF;

#if WR_DTAG_TYPE == DTAG_T_SRAM
#else
	dtag_register_caller_gc(DTAG_T_SRAM, rdisk_dtag_gc);
#endif

	wait_remote_item_done(l2p_init);

	/* ucache is resident in CPU_FE */
	ucache_init(ns[1 - 1].cap);
#if(PLP_SUPPORT == 1)
    plp_one_time_init();
#else
#if(BG_TRIM == ENABLE)
    chk_bg_trim();
#endif
#endif
	urg_evt_register(plp_evt, 0, evt_plp_flush);
	urg_evt_register(plp_wait_streaming_finish_evt, 0, evt_check_streaming);
#ifdef ERRHANDLE_ECCT
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
    ecct_cnt = pECC_table->ecc_table_cnt;     //Search same idx of lda in ecct
    //disp_apl_trace(LOG_INFO, 0, "[DBG] rdisk_init_fe: INIT ECCT cnt: 0x%x", ecct_cnt);
#endif

	btn_cmd_hook(bcmd_exec, bcmd_fast_read);

	_pat_gen_test_id = pat_gen_user_register(uart_pat_gen_done);
	cpu1_btcm_free_start = (u32)&__btcm_data_ni_start;
	pmu_register_handler(SUSPEND_COOKIE_FTL, rdisk_suspend,
						 RESUME_COOKIE_FTL, rdisk_resume);
#ifdef NS_MANAGE
	ns_array_managa_init(); //joe add NS 20200813
#endif
	rdisk_fe_info_restore();

///Andy test
#if ((defined(USE_CRYPTO_HW)) && (_TCG_ == TCG_NONE))
	///Load AES key///Load AES key
	u8 cryptID;
//move to preformat & create NS create key
#if 0
	//epm_aes_t* epm_aes_data = (epm_aes_t*)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	//u32 *crpto_information = (u32*)(&epm_aes_data->data[0]);
	//EPM_crypto_info_t *crpto_information = (EPM_crypto_info_t*)(&epm_aes_data->data[0]);
	//if(crpto_information[0] != 0xDEADFACE)
	{
		///Generate AES key
		//for(cryptID=0 ; cryptID<64 ; cryptID++)
		{
			//crypto_change_mode_range(1,0,1,0);
		}
	}
#endif
	///Reload all crypto info from EPM info
	for (cryptID = 0; cryptID < 64; cryptID++)
	{
		crypto_AES_EPM_Read(cryptID, 0);
	}

#endif

	memset((void*)&smResetHandle, 0, sizeof(ResetHandle_t));

	ctrl_attr.all = 0;
#if (TRIM_SUPPORT == ENABLE)
	ctrl_attr.b.dsm = 1;
#else
	ctrl_attr.b.dsm = 0;
#endif
	ctrl_attr.b.set_feature_save = 1;
    ctrl_attr.b.write_zero = CO_SUPPORT_WRITE_ZEROES;
    ctrl_attr.b.compare = CO_SUPPORT_COMPARE;
	///Andy IOL WUC 20201020
	ctrl_attr.b.write_uc = CO_SUPPORT_WRITE_UNCORRECTABLE;
	ctrl_attr.b.timestamp = 1;
	nvmet_set_ctrlr_attrs(&ctrl_attr);

	rdisk_fe_ns_restore();

	memset(&req_statistics, 0, sizeof(rdisk_req_statistics_t));
	bg_training_init();
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_init(rdisk_ra_bcmd_resume);
	#endif
	LedInit();
	disp_apl_trace(LOG_INFO, 0x88e6, "ENTER GetSensorTemp config");
	GetSensorTemp_timer.function = GetSensorTemp;
	GetSensorTemp_timer.data = "GetSensorTemp";
	if (misc_is_warm_boot() == true)
	{
		mod_timer(&GetSensorTemp_timer, jiffies + 1);
	}
	else
	{
		mod_timer(&GetSensorTemp_timer, jiffies + 2*HZ);
	}
//for print
    inflight_cmd_print_cnt = 59;
    inflight_cmd_print(NULL);
    inflight_cmd_print(NULL);
	print_timer.function = inflight_cmd_print;
	print_timer.data = "for print";
	mod_timer(&print_timer, jiffies + 10*HZ);
#if PLP_SUPPORT == 0
	smart_timer.function = nvmet_update_smart_timer;
	smart_timer.data="for smart update and flush";
	mod_timer(&smart_timer, jiffies + 600*HZ);
#endif
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	INIT_LIST_HEAD(&telemetry_log_update_timer.entry);
	telemetry_log_update_timer.function = telemetry_log_update_timer_reset;
	telemetry_log_update_timer.data = "telemetry_log_update_timer";
#endif

#ifdef LJ1_WUNC
    wunc_ua[0] = wunc_ua[1] = INV_U32;
    wunc_bmp[0] = wunc_bmp[1] = 0;
#endif
#if (TRIM_SUPPORT == ENABLE)
    PowerOnInitTrimTable();
#endif
#if (_TCG_)

	wait_remote_item_done(tcg_nf_init);

	extern void tcg_if_onetime_init(bool bootup, bool buf_to_dram);
	tcg_if_onetime_init(true, true);
	for(u16 idx=0; idx<256; idx++)
		mbr_pl[idx].nvm_cmd_id = 0xFFFF;
#endif
	//20210426-Eddie-Move to further more loop_main()
	ascll_to_string(MPIN->drive_serial_number,32);
#if CO_SUPPORT_DEVICE_SELF_TEST
	NvmeDeviceSelfTestInit();
#endif
	disp_apl_trace(LOG_ALW, 0x0bfb, "FE done");
	
#if (FW_BUILD_VAC_ENABLE == mENABLE)
	while(CPU3_spor_state != SPOR_STATE_BUILD_DONE)
	{
		//cpu_msg_isr();
		if( vc_recon_busy[CPU_ID_0] == true )
		{
			l2p_vac_recon(NULL);
			break;
		}
	}
#endif

}

/*! \cond PRIVATE */
module_init(rdisk_init_fe, DISP_APL);
/*! \endcond */
/*! @} */

#if CPU_ID == CPU_FE
#if CO_SUPPORT_PANIC_DEGRADED_MODE
extern bool panic_occure[4];

ddr_code void rdisk_assert_reset_disk_handler(void)
{
	if ((ctrlr->admin_running_cmds - ctrlr->aer_outstanding) != 0 )
	{
		AplAdminCmdRelease();
	}

	while (!bcmd_list_empty(&bcmd_pending))
	{
		bcmd_list_pop_head(&bcmd_pending);
	}

	// rx cmd
	rxcmd_abort();

	// RW CMD
	btn_abort_all();
}

ddr_code void Host_AssertWaitShutDown(void)
{
	// set critical_warning

	//step 1. erase blk & backup data

	//step 2. backup sysinfo

	 //step 3. Flush Dummy Close Block

	//Start to service command
	while (true)
	{
		extern void l2p_isr_q0_srch(void);
		extern void bm_isr_slow(void);
		extern void Assert_cmd_proc_isr(void);
		extern void uart_poll(void);
		extern void btn_rcmd_rls_isr(void);

		Assert_nvmet_isr();//nvmet_isr()
		Assert_timer_isr();
		btn_rcmd_rls_isr();
		l2p_isr_q0_srch();
		bm_isr_slow();

		Assert_cmd_proc_isr();//cmd_proc_isr();
		cpu_msg_isr();
		uart_poll();
		if (urg_evt_chk())
			urg_evt_task_process();
#if defined(RDISK)
		sw_ipc_poll();
#endif
		evt_task_process_one();
	}
}

ddr_code void Host_ServiceLoopAssert(u32 r0, u32 r1, u32 r2)
{
	//Already In Assert
	//if(Assert_HostStart() == ASSERT_FUNC_FAIL) goto FailStep;

	//step 1. erase blk & backup data
	cmd_proc_non_read_write_mode_setting(true);
	// Copy data to DRAM & make Core cpu issue job
	//Host_AssertDataBackup();
	//FailStep:
	Host_AssertWaitShutDown();
}

ddr_code void Assert_Core_OneTimeInit(void)
{
	#if CPU_ID == 1
	if (smCPUxAssert)
	{
		assert_evt_set(evt_degradedMode, 0, 0);
	}
	#endif
}
#endif
#endif
