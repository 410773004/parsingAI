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
/*! \file ftl_ns.h
 * @brief define ftl namespace object
 *
 * \addtogroup ftl
 * \defgroup ftl_ns
 * \ingroup ftl
 * @{
 *
 */
#pragma once
#include "mpc.h"
#include "fc_export.h"
#include "spb_pool.h"
#include "cbf.h"
#include "system_log.h"

#ifdef Dynamic_OP_En
share_data_zi volatile u32 DYOPCapacity;
#endif

#if LBA_SIZE == 512
#define calc_jesd218a_capacity(x)   \
	(((97696368+(1953504ULL*(x-50))) >> NR_LBA_PER_LDA_SHIFT))
	//(((21168 + (1953504ULL * x)) >> NR_LBA_PER_LDA_SHIFT))	///< define jesd disk capacity if lba is 512b
#elif LBA_SIZE == 4096
#define	calc_jesd218a_capacity(x)   \
	((2646 + (244188ULL * x)))					///< define jesd disk capacity if lba is 4k,
#else
#error "LBA size wrong define"
#endif
enum {
	FTL_NS_ID_START = 1,

	FTL_NS_ID_INTERNAL = INT_NS_ID,
	FTL_NS_ID_END = FTL_NS_ID_INTERNAL,
	FTL_NS_ID_MAX = FTL_NS_ID_END + 1,
};

#define SPB_QUEUE_SZ	16

typedef struct _ftl_ns_desc_t {
	sn_t spb_sn;
	u32 quota[SPB_POOL_MAX];
	union {
		struct {
			u32 clean : 1;
			u32 virgin : 1;
			u32 hmeta : 1;
			u32 du4k : 1;
		} b;
		u32 all;
	} flags;
	ftl_fence_t ffence;
	spb_id_t spb_queue[FTL_CORE_MAX][SPB_QUEUE_SZ];
	u8 frange[0];		///< ftl range bitmap
} ftl_ns_desc_t;

typedef struct _ftl_ns_pool_t {
	u32 quota;
	u32 allocated;
	u32 max_spb_cnt;
	u32 min_spb_cnt;

	struct {
		u16 block;	///< if free <= block, block IO
		u16 heavy;	///< if free <= heavy, fixed minimal speed
		u16 start;	///< if free <= start, start GC trigger
		u16 end;		///< if free > end, end GC loop
	} gc_thr;

	union {
		struct {
			u32 gcing : 1;	///< spb pool is gcing trigger by spb free count
		} b;
		u32 all;
	} flags;
} ftl_ns_pool_t;

#define ftl_ns_pool_init	{ .quota = 0, .allocated = 0, .max_spb_cnt = 0, .flags.all = 0,}
#define ftl_ns_pools_init	{ftl_ns_pool_init, ftl_ns_pool_init, ftl_ns_pool_init, ftl_ns_pool_init}

typedef struct _ftl_ns_t {
	u32 capacity;
	u32 capacity_in_spb;
	ftl_ns_pool_t pools[SPB_POOL_MAX];

	union {
		struct {
			u32 slc : 1;
			u32 native : 1;
			u32 mix : 1;
			u32 tbl : 1;
		} b;
		u32 all;
	} attr;

	union {
		struct {
			u32 spb_queried : FTL_CORE_MAX;
			u32 flushing : 1;
			u32 desc_flushing : 1;
			u32 weak_spb_gc : 1;
			u32 reconing : 1;
			u32 QBT : 1;
		} b;
		u32 all;
	} flags;

	spb_queue_t *spb_queue[FTL_CORE_MAX];
	spb_queue_t dirty_spb_que[FTL_CORE_MAX];	///< spb closed but its l2pp haven't been flushed

	ftl_ns_desc_t *ftl_ns_desc;
	sys_log_desc_t ns_log_desc;
	sys_log_desc_t ns_range_desc;
	sld_flush_t sld_flush;

	u8 retention_chk;
	u8 alloc_retention_chk;
	u32 closed_spb_cnt;
	// gc threshold
} ftl_ns_t;


