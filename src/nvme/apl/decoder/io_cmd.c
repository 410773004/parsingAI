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
 * @brief I/O Queue operations
 *
 * \addtogroup decoder
 * \defgroup io_cmd I/O command
 * \ingroup decoder
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvmet.h"
#include "hal_nvme.h"
#include "cmd_proc.h"
#include "bf_mgr.h"
#include "btn_export.h"
#include "assert.h"
#include "event.h"
#include "hmb.h"
#include "misc.h"
#include "console.h"
#include "smart.h"
#include "ns.h"
#include "spin_lock.h"
#include "mpc.h"
#if CO_SUPPORT_SANITIZE
#include "epm.h"
extern epm_info_t *shr_epm_info;
extern epm_smart_t *epm_smart_data;
#endif
#if (_TCG_)
#include "tcgcommon.h"
#endif
#if (CO_SUPPORT_READ_AHEAD == TRUE)
#include "ra.h"
#endif
/*! \cond PRIVATE */
#define __FILEID__ io
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define NR_CMD_IN_HW_SQ         8	///< definition of HW SQ capacity
#define NR_SUPER_REQ_IN_DTCM    8	///< definition of DTCM super request capacity

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef struct _nvme_bp_rd_info_t {
	u64 prp;			///< host memory address
	u32 offset;		///< offset: 4K unit
	u32 length;		///< total transfer length: 4K unit
	u32 progress;		///< progress count
	dtag_t dtag;			///< dtag id of transfer length
	u32 xfer_sz;		///< transfer size
} nvme_bp_rd_info_t;

typedef struct {
	req_t *req;
	struct list_head entry;
} req_pnd_t;
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
fast_data volatile int io_max = NVME_ACTIVE_COMMAND_COUNT;
extern u8 host_sec_bitz;

slow_data_zi u8 pre_cmd_opc;

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------

static void discard_dtag_evt(void *ctx);

static fast_code bool nvmet_submit_cmd(req_t *req, struct nvme_cmd *cmd);

/*!
 * @brief command submit to next layer
 *
 * Submit command to dispatcher module (ramdisk/rawdisk).
 *
 * @param req	request
 * @param cmd	NVMe command
 *
 * @return	Return command submit status
 */
static fast_code bool nvmet_submit_cmd(req_t *req, struct nvme_cmd *cmd)
{
	return ctrlr->nsid[cmd->nsid - 1].ns->issue(req);
}

/*!
 * @brief	nvme command broadcast
 *
 * @param	req		request
 * @param	cmd		NVMe command
 *
 * @return	true if broadcast successfully
 */
static fast_code bool nvmet_broadcast_cmd(req_t *req, struct nvme_cmd *cmd)
{
	int i;
	bool ret = true;

	for (i = 0; i < NVMET_NR_NS; i++){
		 if(ctrlr->nsid[i].type == NSID_TYPE_ACTIVE)//joe 20201202  for IOL1.4 2.6-3
		 ret &= ctrlr->nsid[i].ns->issue(req);
	}

	return ret;
}

/*!
 * @brief flush command setup
 *
 * @param req	request
 * @param cmd	NVMe command
 *
 * @return	Return handle result status
 *
 *
 */
static enum cmd_rslt_t nvmet_setup_flush(req_t *req, struct nvme_cmd *cmd)
{
	req->opcode = REQ_T_FLUSH;
	req->op_fields.flush.shutdown = false;

#if PLP_SUPPORT == 0 //Eric
	//nvme_apl_trace(LOG_INFO, 0x3e81, "[NON-PLP] Flush");
	return HANDLE_RESULT_RUNNING;
#else
	//nvme_apl_trace(LOG_INFO, 0x18c1, "[PLP] NVME Flush");
	return HANDLE_RESULT_FINISHED;
#endif
	
}

/*!
 * @brief callback function when trim range was pull
 *
 * After trim ranges were pulled, continue to submit request
 *
 * @param _req	should be req
 * @param error	true for dma transfer error
 *
 * @return	not used
 */

