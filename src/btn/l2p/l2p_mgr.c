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

/*! \file
 * @brief rainier l2p engine manager
 *
 * \addtogroup btn
 * \defgroup l2pe
 * \ingroup btn
 * @{
 */
#include "btn_precomp.h"
#include "btn_ftl_ncl_reg.h"
#include "bf_mgr.h"
#include "event.h"
#include "assert.h"
#include "vic_id.h"
#include "pmu.h"
#include "console.h"
#include "rainier_soc.h"
#include "misc.h"
#include "btn.h"
#include "l2p_mgr.h"
#include "l2cache.h"
#include "l2p_sw_q.h"
#include "mpc.h"
#include "pmu.h"
#include "bucket.h"

#define __FILEID__ lxp
#include "trace.h"

#if defined(TFW_CTRL_L2P)
#define xinit_code fast_code
#else
#define xinit_code init_code
#endif

//#define DEBUG_LRANGE

#define L2P_BASE 0xC00D0000			///< l2pe register start address

#define MAX_L2P_SRCH_NUM	32		///< max search number per request

#define ENTRY_NUM 		256		///< entry number
#define SRCH_REQ_ENTRY_CNT	ENTRY_NUM	///< entry number of request queue
#define SRCH_RSP_UNMAP_CNT	ENTRY_NUM	///< entry number of search unmap response queue
#define SRCH_RSP_CNT		ENTRY_NUM	///< entry number of search response queue

/* L2P  update */
#define UPDT_REQ_ENTRY_CNT	256		///< entry number of update request queue
#define UPDT_CPL_ENTRY_CNT	128		///< entry number of update complete queue
#define CMMT_CPL_ENTRY_CNT	128		///< entry number of commit complete queue
#define RECYCLE_ENTRY_CNT	256		///< entry number of recycle queue
#define STATUS_SRCH_REQ_ENTRY_CNT 	32	///< entry number of status search request queue
#define STATUS_SRCH_RSP_ENTRY_CNT 	32	///< entry number of status search response queue
#define STATUS_FET_REQ_ENTRY_CNT	32	///< entry number of status fetch request queue
#define STATUS_FET_RSP_ENTRY_CNT	32	///< entry number of status fetch response queue

/* L2P dvbm fw update */
#define DVL_INFO_UPDT_REQ_ENTRY_CNT		256	///< entry number of valid bit map update request queue
#define DVL_INFO_UPDT_CPL_ENTRY_CNT		256	///< entry number of valid bit map update complete queue

/* L2P pda gen */
#define PDA_GEN_REQ_ENTRY_CNT	16		///< entry number of pda generation request queue
#define PDA_GEN_LOC_ENTRY_CNT	256		///< entry number of pda generation response location source
#define PDA_GEN_RSP_ENTRY_CNT	256		///< entry number of pda generation response queue

#define STATUS_CNT_NOTIFY_ENTRY_CNT	32

#ifndef CPU_FE
#define CPU_FE			1
#endif

#ifndef CPU_BE
#define CPU_BE			1
#endif

#include "l2p_setting.h"

#define PDA_GEN_RST0_UPDT_MASK		(PDA_GEN_RSLT0_QUE7_UPDT_MASK | \
					PDA_GEN_RSLT0_QUE6_UPDT_MASK | \
					PDA_GEN_RSLT0_QUE5_UPDT_MASK | \
					PDA_GEN_RSLT0_QUE4_UPDT_MASK | \
					PDA_GEN_RSLT0_QUE3_UPDT_MASK | \
					PDA_GEN_RSLT0_QUE2_UPDT_MASK | \
					PDA_GEN_RSLT0_QUE1_UPDT_MASK | \
					PDA_GEN_RSLT0_QUE0_UPDT_MASK)

#define PDA_GEN_RST1_UPDT_MASK		(PDA_GEN_RSLT1_QUE7_UPDT_MASK | \
					PDA_GEN_RSLT1_QUE6_UPDT_MASK | \
					PDA_GEN_RSLT1_QUE5_UPDT_MASK | \
					PDA_GEN_RSLT1_QUE4_UPDT_MASK | \
					PDA_GEN_RSLT1_QUE3_UPDT_MASK | \
					PDA_GEN_RSLT1_QUE2_UPDT_MASK | \
					PDA_GEN_RSLT1_QUE1_UPDT_MASK | \
					PDA_GEN_RSLT1_QUE0_UPDT_MASK)



#if CPU_ID == SRCH_QUE0_NRM_CPU_ID || CPU_ID == SRCH_QUE0_HIGH_CPU_ID || CPU_ID == SRCH_QUE0_URG_CPU_ID
#define		L2P_SRCH_CPU
#endif
#if CPU_ID == SRCH_PDA_GRP0_CPU_ID || CPU_ID == SRCH_PDA_GRP1_CPU_ID
#define		L2P_SRCH_RST_CPU
#endif

#if CPU_ID == UPDT_REQ_QUE0_CPU_ID || CPU_ID == UPDT_REQ_QUE1_CPU_ID || CPU_ID == UPDT_REQ_QUE2_CPU_ID || CPU_ID == UPDT_REQ_QUE3_CPU_ID
#define		L2P_UPDT_CPU
#endif

#if CPU_ID == UPDT_CMPL_QUE0_CPU_ID || CPU_ID == UPDT_CMPL_QUE1_CPU_ID
#define		L2P_UPDT_CMPL_CPU
#endif

#if CPU_ID == DVL_INFO_UPDT_REQ_QUE_CPU_ID
#define		L2P_DVL_INFO_UPDT_CPU
#endif

#if CPU_ID == DVL_INFO_UPDT_CPL_QUE_CPU_ID
#define		L2P_DVL_INFO_UPDT_CPL_CPU
#endif

#if CPU_ID == VALID_CNT_QUE0_CPU_ID
#define		L2P_VALID_CNT_CPU
#endif

#if CPU_ID == SRCH_PDA_GRP0_CPU_ID || CPU_ID == UNMAP_QUE_CPU_ID || CPU_ID == PDA_GEN_RSP0_QUE0_CPU_ID
#define L2P_G0_SRCH_CPU
#endif

#if CPU_ID == SRCH_PDA_GRP1_CPU_ID || CPU_ID == PDA_GEN_RSP1_QUE0_CPU_ID
#define L2P_G1_SRCH_CPU
#endif

#if CPU_ID == UPDT_CMPL_QUE0_CPU_ID || CPU_ID == CMMT_CMPL_QUE0_CPU_ID || CPU_ID == UPDT_CMPL_QUE1_CPU_ID || CPU_ID == CMMT_CMPL_QUE1_CPU_ID
#define L2P_UPDT_CMMT_Q01_CPU
#endif

#if CPU_ID == UPDT_CMPL_QUE2_CPU_ID || CPU_ID == CMMT_CMPL_QUE2_CPU_ID || CPU_ID == UPDT_CMPL_QUE3_CPU_ID || CPU_ID == CMMT_CMPL_QUE3_CPU_ID
#define L2P_UPDT_CMMT_Q23_CPU
#endif

#if CPU_ID == STATUS_SRCH_RSP_QUE_CPU_ID || CPU_ID == DVL_INFO_UPDT_CPL_QUE_CPU_ID || CPU_ID == L2P_ERROR_CPU_ID || CPU_ID == STATUS_FET_RSP_QUE_CPU_ID || CPU_ID == STATUS_CNT_NOTIFY_QUE_CPU_ID
#define L2P_FTL_OTHER_CPU
#endif
share_data_zi volatile u32 shr_rd_cnt_tbl_addr;
share_data_zi volatile u32 shr_rd_cnt_counter_addr;
share_data_zi volatile u32 shr_ec_tbl_addr;

share_data_zi volatile u32 shr_ent_l2p_seg_shf;		///< rdisk lda to l2p segment shift bit
#if (FTL_L2P_SEG_BMP == mENABLE)
share_data_zi volatile u32 *shr_l2p_seg_dirty_bmp;		///< rdisk l2p segment dirty bit map
share_data_zi volatile u32 *shr_l2p_seg_flush_bmp;		///< rdisk l2p segment flush bit map
#endif
share_data_zi u32 shr_l2p_seg_bmp_sz;		///< rdisk l2p segment bit map size
share_data_zi volatile u32 shr_l2p_entry_need;		///< rdisk l2p entry need dtag number
share_data_zi volatile u32 shr_l2p_entry_start;		///< rdisk l2p entry start dtag
share_data_zi volatile u32 shr_l2p_seg_cnt;			///< rdisk l2p segment number
share_data_zi volatile u32 shr_l2p_seg_sz;			///< rdisk l2p segment size
share_data_zi volatile u8 shr_l2pp_per_seg;			///< rdisk number of l2p page per segment
//share_data_zi u8 shr_l2pp_per_seg_shf;		///< ridsk shift of l2p page per segment
share_data_zi volatile u32 shr_blklistbuffer_need;

share_data_zi volatile u32 shr_vac_drambuffer_start;
share_data_zi volatile u32 shr_vac_drambuffer_need;

share_data_zi volatile u32 shr_blklistbuffer_start[2];

share_data_zi volatile u32 poh;
share_data_zi volatile u32 pc_cnt;
share_data_zi volatile u32 pom_per_ms;
share_data_zi volatile u16 shr_dummy_blk;
share_data_zi volatile bool shr_format_fulltrim_flag;

// added by Sunny, for ftl core fill meta
share_data_zi volatile u64 shr_page_sn;
share_data_zi volatile u8  shr_qbt_loop;
share_data_zi volatile u8  shr_pbt_loop;

u32  user_min_ec = INV_U32;
u16  max_ec_blk = INV_U16;
u16  user_min_ec_blk = INV_U16;
u16  pre_wl_blk = INV_U16;
share_data_zi volatile bool shr_shutdownflag;

/*!
 * @brief l2p register read
 */
static fast_code u32 l2p_readl(u32 reg)
{
	return readl((void *)(L2P_BASE + reg));
}

/*!
 * @brief l2p register write
 */
static fast_code void l2p_writel(u32 val, u32 reg)
{
	writel(val, (void *)(L2P_BASE + reg));
}

/*!
 * @brief count l2p request queue available number
 *
 * @return	number of available requst
 */
fast_code static inline u32 l2p_req_que_distance(l2p_que_t *que)
{
	volatile u32 wptr = que->reg.entry_pnter.b.wptr;
	volatile u32 rptr = que->ptr;

	if (rptr == wptr)
		return que->reg.entry_dbase.b.max_sz;
	else if (rptr < wptr)
		return que->reg.entry_dbase.b.max_sz - wptr + rptr;
	else // que->ptr > wptr
		return rptr - wptr - 1;
}

