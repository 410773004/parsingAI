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
 * @brief Core supports for HAL layer
 *
 * \addtogroup hal
 * \defgroup ctrlr controller
 * \ingroup hal
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "nvme_spec.h"
#include "nvme_cfg.h"
#include "nvme_reg_access.h"
#include "hal_nvme.h"
#include "dma.h"
#include "cmd_proc.h"
#include "port_ctrl.h"
#include "pmu.h"
#include "bf_mgr.h"
#include "event.h"
#include "assert.h"
#include "req.h"
#include "rainier_soc.h"
#include "vic_id.h"
#include "misc.h"
#include "console.h"
#include "hmb.h"
#include "nvmet.h"
#include "nvme_decoder.h"
#include "pcie_core_register.h"
#include "fc_export.h"
#if (CO_SUPPORT_READ_AHEAD == TRUE)
#include "ra.h"
#endif
#if (MRR_EQUAL_MPS == ENABLE)
#include "pcie_wrapper_register.h"
#endif
/*! \cond PRIVATE */
#define __FILEID__ ctrlr
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define IO_CQUEUE_MAX_DEPTH 128							  ///< controller I/O completion queue depth
#define IQ_CQUEUE_MAX_DEPTH_MSK (IO_CQUEUE_MAX_DEPTH - 1) ///< I/O completion queue depth mask

#define DMA_QUEUE_SHF 6 ///< DMA queue shift

#define QUIRK_INTERRUPTS (ADM_SQ_TAIL_UPDT_MASK | IO_SQ_TAIL_UPDT_MASK | NVM_CQ_SEND_DONE_MASK | NVM_CMD_FETCH_DONE_MASK | ANY_FLR_ACTIVE_MASK)
#if defined(SRIOV_SUPPORT)
#define MASKED_INTERRUPTS (FW_CQ_REQ_DONE_MASK | QUIRK_INTERRUPTS)
#else
#define MASKED_INTERRUPTS (ANY_VF_ADM_CMD_FETCHED_MASK | FW_CQ_REQ_DONE_MASK | QUIRK_INTERRUPTS)
#endif

/* Using BIT23 to indicate whether the command is in ASIC queue */
#define NVME_CMD_IN_BTCM (BIT(23))						   ///< indicate command entry buffer is in BTCM
#define NVME_CMD_FID(fid) (fid << 24)					   ///< bit31 ~ bit24 is command fetch id
#define NVME_CMD_OUT_BTCM(cmd) ((u32)cmd) & ~(BIT(23))	   ///< take BTCM bit off from command entry
#define NVME_CMD_OUT_FID(cmd) (((u32)cmd) & (BIT(24) - 1)) ///< take fetch id off from command entry
#define FUNCTION_AER_ADDR(index) (SQ1_H_BASE_ADDR_L + ((index) << 3)) ///<addr for save aer when warm boot

#if CPU_ID == 1
#define NVME_INT_MASK NVME_INT_MASK_CPU1
#define NVME_MASKED_INT_STATUS NVME_MASKED_INT_STATUS_CPU1
#elif CPU_ID == 2
#define NVME_INT_MASK NVME_INT_MASK_CPU2
#define NVME_MASKED_INT_STATUS NVME_MASKED_INT_STATUS_CPU2
#elif CPU_ID == 3
#define NVME_INT_MASK NVME_INT_MASK_CPU3
#define NVME_MASKED_INT_STATUS NVME_MASKED_INT_STATUS_CPU3
#elif CPU_ID == 4
#define NVME_INT_MASK NVME_INT_MASK_CPU4
#define NVME_MASKED_INT_STATUS NVME_MASKED_INT_STATUS_CPU4
#endif
extern void enable_corr_uncorr_isr(void);
extern void disable_corr_uncorr_isr(void);
extern volatile is_IOQ* is_IOQ_ever_create_or_not;
fast_data bool Nvme_Rset_flag = false;
/*
 *ENABLE:When there are both AER_event_isr and get_log_page, handle AER_event_isr first
 *fix IOL clear AER fail. 2024/5/6 shengbin yang
 */
#define NVME_ISR_CHECK_AER_FIRST ENABLE
extern volatile bool shr_shutdownflag;
extern u32 bm_entry_pending_cnt;
fast_data_zi bool move_gc_resume;

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

/*!
 * @brief controller SQ definition
 */
typedef struct _nvmet_sq_t
{
	u16 reg_cmd_fetch_pnter;

	volatile u16 cmd_fetch_rptr;
	/* 0's based and (qsize & (qsize - 1) == 0) */
	u16 qsize;
} nvmet_sq_t;

/*!
 * @brief controller CQ definition
 */
typedef struct _nvme_cq_t
{
	volatile u16 wptr;
	/* 0's based and (qsize & (qsize - 1) == 0) */
	u16 qsize;
	struct nvme_cpl *base;
} nvmet_cq_t;

/*!
 * @brief insufficient CQ completion entries
 */
typedef struct
{
	struct list_head entry;
	u32 cqid;
	u32 cmd_id;
	struct nvme_cpl cpl;
} cpl_ppnd_t;

/*!
 * @brief DMA request definition
 */
typedef struct
{
	u64 prp;	   /* Host PRP address */
	u32 sram_addr; /* SRAM address, 21 bit */

	union
	{
		u32 all;
		struct
		{
			u32 dir : 1;
			u32 hmbc : 1;
			u32 rvsd : 14;
			u32 len : 16;
		} b;
	} dw3;
} PACKED dma_req_t;

/*!
 * @brief DMA queue definition
 */
struct dma_queue_t
{
	u8 wptr;		 /* request write pointer, update by FW */
	u8 rptr;		 /* request read pointer to indicate HW position */
	u16 qsize;		 /* 0's based DMA queue size */
	dma_req_t *reqs; /* DMA request */

	/* register for queue access */
	u16 ctrlr_sts;
	u16 sram_base;
	u16 queue_ptrs;
};

/*!
 * @brief dma completion callback definition
 *
 * for anyone to use dma to transfer data to host
 */
typedef struct
{
	void *hcmd;								  ///< caller context
	void (*callback)(void *hcmd, bool error); ///< completion callback
} dma_queue_opaque_t;

/*!
 * @brief rainier NVME operation modes.
 */
typedef enum
{
	NVME_OP_MODE_BASIC = 0x0,
	NVME_OP_MODE_SRIOV = 0x1,
	NVME_OP_MODE_NVM_SET = 0x2,
	NVME_OP_MODE_SHASTA = 0x7,
} nvme_op_mode_t;
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
/* SRIOV: 1 PF + max 32 VF Admin Command Queue base.
 * Note: Number of VFs are configurable */
fast_data_ni u32 cmd_base[SRIOV_PF_VF_PER_CTRLR][IO_SQUEUE_MAX_DEPTH] __attribute__((aligned(32))); ///< nvme command base array for command fetching
#elif defined(PROGRAMMER)
fast_data_ni u32 cmd_base[2][IO_SQUEUE_MAX_DEPTH] __attribute__((aligned(32))); ///< nvme command base array for command fetching
#else
fast_data_ni u32 cmd_base[1][IO_SQUEUE_MAX_DEPTH] __attribute__((aligned(32))); ///< nvme command base array for command fetching
#endif
static fast_data_zi struct timer_list cq_timer; ///< controller ppnd CQ timer

fast_data_ni struct nvme_registers nvmet_regs;	///< nvme register
#if defined(SRIOV_SUPPORT)
fast_data_zi static struct nvme_vf_registers nvmet_vf_regs[SRIOV_VF_PER_CTRLR]; ///< nvme register
#endif

fast_data u8 evt_cmd_done = 0xFF;		   ///< command done event to complete command
fast_data u8 evt_shutdown = 0xFF;		   ///< shutdown event
fast_data u8 evt_bootpart_rd = 0xFF;	   ///< boot partition read event
fast_data u8 evt_aer_in = 0xFF;			   ///< aer request in event
fast_data u8 evt_aer_out = 0xFF;		   ///< aer request out event
fast_data u8 evt_hmb_cmd_done = 0xFF;	   ///< hmb command done event
fast_data u8 evt_hmb_auto_cmd_done = 0xFF; ///< hmb auto lookup response event
fast_data u8 evt_abort_cmd_done = 0xFF;	   ///< abort command done event
fast_data u8 evt_fe_gc_resume = 0xFF;
#if CO_SUPPORT_PANIC_DEGRADED_MODE
fast_data u8 evt_assert_rst = 4;
#endif
fast_data_ni static pool_t nvme_cmd_pool;  ///< controller command pool

fast_data_ni struct nvme_cmd nvme_cmd_base[SRAM_NVME_CMD_CNT] __attribute__((aligned(64)));	   ///< controller command entry queue
fast_data_ni struct nvme_cpl nvme_cmpl_base[IO_CQUEUE_MAX_DEPTH] __attribute__((aligned(32))); ///< controller completion entry queue

fast_data_zi nvmet_sq_t sq_base[NVMET_RESOURCES_FLEXIBLE_TOTAL + 1] __attribute__((aligned(32))); ///< controller SQ
fast_data_zi nvmet_cq_t cq_base[1];																  ///< controller CQ  /* Rainier shares completion queue */

fast_data_zi static u32 cpl_ppnd_cmd_id;		///< command id of completion entry postponed
fast_data_zi static u32 cpl_ppnd_cnt;			///< number of completion entry postponed
fast_data static LIST_HEAD(cpl_ppnd_wl); 		///< completion entry waiting list
fast_data_zi bool _fg_wb_reboot = false;	//20210426-Eddie

/* DMA Queue 0 for READ and 1 for WRITE */
static fast_data_zi bool dma_queue_initialized = false;										   ///< indicate if dma queue was initialized
static slow_data_ni dma_req_t dma_reqs[1 << (DMA_QUEUE_SHF + 1)] __attribute__((aligned(32))); ///< dma requests buffer
static slow_data_zi struct dma_queue_t dma_queues[2];										   ///< host dma read/write queue,
static slow_data_ni dma_queue_opaque_t dma_queues_opaque[2][1 << DMA_QUEUE_SHF];			   ///< host dma caller context
static fast_data nvmet_cq_t *cq = &cq_base[0];												   ///< hardware cq, fast reference due to all CQ sharing
extern bool cc_en_set;
share_data volatile bool _fg_warm_boot = false; //20201028-Eddie, warm boot flag to part from normal power-on procedure
share_data volatile bool _fg_fwupgrade_stopPTRD = false;
extern fast_data  struct list_head nvme_sq_del_reqs;	/// nvme sq dele
/*#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
u8 ddr_code telemetry_update_ctrlr_singnal = 0;
#endif*/
//-----------------------------------------------------------------------------
//  Private function proto-type definitions:
//-----------------------------------------------------------------------------
/*!
 * @brief	enable CQ request done interrupt
 *
 * If CQ was full, no any more CQ entry could be inserted to, cq_req_done_isr
 * will be enabled, then we could insert pending CQ entry to CQ again.
 *
 * @return	None
 */
static void nvmet_enable_cq_req_done_isr(void);

/*!
 * @brief	disable CQ request done interrupt
 *
 * @return	None
 */
static void nvmet_disable_cq_req_done_isr(void);
static void nvmet_disable_dma_done_irq(void);
static void nvmet_enable_dma_done_irq(void);
static bool nvmet_dma_queue_done(u32 dma_queue_id);
static void nvmet_cc_en_reset(void);
static void nvmet_csts_rdy(bool rdy); //Johnny 20210710
static void nvmet_csts_shst(enum nvme_shst_value shst);

extern bool nvmet_rst_req_flush(void);
//-----------------------------------------------------------------------------
//  Function Definitions
//-----------------------------------------------------------------------------
#if (MRR_EQUAL_MPS == ENABLE)
static inline u32 pcie_wrap_readl(u32 reg)
{
	return readl((void *)(PCIE_WRAP_BASE + reg));
}
#endif
/*!
 * @brief	cast u32 command entry to nvme command pointer
 *
 * @param	cmd	nvme command entry from command base array
 *
 * @return	nvme command pointer
 */
inline struct nvme_cmd *nvme_cmd_cast(u32 cmd)
{
	/*! take off BTCM bit */
	cmd = NVME_CMD_OUT_BTCM(cmd);

	/*! take off idx */
	cmd = NVME_CMD_OUT_FID(cmd);
	if (cmd == 0)
		return NULL;

	return (struct nvme_cmd *)(dma_to_btcm(cmd));
}

/*!
 * @brief	pop nvme command from pool and make a command entry of command base array
 *
 * @param	str	debug message
 *
 * @return	nvme command entry of command base array
 */
inline fast_code u32 nvme_cmd_get(const char *str)
{
	struct nvme_cmd *cmd = pool_get_ex(&nvme_cmd_pool);
	u32 idx;

	if (cmd == NULL)
	{
		nvme_hal_trace(LOG_ERR, 0xaf7c, "%s: out of nvme cmd", str);
		return 0;
	}

	ctrlr->free_nvme_cmds--;
	idx = cmd - &nvme_cmd_base[0];
	/*! bit(23) indicate this command was from BTCM */
	/*! bit(24~31) as fetch id of nvme command, it's used for HMB auto lookup */
	/*! then we could find nvme command pointer in auto lookup handler */
	return btcm_to_dma((void *)cmd) | NVME_CMD_IN_BTCM | NVME_CMD_FID(idx);
}

/*!
 * @brief	registers READ access
 *
 * @param	reg	which register to access
 *
 * @return	register value
 */
static u32 inline nvme_readl(int reg)
{
	return readl((void *)(NVME_BASE + reg));
}

/*!
 * @brief	registers WRITE access
 *
 * @param	val	the value to update
 * @param	reg	which register to access
 *
 * @return	None
 */
static void inline nvme_writel(u32 data, int reg)
{
	writel(data, (void *)(NVME_BASE + reg));
}

/*!
 * @brief	VF registers READ access
 *
 * @param	fun	VF function ID. 0 for VF1, 1 for VF2 and so on.
 * @param	reg	which register to access
 *
 * @return	register value
 */
static u32 inline nvme_vf_readl(int fun, int reg)
{
	return readl((void *)(NVME_VF_BASE + (0x20 * fun) + reg));
}

/*!
 * @brief	VF registers WRITE access
 *
 * @param	data the value to update
 * @param	fun	VF function ID. 0 for VF1, 1 for VF2 and so on.
 * @param	reg	which register to access
 *
 * @return	None.
 */
static void inline nvme_vf_writel(u32 data, int fun, int reg)
{
	writel(data, (void *)(NVME_VF_BASE + (0x20 * fun) + reg));
}

/*!
 * @brief	nvme check io sq pending
 *
 * @param	None.
 *
 * @return	false: io sq pending
 * @return	true: io sq not pending
 */
ddr_code bool hal_nvmet_check_io_sq_pending(void)
{
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	bool io_pend;
	io_pend = ctrlr_sts.b.nrm_mode_io_cmd_pending;
	if(io_pend == 0) {
		return true;
	}
	return false;
}

/*!
 * @brief	set sq controls command arbitration to HW
 *
 * @param	hpw High Priority Weight
 * @param	mpw Medium Priority Weight
 * @param	lpw Low Priority Weight
 *
 * @return	None
 */
fast_code void hal_nvmet_set_sq_arbitration(u8 hpw, u8 mpw, u8 lpw)
{
	if (hpw < 0xFF)
		hpw++;

	if (mpw < 0xFF)
		mpw++;

	if (lpw < 0xFF)
		lpw++;

	arb_priority_weight_t weight = {
		.all = nvme_readl(ARB_PRIORITY_WEIGHT),
	};
	weight.b.low_priority_weight = lpw;
	weight.b.med_priority_weight = mpw;
	weight.b.high_priority_weight = hpw;
	weight.b.rsvd_24 = 0;
	nvme_writel(weight.all, ARB_PRIORITY_WEIGHT);

	arbitration_control_sts_t arb_ctrls = {
		.all = nvme_readl(ARBITRATION_CONTROL_STS),
	};
	arb_ctrls.b.arbitration_enable = 1;
	nvme_writel(arb_ctrls.all, ARBITRATION_CONTROL_STS);
}

#if defined(SRIOV_SUPPORT)
/*!
 * @brief This function get the Flexible SQ index of host sq for a function.
 *
 * @param fun_id	Function index. 0 to 32. 0 PF0, 1 VF1, 2 VF2 and so on.
 * @param hst_sqid	Host SQ ID. 0 for ASQ and >0 for IOSQ
 *
 * @return	Flexible SQ Index of host sq for the given function.
 */
fast_data u8 nvmet_get_flex_sq_map_idx(u8 fun_id, u8 hst_sqid)
{
	u8 flex_sq_idx;

	if (fun_id == 0x00)
	{
		return hst_sqid;
	}

	flex_sq_idx = ctrlr->sr_iov->vq_flex_strt_idx[fun_id - 1];
	nvme_hal_trace(LOG_ALW, 0xbc72, "SQ resources: cntl id(%d) hst_sqid(%d) flex_sq_idx(%d)", fun_id, hst_sqid, (flex_sq_idx + hst_sqid));
	return (flex_sq_idx + hst_sqid);
}

/*!
 * @brief This function get the Flexible CQ index of host cq for a function.
 *
 * @param fun_id	Function index. 0 for PF and 1 to 32 for VF
 * @param hst_cqid	Host CQ ID. 0 for ASQ and >1 for IOCQ
 *
 * @return	Flexible CQ Index of host cq for the given function.
 */
fast_data u8 nvmet_get_flex_cq_map_idx(u8 fun_id, u8 hst_cqid)
{
	u8 flex_cq_idx;

	if (fun_id == 0x00)
	{
		/* PF0 resource */
		nvme_hal_trace(LOG_DEBUG, 0x2b60, "cntl id(%d) hst_cqid(%d)", fun_id, hst_cqid);
		return hst_cqid;
	}
	flex_cq_idx = ctrlr->sr_iov->vq_flex_strt_idx[fun_id - 1];
	//nvme_hal_trace(LOG_DEBUG, 0, "CQ resources: cntl id(%d) hst_cqid(%d) flex_cq_idx(%d)", fun_id, hst_cqid, (flex_cq_idx + hst_cqid));
	return (flex_cq_idx + hst_cqid);
}

/*!
 * @brief This function get the Flexible MSIX index of host msix for a function.
 *
 * @param fun_id	Function index. 0 to 32. 0 PF0, 1 VF1, 2 VF2 and so on.
 * @param hst_msix	Host msix ID. 0 for ASQ and >1 for IOCQ
 *
 * @return	Flexible MSIX Index of host msix for the given function.
 */
fast_data u8 nvmet_get_flex_vi_map_idx(u8 fun_id, u8 hst_msix)
{
	u8 flex_mx_idx;

	if (fun_id == 0x00)
	{
		/* PF0 msix */
		nvme_hal_trace(LOG_DEBUG, 0xba07, "cntl id(%d) msix-id(%d)", fun_id, hst_msix);
		return hst_msix;
	}

	flex_mx_idx = ctrlr->sr_iov->vi_flex_strt_idx[fun_id - 1];

	return (flex_mx_idx + hst_msix);
}

/*!
 * @brief This function maps Flexible SQ index to host SQ index for a function.
 *
 * @param flex_sq_idx	Flexible SQ index. Range 0 to 65
 * @param fun_id	Function ID. 0 for PF0; 1 to 32 for VF
 * @param hst_sq_id	Host SQ index for this function. 0 for Admin Q. >0 for IOSQ
 *
 * @return	None.
 */
fast_data void nvmet_map_flex_sq_2_fun_sq(u8 flex_sq_idx, u8 fun_id, u8 hst_sq_id)
{
	flex_sq0_mapping_t sq;

	sq.all = flex_map_readl(FLEX_SQ0_MAPPING + (flex_sq_idx << 2));
	sq.b.sq_id = hst_sq_id;
	sq.b.controller_id = fun_id;
	sq.b.mapping_valid = 1;
	flex_map_writel(sq.all, FLEX_SQ0_MAPPING + (flex_sq_idx << 2));
}

/*!
 * @brief This function maps Flexible CQ index to host CQ index for a function.
 *
 * @param flex_cq_idx	Flexible CQ index. Range 0 to 65
 * @param fun_id	Function ID. 0 for PF0; 1 to 32 for VF
 * @param hst_cq_id	Host CQ index for this function. 0 for Admin Q. >0 for IOCQ
 *
 * @return	None.
 */
fast_data void nvmet_map_flex_cq_2_fun_cq(u8 flex_cq_idx, u8 fun_id, u8 hst_cq_id)
{
	flex_cq0_mapping_t cq;

	cq.all = flex_map_readl(FLEX_CQ0_MAPPING + (flex_cq_idx << 2));
	cq.b.cq_id = hst_cq_id;
	cq.b.controller_id_300 = fun_id;
	cq.b.mapping_valid_300 = 1;
	flex_map_writel(cq.all, FLEX_CQ0_MAPPING + (flex_cq_idx << 2));
}

/*!
 * @brief This function maps Flexible msix index to host msix index for a function.
 *
 * @param flex_mx_idx	Flexible msix index. Range 0 to 65
 * @param fun_id	Function ID. 0 for PF0; 1 to 32 for VF
 * @param hst_mx_id	Host msix index for this function.
 *
 * @return	None.
 */
fast_data void nvmet_map_flex_vi_2_fun_vi(u8 flex_mx_idx, u8 fun_id, u8 hst_mx_id)
{
	flex_mx0_mapping_t mx;

	mx.all = flex_map_readl(FLEX_MX0_MAPPING + (flex_mx_idx << 2));
	mx.b.mx_id = hst_mx_id;
	mx.b.controller_id_600 = fun_id;
	mx.b.mapping_valid_600 = 1;
	flex_map_writel(mx.all, FLEX_MX0_MAPPING + (flex_mx_idx << 2));
}

/*!
 * @brief This function unmaps Flexible SQ index.
 *
 * @param flex_sq_idx	resource index. Range 0 to 65
 *
 * @return	None.
 */
fast_data void nvmet_unmap_flex_sq_idx(u8 flex_sq_idx)
{
	flex_sq0_mapping_t sq;

	sq.all = flex_map_readl(FLEX_SQ0_MAPPING + (flex_sq_idx << 2));
	sq.b.sq_id = 0;
	sq.b.controller_id = 0;
	sq.b.mapping_valid = 0;
	flex_map_writel(sq.all, FLEX_SQ0_MAPPING + (flex_sq_idx << 2));
}

/*!
 * @brief This function unmaps Flexible CQ index.
 *
 * @param flex_cq_idx	resource index. Range 0 to 65
 *
 * @return	None.
 */
fast_data void nvmet_unmap_flex_cq_idx(u8 flex_cq_idx)
{
	flex_cq0_mapping_t cq;

	cq.all = flex_map_readl(FLEX_CQ0_MAPPING + (flex_cq_idx << 2));
	cq.b.cq_id = 0;
	cq.b.controller_id_300 = 0;
	cq.b.mapping_valid_300 = 0;
	flex_map_writel(cq.all, FLEX_CQ0_MAPPING + (flex_cq_idx << 2));
}

/*!
 * @brief This function unmaps Flexible MSIX index.
 *
 * @param flex_mx_idx	resource index. Range 0 to 65
 *
 * @return	None.
 */
fast_data void nvmet_unmap_flex_vi_idx(u8 flex_mx_idx)
{
	flex_mx0_mapping_t mx;

	mx.all = flex_map_readl(FLEX_MX0_MAPPING + (flex_mx_idx << 2));
	mx.b.mx_id = 0;
	mx.b.controller_id_600 = 0;
	mx.b.mapping_valid_600 = 0;
	flex_map_writel(mx.all, FLEX_MX0_MAPPING + (flex_mx_idx << 2));
}

/*!
 * @brief This will get the function ID which triggered shutdown interrupt.
 *
 * @param None.res_idx	resource index. Range 0 to 65
 *
 * @return function ID: 0 for PF, 1 to 32 for VFs.
 */
fast_code u8 nvmet_get_disable_bit_triggered_fun(void)
{
	/* Check PF0 */
	pf_ctrlr_dis_act_done_t pf_dis = {
		.all = nvme_readl(PF_CTRLR_DIS_ACT_DONE),
	};
	if (pf_dis.b.pf_controller_disable_active == 1)
		return 0;

	/* Check VF1 to VF32 */
	u8 i;
	vf_ctrlr_dis_act_t vf_dis = {
		.all = nvme_readl(VF_CTRLR_DIS_ACT),
	};
	for (i = 0; i < MAX_SEC_VF; i++)
	{
		if (vf_dis.all & (1 << i))
		{
			i++;
			return i;
		}
	}
	return 0xFF;
}

fast_code void nvmet_clear_fun_disable_bit(u8 fid)
{
	if (fid == 0)
	{
		/* Clear PF0 */
		pf_ctrlr_dis_act_done_t pf_dis = {
			.all = nvme_readl(PF_CTRLR_DIS_ACT_DONE),
		};
		pf_dis.b.pf_controller_disable_active = 1;
		nvme_writel(pf_dis.all, PF_CTRLR_DIS_ACT_DONE);
	}
	else
	{
		/* Check VF1 to VF32 */
		vf_ctrlr_dis_act_t vf_act = {
			.all = nvme_readl(VF_CTRLR_DIS_ACT),
		};
		vf_act.all = (1 << (fid - 1));
		nvme_writel(vf_act.all, VF_CTRLR_DIS_ACT);
	}
}
#endif

fast_data bool nvmet_vf_is_cc_en(u16 cntlid)
{
	controller_confg_t cc;
	/* vf_idx: 0 for VF1, 1 for VF2, ...., 32 for VF32 */
	u16 vf_idx = cntlid - 1;

	cc.all = vf_readl(CONTROLLER_CONFG + (vf_idx << 5));

	return (cc.b.cc_en != 0);
}

/*!
 * @brief This function initialize the Flexible resource for admin Queues (ASQ/ACQ)
 *
 * @param cntlid	nvme vf controller id
 *			 - SRIOV mode: 0 for PF, 1 for VF1, 2 for VF2 .. 32 for VF32
 *			 - NORMAL or other modes: 0 for PF
 *
 * @return		None
 *
 * @note
 *
 */
void nvmet_init_vf_aqa_attrs(u16 cntlid)
{
	adm_cmd_sram_base_func0_t aqb;
	adm_cmd_fetch_ptrs_func0_t aqp;
	u16 vf_offset = cntlid;
	u16 i;

	/* Initialize VFn Admin command buffer base address */
#if defined(SRIOV_SUPPORT)
	for (i = 0; i < IO_SQUEUE_MAX_DEPTH; i++)
		cmd_base[cntlid][i] = nvme_cmd_get(__FUNCTION__);
#endif

	aqb.b.adm_cmd_sram_base = btcm_to_dma(cmd_base[cntlid]);
	aqb.b.addr_is_for_dtcm = 1;
	aqb.b.adm_cmd_fetch_max_sz = IO_SQUEUE_MAX_DEPTH - 1;
	aqp.all = flex_misc_readl(ADM_CMD_FETCH_PTRS_FUNC0 + (vf_offset << 3));
	aqp.b.adm_cmd_fetch_rptr = 0;

	flex_misc_writel(aqb.all, ADM_CMD_SRAM_BASE_FUNC0 + (vf_offset << 3));
	flex_misc_writel(aqp.all, ADM_CMD_FETCH_PTRS_FUNC0 + (vf_offset << 3));

	/*sq_base[0].reg_cmd_fetch_pnter = SQ0_CMD_FETCH_PTRS;
	sq_base[0].qsize = aqa.bits.asqs;
	sq_base[0].cmd_fetch_rptr = 0;
	nvme_writel(sq_base[0].cmd_fetch_rptr, sq_base[0].reg_cmd_fetch_pnter);*/

#if defined(SRIOV_SUPPORT)
	/* get and map admin q resources */
	i = nvmet_get_flex_sq_map_idx(cntlid, 0);
#else
	/* get and map admin q resources */
	i = 0;
#endif

	/* Initialize SQ/CQ Doorbell */
	flex_sq0_db_reg_t sqdb_ctrls = {
		.all = flex_dbl_readl(FLEX_SQ0_DB_REG + (i << 2)),
	};
	sqdb_ctrls.b.sq_tail_ptr = 0;
	sqdb_ctrls.b.sq_head_ptr = 0;
	flex_dbl_writel(sqdb_ctrls.all, FLEX_SQ0_DB_REG + (i << 2));

	/* Initialize command fetch related registers */
	flex_sq_ctrl_sts0_t sq_ctrl = {
		.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (i << 2)),
	};
	sq_ctrl.b.sq_arbitration_en = 1;
	sq_ctrl.b.cq_id_mapped = i;
	sq_ctrl.b.admin_queue = 1;
	flex_misc_writel(sq_ctrl.all, FLEX_SQ_CTRL_STS0 + (i << 2));

