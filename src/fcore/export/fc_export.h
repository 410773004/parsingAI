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

/*! \file fc_export.h
 * @brief define export function and data structures in ftl core
 *
 *
 * \addtogroup fcore
 * \defgroup export
 * \ingroup fcore
 * @{
 */
#pragma once
#include "queue.h"
#include "ftltype.h"
#include "ftl_error.h"
#include "bf_mgr.h"
#include "pool.h"
#include "l2p_mgr.h"
#include "ddr.h"
#include "mpc.h"
#include "cbf.h"
#include "misc.h"
#include "ns.h"
#include "../ncl_20/nand_cfg.h"
#include "../ncl_20/eccu.h"


// #ifndef RAID_SUPPORT
// #define RAID_SUPPORT	0	///< RAID default is disabled
// #endif
//#define SKIP_PROGERR_HANDLE    // define ERRHANDLE_GLIST
#define DU_SEMI_OFST    6
#define DU_SEMI_MASK    0x3F

#define POWER_ON_OPEN 	(DISABLE)

#define FTL_CORE_NRM	0	///< ftl core type: normal
#define FTL_CORE_GC   	1	///< ftl core type: gc
#define FTL_CORE_SLC    2   ///< ftl core type: slc
#define FTL_CORE_PBT	2   ///< ftl core type: PBT
#define FTL_CORE_QBT	2   ///< ftl core type: QBT
#define FTL_CORE_MAX	3

#define MAX_ASPB_IN_PS	1		///< max active spb count in physical stream	pstream only supply 1aspb (pis)

#define DTAG_COMT_OP_NRM		0	///< normal update
#define DTAG_COMT_OP_TRIM_RANGE		1	///< range trim

#if defined(USE_MU_NAND)
#define FTL_CORE_EXP_FACT	2		///< ftl core expand factor
#else
#define FTL_CORE_EXP_FACT	1		///< ftl core expand factor
#endif

#define MAX_TEMP 80
#define MIN_TEMP 25

#define WARMBOOT_FTL_HANDLE	(mENABLE)
#define WA_FW_UPDATE (mDISABLE)
enum{
	pad_normal = 0,
	pad_por,
	pad_plp,
	pad_max
};

enum {
	ps_not_avail = 0,
	blk_list_flushing,
	parity_allocate_not_enough,
	wreqs_not_enough,
	cwl_finished_not_start_new,
	spor_flush_qbt,
	no_reason = 0xFF
};


/*!
 * @brief ftl io meta data structure
 */
typedef struct du_meta_fmt io_meta_t;

/*!
 * @brief spb run time flags, only used in runtime
 */
typedef union _spb_rt_flags_t {
	struct {
		u8 type : 1;	///< 1 for SLC, 0 for NATIVE
		u8 open : 1;	///< open flag
	} b;
	u8 all;
} spb_rt_flags_t;

/*!
 * @brief fc namespace attribute, shared due to 2 BE
 */
typedef union _fcns_attr_t {
	u32 all;
	struct {
		u32 p2l : 1;			///< if P2L was enabled
		u32 raid : 1;			///< if RAID was enabled
	} b;
} fcns_attr_t;

/*!
 * @brief ftl core context
 */
typedef struct _ftl_core_ctx_t {
	QSIMPLEQ_ENTRY(_ftl_core_ctx_t) link;	///< link list entry
	void *caller;
	void (*cmpl)(struct _ftl_core_ctx_t *);
} ftl_core_ctx_t;

/*!
 * @brief ftl context to flush user data
 */
typedef struct _ftl_flush_data_t {
	ftl_core_ctx_t ctx;			///< ftl core context entry
	u32 nsid;				///< namespace id
	union {
		struct {
			u32 dtag_gc : 1;	///< flag indicate dtag GC
			u32 shutdown : 1;	///< flag to shutdown system
			u32 format : 1;		///< flag to format system
			u32 finished : 1;	///< flag to indicate ftl core flush was done
			u32 plp : 1;        ///< plp force flush trigger
		} b;
		u32 all;
	} flags;
} ftl_flush_data_t;

/*!
 * @brief ftl context to flush l2p table
 */
