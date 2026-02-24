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

/*!
 * \file btn_expoert.h
 * @brief export header of btn
 * \addtogroup btn
 * \defgroup btn_export
 * \ingroup export
 * @{
 */

#pragma once
#include "dtag.h"
#include "bf_mgr.h"

#ifndef CPU_DTAG
#define CPU_DTAG	1
#endif

#if defined(RAWDISK) && defined(MPC)	// MPC RAWDISK
#define BTN_CORE_CPU 			1
#define BTN_FREE_WRITE_CPU		2
#define BTN_COM_FREE_CPU		2
#define BTN_RCMD_CPU			1
#define BTN_HOST_READ_CPU		1
#define BTN_WCMD_CPU 			1
#define BTN_DATA_IN_CPU			1
#elif defined(RDISK)			// RDISK
#define BTN_CORE_CPU 			1
#define BTN_FREE_WRITE_CPU		CPU_DTAG
#define BTN_COM_FREE_CPU		BTN_CORE_CPU
#define BTN_RCMD_CPU			1
#define BTN_HOST_READ_CPU		1
#define BTN_WCMD_CPU 			CPU_DTAG
#define BTN_DATA_IN_CPU			1
#else					// DEFAULT single CPU
#define BTN_CORE_CPU 			1
#define BTN_FREE_WRITE_CPU		BTN_CORE_CPU
#define BTN_COM_FREE_CPU		BTN_CORE_CPU
#define BTN_RCMD_CPU 			BTN_CORE_CPU
#define BTN_HOST_READ_CPU		1
#define BTN_WCMD_CPU 			BTN_CORE_CPU
#define BTN_DATA_IN_CPU 		BTN_CORE_CPU
#endif

#define BTN_WR_DAT_RLS_CPU		BTN_DATA_IN_CPU	///< force WR_DAT_RLS == DATA_IN, avoid IPC when push release

#if CPU_ID == BTN_WCMD_CPU || CPU_ID == BTN_HOST_READ_CPU
#define BTN_CMD_CPU
#endif

#define BTN_R_CMD_CNT 			256
#define BTN_W_CMD_CNT 			128  /* WRITE CMD could be less than BTN_R_CMD_CNT */

#define BTN_CMD_CNT			(BTN_R_CMD_CNT + BTN_W_CMD_CNT)

#if !defined(RDISK)
#define BTN_INT_CNT 			BTN_CMD_CNT
#else
#define UCB_OFF  			BTN_CMD_CNT
#define UCB_CNT 			256
#define UA_OFF				(UCB_OFF + UCB_CNT)
#define WUCC_OFF			(UA_OFF + 1)
#define RA_OFF				(WUCC_OFF + 1)
#define BTN_INT_CNT 			(RA_OFF + 1)
#endif

/*!
 * @brief BTN queue header registers
 */
typedef struct {
	union {
		u32 all;
		struct {
			u32 dbase:24;	///< tcm updated queue base address
			u32 max_sz:8;	///< queue size
		} b;
	} entry_dbase;			///< entry queue base

	union entry_pnter {
		u32 all;
		struct {
			u32 wptr:8;	///< tcm updated queue write pointer
			u32 rptr:8;	///< tcm updated queue read pointer
			u32 rsvd:16;
		} b;
	} entry_pnter;			///< entry queue pointer

	union {
		u32 all;
		struct {
			u32 dbase:24;	///< tcm updated queue pointer base address
			u32 rsvd:7;
			u32 updt_en:1;	///< enable auto update from linked list to queue
		} b;
	} pnter_dbase;			///< entry queue pointer base

	void *base;
	u32 mmio;
} hdr_reg_t;

/*!
 * @brief btn IO command data structure, filled by BTN
 */