#if defined(SRIOV_SUPPORT)
	i = nvmet_get_flex_cq_map_idx(cntlid, 0);
#endif

	flex_cq0_db_reg_t cqdb_ctrls = {
		.all = flex_dbl_readl(FLEX_CQ0_DB_REG + (i << 2)),
	};
	cqdb_ctrls.b.cq_head_ptr = 0;
	cqdb_ctrls.b.cq_tail_ptr = 0;
	flex_dbl_writel(cqdb_ctrls.all, FLEX_CQ0_DB_REG + (i << 2));

	hal_nvmet_set_sq_arbitration(0, 0, 0);

	/* Enable Command Fetch */
	if (vf_offset == 0)
	{
		adm_cmd_fetch_en_pf_t en_pf;
		en_pf.all = flex_misc_readl(ADM_CMD_FETCH_EN_PF);
		en_pf.b.adm_cmd_fetch_enable |= BIT(vf_offset);
		flex_misc_writel(en_pf.b.adm_cmd_fetch_enable, ADM_CMD_FETCH_EN_PF);
	}
	else
	{
		adm_cmd_fetch_en_vf_t en_vf;
		en_vf.all = flex_misc_readl(ADM_CMD_FETCH_EN_VF);
		en_vf.b.adm_cmd_fetch_enable_914 |= BIT(vf_offset - 1);
		flex_misc_writel(en_vf.b.adm_cmd_fetch_enable_914, ADM_CMD_FETCH_EN_VF);
	}

	/* Set MSI(X), review MC.MME or MXC.TS if any */
	flex_cq_ctrl_sts0_t cq_ctrl = {
		.all = flex_misc_readl(FLEX_CQ_CTRL_STS0 + (i << 2)),
	};
	/* msix vector mapping: 1 for VF1, 2 for VF2, .. 32 for VF32 */
	cq_ctrl.b.msix_vect_mapped = i;
	cq_ctrl.b.cq_phase_bit = 1;
	cq_ctrl.b.cq_interrupt_en = 1;
	flex_misc_writel(cq_ctrl.all, FLEX_CQ_CTRL_STS0 + (i << 2));
}
/*!
 * @brief	This function initialize the Flexible resource for admin Queues (ASQ/ACQ)
 *
 * @param	cntlid	nvme vf controller id
 *			 - SRIOV mode: 0 for PF, 1 for VF1, 2 for VF2 .. 32 for VF32
 *			 - NORMAL or other modes: 0 for PF
 *
 * @param	refill	fill the address to the hardware queue
 *
 * @return	None
 *
 */

fast_code struct nvme_cmd *hal_nvmet_get_admin_sq_cmd(u16 cntlid, bool refill)
{
	struct nvme_cmd *cmd = NULL;
	adm_cmd_fetch_ptrs_func0_t aqp;
	u16 vf_offset = cntlid;

	aqp.all = flex_misc_readl(ADM_CMD_FETCH_PTRS_FUNC0 + (vf_offset * 8));

	if (aqp.b.adm_cmd_fetch_wptr != aqp.b.adm_cmd_fetch_rptr)
	{
		cmd = nvme_cmd_cast(cmd_base[cntlid][aqp.b.adm_cmd_fetch_rptr]);

		nvme_hal_trace(LOG_DEBUG, 0x1a02, "cmd(%p) cntl id(%d) opcode 0x%x, cid(%d) SQ rptr/wptr(%d/%d)",
					   cmd, cntlid, cmd->opc, cmd->cid, aqp.b.adm_cmd_fetch_rptr, aqp.b.adm_cmd_fetch_wptr);

		if (refill)
			cmd_base[cntlid][aqp.b.adm_cmd_fetch_rptr] = nvme_cmd_get(__FUNCTION__);
		else
			cmd_base[cntlid][aqp.b.adm_cmd_fetch_rptr] = 0;

		aqp.b.adm_cmd_fetch_rptr = (aqp.b.adm_cmd_fetch_rptr + 1) & (IO_SQUEUE_MAX_DEPTH - 1);
		flex_misc_writel(aqp.all, ADM_CMD_FETCH_PTRS_FUNC0 + (vf_offset * 8));
	}

	return cmd;
}

/*!
 * @brief config interrupt Coalescing for noramal mode
 *
 * @param vector	cd
 *
 * @return		not used
 */
ddr_code void hal_nvmet_set_flex_int_calsc(u16 vector, bool cd)
{
	flex_mx_ctrl_sts0_t fmcst;
	fmcst.all = flex_misc_readl(FLEX_MX_CTRL_STS0 + (vector << 2));

	if (cd)
		fmcst.b.int_calsc_en = 0;
	else
		fmcst.b.int_calsc_en = 1;
	flex_misc_writel(fmcst.all, FLEX_MX_CTRL_STS0 + (vector << 2));
}

ddr_code bool hal_nvmet_check_vf_IV_Valid(u8 vector)
{
    u8 cq_res;
    flex_cq_ctrl_sts0_t cq_ctrl;

    for (cq_res = 0 ; cq_res < ctrlr->max_qid ; cq_res++)
    {
        cq_ctrl.all = flex_misc_readl(FLEX_CQ_CTRL_STS0 + (cq_res << 2));

        if (cq_ctrl.b.msix_vect_mapped == vector)
        {
            return true;
        }
    }
    return false;
}
/*!
 * @brief	create nvme cq and update msi-x register
 *
 * @param	vector	cd
 *
 * @return	not used
 */

fast_code bool hal_nvmet_create_vf_cq(u16 cntlid, u16 cqid, u16 qsize,
									  u16 vector, u16 ien, u16 pc, u64 prp1)
{
	u16 cq_res;

	//if (cqid == 0)
	//panic("hal_nvmet_create_cq: not for ACQ\n");

	if (qsize > nvmet_regs.cap.bits.mqes)
		return false;

	nvme_hal_trace(LOG_DEBUG, 0x6f45, "create cntlid (%d) CQ (%d) Qdepth (%d)", cntlid, cqid, qsize);

#if defined(SRIOV_SUPPORT)
	/* VF function use flexible resources */
	cq_res = nvmet_get_flex_cq_map_idx(cntlid, cqid);
	if (cq_res == 0xFF)
	{
		/* VQ resources not allocated using virtual mgmt cmds */
		nvme_hal_trace(LOG_ERR, 0xb69c, "Q resources not allocated");
		return false;
	}
#else
	cq_res = cqid;
#endif
	flex_addr_writel(prp1, FLEX_CQ0_H_BASE_ADDR_L + (cq_res << 3));
	flex_addr_writel(prp1 >> 32, FLEX_CQ0_H_BASE_ADDR_H + (cq_res << 3));

	/* Configure completion queue using host setting */
	flex_sqcq0_size_t q_size = {
		.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (cq_res << 2)),
	};
	q_size.b.cq_size = qsize;
	flex_dbl_writel(q_size.all, FLEX_SQCQ0_SIZE + (cq_res << 2));

	/* Set MSI(X), review MC.MME or MXC.TS if any */
	flex_cq_ctrl_sts0_t cq_ctrl = {
		.all = flex_misc_readl(FLEX_CQ_CTRL_STS0 + (cq_res << 2)),
	};
#if defined(SRIOV_SUPPORT)
	/* flexible msix resource index */
	vector = nvmet_get_flex_vi_map_idx(cntlid, vector);
#endif
	cq_ctrl.b.msix_vect_mapped = vector;
	cq_ctrl.b.cq_phase_bit = 1;
	flex_misc_writel(cq_ctrl.all, FLEX_CQ_CTRL_STS0 + (cq_res << 2));

	/* MSI(X) calsc shall be enabled by default */
	flex_mx_ctrl_sts0_t mx_ctrl = {
		.all = flex_misc_readl(FLEX_MX_CTRL_STS0 + (vector << 2)),
	};
	mx_ctrl.b.int_calsc_en = 1;
	flex_misc_writel(mx_ctrl.all, +FLEX_MX_CTRL_STS0 + (vector << 2));

	/* re-initilize CQ Doorbell */
	flex_cq0_db_reg_t cqdb_ctrls = {
		.all = flex_dbl_readl(FLEX_CQ0_DB_REG + (cq_res << 2)),
	};
	cqdb_ctrls.b.cq_head_ptr = 0;
	cqdb_ctrls.b.cq_tail_ptr = 0;
	flex_dbl_writel(cqdb_ctrls.all, FLEX_CQ0_DB_REG + (cq_res << 2));

	/* Enable interrupt */
	if (ien)
	{
		cq_ctrl.b.cq_interrupt_en = 1;
		flex_misc_writel(cq_ctrl.all, FLEX_CQ_CTRL_STS0 + (cq_res << 2));
	}

	/*enable Interrupt Coalescing by default*/
	hal_nvmet_set_flex_int_calsc(vector, false);
	return true;
}

/*!
 * @brief	delete cq used for SRIOV mode or normal mode
 *
 * @param	vq_mid!G flexible queue mapping ID (0 to 65)
 *
 * @return	true
 */
fast_code bool hal_nvmet_delete_vq_cq(u16 cqid)
{
	u16 cq_res = cqid;
	flex_sqcq0_size_t q_size = {
		.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (cq_res << 2)),
	};

	nvme_hal_trace(LOG_DEBUG, 0xc745, "Delete CQ (%d)", cqid);

	flex_misc_writel(0, FLEX_CQ_CTRL_STS0 + (cq_res << 2));
	flex_dbl_writel(0, FLEX_CQ0_DB_REG + (cq_res << 2));

	if (cqid != 0)
	{
		q_size.b.cq_size = 0;
		flex_dbl_writel(q_size.all, FLEX_SQCQ0_SIZE + (cq_res << 2));
		flex_addr_writel(0, FLEX_CQ0_H_BASE_ADDR_L + (cq_res << 3));
		flex_addr_writel(0, FLEX_CQ0_H_BASE_ADDR_H + (cq_res << 3));
		//Andy test IOL 5.4 20201013
		if (flagtestC != 0)
			flagtestC--;
	}

	return true;
}

/*!
 * @brief create nvme submission queue
 *
 * @param cntlid	controller id
 * @param sqid		submission queue id
 * @param cqid		completion queue id
 * @param qsize		size of queue
 * @param qsize		submission queue priority
 * @param pc		Physically Contiguous
 * @param prp1		prp1 address
 *
 * @return	true
 */
fast_code bool hal_nvmet_create_vf_sq(u16 cntlid, u16 sqid, u16 cqid,
									  u16 qsize, enum nvme_qprio qprio, u8 pc, u64 prp1)
{
	u16 sq_res;

	//sys_assert(sqid > 0);

	nvme_hal_trace(LOG_DEBUG, 0x7f20, "create cntlid (%d) SQ (%d), QDepth(%d)", cntlid, cqid, qsize);

#if defined(SRIOV_SUPPORT)
	sq_res = nvmet_get_flex_sq_map_idx(cntlid, sqid);
	if (sq_res == 0xFF)
	{
		/* VQ resource not allocated using virtual mgmt cmds */
		nvme_hal_trace(LOG_ERR, 0x78d5, "Q resources not allocated");
		return false;
	}
#else
	sq_res = sqid;
#endif

	flex_addr_writel(prp1, FLEX_SQ0_H_BASE_ADDR_L + (sq_res << 3));
	flex_addr_writel(prp1 >> 32, FLEX_SQ0_H_BASE_ADDR_H + (sq_res << 3));

	/* Set SQ size */
	flex_sqcq0_size_t q_size = {
		.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (sq_res << 2)),
	};
	q_size.b.sq_size = qsize;
	flex_dbl_writel(q_size.all, FLEX_SQCQ0_SIZE + (sq_res << 2));

	/* re-initialize SQ Doorbell */
	flex_sq0_db_reg_t sqdb_ctrls = {
		.all = flex_dbl_readl(FLEX_SQ0_DB_REG + (sq_res << 2)),
	};
	sqdb_ctrls.b.sq_head_ptr = 0;
	sqdb_ctrls.b.sq_tail_ptr = 0;
	flex_dbl_writel(sqdb_ctrls.all, FLEX_SQ0_DB_REG + (sq_res << 2));

	/* Initialize command fetch related registers */
	flex_sq_ctrl_sts0_t sq_ctrl = {
		.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (sq_res << 2)),
	};
	sq_ctrl.b.sq_arbitration_en = 1;
	sq_ctrl.b.admin_queue = 0;
#if defined(SRIOV_SUPPORT)
	/* Use flexible (internal) CQ ID */
	cqid = nvmet_get_flex_cq_map_idx(cntlid, cqid);
	sq_ctrl.b.cq_id_mapped = cqid;
#else
	sq_ctrl.b.cq_id_mapped = cqid;
#endif
	if (qprio == NVME_QPRIO_URGENT)
		sq_ctrl.b.urgent_queue = 1;
	else if (qprio == NVME_QPRIO_HIGH)
		sq_ctrl.b.high_priority_queue = 1;
	else if (qprio == NVME_QPRIO_MEDIUM)
		sq_ctrl.b.medium_priority_queue = 1;
	else if (qprio == NVME_QPRIO_ADMIN)
		sq_ctrl.b.admin_queue = 1;
	else
		sq_ctrl.b.low_priority_queue = 1;

	controller_confg_t ctrlr_cfg ={
		.all = nvme_readl(CONTROLLER_CONFG),
	};

	if (ctrlr_cfg.b.cc_ams == 0)
	{
		sq_ctrl.b.urgent_queue = 1;
		sq_ctrl.b.high_priority_queue = 0;
		sq_ctrl.b.medium_priority_queue = 0;
		sq_ctrl.b.admin_queue = 0;
		sq_ctrl.b.low_priority_queue = 0;
	}
	flex_misc_writel(sq_ctrl.all, FLEX_SQ_CTRL_STS0 + (sq_res << 2));

	/* Activate controller to fetch I/O command */
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.io_cmd_hw_fetch_en = 1;
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);

	nvmet_csts_shst(NVME_SHST_NORMAL); /* No SHST has been requested */

	return true;
}

/*!
 * @brief create nvme submission queue
 *
 * @param sqid submission queue id
 * @param fid function id
 *
 * @return	true: non zero; false: zero
 */
fast_code bool hal_nvmet_chk_cq_size(u16 sqid)
{
	u16 cqid = ctrlr->sqs[sqid]->cqid;

	flex_sqcq0_size_t q_size = {
		.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (cqid << 2)),
	};

	if (q_size.b.cq_size == 0) {
		return false;
	} else {
		return true;
	}
}

/*!
 * @brief	delete nvme submission queue
 *
 * @param	sqid	submission queue id
 *
 * @return	true
 */
fast_code bool hal_nvmet_delete_vq_sq(u16 sqid)
{
	u16 sq_res = sqid;
	flex_sqcq0_size_t q_size = {
		.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (sq_res << 2)),
	};

	nvme_hal_trace(LOG_DEBUG, 0x4997, "Delete SQ (%d)", sqid);

#if defined(SRIOV_SUPPORT)
	q_size.b.sq_size = 0;
	flex_dbl_writel(q_size.all, FLEX_SQCQ0_SIZE + (sq_res << 2));
	flex_addr_writel(0, FLEX_SQ0_H_BASE_ADDR_L + (sq_res << 3));
	flex_addr_writel(0, FLEX_SQ0_H_BASE_ADDR_H + (sq_res << 3));
#else
	/*
	 * 1. The PRP list that describes the Submission Queue
	 * may be deallocated by host software
	 *
	 * 2. After all commands submitted to the indicated SQ either completed
	 * or aborted, just return the status to Admin Completion Queue
	 */
	u16 i;
	if (sqid == 0)
	{
		nvmet_sq_t *sq = &sq_base[sqid];
		for (i = 0; i <= sq->qsize; i++)
		{
			if (cmd_base[sqid][i])
			{
				hal_nvmet_put_sq_cmd(nvme_cmd_cast(cmd_base[sqid][i]));
				cmd_base[sqid][i] = 0;
			}
		}
	}
	else
	{
		q_size.b.sq_size = 0;
		flex_dbl_writel(q_size.all, FLEX_SQCQ0_SIZE + (sq_res << 2));
		flex_addr_writel(0, FLEX_SQ0_H_BASE_ADDR_L + (sq_res << 3));
		flex_addr_writel(0, FLEX_SQ0_H_BASE_ADDR_H + (sq_res << 3));
	}
#endif

	flex_misc_writel(0, FLEX_SQ_CTRL_STS0 + (sq_res << 2));
	flex_dbl_writel(0, FLEX_SQ0_DB_REG + (sq_res << 2));

	return true;
}

#ifndef NVME_SHASTA_MODE_ENABLE
/*!
 * @brief	reset normal mode HW
 *
 * @param	None
 *
 * @return	None
 */
static void hal_nvmet_reset_normal_mode(void)
{
	u32 aqa;
	u32 asqh;
	u32 asql;
	u32 acqh;
	u32 acql;

	aqa = flex_dbl_readl(FLEX_SQCQ0_SIZE);
	asqh = flex_dbl_readl(FLEX_SQ0_H_BASE_ADDR_H);
	asql = flex_dbl_readl(FLEX_SQ0_H_BASE_ADDR_L);
	acqh = flex_dbl_readl(FLEX_CQ0_H_BASE_ADDR_H);
	acql = flex_dbl_readl(FLEX_CQ0_H_BASE_ADDR_L);

	if ((aqa == 0) && (asqh == 0) && (asql == 0) && (acqh == 0) && (acql == 0))
	{
#if !defined(SRIOV_SUPPORT)
		/* In SR-IOV mode, FW should not reset SR-IOV memory
		 * register block. If performed, it will change VF
		 * NVME register values to HW initialized values */
		nvme_writel(0xCAFEBEE8, 0x21FC); //reset normal mode
#endif
	}
}

/*!
 * @brief set Physical function max MSI-X support count
 *
 * @param msi_x_count max MSI-X count
 *
 * @return	None
 */
static void hal_nvmet_set_pf_max_msi_x(u32 msi_x_count)
{
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};

#if defined(SRIOV_SUPPORT)
	/* Set NVME_OP_MODE to SRIOV to use HW resources */
	ctrlr_sts.b.nvme_op_mode = NVME_OP_MODE_SRIOV;
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);

	/* setup SQ, CQ and VI mapping for */
	u8 i, j, k, m;
	for (i = 0, k = 0; i < SRIOV_PF_VF_PER_CTRLR; i++)
	{
		if (k < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL)
		{
			/* PF0 */
			for (j = 0; j < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL; j++)
			{
				/* res_idx, fun_id, qid */
				nvmet_map_flex_sq_2_fun_sq(k, i, j);
				nvmet_map_flex_cq_2_fun_cq(k, i, j);
				nvmet_map_flex_vi_2_fun_vi(k, i, j);
				k++;
			}
		}
		else
		{
			/* VF1 to VF32 */
			for (m = 0; m < SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC; m++)
			{
				/* res_idx, fun_id, qid */
				nvmet_map_flex_sq_2_fun_sq(k, i, m);
				nvmet_map_flex_cq_2_fun_cq(k, i, m);
				nvmet_map_flex_vi_2_fun_vi(k, i, m);
				k++;
			}
		}
	}
#else
	/* Set NVME_OP_MODE to NORMAL to use HW resources */
	ctrlr_sts.b.nvme_op_mode = NVME_OP_MODE_BASIC;
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
#endif
#if defined(FPGA)
	if (soc_cfg_reg1.b.ddr != 0x1)
		return;
#endif
#if 0//!defined(FPGA)
	if (is_pcie_clk_enable())
		pcie_set_pf_max_msi_x(msi_x_count);
	else
		nvme_hal_trace(LOG_ERR, 0x1f53, "not have pcie clock when init nvme");
#endif
}
#endif

/*!
 * @brief	rebuild sq after warm boot used for normal mode
 *
 * @param	sqid	sq id
 *
 * @return	None
 */
void hal_nvmet_rebuild_vf_sq(u16 sqid)
{
	u32 i = 0;
	//u32 val = 0;
	u32 q_ofst = 0;	/* queue offset */
	nvmet_sq_t *sq = &sq_base[sqid];

	/* Set SQ size */
	q_ofst = sqid << 3;
	flex_sqcq0_size_t q_size = {
		.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (sqid << 2)),
	};
	sq->qsize = q_size.b.sq_size;

	if((sqid == 0) || (sqid == 65))
	{
	nvme_hal_trace(LOG_INFO, 0x159e, "enable SQ (%d), QDepth(%d)", sqid, sq->qsize);
	}

	if (sqid == 0)
	{
		/* from ASIC side, use its own queue depth with empty/full control */
		adm_cmd_sram_base_func0_t aqb;

		aqb.b.adm_cmd_sram_base = btcm_to_dma(cmd_base[0]);
		aqb.b.addr_is_for_dtcm = 1;
		aqb.b.adm_cmd_fetch_max_sz = IO_SQUEUE_MAX_DEPTH - 1;
		sq->qsize = IO_SQUEUE_MAX_DEPTH - 1;
		for (i = 0; i <= aqb.b.adm_cmd_fetch_max_sz; i++)
		{
			cmd_base[0][i] = nvme_cmd_get(__FUNCTION__);
			if (!cmd_base[0][i])
				panic("hal_nvmet_create_sq: out of nvme_cmd\n");
		}

		flex_misc_writel(aqb.all, ADM_CMD_SRAM_BASE_FUNC0);
		sq0_cmd_sram_base_t sq_base = {
			.all = nvme_readl(SQ0_CMD_SRAM_BASE + q_ofst),
		};
		/* Initialize command fetch related registers */
		sq_base.b.adm_cmd_sram_base = btcm_to_dma(cmd_base[sqid]);
		sq_base.b.addr_is_for_dtcm_2100 = 1;
		//sq_base.b.sq0_cmd_fetch_max_sz = sq->qsize;
		nvme_writel(sq_base.all, SQ0_CMD_SRAM_BASE + q_ofst);
	}

	if (sqid != 0)
	{
		flex_sq_ctrl_sts0_t sq_ctrl = {
			.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (sqid << 2)),
		};

		extern void nvmet_set_sqcq(u16 sqid, u16 cqid);
		nvmet_set_sqcq(sqid, sq_ctrl.b.cq_id_mapped);
	}

	return;
}

/*!
 * @brief	Init NVMe block register RAM
 *
 * Init NVMe block register RAM by writing "0XF12E5A2E" to 0xC0031AB0
 *
 * @return	None
 */
static void nvme_init_block_register_ram(void)
{
	nvme_unmasked_int_status_t ctrlr_sts;
	u32 flags = irq_save();

#define RAM_INIT_PATTERN 0xF12E5A2E
	flex_misc_writel(RAM_INIT_PATTERN, FLEX_HW_SRAM_INIT);
	nvme_writel(RAM_INIT_PATTERN, HW_SRAM_INIT);
	do
	{
		ctrlr_sts.all = nvme_readl(NVME_UNMASKED_INT_STATUS);
	} while (ctrlr_sts.b.hw_sram_init_done != 1);
	nvme_writel(ctrlr_sts.all, NVME_UNMASKED_INT_STATUS);

	irq_restore(flags);
}

/*!
 * @brief	return nvme pcie bar space register
 *
 * @param	None
 *
 * @return	struct nvme register
 */
struct nvme_registers *hal_nvmet_ctrlr_registers(void)
{
	return &nvmet_regs;
}
/*!
 * @brief	check sq has command not used
 *
 * @param	sqid	sq id
 *
 * @return	true if sq has command not used
 */

fast_code bool hal_nvmet_sq_has_cmd(u16 sqid)
{
	u32 bm = nvme_readl(FETCHED_CMD_BITMAP);

	return (bool)((!!(bm & BIT(sqid))) == 1);
}

/*!
 * @brief	check have fw don't read fetched nvme command
 *
 * @param	None
 *
 * @return	0 don't have 1 have
 */
__attribute__((unused)) static fast_code int nvmet_hw_sts(void)
{
	u32 bm = nvme_readl(FETCHED_CMD_BITMAP);

	if (bm & FETCHED_CMD_BITMAP_MASK)
		return 1;

	u32 en_bitmap = nvme_readl(SQ_ARBITRATION_EN);
	int sqid;

	for (sqid = 0; sqid < 8; sqid++)
	{
		if (0 == (en_bitmap & (1 << (sqid + SQ0_ENABLE_SHIFT))))
			continue;

		if (sqid == 0)
		{
			sq0_db_reg_t db_ctrls = {
				.all = nvme_readl(SQ0_DB_REG + (sqid << 3)),
			};
			if (db_ctrls.b.sq0_tail_ptr != db_ctrls.b.sq0_head_ptr)
				return 1;
			continue;
		}

		sq1_db_reg_t db_ctrls = {
			.all = nvme_readl(SQ0_DB_REG + (sqid << 3)),
		};
		if (db_ctrls.b.sq1_tail_ptr != db_ctrls.b.sq1_head_ptr)
			return 1;
	}

	return 0;
}

/*!
 * @brief check nvme HW state can enter pmu
 *
 * @param None
 *
 * @return	1 can't have 0 can
 */
fast_code int hal_nvmet_hw_sts(void)
{
#define NVME_CTRL_NOT_IDLE_MASK (  \
	DBL_NOT_IDLE_MASK |            \
	DMA_NOT_IDLE_MASK |            \
	HMB_NOT_IDLE_MASK |            \
	CMD_PROC_NOT_IDLE_MASK |       \
	IO_CMD_PENDING_MASK |          \
	ADM_CMD_PENDING_MASK |         \
	NRM_MODE_IO_CMD_PENDING_MASK | \
	NRM_MODE_ADM_CMD_PENDING_MASK)

#if 1
	u32 sts = nvme_readl(NVME_CTRL_STATUS);

	int hw_sts = (sts & NVME_CTRL_NOT_IDLE_MASK);
#if 0 /* disable it when hardware state is correct */
	int fw_sts = nvmet_hw_sts();

	if ((hw_sts == 0 && fw_sts != 0) ||
	    (hw_sts != 0 && fw_sts == 0)) {
		nvme_hal_trace(LOG_ALW, 0x49df, "state is not matched, hw %x, fw %x\n", hw_sts, fw_sts);
		hw_sts = 1;
	}
#endif

#ifndef NVME_SHASTA_MODE_ENABLE
	if (hw_sts == 0)
	{
		adm_cmd_fetch_en_pf_t pf_fet = {.all = flex_misc_readl(ADM_CMD_FETCH_EN_PF)};
		fetched_adm_cmd_vf_t vf_fet = {.all = flex_misc_readl(FETCHED_ADM_CMD_VF)};

		if (pf_fet.b.fetched_adm_cmd_bitmap || vf_fet.all != 0)
			hw_sts |= 1;
	}
#endif
	return hw_sts;
#else
	return nvmet_hw_sts();
#endif
}

