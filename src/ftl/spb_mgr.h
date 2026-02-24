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
/*! \file spb_mgr.h
 * @brief define spb manager, allocation method
 *
 * \addtogroup spb_mgr
 *
 * @{
 */
#pragma once
#include "fsm.h"
#include "spb_info.h"
#include "spb_pool.h"
#include "erase.h"
#include "ftl.h"
#include "system_log.h"

#define DSLC_SWITCH_EC_TH 300

#define SPB_DESC_F_BUSY		        0x01	///< this SPB is under programming
#define SPB_DESC_F_CLOSED	        0x02	///< this SPB is not longer for program in this cycle
#define SPB_DESC_F_GCED		        0x04	///< this SPB is GCed
#define SPB_DESC_F_GC_SPB	        0x08	///< this SPB is allocated for GC write
#define SPB_DESC_F_DSLC		        0x10	///< this SPB is dynamic SLC
#define SPB_DESC_F_WEAK		        0x20	///< this SPB is weak SPB
#define SPB_DESC_F_RETIRED	        0x40	///< this SPB is retired
#define SPB_DESC_F_OPEN		        0x80	///< this SPB is not well programmed
#define SPB_DESC_F_RD_DIST          0x100   ///< this SPB is occurred read disturb
#define SPB_DESC_F_NO_NEED_CLOSE    0x200   ///< this SPB is no need close blk 
#define SPB_DESC_F_PATOAL_RD        0x400   ///< this SPB is no need close blk
#define SPB_DESC_F_PLP_LAST_P2L     0x800	///< this SPB has plp force p2l
#define SPB_DESC_F_WARMBOOT_OPEN    0x1000	///< this SPB is warmboot open blk
#define SPB_DESC_F_OVER_TEMP_GC     0x2000	///< this SPB is over temp SPB pushed to GC


#define SPB_SW_F_ALLOCATING			0x01	///< indicate spb is under allocating
#define SPB_SW_F_PADDING			0x02	///< indicate spb is under padding
#define SPB_SW_F_FREEZE				0x04	///< indicate spb is frozen, can't be gced or released
#define SPB_SW_F_VC_ZERO			0x08
#define SPB_SW_F_PBT_ALLOC			0x10
#define SPB_SW_F_SHUTTLE_FREE		0x20


#define SPB_BLKLIST			(mENABLE)//(mDISABLE)
#define SPB_BLKLIST_LOG		(mENABLE)
typedef struct _qbt_grp_t {
	u16  head_qbt_idx;
	u16  tail_qbt_idx;
	u8   grp_cnt_in_alloc;
} qbt_grp_t;

typedef struct _spb_desc_t {
	u32 sn;
	u32 rd_cnt;
	u16 flags;
	u8 ns_id;
    u8 rsv;
} spb_desc_t;
BUILD_BUG_ON(sizeof(spb_desc_t) != 12);

typedef struct _spb_pool_info_t {
	u16 free_cnt[SPB_POOL_MAX];	///< free count of each pool
	u16 head[SPB_POOL_MAX];
    u16 tail[SPB_POOL_MAX];
	u16 open_cnt[SPB_POOL_MAX];	///< open spb count of each pool
} spb_pool_info_t;

struct _spb_appl_t;
/*! @brief callback function type of spb applicant */
typedef bool (*spb_appl_cb_t)(struct _spb_appl_t *appl);

/*! @brief object of SPB applicant to request SPB */
typedef struct _spb_appl_t {
	spb_id_t spb_id;		///< new SPB id for applicant

	spb_appl_cb_t notify_func;	///< to notify applicant, SPB is ready
	union {
		struct {
			u32 slc : 1;		///< allocate SLC SPB
			u32 dslc : 1;		///< allocate XLC SPB used as SLC SPB
			u32 native : 1;		///< allocate XLC SPB
			u32 older : 1;		///< allocate older SPB
			u32 must : 1;		///< must allocate one SPB
			u32 critical : 1;	///< bypass GC threshold check
			u32 QBT : 1;
			u32 rsvd : 20;
			u32 allocating : 1;
			u32 abort : 1;
			u32 flushing : 1;	///< already flushing
			u32 triggered : 1;	///< allocation already triggered
			u32 ready : 1;		///< new SPB was ready
		} b;
		u32 all;
	} flags;

	u8 ns_id;			///< logical space id, owner id
	u8 type;
	u8 pool_id;			///< when ready flag was set, pool id indicate where ready SPB was allocated from

	//u8 appl_id;
	QSIMPLEQ_ENTRY(_spb_appl_t) link;	///< simple queue link
} spb_appl_t;

