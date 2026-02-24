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
 * @brief Admin Queue operations
 *
 * \addtogroup decoder
 * \defgroup admin_cmd Admin command
 * \ingroup decoder
 * @{
 * Define admin command handlers
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvmet.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
#include "bf_mgr.h"
#include "assert.h"
#include "event.h"
#include "cmd_proc.h"
#include "hmb.h"
#include "hmb_cmd.h"
#include "security_cmd.h"
#include "misc.h"
#include "smart.h"
#include "cpu_msg.h"
#include "nvme_apl.h"
#include "ns.h"
#include "nvme_decoder.h"
#include "nvme_spec.h"			  //joe add  NS 20200813
#include "btn_cmd_data_reg.h"	  //joe add 20200817
#include "../../../ncl_20/eccu.h" //joe add 20200817
#include "srb.h"				  //joe add epm 20200901
#include "spin_lock.h"
#include "btn_export.h"
#include "trim.h"
#include "plp.h"
#include "pcie_wrapper_register.h"
#include "../../../ncl_20/GList.h"
#include "vic_register.h"
#include "console.h"
#include "mpc.h"
#include "ipc_api.h"
#include "fw_download.h"
#include "ftl_export.h"
#include "../../../ncl_20/inc/eccu_reg_access.h"
#include "../../../ncl_20/inc/ndcu_reg_access.h"

#if GC_SUSPEND_FWDL		//20210308-Eddie
#include "fc_export.h"
#include "../../../ftl/ftl.h"
#endif
#if !defined(PROGRAMMER)
#include "ssstc_cmd.h"
#endif
#include "ddr.h"
#include "srb.h"

#ifdef OC_SSD
#include "ocssd.h"
#endif

#if (CO_SUPPORT_READ_AHEAD == TRUE)
#include "ra.h"
#endif

#if CO_SUPPORT_SECURITY
#include "nvme_security.h"
#endif

#if (_TCG_) // Jack Li

#include "tcgcommon.h"
#include "tcgnvme.h"
#include "tcg.h"
#include "tcg_if_vars.h"
#include "tcg_sh_vars.h"
//#include "nvme_security.h"

#ifdef TCG_NAND_BACKUP
#include "tcg_nf_mid.h"
extern u8 isTcgNfBusy;
#endif

#endif

/*! \cond PRIVATE */
#define __FILEID__ admin
#include "trace.h"
/*! \endcond */
#if (PI_FW_UPDATE == mENABLE)
share_data_zi volatile bool _fg_pi_sus_io = false;
share_data_zi volatile bool _fg_pi_resum_io = false;
#endif
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
/*!
 * @brief asynchronous event request response
 */
typedef union
{
	u32 all;
	struct
	{
		u32 async_event_type : 3; /*! enum nvme_asynchronous_event_type */
		u32 rvsd1 : 5;
		u32 async_event_information : 8;
		u32 log_page_identifier : 8;
		u32 rvsd2 : 8;
	} b;
} aer_rsp_t;

#define EC_10K          10000
#define PLP_internal    (CPU_CLK/1000)*1000  //1000ms

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
fast_data u8 evt_fw_dwnld = 0xFF;
fast_data u8 evt_fw_commit = 0xFF;
fast_data bool dwnld_com_lock = false;
fast_data u8 evt_set_fwcfg = 0xFF; // 3.1.7.4 Not ready yet
//Andy test IOL 5.4 20201013
volatile fast_data u8 flagtestS = 0;
volatile fast_data u8 flagtestC = 0;
extern u32 _max_capacity;

#ifdef POWER_APST
fast_data u8 evt_change_ps = 0xFF;
static fast_data struct timer_list apst_timer;
static fast_data u32 apst_period;
#endif
share_data_ni volatile struct timer_list AER_timer;

fast_data_zi struct timer_list sq_del_timer;			///< controller sq delet
fast_data LIST_HEAD(nvme_sq_del_reqs);				/// nvme sq dele

// for DM8 //3.1.7.4 merged 20201201 Eddie
share_data_zi volatile fw_img_st_t fw_image_status = FW_IMG_NRM;
share_data_zi volatile u8 fw_prplist_trx_done = 0;
#if GC_SUSPEND_FWDL
share_data_zi volatile u8 fwdl_gcsuspend_done = 0;
#endif
ddr_sh_data bool _fg_prp_list = false;
ddr_sh_data bool  gFormatFlag = false;
ddr_sh_data volatile bool format_gc_suspend_done = 0;
ddr_sh_data bool create_ns_preformat_handle = 0;
share_data_zi volatile commit_ca3 *commit_ca3_fe;
share_data_zi volatile is_IOQ *is_IOQ_ever_create_or_not;
#define FW_DU_BMP_CNT 16
fast_data_zi u32 fw_du_bmp[FW_DU_BMP_CNT];
share_data_zi u32 oldoffset = 0;
share_data_zi bool offset_backward = 0;
fast_data_zi u8 pre_ers = 0;
fast_data_zi u8 pre_critical = 0;
share_data_zi volatile u8 fw_update_flag;	

//fast_data struct ex_health_ipc *g_ex_hlt;
//fast_data struct ex_health_ipc *g_last_ex_hlt;

ddr_data void *secure_com_buf = NULL;
slow_data_zi u8 sec_send_prp_list;


sram_sh_data struct nvme_health_information_page g_health_for_update;						///< smart context
sram_sh_data struct nvme_health_information_page *health_for_update = &g_health_for_update; ///< smart context pointer
extern enum du_fmt_t host_du_fmt;
static sram_sh_data struct nvme_additional_health_information_page g_additional_health_for_update;							 ///< additional smart context
sram_sh_data struct nvme_additional_health_information_page *additional_health_for_update = &g_additional_health_for_update; ///< additional smart context pointer
share_data_zi download_fw_t upgrade_fw;		///< fw upgrade structure
share_data_zi bool download_fw_reset;

fast_data static LIST_HEAD(_admin_req_pending);						  ///<admin pending request
fast_data u8 evt_admin_cmd_check = 0xff;
fast_data u8 evt_admin_format_confirm = 0xff;
#if CO_SUPPORT_SANITIZE // Jack Li, for Sanitize
fast_data u8 evt_admin_sanitize_operation = 0xff;
fast_data_zi u8 sntz_chk_cmd_proc;
#endif
#if (HOST_THERMAL_MANAGE == FEATURE_SUPPORTED)
share_data_zi bool set_hctm_flag = 0;
#endif

share_data_zi volatile bool esr_err_fua_flag;
//fast_data_ni volatile bool VWC_flag = false;


#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
	slow_data u32 pi_req1;
	slow_data u32 pi_req2;
#endif
ddr_data_ni void *telemetry_idtfy;

extern volatile u8 plp_trigger;
extern bool shr_trim_vac_save_done;
extern bool shr_format_copy_vac_flag;
extern bool shr_ftl_save_qbt_flag;
extern u8 shr_total_die;
extern u32 shr_program_fail_count;
extern u32 shr_erase_fail_count;
extern u32 shr_avg_erase;
extern u32 shr_max_erase;
extern u32 shr_min_erase;
extern u32 shr_total_ec;
extern u64 shr_nand_bytes_written;
extern u64 shr_unit_byte_nw;
extern volatile u32 GrowPhyDefectCnt; //for SMART growDef Use
extern u32 shr_die_fail_count;
#if RAID_SUPPORT_UECC
extern u32 nand_ecc_detection_cnt;
extern u32 host_uecc_detection_cnt;
extern u32 internal_uecc_detection_cnt;
extern u32 uncorrectable_sector_count;
extern u32 internal_rc_fail_cnt;
extern u32 host_prog_fail_cnt;
#endif
extern u16 shr_E2E_RefTag_detection_count;	// DBG, SMARTVry
extern u16 shr_E2E_AppTag_detection_count;
extern u16 shr_E2E_GuardTag_detection_count;
extern u16 host_sec_size; //joe add change sec size 20200817
extern u8 host_sec_bitz;  //joe add change sec size  20200817
extern u8 full_1ns;
extern bool esr_status;
extern esr_err_flags_t esr_err_flags;
extern read_only_t read_only_flags;
extern u8  cur_ro_status;

extern corr_err_cnt_t corr_err_cnt;

extern volatile bool PLN_FORMAT_SANITIZE_FLAG;
extern volatile bool PLN_GPIOISR_FORMAT_SANITIZE_FLAG;
extern volatile bool PLN_open_flag;
extern volatile bool PWRDIS_open_flag;
extern u8 evt_pln_format_sanitize;
extern volatile u8 shr_lock_power_on;

extern u32 lane_leq[4]; //LEQ
extern s32 lane_dfe[4]; //DFE
extern u16 shr_pll_kr; //PLL_BAND
extern u32 lane_cdr[4]; //CDR
extern s8 lane_rc[4]; //DAC

extern u16 req_cnt;
#ifdef ERRHANDLE_ECCT
extern u16 ecct_cnt;  //ecct cnt in sram  //tony 20201228
#define NS_SIZE_GRANULARITY_ADMIN1 (0x200000000 >> LBA_SIZE_SHIFT1) //joe 20200525
#define NS_SIZE_GRANULARITY_ADMIN2 (0x200000000 >> LBA_SIZE_SHIFT2) //joe 20200525
extern stECCT_ipc_t rc_ecct_info[MAX_RC_REG_ECCT_CNT];
extern u8 rc_ecct_cnt;
#endif
#if (Synology_case)
extern u32 host_unc_err_cnt;
#endif

share_data_zi u64 old_time_stamp;

extern __attribute__((weak)) void get_program_and_erase_fail_count();
extern __attribute__((weak)) void get_avg_erase_cnt(u32 *avg_erase, u32 *max_erase, u32 *min_erase, u32 *total_ec);
extern __attribute__((weak)) void get_nand_byte_written();
extern void ucache_flush(ftl_flush_data_t *fctx);

#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie
	ddr_sh_data u8 cmf_idx = 1;	//default : MR+512_0 mode
#endif
//extern vu32 Rdcmd;
#if defined(USE_CRYPTO_HW)
///Andy use for crypto
extern void crypto_change_mode_range(u8 crypto_type, u8 NS_ID, u8 change_key, u8 cryptoID);
#endif

#if CO_SUPPORT_SANITIZE
// Jack Li, for Sanitize
#include "ssstc_cmd.h"
#include "epm.h"

extern epm_info_t *shr_epm_info;
fast_data_zi epm_smart_t *epm_smart_data;

enum cmd_rslt_t sanitize_admin_check(req_t *req, struct nvme_cmd *cmd, bool sanitize_allow);
enum cmd_rslt_t nvmet_sanitize(req_t *req, struct nvme_cmd *cmd);
#endif

//for vu test
#if !defined(PROGRAMMER)
share_data NormalTest_Map *NormalTest;
#endif

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
sram_sh_data struct nvme_telemetry_host_initated_log *telemetry_host_data = NULL;
sram_sh_data struct nvme_telemetry_ctrlr_initated_log *telemetry_ctrlr_data = NULL;
static void *telemetry_dataHandle = NULL;
static fast_data u32 telemetry_transfer_prp_count = 0;
static fast_data u32 telemetry_transfer_prp_offset = 0;
static fast_data req_t *telemetry_transfer_req = NULL;
static fast_data u8 evt_telemetry_req_check = 0xff;
static fast_data u8 evt_telemetry_xfer_continue = 0xff;
share_data_zi bool telemetry_08h_trigger_flag = 0;
fast_data static LIST_HEAD(telemetry_req_pending);
static fast_data bool telemetry_transferring = false;
//finiteStationMachine
enum telemetry_task {
	TELEMETRY_TASK_REQUIRE_DATA,
	TELEMETRY_TASK_RECEIVE_DATA,
};
typedef struct _telemetry_finiteStatusMachine_setting
{
	u8 lid;
	bool rae;
	bool telemetry_update;
	enum telemetry_task telemetry_task_status;
}telemetry_finiteStatusMachine_setting;
telemetry_finiteStatusMachine_setting telemetry_finiteStatusMachine;

static fast_data u8 telemetry_require_task_size = 0;
static fast_data u8 telemetry_pending_task = 0;
extern epm_info_t *shr_epm_info;
epm_smart_t *epm_telemetry_data = NULL;
#endif
#if 0
/*!
 * @brief array of strings mapping to opcode
 */
static const char *const opcode_names[] = {
	/* 00-01 */ "NVME_OPC_DELETE_IO_SQ", "NVME_OPC_CREATE_IO_SQ",
	/* 02-03 */ "NVME_OPC_GET_LOG_PAGE", NULL,
	/* 04-05 */ "NVME_OPC_DELETE_IO_CQ", "NVME_OPC_CREATE_IO_CQ",
	/* 06-07 */ "NVME_OPC_IDENTIFY", NULL,
	/* 08-09 */ "NVME_OPC_ABORT", "NVME_OPC_SET_FEATURES",
	/* 0a-0b */ "NVME_OPC_GET_FEATURES", NULL,
	/* 0c-0d */ "NVME_OPC_ASYNC_EVENT_REQUEST", "NVME_OPC_NS_MANAGEMENT",
	/* 0e-0f */ NULL, NULL,
	/* 10-11 */ "NVME_OPC_FIRMWARE_COMMIT",
	"NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD",
	/* 12-13 */ NULL, NULL,
	/* 14-15 */ NULL, "NVME_OPC_NS_ATTACHMENT"};

/*!
 * @brief array of strings mapping to opcode
 */
static const char *const opcode_names_80[] = {
	/* 80-81*/ "NVME_OPC_FORMAT_NVM", "NVME_OPC_SECURITY_SEND",
	/* 82 */ "NVME_OPC_SECURITY_RECEIVE"};
#endif
#if defined(SRIOV_SUPPORT)
static const char *const subnqn_vf[] = {
	"nqn.2016-11.com.innogrit:nvme.rainier_vf01",
	"nqn.2016-11.com.innogrit:nvme.rainier_vf02",
	"nqn.2016-11.com.innogrit:nvme.rainier_vf03",
	"nqn.2016-11.com.innogrit:nvme.rainier_vf04",
	"nqn.2016-11.com.innogrit:nvme.rainier_vf05",
	"nqn.2016-11.com.innogrit:nvme.rainier_vf06",
	"nqn.2016-11.com.innogrit:nvme.rainier_vf07",
	"nqn.2016-11.com.innogrit:nvme.rainier_vf08"};
#endif

#define plp_exist
#if VALIDATE_BOOT_PARTITION

#define BOOT_PARTITION_SIZE 0x40000
#define BP_DTAG_CNT (BOOT_PARTITION_SIZE / DTAG_SZE)
dtag_t *bp_dtags;

u32 bp_get_cnt = 0;
void validate_bp_dtag_get(u32 bp_dtag_cnt)
{
	bp_dtags = (dtag_t *)sys_malloc(SLOW_DATA, sizeof(dtag_t) * bp_dtag_cnt);
	sys_assert(bp_dtags != NULL);
	u32 i;
	for (i = 0; i < bp_dtag_cnt; i++)
	{
		bp_dtags[i] = dtag_get(NULL);
		sys_assert(bp_dtags[i].b.dtag != _inv_dtag.b.dtag);
	}

	return;
}

void validate_bp_mem_copy(void *req_mem, u32 cnt)
{
	u8 *dst, *src;
	u32 i;
	bp_get_cnt = cnt;
	dtag_t *dtag = req_mem;

	for (i = 0; i < bp_get_cnt; i++)
	{
		dst = dtag2mem(bp_dtags[i].b.dtag);
		src = dtag2mem(dtag[i].b.dtag);
		memcpy(dst, src, DTAG_SZE);
	}
	nvme_apl_trace(LOG_ERR, 0x95e1, "Boot Partition copy done\n");
	return;
}
#endif //VALIDATE_BOOT_PARTITION

ddr_code bool nvmet_abort_aer_flow(u16 cid)
{
	struct list_head *cur;
	struct list_head *saved;
	req_t *aer_req;

	list_for_each_safe(cur, saved, &ctrlr->aer_outstanding_reqs)
	{
		aer_req = container_of(cur, req_t, entry);
		u32 cmd_cid = aer_req->fe.nvme.cid;

		nvme_apl_trace(LOG_INFO, 0x4637, "aer->cid %x",
					   cmd_cid);
		if (cmd_cid == cid)
		{
			nvmet_set_status(&aer_req->fe, NVME_SCT_GENERIC, NVME_SC_ABORTED_BY_REQUEST);
			list_del(&aer_req->entry);
			aer_req->completion(aer_req);
			ctrlr->aer_outstanding--;
			return true;
		}
	}

	return false;
}

/*!
 * @brief dma error release source
 *
 * @param req		req_t *
 *
 * @return		not used
 */
ddr_code void nvme_xfer_err_rls_source(req_t *req) {
	nvme_apl_trace(LOG_ERR, 0x69c9,"DMA fail req:%x", req);
	if (req->req_prp.mem) {
		dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, (dtag_t *)req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.mem);
	}
	if (req->req_prp.prp) {
		sys_free(SLOW_DATA, req->req_prp.prp);
	}
	if (req->req_prp.prp_list) {
		sys_free(SLOW_DATA, req->req_prp.prp_list);
	}
	nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);
	evt_set_imt(evt_cmd_done, (u32)req, 0);
}

/*!
 * @brief admin Command public DMA transfer Done(IN & OUT)
 *
 * @param payload	should be request pointer
 * @param error		true for dma transfer error
 *
 * @return		not used
 */
static fast_code void nvmet_admin_public_xfer_done(void *payload, bool error)
{
	req_t *req = (req_t *)payload;

	//sys_assert(error == false);
	if (error == true) {
		nvme_xfer_err_rls_source(req);
		return;
	}
	sys_assert(req->req_prp.fetch_prp_list == false);
	/* Exactly 4K for misc data (ctrlr or others) of identify */
	if (++req->req_prp.transferred == req->req_prp.required)
	{
		dtag_t *dtag = (dtag_t *)req->req_prp.mem;

		dtag_put(DTAG_T_SRAM, dtag[0]);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}

#if 1 //3.1.7.4 merged 20201201 Eddie
static slow_code_ex void nvmet_admin_fw_download_xfer_done(void *payload, bool error);
static slow_code_ex bool nvmet_core_downlaod_cmd_done(req_t *req);
/*!
 * @brief admin Command firmware do image download DMA transfer PRP data
 *
 * @param req	should be request pointer
 *
 * @return		not used
 */
static fast_code void nvmet_admin_fw_download_xfer_prp(req_t *req)
{
	int xfer, ofst = 0, cur = 0, sz = 0;
	dtag_t *dtag;
	void *mem = NULL;
	u32 i, dtag_cnt;

	//printk("nvmet_admin_fw_download_xfer_prp \n");

	/* trigger data transfer */
	dtag = req->req_prp.mem;
	dtag_cnt = req->req_prp.mem_sz;

	if (ctrlr->page_size > DTAG_SZE)
		i = ctrlr->page_size / DTAG_SZE;
	else
		i = 1;

	i = req->req_prp.required / i;
	for (; i < req->req_prp.nprp; i++)
	{
		prp_entry_t *prp = &req->req_prp.prp[i];
		int prp_ofst = 0;
		int len = prp->size;
	again:
		if (sz == 0)
		{
			mem = dtag2mem(dtag[cur++]);
			if (cur > dtag_cnt)
				break;
			ofst = 0;
			sz = DTAG_SZE;
		}

		xfer = min(len, sz);

		req->req_prp.required++;
		hal_nvmet_data_xfer(prp->prp + prp_ofst,
							ptr_inc(mem, ofst),
							xfer, READ, (void *)req, nvmet_admin_fw_download_xfer_done);

		ofst += xfer;
		prp_ofst += xfer;
		sz -= xfer;
		len -= xfer;

		if (len != 0)
		{
			//printk("len != 0 \n");
			sys_assert(sz == 0);
			goto again;
		}
	}
}

static slow_code_ex void nvmet_prplist_launch_one_dtag_xfer(req_t *req,void *mem)
{		//20201222-Eddie-from CA6
	int xfer, ofst = 0, sz = DTAG_SZE;

	while(true)
    	{
	        prp_entry_t *prp = &req->req_prp.prp[req->req_prp.sec_xfer_idx];
	        u32 len = (prp->size - req->req_prp.prp_offse);

		  if (prp->size == 0)
		  {
		     //evlog_printk(LOG_ALW,"Empty PRP");
		     break;
	        }

	        xfer = min(len, sz);

		 //evlog_printk(LOG_ALW,"id[%d] xfer[%x] prp1 size[%x] prp[%x] [%x] ",req->req_prp.sec_xfer_idx,xfer,prp->size,((u32)(prp->prp>>32)),((u32)(prp->prp + req->req_prp.prp_offse)));
		// evlog_printk(LOG_ALW,"len[%x] prp_offse[%x] ",len,req->req_prp.prp_offse);
	        req->req_prp.sec_xfer_idx++;
	        req->req_prp.required++;
	        req->req_prp.sec_xfered_cnt += xfer;

		 sz -= xfer;
		 if ((sz == 0) ||(req->req_prp.sec_xfer_idx >= req->req_prp.nprp))
		 	hal_nvmet_data_xfer(prp->prp + req->req_prp.prp_offse, ptr_inc(mem, ofst), xfer, READ, (void *) req, nvmet_admin_fw_download_xfer_done);
		 else
	        	hal_nvmet_data_xfer(prp->prp + req->req_prp.prp_offse, ptr_inc(mem, ofst), xfer, READ, (void *) req, NULL);

	        ofst += xfer;
	        req->req_prp.prp_offse += xfer;
	        len -= xfer;

	        if (len == 0)
	        {
	            req->req_prp.prp_offse = 0;
		     //evlog_printk(LOG_ALW,"SET OFT to 0");
	        }

	        if (sz == 0) // assign 4k done
	        {
	            if (len)
	            {
	                //evlog_printk(LOG_ALW,"prp_entry separate \n");
	                req->req_prp.sec_xfer_idx--;
	            }
	            break;
	        }

	        if (req->req_prp.sec_xfer_idx >= req->req_prp.nprp)
	        {
	        	//evlog_printk(LOG_ALW,"sec_xfer_idx exceed!! \n");
	            break;
	        }
    	}

}


//after prp list xfer done , take out prp entry
static slow_code_ex void nvmet_map_admin_prp_entry_takeout(req_t *req)		//20201222-Eddie-from CA6
{
    u32 len = req->req_prp.size;
    prp_t *prp = req->req_prp.prp_list;
    req_prp_t *rprp = &req->req_prp;
    prp_entry_t *prp_entry = &rprp->prp[rprp->nprp];

    //evlog_printk(LOG_ALW,"prp_list TRUE len %d",req->req_prp.size);

    req->req_prp.fetch_prp_list = false;
    req->req_prp.transferred = 0;
    req->req_prp.required = 0;
    req->req_prp.sec_xfered_cnt = 0;
    req->req_prp.sec_xfer_idx = 0;
    req->req_prp.prp_offse = 0;
    req->req_prp.sec_got_dtag_cnts = 1;	//xfer in dtag counts one by one

    while (len != 0) {
        u32 trans_len = min(len, ctrlr->page_size);

        prp_entry->prp = *prp;
        prp_entry->size = trans_len;

	  //evlog_printk(LOG_ALW,"prp_list TRUE nprp %d PRP_Entry 0x[%x][%x] size[%x]",req->req_prp.nprp,((u32)(prp_entry->prp>>32)),(u32)prp_entry->prp,prp_entry->size);

        prp_entry++;
        prp++;
        req->req_prp.nprp++;
	 //evlog_printk(LOG_ALW,"prp_list TRUE nprp %d PRP_Entry 0x[%x][%x] size[%x]",req->req_prp.nprp,((u32)(prp_entry->prp>>32)),(u32)prp_entry->prp,prp_entry->size);
        len -= trans_len;
	 if (len == 0){
	 	prp_entry->prp = *prp;
        	prp_entry->size = len;
		//evlog_printk(LOG_ALW,"Last prp_list TRUE nprp %d PRP_Entry 0x[%x][%x] size[%x]",req->req_prp.nprp,((u32)(prp_entry->prp>>32)),(u32)prp_entry->prp,prp_entry->size);
	 }
    }

	//sys_free(SLOW_DATA, req->req_prp.prp_list);
}

/*!
 * @brief admin Command firmware image download DMA transfer Done(IN & OUT)
 *
 * @param payload	should be request pointer
 * @param error		true for dma transfer error
 *
 * @return		not used
 */
static slow_code_ex void nvmet_admin_fw_download_xfer_done(void *payload, bool error)
{
	req_t *req = (req_t *)payload;
	struct nvme_cmd *nvme_cmd = req->host_cmd;
	dtag_t *dtag = NULL;

	sys_assert(error == false);

	if (req->req_prp.fetch_prp_list == true)
	{
		fw_prplist_trx_done = 0;
		req->completion = nvmet_core_downlaod_cmd_done;
	 	nvmet_map_admin_prp_entry_takeout(req);		//20201222-Eddie-from CA6
	 	dtag = req->req_prp.mem;
        	void *mem = dtag2mem(dtag[0]);
		evlog_printk(LOG_ALW,"prp offset size start addr %x",req->req_prp.prp[0].size);
		if ((FWUG_SZ - req->req_prp.prp[0].size) != 0)	//For the case which counts matched in both Transfer and nprp, check if first starting addr is aligned to 0 or not..
			req->req_prp.transferred++;

	 	nvmet_prplist_launch_one_dtag_xfer(req,mem);
		//evlog_printk(LOG_ALW,"PRP_list_true_return \n");
		return;
	}

	req->req_prp.transferred++;
	//evlog_printk(LOG_ALW,"CALL FWDL EVT Txfered %d ", req->req_prp.transferred);
	if (nvme_cmd->opc == NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD)
	{
		if (req->req_prp.fg_prp_list == true){
			//evlog_printk(LOG_ALW,"evt_fw_dwnld_128 \n");
			if (evt_fw_dwnld != 0xFF){
				evt_set_imt(evt_fw_dwnld, (u32)req, 0);
			}
			while (!fw_prplist_trx_done){
				cpu_msg_isr();
				}	//Lock Until image txfer done -> CPU2 fwdl_download
			dtag = req->req_prp.mem;
			void *mem = dtag2mem(dtag[0]);
			fw_prplist_trx_done = 0;
			nvmet_prplist_launch_one_dtag_xfer(req,mem);
		}
		else{
			if ((req->req_prp.required * DTAG_SZE) < (req->req_prp.nprp * ctrlr->page_size))
			{
				if (evt_fw_dwnld != 0xFF){
				#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
					pi_req1 = (u32)req;
				#endif
					misc_set_STOP_BGREAD();
				#if (PI_FW_UPDATE == mENABLE)		//20210110-Eddie
					if (_fg_pi_sus_io == false){
						evlog_printk(LOG_ALW,"[FW image DL] pi cmd suspend");
						evt_set_imt(evt_fw_dwnld, (u32)req, 0);
						//nvmet_pi_start_cmd_suspend(req);
						//evlog_printk(LOG_ALW,"[FW image DL] pi cmd suspend DONE");
				#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
					//Move to pi_timer
				#else
                                 #if GC_SUSPEND_FWDL// GC suspend while FW download -20210308-Eddie
						evlog_printk(LOG_ALW,"[FW DL] GC suspend");
						FWDL_GC_Handle(GC_ACT_SUSPEND);
						while (!fwdl_gcsuspend_done){
							}	//Lock Until GC handle done -> CPU3 FWDL_GC_Handle FTL.C
						fwdl_gcsuspend_done = 0;
					#endif
						if ((cmf_idx == 1) || (cmf_idx == 2)){
							//evt_set_imt(evt_fw_dwnld, (u32)req, 0);
							evt_set_delay(evt_fw_dwnld, (u32)req, 0, 10);	//wait for 1 sec cmds consuming
						}
						else{
							evt_set_delay(evt_fw_dwnld, (u32)req, 0, 10);	//wait for 1 sec cmds consuming
					        }
				#endif
					}
					else
						evt_set_imt(evt_fw_dwnld, (u32)req, 0);
				#else
					evt_set_imt(evt_fw_dwnld, (u32)req, 0);
				#endif
				}
			}
			else if (req->req_prp.transferred == req->req_prp.required)
			{
				if (evt_fw_dwnld != 0xFF)
				{
				#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
					pi_req1 = (u32)req;
				#endif
					misc_set_STOP_BGREAD();
					#if (PI_FW_UPDATE == mENABLE)		//20210110-Eddie
						if (_fg_pi_sus_io == false){
							evlog_printk(LOG_ALW,"[FW image DL] pi cmd suspend");
							evt_set_imt(evt_fw_dwnld, (u32)req, 0);
							//nvmet_pi_start_cmd_suspend(req);
							///evlog_printk(LOG_ALW,"[FW image DL] pi cmd suspend DONE");
					#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
						//Move to pi_timer
					#else
                                               #if GC_SUSPEND_FWDL// GC suspend while FW download -20210308-Eddie
							evlog_printk(LOG_ALW,"[FW DL] GC suspend");
							FWDL_GC_Handle(GC_ACT_SUSPEND);
							while (!fwdl_gcsuspend_done){
								}	//Lock Until GC handle done -> CPU3 FWDL_GC_Handle FTL.C
							fwdl_gcsuspend_done = 0;
						#endif
							if ((cmf_idx == 1) || (cmf_idx == 2)){
								//evt_set_imt(evt_fw_dwnld, (u32)req, 0);
								evt_set_delay(evt_fw_dwnld, (u32)req, 0, 10);	//wait for 1 sec cmds consuming
							}
							else{
								evt_set_delay(evt_fw_dwnld, (u32)req, 0, 10);	//wait for 1 sec cmds consuming
						        }
					#endif
						}
						else
							evt_set_imt(evt_fw_dwnld, (u32)req, 0);
					#else
						evt_set_imt(evt_fw_dwnld, (u32)req, 0);
					#endif

					return;
				}
				nvme_apl_trace(LOG_ERR, 0x6492, "not download event handler");
				evt_set_imt(evt_cmd_done, (u32)req, 0);
			}
		}
	}
}

#if 1
static slow_code_ex void fw_download_free_resource(req_t *req)
{
	u32 dtag_cnt = req->req_prp.mem_sz;
	if(req->req_prp.is2dtag)
		dtag_cnt++;
	dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, (dtag_t *)req->req_prp.mem);
	//printk("sys_free mem \n");
	sys_free(SLOW_DATA, req->req_prp.mem);
	//printk("sys_free prp \n");
	sys_free(SLOW_DATA, req->req_prp.prp);
	//printk("core_downlaod_cmd_done 2 \n");
}
static slow_code_ex bool nvmet_core_downlaod_cmd_done(req_t *req)
{	//20201222-Eddie
	//evlog_printk(LOG_ALW,"CORE_DL_cmd_done id[%d]\n",req->req_prp.sec_xfer_idx);
	if(req->req_prp.fg_prp_list != true){	// Not PRP list
	if ((req->req_prp.required * DTAG_SZE) < (req->req_prp.nprp * ctrlr->page_size))
	{
		nvmet_admin_fw_download_xfer_prp(req);
		return true;
	}

	fw_download_free_resource(req);

	#if (PI_FW_UPDATE == mENABLE)			//20210110-Eddie
		bool ret;
		ret = nvmet_core_cmd_done(req);

		if (_fg_pi_resum_io == true){
		#if GC_SUSPEND_FWDL		//20210308-Eddie
			if ((cmf_idx == 3) || (cmf_idx == 4)){	//Temporarily +8 need to GC suspend
				evlog_printk(LOG_ALW,"[FWDL] Resume GC");
				FWDL_GC_Handle(GC_ACT_RESUME);
				while (!fwdl_gcsuspend_done)
				{
					cpu_msg_isr();
					extern void bm_isr_com_free(void);
					bm_isr_com_free();
				}	//Lock Until GC handle done -> CPU3 FWDL_GC_Handle FTL.C
				fwdl_gcsuspend_done = 0;
			}
		#endif
			if ((cmf_idx == 1) || (cmf_idx == 2)){
				nvmet_enable_fetched_cmd_pi();
				evlog_printk(LOG_ALW,"pi cmd resume fetching");
			}
			else{	//512+8 ; 4096+8
				nvmet_enable_fetched_cmd_pi();
				evlog_printk(LOG_ALW,"pi cmd resume fetching");
			}
			_fg_pi_resum_io = false;
			misc_clear_STOP_BGREAD();
		}
		return ret;
	#else
		return nvmet_core_cmd_done(req);
	#endif
	}
	else{	//prp list
		if (req->req_prp.transferred < req->req_prp.nprp){
			//dtag_t *dtag = NULL;
			//dtag = req->req_prp.mem;
			//void *mem = dtag2mem(dtag[0]);
			//nvmet_prplist_launch_one_dtag_xfer(req,mem);
			//return true;
		}
		else
		{
			fw_download_free_resource(req);
			return nvmet_core_cmd_done(req);
		}
	}
	return true;
}
#endif
#else
static fast_code void nvmet_admin_fw_download_xfer_done(void *payload, bool error)
{
	req_t *req = (req_t *)payload;
	struct nvme_cmd *nvme_cmd = req->host_cmd;

	sys_assert(error == false);
	sys_assert(nvme_cmd->opc == NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD);
	if (req->req_prp.fetch_prp_list == true)
	{
		void *mem = NULL;
		dtag_t *dtag;
		int xfer, ofst = 0, cur = 0, sz = 0, i = 0;
		u32 len = req->req_prp.size;
		prp_t *prp = req->req_prp.prp_list;
		req_prp_t *rprp = &req->req_prp;
		prp_entry_t *prp_entry = &rprp->prp[rprp->nprp];

		req->req_prp.fetch_prp_list = false;?		req->req_prp.transferred = 0;
		req->req_prp.required = 0;

		while (len != 0)
		{
			u32 trans_len = min(len, ctrlr->page_size);

			prp_entry->prp = *prp;
			prp_entry->size = trans_len;
			prp_entry++;
			prp++;

			req->req_prp.nprp++;
			len -= trans_len;
		}
		sys_free(SLOW_DATA, req->req_prp.prp_list);

		/* trigger data transfer */
		dtag = req->req_prp.mem;
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			prp_entry_t *prp = &req->req_prp.prp[i];
			int prp_ofst = 0;
			int len = prp->size;
		again:
			if (sz == 0)
			{
				mem = dtag2mem(dtag[cur++]);
				ofst = 0;
				sz = DTAG_SZE;
			}

			xfer = min(len, sz);

			req->req_prp.required++;
			hal_nvmet_data_xfer(prp->prp + prp_ofst,
								ptr_inc(mem, ofst),
								xfer, READ, (void *)req, nvmet_admin_fw_download_xfer_done);

			ofst += xfer;
			prp_ofst += xfer;
			sz -= xfer;
			len -= xfer;

			if (len != 0)
			{
				goto again;
			}
		}

		return;
	}

	if (++req->req_prp.transferred == req->req_prp.required)
	{
		if (evt_fw_dwnld != 0xFF)
		{
			evt_set_imt(evt_fw_dwnld, (u32)req, 0);
			return;
		}

		//nvme_apl_trace(LOG_ERR, 0, "not download event handler");
		dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, (dtag_t *)req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}
#endif

#if CO_SUPPORT_SECURITY
static void nvmet_security_mem_free(req_t *req);
static void nvmet_security_mem_rdy(void *hreq);
static void nvmet_admin_security_send_xfer_done(void *payload, bool error);
static void nvmet_admin_security_receive_xfer_done(void *payload, bool error);
#if 0
static ddr_code NOINLINE void nvmet_admin_security_send_xfer_prp(req_t *req)
{
	int xfer, ofst = 0, cur = 0, sz = 0;
	dtag_t dtag[3];
	void *mem = NULL;
	u32 i; //, dtag_cnt;
	if(secure_com_buf == NULL)
	{
		dtag_t continue_dtag[3] = {0};
		continue_dtag[0] = dtag_get(DTAG_T_SRAM,NULL);
		continue_dtag[1] = dtag_get(DTAG_T_SRAM,NULL);
		continue_dtag[2] = dtag_get(DTAG_T_SRAM,NULL);


		while(!((continue_dtag[1].b.dtag == (continue_dtag[0].b.dtag+1)) && (continue_dtag[2].b.dtag == (continue_dtag[1].b.dtag+1))))
		{
			nvme_apl_trace(LOG_ERR, 0xaa5f, "error! allocate secure_com_buf fail ...");
			if(continue_dtag[2].b.dtag == (continue_dtag[1].b.dtag+1))
			{
				dtag_put(DTAG_T_SRAM, continue_dtag[0]);
				continue_dtag[0] = continue_dtag[1];
				continue_dtag[1] = continue_dtag[2];
				continue_dtag[2] = dtag_get(DTAG_T_SRAM,NULL);
			}
			else
			{
				dtag_put(DTAG_T_SRAM, continue_dtag[0]);
				dtag_put(DTAG_T_SRAM, continue_dtag[1]);
				continue_dtag[0] = continue_dtag[2];
				continue_dtag[1] = dtag_get(DTAG_T_SRAM,NULL);
				continue_dtag[2] = dtag_get(DTAG_T_SRAM,NULL);
			}
		}
		secure_com_buf = dtag2mem(continue_dtag[0]);
		req->req_prp.sec_xfer_buf = secure_com_buf;
	}
	dtag[0] = mem2dtag(secure_com_buf);
	dtag[1] = mem2dtag(secure_com_buf+0x1000);
	dtag[2] = mem2dtag(secure_com_buf+0x2000);

	if (ctrlr->page_size > DTAG_SZE) {
		i = ctrlr->page_size / DTAG_SZE;
	} else {
		i = 1;
	}

	nvme_apl_trace(LOG_INFO, 0xa0f8, "[Max debug] req->req_prp.nprp | %x",req->req_prp.nprp);

	i = req->req_prp.required / i;
	for (; i < req->req_prp.nprp; i++) {
		prp_entry_t *prp = &req->req_prp.prp[i];
		int prp_ofst = 0;
		int len = prp->size;

		if (sz == 0) {
			mem = dtag2mem(dtag[cur++]);

			ofst = 0;
			sz = DTAG_SZE;
		}
		xfer = min(len, sz);
		req->req_prp.required++;
		hal_nvmet_data_xfer(prp->prp + prp_ofst,
				ptr_inc(mem, ofst),
				xfer, READ, (void *) req, nvmet_admin_security_send_xfer_done);

		nvme_apl_trace(LOG_INFO, 0xdf23, "[Max debug] mem  %x",*((u32 *)mem));
		ofst += xfer;

	}
}
#endif
/*!
 * @brief admin Command security send DMA transfer Done(IN & OUT)
 *
 * @param payload	should be request pointer
 * @param error		true for dma transfer error
 *
 * @return		not used
 */
ddr_code void nvmet_admin_security_send_xfer_done(void *payload, bool error)//joe slow->ddr 20201124
{
	req_t *req = (req_t *)payload;
	struct nvme_cmd *nvme_cmd = req->host_cmd;
	dtag_t* dtag = req->req_prp.mem;
	void *mem = dtag2mem(dtag[0]);

	//sys_assert(error == false);
	if (error == true) {
		nvme_xfer_err_rls_source(req);
		return;
	}
	sys_assert(nvme_cmd->opc == NVME_OPC_SECURITY_SEND);

	//memcpy((u8*)req->req_prp.sec_xfer_buf, (u8*)mem, DTAG_SZE);
	nvme_apl_trace(LOG_INFO, 0x1323, "mem %x ; xfer_buf %x",(u32)mem,(u32)req->req_prp.sec_xfer_buf);
	//bm_data_copy((u32)mem, (u32)req->req_prp.sec_xfer_buf, DTAG_SZE, NULL, NULL);
	//memcpy(req->req_prp.sec_xfer_buf, (const void *)mem, DTAG_SZE);

	if (req->req_prp.fetch_prp_list == true) {

		sec_send_prp_list = true;
		u32 len = req->req_prp.size;
		prp_t *prp = req->req_prp.prp_list;
		req_prp_t *rprp = &req->req_prp;
		prp_entry_t *prp_entry = &rprp->prp[rprp->nprp];
		req->req_prp.fetch_prp_list = false;
		req->req_prp.transferred = 0;
		req->req_prp.required = 0;
		while (len != 0) {
			u32 trans_len = min(len, ctrlr->page_size);
			prp_entry->prp = *prp;
			prp_entry->size = trans_len;
			prp_entry++;
			prp++;
			req->req_prp.nprp++;
			len -= trans_len;
		}
		sys_free(SLOW_DATA, req->req_prp.prp_list);
		//nvmet_admin_security_send_xfer_prp(req);
		req->req_prp.required++;
		prp_entry = &rprp->prp[0];
		hal_nvmet_data_xfer(prp_entry->prp, mem, prp_entry->size, READ, (void *) req, nvmet_admin_security_send_xfer_done);
		return;
	}
#if 0//(CO_SUPPORT_ATA_SECURITY == TRUE) // for ATA cmd, marked by Jack Li
	u32 pid = ((nvme_tcg_cmd_dw10_t)nvme_cmd->cdw10).b.protocol_id;
	if(pid == c_ATA_SECURITY)
    {
        ATA_SecuritySend(mem,req);
    }
    else
#else
    {
	/*
		req_prp_t *rprp = &req->req_prp;
		prp_entry_t *prp_entry = &rprp->prp[req->req_prp.transferred];
		void *tmp_ptr = req->req_prp.sec_xfer_buf;

		for(u8 s = 0; s < req->req_prp.transferred; s++)
		{
			tmp_ptr += rprp->prp[s].size;
		}

		u32 prp_head = *((u32 *)tmp_ptr);
		u32 prp_tail = *((u32 *)((tmp_ptr + prp_entry->size)-4));

		nvme_apl_trace(LOG_INFO, 0x6d20, "req_prp.transferred %x",req->req_prp.transferred);
		nvme_apl_trace(LOG_INFO, 0xa369, "prp | 0x%x",*(u32 *)prp_entry);
		nvme_apl_trace(LOG_INFO, 0xe8e4, "tmp_ptr | 0x%x",(u32 *)tmp_ptr);
		nvme_apl_trace(LOG_INFO, 0xd28b, "prp size %x",prp_entry->size);
		nvme_apl_trace(LOG_INFO, 0x5ac5, "prp_head %x",prp_head);
		nvme_apl_trace(LOG_INFO, 0x076b, "prp_tail %x",prp_tail);
	*/
    	//if xfer over a PRP: to do following and related execution
        //req->req_prp.sec_xfer_buf = secure_com_buf;  //reset sec, because xfer change this index
		if((req->req_prp.transferred+1) == req->req_prp.required)
		{
			if(sec_send_prp_list)
			{
				u32 i = 0;
				u32 mem_ofst = 0, xfer = 0;
				//prp_entry_t *prp_entry = &req->req_prp.prp[0];
				for(i=0; i<req->req_prp.transferred; i++)
					mem_ofst += req->req_prp.prp[i].size;
				memcpy(req->req_prp.sec_xfer_buf+mem_ofst, (const void *)mem, req->req_prp.prp[i].size);

				if(req->req_prp.required < req->req_prp.nprp)
				{
					xfer = req->req_prp.prp[i+1].size;

					hal_nvmet_data_xfer(req->req_prp.prp[i+1].prp, mem, xfer, READ, (void *) req, nvmet_admin_security_send_xfer_done);
					req->req_prp.required++;
					req->req_prp.transferred++;

					return;
				}
				else
					sec_send_prp_list = 0;
			}
			else
			{
				memcpy(req->req_prp.sec_xfer_buf, (const void *)mem, DTAG_SZE);
				if(req->req_prp.mem_sz > 1)
				{
					mem = dtag2mem(dtag[1]);
					memcpy(req->req_prp.sec_xfer_buf+DTAG_SZE, (const void *)mem, DTAG_SZE);
				}
			}
			nvmet_security_mem_rdy(req);
		}
    }
#endif

	nvmet_security_mem_free(req);
}
#if 0
static ddr_code NOINLINE void nvmet_admin_security_receive_xfer_prp(req_t *req)
{
	int xfer, ofst = 0, cur = 0, sz = 0;
	dtag_t dtag[3];
	void *mem = NULL;
	u32 i; //, dtag_cnt;

	/* trigger data transfer */
	dtag[0] = mem2dtag(secure_com_buf);
	dtag[1] = mem2dtag(secure_com_buf+0x1000);
	dtag[2] = mem2dtag(secure_com_buf+0x2000);
	//dtag_cnt = req->req_prp.mem_sz;

	if (ctrlr->page_size > DTAG_SZE) {
		i = ctrlr->page_size / DTAG_SZE;
	} else {
		i = 1;
	}

	i = req->req_prp.required / i;
	for (; i < req->req_prp.nprp; i++) {
		prp_entry_t *prp = &req->req_prp.prp[i];
		int prp_ofst = 0;
		int len = prp->size;
//again:
		if (sz == 0) {
			mem = dtag2mem(dtag[cur++]);
			/*if (cur > dtag_cnt) {
				break;
			}*/
			ofst = 0;
			sz = DTAG_SZE;
		}

		xfer = min(len, sz);

		req->req_prp.required++;
		hal_nvmet_data_xfer(prp->prp + prp_ofst,
				ptr_inc(mem, ofst),
				xfer, WRITE, (void *) req, nvmet_admin_security_receive_xfer_done);

		ofst += xfer;
		//prp_ofst += xfer;
		//sz -= xfer;
		//len -= xfer;

		/*if (len != 0) {
			sys_assert(sz == 0);
			goto again;
		}*/
	}
}
#endif
/*!
* @brief admin Command security receive DMA transfer Done(IN & OUT)
*
* @param payload	should be request pointer
* @param error		true for dma transfer error
*
* @return		not used
*/
ddr_code void nvmet_admin_security_receive_xfer_done(void *payload, bool error)
{
	req_t *req = (req_t *)payload;
	struct nvme_cmd *nvme_cmd = req->host_cmd;
	dtag_t* dtag = req->req_prp.mem;
	void *mem = dtag2mem(dtag[0]);

	//sys_assert(error == false);
	if (error == true) {
		nvme_xfer_err_rls_source(req);
		return;
	}
	sys_assert(nvme_cmd->opc == NVME_OPC_SECURITY_RECEIVE);

	if (req->req_prp.fetch_prp_list == true) {
		sec_send_prp_list = true;

		u32 len = req->req_prp.size;
		prp_t *prp = req->req_prp.prp_list;
		req_prp_t *rprp = &req->req_prp;
		prp_entry_t *prp_entry = &rprp->prp[rprp->nprp];

		req->req_prp.fetch_prp_list = false;
		req->req_prp.transferred = 0;
		req->req_prp.required = 0;

		while (len != 0) {
			u32 trans_len = min(len, ctrlr->page_size);

			prp_entry->prp = *prp;
			prp_entry->size = trans_len;
			prp_entry++;
			prp++;

			req->req_prp.nprp++;
			len -= trans_len;
		}
		sys_free(SLOW_DATA, req->req_prp.prp_list);

		//nvmet_admin_security_receive_xfer_prp(req);
		req->req_prp.required++;
		prp_entry = &rprp->prp[0];
		memcpy(mem, (const void *)req->req_prp.sec_xfer_buf, prp_entry->size);
		hal_nvmet_data_xfer(prp_entry->prp, mem, prp_entry->size, WRITE, (void *) req, nvmet_admin_security_receive_xfer_done);
		return;
	}
	if(((req->req_prp.transferred+1) == req->req_prp.required) && (secure_com_buf != NULL))
	{
		if(sec_send_prp_list)
		{
			u32 i = 0;
			u32 mem_ofst = 0, xfer = 0;
			//prp_entry_t *prp_entry = &req->req_prp.prp[0];

			if(req->req_prp.required < req->req_prp.nprp)
			{
				for(i=0; i<req->req_prp.required; i++)
					mem_ofst += req->req_prp.prp[i].size;
				memcpy(mem, (const void *)(req->req_prp.sec_xfer_buf+mem_ofst), req->req_prp.prp[i].size);

				xfer = req->req_prp.prp[i].size;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, mem, xfer, WRITE, (void *) req, nvmet_admin_security_receive_xfer_done);
				req->req_prp.required++;
				req->req_prp.transferred++;

				return;
			}
			else
				sec_send_prp_list = 0;
		}
		/*
		dtag_put(DTAG_T_SRAM, (dtag_t)mem2sdtag(secure_com_buf));
		dtag_put(DTAG_T_SRAM, (dtag_t)mem2sdtag(secure_com_buf+0x1000));
		dtag_put(DTAG_T_SRAM, (dtag_t)mem2sdtag(secure_com_buf+0x2000));
		secure_com_buf = NULL;
		*/
	}
	nvmet_security_mem_free(req);
}

#endif

#if (AUTO_POWER_STATE_FEATURE == FEATURE_SUPPORTED)
/*!
 * @brief admin Command set features Autonomous Power State Transition DMA transfer Done(IN & OUT)
 *
 * @param payload	should be request pointer
 * @param error		if DMA was error
 *
 * @return		not used
 */
static ddr_code void nvmet_admin_set_features_apst_xfer_done(void *payload, bool error)//joe slow->ddr 20201124
{
	req_t *req = (req_t *)payload;
	struct nvme_cmd *nvme_cmd = req->host_cmd;
	enum nvme_feat fid = (enum nvme_feat)(nvme_cmd->cdw10 & 0xff);

	//sys_assert(error == false);
	if (error == true) {
		nvme_xfer_err_rls_source(req);
		return;
	}
	sys_assert(nvme_cmd->opc == NVME_OPC_SET_FEATURES);
	sys_assert(fid == NVME_FEAT_AUTONOMOUS_POWER_STATE_TRANSITION);
	/* now only HMB will use DMA */
	req->req_prp.transferred++;
	if (req->req_prp.transferred == req->req_prp.required)
	{
		dtag_t *dtag = (dtag_t *)req->req_prp.mem;
		apst_data_entry_t *apst_entry = (apst_data_entry_t *)dtag2mem(dtag[0]);
		u32 i;
		bool ret = true;
		apst_t apst;
		bool save = (bool)(nvme_cmd->cdw10 >> 31);

		apst.b.apst_en = nvme_cmd->cdw11 & BIT0;
		for (i = 0; i < IDTFY_NPSS - 1; i++)
		{
			/* The field specified is required to be a non-operational state as described */
			if ((apst_entry[i].itps <= 2) || (apst_entry[i].itps <= i))
			{
				nvme_apl_trace(LOG_ERR, 0x8cb8, "error itps itps %d itpt %d",
							   apst_entry[i].itps, apst_entry[i].itpt);
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
				ret = false;
				break;
			} else if ((apst_entry[i].b.rsvd0 != 0) || (apst_entry[i].b.rsvd32 != 0)) {
				nvme_apl_trace(LOG_ERR, 0x2f05, "error dw0 0x%x dw1 0x%x",
						apst_entry[i].all.dw0, apst_entry[i].all.dw1);
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
				ret = false;
				break;
			}
		}

		for (i = IDTFY_NPSS; i < MAX_NPSS_NUM; i++) {
			if ((apst_entry[i].all.dw0 != 0) || (apst_entry[i].all.dw1 != 0)) {
				nvme_apl_trace(LOG_ERR, 0xc27d,"error dw0 0x%x dw1 0x%x",
						apst_entry[i].all.dw0, apst_entry[i].all.dw1);
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
				ret = false;
				break;
			}
		}

		if (ret)
		{
			memcpy(ctrlr->cur_feat.apst_feat.apst_de, dtag2mem(dtag[0]), IDTFY_NPSS * sizeof(apst_data_entry_t));
			ctrlr->cur_feat.apst_feat.apst.b.apst_en = apst.b.apst_en;
			if (save)
			{
				ctrlr->saved_feat.apst_feat.apst.b.apst_en = apst.b.apst_en;
			}
		}

		dtag_put_bulk(DTAG_T_SRAM, req->req_prp.required, dtag);
		sys_free(SLOW_DATA, req->req_prp.mem);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}
#endif

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
/*!
 * @brief map admin NVM command's PRP1/2 to req_prp
 *
 * Allocate req_prp buffer, if there is prp list, trigger DMA to fetch prp list.
 *
 * @param req	request
 * @param cmd	nvme command
 * @param len	transfer size
 *
 * @return	Return command result
 */
fast_code enum cmd_rslt_t nvmet_map_admin_prp(
	req_t *req, struct nvme_cmd *cmd, u32 len, void (*callback)(void *hcmd, bool error))
{
	prp_entry_t *prp_entry;
	u64 prp1 = cmd->dptr.prp.prp1;
	u64 prp2 = cmd->dptr.prp.prp2;
	req_prp_t *rprp = &req->req_prp;

	struct nvme_cmd *nvme_cmd = req->host_cmd;

	u32 trans_len = ctrlr->page_size - (prp1 & (ctrlr->page_size - 1));

	//evlog_printk(LOG_ALW,"ctrlr->page_size %d, len %d",ctrlr->page_size,len);

	trans_len = min(len, trans_len);

	if ((prp1 & 0x3) || (prp2 & 0x3))
	{
		if (req->req_prp.mem)
		{
			dtag_t *dtags;

			dtags = (dtag_t *)req->req_prp.mem;
			sys_assert(req->req_prp.mem_sz);
			dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, dtags);
			sys_free(SLOW_DATA, req->req_prp.mem);
		}

		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_PRP_OFFSET);

		return HANDLE_RESULT_FAILURE;
	}

	//printk("trans_len = %d \n",trans_len);

	/* XXX: MTS */
	//sys_assert(len <= 256 * 1024);
	extern struct nvme_registers nvmet_regs;
	sys_assert(len <= ( 1 << NVME_MAX_DATA_TRANSFER_SIZE ) * ( 1 << (12 + nvmet_regs.cap.bits.mpsmin)));

	/* start + len may span pages, so +2 */
	rprp->nalloc = (len >> ctrlr->page_bits) + 2;
	rprp->prp = sys_malloc(SLOW_DATA, rprp->nalloc * sizeof(prp_entry_t));
	//nvme_apl_trace(LOG_ERR, 0, "!!!! len %d ; page_bits %d ; nalloc %d ; sizeof prp_entry_t %d \n",len,ctrlr->page_bits,rprp->nalloc,sizeof(prp_entry_t));
	sys_assert(rprp->prp != NULL);
	rprp->transferred = 0;
	rprp->required = 0;
	rprp->fetch_prp_list = false;

	len -= trans_len;

	rprp->size = len;
	prp_entry = &rprp->prp[0];
	prp_entry->prp = prp1;
	prp_entry->size = trans_len;
	rprp->nprp = 1;
	if (nvme_cmd->opc == NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD){
		rprp->fg_prp_list = false;	//default : Not PRP list case
		_fg_prp_list = false;	//default : Not PRP list case
	}
	if (len)
	{
		if (prp2 == 0)
		{
			if (req->req_prp.mem)
			{
				dtag_t *dtags;

				dtags = (dtag_t *)req->req_prp.mem;
				sys_assert(req->req_prp.mem_sz);
				dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, dtags);
				sys_free(SLOW_DATA, req->req_prp.mem);
			}
			sys_free(SLOW_DATA, rprp->prp);

			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_PRP_OFFSET);

			return HANDLE_RESULT_FAILURE;
		}

		if (len > ctrlr->page_size)
		{
			//evlog_printk(LOG_ALW,"PRP List Trx");

			if (nvme_cmd->opc == NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD){
				dtag_t *dtag = req->req_prp.mem;
	           		rprp->prp_list = dtag2mem(dtag[1]);   //use dtag 1 for prp list xfer
			}
			else{
				rprp->prp_list = sys_malloc(SLOW_DATA, ctrlr->page_size);
			}

			sys_assert(rprp->prp_list != NULL);

			rprp->fetch_prp_list = true;
			rprp->required = 1;
			if (nvme_cmd->opc == NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD){
				rprp->fg_prp_list = true;
				_fg_prp_list = true;
				dwnld_com_lock = true;
				//evlog_printk(LOG_ALW,"prp_list prp1 0x[%x][%x]",((u32)(prp1>>32)),(u32)prp1);
				//evlog_printk(LOG_ALW,"prp_list prp2 0x[%x][%x]",((u32)(prp2>>32)),(u32)prp2);
			}

			hal_nvmet_data_xfer(prp2, rprp->prp_list,
								ctrlr->page_size, READ, (void *)req, callback);
			return HANDLE_RESULT_PRP_XFER;
		}
		else
		{
			prp_entry = &rprp->prp[1];
			prp_entry->prp = prp2;
			prp_entry->size = len;
			rprp->size = 0;
			rprp->nprp++;
		}
	}

	return HANDLE_RESULT_DATA_XFER;
}

/*!
 * @brief allocate dtag resource for admin command
 *
 * @param req		the request to allocate resource
 * @param size		how many memory in size to allocate
 *
 * @return		not used
 */
slow_code void nvmet_alloc_admin_res(req_t *req, u32 size) // 20210224 Jamie fast_code -> slow_code
{
	u16 i, required;
	dtag_t *dtag;

	required = occupied_by(size, DTAG_SZE);
	//printk("[nvmet_alloc] DTAG required %d \n",required);
	req->req_prp.mem = sys_malloc(SLOW_DATA, required * sizeof(dtag_t));

	sys_assert(req->req_prp.mem);

	dtag = req->req_prp.mem;
	for (i = 0; i < required; i++)
	{
		dtag[i] = dtag_get(DTAG_T_SRAM, NULL);
		if (dtag[i].dtag == _inv_dtag.dtag)
		{
			dtag[i] = dtag_get_urgt(DTAG_T_SRAM, NULL);
			if (dtag[i].dtag == _inv_dtag.dtag)
				printk("[nvmet_alloc] Insufficient DTAG i = %d require %d dtags\n", i, required);
		}

		sys_assert((dtag[i].dtag != _inv_dtag.dtag));
	}

	req->req_prp.mem_sz = required;
	req->req_prp.data_size = size;
}

#if CO_SUPPORT_SECURITY
/*!
 * @brief release dtag resource for admin command
 *
 * @param req		the admin request
 *
 * @return		none
 */
static fast_code void nvmet_free_admin_res(req_t *req)
{
	dtag_t *dtag;
	u32 i;

	dtag = (dtag_t *)req->req_prp.mem;
	for (i = 0; i < req->req_prp.mem_sz; i++)
		dtag_put(DTAG_T_SRAM, dtag[i]);

	sys_free(SLOW_DATA, req->req_prp.mem);
}
#endif
/*!
 * @brief queue operations sanity check, and set status if fails.
 *
 * @param req	request of create or delete CQ/SQ
 * @param qid	queue Id
 * @param size	queue Size, if size is error, don't check param size
 *
 * @return	Pass for true and fail for false
 */
static fast_code bool nvmet_queue_sanity_check(req_t *req, u16 qid, u16 size, u32 nsid)
{
	struct nvme_cmd *cmd = req->host_cmd;
	bool flag = (cmd->opc==NVME_OPC_CREATE_IO_SQ) ? true : false;
	extern bool create_q_check_cc_reg();
	nvme_apl_trace(LOG_DEBUG, 0x1928, "flag:%d",flag);

#if defined(SRIOV_SUPPORT)
	/* qid depends on PF and VF function */
	u8 fnid = req->fe.nvme.cntlid;
	u8 error = 0;
	if (fnid == 0)
	{
		/* Validate PF IO queue */
		if ((qid >= ctrlr->max_qid) || (qid == 0))
			error = 1;
	}
	else if (fnid <= SRIOV_VF_PER_CTRLR)
	{
		/* Validate VF IO queue */
		if ((qid > SRIOV_FLEX_VF_IO_Q_PER_FUNC) || (qid == 0))
			error = 1;
	}
	else
	{
		/* Not valid PF or VF */
		error = 1;
	}
	if (error)
	{
		nvme_apl_trace(LOG_ERR, 0xb34d, "req(%x) qid(%x)", req, qid);
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return false;
	}
#else
	if ((qid >= ctrlr->max_qid) || (qid == 0))
	{
		nvme_apl_trace(LOG_ERR, 0x513c, "req(%x) qid(%x)", req, qid);
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return false;
	}
#endif

	if ((size > 0) && (size > ctrlr->regs->cap.bits.mqes))
	{
		nvme_apl_trace(LOG_ERR, 0xe5b6, "req(%x) qsize(%x)", req, size);
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_SIZE);
		return false;
	}

	if(create_q_check_cc_reg(flag,size) == false)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_SIZE);
		return false;
	}

	if (nsid == 0) // NSID = 0
	{
		// Nothing to do now
	}
	else if (nsid == NVME_GLOBAL_NS_TAG) // NSID = FFFFFFFFh
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return false;
	}
	else if (nsid > NVMET_NR_NS) // NSID > 1, NSID < FFFFFFFFh
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return false;
	}
	else // NSID = 1
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return false;
	}

	return true;
}

/*!
 * @brief delete I/O completion queue
 *
 * @param req	request to delete I/O CQ
 * @param cmd	nvme command
 *
 * @return	command execution status
 */
static enum cmd_rslt_t nvmet_delete_io_cq(req_t *req, struct nvme_cmd *cmd)
{
	u16 qid = 1;
	u16 cqid = cmd->cdw10;
	struct nvmet_cq *cq;
	u32 nsid = cmd->nsid;
	nvme_apl_trace(LOG_INFO, 0xe942, "Delete I/O CQ: CQID(%d)", cqid);

	if (!nvmet_queue_sanity_check(req, cqid, 0, nsid))
		return HANDLE_RESULT_FAILURE;

#if defined(SRIOV_SUPPORT)
	u8 fnid = req->fe.nvme.cntlid;
	cq = (fnid == 0) ? ctrlr->cqs[cqid] : ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrcq[cqid];
#else
	cq = ctrlr->cqs[cqid];
#endif
	if (cq == NULL)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

#if defined(SRIOV_SUPPORT)
	u8 error = 0;
	if (fnid == 0)
	{
		for (; qid < ctrlr->max_qid; qid++)
		{
			struct nvmet_sq *sq = ctrlr->sqs[qid];
			if (sq && sq->cqid == cqid)
			{
				/* Host software shall ensure that any associated I/O Submission
				 * Queue is deleted prior to Completion Queue deletion */
				error = 1;
				break;
			}
		}
	}
	else
	{
		for (; qid < ctrlr->sr_iov->cq_count[fnid - 1]; qid++)
		{
			struct nvmet_sq *sq = ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrsq[qid];
			if (sq && sq->cqid == cqid)
			{
				/* Host software shall ensure that any associated I/O Submission
				 * Queue is deleted prior to Completion Queue deletion */
				error = 1;
				break;
			}
		}
	}
	if (error)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_DELETION);
		return HANDLE_RESULT_FAILURE;
	}
#else
	for (; qid < ctrlr->max_qid; qid++)
	{
		struct nvmet_sq *sq = ctrlr->sqs[qid];

		if (sq && sq->cqid == cqid)
		{
			/* Host software shall ensure that any associated I/O
			 * Submission Queues is deleted prior to delete a
			 * Completion Queue.
			 */
			nvmet_set_status(&req->fe,
							 NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_QUEUE_DELETION);
			return HANDLE_RESULT_FAILURE;
		}
	}
#endif

#if defined(SRIOV_SUPPORT)
	if ((fnid == 0) && (cqid < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL))
	{
		/* PF0 - Use private and flexible resources */
		nvmet_unset_vfcq(fnid, cqid);
		hal_nvmet_delete_vq_cq(cqid);
	}
	else
	{
		/* Get mapping ID for CQ */
		u8 cq_mid = nvmet_get_flex_cq_map_idx(fnid, cqid);
		/* VF1 to VF32 - Use flexible resources */
		nvmet_unset_vfcq(fnid, cqid);
		if (cq_mid != 0xFF)
		{
			hal_nvmet_delete_vq_cq(cq_mid);
			/* TODO: decrease cq_count[] after delete */
		}
		else
		{
			/* Invalid mapping ID for CQ */
			nvmet_set_status(&req->fe,
							 NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_QUEUE_DELETION);
			return HANDLE_RESULT_FAILURE;
		}
	}
#else
	nvmet_unset_cq(cqid);

#ifdef NVME_SHASTA_MODE_ENABLE
	hal_nvmet_delete_cq(cqid);
#else
	hal_nvmet_delete_vq_cq(cqid);
#endif
#endif

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief delete I/O submission queue
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	Command execution status
 */
static enum cmd_rslt_t nvmet_delete_io_sq(req_t *req, struct nvme_cmd *cmd)
{
	struct list_head *cur, *saved;
	u16 sqid = cmd->cdw10;
	struct nvmet_sq *sq = NULL;
	UNUSED u8 fnid;
	u8 sq_mid;
	u32 nsid = cmd->nsid;
	if (!nvmet_queue_sanity_check(req, sqid, 0, nsid))
		return HANDLE_RESULT_FAILURE;

#if defined(SRIOV_SUPPORT)
	fnid = req->fe.nvme.cntlid;
	sq = (fnid == 0) ? ctrlr->sqs[sqid] : ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrsq[sqid];
#else
	fnid = 0;
	sq = ctrlr->sqs[sqid];
#endif

	if ((sqid == 0) || (sq == NULL))
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

	nvme_apl_trace(LOG_INFO, 0xe988, "Delete I/O SQ: SQID(%d)", sqid);

#if defined(SRIOV_SUPPORT)
	sq_mid = nvmet_get_flex_sq_map_idx(fnid, sqid);
	if (sq_mid == 0xFF)
	{
		nvme_apl_trace(LOG_ERR, 0x52c2, "Mapping failed .....");
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}
#else
	sq_mid = sqid;
#endif

	req->op_fields.admin.sq_id = sq_mid;

	//extern int btn_cmd_idle(void);

	//if ((nvme_hal_check_pending_cmd_by_sq(sq_mid) && list_empty(&nvme_sq_del_reqs)) &&
		//(hal_nvmet_check_io_sq_pending()) &&
		//(btn_cmd_idle()) ) {
	if (1) {
		/*
		 * The command causes all commands submitted to the indicated Submission
		 * Queue that are still in progress to be aborted
		 * TODO: Create linked list to manage host requests.
		 */
		list_for_each_safe(cur, saved, &sq->reqs)
		{
			req_t *req_cur = container_of(cur, req_t, entry);

			list_del_init(&req_cur->entry);
			list_add_tail(&req_cur->entry, &ctrlr->aborted_reqs);
		}
		cmd_proc_delete_sq(sq_mid, req);
	}else{
		nvme_apl_trace(LOG_ALW, 0x626d, "post delsq:0x%x", sq_mid);
		list_del_init(&req->entry);
		list_add_tail(&req->entry, &nvme_sq_del_reqs);
		/* there maybe a lot of commands */
		mod_timer(&sq_del_timer, jiffies + 5);
	}

	return HANDLE_RESULT_RUNNING;
}

/*!
 * @brief create I/O submission queue
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	error code
 */
static enum cmd_rslt_t nvmet_create_io_sq(req_t *req, struct nvme_cmd *cmd)
{
	u16 sqid = cmd->cdw10;
	u16 qsize = cmd->cdw10 >> 16;
	u16 cqid = cmd->cdw11 >> 16;
	enum nvme_qprio qprio = (enum nvme_qprio)((cmd->cdw11 >> 1) & 0x3);
	u8 pc = cmd->cdw11 & 0x1;
	bool result;
	struct nvmet_sq *sq = NULL;
	u32 nsid = cmd->nsid;

	nvme_apl_trace(LOG_INFO, 0xf2d9, " Create I/O SQ: SQID(%d) CQID(%d) SIZE(%d) PRIO(%d)", sqid, cqid, qsize, qprio);

	/* IOL tnvme 17:3.0.0, Qsize==0 is invalid*/
	if (qsize == 0)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_SIZE);
		return HANDLE_RESULT_FAILURE;
	}

	if (!nvmet_queue_sanity_check(req, sqid, qsize, nsid))
		return HANDLE_RESULT_FAILURE;

	/* IOL tnvme 17:6.0.0, prp should have 0h offset */
	if ((cmd->psdt == 0) && (cmd->dptr.prp.prp1 & 0xFFF))
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_PRP_OFFSET);
		return HANDLE_RESULT_FAILURE;
	}

	if (cqid == 0)
	{
		///Andy change error code for IOL 20201026 1.4 Case 10
		///IOL Test 1.4 Case 7 CQ=0, need return NVME_SC_INVALID_QUEUE_IDENTIFIER
		///IOL Test 1.4 Case 10 also set CQID=0 , but need return NVME_SC_COMPLETION_QUEUE_INVALID
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

	/*
	 * tnvme 17:0.3.0 will issue cq_id from 2 to 0xffff
	 * and expect return code = NVME_SC_COMPLETION_QUEUE_INVALID
	 * because tnvme didn't create that CQ
	 *
	 * for cqid > NVMET_NR_IO_QUEUE, we should return
	 * NVME_SC_COMPLETION_QUEUE_INVALID
	 */
#if defined(SRIOV_SUPPORT)
	u8 fnid = req->fe.nvme.cntlid;
	u8 error = 0;
	if (fnid == 0)
	{
		/* Validate PF */
		if (!ctrlr->cqs[cqid] || (cqid >= ctrlr->max_qid))
			error = 1;
	}
	else
	{
		/* Validate VF */
		if (!ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrcq[cqid] || (cqid > SRIOV_FLEX_VF_IO_Q_PER_FUNC))
		{
			error = 1;
		}
	}
	if (error)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_COMPLETION_QUEUE_INVALID);
		return HANDLE_RESULT_FAILURE;
	}
#else
	if ((cqid >= ctrlr->max_qid))
	{
		///Andy change error code for IOL 20201026 1.4 Case 9
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}
	if (!ctrlr->cqs[cqid])
	{
		nvme_apl_trace(LOG_ERR, 0xbb64, "[Andy] CQID no create\n");
		///Andy change error code for IOL 20201026 1.4 Case 10
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_COMPLETION_QUEUE_INVALID);
		return HANDLE_RESULT_FAILURE;
	}
#endif

	if (pc == 0)
	{
		if (ctrlr->regs->cmbsz.bits.sqs == 1)
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_CONTROLLER_MEM_BUF);
			return HANDLE_RESULT_FAILURE;
		}

		if (ctrlr->regs->cap.bits.cqr == 1)
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}
	}

#if defined(SRIOV_SUPPORT)
	error = 0;
	if (fnid == 0)
	{ /* PF function */
		if (ctrlr->sqs[sqid])
			error = 1;
	}
	else
	{
		if (ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrsq[sqid])
			error = 1;
	}
	if (error)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}
#else
	if (ctrlr->sqs[sqid])
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}
#endif

#if defined(SRIOV_SUPPORT)
	if ((req->fe.nvme.cntlid == 0) && (sqid < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL))
	{
		/* PF0 use private and flexible resources */
		nvmet_set_vfsq(fnid, sqid);
		nvmet_bind_vfcq(fnid, sqid, cqid);

		sq = ctrlr->sqs[sqid];
		sq->qprio = qprio;
		sq->head = sq->tail = ~0;
	}
	else
	{
		/* VF1 to VF32 use flexible resources */
		nvmet_set_vfsq(fnid, sqid);
		nvmet_bind_vfcq(fnid, sqid, cqid);

		sq = ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrsq[sqid];
		sq->qprio = qprio;
		sq->head = sq->tail = ~0;
	}
#else
	nvmet_set_sq(sqid);
	nvmet_bind_cq(sqid, cqid);

	sq = ctrlr->sqs[sqid];
	sq->qprio = qprio;
	sq->head = sq->tail = ~0;
#endif

	/* CQR is enabled, so it's contiguous host memory */
	/* TODO: when hw_push_en, no create_sq need */
#if defined(SRIOV_SUPPORT)
	if ((req->fe.nvme.cntlid == 0) && (sqid < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL))
	{
		/* PF0 use private and flexible resources */
		result = hal_nvmet_create_vf_sq(req->fe.nvme.cntlid, sqid, cqid, qsize, qprio, pc, cmd->dptr.prp.prp1);
	}
	else
	{
		/* VF1 to VF32 use flexible resources */
		result = hal_nvmet_create_vf_sq(req->fe.nvme.cntlid, sqid, cqid, qsize, qprio, pc, cmd->dptr.prp.prp1);
	}
#elif defined(NVME_SHASTA_MODE_ENABLE)
	result = hal_nvmet_create_sq(sqid, cqid, qsize, qprio, pc, cmd->dptr.prp.prp1);
#else
	result = hal_nvmet_create_vf_sq(req->fe.nvme.cntlid, sqid, cqid, qsize, qprio, pc, cmd->dptr.prp.prp1);
#endif

	if (result == true)
	{
		INIT_LIST_HEAD(&sq->reqs);
#if 1
		///Andy test IOL 20201013
		//nvme_apl_trace(LOG_ERR, 0, "[Andy] create IO submin\n");
		flagtestS++;
#endif
		is_IOQ_ever_create_or_not->flag = 1;

		return HANDLE_RESULT_FINISHED;
	}
	else
	{
		/* houskeeping */
#if defined(SRIOV_SUPPORT)
		/* PF0 and VFs use same function because all resources comes
		 * from flexible resources */
		nvmet_unbind_vfcq(fnid, sqid, cqid);
		nvmet_unset_vfsq(fnid, sqid);
#else
		nvmet_unbind_cq(sqid, cqid);
		nvmet_unset_sq(sqid);
#endif
		return HANDLE_RESULT_FAILURE;
	}
}

/*!
 * @brief create I/O completion queue
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	Command execution status
 */
static enum cmd_rslt_t nvmet_create_io_cq(req_t *req, struct nvme_cmd *cmd)
{
	u16 qid = cmd->cdw10;
	u16 qsize = cmd->cdw10 >> 16;
	u16 pc = (cmd->cdw11 & BIT(0)) ? 1 : 0;
	u16 ien = (cmd->cdw11 & BIT(1)) ? 1 : 0;
	u16 iv = cmd->cdw11 >> 16;
	struct nvmet_cq *cq = NULL;
	bool result;
	u32 nsid = cmd->nsid;

	nvme_apl_trace(LOG_INFO, 0x2731, "Create I/O CQ: CQID(%d) SIZE(%d) PC(%d)", qid, qsize, pc);

	/* IOL tnvme 16:3.0.0, Qsize==0 is invalid*/
	if (qsize == 0)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_SIZE);
		return HANDLE_RESULT_FAILURE;
	}

	if (!nvmet_queue_sanity_check(req, qid, qsize, nsid))
		return HANDLE_RESULT_FAILURE;

	/* IOL tnvme 16:5.0.0, prp should be 0h offset */
	if ((cmd->psdt == 0) && (cmd->dptr.prp.prp1 & 0xFFF))
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_PRP_OFFSET);
		return HANDLE_RESULT_FAILURE;
	}

	/* For SR-IOV, this value could be 0 to 65 */
#if defined(SRIOV_SUPPORT)
	if (iv > (SRIOV_PF_VF_Q_PER_CTRLR - 1))
#else
#if defined(FPGA)
#define PCIE_REG_MSIXCAP_MXC_TS 8
	if (iv > PCIE_REG_MSIXCAP_MXC_TS)
#else
#if 1 //for edevx test
	extern bool is_pcie_msi_enable(void);
	extern bool is_pcie_msi_x_enable(void);
	extern u32 get_pcie_msi_multiple_message_enable(void);
	extern u32 get_pcie_msi_x_table_size(void);
	u16 spt_iv;
	if (is_pcie_msi_enable())
		spt_iv = get_pcie_msi_multiple_message_enable();
	else if (is_pcie_msi_x_enable())
		spt_iv = get_pcie_msi_x_table_size();
	else
		spt_iv = 0;

	if (iv > spt_iv)
#else
	if (iv >=  ctrlr->max_qid)
#endif
#endif
#endif
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_INTERRUPT_VECTOR);
		return HANDLE_RESULT_FAILURE;
	}

	if (pc == 0)
	{
		if (ctrlr->regs->cmbsz.bits.cqs == 1)
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_CONTROLLER_MEM_BUF);
			return HANDLE_RESULT_FAILURE;
		}
		/* Fix for issue #967: Check for error condition "pc=0 and cqr=1" */
		if (ctrlr->regs->cap.bits.cqr == 1)
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}
	}

#if defined(SRIOV_SUPPORT)
	/* Note: qid is "host cqid" and cq is "flexible cqid" */
	u8 fnid = req->fe.nvme.cntlid;
	cq = (fnid == 0) ? ctrlr->cqs[qid] : ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrcq[qid];
#else
	cq = ctrlr->cqs[qid];
#endif
	if (cq)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

#if defined(SRIOV_SUPPORT)
	if ((req->fe.nvme.cntlid == 0) && (qid < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL))
	{
		/* PF0 which use private and flexible resources */
		nvmet_set_vfcq(req->fe.nvme.cntlid, qid);
		result = hal_nvmet_create_vf_cq(req->fe.nvme.cntlid, qid, qsize, iv, ien, pc, cmd->dptr.prp.prp1);
	}
	else
	{
		/* VF1 to VF32 use flexible resources */
		nvmet_set_vfcq(req->fe.nvme.cntlid, qid);
		result = hal_nvmet_create_vf_cq(req->fe.nvme.cntlid, qid, qsize, iv, ien, pc, cmd->dptr.prp.prp1);
	}
#else
	nvmet_set_cq(qid);

#ifdef NVME_SHASTA_MODE_ENABLE
	result = hal_nvmet_create_cq(qid, qsize, iv, ien, pc, cmd->dptr.prp.prp1);
#else
	result = hal_nvmet_create_vf_cq(req->fe.nvme.cntlid, qid, qsize, iv, ien, pc, cmd->dptr.prp.prp1);
#endif
#endif

	if (result == true)
	{
#if 1
		//Andy test IOL 20201013
		//nvme_apl_trace(LOG_ERR, 0, "[Andy] create IO CompleteQ\n");
		flagtestC++;
#endif
		is_IOQ_ever_create_or_not->flag = 1;

		return HANDLE_RESULT_FINISHED;
	}
	else
	{
#if defined(SRIOV_SUPPORT)
		/* For PF0 and VF1 to VF32 */
		nvmet_unset_vfcq(fnid, qid);
#else
		nvmet_unset_cq(qid);
#endif
		return HANDLE_RESULT_FAILURE;
	}
}

/*!
 * @brief unmask async event request event type, and complete aer request
 *
 * @param event_type	AER event type
 *
 * @return		not used
 */
static ddr_code void nvmet_aer_unmask_event_type(enum nvme_asynchronous_event_type event_type)
{
	ctrlr->aer_evt_mask_bitmap &= ~(BIT(event_type));
	if (ctrlr->aer_outstanding != 0)
	{
		req_t *req = list_first_entry(&ctrlr->aer_outstanding_reqs, req_t, entry);

		list_del_init(&req->entry);
		ctrlr->aer_outstanding--;
		evt_set_imt(evt_aer_out, (u32)req, 0);
	}
}
/*
 *@for aer polling smart critical warning bit
*/
slow_code void AER_Polling_SMART_Critical_Warning_bit(u8 smart_warning){

	enum nvme_event_smart_critical_en_type warning = SMART_CRITICAL_BELOW_AVA_SPARE;
	u8 smart_en = ctrlr->cur_feat.aec_feat.b.smart;
#ifdef AER_Polling
	extern smart_statistics_t *smart_stat;
	u8 smart_warning = smart_stat->critical_warning.raw;
#endif
	//feature support and critical warning set
	nvme_apl_trace(LOG_INFO, 0x852e, "AER smarten: %d smart warning: %d",smart_en,smart_warning);
	if((smart_en & smart_warning) != 0){
		u8 aer_out = smart_en & smart_warning;
		bool flag = false;

		if((aer_out & BELOW_AVA_SPARE_MASK) != 0){//no space
			warning = SMART_STS_SPARE_THRESH;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & TEMPR_EXCEED_THR_MASK) != 0)	{//temp tho
			warning = SMART_STS_TEMP_THRESH;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & INTERNAL_ERR_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & READ_ONLY_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & BACKUP_FAILED_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & PMR_RD_ONLY_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}

AER_IN:
		if(flag == true)
			nvme_apl_trace(LOG_INFO, 0x1bfd, "sub type: %d\n",warning);
			nvmet_evt_aer_in(((NVME_EVENT_TYPE_SMART_HEALTH << 16)|warning),0);

	}
#ifdef AER_Polling
	mod_timer(&AER_timer, jiffies + 2*HZ);
#endif
}

ddr_code void nvmet_aer_unmask_for_getlogcmd(Get_Log_CDW10 cdw10){
	//Get_Log_CDW10 cdw10 = (Get_Log_CDW10)cmd->cdw10;
	enum nvme_asynchronous_event_type type;
	//enum nvme_event_notice_status_type sts;
	u8 sts;
	//if(cmd->opc != NVME_OPC_GET_LOG_PAGE)
	//	return;

	if (cdw10.b.RAE != 0)
		return;

	switch(cdw10.b.lid){
		case NVME_LOG_ERROR:
			type = NVME_EVENT_TYPE_ERROR_STATUS;
			break;
		case NVME_LOG_HEALTH_INFORMATION:
			type = NVME_EVENT_TYPE_SMART_HEALTH;
			break;
		case NVME_LOG_FIRMWARE_SLOT:
			type = NVME_EVENT_TYPE_NOTICE;
			sts = NOTICE_STS_FIRMWARE_ACTIVATION_STARTING;
			break;
		case NVME_LOG_ADDITIONAL_SMART:
			type = NVME_EVENT_TYPE_NOTICE;
			//sts = NOTICE_STS_ERASE_FAIL | NOTICE_STS_PROGRAM_FAIL | NOTICE_STS_GC_READ_FAIL;
			break;
		case NVME_LOG_SANITIZE_STATUS:
			type = NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS;
			epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
			sanitize_log_page_t sanitize_lp = {.all = (epm_smart_data->sanitizeInfo.sanitize_log_page)};
			if(sanitize_lp.b.SanitizeStatus == sSanitizeCompletedNDI)
				sts = NOTICE_STS_SANITIZE_OPERATION_COMPLETED_WITH_UN_DA;
			else
				sts = NOTICE_STS_SANITIZE_OPERATION_COMPLETED;
			break;
		case NVME_LOG_COMMAND_EFFECTS_LOG:
			return;
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
		case NVME_LOG_TELEMETRY_CTRLR_INIT:
			type = NVME_EVENT_TYPE_NOTICE;
			sts = NOTICE_STS_TELEMETRY_LOG_CHANGED;
			break;
#endif
		default:
			return;
	}

	if(type < NVME_EVENT_TYPE_NOTICE)
		ctrlr->aer_evt_sts[type] = 0;
	else if(cdw10.b.lid == NVME_LOG_ADDITIONAL_SMART)
		ctrlr->aer_evt_sts[type] &= ~(BIT(NOTICE_STS_ERASE_FAIL)|BIT(NOTICE_STS_PROGRAM_FAIL)|BIT(NOTICE_STS_GC_READ_FAIL));
	else
		ctrlr->aer_evt_sts[type] &= ~(BIT(sts));

	if(ctrlr->aer_evt_sts[type] == 0) //for notice event
		ctrlr->aer_evt_bitmap &= ~(BIT(type));

	nvmet_aer_unmask_event_type(type);
}

/*!
 * @brief return error log page
 *
 * @param req		request
 * @param cmd		NVM command
 * @param transfer	how many bytes Host requests
 *
 * @return		command status, always data transferring
 */
static enum cmd_rslt_t nvmet_get_lp_log_error(req_t *req,
											  struct nvme_cmd *cmd, u16 transfer)
{
	void *mem;
	dtag_t *dtag;
	struct nvme_error_information_entry *elog;
	u8 elog_last;
	u8 i = 0;
	u16 n_records = transfer >> 6; /* # records to put */
	u32 ofst = 0;
	u8 elog_cur = ctrlr->elog_cur;
	u8 elog_valid = ctrlr->elog_valid;
	enum cmd_rslt_t handle_result;

	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	/* ELPE is 63(0'h based, so the length is 4096 at maximum */
	sys_assert(transfer <= DTAG_SZE);

	nvmet_alloc_admin_res(req, transfer);

	dtag = req->req_prp.mem;
	mem = dtag2mem(dtag[0]);
	memset(mem, 0, DTAG_SZE);

	//nvme_apl_trace(LOG_DEBUG, 0, "Valid(%d) Pos(%d)", elog_valid, elog_cur);
	elog_last = elog_cur;
	while ((elog_valid) > 0 && (n_records > 0))
	{
		if (elog_last == 0)
			elog_last = IDTFY_ELPE;
		else
			--elog_last;

		memcpy((struct nvme_error_information_entry *)mem + i,
			   &ctrlr->elog[elog_last], sizeof(*elog));

		n_records--;
		i++;
		elog_valid--;
	}

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);
	if (handle_result == HANDLE_RESULT_DATA_XFER)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(mem, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
#if 0
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		/* Clear AER event if any */
		ctrlr->aer_evt_sts[NVME_EVENT_TYPE_ERROR_STATUS] &= ~(BIT(ERR_STS_INVALID_SQ));

		if (ctrlr->aer_evt_sts[NVME_EVENT_TYPE_ERROR_STATUS] == 0)
		{
			ctrlr->aer_evt_bitmap &= ~(BIT(NVME_EVENT_TYPE_ERROR_STATUS));
		}
		nvmet_aer_unmask_event_type(NVME_EVENT_TYPE_ERROR_STATUS);
	}
#endif
	return handle_result;
}

/*!
 * @brief return SMART/Health information
 *
 * @param req       request
 * @param cmd       NVM command
 * @param bytes     how many bytes Host requests
 *
 * @return      command status, always data transferring
 */
static enum cmd_rslt_t nvmet_get_lp_health_information(
	req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	u32 nsid = cmd->nsid;
      // nvme_apl_trace(LOG_DEBUG, 0, "get lp health nsid:%d",nsid);

	/* TNVME: 1.1a differs from 1.0 (invalidNamspc_r11a.cpp) */
	if (ctrlr->ns_smart)
	{
		if (nsid == 0 ||( (nsid > NVMET_NR_NS)&&(nsid != NVME_GLOBAL_NS_TAG)))  //joe 20201202 modify for IOL1.4 1.3-20
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}
	}
	else
	{
		if (nsid != NVME_GLOBAL_NS_TAG && nsid != 0) //DM test modify
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}
	}
	struct health_ipc *hlt = sys_malloc(SLOW_DATA, sizeof(health_ipc));
	hlt->avg_erase = 100;
	hlt->max_erase = 1400;
	hlt->avail = 100;
	hlt->spare = 100;
	hlt->thr = 10;
	hlt->cmdreq = (u32 *)req;
	hlt->bytes = sizeof(struct nvme_health_information_page) > bytes ? bytes : sizeof(struct nvme_health_information_page);
	nvmet_alloc_admin_res(req, hlt->bytes);
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GET_SPARE_AVG_ERASE_CNT, 0, (u32)hlt);
	return HANDLE_RESULT_RUNNING;
}

slow_code void ipc_get_spare_avg_erase_cnt_done(volatile cpu_msg_req_t *msg_req)
{
	extern smart_statistics_t *smart_stat;

	struct health_ipc *hlt = (struct health_ipc *)msg_req->pl;
	struct nvme_health_information_page *health;
	u32 used;
	dtag_t *dtag;
	req_t *req = hlt->cmdreq;
	u32 i;
	u32 ofst = 0;
	struct nvme_cmd *cmd = req->host_cmd;
	u32 transfer = hlt->bytes;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	//nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	health = dtag2mem(dtag[0]);
	memset(health, 0, sizeof(*health));
	health->critical_warning.raw = 0;
	health->endu_grp_crit_warn_sumry.raw = 0; //not support

	smart_stat->available_spare = 100 * hlt->avail / hlt->spare;
	smart_stat->available_spare_threshold = hlt->thr;
	health->available_spare = smart_stat->available_spare;
	health->available_spare_threshold = smart_stat->available_spare_threshold;
	extern Ec_Table* EcTbl;

	used = 100 * EcTbl->header.AvgEC / MAX_AVG_EC; // Max Erase Cnt 10000
	if (used > 254)
		used = 255;
	smart_stat->percentage_used = (u8)used;

	health->percentage_used = smart_stat->percentage_used;

	// if (health->available_spare < health->available_spare_threshold)
	// {
	// 	nvme_apl_trace(LOG_ERR, 0, "available spare(%d) < thr(%d)",
	// 				   health->available_spare, health->available_spare_threshold);
	// 	health->critical_warning.bits.available_spare = 1;
	// }
	sys_free(SLOW_DATA, hlt);

#if(PLP_SUPPORT == 1)
    if(!plp_test_flag) {
		u64 discharge_t = get_tsc_64();
        u8 strpg_status = 0;
		//nvme_apl_trace(LOG_WARNING, 0, "aldebug smart_time: 0x%x;",discharge_t);
		discharge_t -= plp_down_time;
		//nvme_apl_trace(LOG_WARNING, 0, "aldebug plp_down_time: 0x%x;0x%x",discharge_t,plp_down_time);
		if((plp_down_time == 0) || (discharge_t > PLP_internal)) {
	        strpg_status = esr_err_flags.b.strpg = !gpio_get_value(GPIO_PLP_STRPG_SHIFT);
			esr_err_flags.b.strpg = esr_err_flags.b.strpg ? esr_err_flags.b.strpg_bak : esr_err_flags.b.strpg;
			esr_err_flags.b.strpg_bak = strpg_status;
		}
    }

	if (esr_err_flags.all & 0xF)
	{
        nvme_apl_trace(LOG_WARNING, 0x48be, "esr_err_flags 0x%x",esr_err_flags.all);
		#if(degrade_mode == ENABLE)
			smart_stat->critical_warning.bits.volatile_memory_backup = 1;
			nvme_apl_trace(LOG_ALW, 0xf591, "Set critical warning bit[4] because plp circuit broken");
      	#endif
		// health->critical_warning.bits.volatile_memory_backup = 1;
        // read_only_flags.b.esr_err = true;
		// if(cur_ro_status != RO_MD_IN)
		// {
		// 	//cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
		// 	nvme_apl_trace(LOG_INFO, 0, "IN RO %x", read_only_flags.all);
		// 	cmd_proc_read_only_setting(true);
		// }
        if(pre_ers != esr_err_flags.all){
            flush_to_nand(EVT_READ_ONLY_MODE_IN);
        }
		// fua write
		esr_err_fua_flag = true;
	}
    // else{
        // health->critical_warning.bits.volatile_memory_backup = 0;
        // read_only_flags.b.esr_err = false;
		// if(cur_ro_status == RO_MD_IN)
		// {
		// 	//cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
		// 	nvme_apl_trace(LOG_INFO, 0, "OUT RO %x", read_only_flags.all);
		// 	if(read_only_flags.all == 0)
		// 	{
		// 		cmd_proc_read_only_setting(false);
		// 	}
		// }
		// normal write
		// esr_err_fua_flag = false;
    // }
	pre_ers=esr_err_flags.all;
#endif
	extern __attribute__((weak)) bool is_system_read_only(void);

	if (is_system_read_only)
	{
		health->critical_warning.bits.read_only = is_system_read_only();
	}

	health_get_io_info(health);
	health_get_power_info(health);
	health_get_errors(health);
	health_get_temperature(health);

	health->critical_warning.raw = smart_stat->critical_warning.raw;

#ifdef SMART_PLP_NOT_DONE
	if(smart_stat->critical_warning.bits.epm_vac_err)
		health->critical_warning.bits.epm_vac_err = 1;
#endif

	if (health->critical_warning.raw & 0x7F)
	{
		nvme_apl_trace(LOG_ERR, 0xa702, "smart error %x", health->critical_warning.raw);
		//smart_stat->critical_warning.raw = health->critical_warning.raw;
#ifndef AER_Polling
		AER_Polling_SMART_Critical_Warning_bit(health->critical_warning.raw);
#endif
		if(pre_critical != health->critical_warning.raw){
		    flush_to_nand(EVT_CRITICAL_WARNING);
        }
	}
	pre_critical = health->critical_warning.raw & 0x7F;

#if 0//(_TCG_)
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	health->rdlocked_sts = mReadLockedStatus;
	health->wrlocked_sts = mWriteLockedStatus;
	health->tcg_sts      = mTcgStatus;
	health->prefmtted    = epm_aes_data->prefmtted;
	health->tcg_err_flag = epm_aes_data->tcg_err_flag;
#endif

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result == HANDLE_RESULT_DATA_XFER)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(health, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
	else if (handle_result == HANDLE_RESULT_FAILURE)
	{
		//dtag_put(DTAG_T_SRAM, dtag[0]);
		//sys_free(SLOW_DATA, req->req_prp.mem);
		//sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}


/*!
 * @brief return firmware slot information
 *
 * @param req	request
 * @param cmd	NVM command
 * @param bytes	how many bytes Host requests
 *
 * @return	command status, always data transferring
 */
static enum cmd_rslt_t nvmet_get_lp_firmware_slot(
	req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	int i = 0;
	u32 ofst = 0;
	struct nvme_firmware_page *firmware_page = NULL;
	u32 transfer = sizeof(*firmware_page) >= bytes ? bytes : sizeof(*firmware_page);

	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;

	firmware_page = dtag2mem(dtag[0]);
	memset(firmware_page, 0, sizeof(*firmware_page));
	memset(firmware_page->revision, 0x20, sizeof(u64));

	firmware_page->afi.slot = ctrlr->cur_afi.slot + 1;
	firmware_page->afi.slot_next = ctrlr->cur_afi.slot_next;
	for (i = 0; i < MAX_FWSLOT; i++)
		memcpy(&firmware_page->revision[i], FR, 7);

	nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	for (i = 0; i < req->req_prp.nprp; i++)
	{
		req->req_prp.required++;

		hal_nvmet_data_xfer(req->req_prp.prp[i].prp,
							ptr_inc(firmware_page, ofst),
							req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
		ofst += req->req_prp.prp[i].size;
	}

	return HANDLE_RESULT_DATA_XFER;
}

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
/*!
 * @brief prepare prp from prplist
 *
 * @param req request
 */
static ddr_code void prplist_prep(req_t *req)
{
	u32 len = req->req_prp.size;
	prp_t *prp = req->req_prp.prp_list;
	req_prp_t *rprp = &req->req_prp;
	prp_entry_t *prp_entry = &rprp->prp[rprp->nprp];

	req->req_prp.fetch_prp_list = false;

	while (len != 0)
	{
		u32 trans_len = min(len, ctrlr->page_size);
		prp_entry->prp = *prp;
		prp_entry->size = trans_len;
		prp_entry++;
		prp++;

		req->req_prp.nprp++;
		len -= trans_len;
	}
	sys_free(SLOW_DATA, req->req_prp.prp_list);

	return;
}

static ddr_code void telemetry_xfer_ddr_to_prp_callback(void *payload, bool error);
static ddr_code void telemetry_ctrlr_data_update_focus();
/*!
 * @brief transfer telemetry data from ddr to sram, then to host.
 *
 * @param req request
 */
static ddr_code void telemetry_xfer_ddr_to_prp(req_t *req)
{
	/* trigger data transfer  */
	int xfer = 0;

	dtag_t *dtag = req->req_prp.mem;

	prp_entry_t *prp = &req->req_prp.prp[telemetry_transfer_prp_count];
	xfer = min(prp->size - telemetry_transfer_prp_offset, DTAG_SZE);

	memcpy( (void *)dtag2mem(dtag[0]), telemetry_dataHandle, xfer );
	telemetry_dataHandle += xfer;

	req->req_prp.required = 1;
	req->req_prp.transferred = 0;
	hal_nvmet_data_xfer(prp->prp + telemetry_transfer_prp_offset,
						(void *)dtag2mem(dtag[0]),
						xfer, WRITE, (void *)req, telemetry_xfer_ddr_to_prp_callback);

	telemetry_transfer_prp_offset += xfer;

	if(telemetry_transfer_prp_offset == prp->size)
	{
		telemetry_transfer_prp_offset = 0;
		telemetry_transfer_prp_count += 1;
	}
}

/*!
 * @brief new dma cmd must issue after dma callback handle finish.
 */
ddr_code static void telemetry_xfer_continue(u32 unused, u32 r0, u32 r1)
{
	req_t *req = (req_t *)r0;
	telemetry_xfer_ddr_to_prp(req);
}

/*!
 * @brief telemetry DMA transfer done, and check telemetry_req_pending list.
 *
 * @param payload should be request pointer
 * @param error   true for dma transfer error
 */
static ddr_code void telemetry_xfer_ddr_to_prp_callback(void *payload, bool error)
{
	req_t *req = (req_t *)payload;

	sys_assert(error == false);

	if (++req->req_prp.transferred == req->req_prp.required)
	{
		if(telemetry_transfer_prp_count < req->req_prp.nprp)
		{
			//telemetry_xfer_ddr_to_prp(req); it may be intr missing.
			evt_set_cs(evt_telemetry_xfer_continue, (u32)req, 0, CS_TASK);

			return;
		}

		dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, (dtag_t *)req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);


		//workaround for Edevx
		if (telemetry_finiteStatusMachine.lid == NVME_LOG_TELEMETRY_CTRLR_INIT)
		{
			epm_telemetry_data->telemetry_ctrlr_available = 0;
			telemetry_ctrlr_data->available = 0;
		}

		evt_set_cs(evt_telemetry_req_check, 0, 0, CS_TASK); //DO NOT USE CS_NOW, DMA callback issue new DMA cmd will cause intr missing.
	}
}
#if 0
ddr_code void telemetry_get_errorInformation( void *mem )
{
	u8 i = 0;
	u8 elog_last = ctrlr->elog_cur;
	u8 elog_valid = ctrlr->elog_valid;
	u8 elog_telemetryCount = sizeof(telemetry_host_data->error_infomation)/ sizeof(struct nvme_error_information_entry);

	while ((elog_valid) > 0 && (elog_telemetryCount > 0))
	{
		if (elog_last == 0)
			elog_last = IDTFY_ELPE;
		else
			--elog_last;

		memcpy((struct nvme_error_information_entry *)mem + i,
			   &ctrlr->elog[elog_last], sizeof(struct nvme_error_information_entry));

		i++;
		elog_valid--;
		elog_telemetryCount--;
	}
}
#endif
ddr_code void telemetry_get_featureSetting( struct telemetry_featureSetting *featureSetting )
{
	featureSetting->temperatureThreshold_over = ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH];
	featureSetting->temperatureThreshold_under = ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH];
	//unsupport
	//featureSetting->hctm = ctrlr->cur_feat.hctm_feat.all;
	//unsupport
	//featureSetting->volatileWriteCache = ctrlr->cur_feat.vwc_feat;
}

ddr_code void telemetry_get_deviceproperty( struct telemetry_deviceproperty *deviceproperty )
{
	srb_t *srb = (srb_t *) SRAM_BASE;
	struct nvme_ctrlr_data *idtfy = (struct nvme_ctrlr_data *)telemetry_idtfy;
	struct nvme_ns_data *ns_idtfy = (struct nvme_ns_data *)telemetry_idtfy;
	struct nvmet_namespace *ns = ctrlr->nsid[0].ns;

	memcpy(deviceproperty->programmer_ver, srb->pgr_ver, 6);
	memcpy(deviceproperty->loader_ver, srb->ldr_ver, 6);
	memcpy(deviceproperty->fw_ver, FR, 7);
	memcpy(deviceproperty->sn, idtfy->sn, 20);
	memcpy(deviceproperty->mn, idtfy->mn, 17);

	deviceproperty->rtd3e = idtfy->rtd3e;
	deviceproperty->rtd3r = idtfy->rtd3r;
	deviceproperty->eui64 = ns_idtfy->eui64;
	deviceproperty->lbaf = ns->lbaf;

}

ddr_code void telemetry_get_namespaceinfo( struct telemetry_namespaceinfo *namespaceinfo )
{
	extern struct ns_array_manage *ns_array_menu;

	for(int i=0;i<32;i++){
		namespaceinfo->ns_attr[i].nsid = ns_array_menu->ns_attr[i].hw_attr.nsid;
		namespaceinfo->ns_attr[i].nsz = ns_array_menu->ns_attr[i].fw_attr.nsz;
		namespaceinfo->ns_attr[i].attached_ctrl_bitmap = ns_array_menu->ns_attr[i].fw_attr.attached_ctrl_bitmap;
	}

	namespaceinfo->total_order_now = ns_array_menu->total_order_now;
	namespaceinfo->host_sec_bits = ns_array_menu->host_sec_bits;

}

#define CMD_PROC_BASE 0xC003C000
static inline u32 telemetry_pcie_core_readl(u32 reg) //copy from pcie.c
{
#if 0//PLP_TEST == 1
	if(plp_trigger){
		rtos_core_trace(LOG_ERR, 0x273f, "doing plp, cant read pcie configuration");
	}
#endif
#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return 0;
	}
#endif
	return readl((void *)(PCIE_CORE_BASE + reg));
}

ddr_code void telemetry_get_frontendregister( struct telemetry_front_end_register *front_end_register )
{
	//pcie core
		front_end_register->nvme_debug_reg_info.status_commnad = telemetry_pcie_core_readl(0x0004);
		front_end_register->nvme_debug_reg_info.pm_con_status  = telemetry_pcie_core_readl(0x0044);
		front_end_register->nvme_debug_reg_info.device_capabilities  = telemetry_pcie_core_readl(0x0074);
		front_end_register->nvme_debug_reg_info.device_control_device_status	= telemetry_pcie_core_readl(0x0078);
		front_end_register->nvme_debug_reg_info.link_capabilities  = telemetry_pcie_core_readl(0x007C);
		front_end_register->nvme_debug_reg_info.link_control_link_status	= telemetry_pcie_core_readl(0x0080);
		front_end_register->nvme_debug_reg_info.device_capablities2  = telemetry_pcie_core_readl(0x0094);
		front_end_register->nvme_debug_reg_info.device_control_device_status2  = telemetry_pcie_core_readl(0x0098);
		front_end_register->nvme_debug_reg_info.link_capabilities2  = telemetry_pcie_core_readl(0x009C);
		front_end_register->nvme_debug_reg_info.link_control_link_status2  = telemetry_pcie_core_readl(0x00A0);

		front_end_register->nvme_debug_reg_info.uncorr_err_status  = telemetry_pcie_core_readl(0x0104);
		front_end_register->nvme_debug_reg_info.uncorr_err_sev  = telemetry_pcie_core_readl(0x010C);
		front_end_register->nvme_debug_reg_info.corr_err_status  = telemetry_pcie_core_readl(0x0110);
		front_end_register->nvme_debug_reg_info.adv_err_cap_ctrl	= telemetry_pcie_core_readl(0x0118);
		front_end_register->nvme_debug_reg_info.tlp_prefix_log1  = telemetry_pcie_core_readl(0x0138);
		front_end_register->nvme_debug_reg_info.tlp_prefix_log2  = telemetry_pcie_core_readl(0x013C);
		front_end_register->nvme_debug_reg_info.tlp_prefix_log3  = telemetry_pcie_core_readl(0x0140);
		front_end_register->nvme_debug_reg_info.tlp_prefix_log4  = telemetry_pcie_core_readl(0x0144);
		front_end_register->nvme_debug_reg_info.lane_err_status  = telemetry_pcie_core_readl(0x0160);

		front_end_register->nvme_debug_reg_info.margin_lane0_cntrl_status  = telemetry_pcie_core_readl(0x01A4);
		front_end_register->nvme_debug_reg_info.margin_lane1_cntrl_status  = telemetry_pcie_core_readl(0x01A8);
		front_end_register->nvme_debug_reg_info.margin_lane2_cntrl_status  = telemetry_pcie_core_readl(0x01AC);
		front_end_register->nvme_debug_reg_info.margin_lane3_cntrl_status  = telemetry_pcie_core_readl(0x01B0);

		front_end_register->nvme_debug_reg_info.ltr_latency  = telemetry_pcie_core_readl(0x01F8);
		front_end_register->nvme_debug_reg_info.l1sub_capbilities  = telemetry_pcie_core_readl(0x0200);
		front_end_register->nvme_debug_reg_info.l1sub_control1  = telemetry_pcie_core_readl(0x0204);
		front_end_register->nvme_debug_reg_info.l1sub_control2  = telemetry_pcie_core_readl(0x0208);
		front_end_register->nvme_debug_reg_info.rasdp_corr_counter_ctrl  = telemetry_pcie_core_readl(0x0218);
		front_end_register->nvme_debug_reg_info.rasdp_corr_counter_report  = telemetry_pcie_core_readl(0x021C);
		front_end_register->nvme_debug_reg_info.rasdp_uncorr_counter_ctrl  = telemetry_pcie_core_readl(0x0220);
		front_end_register->nvme_debug_reg_info.rasdp_uncorr_counter_report  = telemetry_pcie_core_readl(0x0224);

		//pcie wrap
		front_end_register->nvme_debug_reg_info.pcie_core_status = readl((void *)(PCIE_WRAP_BASE + 0x0018));
		front_end_register->nvme_debug_reg_info.pcie_powr_status = readl((void *)(PCIE_WRAP_BASE + 0x001C));
		front_end_register->nvme_debug_reg_info.cfg_mps_mrr = readl((void *)(PCIE_WRAP_BASE + 0x00A8));
		front_end_register->nvme_debug_reg_info.pcie_core_status2 = readl((void *)(PCIE_WRAP_BASE + 0x00B4));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log0 = readl((void *)(PCIE_WRAP_BASE + 0x00D0));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log1 = readl((void *)(PCIE_WRAP_BASE + 0x00D4));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log2 = readl((void *)(PCIE_WRAP_BASE + 0x00D8));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log3 = readl((void *)(PCIE_WRAP_BASE + 0x00DC));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log4 = readl((void *)(PCIE_WRAP_BASE + 0x00E0));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log5 = readl((void *)(PCIE_WRAP_BASE + 0x00E4));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log6 = readl((void *)(PCIE_WRAP_BASE + 0x00E8));
		front_end_register->nvme_debug_reg_info.rxsts_l0_rcvry_log7 = readl((void *)(PCIE_WRAP_BASE + 0x00EC));

		//nvme ctrl
		front_end_register->nvme_debug_reg_info.controller_cap_low = readl((void *)(NVME_BASE + 0x0000));
		front_end_register->nvme_debug_reg_info.controller_cap_high = readl((void *)(NVME_BASE + 0x0004));
		front_end_register->nvme_debug_reg_info.controller_config = readl((void *)(NVME_BASE + 0x00014));
		front_end_register->nvme_debug_reg_info.controller_status = readl((void *)(NVME_BASE + 0x001C));
		front_end_register->nvme_debug_reg_info.admQ_attr = readl((void *)(NVME_BASE + 0x0024));
		front_end_register->nvme_debug_reg_info.adm_sq_base_addr_l = readl((void *)(NVME_BASE + 0x0028));
		front_end_register->nvme_debug_reg_info.adm_sq_base_addr_h = readl((void *)(NVME_BASE + 0x002C));
		front_end_register->nvme_debug_reg_info.adm_cq_base_addr_l = readl((void *)(NVME_BASE + 0x0030));
		front_end_register->nvme_debug_reg_info.adm_cq_base_addr_h = readl((void *)(NVME_BASE + 0x0034));

		front_end_register->nvme_debug_reg_info.internal_int_status = readl((void *)(NVME_BASE + 0x1760));
		front_end_register->nvme_debug_reg_info.intv01_coalescing_cnts = readl((void *)(NVME_BASE + 0x1980));
		front_end_register->nvme_debug_reg_info.intv23_coalescing_cnts = readl((void *)(NVME_BASE + 0x1984));
		front_end_register->nvme_debug_reg_info.intv45_coalescing_cnts = readl((void *)(NVME_BASE + 0x1988));
		front_end_register->nvme_debug_reg_info.intv67_coalescing_cnts = readl((void *)(NVME_BASE + 0x198C));
		front_end_register->nvme_debug_reg_info.intv8_coalescing_cnts = readl((void *)(NVME_BASE + 0x1990));
		front_end_register->nvme_debug_reg_info.arbitration_control_sts = readl((void *)(NVME_BASE + 0x1A00));

		//nvme proc
		front_end_register->nvme_debug_reg_info.unmask_err = readl((void *)(CMD_PROC_BASE + 0x0200));
		front_end_register->nvme_debug_reg_info.mask_err = readl((void *)(CMD_PROC_BASE + 0x0208));
		front_end_register->nvme_debug_reg_info.port_reset_ctrl = readl((void *)(CMD_PROC_BASE + 0x02014));
		front_end_register->nvme_debug_reg_info.cmd_q_base = readl((void *)(CMD_PROC_BASE + 0x0218));
		front_end_register->nvme_debug_reg_info.cmd_q_ptr = readl((void *)(CMD_PROC_BASE + 0x021C));
		front_end_register->nvme_debug_reg_info.cmd_rx_q_base = readl((void *)(CMD_PROC_BASE + 0x0220));
		front_end_register->nvme_debug_reg_info.cmd_rx_q_bas1 = readl((void *)(CMD_PROC_BASE + 0x0224));
		front_end_register->nvme_debug_reg_info.cmd_rx_q_ptr = readl((void *)(CMD_PROC_BASE + 0x0228));
		front_end_register->nvme_debug_reg_info.cmd_cmpl_q_base = readl((void *)(CMD_PROC_BASE + 0x022C));
		front_end_register->nvme_debug_reg_info.cmd_cmpl_q_ptr = readl((void *)(CMD_PROC_BASE + 0x0230));
		front_end_register->nvme_debug_reg_info.host_ctrl = readl((void *)(CMD_PROC_BASE + 0x0234));
		front_end_register->nvme_debug_reg_info.abort_xfer = readl((void *)(CMD_PROC_BASE + 0x0238));

		front_end_register->nvme_debug_reg_info.srmb_key_0 = readl((void *)(CMD_PROC_BASE + 0x02A0));
		front_end_register->nvme_debug_reg_info.srmb_key_1 = readl((void *)(CMD_PROC_BASE + 0x02A4));
		front_end_register->nvme_debug_reg_info.srmb_key_2 = readl((void *)(CMD_PROC_BASE + 0x02A8));
		front_end_register->nvme_debug_reg_info.srmb_key_3 = readl((void *)(CMD_PROC_BASE + 0x02AC));

		front_end_register->nvme_debug_reg_info.dcrc_key_0 = readl((void *)(CMD_PROC_BASE + 0x02B0));
		front_end_register->nvme_debug_reg_info.dcrc_key_1 = readl((void *)(CMD_PROC_BASE + 0x02B4));
		front_end_register->nvme_debug_reg_info.dcrc_key_2 = readl((void *)(CMD_PROC_BASE + 0x02B8));
		front_end_register->nvme_debug_reg_info.dcrc_key_3 = readl((void *)(CMD_PROC_BASE + 0x02BC));

		//btn
		front_end_register->nvme_debug_reg_info.btn_um_int_sts = readl((void *)(BM_BASE + 0x0000));
		front_end_register->nvme_debug_reg_info.btn_control_reg = readl((void *)(BM_BASE + 0x0044));
		front_end_register->nvme_debug_reg_info.btn_ctrl_status = readl((void *)(BM_BASE + 0x0048));
		front_end_register->nvme_debug_reg_info.btn_err_status = readl((void *)(BM_BASE + 0x004C));
		front_end_register->nvme_debug_reg_info.btn_sram_ecc_err_sts = readl((void *)(BM_BASE + 0x0050));
		front_end_register->nvme_debug_reg_info.rd_hcrc_err_data = readl((void *)(BM_BASE + 0x0054));
		front_end_register->nvme_debug_reg_info.dxfr_sram_ecc_sts = readl((void *)(BM_BASE + 0x0058));
		front_end_register->nvme_debug_reg_info.inter_dbg_sts_select = readl((void *)(BM_BASE + 0x0060));
		front_end_register->nvme_debug_reg_info.selected_inter_dbg_sts = readl((void *)(BM_BASE + 0x0064));
		front_end_register->nvme_debug_reg_info.btn_ctag_pointers = readl((void *)(BM_BASE + 0x006C));

		front_end_register->nvme_debug_reg_info.wr_semistrm_ctrl_status = readl((void *)(BM_BASE + 0x0070));
		front_end_register->nvme_debug_reg_info.wr_semistrm_list0_status = readl((void *)(BM_BASE + 0x0074));
		front_end_register->nvme_debug_reg_info.wr_semistrm_list1_status = readl((void *)(BM_BASE + 0x0078));

		front_end_register->nvme_debug_reg_info.dtag_list_ctrl_status = readl((void *)(BM_BASE + 0x0080));
		front_end_register->nvme_debug_reg_info.wd_entry_list_ctrl_status = readl((void *)(BM_BASE + 0x00C0));
		front_end_register->nvme_debug_reg_info.rd_entry_list_ctrl_status = readl((void *)(BM_BASE + 0x0100));

		front_end_register->nvme_debug_reg_info.btn_auto_updt_enable = readl((void *)(BM_BASE + 0x0150));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs_surc0_base = readl((void *)(BM_BASE + 0x0154));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs_surc0_ptrs = readl((void *)(BM_BASE + 0x0158));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_rptr_dbase = readl((void *)(BM_BASE + 0x015C));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_updt_base = readl((void *)(BM_BASE + 0x0160));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_updt_ptrs = readl((void *)(BM_BASE + 0x0164));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_wptr_dbase = readl((void *)(BM_BASE + 0x0168));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs_surc1_base = readl((void *)(BM_BASE + 0x016C));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs_surc1_ptrs = readl((void *)(BM_BASE + 0x0170));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs1_rptr_dbase = readl((void *)(BM_BASE + 0x0174));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs1_updt_base = readl((void *)(BM_BASE + 0x0178));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs1_updt_ptrs = readl((void *)(BM_BASE + 0x017C));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs1_wptr_dbase = readl((void *)(BM_BASE + 0x0180));

		front_end_register->nvme_debug_reg_info.btn_rd_adrs_surc0_base = readl((void *)(BM_BASE + 0x0184));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs_surc0_ptrs = readl((void *)(BM_BASE + 0x0188));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs0_rptr_dbase = readl((void *)(BM_BASE + 0x018C));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs0_updt_base = readl((void *)(BM_BASE + 0x0190));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs0_updt_ptrs = readl((void *)(BM_BASE + 0x0194));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs0_wptr_dbase = readl((void *)(BM_BASE + 0x0198));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs_surc1_base = readl((void *)(BM_BASE + 0x019C));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs_surc1_ptrs = readl((void *)(BM_BASE + 0x01A0));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs1_rptr_dbase = readl((void *)(BM_BASE + 0x01A4));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs1_updt_base = readl((void *)(BM_BASE + 0x01A8));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs1_updt_ptrs = readl((void *)(BM_BASE + 0x01AC));
		front_end_register->nvme_debug_reg_info.btn_rd_adrs1_wptr_dbase = readl((void *)(BM_BASE + 0x01B0));

		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_updt1_base = readl((void *)(BM_BASE + 0x01B4));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_updt1_ptrs = readl((void *)(BM_BASE + 0x01B8));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_wptr1_dbase = readl((void *)(BM_BASE + 0x01BC));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_updt2_base = readl((void *)(BM_BASE + 0x01C0));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_updt2_ptrs = readl((void *)(BM_BASE + 0x01C4));
		front_end_register->nvme_debug_reg_info.btn_wd_adrs0_wptr2_dbase = readl((void *)(BM_BASE + 0x01C8));

		front_end_register->nvme_debug_reg_info.btn_com_free_dtag_dbase = readl((void *)(BM_BASE + 0x01E4));
		front_end_register->nvme_debug_reg_info.btn_com_free_dtag_ptrs = readl((void *)(BM_BASE + 0x01E8));
		front_end_register->nvme_debug_reg_info.btn_com_free_wptr_dbase = readl((void *)(BM_BASE + 0x01EC));
		front_end_register->nvme_debug_reg_info.data_entry_err_count = readl((void *)(BM_BASE + 0x01F0));
}

ddr_code void telemetry_get_backendregister( struct telemetry_back_end_register *back_end_register )
{
	u32 num_offsets[4],reg_offsets[4];
	u32 dests[4];
	u32 num_offset = 0;
	num_offsets[0] = sizeof(eccu_reg_info_t);
	num_offsets[1] = sizeof(ficu_reg_info_t);
	num_offsets[2] = sizeof(ndcu_reg_info_t);
	num_offsets[3] = sizeof(raid_reg_info_t);

	reg_offsets[0] = ECCU_REG_ADDR;
	reg_offsets[1] = FICU_REG_ADDR;
	reg_offsets[2] = NDCU_REG_ADDR;
	reg_offsets[3] = RAID_TOP_REG_ADDR;

	dests[0] = (u32)&(back_end_register->eccu_reg_info.eccu_ver_reg);
	dests[1] = (u32)&(back_end_register->ficu_reg_info.ficu_ctrl_reg0);
	dests[2] = (u32)&(back_end_register->ndcu_reg_info.nf_ctrl_reg0);
	dests[3] = (u32)&(back_end_register->raid_reg_info.raid_cfg_blk_reg);

	for (u32 j = 0; j < 4; j++){
		num_offset = num_offsets[j];
		for (u32 i = 0; i < num_offset; i+=sizeof(u32))
    	{
        	u32 *dest = (u32 *)(dests[j] + i);
			u32 tmp = readl((void *)(reg_offsets[j] + i));
			*dest =((tmp & 0x000000FF) << 24) |
           		((tmp & 0x0000FF00) << 8) |
           		((tmp & 0x00FF0000) >> 8) |
           		((tmp & 0xFF000000) >> 24);
    	}
	}

}

ddr_code void telemetry_get_ErrHandleVar(struct telemetry_errorhandle *ErrHandleVar)
{
//For CPU1
    extern u8 epm_Glist_updating;
    ErrHandleVar->tel_epm_Glist_updating = epm_Glist_updating;
//For CPU2
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_GET_ERRHANDLEVAR_CPU_BE, 0, 0);
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_GET_ERRHANDLEVAR_CPU_BE_SMART, 0, 0);
//For CPU3
    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GET_ERRHANDLEVAR_CPU_FTL, 0, 0);
//For CPU4
    cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_GET_ERRHANDLEVAR_CPU_BE, 0, 0);
}

#if (_TCG_)
ddr_code void telemetry_get_TCG_info(struct telemetry_TCG_sts_info *TCG_sts_info)
{
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	memset((void *)TCG_sts_info, 0, 512);
	// variables

	TCG_sts_info->tcg_en_dis_tag = epm_aes_data->tcg_en_dis_tag;
	TCG_sts_info->init_pfmt_tag  = epm_aes_data->prefmtted;
#if TCG_FS_PSID
	TCG_sts_info->res_cmp_PSID = (u8)CPinMsidCompare(CPIN_PSID_IDX);
#endif
	TCG_sts_info->res_cmp_SID  = (u8)CPinMsidCompare(CPIN_SID_IDX);

	TCG_sts_info->tcg_status   = mTcgStatus;
	TCG_sts_info->tcg_err_flag = epm_aes_data->tcg_err_flag;

#ifdef NS_MANAGE
	for (u8 i = 0; i < NVMET_NR_NS; i++)
	{
		if(ctrlr->nsid[i].type != NSID_TYPE_UNALLOCATED)
		{
			TCG_sts_info->bmp_ns_created |= (1 << i);
		}
	}
#endif
	TCG_sts_info->bmp_nGR_rd_locked = mReadLockedStatus;
	TCG_sts_info->bmp_nGR_wr_locked = mWriteLockedStatus;
	for(u8 idx=0; idx<LOCKING_RANGE_CNT; idx++)
	{
		u32 rng_no = pLockingRangeTable[idx].rangeNo;

		// locked info on the table
		// GR, R1, ..., R16
		TCG_sts_info->rd_lock_en |= (pG3->b.mLckLocking_Tbl.val[rng_no].readLockEnabled  << rng_no);
		TCG_sts_info->rd_locked  |= (pG3->b.mLckLocking_Tbl.val[rng_no].readLocked       << rng_no);
		TCG_sts_info->wr_lock_en |= (pG3->b.mLckLocking_Tbl.val[rng_no].writeLockEnabled << rng_no);
		TCG_sts_info->wr_locked  |= (pG3->b.mLckLocking_Tbl.val[rng_no].writeLocked      << rng_no);

		if(rng_no==0)
			break;
		TCG_sts_info->nGR_slba[rng_no] = pLockingRangeTable[idx].rangeStart;
		TCG_sts_info->nGR_elba[rng_no] = pLockingRangeTable[idx].rangeEnd;
	}

	// on table
	TCG_sts_info->tcg_act   = pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle;
	TCG_sts_info->mbr_en    = pG3->b.mLckMbrCtrl_Tbl.val[0].enable;
	TCG_sts_info->mbr_done  = pG3->b.mLckMbrCtrl_Tbl.val[0].done;

	// Admin(Admins, Makers, SID, Adm1-4, PSID): 8
	u8 en_auth_shift = 0;
	for(u8 idx=1; idx<ADM_AUTHORITY_TBLOBJ_CNT; idx++, en_auth_shift++)
	{
		TCG_sts_info->auth_en |= ((pG1->b.mAdmAuthority_Tbl.val[idx].enabled & 0x1) << en_auth_shift);
	}
	// Lck(Admins, Adm1-4, Usrs, Usr1-9, rsvd_Usr10-17): 15+8
	for(u8 idx=1; idx<LCK_AUTHORITY_TBLOBJ_CNT; idx++, en_auth_shift++)
	{
		TCG_sts_info->auth_en |= ((pG3->b.mLckAuthority_Tbl.val[idx].enabled & 0x1) << en_auth_shift);
	}

	// Admin(SID, Adm1-4, PSID): 6
	u8 cpin_auth_shift = 0;
	for(u8 idx=0; idx<ADM_CPIN_TBLOBJ_CNT; idx++, cpin_auth_shift++)
	{
		if(pG1->b.mAdmCPin_Tbl.val[idx].uid == UID_CPIN_MSID)
		{
			cpin_auth_shift--;
			continue;
		}
		TCG_sts_info->auth_tries[cpin_auth_shift] = pG1->b.mAdmCPin_Tbl.val[idx].tries;
	}
	// Lck(Adm1-4, Usr1-9, rsvd_Usr10-17): 13+8
	for(u8 idx=0; idx<LCK_CPIN_TBLOBJ_CNT; idx++, cpin_auth_shift++)
	{
		TCG_sts_info->auth_tries[cpin_auth_shift] = pG3->b.mLckCPin_Tbl.val[idx].tries;
	}
}
#endif

ddr_code void telemetry_get_journal( void *mem )
{
	epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
	memcpy(mem, epm_journal_data->info, 4096);
	//nvme_apl_trace(LOG_ALW,"info[0]:%x",epm_journal_data->info);
}

/*!
 * @brief manage telemetry task status(receiveData/transferData)
 */
static ddr_code void telemetry_task_manager(void)
{
	epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag); //for edvx
	struct journal_tag *ptr = (struct journal_tag*)(((u32*)(void*)epm_journal_data)+4);
	extern bool panic_occure[4];
	static u32 telemetry_require_task_count = 0;
	switch(telemetry_finiteStatusMachine.telemetry_task_status){
		case TELEMETRY_TASK_REQUIRE_DATA:
			telemetry_require_task_count = 0;

			if (telemetry_finiteStatusMachine.lid == NVME_LOG_TELEMETRY_HOST_INIT)
			{
				if (telemetry_finiteStatusMachine.telemetry_update == false) {
					telemetry_finiteStatusMachine.telemetry_task_status = TELEMETRY_TASK_RECEIVE_DATA;
					goto trasefer;
				}else
				{
					telemetry_host_data->available = epm_telemetry_data->telemetry_ctrlr_available = 1;
					//host-initated signal controller-initated, so add 1 in here.
					telemetry_host_data->gen_num = epm_telemetry_data->telemetry_ctrlr_gen_num + 1;
					#if (PLP_SUPPORT == 0)
					epm_update(SMART_sign, (CPU_ID - 1));
					#endif
				}

				telemetry_require_task_size = 5;

				//telemetry_get_errorInformation( &(telemetry_host_data->error_infomation) );

				telemetry_get_featureSetting( &(telemetry_host_data->featureSetting) );

				telemetry_get_deviceproperty( &(telemetry_host_data->deviceproperty) );

				telemetry_get_namespaceinfo( &(telemetry_host_data->namespaceinfo) );

				telemetry_get_frontendregister( &(telemetry_host_data->front_end_register));

				telemetry_get_backendregister(&(telemetry_host_data->back_end_register));

				if(!panic_occure[1] && !panic_occure[2] && !panic_occure[3]){
					telemetry_get_ErrHandleVar(&(telemetry_host_data->errorhandle));
				}

			#if (_TCG_)
				telemetry_get_TCG_info(&(telemetry_host_data->TCG_sts_info));
			#endif

				telemetry_host_data->format_version = ((ptr+(epm_journal_data->journal_offset-1))->event_id); //for edvx

			}else
			{	//NVME_LOG_TELEMETRY_CTRLR_INIT
				/*if( telemetry_finiteStatusMachine.rae == 0 )
				{
					telemetry_ctrlr_data->available = 0;
					epm_telemetry_data->telemetry_ctrlr_available = telemetry_ctrlr_data->available;
				}*/

				if (telemetry_finiteStatusMachine.telemetry_update == false) {
					telemetry_finiteStatusMachine.telemetry_task_status = TELEMETRY_TASK_RECEIVE_DATA;
					goto trasefer;
				}

				epm_telemetry_data->telemetry_ctrlr_available = 1;
				telemetry_ctrlr_data->available = 1;
				epm_telemetry_data->telemetry_ctrlr_gen_num++;
				telemetry_ctrlr_data->gen_num = epm_telemetry_data->telemetry_ctrlr_gen_num;
				#if (PLP_SUPPORT == 0)
				epm_update(SMART_sign, (CPU_ID - 1));
				#endif

				telemetry_require_task_size = 1;

				telemetry_get_journal( &(telemetry_ctrlr_data->journal));

				telemetry_ctrlr_data->format_version = ((ptr+(epm_journal_data->journal_offset-1))->event_id); //for edvx

				telemetry_08h_trigger_flag = 1;

				//cpu_msg_issue(	2, CPU_MSG_GET_CPU3_TELEMETRY_CTRLR_DATA, 0, (u32)telemetry_ctrlr_data->runtime_log );
			}

			telemetry_finiteStatusMachine.telemetry_task_status = TELEMETRY_TASK_RECEIVE_DATA;
			//telemetry data maybe only receive from CPU1, break; is not necessary.
		case TELEMETRY_TASK_RECEIVE_DATA:
			if (++telemetry_require_task_count == telemetry_require_task_size)
			{
				if (telemetry_finiteStatusMachine.lid == NVME_LOG_TELEMETRY_CTRLR_INIT && \
					telemetry_finiteStatusMachine.telemetry_update == true)
				{
					if (ctrlr->cur_feat.aec_feat.b.tln){
							nvmet_evt_aer_in(((NVME_EVENT_TYPE_NOTICE << 16)|NOTICE_STS_TELEMETRY_LOG_CHANGED),0);
					}
					evt_set_cs(evt_telemetry_req_check, 0, 0, CS_NOW);
					return;
				}

trasefer:
				if (telemetry_finiteStatusMachine.lid == NVME_LOG_TELEMETRY_CTRLR_INIT && \
					telemetry_finiteStatusMachine.telemetry_update == false && telemetry_08h_trigger_flag) //prevent 07h triggr 08h and cost too much time to save log
				{
					extern struct timer_list telemetry_log_update_timer;
					extern bool needupdate_flag;

					if(needupdate_flag || panic_occure[2])
					{	
						cpu_msg_issue(	2, CPU_MSG_GET_CPU3_TELEMETRY_CTRLR_DATA, 0, (u32)telemetry_ctrlr_data->runtime_log );

						needupdate_flag = false;
						mod_timer(&telemetry_log_update_timer, jiffies + 600*HZ);
						telemetry_08h_trigger_flag = 0;
						return;
					}
				}

				telemetry_transfer_prp_count = 0;
				//telemetry_xfer_ddr_to_prp(telemetry_transfer_req); it may be intr missing.
				evt_set_cs(evt_telemetry_xfer_continue, (u32)telemetry_transfer_req, 0, CS_TASK);
			}
			break;
		default:
			panic(0);
	}
}
/*!
 * @brief receive telemetry data receive status.
 */
static ddr_code void telemetry_task_receiver(volatile cpu_msg_req_t *msg_req)
{
	telemetry_task_manager();
}

/*!
 * @brief prepare prplist, then require/transfer telemetry data.
 *
 * @param payload should be request pointer
 * @param error   true for dma transfer error
 */
static ddr_code void telemetry_prplist_prep_callback(void *payload, bool error)
{
	req_t *req = (req_t *)payload;

	sys_assert(error == false);

	if(req->req_prp.fetch_prp_list == true)
	{
		prplist_prep(req);
	}

	telemetry_task_manager();
}

/*!
 * @brief update controller-initate log
 */
static ddr_code void telemetry_ctrlr_data_update_focus()
{
	if (epm_telemetry_data == NULL)
		epm_telemetry_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	telemetry_finiteStatusMachine.lid = NVME_LOG_TELEMETRY_CTRLR_INIT;
	telemetry_finiteStatusMachine.telemetry_update = true;
	telemetry_finiteStatusMachine.telemetry_task_status = TELEMETRY_TASK_REQUIRE_DATA;
	telemetry_task_manager();
}

/*!
 * @brief check transferring and update controller-initate log
 */
ddr_code void telemetry_ctrlr_data_update()
{
	if (!telemetry_transferring)
	{
		telemetry_transferring = true;
		telemetry_ctrlr_data_update_focus();
	}else
	{
		telemetry_pending_task = 1;
	}
}

/*!
 * @brief return telemetry host information
 *
 * @param req	request
 * @param cmd	NVM command
 * @param bytes	how many bytes Host requests
 *
 * @return	command status, always data transferring
 */
static ddr_code enum cmd_rslt_t nvmet_get_lp_telemetry_host(req_t *req, struct nvme_cmd *cmd, u32 bytes, u8 lid)
{
	spin_lock_take(SPIN_LOCK_KEY_JOURNAL, 0, true); //for edvx
	journal_update(JNL_TAG_RETRIEVAL_LOG, 0);
	spin_lock_release(SPIN_LOCK_KEY_JOURNAL);

	u32 ofst = ((u64)cmd->cdw13 << 32) | ((u64)cmd->cdw12);

	u32 transfer_size = sizeof(*telemetry_host_data) <= bytes ? sizeof(*telemetry_host_data) : bytes;
	transfer_size = min( sizeof(*telemetry_host_data) - ofst, transfer_size );

	bool telemetry_update = true;

	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if ((cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG) || (ofst % TELEMETRY_SECTOR_SIZE) || (ofst >= sizeof(*telemetry_host_data)) || (bytes % TELEMETRY_SECTOR_SIZE)){
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	/*dw12 offset lower shoule be multiple of 512Byte */
	u8 lsp = (cmd->cdw10 >> 8) & 0x7; //bit 11:08  Log Specific Field (LSP)
	//telemetry should not update until true or fw commit command or pwr on reset
	bool create = lsp & BIT(0) ? true : false;   // Fig 205
	telemetry_update = create ? true : false;

	static slow_data bool telemetry_first_update = true;

	telemetry_update |= telemetry_first_update;
	telemetry_first_update = false;

	//epm
	if (epm_telemetry_data == NULL)
		epm_telemetry_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	telemetry_dataHandle = (void * )telemetry_host_data + ofst;

	telemetry_transfer_req = req;

	telemetry_finiteStatusMachine.lid = lid;
	telemetry_finiteStatusMachine.telemetry_update = telemetry_update;
	telemetry_finiteStatusMachine.telemetry_task_status = TELEMETRY_TASK_REQUIRE_DATA;

	nvmet_alloc_admin_res(req, DTAG_SZE);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer_size, telemetry_prplist_prep_callback);
	if(handle_result == HANDLE_RESULT_DATA_XFER) {
		telemetry_task_manager();
	}

	if(handle_result != HANDLE_RESULT_FAILURE) {
		if (ofst == 0 && telemetry_update)
			telemetry_ctrlr_data_update();
		return HANDLE_RESULT_RUNNING;
	}

	return handle_result;
}

/*!
 * @brief return telemetry controller information
 *
 * @param req	request
 * @param cmd	NVM command
 * @param bytes	how many bytes Host requests
 *
 * @return	command status, always data transferring
 */
static ddr_code enum cmd_rslt_t nvmet_get_lp_telemetry_ctrlr(req_t *req, struct nvme_cmd *cmd, u32 bytes, u8 lid)
{
	u32 ofst = ((u64)cmd->cdw13 << 32) | ((u64)cmd->cdw12);

	Get_Log_CDW10 cdw10 = (Get_Log_CDW10)cmd->cdw10;

	u32 transfer_size = sizeof(*telemetry_ctrlr_data) <= bytes ? sizeof(*telemetry_ctrlr_data) : bytes;
	transfer_size = min( sizeof(*telemetry_ctrlr_data) - ofst, transfer_size );

	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

#if 0
	u8 rae = (cmd->cdw10 & BIT(15)) >> 15;
#endif

	if ((cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG) || (ofst % TELEMETRY_SECTOR_SIZE) || (ofst >= sizeof(*telemetry_ctrlr_data)) || (bytes % TELEMETRY_SECTOR_SIZE)){
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	//epm
	if (epm_telemetry_data == NULL)
	{
		epm_telemetry_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		telemetry_ctrlr_data->available = epm_telemetry_data->telemetry_ctrlr_available;
		telemetry_ctrlr_data->gen_num = epm_telemetry_data->telemetry_ctrlr_gen_num;
	}

	telemetry_dataHandle = (void * )telemetry_ctrlr_data + ofst;

	telemetry_transfer_req = req;

	telemetry_finiteStatusMachine.lid = lid;
	telemetry_finiteStatusMachine.rae = cdw10.b.RAE;
	telemetry_finiteStatusMachine.telemetry_update = false;
	telemetry_finiteStatusMachine.telemetry_task_status = TELEMETRY_TASK_REQUIRE_DATA;

	nvmet_alloc_admin_res(req, DTAG_SZE);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer_size, telemetry_prplist_prep_callback);
	if(handle_result == HANDLE_RESULT_DATA_XFER) {
		telemetry_task_manager();
	}

	if(handle_result != HANDLE_RESULT_FAILURE)
	{

		return HANDLE_RESULT_RUNNING;
	}

	return handle_result;
}
#endif

#if 1

/*!
 * @brief fill out nvme command effect log structure
 *
 * @param mem	buffer to fill nvme command effect log data
 *
 * @return	None
 *
 * @date 2019-12-12
 * @author Alan MS Lin
 */

static ddr_code void nvmet_command_effect_log(struct LogPageCommandEffectsEntry_t *celp)//joe slow->ddr 20201124
{
	//struct LogPageCommandEffectsEntry_t *celp = (struct LogPageCommandEffectsEntry_t *)mem;

	celp->ACS[NVME_OPC_DELETE_IO_SQ].CSE_B17 = 1;
	celp->ACS[NVME_OPC_DELETE_IO_SQ].CSUPP = 1;

	celp->ACS[NVME_OPC_CREATE_IO_SQ].CSE_B17 = 1;
	celp->ACS[NVME_OPC_CREATE_IO_SQ].CSUPP = 1;

	celp->ACS[NVME_OPC_GET_LOG_PAGE].CSUPP = 1;

	celp->ACS[NVME_OPC_DELETE_IO_CQ].CSE_B17 = 1;
	celp->ACS[NVME_OPC_DELETE_IO_CQ].CSUPP = 1;

	celp->ACS[NVME_OPC_CREATE_IO_CQ].CSE_B17 = 1;
	celp->ACS[NVME_OPC_CREATE_IO_CQ].CSUPP = 1;

	celp->ACS[NVME_OPC_IDENTIFY].CSUPP = 1;
	celp->ACS[NVME_OPC_ABORT].CSUPP = 1;
	celp->ACS[NVME_OPC_SET_FEATURES].CSUPP = 1;
	celp->ACS[NVME_OPC_SET_FEATURES].NCC = 1;
	celp->ACS[NVME_OPC_SET_FEATURES].NIC = 1;
	celp->ACS[NVME_OPC_SET_FEATURES].CCC = 1;
	celp->ACS[NVME_OPC_GET_FEATURES].CSUPP = 1;
	celp->ACS[NVME_OPC_ASYNC_EVENT_REQUEST].CSUPP = 1;

	celp->ACS[NVME_OPC_NS_MANAGEMENT].CSE_B16 = 1; //joe test 1-->0 20200909
	celp->ACS[NVME_OPC_NS_MANAGEMENT].NIC = 0;
	celp->ACS[NVME_OPC_NS_MANAGEMENT].NCC = 0;
	celp->ACS[NVME_OPC_NS_MANAGEMENT].LBCC = 0;
	celp->ACS[NVME_OPC_NS_MANAGEMENT].CSUPP = 1;

	celp->ACS[NVME_OPC_FIRMWARE_COMMIT].CSE_B18 = 1; //for GTP spec
	celp->ACS[NVME_OPC_FIRMWARE_COMMIT].CCC = 1;
	celp->ACS[NVME_OPC_FIRMWARE_COMMIT].CSUPP = 1;

	celp->ACS[NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD].CSUPP = 1;
	celp->ACS[NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD].CSE_B18 = 1;
#if defined(DST)
	celp->ACS[NVME_OPC_DEV_SELF_TEST].CSUPP = 1;
#endif
	celp->ACS[NVME_OPC_NS_ATTACHMENT].CSE_B16 = 1;
	celp->ACS[NVME_OPC_NS_ATTACHMENT].CSUPP = 1;

	celp->ACS[NVME_OPC_VIRTUALIZATION_MANAGEMENT].CSUPP = 0; //for GTP spec

	celp->ACS[NVME_OPC_FORMAT_NVM].CSE_B16 = 1;
	celp->ACS[NVME_OPC_FORMAT_NVM].NCC = 1;
	celp->ACS[NVME_OPC_FORMAT_NVM].LBCC = 1;
	celp->ACS[NVME_OPC_FORMAT_NVM].CSUPP = 1;

	celp->ACS[NVME_OPC_SECURITY_SEND].CSE_B16 = 0; //for GTP spec
	celp->ACS[NVME_OPC_SECURITY_SEND].CSUPP = 1;

	celp->ACS[NVME_OPC_SECURITY_RECEIVE].CSE_B16 = 0; //for GTP spec
	celp->ACS[NVME_OPC_SECURITY_RECEIVE].CSUPP = 1;

#if CO_SUPPORT_SANITIZE
	celp->ACS[NVME_OPC_SANITIZE].CSE_B16 = 1;
	celp->ACS[NVME_OPC_SANITIZE].CSUPP = 1;
#endif
#if VALIDATE_BOOT_PARTITION
	celp->ACS[NVME_OPC_VSC_BP_READ].CSUPP = 1;
#endif
	celp->ACS[NVME_OPC_OCSSD_V20_GEOMETRY].CSUPP = 0; //for GTP spec

#ifdef VSC_CUSTOMER_ENABLE // FET, RelsP2AndGL
	celp->ACS[NVME_OPC_SSSTC_VSC_CUSTOMER].CSUPP = 1;
#endif
	celp->ACS[NVME_OPC_SSSTC_VSC_NONE].CSUPP = 1;
	celp->ACS[NVME_OPC_SSSTC_VSC_WRITE].CSUPP = 1;
	celp->ACS[NVME_OPC_SSSTC_VSC_READ].CSUPP = 1;

	celp->IOCS[NVME_OPC_FLUSH].LBCC = 0; //for GTP spec
	celp->IOCS[NVME_OPC_FLUSH].CSUPP = 1;
	celp->IOCS[NVME_OPC_WRITE].LBCC = 1;
	celp->IOCS[NVME_OPC_WRITE].CSUPP = 1;

	celp->IOCS[NVME_OPC_READ].CSUPP = 1;

	celp->IOCS[NVME_OPC_WRITE_UNCORRECTABLE].LBCC = 1;
	celp->IOCS[NVME_OPC_WRITE_UNCORRECTABLE].CSUPP = 1;

	/*Compare command: 0x05*/
	celp->IOCS[NVME_OPC_COMPARE].CSUPP = 0; //for GTP Spec by Joylon 20211015

	/* 0x06-0x07 - reserved */
	/*Write Zeroes command: 0x08*/
	celp->IOCS[NVME_OPC_WRITE_ZEROES].LBCC = 0; //for GTP spec
	celp->IOCS[NVME_OPC_WRITE_ZEROES].CSUPP = 0;

	/*Dataset Management command: 0x09*/
	celp->IOCS[NVME_OPC_DATASET_MANAGEMENT].LBCC = 1;
	celp->IOCS[NVME_OPC_DATASET_MANAGEMENT].CSUPP = 1;

	/*Reservation Register command: 0x0d*/
	celp->IOCS[NVME_OPC_RESERVATION_REGISTER].CSUPP = 0; //for GTP spec
	/*Reservation Report command: 0x0e*/
	celp->IOCS[NVME_OPC_RESERVATION_REPORT].CSUPP = 0;
	/*Reservation Acquire command: 0x11*/
	celp->IOCS[NVME_OPC_RESERVATION_ACQUIRE].CSUPP = 0;
	/*Reservation Release command: 0x15*/
	celp->IOCS[NVME_OPC_RESERVATION_RELEASE].CSUPP = 0;

	celp->IOCS[NVME_OPC_OCSSD_V20_VEC_CHUNK_RESET].LBCC = 0;
	celp->IOCS[NVME_OPC_OCSSD_V20_VEC_CHUNK_RESET].CSUPP = 0;

	celp->IOCS[NVME_OPC_OCSSD_V20_VEC_CHUNK_WRITE].LBCC = 0;
	celp->IOCS[NVME_OPC_OCSSD_V20_VEC_CHUNK_WRITE].CSUPP = 0;

	celp->IOCS[NVME_OPC_OCSSD_V20_VEC_CHUNK_READ].CSUPP = 0;

	celp->IOCS[NVME_OPC_OCSSD_V20_VEC_CHUNK_COPY].LBCC = 0;
	celp->IOCS[NVME_OPC_OCSSD_V20_VEC_CHUNK_COPY].CSUPP = 0;
}

/*!
 * @brief return commands supported and effects information
 *
 * @param req	request
 * @param cmd	NVM command
 * @param bytes	how many bytes Host requests
 *
 * @return	command status, always data transferring
 *
 * @date 2019-12-12
 * @author Alan MS Lin
 */
static enum cmd_rslt_t nvmet_get_lp_command_effects_log(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	struct LogPageCommandEffectsEntry_t *command_effect_page = NULL;
	u32 i = 0, ofst = 0;
	u32 transfer = sizeof(*command_effect_page) > bytes ? bytes : sizeof(*command_effect_page);
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	command_effect_page = dtag2mem(dtag[0]);
	memset(command_effect_page, 0, sizeof(*command_effect_page));

	nvmet_command_effect_log(command_effect_page);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(command_effect_page, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return handle_result;
}
#endif

#if (Synology_case)
ddr_code static enum cmd_rslt_t nvmet_get_synology_smart(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	extern smart_statistics_t *smart_stat;
	extern synology_smart_statistics_t *synology_smart_stat;
	extern Ec_Table* EcTbl;

	struct nvme_synology_smart_page *synology_smart_pg;
	dtag_t *dtag;
	u32 i , ofst = 0;
	u32 transfer = sizeof(*synology_smart_pg) > bytes ? bytes : sizeof(*synology_smart_pg);;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	synology_smart_pg = dtag2mem(dtag[0]);
	memset(synology_smart_pg, 0x00, sizeof(*synology_smart_pg));

	/*Host UNC Error Count*/
	synology_smart_stat->host_UNC_error_cnt += host_unc_err_cnt;
	synology_smart_pg->host_UNC_error_cnt = synology_smart_stat->host_UNC_error_cnt;
	host_unc_err_cnt = 0;

	/*Average Erase Count*/
	synology_smart_stat->avg_erase_cnt = EcTbl->header.AvgEC;
	synology_smart_pg->avg_erase_cnt = synology_smart_stat->avg_erase_cnt;

	/*Max Erase Count*/
	synology_smart_stat->max_erase_cnt = EcTbl->header.MaxEC;
	synology_smart_pg->max_erase_cnt = synology_smart_stat->max_erase_cnt;
	//nvme_apl_trace(LOG_ALW, 0xfa5b,"maxxxxx:%d",EcTbl->header.MaxEC);

	/*Total Early Bad Block Count*/
	synology_smart_pg->total_early_bad_block_cnt = synology_smart_stat->total_early_bad_block_cnt;

	/*Total Later Bad Block Count*/
	synology_smart_pg->total_later_bad_block_cnt = synology_smart_stat->total_later_bad_block_cnt;

	/*NAND Write Sector*/
	shr_unit_byte_nw = shr_nand_bytes_written * 1024 *1024;
	synology_smart_stat->nand_write_sector[0] = (shr_unit_byte_nw >> 4);	//32Mib change to 512bytes unit
	synology_smart_stat->nand_write_sector[1] = ((shr_unit_byte_nw >> 4)) >> 16;
	synology_smart_stat->nand_write_sector[2] = ((shr_unit_byte_nw >> 4)) >> 32;
	synology_smart_stat->nand_write_sector[3] = ((shr_unit_byte_nw >> 4)) >> 48;
	synology_smart_pg->nand_write_sector[0] = synology_smart_stat->nand_write_sector[0];
	synology_smart_pg->nand_write_sector[1] = synology_smart_stat->nand_write_sector[1];
	synology_smart_pg->nand_write_sector[2] = synology_smart_stat->nand_write_sector[2];
	synology_smart_pg->nand_write_sector[3] = synology_smart_stat->nand_write_sector[3];

	/*Host Write Sector*/
	btn_smart_io_t wio;
	btn_get_w_smart_io(&wio);
	smart_stat->data_units_written += wio.host_du_cnt;
	synology_smart_stat->host_write_sector[0] = (smart_stat->data_units_written);
	synology_smart_stat->host_write_sector[1] = (smart_stat->data_units_written) >> 16;
	synology_smart_stat->host_write_sector[2] = (smart_stat->data_units_written) >> 32;
	synology_smart_stat->host_write_sector[3] = (smart_stat->data_units_written) >> 48;
	synology_smart_pg->host_write_sector[0] = synology_smart_stat->host_write_sector[0];
	synology_smart_pg->host_write_sector[1] = synology_smart_stat->host_write_sector[1];
	synology_smart_pg->host_write_sector[2] = synology_smart_stat->host_write_sector[2];
	synology_smart_pg->host_write_sector[3] = synology_smart_stat->host_write_sector[3];

	/*Data Read Retry Count*/
	synology_smart_pg->data_read_retry_cnt = synology_smart_stat->data_read_retry_cnt;

	/*RD_io count for Synology check*/
	extern btn_smart_io_t rd_io;
	synology_smart_pg->rd_io = rd_io.running_cmd;

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result == HANDLE_RESULT_DATA_XFER)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(synology_smart_pg, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
	else if (handle_result == HANDLE_RESULT_FAILURE)
	{
		//dtag_put(DTAG_T_SRAM, dtag[0]);
		//sys_free(SLOW_DATA, req->req_prp.mem);
		//sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}

	return handle_result;
}
#endif

#if CO_SUPPORT_OCP
ddr_code static enum cmd_rslt_t nvmet_get_ocp_smart(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	extern ocp_smart_statistics_t *ocp_smart_stat;
	extern tencnet_smart_statistics_t *tx_smart_stat;
	extern smart_statistics_t *smart_stat;
	extern Ec_Table* EcTbl;

	struct nvme_ocp_smart_page *ocp_smart_pg;
	dtag_t *dtag;
	u32 i , ofst = 0;
	u32 transfer = sizeof(*ocp_smart_pg) > bytes ? bytes : sizeof(*ocp_smart_pg);;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	ocp_smart_pg = dtag2mem(dtag[0]);
	memset(ocp_smart_pg, 0x00, sizeof(*ocp_smart_pg));

	//nvme_apl_trace(LOG_ALW, 0x4eb0,"1:%d 2:%d",sizeof(ocp_smart_info_t),sizeof(ocp_smart_pg->dssd_spec_version));

	/*Physical Media Units Written*/
	ocp_smart_pg->phy_media_units_written[0] = 0;

	/*Physical Media Units read*/
	ocp_smart_pg->phy_media_units_read[0] = 0;

	/*Bad User NAND Blocks*/
	ocp_smart_pg->bad_user_nand_blocks_normalized_value[0] = 0;
	ocp_smart_pg->bad_user_nand_blocks_raw_cnt[0] = 0;

	/*Bad System NAND Blocks*/
	ocp_smart_pg->bad_system_nand_blocks_normalized_value[0] = 0;
	ocp_smart_pg->bad_system_nand_blocks_raw_cnt[0] = 0;

	/*XOR Recovery Count*/
	ocp_smart_pg->uncorrect_read_err_cnt = 0;

	#if RAID_SUPPORT_UECC	
	/*Uncorrectable Read Error Count*/
	ocp_smart_stat->uncorrect_read_err_cnt = tx_smart_stat->uncorrectable_sector_count;
	ocp_smart_pg->uncorrect_read_err_cnt = ocp_smart_stat->uncorrect_read_err_cnt;
	#endif

	/*Soft ECC Error Count*/
	ocp_smart_pg->soft_ecc_err_cnt = 0;

	/*End to End Correction Counts*/
	ocp_smart_pg->end_to_end_correction_cnt[0] = 0;
	ocp_smart_pg->end_to_end_correction_cnt[1] = 0;

	/*System Data % Used*/
	ocp_smart_pg->system_data_used = 0;

	/*Refresh Counts*/
	ocp_smart_pg->refresh_cnt[0] = 0;

	/*User Data Erase Counts*/
	ocp_smart_pg->user_data_erase_cnt[0] = 0;
	ocp_smart_pg->user_data_erase_cnt[1] = 0;

	/*Thermal Throttling Status and Count*/
	ocp_smart_stat->thermal_throttling_status_and_cnt[0] = ts_tmt.gear;
	ocp_smart_pg->thermal_throttling_status_and_cnt[0] = ocp_smart_stat->thermal_throttling_status_and_cnt[0];
	ocp_smart_stat->thermal_throttling_status_and_cnt[1] = smart_stat->thermal_management_t1_trans_cnt + smart_stat->thermal_management_t2_trans_cnt;
	ocp_smart_pg->thermal_throttling_status_and_cnt[1] = ocp_smart_stat->thermal_throttling_status_and_cnt[1];

	/*DSSD Specification Version*/
	ocp_smart_pg->dssd_spec_version.major_version = 0x02;
	ocp_smart_pg->dssd_spec_version.minor_version = 0x0005;
	ocp_smart_pg->dssd_spec_version.point_version = 0x0000;
	ocp_smart_pg->dssd_spec_version.errata_version = 0x00;

	/*PCIe Correctable Error Count*/
	ocp_smart_pg->pcie_correctable_err_cnt = 0;

	/*Incomplete Shutdowns*/
	ocp_smart_pg->incomplete_shutdowns = 0;

	/*% Free Blocks*/
	ocp_smart_pg->free_blocks = 0;

	/*Capacitor Health*/
	ocp_smart_pg->capacitor_health[0] = 0;

	/*NVMe Errata Version*/
	ocp_smart_pg->nvme_errata_version = 0;

	/*NVMe Command Set Errata Version*/
	ocp_smart_pg->nvme_cmd_set_errata_version = 0;

	/*Unaligned I/O*/
	ocp_smart_pg->unaligned_io_cnt = 0;

	/*Security Version Number*/
	ocp_smart_pg->securiity_version_num = 0;

	/*Total NUSE*/
	ocp_smart_pg->total_nuse = ocp_smart_stat->total_nuse;

	/*PLP Start Count*/
	ocp_smart_pg->plp_start_cnt[0] = 0;

	/*Endurance Estimate*/
	ocp_smart_pg->endurance_estimate[0] = 0;

	/*PCIe Link Retraining Count*/
	ocp_smart_pg->pcie_link_retraining_cnt = 0;

	/*Power State Change Count*/
	ocp_smart_pg->power_state_change_cnt = 0;

	/*Lowesr Permitted Firmware Revision*/
	ocp_smart_pg->lowest_permitted_fw_revision = 0;

	/*Log Page Version*/
	ocp_smart_pg->log_page_version[0] = 0x04;
	ocp_smart_pg->log_page_version[1] = 0x00;

	/*Log Page GUID*/
	ocp_smart_pg->log_page_GUID[0] = 0xA4F2BFEA2810AFC5; 
	ocp_smart_pg->log_page_GUID[1] = 0xAFD514C97C6F4F9C;

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result == HANDLE_RESULT_DATA_XFER)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(ocp_smart_pg, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
	else if (handle_result == HANDLE_RESULT_FAILURE)
	{
		//dtag_put(DTAG_T_SRAM, dtag[0]);
		//sys_free(SLOW_DATA, req->req_prp.mem);
		//sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}

	return handle_result;
}
#endif


#if CO_SUPPORT_SANITIZE
fast_data_zi volatile bool sanitize_not_break; //for sanitize op over powercycle when status: sSanitize_FW_InProgress
fast_data_zi u64 sanitize_start_timestamp;
//fast_data_zi u32 sanitize_time_period;
ddr_code enum cmd_rslt_t nvmet_get_lp_sanitize(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	struct LogPageSanitizeStatus_t *sanitize_page = NULL;
	u32 i = 0, ofst = 0;
	u32 transfer = sizeof(*sanitize_page) > bytes ? bytes : sizeof(*sanitize_page);
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	sanitize_page = dtag2mem(dtag[0]);
	memset(sanitize_page, 0x00, sizeof(*sanitize_page));

	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	if(epm_smart_data->sanitizeInfo.sanitize_Tag != 0xDEADFACE)
	{
		epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_None;
		//epm_smart_data->sanitizeInfo.sanitize_log_page &= ~(0xFF0000);
		//epm_smart_data->sanitizeInfo.sanitize_log_page |= (0xFFFF);
		epm_smart_data->sanitizeInfo.sanitize_log_page = 0xFFFF;
		epm_smart_data->sanitizeInfo.handled_wr_cmd_cnt = 0;
		epm_smart_data->sanitizeInfo.bmp_w_cmd_sanitize = 0;
		epm_smart_data->sanitizeInfo.sanitize_Tag = 0xDEADFACE;
	}

	sanitize_log_page_t sanitize_lp = {.all = (epm_smart_data->sanitizeInfo.sanitize_log_page)};

	// report est time
	// Blk erase for PJ1-1920 & more
	sanitize_page->EstTimeBlkErase      = EST_TIMECOST_SANITIZE_BLK_ERASE+((shr_nand_info.interleave-1)/64);
	sanitize_page->EstTimeCyptErase     = EST_TIMECOST_SANITIZE_CRY_ERASE;
	sanitize_page->EstTimeOverwrite     = 0xFFFFFFFF;   //no time period is reported
	sanitize_page->EstTimeBlkEraseNDMM  = 0xFFFFFFFF;   //no time period is reported
	sanitize_page->EstTimeCyptEraseNDMM = 0xFFFFFFFF;   //no time period is reported
	sanitize_page->EstTimeOverwriteNDMM = 0xFFFFFFFF;   //no time period is reported

	if(sanitize_lp.b.SanitizeStatus == sSanitizeInProgress)
	{
#if 0
		u64 timeProgress = (time_elapsed_in_ms(sanitize_start_timestamp)/1000)*0x1000;
		if(timeProgress > 0xFFFF)
			sanitize_lp.b.SPROG = 0xF000;
		else
			sanitize_lp.b.SPROG = (u16)timeProgress;
#else
		if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_Completed)
			sanitize_lp.b.SPROG = 0xFFFE;
		else if((!sanitize_not_break) || (epm_smart_data->sanitizeInfo.fwSanitizeProcessStates < sSanitize_FW_Completed))
			sanitize_lp.b.SPROG = 0;
		else //if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_InProgress)
		{
			u32 *est_time_cost = NULL;
			switch(sanitize_lp.b.DW10.b.SANACT)
			{
				case cStart_Block_Erase:
					est_time_cost = &(sanitize_page->EstTimeBlkErase);
					break;
				case cStart_Crypto_Erase:
					est_time_cost = &(sanitize_page->EstTimeCyptErase);
					break;
				default:
					break;
			}
			sys_assert(est_time_cost);
			u32 timecost = time_elapsed_in_ms(sanitize_start_timestamp);
			u32 SPROG = (0xFFFF/(*est_time_cost<<10))*timecost;
			if(SPROG<0xFFFD)
				sanitize_lp.b.SPROG = (u16)SPROG;
			else
			{
				sanitize_lp.b.SPROG = 0xFFFD;
				//*est_time_cost = (timecost>>10)+((timecost&0x200)?1:0);
			}
		}
#endif
		nvme_apl_trace(LOG_INFO, 0xb812, "SPROG in Log Page: 0x%x", sanitize_lp.b.SPROG);
	}
	else
		sanitize_lp.b.SPROG = 0xFFFF;

	extern smart_statistics_t *smart_stat;
	btn_smart_io_t wio;
	btn_get_w_smart_io(&wio);
	smart_stat->data_units_written += wio.host_du_cnt;
	smart_stat->host_write_commands += wio.cmd_recv_cnt;
	if((sanitize_lp.b.SanitizeStatus != sSanitizeInProgress) && (sanitize_lp.b.SanitizeStatus != sSanitizeCompletedFail))
		sanitize_lp.b.GlobalDataErased = (epm_smart_data->sanitizeInfo.handled_wr_cmd_cnt == smart_stat->host_write_commands)
											&& (epm_smart_data->sanitizeInfo.bmp_w_cmd_sanitize == 0);
	epm_smart_data->sanitizeInfo.sanitize_log_page = sanitize_lp.all;
	epm_update(SMART_sign, (CPU_ID - 1));
	sanitize_page->SPROG = sanitize_lp.b.SPROG;
	sanitize_page->SanitizeStatus = sanitize_lp.b.SanitizeStatus;
	sanitize_page->OverwritePassNunber = sanitize_lp.b.OverwritePassNunber;
	sanitize_page->GlobalDataErased = sanitize_lp.b.GlobalDataErased;
	sanitize_page->SCDW10 = sanitize_lp.b.DW10.all;

#if 0
	if(epm_smart_data->sanitizeInfo.last_time_cost)
	{
		if((sanitize_lp.b.SanitizeStatus == sSanitizeCompleted) && sanitize_lp.b.DW10.b.NDAS)
		{
			if(sanitize_lp.b.DW10.b.SANACT == 2)
			{
				// Round Up
				//sanitize_page->EstTimeBlkEraseNDMM = (u32)occupied_by(epm_smart_data->sanitizeInfo.last_time_cost,1000);
				// Round Down
				sanitize_page->EstTimeBlkEraseNDMM = (u32)(epm_smart_data->sanitizeInfo.last_time_cost>>10);
			}
			else if(sanitize_lp.b.DW10.b.SANACT == 3)
			{
				sanitize_page->EstTimeOverwriteNDMM = (u32)(epm_smart_data->sanitizeInfo.last_time_cost>>10);
			}
			else if(sanitize_lp.b.DW10.b.SANACT == 4)
			{
				sanitize_page->EstTimeCyptEraseNDMM = (u32)(epm_smart_data->sanitizeInfo.last_time_cost>>10);
			}
		}
		else if((sanitize_lp.b.SanitizeStatus == sSanitizeCompletedNDI) || (sanitize_lp.b.SanitizeStatus == sSanitizeCompleted))
		{
			if(sanitize_lp.b.DW10.b.SANACT == 2)
			{
				sanitize_page->EstTimeBlkErase = (u32)(epm_smart_data->sanitizeInfo.last_time_cost>>10);
			}
			else if(sanitize_lp.b.DW10.b.SANACT == 3)
			{
				sanitize_page->EstTimeOverwrite = (u32)(epm_smart_data->sanitizeInfo.last_time_cost>>10);
			}
			else if(sanitize_lp.b.DW10.b.SANACT == 4)
			{
				sanitize_page->EstTimeCyptErase = (u32)(epm_smart_data->sanitizeInfo.last_time_cost>>10);
			}
		}
	}
#endif

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(sanitize_page, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return handle_result;
}
#endif

#if CO_SUPPORT_DEVICE_SELF_TEST

//-----------------------------------------------------------------------------
/**
    Get log page - Command Effects Log

    @param[in]  pLogPage   log page buffer pointer
**/
//-----------------------------------------------------------------------------
ddr_code static void GetLogPageDeviceSelfTestLog(LogPageDeviceSelfTestEntry_t* pLogPage)
{
	// TODO
	pLogPage->Operation  = gCurrDSTOperation;
    pLogPage->Completion = gCurrDSTCompletion;
	pLogPage->reserved2 = 0 ;

	extern tDST_LOG *smDSTInfo;

    memcpy((u8*)&pLogPage->DSTResultData[0], (u8*)&smDSTInfo->DSTLogEntry[0], sizeof(tDST_LOG_ENTRY) * 20);

	nvme_apl_trace(LOG_INFO, 0x09e9, "pLogPage->DSTResultData[0].StatusCode|%x CodeType|%x ValidDiagnosticInfo|%x",pLogPage->DSTResultData[0].StatusCode, pLogPage->DSTResultData[0].CodeType ,pLogPage->DSTResultData[0].ValidDiagnosticInfo);

}


ddr_code static enum cmd_rslt_t nvmet_get_lp_command_dev_self_test(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	struct LogPageDeviceSelfTestEntry_t *pLogPage = NULL;
	u32 i = 0;
	u32 ofst = 0;
	u32 transfer = sizeof(LogPageDeviceSelfTestEntry_t) > bytes ? bytes : sizeof(LogPageDeviceSelfTestEntry_t);
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	pLogPage = dtag2mem(dtag[0]);
	//memset(pLogPage, 0x5a, sizeof(LogPageDeviceSelfTestEntry_t));

	GetLogPageDeviceSelfTestLog((LogPageDeviceSelfTestEntry_t*)pLogPage);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)pLogPage + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return handle_result;
}
#endif

ddr_code void ipc_get_new_ec_smart_done(volatile cpu_msg_req_t *msg_req)
{

	struct ex_health_ipc *sm_ex_hlt = (struct ex_health_ipc *)msg_req->pl;
	if (is_ptr_tcm_share((void*)sm_ex_hlt))
		sm_ex_hlt = (struct ex_health_ipc*)tcm_share_to_local((void*)sm_ex_hlt);
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_GET_PROGRAM_AND_ERASE_FAIL_COUNT, 0, (u32)sm_ex_hlt);
}


/*!
 * @brief return Additional SMART information
 *
 * @param req		request
 * @param cmd		NVM command
 * @param bytes		how many bytes Host requests
 *
 * @return		command status, always data transferring
 */
#if (Add_smart)
  #ifndef Xfusion_case
static enum cmd_rslt_t nvmet_get_additional_smart(
	req_t *req, struct nvme_cmd *cmd, u16 bytes)
{

	u32 nsid = cmd->nsid;
    nvme_apl_trace(LOG_DEBUG, 0x9a8f, "get lp health nsid:%d",nsid);
	// PCBasher full random
	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	struct ex_health_ipc *ex_hlt = sys_malloc(SLOW_DATA, sizeof(ex_health_ipc));
	ex_hlt->cmdreq = (u32 *)req;
	ex_hlt->bytes = sizeof(struct nvme_additional_health_information_page) > bytes ? bytes : sizeof(struct nvme_additional_health_information_page);
	//nvme_apl_trace(LOG_INFO, 0, "size:%d", sizeof(struct nvme_additional_health_information_page));
	nvmet_alloc_admin_res(req, ex_hlt->bytes);
	//cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GET_ADDITIONAL_SMART_INFO, 0, (u32)ex_hlt);
	Get_EC_Table(2, (u32)ex_hlt);
//	cpu_msg_issue(CPU_BE - 1, CPU_MSG_GET_PROGRAM_AND_ERASE_FAIL_COUNT, 0, (u32)ex_hlt);
	return HANDLE_RESULT_RUNNING;
}
  #endif
#endif

ddr_code void ipc_get_additional_smart_info_done(volatile cpu_msg_req_t *msg_req)
{
	extern smart_statistics_t *smart_stat;
	extern tencnet_smart_statistics_t *tx_smart_stat;
	extern Ec_Table* EcTbl;

	struct ex_health_ipc *ex_hlt = (struct ex_health_ipc *)msg_req->pl;
	struct nvme_additional_health_information_page *ex_smart_pg;
	dtag_t *dtag;
	req_t *req = ex_hlt->cmdreq;
	u32 i;
	u32 ofst = 0;
	struct nvme_cmd *cmd = req->host_cmd;
	u32 transfer = ex_hlt->bytes;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	// DBG, SMARTVry
	#if RAID_SUPPORT_UECC
	nvme_apl_trace(LOG_INFO, 0x81ae, "[DBG] Rd(TotDet)[%d] Hst(Det/Unc)[%d/%d] Int(Det/Unc)[%d/%d]", \
		tx_smart_stat->nand_ecc_detection_count, tx_smart_stat->host_uecc_detection_cnt, tx_smart_stat->uncorrectable_sector_count);
	nvme_apl_trace(LOG_INFO, 0xf1b8, "[DBG] Int(Det/Unc)[%d/%d]", internal_uecc_detection_cnt, tx_smart_stat->raid_recovery_fail_count);
    #endif
    nvme_apl_trace(LOG_INFO, 0x6746, "[DBG] (Pg/Er)[%d/%d]", tx_smart_stat->program_fail_count, tx_smart_stat->erase_fail_count);


	//nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	ex_smart_pg = dtag2mem(dtag[0]);
	memset(ex_smart_pg, 0, sizeof(*ex_smart_pg));


//	get_avg_erase_cnt(&EcTbl->header.AvgEC, &EcTbl->header.MaxEC, &EcTbl->header.MinEC, &EcTbl->header.TotalEC);

	shr_avg_erase = EcTbl->header.AvgEC;//ex_hlt->avg_erase;
	shr_max_erase = EcTbl->header.MaxEC;//ex_hlt->max_erase;
	shr_min_erase = EcTbl->header.MinEC;//ex_hlt->min_erase;
	shr_total_ec = EcTbl->header.TotalEC;//ex_hlt->total_ec;

	sys_free(SLOW_DATA, ex_hlt);
	//Get Program Fail Count and Erase Fail Count
	//get_program_and_erase_fail_count();//1

	//u32 endurance = 114 *  shr_total_die; // TSB Spec, refer to CA6
	nvme_apl_trace(LOG_INFO, 0x23e1, "res blk:%d", resver_blk);

	//tx_smart_stat->program_fail_count[0] = shr_program_fail_count;
	//tx_smart_stat->program_fail_count[1] = shr_program_fail_count >> 16;
	ex_smart_pg->program_fail_count = 0xAB;
	//ex_smart_pg->program_fail_count_normalized_value = 100 - (shr_program_fail_count * 100 / resver_blk);	 //beginning at 100,shows the percent remaining of allowable program fails.	// ISU, EHSmartNorValAdj
	if (shr_program_fail_count >= resver_blk)
		ex_smart_pg->program_fail_count_normalized_value = 0;
	else
		ex_smart_pg->program_fail_count_normalized_value = 100 - (shr_program_fail_count * 100 / resver_blk);
	ex_smart_pg->program_fail_count_current_raw_value = tx_smart_stat->program_fail_count;
	//ex_smart_pg->program_fail_count_current_raw_value[1] = tx_smart_stat->program_fail_count[1];

	//tx_smart_stat->erase_fail_count[0] = shr_erase_fail_count;
	//tx_smart_stat->erase_fail_count[1] = shr_erase_fail_count >> 16;
	ex_smart_pg->erase_fail_count = 0xAC;
	//ex_smart_pg->erase_fail_count_normalized_value = 100 - (shr_erase_fail_count * 100 / resver_blk); //beginning at 100,shows the percent remaining of allowable erase fails.	// ISU, EHSmartNorValAdj
	if (shr_erase_fail_count >= resver_blk)
		ex_smart_pg->erase_fail_count_normalized_value = 0;
	else
		ex_smart_pg->erase_fail_count_normalized_value = 100 - (shr_erase_fail_count * 100 / resver_blk);

	//ex_smart_pg->erase_fail_count_normalized_value = 100; //Temporarily changed to 100
	ex_smart_pg->erase_fail_count_current_raw_value = tx_smart_stat->erase_fail_count;
	//ex_smart_pg->erase_fail_count_current_raw_value[1] = tx_smart_stat->erase_fail_count[1];

	//Wear Leveling Count
	// get_avg_erase_cnt(&shr_avg_erase, &shr_max_erase, &shr_min_erase, &shr_total_ec);
	ex_smart_pg->wear_leveling_count =0xAD;
	ex_smart_pg->wear_leveling_count_normalized_value = 100 - (shr_avg_erase * 100 / MAX_AVG_EC); //decrements from 100 to 0, DBG, SMARTVry
	if((shr_avg_erase * 100 / MAX_AVG_EC)>100)
    {
        ex_smart_pg->wear_leveling_count_normalized_value = 0;
    }
    ex_smart_pg->wear_leveling_count_current_raw_value[0] = (u16)shr_min_erase;
	ex_smart_pg->wear_leveling_count_current_raw_value[1] = (u16)shr_max_erase;
	ex_smart_pg->wear_leveling_count_current_raw_value[2] = (u16)shr_avg_erase < (u16)shr_min_erase ? (u16)shr_min_erase : (u16)shr_avg_erase;
	tx_smart_stat->wear_levelng_count[0] = ex_smart_pg->wear_leveling_count_current_raw_value[0];

	//End to End Detection Count
	//tx_smart_stat->end_to_end_detection_count[0] += shr_E2E_GuardTag_detection_count;
	//tx_smart_stat->end_to_end_detection_count[1] += shr_E2E_AppTag_detection_count;
	//tx_smart_stat->end_to_end_detection_count[2] += shr_E2E_RefTag_detection_count;
	ex_smart_pg->end_to_end_error_detection_count = 0xB8;
	ex_smart_pg->end_to_end_error_detection_count_normalized_value = 100; //Always 100
	ex_smart_pg->end_to_end_error_detection_count_current_raw_value[0] = tx_smart_stat->end_to_end_detection_count[0];
	ex_smart_pg->end_to_end_error_detection_count_current_raw_value[1] = tx_smart_stat->end_to_end_detection_count[1];
	ex_smart_pg->end_to_end_error_detection_count_current_raw_value[2] = tx_smart_stat->end_to_end_detection_count[2];
	//shr_E2E_GuardTag_detection_count= 0;	// CPU1 access var, should be ok to clear directly, DBG, SMARTVry
	//shr_E2E_AppTag_detection_count	= 0;
	//shr_E2E_RefTag_detection_count	= 0;

	//CRC Error Count
	//if ((tx_smart_stat->crc_error_count[0] + (corr_err_cnt.bad_tlp_cnt & 0xFFFF)) > 0xFFFF)	// Handle carry in
	//	tx_smart_stat->crc_error_count[1] += 1;
	//tx_smart_stat->crc_error_count[0] += (u16)corr_err_cnt.bad_tlp_cnt;
	//tx_smart_stat->crc_error_count[1] += (u16)(corr_err_cnt.bad_tlp_cnt >> 16);
	ex_smart_pg->crc_error_count = 0xC7;
	ex_smart_pg->crc_error_count_normalized_value = 100; //Always 100
	ex_smart_pg->crc_error_count_current_raw_value = tx_smart_stat->crc_error_count;
	//ex_smart_pg->crc_error_count_current_raw_value[1] = tx_smart_stat->crc_error_count[1];
	//corr_err_cnt.bad_tlp_cnt = 0;	// CPU1 access var, should be ok to clear directly, DBG, SMARTVry

	//NAND Bytes Written
	// DBG, SMARTVry
	//tx_smart_stat->nand_bytes_written[0] += shr_nand_bytes_written;
	//tx_smart_stat->nand_bytes_written[1] += shr_nand_bytes_written >> 16;
	//tx_smart_stat->nand_bytes_written[0] = shr_nand_bytes_written;
	//tx_smart_stat->nand_bytes_written[1] = shr_nand_bytes_written >> 16;
	shr_unit_byte_nw = shr_nand_bytes_written * 1024 *1024;
	tx_smart_stat->nand_bytes_written[0] = shr_unit_byte_nw>>ctz(0x100000);		//MB to MiB
	tx_smart_stat->nand_bytes_written[1] = (shr_unit_byte_nw>>ctz(0x100000)) >> 16;
	tx_smart_stat->nand_bytes_written[2] = (shr_unit_byte_nw>>ctz(0x100000)) >> 32;
	ex_smart_pg->nand_bytes_written = 0xF4;
	ex_smart_pg->nand_bytes_written_normalized_value = 100; //Always 100
	//ex_smart_pg->nand_bytes_written_current_value = tx_smart_stat->nand_bytes_written;
	ex_smart_pg->nand_bytes_written_current_value[0] = tx_smart_stat->nand_bytes_written[0];
	ex_smart_pg->nand_bytes_written_current_value[1] = tx_smart_stat->nand_bytes_written[1];
	ex_smart_pg->nand_bytes_written_current_value[2] = tx_smart_stat->nand_bytes_written[2];

	//Host Bytes Written
	//btn_smart_io_t wio;
	//btn_get_w_smart_io(&wio);
	//smart_stat->data_units_written += wio.host_du_cnt;
	//smart_stat->host_write_commands += wio.cmd_recv_cnt;

	health_get_inflight_command(ex_smart_pg);
	tx_smart_stat->host_bytes_written[0] = (smart_stat->data_units_written)>>ctz(0x10000);	// 512 Bytes to Unit is 32MB
	tx_smart_stat->host_bytes_written[1] = (smart_stat->data_units_written)>>ctz(0x10000)>>16;
	tx_smart_stat->host_bytes_written[2] = (smart_stat->data_units_written)>>ctz(0x10000)>>32;
	ex_smart_pg->host_bytes_written = 0xF5;
	ex_smart_pg->host_bytes_written_normalized_value = 100;//Always 100
	ex_smart_pg->host_bytes_written_current_value[0] = tx_smart_stat->host_bytes_written[0];
	ex_smart_pg->host_bytes_written_current_value[1] = tx_smart_stat->host_bytes_written[1];
	ex_smart_pg->host_bytes_written_current_value[2] = tx_smart_stat->host_bytes_written[2];

#if RAID_SUPPORT_UECC
	//Reallocated Sector Count
	u32 nand_reallocated_sector_cnt = host_prog_fail_cnt + host_uecc_detection_cnt;    //host cnt (read+prog)
	// DBG, SMARTVry
	tx_smart_stat->reallocated_sector_count = tx_smart_stat->program_fail_count + tx_smart_stat->host_uecc_detection_cnt;    //host cnt (read+prog)
	//tx_smart_stat->reallocated_sector_count[0] += nand_reallocated_sector_cnt;
	//tx_smart_stat->reallocated_sector_count[1] += nand_reallocated_sector_cnt>>16;
	//tx_smart_stat->reallocated_sector_count[0] = nand_reallocated_sector_cnt;
	//tx_smart_stat->reallocated_sector_count[1] = nand_reallocated_sector_cnt>>16;
	ex_smart_pg->reallocated_sector_count = 0x05;
	//ex_smart_pg->reallocated_sector_count_normalized_value = 100 - (nand_reallocated_sector_cnt * 100/resver_blk);		// ISU, EHSmartNorValAdj
	if (nand_reallocated_sector_cnt >= resver_blk)
		ex_smart_pg->reallocated_sector_count_normalized_value = 0;
	else
		ex_smart_pg->reallocated_sector_count_normalized_value = 100 - (nand_reallocated_sector_cnt * 100 / resver_blk);
	ex_smart_pg->reallocated_sector_count_current_value = tx_smart_stat->reallocated_sector_count;
	//ex_smart_pg->reallocated_sector_count_current_value[1] = tx_smart_stat->reallocated_sector_count[1];

	//Uncorrectable Sector Count for host data recv result (UECC detect or prog failed)
	// DBG, SMARTVry
	//tx_smart_stat->uncorrectable_sector_count[0] += uncorrectable_sector_count;             //host cnt
	//tx_smart_stat->uncorrectable_sector_count[1] += uncorrectable_sector_count>>16;
	//tx_smart_stat->uncorrectable_sector_count[0] = uncorrectable_sector_count;
	//tx_smart_stat->uncorrectable_sector_count[1] = uncorrectable_sector_count>>16;
	ex_smart_pg->uncorrectable_sector_count = 0xC6;
	ex_smart_pg->uncorrectable_sector_count_normalized_value = 100;//Always 100
	ex_smart_pg->uncorrectable_sector_count_current_value = tx_smart_stat->uncorrectable_sector_count;
	//ex_smart_pg->uncorrectable_sector_count_current_value[1] = tx_smart_stat->uncorrectable_sector_count[1];

	//NAND ECC Detection Count for host data and internal data.
	// DBG, SMARTVry
	//tx_smart_stat->nand_ecc_detection_count[0] += nand_ecc_detection_cnt;                   //host + internal cnt
	//tx_smart_stat->nand_ecc_detection_count[1] += nand_ecc_detection_cnt>>16;
	//tx_smart_stat->nand_ecc_detection_count[0] = nand_ecc_detection_cnt;
	//tx_smart_stat->nand_ecc_detection_count[1] = nand_ecc_detection_cnt>>16;
	ex_smart_pg->nand_ecc_detection_count = 0xE1;
	ex_smart_pg->nand_ecc_detection_count_normalized_value = 100;//Always 100
	ex_smart_pg->nand_ecc_detection_count_current_value = tx_smart_stat->nand_ecc_detection_count;
	//ex_smart_pg->nand_ecc_detection_count_current_value[1] = tx_smart_stat->nand_ecc_detection_count[1];

	//NAND ECC Correction Count for host data
	tx_smart_stat->nand_ecc_correction_count = tx_smart_stat->reallocated_sector_count - tx_smart_stat->uncorrectable_sector_count;
	// DBG, SMARTVry
	//tx_smart_stat->nand_ecc_correction_count[0] += nand_ecc_correction_cnt;
	//tx_smart_stat->nand_ecc_correction_count[1] += nand_ecc_correction_cnt>>16;
	//tx_smart_stat->nand_ecc_correction_count = nand_ecc_correction_cnt;
	//tx_smart_stat->nand_ecc_correction_count[1] = nand_ecc_correction_cnt>>16;
	ex_smart_pg->nand_ecc_correction_count = 0xE2;
	ex_smart_pg->nand_ecc_correction_count_normalized_value = 100;//Always 100
	ex_smart_pg->nand_ecc_correction_count_current_value = tx_smart_stat->nand_ecc_correction_count;
	//ex_smart_pg->nand_ecc_correction_count_current_value[1] = tx_smart_stat->nand_ecc_correction_count[1];

    //Internal Raid Recovery Fail Count
    // DBG, SMARTVry
	//tx_smart_stat->raid_recovery_fail_count[0] += internal_rc_fail_cnt;                     //internal cnt
	//tx_smart_stat->raid_recovery_fail_count[1] += internal_rc_fail_cnt>>16;
	//tx_smart_stat->raid_recovery_fail_count[0] = internal_rc_fail_cnt;
	//tx_smart_stat->raid_recovery_fail_count[1] = internal_rc_fail_cnt>>16;
	ex_smart_pg->raid_recovery_fail_count = 0xFF;	// TBC, which id shold be used
	ex_smart_pg->raid_recovery_fail_count_normalized_value = 100;//Always 100
	ex_smart_pg->raid_recovery_fail_count_current_value = tx_smart_stat->raid_recovery_fail_count;
	//ex_smart_pg->raid_recovery_fail_count_current_value[1] = tx_smart_stat->raid_recovery_fail_count[1];
#endif

	//Bad Block Failure Rate
	// Need not maintain this ID, DBG, SMARTVry
	//tx_smart_stat->bad_block_failure_rate[0] += GrowPhyDefectCnt;
	//tx_smart_stat->bad_block_failure_rate[1] += GrowPhyDefectCnt>>16;
	//tx_smart_stat->bad_block_failure_rate[0] = GrowPhyDefectCnt;
	//tx_smart_stat->bad_block_failure_rate[1] = GrowPhyDefectCnt>>16;
	ex_smart_pg->bad_block_failure_rate = 0xB1;
	ex_smart_pg->bad_block_failure_rate_normalized_value = 100;//Always 100
	ex_smart_pg->bad_block_failure_rate_current_value = tx_smart_stat->bad_block_failure_rate;
	//ex_smart_pg->bad_block_failure_rate_current_value[1] = tx_smart_stat->bad_block_failure_rate[1];

	health_get_gc_count(ex_smart_pg);

	ex_smart_pg->dram_uecc_detection_count = 0xDD;
	ex_smart_pg->dram_uecc_detection_count_normalized_value = 100;//Always 100
	ex_smart_pg->dram_uecc_detection_count_current_value[0] = tx_smart_stat->dram_error_count[0];
	ex_smart_pg->dram_uecc_detection_count_current_value[1] = tx_smart_stat->dram_error_count[1];
	ex_smart_pg->dram_uecc_detection_count_current_value[2] = tx_smart_stat->dram_error_count[2];

	ex_smart_pg->sram_uecc_detection_count = 0xDE;
	ex_smart_pg->sram_uecc_detection_count_normalized_value = 100;//Always 100
	ex_smart_pg->sram_uecc_detection_count_current_value[0] = tx_smart_stat->sram_error_count[0];
	ex_smart_pg->sram_uecc_detection_count_current_value[1] = tx_smart_stat->sram_error_count[1];
	ex_smart_pg->sram_uecc_detection_count_current_value[2] = tx_smart_stat->sram_error_count[2];

	ex_smart_pg->pcie_correctable_error_count = 0xDF;
	ex_smart_pg->pcie_correctable_error_count_normalized_value = 100;//Always 100
	ex_smart_pg->pcie_correctable_error_count_current_value[0] = tx_smart_stat->pcie_correctable_error_count[0];
	ex_smart_pg->pcie_correctable_error_count_current_value[1] = tx_smart_stat->pcie_correctable_error_count[1];
	ex_smart_pg->pcie_correctable_error_count_current_value[2] = tx_smart_stat->pcie_correctable_error_count[2];

#if (GET_PCIE_ERR == DISABLE)
	ex_smart_pg->pcie_correctable_error_count_current_value[0] = 0;
	ex_smart_pg->pcie_correctable_error_count_current_value[1] = 0;
	ex_smart_pg->pcie_correctable_error_count_current_value[2] = 0;
#endif

	//Internal End to End Detection Count
	health_get_hcrc_detection_count(ex_smart_pg);	// hcrc_detection_count = 0xFB, DBG, SMARTVry

	//tx_smart_stat->die_fail_count[0] = shr_die_fail_count;
	//tx_smart_stat->die_fail_count[1] = shr_die_fail_count >> 16;
	ex_smart_pg->die_fail_count = 0xFC;
	ex_smart_pg->die_fail_count_normalized_value = 100; //Temporarily changed to 100
	ex_smart_pg->die_fail_count_current_value = tx_smart_stat->die_fail_count;
	//ex_smart_pg->die_fail_count_current_value[1] = tx_smart_stat->die_fail_count[1];

#ifdef SMART_PLP_NOT_DONE
	ex_smart_pg->plp_not_done_sign = 0x99;
	ex_smart_pg->plp_not_done_cnt = tx_smart_stat->plp_not_done_cnt;
#endif

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(ex_smart_pg, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
	else if (handle_result == HANDLE_RESULT_FAILURE)
	{
		//dtag_put(DTAG_T_SRAM, dtag[0]);
		//sys_free(SLOW_DATA, req->req_prp.mem);
		//sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}

/*!
 * @brief return Additional Intel SMART information for Xfusion case
 *
 * @param req		request
 * @param cmd		NVM command
 * @param bytes		how many bytes Host requests
 *
 * @return		command status, always data transferring
 */

#if (Xfusion_case)
ddr_code enum cmd_rslt_t nvmet_get_intel_additional_smart(
	req_t *req, struct nvme_cmd *cmd, u16 bytes)
{

	u32 nsid = cmd->nsid;
    nvme_apl_trace(LOG_DEBUG, 0x0b16, "get intel smart nsid:%d",nsid);
	// PCBasher full random
	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	struct ex_health_ipc *ex_hlt = sys_malloc(SLOW_DATA, sizeof(ex_health_ipc));
    sys_assert(ex_hlt != NULL);
	ex_hlt->cmdreq = (u32 *)req;
	ex_hlt->bytes = sizeof(struct nvme_additional_health_information_page) > bytes ? bytes : sizeof(struct nvme_additional_health_information_page);
	//nvme_apl_trace(LOG_INFO, 0, "size:%d", sizeof(struct nvme_additional_health_information_page));
	nvmet_alloc_admin_res(req, ex_hlt->bytes);
	//cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GET_ADDITIONAL_SMART_INFO, 0, (u32)ex_hlt);
	Get_EC_Table(2, (u32)ex_hlt);
//	cpu_msg_issue(CPU_BE - 1, CPU_MSG_GET_PROGRAM_AND_ERASE_FAIL_COUNT, 0, (u32)ex_hlt);
	return HANDLE_RESULT_RUNNING;
}


ddr_code void ipc_get_intel_smart_info_done(volatile cpu_msg_req_t *msg_req)
{
	extern smart_statistics_t *smart_stat;
	extern tencnet_smart_statistics_t *tx_smart_stat;
	extern Ec_Table* EcTbl;

	struct ex_health_ipc *ex_hlt = (struct ex_health_ipc *)msg_req->pl;
	struct nvme_intel_smart_page *intel_smart_page;
	dtag_t *dtag;
	req_t *req = ex_hlt->cmdreq;
	u32 i;
	u32 ofst = 0;
	struct nvme_cmd *cmd = req->host_cmd;
	u32 transfer = ex_hlt->bytes;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;


	//nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	intel_smart_page = dtag2mem(dtag[0]);
	memset(intel_smart_page, 0, sizeof(*intel_smart_page));

	shr_avg_erase = EcTbl->header.AvgEC;//ex_hlt->avg_erase;
	shr_max_erase = EcTbl->header.MaxEC;//ex_hlt->max_erase;
	shr_min_erase = EcTbl->header.MinEC;//ex_hlt->min_erase;

	sys_free(SLOW_DATA, ex_hlt);

	/*Program fail count*/
	intel_smart_page->program_fail_cnt.key = 0xAB;
	if (shr_program_fail_count >= resver_blk){
		intel_smart_page->program_fail_cnt.norm = 0;
	}
	else{
		intel_smart_page->program_fail_cnt.norm = 100 - (shr_program_fail_count * 100 / resver_blk);
	}
	intel_smart_page->program_fail_cnt.raw[0] = tx_smart_stat->program_fail_count;
	intel_smart_page->program_fail_cnt.raw[1] = tx_smart_stat->program_fail_count >> 16;

	/*Erase fail count*/
	intel_smart_page->erase_fail_cnt.key = 0xAC;
	if (shr_erase_fail_count >= resver_blk){
		intel_smart_page->erase_fail_cnt.norm = 0;
	}
	else{
		intel_smart_page->erase_fail_cnt.norm = 100 - (shr_erase_fail_count * 100 / resver_blk);
	}
	intel_smart_page->erase_fail_cnt.raw[0] = tx_smart_stat->erase_fail_count;
	intel_smart_page->erase_fail_cnt.raw[1] = tx_smart_stat->erase_fail_count >> 16;

	/*Wear Leveling Count*/
	intel_smart_page->wear_leveling_cnt.key = 0xAD;
	if((shr_avg_erase * 100 / MAX_AVG_EC)>100)
    {
        intel_smart_page->wear_leveling_cnt.norm = 0;
    }
	else{
		intel_smart_page->wear_leveling_cnt.norm = 100 - (shr_avg_erase * 100 / MAX_AVG_EC); //decrements from 100 to 0, DBG, SMARTVry
	}
    intel_smart_page->wear_leveling_cnt.wear_level.min = (u16)shr_min_erase;
	intel_smart_page->wear_leveling_cnt.wear_level.max = (u16)shr_max_erase;
	intel_smart_page->wear_leveling_cnt.wear_level.avg = (u16)shr_avg_erase < (u16)shr_min_erase ? (u16)shr_min_erase : (u16)shr_avg_erase;
	tx_smart_stat->wear_levelng_count[0] = intel_smart_page->wear_leveling_cnt.wear_level.min;

	/*End to End Detection Count*/
	intel_smart_page->e2e_err_cnt.key = 0xB8;
	intel_smart_page->e2e_err_cnt.norm = 100; //Always 100
	intel_smart_page->e2e_err_cnt.raw[0] = tx_smart_stat->end_to_end_detection_count[0];
	intel_smart_page->e2e_err_cnt.raw[1] = tx_smart_stat->end_to_end_detection_count[1];
	intel_smart_page->e2e_err_cnt.raw[2] = tx_smart_stat->end_to_end_detection_count[2];

	/*CRC Error Count*/
	intel_smart_page->crc_err_cnt.key = 0xC7;
	intel_smart_page->crc_err_cnt.norm = 100; //Always 100
	intel_smart_page->crc_err_cnt.raw[0] = tx_smart_stat->crc_error_count;
	intel_smart_page->crc_err_cnt.raw[1] = tx_smart_stat->crc_error_count >>16;

	/*Timed workload media wear*/ //Not support
	intel_smart_page->timed_workload_media_wear.key = 0xE2;
	intel_smart_page->timed_workload_media_wear.norm = 0;
	intel_smart_page->timed_workload_media_wear.raw[0] = 0;

	/*Timed workload host reads*/ //Not support
	intel_smart_page->timed_workload_host_reads.key = 0xE3;
	//intel_smart_page->timed_workload_host_reads.norm = 0;
	intel_smart_page->timed_workload_host_reads.raw[0] = 0;

	/*Timed workload timer*/ //Not support
	intel_smart_page->timed_workload_timer.key = 0xE4;
	intel_smart_page->timed_workload_timer.norm = 0;
	intel_smart_page->timed_workload_timer.raw[0] = 0;

	/*Thermal throttle statusr*/ //Not support
	intel_smart_page->thermal_throttle_status.key = 0xEA;
	intel_smart_page->thermal_throttle_status.thermal_throttle.count = 0;
	intel_smart_page->thermal_throttle_status.thermal_throttle.pct = 0;

	/*Retry buffer over flow count*/ //Not support
	intel_smart_page->retry_buffer_overflow_cnt.key = 0xF0;
	intel_smart_page->retry_buffer_overflow_cnt.norm = 0;
	intel_smart_page->retry_buffer_overflow_cnt.raw[0] = 0;

	/*Pll lock loss count*/	//Not support
	intel_smart_page->pll_lock_loss_cnt.key = 0xF3;
	intel_smart_page->pll_lock_loss_cnt.norm = 0;
	intel_smart_page->pll_lock_loss_cnt.raw[0] = 0;

	/*Nand bytes written*/
	shr_unit_byte_nw = shr_nand_bytes_written * 1024 *1024;
	tx_smart_stat->nand_bytes_written[0] = shr_unit_byte_nw>>ctz(0x100000);		//MB to MiB
	tx_smart_stat->nand_bytes_written[1] = (shr_unit_byte_nw>>ctz(0x100000)) >> 16;
	tx_smart_stat->nand_bytes_written[2] = (shr_unit_byte_nw>>ctz(0x100000)) >> 32;
	intel_smart_page->nand_bytes_written.key = 0xF4;
	intel_smart_page->nand_bytes_written.norm = 100; //Always 100
	intel_smart_page->nand_bytes_written.raw[0] = tx_smart_stat->nand_bytes_written[0];
	intel_smart_page->nand_bytes_written.raw[1] = tx_smart_stat->nand_bytes_written[1];
	intel_smart_page->nand_bytes_written.raw[2] = tx_smart_stat->nand_bytes_written[2];

	/*Host Bytes Written*/
	btn_smart_io_t wio;
	btn_get_w_smart_io(&wio);
	smart_stat->data_units_written += wio.host_du_cnt;
	smart_stat->host_write_commands += wio.cmd_recv_cnt;
	tx_smart_stat->host_bytes_written[0] = (smart_stat->data_units_written)>>ctz(0x10000);	// 512 Bytes to Unit is 32MB
	tx_smart_stat->host_bytes_written[1] = (smart_stat->data_units_written)>>ctz(0x10000)>>16;
	tx_smart_stat->host_bytes_written[2] = (smart_stat->data_units_written)>>ctz(0x10000)>>32;
	intel_smart_page->host_bytes_written.key = 0xF5;
	intel_smart_page->host_bytes_written.norm = 100;//Always 100
	intel_smart_page->host_bytes_written.raw[0] = tx_smart_stat->host_bytes_written[0];
	intel_smart_page->host_bytes_written.raw[1] = tx_smart_stat->host_bytes_written[1];
	intel_smart_page->host_bytes_written.raw[2] = tx_smart_stat->host_bytes_written[2];

	/*Host context wear used*/ //Not support
	intel_smart_page->host_ctx_wear_used.key = 0xF6;
	intel_smart_page->host_ctx_wear_used.norm = 0;
	intel_smart_page->host_ctx_wear_used.raw[0] = 0;

	/*Performance status indicator*/ //Not support
	intel_smart_page->perf_stat_indicator.key = 0xF7;
	intel_smart_page->perf_stat_indicator.norm = 0;
	intel_smart_page->perf_stat_indicator.raw[0] = 0;

	/*Media bytes read*/ //Not support
	intel_smart_page->media_bytes_read.key = 0xF8;
	intel_smart_page->media_bytes_read.norm = 0;
	intel_smart_page->media_bytes_read.raw[0] = 0;

	/*Available fw downgrades*/ //Not support
	intel_smart_page->avail_fw_downgrades.key = 0xF9;
	intel_smart_page->avail_fw_downgrades.norm = 0;
	intel_smart_page->avail_fw_downgrades.raw[0] = 0;

#if RAID_SUPPORT_UECC
	/*Re-allocated sector count*/
	u32 nand_reallocated_sector_cnt = host_prog_fail_cnt + host_uecc_detection_cnt;    //host cnt (read+prog)
	tx_smart_stat->reallocated_sector_count = tx_smart_stat->program_fail_count + tx_smart_stat->host_uecc_detection_cnt;    //host cnt (read+prog)
	intel_smart_page->re_alloc_sectr_cnt.key = 0x05;
	if (nand_reallocated_sector_cnt >= resver_blk){
		intel_smart_page->re_alloc_sectr_cnt.norm = 0;
	}
	else{
		intel_smart_page->re_alloc_sectr_cnt.norm = 100 - (nand_reallocated_sector_cnt * 100 / resver_blk);
	}
	intel_smart_page->re_alloc_sectr_cnt.raw[0] = tx_smart_stat->reallocated_sector_count;
	intel_smart_page->re_alloc_sectr_cnt.raw[1] = tx_smart_stat->reallocated_sector_count >> 16;
#endif

	/*Soft ecc error rate*/ //Not support
	intel_smart_page->soft_ecc_err_rate.key = 0x0D;
	intel_smart_page->soft_ecc_err_rate.norm = 0;
	intel_smart_page->soft_ecc_err_rate.raw[0] = 0;

	/*Unexpected power loss*/ //Not support
	intel_smart_page->unexp_power_loss.key = 0xAE;
	intel_smart_page->unexp_power_loss.norm = 0;
	intel_smart_page->unexp_power_loss.raw[0] = 0;

	//nvme_apl_trace(LOG_ALW, 0x202d,"t:0x%x",sizeof(intel_smart_info_t));
	//nvme_apl_trace(LOG_ALW, 0x5fe0,"t2:0x%x",sizeof(nvme_intel_smart_log_item));

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(intel_smart_page, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
	else if (handle_result == HANDLE_RESULT_FAILURE)
	{
		//dtag_put(DTAG_T_SRAM, dtag[0]);
		//sys_free(SLOW_DATA, req->req_prp.mem);
		//sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}

}
#endif

/*!
 * @brief return PHY Training Result
 *
 * @param req		request
 * @param cmd		NVM command
 * @param bytes		how many bytes Host requests
 *
 * @return		command status, always data transferring
 */
static enum cmd_rslt_t nvmet_get_phy_training_result(
	req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	// PCBasher full random
	if (cmd->nsid != 0 && cmd->nsid != NVME_GLOBAL_NS_TAG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	pcie_phy_leq_dfe();
	pcie_phy_cali_results();

	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	dtag_t *dtag;
	void *mem = NULL;
	struct nvme_phy_trainging_result_page *train_result;
	u32 transfer = DTAG_SZE <= bytes ? DTAG_SZE : bytes;
	int i = 0;
	u32 ofst = 0;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	mem = dtag2mem(dtag[0]);
	memset(mem, 0, DTAG_SZE);

	train_result = (struct nvme_phy_trainging_result_page *)mem;

	train_result->lane_leq[0] = lane_leq[0];
	train_result->lane_leq[1] = lane_leq[1];
	train_result->lane_leq[2] = lane_leq[2];
	train_result->lane_leq[3] = lane_leq[3];

	train_result->lane_dfe[0] = lane_dfe[0];
	train_result->lane_dfe[1] = lane_dfe[1];
	train_result->lane_dfe[2] = lane_dfe[2];
	train_result->lane_dfe[3] = lane_dfe[3];

	train_result->pll_kr = shr_pll_kr;

	train_result->lane_cdr[0] = lane_cdr[0];
	train_result->lane_cdr[1] = lane_cdr[1];
	train_result->lane_cdr[2] = lane_cdr[2];
	train_result->lane_cdr[3] = lane_cdr[3];

	train_result->lane_rc[0] = lane_rc[0];
	train_result->lane_rc[1] = lane_rc[1];
	train_result->lane_rc[2] = lane_rc[2];
	train_result->lane_rc[3] = lane_rc[3];

	pcie_core_status2_t link_status;
	link_status.all = readl((void *)(PCIE_WRAP_BASE + PCIE_CORE_STATUS2));
	train_result->pcie_gen = link_status.b.neg_link_speed;
	train_result->pcie_lanes = link_status.b.neg_link_width;

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(train_result, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
	return handle_result;
}

/*!
 * @brief NVMe get misc log pages handler
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
static ddr_code enum cmd_rslt_t nvmet_get_log_page(req_t *req, struct nvme_cmd *cmd)
{
	u8 lid = cmd->cdw10;
	u16 numd = ((cmd->cdw10 >> 16) & 0x0FFF);
	u16 numd_bytes = (numd + 1) << 2;
	u16 numd_bytes_g = cmd ->cdw12;
    u32 lpol = cmd->cdw12;
	u32 lpou = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;
//    nvme_apl_trace(LOG_ERR, 0, "nvme_vsc_ev_log cdw10:(%d) cdw11:(%d) cdw12:(%d) cdw13:(%d) cdw14:(%d) cdw15:(%d)", cmd->cdw10, cmd->cdw11, cmd->cdw12, cmd->cdw13, cmd->cdw14, cmd->cdw15);
	nvme_apl_trace(LOG_INFO, 0x28c0, "GetLogPage: LID(%d) NUMD(%d) PRP1(%x) PRP2(%x)",
				   lid, numd, cmd->dptr.prp.prp1, cmd->dptr.prp.prp2);
    if(lid == 0xD6 || lid == 0xD7)
    {
        switch (lid)
        {
        #if !defined(PROGRAMMER)
            case NVME_LOG_DUMPLOG_NAND:
    //            nvme_apl_trace(LOG_ERR, 0, "nvme_vsc_ev_log cdw10:(%d) cdw11:(%d) cdw12:(%d) cdw13:(%d) cdw14:(%d) cdw15:(%d)", cmd->cdw10, cmd->cdw11, cmd->cdw12, cmd->cdw13, cmd->cdw14, cmd->cdw15);
    //            nvme_apl_trace(LOG_INFO, 0, "GetLogPage: LID(%d) NUMD(%d) PRP1(%x) PRP2(%x)",
    //				   lid, numd, cmd->dptr.prp.prp1, cmd->dptr.prp.prp2);
                handle_result = nvme_vsc_ev_log(req, cmd, 0);
                if(handle_result == HANDLE_RESULT_FAILURE)
                    nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_LOG_PAGE);
                break;

            case READ_GLIST:
                    //nvme_apl_trace(LOG_ERR, 0, "read glist");
                handle_result = Read_Glist_Table(req, cmd, numd_bytes_g);
                if(handle_result == HANDLE_RESULT_FAILURE)
    		        nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_LOG_PAGE);
                break;
        #endif                                              //dump log & read glist Young add 20210714
            default:
                /* FIXME: old tnvme sucks for SC=09 */
                nvmet_set_status(&req->fe,
                                 NVME_SCT_COMMAND_SPECIFIC,
                                 NVME_SC_INVALID_LOG_PAGE);
                nvme_apl_trace(LOG_ERR, 0x05e9, "outher\n");
                return HANDLE_RESULT_FAILURE;
        }

        nvme_apl_trace(LOG_ERR, 0x8a34, "success\n");
        return handle_result;
    }

    else
    {

    	if (lpol > numd_bytes || lpou > numd_bytes)
    	{
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
        	if( lid != NVME_LOG_TELEMETRY_HOST_INIT && lid != NVME_LOG_TELEMETRY_CTRLR_INIT )
#endif
        	{
            	nvmet_set_status(&req->fe,
                            	 NVME_SCT_GENERIC,
                            	 NVME_SC_INVALID_FIELD);
            	return HANDLE_RESULT_FAILURE;
        	}
    	}

    	/* IOL tnvme 19:0.12.0 */
    	if (numd_bytes > DTAG_SZE)
    	{
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
        	if( lid != NVME_LOG_TELEMETRY_HOST_INIT && lid != NVME_LOG_TELEMETRY_CTRLR_INIT )
#endif
        	{
            	numd_bytes = DTAG_SZE;
        	}
    	}

    	switch (lid)
    	{
    	case NVME_LOG_ERROR:
    		handle_result = nvmet_get_lp_log_error(req, cmd, numd_bytes);
    		break;
    	case NVME_LOG_HEALTH_INFORMATION:
    		handle_result = nvmet_get_lp_health_information(req, cmd, numd_bytes);
    		break;
    	case NVME_LOG_FIRMWARE_SLOT:
    		handle_result = nvmet_get_lp_firmware_slot(req, cmd, numd_bytes);
    		break;
    	case NVME_LOG_CHANGED_NS_LIST:
    		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_LOG_PAGE);
    		return HANDLE_RESULT_FAILURE;
    		break;
    	case NVME_LOG_COMMAND_EFFECTS_LOG: //2019-12-12 Alan MS Lin
    		handle_result = nvmet_get_lp_command_effects_log(req, cmd, numd_bytes);
    		break;
#if CO_SUPPORT_DEVICE_SELF_TEST
		case NVME_LOG_DEVICE_SELF_TEST:
			handle_result = nvmet_get_lp_command_dev_self_test(req, cmd, numd_bytes);
			break;
#endif

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
		case NVME_LOG_TELEMETRY_HOST_INIT:
		case NVME_LOG_TELEMETRY_CTRLR_INIT:
    		if((!telemetry_transferring)) {
        		telemetry_transferring = true;

        		if(lid == NVME_LOG_TELEMETRY_HOST_INIT) {
            		handle_result = nvmet_get_lp_telemetry_host(req, cmd, numd_bytes, lid);
        		} else {
            		handle_result = nvmet_get_lp_telemetry_ctrlr(req, cmd, numd_bytes, lid);
        		}

        		if (handle_result == HANDLE_RESULT_FAILURE)
            		evt_set_cs(evt_telemetry_req_check, 0, 0, CS_NOW);

        		break;
    		}else {
        		list_add_tail(&req->entry2, &telemetry_req_pending);
        		handle_result = HANDLE_RESULT_RUNNING;
    		}
    		break;
#endif

#if defined(OC_SSD)
    	case OCSSD_V20_LOG_CHUNK_INFO:
    		handle_result = nvmet_get_lp_chunk_information(req, cmd);
    		break;
#endif

#if (Synology_case)
		case NVME_SYNOLOGY_SMART:
			handle_result = nvmet_get_synology_smart(req, cmd, numd_bytes);
			break;
#endif

#if CO_SUPPORT_OCP
		case NVME_OCP_SMART:
			handle_result = nvmet_get_ocp_smart(req, cmd, numd_bytes);
			break;
#endif

#if CO_SUPPORT_SANITIZE
		case NVME_LOG_SANITIZE_STATUS:
			handle_result = nvmet_get_lp_sanitize(req, cmd, numd_bytes);
			break;
#endif
#if (Add_smart)
	#if (Xfusion_case)
		case NVME_LOG_ADDITIONAL_SMART: //2024-04-15 Marven Chen
    		handle_result = nvmet_get_intel_additional_smart(req, cmd, numd_bytes);
    		break;
	#else
    	case NVME_LOG_ADDITIONAL_SMART: //2020-09-03 Alan Lin
    		handle_result = nvmet_get_additional_smart(req, cmd, numd_bytes);
    		break;
	#endif
#endif
    	case NVME_LOG_PHY_TRAINING_REUSLT: //2021-01-022 Alan Lin
    		handle_result = nvmet_get_phy_training_result(req, cmd, numd_bytes);
    		break;
    	default:
    		/* FIXME: old tnvme sucks for SC=09 */
    		nvmet_set_status(&req->fe,
    						 NVME_SCT_COMMAND_SPECIFIC,
    						 NVME_SC_INVALID_LOG_PAGE);
    		return HANDLE_RESULT_FAILURE;
    	}

    	if (handle_result != HANDLE_RESULT_FAILURE)
    	{
    		nvmet_aer_unmask_for_getlogcmd((Get_Log_CDW10)cmd->cdw10);
    	}

    	return handle_result;
    }
}

extern void nvmet_csts_PP(bool set);	//20201225-Eddie
slow_code_ex static bool nvmet_fw_commit_done(req_t *req)
{
	nvmet_core_cmd_done(req);

	extern void nvmet_start_warm_boot(void);

	misc_clear_STOP_BGREAD();
#if (PI_FW_UPDATE == mENABLE)			//20210110-Eddie
#if GC_SUSPEND_FWDL		//20210308-Eddie
	if ((cmf_idx == 3) || (cmf_idx == 4)){	//Temporarily +8 need to GC suspend
		evlog_printk(LOG_ALW,"[FW Commit] Resume GC");
		FWDL_GC_Handle(GC_ACT_RESUME);
		while (!fwdl_gcsuspend_done)
		{
			cpu_msg_isr();
			extern void bm_isr_com_free(void);
			bm_isr_com_free();
		}	//Lock Until GC handle done -> CPU3 FWDL_GC_Handle FTL.C
		fwdl_gcsuspend_done = 0;
	}
#endif
	if ((cmf_idx == 1) || (cmf_idx == 2)){
		nvmet_enable_fetched_cmd_pi();
		evlog_printk(LOG_ALW,"pi cmd resume fetching");
	}
	else{
	        nvmet_enable_fetched_cmd_pi();
	        evlog_printk(LOG_ALW,"pi cmd resume fetching");
	}
#endif

	if (misc_is_fw_run_reset())	//20201225-Eddie
	{
	#if 0//(_TCG_)
		// update EPM
		epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		epm_aes_data->tcg_sts     = mTcgStatus;
		epm_aes_data->readlocked  = mReadLockedStatus;
		epm_aes_data->writelocked = mWriteLockedStatus;
		epm_update(AES_sign, (CPU_ID - 1));
	#endif

		nvmet_csts_PP(true);
		nvmet_start_warm_boot();
	}

	return true;
}

/*!
 * @brief NVMe firmware commit function
 *
 * This function is not finished, yet.
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
slow_code_ex void reset_fw_du_bmp(void)
{
	memset((void*)fw_du_bmp, 0, sizeof(u32)*FW_DU_BMP_CNT);
	//nvme_apl_trace(LOG_ERR, 0, "4 0x%x 0x%x 0x%x",fw_du_bmp[0],fw_du_bmp[1],fw_du_bmp[2]);
	//nvme_apl_trace(LOG_ERR, 0, "5 0x%x 0x%x 0x%x",fw_du_bmp[3],fw_du_bmp[4],fw_du_bmp[5]);
	//nvme_apl_trace(LOG_ERR, 0, "6 0x%x 0x%x 0x%x",fw_du_bmp[6],fw_du_bmp[7],fw_du_bmp[8]);
	//nvme_apl_trace(LOG_ERR, 0, "6 0x%x 0x%x 0x%x",fw_du_bmp[9],fw_du_bmp[10],fw_du_bmp[11]);
}
static fast_code bool check_fw_du_overlap(u32 fw_size,u32 fw_ofst){

	u32 du = (fw_ofst<<2)>>12;
	u32 numd = (fw_size>>12) - 1;//0'base
	u32 i;

	for(i = 0; i<=numd; i++){
		u32 temp = du + i;
		if((temp>>5) >= FW_DU_BMP_CNT)
		{
			nvme_apl_trace(LOG_ERR, 0x9e6d, "du:%d over bmp range",temp);
			return false;
		}

		if(fw_du_bmp[temp>>5] & BIT(temp&0x1F))
		{
			nvme_apl_trace(LOG_ERR, 0x7e00, "du:%d overlap",temp);
			nvme_apl_trace(LOG_ERR, 0x7b37, "1 0x%x",fw_du_bmp[temp>>5]);
			return true;
		}
		else
			fw_du_bmp[temp>>5] |= BIT(temp&0x1F);
	}
	//nvme_apl_trace(LOG_ERR, 0, "1 0x%x 0x%x 0x%x",fw_du_bmp[0],fw_du_bmp[1],fw_du_bmp[2]);
	//nvme_apl_trace(LOG_ERR, 0, "2 0x%x 0x%x 0x%x",fw_du_bmp[3],fw_du_bmp[4],fw_du_bmp[5]);
	//nvme_apl_trace(LOG_ERR, 0, "3 0x%x 0x%x 0x%x",fw_du_bmp[6],fw_du_bmp[7],fw_du_bmp[8]);

	return false;
}

static slow_code_ex enum cmd_rslt_t
nvmet_firmware_commit(req_t *req, struct nvme_cmd *cmd)
{
	struct nvme_fw_commit *fw_commit =
		(struct nvme_fw_commit *)(&cmd->cdw10);
	nvme_apl_trace(LOG_ERR, 0xb881, "FwActivate  FS|%d  CA|%d",fw_commit->fs, fw_commit->ca);

	//follow nvme spec, when fs==0, controller choose from fw slot 1~7.
	//So when fw slot ==0, we always select fw slot 1.
	if(fw_commit->fs == 0){
		fw_commit->fs = 1;
	}

#if 0 //need debug
	upgrade_fw.fw_dw_status = DW_ST_INIT;
	upgrade_fw.sb_dus_amt = 0;
	upgrade_fw.fw_dw_status = 0;
#endif
	// PCBasher full random
	if (cmd->nsid != 0)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	//for DM8 NT3 //3.1.7.4 merged 20201201 Eddie
	if (fw_image_status != FW_IMG_NRM)
	{
		if (fw_image_status == FW_IMG_OVERLAP)
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_OVERLAPPING_RANGE);
		}
		else if (fw_image_status == FW_IMG_INVALID)
		{
			if ((fw_commit->ca == NVME_FW_COMMIT_ENABLE_IMG)&&((fw_commit->fs == (ctrlr->cur_afi.slot + 1)) ||
				(fw_commit->fs == ctrlr->cur_afi.slot))){
				if((cmd->nsid == 0)){
					evlog_printk(LOG_ALW,"CA2,FS0&1,NS0");
					nvmet_set_status(&req->fe,
									 NVME_SCT_COMMAND_SPECIFIC,
									 NVME_SC_FIRMWARE_ACTIVATION_PROHIBITED);
				}
				else{
					evlog_printk(LOG_ALW,"CA2,FS0&1");
					nvmet_set_status(&req->fe,
									 NVME_SCT_GENERIC,
									 NVME_SC_INVALID_FIELD);
				}
			}
			else{

				nvmet_set_status(&req->fe,
								 NVME_SCT_COMMAND_SPECIFIC,
								 NVME_SC_INVALID_FIRMWARE_IMAGE);
			}
		}
		fw_image_status = FW_IMG_NRM;
		nvmet_enable_fetched_cmd_pi();
		misc_clear_STOP_BGREAD();
		reset_fw_du_bmp();
		return HANDLE_RESULT_FAILURE;
	}

	if (fw_commit->ca > NVME_FW_COMMIT_RUN_IMG)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

#if defined(PROGRAMMER)
	extern u16 fw_max_slot;
	if ((fw_commit->fs > fw_max_slot) || (fw_commit->fs == 0))
	{
#else
	if ((fw_commit->fs > ctrlr->max_fw_slot) || (fw_commit->fs == 0))
	{
#endif
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_FIRMWARE_SLOT);
		return HANDLE_RESULT_FAILURE;
	}

	//nvme_apl_trace(LOG_ALW, 0, "Commit ca:%d", fw_commit->ca);

	if (fw_commit->ca != NVME_FW_COMMIT_REPLACE_IMG)
		ctrlr->cur_afi.slot_next = fw_commit->fs;

	dwnld_com_lock = true;
	reset_fw_du_bmp();
	req->completion = nvmet_fw_commit_done;

	misc_set_STOP_BGREAD();

#if (PI_FW_UPDATE == mENABLE)		//20210110-Eddie
#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
	pi_req1 = ((fw_commit->fs << 16) | fw_commit->ca);
	pi_req2 = (u32)req;
#endif
	evlog_printk(LOG_ALW,"pi cmd suspend");
	nvmet_pi_start_cmd_suspend(req);
	evlog_printk(LOG_ALW,"pi cmd suspend DONE");
#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
//Move to pi_timer
#else
#if GC_SUSPEND_FWDL// GC suspend while FW download -20210308-Eddie
	evlog_printk(LOG_ALW,"[FW Commit] GC suspend");
	FWDL_GC_Handle(GC_ACT_SUSPEND);
	while (!fwdl_gcsuspend_done){
		}	//Lock Until GC handle done -> CPU3 FWDL_GC_Handle FTL.C
	fwdl_gcsuspend_done = 0;
#endif
	if ((cmf_idx == 1) || (cmf_idx == 2)){
		//evt_set_cs(evt_fw_commit, (fw_commit->fs << 16) | fw_commit->ca, (u32)req, CS_TASK);
		evt_set_delay(evt_fw_commit, (fw_commit->fs << 16) | fw_commit->ca, (u32)req, 10);	//wait for 1 sec cmds consuming
	}
	else{		//512+8 or 4096+8
		evt_set_delay(evt_fw_commit, (fw_commit->fs << 16) | fw_commit->ca, (u32)req, 10);	//wait for 1 sec cmds consuming
	}
#endif
#else
	evt_set_cs(evt_fw_commit, (fw_commit->fs << 16) | fw_commit->ca, (u32)req, CS_TASK);
#endif

	return HANDLE_RESULT_PENDING_BE;
}

/*!
 * @brief NVMe firmware image download function
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
static enum cmd_rslt_t
nvmet_firmware_image_download(req_t *req, struct nvme_cmd *cmd)
{
	enum cmd_rslt_t handle_result;
	u32 numd = cmd->cdw10;
	u32 fw_ofst = cmd->cdw11;
	u32 fw_size = (numd + 1) << 2;

#if 1 //3.1.7.4 merged 20201201 Eddie

	nvme_apl_trace(LOG_ERR, 0xcc60, "FwImageDl  Numd|%x offset|%x size|%x ", numd,fw_ofst,fw_size);

	//NT2 test: Can not specify any namespace
	if (cmd->nsid != 0)
	{
		evlog_printk(LOG_ALW,"FW image DL, nsid = %d",cmd->nsid);
		//download_cmd_err = true;
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	if(oldoffset<fw_ofst)
	{
		//offset_backward = false;
		oldoffset = fw_ofst;
	}
	else
	{
		nvme_apl_trace(LOG_ERR, 0xfbc1, "offset backward found");
		offset_backward = true;
		oldoffset = 0;
	}
#else
	/* TODO: an offset sanity check due to it cannot be overlapped */
	if (nsid == 0) // NSID = 0
	{
		// Nothing to do now
	}
	else if (nsid == NVME_GLOBAL_NS_TAG) // NSID = FFFFFFFFh
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	else if (nsid > NVMET_NR_NS) // NSID > 1, NSID < FFFFFFFFh
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	else // NSID = 1
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
#endif
	if ((fw_ofst & 0x3ff) != 0)
	{
		//download_cmd_err = true;
		nvme_apl_trace(LOG_ERR, 0x2bce, "download fw NUMD(0x%x) OFFSET(0x%x) not 4K alignment",
					   numd, fw_ofst);
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_OVERLAPPING_RANGE);
		fw_image_status = FW_IMG_OVERLAP; //fw image isn't 4K alignment
		return HANDLE_RESULT_FAILURE;
	}else if((fw_size &  0xfff) != 0){
		nvme_apl_trace(LOG_ERR, 0x0254, "fw size (0x%x) not 4K alignment",
					   fw_size);
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_OVERLAPPING_RANGE);
		return HANDLE_RESULT_FAILURE;
	}else if(check_fw_du_overlap(fw_size,fw_ofst)){
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_OVERLAPPING_RANGE);
		fw_image_status = FW_IMG_OVERLAP; //fw image isn't 4K alignment
		return HANDLE_RESULT_FAILURE;
	}

//	nvme_apl_trace(LOG_DEBUG, 0, "NUMD(%d) OFFSET(%d) PRP1(%x) PRP2(%x)",
//				   numd, fw_ofst, cmd->dptr.prp.prp1, cmd->dptr.prp.prp2);
#if 0//need debug

	u8 fw_du_xfered = (fw_ofst*4)>>12;//fw is 4k aligh set in identify controller
	if(download_fw_reset)
	{
		if(upgrade_fw.fw_dw_status != DW_ST_UPGRADE_FW_COMPLETE &&
			//upgrade_fw.fw_dw_status != DW_ST_VERIFY_FW_ERR ||
			(fw_image_status != FW_IMG_NRM && upgrade_fw.fw_dw_status == DW_ST_INIT) ||
			upgrade_fw.fw_dw_status != DW_ST_INIT) //check fw overlap
		{
			if(fw_du_xfered != upgrade_fw.sb_dus_amt ||	fw_du_xfered == 0 )
			{
				fw_image_status = FW_IMG_OVERLAP;
				nvme_apl_trace(LOG_ERR, 0x2106, "fw ofset_xfer_du %d,fw already alloc du %d",
					fw_du_xfered,upgrade_fw.sb_dus_amt);
				nvmet_set_status(&req->fe,
								 NVME_SCT_COMMAND_SPECIFIC,
								 NVME_SC_OVERLAPPING_RANGE);
				return HANDLE_RESULT_FAILURE;
			}
		}
	}

	download_fw_reset = true;
#endif
#if 1 //3.1.7.4 merged 20201201 Eddie
	req->completion = NULL;//nvmet_core_downlaod_cmd_done;
	req->req_prp.is2dtag = false;

	if (fw_size > ctrlr->page_size){
		nvmet_alloc_admin_res(req, (DTAG_SZE * 2));  // dtag 0 for data . dtag 1 for prp list
		req->req_prp.mem_sz = occupied_by(ctrlr->page_size, DTAG_SZE);
		req->req_prp.is2dtag = true;
	}
	else{
	nvmet_alloc_admin_res(req, ctrlr->page_size);
	}

	//evlog_printk(LOG_ALW, "[ADMIN_FWDL]req->req_prp.mem_sz %d, fw_size %d", req->req_prp.mem_sz, fw_size);
#else
	nvmet_alloc_admin_res(req, fw_size);
#endif

#if VALIDATE_BOOT_PARTITION
	u32 dtag_cnt;
	dtag_cnt = req->req_prp.mem_sz;
	validate_bp_dtag_get(dtag_cnt);
#endif

	handle_result = nvmet_map_admin_prp(req, cmd, fw_size, nvmet_admin_fw_download_xfer_done);

	if (handle_result == HANDLE_RESULT_DATA_XFER)
	{
		dwnld_com_lock = true;
		req->completion = nvmet_core_downlaod_cmd_done;
#if 1
		nvmet_admin_fw_download_xfer_prp(req);
#else
		int xfer, ofst = 0, cur = 0, sz = 0;

		for (i = 0; i < req->req_prp.nprp; i++)
		{
			prp_entry_t *prp = &req->req_prp.prp[i];
			int prp_ofst = 0;
			int len = prp->size;
		again:
			if (sz == 0)
			{
				mem = dtag2mem(dtag[cur++]);
				sys_assert(cur <= dtag_cnt);
				ofst = 0;
				sz = DTAG_SZE;
			}

			xfer = min(len, sz);

			req->req_prp.required++;
			hal_nvmet_data_xfer(prp->prp + prp_ofst,
								ptr_inc(mem, ofst),
								xfer, READ, (void *)req, nvmet_admin_fw_download_xfer_done);

			ofst += xfer;
			prp_ofst += xfer;
			sz -= xfer;
			len -= xfer;

			if (len != 0)
			{
				sys_assert(sz == 0);
				goto again;
			}
		}
#endif
	}

	return handle_result;
}

#if CO_SUPPORT_DEVICE_SELF_TEST

/*ddr_code void run_time_flush_qbt_done(ftl_core_ctx_t *ctx)
{
    btn_de_wr_enable();
    nvme_apl_trace(LOG_INFO, 0x60ed, "flush QBT done, ctx:0x%x", ctx);
    sys_free(FAST_DATA, ctx);
}

ddr_code bool run_time_flush_qbt()
{
    ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));
	sys_assert(fctx);
    nvme_apl_trace(LOG_INFO, 0x73ac, "start flush QBT, ctx:0x%x", fctx);

	btn_de_wr_disable();
	fctx->ctx.caller = NULL;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->flags.b.shutdown = 1;
	fctx->ctx.cmpl = run_time_flush_qbt_done;
	ucache_flush(fctx);

    return true;
}*/
ddr_code enum cmd_rslt_t nvmet_dev_self_test(req_t *req, struct nvme_cmd *cmd)
	{
		u16 stc = cmd->cdw10;
		nvme_apl_trace(LOG_INFO, 0x98d1,"DeviceSelfTest STC|%x NSID|%d", stc,cmd->nsid);
		// 1ns
#ifdef DISABLE_NS_G
		if ((cmd->nsid > 1) && (cmd->nsid != 0xFFFFFFFF))
#else
		if ((cmd->nsid > NVMET_NR_NS) && (cmd->nsid != 0xFFFFFFFF))
#endif
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}

		else if((ctrlr->nsid[cmd->nsid - 1].type != NSID_TYPE_ACTIVE) && (cmd->nsid != 0) && (cmd->nsid != 0xFFFFFFFF)){
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}

        /*if (run_time_flush_qbt() == false)
        {
            return HANDLE_RESULT_FAILURE;
        }*/

		switch (stc)  // Self-test Code
		{
			case cDST_SHORT:
			case cDST_EXTENDED:
				if (!gCurrDSTOperation)
				{
					if		(stc == cDST_SHORT)    gDeviceSelfTest = cDST_SHORT_RAM_CHECK; // TODO
					else if (stc == cDST_EXTENDED) gDeviceSelfTest = cDST_EXTEN_DATA_INTEGRITY;
					/*
					if(cmd->nsid == 0)
					{
						DST_Operation(stc, 0);
						DST_Completion(cDSTCompleted);	
						nvme_apl_trace(LOG_INFO, 0xbb05, "[DST] NSID is 0 -> Nothing to do");
						return HANDLE_RESULT_FINISHED;
					}
					*/
					/*
					if(cmd->nsid == 0)
						DST_Operation(stc, 2);// for edvx testing nsid 0 -> 2
					else if(cmd->nsid == 32)
						DST_Operation(stc, 1);// for edvx testing nsid 0 -> 2
					else */					
					DST_Operation(stc, cmd->nsid);
					evt_set_cs(evt_dev_self_test, 0, 0, CS_TASK);
				}
				else
				{
					nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_DEVICE_SELF_TEST_IN_PROGRESS);
					nvme_apl_trace(LOG_INFO, 0xc8ea, "[DST] CMD fail DST in progress");
					return HANDLE_RESULT_FAILURE;//Eric for edvx20240115
				}
				break;

			case cDST_ABORT:
				DST_CmdAbort(req, stc);
				break;

			default:
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
				return HANDLE_RESULT_FAILURE;
		}

		return HANDLE_RESULT_FINISHED;
	}
#endif


#if VALIDATE_BOOT_PARTITION
req_t *bp_req;

static enum cmd_rslt_t nvmet_boot_partition_read(req_t *req, struct nvme_cmd *cmd)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_RUNNING;
	bp_req = req;
	nvmet_map_admin_prp(req, cmd, DTAG_SZE);
	nvme_apl_trace(LOG_ERR, 0x1019, "host mem: 0x%x %x\n", cmd->dptr.prp.prp1, cmd->dptr.prp.prp1 >> 32);
	extern int bp_read_trigger_main(u64 hmem, u8 bp_sel, u32 offset);
	bp_read_trigger_main(cmd->dptr.prp.prp1, 0, cmd->cdw10);
	return handle_result;
}

#endif // VALIDATE_BOOT_PARTITION

/*
slow_code void ftl_ns_del_done(flush_ctx_t *ctx)
{
	nvme_apl_trace(LOG_INFO, 0, "ftl_ns_del_done");
	sys_free(FAST_DATA, ctx);
}
*/

bool ddr_code nvme_format_stop_gc_wait()
{
	format_gc_suspend_done = 0;
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GC_STOP, 0, 0);
	while(format_gc_suspend_done == 0){
		if (plp_trigger)
		{
			nvme_apl_trace(LOG_ERR, 0x46ab, "format power loss return");
			return 1;
		}
	}
	return 0;
}

#if (NS_MANAGE == FEATURE_SUPPORTED) //joe add def 20200916
//--------------joe 20200608 ns_array memory manage function start----------------//
//joe add NS 20200813
extern u16 ns_valid_sector;				 //joe 20200528
extern u16 drive_total_sector;			 //joe 20200721
extern struct ns_section_id ns_sec_id[]; //joe 20200721 for ftl core
extern struct ns_array_manage *ns_array_menu;
extern u16 ns_trans_sec[];
extern u16 ns_order;
extern u16 ns_sec_order[];
extern u16 ns_valid_sec;//joe 20201223 add for ns read performance
extern u16 *sec_order_p;
ddr_code void nvmet_create_ns_section_array(u32 ns_id, u64 nsze)
{
	vu32 t1, t2; //joe 20200618 add test time
	u32 time;
	t1 = get_tsc_lo(); //joe 20200618 add test time
	u16 ns_sec_num;
	u16 aa = 0;
	u32 iii = 0;
	u16 count = 0;
	u16 a = 0;
	if (host_sec_bitz == 9)
	{//joe add sec size 20200817
		if ((nsze % NS_SIZE_GRANULARITY1) == 0) //joe 20200528  check the capacity is enough
		ns_sec_num = nsze / NS_SIZE_GRANULARITY1;
		else
		ns_sec_num = nsze / NS_SIZE_GRANULARITY1 + 1;
	}
	else
	{
		if ((nsze % NS_SIZE_GRANULARITY2) == 0) //joe 20200528  check the capacity is enough
		ns_sec_num = nsze / NS_SIZE_GRANULARITY2;
		else
		ns_sec_num = nsze / NS_SIZE_GRANULARITY2 + 1;
	}
	nvme_apl_trace(LOG_INFO, 0x8b75, "JOE: create ns_sec_num:%d", ns_sec_num);

	//part1 collect valid sec
	for (iii = 0; iii < drive_total_sector; iii++)
	{

		if (0 == ns_sec_num) //joe add fix bug at nsze=0 20200805
			break;

		if (ns_array_menu->valid_sec2[iii] == 0)
		{
			ns_array_menu->ns_sec_array[ns_array_menu->free_start_array_point + aa] = iii;
			nvme_apl_trace(LOG_DEBUG, 0x8b76, "nsid:%d ns_sec_array[%d]=%d", ns_id, ns_array_menu->free_start_array_point + aa, ns_array_menu->ns_sec_array[ns_array_menu->free_start_array_point + aa]);
			ns_array_menu->valid_sec2[iii] = 1;
			ns_array_menu->sec_order[iii] = aa; //joe add  for hcrc faster 20200825
			aa++;
			ns_array_menu->ns_valid_sector++; //joe 20200827 add ns_array_menu
		}
		if (aa == ns_sec_num)
			break;
	}
	for(iii=0;iii<drive_total_sector;iii++)//joe add performance test 20201217
	{
		ns_trans_sec[iii]=ns_array_menu->ns_sec_array[iii];
	}

	ns_sec_id[ns_id].sec_num = ns_sec_num; //joe add 20200731

	//part 2 prpare ns_array struct, order, total order, free start array point
	ns_array_menu->array_order[ns_array_menu->total_order_now] = ns_id;
	nvme_apl_trace(LOG_DEBUG, 0xe5f3, "create array_order[%d]:%d", ns_array_menu->total_order_now, ns_array_menu->array_order[ns_array_menu->total_order_now]);
	ns_array_menu->total_order_now++;
	ns_array_menu->ns_array[ns_id].sec_id = &ns_array_menu->ns_sec_array[ns_array_menu->free_start_array_point];
	//ns_sec_id[ns_id].sec_id = &ns_array_menu->ns_sec_array[ns_array_menu->free_start_array_point]; //joe 20200617 add for ftl_core set meta
	ns_sec_id[ns_id].sec_id = &ns_trans_sec[ns_array_menu->free_start_array_point]; //joe 20201217 add for performance
	for (count = 0; count < ns_sec_num; count++)
	{ //joe add 20200609 add  debug
		nvme_apl_trace(LOG_DEBUG, 0xb27f, "create ns_array[%d].sec_id[%d]:%d", ns_id, count, ns_array_menu->ns_array[ns_id].sec_id[count]);
	}

	ns_array_menu->ns_array[ns_id].array_start = ns_array_menu->free_start_array_point;
	nvme_apl_trace(LOG_DEBUG, 0x5241, "create 1 free_start_array_point:%d", ns_array_menu->free_start_array_point);
	ns_array_menu->ns_array[ns_id].array_last = ns_array_menu->free_start_array_point + ns_sec_num - 1;
	ns_array_menu->ns_array[ns_id].sec_num = ns_sec_num;
	ns_array_menu->free_start_array_point = ns_array_menu->free_start_array_point + ns_sec_num;
	nvme_apl_trace(LOG_DEBUG, 0xc690, "create 2 free_start_array_point:%d", ns_array_menu->free_start_array_point);

	t2 = get_tsc_lo(); //joe 20200618 add test time
	time = (t2 - t1) * 100 / (SYS_CLK / 1000);
	nvme_apl_trace(LOG_INFO, 0x5bfd, "create ns array time %d.%d%dms", time / 100, (time / 10) % 10, time % 10);

	for (a = 0; a < ns_array_menu->ns_valid_sector; a++)
	{ //joe 20200827 add ns_array_menu
		nvme_apl_trace(LOG_DEBUG, 0x0e49, "create ns_sec_array[%d]:%d", a, ns_array_menu->ns_sec_array[a]);
	}
	ns_order=ns_array_menu->total_order_now;
	u16 b=0;
	for(b=0;b<1024;b++){
		ns_sec_order[b]=ns_array_menu->sec_order[b];
	}
	ns_valid_sec=ns_array_menu->ns_valid_sector;
	sec_order_p=ns_sec_order;

	// full_1ns for write performance
	if((ns_order==1&&(drive_total_sector==ns_valid_sec)))
		full_1ns=1;
	else
		full_1ns=0;

}
extern void ftl_l2p_partial_reset(u32 sec_idx);
extern void gc_re(void);
extern void hal_nvmet_abort_xfer(bool abort);

#if (TRIM_SUPPORT == ENABLE)
extern Trim_Info TrimInfo;
extern void TrimFlush();
#endif
extern volatile u8 plp_trigger;
slow_code void nvmet_delete_ns_section_array(u32 ns_id, u8 hw_updateL2P)
{
	u16 d = 0;
	u16 c=0;
	//trim ns real lda
	/*  Host_Trim_Data  *trim_data_ns;//joe add 20200923
	trim_data_ns= sys_malloc_aligned(SLOW_DATA, sizeof(Host_Trim_Data), 32);
	u8 ns_del_flag=0;
	trim_data_ns->nsid=ns_id;
	trim_data_ns->Validcnt=ns_array_menu->ns_array[ns_id].sec_num;
	trim_data_ns->Validtag=0;

	if(Trim_handle1(trim_data_ns)){
	ns_del_flag=1;
	printk("ns trim success:%d",ns_del_flag);
	}
	sys_free_aligned(SLOW_DATA, trim_data_ns);*/

	//part1 return free sec
	u16 ns_sec_total = ns_array_menu->ns_array[ns_id].sec_num;
	u16 ns_sec_number = 0;
	//volatile u8  flagg=0;

	if(hw_updateL2P)// if the hw_updateL2P is false,indecat not use the l2p hw to update table
	{
		nvme_apl_trace(LOG_INFO, 0x5fee, "del ns start l2p reset\n");
		//hal_nvmet_abort_xfer(true);
		#if (TRIM_SUPPORT == ENABLE)
		TrimFlush();
		//mdelay(5000);
		//nvme_apl_trace(LOG_INFO, 0, "2. trimFlush:%d\n",TrimInfo.TrimFlush);
		//do
		//{
		//	nvme_apl_trace(LOG_INFO, 0, "wait trim flush flag\n");
		//	nvme_apl_trace(LOG_INFO, 0, "3. trimFlush:%d\n",TrimInfo.TrimFlush);
		//	cpu_msg_isr();
        //    evt_task_process_one();
		//}//while(TrimInfo.TrimFlush == 1);//waite Flush done
		#endif
		//mdelay(1000);

		shr_ftl_save_qbt_flag = false;

        if(nvme_format_stop_gc_wait())
        {
            return ;//power loss return
        }

		shr_trim_vac_save_done = false;

		cpu_msg_issue(CPU_BE - 1, CPU_MSG_TRIM_CPY_VAC, 0, 1);
		while(shr_trim_vac_save_done == false)//wait cpu2 save vac table
		{
			if(plp_trigger)
				return ;

			extern void btn_data_in_isr(void);
			btn_data_in_isr();
            dpe_isr();  // for PBT raid bm_copy_done
            cpu_msg_isr();  // for PBT raid bm_data_copy

		}
		shr_format_copy_vac_flag = true;

		for (c=0;c<ns_sec_total;c++)
		{
			//internal_trim_flag=0;
			if(plp_trigger)
				return;
			ns_sec_number = ns_array_menu->ns_array[ns_id].sec_id[c];
			ftl_l2p_partial_reset(ns_sec_number);
			mdelay_plp(100);//chk plp flag
			//mdelay(100);
		}
		/*
		flush_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(flush_ctx_t));
		sys_assert(ctx);
		ctx->nsid = 1;
		ctx->caller = NULL;
		ctx->flags.all = 0;
		ctx->flags.b.shutdown = 1;
		ctx->cmpl = ftl_ns_del_done;
		ftl_flush(ctx);

		*/
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_DEL_NS_FTL_HANDLE, 0, 0);

		while(shr_ftl_save_qbt_flag == false){
			if(plp_trigger)
				return;
			extern void btn_data_in_isr(void);
	        btn_data_in_isr();
            dpe_isr();  // for PBT raid bm_copy_done
            cpu_msg_isr();  // for PBT raid bm_data_copy

		}
		nvme_apl_trace(LOG_INFO, 0xb760, "del ns end l2p reset");
		//hal_nvmet_abort_xfer(false);
		gc_re();
        #if(BG_TRIM == ENABLE)
        SetBgTrimAbort();
        #endif
	}

	for (d = 0; d < ns_sec_total; d++)
	{
		ns_sec_number = ns_array_menu->ns_array[ns_id].sec_id[d];
		ns_array_menu->valid_sec2[ns_sec_number] = 0;
		nvme_apl_trace(LOG_DEBUG, 0x0d34, "ns_sectotal[%d]:%d", d, ns_sec_number);
		ns_array_menu->ns_valid_sector = ns_array_menu->ns_valid_sector - 1; //joe 20200827 add ns_array_menu
	}
	//nvme_apl_trace(LOG_DEBUG, 0, "delete open free_start_array_point:%d",ns_array_menu->free_start_array_point);
	//part2  find the start order

	nvme_apl_trace(LOG_ERR, 0x44ca, "after delete ns_array_menu->ns_valid_sector%d\n", ns_array_menu->ns_valid_sector);
	u8 order = 0;
	u8 nsid_f;
	u32 i = 0;
	for (i = 0; i < 32; i++)
	{
		if (ns_array_menu->array_order[i] == ns_id)
		{
			nvme_apl_trace(LOG_DEBUG, 0xc3c1, "nsid:%d, order:%d i=%d", ns_id, ns_array_menu->array_order[i], i);
			order = i + 1;
			break;
		}
	}

	//part3   reorder the ns_sec_array
	if (order == 1 && order == ns_array_menu->total_order_now)
	{ //joe add 20200610   for only one NS and delete the only one case

		ns_array_menu->free_start_array_point = 0;
		nvme_apl_trace(LOG_DEBUG, 0x5c87, "1. delete order==total free_start_array_point:%d", ns_array_menu->free_start_array_point);
	}
	else if (order == ns_array_menu->total_order_now)
	{ ///joe add 20200610  for the last order NS  delete case

		u8 last_nsid1 = ns_array_menu->array_order[order - 2];
		ns_array_menu->free_start_array_point = ns_array_menu->ns_array[last_nsid1].array_last + 1;
		nvme_apl_trace(LOG_DEBUG, 0xa440, "2.delete order==total free_start_array_point:%d", ns_array_menu->free_start_array_point);
	}
	else
	{
		u8 ii = 0;
		for (ii = order; ii < ns_array_menu->total_order_now; ii++)
		{

			nsid_f = ns_array_menu->array_order[ii];
			nvme_apl_trace(LOG_DEBUG, 0x0df5, "nsid_f:%d, order:%d", nsid_f, ii);
			u16 secNUM = ns_array_menu->ns_array[nsid_f].sec_num;
			nvme_apl_trace(LOG_DEBUG, 0x016f, "secNUM:%d", secNUM);
			u8 last_nsid = ns_array_menu->array_order[ii - 1];
			nvme_apl_trace(LOG_DEBUG, 0x544b, "last_nsid:%d", last_nsid);

			u16 new_start_point;
			if (ii == order) //joe add 20200609 test solve delete will always point the first order's array
				new_start_point = ns_array_menu->ns_array[last_nsid].array_start;
			else
				new_start_point = ns_array_menu->ns_array[last_nsid].array_last + 1;

			nvme_apl_trace(LOG_DEBUG, 0xed3a, "new_start_point:%d", new_start_point);
			u16 count = 0;
			for (count = 0; count < secNUM; count++)
			{
				//nvme_apl_trace(LOG_DEBUG, 0, "before ns_sec_array[]:%d array_point:%d",ns_array_menu->ns_sec_array[new_start_point+count],new_start_point+count);
				ns_array_menu->ns_sec_array[new_start_point + count] = ns_array_menu->ns_array[nsid_f].sec_id[count];
				nvme_apl_trace(LOG_DEBUG, 0xb5b5, "after ns_sec_array[]:%d  array_point:%d", ns_array_menu->ns_sec_array[new_start_point + count], new_start_point + count);
			}
			ns_array_menu->ns_array[nsid_f].sec_id = &ns_array_menu->ns_sec_array[new_start_point];
			//ns_sec_id[nsid_f].sec_id = &ns_array_menu->ns_sec_array[new_start_point]; //joe 20200617 add for ftl_core set meta
			ns_sec_id[nsid_f].sec_id = &ns_trans_sec[new_start_point];//joe add performance test 20201217
			u16 countt = 0;
			for (countt = 0; countt < secNUM; countt++)
			{ //joe add 20200609 add  debug
				nvme_apl_trace(LOG_DEBUG, 0xf074, "delete re_order ns_array[%d].sec_id[%d]:%d", nsid_f, countt, ns_array_menu->ns_array[nsid_f].sec_id[countt]);
			}

			ns_array_menu->ns_array[nsid_f].array_start = new_start_point;
			ns_array_menu->ns_array[nsid_f].array_last = new_start_point + secNUM - 1;
			ns_array_menu->array_order[ii - 1] = nsid_f;
			ns_array_menu->free_start_array_point = ns_array_menu->ns_array[nsid_f].array_last + 1;
			nvme_apl_trace(LOG_DEBUG, 0xefff, "3.free_start_array_point:%d", ns_array_menu->free_start_array_point);
		}
	}

	//joe 20200610 part4 recover the ns_sec_array to init value
	u16 b = 0;
	for (b = 0; b < ns_sec_total; b++)
	{
		ns_array_menu->ns_sec_array[ns_array_menu->free_start_array_point + b] = 0xffff;
	}

	//part5 reorder the order array
	ns_array_menu->total_order_now--;
	ns_array_menu->array_order[ns_array_menu->total_order_now] = 0xff;
	u16 iii;
       for(iii=0;iii<drive_total_sector;iii++)//joe add test 20201217
	{
		ns_trans_sec[iii]=ns_array_menu->ns_sec_array[iii];
	}
	u16 a = 0;
	for (a = 0; a < ns_array_menu->ns_valid_sector; a++)
	{ //joe 20200827 add ns_array_menu
		nvme_apl_trace(LOG_DEBUG, 0xb6a8, "ns_sec_array[%d]:%d", a, ns_array_menu->ns_sec_array[a]);
	}
	ns_order=ns_array_menu->total_order_now;
	ns_valid_sec=ns_array_menu->ns_valid_sector;
}

//--------------joe 20200608 ns_array memory manage function end----------------//

//-------joe add for nvme format cmd use 20200820--------//
///  DU format Structure
struct du_format_t1
{
	///  Fix format
	eccu_du_fmt_reg0_t fmt0;
	eccu_du_fmt_reg1_t fmt1;

	///  Calc
	eccu_du_fmt_reg2_t fmt2;
	eccu_du_fmt_reg3_t fmt3;
	eccu_du_fmt_reg4_t fmt4;
	eccu_du_fmt_reg5_t fmt5;
	eccu_du_fmt_reg6_t fmt6;

	///  DU property
	u16 encoded_ecc_du_sz;
	u16 encoded_ecc_du_sz_2align;
};
typedef union
{
	u32 all;
	struct
	{
		u32 du_fmt_cfg_idx : 5;
		u32 rsvd_5 : 27;
	} b;
} eccu_du_fmt_sel_reg_t1;
typedef union
{
	u32 all;
	struct
	{
		u32 hlba_src : 1;
		u32 hlba_sz : 2;
		u32 rsvd_3 : 4;
		u32 dec_force_crc_err : 1;
		u32 hlba_loc_start : 6;
		u32 dec_force_phy_pad_en : 1;
		u32 enc_force_phy_pad_en : 1;
		u32 dec_tag_num_max : 4;
		u32 enc_tag_num_max : 4;
		u32 dec_raid_par_num_max : 4;
		u32 force_user_scr_off : 1;
		u32 force_meta_scr_off : 1;
		u32 force_raid_du_sz : 1;
		u32 force_raid_min_cw_gap : 1;
	} b;
} eccu_ctrl_reg5_t1;
typedef union
{
	u32 all;
	struct
	{
		u32 fdma_conf_mode : 3;
		u32 fdma_conf_enc_dec_sel : 1;
		u32 fdma_conf_rd_wrb : 1;
		u32 fdma_conf_cmf_sel : 1;
		u32 rsvd_6 : 2;
		u32 fdma_conf_enc_init : 1;
		u32 fdma_conf_dec_init : 1;
		u32 fdma_conf_scr_lut_init : 1;
		u32 fdma_conf_xfer_rd_finish : 1;
		u32 rsvd_12 : 2;
		u32 fdma_conf_dec_rdy : 1;
		u32 fdma_conf_enc_rdy : 1;
		u32 fdma_conf_scr_lut_rdy : 1;
		u32 fdma_conf_seed_bypass : 1;
		u32 rsvd_18 : 13;
		u32 fdma_conf_intf_reset : 1;
	} b;
} eccu_fdma_cfg_reg_t1;

slow_data struct du_format_t1 du_fmt_tbl1[1] = {
	{
		.fmt0 = {
			.b.host_sector_sz = 512,
			.b.du2host_ratio = 4096 / 512,
#if defined(DISABLE_HS_CRC_SUPPORT)
			.b.hlba_mode = 1,
#else
			.b.hlba_mode = 0,
#endif
			.b.meta_sz = 32,
			.b.hlba_sz_20 = 1, // 1:4b
		},
		.fmt1 = {
			.b.ecc_cmf_sel = 0,
			.b.meta_crc_en = 1,
			.b.du_crc_6b_en = 0,
			.b.du_crc_en = 1,
			.b.ecc_bypass = 0,
			.b.ecc_inv_en = 0,
			.b.du_crc_inv_en = 0,
#if DEBUG
			.b.meta_scr_en = 0,
			.b.user_scr_en = 0,
#else
			.b.meta_scr_en = 1,
			.b.user_scr_en = 1,
#endif
			.b.cw_num_du = (4096 / 4096),
			.b.scr_lut_en = 1,
			// Don't change to 1, see ticket #3978
			.b.scr_seed_sel = 1, // meta as seed
			.b.ecc_core_sel = 0,
			.b.bch_t = 0,
		},
	}

};
//joe add sec size 20200818
ddr_code void eccu_du_fmt_cfg1(int idx)//joe slow->ddr 20201124
{
	struct du_format_t1 *fmt;
	eccu_du_fmt_sel_reg_t1 fmt_sel;

	// Configure HW setting
	fmt_sel.all = readl((const void *)(0xc000001c));
	fmt_sel.b.du_fmt_cfg_idx = 2;
	//eccu_writel(fmt_sel.all, ECCU_DU_FMT_SEL_REG);
	writel(fmt_sel.all, (void *)(0xc000001c));
	//joe add 20200814  init sec size, need to read from nand?
	fmt = &du_fmt_tbl1[idx];
	if (host_sec_bitz == 9)
	{
		fmt->fmt0.b.host_sector_sz = 512; //joe add 20200814 host sec
		fmt->fmt0.b.du2host_ratio = 8;
	}
	else
	{
		fmt->fmt0.b.host_sector_sz = 4096; //joe add 20200814 host sec
		fmt->fmt0.b.du2host_ratio = 1;
	}

	//eccu_writel(fmt->fmt0.all, ECCU_DU_FMT_REG0);
	writel(fmt->fmt0.all, (void *)(0xc0000020));
	//eccu_writel(fmt->fmt1.all, ECCU_DU_FMT_REG1);
	writel(fmt->fmt1.all, (void *)(0xc0000024));

	//eccu_writel(ECCU_OVRLMT_THD, ECCU_DU_FMT_REG3);
	writel(300, (void *)(0xc000002c));
}
ddr_code void eccu_du_fmt_init1(int fmt_id)//joe slow->ddr 20201124
{
	struct du_format_t1 *fmt;
	u16 usr_sz, ecc_sz, par_sz;

	fmt = &du_fmt_tbl1[fmt_id];

	//fmt->fmt4.all = eccu_readl(ECCU_DU_FMT_REG4);
	fmt->fmt4.all = readl((const void *)(0xc0000030));
	//fmt->fmt5.all = eccu_readl(ECCU_DU_FMT_REG5);
	fmt->fmt4.all = readl((const void *)(0xc0000034));

	usr_sz = fmt->fmt0.b.meta_sz;
	if (fmt->fmt1.b.meta_crc_en)
	{
		usr_sz += 4;
	}
	usr_sz += fmt->fmt0.b.host_sector_sz * fmt->fmt0.b.du2host_ratio;
	eccu_ctrl_reg5_t1 ctrl_reg;
	//ctrl_reg.all = eccu_readl(ECCU_CTRL_REG5);
	ctrl_reg.all = readl((const void *)(0xc0000018));
	if (ctrl_reg.b.hlba_src)
	{
		switch (fmt->fmt0.b.hlba_mode)
		{
		case 0:
		break;
		case 1:
		usr_sz += fmt->fmt0.b.hlba_sz_20 * 4;
		break;
		case 2:
		usr_sz += fmt->fmt0.b.hlba_sz_20 * 4 * fmt->fmt0.b.du2host_ratio;
		break;
		default:
		sys_assert(0);
		break;
		}
	}
	if (fmt->fmt1.b.du_crc_en)
	{
		if (fmt->fmt1.b.du_crc_6b_en)
		{
		usr_sz += 6;
		}
		else
		{
		usr_sz += 4;
		}
	}

	eccu_fdma_cfg_reg_t1 fdma_reg;
	//fdma_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
	fdma_reg.all = readl((const void *)(0xc0000080));
	fdma_reg.b.fdma_conf_cmf_sel = fmt->fmt1.b.ecc_cmf_sel;
	//eccu_writel(fdma_reg.all, ECCU_FDMA_CFG_REG);
	writel(fdma_reg.all, (void *)(0xc0000080));
	//fmt->fmt6.all = eccu_readl(ECCU_DU_FMT_REG6);
	fmt->fmt6.all = readl((const void *)(0xc0000038));
	par_sz = fmt->fmt6.b.ecc_max_par_sz;
	if (fmt->fmt1.b.ecc_bypass == 0x3)
	{
		par_sz = 0;
	}
	else if (!par_sz)
	{
		par_sz = 256; //Encoder 03 nand_get_cmf_parity_size();
	}
	ecc_sz = par_sz * fmt->fmt1.b.cw_num_du;
	fmt->encoded_ecc_du_sz = usr_sz + ecc_sz;
	fmt->encoded_ecc_du_sz_2align =
	round_up_by_2_power(fmt->encoded_ecc_du_sz, 2);

	//ncb_eccu_trace(LOG_INFO, 0, "DU fmt %d size 0x%x", fmt_id, fmt->encoded_ecc_du_sz_2align);
	//if (fmt->encoded_ecc_du_sz_2align * 4 > nand_whole_page_size()) {
	/*if ((fmt_id != 0) && (fmt_id != 3)) {
	ncb_eccu_trace(LOG_INFO, 0, "DU %d sz error, pg size: %x", fmt_id,
	nand_whole_page_size());
	sys_assert(0);
	}*/
	//}
	//fmt->fmt2.all = eccu_readl(ECCU_DU_FMT_REG2);
	fmt->fmt2.all = readl((const void *)(0xc0000028));

	fmt->fmt2.b.phy_pad_sz = 0;

	fmt->fmt2.b.erase_det_thd = 0xFF; // Temp set maximum
	//eccu_writel(fmt->fmt2.all, ECCU_DU_FMT_REG2);
	writel(fmt->fmt2.all, (void *)(0xc0000028));
}
//joe add sec size 20200818

//joe add fmt function 20200908
extern volatile u8 plp_trigger;

bool ddr_code format_sec(u8 lbads, u8 lbaf, u8 eccu_setting)//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "ctrlr->drive_capacity1:%x ",ctrlr->drive_capacity);

		nvme_apl_trace(LOG_INFO, 0xc270, "format1  hostsecbitz :%d  lbads:%d",host_sec_bitz,lbads);
		ctrlr->drive_capacity = ctrlr->drive_capacity << (host_sec_bitz);
		u8 nsidd = 0;
		for (nsidd = 0; nsidd < 32; nsidd++)
		{ //joe add change id-ns ncap,nsze,lbaf

			if (ctrlr->nsid[nsidd].type != NSID_TYPE_UNALLOCATED)
			{
				nvme_apl_trace(LOG_ERR, 0xbb77, "ctrlr->nsid[%d].ns->ncap1:%x  ", nsidd, ctrlr->nsid[nsidd].ns->ncap);
				ctrlr->nsid[nsidd].ns->ncap = ctrlr->nsid[nsidd].ns->ncap << (host_sec_bitz);
				ctrlr->nsid[nsidd].ns->nsze = ctrlr->nsid[nsidd].ns->nsze << (host_sec_bitz);
				ctrlr->nsid[nsidd].ns->lbaf = lbaf;
			}
		}

		host_sec_bitz = lbads;
		host_sec_size = 1 << host_sec_bitz;
		ctrlr->drive_capacity = ctrlr->drive_capacity >> host_sec_bitz;
		// nvme_apl_trace(LOG_ERR, 0, "ctrlr->drive_capacity2:%x  ",ctrlr->drive_capacity);
		u8 nsiddd = 0;
		for (nsiddd = 0; nsiddd < 32; nsiddd++)
		{

			if (ctrlr->nsid[nsiddd].type != NSID_TYPE_UNALLOCATED)
			{
				ctrlr->nsid[nsiddd].ns->ncap = ctrlr->nsid[nsiddd].ns->ncap >> host_sec_bitz;
				ctrlr->nsid[nsiddd].ns->nsze = ctrlr->nsid[nsiddd].ns->nsze >> host_sec_bitz;
				nvme_apl_trace(LOG_ERR, 0x09e0, "ctrlr->nsid[%d].ns->ncap2:%x  ", nsiddd, ctrlr->nsid[nsiddd].ns->ncap);
			}
		}

		// 1. btncore_resume
		btn_control_reg_t btn_ctrl = {
		.all = readl((void *)(0xc0010044)),
		};
		btn_ctrl.b.host_sector_size = (lbads == 12) ? 1 : 0;
		#if defined(HMETA_SIZE)
			if(_lbaf_tbl[lbaf].ms!=0){
			btn_ctrl.b.host_meta_enbale = 1;
			//btn_ctrl.b.host_meta_enbale = 0;//joe test 20210125
			btn_ctrl.b.host_meta_16byte = (HMETA_SIZE > 8) ? 1 : 0;
			btn_ctrl.b.host_meta_size_mix = (HMETA_SIZE > 8) ? 1 : 0;
			}
		#endif
		writel(btn_ctrl.all, (void *)(0xc0010044));


		#ifndef HMETA_SIZE
		// 2. eccu
		 struct du_format_t1 *fmt1 = &du_fmt_tbl1[0];
		 fmt1->fmt0.b.host_sector_sz = host_sec_size; //joe add 20200814 host sec
		 fmt1->fmt0.b.du2host_ratio = NAND_DU_SIZE >> (host_sec_bitz);
		 eccu_du_fmt_cfg1(0);
		 // Update SW setting
		 eccu_du_fmt_init1(0);

		#else
		//change CMF

		if(eccu_setting)
		{
			eccu_switch_setting(lbaf);
		}
		#if (PI_FW_UPDATE == mENABLE)
		switch(lbaf)
		{
			case 0 : //512
				cmf_idx = 1;
				break;
			case 1 : //512+8
				cmf_idx = 3;
				break;
			case 2 : //4096
				cmf_idx = 2;
				break;
			case 3 : //4096+8
				cmf_idx = 4;
				break;
			default :
				panic("imp");
				break;
		}
		#endif
		#endif



		// 3. ns setting
		u8 nsidddd = 1;
		for (nsidddd = 1; nsidddd < 33; nsidddd++)
		{
			//nvme_ns_attr_t *ns_attr = get_ns_info_slot(nsiddd);
			nvme_ns_attr_t *ns_attr = &ns_array_menu->ns_attr[nsidddd - 1];
			ns_attr->hw_attr.lbaf = lbaf;
			ns_attr->fw_attr.ncap = ctrlr->nsid[nsidddd - 1].ns->ncap;
			ns_attr->fw_attr.nsz = ctrlr->nsid[nsidddd - 1].ns->nsze;
			ns_attr->hw_attr.lb_cnt = ns_attr->fw_attr.ncap;
			#if defined(HMETA_SIZE)
			ns_attr->hw_attr.pit = ctrlr->nsid[nsidddd - 1].ns->pit;
			ns_attr->hw_attr.pil = ctrlr->nsid[nsidddd - 1].ns->pil;
			ns_attr->hw_attr.ms = ctrlr->nsid[nsidddd - 1].ns->ms;
			#endif
			//nvme_apl_trace(LOG_ERR, 0, "ns %d type:%d\n", ns_attr->hw_attr.nsid, ctrlr->nsid[nsidddd - 1].type);
			if (ctrlr->nsid[nsidddd - 1].type == NSID_TYPE_ACTIVE)
			{
				nvmet_set_ns_attrs(ns_attr, true);
				nvme_apl_trace(LOG_ERR, 0x2f62, "ns %d active\n", nsidddd);
			}
			if (ctrlr->nsid[nsidddd - 1].type == NSID_TYPE_ALLOCATED)
			{
				nvmet_set_ns_attrs_init(ns_attr, false);
				nvme_apl_trace(LOG_ERR, 0xa468, "ns %d allocated\n", nsidddd);
			}

			//nvme_apl_trace(LOG_ERR, 0, "ns %d OK\n", nsidddd);
		}
		ns_array_menu->host_sec_bits = host_sec_bitz;
		nvme_apl_trace(LOG_INFO, 0xefef, "format2  hostsecbitz :%d  lbads:%d",host_sec_bitz,lbads);
		nvme_apl_trace(LOG_INFO, 0x19a9, "ns_array_menu->ns_attr[0].hw_attr.lbaf:%d",ns_array_menu->ns_attr[0].hw_attr.lbaf);

	epm_update(NAMESPACE_sign, (CPU_ID - 1)); //joe add update epm 20200908
	if(plp_trigger)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
#endif //joe end if 20200916

#if (TRIM_SUPPORT == ENABLE)
bool ddr_code nvme_format_trim_handle_wait()
{
	u32 act = 0;
    TrimInfo.FullTrimTriggered = 1;
	FullTrimHandle((void *)INV_U32, false);
#ifdef While_break
	u64 start = get_tsc_64();
#endif
	while(TrimInfo.FullTrimTriggered == 1)
	{
		if(plp_trigger)
		{
			return 1;
		}

		if (AplPollingResetEvent() == true)
		{
		    nvme_apl_trace(LOG_INFO, 0xa0e3, "AplPollingResetEvent() == true, gFormatFlag:%u", gFormatFlag);
			gFormatInProgress = true;
			return 1;
		}


        dpe_isr();
		act = (vic_readl(RAW_INTERRUPT_STATUS) & (u32)1 << VID_CPU_MSG_INT);
		if(act)
		{
			cpu_msg_isr();
		}
#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}
	vic_writel(act , VECTOR_ADDRESS);
	return 0;
}
#endif

//joe add fmt function 20200908
extern bool shr_format_fulltrim_flag;

ddr_code void format_post_setup(void)
{
	mdelay_plp(2500);
	#if defined(HMETA_SIZE)
		if(_lbaf_tbl[ns_array_menu->ns_attr[0].hw_attr.lbaf ].ms==0)
			host_du_fmt = DU_FMT_USER_4K;
		else
			host_du_fmt = DU_FMT_USER_4K_HMETA;
	#else
		host_du_fmt = DU_FMT_USER_4K;
	#endif
	//format_sec(_lbaf_tbl[ns->lbaf].lbads, ns->lbaf);

#if(degrade_mode == ENABLE)
	extern ddr_code void degrade_mode_fmt_reset_io();
	degrade_mode_fmt_reset_io();
#endif

	gc_re();
	nvmet_io_fetch_ctrl(false);
	gFormatFlag = false;
	shr_format_fulltrim_flag = false;

#if (PLP_SUPPORT == 0)
	extern volatile u8 non_plp_format_type;
	if(non_plp_format_type == NON_PLP_FORMAT)
	{
//		epm_update(FTL_sign, (CPU_ID-1));	//NON-PLP need update epm again to clean epm format tag
		non_plp_format_type = 0;
	}
#endif

	nvme_apl_trace(LOG_INFO, 0x95a7, "format finish");
	//flush_to_nand(EVT_NVME_FORMATE);
}

//-------joe add for nvme format cmd use 20200820--------//
/*!
 * @brief format namespace and setup PI setting
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
static ddr_code __attribute__((unused)) enum cmd_rslt_t

nvmet_format_ns(req_t *req, struct nvme_cmd *cmd)
{
	union nvme_format format;
	struct nvmet_namespace *ns = NULL;
	//u8 nss_crypto=0;//joe add for IOL1.4 1.6 20201202
	format.all = cmd->cdw10;

	nvme_apl_trace(LOG_INFO, 0x1334, "Format: NSID(%d) LBAF(%d) MS(%d) PI(%d) SES(%d)", cmd->nsid, format.b.lbaf, format.b.mset, format.b.pi, format.b.ses);

	if (cmd->nsid == ~0)
	{ ///< all namespace
	#if (NS_MANAGE != FEATURE_SUPPORTED)
		cmd->nsid = 1;
	#endif
//#if (NS_MANAGE == FEATURE_SUPPORTED) // don't support broadcase format//joe marked for iol14 1.6 case  20201202
	//	nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
		//				 NVME_SC_INVALID_FORMAT);
		//return HANDLE_RESULT_FAILURE;
//#endif
	}

	#if(degrade_mode == ENABLE)
		extern none_access_mode_t noneaccess_mode_flags;
		if(noneaccess_mode_flags.b.defect_table_fail || noneaccess_mode_flags.b.tcg_key_table_fail){
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);
			evlog_printk(LOG_INFO,"[NON-ACCESS-MODE] cmd->opc : %x",cmd->opc);
			return HANDLE_RESULT_FAILURE;
		}
	#endif

	//Andy 20201113 Drive master
	if(cmd->nsid == 0 || ((cmd->nsid > NVMET_NR_NS)&&(cmd->nsid !=~0)))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}

	#if(NS_MANAGE != FEATURE_SUPPORTED)
		ns = ctrlr->nsid[cmd->nsid - 1].ns;
	#else
		if(cmd->nsid==~0)
			ns = ctrlr->nsid[0].ns;
		else
			ns=ctrlr->nsid[cmd->nsid - 1].ns;
	#endif

	//Eric 20230605 For EDVX
	#if !defined(PI_SUPPORT)

	if (format.b.pi != 0 && cmd->nsid == ~0){
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
							NVME_SC_INVALID_FORMAT);
		return HANDLE_RESULT_FAILURE;
		}

	if(format.b.pi != 0){
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
		}

	#else

	#endif
	//if (format.b.lbaf !=0 && format.b.lbaf !=1)//0=512 1=4096
	if (format.b.lbaf >= ns->lbaf_cnt || format.b.pi > ns->npit)//modify by suda
	{
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}

	if((cmd->nsid !=~0)){
		if(ctrlr->nsid[cmd->nsid - 1].type != NSID_TYPE_ACTIVE){
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}

		if (ns_array_menu->total_order_now > 1)
		{
			nvme_ns_attr_t *ns_attr = &ns_array_menu->ns_attr[cmd->nsid - 1];

			if ((ns_attr->hw_attr.lbaf != format.b.lbaf) || (ns_attr->hw_attr.ms != format.b.mset) || (ns_attr->hw_attr.pit != format.b.pi) || (ns_attr->hw_attr.pil != format.b.pil))
			{
				nvme_apl_trace(LOG_DEBUG, 0x8fbb, "mlt ns,change with nsid 0xffffffff");
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
								 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
				return HANDLE_RESULT_FAILURE;
			}

			//edevx dShallFail & BIT8 case
			if ((ns_attr->hw_attr.lbaf == format.b.lbaf) && (ns_attr->hw_attr.ms == format.b.mset) && (ns_attr->hw_attr.pit == format.b.pi) && (ns_attr->hw_attr.pil == format.b.pil)
#if defined(USE_CRYPTO_HW)
				&& (format.b.ses > NVME_FMT_NVM_SES_CRYPTO_ERASE))
#else
				&& (format.b.ses >= NVME_FMT_NVM_SES_CRYPTO_ERASE))
#endif
			{
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
								 NVME_SC_INVALID_FIELD);
				return HANDLE_RESULT_FAILURE;
			}
		}

		if (ns_array_menu->total_order_now == 1)
		{
#if defined(USE_CRYPTO_HW)
			if (format.b.ses > NVME_FMT_NVM_SES_CRYPTO_ERASE)
#else
			if (format.b.ses >= NVME_FMT_NVM_SES_CRYPTO_ERASE)
#endif
			{
				nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
								 NVME_SC_INVALID_FORMAT);
				return HANDLE_RESULT_FAILURE;
			}
		}
	}
	//broacast mode value setting valid testing
	else
	{
		u8 cnt = 0;
		for (u8 i = 0; i < NVMET_NR_NS; i++)
		{
			if (ns_array_menu->ns_attr[i].fw_attr.type == NSID_TYPE_ALLOCATED)
			{
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
								 NVME_SC_INVALID_FIELD);
				return HANDLE_RESULT_FAILURE;
			}

			if(ns_array_menu->ns_attr[i].fw_attr.type == NSID_TYPE_ACTIVE)
				cnt = cnt + 1;
		}

		if (cnt == 0)
		{
			//nvme_apl_trace(LOG_DEBUG, 0xdd61, "zero active ns");

			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						NVME_SC_INVALID_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}
#if defined(USE_CRYPTO_HW)
		if (format.b.ses > NVME_FMT_NVM_SES_CRYPTO_ERASE)
#else
		if (format.b.ses >= NVME_FMT_NVM_SES_CRYPTO_ERASE)
#endif
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}

	}

	////Andy 20200922 modify change "format.b.ses > NVME_FMT_NVM_SES_USER_DATA_ERASE" to " format.b.ses > NVME_FMT_NVM_SES_CRYPTO_ERASE"
#if defined(USE_CRYPTO_HW)
	if (format.b.ses > NVME_FMT_NVM_SES_CRYPTO_ERASE
#else
	if (format.b.ses >= NVME_FMT_NVM_SES_CRYPTO_ERASE
#endif
		|| ((format.b.pi != 0) && (0 == _lbaf_tbl[format.b.lbaf].ms))
#ifndef PI_WORKAROUND
		|| ((format.b.pil == 0) && (_lbaf_tbl[format.b.lbaf].ms > 8)) //< workaround issue #5817
#endif
	)
	{
//mark for IOL -> this is default setting
#if 1 // reopen for edevx test
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_FORMAT);
#else
		//andy IOL 1.6 Case 4 20201020
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
#endif
		return HANDLE_RESULT_FAILURE;
	}

	////Andy invalid LBAF use 0A error code:NVME_SC_INVALID_FORMAT
	//if (format.b.lbaf >= ns->lbaf_cnt)
	//{
	//	nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
	//					 NVME_SC_INVALID_FORMAT);
	//	return HANDLE_RESULT_FAILURE;
	//}
#if CO_SUPPORT_DEVICE_SELF_TEST//eric 24040131
extern tDST_LOG *smDSTInfo;
if (cmd->nsid == smDSTInfo->DSTResult.NSID || cmd->nsid==0xFFFFFFFF) //Eric 20230707 for DST spec
	{
		DST_Completion(cDSTAbortFormat);
	}
if (smDSTInfo->DSTResult.NSID==0xFFFFFFFF && ctrlr->nsid[cmd->nsid - 1].type == NSID_TYPE_ACTIVE)
	{
		//nvme_apl_trace(LOG_INFO, 0xc596, "[DST] format trigger");
		DST_Completion(cDSTAbortFormat);
	}
#endif

//Andy add for crypto erase
#if defined(USE_CRYPTO_HW)
	if (format.b.ses == NVME_FMT_NVM_SES_CRYPTO_ERASE || format.b.ses == NVME_FMT_NVM_SES_NO_SECURE_ERASE)
	{
		u8 ns_start = cmd->nsid;
		u8 ns_end = cmd->nsid;
		u8 ns_cur;

		if(cmd->nsid == ~0){
			ns_start = 1;
			ns_end = NVMET_NR_NS;
		}

		for(ns_cur = ns_start; ns_cur <= ns_end; ns_cur++){
	#if (_TCG_)
			extern bool bKeyChanged;
			extern void TcgChangeKey(u8 idx);
			extern void tcg_init_aes_key_range(void);
			extern u8 tcg_nf_G3Wr(bool G5_only);
		#ifdef NS_MANAGE
			TcgChangeKey(LOCKING_RANGE_CNT + ns_cur);
			if(nonGR_set && (ns_array_menu->total_order_now==1) && (ns_cur == ns_array_menu->array_order[0]+1))
			{
				for(u8 idx=1; idx<=LOCKING_RANGE_CNT; idx++)
				{
					TcgChangeKey(idx);
				}
			}

		#else
			TcgChangeKey(0);
			if(nonGR_set)
			{
				for(u8 idx=1; idx<=LOCKING_RANGE_CNT; idx++)
				{
					TcgChangeKey(idx);
				}
			}
		#endif  // ifdef NS_MANAGE
			tcg_init_aes_key_range();
			tcg_nf_G3Wr(false);
			bKeyChanged = false;
	#else
			crypto_change_mode_range(3, ns_cur, 1, ns_cur); //Andy 20200921
	#endif
#if 1
#ifdef ERRHANDLE_ECCT      //Tony 20201120
	        //epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
	        //pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
	        //if(pECC_table->ecc_table_cnt)     //Search same idx of lda in ecct
	        if(ecct_cnt)
	        {
	            u64 slba = 0;
	            u32 lba_cnt = 0;
	            if(host_sec_bitz == 9)
	            {
	                slba = ns_array_menu->ns_array[ns_cur - 1].sec_id[0] * NS_SIZE_GRANULARITY_ADMIN1;
	                lba_cnt = (ns_array_menu->ns_array[ns_cur - 1].sec_num) * NS_SIZE_GRANULARITY_ADMIN1;
	            }
	            else
	            {
	                slba = ns_array_menu->ns_array[ns_cur - 1].sec_id[0] * NS_SIZE_GRANULARITY_ADMIN2;
	                lba_cnt = (ns_array_menu->ns_array[ns_cur - 1].sec_num) * NS_SIZE_GRANULARITY_ADMIN2;
	            }

	            //stECCT_ipc_t *ecct_info;
	            rc_ecct_info[rc_ecct_cnt].lba       = slba;
	            rc_ecct_info[rc_ecct_cnt].source    = ECC_REG_VU;
	            rc_ecct_info[rc_ecct_cnt].total_len = lba_cnt;
	            rc_ecct_info[rc_ecct_cnt].type      = VSC_ECC_unreg;

                nvme_apl_trace(LOG_INFO, 0xf46d, "[NVM] nvmet_format_ns Unregister ECCT");
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
#endif
		}


	}
#else
	if(format.b.ses == NVME_FMT_NVM_SES_CRYPTO_ERASE){
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}

	if(plp_trigger)//power loss skip format
	{
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}
    
	if (1)
	{
		u8 ns_start = cmd->nsid;
		u8 ns_end = cmd->nsid;
		u8 ns_cur;

		if(cmd->nsid == ~0){
			ns_start = 1;
			ns_end = NVMET_NR_NS;
		}

		for(ns_cur = ns_start; ns_cur <= ns_end; ns_cur++){
#if 1
#ifdef ERRHANDLE_ECCT      //Tony 20201120
	        //epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
	        //pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
	        //if(pECC_table->ecc_table_cnt)     //Search same idx of lda in ecct
	        if(ecct_cnt)
	        {
	            u64 slba = 0;
	            u32 lba_cnt = 0;
	            if(host_sec_bitz == 9)
	            {
	                //slba = ns_array_menu->ns_array[ns_cur - 1].sec_id[0] * NS_SIZE_GRANULARITY_RDISK1;
	                //lba_cnt = (ns_array_menu->ns_array[ns_cur - 1].sec_num) * NS_SIZE_GRANULARITY_RDISK1;
	                slba = ns_array_menu->ns_array[ns_cur - 1].sec_id[0] * NS_SIZE_GRANULARITY_ADMIN1;
	                lba_cnt = (ns_array_menu->ns_array[ns_cur - 1].sec_num) * NS_SIZE_GRANULARITY_ADMIN1;
	            }
	            else
	            {
	                //slba = ns_array_menu->ns_array[ns_cur - 1].sec_id[0] * NS_SIZE_GRANULARITY_RDISK2;
	                //lba_cnt = (ns_array_menu->ns_array[ns_cur - 1].sec_num) * NS_SIZE_GRANULARITY_RDISK2;
	                slba = ns_array_menu->ns_array[ns_cur - 1].sec_id[0] * NS_SIZE_GRANULARITY_ADMIN2;
	                lba_cnt = (ns_array_menu->ns_array[ns_cur - 1].sec_num) * NS_SIZE_GRANULARITY_ADMIN2;
	            }

	            //stECCT_ipc_t ecct_info;
	            rc_ecct_info[rc_ecct_cnt].lba       = slba;
	            rc_ecct_info[rc_ecct_cnt].source    = ECC_REG_VU;
	            rc_ecct_info[rc_ecct_cnt].total_len = lba_cnt;
	            rc_ecct_info[rc_ecct_cnt].type      = VSC_ECC_unreg;

                nvme_apl_trace(LOG_INFO, 0x59f8, "[NVM] nvmet_format_ns Unregister ECCT");
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
#endif
		}


	}

#endif
#if 1
	if(cmd->nsid == ~0)
    {
    	for(u32 i = 1; i <= NVMET_NR_NS; i++)
    	{
    		nvme_ns_attr_t *p_ns = get_ns_info_slot(i);
			p_ns->hw_attr.pit = format.b.pi;
			p_ns->hw_attr.pil = format.b.pil;
			p_ns->hw_attr.ms = format.b.mset;
			p_ns->hw_attr.lbaf = format.b.lbaf;
		}
	}
	else
	{
		nvme_ns_attr_t *p_ns = get_ns_info_slot(cmd->nsid);
		p_ns->hw_attr.pit = format.b.pi;
		p_ns->hw_attr.pil = format.b.pil;
		p_ns->hw_attr.ms = format.b.mset;
		p_ns->hw_attr.lbaf = format.b.lbaf;
	}

#endif

	//if(cmd->nsid!=~0)
	//{
	//	ns->pit = format.b.pi;
	//	ns->pil = format.b.pil;
	//	ns->ms = format.b.mset;
	//	ns->lbaf = format.b.lbaf;
	//}
	//else
	//{
	//	ns->pit = format.b.pi;
	//	ns->pil = format.b.pil;
	//	ns->ms = format.b.mset;
	//	ns->lbaf = format.b.lbaf;
		struct nvmet_namespace *ns_set = NULL;
		u8 ns_set_id=0;
		for(ns_set_id=0;ns_set_id<NVMET_NR_NS;ns_set_id++)
		{
			ns_set=ctrlr->nsid[ns_set_id].ns;
			if(ctrlr->nsid[ns_set_id].type != NSID_TYPE_UNALLOCATED)
			{
				ns_set->pit = format.b.pi;
				ns_set->pil = format.b.pil;
				ns_set->ms = format.b.mset;
				ns_set->lbaf = format.b.lbaf;
			}
		}
	//}
//#if defined(HMETA_SIZE)
	//if(_lbaf_tbl[ns->lbaf].ms!=0)
	//cmd_proc_hmeta_ctrl(format.b.pi, format.b.mset, format.b.pil, cmd->nsid, _lbaf_tbl[ns->lbaf].ms);
//#endif //HMETA_SIZE
	//nvme_apl_trace(LOG_INFO, 0x79c0, "format %x, meta %d", format.all, _lbaf_tbl[ns->lbaf].ms);
//joe 20200820  change sec size

#if (NS_MANAGE == FEATURE_SUPPORTED) //joe add  defined20200916

	u16 ns_sec_total = 0;
	u16 ns_sec_number = 0;
	u16 cc=0;

	if (!plp_trigger)
	{
		nvme_apl_trace(LOG_INFO, 0xbb6c, "format start l2p reset");
		//STOP FETCH IO
		//nvmet_io_fetch_ctrl(true);
		#if (TRIM_SUPPORT == ENABLE)
		TrimFlush();
		#endif
		shr_format_fulltrim_flag = true;
		//mdelay(5000);
		//nvme_apl_trace(LOG_INFO, 0, "2. format ns:%d\n",TrimInfo.TrimFlush);
		//do
		//{
			//nvme_apl_trace(LOG_INFO, 0, "wait trim flush flag\n");
			//nvme_apl_trace(LOG_INFO, 0, "3. format ns:%d\n",TrimInfo.TrimFlush);
			//cpu_msg_isr();
		//}while(TrimInfo.TrimFlush == 1);//waite Flush done
		//mdelay(500);
        //if (format.b.lbaf==ns_array_menu->ns_attr[0].hw_attr.lbaf)
    	//if(format.b.mset == ns_array_menu->ns_attr[0].hw_attr.ms)// pi change or not
		if(_lbaf_tbl[format.b.lbaf].ms == _lbaf_tbl[ns_array_menu->ns_attr[0].hw_attr.lbaf].ms
			&& (format.b.ses != NVME_FMT_NVM_SES_USER_DATA_ERASE || (format.b.ses == NVME_FMT_NVM_SES_USER_DATA_ERASE && (!(cmd->nsid == ~0 || ns_array_menu->total_order_now == 1)))))
    	{
        	if(host_sec_bitz == _lbaf_tbl[ns->lbaf].lbads)
    		{
    			u8 pi_format_setting = 1;
    			if(cmd->nsid==~0)
    			{
					nvme_ns_attr_t *ns_attr = &ns_array_menu->ns_attr[0];
					if(ns_attr->hw_attr.ms == format.b.mset && ns_attr->hw_attr.pit == format.b.pi && ns_attr->hw_attr.pil == format.b.pil)
					{
						pi_format_setting = 0;
					}
				}
				else
				{
					nvme_ns_attr_t *ns_attr = &ns_array_menu->ns_attr[cmd->nsid - 1];
					if(ns_attr->hw_attr.ms == format.b.mset && ns_attr->hw_attr.pit == format.b.pi && ns_attr->hw_attr.pil == format.b.pil)
					{
						pi_format_setting = 0;
					}
				}

        		if(cmd->nsid != ~0 && ns_array_menu->total_order_now > 1 && pi_format_setting == 0)
    			{
        			ns_sec_total = ns_array_menu->ns_array[cmd->nsid - 1].sec_num;
        			nvme_apl_trace(LOG_INFO, 0x3930, "fmt1 ns start l2p reset\n");
					shr_ftl_save_qbt_flag = false;

                    if(nvme_format_stop_gc_wait())
					{
						return HANDLE_RESULT_FAILURE;//power loss return
					}
                
					shr_trim_vac_save_done = false;
					cpu_msg_issue(CPU_BE - 1, CPU_MSG_TRIM_CPY_VAC, 0, 1);
					while(shr_trim_vac_save_done == false)//wait cpu2 save vac table
					{
						if(plp_trigger)
							return HANDLE_RESULT_FAILURE;

						extern void btn_data_in_isr(void);
	        			btn_data_in_isr();
                        dpe_isr();  // for PBT raid bm_copy_done
                        cpu_msg_isr();  // for PBT raid bm_data_copy

					}
					shr_format_copy_vac_flag = true;

        			for (cc=0;cc<ns_sec_total;cc++)
    				{
			    		if(plp_trigger)
							return HANDLE_RESULT_FAILURE;

        				ns_sec_number = ns_array_menu->ns_array[cmd->nsid - 1].sec_id[cc];
        				ftl_l2p_partial_reset(ns_sec_number);// mark test for IOL1.4 1.6-4
        				mdelay_plp(100);//chk plp flag
        				//mdelay(100);
        			}
					/*
					flush_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(flush_ctx_t));
					sys_assert(ctx);
					ctx->nsid = 1;
					ctx->caller = NULL;
					ctx->flags.all = 0;
					ctx->flags.b.shutdown = 1;
					ctx->cmpl = ftl_ns_del_done;
					ftl_flush(ctx);
					*/
					cpu_msg_issue(CPU_FTL - 1, CPU_MSG_DEL_NS_FTL_HANDLE, 0, 0);
					while(shr_ftl_save_qbt_flag == false){
						if(plp_trigger)
							return HANDLE_RESULT_FAILURE;

						extern void btn_data_in_isr(void);
	        			btn_data_in_isr();
                        dpe_isr();  // for PBT raid bm_copy_done
                        cpu_msg_isr();  // for PBT raid bm_data_copy
					}
        			//internal_trim_flag=0;
        			nvme_apl_trace(LOG_INFO, 0xf61c, "fmt1 ns end l2p reset");
        		} else {
        			#if (TRIM_SUPPORT == ENABLE)
					nvme_apl_trace(LOG_INFO, 0xfa59, "fmt1 nsid:0xFFFF start l2p reset\n");
					//STOP GC
					if(nvme_format_stop_gc_wait())
					{
						return HANDLE_RESULT_FAILURE;//power loss return
					}

					if(pi_format_setting)
					{
						evlog_printk(LOG_ERR,"pi_format_setting");
						if(format_sec(_lbaf_tbl[ns->lbaf].lbads, ns->lbaf,true))
						{
							return HANDLE_RESULT_FAILURE;//power loss return
						}
					}

					if(nvme_format_trim_handle_wait())
					{
						return HANDLE_RESULT_FAILURE;//power loss return
					}

    				nvme_apl_trace(LOG_INFO, 0x88ce, "fmt1  nsid:0xFFFF end l2p reset\n");
                    #else
                    nvme_apl_trace(LOG_INFO, 0x61fb, "No support trim ,format fail\n");
                    #endif
				}
			}
			else
			{
                if (_lbaf_tbl[format.b.lbaf].ms == 8)
				{
					evlog_printk(LOG_ALW, "[format]preformat1 host_du_fmt %d", host_du_fmt);
					vsc_preformat(req, false);
            		return HANDLE_RESULT_PENDING_BE;
                } else {
            		#if (TRIM_SUPPORT == ENABLE)
					nvme_apl_trace(LOG_INFO, 0x0921, "fmt1 nsid:0xFFFF start l2p reset\n");
					//STOP GC
					if(nvme_format_stop_gc_wait())
					{
						return HANDLE_RESULT_FAILURE;//power loss return
					}

					if(format_sec(_lbaf_tbl[ns->lbaf].lbads, ns->lbaf,true))
					{
						return HANDLE_RESULT_FAILURE;//power loss return
					}

					if(nvme_format_trim_handle_wait())
					{
						return HANDLE_RESULT_FAILURE;//power loss return
					}

        			nvme_apl_trace(LOG_INFO, 0x69b2, "fmt1  nsid:0xFFFF end l2p reset\n");
                    #else
                    nvme_apl_trace(LOG_INFO, 0xa28b, "No support trim ,format fail\n");
                    #endif
                }
        	}
    	}
    	else
    	{
			evlog_printk(LOG_ALW, "[format]preformat2 host_du_fmt %d", host_du_fmt);
			btn_de_wr_disable();
    		vsc_preformat(req, false);
    		return HANDLE_RESULT_PENDING_BE;
    	}
    	//gc_re();
    	//nvmet_io_fetch_ctrl(false);
	}
	else
	{
		return HANDLE_RESULT_FAILURE;//power loss return
	}

	//mdelay(2500);
	//nvme_apl_trace(LOG_INFO, 0, "fmt start");
	//format_sec(_lbaf_tbl[ns->lbaf].lbads, ns->lbaf);
	//nvme_apl_trace(LOG_INFO, 0, "fmt end");
	// #if defined(HMETA_SIZE)
	// 	if(_lbaf_tbl[ns_array_menu->ns_attr[0].hw_attr.lbaf ].ms==0)
	// 		host_du_fmt = DU_FMT_USER_4K;
	// 	else
	// 		host_du_fmt = DU_FMT_USER_4K_HMETA;
	// #else
	// 	host_du_fmt = DU_FMT_USER_4K;
	// #endif
	// //format_sec(_lbaf_tbl[ns->lbaf].lbads, ns->lbaf);
	//
	//
	// gc_re();
	// nvmet_io_fetch_ctrl(false);
	// gFormatFlag = false;
	// shr_format_fulltrim_flag = false;
	// epm_format_state_update(0);
	// nvme_apl_trace(LOG_INFO, 0, "format finish");
	//flush_to_nand(EVT_NVME_FORMATE);

	format_post_setup();
	if(shr_format_copy_vac_flag)
	{
		//shr_format_copy_vac_flag = false;
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_TRIM_CPY_VAC, 0, 0);
	}
	return HANDLE_RESULT_FINISHED;
#else

	/* submit command */
	req->opcode = REQ_T_FORMAT;
	req->state = REQ_ST_DISK;
	req->op_fields.format.meta_enable = (_lbaf_tbl[ns->lbaf].ms) ? true : false;
	req->op_fields.format.erase_type = format.b.ses;
	req->nsid = cmd->nsid;

	if (ctrlr->nsid[cmd->nsid - 1].ns->issue(req))
		return HANDLE_RESULT_RUNNING;

	return HANDLE_RESULT_FINISHED;

#endif //joe end define NS
}

#if (NS_MANAGE == FEATURE_SUPPORTED)
/*!
 * @brief admin get un-allocated namespace id
 *
 * @param		not used
 *
 * @return		u16, un-allocated namespace id
 */
static ddr_code u16 nvmet_get_ns_id(void)
{
	u8 i = 0;
	for (i = 0; i < ctrlr->max_ns; i++)
	{
		if (ctrlr->nsid[i].type == NSID_TYPE_UNALLOCATED)
			return ctrlr->nsid[i].nsid;
	}
	return (NVMET_NR_NS + 1); // invalid value, all ns are allocated.
}

/*!
 * @brief get start lba of new created namespace
 *
 * @param		u32, nsid
 *
 * @return		u64, start lba
 */
static ddr_code u64 nvmet_create_ns_start_lba(u32 ns_id)
{
	u64 start_lba = 0;
	if (ns_id != 1)
	{
		start_lba = ctrlr->nsid[ns_id - 2].ns->start_lba + ctrlr->nsid[ns_id - 2].ns->nsze;
	}
	return start_lba;
}

/*!
 * @brief admin get namespace write protect state
 *
 * @param		u8, nsid
 *
 * @return		u8, write protect state
 */
UNUSED static ddr_code u8 nvmet_get_ns_wp_state(u8 nsid)
{
	return ctrlr->nsid[nsid - 1].wp_state;
}

/*!
 * @brief admin change AES KEY for NSID
 *
 * @param		u8, aes key id
 * @param		u8, nsid
 *
 * @return		u8, write protect state
 */
UNUSED static ddr_code void nvmet_set_crypto_key(u8 key_id, u8 nsid)//joe slow->ddr 20201124
{
	nvme_apl_trace(LOG_INFO, 0xe83b, "Empty function, change AES key: %d for NSID: %d", key_id, nsid);
	//crypto_prog_entry(key_id, entry);
}

/*!
 * @brief admin Command namespace management DMA transfer Done(IN & OUT)
 *
 * @param payload	should be request pointer
 *
 * @return		not used
 */

static ddr_code void nvmet_admin_ns_manage_xfer_done(void *payload, bool error)//joe slow->ddr 20201124
{
	req_t *req = (req_t *)payload;
	u32 ns_id = req->op_fields.ns_ctrl.ns_id;
	//joe add NS 20200813
	// u16 new_sec_num=0;//joe add 20200730
	u64 origin_nsze = 0; //joe add 20200730 //20200715  fix the total_nsze over the drive_capacity
	u64 valid_nsze = 0;	 //joe for IOL 9.2.3 20200805
	//u16 nsid_cnt=0;//joe add for edevx NNMTID.o 20201217
	//u8 valid_ns_flag=0;//joe add for edevx NNMTID.o 20201217

	nvme_ns_attr_t *ns_attr = get_ns_info_slot(ns_id);

	//sys_assert(error == false);
	if (error == true) {
		nvme_xfer_err_rls_source(req);
		return;
	}

	/* less than 4K for namespace management */
	if (++req->req_prp.transferred == req->req_prp.required)
	{
		dtag_t *dtag = (dtag_t *)req->req_prp.mem;
		ns_manage_t *ns_manage = (ns_manage_t *)dtag2mem(dtag[0]);
		struct nvmet_nsid *nsid_ctrl = (struct nvmet_nsid *)(&ctrlr->nsid[ns_id - 1]);

		// nvme_apl_trace(LOG_INFO, 0, "JOE capacity nsid: %d start: %x nsze: 0x%x",
		//	ns_id, nsid_ctrl->ns->start_lba, ns_manage->nsze);//joe 20200730
		origin_nsze = ns_manage->nsze; //joe add 20200730 //joe 20200715  fix the total_nsze over the drive_capacity

		// step 1. valid check
		if (ns_manage->flbas.format >= MAX_LBAF)
		{ ///< invalid lbaf format
			nvme_apl_trace(LOG_ERR, 0x8180, "[create fail] lba format nsid: %d lbaf: %d",ns_id, ns_manage->flbas.format);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
			goto NS_MANAGE_RLS;
		}

		if ((ns_manage->nsze != ns_manage->ncap))//joe add 20210104  we don't surpport thin provision
		{ //joe add sec size 20200820  prevent not the using sec size
			nvme_apl_trace(LOG_ERR, 0x5a96, "[create fail] (nsze:%x != ncap:%x)", ns_manage->nsze, ns_manage->ncap);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_THINPROVISIONING_NOT_SUPPORTED);		// Thin OP not support status. Drive master 2023/8/16 Richard Lu.
			goto NS_MANAGE_RLS;
		}

		//if ((host_sec_bitz != _lbaf_tbl[ns_manage->flbas.format].lbads)&&ns_array_menu->total_order_now!=0)
		if ((ns_array_menu->ns_attr[0].hw_attr.lbaf != ns_manage->flbas.format)&&ns_array_menu->total_order_now!=0)//joe change 20210105
		{ //joe add sec size 20200820  prevent not the using sec size
			nvme_apl_trace(LOG_ERR, 0xeaf6, "[create fail] 2 lba format org_lbaf: %d cur_lbaf: %d",ns_array_menu->ns_attr[0].hw_attr.lbaf, ns_manage->flbas.format);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
			goto NS_MANAGE_RLS;
		}
		//ns->lbaf=ns_manage->flbas.format;//joe add 20201218

		if(ns_array_menu->total_order_now == 0)//create the first ns
		{

			if(_lbaf_tbl[ns_manage->flbas.format].ms == _lbaf_tbl[ns_array_menu->ns_attr[0].hw_attr.lbaf].ms)
			{
				if(host_sec_bitz != _lbaf_tbl[ns_manage->flbas.format].lbads && _lbaf_tbl[ns_manage->flbas.format].ms == 8)
				{
						create_ns_preformat_handle = true;
				}
			}
			else
			{
				create_ns_preformat_handle = true;
			}
			gFormatFlag = true;
    		format_sec(_lbaf_tbl[ns_manage->flbas.format].lbads, ns_manage->flbas.format,true);
			gFormatFlag = false;
		}

#if defined(PI_SUPPORT)
		// edevx create protection for pi mode
		if (ns_array_menu->total_order_now > 0)
		{
			if((ns_array_menu->ns_attr[0].hw_attr.pit != (ns_manage->dps & 7)) ||
			   (ns_array_menu->ns_attr[0].hw_attr.pil != (ns_manage->dps & 8 ? 1 : 0)) ||
			   (ns_array_menu->ns_attr[0].hw_attr.ms != ns_manage->flbas.extended))
			{
				nvme_apl_trace(LOG_ERR, 0x214d, "[LJ1 ] invalid pi setting: %d.%d", ns_manage->dps, ns_manage->flbas.extended);
				nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
				goto NS_MANAGE_RLS;
			}
		}

		if ((ns_manage->dps & 7) > 3)
		{ ///< invalid pi setting
			nvme_apl_trace(LOG_ERR, 0xaac0, "[create fail] invalid pi setting pi type: %d", ns_manage->dps);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
			goto NS_MANAGE_RLS;
		}

		if ((ns_manage->dps & 7) <= 3)
		{
			if (((ns_manage->dps & 7) != 0) && ((ns_manage->flbas.format == 0) || (ns_manage->flbas.format == 2)))
			{
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
				goto NS_MANAGE_RLS;
			}
		}

		if (ns_manage->nmic)
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
			goto NS_MANAGE_RLS;
		}
#else
		if ((ns_manage->dps & 7) != 0) //Eric add 20230605
		{ ///< invalid pi setting//joe add 20200825  now just 512_0 and 4096_0
			nvme_apl_trace(LOG_ERR, 0x8973, "[create fail] No spport pi ,pi type: %d", ns_manage->dps);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
			goto NS_MANAGE_RLS;
		}
		if ((ns_manage->nmic != 0))//joe 20200105  how to set nmic?
		{ ///< invalid pi setting//joe add 20200825  now just 512_0 and 4096_0
			nvme_apl_trace(LOG_ERR, 0x44c5, "[create fail] No support pi, nmic: %d", ns_manage->nmic);
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);		// 23/8/14 Richard Lu modify for Drive Master.
			goto NS_MANAGE_RLS;
		}
		//Eric mark 20230605
		/*if ((ns_manage->flbas.extended != 0))//joe 20200105  how to set mset?
		{ ///< invalid pi setting//joe add 20200825  now just 512_0 and 4096_0
			nvme_apl_trace(LOG_ERR, 0, "[create fail] No support pi, extended: %d", ns_manage->flbas.extended);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
			goto NS_MANAGE_RLS;
		}*/
#endif
		if ((ns_manage->nsze == 0))
		{ ///< invalid pi setting//joe add 20200825  now just 512_0 and 4096_0
			nvme_apl_trace(LOG_ERR, 0x14f2, "[create fail] nsze == 0 error");
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
			goto NS_MANAGE_RLS;
		}
		// step 2. adjust nsze and start lba
		nsid_ctrl->ns->start_lba = nvmet_create_ns_start_lba(ns_id);

		//joe add NS 20200813
		//  nvme_apl_trace(LOG_INFO, 0, "drive_total_sector before:%d new_sec_num:%d ns_valid_sector:%d",drive_total_sector, new_sec_num,ns_valid_sector);//joe add log test 20200730
		if (host_sec_bitz == 9)
		{							  //joe add sec size 20200817
			if (ns_manage->nsze == 0) //joe 20200730 may can no need
			{
				ns_manage->nsze = 0;
			}
			else if ((ns_manage->nsze % NS_SIZE_GRANULARITY1) != 0)
			{ //joe add 20200721 ||(ns_manage->nsze / NS_SIZE_GRANULARITY==0)
				ns_manage->nsze = (ns_manage->nsze / NS_SIZE_GRANULARITY1 + 1) * NS_SIZE_GRANULARITY1;
			}
			//new_sec_num=ns_manage->nsze/NS_SIZE_GRANULARITY1+ns_array_menu->ns_valid_sector;//joe 20200827//ns_valid_sector;//joe add 20200730
			//nvme_apl_trace(LOG_INFO, 0, "drive_total_sector after:%d new_sec_num:%d ns_valid_sector:%d",drive_total_sector, new_sec_num,ns_array_menu->ns_valid_sector);//joe add 20200721
			valid_nsze = ns_array_menu->ns_valid_sector * NS_SIZE_GRANULARITY1 + origin_nsze; //joe add ns_array_menu 20200827
		}
		else
		{
			if (ns_manage->nsze == 0) //joe 20200730 may can no need
			{
				ns_manage->nsze = 0;
			}
			else if ((ns_manage->nsze % NS_SIZE_GRANULARITY2) != 0)
			{ //joe add 20200721 ||(ns_manage->nsze / NS_SIZE_GRANULARITY==0)
				ns_manage->nsze = (ns_manage->nsze / NS_SIZE_GRANULARITY2 + 1) * NS_SIZE_GRANULARITY2;
			}
			//new_sec_num=ns_manage->nsze/NS_SIZE_GRANULARITY2+ns_array_menu->ns_valid_sector;//joe add 20200827 ns_array_menu//ns_valid_sector;//joe add 20200730
			// nvme_apl_trace(LOG_INFO, 0, "drive_total_sector after:%d new_sec_num:%d ns_valid_sector:%d",drive_total_sector, new_sec_num,ns_array_menu->ns_valid_sector);//joe add 20200721
			valid_nsze = ns_array_menu->ns_valid_sector * NS_SIZE_GRANULARITY2 + origin_nsze; //joe add ns_array_menu 20200827
		}
		//joe check capacity 20200828
		if ((valid_nsze) > ctrlr->drive_capacity&&(origin_nsze!=0))//joe add the origin_size!=0 for edevx NRN 20210107
		{ //joe 20200805 for IOL test 9.2.3
			nvme_apl_trace(LOG_ERR, 0x82ad, "[create fail]in-sufficient capacity  valid_nsze:%x dev_cap:%x", valid_nsze, ctrlr->drive_capacity);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_INSUFFICIENT_CAPACITY);
			goto NS_MANAGE_RLS;
		}

		set_ns_slba_elba(ns_id, nsid_ctrl->ns->start_lba,
		nsid_ctrl->ns->start_lba + ns_manage->nsze - 1);
		nvmet_create_ns_section_array(ns_id - 1, ns_manage->nsze); //joe add NS 20200813

		// step 3. set namespace capability
		//joe 20200715 fix the total_nsze over the drive_capacity //joe 20200803 maybe fix the LDA overflow problem, need test
		if (ns_array_menu->ns_valid_sector == drive_total_sector)
		{ //joe add ns_array_menu 20200827
			ns_manage->nsze = origin_nsze;
			ns_manage->ncap = ns_manage->nsze;
		}
		else
		{
			ns_manage->nsze = origin_nsze; //joe test 20200805 for iol test 9.2.1&9.2.2??
			ns_manage->ncap = ns_manage->nsze;
		} //joe add NS 20200813
		// extern bool req_exec(req_t *req);

		// ns_attr->issue = req_exec;	///< ns relevant command handler
		ns_attr->hw_attr.lb_cnt = ns_manage->nsze; ///< ns size, in lba
		ns_attr->fw_attr.ncap = ns_manage->nsze;
		ns_attr->fw_attr.nsz = ns_manage->nsze;

		 ns_attr->hw_attr.nsid = ns_id;	///< ns id
		ns_attr->hw_attr.lbaf = ns_manage->flbas.format;
		ns_attr->fw_attr.type = NSID_TYPE_ALLOCATED;
#if defined(HMETA_SIZE)
		ns_attr->hw_attr.pit = (ns_manage->dps & 7);
		ns_attr->hw_attr.pil = (ns_manage->dps & 8 ? 1 : 0);
		ns_attr->hw_attr.ms = ns_manage->flbas.extended;
#endif
		//nvme_apl_trace(LOG_DEBUG, 0, "JOE ns_manage->flbas.format:%d  ns_attr->hw_attr.lbaf:%d ",ns_manage->flbas.format,ns_attr->hw_attr.lbaf);//joe add NS?20200813
		// nvme_apl_trace(LOG_DEBUG, 0, "JOE ns_manage->dps:%d  ns_attr->hw_attr.pit:%d ",ns_manage->dps,ns_attr->hw_attr.pit);
		nvmet_set_ns_attrs(ns_attr, false);

		nsid_ctrl->ns->nsfeat.all = 0;

		// step 4. set namespace meta
//#if defined(HMETA_SIZE)//joe marked 20200125  this move to nvmet_set_ns_attrs()
		//cmd_proc_hmeta_ctrl(ns_attr->hw_attr.pit,				  // pi type
		//ns_attr->hw_attr.ms,				  // meta set
		//ns_attr->hw_attr.pil,				  // pi loaction, 0 by default
		//ns_id,								  // ns id
		//_lbaf_tbl[ns_attr->hw_attr.lbaf].ms); // host meta size
//#endif														  //HMETA_SIZE

		nsid_ctrl->type = NSID_TYPE_ALLOCATED;

		// step 5. setup internal ns structure
#if 0
		ns[ns_id - 1].cap = ns_manage->ncap;
		ns[ns_id - 1].seg_off = nsid_ctrl->ns->start_lba;
		ns[ns_id - 1].seg_end = nsid_ctrl->ns->start_lba + ns_manage->ncap - 1;
#endif
		// step 6. configure aes key for nsid
#if defined(USE_CRYPTO_HW)
	#if (_TCG_)
		extern bool bKeyChanged;
		extern void TcgChangeKey(u8 idx);
		extern void tcg_init_aes_key_range(void);
		extern u8 tcg_nf_G3Wr(bool G5_only);

		TcgChangeKey(LOCKING_RANGE_CNT + ns_id);
		tcg_init_aes_key_range();
		tcg_nf_G3Wr(false);
		bKeyChanged = false;

		//LockingRangeTable_Update();
	#else
		//nvmet_set_crypto_key(ns_id, ns_id);
		crypto_change_mode_range(3, (u8)ns_id, 1, (u8)ns_id); //Andy 20200921
	#endif
#endif

		// step 7. store namespace data structure to nand
		epm_update(NAMESPACE_sign, (CPU_ID - 1)); //joe add update epm 20200901

		// step 8. setup cq status
		req->fe.nvme.cmd_spec = ns_id; // return allocated ns id

		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_SUCCESS);

		if(create_ns_preformat_handle)
		{
			evlog_printk(LOG_ERR,"[create_ns] start preformat");
			nvmet_io_fetch_ctrl(true);
			vsc_preformat(req, false);
			return;
		}

		NS_MANAGE_RLS:
		// step 9. recycle dtag
		dtag_put(DTAG_T_SRAM, dtag[0]);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);

		create_ns_preformat_handle = false;
		evt_set_imt(evt_cmd_done, (u32)req, 0);
		nvme_apl_trace(LOG_INFO, 0x0d6c, "creat ns finish");
		//flush_to_nand(EVT_NVME_NS_CREATE);
	}
}

/*!
 * @brief namespace management command
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
static __attribute__((unused)) enum cmd_rslt_t
ddr_code nvmet_ns_manage(req_t *req, struct nvme_cmd *cmd)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;
	enum nvme_ns_management_type del_ns = (cmd->cdw10 & BIT0) ? 1 : 0;
	void *mem = NULL;
	dtag_t *dtag = NULL;
	u32 xfer_size = NS_MANAGE_CTRL_SIZE; // host xfer 384 bytes ctrl data
	u32 ns_id = cmd->nsid;
	u8 sel = cmd->cdw10 & 0xF;
	u8 i = 0;

	nvme_apl_trace(LOG_INFO, 0x4d09, "NamespaceManagement: SEL |%d NSID|%x",sel, cmd->nsid);

	// PCBasher Full Random
	if (sel >= NVME_NS_MANAGEMENT_RESERVED)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		handle_result = HANDLE_RESULT_FAILURE;
		return handle_result;
	}

	switch (del_ns)
	{
	case NVME_NS_MANAGEMENT_CREATE: // create namespace

		// PCBasher full random
		//The NSID field is reserved for this operation; host software clears this field to a value of 0h.
		if (cmd->nsid != 0)
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
			handle_result = HANDLE_RESULT_FAILURE;
			break;
		}

		ns_id = nvmet_get_ns_id();	// drive allocate ns id for create operation.
		if (ns_id > NVMET_NR_NS)
		{
			nvme_apl_trace(LOG_INFO, 0x5f29, "all valid NSs are allocated");
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_ID_UNAVAILABLE);
			handle_result = HANDLE_RESULT_FAILURE;
			break;
		}
		req->op_fields.ns_ctrl.ns_id = ns_id;

		nvmet_alloc_admin_res(req, xfer_size);
		dtag = req->req_prp.mem;

		nvme_apl_trace(LOG_INFO, 0x394c, "Create NS nsid: %d dtag: 0x%x cmd-id:%x", ns_id, dtag->dtag, cmd->cid);

		handle_result = nvmet_map_admin_prp(req, cmd, xfer_size, NULL);
		if(handle_result == HANDLE_RESULT_FAILURE)
			return handle_result;
		sys_assert(handle_result == HANDLE_RESULT_DATA_XFER);

		mem = dtag2mem(*dtag);
		req->req_prp.required++;
		hal_nvmet_data_xfer(req->req_prp.prp[0].prp,
							mem,
							req->req_prp.prp[0].size,
							READ,
							(void *)req,
							nvmet_admin_ns_manage_xfer_done);
		break;

	case NVME_NS_MANAGEMENT_DELETE: // delete namespace, TODO: take care of power cycle resume op !!!
		handle_result = HANDLE_RESULT_FAILURE;

		// Drive master error, Del ns for ns ID out of range should return invalid namespace.  23/8/14 Richard
		if (ns_id != 0xFFFFFFFF && ((ns_id < 1) || (ns_id > NVMET_NR_NS)))
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			break;
		}
		if ((ns_id != 0xFFFFFFFF) && (ctrlr->nsid[ns_id - 1].type == NSID_TYPE_UNALLOCATED))// invalid ns check
		{

			nvme_apl_trace(LOG_INFO, 0x2ef3, "invlaid ns id: 0x%x type: %d", ns_id, ctrlr->nsid[ns_id - 1].type);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_ID_UNAVAILABLE);
			break;
		}

		nvme_ns_attr_t *ns_attr = get_ns_info_slot(ns_id);

		// wait ongoing commands to idle, then delete it.
		extern fast_code void btn_rcmd_rls_isr(void);
		extern bool btn_otf_cmd_idle_check(u8 nsid);
		while (btn_otf_cmd_idle_check(i) == 0)
		{
			btn_rcmd_rls_isr();
		}

		//STOP FETCH IO
		nvmet_io_fetch_ctrl(true); // disable io fetch

		nvme_apl_trace(LOG_INFO, 0xe5ad, "Delete NS nsid: %d cmd-id:%x", ns_id, cmd->cid);


		if (ns_id == 0xFFFFFFFF || ns_array_menu->total_order_now == 1)
		{
		    #if (TRIM_SUPPORT == ENABLE)
			u32 act = 0;
			nvme_apl_trace(LOG_INFO, 0xad8f, "delete all ns||only one ns\n");
			//STOP GC
			cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GC_STOP, 0, 0);
			//UPDATE L2P TABLE AND VAC
			TrimInfo.FullTrimTriggered = 1;
			FullTrimHandle((void *)INV_U32, true);

			//BACK GROUND TABLE HANDLE
			for (i = 0; i < NVMET_NR_NS; i++)
			{
				if(ctrlr->nsid[i].type==NSID_TYPE_ACTIVE||ctrlr->nsid[i].type==NSID_TYPE_ALLOCATED)
				{
					nvmet_delete_ns_section_array(i,false); //joe 20200608 test//joe add NS 20200813
				}
				ctrlr->nsid[i].type = NSID_TYPE_UNALLOCATED;
				ctrlr->nsid[i].wp_state = NS_NO_WP;
				ns_array_menu->ns_attr[i].fw_attr.type = NSID_TYPE_UNALLOCATED;

				ns_attr = get_ns_info_slot(i+1);
				ns_attr->fw_attr.type = NSID_TYPE_UNALLOCATED;//Eric 20230605 fix for not set status

				cmd_proc_ns_del(i + 1);

			}

#ifdef While_break
			u64 start = get_tsc_64();
#endif
			//CHECK THE FULL TRIM FINISH OR NOT
			while(TrimInfo.FullTrimTriggered == 1)
			{
			    if (plp_trigger){
                    handle_result = HANDLE_RESULT_FAILURE;
                    break;
                 }
				act = (vic_readl(RAW_INTERRUPT_STATUS) & (u32)1 << VID_CPU_MSG_INT);
				if(act)
				{
					cpu_msg_isr();
				}
#ifdef While_break
				if(Chk_break(start,__FUNCTION__, __LINE__))
					break;
#endif
			}
			vic_writel(act , VECTOR_ADDRESS);

			//RESTART GC
			gc_re();
			//UPDATE NS INFO TO EPM
			epm_update(NAMESPACE_sign,(CPU_ID-1));
			//nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_ID_UNAVAILABLE);
			#else
            nvme_apl_trace(LOG_INFO, 0xcc13, "No support trim ,ns delete fail\n");
			#endif
			handle_result = HANDLE_RESULT_FINISHED;
		}
		else
		{

			//for(i = ns_id - 1; i < NVMET_NR_NS; i ++) {	// delete all ns which ns id is bigger than ns_id //joe marked 20200813
			// set invalid nsid to abort new SQ commands.
			//cmd_proc_ns_del(i + 1);
			cmd_proc_ns_del(ns_id); //joe add NS 20200813

			//joe add NS 20200813
			//ctrlr->nsid[i].type = NSID_TYPE_UNALLOCATED;
			//ctrlr->nsid[i].wp_state = NS_NO_WP;
			ctrlr->nsid[ns_id - 1].type = NSID_TYPE_UNALLOCATED; //joe debug 20200731  need change i to ns_id-1
			ctrlr->nsid[ns_id - 1].wp_state = NS_NO_WP;
			//memset(&ctrlr->nsid->ns[ns_id-1], 0, sizeof(struct nvmet_namespace) - 8);//joe marked test edevx 20200811 //joe need check here 20200813
			set_ns_slba_elba(i + 1, 0, 0);
			//nvme_apl_trace(LOG_INFO, 0, "success delete nsid: %d", i);
			nvmet_delete_ns_section_array(ns_id - 1, true); //joe 20200608 test//joe add NS 20200813

			ns_array_menu->ns_attr[ns_id - 1].fw_attr.type = NSID_TYPE_UNALLOCATED; //joe add epm 20200901

			ns_attr->fw_attr.type = NSID_TYPE_UNALLOCATED;

			// store namespace data structure to nand
			epm_update(NAMESPACE_sign, (CPU_ID - 1));								//joe add update epm 20200901
			//}

			// change aes key
			//nvmet_set_crypto_key(i, i + 1);
#if defined(USE_CRYPTO_HW)
	#if (_TCG_)
			extern bool bKeyChanged;
			extern void TcgChangeKey(u8 idx);
			extern void tcg_init_aes_key_range(void);
			extern u8 tcg_nf_G3Wr(bool G5_only);

			TcgChangeKey(LOCKING_RANGE_CNT + ns_id);
			tcg_init_aes_key_range();
			tcg_nf_G3Wr(false);
			bKeyChanged = false;

			//LockingRangeTable_Update();
	#else
			//nvmet_set_crypto_key(ns_id, ns_id + 1);//joe add NS 20200813
			crypto_change_mode_range(3, (u8)ns_id, 1, (u8)ns_id); //Andy 20200921
	#endif
#endif

			handle_result = HANDLE_RESULT_FINISHED;
		}

		if(shr_format_copy_vac_flag && !plp_trigger)
		{
			//shr_format_copy_vac_flag = false;
			cpu_msg_issue(CPU_BE - 1, CPU_MSG_TRIM_CPY_VAC, 0, 0);
		}
		//RESTART FETCH IO
		nvmet_io_fetch_ctrl(false); // enable io fetch
		nvme_apl_trace(LOG_INFO, 0x6546, "delete ns finish");
		//flush_to_nand(EVT_NVME_NS_DELETE);

		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		handle_result = HANDLE_RESULT_FAILURE;
		return handle_result;
	}
	if((ns_order==1&&(drive_total_sector==ns_valid_sec)))
		full_1ns=1;
	else
		full_1ns=0;
	return handle_result;
}

/*!
 * @brief admin Command namespace attach DMA transfer Done(IN & OUT)
 *
 * @param payload	should be request pointer
 *
 * @return		not used
 */
static ddr_code void nvmet_admin_ns_attach_xfer_done(void *payload, bool error) //joe slow->ddr 20201124
{
	req_t *req = (req_t *)payload;
	u32 ns_id = req->op_fields.ns_ctrl.ns_id;
	enum nvme_ns_attach_type op_type = (enum nvme_ns_attach_type)req->op_fields.ns_ctrl.op_type;
	nvme_ns_attr_t *ns_attr = get_ns_info_slot(ns_id); //joe add for attach 20200730  //joe add NS 20200813
	struct insert_nvme_error_information_entry attach_err;

	//sys_assert(error == false);
	if (error == true) {
		nvme_xfer_err_rls_source(req);
		return;
	}

	/* less than 4K for namespace management */
	if (++req->req_prp.transferred == req->req_prp.required)
	{
		dtag_t *dtag = (dtag_t *)req->req_prp.mem;
		struct nvme_ctrlr_list *ns_ctrlr = (struct nvme_ctrlr_list *)dtag2mem(dtag[0]);
		struct nvmet_nsid *nsid_ctrl = (struct nvmet_nsid *)(&ctrlr->nsid[ns_id - 1]);

		// step 1. invalid check
		/* IOL 1.3 27 will check CNTLID list, should be start from 1, instead of 0.*/
		if ((ns_ctrlr->ctrlr_count != 1) || (ns_ctrlr->ctrlr_list[0] != (CNTID + 1)))
		{
			//nvme_apl_trace(LOG_ERR, 0, "nsid: 0x%x ctrlr cnt: %d list[0]:%d [1]:%d",
			//ns_id, ns_ctrlr->ctrlr_count, ns_ctrlr->ctrlr_list[0], ns_ctrlr->ctrlr_list[1]);

			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_CONTROLLER_LIST_INVALID);
			struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
			nvme_status->dnr = 1;
			nvme_status->m = 1;
			attach_err.status.sc = NVME_SC_CONTROLLER_LIST_INVALID;
			attach_err.status.sct = NVME_SCT_COMMAND_SPECIFIC;
			attach_err.status.rsvd2 = 0;
			attach_err.status.dnr = 1;
			attach_err.status.m = 1;
			attach_err.cid = req->nvm_cmd_id;
			attach_err.sqid = req->op_fields.admin.sq_id;
			if(ns_ctrlr->ctrlr_count != 1)
			{
				attach_err.cmd_specific = 0;
			}
			else
			{
				attach_err.cmd_specific = 2;
			}
			nvmet_err_insert_event_log(attach_err);

			goto NS_ATTACH_RLS;
		}

		// Check controller list valid first, and then check namesapce valid. Drive master error 23/8/14 Richard
		switch (op_type)
		{
			case NVME_NS_CTRLR_ATTACH: // 0, attach namespace
				if (ctrlr->nsid[ns_id - 1].type == NSID_TYPE_ACTIVE)
				{ // already attached
					nvme_apl_trace(LOG_ERR, 0xcfd5, "already attached, nsid: 0x%x", ns_id);
					nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_ALREADY_ATTACHED);
					goto NS_ATTACH_RLS;
				}

				if (ctrlr->nsid[ns_id - 1].type == NSID_TYPE_UNALLOCATED)
				{
					nvme_apl_trace(LOG_ERR, 0xe359, "please create ns first, nsid: 0x%x type: %d",
								   ns_id, ctrlr->nsid[ns_id - 1].type);
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
					goto NS_ATTACH_RLS;
				}
				break;
			case NVME_NS_CTRLR_DETACH: // 1, detach namespace
				if (ctrlr->nsid[ns_id - 1].type == NSID_TYPE_UNALLOCATED)		// For DELETE first and DETACH next case in Drive Master, return invalid field. 23/8/16 Richard
				{
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
					goto NS_ATTACH_RLS;
				}
				if (ctrlr->nsid[ns_id - 1].type != NSID_TYPE_ACTIVE)
				{ // already detached
					nvme_apl_trace(LOG_ERR, 0x0948, "not attached, nsid: 0x%x type: %d", ns_id, ctrlr->nsid[ns_id - 1].type);
					nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_NOT_ATTACHED);
					goto NS_ATTACH_RLS;
				}
				break;
			default:
				panic(0);
		}

		// step 2. attach or detach handler
		switch (op_type)
		{
		case NVME_NS_CTRLR_ATTACH:				// 0, attach namespace
			ns_attr->fw_attr.type = NSID_TYPE_ACTIVE;
			nvmet_set_ns_attrs(ns_attr, true); //joe add 20200730 //joe add NS 20200813
			nsid_ctrl->type = NSID_TYPE_ACTIVE;

			ns_array_menu->ns_attr[ns_id - 1].fw_attr.type = NSID_TYPE_ACTIVE; //joe add epm 20200901
			epm_update(NAMESPACE_sign, (CPU_ID - 1));						   //joe add update epm 20200901
			break;
		case NVME_NS_CTRLR_DETACH: // 1, detach namespace
			// set invalid nsid to abort new SQ commands.
			cmd_proc_ns_del(ns_id);

			extern fast_code void btn_rcmd_rls_isr(void);
			// wait read command idle
			extern bool btn_otf_cmd_idle_check(u8 nsid);
			while (btn_otf_cmd_idle_check(ns_id - 1) == 0)
			{
				btn_rcmd_rls_isr();
			}

			ns_attr->fw_attr.type = NSID_TYPE_ALLOCATED;
			nsid_ctrl->type = NSID_TYPE_ALLOCATED;								  //joe add 20200730 for EDEVX  inactive-->allocate //joe add NS 20200813
			ns_array_menu->ns_attr[ns_id - 1].fw_attr.type = NSID_TYPE_ALLOCATED; //joe add epm 20200901
			epm_update(NAMESPACE_sign, (CPU_ID - 1));							  //joe add update epm 20200901
			break;
		default:
			panic(0);
		}


		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_SUCCESS);

	NS_ATTACH_RLS:
		// step 4. recycle dtag
		dtag_put(DTAG_T_SRAM, dtag[0]);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);

		// step 5. setup cq status
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}

/*!
 * @brief namespace attachment command
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
ddr_code __attribute__((unused)) enum cmd_rslt_t
nvmet_ns_ctrlr_attach(req_t *req, struct nvme_cmd *cmd)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;
	enum nvme_ns_attach_type attach_ns = (cmd->cdw10 & BIT0) ? 1 : 0;
	u32 ns_id = cmd->nsid;
	void *mem = NULL;
	dtag_t *dtag = NULL;
	u32 xfer_size = sizeof(struct nvme_ctrlr_list); // host xfer 384 bytes ctrl data
	u8 sel = cmd->cdw10 & 0xF;

	nvme_apl_trace(LOG_INFO, 0x0e8c, "NamespaceAttachment: SEL |%d NSID|%x",sel, cmd->nsid);

	// PCBasher Full Random
	if (sel >= NVME_NS_CTRLR_RESERVED)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		handle_result = HANDLE_RESULT_FAILURE;
		return handle_result;
	}

	// step 1. nsid valid check, skip broadcast ns value.
	// Drive master error, 0xffffffff should return invalid field. 23/8/14 Richard
	if (ns_id == 0xFFFFFFFF)
	{
		nvme_apl_trace(LOG_ERR, 0xecaf, "invalid ns id: 0xffffffff");
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	if ((ns_id < 1) || (ns_id > NVMET_NR_NS))
	{
		nvme_apl_trace(LOG_ERR, 0x5549, "invlaid ns id: 0x%x", ns_id);
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}
	sys_assert(ns_id == ctrlr->nsid[ns_id - 1].nsid);

	// step 2. command valid check  ->  Move to After fetch controller List
	/*
	switch (attach_ns)
	{
	case NVME_NS_CTRLR_ATTACH: // 0, attach namespace
		if (ctrlr->nsid[ns_id - 1].type == NSID_TYPE_ACTIVE)
		{ // already attached
			nvme_apl_trace(LOG_ERR, 0, "already attached, nsid: 0x%x", ns_id);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_ALREADY_ATTACHED);
			return HANDLE_RESULT_FAILURE;
		}

		if (ctrlr->nsid[ns_id - 1].type == NSID_TYPE_UNALLOCATED)
		{
			nvme_apl_trace(LOG_ERR, 0, "please create ns first, nsid: 0x%x type: %d",
						   ns_id, ctrlr->nsid[ns_id - 1].type);
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}
		break;
	case NVME_NS_CTRLR_DETACH: // 1, detach namespace
		if (ctrlr->nsid[ns_id - 1].type != NSID_TYPE_ACTIVE)
		{ // already detached
			nvme_apl_trace(LOG_ERR, 0, "not attached, nsid: 0x%x type: %d", ns_id, ctrlr->nsid[ns_id - 1].type);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_NAMESPACE_NOT_ATTACHED);
			return HANDLE_RESULT_FAILURE;
		}
		break;
	default:
		panic(0);
	}
	*/

	// step 3. store valid ctrl info
	req->op_fields.ns_ctrl.ns_id = ns_id;
	req->op_fields.ns_ctrl.op_type = (bool)attach_ns;

	// step 4. fetch controller list
	nvmet_alloc_admin_res(req, xfer_size);
	dtag = req->req_prp.mem;

	nvme_apl_trace(LOG_INFO, 0xdff7, "nsid: %d dtag: 0x%x type: %d", ns_id, dtag->dtag, attach_ns);

	handle_result = nvmet_map_admin_prp(req, cmd, xfer_size, NULL);
	if(handle_result == HANDLE_RESULT_FAILURE)
		return handle_result;
	sys_assert(handle_result == HANDLE_RESULT_DATA_XFER);

	req->nvm_cmd_id=cmd->cid;
	mem = dtag2mem(*dtag);
	req->req_prp.required++;
	hal_nvmet_data_xfer(req->req_prp.prp[0].prp,
						mem,
						req->req_prp.prp[0].size,
						READ,
						(void *)req,
						nvmet_admin_ns_attach_xfer_done);

	return handle_result;
}
#endif

ddr_code void dump_idtfy_ctrlr(void *mem)
{
	struct nvme_ctrlr_data *idtfy = (struct nvme_ctrlr_data *)mem;
	//======== ns =========
	nvme_apl_trace(LOG_INFO, 0xb515, "tnvmcap: %x-%x",GET_B63_32(idtfy->tnvmcap[0]),GET_B31_00(idtfy->tnvmcap[0]));
	nvme_apl_trace(LOG_INFO, 0x065c, "unvmcap: %x-%x",GET_B63_32(idtfy->unvmcap[0]),GET_B31_00(idtfy->unvmcap[0]));
	nvme_apl_trace(LOG_INFO, 0x9d7c, "fna(format_all_ns):%x",(u8)idtfy->fna.format_all_ns);
	//=====================
}


/*!
 * @brief fill out ctrlr identification structure
 *
 * @param mem	buffer to fill identify data
 *
 * @return	not used
 */
static ddr_code void nvmet_ctrlr_idtfy(void *mem)//joe slow->ddr 20201124
{
	struct nvme_ctrlr_data *idtfy = (struct nvme_ctrlr_data *)mem;

	idtfy->vid = VID;
	idtfy->ssvid = SSVID;

	extern AGING_TEST_MAP_t *MPIN;
	void *SN_num = (void *)MPIN->drive_serial_number;
	memcpy(idtfy->sn, (s8 *)SN_num, 20);
	//for edevx set
	memset(idtfy->mn, 0x20, 40);
	memset(idtfy->fr, 0x20, 8);
	srb_t *srb = (srb_t *)SRAM_BASE;
     // memcpy(idtfy->sn, "P0123456789123456789", 20);
	/*if (srb->cap_idx == 4) //8T
	{
#if (CUST_FR == 1)
#if (CUST_CODE == 0)
		memcpy(idtfy->mn, "SSSTC PJ1-GW7680-P", 18);
#else
		memcpy(idtfy->mn, "TENCENT TXPU7T6TOP1500", 22);
#endif
#else
		memcpy(idtfy->mn, "SSSTC LJ1-2W7680", 16);
#endif
		memcpy(idtfy->subnqn, "nqn.2020-09.com.SSSTC:nvme:", 27);
		///Andy subNQN 20200929
		memcpy(&(idtfy->subnqn[27]), (s8 *)SN_num, 20);
		idtfy->rtd3e = 0xE4E1C0;  // shutdown 4T 10s=0x989680 8T 15s=0xe4e1c0  60s=0x3938700
		idtfy->rtd3r = 0x1312D00; // resume 4T 10s=0x989680 8T 20s=0x1312D00
		memcpy(idtfy->fr, FR, 7);
	}*/

	if (srb->cap_idx == 3) //4T
	{
#if (CUST_FR == 1)
#if (CUST_CODE == 0)

	#if PLP_SUPPORT == 1 //Eric
			memcpy(idtfy->mn, "SSSTC PJ1-GW3840P", 17);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW3840", 16);
	#endif

#elif(CUST_CODE == 2)

	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW3840P", 17);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW3840", 16);
	#endif
#elif(CUST_CODE == 7)
	memcpy(idtfy->mn, "FDMP81920TCS11D1", 16);
#else
	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW3840P", 17);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW3840", 16);
	#endif
#endif
#else
		memcpy(idtfy->mn, "SSSTC PJ1-GW1920P", 17);
#endif
#if (CUST_CODE == 7)
        memcpy(idtfy->subnqn, "nqn.2024-03.com.smartm:nvme:nvme-subsystem-sn-", 46);
        memcpy(&(idtfy->subnqn[46]), (s8 *)SN_num, 20);
#else
		memcpy(idtfy->subnqn, "nqn.2023-09.com.SSSTC:nvme:", 27);
		///Andy subNQN 20200929
		memcpy(&(idtfy->subnqn[27]), (s8 *)SN_num, 20);
#endif
		idtfy->rtd3e = 0x989680; // shutdown 4T 10s=0x989680 8T 15s=0xe4e1c0  60s=0x3938700
		idtfy->rtd3r = 0x989680; // resume 4T 10s=0x989680 8T 20s=0x1312D00//4T 20s
		memcpy(idtfy->fr, FR, 7);

	}
	/*else if (srb->cap_idx == 2) //2T*/
	else if (srb->cap_idx == 2) //2T
	{
#if (CUST_FR == 1)
#if (CUST_CODE == 0)

	#if PLP_SUPPORT == 1 
			memcpy(idtfy->mn, "SSSTC PJ1-GW1920P", 17);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW1920", 16);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT640S", 17);
	#endif

#elif(CUST_CODE == 2)

	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW1920P", 17);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW1920", 16);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT640S", 17);
	#endif
#elif(CUST_CODE == 7)//Smart Modular
	#if PLP_SUPPORT == 1 
			memcpy(idtfy->mn, "FDMP81920F1S11G1", 16);
	#else
			memcpy(idtfy->mn, "FDMP81920FCS11G1", 16);
	#endif
#elif (Synology_case)
			memcpy(idtfy->mn, "SNV5420-1600G", 13);

#else
	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW1920P", 17);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW1920", 16);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT640S", 17);
	#endif
#endif
#else
		memcpy(idtfy->mn, "SSSTC PJ1-GW1920P", 17);
#endif
#if (CUST_CODE == 7)
        memcpy(idtfy->subnqn, "nqn.2024-03.com.smartm:nvme:nvme-subsystem-sn-", 46);
        memcpy(&(idtfy->subnqn[46]), (s8 *)SN_num, 20);
#else
		memcpy(idtfy->subnqn, "nqn.2023-09.com.SSSTC:nvme:", 27);
		///Andy subNQN 20200929
		memcpy(&(idtfy->subnqn[27]), (s8 *)SN_num, 20);
#endif
		idtfy->rtd3e = 0x989680; // shutdown 4T 10s=0x989680 8T 15s=0xe4e1c0  60s=0x3938700
		idtfy->rtd3r = 0x989680; // resume 4T 10s=0x989680 8T 20s=0x1312D00//4T 20s
		memcpy(idtfy->fr, FR, 7);
	}
	else if (srb->cap_idx == 1) //1T
	{
#if (CUST_FR == 1)
#if (CUST_CODE == 0)

	#if PLP_SUPPORT == 1 
			memcpy(idtfy->mn, "SSSTC PJ1-GW960P", 16);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW960", 15);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT320S", 16);
	#endif

#elif(CUST_CODE == 2)

	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW960P", 16);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW960", 15);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT320S", 16);
	#endif
#elif(CUST_CODE == 7)//Smart Modular
	#if PLP_SUPPORT == 1 
				memcpy(idtfy->mn, "FDMP8960GF1S11G1", 16);
	#else
				memcpy(idtfy->mn, "FDMP8960GFCS11G1", 16);
	#endif
#elif (Synology_case)
			memcpy(idtfy->mn, "SNV5420-800G", 12);

#else
	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW960P", 16);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW960", 15);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT320S", 16);
	#endif
#endif
#else
		memcpy(idtfy->mn, "SSSTC PJ1-GW960P", 16);
#endif
#if (CUST_CODE == 7)
        memcpy(idtfy->subnqn, "nqn.2024-03.com.smartm:nvme:nvme-subsystem-sn-", 46);
        memcpy(&(idtfy->subnqn[46]), (s8 *)SN_num, 20);
#else
		memcpy(idtfy->subnqn, "nqn.2023-09.com.SSSTC:nvme:", 27);
		///Andy subNQN 20200929
		memcpy(&(idtfy->subnqn[27]), (s8 *)SN_num, 20);
#endif
		idtfy->rtd3e = 0x989680; // shutdown 4T 10s=0x989680 8T 15s=0xe4e1c0  60s=0x3938700
		idtfy->rtd3r = 0x989680; // resume 4T 10s=0x989680 8T 20s=0x1312D00//4T 20s
		memcpy(idtfy->fr, FR, 7);
	}
	else if (srb->cap_idx == 0) //512G
	{
#if (CUST_FR == 1)
#if (CUST_CODE == 0)

	#if PLP_SUPPORT == 1 
			memcpy(idtfy->mn, "SSSTC PJ1-GW480P", 16);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW480", 15);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT160S", 16);
	#endif

#elif(CUST_CODE == 2)

	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW480P", 16);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW480", 15);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT160S", 16);
	#endif
#elif(CUST_CODE == 7)//Smart Modular
	#if PLP_SUPPORT == 1 
			memcpy(idtfy->mn, "FDMP8480GF1S11G1", 16);
	#else
			memcpy(idtfy->mn, "FDMP8480GFCS11G1", 16);
	#endif
#elif (Synology_case)
			memcpy(idtfy->mn, "SNV5420-400G", 12);

#else
	#if PLP_SUPPORT == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GW480P", 16);
	#else
			memcpy(idtfy->mn, "SSSTC PJ1-GW480", 15);
	#endif
	#if PURE_SLC == 1
			memcpy(idtfy->mn, "SSSTC PJ1-GT160S", 16);
	#endif
#endif
#else
		memcpy(idtfy->mn, "SSSTC PJ1-GW480P", 16);
#endif
#if (CUST_CODE == 7)
        memcpy(idtfy->subnqn, "nqn.2024-03.com.smartm:nvme:nvme-subsystem-sn-", 46);
        memcpy(&(idtfy->subnqn[46]), (s8 *)SN_num, 20);
#else
		memcpy(idtfy->subnqn, "nqn.2023-09.com.SSSTC:nvme:", 27);
		///Andy subNQN 20200929
		memcpy(&(idtfy->subnqn[27]), (s8 *)SN_num, 20);
#endif
		idtfy->rtd3e = 0x989680; // shutdown 4T 10s=0x989680 8T 15s=0xe4e1c0  60s=0x3938700
		idtfy->rtd3r = 0x989680; // resume 4T 10s=0x989680 8T 20s=0x1312D00//4T 20s
		memcpy(idtfy->fr, FR, 7);
	}
	else
	{
		memcpy(idtfy->mn, "SSSTC LJ1-2Wxxxx", 16);
		memcpy(idtfy->fr, FR, 7);
		memcpy(idtfy->subnqn, "nqn.2023-09.com.SSSTC:nvme:", 27);
		///Andy subNQN 20200929
		memcpy(&(idtfy->subnqn[27]), (s8 *)SN_num, 20);
		idtfy->rtd3e = 0xe4e1c0; // shutdown 4T 10s=0x989680 8T 15s=0xe4e1c0  60s=0x3938700
		idtfy->rtd3r = 0x989680; // poweron 10s
	}

	/*
	 * 3 approaches to arbitration
	 * a. Round Robin
	 * b. WRR with urgent priority class
	 * c. VSC
	 */
	idtfy->rab = IDTFY_RAB; /* Arbitration Burst for a particular SQ */
	/*73:75 ieee*/
	idtfy->ieee[0] = IeeeL;
	idtfy->ieee[1] = IeeeM;
	idtfy->ieee[2] = IeeeU;
	/* 77 MDTS */
	idtfy->mdts = NVME_MAX_DATA_TRANSFER_SIZE; /* each Command Maximum Data Transfer Size is 128KB */

	/* 79:78 controller identifier */
#if (NS_MANAGE == FEATURE_SUPPORTED)
	idtfy->cntid = CNTID + 1; // IOL 1.3 27 CNTLID will check CNTLID from 1, instead of 0.
#else
	idtfy->cntid = CNTID;
#endif
	/* 83:80 VER */
	idtfy->ver.raw = 0x00;
	idtfy->ver.bits.mjr = NVME_VS_MAJOR;
	idtfy->ver.bits.mnr = NVME_VS_MINOR;
	/*84:87 88:91*/
	//idtfy->rtd3r = RTD3R;
	//idtfy->rtd3e = RTD3E;
#if (NS_MANAGE == FEATURE_SUPPORTED)
	/* 99:96 Controller Attributes */
	idtfy->ctratt.namespace_granualrity = 1;

	/* 257:256 OACS */
	idtfy->oacs.ns_manage = 1; /* Namespace Management */
#else
	idtfy->oacs.ns_manage = 0; /* No Namespace Management */
	idtfy->ctratt.namespace_granualrity = 0;
#endif
	idtfy->ctratt.host_id_exhid_supported = 1;
	idtfy->cntrltype = 1;

	idtfy->oacs.format = 1; /* No FORMAT NVM */

#if DISABLE_SECURITY
	idtfy->oacs.security = 0;
#else
	idtfy->oacs.security = 1; /* Security Send/Receive */
#endif
	idtfy->oacs.firmware = 1;

#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
	idtfy->oacs.vi_manage = 1;
#endif

#if defined(DST)
		idtfy->oacs.dv_self_test = CO_SUPPORT_DEVICE_SELF_TEST;
#endif

	/* 258: ACL 0's based and recommended to be minium 4 */
	idtfy->acl = (IDFTY_ACL - 1);
	/* 259: AERL 0's based and  minimum 4 which is 0 based */
	idtfy->aerl = IDFTY_AERL;

	/* 260 FRMW */
#if defined(PROGRAMMER)
	extern u16 fw_max_slot;
	idtfy->frmw.num_slots = fw_max_slot;
#else
	idtfy->frmw.num_slots = ctrlr->max_fw_slot;
#endif
	idtfy->frmw.slot1_ro = 0;				  /* first firmware slot is read/write */
	idtfy->frmw.activation_without_reset = 1; /* Lenovo prefers to 1 */

	/* 261 LPA */
	idtfy->lpa.ns_smart = ctrlr->ns_smart;				   /* not support the SMART / Health Information log page on a per namespace basis.*/
	idtfy->lpa.celp = 1; /* no command Effects Log page */ //vu
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	idtfy->lpa.telemetry = 1;
#endif

	/* 262 ELPE */
	idtfy->elpe = IDTFY_ELPE;

	/* 263 NPSS, power state */
	idtfy->npss = IDTFY_NPSS - 1; /* 0's based value */
	#ifdef MDOT2_SUPPORT
	idtfy->psd[0].mp = 825;
	#else
	idtfy->psd[0].mp = 2500;
	#endif
	idtfy->psd[0].mps = 0; /* scale: 0.01 -> mp*0.01 */
	idtfy->psd[0].nops = 0;
	idtfy->psd[0].enlat = 0;
	idtfy->psd[0].exlat = 0;
	idtfy->psd[0].rrt = 0;
	idtfy->psd[0].rrl = 0;
	idtfy->psd[0].rwt = 0;
	idtfy->psd[0].rwl = 0;

	idtfy->avscc.spec_format = 1;
	/* 265 APSTA, autonomous power state transition attributes*/
	idtfy->apsta.supported = 0; //vu

	/* 267:266 WCTEMP */
	idtfy->wctemp = c_deg_to_k_deg(ts_tmt.warning);

	/* 269:268 CCTEMP */
	idtfy->cctemp = c_deg_to_k_deg(ts_tmt.critical);

	/* 271:270 mtfa */
	idtfy->mtfa = NVME_MTFA_LIMIT;

#if (NS_MANAGE == FEATURE_SUPPORTED)
	/* 295:280 TNVMCAP */

	idtfy->tnvmcap[1] = 0;
	/* 311:296 UNVMCAP */
	//u8 nsid = 0;
	//u64 lba_cnt = 0;
	//while(ctrlr->nsid[nsid].type != 0) {
	//lba_cnt += ctrlr->nsid[nsid ++].ns->ncap;
	//}
	//idtfy->unvmcap[0] = (ctrlr->drive_capacity - lba_cnt) * LBA_SIZE; /* in bytes */
	//joe add NS 20200813
	if (host_sec_bitz == 9)
	{ //joe add sec size 20200817

		idtfy->tnvmcap[0] = (u64)(ctrlr->drive_capacity * LBA_SIZE1); /* in bytes */

		if (ns_array_menu->ns_valid_sector < drive_total_sector)																//joe add 20200827
			idtfy->unvmcap[0] = (u64)(ctrlr->drive_capacity * LBA_SIZE1) - (u64)(ns_array_menu->ns_valid_sector * 0x200000000); //0; /* in bytes *///joe add 20200629
		else
			idtfy->unvmcap[0] = 0;
	}
	else
	{

		idtfy->tnvmcap[0] = (u64)(ctrlr->drive_capacity * LBA_SIZE2); /* in bytes */

		if (ns_array_menu->ns_valid_sector < drive_total_sector)
			idtfy->unvmcap[0] = (u64)(ctrlr->drive_capacity * LBA_SIZE2) - (u64)(ns_array_menu->ns_valid_sector * 0x200000000); //0; /* in bytes *///joe add 20200629
		else
			idtfy->unvmcap[0] = 0;
	}
#endif

	/* 315:312 RPMB */
	idtfy->rpmbs.num_rpmb_units = 0; /* not yet */

#if defined(DST)
	idtfy->edstt = ExtenDSTTime / 60;
	idtfy->dsto.supported_type= 1;
#endif

	/* 319 FWUG */
	idtfy->fwug = FWUG; // 4KB (bytes[319])  20201126-Eddie

#if (HOST_THERMAL_MANAGE == FEATURE_SUPPORTED)
		idtfy->hctma = 1;
		idtfy->mntmt = temp_55C;
		idtfy->mxtmt = temp_84C;
#else
	/* 323:322 HCTMA */
	idtfy->hctma = 0;
	/* 325:324 MNTMT */
	idtfy->mntmt = 0; //c_deg_to_k_deg(TS_MIN_TMT);
	/* 327:326 MXTMT */
	idtfy->mxtmt = 0; //c_deg_to_k_deg(TS_MAX_TMT);
#endif

#if CO_SUPPORT_SANITIZE
	/* 331:328 SANICAP */
	idtfy->SANICAP.CES = SANICAP_CES;
	idtfy->SANICAP.BES = SANICAP_BES;
	idtfy->SANICAP.OWS = SANICAP_OWS;
	idtfy->SANICAP.NDI = SANICAP_NDI;
	idtfy->SANICAP.NODMMAS = SANICAP_NODMMAS;
#endif

	/* 512 SQES */
	idtfy->sqes.sq_min = 6;
	idtfy->sqes.sq_max = 6;

	/* 513 CQES */
	idtfy->cqes.cq_min = 4;
	idtfy->cqes.cq_max = 4;

	/* 519:516 NN */
	idtfy->nn = NVMET_NR_NS;

	/* 521:520 ONCS */
	idtfy->oncs.compare = ctrlr->attr.b.compare;
	idtfy->oncs.write_unc = ctrlr->attr.b.write_uc;
	idtfy->oncs.dsm = ctrlr->attr.b.dsm;
	idtfy->oncs.write_zeroes = ctrlr->attr.b.write_zero;
	idtfy->oncs.set_features_save = ctrlr->attr.b.set_feature_save;
	idtfy->oncs.timestamp = ctrlr->attr.b.timestamp;

	/* 524: FNA though NSID=0xFFFFFFFF regardless of those values */
	idtfy->fna.format_all_ns = 0; /* per namespace basis format */
	idtfy->fna.erase_all_ns = 0;  /* ditto. */
#if defined(USE_CRYPTO_HW)
	idtfy->fna.crypto_erase_supported = 1;
#else
	idtfy->fna.crypto_erase_supported = 0;
#endif

	/* 525: Volatile Write Cache (VWC) */
#if (PLP_SUPPORT == 0)
	idtfy->vwc.present = 1;
#else
	idtfy->vwc.present = 0;
#endif
	idtfy->vwc.flush_behavior = support_FFFFFFFF;

	/* 527:526 AWUN */	//joe add NS 20200813
	idtfy->awun = 0x04; //joe 20200731 ch 1--->0x20  (512*32=16KB)

	/* 529:528 AWUNF */
	idtfy->awupf = 0x04; //joe 20200731 change 1--->0x20  //joe wind advice to 0x4 20200916

	idtfy->nvscc = 1; /* vendor specific commands support */

	idtfy->sgls.supported = 0;
	idtfy->sgls.bit_bucket_descriptor = 0;
	idtfy->sgls.metadata_pointer = 0;
	idtfy->sgls.oversized_sgl = 0;

#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
	idtfy->lenovo_en_plp_features.plp_supported = 1;
#else
	idtfy->lenovo_en_plp_features.plp_supported = 0;
#endif

#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
	idtfy->lenovo_en_pwrdis_features.pwrdis_supported = 1;
#else
	idtfy->lenovo_en_pwrdis_features.pwrdis_supported = 0;
#endif

	dump_idtfy_ctrlr(idtfy);
}



static ddr_code void dump_idtfy_ns(void * mem)
{
	extern u8 host_sec_bitz;
	struct nvme_ns_data *ns_idtfy = (struct nvme_ns_data *)mem;
	//======== ns =========
	nvme_apl_trace(LOG_INFO, 0x9b11, "nsze: %x-%x",GET_B63_32(ns_idtfy->nsze),GET_B31_00(ns_idtfy->nsze));
	nvme_apl_trace(LOG_INFO, 0x936d, "ncap: %x-%x",GET_B63_32(ns_idtfy->ncap),GET_B31_00(ns_idtfy->ncap));
	nvme_apl_trace(LOG_INFO, 0xf23f, "nuse: %x-%x",GET_B63_32(ns_idtfy->nuse),GET_B31_00(ns_idtfy->nuse));
	nvme_apl_trace(LOG_INFO, 0xef6a, "nvmcap: %x-%x",GET_B63_32(ns_idtfy->nvmcap[0]),GET_B31_00(ns_idtfy->nvmcap[0]));
	nvme_apl_trace(LOG_INFO, 0x97b8, "sector bitz: %d",host_sec_bitz);
	nvme_apl_trace(LOG_INFO, 0x1600, "nlbaf: %x, flbas:(format)%x",ns_idtfy->nlbaf,ns_idtfy->flbas.format);
	//======== pi ==========
	#if defined(HMETA_SIZE)
	nvme_apl_trace(LOG_INFO, 0xbc2b, "lbaf: %d, pit: %d, pil: %d, meta_extended: %d",ns_idtfy->flbas.format, ns_idtfy->dps.pit, ns_idtfy->dps.md_start, ns_idtfy->flbas.extended);
	#endif
	//======================
}

/*!
 * @brief fill out namespace identify structure
 *
 * @param mem	buffer to fill identify data
 * @param ns	namespace
 *
 * @return	not used
 */
static ddr_code void nvmet_ns_idtfy(void *mem, struct nvmet_namespace *ns , u32 nsid)//joe slow->ddr 20201124
{
	struct nvme_ns_data *ns_idtfy = (struct nvme_ns_data *)mem;
	u32 i;
	extern AGING_TEST_MAP_t *MPIN;
	u8 temp_MPIN[8];

	if (nsid == NVME_GLOBAL_NS_TAG)
	{
		ns_idtfy->nuse                     = 0;
		ns_idtfy->nlbaf                    = MAX_LBAF - 1; /* #LBA Format, it's 0's based */

		/* 131:128 LBA Format 0: (M) */
		for (i = 0; i < MAX_LBAF; i++)
		{
			ns_idtfy->lbaf[i].lbads = _lbaf_tbl[i].lbads;
			ns_idtfy->lbaf[i].ms = _lbaf_tbl[i].ms;
			ns_idtfy->lbaf[i].rp = _lbaf_tbl[i].rp;
			nvme_apl_trace(LOG_DEBUG, 0x3e69, "ns_idtfy->lbaf[i].lbads:%d   ns_idtfy->lbaf[i].ms:%d  ns_idtfy->lbaf[i].rp:%d  ", ns_idtfy->lbaf[i].lbads, ns_idtfy->lbaf[i].ms, ns_idtfy->lbaf[i].rp); //joe add 20200730
		}

		if(host_sec_bitz== 9)
		{
			ns_idtfy->nawun=0x20;//for 512
			ns_idtfy->nawupf=0x20;
		}
		else
		{
			ns_idtfy->nawun=0x04;//for 4096
			ns_idtfy->nawupf=0x04;
		}
		ns_idtfy->nsfeat.atomic=1;//joe add 20201203
		ns_idtfy->dlfeat=0;//joe add for edevx 20201214//20210105 change 0, cause not support 512lba trim

		for(u8 i = 0; i < NVME_NIDT_EUI64_LEN ;i++)
		{
			temp_MPIN[i] = MPIN->EUI64[8 + i];
		}

		#if defined(HMETA_SIZE)
		ns_idtfy->mc.extended = 1; //close extened mode by Joylon 20211015//because of low performance reopen extend mode
		ns_idtfy->mc.pointer = 1; /* support separate buffer */
		#endif

		if (MAX_PI_TYPE_CNT != 0)
		{
			ns_idtfy->dpc.md_end = 1;
			ns_idtfy->dpc.md_start = 1;
			ns_idtfy->dpc.pit1 = MAX_PI_TYPE_CNT >= 1 ? 1 : 0;
			ns_idtfy->dpc.pit2 = MAX_PI_TYPE_CNT >= 2 ? 1 : 0;
			ns_idtfy->dpc.pit3 = MAX_PI_TYPE_CNT >= 3 ? 1 : 0;
		}

		#if ((RAMDISK_2NS == FEATURE_SUPPORTED) || (NS_MANAGE == FEATURE_SUPPORTED))
		temp_MPIN[4] += (u8)(1-1);
		#endif
		memcpy((&ns_idtfy->eui64),  (void *)(&temp_MPIN[0]), 8);

		#if (NS_MANAGE == FEATURE_SUPPORTED)
		ns_idtfy->nsze=ctrlr->drive_capacity;
		ns_idtfy->ncap=ctrlr->drive_capacity;
		ns_idtfy->nuse=ns_idtfy->ncap;

		for (i = 0; i < NVMET_NR_NS; i++)
		{
			if (ctrlr->nsid[i].type == NSID_TYPE_ACTIVE)
			{
				if (host_sec_bitz == 9)							//joe add sec size 20200817
					ns_idtfy->nvmcap[0] += ctrlr->nsid[i].ns->ncap * LBA_SIZE1; //joe add 20200730 for edevx  //joe add NS 20200813
				else
					ns_idtfy->nvmcap[0] += ctrlr->nsid[i].ns->ncap * LBA_SIZE2; //joe add 20200730 for edevx  //joe add NS 20200813
			}
		}
		#endif
	}
	else
	{
		/* nsze >= ncap >= nuse, no thin provisioning */
		ns_idtfy->nsze = ns->nsze;
		ns_idtfy->ncap = ns->ncap;
		ns_idtfy->nuse = ns_idtfy->ncap;
		if (host_sec_bitz == 9)							//joe add sec size 20200817
			ns_idtfy->nvmcap[0] = ns->ncap * LBA_SIZE1; //joe add 20200730 for edevx  //joe add NS 20200813
		else
			ns_idtfy->nvmcap[0] = ns->ncap * LBA_SIZE2; //joe add 20200730 for edevx  //joe add NS 20200813
		nvme_apl_trace(LOG_DEBUG, 0xd13c, "NSZE:0x%x%x", GET_B63_32(ns->nsze), GET_B31_00(ns->nsze));

		ns_idtfy->nlbaf = ns->lbaf_cnt - 1; /* #LBA Format, it's 0's based */
		//ns_idtfy->nlbaf = 0;	/* #LBA Format, it's 0's based */ //joe test 20200812
		nvme_apl_trace(LOG_DEBUG, 0x434f, "id-ns   nlbaf:%d   lbaf_cnt:%d ", ns_idtfy->nlbaf, ns->lbaf_cnt); //joe add test 20200723

		ns_idtfy->flbas.extended = ns->ms; /* metadata separate transfer */
		ns_idtfy->flbas.format = ns->lbaf; /* One Format for each NS, bind to 0 */

		/* 131:128 LBA Format 0: (M) */
		for (i = 0; i < MAX_LBAF; i++)
		{
			ns_idtfy->lbaf[i].lbads = _lbaf_tbl[i].lbads;
			ns_idtfy->lbaf[i].ms = _lbaf_tbl[i].ms;
			ns_idtfy->lbaf[i].rp = _lbaf_tbl[i].rp;
			nvme_apl_trace(LOG_DEBUG, 0xd5f2, "ns_idtfy->lbaf[i].lbads:%d   ns_idtfy->lbaf[i].ms:%d  ns_idtfy->lbaf[i].rp:%d  ", ns_idtfy->lbaf[i].lbads, ns_idtfy->lbaf[i].ms, ns_idtfy->lbaf[i].rp); //joe add 20200730
		}

	#if defined(HMETA_SIZE)
		ns_idtfy->mc.extended = 1; //close extened mode by Joylon 20211015//because of low performance reopen extend mode
		ns_idtfy->mc.pointer = 1; /* support separate buffer */
	#endif						  // HMETA_SIZE
		if (ns->npit)
		{
			ns_idtfy->dpc.md_end = 1;
	#ifndef PI_WORKAROUND
			ns_idtfy->dpc.md_end = (_lbaf_tbl[ns->lbaf].ms > 8) ? 0 : 1;
	#endif
			ns_idtfy->dpc.md_start = 1;
			ns_idtfy->dpc.pit1 = ns->npit >= 1 ? 1 : 0;
			ns_idtfy->dpc.pit2 = ns->npit >= 2 ? 1 : 0;
			ns_idtfy->dpc.pit3 = ns->npit >= 3 ? 1 : 0;
		}

		ns_idtfy->dps.pit = ns->pit;
		ns_idtfy->dps.md_start = ns->pil;

		ns_idtfy->nmic.can_share = 0;

		ns_idtfy->flbas.format = ns->lbaf;
		ns_idtfy->flbas.extended = ns->ms;


		if(host_sec_bitz== 9)
		{
			ns_idtfy->nawun=0x20;//for 512
			ns_idtfy->nawupf=0x20;
		}
		else
		{
			ns_idtfy->nawun=0x04;//for 4096
			ns_idtfy->nawupf=0x04;
		}
		ns_idtfy->nsfeat.atomic=1;//joe add 20201203
		ns_idtfy->dlfeat=0;//joe add for edevx 20201214//20210105 change 0, cause not support 512lba trim

		for(u8 i = 0; i < NVME_NIDT_EUI64_LEN ;i++)
		{
			temp_MPIN[i] = MPIN->EUI64[8 + i];
		}

		#if ((RAMDISK_2NS == FEATURE_SUPPORTED) || (NS_MANAGE == FEATURE_SUPPORTED))
		temp_MPIN[4] += (u8)(nsid-1);
		#endif

		memcpy((&ns_idtfy->eui64),  (void *)(&temp_MPIN[0]), 8);

       // ns_idtfy->eui64=0x38F60105;//joe 20201216 EUI64=0x ex-id|OUI|NNA;
        //ns_idtfy->eui64=0x5001F638;//joe edevx  check first 1/2 byte for NAAinEUI=5 and OUIinEUI=38F601 here
	#if defined(OC_SSD)
		/* open-channel support. set the first byte of vs to 1 */
		ns_idtfy->vendor_specific[0] = 1;
	#endif
		dump_idtfy_ns(ns_idtfy);
	}

}

#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
/*!
 * @brief fill out Primary Controller Capabilities identify structure
 *
 * @param mem	buffer to fill identify data
 *
 * @return	not used
 */
static void nvmet_pri_ctrlr_idtfy(void *mem)
{
	struct nvme_pri_ctrlr_cap *cap_idtfy = (struct nvme_pri_ctrlr_cap *)mem;

	cap_idtfy->cntl_id = 0;
	cap_idtfy->port_id = 0;
	/* Since VQ and VI resources are supported, looks like it
	 * should be supported for both PF0 and all VFs */
	cap_idtfy->crt = BIT1 | BIT0;

	/* Total # of VQ flexible resources for the primary and its secondary
	 * controllers */
	cap_idtfy->vqfrt = SRIOV_FLEX_Q_PER_CTRLR;
	/* Total # of VQ flexible resources assigned to the associated secondary
	 * controllers */
	cap_idtfy->vqrfa = SRIOV_FLEX_VF_Q_PER_CTRLR; //TODO: ctrlr->sr_iov->sec_vq_count;
	/* Flexible resources allocated to the primary controller */
	cap_idtfy->vqrfap = SRIOV_FLEX_PF_IO_Q;
	/* If nvme_op_mode = b'1010, a primary controller can
	 * have 1 Admin Q and maximum 8 IO Q. TODO: change value as per nvme_op_mode */
	/* Total # of VQ private resources for the primary controller */
	cap_idtfy->vqprt = SRIOV_PRIV_PF_ADM_IO_Q_TOTAL;
	/* Maximum # of VQ flexible resources that may be assigned to sec. cntrl  */
	cap_idtfy->vqfrsm = SRIOV_FLEX_VF_Q_PER_CTRLR;
	/* Preferred granularity for VQ flexible resource */
	cap_idtfy->vqgran = 1;

	/* Total # of VI flexible resources for the primary and its seconday
	 * controllers */
	cap_idtfy->vifrt = SRIOV_FLEX_Q_PER_CTRLR;
	/* Total # of VI flexible resources assigned to the associated seconday
	 * controllers */
	cap_idtfy->virfa = SRIOV_FLEX_VF_Q_PER_CTRLR; //TODO: ctrlr->sr_iov->sec_vi_count;
	/* Total number of VI flexible resources currently allocated to the
	 * primary controller. This may change after controller level reset */
	cap_idtfy->virfap = SRIOV_FLEX_PF_IO_Q;
	/* If nvme_op_mode = b'1010, a primary controller can
	 * have 1 Admin Q and maximum 8 IO Q */
	cap_idtfy->viprt = SRIOV_PRIV_PF_ADM_IO_Q_TOTAL;
	/* A secondary controller can have 1 Admin Q and remaining IO Q */
	cap_idtfy->vifrsm = SRIOV_FLEX_VF_Q_PER_CTRLR;
	cap_idtfy->vigran = 1;
}

/*!
 * @brief fill out Secondary Controller List identify structure
 *
 * @param mem	buffer to fill identify data
 *
 * @return	not used
 */
static void nvmet_sec_ctrlr_list(void *mem)
{
	struct nvme_sec_ctrlr_list *ctrlr_list = (struct nvme_sec_ctrlr_list *)mem;
	u32 i, j;

	/* Number of secondary entries in the list */
	ctrlr_list->num_id = SRIOV_VF_PER_CTRLR;

	/* Fill VF1 to VF32 entries to the list */
	for (j = 0, i = 0; i < SRIOV_VF_PER_CTRLR; i++, j++)
	{
		ctrlr_list->sc[j].scid = i + 1;
		ctrlr_list->sc[j].pcid = 0;
		ctrlr_list->sc[j].scs = ctrlr->sr_iov->ctrl_state[i + 1]; // SEC_CTRL_STATE_ONLINE
		ctrlr_list->sc[j].vfn = i + 1;
		ctrlr_list->sc[j].nvq = SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC; // ctrlr->sr_iov->sq_count[i];
		ctrlr_list->sc[j].nvi = SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC; //  ctrlr->sr_iov->vi_count[i];
	}
}

/*!
 * @brief virtualization management command handler
 *
 * @param req	request
 * @param cmd	NVM command
 *
 * @return	command status
 */
static enum cmd_rslt_t nvmet_virt_mngmt_cmd(req_t *req, struct nvme_cmd *cmd)
{
	u16 cntlid = cmd->cdw10 >> 16;
	u8 rt = (cmd->cdw10 >> 8) & 0x7;
	u8 act = cmd->cdw10 & 0xf;
	u16 nr = cmd->cdw11;
	u16 res_count;
	u8 res_idx, i;

	nvme_apl_trace(LOG_DEBUG, 0x212d, "CNTLID(%d) RT(%d) ACT(%d) NR(%d)",
				   cntlid, rt, act, nr);

	if (cntlid > SRIOV_VF_PER_CTRLR)
	{
		nvme_apl_trace(LOG_ERR, 0xe4fc, "Invalid controller id %d",
					   cntlid);
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_CONTROLLER_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

	if (rt == 0)
	{
		res_count = SRIOV_FLEX_VF_Q_PER_CTRLR -
					ctrlr->sr_iov->pri_vq_count -
					ctrlr->sr_iov->sec_vq_count;
	}
	else if (rt == 1)
	{
		res_count = SRIOV_FLEX_VF_Q_PER_CTRLR -
					ctrlr->sr_iov->pri_vi_count -
					ctrlr->sr_iov->sec_vi_count;
	}
	else
	{
		nvme_apl_trace(LOG_ERR, 0xcfad, "Invalid Resource Type (%d)", rt);
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_RESOURCE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

	nvme_apl_trace(LOG_DEBUG, 0xbb6e, "res_count(%d)", res_count);

	if (nr > res_count)
	{
		nvme_apl_trace(LOG_ERR, 0xd441, "NR(%d) > res_count(%d)", nr, res_count);
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_NUMBER_OF_CONTROLLER_RESOURCES);
		return HANDLE_RESULT_FAILURE;
	}

	switch (act)
	{
	case NVME_SR_IOV_PRI_ALLOC:
		if (cntlid > 0)
		{
			nvme_apl_trace(LOG_ERR, 0x96ef, "Invalid controller id %d", cntlid);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_CONTROLLER_IDENTIFIER);
			return HANDLE_RESULT_FAILURE;
		}
		/* Check the max primary resource available condition */
		if (nr > SRIOV_FLEX_PF_ADM_IO_Q_TOTAL)
		{
			nvme_apl_trace(LOG_ERR, 0x6c50, "Primary Resource Allocate NR(%d)", nr);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_NUMBER_OF_CONTROLLER_RESOURCES);
			return HANDLE_RESULT_FAILURE;
		}

		if (rt == 0)
		{
			/* Allocate maximum number of resources */
			ctrlr->sr_iov->pri_vq_count = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL;
			for (i = 0; i < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL; i++)
			{
				//nvmet_map_flex_sq_2_fun_sq(i, cntlid, i+8);
				//nvmet_map_flex_cq_2_fun_cq(i, cntlid, i+8);
			}
		}
		else
		{
			ctrlr->sr_iov->pri_vi_count = nr;
			for (i = 0; i < nr; i++)
			{
				//nvmet_map_flex_vi_2_fun_vi(i, cntlid, i+8);
			}
		}
		break;
	case NVME_SR_IOV_SEC_OFFLINE:
		if (cntlid == 0)
		{
			nvme_apl_trace(LOG_ERR, 0x3357, "Invalid CNTLID (%d)", cntlid);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_CONTROLLER_IDENTIFIER);
			return HANDLE_RESULT_FAILURE;
		}
		//ctrlr->sr_iov->sec_vi_count -= ctrlr->sr_iov->vi_count[cntlid];
		/* Unmap VI resouces */
		res_count = ctrlr->sr_iov->vi_count[cntlid - 1];
		res_idx = ctrlr->sr_iov->vi_flex_strt_idx[cntlid - 1];
		nvme_apl_trace(LOG_DEBUG, 0x5474, "Sec Offline VI: res_count(%d), res_idx(%d)", res_count, res_idx);
		for (i = 0; i < res_count; i++)
		{
			nvmet_unmap_flex_vi_idx(res_idx + i);
		}
		ctrlr->sr_iov->vi_count[cntlid - 1] = 0;
		//ctrlr->sr_iov->sec_vq_count -= ctrlr->sr_iov->vq_count[cntlid];
		/* Unmap VQ resouces */
		res_count = ctrlr->sr_iov->sq_count[cntlid - 1];
		res_idx = ctrlr->sr_iov->vq_flex_strt_idx[cntlid - 1];
		nvme_apl_trace(LOG_DEBUG, 0x555c, "Sec Offline VQ: res_count(%d), res_idx(%d)", res_count, res_idx);
		for (i = 0; i < res_count; i++)
		{
			nvmet_unmap_flex_sq_idx(res_idx + i);
		}
		res_count = ctrlr->sr_iov->cq_count[cntlid - 1];
		for (i = 0; i < res_count; i++)
		{
			nvmet_unmap_flex_cq_idx(res_idx + i);
		}
		ctrlr->sr_iov->sq_count[cntlid - 1] = 0;
		ctrlr->sr_iov->cq_count[cntlid - 1] = 0;
		ctrlr->sr_iov->ctrl_state[cntlid] = SEC_CTRL_STATE_OFFLINE;
		//ctrlr->sr_iov->vq_flex_strt_idx[cntlid - 1] = 0;
		//ctrlr->sr_iov->vi_flex_strt_idx[cntlid - 1] = 0;
		break;
	case NVME_SR_IOV_SEC_ASSIGN:
		if (cntlid == 0)
		{
			nvme_apl_trace(LOG_ERR, 0x18ba, "Invalid CNTLID (%d)", cntlid);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_CONTROLLER_IDENTIFIER);
			return HANDLE_RESULT_FAILURE;
		}
		if (rt == 0)
		{
			//ctrlr->sr_iov->sec_vq_count += nr;
			ctrlr->sr_iov->sq_count[cntlid - 1] = nr;
			/* Perform SQ/CQ mapping */
			//res_idx = ctrlr->sr_iov->vq_flex_cur_idx;
			res_idx = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL +
					  (cntlid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
			nvme_apl_trace(LOG_DEBUG, 0x395c, "Sec Assign VQ: res_idx(%d), nr(%d)", res_idx, nr);
			for (i = 0; i < nr; i++)
			{
				//nvmet_map_flex_sq_2_fun_sq(res_idx+i, cntlid, i);
				//nvmet_map_flex_cq_2_fun_cq(res_idx+i, cntlid, i);
			}
			nvme_apl_trace(LOG_DEBUG, 0xf368, "Map SQ & CQ done");
			//for (i = 0; i < nr; i++) {
			//nvmet_map_flex_sq_2_fun_sq(res_idx+i, cntlid, i);
			//nvmet_map_flex_cq_2_fun_cq(res_idx+i, cntlid, i);
			//}
			//nvme_apl_trace(LOG_DEBUG, 0, "Map CQ done");
			ctrlr->sr_iov->vq_flex_strt_idx[cntlid - 1] = res_idx;
			ctrlr->sr_iov->vq_flex_cur_idx += nr;
		}
		else
		{
			//ctrlr->sr_iov->sec_vi_count += nr;
			ctrlr->sr_iov->vi_count[cntlid - 1] = nr;
			/* Perform VI mapping */
			//res_idx = ctrlr->sr_iov->vi_flex_cur_idx;
			res_idx = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL +
					  (cntlid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
			nvme_apl_trace(LOG_DEBUG, 0x7113, "Sec Assign VI: res_idx(%d), nr(%d)", res_idx, nr);
			for (i = 0; i < nr; i++)
			{
				//nvmet_map_flex_vi_2_fun_vi(res_idx+i, cntlid, i);
			}
			ctrlr->sr_iov->vi_flex_strt_idx[cntlid - 1] = res_idx;
			ctrlr->sr_iov->vi_flex_cur_idx += nr;
		}
		/* If VQ and VI are assigned, change state to sec configured */
		//ctrlr->sr_iov->sq_count[cntlid - 1] = 0; sq or cq? why count
		//change to 0?
		if (ctrlr->sr_iov->sq_count[cntlid - 1] &&
			ctrlr->sr_iov->cq_count[cntlid - 1] &&
			ctrlr->sr_iov->vi_count[cntlid - 1])
		{
			ctrlr->sr_iov->ctrl_state[cntlid] = SEC_CTRL_STATE_CONFIGURED;
		}

		/* setup dw0 of completion response */
		req->fe.nvme.cmd_spec = nr;
		break;
	case NVME_SR_IOV_SEC_ONLINE:
		if (cntlid == 0)
		{
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_RESOURCE_IDENTIFIER);
			return HANDLE_RESULT_FAILURE;
		}

		ctrlr->sr_iov->ctrl_state[cntlid] = SEC_CTRL_STATE_ONLINE;
		break;
	case NVME_SR_IOV_RSVD:
	default:
		nvme_apl_trace(LOG_ERR, 0xf386, "Invalid Action (%d)", act);
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	return HANDLE_RESULT_FINISHED;
}
#endif

#if (NS_MANAGE == FEATURE_SUPPORTED)
/*!
 * @brief fill out Controller List identify structure
 *
 * @param mem	buffer to fill identify data
 *
 * @return	not used
 */
static void nvmet_idfy_ctrlr_list(void *mem, u16 cntid, u8 cns)
{
	struct nvme_ctrlr_list *ctrlr_list = (struct nvme_ctrlr_list *)mem;
	if((cns == NVME_IDENTIFY_NS_ATTACHED_CTRLR_LIST) && (cntid <= CNTID + 1))
	{
		ctrlr_list->ctrlr_count = 0;

		for(u8 i = 0; i < NVMET_NR_NS ; i++)
		{
			if(ctrlr->nsid[i].type == NSID_TYPE_ACTIVE)
			{
				ctrlr_list->ctrlr_count = 1;
				ctrlr_list->ctrlr_list[0] = CNTID + 1;
				break;
			}
		}
	}
	else if((cns == NVME_IDENTIFY_CTRLR_LIST) && (cntid <= CNTID + 1))
	{
		ctrlr_list->ctrlr_count = 1;
		ctrlr_list->ctrlr_list[0] = CNTID + 1; /* IOL --rev 1.3 --test 27*/
	}
}

/*!
 * @brief fill out namespace granularity identify structure
 *
 * @param mem	buffer to fill identify data
 *
 * @return	not used
 */
static void nvmet_idfy_ns_granularity_list(void *mem)
{
	struct nvme_ng_list *granularity_list = (struct nvme_ng_list *)mem;
	granularity_list->nsga = 0; // NG Descriptor 0 shall apply to all LBA formats
	granularity_list->nod = 0;
	if (host_sec_bitz == 9)
	{
		granularity_list->ng_des[0].nszeg = NS_SIZE_GRANULARITY1 * LBA_SIZE1; // in bytes
		granularity_list->ng_des[0].ncapg = NS_CAP_GRANULARITY1 * LBA_SIZE1;  // in bytes
	}
	else
	{
		granularity_list->ng_des[0].nszeg = NS_SIZE_GRANULARITY2 * LBA_SIZE2; // in bytes
		granularity_list->ng_des[0].ncapg = NS_CAP_GRANULARITY2 * LBA_SIZE2;  // in bytes
	}
}
#endif

/*!
 * @brief identify command handler
 *
 * @param req	request
 * @param cmd	NVM command
 *
 * @return	command status
 */
static enum cmd_rslt_t nvmet_identify(req_t *req, struct nvme_cmd *cmd)
{
	void *mem = NULL;
	dtag_t *dtag = NULL;
	u32 i = 0, ns = 0, ofst = 0;
	u32 nsid = cmd->nsid;
	u32 nsid0 = nsid - 1;
	u16 cntid = cmd->cdw10 >> 16;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;
	enum nvme_identify_cns cns = (enum nvme_identify_cns)(cmd->cdw10 & 0xFF);

	nvme_apl_trace(LOG_INFO, 0x1793, "Identify: CNS(%d) NSID(%d) CNTID(%d)", cns, nsid, cntid);

	//   CNS   NSID    CNTID
	// -----   -----  -------
	//  00h     Y         N
	//  01h     N         N
	//  02h     Y         N
	//  03h     Y         N
	//  04h     N         N
	//  10h     Y         N
	//  11h     Y         N
	//  12h     Y         Y
	//  13h     N         Y
	//  14h     N         Y
	//  15h     N         Y
	//  16h     N         N
	//  17h     N         N

	if (cntid != CNTID)
	{
		/* tnvme Group 20 with option -b */
		/* Just for IOL grp 19:0.4.0, IOL base on spec 1.2 and cannot
		 * recognize NVME_SC_INVALID_CONTROLLER_IDENTIFIER (0x1F)
		 * TODO: change back to NVME_SC_INVALID_CONTROLLER_IDENTIFIER */
		if (cns == NVME_IDENTIFY_PRIMARY_CONTROLLER_CAPABILITIES || cns == NVME_IDENTIFY_SECONDARY_CONTROLLER_LIST)
		{
			nvme_apl_trace(LOG_DEBUG, 0xc9cf, "nvmet_identify: No such controller id %x",
						   cntid);
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}
		else if(cns == NVME_IDENTIFY_NS_ATTACHED_CTRLR_LIST || cns == NVME_IDENTIFY_CTRLR_LIST)
		{

		}
		else
		{
			nvme_apl_trace(LOG_DEBUG, 0x4fff, "field not use,controller id %x ->0", cntid);
			cntid = 0;
		}
	}

	switch (cns)
	{
	case NVME_IDENTIFY_NS:
#if (NS_MANAGE == FEATURE_SUPPORTED)
		if ((nsid != NVME_GLOBAL_NS_TAG) && ((nsid == 0) || (nsid > NVMET_NR_NS)))
#else
		if ((nsid == NVME_GLOBAL_NS_TAG) || (nsid == 0) || (nsid > NVMET_NR_NS))
#endif
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			nvme_apl_trace(LOG_ERR, 0x65cb, "Identify: Invalid namespace (0x%x)", nsid);
			return HANDLE_RESULT_FAILURE;
		}

		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;

		/*
		 * if NS[NSID] is active, Identify Namespace data structure
		 * returned otherwise a zero filled data structure
		 *
		 * if oacs.ns_manage == 1, NSID = NVME_GLOBAL_NS_TAG, return
		 * common capabilities across namespaces for this controller
		 */

		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		telemetry_idtfy = mem;

		if (nsid == NVME_GLOBAL_NS_TAG)
		{
			/* here, only work for namespace management, previous macro will check it for namespace disable case. */
			/* return common capabilities */
			nvmet_ns_idtfy(mem, ctrlr->nsid[0].ns, NVME_GLOBAL_NS_TAG);

		}
		else if (ctrlr->nsid[nsid0].type == NSID_TYPE_ACTIVE)
		{
			nvmet_ns_idtfy(mem, ctrlr->nsid[nsid0].ns,nsid);
		}
		else
		{
			/* used zero filled data to identify if it's active/allocated
		 * or not, since we zero the data buffer already
		 */
			nvme_apl_trace(LOG_DEBUG, 0xac11, "NSID(%d) isn't Active",
						   nsid);
		}
		break;
	case NVME_IDENTIFY_CTRLR:
		// {
		// 	u32 prp1_lo = (u32)(cmd->dptr.prp.prp1 & 0xFFFFFFFF);
		// 	u32 prp1_hi = (u32)((cmd->dptr.prp.prp1 >> 16) >> 16);
		// 	u32 prp2_lo = (u32)(cmd->dptr.prp.prp2 & 0xFFFFFFFF);
		// 	u32 prp2_hi = (u32)((cmd->dptr.prp.prp2 >> 16) >> 16);
		//
		// 	nvme_apl_trace(LOG_ERR, 0, "prp1 0x%x%x prp2 0x%x%x\n", prp1_hi, prp1_lo, prp2_hi, prp2_lo);
		// }
		if (nsid != 0)
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			nvme_apl_trace(LOG_ERR, 0x69b0, "Identify cns:1,Invalid namespace (0x%x)", nsid);
			return HANDLE_RESULT_FAILURE;
		}
		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		telemetry_idtfy = mem;
		memset(mem, 0, DTAG_SZE);

		nvmet_ctrlr_idtfy(mem);
#if defined(SRIOV_SUPPORT)
		/* Each controller must have unique "subnqn" string. Otherwise Linux
		 * nvme driver will not work properly for VF functions. */
		if (req->fe.nvme.cntlid != 0)
		{
			struct nvme_ctrlr_data *idtfy = (struct nvme_ctrlr_data *)mem;
			u8 fun_id = req->fe.nvme.cntlid;
			memcpy(idtfy->subnqn, subnqn_vf[fun_id - 1], strlen(subnqn_vf[fun_id - 1]) >= 223 ? 223 : strlen(subnqn_vf[fun_id - 1]));
		}
#endif
		break;
	case NVME_ID_CNS_NS_DESC_LIST:
		if ((nsid == 0) || (nsid > NVMET_NR_NS))
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			nvme_apl_trace(LOG_ERR, 0x7d29, "Identify: Invalid namespace (0x%x)", nsid);
			return HANDLE_RESULT_FAILURE;
		}

		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);

		if (ctrlr->nsid[nsid0].type == NSID_TYPE_ACTIVE)
		{
			struct nvme_ns_id_desc *ns_id_desc = (struct nvme_ns_id_desc *)mem;
			extern AGING_TEST_MAP_t *MPIN;
			u8 temp_MPIN[8];
			for(u8 i = 0; i < NVME_NIDT_EUI64_LEN ;i++)
			{
				temp_MPIN[i] = MPIN->EUI64[8 + i];
			}

			ns_id_desc->nidt = NVME_NIDT_EUI64;
			ns_id_desc->nidl = NVME_NIDT_EUI64_LEN;

			#if ((RAMDISK_2NS == FEATURE_SUPPORTED) || (NS_MANAGE == FEATURE_SUPPORTED))
			temp_MPIN[4] += (u8)(nsid-1);
			#endif

			memcpy((void *)(ns_id_desc + 1),  (void *)(&temp_MPIN[0]), NVME_NIDT_EUI64_LEN);
			/*//====================================================================================================
			ns_id_desc = (void *)(ns_id_desc + 3);


			ns_id_desc->nidt = NVME_NIDT_UUID;
			ns_id_desc->nidl = NVME_NIDT_UUID_LEN;

			u32 __uuid[] = {
				0xd6868edf,
				0x1699478a,
				0x8b8e432e,
				0xcdb7fd6e};

			#if ((RAMDISK_2NS == FEATURE_SUPPORTED) || (NS_MANAGE == FEATURE_SUPPORTED))
			__uuid[0] += nsid;
			#endif
			memcpy((void *)(ns_id_desc + 1),  __uuid, NVME_NIDT_UUID_LEN);
			*///========================================================================

		}
		break;
	case NVME_IDENTIFY_ACTIVE_NS_LIST:
		if ((nsid == NVME_GLOBAL_NS_TAG) || (nsid == (NVME_GLOBAL_NS_TAG - 1)))
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			nvme_apl_trace(LOG_ERR, 0xd53b, "Identify: (NSL) Invalid namespace (%d)", nsid);
			return HANDLE_RESULT_FAILURE;
		}

		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);

		for (i = 0, ns = nsid; ns < NVMET_NR_NS; ns++)
		{
			if (ctrlr->nsid[ns].type == NSID_TYPE_ACTIVE)
			{
				((u32 *)mem)[i++] =
					ctrlr->nsid[ns].nsid;
				if (i == 1024)
					break;
			}
		}
		break;
#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
	case NVME_IDENTIFY_PRIMARY_CONTROLLER_CAPABILITIES:
		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		nvmet_pri_ctrlr_idtfy(mem);
		break;
	case NVME_IDENTIFY_SECONDARY_CONTROLLER_LIST:
		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		nvmet_sec_ctrlr_list(mem);
		break;
#endif
#if (NS_MANAGE == FEATURE_SUPPORTED)
	case NVME_IDENTIFY_CTRLR_LIST:
		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		nvmet_idfy_ctrlr_list(mem,cntid,cns);
		break;
	case NVME_IDENTIFY_NS_GRANULARITY_LIST:
		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		nvmet_idfy_ns_granularity_list(mem);
		break;
	case NVME_IDENTIFY_ALLOCATED_NS_LIST:
		if ((nsid == NVME_GLOBAL_NS_TAG) || (nsid == (NVME_GLOBAL_NS_TAG - 1)))
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			nvme_apl_trace(LOG_ERR, 0xfeb1, "Identify: CNS10 Invalid namespace (0x%x)", nsid);
			return HANDLE_RESULT_FAILURE;
		}
		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		for (i = 0, ns = nsid; ns < NVMET_NR_NS; ns++)
		{
			if (ctrlr->nsid[ns].type == NSID_TYPE_ALLOCATED || ctrlr->nsid[ns].type == NSID_TYPE_ACTIVE)
			{ //joe add 20200730 //joe add NS 20200813
				((u32 *)mem)[i++] =
					ctrlr->nsid[ns].nsid;
				nvme_apl_trace(LOG_INFO, 0x106b, "allocated NS:%d", ctrlr->nsid[ns].nsid); //joe add 20200730 //joe add NS 20200813
				if (i == 1024)
					break;
			}
		}
		break;
	case NVME_IDENTIFY_NS_ALLOCATED:
		if ((nsid == 0) || (nsid > NVMET_NR_NS))
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			nvme_apl_trace(LOG_ERR, 0x6614, "Identify: CNS11 Invalid namespace (0x%x)", nsid);
			return HANDLE_RESULT_FAILURE;
		}

		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;

		/*
		 * if NS[NSID] is active, Identify Namespace data structure
		 * returned otherwise a zero filled data structure
		 *
		 * if oacs.ns_manage == 1, NSID = NVME_GLOBAL_NS_TAG, return
		 * common capabilities across namespaces for this controller
		 */

		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		telemetry_idtfy = mem;

		if ((ctrlr->nsid[nsid0].type == NSID_TYPE_ALLOCATED)||(ctrlr->nsid[nsid0].type == NSID_TYPE_ACTIVE))//joe 20201202 add active type for iol1.4 1.1-6
		{
			nvmet_ns_idtfy(mem, ctrlr->nsid[nsid0].ns,nsid);
		}
		break;
	case NVME_IDENTIFY_NS_ATTACHED_CTRLR_LIST:
		if (nsid == NVME_GLOBAL_NS_TAG)
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			nvme_apl_trace(LOG_ERR, 0xba8a, "Identify:  CNS 0X12 Invalid ns (0x%x)", nsid);
			return HANDLE_RESULT_FAILURE;
		}else if(nsid == 0 || nsid > NVMET_NR_NS){
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			nvme_apl_trace(LOG_ERR, 0x227b, "Identify:  CNS 0X12 Invalid ns (0x%x)", nsid);
			return HANDLE_RESULT_FAILURE;
		}

		nvmet_alloc_admin_res(req, DTAG_SZE);
		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
		nvmet_idfy_ctrlr_list(mem,cntid,cns);
		break;
#endif
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	/* XXX: prp2 shall not be a pointer to a PRP list as the data buffer
	 * may not cross more than one page boundary
	 */
	handle_result = nvmet_map_admin_prp(req, cmd, IDENTIFY_XFER_SZ, nvmet_admin_public_xfer_done);
	if(handle_result == HANDLE_RESULT_FAILURE)
		return handle_result;
	sys_assert(handle_result == HANDLE_RESULT_DATA_XFER);

	nvme_apl_trace(LOG_DEBUG, 0x9c7f, "Identify: CNS(%d) DMA transfer(%d)", cns, req->req_prp.nprp);

	for (i = 0; i < req->req_prp.nprp; i++)
	{
		req->req_prp.required++;

		hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(mem, ofst),
							req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
		ofst += req->req_prp.prp[i].size;
	}

	return handle_result;
}

/*!
 * @brief set feature of queue number
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
static enum cmd_rslt_t
nvmet_feat_set_number_of_queues(req_t *req, struct nvme_cmd *cmd)
{
	u16 ncqr = cmd->cdw11 >> 16;
	u16 nsqr = cmd->cdw11;
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;
	u32 max_q_num;

	// Check DW11 firstly as Samsung 9A3, intel 5510 does.			--- Richard Lu 2024/1/11
	if (ncqr > 65534 || nsqr > 65534)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

#if 1 ///Andy test IOL 5.4 20201013
	if ((flagtestS != 0 && flagtestC != 0)|| (is_IOQ_ever_create_or_not->flag == 1))
	{
#ifndef spec
		if((cmd->nsid == 0xffffffff) && (nsqr == 0x40) && (ncqr == 0x40) && (flagtestS == 65) && (flagtestC == 65))
			goto edevx;//for edevx add by suda 21/1/22
#endif
		//Andy test IOL
		nvme_apl_trace(LOG_ERR, 0xe14d, "[Andy] SEQ error, SQ cnt: %d, CQ cnt: %d ;flag: %d\n", flagtestS, flagtestC, is_IOQ_ever_create_or_not->flag);
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_COMMAND_SEQUENCE_ERROR);
		return HANDLE_RESULT_FAILURE;
	}
	else
	{
		//Andy test IOL
		nvme_apl_trace(LOG_ERR, 0x9ddf, "[Andy] SQ flag: %d, CQ flag: %d ,IOQ flag %d\n", flagtestS, flagtestC, is_IOQ_ever_create_or_not->flag);
	}
#endif

#ifndef spec
edevx:
#endif


#if defined(SRIOV_SUPPORT)
	u8 fnid = req->fe.nvme.cntlid;
	max_q_num = (fnid == 0) ? NVMET_NR_IO_QUEUE : SRIOV_FLEX_VF_IO_Q_PER_FUNC;
#elif defined(NVME_SHASTA_MODE_ENABLE)
	max_q_num = NVMET_NR_IO_QUEUE;
#else
	max_q_num = NVMET_RESOURCES_FLEXIBLE_TOTAL - 1;
#endif

	nvme_apl_trace(LOG_DEBUG, 0x85d4, "Request ncqr/nsqr (%d/%d) and Allocate sq/cq (%d/%d)", ncqr + 1, nsqr + 1, max_q_num, max_q_num);

	*cpl_dw0 = (max_q_num - 1) << 16 | (max_q_num - 1);
	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief set feature of arbitration
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status
 */
static enum cmd_rslt_t
nvmet_feat_set_arbitration(req_t *req, struct nvme_cmd *cmd, bool save)
{
	u32 arb = cmd->cdw11 & 0xFFFFFF07;
	u8 hpw, mpw, lpw;

	nvme_apl_trace(LOG_INFO, 0x1d68, "Artbration (0x%x) -> (0x%x)", ctrlr->cur_feat.arb_feat.all, arb);

	ctrlr->cur_feat.arb_feat.all = arb;
	if (save)
		ctrlr->saved_feat.arb_feat.all = arb;

	hpw = (u8)(arb >> 24);
	mpw = (u8)(arb >> 16);
	lpw = (u8)(arb >> 8);
	hal_nvmet_set_sq_arbitration(hpw, mpw, lpw);

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief get feature of arbitration
 *
 * @param req		request
 * @param cmd		nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
static enum cmd_rslt_t nvmet_feat_get_arbitration(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.arb_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.arb_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.arb_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = (NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE);
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.arb_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief set feature of temperature threshold
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
static enum cmd_rslt_t
nvmet_feat_set_tempture_threshold(req_t *req, struct nvme_cmd *cmd, bool save)
{
	u32 temp = cmd->cdw11 & 0x3FFFFF;
	u32 tmpth;
	u32 tmpsel;
	u32 thsel;
	u32 sel;
	u32 end;
	u32 i;
	s32 cur_ts = 0;
	u32 k_deg = 0;
	///Andy test IOL 1.7 Case 1
	bool aer_report = false;

	tmpth = temp & 0xFFFF;
	temp = temp >> 16;

	tmpsel = temp & 0xF;
	temp = temp >> 4;
	thsel = temp;
	u32 minv = temp_55C;

	if (thsel >= MAX_TH || (tmpsel > SENSOR_IN_SMART && tmpsel != 0xF) || (tmpth < minv))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	if (tmpsel == 0xF)
	{
		sel = 1;
		end = sel + 1; // only one sensor now
	}
	else
	{
		sel = tmpsel;
		end = tmpsel + 1;
	}
	ctrlr->cur_feat.warn_cri_feat.tmt_warning = tmpth;
	ts_tmt.warning = tmpth - 273;
	if (save)
	{
		ctrlr->saved_feat.warn_cri_feat.tmt_warning = tmpth;

	}
	for (i = sel; i < end; i++)
	{
		nvme_apl_trace(LOG_DEBUG, 0x25b3, "Tempture Threshold %d(%d) (0x%x) -> (0x%x)",
					   i, thsel,
					   ctrlr->cur_feat.temp_feat.tmpth[i][thsel], tmpth);

		ctrlr->cur_feat.temp_feat.tmpth[i][thsel] = tmpth;
		if (save)
			ctrlr->saved_feat.temp_feat.tmpth[i][thsel] = tmpth;

		extern smart_statistics_t *smart_stat;

		if(i == 0 || i == 1)//composite temp//is same with sensor1
		{
			cur_ts = smart_stat->temperature;
			if (cur_ts > k_deg && cur_ts != 0x80+273)
			{
				k_deg = cur_ts;
			}
		}
		else
		{
			cur_ts = smart_stat->temperature_sensor[i - 1];
			if(cur_ts > k_deg && cur_ts != 0x80+273)
				k_deg = cur_ts;
		}
		//cur_ts = ts_get();
		//k_deg = c_deg_to_k_deg(cur_ts);
		///Andy test IOL 1.7 Case 1
		if (thsel == 0 && tmpth <= k_deg)
			aer_report = true;
		else if(thsel == 1 && tmpth >= k_deg)
			aer_report = true;
		//else if (tmpth > k_deg)
		//	aer_report = false;
	}
	///Andy test IOL 1.7 Case 1
	if (aer_report && (ctrlr->cur_feat.aec_feat.b.smart & 0x02))
	{
		nvmet_evt_aer_in(((NVME_EVENT_TYPE_SMART_HEALTH << 16)|SMART_STS_TEMP_THRESH),0);
	}
		//nvmet_evt_aer_in((NVME_EVENT_TYPE_SMART_HEALTH << 16) | SMART_STS_TEMP_THRESH, 0);

#if 0 ///Andy test IOL 1.7 Case 1
	if (thsel == 0)
	{
		if (tmpth < k_deg)
		{
			nvmet_evt_aer_in((NVME_EVENT_TYPE_SMART_HEALTH << 16) | SMART_STS_TEMP_THRESH, 0);
		}
	}
	else
	{
		if (tmpth > k_deg)
		{
			nvmet_evt_aer_in((NVME_EVENT_TYPE_SMART_HEALTH << 16) | SMART_STS_TEMP_THRESH, 0);
		}
	}
#endif
	return HANDLE_RESULT_FINISHED;
}
/*!
 * @brief get feature of temperature threshold
 *
 * @param req		request
 * @param cmd		nvme command
 * @param select	select settings in current/default/saved
 *
 * @return	not used
 */
ddr_code enum cmd_rslt_t nvmet_feat_get_tempture_threshold(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 dw11, return_dw11;
	dw11 = return_dw11 = cmd->cdw11 & 0x3FFFFF;
	u32 tmpsel;
	u32 thsel;
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	dw11 = dw11 >> 16;
	tmpsel = dw11 & 0xF;
	dw11 = dw11 >> 4;
	thsel = dw11 & 0x3;

/*
	if (tmpsel >= 2)
		tmpsel = 0;

	if (thsel >= 2)
		thsel = 0;
*/
	if (thsel >= MAX_TH || tmpsel > SENSOR_IN_SMART)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = (ctrlr->def_feat.temp_feat.tmpth[tmpsel][thsel] | return_dw11);
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = (ctrlr->saved_feat.temp_feat.tmpth[tmpsel][thsel] | return_dw11);
		}
		else
		{
			*cpl_dw0 = (ctrlr->def_feat.temp_feat.tmpth[tmpsel][thsel] | return_dw11);
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = (ctrlr->cur_feat.temp_feat.tmpth[tmpsel][thsel] | return_dw11);
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief set feature of write atomicity normal
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
static enum cmd_rslt_t
nvmet_feat_set_write_atomic(req_t *req, struct nvme_cmd *cmd, bool save)
{
	wan_feat_t wan_feat;

	wan_feat.all = cmd->cdw11 & 0x1;

	nvme_apl_trace(LOG_DEBUG, 0xe85d, "Write Automic Normal (0x%x) -> (0x%x)",
				   ctrlr->cur_feat.wan_feat.b.dn, wan_feat.b.dn);

	/* TODO */
	ctrlr->cur_feat.wan_feat = wan_feat;
	if (save)
		ctrlr->saved_feat.wan_feat = wan_feat;

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief get feature of write atomicity normal
 *
 * @param req		request
 * @param cmd		nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
static enum cmd_rslt_t nvmet_feat_get_write_atomic(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.wan_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.wan_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.wan_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.wan_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief get feature of asynchronous event configuration
 *
 * @param req		request
 * @param cmd		nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
ddr_code enum cmd_rslt_t nvmet_feat_get_async_event_conf(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.aec_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.aec_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.aec_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.aec_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief set feature of asynchronous event configuration
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
ddr_code enum cmd_rslt_t
nvmet_feat_set_async_event_conf(req_t *req, struct nvme_cmd *cmd, bool save)
{
	aec_feat_t aec_feat;

	aec_feat.all = cmd->cdw11 & 0x7FFF;

	nvme_apl_trace(LOG_DEBUG, 0xcb83, "Async Event Conf (0x%x) -> (0x%x)",
				   ctrlr->cur_feat.aec_feat.all, aec_feat.all);
	if(aec_feat.b.egealcn == 1)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	/* TODO */
	ctrlr->cur_feat.aec_feat = aec_feat;
	if (save)
		ctrlr->saved_feat.aec_feat = aec_feat;

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief set feature of error recovery
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
ddr_code enum cmd_rslt_t
nvmet_feat_set_error_recovery(req_t *req, struct nvme_cmd *cmd, bool save)
{
	u32 err_rec = cmd->cdw11 & 0x0001FFFF;

	nvme_apl_trace(LOG_DEBUG, 0x6202, "Err recovery (0x%x) -> (0x%x)",
				   ctrlr->cur_feat.error_feat.all, err_rec);

	/* TODO  */
	/*
		Using the Error Recovery feature (refer to section 5.21.1.5), host software may enable an error to be
		returned if a deallocated or unwritten logical block is read. If this error is supported for the namespace and
		enabled, then a Read, Verify, or Compare command that includes a deallocated or unwritten logical block
		shall fail with the Unwritten or Deallocated Logical Block status code. Note: Legacy software may not handle
		an error for this case.
	*/
	ctrlr->cur_feat.error_feat.all = err_rec;
	if (save)
		ctrlr->saved_feat.error_feat.all = err_rec;

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief get feature of error recovery
 *
 * @param req		request
 * @param cmd		nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
ddr_code enum cmd_rslt_t nvmet_feat_get_error_recovery(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.error_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.error_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.error_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE | NVME_FEAT_NS_SPECIFIC;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.error_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}

#if (POWER_MANAGE_FEATURE == FEATURE_SUPPORTED)
/*!
 * @brief Set feature of power management
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status
 */
static enum cmd_rslt_t
nvmet_feat_set_power_management(req_t *req, struct nvme_cmd *cmd, bool save)
{
	ps_feat_t ps_feat;

	ps_feat.all = cmd->cdw11 & 0xFF;

	if (ps_feat.b.ps > IDTFY_NPSS - 1)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	nvme_apl_trace(LOG_DEBUG, 0x3015, "WH (%d) Power State (%d) -> (%d)",
				   ps_feat.b.wh, ctrlr->cur_feat.ps_feat.b.ps, ps_feat.b.ps);

	/* if disk is in non-Operational Power States, an IO command is received,
	   disk need back to last Operational Power States,
	   so record the last operational power state(PS0/PS1/PS2) */
	if (ctrlr->cur_feat.ps_feat.b.ps <= 2)
		ctrlr->last_ps = ctrlr->cur_feat.ps_feat.b.ps;

#ifdef POWER_APST
	if (ctrlr->cur_feat.ps_feat.b.ps != ps_feat.b.ps && evt_change_ps != 0xFF)
		evt_set_cs(evt_change_ps, (u32)ps_feat.b.ps, 0, CS_TASK);
#endif

	ctrlr->cur_feat.ps_feat = ps_feat;
	if (save)
		ctrlr->saved_feat.ps_feat = ps_feat;

		/* because ps had been change by host ,so clear apst timer count,  */
#ifdef POWER_APST
	apst_period = 0;
#endif
	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief get feature of power management
 *
 * @param req		request
 * @param cmd		nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
static enum cmd_rslt_t nvmet_feat_get_power_management(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.ps_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.ps_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.ps_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.ps_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}
#endif

/*!
 * @brief set feature of interrupt vector configuration
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status
 */
static enum cmd_rslt_t nvmet_feat_set_interrupt_vector_conf(req_t *req, struct nvme_cmd *cmd, bool save)
{
	ivc_feat_t ivc_feat;

	ivc_feat.all = cmd->cdw11 & 0x1ffff;
#ifdef NVME_SHASTA_MODE_ENABLE
	if (ivc_feat.b.iv > NVMET_NR_INT_VECTORS ||
		hal_nvmet_check_vf_IV_Valid(ivc_feat.b.iv) == false)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	hal_nvmet_set_int_calsc(ivc_feat.b.iv, (bool)ivc_feat.b.cd);
#else
	if (ivc_feat.b.iv > (NVMET_NR_INT_VECTORS_NORMAL_MODE - 1) ||
		hal_nvmet_check_vf_IV_Valid(ivc_feat.b.iv) == false)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	hal_nvmet_set_flex_int_calsc(ivc_feat.b.iv, (bool)ivc_feat.b.cd);
#endif
	//TODO: need check the interrupt vector setting on ASIC
	ctrlr->cur_feat.ivc_feat = ivc_feat;
	if (save)
		ctrlr->saved_feat.ivc_feat = ivc_feat;

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief get feature of interrupt vector configuration
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	not used
 */
static enum cmd_rslt_t nvmet_feat_get_interrupt_vector_conf(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;
	u16 iv = cmd->cdw11 & 0xffff;

#ifdef NVME_SHASTA_MODE_ENABLE
		if (iv > NVMET_NR_INT_VECTORS)
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}
#else
		if (iv > (NVMET_NR_INT_VECTORS_NORMAL_MODE - 1))
		{
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}
#endif

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.ivc_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.ivc_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.ivc_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		// TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.ivc_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief aggregation setting with threshold and time out value
 *
 * The controller may apply this time per interrupt vector or across all
 * interrupt vectors.
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
static enum cmd_rslt_t
nvmet_feat_set_interrupt_coalescing(req_t *req, struct nvme_cmd *cmd, bool save)
{
	if (save){
		//ctrlr->saved_feat.ic_feat = ic_feat;
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_FEATURE_ID_NOT_SAVEABLE);
		return HANDLE_RESULT_FAILURE;
	}

	ic_feat_t ic_feat;

	ic_feat.all = cmd->cdw11 & 0xffff;

	hal_nvmet_set_aggregation(ic_feat.b.thr, ic_feat.b.time);
	ctrlr->cur_feat.ic_feat = ic_feat;

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief set feature of volatile write cache
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
#if (PLP_SUPPORT == 0)
static enum cmd_rslt_t nvmet_feat_set_volatile_wr_cache(req_t *req, struct nvme_cmd *cmd, bool save)
{
	vwc_feat_t vwc_feat;

	vwc_feat.all = cmd->cdw11 & 0x1;

	extern void VWC_flush_done();
	extern void ucache_flush();
	extern ftl_flush_data_t vwc_flush_ctx;
	nvme_apl_trace(LOG_INFO, 0x5db6, "Volatile Write Cache (0x%x) -> (0x%x)",
				   ctrlr->cur_feat.vwc_feat.b.wce, vwc_feat.b.wce);

	ctrlr->cur_feat.vwc_feat = vwc_feat;
	if (save)
		ctrlr->saved_feat.vwc_feat = vwc_feat;


	//when write cachee disable. All write cmd FUA
	if (vwc_feat.all) {
		esr_err_fua_flag = false;
	} else {
		esr_err_fua_flag = true;
		ftl_flush_data_t *fctx = &vwc_flush_ctx;
		if(fctx->nsid == 0){
			//sys_assert(fctx->nsid == 0); /// nsid must be zero
			btn_de_wr_disable();
			fctx->ctx.caller = req;
			fctx->nsid = 1;
			fctx->flags.all = 0;
			fctx->ctx.cmpl = VWC_flush_done;
			ucache_flush(fctx);
			return HANDLE_RESULT_RUNNING;
		}else{
			nvme_apl_trace(LOG_INFO, 0xe98e, "[VWC] VWC ongoing");
		}
	}
	return HANDLE_RESULT_FINISHED;
}
#endif

#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
static enum cmd_rslt_t nvmet_feat_set_enable_plp(req_t *req, struct nvme_cmd *cmd, bool save)
{
	en_plp_feat_t en_plp_feat;
	en_plp_feat.all = cmd->cdw11 & 0x1;


	nvme_apl_trace(LOG_ALW, 0x9179, "PLP Enable bit (0x%x) -> (0x%x)",ctrlr->cur_feat.en_plp_feat.b.opie, en_plp_feat.b.opie);

	if(en_plp_feat.b.opie){
        PLN_open_flag = true;
		nvme_apl_trace(LOG_ALW, 0xa9f0, "PLP enable ");
	}
	else{
        PLN_open_flag = false;
		nvme_apl_trace(LOG_ALW, 0xe397, "PLP disable");
	}

	ctrlr->cur_feat.en_plp_feat = en_plp_feat;
	if (save)
		ctrlr->saved_feat.en_plp_feat = en_plp_feat;

	return HANDLE_RESULT_FINISHED;
}

static enum cmd_rslt_t nvmet_feat_get_plp_status(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.en_plp_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.en_plp_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.en_plp_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.en_plp_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}
#endif

#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
static enum cmd_rslt_t nvmet_feat_set_enable_pwrdis(req_t *req, struct nvme_cmd *cmd, bool save)
{
	en_pwrdis_feat_t en_pwrdis_feat;
	en_pwrdis_feat.all = cmd->cdw11 & 0x1;

	nvme_apl_trace(LOG_ALW, 0x3687, "PWRDIS Enable bit (0x%x) -> (0x%x)",ctrlr->cur_feat.en_pwrdis_feat.b.pwrdis, en_pwrdis_feat.b.pwrdis);

	ctrlr->cur_feat.en_pwrdis_feat = en_pwrdis_feat;
    if(en_pwrdis_feat.b.pwrdis){
        PWRDIS_open_flag = true;
        nvme_apl_trace(LOG_ALW, 0x3699, "PWRDIS enable ");
    }
    else{
        PWRDIS_open_flag = false;
        nvme_apl_trace(LOG_ALW, 0xf28c, "PWRDIS disable");
    }
	if (save)
		ctrlr->saved_feat.en_pwrdis_feat = en_pwrdis_feat;

	return HANDLE_RESULT_FINISHED;
}

static enum cmd_rslt_t nvmet_feat_get_pwrdis_status(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.en_pwrdis_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.en_pwrdis_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.en_pwrdis_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.en_pwrdis_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}
#endif


static slow_code void nvmet_set_timestamp_xfer_done(void *payload, bool error) //joe fast-->slow 20200908
{
	req_t *req = (req_t *)payload;
	timestamp_feat_t *mem = NULL;
	struct nvme_cmd *cmd = req->host_cmd;
	bool sv = (bool)(cmd->cdw10 >> 31);

	//sys_assert(error == false);
	sys_assert(req->req_prp.fetch_prp_list == false);

	if (++req->req_prp.transferred == req->req_prp.required)
	{

		dtag_t *dtag = req->req_prp.mem;
		mem = (timestamp_feat_t *)dtag2mem(dtag[0]);
		if(error == false)
		{
			ctrlr->cur_feat.timestamp = (u64)mem->timestamp_l + (u64)((u64)mem->timestamp_h << 32);
			set_cur_sys_time(ctrlr->cur_feat.timestamp);
			old_time_stamp = ctrlr->cur_feat.timestamp;
			if (sv)
				ctrlr->saved_feat.timestamp = (u64)mem->timestamp_l + (u64)((u64)mem->timestamp_h << 32);

			nvme_apl_trace(LOG_INFO, 0x5b7c, "map dtag:%d,addr:0x%x,set timestamp low:%x,high:%x",
						   dtag->b.dtag, (u32)mem, mem->timestamp_l, mem->timestamp_h);
		}else{
			nvme_apl_trace(LOG_ERR, 0x864a, "DMA ERR TimeStamp Set Fail");
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_DATA_TRANSFER_ERROR);
		}
		dtag_put(DTAG_T_SRAM, dtag[0]);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}

static enum cmd_rslt_t nvme_feat_get_timestamp(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	dtag_t *dtag;
	void *mem;
	u16 i, ofst;
	u64 cur_ts = 0;
	get_ts_feat_t *ptr;
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	u32 len = sizeof(get_ts_feat_t);

	nvmet_alloc_admin_res(req, len);

	dtag = req->req_prp.mem;
	mem = dtag2mem(dtag[0]);
	memset(mem, 0, DTAG_SZE);

	ptr = (get_ts_feat_t *)mem;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
	case NVME_FEAT_SELECT_CURRENT:
		ctrlr->cur_feat.timestamp = ctrlr->cur_feat.timestamp + (get_cur_sys_time() - old_time_stamp);
		old_time_stamp = get_cur_sys_time();
		cur_ts =  ctrlr->cur_feat.timestamp;//get_cur_sys_time();
		ptr->timestamp_lo = (u32)cur_ts;
		ptr->timestamp_hi = (u16)(cur_ts >> 32);
		ptr->sync = 0;	 //continous
		ptr->ts_org = 1; //from last set features command calc
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			ptr->timestamp_lo = (u32)ctrlr->saved_feat.timestamp;
			ptr->timestamp_hi = (u16)(ctrlr->saved_feat.timestamp >> 32);
			ptr->sync = 0;	 //continous
			ptr->ts_org = 1; //from last set features command calc
		}
		else
		{
			cur_ts = ctrlr->cur_feat.timestamp;//get_cur_sys_time();
			ptr->timestamp_lo = (u32)cur_ts;
			ptr->timestamp_hi = (u16)(cur_ts >> 32);
			ptr->sync = 0;	 //continous
			ptr->ts_org = 1; //from last set features command calc
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}

	enum cmd_rslt_t handle_result = nvmet_map_admin_prp(req, cmd, len, nvmet_admin_public_xfer_done);
	if(handle_result == HANDLE_RESULT_FAILURE)
		return handle_result;
	sys_assert(handle_result == HANDLE_RESULT_DATA_XFER);
	ofst = 0;
	for (i = 0; i < req->req_prp.nprp; i++)
	{
		req->req_prp.required++;

		hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(mem, ofst),
							req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
		ofst += req->req_prp.prp[i].size;
	}
	return handle_result;
}

static enum cmd_rslt_t nvme_feat_set_timestamp(req_t *req, struct nvme_cmd *cmd, bool save)
{
	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;
	dtag_t *dtag;
	void *mem = NULL;
	u32 ofst = 0;

	nvme_apl_trace(LOG_INFO, 0x88e1, "Set Timestamp, save %d", save);

	nvmet_alloc_admin_res(req, sizeof(timestamp_feat_t));

	dtag = req->req_prp.mem;

	handle_result = nvmet_map_admin_prp(req, cmd, sizeof(timestamp_feat_t), NULL);
	if(handle_result == HANDLE_RESULT_FAILURE)
		return handle_result;
	sys_assert(handle_result == HANDLE_RESULT_DATA_XFER);

	mem = dtag2mem(*dtag);

	nvme_apl_trace(LOG_DEBUG, 0xbda5, "dtag %d,addr 0x%x", dtag->b.dtag, (u32)mem);

	for (i = 0; i < req->req_prp.nprp; i++)
	{
		req->req_prp.required++;

		hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(mem, ofst),
							req->req_prp.prp[i].size, READ, (void *)req, nvmet_set_timestamp_xfer_done);
		ofst += req->req_prp.prp[i].size;
	}

	return handle_result;
}

#ifdef NS_MANAGE
/*!
 * @brief set feature of namepsace write protect
 *
 * @param req	req
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status
 */
ddr_code enum cmd_rslt_t nvmet_feat_set_ns_wp(req_t *req, struct nvme_cmd *cmd, bool save)
{
	enum cmd_rslt_t ret = HANDLE_RESULT_FINISHED;
	u32 nsid = cmd->nsid;
	u8 i = 0;
	u8 j = 0;
	bool wp_state = ((cmd->cdw11 & 0x3) != 0) ? true : false;

	if (nsid == 0xFFFFFFFF)
	{
		for (i = 0; i < NVMET_NR_NS; i++)
		{
			if (ctrlr->nsid[i].type != NSID_TYPE_ACTIVE)
			{
				continue;
			}
			j++;
			cmd_proc_ns_set_wp(i + 1, wp_state);
			ctrlr->nsid[i].wp_state = (enum nsid_wp_state)(cmd->cdw11 & 0x3);
		}
		if (j == 0)
		{
			nvme_apl_trace(LOG_ERR, 0xfdfb, "no active ns");
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}
	}
	else
	{
		if ((nsid < 1 || nsid > NVMET_NR_NS) ||
			ctrlr->nsid[nsid - 1].type != NSID_TYPE_ACTIVE)
		{
			nvme_apl_trace(LOG_ERR, 0x33d1, "invlaid ns id: %d", nsid);
			nvmet_set_status(&req->fe,
							 NVME_SCT_GENERIC,
							 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}

		cmd_proc_ns_set_wp(nsid, wp_state);
		ctrlr->nsid[nsid - 1].wp_state = (enum nsid_wp_state)(cmd->cdw11 & 0x3);
	}

	return ret;
}

/*!
 * @brief get feature of namespace write protect state
 *
 * @param req	request
 * @param cmd	nvme command
 * @param select	select settings in current/default/saved
 *
  * @return	command status
 */
ddr_code enum cmd_rslt_t nvmet_feat_get_ns_wp(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	enum cmd_rslt_t ret = HANDLE_RESULT_FINISHED;
	u32 nsid = cmd->nsid;
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	if (nsid < 1 || nsid > NVMET_NR_NS || nsid == 0xFFFFFFFF)
	{
		nvme_apl_trace(LOG_INFO, 0xb6f2, "invlaid ns id: %d", nsid);
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}

	if (ctrlr->nsid[nsid - 1].type != NSID_TYPE_ACTIVE)
	{
		nvme_apl_trace(LOG_INFO, 0xf814, "invlaid ns id: %d type: %d",
					   nsid, ctrlr->nsid[nsid - 1].type);
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
	case NVME_FEAT_SELECT_SAVED:
	case NVME_FEAT_SELECT_SUPPORTED:
	case NVME_FEAT_SELECT_CURRENT:
	default:
		*cpl_dw0 = ctrlr->nsid[nsid - 1].wp_state;
		break;
	}

	return ret;
}
#endif

#ifdef Dynamic_OP_En
//#include "epm.h"
extern epm_info_t *shr_epm_info;
extern u8 DYOP_FRB_Erase_flag;
extern u32 DYOP_LBA_L;
extern u32 DYOP_LBA_H;
ddr_code void Dynamic_OP_main(u32 value1, u32 value2, req_t *req)
{
	/*u64 OP_cap = 0;

	OP_cap = (u64)value1 + (((u64)(value2))<<32) ;

	if(host_sec_bitz == 12)
	{
		OP_cap = (u64)value1 + (((u64)(value2))<<32) ;
		//nvme_apl_trace(LOG_ALW, 0, "ocan777 OP_cap 0x%x%x \n",(u32)(OP_cap>>32), OP_cap);
		OP_cap = OP_cap * 8;
		//nvme_apl_trace(LOG_ALW, 0, "ocan888 OP_cap 0x%x%x \n",(u32)(OP_cap>>32), OP_cap);
		DYOP_LBA_L = OP_cap;
		DYOP_LBA_H = (u32)(OP_cap>>32);
	}
	else if(host_sec_bitz == 9)
	{
		DYOP_LBA_L = value1;
		DYOP_LBA_H = value2;
	}
	else
	{
		DYOP_LBA_L= value1;
		DYOP_LBA_H= value2;
		nvme_apl_trace(LOG_ERR, 0, "ocan8 Attention Attention no this case \n");
	}*/

	DYOP_LBA_L= value1;
	DYOP_LBA_H= value2;

	nvme_apl_trace(LOG_ERR, 0x1f88, "Ocan9 OP_main LBAL 0x%x, LBAH 0x%x, hostsecbitz %d \n",DYOP_LBA_L, DYOP_LBA_H, host_sec_bitz);

	//epm_update(FTL_sign, (CPU_ID - 1));
	DYOP_FRB_Erase_flag = 1; //20210511 now only use to NS setting //for  vsc_preformat() to do FRB_erase
	vsc_preformat(req,true);
}

static enum cmd_rslt_t nvmet_feat_set_Dynamic_OP(req_t *req, struct nvme_cmd *cmd)
{
	u32 OpValue1 = cmd->cdw11;
	u32 OpValue2 = cmd->cdw12;
	u64 LBA = 0;

	srb_t *srb = (srb_t *)SRAM_BASE;
	LBA = (u64)OpValue1 + (((u64)(OpValue2))<<32) ;

	nvme_apl_trace(LOG_ERR, 0x6ea6, "Ocan10  host_sec_bitz %u, LBA 0x%x%x, srb->cap_idx %u _max_capacity %u", host_sec_bitz, (u32)(LBA>>32), LBA, srb->cap_idx, _max_capacity);

    if((LBA > (u64)((u64)_max_capacity*8)) || (LBA < (u64)((u64)DYOP_MINLBA*8)))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}


	nvmet_io_fetch_ctrl(true);  //stopIO
	//STOP GC
	format_gc_suspend_done = 0;
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GC_STOP, 0, 0);
		while(format_gc_suspend_done == 0){
			;
		}
		Dynamic_OP_main(OpValue1, OpValue2, req);

	return HANDLE_RESULT_PENDING_BE; //HANDLE_RESULT_FINISHED;
}
#endif
/*!
 * @brief get feature of volatile write cache
 *
 * @param req	request
 * @param cmd	nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
#if (PLP_SUPPORT == 0)
static enum cmd_rslt_t nvmet_feat_get_volatile_wr_cache(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.vwc_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.vwc_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.vwc_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		// TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.vwc_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}
#endif
/*!
 * @brief get feature of interrupt coalescing
 *
 * @param req	request
 * @param cmd	nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
static enum cmd_rslt_t nvmet_feat_get_interrupt_coalescing(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	*cpl_dw0 = hal_nvmet_get_aggregation();
	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.ic_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		*cpl_dw0 = ctrlr->def_feat.ic_feat.all;
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = hal_nvmet_get_aggregation();
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		//*cpl_dw0 = hal_nvmet_get_aggregation();
		break;
	}
	return HANDLE_RESULT_FINISHED;
}

#if (AUTO_POWER_STATE_FEATURE == FEATURE_SUPPORTED)
/*!
 * @brief set feature of Autonomous Power State Transition
 *
 * @param req	request
 * @param cmd	nvme command
 * @param select	select settings in current/default/saved
 *
 * @return	command status
 */
static enum cmd_rslt_t nvmet_feat_set_apst(req_t *req, struct nvme_cmd *cmd, bool save)
{
	apst_t apst;
	apst.b.apst_en = cmd->cdw11 & BIT0;
	void *mem;
	dtag_t *dtag;

	nvme_apl_trace(LOG_INFO, 0xac47, "apst set to %d, save %d", apst.b.apst_en, save);

	/* if set also change the Autonomous Power State Transition data structure,
		so need transfer the data first */
	if (cmd->dptr.prp.prp1)
	{
		u32 len = MAX_NPSS_NUM * sizeof(apst_data_entry_t);
		nvmet_alloc_admin_res(req, len);

		dtag = req->req_prp.mem;
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);

		req->req_prp.transferred = 0;
		sys_assert(req->req_prp.mem_sz == 1);
		req->req_prp.required = 1;
		hal_nvmet_data_xfer(cmd->dptr.prp.prp1, mem, len, READ, (void *)req, nvmet_admin_set_features_apst_xfer_done);
		return HANDLE_RESULT_DATA_XFER;
	}
	else
	{
		ctrlr->cur_feat.apst_feat.apst.b.apst_en = apst.b.apst_en;
		if (save)
			ctrlr->saved_feat.apst_feat.apst.b.apst_en = apst.b.apst_en;
		return HANDLE_RESULT_FINISHED;
	}
}

/*!
 * @brief set feature of Autonomous Power State Transition
 *
 * @param req	request
 * @param cmd	nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		None
 */
static void nvmet_feat_get_apst(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;
	dtag_t *dtag;
	void *mem;
	u16 i, ofst;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.apst_feat.apst.all;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.apst_feat.apst.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		*cpl_dw0 = ctrlr->def_feat.apst_feat.apst.all;
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	}

	u32 len = MAX_NPSS_NUM * sizeof(apst_data_entry_t);
	nvmet_alloc_admin_res(req, len);

	dtag = req->req_prp.mem;
	mem = dtag2mem(dtag[0]);
	memset(mem, 0, DTAG_SZE);

	memcpy(mem, ctrlr->def_feat.apst_feat.apst_de,
		   IDTFY_NPSS * sizeof(apst_data_entry_t));

	enum cmd_rslt_t handle_result = nvmet_map_admin_prp(req, cmd, len, nvmet_admin_public_xfer_done);
	if(handle_result == HANDLE_RESULT_FAILURE)
		return handle_result;
	sys_assert(handle_result == HANDLE_RESULT_DATA_XFER);
	ofst = 0;
	for (i = 0; i < req->req_prp.nprp; i++)
	{
		req->req_prp.required++;

		hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(mem, ofst),
							req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
		ofst += req->req_prp.prp[i].size;
	}
}
#endif

/*!
 * @brief Set feature of Host Controlled Thermal Management
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
ddr_code enum cmd_rslt_t
nvmet_feat_set_host_controlled_thermal_mgr(req_t *req, struct nvme_cmd *cmd, bool save)//joe slow->ddr 20201124
{
	u32 tmt2 = cmd->cdw11 & 0xFFFF;
	u32 tmt1 = (cmd->cdw11 & 0xFFFF0000) >> 16;
	u32 minv = temp_55C;
	u32 maxv = temp_84C;

	if (tmt1 != 0 && (tmt1 < minv || tmt1 > maxv))
		goto fail;

	if (tmt2 != 0 && (tmt2 < minv || tmt2 > maxv))
		goto fail;

	if (tmt2 != 0 && tmt1 != 0 && tmt1 >= tmt2)
	{
	fail:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	tmt1 = tmt1 != 0 ? k_deg_to_c_deg(tmt1) : ~0;
	tmt2 = tmt2 != 0 ? k_deg_to_c_deg(tmt2) : ~0;

	set_hctm_flag |= save;

	ts_tmt_setup(tmt1, tmt2);
	ctrlr->cur_feat.hctm_feat.all = cmd->cdw11;

	if (save)
		ctrlr->saved_feat.hctm_feat.all = cmd->cdw11;

	return HANDLE_RESULT_FINISHED;
}
#if CO_SUPPORT_SANITIZE
/*!
 * @brief set feature of sanitize configuration
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 * @return	command status, always HANDLE_RESULT_FINISHED
 */
ddr_code enum cmd_rslt_t
nvmet_feat_set_sanitize_conf(req_t *req, struct nvme_cmd *cmd, bool save)
{
	sanitize_feat_t sanitize_feat;

	sanitize_feat.b.NODRM = cmd->cdw11 & 0x1;

	nvme_apl_trace(LOG_DEBUG, 0x26e7, "Sanitize Conf NODRM: (0x%x) -> (0x%x)",
				   ctrlr->cur_feat.sanitize_feat.b.NODRM, sanitize_feat.b.NODRM);

	/* TODO */
	ctrlr->cur_feat.sanitize_feat.all = sanitize_feat.all;
	if (save)
		ctrlr->saved_feat.sanitize_feat.all = sanitize_feat.all;

	return HANDLE_RESULT_FINISHED;
}
#endif

/*!
 * @brief get feature of Host Controlled Thermal Management
 *
 * @param req		request
 * @param cmd		nvme command
 * @param select	select settings in current/default/saved
 *
 * @return		not used
 */
ddr_code enum cmd_rslt_t
nvmet_feat_get_host_controlled_thermal_mgr(req_t *req, struct nvme_cmd *cmd, u8 select)//joe slow->ddr 20201124
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_SUPPORTED:
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.hctm_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.hctm_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.hctm_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.hctm_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
		return HANDLE_RESULT_FINISHED;
}

#if CO_SUPPORT_SANITIZE
ddr_code enum cmd_rslt_t nvmet_feat_get_sanitize_conf(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

	switch (select)
	{
	case NVME_FEAT_SELECT_DEFAULT:
		*cpl_dw0 = ctrlr->def_feat.sanitize_feat.all;
		break;
	case NVME_FEAT_SELECT_SAVED:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->saved_feat.sanitize_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.sanitize_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		//TODO
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->cur_feat.sanitize_feat.all;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
		break;
	}
	return HANDLE_RESULT_FINISHED;
}
#endif

static inline bool check_namespaces(req_t *req, struct nvme_cmd *cmd)
{
	enum nvme_feat fid = (enum nvme_feat)(cmd->cdw10 & 0xff);
	sys_assert((cmd->opc == NVME_OPC_SET_FEATURES) || (cmd->opc == NVME_OPC_GET_FEATURES));
	bool accept_broadcast = (cmd->opc == NVME_OPC_SET_FEATURES) ? true : false;

	switch (fid)
	{
	case NVME_FEAT_ARBITRATION:
	case NVME_FEAT_POWER_MANAGEMENT:
	case NVME_FEAT_TEMPERATURE_THRESHOLD:
	//case NVME_FEAT_ERROR_RECOVERY:
#if (PLP_SUPPORT == 0)
	case NVME_FEAT_VOLATILE_WRITE_CACHE:
#endif
	case NVME_FEAT_NUMBER_OF_QUEUES:
	case NVME_FEAT_INTERRUPT_COALESCING:
	case NVME_FEAT_INTERRUPT_VECTOR_CONFIGURATION:
	case NVME_FEAT_WRITE_ATOMICITY:
	case NVME_FEAT_ASYNC_EVENT_CONFIGURATION:
	//case NVME_FEAT_AUTONOMOUS_POWER_STATE_TRANSITION:
	case NVME_FEAT_TIMESTAMP:
	case NVME_FEAT_HOST_THERMAL_MANAGEMENT:
	case NVME_FEAT_SANITIZE_CONFIG:

		if ((accept_broadcast == true) && (cmd->nsid != 0) && (cmd->nsid != NVME_GLOBAL_NS_TAG))
			return false;

		if (cmd->nsid > NVMET_NR_NS && cmd->nsid != NVME_GLOBAL_NS_TAG)
			return false;

		return true;
#if !defined(PROGRAMMER)
    case NVME_LOG_SAVELOG:
        return true;
#endif//young add for save log 20210714
	default:
		return true;
	}
}

/*!
 * @brief set features support
 *
 * @param req	request
 * @param cmd	nvme command
 * @param save	save settings to NAND or not
 *
 *  //       Feature Name               FeatureID   Spt   Savable  NS Spcf  Chgable
 *	//  ----------------------          ---------  -----  -------  -------  -------
 *	//  Arbitration                         01h     YES     YES      NO       YES
 *	//  Power Management                    02h     YES     YES      NO       YES
 *	//  LBA Range Type                      03h     NO      N/A      YES      N/A
 *	//  Temperature Threshold               04h     YES     YES      NO       YES
 *	//  Error Recovery                      05h     YES     YES      YES      YES
 *	//  Volatile Write Cache                06h     YES     YES      NO       YES
 *	//  Number of Queues                    07h     YES     YES      NO       YES
 *	//  Interrupt Coalescing                08h     YES     YES      NO       YES
 *	//  Interrupt Vector Configuration      09h     YES     YES      NO       YES
 *	//  Write Atomicity                     0Ah     YES     YES      NO       YES
 *	//  Async Event Config                  0Bh     YES     YES      NO       YES
 *	//  Auto Pw State Trans                 0Ch     NO      N/A      N/A      N/A
 *	//  Host Memory Buffer                  0Dh     YES     NO       NO       YES
 *	//  Timestamp                           0Eh     NO      N/A      N/A      N/A
 *	//  Host Controlled Thermal Management  10h     NO      N/A      N/A      N/A
 *	//  NOPS Config                         11h     NO      N/A      N/A      N/A
 *
 * @return		Feature depends
 */
static slow_code_ex enum cmd_rslt_t nvmet_set_feature(req_t *req, struct nvme_cmd *cmd)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;
	bool sv = (bool)(cmd->cdw10 >> 31);
	enum nvme_feat fid = (enum nvme_feat)(cmd->cdw10 & 0xff);
	extern __attribute__((weak)) void nvme_info_save(req_t *);

	nvme_apl_trace(LOG_INFO, 0x9d85, "SetFeatures: FID(0x%x) SV(%d) DW11(0x%x) NSID(%d)", fid, sv, (u32)(cmd->cdw11), cmd->nsid);

	if (check_namespaces(req, cmd) == false)
	{
		nvme_apl_trace(LOG_ERR, 0x517a, "error namespace id %d fid %d", cmd->nsid, fid);
		if((cmd->nsid >= 1) && (cmd->nsid <= NVMET_NR_NS))
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_FEATURE_NOT_NAMESPACE_SPECIFIC);
		else
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);

		return HANDLE_RESULT_FAILURE;
	}

#if 0
	u32 nsid = cmd->nsid;
	if (nsid == 0)
	{
		// Nothing to do now
		if(fid == NVME_FEAT_ERROR_RECOVERY)
		{
			nvmet_set_status(&req->fe,NVME_SCT_GENERIC,NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}
	}
	else if (nsid == NVME_GLOBAL_NS_TAG)
	{ // FFFFFFFFh
		switch (fid)
		{
		//feature are namespace specific

		/* A Set Features command with the Feature Identifier set to 03h and the NSID field set to FFFFFFFFh shall *\
		\* be aborted with a status of Invalid Field in Command.												   */
		case NVME_FEAT_LBA_RANGE_TYPE:

			/* A Set Features command that uses a namespace ID of FFFFFFFFh modifies the reservation notification mask  *\
		\* of all namespaces that are attached to the controller and that support reservations.						*/
			//case NVME_FEAT_HOST_RESERVE_MASK:

			/* A Set Features command that uses the namespace ID FFFFFFFFh modifies the PTPL state       *\
		\* associated with all namespaces that are attached to the controller and that support PTPL  */
			//case NVME_FEAT_HOST_RESERVE_PERSIST:

			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;

		//feature are controller specific
		default:
			break;
		}
	}
	else if((nsid > NVMET_NR_NS) && (fid != NVME_FEAT_ERROR_RECOVERY))
	{ // >1, <FFFFFFFFh
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);

		// for DM Protocol test suite 2.0,
		// Expected: StatusType = 0h; StatusCode = 0Bh -- Generic Command Status  : Invalid Namespace or Format
		//nvmet_set_status(&req->fe,
		//		 NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}
	else if(fid != NVME_FEAT_ERROR_RECOVERY)
	{ // nsid = 1
		// The Set Features command shall fail with a status code of Feature Not Namespace Specific
		switch (fid)
		{
		case NVME_FEAT_ARBITRATION:
		case NVME_FEAT_POWER_MANAGEMENT:
		case NVME_FEAT_TEMPERATURE_THRESHOLD:
		//case NVME_FEAT_ERROR_RECOVERY:
		//case NVME_FEAT_VOLATILE_WRITE_CACHE:
		case NVME_FEAT_NUMBER_OF_QUEUES:
		case NVME_FEAT_INTERRUPT_COALESCING:
		case NVME_FEAT_INTERRUPT_VECTOR_CONFIGURATION:
		case NVME_FEAT_WRITE_ATOMICITY:
		case NVME_FEAT_ASYNC_EVENT_CONFIGURATION:
			//case NVME_FEAT_AUTONOMOUS_POWER_STATE_TRANSITION:

			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_FEATURE_NOT_NAMESPACE_SPECIFIC);
			break;
		default:
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		}
		return HANDLE_RESULT_FAILURE;
	}
#endif
	u32 nsid = cmd->nsid;

	if(fid == NVME_FEAT_ERROR_RECOVERY)
	{
		if((nsid != NVME_GLOBAL_NS_TAG) && (nsid == 0 || nsid >NVMET_NR_NS))
		{
			nvmet_set_status(&req->fe,NVME_SCT_GENERIC,NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
			return HANDLE_RESULT_FAILURE;
		}else if(nsid != NVME_GLOBAL_NS_TAG && ctrlr->nsid[nsid - 1].type != NSID_TYPE_ACTIVE){
			nvmet_set_status(&req->fe,NVME_SCT_GENERIC,NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}
	}

	nvme_apl_trace(LOG_DEBUG, 0x1f66, "fid=%x, sv=%d, NSID=%x", fid, sv, cmd->nsid);
	switch (fid)
	{
	case NVME_FEAT_ARBITRATION:
		handle_result = nvmet_feat_set_arbitration(req, cmd, sv); //01h
		break;

#if (POWER_MANAGE_FEATURE == FEATURE_SUPPORTED)
	case NVME_FEAT_POWER_MANAGEMENT: //02h
		handle_result = nvmet_feat_set_power_management(req, cmd, sv);
		break;
#endif

	case NVME_FEAT_TEMPERATURE_THRESHOLD: //04h
		handle_result = nvmet_feat_set_tempture_threshold(req, cmd, sv);
		break;
	case NVME_FEAT_ERROR_RECOVERY: //05h
		handle_result = nvmet_feat_set_error_recovery(req, cmd, sv);
		break;
#if (PLP_SUPPORT == 0)						 //because of plp exist
	case NVME_FEAT_VOLATILE_WRITE_CACHE: //06h
		handle_result = nvmet_feat_set_volatile_wr_cache(req, cmd, sv);
		break;
#endif
	case NVME_FEAT_NUMBER_OF_QUEUES: //07h
		handle_result = nvmet_feat_set_number_of_queues(req, cmd);
		break;
	case NVME_FEAT_INTERRUPT_COALESCING: //08h
		handle_result = nvmet_feat_set_interrupt_coalescing(req, cmd, sv);
		break;
	case NVME_FEAT_INTERRUPT_VECTOR_CONFIGURATION: //09h
		handle_result = nvmet_feat_set_interrupt_vector_conf(req, cmd, sv);
		break;
	case NVME_FEAT_WRITE_ATOMICITY: //0Ah
		handle_result = nvmet_feat_set_write_atomic(req, cmd, sv);
		break;
	case NVME_FEAT_ASYNC_EVENT_CONFIGURATION: //0Bh
		handle_result = nvmet_feat_set_async_event_conf(req, cmd, sv);
		break;

#if (AUTO_POWER_STATE_FEATURE == FEATURE_SUPPORTED)
	case NVME_FEAT_AUTONOMOUS_POWER_STATE_TRANSITION: //0Ch
		handle_result = nvmet_feat_set_apst(req, cmd, sv);
		break;
#endif

	case NVME_FEAT_TIMESTAMP: //0Eh  Timestamp
		handle_result = nvme_feat_set_timestamp(req, cmd, sv);
		break;

#if (HOST_THERMAL_MANAGE == FEATURE_SUPPORTED)
	case NVME_FEAT_HOST_THERMAL_MANAGEMENT: //10h
		handle_result = nvmet_feat_set_host_controlled_thermal_mgr(req, cmd, sv);
		break;
#endif

#if CO_SUPPORT_SANITIZE
	case NVME_FEAT_SANITIZE_CONFIG: //17h
		handle_result = nvmet_feat_set_sanitize_conf(req, cmd, sv);
		break;
#endif

#ifdef NS_MANAGE//joe marked 20201202
	case NVME_FEAT_NS_WRITE_PROTECT_CONFIG: //84h
		nvmet_feat_set_ns_wp(req, cmd, sv);
		break;
#endif

#ifdef Dynamic_OP_En
	case NVME_FEAT_Dynamic_OP: //C1h  Dynamic_OP
		handle_result = nvmet_feat_set_Dynamic_OP(req, cmd);
		break;
#endif

#if !defined(PROGRAMMER)
        case NVME_LOG_SAVELOG:
            handle_result = nvme_vsc_ev_log(NULL, cmd, 0);
        if(handle_result == HANDLE_RESULT_FAILURE)
            nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
        break;
#endif//save log Young add 20210714

#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
		case NVME_FEAT_EN_PWRDIS: //D6h
			handle_result = nvmet_feat_set_enable_pwrdis(req, cmd, sv);
			break;
#endif

#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
		case NVME_FEAT_EN_PLP: //F0h
			handle_result = nvmet_feat_set_enable_plp(req, cmd, sv);
			break;
#endif

	case NVME_FEAT_LBA_RANGE_TYPE: //03h
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

if(fid == 0xD1){}//young change for save log 20210715
else{
	if (sv && nvme_info_save)
		nvme_info_save(NULL);
    }

#if PLP_SUPPORT == 0
	if(sv){
		epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		epm_update(SMART_sign, (CPU_ID - 1));
	}
#endif

	return handle_result;
}

/*!
 * @brief get feature of number of queues
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return		not used
 */
static enum cmd_rslt_t nvmet_feat_get_number_of_queues(req_t *req, struct nvme_cmd *cmd, u8 select)
{
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;

#if defined(SRIOV_SUPPORT)
	u8 fnid = req->fe.nvme.cntlid;
	u32 max_q_num = (fnid == 0) ? NVMET_NR_IO_QUEUE : SRIOV_FLEX_VF_IO_Q_PER_FUNC;
#elif defined(NVME_SHASTA_MODE_ENABLE)
	u32 max_q_num = NVMET_NR_IO_QUEUE;
#else
	u32 max_q_num = NVMET_RESOURCES_FLEXIBLE_TOTAL - 1;
#endif

	switch (select)
	{
	case NVME_FEAT_SELECT_CURRENT:
		*cpl_dw0 = ctrlr->def_feat.nque_feat.all;
		break;
	case NVME_FEAT_SELECT_DEFAULT:
		if (ctrlr->attr.b.set_feature_save)
		{
			*cpl_dw0 = ctrlr->def_feat.nque_feat.all;
		}
		else
		{
			*cpl_dw0 = ctrlr->def_feat.nque_feat.all;
		}
		break;
	case NVME_FEAT_SELECT_SAVED:
		*cpl_dw0 = ctrlr->def_feat.nque_feat.all;
		break;
	case NVME_FEAT_SELECT_SUPPORTED:
		*cpl_dw0 = NVME_FEAT_CHANGEABLE | NVME_FEAT_SAVABLE;
		break;
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		*cpl_dw0 = (max_q_num - 1) << 16 | (max_q_num - 1);
		return HANDLE_RESULT_FAILURE;
		break;
	}

	return HANDLE_RESULT_FINISHED;
}

/*!
 * @brief get features support
 *
 * @param req	request
 * @param cmd	nvme command
 *
 *  //       Feature Name               FeatureID   Spt   Savable  NS Spcf  Chgable
 *	//  ----------------------          ---------  -----  -------  -------  -------
 *	//  Arbitration                         01h     YES     YES      NO       YES
 *	//  Power Management                    02h     YES     YES      NO       YES
 *	//  LBA Range Type                      03h     NO      N/A      YES      N/A
 *	//  Temperature Threshold               04h     YES     YES      NO       YES
 *	//  Error Recovery                      05h     YES     YES      YES      YES
 *	//  Volatile Write Cache                06h     YES     YES      NO       YES
 *	//  Number of Queues                    07h     YES     YES      NO       YES
 *	//  Interrupt Coalescing                08h     YES     YES      NO       YES
 *	//  Interrupt Vector Configuration      09h     YES     YES      NO       YES
 *	//  Write Atomicity                     0Ah     YES     YES      NO       YES
 *	//  Async Event Config                  0Bh     YES     YES      NO       YES
 *	//  Auto Pw State Trans                 0Ch     NO      N/A      N/A      N/A
 *	//  Host Memory Buffer                  0Dh     YES     NO       NO       YES
 *	//  Timestamp                           0Eh     NO      N/A      N/A      N/A
 *	//  Host Controlled Thermal Management  10h     NO      N/A      N/A      N/A
 *	//  NOPS Config                         11h     NO      N/A      N/A      N/A
 *
 * @return		Feature depends
 */
static slow_code_ex enum cmd_rslt_t nvmet_get_feature(req_t *req, struct nvme_cmd *cmd)
{
	u8 fid = cmd->cdw10 & 0xff;
	u8 sel = (cmd->cdw10 >> 8) & 0x7; /*SEL field, 0:current, 1:default, 2:saved*/
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;

	nvme_apl_trace(LOG_INFO, 0x3a69, "GetFeatures: FID(0x%x) SEL(%d)", fid, sel);

	if (check_namespaces(req, cmd) == false)
	{
		nvme_apl_trace(LOG_DEBUG, 0x4be3, "error namespace id %d fid %d", cmd->nsid, fid);//PC BASHER FULL RANDOM
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	if (sel >= NVME_FEAT_SELECT_NOT_SUPPORT) {
		nvme_apl_trace(LOG_DEBUG, 0x261e, "error Select %d fid %d", sel, fid);
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);//PC BASHER FULL RANDOM
		return HANDLE_RESULT_FAILURE;
	}

	if(fid == NVME_FEAT_ERROR_RECOVERY)
	{
		if((cmd->nsid == 0) || (cmd->nsid > NVMET_NR_NS))
		{
			nvmet_set_status(&req->fe,NVME_SCT_GENERIC,NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
        	return HANDLE_RESULT_FAILURE;
		}else if(ctrlr->nsid[cmd->nsid - 1].type != NSID_TYPE_ACTIVE){
			nvmet_set_status(&req->fe,NVME_SCT_GENERIC,NVME_SC_INVALID_FIELD);
			return HANDLE_RESULT_FAILURE;
		}
	}

	switch (fid)
	{
	case NVME_FEAT_ARBITRATION: //01h
		handle_result = nvmet_feat_get_arbitration(req, cmd, sel);
		break;

#if (POWER_MANAGE_FEATURE == FEATURE_SUPPORTED)
	case NVME_FEAT_POWER_MANAGEMENT: //02h
		handle_result = nvmet_feat_get_power_management(req, cmd, sel);
		break;
#endif

	case NVME_FEAT_TEMPERATURE_THRESHOLD: //04h
		handle_result = nvmet_feat_get_tempture_threshold(req, cmd, sel);
		break;
	case NVME_FEAT_ERROR_RECOVERY: //05h
		handle_result = nvmet_feat_get_error_recovery(req, cmd, sel);
		break;
#if (PLP_SUPPORT == 0)						 //because of plp exists
	case NVME_FEAT_VOLATILE_WRITE_CACHE: //06h
		handle_result = nvmet_feat_get_volatile_wr_cache(req, cmd, sel);
		break;
#endif
	case NVME_FEAT_NUMBER_OF_QUEUES: //07h
		handle_result = nvmet_feat_get_number_of_queues(req, cmd, sel);
		break;
	case NVME_FEAT_INTERRUPT_COALESCING: //08h
		handle_result = nvmet_feat_get_interrupt_coalescing(req, cmd, sel);
		break;
	case NVME_FEAT_INTERRUPT_VECTOR_CONFIGURATION: //09h
		handle_result = nvmet_feat_get_interrupt_vector_conf(req, cmd, sel);
		break;
	case NVME_FEAT_WRITE_ATOMICITY: //0Ah
		handle_result = nvmet_feat_get_write_atomic(req, cmd, sel);
		break;
	case NVME_FEAT_ASYNC_EVENT_CONFIGURATION: //0Bh
		handle_result = nvmet_feat_get_async_event_conf(req, cmd, sel);
		break;

#if (AUTO_POWER_STATE_FEATURE == FEATURE_SUPPORTED)
	case NVME_FEAT_AUTONOMOUS_POWER_STATE_TRANSITION: //0Ch
		nvmet_feat_get_apst(req, cmd, sel);
		handle_result = HANDLE_RESULT_DATA_XFER;
		break;
#endif

	case NVME_FEAT_TIMESTAMP: //0Eh
		handle_result = nvme_feat_get_timestamp(req, cmd, sel);
		//handle_result = HANDLE_RESULT_DATA_XFER;
		break;

#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED) //D6h
		case NVME_FEAT_EN_PWRDIS:
			handle_result = nvmet_feat_get_pwrdis_status(req, cmd, sel);
			break;
#endif

#if (EN_PLP_FEATURE == FEATURE_SUPPORTED) //F0h
	case NVME_FEAT_EN_PLP:
		handle_result = nvmet_feat_get_plp_status(req, cmd, sel);
		break;
#endif

#if (HOST_THERMAL_MANAGE == FEATURE_SUPPORTED)
	case NVME_FEAT_HOST_THERMAL_MANAGEMENT: //10h
		handle_result = nvmet_feat_get_host_controlled_thermal_mgr(req, cmd, sel);
		break;
#endif

#if CO_SUPPORT_SANITIZE
	case NVME_FEAT_SANITIZE_CONFIG: //17h
		handle_result = nvmet_feat_get_sanitize_conf(req, cmd, sel);
		break;
#endif

/*#ifdef NS_MANAGE//joe marked 20201202
	case NVME_FEAT_NS_WRITE_PROTECT_CONFIG: //84h
		handle_result = nvmet_feat_get_ns_wp(req, cmd, sel);
		break;
#endif*/

	case 0:
	case NVME_FEAT_LBA_RANGE_TYPE:
	case NVME_FEAT_KEEP_ALIVE_TIMER:
	case NVME_FEAT_NON_OPERATIONAL_POWER_STATE_CONFIG:

	case 0x12 ... 0x16:
	case 0x18 ... 0x77:
	case 0x78 ... 0x7D:
	case 0x85 ... 0xBF:
	default:
		//Fixed tnvme 22:0.2.0: Set unsupported/rsvd fields in cmd
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	return handle_result;
}

/*!
 * @brief convert event type to log id
 *
 * @param type	AER event type
 *
 * @return	Log id of AER event type
 */
static u8
nvmet_event_type_to_log_identifier(enum nvme_asynchronous_event_type type)
{
	switch (type)
	{
	case NVME_EVENT_TYPE_ERROR_STATUS:
		return NVME_LOG_ERROR;
	case NVME_EVENT_TYPE_SMART_HEALTH:
		return NVME_LOG_HEALTH_INFORMATION;
	case NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS:
		return NVME_LOG_SANITIZE_STATUS;
	case NVME_EVENT_TYPE_VENDOR_SPECIFIC:
		return 0;
	case NVME_EVENT_TYPE_NOTICE:
		{
			u8 sub = 0;
			sub = find_first_bit((void *)&ctrlr->aer_evt_sts[type], NVME_EVENT_TYPE_MAX_STATUS);
			switch(sub){
				case NOTICE_STS_FIRMWARE_ACTIVATION_STARTING:
					return NVME_LOG_FIRMWARE_SLOT;
				case NOTICE_STS_PROGRAM_FAIL:
				case NOTICE_STS_ERASE_FAIL:
				case NOTICE_STS_GC_READ_FAIL:
					return NVME_LOG_ADDITIONAL_SMART;
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
				case NOTICE_STS_TELEMETRY_LOG_CHANGED:
					return NVME_LOG_TELEMETRY_CTRLR_INIT;
#endif
			default:
				return 0;
				}
		}
	default:
		/* see asynchronous event information field */
		return 0;
	}
}

/*!
 * @brief Updated Journal EVT_POWERON log in init, but reset aer in init too. So set aer in here.
 */
 #if NVME_TELEMETRY_LOG_PAGE_SUPPORT
ddr_code void telemetry_set_aer()
{
	if (epm_telemetry_data == NULL)
		epm_telemetry_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	if(epm_telemetry_data->telemetry_update_ctrlr_signal)
	{
		//epm_telemetry_data->telemetry_update_ctrlr_signal = 0;
		if (ctrlr->cur_feat.aec_feat.b.tln){
			nvmet_evt_aer_in(((NVME_EVENT_TYPE_NOTICE << 16)|NOTICE_STS_TELEMETRY_LOG_CHANGED),0);
		}
	}
}
#endif

/*!
 * @brief host requests Asynchronous Event, return immediately if any
 *
 * @param req	request
 * @param cmd	NVMe command
 *
 * @return	command status
 */
ddr_code static enum cmd_rslt_t
nvmet_async_event_request(req_t *req, struct nvme_cmd *cmd)
{
	enum nvme_asynchronous_event_type event_type = NVME_EVENT_TYPE_ERROR_STATUS;

	nvme_apl_trace(LOG_INFO, 0xf97f, "AsyEventReq");

	if (ctrlr->aer_outstanding > IDFTY_AERL)
	{
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_ASYNC_EVENT_REQUEST_LIMIT_EXCEEDED);
		return HANDLE_RESULT_FAILURE;
	}
	///Andy test IOL Case 1.7 20201020
	/*DM8 NT3 Async Testing
		Case 02: w/NSID = 000000001h , Case 03: w/NSID = Invalid NSID , Case 04: w/NSID = 0ffffffffh , must return : Invalid Field in Command
		Due to nvmet_admin_aer_out() -> nvmet_async_event_request(req, NULL) , the "cmd" will be NULL and not into this condition.*/
	if ((cmd != NULL) && (cmd->nsid != 0))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	telemetry_set_aer();
#endif

	for (; event_type < NUMBER_OF_NVME_EVENT_TYPE; event_type++)
	{
		if ((ctrlr->aer_evt_bitmap & BIT(event_type)) &&
			(!(ctrlr->aer_evt_mask_bitmap & BIT(event_type))))
		{
			///Andy IOL Sanblaze 20201105
				//nvme_apl_trace(LOG_INFO, 0, "[Andy] Event type:%d\n", event_type);
				aer_rsp_t *rsp = (aer_rsp_t *)&req->fe.nvme.cmd_spec;

				rsp->b.async_event_type = event_type;
				rsp->b.async_event_information =
					find_first_bit((void *)&ctrlr->aer_evt_sts[event_type], NVME_EVENT_TYPE_MAX_STATUS);
				rsp->b.log_page_identifier =
					nvmet_event_type_to_log_identifier(event_type);
				/* Mask the event type */
				nvme_apl_trace(LOG_INFO, 0x4642, "[Andy] Event :%x\n", rsp->all);
				ctrlr->aer_evt_mask_bitmap |= BIT(event_type);
				if(rsp->b.async_event_type==NVME_EVENT_TYPE_NOTICE && rsp->b.async_event_information==NOTICE_STS_TELEMETRY_LOG_CHANGED){
					extern epm_info_t *shr_epm_info;
					epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
					epm_smart_data->telemetry_update_ctrlr_signal = 0;
				}
				return HANDLE_RESULT_FINISHED;
		}
	}

	list_del_init(&req->entry);
	list_add_tail(&req->entry, &ctrlr->aer_outstanding_reqs);
	if (req->host_cmd)
	{
		hal_nvmet_put_sq_cmd(req->host_cmd);
		req->host_cmd = NULL;
	}

	ctrlr->aer_outstanding++;
	return HANDLE_RESULT_PENDING_FE;
}

/*!
 * @brief Support nvmet_abort() to check if has admin command need to be aborted
 *
 * @param u16	sqid
 * @param u16	cid
 *
 * @return	true/false
 */
static slow_code bool Is_admin_cmd_aborted(u16 sqid, u16 cid)
{
	struct nvmet_sq *sq;
	struct list_head *cur, *saved;
	req_t *abort_req;
	bool admin_exist = false;

	sq = ctrlr->sqs[sqid];

	list_for_each_safe(cur, saved, &sq->reqs)
	{
		abort_req = container_of(cur, req_t, entry);
		if (abort_req->fe.nvme.cid == cid)
		{
			nvme_apl_trace(LOG_ERR, 0xc108, "req (%x) Op(%d) CID(%d)", abort_req, (u32)abort_req->opcode, abort_req->fe.nvme.cid);

			if (abort_req->req_from == REQ_Q_ADMIN)
			{
				struct nvme_cmd *adm_cmd;
				adm_cmd = abort_req->host_cmd;
				nvme_apl_trace(LOG_ERR, 0x9cb2, "abort adm_cmd Op(%x)\n", adm_cmd->opc);
			}
			admin_exist = true;
			break;
		}
	}

	/* if admin command is not in SQ0, may be aer command */
	if ((sqid == 0) && (admin_exist == false) && (ctrlr->aer_outstanding != 0))
	{
		list_for_each_safe(cur, saved, &ctrlr->aer_outstanding_reqs)
		{
			abort_req = container_of(cur, req_t, entry);
			if (abort_req->fe.nvme.cid == cid)
			{
				nvme_apl_trace(LOG_ERR, 0xd9ed, "AER req (%x) Op(%d) CID(%d)", abort_req, (u32)abort_req->opcode, abort_req->fe.nvme.cid);

				/*Setting AER command status = NVME_SC_ABORTED_BY_REQUEST*/
				nvmet_set_status(&abort_req->fe, NVME_SCT_GENERIC, NVME_SC_ABORTED_BY_REQUEST);

				if (abort_req->completion)
					abort_req->completion(abort_req);

				ctrlr->aer_outstanding--;
				admin_exist = true;
				break;
			}
		}
	}
	return admin_exist;
}

/*!
 * @brief abort a specific command when nvme_timeout takes place
 *
 * It's a best effort command, the command to abort may have already completed,
 * currenlty be in execution, or may be deeply queued.
 * It's implementation specific when the command is not found.
 *
 * @param req	request
 * @param cmd	NVMe command to be aborted
 *
 * @return	command status
 */
ddr_code static enum cmd_rslt_t nvmet_abort(req_t *req, struct nvme_cmd *cmd)
{
	u16 sqid = cmd->cdw10;
	u16 cid = cmd->cdw10 >> 16;
	int running_cms = nvmet_cmd_pending();
	bool admin_exist = false;
	u32 *cpl_dw0 = &req->fe.nvme.cmd_spec;
    #if defined(SRIOV_SUPPORT)
    if(sqid > SRIOV_FLEX_PF_ADM_IO_Q_TOTAL)
    #else
    if(sqid > (NVMET_RESOURCES_FLEXIBLE_TOTAL + 1))
    #endif
    {
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	if ((cmd->nsid != 0)||(ctrlr->sqs[sqid] == NULL))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	if (ctrlr->currAbortCnt == IDFTY_ACL)
	{
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_ABORT_COMMAND_LIMIT_EXCEEDED);
		return HANDLE_RESULT_FAILURE;
	}

	nvme_apl_trace(LOG_ERR, 0x8ebc, "QID(%d) CID(%d) out of (#%d), aborting",
				   sqid, cid, running_cms - 1);

	admin_exist = Is_admin_cmd_aborted(sqid, cid);

	/* if the abort command is admin command, aboort it directly */
	if ((sqid == 0) && (admin_exist))
	{

		*cpl_dw0 = 0; /* the command is aborted */
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_SUCCESS);
		return HANDLE_RESULT_FINISHED;
	}
	else
	{
		cmd_proc_abort_single_cmd(sqid, cid);
		list_add_tail(&req->entry, &ctrlr->sqs[0]->reqs);
		ctrlr->currAbortCnt++;

		/*
		* Comments from Linux:
		* A "mimimum viable" abort implementation: the command is mandatory in
		* the spec, but we are not required to do any useful work.
		* We couldn't really do a useful abort, so don't bother even with
		* waiting for the command to be exectuted and return immediately
		* telling the command to abort wasn't found.
		*/

		*cpl_dw0 = 1; /* the command is not aborted */
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_SUCCESS);

		return HANDLE_RESULT_RUNNING;
	}
}

#if 0
/*!
 * @brief operation code string name
 *
 * @param opc	operation code
 *
 * @return	Verbose name for the operation code
 */
static const char *nvmet_admin_opc_sa_name(u16 opc)
{
	if (opc < 0x80)
	{
		if (opc < ARRAY_SIZE(opcode_names))
		{
			if (opcode_names[opc] != NULL)
				return opcode_names[opc];
		}
	}
	else
	{
		opc -= 0x80;
		if (opc < ARRAY_SIZE(opcode_names_80))
		{
			if (opcode_names_80[opc] != NULL)
				return opcode_names_80[opc];
		}
	}
	return "NIL";
}
#endif

#if CO_SUPPORT_SECURITY
enum security_xfer_type
{
	NO_XFER = 0,
	ATA_CMD_XFER,
	TCG_CMD_XFER,
};


ddr_code static void nvmet_security_mem_free(req_t *req)
{
	struct nvme_cmd *cmd = req->host_cmd;
	dtag_t *dtag;

	nvme_apl_trace(LOG_INFO, 0x1ce4, "req(%x) opcode(%x)", req, cmd->opc);
#if 0
	dtag = (dtag_t *)req->req_prp.mem;
	dtag_put(DTAG_T_SRAM, dtag[0]);
	sys_free(SLOW_DATA, req->req_prp.mem);
	evt_set_imt(evt_cmd_done, (u32)req, 0);
#else
	if(++req->req_prp.transferred == req->req_prp.required)
	{

		dtag = (dtag_t *)req->req_prp.mem;
		for(u8 i=0; i<(req->req_prp.mem_sz); i++)
			dtag_put(DTAG_T_SRAM, dtag[i]);

		sys_free(SLOW_DATA, req->req_prp.mem);

		sys_free(SLOW_DATA, req->req_prp.prp);

		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
#endif
}

ddr_code static void nvmet_security_mem_rdy(void *hreq)
{
	req_t *req = (req_t *)hreq;
	struct nvme_cmd *cmd = req->host_cmd;
#if 0
	dtag_t *dtag;
	u8 *mem;
	int len = min(cmd->cdw11, DTAG_SZE);
#endif
	nvme_apl_trace(LOG_INFO, 0xa814, "req(%x) opcode(%x)", req, cmd->opc);
#if 0
	dtag = req->req_prp.mem;
	mem = dtag2mem(dtag[0].b.dtag);
	nvmet_security_handle_cmd(req, cmd, mem, len);
#else
	nvme_apl_trace(LOG_INFO, 0x0e20, "%s, todo\n", __FUNCTION__);
  #if _TCG_ != TCG_NONE
	u32 pid = ((nvme_tcg_cmd_dw10_t)cmd->cdw10).b.protocol_id;
	if(pid == c_SECURITY_INFO || pid == c_TCG_PID_01 || pid == c_TCG_PID_02){
		if(tcg_cmd_handle(req) == HANDLE_RESULT_FAILURE){
			// error handle
			nvme_apl_trace(LOG_ERR, 0x1956, "Tcg cmd Error, TPer sts:0x%x", tcg_tper_status);
			if(tcg_tper_status == TPER_SYNC_PROTOCOL_VIOLATION){
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_COMMAND_SEQUENCE_ERROR);  // DM A1-1-3-1-3
			}else{
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
			}
			struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
			nvme_status->dnr = 1;
		}
	}
  #endif

#endif
}
// Jack Li: temp macro
//#define SEC_CMD_MAX_XFER_LEN 0xD000
//#define SECURE_COM_BUF_SZ 0xD000

extern bool tcg_ioCmd_inhibited;


ddr_data const u8 SECURITY_SUPPORT_PID_LIST_NO_TCG[] = {
		c_SECURITY_INFO,	 //according SPC-4 spec shall ascending order
    #if (_TCG_)
		c_TCG_PID_01,
		c_TCG_PID_02,
    #endif
    #if CO_SUPPORT_NVME_PID_EA
		c_NVME_PID_EA,
    #endif
    #if (_TCG_ == TCG_EDRV)
		c_IEEE1667_PID,
    #endif
    #if (CO_SUPPORT_ATA_SECURITY == TRUE)
		c_ATA_SECURITY
    #endif
};


ddr_code void prepare_support_protocol_list(u8 *pbuf)
{
    //[6~7] padload length
    pbuf += 6;

    //IrvingWang,20200420. The total data length shall be 512 bytes. Pad bytes are appended as needed to meet this requirement. Pad
    //bytes shall have a value of 00h.
    *pbuf++ = 0x00;
    //*pbuf++ = 0x03;

#if (_TCG_ != TCG_NONE) && (CO_SUPPORT_ATA_SECURITY == TRUE)
    if(mTcgStatus & TCG_ACTIVATED) {
		*pbuf++ = 0x05;
        memcpy(pbuf, SECURITY_SUPPORT_PID_LIST_NO_ATA, sizeof(SECURITY_SUPPORT_PID_LIST_NO_ATA));
    }else if(AatSecuriytActivated()){
		*pbuf++ = 0x06;
        memcpy(pbuf, SECURITY_SUPPORT_PID_LIST_NO_TCG, sizeof(SECURITY_SUPPORT_PID_LIST_NO_TCG));
    }
    else
#endif
    {
#if (_TCG_)
		epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		if(epm_aes_data->tcg_en_dis_tag != TCG_TAG)
		{
			*pbuf++ = 0x01;
			*pbuf = c_SECURITY_INFO;
		}
		else
#endif
		{
			*pbuf++ = 0x03;
			memcpy(pbuf, SECURITY_SUPPORT_PID_LIST_NO_TCG, sizeof(SECURITY_SUPPORT_PID_LIST_NO_TCG));
		}
    }
}


ddr_code enum cmd_rslt_t nvmet_security_send_cmd(req_t *req, struct nvme_cmd *cmd)
{
    //u8 *mem = NULL;
    //u32 len = min(cmd->cdw11, DTAG_SZE);
    u32 len = cmd->cdw11;
	u8 protocol;
#if _TCG_ != TCG_NONE
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
#endif
#if 0
	nvme_tcg_cmd_dw10_t tcg_cdw10 = {.all = cmd->cdw10};
    if(epm_aes_data->tcg_en_dis_tag == TCG_TAG)
		protocol = tcg_cdw10.b.protocol_id;
	else
#endif
		protocol = cmd->cdw10 >> 24;

  #if 0//(CO_SUPPORT_ATA_SECURITY == TRUE) // for ATA cmd, marked by Jack Li
    u16 SP_Specific = tcg_cdw10.b.com_id;
  #endif
    enum cmd_rslt_t rslt = HANDLE_RESULT_FAILURE;
    enum security_xfer_type xferType = NO_XFER;

	nvme_apl_trace(LOG_INFO, 0x9920, "nvmet_security_send_cmd, protocol %x", protocol);

    /* Check for valid protocol */
    switch (protocol) {
      #if 0//(CO_SUPPORT_ATA_SECURITY == TRUE) // for ATA cmd, marked by Jack Li
        case c_ATA_SECURITY:

            #if _TCG_ != TCG_NONE
            if(mTcgStatus & TCG_ACTIVATED) {
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
                return HANDLE_RESULT_FAILURE;
            }
            #endif

            if ((SP_Specific == c_SATA_CMD_SetPassword) || (SP_Specific == c_SATA_CMD_Unlock) ||
                (SP_Specific == c_SATA_CMD_EraseUnit) || (SP_Specific == c_SATA_CMD_DisablePassword))
            {
                if (len != 0x24)
                {
                    nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
                    return HANDLE_RESULT_FAILURE;
                }
                xferType = ATACMD_XFER;
            }
            else
            {
                rslt = ATA_SecuritySend((tSATA_PW_DATA *)mem, req);
            }
            break;
      #endif



        #if _TCG_ != TCG_NONE
        case c_TCG_PID_01 ... c_TCG_PID_02:
        /* core 3.3, only support 0x1 and 0x2 */
			if(epm_aes_data->tcg_en_dis_tag == TCG_TAG){
            	if ((len > mTperProperties[0].val) || (len == 0)){   // len = 0 or len > MaxComPacketSize
            	
					nvme_apl_trace(LOG_INFO, 0xa929, "[TCG] NG!! xfer len equal 0");
                	nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
					struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
					nvme_status->dnr = 1;
                	return HANDLE_RESULT_FAILURE;
            	}
            	//else if (!len)
                //	break;
            	xferType = TCG_CMD_XFER;

            	break;
			}
        #endif

        default:
            nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
			struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
			nvme_status->dnr = 1;
            return HANDLE_RESULT_FAILURE;
    }

    if (xferType != NO_XFER)
    {
        rslt = nvmet_map_admin_prp(req, cmd, len, nvmet_admin_security_send_xfer_done);

        if (xferType == TCG_CMD_XFER){
            req->req_prp.sec_xfer_buf = secure_com_buf;
        }else{  //ATACMD_XFER
            req->req_prp.sec_xfer_buf = NULL;  //set null for prp list xfer
        }
		/*
		if (rslt == HANDLE_RESULT_DATA_XFER)
		{
			hal_nvmet_data_xfer(cmd->dptr.prp.prp1, mem, len,
								READ, (void *)req, nvmet_admin_security_send_xfer_done);
		}
		*/
    }
    return rslt;
}

ddr_code enum cmd_rslt_t nvmet_security_received_cmd(req_t *req, struct nvme_cmd *cmd)
{
    //void *mem;
    //u32 len = min(cmd->cdw11, DTAG_SZE);
    u32 len = cmd->cdw11;
	u8 protocol = 0;
	u16 SP_Specific = 0;
	enum security_xfer_type xferType = NO_XFER;
    enum cmd_rslt_t rslt = HANDLE_RESULT_FAILURE;

#if _TCG_ != TCG_NONE
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
#endif
#if 0
	nvme_tcg_cmd_dw10_t tcg_cdw10 = {.all = cmd->cdw10};
	if(epm_aes_data->tcg_en_dis_tag == TCG_TAG)
	{
		protocol = tcg_cdw10.b.protocol_id;
		SP_Specific = tcg_cdw10.b.com_id;
	}
	else
#endif
	{
		protocol = cmd->cdw10 >> 24;
		SP_Specific = (cmd->cdw10 >> 8) & 0xFFFF;
	}

    //u64 PRP1 = cmd->dptr.prp.prp1;
    //u64 PRP2 = cmd->dptr.prp.prp2;

    nvme_apl_trace(LOG_INFO, 0x8e67, "nvmet_security_received_cmd, protocol %x SP_Specific %x dw11 %x", protocol, SP_Specific, cmd->cdw11);

    if ((protocol == 0) && (len == 0))
    {
		nvme_apl_trace(LOG_INFO, 0x1898, "[TCG] Security Protocol (SPSP) equal 0");
		return HANDLE_RESULT_FINISHED;

	}
    //if ((cmd->cdw11 == 0) && (protocol != 0) && (SP_Specific !=0))    
    //if ((cmd->cdw11 == 0) && (protocol == 0) && )


	if (len == 0)
    {   
	    nvme_apl_trace(LOG_INFO, 0xa24b, "[TCG] NG!! xfer len equal 0");
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
		nvme_status->dnr = 1;
		return HANDLE_RESULT_FAILURE;
        //return HANDLE_RESULT_FINISHED;
    }

    /* Check for valid protocol */
    switch (protocol) {

        case c_SECURITY_INFO:
            if (SP_Specific == 0)
            {
                prepare_support_protocol_list(secure_com_buf); // marked by Jack Li
            }
            xferType = ATA_CMD_XFER;
            break;
      #if 0//(CO_SUPPORT_ATA_SECURITY == TRUE) // for ATA cmd, marked by Jack Li
        case c_ATA_SECURITY:
            #if _TCG_ != TCG_NONE
            if(mTcgStatus & TCG_ACTIVATED) {
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
                return HANDLE_RESULT_FAILURE;
            }
            #endif

            if (SP_Specific != c_SATA_CMD_SecurityInfo)
            {
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
                return HANDLE_RESULT_FAILURE;
            }

            rslt = ATA_SecurityReceive((ATA_SecurityReceive_t *)mem,req);
            xferType = ATACMD_XFER;
            break;
      #endif

        #if _TCG_ != TCG_NONE
        case c_TCG_PID_01 ... c_TCG_PID_02:
        /* core 3.3, only support 0x1 and 0x2 */
			if(epm_aes_data->tcg_en_dis_tag == TCG_TAG){
            	if ((len > mTperProperties[0].val)){   // len > MaxComPacketSize
                	nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
					struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
					nvme_status->dnr = 1;
                	return HANDLE_RESULT_FAILURE;
            	}
            	xferType = TCG_CMD_XFER;
            	break;
			}
        #endif



        default:
            nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
			struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
			nvme_status->dnr = 1;
            return HANDLE_RESULT_FAILURE;

    } //switch

    if (xferType != NO_XFER)
    {
    	req->req_prp.sec_xfer_buf = secure_com_buf;
        if (xferType == TCG_CMD_XFER){
            //req->req_prp.sec_xfer_buf = secure_com_buf;
          #if _TCG_ != TCG_NONE
			if(epm_aes_data->tcg_en_dis_tag == TCG_TAG)
            	rslt = tcg_cmd_handle(req);
          #endif
			nvme_apl_trace(LOG_INFO, 0x5162, "TCG in nvmet_security_received_cmd(), todo");
            //memcpy(dtag2mem(*((dtag_t *)req->req_prp.mem)), req->req_prp.sec_xfer_buf, DTAG_SZE);
			//dtag_put(DTAG_T_SRAM, (dtag_t)mem2sdtag(secure_com_buf));
			//dtag_put(DTAG_T_SRAM, (dtag_t)mem2sdtag(secure_com_buf+0x1000));
			//dtag_put(DTAG_T_SRAM, (dtag_t)mem2sdtag(secure_com_buf+0x2000));
			//secure_com_buf = NULL;
            if(rslt == HANDLE_RESULT_FAILURE){
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);  //DM A1-2-3-2-2
                struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
				nvme_status->dnr = 1;
            }

        }/*else{  //ATACMD_XFER
            req->req_prp.sec_xfer_buf = NULL;  //set null for prp list xfer
        }*/
        req->req_prp.data_size = len;

        rslt = nvmet_map_admin_prp(req, cmd, len, nvmet_admin_security_receive_xfer_done);
		/*
        if (xferType == TCG_CMD_XFER){
            req->req_prp.sec_xfer_buf = secure_com_buf;
        }else{  //ATACMD_XFER
            req->req_prp.sec_xfer_buf = NULL;  //set null for prp list xfer
        }

		if (rslt == HANDLE_RESULT_DATA_XFER)
		{
			hal_nvmet_data_xfer(cmd->dptr.prp.prp1, mem, len,
								WRITE, (void *)req, nvmet_admin_security_receive_xfer_done);;
		}
		*/
    }
    return rslt;
}



/*!
 * @brief security command handler
 *
 * @param req	request
 * @param cmd	NVM command
 *
 * @return	command status
 */
ddr_code static enum cmd_rslt_t nvmet_security_cmd(req_t *req, struct nvme_cmd *cmd)
{
	u8 *mem;
	dtag_t *dtag;
	//u32 len = min(cmd->cdw11, DTAG_SZE);
	u32 len = cmd->cdw11;
	u8 protocol = cmd->cdw10 >> 24;
	u32 i =0;
	enum cmd_rslt_t rslt = HANDLE_RESULT_FAILURE;

	nvme_apl_trace(LOG_INFO, 0xa467, "req(%x) opcode(%x) len %x", req, cmd->opc, len);
	nvme_apl_trace(LOG_INFO, 0xe22d, "protocol(%x) ComId(%x)", protocol, (u16)(cmd->cdw10 >> 8));

#if (_TCG_) //_TCG_ != TCG_NONE // from CA6, marked by Jack Li
	//if(isTcgIfReady() == FALSE){
	extern bool bTcgTblErr;
	if(bTcgTblErr)
	{
		nvme_apl_trace(LOG_ERR, 0x5713, "[TCG] Table Err, blk security cmd !!");
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}
	/*
	if(isTcgNfBusy){
		nvme_apl_trace(LOG_WARNING, 0, "[TCG] Warnning!!, TCG Nf is not ready");

		if(!list_empty(&_admin_req_pending)){
            list_add_tail(&req->entry2, &_admin_req_pending);
    		evt_set_cs(evt_admin_cmd_check, 0, 0, CS_TASK);
            return HANDLE_RESULT_PENDING_FE;
        }
	}
	*/
#endif

#if 0//(AUTO_REFORMAT == TRUE) // from CA6, trusted send only, marked by Jack Li
	if (SetDeviceEvent.d.Ev_ReFormating)
	{
		Push_to_waiting_cmd(req);
		return HANDLE_RESULT_PENDING_FE;
	}
#endif

	if (len % 4)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
		nvme_status->dnr = 1;
		return HANDLE_RESULT_FAILURE;
	}

	if (secure_com_buf == NULL)
	{
		nvme_apl_trace(LOG_ERR, 0xaa5e, "error! allocate secure_com_buf fail ...");
		return HANDLE_RESULT_FAILURE;
	}
#if (_TCG_)
	memset(secure_com_buf, 0, SECURE_COM_BUF_SZ);
#else
	memset(secure_com_buf, 0, DTAG_SZE);
#endif

	//if(len != 0)
	{
		nvmet_alloc_admin_res(req, ((len <= DTAG_SZE)?DTAG_SZE:(DTAG_SZE<<1)));
		dtag = req->req_prp.mem;
		if(req->req_prp.mem_sz > 1)
		{
			mem = dtag2mem(dtag[1]);
			memset(mem, 0, DTAG_SZE);
		}
		mem = dtag2mem(dtag[0]);
		memset(mem, 0, DTAG_SZE);
	}

	if (cmd->opc == NVME_OPC_SECURITY_SEND)
    {
        rslt = nvmet_security_send_cmd(req, cmd);

        if (rslt == HANDLE_RESULT_DATA_XFER)
        {
            u32 ofst = 0;
            for (i = 0; i < req->req_prp.nprp; i++)
            {
                req->req_prp.required++;

				if((i>0) && (req->req_prp.mem_sz > 1))
				{
					// 2 DTAG is NOT continuous
					if(dtag[1].dtag != (dtag[0].dtag+1))
					{
						// PRP2 begins form 2nd DTAG
						// 2nd DTAG w/o offset
						if(ofst == DTAG_SZE)
						{
							mem = dtag2mem(dtag[1]);
							ofst = 0;
						}
						else
						{
							// prp[0]:PRP1->Dtag0, prp[1]:PRP2->Dtag0, prp[2]:PRP2->Dtag1,
							req->req_prp.nprp++;
							u32 rem_dtag = DTAG_SZE - ofst;
							req->req_prp.prp[i+1].prp = req->req_prp.prp[i].prp + rem_dtag;
							req->req_prp.prp[i+1].size = req->req_prp.prp[i].size - rem_dtag;
							req->req_prp.prp[i].size = rem_dtag;
						}
					}
				}

                hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(mem, ofst), req->req_prp.prp[i].size,
                                READ, (void *)req, nvmet_admin_security_send_xfer_done);
                ofst += req->req_prp.prp[i].size;
            }
        }

    }
    else if (cmd->opc == NVME_OPC_SECURITY_RECEIVE)
    {
        // rslt = nvmet_security_handle_cmd(req, cmd, mem, DTAG_SZE); //dbg syu
        //nvme_apl_trace(LOG_ERR, 0, "nvmet_security_cmd, todo");

        rslt = nvmet_security_received_cmd(req, cmd);

        if (rslt == HANDLE_RESULT_DATA_XFER)
        {
            u32 ofst = 0;
			memcpy(dtag2mem(*((dtag_t *)req->req_prp.mem)), req->req_prp.sec_xfer_buf, DTAG_SZE);
			if(req->req_prp.mem_sz > 1)
				memcpy(dtag2mem(*((dtag_t *)req->req_prp.mem+1)), req->req_prp.sec_xfer_buf+DTAG_SZE, (len-DTAG_SZE));
            for (i = 0; i < req->req_prp.nprp; i++)
            {
                req->req_prp.required++;

				if((i>0) && (req->req_prp.mem_sz > 1))
				{
					// 2 DTAG is NOT continuous
					if(dtag[1].dtag != (dtag[0].dtag+1))
					{
						// PRP2 begins form 2nd DTAG
						// 2nd DTAG w/o offset
						if(ofst == DTAG_SZE)
						{
							mem = dtag2mem(dtag[1]);
							ofst = 0;
						}
						else
						{
							// prp[0]:PRP1->Dtag0, prp[1]:PRP2->Dtag0, prp[2]:PRP2->Dtag1,
							req->req_prp.nprp++;
							u32 rem_dtag = DTAG_SZE - ofst;
							req->req_prp.prp[i+1].prp = req->req_prp.prp[i].prp + rem_dtag;
							req->req_prp.prp[i+1].size = req->req_prp.prp[i].size - rem_dtag;
							req->req_prp.prp[i].size = rem_dtag;
						}
					}
				}

                hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(mem, ofst), req->req_prp.prp[i].size,
                                WRITE, (void *)req, nvmet_admin_security_receive_xfer_done);
                ofst += req->req_prp.prp[i].size;
            }
        }

    }


//-----------------------
#if 0
	if (cmd->opc == NVME_OPC_SECURITY_SEND)
	{
		rslt = nvmet_security_send_cmd(req, cmd);

		if (rslt == HANDLE_RESULT_DATA_XFER)
		{
			hal_nvmet_data_xfer(cmd->dptr.prp.prp1, mem, len,
								READ, (void *)req, nvmet_admin_security_send_xfer_done);
		}

	}
	else if (cmd->opc == NVME_OPC_SECURITY_RECEIVE)
	{
		// rslt = nvmet_security_handle_cmd(req, cmd, mem, DTAG_SZE); //dbg syu
		//nvme_apl_trace(LOG_ERR, 0, "nvmet_security_cmd, todo");

		rslt = nvmet_security_received_cmd(req, cmd);

		if (rslt == HANDLE_RESULT_DATA_XFER)
		{
			hal_nvmet_data_xfer(cmd->dptr.prp.prp1, mem, len,
								WRITE, (void *)req, nvmet_admin_security_receive_xfer_done);
		}

	}

#endif

	if ((rslt != HANDLE_RESULT_DATA_XFER) && (rslt != HANDLE_RESULT_PRP_XFER))
		nvmet_free_admin_res(req);

	return rslt;
}

#if (_TCG_)

slow_code bool nvmet_adm_cmd_tcg_chk(req_t *req)
{
	bool sts = false;
	struct nvme_cmd *cmd = req->host_cmd;

	//nvme_apl_trace(LOG_INFO, 0x2803, "[TCG] admin cmd chk, op: %d", cmd->opc);

	switch (cmd->opc)
    {
    /*
        case NVME_OPC_FIRMWARE_COMMIT:
        case NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD:
            if(mWriteLockedStatus || mReadLockedStatus || (mTcgStatus & MBR_SHADOW_MODE))
            { // if drive is locked or in MBR-S mode, FW update is not allowed!
               nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
               sts = true;
               nvme_apl_trace(LOG_ERR, 0x5e6e, "[TCG] FW update inhibitted, rdlck: %d, wrlck: %d, sts: %d", mReadLockedStatus, mWriteLockedStatus, mTcgStatus);
            }
            break;
	*/
        case NVME_OPC_SANITIZE:
            if(mTcgStatus & TCG_ACTIVATED)
            {
                nvmet_set_status(&req->fe, NVME_SCT_MEDIA_ERROR, NVME_SC_ACCESS_DENIED);
                sts = true;
                nvme_apl_trace(LOG_ERR, 0xd024, "[TCG] sanitize inhibitted, sts: %d", mTcgStatus);
            }
            break;

        case NVME_OPC_FORMAT_NVM:
            //if(mTcgStatus&TCG_ACTIVATED)    //SIIS_v1.04
            if(mWriteLockedStatus)        //SIIS_v1.05,
            {
                nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
                sts = true;
                nvme_apl_trace(LOG_ERR, 0xf36a, "[TCG] format inhibitted, wrlck: %d", mWriteLockedStatus);
            }
            break;

		case NVME_OPC_NS_MANAGEMENT:
            //if(mTcgStatus&TCG_ACTIVATED)    //SIIS_v1.04
            //if(mWriteLockedStatus)          //SIIS_v1.05,
            //SIIS_v1.10,
            if((mWriteLockedStatus&0x1) || (mReadLockedStatus&0x1) || nonGR_set)
            {
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_OPERATION_DENIED);
                sts = true;
                nvme_apl_trace(LOG_ERR, 0xbeed, "[TCG] NS MGR inhibitted, wrlck: %d, rdlck: %d, nonGR set: %d,", mWriteLockedStatus, mWriteLockedStatus, nonGR_set);
            }
            break;
    }

    return sts;
}

#endif

#endif

/*!
 * @brief nvme delet sq handle
 *
 * @param data	not use
 *
 * @return	not use
 */
ddr_code void nvme_delete_sq_post_handle(void* data)
{
	struct list_head *cur_sq_req, *saved_sq_req;

	list_for_each_safe(cur_sq_req, saved_sq_req, &nvme_sq_del_reqs) {
		req_t *req_curr = container_of(cur_sq_req, req_t, entry);
		struct nvmet_sq *sq = NULL;

		u32 flex_sqid = req_curr->op_fields.admin.sq_id;
		sq = ctrlr->sqs[flex_sqid];

		extern int btn_cmd_idle(void);

		if ((nvme_hal_check_pending_cmd_by_sq(flex_sqid)) &&
			(hal_nvmet_check_io_sq_pending()) &&
			(btn_cmd_idle()) &&
			(get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds == 0)) {

			struct list_head *cur, *saved;

			list_for_each_safe(cur, saved, &sq->reqs)
			{
				req_t *req_cur = container_of(cur, req_t, entry);

				list_del_init(&req_cur->entry);
				list_add_tail(&req_cur->entry, &ctrlr->aborted_reqs);
			}
			cmd_proc_delete_sq(flex_sqid, req_curr);

			list_del_init(&req_curr->entry);
		}
	}

	if (list_empty(&nvme_sq_del_reqs)) {
		del_timer(&sq_del_timer);
		nvme_apl_trace(LOG_ALW, 0x2018,"delet all post sq done");
	} else {
		mod_timer(&sq_del_timer, jiffies + 5);
		nvme_apl_trace(LOG_ALW, 0xf889,"start timer again");
	}

}

/*!
 * @brief NVMe admin command core handler
 *
 * @param req	request
 *
 * @return	error code
 */
static fast_code enum cmd_rslt_t nvmet_asq_handle(req_t *req)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;
	struct nvme_cmd *cmd = req->host_cmd;
	//nvme_apl_trace(LOG_DEBUG, 0, "req(%x) Admin opcode(%x): %s", req, cmd->opc,
	//			   nvmet_admin_opc_sa_name(cmd->opc));

#if (_TCG_)
	if(mTcgStatus & TCG_ACTIVATED)
		if(nvmet_adm_cmd_tcg_chk(req))
			return HANDLE_RESULT_FAILURE;
#endif

#if CO_SUPPORT_SANITIZE
	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	sanitize_status_t sntz_sts = (epm_smart_data->sanitizeInfo.sanitize_log_page & 0x70000)>>16;
	if(epm_smart_data->sanitizeInfo.sanitize_Tag == 0xDEADFACE)
	{
		if ((epm_smart_data->sanitizeInfo.fwSanitizeProcessStates != sSanitize_FW_None) || (sntz_sts == sSanitizeCompletedFail))
		{
			nvme_apl_trace(LOG_ERR, 0xd44b, "Sanitize Sts(inFW): 0x%x, (inSSTAT): 0x%x", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates, sntz_sts);
			bool sanitize_allow = (epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_None);
			if (sanitize_admin_check(req, cmd, sanitize_allow) == HANDLE_RESULT_FAILURE)
			{
				if(sanitize_allow)
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_FAILED);
				else
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_IN_PROGRESS);
				return HANDLE_RESULT_FAILURE;
			}
		}
	}
	else
	{
		epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_None;
		//epm_smart_data->sanitizeInfo.sanitize_log_page &= ~(0xFF0000);
		//epm_smart_data->sanitizeInfo.sanitize_log_page |= (0xFFFF);
		epm_smart_data->sanitizeInfo.sanitize_log_page = 0xFFFF;
		epm_smart_data->sanitizeInfo.handled_wr_cmd_cnt = 0;
		epm_smart_data->sanitizeInfo.bmp_w_cmd_sanitize = 0;
		epm_smart_data->sanitizeInfo.sanitize_Tag = 0xDEADFACE;
		epm_update(SMART_sign, (CPU_ID - 1));
	}
#endif

	//nvme_apl_trace(LOG_ERR, 0, "admin cmd in\n");
	if((cmd->fuse != 0) || (cmd->psdt != 0) || (cmd->rsvd1 != 0))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		nvme_apl_trace(LOG_DEBUG, 0x2a4d, "fused:%x psdt:%x rsvd1:%x",cmd->fuse, cmd->psdt, cmd->rsvd1);//PC BASHER FULL RANDOM
		return HANDLE_RESULT_FAILURE;
	}

	switch (cmd->opc)
	{
	case NVME_OPC_DELETE_IO_SQ:
		handle_result = nvmet_delete_io_sq(req, cmd);
		break;
	case NVME_OPC_CREATE_IO_SQ:
		handle_result = nvmet_create_io_sq(req, cmd);
		break;
	case NVME_OPC_GET_LOG_PAGE:
//        nvme_apl_trace(LOG_ERR, 0, "nvme_vsc_ev_log cdw10:(%d) cdw11:(%d) cdw12:(%d) cdw13:(%d) cdw14:(%d) cdw15:(%d)", cmd->cdw10, cmd->cdw11, cmd->cdw12, cmd->cdw13, cmd->cdw14, cmd->cdw15);
		handle_result = nvmet_get_log_page(req, cmd);
        //nvme_apl_trace(LOG_ALW, 0, "handle result:0x%x",handle_result);
		break;
	case NVME_OPC_DELETE_IO_CQ:
		handle_result = nvmet_delete_io_cq(req, cmd);
		break;
	case NVME_OPC_CREATE_IO_CQ:
		handle_result = nvmet_create_io_cq(req, cmd);
		break;
	case NVME_OPC_IDENTIFY:
		handle_result = nvmet_identify(req, cmd);
		break;
	case NVME_OPC_ABORT:
		handle_result = nvmet_abort(req, cmd);
		break;
	case NVME_OPC_SET_FEATURES:
		handle_result = nvmet_set_feature(req, cmd);
        //nvme_apl_trace(LOG_ALW, 0, "handle result feature:0x%x",handle_result);
		break;
	case NVME_OPC_GET_FEATURES:
		handle_result = nvmet_get_feature(req, cmd);
		break;
	case NVME_OPC_ASYNC_EVENT_REQUEST:
		handle_result = nvmet_async_event_request(req, cmd);
		break;
	case NVME_OPC_FIRMWARE_COMMIT:
		handle_result = nvmet_firmware_commit(req, cmd);
		break;
	case NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD:
		handle_result = nvmet_firmware_image_download(req, cmd);
		break;
#if defined(DST)
	case NVME_OPC_DEV_SELF_TEST:
		handle_result = nvmet_dev_self_test(req, cmd);
		break;
#endif

#if VALIDATE_BOOT_PARTITION
	case NVME_OPC_VSC_BP_READ:
		handle_result = nvmet_boot_partition_read(req, cmd);
		break;
#endif
		/* Optional and not supported in oacs */
	case NVME_OPC_FORMAT_NVM:
#if 1
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
    	if(PLN_open_flag == true){
	        PLN_FORMAT_SANITIZE_FLAG = true;
        }
#endif
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif

		gFormatFlag = true;
		nvmet_io_fetch_ctrl(true);
        if((!list_empty(&_admin_req_pending)) || (!btn_wr_cmd_idle()) || (!btn_rd_cmd_idle()) 
			|| (shr_lock_power_on & (SLC_LOCK_POWER_ON | FQBT_LOCK_POWER_ON | NON_PLP_LOCK_POWER_ON))
			||!cmd_proc_slot_check()){
            list_add_tail(&req->entry2, &_admin_req_pending);
    		evt_set_cs(evt_admin_cmd_check, 0, 0, CS_TASK);
            handle_result = HANDLE_RESULT_PENDING_FE;
        }
        else
		{
			handle_result = nvmet_format_ns(req, cmd);
			if(handle_result == HANDLE_RESULT_FAILURE)
			{
				gFormatFlag = false;
				nvmet_io_fetch_ctrl(false);
				if (gFormatInProgress)
				{
					gFormatFlag = true;
					btn_de_wr_disable();
					evt_set_cs(evt_admin_format_confirm, 0, 0, CS_TASK);
				}
			}
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
            if((PLN_GPIOISR_FORMAT_SANITIZE_FLAG == true)&&(PLN_open_flag == true))
            {
                PLN_GPIOISR_FORMAT_SANITIZE_FLAG = false;
                evt_set_cs(evt_pln_format_sanitize,0,0,CS_TASK);
            }
            PLN_FORMAT_SANITIZE_FLAG = false;
#endif
        }
#else
		nvmet_set_status(req, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
		handle_result = HANDLE_RESULT_FAILURE;
#endif
		break;
#if CO_SUPPORT_SECURITY
	case NVME_OPC_SECURITY_SEND:
	case NVME_OPC_SECURITY_RECEIVE:
		handle_result = nvmet_security_cmd(req, cmd);
		break;
#endif
#if defined(OC_SSD)
	case NVME_OPC_OCSSD_V20_GEOMETRY:
		handle_result = nvmet_open_channel_geometry(req, cmd);
		break;
#endif
#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
	case NVME_OPC_VIRTUALIZATION_MANAGEMENT:
		handle_result = nvmet_virt_mngmt_cmd(req, cmd);
		break;
#endif
#if (NS_MANAGE == FEATURE_SUPPORTED)
	case NVME_OPC_NS_MANAGEMENT:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		if(cmd->cdw10 & BIT0)//delete ns
		{
			nvmet_io_fetch_ctrl(true);
			if((!list_empty(&_admin_req_pending)) || (!btn_wr_cmd_idle()) || (!btn_rd_cmd_idle()) || (!btn_rd_cmd_idle()) 
                || (shr_lock_power_on & (SLC_LOCK_POWER_ON | FQBT_LOCK_POWER_ON | NON_PLP_LOCK_POWER_ON))
                ||!cmd_proc_slot_check())
			{
				list_add_tail(&req->entry2, &_admin_req_pending);
				evt_set_cs(evt_admin_cmd_check, 0, 0, CS_TASK);
				handle_result = HANDLE_RESULT_PENDING_FE;
			}
			else
			{
				handle_result = nvmet_ns_manage(req, cmd);
				if(handle_result == HANDLE_RESULT_FAILURE)
				{
					nvmet_io_fetch_ctrl(false);
				}
			}
		}
		else
		{
			handle_result = nvmet_ns_manage(req, cmd);
		}
		break;

	case NVME_OPC_NS_ATTACHMENT:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		handle_result = nvmet_ns_ctrlr_attach(req, cmd);
		break;
#endif

#if CO_SUPPORT_SANITIZE
	///Andy add for IOL Sanitize need return INVALID Test1.17 Case2
	case NVME_OPC_SANITIZE:
	//nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
    	if(PLN_open_flag == true){
	        PLN_FORMAT_SANITIZE_FLAG = true;
        }
#endif
		nvmet_io_fetch_ctrl(true);
		if((!list_empty(&_admin_req_pending)) || (!btn_wr_cmd_idle()) || (!btn_rd_cmd_idle()) || (!btn_rd_cmd_idle()) 
            || (shr_lock_power_on & (SLC_LOCK_POWER_ON | FQBT_LOCK_POWER_ON | NON_PLP_LOCK_POWER_ON))
            ||!cmd_proc_slot_check()){
			list_add_tail(&req->entry2, &_admin_req_pending);
			evt_set_cs(evt_admin_cmd_check, 0, 0, CS_TASK);
			handle_result = HANDLE_RESULT_PENDING_FE;
		}
		else
		{
			handle_result = nvmet_sanitize(req, cmd);
			if(handle_result == HANDLE_RESULT_FAILURE)
			{
				nvmet_io_fetch_ctrl(false);
			}
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
            if((PLN_GPIOISR_FORMAT_SANITIZE_FLAG == true)&&(PLN_open_flag == true))
            {
                PLN_GPIOISR_FORMAT_SANITIZE_FLAG = false;
                evt_set_cs(evt_pln_format_sanitize,0,0,CS_TASK);
            }
            PLN_FORMAT_SANITIZE_FLAG = false;
#endif
		}
		break;
#endif

#if !defined(PROGRAMMER)
	#ifdef VSC_CUSTOMER_ENABLE	// FET, RelsP2AndGL
	case NVME_OPC_SSSTC_VSC_CUSTOMER:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xfa6b, "VSC F0");
		handle_result = nvmet_ssstc_vsc_f0cmd(req, cmd);
		break;
	#endif

	case NVME_OPC_SSSTC_VSC_NONE:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xc1b0, "SSSTC Vendor Specific Command FC\n");
		if(cmd->cdw12 == 0x2018){
			nvme_apl_trace(LOG_INFO, 0x0108, "NVMe format in VU mdoe cmd nsid = %d CDW10 = 0x%x, CDW12 = 0x%x, CDW13 = 0x%x"
							, cmd->nsid, cmd->cdw10, cmd->cdw12, cmd->cdw13);

			if(cmd->cdw13 == 0 || cmd->cdw13 == 1){
				cmd->opc = NVME_OPC_FORMAT_NVM;
				cmd->nsid = 0xffffffff;
				cmd->cdw10 = cmd->cdw13;

				nvme_apl_trace(LOG_INFO, 0xd2c0, "cmd->cdw10 - 0x%x", cmd->cdw10);
				handle_result = nvmet_asq_handle(req);
			}
			else
				handle_result = HANDLE_RESULT_FAILURE;

		}

		else if(cmd->cdw12 == 0x2019){
			vsc_preformat(req, false);
			handle_result = HANDLE_RESULT_PENDING_BE;

		}
		else if (cmd->cdw12 == 0x2118)//Vsc for Dynamic OP Synology case
		{
			if(cmd->cdw13 == 0)//480-->400
			{
				cmd->opc = NVME_OPC_SET_FEATURES;
				cmd->cdw10 = 0xc1;
				cmd->cdw11 = 0x2E9390B0;
				cmd->cdw12 = 0x0;
				handle_result = nvmet_asq_handle(req);
			}
			else if(cmd->cdw13 == 1)//960-->800
			{
				cmd->opc = NVME_OPC_SET_FEATURES;
				cmd->cdw10 = 0xc1;
				cmd->cdw11 = 0x5D26CEB0;
				cmd->cdw12 = 0x0;
				handle_result = nvmet_asq_handle(req);
			}
			else if(cmd->cdw13 == 2)//1920-->1600
			{
				cmd->opc = NVME_OPC_SET_FEATURES;
				cmd->cdw10 = 0xc1;
				cmd->cdw11 = 0xBA4D4AB0;
				cmd->cdw12 = 0x0;
				handle_result = nvmet_asq_handle(req);
			}
			else
				handle_result = HANDLE_RESULT_FAILURE;
		}
		else
			handle_result = nvmet_ssstc_vsc_fccmd(req, cmd);
		break;
	case NVME_OPC_SSSTC_VSC_WRITE:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xfa77, "VSC FD");
		handle_result = nvmet_ssstc_vsc_fdcmd(req, cmd);
		break;
	case NVME_OPC_SSSTC_VSC_READ:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xfa76, "VSC FE");
		handle_result = nvmet_ssstc_vsc_fecmd(req, cmd);
		break;
#endif
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
		handle_result = HANDLE_RESULT_FAILURE;
	}

	if (handle_result == HANDLE_RESULT_FAILURE)
	{
		struct nvme_status *nvme_status =
			(struct nvme_status *)&req->fe.nvme.nvme_status;
		nvme_apl_trace(LOG_ERR, 0x1bc9, "req(%x) Admin opcode(%x) CE: SCT(%x) SC(%x)", req, cmd->opc, nvme_status->sct, nvme_status->sc);
	}

	return handle_result;
}

/*!
 * @brief admin Command handler
 *
 * Pop nvme command from admin queue, and build request to process it.
 *
 * @param cntlid	controller id.
 *			  0 -- PF0 Admin queue request
 *			  1 -- VF1 Admin queue request
 *			  2 -- VF2 Admin queue request and so on ..
 *
 * @return		not used
 */
fast_code void nvmet_admin_cmd_in(u32 cntlid)
{
	enum cmd_rslt_t result;
	struct nvme_cmd *host_cmd;
	bool refill = true;
#if defined(SRIOV_SUPPORT) || !defined(NVME_SHASTA_MODE_ENABLE)
	while ((ctrlr->free_nvme_cmds > 0) && (req_cnt > 0) && (dtag_get_admin_avail(DTAG_T_SRAM)) &&
		   (!dwnld_com_lock) && (host_cmd = hal_nvmet_get_admin_sq_cmd(cntlid, refill)))
	{
#else
	while ((ctrlr->free_nvme_cmds > 0) && (req_cnt > 0) && (dtag_get_admin_avail(DTAG_T_SRAM)) &&
		   (!dwnld_com_lock) && (host_cmd = hal_nvmet_get_sq_cmd(0, refill)))
	{
#endif
		/* set it true in evt_task */
		//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
		//Rdcmd++;
		//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
		//nvme_apl_trace(LOG_INFO, 0, "admin in blink %d",
		//			Rdcmd);
		req_t *req = nvmet_get_req();

		sys_assert(req != NULL);

		ctrlr->admin_running_cmds++;
		nvme_apl_trace(LOG_DEBUG, 0xf197, "admin cmd in %d",
					   ctrlr->admin_running_cmds);
		INIT_LIST_HEAD(&req->entry);
		req->req_from = REQ_Q_ADMIN;
		req->host_cmd = host_cmd;

#if defined(SRIOV_SUPPORT)
		/* sqid must be flexible sqid index (0 to 65). */
		req->fe.nvme.sqid = (cntlid == 0) ? 0 : SRIOV_FLEX_PF_ADM_IO_Q_TOTAL + (cntlid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
#else
		req->fe.nvme.sqid = 0;
#endif
		req->fe.nvme.cntlid = (u8)cntlid;
		req->fe.nvme.cid = host_cmd->cid;
		req->fe.nvme.cmd_spec = 0;
		req->fe.nvme.nvme_status = 0;

		req->error = 0;
		req->completion = nvmet_core_cmd_done;
		req->state = REQ_ST_FE_ADMIN;
		result = nvmet_asq_handle(req);
//		nvme_apl_trace(LOG_DEBUG, 0, "cntlid(%d) req(%x) rslt(%d)",
//					   cntlid, req, result);

		if ((result == HANDLE_RESULT_FINISHED) ||
			(result == HANDLE_RESULT_FAILURE))
		{
			req->completion(req);
		}
		else
		{
			nvme_apl_trace(LOG_DEBUG, 0x4f55, "req(%x) rslt(%d)",
						   req, result);
		}
	}
}

/*!
 * @brief admin asynchronous event request command handler
 *
 * @param unused	not used
 * @param payload	should be request pointer
 * @param sts		not used
 *
 * @return		not used
 */
static ddr_code void nvmet_admin_aer_out(u32 unused, u32 param, u32 sts)
{
	req_t *req = (req_t *)param;
	enum cmd_rslt_t result = nvmet_async_event_request(req, NULL);

	if (result == HANDLE_RESULT_FINISHED)
		req->completion(req);
	else
		nvme_apl_trace(LOG_ERR, 0xb01c, "Req (%x) AER masked", req);
}

/*!
 * @brief admin abort request command done
 *
 * @param unused	not used
 * @param cdw10		cdw10 of abort command
 * @param aborted	1 for aborted
 *
 * @return		not used
 */
static void nvmet_admin_abort_done(u32 unused, u32 cdw10, u32 cmpl_sts)
{
	cmd_abort_cmpl_t csts;
	req_t *abort_req;
	struct list_head *cur;
	struct list_head *saved;
	struct nvmet_sq *sq = ctrlr->sqs[0];

	csts.all = cmpl_sts;
	list_for_each_safe(cur, saved, &sq->reqs)
	{
		abort_req = container_of(cur, req_t, entry);
		struct nvme_cmd *abort_cmd = abort_req->host_cmd;
		u32 cmd_cdw10 = abort_cmd->cdw10;

		if ((abort_cmd->opc == NVME_OPC_ABORT) && (cmd_cdw10 == cdw10))
		{
			if (csts.b.cmd_aborted)
			{
				nvme_apl_trace(LOG_ALW, 0xfa5e, "cmd aborted: sq(%d) cid(0x%x) cmpl sts(0x%x)",
							   csts.b.sq_id, csts.b.cmd_id, csts.all);
			}
			else if (csts.b.cmd_started)
			{
				nvme_apl_trace(LOG_ALW, 0xf731, "cmd started: sq(%d) cid(0x%x) cmpl sts(0x%x)",
							   csts.b.sq_id, csts.b.cmd_id, csts.all);
			}
			else if (csts.b.cmd_in_btn)
			{
				nvme_apl_trace(LOG_ALW, 0xc392, "cmd in btn: sq(%d) cid(0x%x) cmpl sts(0x%x)",
							   csts.b.sq_id, csts.b.cmd_id, csts.all);
			}
			else if (csts.b.cmd_not_found)
			{
				abort_req->fe.nvme.cmd_spec = 1;
				nvme_apl_trace(LOG_ALW, 0xdc7a, "cmd not found: sq(%d) cid(0x%x) cmpl sts(0x%x)", csts.b.sq_id, csts.b.cmd_id, csts.all);
				bool flag = nvmet_abort_aer_flow(csts.b.cmd_id);
				if (flag == true)
				{
					abort_req->fe.nvme.cmd_spec = 0;
					nvme_apl_trace(LOG_ALW, 0xe938, "cmd found in pending aer");
				}
			}
			else if (csts.b.cmd_aborted == 0)
			{
				abort_req->fe.nvme.cmd_spec = 1;
				nvme_apl_trace(LOG_ERR, 0xe823, "Can't abort cmd: sq(%d) cid(%d) cmpl sts(0x%x)",
							   cdw10 & 0xFFFF, cdw10 >> 16, csts.all);
			}
			ctrlr->currAbortCnt--;
			abort_req->completion(abort_req);
			list_del(&abort_req->entry);
			return;
		}
	}

	/* CMD_PROC single cmd abort - FW abort (not using admin abort path).
	 * Use the CMD_PROC HW support to do abort single cmd.
	 * Used abort_q and abort_cmpl_q */
	nvme_apl_trace(LOG_ERR, 0x7e3c, "abort req miss: cdw10(0x%x), abrt cmpl sts(0x%x)", cdw10, cmpl_sts);
}
slow_code static void nvmet_admin_cmd_check(u32 unused, u32 r0, u32 r1)
{
    if (list_empty(&_admin_req_pending))
		return;
#if (_TCG_)
	//if(isTcgIfReady() == FALSE){
	/*
	if(isTcgNfBusy){
		nvme_apl_trace(LOG_WARNING, 0, "[TCG] Warnning!!, TCG Nf is STILL not ready");
		evt_set_cs(evt_admin_cmd_check, 0, 0, CS_TASK);
		return;
	}
	*/
#endif

    if (btn_wr_cmd_idle() && btn_rd_cmd_idle()){
        enum cmd_rslt_t result;
        LIST_HEAD(local);
        list_splice(&_admin_req_pending, &local);
        INIT_LIST_HEAD(&_admin_req_pending);
        do
        {
            req_t *req = list_first_entry(&local, req_t, entry2);

            list_del(&req->entry2);
            result = nvmet_asq_handle(req);
            if ((result == HANDLE_RESULT_FINISHED) ||(result == HANDLE_RESULT_FAILURE))
		    {
			    req->completion(req);
		    }
        } while (!list_empty(&local));
        if (list_empty(&_admin_req_pending)){
    		nvmet_io_fetch_ctrl(false); // enable io fetch
        }
    }
	else
		evt_set_cs(evt_admin_cmd_check, 0, 0, CS_TASK);
}

ddr_code static void nvmet_admin_format_post_chk(u32 unused, u32 r0, u32 r1)
{
    //MEMO Current flow only consider one namespace
#if (TRIM_SUPPORT == ENABLE)
    if ((TrimInfo.FullTrimTriggered == 0) && (gFormatInProgress))
#else
	if (gFormatInProgress)
#endif
	{
        gFormatInProgress = false;
        format_post_setup();
        hal_pi_enable_cmd_fetch();
        btn_de_wr_enable();
    }
    else
    {
        evt_set_cs(evt_admin_format_confirm, 0, 0, CS_TASK);
    }
}

#ifdef POWER_APST

slow_code void apst_transfer_to_last_ps(void) //joe slow->ddr 20201124
{
	/* if apst is enable(identify-byte265) and current ps is ps3 or ps4,  */
	if (ctrlr->cur_feat.ps_feat.b.ps > 2 && evt_change_ps != 0xFF)
	{
		ctrlr->cur_feat.ps_feat.b.ps = ctrlr->last_ps;
		evt_set_cs(evt_change_ps, (u32)ctrlr->last_ps, 0, CS_TASK);
	}
}

/*!
 * @brief nvme check autonomous power state transition timer handle
 *
 * @param data		timer name
 *
 * @return 		None
 */
slow_code void apst_timer_handler(void *data)
{
	u32 current_ps = ctrlr->cur_feat.ps_feat.b.ps;
	ps_feat_t ps_feat;
	u32 threshold;

	sys_assert(current_ps <= IDTFY_NPSS - 1);

	/* if apst is enbale(identify-byte265), and current ps is PS0/PS1/PS2/PS3 */
	if (ctrlr->cur_feat.apst_feat.apst.b.apst_en && (current_ps <= 3))
	{
		if (nvmet_cmd_pending() != 0)
		{
			apst_period = 0;
		}
		else
		{
			apst_period++;

			/* -3: transition to next ps, 300ms ahead of itpt to avoid timer not accurate */
			if (ctrlr->cur_feat.apst_feat.apst_de[current_ps].itpt < 1000)
				threshold = ctrlr->cur_feat.apst_feat.apst_de[current_ps].itpt / 100;
			else
				threshold = ctrlr->cur_feat.apst_feat.apst_de[current_ps].itpt / 100 - 3;

			/* if the idle time exceed the apst_de[0].itpt, enter into itps */
			if (apst_period >= threshold)
			{

				/* record the last operational power state */
				if (ctrlr->cur_feat.ps_feat.b.ps <= 2)
					ctrlr->last_ps = ctrlr->cur_feat.ps_feat.b.ps;

				ps_feat = ctrlr->cur_feat.ps_feat;
				ps_feat.b.ps = ctrlr->cur_feat.apst_feat.apst_de[current_ps].itps;

				if (ctrlr->cur_feat.ps_feat.b.ps != ps_feat.b.ps && evt_change_ps != 0xFF)
				{
					evt_set_cs(evt_change_ps, (u32)ps_feat.b.ps, 0, CS_TASK);
				}
				ctrlr->cur_feat.ps_feat = ps_feat;

				apst_period = 0;
			}
		}
	}

	if (ctrlr->cur_feat.apst_feat.apst.b.apst_en == 0)
		apst_period = 0;

	mod_timer(&apst_timer, jiffies + HZ/10);
}
#endif

#if CO_SUPPORT_SANITIZE
// Jack Li, for Sanitize

ddr_code enum cmd_rslt_t sanitize_admin_check(req_t *req, struct nvme_cmd *cmd, bool sanitize_allow)
{
    u8 lid = cmd->cdw10;
	nvme_apl_trace(LOG_ERR, 0x9d17, "opc:0x%x cdw10:0x%x", cmd->opc, cmd->cdw10);
	if (cmd->opc <= NVME_OPC_ASYNC_EVENT_REQUEST || cmd->opc >= NVME_OPC_SSSTC_VSC_CUSTOMER)
    {
        if ((cmd->opc == NVME_OPC_GET_LOG_PAGE) && (lid != NVME_LOG_ERROR && lid != NVME_LOG_HEALTH_INFORMATION && lid != NVME_LOG_SANITIZE_STATUS))
        {
            //shall return error
        }
        else
        {
            return HANDLE_RESULT_RUNNING;
        }
    }
	else if (sanitize_allow && (cmd->opc == NVME_OPC_SANITIZE))
		return HANDLE_RESULT_RUNNING;

    nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_IN_PROGRESS);
    return HANDLE_RESULT_FAILURE;
}

ddr_code bool Sanitize_format_done(req_t *req)
{
    //printk("SANITIZE format done\n");
    nvme_apl_trace(LOG_ERR, 0xf666, "Enter function format done, time cost: %d ms", time_elapsed_in_ms(sanitize_start_timestamp));
    epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_Completed;    //for trigger sanitize process
    epm_update(SMART_sign, (CPU_ID - 1));
    nvmet_put_req(req);

	// Complete handle
	evt_set_cs(evt_admin_sanitize_operation, 0, 0, CS_TASK);

	return true;
}

/*fast_code bool Sanitize_complete(req_t *req)
{
    //printk("SANITIZE complete done\n");
    HWCBackup = smart_stat.host_write_commands;
    GlobalDataErasedUpdate = 1;  // need update
    smSanitizeProcessStates = sSanitizeFWNone;    // sanitize process done

    SetDeviceEvent.d.Ev_Sanitize = 0;
    SetDeviceEvent.d.Ev_Formating = 0;
    nvmet_put_req(req);

    cmd_disable_btn(SetDeviceEvent);
    cmd_proc_enable_fetch_IO(1, 1);

    #if 1//(CO_SUPPORT_PERSISTENT_EVENT_LOG == TRUE)
    tSanitizeComp *SanitizeCompInfo;
    MEM_CLR(PEinfoBuf, PEinfoBufSZ);
    SanitizeCompInfo = (tSanitizeComp *)PEinfoBuf;

    SanitizeCompInfo->SanitizeProgress = smSysInfo->d.LogInfo.d.SanitizeLog.SPROG;
    SSTAT_t SSTAT;
    SSTAT.d.SanitizeStatus = smSysInfo->d.LogInfo.d.SanitizeLog.SanitizeStatus;
    SSTAT.d.OverwritePassNunber = smSysInfo->d.LogInfo.d.SanitizeLog.OverwritePassNunber;
    SSTAT.d.GlobalDataErased = smSysInfo->d.LogInfo.d.SanitizeLog.GlobalDataErased;
    SSTAT.d.Reserved = 0;
    SanitizeCompInfo->SanitizeStatus = SSTAT.all;
    SanitizeCompInfo->CompletionInfo = 0;

    Register_Persistent_Event(Idx_SanitizeComp, (U8*)SanitizeCompInfo);
    #endif

    if (smSysInfo->d.LogInfo.d.SanitizeLog.SanitizeStatus == sSanitizeCompletedNDI)
    {
        nvmet_evt_aer_in((NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS | (NOTICE_STS_SANITIZE_OPERATION_COMPLETED_WITH_UN_DA << 8)), 0);
    }
    else
    {
        nvmet_evt_aer_in((NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS | (NOTICE_STS_SANITIZE_OPERATION_COMPLETED << 8)), 0);
    }
    return true;
}*/

ddr_code bool Sanitize_Start(req_t *req)
{
	nvme_apl_trace(LOG_ERR, 0xf36e, "Enter function Sanitize Start");
	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_Start;    //for trigger sanitize process
	epm_smart_data->sanitizeInfo.bmp_w_cmd_sanitize = 0;
	//epm_smart_data->sanitizeInfo.sanitize_Tag = 0xDEADFACE;
	epm_update(SMART_sign, (CPU_ID - 1));

	while((!btn_wr_cmd_idle()) || (!btn_rd_cmd_idle())){}
	cmd_disable_btn(1, 1);
	//mdelay(100);
	//nvmet_io_fetch_ctrl(false);

	evt_set_cs(evt_admin_sanitize_operation, 0, 0, CS_TASK);

	nvme_apl_trace(LOG_ERR, 0x0325, "Leave function Sanitize Start");
    return nvmet_core_cmd_done(req);
}

ddr_code void Sanitize_Operation(u32 unused, u32 r0, u32 r1)
{
	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	sanitize_log_page_t sanitize_lp = {.all = (epm_smart_data->sanitizeInfo.sanitize_log_page)};
	nvme_apl_trace(LOG_ERR, 0xbe03, "Enter function, fwStat:%d, lpStat:%d", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates, sanitize_lp.b.SanitizeStatus);
	if (epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_Start)
    {
        // Sanitize process start" //
        sanitize_start_timestamp = get_tsc_64();
        sanitize_not_break = 1;
        epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_InProgress;
        sanitize_lp.b.SPROG = 0x1;
		epm_smart_data->sanitizeInfo.sanitize_log_page = sanitize_lp.all;
		epm_update(SMART_sign, (CPU_ID - 1));

		nvmet_io_fetch_ctrl(false);

		//nvmet_format_ns(req, (struct nvme_cmd *) req->host_cmd);
		if(sanitize_lp.b.DW10.b.SANACT == cStart_Crypto_Erase)
		{
			nvme_apl_trace(LOG_INFO, 0xd9c7, "SANACT: %d, TODO: ChangeKey + FullTrim", sanitize_lp.b.DW10.b.SANACT);

#if defined(USE_CRYPTO_HW)
		// Change Key //
			u64 time_chg_key = get_tsc_64();
	#if (_TCG_)
			extern bool bKeyChanged;
			extern void TcgChangeKey(u8 idx);
			extern void tcg_init_aes_key_range(void);
			extern u8 tcg_nf_G3Wr(bool G5_only);
		#ifdef NS_MANAGE
			for(u8 i=0; i<ns_array_menu->total_order_now; i++)
			{
				u8 ns_id = ns_array_menu->array_order[i] + 1;
				TcgChangeKey(LOCKING_RANGE_CNT + ns_id);
			}
		#else
			TcgChangeKey(0);
		#endif  // ifdef NS_MANAGE
			tcg_init_aes_key_range();
			tcg_nf_G3Wr(false);
			bKeyChanged = false;
	#else
			extern void crypto_change_mode_range(u8 crypto_type, u8 NS_ID, u8 change_key, u8 cryptoID);
		#ifdef NS_MANAGE
			for(u8 i=0; i<ns_array_menu->total_order_now; i++)
			{
				u8 ns_id = ns_array_menu->array_order[i] + 1;
				crypto_change_mode_range(3, ns_id, 1, ns_id);
			}
		#else
			crypto_change_mode_range(3, 1, 1, 1);
		#endif  // ifdef NS_MANAGE
	#endif  // if (_TCG_)
			nvme_apl_trace(LOG_INFO, 0x33ce, "Change Key spends %d ms", time_elapsed_in_ms(time_chg_key));
#endif

			cmd_proc_rx_cmd();
			nvmet_admin_cmd_in(0);

	#if (TRIM_SUPPORT == ENABLE)
		// Full Trim //
			u64 time_trim_flush = get_tsc_64();
			TrimFlush();
			shr_format_fulltrim_flag = true;
			nvme_apl_trace(LOG_INFO, 0xb08c, "Trim Flush spends %d ms", time_elapsed_in_ms(time_trim_flush));

			u64 time_stop_gc = get_tsc_64();
			if(nvme_format_stop_gc_wait())
			{
				return; //power loss return
			}
			nvme_apl_trace(LOG_INFO, 0xef38, "GC stop spends %d ms", time_elapsed_in_ms(time_stop_gc));

			u64 time_trim_handle = get_tsc_64();
			if(nvme_format_trim_handle_wait())
			{
				return; //power loss return
			}
			nvme_apl_trace(LOG_INFO, 0x2bb7, "Trim Handle spends %d ms", time_elapsed_in_ms(time_trim_handle));

			cmd_proc_rx_cmd();
			nvmet_admin_cmd_in(0);

			if(!TrimInfo.FullTrimTriggered)
	#endif
			{
				epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_Completed;
				epm_update(SMART_sign, (CPU_ID - 1));
			}

			// Complete handle
			evt_set_cs(evt_admin_sanitize_operation, 0, 0, CS_TASK);
		}
		else if(sanitize_lp.b.DW10.b.SANACT <= cStart_Block_Erase) // including Block Erase and Exit Failure Mode
		{
			nvme_apl_trace(LOG_INFO, 0x25de, "[Sanitize] SANACT: %d, TODO: Preformat", sanitize_lp.b.DW10.b.SANACT);
			req_t *req = nvmet_get_req();
			req->nsid = 1;
			req->host_cmd = NULL;
			req->completion = Sanitize_format_done;
			vsc_preformat(req, false);
		}
		/*else if(sanitize_lp.b.DW10.b.SANACT == cExit_Failure_Mode)
		{
			nvme_apl_trace(LOG_INFO, 0, "[Sanitize] SANACT: %d, TODO: Nothing", sanitize_lp.b.DW10.b.SANACT);
			epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_Completed;
			epm_update(SMART_sign, (CPU_ID - 1));
			//nvmet_io_fetch_ctrl(true);
		}*/
	}
    else if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_Completed)
    {
        // "Sanitize process complete" //

		// stop to fetch cmd
		nvmet_io_fetch_ctrl(true);

		// return all rx cmd
		cmd_proc_rx_cmd();

		// update Wr cmd cnt
		extern smart_statistics_t *smart_stat;
		btn_smart_io_t wio;
		btn_get_w_smart_io(&wio);
		smart_stat->data_units_written += wio.host_du_cnt;
		smart_stat->host_write_commands += wio.cmd_recv_cnt;
		epm_smart_data->sanitizeInfo.handled_wr_cmd_cnt = smart_stat->host_write_commands;

		// return all admin cmd
		nvmet_admin_cmd_in(0);

		cmd_disable_btn(0, 0);

		// Round Up (sec)
		// epm_smart_data->sanitizeInfo.last_time_cost = (u64)occupied_by(time_elapsed_in_ms(sanitize_start_timestamp),1000);
		// Record in ms
		epm_smart_data->sanitizeInfo.last_time_cost = time_elapsed_in_ms(sanitize_start_timestamp);

        sanitize_lp.b.SPROG = 0xFFFF;
        if (sanitize_lp.b.DW10.b.NDAS)
        {
            sanitize_lp.b.SanitizeStatus = sSanitizeCompletedNDI;
        }
        else
        {
            sanitize_lp.b.SanitizeStatus = sSanitizeCompleted;
        }
		epm_smart_data->sanitizeInfo.sanitize_log_page = sanitize_lp.all;

		if (sanitize_lp.b.SanitizeStatus == sSanitizeCompletedNDI)
		{
			nvmet_evt_aer_in(((NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS << 16) | (NOTICE_STS_SANITIZE_OPERATION_COMPLETED_WITH_UN_DA)), 0);
		}
		else
		{
			nvmet_evt_aer_in(((NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS << 16) | (NOTICE_STS_SANITIZE_OPERATION_COMPLETED)), 0);
		}

		// abort all remained IO cmd sorted to cmd_proc
		sntz_chk_cmd_proc = 1;
		extern void cmd_proc_isr(void);
		while(sntz_chk_cmd_proc)
		{
			cmd_proc_isr();
		}

		// resume to fetch cmd
		nvmet_io_fetch_ctrl(false);

		// update Sanitize status last
		nvme_apl_trace(LOG_INFO, 0xaa95, "[Sanitize] FW complete already !! cost %d ms", time_elapsed_in_ms(sanitize_start_timestamp));
		epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_None;
		epm_update(SMART_sign, (CPU_ID - 1));
     }
	else if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_InProgress)
     {
     	if(!sanitize_not_break)
		{
			nvme_apl_trace(LOG_ERR, 0xc0e4, "Sanitize operation need resume!");
			epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_Start;
        	epm_update(SMART_sign, (CPU_ID - 1));

			nvmet_io_fetch_ctrl(true);
			while((!btn_wr_cmd_idle()) || (!btn_rd_cmd_idle())){}
			cmd_disable_btn(1, 1);
			//mdelay(100);
			//nvmet_io_fetch_ctrl(false);
			evt_set_cs(evt_admin_sanitize_operation, 0, 0, CS_TASK);
			return;
		}
	#if 0
		else
		{
			u64 timeProgress = (time_elapsed_in_ms(sanitize_start_timestamp)/1000)*0x1000;
			if(timeProgress > 0xFFFF)
				sanitize_lp.b.SPROG = 0xF000;
			else
				sanitize_lp.b.SPROG = (u16)timeProgress;
			nvme_apl_trace(LOG_INFO, 0x4102, "SPROG in Log Page: 0x%x", sanitize_lp.b.SPROG);
			epm_smart_data->sanitizeInfo.sanitize_log_page = sanitize_lp.all;
		}
	#endif
	}
}

ddr_code enum cmd_rslt_t nvmet_sanitize(req_t *req, struct nvme_cmd *cmd)
{
/*
#if (_TCG_ != TCG_NONE)
    if (mTcgStatus & TCG_ACTIVATED) { // SIIS v1.07 5.5.3, return Sanitize not supported if TCG activated
        nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
        return HANDLE_RESULT_FAILURE;
    }
#endif
*/
	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	sanitize_log_page_t sanitize_lp = {.all = (epm_smart_data->sanitizeInfo.sanitize_log_page)};

	if(cmd->nsid)
    {
        nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
        return HANDLE_RESULT_FAILURE;
    }

	sanitize_dw10_t dw10 = {.all = cmd->cdw10};
	switch(dw10.b.SANACT)  // Sanitize Action Code
    {
        case cExit_Failure_Mode:
            if (sanitize_lp.b.SanitizeStatus != sSanitizeCompletedFail)
            {
				//nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_INVALID_FIELD);
				sanitize_lp.b.SPROG = 0xFFFF;
				sanitize_lp.b.SanitizeStatus = sSanitizeCompleted;
				sanitize_lp.b.OverwritePassNunber = 0;
				sanitize_lp.b.DW10.all = dw10.all;					  //for sanitize status log byte 07:04 : SCDW10
				epm_smart_data->sanitizeInfo.sanitize_log_page = sanitize_lp.all;
				extern smart_statistics_t *smart_stat;
				btn_smart_io_t wio;
				btn_get_w_smart_io(&wio);
				smart_stat->data_units_written += wio.host_du_cnt;
				smart_stat->host_write_commands += wio.cmd_recv_cnt;
				epm_smart_data->sanitizeInfo.handled_wr_cmd_cnt = smart_stat->host_write_commands;
				epm_smart_data->sanitizeInfo.last_time_cost = 0;
				//epm_smart_data->sanitizeInfo.sanitize_Tag = 0xDEADFACE;
				epm_update(SMART_sign, (CPU_ID - 1));
				nvmet_io_fetch_ctrl(false);

				return HANDLE_RESULT_FINISHED;
            }
			else
			{
				if (sanitize_lp.b.DW10.b.AUSE == mFALSE) // Spec v1.4: Fail with Restricted Mode is not allow Exit Failure Mode
				{
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_FAILED);
                	return HANDLE_RESULT_FAILURE;
				}
			}
//      #if (CO_SUPPORT_AES == TRUE) //&& (_TCG_!=TCG_PYRITE)  //FDE, OPAL & EDRV
	#if defined(USE_CRYPTO_HW)
		case cStart_Crypto_Erase:
    #endif
        case cStart_Block_Erase:
            if (sanitize_lp.b.SanitizeStatus == sSanitizeCompletedFail &&
                sanitize_lp.b.DW10.b.AUSE == mFALSE &&
                dw10.b.AUSE == mTRUE)     //spec 5.24, restricted sanitize fail, only accept restricted sanitize command again
            {
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_FAILED);
                return HANDLE_RESULT_FAILURE;
            }

            if (dw10.b.NDAS == mTRUE && (ctrlr->cur_feat.sanitize_feat.b.NODRM == 0))
            {
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_INVALID_FIELD);
                return HANDLE_RESULT_FAILURE;
            }

            // spec, If the Firmware Commit command that established the pending firmware activation with reset condition completed successfully
            //then the controller should abort the Sanitize command with a status code of Firmware Activation Requires Controller Level Reset.
            if (misc_is_fw_wait_reset())
            {
                nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC,NVME_SC_FIRMWARE_REQ_RESET);
                return HANDLE_RESULT_FAILURE;
            }

            #if defined(USE_CRYPTO_HW)
            if(dw10.b.SANACT == cStart_Crypto_Erase)
			{
                // change key function //
                //return HANDLE_RESULT_PENDING_BE;
            }
            #endif

            #if 0//For Persistent Event
            tSanitizeSt *SanitizeStInfo;
            MEM_CLR(PEinfoBuf, PEinfoBufSZ);
            SanitizeStInfo = (tSanitizeSt *)PEinfoBuf;

            SanitizeStInfo->SANICAP = ((SANICAP_NODMMAS << 30) | (SANICAP_NDI << 29) | (SANICAP_OWS << 2) | (SANICAP_BES << 1) | SANICAP_CES);
            SanitizeStInfo->SanitizeCDW10 = pCdb->CDW10.all;
            SanitizeStInfo->SanitizeCDW11 = pCdb->OVRPAT;

            Register_Persistent_Event(Idx_SanitizeSt, (U8*)SanitizeStInfo);
            #endif

            sanitize_lp.b.SPROG = 0;
            sanitize_lp.b.SanitizeStatus = sSanitizeInProgress;
            sanitize_lp.b.OverwritePassNunber = 0;
            sanitize_lp.b.DW10.all = dw10.all;                    //for sanitize status log byte 07:04 : SCDW10
			epm_smart_data->sanitizeInfo.sanitize_log_page = sanitize_lp.all;
			//epm_update(SMART_sign, (CPU_ID - 1));

            Sanitize_Start(req);

			#if CO_SUPPORT_DEVICE_SELF_TEST//eric 24040205
			//extern tDST_LOG *smDSTInfo;
			//if (cmd->nsid == smDSTInfo->DSTResult.NSID || cmd->nsid==0xFFFFFFFF) //Eric 20230707 for DST spec
			DST_Completion(cDSTAbortSanitize);
			#endif

            return HANDLE_RESULT_PENDING_BE;

        case cStart_Overwrite:
        default:
            nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_INVALID_FIELD);
            return HANDLE_RESULT_FAILURE;

    }
}
#endif


#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
ddr_code static void telemetry_req_check(u32 unused, u32 r0, u32 r1)
{
	if (telemetry_pending_task)
	{
		telemetry_pending_task = 0;
		telemetry_ctrlr_data_update_focus();
		return;
	}

	if(list_empty(&telemetry_req_pending)) {
		telemetry_transferring = false;
		return;
	}

	//if controller-initated update log event occur between two telemetry getlog cmd, need to check dtag count.
	if (!dtag_get_admin_avail(DTAG_T_SRAM))
	{
		evt_set_cs(evt_telemetry_req_check, 0, 0, CS_TASK);
		return;
	}

	enum cmd_rslt_t handle_result;

	req_t *req = list_first_entry(&telemetry_req_pending, req_t, entry2);
	list_del(&req->entry2);

	telemetry_transferring = false;
	handle_result = nvmet_asq_handle(req);
	if ((handle_result == HANDLE_RESULT_FINISHED)||(handle_result == HANDLE_RESULT_FAILURE))
	{
		req->completion(req);
	}
}
#endif


init_code void nvmet_admin_cmd_init(void)
{
#ifdef POWER_APST
	apst_period = 0;
	apst_timer.function = apst_timer_handler;
	apst_timer.data = "apst timer";
	mod_timer(&apst_timer, jiffies + HZ);
#endif
#ifdef AER_Polling
	//add for aer to polling smart
	AER_timer.function = AER_Polling_SMART_Critical_Warning_bit;
	AER_timer.data = "AER timer";
	mod_timer(&AER_timer, jiffies + 3*HZ);
#endif
#if !defined(PROGRAMMER)
	u32 Mapneed = occupied_by(sizeof(struct NormalTest_Map), DTAG_SZE);
	u32 Map_start = ddr_dtag_register(Mapneed);
	NormalTest = (NormalTest_Map *)ddtag2mem(Map_start);
	memset(NormalTest, 2, sizeof(struct NormalTest_Map));
	Fake_table_init();
#endif
	evt_register(nvmet_admin_aer_out, 0, &evt_aer_out);
	evt_register(nvmet_admin_abort_done, 0, &evt_abort_cmd_done);
    evt_register(nvmet_admin_cmd_check,0,&evt_admin_cmd_check);
	evt_register(nvmet_admin_format_post_chk,0,&evt_admin_format_confirm);
#if CO_SUPPORT_SANITIZE // Jack Li, for Sanitize
	evt_register(Sanitize_Operation,0,&evt_admin_sanitize_operation);
#endif

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	evt_register(telemetry_req_check,0,&evt_telemetry_req_check);
	evt_register(telemetry_xfer_continue,0,&evt_telemetry_xfer_continue);
	cpu_msg_register(CPU_MSG_GET_TELEMETRY_DATA_DONE, telemetry_task_receiver);
	cpu_msg_register(CPU_MSG_REQUIRE_UPDATE_TELEMETRY_CTRLR_DATA, telemetry_ctrlr_data_update);

	//host initated
	u32 telemetry_ddr_dtag = ddr_dtag_telemetry_register(occupied_by(sizeof(struct nvme_telemetry_host_initated_log), DTAG_SZE));
	telemetry_host_data = (struct nvme_telemetry_host_initated_log *)ddtag2mem( telemetry_ddr_dtag );
	memset(telemetry_host_data, 0, sizeof(*telemetry_host_data));

	telemetry_host_data->lid = NVME_LOG_TELEMETRY_HOST_INIT;
	telemetry_host_data->IEEE[0] = IeeeL;
	telemetry_host_data->IEEE[1] = IeeeM;
	telemetry_host_data->IEEE[2] = IeeeU;
	telemetry_host_data->last_blk1 = sizeof(*telemetry_host_data) / TELEMETRY_SECTOR_SIZE - 1;//header used 1 sector.
	telemetry_host_data->last_blk2 = sizeof(*telemetry_host_data) / TELEMETRY_SECTOR_SIZE - 1;
	telemetry_host_data->last_blk3 = sizeof(*telemetry_host_data) / TELEMETRY_SECTOR_SIZE - 1;

	//ctrlr initated
	telemetry_ddr_dtag = ddr_dtag_telemetry_register(occupied_by(sizeof(struct nvme_telemetry_ctrlr_initated_log),DTAG_SZE));
	telemetry_ctrlr_data = (struct nvme_telemetry_ctrlr_initated_log *)ddtag2mem(telemetry_ddr_dtag);
	memset(telemetry_ctrlr_data, 0, sizeof(*telemetry_ctrlr_data));

	telemetry_ctrlr_data->lid = NVME_LOG_TELEMETRY_CTRLR_INIT;
	telemetry_ctrlr_data->IEEE[0] = IeeeL;
	telemetry_ctrlr_data->IEEE[1] = IeeeM;
	telemetry_ctrlr_data->IEEE[2] = IeeeU;
	telemetry_ctrlr_data->last_blk1 = sizeof(*telemetry_ctrlr_data) / TELEMETRY_SECTOR_SIZE - 1;//header used 1 sector.
	telemetry_ctrlr_data->last_blk2 = sizeof(*telemetry_ctrlr_data) / TELEMETRY_SECTOR_SIZE - 1;
	telemetry_ctrlr_data->last_blk3 = sizeof(*telemetry_ctrlr_data) / TELEMETRY_SECTOR_SIZE - 1;
	telemetry_ctrlr_data->available = epm_telemetry_data->telemetry_ctrlr_available;
	telemetry_ctrlr_data->gen_num = epm_telemetry_data->telemetry_ctrlr_gen_num;
#endif

	INIT_LIST_HEAD(&sq_del_timer.entry);
	sq_del_timer.function = nvme_delete_sq_post_handle;
	sq_del_timer.data = "sq delete";
}

#if CO_SUPPORT_PANIC_DEGRADED_MODE
static ddr_code enum cmd_rslt_t nvmet_assert_asq_handle(req_t *req)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;
	struct nvme_cmd *cmd = req->host_cmd;
	//nvme_apl_trace(LOG_DEBUG, 0, "req(%x) Admin opcode(%x): %s", req, cmd->opc,
	//			   nvmet_admin_opc_sa_name(cmd->opc));

#if (_TCG_)
	if(mTcgStatus & TCG_ACTIVATED)
		if(nvmet_adm_cmd_tcg_chk(req))
			return HANDLE_RESULT_FAILURE;
#endif

#if CO_SUPPORT_SANITIZE
	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	sanitize_status_t sntz_sts = (epm_smart_data->sanitizeInfo.sanitize_log_page & 0x70000)>>16;
	if(epm_smart_data->sanitizeInfo.sanitize_Tag == 0xDEADFACE)
	{
		if ((epm_smart_data->sanitizeInfo.fwSanitizeProcessStates != sSanitize_FW_None) || (sntz_sts == sSanitizeCompletedFail))
		{
			nvme_apl_trace(LOG_ERR, 0x0fd1, "Sanitize Sts(inFW): 0x%x, (inSSTAT): 0x%x", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates, sntz_sts);
			bool sanitize_allow = (epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_None);
			if (sanitize_admin_check(req, cmd, sanitize_allow) == HANDLE_RESULT_FAILURE)
			{
				if(sanitize_allow)
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_FAILED);
				else
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_IN_PROGRESS);
				return HANDLE_RESULT_FAILURE;
			}
		}
	}
	else
	{
		epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = sSanitize_FW_None;
		//epm_smart_data->sanitizeInfo.sanitize_log_page &= ~(0xFF0000);
		//epm_smart_data->sanitizeInfo.sanitize_log_page |= (0xFFFF);
		epm_smart_data->sanitizeInfo.sanitize_log_page = 0xFFFF;
		epm_smart_data->sanitizeInfo.handled_wr_cmd_cnt = 0;
		epm_smart_data->sanitizeInfo.bmp_w_cmd_sanitize = 0;
		epm_smart_data->sanitizeInfo.sanitize_Tag = 0xDEADFACE;
		epm_update(SMART_sign, (CPU_ID - 1));
	}
#endif

	//nvme_apl_trace(LOG_ERR, 0, "admin cmd in\n");
	if((cmd->fuse != 0) || (cmd->psdt != 0) || (cmd->rsvd1 != 0))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		nvme_apl_trace(LOG_DEBUG, 0x00e4, "fused:%x psdt:%x rsvd1:%x",cmd->fuse, cmd->psdt, cmd->rsvd1);//PC BASHER FULL RANDOM
		return HANDLE_RESULT_FAILURE;
	}

	switch (cmd->opc)
	{
	case NVME_OPC_DELETE_IO_SQ:
		handle_result = nvmet_delete_io_sq(req, cmd);
		break;
	case NVME_OPC_CREATE_IO_SQ:
		handle_result = nvmet_create_io_sq(req, cmd);
		break;
	case NVME_OPC_GET_LOG_PAGE:
//        nvme_apl_trace(LOG_ERR, 0, "nvme_vsc_ev_log cdw10:(%d) cdw11:(%d) cdw12:(%d) cdw13:(%d) cdw14:(%d) cdw15:(%d)", cmd->cdw10, cmd->cdw11, cmd->cdw12, cmd->cdw13, cmd->cdw14, cmd->cdw15);
		handle_result = nvmet_get_log_page(req, cmd);
        //nvme_apl_trace(LOG_ALW, 0, "handle result:0x%x",handle_result);
		break;
	case NVME_OPC_DELETE_IO_CQ:
		handle_result = nvmet_delete_io_cq(req, cmd);
		break;
	case NVME_OPC_CREATE_IO_CQ:
		handle_result = nvmet_create_io_cq(req, cmd);
		break;
	case NVME_OPC_IDENTIFY:
		handle_result = nvmet_identify(req, cmd);
		break;
	case NVME_OPC_ABORT:
		handle_result = nvmet_abort(req, cmd);
		break;
	case NVME_OPC_SET_FEATURES:
		handle_result = nvmet_set_feature(req, cmd);
        //nvme_apl_trace(LOG_ALW, 0, "handle result feature:0x%x",handle_result);
		break;
	case NVME_OPC_GET_FEATURES:
		handle_result = nvmet_get_feature(req, cmd);
		break;
	case NVME_OPC_ASYNC_EVENT_REQUEST:
		handle_result = nvmet_async_event_request(req, cmd);
		break;

#if CO_SUPPORT_SECURITY
	case NVME_OPC_SECURITY_SEND:
	case NVME_OPC_SECURITY_RECEIVE:
		handle_result = nvmet_security_cmd(req, cmd);
		break;
#endif

#if !defined(PROGRAMMER)
	#ifdef VSC_CUSTOMER_ENABLE	// FET, RelsP2AndGL
	case NVME_OPC_SSSTC_VSC_CUSTOMER:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xc756, "VSC F0");
		handle_result = nvmet_ssstc_vsc_f0cmd(req, cmd);
		break;
	#endif

	case NVME_OPC_SSSTC_VSC_NONE:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xc7c3, "VSC FC");
		handle_result = nvmet_ssstc_vsc_fccmd(req, cmd);
		break;
	case NVME_OPC_SSSTC_VSC_WRITE:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xc7c2, "VSC FD");
		handle_result = nvmet_ssstc_vsc_fdcmd(req, cmd);
		break;
	case NVME_OPC_SSSTC_VSC_READ:
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(200); // 20s// TODO DISCUSS
		#endif
		nvme_apl_trace(LOG_ERR, 0xc7c1, "VSC FE");
		handle_result = nvmet_ssstc_vsc_fecmd(req, cmd);
		break;
#endif
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
		handle_result = HANDLE_RESULT_FAILURE;
	}

	if (handle_result == HANDLE_RESULT_FAILURE)
	{
		struct nvme_status *nvme_status =
			(struct nvme_status *)&req->fe.nvme.nvme_status;
		nvme_apl_trace(LOG_ERR, 0x920c, "req(%x) Admin opcode(%x) CE: SCT(%x) SC(%x)", req, cmd->opc, nvme_status->sct, nvme_status->sc);
	}

	return handle_result;
}

ddr_code void nvmet_assert_admin_cmd_in(u32 cntlid)
{
	enum cmd_rslt_t result;
	struct nvme_cmd *host_cmd;
	bool refill = true;

	while ((ctrlr->free_nvme_cmds > 0) && (req_cnt > 0) && (dtag_get_admin_avail(DTAG_T_SRAM)) &&
		   (!dwnld_com_lock) && (host_cmd = hal_nvmet_get_admin_sq_cmd(cntlid, refill)))
	{

		req_t *req = nvmet_get_req();

		sys_assert(req != NULL);

		ctrlr->admin_running_cmds++;
		nvme_apl_trace(LOG_DEBUG, 0x349e, "admin cmd in %d",
					   ctrlr->admin_running_cmds);
		INIT_LIST_HEAD(&req->entry);
		req->req_from = REQ_Q_ADMIN;
		req->host_cmd = host_cmd;
		req->fe.nvme.sqid = 0;
		req->fe.nvme.cntlid = (u8)cntlid;
		req->fe.nvme.cid = host_cmd->cid;
		req->fe.nvme.cmd_spec = 0;
		req->fe.nvme.nvme_status = 0;
		req->error = 0;
		req->completion = nvmet_core_cmd_done;
		req->state = REQ_ST_FE_ADMIN;
		result = nvmet_assert_asq_handle(req);

		if ((result == HANDLE_RESULT_FINISHED) ||
			(result == HANDLE_RESULT_FAILURE))
		{
			req->completion(req);
		}
		else
		{
			nvme_apl_trace(LOG_DEBUG, 0xf696, "req(%x) rslt(%d)",
						   req, result);
		}
	}
}
#endif
/*! @} */
