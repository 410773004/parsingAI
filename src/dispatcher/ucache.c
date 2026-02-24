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
/*! \file ucache.c
 * @brief ucache support
 *
 * \addtogroup dispatcher
 * \defgroup rainier
 * \ingroup dispatcher
 * @{
 *
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
/*
 * XXX: CAVEAT
 *
 * uCache is a litte different from Cache, CE(s) is for L2P but not for Dtag.
 * Both CE and Dtag have separate reference count.
 *
 * New Read just increases the reference count of Dtags but not CE's.
 * And if the ce is not CE_ST_DIRTY/CE_ST_L2UP, waiting for the state change,
 * etc. the merge is done.
 *
 * The only criterion to release a CE is L2P updates done and CE_ST_L2UP.
 */

#include "nvme_precomp.h"
#include "mod.h"
#include "event.h"
#include "console.h"
#include "assert.h"
#include "sect.h"
#include "types.h"
#include "misc.h"
#include "dtag.h"
#include "mpc.h"
#include "l2p_mgr.h"
#include "bf_mgr.h"
#include "rdisk.h"
#include "fc_export.h"
#include "ipc_api.h"
#include "req.h"
#include "nvme_spec.h"
#include "ftl_meta.h"
#include "ncl_exports.h"
#include "spin_lock.h"
#include "trim.h"
#include "ucache.h"
#if (CO_SUPPORT_READ_AHEAD == TRUE)
#include "ra.h"
#endif
/*! \cond PRIVATE */
#define __FILEID__ ucache
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
//#define EXTERN_CACHE_ENTRY_LIST     (false)

#define ADD_TO_TAIL       (1)
#define ADD_TO_HEAD       (2)

#define TYPE_HASH_LIST      (1)
#define TYPE_ENTRY_LIST     (2)

#define CE_HLIST_SHF            12			///< hash list size setting
#define CE_HLIST_SZE      (1 << CE_HLIST_SHF)		///< hash list size
#define CE_HLIST_MSK      (CE_HLIST_SZE - 1)		///< mask to get hash list position

#define MERGER_HLIST_SHF        6			///< merger hash list size setting
#define MERGER_HLIST_SZE  (1 << MERGER_HLIST_SHF)	///< merger hash list size
#define MERGER_HLIST_MSK  (MERGER_HLIST_SZE - 1)	///< merger mask to get hash list position

#define LOG_UCACHE_DEBUG    LOG_DEBUG
#define LOG_WUNC_INFO       LOG_INFO


#define CR_IDX_MASK             (0x7FFF)
#define MEM_CR_MASK             (0x8000)
#define MEM_CR_CHECK(cr_idx)    ((cr_idx) & MEM_CR_MASK)
#define CR_CHECK_FREE(cr_idx)        (((cr_idx) & CR_IDX_MASK) == CR_IDX_MASK)

#define CES_CNT 		(TOTAL_DTAG + 260)		///< number of ce // PJ1 one bin can not use

//#define CAT_CTX_CNT 		256			///< number of cancat context

#define CRT_CNT         1024

#define RDISK_L2P_FE_SRCH_QUE 		0		///< l2p search queue id

#define UPDATER_FLUSH_TRH 		24		///< threshold to flush updater
#define READER_CAN_IMT_THR 		32		///< threshold to trigger concat list

#define UCACHE_TRIM_RANGE_THR		16		///< threshold to trigger idle trim range

#define WR_HOLD_THRESHOLD           256
#define WR_CANCEL_HOLD_THRESHOLD    512

#define CE_ID(_ce) 	((_ce) - _ces)			///< get ce id
#define ID_CE(id) 	&_ces[id]			///< get ce by id

#if (CPU_DTAG != CPU_ID)
#define dtag_put_ex(X)		rdisk_dref_dec((u32*)&(X), 1);
#define dtag_ref_inc_ex(X) 	rdisk_dref_inc((u32*)&(X), 1);
#endif

#define FUA_BTAG_FREE		0	///< indicate no fua
#define	FUA_BTAG_ON		0xFFFF	///< indicate fua process on ce
#define IS_WUNC_CACHE(btag)  ((btag) & WUNC_BTAG_TAG)
#define IS_FUA_CACHE(btag)  ((btag) & FUA_BTAG_TAG)
#define IS_FUAorWUNC_CMD(btag)  ((btag) & (~BTAG_MASK))


typedef CBF(ftl_flush_data_t*, 256) ucache_flush_ntf_que_t;

//#define UNALIGNED_CONCAT_MERGER 	FEATURE_SUPPORTED
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef struct {
	u32   ofst;		///< offset of the command
	dtag_t dtag;		///< lda
} _unmap_data_fill_ctx_t;

typedef struct {
	struct list_head entry;	///< list entry
	int btag;		///< btag
	int du_ofst;		///< du offset
} rp_ctx_t;

typedef struct {
	u32 size;		///< size
	u32 avail;		///< available count
	pool_t pool;		///< pool
} rs_pool_t;

enum ce_state_t {
	CE_ST_WANT = 0,	//0	///< ce wait for data
	CE_ST_READ,     //1 	///< ce on reading
	CE_ST_MERGE,    //2 	///< ce on merging
	CE_ST_DIRTY,	//3	    ///< ce not update but used
	CE_ST_L2UP,     //4 	///< ce has issued to L2P engine
	CE_ST_INVALID,  //5     ///< ce is invalid
	CE_ST_HOLD,     //6     ///< ce is hold in ce list
	CE_ST_START,    //7    ///<  par ce, ofst = 0
	CE_ST_CONCAT,   //8
};

/* a merger is to merge LDA with many mslices */
typedef struct {
	struct list_head entry; ///< binds to MERGER    //dw0,1
	//lda_t lda;		///< lda                        //dw2
	void* ce;           //ce_t* type pointer

	//struct list_head head; 	///< head of mslice     //dw5,6
	dtag_t dtag;
    //merger s & len
    u8 lba_s;               //use to record merge lba start, if merge pending
    u8 nlba;                //use to record merge lba length, if merge pending
    //dtag s & len
    u8 src_lba_s;           //record dst dtag valid lba start
    u8 src_nlba;            //record dst dtag valid lba length
	//bool concating;		///< on concat
	//bool updating;		///< on update
	u32 concating   : 1;                            //dw7
    u32 updating    : 1;
    u32 ongoing     : 8; 		///< number of ongoing
    u32 size 		: 8;///< number of mslice           //dw3
    u32 wunc_bitmap : 8;
    u32 rev32       : 6;
} merger_t;


#if 0
typedef struct {
//*****never add var before & between this line*******
//*****never change the sequence*******
    u16 entry_prev;     //TYPE_HASH_LIST        //dw0
    u16 entry_next;

    u16 entry2_prev;    //TYPE_ENTRY_LIST       //dw1
    u16 entry2_next;

    u16 ce_prev;
    u16 ce_next;
//******never add var before & between this line********
    //merger_t *mrgr;

    //u16 ce_idx;                                 //dw2
    u32 nrm     : 1;            //nrm data or partial data
    u32 nlba    : 4;
    u32 ofst    : 4;
    u32 state    : 7;
    u32 rph_head : 16;
   // u8  rev;

    lda_t lda;			///< lda                //dw3
    dtag_t dtag;			///< dtag           //dw4

    u16 btag;
    union {
        u16 mrgr_indx;
        u8 wunc_bitmap;
    };

} ce_t;
#endif
BUILD_BUG_ON(sizeof(ce_t)!= 28);
BUILD_BUG_ON(sizeof(merger_t) > sizeof(ce_t));


typedef enum {
	READER_IMT = 0,		///< read immediately
	READER_CAN, 		///< postpone NAND read in case of concat merge
	READER_CAN_FLUSH, 	///< no wait any more, READ_IMT to merge
	READER_RADJ,		///< read adjust to bcmd
	READER_CANCEL,		///< cancel read action
} read_op_t;

typedef struct {
	//ce_t* rdp_list_head;
	//ce_t* rdp_list_tail;
	/* out of Dtag so waiting */
	//struct list_head w8ing;			///< waiting list
	ce_t* w8ing_head;
	ce_t* w8ing_tail;
	/* read candidates list head
	 * 1) host read (o)
	 * 2) dtag is out of order
	 * 3) concat merge is discontiguous (o)
	 * 4) host flushing
	 * 5) timer
	 */
	//struct list_head can_ls;
	ce_t* can_ls_head;		///< postpone list
	ce_t* can_ls_tail;
	u32 can_cnt;				///< postpone count
	u32 last_can_cnt;			///< last count for idle detect
	struct timer_list can_ls_timer;		///< timer to trigger idle can_ls

	u32 rs_cnt;				///< total read search count
	u32 rr_cnt;				///< total fw read count
#define CACHE_READER_LDA_SZE  64		///< total read record size
	//lda_t ldas[CACHE_READER_LDA_SZE];	///< read record
	ce_t *ce[CACHE_READER_LDA_SZE];
	u32 lda_bmp[CACHE_READER_LDA_SZE/BITS_PER_ULONG];	///< read record used bitmap
	dtag_t *dtags;
} READER_t;

typedef enum {
	MERGER_NEW = 0,		///< new insert to merger
	MERGER_NEW_UPDT,	///< new insert to merger, wait update done
	MERGER_CONCAT,		///< concat to merger
	MERGER_CANCEL,		///< cancel merger action
	MERGER_TRIGGER,		///< trigger merge
	MERGER_TRIGGER_FORCE,	///< force trigger
} merger_op_t;

typedef enum {
	MERGER_ST_NEW,		///< slice new
	MERGER_ST_COMMIT,	///< slice merge ongoing
	MERGER_ST_CANCELED	///< slice canceled
} merger_slice_state_t;

typedef struct {
	struct list_head entry;	///< binds to merger                //dw0,1
	merger_t *merger;       ///< for fast reference to merger   //dw2
	dtag_t dtag;		///< dtag                               //dw3
	dtag_t dstd;   		///< dtst dtag                          //dw4
	//u16 btag;		///< btag                                   //dw5
	//u8 unmap_dst;		///< merge to unmap
	//u8 state;		///< state
	//u8 du_ofst;		///< du offset                              //dw6
	//u8 nlba;		///< number of lba
	u32 btag        : 16;		///< btag
	u32 unmap_dst   : 1;		///< merge to unmap
	u32 du_ofst     : 4;		///< du offset
	u32 nlba        : 4;		///< number of lba
	u32 state       : 4;		///< state
} merger_slice_t;
BUILD_BUG_ON(sizeof(merger_slice_t) > sizeof(ce_t));

typedef struct {
	merger_slice_t *src;	///< source slice
	merger_slice_t *dst;	///< destination slice
} merger_cat_ctx_t;

typedef struct {
	int size;		///< total merger task
	int mergeing_size;
	struct list_head head;	///< head hash list
	struct list_head head2;	///< head hash list
	rs_pool_t cat_ctx_pool;	///< concat pool
} MERGER_t;

typedef enum {
	UPDATER_ST_NEW,		///< new insert to updater
	UPDATER_ST_CANCEL,	///< cancel ce update
	UPDATER_ST_FORCE_UPDATE,///< force flush update
	UPDATER_CONTINUE,       ///< continue commit dtag to cpu2
} updater_op_t;

typedef struct {
	//struct list_head head;		///< head list
	ce_t* head;
	ce_t* tail;
	u32 size;			///< size of the waiting ce

	u32 updt_cnt;			///< update ongoing count
	u32 last_updt_cnt;		///< last update count to detect idle
	struct timer_list updt_timer;	///< update check timer

	ftl_flush_data_t *fctx;		///< flush context
	struct timer_list flush_timer;	///< flush check timer
	bool lock;						///< update lock
} UPDATER_t;

typedef struct {
	ce_t* head;
	ce_t* tail;
	u32 size;			///< size of the fua cmd list
	u32 warning_cnt;			///< abort system fail	
	struct timer_list fua_ls_timer;		///< timer to trigger idle fua_ls
} FUALIST_t;

typedef struct {
	struct list_head req;		///< list for range operation
	u32 *wbmp;			///< waiting bitmap
	u32 wnum;			///< waiting number
	u32 *fbmp;			///< flushing bitmap
	u32 fnum;			///< flushing number
	u8 *fcnt;			///< flushing counter
	u32 size;			///< size of bmp
	union {
		u32 all;
		struct {
			u32 clean : 1;		///< range clean
			u32 suspend : 1;	///< range operation suspend
			u32 cpu_trim : 1;	///< cpu trim
		} b;
	} flags;
} range_t;

typedef struct {
	//struct list_head ce_hlist[CE_HLIST_SZE];	///< ce hash list
	u16 hash_table[CE_HLIST_SZE];
	rs_pool_t ce_pool;				///< ce pool

	MERGER_t MERGER;		///< merger
	READER_t READER;		///< reader
	UPDATER_t UPDATER;		///< updater
	FUALIST_t FUALIST;      ///< fualist

	u32 slice_cnt;
	#if 0//(TRIM_SUPPORT == DISABLE)
	range_t range;			///< range
	#endif
} ucache_mgr_t;

typedef struct {
    //u8 prev;
    u16 next;
    //u8 index;
    u8 btag;
    u8 du_ofst;
} cache_read_t;
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
extern volatile u32 otf_fua_cmd_cnt;
extern volatile u8 CPU1_plp_step;
extern u16 ua_btag; 			///< special btag for firmware read
extern u16 wunc_btag;
extern volatile u32 shr_dtag_ins_cnt;	///< dtag insert to be count
#if PLP_TEST == 1
extern volatile u8 plp_trigger;
#endif
static fast_data_ni cache_read_t crt[CRT_CNT];
static fast_data_ni rs_pool_t crt_pool;
//static fast_data_ni ce_t _ces[CES_CNT];	///< ce entries
static fast_data_ni ce_t *_ces;	///< ce entries
extern u32 cache_cnt; // replace CES_CNT for one bin
//extern u16 global_capacity;

//static slow_data_ni merger_cat_ctx_t _cat_ctx[CAT_CTX_CNT];	///< concat context
static fast_data_zi ucache_mgr_t _umgr;		///< cache manager unit
share_data_zi volatile u16 * ftl_core_gc_umgr;
share_data_zi ce_t * ftl_core_gc_ces;
share_data_zi volatile u16 lock_ce_hash1;
share_data_zi volatile u16 lock_ce_hash0;

static fast_data ucache_mgr_t *umgr = &_umgr;	///< cache manager
//static fast_data_ni dtag_t dtag_list[UPDATER_FLUSH_TRH];	///< dtag list for commit
//static fast_data_ni lda_t lda_list[UPDATER_FLUSH_TRH];	///< lda list for commit
//static fast_data_ni u16 btag_list[UPDATER_FLUSH_TRH];	///< btag list for commit
//static fast_data_ni ce_t *ce_list[UPDATER_FLUSH_TRH];	///< ce list for commit
//static fast_data_ni u16 _r_dtag2ces[TOTAL_DTAG];		///< dtag to ce table
static fast_data_ni u16 *_r_dtag2ces;		///< dtag to ce table
static fast_data_zi u32 remote_dtag_wait = 0;
fast_data_zi u32 ucache_read_pend_cnt = 0;
share_data_ni dtag_t cache_read_dtags[CACHE_READER_LDA_SZE];

share_data_ni dtag_comt_t shr_dtag_comt;			///< dtag commit queue
share_data_ni volatile draf_comt_t shr_dref_inc;			///< dtag reference count increase queue
share_data_ni volatile draf_comt_t shr_dref_dec;			///< dtag reference count decrease queue
share_data_zi volatile u32 shr_range_size = 0;			///< range bitmap size
share_data_zi volatile u8 *shr_range_ptr = 0;			///< range bitmap pointer
share_data_zi volatile u32 *shr_range_wnum = 0;			///< range wit number pointer
extern u32 cache_handle_dtag_cnt;

//share_data_zi volatile u32 need_force_flush_timer = 0;

fast_data_ni ucache_flush_ntf_que_t ucache_flush_ntf_que;
fast_data bool ucache_force_flush = false;
extern void btn_de_wr_hold_handle(u32 cnt, u32 hold_thr, u32 dis_thr);
static fast_code void ucache_flush_check(u32 parm, u32 payload, u32 sts);

////temp use cache entry to store merge slice
#define MERGE_SLICE_GET()       pool_get_ex(&umgr->ce_pool.pool)
#define MERGE_SLICE_PUT(p)      pool_put_ex(&umgr->ce_pool.pool, (void *) p)
#define ce_index(ce)            ((ce_t*)(ce) - _ces)
#define ce_index_2_ce(idx)      (&_ces[idx])
#define mgr_index(mgr)          ((ce_t*)(mgr) - _ces)
#define mgr_index_2_mgr(idx)    ((merger_t*)(&_ces[idx]))

#define is_semi_dtag(dtag)      ((dtag).b.type_ctrl & BTN_SEMI_STREAMING_MODE)
#define is_ddtag(dtag)          (is_semi_dtag(dtag) == false)

#if(BG_TRIM == ENABLE)
extern bg_trim_mgr_t bg_trim;
#endif

#if 0
#define UCACHE_FREE_CE(ce)      sys_assert((ce) != NULL); \
                                pool_put_ex(&umgr->ce_pool.pool, (void *) (ce)); \
                                umgr->ce_pool.avail++;\
                                cache_handle_dtag_cnt--;\
                                if (IS_FUAorWUNC_CMD((ce)->btag)) {       \
                                	rdisk_fua_done((ce)->btag,(ce)->dtag);  \
                                }       \
                                btn_de_wr_hold_handle(umgr->ce_pool.avail, WR_HOLD_THRESHOLD, WR_CANCEL_HOLD_THRESHOLD)
#define UCACHE_FREE_MRGR(mrgr)  sys_assert(mrgr != NULL); \
                                pool_put_ex(&umgr->ce_pool.pool, (void *) (mrgr)); \
                                umgr->ce_pool.avail++;\
                                btn_de_wr_hold_handle(umgr->ce_pool.avail, WR_HOLD_THRESHOLD, WR_CANCEL_HOLD_THRESHOLD)
#endif