typedef enum {
	SPB_GC_CAN_POLICY_MIN_VC = 0,
	SPB_GC_CAN_POLICY_WEAK = 1,
	SPB_GC_CAN_POLICY_WL = 2,
	//SPB_GC_CAN_POLICY_OPEN = 3,
	SPB_GC_CAN_SHOW = 3,
	SPB_GC_CAN_POLICY_DR = 4,
} spb_gc_can_policy_t;

#define OFFSET_BLKLISTTBL           (0)
#define SIZE_BLKLISTTBL             (sizeof(spb_info_t) * get_total_spb_cnt())
#define OFFSET_SPBDESC              (OFFSET_BLKLISTTBL + SIZE_BLKLISTTBL)
#define SIZE_SPBDESC                (sizeof(spb_desc_t) * get_total_spb_cnt())
#define OFFSET_POOL_INFO            (OFFSET_SPBDESC + SIZE_SPBDESC)
#define SIZE_POOL_INFO              (sizeof(spb_pool_info_t))
#define OFFSET_MANAGER              (OFFSET_POOL_INFO + SIZE_POOL_INFO)
#define SIZE_MANAGER                (sizeof(stFTL_MANAGER))
#define MISC_INFO					(OFFSET_MANAGER + SIZE_MANAGER)
#define SIZE_MISC_INFO				(sizeof(cache_VU_FLAG))

#define BLKINFO_SIZE_MAX            (SIZE_BLKLISTTBL + SIZE_SPBDESC + SIZE_POOL_INFO + SIZE_MANAGER + SIZE_MISC_INFO)
//#define BLK_DATA_BUFFER_CNT_IN_PAGE ((BLKINFO_SIZE_MAX+NAND_PAGE_SIZE-1) / NAND_PAGE_SIZE)
//#define BLK_DATA_BUFFER_SIZE        (BLK_DATA_BUFFER_CNT_IN_PAGE*NAND_PAGE_SIZE)


void FTL_CACL_QBTPBT_CNT(void);
#define SRB_BLK_SYSINFO_IDX         (0)
#define SRB_SYSINFO_BLOCK_CNT       (1)
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
#define CACHE_RSVD_BLOCK_CNT        (1)
#endif

#define FTL_BLK_QBT_START_IDX       (SRB_BLK_SYSINFO_IDX+SRB_SYSINFO_BLOCK_CNT)
#define FTL_QBT_BLOCK_CNT           (QBT_BLK_CNT)
#define FTL_MAX_QBT_CNT             (FTL_QBT_BLOCK_CNT*2)
#define FTL_BLK_QBT_END_IDX         (FTL_BLK_QBT_START_IDX+FTL_MAX_QBT_CNT-1)

#define FTL_PBT_BLOCK_CNT           (FTL_QBT_BLOCK_CNT)
#define FTL_MAX_PBT_CNT             (FTL_PBT_BLOCK_CNT*2)


//extern u32         QBT_BLK_CNT;
extern qbt_grp_t   QBT_GRP_HANDLE[2];
extern spb_id_t    host_spb_last_idx;
extern spb_id_t    gc_spb_last_idx;
extern spb_id_t    host_spb_pre_idx;
extern bool pbt_query_ready;
extern bool pbt_query_need_resume;
extern u8 shr_gc_read_disturb_ctrl;
extern u32 pre_shr_host_write_cnt;
extern bool rd_gc_flag;

extern bool force_empty;


typedef enum {
	SPB_SHUTDOWN = 0,
	SPB_WARMBOOT = 1,
} blk_handle_t;

