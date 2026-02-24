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
 * \file l2p_mgr.h
 * @brief L2P manager export api
 * \addtogroup export
 * \defgroup l2pe
 * \ingroup export
 * @{
 */
#include "btn_export.h"
#include "bitops.h"
#pragma once

#define MAX_L2P_TRIM_NUM	65536		///< max trim number per request
#define L2P_SRCHQ_AVAIL_CNT_MIN 16
extern u16 srhq_avail_cnt;

extern volatile u32 shr_rd_cnt_tbl_addr;
extern volatile u32 shr_rd_cnt_counter_addr;
extern volatile u32 shr_ec_tbl_addr;
///< LSB to MSB
typedef enum {
	PL_CH_CE_LUN = 2,
	CE_CH_LUN_PL = 15,
} l2p_intlv_type_t;

typedef enum {
	TRIM_PAT_0 = 0,
	TRIM_PAT_1,
	TRIM_PAT_2,
	TRIM_PAT_3,
	TRIM_PAT_MAX,
} l2p_trim_pat_t;

/*!
 * @brief L2P queue definition
 */
typedef struct {
	hdr_reg_t reg;				///< common queue header register
	u32 ptr __attribute__ ((aligned(8)));	///< asic update pointer
	u32 swq_id;				///< software queue id
} l2p_que_t;

/*!
 * @brief definition of l2p search request
 */
typedef struct {
	u32 lda;		///< lda to be search
	u32 ofst:12;		///< lda offset in origin command
	u32 btag:12;		///< command tag
	u32 region:1;		///< lda region, useless now, default is 0
	u32 num:5;		///< search count, max is 32, 0 base value
	u32 rsvd:2;
} srch_req_t;
BUILD_BUG_ON(sizeof(srch_req_t) != 8);

/*! @brief search priority definition */
typedef enum {
	SRCH_NRM = 0,
	SRCH_HIG,
	SRCH_URG,
	SRCH_MAX
} l2p_srch_pri_t;

/*!
 * @brief definition of l2p search pda result
 */
typedef struct _l2p_srch_rsp_t {
	u32 pda;			///< compact result PDA, CPDA

	u32 ofst:12;			///< du offset of origin command
	u32 btag:12;			///< command tag
	u32 rsvd56:8;

	u64 ce:4;			///< ce number in channel
	u64 ch:4;			///< channel

	u64 pda_pg:13;			///< page number
	u64 pda_ln:4;			///< lun number
	u64 pda_pl:4;			///< plane number
	u64 pda_du:3;			///< du number
	u64 rsvd96:31;
	u64 region:1;
} l2p_srch_rsp_t;
BUILD_BUG_ON(sizeof(l2p_srch_rsp_t) != 16);

/*!
 * @brief definition of l2p search unmap/dtag result
 */
typedef struct _l2p_srch_unmap_t{
	u32 lda;			///< if dtag = 1, this field is dtag in l2p
	u32 ofst:12;			///< du offset of origin command
	u32 btag:12;			///< command tag
	u32 region:1;
	u32 dtag:1;			///< if dtag in l2p
	u32 unmapped_type:2;		///< unmapped pda0/1/2/3
	u32 rsvd:4;
} l2p_srch_unmap_t;
BUILD_BUG_ON(sizeof(l2p_srch_unmap_t) != 8);

/*!
 * @brief definition of fda format which was used in l2p update
 */
typedef struct _l2p_fda_t {
	union  {
		struct {
			u32 pda_du:   3;
			u32 pda_pl:   4;
			u32 pda_lun:  4;
			u32 pda_ce:   4;
			u32 pda_ch:   4;
			u32 pda_pg:  13;
		} b;
		u32 all;
	} dw0;

	union {
		struct {
			u32 pda_blk : 14;
			u32 rsvd : 18;
		} b;
		u32 all;
	} dw1;
} l2p_fda_t;

typedef union _updt_req_t {
	struct {
		/* bit 0*/
		u32 lda;	///< lda to be updated

		/* bit 32*/
		union {
			u32 old_val;	///< old value to be compared, used if compare = 1
			struct {
				u32 lda_ofst : 12;
				u32 btag : 12;
				u32 rsvd : 8;
			} dtag_updt;
		};

		/* bit 64*/
		union {
			u32 pda;
			u32 dtag;
		};

		/* bit 96*/
		u32 lda_auto_inc:1;		///< lda is not a buffer, hw automatically increased
		u32 valid_cnt_adjust:1;		///< 1 to do valid count/valid bitmap adjust
		u32 dtag_or_pda:1;		///< 0 is PDA, 1 is DTAG
		u32 region:1;
		u32 compare:1;			///< if 1, update is done if old_val = l2p[lda]

		u32 lda_num:5;			///< lda number
		u32 fw_seq_id:20;		///< sequence id, meanless, bypass value
		u32 req_type:2;			///< 0 is update request, 1 is commit request
	} PACKED updt;
	struct {
		u32 lda;		///< lda to be updated

		u32 ofst:12;		///< du offset in btn command
		u32 btag:12;		///< btag of btn command
		u32 rsvd_56:8;

		u32 dtag_id:24;
		u32 rsvd_88:8;

		u64 lda_auto_inc:1;
		u64 rsvd_97:4;
		u64 lda_num:5;
		u64 rsvd_106:4;
		u64 fw_seq_id:16;
		u64 req_type:2;
	} PACKED cmmt;

	struct {
		u32 lda;
		u32 rvsd_32;

		u64 rsvd_64:23;
		u64 pattern:2;
		u64 valid_cnt_adjst:1;
		u64 lda_num:16;
		u64 fw_seq_id:20;
		u64 req_type:2;
	} PACKED trim;
} updt_req_t;
BUILD_BUG_ON(sizeof(updt_req_t) != 16);

/*!
 * @brief definition of update completion entry
 */
typedef union _updt_cpl_t {
	struct {
		u32 old_val;		///< old value in l2p

		u32 lda_ofst:12;
		u32 btag:12;
		u32 rsvd_56:8;

		u32 new_val;

		u32 rsvd_96:6;
		u32 old_dtag:1;
		u32 dtag_or_pda:1;
		u32 region:1;
		u32 status:1;
		u32 fw_seq_id:20;
		u32 req_type:2;
	} updt;

	struct {
		u32 old_val;
		u32 lda;
		u32 new_val;

		u32 rsvd_96:6;
		u32 old_dtag:1;
		u32 dtag_or_pda:1;
		u32 region:1;
		u32 status:1;
		u32 fw_seq_id:20;
		u32 req_type:2;
	} trim;

	struct {
		u32 old;
		u32 lda;
		u32 rsvd_64;

		u32 rsvd_96:10;
		u32 fw_seq_id:20;
		u32 req_type:2;
	} end_of_trim;
} updt_cpl_t;
BUILD_BUG_ON(sizeof(updt_cpl_t) != 16);

/*!
 * @brief definition of commit completion entry
 */
typedef struct _cmmt_cpl_t {
	u64 dtag:24;		///< dtag id in request
	u64 ofst:12;		///< du offset in origin command
	u64 btag:12;		///< command tag
	u64 seq_id:12;		///< sequence id in request
	u64 rsvd:4;
} cmmt_cpl_t;

/*!
 * @brief definition of status fetch count request
 */
typedef struct _fet_cnt_req_t {
	u32 ce_id  :  4;	///< ce id, useless now
	u32 ch_id  :  4;	///< channel id, useless now
	u32 spb_id : 14;	///< block id
	u32 rsvd   : 8;
	u32 fet_cnt_type : 2;	///< count table 0~3
} fet_cnt_req_t;
BUILD_BUG_ON(sizeof(fet_cnt_req_t) != 4);

/*!
 * @brief definition of status fetch count response
 */
typedef struct _fet_cnt_rsp_t {
	u64 ce_id  :  4;	///< ce id, useless now
	u64 ch_id  :  4;	///< channel id, useless now
	u64 spb_id : 14;	///< block id
	u64 rsvd1  : 10;
	u64 cnt: 32;		///< count now
} fet_cnt_rsp_t;
BUILD_BUG_ON(sizeof(fet_cnt_rsp_t) != 8);

typedef enum {
	SRCH_FOR_MIN = 0,
	SRCH_FOR_MIN_BUT_NO_ZERO = 1,
	SRCH_FOR_MAX = 2,
	SRCH_FOR_MAX_BUT_LESS_MAX = 3
} status_srch_op_t;

typedef enum {
	CNT_IN_SRAM = 0,	///< valid count in sram
	CNT_IN_DDR = 1,	///< valid count in ddr, not support
	CNT_IN_INTB = 2,	///< valid count in internal buffer
	CNT_IN_EXTEND = 3,	///< valid count in internal buffer, and extend to sram
	INV_CNT_LOC = 4
} l2p_cnt_loc_t;

enum {
	CNT_ENTRY_4B = 0,
	CNT_ENTRY_8B = 1
};

/*!
 * @brief definition of status count request
 */
typedef struct _cnt_req_t {
	u32 mask0;		///< cnt[grp][spb] & mask0 must be mask0
	u32 mask1;		///< cnt[grp][spb] & mask1 must be 0

	u16 start_entry_ptr;	///< search start off in table
	u16 srch_num;		///< search number

	u32 srch_cnt : 2;	///< status type number 0~3
	u32 srch_op_type : 2;	///< status_srch_op_t
	u32 fw_seq_id : 28;
} cnt_req_t;
BUILD_BUG_ON(sizeof(cnt_req_t) != 16);

/*!
 * @brief definition of valid count response
 */
typedef struct _cnt_rsp_t {
	u32 spb_id;		///< search result spb_id

	u32 srch_cnt_type : 2;	///< search count type
	u32 srch_op_type : 2;	///< search op type
	u32 fw_seq_id : 28;
} cnt_rsp_t;
BUILD_BUG_ON(sizeof(cnt_rsp_t) != 8);

typedef struct _cnt_notify_t {
	u64 cnt : 32;		///< count now
	u64 ce_id : 4;		///< ce id, useless now
	u64 ch_id : 4;		///< channel id, useless now
	u64 spb_id : 14;	///< block id
	u64 rsvd1 : 10;
} cnt_notify_t;
BUILD_BUG_ON(sizeof(cnt_notify_t) != 8);

/*!
 * @brief fda format firmware used
 */
typedef struct {
	u8 ch;
	u8 ce;
	u8 lun;
	u8 pl;
	u16 blk;
	u16 pg;
	u8 du;
} fda_t;

/*!
 * @brief definition of valid count / valid bitmap update request
 *
 * use this request to adjust valid bitmap, not used now
 */
typedef struct _dvl_info_updt_req_t {
	u64 cpda : 32;		///< target CPDA to be took off
	u64 dtag : 24;		///< bypass value
	u64 inv : 1;		///< 1 for invalidate, 0 for validate
	u64 rsvd : 7;
} dvl_info_updt_req_t;
BUILD_BUG_ON(sizeof(dvl_info_updt_req_t) != 8);

/*!
 * @brief definition of valid count / valid bitmap update result
 */
typedef struct _dvl_info_updt_cpl_t {
	u32 dtag : 24;		///< dtag field in request
	u32 inv : 1;		///< 1 for invalidate, 0 for validate
	u32 status : 1;		///< result, 0 is changed, 1 is not changed
	u32 rsvd : 6;
} dvl_info_updt_cpl_t;
BUILD_BUG_ON(sizeof(dvl_info_updt_cpl_t) != 4);

/*!
 * @brief definition of valid PDA generation request
 *
 * using in GC
 */
typedef struct _pda_gen_req_t {
	u32 lun : 4;		///< lun id
	u32 ce : 4;		///< ce id
	u32 ch : 4;		///< channel id
	u32 spb : 14;		///< block id
	u32 pde_gen_mode_sel : 1;	///< 0 : spb, 1: die
	u32 rsvd : 5;
} PACKED pda_gen_req_t;
BUILD_BUG_ON(sizeof(pda_gen_req_t) != 4);

typedef struct p2l_pda_gen_req_t {
	u32 dtag_id:24;
	u32 dtag_num:8;

	u16 p2l_id;
	u16 spb_id;

	u32 fw_seq_num;
	u32 rsvd;
} PACKED p2l_pda_gen_req_t;
BUILD_BUG_ON(sizeof(p2l_pda_gen_req_t) != 16);

/*!
 * @brief definition of valid PDA generation result entry
 *
 * valid pda entry when GC start
 */
typedef union _pda_gen_rst_t {
	struct {
		u32 cpda;		///< valid compact PDA
		u32 lda;

		u64 pda_ce : 4;		///< ce id
		u64 pda_ch : 4;		///< channel id
		u64 pda_pga : 13;	///< page id
		u64 pda_lun : 4;	///< lun id
		u64 pda_pl : 4;		///< plane id
		u64 pda_du : 3;		///< du

		u64 fw_seq_num : 32;
	} ent;
	struct {
		u32 ttl_vld;		/// total valid PDA output in this request
		u32 ttl_rsp;
		u32 end_of_mark;	/// should be 0xFFFFFFFF
		u32 fw_seq_num : 32;	/// sequence id
	} end;
} pda_gen_rsp_t;
BUILD_BUG_ON(sizeof(pda_gen_rsp_t) != 16);

typedef union _btn_info_t {
	u32 all;
	struct {
		u32 lda_ofst : 12;
		u32 btag : 12;
		u32 rsvd : 8;
	} b;
} btn_info_t;

/*!
 * @brief bitmap to control idle check
 */
typedef union _l2p_idle_ctrl_t {
	struct {
		u32 misc : 1;		/* misc queue */
		u32 srch : 1;		/* search request and response queue */
		u32 updt : 1;		/* update request */
		u32 updt_cmpl : 1;	/* update response */
		u32 gc : 1;		/* gc pda gen queue and response */
		u32 rsvd : 25;
		u32 all : 1;		/* check all */
		u32 wait : 1;		/* force wait, and timeout assert */
	} b;
	u32 all;
} l2p_idle_ctrl_t;

extern volatile u32 shr_ent_l2p_seg_shf;		///< shift of l2pp id to l2p segment id
#if (FTL_L2P_SEG_BMP == mENABLE)
extern u32 *shr_l2p_seg_dirty_bmp;	///< pointer of l2p dirty segment bitmap
extern u32 *shr_l2p_seg_flush_bmp;	///< pointer of l2p flush segment bitmap
#endif
extern u32 shr_l2p_seg_bmp_sz;		///< l2p segment bitmap size
extern volatile u32 shr_l2p_entry_need;		///< total l2pp count
extern volatile u32 shr_l2p_entry_start;		///< ddr dtag offset of l2p table
extern volatile u32 shr_l2p_seg_cnt;		///< total l2p segment count
extern volatile u32 shr_l2p_seg_sz;		///< l2p segment size
extern volatile u8 shr_l2pp_per_seg;		///< l2pp count per l2p segment
//extern u8 shr_l2pp_per_seg_shf;		///< shift value of l2pp count per l2p segment
extern volatile u32 shr_blklistbuffer_start[2];
extern volatile u32 shr_blklistbuffer_need;
extern volatile u32 shr_vac_drambuffer_start;
extern volatile u32 shr_vac_drambuffer_need;
extern volatile u32 poh;
extern volatile u32 pom_per_ms;

// added by Sunny, for ftl core fill meta
extern volatile u64 shr_page_sn;
extern volatile u8  shr_qbt_loop;
extern volatile u8  shr_pbt_loop;

extern volatile u16 shr_dummy_blk;

extern u32 user_min_ec;
extern u16 max_ec_blk;
extern u16 user_min_ec_blk;
extern u16 pre_wl_blk;

extern u8 evt_l2p_srch0;		///< event of l2p search PDA result group 0
extern u8 evt_l2p_srch1;		///< event of l2p search PDA result group 1
extern u8 evt_l2p_srch_umap;		///< event of l2p search unmap result
extern u8 evt_l2p_updt0;		///< event of l2p update completion queue 0
extern u8 evt_l2p_updt1;		///< event of l2p update completion queue 1
extern u8 evt_l2p_updt2;		///< event of l2p update completion queue 2
extern u8 evt_l2p_updt3;		///< event of l2p update completion queue 3
extern u8 evt_l2p_cmmt0;		///< event of l2p commit completion queue 0
extern u8 evt_l2p_cmmt1;		///< event of l2p commit completion queue 1
extern u8 evt_l2p_cmmt2;		///< event of l2p commit completion queue 2
extern u8 evt_l2p_cmmt3;		///< event of l2p commit completion queue 3
extern u8 evt_l2p_recycle_que;		///< event of l2p recycle queue
extern u8 evt_pda_gen_rsp0_que0;	///< event of valid pda generation response queue 0 for group 0
extern u8 evt_pda_gen_rsp1_que0;	///< event of valid pda generation response queue 0 for group 1
extern u8 evt_pda_gen_rsp0_que1;	///< event of valid pda generation response queue 1 for group 0
extern u8 evt_pda_gen_rsp1_que1;	///< event of valid pda generation response queue 1 for group 1
extern u8 evt_status_srch_rsp;		///< event of status count result queue
extern u8 evt_status_fet_rsp;		///< event of status count fetch result queue
extern u8 evt_dvl_info_updt_rslt;		///< event of valid bitmap update result queue
extern u8 evt_vcnt_notify;		///< event of valid count below threshold

/*!
 * @brief api to initialize nand layout to l2p engine, 1'base
 *
 * @param ch		number of channel
 * @param ce		number of ce per channel
 * @param lun		number of lun per ce
 * @param pl		number of plane per lun
 * @param blk		number of block per plane
 * @param page		number of page per block
 * @param du_cnt	number of du per page
 * @param pda_mode	true for PDA mode, otherwise CPDA
 *
 * @return		return last PDA in this layout
 */
pda_t l2p_addr_init(u32 ch, u32 ce, u32 lun, u32 pl, u32 blk, u32 page, u32 du_cnt, bool pda_mode);

/*!
 * @brief enable lt mode, set SPB ID field
 *
 * @param off		bit offset of SPB ID in pda
 * @param bits		bit count of SPB ID in pda
 * @param en		enable lt mode
 *
 * @return		not used
 */
void l2p_ltmode_enable(u32 off, u32 bits, bool en);

/*!
 * @brief api to search PDA from l2p according lda
 *
 * @param lda		start lda to be searched
 * @param cnt		search count
 * @param btn_tag	command tag
 * @param qid		search queue id
 * @param pri		search priority
 *
 * @return		return how many LDAs were issued
 */
u32 l2p_srch(lda_t lda, u32 cnt, u32 btn_tag, u32 qid, l2p_srch_pri_t pri);
u32 l2p_srch_cross(lda_t lda, u32 cnt, u32 btn_tag, u32 qid, l2p_srch_pri_t pri,u32 cross_ofst);//joe add 20200730//joe add 20200528  for read cross section case //joe add NS 20200813


/*!
 * @brief api to search PDA from l2p according lda with assigned offset
 *
 * @param lda		start lda to be searched
 * @param cnt		search count
 * @param btn_tag	command tag
 * @param ofst		start offset
 * @param qid		search queue id
 * @param pri		search priority
 *
 * @return		return how many LDAs were issued
 */
u32 l2p_srch_ofst(lda_t lda, u32 cnt, u32 btn_tag, u32 ofst, u32 qid, l2p_srch_pri_t pri);

/*!
 * @brief api to search single lda from l2p
 *
 * @param lda		search target lda
 * @param ofst		du offset of original command
 * @param btn_tag	command tag
 * @param qid		search queue id
 * @param pri		search priority
 *
 * @return		always 1
 */
u32 l2p_single_srch(lda_t lda, u32 ofst, u32 btn_tag, u32 qid, l2p_srch_pri_t pri);

#define l2p_srch0_nrm(l, c, b)		l2p_srch(l, c, b, SRCH_QUE0, SRCH_NRM);
#define l2p_srch0_high(l, c, b)		l2p_srch(l, c, b, SRCH_QUE0, SRCH_HIGH);
#define l2p_srch0_urg(l, c, b)		l2p_srch(l, c, b, SRCH_QUE0, SRCH_URG);
#define l2p_srch1_nrm(l, c, b)		l2p_srch(l, c, b, SRCH_QUE1, SRCH_NRM);
#define l2p_srch1_high(l, c, b)		l2p_srch(l, c, b, SRCH_QUE1, SRCH_HIGH);
#define l2p_srch1_urg(l, c, b)		l2p_srch(l, c, b, SRCH_QUE1, SRCH_URG);

/*!
 * @brief api to update single pda to l2p
 *
 * update a single fda to lda, asic will convert fda to cpda then update it
 * if cmp == 1, only update if l2p[lda] == pda
 * if adj == 1, if updated, adjust valid count and valid bitmap
 *
 * @param lda		target lda
 * @param new_pda	pda to be updated to l2p
 * @param pda		old valid in pda, only significant if cmp = 1
 * @param cmp		if need to compare old valid before updating
 * @param adj		adjust valid count and valid bitmap or not
 * @param qid		update queue id
 * @param id		sequence id, bypass value
 *
 * @return		not used
 */
void l2p_updt_pda(lda_t lda, pda_t new_pda, pda_t pda, bool cmp, bool adj, u32 qid, u32 id);

/*!
 * @brief trim l2p for a lda range
 *
 * @param lda		start lda to be trimmed
 * @param pat		trim patterm
 * @param adj		adjust or not adjust valid count
 * @param num		number of lda
 * @param qid		qid
 * @param id		id
 *
 * @return		not used
 */
void l2p_updt_trim(lda_t lda, l2p_trim_pat_t pat, bool adj, u32 num, u32 qid, u32 id);

/*!
 * @brief l2p update sequential pda into table,
 *
 * @param new		pda to be updated
 * @param lda		target lda
 * @param lda_list	lda list, may be NULL, then lda will be automatically increased
 * @param cnt		update count
 * @param old		if need to compare old valid before updating
 * @param dtag		if new value was dtag
 * @param adj		adjust valid count and valid bitmap or not
 * @param qid		update queue id
 * @param id		sequence id, bypass value
 *
 * @return		not used
 */
void l2p_updt_bulk(u32 *new, lda_t lda, lda_t *lda_list, u32 cnt, u32 *old, bool dtag, bool adj, u32 qid, u32 id);
static inline void l2p_updt_pda_bulk(pda_t *pda, lda_t lda, lda_t *lda_list, u32 cnt, pda_t *old_list, bool adj, u32 qid, u32 id)
{
	l2p_updt_bulk((u32 *) pda, lda, lda_list, cnt, (u32 *) old_list, false, adj, qid, id);
}
static inline void l2p_updt_dtag_bulk(dtag_t *dtag, lda_t lda, lda_t *lda_list, u32 cnt, btn_info_t *info_list, bool adj, u32 qid, u32 id)
{
	l2p_updt_bulk((u32 *) dtag, lda, lda_list, cnt, (u32 *) info_list, true, adj, qid, id);
}

/*!
 * @brief api to update pda list to l2p
 *
 * @param lda	lda list to be updated
 * @param fda	start fda
 * @param pda	old pda list to be compared
 * @param cnt	list length
 * @param qid	update queue id
 * @param cmpr	compare enable
 *
 * @return	not used
 */
void l2p_updt_pdas(lda_t *lda, pda_t new_pda, pda_t *pda, u32 cnt, u32 qid, bool cmpr);

/*!
 * @brief api to update dtag list to l2p
 *
 * @param dtag_list	dtag list to be updated
 * @param lda_list	lda list to be updated
 * @param btag_list	command tag list
 * @param cnt		list length
 * @param qid		update queue id
 *
 * @return		not used
 */
void l2p_updt_dtags(u32 *dtag_list, lda_t *lda_list, u16 *btag_list, u32 cnt, u32 qid);

/*!
 * @brief api to update single dtag to l2p
 *
 * @param lda		lda to be updated
 * @param dtag		dtag to be updated
 * @param btag		btag
 * @param lda_ofst	lda offset
 * @param adj		adjust or not
 * @param qid		update queue id
 * @param id		sequence id, bypass value
 *
 * @return		not used
 */
void l2p_updt_dtag(lda_t lda, dtag_t dtag, u32 btag, u32 lda_ofst, bool adj, u32 qid, u32 id);

/*!
 * @brief api to commit pdas to l2p
 *
 * commit request will not change l2p table, but bypass information to response queue like ipc
 *
 * @param llist		lda list to be committed
 * @param dtags		dtag list to be committed
 * @param cnt		list length
 * @param qid		update queue id
 *
 * @return		not used
 */
void l2p_cmmt_pdas(lda_t *llist, u32 *dtags, u32 cnt, u32 qid);

/*!
 * @brief api to commit single pda to l2p
 *
 * @param lda		single lda to be committed
 * @param dtag		dtag to be committed
 * @param btag		command tag
 * @param ofst		du offset
 * @param cmp		compare or not, useless
 * @param seq_id	sequence id, bypass value
 * @param qid		update queue id
 *
 * @return		not used
 */
void l2p_cmmt(lda_t lda, dtag_t dtag, u32 btag, u32 ofst, bool cmp, u32 seq_id, u32 qid);

/*!
 * @brief get valid count of spb, only spb mode
 *
 * @param spb_id	target spb_id
 * @param ch_id		useless, set 0
 * @param ce_id		useless, set 0
 *
 * @return		not used
 */
void l2p_status_cnt_fetch(u32 spb_id, u32 ch_id, u32 ce_id);

/*!
 * @brief api to update valid bitmap and valid count table
 *
 * @param dtag		dtag id, bypass value
 * @param cpda		compact pda to be invalidate or validate
 * @param inv		invalidate or validate
 *
 * @return		not used
 */
void dvl_info_updt(dtag_t dtag, pda_t cpda, bool inv);

/*!
 * @brief api to generate valid pda of target spb
 *
 * taret SPB should be GC candidate
 *
 * @param spb_id	target spb
 * @param ch		useless now, must be 0
 * @param ce		useless now, must be 0
 * @param lun		useless now, must be 0
 * @param die_base	die base or spb base, always use spb base now
 * @param qid		pda gen queue id
 *
 * @return		not used
 */
void valid_pda_gen(u32 spb_id, u32 ch, u32 ce, u32 lun, bool die_base, u32 qid);

/*!
 * @brief use p2l to generate valid pda
 *
 * @param dtag		dtag of P2L
 * @param num		number of dtag
 * @param p2l_id	p2l id
 * @param spb_id	spb id
 * @param seq		sequence id
 * @param qid		queue id
 *
 * @return		not used
 */
void p2l_valid_pda_gen(dtag_t dtag, u32 num, u32 p2l_id, u32 spb_id, u32 seq, u32 qid);

/*!
 * @brief api to handle search unmapped resule queue
 *
 * @return		not used
 */
void l2p_srch_unmap_poll(void);

/*!
 * @brief api to return source entry of pda gen result entry buffer
 *
 * @param ptr_list	source entry list
 * @param count		list length
 * @param grp		group to be return, 0 or 1
 *
 * @return		not used
 */
void l2p_pda_grp_rsp_ret(u32 *ptr_list, u32 count, u32 grp);

/*!
 * @brief set a lun to a specific group, used in nvme set
 *
 * @param ch		channel id
 * @param ce		target id in channel
 * @param grp_id	group id
 *
 * @return		not used
 */
void l2p_mgr_die_grp(u32 ch, u32 ce, u32 grp_id);

/*!
 * @brief initialize dtag setting of L2P
 *
 * @param max_ddtag	max dtag count
 *
 * @return		not used
 */
void l2p_mgr_dtag_init(u32 max_ddtag);

/*!
 * @brief initialize l2p mgr buffer
 *
 * @param l2p_base	l2p table start address
 * @param l2p_size	l2p table size in byte
 * @param vbmp_base	vbmp table start address
 * @param vbmp_size	vbmp table size in byte
 * @param vcnt_base	vcnt table start address
 * @param vcnt_size	vcnt table size in byte (must be the spb count * 4)
 * @param vcnt_loc	valid count table location
 *
 * @return		not used
 */
void l2p_mgr_buf_init(u64 l2p_base, u64 l2p_size, u64 vbmp_base, u64 vbmp_size,
		u64 vcnt_base, u64 vcnt_size, l2p_cnt_loc_t vcnt_loc);

/*!
 * @brief control l2p behavior
 *
 * @param vbmp_used	true if vbmp was used instead of p2l
 * @param max_lda	max lda
 *
 * @return		not used
 */
void l2p_mgr_ctrl(bool vbmp_used, u32 max_lda);

/*!
 * @brief initialize l2p manager
 *
 * @return		not used
 */
void l2p_mgr_init(void);

/*!
 * @brief after enabled, trim completion response will not be updated to completion queue
 *
 * @param en		enable or disable
 * @param qid		udpate completion queue id
 *
 * @return		not used
 */
void l2p_mgr_trim_cmpl_filter(bool en, u32 qid);

/*!
 * @brief after enabled, only DTAG in l2p will be pushed into l2p recycle queue
 *
 * @param en		enable or disable
 *
 * @return		not used
 */
void l2p_mgr_dtag_recycle_que_filter(bool en);

/*!
 * @brief switch update/commit response queue
 *
 * @param en		enable response queue
 * @param qid		queue id
 * @param updt		true for update completion, false for commit completion
 *
 * @return		not used
 */
void l2p_mgr_switch_rsp_que(bool en, u32 qid, bool udpt);

/*!
 * @brief switch recycle queue
 *
 * @param en		enable recycle queue if true
 *
 * @return		not used
 */
void l2p_mgr_switch_recycle_que(bool en);

/*!
 * @brief poll recycle queue
 *
 * @param en		poll recycle queue
 *
 * @return		not used
 */
void l2p_mgr_recycle_que_poll(void);

/*!
 * @brief rebuild valid count table synchronously
 *
 * @return		return false if overflow detected
 */
bool l2p_mgr_vcnt_rebuild(void);

/*!
 * @brief move valid count table between l2p internal sram and external ram
 *
 * @param dir		0 for backup to buffer, 1, for restore from buffer
 * @param buffer	external buffer
 * @param size		buffer length
 *
 * @return		not used
 */
void l2p_mgr_vcnt_move(bool dir, void *buffer, u32 size);

/*!
 * @brief enable or disable l2p range check
 *
 * @param updt		true for update, false for search
 *
 * @return		not used
 */
void l2p_mgr_range_chk_ctrl(bool en, bool updt);

/*!
 * @brief set l2p segment dirty
 *
 * @param lda	lda
 *
 * @return	not used
 */
static inline void l2p_mgr_set_seg_dirty(lda_t lda)
{
//	u32 seg_id;//adams no update L2P

//	seg_id = lda >> shr_ent_l2p_seg_shf;
//	set_bit(seg_id, shr_l2p_seg_dirty_bmp);
}

/*!
 * @brief wait l2p queue idle, only check queue in current CPU
 *
 * @param wait		idle ctrl bitmap
 *
 * @return		return busy queue number
 */
int wait_l2p_idle(l2p_idle_ctrl_t ctrl);

/*!
 * @brief l2p resume function due to btn reset
 *
 * @return		not used
 */
void l2p_mgr_reset_resume(void);

/*!
 * @brief l2p reset function due to btn reset, call before btn reset, it is suspend function indeed
 *
 * @return		not used
 */
void l2p_mgr_reset(void);
bool l2p_srch_swq_check(void);
bool l2p_sw_que_full_check(u32 swq_id);

/*! @} */
