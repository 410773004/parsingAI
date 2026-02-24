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
#pragma once

#include "types.h"
#include "cbf.h"
#include "mpc.h"
#include "misc.h"
#include "../../nvme/inc/nvme_precomp.h"


#ifdef MPC

#if MPC == 1
	#error "mpc should be larger than 1"
#endif

#define CPU_MSG_CPU_NUM		MPC		///< cpu_msg cpu number

#ifdef RAWDISK
	#define CPU_MSG_Q_SIZE	256		///< rawdisk use cpu msg to dispatch NCL command
#else
	#define CPU_MSG_Q_SIZE	128
#endif

#define CPU_MSG_TX_CPU_IDX	CPU_ID_0	///< current cpu transfer cup id 0 base
#define CPU_MSG_RX_CPU_IDX	CPU_ID_0	///< current cpu receiver cup id 0 base

#define MSG_INT_CAUSED_BY_CPU1	BIT0
#define MSG_INT_CAUSED_BY_CPU2	BIT1
#define MSG_INT_CAUSED_BY_CPU3	BIT2
#define MSG_INT_CAUSED_BY_CPU4	BIT3

#define MSG_CPU1_ID		1
#define MSG_CPU2_ID		2
#define MSG_CPU3_ID		3
#define MSG_CPU4_ID		4

enum {//20200310-maurice VU cmd ipc
	VSC_ERASE_NAND,
	VSC_SCAN_DEFECT,
	VSC_AGING_BATCH,
	VSC_PLIST_BACKUP,
	VSC_DRAM_DLL,
	VSC_Read_FlashID,
	//VSC_Read_NorID,
	VSC_DRAMtag_Clear,
	VSC_Plist_OP,
	VSC_Read_Tempture,
	VSC_Read_Agingtest,
	VSC_Read_Bitmap,
	VSC_Read_P1list,
	VSC_Read_P2list,
	VSC_Read_CTQ,
	VSC_AGING_ERASEALL,
	VSC_AGING_ERASENAND,
	VSC_PREFORMAT,
	VSC_Read_SysInfo,
	VSC_Refresh_SysInfo,
	VSC_GBBPlus
};


typedef struct _ipc_evt_t {
	u32 evt_opc;		/// < event admin command opcode
	u32 r0, r1;		///< event parameters
} ipc_evt_t;
//_GENE_TEMP_USE//add for vu