/* Search Queue 0 resource */
#if SRCH_QUE0_NRM_CPU_ID == CPU_ID
static fast_data l2p_que_t _srch_req0_nrm = { .swq_id = ~0 };			///< normal priority search request queue 0
static fast_data_ni srch_req_t _srch_nrm_req0_entry[SRCH_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< normal priority search request queue 0 entry
#endif

#if SRCH_QUE0_HIGH_CPU_ID == CPU_ID
static fast_data l2p_que_t _srch_req0_high = { .swq_id = ~0 };			///< high priority search request queue 0
static fast_data_ni srch_req_t _srch_hig_req0_entry[SRCH_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< highy priority search request queue 0 entry
#endif

#if SRCH_QUE0_URG_CPU_ID == CPU_ID
static fast_data l2p_que_t _srch_req0_urg = { .swq_id = ~0 };			///< urgent priority search request queue 0
static fast_data_ni srch_req_t _srch_urg_req0_entry[SRCH_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< urgent priority search request queue 0 entry
#endif

/* Search Queue 1 resource */
#if SRCH_QUE1_NRM_CPU_ID == CPU_ID
static fast_data l2p_que_t _srch_req1_nrm = { .swq_id = ~0 };			///< normal priority search request queue 1
static fast_data_ni srch_req_t _srch_nrm_req1_entry[SRCH_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< normal priority search request queue 1 entry
#endif

#if SRCH_QUE1_HIGH_CPU_ID == CPU_ID
static fast_data l2p_que_t _srch_req1_high = { .swq_id = ~0 };			///< high priority search request queue 1
static fast_data_ni srch_req_t _srch_hig_req1_entry[SRCH_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< highy priority search request queue 1 entry
#endif

#if SRCH_QUE1_URG_CPU_ID == CPU_ID
static fast_data l2p_que_t _srch_req1_urg = { .swq_id = ~0 };			///< urgent priority search request queue 1
static fast_data_ni srch_req_t _srch_urg_req1_entry[SRCH_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< urgent priority search request queue 1 entry
#endif

/* L2P rsp  - unmap */
#if UNMAP_QUE_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t  _srch_rsp_unmap;					///< search unmap response queue
static fast_data_ni l2p_srch_unmap_t _srch_rsp_unmap_entry[SRCH_RSP_UNMAP_CNT] \
			 __attribute__ ((aligned(8)));				///< search unmap response queue entry
fast_data u8 evt_l2p_srch_umap = 0xff;						///< search unmap response handle event
#endif

#if SRCH_PDA_GRP0_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _srch_rsp0_surc;					///< search response group 0 source queue
static fast_data_ni u32 _srch_rsp0_surc_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< search response group 0 source entry

static fast_data_zi buckets_t *_srch_rsp0_buckets;					///< search response group 0 resource pool
static fast_data_ni l2p_srch_rsp_t _srch_rsp0_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< search response group 0 resource entry

static fast_data_ni l2p_que_t _srch_rsp0[SRCH_MAX];				///< search response group 0 queues
static fast_data_ni u32 _srch_nrm_rsp0_dst_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< normal priority search response group 0 entry

static fast_data_ni u32 _srch_hig_rsp0_dst_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< high priority search response group 0 entry

static fast_data_ni u32 _srch_urg_rsp0_dst_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< urgent priority search response group 0 entry

fast_data u8 evt_l2p_srch0 = 0xff;						///< search response group 1 handle event
#endif

#if SRCH_PDA_GRP1_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _srch_rsp1_surc;					///< search response group 1 source queue
static fast_data_ni u32 _srch_rsp1_surc_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(8)));				///< search response group 1 source entry

static fast_data_zi buckets_t *_srch_rsp1_buckets;					///< search response group 1 resource pool
static fast_data_ni l2p_srch_rsp_t _srch_rsp1_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< search response group 1 resource entry

static fast_data_ni l2p_que_t _srch_rsp1[SRCH_MAX];				///< search response group 1 queues
static fast_data_ni u32 _srch_nrm_rsp1_dst_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< normal priority search response group 1 entry

static fast_data_ni u32 _srch_hig_rsp1_dst_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< high priority search response group 1 entry

static fast_data_ni u32 _srch_urg_rsp1_dst_entry[SRCH_RSP_CNT] \
			 __attribute__ ((aligned(16)));				///< urgent priority search response group 1 entry
fast_data u8 evt_l2p_srch1 = 0xff;						///< search response group 1 handle event
#endif

/* update queue resource */
#if UPDT_REQ_QUE0_CPU_ID == CPU_ID
static fast_data l2p_que_t _updt_req_que0 = { .swq_id = ~0 };			///< update request queue 0
static fast_data_ni updt_req_t _updt_req_que0_entry[UPDT_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update request queue 0 entry
#endif

#if UPDT_REQ_QUE1_CPU_ID == CPU_ID
static fast_data l2p_que_t _updt_req_que1 = { .swq_id = ~0 };			///< update request queue 1
static fast_data_ni updt_req_t _updt_req_que1_entry[UPDT_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update request queue 1 entry
#endif

#if UPDT_REQ_QUE2_CPU_ID == CPU_ID
static fast_data l2p_que_t _updt_req_que2 = { .swq_id = ~0 };			///< update request queue 2
static fast_data_ni updt_req_t _updt_req_que2_entry[UPDT_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update request queue 2 entry
#endif

#if UPDT_REQ_QUE3_CPU_ID == CPU_ID
static fast_data l2p_que_t _updt_req_que3 = { .swq_id = ~0 };			///< update request queue 3
static fast_data_ni updt_req_t _updt_req_que3_entry[UPDT_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update request queue 3 entry
#endif

#if UPDT_CMPL_QUE0_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _updt_cpl_que0;					///< update complete queue 0
static fast_data_ni updt_cpl_t _updt_cpl_que0_entry[UPDT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update complete queue 0 entry
fast_data u8 evt_l2p_updt0 = 0xff;						///< update complete queue 0 handle event
#endif

#if UPDT_CMPL_QUE1_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _updt_cpl_que1;					///< update complete queue 1
static fast_data_ni updt_cpl_t _updt_cpl_que1_entry[UPDT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update complete queue 1 entry
fast_data u8 evt_l2p_updt1 = 0xff;						///< update complete queue 1 handle event
#endif

#if UPDT_CMPL_QUE2_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _updt_cpl_que2;					///< update complete queue 2
static fast_data_ni updt_cpl_t _updt_cpl_que2_entry[UPDT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update complete queue 2 entry
fast_data u8 evt_l2p_updt2 = 0xff;						///< update complete queue 2 handle event
#endif

#if UPDT_CMPL_QUE3_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _updt_cpl_que3;					///< update complete queue 3
static fast_data_ni updt_cpl_t _updt_cpl_que3_entry[UPDT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< update complete queue 3 entry
fast_data u8 evt_l2p_updt3 = 0xff;						///< update complete queue 3 handle event
#endif

#if CMMT_CMPL_QUE0_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _cmmt_cpl_que0;					///< commit complete queue 0
static fast_data_ni cmmt_cpl_t _cmmt_cpl_que0_entry[CMMT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));				///< commit complete queue 0 entry
fast_data u8 evt_l2p_cmmt0 = 0xff;						///< commit complete queue 0 handle event
#endif

#if CMMT_CMPL_QUE1_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _cmmt_cpl_que1;					///< commit complete queue 1
static fast_data_ni cmmt_cpl_t _cmmt_cpl_que1_entry[CMMT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< commit complete queue 1 entry
fast_data u8 evt_l2p_cmmt1 = 0xff;						///< commit complete queue 1 handle event
#endif

#if CMMT_CMPL_QUE2_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _cmmt_cpl_que2;					///< commit complete queue 2
static fast_data_ni cmmt_cpl_t _cmmt_cpl_que2_entry[CMMT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< commit complete queue 2 entry
fast_data u8 evt_l2p_cmmt2 = 0xff;						///< commit complete queue 2 handle event
#endif

#if CMMT_CMPL_QUE3_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _cmmt_cpl_que3;					///< commit complete queue 3
static fast_data_ni cmmt_cpl_t _cmmt_cpl_que3_entry[CMMT_CPL_ENTRY_CNT] \
			 __attribute__ ((aligned(8)));				///< commit complete queue 3 entry
fast_data u8 evt_l2p_cmmt3 = 0xff;						///< commit complete queue 3 handle event
#endif

#if RECYCLE_QUE_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _recycle_que;							///< recycle queue
static fast_data_ni u32 _recycle_que_entry[RECYCLE_ENTRY_CNT] __attribute__ ((aligned(8)));	///< recycle queue entry
fast_data u8 evt_l2p_recycle_que = 0xff;						///< commit complete queue 3 handle event
#endif

/* du valid bitmap update queue resource */
#if DVL_INFO_UPDT_REQ_QUE_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _dvl_info_updt_req_que;							///< valid bit map update request queue
static fast_data_ni dvl_info_updt_req_t _dvl_info_updt_req_que_entry[DVL_INFO_UPDT_REQ_ENTRY_CNT] ALIGNED(8);	///< valid bit map update request entry
fast_data u8 evt_dvl_info_updt_rslt = 0xff;
#endif

#if DVL_INFO_UPDT_CPL_QUE_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _dvl_info_updt_cpl_que;							///< valid bit map update complete queue
static fast_data_ni dvl_info_updt_cpl_t _dvl_info_updt_cpl_que_entry[DVL_INFO_UPDT_CPL_ENTRY_CNT] ALIGNED(4);	///< valid bit map update complete entry
#endif

#if PDA_GEN_QUE0_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_req_que0;							///< pda generation group 0 request queue
static fast_data_ni union {
	pda_gen_req_t pda_gen[PDA_GEN_REQ_ENTRY_CNT];						///< pda generation group 0 request entry
	p2l_pda_gen_req_t p2l_pda_gen[PDA_GEN_REQ_ENTRY_CNT];
} ALIGNED(16) _pda_gen_req_que0_entry;
#endif

#if PDA_GEN_QUE1_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_req_que1;							///< pda generation group 0 request queue
static fast_data_ni union {
	pda_gen_req_t pda_gen[PDA_GEN_REQ_ENTRY_CNT];						///< pda generation group 0 request entry
	p2l_pda_gen_req_t p2l_pda_gen[PDA_GEN_REQ_ENTRY_CNT];
} ALIGNED(16) _pda_gen_req_que1_entry;
#endif

#if PDA_GEN_LOC0_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_loc_que0;						///< pda generation group 0 location source queue
static fast_data_ni u32 _pda_gen_loc_que0_entry[PDA_GEN_LOC_ENTRY_CNT] ALIGNED(4);		///< pda generation group 0 location source entry
#endif

#if PDA_GEN_LOC1_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_loc_que1;						///< pda generation group 1 location source queue
static fast_data_ni u32 _pda_gen_loc_que1_entry[PDA_GEN_LOC_ENTRY_CNT] ALIGNED(4);		///< pda generation group 1 location source entry
#endif

#if PDA_GEN_RSP0_QUE0_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_rsp0_que0;						///< pda generation group 0 response queue
static fast_data_ni u32 _pda_gen_rsp0_que0_entry[PDA_GEN_RSP_ENTRY_CNT] ALIGNED(4);	///< pda generation group 0 response entry
fast_data u8 evt_pda_gen_rsp0_que0 = 0xff;						///< pda generation group 0 response handle event
#endif

#if PDA_GEN_RSP1_QUE0_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_rsp1_que0;						///< pda generation group 1 response queue
static fast_data_ni u32 _pda_gen_rsp1_que0_entry[PDA_GEN_RSP_ENTRY_CNT] ALIGNED(4);	///< pda generation group 1 response entry
fast_data u8 evt_pda_gen_rsp1_que0 = 0xff;						///< pda generation group 1 response handle event
#endif

#if PDA_GEN_RSP0_QUE1_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_rsp0_que1;						///< pda generation group 0 response queue
static fast_data_ni u32 _pda_gen_rsp0_que1_entry[PDA_GEN_RSP_ENTRY_CNT] ALIGNED(4);	///< pda generation group 0 response entry
fast_data u8 evt_pda_gen_rsp0_que1 = 0xff;						///< pda generation group 0 response handle event
#endif

#if PDA_GEN_RSP1_QUE1_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _pda_gen_rsp1_que1;						///< pda generation group 1 response queue
static fast_data_ni u32 _pda_gen_rsp1_que1_entry[PDA_GEN_RSP_ENTRY_CNT] ALIGNED(4);	///< pda generation group 1 response entry
fast_data u8 evt_pda_gen_rsp1_que1 = 0xff;						///< pda generation group 1 response handle event
#endif

#if STATUS_FET_REQ_QUE_CPU_ID == CPU_ID
fast_data_ni l2p_que_t _status_fet_req_que;					///< spb valid count search queue
fast_data_ni fet_cnt_req_t _status_fet_req_que_entry[STATUS_FET_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));			///< spb valid count search entry
#endif

#if STATUS_FET_RSP_QUE_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _status_fet_rsp_que;				///< spb valid count response queue
fast_data_ni fet_cnt_rsp_t _status_fet_rsp_que_entry[STATUS_FET_RSP_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));			///< spb valid count response entry
fast_data u8 evt_status_fet_rsp = 0xff;					///< spb valid count response handle result
#endif

#if STATUS_SRCH_REQ_QUE_CPU_ID == CPU_ID
fast_data_ni l2p_que_t _status_srch_req_que;					///< spb valid count search queue
fast_data_ni cnt_req_t _status_srch_req_que_entry[STATUS_FET_REQ_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));			///< spb valid count search entry
#endif

#if STATUS_SRCH_RSP_QUE_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _status_srch_rsp_que;				///< status count response queue
fast_data_ni cnt_rsp_t _status_srch_rsp_que_entry[STATUS_SRCH_RSP_ENTRY_CNT] \
			 __attribute__ ((aligned(16)));			///< status count response entry
fast_data u8 evt_status_srch_rsp = 0xff;					///< status count response handle result
#endif

#if STATUS_CNT_NOTIFY_QUE_CPU_ID == CPU_ID
static fast_data_ni l2p_que_t _status_cnt_notify_que;				///< status counter notify queue
static fast_data_ni cnt_notify_t _status_cnt_notify_que_entry[STATUS_CNT_NOTIFY_ENTRY_CNT] \
			__attribute__ ((aligned(8)));				///< status counter notify queue entry
fast_data u8 evt_vcnt_notify = 0xff;
#endif

fast_data_zi u32 max_dtag;						///< max number of dtag
#if CPU_ID == 1
fast_data_zi u32 backup_l2p_regs[BACKUP_L2P_REG_CNT];		///< backup of l2p register
#endif

#if defined(DEBUG_LRANGE)
#if CPU_ID == 1
share_data_zi volatile u32 shr_max_lda = 0;
#else
share_data_zi volatile u32 shr_max_lda;
#endif

static inline void debug_lrange(u32 lda, lda_t *lda_list, u32 cnt, u32 line)
{
	if (shr_max_lda == 0) {
		lxp_mgr_trace(LOG_ERR, 0xf43f, "shr_max_lda == 0\n");
		panic("fatal error");
		return;
	}

	if (lda_list == NULL) {
		if (shr_max_lda <= lda || shr_max_lda <= (lda + cnt - 1)) {
			lxp_mgr_trace(LOG_ERR, 0x91a9, "lda overflow %d-%d at %d\n", lda, cnt, line);
		}
	} else {
		u32 i = 0;

		do {
			if (lda_list[i] >= shr_max_lda) {
				lxp_mgr_trace(LOG_ERR, 0x5b34, "lda[%d] overflow, %d at %d\n", i, lda_list[i], line);
			}
		} while (++i < cnt);
	}
}
#else
#define debug_lrange(a, b, c, d)
#endif

/*!
 * @brief search request queue setup
 *
 * @param pri	search priority
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param reqs	pinter to request entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_srch_req_q_setup(l2p_srch_pri_t pri, u32 qsize, l2p_que_t *que, srch_req_t *reqs, u32 qid);

/*!
 * @brief unmap response queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param rsps	pinter to response entry
 *
 * @return	not used
 */
static void l2p_srch_unmap_q_setup(u32 qsize, l2p_que_t *que, l2p_srch_unmap_t *rsps);

/*!
 * @brief search resource queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param locs	pointer to location source
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_srch_loc_q_setup(u32 qsize, l2p_que_t *que, u32 *locs, u32 qid);

/*!
 * @brief search complete queue setup
 *
 * @param pri	search priority
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param rsps	pinter to response entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_srch_cmpl_q_setup(l2p_srch_pri_t pri, u32 qsize, l2p_que_t *que, u32 *rsps, u32 gid);

/*!
 * @brief update request queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param reqs	pinter to request entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_updt_req_q_setup(u32 qsize, l2p_que_t *que, updt_req_t *reqs, u32 qid);

/*!
 * @brief update complete queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param reqs	pinter to complete entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_updt_cmpl_q_setup(u32 qsize, l2p_que_t *que, updt_cpl_t *reqs, u32 qid);

/*!
 * @brief commit complete queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param reqs	pinter to complete entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_cmmt_cmpl_q_setup(u32 qsize, l2p_que_t *que, cmmt_cpl_t *reqs, u32 qid);

/*!
 * @brief pda generation request queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param reqs	pinter to request entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_pda_gen_req_q_setup(u32 qsize,l2p_que_t *que, void *reqs, u32 qid);

/*!
 * @brief pda generation resource queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param locs	pinter to location source entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_pda_gen_loc_q_setup(u32 qsize, l2p_que_t *que, u32 *locs, u32 gid);

/*!
 * @brief pda generation group 0 response queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param rsps	pinter to response entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_pda_gen_rsp0_q_setup(u32 qsize, l2p_que_t *que, u32 *rsps, u32 qid);

/*!
 * @brief pda generation group 1 response queue setup
 *
 * @param qsize	queue size
 * @param que	pointer to queue to be setup
 * @param rsps	pinter to response entry
 * @param qid	queue id
 *
 * @return	not used
 */
static void l2p_pda_gen_rsp1_q_setup(u32 qsize, l2p_que_t *que, u32 *rsps, u32 qid);

/*!
 * @brief function to polling target queue
 *
 * @param que		pointer to queue to be setup
 * @param evt		handle event
 * @param ent_sz	result entry size
 *
 * @return	not used
 */
static void rslt_poll(l2p_que_t *que, u8 evt, u32 ent_sz);

share_data_zi volatile l2p_fda_t _max_fda;			///< max value of each fda element

/*!
 * @brief pda update request template
 */
fast_data updt_req_t _pda_updt = {
		.updt.valid_cnt_adjust = 1,
		.updt.dtag_or_pda = 0,	// 0 is PDA, 1 is DTAG
		.updt.fw_seq_id = 0, .updt.region = 0,
		.updt.req_type = 0,
		.updt.lda_num = 0, // to be refined
		.updt.lda_auto_inc = 0
};
//#define PRINT_LVL  LOG_DEBUG //LOG_INFO //

/****************************************************************************
 * update/commit
 ****************************************************************************/
/*!
 * @brief function to polling update complete queue
 *
 * @param updt_cpl	pointer to the queue
 * @param evt		handle event
 *
 * @return	not used
 */
UNUSED fast_code static void updt_cmpl_poll(l2p_que_t* updt_cpl, u8 evt)
{
	hdr_reg_t *reg = &(updt_cpl->reg);
	updt_cpl_t *entry  = (updt_cpl_t *)reg->base;
	u8 wptr = updt_cpl->ptr;

	if (reg->entry_pnter.b.rptr == wptr)
		return;

	if (reg->entry_pnter.b.rptr > wptr) {
		updt_cpl_t *p = entry + reg->entry_pnter.b.rptr;
		u32 c = reg->entry_dbase.b.max_sz;

		c += 1;
		c -= reg->entry_pnter.b.rptr;
		evt_set_imt(evt, (u32) p, c);
		reg->entry_pnter.b.rptr = 0;
	}

	if (reg->entry_pnter.b.rptr < wptr) {
		updt_cpl_t *p = entry + reg->entry_pnter.b.rptr;
		u32 c = wptr;

		c -= reg->entry_pnter.b.rptr;
		evt_set_imt(evt, (u32) p, c);
		reg->entry_pnter.b.rptr = wptr;
	}

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

/*!
 * @brief function to polling commit complete queue
 *
 * @param cmmt_cpl	pointer to the queue
 * @param evt		handle event
 *
 * @return	not used
 */
UNUSED fast_code static void cmmt_cmpl_poll(l2p_que_t* cmmt_cpl, u8 evt)
{
	hdr_reg_t *reg = &(cmmt_cpl->reg);
	cmmt_cpl_t *entry  = (cmmt_cpl_t *)reg->base;
	u8 wptr = cmmt_cpl->ptr;

	if (reg->entry_pnter.b.rptr == wptr)
		return;

	if (reg->entry_pnter.b.rptr > wptr) {
		cmmt_cpl_t *p = entry + reg->entry_pnter.b.rptr;
		u32 c = reg->entry_dbase.b.max_sz;

		c += 1;
		c -= reg->entry_pnter.b.rptr;
		evt_set_imt(evt, (u32) p, c);
		reg->entry_pnter.b.rptr = 0;
	}

	if (reg->entry_pnter.b.rptr < wptr) {
		cmmt_cpl_t *p = entry + reg->entry_pnter.b.rptr;
		u32 c = wptr;

		c -= reg->entry_pnter.b.rptr;
		evt_set_imt(evt, (u32) p, c);
		reg->entry_pnter.b.rptr = wptr;
	}

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

/*!
 * @brief function to polling commit complete queue
 *
 * @param cmmt_cpl	pointer to the queue
 * @param evt		handle event
 *
 * @return	not used
 */
UNUSED fast_code static void recycle_poll(l2p_que_t* recycle_que, u8 evt)
{
	hdr_reg_t *reg = &(recycle_que->reg);
	u32 *entry  = (u32 *)reg->base;
	u8 wptr = recycle_que->ptr;

	if (reg->entry_pnter.b.rptr == wptr)
		return;

	if (reg->entry_pnter.b.rptr > wptr) {
		u32 *p = entry + reg->entry_pnter.b.rptr;
		u32 c = reg->entry_dbase.b.max_sz;

		c += 1;
		c -= reg->entry_pnter.b.rptr;
		evt_set_imt(evt, (u32) p, c);
		reg->entry_pnter.b.rptr = 0;
	}

	if (reg->entry_pnter.b.rptr < wptr) {
		u32 *p = entry + reg->entry_pnter.b.rptr;
		u32 c = wptr;

		c -= reg->entry_pnter.b.rptr;
		evt_set_imt(evt, (u32) p, c);
		reg->entry_pnter.b.rptr = wptr;
	}

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

#define PRINT_LVL  LOG_DEBUG //LOG_INFO //

/*!
 * @brief function to handle response queue and return resource
 *
 * @param src		pointer to resource queue
 * @param que		pointer to the queue
 * @param evt		handle event
 *
 * @return	not used
 */
UNUSED fast_code static void l2p_isr_rsp_recycle(l2p_que_t *src, buckets_t *buckets, l2p_que_t *que, u8 evt)
{
	hdr_reg_t *reg;
	u8 wptr;
	u32 *entry;
	bucket_t bucket;

	reg = &que->reg;
	wptr = que->ptr;
	entry = (u32 *) reg->base;
	if (((wptr + 1) & reg->entry_dbase.b.max_sz) == reg->entry_pnter.b.rptr)
		lxp_mgr_trace(LOG_DEBUG, 0xb6a6, "%x full", que);

#if defined(RDISK)
	u16 rsv_die_que_trig(void);
	if (rsv_die_que_trig()) {
		return;
	}
#endif

	if (reg->entry_pnter.b.rptr == wptr)
		return;

	if (reg->entry_pnter.b.rptr > wptr) {
		u32 *p = entry + reg->entry_pnter.b.rptr;
		u32 c = reg->entry_dbase.b.max_sz;
		u32 i;

		c += 1;
		c -= reg->entry_pnter.b.rptr;

		evt_set_imt(evt, (u32)p, c);

		for (i = 0; i < c; i++) {
			bool ret = bucket_put_entry(buckets, p[i]);
			if (ret) {
				ret = bucket_get_item(buckets, &bucket);
				sys_assert(ret == true);
				hdr_surc_push(&src->reg, src->ptr, bucket.all);
			}
		}

		reg->entry_pnter.b.rptr = 0;
	}

	if (reg->entry_pnter.b.rptr < wptr) {
		u32 *p = entry + reg->entry_pnter.b.rptr;
		u32 c = wptr;
		u32 i;

		c -= reg->entry_pnter.b.rptr;

		evt_set_imt(evt, (u32)p, c);

		for (i = 0; i < c; i++) {
			bool ret = bucket_put_entry(buckets, p[i]);
			if (ret) {
				ret = bucket_get_item(buckets, &bucket);
				sys_assert(ret == true);
				hdr_surc_push(&src->reg, src->ptr, bucket.all);
			}
		}

		reg->entry_pnter.b.rptr = wptr;
	}

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

UNUSED fast_code void l2p_pda_grp_rsp_ret(u32 *ptr_list, u32 count, u32 grp)
{
	u32 i;
	hdr_reg_t *reg;
	u32 ptr = 0;

	switch (grp) {
#if PDA_GEN_RSP0_QUE0_CPU_ID == CPU_ID
	case 0:
		reg = &_pda_gen_loc_que0.reg;
		ptr = _pda_gen_loc_que0.ptr;
		break;
#endif
#if PDA_GEN_RSP1_QUE0_CPU_ID == CPU_ID
	case 1:
		reg = &_pda_gen_loc_que1.reg;
		ptr = _pda_gen_loc_que1.ptr;
		break;
#endif
	default:
		reg = NULL;
		break;
	}
	sys_assert(reg);

	for (i = 0; i < count; i++)
		hdr_surc_push(reg, ptr, ptr_list[i]);
}

/*!
 * @brief function to handle response queue
 *
 * @param src		pointer to resource queue
 * @param que		pointer to the queue
 * @param evt		handle event
 *
 * @return	not used
 */
UNUSED fast_code static void l2p_isr_rsp_handle(l2p_que_t *src, l2p_que_t *que, u8 evt)
{
	hdr_reg_t *reg;
	u8 wptr;
	u32 *entry;

	reg = &que->reg;
	wptr = que->ptr;
	entry = (u32 *) reg->base;
	if (((wptr + 1) & reg->entry_dbase.b.max_sz) == reg->entry_pnter.b.rptr)
		lxp_mgr_trace(LOG_WARNING, 0x94e3, "%x full", que);

	if (reg->entry_pnter.b.rptr == wptr)
		return;

	if (reg->entry_pnter.b.rptr > wptr) {
		u32 *p = entry + reg->entry_pnter.b.rptr;
		u32 c = reg->entry_dbase.b.max_sz;

		c += 1;
		c -= reg->entry_pnter.b.rptr;

		evt_set_imt(evt, (u32)p, c);

		reg->entry_pnter.b.rptr = 0;
	}

	if (reg->entry_pnter.b.rptr < wptr) {
		u32 *p = entry + reg->entry_pnter.b.rptr;
		u32 c = wptr;

		c -= reg->entry_pnter.b.rptr;

		evt_set_imt(evt, (u32)p, c);

		reg->entry_pnter.b.rptr = wptr;
	}

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

/*!
 * @brief function to polling unmap queue
 *
 * @param _que		pointer to the queue
 * @param cookie	handle event
 *
 * @return	not used
 */
fast_code void srch_unmap_poll(l2p_que_t* _que, u8 cookie)
{
	l2p_srch_unmap_t *entry;
	hdr_reg_t *reg;
	l2p_que_t *que;
	u8 wptr;

	que = (l2p_que_t *) _que;
	reg = &que->reg;
	entry = (l2p_srch_unmap_t *) reg->base;
	wptr = que->ptr;
	if (((wptr + 1) & reg->entry_dbase.b.max_sz) == reg->entry_pnter.b.rptr) {
		lxp_mgr_trace(LOG_DEBUG, 0xb644, "unmap queue full");
	}

	if (reg->entry_pnter.b.rptr > wptr) {
		l2p_srch_unmap_t *p = entry + reg->entry_pnter.b.rptr;
		u32 c = reg->entry_dbase.b.max_sz;

		c += 1;
		c -= reg->entry_pnter.b.rptr;

		evt_set_imt(cookie, (u32)p, c);
		reg->entry_pnter.b.rptr = 0;
	}

	if (reg->entry_pnter.b.rptr < wptr) {
		l2p_srch_unmap_t *p = entry + reg->entry_pnter.b.rptr;
		u32 c = wptr;

		c -= reg->entry_pnter.b.rptr;
		evt_set_imt(cookie, (u32)p, c);
		reg->entry_pnter.b.rptr = wptr;
	}

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

#if UNMAP_QUE_CPU_ID == CPU_ID
fast_code UNUSED void l2p_srch_unmap_poll(void)
{
	srch_unmap_poll(&_srch_rsp_unmap, evt_l2p_srch_umap);
}
#endif

/*!
 * @brief polling group 0 search result and pda generation isr
 *
 * @return	not used
 */
fast_code void l2p_isr_q0_srch(void)
{
	btn_ftl_ncl_int_grp0_src_t src = {
		.all = l2p_readl(L2P_GRP0_INT_STS),
	};
#if SRCH_PDA_GRP0_CPU_ID == CPU_ID
	if (src.b.l2p_pda_rslt_urgent_que0_updt) {
		l2p_isr_rsp_recycle(&_srch_rsp0_surc, _srch_rsp0_buckets, &_srch_rsp0[SRCH_URG], evt_l2p_srch0);
	}

	if (src.b.l2p_pda_rslt_high_que0_updt) {
		l2p_isr_rsp_recycle(&_srch_rsp0_surc, _srch_rsp0_buckets, &_srch_rsp0[SRCH_HIG], evt_l2p_srch0);
	}

	if (src.b.l2p_pda_rslt_norm_que0_updt) {
		l2p_isr_rsp_recycle(&_srch_rsp0_surc, _srch_rsp0_buckets, &_srch_rsp0[SRCH_NRM], evt_l2p_srch0);
	}
#endif

#if UNMAP_QUE_CPU_ID == CPU_ID
	if (src.b.l2p_unmap_lda_que_updt) {
		srch_unmap_poll(&_srch_rsp_unmap, evt_l2p_srch_umap);
	}
#endif

	if (src.all & PDA_GEN_RST0_UPDT_MASK) {
		//u32 mask = src.all & PDA_GEN_RST0_UPDT_MASK;

		// debug
		//lxp_mgr_trace(LOG_ERR, 0, "pda gen rst 0 %x\n", mask >> PDA_GEN_RSLT0_QUE0_UPDT_SHIFT);
	}

#if PDA_GEN_RSP0_QUE0_CPU_ID == CPU_ID
	if (src.b.pda_gen_rslt0_que0_updt) {
		l2p_isr_rsp_handle(&_pda_gen_loc_que0, &_pda_gen_rsp0_que0, evt_pda_gen_rsp0_que0);
		//l2p_isr_rsp_recycle(&_pda_gen_loc_que0, &_pda_gen_rsp0_que0, evt_pda_gen_rsp0_que0);
	}
#endif

#if PDA_GEN_RSP0_QUE1_CPU_ID == CPU_ID
	if (src.b.pda_gen_rslt0_que1_updt) {
		l2p_isr_rsp_handle(&_pda_gen_loc_que0, &_pda_gen_rsp0_que1, evt_pda_gen_rsp0_que1);
	}
#endif

	lxp_mgr_trace(PRINT_LVL, 0x69e5, "%s\n", __FUNCTION__);
}

/*!
 * @brief polling group 1 search result and pda generation isr
 *
 * @return	not used
 */
fast_code void l2p_isr_q1_srch(void)
{
	btn_ftl_ncl_int_grp1_src_t src = {
		.all = l2p_readl(L2P_GRP1_INT_STS),
	};
#if SRCH_PDA_GRP1_CPU_ID == CPU_ID
#if defined(RDISK)
	cpu_msg_isr();
#endif
	if (src.b.l2p_pda_rslt_urgent_que1_updt) {
		l2p_isr_rsp_recycle(&_srch_rsp1_surc, _srch_rsp1_buckets, &_srch_rsp1[SRCH_URG], evt_l2p_srch1);
	}

	if (src.b.l2p_pda_rslt_high_que1_updt) {
		l2p_isr_rsp_recycle(&_srch_rsp1_surc, _srch_rsp1_buckets, &_srch_rsp1[SRCH_HIG], evt_l2p_srch1);
	}

	if (src.b.l2p_pda_rslt_norm_que1_updt) {
		l2p_isr_rsp_recycle(&_srch_rsp1_surc, _srch_rsp1_buckets, &_srch_rsp1[SRCH_NRM], evt_l2p_srch1);
	}
#endif

	if (src.all & PDA_GEN_RST1_UPDT_MASK) {
		//u32 mask = src.all & PDA_GEN_RST1_UPDT_MASK;

		// debug
		//lxp_mgr_trace(LOG_ERR, 0, "pda gen mask 1 %x\n", mask >> PDA_GEN_RSLT1_QUE0_UPDT_SHIFT);
	}

#if PDA_GEN_RSP1_QUE0_CPU_ID == CPU_ID
	if (src.b.pda_gen_rslt1_que0_updt) {
		l2p_isr_rsp_handle(&_pda_gen_loc_que1, &_pda_gen_rsp1_que0, evt_pda_gen_rsp1_que0);
	}
#endif

#if PDA_GEN_RSP1_QUE1_CPU_ID == CPU_ID
	if (src.b.pda_gen_rslt1_que1_updt) {
		l2p_isr_rsp_handle(&_pda_gen_loc_que1, &_pda_gen_rsp1_que1, evt_pda_gen_rsp1_que1);
	}
#endif

	lxp_mgr_trace(PRINT_LVL, 0xe2af, "%s\n", __FUNCTION__);
}

/*!
 * @brief polling update and commit complete queue 0/1 isr
 *
 * @return	not used
 */
fast_code void l2p_isr_q01_updt(void)
{
#ifdef L2P_UPDT_CMMT_Q01_CPU
	btn_ftl_ncl_int_grp0_src_t sts = {
		.all = l2p_readl(L2P_GRP0_INT_STS)
	};
#endif

	// update commit queue 0/1
#if UPDT_CMPL_QUE0_CPU_ID == CPU_ID
	if (sts.b.l2p_updt_compl_que0_updt)
		updt_cmpl_poll(&_updt_cpl_que0, evt_l2p_updt0);
#endif

#if CMMT_CMPL_QUE0_CPU_ID == CPU_ID
	if (sts.b.l2p_commit_compl_que0_updt)
		cmmt_cmpl_poll(&_cmmt_cpl_que0, evt_l2p_cmmt0);
#endif
#if UPDT_CMPL_QUE1_CPU_ID == CPU_ID
	if (sts.b.l2p_updt_compl_que1_updt)
		updt_cmpl_poll(&_updt_cpl_que1, evt_l2p_updt1);
#endif
#if CMMT_CMPL_QUE1_CPU_ID == CPU_ID
	if (sts.b.l2p_commit_compl_que1_updt)
		cmmt_cmpl_poll(&_cmmt_cpl_que1, evt_l2p_cmmt1);
#endif

#if RECYCLE_QUE_CPU_ID == CPU_ID
	if (sts.b.l2p_updt_recycle_que_updt)
		recycle_poll(&_recycle_que, evt_l2p_recycle_que);
#endif

	lxp_mgr_trace(PRINT_LVL, 0x7b32, "%s\n", __FUNCTION__);
}

/*!
 * @brief polling update and commit complete queue 2/3 isr
 *
 * @return	not used
 */
fast_code void l2p_isr_q23_updt(void)
{
#ifdef L2P_UPDT_CMMT_Q23_CPU
	btn_ftl_ncl_int_grp1_src_t sts = {
		.all = l2p_readl(L2P_GRP1_INT_STS)
	};
#endif
	// update queue 2/3, commit queue 2/3
#if UPDT_CMPL_QUE2_CPU_ID == CPU_ID
	if (sts.b.l2p_updt_compl_que2_updt)
		updt_cmpl_poll(&_updt_cpl_que2, evt_l2p_updt2);
#endif

#if CMMT_CMPL_QUE2_CPU_ID == CPU_ID
	if (sts.b.l2p_commit_compl_que2_updt)
		cmmt_cmpl_poll(&_cmmt_cpl_que2, evt_l2p_cmmt2);
#endif

#if UPDT_CMPL_QUE3_CPU_ID == CPU_ID
	if (sts.b.l2p_updt_compl_que3_updt)
		updt_cmpl_poll(&_updt_cpl_que3, evt_l2p_updt3);
#endif
#if CMMT_CMPL_QUE3_CPU_ID == CPU_ID
	if (sts.b.l2p_commit_compl_que3_updt)
		cmmt_cmpl_poll(&_cmmt_cpl_que3, evt_l2p_cmmt3);
#endif

	lxp_mgr_trace(PRINT_LVL, 0x1c90, "%s\n", __FUNCTION__);
}

/*!
 * @brief polling spb search and nand status update isr
 *
 * @return	not used
 */
fast_code void l2p_isr_ftl_others(void)
{
	UNUSED btn_ftl_ncl_int_grp0_src_t src = {
		.all = l2p_readl(BTN_FTL_NCL_INT_GRP0_SRC),
	};

#if STATUS_SRCH_RSP_QUE_CPU_ID == CPU_ID
	if (src.b.ftl_stat_cnt_srch_rslt_que_updt)
		rslt_poll(&_status_srch_rsp_que, evt_status_srch_rsp, ctz(sizeof(cnt_rsp_t)));
#endif

#if STATUS_FET_RSP_QUE_CPU_ID == CPU_ID
	if (src.b.ftl_stat_cnt_fet_rslt_que_updt)
		rslt_poll(&_status_fet_rsp_que, evt_status_fet_rsp, ctz(sizeof(fet_cnt_rsp_t)));
#endif

#if DVL_INFO_UPDT_CPL_QUE_CPU_ID == CPU_ID
	if (src.b.dvl_info_fw_updt_rslt_que_updt)
		rslt_poll(&_dvl_info_updt_cpl_que, evt_dvl_info_updt_rslt, ctz(sizeof(dvl_info_updt_cpl_t)));
#endif

#if STATUS_CNT_NOTIFY_QUE_CPU_ID == CPU_ID
	if (src.b.ftl_stat_cnt_notif_que_updt)
		rslt_poll(&_status_cnt_notify_que, evt_vcnt_notify, ctz(sizeof(cnt_notify_t)));
#endif

#if L2P_ERROR_CPU_ID == CPU_ID
	if (src.b.ftl_hw_accelr_if_err) {
		lxp_mgr_trace(LOG_ERR, 0x4e04, "src.all %x", src.all);
		lxp_mgr_trace(LOG_ERR, 0x08d7, "hw if err");
		l2p_writel(FTL_HW_ACCELR_IF_ERR_MASK, BTN_FTL_NCL_INT_GRP0_SRC);
		btn_ftl_ncl_error_status0_t status0 = { .all = l2p_readl(BTN_FTL_NCL_ERROR_STATUS0)};
		lxp_mgr_trace(LOG_ERR, 0xcb19, "error status0 %x", status0.all);	
		if(status0.b.ftl_l2p_upda_lda_ovfl_err!=1)//joe add test 20201223
		panic("hw if err");
	}

	if (src.b.ftl_hw_accelr_mem_err) {
		lxp_mgr_trace(LOG_ERR, 0xb3f5, "hw mem err");
		l2p_writel(FTL_HW_ACCELR_MEM_ERR_MASK, BTN_FTL_NCL_INT_GRP0_SRC);
	}

	if (src.b.ftl_hw_accelr_ovfl_err) {
		btn_ftl_ncl_error_status0_t err = { .all = l2p_readl(BTN_FTL_NCL_ERROR_STATUS0)};

		// if OLD=UNMAP, this will be asserted, so we filter it
		if (err.all != FTL_L2P_UPDT_SPB_ID_OVFL_ERR_MASK)
			lxp_mgr_trace(LOG_DEBUG, 0xbf92, "hw overflow err %x", err.all);

		l2p_writel(err.all, BTN_FTL_NCL_ERROR_STATUS0);
		//l2p_writel(FTL_HW_ACCELR_OVFL_ERR_MASK, BTN_FTL_NCL_INT_GRP0_SRC);
	}
#endif
}

#undef PRINT_LVL
#define HDR_REG_INIT(Q, QSZ, REQS, REG_OFF)	\
	hdr_reg_init(&Q->reg, REQS, QSZ, &Q->ptr, L2P_BASE + REG_OFF)

ps_code UNUSED void l2p_srch_req_q_setup(l2p_srch_pri_t pri, u32 qsize,
		l2p_que_t *que, srch_req_t *reqs, u32 qid)
{
	u32 regs[2][3] = {{
			BTN_L2P_NORM_SRCH_QUE0_BASE_ADDR,
			BTN_L2P_HIGH_SRCH_QUE0_BASE_ADDR,
			BTN_L2P_URGENT_SRCH_QUE0_BASE_ADDR},
			{
			BTN_L2P_NORM_SRCH_QUE1_BASE_ADDR,
			BTN_L2P_HIGH_SRCH_QUE1_BASE_ADDR,
			BTN_L2P_URGENT_SRCH_QUE1_BASE_ADDR}};

	HDR_REG_INIT(que, qsize, reqs, regs[qid][pri]);

	if (que->swq_id == ~0)
		que->swq_id = l2p_sw_que_init(sizeof(*reqs), SWQ_SZ_SRCH, que);
}

ps_code UNUSED void l2p_srch_unmap_q_setup(u32 qsize, l2p_que_t *que,
		l2p_srch_unmap_t *rsps)
{
	HDR_REG_INIT(que, qsize, rsps, BTN_L2P_UNMAP_LDA_QUE_BASE_ADDR);
}

ps_code UNUSED void l2p_srch_loc_q_setup(u32 qsize, l2p_que_t *que,
		u32 *locs, u32 qid)
{
	u32 regs[] = {BTN_L2P_PDA_RSLT_LOC_PTR_QUE0_BASE_ADDR,
			BTN_L2P_PDA_RSLT_LOC_PTR_QUE1_BASE_ADDR};

	HDR_REG_INIT(que, qsize, locs, regs[qid]);
}

ps_code UNUSED void l2p_srch_cmpl_q_setup(l2p_srch_pri_t pri, u32 qsize,
		l2p_que_t *que, u32 *rsps, u32 gid)
{
	u32 regs[2][3] = {
			{
			BTN_L2P_PDA_RSLT_NORM_QUE0_BASE_ADDR,
			BTN_L2P_PDA_RSLT_HIGH_QUE0_BASE_ADDR,
			BTN_L2P_PDA_RSLT_URGENT_QUE0_BASE_ADDR},
			{
			BTN_L2P_PDA_RSLT_NORM_QUE1_BASE_ADDR,
			BTN_L2P_PDA_RSLT_HIGH_QUE1_BASE_ADDR,
			BTN_L2P_PDA_RSLT_URGENT_QUE1_BASE_ADDR}};

	HDR_REG_INIT(que, qsize, rsps, regs[gid][pri]);
}

ps_code UNUSED void l2p_updt_cmpl_q_setup(u32 qsize, l2p_que_t *que,
		updt_cpl_t *reqs, u32 qid)
{
	u32 regs[] = {BTN_L2P_UPDT_COMPL_QUE0_BASE_ADDR,
			BTN_L2P_UPDT_COMPL_QUE1_BASE_ADDR,
			BTN_L2P_UPDT_COMPL_QUE2_BASE_ADDR,
			BTN_L2P_UPDT_COMPL_QUE3_BASE_ADDR};

	HDR_REG_INIT(que, qsize, reqs, regs[qid]);
}

ps_code UNUSED void l2p_cmmt_cmpl_q_setup(u32 qsize, l2p_que_t *que,
		cmmt_cpl_t *reqs, u32 qid)
{
	u32 regs[] = {BTN_L2P_COMMIT_COMPL_QUE0_BASE_ADDR,
			BTN_L2P_COMMIT_COMPL_QUE1_BASE_ADDR,
			BTN_L2P_COMMIT_COMPL_QUE2_BASE_ADDR,
			BTN_L2P_COMMIT_COMPL_QUE3_BASE_ADDR};

	HDR_REG_INIT(que, qsize, reqs, regs[qid]);
}

ps_code UNUSED void l2p_pda_gen_req_q_setup(u32 qsize, l2p_que_t *que, void *reqs, u32 qid)
{
	u32 regs[] = {
			BTN_PDA_GEN_REQ_QUE0_BASE_ADDR,
			BTN_PDA_GEN_REQ_QUE1_BASE_ADDR,
			BTN_PDA_GEN_REQ_QUE2_BASE_ADDR,
			BTN_PDA_GEN_REQ_QUE3_BASE_ADDR,
			BTN_PDA_GEN_REQ_QUE4_BASE_ADDR,
			BTN_PDA_GEN_REQ_QUE5_BASE_ADDR,
			BTN_PDA_GEN_REQ_QUE6_BASE_ADDR,
			BTN_PDA_GEN_REQ_QUE7_BASE_ADDR
	};
	HDR_REG_INIT(que, qsize, reqs, regs[qid]);
}

ps_code UNUSED void l2p_pda_gen_loc_q_setup(u32 qsize, l2p_que_t *que, u32 *locs, u32 gid)
{
	u32 regs[] = {
			BTN_PDA_GEN_RSLT0_LOC_PTR_QUE_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_LOC_PTR_QUE_BASE_ADDR
	};

	HDR_REG_INIT(que, qsize, locs, regs[gid]);
}

ps_code UNUSED void l2p_pda_gen_rsp0_q_setup(u32 qsize, l2p_que_t *que, u32 *rsps, u32 qid)
{
	u32 regs[] = {
			BTN_PDA_GEN_RSLT0_QUE0_BASE_ADDR,
			BTN_PDA_GEN_RSLT0_QUE1_BASE_ADDR,
			BTN_PDA_GEN_RSLT0_QUE2_BASE_ADDR,
			BTN_PDA_GEN_RSLT0_QUE3_BASE_ADDR,
			BTN_PDA_GEN_RSLT0_QUE4_BASE_ADDR,
			BTN_PDA_GEN_RSLT0_QUE5_BASE_ADDR,
			BTN_PDA_GEN_RSLT0_QUE6_BASE_ADDR,
			BTN_PDA_GEN_RSLT0_QUE7_BASE_ADDR
	};

	HDR_REG_INIT(que, qsize, rsps, regs[qid]);
}

ps_code UNUSED void l2p_pda_gen_rsp1_q_setup(u32 qsize, l2p_que_t *que, u32 *rsps, u32 qid)
{
	u32 regs[] = {
			BTN_PDA_GEN_RSLT1_QUE0_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_QUE1_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_QUE2_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_QUE3_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_QUE4_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_QUE5_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_QUE6_BASE_ADDR,
			BTN_PDA_GEN_RSLT1_QUE7_BASE_ADDR
	};

	HDR_REG_INIT(que, qsize, rsps, regs[qid]);
}

/*!
 * @brief interface to get search queue
 *
 * @param qid	queue id
 * @param pri	queue priority
 *
 * @return	pointer to the queue
 */
static inline l2p_que_t *get_srch_que(u32 qid, l2p_srch_pri_t pri)
{
#if SRCH_QUE0_NRM_CPU_ID == CPU_ID
	if (qid == 0 && pri == SRCH_NRM)
		return &_srch_req0_nrm;
#endif
#if SRCH_QUE0_HIGH_CPU_ID == CPU_ID
	if (qid == 0 && pri == SRCH_HIG)
		return &_srch_req0_high;
#endif
#if SRCH_QUE0_URG_CPU_ID == CPU_ID
	if (qid == 0 && pri == SRCH_URG)
		return &_srch_req0_urg;
#endif
#if SRCH_QUE1_NRM_CPU_ID == CPU_ID
	if (qid == 1 && pri == SRCH_NRM)
		return &_srch_req1_nrm;
#endif
#if SRCH_QUE1_HIGH_CPU_ID == CPU_ID
	if (qid == 1 && pri == SRCH_HIG)
		return &_srch_req1_high;
#endif
#if SRCH_QUE1_URG_CPU_ID == CPU_ID
	if (qid == 1 && pri == SRCH_URG)
		return &_srch_req1_urg;
#endif
	return NULL;
}

fast_code u32 l2p_single_srch(lda_t lda, u32 ofst, u32 btn_tag, u32 qid, l2p_srch_pri_t pri)
{
	srch_req_t *req;
	hdr_reg_t *reg;
	int idx;
	int next = -1;
	l2p_que_t *que = get_srch_que(qid, pri);
	bool use_swq = false;

	debug_lrange(lda, NULL, 1, __LINE__);

	sys_assert(que);
	reg = &que->reg;

	use_swq = l2p_swq_check(que->swq_id);

again:
	if (use_swq == false) {
		next = _req_submit(que, &idx, false);
		if (next == -1) {
			use_swq = true;
			goto again;
		}
		req = (srch_req_t *) reg->base;
		req += idx;
	} else {
		req = (srch_req_t *) l2p_sw_que_get_next_req(que->swq_id);
		if (req == NULL){
            sys_assert(0);
			return 0;
		}else{
			if(srhq_avail_cnt)
				srhq_avail_cnt--;
		}
	}

	req->btag = btn_tag;
	req->lda = lda;
	req->num = 0; // zero based
	req->ofst = ofst;
	req->region = 0;

	if (use_swq == false) {
		sys_assert(next != -1);
		reg->entry_pnter.b.wptr = (u16) next;
		writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	} else {
		l2p_sw_que_submit(que->swq_id);
	}

	return 1;
}

#define RDISK_L2P_FE_SRCH_QUE	   0	   ///< l2p search queue id
fast_code bool l2p_srch_swq_check(void){
	return (srhq_avail_cnt < L2P_SRCHQ_AVAIL_CNT_MIN ? true:false);
}

fast_code u32 l2p_srch_ofst(lda_t lda, u32 cnt, u32 btn_tag, u32 ofst, u32 qid, l2p_srch_pri_t pri)
{
	srch_req_t *req;
	hdr_reg_t *reg;
	int idx;
	int next = -1;
	u32 issued = 0;
	l2p_que_t *que = get_srch_que(qid, pri);
	bool use_swq = false;

	sys_assert(que);
	reg = &que->reg;

	use_swq = l2p_swq_check(que->swq_id);

	do {
		u32 lc = min(cnt - issued, MAX_L2P_SRCH_NUM);
		sys_assert(lc != 0);

	again:
		if (use_swq == false) {
			next = _req_submit(que, &idx, false);
			if (next == -1) {
				use_swq = true;
				goto again;
			}
			req = (srch_req_t *) reg->base;
			req += idx;
		} else {
			req = (srch_req_t *) l2p_sw_que_get_next_req(que->swq_id);
			if (req == NULL){
                sys_assert(0);
				return issued;
			}else{
				if(srhq_avail_cnt)
					srhq_avail_cnt--;
			}
		}

		req->btag = btn_tag;
		req->lda = lda;
		req->num = lc - 1; // zero based
		req->ofst = ofst;
		req->region = 0;

		if (use_swq == false) {
			sys_assert(next != -1);
			reg->entry_pnter.b.wptr = (u16) next;
			writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
		} else {
			l2p_sw_que_submit(que->swq_id);
		}

		lda += lc;
		issued += lc;
		ofst += lc;
	} while (issued < cnt);

	return issued;
}

fast_code u32 l2p_srch(lda_t lda, u32 cnt, u32 btn_tag, u32 qid, l2p_srch_pri_t pri)
{
	srch_req_t *req;
	hdr_reg_t *reg;
	int idx;
	int next = -1;
	u32 issued = 0;
	l2p_que_t *que = get_srch_que(qid, pri);
	bool use_swq = false;

	debug_lrange(lda, NULL, cnt, __LINE__);

	sys_assert(que);
	reg = &que->reg;

	use_swq = l2p_swq_check(que->swq_id);

	do {
		u32 lc = min(cnt - issued, MAX_L2P_SRCH_NUM);
		sys_assert(lc != 0);

	again:
		if (use_swq == false) {
			next = _req_submit(que, &idx, false);
			if (next == -1) {
				use_swq = true;
				goto again;
			}
			req = (srch_req_t *) reg->base;
			req += idx;
		} else {
			req = (srch_req_t *) l2p_sw_que_get_next_req(que->swq_id);
			if (req == NULL){
				return issued;
			}else{
				if(srhq_avail_cnt)
					srhq_avail_cnt--;
			}
		}

		req->btag = btn_tag;
		req->lda = lda;
		req->num = lc - 1; // zero based
		req->ofst = issued;
		req->region = 0;

		if (use_swq == false) {
			sys_assert(next != -1);
			reg->entry_pnter.b.wptr = (u16) next;
			writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
		} else {
			l2p_sw_que_submit(que->swq_id);
		}

		lda += lc;
		issued += lc;
	} while (issued < cnt);

	return issued;
}


/*!
 * @brief interface to get update queue
 *
 * @param qid	queue id
 *
 * @return	pointer to the queue
 */
static inline l2p_que_t *get_updt_que(u32 qid)
{
	l2p_que_t *que;

	switch (qid) {
	case 0:
#if UPDT_REQ_QUE0_CPU_ID == CPU_ID
		que = &_updt_req_que0;
		break;
#endif
	case 1:
#if UPDT_REQ_QUE1_CPU_ID == CPU_ID
		que = &_updt_req_que1;
		break;
#endif
	case 2:
#if UPDT_REQ_QUE2_CPU_ID == CPU_ID
		que = &_updt_req_que2;
		break;
#endif
	case 3:
#if UPDT_REQ_QUE3_CPU_ID == CPU_ID
		que = &_updt_req_que3;
		break;
#endif
	default:
		que = NULL;
		break;
	}
	return que;
}

ps_code UNUSED void l2p_updt_req_q_setup(u32 qsize, l2p_que_t *que,
		updt_req_t *reqs, u32 qid)
{
	u32 regs[] = {BTN_L2P_UPDT_REQ_QUE0_BASE_ADDR,
			BTN_L2P_UPDT_REQ_QUE1_BASE_ADDR,
			BTN_L2P_UPDT_REQ_QUE2_BASE_ADDR,
			BTN_L2P_UPDT_REQ_QUE3_BASE_ADDR};

	HDR_REG_INIT(que, qsize, reqs, regs[qid]);
#if !defined(FPGA)
	if (que->swq_id == ~0)
		que->swq_id = l2p_sw_que_init(sizeof(*reqs), SWQ_SZ_UPDT, que);
#endif
}


/*!
 * @brief interface to get next fda
 *
 * @param pri	queue priority
 *
 * @return	nexst fda
 */
fast_code static inline void next_l2p_fda(l2p_fda_t *fda)
{
	if (fda->dw0.b.pda_du < _max_fda.dw0.b.pda_du) {
		fda->dw0.b.pda_du++;
		return;
	}
	fda->dw0.b.pda_du = 0;
	if (fda->dw0.b.pda_pl < _max_fda.dw0.b.pda_pl) {
		fda->dw0.b.pda_pl++;
		return;
	}
	fda->dw0.b.pda_pl = 0;
	if (fda->dw0.b.pda_ch < _max_fda.dw0.b.pda_ch) {
		fda->dw0.b.pda_ch++;
		return;
	}
	fda->dw0.b.pda_ch = 0;
	if (fda->dw0.b.pda_ce < _max_fda.dw0.b.pda_ce) {
		fda->dw0.b.pda_ce++;
		return;
	}
	fda->dw0.b.pda_ce = 0;
	if (fda->dw0.b.pda_lun < _max_fda.dw0.b.pda_lun) {
		fda->dw0.b.pda_lun++;
		return;
	}
	fda->dw0.b.pda_lun = 0;
	if (fda->dw0.b.pda_pg < _max_fda.dw0.b.pda_pg) {
		fda->dw0.b.pda_pg++;
		return;
	}
	panic("cross spb");
}

fast_code void l2p_updt_pdas(lda_t *lda, pda_t new_pda, pda_t *pda, u32 cnt, u32 qid, bool cmpr)
{
	updt_req_t *req;
	hdr_reg_t *reg;
	u32 dis;
	u32 i;
	l2p_que_t *que = get_updt_que(qid);
	u16 wptr;

	reg = &que->reg;
	do {
		dis = l2p_req_que_distance(que);
	} while (dis < cnt);

	req = (updt_req_t *) reg->base;
	wptr = reg->entry_pnter.b.wptr;
	for (i = 0; i < cnt; i++) {
		if (lda[i] == INV_LDA)
			continue;

		req[wptr] = _pda_updt;

		req[wptr].updt.lda = lda[i];
		req[wptr].updt.compare = cmpr;
		if (cmpr) {
			req[wptr].updt.old_val = pda[i];
			req[wptr].updt.fw_seq_id = 0xFFF;
		} else {
			req[wptr].updt.fw_seq_id = pda[i];
		}

		req[wptr].updt.pda = new_pda + i;
		wptr = (wptr + 1) & reg->entry_dbase.b.max_sz;
	}
	reg->entry_pnter.b.wptr = wptr;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void l2p_updt_pda(lda_t lda, pda_t new_pda, pda_t pda,
		bool cmp, bool adj, u32 qid, u32 id)
{
	updt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
	l2p_que_t *que = get_updt_que(qid);
	bool use_swq = false;

	sys_assert(que);
	reg = &que->reg;

again:
	use_swq = l2p_swq_check(que->swq_id);
	if (use_swq == false) {
		next = _req_submit(que, &idx, false);
		if (next == -1)
			goto again;

		req = (updt_req_t *) reg->base;
		req += idx;
	} else {
		req = (updt_req_t *) l2p_sw_que_get_next_req(que->swq_id);
		if (req == NULL)
			goto again;
	}

	if (cmp) {
		req->updt.old_val = pda;
		req->updt.compare = true;
	} else {
		req->updt.compare = false;
	}

	req->updt.lda = lda;
	req->updt.dtag_or_pda = 0;
	// update valid cnt adjust should be input value of this function
	req->updt.valid_cnt_adjust = adj;
	req->updt.lda_num = 0;
	req->updt.pda = new_pda;
	req->updt.region = 0;
	req->updt.req_type = 0;
	req->updt.fw_seq_id = id;

	if (use_swq == false) {
		reg->entry_pnter.b.wptr = (u16) next;
		writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	} else {
		l2p_sw_que_submit(que->swq_id);
	}
}

#if 0 //debug
fast_code void l2p_que_dump(u32 reg)
{
	lxp_mgr_trace(LOG_ERR, 0x0e07, ": %x %x %x\n", l2p_readl(reg), l2p_readl(reg + 4), l2p_readl(reg + 8));
}

fast_code void l2p_mgr_dump(void)
{
	lxp_mgr_trace(LOG_ERR, 0x2883, "srch req nrm");
	l2p_que_dump(BTN_L2P_NORM_SRCH_QUE0_BASE_ADDR);

	lxp_mgr_trace(LOG_ERR, 0x150b, "updt req0");
	l2p_que_dump(BTN_L2P_UPDT_REQ_QUE1_BASE_ADDR);
}
#endif

fast_code void l2p_updt_trim(lda_t lda, l2p_trim_pat_t pat, bool adj, u32 num, u32 qid, u32 id)
{
	updt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
	l2p_que_t *que = get_updt_que(qid);
	bool use_swq = false;

	debug_lrange(lda, NULL, num, __LINE__);

	sys_assert(num <= MAX_L2P_TRIM_NUM);

	reg = &que->reg;
noque:
	use_swq = l2p_swq_check(que->swq_id);
again:
	if (use_swq == false) {
		next = _req_submit(que, &idx, false);
		if (next == -1) {
		#if !defined(FPGA)
			use_swq = true;
		#endif
			goto again;
		}
		req = (updt_req_t *) reg->base;
		req += idx;
	} else {
		req = (updt_req_t *) l2p_sw_que_get_next_req(que->swq_id);
		if(req==NULL){//joe 20210209 modify queue1 full flow
			btn_ftl_ncl_int_grp0_src_t sts1 = {
			.all = l2p_readl(L2P_GRP0_INT_STS)
				};
			sts1.b.l2p_updt_compl_que1_updt=1;
			sts1.b.l2p_commit_compl_que1_updt=1;
			l2p_writel(sts1.all, L2P_GRP0_INT_STS);
            l2p_isr_ftl_others();
			l2p_isr_q01_updt();
			goto noque;
		}
		//req = (updt_req_t *) l2p_sw_que_get_next_req(que->swq_id);
		//sys_assert(req);
	}

	req->trim.fw_seq_id = id;
	req->trim.lda = lda;
	req->trim.lda_num = num - 1;
	req->trim.pattern = pat;
	req->trim.req_type = 2;
	req->trim.valid_cnt_adjst = adj;

	if (use_swq == false) {
		reg->entry_pnter.b.wptr = (u16) next;
		writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	} else {
		l2p_sw_que_submit(que->swq_id);
	}
}

fast_code void l2p_updt_bulk(u32 *new, lda_t lda, lda_t *lda_list, u32 cnt, u32 *old, bool dtag, bool adj, u32 qid, u32 id)
{
	updt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
	l2p_que_t *que = get_updt_que(qid);
	bool use_swq = false;

	debug_lrange(lda, lda_list, cnt, __LINE__);

	sys_assert(cnt <= 32);

	reg = &que->reg;
noq:
	use_swq = l2p_swq_check(que->swq_id);
again:
	if (use_swq == false) {
		next = _req_submit(que, &idx, false);
		if (next == -1) {
		#if !defined(FPGA)
			use_swq = true;
		#endif
			goto again;
		}
		req = (updt_req_t *) reg->base;
		req += idx;
	} else {
		req = (updt_req_t *) l2p_sw_que_get_next_req(que->swq_id);
		if(req==NULL){//joe 20210209 modify queue1 full flow
			btn_ftl_ncl_int_grp0_src_t sts2 = {
			.all = l2p_readl(L2P_GRP0_INT_STS)
				};
			sts2.b.l2p_updt_compl_que1_updt=1;
			sts2.b.l2p_commit_compl_que1_updt=1;
			l2p_writel(sts2.all, L2P_GRP0_INT_STS);
            l2p_isr_ftl_others();
			l2p_isr_q01_updt();
			goto noq;
		}
		//sys_assert(req);
	}

	if (old) {
		if (cnt == 1 || (dtag && lda_list == NULL))
			req->updt.old_val = old[0];
		else
			req->updt.old_val = (u32) btcm_to_dma(old);
		if (dtag == false)
			req->updt.compare = true;
		else
			req->updt.compare = false;
	} else {
		req->updt.compare = false;
	}

	if (cnt == 1)
		req->updt.lda = lda_list ? lda_list[0] : lda;
	else
		req->updt.lda = (lda_list != NULL) ? (u32) btcm_to_dma(lda_list) : lda;

	req->updt.dtag_or_pda = dtag;
	// update valid cnt adjust should be input value of this function
	req->updt.valid_cnt_adjust = adj;

	if (cnt == 1)
		req->updt.pda = new[0];
	else
		req->updt.pda = (u32) btcm_to_dma(new);
	req->updt.lda_auto_inc = lda_list ? 0 : 1;
	req->updt.lda_num = cnt - 1;
	req->updt.region = 0;
	req->updt.req_type = 0;
	req->updt.fw_seq_id = id;

	if (use_swq == false) {
		reg->entry_pnter.b.wptr = (u16) next;
		writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	} else {
		l2p_sw_que_submit(que->swq_id);
	}
}

/*!
 * @brief l2p dtag update request template
 */
fast_data updt_req_t _dtag_updt = { .updt.lda = 0, .updt.old_val = INV_PDA,
		.updt.valid_cnt_adjust = 1, .updt.dtag_or_pda = 1,
		.updt.region = 0, .updt.compare = 0,
		.updt.req_type = 0};

/*!
 * @brief l2p dtag commit request template
 */
fast_data updt_req_t _dtag_cmmt = { .cmmt.lda = 0,
		.cmmt.dtag_id = 0,
		.cmmt.ofst = 0,
		.cmmt.btag = 0,
		.cmmt.req_type = 1};

fast_code void l2p_updt_dtags(u32 *dtag_list, lda_t *lda_list, u16 *btag_list, u32 cnt, u32 qid)
{
	l2p_que_t *que = get_updt_que(qid);
	updt_req_t *req;
	hdr_reg_t *reg;
	u32 i;
	volatile u32 dis;
	u32 wptr;

	reg = &que->reg;

	do {
		dis = l2p_req_que_distance(que);
	} while (dis < cnt);

	wptr = reg->entry_pnter.b.wptr;
	req = (updt_req_t *) reg->base;

	for (i = 0; i < cnt; i++) {
		_dtag_cmmt.cmmt.lda = lda_list[i];
		_dtag_cmmt.cmmt.dtag_id = dtag_list[i];
		req[wptr] = _dtag_cmmt;
		wptr = (wptr + 1) & reg->entry_dbase.b.max_sz;
	}
	reg->entry_pnter.b.wptr = wptr;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void l2p_updt_dtag(lda_t lda, dtag_t dtag, u32 btag, u32 lda_ofst, bool adj, u32 qid, u32 id)
{
	updt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
	l2p_que_t *que = get_updt_que(qid);
	bool use_swq = false;

	reg = &que->reg;

	use_swq = l2p_swq_check(que->swq_id);
again:
	if (use_swq == false) {
		next = _req_submit(que, &idx, false);
		if (next == -1) {
		#if !defined(FPGA)
			use_swq = true;
		#endif
			goto again;
		}
		req = (updt_req_t *) reg->base;
		req += idx;
	} else {
		req = (updt_req_t *) l2p_sw_que_get_next_req(que->swq_id);
		sys_assert(req);
	}

	req->updt.dtag_updt.btag = btag;
	req->updt.dtag_updt.lda_ofst = lda_ofst;
	req->updt.compare = false; // try
	req->updt.lda_num = 0;
	req->updt.lda = lda;
	req->updt.dtag_or_pda = 1;
	req->updt.valid_cnt_adjust = adj;
	req->updt.region = 0;
	req->updt.req_type = 0;
	req->updt.fw_seq_id = id;
	req->updt.dtag = dtag.dtag;

	if (use_swq == false) {
		reg->entry_pnter.b.wptr = (u16) next;
		writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	} else {
		l2p_sw_que_submit(que->swq_id);
	}
}

/*!
 * @brief l2p pda commit request template
 */
fast_data updt_req_t _cmmt_pda = {
		.cmmt.lda = 0,
		.cmmt.dtag_id = 0,
		.cmmt.ofst = 0,
		.cmmt.btag = 0,
		.cmmt.rsvd_56 = 0,
		.cmmt.rsvd_88 = 0,
		.cmmt.rsvd_106 = 0,
		.cmmt.fw_seq_id = 0,
		.cmmt.req_type = 1,
};

fast_code void l2p_cmmt_pdas(lda_t *llist, u32 *dtags, u32 cnt, u32 qid)
{
	l2p_que_t *que = get_updt_que(qid);
	updt_req_t *req;
	hdr_reg_t *reg;
	u32 i;
	volatile u32 dis;
	u32 wptr;

	reg = &que->reg;

	do {
		dis = l2p_req_que_distance(que);
	} while (dis < cnt);

	wptr = reg->entry_pnter.b.wptr;
	req = (updt_req_t *) reg->base;

	for (i = 0; i < cnt; i++) {
		if (llist[i] == INV_LDA)
			continue;

		_cmmt_pda.cmmt.lda = llist[i];
		_cmmt_pda.cmmt.dtag_id = dtags[i];

		req[wptr] = _cmmt_pda;
		wptr = (wptr + 1) & reg->entry_dbase.b.max_sz;
	}
	reg->entry_pnter.b.wptr = wptr;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void l2p_cmmt(lda_t lda, dtag_t dtag, u32 btag, u32 ofst, bool cmp,
		u32 seq_id, u32 qid)
{
	updt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
	l2p_que_t *que = get_updt_que(qid);
	bool use_swq = false;

	reg = &que->reg;

	use_swq = l2p_swq_check(que->swq_id);
again:
	if (use_swq == false) {
		next = _req_submit(que, &idx, false);
		if (next == -1) {
		#if !defined(FPGA)
			use_swq = true;
		#endif
			goto again;
		}
		req = (updt_req_t *) reg->base;
		req += idx;
	} else {
		req = (updt_req_t *) l2p_sw_que_get_next_req(que->swq_id);
		sys_assert(req);
	}

	req->cmmt.btag = btag;
	req->cmmt.dtag_id = dtag.dtag;
	req->cmmt.lda = lda;
	req->cmmt.req_type = 1;
	req->cmmt.ofst = ofst;
	req->cmmt.fw_seq_id = seq_id;

	if (use_swq == false) {
		reg->entry_pnter.b.wptr = (u16) next;
		writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	} else {
		l2p_sw_que_submit(que->swq_id);
	}
}

fast_code void dvl_info_updt(dtag_t dtag, pda_t cpda, bool inv)
{
	dvl_info_updt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
#if DVL_INFO_UPDT_REQ_QUE_CPU_ID == CPU_ID
	l2p_que_t *que = &_dvl_info_updt_req_que;
#else
	l2p_que_t *que = NULL;
#endif

	reg = &que->reg;
	next = _req_submit(que, &idx, 1);
	sys_assert(next != -1);
	req = (dvl_info_updt_req_t *) reg->base;
	req += idx;

	req->cpda = cpda;
	req->dtag = dtag.dtag;
	req->inv = inv;

	reg->entry_pnter.b.wptr = (u16) next;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void p2l_valid_pda_gen(dtag_t dtag, u32 num, u32 p2l_id, u32 spb_id, u32 seq, u32 qid)
{
	p2l_pda_gen_req_t *req;
	l2p_que_t *que;
	hdr_reg_t *reg;
	int next;
	int idx;

	switch (qid) {
#if PDA_GEN_QUE0_CPU_ID == CPU_ID
	case 0:
		que = &_pda_gen_req_que0;
		break;
#endif
#if PDA_GEN_QUE1_CPU_ID == CPU_ID
	case 1:
		que = &_pda_gen_req_que1;
		break;
#endif
	default:
		que = NULL;
		sys_assert(0);
		break;
	};

	reg = &que->reg;
	next = _req_submit(que, &idx, 1);

	sys_assert(next != -1);
	req = (p2l_pda_gen_req_t *) reg->base;
	req += idx;

	req->dtag_id = dtag.dtag;
	req->dtag_num = num - 1;
	req->spb_id = spb_id;
	req->p2l_id = p2l_id;
	req->fw_seq_num = seq;

	reg->entry_pnter.b.wptr = (u16) next;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void valid_pda_gen(u32 spb_id, u32 ch, u32 ce, u32 lun, bool die_base, u32 qid)
{
	pda_gen_req_t *req;
	l2p_que_t *que;
	hdr_reg_t *reg;
	int next;
	int idx;

	switch (qid) {
#if PDA_GEN_QUE0_CPU_ID == CPU_ID
	case 0:
		que = &_pda_gen_req_que0;
		break;
#endif
	default:
		que = NULL;
		sys_assert(0);
		break;
	};

	reg = &que->reg;
	next = _req_submit(que, &idx, 1);

	sys_assert(next != -1);
	req = (pda_gen_req_t *) reg->base;
	req += idx;

	if (die_base) {
		req->ce = ce;
		req->ch = ch;
		req->lun = lun;
	}
	req->spb = spb_id;
	req->pde_gen_mode_sel = die_base;

	reg->entry_pnter.b.wptr = (u16) next;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void l2p_status_cnt_fetch(u32 spb_id, u32 ch_id, u32 ce_id)
{
	fet_cnt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
#if STATUS_FET_REQ_QUE_CPU_ID == CPU_ID
	l2p_que_t *que = &_status_fet_req_que;
#else
	l2p_que_t *que = NULL;
#endif

	reg = &que->reg;
	next = _req_submit(que, &idx, 1);
	sys_assert(next != -1);
	req = (fet_cnt_req_t *) reg->base;
	req += idx;

	req->fet_cnt_type = 0;
	req->spb_id = spb_id;
	req->ch_id = ch_id;
	req->ce_id = ce_id;

	reg->entry_pnter.b.wptr = (u16) next;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void l2p_status_cnt_srch(u32 grp, u32 mask0, u32 mask1, u32 from, u32 cnt, status_srch_op_t op, u32 seq_id)
{
	//todo search other counter, now only valid count
	sys_assert(0 == grp);
	cnt_req_t *req;
	hdr_reg_t *reg;
	int next;
	int idx;
#if STATUS_SRCH_REQ_QUE_CPU_ID == CPU_ID
	l2p_que_t *que = &_status_srch_req_que;
#else
	l2p_que_t *que = NULL;
#endif

	reg = &que->reg;
	next = _req_submit(que, &idx, 1);
	sys_assert(next != -1);
	req = (cnt_req_t *) reg->base;
	req += idx;

	req->fw_seq_id = seq_id;
	req->mask0 = mask0;
	req->mask1 = mask1;
	req->srch_cnt = cnt;
	req->srch_num = from;
	req->srch_op_type = op;
	req->start_entry_ptr = grp;

	reg->entry_pnter.b.wptr = (u16) next;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

// try to improve it
fast_code UNUSED void rslt_poll(l2p_que_t* srch_rslt, u8 evt, u32 ent_sft)
{
	hdr_reg_t *reg = &(srch_rslt->reg);
	void *entry  = (void *)reg->base;
	u16 wptr = srch_rslt->ptr;

	while (wptr != reg->entry_pnter.b.rptr) {
		u32 p = (u32) entry;

		p += reg->entry_pnter.b.rptr << ent_sft;

		evt_set_imt(evt, p, 1);
		reg->entry_pnter.b.rptr = (reg->entry_pnter.b.rptr + 1) &
				reg->entry_dbase.b.max_sz;
	}
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

xinit_code void l2p_ltmode_enable(u32 off, u32 bits, bool en)
{
	btn_ftl_ltmode_config_t config = { .all = 0 };
	u32 pda_mask;
	if (en) {
		config.b.ftl_ltmode_en = 1;
		config.b.ftl_ltmode_spb_bw = bits;
		config.b.ftl_ltmode_spb_offset = off;
		pda_mask = ~0;

	} else {
		btn_pda_conf_reg0_t conf_reg0 = { .all = l2p_readl(BTN_PDA_CONF_REG0) };

		pda_mask = (1 << conf_reg0.b.ftl_pda_vld_msb) - 1;
	}
	l2p_writel(pda_mask, BTN_PDA_MASK);
	l2p_writel(INV_PDA & pda_mask, BTN_UNMAPPED_PDA0);
	l2p_writel(UNMAP_PDA & pda_mask, BTN_UNMAPPED_PDA1);
	l2p_writel(ERR_PDA & pda_mask, BTN_UNMAPPED_PDA2);
	l2p_writel(ZERO_PDA & pda_mask, BTN_UNMAPPED_PDA3);

	l2p_writel(config.all, BTN_FTL_LTMODE_CONFIG);
}

#define ZERO_BASED 1
xinit_code pda_t l2p_addr_init(u32 ch, u32 ce, u32 lun, u32 pl,
		u32 blk, u32 page, u32 du_cnt, bool pda_mode)
{
	pda_t max_pda;
	u32 i;
	u32 interleave;
	u32 aligned_page;
	u32 aligned_blk;

	_max_fda.dw0.b.pda_ce = ce - 1;
	_max_fda.dw0.b.pda_ch = ch - 1;
	_max_fda.dw0.b.pda_du = du_cnt - 1;
	_max_fda.dw0.b.pda_lun = lun - 1;
	_max_fda.dw0.b.pda_pg = page - 1;
	_max_fda.dw0.b.pda_pl = pl - 1;

	btn_ftl_ncl_ctrl1_t ctrl1 = {
		.b = {
			.du_per_page = du_cnt - ZERO_BASED,
			.plane_num = pl - ZERO_BASED,
			.lun_num = lun - ZERO_BASED,
			.ce_num = ce - ZERO_BASED,
			.ch_num = ch - ZERO_BASED,
			.page_per_block = page - ZERO_BASED,
		},
	};
	l2p_writel(ctrl1.all, BTN_FTL_NCL_CTRL1);

	max_pda = du_cnt * pl * lun * ce * ch * page * blk;
	interleave = ce * ch * lun * pl;
	aligned_page = get_next_power_of_two(page);
	aligned_blk = get_next_power_of_two(blk);

	// PDA mode first, todo CPDA
	btn_pda_conf_reg0_t pda_conf = {
		.b = {
			.ftl_pda_du_offset = 0,
			.ftl_pda_intlv_offset = ctz(du_cnt),
			.ftl_pda_page_offset = ctz(interleave) + ctz(du_cnt),
			.ftl_pda_spb_offset = ctz(interleave) + ctz(du_cnt) + ctz(aligned_page),
			.ftl_pda_vld_msb = ctz(interleave) + ctz(du_cnt) + ctz(aligned_page) + ctz(aligned_blk),
			.ftl_pda_cpda_mode_sel = pda_mode ? L2P_PDA_MODE : L2P_CPDA_MODE
		},
	};
	l2p_writel(pda_conf.all, BTN_PDA_CONF_REG0);

#if defined(RDISK)
	u32 pda_mask = ~0;
#else
	u32 pda_mask = (1 << pda_conf.b.ftl_pda_vld_msb) - 1;
#endif
	l2p_writel(pda_mask, BTN_PDA_MASK);
	lxp_mgr_trace(LOG_ALW, 0x2853, "pda_mask %x max_pda %x", pda_mask, max_pda);

	l2p_writel(INV_PDA & pda_mask, BTN_UNMAPPED_PDA0);
	l2p_writel(UNMAP_PDA & pda_mask, BTN_UNMAPPED_PDA1);
	l2p_writel(ERR_PDA & pda_mask, BTN_UNMAPPED_PDA2);
	l2p_writel(ZERO_PDA & pda_mask, BTN_UNMAPPED_PDA3);
	lxp_mgr_trace(LOG_ALW, 0xdaaf, "PDA: INV %x UNM %x ERR %x ZERO %x",
			INV_PDA & pda_mask, UNMAP_PDA & pda_mask, ERR_PDA & pda_mask, ZERO_PDA & pda_mask);
	btn_ftl_ncl_ctrl2_t ctrl2 = {.all = l2p_readl(BTN_FTL_NCL_CTRL2)};
	ctrl2.b.ftl_unmapped_pda_vld = 0xF; // we have four
	l2p_writel(ctrl2.all, BTN_FTL_NCL_CTRL2);

	btn_pda_conf_reg1_t pda_conf1 = {
		.b = {
			.ftl_pda_intlv_pln_bw = ctz(pl),
			.ftl_pda_intlv_lun_bw = ctz(lun),
			.ftl_pda_intlv_ch_bw = ctz(ch),
			.ftl_pda_intlv_ce_bw = ctz(ce),
			.ftl_pda_intlv_type = PL_CH_CE_LUN
		},
	};

	l2p_writel(pda_conf1.all, BTN_PDA_CONF_REG1);

	btn_cpda_conf_reg0_t cpda_conf = {
		.b = {
			.ftl_cpda_du_offset = 0,
			.ftl_cpda_plane_offset = ctz(du_cnt),
			.ftl_cpda_lun_offset = ctz(pl) + ctz(du_cnt),
			.ftl_cpda_spcc_offset = ctz(lun) + ctz(pl) + ctz(du_cnt),
			.ftl_cpda_vld_msb = 31
		},
	};
	l2p_writel(cpda_conf.all, BTN_CPDA_CONF_REG0);

	btn_pda_rslt_grp_sel_conf_reg_t grp_sel = {
		.all = l2p_readl(BTN_PDA_RSLT_GRP_SEL_CONF_REG),
	};

	grp_sel.all = 0;
	if (ch != 1) {
		for (i = ch / 2; i < ch; i++) {
			grp_sel.all |= 1 << (i * PDA_CH1_GRP_SEL_SHIFT);
		}
	}
	l2p_writel(grp_sel.all, BTN_PDA_RSLT_GRP_SEL_CONF_REG);


	lxp_mgr_trace(LOG_ALW, 0x177c, "CH %d, CE %d, LUN %d, PL %d BLK %d",
			ch, ce, lun, pl, blk);
	lxp_mgr_trace(LOG_ALW, 0xa87a, "page %d du %d, MAX %x", page, du_cnt, max_pda);
	lxp_mgr_trace(LOG_ALW, 0x389e, "CH_GRP[0] 0 ~ %d, CH_GRP[1] %d - %d %x",
			(ch / 2) - 1, (ch / 2), ch, grp_sel.all);

	return max_pda;
}

init_code void l2p_mgr_die_grp(u32 ch, u32 ce, u32 grp_id)
{
	btn_spb_die_grp_conf_reg_t grp_conf = { .all = 0 };

	sys_assert(grp_id < 16);

	grp_conf.b.spb_die_grp_conf_ce_id = ce;
	grp_conf.b.spb_die_grp_conf_ch_id = ch;
	grp_conf.b.spb_die_grp_conf_direction = 1;
	grp_conf.b.spb_die_grp_id = grp_id;

	l2p_writel(grp_conf.all, BTN_SPB_DIE_GRP_CONF_REG);
}

init_code void l2p_mgr_dtag_init(u32 max_ddtag)
{
	btn_ftl_ncl_ctrl0_t ctrl0 = {.all = l2p_readl(BTN_FTL_NCL_CTRL0),};

	ctrl0.b.cmt_bypass_mode = 1;
	ctrl0.b.ftl_gtd_enable  = 0;
	ctrl0.b.l2p_srch_dtag_rslt_loc_sel = 0; // 1;
	l2p_writel(ctrl0.all, BTN_FTL_NCL_CTRL0);

	// clear interrupts
	l2p_writel(0xffffffff, BTN_FTL_NCL_INT_GRP0_SRC);
	l2p_writel(0xffffffff, BTN_FTL_NCL_INT_GRP1_SRC);

	l2p_writel(max_ddtag - 1, BTN_RSVD_PDA_RANGE);
	max_dtag = max_ddtag;
}

xinit_code void l2p_mgr_buf_init(u64 l2p_base, u64 l2p_size, u64 vbmp_base, u64 vbmp_size,
		u64 vcnt_base, u64 vcnt_size, l2p_cnt_loc_t vcnt_loc)
{
	// probably move to l2p_addr_init
	l2p_writel(0, BTN_L2P_TABLE_START_LDA);
	l2p_writel((u32)(l2p_size >> 2) - 1, BTN_L2P_TABLE_ENTRY_NUM);
	l2p_writel((u32)(l2p_base >> 4), BTN_FTL_REGION0_DATA_TP_BASE_ADDR);
	l2p_writel((u32)(vbmp_base >> 4), BTN_FTL_REGION0_DATA_DVBM_BASE_ADDR);

	btn_ftl_status_cnt_table_config_reg2_t cfg2 = { .all = btn_readl(BTN_FTL_STATUS_CNT_TABLE_CONFIG_REG2) };
	l2p_writel(0, BTN_FTL_STATUS_CNT_TABLE_CONFIG_REG3);

	if (vcnt_loc == CNT_IN_INTB) {
		//sys_assert(vcnt_base == 0);
		// use internal buffer
		cfg2.b.ftl_status_cnt_loc_sel = CNT_IN_INTB;
		sys_assert(L2P_INT_BUF_SZ >= vcnt_size);
	} else if (vcnt_loc == CNT_IN_EXTEND || vcnt_loc == CNT_IN_SRAM) {
		btn_ftl_status_cnt_table_config_reg0_t cfg0;

		cfg2.b.ftl_status_cnt_loc_sel = CNT_IN_SRAM;
		cfg0.b.ftl_status_cnt_base_addr = vcnt_base;
		cfg0.b.ftl_status_cnt_base_addr /= 16; // 16bytes address
		l2p_writel(cfg0.all, BTN_FTL_STATUS_CNT_TABLE_CONFIG_REG0);
		// clear sram
	} else {
		panic("not support");
	}

	btn_ftl_ncl_ctrl0_t ctrl0 = { .all = l2p_readl(BTN_FTL_NCL_CTRL0) };

	ctrl0.b.vldc_loc_mem_disable = (vcnt_loc == CNT_IN_SRAM) ? true : false;
	l2p_writel(ctrl0.all, BTN_FTL_NCL_CTRL0);

	cfg2.b.ftl_status_cnt_entry_size = CNT_ENTRY_4B;
	cfg2.b.ftl_status_cnt_table_max_entry_per_grp = ((u32)vcnt_size) / sizeof(u32);

	l2p_writel(cfg2.all, BTN_FTL_STATUS_CNT_TABLE_CONFIG_REG2);
	l2p_writel(0, BTN_FTL_STATUS_CNT_NOTIF_THRESHOLD);

#ifdef RAWDISK
	bm_scrub_ddr(l2p_base, l2p_size, 0xffffffff);
#endif
	bm_scrub_ddr(vbmp_base, vbmp_size, 0x0);
}

ddr_code bool l2p_mgr_vcnt_rebuild(void)
{
	volatile btn_ftl_ncl_ctrl0_t ctrl0 = { .all = l2p_readl(BTN_FTL_NCL_CTRL0) };
	btn_ftl_ncl_int_grp0_src_t int_grp = { .all = l2p_readl(BTN_FTL_NCL_INT_GRP0_SRC) };

	if (int_grp.b.ftl_hw_accelr_ovfl_err) {
		lxp_mgr_trace(LOG_ERR, 0x450f, "overflow before rebuild");
		l2p_writel(FTL_HW_ACCELR_OVFL_ERR_MASK, BTN_FTL_NCL_INT_GRP0_SRC);
	}

	lxp_mgr_trace(LOG_INFO, 0x8f5d, "building vcnt tbl.....");

	// todo: may initialize valid count if it was not internal
	if (ctrl0.b.vldc_loc_mem_disable == false) {
		ctrl0.b.ftl_internal_sram_init = 1;
		l2p_writel(ctrl0.all, BTN_FTL_NCL_CTRL0);

		do {
			ctrl0.all = l2p_readl(BTN_FTL_NCL_CTRL0);
		} while (ctrl0.b.ftl_internal_sram_init);
	}

	ctrl0.b.du_vld_info_rebuild_start = 1;
	l2p_writel(ctrl0.all, BTN_FTL_NCL_CTRL0);

	do {
		ctrl0.all = l2p_readl(BTN_FTL_NCL_CTRL0);
	} while (ctrl0.b.du_vld_info_rebuild_start);

	lxp_mgr_trace(LOG_INFO, 0x4238, "building vcnt tbl done");

	int_grp.all = l2p_readl(BTN_FTL_NCL_INT_GRP0_SRC);
	if (int_grp.b.ftl_hw_accelr_ovfl_err) {
		l2p_writel(FTL_HW_ACCELR_OVFL_ERR_MASK, BTN_FTL_NCL_INT_GRP0_SRC);
		return false;
	}

	return true;
}
fast_code void l2p_mgr_vcnt_move(bool dir, void *buffer, u32 size)
{
	u32 addr = sram_to_dma(buffer) ;
	spin_lock_take(SPIN_LOCK_KEY_VCNT,0,true);
	l2p_writel(addr, BTN_VLDC_BACKUP_RESTORE_LOC_ADDR);
	l2p_writel(size - 1, BTN_VLDC_BACKUP_RESTORE_SIZE);

	volatile btn_ftl_ncl_ctrl0_t ctrl0 = { .all = l2p_readl(BTN_FTL_NCL_CTRL0) };

	sys_assert(ctrl0.b.vldc_backup_restore_start == 0);
	ctrl0.b.vldc_backup_restore_dir = dir;
	ctrl0.b.vldc_backup_restore_start = 1;
	l2p_writel(ctrl0.all, BTN_FTL_NCL_CTRL0);

	do {
		ctrl0.all = l2p_readl(BTN_FTL_NCL_CTRL0);
	} while (ctrl0.b.vldc_backup_restore_start);
    spin_lock_release(SPIN_LOCK_KEY_VCNT);
}

xinit_code void l2p_mgr_ctrl(bool vbmp_used, u32 max_lda)
{
	btn_ftl_ncl_ctrl0_t ctrl0 = {.all = l2p_readl(BTN_FTL_NCL_CTRL0),};

	ctrl0.b.p2l_dvbm_conf_sel = vbmp_used;

	l2p_writel(ctrl0.all, BTN_FTL_NCL_CTRL0);
#if defined(DEBUG_LRANGE)
	shr_max_lda = max_lda;
#endif
	l2p_writel(max_lda, BTN_LDA_MAX_VALUE);
	l2p_writel(INV_LDA, BTN_RSVD_LDA_VALUE);
}

fast_code void l2p_mgr_trim_cmpl_filter(bool en, u32 qid)
{
	btn_ftl_ncl_ctrl2_t ctrl2 = {.all = l2p_readl(BTN_FTL_NCL_CTRL2)};

	if (qid == 0)
		ctrl2.b.l2p_updt_compl_que0_config = en;
	else if (qid == 1)
		ctrl2.b.l2p_updt_compl_que1_config = en;
	else if (qid == 2)
		ctrl2.b.l2p_updt_compl_que2_config = en;
	else if (qid == 3)
		ctrl2.b.l2p_updt_compl_que3_config = en;
	else
		return;

	l2p_writel(ctrl2.all, BTN_FTL_NCL_CTRL2);
}

fast_code void l2p_mgr_dtag_recycle_que_filter(bool en)
{
	btn_ftl_ncl_ctrl2_t ctrl2 = {.all = l2p_readl(BTN_FTL_NCL_CTRL2)};

	ctrl2.b.l2p_updt_recycle_que_config = en;

	l2p_writel(ctrl2.all, BTN_FTL_NCL_CTRL2);
}

fast_code void l2p_mgr_range_chk_ctrl(bool en, bool updt)
{
	btn_ftl_ncl_ctrl2_t ctrl2 = { .all = l2p_readl(BTN_FTL_NCL_CTRL2) };

	if (updt)
		ctrl2.b.l2p_updt_range_chk_en = en;
	else
		ctrl2.b.l2p_srch_range_chk_en = en;

	l2p_writel(ctrl2.all, BTN_FTL_NCL_CTRL2);
}

fast_code void l2p_mgr_switch_que(l2p_que_t *que, bool en)
{
	if (en) {
		writel(que->reg.entry_dbase.all, (void *) que->reg.mmio);
	} else {
		u32 t = que->reg.entry_dbase.b.max_sz;

		que->reg.entry_dbase.b.max_sz = 0;
		writel(que->reg.entry_dbase.all, (void *) que->reg.mmio);
		que->reg.entry_dbase.b.max_sz = t;
		que->reg.entry_pnter.all = 0;
		que->ptr = 0;
		writel(que->reg.entry_pnter.all, (void *) (que->reg.mmio + 4));
	}
}

#if RECYCLE_QUE_CPU_ID == CPU_ID
fast_code void l2p_mgr_switch_recycle_que(bool en)
{
	btn_ftl_ncl_int_grp0_src_t sts;

	do {
		sts.all = l2p_readl(L2P_GRP0_INT_STS);
		recycle_poll(&_recycle_que, evt_l2p_recycle_que);
	} while (sts.b.l2p_updt_recycle_que_updt);

	l2p_mgr_switch_que(&_recycle_que, en);
}
#endif

fast_code void l2p_mgr_switch_rsp_que(bool en, u32 qid, bool updt)
{
	l2p_que_t *que = NULL;

	if (updt) {
		switch (qid) {
#if UPDT_CMPL_QUE0_CPU_ID == CPU_ID
		case 0:
			que = &_updt_cpl_que0;
			break;
#endif
#if UPDT_CMPL_QUE1_CPU_ID == CPU_ID
		case 1:
			que = &_updt_cpl_que1;
			break;
#endif
#if UPDT_CMPL_QUE2_CPU_ID == CPU_ID
		case 2:
			que = &_updt_cpl_que2;
			break;
#endif
#if UPDT_CMPL_QUE3_CPU_ID == CPU_ID
		case 3:
			que = &_updt_cpl_que3;
			break;
#endif
		default:
			panic("stop");
			break;
		}
	} else {
		switch (qid) {
#if CMMT_CMPL_QUE0_CPU_ID == CPU_ID
		case 0:
			que = &_cmmt_cpl_que0;
			break;
#endif
#if CMMT_CMPL_QUE1_CPU_ID == CPU_ID
		case 1:
			que = &_cmmt_cpl_que1;
			break;
#endif
#if CMMT_CMPL_QUE2_CPU_ID == CPU_ID
		case 2:
			que = &_cmmt_cpl_que2;
			break;
#endif
#if CMMT_CMPL_QUE3_CPU_ID == CPU_ID
		case 3:
			que = &_cmmt_cpl_que3;
			break;
#endif
		default:
			panic("stop");
			break;
		}
	}

	lxp_mgr_trace(LOG_ALW, 0xe3e9, "qid %d-%d -> %d", qid, updt, en);
	l2p_mgr_switch_que(que, en);
}

/*!
 * @brief l2p mgr suspend handling
 *
 * @param[in]	mode	sleep mode
 *
 * @return fail or good
 */
norm_ps_code bool l2p_mgr_suspend(enum sleep_mode_t mode)
{
#if CPU_ID == 1
	u32 i = 0;

	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG0);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG1);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG2);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG3);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG4);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG5);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG6);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG7);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG8);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG9);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG10);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG11);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG12);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG13);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG14);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG15);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG16);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG17);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG18);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG19);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG20);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG21);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG22);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG23);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG24);
	backup_l2p_regs[i++] = l2p_readl(BACKUP_L2P_REG25);
#endif
	return true;
}