#define NON_WUNC_BIT_SET(lba, ofst, wunc_bitmap)      ((~(((1 <<(lba)) - 1) << (ofst))) & (wunc_bitmap))
#define WUNC_BIT_SET(lba, ofst, wunc_bitmap)
#if(BG_TRIM == ENABLE)
fast_data u8 evt_trim_range_issue = 0xff;		///< event for range trim
#endif
fast_data u8 evt_cache_force_flush = 0xff;
//joe add sec size 20200817
extern u8 host_sec_bitz;
extern u16 host_sec_size;
//joe add sec size 20200817
#define crt_index(cr) ((cr) - crt)
extern void rdisk_dtag_gc(u32 *need);
extern volatile ns_t ns[INT_NS_ID];
//
fast_data u8 evt_get_pend_dtag = 0xff;

fast_code void par_data_in_handle(ce_t* ce);
fast_code void par_data_in_hit_par(ce_t* ce, ce_t* next);
fast_code void nrm_data_in_handle(ce_t* ce);
static fast_code void cache_entry_add(ce_t* ce, ce_t** list_head, ce_t** list_tail, u32 add);
fast_code void ucache_inc_ddtag(dtag_t dtag);
fast_code void ucache_free_dtag(dtag_t dtag);
fast_code ce_t* cache_search(u32 lda);


fast_code void UCACHE_FREE_CE(ce_t* ce)
{
    sys_assert((ce) != NULL);
    if (IS_FUAorWUNC_CMD((ce)->btag)) {
		rdisk_fua_done((ce)->btag,(ce)->dtag);
    }
    pool_put_ex(&umgr->ce_pool.pool, (void *) (ce));
    umgr->ce_pool.avail++;
    cache_handle_dtag_cnt--;
    btn_de_wr_hold_handle(umgr->ce_pool.avail, WR_HOLD_THRESHOLD, WR_CANCEL_HOLD_THRESHOLD);

}

fast_code void UCACHE_FREE_CE_and_DTAG(ce_t* ce , bool update_done)
{
    sys_assert((ce) != NULL);
    if (IS_FUAorWUNC_CMD((ce)->btag)) 
    {
    	if(!update_done)//fua cmd is no done!!! because data still keep in ucache
    	{
    		if(cache_search(ce->lda) != NULL)
    		{
    			//add this is fua list
	    		sys_assert(ce->entry2_prev == INV_U16);
	    		sys_assert(ce->entry2_next == INV_U16);
	            cache_entry_add(ce, &umgr->FUALIST.head, &umgr->FUALIST.tail, ADD_TO_TAIL);
	            if(umgr->FUALIST.size == 0)
	            	mod_timer(&umgr->FUALIST.fua_ls_timer, jiffies + HZ);
	            umgr->FUALIST.size++;
	            //ucache_inc_ddtag(ce->dtag);//inc 1 
	            //disp_apl_trace(LOG_ALW, 0x4807,"add fua list btag:0x%x lda:0x%x dtag:0x%x nlba:0x%x ofst:0x%x cnt:%d",ce->btag,ce->lda,ce->dtag.dtag,ce->nlba,ce->ofst,umgr->FUALIST.size);
	            return;
    		}
    		else
    		{
				//disp_apl_trace(LOG_ALW, 0xedde,"[FUA] lda:0x%x has update done,just release!!",ce->lda);
    		}
    	}
		rdisk_fua_done((ce)->btag,(ce)->dtag);
    }
    pool_put_ex(&umgr->ce_pool.pool, (void *) (ce));
    umgr->ce_pool.avail++;
    cache_handle_dtag_cnt--;
    //free dtag
    ucache_free_dtag(ce->dtag);
    btn_de_wr_hold_handle(umgr->ce_pool.avail, WR_HOLD_THRESHOLD, WR_CANCEL_HOLD_THRESHOLD);

}


fast_code void UCACHE_FREE_MRGR(merger_t* mrgr)
{
    sys_assert(mrgr != NULL);
    pool_put_ex(&umgr->ce_pool.pool, (void *) (mrgr));
    umgr->ce_pool.avail++;
    btn_de_wr_hold_handle(umgr->ce_pool.avail, WR_HOLD_THRESHOLD, WR_CANCEL_HOLD_THRESHOLD);
}

//-----------------------------------------------------------------------------
//  Codes
//-----------------------------------------------------------------------------
/*!
 * @brief reader for cache read
 *
 * @param type		type of action
 * @param ce		pointer to ce
 *
 * @return		not used
 */
static fast_code void READER(read_op_t type, ce_t *ce);

/*!
 * @brief merger for cache to merge
 *
 * @param ops		operation code
 * @param lda		lda
 * @param mslice	pointer to slice
 *
 * @return		not used
 */
static fast_code void MERGER(merger_op_t ops, ce_t* ce);

fast_code ce_t* ucache_get_pre_ce(ce_t* ce, u8 state)
{
    ce_t* tmp_ce = NULL;
    ce_t* ret_ce = NULL;
    while(ce->ce_prev != INV_U16) {
        tmp_ce = ce_index_2_ce(ce->ce_prev);
        if (tmp_ce->state == state) {
            ret_ce = tmp_ce;
            break;
        }
        ce = tmp_ce;
    }
    return ret_ce;
}

/*!
 * @brief get ce hash value
 *
 * @param lda		lda
 * @param mask		mask value
 *
 * @return		mask result
 */
static fast_code inline u8 ucache_hash(lda_t lda, u32 mask)
{
	return lda & mask;
}

fast_code bool flush_que_chk(void)
{
	if (ucache_flush_ntf_que.wptr != ucache_flush_ntf_que.rptr)
	{
		//disp_apl_trace(LOG_DEBUG, 0, "uwptr:%d urptr:%d", ucache_flush_ntf_que.wptr, ucache_flush_ntf_que.rptr);
		return true;
	}
	else
	{
		return false;
	}
}

#ifdef EXTERN_CACHE_ENTRY_LIST
static fast_code ce_t* cache_entry_next(ce_t* ce, u32 type)
{
    u32 ofst = 0;
    switch(type) {
        case TYPE_HASH_LIST:
            ofst = 0;
            break;
        case TYPE_ENTRY_LIST:
            ofst = 2;
            break;
        default:sys_assert(0);
    }

    u16 *next = (u16*)ce + ofst + 1;
    if (*next == INV_U16) {
        return NULL;
    } else {
        return &_ces[*next];
    }
}

/*!
 * @brief add cache to list
 *
 * @param ce:           cache entry to add to list
 * @param list_head:    list_head address
 * @param list_tail:    list_tail address
 * @param add:          add to tail or add to head
 * @param type		    list entry type
 *
 * @return		none
 */
static fast_code void cache_entry_add(ce_t* ce, ce_t** list_head, ce_t** list_tail, u32 add, u32 type)
{
    u32 ofst = 0;
    switch(type) {
        case TYPE_HASH_LIST:
            ofst = 0;
            sys_assert(0); //hash table will call cache_insert
            break;
        case TYPE_ENTRY_LIST:
            ofst = 2;
            break;
        default:sys_assert(0);
    }

    u16* ce_prev = (u16*)ce;
    u16* ce_next = (u16*)ce + 1;
    u16* list_next = (u16*)NULL;
    u16* list_prev = (u16*)NULL;

    ce_prev += ofst;
    ce_next += ofst;

    if (add == ADD_TO_HEAD) {
        list_prev = (u16*)(*list_head) + ofst;
        list_next = (u16*)(*list_head) + ofst + 1;
    } else if (add == ADD_TO_TAIL) {
        list_prev = (u16*)(*list_tail) + ofst;
        list_next = (u16*)(*list_tail) + ofst + 1;
    }

    if (*list_head == NULL) { //list is empty
        *list_head = *list_tail = ce;
        *ce_next = *ce_prev = INV_U16;
    } else if (add == ADD_TO_HEAD) {
        *ce_next   = ce_index(*list_head);
        *list_prev = ce_index(ce);
        *list_head = ce;
        *ce_prev   = INV_U16;
    } else if (add == ADD_TO_TAIL) {
        *ce_prev   = ce_index(*list_tail);
        *list_next = ce_index(ce);
        *list_tail = ce;
        *ce_next   = INV_U16;
    }
}

/*!
 * @brief delete cache entry from list
 *
 * @param ce:           cache entry to del from list
 * @param list_head:    list_head address
 * @param list_tail:    list_tail address
 * @param type		    list entry type
 *
 * @return		none
 */

static fast_code void cache_entry_del(ce_t* ce, ce_t** list_head, ce_t** list_tail, u32 type)
{
    u32 ofst = 0;
    switch(type) {
        case TYPE_HASH_LIST:
            ofst = 0;
            sys_assert(0);//hash table will call cache_delete
            break;
        case TYPE_ENTRY_LIST:
            ofst = 2;
            break;
        default:sys_assert(0);
    }

    u16* ce_prev = (u16*)ce;
    u16* ce_next = (u16*)ce + 1;

    ce_prev += ofst;
    ce_next += ofst;

    if ((*list_head == ce) && (*list_tail == ce)) {
        *list_head = *list_tail = NULL;
    } else if (*list_head == ce) {
        *list_head = &_ces[*ce_next];

        u16* list_prev = (u16*)(*list_head) + ofst;

        *list_prev = INV_U16;
    } else if (*list_tail == ce) {
        *list_tail = &_ces[*ce_prev];

        u16* list_next = (u16*)(*list_tail) + ofst + 1;

        *list_next = INV_U16;
    } else {
        ce_t* prev = &_ces[*ce_prev];
        ce_t* next = &_ces[*ce_next];

        u16* prev_next = (u16*)prev + ofst + 1;
        *prev_next = ce_index(next);

        u16* next_prev = (u16*)next + ofst;
        *next_prev = ce_index(prev);
    }
    *ce_prev = *ce_next = INV_U16;
}

static fast_code void cache_entry_del_single(ce_t* ce, u32 type)
{
    u32 ofst = 0;
    switch(type) {
        case TYPE_HASH_LIST:
            ofst = 0;
            break;
        case TYPE_ENTRY_LIST:
            ofst = 2;
            break;
        default:sys_assert(0);
    }

    u16* ce_prev = (u16*)ce;
    u16* ce_next = (u16*)ce + 1;

    if (type == TYPE_ENTRY_LIST) {
        //
        if (ce == umgr->READER.can_ls_head || ce == umgr->READER.can_ls_tail) {
            cache_entry_del(ce, &umgr->READER.can_ls_head, &umgr->READER.can_ls_tail, TYPE_ENTRY_LIST);
        } else if (ce == umgr->READER.w8ing_head || ce == umgr->READER.w8ing_tail) {
            cache_entry_del(ce, &umgr->READER.w8ing_head, &umgr->READER.w8ing_tail, TYPE_ENTRY_LIST);
        } else {
            ce_t* prev = &_ces[*ce_prev];
            ce_t* next = &_ces[*ce_next];

            u16* prev_next = (u16*)prev + ofst + 1;
            *prev_next = ce_index(next);

            u16* next_prev = (u16*)next + ofst;
            *next_prev = ce_index(prev);
        }
    } else {
        sys_assert(0); //not support
    }

    *ce_prev = *ce_next = INV_U16;
}
#else
#if 0
static fast_code ce_t* cache_entry_next(u16 next_index)
{
    if (next_index == INV_U16) {
        return NULL;
    } else {
        return &_ces[next_index];
    }
}
#endif
/*!
 * @brief add cache to list
 *
 * @param ce:           cache entry to add to list
 * @param list_head:    list_head address
 * @param list_tail:    list_tail address
 * @param add:          add to tail or add to head
 *
 * @return		none
 */
static fast_code void cache_entry_add(ce_t* ce, ce_t** list_head, ce_t** list_tail, u32 add)
{
    if (*list_head == NULL) {
        *list_head = *list_tail = ce;
        ce->entry2_next = ce->entry2_prev = INV_U16;
    } else if (add == ADD_TO_HEAD) {
        ce->entry2_next = ce_index(*list_head);
        *list_head = ce;
        ce->entry2_prev = INV_U16;
    } else {
        ce->entry2_prev = ce_index(*list_tail);
        (*list_tail)->entry2_next = ce_index(ce);
        *list_tail = ce;
        ce->entry2_next = INV_U16;
    }
}

/*!
 * @brief delete cache from list
 *
 * @param ce:           cache entry to delete
 * @param list_head:    list_head address
 * @param list_tail:    list_tail address
 *
 * @return		none
 */
static fast_code void cache_entry_del(ce_t* ce, ce_t** list_head, ce_t** list_tail)
{
    if ((ce == *list_head) && (ce == *list_tail)) {
        *list_head = *list_tail = NULL;
    } else if (ce == *list_head) {
        *list_head = &_ces[ce->entry2_next];
        (*list_head)->entry2_prev = INV_U16;
    } else if (ce == *list_tail) {
        *list_tail = &_ces[ce->entry2_prev];
        (*list_tail)->entry2_next = INV_U16;
    } else {
        ce_t* prev = &_ces[ce->entry2_prev];
        ce_t* next = &_ces[ce->entry2_next];
        prev->entry2_next = ce_index(next);
        next->entry2_prev = ce_index(prev);
    }
    ce->entry2_next = ce->entry2_prev = INV_U16;
}

/*!
 * @brief delete cache from list,
 *
 * @param ce:   cache entry to delete
 *
 * @return		none
 */
static fast_code void cache_entry_del_single(ce_t* ce)
{
    if (ce == umgr->READER.can_ls_head || ce == umgr->READER.can_ls_tail) {
        cache_entry_del(ce, &umgr->READER.can_ls_head, &umgr->READER.can_ls_tail);
    } else if (ce == umgr->READER.w8ing_head || ce == umgr->READER.w8ing_tail) {
        cache_entry_del(ce, &umgr->READER.w8ing_head, &umgr->READER.w8ing_tail);
    }
    else {
        if (ce->entry2_next == INV_U16 && ce->entry2_prev == INV_U16) {
            sys_assert(0);
        }
        ce_t* prev = &_ces[ce->entry2_prev];
        ce_t* next = &_ces[ce->entry2_next];

        prev->entry2_next = ce_index(next);
        next->entry2_prev = ce_index(prev);
    }
    ce->entry2_next = ce->entry2_prev = INV_U16;
}
#endif

//@brief check list is empry or not
static fast_code inline bool cache_list_empty(ce_t* list_head)
{
    return list_head == NULL;
}
fast_data_zi bool need_check;

fast_code bool cache_chk_lda_hit_trim(Host_Trim_Data * trim_data, lda_t lda)
{
    for(u32 i = 0; i < trim_data->Validcnt; ++i){
        lda_t eLDA = trim_data->Ranges[i].sLDA + trim_data->Ranges[i].Length - 1;
        if (lda >= trim_data->Ranges[i].sLDA && lda <= eLDA && trim_data->Ranges[i].Length) {
            return true;
        }
    }
    return false;
}


fast_code void cache_clear_trim_bit(Host_Trim_Data * trim_data)
{
    if (!need_check)
        return;
    u32 check = 0;
    need_check = false;
    for(u16 ce_idx = 0; ce_idx < cache_cnt; ++ce_idx){
        ce_t* ce = &_ces[ce_idx];
        if(ce->after_trim){
            check ++;
            need_check = true;
            if(cache_chk_lda_hit_trim(trim_data, ce->lda)){
                ce->after_trim = 0;
            }
        }
    }
}

ddr_code void ce_set_ce_list_trim_tag(ce_t* ce)
{
	//sys_assert(ce->state != CE_ST_INVALID);  nrm data in just set ce->state == INVALID , but don't clear cache list

	ce->before_trim = 1;
	//disp_apl_trace(LOG_ALW, 0x7df2,"[Before Trim]ce_id:%d state:%d lda:0x%x ce->prev:0x%x read_list:%d",ce_index(ce),ce->state,ce->lda,ce->ce_prev,ce->rph_head);
	while(ce->ce_prev != INV_U16)
	{
	    ce = ce_index_2_ce(ce->ce_prev);
		ce->before_trim = 1;
		//disp_apl_trace(LOG_ALW, 0x093d, "Prev ce_id:%d", ce_index(ce));
	}
}

ddr_code void cache_check_trim_hit_par_data(Host_Trim_Data * trim_data)
{
    // 1. check can list
    if(umgr->READER.can_cnt){
        ce_t** list_head = &umgr->READER.can_ls_head;
        ce_t *ce = *list_head;
        ce_t *ce_next;
        while(ce != NULL){
            ce_next = ce->entry2_next == INV_U16 ? NULL:ce_index_2_ce(ce->entry2_next);
            if(cache_chk_lda_hit_trim(trim_data, ce->lda)){
                //ce->before_trim = 1;
                ce_set_ce_list_trim_tag(ce);
                cache_entry_del(ce, &umgr->READER.can_ls_head, &umgr->READER.can_ls_tail);
                umgr->READER.can_cnt--;
                ce->state = CE_ST_WANT;
                READER(READER_IMT, ce);
            }
            ce = ce_next;
        }
    }
    // 2. check reader list
	int ofst = find_first_bit((void *)umgr->READER.lda_bmp, CACHE_READER_LDA_SZE);
    while(ofst < CACHE_READER_LDA_SZE){
        ce_t *ce = umgr->READER.ce[ofst];
        if(!ce->before_trim && cache_chk_lda_hit_trim(trim_data, ce->lda)){
            //ce->before_trim = 1;
			ce_set_ce_list_trim_tag(ce);
        }
        ofst = find_next_bit((void *)umgr->READER.lda_bmp, CACHE_READER_LDA_SZE, ofst+1);
    }
    // 3. check reader pending list
	if (!cache_list_empty(umgr->READER.w8ing_head)) {
        ce_t** list_head = &umgr->READER.w8ing_head;
        ce_t *ce = *list_head;
        ce_t *ce_next;
        while(ce != NULL){
            ce_next = ce->entry2_next == INV_U16 ? NULL:ce_index_2_ce(ce->entry2_next);
            if(!ce->before_trim && cache_chk_lda_hit_trim(trim_data, ce->lda)){
                //ce->before_trim = 1;
                ce_set_ce_list_trim_tag(ce);
            }
            ce = ce_next;
        }
    }
    // 4. check merger pending list
    if(umgr->MERGER.size){
        struct list_head *entry;
        list_for_each(entry, &umgr->MERGER.head) {
            merger_t *mrgr = container_of(entry, merger_t, entry);
            ce_t* ce = (ce_t*)(mrgr->ce);
            if(!ce->before_trim && cache_chk_lda_hit_trim(trim_data, ce->lda)){
                //ce->before_trim = 1;
                ce_set_ce_list_trim_tag(ce);
            }
        }
    }

    // 5. check merger merging
    if(umgr->MERGER.mergeing_size){
        // disp_apl_trace(LOG_ERR, 0x0216, "mergeing_size %u", umgr->MERGER.mergeing_size);
        struct list_head *entry;
        list_for_each(entry, &umgr->MERGER.head2) {
            merger_t *mrgr = container_of(entry, merger_t, entry);
            ce_t* ce = (ce_t*)(mrgr->ce);
            if(!ce->before_trim && cache_chk_lda_hit_trim(trim_data, ce->lda)){
                //ce->before_trim = 1;
                ce_set_ce_list_trim_tag(ce);
            }
        }
    }
}