enum {
//0
	CPU_MSG_RAWDISK_4K_READ,	///< rawdisk 4k read message
	CPU_MSG_NCMD,			///< ncl_cmd submit
	CPU_MSG_NCMD_DONE,		///< ncl_cmd submit done
	CPU_MSG_NCMD_INSERT_SCH, ///<ncl_cmd insert to the scheduler
	CPU_MSG_RAPID_NCMD,		///< rapid ncl command issue
	CPU_MSG_RAPID_NCMD_DONE,	///< rapid ncl command done
	CPU_MSG_SCHEDULE_ERASE,		///< erase issue
	CPU_MSG_SCHEDULE_ERASE_DONE,	///< erase done
	CPU_MSG_BM_COPY,		///< bm copy
	CPU_MSG_SHA3_SM3_CALC,		///< SHA3/SM3 calculation
//10
	CPU_MSG_SHA3_SM3_CALLBACK,		///< SHA3/SM3 calculation done callback
	CPU_MSG_BM_COPY_DONE,		///< bm copy done
	CPU_MSG_SPB_QUERY,		///< query for spb
	CPU_MSG_SPB_ACK,		///< ack for spb query
	CPU_MSG_SPB_WEAK_RETIRE,	///< set spb weak or retire
	CPU_MSG_SPB_RD_CNT_UPDT,	///< update a spb read counter
	CPU_MSG_SPB_RD_CNT_UPDT_ACK,	///< ack a spb read counter update
	CPU_MSG_SRB_READ_DEFECT,	///< defect infomation read
	CPU_MSG_RD_ERR,			///< read error message
	CPU_MSG_NS_START,		///< namespace start action
//20
	CPU_MSG_NS_OPEN,		///< namespace open action
	CPU_MSG_NS_OPEN_DONE,		///< namespace open action done
	CPU_MSG_NS_CLEAN,		///< make namespace clean
	CPU_MSG_FC_CTX_DONE,		///< messge to handle ftl core context done
	CPU_MSG_FLUSH_TABLE,		///< given table flush action
	CPU_MSG_FLUSH_MISC,		///< flush misc infomation
	CPU_MSG_FLUSH_DATA,		///< data flush
	CPU_MSG_FORMAT,			///< format
	CPU_MSG_GC_START,		///< start gc action
	CPU_MSG_GC_DONE,		///< gc done
//30
	CPU_MSG_FTL_FLUSH,		///< flush ftl on the fly entries
	CPU_MSG_FTL_FLUSH_DONE,		///< ftl flush done
	CPU_MSG_FTL_FORMAT,		///< ftl format
	CPU_MSG_FTL_FORMAT_DONE,	///< ftl format done
	CPU_MSG_TABLE_FLUSHED,		///< table flush done
	CPU_MSG_UPDATE_FENCE,		///< ftl fence update
	CPU_MSG_RESTORE_FENCE,		///< restore ftl fence value
	CPU_MSG_RESTORE_RT_FLAGS,	///< restore run time flag
	CPU_MSG_LOG_LEVEL_CHG,		///< log level change action
	CPU_MSG_RC_DTAG_ALLOC,		///< allocate raid recovery dtag
//40
	CPU_MSG_RC_DTAG_ALLOCED,	///< recovery dtag allocate done
	CPU_MSG_RC_DTAG_FREE,		///< free raid recovery dtag
	CPU_MSG_XFER_RC_DATA,		///< transfer raid recovery data
	CPU_MSG_DEFECT_SCAN,		///< defect spb scan
	CPU_MSG_SPB_PAD,		///< pad spb action
	CPU_MSG_SPB_PAD_DONE,		///< pad spb done
	CPU_MSG_PMU_SYNC_SUSPEND,	///< pmu suspend action
	CPU_MSG_WB_SYNC_LOCK,		///< CPU warmboot lock
	CPU_MSG_WR_ERA_INS,		///< die queue write/erase insert
	CPU_MSG_WR_ERA_DONE,		///< die queue write/erase done
//50
    CPU_MSG_DTAG_RECYCLE,		///< recycle dtag after bootup
	CPU_MSG_HEAP_ALLOC,		///< heap allocation from remote CPU
	CPU_MSG_WR_CREDIT,		///< add write credit
	CPU_MSG_ADD_REF,		///< add reference count
	CPU_MSG_DTAG_PUT,		///< release one DTAG from remote CPU
	CPU_MSG_DTAG_GET_SYNC,		///< acquire one DTAG from remote CPU
	CPU_MSG_DTAG_GET_ASYNC,		///< acquire one DTAG from remote CPU
	CPU_MSG_DTAG_GET_ASYNC_DONE,	///< acquire one DTAG from remote CPU
	CPU_MSG_RAID_CORRECT_PUSH,	///< push raid correct req to CPU_BE
	CPU_MSG_READ_DONE_RC_ENTRY,   ///< Tony Lin 20201027
//60
	CPU_MSG_RAID_CORRECT_DONE,	///< raid correct done
	CPU_MSG_GC_GRP_RD_DONE,		///< gc group write done
	CPU_MSG_GC_DTAG_DONE,		///< GC write dtag early done event
	CPU_MSG_FWDL_OP,		///< fw download
	CPU_MSG_FWDL_OP_DONE,		///< fw download done
	CPU_MSG_NCL_CMD_EMPTY,		///< check if ncl cmd empty
	CPU_MSG_BTN_SEMI_WAIT_SWITCH,	///< call btn_semi_wait_switch
	CPU_MSG_UCACHE_READ_ERROR,	///< ucache read error data in
	CPU_MSG_GET_BTN_WR_SMART_IO,	///< get btn receive write smart io info
	CPU_MSG_GET_BTN_RD_SMART_IO,	///< get btn receive write smart io info
//70
	CPU_MSG_GC_GRP_SUSPEND,		///< suspend gc group
	CPU_MSG_GC_ACT,			///< stop gc
	CPU_MSG_GC_ACT_DONE,		///< gc has been stopped
	CPU_MSG_FCORE_GC_ACT,		///< ftl gc action
	CPU_MSG_FCORE_GC_ACT_DONE,	///< ftl gc action done
	CPU_MSG_SIRQ_CTRL,		///< enable or disable system interrupt
	CPU_MSG_EVLOG_MGR_COPY,		///< alert event log manager to copy
	CPU_MSG_EVLOG_CLEAR_AND_RESET,	///< alert event log to clear nand log block and to reset page index
	CPU_MSG_CPU1_PANIC,
	CPU_MSG_CPU2_PANIC,
//80
	CPU_MSG_CPU4_PANIC,
	CPU_MSG_WAIT_NCL_IDLE,		///< wait NCL idle
	CPU_MSG_NCL_CMD_BLOCK,		///< set NCL block
	CPU_MSG_NCL_HANDLE_PENDING_CMD,	///< handle NCL pending list
	CPU_MSG_WRITE_DE_ABORT,		////< btn write data entries abort
	CPU_MSG_FE_REQ_OP,		///< read or write fe log
	CPU_MSG_FE_REQ_OP_DONE,		///< read or write fe log done
	CPU_MSG_LOAD_P2L,		///< load p2l sync
	CPU_MSG_GET_SPARE_AVG_ERASE_CNT,		///< get spare average erase count information from FTL
	CPU_MSG_GET_SPARE_AVG_ERASE_CNT_DONE,		///< get spare average erase count information done
//90
	CPU_MSG_UPDATE_SPARE_AVG_ERASE_CNT,		///< for 10mins update
	CPU_MSG_UCACHE_SUSP_TRIM,	///< suspend trim
	CPU_MSG_UCACHE_SUSP_TRIM_DONE,	///< suspend trim done
	CPU_MSG_UCACHE_RESM_TRIM,	///< resume trim
	CPU_MSG_FW_BTAG_RELEASE,	///< inform release one write btn cmd
	CPU_MSG_L2P_LOAD,		///< load l2p segment
	CPU_MSG_L2P_LOAD_DONE,		///< l2p segment load done
	CPU_MSG_FTL_PL,			///< power loss event to FTL
	CPU_MSG_BE_PL,			///< power loss event
	CPU_MSG_BG_TASK_EXEC,		///< inform cpu to execute background task
//100
	CPU_MSG_BG_TASK_DONE,		///< background task execute done
	CPU_MSG_BG_TASK_ABORT,		///< abort background task
	CPU_MSG_READ_RECOVERIED_COMMIT,	///< read error recovery issue
	CPU_MSG_READ_RECOVERIED_DONE,	///< read error recovery done
	CPU_MSG_PMU_SWAP_REG,		///< pmu SWAP memory range register
	CPU_MSG_BTN_CMD_DONE,		///< inform FW to report cmd's completion queue
	CPU_MSG_BTN_WCMD_ABORT,		///< abort btn write outstanding command
	CPU_MSG_STRIPE_XOR_DONE,		///< cwl stripe all du xot done
	CPU_MSG_SCHED_ABORT_STRM_READ,	///< abort scheduler 1 stream read
	CPU_MSG_SCHED_RESUME_STRM_READ,	///< resume scheduler to handle stream read
//110
	CPU_MSG_SCHED_ABORT_STRM_READ_DONE,	///< abort scheduler 1 stream read done
	CPU_MSG_PERST_FTL_NOTIFY,	///< PERST happened
	CPU_MSG_PERST_FTL_CHK_ACK,	///< check ftl if ack was post
	CPU_MSG_PERST_DONE_FTL_NOTIFY,	///< PERST done ftl notify
	CPU_MSG_WAIT_BTN_RESET,		///< wait for BTN reset
	CPU_MSG_FC_READY_CHK,		///< check fc ready status
	CPU_MSG_FC_READY_DONE,		///< fc ready check done  //CPU_MSG_FRB_UPDATE_QBTINFO,
	CPU_MSG_EPM_UPDATE,		///< epm_ipc
	CPU_MSG_EPM_GET_KEY,		///< epm_ipc get key
	CPU_MSG_EPM_UNLOCK,		///< epm_ipc unlock
//120
	CPU_MSG_JOURNAL_UPDATE,
	CPU_MSG_EPM_REMAP_tbl_UPDATE,		///< epm_remap table update; 120
	CPU_MSG_FW_CONFIG_Rebuild,		//20201008-Eddie
	CPU_MSG_SRB_ERASE,		//20201014-Eddie
	CPU_MSG_EVT_VUCMD_SEND,         /// _GENE_20200714
	CPU_MSG_EVT_AGING,                      /// _GENE_20200710
    CPU_MSG_PLP_DEBUG_FILL,               //plp debug fill up flow
	CPU_MSG_PLP_FILL_DONE,                //use to notice cpu 2 fill up done
	CPU_MSG_PLP_FORCE_FLUSH,          //force to flush cache to ftl core
	CPU_MSG_PLP_TRIGGER,
//130
	CPU_MSG_PLP_DONE,
	CPU_MSG_TZU_GET,
	CPU_MSG_FTL_FULL_TRIM,    ///Maksin_20200827
	CPU_MSG_FTL_FULL_TRIM_HANDLE_DONE,//Maksin add to avoid cpu1 & cpu 3 died lock
    CPU_MSG_REG_GLIST,            ///< REGGLIST NIKLAUS 20200819
    CPU_MSG_ECCT_OPERATION,       ///< ECCT Operation  Tony 20201029
    CPU_MSG_ECCT_BUILD,           ///< ECCT build  Tony 20201102
    CPU_MSG_SET_FAIL_OP_BLK,        ///< SET FAIL BLK INTO FAILOPEN BLK ARRAY NIKLAUS 20200902
    CPU_MSG_DEL_FAIL_OP_CNT,        ///< DEL FAIL BLK OPEN CNT NIKLAUS 20200902
    CPU_MSG_DEL_FAILBLK,            ///< DEL_FAILBLK NIKLAUS 20200819
//140  
	CPU_MSG_SET_FAILBLK, 			///< SET_FAILBLK NIKLAUS 20200819   
    CPU_MSG_SET_ERR_TASK,
    CPU_MSG_SCAN_WRITTEN,
    CPU_MSG_INIT_WRITTEN,
    CPU_MSG_SCAN_GLIST_TRG_HANDLE,  //when fail_array over 10 will trigger
    CPU_MSG_ECCT_MARK,
	CPU_MSG_crypto,			///< Andy crypto
	CPU_MSG_Loadcrypto,      ///< Andy crypto
	CPU_MSG_SysInfo_UPDATE,	  //Alan CC HUANG
	CPU_MSG_L2P_RESET,
//150
	CPU_MSG_GC_STOP,
	CPU_MSG_GC_RESET,
	CPU_MSG_OPEN_QBT,
	CPU_MSG_OPEN_QBT_DONE,
	CPU_MSG_WARM_BOOT_FTL_RESET,
	CPU_MSG_GET_PROGRAM_AND_ERASE_FAIL_COUNT,
	CPU_MSG_GET_EC_COUNT,
	CPU_MSG_GET_NAND_BYTE_WRITTEN,
	CPU_MSG_RELEASE_SPB,
	CPU_MSG_RELEASE_PBT_SPB,
//160
	CPU_MSG_RD_ERR_HANDLING,
    CPU_MSG_CLEAN_EC_TBL,
	CPU_MSG_CLR_GLIST,      ///< Paul_2021010
	CPU_MSG_FLUSH_BLKLIST,
	CPU_MSG_FREE_BLKLIST,
	CPU_MSG_FLUSH_BLKLIST_HANDLING,
	CPU_MSG_PSTREAM_ACK,
	CPU_MSG_FORCE_CLOSE_PSTREAM,	// ISU, Tx, PgFalClsNotDone (1)
	CPU_MSG_FREE_FLUSH_BLKLIST_SDTAG,
	CPU_MSG_GET_ADDITIONAL_SMART_INFO,
//170	
	CPU_MSG_GET_ADDITIONAL_SMART_INFO_DONE, //CPU_MSG_FTL_NS_FLUSH_DONE,
#if (Xfusion_case)
		CPU_MSG_GET_INTEL_SMART_INFO_DONE, 
#endif
	CPU_MSG_ENTER_READ_ONLY_MODE,
	CPU_MSG_LEAVE_READ_ONLY_MODE,
	CPU_MSG_DISABLE_BTN,
	CPU_MSG_RC_REG_ECCT,    ///< reg ecct
	CPU_MSG_AER,//add for aer event by suda
	CPU_MSG_FORCE_FLUSH,//Ethan_2021_0311 for plp
	CPU_MSG_ULINK,  ///< for save cdump via uart use.
	CPU_MSG_SUSPEND_GC,//178
	CPU_MSG_SWITCH_DU_FMT,
//180
	CPU_MSG_SWITCH_CMF,
	CPU_MSG_RETRY_REWRITE,
	CPU_MSG_RETRY_GET_LDA_REWRITE,
	CPU_MSG_UECC_FILL_SMART_INFO,
	CPU_MSG_FRB_DROP_P2GL,	// FET, RelsP2AndGL
	CPU_MSG_GC_HANDLE_FWDL,		///< gc hamdle in FWDL 20210308-Eddie
	CPU_MSG_CONTROL_GC,     ///< vu cmd control gc
	CPU_MSG_GET_NEW_EC,
	CPU_MSG_GET_NEW_EC_DONE,
	CPU_MSG_GET_NEW_EC_SMART_DONE,
//190
	CPU_MSG_UPDATE_SMART_STATE,
	CPU_MSG_PLP_CANCEL_DIEQUE,
	CPU_MSG_ONDEMAND_DUMP,		///< dump key info on demand
	CPU_MSG_RESUME_SEMI_DDR_DTAG,
	CPU_MSG_INIT_EH_MGR,	// FET, PfmtHdlGL
	CPU_MSG_HOST_GET_LDA,   //get host and ua err lda
	CPU_MSG_STOP_GC_FOR_VAC_CAL,
    CPU_MSG_ACTIVE_GC,
    CPU_MSG_COMPARE_VAC,
	CPU_MSG_DUMP_GLIST,		// FET, DumpGLLog
//200	
	CPU_MSG_FORCE_PBT,
	CPU_MSG_FORCE_PBT_CPU1_ACK,
	CPU_MSG_DEL_NS_FTL_HANDLE,
	CPU_MSG_CLEAR_API,
	CPU_MSG_EVLOG_DUMP,     ///cpu1 to cpu 3 for dump log
	CPU_MSG_EVLOG_DUMP_ACK,
	CPU_MSG_EVT_VUCMD_SEND_CPU2,
	CPU_MSG_CHK_GC,
	CPU_MSG_PATROL,
	// #if CO_SUPPORT_DEVICE_SELF_TEST
	//CPU_MSG_DT_DELAY,
	//CPU_MSG_DT_DELAY_DONE,
	// #endif
    CPU_MSG_SAVE_PBT_PARAM,
	CPU_MSG_RA_READ_ERROR,
//210	
	CPU_MSG_PLP_SET_ENA,
    CPU_MSG_FPLP_TRIGGER,
    CPU_MSG_PLA_PULL,
    CPU_MSG_IO_CMD_SWITCH,
    CPU_MSG_SET_SPB_OVER_TEMP_FLAG,
    CPU_MSG_BTN_WR_SWITCH,
    CPU_MSG_TRIM_CPY_VAC,
    CPU_MSG_INIT_DONE_SAVE_EPM,
#ifdef TCG_NAND_BACKUP
	CPU_MSG_TCG_NAND_API,
	CPU_MSG_TCG_CHANGE_CHKFUNC_API,
	CPU_MSG_MBR_RD,
#endif
	CPU_MSG_FW_ARD_HANDLER,     //For FW ARD operations
    CPU_MSG_READ_RETRY_HANDLING,
	CPU_MSG_GC_PLP_SLC,
	CPU_MSG_RD_KOTP,
	CPU_MSG_CHK_BG_TRIM,
	CPU_MSG_ECCT_RECORD,
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	CPU_MSG_GET_CPU3_TELEMETRY_CTRLR_DATA,
	CPU_MSG_GET_TELEMETRY_DATA_DONE,  // receive telemetry data
	CPU_MSG_REQUIRE_UPDATE_TELEMETRY_CTRLR_DATA, //require to update ctrlr log
	CPU_MSG_GET_ERRHANDLEVAR_CPU_BE,
	CPU_MSG_GET_ERRHANDLEVAR_CPU_BE_SMART,
	CPU_MSG_GET_ERRHANDLEVAR_CPU_FTL,
#endif

#if 1//CO_SUPPORT_DEVICE_SELF_TEST
	CPU_MSG_DST_PATROL,     //read scan all WL (For DST only)
#endif
	CPU_MSG_VC_RECON,
	CPU_MSG_LOOP_FEEDBACK,	//CPU4 1min send to other CPUs	
	CPU_MSG_END,			///< end of the cpu message
};