/*!
 * @brief l2p mgr resume handling
 *
 * @param[in]	mode	sleep mode
 *
 * @return N/A
 */
ps_code void l2p_mgr_resume(enum sleep_mode_t mode)
{
	UNUSED u32 i = 0;
	u32 mask0;
	u32 mask1;

	if (mode == SUSPEND_ABORT)
		return;

#if CPU_ID == 1
	if (mode != SUSPEND_INIT) {
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG0);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG1);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG2);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG3);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG4);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG5);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG6);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG7);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG8);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG9);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG10);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG11);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG12);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG13);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG14);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG15);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG16);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG17);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG18);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG19);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG20);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG21);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG22);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG23);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG24);
		l2p_writel(backup_l2p_regs[i++], BACKUP_L2P_REG25);
	}
	volatile btn_ftl_ncl_ctrl0_t ctrl0 = { .all = l2p_readl(BTN_FTL_NCL_CTRL0) };
	ctrl0.b.ftl_internal_sram_init = 1;
	l2p_writel(ctrl0.all, BTN_FTL_NCL_CTRL0);
	do {
		ctrl0.all = l2p_readl(BTN_FTL_NCL_CTRL0);
	} while (ctrl0.b.ftl_internal_sram_init);
#endif

	mask0 = l2p_readl(L2P_GRP0_INT_MASK);
	mask1 = l2p_readl(L2P_GRP1_INT_MASK);

	// Search Queue 0
