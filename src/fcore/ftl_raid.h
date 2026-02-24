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
/*! \file ftl_raid.h
 * @brief define raid behavior in ftl core, included parity flush, recover, and raid buffer switch
 *
 * \addtogroup fcore
 * \defgroup raid
 * \ingroup fcore
 *
 */
#pragma once

#include "mpc.h"
#include "fsm.h"
#include "types.h"
#include "queue.h"
#include "bf_mgr.h"
#include "nand.h"
#include "ftltype.h"
#include "die_que.h"
#include "pstream.h"
#include "die_que.h"
#include "ncl_cmd.h"
#include "ncl_raid.h"
#include "fc_export.h"
#include "rainier_soc.h"

#define DU_CNT_PER_RAID_XOR_CMD 32


#define RAID_CORRECT_BANK_ID (RAID_BANK_NUM - 1)
#define RAID_CORRECT_RAID_ID (TTL_RAID_ID - 1)

enum {
	RAID_ID_ST_FREE,	///< not allocated
	RAID_ID_ST_IDLE,	///< allocated but have not been used
	RAID_ID_ST_BUSY,	///< xored or xoring
	RAID_ID_ST_SUSPENDING,	///< stripe is suspending
	RAID_ID_ST_SUSPENED,	///< stripe has suspended
	RAID_ID_ST_RESUMING,	///< stripe is resuming
	RAID_ID_ST_RESUMED,	///< stripe has resumed
	RAID_ID_ST_MAX,
};

typedef struct {
	u16 state : 4;		///< state
	u16 nsid : 2;		///< namespace id
	u16 type : 2;		///< payload type
	u16 bank_id : 2;	///< bank id
	u16 stripe_id : 6;	///< stripe id
} raid_id_info_t;

typedef struct {
	u64 id_bmp; /// < raid id bitmap used for allocation, 0 - idle, 1 - busy
	raid_id_info_t id_info[MAX_WR_RAID_ID]; /// < detailed info of each raid id
	dtag_t dtag[MAX_CWL_STRIPE * XLC * DU_CNT_PER_PAGE];
    u32 pad_meta_idx[MAX_CWL_STRIPE];
} raid_id_mgr_t;
#define DTAG_RES_CNT MAX_CWL_STRIPE
typedef struct {
	u32 id_bmp; /// < raid id bitmap used for allocation, 0 - idle, 1 - busy
	dtag_t dtag[3 * XLC * DU_CNT_PER_PAGE * DTAG_RES_CNT];
    ncl_w_req_t * req[DTAG_RES_CNT];
    u32 otf[DTAG_RES_CNT];
} pbt_dtag_mgr_t;

typedef struct {
	u8 ttl_id_cnt[RAID_BANK_NUM];		///< using raid id count of each bank
	u8 free_id_cnt[RAID_BANK_NUM];		///< available count of each bank
	u8 switchable_id_cnt[RAID_BANK_NUM];	///< number of using id available to switch
} raid_bank_mgr_t;

enum {
	STRIPE_ST_FREE,		///< not allocated
	STRIPE_ST_IDLE,		///< allocated but have not been used
	STRIPE_ST_BUSY,		///< allocated and used
	STRIPE_ST_SUSPENDING,	///< stripe is suspending
	STRIPE_ST_SUSPEND,	///< stripe has suspended
	STRIPE_ST_RESUMING,	///< stripe is resuming
	STRIPE_ST_FINISHING,	///< stripe is finishing
	STRIPE_ST_MAX,
};

typedef struct _ncl_raid_cmd_t {
	struct ncl_cmd_t ncl_cmd;
	struct ncl_cmd_t ncl_cmd_pout;
	struct info_param_t info_list[max((MAX_PLANE - 1), DU_CNT_PER_PAGE) * XLC];
	pda_t pda_list[max((MAX_PLANE - 1), DU_CNT_PER_PAGE) * XLC];
	bm_pl_t bm_pl_list[(MAX_PLANE - 1) * DU_CNT_PER_PAGE * XLC];
} ncl_raid_cmd_t;

typedef struct _cwl_stripe_t {
	QSIMPLEQ_ENTRY(_cwl_stripe_t) link;	///< link entry
	u8 raid_id[XLC][1];		///< raid id of each page
	u8 bank_id[XLC][1];		///< bank id of each page
	u8 suspend_cnt[RAID_BANK_NUM];		///< suspend id count of the stripe on each bank
	u8 nsid;				///< namespace id
	u8 type;				///< type
	u8 stripe_id;				///< stripe id
	u8 page_cnt;				///< page count
	u8 state;				///< state
	u8 fly_wr;				///< ongoing write count
	u8 done_wr;				///< program done write count
	u16 fpage;				///< nand physical page
	u16 spb_id;				///< spb id
	ncl_w_req_t *ncl_req;			///< ncl req to program parity
	dtag_t parity_buffer_dtag[XLC * DU_CNT_PER_PAGE * 1];				///< in wait queue
		
	u8 wait : 1;				///< in wait queue
    u8 parity_mix : 1;	///< good pn cnt of parity die > 1, this die has user data
    u8 pout_send : 1;
    u8 xor_done : 1;
    u8 xor_only_send : 1;
    u8 parity_die_ready : 1;
    u8 parity_user_done : 1; // used only parity_die_2_req == 1
    u8 pout_done : 1;
	ncl_w_req_t *parity_req;
    ncl_raid_cmd_t ncl_raid_cmd;
} cwl_stripe_t;