fast_code void hal_nvmet_abort_xfer(bool abort)
{
	abort_xfer_t abt_xfer = {.all = nvme_readl(ABORT_XFER)};

	if (abort == false)
	{
		abt_xfer.b.abort_write_dma = 0;
		abt_xfer.b.abort_read_dma = 0;
		abt_xfer.b.abort_pcie_wr = 0;
		nvme_writel(abt_xfer.all, ABORT_XFER);
	}
	else
	{
		abt_xfer.b.abort_write_dma = 1;
		abt_xfer.b.abort_read_dma = 1;
		abt_xfer.b.abort_pcie_wr = 1;
		nvme_writel(abt_xfer.all, ABORT_XFER);
#if 0
		/* Wait for 1us. 1us is for simulation. For FW adding higher
	 	 * time (may be 1ms). The wait and data xfer set and clear
	 	 * are FW workaround for the following HW issue => If there
	 	 * are 8 DUs pending in read DMA and PCIe is stuck (wready is 0),
	 	 * read DMA will not fetch DTAG from BTN */
		mdelay(1);

		/* Reset data xfer */
		port_reset_ctrl_t rst_ctrl = {.all = nvme_readl(PORT_RESET_CTRL), };

		rst_ctrl.b.reset_data_xfer = 1;
		nvme_writel(rst_ctrl.all, PORT_RESET_CTRL);
		/* Clear data xfer */
		rst_ctrl.all = nvme_readl(PORT_RESET_CTRL);
		rst_ctrl.b.reset_data_xfer = 0;
		nvme_writel(rst_ctrl.all, PORT_RESET_CTRL);
#endif
	}
}

/*!
 * @brief	Get SQ pointer
 *
 * @param	sqid	SQ id
 * @param	head	current head pointer (software updated)
 * @param	tail	current tail pointer (hardware updated)
 *
 * @return	none
 */
fast_code void hal_nvmet_get_sq_pnter(u16 sqid, u16 *head,
									  u16 *tail)
{
#ifdef NVME_SHASTA_MODE_ENABLE
	nvmet_sq_t *sq = &sq_base[sqid];

	sq0_cmd_fetch_ptrs_t fetch_pntr = {
		.all = nvme_readl(sq->reg_cmd_fetch_pnter),
	};

	*head = sq->cmd_fetch_rptr;
	*tail = fetch_pntr.b.adm_cmd_fetch_wptr;
#else
	flex_sq0_db_reg_t db = {.all = flex_dbl_readl(FLEX_SQ0_DB_REG + (sqid << 2))};

	*head = db.b.sq_head_ptr;
	*tail = db.b.sq_tail_ptr;
#endif
}
/*!
 * @brief	get the standard nvme command from buffer and may refill again
 *
 * @param	sqid :sq id
 * @param	refill :fill the address to the hardware queue
 *
 * @return	nvme_cmd pointer
 */
fast_code struct nvme_cmd *hal_nvmet_get_sq_cmd(u16 sqid, bool refill)
{
	struct nvme_cmd *cmd = NULL;
	nvmet_sq_t *sq = &sq_base[sqid];

	sq0_cmd_fetch_ptrs_t fetch_pntr = {
		.all = nvme_readl(sq->reg_cmd_fetch_pnter),
	};

	if (fetch_pntr.b.adm_cmd_fetch_wptr != sq->cmd_fetch_rptr)
	{
		cmd = nvme_cmd_cast(cmd_base[sqid][sq->cmd_fetch_rptr]);

		nvme_hal_trace(LOG_DEBUG, 0x1db1, "cmd(%p) qid(%d) opcode 0x%x, cid(%d) SQ rptr/wptr(%d/%d)",
					   cmd, sqid, cmd->opc, cmd->cid, sq->cmd_fetch_rptr, fetch_pntr.b.adm_cmd_fetch_wptr);

		if (refill)
			cmd_base[sqid][sq->cmd_fetch_rptr] = nvme_cmd_get(__FUNCTION__);
		else
			cmd_base[sqid][sq->cmd_fetch_rptr] = 0;

		sq->cmd_fetch_rptr = (sq->cmd_fetch_rptr + 1) & sq->qsize;
		fetch_pntr.b.adm_cmd_fetch_rptr = sq->cmd_fetch_rptr;
		nvme_writel(fetch_pntr.all, sq->reg_cmd_fetch_pnter);
	}

	return cmd;
}

/*!
 * @brief	release the nvme_cmd for recycling
 * 		Put the nvme_cmd to (A)SQ for recycling
 *
 * @param	cmd	the recycling nvme_cmd to pool
 *
 * @return	None
 */
fast_code void hal_nvmet_put_sq_cmd(struct nvme_cmd *cmd)
{
	pool_put_ex(&nvme_cmd_pool, cmd);
	ctrlr->free_nvme_cmds++;
}

#if !defined(SRIOV_SUPPORT)
/*!
 * @brief clear_ ppnd cpl
 *
 *
 * @param None
 *
 * @return	None
 */
static fast_code void hal_nvmet_clear_ppnd_cpl(void)
{
	struct list_head *pos, *n;
	if (cpl_ppnd_cnt == 0)
		return;

	nvme_hal_trace(LOG_WARNING, 0x9a94, "[OUT] cpl_ppnd_cnt=%d", cpl_ppnd_cnt);

	list_for_each_safe(pos, n, &cpl_ppnd_wl)
	{
		nvme_hal_trace(LOG_DEBUG, 0x6a6d, "%p %p\n",
					   pos, n);
		cpl_ppnd_t *cpl_ppnd =
			container_of(pos, cpl_ppnd_t,
						 entry);
		nvme_hal_trace(LOG_DEBUG, 0xd91e, "cpl_ppnd_cmd(%p) id %d\n", cpl_ppnd, cpl_ppnd->cmd_id);
		cpl_ppnd_cnt--;
		list_del_init(&cpl_ppnd->entry);
		free(cpl_ppnd);
	}

	if (cpl_ppnd_cnt == 0)
		nvmet_disable_cq_req_done_isr();

	if (timer_is_pending(&cq_timer))
		del_timer(&cq_timer);
}
#endif

/*!
 * @brief	check ppnd cpl entry and send to HW
 *
 *
 * @param	None
 *
 * @return	None
 */
static fast_code void hal_nvmet_check_ppnd_cpl(void)
{
	struct list_head *pos, *n;
	if (cpl_ppnd_cnt == 0)
		return;

	nvme_hal_trace(LOG_DEBUG, 0xf13f, "[OUT] cpl_ppnd_cnt=%d", cpl_ppnd_cnt);

	list_for_each_safe(pos, n, &cpl_ppnd_wl)
	{
		nvme_hal_trace(LOG_DEBUG, 0x5148, "%p %p\n",
					   pos, n);
		cpl_ppnd_t *cpl_ppnd =
			container_of(pos, cpl_ppnd_t,
						 entry);
		nvme_hal_trace(LOG_DEBUG, 0xb88c, "cpl_ppnd_cmd(%p) id %d\n", cpl_ppnd, cpl_ppnd->cmd_id);
#ifndef NVME_SHASTA_MODE_ENABLE
		flex_cq_ctrl_sts0_t cq_ctrl = {
			.all = flex_misc_readl(FLEX_CQ0_DB_REG + (cpl_ppnd->cqid << 2)),
		};
		if (cq_ctrl.b.cq_full_300 == 0)
#endif
		{
			if (hal_nvmet_update_cq(cpl_ppnd->cqid, &cpl_ppnd->cpl,
									true) == false)
			{
				nvme_hal_trace(LOG_ERR, 0xda63, "WIP %p", cpl_ppnd);
				break;
			}
			cpl_ppnd_cnt--;
			list_del_init(&cpl_ppnd->entry);
			free(cpl_ppnd);
		}
	}

	if (cpl_ppnd_cnt == 0)
		nvmet_disable_cq_req_done_isr();
}

/*!
 * @brief	CQ ppnd cpl timer handler
 *
 * @param	data	not used
 *
 * @return	None
 */
static fast_code void nvmet_cq_timer(void *data)
{
	hal_nvmet_check_ppnd_cpl();
	if (cpl_ppnd_cnt != 0)
		mod_timer(&cq_timer, jiffies + HZ/10);
}
/*!
 * @brief	update CQ entry and send out the result
 * 		Use ACQ for all queue completion currently.
 *
 * @param	cqid	where the entry to update
 * @param	cpl	the completion entry to send
 * @param	ppnd	is the cpl entry postponed before
 *
 * @return	true:  delivery success, false : enable CQ request ISR and postpone to deliver
 */
bool fast_code
hal_nvmet_update_cq(u16 cqid, struct nvme_cpl *cpl, bool ppnd)
{
	u16 wptr_nxt = (cq->wptr + 1) & cq_base[0].qsize;
	bool cq_full = false;
	fw_cq_reqs_pointers_t cq_req_pntr = {
		.all = nvme_readl(FW_CQ_REQS_POINTERS),
	};

#ifndef NVME_SHASTA_MODE_ENABLE
	if (ppnd == false)
	{
		flex_cq_ctrl_sts0_t cq_ctrl = {
			.all = flex_misc_readl(FLEX_CQ_CTRL_STS0 + (cqid << 2)),
		};
		if (cq_ctrl.b.cq_full_300)
		{
			cq_full = true;
			mod_timer(&cq_timer, jiffies + 2*HZ/10);
		}
	}
#endif

	if (wptr_nxt == cq_req_pntr.b.fw_cq_reqs_rptr)
		cq_full = true;

	if (cq_full == false)
	{
		nvme_hal_trace(LOG_DEBUG, 0xa3f1, "SQ(%d) CQ(%d) CID(%d) rptr/wptr (%d/%d) CDW0 %x", cpl->sqid, cqid, cpl->cid,
					   cq_req_pntr.b.fw_cq_reqs_rptr, cq->wptr, cpl->cdw0);
		/* ldmia & stmia.w */
		cq->base[cq->wptr] = *cpl;
		cq->wptr = wptr_nxt;

		cq_req_pntr.b.fw_cq_reqs_wptr = wptr_nxt;
		nvme_writel(cq_req_pntr.all, FW_CQ_REQS_POINTERS);

		return true;
	}

	/* completion full */
	if (ppnd == false)
	{
		cpl_ppnd_t *cpl_ppnd = malloc(sizeof(*cpl_ppnd));

		if (cpl_ppnd == NULL)
		{
			panic("hal_nvmet_update_cq: out of memory\n");
		}
		cpl_ppnd->cpl = *cpl;
		cpl_ppnd->cqid = cqid;
		cpl_ppnd->cmd_id = cpl_ppnd_cmd_id++;

		INIT_LIST_HEAD(&cpl_ppnd->entry);
		list_add_tail(&cpl_ppnd->entry, &cpl_ppnd_wl);
		cpl_ppnd_cnt++;

		nvmet_enable_cq_req_done_isr();

		//nvme_hal_trace(LOG_DEBUG, 0, "(IN) %p cpl_ppnd_cnt = %d\n",
			//		   cpl_ppnd, cpl_ppnd_cnt);
	}

	return false;
}

fast_code bool hal_nvmet_delete_cq(u16 cqid)
{
	u32 q_shf = 0;
	u32 q_ofst = 0;
	u32 val = 0;

	//nvme_hal_trace(LOG_DEBUG, 0, "Delete CQ (%d)", cqid);

	/* restore CQ Doorbell */
	cq0_db_reg_t cqdb_sts = {
		.all = nvme_readl(CQ0_DB_REG + (cqid << 3)),
	};
	cqdb_sts.b.cq0_head_ptr = 0;
	cqdb_sts.b.cq0_tail_ptr = 0;
	cqdb_sts.b.cq0_phase_tag = 1;
	nvme_writel(cqdb_sts.all, CQ0_DB_REG + (cqid << 3));

	/* restore MSIX to default value */
	q_shf = (cqid << 3) & 0x1f;
	q_ofst = (cqid >> 2) << 2;

	val = nvme_readl(MSIX_CQ_MAP0 + q_ofst);
	val &= ~(0xff << q_shf); /* FIXME: vector 0 to disable */
	nvme_writel(val, MSIX_CQ_MAP0 + q_ofst);

	/* clean up interrupt whatever set or not */
	val = nvme_readl(CQ_INTERRUPT_EN);
	val &= ~(1 << cqid);
	nvme_writel(val, CQ_INTERRUPT_EN);

	return true;
}

/*!
 * @brief	delete I/O submission queue
 *
 * @param	sqid	the queue to be deleted
 *
 * @return		Return success
 */
fast_code bool _hal_nvmet_delete_sq(u16 sqid)
{
	u32 val = 0;
	/* Disable Arbitration */
	val = nvme_readl(SQ_ARBITRATION_EN);
	val &= ~(1 << (sqid + SQ0_ENABLE_SHIFT));
	nvme_writel(val, SQ_ARBITRATION_EN);
#if 0 // TODO: move to where?
	arbitration_control_sts_t arb_ctrls = {
		.all = nvme_readl(ARBITRATION_CONTROL_STS),
	};
	arb_ctrls.b.arbitration_enable = 0;
	nvme_writel(arb_ctrls.all, ARBITRATION_CONTROL_STS);
#endif
	/* Disable Command Fetch */
	sq_cmd_fetch_enable_t sq_fetch = {
		.all = nvme_readl(SQ_CMD_FETCH_ENABLE),
	};
	sq_fetch.b.sq_cmd_fetch_enable &= ~(BIT(sqid));
	nvme_writel(sq_fetch.all, SQ_CMD_FETCH_ENABLE);

	/* restore Queue size & rptr/wptr */
	u32 q_ofst = sqid << 3;
	sq0_cmd_sram_base_t sq_base = {
		.all = nvme_readl(SQ0_CMD_SRAM_BASE + q_ofst),
	};
	sq_base.b.adm_cmd_sram_base = 0;
	sq_base.b.adm_cmd_fetch_max_sz = 0;
	nvme_writel(sq_base.all, SQ0_CMD_SRAM_BASE + q_ofst);

	sq0_cmd_fetch_ptrs_t cmd_fetch_pntr = {
		.all = nvme_readl(SQ0_CMD_FETCH_PTRS + q_ofst),
	};
	cmd_fetch_pntr.b.adm_cmd_fetch_rptr = 0;
	cmd_fetch_pntr.b.adm_cmd_fetch_wptr = 0;
	nvme_writel(cmd_fetch_pntr.all, SQ0_CMD_FETCH_PTRS + q_ofst);

	/* clear SQ size */
	sq_size_reg1_t sq_size = {
		.all = nvme_readl(SQ_SIZE_REG1 + ((sqid - 1) << 2)),
	};
	sq_size.b.sq1_size = 0;
	nvme_writel(sq_size.all, SQ_SIZE_REG1 + ((sqid - 1) << 2));

	/* restore SQ Doorbell */
	sq0_db_reg_t sqdb_sts = {
		.all = nvme_readl(SQ0_DB_REG + (sqid << 3)),
	};
	sqdb_sts.b.sq0_tail_ptr = 0;
	sqdb_sts.b.sq0_head_ptr = 0;
	/*!
	 * if we clear cq_id for this delete SQ, then the CE of this SQ will
	 * be send to CQ0, it should be wrong
	 * tnvme 14:3.0.0
	 */
	//sqdb_sts.b.cq_id_for_sq0 = 0;
	nvme_writel(sqdb_sts.all, SQ0_DB_REG + (sqid << 3));

	return true;
}

fast_code bool hal_nvmet_delete_sq(u16 sqid)
{
	u16 i = 0;
	nvmet_sq_t *sq = &sq_base[sqid];

	nvme_hal_trace(LOG_DEBUG, 0x6bb8, "Delete SQ (%d)", sqid);
	/*
	 * 1. The PRP list that describes the Submission Queue
	 * may be deallocated by host software
	 *
	 * 2. After all commands submitted to the indicated SQ either completed
	 * or aborted, just return the status to Admin Completion Queue
	 */
	if (sqid == 0)
	{
		for (i = 0; i <= sq->qsize; i++)
		{
			if (cmd_base[sqid][i])
			{
				hal_nvmet_put_sq_cmd(nvme_cmd_cast(cmd_base[sqid][i]));
				cmd_base[sqid][i] = 0;
			}
		}
	}

	_hal_nvmet_delete_sq(sqid);

	return true;
}

/*!
 * @brief set I/O submission queue Priority
 *
 * @param sqid		sq Identifier
 * @param qprio		Queue Priority
 *
 * @return		None
 */
fast_code void hal_nvmet_sq_arb_setup(u16 sqid, u8 qprio)
{
	if (qprio == NVME_QPRIO_URGENT)
	{
		urgent_queue_bitmap_t u_bitmap = {
			.all = nvme_readl(URGENT_QUEUE_BITMAP),
		};
		u_bitmap.b.urgent_queue_bitmap |= (1 << sqid);
		u_bitmap.b.rsvd_9 = 0;
		nvme_writel(u_bitmap.all, URGENT_QUEUE_BITMAP);
	}
	else if (qprio == NVME_QPRIO_HIGH)
	{
		h_priority_que_bitmap_t h_bitmap = {
			.all = nvme_readl(H_PRIORITY_QUE_BITMAP),
		};
		h_bitmap.b.h_priority_que_bitmap |= (1 << sqid);
		h_bitmap.b.rsvd_0 = 0;
		h_bitmap.b.rsvd_25 = 0;
		nvme_writel(h_bitmap.all, H_PRIORITY_QUE_BITMAP);
	}
	else if (qprio == NVME_QPRIO_MEDIUM)
	{
		m_priority_que_bitmap_t m_bitmap = {
			.all = nvme_readl(M_PRIORITY_QUE_BITMAP),
		};
		m_bitmap.b.m_priority_que_bitmap |= (1 << sqid);
		m_bitmap.b.rsvd_9 = 0;
		nvme_writel(m_bitmap.all, M_PRIORITY_QUE_BITMAP);
	}
	else if (qprio == NVME_QPRIO_LOW)
	{
		l_priority_que_bitmap_t l_bitmap = {
			.all = nvme_readl(L_PRIORITY_QUE_BITMAP),
		};
		l_bitmap.b.l_priority_que_bitmap |= (1 << sqid);
		l_bitmap.b.rsvd_0 = 0;
		l_bitmap.b.rsvd_25 = 0;
		nvme_writel(l_bitmap.all, L_PRIORITY_QUE_BITMAP);
	}
}
/*!
 * @brief	create create HAL I/O submission Queue
 *
 * @param	sqid	sq id
 * @param	cqid	cq id
 * @param	qsize	size of queue
 * @param	qprio	priority of submission queue
 * @param	pc	ppr contiunous
 * @param	prp1	prp1 of submission queue
 *
 * @return	true
 */
fast_code bool
hal_nvmet_create_sq(u16 sqid, u16 cqid,
					u16 qsize, u8 qprio, u8 pc, u64 prp1)
{
	u32 val = 0;
	u32 q_ofst = 0;	/* queue offset */
	nvmet_sq_t *sq = &sq_base[sqid];

	sys_assert(sqid > 0);

	sq->qsize = ((qsize & (qsize - 1)) == 0) ? qsize - 1 : get_next_power_of_two(qsize) - 1;
	if (sq->qsize > IO_SQUEUE_MAX_DEPTH - 1)
		sq->qsize = IO_SQUEUE_MAX_DEPTH - 1;

	/* It's 0 based, but at least 2 entries, means the max_sz should be 1 or larger */
	if (sq->qsize == 0)
		sq->qsize = 1;

	sq->cmd_fetch_rptr = 0;

	nvme_hal_trace(LOG_DEBUG, 0x1d93, "create SQ (%d), QDepth(%d/%d)", cqid, qsize,
				   sq->qsize);

	nvme_writel(prp1, SQ1_H_BASE_ADDR_L + ((sqid - 1) << 3));
	nvme_writel(prp1 >> 32, SQ1_H_BASE_ADDR_H + (((sqid - 1)) << 3));

	/* Set SQ size */
	sq_size_reg1_t sq_size = {
		.all = nvme_readl(SQ_SIZE_REG1 + ((sqid - 1) << 2)),
	};
	sq_size.b.sq1_size = qsize;
	nvme_writel(sq_size.all, SQ_SIZE_REG1 + ((sqid - 1) << 2));

	/* re-initialize SQ Doorbell */
	sq0_db_reg_t db_ctrls = {
		.all = nvme_readl(SQ0_DB_REG + (sqid << 3)),
	};
	db_ctrls.b.sq0_tail_ptr = 0;
	db_ctrls.b.sq0_head_ptr = 0;
	db_ctrls.b.cq_id_for_sq0 = cqid;
	nvme_writel(db_ctrls.all, SQ0_DB_REG + (sqid << 3));

	/*setup arb bitmap*/
	hal_nvmet_sq_arb_setup(sqid, qprio);

	/* Queue Arbitration enable */
	val = nvme_readl(SQ_ARBITRATION_EN);
	val |= (1 << (sqid + SQ0_ENABLE_SHIFT));
	nvme_writel(val, SQ_ARBITRATION_EN);

	hal_nvmet_set_sq_arbitration(0, 0, 0);

	/* FW needs to set this bit when it updates any arbitration
	 * related registers or when the system resumed from low power mode,
	 * no matter the orignal value is 1 or 0 */
	arbitration_control_sts_t arb_ctrls = {
		.all = nvme_readl(ARBITRATION_CONTROL_STS),
	};
	arb_ctrls.b.arbitration_enable = 1;
	nvme_writel(arb_ctrls.all, ARBITRATION_CONTROL_STS);

	/* Initialize command fetch related registers */
	q_ofst = sqid << 3;
	sq0_cmd_sram_base_t sq_base = {
		.all = nvme_readl(SQ0_CMD_SRAM_BASE + q_ofst),
	};
	sq_base.b.adm_cmd_sram_base = 0;
	sq_base.b.addr_is_for_dtcm_2100 = 0;
	sq_base.b.adm_cmd_fetch_max_sz = sq->qsize;
	nvme_writel(sq_base.all, SQ0_CMD_SRAM_BASE + q_ofst);

	sq0_cmd_fetch_ptrs_t cmd_fetch_pntr = {
		.all = nvme_readl(SQ0_CMD_FETCH_PTRS + q_ofst),
	};
	cmd_fetch_pntr.b.adm_cmd_fetch_rptr = 0;
	sq->reg_cmd_fetch_pnter = SQ0_CMD_FETCH_PTRS + q_ofst;
	nvme_writel(cmd_fetch_pntr.all, sq->reg_cmd_fetch_pnter);

	/* Enable Command Fetch */
	sq_cmd_fetch_enable_t cmd_fetch_enable = {
		.all = nvme_readl(SQ_CMD_FETCH_ENABLE),
	};
	cmd_fetch_enable.b.sq_cmd_fetch_enable |= BIT((sqid));
	nvme_writel(cmd_fetch_enable.all, SQ_CMD_FETCH_ENABLE);

	/* Activate controller to fetch I/O command */
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.io_cmd_hw_fetch_en = 1;
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);

	return true;
}

/*!
 * @brief	create I/O completion queue
 *
 * @param	sqid	cq id
 * @param	qsize	size of queue
 * @param	vector	msi-z venctor
 * @param	ien	interrupt enable
 * @param	pc	ppr contiunous
 * @param	prp1	prp1 of completition queue
 *
 * @return	true
 */
fast_code bool
hal_nvmet_create_cq(u16 cqid, u16 qsize,
					u16 vector, u16 ien, u16 pc, u64 prp1)
{
	u32 val = 0;
	u32 q_shf = 0;
	u32 q_ofst = 0;

	if (cqid == 0)
		panic("hal_nvmet_create_cq: not for ACQ\n");

	if (qsize > nvmet_regs.cap.bits.mqes)
		return false;

	nvme_hal_trace(LOG_DEBUG, 0x4ce3, "create CQ (%d) Qdepth (%d)", cqid, qsize);

	nvme_writel(prp1, CQ1_H_BASE_ADDR_L + ((cqid - 1) << 3));
	nvme_writel(prp1 >> 32, CQ1_H_BASE_ADDR_H + ((cqid - 1) << 3));

	/* Configure completion queue using host setting */
	q_shf = (cqid << 4) & 0x1f;
	q_ofst = (cqid >> 1) << 2;

	cq_size_reg1_t cq_size = {
		.all = nvme_readl(CQ_SIZE_REG1 + ((cqid - 1) << 2)),
	};
	cq_size.b.cq1_size = qsize;
	nvme_writel(cq_size.all, CQ_SIZE_REG1 + ((cqid - 1) << 2));

	/* Set MSI(X), review MC.MME or MXC.TS if any */
	q_shf = (cqid << 3) & 0x1f;
	q_ofst = (cqid >> 2) << 2;

	val = nvme_readl(MSIX_CQ_MAP0 + q_ofst);
	val &= ~(0xff << q_shf);
	val |= vector << q_shf;
	nvme_writel(val, MSIX_CQ_MAP0 + q_ofst);

	/* re-initilize CQ Doorbell */
	cq1_db_reg_t db_ctrls = {
		.all = nvme_readl(CQ1_DB_REG + ((cqid - 1) << 3)),
	};
	db_ctrls.b.cq1_head_ptr = 0;
	db_ctrls.b.cq1_tail_ptr = 0;
	db_ctrls.b.cq1_phase_tag = 1;
	nvme_writel(db_ctrls.all, CQ1_DB_REG + ((cqid - 1) << 3));

	/* Enable interrupt */
	if (ien)
	{
		val = nvme_readl(CQ_INTERRUPT_EN);
		val |= (1 << cqid);
		nvme_writel(val, CQ_INTERRUPT_EN);
	}

	//default, coalescing settings are enabled for each interrupt vector.
	hal_nvmet_set_int_calsc(vector, false);

	return true;
}
/*!
 * @brief	xfer data between host and drive interface by using dedicated DMA engine
 *
 * @param	prp 		host prp address
 * @param	sram		sram address
 * @param	len		lengthe of xfer data
 * @param	hcmd		host command pointer
 * @param	callback	call back function
 *
 * @return	true
 */