#if SRCH_QUE0_NRM_CPU_ID == CPU_ID
	l2p_srch_req_q_setup(SRCH_NRM, SRCH_REQ_ENTRY_CNT, &_srch_req0_nrm, _srch_nrm_req0_entry, 0);
#endif
#if SRCH_QUE0_HIGH_CPU_ID == CPU_ID
	l2p_srch_req_q_setup(SRCH_HIG, SRCH_REQ_ENTRY_CNT, &_srch_req0_high, _srch_hig_req0_entry, 0);
#endif
#if SRCH_QUE0_URG_CPU_ID == CPU_ID
	l2p_srch_req_q_setup(SRCH_URG, SRCH_REQ_ENTRY_CNT, &_srch_req0_urg, _srch_urg_req0_entry, 0);
#endif
#if SRCH_QUE1_NRM_CPU_ID == CPU_ID
	l2p_srch_req_q_setup(SRCH_NRM, SRCH_REQ_ENTRY_CNT, &_srch_req1_nrm, _srch_nrm_req1_entry, 1);
#endif
#if SRCH_QUE1_HIGH_CPU_ID == CPU_ID
	l2p_srch_req_q_setup(SRCH_HIG, SRCH_REQ_ENTRY_CNT, &_srch_req1_high, _srch_hig_req1_entry, 1);