/*!
 * @brief check lda is in cache or not
 *
 * @param lda:   input lda
 *
 * @return ce:  cache entry if cache hit, else return NULL
 */
fast_code ce_t* cache_search(u32 lda)
{
    u16 hash_code = ucache_hash(lda, CE_HLIST_MSK);
    u16 ce_idx = umgr->hash_table[hash_code];

    if (ce_idx == INV_U16) {
        return NULL;
    }
    sys_assert(ce_idx < cache_cnt);

    ce_t* ce = &_ces[ce_idx];

    while(1) {
        if (ce->lda == lda) {
            return ce;
        }

        if (ce->entry_next == INV_U16) {
            return NULL;
        }
        ce = &_ces[ce->entry_next];
    }

    return NULL;
}

fast_code ce_t* cache_cut(ce_t* ce)
{
    sys_assert(ce->ce_prev != INV_U16);
    sys_assert(ce->ce_prev < cache_cnt);

    ce_t* prev = ce_index_2_ce(ce->ce_prev);
    ce_t* next;
    if (ce->ce_next == INV_U16) {
        prev->ce_next = INV_U16;
        next = NULL;
    } else {
        next = ce_index_2_ce(ce->ce_next);
        prev->ce_next = ce->ce_next;
        next->ce_prev = ce_index(prev);
    }
    ce->ce_next = ce->ce_prev = INV_U16;
    return next;
}

/*!
 * @brief insert ce to cache hash list
 *
 * @param ce:   cache entry
 *
 * @return none
 */
extern volatile ftl_flags_t shr_ftl_flags;
fast_code void cache_insert(ce_t* ce)
{
    u16 hash_code = ucache_hash(ce->lda, CE_HLIST_MSK);
    //spin_lock_take(SPIN_LOCK_KEY_CACHE,0,true);
    //lock_ce_hash0 = hash_code;
    //while(lock_ce_hash1 == lock_ce_hash0);
    //spin_lock_release(SPIN_LOCK_KEY_CACHE);
    u16 idx = umgr->hash_table[hash_code];

    umgr->hash_table[hash_code] = ce_index(ce);
    if (idx == INV_U16) {  //hash table is empty
        ce->entry_next = INV_U16;
    } else {
        ce->entry_next = idx;
        _ces[idx].entry_prev = ce_index(ce);
    }
    ce->entry_prev = INV_U16;
    lock_ce_hash0 = INV_U16;
}

/*!
 * @brief delete ce from cache hash list
 *
 * @param ce:   cache entry
 *
 * @return none
 */
fast_code void cache_delete(ce_t* ce)
{
    sys_assert(ce_index(ce) < cache_cnt);

    u16 prev = ce->entry_prev;
    u16 next = ce->entry_next;
    u16 hash_code = ucache_hash(ce->lda, CE_HLIST_MSK);
    //spin_lock_take(SPIN_LOCK_KEY_CACHE,0,true);
    //lock_ce_hash0 = hash_code;
    //while(lock_ce_hash1 == lock_ce_hash0);
    //spin_lock_release(SPIN_LOCK_KEY_CACHE);
    if ((prev == INV_U16) && (next == INV_U16)) {
        sys_assert(umgr->hash_table[hash_code] == ce_index(ce));
        umgr->hash_table[hash_code] = INV_U16;
    } else if (prev == INV_U16) {   //head
        umgr->hash_table[hash_code] = next;
        _ces[next].entry_prev   = INV_U16;
    } else if
(next == INV_U16) {  //tail
        _ces[prev].entry_next   = INV_U16;
    } else {                       //middle
        _ces[prev].entry_next   = next;
        _ces[next].entry_prev   = prev;
    }

    ce->entry_prev = ce->entry_next = INV_U16;
    lock_ce_hash0 = INV_U16;
}

fast_code dtag_t ucache_free_smdtag(dtag_t dtag)
{
    dtag_t sdtag;
    sdtag.dtag = smdtag2sdtag(dtag.dtag);
    sys_assert(dtag.b.type_ctrl & BTN_SEMI_STREAMING_MODE);
    bm_free_semi_write_load(&sdtag, 1, 0);
    dtag.b.type_ctrl &= ~BTN_SEMI_MODE_MASK;
    dtag.dtag = smdtag2ddtag(dtag.dtag);
    return dtag;
}

fast_code void ucache_inc_ddtag(dtag_t dtag)
{
    if (dtag.b.type_ctrl & BTN_SEMI_STREAMING_MODE) {
        dtag_t ddtag;
        ddtag.dtag = smdtag2ddtag(dtag.dtag);
        rdisk_dref_inc(&ddtag.dtag, 1);
    }
    else {
        if (dtag.dtag != DDTAG_MASK) {
            rdisk_dref_inc(&dtag.dtag, 1);
        }
    }
}
extern u32 WUNC_DTAG;

fast_code void ucache_free_dtag(dtag_t dtag)
{
    if (is_semi_dtag(dtag)) {
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free sdtag:%d ddtag:%x", smdtag2sdtag(dtag.dtag),smdtag2ddtag(dtag.dtag));
		smdtag_recycle(dtag);
	} else {
		if ((dtag.dtag != DDTAG_MASK) && (dtag.dtag != EVTAG_ID) && (dtag.dtag != WUNC_DTAG)) {
            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ddtag:%x", dtag.dtag);
            dtag_put_ex(dtag);
		}
	}
}

fast_code bool SrchTrimCache(lda_t lda){  // check lda with trim flag in ucahe
    if (ucache_clean()){
        return false;
    }
    ce_t *ce = cache_search(lda);
    if (ce != NULL && ce->after_trim) {
        return true;
    }
    return false;
}

/*!
 * @brief commit dtag to be
 *
 * @param dtag_list	dtag list
 * @param lda_list	lda list
 * @param cnt		number to commit
 *
 * @return		not used
 */
fast_code static inline void rdisk_dtag_comt_new(ce_t* ce)
{
    dtag_t dtag = ce->dtag;
    lda_t lda = ce->lda;
    #if(BG_TRIM == ENABLE)
    if((bg_trim.flags.b.clean==0) && (!plp_trigger) && (!bg_trim.flags.b.abort))
    {
        #ifdef BG_TRIM_ON_TIME
        if(BgTrimUcacheCheck(lda)){
            dmb();
        }
        else{
            cache_entry_add(ce, &umgr->UPDATER.head, &umgr->UPDATER.tail, ADD_TO_TAIL);
            umgr->UPDATER.size++;
            evt_set_cs(evt_cache_force_flush, 0, 0, CS_TASK);
            return;
        }
        #else
        BgTrimUcacheCheck(lda);
        #endif
    }
    #endif
    u32 wptr = shr_dtag_comt.que.wptr;
	u32 size = shr_dtag_comt.que.size;
    u32 rdtag;
    u32 rptr = shr_dtag_comt.que.rptr;
//pre handle
    u32 wptr1 = wptr;
    wptr1++;
    if (wptr1 == size) {
        wptr1 = 0;
    }
    if (wptr1 == rptr) {
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "cmt full,pending,ce:%x, lda:%d size:%d", ce, ce->lda, umgr->UPDATER.size);
        //if commit this ce,this comt que is full,so we pending this ce in update list
        cache_entry_add(ce, &umgr->UPDATER.head, &umgr->UPDATER.tail, ADD_TO_TAIL);
        umgr->UPDATER.size++;
        evt_set_cs(evt_cache_force_flush, 0, 0, CS_TASK);
        return;
    }
    //if(IS_FUAorWUNC_CMD(ce->btag)){
        //dtag.b.type_ctrl |= FUA_TYPE_CTRL_BIT;
    //}
    sys_assert(cache_handle_dtag_cnt != 0);
    cache_handle_dtag_cnt--;  //dec here

    shr_dtag_comt.que.buf[wptr].dtag = dtag;
    shr_dtag_comt.lda[wptr] = lda;

    if (dtag.dtag != EVTAG_ID) {
        if (dtag.b.type_ctrl & BTN_SEMI_STREAMING_MODE)
			rdtag = smdtag2rdtag(dtag.dtag);
		else
			rdtag = dtag.b.dtag;

        _r_dtag2ces[rdtag] = ce_index(ce);

    } else {
        //disp_apl_trace(LOG_INFO, 0, "commit dtag:%x ce:%x", dtag.dtag, ce);
        //cache_delete(ce);
		//UCACHE_FREE_CE(ce);
        //disp_apl_trace(LOG_WUNC_INFO, 0, "WUNC commit dtag ce:%x", ce);
    }
    wptr++;
    if (wptr == size) {
        wptr = 0;
    }
    sys_assert(wptr != rptr);
    #if 0
    while (wptr == shr_dtag_comt.que.rptr) {
            sw_ipc_poll();
            cpu_msg_isr();
    //add pdone polling
            #if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_FDONE
            extern void btn_wr_grp0_pdone_handle(void);
            btn_wr_grp0_pdone_handle();//check pdone here to avoid cpu2 wait parity out timeout
            #endif
   }
   #endif
    shr_dtag_ins_cnt ++;
    dmb();
	shr_dtag_comt.que.wptr = wptr;
}

#if 0//(TRIM_SUPPORT == DISABLE)
static inline void rdisk_trim_info_dtag_comt(req_t *req)
{
	/* trim info commit */
	u32 wptr = shr_dtag_comt.que.wptr;
	u32 size = shr_dtag_comt.que.size;
	dtag_t dtag = mem2dtag(req->op_fields.trim.dsmr);

	shr_dtag_comt.que.buf[wptr].dtag = dtag;
	shr_dtag_comt.lda[wptr] = TRIM_LDA;

	if (++wptr == size)
		wptr = 0;

	shr_dtag_ins_cnt += 1;
	shr_dtag_comt.que.wptr = wptr;
}
inline static void ucache_range_clean_chk(void)
{
	if (list_empty(&umgr->range.req) &&
			(umgr->range.wnum == 0) &&
			(umgr->range.fnum == 0))
		umgr->range.flags.b.clean = true;
}
fast_code void rdisk_trim_info_done(req_t *req)
{
	dtag_t dtag = mem2dtag(req->op_fields.trim.dsmr);

	if (req->completion) {
		disp_apl_trace(LOG_DEBUG, 0x3d0b, "rdisk finish trim dtag(%d)", dtag.b.dtag);
		req->completion(req);
	}

	ftl_trim_t *ftl_trim = (ftl_trim_t *)req->op_fields.trim.dsmr;
	list_del_init(&ftl_trim->entry);
	dtag_put(DTAG_T_SRAM, dtag);

	ucache_range_clean_chk();
}
#endif
/*!
 * @brief increase remote dtag reference count
 *
 * @param dtag_list	dtag list
 * @param cnt		number to commit
 *
 * @return		not used
 */
fast_code void rdisk_dref_inc(u32 *dtag_list, u32 cnt)
{
#if (CPU_ID != CPU_DTAG)
	u32 i;
	u32 wptr = shr_dref_inc.wptr;
	u32 size = shr_dref_inc.size;

	for (i = 0; i < cnt; i++) {
		shr_dref_inc.buf[wptr] = dtag_list[i];
		if (++wptr == size)
			wptr = 0;
        while (wptr == shr_dref_inc.rptr);
	}

	shr_dref_inc.wptr = wptr;
#else
	u32 i;
	for (i = 0; i < cnt; i++) {
		dtag_t dtag = { .dtag = dtag_list[i]};
		dtag_ref_inc_ex(dtag);
	}
#endif
}

#if (CPU_ID != CPU_DTAG)
/*!
 * @brief decrease remote dtag reference count
 *
 * @param dtag_list	dtag list
 * @param cnt		number to commit
 *
 * @return		not used
 */
fast_code void rdisk_dref_dec(u32 *dtag_list, u32 cnt)
{
	u32 i;
	u32 wptr = shr_dref_dec.wptr;
	u32 size = shr_dref_dec.size;

	for (i = 0; i < cnt; i++) {
		shr_dref_dec.buf[wptr] = dtag_list[i];
		if (++wptr == size)
			wptr = 0;
        while (wptr == shr_dref_dec.rptr);
	}

	shr_dref_dec.wptr = wptr;
}
#endif

/*!
 * @brief bulk handle for datg commit
 *
 * @param force		force all ce update
 *
 * @return		not used
 */
static fast_code void ucache_l2p_update_cmmt(ce_t* ce)
{
    #ifdef LJ1_WUNC
    if (ce->wunc_bitmap && (ce->dtag.dtag != EVTAG_ID)) {
        ce->btag |= WUNC_BTAG_TAG;
        disp_apl_trace(LOG_INFO, 0xc9bd, "wunc write ce:%x lda:%x bit:%x", ce, ce->lda, ce->wunc_bitmap);
        set_pdu_bmp(ce->dtag, ce->wunc_bitmap);
    }
    #endif
    if (!CR_CHECK_FREE(ce->rph_head)) {
        READER(READER_RADJ, ce);
    }
    ce->state = CE_ST_L2UP;
    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "commit lda:%x ce:%x", ce->lda, ce);

    if (ce->dtag.dtag == DDTAG_MASK) {
        //dtag is invalid, do not commit to cpu2
        sys_assert_RD(0);
		//list_del_init(&ce->entry);
		//che_delete(ce);
		//ACHE_FREE_CE(ce);
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
        //turn;
    }
    //do not cache dtag in cpu1, just commit dtag to cpu2
	rdisk_dtag_comt_new(ce);
}

fast_code bool ucache_clean(void)
{
    #if 0//(TRIM_SUPPORT == DISABLE)
    bool ret = (umgr->ce_pool.avail == umgr->ce_pool.size) && (umgr->range.flags.b.clean);
    #else
	bool ret = (umgr->ce_pool.avail == umgr->ce_pool.size);
    #endif
	return ret;
}

static fast_code cache_read_t* cache_read_ptr_get(u16 cr_idx)
{
    u32 base = (u32)(&__btcm_free_start);
    if (MEM_CR_CHECK(cr_idx)) {
        return (cache_read_t*)(base + (cr_idx & CR_IDX_MASK) * sizeof(cache_read_t));
    }
    else {
        return &crt[cr_idx];
    }
}

static fast_code u16 cache_read_idx_get(cache_read_t* cr)
{
    u32* base = (u32*)(&__btcm_free_start);

    if (((u32)cr >= (u32)(&crt[0])) && ((u32)cr <= (u32)(&crt[CRT_CNT - 1]))) {
        return crt_index(cr);
    }
    else {
        u16 index = ((u32*)cr - base)/sizeof(cache_read_t);
        sys_assert(index <= CR_IDX_MASK);
        return index | MEM_CR_MASK;
    }
}


static fast_code inline cache_read_t* cache_read_entry_get(u8 btag, u8 du_ofst)
{
    cache_read_t* cr = pool_get_ex(&crt_pool.pool);
    if (cr == NULL) {
        cr = sys_malloc(FAST_DATA, sizeof(cache_read_t));
        //disp_apl_trace(LOG_INFO, 0, "cr used up, allocate cr:%x", cr);
        sys_assert(cr != NULL);
    }
    else {
        crt_pool.avail--;
    }
    //sys_assert(cr != NULL);


    cr->btag = btag;
    cr->du_ofst = du_ofst;
    cr->next = INV_U16;

    return cr;
}

static fast_code inline void cache_read_entry_put(cache_read_t* cr)
{
    if (((u32)cr >= (u32)(&crt[0])) && ((u32)cr <= (u32)(&crt[CRT_CNT - 1]))) {
        pool_put_ex(&crt_pool.pool, (void *) cr);
        crt_pool.avail++;
    }
    else {
        disp_apl_trace(LOG_INFO, 0x2569, "cr used up, free cr:%x", cr);
        sys_assert((u32)cr >= (u32)&__btcm_free_start);
        sys_assert((u32)cr < (u32)&__btcm_free_end);
        sys_free(FAST_DATA,cr);
    }
}

static inline fast_code bool ucache_read_issue(ce_t *ce, dtag_t rdtag)
{
	int ofst = find_first_zero_bit((void *)umgr->READER.lda_bmp, CACHE_READER_LDA_SZE);
	if (ofst == CACHE_READER_LDA_SZE)
		return false;
	set_bit(ofst, (void *) umgr->READER.lda_bmp);

	ce->state = CE_ST_READ;
	/* Otherwise the CE could be recycled during the READ */
    //ce->ref_cnt++;

	umgr->READER.ce[ofst] = ce;
	umgr->READER.dtags[ofst] = rdtag;
	umgr->READER.rs_cnt++;
	/* scheduler_l2p_srch_done */
	l2p_single_srch(ce->lda, ofst, ua_btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
	return true;
}
static fast_code void put_pend_dtag_func(u32 dtag)
{
	sys_assert(!list_empty(&pend_dtag_mgr->entry2));
	sys_assert(pend_dtag_mgr->free_cnt);

	dtag_pend* dtag_pending = list_first_entry(&pend_dtag_mgr->entry2, dtag_pend, pending);
	list_del_init(&dtag_pending->pending);
	list_add_tail(&dtag_pending->pending, &pend_dtag_mgr->entry1);

	dtag_pending->pl = dtag;
	//disp_apl_trace(LOG_INFO, 0, "put dtag(%d) ,index(%d)", dtag, dtag_pending->index);

	pend_dtag_mgr->availa_cnt++;
	pend_dtag_mgr->free_cnt--;
}

/*!
* @brief trigger waiting read
*
* @param ctx		not used
*
* @return		not used
*/
static fast_code void ucache_read_cont(void *ctx)
{
	if (cache_list_empty(umgr->READER.w8ing_head)) {
_pend:
		if(pend_dtag_mgr->free_cnt && !cache_list_empty(umgr->READER.w8ing_head))
		{
			put_pend_dtag_func((u32)ctx);
		}
		else
			cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_DTAG_PUT, 0, (u32)ctx);
		return;
	}
    merger_t *mrgr;
	ce_t *ce = umgr->READER.w8ing_head;
	dtag_t rdtag = {.dtag = (u32)ctx};
    // disp_apl_trace(LOG_UCACHE_DEBUG, 0x168c, "LDA(%x) ce->state (%x) dtag:%x", ce->lda, ce->state, rdtag.dtag);

    switch (ce->state) {
        case CE_ST_WANT:  //most case, get dtag start read modify write
            break;
        case CE_ST_INVALID: //current ce is invalid
            mrgr = mgr_index_2_mgr(ce->mrgr_indx);
            UCACHE_FREE_MRGR(mrgr);
            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr);
            //ucache_free_dtag(ce->dtag);
            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
            cache_entry_del(ce, &umgr->READER.w8ing_head, &umgr->READER.w8ing_tail);

            UCACHE_FREE_CE_and_DTAG(ce,false);
            goto _pend;
            break;
        default:
            sys_assert(0);
            break;
    }

	if (ucache_read_issue(ce, rdtag) == true) {
		cache_entry_del(ce, &umgr->READER.w8ing_head, &umgr->READER.w8ing_tail);
	} else {
		ucache_read_pend_cnt++;
		goto _pend;
	}
}

