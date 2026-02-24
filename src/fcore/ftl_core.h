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

/*! \file ftl_core.c
 * @brief ftl core module, receive write data entries and save it to nand
 *
 * \addtogroup fcore
 * \defgroup fcore
 * \ingroup fcore
 * @{
 * receive write data entries, assign page resource and update mapping
 * manage GC data moving, RAID, ..., etc
 */

#pragma once

#include "fc_export.h"
#include "scheduler.h"
#include "ftl_raid.h"
#include "die_que.h"
#include "bitops.h"

#define LECROY_IOL_TEST_ENABLE 0

#define WPL_H_MODE		true	///< wr_pl for horizontal mode
#define WPL_V_MODE		false	///< wr_pl for vertical mode

#define POWER_ON_OPEN 	(DISABLE)

#define BLOCK_NO_BAD    (0)
#define BLOCK_P0_BAD    (1)
#define BLOCK_P1_BAD    (2)
#define BLOCK_ALL_BAD   (15) //2P:3, 4P:15

// ERRHANDLE_GLIST
#define CLOSE_WL_EXPIRE      60
#if(PLP_SUPPORT == 0)
#define CLOSE_WL_EXPIRE_NRM  200
#endif
#define CLOSE_LAYER_EXPIRE   5
#define CLOSE_BLK_EXPIRE     144


// Move to fc_export.h for other cpu, ISU, LJ1-337, PgFalClsNotDone (1)
//#define FILL_TYPE_WL    1	
//#define FILL_TYPE_LAYER 2
//#define FILL_TYPE_BLK   3

//#define FTL_PBT_BLOCK_CNT           (QBT_BLK_CNT)
//#define FTL_MAX_PBT_CNT             (FTL_PBT_BLOCK_CNT*2)

#define MAX_FTL_CORE_CTX	(8 * NS_SUPPORT)	///< number of ftl core context, each type of write has one core context

enum
{
    FTL_BLK_TYPE_HOST = 0,
    FTL_BLK_TYPE_GC,
    FTL_BLK_TYPE_FTL,
    FTL_BLK_TYPE_MAX,
};

typedef struct _cur_wr_pl_t {
	pda_t *pda;		///< pointer to pda list
	lda_t *lda;		///< pointer to lda list
	bm_pl_t *pl;		///< pinter to pl list
	u8 nsid;
	u8 cnt;			///< cnt of data received
	u8 max_cnt;		///< max cnt of data receive
	u16 die;		///< operating die

	u8 type;
	u8 stripe_id;

	union {
		struct {
			u32 slc : 1;
			u32 raid : 1;
			u32 p2l : 1;
			u32 trim_info : 1;
#ifdef SKIP_MODE
			u32 one_pln : 2;
			u32 pln_write : 2; ///< true if command with (1P/2P/3P) write
#else
			u32 pln_st	: 2;
#endif
			u32 flush_blklist : 1;
            u32 dummy : 1;
			u32 ftl : 1;
            u32 parity : 1;
            u32 parity_mix : 1;
		} b;
		u32 all;
	} flags;
} cur_wr_pl_t;

/*!
 * @brief ftl core global flags
 */
typedef union _fc_flags_t {
	struct {
		u32 reseting : 1;
		u32 reset_ack : 1;
		u32 reset_ack_req : 1;
	} b;
	u32 all;
} fc_flags_t;

// FET, EHDbgMsg
typedef struct _ftl_core_t {
	pstream_t ps;				///< physical stream
	core_wl_t cwl;				///< ftl core word-line
	cur_wr_pl_t cur_wr_pl;			///< current write payload
	stripe_user_t su;			///< raid stripe user
	struct timer_list idle_timer;		///< idle timer
	u32 data_in;				///< data in record
	u32 last_data_in;			///< last record for idle check

	u32 sn;					///< serial number
	u32 wreq_cnt;				///< write request count
    u32 parity_wreq_cnt;
	union {
		u8 all;
		struct {
			u8 prog_err_blk_in 	: 1;	///< data in blocked by program error
			u8 prog_err_blk_updt: 1;	///< update blocked by program error
			u8 wreq_pend 		: 1;	///< get wreq fail
			u8 dummy_done 		: 1; 	// notice force close after EH. // ISU, EH_GC_not_done (1)
			u8 rd_open_close 	: 1;
		} b;
	} err;
} ftl_core_t;

