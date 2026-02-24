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
/*! \file cmd_proc.c
 * @brief Rainier Command Proc Module
 *
 * \addtogroup hal
 * \defgroup cmd_proc command processor
 * \ingroup hal
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "nvme_spec.h"
#include "cmd_proc.h"
#include "registers/common_ctrl.h"
#include "registers/port_ctrl.h"
#include "hal_nvme.h"
#include "bf_mgr.h"
#include "event.h"
#include "assert.h"
#include "rainier_soc.h"
#include "vic_id.h"
#include "io.h"
#include "nvmet.h"
#include "req.h"
#include "misc.h"
#include "btn_export.h"
#include "ns.h"
#include "nvme_decoder.h"
#include "spin_lock.h"
#include "cpu_msg.h"
#include "smart.h"
#include "console.h"
#include "flex_misc.h"

#if defined(OC_SSD)
#include "ocssd.h"
#endif

#if CO_SUPPORT_SANITIZE
// Jack Li, for Sanitize
#include "epm.h"
extern epm_info_t *shr_epm_info;
extern epm_smart_t *epm_smart_data;
extern u8 sntz_chk_cmd_proc;
#endif

#if TCG_WRITE_DATA_ENTRY_ABORT
#include "tcgcommon.h"
#endif

/*! \cond PRIVATE */
#define __FILEID__ cmdproc
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
/* Don't change those hw numbers */
#define NUM_LOTS_DELV 64 ///< number of lot for nvme_cmd delivery
#define NUM_LOTS_CPL 64	 ///< number of lot for cmd_proc completion

#define NUM_ERR_CMPL_CMD 16 ///< number of err completion cmd

#define NUM_LOTS_DEL_0_BASED (NUM_LOTS_DELV - 1)	  ///< lot for nvme_cmd delivery, 0 based
#define NUM_LOTS_CPL_0_BASED (NUM_LOTS_CPL - 1)		  ///< lot for cmd_proc completion, 0 based
#define NUM_CMDP_RX_CMD_0_BASED (NUM_CMDP_RX_CMD - 1) ///< lot for cmd_proc rx command, 0 based
#define NUM_ERR_CMPL_CMD_0_BASED (NUM_ERR_CMPL_CMD - 1)

#define NUM_CMDP_ABORT_CMD 8							  ///< number of lot for cmd_proc abort nvme command
#define NUM_CMDP_ABORT_CMD_BASED (NUM_CMDP_ABORT_CMD - 1) ///< lot for cmd_proc rx command, 0 based

#if defined(OC_SSD)
#define NUM_LOTS_PPA_Q 16						   ///< number of slots in Physical Page Address Queue
#define NUM_LOTS_PPAQ_0_BASED (NUM_LOTS_PPA_Q - 1) ///< lot for cmd_proc rx command, 0 based
#endif

#define CMD_ABORT_QDEPTH 4 ///< number of command abort queue depth

#define MAX_CMD_SLOT_CNT_SHIFT 8
#define MAX_CMD_SLOT_CNT (1 << MAX_CMD_SLOT_CNT_SHIFT)

#define MAX_FW_CMD_SLOT_SHIFT 5
#define MAX_FW_CMD_SLOT_CNT (1 << MAX_FW_CMD_SLOT_SHIFT)

#if CPU_ID == 1
#define INT_MASK INT_MASK_1
#elif CPU_ID == 2
#define INT_MASK INT_MASK_2
#elif CPU_ID == 3
#define INT_MASK INT_MASK_3
#elif CPU_ID == 4
#define INT_MASK INT_MASK_4
#endif

#if defined(SRIOV_SUPPORT)
#define MAX_FUNC_NUM SRIOV_PF_VF_PER_CTRLR
#else
#define MAX_FUNC_NUM 1
#endif
#define MAX_RETRY_DELETE_SQ_COUNT      0x100

#define GPIO_INT                                 0x00000048
#define   GPIO_INT_48_MASK                       0x0000ffff
#define   GPIO_INT_48_SHIFT                               0

#define   GPIO_PLP_DETECT_SHIFT                  (3)    //GPIO 3

extern u16 req_cnt;
extern smart_statistics_t *smart_stat;
extern tencnet_smart_statistics_t *tx_smart_stat;
extern volatile u8 plp_trigger;
extern volatile u8 cur_ro_status;
//extern bool ECCT_HIT_2_FLAG;
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
extern u16 shr_E2E_RefTag_detection_count;	// DBG, SMARTVry
extern u16 shr_E2E_AppTag_detection_count;
extern u16 shr_E2E_GuardTag_detection_count;

extern void nvmet_dump_dma_prp(u32 prp_hi, u32 prp_lo, u32 nlb);


extern void smart_inc_err_cnt();
#if defined(OC_SSD)
/*!
 * @brief ppa queue entry definition
 */
typedef union
{
	u32 all;
	struct
	{
		u32 ppa_buf_addr : 23; /*! [22:0] PPA buffer address (each buffer is 32 bytes) */
		u32 ppa_buf_loc : 1;   /*! PPA buffer location 0h:SRAM, 1h:BTCM */
		u32 rsvd : 8;		   /*! reserved */
	} b;
} ppaq_entry_t;
#endif

#define ERR_CMPL_ERR_MASK (0xFFFF | BIT(31) | BIT(30) | BIT(29))

/*!
 * @brief err completion queue entry definition
 */
typedef struct
{
	/*dword 0*/
	union
	{
		u32 all;
		struct
		{
			u32 cq_full : 1;		   /*! bit0 cq was full */
			u32 cmd_err : 1;		   /*! bit1 command completed with error */
			u32 cmd_abort : 1;		   /*! bit2 command aborted */
			u32 prp_offset_inv : 1;	   /*! bit3 prp offset invalid */
			u32 sgl_desc_type_inv : 1; /*! bit4 sgl descriptor type invalid */
			u32 sgl_sub_type_inv : 1;  /*! bit5 sgl sub type invalid */
			u32 data_sgl_len_inv : 1;  /*! bit6 data sgl length invalid */
			u32 pcie_read_timeout : 1; /*! bit7 PCIe read timeout */
			u32 sgl_num_inv : 1;	   /*! bit8 sgl number invalid */
			u32 sgl_seg_inv : 1;	   /*! bit9 sgl segment invalid */
			u32 lbrt_err : 1;		   /*! bit10 end-to-end reference tag check error */
			u32 lbat_err : 1;		   /*! bit11 end-to-end application tag check error */
			u32 guard_err : 1;		   /*! bit12 end-to-end guard check error */
			u32 par_err : 1;		   /*! bit13 sram parity error */
			u32 data_ecc_err : 1;	   /*! bit14 fifo data ecc error */
			u32 hs_crc_err : 1;		   /*! bit15 Host sector CRC error */
			u32 cmd_slot : 9;		   /*! bit24:16 cmd slot */
			u32 err_lba_h : 4;		   /*! bit 28:25 First error LBA bits [35:32] */
			u32 btn_rd_err : 1;		   /*! bit 29 BTN read commnd error */
			u32 cq_err : 1;			   /*! bit 30*/
			u32 phase : 1;			   /*! bit 31*/
		} b;
	} dw0;
	/*dword 1*/
	u32 err_lba_l; /*! bit 63:32: first error LBA bits [31:0] */
	/*dword 2*/
	union
	{
		u32 all;
		struct
		{
			u16 cid;	   /*! bit 79:64: nvme command id (in command dw0) */
			u16 sq_id : 8; /*! bit 87:80: sq id */
			u16 rsvd1 : 8;
		} b;
	} dw2;
	/*dword 3*/
	u32 rsvd2;
} err_cmpl_queue_t;

/*!
 * @brief cmd_proc rx command receive queue
 */
typedef union
{
	u32 all;
	struct
	{
		u32 sq_id : 8;		 /*! bit7:0 sq id */
		u32 function_id : 7; /*! bit14:8 function id */
		u32 priority : 1;	 /*! bit15 priority */
		u32 lr_err : 1;		 /*! bit16 LBA range error */
		u32 nsid_inv : 1;	 /*! bit 17 namespace id invalid */
		u32 mdts_err : 1;	 /*! bit 18 max data transfer size error */
		u32 fatal_err : 1;	 /*! parity error or host read timeout for PPA */
		u32 ppa_sp : 12;	 /*! open channel commmand ppa q starting pointer */
	} b;
} cmd_rx_queue_t;

typedef struct _ps_ns_fmt_t
{
	u64 nsze;  /*! namespace size, in logical block */
	u64 ncap;  /*! namespace capacity, in logical block */
	u8 flbas;  /*! formatted lba size */
	u8 pit;	   /*! PI type */
	u8 pil;	   /*! metadata start */
	u8 lbaf;   /*! lba format index */
	u8 ms;	   /*! metadata separated or extended */
	u8 nsfeat; /*! ns features ctrl, detail refer idfy cns 00h byte 24 */

	struct
	{
		u16 nabsn;	/*! ns atomic boundary size normal */
		u16 nacwu;	/*! ns atomic compare & write unit */
		u16 nabo;	/*! ns atomic boundary offset */
		u16 nabspf; /*! ns atomic boundary size power fail */
	} nabp;			/*! ns atomic boundary parameter*/

	u64 start_lba; /*! start lba, raw disk level */
	enum nsid_type type;
	enum nsid_wp_state wp_state;
} ps_ns_fmt_t;

/*! @brief shutdown callback function type */
#if defined(SRIOV_SUPPORT)
typedef void (*shutdown_cb_t)(u8 fun_id);
#else
typedef void (*shutdown_cb_t)(void);
#endif

typedef struct
{
	u32 nvm_cmd_id : 9; /*! NVM_CMD slot index in CMD_PROC */
	u32 cmd_type : 2;	/*! cmd type: 00/rd, 01/wr, 10/compare */
	u32 auto_cmpl : 1;	/*! always set to 0 for internal NVM cmd*/
	u32 priority : 1;	/*! 0: normal priority, 1: high priority*/
	u32 sq_id : 8;		/*! not overlap with host SQs */
	u32 rsvd0 : 1;
	u32 func_id : 7;  /*!  */
	u32 protocol : 1; /*! 0: nvme; 1: open channel */
	u32 rsvd1 : 2;
	u32 cmd_buf_addr : 23; /*! address to 64-byte command buffer (32-byte aligned) */
	u32 cmd_buf_loc : 1;   /*! command buffer location, 0: SRAM; 1: BTCM */
	u32 rsvd2 : 8;
} nvm_cmd_entry_t;

typedef struct _cmd_proc_dis_func_ctx_t
{
	void *ctx;
	u16 deleting;
	u16 retry_cnt;
	u32 del_sq_bmp[3];
	cmd_proc_dis_func_cb_t cmpl;
} cmd_proc_dis_func_ctx_t;

typedef union {
    u32 all;
    struct {
        u32 gpio_int_48:16;
        u32 rsvd_16:16;
    } b;
} gpio_int_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
/* Both in SRAM */
static fast_data_ni u32 rx_cmd_base[NUM_CMDP_RX_CMD] __attribute__((aligned(32)));			   ///< controller command entry queue
static fast_data_ni nvm_cmd_entry_t isu_cmd_base[NUM_FW_NVM_CMD] __attribute__((aligned(64))); ///< fw nvm cmd issuing queue
static fast_data_ni struct nvme_cmd fw_nvm_cmd[NUM_FW_NVM_CMD] __attribute__((aligned(64)));   ///< fw NVM command array
static fast_data_ni u32 fw_cmd_act_bitmap;													   ///< need adjust it if more than 32 NVM_CMD slots reserved for FW issuing commands

static fast_data_ni cmd_rx_queue_t rx_q_base[NUM_CMDP_RX_CMD] __attribute__((aligned(32))); ///< rx command receive queue
static fast_data_ni err_cmpl_queue_t err_cmpl_base[NUM_ERR_CMPL_CMD] __attribute__((aligned(32)));
static fast_data_ni u32 abort_cmd_base[NUM_CMDP_ABORT_CMD] __attribute__((aligned(32)));			   ///< controller command entry queue
static fast_data_ni cmd_abort_cmpl_t abort_cmpl_base[NUM_CMDP_ABORT_CMD] __attribute__((aligned(32))); ///< abort command Completion queue

static fast_data_ni u8 rx_q_rptr; ///< rx command receive queue read pointer
static ps_data ps_ns_fmt_t _ps_ns_fmt[NVMET_NR_NS];
static fast_data_ni u8 err_cmpl_rptr; ///< err cmpl read pointer
static fast_data_ni u8 leave_ro_rx_wptr;///< for leave read only check all cmd in rx pool is finish

#if defined(OC_SSD)
static fast_data_ni u32 ppaq_base[NUM_LOTS_PPA_Q] __attribute__((aligned(32))); ///< PPA queue base
static fast_data_ni u32 ppaq_rptr;												///< PPA Q read pointer
#endif

extern u16 req_cnt;
fast_data LIST_HEAD(_del_sq_reqs); ///< request pending queue

fast_data_ni static cmd_proc_dis_func_ctx_t _dis_func_ctx[MAX_FUNC_NUM];
extern volatile u8 plp_trigger;

//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
/*!
 * @brief registers read access
 *
 * @param reg	which register to access
 *
 * @return	register value
 */
static fast_code u32 cmd_proc_readl(u32 reg)
{
	return readl((void *)(NVME_BASE + reg));
}

/*!
 * @brief registers write access
 *
 * @param val	the value to update
 * @param reg	which register to access
 *
 * @return	None
 */