static inline fast_code dtag_t ucache_read_dtag_get(ce_t *ce)
{
	dtag_t rdtag;

	if (pend_dtag_mgr->availa_cnt > dtag_evt_trigger_cnt) {
		evt_set_cs(evt_get_pend_dtag, 0, 0, CS_TASK);
		dtag_evt_trigger_cnt++;
	} else if((remote_dtag_wait + pend_dtag_mgr->availa_cnt) < CACHE_READER_LDA_SZE){
		ipc_api_remote_dtag_get((u32 *) NULL, false, RDISK_PAR_DTAG_TYPE);
		remote_dtag_wait++;
	}else{
		ucache_read_pend_cnt++;
	}

	cache_entry_add(ce, &umgr->READER.w8ing_head, &umgr->READER.w8ing_tail, ADD_TO_TAIL);
	rdtag.dtag = DTAG_INV;

	return rdtag;
}

static fast_code void READER(read_op_t type, ce_t *ce)
{
	switch (type) {
	case READER_CAN:
        umgr->READER.can_cnt++;
		cache_entry_add(ce, &umgr->READER.can_ls_head, &umgr->READER.can_ls_tail, ADD_TO_TAIL);
        if (umgr->READER.can_cnt <= 32) {
		    break;
        }
	case READER_CAN_FLUSH:
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "can cnt:%d start can flush", umgr->READER.can_cnt);
		{
            while (cache_list_empty(umgr->READER.can_ls_head) == false) {
                ce = umgr->READER.can_ls_head;
                //cache_entry_del(ce, &umgr->READER.can_ls_head, &umgr->READER.can_ls_tail, TYPE_ENTRY_LIST);
                cache_entry_del(ce, &umgr->READER.can_ls_head, &umgr->READER.can_ls_tail);
                umgr->READER.can_cnt--;
                ce->state = CE_ST_WANT;
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "read imt ce:%x lda:%x dtag:%x s:%x l:%d", ce, ce->lda, ce->dtag.dtag, ce->ofst, ce->nlba);
                READER(READER_IMT, ce);
            }
		}
		break;
	case READER_IMT:
	{
		dtag_t rdtag = ucache_read_dtag_get(ce);

		if (rdtag.dtag != DTAG_INV)
			ucache_read_issue(ce, rdtag);
		break;
	}
	case READER_RADJ:
        if (!CR_CHECK_FREE(ce->rph_head)) {
            cache_read_t* cr = cache_read_ptr_get(ce->rph_head);
            cache_read_t* cr_next = NULL;
            while(cr != NULL) {
                dtag_t _dtag;
                if (ce->dtag.b.type_ctrl & BTN_SEMI_STREAMING_MODE)
                    _dtag.dtag = smdtag2ddtag(ce->dtag.dtag);
                else
                    _dtag.dtag = ce->dtag.dtag;
                if(_dtag.dtag == EVTAG_ID){
                    //_dtag.dtag = EVTAG_ID;
                }
                else if (IS_WUNC_CACHE(ce->btag)) {
                    u8 lba_num;
                    if (host_sec_bitz == 9) //joe add sec size 20200818
                		lba_num = 8;
                	else
                		lba_num = 1;
                    u32 dtagbak = _dtag.dtag;
                    _dtag.dtag = EVTAG_ID;

                    btn_cmd_t *bcmd = btag2bcmd(cr->btag);
                    u64 slba = bcmd_get_slba(bcmd);
                    u16 len = bcmd->dw3.b.xfer_lba_num;
                    lda_t lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz));
                    lda_t lda_tail = ((slba + len) >> (LDA_SIZE_SHIFT - host_sec_bitz));
                    u8 head = slba & (lba_num - 1);
                    u8 tail = (slba + len) & (lba_num - 1);
                    if (head && tail && (lda == lda_tail))
                        tail = 0;
                    if ((lda == ce->lda)&&(head)){
                        u32 cnt = min(lba_num - head, len);
                        u8 headbitmap = (((1 << cnt) - 1)<< head);
                        //printk("reader headbitmap %x,cnt %x head %x\n",headbitmap,cnt,head);
                        if(!(ce->wunc_bitmap & headbitmap)){
                            _dtag.dtag = dtagbak;
                            sys_assert(_dtag.b.dtag < DDR_DTAG_CNT);  // dtag must be valid dtag
                            dtag_ref_inc_ex(_dtag);
                        }
                    }
                    if((tail)&&(lda_tail == ce->lda)){
                        u8 tailbitmap = ((1 << (tail)) - 1);
                        //printk("reader tailbitmap %x,tail %x\n",tailbitmap,tail);
                        if(!(ce->wunc_bitmap & tailbitmap)) {
                            _dtag.dtag = dtagbak;
                            sys_assert(_dtag.b.dtag < DDR_DTAG_CNT);  // dtag must be valid dtag
                            dtag_ref_inc_ex(_dtag);
                        }
                    }
                }
                else if (_dtag.dtag == DDTAG_MASK){
                    _dtag.dtag = RVTAG_ID;  //impossible now, no mapping read
                }
                else {
                    sys_assert(_dtag.b.dtag < DDR_DTAG_CNT);  // dtag must be valid dtag
                    dtag_ref_inc_ex(_dtag);
                }

                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "read peding done,lda:%x ofst:%d btag:%d, ce:%x", ce->lda, cr->du_ofst,cr->btag, ce);

				bm_rd_dtag_commit(cr->du_ofst, cr->btag, _dtag);

                if (CR_CHECK_FREE(cr->next)) {
                    cr_next = NULL;
                } else {
                    cr_next = cache_read_ptr_get(cr->next);
                }
                cache_read_entry_put(cr);
                cr = cr_next;

            }
            ce->rph_head = INV_U16;
        }
		break;
    default:
        sys_assert(0);
        break;
	}
}

fast_code void ucache_chk_fua_list(lda_t lda)
{	
	ce_t* cur = umgr->FUALIST.head;
	ce_t* ce_next = NULL ;

	do{
		if(cur->entry2_next != INV_U16)
			ce_next = ce_index_2_ce(cur->entry2_next);
		else
			ce_next = NULL;

		if(cur->lda == lda)
		{
			//disp_apl_trace(LOG_ALW, 0x46a9,"FUA cmd 0x%x can free!! lda:0x%x dtag:0x%x",cur->btag,lda,cur->dtag.dtag);

			cache_entry_del(cur, &umgr->FUALIST.head, &umgr->FUALIST.tail);
			umgr->FUALIST.size--;
			//ucache_free_dtag(cur->dtag);//dec 1
			UCACHE_FREE_CE_and_DTAG(cur, true);
			//break;
		}
		cur = ce_next;
	}while(cur != NULL);
}

fast_code void ucache_pda_updt_ce_handle(ce_t* ce)
{
    sys_assert(ce->nrm == 1);
	ce_t* pre_dirty_ce;
	switch(ce->state) {
	case CE_ST_L2UP:  //normal case, ce status not change
        cache_delete(ce);
        if(umgr->FUALIST.size)
        {
			ucache_chk_fua_list(ce->lda);
        }
        UCACHE_FREE_CE(ce);
        //disp_apl_trace(LOG_INFO, 0, "free ce:%x lda:%x", ce, ce->lda);
        break;
    case CE_ST_INVALID:
        //ce status invalid,  two case
        //1,nrm in set this ce invalid, after cur ce prog done, need check pre dirty ce,to continue commit
        //2,par in set this ce invalid, cur dtag is used to do merge, then do nothing
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "lda:%x ce:%x update done invaid", ce->lda, ce);
        pre_dirty_ce = ucache_get_pre_ce(ce, CE_ST_DIRTY);
        UCACHE_FREE_CE(ce);
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
        //sys_assert(pre_dirty_ce != NULL);
        //add to update list, we handle this ce in evt task to avoid sw_ipc_poll dead loop
        if (pre_dirty_ce != NULL) {
            sys_assert(IS_WUNC_CACHE(pre_dirty_ce->btag));
            cache_cut(ce);
            pre_dirty_ce->state = CE_ST_L2UP;
            cache_entry_add(pre_dirty_ce, &umgr->UPDATER.head, &umgr->UPDATER.tail, ADD_TO_TAIL);
            umgr->UPDATER.size++;
            evt_set_cs(evt_cache_force_flush, 0, 0, CS_TASK);
        }
		break;
	default:
		sys_assert(0);
	}
}
fast_code void ucache_pda_updt_done(u32 did)
{
	dtag_t dtag = { .dtag = did };
	u32 rdtag = dtag.b.dtag;

    //sys_assert(_r_dtag2ces[rdtag] != 0xFFFF);
    sys_assert(_r_dtag2ces[rdtag] < cache_cnt);
    ce_t *ce = &_ces[_r_dtag2ces[rdtag]];
    cache_handle_dtag_cnt++; //inc 1 because UCACHE_FREE_CE will dec 1
    ucache_pda_updt_ce_handle(ce);
}
#ifdef LJ1_WUNC
share_data_zi wunc_t WUNC_lda2ce;
#define ABT_BIT (0x8000)
#define CE_MASK (ABT_BIT-1)

fast_code bool insert_wunc_lda2ce(ce_t* ce)
{
    sys_assert(IS_WUNC_CACHE(ce->btag));
    lda_t lda = ce->lda;
    u32 idx = 0;
    if((lda >= WUNC_lda2ce.startlda0) && (lda <= WUNC_lda2ce.endlda0)){
        idx = lda - WUNC_lda2ce.startlda0;
        sys_assert(idx < WUNC_MAX_CACHE);
        WUNC_lda2ce.ceidxs[idx] = ce_index(ce);
        return true;
    }
    if(WUNC_lda2ce.cross_cnt){
        if((lda >= WUNC_lda2ce.startlda1) && (lda <= WUNC_lda2ce.endlda1)){
            idx = lda - WUNC_lda2ce.startlda1 + WUNC_lda2ce.cross_cnt;
            sys_assert(idx < WUNC_MAX_CACHE);
            WUNC_lda2ce.ceidxs[idx] = ce_index(ce);
            return true;
        }
    }
    return false;
}

slow_code static inline u16 get_wunc_lda2ce(u32 did)
{

    lda_t lda = (did & 0x7FFFFFFF);
    u16 idx = 0;
    u16 ce_idx = INV_U16;
    if((lda >= WUNC_lda2ce.startlda0) && (lda <= WUNC_lda2ce.endlda0)){
        idx = lda - WUNC_lda2ce.startlda0;
        sys_assert(idx < WUNC_MAX_CACHE);
        ce_idx = WUNC_lda2ce.ceidxs[idx];
        WUNC_lda2ce.ceidxs[idx] = WUNC_lda2ce.ceidxs[idx] & CE_MASK;
        return ce_idx;
    }
    if(WUNC_lda2ce.cross_cnt){
        if((lda >= WUNC_lda2ce.startlda1) && (lda <= WUNC_lda2ce.endlda1)){
            idx = lda - WUNC_lda2ce.startlda1 + WUNC_lda2ce.cross_cnt;
            sys_assert(idx < WUNC_MAX_CACHE);
            ce_idx = WUNC_lda2ce.ceidxs[idx];
            WUNC_lda2ce.ceidxs[idx] = WUNC_lda2ce.ceidxs[idx] & CE_MASK;
            return ce_idx;
        }
    }
    return ce_idx;
}

slow_code void wunc_handle_done(u32 did)
{
    u16 ce_idx = get_wunc_lda2ce(did);
    sys_assert(ce_idx != INV_U16);
    ce_t *ce = ce_index_2_ce(ce_idx&CE_MASK);
    sys_assert(IS_WUNC_CACHE(ce->btag));
    cache_handle_dtag_cnt++; //inc 1 because UCACHE_FREE_CE will dec 1
    if(ce_idx & ABT_BIT){
        cache_entry_add(ce, &umgr->UPDATER.head, &umgr->UPDATER.tail, ADD_TO_TAIL);
        umgr->UPDATER.size++;
        evt_set_cs(evt_cache_force_flush, 0, 0, CS_TASK);
    }
    else{
        ucache_pda_updt_ce_handle(ce);
    }
}
#endif
fast_code void ucache_pda_updt_abort(u32 did)
{
	dtag_t dtag = { .dtag = did };
	u32 rdtag = dtag.b.dtag;

    sys_assert(_r_dtag2ces[rdtag] != 0xFFFF);
    ce_t *ce = &_ces[_r_dtag2ces[rdtag]];

	// ISU, LJ1-195, PgFalReWrMisComp
	if (is_semi_dtag(ce->dtag)) {
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free sdtag:%d", smdtag2sdtag(ce->dtag.dtag));
		ce->dtag.b.type_ctrl &= ~BTN_SEMI_MODE_MASK;	// Use ddtag, apply NORMAL_MODE = 0
        ce->dtag.dtag = smdtag2ddtag(ce->dtag.dtag);
    }
    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "cmt full,pending,ce:%x, lda:%d size:%d", ce, ce->lda, umgr->UPDATER.size);
    //if commit this ce,this comt que is full,so we pending this ce in update list
    cache_entry_add(ce, &umgr->UPDATER.head, &umgr->UPDATER.tail, ADD_TO_TAIL);
    umgr->UPDATER.size++;
    evt_set_cs(evt_cache_force_flush, 0, 0, CS_TASK);
    cache_handle_dtag_cnt++;
    disp_apl_trace(LOG_INFO, 0x5ded, "Re-write Will return");

}
#ifdef LJ1_WUNC
extern lda_t wunc_ua[2];
extern u8 wunc_bmp[2];
/*from ce handle wunc cache until ce_prev == invalid or prev ce is not wunc ce
return NULL if ce_prev == invalid or the prev not wunc ce*/
static fast_code  ce_t * ucache_WUNC_handle(ce_t* ce, u8 pdu_bmp,dtag_t dtag)
{
    u8 lbanum;
    if(host_sec_bitz==9)//joe add sec size 20200818
		lbanum=8;
	else
		lbanum=1;

    sys_assert(ce->nrm == 0);
    merger_t *mrgr = mgr_index_2_mgr(ce->mrgr_indx);
    mrgr->wunc_bitmap = mrgr->wunc_bitmap|pdu_bmp;
    mrgr->src_lba_s = 0;
    mrgr->src_nlba = lbanum;
    pdu_bmp = mrgr->wunc_bitmap;

    if (!IS_WUNC_CACHE(ce->btag)) {  //current ce is not wunc, do not check prev, just start merge
        if (mrgr->wunc_bitmap) {
            mrgr->wunc_bitmap = NON_WUNC_BIT_SET(ce->nlba, ce->ofst, mrgr->wunc_bitmap);//(~(((1 << ce->nlba) - 1) << ce->ofst)) & mrgr->wunc_bitmap;  //clear
        }
        return ce;
    }
    //get prev

#ifdef While_break
	u64 start = get_tsc_64();
#endif

    while (ce->ce_prev != INV_U16) {
        ce_t* prev = ce_index_2_ce(ce->ce_prev);
        merger_t *prev_mrgr = mgr_index_2_mgr(prev->mrgr_indx);


        prev_mrgr->wunc_bitmap |= pdu_bmp;
        pdu_bmp = prev_mrgr->wunc_bitmap;
        prev_mrgr->src_lba_s = 0;
        prev_mrgr->src_nlba = lbanum;

        sys_assert(IS_WUNC_CACHE(ce->btag));
        cache_cut(ce);
        UCACHE_FREE_MRGR(mrgr);
        UCACHE_FREE_CE(ce);

        if (!IS_WUNC_CACHE(prev->btag)) {
            ce = prev;
            mrgr = prev_mrgr;
            break;
        }

        ce = prev;
        mrgr = prev_mrgr;

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
    }

    if (IS_WUNC_CACHE(ce->btag)) {
        ce->nrm = 1;
        ce->wunc_bitmap = mrgr->wunc_bitmap;
        UCACHE_FREE_MRGR(mrgr);

        if (dtag.dtag == EVTAG_ID) {
            sys_assert(ce->dtag.b.dtag == dtag.b.dtag);
            u8 wunc_ua_cnt;
            for(wunc_ua_cnt = 0; wunc_ua_cnt<2; wunc_ua_cnt++){
                if(ce->lda == wunc_ua[wunc_ua_cnt])
                {
                    wunc_bmp[wunc_ua_cnt] = ce->wunc_bitmap;
                }
            }
        }
        else {
            ce->dtag = dtag;
        }
        ucache_l2p_update_cmmt(ce);
        ce = NULL;
    }
    else {
        if (mrgr->wunc_bitmap) {
            mrgr->wunc_bitmap = NON_WUNC_BIT_SET(ce->nlba, ce->ofst, mrgr->wunc_bitmap);//(~(((1 << ce->nlba) - 1) << ce->ofst)) & mrgr->wunc_bitmap;  //clear
        }
    }

    return ce;
}
#endif