#endif
#if SRCH_QUE1_URG_CPU_ID == CPU_ID
	l2p_srch_req_q_setup(SRCH_URG, SRCH_REQ_ENTRY_CNT, &_srch_req1_urg, _srch_urg_req1_entry, 1);
#endif
#if UNMAP_QUE_CPU_ID == CPU_ID
	l2p_srch_unmap_q_setup(SRCH_RSP_UNMAP_CNT, &_srch_rsp_unmap, _srch_rsp_unmap_entry);

	mask0 &= ~(L2P_UNMAP_LDA_QUE_UPDT_MASK);
#endif

#if SRCH_PDA_GRP0_CPU_ID == CPU_ID
	l2p_srch_loc_q_setup(SRCH_RSP_CNT, &_srch_rsp0_surc, _srch_rsp0_surc_entry, 0);
	l2p_srch_cmpl_q_setup(SRCH_NRM, SRCH_RSP_CNT, &_srch_rsp0[SRCH_NRM], _srch_nrm_rsp0_dst_entry, 0);
	l2p_srch_cmpl_q_setup(SRCH_HIG, SRCH_RSP_CNT, &_srch_rsp0[SRCH_HIG], _srch_hig_rsp0_dst_entry, 0);
	l2p_srch_cmpl_q_setup(SRCH_URG, SRCH_RSP_CNT, &_srch_rsp0[SRCH_URG], _srch_urg_rsp0_dst_entry, 0);
	mask0 &= ~(L2P_PDA_RSLT_NORM_QUE0_UPDT_MASK | L2P_PDA_RSLT_HIGH_QUE0_UPDT_MASK | L2P_PDA_RSLT_URGENT_QUE0_UPDT_MASK);

	sys_assert(_srch_rsp0_buckets);
	bucket_resume(_srch_rsp0_buckets, sizeof(l2p_srch_rsp_t), ARRAY_SIZE(_srch_rsp0_entry),
				btcm_to_dma(&(_srch_rsp0_entry[0])), 16);
	for (i = 0; i < SRCH_RSP_CNT / 16; i ++) {
		bucket_t bucket;
		bool ret;

		ret = bucket_get_item(_srch_rsp0_buckets, &bucket);
		sys_assert(ret == true);
		if (hdr_surc_push(&_srch_rsp0_surc.reg, _srch_rsp0_surc.ptr, bucket.all))
			__asm("BKPT");
	}
