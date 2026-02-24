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
 * @brief nvme core layer
 *
 * \addtogroup decoder
 * \defgroup core core
 * \ingroup decoder
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_cfg.h"
#include "nvme_apl.h"
#include "nvmet.h"
#include "hal_nvme.h"
#include "cmd_proc.h"
#include "bf_mgr.h"
#include "event.h"
#include "assert.h"
#include "pmu.h"
#include "misc.h"
#include "hmb_cmd.h"
#include "hmb.h"
#include "srb.h"
#ifdef OC_SSD
#include "ocssd.h"
#endif
#include "smart.h"
#include "btn_export.h"
#include "irq.h"
#include "ipc_api.h"
#include "cmd_proc.h"
#include "ns.h"
#include "bg_training.h"
#include "console.h"
#include "ssstc_cmd.h"
#include "spin_lock.h"
#include "mpc.h"
#include "trim.h"
#include "fc_export.h"

#include "rdisk.h"
#include "epm.h"
#include "btn_cmd.h"
#if (CO_SUPPORT_READ_AHEAD == TRUE)
#include "ra.h"
#endif
#include "../../../ftl/ftl.h"

#if _TCG_ != TCG_NONE
#include "tcgcommon.h"
#include "tcg.h"
#include "tcg_if_vars.h"
#endif

/*! \cond PRIVATE */
#define __FILEID__ core
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define req2ctag(req)    (req - _reqs)		///< get ctag from request

#define REQ_RUNTIME_WARNING	(8 * HZ)		///< req runtime threshold to print warning message
#define REQ_TIMEOUT		(15 * HZ)		///< req internal timeout threshold
#define REQ_TIMEOUT_MS		(REQ_TIMEOUT * 100)	///< req internal timeout threshold in ms
#define NS_SIZE_GRANULARITY_CORE1	(0x200000000 >> LBA_SIZE_SHIFT1)//joe add 20200730 //joe add NS 20200813
#define NS_SIZE_GRANULARITY_CORE2	(0x200000000 >> LBA_SIZE_SHIFT2)//joe add 20200730 //joe add NS 20200813//joe add for sec size 20200817
#define NSSR_RST_DELAYA ENABLE		 //for jira_10. solution(4/12).shengbin yang 2023/11/15
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
extern __attribute__((weak)) void ncl_cmd_wait_completion();

/*! @brief shutdown callback function type */
#if defined(SRIOV_SUPPORT)
typedef void (*shutdown_cb_t)(u8 fun_id);
#else
typedef void (*shutdown_cb_t)(void);
#endif

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
static fast_data_ni struct nvmet_sq sqs[NVMET_RESOURCES_FLEXIBLE_TOTAL];	///< controller SQ, dTCM, 0 for ASQ
static fast_data_ni struct nvmet_cq cqs[NVMET_RESOURCES_FLEXIBLE_TOTAL];	///< controller CQ, dTCM, 0 for ACQ
static fast_data_zi struct nvmet_ctrlr g_ctrlr;			///< controller context
fast_data struct nvmet_ctrlr *ctrlr = &g_ctrlr;			///< controller context pointer

fast_data_zi req_t _reqs[NVME_ACTIVE_COMMAND_COUNT];		///< request buffer

#if(NSSR_RST_DELAYA == ENABLE)
fast_data_zi u64 reset_start_time;
fast_data_zi u8 reset_delay_cnt;
#endif

static fast_data_zi pool_t reqs_pool;				///< request pool

static fast_data_zi struct nvmet_namespace _ns[NVMET_NR_NS];	///< controller namespace
static fast_data_zi struct nvmet_nsid _nsid[NVMET_NR_NS];		///< controller namespace id

// introduce to replace above two variables, may co-exist for a while
slow_data_zi nvme_ns_attr_t _nvme_ns_info[NVMET_NR_NS];//joe change this to slow 20200813 as test
#if (NS_MANAGE == FEATURE_SUPPORTED)//joe add define 20200916
//joe add NS 20200813
//===========joe add for ns 20200730============//
//share_data struct ns_section_id ns_sec_id[32];
 //share_data u16 ns_sectotal[32];
//share_data u16 ns_valid_sector=0;//joe 20200528
 //share_data u16 drive_total_sector;//joe 20200528
 share_data volatile struct ns_section_id ns_sec_id[32];
//share_data u16 ns_sectotal[32];
//share_data u16 ns_valid_sector=0;//joe 20200528
share_data volatile u16 *sec_order_point;//joe add 20200825  for hcrc faster
share_data volatile u16 drive_total_sector;//joe 20200528
share_data volatile u16 ns_valid_sec;
share_data volatile struct ns_array_manage *ns_array_menu;//joe 20200616 add ns_manage to struct
fast_data_zi u16 ns_trans_sec[1024];//joe add 202012 for performance
slow_data_zi u16 ns_sec_order[1024];//joe add 202012
//share_data u16 ns_sec_order[1024];//joe add 202012
share_data volatile u16 *sec_order_p;
share_data volatile u32 er_blk_cnt = 0;

//=========joe add for ns 20200730=============//
#endif

// default LBA format setting
#if (NS_MANAGE == FEATURE_SUPPORTED)//joe add define 20200916
	#if defined(HMETA_SIZE)
		slow_data lbaf_entry_t _lbaf_tbl[MAX_LBAF] = {//joe test 512/0 4096/0 202008
			{ .ms = 0, .lbads = 9, .rp = 1, },
			{ .ms = 8, .lbads = 9, .rp = 3, },
			{ .ms = 0, .lbads = 12, .rp = 0, },
			{ .ms = 8, .lbads = 12, .rp = 2, }
		};
	#else
		slow_data lbaf_entry_t _lbaf_tbl[MAX_LBAF] = {
			{ .ms = 0, .lbads = 9, .rp = 0, },
			{ .ms = 0, .lbads = 12, .rp = 0, }
		};
	#endif

#else
// default LBA format setting
fast_data lbaf_entry_t _lbaf_tbl[MAX_LBAF] = {
	{ .ms = 0, .lbads = HLBASZ, .rp = 0, },
	{ .ms = 8, .lbads = HLBASZ, .rp = 0, }
};
#endif

static fast_data_zi struct timer_list sq_timer;			///< monitor sq process
static fast_data_zi struct timer_list wb_timer;			///< warm boot process
#if (PI_FW_UPDATE == mENABLE)
static fast_data_zi struct timer_list pi_timer;
static fast_data_zi struct timer_list pi_fwdl_timer;			///< PI fwdl hadle process
static fast_data_zi struct timer_list pi_fwca_timer;			///< PI fwca hadle process
extern u8 cmf_idx;
#endif
static fast_data_zi shutdown_cb_t shutdown_cb;			///< shutdown callback function
fast_data_zi u8 shudown2000_flag = 0;			///< shutdown callback function
fast_data u8 evt_dtag_ins = 0xff;			///< event to force one dtag insertion to BTN to clean WR_DTAG_REQ_PENDING bit in BTN status
#if UART_STARTWB
share_data_ni volatile struct timer_list startwb_timer;
#endif
fast_data u8 evt_reset_disk = 0xff;			///< event to reset disk
fast_data u8 evt_abort_wr_req = 0xff;			///< event for dispatch to hook to abort write request
fast_data_zi u32 aborting_write_reqs;			///< how many write requests are aborting
ddr_data u8 wait_done_recycle_cnt = 0;

ddr_data_ni CmdRefInfo_t gBackupAdminCmdRefInfo[NVME_ACTIVE_COMMAND_COUNT];

#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
static slow_data_zi struct nvmet_sr_iov _sr_iov;

/* In SRIOV mode, this is used look up nvme submission and completion queue for VF1 to VF32
 * ASQ/ACQ and IOSQ/IOCQ. The number of VFs and IOSQ/IOCQ in each VF are configurable. */
static fast_data_zi struct nvmet_sq fqr_sqs[NVMET_RESOURCES_FLEXIBLE_TOTAL];
static fast_data_zi struct nvmet_cq fqr_cqs[NVMET_RESOURCES_FLEXIBLE_TOTAL];
#endif

typedef void (*rst_post_cb)(u32);

fast_data_zi u32 _is_flr_reset = 0;

extern u16 host_sec_size;//joe add change sec size 20200817
extern u8 host_sec_bitz;//joe add change sec size  20200817
//extern vu32 Rdcmd;
share_data volatile u16 req_cnt = NVME_ACTIVE_COMMAND_COUNT;
share_data volatile bool OTF_WARM_BOOT_FLAG = false;
fast_data u32 gRstFsm = 0;

extern volatile bool hostReset;

share_data_zi volatile u8 wb_epm_lock_done = 0;		//20210317-Eddie
share_data_zi volatile u8 wb_frb_lock_done = 0;		//20210325-Eddie
extern bool _fg_warm_boot;

extern volatile bool PLN_open_flag;
extern volatile bool PWRDIS_open_flag;
extern volatile bool ts_reset_sts;
#if (PI_FW_UPDATE == mENABLE)		//20210110-Eddie
	extern u32 pi_req1;
	extern u32 pi_req2;
#endif
#if GC_SUSPEND_FWDL
extern volatile u8 fwdl_gcsuspend_done;
#endif

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
share_data_zi bool telemetry_preset_update = 0;
#endif

extern volatile u8 plp_trigger;

//extern bool ECCT_HIT_2_FLAG;
extern u64 old_time_stamp;

extern bool move_gc_resume;
/// @brief Reset service group operation code
enum
{
    RST_STATE_CORE = 0,
    RST_STATE_CACHE,
    RST_STATE_HARDWARE,
    RST_STATE_BTN,
    RST_STATE_FINISH,
    RST_STATE_COUNT
};

typedef enum{
    RST_RET_CONTINUE = 0,
    RST_RET_POSTPONE,
    RST_RET_HALT,
    RST_RET_ESCAPE
} RstStatus_t;

static RstStatus_t Host_ResetCore(u32 reason, u32 _cb);
static RstStatus_t Host_ResetCache(u32 reason, u32 _cb);
static RstStatus_t Host_ResetHardware(u32 reason, u32 _cb);
static RstStatus_t Host_ResetBTN(u32 reason, u32 _cb);
static RstStatus_t Host_ResetFinish(u32 reason, u32 _cb);

static RstStatus_t (*gProcHostRst[RST_STATE_COUNT]) (u32 reason, u32 _cb) =
{
    Host_ResetCore,
    Host_ResetCache,
    Host_ResetHardware,
    Host_ResetBTN,
    Host_ResetFinish,
};

#if CO_SUPPORT_PANIC_DEGRADED_MODE
ddr_code static void assert_reset_task_handler(u32 p0, u32 _cb, u32 reason /* PCIE_RST_XXX */);
#endif

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------


/*!
 * @brief get one ns_info slot by its nsid
 *
 * @param nsid  namespace id
 *
 * @return ns_info slot
 */
ddr_code nvme_ns_attr_t *get_ns_info_slot(u32 nsid)//joe slow->ddr 20201124
{
	return &_nvme_ns_info[nsid - 1];
}

/*!
 * @brief registers READ access
 *
 * @param reg  which register to access
 *
 * @return register value
 */
static u32 inline nvme_readl(int reg)
{
	return readl((void *)(NVME_BASE + reg));
}

/*!
 * @brief read misc register
 *
 * @param reg	register offset
 *
 * @return	current register value
 */
static inline u32 misc_readl(u32 reg)
{
	return readl((void *)(MISC_BASE + reg));
}

/*!
 * @brief check if any IO command was pending
 *
 * @return	return pending IO command count
 */
fast_code int nvmet_cmd_pending(void)
{
	return ctrlr->admin_running_cmds + ctrlr->cmd_proc_running_cmds + get_btn_running_cmds() - ctrlr->aer_outstanding;
}


ddr_code void AplAdminCmdRelease(void)
{
	u32 i;
	for (i = 0; i < NVME_ACTIVE_COMMAND_COUNT; i++)
	{
		req_t *req = &_reqs[i];
		struct nvme_cmd *cmd = req->host_cmd;

		if ((req->req_from == REQ_Q_ADMIN) && (req->host_cmd != NULL) && (req->state == REQ_ST_FE_ADMIN))
		{
			nvme_apl_trace(LOG_ALW, 0xdf20, "[ACMD] ID:%d opcode:0x%x req:0x%x",i,cmd->opc, req);
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_ABORTED_BY_REQUEST);
			if (req->completion)
			{
				if (cmd->opc != NVME_OPC_IDENTIFY)
				{
					if (cmd->opc == NVME_OPC_FORMAT_NVM)
					{
						req->postformat = true;
						gBackupAdminCmdRefInfo[i].OPC = cmd->opc;
						gBackupAdminCmdRefInfo[i].NSID = cmd->nsid;
					}
					req->completion = nvmet_core_cmd_overlapped;
					req->completion(req);
					req->wholeCdb = true;
				}
				else
				{
					req->completion(req);
				}
			}
		}
	}
}

/*!
 * @brief get a request from the pool
 *
 * @return 	request resource
 */
fast_code req_t *nvmet_get_req(void)
{
	req_t *req = (req_t *) pool_get_ex(&reqs_pool);
	//nvme_apl_trace(LOG_DEBUG, 0, "req_cnt take away(%d),req :%x", req_cnt, req);
	if (req != NULL) {
		req_cnt --;
		req->start = jiffies;
		req->state = REQ_ST_ALLOC;
		req->wholeCdb = false;
		req->postformat = false;
		req->req_prp.fg_prp_list=false;
	}
	return req;
}

/*!
 * @brief put the request to the pool
 *
 * @param req	the request to be recycled
 *
 * @return 	not used
 */
fast_code void nvmet_put_req(req_t *req)
{
	sys_assert(req != NULL);
	req->state = REQ_ST_UNALLOC;
	req->wholeCdb = false;
	req->postformat = false;
	sys_assert(req);
	req->host_cmd = NULL;
	req->completion = NULL;
	pool_put_ex(&reqs_pool, req);
	req_cnt ++;
	//nvme_apl_trace(LOG_DEBUG, 0, "req_cnt put back(%d)", req_cnt, req);
}

/*!
 * @brief set CQ status
 *
 * Set the completion entry status field
 *
 * @param fe	which the completion entry attached
 * @param sct	status code type
 * @param sc	status code
 *
 * @return 	None
 */
void nvmet_set_status(fe_t *fe, u8 sct, u8 sc)
{
	struct nvme_status *nvme_status = (struct nvme_status *) &fe->nvme.nvme_status;

	nvme_status->sct = sct;
	nvme_status->sc = sc;
}

/*!
 * @brief this function is used in test firmware, it allow test module to wrapper IO handler
 *
 * @param ns_id		namespace id to be wrappered
 * @param issue		IO handler of test module
 *
 * @return		return original io handler
 */
void *nvmet_wrapper_ns_issue(u16 ns_id, bool (*issue)(void *))
{
	void *wrappered;

	wrappered = (void *)_nsid[ns_id].ns->issue;
	_nsid[ns_id].ns->issue = (bool (*)(req_t *))issue;
	return wrappered;
}

/*!
 * @brief get relative performance by lba data size
 *
 * @param lbads		lba data size
 *
 * @return 	relative performance
 */
ps_code u32 nvmet_get_rp_by_lbads(u32 lbads)
{
	u32 ret;
	switch (lbads) {
	case 12:
		ret = 2;	/* Good performance */
		break;
	case 15:		/* multi-plane */
	case 16:
		ret = 0;	/* Better performance */
		break;
	case 9:
	default:
		ret = 3;	/* Degraded performance */
		break;
	}
	return ret;
}

/*!
 * @brief set nvme contrller Write Zeroes command
 *
 * @param ctrl_attr 	nvme controller pointer
 *
 * @return		not used
 *
 */
ddr_code void nvmet_set_ctrlr_attrs(nvme_ctrl_attr_t *ctrl_attr)
{
	ctrlr->attr.all = ctrl_attr->all;
	if (ctrlr->attr.b.set_feature_save) {
		extern __attribute__((weak)) void nvme_info_save(req_t *);
		if (!nvme_info_save)
			panic("nvme_info_save miss");
	}

	cmd_proc_write_zero_ctrl(ctrl_attr->b.write_zero);
}