ddr_code void ucache_free_trim_nrm_ce(ce_t* ce,u8 method)
{
    sys_assert(ce->ce_prev == INV_U16);
    sys_assert(ce->ce_next == INV_U16);

    disp_apl_trace(LOG_INFO, 0xf0b9, "lda:0x%x ce_state:%d read_list:%d method:%d",ce->lda,ce->state,ce->rph_head,method);

    if (!CR_CHECK_FREE(ce->rph_head)) {
        READER(READER_RADJ, ce);
    }
    cache_delete(ce);
    ucache_free_dtag(ce->dtag);
    UCACHE_FREE_CE(ce);
	if(umgr->FUALIST.size)
	{
		ucache_chk_fua_list(ce->lda);
	}

}

/*!
* @brief release partial cache which masked trim
*
*
* @return		not used
*/
ddr_code void ucache_free_trim_partial_ce(ce_t* ce , merger_t* mrgr ,u8 lbanum , u8 method)
{
	/*
		if prev ce exist
		1. if prev ce normal  , it will set cur ce invalid , just free source
		2. if prev ce partial , it will wait cur ce merge done , then trigger merge again
		   now we want to free cur ce and prev ce,  there are some detail need consider
		   1) hash list			---if prev ce no exist,call cache_delete()
		   2) cache_list		---if prev ce exist,call cache_cut() before cur ce free done
		   3) ce resource		---ce and ce->dtag free
		   4) merge resource	---merge and merge dtag(merge dtag may no exist if ummap read data in)
		   5) ce state			---prev partial ce state must be HOLD unless 4K nrm data in
	*/
	ce_t* prev = NULL;
	ce_t* temp = NULL;
	merger_t* pre_mrgr = NULL;
	if(ce->state != CE_ST_INVALID)	//chk prev partial ce
	{
		if(ce->ce_prev != INV_U16)
		{
			prev = ce_index_2_ce(ce->ce_prev);
			cache_cut(ce);		//remove cur ce from ce_list
		}
		else
		{
			cache_delete(ce); //remove cur ce from hash_list
		}
	}
	else
	{
		// nrm data in will set cur ce invalid , just free ce and dtag
	}

// 	disp_apl_trace(LOG_INFO, 0x73f8, "lda:0x%x ce_state:%d ce->prev:%d method:%d",ce->lda,ce->state,ce->ce_prev,method);
    if(method != 1){
        ucache_free_dtag(mrgr->dtag);//unmap read no need free mrgr dtag!!!
        }
	UCACHE_FREE_MRGR(mrgr);
	ucache_free_dtag(ce->dtag);
	UCACHE_FREE_CE(ce);

	while(prev != NULL)
	{
// 		disp_apl_trace(LOG_INFO, 0x2181, "[Prev_ce]  ce_state:%d ce->prev:%d",prev->state,prev->ce_prev);
		sys_assert(prev->nrm == false); 	//must be partial
		sys_assert(prev->after_trim == 0); //if after_trim is True,cur ce and prev ce will be invalid
		switch(prev->state)
		{
			case CE_ST_HOLD:
				//prev->state = CE_ST_INVALID;
				if(prev->ce_prev != INV_U16)
				{
					temp = ce_index_2_ce(prev->ce_prev);
					cache_cut(prev);
				}
				else
				{
					temp = NULL;
					cache_delete(prev);
				}

				pre_mrgr = mgr_index_2_mgr(prev->mrgr_indx);
				UCACHE_FREE_MRGR(pre_mrgr);
				ucache_free_dtag(prev->dtag);
				UCACHE_FREE_CE(prev);
				prev = temp;
			break;
			default:
			//all prev partial ce state must be HOLD!!! , par_data_in_hit_par()
// 			disp_apl_trace(LOG_WARNING, 0x1bd5, "pre_ce state not hold!! ce_state:%d ce->prev:%d ce->trim:%d",prev->state,prev->ce_prev,prev->before_trim);
            if(!prev->after_trim){
			    prev->before_trim = 1;//force set true
            }
			return;
			//sys_assert(0);
			break;
		}
	}
}

/*!
* @brief merge done handle
*
* @param ctx		pointer to mslice
* @param rst		merge result
*
* @return		not used
*/
static fast_code int ucache_merge_done(void *ctx, dpe_rst_t *rst)
{
    u8 lbanum;
    if(host_sec_bitz==9)//joe add sec size 20200818
		lbanum=8;
	else
		lbanum=1;
	merger_t *mrgr = ctx;
	ce_t *ce = (ce_t*)mrgr->ce;

	sys_assert(rst->error == 0);
	sys_assert(ce);

    list_del_init(&mrgr->entry);
    umgr->MERGER.mergeing_size--;
    //1, handle mrgr slice done, and check mrgr merge done
    //mrgr->lba_s;
    //mrgr->nlba;
    if (mrgr->lba_s == 0) {
        sys_assert(ce->ofst != 0);
        ce->ofst = 0;
        ce->nlba += mrgr->nlba;
    }
    else {
        sys_assert(ce->ofst == 0);
        ce->nlba += mrgr->nlba;
    }
    #ifdef LJ1_WUNC
    u8 wunc_bitmap = mrgr->wunc_bitmap;
    sys_assert(!IS_WUNC_CACHE(ce->btag));
    #endif

	/*
	if(ce->before_trim)
	{
// 	    disp_apl_trace(LOG_INFO, 0xddd6, "merge done,ce:%x lda:%x s:%d l:%d state:%d", ce, ce->lda, ce->ofst, ce->nlba, ce->state);
        ucache_free_trim_partial_ce(ce,mrgr,lbanum,2);
		goto MERGE_DONE;
	}
	*/
    if (ce->ofst + ce->nlba != lbanum) {  //concat or merge not done, need handle
        switch (ce->state) {
            case CE_ST_INVALID:     //new nvm data in, current ce was set invalid
                ucache_free_dtag(mrgr->dtag);
                UCACHE_FREE_MRGR(mrgr);
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr);
                //free dtag
                //ucache_free_dtag(ce->dtag);
                UCACHE_FREE_CE_and_DTAG(ce,false);
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
                //umgr->slice_cnt--;
                break;
            case CE_ST_CONCAT:
                ucache_free_dtag(mrgr->dtag);
                ce->state = CE_ST_START;
                //check prev cache
                READER(READER_CAN, ce);
                 if (ce->ce_prev != INV_U16){
                    ce_t* prev = ce_index_2_ce(ce->ce_prev);
                    sys_assert(prev->nrm == false);
                    par_data_in_hit_par(prev, ce);
                }
                else if (!CR_CHECK_FREE(ce->rph_head)) {
                    READER(READER_CAN_FLUSH, NULL);
                }
                break;
            case CE_ST_MERGE:
                MERGER(MERGER_TRIGGER, ce);
                break;
            default:
                sys_assert(0);
                break;
        }

    }
    else {
        ucache_free_dtag(mrgr->dtag);
        UCACHE_FREE_MRGR(mrgr);
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr);
        //umgr->slice_cnt--;
        sys_assert(umgr->ce_pool.avail < cache_cnt);
        ce->nrm = 1;
        //merge done, continue task
        switch(ce->state){
        case CE_ST_INVALID://3.invalid ce status return ce
            sys_assert(ce->ce_next == INV_U16);
            sys_assert(ce->ce_prev == INV_U16);
            //ucache_free_dtag(ce->dtag);
            UCACHE_FREE_CE_and_DTAG(ce,false);
            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
            break;
        case CE_ST_MERGE:
            //4. data ce become nrm
            //5. get pre ce
            if(ce->ce_prev != INV_U16)
            {
                ce_t* prev = ce_index_2_ce(ce->ce_prev);
                ucache_inc_ddtag(ce->dtag);
                cache_cut(ce);
                #ifdef LJ1_WUNC
                sys_assert(!IS_WUNC_CACHE(ce->btag));
                prev = ucache_WUNC_handle(prev, wunc_bitmap, ce->dtag);
                #endif
                if(prev){
                    merger_t* mrgr_prev;
                    switch(prev->state){
                        case CE_ST_HOLD:
                            mrgr_prev = mgr_index_2_mgr(prev->mrgr_indx);
                            prev->state = CE_ST_MERGE;
                            sys_assert(is_semi_dtag(ce->dtag) == false);
                            mrgr_prev->dtag = ce->dtag;

                            mrgr_prev->src_lba_s = 0;        //nrm data, all lba is valid
                            mrgr_prev->src_nlba = lbanum;

                            //ucache_free_dtag(ce->dtag);
                            UCACHE_FREE_CE_and_DTAG(ce,false);
                            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
                            MERGER(MERGER_TRIGGER,prev);
                            break;
                        default:
                            sys_assert(0);
                            break;
                    }
                }
                else {
                    //ucache_free_dtag(ce->dtag);
                    UCACHE_FREE_CE_and_DTAG(ce,false);
                }
            }
            else //par data -> nrm data, process mrn data in
            {
                ce->state = CE_ST_DIRTY;
                #ifdef LJ1_WUNC
                if(wunc_bitmap){
                    ce->wunc_bitmap = wunc_bitmap;
                }
                else
                #endif
                {
                    ce->wunc_bitmap = 0;
                }
                if(ce->before_trim)
                {
					ucache_free_trim_nrm_ce(ce,2);
                }
                else
                {
               		nrm_data_in_handle(ce);
                }
            }
            break;
         default :
            sys_assert(0);
            break;
        }

    }
//MERGE_DONE:
    //2, checm MERGER list
    if (umgr->MERGER.size) {
        MERGER(MERGER_TRIGGER_FORCE, NULL);
    }


	return 0;
}

/*!
* @brief concat done handle
*
* @param _ctx		pointer to mslice
* @param rst		concat result
*
* @return		not used
*/
static fast_code void ucache_merge(merger_t* mrgr, dtag_t dst, u8 start, u8 nlba, dpe_cb_t callback)
{
    MERGER_t *merger = &umgr->MERGER;
    sys_assert(is_semi_dtag(dst) == false);
    mrgr->lba_s = start;
    mrgr->nlba = nlba;

    //pengding merge exist, force add mrgr to pengding
    if (umgr->MERGER.size) {
        umgr->MERGER.size++;
        list_add_tail(&mrgr->entry, &merger->head);
    }
    else if(!bm_data_merge(mrgr->dtag, dst, start, nlba, callback, (void*)mrgr))
    {
        //Q full condition,add to MERGER todo
        umgr->MERGER.size++;
        list_add_tail(&mrgr->entry, &merger->head);
    } else {
        list_add_tail(&mrgr->entry, &merger->head2);
        // disp_apl_trace(LOG_UCACHE_DEBUG, 0x6e9a, "merge start,src:%x dst:%x s:%x n:%d", mrgr->dtag.dtag, dst.dtag, start, nlba);
        umgr->MERGER.mergeing_size++;
    }
}

static fast_code void MERGER(merger_op_t ops, ce_t* ce)
{
    u8 lbanum;
	merger_t *mrgr = mgr_index_2_mgr(ce->mrgr_indx);
	switch (ops) {
	case MERGER_TRIGGER:
        if(host_sec_bitz==9)//joe add sec size 20200818
    		lbanum=8;
    	else
    		lbanum=1;
        sys_assert(mrgr->src_lba_s == 0);
        if (ce->ofst != 0) {            ////merge head
            sys_assert(mrgr->src_nlba >= ce->ofst);
            ucache_merge(mrgr, ce->dtag, 0, ce->ofst, ucache_merge_done);
        }
        else if (mrgr->src_lba_s + mrgr->src_nlba == lbanum) {
            ucache_merge(mrgr, ce->dtag, ce->nlba, lbanum - ce->nlba, ucache_merge_done);
        }
        else {
            sys_assert(0);
        }
		break;
    case MERGER_TRIGGER_FORCE:
        sys_assert(!list_empty(&umgr->MERGER.head));
        ce_t* ce_force;
        do {
            mrgr = list_first_entry(&umgr->MERGER.head, merger_t, entry);
            ce_force = (ce_t*)(mrgr->ce);
            if (bm_data_merge(mrgr->dtag, ce_force->dtag, mrgr->lba_s, mrgr->nlba, ucache_merge_done, (void*)mrgr)) {
                list_del_init(&mrgr->entry);
                sys_assert(umgr->MERGER.size != 0);
                umgr->MERGER.size--;
                umgr->MERGER.mergeing_size++;
                list_add_tail(&mrgr->entry, &umgr->MERGER.head2);
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "force merge start,src:%x dst:%x s:%x n:%d", mrgr->dtag.dtag, ce_force->dtag.dtag, mrgr->lba_s, mrgr->nlba);
            }
            else {
                //Q full condition,add to MERGER todo
                break;
            }
        }while(!list_empty(&umgr->MERGER.head));
        break;
    default:
        sys_assert(0);
        break;
	}
}

//concat valid lba from src to dst
//1,inc src dtag ref
//2,sned merge request
//3,free src resource
void ucache_concat(ce_t* src, ce_t* dst)
{
    u8 lbanum;
    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "concat src ce:%x s:%x l:%d dst:%x s:%x l:%d", src, src->ofst, src->nlba, dst, dst->ofst, dst->nlba);
    if(host_sec_bitz==9)//joe add sec size 20200818
		lbanum=8;
	else
		lbanum=1;
    merger_t* mrgr = mgr_index_2_mgr(dst->mrgr_indx);
    sys_assert(src->ofst == 0);
    sys_assert(dst->ofst == src->nlba);
    //1, send merge request
    sys_assert(is_ddtag(src->dtag));
    mrgr->dtag = src->dtag;
    mrgr->src_lba_s = src->ofst;
    mrgr->src_nlba = src->nlba;
    ucache_inc_ddtag(mrgr->dtag);
    //after merge done, dst ce will become nrm ce(par -> nrm)
    if (dst->ofst + dst->nlba == lbanum) {
        dst->state = CE_ST_MERGE;
    }
    MERGER(MERGER_TRIGGER, dst);

    //2, delete src ce
    //free src mrgr
    merger_t *src_mrgr = mgr_index_2_mgr(src->mrgr_indx);
    UCACHE_FREE_MRGR(src_mrgr);
    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", src_mrgr);
    //free src ce
    cache_cut(src);
    //ucache_free_dtag(src->dtag);
    UCACHE_FREE_CE_and_DTAG(src,false);
   // disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", src);
    //umgr->slice_cnt--;
}

fast_code void par_data_in_hit_par(ce_t* ce, ce_t* next)
{
    //par hit par ce handle flow
    //1, check ofst continue, if yes, start merge, else wait prev ce merge done
    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "cur ce:%x s:%x l:%d next:%x s:%x l:%d", ce, ce->ofst, ce->nlba, next, next->ofst, next->nlba);
    sys_assert(ce != NULL);
    sys_assert(next != NULL);
    ce->state = CE_ST_HOLD;

    //4.2.1.1 check next cache status
    switch (next->state) {
        case CE_ST_START:
            cache_entry_del_single(next);
            umgr->READER.can_cnt--;
            sys_assert(next->ce_next == INV_U16);
            //check lba sequential
            if ((ce->ofst == next->nlba)&&(!IS_FUAorWUNC_CMD(ce->btag))) {
                ce->state = CE_ST_CONCAT;
                //concat
                ucache_concat(next, ce);
                //concat start, delete current ce
            }
            else {   //not sequential par in, start read modify read for the first par in
                next->state = CE_ST_WANT;
                READER(READER_IMT, next);
            }
            break;
            //next ce is merging, only check lba overlap case
        default:
            break;
    }
}

ddr_code void ucache_par_data_in(lda_t lda, dtag_t dtag, u16 btag, int ofst, int nlba)
{
    sys_assert(0);
}


fast_code void par_data_in_handle(ce_t* ce)
{
    //1,release semi dtag if par in, we don't want handle semi dtag in partial data in
    if (is_semi_dtag(ce->dtag)) {
        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free sdtag:%d", smdtag2sdtag(ce->dtag.dtag));
        ce->dtag = ucache_free_smdtag(ce->dtag);
    }

    //2.allocate slice for merge use, do not need any more, deleted

    //3. allocate mrgr for merge use
    merger_t* mrgr = MERGE_SLICE_GET();
    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "par in get slice:%x", mrgr);
    sys_assert(umgr->ce_pool.avail != 0);
    umgr->ce_pool.avail--;
    btn_de_wr_hold_handle(umgr->ce_pool.avail, WR_HOLD_THRESHOLD, WR_CANCEL_HOLD_THRESHOLD);
	sys_assert(mrgr != NULL);

    INIT_LIST_HEAD(&mrgr->entry);
    mrgr->ce = (void*)ce;
    mrgr->size = 0;
    ce->mrgr_indx = mgr_index(mrgr);
    #ifdef LJ1_WUNC
    if(IS_WUNC_CACHE(ce->btag)){
        mrgr->wunc_bitmap = (((1 << (ce->nlba)) - 1) << (ce->ofst));
        //disp_apl_trace(LOG_WUNC_INFO, 0, "WUNC in cache ce:%x,mrgr:%x,wunc_bitmap:%x", ce, mrgr, mrgr->wunc_bitmap);
    }
    else
    #endif
    {
        mrgr->wunc_bitmap = 0;
    }
    //disp_apl_trace(LOG_INFO, 0, "par in cache ce:%x,ofst:%x,nlba:%d,lda:%x", ce,ce->ofst,ce->nlba, ce->lda);
    //4,check ce list and handle case
    //4.1, cache list enpty
    if (ce->ce_next == INV_U16) {
        if ((ce->ofst == 0) && (!IS_FUAorWUNC_CMD(ce->btag))) {
            ce->state = CE_ST_START;
            READER(READER_CAN, ce);
        }
        else {
            ce->state = CE_ST_WANT;
            READER(READER_IMT, ce);
        }
    }
    else {
    //4.2, check cache list , only need to check prev ce, because other ce was handled by it's prev ce
        ce_t* next = ce_index_2_ce(ce->ce_next);
        //4.2.1 prev ce is par data, check continue or not
        if (next->nrm == false) {
            par_data_in_hit_par(ce, next);
        }
        else {
        //4.2.2 prev ce is nrm data, just start merge
            ce->state = CE_ST_MERGE;
            if (is_semi_dtag(next->dtag)) {
                mrgr->dtag.dtag = smdtag2ddtag(next->dtag.dtag);
            }
            else {
                mrgr->dtag = next->dtag;
            }
            u8 lbanum;
            if(host_sec_bitz==9)//joe add sec size 20200818
                lbanum=8;
            else
                lbanum=1;
            mrgr->src_lba_s = 0;        //nrm data, all lba is valid
            mrgr->src_nlba = lbanum;
            //use mrgr->dtag to do merge,so inc ddtag ref
            if(mrgr->dtag.dtag != EVTAG_ID)
                ucache_inc_ddtag(mrgr->dtag);
            #ifdef LJ1_WUNC
            if(IS_WUNC_CACHE(ce->btag)){//prev or cur ce is WUNC cache
                ce->dtag = mrgr->dtag;
                ce->state = CE_ST_DIRTY;
                ce->nrm = 1;
                ce->wunc_bitmap = next->wunc_bitmap|mrgr->wunc_bitmap;
                UCACHE_FREE_MRGR(mrgr);
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr);
                //umgr->slice_cnt--;
                sys_assert(umgr->ce_pool.avail < cache_cnt);
            }
            else if(mrgr->dtag.dtag == EVTAG_ID){
                ce->nrm = 1;
                ce->state = CE_ST_DIRTY;
                ce->wunc_bitmap = NON_WUNC_BIT_SET(ce->nlba, ce->ofst, next->wunc_bitmap);//((~(((1 <<(ce->nlba)) - 1) << (ce->ofst))) & (next->wunc_bitmap));//cur ce is not wunc remove the valid bit
                UCACHE_FREE_MRGR(mrgr);
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr);
                //umgr->slice_cnt--;
                sys_assert(umgr->ce_pool.avail < cache_cnt);
                //if(ce->wunc_bitmap){
                    //ce->btag |= WUNC_BTAG_TAG;
                //}
                #ifndef FTL_H_MODE
                ucache_l2p_update_cmmt(ce);
                #endif
            }
            else
            #endif
            {
                if (next->wunc_bitmap) {
                    mrgr->wunc_bitmap = NON_WUNC_BIT_SET(ce->nlba, ce->ofst, next->wunc_bitmap);//((~(((1 <<(ce->nlba)) - 1) << (ce->ofst))) & (next->wunc_bitmap));
                }
                MERGER(MERGER_TRIGGER, ce);
            }

            //4.2.2.1 handle prev ce cache status,for nrm data, only 3 status, dirty or l2p update, or invalid??
            switch (next->state) {
                case CE_ST_DIRTY:
                    cache_cut(next);
                    //ucache_free_dtag(next->dtag);
                    //free ce here??
                    UCACHE_FREE_CE_and_DTAG(next,false);
                    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", next);
                    break;
                case CE_ST_L2UP:
                    next->state = CE_ST_INVALID;
					#ifndef FTL_H_MODE
					// after update done, no need to find prev
					#ifdef LJ1_WUNC
                    if(!IS_WUNC_CACHE(ce->btag))
                    #endif
                    {
					    cache_cut(next);
                    }
					#endif
                    break;
                case CE_ST_INVALID:
                    //impossible, if one ce is marked as invalid, then prev ce must exist one dirty ce, just assert
                default:
                    sys_assert(0);
                    break;
            }

        }
    }


}