static slow_code __attribute__((unused)) void trim_range_pull_done(void *_req, bool error)
{
	req_t *req = (req_t *) _req;

	sys_assert(error == false);

	req->req_prp.required--;
	/* check ranges valid status for IOL */
	if (req->req_prp.required == 0) {
		struct nvme_cmd *nvme_cmd = req->host_cmd;
		u32 list_cnt;
		u32 i;
		struct nvme_dsm_range *dsmr;
		list_cnt = req->op_fields.trim.nr;
		dsmr = (struct nvme_dsm_range *)req->op_fields.trim.dsmr;

		for (i = 0; i < list_cnt ; i++) {
			u64 slba = dsmr[i].starting_lba;
			u32 nlb = dsmr[i].length;
			if (slba > ctrlr->nsid[nvme_cmd->nsid - 1].ns->ncap ||
				slba + nlb > ctrlr->nsid[nvme_cmd->nsid - 1].ns->ncap) {
				nvme_apl_trace(LOG_INFO, 0x3fa6, "[TRIM ERR] nsid = %d", nvme_cmd->nsid);
				nvme_apl_trace(LOG_INFO, 0x3443, "[TRIM ERR] ns->ncap = 0x%x", ctrlr->nsid[nvme_cmd->nsid - 1].ns->ncap);
				nvme_apl_trace(LOG_INFO, 0x48d7, "[TRIM ERR] slba = 0x%x-%x, nlb = 0x%x", (u32)(slba >> 32), (u32)slba, nlb);
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC,
							NVME_SC_LBA_OUT_OF_RANGE);

				dtag_t dtag;
				dtag = mem2dtag(req->op_fields.trim.dsmr);
				dtag_put(DTAG_T_SRAM, dtag);

				if (req->completion)
					req->completion(req);
				return;
			}
		#if (_TCG_)
			else if((mTcgStatus & MBR_SHADOW_MODE) || (mWriteLockedStatus))
			{
				bool chk = false;
				if((mTcgStatus & MBR_SHADOW_MODE) && (slba < (0x8000000 >> host_sec_bitz)))
					chk = true;
				if(TcgRangeCheck(slba, nlb, mWriteLockedStatus))
					chk = true;

				if(chk)
				{
					nvmet_set_status(&req->fe, NVME_SCT_MEDIA_ERROR, NVME_SC_ACCESS_DENIED);
					struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
					nvme_status->dnr = 1;

					dtag_t dtag;
					dtag = mem2dtag(req->op_fields.trim.dsmr);
					dtag_put(DTAG_T_SRAM, dtag);

					if (req->completion)
						req->completion(req);
					return;
				}
			}
		#endif
		}
		nvmet_submit_cmd(req, req->host_cmd);
	}

}

#if CO_SUPPORT_WRITE_ZEROES
/*!
 * @brief write zeros command setup
 *
 * @param req	request
 * @param cmd	NVMe command
 *
 * @return	Return handle result status
 *
 * @note It's not supported, yet.
 */
static enum cmd_rslt_t nvmet_setup_write_zeros(req_t *req, struct nvme_cmd *cmd)
{
	u64 slba;
	u16 nlb;

	slba = (((u64)cmd->cdw11) << 32) | cmd->cdw10;
	nlb = (cmd->cdw12 & 0xffff) ;

	#if 0
	u8 lr = cmd->cdw12 & BIT(31);
	u8 fua = cmd->cdw12 & BIT(30);
	u8 prinfo = (u8)(cmd->cdw12 >> 24) & 0x3c;
	u8 deac = cmd->cdw12 & BIT(25);
	u32 ilbrt = cmd->cdw14;
	u16 lbat = (u16)(cmd->cdw15 & 0xffff);
	u16 lbatm = (u16)(cmd->cdw15 >> 16);
	#endif

