
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

//=============================================================================
//
/*! \file ftl_export.h
 * @brief export ftl interface
 *
 * \addtogroup ftl_export
 * @{
 *
 * define ftl interface for upper module
 */
#pragma once

#include "ftl_error.h"
#include "ftltype.h"
#include "queue.h"
#include "bf_mgr.h"
#include "stat_head.h"
#include "fc_export.h"
#include "mpc.h"

#if defined(USE_TSB_NAND)
	#define VENDOR_FUNC(x)	toshiba_##x
#elif defined(USE_MU_NAND)
	#define VENDOR_FUNC(x)	micron_##x
#elif defined(USE_SNDK_NAND)
	#define VENDOR_FUNC(x)	sndk_##x
#elif defined(USE_HYNX_NAND)
	#define VENDOR_FUNC(x)	hynx_##x
#elif defined(USE_UNIC_NAND)
	#define VENDOR_FUNC(x)	unic_##x
#elif defined(USE_YMTC_NAND)
	#define VENDOR_FUNC(x)	ymtc_##x
#elif defined(USE_SS_NAND)
	#define VENDOR_FUNC(x)	ss_##x
#elif !defined(RAMDISK)
	#error "Not Support NAND"
#endif

#define CROSS_TEMP_OP 1

#define MAX_INTERLEAVE_CNT_IN_FTL		256					///< max interleave count supported by FTL
#define MAX_INTERLEAVE_DW_IN_FTL		(MAX_INTERLEAVE_CNT_IN_FTL / 32)	///< in dword

#define tlc_get_shared_page_cnt VENDOR_FUNC(tlc_get_shared_page_cnt)	///< not used now

#define FTL_STAT_VER		0x2		///< ftl statistics version
#define FTL_STAT_SIG		0x46737461	///< 'Fsta'

#define FE_NS_INFO_MAX_SZ_PER_NS	(256)		///< FE namespace information maximum size
#define FE_NS_MAX_CNT			(32)		///< FE namespace max count

#define BG_TASK_NEW	1	///< exec bg task from start
#define BG_TASK_CONT	2	///< continue to exec current bg task

/*!
 * @brief define OP percentage
 */
typedef enum {
		OP_96875 = 31,  //!< OP_96875, 96.875%
		OP_9375 = 30,   //!< OP_9375
		OP_90625 = 29,  //!< OP_90625
		OP_875 = 28,    //!< OP_875
		OP_84375 = 27,  //!< OP_84375
		OP_8125 = 26,   //!< OP_8125
		OP_78125 = 25,  //!< OP_78125

		OP_5000 = 16,   //!< OP_5000

		OP_DIVIDER = 32,//!< OP_DIVIDER
} op_per_t;

/*! @brief detail of FTL setting */
typedef struct _ftl_setting_t {
	unsigned int version;
	unsigned int op;		///< user over provision, op/100 = 93.75%, if op = 9375
	unsigned int tbl_op;		///< table over provision,
	unsigned short tbw;		///< tbw for 128G disk size
	unsigned short burst_wr_mb;	///< burst write size in mb
	unsigned short slc_ep;		///< slc erase-program cycle
	unsigned short native_ep;	///< native erase-prorgram cycle
	unsigned short user_spare;	///< user spare ratio in thousands
	char read_only_spare;		///< user mini spare, if user space is under this op, enter read-only mode, unit: percentage
	char wa;			///< estimated WA
	unsigned int nat_rd_ret_thr;	///< native read retention threshold
	unsigned int slc_rd_ret_thr;	///< slc read retention threshold
	unsigned int max_wio_size;	///< set according to nand geo
	char gc_retention_chk;		///< retention gc period for WL and WEAK
	char alloc_retention_chk;	///< retention gc period when allocation
	char avail_spare_thr;		///< smart available spare threshold
} ftl_setting_t;

/*! @brief nand error statistics */
typedef struct _ftl_stat_t {
	stat_head_t head;

	u32 total_ecc_err_cnt;	///< ecc error
	u32 total_era_err_cnt;	///< erase error
	u32 total_pro_err_cnt;	///< program error
	u32 total_spor_cnt;	///< ungraceful shutdown count
	u64 total_write_sector_cnt; ///< host write sector count, one sector = 512Byte
	u32 total_factory_defect_blk_cnt; ///< factory defect blk
	u32 total_read_retry_cnt;         ///< read retry cnt what happened
	u32 total_phy_error_cnt;
	u32 total_dma_error_cnt;

	struct {
		u32 wp : 1;	///< 0 disable write protection, 1 enable write protect
		u32 rsvd : 31;
	} b;

	// please add new field by reduce rsvd count
	// and don't modify fields which are already defined
	u32 rsvd[19];
} ftl_stat_t;
BUILD_BUG_ON(sizeof(ftl_stat_t) != 128);