extern volatile u8 plp_trigger;
#if 0	//3.1.7.4 merged 20201201 Eddie
fast_code bool
hal_nvmet_data_xfer(u64 prp, void *sram, u16 len, dir_t dir,
		    void *hcmd, void (*callback)(void *hcmd, bool error))
{
	dma_req_t *req;
	u16 orlen = 0;
	struct dma_queue_t *queue = &dma_queues[dir];
	dma_queue0_pointers_t queue_ptrs = {
		.all = nvme_readl(queue->queue_ptrs),
	};
	u8 next_wptr = (queue->wptr + 1) & queue->qsize;

	while (queue_ptrs.b.dma_queue0_rptr == next_wptr)
		queue_ptrs.all = nvme_readl(queue->queue_ptrs);

	req = &queue->reqs[queue->wptr];

	dma_queues_opaque[dir][queue->wptr].hcmd = hcmd;
	dma_queues_opaque[dir][queue->wptr].callback = callback;

	req->prp = prp;
	req->sram_addr = sram_to_dma(sram);

	req_t *req_xfer = hcmd;

	if(req_xfer->req_prp.fg_prp_list == true){
		if(len%4)
	    	{
			orlen = len;
			len = 4*(len/4);
			evlog_printk(LOG_ALW,"xfer size 4B aligned :[%x] to [%x] \n",orlen,len);
	    	}
	}

	req->dw3.b.len = len;

	queue->wptr = next_wptr;
	queue_ptrs.b.dma_queue0_wptr = queue->wptr;
	dsb();

	nvme_writel(queue_ptrs.all, queue->queue_ptrs);

	return true;
}
#else
//fast_data_zi bool In_Data_Xfer;
fast_code bool
hal_nvmet_data_xfer(u64 prp, void *sram, u16 len, dir_t dir,
					void *hcmd, void (*callback)(void *hcmd, bool error))
{
	dma_req_t *req;
	u16 orlen = 0;
	struct dma_queue_t *queue = &dma_queues[dir];
	dma_queue0_pointers_t queue_ptrs = {
		.all = nvme_readl(queue->queue_ptrs),
	};
	u8 next_wptr = (queue->wptr + 1) & queue->qsize;
	//In_Data_Xfer = true;
	//while (queue_ptrs.b.dma_queue0_rptr == next_wptr)
	//	queue_ptrs.all = nvme_readl(queue->queue_ptrs);

#ifdef While_break
	u64 start = get_tsc_64();
#endif
	while (queue->rptr == next_wptr)
	{
		nvme_unmasked_int_status_t ctrlr_sts = {
			.all = nvme_readl(NVME_MASKED_INT_STATUS),
		};
        if(plp_trigger)
            return false;
		if (ctrlr_sts.b.dma_queue0_xfer_done ||
			ctrlr_sts.b.dma_queue1_xfer_done ||
			ctrlr_sts.b.dma_queue_xfer_error)
		{
			bool force = false;

			if (ctrlr_sts.b.dma_queue_xfer_error)
			{
				extern __attribute__((weak)) void dma_error_cnt_incr(void);
				force = true;
				if (dma_error_cnt_incr)
					dma_error_cnt_incr();
			}
			/*! mask dma interrupt to avoid interrupt missing */
			/*
                 * 1. mask DMA done interrupt
                 * 2. clear DMA done interrupt
                 * 3. handle interrupt
                 * 4. unmask interrupt
                 */
			nvmet_disable_dma_done_irq();
			u32 isr_ack = 0;
			if (force || ctrlr_sts.b.dma_queue0_xfer_done)
			{
				if (false == nvmet_dma_queue_done(0))
				{
					isr_ack |= DMA_QUEUE0_XFER_DONE_MASK;
				}
			}

			if (force || ctrlr_sts.b.dma_queue1_xfer_done)
			{
				if (false == nvmet_dma_queue_done(1))
				{
					isr_ack |= DMA_QUEUE1_XFER_DONE_MASK;
				}
			}

			nvme_writel(isr_ack, NVME_UNMASKED_INT_STATUS);

			nvmet_enable_dma_done_irq();
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

	req = &queue->reqs[queue->wptr];

	dma_queues_opaque[dir][queue->wptr].hcmd = hcmd;
	dma_queues_opaque[dir][queue->wptr].callback = callback;

	req->prp = prp;
	req->sram_addr = sram_to_dma(sram);

	if(hcmd != NULL)//null req not form cmd//now IG debug func
	{
		req_t *req_xfer = hcmd;
		struct nvme_cmd *nvme_cmd = req_xfer->host_cmd;

		if(req_xfer->req_prp.fg_prp_list == true){
			if(len % 4)
			{
				orlen = len;
				len = 4 * (len / 4);
				nvme_hal_trace(LOG_INFO, 0x0430, "xfer size 4B aligned :[%x] to [%x] \n",orlen,len);
			}

		}else if(nvme_cmd->opc == NVME_OPC_GET_LOG_PAGE || nvme_cmd->opc == NVME_OPC_SSSTC_VSC_READ)
		{
			if(len%4)
			{
				orlen = len;
				len = 4*(len/4 + 1);
				nvme_hal_trace(LOG_INFO, 0x25e3, "xfer size 4B aligned :[%x] to [%x] \n",orlen,len);
			}
		}
	}
	req->dw3.b.len = len;

	queue->wptr = next_wptr;
	queue_ptrs.b.dma_queue0_wptr = queue->wptr;
	dsb();

//    nvme_hal_trace(LOG_ERR, 0, "Xfer done");

	nvme_writel(queue_ptrs.all, queue->queue_ptrs);
	//In_Data_Xfer = false;
	return true;
}
#endif
fast_code void hal_nvmet_set_aggregation(u8 thr, u8 time)
{
	u32 val;

	val = REG_SFLD(thr, AGGREGATION_THRH);
	val |= REG_SFLD(time, AGGREGATION_TIME);
	flex_misc_writel(val, FLEX_INT_AGGREGATION_PAR);
}

fast_code u32 hal_nvmet_get_aggregation(void)
{
	u32 aggr = flex_misc_readl(FLEX_INT_AGGREGATION_PAR);

	aggr &= 0x0000FFFF;
	return aggr;
}

/*!
 * @brief	get boot partition commands
 *
 * @param	ofst	boot partition read offset
 * @param	len	data read size
 * @param	hmem	host memory
 *
 * @return	always true
 */
ddr_code bool hal_nvmet_bootpart_get_cmd(u32 *ofst, u32 *len, u64 *hmem)
{
	union nvme_bprsel_register bprsel = {
		.raw = nvme_readl(BOOT_PART_READ_SEL),
	};

	if (nvmet_regs.bpinfo.bits.abpid != bprsel.bits.bpid)
	{
		nvme_hal_trace(LOG_INFO, 0x7f8d, "Boot Partition(%d) is not active\n", bprsel.bits.bpid);
		return false;
	}

	*ofst = bprsel.bits.bprof;
	*len = bprsel.bits.bprsz;

	*hmem = (((u64)nvme_readl(BOOT_PART_MEM_BUF_LOC_H)) << 32) | nvme_readl(BOOT_PART_MEM_BUF_LOC_L);

	sys_assert((((*hmem) & 0xFFF) == 0));

	return true;
}
/*!
 * @brief	update boot partition read status
 *
 * @param	brs	boot partition read status to be update
 *
 * @return	not used
 */

ddr_code void hal_nvmet_bootpart_upt_brs(enum nvme_brs brs)
{
	nvmet_regs.bpinfo.bits.brs = brs;
	nvme_writel(nvmet_regs.bpinfo.raw, BOOT_PART_INFO);
#if VALIDATE_BOOT_PARTITION
	nvme_hal_trace(LOG_ERR, 0xbbda, "boot partition reg: 0x%x\n", nvme_readl(BOOT_PART_INFO));
#endif //VALIDATE_BOOT_PARTITION
	return;
}
/*!
 * @brief	Set Active Boot Partition Identifier
 *
 * @param	abpid	active boot partition id to be set
 *
 * @return	not used
 */
ddr_code void hal_nvmet_bootpart_set_abpid(u8 abpid)
{
	nvmet_regs.bpinfo.bits.abpid = abpid;
	nvme_writel(nvmet_regs.bpinfo.raw, BOOT_PART_INFO);
#if VALIDATE_BOOT_PARTITION
	nvme_hal_trace(LOG_ERR, 0x91e7, "boot partition reg: 0x%x\n", nvme_readl(BOOT_PART_INFO));
#endif //VALIDATE_BOOT_PARTITION
	return;
}

fast_code void hal_nvmet_set_int_calsc(u16 vector, bool cd)
{
	u32 val = nvme_readl(INT_COALESCING_CTRL);

	vector += INTV0_CALSC_EN_SHIFT;
	if (cd)
		val &= ~(1 << vector);
	else
		val |= (1 << vector);

	nvme_writel(val, INT_COALESCING_CTRL);
}

/*!
 * @brief	rebuild sq after warm boot used for shasta mode
 *
 * @param	sqid sq id
 *
 * @return	None
 */
#if defined(PROGRAMMER)
fast_code void hal_nvmet_rebuild_sq(u16 sqid)
{
	u32 i = 0;
	//u32 val = 0;
	u32 q_ofst = 0;	/* queue offset */
	nvmet_sq_t *sq = &sq_base[sqid];

	/* Set SQ size */
	q_ofst = sqid << 3;
	sq0_cmd_sram_base_t sq_base = {
		.all = nvme_readl(SQ0_CMD_SRAM_BASE + q_ofst),
	};
	sq->qsize = sq_base.b.adm_cmd_fetch_max_sz;
	sq->reg_cmd_fetch_pnter = SQ0_CMD_FETCH_PTRS + q_ofst;

	nvme_hal_trace(LOG_INFO, 0x829a, "enable SQ (%d), QDepth(%d)", sqid, sq->qsize);

	/* from ASIC side, use its own queue depth with empty/full control */
	for (i = 0; i <= sq->qsize; i++)
	{
		cmd_base[sqid][i] = nvme_cmd_get(__FUNCTION__);
		if (!cmd_base[sqid][i])
		{
			panic("hal_nvmet_create_sq: out of nvme_cmd\n");
		}
	}

	u32 *old_cmd;
	sq0_cmd_fetch_ptrs_t sq_ptr = {
		.all = nvme_readl(SQ0_CMD_FETCH_PTRS + q_ofst),
	};
	sq->cmd_fetch_rptr = sq_ptr.b.adm_cmd_fetch_rptr;
	old_cmd = dma_to_sram(sq_base.b.adm_cmd_sram_base);
	nvme_hal_trace(LOG_INFO, 0x17c8, "enable SQ (%d), base addr: %x %x %x", sqid, old_cmd, old_cmd[0], sq_base.b.adm_cmd_sram_base);
	nvme_hal_trace(LOG_INFO, 0x4883, "read ptr:%x write ptr:%x", sq_ptr.b.adm_cmd_fetch_rptr, sq_ptr.b.adm_cmd_fetch_wptr);
	if (sq_ptr.b.adm_cmd_fetch_rptr < sq_ptr.b.adm_cmd_fetch_wptr)
	{
		for (i = sq_ptr.b.adm_cmd_fetch_rptr; i < sq_ptr.b.adm_cmd_fetch_wptr; i++)
			memcpy((void *)nvme_cmd_cast(cmd_base[sqid][i]), (void *)old_cmd[i], sizeof(struct nvme_cmd));
	}
	else if (sq_ptr.b.adm_cmd_fetch_rptr > sq_ptr.b.adm_cmd_fetch_wptr)
	{
		for (i = sq_ptr.b.adm_cmd_fetch_rptr; i <= sq->qsize; i++)
			memcpy((void *)nvme_cmd_cast(cmd_base[sqid][i]), (void *)old_cmd[i], sizeof(struct nvme_cmd));
		for (i = 0; i < sq_ptr.b.adm_cmd_fetch_wptr; i++)
			memcpy((void *)nvme_cmd_cast(cmd_base[sqid][i]), (void *)old_cmd[i], sizeof(struct nvme_cmd));
	}
	sq_base.b.adm_cmd_sram_base = btcm_to_dma(cmd_base[sqid]);

	/* Initialize command fetch related registers */
	sq_base.b.addr_is_for_dtcm_2100 = 1;
	//sq_base.b.sq0_cmd_fetch_max_sz = sq->qsize;
	nvme_writel(sq_base.all, SQ0_CMD_SRAM_BASE + q_ofst);

	if (sqid != 0)
	{
		sq0_db_reg_t db_ctrls = {
			.all = nvme_readl(SQ0_DB_REG + (sqid << 3)),
		};

		extern void nvmet_set_sqcq(u16 sqid, u16 cqid);
		nvmet_set_sqcq(sqid, db_ctrls.b.cq_id_for_sq0);
	}

	return;
}
#else
fast_code void hal_nvmet_rebuild_sq(u16 sqid)
{
	u32 i = 0;
	//u32 val = 0;
	u32 q_ofst = 0; /* queue offset */
	nvmet_sq_t *sq = &sq_base[sqid];

	/* Set SQ size */
	q_ofst = sqid << 3;
	sq0_cmd_sram_base_t sq_base = {
		.all = nvme_readl(SQ0_CMD_SRAM_BASE + q_ofst),
	};
	sq->qsize = sq_base.b.adm_cmd_fetch_max_sz;
	sq->reg_cmd_fetch_pnter = SQ0_CMD_FETCH_PTRS + q_ofst;

	nvme_hal_trace(LOG_INFO, 0xe9c7, "enable SQ (%d), QDepth(%d)", sqid, sq->qsize);

	if (sqid == 0)
	{
		/* from ASIC side, use its own queue depth with empty/full control */
		for (i = 0; i <= sq->qsize; i++)
		{
			cmd_base[sqid][i] = nvme_cmd_get(__FUNCTION__);
			if (!cmd_base[sqid][i])
			{
				panic("hal_nvmet_create_sq: out of nvme_cmd\n");
			}
		}
		sq_base.b.adm_cmd_sram_base = btcm_to_dma(cmd_base[sqid]);
	}
	else
		sq_base.b.adm_cmd_sram_base = 0;
	/* Initialize command fetch related registers */
	sq_base.b.addr_is_for_dtcm_2100 = 1;
	//sq_base.b.sq0_cmd_fetch_max_sz = sq->qsize;
	nvme_writel(sq_base.all, SQ0_CMD_SRAM_BASE + q_ofst);

	if (sqid != 0)
	{
		sq0_db_reg_t db_ctrls = {
			.all = nvme_readl(SQ0_DB_REG + (sqid << 3)),
		};

		extern void nvmet_set_sqcq(u16 sqid, u16 cqid);
		nvmet_set_sqcq(sqid, db_ctrls.b.cq_id_for_sq0);
	}

	return;
}
#endif

/*!
 * @brief	rebuild cq after warm boot
 *
 * @param	None
 *
 * @return	None
 */
fast_code void hal_nvmet_rebuild_cq(void)
{
	fw_cq_reqs_sram_addr_t acq_base = {
		.all = nvme_readl(FW_CQ_REQS_SRAM_ADDR),
	};

	/* ACQ: All completion queue shares ACQ */
	acq_base.b.fw_cq_sram_addr = btcm_to_dma(nvme_cmpl_base);
	acq_base.b.addr_is_for_dtcm = 1;
	nvme_writel(acq_base.all, FW_CQ_REQS_SRAM_ADDR);

	fw_cq_reqs_pointers_t cq_req_pntr = {
		.all = nvme_readl(FW_CQ_REQS_POINTERS),
	};

	fw_cq_reqs_size_t acq_sz = {
		.all = nvme_readl(FW_CQ_REQS_SIZE),
	};
	cq_base[0].qsize = acq_sz.b.fw_cq_reqs_max_sz;

	cq_base[0].wptr = cq_req_pntr.b.fw_cq_reqs_wptr;
	cq_base[0].base = nvme_cmpl_base;
}

fast_code void hal_nvmet_suspend_cmd_fetch(void)
{
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.adm_cmd_hw_fetch_en = 0; /* Stop Fetch Admin Cmd */
	ctrlr_sts.b.io_cmd_hw_fetch_en = 0;	 /* Stop Fetch I/O Cmd */
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
	//reset_sq_arbitration_en();
}

slow_code_ex void hal_pi_suspend_cmd_fetch(void)
{
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.io_cmd_hw_fetch_en = 0;	 /* Stop Fetch I/O Cmd */
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
	//reset_sq_arbitration_en();
}

slow_code_ex void hal_nvmet_enable_cmd_fetch(void)
{
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.adm_cmd_hw_fetch_en = 1; /* enable Fetch Admin Cmd */
	ctrlr_sts.b.io_cmd_hw_fetch_en = 1;	 /* enable Fetch I/O Cmd */
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
	//reset_sq_arbitration_en();
}

fast_code void hal_pi_enable_cmd_fetch(void)
{
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.io_cmd_hw_fetch_en = 1;	 /* enable Fetch I/O Cmd */
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
	//reset_sq_arbitration_en();
}

slow_code_ex void nvmet_csts_PP(bool set)		//20201225-Eddie
{
    union nvme_csts_register csts = {
        .raw = nvme_readl(CONTROLLER_STATUS),
    };

    csts.bits.pp = set? 1 : 0;

    printk("pp : %d\n",csts.bits.pp);
    nvme_writel(csts.raw, CONTROLLER_STATUS);
    nvmet_regs.csts = csts;
}

/*!
 * @brief	save AER for each function when warm boot
 *
 * @return	none
 */
ddr_code void nvme_save_aer_for_each_func(void)
{
	u32 fid, i, aer01, aer23;
	struct list_head *cur, *saved;

	u16 cid, func_aer[IDFTY_AERL+1]; //two-dimensional array save cid for each function

	i = 0;
	if (ctrlr->aer_outstanding == 0) {
		for (; i < (IDFTY_AERL+1); ++i) {
			func_aer[i] = 0; //
		}
	}

	if (ctrlr->aer_outstanding != 0) {

		list_for_each_safe(cur, saved, &ctrlr->aer_outstanding_reqs) {
			req_t *req = container_of(cur, req_t, entry);
			cid = req->fe.nvme.cid;
			func_aer[i] = cid;
			list_del_init(&req->entry);
			i++;
			ctrlr->admin_running_cmds--;
			ctrlr->aer_outstanding--;
		}
		sys_assert(ctrlr->aer_outstanding == 0);
	}

	if (i < IDFTY_AERL) {
		for(; i < (IDFTY_AERL+1); ++i) {
			func_aer[i] = 0;
		}
	}
		nvme_apl_trace(LOG_INFO, 0x28ee,"save aer cid(%d %d %d %d)", func_aer[0], func_aer[1], func_aer[2], func_aer[3]);

	for (fid = 0; fid < 1; ++fid) {
		aer01 = func_aer[0] << 16 |  func_aer[1];
		aer23 = func_aer[2] << 16 |  func_aer[3];
		nvme_writel(aer01, FUNCTION_AER_ADDR(fid));//Controller_Cap_low save aer0 & aer1
		nvme_writel(aer23, FUNCTION_AER_ADDR(fid)+4);//Controller_Cap_high save aer2 & aer3
	}
}

#ifndef NVME_SHASTA_MODE_ENABLE
/*!
 * @brief	recover AER for each function when resume warm boot
 *
 * @return	none
 */
ddr_code void nvme_recover_aer_for_each_func(void)
{
	u32 fid, i, aer01, aer23;
	u16 aer0, aer1, aer2, aer3, cid;

	for (fid = 0; fid < 1; ++fid) {
		aer01 = nvme_readl(FUNCTION_AER_ADDR(fid));
		aer23 = nvme_readl(FUNCTION_AER_ADDR(fid) + 4);
		aer0 = aer01 >> 16 & 0xff;
		aer1 = aer01 & 0xff;
		aer2 = aer23 >> 16 & 0xff;
		aer3 = aer23 & 0xff;
		if ((aer01 == 0) && (aer23 == 0)) {
			continue; //this function no aer, so did not recover
		}
		INIT_LIST_HEAD(&ctrlr->aer_outstanding_reqs);

		for (i=0; i<(IDFTY_AERL+1); i++) {
			cid = (i == 0) ? aer0 : (i == 1)? aer1 : (i == 2)? aer2 : aer3;
			if (cid == 0)
				continue;
			ctrlr->aer_outstanding++;
			ctrlr->admin_running_cmds++;

			req_t *req = nvmet_get_req();
			sys_assert(req != NULL);
			INIT_LIST_HEAD(&req->entry);
			req->req_from = REQ_Q_ADMIN;
			req->fe.nvme.sqid = 0;
			req->fe.nvme.cntlid = (u8)fid;
			req->fe.nvme.cid = cid;
			req->fe.nvme.cmd_spec = 0;
			req->fe.nvme.nvme_status = 0;
			req->error = 0;
			req->completion = nvmet_core_cmd_done;
			req->state = REQ_ST_FE_ADMIN;

			list_add_tail(&req->entry, &ctrlr->aer_outstanding_reqs);
		}
		nvme_apl_trace(LOG_INFO, 0x0438, "recover aer(%x %x %x %x) done, aer_outstanding(%d)",aer0, aer1, aer2, aer3, ctrlr->aer_outstanding);
	}
}
#endif

slow_code void hal_nvmet_rebuild_io_cq()
{
	u16 cqid;
	for (cqid = 1; cqid < NVMET_RESOURCES_FLEXIBLE_TOTAL; cqid++)
	{
		if(ctrlr->cqs[cqid] == NULL)
		{
			u32 prplo = flex_addr_readl(FLEX_CQ0_H_BASE_ADDR_L + (cqid << 3));
			if (!!prplo)//cq exsit but sq delete
			{
				nvmet_set_cq(cqid);
				evlog_printk(LOG_ALW,"rebuild cq %d",cqid);
			}
		}
	}
}

slow_code void hal_nvmet_check_cmd_fetch()
{
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	if(ctrlr_sts.b.adm_cmd_hw_fetch_en != 1 || ctrlr_sts.b.io_cmd_hw_fetch_en != 1)
	{
		nvme_hal_trace(LOG_INFO, 0xbbb9, "reset admin:%d, io:%d", ctrlr_sts.b.adm_cmd_hw_fetch_en, ctrlr_sts.b.io_cmd_hw_fetch_en);
		ctrlr_sts.b.adm_cmd_hw_fetch_en = 1;
		ctrlr_sts.b.io_cmd_hw_fetch_en = 1;
		nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
	}
}

ddr_code void reset_sq_arbitration_en(void)
{
	u32 sqid;
	for (sqid = 0; sqid < NVMET_RESOURCES_FLEXIBLE_TOTAL; sqid++)
	{
		if(plp_trigger)
		{
			return;
		}

		flex_sq_ctrl_sts0_t sq_ctrl = {
			.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (sqid << 2)),
		};
		//evlog_printk(LOG_TENCENT, "SQ %d sq_ctl 0x%x", sqid, sq_ctrl.all);
		if (sq_ctrl.b.sq_arbitration_en)
		{
			sq_ctrl.b.sq_arbitration_en = 0;
			flex_misc_writel(sq_ctrl.all, FLEX_SQ_CTRL_STS0 + (sqid << 2));
			sq_ctrl.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (sqid << 2));
			sq_ctrl.b.sq_arbitration_en = 1;
			flex_misc_writel(sq_ctrl.all, FLEX_SQ_CTRL_STS0 + (sqid << 2));
		}
	}
}
/*!
 * @brief	nvmet enable to fetch comand after warmboot
 *
 * @param	None
 *
 * @return	None
 */
ddr_code void nvmet_enable_fetched_cmd_warmboot(void)
{
	u32 sqid;

#ifndef NVME_SHASTA_MODE_ENABLE
	for (sqid = 0; sqid < NVMET_RESOURCES_FLEXIBLE_TOTAL; sqid++)
	{
		flex_sq_ctrl_sts0_t sq_ctrl = {
			.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (sqid << 2)),
		};
		if (sq_ctrl.b.sq_arbitration_en)
		{
			hal_nvmet_rebuild_vf_sq(sqid);
		}
	}
#else
	sq_cmd_fetch_enable_t sq_fetch = {
		.all = nvme_readl(SQ_CMD_FETCH_ENABLE),
	};

	for (sqid = 0; sqid <= NVMET_NR_IO_QUEUE; sqid++)
	{
		if ((1 << sqid) & sq_fetch.b.sq_cmd_fetch_enable)
		{
			hal_nvmet_rebuild_sq(sqid);
		}
	}
#endif

	hal_nvmet_rebuild_cq();
	hal_nvmet_rebuild_io_cq();
	hal_nvmet_enable_cmd_fetch();
	hal_nvmet_check_cmd_fetch();
	//reset_sq_arbitration_en();

	if (misc_is_CA3_active()){	//CA3
		evlog_printk(LOG_ALW,"CA3_EN_PP");
		nvmet_csts_PP(false);
	}
/*  //Alan revert 20210831
    else{
        nvmet_csts_rdy(true); //Johnny 20210702
    }
*/


	misc_clear_warm_boot_flag();
#ifdef MEASURE_TIME
	recMsec[2]= *(vu32*)0xC0201044;
	evlog_printk(LOG_ALW,"[M_T]main->cmd_fetch %8d ms\n",(recMsec[2] - recMsec[1])/800000);
#endif
}
#if (PI_FW_UPDATE == mENABLE)
slow_code_ex void nvmet_enable_fetched_cmd_pi(void)
{		//20210110-Eddie
	hal_pi_enable_cmd_fetch();
	//misc_clear_warm_boot_flag();
}
#endif


dtag_t dma_dtag_dbg = (dtag_t)INV_LDA;

static ddr_code void dma_dump_prp_done(void *hcmd, bool error)
{
	u32 *mem = dtag2mem(dma_dtag_dbg);

	evlog_printk(LOG_INFO,"===== dma dump prp done ============= error %x\n", error);
	mem_dump(mem, 64);

	dtag_put(DTAG_T_SRAM, dma_dtag_dbg);
	dma_dtag_dbg.dtag = INV_LDA;
}


ddr_code UNUSED void nvmet_dump_dma_prp(u32 prp_hi, u32 prp_lo, u32 nlb)
{
#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return;
	}
#endif

	u32 *mem;
	dma_dtag_dbg = dtag_get(DTAG_T_SRAM, (void *)&mem);
	if (dma_dtag_dbg.dtag == _inv_dtag.dtag) {
		evlog_printk(LOG_ERR,"No src dtag allocaed, give up\n");
		return;
	}
	evlog_printk(LOG_INFO,"prp %x%x, sram_addr %x len %x, dtag %x\n", (prp_hi), (prp_lo), sram_to_dma(mem), nlb, dma_dtag_dbg.b.dtag);

	evlog_printk(LOG_INFO,"start dma transfer...\n");
	hal_nvmet_data_xfer((u64)(((u64)prp_hi << 32) | prp_lo), mem, DTAG_SZE, READ, NULL, dma_dump_prp_done);

}


/*!
 * @brief	Initialize DMA queues for READ/WRITE
 *
 * @return	None
 */
static void nvmet_init_dma_queues(void)
{
	int i = 0;
	dma_req_t *req;

	if (dma_queue_initialized == true)
		return;

	/* Queue 0 */
	dma_queue0_pointers_t q0_ptrs = {.all = 0};
	dma_queue0_sram_base_t q0_sram_base = {.all = 0};

	/* Queue 1 */
	dma_queue1_pointers_t q1_ptrs = {.all = 0};
	dma_queue1_sram_base_t q1_sram_base = {.all = 0};

	/* initialize Queue 0 for READ */
	dma_queues[0].reqs = dma_reqs;
	for (i = 0; i < (1 << DMA_QUEUE_SHF); i++)
	{
		req = &dma_queues[0].reqs[i];
		req->dw3.all = 0;
	}

	dma_queues[0].qsize = (1 << DMA_QUEUE_SHF) - 1; /* 0 based */
	dma_queues[0].wptr = 0;
	dma_queues[0].rptr = 0;
	dma_queues[0].ctrlr_sts = DMA_QUEUE0_CTRL_STS;
	dma_queues[0].sram_base = DMA_QUEUE0_SRAM_BASE;
	dma_queues[0].queue_ptrs = DMA_QUEUE0_POINTERS;

	dma_queue0_ctrl_sts_t q0_ctrlr_sts = {
		.all = nvme_readl(DMA_QUEUE0_CTRL_STS),
	};
	q0_ctrlr_sts.b.dma_queue0_enable = 0;
	nvme_writel(q0_ctrlr_sts.all, DMA_QUEUE0_CTRL_STS);

	q0_sram_base.b.dma_queue0_sram_base = sram_to_dma(dma_queues[0].reqs);
	q0_sram_base.b.dma_queue0_max_sz = dma_queues[0].qsize;
	nvme_writel(q0_sram_base.all, DMA_QUEUE0_SRAM_BASE);

	q0_ptrs.b.dma_queue0_wptr = dma_queues[0].wptr;
	nvme_writel(q0_ptrs.all, DMA_QUEUE0_POINTERS);

	q0_ctrlr_sts.b.dma_queue0_enable = 1;
	q0_ctrlr_sts.b.dr_done_wait_ram_wdone = 1;
	nvme_writel(q0_ctrlr_sts.all, DMA_QUEUE0_CTRL_STS);

	/* initialize Queue 1 for WRITE */
	dma_queues[1].reqs = dma_reqs + (1 << DMA_QUEUE_SHF);

	for (i = 0; i < (1 << DMA_QUEUE_SHF); i++)
	{
		req = &dma_queues[1].reqs[i];
		req->dw3.all = 0;
		req->dw3.b.dir = 1;
	}

	dma_queues[1].qsize = (1 << DMA_QUEUE_SHF) - 1; /* 0 based */
	dma_queues[1].wptr = 0;
	dma_queues[1].rptr = 0;
	dma_queues[1].ctrlr_sts = DMA_QUEUE1_CTRL_STS;
	dma_queues[1].sram_base = DMA_QUEUE1_SRAM_BASE;
	dma_queues[1].queue_ptrs = DMA_QUEUE1_POINTERS;

	dma_queue1_ctrl_sts_t q1_ctrlr_sts = {
		.all = nvme_readl(DMA_QUEUE1_CTRL_STS),
	};
	q1_ctrlr_sts.b.dma_queue1_enable = 0;
	nvme_writel(q1_ctrlr_sts.all, DMA_QUEUE1_CTRL_STS);

	q1_sram_base.b.dma_queue1_sram_base = sram_to_dma(dma_queues[1].reqs);
	q1_sram_base.b.dma_queue1_max_sz = dma_queues[1].qsize;
	nvme_writel(q1_sram_base.all, DMA_QUEUE1_SRAM_BASE);

	q1_ptrs.b.dma_queue1_wptr = dma_queues[1].wptr;
	nvme_writel(q1_ptrs.all, DMA_QUEUE1_POINTERS);

	q1_ctrlr_sts.b.dma_queue1_enable = 1;
	nvme_writel(q1_ctrlr_sts.all, DMA_QUEUE1_CTRL_STS);

	dma_queue_initialized = true;
}