extern volatile ns_t ns[INT_NS_ID];
extern u32 _max_capacity;
extern volatile u16 ps_open[3];
extern volatile u8  QBT_BLK_CNT;
extern volatile u8  ftl_pbt_cnt;
//extern volatile u8  ftl_pbt_idx;
extern u32 _max_capacity;


extern volatile stGC_GEN_INFO gGCInfo;       //GC information

extern bool gc_continue;
extern bool gc_suspend_stop_next_spb;
extern bool gc_suspend_start_wl;

extern u32 CAP_NEED_SPBBLK_CNT;
/*!
 * @brief initialize whole disk to reserved namespace 0, and create internal namespace
 *
 * @return	not used
 */
void ftl_ns_init(void);

/*!
 * @brief create and allocate a new namespace from reserved namespace 0
 *
 * @param nsid		created namespace id
 * @param cap		capacity of namespace
 * @param slc		if need SLC
 * @param native	if native type used
 *
 * @return		return true if created successfully
 */
bool ftl_ns_alloc(u32 nsid, u32 cap, bool slc, bool native);

/*!
 * @brief link a pool to namespace, and allocate enough quota from pool
 *
 * @param nsid		namespace id
 * @param pool_id	pool id to be linked
 * @param min_spb_cnt	minimal spb need by namespace
 * @param max_spb_cnt	maximal spb need by namespace
 *
 * @return		not used
 */
void ftl_ns_link_pool(u32 nsid, u32 pool_id, u32 min_spb_cnt, u32 max_spb_cnt);

/*!
 * @brief add allocated spb count of namespace when spb was allocated by a namespace
 *
 * @param nsid		namespace id
 * @param pool_id	pool id
 *
 * @return		not used
 */
void ftl_ns_add_pool(u32 nsid, u32 pool_id);

/*!
 * @brief get namespace's spb pool
 *
 * @param nsid		namespace id
 * @param pool_id		pool id
 *
 * @return		namespace's spb pool
 */
ftl_ns_pool_t *ftl_ns_get_pool(u32 nsid, u32 pool_id);

/*!
 * @brief get capacity in du of namespace
 *
 * @param nsid		namespace id
 *
 * @return		capacity of namespace
 */
u32 ftl_ns_get_capacity(u32 nsid);

/*!
 * @brief start all namespace, boot FTL
 *
 * @return		not used
 */
void ftl_ns_start(void);

/*!
 * @brief trigger spb allocation for namespace
 *
 * @param nsid		namespace id
 * @param type		required type
 *
 * @return		not used
 */
void ftl_ns_spb_trigger(u32 nsid, u32 type);
/*!
 * @brief calculate min/max spb for namespace
 *
 * @param nsid			namespace id
 * @param min_spb_cnt		min spb required count of namespace
 * @param max_spb_cnt		max spb required count of namespace
 * @param min_spare_ratio	spare ratio per SPB, unit: 1/10000
 * @param max_spare_ratio	spare ratio per SPB, unit: 1/10000, 5000 which mean max spb count will be double of min spb count
 * @param mu_ratio		mu ratio, default is 1(4K):1(4K), internal may be different, 32K:4K = 8
 *
 * @return			not used
 */
void ftl_ns_calc_cap_min_max(u32 nsid, u32 *min_spb_cnt, u32 *max_spb_cnt, op_per_t min_spare_ratio, op_per_t max_spare_ratio, u32 mu_ratio);

/*!
 * @brief reduced allocated spb count of namespace
 *
 * @param nsid		namespace id
 * @param pool_id	pool id
 *
 * @return		not used
 */
void ftl_ns_spb_rel(u32 nsid, u32 pool_id);

