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

/*! \file sie_que.h
 * @brief scheduler module, ncl die command scheduler
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

#pragma once

#include "pool.h"
#include "stdlib.h"
#include "nand_cfg.h"
#include "ftltype.h"
#include "ftl_export.h"
#define __FILEID__ dieque
#include "trace.h"

#if defined(USE_MU_NAND)
#define EACH_DIE_DTAG_CNT	(16 * 2)		///< dtag count for each die command, B17 MAX
#else
#define EACH_DIE_DTAG_CNT	(48)//(24)		///< dtag count for each die command
#endif

#define EACH_DIE_ERASE_PDA_CNT	(4)			///< pda count for each die erase
#define FTL_TBL_DDR			(4)	//TBL_DDR + TBL_SRAM max 128 (pis)
#define FTL_TBL_SRAM		(0)
#define MAX_FTL_TBL_IO		(FTL_TBL_DDR+FTL_TBL_SRAM)			///< number of table flush io request count

#define RDISK_NCL_W_REQ_CNT	(MAX_DIE_MGR_CNT * 2 + 30)	///< ncl request count

#define RDISK_NCL_E_REQ_CNT 64//128			///< number of ncl erase request

#define MAX_PPU_LIST 192

//#define RD_CNT_UPDT_PERIOD	10000			///< total read counter update period
//#define RD_CNT_THRESHOLD    15000 * 1152     //shr_nand_info.geo.nr_pages

#if (CPU_ID == 2)
#define CHANGE_DIENUM_G2L(dienum)   ((dienum & 0x3) + ((dienum & 0XFC) >> 1))
#elif (CPU_ID == 4)
#define CHANGE_DIENUM_G2L(dienum)   (((dienum - 4) & 0x3) + (((dienum - 4) & 0XFC) >> 1))
#endif

typedef union die_ent_info_t {
	struct {
		u8 host : 1;		///< host command
		u8 slc : 1;		///< slc command
		u8 stream : 1;		///< streaming read
		u8 gc : 1;		///< gc command
		u8 single : 1;		///< single read
		u8 debug : 1;
		u8 pl : 2;		///< plane
		u8 ra : 1;		///< read ahead
		u8 rsvd : 7;
	} b;
	u16 all;
} die_ent_info_t;

#define DIE_QUE_SZ		die_que_size		///< size of each die queue
#define ROUND_SZ		16			///< round size, 16 for 4-plane
#define DIE_QUE_PH_SZ		(DIE_QUE_SZ + ROUND_SZ)	///< phisical size of each die queue
#define SORT_DIE_QUE_SZ		1024			///< size of sort die queue
#define SORT_DIE_QUE_MASK	(SORT_DIE_QUE_SZ - 1)	///< mask of sort die queue
#define RSV_DIE_QUE_SZ		256			///< size of reserved die queue
#define DIE_QUE_GC_SZ       DIE_QUE_SZ
#define DIE_QUE_PH_GC_SZ    (DIE_QUE_GC_SZ+ROUND_SZ)

#define DIE_Q_RD	0		///< read die queue index
#define DIE_Q_GC    1
#define DIE_Q_MAX	2		///< total number of die queue per die

#if OPEN_ROW == 1
#define VIA_SCH_TAG 0XCC
#endif

typedef QSIMPLEQ_HEAD(_cmpl_pend_que, ncl_cmd_t) cmpl_pend_que;		///< pending queue for complete ncl command

typedef struct _die_que_t {
	bm_pl_t *bm_pl;					///< bm payload
	pda_t *pda;					///< pda
	die_ent_info_t *info;				///< info

	u16 rptr;					///< die queue read pointer
	u16 wptr;					///< die queue write pointer
	u16 cmpl_ptr;					///< die queue complete pointer
	u16 die_id;					///< die id

	//u16 busy_cnt;					///< number of busy count

	u32 issue_sn;					///< read command issue sn
	u32 cmpl_sn;					///< expected sn of read done command
	cmpl_pend_que pend_que;				///< pend que of completed read cmd when cmpl out of order

} die_que_t;

typedef struct _rsv_die_que_t {
	bm_pl_t bm_pl[RSV_DIE_QUE_SZ];			///< bm payload
	pda_t pda[RSV_DIE_QUE_SZ];				///< pda
	die_ent_info_t info[RSV_DIE_QUE_SZ];		///< info
	u16 die_id[RSV_DIE_QUE_SZ];

	u16 rptr;					///< die queue read pointer
	u16 wptr;					///< die queue write pointer

	u16 busy_cnt;					///< number of busy count
} rsv_die_que_t;

typedef struct _die_cmd_info_t {
	die_que_t *die_queue;				///< die queue pointer
	die_ent_info_t cmd_info;			///< command info
	u32 sn;
} die_cmd_info_t;

typedef union _wr_era_op_t {
	struct {
		u32 slc       : 1;	///< slc command
		u32 host      : 1;	///< host command
		u32 erase     : 1;	///< erase command
		u32 xor       : 1;	///< true if raid xor operation enable
		u32 pout      : 1;	///< true if raid parity out operation enable
		u32 parity_mix  : 1;	///< true if do parity out before write to nand
		u32 remote    : 1;	///< true if command from other cpu
		u32 raid      : 1;	///< true if command with raid
		u32 ncl_done  : 1;	///< true if ncl  exec done
		u32 upt_done  : 1;	///< true if l2p update done
		u32 p2l_nrm   : 1;	///< true if command with p2l nromal data
		u32 p2l_pad   : 1;	///< true if command with p2l pading data
		u32 trim_info : 1;	///< trim if command with trim info operation
		u32 prog_err  : 1;	///< program happen
		u32 skip_updt : 1;	///< skip l2p update
		u32 tbl_updt  :	1;	///< table update request
		u32 dummy 	  : 1;
        u32 ftl 	  :	1;	///< ftl blcok tag
        u32 blklist   : 1;	///< ftl blcok list in HOST&GC block
        u32 erase_err : 1;	///< true if erase failed, ISU, SPBErFalHdl
        u32 plp_tag   : 2;	///< true if set FTL_PLP_TAG in meta
        u32 last_req  : 1;	///< true if last req of spb.
#ifdef SKIP_MODE
		u32 one_pln   : 1;	///< true if command with one pln
		u32 pln_write : 2;	///< true if command with (1P/2P/3P) write
		u32 parity    : 1;  ///< true if this req is for parity payload
#else
		u32 pln_st    : 2;
		u32 parity	  : 1;	///< true if this req is for parity payload
#endif
	} b;
	u32 all;
} wr_era_op_t;
BUILD_BUG_ON(sizeof(wr_era_op_t) != 4);

struct _ncl_w_req_t;
typedef void (*ncl_req_cmpl_t) (struct _ncl_w_req_t *);

typedef struct _req_raid_id_t {
	u8 raid_id;			///< raid stripe id
	u8 bank_id;			///< raid bank id
} req_raid_id_t;

typedef struct _ncl_req_t {
	void *caller;			///< caller
	ncl_req_cmpl_t cmpl;		///< complete function
	ncl_req_cmpl_t tmp_cmpl;		///< complete function

	u8 aspb_id;			///< aspb id
	u8 die_id;			///< die id
	u8 stripe_id;			///< stripe id
	u8 pad_sidx;			///< pad meta index

	u8 id;				///< request id
	u8 cnt;				///< number of received data
	u8 cpage;			///< current operating page
	u8 tpage;			///< total page

	u8 nsid;			///< namespace id
	u8 type;			///< type
	u8 updt_cnt;			///< flying pda updt cnt
	u8 padding_cnt;		///< number of data padding
	u8 mp_du_cnt;			///< du cnt per multi plane
	u8 die_cnt;			///< die count per req

	u8 blklist_pg[3];
	u8 blklist_dtag_cnt;
    u8 pgsn_page;
	u8 p2l_page;		///< p2l page index in this program
	u8 p2l_grp_idx_nrm;	///< nromal p2l group index
	u8 p2l_grp_idx_pad;	///< pad p2l group index
	u8 p2l_dtag_cnt;
	u8 trim_page;		///< trim page index in this program
	u8 wunc_cnt;
    u8 parity_die;
    u8 bad_die_cnt;
    u8 dtag_id;
	wr_era_op_t op_type;		///< op type
	QSIMPLEQ_ENTRY(_ncl_req_t) link;///< request link
} ncl_req_t;

typedef struct _ncl_w_pl_t {
	pda_t pda[EACH_DIE_DTAG_CNT >> DU_CNT_SHIFT];	///< pda
	bm_pl_t pl[EACH_DIE_DTAG_CNT];	///< bm payload
	lda_t lda[EACH_DIE_DTAG_CNT];	///< lda list addr must be 8B aligned when l2p update bulk, so we allocate 8B aligned mem for this

#ifdef DUAL_BE
	req_raid_id_t raid_id[XLC][MAX_PLANE];
#endif

	pda_t l2p_pda[EACH_DIE_DTAG_CNT / DU_CNT_PER_PAGE];	/// pda list to update l2p
	pda_t *updt_pda;	///< L2P Engine pda update list, must be 8B aligned
	pda_t *old_pda;	///< L2P Engine pda update list, must be 8B aligned

	u32 updt_cnt;			///< flying pda updt cnt

    #ifdef LJ_Meta          // Added by Sunny for fill meta
    u32 spb_sn;
    #else
	u32 meta_sn;			///< sn in meta
    #endif

	struct {
		dtag_t dtag;		///< dtag for trim info, temp used to store gc dtag_idx
	} trim;
} ncl_w_pl_t;

typedef struct _ncl_e_pl_t {
	pda_t pda[EACH_DIE_ERASE_PDA_CNT];	///< pda
} ncl_e_pl_t;

typedef struct _ncl_w_req_t {
	ncl_req_t req;
	ncl_w_pl_t w;
} ncl_w_req_t;

typedef struct _ncl_e_req_t {
	ncl_req_t req;
	ncl_e_pl_t e;
} ncl_e_req_t;

typedef struct _active_plane_buffer_t {
	u32 pl;					///< plane
	pda_t pda;				///< pda
} active_plane_buffer_t;

typedef struct _ppu_list_t {
	u32 LDA;			    ///< LDA
	bool valid;				///< flag
} ppu_list_t;

extern ncl_w_req_t ncl_w_reqs[RDISK_NCL_W_REQ_CNT];		///< ncl request
extern pool_t ncl_w_reqs_pool;				///< ncl request pool
extern u32 ncl_w_reqs_free_cnt;				///< ncl free counter
extern ncl_e_req_t ncl_e_reqs[RDISK_NCL_E_REQ_CNT];	///< erase ncl request
extern pool_t ncl_e_reqs_pool;				///< erase ncl request pool
extern u32 ncl_e_reqs_free_cnt;				///< erase ncl free counter
extern u16 otf_e_reqs;					///< numbers of on the fly erase request
extern u32 rd_cnt_threshold_close;
extern u32 rd_cnt_threshold_open;
extern volatile u8 otf_forcepbt;

/*!
 * @brief die manager resume from pmu
 *
 * @param gid		groud id
 *
 * @return		not used
 */
