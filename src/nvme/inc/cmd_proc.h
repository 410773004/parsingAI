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
 * @brief Command processor function for HAL layer
 *
 * \addtogroup hal
 */
#pragma once

struct _req_t;


#define IOMGR_WORKAROUND



/*!
 * @brief cmd_proc abort command receive queue
 */
typedef union {
	u32 all;
	struct {
		u32 cmd_id :16;	/*! bit15:0 command id */
		u32 sq_id :8;	/*! bit23:16 sq id */
		u32 cmd_aborted :1;	/*! bit24 cmd aborted */
		u32 cmd_started :1;	/*! bit25 cmd started, abort fail */
		u32 cmd_in_btn :1;		/*! bit 26 cmd in btn */
		u32 cmd_not_found :1;	/*! bit 27 cmd not found */
		u32 reserved :4;		/*! bit 31:28 Reserved */
	} b;
} cmd_abort_cmpl_t;

/*! @brief cmd_proc disable function completion callback type */
typedef void (*cmd_proc_dis_func_cb_t)(void *, u8);

enum {
	HW_CMD_READ_TYPE = 0,
	HW_CMD_WRITE_TYPE,
	HW_CMD_COMP_TYPE,
	HW_CMD_IO_MA_TYPE,

	/* TODO: maybe we need have some hw cmd mgr api */
	HW_CMD_OCSSD_V20_VEC_READ_TYPE,
	HW_CMD_OCSSD_V20_VEC_WRITE_TYPE,
	HW_CMD_OCSSD_V20_VEC_COPY_TYPE,
};

typedef union {
	u32 all;
	struct {
		u32 aw_dtag : 24;	// latest DTAG received from BTN, may used for DATA xfer or force release
		u32 aw_cs : 5;		// state machine for write data
					// 5'h0	idle
					// 5'h1	request DTAG
					// 5'h2	wait for DTAG
					// 5'h3	check for data availability
					// 5'h5 check if all requests for the FIFO have been sent
					// 5'h6 save an error dt_que entry need to release DTAG
					// 5'h7 save an error dt_que entry, not to release DTAG
					// 5'h8 wait for i_main_data_err_ack
					// 5'h9 wait for all transfers finish during error handling
					// 5'hA check f_que during error handling
					// 5'hB discard an fq entry, abort the command
					// 5'hC abort due to sys_abort
					// 5'hD abort done due to error other than system abort
					// 5'hE system abort, save an error to dt_que, need to release DTAG
					// 5'hF wait for all transactions finish during system abort
					// 5'h10 send sq_wr_req for filling partial DU
					// 5'h11 check if filling partial DU finishes
		u32 rsvd : 3;
	};
} wr_dma_axi_wr_dbg_4_t;

typedef union {
	u32 all;
	struct {
		u32 dq_cmd_tag : 10;			// NVM cmd slot
		u32 dq_rid : 3;				//
		u32 dq_err_cmpl : 1;			//
		u32 dq_rel_dtag : 1;			//
		u32 dq_axi_to : 1;			// axi bus timeout
		u32 dq_par_err : 1;			//
		u32 dq_meta_size : 1;			//
		u32 dq_dix_mode : 1;			//
		u32 dq_pi_first : 1;
		u32 dq_pi_ins_en : 1;			//
		u32 dq_pi_en : 1;			//
		u32 dq_lb_bdry : 1;			//
		u32 dq_cmd_last_blk_in_fifo : 1;	//
		u32 dq_ff_last_blk : 1;			//
		u32 dq_first_du : 1;			//
		u32 dq_wz_cmd : 1;			//
		u32 dq_part_wr_set : 1;			//
		u32 dq_fill_part_du : 1;		//
		u32 dq_cmd_last_xfr : 1;		//
		u32 w_dq_empty : 1;			//
		u32 w_dq_full : 1;			//
	};
} wr_dma_axi_wr_dbg_5_t;

typedef union {
	u32 all;
	struct {
		u32 dq_dtag : 24;	// DTAG used for ongoing data xfer
		u32 dt_cs : 4;		// current state of data xfer
					// 4'h idle
		u32 rsvd : 4;
	};
} wr_dma_axi_wr_dbg_6_t;

typedef union {
	u32 all;
	struct {
		u32 w_nq_diff : 4;	//
		u32 w_nq_rd : 12;	//
		u32 w_cq_diff : 5;	//
		u32 w_cq_rd : 10;	//
		u32 rsvd : 1;
	};
} wr_dma_axi_wr_dbg_7_t;

/*!
 * @brief cmd_proc PMU resume function
 *
 * @param abort		if resume from suspend abort
 *
 * @return      	none
 */
extern void cmd_proc_resume(int abort);

/*!
 * @brief cmd_proc PMU suspend function
 *
 * @return		none
 */
extern void cmd_proc_suspend(void);

/*!
 * @brief Initialize cmd_proc registers and relative INT resources.
 *
 * @return      none
 */
extern void cmd_proc_init(void);

extern void cmd_proc_req_pending(void);	//20210326-Eddie   From 3.1.8.1