static fast_code void cmd_proc_writel(u32 val, u32 reg)
{
	writel(val, (void *)(NVME_BASE + reg));
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

fast_code bool is_cmd_proc_dma_idle(void)
{
	abort_xfer_t xfer_sts = {.all = cmd_proc_readl(ABORT_XFER)};
	u32 idle_chk_mask = WRITE_HMB_PCIE_IDLE_MASK |
						READ_HMB_PCIE_IDLE_MASK |
						WRITE_HMB_IDLE_MASK |
						READ_HMB_IDLE_MASK |
						WRITE_DMA_PCIE_IDLE_MASK |
						READ_DMA_PCIE_IDLE_MASK |
						WRITE_DMA_IDLE_MASK |
						READ_DMA_IDLE_MASK |
						WRITE_PRP_IDLE_MASK |
						DATA_XFER_PCIE_IDLE_MASK |
						DATA_XFER_IDLE_MASK;

	return (idle_chk_mask == (xfer_sts.all & idle_chk_mask)) ? true : false;
}

/*!
 * @brief check stuck dtag
 *
 * @param dtag	dtag id
 *
 * @return	dtag index
 */
fast_code u8 cmd_proc_wdma_stuck_dtag(u32 *dtag)
{
	u8 dtag_idx = 0;

	dtag[0] = _inv_dtag.dtag;
	dtag[1] = _inv_dtag.dtag;

	// check write path
	cmd_proc_writel(4, DATA_XFER_DEBUG_ADDR);
	volatile wr_dma_axi_wr_dbg_4_t reg4 = { .all = cmd_proc_readl(DATA_XFER_DEBUG_DATA), };

	cmd_proc_writel(5, DATA_XFER_DEBUG_ADDR);
	wr_dma_axi_wr_dbg_5_t reg5 = { .all = cmd_proc_readl(DATA_XFER_DEBUG_DATA), };

	cmd_proc_writel(6, DATA_XFER_DEBUG_ADDR);
	wr_dma_axi_wr_dbg_6_t reg6 = { .all = cmd_proc_readl(DATA_XFER_DEBUG_DATA), };

	cmd_proc_writel(7, DATA_XFER_DEBUG_ADDR);
	wr_dma_axi_wr_dbg_7_t reg7 = { .all = cmd_proc_readl(DATA_XFER_DEBUG_DATA), };

	cmd_proc_writel(0x18, DATA_XFER_DEBUG_ADDR);
	u32 reg_x18 = cmd_proc_readl(DATA_XFER_DEBUG_DATA);

	cmd_proc_writel(0x19, DATA_XFER_DEBUG_ADDR);
	u32 reg_x19 = cmd_proc_readl(DATA_XFER_DEBUG_DATA);

	cmd_proc_trace(LOG_WARNING, 0x277e, ":::%x %x %x %x", reg4.all, reg5.all, reg6.all, reg7.all);
	cmd_proc_trace(LOG_WARNING, 0xc24d, "->> %x %x", reg_x18, reg_x19);

	if (0 != reg6.dt_cs) {
		// data xfer state machine unable reach to idle state
		dtag[dtag_idx ++] = reg6.dq_dtag;
	}

	if ((3 == reg4.aw_cs) || (4 == reg4.aw_cs) || (8 == reg4.aw_cs)
			|| (9 == reg4.aw_cs) || (2 == reg4.aw_cs)) {
		if (2 != reg4.aw_cs) {
			if (!((reg6.dq_dtag == reg4.aw_dtag) && (0 != reg6.dt_cs)))
				dtag[dtag_idx ++] = reg4.aw_dtag;
		}
	} else {
		if (0 != reg4.aw_cs) {
			u32 val = reg4.aw_cs;

			do {
				cmd_proc_writel(4, DATA_XFER_DEBUG_ADDR);
				reg4.all = cmd_proc_readl(DATA_XFER_DEBUG_DATA);
			} while (0);//val == reg4.aw_cs); //for jira_10. solution(3/12).IG based patches.shengbin yang 2023/11/15

			cmd_proc_trace(LOG_WARNING, 0x8067, "pre: %x after: %x", val, reg4.aw_cs);
		}
	}

	return dtag_idx;
}

fast_code void cmd_proc_set_payload_sz(u32 max_pl_sz, u32 max_rd_sz)
{
	host_ctrl_t dft = {.all = cmd_proc_readl(HOST_CTRL)};

	dft.b.max_payload_sz = max_pl_sz;	/* 0: 128 bytes, 1: 256 bytes, 2,3: 512 bytes */
	dft.b.pcie_max_read_sz = max_rd_sz; /* 0: 128 bytes, 1: 256 bytes, 2,3: 512 bytes */
	cmd_proc_writel(dft.all, HOST_CTRL);
}

#if defined(SRIOV_SUPPORT)
/*!
 * @brief This function set up Namespace table index for Virtual functions
 *
 * @param ns_tbl_idx	namespace table index 1 to 32
 * @param hss		host sector size
 * @param lbacnt	LBA count
 *
 * @return	None
 */
fast_code static void cmd_proc_set_vf_ns_table(u32 ns_tbl_idx, u8 hss, u64 lbacnt)
{
	max_lba_l_t ns_l = {.all = 0};
	max_lba_h_t ns_h = {.all = 0};
	name_space_ctrl_t ns_c = {.all = 0};

	/* name space info update */
	/* 1. set values */
	ns_l.b.max_lba_low = lbacnt;
	cmd_proc_writel(ns_l.all, MAX_LBA_L);

	ns_h.b.max_lba_high = lbacnt >> 32;
	ns_h.b.host_sector_size = hss == 12 ? 1 : 0;
	ns_h.b.pi_ctrl = 0;		  /* TODO change to enum type */
	ns_h.b.meta_size = 0;	  /* TODO */
	ns_h.b.meta_separate = 0; /* TODO */
	ns_h.b.pi_location = 0;	  /* TODO */
	cmd_proc_writel(ns_h.all, MAX_LBA_H);

	/* 2. write to ns ram */
	ns_c.b.name_space_table_rw = 1;
	ns_c.b.name_space_table_id = ns_tbl_idx;
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);
}
#endif

ddr_code void cmd_proc_ns_cfg(nvme_ns_hw_attr_t *attr, u32 meta_size, u32 lbas_shift)//joe slow->ddr 20201124
{
	u64 ns_cap = attr->lb_cnt;

	max_lba_l_t ns_l = {.all = 0};
	ns_l.b.max_lba_low = (u32)(ns_cap & 0xFFFFFFFF);
	cmd_proc_writel(ns_l.all, MAX_LBA_L);

	max_lba_h_t ns_h = {.all = 0};
	ns_h.b.max_lba_high = ns_cap >> 32;
	ns_h.b.host_sector_size = (lbas_shift == 12) ? 1 : 0;
	ns_h.b.pi_ctrl = attr->pit;
	ns_h.b.meta_size = (16 > meta_size) ? 0 : 1;
	ns_h.b.meta_separate = attr->ms; // DIF or DIX
	ns_h.b.pi_location = attr->pil;
	ns_h.b.par_write_set = attr->pad_pat_sel; // 0: 0's; 1: 1's
	ns_h.b.meta_only_en = (meta_size > 0) && (0 == attr->pit);

	ns_h.b.ns_wr_prot = attr->wr_prot;

	cmd_proc_writel(ns_h.all, MAX_LBA_H);

	/* 2. write to ns ram */
	name_space_ctrl_t ns_c = {.all = 0};
	ns_c.b.name_space_table_rw = 1;
#if defined(SRIOV_SUPPORT)
	/* PF Table ID 0 */
	ns_c.b.name_space_table_id = attr->nsid - 1;
#else
	ns_c.b.name_space_table_id = attr->nsid;
#endif
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);

	//-----check flow Eric 20231018-----//
	/*ns_l.all = 0;
	ns_h.all = 0;
	ns_c.all = 0;
	ns_l.all = cmd_proc_readl(MAX_LBA_L);
	ns_h.all = cmd_proc_readl(MAX_LBA_H);
	ns_c.all = cmd_proc_readl(NAME_SPACE_CTRL);
	nvme_apl_trace(LOG_INFO, 0, "hw LBAcnt: %x-%x\n",ns_h.b.max_lba_high,ns_l.b.max_lba_low);
	nvme_apl_trace(LOG_INFO, 0, "Eric: name_space_table_id: %d name_space_table_rw: %x", ns_c.b.name_space_table_id,ns_c.b.name_space_table_rw);*/
	/**/
	//--------------------//



#if defined(SRIOV_SUPPORT)
	/* setup namespace table for virtual functions. In this model
	 * one namespace (ID=1) is shared between PF0 and all VFs.
	 * So PF0, VF1 to VF32 share namespace ID 1 */
	u8 i;
	for (i = 0; i < SRIOV_VF_PER_CTRLR; i++)
	{
		cmd_proc_set_vf_ns_table(i + 1, lbads, lbacnt);
	}
#endif
}
/*!
 * @brief setup LBA format
 * Configure name space's LBA format, etc. 9 for 512 and 12 for 4K
 * Note that only 9 and 12 is supported.
 *
 * @param nsid		Namespace ID
 * @param lbads		LBA data size shift, i.e. 9 for 512 and 12 for 4K
 * @param lbacnt	max lba number
 * @param hmeta_size	host metadata buffer size per sector
 * @param pi_type	PI type, NVME Spec:
 *			0 means PI is disabled
 *			1 means PI is enabled, type 1
 *			2 means PI is enabled, type 2
 *			3 means PI is enabled, type 3
 * @param meta_set	Metadata settings, NVME Spec:
 *			0 means metadata transferred separate,
 *			1 means metadata transferred as part of an extended data LBA
 * @param pil		PI location, NVME Spec:
 *			0 means PI is transferred as the last 8 bytes of metadata
 *			1 means PI is transferred as the first 8 bytes of metadata
 *
 * @return              not used
 */

ddr_code void cmd_proc_set_lba_format(u32 nsid, u8 lbads, u64 lbacnt, u16 hmeta_size)//joe slow->ddr 20201124
{


	host_ctrl_t dft = {.all = cmd_proc_readl(HOST_CTRL)};
	mode_ctrl_t mct = {.all = cmd_proc_readl(MODE_CTRL)};

#if 0 // todo, when modify set namespace attribute function.
	_ps_ns_fmt[nsid - 1].en = 1;
	_ps_ns_fmt[nsid - 1].lbads = lbads;
	_ps_ns_fmt[nsid - 1].nlb = lbacnt;
	_ps_ns_fmt[nsid - 1].ms = hmeta_size;
#endif
	dft.b.du_size = (DTAG_SHF == 13) ? 1 : 0;

#if defined(DISABLE_HS_CRC_SUPPORT) && !defined(USE_CRYPTO_HW)
	mct.b.par_du_rd_mode = 0;
#if defined(FAST_MODE)
	mct.b.par_du_rd_mode = 1;
#endif
#else
	mct.b.par_du_rd_mode = 1;
#endif
	mct.b.fw_cmd_q_depth = 3;
	mct.b.flush_all_ns_en = 1; // nvme 1.4 todo: update VWC in identify
#if !defined(DISABLE_HS_CRC_SUPPORT)
	mct.b.hs_crc_report_en = 1;
#endif

#if PI_SUPPORT
	mct.b.chk_lbrt = 0;//0:ignore LBRT error for PI mode 3
#endif

	cmd_proc_writel(mct.all, MODE_CTRL);
	cmd_proc_writel(dft.all, HOST_CTRL);

	/* name space info update */
	/* 1. set values */
	max_lba_l_t ns_l = {.all = 0};
	ns_l.b.max_lba_low = lbacnt;
	cmd_proc_writel(ns_l.all, MAX_LBA_L);//Eric 20231016 move

	max_lba_h_t ns_h = {.all = 0};
	ns_h.b.max_lba_high = lbacnt >> 32;
	ns_h.b.host_sector_size = lbads == 12 ? 1 : 0;
	ns_h.b.pi_ctrl = 0;		  /* TODO change to enum type */
	ns_h.b.meta_size = 0;	  /* TODO */
	ns_h.b.meta_separate = 0; /* TODO */
	ns_h.b.pi_location = 0;	  /* TODO */
	ns_h.b.par_write_set = 0; //joe add NS 20200813  1--->0  fill up zero data
	ns_h.b.meta_size = hmeta_size / 8;
	cmd_proc_writel(ns_h.all, MAX_LBA_H);//Eric 20231016 move

	/* 2. write to ns ram */
	name_space_ctrl_t ns_c = {.all = 0};
	ns_c.b.name_space_table_rw = 1;
#if defined(SRIOV_SUPPORT)
	/* PF Table ID 0 */
	ns_c.b.name_space_table_id = nsid - 1;
#else
	ns_c.b.name_space_table_id = nsid;
#endif

	//Eric 20231016 for EDVX PCIe interupt out of ranges
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);


	//-----check flow 20231018-----//
	ns_l.all = 0;
	ns_h.all = 0;
	ns_c.all = 0;
	ns_l.all = cmd_proc_readl(MAX_LBA_L);
	ns_h.all = cmd_proc_readl(MAX_LBA_H);
	ns_c.all = cmd_proc_readl(NAME_SPACE_CTRL);
	nvme_apl_trace(LOG_INFO, 0x3edb, "check LBAcnt: %x-%x\n",ns_h.b.max_lba_high,ns_l.b.max_lba_low);
	nvme_apl_trace(LOG_INFO, 0xbafb, "check id: %d rw: %x", ns_c.b.name_space_table_id,ns_c.b.name_space_table_rw);
	//--------------------//

#if defined(SRIOV_SUPPORT)
	/* setup namespace table for virtual functions. In this model
	 * one namespace (ID=1) is shared between PF0 and all VFs.
	 * So PF0, VF1 to VF32 share namespace ID 1 */
	u8 i;
	for (i = 0; i < SRIOV_VF_PER_CTRLR; i++)
	{
		cmd_proc_set_vf_ns_table(i + 1, lbads, lbacnt);
	}
#endif
}
/*!
 * @brief set mps from cc.en
 *
 * @param mps	host memory page shift bit from 4K
 *
 * @return   	not used
 */
fast_code void cmd_proc_set_mps(u32 mps)
{
	host_ctrl_t dft = {.all = cmd_proc_readl(HOST_CTRL)};

	dft.b.mps = mps;
	cmd_proc_writel(dft.all, HOST_CTRL);
}

#if defined(HMETA_SIZE)
fast_code void cmd_proc_hmeta_ctrl(u8 pi_type, u8 meta_set, u8 pil, u16 nsid, u16 hmeta_size)
{
#if 0 // todo, when modify set namespace attribute function.
	_ps_ns_fmt[nsid - 1].pi_type = pi_type;
	_ps_ns_fmt[nsid - 1].meta_set = meta_set;
	_ps_ns_fmt[nsid - 1].pil = pil;
	_ps_ns_fmt[nsid - 1].ms = hmeta_size;
#endif
	// 1. set name_space_ctrl register to corresponding nsid
	name_space_ctrl_t ns_c = {.all = 0};
	ns_c.b.name_space_table_rw = 0;
	ns_c.b.name_space_table_id = nsid;
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);

	// 2. read out MAX_LBA_H and fill corresponding pi setting of nsid
	max_lba_h_t ns_mlh = {.all = cmd_proc_readl(MAX_LBA_H)};
	ns_mlh.b.pi_ctrl = pi_type;
	ns_mlh.b.meta_separate = (((hmeta_size > 0) && (meta_set > 0)) ? 0 : 1); ///<CMD processor spec v0.82: 0 means metadata is part of user data, 1 means metadata is separate
	ns_mlh.b.pi_location = (pil ? 0 : 1);									 ///<CMD processor spec v0.82: 0 means first 8 byte, 1 means last 8 byte
	ns_mlh.b.meta_size = hmeta_size > 8 ? 1 : 0;
	ns_mlh.b.meta_only_en = (pi_type == 0) ? 1 : 0; ///<enable meta only, if meta size is none zero, pi type 0 (disable)

	// 3. write pi setting to MAX_LBA_H
	cmd_proc_writel(ns_mlh.all, MAX_LBA_H);

	// 4. write name_space_ctrl register to take effect nsid
	ns_c.b.name_space_table_rw = 1;
	ns_c.b.name_space_table_id = nsid;
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);
}