enum {
	CPU_MSG_Q_CPU1_TO_CPU2 = 0,	///< msg queue for cpu1 to cpu2
	CPU_MSG_Q_CPU2_TO_CPU1,		///< msg queue for cpu2 to cpu1
#if MPC >= 3
	CPU_MSG_Q_CPU1_TO_CPU3,		///< msg queue for cpu1 to cpu3
	CPU_MSG_Q_CPU2_TO_CPU3,		///< msg queue for cpu2 to cpu3
	CPU_MSG_Q_CPU3_TO_CPU1,		///< msg queue for cpu3 to cpu1
	CPU_MSG_Q_CPU3_TO_CPU2,		///< msg queue for cpu3 to cpu2
#endif
#if MPC >= 4
	CPU_MSG_Q_CPU1_TO_CPU4,		///< msg queue for cpu1 to cpu4
	CPU_MSG_Q_CPU2_TO_CPU4,		///< msg queue for cpu2 to cpu4
	CPU_MSG_Q_CPU3_TO_CPU4,		///< msg queue for cpu3 to cpu4
	CPU_MSG_Q_CPU4_TO_CPU1,		///< msg queue for cpu4 to cpu1
	CPU_MSG_Q_CPU4_TO_CPU2,		///< msg queue for cpu4 to cpu2
	CPU_MSG_Q_CPU4_TO_CPU3,		///< msg queue for cpu4 to cpu3
#endif
	CPU_MSG_Q_CPU_TO_CPU_END,	///< end of the msg queue number
};