#if (NS_MANAGE == FEATURE_SUPPORTED)
fast_code int nvmet_set_ns_attrs(nvme_ns_attr_t *attr, bool update_reg)//joe slow->ddr 20201124
{
	u8 nsid;
  //  u8 a=0;
	nsid = attr->hw_attr.nsid;
	if ((nsid > NVMET_NR_NS) || (nsid < 1)) {
		nvme_apl_trace(LOG_ERR, 0x7a60, "attr->hw_attr.nsid:%d",attr->hw_attr.nsid);
		panic("Invalid NSID\n");
		return -EBUSY;
	}

	nsid = attr->hw_attr.nsid - 1;
	memcpy((void *)&_nvme_ns_info[nsid], (void *)attr, sizeof(nvme_ns_attr_t));
	memcpy((void *)&ns_array_menu->ns_attr[nsid], (void *)attr, sizeof(nvme_ns_attr_t));//joe add epm 20200901
#if 0
		if ((is_power_on) && (NS_WP_ONCE == _nvme_ns_info[nsid].wp_state)) {
			_nvme_ns_info[nsid].wp_state = NS_NO_WP;
			_nvme_ns_info.hw_attr.wr_prot = 0;
		}
		cmd_proc_ns_cfg(&_nvme_ns_info[nsid].hw_attr, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].ms, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].lbads);
		return;
#endif

	nvme_ns_attr_t *p_ns = &_nvme_ns_info[nsid];

	if (_nsid[nsid].ns)
	{
		nvme_apl_trace(LOG_INFO, 0xc6ce, "NS update cap(%x -> %x), lbaf(%d -> %d)", _nsid[nsid].ns->ncap, p_ns->hw_attr.lb_cnt, _nsid[nsid].ns->lbaf, p_ns->hw_attr.lbaf);
		#if defined(HMETA_SIZE)
		nvme_apl_trace(LOG_INFO, 0x1070, "PI update pit(%d -> %d), pil(%d -> %d), ms(%d -> %d)",_nsid[nsid].ns->pit, p_ns->hw_attr.pit, _nsid[nsid].ns->pil, p_ns->hw_attr.pil, _nsid[nsid].ns->ms, p_ns->hw_attr.ms);
		#endif
	}

	//nvme_apl_trace(LOG_INFO, 0, "ns_array_menu->ns_attr[nsid].hw_attr.lbaf(%d)", ns_array_menu->ns_attr[nsid].hw_attr.lbaf);
	_nsid[nsid].nsid = p_ns->hw_attr.nsid;

	_nsid[nsid].ns = &_ns[nsid];
	_nsid[nsid].ns->ncap = p_ns->fw_attr.ncap;
	_nsid[nsid].ns->nsze = p_ns->fw_attr.nsz;

     /*  for(a=0;a<MAX_LBAF;a++){//joe add sec size 20200820

		if(host_sec_bitz==_lbaf_tbl[a].lbads){
			p_ns->hw_attr.lbaf=a;
			ns_array_menu->ns_attr[nsid].hw_attr.lbaf=a;
			break;
			}

       }*/
       ns_array_menu->ns_attr[nsid].hw_attr.lbaf=p_ns->hw_attr.lbaf;
	_nsid[nsid].ns->lbaf = p_ns->hw_attr.lbaf;
	_nsid[nsid].ns->lbaf_cnt = p_ns->fw_attr.support_lbaf_cnt;
	_nsid[nsid].ns->pil = p_ns->hw_attr.pil;
	_nsid[nsid].ns->ms = p_ns->hw_attr.ms;
	_nsid[nsid].ns->pit = p_ns->hw_attr.pit;

	_nsid[nsid].ns->npit = p_ns->fw_attr.support_pit_cnt;

#if !defined(PROGRAMMER)
	extern bool req_exec(req_t *req);
	_nsid[nsid].ns->issue = req_exec;
#endif /* !PROGRAMMER */

	//if (ctrlr->attr.b.ns_mgt == 0)
	//	_nsid[nsid].type = NSID_TYPE_ACTIVE;
	//else
	//	_nsid[nsid].type = NSID_TYPE_UNALLOCATED;
	_nsid[nsid].type=p_ns->fw_attr.type;//joe add epm 20200828

	if(update_reg)
	{
		BEGIN_CS1 //Eric 20231019 for LBA out of range
		cmd_proc_set_lba_format(nsid + 1, _lbaf_tbl[p_ns->hw_attr.lbaf].lbads, p_ns->hw_attr.lb_cnt, _lbaf_tbl[p_ns->hw_attr.lbaf].ms);
		END_CS1
#if defined(HMETA_SIZE)
		if(_lbaf_tbl[p_ns->hw_attr.lbaf].ms!=0)//joe add 20210125  if no meta lbaf, don not set. It will cause hcrc
		{
			cmd_proc_hmeta_ctrl(_nsid[nsid].ns->pit,_nsid[nsid].ns->ms, _nsid[nsid].ns->pil , nsid + 1,_lbaf_tbl[p_ns->hw_attr.lbaf].ms);
		}
#endif
	}


	return 0;
}

//joe add NS 20200814
ddr_code int nvmet_set_ns_attrs_init(nvme_ns_attr_t *attr, bool is_power_on) //slow_code joe
{
	u8 nsid;
   // u8 a=0;
	nsid = attr->hw_attr.nsid;
	if ((nsid > NVMET_NR_NS) || (nsid < 1)) {
		panic("Invalid NSID\n");
		return -EBUSY;
	}

	nsid = attr->hw_attr.nsid - 1;
	memcpy((void *)&_nvme_ns_info[nsid], (void *)attr, sizeof(nvme_ns_attr_t));
    //  memcpy((void *)&ns_array_menu->ns_attr[nsid], (void *)attr, sizeof(nvme_ns_attr_t));//joe add epm 20200901
#if 0
		if ((is_power_on) && (NS_WP_ONCE == _nvme_ns_info[nsid].wp_state)) {
			_nvme_ns_info[nsid].wp_state = NS_NO_WP;
			_nvme_ns_info.hw_attr.wr_prot = 0;
		}
		cmd_proc_ns_cfg(&_nvme_ns_info[nsid].hw_attr, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].ms, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].lbads);
		return;
#endif

	nvme_ns_attr_t *p_ns = &_nvme_ns_info[nsid];

	//if (_nsid[nsid].ns)
		//nvme_apl_trace(LOG_DEBUG, 0, "NS update cap(%x -> %x), lbaf(%d)", _nsid[nsid].ns->ncap, p_ns->hw_attr.lb_cnt, p_ns->hw_attr.lbaf);

	_nsid[nsid].nsid = p_ns->hw_attr.nsid;

	_nsid[nsid].ns = &_ns[nsid];
	_nsid[nsid].ns->ncap = p_ns->fw_attr.ncap;
	_nsid[nsid].ns->nsze = p_ns->fw_attr.nsz;


       /*for(a=0;a<MAX_LBAF;a++){//joe add sec size 20200820
		if(host_sec_bitz==_lbaf_tbl[a].lbads){
			p_ns->hw_attr.lbaf=a;
			ns_array_menu->ns_attr[nsid].hw_attr.lbaf=a;
			break;
			}
		}*/
	    //_nsid[nsid].ns->lbaf = p_ns->hw_attr.lbaf;
	    _nsid[nsid].ns->lbaf = ns_array_menu->ns_attr[nsid].hw_attr.lbaf;
	_nsid[nsid].ns->lbaf_cnt = p_ns->fw_attr.support_lbaf_cnt;
	_nsid[nsid].ns->pil = p_ns->hw_attr.pil;
	_nsid[nsid].ns->ms = p_ns->hw_attr.ms;
	_nsid[nsid].ns->pit = p_ns->hw_attr.pit;

	_nsid[nsid].ns->npit = p_ns->fw_attr.support_pit_cnt;

#if !defined(PROGRAMMER)
	extern bool req_exec(req_t *req);
	_nsid[nsid].ns->issue = req_exec;
#endif /* !PROGRAMMER */

	//if (ctrlr->attr.b.ns_mgt == 0)
		//_nsid[nsid].type = NSID_TYPE_ACTIVE;
	//else
		//_nsid[nsid].type = NSID_TYPE_ALLOCATED;
		_nsid[nsid].type=p_ns->fw_attr.type;//joe add epm 2020831

	//cmd_proc_set_lba_format(nsid + 1, _lbaf_tbl[p_ns->hw_attr.lbaf].lbads, p_ns->hw_attr.lb_cnt, _lbaf_tbl[p_ns->hw_attr.lbaf].ms);

	return 0;
}
#else
fast_code int nvmet_set_ns_attrs(nvme_ns_attr_t *attr, bool update_reg)
{
	u8 nsid;
	nsid = attr->hw_attr.nsid;
	if ((nsid > NVMET_NR_NS) || (nsid < 1)) {
		panic("Invalid NSID\n");
		return -EBUSY;
	}

	nsid = attr->hw_attr.nsid - 1;
	memcpy((void *)&_nvme_ns_info[nsid], (void *)attr, sizeof(nvme_ns_attr_t));

#if 0
		if ((is_power_on) && (NS_WP_ONCE == _nvme_ns_info[nsid].wp_state)) {
			_nvme_ns_info[nsid].wp_state = NS_NO_WP;
			_nvme_ns_info.hw_attr.wr_prot = 0;
		}
		cmd_proc_ns_cfg(&_nvme_ns_info[nsid].hw_attr, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].ms, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].lbads);
		return;
#endif


	nvme_ns_attr_t *p_ns = &_nvme_ns_info[nsid];

	//if (_nsid[nsid].ns)
		//nvme_apl_trace(LOG_DEBUG, 0, "NS update cap(%x -> %x), lbaf(%d)", _nsid[nsid].ns->ncap, p_ns->hw_attr.lb_cnt, p_ns->hw_attr.lbaf);

	_nsid[nsid].nsid = p_ns->hw_attr.nsid;

	_nsid[nsid].ns = &_ns[nsid];
	_nsid[nsid].ns->ncap = p_ns->fw_attr.ncap;
	_nsid[nsid].ns->nsze = p_ns->fw_attr.nsz;

	_nsid[nsid].ns->lbaf = p_ns->hw_attr.lbaf;
	_nsid[nsid].ns->lbaf_cnt = p_ns->fw_attr.support_lbaf_cnt;
	_nsid[nsid].ns->pil = p_ns->hw_attr.pil;
	_nsid[nsid].ns->ms = p_ns->hw_attr.ms;
	_nsid[nsid].ns->pit = p_ns->hw_attr.pit;

	_nsid[nsid].ns->npit = p_ns->fw_attr.support_pit_cnt;

#if !defined(PROGRAMMER)
	extern bool req_exec(req_t *req);
	_nsid[nsid].ns->issue = req_exec;
#endif /* !PROGRAMMER */

	if (ctrlr->attr.b.ns_mgt == 0)
		_nsid[nsid].type = NSID_TYPE_ACTIVE;
	else
		_nsid[nsid].type = NSID_TYPE_UNALLOCATED;

	BEGIN_CS1 //Eric 20231019 for LBA out of range
	cmd_proc_set_lba_format(nsid + 1, _lbaf_tbl[p_ns->hw_attr.lbaf].lbads, p_ns->hw_attr.lb_cnt, _lbaf_tbl[p_ns->hw_attr.lbaf].ms);
	END_CS1

	return 0;
}
#endif

/*!
 * @brief API to restore HW setting of namespace in CMD_PROC
 *
 * @return  none
 */
fast_code void nvmet_ns_resume(void)//joe fast--->slow 202008 --> init
{
	nvme_ns_attr_t *p_ns = NULL;
	u8 i;
	for (i = 0; i < NVMET_NR_NS; i ++) {
		p_ns = &_nvme_ns_info[i];

		if (NSID_TYPE_ACTIVE == p_ns->fw_attr.type)
		{
			cmd_proc_ns_cfg(&p_ns->hw_attr, _lbaf_tbl[p_ns->hw_attr.lbaf].ms, _lbaf_tbl[p_ns->hw_attr.lbaf].lbads);
			#if defined(HMETA_SIZE)
			if(_lbaf_tbl[p_ns->hw_attr.lbaf].ms!=0)
			{
				cmd_proc_hmeta_ctrl(p_ns->hw_attr.pit, p_ns->hw_attr.ms, p_ns->hw_attr.pil , i + 1,_lbaf_tbl[p_ns->hw_attr.lbaf].ms);
			}
			#endif
		}
	}
}

/*!
 * @brief restore thermal management Temperature, include TMT1 and TMT2
 *
 * @return  none
 */
init_code void nvmet_restore_tmt(void)  //fast_code joe
{
	u32 tmt1, tmt2, tmt_warning, tmt_critical;

	tmt1 = ctrlr->cur_feat.hctm_feat.b.tmt1;
	tmt2 = ctrlr->cur_feat.hctm_feat.b.tmt2;
	tmt1 = tmt1 != 0 ? k_deg_to_c_deg(tmt1) : ~0;
	tmt2 = tmt2 != 0 ? k_deg_to_c_deg(tmt2) : ~0;

	ts_tmt_setup(tmt1, tmt2);

	tmt_warning = k_deg_to_c_deg(ctrlr->cur_feat.warn_cri_feat.tmt_warning);
	tmt_critical = k_deg_to_c_deg(ctrlr->cur_feat.warn_cri_feat.tmt_critical);
	ts_warn_cri_tmt_setup(tmt_warning, tmt_critical);
}

/*!
 * @brief restore NVME feature
 *
 * @param saved_feat 	nvme feature struct
 *
 * @return		not used
 */
init_code void nvmet_restore_feat(nvme_feature_saved_t *saved_feat)
{
/*
	u32 new_fsize = sizeof(*saved_feat) - sizeof(saved_feat->rsvd);

	if (saved_feat == NULL)
		goto def;

	if (stat_match(&saved_feat->head, SAVED_FEAT_VER, new_fsize, SAVED_FEAT_SIG)) {
		ctrlr->cur_feat = saved_feat->saved_feat;
		ctrlr->saved_feat = saved_feat->saved_feat;
	} else {
def:
		nvme_apl_trace(LOG_ALW, 0, "def feat used");
		ctrlr->cur_feat = ctrlr->def_feat;
		ctrlr->saved_feat = ctrlr->def_feat;
	}

	nvmet_restore_tmt();
*/
		extern epm_info_t *shr_epm_info;
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		memcpy(&ctrlr->saved_feat, epm_smart_data->feature_save, sizeof(struct nvmet_feat));

        u32 gpio_reg = readl((void *)(MISC_BASE + GPIO_INT_CTRL));


		if(ctrlr->saved_feat.Tag == 0x53415645)//"SAVE"
		{
			nvme_apl_trace(LOG_ALW, 0xf62a, "saved feat used");
			if(!ctrlr->saved_feat.warn_cri_feat.tmt_critical)
			{
				ctrlr->saved_feat.hctm_feat.b.tmt1 = c_deg_to_k_deg(TS_DEFAULT_TMT1);
				ctrlr->saved_feat.hctm_feat.b.tmt2 = c_deg_to_k_deg(TS_DEFAULT_TMT2);
				ctrlr->saved_feat.warn_cri_feat.tmt_warning = c_deg_to_k_deg(TS_DEFAULT_WARNING);
				ctrlr->saved_feat.warn_cri_feat.tmt_critical = c_deg_to_k_deg(TS_DEFAULT_CRITICAL);
			}

			ctrlr->cur_feat = ctrlr->saved_feat;
			ctrlr->cur_feat.ic_feat.all = ctrlr->def_feat.ic_feat.all;
			nvmet_restore_tmt();
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
				if(ctrlr->cur_feat.en_plp_feat.b.opie == 1){
                    PLN_open_flag = true;
                    nvme_apl_trace(LOG_ALW, 0xfaed, "PLP Enable ");
				}
				else{
                    PLN_open_flag = false;
                    nvme_apl_trace(LOG_ALW, 0x8d40, "PLP Disable ");
				}
#endif
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
                if(ctrlr->cur_feat.en_pwrdis_feat.b.pwrdis){
                    PWRDIS_open_flag = true;
                    nvme_apl_trace(LOG_ALW, 0x1c82, "PWRDIS enable ");
                }
                else{
                    PWRDIS_open_flag = false;
                    nvme_apl_trace(LOG_ALW, 0x4898, "PWRDIS disable ");
                }
#endif
			return;
		}

		nvme_apl_trace(LOG_ALW, 0x9817, "def feat used");
		ctrlr->saved_feat = ctrlr->def_feat;
		ctrlr->cur_feat = ctrlr->saved_feat;

#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
			if(ctrlr->cur_feat.en_plp_feat.b.opie == 1){
                PLN_open_flag = true;
                nvme_apl_trace(LOG_ALW, 0x34c8, "PLP Enable ");
			}
			else{
                PLN_open_flag = false;
                nvme_apl_trace(LOG_ALW, 0x1501, "PLP Disable ");
			}
#endif
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
            if(ctrlr->cur_feat.en_pwrdis_feat.b.pwrdis){
                PWRDIS_open_flag = true;
                nvme_apl_trace(LOG_ALW, 0x979f, "PWRDIS enable ");
            }
            else{
                PWRDIS_open_flag = false;
                nvme_apl_trace(LOG_ALW, 0x9282, "PWRDIS disable ");
            }
#endif
        nvme_apl_trace(LOG_ALW, 0x17d3, " GPIO_INT_CTRL 0x%x",gpio_reg);

		//nvme_apl_trace(LOG_ERR, 0, "cur_ARB %x,saved_arb %x\n",ctrlr->cur_feat.arb_feat.all,ctrlr->saved_feat.arb_feat.all);
		//ctrlr->saved_feat = saved_feat->saved_feat;
}


