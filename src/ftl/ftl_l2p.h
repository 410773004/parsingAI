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
/*! \file ftl_l2p.h
 * @brief define ftl l2p manager, define l2p memory usage and l2p engine operations
 *
 * \addtogroup ftl
 * \defgroup ftl_l2p
 * \ingroup ftl
 * @{
 *
 */
#pragma once

#define L2PP_SIZE	DTAG_SZE
#define ENT_IN_L2PP	(L2PP_SIZE / sizeof(pda_t))
#define ENT_IN_L2P_SEG	(seg_size / sizeof(pda_t))
#define NR_L2PP_IN_SEG	(ENT_IN_L2P_SEG / ENT_IN_L2PP)

#define PBT_OP					(mENABLE)
#define PBT_DEBUG				(mDISABLE)//(mDISABLE)
#define PBT_DUMP_PG_CNT   		(3)
#if (PLP_SUPPORT == 0)//non-plp
#define PBT_RATIO				(16)	// HOST:PBT = PBT_RATIO:1
#else
#define PBT_RATIO				(20)	// HOST:PBT = PBT_RATIO:1
#endif
#define QBT_TLC_MODE           (mENABLE)
#define QBT_TLC_MODE_DBG       (mENABLE)
#define QBT_SKIP_MODE_2SEG     (2) // QBT skip mode 2 seg
#define PARITY_FLUSH_PBT_TIC 	(48768)
#define RELEASE_LOG_CTL 		(mENABLE)


typedef struct _l2p_ele_t {
	u64 lut;
	u32 seg_off;
	u32 seg_end;
} l2p_ele_t;

typedef struct _l2p_bg_load_req_t {
	struct list_head entry;
	u32 seg_off;
	u32 seg_end;
	u32 seg_cnt;
	u32 ready_cnt;
} l2p_bg_load_req_t;

typedef struct _l2p_load_mgr_t {
	u32 *ready_bmp;		///< l2p seg ready bitmap
	u32 *loading_bmp;	///< l2p seg loading bitmap

	u32 seg_off;	///< load l2p seg off
	u32 seg_end;	///< load l2p seg end
	u32 seg_idx;	///< load seg search index
	u32 seg_cnt;	///< ttl seg to load
	u32 ttl_ready;	///< total ready seg cnt
	u32 ready_cnt;	///< ready seg cnt in this bg load

	u8 otf_load;		///< on the fly load seg cnt
	u8 evt_load;		///< event to trigger l2p seg load
	u8 rsvd[2];

	l2p_bg_load_req_t *cur_bg_req;		///< busy background load req
	struct list_head bg_waiting_list;	///< waiting background req list
	struct list_head urg_waiting_list;	///< waiting urgent req list
	struct list_head urg_loading_list;	///< loading urgent req list
} l2p_load_mgr_t;

extern u32 seg_size;
extern u32 seg_size_shf;
extern pda_t L2P_LAST_PDA;
extern l2p_load_mgr_t l2p_load_mgr;
extern volatile u8 otf_forcepbt;
/*!
 * @brief ftl l2p initial
 * calculate the space usage of valid bit map and valid count table
 *
 * @param cap 	capacity
 *
 * @return	not used
 */
void ftl_l2p_init(u32 cap);

u32 GET_BLKLIST_START_DTAGIDX(u32 type);
/*!
 * @brief ftl l2p initial
 * calculate the space usage of valid bit map and valid count table
 *
 * @param cap 	capacity
 *
 * @return	not used
 */
void ftl_l2p_para_init(void);

/*!
 * @brief get misc data size in segment
 *
 * @return	segments count of misc data
 */
u32 ftl_l2p_misc_cnt(void);

/*!
 * @brief copy valid count buffer from internal buffer and return dtag of valid count sram buffer
 *
 * @param cnt 	return dtag number
 * @param buf	pointer of pointer of valid count buffer
 *
 * @return	dtag of valid count sram buffer
 */
dtag_t ftl_l2p_get_vcnt_buf(u32 *cnt, void **buf);

/*!
 * @brief return valid count sram buffer
 *
 * @param dtag		dtag of valid count sram buffer
 * @param cnt		count of dtag
 * @param restore	true to restore buffer back to internal buffer
 *
 * @return		not used
 */