ddr_code void dump_proc_hmeta_ctrl(u16 nsid)
{
	name_space_ctrl_t ns_c = {.all = 0};
	ns_c.b.name_space_table_rw = 0;
	ns_c.b.name_space_table_id = nsid;
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);

	// 2. read out MAX_LBA_H and fill corresponding pi setting of nsid
	max_lba_h_t ns_mlh = {.all = cmd_proc_readl(MAX_LBA_H)};

	nvme_apl_trace(LOG_ERR, 0xef00, "nsid: %x, pi_hw_setting:%x",nsid,ns_mlh.all);

	// 4. write name_space_ctrl register to take effect nsid
	ns_c.b.name_space_table_rw = 1;
	ns_c.b.name_space_table_id = nsid;
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);

}

#endif //HMETA_SIZE

#ifdef NS_MANAGE
/*!
 * @brief load namespace data struct from nand flash
 *
 * @return		not used
 */
UNUSED static fast_code void cmd_proc_load_ns_struct(void)
{
	nvme_apl_trace(LOG_INFO, 0x89a7, "Empty function, load ns struct from nand.");
#if 0
	for (i = 0; i < NVMET_NR_NS; i++) {
		if (_ps_ns_fmt[i].en == 0)
			continue;

		cmd_proc_set_lba_format(i + 1, _ps_ns_fmt[i].lbads, _ps_ns_fmt[i].nlb, _ps_ns_fmt[i].ms);
#if defined(HMETA_SIZE)
		cmd_proc_hmeta_ctrl(_ps_ns_fmt[i].pi_type, _ps_ns_fmt[i].meta_set, _ps_ns_fmt[i].pil, i + 1, _ps_ns_fmt[i].ms);
#endif
	}
#endif
}

/*!
 * @brief delete namespace valid bitmap
 *
 * @param u32 ns_id, host command namespace id
 *
 * @return      not used
 */
fast_code void cmd_proc_ns_del(u32 ns_id)
{
	sys_assert(ns_id <= 32); // valid bitmap only support 32 namespace id.

	if (ns_id < 32)
		cmd_proc_writel(BIT(ns_id), NAME_SPACE_VLD_0);
	else
		cmd_proc_writel(BIT(0), NAME_SPACE_VLD_1);
}

/*!
 * @brief change namespace wp state
 *
 * @param u32 ns_id, host command namespace id
 * @param bool wp, write protect or not
 *
 * @return      not used
 */
ddr_code void cmd_proc_ns_set_wp(u32 ns_id, bool wp)//joe slow->ddr 20201124
{
	sys_assert(ns_id <= 32); // valid bitmap only support 32 namespace id.

	// step 1. select nsid
	name_space_ctrl_t ns_c = {.all = 0};
	ns_c.b.name_space_table_rw = 0;
	ns_c.b.name_space_table_id = ns_id;
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);

	// step 2. change wp state
	max_lba_h_t ns_h = {.all = cmd_proc_readl(MAX_LBA_H)};
	ns_h.b.ns_wr_prot = wp;
	cmd_proc_writel(ns_h.all, MAX_LBA_H);

	// step 3. enable wp state
	ns_c.b.name_space_table_rw = 1;
	cmd_proc_writel(ns_c.all, NAME_SPACE_CTRL);
}
#endif

/*!
 * @brief cmd_proc abort command complete.
 *
 * @return      not used
 */
slow_code void cmd_proc_cmd_abort_cmpl(void)
{
	abort_cmpl_q_ptr_t cmd_proc_abort_pnter = {
		.all = cmd_proc_readl(ABORT_CMPL_Q_PTR),
	};
	u8 abort_wptr = cmd_proc_abort_pnter.b.abort_cmpl_wr_ptr;
	u8 abort_rptr = cmd_proc_abort_pnter.b.abort_cmpl_rd_ptr;

	while (abort_wptr != abort_rptr)
	{
		u32 cdw10;
		cmd_abort_cmpl_t *cmpl;

		cmd_proc_trace(LOG_ALW, 0xc6ac, "abort_cmpl sq(%d) cid(0x%x) status(0x%x)",
					   abort_cmpl_base[abort_rptr].b.sq_id,
					   abort_cmpl_base[abort_rptr].b.cmd_id,
					   abort_cmpl_base[abort_rptr].all);

		cmpl = &abort_cmpl_base[abort_rptr];
		cdw10 = (cmpl->b.cmd_id << 16) | cmpl->b.sq_id;
		evt_set_imt(evt_abort_cmd_done, cdw10, cmpl->all);
		abort_rptr = (abort_rptr + 1) & NUM_CMDP_ABORT_CMD_BASED;
		cmd_proc_abort_pnter.b.abort_cmpl_rd_ptr = abort_rptr;
		cmd_proc_writel(cmd_proc_abort_pnter.all, ABORT_CMPL_Q_PTR);
	}
}

/*!
 * @brief This function abort single command using HW Abort Req and Abort Cmpl Q
 *
 * @param sqid		SQ ID of the command (cdw10[15:00])
 * @param cmd_id	CMD ID of the command (cdw10[31:16])
 *
 * @return		not used
 */
slow_code void cmd_proc_abort_single_cmd(u16 sqid, u16 cmd_id)
{
	abort_q_ptr_t abort_q_p = {.all = cmd_proc_readl(ABORT_Q_PTR)};
	u32 entry;

	/* Take care of abort_q_wr_ptr wrap condition */
	while (((abort_q_p.b.abort_q_wr_ptr + 1) & NUM_CMDP_ABORT_CMD_BASED) ==
		   abort_q_p.b.abort_q_rd_ptr)
	{
		cmd_proc_cmd_abort_cmpl();
		abort_q_p.all = cmd_proc_readl(ABORT_Q_PTR);
	}

	entry = ((u32)sqid << 16) | cmd_id;
	abort_cmd_base[abort_q_p.b.abort_q_wr_ptr] = entry;
	abort_q_p.b.abort_q_wr_ptr = (abort_q_p.b.abort_q_wr_ptr + 1) & NUM_CMDP_ABORT_CMD_BASED;
	cmd_proc_writel(abort_q_p.all, ABORT_Q_PTR);
}

ddr_code UNUSED void cmd_proc_debug_mdts(void)
{
	err_cmpl_queue_t *err_cmpl_cmd;
	err_cmpl_q_ptr_t err_cmpl_ptr = {
		.all = cmd_proc_readl(ERR_CMPL_Q_PTR),
	};
#if(PLP_SUPPORT == 1)
	gpio_int_t gpio_int_status;
#endif

	u8 err_cmpl_wptr = err_cmpl_ptr.b.err_cmpl_wr_ptr;
	evlog_printk(LOG_INFO,"[MDTS] err_cmpl_wptr %x \n", err_cmpl_wptr);

	u32 i;
	u32 cmd_slot;

	debug_control_t dbg_ctrl;
	evlog_printk(LOG_INFO,"debug mtds dump:\n");
	evlog_printk(LOG_INFO,"doorbell reg :\n");
	mem_dump((void *)0xC0034000, 70);
	mem_dump((void *)0xC0034300, 70);
	evlog_printk(LOG_INFO,"cmd_proc slot:\n");
	mem_dump((void *)0xC003c100, 16);

	cmd_rx_q_ptr_t cmd_proc_rx_pnter = {
		.all = cmd_proc_readl(CMD_RX_Q_PTR),
	};
	evlog_printk(LOG_INFO,"cmd_proc_rx_pnter %x\n", cmd_proc_rx_pnter.all);
	evlog_printk(LOG_INFO,"error complete queue base[0xC003C080] %x, qptr[0xC003C084] %x \n",
		cmd_proc_readl(ERR_CMPL_Q_BASE), cmd_proc_readl(ERR_CMPL_Q_PTR) );

	if(plp_trigger)
		return;

	for (i = 0; i <= err_cmpl_rptr; i++)
	{
#if(PLP_SUPPORT == 1)
		gpio_int_status.all = misc_readl(GPIO_INT);
		if((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))||plp_trigger)
			return;
#endif
		err_cmpl_cmd = &err_cmpl_base[i];

		evlog_printk(LOG_INFO,"err_cmpl_cmd [%x] Slot[%x] [dw0]%x [dw1] %x, [dw2] %x \n", i,
				err_cmpl_cmd->dw0.b.cmd_slot,
				err_cmpl_cmd->dw0,
				err_cmpl_cmd->err_lba_l,
				err_cmpl_cmd->dw2);

		cmd_slot = err_cmpl_cmd->dw0.b.cmd_slot;

		u32 j;
		for (j = 0; j <= 0x10; j++) {

			if(plp_trigger)
				return;

			dbg_ctrl.b.debug_addr = (cmd_slot << 5) | j;
			dbg_ctrl.b.debug_en = 1;
			cmd_proc_writel(dbg_ctrl.all, DEBUG_CONTROL);
			evlog_printk(LOG_INFO," error cmd %x\n",cmd_proc_readl(TABLE_DATA));
		}

		cmd_proc_writel(0, DEBUG_CONTROL);
	}

}


/*!
 * @brief  cmd_proc rx NVMe I/O SQ read and write commands err handler
 *
 * @param rx_cmd	rx command
 * @param cmd		NVM command
 *
 * @return		not used
 */
ddr_code void cmd_proc_rw_err_handle(cmd_rx_queue_t *rx_cmd,
									  struct nvme_cmd *cmd)
{
	req_t *req;
	u32 sq_id = rx_cmd->b.sq_id;
	u8 funid = rx_cmd->b.function_id;
	struct nvmet_sq *sq = NULL;
	//struct insert_nvme_error_information_entry err_info;

#if defined(SRIOV_SUPPORT)
	if (funid == 0)
	{
		sq = ctrlr->sqs[sq_id];
	}
	else
	{
		u8 hst_sqid = sq_id - SRIOV_FLEX_PF_ADM_IO_Q_TOTAL -
					  (funid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
		sq = ctrlr->sr_iov->fqr_sq_cq[funid - 1].fqrsq[hst_sqid];
	}
#else
	sq = ctrlr->sqs[sq_id];
#endif

    //sys_assert(sq);
	if(sq == NULL)
	{
		cmd_proc_trace(LOG_ERR, 0x6f78, "SQ(%d) NULL %x", sq_id);
		hal_nvmet_put_sq_cmd(cmd);
		return;
	}

	req = nvmet_get_req();
	sys_assert(req);

	list_add_tail(&req->entry, &sq->reqs);

	req->host_cmd = cmd;
	req->error = 0;
	req->state = REQ_ST_FE_IO;
	req->req_from = REQ_Q_IO;

	req->fe.nvme.sqid = sq_id;
	req->fe.nvme.cntlid = funid;
	req->fe.nvme.cid = cmd->cid;
	req->fe.nvme.nvme_status = 0;
	req->fe.nvme.cmd_spec = 0;

	req->completion = nvmet_core_cmd_done;
	//extern vu32 Rdcmd;
	//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
	//Rdcmd++;
	//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);

	ctrlr->cmd_proc_running_cmds++;

	if (rx_cmd->b.nsid_inv)
	{
		cmd_proc_trace(LOG_DEBUG, 0x3543, "cmd ns %x", cmd->nsid);
#ifdef NS_MANAGE
		if ((cmd->nsid > 0 && cmd->nsid <= NVMET_NR_NS) &&
			(0 != ctrlr->nsid[cmd->nsid - 1].wp_state))
		{
			nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_NS_WRITE_PROTECTED);
		}
		else
#endif
		{
			if(cmd->nsid == 0xffffffff)
			{
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
			}
			else
			{
				nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE_OR_FORMAT);

				if ((cmd->opc != NVME_OPC_FLUSH) && (cmd->opc != NVME_OPC_WRITE) && (cmd->opc != NVME_OPC_READ) && (cmd->opc != NVME_OPC_WRITE_UNCORRECTABLE) && (cmd->opc != NVME_OPC_DATASET_MANAGEMENT))
				{
					nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
				}
			}
		}
		//err_info.error_location = 4;
	}
	else if (rx_cmd->b.mdts_err)
	{
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		//err_info.error_location = 48;
		cmd_proc_trace(LOG_ERR, 0x6279, "SLBA 0x%x%x NLB 0x%x", cmd->cdw11, cmd->cdw10, cmd->cdw12);
		cmd_proc_debug_mdts();
	}
	else if (rx_cmd->b.lr_err)
	{
		cmd_proc_trace(LOG_ERR, 0x00e9, "LBA %x%x cnt %x", cmd->cdw11, cmd->cdw10, cmd->cdw12);
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_LBA_OUT_OF_RANGE);
		//Eric 20231018
		//-----double check flow-----//
		max_lba_l_t ns_l = {.all = 0};
		max_lba_h_t ns_h = {.all = 0};
		name_space_ctrl_t ns_c = {.all = 0};
		ns_l.all = 0;
		ns_h.all = 0;
		ns_c.all = 0;
		ns_l.all = cmd_proc_readl(MAX_LBA_L);
		ns_h.all = cmd_proc_readl(MAX_LBA_H);
		ns_c.all = cmd_proc_readl(NAME_SPACE_CTRL);
		nvme_apl_trace(LOG_INFO, 0x91db, "hw LBAcnt: %x-%x\n",ns_h.b.max_lba_high,ns_l.b.max_lba_low);
		nvme_apl_trace(LOG_INFO, 0x8106, "Eric: name_space_table_id: %d name_space_table_rw: %x", ns_c.b.name_space_table_id,ns_c.b.name_space_table_rw);
		//--------------------//
		//err_info.error_location = 40;
	}
	else if (cmd->fuse)
	{
		cmd_proc_trace(LOG_ERR, 0x7ce9, "LBA %x%x cnt %x fuse %x", cmd->cdw11, cmd->cdw10, cmd->cdw12, cmd->fuse);
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
		//err_info.error_location = 8;
	}
#if (PI_SUPPORT)
	else if (rx_cmd->b.fatal_err)
	{
		//< TODO: Pcontinue to update PI relevant check with parity and other type error
		cmd_proc_trace(LOG_ERR, 0xc196, "[FATAL]PRINFO %x", ((cmd->cdw12 >> 26) & 0xF));
		dump_proc_hmeta_ctrl(cmd->nsid);

		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_PROTECTION_INFO);
	}
#endif

	cmd_proc_trace(LOG_ERR, 0x7565, "rx_cmd sq(%d) opcode:0x%x cid(%d) lr_err(%d) nsid_inv(%d) mdts_err(%d)",
				   rx_cmd->b.sq_id, cmd->opc, cmd->cid, rx_cmd->b.lr_err,
				   rx_cmd->b.nsid_inv, rx_cmd->b.mdts_err);

	req->completion(req);
#if 0
	struct nvme_status *status = (struct nvme_status *)(&req->fe.nvme.nvme_status);
	if(rx_cmd->b.lr_err != 1){//out of range not accord in log
		err_info.status.sc = status->sc;
		err_info.status.sct = status->sct;
		err_info.sqid = rx_cmd->b.sq_id;
		err_info.cid = cmd->cid;
		err_info.nsid = cmd->nsid;
		err_info.lba = ((u64)cmd->cdw11)<<32 | cmd->cdw10;
		nvmet_err_insert_event_log(err_info);
		}
#endif
}