fast_code void nvmet_reinit_feat(void)  //3.1.7.4 merged 20201201 Eddie
{
	nvme_apl_trace(LOG_INFO, 0x07af, "reinit nvme feature (saved) -> (cur)");
	ctrlr->cur_feat = ctrlr->saved_feat;
	old_time_stamp = get_cur_sys_time();
}

/*!
 * @brief update NVME feature
 *
 * @param saved_feat 	nvme feature struct
 *
 * @return		not used
 */
fast_code void nvmet_update_feat(nvme_feature_saved_t *to_save_feat)
{
	u32 fsize = sizeof(*to_save_feat) - sizeof(to_save_feat->rsvd);

	stat_init(&to_save_feat->head, SAVED_FEAT_VER, fsize, SAVED_FEAT_SIG);
	to_save_feat->saved_feat = ctrlr->saved_feat;
	memset(to_save_feat->rsvd, 0, sizeof(to_save_feat->rsvd));
}

/*!
 * @brief setup NVMe controller SQ
 *
 * @param sqid 		SQ id to be setup
 *
 * @return 		not used
 */
fast_code void nvmet_set_sq(u16 sqid)
{
	ctrlr->sqs[sqid] = &sqs[sqid];
}

/*!
 * @brief unset NVMe controller SQ
 *
 * @param sqid 		SQ id to be unset
 *
 * @return 		not used
 */
fast_code void nvmet_unset_sq(u16 sqid)
{
	ctrlr->sqs[sqid] = NULL;
}

/*!
 * @brief setup NVMe controller CQ
 *
 * @param sqid 		CQ id to be setup
 *
 * @return 		not used
 */
fast_code void nvmet_set_cq(u16 cqid)
{
	ctrlr->cqs[cqid] = &cqs[cqid];
	ctrlr->cqs[cqid]->qid = cqid;	/* placeholder */
}

/*!
 * @brief unset NVMe controller CQ
 *
 * @param sqid 		CQ id to be unset
 *
 * @return 		not used
 */
fast_code void nvmet_unset_cq(u16 cqid)
{
	ctrlr->cqs[cqid]->qid = 0xffff;	/* placeholder */
	ctrlr->cqs[cqid] = NULL;
}

/*!
 * @brief binding SQ and CQ
 *
 * @param sqid		SQ id to be binded
 * @param cqid		CQ id for SQ to be binded
 *
 * @return 		not used
 */
fast_code void nvmet_bind_cq(u16 sqid, u16 cqid)
{
	if (!ctrlr->sqs[sqid])
		panic("nvmet_bind_cq: no such sqid\n");

	ctrlr->sqs[sqid]->cqid = cqid;
}

/*!
 * @brief unbinding SQ and CQ
 *
 * @param sqid		SQ id to be unbinded
 * @param cqid		CQ id for SQ to be unbinded
 *
 * @return 		not used
 */
fast_code void nvmet_unbind_cq(u16 sqid, u16 cqid)
{
	if (!ctrlr->sqs[sqid] || (ctrlr->sqs[sqid]->cqid != cqid))
		panic("no such sqid or it doesn't bind to the cq\n");

	ctrlr->sqs[sqid]->cqid = 0xffff;
}

/*!
 * @brief setup NVMe controller CQ SQ and bind
 *
 * @param cqid		SQ id to be setup
 * @param cqid		CQ id to be setup
 *
 * @return 		None
 */
fast_code void nvmet_set_sqcq(u16 sqid, u16 cqid)
{
	struct nvmet_sq *sq;

	nvmet_set_cq(cqid);
	nvmet_set_sq(sqid);
	nvmet_bind_cq(sqid, cqid);

	sq = ctrlr->sqs[sqid];
	sq->qprio = NVME_QPRIO_URGENT;
	sq->head = sq->tail = ~0;

	INIT_LIST_HEAD(&sq->reqs);
}

#if defined(SRIOV_SUPPORT)
/*!
 * @brief This function set Host Submission Queue ID for nvme submission queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs.
 * @param hst_sqid	Function SQID. For Admin 0, IO > 0
 *
 * @return	none
 */
fast_code void nvmet_set_vfsq(u8 fid, u8 hst_sqid)
{
	if (fid == 0) {
		nvmet_set_sq(hst_sqid);
	} else {
		ctrlr->sr_iov->fqr_sq_cq[fid - 1].fqrsq[hst_sqid] =
			&fqr_sqs[(fid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC + hst_sqid];
	}
}

/*!
 * @brief This function unset Host Submission Queue ID for nvme submission queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs.
 * @param hst_sqid	Function SQID. For Admin 0, IO > 0
 *
 * @return	none
 */
fast_code void nvmet_unset_vfsq(u8 fid, u8 hst_sqid)
{
	if (fid == 0)
		nvmet_unset_sq(hst_sqid);
	else
		ctrlr->sr_iov->fqr_sq_cq[fid - 1].fqrsq[hst_sqid] = NULL;
}

/*!
 * @brief This function set Host Completion Queue ID for nvme completiton queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 *
 */
fast_code void nvmet_set_vfcq(u8 fid, u8 hst_cqid)
{
	if (fid == 0) {
		nvmet_set_cq(hst_cqid);
	} else {
		ctrlr->sr_iov->fqr_sq_cq[fid - 1].fqrcq[hst_cqid] =
			&fqr_cqs[(fid -1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC + hst_cqid];
		ctrlr->sr_iov->fqr_sq_cq[fid - 1].fqrcq[hst_cqid]->qid = hst_cqid;
	}
}

/*!
 * @brief This function unset Host Completion Queue ID for nvme completiton queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 */
fast_code void nvmet_unset_vfcq(u8 fid, u8 hst_cqid)
{
	if (fid == 0) {
		nvmet_unset_cq(hst_cqid);
	} else {
		ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrcq[hst_cqid]->qid = 0xffff;
		ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrcq[hst_cqid] = NULL;
	}
}

/*!
 * @brief This function binds host CQ-SQ IDs for PF0 and VFs
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_sqid	Function SQID. For Admin 0; IO > 0
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 */
fast_code void nvmet_bind_vfcq(u8 fid, u8 hst_sqid, u8 hst_cqid)
{
	if (fid == 0) {
		nvmet_bind_cq(hst_sqid, hst_cqid);
	} else {
		if (!ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid])
			panic("nvmet_bind_vfcq: no such PF/VF sqid\n");

		ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid]->cqid = hst_cqid;
	}
}

/*!
 * @brief This function unbinds host CQ-SQ IDs for PF0 and VFs
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_sqid	Function SQID. For Admin 0; IO > 0
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 */
fast_code void nvmet_unbind_vfcq(u8 fid, u8 hst_sqid, u8 hst_cqid)
{
	if (fid == 0) {
		nvmet_unbind_cq(hst_sqid, hst_cqid);
	} else {
		if (!ctrlr->sr_iov->fqr_sq_cq[fid - 1].fqrsq[hst_sqid] ||
		    (ctrlr->sr_iov->fqr_sq_cq[fid - 1].fqrsq[hst_sqid]->cqid != hst_cqid))
			panic("no such VF sqid or it doesn't bind to the VF cq\n");

		ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid]->cqid = 0xffff;
	}
}
#endif

/*!
 * @brief abort running nvme req
 *
 * @param req		abort req stuct point
 *
 * @return 		None
 */
fast_code void nvmet_abort_running_req(req_t *req)
{
	switch (req->opcode) {
	case REQ_T_READ:
	case REQ_T_WRITE:
		panic("not support");
		break;
    case REQ_T_FLUSH:
		if (req->state == REQ_ST_DISK) {
            nvme_apl_trace(LOG_ALW, 0x4d7b, "abort flush");
			// this command should be pended
			list_del(&req->entry2);
			req->error = true;
			req->completion(req);
		}
		break;
	case REQ_T_TRIM:
		if (req->state == REQ_ST_FE_IO) {
			dtag_remove_evt(DTAG_T_SRAM, (void*) req);
			req->error = true;
			req->completion(req);
		}
        else if ((req->state == REQ_ST_DISK) && (gResetFlag & (BIT(cNvmePCIeReset))))
        {
            dtag_t dtag;

            dtag = mem2dtag(req->op_fields.trim.dsmr);
            dtag_put(DTAG_T_SRAM, dtag);
            req->error = true;
            req->completion(req);
        }
        else
        {
			// todo: return error and let FTL continue to trim
		}
		break;
	case REQ_T_FORMAT:
		panic("not avaliable");
		break;
	default:
		break;
	};
}

/*!
 * @brief	nvme check sq req timeout timer handle
 *
 * @param	data timer name
 *
 * @return	None
 */
static fast_code void nvmet_sq_timer_chk(void *data)
{
	int qid;

	for (qid = ctrlr->max_qid - 1; qid >= 0; qid--) {
		u16 head;
		u16 tail;
		struct list_head *entry;
		struct list_head *entry2;

		/* queue pointer check */
		if (ctrlr->sqs[qid] == NULL)
			continue;

		if (qid == 0) {
			// record doorbell
			hal_nvmet_get_sq_pnter(qid, &head, &tail);
			ctrlr->sqs[qid]->head = head;
			ctrlr->sqs[qid]->tail = tail;

			/* IO sq req elapsed time check */
			continue;
		}

		if (list_empty(&ctrlr->sqs[qid]->reqs))
			continue;

		list_for_each_safe(entry, entry2, &ctrlr->sqs[qid]->reqs) {
			req_t *req = container_of(entry, req_t, entry);

			if (nvmet_req_elapsed_time(req) < REQ_TIMEOUT_MS)
				break;

			nvme_apl_trace(LOG_ERR, 0xe985, "req 0x%p timeout(%d ms), op(%d)",
					req, nvmet_req_elapsed_time(req), req->opcode);
{ // debug msgs, it will be removed
			extern __attribute__((weak)) void ncl_cmd_timeout_dump(void);
			ncl_cmd_timeout_dump();
}
			// try to abort it
			nvmet_abort_running_req(req);
		}
	}

	nvmet_sq_timer_enable();
}

#if (TRIM_SUPPORT == ENABLE) && (_TCG_)
extern Trim_Info TrimInfo;
fast_data u8 evt_tcg_trim_chk = 0xFF;
ddr_code void nvmet_evt_tcg_trim_chk(u32 param, u32 r0, u32 r1)
{
	//nvmet_core_cmd_done((req_t *)r0);
	if((gFormatInProgress) && (TrimInfo.FullTrimTriggered == 1))
		evt_set_cs(evt_tcg_trim_chk, r0, 0, CS_TASK);
	else
	{
		gFormatInProgress = false;
		tcg_ioCmd_inhibited = false;
		nvmet_io_fetch_ctrl(tcg_ioCmd_inhibited);
	}
}

#endif

/*!
 * @brief return CQ from fw
 *
 * @param fe	fe paramter
 *
 * @return	return false if hw resource is not enough
 */
bool fast_code nvmet_core_handle_cq(fe_t *fe)
{
#if defined(SRIOV_SUPPORT)
	u8 fid = fe->nvme.cntlid;
	u16 sqid, hst_sqid;

	if (fid == 0) {
		/* tNVMe 14:3.0.0 - We may delete the SQ and unbind CQ per Host */
		if (ctrlr->sqs[fe->nvme.sqid] == NULL)
			return true;
	} else {
		sqid = fe->nvme.sqid;
		hst_sqid = sqid - SRIOV_FLEX_PF_ADM_IO_Q_TOTAL -
			(fid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
		if (ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid] == NULL) {
			nvme_apl_trace(LOG_INFO, 0x3983, "cntlid(%d), sqid(%d), hst_sqid(%d)",fid, sqid, hst_sqid);
			return true;
		}
	}
#else
	/* tNVMe 14:3.0.0 - We may delete the SQ and unbind CQ per Host */
	if (ctrlr->sqs[fe->nvme.sqid] == NULL)
		return true;
#endif

	/* To avoid memset the 16 bytes */
	struct nvme_cpl cpl; // = {.rsvd1 = 0 };
#if defined(SRIOV_SUPPORT)
	u16 cqid;
	if (fid == 0) {
		cqid = ctrlr->sqs[fe->nvme.sqid]->cqid;
	} else {
		cqid = ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid]->cqid;
		cqid = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL + (fid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
		nvme_apl_trace(LOG_DEBUG, 0x7569, "cntlid(%d), flex sqid(%d), flex cqid(%d), hst_sqid(%d)",fid, sqid, cqid, hst_sqid);
	}
#else
	u16 cqid = ctrlr->sqs[fe->nvme.sqid]->cqid;
#endif
	/* it's ugly due to type-punned pointer breaks strict-aliasing rules */
	nvme_status_alias_t _status;

	cpl.rsvd1 = 0;
	cpl.sqid = fe->nvme.sqid;
	cpl.cntlid = fe->nvme.cntlid;
	cpl.cdw0 = fe->nvme.cmd_spec;
	cpl.cid = fe->nvme.cid;

	_status.all = fe->nvme.nvme_status;
	cpl.status = _status.status;

	if (cpl.status.sct != 0 || cpl.status.sc != 0) {
		nvme_apl_trace(LOG_ERR, 0x08d5, "sq %d cid %d err sct %d sc %d",
				fe->nvme.sqid, fe->nvme.cid, cpl.status.sct, cpl.status.sc);
	}

	return hal_nvmet_update_cq(cqid, &cpl, false);
}
void ddr_code nvmet_warmboot_handle_commit_done(fe_t *fe)
{
	nvmet_core_handle_cq(fe);
}
bool fast_code nvmet_core_cmd_done(req_t *req)
{
	if (req->error == true)
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);

	struct nvme_cmd* cmd = req->host_cmd;
	struct nvme_fw_commit *fw_commit =
		(struct nvme_fw_commit *)(&cmd->cdw10);
	extern commit_ca3* commit_ca3_fe;
	extern is_IOQ* is_IOQ_ever_create_or_not;
#if _TCG_ != TCG_NONE
	extern bool tcg_ioCmd_inhibited;
	if(tcg_ioCmd_inhibited && ((cmd->opc == NVME_OPC_SECURITY_SEND) || (cmd->opc == NVME_OPC_SECURITY_RECEIVE))){
#if (TRIM_SUPPORT == ENABLE)
		if((gFormatInProgress) && (TrimInfo.FullTrimTriggered == 1))
		{
			evt_set_cs(evt_tcg_trim_chk, (u32)req, 0, CS_TASK);
			if(gResetFlag & BIT(cNVMeLinkReqRstNot))
				return false;
		}
		else if(gFormatInProgress)
			evt_set_cs(evt_tcg_trim_chk, (u32)req, 0, CS_NOW);
		else
#endif
		{
			tcg_ioCmd_inhibited = false;
			nvmet_io_fetch_ctrl(tcg_ioCmd_inhibited);
		}
	}