	if (ctrlr->attr.b.write_zero == 0) {
		nvmet_set_status(&req->fe,
				NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
		return HANDLE_RESULT_FAILURE;
	}

	///Andy modify follwe with IOL Test 2.2 Case 3 (DataSet Management)
	if (slba > ctrlr->nsid[cmd->nsid - 1].ns->ncap
		|| (slba + nlb + 1) > ctrlr->nsid[cmd->nsid - 1].ns->ncap) {
		nvme_apl_trace(LOG_INFO, 0x9d1e, "[WRITE ZEROS ERR] nsid = %d", cmd->nsid);
		nvme_apl_trace(LOG_INFO, 0xe65d, "[WRITE ZEROS ERR] ns->ncap = 0x%x", ctrlr->nsid[cmd->nsid - 1].ns->ncap);
		nvme_apl_trace(LOG_INFO, 0x7c78, "[WRITE ZEROS ERR] slba = 0x%x-%x, nlb = 0x%x", (u32)(slba >> 32), (u32)slba, nlb);

		nvmet_set_status(&req->fe,
				NVME_SCT_GENERIC, NVME_SC_LBA_OUT_OF_RANGE);
		return HANDLE_RESULT_FAILURE;
	}

	req->lba.srage.slba = slba;
	req->lba.srage.nlb = nlb + 1;

	req->opcode = REQ_T_WZEROS;

	return HANDLE_RESULT_RUNNING;
}
#endif

#if CO_SUPPORT_WRITE_UNCORRECTABLE
/*!
 * @brief write uncorrectable command setup
 *
 * @param req	request
 * @param cmd	NVMe command
 *
 * @return	Return handle result status
 *
 * @note It's not supported, yet.
 */
ddr_code enum cmd_rslt_t nvmet_setup_write_unc(req_t *req, struct nvme_cmd *cmd)
{
	u64 slba;
	u16 nlb;

	slba = (((u64)cmd->cdw11) << 32) | cmd->cdw10;
	nlb = (cmd->cdw12 & 0xffff);

    nvme_apl_trace(LOG_INFO, 0xd40b, "WUNC: LBA(0x%x%x) NLB(%d)",GET_B63_32(slba),GET_B31_00(slba),nlb);

	if (ctrlr->attr.b.write_uc == 0) {
		nvmet_set_status(&req->fe,
				NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
		return HANDLE_RESULT_FAILURE;
	}

	///Andy Follow with NVMe spec MTDS, and IOL test 2.5  case5
	#if 1
	if (nlb > (1 << NVME_MAX_DATA_TRANSFER_SIZE)) {
		#if 0
        nvmet_set_status(&req->fe,
				NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
        #endif
        //Randolph
    	#ifdef NS_MANAGE
        //nlb = (1 << NVME_MAX_DATA_TRANSFER_SIZE);//joe open for iol2.5.5 20201215
        #else
	//nlb = (1 << NVME_MAX_DATA_TRANSFER_SIZE);//joe open for iol2.5.5 20201215
	#endif
	}
	#endif

	///Andy modify follwe with IOL Test 2.2 Case 3 (DataSet Management)
	if (slba > ctrlr->nsid[cmd->nsid - 1].ns->ncap ||
		(slba + nlb + 1) > ctrlr->nsid[cmd->nsid - 1].ns->ncap) {
		nvme_apl_trace(LOG_INFO, 0xc9dd, "[WRITE WUNC ERR] nsid = %d", cmd->nsid);
		nvme_apl_trace(LOG_INFO, 0xdf60, "[WRITE WUNC ERR] ns->ncap = 0x%x", ctrlr->nsid[cmd->nsid - 1].ns->ncap);
		nvme_apl_trace(LOG_INFO, 0x97f5, "[WRITE WUNC ERR] slba = 0x%x-%x, nlb = 0x%x", (u32)(slba >> 32), (u32)slba, nlb);
		nvmet_set_status(&req->fe,
				NVME_SCT_GENERIC, NVME_SC_LBA_OUT_OF_RANGE);
		return HANDLE_RESULT_FAILURE;
	}

	req->lba.srage.slba = slba;
#ifdef NS_MANAGE
	req->lba.srage.nlb = nlb + 1;
	req->nsid=cmd->nsid;//joe add 20201202 for IOL1.4  2.5
#else
	req->lba.srage.nlb = nlb+1;
#endif
	req->opcode = REQ_T_WUNC;

	return HANDLE_RESULT_RUNNING;
}
#endif

/*!
 * @brief trim command setup (Dataset Management)
 *
 * @param req	request
 * @param cmd	NVMe command
 *
 * @return	Return handle result status
 *
 * @note It's not supported, yet.
 */
static enum cmd_rslt_t nvmet_setup_discard(req_t *req, struct nvme_cmd *cmd)
{
	u32 nr = cmd->cdw10 & 0xff;	// number of ranges
	void *mem = NULL;
//	u32 ad = cmd->cdw11 & BIT(2);	// deallocate
//	u32 idw = cmd->cdw11 & BIT(1);	// integral dataset for write
//	u32 idr = cmd->cdw11 & BIT(0);	// integral dataset for read

	if (ctrlr->attr.b.dsm == 0) {
		nvmet_set_status(&req->fe,
				NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
		return HANDLE_RESULT_FAILURE;
	}
	if(cmd->cdw11 & 0xFFFFFFF8)
	{
		//unsupported DSM attribute,BIT0:IDR,BIT1:IDW,BIT2:AD
		nvmet_set_status(&req->fe,NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	req->opcode = REQ_T_TRIM;
	dtag_get(DTAG_T_SRAM, &mem);
	if (mem == NULL) {
		nvme_apl_trace(LOG_INFO, 0x52ad, "trim: lack of dtag,req:0x%x",req);
		dtag_register_evt(DTAG_T_SRAM, discard_dtag_evt, req, false);
		return HANDLE_RESULT_PENDING_FE;
	}

	req->op_fields.trim.att = cmd->cdw11;
	req->op_fields.trim.nr = nr + 1;
	req->state = REQ_ST_DISK;	// actually we are under xfering
	req->op_fields.trim.dsmr = mem;

	u64 prp = cmd->dptr.prp.prp1;
	u32 trans_len = ctrlr->page_size - (prp & (ctrlr->page_size - 1));
	u32 ofst = 0;
	u32 length;

	length = req->op_fields.trim.nr * sizeof(struct nvme_dsm_range);

	trans_len = min(length, trans_len);
	hal_nvmet_data_xfer(prp, mem, trans_len, READ, (void *) req,
				trim_range_pull_done);

	length -= trans_len;
	ofst += trans_len;
	req->req_prp.required = 1;

	if (length) {
		prp = cmd->dptr.prp.prp2;
		hal_nvmet_data_xfer(prp, ptr_inc(mem, ofst),
				length, READ, (void *) req, trim_range_pull_done);
		req->req_prp.required++;
	}

	return HANDLE_RESULT_PENDING_FE;
}

__attribute__((unused)) static fast_code void discard_dtag_evt(void *ctx)
{
	req_t *req = (req_t *) ctx;
	struct nvme_cmd *cmd = (struct nvme_cmd *) req->host_cmd;

	nvmet_setup_discard(req, cmd);
}

#if (_TCG_)
extern u32 mTcgStatus;
extern u16 mWriteLockedStatus;
extern tcg_io_chk_func_t tcg_io_chk_range;
ddr_code bool nvmet_io_cmd_tcg_chk(req_t *req)
{
	struct nvme_cmd *cmd = req->host_cmd;
	u64 slba  = (((u64)cmd->cdw11) << 32) | cmd->cdw10;
	u64 nlba = (((u64)cmd->cdw12) & 0xffff)+1;

	switch (cmd->opc)
	{
		case NVME_OPC_FLUSH:
	#if CO_SUPPORT_WRITE_ZEROES
		case NVME_OPC_WRITE_ZEROES:
	#endif
	#if CO_SUPPORT_WRITE_UNCORRECTABLE
		case NVME_OPC_WRITE_UNCORRECTABLE:
	#endif
			if((mTcgStatus & MBR_SHADOW_MODE) && (slba < (0x8000000 >> host_sec_bitz)))
			{
				nvmet_set_status(&req->fe,NVME_SCT_MEDIA_ERROR, NVME_SC_ACCESS_DENIED);
				struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
				nvme_status->dnr = 1;
				return true;
			}
			else
			{
				if(TcgRangeCheck(slba, nlba, mWriteLockedStatus))
				{
					nvmet_set_status(&req->fe,NVME_SCT_MEDIA_ERROR, NVME_SC_ACCESS_DENIED);
					struct nvme_status *nvme_status = (struct nvme_status *) &req->fe.nvme.nvme_status;
					nvme_status->dnr = 1;
					return true;
				}
			}
			break;

		default:
			break;
	}


	return false;
}
#endif

/*!
 * @brief NVMe I/O SQ commands core handler
 *
 * @param req	request
 * @param cmd	NVM command binds to the request
 *
 * @return	Return request handle result
 */
static fast_code enum cmd_rslt_t nvmet_sq_handle(req_t *req)
{
#if defined(PROGRAMMER)
	//for programmer, it doesn't handle any IO commands as usual. Just finish them without any error.
	return HANDLE_RESULT_FINISHED;
#endif /* PROGRAMMER */
	enum cmd_rslt_t handle_result;
	struct nvme_cmd *cmd = req->host_cmd;
	u32 nsid0;

#if CO_SUPPORT_SANITIZE
	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	if(((epm_smart_data->sanitizeInfo.sanitize_log_page >> 16) & 0x7) == sSanitizeCompletedFail)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_FAILED);
		handle_result = HANDLE_RESULT_FAILURE;
		return handle_result;
	}
	else if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates != sSanitize_FW_None)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC,NVME_SC_SANITIZE_IN_PROGRESS);
		handle_result = HANDLE_RESULT_FAILURE;
		return handle_result;
	}
	//nvme_apl_trace(LOG_ERR, 0, "Sntz FW sts: %d", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates);
#endif

	extern u8  cur_ro_status;

	nsid0 = cmd->nsid - 1;
	if((cmd->fuse != 0) || (cmd->psdt != 0) || (cmd->rsvd1 != 0))
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		//nvme_apl_trace(LOG_DEBUG, 0x3fdd, "fused:%x psdt:%x rsvd1:%x",cmd->fuse, cmd->psdt, cmd->rsvd1);
		return HANDLE_RESULT_FAILURE;
	}