typedef union {
	u32 all;
	struct {
		u32 read_pointer_cpu:15;
		u32 rptr_phase_cpu:1;
		u32 read_pointer_mirror_from_cpu:15;
		u32 rptr_phase_mirror_from_cpu:1;
	} b;
} msg_rp_reg_t;

typedef union {
	u32 all;
	struct {
		u32 write_pointer_to_cpu:15;
		u32 wptr_phase_to_cpu:1;
		u32 write_pointer_mirror_from_cpu:15;
		u32 wptr_phase_mirror_from_cpu:1;
	} b;
} msg_wp_reg_t;

typedef struct _cpu_msg_req_t {
	union {
		u32 dw0;
		struct {
			u8 msg;		///< message to pass
			u8 tx;		///< transfer cpu id
			u16 flags;	///< command flags
		} cmd;
	};
	u32 pl;				///< payload
} cpu_msg_req_t;

typedef struct _cpu_msg_q_t {
	cpu_msg_req_t volatile req[CPU_MSG_Q_SIZE];	///< request buffer
} cpu_msg_q_t;

typedef struct _cpu_msg_t {
	u32 volatile sync_done[CPU_MSG_CPU_NUM];	///< synchronous cpu msg handshake
	cpu_msg_q_t q[CPU_MSG_Q_CPU_TO_CPU_END];	///< cpu msg queue pointer
} cpu_msg_t;