#endif

	if(cmd->opc == NVME_OPC_FIRMWARE_COMMIT &&
		fw_commit->ca == NVME_FW_COMMIT_RUN_IMG &&
		req->fe.nvme.nvme_status == 0)//one time is one commit cmd
	{
		nvme_apl_trace(LOG_INFO, 0xdb2f, "warmboot save fe, IOQ_flag %d",is_IOQ_ever_create_or_not->flag);
		commit_ca3_fe->flag = 0xABCD;
		commit_ca3_fe->fe = req->fe;
	}
	else if(cmd->opc == NVME_OPC_FIRMWARE_COMMIT  &&
		(fw_commit->ca < NVME_FW_COMMIT_RUN_IMG) &&
		req->fe.nvme.nvme_status == 0)
	{
		commit_ca3_fe->flag = 0;
		nvmet_core_handle_cq(&req->fe);
	}
	else
		nvmet_core_handle_cq(&req->fe);

	//nvme_apl_trace(LOG_DEBUG, 0, "req(%x), cntlid(%d)", req, req->fe.nvme.cntlid);

	if (req->req_from == REQ_Q_ADMIN){
		ctrlr->admin_running_cmds--;
        /*when admin cmd error, print more log by Joylon 20210903*/
        struct nvme_cpl cpl;
		struct nvme_cmd *cmd = req->host_cmd;
		nvme_status_alias_t _status;
		_status.all = req->fe.nvme.nvme_status;
	    cpl.status = _status.status;

		if(cpl.status.sct != 0 || cpl.status.sc != 0){
			nvme_apl_trace(LOG_DEBUG, 0xbb0d, "nsid %d cdw10 %x cdw11 %x cdw12 %x",cmd->nsid,cmd->cdw10,cmd->cdw11,cmd->cdw12);
			nvme_apl_trace(LOG_DEBUG, 0x4578, "cdw13 %x cdw14 %x cdw15 %x",cmd->cdw13,cmd->cdw14,cmd->cdw15);
		}
		//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
		//Rdcmd--;
		//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
	}
	else{
		ctrlr->cmd_proc_running_cmds--;
#if CO_SUPPORT_SANITIZE
		struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
		if((nvme_status->sc == 0) && (nvme_status->sct == 0))
		{
			extern epm_info_t *shr_epm_info;
			epm_smart_t *smart_data;
			switch(cmd->opc)
			{
				case NVME_OPC_WRITE_UNCORRECTABLE:
				case NVME_OPC_WRITE_ZEROES:
					smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
					if(smart_data->sanitizeInfo.sanitize_Tag != 0xDEADFACE)
					{
						smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_None;
						//smart_data->sanitizeInfo.sanitize_log_page &= ~(0xFF0000);
						//smart_data->sanitizeInfo.sanitize_log_page |= (0xFFFF);
						smart_data->sanitizeInfo.sanitize_log_page = 0xFFFF;
						smart_data->sanitizeInfo.handled_wr_cmd_cnt = 0;
						smart_data->sanitizeInfo.bmp_w_cmd_sanitize = 0;
						smart_data->sanitizeInfo.sanitize_Tag = 0xDEADFACE;
					}
					if(smart_data->sanitizeInfo.bmp_w_cmd_sanitize == 0)
					{
						smart_data->sanitizeInfo.bmp_w_cmd_sanitize = 1;
						epm_update(SMART_sign, (CPU_ID - 1));
					}
				default:
					break;
			}
		}
#endif
		//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
		//Rdcmd--;
		//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);

	//nvme_apl_trace(LOG_INFO, 0, "proc done %x", Rdcmd);
	}

#if defined(SRIOV_SUPPORT)
	/* TODO: Analyze this part of code */
	list_del_init(&req->entry);
	if (req->host_cmd)
		hal_nvmet_put_sq_cmd(req->host_cmd);

	if (req->fe.nvme.sqid != 0)
		nvmet_io_end();

	nvmet_put_req(req);
	if (aborting_write_reqs == 0)
		hal_nvmet_kickoff();
#else
	list_del_init(&req->entry);
	if (req->host_cmd)
		hal_nvmet_put_sq_cmd(req->host_cmd);

	nvmet_put_req(req);
	if (aborting_write_reqs == 0)
		hal_nvmet_kickoff();
#endif
	return true;
}

bool ddr_code nvmet_core_cmd_overlapped(req_t *req)
{
#if 0//_TCG_ != TCG_NONE
	struct nvme_cmd* cmd_chk = req->host_cmd;
	if((cmd_chk->opc == NVME_OPC_SECURITY_SEND) || (cmd_chk->opc == NVME_OPC_SECURITY_RECEIVE)){
		nvme_apl_trace(LOG_INFO, 0x4783, "skip cmd opc:0x%x, req(0x%x)", cmd_chk->opc, req);
		req->completion = nvmet_core_cmd_done;
		return false;
	}
#endif

	if (req->wholeCdb == false)
	{
		if (req->error == true)
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);

		struct nvme_cmd* cmd = req->host_cmd;
		struct nvme_fw_commit *fw_commit =
			(struct nvme_fw_commit *)(&cmd->cdw10);
		extern commit_ca3* commit_ca3_fe;

		if(cmd->opc == NVME_OPC_FIRMWARE_COMMIT &&
			fw_commit->ca == 0x3 &&
			req->fe.nvme.nvme_status == 0)//one time is one commit cmd
		{
			nvme_apl_trace(LOG_INFO, 0x4784, "warmboot save fe" );
			commit_ca3_fe->flag = 0xABCD;
			commit_ca3_fe->fe = req->fe;
		}
		else
			//nvme_apl_trace(LOG_INFO, 0, "opc done:%x", cmd->opc);
			nvmet_core_handle_cq(&req->fe);

		//nvme_apl_trace(LOG_DEBUG, 0, "req(%x), cntlid(%d)", req, req->fe.nvme.cntlid);

		if (req->req_from == REQ_Q_ADMIN){
			ctrlr->admin_running_cmds--;
			//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
			//Rdcmd--;
			//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
		}
		else{
			ctrlr->cmd_proc_running_cmds--;
			//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
			//Rdcmd--;
			//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);

			//nvme_apl_trace(LOG_INFO, 0, "proc done %x", Rdcmd);
		}

	#if defined(SRIOV_SUPPORT)
		/* TODO: Analyze this part of code */
		// list_del_init(&req->entry);
		// if (req->host_cmd)
		//     hal_nvmet_put_sq_cmd(req->host_cmd);
		//
		// if (req->fe.nvme.sqid != 0)
		//     nvmet_io_end();
		//
		// nvmet_put_req(req);
		if (aborting_write_reqs == 0)
			hal_nvmet_kickoff();
	 #else
		//list_del_init(&req->entry);
		if (req->host_cmd)
			hal_nvmet_put_sq_cmd(req->host_cmd);

		//nvmet_put_req(req);
		if (aborting_write_reqs == 0)
			hal_nvmet_kickoff();
	#endif
	}
	else
	{
		nvme_apl_trace(LOG_ALW, 0x5678, "Put :0x%x back",req);
		list_del_init(&req->entry);
		nvmet_put_req(req);
	}

	return true;
}

#ifdef NS_MANAGE
ddr_code void nvmet_set_drive_capacity(u64 capacity)//joe slow->ddr 20201124
{
	//ctrlr->drive_capacity = capacity;
	nvme_apl_trace(LOG_INFO, 0x5b71, "Drive Capacity: %d (LBACNT)", capacity);
	//joe add NS 20200813
	if(host_sec_bitz==9){	//joe add sec size 20200817 //20200908  the ns.cap[0]  is LDA cnt
		if((capacity%NS_SIZE_GRANULARITY_CORE1)==0){//joe 20200528  check the capacity is enough
			drive_total_sector=capacity/NS_SIZE_GRANULARITY_CORE1;
			ctrlr->drive_capacity =drive_total_sector*NS_SIZE_GRANULARITY_CORE1;
		} else{
			drive_total_sector=capacity/NS_SIZE_GRANULARITY_CORE1+1;
			ctrlr->drive_capacity=capacity;
		}
	}else{
		if((capacity%NS_SIZE_GRANULARITY_CORE2)==0){//joe 20200528  check the capacity is enough
			drive_total_sector=capacity/NS_SIZE_GRANULARITY_CORE2;
			ctrlr->drive_capacity =drive_total_sector*NS_SIZE_GRANULARITY_CORE2;
		}else{
			drive_total_sector=capacity/NS_SIZE_GRANULARITY_CORE2+1;
			ctrlr->drive_capacity=capacity;
		}
	}

	nvme_apl_trace(LOG_INFO, 0x92bb, "Drive Capacity: %x (LBACNT)  host_sec_bitz:%d  ", capacity,host_sec_bitz);
	nvme_apl_trace(LOG_INFO, 0xc48c, "Drive total sector: %d ", drive_total_sector);
	//joe add NS 20200813
}

/*!
 * @brief convert host cmd start to namespace based start lba.
 *
 * @param param		u32 host command namespace id, 1 based
 *
 * @return		u64 start lba
 */
fast_code u64 nvmet_get_ns_slba(u32 nsid)
{
	return _nsid[nsid - 1].ns->start_lba;
}

/*!
 * @brief check namespace id active state
 *
 * @param		nsid, namespace id
 *
 * @return		bool, true: namespace is active, false, namespace is inactive.
 */
UNUSED fast_code bool nvmet_ns_valid_check(u8 nsid)
{
	return ((_nsid[nsid - 1].type != NSID_TYPE_ACTIVE) ? false : true);
}
#endif

/*!
 * @brief NVM command done event handler (Interrupt Context)
 *
 * @param param		not used
 * @param payload	req
 * @param sts		req execution status
 *
 * @return		None
 */

extern void format_sec(u8 lbads, u8 lbaf, u8 eccu_setting);
extern enum du_fmt_t host_du_fmt;
extern bool  gFormatFlag;
extern volatile u8 plp_trigger;
extern bool create_ns_preformat_handle;


static fast_code void nvmet_evt_cmd_done(u32 param, u32 data, u32 sts)
{
	req_t *req = (void *)(data);
	struct nvme_cmd *cmd = req->host_cmd;

	//nvme_apl_trace(LOG_DEBUG, 0, "req(%p) sts=(0x%x)", req, sts);

	if (sts != 0) {
		u8 sc, sct;
		sct = (sts >> 8) & 0xFF;
		sc = sts & 0xFF;
		nvmet_set_status(&req->fe, sct, sc);
	}
	if((((cmd->opc == NVME_OPC_FORMAT_NVM) || (req->postformat == true)) && (gFormatFlag == true)) && !plp_trigger)
	{
		struct nvmet_namespace *ns = NULL;
		u32 nsid = cmd->nsid;

		if (req->postformat)
		{
			u32 idx = req2ctag(req);
			nsid = gBackupAdminCmdRefInfo[idx].NSID;
			nvme_apl_trace(LOG_INFO, 0x30d6, "ID:%d NSID:0x%x", idx, nsid);
		}

		if(nsid==~0)
		{
			ns = ctrlr->nsid[0].ns;
		}
		else
		{
			ns=ctrlr->nsid[nsid - 1].ns;
		}
		format_sec(_lbaf_tbl[ns->lbaf].lbads, ns->lbaf, true);
		#if defined(HMETA_SIZE)
        if(_lbaf_tbl[ns->lbaf].ms==0)
		{
			host_du_fmt = DU_FMT_USER_4K;
		}
		else
		{
			host_du_fmt = DU_FMT_USER_4K_HMETA;
		}
		#else
		host_du_fmt = DU_FMT_USER_4K;
		#endif

#if(degrade_mode == ENABLE)
		extern ddr_code void degrade_mode_fmt_reset_io();
		degrade_mode_fmt_reset_io();
#endif

		btn_de_wr_enable();
		nvmet_io_fetch_ctrl(false);
		gFormatFlag = false;
		nvme_apl_trace(LOG_INFO, 0xabf0, "[format]preformat finish");
		//flush_to_nand(EVT_NVME_FORMATE);
	}


	if(cmd->opc == NVME_OPC_NS_MANAGEMENT && create_ns_preformat_handle == true)
	{
		dtag_t *dtag = (dtag_t *)req->req_prp.mem;

		dtag_put(DTAG_T_SRAM, dtag[0]);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);

		nvmet_io_fetch_ctrl(false);
		create_ns_preformat_handle = false;
		nvme_apl_trace(LOG_INFO, 0x72de, "[create_ns]preformat finish");
	}

	if (req->completion)
		req->completion(req);
}

fast_code void nvmet_clear_io_queue(u8 fid, u32 cmd_proc_del[3])
{
	struct list_head *cur, *saved;
	int sqid = 0, cqid = 0;

	//nvme_apl_trace(LOG_DEBUG, 0, "Outstanding #AER(%d)", ctrlr->aer_outstanding);

	/* If Asynchronous Event Requests commands are outstanding when the controller is reset,
	 * the commands are abort */
	if (ctrlr->aer_outstanding != 0) {
		if(gResetFlag == BIT(cNvmeShutDown)){
			goto AER_END;
		}
		//nvme_apl_trace(LOG_DEBUG, 0, "abort #(%d) AER commands", ctrlr->aer_outstanding);
		list_for_each_safe(cur, saved, &ctrlr->aer_outstanding_reqs) {
			req_t *req = container_of(cur, req_t, entry);

			req->start = jiffies;
			/* FIXME: Power Loss */
			///Andy IOL test modify to abort by request
			#if 0
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_ABORTED_POWER_LOSS);
			#else
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_ABORTED_BY_REQUEST);
			#endif
			list_del_init(&req->entry);
			list_add_tail(&req->entry, &ctrlr->aer_outstanding_reqs);

			if (req->completion)
				req->completion(req);
			ctrlr->aer_outstanding--;
		}
		sys_assert(ctrlr->aer_outstanding == 0);
	}
AER_END:

#if defined(SRIOV_SUPPORT)
#error "todo"
	if (fid > SRIOV_VF_PER_CTRLR)
		return;

	if (fid == 0) {
		/* Disable Windows controller doesn't delete any queues, so does here */
		for (sqid = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL - 1; sqid >= 0; sqid--) {
			/* Handle PF SQs */
			if (ctrlr->sqs[sqid]) {
				if (sqid != 0) {
					struct nvmet_sq *sq = ctrlr->sqs[sqid];

					// move all running requests to abort request queue
					while (!list_empty(&sq->reqs)) {
						req_t *req = list_first_entry(&sq->reqs, req_t, entry);

						list_del_init(&req->entry);
						list_add_tail(&req->entry, &ctrlr->aborted_reqs);
					}

					nvmet_unbind_vfcq(fid, sqid, ctrlr->sqs[sqid]->cqid);
					nvmet_unset_vfsq(fid, sqid);
				}
				hal_nvmet_delete_vq_sq(sqid);
			}
		}

		for (cqid = 0; cqid < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL; cqid++) {
			/* Handle PF CQs */
			if (ctrlr->cqs[cqid]) {
				if (cqid != 0)
					nvmet_unset_vfcq(fid, cqid);

				hal_nvmet_delete_vq_cq(cqid);
			}
		}
	} else {
		/* Handle VF SQs. TODO: Check max VF sqid */
		u8 hst_sqid = SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC -1;
		for (; hst_sqid > 0; hst_sqid--) { /* Only IOSQs */
			if (ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid]) {
				if (hst_sqid != 0) {
					struct nvmet_sq *sq = ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid];
					/* move all running requests to abort request queue */
					while (!list_empty(&sq->reqs)) {
						req_t *req = list_first_entry(&sq->reqs, req_t, entry);

						list_del_init(&req->entry);
						list_add_tail(&req->entry, &ctrlr->aborted_reqs);
					}
					nvmet_unbind_vfcq(fid, hst_sqid, ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid]->cqid);
					nvmet_unset_vfsq(fid, hst_sqid);
				}
				/* For VFs use flexible sqid */
				u8 flx_sqid = nvmet_get_flex_sq_map_idx(fid, hst_sqid);
				hal_nvmet_delete_vq_sq(flx_sqid);
			}
		}

		/* Handle VF CQs */
		u8 hst_cqid; /* Only IOCQ */
		for (hst_cqid = 1; hst_cqid < SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC -1; hst_cqid++) {
			if (ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrcq[hst_cqid]) {
				if (hst_cqid != 0)
					nvmet_unset_vfcq(fid, hst_cqid);

				/* For VFs use flexible cqid */
				u8 flx_cqid = nvmet_get_flex_cq_map_idx(fid, hst_cqid);
				hal_nvmet_delete_vq_cq(flx_cqid);
			}
		}
	}
#else
	/* Disable Windows controller doesn't delete any queues, so does here */
	for (sqid = ctrlr->max_qid - 1; sqid >= 1; sqid--) {
		if (ctrlr->sqs[sqid]) {
			struct nvmet_sq *sq = ctrlr->sqs[sqid];

			// move all running requests to abort request queue
			while (!list_empty(&sq->reqs)) {
				req_t *req = list_first_entry(&sq->reqs, req_t, entry);

				list_del_init(&req->entry);
				list_add_tail(&req->entry, &ctrlr->aborted_reqs);
			}

			if (cmd_proc_del) {
				sys_assert(sqid != 0);
				set_bit((sqid - 1), cmd_proc_del);
				continue;
			}

			nvmet_unbind_cq(sqid, ctrlr->sqs[sqid]->cqid);
			nvmet_unset_sq(sqid);

#ifdef NVME_SHASTA_MODE_ENABLE
			hal_nvmet_delete_sq(sqid);
#else
			hal_nvmet_delete_vq_sq(sqid);
#endif
		}
	}

	for (cqid = 1; cqid < ctrlr->max_qid; cqid++) {
		if (ctrlr->cqs[cqid]) {
			nvmet_unset_cq(cqid);

#ifdef NVME_SHASTA_MODE_ENABLE
			hal_nvmet_delete_cq(cqid);
#else
			hal_nvmet_delete_vq_cq(cqid);
#endif
		}
	}