	if (cmd->opc == NVME_OPC_FLUSH && cmd->nsid == 0xFFFFFFFF) {// 7FFFF //for IOL 2.6
		nsid0 = ~0;	/// todo: support broadcast flush
		goto skip_ns_check;
	}

#if !defined(PERF_BUILD)
	if(cmd->nsid == 0xFFFFFFFF)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		return HANDLE_RESULT_FAILURE;
	}

	if ((nsid0 >= NVMET_NR_NS) || (ctrlr->nsid[nsid0].ns == NULL)) {
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}

	if (ctrlr->nsid[nsid0].type != NSID_TYPE_ACTIVE) {
		if(cmd->opc == NVME_OPC_FLUSH)
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		else
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);
		return HANDLE_RESULT_FAILURE;
	}
#endif
skip_ns_check:
	req->nsid = nsid0;

#if(degrade_mode == ENABLE)
	extern read_only_t read_only_flags;
	extern none_access_mode_t noneaccess_mode_flags;
	if(read_only_flags.b.plp_not_done || read_only_flags.b.spor_user_build || noneaccess_mode_flags.b.defect_table_fail || noneaccess_mode_flags.b.tcg_key_table_fail)
	{
		if(cmd->opc == NVME_OPC_READ || cmd->opc == NVME_OPC_WRITE || cmd->opc == NVME_OPC_WRITE_UNCORRECTABLE)
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);
			if((pre_cmd_opc != cmd->opc) && !(read_only_flags.b.plp_not_done || read_only_flags.b.spor_user_build))
				evlog_printk(LOG_INFO,"[NON-ACCESS-MODE] cmd->opc : %x",cmd->opc);

			pre_cmd_opc =  cmd->opc;
			return HANDLE_RESULT_FAILURE;
		}
	}