#ifdef DUMP_SRAM_REG
ddr_code void dump_sram_error_reg()
{
  u32 i = 0;

#if(PLP_SUPPORT == 1)
  if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
  {
	  return;
  }
#endif


  cmd_proc_trace(LOG_INFO, 0x3c11, "----dump_sram_error_reg start------");
  cmd_proc_trace(LOG_INFO, 0x2503, "R0x%x : 0x%x",0xc001004C, readl((const void *)0xc001004C));
  // 2021/12/1 Close for plp hang
  /*
  for(i = 0xc0020218; i <= 0xc002023c; i += 4){
      cmd_proc_trace(LOG_INFO, 0, "R0x%x : 0x%x",i, readl((const void *)i));
  }
  */
  cmd_proc_trace(LOG_INFO, 0x156e, "R0x%x : 0x%x",0xc0010050, readl((const void *)0xc0010050));
  cmd_proc_trace(LOG_INFO, 0xaa0d, "R0x%x : 0x%x",0xc0010058, readl((const void *)0xc0010058));
  cmd_proc_trace(LOG_INFO, 0xd5eb, "R0x%x : 0x%x",0xc0040000, readl((const void *)0xc0040000));
  cmd_proc_trace(LOG_INFO, 0x2ebb, "R0x%x : 0x%x",0xc0040274, readl((const void *)0xc0040274));
  cmd_proc_trace(LOG_INFO, 0xa2c8, "R0x%x : 0x%x",0xc0040290, readl((const void *)0xc0040290));

  for(i = 0xc00402c4; i< 0xc00402e0; i += 4){
      cmd_proc_trace(LOG_INFO, 0x48c4, "R0x%x : 0x%x",i, readl((const void *)i));
  }
  cmd_proc_trace(LOG_INFO, 0xf0ab, "R0x%x : 0x%x",0xc00e0040, readl((const void *)0xc00e0040));
  cmd_proc_trace(LOG_INFO, 0x42f9, "R0x%x : 0x%x",0xc00e0044, readl((const void *)0xc00e0044));
  cmd_proc_trace(LOG_INFO, 0xa8fe, "R0x%x : 0x%x",0xc00e0048, readl((const void *)0xc00e0048));
  cmd_proc_trace(LOG_INFO, 0x7fe3, "R0x%x : 0x%x",0xc00e004c, readl((const void *)0xc00e004c));
  cmd_proc_trace(LOG_INFO, 0x7d32, "----dump_sram_error_reg end------");
}
#endif

fast_code bool cmd_proc_search_cmd_id(u32 nvm_cmd_id , u16 cmd_id)
{
       debug_control_t ctrl = {
               .b.debug_addr = nvm_cmd_id * 32 + 0, // get context memory 1
               .b.debug_dorce_rdy = 0,
               .b.debug_sel = 1,
               .b.debug_en = 1};
       u32 t,t_cmd_id;

       cmd_proc_writel(ctrl.all, DEBUG_CONTROL);
       t = cmd_proc_readl(TABLE_DATA);
       t_cmd_id = t >> 16;
       if(t_cmd_id == cmd_id)
       {
           cmd_proc_writel(0, DEBUG_CONTROL);
           return true;
       }
       else
       {
           //nvme_hal_trace(LOG_ERR, 0, "cmd_id %d != %d in nvm_cmd_id %d ", cmd_id, t_cmd_id, nvm_cmd_id);
       }
       cmd_proc_writel(0, DEBUG_CONTROL);
       return false;
}

ddr_code void cmd_proc_dump_nvm_cmd(u32 nvm_cmd_id, err_cmpl_queue_t *err_cmd, u32* nsid)
{
#if(PLP_SUPPORT == 1)
	   if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	   {
		   return;
	   }
#endif


       debug_control_t ctrl = {
               .b.debug_addr = nvm_cmd_id * 32 + 0, // get context memory 1
               .b.debug_dorce_rdy = 0,
               .b.debug_sel = 1,
               .b.debug_en = 1};

       nvme_hal_trace(LOG_ERR, 0x85a5, "nvme_cmd_id: %d\n", nvm_cmd_id);
       u32 i, t;
       for (i = 0; i < 16; i++)
       {
               cmd_proc_writel(ctrl.all, DEBUG_CONTROL);
               t = cmd_proc_readl(TABLE_DATA);
               ctrl.b.debug_addr++;
               nvme_hal_trace(LOG_ERR, 0x4d79, "[%d] %x\n", i, t);
				if(i==1)
				{
					*nsid = t;
				}
               if((err_cmd->dw0.b.err_lba_h == 0) && (err_cmd->err_lba_l == 0))
               {
                    if(i == 10)
                        err_cmd->err_lba_l = t;
                    else if(i == 11)
                        err_cmd->dw0.b.err_lba_h = t;
               }
       }
       cmd_proc_writel(0, DEBUG_CONTROL);
}

ddr_code UNUSED void cmd_proc_debug_prp(void)
{
	/*1.	Read error complete queue base address register 0xC003C080.
	 * [22:0] is the address, [23] is SRAM/DTCM indicator. */
#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return;
	}
#endif
	err_cmpl_queue_t *err_cmpl_cmd;
	err_cmpl_q_ptr_t err_cmpl_ptr = {
		.all = cmd_proc_readl(ERR_CMPL_Q_PTR),
	};

	u8 err_cmpl_wptr = err_cmpl_ptr.b.err_cmpl_wr_ptr;
	evlog_printk(LOG_INFO,"[PRP] err_cmpl_wptr %x err_cmpl_rptr %x\n", err_cmpl_wptr, err_cmpl_rptr);

	u32 i;
	u32 cmd_slot;

	debug_control_t dbg_ctrl;
    i = err_cmpl_rptr;
	//for (i = 0; i <= err_cmpl_rptr; i++) {
		u32 nlb;
		u32 prp_lo;
		u32 prp_hi;


		err_cmpl_cmd = &err_cmpl_base[i];

		evlog_printk(LOG_INFO,"err_cmpl_cmd [%x] Slot[%x] [dw0]%x [dw1] %x, [dw2] %x \n", i,
				err_cmpl_cmd->dw0.b.cmd_slot,
				err_cmpl_cmd->dw0,
				err_cmpl_cmd->err_lba_l,
				err_cmpl_cmd->dw2);

		cmd_slot = err_cmpl_cmd->dw0.b.cmd_slot;

		dbg_ctrl.b.debug_addr = (cmd_slot << 5) | 0xc;
		dbg_ctrl.b.debug_en = 1;
		cmd_proc_writel(dbg_ctrl.all, DEBUG_CONTROL);
		nlb = cmd_proc_readl(TABLE_DATA);
		evlog_printk(LOG_INFO," NLB %x\n", cmd_proc_readl(TABLE_DATA));

		dbg_ctrl.b.debug_addr = (cmd_slot << 5) | 0x6;
		dbg_ctrl.b.debug_en = 1;
		cmd_proc_writel(dbg_ctrl.all, DEBUG_CONTROL);

		evlog_printk(LOG_INFO," PRP1 LO %x\n",cmd_proc_readl(TABLE_DATA));

		dbg_ctrl.b.debug_addr = (cmd_slot << 5) | 0x7;
		dbg_ctrl.b.debug_en = 1;
		cmd_proc_writel(dbg_ctrl.all, DEBUG_CONTROL);

		evlog_printk(LOG_INFO," PRP1 HI %x\n",cmd_proc_readl(TABLE_DATA));

		dbg_ctrl.b.debug_addr = (cmd_slot << 5) | 0x8;
		dbg_ctrl.b.debug_en = 1;
		cmd_proc_writel(dbg_ctrl.all, DEBUG_CONTROL);
		prp_lo = cmd_proc_readl(TABLE_DATA);
		evlog_printk(LOG_INFO," PRP2 LO %x\n",cmd_proc_readl(TABLE_DATA));

		dbg_ctrl.b.debug_addr = (cmd_slot << 5) | 0x9;
		dbg_ctrl.b.debug_en = 1;
		cmd_proc_writel(dbg_ctrl.all, DEBUG_CONTROL);
		evlog_printk(LOG_INFO," PRP2 HI %x\n",cmd_proc_readl(TABLE_DATA));
		prp_hi = cmd_proc_readl(TABLE_DATA);
		cmd_proc_writel(0, DEBUG_CONTROL);

		evlog_printk(LOG_INFO," dump dma prp prp hi%x, prp lo %x, nlb %x\n", prp_hi, prp_lo, nlb);
		nvmet_dump_dma_prp(prp_hi, prp_lo, nlb);

	//}
}

fast_code u8 cmd_proc_judge_opcode(u32 nvm_cmd_id)
{
	debug_control_t ctrl;
	NvmeCommandDw0_t cmdCdb0;

	ctrl.b.debug_addr = nvm_cmd_id * 32 + 0;
	ctrl.b.debug_dorce_rdy = 0;
	ctrl.b.debug_sel = 1;
	ctrl.b.debug_en = 1;

	cmd_proc_writel(ctrl.all, DEBUG_CONTROL);
	cmdCdb0.all = cmd_proc_readl(TABLE_DATA);

	cmd_proc_writel(0, DEBUG_CONTROL);

	return cmdCdb0.b.OPC;
}
/*!
 * @brief  cmd_proc NVMe SQ error complete commands
 *
 * @param status	nvme status point
 * @param cmd	error complete command
 *
 * @return	None
 */
ddr_code void cmd_proc_err_cmpl_paser(struct nvme_status *status, err_cmpl_queue_t *cmd)
{
	struct insert_nvme_error_information_entry err_info;
	u8 chk_flag = 0xFF;
	err_info.error_location = 0;
	u32 NSID = 0;
	u64 media_err_tmp = smart_stat->media_err[0];
	if (cmd->dw0.b.cq_full)
	{
		status->sct = NVME_SCT_GENERIC;
		status->sc = NVME_SC_SUCCESS;
	}
	if (cmd->dw0.b.cmd_err)
	{
#ifdef DUMP_SRAM_REG
        dump_sram_error_reg();
#endif
		if (cmd->dw0.b.cmd_abort)
		{
			status->sct = NVME_SCT_MEDIA_ERROR;
			status->sc = NVME_SC_UNRECOVERED_READ_ERROR;
			smart_stat->media_err[0]++;
		}
		else if (cmd->dw0.b.prp_offset_inv)
		{
			status->sct = NVME_SCT_GENERIC;
			status->sc = NVME_SC_INVALID_PRP_OFFSET;
			chk_flag = 0xbb;
			err_info.error_location = 31;
			ctrl_fatal_state = true;
			cmd_proc_debug_prp();
		}
		else if (cmd->dw0.b.sgl_desc_type_inv)
		{
			status->sct = NVME_SCT_GENERIC;
			status->sc = NVME_SC_INVALID_SGL_SEG_DESCRIPTOR;
			err_info.error_location = 39;
		}
		else if (cmd->dw0.b.sgl_sub_type_inv)
		{
			status->sct = NVME_SCT_GENERIC;
			status->sc = NVME_SC_INVALID_SGL_SEG_DESCRIPTOR;
			err_info.error_location = 39;
		}
		else if (cmd->dw0.b.data_sgl_len_inv)
		{
			status->sct = NVME_SCT_GENERIC;
			status->sc = NVME_SC_DATA_SGL_LENGTH_INVALID;
			err_info.error_location = 32;
		}
		else if (cmd->dw0.b.pcie_read_timeout)
		{
            status->sct = NVME_SCT_GENERIC;
            status->sc = NVME_SC_TRANSIENT_TRANSPORT_ERROR;

		}
		else if (cmd->dw0.b.sgl_num_inv)
		{
			status->sct = NVME_SCT_GENERIC;
			status->sc = NVME_SC_INVALID_NUM_SGL_DESCIRPTORS;
			err_info.error_location = 24;
		}
		else if (cmd->dw0.b.sgl_seg_inv)
		{
			status->sct = NVME_SCT_GENERIC;
			status->sc = NVME_SC_INVALID_SGL_SEG_DESCRIPTOR;
			err_info.error_location = 24;
		}
		else if (cmd->dw0.b.lbrt_err)
		{
			status->sct = NVME_SCT_MEDIA_ERROR;
			status->sc = NVME_SC_REFERENCE_TAG_CHECK_ERROR;
			//shr_E2E_RefTag_detection_count++;	// DBG, SMARTVry
			tx_smart_stat->end_to_end_detection_count[2]++;
			smart_stat->media_err[0]++;
			err_info.error_location = 16;
		}
		else if (cmd->dw0.b.lbat_err)
		{
			status->sct = NVME_SCT_MEDIA_ERROR;
			status->sc = NVME_SC_APPLICATION_TAG_CHECK_ERROR;
			//shr_E2E_AppTag_detection_count++;	// DBG, SMARTVry
			tx_smart_stat->end_to_end_detection_count[1]++;
			smart_stat->media_err[0]++;
		}
		else if (cmd->dw0.b.guard_err)
		{
			status->sct = NVME_SCT_MEDIA_ERROR;
			status->sc = NVME_SC_GUARD_CHECK_ERROR;
			//shr_E2E_GuardTag_detection_count++;	// DBG, SMARTVry
			tx_smart_stat->end_to_end_detection_count[0]++;
			smart_stat->media_err[0]++;
		}
		else if (cmd->dw0.b.btn_rd_err)
		{
			if (cmd_proc_judge_opcode(cmd->dw0.b.cmd_slot) == NVME_OPC_READ)
			{
			#if TCG_WRITE_DATA_ENTRY_ABORT
				extern bool tcg_rdcmd_list_search_del(u32 val);
				if(tcg_rdcmd_list_search_del(cmd->dw0.b.cmd_slot))
				{
					status->sct = NVME_SCT_MEDIA_ERROR;
					status->sc  = NVME_SC_ACCESS_DENIED;
					status->dnr = 1;
				}
				else
			#endif
				{
					status->sct = NVME_SCT_MEDIA_ERROR;
					status->sc = NVME_SC_UNRECOVERED_READ_ERROR;
					smart_stat->media_err[0]++;
				}
			}
			else
			{
				//soc fetch write cmd prp and cpl timeout occur case
				status->sct = NVME_SCT_GENERIC;
				status->sc = NVME_SC_TRANSIENT_TRANSPORT_ERROR;
			}
		}
		else if (cmd->dw0.b.hs_crc_err)
		{
#if 0//TCG_WRITE_DATA_ENTRY_ABORT
	#ifdef TCG_NAND_BACKUP
			extern volatile u16 mbr_rd_cid;
			if(mbr_rd_cid == cmd->dw0.b.cmd_slot)
			{
				nvme_hal_trace(LOG_INFO, 0x9d8a, "[TCG] HCRC err when MBR read, bypass cid: %d", cmd->dw0.b.cmd_slot);
				status->sct = NVME_SCT_GENERIC;
				status->sc = NVME_SC_SUCCESS;
				mbr_rd_cid = 0xFFFF;
			}
			else
	#endif
#endif
			{
				status->sct = NVME_SCT_GENERIC;
				status->sc = NVME_SC_DATA_TRANSFER_ERROR;
				tx_smart_stat->hcrc_error_count[0]++;
				smart_stat->media_err[0]++;
			}
		}
		else if (cmd->dw0.b.par_err)
		{
			status->sct = NVME_SCT_GENERIC;
			status->sc = NVME_SC_DATA_TRANSFER_ERROR;
			smart_stat->media_err[0]++;
		}


	}

	if ((status->sct != NVME_SCT_GENERIC) || (status->sc != NVME_SC_SUCCESS))
	{
		if(cmd->dw0.b.prp_offset_inv || ((cmd->dw0.b.err_lba_h == 0) && (cmd->err_lba_l == 0)))
		{
			for(u16 i = 0 ; i < MAX_CMD_SLOT_CNT ; i++)
			{
                if(plp_trigger)
                {
                    break;
                }
				if(cmd_proc_search_cmd_id(i, cmd->dw2.b.cid) == true)
				{
    				cmd_proc_dump_nvm_cmd(i, cmd,&NSID);
    				cmd_proc_trace(LOG_ALW, 0xb7e4, "Check flag %x", chk_flag);
    				chk_flag = 0;
				}
			}
		}
		cmd_proc_trace(LOG_ERR, 0xa8f1, "cid %d, err %x, lba %x%x",
					   cmd->dw0.b.cmd_slot, cmd->dw0.all & ERR_CMPL_ERR_MASK,
					   cmd->dw0.b.err_lba_h, cmd->err_lba_l);
		cmd_proc_trace(LOG_ERR, 0x327f, "ret err CQ host cid %x, sct %d, sc %d", cmd->dw2.b.cid,
					   status->sct, status->sc);
        if(cmd->dw0.b.par_err)
        {
            //flush_to_nand(EVT_SRAM_PARITY_ERR); //PRP ERROR SAVE LOG ERR 0x80002002
        }
        if(cmd->dw0.b.pcie_read_timeout){
            //flush_to_nand(EVT_AXI_PARITY_ERR);
        }
		err_info.status = *status;
		err_info.sqid = cmd->dw2.b.sq_id;
		err_info.cid = cmd->dw2.b.cid;
		err_info.lba = cmd->dw0.b.err_lba_h;
		err_info.lba = err_info.lba << 32;
		err_info.lba |= cmd->err_lba_l;
		err_info.nsid = NSID;
		if(media_err_tmp != smart_stat->media_err[0])
		{
			err_info.status.m=1; //will save err log page, set More bit to 1;
			nvmet_err_insert_event_log(err_info);
		}

	}
	else
	{
		cmd_proc_trace(LOG_ALW, 0x2f14, "err dw0 %x", cmd->dw0.all);
	}

}

