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
/*! \file pstream.h
 * @brief define physical stream object, supply physical page resource
 *
 * \addtogroup fcore
 * \defgroup pstream
 * \ingroup fcore
 * @{
 * it will help to allocate new SPB from FTL, and provide page resource
 */
#pragma once
#include "fc_export.h"
#include "cbf.h"

#define INV_P2L_GRP_ID	0xFFFF
#define CWL_FREE_RID	0xFFFF		///< free rid to use
#define GRP_PER_P2L_USR	3

#ifdef SKIP_MODE
share_data_zi u8* gl_pt_defect_tbl;
u8* get_spb_defect(u32 spb_id);
u8 defect_check(u8* ftl_df, u32 interleave);
u8 get_defect_pl_pair(u8* ftl_df, u32 interleave);


#endif

/*!
 * @brief definition of active spb
 */
typedef struct _aspb_t {
	spb_id_t spb_id;		///< spb id

	u32 wptr;			///< write pointer
	u32 max_ptr;			///< max write pointer
	u32 cmpl_cnt;			///< number of complete page
	u32 cmpl_ptr;			///< safe completed pointer

	sn_t sn;			///< serial number of active spb
	spb_id_t *remap_tbl;		///< remap table of this spb
	u8 total_bad_die_cnt;
	u8 parity_die;			///< die id to save parity
	u8 parity_die_pln_idx;			///< pn id to save parity
	u8 parity_die_pln_pair;
#ifdef SKIP_MODE                    // for skip mode
    u8 open_skip_cnt;
	u32 defect_max_ptr;			///< max write pointer
#endif
	u8 p2l_die;     //p2l interleave
    u8 p2l_plane;
    u8 pgsn_plane;
    u16 p2l_grp_id;
    u16 p2l_page;
    u16 pgsn_page;
    u16 ttl_usr_intlv;
    union {
		struct {
			u32 slc : 1;	///< if this SPB was slc or not
			u32 rinfo : 1;	///< if the SPB have range info
			u32 valid : 1;	///< if the SPB info valid
			u32 err : 1;	///< aspd error happened
		    u32 print : 1;
		    u32 plp_tag : 2;///< true if set FTL_PLP_TAG in meta
		    u32 parity_mix : 1;	///< good pn cnt of parity die > 1, this die has user data
        } b;
		u32 all;
	} flags;

} aspb_t;

#define MAX_PGSN_TABLE_CNT  4
typedef struct _p2l_grp_t {
	u16 grp_id;	///< p2l group id in spb
	u16 ttl_pgsn_cnt;
	u16 ttl_p2l_cnt;	///< total p2l count in this group
	u16 cur_p2l_cnt;	///< current allocated p2l cnt
	u16 updt_p2l_cnt;	///< updt cmpl p2l cnt
	u16 pgsn_ofst;
	void* pgsn[MAX_PGSN_TABLE_CNT];
    void* pda;
    //pgsn_ctl_t pgsn;
	u8 res_id[MAX_PLANE * DU_CNT_PER_PAGE];	///< p2l res id
} p2l_grp_t;

typedef struct _p2l_user_t {
	u16 flush_die;	///< next p2l flush die
	u16 flush_page;	///< next p2l flush page
	p2l_grp_t grp[GRP_PER_P2L_USR];	///< active p2l group
} p2l_user_t;

/*!
 * @brief definition of physical stream
 */
typedef struct _pstream_t {
	aspb_t aspb[MAX_ASPB_IN_PS];	///< aspb
	p2l_user_t pu[MAX_ASPB_IN_PS];	///< p2l user
	u8 curr;			///< current slot
	u8 du_cnt_per_cmd;		///< du count per command
	u8 nsid;			///< namespace id
	u8 type;			///< type
	u8 qbt_cnt;
    u8 parity_die;
    u8 parity_cnt;
	union {
		u8 all;
		struct {
			u8 queried : 1;	///< query for spb
			u8 query_nxt :1;
			u8 flush_blklist_start :1;
			u8 flush_blklist_suspend :1;
			u8 flush_blklist_done :1;
			u8 closing :1;
			u8 p2l_enable:1; 
		} b;
	} flags;

	u32 avail_cnt;			///< remain available page count
	spb_queue_t *spb_que;		///< queue for spb
} pstream_t;

/*!
 * @brief definition of core WL, core WL is composed by all user interleave WL
 *
 * Ex: TOSHIBA BiCS4, WL is 3 page, multiple-plane WL is 2 * 3 pages, core WL is interleave * 3
 */