typedef struct _cpu_msg_que_t {
	u32 wptr;
	u32 rptr;
} cpu_msg_que_t;

static inline void cpu_msg_que_init(cpu_msg_que_t *cpu_msg_que)
{
	cpu_msg_que->wptr = cpu_msg_que->rptr = 0;
}

typedef void (*cpu_msg_handler_t)(cpu_msg_req_t volatile *);

typedef struct _cbf_recv_t {
	cbf_t *cbf;
	void (*handler)(void);
} cbf_recv_t;

static inline void dtag_comt_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t dtag_comt_recv;

	dtag_comt_recv.cbf = cbf;
	dtag_comt_recv.handler = handler;
}

static inline void dtag_free_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t dtag_free_recv;

	dtag_free_recv.cbf = cbf;
	dtag_free_recv.handler = handler;
}

static inline void dref_inc_comt_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t dref_inc_comt_recv;

	dref_inc_comt_recv.cbf = cbf;
	dref_inc_comt_recv.handler = handler;
}

static inline void dref_dec_comt_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t dref_dec_comt_recv;

	dref_dec_comt_recv.cbf = cbf;
	dref_dec_comt_recv.handler = handler;
}

static inline void gc_src_data_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t gc_src_data_recv;

	gc_src_data_recv.cbf = cbf;
	gc_src_data_recv.handler = handler;
}