/*!
 * @brief  cmd_proc NVMe SQ error complete commands handle
 *
 * @param 	not used
 *
 * @return	not used
 */
 extern dtag_t dma_dtag_dbg;
ddr_code bool cmd_proc_err_cmpl_cmd(void)
{
	err_cmpl_queue_t *err_cmpl_cmd;
	err_cmpl_q_ptr_t err_cmpl_ptr = {
		.all = cmd_proc_readl(ERR_CMPL_Q_PTR),
	};

	u8 err_cmpl_wptr = err_cmpl_ptr.b.err_cmpl_wr_ptr;

	while (err_cmpl_rptr != err_cmpl_wptr)
	{
		fe_t fe;
		nvme_status_alias_t _status;

		err_cmpl_cmd = &err_cmpl_base[err_cmpl_rptr];
		_status.all = 0;
		if(plp_trigger)
		{
			return false;
		}

		if(dma_dtag_dbg.dtag != INV_LDA && err_cmpl_cmd->dw0.b.prp_offset_inv)
		{
			ctrl_fatal_state = true;
			return false;
		}

#if 0//TCG_WRITE_DATA_ENTRY_ABORT
		bool skip_tcg_btn_err = false;
		extern bool tcg_rdcmd_list_search_del(u32 val);
		skip_tcg_btn_err = tcg_rdcmd_list_search_del(err_cmpl_cmd->dw0.b.cmd_slot);
		if(!skip_tcg_btn_err)
#endif
			cmd_proc_err_cmpl_paser(&_status.status, err_cmpl_cmd);

		// update status, and wait for BTN release
		fe.nvme.cid = err_cmpl_cmd->dw2.b.cid;
		fe.nvme.sqid = err_cmpl_cmd->dw2.b.sq_id;
		fe.nvme.nvme_status = _status.all;
		fe.nvme.cntlid = 0;
		fe.nvme.cmd_spec = 0;

        //cmd_proc_trace(LOG_ERR, 0, "[DBG] proc err_cmpl_cmd struct[0x%x]", err_cmpl_cmd->dw0.all);

        //if (!err_cmpl_cmd->dw0.b.btn_rd_err)  //IG patch
#if 0//TCG_WRITE_DATA_ENTRY_ABORT
		if(skip_tcg_btn_err)
		{
			skip_tcg_btn_err = false;
			cmd_proc_trace(LOG_ALW, 0xcfb4, "[TCG] unexcepted BTN rd err occurs");
		}
		else
#endif
		    nvmet_core_handle_cq(&fe);

		//extern vu32 Rdcmd;
		//Rdcmd--;
		err_cmpl_rptr = (err_cmpl_rptr + 1) & NUM_ERR_CMPL_CMD_0_BASED;
		err_cmpl_ptr.b.err_cmpl_rd_ptr = err_cmpl_rptr;
		cmd_proc_writel(err_cmpl_ptr.all, ERR_CMPL_Q_PTR);
	}
	return true;
}

/*!
 * @brief  cmd_proc rx NVMe I/O SQ commands processor handler
 *
 * @param None
 *
 * @return	None
 */
fast_code void cmd_proc_rx_cmd(void)
{
	struct nvme_cmd *cmd;
	cmd_rx_queue_t *rx_cmd;
	cmd_rx_q_ptr_t cmd_proc_rx_pnter = {
		.all = cmd_proc_readl(CMD_RX_Q_PTR),
	};
	u8 rx_wptr = cmd_proc_rx_pnter.b.cmd_rx_wr_ptr;

	if(cur_ro_status == RO_MD_WAIT_RX_FINISH)
	{
		cmd_proc_trace(LOG_INFO, 0x3692, "[RO]hw rptr|%x ,wptr|%x",rx_q_rptr,rx_wptr);
	}

	while ((ctrlr->free_nvme_cmds > 0) &&
		   (rx_q_rptr != rx_wptr) && (req_cnt > 0) && (plp_trigger == 0))
	{
		rx_cmd = &rx_q_base[rx_q_rptr];
		cmd = (struct nvme_cmd *)nvme_cmd_cast(rx_cmd_base[rx_q_rptr]);
		rx_cmd_base[rx_q_rptr] = nvme_cmd_get(__FUNCTION__);
		sys_assert(rx_cmd_base[rx_q_rptr]);

		if (rx_cmd->b.lr_err || rx_cmd->b.nsid_inv || rx_cmd->b.mdts_err || rx_cmd->b.fatal_err || cmd->fuse)
		{
			if (((rx_cmd->b.nsid_inv == 1) && (cmd->opc == NVME_OPC_FLUSH)) || (cmd->opc == NVME_OPC_WRITE_UNCORRECTABLE)) //Andy write zeros || (cmd->opc == NVME_OPC_WRITE_ZEROES)
			{
				goto Flush_invalid; //for flush with invalid Nsid FFFFFFFF
			}
			cmd_proc_rw_err_handle(rx_cmd, cmd);
		}
		else if (is_sqs_delete(rx_cmd->b.sq_id)) //[PCBasher] nSQ+nCQ_01
		{
			cmd_proc_trace(LOG_ALW, 0xf363,"cmd proc rx cmd cid %d sq id:%d opcode:0x%x After SQ Delete",
					cmd->cid, rx_cmd->b.sq_id, cmd->opc);
			hal_nvmet_put_sq_cmd(cmd);
		}
		else
		{
		Flush_invalid:
			cmd_proc_trace(LOG_DEBUG, 0xc5e4, "rx cmd cid %d sq id:%d opcode:0x%x",
						   cmd->cid, rx_cmd->b.sq_id, cmd->opc);
			extern void nvmet_rx_handle_cmd(u32 sqid, struct nvme_cmd * cmd, u32 nvm_cmd_id, u8 func_id);
			nvmet_rx_handle_cmd(rx_cmd->b.sq_id, cmd, cmd->cid, rx_cmd->b.function_id);
		}
		rx_q_rptr = (rx_q_rptr + 1) & NUM_CMDP_RX_CMD_0_BASED;
		cmd_proc_rx_pnter.b.cmd_rx_rd_ptr = rx_q_rptr;
		cmd_proc_writel(cmd_proc_rx_pnter.all, CMD_RX_Q_PTR);


		if(cur_ro_status == RO_MD_WAIT_RX_FINISH)
		{
			cmd_proc_trace(LOG_INFO, 0x2c15, "[RO]status RO_MD_WAIT_RX_FINISH r:%x w:%x",rx_q_rptr,leave_ro_rx_wptr);
		}

	}
		if(cur_ro_status == RO_MD_WAIT_RX_FINISH)
		{
			cmd_proc_rx_pnter.all = cmd_proc_readl(CMD_RX_Q_PTR);
			leave_ro_rx_wptr = cmd_proc_rx_pnter.b.cmd_rx_wr_ptr;
			if(leave_ro_rx_wptr != rx_wptr)
			{
				cmd_proc_trace(LOG_INFO, 0x4e65, "[RO]RX POOL not empty rptr|%x ,wptr|%x",rx_q_rptr,leave_ro_rx_wptr);
				cur_ro_status = RO_MD_WAIT_RX_FINISH;
			}
			else
			{
				cmd_proc_trace(LOG_INFO, 0x6144, "[RO]RX POOL real empty");
				cur_ro_status = RO_MD_OUT;
			}
		}

}

fast_code void cmd_proc_disable_function_ram_init(void)
{
	slot_misc_ctrl_t ctrl = {.all = cmd_proc_readl(SLOT_MISC_CTRL)};

	ctrl.b.func_dis_init_st = 1;
	//ctrl.b.rd_abort_mode = 1;

	cmd_proc_writel(ctrl.all, SLOT_MISC_CTRL);

	do
	{
		ctrl.all = cmd_proc_readl(SLOT_MISC_CTRL);
	} while (ctrl.b.func_dis_init_st == 1);
}

fast_code static void _cmd_proc_disable_function(u8 fid)
{
	dis_func_ctrl_t del = {.all = cmd_proc_readl(DIS_FUNC_CTRL)};

	del.b.operation = 0;
	del.b.func_id = fid;

	cmd_proc_writel(del.all, DIS_FUNC_CTRL);
}

/*!
 * @brief delete sq for specific function id
 *
 * @param fid	function id
 *
 * @return	not used
 */
static void _cmd_proc_del_sqs(u8 fid);

fast_code bool cmd_proc_del_sq_done(req_t *req)
{
	u8 fid = req->fe.nvme.cntlid;
	u32 sqid = req->op_fields.admin.sq_id - 1;

	clear_bit(sqid, _dis_func_ctx[fid].del_sq_bmp);
	nvmet_put_req(req);

	sys_assert(_dis_func_ctx[fid].deleting);
	if(!plp_trigger)
		cmd_proc_trace(LOG_INFO, 0x6432, "delete sq done call back");
	_dis_func_ctx[fid].deleting--;
	if (_dis_func_ctx[fid].deleting == 0)
	{
		if (_dis_func_ctx[fid].del_sq_bmp[0] || _dis_func_ctx[fid].del_sq_bmp[1] || _dis_func_ctx[fid].del_sq_bmp[2])
		{
			cmd_proc_trace(LOG_INFO, 0x5c57, "bmp[0]:%d , bmp[1]:%d, bmp[2]:%d",_dis_func_ctx[fid].del_sq_bmp[0], _dis_func_ctx[fid].del_sq_bmp[1], _dis_func_ctx[fid].del_sq_bmp[2]);
			_cmd_proc_del_sqs(fid);
		}
		else
			_cmd_proc_disable_function(fid);
	}

	return 0;
}

fast_code void _cmd_proc_del_sqs(u8 fid)
{
	u32 bmp[3];
	u32 i;

	sys_assert(_dis_func_ctx[fid].deleting == 0);

	bmp[0] = _dis_func_ctx[fid].del_sq_bmp[0];
	bmp[1] = _dis_func_ctx[fid].del_sq_bmp[1];
	bmp[2] = _dis_func_ctx[fid].del_sq_bmp[2];

	for (i = 0; i < 3; i++)
	{
		u32 sqid;

		while (bmp[i] != 0)
		{
			req_t *req = nvmet_get_req();
			if (req == NULL)
			{
				sys_assert(_dis_func_ctx[fid].deleting != 0);
				return;
			}

			sqid = ctz(bmp[i]);
			bmp[i] &= ~(1 << sqid);
			sqid += i * 32 + 1; // we don't delete SQ 0

			req->req_from = REQ_Q_OTHER;
			req->completion = cmd_proc_del_sq_done;
			req->fe.nvme.cntlid = fid;
			cmd_proc_delete_sq(sqid, req);
			_dis_func_ctx[fid].deleting++;
		}
	}
}

fast_code void cmd_proc_disable_function(u8 fid, u32 del_sq_bmp[3], void *ctx, cmd_proc_dis_func_cb_t cmpl)
{

	sys_assert(fid < MAX_FUNC_NUM);
	if (_dis_func_ctx[fid].cmpl != NULL)
	{
		_dis_func_ctx[fid].cmpl(_dis_func_ctx[fid].ctx, fid);
		_dis_func_ctx[fid].cmpl = cmpl;
		_dis_func_ctx[fid].ctx = ctx;
		cmd_proc_trace(LOG_WARNING, 0xbae5, "disable function (%d) doing", fid);
		return;
	}

	_dis_func_ctx[fid].del_sq_bmp[0] = del_sq_bmp[0];
	_dis_func_ctx[fid].del_sq_bmp[1] = del_sq_bmp[1];
	_dis_func_ctx[fid].del_sq_bmp[2] = del_sq_bmp[2];
	_dis_func_ctx[fid].cmpl = cmpl;
	_dis_func_ctx[fid].ctx = ctx;
	_dis_func_ctx[fid].deleting = 0;

	if (_dis_func_ctx[fid].del_sq_bmp[0] == 0 &&
		_dis_func_ctx[fid].del_sq_bmp[1] == 0 &&
		_dis_func_ctx[fid].del_sq_bmp[2] == 0)
	{
		_cmd_proc_disable_function(fid);
		return;
	}

	_cmd_proc_del_sqs(fid);
}

/*!
 * @brief This function disable function done handle
 *
 * @param	None
 *
 * @return	not used
 */