typedef enum {
	SPB_QBT_FREE = 0,
	SPB_FREE = 1,
} spb_free_pool_t;
extern spb_id_t    dr_can;
#if (RD_NO_OPEN_BLK == mENABLE)
extern bool host_idle;
#endif
extern u8 qbt_done_cnt;

extern u32 rd_cnt;
extern u32 dr_cnt;

typedef struct{//32 byte
	u32 tag;
	u16 Type;
	u16 Ver;
	u32 PayLoadLen;
	u32 res;
	u32 MaxEC;
	u32 AvgEC;
	u32 MinEC;
	u32 TotalEC;
}Header_Form;

typedef struct{
	Header_Form header;
	u16 EcCnt[1958];
}Ec_Table;

Ec_Table* spb_ec_tbl;

typedef struct{
	u32 good_phy_spb;
	u32 capability_alloc_spb;
	u32 model;
}ftl_for_smart;

ftl_for_smart shr_ftl_smart;
typedef QSIMPLEQ_HEAD(_spb_appl_queue_t, _spb_appl_t) spb_appl_queue_t;

typedef struct _spb_mgr_t {
	spb_desc_t *spb_desc;		///< spb descriptor table

	u16 *sw_flags;			///< only active when runtime, it will not flush to nand

	sys_log_desc_t slog_desc;	///< system log descriptor for spb descriptor
#if(SPB_BLKLIST == mDISABLE)
	dslc_statistics_t dslc_data;	///< dynamic slc object
#endif
	spb_pool_info_t   pool_info;
	u8 evt_flush_desc;		///< event to flush descriptor
	u8 evt_spb_alloc;		///< event to allocate new spb
	u8 evt_special_erase_spb;
	spb_appl_queue_t appl_queue;	///< the queue of applicants to request SPB

	sld_flush_t sld_flush;		///< flush context of system log
	u32 *vcnt_tbl;	///< valid cnt table, dump from L2P internal sram, 32B aligned

	u32 ttl_open;			///< total open spb count
	//u32 open_cnt[SPB_POOL_MAX];	///< open spb count of each pool

	union {
		struct {
			u32 allocating : 1;	///< indicate spb allocating
			u32 sld_flush_used : 1;	///< sld_flush was issued
			u32 desc_flush_delay : 1;	///< if delay event was triggered
			u32 desc_flush_all : 1;		///< flag to flush all descriptor
		} b;
		u32 all;
	} flags;

	fsm_head_t allocate_wait_que;		///< pending state machine for allocate

    struct timer_list poh_timer;		///< poh timer
} spb_mgr_t;

/*!
 * @brief init spb manager
 * update spb manager needed parameter and mount spb allocation handler
 *
 * @return	not used
 */
void spb_mgr_init(ftl_initial_mode_t mode);

/*!
 * @brief start spb manager
 * calculate the free count of spb manager for specific pool id
 *
 * @return			not used
 */
void spb_mgr_sync_ns(void);
void spb_mgr_start(void);
mUINT_16 FTL_BlockPopHead(mUINT_16 ftlpool);
void FTL_BlockPushHead(mUINT_16 ftlpool, mUINT_16 SBlk);
void FTL_BlockPopList(mUINT_16 ftlpool, mUINT_16 SBlk);
void FTL_BlockPushList(mUINT_16 ftlpool, mUINT_16 SBlk, mUINT_16 sortRule);
void FTL_BlockPopPushList(mUINT_16 pool, mUINT_16 block, mUINT_16 sortRule);

/*!
 * @brief rebuild the free count of spb manager after vc table rebuilt in dirty boot
 *
 * @return			not used
 */
void spb_mgr_rebuild_free_cnt(void);

/*!
 * @brief check if dynamic slc is available
 *
 * @return	true if available
 */
bool is_dslc_enabled(void);

/*!
 * @brief Interface to trigger SPB allocation for give SPB applicant
 *
 * @param appl		applicant to allocate new SPB
 *
 * @return 		not used
 */
void spb_allocation_trigger(spb_appl_t *appl);

/*!
 * @brief get descriptor for given SPB ID
 *
 * @param spb_id	SPB ID
 *
 * @return		the descriptor of spb
 */
spb_desc_t* spb_get_desc(spb_id_t spb_id);