#endif
}

/*!
 * @brief callback function for shutdown request
 *
 * @param req		flush req
 *
 * @return		always return true
 */
static fast_code bool nvmet_fe_shutdown_done(req_t *req)
{
	nvmet_put_req(req);
	if (shutdown_cb != NULL)
#if defined(SRIOV_SUPPORT)
		shutdown_cb(req->fe.nvme.cntlid);
#else
		shutdown_cb();
#endif
	shutdown_cb = NULL;

	return true;
}

/*!
 * @brief callback function for shutdown request
 *
 * @param req		flush req
 *
 * @return		return false if no finished yet
 */
static fast_code bool nvmet_be_shutdown_done(req_t *req)
{
#if defined(HMB_SUPPORT)
	if (is_hmb_enabled()) {
		req->completion = nvmet_fe_shutdown_done;
		if (!hmb_detach(5)) {
			return false;
		}
	}
#endif
	nvmet_fe_shutdown_done(req);
	if(move_gc_resume && !(gResetFlag & BIT(cWarmReset)))	//move from cc_en set to here
	{
		evt_set_imt(evt_fe_gc_resume,0,0);// 	PCBaher plp01 workaround
	}
	move_gc_resume = false;
	return true;
}

/*!
 * @brief abort all running commands
 *
 * @return none
 */
static norm_ps_code void nvmet_abort_all_running_reqs(void)
{
	struct list_head *entry;
	struct list_head *next;
	u32 dummy_cnt = 0;

	list_for_each_safe(entry, next, &ctrlr->aborted_reqs) {
		req_t *req = container_of(entry, req_t, entry);

		nvmet_abort_running_req(req);

		nvme_apl_trace(LOG_ALW, 0x3068, "req 0x%p(%d) state %d abort, rt %d",
				req, req->opcode, req->state, nvmet_req_elapsed_time(req));
	}

	if (dummy_cnt) {
		hal_nvmet_io_stop();
		nvme_apl_trace(LOG_ERR, 0xd943, "all abort wreq cnt %d %d", aborting_write_reqs, dummy_cnt);
		evt_set_imt(evt_abort_wr_req, dummy_cnt, 0);
	}
}

/*!
 * @brief reset nvme core, clear IOQ, and abort all running cmds
 *
 * @return	not used
 */
fast_code void nvmet_core_reset(void)
{
	/* SRIOV: Now only PF0. TODO: This should reset all VFs */
	nvmet_clear_io_queue(0, NULL);
	nvmet_abort_all_running_reqs();
}

/*!
 * @brief controller shutdown event handler
 *
 * Shutdown the controller, set CC.EN 1 -> 0 and delete queues
 *
 * @param param		not used
 * @param payload	should be callback function of shutdown event
 * @param fid_isr	the bit31 indicate called from delay event, other bits is FID
 *
 * @return		None
 */
static fast_code void nvmet_evt_shutdown(u32 param, u32 payload, u32 fid_isr)
{
	req_t *req;

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_disable();// TODO DISCUSS need more time or not
	#endif

	if ((fid_isr & BIT31) == 0)
    {
        //btn_abort_all();
        nvmet_abort_all_running_reqs();
    }

    if (plp_trigger)
    {
        nvme_apl_trace(LOG_WARNING, 0x5770, "let plp flush");
        return;
    }

	if (nvmet_cmd_pending()) {
		nvme_apl_trace(LOG_INFO, 0x0653, "wait %d-%d-%d, %d",
				ctrlr->admin_running_cmds,
				ctrlr->cmd_proc_running_cmds,
				get_btn_running_cmds(),
				ctrlr->aer_outstanding);

		if ((ctrlr->admin_running_cmds - ctrlr->aer_outstanding) != 0 )
		{
			wait_done_recycle_cnt++;
			if(wait_done_recycle_cnt >= 3)
			{
				AplAdminCmdRelease();
			}
		}

		evt_set_delay(evt_shutdown, payload, BIT31 | fid_isr, 1);
		return;
	}

	wait_done_recycle_cnt = 0;

	req = nvmet_get_req();
	sys_assert(req);
	req->req_from = REQ_Q_OTHER;
#if defined(SRIOV_SUPPORT)
	/* To pass "function ID" to shutdown callback function */
	req->fe.nvme.cntlid = isr;
#endif

#if defined(RDISK)
	if (shutdown_cb == NULL) {
		shutdown_cb = (shutdown_cb_t)payload;
		/* we may have no req to build req to flush system if system was busy */
		req->opcode = REQ_T_FLUSH;
		req->completion = nvmet_be_shutdown_done;
		if(((shudown2000_flag == 1)&&(gResetFlag & BIT(cNvmeShutDown))) || (OTF_WARM_BOOT_FLAG == true))
		{
			req->op_fields.flush.shutdown = true;
			nvme_apl_trace(LOG_ERR, 0xd0ed, "shutdown2000_flag %d OTF_WARM_BOOT_FLAG %d", shudown2000_flag, OTF_WARM_BOOT_FLAG);
			shudown2000_flag = 0;
			OTF_WARM_BOOT_FLAG = false;
			ctrlr->nsid[0].ns->issue(req);
		}
		else
		{
			extern bool ucache_clean(void);
			req->op_fields.flush.shutdown = false;
			nvme_apl_trace(LOG_ERR, 0x1da6, "shutdown2000_flag %d", shudown2000_flag);
			if(ucache_clean())
				req->completion(req);//to save time
			else
				ctrlr->nsid[0].ns->issue(req);
		}
	}
	else {
#if defined(SRIOV_SUPPORT)
		((shutdown_cb_t)payload)(req->fe.nvme.cntlid);
#else
		shutdown_cb();
		shutdown_cb = (shutdown_cb_t)payload;
		nvmet_put_req(req);//bid82
#endif
	}
#else
	shutdown_cb = (shutdown_cb_t)payload;
	nvmet_be_shutdown_done(req);
#endif
}

static fast_code void nvmet_evt_fe_gc_resume(u32 param, u32 payload, u32 fid_isr)
{
	rdisk_trigger_gc_resume();
}

/*!
 * @brief controller Configuration enable event handler
 *
 * CC.en 0 -> 1, restore miscellaneous software structures
 *
 * @param cc		Value of controller configuration register
 *
 * @return		not used
 */
fast_code void nvmet_cc_en(u32 cc)
{
	int i = 0;
	u32 mps;

	//nvme_apl_trace(LOG_ALW, 0, "\033[91mCC.EN (0->1)\x1b[0m");
	/*
	**=============================================================
	**for pcie reset/nvme subsystem reset clear sq not completely
	**they can interrupt the hw flow of delete sq, but hw would reset
	**during pcie reset
	**controller reset should clear sq all then drive clear rdy.bit
	**============================================================
	*/
	//reset_del_sq_resource();
	flagtestS = flagtestC = 0;

	ctrlr->elog_cur = 0;
	ctrlr->elog_valid = 0;
	ctrlr->elog_tot = 0; /* Reload from Backend */

	/* Asynchronous Event Request initialization */
	ctrlr->aer_evt_bitmap = 0;

	/* Asynchronous Event Request initialization */
	ctrlr->aer_evt_bitmap = 0;
	ctrlr->aer_evt_mask_bitmap = 0;
	for (i = 0; i < NUMBER_OF_NVME_EVENT_TYPE; i++)
		ctrlr->aer_evt_sts[i] = 0;

	if (ctrlr->aer_outstanding != 0)
		sys_assert(0);

	ctrlr->aer_outstanding = 0;
	INIT_LIST_HEAD(&ctrlr->aer_outstanding_reqs);

	/* FIXME: Queue may be not empty */
	INIT_LIST_HEAD(&ctrlr->aborted_reqs);
	INIT_LIST_HEAD(&ctrlr->waiting_reqs);

	mps = ((cc >> 7) & 0xF);
	ctrlr->page_bits =  12 + mps;
	ctrlr->page_size =  1 << ctrlr->page_bits;

	/* controller reset saved value need*/
	ctrlr->saved_feat.ic_feat = ctrlr->def_feat.ic_feat;
	//ctrlr->cur_feat = ctrlr->saved_feat;
	hal_nvmet_set_aggregation(ctrlr->def_feat.ic_feat.b.thr, ctrlr->def_feat.ic_feat.b.time);

	cmd_proc_set_mps(mps);
	cmd_proc_disable_function_ram_init();

#if defined(RDISK)
	bg_enable();//bg disabled in host shutdown flush
#endif
	smart_stat_init_ctrl_busy_time(0);//for controller busy time
}

/*!
 * @brief controller AER error status handler
 *
 * @param sts		Event for error status type
 * @param param		error status parameter
 *
 * @return		None
 */
static ddr_code void nvmet_aer_error_status(enum nvme_event_error_status_type sts, u32 param)
{
	sys_assert(sts < NVME_EVENT_TYPE_MAX_STATUS);
	ctrlr->aer_evt_sts[NVME_EVENT_TYPE_ERROR_STATUS] |= BIT(sts);
#ifdef error_log
	struct nvme_error_information_entry *err = &ctrlr->elog[ctrlr->elog_cur];

	switch (sts) {
	case ERR_STS_INVALID_SQ:
	{
		/* Prepare Log Page */
#ifdef NVME_SHASTA_MODE_ENABLE
		err->sqid = param;
#else
		err->sqid = find_first_bit((void *)&param, 9);
#endif
		nvme_apl_trace(LOG_ERR, 0x5142, "SQ(%d) Invalid Queue", err->sqid);
		err->cid = INVALID_SQ_ERR;
		err->error_location = 0xffff;
		err->vendor_specific = 0;
		err->cmd_specific = 0;
	}
		break;
	case ERR_STS_INVALID_DB_WR:
	{
#ifdef NVME_SHASTA_MODE_ENABLE
		err->sqid = param;
#else
		err->sqid = find_first_bit((void *)&param, 9);
#endif
		err->cid = INVALID_DB_WR;
		err->error_location = 0xffff;
		err->vendor_specific = 0;
		err->cmd_specific = 0;
	}
		break;
	case ERR_STS_PERSISTENT_INTERNAL_DEV_ERR:
	{//todo
		nvme_apl_trace(LOG_ERR, 0x8119, "internal error type: %d", param);
		err->sqid = INTERNAL_ERR;
		err->cid = INTERNAL_ERR;
		err->error_location = 0xffff;

		err->vendor_specific = param;
		err->cmd_specific = 0;
	}
		break;
	case ERR_STS_DIAG_FAIL:
	case ERR_STS_TRANSIENT_INTERNAL_DEV_ERR:
	case ERR_STS_FW_IMG_LOAD_ERR:
		break;
	}

	err->error_count = ++ctrlr->elog_tot;
	if (ctrlr->elog_valid <= IDTFY_ELPE)
		ctrlr->elog_valid++;
	ctrlr->elog_cur = (ctrlr->elog_cur + 1) & IDTFY_ELPE;

	smart_inc_err_cnt();
#else
	switch (sts) {
	case ERR_STS_INVALID_SQ:
	case ERR_STS_INVALID_DB_WR:
	case ERR_STS_PERSISTENT_INTERNAL_DEV_ERR:
	case ERR_STS_DIAG_FAIL:
	case ERR_STS_TRANSIENT_INTERNAL_DEV_ERR:
	case ERR_STS_FW_IMG_LOAD_ERR:
		break;
	}
#endif
	return;
}


/*!
 * @brief controller for smart health of asynchronous event request handler
 *
 * @param sts		event for SMART/health status type
 * @param param		SMART/health AER event parameter
 *
 * @return		None
 */
static ddr_code void nvmet_aer_smart_health(enum nvme_event_smart_health_status_type sts, u32 param)
{
	ctrlr->aer_evt_sts[NVME_EVENT_TYPE_SMART_HEALTH] |= BIT(sts);
	switch (sts) {
	case SMART_STS_RELIABILITY:
		break;
	case SMART_STS_TEMP_THRESH:
		break;
	case SMART_STS_SPARE_THRESH:
		break;
	}
	return;
}

/*!
 * @brief controller for notice of asynchronous event request handler
 *
 * @param sts		event for notice status type
 * @param param		Notice AER event parameter
 *
 * @return		None
 */
static void nvmet_aer_notice(enum nvme_event_notice_status_type sts, u32 param)
{

	ctrlr->aer_evt_sts[NVME_EVENT_TYPE_NOTICE] |= BIT(sts);
	switch (sts) {
	case NOTICE_STS_NAMESPACE_ATTRIBUTE_CHANGED:
	case NOTICE_STS_FIRMWARE_ACTIVATION_STARTING:
		break;
	case NOTICE_STS_PROGRAM_FAIL:
	case NOTICE_STS_ERASE_FAIL://err handle will handle these case,additional smart will get the count
	case NOTICE_STS_GC_READ_FAIL:
		break;                     //nothing to do here

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	case NOTICE_STS_TELEMETRY_LOG_CHANGED:
		break;
#endif
	}
	return;
}
static void nvmet_aer_io_command_specific_status(enum nvme_event_io_command_status_type sts, u32 param)
{

	ctrlr->aer_evt_sts[NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS] |= BIT(sts);
	switch (sts) {
	case NOTICE_STS_RESERVATION_LOG_PAGE_AVAILABLE:
	case NOTICE_STS_SANITIZE_OPERATION_COMPLETED:
	case NOTICE_STS_SANITIZE_OPERATION_COMPLETED_WITH_UN_DA:
		break;
	}
	return;
}

/*!
 * @brief core handler for asynchronous event request
 *
 * @param types_sts		asynchronous event type
 * @param param			AER event parameter
 * @param fid			function id
 *
 * @return		not used
 */
fast_code void nvmet_evt_aer_in(u32 type_sts, u32 param)
{
	enum nvme_asynchronous_event_type evt_type = (enum nvme_asynchronous_event_type) (type_sts >> 16);
	enum nvme_event_error_status_type sts = (enum nvme_event_error_status_type) (type_sts & 0xFFFF);

    if(ctrlr->aer_evt_sts[evt_type] & BIT(sts))return;

	/* Set the Event Type Bitmap */
	ctrlr->aer_evt_bitmap |= BIT(evt_type);

	switch (evt_type) {
	case NVME_EVENT_TYPE_ERROR_STATUS:
		nvmet_aer_error_status(sts, param);
		break;
	case NVME_EVENT_TYPE_SMART_HEALTH:
		nvmet_aer_smart_health(sts, param);
		break;
	case NVME_EVENT_TYPE_NOTICE:
		nvmet_aer_notice(sts, param);
		break;
	case NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS:
		nvmet_aer_io_command_specific_status(sts, param); // Used to solve some errors in diverMaster_Sanitize testing . 2023/8/17 shengbin yang
		break;
	case NVME_EVENT_TYPE_VENDOR_SPECIFIC:
	default:
		return;
	}

	/*
		nvme_event_smart_critical_en_type
	*/
	////Andy test IOL 1.7 need mark
	#if 0
	if (!(ctrlr->cur_feat.aec_feat.b.smart & param) && (NVME_EVENT_TYPE_SMART_HEALTH == evt_type)){
		return ;
	}
	#endif

	if (ctrlr->aer_outstanding != 0) {
		req_t *req = list_first_entry(&ctrlr->aer_outstanding_reqs, req_t, entry);

		list_del_init(&req->entry);
		ctrlr->aer_outstanding--;
		evt_set_imt(evt_aer_out, (u32) req, 0);
	}

	return;
}

fast_code void nvmet_sq_timer_enable(void)
{
	mod_timer(&sq_timer, jiffies + 2*HZ);
}

#if !defined(FPGA) && CPU_ID == 1
ddr_code void trigger_vol_mon_task(u32 r0, u32 r1, u32 r2)
{
	extern __attribute__((weak)) void ncl_cmd_power_loss(void);
	extern __attribute__((weak)) void ftl_cmd_power_loss(void);
	///< step 1/2, in IRQ mode
	///< step 3. notify ncb
	ncl_cmd_power_loss();

	///< step 4. notify ftl
	ftl_cmd_power_loss();

#if defined(RDISK)
	extern void rdisk_power_loss_flush(void);
	rdisk_power_loss_flush();
#endif
}
#endif