/*! @brief ftl core namesapce, each namespace has serveral ftl_core */
typedef struct _ftl_core_ns_t {
	ftl_core_t *ftl_core[FTL_CORE_MAX];			///< ftl core object
	union {
		u32 all;
		struct {
			u32 clean : 1;				///< if table was clean
			u32 opening : 1;			///< if opening ns
		} b;
	} flags;

	u32 wreq_cnt;						///< write request count
	u32 parity_wreq_cnt;				///< raid parity write request count
	QSIMPLEQ_HEAD(_ftl_core_ctx_list_t, _ftl_core_ctx_t) pending;	///< pending flushing context
	ftl_flush_data_t *flushing_ctx;				///< outstanding flush context
	ftl_format_t *format_ctx;				///< outstanding format context
	QSIMPLEQ_HEAD(ns_pad_list_t, _ftl_core_ns_pad_t) ns_pad_list;	///< outstanding padding context to close a core wl
} ftl_core_ns_t;

extern spb_rt_flags_t *spb_rt_flags;

struct _ncl_w_req_t;

extern u32 pg_cnt_in_slc_spb;	///< pda number in SLC SPB
extern u32 pg_cnt_in_xlc_spb;	///< pda number in XLC SPB
extern u16 spb_total;		///< total SPB number
extern u32 width_nominal;	///< interleave, #CH * #DIE * #PLN
extern u8 width_in_dws;		///< hook to Dword
extern pool_t ftl_core_ctx_pool;		///< ftl core context for operations
extern u32 ftl_core_ctx_cnt;
extern fc_flags_t fc_flags;
extern u8 reset_ack_req_tx;
//extern u8 show_log;
extern u8 ftl_switch_block;
extern u8 host_flush_blklist_done[2];
extern ftl_flush_misc_t *blist_flush_ctx[2];
#if (RD_NO_OPEN_BLK == mENABLE)
extern bool host_idle;
#endif
extern u8 evt_force_pbt;
extern ftl_flush_tbl_t *tbl_flush_ctx;
extern bool tbl_flush_block;
//extern u8 otf_forcepbt;
extern volatile u32 shr_host_write_cnt;
extern volatile u32 shr_host_write_cnt_old;
extern bool first_usr_open;
extern volatile bool shr_qbtflag;
extern u8 irq_int_done;
extern bool pbt_query_ready;
extern volatile u8 power_on_update_epm_flag;

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
extern volatile u8 SLC_init;
extern volatile u32 max_usr_blk_sn;
extern bool SLC_call_force_gc;
#endif

typedef enum{
    pad_idel,
    pad_start,
    pad_send,
    pad_end,
}pad_status;

// ISU, LJ1-337, PgFalClsNotDone (1)
typedef struct {
	u8 wl_filled;
	u8 layer_filled;
	u8 blk_filled;
	u8 blk_filling;
	u16 wl_timer;			///< idle 1 min close open wl
	u16 layer_timer;		///< idle 5 min close open layer
	u16 blk_timer;			///< idle 12 hour close open block
	u32 wptr;
	spb_id_t spb_id;		///< spb id
}idle_chk_t;

#define MAX_SPBPAD_CNT 6  // max support to 32, since using_bmp is 32bit
typedef struct {
	idle_chk_t chk[FTL_CORE_MAX];
    ftl_spb_pad_t spb_pad[MAX_SPBPAD_CNT]; //[3][2]
    u32 using_bmp;
    u8 free_cnt;
    u8 evt_fill_dummy_task;
}ftl_core_open_blk_handle_t;


/*!
 * @brief initialization of ftl core
 *
 * @return	not used
 */
void ftl_core_init(void);

/*!
 * @brief submit ncl write request to die queue
 *
 * @note if request was not setup well, it won't issue to die queue, and return NULL
 *
 * @param wr_pl		current write payload
 * @param caller	caller of ncl write request
 * @param cmpl		callback of ncl write request, can be NULL
 *
 * @return		return NULL if request was not issued
 */
ncl_w_req_t *ftl_core_submit(cur_wr_pl_t *wr_pl, void *caller, ncl_req_cmpl_t cmpl);

/*!
 * @brief get DU count of next write command
 *
 * @param nsid		namespace id
 * @param type		type GC or HOST
 *
 * @return		du count of next write command
 */