/*!
 * @brief api to get ftl namespace object by namespace id
 *
 * @param nsid		namespace id
 *
 * @return		return namespace object
 */
ftl_ns_t *ftl_ns_get(u32 nsid);
u16 get_gcinfo_shuttle_cnt(void);
u16 get_gcinfo_rddisturb_cnt(void);
u16 get_gcinfo_patrolrd_cnt(void);
u16 set_gcinfo_patrolrd_cnt(void);
u16 set_gcinfo_shuttle_cnt(void);
u16 set_gcinfo_rddisturb_cnt(void);

/*!
 * @brief initialize ftl internal namespace
 *
 * @note link pool here
 *
 * @param ns	namespace object of internal namespace
 *
 * @return	not used
 */
void ftl_int_ns_init(ftl_ns_t *ns);

/*!
 * @brief start internal namespace, initialize l2p, virgin/clean/dirty boot here
 *
 * @return	status of internal namespace start, now only support virgin and clean
 */
ftl_err_t ftl_int_ns_start(void);

/*!
 * @brief lookup pda of host l2p segment
 *
 * @param segid		host l2p segment to be looked up
 *
 * @return		pda of segment
 */
pda_t ftl_int_ns_lookup(u32 segid);

/*!
 * @brief update descriptor of internal namespace, called before flushing
 *
 * @return		not used
 */
void ftl_int_ns_update_desc(void);

/*!
 * @brief flush l2p of namespace, including valid count table of internal namespace
 *
 * @return		not used
 */
void ftl_int_ns_flush_l2p(void);

/*!
 * @brief api to get lda of misc data to be stored
 *
 * @return	start lda of misc data
 */
u32 ftl_int_ns_get_misc_pos(void);

/*!
 * @brief restore valid count table of internal namespace
 *
 * @note valid count table of internal namespace was stored in system log instead of misc data
 *
 * @return	not used
 */
void ftl_int_ns_vc_restore(void);

/*!
 * @brief api to format internal space (not ready)
 *
 * @param ns	ftl namespace object of internal namespace
 *
 * @return	not used
 */
void ftl_int_ns_format(ftl_ns_t *ns);

/*!
 * @brief initialize ftl host namespace, allocate l2p memory
 *
 * @param ns		ftl namespace object
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_host_ns_init(ftl_ns_t *ns, u32 nsid);

/*!
 * @brief post ftl host namespace information to let FE start early
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_host_ns_post(u32 nsid);

/*!
 * @brief start host namespace, initialize l2p, virgin/clean/dirty boot here
 *
 * @param nsid		namespace id
 * @param ins_err	internal namespace start status
 *
 * @return		not used
 */
ftl_err_t ftl_host_ns_start(u32 nsid, ftl_err_t ins_err);

/*!
 * @brief update current host descriptor to system log before flushing
 *
 * @note update current active spb and fence before flushing
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_host_ns_update_desc(u32 nsid);

/*!
 * @brief gc next candidate, need to be revised in multiple namespace
 *
 * @param nsid		namespace id of last GC
 *
 * @return		not used
 */
void ftl_ns_gc_start_chk(u32 nsid);

/*!
 * @brief update current GC performance for flow control monitor
 *
 * @param nsid		namespace id
 * @param free		free du count in last GC
 *
 * @return		not used
 */
void ftl_ns_upd_gc_perf(u32 decrease_percent);

/*!
 * @brief initialize ftl namespace descriptor, when virgin
 *
 * @param desc		ftl namespace descriptor's system log descriptor
 *
 * @return		not used
 */
void ftl_ns_desc_init(sys_log_desc_t *desc);

/*!
 * @brief api to flush descriptor to make namespace clean
 *
 * @param nsid		namespace id
 * @param caller	caller context
 * @param caller_cmpl	callback function when flushed
 *
 * @return		true if already clean
 */
bool ftl_ns_make_clean(u32 nsid, void *caller, completion_t caller_cmpl);