//-----------------------------------------------------------------------------
/**
    HalWriteDtagRelease
**/
//-----------------------------------------------------------------------------
//fast_code void HalWriteDtagRelease(void)
ddr_code void HalWriteDtagRelease(void)
{
    u32 stuck_dtag[2];
    u8 stuck_dtag_cnt = 0;
    stuck_dtag_cnt = cmd_proc_wdma_stuck_dtag(&stuck_dtag[0]);
    if (stuck_dtag_cnt > 0)
    {
        u8 i = 0;
        while (i < stuck_dtag_cnt)
        {
            sys_assert(stuck_dtag[i] != _inv_dtag.dtag);

            dtag_t dtag = { .dtag = stuck_dtag[i], };

            btn_err_dtag_ret(dtag);

            i ++;
        }
    }
}

//-----------------------------------------------------------------------------
/**
    Handle PERST reset flow
**/
//-----------------------------------------------------------------------------
fast_code static void MdlHost_Perst_Handle(u32 reason, u32 _cb)
{
    rst_post_cb cb = (rst_post_cb) _cb;
    //nvme_apl_trace(LOG_ERR, 0, "_cb_revise:%x\n",_cb);

    #if (CO_SUPPORT_NSSR_PERST_FLOW == FEATURE_SUPPORTED)
    if (gResetFlag & BIT(cNvmeSubsystemReset))
    {
        disable_ltssm(1);
    }
    #endif

    /* 1. abort outstanding Admin/IO commands and reset CQ/SQ */
    nvmet_core_reset();

    /* 2. reclaim interface resource between NVME HW & FW, disable FE DMA */
    hal_nvmet_reset();

    /* 3. reset NVME module */
    misc_reset(RESET_NVME);

    /* 4. reset PCIE */
    if (PCIE_RST == reason)
    {
        misc_reset(RESET_PCIE);
    }

    /* 5. resume NVME module */
    extern void hal_nvmet_resume(void);
    hal_nvmet_resume();

	#if (_TCG_)
		if (gResetFlag & (BIT(cNvmeSubsystemReset) | BIT(cNvmePCIeReset)))
			TcgHardReset();
	#endif

    u32 flags = irq_save();

    /* unblock SQE/CQE fetching/returning, need to be protected by irq
    disabling. Don't worry subsequent reset before BTN reset completes,
    since CC.EN 0->1 will not be answered before BTN reset done if needed */
    misc_nvm_ctrl_reset(false);

    // call post handler of target reset event
    sys_assert(cb);
    cb(reason);

    //gResetFlag &= ~BIT(cNvmePCIeReset);
    if(gResetFlag & BIT(cNvmePCIeReset))
    {
		gResetFlag = 0;
	}
    irq_restore(flags);
}

//-----------------------------------------------------------------------------
/**
    reset internal processing

    @param[in]  reason     : reset type
    @param[in]  _cb        : callback

    @return status
**/
//-----------------------------------------------------------------------------
fast_code RstStatus_t Host_ResetCore(u32 reason, u32 _cb)
{
    //extern __attribute__((weak)) bool ncl_cmd_empty(bool rw);
    //extern __attribute__((weak)) void ncl_cmd_block(void);

    // /* 1. prepare reset ,handle incoming command first */
    // btn_handle_incoming_cmd();

    /* 2. Backup SMART attribute data */
    SmartDataBackup(cLogSmartBackupPOR);

	#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	if (gResetFlag & BIT(cNvmePCIeReset))
    {
    	telemetry_preset_update = 1;
    }
	#endif

    #if (CO_SUPPORT_READ_AHEAD == TRUE)
    ra_disable_time(5); // 500ms // TODO DISCUSS
    #endif

    btn_io_queue_debug();

    if ((ctrlr->admin_running_cmds - ctrlr->aer_outstanding) != 0 )
    {
        AplAdminCmdRelease();
    }

    if (nvmet_cmd_pending())
    {
#if(NSSR_RST_DELAYA == ENABLE)	 //for jira_10. solution(5/12).shengbin yang 2023/11/15
   		if(gResetFlag & BIT(cNvmeSubsystemReset))
		{
			if(reset_start_time == 0)
			{
				reset_start_time = get_tsc_64();
				/*nvme_hal_trace(LOG_ALW, 0xa23d, "reset_start_time(0x%x - %x),[DEBG] %d-%d-%d, %d",
					(reset_start_time >> 32),reset_start_time,
	                ctrlr->admin_running_cmds,
	                ctrlr->cmd_proc_running_cmds,
	                get_btn_running_cmds(),
	                ctrlr->aer_outstanding);*/
				gRstFsm = RST_STATE_CORE;
    			return RST_RET_POSTPONE;
			}
			else
			{
				if((time_elapsed_in_ms(reset_start_time)) < 10)	//10ms
				{
					reset_delay_cnt++;
					gRstFsm = RST_STATE_CORE;
    				return RST_RET_POSTPONE;
				}
			}
		}
#endif
        /*nvme_apl_trace(LOG_INFO, 0x9350, "[DEBG] %d-%d-%d, %d",
                ctrlr->admin_running_cmds,
                ctrlr->cmd_proc_running_cmds,
                get_btn_running_cmds(),
                ctrlr->aer_outstanding);*/

        gPerstInCmd = true;
        hostReset = true;
    }

	/*Turn off ts .base plp handle flow. 2024/01/12 shengbin yang*/
	ts_reset_sts = true;

    if (gPerstInCmd || ctrl_fatal_state)
    {
        // /* 1. prepare reset ,handle incoming command first */
        btn_handle_incoming_cmd();

        /* 3. Wait job done */
        rdisk_reset_disk_handler();

    	/* 4. Wait for NCL to become IDLE */
    	// ncl_cmd_block();

        /* 5. While pcie reset, need to find the write dtag and push a dummy to avoid hang */
        HalWriteDtagRelease();

        /* 6. drain all pending streaming xfer from NCB */
        while (ncl_cmd_empty && (ncl_cmd_empty(false) == false))
        {
            //nvme_apl_trace(LOG_ERR, 0, "[RSET] WAIT RNCL");
            //btn_feed_rd_dtag();	//for jira_10. solution(6/12).shengbin yang 2023/11/15
            extern void btn_de_rd_grp0(void);
			extern void bm_isr_com_free(void);
			extern void btn_data_in_isr(void);
    		btn_feed_rd_dtag();
			bm_handle_rd_err();
			btn_de_rd_grp0();
			bm_isr_com_free();
			btn_data_in_isr();
        }
    }
/*#if(NSSR_RST_DELAYA == ENABLE)
	else if(reset_start_time != 0)
	{
		nvme_apl_trace(LOG_ALW, 0x9715, "Delay filtered out 2-1 BTN");
	}
#endif*/
#if(NSSR_RST_DELAYA == ENABLE)
    nvme_apl_trace(LOG_ERR, 0x9c3d, "[RSET] #0 Msg, reset_delay_cnt(%d),[DEBG] %d-%d-%d, %d",
    			reset_delay_cnt,
    			ctrlr->admin_running_cmds,
                ctrlr->cmd_proc_running_cmds,
                get_btn_running_cmds(),
                ctrlr->aer_outstanding);
#else
	nvme_apl_trace(LOG_ERR, 0x72f1, "[RSET] #0 Msg,[DEBG] %d-%d-%d, %d",
				ctrlr->admin_running_cmds,
                ctrlr->cmd_proc_running_cmds,
                get_btn_running_cmds(),
                ctrlr->aer_outstanding);
#endif
    gRstFsm = RST_STATE_CACHE;
    return RST_RET_CONTINUE;
}

//-----------------------------------------------------------------------------
/**
    flush cache data

    @param[in]  reason     : reset type
    @param[in]  _cb        : callback

    @return status
**/
//-----------------------------------------------------------------------------
fast_code RstStatus_t Host_ResetCache(u32 reason, u32 _cb)
{
    rdisk_reset_cache();

    // TODO HOW ABOUT PROGRAM ERROR

    nvme_apl_trace(LOG_ERR, 0x2b0e, "[RSET] #1 Cache");
    gRstFsm = RST_STATE_HARDWARE;
    return RST_RET_CONTINUE;
}

//-----------------------------------------------------------------------------
/**
    Handle Controller Reset

    @param[in]  resetMode        : reset mode

    @return error code
**/
//-----------------------------------------------------------------------------
fast_code RstStatus_t Host_ResetHardware(u32 reason, u32 _cb)
{
    MdlHost_Perst_Handle(reason, _cb);

    // TODO flag setting
    if (gResetFlag & BIT(cNvmePCIeReset))
    {
    }

    if (gResetFlag & BIT(cNvmeFlrPfReset))
    {
        gResetFlag &= ~BIT(cNvmeFlrPfReset);
    }

    if (gResetFlag & BIT(cNvmeSubsystemReset))
    {
        gResetFlag &= ~BIT(cNvmeSubsystemReset);
    }

	if(gResetFlag & BIT(cNVMeLinkReqRstNot))
	{
		gResetFlag &= ~BIT(cNVMeLinkReqRstNot);
	}

    nvme_apl_trace(LOG_ERR, 0xfa89, "[RSET] #2 HW");

    if (gPerstInCmd || ctrl_fatal_state)
    {
        gRstFsm = RST_STATE_BTN;
    }
    else
    {
        gRstFsm = RST_STATE_FINISH;
    }

    return RST_RET_CONTINUE;
}

//-----------------------------------------------------------------------------
/**
    Reset BTN module (SDK flow)

    @param[in]  reason     : reset type
    @param[in]  _cb        : callback

    @return status
**/
//-----------------------------------------------------------------------------
extern  bool dpe_busy;
extern void bm_data_copy_pend_out();
extern u16 dpe_ctxs_allocated_cnt;
extern u32 bm_entry_pending_cnt;
fast_code RstStatus_t Host_ResetBTN(u32 reason, u32 _cb)
{
    //extern __attribute__((weak)) void ncl_handle_pending_cmd(void);
    /* prepare reset btn */
    //btn_reset_pending();
    bm_entry_pending_cnt = 0;
    dpe_busy = 1;
    if(dpe_ctxs_allocated_cnt)	//handle dpe_cb before resetBTN. 23/11/24 shengbin add for Wreq_cnt != 0
	{
		dpe_handle_incoming();
	}
    //nvme_apl_trace(LOG_WARNING, 0x0424, "[BTN ] Ready for reset");

    rdisk_reset_l2p_handler();

    misc_reset(RESET_BM);
    nvme_apl_trace(LOG_ERR, 0xbd1e, "[RSET] #2-1 BTN");

    gRstFsm = RST_STATE_FINISH;
    return RST_RET_CONTINUE;
}

//-----------------------------------------------------------------------------
/**
    reset event finialization
**/
//-----------------------------------------------------------------------------
fast_code RstStatus_t Host_ResetFinish(u32 reason, u32 _cb)
{
    //extern __attribute__((weak)) void ncl_handle_pending_cmd(void);

    if (gPerstInCmd || ctrl_fatal_state)
    {
        // re-initial BTN
        btn_reset_resume();
        rdisk_reset_resume_handler();

        /* resume NCL, and restore write cmd hander */
        //ncl_handle_pending_cmd();
    }

    // TODO reinit something
    gPerstInCmd = false;
    hostReset = false;
    ctrl_fatal_state = false;

    //ECCT_HIT_2_FLAG = false; //hardware issue

#if CO_SUPPORT_DEVICE_SELF_TEST
	DST_Completion(cDSTAbortReset);
#endif
/*
#if defined(USE_CRYPTO_HW)
#if (_TCG_)
	extern void tcg_init_aes_key_range(void);
	tcg_init_aes_key_range();
#else
	for (u8 cryptID = 0; cryptID < 64; cryptID++)
	{
		crypto_AES_EPM_Read(cryptID, 0);
	}
#endif
#endif
*/
#if(NSSR_RST_DELAYA == ENABLE)
	reset_start_time = 0;
	reset_delay_cnt = 0;
#endif

	/*Turn on ts .base plp handle flow. 2024/01/12 shengbin yang*/
	ts_reset_sts = false;

    nvme_apl_trace(LOG_WARNING, 0x0f43, "[RSET] #3 Finish");

    bm_entry_pending_cnt = 0;

    if (misc_is_fw_wait_reset())
    {
        extern void HalNvmeClearNssro(void);
        HalNvmeClearNssro();
    }

	#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	if(telemetry_preset_update) //for edevx
	{
		spin_lock_take(SPIN_LOCK_KEY_JOURNAL, 0, true);
		journal_update(JNL_TAG_REBOOT, 0);
		spin_lock_release(SPIN_LOCK_KEY_JOURNAL);

		telemetry_preset_update = 0;

		extern void telemetry_ctrlr_data_update();
		telemetry_ctrlr_data_update();

		//extern u8 telemetry_update_ctrlr_singnal;
		//telemetry_update_ctrlr_singnal = 1;
		extern epm_info_t *shr_epm_info;
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		epm_smart_data->telemetry_update_ctrlr_signal = 1;
	}
	#endif


#if CO_SUPPORT_SANITIZE
	// Sanitize break handling
	extern epm_info_t *shr_epm_info;
	epm_smart_t* epm_smart_sntz = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	if(epm_smart_sntz->sanitizeInfo.sanitize_Tag == 0xDEADFACE)
	{
		if(epm_smart_sntz->sanitizeInfo.fwSanitizeProcessStates)
		{
			extern u8 evt_admin_sanitize_operation;
			evt_set_cs(evt_admin_sanitize_operation, 0, 0, CS_TASK);
		}
	}
#endif

    dpe_busy = 0;
    bm_data_copy_pend_out();

#if defined(USE_CRYPTO_HW)
#if (_TCG_)
	extern void tcg_init_aes_key_range(void);
	tcg_init_aes_key_range();
#else
	for (u8 cryptID = 0; cryptID < 64; cryptID++)
	{
		crypto_AES_EPM_Read(cryptID, 0);
	}
#endif
#endif

    return RST_RET_ESCAPE;
}

fast_code void reset_task_handler(u32 p0, u32 _cb, u32 reason /* PCIE_RST_XXX */)
{
    if (gPcieRstRedirected == true)
    {
        RstStatus_t ret;
#if(PLP_SUPPORT == 1)
		gpio_int_t gpio_int_status;
#endif

        do {
#if(PLP_SUPPORT == 1)
			gpio_int_status.all = misc_readl(GPIO_INT);
			if(((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))||plp_trigger) && gRstFsm != RST_STATE_FINISH)
#else
            if (plp_trigger && gRstFsm != RST_STATE_FINISH)
#endif
            {
                rtos_unlock_other_cpu(); // avoid reset in #2-1 state
                nvme_apl_trace(LOG_WARNING, 0x36c2, "Stop in state:%d",gRstFsm);
                break;
            }

            ret = gProcHostRst[gRstFsm](reason , _cb);

#if(PLP_SUPPORT == 1)
			gpio_int_status.all = misc_readl(GPIO_INT);
			if(((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))||plp_trigger) && gRstFsm != RST_STATE_FINISH)
#else
			if (plp_trigger && gRstFsm != RST_STATE_FINISH)
#endif
            {
                rtos_unlock_other_cpu();
                nvme_apl_trace(LOG_WARNING, 0xe845, "[PLP ] RST:%d",gRstFsm);
                break;
            }
            if (ret != RST_RET_CONTINUE)
            {
                if (ret == RST_RET_ESCAPE)
                {
                    gRstFsm = RST_STATE_CORE;
                    break;
                }

                if (ret == RST_RET_POSTPONE)
                {
                    // TODO if need
                   if(gResetFlag & BIT(cNvmeSubsystemReset))	 //for jira_10. solution(7/12).shengbin yang 2023/11/15
					{
						extern void pcie_rst_post(u32 reason);
						if (evt_perst_hook != 0xff)
							urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST);
					}
                    break;
                }
            }

        } while(true);
        gPerstInCmd = false;
    }
    else
    {
        MdlHost_Perst_Handle(reason, _cb);
    }
}

fast_code void nvmet_set_fw_slot_revision(u8 slot, u64 revision)
{
	sys_assert((slot >= 1) && (slot <= 7));
	slot--;
	ctrlr->fw_slot_version[slot] = revision;
}

fast_code void nvmet_io_fetch_ctrl(bool stop)
{
	if (stop)
		hal_nvmet_suspend_cmd_fetch();
	else
		hal_nvmet_enable_cmd_fetch();
}

/*!
 * @brief start warm boot flow
 *
 * @return		none
 */
 extern struct timer_list GetSensorTemp_timer;