typedef struct _ftl_flush_tbl_t {
	ftl_core_ctx_t ctx;			///< ftl core context entry
	u16 nsid;				///< namespace id
	spb_id_t gc_spb;			///< gc spb id
	union {
		struct {
			u32 shutdown : 1;	///< if triggered by shutdown command
			u32 flush_all : 1;	///< if flush all table no matter dirty or clean
			u32 wait_next_blk : 1;
			u32 pbt_dump : 1;
			u32 pbt_filldummy : 1;
			u32 partial_trim : 1;
		} b;
		u32 all;
	} flags;
	u8  tbl_meta;
	u8	pbt_mode;
	u32 next_bit;				///< next flushing segment bit
	//u32 io_cnt;				///< issued io count
	u32 seg_cnt;				///< segment flushed count
} ftl_flush_tbl_t;

/*!
 * @brief ftl context to flush misc data (valid count and valid bitmap)
 */
typedef struct _ftl_flush_misc_t {
	ftl_core_ctx_t ctx;			///< ftl core context entry
	u32 dtag_start[2];				///< start dtag of misc data
	u16 dtag_cnt[2];				///< dtag count of misc data
	u32 seg_start;				///< start segment of misc data stored
	u16 seg_cnt;				///< segment count of misc data
	u16 idx;				///< flush index (in segment)
	u16 io_cnt;				///< running io of misc data
	u16 nsid;
	u16 type;
	u8  blklist_dtag_cnt;
} ftl_flush_misc_t;


// ISU, LJ1-337, PgFalClsNotDone (1)
#define FILL_TYPE_WL       1	
#define FILL_TYPE_LAYER    2
#define FILL_TYPE_BLK      3
#define FILL_TYPE_CLOSE_WL 4

#define FILL_TYPE_RD_OPEN   FILL_TYPE_LAYER // fill dummy type of read disturb hit open block

typedef struct _ftl_spb_pad_t {
	ftl_core_ctx_t ctx;			///< ftl core context entry
    void (*private_cmpl)(struct _ftl_spb_pad_t *);
    //void *caller;    // Use caller in ftl_core_ctx_t ctx
    u32  dummy_req_cnt;
    u32  dummy_done_cnt;
	u32  spb_id;
	u32  spb_pad_idx;
	bool pad_done;
	u64  start;
	u8   pad_attribution;
    struct {
        u8 start_nsid;
        u8 start_type;
		u8 end_nsid;
		u8 end_type;
        u8 cwl_cnt;
        u8 pad_all;
    } param;

    struct {
		u8 nsid;
		u8 type;
		u8 cwl_cnt;
		u8 pad_stripe;
	} cur;
} ftl_spb_pad_t;

typedef struct _plp_spb_pad_t {
	ftl_core_ctx_t ctx;			///< ftl core context entry

    void (*private_cmpl)(struct _plp_spb_pad_t *);

    bool task_fill;
    u32  dummy_req_cnt;
    u32  dummy_done_cnt;
    struct {
        u8 nsid;
        u8 type;
        u8 cwl_cnt;
        u8 pad_all;
    } param;

    struct {
		u8 nsid;
		u8 type;
		u8 cwl_cnt;
		u8 pad_stripe;
	} cur;

	u32 ns_bitmap;

	u8 start_type;
	u8 type_cnt;
	u8 pad_cwl_cnt;

	spb_id_t pad_to_end_spb_id;
} plp_spb_pad_t;


typedef struct _spb_fence_t {
	u16 spb_id;
	union {
		struct {
			u16 valid : 1;
			u16 closed : 1;
			u16 slc : 1;
		} b;
		u16 all;
	} flags;

	u32 sn;
	u32 ptr;
} spb_fence_t;

/*!
 * @brief ftl fence, it's reconstruction start point
 */
typedef struct _ftl_fence_t {
	ftl_core_ctx_t ctx;			///< ftl core context entry
	u32 nsid;				///< namespace id
	union {
		u32 all;
		struct {
			u32 clean : 1;		///< clean boot, only used in restore
		} b;
	} flags;

	spb_fence_t spb_fence[FTL_CORE_MAX][MAX_ASPB_IN_PS];	///< spb fence in a namespace
} ftl_fence_t;

typedef struct _pbt_resume_t {
    u32 cur_seg_ofst;
    u32 pbt_cover_usr_cnt;
    u8 fill_wl_cnt; 

    struct
    {
        spb_id_t spb_id;
        u8 open_skip_cnt;
        u8 total_bad_die_cnt;  
#ifdef RAID_SUPPORT
        u8 parity_die;
	    u8 parity_die_pln_idx;
#endif
        u32 ptr;
        sn_t sn;
        union{
            struct{
                u8 slc:1;
                u8 valid:1;
                u8 resume_pbt:1;
                u8 wb_flushing_pbt:1;
#ifdef RAID_SUPPORT
                u8 parity_mix : 1;
#endif
            }b;
            u8 all;
        }flags;
    }pbt_info;

    u16 sw_flag;
}pbt_resume_t;