/*!
 * @brief	Release DMA queues for READ/WRITE
 *
 * @return	None
 */
static void nvmet_fini_dma_queues(void)
{
	int i = 0;
	dma_queue0_ctrl_sts_t ctrlr_sts;
	struct dma_queue_t *queue = &dma_queues[0];

	for (i = 0; i < 2; queue++, i++)
	{
		ctrlr_sts.all = nvme_readl(queue->ctrlr_sts);

		/* Disable DMA Queue */
		ctrlr_sts.b.dma_queue0_enable = 0;
		nvme_writel(ctrlr_sts.all, queue->ctrlr_sts);

		queue->qsize = 0;

		nvme_writel(queue->qsize, queue->sram_base); /* When set to 0, the related function is disabled */
	}

	dma_queue_initialized = false;
}

/*!
 * @brief	Initialize hardware PCIE bar registers
 *
 * @param	regs	Registers for PCIE of controller
 *
 * @return	None
 */
static void nvmet_init_hw_ctrlr_regs(struct nvme_registers *regs)
{
	nvme_writel(regs->cap.raw >> 32, CONTROLLER_CAP_HIGH);
	nvme_writel(regs->cap.raw, CONTROLLER_CAP_LOW);

	nvme_writel(regs->vs.raw, NVME_VERSION);

	nvme_writel(regs->intms, INT_MASK_SET);
	nvme_writel(regs->intmc, INT_MASK_CLEAR);

	nvme_writel(regs->csts.raw, CONTROLLER_STATUS);

	nvme_writel(regs->bpinfo.raw, BOOT_PART_INFO);
}

/*!
 * @brief	get hardware PCIE bar registers
 *
 * @param	regs	Registers for PCIE of controller
 *
 * @return	None
 */
static void nvmet_get_hw_ctrlr_regs(struct nvme_registers *regs)
{
	regs->cap.raw = nvme_readl(CONTROLLER_CAP_HIGH);
	regs->cap.raw = regs->cap.raw << 32;
	regs->cap.raw |= nvme_readl(CONTROLLER_CAP_LOW);

	regs->vs.raw = nvme_readl(NVME_VERSION);

	regs->intms = nvme_readl(INT_MASK_SET);
	regs->intmc = nvme_readl(INT_MASK_CLEAR);

	regs->cc.raw = nvme_readl(CONTROLLER_CONFG);
	regs->csts.raw = nvme_readl(CONTROLLER_STATUS);

	regs->bpinfo.raw = nvme_readl(BOOT_PART_INFO);
}

#if defined(SRIOV_SUPPORT)
static void nvmet_init_vf_hw_ctrlr_regs(struct nvme_vf_registers *regs, u8 fun)
{
	nvme_vf_writel(regs->cap.raw >> 32, fun - 1, CONTROLLER_CAP_HIGH);
	nvme_vf_writel(regs->cap.raw, fun - 1, CONTROLLER_CAP_LOW);
}

static void nvmet_get_vf_hw_ctrlr_regs(struct nvme_vf_registers *regs, u8 fun)
{
	regs->cap.raw = nvme_vf_readl(fun - 1, CONTROLLER_CAP_HIGH);
	regs->cap.raw = regs->cap.raw << 32;
	regs->cap.raw |= nvme_vf_readl(fun - 1, CONTROLLER_CAP_LOW);
}

/*!
 * @brief This function initializes a number of VF HW controllered nvme
 * registers.
 *
 * @param vf_funs  number of VF functions
 *
 * @return		None
 */
static void nvmet_init_n_vf_hw_ctrlr_regs(u8 vf_funs)
{
	struct nvme_vf_registers *vf_reg;
	u32 i;

	for (i = 0; i < vf_funs; i++)
	{
		vf_reg = (nvmet_vf_regs + i);
		vf_reg->cap.bits.cqr = 1;
		vf_reg->cap.bits.mqes = 255;   /* ASIC I/O support up to 256 */
		vf_reg->cap.bits.mpsmax = 1;   /* max 8K */
		vf_reg->cap.bits.mpsmin = 0;   /* min 4K */
		vf_reg->cap.bits.css = BIT(0); /* NVM command set only */
		vf_reg->cap.bits.nssrs = 1;	   /* Support NSSR */
		vf_reg->cap.bits.bps = 0;
		vf_reg->cap.bits.to = 100;
		vf_reg->cap.bits.ams = 1;
		vf_reg->cap.bits.dstrd = 0;

		nvmet_init_vf_hw_ctrlr_regs(vf_reg, i + 1);
	}
}
#endif

/*!
 * @brief initialize admin queue, set ASQ/ACQ queue base/qsize
 *
 * @param aqa	nvme_aqa_register controller register
 * @param asqb	ASQ base address
 * @param acqb	ACQ base address
 *
 * @return		None
 *
 * @note
 *  Because our ASIC can parse ASQB & ACQB, ignore the parameters.
 *  and ASIC supports reuse nvme_cmd entries & empty/full policy, so ASIC
 *  can use its own queth depth.
 *
 *  ACQ interrupt is enabled by default.
 */
static void
nvmet_init_aqa_attrs(union nvme_aqa_register aqa, u64 asqb, u64 acqb)
{
	union nvme_aqa_register aqa_shasta = {
		/* up to 4K for SPEC but Rainier supports 8 due to limited resource */
		.bits = {
			.asqs = IO_SQUEUE_MAX_DEPTH - 1,
			.acqs = IO_CQUEUE_MAX_DEPTH - 1,
		}};

	fw_cq_reqs_sram_addr_t acq_base = {
		.all = 0,
	};

	nvme_hal_trace(LOG_DEBUG, 0xa98a, "ASQ/ACQ (0x%x%x/0x%x%x)",
				   nvme_readl(ADMIN_SQ_BASE_ADDR_H),
				   nvme_readl(ADMIN_SQ_BASE_ADDR_L),
				   nvme_readl(ADMIN_CQ_BASE_ADDR_H),
				   nvme_readl(ADMIN_CQ_BASE_ADDR_L));

	nvme_hal_trace(LOG_DEBUG, 0xa4d1, "ASQ/ACQ Qdepth (%d/%d) -> (%d/%d)",
				   aqa.bits.asqs, aqa.bits.acqs, aqa_shasta.bits.asqs,
				   aqa_shasta.bits.acqs);

	aqa = aqa_shasta;
	/* set up Admin Submit Queue buffers in PF for SHASTA, NORMAL and SRIOV modes */
	u32 i;
	for (i = 0; i <= aqa.bits.asqs; i++)
		cmd_base[0][i] = nvme_cmd_get(__FUNCTION__);

	sq0_cmd_sram_base_t asq_base = {
		.all = nvme_readl(SQ0_CMD_SRAM_BASE),
	};
	asq_base.b.adm_cmd_sram_base = btcm_to_dma(cmd_base[0]);
	asq_base.b.addr_is_for_dtcm_2100 = 1;
	asq_base.b.adm_cmd_fetch_max_sz = aqa.bits.asqs;
	nvme_writel(asq_base.all, SQ0_CMD_SRAM_BASE);

	sq_base[0].reg_cmd_fetch_pnter = SQ0_CMD_FETCH_PTRS;
	sq_base[0].qsize = aqa.bits.asqs;
	sq_base[0].cmd_fetch_rptr = 0;
	nvme_writel(sq_base[0].cmd_fetch_rptr, sq_base[0].reg_cmd_fetch_pnter);

	/* ACQ: All completion queue shares ACQ */
	acq_base.b.fw_cq_sram_addr = btcm_to_dma(nvme_cmpl_base);
	acq_base.b.addr_is_for_dtcm = 1;

	fw_cq_reqs_size_t acq_sz = {
		.b.fw_cq_reqs_max_sz = aqa.bits.acqs,
	};

	nvme_writel(acq_sz.all, FW_CQ_REQS_SIZE);
	nvme_writel(acq_base.all, FW_CQ_REQS_SRAM_ADDR);

	cq_base[0].qsize = aqa.bits.acqs;
	cq_base[0].wptr = 0;
	cq_base[0].base = nvme_cmpl_base;
	nvme_writel(cq_base[0].wptr, FW_CQ_REQS_POINTERS);

	/* Initialize SQ/CQ Doorbell */
	sq0_db_reg_t sqdb_ctrls = {
		.all = nvme_readl(SQ0_DB_REG),
	};
	sqdb_ctrls.b.sq0_tail_ptr = 0;
	sqdb_ctrls.b.sq0_head_ptr = 0;
	sqdb_ctrls.b.cq_id_for_sq0 = 0;
	nvme_writel(sqdb_ctrls.all, SQ0_DB_REG);

	cq0_db_reg_t cqdb_ctrls = {
		.all = nvme_readl(CQ0_DB_REG),
	};
	cqdb_ctrls.b.cq0_head_ptr = 0;
	cqdb_ctrls.b.cq0_tail_ptr = 0;
	cqdb_ctrls.b.cq0_phase_tag = 1;
	nvme_writel(cqdb_ctrls.all, CQ0_DB_REG);

	admin_queue_bitmap_t que_bit = {
		.all = nvme_readl(ADMIN_QUEUE_BITMAP),
	};
	que_bit.b.admin_queue_bitmap |= 0x1;
	nvme_writel(que_bit.all, ADMIN_QUEUE_BITMAP);
	/* Enable Arbitration */
	sq_arbitration_en_t sq_arb = {
		.all = nvme_readl(SQ_ARBITRATION_EN),
	};
	sq_arb.b.sq0_enable = 1;
	nvme_writel(sq_arb.all, SQ_ARBITRATION_EN);

	arbitration_control_sts_t arb_ctrls = {
		.all = nvme_readl(ARBITRATION_CONTROL_STS),
	};
	arb_ctrls.b.arbitration_enable = 1;
	arb_ctrls.b.rsvd_1 = 0;
	arb_ctrls.b.rsvd_20 = 0;
	nvme_writel(arb_ctrls.all, ARBITRATION_CONTROL_STS);

	nvme_axi_interface_ctrl_t nvme_axi = {
		.all = nvme_readl(NVME_AXI_INTERFACE_CTRL),
	};
	arbitration_control_sts_t arb_ctrls1 = {
		.all = nvme_readl(ARBITRATION_CONTROL_STS),
	};

	arb_ctrls1.b.arbitration_mode = 1;

	if (nvme_axi.b.nvme_pcie_max_rd_128b == 1)
		arb_ctrls1.b.arbitration_burst = 1;
	else
		arb_ctrls1.b.arbitration_burst = 2;
	nvme_writel(arb_ctrls1.all, ARBITRATION_CONTROL_STS);

	/*
	 * DMA Slot(SQ/CQ command/completion fetch) enable
	 * DMA slot(s) are mainly controlled by HW, but FW
	 * can also use this slot when the related hardware is disabled.
	 */
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.adm_cmd_hw_fetch_en = 1; /* Enable Admin command fetch */

	ctrlr_sts.b.io_cmd_push_en = 1; /* Shasta mode, fw will receive all the io cmd */
#if defined(SRIOV_SUPPORT)
	ctrlr_sts.b.nvme_op_mode = NVME_OP_MODE_SRIOV;
	ctrlr_sts.b.io_cmd_hw_fetch_en = 1;
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
#else
#ifndef NVME_SHASTA_MODE_ENABLE
	ctrlr_sts.b.nvme_op_mode = NVME_OP_MODE_BASIC; /* NVM-set mode. SR-IOV resource per FW setting */
#endif
	ctrlr_sts.b.io_cmd_hw_fetch_en = 1;
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
#endif

	/* Enable Command Fetch */
	sq_cmd_fetch_enable_t sq_fetch_enable = {
		.all = nvme_readl(SQ_CMD_FETCH_ENABLE),
	};
	sq_fetch_enable.b.sq_cmd_fetch_enable |= BIT(0);
	nvme_writel(sq_fetch_enable.all, SQ_CMD_FETCH_ENABLE);

	/* Enable CQ transfer */
	fw_cq_reqs_ctrl_sts_t cq_ctrls = {
		.all = nvme_readl(FW_CQ_REQS_CTRL_STS),
	};
	cq_ctrls.b.fw_cq_reqs_xfer_en = 1;
	nvme_writel(cq_ctrls.all, FW_CQ_REQS_CTRL_STS);

	nvme_writel(CQ0_INTERRUPT_EN_MASK, CQ_INTERRUPT_EN);
}

/*!
 * @brief	disable admin queue resource
 *
 * @return	None
 */
static void _nvmet_fini_aqa_attrs(void)
{
	fw_cq_reqs_ctrl_sts_t cq_ctrls = {
		.all = nvme_readl(FW_CQ_REQS_CTRL_STS),
	};
	cq_ctrls.b.fw_cq_reqs_xfer_en = 0;
	nvme_writel(cq_ctrls.all, FW_CQ_REQS_CTRL_STS);

	/* Disable Arbitration */
	sq_arbitration_en_t sq_arb = {
		.all = nvme_readl(SQ_ARBITRATION_EN),
	};
	sq_arb.b.sq0_enable = 0;
	nvme_writel(sq_arb.all, SQ_ARBITRATION_EN);

	arbitration_control_sts_t arb_ctrls = {
		.all = nvme_readl(ARBITRATION_CONTROL_STS),
	};
	arb_ctrls.b.arbitration_enable = 0;
	nvme_writel(arb_ctrls.all, ARBITRATION_CONTROL_STS);

	/* Disable Command Fetch */
	sq_cmd_fetch_enable_t sq_fetch = {
		.all = nvme_readl(SQ_CMD_FETCH_ENABLE),
	};
	sq_fetch.b.sq_cmd_fetch_enable = 0; /* Disable all */
	nvme_writel(sq_fetch.all, SQ_CMD_FETCH_ENABLE);

	/* Disable Controller Engine */
	nvme_ctrl_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_CTRL_STATUS),
	};
	ctrlr_sts.b.adm_cmd_hw_fetch_en = 0; /* Stop Fetch Admin Cmd */
	ctrlr_sts.b.io_cmd_hw_fetch_en = 0;	 /* Stop Fetch I/O Cmd */
	nvme_writel(ctrlr_sts.all, NVME_CTRL_STATUS);
}

/*!
 * @brief	release admin queue resource
 *
 * @return	None
 */
static void nvmet_fini_aqa_attrs(void)
{
	u32 i = 0;

	for (i = 0; i < IO_SQUEUE_MAX_DEPTH; i++)
	{
		if (cmd_base[0][i])
		{
			hal_nvmet_put_sq_cmd(nvme_cmd_cast(cmd_base[0][i]));
			cmd_base[0][i] = 0;
		}
	}

	/* Disable CQ transfer */
	_nvmet_fini_aqa_attrs();
}

#if defined(SRIOV_SUPPORT)
/*!
 * @brief release admin queue resource
 *
 * @return	None
 */
static void nvmet_fini_aqa_attrs_vf(u8 fid)
{
	u32 i = 0;

	for (i = 0; i < IO_SQUEUE_MAX_DEPTH; i++)
	{
		if (cmd_base[fid][i])
		{
			hal_nvmet_put_sq_cmd(nvme_cmd_cast(cmd_base[fid][i]));
			cmd_base[fid][i] = 0;
		}
	}

	/* Disable CQ transfer */
	_nvmet_fini_aqa_attrs();
}
#endif

/*!
 * @brief	data transfer done routine, and it will notify upper layer
 *
 * @param	dma_queue_id	DMA queue id, 0 for READ and 1 for WRITE
 *
 * @return	bool	pending request exists
 */
fast_code static bool nvmet_dma_queue_done(u32 dma_queue_id)
{
	struct dma_queue_t *queue = &dma_queues[dma_queue_id];
	dma_queue0_pointers_t ptrs = {
		.all = nvme_readl(queue->queue_ptrs),
	};
	u32 err_ptr = ~0;
	dma_queue0_ctrl_sts_t sts = {.all = nvme_readl(queue->ctrlr_sts)};

	if (sts.b.dma_queue0_xfer_err)
	{
		nvme_hal_trace(LOG_ERR, 0x9c5c, "DMA queue %d xfer error %x, ptr %x", dma_queue_id,
					   sts.b.dma_queue0_error_code, ptrs.all);
		err_ptr = ptrs.b.dma_queue0_rptr;
		if (err_ptr == 0)
			err_ptr = queue->qsize;
		else
			err_ptr -= 1;
	}

	/*
	 * Move read pointer indicates the related DMA request is done
	 */
	while (ptrs.b.dma_queue0_rptr != queue->rptr)
	{
		void (*callback)(void *hcmd, bool error) = dma_queues_opaque[dma_queue_id][queue->rptr].callback;
		void *req = dma_queues_opaque[dma_queue_id][queue->rptr].hcmd;
        if(!plp_trigger){
    		if (callback)
    		{
    			if (err_ptr == ~0 || err_ptr != queue->rptr)
    			{
    				callback(req, false);
    			}
    			else
    			{
    				callback(req, true);
    				nvme_writel(sts.all, queue->ctrlr_sts);
    			}
    		}
        }

		queue->rptr = (queue->rptr + 1) & queue->qsize;

		/* Since No update to rptr, we need check all outstanding request */
		/* try new DMA queue handling process, disable this workaround */
		//ptrs.all = nvme_readl(queue->queue_ptrs);
	}

	return (queue->rptr != queue->wptr);
}

/*!
 * @brief	configure controller ready status
 *
 * @param rdy	update controller's status, caution!
 *
 * @return	None
 */
static void nvmet_csts_rdy(bool rdy)
{
	union nvme_csts_register csts = {
		.raw = nvme_readl(CONTROLLER_STATUS),
	};

	/* To start executing commands on controller after a shutdown
	 * operation, a CC.EN cleared to '0' is required.
	 *
	 * If host software submits commands to the controller without a reset,
	 * the behavior is undefined.
	 */
	if (nvmet_regs.csts.bits.rdy == 1 && rdy == 1){	//20210426-Eddie
		if (_fg_wb_reboot == true){	//Don't panic since ca1&2 need reboot
			_fg_wb_reboot = false;
		}
		else
		panic("Submit a CC.EN=0 to reset after SHST completion.\n");
	}

	csts.bits.rdy = rdy ? 1 : 0;

	nvme_writel(csts.raw, CONTROLLER_STATUS);
	nvmet_regs.csts = csts;
}

#if defined(SRIOV_SUPPORT)
/*!
 * @brief configure controller ready status
 *
 * @param rdy	update controller's status, caution!
 *
 * @return		None
 */
static void nvmet_vf_csts_rdy(bool rdy, u8 vf_idx)
{
	u8 vfn = vf_idx - 1;
	union nvme_csts_register csts = {
		.raw = vf_readl(CONTROLLER_STATUS + (vfn << 5)),
	};

	/* To start executing commands on controller after a shutdown
	 * operation, a CC.EN cleared to '0' is required.
	 *
	 * If host software submits commands to the controller without a reset,
	 * the behavior is undefined.
	 */

	csts.bits.rdy = rdy ? 1 : 0;

	vf_writel(csts.raw, CONTROLLER_STATUS + (vfn << 5));
	nvmet_vf_regs[vfn].csts = csts;
}

/*!
 * @brief configure controller shutdown status
 *
 * @param shst	shutdown status
 *
 * @return		None
 */
static void nvmet_csts_shst(u8 fun_id, enum nvme_shst_value shst)
{
	union nvme_csts_register csts;
	if (fun_id == 0)
	{
		csts.raw = nvme_readl(CONTROLLER_STATUS);

		csts.bits.shst = shst;
		nvme_writel(csts.raw, CONTROLLER_STATUS);
		nvmet_regs.csts = csts;
	}
	else if (fun_id < 3)
	{
		csts.raw = vf_readl(CONTROLLER_STATUS + ((fun_id - 1) << 5));

		csts.bits.shst = shst;
		vf_writel(csts.raw, CONTROLLER_STATUS + ((fun_id - 1) << 5));
		nvmet_vf_regs[fun_id - 1].csts = csts;
	}
	else
	{
		panic("Not tested 3 VFs or more now\n");
	}
}
#endif

#if !defined(SRIOV_SUPPORT)
/*!
 * @brief configure controller shutdown status
 *
 * @param shst	shutdown status
 *
 * @return		None
 */
static void nvmet_csts_shst(enum nvme_shst_value shst)
{
	union nvme_csts_register csts = {
		.raw = nvme_readl(CONTROLLER_STATUS),
	};

	csts.bits.shst = shst;
	nvme_writel(csts.raw, CONTROLLER_STATUS);
	nvmet_regs.csts = csts;
}
#endif

/*!
 * @brief	enable cq req done isr
 *
 * @param	None
 *
 * @return	None
 */
static void nvmet_enable_cq_req_done_isr(void)
{
	u32 val = nvme_readl(NVME_INT_MASK);
	val &= ~(1 << FW_CQ_REQ_DONE_SHIFT);
	nvme_writel(val, NVME_INT_MASK);
}

/*!
 * @brief	disable cq req done isr
 *
 * @param	None
 *
 * @return	None
 */
static void nvmet_disable_cq_req_done_isr(void)
{
	u32 val = nvme_readl(NVME_INT_MASK);
	val |= (1 << FW_CQ_REQ_DONE_SHIFT);
	nvme_writel(val, NVME_INT_MASK);
}

/*!
 * @brief	enable nvme interrupt
 *
 * @return	None
 *
 * @note	Only FW_CQ_REQ_DONE_MASK and QUIRK_INTERRUPTS will not be enabled
 */
static fast_code void nvmet_enable_irq(void)
{
	nvme_writel(MASKED_INTERRUPTS, NVME_INT_MASK);
}

/*!
 * @brief	disable nvme interrupt
 *
 * @return	None
 */
static fast_code void nvmet_disable_irq(void)
{
	nvme_writel(~0x0, NVME_INT_MASK);
}

/*!
 * @brief	disable dma done interrupt
 *
 * @return	None
 */
static fast_code void nvmet_disable_dma_done_irq(void)
{
	u32 val = nvme_readl(NVME_INT_MASK);
	val |= DMA_QUEUE1_XFER_DONE_MASK | DMA_QUEUE0_XFER_DONE_MASK;
	nvme_writel(val, NVME_INT_MASK);
}

/*!
 * @brief	enable dma done interrupt
 *
 * @return	None
 */
static fast_code void nvmet_enable_dma_done_irq(void)
{
	u32 val = nvme_readl(NVME_INT_MASK);
	val &= ~(DMA_QUEUE1_XFER_DONE_MASK | DMA_QUEUE0_XFER_DONE_MASK);
	nvme_writel(val, NVME_INT_MASK);
}

/*!
 * @brief	re-initialize registers function
 *
 * @return	None
 */
static fast_code void nvmet_regs_reinit(void)
{
	/* for tnvme 1:0.0.0, re-init VS regs to 0 */
	nvmet_regs.vs_idir_addr.addr = 0;
	nvmet_regs.vs_idir_data.data = 0;
	nvme_writel(nvmet_regs.vs_idir_addr.addr, VS_IDIR_ADDR);
	nvme_writel(nvmet_regs.vs_idir_data.data, VS_IDIR_DATA);
}

/*!
 * @brief configure NVMe AXI transfer payload size
 *
 * @return	None
 */
static fast_code void nvmet_config_xfer_payload(void)
{
	nvme_axi_interface_ctrl_t nvme_axi;
	u32 pcie_mps; /* PCIE Max payload size */
	u32 pcie_mrr; /* PCIE MAX Read request size */
	u32 cmd_p_max_pl = 0;
	u32 cmd_p_max_rd = 0;

	pcie_set_xfer_busy(true);

	/* confirm pcie reference clock */
	if (is_pcie_clk_enable())
	{
		pcie_mps = get_pcie_xfer_mps();
		pcie_mrr = get_pcie_xfer_mrr();
	}
	else
	{
		nvme_hal_trace(LOG_ERR, 0x09c7, " No pcie clock ");
		pcie_mps = 0;
		pcie_mrr = 0;
	}

	nvme_axi.all = nvme_readl(NVME_AXI_INTERFACE_CTRL);
	/*
	 * Set maximum read-req size = min { MPS , MRR}
	 * Set maximum payload size = MPS
	 */
#if (MRR_EQUAL_MPS == ENABLE)
	pcie_core_status2_t link_sts;
	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
	if(link_sts.b.neg_link_speed == PCIE_GEN2)
	{
		pcie_mrr = pcie_mps;
		nvme_hal_trace(LOG_ERR, 0xf6fb, "Gen2, Force MRR equal to MPS (%d)",pcie_mps);
	}
#endif
	nvme_hal_trace(LOG_ALW, 0xc4fd, "PCIE mps %d mrr %d", pcie_mps, pcie_mrr);
	if (pcie_mps == 0)
	{ /* [MPS] 128 bytes */
		nvme_axi.b.nvme_pcie_max_wr_128b = 1;
		cmd_p_max_pl = 0;

		nvme_axi.b.nvme_pcie_max_rd_128b = 1; /* (MRR) 128 bytes */
		cmd_p_max_rd = 0;
	}
	else if (pcie_mps == 1 || pcie_mps == 2)
	{ /* [MPS] 256 bytes or 512 bytes*/
		nvme_axi.b.nvme_pcie_max_wr_128b = 0;
		cmd_p_max_pl = pcie_mps;

		if (pcie_mrr == 0)
		{ /* (MRR) 128 bytes */
			nvme_axi.b.nvme_pcie_max_rd_128b = 1;
			cmd_p_max_rd = 0;
		}
		else if (pcie_mrr == 1 || pcie_mrr == 2)
		{ /* (MRR) 256 bytes */
			nvme_axi.b.nvme_pcie_max_rd_128b = 0;
			cmd_p_max_rd = pcie_mrr;
		}
		else
		{
			cmd_p_max_rd = 3;
		}
	}
	else
	{
		nvme_axi.b.nvme_pcie_max_wr_128b = 0;
		nvme_axi.b.nvme_pcie_max_rd_128b = 0;
		cmd_p_max_pl = 3;
		if (pcie_mrr < 3)
			cmd_p_max_rd = pcie_mrr;
		else
			cmd_p_max_rd = 3;
	}

    #ifdef MPS128
	cmd_p_max_pl = 0;
	cmd_p_max_rd = 0;
    #endif

	/* configure nvme axi */
	nvme_writel(nvme_axi.all, NVME_AXI_INTERFACE_CTRL);

	/* configure cmd_proc */
	cmd_proc_set_payload_sz(cmd_p_max_pl, cmd_p_max_rd);

	pcie_set_xfer_busy(false);
}

fast_code void hal_nvmet_kickoff(void)
{
	nvmet_enable_irq();
}

fast_code void hal_nvmet_io_stop(void)
{
	nvmet_disable_irq();
}

/*!
 * @brief	wait NVMe CQ cpl transfer status idle
 *
 * @para	none
 *
 * @return	None
 */
fast_code void nvme_hal_wait_cq_xfer_idle(void)
{
	cmpl_gen_dbg_info_t cmpl_info;

	cmpl_info.all = nvme_readl(CMPL_GEN_DBG_INFO);
	while (cmpl_info.b.cmpl_gen_dbg_info & 0x000000FF) {
		nvme_hal_trace(LOG_WARNING, 0x65c2, "NVMe CQ cpl transfer status not idle reg: 0x%x",
				cmpl_info.b.cmpl_gen_dbg_info);
		cmpl_info.all = nvme_readl(CMPL_GEN_DBG_INFO);
	}
}