fast_code void nvmet_start_warm_boot(void)
{
#if (_TCG_)
	// update EPM
	extern epm_info_t *shr_epm_info;
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	epm_aes_data->tcg_sts	  = mTcgStatus;
	epm_aes_data->readlocked  = mReadLockedStatus;
	epm_aes_data->writelocked = mWriteLockedStatus;
	epm_update(AES_sign, (CPU_ID - 1));
#endif

	struct list_head *cur, *saved;
#if 1 //STOP_PRRD_WARMBOOT 20210302-Eddie
	//Stop Patrol Read & Refresh Read
	misc_set_STOP_BGREAD();
#endif

	del_timer(&GetSensorTemp_timer);

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_disable_time(20); // 2s // TODO DISCUSS
	#endif

	//reset_sq_arbitration_en();
	hal_nvmet_suspend_cmd_fetch();

	/* If Asynchronous Event Requests commands are outstanding when the controller is reset,
	 * the commands are abort */
	if (ctrlr->aer_outstanding != 0) {
		//nvme_apl_trace(LOG_DEBUG, 0, "abort #(%d) AER commands", ctrlr->aer_outstanding);
		list_for_each_safe(cur, saved, &ctrlr->aer_outstanding_reqs) {
			req_t *req = container_of(cur, req_t, entry);
			req->start = jiffies;
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_ABORTED_POWER_LOSS);
			list_del_init(&req->entry);
			list_add_tail(&req->entry, &ctrlr->aer_outstanding_reqs);

			if (req->completion)
				req->completion(req);
			ctrlr->aer_outstanding--;
		}
		sys_assert(ctrlr->aer_outstanding == 0);
	}
#if CO_SUPPORT_DEVICE_SELF_TEST
	DST_Completion(cDSTAbortReset);
#endif

	//extern void nvme_save_aer_for_each_func(void);
	//nvme_save_aer_for_each_func();
	nvme_apl_trace(LOG_INFO, 0x5787, "warm boot disable sq OK");

	//mod_timer(&wb_timer, jiffies + 20);
	mod_timer(&wb_timer, jiffies + HZ/10);	//20201225-Eddie
}
#if UART_STARTWB	//20210517-Eddie
slow_code_ex void start_warm_boot_timer(void *data)
{
	struct list_head *cur, *saved;

	hal_nvmet_suspend_cmd_fetch();

	/* If Asynchronous Event Requests commands are outstanding when the controller is reset,
	 * the commands are abort */
	if (ctrlr->aer_outstanding != 0) {
		nvme_apl_trace(LOG_DEBUG, 0x7acb, "abort #(%d) AER commands", ctrlr->aer_outstanding);
		list_for_each_safe(cur, saved, &ctrlr->aer_outstanding_reqs) {
			req_t *req = container_of(cur, req_t, entry);
			req->start = jiffies;
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_ABORTED_POWER_LOSS);
			list_del_init(&req->entry);
			list_add_tail(&req->entry, &ctrlr->aer_outstanding_reqs);

			if (req->completion)
				req->completion(req);
			ctrlr->aer_outstanding--;
		}
		sys_assert(ctrlr->aer_outstanding == 0);
	}
	nvme_apl_trace(LOG_INFO, 0x9408, "warm boot disable sq OK");

#ifdef MEASURE_TIME
	recMsec[10]= *(vu32*)0xC0201044;
#endif
	//20210514-Eddie-Delete BG Action
#if	0
	del_timer(&refresh_read_timer);
	del_timer(&patrol_read_timer);
	del_timer(&GetSensorTemp_timer);
#endif
	del_timer(&GetSensorTemp_timer);
	//mod_timer(&wb_timer, jiffies + 20);
	mod_timer(&wb_timer, jiffies + 1);	//20201225-Eddie
}
#endif

#if (PI_FW_UPDATE == mENABLE)		//20210110-Eddie
slow_code_ex void nvmet_pi_suspend_timer(void *data)
{		//20210110-Eddie
	if (ctrlr->cmd_proc_running_cmds || get_btn_running_cmds()) {
		mod_timer(&pi_timer, jiffies + HZ/10);
		nvme_apl_trace(LOG_ALW, 0x97ab, "pi sus wait io");

		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(20); // 2s
		#endif

		return;
	}
	nvme_apl_trace(LOG_ALW, 0xd896, "pi sus done");
}
#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
slow_code_ex void nvmet_pi_suspend_fwdl_timer(void *data)
{
	if(plp_trigger)
	{
		return;
	}

	//20210110-Eddie
	if (ctrlr->cmd_proc_running_cmds || get_btn_running_cmds()) {
		mod_timer(&pi_fwdl_timer, jiffies + HZ/10);
		nvme_apl_trace(LOG_ALW, 0x3a7d, "pi sus wait io");

		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(20); // 2s
		#endif

		return;
	}
	nvme_apl_trace(LOG_ALW, 0x6ae6, "pi sus done");
	#if GC_SUSPEND_FWDL// GC suspend while FW download -20210308-Eddie
	if ((cmf_idx == 3) || (cmf_idx == 4)){	//Temporarily +8 need to GC suspend
		evlog_printk(LOG_ALW,"[FW DL] GC suspend");
		FWDL_GC_Handle(GC_ACT_SUSPEND);
		while (!fwdl_gcsuspend_done)
		{
			cpu_msg_isr();
			extern void bm_isr_com_free(void);
			bm_isr_com_free();
		}	//Lock Until GC handle done -> CPU3 FWDL_GC_Handle FTL.C
		fwdl_gcsuspend_done = 0;
	}
	#endif

	evt_set_imt(evt_fw_dwnld, pi_req1, 0);	//wait for 1 sec cmds consuming
}

slow_code_ex void nvmet_pi_suspend_fwca_timer(void *data)
{		//20210110-Eddie
	if (ctrlr->cmd_proc_running_cmds || get_btn_running_cmds()) {
		mod_timer(&pi_fwca_timer, jiffies + HZ/10);
		nvme_apl_trace(LOG_ALW, 0x04e4, "pi sus wait io");
		return;
	}
	nvme_apl_trace(LOG_ALW, 0xb5f9, "pi sus done");
	#if GC_SUSPEND_FWDL// GC suspend while FW download -20210308-Eddie
	if ((cmf_idx == 3) || (cmf_idx == 4)){	//Temporarily +8 need to GC suspend
		evlog_printk(LOG_ALW,"[FW DL] GC suspend");
		FWDL_GC_Handle(GC_ACT_SUSPEND);
		while (!fwdl_gcsuspend_done)
		{
			cpu_msg_isr();
			extern void bm_isr_com_free(void);
			bm_isr_com_free();
		}	//Lock Until GC handle done -> CPU3 FWDL_GC_Handle FTL.C
		fwdl_gcsuspend_done = 0;
	}
	#endif

	evt_set_cs(evt_fw_commit, pi_req1, pi_req2, CS_TASK);

}
#endif
slow_code_ex void nvmet_pi_start_cmd_suspend(req_t *req)
{		//20210110-Eddie
	//struct list_head *cur, *saved;
	struct nvme_cmd *nvme_cmd = req->host_cmd;

	hal_pi_suspend_cmd_fetch();

	//nvme_apl_trace(LOG_ALW, 0, "PI FWhandle disable sq OK");
#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
	if (nvme_cmd->opc == NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD)
		mod_timer(&pi_fwdl_timer, jiffies + HZ/10);
	else if (nvme_cmd->opc == NVME_OPC_FIRMWARE_COMMIT)
		mod_timer(&pi_fwca_timer, jiffies + HZ/10);
#else
	//mod_timer(&pi_timer, jiffies + HZ/10);
#endif
}
#endif
/*!
 * @brief warm boot reset cpu
 *
 * @return		none
 */
static fast_code void nvmet_warm_boot_reset(void)
{
	extern void ftl_warm_boot_handle(void);

	extern __attribute__((weak)) void ncl_cmd_wait_completion();
#if(WB_IDLE_MODE == ENABLE)
	l2p_idle_ctrl_t ctrl;		//20210426-Eddie wait l2pe idle
#endif

	OTF_WARM_BOOT_FLAG = true;	//delete refresh read in cpu4

#ifdef MEASURE_TIME
	recMsec[8] = *(vu32*)0xC0201044;
    	evlog_printk(LOG_ALW,"[M_T]Uncached Handle (SHT2000) %d ms\n", (recMsec[8]-recMsec[7])/800000);
#endif

#ifdef MEASURE_TIME
	u64 t3;
	t3 = get_tsc_64();
	//recMsec[1] = *(vu32*)0xC0201044;
#endif

	cmd_proc_suspend();
	gResetFlag &= ~BIT(cWarmReset);

	nvmet_update_smart_stat(NULL);//smart update


#if(WARMBOOT_FTL_HANDLE == mENABLE)
#if(WA_FW_UPDATE == ENABLE)
{
#if 1
	#if(TRIM_SUPPORT == ENABLE)
		TrimPowerLost(true);
	#endif
		epm_update(EPM_POR, (CPU_ID - 1));  //SAVE ALL EPM data
		//20210317-Eddie-wait-cpu 4 complete EPM update
		while ((!wb_epm_lock_done) || (!wb_frb_lock_done)){
			extern void btn_data_in_isr(void);
	        	btn_data_in_isr();
		}
		wb_epm_lock_done = 0;
		wb_frb_lock_done = 0;
		nvme_apl_trace(LOG_INFO, 0x6635, "OTF_WARM_BOOT_WA save qbt and epm done");
#endif
}
#else
#if(TRIM_SUPPORT == ENABLE)
	TrimPowerLost((true));
#endif

#if EPM_OTF_Time
	ftl_warm_boot_handle();

    while ( (remote_init_item(warm_boot_ftl_handle) == 0) ) {

		if(plp_trigger)
		{
			return ;
		}

        extern void btn_data_in_isr(void);
        btn_data_in_isr();
        dpe_isr();  // for PBT raid bm_copy_done
        cpu_msg_isr();  // for PBT raid bm_data_copy
	}
#else
	epm_update(EPM_POR, (CPU_ID - 1));  //SAVE ALL EPM data
	ftl_warm_boot_handle();

    while ( (remote_init_item(warm_boot_ftl_handle) == 0) || (!wb_epm_lock_done) ) {
        extern void btn_data_in_isr(void);
        btn_data_in_isr();
	}
#endif
	wb_epm_lock_done = 0;
#endif
#endif
#if(WB_IDLE_MODE == ENABLE)
	//20210426-Eddie wait l2pe idle
	ctrl.all = 0;
	ctrl.b.all = 1;
	ctrl.b.wait = 1;
	wait_l2p_idle(ctrl);
#endif
#ifdef OTF_MEASURE_TIME
    recMsec[9] = *(vu32*)0xC0201044;
    evlog_printk(LOG_ALW,"[M_T]Commit2WRAMRest %8d ms\n", (recMsec[9]-recMsec[0])/800000);
#endif
	//mdelay(1000);
	/* Clean up NCB and wait for it become idle */
	ncl_cmd_wait_completion();	//20200317-Eddie
	nvme_apl_trace(LOG_INFO, 0x6ca9, "Ficu Done");
	warm_boot();
	//nvme_apl_trace(LOG_INFO, 0, "warm boot cmd proc resume");
	//cmd_proc_resume(0);
	//hal_nvmet_enable_cmd_fetch();
	nvme_apl_trace(LOG_INFO, 0x4c48, "warm boot end");

}

/*!
 * @brief warm boot timer handle
 *
 * @param data timer name
 *
 * @return		none
 */
 typedef union {
	u32 all;
	struct {
		u32 wptr:8;	///< tcm updated queue write pointer
		u32 rptr:8;	///< tcm updated queue read pointer
		u32 rsvd:16;
	} b;
} entry_pnter;
typedef union {
    u32 all;
    struct {
        u32 fw_cq_reqs_wptr:12;
        u32 rsvd_12:4;
        u32 fw_cq_reqs_rptr:12;
        u32 rsvd_28:4;
    } b;
}cq_ptr;
ddr_code bool check_cmd_qptr(void)
{
	entry_pnter entry1,entry2,entry3,entry4,entry5;
	entry1.all = readl((void*)0xC0037804);//check admin q
	entry2.all = readl((void*)0xC0010368);//normal read
	entry3.all = readl((void*)0xC0010374);//p read
	entry4.all = readl((void*)0xC0010380);//write q2
	entry5.all = readl((void*)0xC001038C);//write q3

	if(entry1.b.wptr != entry1.b.rptr ||
		entry2.b.wptr != entry2.b.rptr ||
		entry3.b.wptr != entry3.b.rptr ||
		entry4.b.wptr != entry4.b.rptr ||
		entry5.b.wptr != entry5.b.rptr)
	{
		nvme_apl_trace(LOG_INFO, 0x3249, "cmd exist in hw q");
		return true;
	}else
		return false;
}
ddr_code bool check_cmd_cqptr(void)
{
	cq_ptr entry1;

	entry1.all = readl((void*)0xC0032028);//acq
	u32 entry2 = readl((void*)0xC0032090);//check i/o cq


	if(entry1.b.fw_cq_reqs_rptr != entry1.b.fw_cq_reqs_wptr ||
		entry2 & 0xff)
	{
		nvme_apl_trace(LOG_INFO, 0x72ba, "cq not empty");
		return true;
	}else
		return false;
}
ddr_code bool check_data_entry_qptr(void)
{
	entry_pnter entry1,entry2,entry3,entry4;
	entry1.all = readl((void*)0xC0010164);
	entry2.all = readl((void*)0xC001017C);
	entry3.all = readl((void*)0xC0010194);
	entry4.all = readl((void*)0xC00101ac);

	if(entry1.b.wptr != entry1.b.rptr ||
		entry2.b.wptr != entry2.b.rptr ||
		entry3.b.wptr != entry3.b.rptr ||
		entry4.b.wptr != entry4.b.rptr)
	{
		nvme_apl_trace(LOG_INFO, 0xcd3a, "data entry exist");
		return true;
	}else
		return false;
}
ddr_code bool check_cmd_slot(void)
{
	int i;
	for(i=0;i<=0x3c;i=i+4)
	{
		if(readl((void*)(0xc003c100+i)) != 0)
			return true;
	}
	return false;
}
ddr_code bool warm_boot_check_hw_qptr(void)
{

	if(check_cmd_qptr()||
		check_cmd_cqptr()||
		check_data_entry_qptr()||
		check_cmd_slot())
	{
		return true;
	}

	return false;
}
fast_code void nvmet_warm_boot_timer(void *data)
{
	bool flag1 = true;

	if(plp_trigger)
	{
		return;
	}


point:
	if (get_btn_running_cmds() ||
           ctrlr->cmd_proc_running_cmds ||
           (ctrlr->admin_running_cmds - ctrlr->aer_outstanding) ||
           warm_boot_check_hw_qptr()||
           gResetFlag & BIT(cNvmeControllerResetClr)||
           gResetFlag & BIT(cNvmeShutDown)){	//20201225-Eddie
		//mod_timer(&wb_timer, jiffies + 20);

        #if (CO_SUPPORT_READ_AHEAD == TRUE)
        ra_disable_time(20); // 2s// TODO DISCUSS
        #endif
		mod_timer(&wb_timer, jiffies + HZ/10);
		nvme_apl_trace(LOG_INFO, 0x0b1d, "warm boot wait io");
		return;
	}

	if(flag1 == true)
	{
		flag1 = false;
		goto point;
	}
	nvme_apl_trace(LOG_INFO, 0x04ae, "warm boot shutdown");

	misc_set_warm_boot_flag(&ctrlr->hmb_attr.hsize, sizeof(ctrlr->hmb_attr) / sizeof(u32));
	//cmd_proc_disable_function(0, (u32)nvmet_warm_boot_reset);
	#if(WA_FW_UPDATE == ENABLE)
	{
	OTF_WARM_BOOT_FLAG = true;
	}
	#endif
	_fg_warm_boot = true;
	nvme_apl_trace(LOG_INFO, 0x2737, "OTF_WARM_BOOT_FLAG %d", OTF_WARM_BOOT_FLAG);
	gResetFlag |= BIT(cWarmReset);
	evt_set_imt(evt_shutdown, (u32)nvmet_warm_boot_reset, BIT31);
	//nvmet_warm_boot_reset();
}