/*!
 * @brief Set SN for given SPB ID
 *
 * @param spb_id	SPB ID
 * @param sn		SN of SPB
 *
 * @return		not used
 */
void spb_set_sn(spb_id_t spb_id, sn_t sn);

/*!
 * @brief Get SN for give SPB ID
 *
 * @param spb_id	SPBID
 *
 * @return		SN of spb_id
 */
sn_t spb_get_sn(spb_id_t spb_id);

/*!
 * @brief set flag for given SPB ID
 *
 * @param spb_id	SPB ID
 * @param flag		flag of SPB
 *
 * @return		not used
 */
void spb_set_flag(spb_id_t spb_id, u32 flag);
void spb_clear_flag(spb_id_t spb_id, u32 flag);

/*!
 * @brief get flag for given SPB ID
 *
 * @param spb_id	SPB ID
 *
 * @return		flag of SPB
 */
u16 spb_get_flag(spb_id_t spb_id);

/*!
 * @brief get namespace id of spb
 *
 * @param spb_id	spb id
 *
 * @return		namespace id of spb
 */
u8 spb_get_nsid(spb_id_t spb_id);

/*!
 * @brief get pool id of spb
 *
 * @param spb_id	spb id
 *
 * @return		pool id of spb
 */
u8 spb_get_poolid(spb_id_t spb_id);

/*!
 * @brief api to check if spb was slc
 *
 * @param spb_id	spb id
 *
 * @return		return true if this spb used as SLC
 */
bool is_spb_slc(spb_id_t spb_id);

/*!
 * @brief api to check if spb was allocated for gc write
 *
 * @param spb_id	spb id to be checked
 *
 * @return		return true if this spb was allocated for GC write
 */
bool is_spb_gc_write(spb_id_t spb_id);

/*!
 * @brief to release spb
 *
 * @param spb_id	spb id to be released
 *
 * @return		not used
 */
void spb_release(spb_id_t spb_id);

void pbt_release(spb_id_t spb_id);

/*!
 * @brief api to check if spb manager was patrolling
 *
 * @param fsm		state machine pointer, can be NULL
 *
 * @return		return true if patrolling
 */
bool is_spb_mgr_patrolling(fsm_ctx_t *fsm);

/*!
 * @brief api to check if spb manager was allocating
 *
 * @param fsm		state machine pointer, can be NULL
 *
 * @return		return true if allocating
 */
bool is_spb_mgr_allocating(fsm_ctx_t *fsm);

/*!
 * @brief start spb release check (patrol)
 *
 * @return		not used
 */
void spb_mgr_spb_chk_start(void);

/*!
 * @brief continue to check spb until the last spb was checked
 *
 * @return		not used
 */
void spb_mgr_spb_chk_cont(void);

/*!
 * @brief api to flush whole spb descriptor, only can be called in shutdown flush
 *
 * @return		not used
 */
void spb_mgr_flush_desc(void);

void init_max_rd_cnt_tbl(void);
/*!
 * @brief api to get gc candidate
 *
 * @param nsid		namespace id
 * @param pool_id	pool id
 *
 * @return		spb id if there is a candidate, or INV_SPB_ID
 */
spb_id_t spb_mgr_find_gc_candidate(u32 nsid, u32 pool_id, spb_gc_can_policy_t policy);
spb_id_t find_shuttle_blk(u32 pool_id);
//spb_id_t find_shuttle_blk_cnt(u32 pool_id);
spb_id_t find_min_vc(u32 nsid, u32 pool_id);
spb_id_t find_weak(u32 pool_id);
#if (PLP_SUPPORT == 0)
spb_id_t find_non_plp_blk();
#endif
void rescan_ec(u32 nsid, u32 pool_id);

//spb_id_t find_open(u32 nsid, u32 pool_id);
spb_id_t find_dr(u32 nsid, u32 pool_id);

/*!
 * @brief pop busy spb of specific namespace
 *
 * @param nsid		namespace id
 * @param sn		return sn in this pointer
 * @param last		last pop SPB,
 * @param gc		pop busy SPB with SPB_DESC_F_GC_SPB
 * @return
 */