static inline void gc_res_free_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t gc_res_free_recv;

	gc_res_free_recv.cbf = cbf;
	gc_res_free_recv.handler = handler;
}

static inline void l2p_updt_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t l2p_updt_recv;

	l2p_updt_recv.cbf = cbf;
	l2p_updt_recv.handler = handler;
}

static inline void l2p_updt_done_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t l2p_updt_done_recv;

	l2p_updt_done_recv.cbf = cbf;
	l2p_updt_done_recv.handler = handler;
}

static inline void pbt_updt_recv_hook(cbf_t *cbf, void (*handler)(void))
{
	extern cbf_recv_t pbt_updt_recv;

	pbt_updt_recv.cbf = cbf;
	pbt_updt_recv.handler = handler;
}


/*!
 * @brief initialize cpu msg module
 */
void cpu_msg_init(void);

/*!
 * @brief cpu msg isr
 *
 * cpu msg comes when queue rptr and wptr mismatch
 *
 * @return    not used
 */
void cpu_msg_isr(void);

/*!
 * @brief register cpu msg handle function
 *
 * @param msg   cpu msg to be registered
 * @param func  handle function
 *
 * @return    not used
 */
void cpu_msg_register(u32 msg, cpu_msg_handler_t func);

/*!
 * @brief issue cpu message command
 *
 * @param rx_cpu_idx	receiving cpu id (zero based)
 * @param msg		cpu message
 * @param flags		flags
 * @param pl		transfer payload
 *
 * @return		not used
 */