typedef struct _ftl_format_t {
	ftl_core_ctx_t ctx;
	union {
		u32 all;
		struct {
			u32 host_meta : 1;
			u32 preformat_erase : 1;
			u32 full_trim : 1;
			u32 del_ns : 1;
		} b;
	} flags;
	u32 nsid;
} ftl_format_t;

/*!
 * @brief union data structure of ftl core context
 */
typedef struct _ftl_core_union_t {
	union {
		ftl_flush_data_t data;
		ftl_flush_tbl_t tbl;
		ftl_fence_t fence;
		ftl_flush_misc_t misc;
		ftl_spb_pad_t pad;
		ftl_format_t format;
	};

	u32 tx;
} ftl_core_union_t;

typedef union _comt_op_dtag_t {
	u32 dtag;
	struct {
		u32 dtag   	:22;	///< dtag id
		u32 type	: 1;	///< 0 = sram type, 1 = hmb type; when .in_ddr = 1 , dtag = (.type<<22) + .dtag */
		u32 in_ddr 	: 1;	///< access to DDR
		u32 type_ctrl   : 6;	///< refer to usage
		u32 rop		: 2;	///< refer to range operation code
	} b;
} comt_op_dtag_t;

typedef union _dtag_comt_entry_t {
	dtag_t dtag;
	//comt_op_dtag_t dtag_op;
} dtag_comt_entry_t;

#define DTAG_COMT_DEPTH 	(950)
typedef CBF(dtag_comt_entry_t, DTAG_COMT_DEPTH) dtag_comt_que_t;

typedef struct _dtag_comt_t {
	dtag_comt_que_t que;
	u32 lda[DTAG_COMT_DEPTH];
} dtag_comt_t;

typedef struct _ftl_trim_range_t {
	u32 slda;			///< start lda of trim range
	u32 len;			///< length of trim
} ftl_trim_range_t;

typedef struct _ftl_trim_t {
	ftl_trim_range_t range[256];	///< trim range
	u16 opc;			///< op code
	void *caller;			///< caller for trim
	u32 num;			///< number of range
	u32 done;			///< number of trimed range
	struct list_head entry;		///< list entry
} ftl_trim_t;

/*! @brief header of ftl_core_xxx api from other cpu */
typedef struct _ftl_core_ctx_ipc_hdl_t {
	ftl_core_ctx_t ctx;		///< local ftl core context, will copy remote context to local
	u8 tx;				///< command tx
} ftl_core_ctx_ipc_hdl_t;

/*!
 * @brief definition of ftl remap
 */
typedef struct _ftl_remap_t {
	u16 interleave;		///< interleave of remap table
	u16 remap_cnt;
	u16 *remap_idx;		///< remap table index for each spb, 0xFFFF mean no remap table
	spb_id_t *remap_tbl;	///< remap table for remapped spb
} ftl_remap_t;

typedef CBF(u64, 16) spb_queue_t;
typedef CBF(u32, 64) gc_dtag_ret_que_t;
typedef CBF(u32, 1024) dtag_free_que_t;

#define WUNC_FLAG	BIT(31)			///< bit for WUNC done
#define DTAG_FREE_RANGE_TRIM_DONE_BIT	BIT(30)			///< bit for range trim done
#define DTAG_FREE_UPDT_ABORT_BIT	BIT(29)			///< bit for update aborted

#define DTAG_FREE_VALID_MSB		BIT(24)				///< valid msb
#define DTAG_FREE_VALID_MASK		(DTAG_FREE_VALID_MSB - 1)	///< msb mask
/*!
 * @brief definition of raid correction request
 */

typedef struct _rc_dtag_t { // rc req dtag info
    struct _rc_dtag_t * prev;
    dtag_t dtag;
    u8* mem;
    u16 w_ptr;
    u16 r_ptr;
} rc_dtag_t;

typedef struct _rc_req_t {
	QSIMPLEQ_ENTRY(_rc_req_t) link;		///< request link

	u32 list_len;				///< list length
	pda_t *pda_list;			///< pda list, original pda list from ncl command
	bm_pl_t *bm_pl_list;			///< bm payload list, original bm_pl_list from ncl command
	struct info_param_t *info_list;		///< info list, original info from ncl command

	void *caller_priv;			///< caller context
	void (*cmpl)(struct _rc_req_t *);	///< completion when all error pda was corrected

	union {
		u32 all;
		struct {
			u32 host : 1;		///< host data or table
			u32 stream : 1;		///< if streaming read
			u32 gc : 1;		///< GC data or not
			u32 fail : 1;		///< if read error happened
			u32 remote : 1;		///< request from remote cpu
			u32 put_ncl_cmd : 1;///< put ncl_cmd after rc done
		} b;
	} flags;

	u32 ack_cpu;				///< tx command
	rc_dtag_t* rc_dtag;
	u16 mem_len;
} rc_req_t;

