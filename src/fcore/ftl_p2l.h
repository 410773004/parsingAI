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
/*! \file ftl_p2l.h
 * @brief define P2L behavior in ftl core, included initilization, update, flush, load, recover
 *
 * \addtogroup fcore
 * \defgroup p2l
 * \ingroup fcore
 *
 */
#pragma once

#include "types.h"
#include "eccu.h"
#include "nand.h"
#include "die_que.h"
#include "pstream.h"
#include "ftl_raid.h"
#include "ftl_export.h"
#include "ftl_core.h"
#include "fc_export.h"

#define GRP_FREE_RES_ID	0xFF
//#define MAX_MP_DU_CNT		(MAX_PLANE * DU_CNT_PER_PAGE)
#define TTL_P2L_LOAD_CMD	(4)
//#define DDR_P2L_DTAG_START  (DDR_GC_DTAG_START + FCORE_GC_DTAG_CNT + 32)
//#define MAX_P2L_CNT		((MAX_MP_DU_CNT + 2) * FTL_CORE_MAX * (TOTAL_NS - 2))	///< total p2l res count, exclude ns 0 and int ns
#define MAX_P2L_SCAN_CMD	(16)	///< max p2l scan command count to rebuild p2l
#define MAX_P2L_SDTAG_CNT	(15)
#define GROUP_P2L_PGSN_DTAG_CNT     (5) // 3 + 2

#define P2L_BUILD_NCL_CMD_MAX  (16)
#define DU_PER_P2L_GRP     (3072) //8CH*8CE*LUN*2PN*4(4K)*3(PAGE)
#define P2L_PENDING_CNT_MAX (384)  // 448 adams
//#define DU_PER_SPAGE        (get_du_cnt_in_native_spb()/get_page_per_block())
//#define DU_PER_SWL          (FTL_DU_PER_SPAGE*get_nr_bits_per_cell())
#define DU_PER_SPAGE        (256*4)
#define DU_PER_SWL          (FTL_DU_PER_SPAGE*shr_nand_info.bit_per_cell)
#define DU_PER_WL           (DU_CNT_PER_PAGE*shr_nand_info.bit_per_cell)


typedef CBF(dtag_t, 32) p2l_sdtag_pool_t;

typedef struct _p2l_t {
	u16 id;	///< p2l id in a spb
	u16 updt_cnt;	///< updated entry count

	dtag_t ddtag;	///< ddr dtag
	lda_t *dmem;	///< ddr dtag mem

	dtag_t sdtag;	///< sram dtag
	lda_t* smem;	///< sram dtag mem

	p2l_grp_t *grp;	///< p2l group the p2l belongs to

	union {
		struct {
			u32 ddtag_only : 1;	///< no sram dtag in this p2l
			u32 sdtag_flush : 1;	///< flush to nand with sram dtag
			u32 copy_send : 1;///< sdtag copy to ddtag send
			u32 copy_done : 1;	///< sdtag copy to ddtag done
			u32 force_flush : 1;	///< flush p2l when update don't cmpl
			u32 flush_done : 1;	///< p2l flush done
			u32 init_done : 1;
		} b;
		u32 all;
	} flags;
} p2l_t;

typedef struct _p2l_para_t {
	u16 pg_per_p2l;	///< interleave page count per p2l
	u16 pg_p2l_shift;
	u32 pg_p2l_mask;

	u16 pg_per_grp;	///< interleave page count per p2l group
	//u16 pg_grp_shift;
	//u32 pg_grp_mask;

	u16 p2l_per_grp;	///< p2l count per p2l group
	//u16 p2l_grp_shift;
	//u32 p2l_grp_mask;

	u16 slc_p2l_cnt;
	u16 slc_grp_cnt;

	u16 xlc_p2l_cnt;
	u16 xlc_grp_cnt;
} p2l_para_t;

typedef struct _p2l_mgr_res_t {
	p2l_t p2l[MAX_P2L_CNT];	///< p2l entries
	u32 bmp[occupied_by(MAX_P2L_CNT, 32)];	///< p2l avail bitmap
	u16 avail;	///< p2l avail count
	u16 total;	///< p2l total count
	u32 ddtag_start;	///< p2l ddr dtag start
	u32 dbg_ddtag_start;	///< p2l debug ddr dtag start
	u32 dbg_p2l_build_ddtag_start;	///< p2l debug ddr dtag start
	p2l_sdtag_pool_t sdtag_pool;	///< p2l sram dtag pool
} p2l_mgr_res_t;