#if EPM_NOT_SAVE_Again
extern u8 EPM_NorShutdown;
#endif
/*!
 * @brief callback function for shutdown 2000 interrut
 *
 * @return	not used
 */
#if defined(SRIOV_SUPPORT)
#ifdef EVT_DELAY_ARRAY
fast_code void shutdown2000(u8 fid)
#else
static void shutdown2000(u8 fid)
#endif
{
	u32 i, j;
#if 0
#if (TRIM_SUPPORT == ENABLE)
	extern void TrimPowerLost();
	TrimPowerLost();
#endif
#endif

	nvmet_csts_shst(fid, NVME_SHST_COMPLETE);
#if EPM_NOT_SAVE_Again
	EPM_NorShutdown = 1;
#endif
	/* Reset SQ/CQ DB registers of function fid */
	if (fid == 0)
	{
		for (i = 0; i < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL; i++)
		{
			flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
			flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
		}
	}
	else
	{
		j = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL + (fid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
		for (i = j; i < (j + SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC); i++)
		{
			flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
			flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
		}
	}
}
#else
#ifdef EVT_DELAY_ARRAY
fast_code void shutdown2000(void)
#else
static void shutdown2000(void)
#endif
{
	// If any reset flow trigger, shutdown bit should be clear. 12/14 Richard modify for PCBasher
	if((gResetFlag & BIT(cNvmeShutDown)) == 0)
	{
		nvme_hal_trace(LOG_ERR, 0x23cb, "bypass shutdown2000");
		nvmet_csts_shst(NVME_SHST_NORMAL);
	}
	else
	{
#if 0
#if (TRIM_SUPPORT == ENABLE)
	extern void TrimPowerLost();
	TrimPowerLost();
#endif
#endif
	nvmet_csts_shst(NVME_SHST_COMPLETE);
#if EPM_NOT_SAVE_Again
	EPM_NorShutdown = 1;
#endif
	nvme_hal_trace(LOG_ERR, 0x0e41, "NVME_SHST_COMPLETE\n");
#ifndef NVME_SHASTA_MODE_ENABLE
	//flex_misc_writel(0, ADM_CMD_FETCH_EN0_VF);
	//flex_misc_writel(0, ADM_CMD_FETCH_EN1_VF);
	int i;
	for (i = 1; i < NVMET_RESOURCES_FLEXIBLE_TOTAL; i++)
	{
		flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
		flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
	}
#endif
#if CO_SUPPORT_DEVICE_SELF_TEST
		DST_Completion(cDSTAbortReset);
#endif

		BEGIN_CS1
		gResetFlag &= ~BIT(cNvmeShutDown);
		END_CS1
	}
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_forcestartup();
	#endif
}
#endif

/*!
 * @brief callback function for cc.en 1 -> 0
 *
 * @return	none
 */
#if defined(SRIOV_SUPPORT)
#ifdef EVT_DELAY_ARRAY
fast_code void shutdown(u8 fid)
#else
fast_code static void shutdown(u8 fid)
#endif
{
	u32 i;

	/* For PF0 and VF1 to VF32 */
	/* No SHST has been requested */
	nvmet_csts_shst(fid, NVME_SHST_NORMAL);

	if (fid == 0)
	{
		nvmet_fini_aqa_attrs();
		nvmet_fini_dma_queues();

		nvme_writel(0, INT_MASK_SET);
		nvme_writel(0, CONTROLLER_CONFG);
		/*
		 * When this field is clear to '0', the CSTS.RDY bit is cleared to '0'
		 * by the controller once the controller is ready to be re-enabled.
		 */
		nvmet_csts_rdy(false);

		hal_nvmet_reset_normal_mode();
		flex_misc_writel(0, ADM_CMD_FETCH_EN_PF);
		flex_misc_writel(0, ADM_CMD_FETCH_EN_VF);

		for (i = 0; i < NVMET_RESOURCES_FLEXIBLE_TOTAL; i++)
		{
			flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
			flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
		}
	}
	else
		nvmet_fini_aqa_attrs_vf(fid);
}
#else
#ifdef EVT_DELAY_ARRAY
fast_code void shutdown(void)
#else
fast_code static void shutdown(void)
#endif
{
	/*
	/ because of flush and so on, cpu1 can't do this func quickly
	/ rdy bit keep 1,and host send create io sq/cq again
	/ then do cc_en_reset flow to delete sq/cq for next cc.en set and set number of queue cmd
	*/
	if(flagtestS && flagtestC)
	{
		nvme_hal_trace(LOG_ERR, 0x766b, "delete Q not clean or maybe get admin cmd again after cc_clr");
		nvmet_cc_en_reset();
		return;
	}
	nvmet_csts_shst(NVME_SHST_NORMAL); /* No SHST has been requested */

	nvmet_fini_aqa_attrs();
	nvmet_fini_dma_queues();

	nvme_writel(0, INT_MASK_SET);
	nvme_writel(0, CONTROLLER_CONFG);
	/*
	 * When this field is clear to '0', the CSTS.RDY bit is cleared to '0'
	 * by the controller once the controller is ready to be re-enabled.
	 */
	nvmet_csts_rdy(false);
#ifndef NVME_SHASTA_MODE_ENABLE
	hal_nvmet_delete_vq_sq(0);
	hal_nvmet_delete_vq_cq(0);
	hal_nvmet_reset_normal_mode();
	flex_misc_writel(0, ADM_CMD_FETCH_EN_PF);
	flex_misc_writel(0, ADM_CMD_FETCH_EN_VF);

	int i;
	for (i = 0; i < NVMET_RESOURCES_FLEXIBLE_TOTAL; i++)
	{
		flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
		flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
	}
#else
	hal_nvmet_delete_sq(0);
	hal_nvmet_delete_cq(0);
#endif

	BEGIN_CS1
	gResetFlag &= ~BIT(cNvmeControllerResetClr);
	END_CS1
#if CO_SUPPORT_DEVICE_SELF_TEST
	DST_Completion(cDSTAbortReset);
#endif

	nvme_hal_trace(LOG_INFO, 0xe174, "[RSET] CSTS.RDY = 0");
}
#endif

void _shutdown(void)
{
#if defined(SRIOV_SUPPORT)
	/* TODO: Now call for PF0. Need to improve for VF1 to VF32 */
	nvmet_csts_shst(0, NVME_SHST_NORMAL); /* No SHST has been requested */
#else
	nvmet_csts_shst(NVME_SHST_NORMAL); /* No SHST has been requested */
#endif
	_nvmet_fini_aqa_attrs();
	nvme_writel(0, INT_MASK_SET);
	nvme_writel(0, CONTROLLER_CONFG);

	nvmet_csts_rdy(false);
#ifndef NVME_SHASTA_MODE_ENABLE
	flex_misc_writel(0, ADM_CMD_FETCH_EN_PF);
	flex_misc_writel(0, ADM_CMD_FETCH_EN_VF);

	int i;
	for (i = 0; i < NVMET_RESOURCES_FLEXIBLE_TOTAL; i++)
	{
		flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
		flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
	}
#endif
}

#if (CO_SUPPORT_NSSR_PERST_FLOW == FEATURE_NOT_SUPPORTED)
/*!
 * @brief callback function for NVM subsystem reset callback
 *
 * @return	not used
 */
#if defined(SRIOV_SUPPORT)
static void nvm_subsystem_reset(u8 fid)
{
	/* set nssro to 1 */
	if (fid != 0)
	{
		u8 vfn = fid - 1;
		union nvme_csts_register csts = {
			.raw = vf_readl(CONTROLLER_STATUS + (vfn << 5)),
		};
		csts.bits.csts_nssro = 1;
		vf_writel(csts.raw, CONTROLLER_STATUS + (vfn << 5));
		nvmet_vf_regs[vfn].csts = csts;
	}
	else
	{
		controller_status_t csts = {
			.all = nvme_readl(CONTROLLER_STATUS),
		};
		csts.b.csts_nssro = 1;
		nvmet_regs.csts.bits.nssro = 1;
		nvme_writel(csts.all, CONTROLLER_STATUS);
	}

	shutdown(fid);

    extern void pcie_rst_post(u32 reason);
    // configure NVMe HW to disable SQE_fetching / CQE_returning
    misc_nvm_ctrl_reset(true);

    // configure cmd_proc to abort DMA xfer
    // continue to fetch DTAG from BTN and directly release DTAG to BTN
    hal_nvmet_abort_xfer(true);

    // trigger main reset task before release PCIe link training
    if (evt_perst_hook != 0xff)
        urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST_NSSR);

}
#else
static void nvm_subsystem_reset(void)
{
			/* set nssro to 1 */
	controller_status_t csts = {
		.all = nvme_readl(CONTROLLER_STATUS),
	};
	csts.b.csts_nssro = 1;
	nvmet_regs.csts.bits.nssro = 1;
	nvme_writel(csts.all, CONTROLLER_STATUS);

	shutdown();

    extern void pcie_rst_post(u32 reason);
    // configure NVMe HW to disable SQE_fetching / CQE_returning
    misc_nvm_ctrl_reset(true);

    // configure cmd_proc to abort DMA xfer
    // continue to fetch DTAG from BTN and directly release DTAG to BTN
    hal_nvmet_abort_xfer(true);

    // trigger main reset task before release PCIe link training
    if (evt_perst_hook != 0xff)
        urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST);

}
#endif
#endif
/*!
 * @brief This function clear the PF0 disable active bit.
 *
 * @param	None
 *
 * @return	None
 *
 */
static void fast_code nvmet_clear_pf0_disable_active(void)
{
	pf_ctrlr_dis_act_done_t pf_dis;

	u32 flags = irq_save();
	do {
		pf_dis.all = nvme_readl(PF_CTRLR_DIS_ACT_DONE);
		if (pf_dis.b.pf_controller_disable_active == 0) {
			irq_restore(flags);
			return;
		}
	} while (pf_dis.b.pf_controller_disable_done == 0);

	pf_dis.b.pf_controller_disable_active = 1;
	nvme_writel(pf_dis.all, PF_CTRLR_DIS_ACT_DONE);
	irq_restore(flags);
}

slow_code void nvmet_sq_err_report(void)
{
	/* sq */
#ifndef NVME_SHASTA_MODE_ENABLE
	u32 sq_id;
	flex_sq_ctrl_sts0_t sq_ctrl;
	flex_sqcq0_size_t flex_sqcq0_size;
	for (sq_id = 0; sq_id < NVMET_RESOURCES_FLEXIBLE_TOTAL; sq_id++)
	{
		sq_ctrl.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (sq_id << 2));
		if (sq_ctrl.b.sq_tail_update_err)
		{
			flex_sqcq0_size.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (sq_id << 2));
			if (flex_sqcq0_size.b.sq_size == 0)
				nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_SQ, sq_id);
			else
				nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_DB_WR, sq_id);
			flex_misc_writel(sq_ctrl.all, FLEX_SQ_CTRL_STS0 + (sq_id << 2));
		}
	}
#else
	sq_pointer_updt_errs_t s_errs = {
		.all = nvme_readl(SQ_POINTER_UPDT_ERRS),
	};

	if (s_errs.b.sq_tail_updt_err)
		nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_SQ, s_errs.b.sq_tail_updt_err);

	/* RW1C */
	nvme_writel(s_errs.all, SQ_POINTER_UPDT_ERRS);
#endif

	/* cq */
#ifndef NVME_SHASTA_MODE_ENABLE
	u32 cq_id;
	flex_cq_ctrl_sts0_t cq_ctrl;
	for (cq_id = 0; cq_id < NVMET_RESOURCES_FLEXIBLE_TOTAL; cq_id++)
	{
		cq_ctrl.all = flex_misc_readl(FLEX_CQ0_DB_REG + (cq_id << 2));
		if (cq_ctrl.b.cq_head_update_err)
		{
			flex_sqcq0_size.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (cq_id << 2));
			if (flex_sqcq0_size.b.cq_size == 0)
				nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_SQ, cq_id);
			else
				nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_DB_WR, cq_id);
			flex_misc_writel(cq_ctrl.all, FLEX_CQ0_DB_REG + (cq_id << 2));
		}
	}
#else
	cq_pointer_updt_errs_t c_errs = {
		.all = nvme_readl(CQ_POINTER_UPDT_ERRS),
	};

	if (c_errs.b.cq_head_updt_err)
		nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_DB_WR, c_errs.b.cq_head_updt_err);

	nvme_writel(c_errs.all, CQ_POINTER_UPDT_ERRS);
#endif
}

fast_code static void nvmet_func_disable(u8 fid, void *ctx, cmd_proc_dis_func_cb_t cb)
{
	u32 del_sq_bmp[3] = {0, 0, 0};

	nvmet_clear_io_queue(fid, del_sq_bmp);
	cmd_proc_disable_function(fid, del_sq_bmp, ctx, cb);
}

fast_code static void nvmet_dis_func_done(void *ctx, u8 fid)
{
	evt_set_imt(evt_shutdown, (u32)ctx, fid);
}

fast_code static void nvmet_cc_en_reset(void)
{
	hal_nvmet_clear_ppnd_cpl();
	//hal_nvmet_suspend_cmd_fetch();

	/* When FW receives 1. CC.EN (1->0),
	 * 2. SHN, 3. NSSR or 4. FLR, please write 1 to
	 * clear 0xc0032304[0], then write 1 clear 0xC0032000[1] */
	nvmet_clear_pf0_disable_active();
	nvmet_func_disable(0, (void *)shutdown, nvmet_dis_func_done);
}

fast_code static void nvmet_shut_down(void)
{
	extern u8 shudown2000_flag; // Curry 20200902
	shudown2000_flag = 1;
	hal_nvmet_clear_ppnd_cpl();

	/* It is expected that the controller is given
	 * time to process the shutdown notification.
	 * To start executing commands on controller after a shutdown
	 * operation, a CC.EN cleared to '0' is required.
	 */

	/* When FW receives 1. CC.EN (1->0),
	 * 2. SHN, 3. NSSR or 4. FLR, please write 1 to
	 * clear 0xc0032304[0], then write 1 clear 0xC0032000[1] */
	nvmet_clear_pf0_disable_active();
	nvmet_func_disable(0, (void *)shutdown2000, nvmet_dis_func_done);
}
fast_code static void Host_NVMeSubsystemResetEvent(void)
{
	nvme_hal_trace(LOG_ALW, 0x8db8, "NSSR");

	Nvme_Rset_flag = true;

	// Clear Shutdown bit when NSSR trigger. 12/14 Richard modify for PCBasher
	BEGIN_CS1
	if(gResetFlag & BIT(cNvmeShutDown))
	{
		gResetFlag &= ~BIT(cNvmeShutDown);
	}
	gResetFlag |= BIT(cNvmeSubsystemReset);
	END_CS1

	extern void pcie_rst_post(u32 reason);
	// configure NVMe HW to disable SQE_fetching / CQE_returning
	misc_nvm_ctrl_reset(true);

	nvmet_io_fetch_ctrl(true);	//for jira_10. solution(8/12).shengbin yang 2023/11/15

	// configure cmd_proc to abort DMA xfer
	// continue to fetch DTAG from BTN and directly release DTAG to BTN
	//hal_nvmet_abort_xfer(true);	//for jira_10. solution(9/12).shengbin yang 2023/11/15

	// trigger main reset task before release PCIe link training
	if (evt_perst_hook != 0xff)
		urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST);

	nvmet_clear_pf0_disable_active();

}

extern bool gFormatFlag;

fast_data_zi bool shst_running;
fast_data_zi bool cclr_running;

extern int btn_cmd_idle(void);

ddr_code bool nvme_check_hw_pending_by_func(u32 fid)
{
	u32 index;
	u32 adm_io_q_count;
	u32 sq_id;
	u32 sq_offset;
	flex_sq_ctrl_sts0_t flex_sq_ctrl_sts;
	bool cq_full = false;

	adm_io_q_count = NVMET_RESOURCES_FLEXIBLE_TOTAL;

	for (index = 1; index < adm_io_q_count; ++index) { //index 0-->1 ,ignore admin Q for CC_EN CLR. 2024/4/29 shengbin yang
		sq_id = index;
		sq_offset = sq_id << 2;
		flex_sq_ctrl_sts.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + sq_offset);
		if ((flex_sq_ctrl_sts.b.sq_outstanding == 1) && (flex_sq_ctrl_sts.b.cq_full == 0)) {
			return false;
		}
		if ((flex_sq_ctrl_sts.b.sq_outstanding == 1) && (flex_sq_ctrl_sts.b.cq_full == 1)) {
			cq_full = true;
		}
	}
	if (cq_full) {
		nvme_hal_trace(LOG_ALW, 0xca20,"cq_full:0x%x", cq_full);
	}
	return true;
}

/*!
 * @brief nvme check HW pending cmd by sq
 *
 * @param	fid	function id
 * @param	sqid	sq id
 *
 * @return	false	sq cmd pending
 * @return	true	sq no cmd pending
 */
ddr_code bool nvme_hal_check_pending_cmd_by_sq(u32 flex_sqid)
{
	u32 sq_offset;
	flex_sq_ctrl_sts0_t flex_sq_ctrl_sts;
	sq_offset = flex_sqid << 2;
	flex_sq_ctrl_sts.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + sq_offset);
	if ((flex_sq_ctrl_sts.b.sq_outstanding == 1) && (flex_sq_ctrl_sts.b.cq_full == 0)) {
		return false;
	} else	{
		return true;
	}
}

/*#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
static ddr_code void telemetry_set_aer()
{
	if (telemetry_update_ctrlr_singnal)
	{
		telemetry_update_ctrlr_singnal = 0;
		if (ctrlr->cur_feat.aec_feat.b.tln){
			nvmet_evt_aer_in(((NVME_EVENT_TYPE_NOTICE << 16)|NOTICE_STS_TELEMETRY_LOG_CHANGED),0);
		}
	}
}
#endif*/

/*!
 * @brief	nvme reset flush request callback function
 *
 * @param	req requset struct
 *
 * @return	true/false
 */
ddr_code bool nvme_rst_req_flush_callback(req_t * req)
{
	nvmet_put_req(req);
	ctrlr->nvme_req_flush = false;
	return true;
}

ddr_code bool nvmet_rst_req_flush(void)
{
	req_t *req;

	req = nvmet_get_req();
	sys_assert(req);
	req->req_from = REQ_Q_OTHER;
	req->opcode = REQ_T_FLUSH;
	req->op_fields.flush.shutdown = false;
	req->completion = nvme_rst_req_flush_callback;

	ctrlr->nvme_req_flush = true;

	ctrlr->nsid[0].ns->issue(req);
	return true;
}

ddr_code void nvmet_cc_en_clr_delay_check(void)
{
	if (shst_running)
	{
		shst_running = false;
		nvme_apl_trace(LOG_INFO, 0x88f9, "FORCE CLR");
	}

	//if ((get_btn_running_cmds() != 0) || (nvme_check_hw_pending_by_func(0) == false))
	//if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds)!= 0)//check IO & RX cmd
	if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds != 0) ||
		//(nvme_check_hw_pending_by_func(0) == false) ||
		//(!list_empty(&nvme_sq_del_reqs)) ||
		//(hal_nvmet_check_io_sq_pending() == false) ||
		(btn_cmd_idle() != true)
	)//check IO & RX cmd && HW pointer
	{
		if (cclr_running == false)
		{
			nvme_apl_trace(LOG_INFO, 0x1e8e, "[CLR] WIO");
			cclr_running = true;
		}
	}
	else
	{
		cclr_running = false;

		if (ctrlr->nvme_req_flush == false)
		{
			extern bool ucache_clean(void);
			if (ucache_clean() == false)
			{
				nvmet_rst_req_flush();
			}
			else
			{
				nvme_apl_trace(LOG_INFO, 0xe64d, "[CLR] Cache");
				nvmet_cc_en_reset();
			}
		}
	}
}

ddr_code void nvmet_shutdown_delay_check(u32 type)
{
	//if ((get_btn_running_cmds() != 0) || (nvme_check_hw_pending_by_func(0) == false))
	//if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds)!= 0)//check IO & RX cmd
	if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds != 0) ||
		//(nvme_check_hw_pending_by_func(0) == false) ||
		//(!list_empty(&nvme_sq_del_reqs)) ||
		//(hal_nvmet_check_io_sq_pending() == false) ||
		(btn_cmd_idle() != true)
	)//check IO & RX cmd && HW pointer
	{
		if (shst_running == false)
		{
			nvme_apl_trace(LOG_INFO, 0x5530, "[SHST] WIO");
			shst_running = true;
		}
	}
	else
	{
		shst_running = false;

		if (ctrlr->nvme_req_flush == false)
		{
			extern bool ucache_clean(void);
			if (ucache_clean() == false)
			{
				nvmet_rst_req_flush();
			}
			else
			{
				nvme_apl_trace(LOG_INFO, 0xb95a, "[SHST] cache");
				nvmet_shut_down();
			}
		}
	}
}

/*!
 * @brief	nvme msi mode cfg
 *
 * @param	None
 *
 * @return	None
 */
fast_code void nvme_pcie_msi_mode_cfg(void)
{
	flex_block_internal_ctrl_t flex_block_internal_ctrl;
	extern fast_code bool is_pcie_msi_enable(void);
	flex_block_internal_ctrl.all= flex_misc_readl(FLEX_BLOCK_INTERNAL_CTRL);
	if (is_pcie_msi_enable()) {
		flex_block_internal_ctrl.all |= BIT7;
	} else {
		flex_block_internal_ctrl.all &= ~BIT7;
	}
	flex_misc_writel(flex_block_internal_ctrl.all,FLEX_BLOCK_INTERNAL_CTRL);
	nvme_hal_trace(LOG_WARNING, 0x0d45,"msicfg:0x%x stx:0x%x",flex_misc_readl(FLEX_BLOCK_INTERNAL_CTRL),nvme_readl(NVME_CTRL_STATUS));
}

/*!
 * @brief	nvme cfg flex misc ctrl
 *
 * @param	fid:	function id
 * @param 	bit:	target bit to clear and clear
 * @param	setclr:	set:true, clr:false
 * @param	edge:	Electric frequency active signal true, false
 *
 * @return	err	true: fetch error cmd; false: fetch normal cmd
 */
ddr_code bool nvme_cfg_flex_misc_status(u32 fid, u32 bit, u32 setclr, bool edge, u32 cmd_type)
{
	u32 sq_start  = 0;
	u32 sq_end = NVMET_RESOURCES_FLEXIBLE_TOTAL;
	u32 sq_idx = 0;
	u32 sq_id = 0;
	flex_sq_ctrl_sts0_t flex_sq_ctrl_sts0;
	bool err = false;

	if (cmd_type == NVME_ADMIN_CMD) {
		sq_end = 1;
	} else if (cmd_type == NVME_RX_IO_CMD) {
		sq_start = 1;
	}
	for (sq_idx = sq_start; sq_idx < sq_end; sq_idx++) {
		sq_id = sq_idx;
		flex_sq_ctrl_sts0.all = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (sq_id << 2));
		if (flex_sq_ctrl_sts0.all & ((u32)1 << bit)) {
			nvme_apl_trace(LOG_ALW, 0x6483,"sq:0x%x sts:0x%x",sq_id, flex_sq_ctrl_sts0.all);
			if (edge) {
				flex_sq_ctrl_sts0.all |= (u32)1 << bit;
			} else {
				flex_sq_ctrl_sts0.all &= ~(u32)1 << bit;
			}
			flex_misc_writel(flex_sq_ctrl_sts0.all, FLEX_SQ_CTRL_STS0 + (sq_id << 2));
			err = true;
		}
	}
	return err;
}

/*!
 * @brief	nvmet slow interupt servide routine
 *
 * @param	ctrlr_sts interrupt flag
 *
 * @return	None
 *
 */