enum {
#if CPU_ID == CPU_BE
	GC_LOCAL_PDA_GRP = 0,
	GC_REMOTE_PDA_GRP = 1,
#else
	GC_LOCAL_PDA_GRP = 1,
	GC_REMOTE_PDA_GRP = 0,
#endif
	MAX_GC_PDA_GRP = 2,
	GC_PDA_GRP_SHIFT = 1,
};

typedef struct _gc_pda_grp_t {
	u32 spb_id;
	u32 vcnt;	///< total received valid pda count
	u32 done;	///< total gc done du count
	u32 reading;	///< outstanding reading count
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	u32 dummy_cnt;	///< total gc read slc dummy du count
	u32 defect_cnt;	///< total gc slc defect du count
	pda_t slc_pda;
#endif	
	bool pda_in;
	union {
		struct {
			u32 slc : 1;	///< if current GC spb is slc or not
			u32 abort : 1;	///< abort this group
		} b;
		u16 all;
	} flags;

	union {
		struct {
			u32 remote : 1;	///< if this group is not handled in ftl core
		} b;
		u16 all;
	} attr;
} gc_pda_grp_t;

typedef struct _gc_res_free_t {
	u16 rsp_idx;
	u16 dtag_idx;
} gc_res_free_t;

#define DU_CNT_PER_P2L		(NAND_DU_SIZE >> 2)
#define DU_CNT_PER_P2L_SHIFT	ctz(DU_CNT_PER_P2L)
#define DU_CNT_PER_P2L_MASK	(DU_CNT_PER_P2L - 1)

#define MAX_GC_INTELEAVE	128
#define MAX_DIE_MGR_CNT		32//64

#define FCORE_GC_DTAG_CNT	(3072)//(min(shr_nand_info.interleave, 128) * DU_CNT_PER_PAGE * XLC*2) ///< can be decreased according resource
#define DTAG_CNT_PER_GRP	(FCORE_GC_DTAG_CNT >> GC_PDA_GRP_SHIFT)
#define DTAG_MAX_CNT_PER_GRP	((FCORE_GC_DTAG_CNT >> GC_PDA_GRP_SHIFT) + 1) //2049 for free que size
#define VPDA_RSP_QUE_SIZE   ((DTAG_SZE * 3) - DTAG_CNT_PER_GRP)

#define PDA_RSP_PER_GRP		(737)//(1536+201)//(DU_CNT_PER_P2L	+ MAX_PLANE * DU_CNT_PER_PAGE * XLC)
#define RD_CNT_SIZE_PER_GRP (1024)
#define PEND_BUFF_CNT		(FCORE_GC_DTAG_CNT+1)
#define NUM_P2L_GEN_PDA     (2)//(1)
#define MAX_GC_RES_PER_GRP     (nand_info.lun_num)//8   ///< max gc resource per group

#define MAX_MP_DU_CNT		(MAX_PLANE * DU_CNT_PER_PAGE)
#define DDR_RAID_DTAG_START  (DDR_GC_DTAG_START + FCORE_GC_DTAG_CNT + 32)
#define DDR_RAID_DTAG_CNT    (480) // 64 * 4

#define DDR_P2L_DTAG_START  (DDR_RAID_DTAG_START + DDR_RAID_DTAG_CNT)
#define MAX_P2L_CNT		((MAX_MP_DU_CNT + 2) * FTL_CORE_MAX * (TOTAL_NS - 2))	///< total p2l res count, exclude ns 0 and int ns

#define DDR_BLIST_DTAG_START  (DDR_P2L_DTAG_START + MAX_P2L_CNT + 32)

typedef CBF(bm_pl_t, 1024) gc_src_data_que_t;
typedef CBF(gc_res_free_t, 2049) gc_res_free_que_t;//tzu CBF size must be power of 2, if need more space should modify function next_cbf_idx_2n()

#define GC_ACT_SUSPEND		0
#define GC_ACT_ABORT		1
#define GC_ACT_RESUME		2
#define GC_ACT_STOP_BG_GC   3