typedef struct _p2l_ncl_cmd_t {
	struct ncl_cmd_t ncl_cmd;		///< ncl command
	pda_t pda[MAX_MP_DU_CNT];			///< pda buffer
	bm_pl_t bm_pl[MAX_MP_DU_CNT];	///< bm payload buffer
	struct info_param_t info[MAX_MP_DU_CNT];	///< info buffer
} p2l_ncl_cmd_t;

typedef struct _p2l_load_mgr_t {
	p2l_ncl_cmd_t cmd[TTL_P2L_LOAD_CMD];///< p2l load cmds
	u32 bmp;	///< cmds avail bitmap
	u16 avail;	///< cmds avail count
} p2l_load_mgr_t;

typedef struct _p2l_build_req_t {
	struct list_head entry;
	spb_id_t spb_id;
	u32 start_page;
	u32 last_page;
	dtag_t dtags[MAX_MP_DU_CNT * 2];

	void* ctx;
	completion_t cmpl;
} p2l_build_req_t;

typedef struct _newp2l_build_req_t {
	struct list_head entry;
	spb_id_t spb_id;
	u16 p2l_id;
	u16 p2l_cnt;
    u16 grp_id;
	dtag_t dtags[MAX_MP_DU_CNT * 2];

	void* ctx;
	completion_t cmpl;
} np2l_build_req_t;


//typedef struct _p2l_build_mgr_t {
//	struct list_head wait_que;	///< waiting p2l build req
//	p2l_build_req_t *busy_req;	///< p2l build req which is executing
//
//	//meta scan res
//	u32 meta_idx;	///< ddr meta index
//	io_meta_t *meta;	///< ddr meta addr
//	p2l_ncl_cmd_t scan_cmd[MAX_P2L_SCAN_CMD];	///< command to scan meta
//
//	u16 p2l_id;		///< building p2l id
//	u16 scan_page;	///< scaning page number
//	u16 scan_die;	///< last scan die number
//	u16 otf_req;		///< flying scan command
//} p2l_build_mgr_t;
//
//typedef struct _newp2l_build_mgr_t {
//	np2l_build_req_t *busy_req;	///< p2l build req which is executing
//
//	//meta scan res
//	u32 meta_idx;	///< ddr meta index
//	io_meta_t *meta;	///< ddr meta addr
//	p2l_ncl_cmd_t scan_cmd[MAX_P2L_SCAN_CMD];	///< command to scan meta
//
////	u16 p2l_id;		///< building p2l id
////	u16 scan_page;	///< scaning page number
////	u16 scan_die;	///< last scan die number
////	u16 otf_req;		///< flying scan command
//} np2l_build_mgr_t;

typedef struct _pending_p2l_build_t {
	u32 spb_id;	
    u16 p2l_id;
    p2l_ncl_cmd_t *p2l_build_ncl_list;
    p2l_load_req_t *p2l_build_load_list;
} pending_p2l_build_t;
//extern slow_data pending_p2l_build_t *pending_p2l_build_tbl;

extern p2l_para_t p2l_para;
extern volatile p2l_ext_para_t p2l_ext_para;

typedef struct _p2l_build_pending_list_t {
    u32 p2l_build_pend_cnt;
    u32 p2l_build_tmp_cnt;
	pending_p2l_build_t *pending_p2l_build_buf;
} p2l_build_pending_list_t;

/*!
 * @brief  p2l module init
 *
 * @return	not used
 */
void p2l_init(void);

/*!
 * @brief  p2l user init
 *
 * @param pu	p2l user
 *
 * @return	not used
 */