void die_mgr_resume(u32 gid);

/*!
 * @brief initialize die manager
 *
 * @param nr_die	number of die managed
 * @param gid		group id
 *
 * @return		not used
 */

/*!
* @brief check if any outstanding stream read
*
* @param die_id		die id
*
* @return			return true for idle
*/
bool die_mgr_chk_stream_rd_idle(u32 die_id);

/*!
 */
void die_mgr_init(int nr_die, u32 gid);

/*!
 * @brief filer die queue to cancel entries according target info
 *
 * @param die_id	die id
 * @param target_info	target info to be filter
 *
 * @return		how many entries were cancelled
 */
u32 die_que_cancel(u32 die_id, die_ent_info_t target_info);

/*!
 * @brief insert pda read to die queue
 *
 * @param bm_pl		bm payload to be used
 * @param pda		pda to be read
 * @param info		entry information
 * @param die_id	die id
 * @param imt		trigger immediately
 *
 * @return		true if insert successfully
 */
bool die_que_rd_ins(bm_pl_t *bm_pl, pda_t pda, die_ent_info_t info, u8 die_id, u8 q_type,bool imt);

#if defined(TCG_NAND_BACKUP)
ddr_code u32 die_que_mbr_rd_remain_chk(u32 die_id);
#endif

/*!
 * @brief check if die read queue available or not
 *
 * @param die_id	die id
 *
 * @return		true if available
 */