#define GC_LOCK_POWER_ON    	(0x01)
#define SLC_LOCK_POWER_ON		(0x02)
#define FQBT_LOCK_POWER_ON		(0x04)
#define NON_PLP_LOCK_POWER_ON	(0x08)
#define READ_ONLY_LOCK_IO		(0x10)

typedef struct _gc_req_t {
	spb_id_t spb_id;	///< gc candidate spb
	u32 sn;			///< sn of gc candidate
	union {
		u32 all;
		struct {
			u32 spb_open : 1;	///< spb was not fully programmed
			u32 dslc : 1;		///< dynamic slc
			u32 wl_suspend : 1; ///< spb was suspended by WL before
			u32 spb_spor_open : 1;
			u32 spb_warmboot_open : 1;
		} b;
	} flags;
	bool spb_open;		///< spb is open spb
	completion_t done;	///< callback function when done
} gc_req_t;

typedef struct _ns_start_t {
	u16 type;			///< tx CPU
	u16 nsid;		///< action
	u32 tx;			///< tx CPU
	void* caller;		///< caller context
	void (*cmpl)(struct _ns_start_t*);	///< callback function when done
} ns_start_t;
typedef struct _gc_action_t {
	u8 tx;		///< tx CPU
	u8 act;		///< action
	union {
		struct {
			u16 set_stop : 1;		///< set gc stop flag when suspend or abort
			u16 clr_stop : 1;		///< clr gc stop flag when resume
		} b;
		u16 all;
	} flags;

	void* caller;		///< caller context
	void (*cmpl)(struct _gc_action_t*);	///< callback function when done

	#if (PLP_SUPPORT == 0)
	struct _gc_action_t* next_delay_act;  ///< to record all suspend GC action in non PLP GC
	#endif
} gc_action_t;

/*!
 * @brief ftl reset, it's context to make FTL stop response host read
 */
typedef struct _ftl_reset_t
{
	ftl_core_ctx_t ctx;

	u8 scheduler_cnt;
	u8 scheduler_idle;

} ftl_reset_t;

typedef struct _runtime_rd_cnt_idx_t {
	u32 runtime_rd_cnt_idx[RD_CNT_SIZE_PER_GRP];
} runtime_rd_cnt_idx_t;

typedef struct _runtime_rd_cnt_inc_t {
	u8 runtime_rd_cnt_idx[RD_CNT_SIZE_PER_GRP*4];
} runtime_rd_cnt_inc_t;

typedef struct _p2l_ext_para_t {
	u16 slc_p2l_cnt;
	u16 slc_grp_cnt;

	u16 xlc_p2l_cnt;
	u16 xlc_grp_cnt;

	u32 p2l_per_grp;
} p2l_ext_para_t;

typedef struct _p2l_load_req_t {
	u16 spb_id;
	u16 grp_id;	///< one p2l group is multi-plane size
	u16 p2l_id;	///< one p2l is du size
	u16 p2l_cnt;	///< loading count in this request
	dtag_t dtags[MAX_PLANE * 4];

	u32 tx;	///< msg send cpu
	void* caller;
	completion_t cmpl;

	union {
		struct {
			u16 aux : 1;
			u16 p2l_dummy : 1;  //p2l is dummy
			u16 last_wl_p2l : 1; //last grp p2l in last wl
		} b;
		u16 all;
	} flags;

	u16 p2l_dummy_grp_idx;
} p2l_load_req_t;

typedef enum
{
	DUMP_PBT_REGULAR = 0,				//0
    DUMP_PBT_NORMAL_PARTIAL,			//1. Start new PBT Group, force dump PBT with assign PBT page gap (#define PBT_FORCE_DUMP_GAP)
    DUMP_PBT_FORCE_NOT_BREAK,			//2. Start new PBT Group, Won't respond other cmds until force dump mode finish.
    DUMP_PBT_FORCE_FINISHED_CUR_GROUP,	//3. continue current PBT dump osft and force dump in assign PBT page gap (#define PBT_FORCE_DUMP_GAP)
	DUMP_PBT_RD,
    DUMP_PBT_SHUTTLE,                   //5. // ISU, EHPerfImprove
	DUMP_PBT_BGTRIM,
	DUMP_PBT_WARMBOOT,
	DUMP_PBT_MODE_MAX
}DUMP_PBT_MODE;