void p2l_user_init(p2l_user_t *pu, u32 nsid);
void p2l_build_aux(u32 spb_id,u16 p2l_id);
void page_aux_build_p2l_done(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief p2l user reset, recycle all resource in p2l user
 *
 * @param pu	p2l user
 * @param nsid	ns id
 *
 * @return	not used
 */
void p2l_user_reset(p2l_user_t *pu, u32 nsid);

/*!
 * @brief update req's lda into p2l
 *
 * @param ps		pstream
 * @param req	write req
 *
 * @return	not used
 */
void p2l_update(pstream_t *ps, ncl_w_req_t *req);
void pagesn_update(pstream_t *ps, ncl_w_req_t *req);
#if (PLP_SUPPORT == 0) 
u32 pda2wl(pda_t pda);
#endif
#if 0
/*!
 * @brief load p2l into dtags
 *
 * @param load_req	p2l load request, it includes detailed infomation
 *
 * @return	not used
 */
void p2l_load(p2l_load_req_t *load_req);
#endif
void p2l_req_pl_ins(u32 p2l_grp_id, p2l_user_t *pu, ncl_w_req_t *req, bool ins_p2l);


/*!
 * @brief insert p2l lda & dtag into req payload
 *
 * @param cwl	core wordline
 * @param pu	p2l user
 * @param req	write req which include p2l data
 *
 * @return	not used
 */
void p2l_get_next_pos(aspb_t *aspb, p2l_user_t *pu, u32 nsid);

#ifndef DUAL_BE
void set_ncl_cmd_p2l_info(ncl_req_t *req, struct ncl_cmd_t *ncl_cmd);
#endif

/*!
 * @brief check if submit p2l write req
 *
 * @param cwl	core wordline
 * @param grp	p2l group
 *
 * @return	true if it's time to submit p2l write req
 */
//bool p2l_req_submit_chk(core_wl_t *cwl, p2l_grp_t *grp, u32 nsid);

/*!
 * @brief submit p2l write req
 *
 * @param cwl	core wordline
 * @param grp	p2l group
 * @param req	write req which include p2l data
 *
 * @return	not used
 */
//void p2l_req_submit(core_wl_t *cwl, p2l_grp_t* grp, ncl_w_req_t *req);

/*!
 * @brief p2l group normal prog done handle
 *
 * @param grp	p2l group
 * @param reset	if called by reset function
 *
 * @return	not used
 */
void p2l_grp_nrm_done(p2l_grp_t *grp, bool reset);

/*!
 * @brief p2l group shutdown pad prog done handle
 *
 * @param grp	p2l group
 *
 * @return	not used
 */
void p2l_grp_pad_done(p2l_grp_t *grp);

/*!
 * @brief insert open p2l group's p2l dtag into write payload when shutdown
 *
 * @param pu	p2l user
 * @param cwl	shutdown pad cwl
 * @param wr_pl	write payload to insert
 *
 * @return	not used
 */

/*!
 * @brief get p2l group's last page
 *
 * @param grp_id	p2l group id
 * @param slc		slc or xlc spb
 *
 * @return	the last page of p2l group
 */
u32 p2l_get_grp_last_page(u32 grp_id, bool slc);

/*!
 * @brief get p2l group's p2l count
 *
 * @param grp_id	p2l group id
 * @param slc		slc or xlc spb
 *
 * @return	the p2l count of p2l group
 */
u32 p2l_get_grp_p2l_cnt(u32 grp_id, bool slc);

/*!
 * @brief restore open p2l group when clean boot
 *
 * @param aspb	active spb
 * @param pu	p2l user
 * @param nsid	namespace id
 *
 * @return	not used
 */
void p2l_open_grp_restore(aspb_t *aspb, p2l_user_t *pu, u32 nsid);

/*!
 * @brief load p2l data from pad cwl when pmu resume
 *
 * @param aspb	active spb
 * @param grp	open p2l grp
 * @param nsid	namespace id
 *
 * @return	not used
 */
//void p2l_resume(aspb_t *aspb, p2l_grp_t *grp, u32 nsid);

/*!
 * @brief build p2l with meta
 *
 * @param build_req	p2l build request
 *
 * @return	not used
 */
void p2l_build_push(p2l_build_req_t *build_req);

static inline u32 get_p2l_per_grp(void)
{
	return p2l_ext_para.p2l_per_grp;
}

static inline u32 get_slc_p2l_cnt(void)
{
	return p2l_ext_para.slc_p2l_cnt;
}

static inline u32 get_xlc_p2l_cnt(void)
{
	return p2l_ext_para.xlc_p2l_cnt;
}

static inline u32 get_page_per_p2l(void)
{
	return p2l_para.pg_per_p2l;
}

static inline u32 page_to_p2l_id(u32 page)
{
	return page >> p2l_para.pg_p2l_shift;
}

static inline u32 page_to_p2l_grp_id(u32 page)
{
	return page / p2l_para.p2l_per_grp;
}

static inline u32 p2l_get_grp_first_page(u32 grp_id)
{
	return grp_id * p2l_para.p2l_per_grp;
}