fast_code void nrm_data_in_handle(ce_t* ce)
{
    //most case, only one ce belong to the ce->lda
    if (ce->ce_next == INV_U16) {
        ucache_l2p_update_cmmt(ce);
        return;
    }
    bool commit = true;
    sys_assert(ce->ce_next < cache_cnt);
    ce_t* ce_cur = ce_index_2_ce(ce->ce_next);
    ce_t* ce_next;
    merger_t* mrgr_ce_cur;
    //nrm data in, mark old ce to invalid
    while(ce_cur != NULL) {

        switch (ce_cur->state) {
            case CE_ST_L2UP:
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "lda:%x hit l2p update, new:ce:%x cur:%x", ce->lda, ce, ce_cur);
                ce_cur->state = CE_ST_INVALID;
            case CE_ST_INVALID:
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "lda:%x hit INV, new:ce:%x cur:%x", ce->lda, ce, ce_cur);

				/*
				 * We use V_MODE for ftl translation right now, if you want to use H_MODE, please define FTL_H_MODE for ucache.
				 * Use V_MODE can make plp cache flush faster, since we don't need to wait last cache done.
				 */
                #ifdef FTL_H_MODE
                if(ce_cur->ce_next == INV_U16) {
                    ce_next = NULL;
                } else {
                    ce_next = ce_index_2_ce(ce_cur->ce_next);
                }
                commit = false;
				#else
				// just commit new nrm data, and cut prev ce here
                ce_next = cache_cut(ce_cur);
				#endif

                break;
            case CE_ST_START:
                cache_entry_del_single(ce_cur);
                umgr->READER.can_cnt--;
            case CE_ST_HOLD:
                mrgr_ce_cur = mgr_index_2_mgr(ce_cur->mrgr_indx);
                //if (ce_cur->state != CE_ST_START) {
                    //ucache_free_dtag(mrgr_ce_cur->dtag);
                //}
                UCACHE_FREE_MRGR(mrgr_ce_cur);
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr_ce_cur);
            case CE_ST_DIRTY:
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "lda:%x hit dirty, new:ce:%x cur:%x", ce->lda, ce, ce_cur);
                ce_next = cache_cut(ce_cur);
                //free dtag
                //ucache_free_dtag(ce_cur->dtag);
                //free cache entry
                UCACHE_FREE_CE_and_DTAG(ce_cur,false);
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce_cur);
                break;
                //partial handle already start,set invalid then recorver by itself
            case CE_ST_READ:
            case CE_ST_MERGE:
            case CE_ST_WANT:
            case CE_ST_CONCAT:
                sys_assert(ce_cur->nrm == false);
                ce_cur->state = CE_ST_INVALID;
                ce_next = cache_cut(ce_cur);
                break;
            default:
                //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "ce:%x state:%d", ce, ce->state);
                ce_next = NULL;
                sys_assert(0);
                break;
        }
        ce_cur = ce_next;
    }
    if (commit) {
        ucache_l2p_update_cmmt(ce);
    }

}
#if SHOW_PERFORMANCE_WRITE
fast_data u32 performance_write = 0;
fast_data u32 time_write = 0;
extern u32 shr_hostw_perf;
#endif
#if (TRIM_SUPPORT == ENABLE)
extern bool trim_host_write;
#if(BG_TRIM == ENABLE)
extern unalign_LDA TrimUnalignLDA;
#endif
#endif
#ifdef WCMD_DROP_SEMI
share_data_zi volatile u32 dropsemi;
#endif
extern bool is_lda_in_otf_trimdata(lda_t slda, lda_t elda);
fast_code void ucache_nrm_par_data_in(lda_t *ldas, dtag_t *dtags, u16 *btags, r_par_t *par, int count)
{
	int i = 0;
    #if 0
	 u8 lbanum=0;
      if(host_sec_bitz==9)//joe add sec size 20200818
		lbanum=8;
	else
		lbanum=1;
    #endif
    #if SHOW_PERFORMANCE_WRITE
    performance_write+=count;
    if (jiffies - time_write >= 10*HZ/10) {
        time_write = jiffies;
        shr_hostw_perf = performance_write;
        #if SHOW_PERFORMANCE_WLOG
        disp_apl_trace(LOG_INFO, 0x3aa3, "write performance:%d MB/s", performance_write>>8);
        #endif
        performance_write = 0;
    }
    #endif
    #if (TRIM_SUPPORT == ENABLE)
    trim_host_write = true;
    #if(BG_TRIM == ENABLE)
    if(TrimUnalignLDA.LBA != INV_U64){
        TrimUnalignLDA.LBA = INV_U64;
    }
    #endif
    #endif

	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	//if (ra_range_chk(lda, 1))
	{
		ra_disable();
	}
	#endif

	for (i = 0; i < count; i++) {
		ce_t *ce;
		lda_t lda = ldas[i];
		dtag_t dtag = dtags[i];
		u16 btag = btags[i];
		#ifdef WCMD_DROP_SEMI
        if(dropsemi>=100){
            if (is_semi_dtag(dtag)) {
                dtag = ucache_free_smdtag(dtag);
            }
        }
        #endif
        ce = pool_get_ex(&umgr->ce_pool.pool);
        sys_assert(ce != NULL);
        umgr->ce_pool.avail--;
        btn_de_wr_hold_handle(umgr->ce_pool.avail, WR_HOLD_THRESHOLD, WR_CANCEL_HOLD_THRESHOLD);
        ce->lda = lda;
		ce->state = CE_ST_DIRTY;
		ce->dtag = dtag;
		ce->btag = btag;
        ce->wunc_bitmap = 0; ///
        ce->entry2_next = ce->entry2_prev = INV_U16;
        ce->after_trim = 0;
        ce->before_trim = 0;
        ce_t* ce_cache = cache_search(lda);
        //disp_apl_trace(LOG_INFO, 0, "lda:%x in ce:%x cache:%x, sdtag:%d dd:%x", ce->lda, ce, ce_cache, smdtag2sdtag(dtag.dtag),smdtag2ddtag(dtag.dtag));
        if (ce_cache != NULL) {
            //del old cache entry in hash table
            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "old cache st:%d s:%x l:%d", ce_cache->state, ce_cache->ofst, ce_cache->nlba);
            cache_delete(ce_cache);
            //update cache list
            ce->ce_next = ce_index(ce_cache);
            ce->ce_prev = INV_U16;
            ce_cache->ce_prev = ce_index(ce);
            ce->rph_head = ce_cache->rph_head;//replace new read pending head
            //disp_apl_trace(LOG_INFO, 0, "rph_head move from ce:0x%x to ce:%x lda:%x", ce_cache, ce, ce_cache->lda);
            ce_cache->rph_head = INV_U16;  //remove old read pending head
        } else {
            ce->ce_next = ce->ce_prev = INV_U16;
            ce->rph_head = INV_U16;
        }
        //register new cache entry to hash table
        cache_insert(ce);
        #ifdef LJ1_WUNC
        if(IS_WUNC_CACHE(ce->btag)){
            bool rst = insert_wunc_lda2ce(ce);
            sys_assert(rst);
        }
        #endif
        #if(TRIM_SUPPORT == ENABLE)
        extern u8* TrimTable;
        extern Trim_Info TrimInfo;
        if(TrimInfo.Dirty && (bitmap_check((u32 *)TrimTable, (ce->lda >> LDA2TRIMBITSHIFT))
            || is_lda_in_otf_trimdata(ce->lda, ce->lda))){
            // disp_apl_trace(LOG_ALW, 0, "ce:%x LDA(0x%x) Dtag(%x) state:%d, after_trim 1",ce, lda, dtag.dtag, ce->state);
            ce->after_trim = 1;
            need_check = true;
            if(par[i].all && ((ce_cache == NULL) || (!ce_cache->after_trim))){
                par[i].all = 0;
                cache_handle_dtag_cnt ++;
            }
        }
        #endif
        if (par[i].all) {
            ce->nrm = 0;    //par data in
            ce->nlba = par[i].nlba;
            ce->ofst = par[i].ofst;
            par_data_in_handle(ce);
        } else {
            ce->nrm = 1;    //nrm data in
            nrm_data_in_handle(ce);
        }
	}
}

fast_code void ucache_fr_data_in(bm_pl_t *pls, int count,bool error)
{
	int i = 0;
	bm_pl_t *pl = &pls[0];
    ce_t* ce;
    u8 lbanum;
    u8 errbits = 0;
    if(host_sec_bitz==9){//joe add sec size 20200818
		lbanum=8;
        errbits = 0xFF;
    }
	else{
		lbanum=1;
        errbits = 1;
	}
	for (; i < count; i++, pl++) {
		sys_assert(pl->pl.btag == ua_btag);
		dtag_t dtag = { .dtag = pl->pl.dtag };

		umgr->READER.rr_cnt++;
        ce = umgr->READER.ce[pl->pl.du_ofst];
        sys_assert(ce != NULL);
		lda_t lda = ce->lda;
		clear_bit(pl->pl.du_ofst, (void *) umgr->READER.lda_bmp);

		if (unlikely(lda == INV_LDA)) {
			if (dtag.dtag != DDTAG_MASK)
				dtag_put_ex(dtag);
			continue;
		}

		umgr->READER.ce[pl->pl.du_ofst] = NULL;

       // error TODO
		//ce_t *ce = cache_search(lda); //??? need get current ce

        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "ce:%x LDA(0x%x) Dtag(%x) state:%d",ce, lda, dtag.dtag, ce->state);
        merger_t *mrgr = mgr_index_2_mgr(ce->mrgr_indx);

		switch (ce->state) {
		case CE_ST_READ:
			/* most of case*/
            if (dtag.dtag == DDTAG_MASK) { //no mapping lda.actually do nothing
                ce->state = CE_ST_DIRTY;
                sys_assert(ce->ce_next == INV_U16);
                #ifdef LJ1_WUNC
                ce = ucache_WUNC_handle(ce,(error?errbits:0),ce->dtag);
                #endif
                if(ce){
                    mrgr = mgr_index_2_mgr(ce->mrgr_indx);
                    ce->wunc_bitmap = mrgr->wunc_bitmap;
                    UCACHE_FREE_MRGR(mrgr);
                    //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr);
                    if (ce->ce_prev == INV_U16) {   // no prev ce,force commit here
                        ce->nrm = 1;
                        ce->ofst = 0;
                        ce->nlba = lbanum;
                        if(ce->before_trim)
						{
							//ucache_free_trim_partial_ce(ce,mrgr,lbanum,1);
							ucache_free_trim_nrm_ce(ce,1);
							continue;
						}
						else
						{
                        	nrm_data_in_handle(ce);	
						}
                    }
                    else {  //prev ce must be partial ce, so par in hit dirty case handle
                        ce_t* prev = ce_index_2_ce(ce->ce_prev);
                        sys_assert(prev->nrm == false);
                        //force use ce->dtag to do merge
                        ucache_inc_ddtag(ce->dtag);
                        ucache_free_dtag(dtag);
                        cache_cut(ce);
                        #ifdef LJ1_WUNC
                        prev = ucache_WUNC_handle(prev,ce->wunc_bitmap,ce->dtag);
                        #endif

                        if(prev){
                            sys_assert(prev->nrm == false);
                            merger_t *mrgr_prev = mgr_index_2_mgr(prev->mrgr_indx);
                            mrgr_prev->dtag = ce->dtag;
                            mrgr_prev->src_lba_s = 0;
                            mrgr_prev->src_nlba = lbanum;

                            prev->state = CE_ST_MERGE;
                            MERGER(MERGER_TRIGGER, prev);
                        }
                        //ucache_free_dtag(ce->dtag);
                        UCACHE_FREE_CE_and_DTAG(ce,false);
                        //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
                   }
               }
            }
            else {
                mrgr->dtag = dtag;
                mrgr->src_lba_s = 0;
                mrgr->src_nlba = lbanum;
                #ifdef LJ1_WUNC
                u8 bmp = *get_pdu_bmp(dtag);
                set_pdu_bmp(dtag, 0);
                ce = ucache_WUNC_handle(ce,bmp,dtag);
                #endif
                if(ce){
                    ce->state = CE_ST_MERGE;
                    mrgr = mgr_index_2_mgr(ce->mrgr_indx);
                    mrgr->dtag = dtag;
    			    MERGER(MERGER_TRIGGER, ce);
                }
            }
			break;
        case CE_ST_INVALID:
            //ucache_free_dtag(ce->dtag);
            ucache_free_dtag(dtag);
            UCACHE_FREE_MRGR(mrgr);
            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free mrgr:%x", mrgr);
            UCACHE_FREE_CE_and_DTAG(ce,false);
            //disp_apl_trace(LOG_UCACHE_DEBUG, 0, "free ce:%x", ce);
			break;
		default:
			//disp_apl_trace(LOG_ERR, 0, "Wrong ce_state(%x)", ce->state);
			sys_assert(0);
            break;
		}
	}

	if ((ucache_read_pend_cnt) && pend_dtag_mgr->availa_cnt > dtag_evt_trigger_cnt) {
		evt_set_cs(evt_get_pend_dtag, 0, 0, CS_TASK);
		dtag_evt_trigger_cnt++;
		ucache_read_pend_cnt--;
	}
	else if ((ucache_read_pend_cnt) && ((remote_dtag_wait + pend_dtag_mgr->availa_cnt) < CACHE_READER_LDA_SZE)) {
		ipc_api_remote_dtag_get((u32 *) NULL, false, RDISK_PAR_DTAG_TYPE);
		remote_dtag_wait++;
		ucache_read_pend_cnt--;
	}
}

fast_code void ucache_unmap_data_in(int ofst, bool pop, bool error)
{
	if (pop) {
        dtag_t dtag = umgr->READER.dtags[ofst];
		sys_assert(dtag.dtag != DTAG_INV);
		dtag_put_ex(dtag);
	}

	bm_pl_t pl = {
		.pl.dtag = DDTAG_MASK,
		.pl.du_ofst = ofst,
		.pl.btag = ua_btag,
	};
	ucache_fr_data_in(&pl, 1, error);

	return;
}
#if 1
fast_code void ucache_read_error_data_in(int ofst)
{
	ucache_unmap_data_in(ofst, true, true);
}
#endif

/*!
* @brief lookup for lda
*
* @param lda		lda
*
* @return		pointer to ce, NULL for not hit
*/
static fast_code inline ce_t *ucache_lookup(lda_t lda)
{
	// struct list_head *cur;
	// struct list_head *ce_hlist =
	// 	&umgr->ce_hlist[ucache_hash(lda, CE_HLIST_MSK)];
	//
	// list_for_each(cur, ce_hlist) {
	// 	ce_t *ce = container_of(cur, ce_t, entry);
	// 	if (ce->lda == lda)
	// 		return ce;
	// }

	ce_t *ce = cache_search(lda);
		if (ce->lda == lda)
			return ce;
	return NULL;
}

/*!
* @brief check lda whether hit in ucache
*
* @param lda	lda
*
* @return		hit status
*/
fast_code bool ucache_read_hit(lda_t lda)
{
	return ucache_lookup(lda) ? true : false;
}