fast_code void cmd_proc_disable_function_done(void)
{
	dis_func_done_0_t dis_done_0;
	dis_func_done_1_t dis_done_1;
	dis_func_ctrl_t del;
	u32 i;
	u64 overall_status = 0;

	dis_done_0.all = cmd_proc_readl(DIS_FUNC_DONE_0);
	dis_done_1.all = cmd_proc_readl(DIS_FUNC_DONE_1);

	overall_status = dis_done_1.all;
	overall_status <<= 32;
	overall_status |= dis_done_0.all;

	for (i = 0; i < 64; i++)
	{
		if (overall_status & (1ULL << i))
		{
			cmd_proc_dis_func_cb_t cmpl;
			void *ctx;

			cmd_proc_trace(LOG_ALW, 0xedfd, "disable function (%d) done", i);

			// clear disable function operation
			del.all = cmd_proc_readl(DIS_FUNC_CTRL);
			del.b.func_id = i;
			del.b.operation = 1;
			cmd_proc_writel(del.all, DIS_FUNC_CTRL);

			// clear interrupt
			cmd_proc_writel(BIT(i & 0x1F), DIS_FUNC_DONE_0 + ((i >> 5) * sizeof(del_sq_dw0_t)));
			cmpl = _dis_func_ctx[i].cmpl;
			ctx = _dis_func_ctx[i].ctx;

			_dis_func_ctx[i].ctx =
				_dis_func_ctx[i].cmpl = 0;

			if (cmpl)
				cmpl(ctx, i);
		}
	}
}

/*!
 * @brief cmd_proc delete sq
 *
 * @param flex_sqid		sq id to be deleted
 * @param req			admin request to delete SQ
 *
 * @return	None
 */
fast_code void cmd_proc_delete_sq(u16 sqid, req_t *req)
{
	/* The sqid must be flexible SQ ID. Use proper sqid to delete a specific
	 * function SQID. Flexible SQ ID varies from 0 to 65. */
	del_sq_ctrl_t del = {.all = cmd_proc_readl(DEL_SQ_CTRL)};

	del.b.operation_c09c = 0;
	del.b.sq_id = sqid;

	cmd_proc_writel(del.all, DEL_SQ_CTRL);

	if (req)
	{
		req->op_fields.admin.sq_id = sqid;
		list_add_tail(&req->entry2, &_del_sq_reqs);
		if(!plp_trigger)
			cmd_proc_trace(LOG_INFO, 0x80e7, "delete SQ %d", sqid);
		//Andy test IOL 5.4 20201013
		if (flagtestS != 0)
			flagtestS--;
	}
}
/*!
 * @brief	clear delete SQ
 *
 * @param	flex_sqid: sq id
 *
 * @return	None
 */
fast_code void cmd_proc_clear_delete_sq(u16 sqid)
{
	/* Note: In SR-IOV for this function to work properly, the caller must pass
	 * sqid as flexible SQ ID. The range of values are 0 to 65. This is
	 * applicable to any functions (PF/VFn) which use flexible resources. */
	del_sq_ctrl_t del = {.all = cmd_proc_readl(DEL_SQ_CTRL)};

	del.b.operation_c09c = 1;
	del.b.sq_id = sqid;

	cmd_proc_writel(del.all, DEL_SQ_CTRL);
}

ddr_code void hal_nvmet_dma_read_engine_states_chk(u16 sqid)
{
    u32 dma_read_engine_states = readl((void *) 0xc00321c4);

    if ((dma_read_engine_states & BIT(25)) && (dma_read_engine_states & BIT(26)))
    {
        cmd_proc_trace(LOG_WARNING, 0x8345, "SQ[%d] buffer empty",sqid);
    }
    else
    {
        cmd_proc_trace(LOG_WARNING, 0xce40, "SQ[%d] DMA state:0x%x",sqid,dma_read_engine_states);
        flush_to_nand(EVT_DEL_SQ_ABNORMAL);
    }
}

/*!
 * @brief  is delete SQ done
 *
 * @param fid           function id sq to be deleted
 * @param sqid		sq to be deleted
 *
 * @return		true: delete SQ done false: not done
 */
fast_code bool cmd_proc_is_delete_sq_done(u8 fid, u16 sqid)
{
	del_sq_done_dw0_t status;

	status.all = cmd_proc_readl(DEL_SQ_DONE_DW0 + ((sqid >> 5) * sizeof(del_sq_done_dw0_t)));

	if ((status.b.del_sq_done_dw0 & BIT(sqid & 0x1F)) != 0) {
		cmd_proc_clear_delete_sq(sqid);
		cmd_proc_writel(BIT(sqid & 0x1F), DEL_SQ_DONE_DW0 + ((sqid >> 5) * sizeof(del_sq_done_dw0_t)));
		u16 head;
		u16 tail;

		hal_nvmet_get_sq_pnter(sqid, &head, &tail);
		if (head != tail) {
			extern bool hal_nvmet_chk_cq_size(u16 sqid);
			cmd_proc_trace(LOG_WARNING, 0x9ca6, "h/t %d/%d, sq %d cq size %d count %d delete again",
				head, tail, sqid, hal_nvmet_chk_cq_size(sqid), _dis_func_ctx[fid].retry_cnt);
			if (_dis_func_ctx[fid].retry_cnt < MAX_RETRY_DELETE_SQ_COUNT) {
				// cq size non zero.
				cmd_proc_delete_sq(sqid, NULL);
				_dis_func_ctx[fid].retry_cnt++;
				return false;
			} else {
				hal_nvmet_dma_read_engine_states_chk(sqid);
				_dis_func_ctx[fid].retry_cnt = 0;
				return true;
			}
		}
		_dis_func_ctx[fid].retry_cnt = 0;
		return true;
	}
	return false;
}

/*!
 * @brief delete SQ done handle
 *
 * @return		not used
 */