typedef QSIMPLEQ_HEAD(_stripe_que_t, _cwl_stripe_t) stripe_que_t;
typedef QSIMPLEQ_HEAD(_raid_sched_que_t, _ncl_req_t) raid_sched_que_t;

typedef struct _stripe_user_t {
	cwl_stripe_t *active;		///< constructing stripe
	stripe_que_t wait_que;		///< stripes waiting for program parity
	stripe_que_t cmpl_que;		///< stripes waiting for completion
} stripe_user_t;

enum {
	RAID_SCHED_ACT_EXEC,		///< to execute
	RAID_SCHED_ACT_PEND,		///< to pend
	RAID_SCHED_ACT_SUSPEND,		///< to handle suspend
	RAID_SCHED_ACT_RESUME,		///< to resume suspend
	RAID_SCHED_ACT_MAX,
};

typedef struct _raid_sched_mgr_t {
	raid_sched_que_t pend_que;		///< pend queue
	u8 pend_cnt[TOTAL_NS][FTL_CORE_MAX];	///< total pend count
	struct ncl_cmd_t *ncl_cmd;		///< pointer to raid schedule ncl command
	bool busy;				///< busy
} raid_sched_mgr_t;

typedef QSIMPLEQ_HEAD(_rc_pend_que, _rc_req_t) rc_pend_que;

typedef struct _raid_correct_t {
	fsm_ctx_t fsm; 				///< raid correct state machine
	rc_pend_que pend_que;			///< reqs waiting for RAID correction
	rc_req_t *cur_req; 			///< RAID correcting req
	pda_t uc_pda;				///< uncorrected pda

	bm_pl_t *bm_pl;				///< pointer to be playload
	struct info_param_t *info;		///< pointer to info buffer
	struct ncl_cmd_t *ncl_cmd;		///< pointer to ncl command
	pda_t pda[DU_CNT_PER_RAID_XOR_CMD];		///< pda list
	dtag_t dtag;				///< dtag for corrected data

	u16 plane_CPU2;					///< operating lun
	u16 plane_CPU4;					///< operating lun

	u8 pda_idx;				///< ftl req pda index
	u8 fail_cnt;				///< fail count
	u8 evt_open; 				///< event id for open stripe handle
	u8 evt_trig; 				///< event id for trigger raid correct

	u8 wait_retry_cnt;
	u16 backup_ncl_cmd_status;

	u16 ttl_fail_cnt;			///< total fail count
	u16 ttl_success_cnt;			///< total success count
	u16 ttl_pending_cnt;			///< total success count

	union {
		struct {
			u32 busy : 1;		///< correcting
			u32 open : 1;		///< stripe open
			u32 fail : 1;		///< fail
			u32 dtag_got : 1;	///< dtag got
			u32 rsvd : 28;
		} b;
		u32 all;
	} flags;
}raid_correct_t;

/*!
 * @brief  raid module init
 *
 * @return	not used
 */
void ftl_raid_init(void);

/*!
 * @brief  stripe user init
 *
 * @param su	stripe user
 *
 * @return	not used
 */
void stripe_user_init(stripe_user_t *su);

/*!
 * @brief reset raid stripe user
 *
 * @param su	stripe user
 *
 * @return	not used
 */
void strip_user_reset(stripe_user_t *su);

/*!
 * @brief get core wordline stripe when get a new cwl
 *
 * @param nsid	name space id
 * @param page_cnt	page count of the wordline
 * @param ttl_usr_du	total user du count of the wordline
 *
 * @return		core wordline stripe
 */
cwl_stripe_t* raid_get_cwl_stripe(pstream_t *ps, u32 page_cnt);

/*!
 * @brief put core wordline stripe
 *
 * @param cwl_stripe	core wordline stripe
 *
 * @return		not used
 */
void raid_put_cwl_stripe(cwl_stripe_t *cwl_stripe);

/*!
 * @brief get cwl stripe by stripe id
 *
 * @param stripe_id	id of cwl stripe
 *
 * @return		cwl stripe
 */
cwl_stripe_t* get_stripe_by_id(u32 stripe_id);

/*!
 * @brief set actice spb's raid info when alloc spb
 *
 * @param aspb	active spb
 *
 * @return	not used
 */
void set_spb_raid_info(aspb_t *aspb);

/*!
 * @brief set ncl req's raid id and bank id info
 *
 * @param req		ncl req
 * @param stripe	cwl stripe of the req
 *
 * @return		not used
 */
void set_ncl_req_raid_info(ncl_w_req_t *req, cwl_stripe_t *stripe, u32 lda_cnt, bool without_data);

/*!
 * @brief set ncl cmd's raid id/bank id & flags
 *
 * @param req	ncl req
 * @param ncl_cmd	ncl cmd
 *
 * @return		not used
 */
#ifndef DUAL_BE
void set_ncl_cmd_raid_info(ncl_w_req_t *req, struct ncl_cmd_t *ncl_cmd);
#endif
/*!
 * @brief update active stripe's state after all flying write done
 *
 * @param stripe_id	id of cwl stripe
 *
 * @return		not used
 */
void stripe_update(stripe_user_t *su, u32 stripe_id, pstream_t* ps);

/*!
 * @brief push raid construct ncl req into raid scheduler
 *
 * @param req	raid construct req
 *
 * @return	not used
 */
void raid_sched_push(ncl_w_req_t *req);

/*!
 * @brief raid module suspend by pmu
 *
 * @return	not used
 */
void ftl_raid_suspend(void);

/*!
 * @brief raid module resume from pmu
 *
 * @return	not used
 */
void ftl_raid_resume(void);

/*! @} */