#endif

#if SRCH_PDA_GRP1_CPU_ID == CPU_ID
	l2p_srch_loc_q_setup(SRCH_RSP_CNT, &_srch_rsp1_surc, _srch_rsp1_surc_entry, 1);
	l2p_srch_cmpl_q_setup(SRCH_NRM, SRCH_RSP_CNT, &_srch_rsp1[SRCH_NRM], _srch_nrm_rsp1_dst_entry, 1);
	l2p_srch_cmpl_q_setup(SRCH_HIG, SRCH_RSP_CNT, &_srch_rsp1[SRCH_HIG], _srch_hig_rsp1_dst_entry, 1);
	l2p_srch_cmpl_q_setup(SRCH_URG, SRCH_RSP_CNT, &_srch_rsp1[SRCH_URG], _srch_urg_rsp1_dst_entry, 1);

	mask1 &= ~(L2P_PDA_RSLT_NORM_QUE1_UPDT_MASK | L2P_PDA_RSLT_HIGH_QUE1_UPDT_MASK | L2P_PDA_RSLT_URGENT_QUE1_UPDT_MASK);

	sys_assert(_srch_rsp1_buckets);
	bucket_resume(_srch_rsp1_buckets, sizeof(l2p_srch_rsp_t), ARRAY_SIZE(_srch_rsp1_entry),
			btcm_to_dma(&(_srch_rsp1_entry[0])), 16);
	for (i = 0; i < SRCH_RSP_CNT / 16; i ++) {
		bucket_t bucket;
		bool ret;

		ret = bucket_get_item(_srch_rsp1_buckets, &bucket);
		sys_assert(ret == true);
		if (hdr_surc_push(&_srch_rsp1_surc.reg, _srch_rsp1_surc.ptr, bucket.all))
			__asm("BKPT");
	}