fast_code void cmd_proc_delete_sq_done(void)
{
	u16 sqid;
	struct list_head *next;
	struct list_head *next2;
	u8 fnid;

	list_for_each_safe(next, next2, &_del_sq_reqs)
	{
		req_t *req = container_of(next, req_t, entry2);

		sqid = req->op_fields.admin.sq_id;
		fnid = req->fe.nvme.cntlid;

		if (ctrlr->sqs[sqid] == NULL) { //3.1.7.4 merged 20201201 Eddie
			cmd_proc_trace(LOG_ALW, 0x790b, "duplicated SQ(%d)", sqid);
			list_del(next);
			evt_set_imt(evt_cmd_done, (u32)req, 0);
			continue;
		}

		if (cmd_proc_is_delete_sq_done(fnid, sqid))
		{
			if(!plp_trigger)
				cmd_proc_trace(LOG_INFO, 0xe9a5, "Delete I/O SQ(%d), cntl ID(%d) done req(%x)", sqid, fnid, req);
			/* unbind CQ and clean Queue */
#if defined(SRIOV_SUPPORT)
			if (fnid == 0)
			{
				/* PF0 SQID, CQID mapping are one to one */
				nvmet_unbind_vfcq(fnid, sqid, ctrlr->sqs[sqid]->cqid);
				nvmet_unset_vfsq(fnid, sqid);
			}
			else
			{
				u8 hst_sqid = sqid - SRIOV_FLEX_PF_ADM_IO_Q_TOTAL - (fnid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
				u8 hst_cqid = ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrsq[hst_sqid]->cqid;
				nvmet_unbind_vfcq(fnid, hst_sqid, hst_cqid);
				nvmet_unset_vfsq(fnid, hst_sqid);
			}
#else
			nvmet_unbind_cq(sqid, ctrlr->sqs[sqid]->cqid);
			nvmet_unset_sq(sqid);
#endif

#if defined(SRIOV_SUPPORT) || !defined(NVME_SHASTA_MODE_ENABLE)
			/* sqid is flexible SQID. Range is from 0 to 65 */
			hal_nvmet_delete_vq_sq(sqid);
#else
			hal_nvmet_delete_sq(sqid);
#endif

			list_del(next);
			evt_set_imt(evt_cmd_done, (u32)req, 0);
		}
	}
}

/*!
 * @brief  This function performs the HW recommended procedure to abort all
 *	commands. Ref: CMD_PROC_Rainier_v1.2 section 3.4.5
 *
 * @param	None
 *
 * @return	None
 */
fast_code void cmd_proc_abort_all_commands(void)
{
	extern __attribute__((weak)) bool ncl_cmd_empty(bool rw);
	extern __attribute__((weak)) void ncl_cmd_wait_completion();
	abort_xfer_t abort_xfer;
	port_reset_ctrl_t reset_ctrl;
	abort_ctrl_t abort_ctrl;

	/* #1. Abort nvm sub system to stop NVMe command fetch, command
	 * completion and data transfer for Admin commands from NVMe sub system. */
	misc_nvm_ctrl_reset(true);

	/* #2. Abort NVMe read & write DMA. This will stop IO transfer */
	abort_xfer.all = cmd_proc_readl(ABORT_XFER);
	abort_xfer.b.abort_write_dma = 1;
	abort_xfer.b.abort_read_dma = 1;
	cmd_proc_writel(abort_xfer.all, ABORT_XFER);

	/* #3. Abort CMD_PROC */
	abort_ctrl.all = cmd_proc_readl(ABORT_CTRL);
	abort_ctrl.b.cmd_proc_abort = 1;
	cmd_proc_writel(abort_ctrl.all, ABORT_CTRL);

	/* #4. Wait 10usec */
	udelay(10);

	/* #5. Clean up NCB and wait for it become idle */
	ncl_cmd_wait_completion();

	/* #6. reset NVMe DATA_XFER */
	reset_ctrl.all = cmd_proc_readl(PORT_RESET_CTRL);
	reset_ctrl.b.reset_data_xfer = 1;
	cmd_proc_writel(reset_ctrl.all, PORT_RESET_CTRL);

	/* #7. Release NVMe DATA_XFER */
	reset_ctrl.all = cmd_proc_readl(PORT_RESET_CTRL);
	reset_ctrl.b.reset_data_xfer = 0;
	cmd_proc_writel(reset_ctrl.all, PORT_RESET_CTRL);

	/* #8: Clean up and reset BTN. Wait for it to become idle */
	//log_level_t old = log_level_chg(LOG_DEBUG);
	// btn_reinit(); >>> replace it with the following code
	btn_reset_pending();
	misc_reset(RESET_BM);
	btn_reset_resume();
	//log_level_chg(old);
	do
	{
		abort_ctrl.all = cmd_proc_readl(ABORT_CTRL);
	} while (abort_ctrl.b.cmd_proc_idle == 0);

	/* #9: Wait for DATA_XFER to become idle */
	do
	{
		abort_xfer.all = cmd_proc_readl(ABORT_XFER);
	} while (abort_xfer.b.data_xfer_idle == 0);

	/* #10. Release NVMe read & write DMA */
	abort_xfer.all = cmd_proc_readl(ABORT_XFER);
	abort_xfer.b.abort_write_dma = 0;
	abort_xfer.b.abort_read_dma = 0;
	cmd_proc_writel(abort_xfer.all, ABORT_XFER);

	/* #11. Release NVMe Sub System Abort */
	misc_nvm_ctrl_reset(false);

	/* #12. Release CMD_PROC abort */
	abort_ctrl.all = cmd_proc_readl(ABORT_CTRL);
	abort_ctrl.b.cmd_proc_abort = 0;
	cmd_proc_writel(abort_ctrl.all, ABORT_CTRL);

	/* #13. Reset CMD_PROC */
	abort_ctrl.all = cmd_proc_readl(ABORT_CTRL);
	abort_ctrl.b.reset_cmd_proc = 1;
	cmd_proc_writel(abort_ctrl.all, ABORT_CTRL);

	/* #14. Initialize CMD_PROC RAM */
	cmd_proc_disable_function_ram_init();
}

/**
 * @brief command complete processor ISR handler
 *
 * Handle cmd_completed interrupt here.
 *
 * @return	always return 0
 */
fast_code void cmd_proc_isr(void)
{
	bool all_finish;
	/* TODO: copy the entries to evt handler and recyle the room */
	unmask_int_stts_t int_stts = {
		.all = cmd_proc_readl(INT_MASK + 4),
	};
	u32 stts = int_stts.all;

	if (int_stts.b.cmd_completed)
	{
		panic("no in shasta mode");
		int_stts.b.cmd_completed = 0;
	}

	if (int_stts.b.err_aborted)
	{
		all_finish = cmd_proc_err_cmpl_cmd();
		if(all_finish || plp_trigger)
		{
			int_stts.b.err_aborted = 0;
		}
	}

	if (int_stts.b.cmd_rcvd)
	{
		cmd_proc_rx_cmd();
		int_stts.b.cmd_rcvd = 0;
	}
	else if(sntz_chk_cmd_proc)
		sntz_chk_cmd_proc = 0;

	if (int_stts.b.sq_deleted)
	{
		cmd_proc_delete_sq_done();
		int_stts.b.sq_deleted = 0;
	}

	if (int_stts.b.func_dis_done)
	{
		cmd_proc_disable_function_done();
		int_stts.b.func_dis_done = 0;
	}

	if (int_stts.b.cmd_abort_done)
	{
		cmd_proc_cmd_abort_cmpl();
		int_stts.b.cmd_abort_done = 0;
	}

	if (int_stts.b.du_cmd_inv)
	{
		debug_slot_t ds = {.all = cmd_proc_readl(DEBUG_SLOT)};

		// if fw push wrong RDE to BTN, this error will be popped
		// du_cmd_inv_slot is the error cmd_proc_cid
		cmd_proc_trace(LOG_ERR, 0xac2c, "du cmd inv slot %d, cmd inv slot %d",
					   ds.b.du_cmd_inv_slot, ds.b.cmd_inv_slot);

		int_stts.b.du_cmd_inv = 0;
	}

	if (int_stts.b.cmd_inv_err)
	{
		debug_slot_t ds = { .all = cmd_proc_readl(DEBUG_SLOT) };

		cmd_proc_trace(LOG_DEBUG, 0x0a45, "cmd inv slot %d", ds.b.cmd_inv_slot);
		int_stts.b.cmd_inv_err = 0;
		//cmd_proc_writel(CMD_INV_SLOT_FLAG_MASK, DEBUG_SLOT);
	}

	if (int_stts.all)
		cmd_proc_trace(LOG_ERR, 0xaa59, "unhandled %x", int_stts.all);

	cmd_proc_writel(stts, UNMASK_INT_STTS);
}

fast_code UNUSED u32 pat_gen_req(u64 slba, u32 len, u32 nsid, u32 user_idx)
{
	u32 slot;

	if (len > 8)
		return ~0;

	slot = find_first_zero_bit(&fw_cmd_act_bitmap, NUM_FW_NVM_CMD);
	if (NUM_FW_NVM_CMD == slot)
		return ~0;

	fw_cmd_act_bitmap |= ((u32)1 << slot);

	// issue FW NVM command
	struct nvme_cmd *pcmd = &fw_nvm_cmd[slot];
	pcmd->nsid = nsid;
	pcmd->cdw10 = slba & 0xFFFFFFFF;
	pcmd->cdw11 = slba >> 32;
	pcmd->cdw12 = len - 1;

	// note, no need to check issuing free since active fw cmd <= queue_depth
	cmd_q_ptr_t ptr = {
		.all = cmd_proc_readl(CMD_Q_PTR),
	};
	u32 isu_slot = ptr.b.cmd_q_wr_ptr;

	u32 sq_id = MAX_FW_SQ_ID - user_idx;

	isu_cmd_base[isu_slot].nvm_cmd_id = slot;
	isu_cmd_base[isu_slot].cmd_type = 1;
	isu_cmd_base[isu_slot].sq_id = sq_id;
	isu_cmd_base[isu_slot].cmd_buf_addr = btcm_to_dma((void *)pcmd);
	isu_cmd_base[isu_slot].cmd_buf_loc = 1;

	isu_slot = (isu_slot + 1) % NUM_FW_NVM_CMD;
	sys_assert(isu_slot != ptr.b.cmd_q_rd_ptr);

	cmd_proc_writel(isu_slot, CMD_Q_PTR);

	return slot;
}

/*!
 * @brief release data pattern generate req
 *
 * @param cmd_slot	NVM slot returned by pat_gen_req
 *
 * return 		not used
 */
fast_code void pat_gen_rel(u32 cmd_slot)
{
	sys_assert(cmd_slot < NUM_FW_NVM_CMD);
	sys_assert(((u32)1 << cmd_slot) & fw_cmd_act_bitmap);

	cmd_proc_writel(1 << cmd_slot, CMD_SLOT_VALID_0);
	fw_cmd_act_bitmap &= (~((u32)1 << cmd_slot));
}

fast_code void cmd_proc_resume(int abort)
{
	host_ctrl_t hct = {.all = cmd_proc_readl(HOST_CTRL)};
	max_lba_h_t mlt = {.all = cmd_proc_readl(MAX_LBA_H)};
	mode_ctrl_t mct = {.all = cmd_proc_readl(MODE_CTRL)};
	max_data_sz_t mdsz = {.all = cmd_proc_readl(MAX_DATA_SZ)};
	int i;
#if PI_SUPPORT
    bool pi_mode = (mlt.b.pi_ctrl == 0) ? mFALSE : mTRUE;
#endif

#if defined(OC_SSD)
	ppa_buf_init();
	/* prepare PPA queue buffers */
	for (i = 0; i < NUM_LOTS_PPA_Q; i++)
	{
		ppaq_base[i] = ppa_buf_get(__FUNCTION__);
		sys_assert(ppaq_base[i]);
	}
#endif
	reset_del_sq_resource();
	memset(_dis_func_ctx, 0, sizeof(_dis_func_ctx));

	if (abort == 0)
	{
		err_cmpl_q_base_t err_cmpl_queue_base = {
			.b.err_cmpl_q_base = btcm_to_dma(err_cmpl_base),
			.b.err_cmpl_q_loc = 1,
			.b.err_cmpl_max_sz = NUM_ERR_CMPL_CMD - 1,
		};
#if defined(OC_SSD)
		/* Setup PPA Queue */
		ppa_q_base_t ppa_queue_base = {
			.b = {
				.ppa_q_base = btcm_to_dma(ppaq_base),
				.ppa_q_loc = 1,
			}};
		ppa_q_size_t ppa_q_size = {
			.b = {
				.ppa_q_max_sz = NUM_LOTS_PPAQ_0_BASED,
			}};
		cmd_proc_writel(ppa_queue_base.all, PPA_Q_BASE);
		cmd_proc_writel(ppa_q_size.all, PPA_Q_SIZE);
		if (misc_is_warm_boot())
		{
			ppa_q_ptr_t ppaq_ptr = {
				.all = cmd_proc_readl(PPA_Q_PTR),
			};
			ppaq_rptr = ppaq_ptr.b.ppa_q_rd_ptr;
		}
		else
		{
			cmd_proc_writel(0, PPA_Q_PTR);
			/* initialize ppa queue read/write pointers */
			ppaq_rptr = 0;
		}
#endif

		cmd_proc_writel(err_cmpl_queue_base.all, ERR_CMPL_Q_BASE);

		if (misc_is_warm_boot())
		{
			err_cmpl_q_ptr_t err_cmpl_ptr = {
				.all = cmd_proc_readl(ERR_CMPL_Q_PTR),
			};
			err_cmpl_rptr = err_cmpl_ptr.b.err_cmpl_rd_ptr;
		}
		else
			err_cmpl_rptr = 0;

		poll_mask(VID_CMDPROC_P0);
		poll_mask(VID_CMDPROC_P1);

		read_unm_ctrl_t rd_unm_ctrl = {
			.b.read_unm_dtag = RVTAG_ID,
			.b.read_unm_en = 1,
		};
		cmd_proc_writel(rd_unm_ctrl.all, READ_UNM_CTRL);

		read_unm1_ctrl_t rd_unm_ctrl1 = {
			.b.read_unm1_dtag = RVTAG2_ID,
			.b.read_unm1_en = 1,
		};
		cmd_proc_writel(rd_unm_ctrl1.all, READ_UNM1_CTRL);

		read_err_dtag_t rd_err_dtag = {
			.b.read_err_dtag_en = 1,
			.b.read_err_dtag = EVTAG_ID,
		};

		cmd_proc_writel(rd_err_dtag.all, READ_ERR_DTAG);
	}

	mct.b.fw_cmd_q_depth = (MAX_FW_CMD_SLOT_SHIFT - 2);
	mct.b.lba_range_chk_en = 1;
	mct.b.max_cmd_slot = (MAX_CMD_SLOT_CNT_SHIFT - 7);
	mct.b.par_data_fill_en = 1;
#if defined(USE_CRYPTO_HW)
	//mct.b.aes_en = 1;
#endif
	mdsz.b.max_data_size = (1 << NVME_MAX_DATA_TRANSFER_SIZE);
#if defined(OC_SSD)
	mct.b.open_chnl_en = 1;
	mct.b.ppa_sz = OCSSD_PPA_SZ;
#endif
#if !defined(DISABLE_HS_CRC_SUPPORT)
	mct.b.hs_crc_report_en = 1;
#endif
#if defined(SRIOV_SUPPORT)
	mct.b.sriov_en = 1;
#endif

#if PI_SUPPORT
	mct.b.chk_lbrt = pi_mode;
	mct.b.prinfo_chk_en = 0;//pi_mode  ignor all prinfo chk
	mct.b.ilbrt_chk_en = pi_mode;
#endif

#if defined(DISABLE_HS_CRC_SUPPORT) && !defined(USE_CRYPTO_HW)
	mct.b.par_du_rd_mode = 0;
#if defined(FAST_MODE)
	mct.b.par_du_rd_mode = 1;
#endif
#else
	mct.b.par_du_rd_mode = 1;
#endif


	hct.b.du_size = (DTAG_SHF == 13) ? 1 : 0;

	cmd_proc_writel(mct.all, MODE_CTRL);
	cmd_proc_writel(mlt.all, MAX_LBA_H);
	cmd_proc_writel(hct.all, HOST_CTRL);
	cmd_proc_writel(mdsz.all, MAX_DATA_SZ);
	/* TODO: prepare RX cmd buffer */
	for (i = 0; i < NUM_CMDP_RX_CMD; i++)
	{
		rx_cmd_base[i] = nvme_cmd_get(__FUNCTION__); // bit(24~31) doesn't matter
		sys_assert(rx_cmd_base[i]);
	}

	cmd_q_base_t cmd_issue_queue_base = {
		.b.cmd_sram_base_addr = btcm_to_dma(isu_cmd_base),
		.b.cmd_queue_loc = 1,
		.b.cmd_queue_max_sz = NUM_FW_NVM_CMD - 1,
	};

	cmd_rx_q_base_t cmd_rx_queue_base = {
		.b.cmd_rx_q_base = btcm_to_dma(rx_cmd_base),
		.b.cmd_rx_q_loc = 1,
		.b.cmd_rx_q_max_sz = NUM_CMDP_RX_CMD_0_BASED,
	};

	cmd_rx_q_base1_t cmd_rx_queue_base1 = {
		.b.cmd_rx_q_base1 = btcm_to_dma(rx_q_base),
		.b.cmd_rx_q_loc1 = 1,
	};

	if (misc_is_warm_boot())
	{
		cmd_rx_q_ptr_t cmd_proc_rx_pnter = {
			.all = cmd_proc_readl(CMD_RX_Q_PTR),
		};
		rx_q_rptr = cmd_proc_rx_pnter.b.cmd_rx_rd_ptr;
	}
	else
	{
		rx_q_rptr = 0;
		cmd_proc_writel(0, CMD_RX_Q_PTR);
	}
	cmd_proc_writel(cmd_rx_queue_base.all, CMD_RX_Q_BASE);
	cmd_proc_writel(cmd_rx_queue_base1.all, CMD_RX_Q_BASE1);

	cmd_proc_writel(cmd_issue_queue_base.all, CMD_Q_BASE);
	cmd_proc_writel(0, CMD_Q_PTR);

	fw_cmd_act_bitmap = 0;
	memset((void *)fw_nvm_cmd, 0, sizeof(fw_nvm_cmd));

	for (i = 0; i < NUM_FW_NVM_CMD; i++)
	{
		fw_nvm_cmd[i].opc = NVME_OPC_WRITE_ZEROES;
		fw_nvm_cmd[i].nsid = 1;
	}

	// register opcode, type, and set valid
	hw_cmd_opcode_1_t cmd_opcode1 = {.all = cmd_proc_readl(HW_CMD_OPCODE_1)};
	hw_cmd_opcode_2_t cmd_opcode2 = {.all = cmd_proc_readl(HW_CMD_OPCODE_2)};
	hw_cmd_opcode_3_t cmd_opcode3 = {.all = cmd_proc_readl(HW_CMD_OPCODE_3)};
	hw_cmd_type_t cmd_type = {.all = cmd_proc_readl(HW_CMD_TYPE)};
	hw_cmd_type_1_t cmd_type1 = {.all = cmd_proc_readl(HW_CMD_TYPE_1)};
	hw_cmd_ctrl_t hwcmd_ctl = {.all = cmd_proc_readl(HW_CMD_CTRL)};

#ifndef IOMGR_WORKAROUND
	cmd_opcode1.b.hw_cmd_opcode_2 = NVME_OPC_FLUSH;
	cmd_opcode1.b.hw_cmd_opcode_3 = NVME_OPC_WRITE_ZEROES;

	cmd_type.b.hw_cmd_type_2 = HW_CMD_IO_MA_TYPE;
	cmd_type.b.hw_cmd_type_3 = HW_CMD_IO_MA_TYPE;

	hwcmd_ctl.all |= BIT(2) | BIT(3);
#endif

#if defined(OC_SSD)
	cmd_opcode2.b.hw_cmd_opcode_4 = NVME_OPC_OCSSD_V20_VEC_CHUNK_READ;
	cmd_opcode2.b.hw_cmd_opcode_5 = NVME_OPC_OCSSD_V20_VEC_CHUNK_WRITE;
	cmd_opcode2.b.hw_cmd_opcode_6 = NVME_OPC_OCSSD_V20_VEC_CHUNK_COPY;

	cmd_type.b.hw_cmd_type_4 = HW_CMD_OCSSD_V20_VEC_READ_TYPE;
	cmd_type.b.hw_cmd_type_5 = HW_CMD_OCSSD_V20_VEC_WRITE_TYPE;
	cmd_type.b.hw_cmd_type_6 = HW_CMD_OCSSD_V20_VEC_COPY_TYPE;

	/* Use bits 4:oc read, 5:oc write; 6:oc copy */
	hwcmd_ctl.all |= BIT(4) | BIT(5) | BIT(6);
#endif

	cmd_opcode2.b.hw_cmd_opcode_7 = NVME_OPC_COMPARE;
	cmd_type.b.hw_cmd_type_7 = HW_CMD_COMP_TYPE;
#if defined(RAMDISK)
	hwcmd_ctl.all |= BIT(7);
#endif
#ifndef IOMGR_WORKAROUND
	cmd_opcode3.b.hw_cmd_opcode_8 = NVME_OPC_WRITE_UNCORRECTABLE;
	cmd_opcode3.b.hw_cmd_opcode_9 = NVME_OPC_DATASET_MANAGEMENT;

	cmd_type1.b.hw_cmd_type_8 = HW_CMD_IO_MA_TYPE;
	cmd_type1.b.hw_cmd_type_9 = HW_CMD_IO_MA_TYPE;
	hwcmd_ctl.all |= BIT(8) | BIT(9);
#endif

	if(cur_ro_status == RO_MD_IN)//if in ROM status change hwcmd_ctl.all
	{
		cmd_proc_trace(LOG_ALW, 0x7ea6, "IN RO, status:%d",cur_ro_status);
		hwcmd_ctl.all &= ~ BIT(0);
	}

	cmd_proc_writel(cmd_opcode1.all, HW_CMD_OPCODE_1);
	cmd_proc_writel(cmd_opcode2.all, HW_CMD_OPCODE_2);
	cmd_proc_writel(cmd_opcode3.all, HW_CMD_OPCODE_3);
	cmd_proc_writel(cmd_type.all, HW_CMD_TYPE);
	cmd_proc_writel(cmd_type1.all, HW_CMD_TYPE_1);
#if defined(PROGRAMMER)
	cmd_proc_writel(0, HW_CMD_CTRL);
#else
	cmd_proc_writel(hwcmd_ctl.all, HW_CMD_CTRL);
#endif /* PROGRAMMER */

	abort_q_base_t abort_q_base;
	abort_q_base.b.abort_q_base = btcm_to_dma(abort_cmd_base);
	abort_q_base.b.abort_q_loc = 1;
	abort_q_base.b.abort_q_max_sz = NUM_CMDP_ABORT_CMD_BASED;

	abort_cmpl_q_base_t abort_cmpl_q_base;
	abort_cmpl_q_base.b.abort_cmpl_q_base = btcm_to_dma(abort_cmpl_base);
	abort_cmpl_q_base.b.abort_cmpl_q_loc = 1;
	abort_cmpl_q_base.b.abort_cmpl_max_sz = NUM_CMDP_ABORT_CMD_BASED;

	cmd_proc_writel(0, ABORT_Q_PTR);
	cmd_proc_writel(abort_q_base.all, ABORT_Q_BASE);
	cmd_proc_writel(0, ABORT_CMPL_Q_PTR);
	cmd_proc_writel(abort_cmpl_q_base.all, ABORT_CMPL_Q_BASE);

	cmd_proc_writel(0, INT_MASK); /* enable IRQ */
}

ps_code void cmd_proc_suspend(void)
{
	u32 i;

	for (i = 0; i < NUM_CMDP_RX_CMD; i++)
	{
		if (rx_cmd_base[i])
		{
			nvme_apl_trace(LOG_DEBUG, 0x2c70, "[WB] RXcmd proc suspend : %d ",i);
			hal_nvmet_put_sq_cmd(nvme_cmd_cast(rx_cmd_base[i]));
			rx_cmd_base[i] = 0;
		}
	}
}

ps_code void cmd_proc_req_pending(void)	//20210326-Eddie   From 3.1.8.1
{
	struct list_head *next;
	struct list_head *next2;

	list_for_each_safe(next, next2, &_del_sq_reqs) {
		req_t *req = container_of(next, req_t, entry2);
		list_del_init(next);
		if ((req->req_from == REQ_Q_ADMIN) && (req->host_cmd))
			hal_nvmet_put_sq_cmd(req->host_cmd);
		nvmet_put_req(req);
	}
}

/*!
 * @brief if write zero was supported, use this api to enable it cmd_proc
 *
 * @param en	enable write zero asic function
 *
 * @return	none
 */
fast_code void cmd_proc_write_zero_ctrl(bool en)
{
	write_zero_ctrl_t ctrl = {.b.write_zero_opcode = NVME_OPC_WRITE_ZEROES,
							  .b.write_zero_en = en};
	cmd_proc_writel(ctrl.all, WRITE_ZERO_CTRL);
}

extern bool _fg_warm_boot;

init_code void cmd_proc_init(void)
{
	poll_irq_register(VID_CMDPROC_P0, cmd_proc_isr);
	poll_irq_register(VID_CMDPROC_P1, cmd_proc_isr);
	memset(_ps_ns_fmt, 0, sizeof(_ps_ns_fmt));
	cmd_proc_resume(0);

	if(_fg_warm_boot == false)//if the _fg_warm_boot == true ,cur_ro_status keep original value
	{
		cur_ro_status = RO_MD_OUT;

        //ECCT_HIT_2_FLAG = false;
        //nvme_apl_trace(LOG_ALW, 0, "[DBG] cmd_proc_init");
	}
	else//need to prevent the address of cur_ro_status be change,duoble check the register in HW_CMD_CTRL
	{
		hw_cmd_ctrl_t hwcmd_ctl = {.all = cmd_proc_readl(HW_CMD_CTRL)};
		if( hwcmd_ctl.all & BIT(0))// 1:RO_MD_OUT, 0:RO_MD_IN
		{
			cur_ro_status = RO_MD_OUT;
		}
		else
		{
			cur_ro_status = RO_MD_IN;
		}

	}
	nvme_hal_trace(LOG_ALW, 0x2407, "init read only stat:%d",cur_ro_status);
	u32 val = readl((void *)0xc003c234);
	nvme_hal_trace(LOG_ERR, 0x62b1, "%x %x->%x\n", 0xc003c234, val, val & 0xFFFFFFF0);
	val &= 0xFFFFFFF0;
	writel(val, (void *)0xc003c234);
}

ddr_code void cmd_proc_read_only_setting(u8 setting)
{
	hw_cmd_ctrl_t hwcmd_ctl = {.all = cmd_proc_readl(HW_CMD_CTRL)};
	if(setting)//enter read only mode
	{
		hwcmd_ctl.all &= ~ BIT(0);
		cmd_proc_writel(hwcmd_ctl.all, HW_CMD_CTRL);
		cur_ro_status = RO_MD_IN;
		nvmet_evt_aer_in(((NVME_EVENT_TYPE_SMART_HEALTH << 16)|SMART_STS_RELIABILITY),0);
	}
	else//leave read only mode
	{
		cmd_rx_q_ptr_t cmd_proc_rx_pnter;


#if CO_SUPPORT_SANITIZE
		epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates)
			cmd_proc_trace(LOG_INFO, 0x1b6b, "[RO]sanitize in progress, en wr cmd later ...");
		else
#endif
			hwcmd_ctl.all |= BIT(0);
		cmd_proc_writel(hwcmd_ctl.all, HW_CMD_CTRL);


		cmd_proc_rx_pnter.all = cmd_proc_readl(CMD_RX_Q_PTR);
		leave_ro_rx_wptr = cmd_proc_rx_pnter.b.cmd_rx_wr_ptr;

		if(leave_ro_rx_wptr == rx_q_rptr)
		{
			cmd_proc_trace(LOG_INFO, 0x7598, "[RO]RX POOL empty");
			cur_ro_status = RO_MD_OUT;
		}
		else
		{
			//sys_assert(cur_ro_status == RO_MD_IN);
			cmd_proc_trace(LOG_INFO, 0x5333, "[RO]RX POOL not empty: rptr|%x wptr|%x",rx_q_rptr,leave_ro_rx_wptr);
			cur_ro_status = RO_MD_WAIT_RX_FINISH;
		}
	}
}