#endif

	if(cur_ro_status)
	{
		if(cmd->opc != NVME_OPC_READ && cmd->opc != NVME_OPC_COMPARE)
		{
			nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_ATTEMPTED_WRITE_TO_RO_PAGE);
			evlog_printk(LOG_INFO,"RO RETURN cmd->opc : %x, sts: %x",cmd->opc, read_only_flags.all);
			extern void nvmet_evt_aer_in();
			nvmet_evt_aer_in(((NVME_EVENT_TYPE_SMART_HEALTH << 16)|SMART_STS_RELIABILITY),0);
			return HANDLE_RESULT_FAILURE;
		}
	}

	switch (cmd->opc) {
	case NVME_OPC_WRITE:
	case NVME_OPC_READ:
#if defined(RAMDISK)
	case NVME_OPC_COMPARE:
#endif
		handle_result = HANDLE_RESULT_FAILURE;
		panic("should be in btn");
		break;
	case NVME_OPC_FLUSH:
		handle_result = nvmet_setup_flush(req, cmd);
		break;
	case NVME_OPC_DATASET_MANAGEMENT:
        #if (CO_SUPPORT_READ_AHEAD == TRUE)
        ra_disable();
        #endif
		handle_result = nvmet_setup_discard(req, cmd);
		break;
#if CO_SUPPORT_WRITE_ZEROES
	case NVME_OPC_WRITE_ZEROES:
#if (_TCG_)
		if(nvmet_io_cmd_tcg_chk(req))
		{
			handle_result = HANDLE_RESULT_FAILURE;
			break;
		}
#endif

		handle_result = nvmet_setup_write_zeros(req, cmd);
		break;
#endif
#if CO_SUPPORT_WRITE_UNCORRECTABLE
	case NVME_OPC_WRITE_UNCORRECTABLE:
#if (_TCG_)
		if(nvmet_io_cmd_tcg_chk(req))
		{
			handle_result = HANDLE_RESULT_FAILURE;
			break;
		}
#endif
		if(host_sec_bitz == 9)
		{
			handle_result = nvmet_setup_write_unc(req, cmd);
			break;
		}
		else
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
			return HANDLE_RESULT_FAILURE;
		}