bool die_que_read_avail(u8 die_id);

/*!
 * @brief insert write/erase request to die queue
 *
 * @param ncl_req	ncl request to be used
 * @param die_id	die id
 *
 * @return		true if insert successfully
 */
bool die_que_wr_era_ins(ncl_req_t *ncl_req, u8 die_id);

/*!
 * @brief cancel data entries = target info in rsvd queue
 *
 * @param target_info	target info to be cancelled
 *
 * @return		not used
 */
void rsv_die_que_cancel(die_ent_info_t target_info);

/*!
 * @brief insert pda read to reserved die queue
 *
 * @param bm_pl		bm payload to be used
 * @param pda		pda to be read
 * @param info		entry information
 * @param die_id	die id
 *
 * @return		true if insert successfully
 */
bool rsv_die_que_rd_ins(bm_pl_t *bm_pl, pda_t pda, die_ent_info_t info, u8 die_id);

/*!
 * @brief die event handler to issue pending command
 *
 * @param param0	not used
 * @param param1	not used
 * @param param1	not used
 *
 * @return		not used
 */
void die_isu_handler(u32 param0, u32 param1, u32 param2);

void ncl_cmd_submit_insert_schedule(struct ncl_cmd_t *ncl_cmd, bool direct_handle);


/*!
 * @brief get handled cpu of target die id
 *
 * @param die		die id
 *
 * @return		cpu id
 */