/*!
 * @brief	check cmd proc slot inforamtion
 *
 * @param	None
 *
 * @return	true idle state, false busy state
 */
fast_code bool cmd_proc_slot_check(void)
{
	u32 regs = CMD_SLOT_VALID_1;
	u32 cmd_slot;
	while (regs <= CMD_SLOT_VALID_15) {
		cmd_slot = cmd_proc_readl(regs);
		if (cmd_slot) {
			cmd_proc_trace(LOG_ALW, 0x36c7, "cmd_slot:0x%x 0x%x ",regs,cmd_slot);
			return false;
		}
		regs += 4;
	}
	return true;
}


ddr_code void cmd_disable_btn(s8 disable_wr, s8 disable_rd)
{
	hw_cmd_ctrl_t hwcmd_ctl = {.all = cmd_proc_readl(HW_CMD_CTRL)};
	if(disable_wr == 1)
	{
		hwcmd_ctl.all &= ~ BIT(0);
	}
	else if(disable_wr == 0)
	{
		if(cur_ro_status)
			cmd_proc_trace(LOG_INFO, 0x9cdc, "[RO]still in RO mode, en wr cmd later ...");
		else
			hwcmd_ctl.all |= BIT(0);
	}
	if(disable_rd == 1)
	{
		hwcmd_ctl.all &= ~ BIT(1);
	}
	else if(disable_rd == 0)
	{
#if(degrade_mode == ENABLE)
		extern read_only_t read_only_flags;
		if(read_only_flags.b.plp_not_done)
			cmd_proc_trace(LOG_INFO, 0x43d8, "[NON-ACCESS-MODE]still in degrade mode, dont en rd cmd");
		else
#endif
			hwcmd_ctl.all |= BIT(1);
	}
	cmd_proc_writel(hwcmd_ctl.all, HW_CMD_CTRL);
}

#if(degrade_mode == ENABLE)
ddr_code void degrade_mode_fmt_reset_io()
{
	extern read_only_t read_only_flags;
	read_only_flags.b.no_free_blk = false;
	read_only_flags.b.gc_stop = false; //When no free blk occur, gc_stop flag will turn to ture.
	read_only_flags.b.spb_retire = false;
	read_only_flags.b.plp_not_done = false;
	read_only_flags.b.spor_user_build = false;

	//smart_stat->critical_warning.raw &= 0x3E;
	//nvme_apl_trace(LOG_ALW, 0, "Clear critical warning bit[0], status %x", read_only_flags.all);

	if(read_only_flags.all == 0)
	{
		smart_stat->critical_warning.raw &= 0x33;
		nvme_apl_trace(LOG_ALW, 0x8fef, "Clear critical warning bit[2],bit[3],bit[6]");
		cmd_proc_read_only_setting(false);
	}

	cmd_disable_btn(-1, 0);
}
#endif

fast_code void reset_del_sq_resource(void)
{
	struct list_head *next;
	struct list_head *next2;

	list_for_each_safe(next, next2, &_del_sq_reqs)
	{
		req_t *req = container_of(next, req_t, entry2);
		u32 sqid = req->op_fields.admin.sq_id - 1;
		cmd_proc_trace(LOG_ALW, 0x3076, "reset_del_sq_resource SQ(%d),sn %d",sqid);
		list_del(next);
		//nvmet_put_req(req);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}

	//sys_assert(_dis_func_ctx[0].deleting);
	if(_dis_func_ctx[0].del_sq_bmp[0] || _dis_func_ctx[0].del_sq_bmp[1] || _dis_func_ctx[0].del_sq_bmp[2])
		cmd_proc_trace(LOG_INFO, 0x6fd6, "bmp:%d-%d-%d",
			_dis_func_ctx[0].del_sq_bmp[0],
			_dis_func_ctx[0].del_sq_bmp[1],
			_dis_func_ctx[0].del_sq_bmp[2]);
}

/*!
 * @brief  check if ctrl->sqs[ ] is NULL
 *
 * @param 	sq_id
 *
 * @return	true if ctrl->sqs[ ] is NULL
 */
fast_code u32 is_sqs_delete(u32 sq_id)
{
	if (ctrlr->sqs[sq_id] == NULL) {
		cmd_proc_trace(LOG_DEBUG, 0xf3d6,"sqs[%d] = NULL", sq_id);
		return true;
	} else {
		return false;
	}
}

#if CO_SUPPORT_PANIC_DEGRADED_MODE
ddr_code void assert_reset_del_sq_resource(void)
{
	u16 sqid;
	struct list_head *next;
	struct list_head *next2;
	u8 fnid;

	list_for_each_safe(next, next2, &_del_sq_reqs)
	{
		req_t *req = container_of(next, req_t, entry2);

		sqid = req->op_fields.admin.sq_id;
		fnid = req->fe.nvme.cntlid;

		if (ctrlr->sqs[sqid] == NULL) { //3.1.7.4 merged 20201201 Eddie
			cmd_proc_trace(LOG_ALW, 0xa628, "duplicated SQ(%d)", sqid);
			list_del(next);
			evt_set_imt(evt_cmd_done, (u32)req, 0);
			continue;
		}

		//if (cmd_proc_is_delete_sq_done(fnid, sqid))
		{
			cmd_proc_trace(LOG_INFO, 0x2594, "Force Del SQ(%d), fnid(%d) done req(%x)", sqid, fnid, req);
			/* unbind CQ and clean Queue */
			nvmet_unbind_cq(sqid, ctrlr->sqs[sqid]->cqid);
			nvmet_unset_sq(sqid);

			/* sqid is flexible SQID. Range is from 0 to 65 */
			hal_nvmet_delete_vq_sq(sqid);

			list_del(next);
			evt_set_imt(evt_cmd_done, (u32)req, 0);
		}
	}
}

ddr_code void cmd_proc_rx_assert_cmd(void)
{
	struct nvme_cmd *cmd;
	cmd_rx_queue_t *rx_cmd;
	cmd_rx_q_ptr_t cmd_proc_rx_pnter = {
		.all = cmd_proc_readl(CMD_RX_Q_PTR),
	};
	u8 rx_wptr = cmd_proc_rx_pnter.b.cmd_rx_wr_ptr;

	while ((ctrlr->free_nvme_cmds > 0) &&
		   (rx_q_rptr != rx_wptr) && (req_cnt > 0))
	{
		rx_cmd = &rx_q_base[rx_q_rptr];
		cmd = (struct nvme_cmd *)nvme_cmd_cast(rx_cmd_base[rx_q_rptr]);
		rx_cmd_base[rx_q_rptr] = nvme_cmd_get(__FUNCTION__);
		sys_assert(rx_cmd_base[rx_q_rptr]);

		if (rx_cmd->b.lr_err || rx_cmd->b.nsid_inv || rx_cmd->b.mdts_err || rx_cmd->b.fatal_err || cmd->fuse)
		{
			if (((rx_cmd->b.nsid_inv == 1) && (cmd->opc == NVME_OPC_FLUSH)) || (cmd->opc == NVME_OPC_WRITE_UNCORRECTABLE)) //Andy write zeros || (cmd->opc == NVME_OPC_WRITE_ZEROES)
			{
				goto Flush_invalid; //for flush with invalid Nsid FFFFFFFF
			}
			cmd_proc_rw_err_handle(rx_cmd, cmd);
		}
		else
		{
		Flush_invalid:
			cmd_proc_trace(LOG_DEBUG, 0x713c, "rx cmd cid %d sq id:%d opcode:0x%x",
						   cmd->cid, rx_cmd->b.sq_id, cmd->opc);
			extern void nvmet_rx_handle_assert_cmd(u32 sqid, struct nvme_cmd * cmd, u32 nvm_cmd_id, u8 func_id);
			nvmet_rx_handle_assert_cmd(rx_cmd->b.sq_id, cmd, cmd->cid, rx_cmd->b.function_id);
		}
		rx_q_rptr = (rx_q_rptr + 1) & NUM_CMDP_RX_CMD_0_BASED;
		cmd_proc_rx_pnter.b.cmd_rx_rd_ptr = rx_q_rptr;
		cmd_proc_writel(cmd_proc_rx_pnter.all, CMD_RX_Q_PTR);
	}
}

/**
 * @brief command complete processor ISR handler
 *
 * Handle cmd_completed interrupt here.
 *
 * @return	always return 0
 */
ddr_code void Assert_cmd_proc_isr(void)
{
	bool all_finish;
	/* TODO: copy the entries to evt handler and recyle the room */
	unmask_int_stts_t int_stts = {
		.all = cmd_proc_readl(INT_MASK + 4),
	};
	u32 stts = int_stts.all;

	if (int_stts.b.err_aborted)
	{
		all_finish = cmd_proc_err_cmpl_cmd();
		if(all_finish)
		{
			int_stts.b.err_aborted = 0;
		}
	}

	if (int_stts.b.cmd_rcvd)
	{
		cmd_proc_rx_assert_cmd();
		int_stts.b.cmd_rcvd = 0;
	}

	if (int_stts.b.sq_deleted)
	{
		cmd_proc_delete_sq_done();
		int_stts.b.sq_deleted = 0;
	}

	if (int_stts.b.func_dis_done)
	{
		cmd_proc_disable_function_done();
		int_stts.b.func_dis_done = 0;
	}

	if (int_stts.b.cmd_abort_done)
	{
		cmd_proc_cmd_abort_cmpl();
		int_stts.b.cmd_abort_done = 0;
	}

	if (int_stts.b.du_cmd_inv)
	{
		debug_slot_t ds = {.all = cmd_proc_readl(DEBUG_SLOT)};

		// if fw push wrong RDE to BTN, this error will be popped
		// du_cmd_inv_slot is the error cmd_proc_cid
		cmd_proc_trace(LOG_ERR, 0xe888, "du cmd inv slot %d, cmd inv slot %d",
					   ds.b.du_cmd_inv_slot, ds.b.cmd_inv_slot);

		int_stts.b.du_cmd_inv = 0;
	}

	if (int_stts.all)
		cmd_proc_trace(LOG_ERR, 0x7da0, "unhandled %x", int_stts.all);

	cmd_proc_writel(stts, UNMASK_INT_STTS);
}

ddr_code void cmd_proc_non_read_write_mode_setting(u8 setting)
{
	hw_cmd_ctrl_t hwcmd_ctl = {.all = cmd_proc_readl(HW_CMD_CTRL)};
	if(setting)
	{
		hwcmd_ctl.all &= ~ (BIT(0) | BIT(1));
		cmd_proc_writel(hwcmd_ctl.all, HW_CMD_CTRL);
	}
	else//leave read only mode
	{
		hwcmd_ctl.all |= (BIT(0) | BIT(1));
		cmd_proc_writel(hwcmd_ctl.all, HW_CMD_CTRL);
	}
}
#endif
/*! @} */
