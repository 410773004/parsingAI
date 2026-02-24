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
/*! \file ftl.h
 * @brief ftl
 *
 * \addtogroup ftl
 * \defgroup ftl
 * \ingroup ftl
 * @{
 * initialize and start ftl module, it will create default namespace,
 * spb manager and spb pool.
 */
#pragma once

#include "types.h"
#include "dtag.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl.h"
#include "mpc.h"

#define DSLC_SPB_RATIO 0	/// < dynamic SLC max SLC SPB percentage, if reach then dynamic disable
#define XLC_SPB_RATIO	100	/// < dynamic SLC max XLC SPB percentage, if reach then dynamic disable

typedef enum {
	FTL_BOOT_NRM,
	FTL_BOOT_VIRGIN_DROP_SYS_LOG,
	FTL_BOOT_VIRGIN_DROP_SPB_LOG,
	FTL_BOOT_VIRGIN_DROP_ALL_PBLK,
	FTL_BOOT_VIRGIN_DROP_FRB
} ftl_boot_lvl_t;

typedef enum
{
    FTL_INITIAL_PREFORMAT=0,
    FTL_INITIAL_POWER_ON,
    FTL_INITIAL_FULL_TRIM,
    FTL_INITIAL_NO_HOST_DATA,
    FTL_INITIAL_MAX,
} ftl_initial_mode_t;

typedef struct _pard_cmd_t{
    struct ncl_cmd_t pard_read_ncl_cmd;
    pda_t pard_pda[XLC];
    bm_pl_t pard_pl[XLC];
    struct info_param_t pard_info_list[XLC];
}pard_cmd_t;

typedef struct _pard_mgr_t{

    bool do_gc;           //do gc flag
    bool sblk_change;
    u8 patrol_times;        //divide a spb into multiple treatments
    u8 pool_id;
    u16 patrol_blk;    //super block to patrol read
    u16 prev_blk; 
    u32 prev_sn;
	u32 pwron_max_sn;
    u32 pard_undone_cnt; //undone patrol read ncl cmd count
}pard_mgr_t;

struct _flush_ctx_t;

extern u8 srb_reserved_spb_cnt;
extern ftl_boot_lvl_t ftl_boot_lvl;
extern bool ftl_virgin;
extern u32 sram_dummy_meta_idx;
extern u32 ddr_dummy_meta_idx;
extern volatile ftl_flags_t shr_ftl_flags;
extern bool first_usr_open;
extern volatile bool shr_qbtflag;
extern bool pbt_query_ready;
extern bool FTL_NO_LOG;
#ifdef Dynamic_OP_En
#define EPM_SET_OP_TAG    ( 0x534F5053 ) // 'SOPS'
#endif

/*!
 * @brief get ftl IO buffer, used for ftl misc data
 *
 * @param size		required buffer size
 *
 * @return		none
 */
void *ftl_get_io_buf(u32 size);

/*!
 * @brief dump FTL main data structure
 *
 * @return		none
 */
void ftl_dump(void);

/*!
 * @brief get dtag count from a memory size
 *
 * @param size	size of memory
 *
 * @return	dtag count of size
 */
static inline u32 ftl_sram_sz_in_dtag(u32 size)
{
	return occupied_by(size, (1 << DTAG_SHF));
}

/*!
 * @brief convert a memory pointer to dtag list
 *
 * @param ptr		memory pointer
 * @param list		dtag list buffer
 * @param list_len	dtag list length
 *
 * @return		none
 */
static inline void ftl_sram_to_dtag_list(void* ptr, dtag_t* list, u32 list_len)
{
	u32 i = 0;
	u8* p = (u8*) ptr;

	do {
		list[i] = mem2dtag((void*)p);
		//ftl_apl_trace(LOG_ERR, 0, "%p %d\n", p, list[i].dtag);

		p += (1 << DTAG_SHF);
	} while (++i < list_len);
}

/*!
 * @brief helper function to setup write data entry in FTL
 *
 * @param bm_pl		BM payload
 * @param dtag_id	dtag
 *
 * @return		none
 */
static inline void ftl_wr_bm_pl_setup(bm_pl_t *bm_pl, u32 dtag_id)
{
	bm_pl->all = dtag_id;
	bm_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
}

static inline void ftl_bm_pl_setup(bm_pl_t *bm_pl, dtag_t dtag)
{
	bm_pl->all = dtag.dtag;
	bm_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
}

static inline void ftl_prea_idx_meta_setup(bm_pl_t *bm_pl, u32 meta_idx)
{
	if (bm_pl->pl.dtag & DTAG_IN_DDR_MASK) {
		bm_pl->pl.type_ctrl |= META_DDR_IDX;
	} else {
		bm_pl->pl.type_ctrl |= META_SRAM_IDX;
	}
	/* pre-assign use nvme_cmd_idx as index */
	bm_pl->pl.nvm_cmd_id = meta_idx;
}

/*!
 * @brief init flush state machine of each name space
 *
 * @param nsid	name space id
 *
 * @return	not used
 */
void flush_fsm_init(u32 nsid);

/*!
 * @brief trim suspended done handler
 *
 * @param nsid	name space id
 *
 * @return	not used
 */
void flush_st_suspend_trim_done(u32 nsid);

/*!
 * @brief start a flush state machine
 *
 * @param ctx	flush context
 *
 * @return	not used
 */
void flush_fsm_run(struct _flush_ctx_t *ctx);

/*!
 * @brief start a runtime table flush state machine
 *
 * @param ctx	flush context
 *
 * @return	not used
 */
void tbl_flush_fsm_run(struct _flush_ctx_t *ctx);

/*!
 * @brief pmu swap pblk init
 *
 * @return		none
 */
void pmu_swap_pblk_init(void);

/*!
 * @brief pmu swap file order
 *
 * @return		none
 */
bool pmu_swap_file_order(u32 cnt);

/*!
 * @brief free pmu swap pblk
 *
 * @return		none
 */
void pmu_swap_end(void);

void FTL_GLOVARINIT(ftl_initial_mode_t mode);

void ftl_warm_boot_handle(void);
#if GC_SUSPEND_FWDL		//20210308-Eddie
extern void FWDL_GC_Handle(u8 type);
extern void __FWDL_GC_Handle(u8 type);
#endif

void save_log_for_plp_not_done(void *data);

void patrol_read_done(struct ncl_cmd_t *ncl_cmd);
void patrol_read_init(void);
void patrol_read(void *data);
#if CO_SUPPORT_DEVICE_SELF_TEST
void DST_patrol_read_start(u32 param0, u32 param1, u32 param2);
void DST_patrol_read(void *data);
void DST_pard_ncl_cmd_submit(pda_t* pda_list, enum ncl_cmd_op_t op, bm_pl_t *bm_pl, u32 count, u32 interleave_id);
bool DST_pard_get_spb(pard_mgr_t* pard_mgr);
void DST_patrol_read_done(struct ncl_cmd_t *ncl_cmd);
extern pda_t DST_L2P_search(lda_t LDA);
#endif
void ftl_flush(struct _flush_ctx_t *ctx);
extern bool blklist_flush_query[2];
bool ftl_blklist_copy(u32 nsid, u32 type);
#ifdef RD_FAIL_GET_LDA
void rd_err_get_lda_init(void);
#endif
void ftl_ns_check_appl_allocating(void);
void ftl_free_blist_dtag(u16 type);
void warmboot_save_pbt_done(u32 r0, u32 r1, u32 r2);
#if (FW_BUILD_VAC_ENABLE == mENABLE)
void fw_vac_rebuild(u32 *vc_buff);
#endif

/*! @} */