typedef struct
{
	DUMP_PBT_MODE 	force_dump_pbt_mode;
	u32 			cur_seg_ofst;
	u32				pbt_flush_page_cnt;
	u32				pbt_cover_usr_cnt;
	u32				pbt_flush_IO_cnt;
	u32				pbt_force_end_wptr;
	u32				total_write_tu_cnt;
	u32				pbt_flush_total_cnt;
	u32				total_write_page_cnt;
	u16         	pbt_spb_cur;
	u8				tbl_res_pool_cnt;
    u8				pbt_cur_loop;
	u8				force_cnt;
    u8              fill_wl_cnt; 
	bool			force_dump_pbt;
	bool			isfilldummy;
	bool			filldummy_done;
	bool        	pbt_res_ready;
	bool        	dump_pbt_start;
	bool        	force_pbt_halt;
}TFtlPbftType;


#if 1
typedef struct
{
    u32              SerialNumber;
    u32              TableSN;
    volatile u64     GlobalPageSN;
	u32              PrevTableSN;
    u16         	 last_host_blk;
    u16              last_gc_blk;
    u32              LastQbtSN;
	u16				 pbt_host_blk;
	u16				 pbt_gc_blk;
	u16				 pbt_pre_host_blk;
	u8               pbt_cur_loop;  // for warmboot pbt pingpong
	u8               qbt_cur_loop;
	u32				 evlog_index; //for warmboot record latest log page
}stFTL_MANAGER; // 64 Bytes
#else
typedef struct
{
    u32              SerialNumber;
    u32              TableSN;
    volatile u64     GlobalPageSN;
    u32              AllEraseCnt;
    u32              GCTLCEC;
    //tPTA                  PBT_PreOpenHostPta;
    //tPTA                  PBT_PreOpenGCPta;
    u16              PBT_PostCloseHostIdx;
    u16              PBT_PostCloseGCIdx;
    u32              HostGBWrite;
    u32              HostGBRead;
    u32 			  SMARTThrottlingEventCnt;
    u32              BackUpTotalLBA_Wr;
    u32              BackUpTotalLBA_Rd;
	u32			  PowerOnHour;
	u32			  TenMinCnt;
	u32              PrevTableSN;
	u8				 pbt_idx;
	u8               First_PBT;
	u8               TrimSkipBlock;
    u8               Force_PBT;
}stFTL_MANAGER; // 64 Bytes
#endif

extern stFTL_MANAGER gFtlMgr;
typedef union
{
    UINT16             all16;
    struct{
		UINT16 forceGC      : 1;   // BIT0 mode
        UINT16 emgrGC       : 1;   // BIT1 mode
        UINT16 lockGC       : 1;   // BIT2 mode
        UINT16 shuttleGC    : 1;   // BIT3 mode
        UINT16 bootGC       : 1;   // BIT4 mode
        UINT16 idleGC       : 1;   // BIT5 mode
        UINT16 staticWL     : 1;   // BIT6 mode
        UINT16 hostdown     : 1;   // BIT7 mode
        UINT16 DBGGC        : 1;   // BIT8 mode
        UINT16 readDisturb  : 1;   // BIT9 mode
        UINT16 dataRetention: 1;   // BIT10 mode
        UINT16 plp_slc		: 1;   // BIT11 mode
        UINT16 non_plp		: 1;   // BIT12 mode
        UINT16 reserve      : 3;   // BIT13~15 mode
    }b;
}stMODE;

typedef union
{
	UINT16			   all16;
	struct{
		UINT16 idle 		: 1;	// BIT0   // acvtied : 0 / 1(idle)
		UINT16 halt 		: 1;	// BIT1
		UINT16 reset		: 1;	// BIT2
		UINT16 stopAtPor	: 1;	// BIT3
		UINT16 bypass       : 1;    // BIT4
		UINT16 readOnly 	: 1;	// BIT5
		UINT16 hostIdle 	: 1;	// BIT6
		UINT16 reserved 	: 9;	// BIT7-15
	}b;
}stSTATE;


typedef enum
{
	GC_MD_NON = 0,       // 0
	GC_MD_FORCE,         // 1
	GC_MD_EMGR,          // 2
	GC_MD_LOCK, 	     // 3
	GC_MD_SHUTTLE,       // 4
	GC_MD_IDLE,          // 5
	GC_MD_STATIC_WL,     // 6
    GC_MD_AUX,           // 7
    GC_MD_READDISTURB,   // 8
    GC_MD_DATARETENTION, // 9
    GC_MD_CNT,           // 10
    GC_MD_DBG,           // 11
	GC_MD_PLP_SLC,       // 12
	GC_MD_NON_PLP,       // 13
    GC_MD_NUL = 0x7FFFFFFF,
}stGC_MODE;