/*! @brief full spb info entry for VSC command, not used yet */
typedef struct _vsc_spb_info{
	u32 erase_cnt :24;	///< erase count
	u32 spb_type :4;	///< spb type
	u32 pool_id :4;		///< pool id
	u32 defect_bmp[2];	///< defect bitmap, need to be increased
} vsc_spb_info_t;

/*! @brief read counter status in die manager */
typedef union _rd_cnt_sts_t {
	u32 all;
	struct {
		u32 counter : 29;			///< read counter
		u32 weak : 1;				///< weak flag, set 1 mean already set weak
		u32 retired : 1;			///< retired flag, set 1 mean already retired
		u32 api:1;
	} b;
} read_cnt_sts_t;

runtime_rd_cnt_idx_t *rd_cnt_runtime_table;
runtime_rd_cnt_inc_t *rd_cnt_runtime_counter;
/*! @brief flush context to flush a namespace*/
typedef struct _flush_ctx_t {
	struct list_head entry;
	u32 nsid;			///< namespace id
	union {
		struct {
			u32 shutdown : 1;	///< if flush from shutdown command
			u32 format : 1;		///< called by format command
			u32 spb_close : 1;	///< if flush from spb close event
			u32 ns_delete : 1;
		} b;
		u32 all;
	} flags;
	void *caller;			///< caller context
	void (*cmpl)(struct _flush_ctx_t *);	///< completion callback of flush
} flush_ctx_t;

/*! @brief header of ftl_flush from remote cpu */
typedef struct _flush_ctx_ipc_hdl_t {
	flush_ctx_t ctx;		///< local flush context, will copy remote context to local
	u8 tx;				///< command tx
} flush_ctx_ipc_hdl_t;

/*! @brief physical block, use spb_id and interleave id to indicate it */
typedef union _pblk_t {
	struct {
		u16 spb_id;		///< spb id
		u16 iid;		///< interleave id
	};
	u32 pblk;			///< overall
} pblk_t;

typedef struct _format_ctx_t {
	union {
		u32 all;
		struct {
			u32 ns_id : 3;
			u32 host_meta : 1;
			u32 ins_format : 1;
			u32 preformat_erase : 1;
			u32 full_trim : 1;
			u32 del_ns : 1;
		} b;
	} ;
	void *caller;
	void (*cmpl)(struct _format_ctx_t *);
} format_ctx_t;

enum {
	FE_REQ_TYPE_LOG = 0,
	FE_REQ_TYPE_NS = 1,
};

/*!
 * @brief request from frontend for fe log
 */
typedef struct _fe_req_t {
	bool write;		///< if write to fe log, or read
	u8 type;		///< enum FE_REQ_TYPE
	u16 size;		///< total size in bytes

	void *mem;		///< should be read, must be dtag aligned memory
	void (*cmpl)(struct _fe_req_t *);	///< completion callback
	void *caller;
} fe_req_t;

extern ftl_setting_t ftl_setting;
extern ftl_stat_t ftl_stat;
extern volatile ns_t ns[INT_NS_ID];
extern volatile u16 ps_open[3];
extern volatile u8  QBT_BLK_CNT;
extern volatile u8  ftl_pbt_cnt;
//extern volatile u8  ftl_pbt_idx;

typedef struct _ec_ipc_hdl_t {
	u32 *avg_erase;
	u32 *max_erase;
	u32 *min_erase;
	u32 *total_ec;
} ec_ipc_hdl_t;

/*!
 * @brief Interface to get user logical space capacity
 *
 * external module should try to enumerate all possible LS, because we may
 * have two Logical spaces in the future.
 *
 * @param ls_id		query capacity for this LS
 *
 * @return		return capacity of ls_id, 0 for invalid using
 */
u32 ftl_get_capacity(u32 ls_id);

/*!
 * @brief Get how many SPB in flash
 *
 * @return	spb count in flash
 */
u32 ftl_get_spb_total_cnt(void);

/*!
 * @brief  get full spb info for VSC command
 *
 * @param spb_id	spb id
 * @param spb_info  spb info entry
 *
 * @return		none
 */
void ftl_get_full_spb_info(u32 spb_id, vsc_spb_info_t *spb_info);

/*!
 * @brief  get SPB busy or not
 *
 * @param spb_id	spb id
 *
 * @return		true or false
 */
bool ftl_is_spb_busy(u32 spb_id);

/*!
 * @brief get ftl statistics information from nand
 *
 * @param 		ftl statistics information from nand
 *
 * @return		return false for default using
 */