typedef struct btn_cmd {
	union {
		u32 all;
		struct {
			/*
			 * BTN has up to 1024 Command-table indexed by NVM_CMD_ID
			 * BTAG/SLBA/NSID/RW... [0:57]
			 */
			u32 nvm_cmd_id : 10; /* cmd_proc cmd slot though 1024 NVM commands */
			u32 rsvd_10_15 : 6;
#define NVM_WRITE 0b0000 /* normal write       */
#define OC_WRITE  0b0010 /* open channel write */
#define OC_COPY   0b1010 /* open channel copy  */
#define IO_MGR    0b1100 /* NVM I/O management read */

#define NVM_READ  0b0001 /* normal read        */
#define OC_READ   0b0011 /* open channel read  */
#define PRI_READ  0b1001 /* high priority read */
			u32 cmd_type   : 4;
			u32 rsvd_20_23 : 4;
//#define BCMD_DW0_VF_NUM_MSK (0x3F000000)
//#define BCMD_DW0_VF_NUM_SHF (24)
			u32 vf_num     : 6;
			u32 rsvd_30_31 : 2;
		} b;
	} dw0;
	union {
		u32 all;
		struct {
			u32 cmd_sqid    : 8;
			u32 rsvd_40_43  : 4;
#define HOST_SECTOR_TYPE_512B  0b000
#define HOST_SECTOR_TYPE_520B  0b001
#define HOST_SECTOR_TYPE_528B  0b010
#define HOST_SECTOR_TYPE_4096B 0b100
#define HOST_SECTOR_TYPE_4104B 0b101
#define HOST_SECTOR_TYPE_4112B 0b110
			u32 host_sector_type : 3;

			u32 rsvd_47     : 1;
			u32 ns_id       : 7;
			u32 rsvd_55     : 1;
			u32 deac_write_zero: 1;
			u32 write_zero  : 1;
			u32 compare     : 1;
			u32 fua         : 1;
			u32 limited_retry  : 1;
			u32 port        : 1;
//#define BCMD_DW1_PF_MSK (0x40000000)
//#define BCMD_DW1_PF_SHF (30)
			u32 pf          : 1;
			u32 rsvd_63     : 1;
		} b;
	} dw1;
	union {
		u32 all;
		struct {
			u32 slba_31_0;
		} b;
	} dw2;
	union {
		u32 all;
		struct {
//#define BCMD_DW3_SLBA_SHF (0x0)
//#define BCMD_DW3_SLBA_MSK (0xF)
			u32 slba_35_32:4;
			u32 stream_id:4;
			u32 dsm:8;
//#define BCMD_DW3_NLBA_MSK (0xFFFF0000)
//#define BCMD_DW3_NLBA_SHF (16)
			u32 xfer_lba_num:16;  /* XXX: change to NLB */
		} b;
	} dw3;
} btn_cmd_t;

/*!
 * @brief btn non-IO command data structure, filled by BTN
 */
typedef struct _btn_io_mgr_cmd_t {
	union {
		u32 all;
		struct {
			u32 cmd_op_type : 8;
			u32 rsvd_8_15 : 8;
			u32 cmd_type   : 4;	///< must be 4'b1100
			u32 rsvd_20_23 : 4;
			u32 vf_num     : 6;
			u32 rsvd_30_31 : 2;
		} b;
	} dw0;
	union {
		u32 all;
		struct {
			u32 cmd_sqid : 8;

			u32 rsvd_40_43 : 4;
			u32 host_sector_type : 3;
			u32 rsvd_47 : 1;

			u32 ns_id : 7;
			u32 rsvd_55_60 : 6;
			u32 port : 1;
			u32 pf : 1;
			u32 rsvd_63 : 1;
		} b;
	} dw1;
	union {
		u32 all;
		struct {
			u32 cmd_cid : 16;
			u32 rsvd_80_95 : 16;
		} b;
	} dw2;
	union {
		u32 all;
	} dw3;
} btn_io_mgr_cmd_t;