/*!
 * @brief api to flush descriptor to make namespace dirty
 *
 * @param nsid		namespace id
 * @param caller	caller context
 * @param caller_cmpl	callback function when flushed
 * @param force		force flush ns descriptor
 *
 * @return		true if already dirty
 */
bool ftl_ns_make_dirty(u32 nsid, void *caller, completion_t caller_cmpl, bool force);

/*!
 * @brief format host namespace
 *
 * @param ns		ftl namespace
 * @param ns_id		namespace id
 * @param host_meta	if host meta was enabled
 *
 * @return		not used
 */
void ftl_host_ns_format(ftl_ns_t *ns, u32 ns_id, bool host_meta);

/*!
 * @brief get spare block count of namespace
 *
 * @param nsid		namespace id
 * @param avail		current avail spare block count
 * @param spare		spare block count when virgin
 * @param thr		threshold of spare count for smart
 *
 * @return		not used
 */
void get_host_ns_spare_cnt(u32 nsid, u32 *avail, u32 *spare, u32 *thr);

/*!
 * @brief flush ftl namespace descriptor to system log
 *
 * @param nsid		namespace id
 * @param caller	caller context
 * @param caller_cmpl	callback when completion
 *
 * @return		always return false
 */
bool ftl_ns_flush_desc(u32 nsid, void *caller, completion_t caller_cmpl);

/*!
 * @brief api to increase name space's closed spb count
 *
 * @param spb		spb id
 *
 * @return		not used
 */
void ftl_ns_inc_closed_spb(spb_id_t spb);


/*!
 * @brief format ftl namespace
 *
 * @param nsid		namespace id
 * @param host_meta	if host meta was enabled
 *
 * @return		not used
 */
void ftl_ns_format(u32 nsid, bool host_meta);

void ftl_ns_dr_gc(u32 nsid, spb_id_t spb_id);
/*!
 * @brief start a weak SPB gc
 *
 * @param nsid		namespace id of this weak SPB
 * @param spb_id	id of SPB
 *
 * @return		not used
 */
void ftl_ns_weak_spb_gc(u32 nsid, spb_id_t spb_id);
void ftl_ns_close_open(u32 nsid, spb_id_t spb_id, u8 fill_type);	// ISU, LJ1-337, PgFalClsNotDone (1)

/*!
 * @brief add erased spb into namespace ready spb queue
 *
 * @param nsid		namespace id
 * @param type		FTL_CORE_NRM or FTL_CORE_GC
 * @param spb_id	id of SPB
 *
 * @return		not used
 */
void ftl_ns_add_ready_spb(u32 nsid, u32 type, spb_id_t spb_id);

/*!
 * @brief retire a spb in ftl namespace
 *
 * @param nsid		namespace id
 * @param pool_id	pool id
 *
 * @return		not used
 */
void ftl_ns_spb_retire(u32 nsid, u32 pool_id);

/*!
 * @brief remove dirty spb from dirty spb queue when table flushed
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_ns_dirty_spb_remove(u32 nsid);

/*!
 * @brief check if ftl ns was already clean
 *
 * @param nsid		namespace id
 *
 * @return		return true if clean
 */
bool ftl_ns_is_clean(u32 nsid);

/*!
 * @brief post ftl namespace to FE, let FE start early
 *
 * @return		not used
 */
void ftl_ns_post(void);

/*!
 * @brief fc ready handler
 *
 * @param req		not used
 *
 * @return		not used
 */
void ftl_check_fc_done(volatile cpu_msg_req_t *req);

/*!
 * @brief dump function of ftl ns
 *
 * @return		not used
 */
void ftl_ns_dump(void);
void ftl_ns_qbf_clear(u8 nsid, u8 type);
extern void ftl_free_flush_blklist_dtag(ftl_core_ctx_t *ctx);
extern ftl_flush_misc_t blklist_tbl[2];


/*! @} */