spb_id_t spb_mgr_pop_busy_spb(u32 nsid, sn_t *sn, spb_id_t last);

/*!
 * @brief pop busy spb of specific namespace
 *
 * @param nsid		namespace id
 * @param sn		return sn in this pointer
 * @param last		last pop SPB,
 * @param gc		pop busy SPB with SPB_DESC_F_GC_SPB
 * @return
 */
spb_id_t spb_mgr_pop_dirty_spb(u32 nsid, u32 type, sn_t sn);

/*!
 * @brief get pool free count
 *
 * @param pool		pool id
 *
 * @return 		free count
 */
u32 spb_get_free_cnt(u32 pool);

/*!
 * @brief update read counter from die queue
 *
 * @param spb_id	spb id to be updated
 * @param cnt		counter to be increased
 *
 * @return		not used
 */
void spb_mgr_rd_cnt_upd(spb_id_t spb_id);//, u32 cnt);

/*!
 * @brief set spb runtime flags
 *
 * @param spb_id	spb id to be set
 * @param flag	flag to be set
 *
 * @return		not used
 */
void spb_set_sw_flag(spb_id_t spb_id, u32 flag);

/*!
 * @brief clear spb runtime flags
 *
 * @param spb_id	spb id to be clear
 * @param flag	flag to be clear
 *
 * @return		not used
 */
void spb_clr_sw_flag(spb_id_t spb_id, u32 flag);

/*!
 * @brief get spb runtime flags
 *
 * @param spb_id	spb id to be get
 *
 * @return		spb runtime flags
 */
u16 spb_get_sw_flag(spb_id_t spb_id);

/*!
 * @brief find open spb and start gc
 *
 * @return	true if found open spb
 */
bool spb_mgr_open_spb_gc(void);

/*!
 * @brief get spb pool id which have open spb
 *
 * @return	spb pool id
 */
u32 spb_mgr_get_open_pool(void);

void spb_pool_table(void);
void spb_mgr_set_head(u32 pool_id, u32 idx);
u16 spb_mgr_get_head(u32 pool_id);
void spb_mgr_set_tail(u32 pool_id, u32 idx);
u16 spb_mgr_get_tail(u32 pool_id);

/*!
 * @brief dump function of spb manager
 *
 * @return	not used
 */
void spb_mgr_dump(void);
//u16 qbt_shutdown_handle(void);
void qbt_poweron_handle(void);
void qbt_retire_handle(u16 retire_qbt);
void blk_shutdown_handle(blk_handle_t mode);
void chk_close_blk_push(void);
sn_t find_max_sn(void);
u32 find_min_sn_spb(u32 prev_sn);
u16 spb_mgr_get_next_spb(u16 blk);

bool is_qbt_spb(spb_id_t spb_id);
void FTL_CopyFtlBlkDataFromBuffer(u32 type);
void FTL_CopyFtlBlkDataToBuffer(u32 type);
void pbt_spb_soft(spb_id_t spb_id);
void spb_poh_init(void);
void spb_clear_ec(void);
void Ec_tbl_update(u16 spb_id);
void spb_mgr_preformat_desc_init(spb_free_pool_t pool);
void spb_PurgePool2Free(u16 pool, u16 sortRule);
void FTL_SearchGCDGC(void);
void spb_blk_info(void);
void spb_blk_list(void);
void spb_ftl_mgr_info(void);
void spb_ReleaseEmptyPool(void);

void spb_set_sw_flags_zero(spb_id_t spb_id);
void spb_set_desc_flags_zero(spb_id_t spb_id);

void smart_uecc_fill_host_blk_info(u16 lblk_id, bool mark_flag);
bool smart_uecc_get_host_blk_info(u16 lblk_id);
void uecc_smart_info_init(void);
void fill_smart_uecc_info(u16 lblk, u16 err_type);
void ipc_fill_smart_uecc_info(volatile cpu_msg_req_t *req);
void spb_special_erase_trigger(u16 blk);

void spb_scan_over_temp_blk(void *data);
extern void set_spb_over_temp_flag(volatile cpu_msg_req_t *req);
/*! @} */