static fast_code __attribute__((noinline)) void
nvmet_slow_isr(nvme_unmasked_int_status_t ctrlr_sts)
{
	/* Boot Partition event */
	if (ctrlr_sts.b.boot_partition_read_op)
	{
		nvme_hal_trace(LOG_ERR, 0x9067, "Host want to read Boot Partition");
		if (dma_queue_initialized == false)
		{
			nvmet_init_dma_queues();
		}
		evt_set_imt(evt_bootpart_rd, 0, 0);
		ctrlr_sts.b.boot_partition_read_op = 0;
	}

	/* so we clear Queue(s) heres if any */
	if (ctrlr_sts.b.cc_en_clear)
	{
		if(plp_trigger)
		{
			nvmet_clear_pf0_disable_active();
			return ;
		}
		cc_en_set = false;
		is_IOQ_ever_create_or_not->flag = 0;

		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(25); // 2.5s// TODO DISCUSS
		#endif

		if (ctrlr_sts.b.cc_en_set && ctrlr_sts.b.cc_en_status)
		{
			nvme_hal_trace(LOG_ERR, 0xcb7b, "both cc_en and cc_dis set, cc_dis is last, ignore cc_dis");
			nvmet_clear_pf0_disable_active();
		}
		else
		{
			#if GET_PCIE_ERR
			disable_corr_uncorr_isr();
			#endif

			// Clear Shutdown bit when cc en clr. 12/14 Richard modify for PCBasher
			BEGIN_CS1
			if(gResetFlag & BIT(cNvmeShutDown))
			{
				gResetFlag &= ~BIT(cNvmeShutDown);
			}

			if (!(gResetFlag & BIT(cNvmeControllerResetClr)))
			{
				nvme_apl_trace(LOG_ALW, 0xb2ae, "[RSET] CC_EN CLR");
			}
			gResetFlag |= BIT(cNvmeControllerResetClr);
			END_CS1

			ctrlr_sts.b.cc_shn_2000 = 0;

			nvmet_cc_en_clr_delay_check();
		}
	}

	if (ctrlr_sts.b.cc_en_set)
	{
		if (ctrlr_sts.b.cc_en_clear && !ctrlr_sts.b.cc_en_status)
		{
			nvme_hal_trace(LOG_ERR, 0x8e03, "both cc_en and cc_dis set, cc_dis is last, ignore cc_en.");
		}
		else
		{
			union nvme_aqa_register aqa = {
				.raw = nvme_readl(ADM_QUEUE_ATTRI),
			};

			controller_confg_t cc = {
				.all = nvme_readl(CONTROLLER_CONFG),
			};

			nvme_apl_trace(LOG_ALW, 0x1920, "[RSET] CC_EN SET");
            bm_entry_pending_cnt = 0;
			is_IOQ_ever_create_or_not->flag = 0;
#if (Baidu_case)
			pcie_cfg_ack_freq();
#endif
            cc_en_set = true;
			Nvme_Rset_flag = false;
			extern bool download_fw_reset;
			download_fw_reset = false;
			dwnld_com_lock = false;
			move_gc_resume = false;
			ctrl_fatal_state = false;
			extern void reset_fw_du_bmp(void);
			reset_fw_du_bmp();

			extern void rdisk_reset_shutdown_state();
			rdisk_reset_shutdown_state();
			//epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
			//epm_ftl_data->POR_tag = 0;//reset POR flag , avoid smart unsafe_shutdowns decrease 1

#if GET_PCIE_ERR
            enable_corr_uncorr_isr(); // set correctable err mask after cc_en_set
#endif
			nvmet_config_xfer_payload();
			/*For tnvme 1:0.0.0 test pass, before CC.EN, we re-init
			 * VS_IDIR_ADDR and VS_IDIR_ADDR to 0 */
			nvmet_regs_reinit();

			/* update CC setting etc. AMS and MPS */
			nvmet_cc_en(cc.all);

			/* ASQB & ACQB can be parsed in ASIC by default. This
			 * will set up to accept Admin commands in PF for SHASTA
			 * and SRIOV modes */
			nvmet_init_aqa_attrs(aqa, 0, 0);
#if defined(SRIOV_SUPPORT)
			nvmet_init_vf_aqa_attrs(0);
#elif !defined(NVME_SHASTA_MODE_ENABLE)
			/* prepare to accept commands in PF for NORMAL & NVMSET
			 * modes */
			nvmet_init_vf_aqa_attrs(0);
#endif
			/*hal_nvmet_create_vf_cq(0, 0, aqa.bits.acqs, 0, 1, 1,
				(u64)flex_addr_readl(FLEX_CQ0_H_BASE_ADDR_H) << 32 | flex_addr_readl(FLEX_CQ0_H_BASE_ADDR_L));
			hal_nvmet_create_vf_sq(0, 0, 0, aqa.bits.asqs, NVME_QPRIO_ADMIN, 1,
				(u64)flex_addr_readl(FLEX_SQ0_H_BASE_ADDR_H) << 32 | flex_addr_readl(FLEX_SQ0_H_BASE_ADDR_L));*/

			nvmet_init_dma_queues();

			misc_nvm_ctrl_reset(false);//2024/03/21 debug PCbasher
			hal_nvmet_abort_xfer(false); //resume DMA in cc_en set.shengbin yang.2023/11/15.//2024/03/21 debug PCbasher

			u32 Temp_reg = nvme_readl(0x1AA4);
			Temp_reg = (Temp_reg|BIT20);
			nvme_writel(Temp_reg, 0x1AA4);
			/* 
			 *shr_shutdownflag:
			 *[true]: shutdown is not done ==> not resume GC
			 *[false]: shutdown finish ==> resume GC
			 *for case: shutdown start --> RESET --> CC_EN Set --> shutdown finish
			 *move gc_resume to shutdown completion when shutdown is not done
			 */
			if(shr_shutdownflag)
			{
				move_gc_resume = true;
			}
			else
			{
				evt_set_imt(evt_fe_gc_resume,0,0);// 	PCBaher plp01 workaround
			}
			nvmet_csts_rdy(true);  //Alan revert 20210831
			extern void nvmet_start_warm_boot(void);
			if (misc_is_fw_wait_reset()){
				nvmet_evt_aer_in((NVME_EVENT_TYPE_NOTICE << 16) | NOTICE_STS_FIRMWARE_ACTIVATION_STARTING, 0);
				nvmet_start_warm_boot();
			}
/*  //Alan revert 20210831
			else{
				nvmet_csts_rdy(true); //Johnny 20210702
				}
*/

			//CC.EN 1->0 , 0->1 , Feature saved value restore to current field , DM8 NT3
			nvmet_reinit_feat(); //3.1.7.4 merged 20201201 Eddie

			if (gFormatInProgress || gFormatFlag)
			{
				nvme_apl_trace(LOG_ALW, 0x1fbd, "wait format done......");
				hal_pi_suspend_cmd_fetch();
			}

			/*#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
			telemetry_set_aer();		//workaround for Edevx, PRESET test
			#endif*/
		}
	}

	/* Win10 sends CC.EN(0) direct rather than shutdown notification,
	 * also there is no Queue(s) deletion, but Linux does */
	if (ctrlr_sts.b.cc_shn_2000)
	{
		if(plp_trigger)
		{
			nvmet_clear_pf0_disable_active();
			return ;
		}
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(150); // 15s// TODO DISCUSS
		#endif
		//union nvme_csts_register csts;
		//nvme_cc_register
		controller_confg_t cc = {
				.all = nvme_readl(CONTROLLER_CONFG),
			};
		//csts.raw = nvme_readl(CONTROLLER_STATUS);

		//if (((cc.b.cc_shn == NVME_SHN_NORMAL || cc.b.cc_shn == NVME_SHN_ABRUPT) && (csts.bits.shst == 0)) && (!(gResetFlag & BIT(cNvmeControllerResetClr))))
		if (((cc.b.cc_shn == NVME_SHN_NORMAL || cc.b.cc_shn == NVME_SHN_ABRUPT)) && (!(gResetFlag & BIT(cNvmeControllerResetClr))))
		{
#if defined(SRIOV_SUPPORT)
			/* get which function generated CC.SHN */
			u8 fun_id = nvmet_get_disable_bit_triggered_fun();
			if (fun_id == 0xFF)
				panic("Function ID invalid .... above 32 -\n");
			nvme_apl_trace(LOG_ALW, 0x682a, "\033[91mshutdown fun(%d)\x1b[0m", fun_id);
			nvmet_csts_shst(fun_id, NVME_SHST_OCCURRING);
			nvmet_func_disable(fun_id, (void *)shutdown2000, nvmet_dis_func_done);
			/* clear controller disable active bit */
			nvmet_clear_fun_disable_bit(fun_id);
#else
			BEGIN_CS1
			if (!(gResetFlag & BIT(cNvmeShutDown)))
			{
				nvme_apl_trace(LOG_INFO, 0x0e72, "Shutdown type(%d)",cc.b.cc_shn);
			}
			gResetFlag |= BIT(cNvmeShutDown);
			END_CS1
			nvmet_csts_shst(NVME_SHST_OCCURRING);
			nvmet_shutdown_delay_check(cc.b.cc_shn);
#endif
		}
		else
		{
			nvme_apl_trace(LOG_ALW, 0x1279, "shutdown again without controller reset");
			nvmet_clear_pf0_disable_active();
		}
	}

	if (ctrlr_sts.b.dma_queue0_xfer_done ||
		ctrlr_sts.b.dma_queue1_xfer_done ||
		ctrlr_sts.b.dma_queue_xfer_error)
	{
		bool force = false;

		if (ctrlr_sts.b.dma_queue_xfer_error)
		{
			extern __attribute__((weak)) void dma_error_cnt_incr(void);
			force = true;
			if (dma_error_cnt_incr)
				dma_error_cnt_incr();
		}
		/*! mask dma interrupt to avoid interrupt missing */
		/*
		 * 1. mask DMA done interrupt
		 * 2. clear DMA done interrupt
		 * 3. handle interrupt
		 * 4. unmask interrupt
		 */
		nvmet_disable_dma_done_irq();
		u32 isr_ack = 0;
		if (force || ctrlr_sts.b.dma_queue0_xfer_done)
		{
			if (false == nvmet_dma_queue_done(0))
			{
				isr_ack |= DMA_QUEUE0_XFER_DONE_MASK;
			}
		}

		if (force || ctrlr_sts.b.dma_queue1_xfer_done)
		{
			if (false == nvmet_dma_queue_done(1))
			{
				isr_ack |= DMA_QUEUE1_XFER_DONE_MASK;
			}
		}

		nvme_writel(isr_ack, NVME_UNMASKED_INT_STATUS);

		nvmet_enable_dma_done_irq();
	}

	if (ctrlr_sts.b.nvm_cq_send_error)
	{
		cq_pointer_updt_errs_t errs = {
			.all = nvme_readl(CQ_POINTER_UPDT_ERRS),
		};

		if (errs.b.cq_head_updt_err)
			nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_DB_WR, errs.b.cq_head_updt_err);

		nvme_writel(errs.all, CQ_POINTER_UPDT_ERRS);
	}

	if (ctrlr_sts.b.fw_cq_req_done)
		hal_nvmet_check_ppnd_cpl();

	if (ctrlr_sts.b.nssr)
	{
#if (CO_SUPPORT_NSSR_PERST_FLOW == FEATURE_SUPPORTED)
		Host_NVMeSubsystemResetEvent();
#else
		nvme_hal_trace(LOG_ALW, 0x258f, "NVMe Subsystem Reset ...");
		disable_ltssm(1);

        nvm_subsys_reset_t nssr = {
            .all = nvme_readl(NVM_SUBSYS_RESET),
        };
        nssr.b.nssrc = 0;
        nvmet_regs.nssr = 0;
        nvme_writel(nssr.all, NVM_SUBSYS_RESET);

		Nvme_Rset_flag = true;

		hal_nvmet_suspend_cmd_fetch();
		nvmet_func_disable(0, (void *)nvm_subsystem_reset, nvmet_dis_func_done);

#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
		u32 i;
		for (i = 0; i < MAX_SEC_VF; i++)
		{
			if (nvmet_vf_is_cc_en(i + 1))
			{
				nvmet_func_disable(i + 1, (void *)nvm_subsystem_reset, nvmet_dis_func_done);
			}
		}
#endif

#ifndef NVME_SHASTA_MODE_ENABLE
		/* Clear after all activities are done ?? */
		/* Clear PF _controller_disable_active bit.
		 * TODO: Similar changes for VF1 to VF32 */
		pf_ctrlr_dis_act_done_t pf_dis = {
			.all = nvme_readl(PF_CTRLR_DIS_ACT_DONE),
		};
		pf_dis.b.pf_controller_disable_active = 1;
		nvme_writel(pf_dis.all, PF_CTRLR_DIS_ACT_DONE);
#endif
#endif
	}

	if (ctrlr_sts.b.que_ptr_update_err)
		nvmet_sq_err_report();

	if (ctrlr_sts.b.nvm_cmd_fetch_error) {
		//nvme_cfg_flex_misc_status(NVME_ALL_FUNCTIONS, CMD_FETCH_T_ERR_SHIFT, false, true, NVME_ALL_CMD);
		nvme_apl_trace(LOG_DEBUG, 0xd088, "nvme cmd fetch timeout error");
	}

	if (ctrlr_sts.b.any_flr_active) {
		/* Should not enter here, already handled in PCIe ISR (IRQ mode) */
		panic("flr");
	}
}

/*!
 * @brief	Rainier ISR handler
 *
 * @return	None
 *
 * @note the interrupt can only been clear in case of wptr == rptr, so we need
 * disable IRQ during commands handling
 */
static void fast_code nvmet_isr(void)
{
	//log_isr = LOG_IRQ_DO;
	nvme_unmasked_int_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_MASKED_INT_STATUS),
	};
	u32 bsts;
#if !defined(PROGRAMMER)
	ctrlr_sts.all &= ~(IO_CMD_FETCHED_MASK);
#endif
	bsts = ctrlr_sts.all;

#if (NVME_ISR_CHECK_AER_FIRST == ENABLE)
	if (ctrlr_sts.b.nvm_cq_send_error)
	{
		cq_pointer_updt_errs_t errs = {
			.all = nvme_readl(CQ_POINTER_UPDT_ERRS),
		};

		if (errs.b.cq_head_updt_err)
			nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_DB_WR, errs.b.cq_head_updt_err);

		nvme_writel(errs.all, CQ_POINTER_UPDT_ERRS);
		ctrlr_sts.b.nvm_cq_send_error = 0; //clear nvm_cq_send_error
	}

	if (ctrlr_sts.b.que_ptr_update_err)
	{
		nvmet_sq_err_report();
		ctrlr_sts.b.que_ptr_update_err = 0; //clear que_ptr_update_err
	}
#endif
	if (ctrlr_sts.b.adm_cmd_fetched)
	{
		nvmet_admin_cmd_in(0);
		ctrlr_sts.b.adm_cmd_fetched = 0;
	}

	if (unlikely(ctrlr_sts.all != 0))
	{
		nvmet_slow_isr(ctrlr_sts);
		// DMA queue interrupt was clear in nvmet_slow_isr already
		bsts &= ~(DMA_QUEUE1_XFER_DONE_MASK | DMA_QUEUE0_XFER_DONE_MASK);
	}

#if (HOST_NVME_FEATURE_SR_IOV == FEATURE_SUPPORTED)
	if (ctrlr_sts.b.any_vf_cc_en_set)
	{
		u32 cc_intr_sts;
		u16 i;
		cc_intr_sts = flex_misc_readl(FLEX_CC_EN_INTR_STS);
		for (i = 0; i < MAX_SEC_VF; i++)
		{
			if (cc_intr_sts & (1 << i))
			{
				if (nvmet_vf_is_cc_en(i + 1))
				{
					/* VF CC is enabled */
					nvmet_init_vf_aqa_attrs(i + 1);
					nvmet_vf_csts_rdy(true, (i + 1));
				}
			}
		}
		flex_misc_writel((u32)cc_intr_sts, FLEX_CC_EN_INTR_STS);
		ctrlr_sts.b.any_vf_cc_en_set = 0;
	}

	if (ctrlr_sts.b.any_vf_adm_cmd_fetched)
	{
		u32 adm_fetched_sts;
		u16 i;
		adm_fetched_sts = flex_misc_readl(FETCHED_ADM_CMD_VF);
		for (i = 0; i < MAX_SEC_VF; i++)
		{
			if (adm_fetched_sts & (1 << i))
				nvmet_admin_cmd_in(i + 1);
		}
		ctrlr_sts.b.any_vf_adm_cmd_fetched = 0;
	}
#endif

	nvme_writel(bsts, NVME_UNMASKED_INT_STATUS);
	//log_isr = LOG_IRQ_REST;
}


/*!
 * @brief This function checked the NVMe FLR interrupts
 *
 * @note when PCIe FLR happened, need to handle NVMe FLR first.
 *  Due to PCIe isr is in IRQ mode, so we don't handle NVMe FLR in
 *  SVC mode.
 *
 * @param 	not used
 *
 * @return	not used
 */
fast_code void nvmet_chk_flr(void)
{
	nvme_unmasked_int_status_t ctrlr_sts;

	do {
		ctrlr_sts.all = nvme_readl(NVME_UNMASKED_INT_STATUS);
		if (ctrlr_sts.b.any_flr_active) {
			/* When FW receives 1. CC.EN (1->0),
			* 2. SHN, 3. NSSR or 4. FLR, please write 1 to
			* clear 0xc0032304[0], then write 1 clear 0xC0032000[1] */
			nvmet_clear_pf0_disable_active();
			nvme_writel(ctrlr_sts.all, NVME_UNMASKED_INT_STATUS);
			break;
		}
	} while (ctrlr_sts.b.any_flr_active != 1);
}

#define NVME_REG1_OFF 0x2014
#define NVME_REG2_OFF 0x2280

fast_data u32 *saved_reg_mem;
fast_data u32 *saved_dbl_mem;
fast_data u32 *saved_misc_mem;
fast_data u32 msix_cq_map[3];
fast_data u16 saved_sq_size[NVMET_NR_IO_QUEUE];
fast_data u16 saved_cq_size[NVMET_NR_IO_QUEUE];

fast_data u32 int_mask_set, int_mask_clear;
fast_data u32 cq_interrupt_en;
#if defined(PMU_DB_DEBUG)
fast_data u32 db_error;
fast_data u32 db_backup[18]; //debug
#endif
/*!
 * @brief PMU suspend callback function of NVMe, toDO
 *
 * @param mode	sleep mode
 *
 * @return	always return true
 */
ddr_code bool nvme_suspend(enum sleep_mode_t state)//ps_code -> ddr_code by suda
{
	int i = 0;
	int nr_regs = (NVME_REG2_OFF - NVME_REG1_OFF) >> 2;

	saved_reg_mem = (u32 *)sys_malloc(FAST_DATA, sizeof(u32) * nr_regs);
	sys_assert(saved_reg_mem != NULL);

	for (i = 0; i < nr_regs; i++)
		saved_reg_mem[i] = nvme_readl(NVME_REG1_OFF + (i << 2));

#if defined(PMU_DB_DEBUG)
	for (i = 0; i < 18; i++)
		db_backup[i] = nvme_readl(0x1000 + (i << 2));
#endif

	msix_cq_map[0] = nvme_readl(MSIX_CQ_MAP0);
	msix_cq_map[1] = nvme_readl(MSIX_CQ_MAP1);
	msix_cq_map[2] = nvme_readl(MSIX_CQ_MAP2);

	int_mask_set = nvme_readl(INT_MASK_SET);
	int_mask_clear = nvme_readl(INT_MASK_CLEAR);
	cq_interrupt_en = nvme_readl(CQ_INTERRUPT_EN);

#ifndef NVME_SHASTA_MODE_ENABLE
	u32 dbl_regs = (FLEX_SQCQ65_SIZE - FLEX_SQ0_DB_REG) >> 2;
	u32 misc_regs = (FLEX_CQ_CTRL_STS65 - FLEX_SQ_CTRL_STS0) >> 2;
	u32 *adm_func;

	saved_dbl_mem = (u32 *)sys_malloc(FAST_DATA, sizeof(u32) * dbl_regs);
	sys_assert(saved_dbl_mem != NULL);
	for (i = 0; i < dbl_regs; i++)
		saved_dbl_mem[i] = flex_dbl_readl(FLEX_SQ0_DB_REG + (i << 2));
	saved_misc_mem = (u32 *)sys_malloc(FAST_DATA, sizeof(u32) * misc_regs);
	sys_assert(saved_misc_mem != NULL);
	for (i = 0; i < misc_regs; i++)
		saved_misc_mem[i] = flex_misc_readl(FLEX_SQ_CTRL_STS0 + (i << 2));
	adm_func = (u32 *)saved_sq_size;
	*adm_func = flex_misc_readl(ADM_CMD_SRAM_BASE_FUNC0);
	adm_func++;
	*adm_func = flex_misc_readl(ADM_CMD_FETCH_EN_PF);
	adm_func++;
	*adm_func = flex_misc_readl(ADM_CMD_FETCH_EN_VF);
#else
	for (i = 0; i < NVMET_NR_IO_QUEUE; i++)
	{
		u32 addr;
		sq_size_reg1_t sq;
		cq_size_reg1_t cq;

		/* sq size */
		addr = SQ_SIZE_REG1 + (i << 2);
		sq.all = nvme_readl(addr);
		saved_sq_size[i] = sq.b.sq1_size;

		/* cq size */
		addr = CQ_SIZE_REG1 + (i << 2);
		cq.all = nvme_readl(addr);
		saved_cq_size[i] = cq.b.cq1_size;
	}
#endif

	extern void cmd_proc_suspend(void);

	cmd_proc_suspend();

	return true;
}

#if defined(PMU_DB_DEBUG)
/*!
 * @brief	PMU DB debug
 *
 * @return	not used
 */
fast_code void nvme_db_pmu_debug(void)
{
	if (db_error)
	{
		u32 i;

		for (i = 0; i < 18; i++)
		{
			nvme_hal_trace(LOG_ERR, 0x4814, "resumed %x, backup %x\n",
				   nvme_readl(0x1000 + (i << 2)),
				   db_backup[i]);
		}
		panic("stop");
	}
}
#endif
/*!
 * @brief	PMU resume function fo nvme
 *
 * @return	not used
 */
static ps_code void _nvme_resume(void)
{
	int i;
	int nr_regs = (NVME_REG2_OFF - NVME_REG1_OFF) >> 2;

	sq_base[0].cmd_fetch_rptr = 0;
#ifndef NVME_SHASTA_MODE_ENABLE
	u32 dbl_regs = (FLEX_SQCQ65_SIZE - FLEX_SQ0_DB_REG) >> 2;

	for (i = 0; i < dbl_regs; i++)
	{
		u32 addr = FLEX_SQ0_DB_REG + (i << 2);
		flex_dbl_writel(saved_dbl_mem[i], addr);
	}
	sys_free(FAST_DATA, saved_dbl_mem);
#else
	for (i = 0; i <= NVMET_NR_IO_QUEUE; i++)
	{
		if (i == 0)
			continue; // no following set for Admin Q

		u32 addr;
		sq_size_reg1_t sq;
		cq_size_reg1_t cq;

		/* sq size */
		addr = SQ_SIZE_REG1 + ((i - 1) << 2);
		sq.all = nvme_readl(addr);
		sq.b.sq1_size = saved_sq_size[i - 1];
		nvme_writel(sq.all, addr);

		/* cq size */
		addr = CQ_SIZE_REG1 + ((i - 1) << 2);
		cq.all = nvme_readl(addr);
		cq.b.cq1_size = saved_cq_size[i - 1];
		nvme_writel(cq.all, addr);
	}
#endif
#if defined(PMU_DB_DEBUG)
	db_error = 0;
	for (i = 0; i < 18; i++)
	{
		u32 v = nvme_readl(0x1000 + (i << 2));

		if (v != db_backup[i])
			db_error++;
	}
#endif

	cq_base[0].wptr = 0;
	dma_queues[0].wptr = 0;
	dma_queues[0].rptr = 0;
	dma_queues[1].wptr = 0;
	dma_queues[1].rptr = 0;

	poll_mask(VID_NVME_P0);

	nvme_writel(msix_cq_map[0], MSIX_CQ_MAP0);
	nvme_writel(msix_cq_map[1], MSIX_CQ_MAP1);
	nvme_writel(msix_cq_map[2], MSIX_CQ_MAP2);

	nvme_writel(int_mask_set, INT_MASK_SET);
	nvme_writel(int_mask_clear, INT_MASK_CLEAR);
	nvme_writel(cq_interrupt_en, CQ_INTERRUPT_EN);

	for (i = 0; i < nr_regs; i++)
	{
		u32 addr = NVME_REG1_OFF + (i << 2);

		if (addr == DMA_QUEUE0_POINTERS ||
			addr == DMA_QUEUE1_POINTERS ||
			// addr == AUTO_LOOKUP_RES0_PTRS ||     // ignore HBM related for now. syu 6/16/2018
			// addr == FW_NTBL_ACCESS_REQ_POINTERS ||
			addr == FW_CQ_REQS_POINTERS)
			continue;
		nvme_writel(saved_reg_mem[i], addr);
	}

#ifndef NVME_SHASTA_MODE_ENABLE
	int misc_regs = (FLEX_CQ_CTRL_STS65 - FLEX_SQ_CTRL_STS0) >> 2;

	u32 *adm_func;
	for (i = 0; i < misc_regs; i++)
	{
		u32 addr = FLEX_SQ_CTRL_STS0 + (i << 2);
		flex_misc_writel(saved_misc_mem[i], addr);
	}
	sys_free(FAST_DATA, saved_misc_mem);
	adm_func = (u32 *)saved_sq_size;
	flex_misc_writel(*adm_func, ADM_CMD_SRAM_BASE_FUNC0);
	adm_func++;
	flex_misc_writel(*adm_func, ADM_CMD_FETCH_EN_PF);
	adm_func++;
	flex_misc_writel(*adm_func, ADM_CMD_FETCH_EN_VF);
#endif
	/* FW needs to set this bit when it updates any arbitration
	 * related registers or when the system resumed from low power mode,
	 * no matter the orignal value is 1 or 0 */
	arbitration_control_sts_t arb_ctrls = {
		.all = nvme_readl(ARBITRATION_CONTROL_STS),
	};
	nvme_writel(arb_ctrls.all, ARBITRATION_CONTROL_STS);

	u32 val = nvme_readl(ADM_QUEUE_ATTRI);

	nvme_writel(val, ADM_QUEUE_ATTRI);

	/* re-enable internal CC-EN */
	val = nvme_readl(INT_CTRL_REG);
	val |= 1 << 12;
	nvme_writel(val, INT_CTRL_REG);
}

/*!
 * @brief PMU resume callback function of NVMe
 *
 * @param mode	sleep mode
 *
 * @return	not used
 */
ddr_code void nvme_resume(enum sleep_mode_t state)//ps_code -> ddr_code by suda
{
	int i;
	pool_init(&nvme_cmd_pool,
			  (void *)nvme_cmd_base,
			  sizeof(nvme_cmd_base),
			  sizeof(struct nvme_cmd), SRAM_NVME_CMD_CNT);
	ctrlr->free_nvme_cmds = SRAM_NVME_CMD_CNT;

	for (i = 0; i < IO_SQUEUE_MAX_DEPTH; i++)
		cmd_base[0][i] = nvme_cmd_get(__FUNCTION__);

	cmd_proc_resume(state == SUSPEND_ABORT);
	cmd_proc_write_zero_ctrl(ctrlr->attr.b.write_zero);
	nvmet_ns_resume();

	nvmet_get_hw_ctrlr_regs(&nvmet_regs);
	if (state != SUSPEND_ABORT)
		_nvme_resume();

	sys_free(FAST_DATA, saved_reg_mem);
	saved_reg_mem = NULL;

	nvmet_enable_irq();
	nvmet_isr();
}

fast_code void hal_nvmet_reset(void)
{
	hal_nvmet_clear_ppnd_cpl();
	nvmet_fini_aqa_attrs();
	nvmet_fini_dma_queues();
	cmd_proc_suspend();
	// misc_reset(RESET_NVME);
}

#if !defined(NVME_SHASTA_MODE_ENABLE)
fast_code void hal_nvmet_clear_sqdb(void)
{
	u32 i;
	u32 dbl_regs = (FLEX_SQCQ65_SIZE - FLEX_SQ0_DB_REG) >> 2;

	for (i = 0; i < dbl_regs; i++) {
		u32 addr = FLEX_SQ0_DB_REG + (i << 2);
		flex_dbl_writel(0, addr);
	}
}
#endif

fast_code void hal_nvmet_resume(void)
{
	nvme_init_block_register_ram();
	nvmet_regs.csts.raw = 0;

	if(Nvme_Rset_flag == true){
		//Nvme_Rset_flag = false;

		#if (CO_SUPPORT_NSSR_PERST_FLOW == FEATURE_SUPPORTED)
		nvm_subsys_reset_t nssr = {
			.all = nvme_readl(NVM_SUBSYS_RESET),
		};
		nssr.b.nssrc = 0;
		nvmet_regs.nssr = 0;
		nvme_writel(nssr.all, NVM_SUBSYS_RESET);
		nvme_writel(0, CONTROLLER_CONFG);
		#endif

		nvmet_regs.csts.bits.nssro = 1;
	}

	nvmet_init_hw_ctrlr_regs(&nvmet_regs);
#if defined(SRIOV_SUPPORT)
	/* Initialize all VF hw controller registers */
	nvmet_init_n_vf_hw_ctrlr_regs(SRIOV_VF_PER_CTRLR);
#elif !defined(NVME_SHASTA_MODE_ENABLE)
	hal_nvmet_clear_sqdb();
#endif

	nvmet_ns_resume();
	cmd_proc_resume(0);
	nvmet_enable_irq();
}

/*!
 * @brief resume pcie config after perst
 *
 * @param None
 *
 * @return	not used
 */
fast_code void nvmet_pcie_config_resume(void)
{
#if defined(SRIOV_SUPPORT)
	hal_nvmet_set_pf_max_msi_x(SRIOV_PF_VF_Q_PER_CTRLR);
#elif !defined(NVME_SHASTA_MODE_ENABLE)
	hal_nvmet_set_pf_max_msi_x(NVMET_RESOURCES_FLEXIBLE_TOTAL - 1);
#endif

//#if !defined(NVME_SHASTA_MODE_ENABLE)
//	hal_nvmet_set_op_mode();
//#endif
}
ddr_code bool create_q_check_cc_reg(bool flag, u16 size)
{
	controller_confg_t cc = {.all = nvme_readl(CONTROLLER_CONFG),};
	//nvme_apl_trace(LOG_INFO, 0, "cc:%x",cc.all);

	if(flag)
	{
		if(cc.b.cc_iosqes)
			return true;
	}else
	{
		if(cc.b.cc_iocqes)
			return true;
	}

	return false;
}

#define PMIC_slave   0x25
#define PMIC_add   (PMIC_slave << 1)
extern u32 I2C_read(u8 slaveID, u8 cmd_code, u8 *value);
extern u32 I2C_write(u8 slaveID, u8 cmd_code, u8 value);