#if defined(HMETA_SIZE)
/*!
 * @brief configure host PI
 * Set the command processor PI control registers
 *
 * @param pi_type	PI type, NVME Spec:
 *                  0 means PI is disabled
 *                  1 means PI is enabled, type 1
 *                  2 means PI is enabled, type 2
 *	                3 means PI is enabled, type 3
 * @param meta_set	Metadata settings, NVME Spec:
 *                  0 means metadata transferred separate,
 *                  1 means metadata transferred as part of an extended data LBA
 * @param pil       PI location, NVME Spec:
 *                  0 means PI is transferred as the last 8 bytes of metadata
 *                  1 means PI is transferred as the first 8 bytes of metadata
 * @param nsid      Namespace ID to be set
 * @param hmeta_size	host meta size
 *
 * @return		not used
 */
extern void cmd_proc_hmeta_ctrl(u8 pi_type, u8 meta_set, u8 pil, u16 nsid, u16 hmeta_size);
#endif //HMETA_SIZE

#ifdef NS_MANAGE
/*!
 * @brief delete namespace valid bitmap
 *
 * @param u32 ns_id, host command namespace id
 *
 * @return      none
 */
extern void cmd_proc_ns_del(u32 ns_id);

/*!
 * @brief change namespace wp state
 *
 * @param u32 ns_id, host command namespace id
 * @param bool wp, write protect or not
 *
 * @return      none
 */
extern void cmd_proc_ns_set_wp(u32 ns_id, bool wp);
#endif

/*!
 * @brief setup LBA format
 * Configure name space's LBA format, etc. 9 for 512 and 12 for 4K
 * Note that only 9 and 12 is supported.
 *
 * @param nsid		Namespace ID
 * @param lbads		LBA data size shift, i.e. 9 for 512 and 12 for 4K
 * @param lbacnt	max lba number
 * @param hmeta_size	host metadata buffer size per sector
 *
 * @return              none
 */
extern void cmd_proc_set_lba_format(u32 nsid, u8 lbads, u64 lbacnt, u16 hmeta_size);

/*!
 * @brief setup payload size and read size
 *
 * @param max_pl_sz	Maximum payload size
 * @param max_rd_sz	PCIe Maximum read size
 *
 * @return              none
 */
extern void cmd_proc_set_payload_sz(u32 max_pl_sz, u32 max_rd_sz);

/*!
 * @brief set mps from cc.en
 *
 * @param mps	host memory page shift bit from 4K
 *
 * @return	none
 */
extern void cmd_proc_set_mps(u32 mps);

/*!
 * @brief delete SQ, block host CQ delivery
 *
 * @param sqid		sq to be deleted
 * @param req		admin request to delete SQ
 *
 * @return		not used
 */
extern void cmd_proc_delete_sq(u16 sqid, struct _req_t *req);

/*!
 * @brief  clear delete SQ
 *
 * @param sqid		sq to be deleted
 *
 * @return		not used
 */
extern void cmd_proc_clear_delete_sq(u16 sqid);

/*!
 * @brief This function disable function
 *
 * @param fid		function id
 * @param del_sq_bmp	sq bitmap should be deleted
 * @param ctx		parent context
 * @param cmpl		callback function when done
 *
 * @return		not used
 */
extern void cmd_proc_disable_function(u8 fid, u32 del_sq_bmp[3], void *ctx, cmd_proc_dis_func_cb_t cmpl);

/*!
 * @brief This function abort single command using HW Abort Req and Abort Cmpl Q
 *
 * @param sqid		SQ ID of the command (cdw10[15:00])
 * @param cmd_id	CMD ID of the command (cdw10[31:16])
 *
 * @return		not used
 */
extern void cmd_proc_abort_single_cmd(u16 sqid, u16 cmd_id);

/*!
 * @brief This function abort all commands, CC.EN clear can be used to abort all commands. If immediately abort is needed
 *
 * @return	not used
 */
extern void cmd_proc_abort_all_commands(void);

/*!
 * @brief  cmd_proc rx NVMe I/O SQ commands processor handler
 *
 * @param None
 *
 * @return	None
 */
extern void cmd_proc_rx_cmd(void);

/*!
 * @brief This function init function ram after function disable
 *
 *
 * @return	not used
 */
extern void cmd_proc_disable_function_ram_init(void);

/*!
 * @brief if write zero was supported, use this api to enable it cmd_proc
 *
 * @param en	enable write zero asic function
 *
 * @return	not used
 */
extern void cmd_proc_write_zero_ctrl(bool en);

extern u8 cmd_proc_wdma_stuck_dtag(u32 *dtag);

/*!
 * @brief check if CMD_PROC DMA idle
 *
 * @return	true for CMD_PROC DMA idle
 */
extern bool is_cmd_proc_dma_idle(void);

/*!
 * @brief  check ctrl->sqs[ ] if is NULL
 *
 * @param 	sq_id
 *
 * @return	true/false
 */
extern u32 is_sqs_delete(u32 sq_id);

#define DUMP_SRAM_REG

#ifdef DUMP_SRAM_REG
extern void dump_sram_error_reg(void);
#endif

/*!
 * @brief	check cmd proc slot inforamtion
 *
 * @param	None
 *
 * return	true idle state, false busy state
 */
extern bool cmd_proc_slot_check(void);

extern void cmd_proc_read_only_setting(u8 setting);
extern void cmd_disable_btn(s8 disable_rd, s8 disable_wr);
extern void hal_nvmet_dma_read_engine_states_chk(u16 sqid);
extern void reset_del_sq_resource(void);
extern void assert_reset_del_sq_resource(void);
extern void cmd_proc_non_read_write_mode_setting(u8 setting);
/*! @} */