#endif
	default:
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
		handle_result = HANDLE_RESULT_FAILURE;
	}

	switch (handle_result) {
	case HANDLE_RESULT_RUNNING:
		if (nsid0 != ~0) {
			if (!ctrlr->nsid[nsid0].ns || !ctrlr->nsid[nsid0].ns->issue) {
				nvme_apl_trace(LOG_ERR, 0x96fd, "No submit handler for NS(%d)", cmd->nsid);
				return HANDLE_RESULT_FAILURE;
			}
			req->state = REQ_ST_DISK;
			if (nvmet_submit_cmd(req, cmd) == false) {
				return HANDLE_RESULT_PENDING_BE;
			}
		} else {
			nvmet_broadcast_cmd(req, cmd);
		}
		return handle_result;
	default:
		return handle_result;
	}
}

/*!
 * @brief  cmd_proc rx NVMe I/O SQ commands core handler
 *
 * @param sqid	sq id
 * @param cmd	NVM command
 * @param nvm_cmd_id		NVM command id
 *
 * @return	None
 */
void fast_code nvmet_rx_handle_cmd(u32 sqid, struct nvme_cmd *cmd, u32 nvm_cmd_id, u8 fun_id)
{
	req_t *req;
	enum cmd_rslt_t result;
#if defined(SRIOV_SUPPORT)
	struct nvmet_sq *sq;
	sq = (fun_id == 0) ? ctrlr->sqs[sqid]: ctrlr->sr_iov->fqr_sq_cq[fun_id-1].fqrsq[sqid];
#else
	struct nvmet_sq *sq = ctrlr->sqs[sqid];
#endif
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_disable();
	#endif

    //sys_assert(sq);
	if(sq == NULL)
	{
		nvme_apl_trace(LOG_ERR, 0xbab6, "SQ(%d) NULL %x", sqid);
		hal_nvmet_put_sq_cmd(cmd);
		return;
	}

	req = nvmet_get_req();
	sys_assert(req);
	list_add_tail(&req->entry, &sq->reqs);

	req->host_cmd = cmd;
	req->error = 0;
	req->req_from = REQ_Q_IO;
	req->state = REQ_ST_FE_IO;

#if defined(SRIOV_SUPPORT)
	/* sqid should be flexible SQ index.*/
	req->fe.nvme.sqid = (fun_id == 0) ? sqid: SRIOV_FLEX_PF_ADM_IO_Q_TOTAL +
			(fun_id - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC + sqid;
#else
	req->fe.nvme.sqid = sqid;
#endif
	req->fe.nvme.cntlid = fun_id;
	req->fe.nvme.cid = cmd->cid;
	req->fe.nvme.nvme_status = 0;
	req->fe.nvme.cmd_spec = 0;
	req->nvm_cmd_id = nvm_cmd_id;
	//extern vu32 Rdcmd;
	//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
	//Rdcmd++;
	//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
	req->completion = nvmet_core_cmd_done;

	ctrlr->cmd_proc_running_cmds++;

	result = nvmet_sq_handle(req);
	switch (result) {
	case HANDLE_RESULT_FINISHED:
	case HANDLE_RESULT_FAILURE:
		req->completion(req);
		break;
	case HANDLE_RESULT_PENDING_BE:
		sys_assert(0);
	default:
		break;
	}

	return;
}

/*!
 * @brief  BTN NVMe I/O SQ commands done handler
 *
 * @param btag		btn tag
 * @param ret_cq	need FW return CQ
 * @param cid		NVM command id
 *
 * @return		not used
 */
fast_code void nvmet_core_btn_cmd_done(int btag, bool ret_cq, u32 cid)
{
	if (ret_cq) {
		btn_cmd_t *bcmd = btag2bcmd(btag);
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
		fe_t fe;

		fe.nvme.cid = cid;
		fe.nvme.sqid = bcmd->dw1.b.cmd_sqid;

		// error from BTN
		if (bcmd_ex->flags.b.err)
		{
			if (bcmd->dw1.b.compare)
			{
				nvmet_set_status(&fe, NVME_SCT_MEDIA_ERROR, NVME_SC_COMPARE_FAILURE);
			}
			else
			{
				//nvme_apl_trace(LOG_ERR, 0, "[DBG] BTN CMD DONE btag[0x%x] nvm_id[0x%x] cid[%d]", btag, bcmd->dw0.b.nvm_cmd_id, cid);  //test tony
				//if(bcmd_ex->flags.b.bcmd_ecc_hit)
				//	nvmet_set_status(&fe, NVME_SCT_MEDIA_ERROR, NVME_SC_UNRECOVERED_READ_ERROR);
				//else
					nvmet_set_status(&fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);
			}
		}
	#if TCG_WRITE_DATA_ENTRY_ABORT
		else if(bcmd_ex->flags.b.tcg_wr_abrt)
		{
			// error from TCG locked
			//extern bool tcg_rdcmd_list_search_del(u32 val);
			//tcg_rdcmd_list_search_del(bcmd->dw0.b.nvm_cmd_id);
			nvmet_set_status(&fe, NVME_SCT_MEDIA_ERROR, NVME_SC_ACCESS_DENIED);
			struct nvme_status *nvme_status = (struct nvme_status *) &fe.nvme.nvme_status;
			nvme_status->dnr = 1;
		}
	#endif
		else
		{
			// error from nvme
			nvmet_set_status(&fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);
		}

		nvmet_core_handle_cq(&fe);
	}
}

static ddr_code bool nvmet_bootpart_be_read_done(req_t *req);

#if VALIDATE_BOOT_PARTITION
static void validate_bp_read_done()
{
	extern req_t *bp_req;
	hal_nvmet_bootpart_upt_brs(NVME_BP_BRS_CMPL);

	evt_set_imt(evt_cmd_done, (u32)bp_req, 0);
	return;
}
#endif //VALIDATE_BOOT_PARTITION
/*!
 * @brief read boot partition from backend
 *
 * @param bp_rd_info	boot partition read information
 * @param ofst		current read offset: 4K unit
 *
 * @return		none
 */
ddr_code void nvmet_bootpart_be_read(nvme_bp_rd_info_t *bp_rd_info, u32 ofst)
{
	req_t *req;

#if VALIDATE_BOOT_PARTITION
	u32 cur = bp_rd_info->offset/4096;
	extern dtag_t *bp_dtags;
	extern req_t *bp_req;
	void *mem = dtag2mem(bp_dtags[cur].b.dtag);
	hal_nvmet_data_xfer(bp_rd_info->prp, mem, DTAG_SZE, WRITE, bp_rd_info, validate_bp_read_done);
	return;
#endif
	req = nvmet_get_req();
	sys_assert(req);

	req->req_from = REQ_Q_OTHER;
	req->opcode = REQ_T_BP_READ;
	req->lba.srage.slba = ofst;		// unit: 4K
	req->lba.srage.nlb = 1;
	req->op_fields.bp_read.dtag_mem = dtag2mem(bp_rd_info->dtag);
	req->completion = nvmet_bootpart_be_read_done;
	req->host_cmd = (void *) bp_rd_info;

	ctrlr->nsid[0].ns->issue(req);
}

/*!
 * @brief boot partition host dma read done (write to host memory)
 *
 * @param hcmd		should be nvme_bp_rd_info
 * @param error		true for dma transfer error
 *
 * @return		not used
 */
static ddr_code void nvmet_bootpart_host_read_done(void *hcmd, bool error)
{
	nvme_bp_rd_info_t *bp_rd_info = (nvme_bp_rd_info_t *) hcmd;

	sys_assert(error == false);

	bp_rd_info->progress += bp_rd_info->xfer_sz >> 12;

	if (bp_rd_info->progress == bp_rd_info->length) {
		hal_nvmet_bootpart_upt_brs(NVME_BP_BRS_CMPL);
		dtag_put(DTAG_T_SRAM, bp_rd_info->dtag);
		sys_free(FAST_DATA, bp_rd_info);
	} else {
		u32 ofst = bp_rd_info->offset + bp_rd_info->progress;

		nvmet_bootpart_be_read(bp_rd_info, ofst);
	}
}

/*!
 * @brief callback function for boot partition backend read done
 *
 * @param req		req
 *
 * @return		not used
 */
static ddr_code bool nvmet_bootpart_be_read_done(req_t *req)
{
	nvme_bp_rd_info_t *bp_rd_info = (nvme_bp_rd_info_t *) req->host_cmd;
	void *mem;
	u16 len;
	u64 hmem;

	bp_rd_info->xfer_sz = req->lba.srage.nlb << 12;

	nvmet_put_req(req);

	mem = dtag2mem(bp_rd_info->dtag);
	len = bp_rd_info->xfer_sz;
	hmem = bp_rd_info->prp + (bp_rd_info->progress << 12);
	hal_nvmet_data_xfer(hmem, mem, len, WRITE, bp_rd_info, nvmet_bootpart_host_read_done);

	return false;
}

/*!
 * @brief read boot partition function
 * Boot partition read event handler to read boot partition.
 *
 * @param param		not used
 * @param payload	not used
 * @param sts		not used
 *
 * @return		None
 */
static ddr_code void nvmet_bootpart_read(u32 param, u32 payload, u32 sts)
{
	u32 ofst;
	u32 len;
	u64 hmem;
	nvme_bp_rd_info_t *bp_rd_info;

	hal_nvmet_bootpart_get_cmd(&ofst, &len, &hmem);

	hal_nvmet_bootpart_upt_brs(NVME_BP_BRS_AIP);

	nvme_apl_trace(LOG_INFO, 0x4434, "BPR: ofst (%x), len (%x) -> mem (0x%p%p)", ofst, len, (hmem >> 16) >> 16, hmem);

	bp_rd_info = sys_malloc(FAST_DATA, sizeof(nvme_bp_rd_info_t));
	sys_assert(bp_rd_info);

	bp_rd_info->length = len;
	bp_rd_info->offset = ofst;
	bp_rd_info->prp = hmem;
	bp_rd_info->progress = 0;
	bp_rd_info->dtag = dtag_get_urgt(DTAG_T_SRAM, NULL);

	/* split requests to backend */
	nvmet_bootpart_be_read(bp_rd_info, ofst);
}

init_code void nvmet_io_cmd_init(void)
{
	evt_register(nvmet_bootpart_read, 0, &evt_bootpart_rd);
}

#if CO_SUPPORT_PANIC_DEGRADED_MODE
/*!
 * @brief NVMe I/O SQ commands core handler
 *
 * @param req	request
 * @param cmd	NVM command binds to the request
 *
 * @return	Return request handle result
 */
static ddr_code enum cmd_rslt_t nvmet_assert_sq_handle(req_t *req)
{
	//enum cmd_rslt_t handle_result;
	struct nvme_cmd *cmd = req->host_cmd;

	nvme_apl_trace(LOG_INFO, 0xae98, "[DMODE] caused by panic, abort CMD:%d",cmd->opc);
	nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INTERNAL_DEVICE_ERROR);

	return HANDLE_RESULT_FAILURE;
}