#endif

#if UPDT_REQ_QUE0_CPU_ID == CPU_ID
	l2p_updt_req_q_setup(UPDT_REQ_ENTRY_CNT, &_updt_req_que0, _updt_req_que0_entry, 0);
#endif

#if UPDT_CMPL_QUE0_CPU_ID == CPU_ID
	l2p_updt_cmpl_q_setup(UPDT_CPL_ENTRY_CNT, &_updt_cpl_que0, _updt_cpl_que0_entry, 0);
	mask0 &= ~(L2P_UPDT_COMPL_QUE0_UPDT_MASK);
#endif
#if CMMT_CMPL_QUE0_CPU_ID == CPU_ID
	l2p_cmmt_cmpl_q_setup(CMMT_CPL_ENTRY_CNT, &_cmmt_cpl_que0, _cmmt_cpl_que0_entry, 0);
	mask0 &= ~(L2P_COMMIT_COMPL_QUE0_UPDT_MASK);
#endif

#if UPDT_REQ_QUE1_CPU_ID == CPU_ID
	l2p_updt_req_q_setup(UPDT_REQ_ENTRY_CNT, &_updt_req_que1, _updt_req_que1_entry, 1);
#endif

#if UPDT_CMPL_QUE1_CPU_ID == CPU_ID
	l2p_updt_cmpl_q_setup(UPDT_CPL_ENTRY_CNT, &_updt_cpl_que1, _updt_cpl_que1_entry, 1);
	mask0 &= ~(L2P_UPDT_COMPL_QUE1_UPDT_MASK);