slow_code_ex void nvmet_err_insert_event_log(struct insert_nvme_error_information_entry err_info)
{
	/* Prepare Log Page */
	struct nvme_error_information_entry *err;
	//extern u64 sys_time;

	if(plp_trigger)
	{
		nvme_apl_trace(LOG_ALW, 0xf265, "PLP => Give up smart error log");
		return;
	}
	err = &ctrlr->elog[ctrlr->elog_cur];

	err->error_count = ++ctrlr->elog_tot;
	err->sqid = err_info.sqid;
	err->cid = err_info.cid;
	err->error_location = err_info.error_location;
	err->lba = err_info.lba;
	err->status = err_info.status;
	err->nsid = err_info.nsid;
	err->vendor_specific = 0;
	err->cmd_specific =  err_info.cmd_specific;

	if (ctrlr->elog_valid <= IDTFY_ELPE)
		ctrlr->elog_valid++;

	ctrlr->elog_cur = (ctrlr->elog_cur + 1) & IDTFY_ELPE;

	smart_inc_err_cnt();
	nvme_apl_trace(LOG_DEBUG, 0x0e7a, "smart_err_num(%d)", (u32) smart_stat->num_error_info_log_entries);//PC BASHER FULL RANDOM
	nvme_apl_trace(LOG_DEBUG, 0xd982, "error injection done");//PC BASHER FULL RANDOM
	//flush_to_nand(EVT_CMD_ERROR);//PC BASHER FULL RANDOM temp mark
}

/*!
 * @brief NVMe controller initialization
 *
 * Initialize controller settings and register event handler functions
 *
 * @return	None
 */
static init_code void nvmet_init_apl(void)
{
#ifdef FPGA
	if (!soc_cfg_reg2.b.nvm_en) {
		return;
	}
#endif

#if NCL_STRESS
	return;
#endif
	int i = 0;
	u32 sz = 0;
	struct nvmet_sq *sq;
#if defined(SRIOV_SUPPORT)
	/* This depends on PF0 or VF functions. Currently setting the value
	 * for PF0 function. When handling VF IO queues, FW should use
	 * appropriate value - SRIOV_FLEX_VF_IO_Q_PER_FUNC */
	ctrlr->max_qid = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL;
#else
#ifdef NVME_SHASTA_MODE_ENABLE
	ctrlr->max_qid = NVMET_NR_IO_QUEUE + 1;	/* plus Admin Queue */
#else
	ctrlr->max_qid = NVMET_RESOURCES_FLEXIBLE_TOTAL;	/* plus Admin Queue */
#endif
#endif

	memset((void*)&_nvme_ns_info[0], 0, sizeof(_nvme_ns_info));

	ctrlr->last_sqid = 1; /* Always from Submission Queue 0 if any */
	ctrlr->max_ns = NVMET_NR_NS;

	sz = sizeof(struct nvme_error_information_entry) * (IDTFY_ELPE + 1);
	ctrlr->elog = malloc(sz);
	if (ctrlr->elog == NULL)
		panic("elog allocation failes\n");

	/*
	 * The controller should clear this log page by removing all entries
	 * on power cycle and reset.
	 */
	//memset(ctrlr->elog, 0, sz);

	sz = sizeof(struct nvme_health_information_page);
	ctrlr->health = malloc(sz);
	if (ctrlr->health == NULL)
		panic("health allocation failes\n");
	/*
	 * The controller should clear this health page by removing all entries
	 * on power cycle and reset.
	 */
	//memset(ctrlr->health, 0, sz);

	//smart_stat_init();

	ctrlr->regs = hal_nvmet_ctrlr_registers();
	//memset(ctrlr->regs, 0, sizeof(struct nvme_registers));	/* clear all */

	/* registers shall be initialized prior to Software access or CC.EN */
	ctrlr->regs->cap.bits.ams = NVME_CAP_AMS_WRR;

	/* up to 1.3 version */
	ctrlr->regs->vs.bits.mjr = NVME_VS_MAJOR;
	ctrlr->regs->vs.bits.mnr = NVME_VS_MINOR;

	for (i = 0; i < NVMET_NR_NS; i++) {
		_nsid[i].type = NSID_TYPE_UNALLOCATED;
		_nsid[i].nsid = i + 1;
		_nsid[i].wp_state = NS_NO_WP;
		_nsid[i].ns = &_ns[i];
		memset(&_ns[i], 0, sizeof(struct nvmet_namespace));

		_nvme_ns_info[i].hw_attr.nsid = i + 1;

	#ifdef NS_MANAGE// joe add define 20200916
	     #if defined(PI_SUPPORT)//joe add NS 20200813
		_nvme_ns_info[i].fw_attr.support_lbaf_cnt = 4;
		_nvme_ns_info[i].fw_attr.support_pit_cnt = 3;
             #else
		_nvme_ns_info[i].fw_attr.support_lbaf_cnt = 2;//joe test 20200817 1-->2 //joe 2---->4 20200911
		_nvme_ns_info[i].fw_attr.support_pit_cnt = 0;
             #endif
	#endif
#ifdef NS_MANAGE
		extern bool req_exec(req_t *req);
		_nsid[i].ns->issue = req_exec;
		set_ns_slba_elba(i + 1, 0, 0);
#endif
	}
	ctrlr->nsid = _nsid;

#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
	/* structure used to support sriov functionality */
	ctrlr->sr_iov = &_sr_iov;
	memset(ctrlr->sr_iov, 0, sizeof(struct nvmet_sr_iov));	/* clear all */

	ctrlr->sr_iov->vq_flex_cur_idx = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL;
	ctrlr->sr_iov->vi_flex_cur_idx = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL;
	ctrlr->sr_iov->pri_vq_count = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL;
	ctrlr->sr_iov->pri_vi_count = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL;

	/* Initialize the vq_flex_strt_idx[] */
	for (i = 0; i < SRIOV_VF_PER_CTRLR; i++) {
		ctrlr->sr_iov->vq_flex_strt_idx[i] = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL +
			(i * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC);
	}
#else
	ctrlr->sr_iov = NULL;
#endif
	ctrlr->admin_running_cmds = 0;
	ctrlr->cmd_proc_running_cmds = 0;
	ctrlr->currAbortCnt = 0;
	ctrlr->nvme_req_flush = 0;
#ifdef NS_MANAGE
	ctrlr->ns_smart = false;//true;
#else
	ctrlr->ns_smart = false;
#endif
	//TODO : check default value
	memset(&ctrlr->def_feat, 0, sizeof(ctrlr->def_feat));
	ctrlr->def_feat.Tag = 0x53415645; /*Tag "SAVE"*/
	ctrlr->def_feat.arb_feat.b.ab = 3;   	/* default AB is 8*/
	ctrlr->def_feat.vwc_feat.b.wce = 1;	/* default is enable */
#if defined(SRIOV_SUPPORT)
	u8 fnid = req->fe.nvme.cntlid;
	u32 max_q_num = (fnid == 0)? NVMET_NR_IO_QUEUE: SRIOV_FLEX_VF_IO_Q_PER_FUNC;
#elif defined(NVME_SHASTA_MODE_ENABLE)
	u32 max_q_num = NVMET_NR_IO_QUEUE;
#else
	u32 max_q_num = NVMET_RESOURCES_FLEXIBLE_TOTAL - 1;
#endif
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
	ctrlr->def_feat.en_plp_feat.b.opie = 0; //default disable
#endif
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
		ctrlr->def_feat.en_pwrdis_feat.all = 0; //default disable
#endif
	ctrlr->def_feat.nque_feat.b.nsqa = max_q_num - 1;//joe edevx spec nsqa is 0x41, so don't -1 20201216
	ctrlr->def_feat.nque_feat.b.ncqa = max_q_num - 1;//joe edevx spec ncqa is 0x41, so don't -1 20201216

	/* tmt init */
	ctrlr->def_feat.temp_feat.tmpth[0][OVER_TH] = c_deg_to_k_deg(TS_DEFAULT_WARNING);	// should be the value in identify
	ctrlr->def_feat.temp_feat.tmpth[0][UNDER_TH] = c_deg_to_k_deg(TS_DEFAULT_UNDER);	// should be the value in identify
	/* three sensort now */
	for (i = 1; i < 4; i++) {
		ctrlr->def_feat.temp_feat.tmpth[i][OVER_TH] = 0xffff;	// spec defined default value
		ctrlr->def_feat.temp_feat.tmpth[i][UNDER_TH] = 0;	// spec defined default value
	}
	for (i = 4; i < 9; i++) {
		ctrlr->def_feat.temp_feat.tmpth[i][OVER_TH] = 0;
		ctrlr->def_feat.temp_feat.tmpth[i][UNDER_TH] = 0;
	}

	ctrlr->def_feat.hctm_feat.all = (c_deg_to_k_deg(TS_DEFAULT_TMT1) << 16) | (c_deg_to_k_deg(TS_DEFAULT_TMT2));
	ctrlr->def_feat.warn_cri_feat.tmt_warning = c_deg_to_k_deg(TS_DEFAULT_WARNING);
	ctrlr->def_feat.warn_cri_feat.tmt_critical = c_deg_to_k_deg(TS_DEFAULT_CRITICAL);

	ctrlr->def_feat.aec_feat.b.smart = 0x3F;

	srb_t *srb = (srb_t *) SRAM_BASE;
	extern AGING_TEST_MAP_t *MPIN;
	void * SN_num = (void *)MPIN->drive_serial_number;
	if(srb->cap_idx == 3) //8T
	{
		memcpy(ctrlr->def_feat.device_feat.mn, "SSSTC LJ1-2W7680", strlen(ctrlr->def_feat.device_feat.mn));
	}
	else if(srb->cap_idx == 2) //4T
	{
		memcpy(ctrlr->def_feat.device_feat.mn, "SSSTC LJ1-2W3840", strlen(ctrlr->def_feat.device_feat.mn));
	}
	else
	{
		memcpy(ctrlr->def_feat.device_feat.mn, "SSSTC LJ1-2Wxxxx", strlen(ctrlr->def_feat.device_feat.mn));
	}
	memcpy(ctrlr->def_feat.device_feat.fr, FR, strlen(ctrlr->def_feat.device_feat.fr));
	memcpy(ctrlr->def_feat.device_feat.sn, (s8*)SN_num, strlen(ctrlr->def_feat.device_feat.sn));

#if defined(PROGRAMMER)
	ctrlr->cur_feat = ctrlr->def_feat;
	ctrlr->cur_afi.slot_next = 0;
	ctrlr->cur_afi.slot = 1;
	ctrlr->max_fw_slot = 7;
#elif defined(RAMDISK)
	/* only for IOL test GRP 30:0.1.0 */
	ctrlr->max_fw_slot = 1;
#else
	fwb_t *fwb = (fwb_t *) (SRB_HD_ADDR + sizeof(srb_t) - 512);
	ctrlr->cur_afi.slot_next = 0;
	ctrlr->cur_afi.slot = fwb->active_slot;
	ctrlr->max_fw_slot = MAX_FWSLOT;
	for (i = 0; i < MAX_FWSLOT; i++)
		ctrlr->fw_slot_version[i] = fwb->fw_slot[i].fw_slot_version;
#endif

	/* PF0 Admin Queue */
	nvmet_set_cq(0);
	nvmet_set_sq(0);
	nvmet_bind_cq(0, 0);
	sq = ctrlr->sqs[0];
	sq->qprio = 0;
	sq->head = sq->tail = ~0;
	INIT_LIST_HEAD(&sq->reqs);

#if defined(SRIOV_SUPPORT)
	/* FW doesn't have enough resources to support 32 VFs. So limit the number
	 * of VFs to small (1, 2 or 4). For the supported VF Admin Queues */
	for (i = 1; i <= SRIOV_VF_PER_CTRLR; i++) {
		nvmet_set_vfcq(i, 0);
		nvmet_set_vfsq(i, 0);
		nvmet_bind_vfcq(i, 0, 0);

		sq = ctrlr->sr_iov->fqr_sq_cq[i-1].fqrsq[0];
		sq->qprio = 0;
		sq->head = sq->tail = ~0;
		INIT_LIST_HEAD(&sq->reqs);
	}
#endif

	for (i = 0; i < NVME_ACTIVE_COMMAND_COUNT; i++) {
		req_t *req = &_reqs[i];

		INIT_LIST_HEAD(&req->entry);
		req->req_id = i;
	}

	pool_init(&reqs_pool, (void *)_reqs,
		  sizeof(req_t) * NVME_ACTIVE_COMMAND_COUNT, sizeof(req_t), NVME_ACTIVE_COMMAND_COUNT);

	nvmet_admin_cmd_init();
	nvmet_io_cmd_init();

	evt_register(nvmet_evt_cmd_done, 0, &evt_cmd_done);
	evt_register(nvmet_evt_shutdown, 0, &evt_shutdown);
	evt_register(nvmet_evt_fe_gc_resume, 0, &evt_fe_gc_resume);

#if (TRIM_SUPPORT == ENABLE) && (_TCG_)
	evt_register(nvmet_evt_tcg_trim_chk, 0, &evt_tcg_trim_chk);
#endif

	urg_evt_register(reset_task_handler, 0, evt_perst_hook);
	#if CO_SUPPORT_PANIC_DEGRADED_MODE
	urg_evt_register(assert_reset_task_handler, 0, evt_assert_rst);
	#endif

#if !defined(FPGA) && CPU_ID == 1
	urg_evt_register(trigger_vol_mon_task, 0, evt_vol_mon_hook);
#endif

	INIT_LIST_HEAD(&ctrlr->aborted_reqs);

	sq_timer.function = nvmet_sq_timer_chk;
	sq_timer.data = "sq_timer";
	nvmet_sq_timer_enable();

	INIT_LIST_HEAD(&wb_timer.entry);
	wb_timer.function = nvmet_warm_boot_timer;
	wb_timer.data = "warm_boot_timer";
#if (PI_FW_UPDATE == mENABLE)
	INIT_LIST_HEAD(&pi_fwdl_timer.entry);
	pi_fwdl_timer.function = nvmet_pi_suspend_timer;
	pi_fwdl_timer.data = "pi_suspend_fwdl_timer";
#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
	INIT_LIST_HEAD(&pi_fwdl_timer.entry);
	pi_fwdl_timer.function = nvmet_pi_suspend_fwdl_timer;
	pi_fwdl_timer.data = "pi_suspend_fwdl_timer";

	INIT_LIST_HEAD(&pi_fwca_timer.entry);
	pi_fwca_timer.function = nvmet_pi_suspend_fwca_timer;
	pi_fwca_timer.data = "pi_suspend_fwca_timer";
#endif
#endif
#if UART_STARTWB
	if (misc_is_startwb()){
		INIT_LIST_HEAD(&startwb_timer.entry);
		startwb_timer.function = start_warm_boot_timer;
		startwb_timer.data = "start_warm_boot_timer";
	}
#endif
	shutdown_cb = NULL;
}

/*! \cond PRIVATE */
module_init(nvmet_init_apl, NVME_APL);
/*! \endcond */
#if UART_STARTWB	//20210517-Eddie
slow_code_ex void start_warm_boot_main(void)
{
	if (misc_is_startwb()){
			if(misc_get_startwb()){
				u64 timer = 0;
				evlog_printk(LOG_ALW,"startwb_timer delay %d sec", (u32)misc_get_startwb());
				timer |= (u64)misc_get_startwb();
				mod_timer(&startwb_timer, jiffies + timer);	//delay sec
			}
			else{
				evlog_printk(LOG_ALW,"startwb_timer default 30 sec");
				mod_timer(&startwb_timer, jiffies + 300);	//30sec
			}
		}
}
ucmd_code int start_warm_boot_uart(int argc, char *argv[])		//20210517-Eddie
{
	u32 type, delay;
	type = ( u32) strtol(argv[1], (void *)0, 0);
	delay = ( u32) strtol(argv[2], (void *)0, 0);

	if (type == 0){
		del_timer(&startwb_timer);	//10sec
		misc_clear_startwb();
	}
	else if (type == 1)	{
		INIT_LIST_HEAD(&startwb_timer.entry);
		startwb_timer.function = start_warm_boot_timer;
		startwb_timer.data = "start_warm_boot_timer";

		if (argc >= 3){
			misc_set_startwb_flag(delay);
			mod_timer(&startwb_timer, jiffies + delay);
		}
		else{
			misc_set_startwb_flag(0);
			mod_timer(&startwb_timer, jiffies + 100);	//10sec
		}
	}

	return 0;
}
static DEFINE_UART_CMD(startwb, "startwb", "startwb", "startwb", 1, 2, start_warm_boot_uart);
#endif

#if CO_SUPPORT_PANIC_DEGRADED_MODE
ddr_code static void assert_reset_task_handler(u32 p0, u32 _cb, u32 reason /* PCIE_RST_XXX */)
{
	assert_reset_del_sq_resource();
	rdisk_assert_reset_disk_handler();

	MdlHost_Perst_Handle(reason, _cb);

	cmd_proc_non_read_write_mode_setting(true);
}
#endif
/*! @} */