fast_code void ucache_read_data_out(u32 btag, lda_t lda, int count, u32 offset)
{
	int i = 0;
	int missed = 0;
	dtag_t dtag;
	for (i = 0; i < count; i++) {
		ce_t *ce = cache_search(lda + i);

		if (likely(ce == NULL)) {
            #if 0//(TRIM_SUPPORT == DISABLE)
			if (!list_empty(&umgr->range.req)) {
				if (ucache_range_op_check(lda + i, i, btag)) {
					if (missed) {
						l2p_srch_ofst(lda + i - missed, missed,btag, i - missed + offset, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
						missed = 0;
					}
				} else {
					missed++;
				}
			}
            else
            #endif
		    {
				missed++;
			}

			continue;
		}

		if (missed) {
			l2p_srch_ofst(lda + i - missed, missed, btag, i - missed + offset, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
			missed = 0;
		}

		switch (ce->state) {
        case CE_ST_DIRTY:
        case CE_ST_L2UP:
            if (ce->dtag.b.type_ctrl & BTN_SEMI_STREAMING_MODE)
				dtag.dtag = smdtag2ddtag(ce->dtag.dtag);
			else
				dtag.dtag = ce->dtag.dtag;
            if(dtag.dtag == EVTAG_ID){
                //dtag.dtag = EVTAG_ID;
            }
            else if (IS_WUNC_CACHE(ce->btag)) {
                u8 lba_num;
                if (host_sec_bitz == 9) //joe add sec size 20200818
            		lba_num = 8;
            	else
            		lba_num = 1;
                u32 dtagbak = dtag.dtag;
                dtag.dtag = EVTAG_ID;

                btn_cmd_t *bcmd = btag2bcmd(btag);
                u64 slba = bcmd_get_slba(bcmd);
                u16 len = bcmd->dw3.b.xfer_lba_num;
                lda_t lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz));
                lda_t lda_tail = ((slba + len) >> (LDA_SIZE_SHIFT - host_sec_bitz));
                u8 head = slba & (lba_num - 1);
                u8 tail = (slba + len) & (lba_num - 1);
                if (head && tail && (lda == lda_tail))
                    tail = 0;
                if ((lda == ce->lda)&&(head)){
                    u32 cnt = min(lba_num - head, len);
                    u8 headbitmap = (((1 << cnt) - 1)<< head);
                    //printk("headbitmap %x,cnt %x head %x\n",headbitmap,cnt,head);
                    if(!(ce->wunc_bitmap & headbitmap)){
                        dtag.dtag = dtagbak;
                        dtag_ref_inc_ex(dtag);
                    }
                }
                if((tail)&&(lda_tail == ce->lda)){
                    u8 tailbitmap = ((1 << (tail)) - 1);
                    //printk("tailbitmap %x,tail %x\n",tailbitmap,tail);
                    if(!(ce->wunc_bitmap & tailbitmap)) {
                        dtag.dtag = dtagbak;
                        dtag_ref_inc_ex(dtag);
                    }
                }
            }
			else if (dtag.dtag == DDTAG_MASK){
				dtag.dtag = RVTAG_ID;
			}
			else
				dtag_ref_inc_ex(dtag);

			bm_rd_dtag_commit(i + offset, btag, dtag);

            break;
        default:
            if (ce->state == CE_ST_START) {
                READER(READER_CAN_FLUSH, NULL);
            }
            sys_assert(ce->state != CE_ST_INVALID);

            cache_read_t* cr = cache_read_entry_get(btag, i + offset);//joe add offset fix cache hitdd WRC fail edevx 20201210

			sys_assert(cr != NULL);

            if (CR_CHECK_FREE(ce->rph_head)) {
                ce->rph_head = cache_read_idx_get(cr);
            } else {
                cr->next = ce->rph_head;
                ce->rph_head = cache_read_idx_get(cr);
            }
            //disp_apl_trace(LOG_INFO, 0, "read pending start,lda:%x ofst:%d btag:%d, ce:%x", ce->lda, cr->du_ofst, cr->btag, ce);
            break;
		}
	}

	if (missed)
		l2p_srch_ofst(lda + i - missed, missed,
			btag, i - missed + offset, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
}

fast_code void ucache_single_read_data_out(u32 btag, lda_t lda, int count, u32 offset)
{
	ce_t *ce = cache_search(lda);

	if (likely(ce == NULL))
	{
		u32 ret = l2p_single_srch(lda, SINGLE_SRCH_MARK, btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
		sys_assert(ret == 1);
	}
	else
	{
		ucache_read_data_out(btag, lda, count, 0);
	}
}

/*!
 * @brief flush idle concat list
 *
 * @param data		not used
 *
 * @return		not used
 */
static fast_code void ucache_idle_concat_flush(void *data)
{
    //ucache_flush_check(0, 0, 0);

	if ((umgr->READER.last_can_cnt == 0) ||
		(umgr->READER.last_can_cnt != umgr->READER.can_cnt)) {
			umgr->READER.last_can_cnt = umgr->READER.can_cnt;
		mod_timer(&umgr->READER.can_ls_timer, jiffies + HZ/10);
		return;
	}


	if (!cache_list_empty(umgr->READER.can_ls_head)) {
		//disp_apl_trace(LOG_UCACHE_DEBUG, 0, "cancel concat merge, READ_IMT for normal merge");
		READER(READER_CAN_FLUSH, NULL);
	}

	mod_timer(&umgr->READER.can_ls_timer, jiffies + HZ/10);
}

 slow_code void ucache_idle_chk_fua_list(void *data)
{
	if(umgr->FUALIST.size == 0)
		return;

	//chk fua list l2p done?
	ce_t* cur = umgr->FUALIST.head;
	ce_t* ce_next = NULL ;
	ce_t* ce_list = NULL ;
	do{
		if(cur->entry2_next != INV_U16)
			ce_next = ce_index_2_ce(cur->entry2_next);
		else
			ce_next = NULL;

		ce_list = cache_search(cur->lda);
		if(ce_list == NULL)//
		{
			disp_apl_trace(LOG_ALW, 0x8796,"[FUA]FUA cmd 0x%x cache no hit?? lda:0x%x dtag:0x%x",cur->btag,cur->lda,cur->dtag.dtag);

			cache_entry_del(cur, &umgr->FUALIST.head, &umgr->FUALIST.tail);
			umgr->FUALIST.size--;
			//ucache_free_dtag(cur->dtag);//dec 1
			UCACHE_FREE_CE_and_DTAG(cur, true);
			//break;
		}
		else
		{
			if(!plp_trigger)
			{
				disp_apl_trace(LOG_INFO, 0x0352,"[FUA] cur:0x%x ce:0x%x state:%d lda:0x%x dtag:0x%x",
					cur,ce_list,ce_list->state,ce_list->lda,ce_list->dtag.dtag);
			}
		}
		cur = ce_next;
	}while(cur != NULL);

	if(umgr->FUALIST.size == 0)
		return;
	else
		mod_timer(&umgr->FUALIST.fua_ls_timer, jiffies + HZ);

}


/*!
 * @brief interface to flush cache
 *
 * @param data		not used
 *
 * @return		not used
 */
static fast_code bool ucache_flush_update(void)
{
    bool ret = true;
    //CPU1_plp_step = 5;
	//TODO
	u16 hash_idx = 0;
    if (umgr->READER.can_cnt) {
        READER(READER_CAN_FLUSH, NULL);
        ret = false;
    }
	if ((ucache_read_pend_cnt) && pend_dtag_mgr->availa_cnt > dtag_evt_trigger_cnt) {
		evt_set_cs(evt_get_pend_dtag, 0, 0, CS_TASK);
		dtag_evt_trigger_cnt++;
		ucache_read_pend_cnt--;
	}
    else if ((ucache_read_pend_cnt) && ((remote_dtag_wait + pend_dtag_mgr->availa_cnt)< CACHE_READER_LDA_SZE)) {
		ipc_api_remote_dtag_get((u32 *) NULL, false, RDISK_PAR_DTAG_TYPE);
		remote_dtag_wait++;
		ucache_read_pend_cnt--;
//		if (plp_trigger) {
//			disp_apl_trace(LOG_ALW, 0, "flush, getting:%d, pending:%d", remote_dtag_wait, ucache_read_pend_cnt);
//		}
	}
    if (umgr->MERGER.size) {
        MERGER(MERGER_TRIGGER_FORCE, NULL);
        ret = false;
    }

    if (umgr->UPDATER.size != 0) {
        ret = false;
    }

    u32 update_cnt = umgr->UPDATER.size; //ucache_l2p_update_cmmt may add new ce to updater list
    while (update_cnt) {
        ret = false;
        ce_t* ce = umgr->UPDATER.head;
		sys_assert(ce);
        cache_entry_del(ce, &umgr->UPDATER.head, &umgr->UPDATER.tail);
        umgr->UPDATER.size--;
        update_cnt--;
        if (ce->state == CE_ST_L2UP) {      //force commit ce here
            ucache_l2p_update_cmmt(ce);
        }
        else if (ce->state == CE_ST_INVALID) {  //status changed, don't need flush, just free current ce & dtag
            ucache_free_dtag(ce->dtag);
            ucache_pda_updt_ce_handle(ce);
        }
        else {
            sys_assert(0);
        }
    }

    if (umgr->FUALIST.size != 0) {
    	ucache_idle_chk_fua_list(NULL);
        //ret = false;//don't need return false , FUA ce will never release until ftl_core_flush done 
    }


    //skip check hash table to save time, because no need
    if (ret == false) {
        return false;
    }

    for (hash_idx = 0; hash_idx < CE_HLIST_SZE; hash_idx++) {
        if (umgr->hash_table[hash_idx] != INV_U16) {
            ce_t* ce = ce_index_2_ce(umgr->hash_table[hash_idx]);
			// check each ce for the same LTA
            while(1) {
                if (ce->state != CE_ST_L2UP) {  //ce is do merge or other operation, need wait l2p update
                	#if (PLP_NO_DONE_DEBUG == mENABLE)
					static bool print = true;
					if (plp_trigger && print) {
						print = false;
						disp_apl_trace(LOG_ALW, 0x504d, "ce 0x%x, state:0x%x, lda:0x%x", ce, ce->state, ce->lda);
					}
					#endif
                    return false;
                }
                if (ce->entry_next == INV_U16)
                    break;
                ce = ce_index_2_ce(ce->entry_next);
            }
        }
    }

    if (ret == true) {
		u64 exec_time = get_tsc_64();
        if(!plp_trigger)
		    disp_apl_trace(LOG_ALW, 0xdd26, "END t:0x%x-%x",exec_time>>32, exec_time&0xFFFFFFFF);
		else
			CPU1_plp_step = 7;
        while (ucache_flush_ntf_que.wptr != ucache_flush_ntf_que.rptr) {
            ftl_flush_data_t *fctx = (ftl_flush_data_t *)ucache_flush_ntf_que.buf[ucache_flush_ntf_que.rptr];
            ftl_core_flush(fctx);
            ucache_flush_ntf_que.rptr++;
            if (ucache_flush_ntf_que.rptr == ucache_flush_ntf_que.size) {
                ucache_flush_ntf_que.rptr = 0;
            }
        }

    }
   // extern u32 otf_fua_cmd_cnt;
   // if ((ret == true) && (otf_fua_cmd_cnt == 0)) {
        //cache_handle_dtag_cnt = 0;   //ucache idle, force set dtag cnt to 0
   // }
    ucache_force_flush = false;

	return ret;
}
#ifdef BG_TRIM_ON_TIME
fast_code void ucache_trim_check()
{
    if(umgr->UPDATER.size){
        ucache_flush_update();
    }
}
#endif
extern u64 plp_cache_tick;
extern bool ucache_flush_flag;
static fast_code void ucache_flush_check(u32 parm, u32 payload, u32 sts)
{
    if (true == ucache_flush_update()) {
        if(plp_trigger){
    		u64 curr1 = get_tsc_64();
    		disp_apl_trace(LOG_ALW, 0x45eb, "finish:0x%x-%x", curr1>>32, curr1&0xFFFFFFFF);
        }
    }
    else {
		if(plp_trigger&& ucache_flush_flag&&(time_elapsed_in_ms(plp_cache_tick) >= 20))
		{
			ucache_dump();
			plp_cache_tick = get_tsc_64();
		}
        evt_set_cs(evt_cache_force_flush, 0, 0, CS_TASK);
    }
}
fast_code void ucache_flush(ftl_flush_data_t *fctx)
{
    bool ret;
	u64 curr1;
    CBF_INS(&ucache_flush_ntf_que, ret, fctx);
    sys_assert(ret == true);
    ucache_force_flush = true;

    if(plp_trigger)
    {
		CPU1_plp_step = 6;
    }

	if (true == ucache_flush_update()) {
		curr1 = get_tsc_64();
		disp_apl_trace(LOG_ALW, 0xd9d5, "finish:0x%x-%x", curr1>>32, curr1&0xFFFFFFFF);
    }
    else {
		//u64 curr2 = get_tsc_64();
		//disp_apl_trace(LOG_ALW, 0, "task:0x%x-%x", curr2>>32, curr2&0xFFFFFFFF);
        evt_set_cs(evt_cache_force_flush, 0, 0, CS_TASK);
    }
}

/*!
 * @brief flush idle ce to be updated
 *
 * @param data		not used
 *
 * @return		not used
 */
 #if 0
static fast_code void ucache_idle_update(void *data)
{
    TODO
	//mod_timer(&umgr->UPDATER.updt_timer, jiffies + 3 * HZ);
}
#endif

static fast_code void ucache_dtag_get_async(volatile cpu_msg_req_t *req)
{
	sys_assert(req->pl != _inv_dtag.dtag);
	sys_assert(remote_dtag_wait);

	remote_dtag_wait--;
	ucache_read_cont((void *) req->pl);
}
static fast_code void evt_get_pend_dtag_func(u32 param0, u32 param1, u32 param2)
{
	sys_assert(!list_empty(&pend_dtag_mgr->entry1));
	sys_assert(pend_dtag_mgr->availa_cnt);

	u8 cnt = dtag_evt_trigger_cnt;
	u8 i;
	for(i=0; i<cnt; i++){
		pend_dtag_mgr->availa_cnt--;
		pend_dtag_mgr->free_cnt++;
		dtag_evt_trigger_cnt --;
		dtag_pend* dtag_pending = list_first_entry(&pend_dtag_mgr->entry1, dtag_pend, pending);
		list_del_init(&dtag_pending->pending);
		list_add_tail(&dtag_pending->pending, &pend_dtag_mgr->entry2);

		sys_assert(!list_empty(&pend_dtag_mgr->entry2));
		//disp_apl_trace(LOG_INFO, 0, "2. get dtag(%d) ,index(%d), cnt%d", dtag_pending->pl, dtag_pending->index, dtag_evt_trigger_cnt);
		ucache_read_cont((void *) dtag_pending->pl);
	}

	if(pend_dtag_mgr->availa_cnt)
		evt_set_cs(evt_get_pend_dtag, 0, 0, CS_TASK);//for no fill up case to free dtag,
}
fast_code static void ucache_read_error(int ofst, u32 status)
{
	disp_apl_trace(LOG_ERR, 0x8376, "handle read error, off %d status %d", ofst, status);
	bm_pl_t bm_pl = { .all = 0 };
	if (status == ficu_err_par_err) {
        #ifdef LJ1_WUNC
	    u8 *pdu_bmp = get_pdu_bmp(umgr->READER.dtags[ofst]);
        #endif
		sys_assert(*pdu_bmp != 0);
		disp_apl_trace(LOG_ERR, 0x8d10, "par err pdu %x", *pdu_bmp);
	}
    #if 1
    else if(status != ficu_err_good){
		//disp_apl_trace(LOG_ERR, 0, "fill up read error ,dtag:0x%x", umgr->READER.dtags[ofst]);
		ucache_read_error_data_in(ofst);
		return;
	}
    #endif
	bm_pl.pl.btag = ua_btag;
	bm_pl.pl.du_ofst = ofst;
	bm_pl.pl.dtag = umgr->READER.dtags[ofst].dtag;

	ucache_fr_data_in(&bm_pl, 1, false);
}

static fast_code void ipc_ucache_read_error(volatile cpu_msg_req_t *req)
{
	u32 ofst = req->pl;
	u32 status = req->cmd.flags;

	ucache_read_error(ofst, status);
}

#if 0//(TRIM_SUPPORT == DISABLE)
static fast_code void ipc_ucache_suspend_trim(volatile cpu_msg_req_t *req)
{
	umgr->range.flags.b.suspend = true;

	if (umgr->range.fnum == 0) {
		disp_apl_trace(LOG_ALW, 0xefdf, "ucache range suspend %d wnum", umgr->range.wnum);
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_UCACHE_SUSP_TRIM_DONE, 0, 0);
	}
}

static fast_code void ipc_ucache_resume_trim(volatile cpu_msg_req_t *req)
{
	umgr->range.flags.b.suspend = false;
	if (umgr->range.wnum)
		evt_set_cs(evt_trim_range_issue, 0, 0, CS_TASK);
}
#endif
norm_ps_code void ucache_resume(void)
{
	u32 i;
    //register cache index
    for(i = 0; i < cache_cnt; i++) {
        //_ces[i].ce_idx = i;
    }

#if 0
    for(i = 0; i < 256; i++) {
        crt[i].index = i;
    }
#endif
    pool_init(&crt_pool.pool, (void*)crt, sizeof(crt), sizeof(cache_read_t), CRT_CNT);
    crt_pool.avail = crt_pool.size = CRT_CNT;

	//pool_init(&umgr->ce_pool.pool, (void *) _ces, sizeof(_ces), sizeof(ce_t), CES_CNT);
	pool_init(&umgr->ce_pool.pool, (void *) _ces, sizeof(ce_t) * cache_cnt, sizeof(ce_t), cache_cnt);
	umgr->ce_pool.size = umgr->ce_pool.avail = cache_cnt;

    #if 0//(TRIM_SUPPORT == DISABLE)
	memset(umgr->range.wbmp, 0, shr_range_size);
	memset(umgr->range.fbmp, 0, shr_range_size);
	memset(umgr->range.fcnt, 0, umgr->range.size);

	// shr_range_ptr -> ftl ns desc
	sys_assert(shr_range_ptr);
	for (i = 0; i < umgr->range.size; i++) {
		if (bitmap_check((u32 *)shr_range_ptr, i)) {
			set_bit(i, umgr->range.wbmp);
			umgr->range.wnum++;
		}
	}
	shr_range_ptr = (u8 *) umgr->range.wbmp;
	shr_range_wnum = tcm_local_to_share(&umgr->range.wnum);
	if (umgr->range.wnum) {
		disp_apl_trace(LOG_ALW, 0xcc52, "ucache range init with %d wnum", umgr->range.wnum);
		umgr->range.flags.b.suspend = true;
		umgr->range.flags.b.clean = false;
	}
    #endif
	for (i = 0; i < CACHE_READER_LDA_SZE; i++)
		cache_read_dtags[i].dtag = DTAG_INV;

    //memset(_r_dtag2ces, 0xFF, sizeof(_r_dtag2ces));
    memset(_r_dtag2ces, 0xFF, sizeof(u16) * (cache_cnt -260));
}

norm_ps_code void ucache_reset_resume(void)
{
	u32 i;

	memset((void*)umgr->hash_table, 0xFF, sizeof(u16)*CE_HLIST_SZE);

	memset(&_ces[0], 0x0, cache_cnt * sizeof(ce_t));
	/* READER */
	umgr->READER.can_cnt = 0;
	umgr->READER.last_can_cnt = 0;
	memset(umgr->READER.lda_bmp, 0, sizeof(umgr->READER.lda_bmp));
	umgr->READER.dtags = cache_read_dtags;
	for (i = 0; i < CACHE_READER_LDA_SZE; i++)
		umgr->READER.ce[i] = NULL;

	/* MERGER */
	INIT_LIST_HEAD(&umgr->MERGER.head);
	umgr->MERGER.size = 0;

	CBF_INIT(&ucache_flush_ntf_que);

	/* UPDATER */
	umgr->UPDATER.size = 0;
	umgr->UPDATER.head = umgr->UPDATER.tail = NULL;

	umgr->UPDATER.updt_cnt = 0;
	umgr->UPDATER.last_updt_cnt = 0;

	umgr->FUALIST.size = 0;
	umgr->FUALIST.head = umgr->FUALIST.tail = NULL;
	umgr->FUALIST.warning_cnt = 15;
	//INIT_LIST_HEAD(&umgr->UPDATER.head);
	//INIT_LIST_HEAD(&umgr->READER.w8ing);
	//INIT_LIST_HEAD(&umgr->READER.can_ls);
	//umgr->READER.rdp_list_head = umgr->READER.rdp_list_tail = NULL;
	umgr->READER.w8ing_head = umgr->READER.w8ing_tail = NULL;
	umgr->READER.can_ls_head = umgr->READER.can_ls_tail = NULL;
	umgr->READER.rr_cnt = umgr->READER.rs_cnt = 0;

	lock_ce_hash0 = INV_U16;
	lock_ce_hash1 = INV_U16;
	//register cache index
	for(i = 0; i < cache_cnt; i++) {
		//_ces[i].ce_idx = i;
	}

	pool_init(&crt_pool.pool, (void*)crt, sizeof(crt), sizeof(cache_read_t), CRT_CNT);
	crt_pool.avail = crt_pool.size = CRT_CNT;

	//pool_init(&umgr->ce_pool.pool, (void *) _ces, sizeof(_ces), sizeof(ce_t), cache_cnt);
	pool_init(&umgr->ce_pool.pool, (void *) _ces, sizeof(ce_t) * cache_cnt, sizeof(ce_t), cache_cnt);
	umgr->ce_pool.size = umgr->ce_pool.avail = cache_cnt;

	for (i = 0; i < CACHE_READER_LDA_SZE; i++)
		cache_read_dtags[i].dtag = DTAG_INV;

	//memset(_r_dtag2ces, 0xFF, sizeof(_r_dtag2ces));
	memset(_r_dtag2ces, 0xFF, sizeof(u16) * (cache_cnt - 260));

	#ifdef WCMD_DROP_SEMI
	dropsemi = 0;
	#endif

	{
		cache_handle_dtag_cnt = 0;
		ucache_read_pend_cnt = 0;
		remote_dtag_wait = 0;
		dtag_evt_trigger_cnt = 0;
	}

	#if defined(MPC) && (CPU_ID == CPU_FE)
	INIT_LIST_HEAD(&pend_dtag_mgr->entry1);
	INIT_LIST_HEAD(&pend_dtag_mgr->entry2);
	pend_dtag_mgr->free_cnt = pend_dtag_mgr->total = pending_dtag_cnt;
	pend_dtag_mgr->availa_cnt = 0;
	for(u8 cnt=0;cnt<pending_dtag_cnt;cnt++)
	{
		pend_dtag[cnt].index = cnt;
		INIT_LIST_HEAD(&pend_dtag[cnt].pending);
		list_add_tail(&pend_dtag[cnt].pending, &pend_dtag_mgr->entry2);
	}
	#endif

	evt_task_cancel(evt_get_pend_dtag);

}

norm_ps_code void ucache_suspend(void)
{
	// todo: try to accumulate data in ucache instead of flush them to FTL if FTL was clean
	sys_assert(umgr->ce_pool.avail == cache_cnt);
}
#if 0
fast_code void ucache_flush_recv_hook(cbf_t *cbf, void (*handler)(void))
{
    extern cbf_recv_t ucache_flush_recv;

	ucache_flush_recv.cbf = cbf;
	ucache_flush_recv.handler = handler;
}
#endif
init_code int ucache_init(u64 nlba)
{
	int i = 0;

	//memset((void *)umgr, 0, sizeof(*umgr));
#if 0
	for (i = 0; i < CE_HLIST_SZE; i++)
		INIT_LIST_HEAD(&umgr->ce_hlist[i]);
#endif

#if 0
	sys_assert(global_capacity != 0);
	if(global_capacity == 2048)
		cache_cnt = 3072;
	else if(global_capacity == 1024)
		cache_cnt = 1536;
	else if(global_capacity == 512)
		cache_cnt = 768;
	else
		panic("nand info error!!");

#else
	extern volatile u32 ddr_cache_cnt;
	cache_cnt = ddr_cache_cnt;
	disp_apl_trace(LOG_INFO, 0x05be, "ucache_init ddr_cache_cnt = %d sizeof(ce_t):%d",ddr_cache_cnt,sizeof(ce_t));
#endif
	cache_cnt += 260; //
	_ces        = sys_malloc_aligned(FAST_DATA, cache_cnt * sizeof(ce_t), sizeof(ce_t));
	_r_dtag2ces = sys_malloc_aligned(FAST_DATA, (cache_cnt-260) * sizeof(u16) , sizeof(u16));
	sys_assert(_ces);
	sys_assert(_r_dtag2ces);
	memset((void*)umgr->hash_table, 0xFF, sizeof(u16)*CE_HLIST_SZE);
	/* READER */
	//umgr->READER.ua_rd_dtag_bal = 0;
	//umgr->READER.can_cnt = 0;
	//umgr->READER.last_can_cnt = 0;
	//memset(umgr->READER.lda_bmp, 0, sizeof(umgr->READER.lda_bmp));
#if (FW_BUILD_VAC_ENABLE == mENABLE)
	extern u32* CPU1_cache_sourece;
	CPU1_cache_sourece = (u32*)_ces;
#endif

	umgr->READER.dtags = cache_read_dtags;
	for (i = 0; i < CACHE_READER_LDA_SZE; i++)
		umgr->READER.ce[i] = NULL;

	umgr->READER.can_ls_timer.function = ucache_idle_concat_flush;
	umgr->READER.can_ls_timer.data = "ucache_idle_concat_flush";
	mod_timer(&umgr->READER.can_ls_timer, jiffies + HZ/10);

	/* MERGER */
	INIT_LIST_HEAD(&umgr->MERGER.head);
	INIT_LIST_HEAD(&umgr->MERGER.head2);
	umgr->MERGER.size = 0;

	CBF_INIT(&ucache_flush_ntf_que);
	//ucache_flush_recv_hook((cbf_t *) &ucache_flush_ntf_que, ucache_flush_check);

	evt_register(ucache_flush_check, 0, &evt_cache_force_flush);
	/* UPDATER */
	umgr->UPDATER.size = 0;
	umgr->UPDATER.head = umgr->UPDATER.tail = NULL;

	umgr->FUALIST.size = 0;
	umgr->FUALIST.head = umgr->FUALIST.tail = NULL;
	umgr->FUALIST.warning_cnt = 15;
	umgr->FUALIST.fua_ls_timer.function = ucache_idle_chk_fua_list;
	umgr->FUALIST.fua_ls_timer.data = "ucache_idle_chk_fua_list";
	mod_timer(&umgr->FUALIST.fua_ls_timer, jiffies + HZ);
	//umgr->UPDATER.updt_cnt = 0;
	//umgr->UPDATER.last_updt_cnt = 0;
	//INIT_LIST_HEAD(&umgr->UPDATER.head);
	//INIT_LIST_HEAD(&umgr->READER.w8ing);
	//INIT_LIST_HEAD(&umgr->READER.can_ls);
	//umgr->READER.rdp_list_head = umgr->READER.rdp_list_tail = NULL;
	umgr->READER.w8ing_head = umgr->READER.w8ing_tail = NULL;
	umgr->READER.can_ls_head = umgr->READER.can_ls_tail = NULL;
#if 0
	//umgr->UPDATER.updt_timer.function = ucache_idle_update;
	//umgr->UPDATER.updt_timer.data = "ucahe_idle_update";
	//mod_timer(&umgr->UPDATER.updt_timer, jiffies + 3 * HZ);
	umgr->UPDATER.fctx = NULL;
	umgr->UPDATER.flush_timer.function = ucache_flush_update;
	umgr->UPDATER.flush_timer.data = "ucahe_flush_update";
#endif

#if 0//(TRIM_SUPPORT == DISABLE)
//u32 nlda = LBA_TO_LDA(nlba);
	u32 nlda=( (nlba) >> (LDA_SIZE_SHIFT - host_sec_bitz));//joe add sec size 20200820
	u32 nrange = occupied_by(nlda, RDISK_RANGE_SIZE);
	umgr->range.size = nrange;
	nrange = occupied_by(nrange, 8);
	nrange = round_up_by_2_power(nrange, 4);
	sys_assert(shr_range_size == nrange);
	INIT_LIST_HEAD(&umgr->range.req);
	//umgr->range.flags.all = 0;
#if defined(DISABLE_HS_CRC_SUPPORT) && !defined(USE_CRYPTO_HW)
	umgr->range.flags.b.cpu_trim = true;
#endif
	umgr->range.flags.b.clean = true;
	//umgr->range.wnum = 0;
	//umgr->range.fnum = 0;
	umgr->range.wbmp = sys_malloc(SLOW_DATA, nrange);
	sys_assert(umgr->range.wbmp);

	umgr->range.fbmp = (u32*)((u32)umgr->range.wbmp + nrange);

	umgr->range.fcnt = sys_malloc(SLOW_DATA, umgr->range.size);
	sys_assert(umgr->range.fcnt);
	evt_register(ucache_trim_range_issue, 0, &evt_trim_range_issue);
#endif
	cpu_msg_register(CPU_MSG_DTAG_GET_ASYNC_DONE, ucache_dtag_get_async);
	cpu_msg_register(CPU_MSG_UCACHE_READ_ERROR, ipc_ucache_read_error);
	ftl_core_gc_ces = tcm_local_to_share(&_ces);    ///< ce entries
	//static slow_data_ni merger_cat_ctx_t _cat_ctx[CAT_CTX_CNT];   ///< concat context
	//sram_sh_data ucache_mgr_t _umgr;      ///< cache manager unit
	//sram_sh_data ucache_mgr_t *umgr = &_umgr; ///< cache manager
	ftl_core_gc_umgr = tcm_local_to_share(&_umgr);
	lock_ce_hash0 = INV_U16;
	lock_ce_hash1 = INV_U16;
	evt_register(evt_get_pend_dtag_func, 0, &evt_get_pend_dtag);
#if 0//(TRIM_SUPPORT == DISABLE)
	cpu_msg_register(CPU_MSG_UCACHE_SUSP_TRIM, ipc_ucache_suspend_trim);
	cpu_msg_register(CPU_MSG_UCACHE_RESM_TRIM, ipc_ucache_resume_trim);
#endif
	ucache_resume();
	local_item_done(cache_init);
	return 0;
}

ddr_code void ucache_free_unused_dtag(void)
{
	if (pend_dtag_mgr->availa_cnt)
	{
		if (cache_list_empty(umgr->READER.w8ing_head))
		{
			disp_apl_trace(LOG_ALW, 0xc647, "[PDTG] %d",pend_dtag_mgr->availa_cnt);
			u8 cnt = pend_dtag_mgr->availa_cnt;
			u8 i;
			for(i=0; i<cnt; i++){
				pend_dtag_mgr->availa_cnt--;
				pend_dtag_mgr->free_cnt++;
				dtag_pend* dtag_pending = list_first_entry(&pend_dtag_mgr->entry1, dtag_pend, pending);
				list_del_init(&dtag_pending->pending);
				list_add_tail(&dtag_pending->pending, &pend_dtag_mgr->entry2);

				sys_assert(!list_empty(&pend_dtag_mgr->entry2));
				ucache_read_cont((void *) dtag_pending->pl);
			}
		}
		else
		{
			disp_apl_trace(LOG_ALW, 0x8acf, "[TODO]");
		}
	}
	else
	{
		disp_apl_trace(LOG_DEBUG, 0x8df6, "[DON]");
	}

	evt_task_cancel(evt_get_pend_dtag);
}

fast_code bool ucache_reader_check(void)
{
	bool ret = (umgr->READER.rs_cnt == umgr->READER.rr_cnt);
	return ret;
}
ddr_code void ucache_dump(void)
{
    disp_apl_trace(LOG_ALW, 0xd206, "CE pool C(%d) s(%d/%d)", ucache_clean(), umgr->ce_pool.size - umgr->ce_pool.avail, umgr->ce_pool.size);
    disp_apl_trace(LOG_ALW, 0x6fd9, "MERGER: s(%d) ing(%d) can(%d)", umgr->MERGER.size, umgr->MERGER.mergeing_size,umgr->READER.can_cnt);
    disp_apl_trace(LOG_ALW, 0xbd81, "READER: S/R (0x%x/0x%x)", umgr->READER.rs_cnt, umgr->READER.rr_cnt);
    disp_apl_trace(LOG_ALW, 0x9b62, "UPDATER: s(%d) FUALIST: %d fua_cmd:%d", umgr->UPDATER.size,umgr->FUALIST.size,otf_fua_cmd_cnt);
    disp_apl_trace(LOG_ALW, 0xfaa7, "io fetch status: (0x%x) cache_handle_dtag_cnt:%d", (*(volatile u32 *)0xc0032014),cache_handle_dtag_cnt);
}
ddr_code void ucache_dtag_check(bool show_log)
{
	 u16 hash_idx = 0;

	u16 ce_want = 0 , ce_read = 0, ce_merge = 0 ,ce_dirty = 0,ce_l2p = 0,ce_inv = 0,ce_hold = 0,ce_start = 0,ce_can = 0;
	 for (hash_idx = 0; hash_idx < CE_HLIST_SZE; hash_idx++)
	 {
	        if (umgr->hash_table[hash_idx] != INV_U16)
	        {
	            ce_t* ce = ce_index_2_ce(umgr->hash_table[hash_idx]);
	            while(1)
	            {
	            	do{
	            		switch(ce->state)
	            		{
							case CE_ST_WANT :
								ce_want++;
							break;
							case CE_ST_READ :
								ce_read++;
							break;
							case CE_ST_MERGE :
								ce_merge++;
							break;
							case CE_ST_DIRTY :
								ce_dirty++;
							break;
							case CE_ST_L2UP :
								ce_l2p++;
							break;
							case CE_ST_INVALID :
								ce_inv++;
							break;
							case CE_ST_HOLD :
								ce_hold++;
							break;
							case CE_ST_START :
								ce_start++;
							break;
							case CE_ST_CONCAT :
								ce_can++;
							break;
	            		}
	            	}while(0);

					if(show_log)
					{
						disp_apl_trace(LOG_ALW, 0x397e,"ce_state:%d lda:0x%x btag:%d ce_id:%d ce->dtag:0x%x d_id:0x%x",
							ce->state,ce->lda,ce->btag,CE_ID(ce),ce->dtag.dtag,ce->dtag.b.dtag);
					}

	                if (ce->entry_next == INV_U16)
	                    break;
	                ce = ce_index_2_ce(ce->entry_next);
	            }
	        }
	}

	disp_apl_trace(LOG_ALW, 0xb180,"w:%d r:%d m:%d d:%d l:%d",ce_want,ce_read,ce_merge,ce_dirty,ce_l2p);
	disp_apl_trace(LOG_ALW, 0x99c6,"i:%d h:%d s:%d c:%d",ce_inv,ce_hold,ce_start,ce_can);

}

#ifdef RD_FAIL_GET_LDA
ddr_code lda_t ucache_srch_ua_lda(u16 ofst)
{
	return umgr->READER.ce[ofst]->lda;
}
#endif

#if 0
static fast_code int ucache_main(int argc, char *argv[])
{
	if (umgr == NULL)
		return 0;

	disp_apl_trace(LOG_ERR, 0x7871, "\n CE pool C(%d) s(%d/%d)\n", ucache_clean(), umgr->ce_pool.size - umgr->ce_pool.avail, umgr->ce_pool.size);
	disp_apl_trace(LOG_ERR, 0xb8b3, "MERGER: s(%d)\n", umgr->MERGER.size);
	disp_apl_trace(LOG_ERR, 0x8ee3, "READER: S/R (0x%x/0x%x) W8ing(%d) CAN_LS(%d)\n", umgr->READER.rs_cnt, umgr->READER.rr_cnt,
			!cache_list_empty(umgr->READER.w8ing_head), !cache_list_empty(umgr->READER.can_ls_head));
	disp_apl_trace(LOG_ERR, 0x4231, "Dtags SRAM: free (%d)\n", dtag_get_avail_cnt(RDISK_PAR_DTAG_TYPE));
	return 1;
}

static DEFINE_UART_CMD(ucache_main, "ucache", "ucache",
		"ucache status", 0, 0, ucache_main);
#else
static ddr_code int ucache_main_dump(int argc, char *argv[])
{
	ucache_dump();
	ucache_dtag_check(1);
	return 1;
}

static DEFINE_UART_CMD(ucache, "ucache", "ucache",
		"ucache status", 0, 0, ucache_main_dump);


#endif





static ddr_code int fua_main(int argc, char *argv[])
{
	extern volatile bool esr_err_fua_flag;
	esr_err_fua_flag ^= 1;
	disp_apl_trace(LOG_ALW, 0x295a,"fua:%d",esr_err_fua_flag);
	return 1;
}

static DEFINE_UART_CMD(fua, "fua", "fua",
		"ucache status", 0, 0, fua_main);