void cpu_msg_issue(u32 rx_cpu_idx, u32 msg, u32 flags, u32 pl);

/*
* @brief debug show cpu msg info
*/
void  cpu_msg_show(u32 rx_cpu_idx);

/*!
 * @brief start of synchronous cpu msg command
 *
 * clear synchronous handshake
 *
 * @return    not used
 */
static inline void cpu_msg_sync_start(void)
{
	extern cpu_msg_t cpu_msg;
	cpu_msg.sync_done[CPU_MSG_TX_CPU_IDX] = 0;
}

/*!
 * @brief end of synchronous cpu msg command
 *
 * check synchronous handshake
 *
 * @return	not used
 */
static inline void cpu_msg_sync_end(void)
{
	extern cpu_msg_t cpu_msg;
	//while (!cpu_msg.sync_done[CPU_MSG_TX_CPU_IDX]);
	while (!cpu_msg.sync_done[CPU_MSG_TX_CPU_IDX]) {
		if (CPU_ID == MSG_CPU1_ID) {
			extern void bm_handle_rd_err(void);
			bm_handle_rd_err();
		}
    }
}

static inline void cpu_msg_sync_end2(void)
{
	extern cpu_msg_t cpu_msg;
	while (!cpu_msg.sync_done[CPU_MSG_TX_CPU_IDX]) {
		if (CPU_ID == MSG_CPU1_ID) {
			extern void bm_handle_rd_err(void);
			extern void bm_isr_com_free(void);
			bm_handle_rd_err();
			bm_isr_com_free();
		}
	}
}

/*!
 * @brief cpu msg sync operation done
 *
 * @param tx_cpu_idx	tx to be notified
 *
 * @return		not used
 */
void cpu_msg_sync_done(u8 tx_cpu_idx);

/*!
 * @brief software ipc poll
 *
 * @return	not used
 */
void sw_ipc_poll(void);

static inline void __dmb(void) { asm volatile ("dmb"); }
#endif