static inline u32 get_target_cpu(u32 die)
{
#ifndef DUAL_BE
	return CPU_BE;
#else
	u32 ch = die & (nand_channel_num() - 1);  // if channel != 1, don't need to check ch == 0
	if (ch < (nand_channel_num() >> 1))  //if (ch == 0 || ch < (nand_channel_num() >> 1))
		return CPU_BE;
	else
		return CPU_BE_LITE;
#endif
}

static inline u32 get_ncl_w_cnt()
{
    return ncl_w_reqs_free_cnt;
}

static inline u32 get_ncl_e_req_cnt(void)
{
    return ncl_e_reqs_free_cnt;
}

/*!
 * @brief get ncl request
 *
 * @param nsid		namespace id
 *
 * @return		ncl request
 */
static inline ncl_w_req_t *get_ncl_w_req(u8 nsid, u8 type, bool force)
{
	ncl_w_req_t *req = NULL;
	u32 rsvd = 0;

	//if (nsid != INT_NS_ID) {
        if (force){
            rsvd = 2;
        }
        else if (nsid == HOST_NS_ID){
			if(otf_forcepbt && (type == FTL_CORE_GC)){
				rsvd = 8+6+(ncl_w_reqs_free_cnt*2/3);
			}else{
            	rsvd = 8+6;
			}
        }else{
        	rsvd = 8;
        }
	//}

	if (ncl_w_reqs_free_cnt > rsvd) {
		req = pool_get_ex(&ncl_w_reqs_pool);
		sys_assert(req);
		ncl_w_reqs_free_cnt--;
	}

	return req;
}


/*!
 * @brief get ncl erase request of target die id
 *
 * @param die		die id
 *
 * @return		ncl request
 */
static inline ncl_e_req_t *get_ncl_e_req(void)
{
	ncl_e_req_t *req = NULL;

	if (ncl_e_reqs_free_cnt) {
		req = pool_get(&ncl_e_reqs_pool, 0);
		sys_assert(req);
		ncl_e_reqs_free_cnt--;
	}

	return req;
}

/*!
 * @brief put ncl request back to the pool
 *
 * @param req		ncl request to be put
 *
 * @return		not used
 */
static inline void put_ncl_w_req(ncl_w_req_t *req)
{
	sys_assert(req->req.id < RDISK_NCL_W_REQ_CNT);

	pool_put_ex(&ncl_w_reqs_pool, req);
	ncl_w_reqs_free_cnt++;
}

/*!
 * @brief put ncl erase request back to the pool
 *
 * @param req		ncl request to be put
 *
 * @return		not used
 */
static inline void put_ncl_e_req(ncl_e_req_t *req)
{
	pool_put(&ncl_e_reqs_pool, req);
	ncl_e_reqs_free_cnt++;
}

/*!
 * @brief get ncl requst by rid
 *
 * @param rid		request id
 *
 * @return		ncl request
 */
static inline ncl_w_req_t *ncl_rid_to_req(u32 rid)
{
	return &ncl_w_reqs[rid];
}

/*!
 * @brief translate physical die information to fw die id
 *
 * @param ch		channel
 * @param ce		target
 * @param lun		lun
 *
 * @return		fw die id
 */
static inline u32 to_die_id(u32 ch, u32 ce, u32 lun)
{
	u32 ret;

	ret = lun;
	ret *= nand_target_num();
	ret += ce;
	ret *= nand_channel_num();
	ret += ch;

	return ret;
}

/*!
 * @brief to reset a read counter in scheduler, usually called because re-open
 *
 * @param spb_id	spb id to be reset
 *
 * @return		not used
 */
void read_cnt_reset(spb_id_t spb_id);

/*!
 * @brief issue read counter to ftl
 *
 * @param spb_id	read count of this spb will be issued
 *
 * @return		not used
 */
void read_cnt_issue(spb_id_t spb_id);

/*!
 * @brief update read counter of die per spb
 *
 * @param spb_id	read count of this spb will be increased by 0xFF
 * @param die_id	read count of this die will be increased by 0xFF
 *
 * @return		not used
 */