typedef struct _core_wl_t {
	struct {
		u16 mpl_qt;		///< remain page quota
		u16 die_qt;		///< remain die quota
		u16 usr_mpl_qt;	///< remain usr page quota

		u16 die;		///< pointer to operating die
		u16 page;		///< page number of current cwl

		u16 open_die;		///< number of open die
		u16 start_die;		///< die start to open
		u16 next_open;		///< die next to open

		u16 issue_cnt;		///< issued req cnt
		u16 exp_issue_cnt;
		//u16 rsvd;
#ifdef SKIP_MODE
		u8 skip_die_cnt;
        u8 total_skip_cnt_in_ce;
        u8 good_die_bm[MAX_PLANE];
#endif
        u8 handle_ce_die; ///<current handle plane number
	} cur;

	u16 p2l_die;	///< die to save p2l data
	u16 p2l_page;	///< page to save p2l data

	union {
		struct {
			u32 slc : 1;	///< aspb slc
			u32 p2l : 1;	///< p2l data in this cwl
		} b;
		u32 all;
	} flags;

	u16 rid[MAX_DIE];		///< rid of operating request, support 256 die
} core_wl_t;

/*!
 * @brief initialize physical stream
 *
 * @param ps			physical stream object
 * @param du_cnt_per_cmd	du count per single command, usually it's du count of multiple plane
 * @param spb_que		allocated spb queue, used to communicate with FTL
 * @param nsid			namespace id of this stream
 * @param type			type, FTL_CORE_XXX, (NRM, GC)
 *
 * @return			not used
 */
void pstream_init(pstream_t *ps, u8 du_cnt_per_cmd, spb_queue_t *spb_que, u8 nsid, u8 type);

/*!
 * @brief continue to allocate new spb for physical stream until it was full
 *
 * @param ps		physical stream object
 *
 * @return		return true if it was already full
 */
bool pstream_rsvd(pstream_t *ps);

/*!
 * @brief get an allocated spb from spb queue and supply it to physical stream
 *
 * @param ps		physical stream object
 *
 * @return		true for supplied
 */
bool pstream_supply(pstream_t *ps);

/*!
 * @brief get pda list from physical stream
 *
 * @param ps		physical stream object
 * @param pda		pda list, it's remapped, it should be used to program
 * @param l2p_pda	l2p pda list, it's not remapped, it should be used to insert to l2p
 * @param page_cnt	page count to be allocated
 * @param exp_fact	expand factor
 *
 * @return		return active spb slot id of allocated page
 */
#ifdef SKIP_MODE
u8* get_spb_defect(u32 spb_id);
u32 pstream_get_pda(pstream_t *ps, pda_t *pda, pda_t *l2p_pda, u32 page_cnt, u8 exp_fact, ncl_w_req_t *req);
#else
u32 pstream_get_pda(pstream_t *ps, pda_t *pda, pda_t *l2p_pda, u32 page_cnt, u8 exp_fact);
#endif
/*!
 * @brief add programmed page count of physical stream
 *
 * @param ps		physical stream object
 * @param req		completed write req
 *
 * @return		not used
 */
void pstream_done(pstream_t *ps, ncl_w_req_t *req);

/*!
 * @brief get a core WL in physical stream
 *
 * @param ps		physical stream
 * @param cwl		core WL to be updated
 *
 * @return		not used
 */
void pstream_get_cwl(pstream_t *ps, core_wl_t *cwl);

/*!
 * @brief restore a physical stream object after clean boot
 *
 * @param ps		physical stream object
 * @param idx		active spb slot index
 * @param spb_fence	spb detailed fence
 *
 * @return		not used
 */
void pstream_restore(pstream_t *ps, u32 idx, spb_fence_t *spb_fence);

/*!
 * @param check if this physical stream need to be padded or not
 *
 * @param ps	physical stream object
 *
 * @return	return false to pad this physical stream
 */
static inline bool is_ps_no_need_to_pad(pstream_t *ps)
{
	aspb_t *aspb = &ps->aspb[ps->curr];

	if(ps->nsid == HOST_NS_ID && ps->type == FTL_CORE_SLC)
		return false;
	if (aspb->spb_id == INV_SPB_ID)
		return true;

	if (aspb->wptr == 0)
		return true;

	if (aspb->wptr == aspb->max_ptr)
		return true;

	/*if (ftl_core_get_spb_type(aspb->spb_id))
		return true;*/

	return false;
}

/*!
 * @brief reset pstream data structure
 *
 * @param ps	physical stream object
 *
 * @return	not used
 */
void pstream_reset(pstream_t *ps);

/*!
 * @brief resume pstream when pmu resume
 *
 * @param ps	physical stream object
 *
 * @return	not used
 */
void pstream_resume(pstream_t *ps);

/*!
 * @brief interface to check pstream ready
 *
 * @param ps	physical stream object
 *
 * @return	true for ready
 */
bool pstream_ready(pstream_t *ps);
bool pstream_full(pstream_t *ps);
void pstream_clear_epm_vac(void);


u8 pstream_get_bad_die_cnt(pstream_t *ps);

/*! @} */