typedef enum
{
    GC_LP = 0,
    GC_AUX,
    GC_NUL = 0x7FFFFFFF,    // Force to DWORD SIZE (4 B)
}stGC_TYPE; // 4 B


typedef struct
{
    stGC_MODE   mode;               // cur gc mode: force, idle, WL...etc.
    stGC_TYPE   type;               // cur gc type: AUX / LP
}stGC_FSM_INFO_MODE;  // 8 B

typedef struct
{
//	stGC_OP         op;                 // 20 B    20
//	stGC_BOOT       boot;               // 4 B     24
    UINT16          rdDisturbCnt;       // 2 B
    UINT16          shuttleCnt;         // 2 B     28
    UINT16          patrlrdCnt;
	UINT16          rsvd;
    stMODE          mode;               // 2 B     30
	stSTATE         state;              // 2 B     32
//    stGC_QOS_INFO   qos_Info;           // 24 B    56
//    stGC_EMPTY_INFO emptygc_Info;       // 16 B    72
}stGC_GEN_INFO;  // 80 B

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

	lda_t lda;			///< lda                //dw3
	dtag_t dtag;			///< dtag           //dw4

	u16 btag;
    union {
        u16 mrgr_indx;
        u8 wunc_bitmap;
    };

    //u16 ce_idx;                                 //dw2
    u32 nrm     : 1;            //nrm data or partial data
    u32 nlba    : 4;
    u32 ofst    : 4;
    u32 state    : 5;
    u32 before_trim : 1;
    u32 after_trim : 1;
    u32 rph_head : 16;
   // u8  rev;
} ce_t;

extern volatile stGC_GEN_INFO gGCInfo;       //GC information

extern struct nand_info_t shr_nand_info;
extern pda_t *gc_pda_list;
extern pda_t *gc_lda_list; //gc lda list from p2l

extern io_meta_t *gc_meta;
extern dtag_comt_t shr_dtag_comt;

static inline bool ftl_core_get_spb_type(u32 spb_id)
{
	extern spb_rt_flags_t *spb_rt_flags;
	return spb_rt_flags[spb_id].b.type;
}

static inline bool ftl_core_get_spb_open(u32 spb_id)
{
#if defined(USE_MU_NAND)
	extern spb_rt_flags_t *spb_rt_flags;
	return spb_rt_flags[spb_id].b.open;
#endif
	return false;
}

static inline void ftl_core_set_spb_open(u32 spb_id)
{
	extern spb_rt_flags_t *spb_rt_flags;
	spb_rt_flags[spb_id].b.open = true;
}

/*!
 * @brief flush all data entries to nand
 *
 * @param fctx		flush data context
 *
 * @return		FTL_ERR_OK, well flushed
 * 			FTL_ERR_PENDING, ctx was pended
 * 			FTL_ERR_BUSY, flushing
 */
ftl_err_t ftl_core_flush(ftl_flush_data_t *fctx);

/*!
 * @brief continue to reserve physical page resource for ftl core
 *
 * @param nsid		namespace id
 * @param type		ftl core type
 *
 * @return		not used
 */
void ftl_core_resume(u32 nsid, u32 type);

/*!
 * @brief start ftl core, should be called after ftl boot
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ftl_core_start(u32 nsid);

/*!
 * @brief flush parity (RAID)
 *
 * @param stripe_id	stripe id to be flushed
 *
 * @return		not used
 */
void ftl_core_pout_and_parity_flush(u32 stripe_id);
void ftl_core_parity_flush(u32 stripe_id);
bool ftl_core_flush_tbl(ftl_flush_tbl_t *ctx);
bool ftl_core_flush_misc(ftl_flush_misc_t *ctx);
bool ftl_core_spb_pad(ftl_spb_pad_t *pctx);
void ftl_core_format(ftl_format_t *format);
void ftl_core_qbt_alloc(ns_start_t *ctx);
void pbt_flush_proc(void);
bool ftl_core_flush_blklist(ftl_flush_misc_t *ctx);
bool ftl_core_flush_blklist_porc(u16 type);
void pstream_force_close(u16 close_blk);	// ISU, Tx, PgFalClsNotDone (1)




/*!
 * @brief ftl core update fence
 *
 * @param fence		fence to be updated
 *
 * @return		not used
 */
void ftl_core_update_fence(ftl_fence_t *fence);

/*!
 * @brief restore fence to open SPB after clean boot
 *
 * @param fence		restored fence
 *
 * @return		not used
 */