void read_cnt_increase(spb_id_t spb_id, u32 die_id);

/*!
 * @brief increase read counter of spb id
 *
 * @param spb_id	read count of this spb will be increased by one
 *
 * @return		not used
 */
static inline void rd_cnt_inc(spb_id_t spb_id, u32 die_id, u32 count)
{

//	_rd_cnt_sts[spb_id].b.counter++;
    u32 spb_num_per_dtag = DTAG_SZE / sizeof(u8) / shr_nand_info.lun_num;
    u16 rd_cnt_grp, num_of_rd_cnt_grp;
	u8 pre_rd_cnt, add_rd_cnt;
    sys_assert(spb_id < MAX_SPB_COUNT);
    sys_assert(die_id < shr_nand_info.lun_num);

    rd_cnt_runtime_counter = (runtime_rd_cnt_inc_t *)shr_rd_cnt_counter_addr;

//    schl_apl_trace(LOG_ERR, 0, "spb_id: %d, die_id: %d\n", spb_id, die_id);
    //rd_cnt_grp = spb_id / 8;
    //num_of_rd_cnt_grp = (spb_id % 8) * 128 + die_id;
    rd_cnt_grp = spb_id / spb_num_per_dtag;
	num_of_rd_cnt_grp = (spb_id % spb_num_per_dtag) * shr_nand_info.lun_num + die_id;

    rd_cnt_runtime_counter += rd_cnt_grp;
//    schl_apl_trace(LOG_ERR, 0, "rd_cnt_tbl_ptr: 0x%x\n", rd_cnt_runtime_table);
    //rd_cnt_runtime_table->runtime_rd_cnt_idx[num_of_rd_cnt_grp]++;
    //rd_cnt_runtime_counter->runtime_rd_cnt_idx[num_of_rd_cnt_grp] += count;
	pre_rd_cnt = rd_cnt_runtime_counter->runtime_rd_cnt_idx[num_of_rd_cnt_grp];
	add_rd_cnt = pre_rd_cnt + count;
    rd_cnt_runtime_counter->runtime_rd_cnt_idx[num_of_rd_cnt_grp] = add_rd_cnt;
//    schl_apl_trace(LOG_ERR, 0, "&rd_cnt_tbl_ptr->runtime_rd_cnt_idx[%d]: 0x%x\n",num_of_rd_cnt_grp, &rd_cnt_tbl_ptr->runtime_rd_cnt_idx[num_of_rd_cnt_grp]);
//    schl_apl_trace(LOG_ERR, 0, "rd_cnt_tbl_ptr->runtime_rd_cnt_idx[%d]: %d\n", num_of_rd_cnt_grp, rd_cnt_runtime_table->runtime_rd_cnt_idx[num_of_rd_cnt_grp]);
//    schl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table[%d].runtime_rd_cnt_idx[%d]: %d\n", rd_cnt_grp, num_of_rd_cnt_grp, rd_cnt_runtime_table[rd_cnt_grp].runtime_rd_cnt_idx[num_of_rd_cnt_grp]);

//    if(rd_cnt_tbl_ptr->runtime_rd_cnt_idx[num_of_rd_cnt_grp] > _rd_cnt_sts[spb_id].b.counter)
	if(pre_rd_cnt > add_rd_cnt)
		read_cnt_increase(spb_id, die_id);
}

/*!
 * @brief sete spb retired
 *
 * @param spb_id	id of spb to be retired
 * @param type		type of retirement
 *
 * @return		not used
 */
#ifndef ERRHANDLE_GLIST // Handle by ErrHandle_Task, Paul_20201202
void set_spb_weak_retire(spb_id_t spb_id, u32 type);
#endif

/*!
 * @brief find if and read error in the ncl command
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		true if any error
 */
bool is_there_uc_pda(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief prepare recovery request from the ncl command
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		recovery request
 */
rc_req_t* rc_req_prepare(struct ncl_cmd_t *ncl_cmd, bool flag_put_ncl_cmd);

void read_done_rc_entry(struct ncl_cmd_t *ncl_cmd);
void decrease_otf_cnt (u8 die_id);
void AplDieQueue_DumpInfo(u32 flag);
void dfu_blank_err_chk(struct ncl_cmd_t *ncl_cmd, die_ent_info_t ent_info);

/*! @} */


void ncl_cmd_put(struct ncl_cmd_t *ncl_cmd, bool flag_put_ncl_cmd);
#undef __FILEID__
