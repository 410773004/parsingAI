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
/*! \file scheduler.h
 * @brief scheduler module
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

#pragma once
#include "types.h"
#include "sect.h"
#include "bf_mgr.h"
#include "stdlib.h"
#include "nand_cfg.h"
#include "fc_export.h"

#define SCHEDULER1_L2P_QID	1		///< scheduler 1 l2p update queue id
#define SCHEDULER2_L2P_QID	3		///< scheduler 2 l2p update queue id

#if SCHEDULER == 1
#define L2P_UPDT_QID		SCHEDULER1_L2P_QID	///< l2p update queue id
#else
#define L2P_UPDT_QID		SCHEDULER2_L2P_QID	///< l2p update queue id
#endif

#define DTAG_GRP_IDX_TO_IDX(idx, grp)	(DTAG_CNT_PER_GRP * (grp) + (idx))
#define DTAG_IDX_TO_GRP_IDX(idx)	((idx) % DTAG_CNT_PER_GRP)

#define RSP_GRP_IDX_TO_IDX(idx, grp)	(PDA_RSP_PER_GRP * (grp) + (idx))		///< translate group index to normal index
#define RSP_IDX_TO_GRP_IDX(idx)		((idx) % PDA_RSP_PER_GRP)			///< translate normal index to group index

#define BLOCK_NO_BAD    (0)
#define BLOCK_P0_BAD    (1)
#define BLOCK_P1_BAD    (2)
#define BLOCK_ALL_BAD   (15) //2P:3, 4P:15

#ifdef SKIP_MODE
share_data_zi u8* gl_pt_defect_tbl;				///< global pointer to ftl defect table which initialized in defect_mgr.c
#endif

/*
** CBF size must be 2's power, it would waste almost 2KB memory, so don't use it here
*/
typedef struct _vpda_rsp_que_t {
	u32 rptr;
	u32 wptr;
	u32 size;
	pda_gen_rsp_t *rsp_idx;
} vpda_rsp_que_t;

typedef struct _gc_scheduler_t {
	u32 grp_res;			///< current PDA resource in this group
	pool_t rsp_pool;		///< resource pool
	pda_gen_rsp_t *vpda_rsp_entry;	///< valid pda response entry, GC PDA resource
	vpda_rsp_que_t vpda_rsp_que;		///< received valid pda reponse entry queue
	u32 vpda_pending;	///< valid pda pending count

	pda_t *pda_list;
    lda_t *lda_list;  //gc lda list from p2l
	io_meta_t *meta_list;

	u32 ddtag_start;	///< group ddr dtag start
	u32 ddtag_avail;
	//bool ddtag_bmp[DTAG_CNT_PER_GRP];	///< group ddr dtag avail bitmap
	u16* free_gc_dtagid_q;
	u16 wptr;
	u16 rptr;
	
	u32 pend_die_bmp[MAX_DIE_MGR_CNT];
	bool rd_done;
} gc_scheduler_t;

#if defined(TCG_NAND_BACKUP)
typedef struct _mbr_scheduler_t {
	u32 value;
	struct _mbr_scheduler_t* next;
} mbr_scheduler_t;

#endif

extern volatile gc_pda_grp_t gc_pda_grp[MAX_GC_PDA_GRP];	///< gc group control context
extern gc_scheduler_t gc_scheduler;
extern u32 rd_dummy_meta_idx;	///< dummy meta index for read
extern u32 wr_dummy_meta_idx;	///< dummy meta index for write

/*!
 * @brief group scheduler initialization
 *
 * @param srch_evt	l2p search done handler event
 * @param gid		group id
 *
 * @return		not used
 */
void scheduler_init(u8 *srch_evt, u32 gid);

/*!
 * @brief suspend group scheduler
 *
 * @param gid		group id
 *
 * @return		not used
 */
void scheduler_suspend(u32 gid);

/*!
 * @brief resume group scheduler
 *
 * @param gid		group id
 *
 * @return		not used
 */
void scheduler_resume(u32 gid);

/*!
 * @brief gc group initialization
 *
 * @param gid		group id
 * @param evt_pda_gen	l2p gc pda in handler event
 *
 * @return		not used
 */
void scheduler_gc_grp_init(u8 gid, u8 *evt_pda_gen);

/*!
 * @brief gc group abort, drop all valid pda
 *
 * @param gid		group id
 *
 * @return		not used
 */
void scheduler_gc_grp_aborted(u32 grp);

/*!
 * @brief refill gc group resource
 *
 * @param grp		group id
 *
 * @return		not used
 */
void scheduler_gc_grp_refill(u32 grp);

/*!
 * @brief gc valid pda read
 *
 * @param grp		group id
 *
 * @return		not used
 */
void scheduler_gc_vpda_read(u32 grp);
void issue_gc_read(pda_gen_rsp_t *rsp);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
void issue_gc_read_slc(u32 qid);
void gc_slc_raid_calculate(u8 * df_ptr);
u8 slc_get_defect_pl_pair(u8* ftl_df, u32 interleave);
#endif



/*!
 * @brief decrease ongoing reading count
 *
 * @param grp		group id
 * @param bm_pl		gc data entries
 * @param cnt		count of entries
 *
 * @return		not used
 */
void scheduler_gc_read_done(u32 grp, bm_pl_t *bm_pl, u32 cnt);

void SysInfo_update(void);
void SysInfo_bkup(u8 ce);    //Sean_20220511

/*!
 * @brief abort all host streaming read in scheduler
 *
 * @param reset		reset context
 *
 * @return		number of scheduler
 */
u32 scheduler_abort_host_streaming_read(ftl_reset_t *reset);

/*!
 * @brief resume scheduler to handle host stream read
 *
 * @return		not used
 */
void scheduler_resume_host_streaming_read(void);

void scheduler_plp_cancel_die_que(volatile cpu_msg_req_t *req);

/*! @} */