typedef union {
	struct {
		u32 pend : 1;
		u32 err : 1;
		u32 wr_err : 1;		//< write error flag
		u32 bcmd_init : 1;	//< btn cmd init flag, workaround for btn data in and cmd in async event.
		u32 bcmd_rls : 1;	//< which mean this command could be released or not
		u32 bcmd_abort: 1;//use for fw abort bcmd

		u32 tcg_wr_abrt : 1;
		//u32 bcmd_ecc_hit : 1;  //rcmd hit ecct
		//u32 rsvd : 25;
		u32 fua : 1;
		u32 rsvd : 24;
	} b;
	u32 all;
} btn_cmd_ex_flag_t;

/*!
 * @brief extension fields of btn command, filled by software
 */
typedef struct _btn_cmd_ex_t {
	struct list_head entry;		///< list entry, _otf_bcmd or _pending_bcmd
	u32 start;
	u16 ndu;
	u16 next_btag;
	union {
		short du_xfer_left;
		u16 read_ofst;
	};
	btn_cmd_ex_flag_t flags;
} btn_cmd_ex_t;

typedef struct _btn_smart_io_t {
	volatile u32 running_cmd;		///< write or read runing commands
	volatile u64 cmd_recv_cnt;		///< write or read command received count in btn
	volatile u64 host_du_cnt;		///< host write or read du count in btn
} btn_smart_io_t;

/*!
 * @brief btn error payload callback function type
 */
typedef void (*btn_callback_t)(bm_pl_t *pl);

extern btn_cmd_t _btn_r_cmds[];		///< btn command array
extern btn_cmd_t _btn_w_cmds[];		///< btn command array
extern btn_cmd_ex_t _btn_cmds_ex[];	///< btn command extension array

/*!
 * @brief btn command handler type
 *
 * @param bcmd	btn command to be handled
 * @param btag	btag of bcmd
 *
 * @return	not used
 */
typedef void (*btn_cmd_handler_t)(btn_cmd_t *bcmd, int btag);

/*!
 * @brief btn fast command handler type, use for 4K read command
 *
 * @param bcmd	btn command to be handled
 * @param btag	btag of bcmd
 *
 * @return	return true if handled
 */
typedef bool (*btn_fast_cmd_handler_t)(btn_cmd_t *bcmd, int btag);

typedef enum {
	RLS_T_WRITE_CQ = 0x0,
	RLS_T_WRITE = 0x1,
	RLS_T_WRITE_ABT = 0x2,
	RLS_T_WRITE_ABT_RSP = 0x3,
	RLS_T_READ_CQ = 0x8,
	RLS_T_READ_ERR = 0x9,
	RLS_T_READ_ABT = 0xA,

	RLS_T_READ_ABT_ACK = 0xB,
	RLS_T_READ_ABT_NACK = 0xC,
	RLS_T_READ_FW_ABORT = 0xD,
	RLS_T_CMD_RLS_ERR = 0xF
} btn_nvm_rls_type_t;

/*!
 * @brief function array to handle error data entries
 */
typedef struct _btn_callbacks_t {
	btn_callback_t hst_strm_rd_err;	///< for host read
	btn_callback_t write_err;	///< for write error
} btn_callbacks_t;

/*!
 * @brief feed read dtag/s to drain out streaming rd xfer from NCB
 *
 * @param	not used
 *
 * @return	not used
 */
extern void btn_rd_dtag_feed(void);

/*!
 * @brief for IO_MGR command to release btn command
 *
 * @param cmd	btn command to be released
 *
 * @return	not used
 */
extern void btn_iom_cmd_rels(btn_cmd_t *cmd);

/*!
 * @brief push btn command release request to release btn command
 *
 * @param cmd		btn command to be released
 * @param rls_type	release type
 *
 * @return		always return 0
 */
extern void btn_cmd_rels_push(btn_cmd_t *bcmd, btn_nvm_rls_type_t rls_type);