void ftl_l2p_put_vcnt_buf(dtag_t dtag, u32 cnt, bool restore);

bool ftl_l2p_misc_flush(ftl_flush_misc_t *flush_misc);

/*!
 * @brief allocate l2p memory for namespace
 *
 * @param cap		capacity of namespace
 * @param nsid		id of namespace
 * @param ele		l2p element of namespace, will be updated here
 *
 * @return		return l2p start offset in DDR
 */
u64 ftl_l2p_alloc(u32 cap, u32 nsid, l2p_ele_t *ele);

/*!
 * @brief reset a range of l2p described by l2p element
 *
 * @param ele		l2p element to be reset
 *
 * @return		not used
 */
void ftl_l2p_reset(l2p_ele_t *ele);

/*!
 * @brief reset a range of l2p described by l2p element
 *		  use for spor pbt build
 * @param
 *
 * @return		not used
 */
void ftl_l2p_reset_partial(l2p_ele_t *ele,u16 seg_cnt);

/*!
 * @brief reload a range of l2p described by l2p elemnt from nand
 *
 * @param ele		l2p element to be loaded
 *
 * @return		not used
 */
pda_t ftl_QBT_seg_lookup(u32 segid, spb_id_t spb_id, u8 page_in_wl);


#if (SPOR_FLOW == mENABLE)
void ftl_qbt_pbt_reload(l2p_ele_t *ele, u8 pool_id, u8 load_l2p_only);
#else
void ftl_qbt_pbt_reload(l2p_ele_t *ele);
#endif

/*!
 * @brief reload misc data from nand
 *
 * return		not used
 */
/*
#if (SPOR_FLOW == mENABLE)
void ftl_misc_reload(pda_t pda_start);
#else
void ftl_misc_reload(void);
#endif
*/
/*!
 * @brief flush l2p of namespace indicated in flush_tbl
 *
 * @param flush_tbl	flush table object
 *
 * @return		not used
 */
void ftl_l2p_flush(ftl_flush_tbl_t *flush_tbl);

/*!
 * @brief debug function to calcualte CRC
 *
 * @return		crc value
 */
u32 ftl_l2p_crc(void);

/*!
 * @brief debug function to calcualte CRC
 *
 * @param dtag		misc buffer was allocated outside if dtag is not NULL
 *
 * @return		crc value
 */
u32 ftl_l2p_misc_crc(dtag_t *dtag);

/*!
 * @brief set flush all flag, next table flush will try to flush all
 *
 * @return	not used
 */
void ftl_l2p_tbl_flush_all_notify(void);

/*!
 * @brief start l2p background load task when pmu resume
 *
 * @return	not used
 */
void ftl_l2p_resume(void);

/*!
 * @brief start l2p background load tash when ns clean boot
 *
 * @param seg_off		l2p seg id to start load
 * @param seg_end	l2p seg id to end load
 *
 * @return	not used
 */
void ftl_l2p_bg_load(u32 seg_off, u32 seg_end);

/*!
 * @brief log l2p segment from remote cpu
 *
 * @param seg_id	l2p segment id
 * @param tx	request cpu
 *
 * @return	not used
 */
void ftl_l2p_urgent_load(u32 seg_id, u32 tx);

void ftl_l2p_partial_reset(u32 sec_idx);
void ftl_l2p_gc_suspend(void);
void gc_re(void);
void ftl_warmboot_reload(l2p_ele_t *ele);

rc_req_t* l2p_rc_req_prepare(struct ncl_cmd_t *ncl_cmd);

bool ftl_uc_pda_chk(struct ncl_cmd_t *ncl_cmd);

#if CO_SUPPORT_DEVICE_SELF_TEST
pda_t DST_L2P_search(lda_t LDA);
#endif

#ifdef SKIP_MODE
u8* ftl_get_spb_defect(u32 spb_id);
u8 ftl_get_defect_pl_pair(u8* ftl_df, u32 interleave);
u8 ftl_defect_check(u8* ftl_df, u32 interleave);
#endif

/*! @} */
