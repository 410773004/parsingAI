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
 * @brief Controller function for HAL layer
 *
 * \addtogroup hal
 * \defgroup ctrlr controller
 * \ingroup hal
 * @{
 */
#pragma once

#include "req.h"
#include "nvme_spec.h"

enum {
	HCMD_REQID_INV = 0xFF,	///< invalid req id for HCMD
	HCMD_REQID_DIS = 0xFE	///< discard req id for HCMD, HMB may discard auto lookup response for a HCMD without req id
};

// reserve N ~ 255 for FW SQs
#define MAX_FW_SQ_ID	0xFF

// total NVM commands FW can use
#define NUM_FW_NVM_CMD		32

#define mENABLE         1
#define mDISABLE        0
#define PI_FW_UPDATE	mENABLE//20210110-Eddie
#define WB_RESUME		mENABLE	//20210401-Eddie
#define PI_FW_UPDATE_REFLOW	mENABLE//20210503-Eddie
/*!
 * @brief request data pattern generation
 *
 * @param slba		start lba
 * @param len		how many sectors, 1-based
 * @param nsid		namespace ID
 * @param user_idx	indicates the caller
 *
 * @ return 		NVM_cmd_slot, value 0xFFFFFFFF means request aborted
 */
extern u32 pat_gen_req(u64 slba, u32 len, u32 nsid, u32 user_idx);

/*!
 * @brief release one request of data pattern generation
 *
 * @param cmd_slot	give the NVM_cmd_slot to be released
 *
 * @return none
 */
extern void pat_gen_rel(u32 cmd_slot);

/*!
 * @brief retrieve nvme_register
 *
 * @return	nvme_register structure
 */
extern struct nvme_registers *hal_nvmet_ctrlr_registers(void);

/*!
 * @brief NVMe get SQ command
 *
 * Retrieve an (A)SQ nvme_cmd for execution, increase rptr & fill a slot.
 *
 * @param sqid		qid of Admin or I/O queue
 * @param refill	refill a new NVM command place-holder or not
 *
 * @return		None or a nvme_cmd
 */
extern struct nvme_cmd *hal_nvmet_get_sq_cmd(u16 sqid, bool refill);

/*!
 * @brief bind req_id to host command
 *
 * Bind req id to host command for HMB feature.
 *
 * @param hcmd		host command to be set
 * @param req_id	req id to be bind to host command
 *
 * @return 		None
 */
extern void hal_nvmet_cmd_setup_req_id(struct nvme_cmd *hcmd, u32 req_id);

/*!
 * @brief setup discard req id for this host command to indicate there is no auto lookup response
 *
 * @param idx	host command index
 *
 * @return	none
 */
extern void hal_nvmet_cmd_set_dis_req_id(u32 idx);

/*!
 * @brief get req id of host command
 *
 * @param cmd	host command pointer
 *
 * @return	req id of this command
 */
extern u32 hal_nvmet_cmd_get_req_id_by_hcmd(struct nvme_cmd *cmd);

/*!
 * @brief get req id from host command
 *
 * @param idx	host command index
 *
 * @return	Return req id binding to host command
 */
extern u32 hal_nvmet_cmd_get_req_id(u32 idx);

/*!
 * @brief release the nvme_cmd for recycling
 *
 * Put the nvme_cmd to (A)SQ for recycling
 *
 * @param cmd	the recycling nvme_cmd to pool
 *
 * @return	None
 */
extern void hal_nvmet_put_sq_cmd(struct nvme_cmd *cmd);

/*!
 * @brief delete I/O completion queue
 *
 * @param cqid		the queue to be deleted
 *
 * @return		Return success
 */
extern bool hal_nvmet_delete_cq(u16 cqid);

/*!
 * @brief delete I/O submission queue
 *
 * @param sqid		the queue to be deleted
 *
 * @return		Return success
 */
extern bool hal_nvmet_delete_sq(u16 sqid);

/*!
 * @brief create HAL I/O submission Queue
 *
 * Create I/O submission queue with parameters from nvme_cmd.
 *
 * @param sqid		submission queue ID
 * @param cqid		which sqid binds to
 * @param qsize		queue depth of specific Submission
 *
 * @return		true: successful, false: return the error status to host
 */
extern bool hal_nvmet_create_sq(u16 sqid, u16 cqid, u16 qsize, u8 qprio, u8 pc, u64 prp1);

/*!
 * @brief create I/O completion queue
 *
 * Create I/O completion queue with parameters from nvme_cmd.
 *
 * @param cqid		completion queue ID
 * @param qsize		qdepth of corresponding completion queue
 * @param vector	IRQ/MSI(X) triggers for attention
 * @param ien		is interrupt enable or not for completion queue
 * @param pc		is the prp contiguous or not
 * @param prpr1		host Queue physical address
 *
 * @return		true: successful, false: return the error status to host
 */
extern bool hal_nvmet_create_cq(u16 cqid, u16 qsize, u16 iv, u16 ien, u16 pc, u64 prp);

/*!
 * @brief update CQ entry and send out the result
 * Use ACQ for all queue completion currently.
 *
 * @param cqid	where the entry to update
 * @param cpl	the completion entry to send
 * @param ppnd	is the cpl entry postponed before
 *
 * @return	true:  delivery success, false : enable CQ request ISR and postpone to deliver
 */
extern bool hal_nvmet_update_cq(u16 cqid, struct nvme_cpl *cpl, bool ppnd);

/*!
 * @brief notify HAL to kickoff
 *
 * Notify HAL to kickoff, etc. enable IRQ.
 *
 * @return	None
 */
extern void hal_nvmet_kickoff(void);

/*!
 * @brief notify HAL to stop IO command
 *
 * Notify HAL to stop, etc. disable IRQ.
 *
 * @return	None
 */
extern void hal_nvmet_io_stop(void);

/*!
 * @brief Get SQ pointer
 *
 * @param sqid	SQ id
 * @param head	current head pointer (software updated)
 * @param tail	current tail pointer (headware updated)
 *
 * @return	none
 */
extern void hal_nvmet_get_sq_pnter(u16 sqid, u16 *head,
		u16 *tail);

/*!
 * @brief fast track of Queue indicator
 *
 * Fast track of Queue indicator other than (wptr == rptr), ASQ is not supported yet.
 *
 * @param sqid	the submission queue to be observed
 *
 * @return	true: the queue has pending command(s), false: the queue is empty
 *
 **/
extern bool hal_nvmet_sq_has_cmd(u16 sqid);

/*!
 * @brief aggregation setting for time and threshold
 *
 * @param thr	maximum aggregation number, 0'based value
 * @param time	maximum aggregation time, count in 100us
 *
 * @return	None
 */
extern void hal_nvmet_set_aggregation(u8 thr, u8 time);

/*!
 * @brief get aggregation settings for time and threshold
 *
 * @return	Return aggregation settings in register
 */
extern u32 hal_nvmet_get_aggregation(void);

/*!
 * @brief data transfer using dedicated DMA engine
 *
 * @param prp		where the data in host
 * @param sram		FW sram address
 * @param len		data length
 * @param dir		0 for READ and 1 for WRITE
 * @param hcmd		Host Cmd binds to the transfer
 * @param callback	callback, if null, for admin command only
 *
 * @return		Success
 **/
extern bool hal_nvmet_data_xfer(u64 prp, void *sram, u16 len, dir_t dir, void *hcmd, void (*callback)(void *hcmd, bool error));

/*!
 * @brief interrupt vector coalescing configuration setting
 *
 * @param vector	interrupt vector
 * @param cd		coalescing disable
 *
 * @return	None
 */
extern void hal_nvmet_set_int_calsc(u16 vector, bool cd);

/*!
 * @brief config interrupt Coalescing for noramal mode
 *
 * @param vector	interrupt vector
 * @param cd		coalescing disable
 *
 * @return		not used
 */
extern void hal_nvmet_set_flex_int_calsc(u16 vector, bool cd);

/*!
 * @brief get boot partition commands
 *
 * @param ofst	boot partition read offset
 * @param len	data read size
 * @param hmem	host memory
 *
 * @return	Success
 */
extern bool hal_nvmet_bootpart_get_cmd(u32 *ofst, u32 *len, u64 *hmem);

/*!
 * @brief update boot partition read status
 *
 * @param brs	boot partition read status to be update
 *
 * @return	None
 */
extern void hal_nvmet_bootpart_upt_brs(enum nvme_brs brs);

/*!
 * @brief Set Active Boot Partition Identifier
 *
 * @param abpid	active boot partition id to be set
 *
 * @return	None
 */
extern void hal_nvmet_bootpart_set_abpid(u8 abpid);

/*!
 * @brief abort nvme read and write DMA xfer
 *
 * @param abort: abort DMA or clear abort DMA
 *
 * @return	None
 */
extern void hal_nvmet_abort_xfer(bool abort);

/*!
 * @brief reset NVMe module
 *
 * @param None
 *
 * @return	not used
 */
extern void hal_nvmet_reset(void);

/*!
 * @brief resume NVMe module
 *
 * @param None
 *
 * @return	not used
 */
extern void hal_nvmet_resume(void);

/*!
 * @brief case u32 command entry to nvme command pointer
 *
 * @param cmd	nvme command entry from command base array
 *
 * @return	nvme command pointer
 */
extern struct nvme_cmd *nvme_cmd_cast(u32 cmd);

/*!
 * @brief pop nvme command from pool and make a command entry of command base array
 *
 * @param str	debug message
 *
 * @return	nvme command entry of command base array
 */
extern u32 nvme_cmd_get(const char *str);

/*!
 * @brief is controller enable
 *
 * @param cntlid controller id
 *
 * @return	true: controller enable false: controller disable
 */
extern bool nvmet_vf_is_cc_en(u16 vf_offset);

/*!
 * @brief initialize VF admin Qqeue, set ASQ/ACQ queue base/qsize
 *
 * @param cntlid	nvme vf controller id
 *
 * @return		None
 *
 * @note
 *
 *
 */
extern void nvmet_init_vf_aqa_attrs(u16 cntlid);

/*!
 * @brief get new admin command used for SRIOV mode or normal mode
 *
 * @param cntlid controller id
 * @param refill refill a new NVM command place-holder or not
 *
 * @return	nvme command struct
 */
extern struct nvme_cmd *hal_nvmet_get_admin_sq_cmd(u16 cntlid, bool refill);

/*!
 * @brief create new sq used for SRIOV mode or normal mode
 *
 * @param cntlid controller id
 * @param sqid sq id
 * @param cqid cq id
 * @param qsize sq size
 * @param qprio Queue Priority
 * @param pc Physically Contiguous
 * @param prp1 PRP Entry 1
 *
 * @return	create new sq result
 */
extern fast_code bool hal_nvmet_create_vf_sq(u16 cntlid, u16 sqid, u16 cqid,
		    u16 qsize,  enum nvme_qprio qprio, u8 pc, u64 prp1);

/*!
 * @brief create new cq used for SRIOV mode or normal mode
 *
 * @param cntlid controller id
 * @param cqid cq id
 * @param qsize cq size
 * @param vector msi-x vector
 * @param ien cq interrupt enable
 * @param pc Physically Contiguous
 * @param prp1 PRP Entry 1
 *
 * @return	create new cq result
 */
extern fast_code bool hal_nvmet_create_vf_cq(u16 cntlid, u16 cqid, u16 qsize,
		    u16 vector, u16 ien, u16 pc, u64 prp1);

/*!
 * @brief	delete sq used for SRIOV mode or normal mode
 *
 * @param	sqid: sq id
 *
 * @return	delete sq result
 */
extern bool hal_nvmet_delete_vq_sq(u16 sqid);

/*!
 * @brief	delete cq used for SRIOV mode or normal mode
 *
 * @param	vq_mid: flexible queue mapping ID (0 to 65)
 *
 * @return	delete cq result
 */
extern bool hal_nvmet_delete_vq_cq(u16 vq_mid);

/*!
 * @brief	nvme check io sq pending
 *
 * @param	None.
 *
 * @return	false	io sq pending
 * @return	true	io sq not pending
 */
extern bool hal_nvmet_check_io_sq_pending(void);

/*!
 * @brief	set sq controls command arbitration to HW
 *
 * @param	hpw High Priority Weight
 * @param	mpw Medium Priority Weight
 * @param	lpw Low Priority Weight
 *
 * @return	None
 */
extern void hal_nvmet_set_sq_arbitration(u8 hpw, u8 mpw, u8 lpw);

/*!
 * @brief	suspend nvme controls fetch command
 *
 * @param	None
 *
 * @return	None
 */
extern void hal_nvmet_suspend_cmd_fetch(void);

/*!
 * @brief	enable nvme controls fetch command
 *
 * @param	None
 *
 * @return	None
 */
extern void hal_nvmet_enable_cmd_fetch(void);

extern void hal_pi_enable_cmd_fetch(void);
extern void hal_pi_suspend_cmd_fetch(void);
/*!
 * @brief	enable nvme controls fetch command after warm boot
 *
 * @param	None
 *
 * @return	None
 */
extern void nvmet_enable_fetched_cmd_warmboot(void);

/*!
 * @brief	This function get the Flexible SQ index of host sq for a function.
 *
 * @param	fun_id: Function index. 0 to 32. 0 PF0, 1 VF1, 2 VF2 and so on.
 * @param	hst_sqid: Host SQ ID. 0 for ASQ and >0 for IOSQ
 *
 * @return	Flexible SQ Index of host sq for the given function.
 */
extern u8 nvmet_get_flex_sq_map_idx(u8 fun_id, u8 hst_sqid);

/*!
 * @brief This function get the Flexible CQ index of host cq for a function.
 *
 * @param fun_id	Function index. 0 for PF and 1 to 32 for VF
 * @param hst_cqid	Host CQ ID. 0 for ASQ and >1 for IOCQ
 *
 * @return	Flexible CQ Index of host cq for the given function.
 */
extern u8 nvmet_get_flex_cq_map_idx(u8 fun_id, u8 hst_cqid);

/*!
 * @brief This function get the Flexible MSIX index of host msix for a function.
 *
 * @param fun_id	Function index. 0 to 32. 0 PF0, 1 VF1, 2 VF2 and so on.
 * @param hst_msix	Host msix ID. 0 for ASQ and >1 for IOCQ
 *
 * @return	Flexible MSIX Index of host msix for the given function.
 */
extern u8 nvmet_get_flex_vi_map_idx(u8 fun_id, u8 hst_msix);

/*!
 * @brief This function maps Flexible SQ index to host SQ index for a function.
 *
 * @param flex_sq_idx	Flexible SQ index. Range 0 to 65
 * @param fun_id	Function ID. 0 for PF0; 1 to 32 for VF
 * @param hst_sq_id	Host SQ index for this function. 0 for Admin Q. >0 for IOSQ
 *
 * @return	None.
 */
extern void nvmet_map_flex_sq_2_fun_sq(u8 flex_sq_idx, u8 fun_id, u8 hst_sq_id);

/*!
 * @brief This function maps Flexible CQ index to host CQ index for a function.
 *
 * @param flex_cq_idx	Flexible CQ index. Range 0 to 65
 * @param fun_id	Function ID. 0 for PF0; 1 to 32 for VF
 * @param hst_cq_id	Host CQ index for this function. 0 for Admin Q. >0 for IOCQ
 *
 * @return	None.
 */
extern void nvmet_map_flex_cq_2_fun_cq(u8 flex_cq_idx, u8 fun_id, u8 hst_cq_id);

/*!
 * @brief This function maps Flexible msix index to host msix index for a function.
 *
 * @param flex_mx_idx	Flexible msix index. Range 0 to 65
 * @param fun_id	Function ID. 0 for PF0; 1 to 32 for VF
 * @param hst_mx_id	Host msix index for this function.
 *
 * @return	None.
 */
extern void nvmet_map_flex_vi_2_fun_vi(u8 flex_mx_idx, u8 fun_id, u8 hst_mx_id);

/*!
 * @brief This function unmaps Flexible SQ index.
 *
 * @param flex_sq_idx	resource index. Range 0 to 65
 *
 * @return	None.
 */
extern void nvmet_unmap_flex_sq_idx(u8 flex_sq_idx);

/*!
 * @brief This function unmaps Flexible CQ index.
 *
 * @param flex_cq_idx	resource index. Range 0 to 65
 *
 * @return	None.
 */
extern void nvmet_unmap_flex_cq_idx(u8 flex_cq_idx);

/*!
 * @brief This function unmaps Flexible MSIX index.
 *
 * @param flex_mx_idx	resource index. Range 0 to 65
 *
 * @return	None.
 */
extern bool hal_nvmet_check_vf_IV_Valid(u8 vector);
extern void nvmet_unmap_flex_vi_idx(u8 flex_mx_idx);
extern void HalNvmeClearNssro(void);
#if (PI_FW_UPDATE == mENABLE)
extern void nvmet_pi_start_cmd_suspend(req_t *req);
extern void nvmet_pi_suspend_timer(void *data);
#if (PI_FW_UPDATE_REFLOW == mENABLE)	//20210503-Eddie
extern void nvmet_pi_suspend_fwdl_timer(void *data);
extern void nvmet_pi_suspend_fwca_timer(void *data);
#endif
extern void nvmet_enable_fetched_cmd_pi(void);
#endif
#if (WB_RESUME == ENABLE)
extern void nvmet_wb_init(void);
#endif

extern bool nvme_hal_check_pending_cmd_by_sq(u32 flex_sqid);

extern u8 evt_cmd_done;		///< command transfer done event
extern u8 evt_shutdown;		///< controller shutdown event
extern u8 evt_bootpart_rd;		///< boot partition read event
extern u8 evt_aer_out;		///< async event request response handled event
extern u8 evt_abort_cmd_done;		///< abort command done event
extern u8 evt_fe_gc_resume;		///<
/*! @} */