#ifdef SKIP_MODE
u32 ftl_core_next_write(u32 nsid, u32 type);
#else
u32 ftl_core_next_write(u32 nsid, u32 type);
#endif
/*!
 * @brief ftl core start, start to reserve physocal stream resource
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_core_start(u32 nsid);

/*!
 * @brief ftl core open done, remove clean and opening flags
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_core_open_done(u32 nsid);

/*!
 * @brief make ftl core namespace clean
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_core_clean(u32 nsid);

/*!
 * @brief get core WL of ftl core
 *
 * @param nsid		namespace id to be got
 * @param type		type, GC or NORMAL
 *
 * @return		core WL object
 */
#if 0
cur_wr_pl_t * ftl_core_get_wpl(u32 nsid, u32 type);
#endif
void ftl_core_host_data_in(u32 nsid, u32 type, bool operation);

cur_wr_pl_t * ftl_core_get_wpl_mix_mode(u32 nsid, u32 type, bool wpl_mode, bool start_new_blk, void* caller, u8 *reason);
void ftl_core_gc_data_in_update(void);
bool chk_l2p_updt_que_cnt_full(void);
bool ftl_core_gc_bypass(u32 nsid);

/*!
 * @brief resume ftl core flush context, continue to flush write data entries
 *
 * @param nsid		namespace id
 */
void ftl_core_flush_resume(u32 nsid);
bool ftl_core_flush_blklist_need_resume(void);

void ftl_core_set_spb_close(u16 spb_id, u8 core_type, u8 nsid);

/*!
 * @brief insert new data entries into ftl core from dtag commt handle
 *
 * @param lda		rdtag to lda array
 * @param ent		dtag commit entry
 * @param cnt		count to be insert
 *
 * @return		count inserted
 */
u32 ftl_core_host_ins_dcomt(u32 *lda, dtag_comt_entry_t *ent, u32 cnt);

static inline void ftl_core_set_spb_type(u32 spb_id, u32 slc)
{
	extern spb_rt_flags_t *spb_rt_flags;
	spb_rt_flags[spb_id].b.type = slc;
	read_cnt_reset(spb_id);
#if defined(DUAL_BE)
	// to reset read counter
	cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_SPB_RD_CNT_UPDT_ACK, 0, spb_id);
#endif
}

/*!
 * @brief default callback when ncl write request done
 *
 * @param req	ncl request
 *
 * @return	not used
 */
void ftl_core_done(ncl_w_req_t *req);

/*!
 * @brief ftl core gc module initialization
 *
 * @return	not used
 */
void ftl_core_gc_init(void);

/*!
 * @brief resume all pend GC data entries
 *
 * @return	not used
 */
void ftl_core_gc_pend_out(void);
void ftl_core_set_wreq_pend(u32 nsid, u32 type, bool value);

/*!
 * @brief if namespace is clean, open it
 *
 * @param nsid		namespace id
 *
 * @return		return true if namespace is opened
 */
bool ftl_core_open(u32 nsid);

/*!
 * @brief if normal core data in should be bypassed
 *
 * @param nsid		namespace id
 *
 * @return		return true if bypass
 */
bool ftl_core_nrm_bypass(u32 nsid);

/*!
 * @brief if namespace is padding
 *
 * @param nsid		namespace id
 *
 * @return		return true if namespace is padding
 */
bool ftl_core_ns_padding(u32 nsid);

/*!
 * @brief ftl core get stripe user
 *
 * @param nsid		namespace id
 * @param type		ftl core type
 *
 * @return		return stripe user object
 */
stripe_user_t *ftl_core_get_su(u32 nsid, u32 type);

/*!
 * @brief padding all open page in current open die to issue this die command
 *
 * @param nsid		namespace id
 * @param type		type, GC or NORMAL
 * @param cmpl		callback function when request completed
 *
 * @return		padding cnt
 */
u16 ftl_core_close_open_die(u32 nsid, u32 type, ncl_req_cmpl_t cmpl);

/*!
 * @brief register all ftl core ipc message handler
 *
 * @return	not used
 */
void ftl_core_ipc_init(void);

/*!
 * @brief register all ftl core swap addr handler
 *
 * @return	not used
 */
void pmu_swap_init(void);

/*!
 * @brief release gc valid pda rsp entry
 *
 * @param resume		resume swap memory
 *
 * @return		not used
 */
void pmu_swap_file_restore(bool resume);

/*!
 * @brief save all ftl core swap memory to ps memory or nand
 *
 * @return	return true if save success
 */
bool pmu_swap_file_save(void);