void ddr_code nvmet_rx_handle_assert_cmd(u32 sqid, struct nvme_cmd *cmd, u32 nvm_cmd_id, u8 fun_id)
{
	req_t *req;
	enum cmd_rslt_t result;

	struct nvmet_sq *sq = ctrlr->sqs[sqid];

    sys_assert(sq);

	req = nvmet_get_req();
	sys_assert(req);
	list_add_tail(&req->entry, &sq->reqs);

	req->host_cmd = cmd;
	req->error = 0;
	req->req_from = REQ_Q_IO;
	req->state = REQ_ST_FE_IO;
	req->fe.nvme.sqid = sqid;
	req->fe.nvme.cntlid = fun_id;
	req->fe.nvme.cid = cmd->cid;
	req->fe.nvme.nvme_status = 0;
	req->fe.nvme.cmd_spec = 0;
	req->nvm_cmd_id = nvm_cmd_id;
	req->completion = nvmet_core_cmd_done;
	ctrlr->cmd_proc_running_cmds++;

	result = nvmet_assert_sq_handle(req);
	switch (result) {
	case HANDLE_RESULT_FINISHED:
	case HANDLE_RESULT_FAILURE:
		req->completion(req);
		break;
	case HANDLE_RESULT_PENDING_BE:
		sys_assert(0);
	default:
		break;
	}

	return;
}
#endif
/*! @} */