static inline bool ftl_stat_restore(ftl_stat_t *stat)
{
	u32 new_fsize = sizeof(*stat) - sizeof(stat->rsvd);

	if (stat == NULL)
		goto def;

	if (stat_match(&stat->head, FTL_STAT_VER, new_fsize, FTL_STAT_SIG)) {
		ftl_stat = *stat;
		return true;
	} else {
def:
		memset(&ftl_stat, 0, sizeof(ftl_stat_t));
		stat_init(&ftl_stat.head, FTL_STAT_VER, new_fsize, FTL_STAT_SIG);
		return false;
	}
}

/*!
 * @brief update  current ftl statistics information to buffer
 *
 * @param 		global ftl statistics information
 *
 * @return		not used
 */
static inline void ftl_stat_update(ftl_stat_t *stat)
{
	u32 fsize = sizeof(*stat) - sizeof(stat->rsvd);

	stat_init(&stat->head, FTL_STAT_VER, fsize, FTL_STAT_SIG);
	*stat = ftl_stat;
	memset(stat->rsvd, 0, sizeof(stat->rsvd));
}

/*!
 * @brief Get current FTL statistics information
 *
 * @param buf		copy current statistic to this buffer
 *
 * @return 		not used
 */
void ftl_stat_get(ftl_stat_t *buf);

/*!
 * @brief get current spare block count
 *
 * @param avail		current spare block count
 * @param spare		initial spare block count from frb
 * @param thr		spare block threshold
 *
 * @return		none
 */
void get_spare_cnt(u32 *avail, u32 *spare, u32 *thr);

/*!
 * @brief get block average erase count for native pool
 *
 * @param avg_erase	average erase count
 * @param max_erase	spec-defined max erase count
 *
 * @return		none
 */
void get_avg_erase_cnt(u32 *avg_erase, u32 *max_erase, u32 *min_erase, u32 *total_ec);

/*!
 * @brief query spb for a specific namespace
 *
 * @param nsid		namespace id
 * @param type		should FTL_CORE_XXXX
 *
 * @return		none
 */
void ftl_set_spb_query(u32 nsid, u32 type);

/*!
 * @brief set spb GCed, received from other CPU to indicate this SPB was well-GCed
 *
 * @param spb_id	spb id
 * @param free_du_cnt	total released du count of the spb
 *
 * @return		not used
 */
void ftl_gc_done(u32 spb_id, u32 free_du_cnt);

/*!
 * @brief flush API to flush everything to clean
 *
 * @param ctx		flush context
 *
 * @return		not used
 */
void ftl_flush(flush_ctx_t *ctx);

/*!
 * @brief format API to format ftl ns
 *
 * @param ctx		format context
 *
 * @return		not used
 */
void ftl_format(format_ctx_t *ctx);

/*!
 * @brief flush namespace descriptor to make namespace dirty to write data
 *
 * @param nsid		namespace id
 *
 * @return		return true if already open, return false if it was not opened
 */
bool ftl_open(u32 nsid);


/*!
 * @brief get a physical block from pool
 *
 * @return		return allocated physical block, it may be INV_SPB_ID
 */
void blk_pool_get_pblk(pblk_t *pblk);

/*!
 * @brief put a physical block into pool, it will be erase soon
 *
 * @return		not used
 */
void blk_pool_put_pblk(pblk_t pblk);

/*!
 * @brief put a physical block into pool, it will be erase soon
 *
 * @param action		gc action(suspend/resume)
 *
 * @return		true if gc action done
 */
bool gc_action(gc_action_t *action);
#if(PLP_GC_SUSPEND == mDISABLE)
bool gc_action2(gc_action_t *action);//joe add for del ns 20111119
#endif

/*!
 * @brief flush data from frontend
 *
 * @param req		flush request
 *
 * @return		not used
 */
void fe_log_flush(fe_req_t *req);

/*!
 * @brief read data from fe log latest page
 *
 * @param req		read request
 *
 * @return		not used
 */
void fe_log_read(fe_req_t *req);

/*!
 * @brief load fe namespace inforamtion from FTL
 *
 * @param req		load request
 *
 * @return		not used
 */
void fe_ns_info_load(fe_req_t *req);

/*!
 * @brief save fe namespace inforation to FTL
 *
 * @param req		save request
 *
 * @return		not used
 */
void fe_ns_info_save(fe_req_t *req);

/*!
 * @brief thermal setting was updated, then ftl need to response it
 *
 * @return		not used
 */
void ftl_thermal_update(void);

/*!
 * @brief notify ftl pcie was reset, start to stop gc
 *
 * @return 	not used
 */
void ftl_reset_notify(void);

/*!
 * @brief notify ftl core pcie was reset, start resume gc
 *
 * @return 	not used
 */
void ftl_reset_done_notify(void);

/*! @} */