void ftl_core_restore_fence(ftl_fence_t *fence);

/*!
 * @brief restore spb runtime flag, called after reconstruction
 *
 * @param rt_flags	runtime flags to be restore
 */
void ftl_core_restore_rt_flags(spb_rt_flags_t *rt_flags);

/*!
 * @brief interface to assign recovery dtag
 *
 * @param dtag		dtag to assign
 */
void raid_correct_got_dtag(u32 dtag);

/*!
 * @brief start to GC a spb
 *
 * @param gc_req	gc request
 *
 * @return		not used
 */
void ftl_core_gc_start(gc_req_t *gc_req);
void tzu_get_gc_info();
spb_id_t fc_get_gc_spb();
bool is_gc_write_done_when_plp();

void clean_rd_cnt_counter(spb_id_t spb_id);

ce_t* ftl_core_gc_cache_search(u32 lda);
/*!
 * @brief handler of GC data entries, insert data entries to write payload
 *
 * @param cnt		list length
 * @param bm_pl_list	GC data entries
 *
 * @return		return false if data was pended
 */
bool ftl_core_gc_data_in(u32 cnt, bm_pl_t *bm_pl_list, u32 *handled_cnt);

/*!
 * @brief start gc valid pda read and write when receive valid pda end of mark
 *
 * @param grp		gc scheduler group
 *
 * @return		not used
 */
void ftl_core_gc_xfer_data(u32 grp);

bool ftl_core_spb_pad(ftl_spb_pad_t *pctx);
/*!
 * @brief push raid correct request to pend queue
 *
 * @param req	raid correct req
 *
 * @return	none
 */
void raid_correct_push(rc_req_t *req);

/*!
 * @brief notify ftl core gc group gid is done
 *
 * @param grp		group id
 *
 * @return		not used
 */
void ftl_core_gc_grp_rd_done(u32 grp);

/*!
 * @brief ftl core gc suspend/resume
 *
 * @param act		gc action context
 *
 * @return		true if action done
 */
bool ftl_core_gc_action(gc_action_t *act);
#if(PLP_GC_SUSPEND == mDISABLE)
bool ftl_core_gc_action2(gc_action_t *act);//joe for del ns 202011
#endif

/*!
 * @brief gtl core gc pmu resume func, init gc resources
 *
 * @return		true if action done
 */
void ftl_core_gc_resume(void);

/*!
 * @brief load p2l into dtags
 *
 * @param load_req	p2l load request, it includes detailed infomation
 *
 * @return	not used
 */
void ftl_core_p2l_load(p2l_load_req_t *load_req);
void p2l_get_grp_pda(u32 grp_id, u32 spb_id, u32 nsid, pda_t* p2l_pda, pda_t* pgsn_pda);

/*!
 * @brief check if fc namespace support P2L
 *
 * @param nsid		namespace id
 *
 * @return		return true if support
 */
static inline bool fcns_p2l_enabled(u32 nsid)
{
	extern volatile fcns_attr_t fcns_attr[INT_NS_ID + 1];
	return fcns_attr[nsid].b.p2l;
}

/*!
 * @brief check if fc namespace RAID was enabled or disabled
 *
 * @param nsid		namespace id
 *
 * @return		return true if raid enabled, return false if raid disabled
 */
static inline bool fcns_raid_enabled(u32 nsid)
{
	extern volatile fcns_attr_t fcns_attr[INT_NS_ID + 1];
	return fcns_attr[nsid].b.raid;
}

/*!
 * @brief api to return read recoveried dtag to scheduler
 *
 * @param dtag		it should be recoveried dtag
 *
 * @return		not used
 */
void read_recoveried_done(dtag_t dtag);

/*!
 * @brief receive scheduler idle done
 *
 * @param grp		group id
 * @param reset		reset context
 *
 * @return		not used
 */
void ftl_core_reset_wait_sched_idle_done(u32 grp, ftl_reset_t *reset);

/*!
 * @brief notify ftl core pcie was reset, start to drop stream entry, and wait stream read idle
 *
 * @return 	not used
 */
void ftl_core_reset_notify(void);
void plp_cancel_die_que(void);

/*!
 * @brief notify ftl core pcie was already reset done, fe <-> ftl sync function
 *
 * @return	not used, completed reset when stream idle done
 */
void ftl_core_reset_done_notify(void);
void ipc_suspend_gc(volatile cpu_msg_req_t *req);
void ipc_control_gc(volatile cpu_msg_req_t *req);


/*! @} */