static init_code void nvmet_init(void)
{
    u8 reg0x34_value, reg0x3a_value;
    u32 res1 = 1;
    u32 res2 = 1;
    u32 cnt = 10;
    while ((res1 || res2) && cnt --){
        res1 = I2C_read(PMIC_add, 0x34, &reg0x34_value);
        res2 = I2C_read(PMIC_add, 0x3A, &reg0x3a_value);
    }

    if(reg0x34_value == 0x20 && reg0x3a_value == 0x42){
        res1 = 1;
        cnt = 10;
        while (res1 && cnt --){
            res1 = I2C_write(PMIC_add, 0X29,0xF0);
        }
		nvme_apl_trace(LOG_INFO, 0x144c, "Force PWM: set pmic reg[0x29] to 0xF0 sts 0x%x", res1);
    }else if(reg0x34_value == 0x11){
        res1 = 1;
        cnt = 10;
        while (res1 && cnt --){
            res1 = I2C_write(PMIC_add, 0xA, 0xF);
        }
		nvme_apl_trace(LOG_INFO, 0xf1ba, "Force PWM: set pmic reg[0xA] to 0xF sts 0x%x", res1);
    }

#if (FPGA)
	if (!soc_cfg_reg2.b.nvm_en)
	{
		return;
	}
#endif

#if NCL_STRESS
	return;
#endif

	if (misc_is_warm_boot() == true)
	{ //20201028-Eddie
	#ifdef AGING2NORMAL_DDRBYPASS	//20201123-Eddie
		if (misc_is_aging2normal_ddr_bypass())
			_fg_warm_boot = false;
		else
			_fg_warm_boot = true;
	#else
		_fg_warm_boot = true;
		cmd_proc_req_pending();		//20210326-Eddie
		//cmd_proc_suspend();  // already do in nvmet_warm_boot_reset(), may rx_cmd_base[i] not initialized after FW rebooting
	#endif

		_fg_fwupgrade_stopPTRD = true;

		if (misc_is_CA1_active() == true)
			_fg_wb_reboot = true;

		misc_clear_STOP_BGREAD();	//20210506-Eddie - Clear Patrol and Refresh Read lock flag
	}
	else
	{
		nvme_init_block_register_ram();
	}

	/* Initialize ctrlr, remember that APL has filled parts of CAP/VS */

	/* Contiguous queues required then SQ/CQ prp1 cannot be PRP entries list */
	nvmet_regs.cap.bits.cqr = 1;
	nvmet_regs.cap.bits.mqes = 4095;	  /* ASIC I/O support up to 256 *///for GTP spec FF->FFF
	nvmet_regs.cap.bits.mpsmax = 0;	  /* max 8K *///for GTP spec 1->0
	nvmet_regs.cap.bits.mpsmin = 0;	  /* min 4K */
	nvmet_regs.cap.bits.css = BIT(0); /* NVM command set only */
	nvmet_regs.cap.bits.nssrs = 1;	  /* Support NSSR */

#if 0//(BOOT_PARTITION_FEATURE_SUPPORT == FEATURE_NOT_SUPPORTED)
	/* Boot Partition initialization */
	{
		nvmet_regs.cap.bits.bps = 1;

		nvmet_regs.bpinfo.bits.abpid = 0x0;			   /* boot partition identifier 0 */
		nvmet_regs.bpinfo.bits.brs = NVME_BP_BRS_NOOP; /* boot partition identifier 0 */
		nvmet_regs.bpinfo.bits.bpsz = 0x1;			   /* 256 KB boot partition */
	}
#endif

	/*
	 * If not enable, Window 10 will read CSTS immediately(after set CC.EN = 1) and w/o retry,
	 * since the status is updated in FW, finally Windows would not enable the device.
	 */
#if (Xfusion_case)
	nvmet_regs.cap.bits.to = 0xFF;// BG_Trim + SPOR 127.5s ;2280 only for XFusion
#elif (PLP_SUPPORT == 1)
	nvmet_regs.cap.bits.to = 40; 	//for GTP spec 100->120//120 -> 240 optimize all init done take much time //240(120s)-->40(20s) .23/10/17
#else
	nvmet_regs.cap.bits.to = 60;  	// non PLP 2T need 30s
#endif

	nvmet_regs.intms = 0;
	nvmet_regs.intmc = 0;

	nvmet_regs.csts.bits.rdy = 0;

	pool_init(&nvme_cmd_pool,
			  (void *)nvme_cmd_base,
			  sizeof(nvme_cmd_base),
			  sizeof(struct nvme_cmd), SRAM_NVME_CMD_CNT);
	ctrlr->free_nvme_cmds = SRAM_NVME_CMD_CNT;

	cmd_proc_init();

	if (misc_is_warm_boot())
		nvmet_get_hw_ctrlr_regs(&nvmet_regs);
	else
		nvmet_init_hw_ctrlr_regs(&nvmet_regs);

#if defined(SRIOV_SUPPORT)
	u32 i;
	/* Initialize all VF hw controller registers */
	nvmet_init_n_vf_hw_ctrlr_regs(SRIOV_VF_PER_CTRLR);

	struct nvme_vf_registers *vf_reg;
	if (misc_is_warm_boot())
	{
		for (i = 0; i < 4; i++)
		{
			vf_reg = (nvmet_vf_regs + i);
			nvmet_get_vf_hw_ctrlr_regs(vf_reg, i + 1);
		}
	}
#endif

	poll_irq_register(VID_NVME_P0, nvmet_isr);
	poll_mask(VID_NVME_P0);

	nvmet_enable_irq();

	if (misc_is_warm_boot() == false)
	{
#if defined(SRIOV_SUPPORT)
		hal_nvmet_set_pf_max_msi_x(SRIOV_PF_VF_Q_PER_CTRLR);
#elif !defined(NVME_SHASTA_MODE_ENABLE)
		hal_nvmet_set_pf_max_msi_x(NVMET_RESOURCES_FLEXIBLE_TOTAL - 1);
		//hal_nvmet_set_op_mode();
#else
		/*
		Because the default value of 0xc0032014[6:4] is changed, please also change FW to
		set 0xc0032014[6:4] to 111b before ltssm_en, Otherwise, the controller won't work for 9-SQ mode.*/

		nvme_ctrl_status_t nvme_ctrlr_sts = {
			.all = nvme_readl(NVME_CTRL_STATUS),
		};
		nvme_ctrlr_sts.b.nvme_op_mode = NVME_OP_MODE_SHASTA;
		nvme_writel(nvme_ctrlr_sts.all, NVME_CTRL_STATUS);
#endif
	}

#if !defined(FPGA) && !defined(PROGRAMMER)
	if (misc_is_warm_boot() == false)
		pcie_link_enable();
#endif

	pmu_register_handler(SUSPEND_COOKIE_NVME, nvme_suspend,
						 RESUME_COOKIE_NVME, nvme_resume);

#if (WB_RESUME == DISABLE)	//20210326-Eddie-Move to the last module init (wait FTL done)
	if (misc_is_warm_boot())
	{
		/* Restore MPS and MRRS */
		nvmet_config_xfer_payload();
		/* update CC setting etc. AMS and MPS */
		nvmet_cc_en(nvmet_regs.cc.raw);

		nvmet_init_dma_queues();

		//extern void nvmet_warm_boot_resume(void);
		//nvmet_warm_boot_resume();
		nvmet_enable_fetched_cmd_warmboot();
		nvme_apl_trace(LOG_INFO, 0xf377, "warm boot resume done");
	}
#endif

	INIT_LIST_HEAD(&cq_timer.entry);
	cq_timer.function = nvmet_cq_timer;
	cq_timer.data = "cq_timer";

    irq_enable();
}

/*! \cond PRIVATE */
module_init(nvmet_init, NVME_HAL);
/*! \endcond */

#if (WB_RESUME == ENABLE)
slow_code_ex void nvmet_wb_init(void){	//20210326-Eddie-Move to the last module init (wait FTL done)
	if (misc_is_warm_boot() == true)	//If warm boot, reume fetching IO cmds
	{
		/* update CC setting etc. AMS and MPS */
             nvmet_config_xfer_payload();
		nvmet_cc_en(nvmet_regs.cc.raw);

		nvmet_init_dma_queues();

#if (_TCG_)
	#ifndef FW_UPDT_TCG_SWITCH
		// resume status from EPM
		extern void tcg_status_resume(void);
		tcg_status_resume();
	#endif
#endif

		//extern void nvmet_warm_boot_resume(void);
		//nvmet_warm_boot_resume();
		// #ifndef NVME_SHASTA_MODE_ENABLE
		// nvme_recover_aer_for_each_func();
		// #endif
		nvmet_enable_fetched_cmd_warmboot();
		extern commit_ca3* commit_ca3_fe;
		nvme_apl_trace(LOG_INFO, 0x90be, "warmboot flag:%x",commit_ca3_fe->flag);
		if(commit_ca3_fe->flag == 0xABCD)
		{
			commit_ca3_fe->flag = 0;
			nvmet_warmboot_handle_commit_done(&commit_ca3_fe->fe);
		}

#ifdef FW_UPDT_TCG_SWITCH
		req_t *req = nvmet_get_req();
		req->nsid = 1;
		req->host_cmd = NULL;
		req->completion = NULL;

		extern void vsc_preformat(req_t * rq, bool ns_reset);
		vsc_preformat(req, false);
#endif

		nvme_apl_trace(LOG_INFO, 0xbd3b, "warm boot resume done");
	}
}
#if 0	//20210401-Eddie
module_init(nvmet_wb_init, NVME_WB_HAL);
#endif
#endif

ddr_code u32 AplPollingResetEvent(void)
{
	u32 ret = false;
	nvme_unmasked_int_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_MASKED_INT_STATUS),
	};

	if (gResetFlag & BIT(cNvmePCIeReset))
	{
		nvme_apl_trace(LOG_INFO, 0x050d, "[Poll] PERST#");
		ret = true;
	}

	if (gResetFlag & BIT(cNvmeFlrPfReset))
	{
		nvme_apl_trace(LOG_INFO, 0xd2e3, "[Poll] FLR");
		ret = true;
	}

	if (gResetFlag & BIT(cNVMeLinkReqRstNot))
	{
		nvme_apl_trace(LOG_INFO, 0xd2e4, "[Poll] ReqRst_N");
		ret = true;
	}

	if (ctrlr_sts.b.cc_en_clear)
	{
		nvme_apl_trace(LOG_INFO, 0x3cc9, "[Poll] CLR");
		ret = true;
	}

	if (ctrlr_sts.b.nssr)
	{
		nvme_apl_trace(LOG_INFO, 0x59e8, "[Poll] NSSR");
		ret = true;
	}
	return ret;
}

fast_code void HalNvmeClearNssro(void)
{
	controller_status_t csts = {
		.all = nvme_readl(CONTROLLER_STATUS),
	};
	csts.b.csts_nssro = 0;
	nvmet_regs.csts.bits.nssro = 0;
	nvme_writel(csts.all, CONTROLLER_STATUS);
}

/*!
 * dma test
 *
 * \cond PRIVATE
 */
#if 0
#define DMA_LP_DTAGS 16
prp_t dl_prp;
dtag_t dl_dtag_src;
dtag_t dl_dtags_dst[DMA_LP_DTAGS];
static fast_data u32 dl_times, dl_subm, dl_cmpl, dl_compared, dl_error;
fast_data u32 dl_dtags;
fast_data u32 dl_pattern;
static void dma_lp_main_2sram_done(void *hcmd);

static int dma_lp_cmp_done(void *ctx, dpe_rst_t *rst)
{
	int i = 0;
	u32 dtag_idx = (u32) ctx;

	dl_compared++;
//	if (rst->cmp_fmt1.cmp_err_cnt != 0) {
	if (rst && rst->cmp_fmt1.cmp_err_cnt != 0) {
		if (dl_error == 0) {
			u32 *mem;

			nvme_hal_trace(LOG_ERR, 0xc369, "\nHit first dtag error: (%u/%u), pattern(0x%x) dtag_idx(%d)\n",
					dl_compared, dl_times, dl_pattern, dtag_idx);
			mem = dtag2mem(dl_dtags_dst[dtag_idx]);
			for (i = 0; i < DTAG_SZE/(sizeof(u32) * 4); i++, mem = mem + 4) {
				nvme_hal_trace(LOG_ERR, 0x5a09, "0x%x %0x %0x %0x\n", *(mem), *(mem+1), *(mem+2), *(mem+3));
			}
			sys_assert(0);
		}
		dl_error++;
	}

	if ((dl_compared % 20000) == 0) {
		for (i = 0; i < 80; i++) {
			putc('\b');
			putc(' ');
			putc('\b');
		}
		nvme_hal_trace(LOG_ERR, 0xd742, "compared(%u/%u):error(%d)", dl_compared, dl_times, dl_error);
	}

	if (dl_subm < dl_times) {
		u32 *mem = dtag2mem(dl_dtags_dst[dtag_idx]);
		/* corrupt data first */
		*(mem) = jiffies;
		*(mem + 0x100) = dl_compared;
		*(mem + 0x200) = dl_subm;
		*(mem + 0x300) = dl_cmpl;
		hal_nvmet_data_xfer(dl_prp, mem, DTAG_SZE, READ, (void *)dtag_idx, dma_lp_main_2sram_done);
		dl_subm++;
	} else if (dl_compared == dl_times) {
		dtag_put(DTAG_T_SRAM, dl_dtag_src);
		dtag_put_bulk(DTAG_T_SRAM, dl_dtags, dl_dtags_dst);
		nvme_hal_trace(LOG_ERR, 0x7b6e, "\ntotal compare error(%d/%d)\n", dl_error, dl_times);
	}
	return 0;
}

static void dma_lp_main_2sram_done(void *hcmd)
{
	u32 dtag_idx = (u32) hcmd;
#if 0
	/* Data Compare */
	bm_data_compare_dtag(dl_dtag_src, dl_dtags_dst[dtag_idx],
		dma_lp_cmp_done, (void *)dtag_idx);
	dl_cmpl++;
#endif
	dl_cmpl++;
	dma_lp_cmp_done((void *)dtag_idx, NULL);
}

static void dma_lp_main_2host_done(void *hcmd)
{
	int i = 0;

	for (; i < dl_dtags && i < dl_times; i++) {
		u32 *mem = dtag2mem(dl_dtags_dst[i]);

		if ((i % 8) == 0)
			nvme_hal_trace(LOG_ERR, 0x2c8c, "\n");
		nvme_hal_trace(LOG_ERR, 0x36ed, "mem[%d](0x%x) ", i, mem);
		/* corrupt data first */
		*(mem) = jiffies;
		*(mem + 0x100) = dl_compared;
		*(mem + 0x200) = dl_subm;
		*(mem + 0x300) = dl_cmpl;
		hal_nvmet_data_xfer(dl_prp, mem, DTAG_SZE, READ, (void *)i, dma_lp_main_2sram_done);
		dl_subm++;
	}
	nvme_hal_trace(LOG_ERR, 0x28a3, "\n");
}

static int dma_lp_main(int argc, char *argv[])
{
	char *endp;

	dl_prp = (prp_t) strtoull(argv[1], &endp, 0);
	dl_pattern = strtoul(argv[2], &endp, 0);
	dl_times = strtoul(argv[3], &endp, 0);

	dl_error = 0;
	dl_compared = 0;
	dl_subm = 0;
	dl_cmpl = 0;

	u32 prp_lo = (u32) (dl_prp & 0xFFFFFFFF);
	u32 prp_hi = (u32) ((dl_prp >> 16) >> 16);

	nvme_hal_trace(LOG_ERR, 0x8867, "\nPRP[0x%x%x] pattern[0x%x] total_times[%u]\n", prp_hi, prp_lo, dl_pattern, dl_times);

	u32 *mem;

	dl_dtag_src = dtag_get(DTAG_T_SRAM, (void *)&mem);
	if (dl_dtag_src.dtag == _inv_dtag.dtag) {
		nvme_hal_trace(LOG_ERR, 0xdc71, "No src dtag allocaed, give up");
		return -1;
	}
	nvmet_init_dma_queues();
	int i = 0;

	for (; i < DTAG_SZE/sizeof(u32); i++)
		*(mem + i) = dl_pattern;

	dl_dtags = dtag_get_bulk(DTAG_T_SRAM, DMA_LP_DTAGS < dl_times ? DMA_LP_DTAGS : dl_times, dl_dtags_dst);
	nvme_hal_trace(LOG_ERR, 0x4b05, "alloced dst #Dtag=%d, %d parallel jobs", dl_dtags, dl_dtags);

	if (dl_dtags == 0) {
		dtag_put(DTAG_T_SRAM, dl_dtag_src);
		nvme_hal_trace(LOG_ERR, 0xccfa, "No dtags allocaed, give up");
		return -1;
	}

	hal_nvmet_data_xfer(dl_prp, mem, DTAG_SZE, WRITE, NULL, dma_lp_main_2host_done);
	return 0;
}

static DEFINE_UART_CMD(dma_lp,
		"dma_lp", "dma_lp host_physical_address data_pattern verified_times",
		"dma_lp: host_physical_address data_pattern verified_times\nDMA data xfer loopback via DPE for one Dtag, one src in host, multiple dst in sram",
		3, 3, dma_lp_main);
#endif

#if VALIDATE_BOOT_PARTITION
#include "pf_reg.h"
/*!
 * @brief	oot partition fucntion
 *
 * @param	hmem	host memory
 * @param	bp_sel	boot partition selection
 * @param	offset	offset
 *
 * @return	always zero
 */
ddr_code int bp_read_trigger_main(u64 hmem, u8 bp_sel, u32 offset)
{

	nvme_unmasked_int_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_MASKED_INT_STATUS_CPU1),
	};

	nvme_hal_trace(LOG_ERR, 0xbc89, "Host mem: 0x%x %x Boot Partition select: 0x%x \n",
		   hmem, hmem >> 32, bp_sel);
	hal_nvmet_bootpart_set_abpid(bp_sel);

	union nvme_bprsel_register bprs = {
		.raw = nvme_readl(BOOT_PART_READ_SEL),
	};

	bprs.bits.bprof = offset;
	bprs.bits.bprsz = 1;
	bprs.bits.bpid = bp_sel;

	nvme_writel(bprs.raw, BOOT_PART_READ_SEL);

	nvme_writel(hmem, BOOT_PART_MEM_BUF_LOC_L);

	nvme_writel(hmem >> 32, BOOT_PART_MEM_BUF_LOC_H);

	ctrlr_sts.b.boot_partition_read_op = 1;
	nvme_writel(ctrlr_sts.all, NVME_MASKED_INT_STATUS_CPU1);
	return 0;
}

#endif //VALIDATE_BOOT_PARTITION

#if CO_SUPPORT_PANIC_DEGRADED_MODE

static ddr_code __attribute__((noinline)) void
nvmet_assert_slow_isr(nvme_unmasked_int_status_t ctrlr_sts)
{
	if (ctrlr_sts.b.dma_queue0_xfer_done ||
		ctrlr_sts.b.dma_queue1_xfer_done ||
		ctrlr_sts.b.dma_queue_xfer_error)
	{
		bool force = false;

		if (ctrlr_sts.b.dma_queue_xfer_error)
		{
			extern __attribute__((weak)) void dma_error_cnt_incr(void);
			force = true;
			if (dma_error_cnt_incr)
				dma_error_cnt_incr();
		}
		/*! mask dma interrupt to avoid interrupt missing */
		/*
		 * 1. mask DMA done interrupt
		 * 2. clear DMA done interrupt
		 * 3. handle interrupt
		 * 4. unmask interrupt
		 */
		nvmet_disable_dma_done_irq();
		u32 isr_ack = 0;
		if (force || ctrlr_sts.b.dma_queue0_xfer_done)
		{
			if (false == nvmet_dma_queue_done(0))
			{
				isr_ack |= DMA_QUEUE0_XFER_DONE_MASK;
			}
		}

		if (force || ctrlr_sts.b.dma_queue1_xfer_done)
		{
			if (false == nvmet_dma_queue_done(1))
			{
				isr_ack |= DMA_QUEUE1_XFER_DONE_MASK;
			}
		}

		nvme_writel(isr_ack, NVME_UNMASKED_INT_STATUS);

		nvmet_enable_dma_done_irq();
	}

	if (ctrlr_sts.b.cc_en_set && ctrlr_sts.b.cc_en_clear) {
		//nvme_apl_trace(LOG_ALW, ctrlr_sts.b.cc_en_status ? "[Sts ] set" : "[Sts ] clear");
		if (ctrlr_sts.b.cc_en_status) {
			ctrlr_sts.b.cc_en_clear = 0;
			nvmet_clear_pf0_disable_active();
			nvme_hal_trace(LOG_ERR, 0xa7e3, "ignore cc_clr");
		} else {
			ctrlr_sts.b.cc_en_set = 0;
			nvme_hal_trace(LOG_ERR, 0x7a3c, "ignore cc_set");
		}
	}

	/* so we clear Queue(s) heres if any */
	if (ctrlr_sts.b.cc_en_clear)
	{
		nvme_apl_trace(LOG_ALW, 0xa527, "[RSET] CC_EN CLR");
		cc_en_set = false;

		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(25); // 2.5s// TODO DISCUSS
		#endif
		is_IOQ_ever_create_or_not->flag = 0;

		nvmet_clear_pf0_disable_active();
		BEGIN_CS1
		if(gResetFlag & BIT(cNvmeShutDown))
		{
			gResetFlag &= ~BIT(cNvmeShutDown);
		}
		gResetFlag |= BIT(cNvmeControllerResetClr);
		urg_evt_set(evt_assert_rst, (u32)shutdown, NVME_RST_CC_CLR);
		END_CS1
	}

	if (ctrlr_sts.b.cc_en_set)
	{
		{
			union nvme_aqa_register aqa = {
				.raw = nvme_readl(ADM_QUEUE_ATTRI),
			};

			controller_confg_t cc = {
				.all = nvme_readl(CONTROLLER_CONFG),
			};

			nvme_apl_trace(LOG_ALW, 0xb033, "[RSET] CC_EN SET");
			is_IOQ_ever_create_or_not->flag = 0;

            cc_en_set = true;
			Nvme_Rset_flag = false;
			extern bool download_fw_reset;
			download_fw_reset = false;
			dwnld_com_lock = false;
			extern void reset_fw_du_bmp(void);
			reset_fw_du_bmp();

			nvmet_config_xfer_payload();
			/*For tnvme 1:0.0.0 test pass, before CC.EN, we re-init
			 * VS_IDIR_ADDR and VS_IDIR_ADDR to 0 */
			nvmet_regs_reinit();

			/* update CC setting etc. AMS and MPS */
			nvmet_cc_en(cc.all);

			/* ASQB & ACQB can be parsed in ASIC by default. This
			 * will set up to accept Admin commands in PF for SHASTA
			 * and SRIOV modes */
			nvmet_init_aqa_attrs(aqa, 0, 0);
#if defined(SRIOV_SUPPORT)
			nvmet_init_vf_aqa_attrs(0);
#elif !defined(NVME_SHASTA_MODE_ENABLE)
			/* prepare to accept commands in PF for NORMAL & NVMSET
			 * modes */
			nvmet_init_vf_aqa_attrs(0);
#endif
			/*hal_nvmet_create_vf_cq(0, 0, aqa.bits.acqs, 0, 1, 1,
				(u64)flex_addr_readl(FLEX_CQ0_H_BASE_ADDR_H) << 32 | flex_addr_readl(FLEX_CQ0_H_BASE_ADDR_L));
			hal_nvmet_create_vf_sq(0, 0, 0, aqa.bits.asqs, NVME_QPRIO_ADMIN, 1,
				(u64)flex_addr_readl(FLEX_SQ0_H_BASE_ADDR_H) << 32 | flex_addr_readl(FLEX_SQ0_H_BASE_ADDR_L));*/

			nvmet_init_dma_queues();

			u32 Temp_reg = nvme_readl(0x1AA4);
			Temp_reg = (Temp_reg|BIT20);
			nvme_writel(Temp_reg, 0x1AA4);

			evt_set_imt(evt_fe_gc_resume,0,0);// 	PCBaher plp01 workaround

			nvmet_csts_rdy(true);  //Alan revert 20210831
			extern void nvmet_start_warm_boot(void);
			if (misc_is_fw_wait_reset()){
				nvmet_evt_aer_in((NVME_EVENT_TYPE_NOTICE << 16) | NOTICE_STS_FIRMWARE_ACTIVATION_STARTING, 0);
				nvmet_start_warm_boot();
			}
/*  //Alan revert 20210831
			else{
				nvmet_csts_rdy(true); //Johnny 20210702
				}
*/

			//CC.EN 1->0 , 0->1 , Feature saved value restore to current field , DM8 NT3
			nvmet_reinit_feat(); //3.1.7.4 merged 20201201 Eddie

			if (gFormatInProgress || gFormatFlag)
			{
				nvme_apl_trace(LOG_ALW, 0x41f8, "wait format done......");
				hal_pi_suspend_cmd_fetch();
			}

		}
	}

	/* Win10 sends CC.EN(0) direct rather than shutdown notification,
	 * also there is no Queue(s) deletion, but Linux does */
	if (ctrlr_sts.b.cc_shn_2000)
	{
		nvme_apl_trace(LOG_ALW, 0x5ccb, "Shutdown");
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(25); // 2.5s// TODO DISCUSS
		#endif
		nvmet_csts_shst(NVME_SHST_OCCURRING);
		nvmet_clear_pf0_disable_active();
		nvmet_csts_shst(NVME_SHST_COMPLETE);
	}

	if (ctrlr_sts.b.nvm_cq_send_error)
	{
		cq_pointer_updt_errs_t errs = {
			.all = nvme_readl(CQ_POINTER_UPDT_ERRS),
		};

		if (errs.b.cq_head_updt_err)
			nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_INVALID_DB_WR, errs.b.cq_head_updt_err);

		nvme_writel(errs.all, CQ_POINTER_UPDT_ERRS);
	}

	if (ctrlr_sts.b.fw_cq_req_done)
		hal_nvmet_check_ppnd_cpl();

	if (ctrlr_sts.b.nssr)
	{
		extern void pcie_rst_post(u32 reason);
		// configure NVMe HW to disable SQE_fetching / CQE_returning
		misc_nvm_ctrl_reset(true);

		// configure cmd_proc to abort DMA xfer
		// continue to fetch DTAG from BTN and directly release DTAG to BTN
		hal_nvmet_abort_xfer(true);

		BEGIN_CS1
		urg_evt_set(evt_assert_rst, (u32)pcie_rst_post, PCIE_RST);
		END_CS1

		nvmet_clear_pf0_disable_active();
	}

	if (ctrlr_sts.b.que_ptr_update_err)
		nvmet_sq_err_report();

	if (ctrlr_sts.b.any_flr_active) {
		/* Should not enter here, already handled in PCIe ISR (IRQ mode) */
		panic("flr");
	}
}

void ddr_code Assert_nvmet_isr(void)
{
	//log_isr = LOG_IRQ_DO;
	nvme_unmasked_int_status_t ctrlr_sts = {
		.all = nvme_readl(NVME_MASKED_INT_STATUS),
	};
	u32 bsts;
#if !defined(PROGRAMMER)
	ctrlr_sts.all &= ~(IO_CMD_FETCHED_MASK);
#endif
	bsts = ctrlr_sts.all;

	if (ctrlr_sts.b.adm_cmd_fetched)
	{
		nvmet_assert_admin_cmd_in(0);
		ctrlr_sts.b.adm_cmd_fetched = 0;
	}

	if (unlikely(ctrlr_sts.all != 0))
	{
		nvmet_assert_slow_isr(ctrlr_sts);
		// DMA queue interrupt was clear in nvmet_slow_isr already
		bsts &= ~(DMA_QUEUE1_XFER_DONE_MASK | DMA_QUEUE0_XFER_DONE_MASK);
	}

	nvme_writel(bsts, NVME_UNMASKED_INT_STATUS);
	//log_isr = LOG_IRQ_REST;
}
#endif
/*
static ddr_code int force_shutdown(int argc, char *argv[])
{
	BEGIN_CS1
	gResetFlag |= BIT(cNvmeShutDown);
	END_CS1
	nvme_apl_trace(LOG_ALW, 0, "Uart force to Shutdown");
	nvmet_csts_shst(NVME_SHST_OCCURRING);
	nvmet_shut_down();
	return 0;
}

static DEFINE_UART_CMD(shutdown, "shutdown",
					   "manually trigger shutdown",
					   "No parameter needed",
					   0, 0, force_shutdown);
*/

/*! \endcond */

/*! @} */