#endif
#if CMMT_CMPL_QUE1_CPU_ID == CPU_ID
	l2p_cmmt_cmpl_q_setup(CMMT_CPL_ENTRY_CNT, &_cmmt_cpl_que1, _cmmt_cpl_que1_entry, 1);
	mask0 &= ~(L2P_COMMIT_COMPL_QUE1_UPDT_MASK);
#endif

#if UPDT_REQ_QUE2_CPU_ID == CPU_ID
	l2p_updt_req_q_setup(UPDT_REQ_ENTRY_CNT, &_updt_req_que2, _updt_req_que2_entry, 2);
#endif

#if UPDT_CMPL_QUE2_CPU_ID == CPU_ID
	l2p_updt_cmpl_q_setup(UPDT_CPL_ENTRY_CNT, &_updt_cpl_que2, _updt_cpl_que2_entry, 2);
	mask1 &= ~(L2P_UPDT_COMPL_QUE2_UPDT_MASK);
#endif
#if CMMT_CMPL_QUE2_CPU_ID == CPU_ID
	l2p_cmmt_cmpl_q_setup(CMMT_CPL_ENTRY_CNT, &_cmmt_cpl_que2, _cmmt_cpl_que2_entry, 2);
	mask1 &= ~(L2P_COMMIT_COMPL_QUE2_UPDT_MASK);
#endif

#if UPDT_REQ_QUE3_CPU_ID == CPU_ID
	l2p_updt_req_q_setup(UPDT_REQ_ENTRY_CNT, &_updt_req_que3, _updt_req_que3_entry, 3);
#endif

#if UPDT_CMPL_QUE3_CPU_ID == CPU_ID
	l2p_updt_cmpl_q_setup(UPDT_CPL_ENTRY_CNT, &_updt_cpl_que3, _updt_cpl_que3_entry, 3);
	mask1 &= ~(L2P_UPDT_COMPL_QUE3_UPDT_MASK);
#endif
#if CMMT_CMPL_QUE3_CPU_ID == CPU_ID
	l2p_cmmt_cmpl_q_setup(CMMT_CPL_ENTRY_CNT, &_cmmt_cpl_que3, _cmmt_cpl_que3_entry, 3);
	mask1 &= ~(L2P_COMMIT_COMPL_QUE3_UPDT_MASK);
#endif

#if RECYCLE_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_recycle_que), RECYCLE_ENTRY_CNT, &_recycle_que_entry[0], BTN_L2P_UPDATE_RECYCLE_QUE_BASE_ADDR);
	mask0 &= ~(L2P_UPDT_RECYCLE_QUE_UPDT_MASK);
#endif

#if DVL_INFO_UPDT_REQ_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_dvl_info_updt_req_que), DVL_INFO_UPDT_REQ_ENTRY_CNT, _dvl_info_updt_req_que_entry,
			BTN_DVL_INFO_FW_UPDT_REQ_QUE_BASE_ADDR);
#endif

#if DVL_INFO_UPDT_CPL_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_dvl_info_updt_cpl_que), DVL_INFO_UPDT_CPL_ENTRY_CNT, _dvl_info_updt_cpl_que_entry,
			BTN_DVL_INFO_FW_UPDT_RSLT_QUE_BASE_ADDR);

	mask0 &= ~(DVL_INFO_FW_UPDT_RSLT_QUE_UPDT_MASK);
#endif

#if PDA_GEN_QUE0_CPU_ID == CPU_ID
	l2p_pda_gen_req_q_setup(PDA_GEN_REQ_ENTRY_CNT, &_pda_gen_req_que0, (void *)&_pda_gen_req_que0_entry, 0);
#endif

#if PDA_GEN_QUE1_CPU_ID == CPU_ID
	l2p_pda_gen_req_q_setup(PDA_GEN_REQ_ENTRY_CNT, &_pda_gen_req_que1, (void *)&_pda_gen_req_que1_entry, 1);
#endif

#if PDA_GEN_LOC0_CPU_ID == CPU_ID
	l2p_pda_gen_loc_q_setup(PDA_GEN_LOC_ENTRY_CNT, &_pda_gen_loc_que0, _pda_gen_loc_que0_entry, 0);
#endif

#if PDA_GEN_LOC1_CPU_ID == CPU_ID
	l2p_pda_gen_loc_q_setup(PDA_GEN_LOC_ENTRY_CNT, &_pda_gen_loc_que1, _pda_gen_loc_que1_entry, 1);
#endif

#if PDA_GEN_RSP0_QUE0_CPU_ID == CPU_ID
	l2p_pda_gen_rsp0_q_setup(PDA_GEN_RSP_ENTRY_CNT, &_pda_gen_rsp0_que0, _pda_gen_rsp0_que0_entry, 0);
	mask0 &= ~(PDA_GEN_RSLT0_QUE0_UPDT_MASK);
#endif
#if PDA_GEN_RSP1_QUE0_CPU_ID == CPU_ID
	l2p_pda_gen_rsp1_q_setup(PDA_GEN_RSP_ENTRY_CNT, &_pda_gen_rsp1_que0, _pda_gen_rsp1_que0_entry, 0);
	mask1 &= ~(PDA_GEN_RSLT1_QUE0_UPDT_MASK);
#endif

#if PDA_GEN_RSP0_QUE1_CPU_ID == CPU_ID
	l2p_pda_gen_rsp0_q_setup(PDA_GEN_RSP_ENTRY_CNT, &_pda_gen_rsp0_que1, _pda_gen_rsp0_que1_entry, 1);
	mask0 &= ~(PDA_GEN_RSLT0_QUE1_UPDT_MASK);
#endif
#if PDA_GEN_RSP1_QUE1_CPU_ID == CPU_ID
	l2p_pda_gen_rsp1_q_setup(PDA_GEN_RSP_ENTRY_CNT, &_pda_gen_rsp1_que1, _pda_gen_rsp1_que1_entry, 1);
	mask1 &= ~(PDA_GEN_RSLT1_QUE1_UPDT_MASK);
#endif

#if STATUS_FET_REQ_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_status_fet_req_que), STATUS_FET_REQ_ENTRY_CNT, _status_fet_req_que_entry, BTN_FTL_STATUS_CNT_FET_REQ_QUE_BASE_ADDR);
#endif

#if STATUS_FET_RSP_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_status_fet_rsp_que), STATUS_FET_RSP_ENTRY_CNT, _status_fet_rsp_que_entry, BTN_FTL_STATUS_CNT_FET_RSLT_QUE_BASE_ADDR);
	mask0 &= ~(FTL_STAT_CNT_FET_RSLT_QUE_UPDT_MASK);
#endif

#if STATUS_SRCH_REQ_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_status_srch_req_que), STATUS_SRCH_REQ_ENTRY_CNT, _status_srch_req_que_entry, BTN_FTL_STATUS_CNT_SRCH_REQ_QUE_BASE_ADDR);
#endif

#if STATUS_SRCH_RSP_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_status_srch_rsp_que), STATUS_SRCH_RSP_ENTRY_CNT, _status_srch_rsp_que_entry, BTN_FTL_STATUS_CNT_SRCH_RSLT_QUE_BASE_ADDR);
	mask0 &= ~(FTL_STAT_CNT_SRCH_RSLT_QUE_UPDT_MASK);
#endif

#if STATUS_CNT_NOTIFY_QUE_CPU_ID == CPU_ID
	HDR_REG_INIT((&_status_cnt_notify_que), STATUS_CNT_NOTIFY_ENTRY_CNT, _status_cnt_notify_que_entry,
			BTN_FTL_STATUS_CNT_NOTIF_QUE_BASE_ADDR);
	mask0 &= ~(FTL_STAT_CNT_NOTIF_QUE_UPDT_MASK);
#endif

	l2p_writel(mask0, L2P_GRP0_INT_MASK); // unmask INT
	l2p_writel(mask1, L2P_GRP1_INT_MASK);
	l2p_writel(0, L2P_GRP0_INT_MASK); // unmask INT
	l2p_writel(0, L2P_GRP1_INT_MASK);

#ifdef L2P_G0_SRCH_CPU
	poll_mask(VID_L2P_Q0_SRCH_RSLT);
#endif
#ifdef L2P_G1_SRCH_CPU
	poll_mask(VID_L2P_Q1_SRCH_RSLT);
#endif
#ifdef L2P_UPDT_CMMT_Q01_CPU
	poll_mask(VID_L2P_Q01_UPDT_RSLT);
#endif

#ifdef L2P_UPDT_CMMT_Q23_CPU
	poll_mask(VID_L2P_Q23_UPDT_RSLT);
#endif

#ifdef L2P_FTL_OTHER_CPU
	poll_mask(VID_NCL_FTL_OTHERS);
#endif

#if (CPU_FE == CPU_ID)
#ifdef ATOMIC
	l2p_mgr_range_chk_ctrl(true, false);
	l2p_mgr_range_chk_ctrl(true, true);
#endif
#endif
}

fast_code void l2p_mgr_reset_resume(void)
{
	l2p_mgr_resume(SLEEP_MODE_PS4);
}

fast_code void l2p_mgr_reset(void)
{
	l2p_mgr_suspend(SLEEP_MODE_PS4);
}

static inline int l2p_chk_que_idle(l2p_que_t *que)
{
	que->reg.entry_pnter.all = readl((void *) (que->reg.mmio + 4));

	if (que->reg.entry_pnter.b.wptr == que->reg.entry_pnter.b.rptr)
		return 0;
	else
		return 1;
}

fast_code int wait_l2p_idle(l2p_idle_ctrl_t ctrl)
{
	int busy_que;
	UNUSED u32 i;
	u64 start = get_tsc_64();

again:
	busy_que = 0;
	if (ctrl.b.srch || ctrl.b.all) {
#if SRCH_QUE0_NRM_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_srch_req0_nrm);
#endif
#if SRCH_QUE0_HIGH_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_srch_req0_high);
#endif
#if SRCH_QUE0_URG_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_srch_req0_urg);
#endif
#if UNMAP_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_srch_rsp_unmap);
#endif
#if SRCH_PDA_GRP0_CPU_ID == CPU_ID
		for (i = 0; i < SRCH_MAX; i++)
			busy_que += l2p_chk_que_idle(&_srch_rsp0[i]);
#endif
#if SRCH_PDA_GRP1_CPU_ID == CPU_ID
		for (i = 0; i < SRCH_MAX; i++)
			busy_que += l2p_chk_que_idle(&_srch_rsp1[i]);
#endif
	}

	if (ctrl.b.updt || ctrl.b.all) {
#if UPDT_REQ_QUE0_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_req_que0);
#endif
#if UPDT_REQ_QUE1_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_req_que1);
#endif
#if UPDT_REQ_QUE2_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_req_que2);
#endif
#if UPDT_REQ_QUE3_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_req_que3);
#endif
	}

	if (ctrl.b.updt_cmpl || ctrl.b.all) {
#if UPDT_CMPL_QUE0_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_cpl_que0);
#endif
#if UPDT_CMPL_QUE1_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_cpl_que1);
#endif
#if UPDT_CMPL_QUE2_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_cpl_que2);
#endif
#if UPDT_CMPL_QUE3_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_updt_cpl_que3);
#endif
#if DVL_INFO_UPDT_REQ_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_dvl_info_updt_req_que);
#endif
#if DVL_INFO_UPDT_CPL_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_dvl_info_updt_cpl_que);
#endif
	}

	if (ctrl.b.gc || ctrl.b.all) {
#if PDA_GEN_QUE0_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_pda_gen_req_que0);
#endif
#if PDA_GEN_RSP0_QUE0_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_pda_gen_rsp0_que0);
#endif
#if PDA_GEN_RSP0_QUE1_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_pda_gen_rsp1_que0);
#endif
	}

	if (ctrl.b.misc || ctrl.b.all) {
#if STATUS_SRCH_REQ_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_status_srch_req_que);
#endif
#if STATUS_SRCH_RSP_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_status_srch_rsp_que);
#endif
#if STATUS_FET_REQ_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_status_fet_req_que);
#endif
#if STATUS_FET_RSP_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_status_fet_rsp_que);
#endif
#if STATUS_CNT_NOTIFY_QUE_CPU_ID == CPU_ID
		busy_que += l2p_chk_que_idle(&_status_cnt_notify_que);
#endif
	}

	if (busy_que && ctrl.b.wait) {
		u32 ms = time_elapsed_in_ms(start);

		if (ctrl.b.srch || ctrl.b.all) {
#ifdef L2P_G0_SRCH_CPU
			l2p_isr_q0_srch();
#endif
#ifdef L2P_G1_SRCH_CPU
			l2p_isr_q1_srch();
#endif
		}
		if (ctrl.b.updt_cmpl || ctrl.b.all) {
#ifdef L2P_UPDT_CMMT_Q01_CPU
			l2p_isr_q01_updt();
#endif
#ifdef L2P_UPDT_CMMT_Q23_CPU
			l2p_isr_q23_updt();
#endif
		}
		if (ctrl.b.misc || ctrl.b.all) {
#ifdef L2P_FTL_OTHER_CPU
			l2p_isr_ftl_others();
#endif
		}
		sys_assert(ms < 2000);
		goto again;
	}

	return busy_que;
}

init_code void l2p_mgr_init(void)
{
	pmu_register_handler(SUSPEND_COOKIE_L2P, l2p_mgr_suspend,
			RESUME_COOKIE_L2P, l2p_mgr_resume);

#ifdef L2P_G0_SRCH_CPU
	poll_irq_register(VID_L2P_Q0_SRCH_RSLT, l2p_isr_q0_srch);
#endif

#ifdef L2P_G1_SRCH_CPU
	poll_irq_register(VID_L2P_Q1_SRCH_RSLT, l2p_isr_q1_srch);
#endif

#ifdef L2P_UPDT_CMMT_Q01_CPU
	poll_irq_register(VID_L2P_Q01_UPDT_RSLT, l2p_isr_q01_updt);
#endif

#ifdef L2P_UPDT_CMMT_Q23_CPU
	poll_irq_register(VID_L2P_Q23_UPDT_RSLT, l2p_isr_q23_updt);
#endif

#if SRCH_PDA_GRP0_CPU_ID == CPU_ID
	_srch_rsp0_buckets = bucket_init(sizeof(l2p_srch_rsp_t), ARRAY_SIZE(_srch_rsp0_entry),
				btcm_to_dma(&(_srch_rsp0_entry[0])), 16);
	sys_assert(_srch_rsp0_buckets);
#endif

#if SRCH_PDA_GRP1_CPU_ID == CPU_ID
	_srch_rsp1_buckets = bucket_init(sizeof(l2p_srch_rsp_t), ARRAY_SIZE(_srch_rsp1_entry),
			btcm_to_dma(&(_srch_rsp1_entry[0])), 16);
	sys_assert(_srch_rsp1_buckets);
#endif

#ifdef L2P_FTL_OTHER_CPU
	poll_irq_register(VID_NCL_FTL_OTHERS, l2p_isr_ftl_others);
#endif
	l2p_mgr_resume(SUSPEND_INIT);
}

/*! @} */