/*!
 * @brief abort write data entries of error btn write command
 *
 * @param btag		btag to be aborted
 *
 * @return		not used
 */
extern void btn_write_de_abort(u16 btag);

/*!
 * @brief release fw command btag
 *
 * @return		not used
 */
extern void fw_btag_release(u32 btag);

/*!
 * @brief push btn command release request to release btn command
 *
 * @param cmds		btn command pointer list
 * @param count		length of cmds
 *
 * @return		not used
 */
extern void btn_cmd_rels_push_bulk(btn_cmd_t **cmds, int count);

static inline btn_cmd_t *btag2bcmd(int btag)
{
#if BTN_HOST_READ_CPU == CPU_ID
	return (btag >= BTN_R_CMD_CNT) ? &_btn_w_cmds[btag - BTN_R_CMD_CNT] : &_btn_r_cmds[btag];
#else
	sys_assert(btag >= BTN_R_CMD_CNT);
	return &_btn_w_cmds[btag - BTN_R_CMD_CNT];
#endif
}

static inline int bcmd2btag(btn_cmd_t *bcmd, bool wr)
{
#if BTN_HOST_READ_CPU == CPU_ID
	return (wr) ? (int) (bcmd - _btn_w_cmds) + BTN_R_CMD_CNT : (int) (bcmd - _btn_r_cmds);
#else
	sys_assert(wr == true);
	return (int) (bcmd - _btn_w_cmds) + BTN_R_CMD_CNT;
#endif
}

static inline btn_cmd_ex_t *btag2bcmd_ex(int btag)
{
	return &_btn_cmds_ex[btag];
}

static inline int bcmd_ex2btag(btn_cmd_ex_t *bcmd_ex)
{
	return bcmd_ex - _btn_cmds_ex;
}

static inline u64 bcmd_get_slba(btn_cmd_t *bcmd)
{
	return ((u64)bcmd->dw3.b.slba_35_32 << 32) | (bcmd->dw2.all);
}

static inline bool is_fua_bcmd(btn_cmd_t *bcmd)
{
	return !!bcmd->dw1.b.fua;
}

static inline bool is_wzero_bcmd(btn_cmd_t *bcmd)
{
	return !!bcmd->dw1.b.write_zero;
}

/*!
 * @brief reset nvme read/write/compare command count from btn register
 *
 * @return		none
 */
extern void btn_rst_nvm_cmd_cnt(void);

/*!
 * @brief get read command count from btn register
 *
 * @return		none
 */
extern u32 btn_get_rd_cmd_cnt(void);

/*!
 * @brief get write command count from btn register
 *
 * @return		none
 */
extern u32 btn_get_wr_cmd_cnt(void);

/*!
 * @brief get write command count from btn register
 *
 * @return		none
 */
extern u32 btn_get_cp_cmd_cnt(void);

/*!
 * @brief hook btn command handler
 *
 * @param handler	normal btn command handler
 * @param fast_handler	4K read btn command handler, can be NULL
 *
 * @return		not used
 */
extern void btn_cmd_hook(btn_cmd_handler_t handler, btn_fast_cmd_handler_t fast_handler);

/*!
 * @brief API to check if any BTN on-the-fly command timeout
 *
 * @param data		not used
 *
 * @return		not used
 */
extern void btn_otf_cmd_chk(void *data);

/*!
 * @brief inform BTN reset event is pending
 *
 * drain all outstanding IO and prepare for NVME reset
 *
 * @return	not used
 */
extern void btn_reset_pending(void);

/*!
 * @brief inform BTN handle incoming command
 *
 * drain all outstanding IO and prepare for NVME reset
 *
 * @return	not used
 */
extern void btn_handle_incoming_cmd(void);

/*!
 * @brief check btn io queue
 *
 * @return
 */
bool btn_io_queue_debug(void);

/*!
 * @brief abort all outstanding btn commands
 *
 * @return	not used
 */