/*!
 * @brief register other cpu memory to swap table
 *
 * @return	not used
 */
void pmu_swap_register_other_cpu_memory(void);

/*!
 * @brief release gc valid pda rsp entry
 *
 * @param req		ncl write request
 *
 * @return		not used
 */
extern void ftl_core_gc_free_rsp(ncl_w_req_t *req);

/*!
 * @brief callback function of gc ncl write request
 *
 * @param req		ncl request
 *
 * @return		not used
 */
extern void ftl_core_gc_wr_done(ncl_w_req_t *req);

/*!
 * @brief get core operation context
 *
 * @return	return core context or NULL
 */
static inline ftl_core_ctx_t *ftl_core_get_ctx(void)
{
	sys_assert(ftl_core_ctx_cnt > 0);
	ftl_core_ctx_cnt--;
	return (ftl_core_ctx_t *) pool_get_ex(&ftl_core_ctx_pool);
}

/*!
 * @brief put core operation context
 *
 * @param ctx	core context to be put
 *
 * @return	not used
 */
static inline void ftl_core_put_ctx(ftl_core_ctx_t *ctx)
{
	pool_put_ex(&ftl_core_ctx_pool, (void *)ctx);
	ftl_core_ctx_cnt++;
	sys_assert(ftl_core_ctx_cnt <= MAX_FTL_CORE_CTX);
}

/*!
 * @brief error handle done function
 *
 * @param req		pointer to req triggered handle done
 * @param spb_id	id of spb handled done
 *
 * @return	not used
 */
void ftl_core_err_handle_done(ncl_w_req_t *req, u32 spb_id);

/*!
 * @brief error handle done function
 *
 * @param p0		not used
 * @param ns_bmp	bitmap to check
 * @param p2		not used
 *
 * @return		not used
 */
void ftl_core_ready(u32 p0, u32 ns_bmp, u32 p2);

void fcore_idle_flush();

void ftl_core_qbt_alloc(ns_start_t *ctx);
void FTL_PBT_Update(u32 req_cnt);
void ftl_clear_pbt_cover_usr_cnt(void);
u32 ftl_get_pbt_cover_usr_cnt(void);
u8 ftl_get_pbt_cover_percent(void);
void ftl_pbt_cover_usr(void);
bool ftl_is_pbt_filldummy(void);
void ftl_clear_pbt_filldummy(void);
void ftl_set_pbt_filldummy_done(void);
void ftl_set_pbt_halt(bool mode);
void fcore_spb_close_fill_dummy(ftl_spb_pad_t *spb_pad);
void plp_debug_fill_done(void);
void plp_force_flush_done(void);
void fplp_trigger_start(void);
void fcore_fill_dummy(u8 core_type, u32 end_type,u8 fill_type, void* cmpl, void* caller);
void ftl_set_force_dump_pbt(u32 param, u32 force_start, u32 mode);
bool ftl_core_seg_flush(ftl_flush_tbl_t *ctx);
void ftl_core_format_close_open(ftl_format_t *format);
void ftl_pbt_supply_init(void);
void ftl_core_return_blklist_ctx(u16 type);
void jump_to_last_wl(pstream_t *ps);  
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
u32  get_plp_lda_dtag_w_idx(void); 
#endif
void vc_print(void); 
void evt_wr_pout_issue(u32 param, u32 req_addr, u32 param2);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
void ftl_slc_pstream_enable(void);
void slc_erase_block(void);
void jump_to_next_wl(pstream_t *ps , u16 skip_wl_num);
void slc_gc_open_pad_done(ftl_core_ctx_t *ctx);
#endif
/*!
 * @brief save the current location and pstream information of PBT during fw update
 *
 * @return 	not used
 */
void evt_fcore_save_pbt_param(u32 r0, u32 r1, u32 r2);
/*!
 * @brief restore the PBT information before fw update, and continue to write this PBT
 *
 * @return 	not used
 */
void ftl_core_restore_pbt_param(void);

/*! @} */

/*
 * @bried used for fill dummy when req is not enough
 */
void fcore_fill_dummy_task();
#ifdef ERRHANDLE_GLIST
void fcore_fill_dummy(u8 core_type, u32 end_type,u8 fill_type, void* cmpl, void* caller);
void fcore_fill_dummy_done_call_EH_Task(ftl_core_ctx_t *ctx);
#endif
#define CPU_OFF_FOR_OTP