extern void btn_abort_all(void);

/*!
 * @brief resume BTN from reset, it should be caller after BTN reset
 *
 * re-initialize all queue, and register
 *
 * @return	not used
 */
extern void btn_reset_resume(void);

/*!
 * @brief disable write data entry interrupt
 *
 * @return	not used
 */
extern void btn_de_wr_disable(void);

/*!
 * @brief enable write data entry interrupt
 *
 * @return	not used
 */
extern void btn_de_wr_enable(void);
/*!
 * @brief disable read data entry interrupt
 *
 * @return	not used
 */
extern void btn_de_rd_disable(void);

/*!
 * @brief enable read data entry interrupt
 *
 * @return	not used
 */
extern void btn_de_rd_enable(void);


/*!
 * @brief check if btn write/compare command are all completed
 *
 * @return	return true if all competed
 */
extern bool btn_wr_cmd_idle(void);
/*!
 * @brief check if btn read command are all completed
 *
 * @return	return true if all competed
 */
extern bool btn_rd_cmd_idle(void);

/*!
 * @brief api to let read/write command in
 *
 * @return	not used
 */
void btn_rw_cmd_in(void);

/*!
 * @brief get running command in btn, in MPC, counter is in share, it is not accurate
 *
 * @note if need accurate number, this api need to be improved
 *
 * @return	return number of btn running command
 */
u32 get_btn_running_cmds(void);

/*!
 * @brief api to get write command smart IO
 *
 * @param smart_io	smart io, return object
 *
 * @return		not used
 */
void btn_get_w_smart_io(volatile btn_smart_io_t *smart_io);

/*!
 * @brief api to get read command smart IO
 *
 * @param smart_io	smart io, return object
 *
 * @return		not used
 */
void btn_get_r_smart_io(volatile btn_smart_io_t *smart_io);

/*!
 * @brief btn core api to register error handling function callback, must be called by dispatcher
 *
 * @param callbacks	callback when write/read error was happened
 *
 * @return		not used
 */
void btn_callback_register(btn_callbacks_t *callbacks);

/*!
 * @brief btn error dtag return from outside
 *
 * @param dtag		error dtag, will release to write to _btn_err_cbs.write_err
 *
 * @return		not used
 */
void btn_err_dtag_ret(dtag_t dtag);

/*!
 * @brief feed dtag to auto release, make stream read command completed
 *
 * @return		not used
 */
u32 btn_feed_rd_dtag(void);

/*!
 * @brief pop all read data entry link list
 *
 * @return		not used
 */
void bm_pop_all_rd_entry_list(void);

/*!
 * @brief check btn io queue status
 *
 * @param cmdq		also check cmdq
 *
 * @return		true for idle
 */
bool btn_io_queue_idle(bool cmdq);

/*!
 * @brief abort unhandled write data entry
 *
 * @param btag	aborted btag
 *
 * @return		not used
 */
extern void btn_data_abort(u32 btag);
/*!
 * @brief dump btn wd info
 *
 * @return		not used
 */
extern void btn_wd_grp0_dump(void);
/*!
 * @brief check btn write is idle state
 *
 * @return	false is idle
 */
bool btn_wr_io_idle(void);

/*!
 * @brief for performance, let caller to operate free write queue
 *
 * @return	return current write pointer of free write queue
 */
u32 btn_free_wr_get_wptr(void);

/*!
 * @brief for performance, let caller to operate free write queue
 *
 * @return	return current read pointer of free write queue
 */
u32 btn_free_wr_get_rptr(void);

/*!
 * @brief for performance, let caller to operate free write queue
 *
 * @param wptr	update new write pointer from caller
 *
 * @return	not used
 */
void btn_free_wr_set_wptr(u32 wptr);

/*!
 * @brief get runing comand count
 *
 * @return	return running comand count
 */
u32 get_btn_rd_otf_cnt(void);
/*! @} */
