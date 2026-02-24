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

#pragma once
#include "rainier_soc.h"
#include "ddr.h"
#include "dma.h"
#include "dtag.h"
#include "cbf.h"
#include "queue.h"
#include "ncl.h"
#include "mpc.h"
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

#define DDTAG_OFF		(SRAM_IN_DTAG_CNT)
#define TOTAL_DTAG		(DDR_DTAG_CNT)

#define PAR_UPDT_Q_SIZE		4
#define MAX_STRIPE_ID		10
//#define DREF_COMT_SIZE		MAX_DTAG_NUM
#define DREF_COMT_SIZE			768 // adams_SeqW

#define RDISK_RANGE_SIZE	(MAX_L2P_TRIM_NUM)
#define RDISK_RANGE_SHIFT	(16)
#define RDISK_RANGE_OFST_MASK	((1 << RDISK_RANGE_SHIFT) - 1)

#define SINGLE_SRCH_MARK	0xFFF				///< used for single search result mark

#define WUNC_BTAG_TAG (BIT(15))
#define FUA_BTAG_TAG  (BIT(14))
#define BTAG_MASK (FUA_BTAG_TAG - 1)
extern void rdisk_dref_inc(u32 *dtag_list, u32 cnt);
extern void rdisk_dref_dec(u32 *dtag_list, u32 cnt);

typedef CBF(u32, DREF_COMT_SIZE + 1) draf_comt_t;
typedef CBF(u32, 256) l2p_updt_que_t;
typedef CBF(bool, 256) l2p_updt_ntf_que_t;
typedef CBF(bool, 256) pbt_updt_ntf_que_t;
typedef CBF(u16, 8) close_blk_ntf_que_t;
typedef CBF(u64, 8) rc_host_queue_t;

typedef union _rdisk_fe_flags_t {
	u32 all;
	struct {
		u32 io_fetch_stop : 1;			///< if stop io fetch already
		u32 info_flushing : 1;			///< if fe info flushing
		u32 need_flush_info : 1;
		u32 pcie_reset : 1;			///< during pcie reset
		u32 warm_boot : 1;			///< warm boot
	} b;
} rdisk_fe_flags_t;

typedef union {
	u8 all;
	struct {
		u8 ofst : 4;
		u8 nlba : 4;
	};
} r_par_t;

/// @brief Cache Reset Flush service group operation code
enum
{
    CACHE_FLUSH_ISSUE = 0,
    CACHE_FLUSH_WAIT,
    CACHE_FLUSH_POLLING,
    CACHE_FLUSH_DONE,
    CACHE_FLUSH_RELEASE,
    CACHE_FLUSH_COUNT
};

typedef enum{
    CACHE_RET_CONTINUE = 0,
    CACHE_RET_POSTPONE,
    CACHE_RET_HALT,
    CACHE_RET_ESCAPE
} CacheFlushStatus_t;

extern u32 gCacheRstState;

#if defined(SEMI_WRITE_ENABLE)
	#define RDISK_PAR_DTAG_TYPE		DTAG_T_DDR
#elif defined(FORCE_IO_DDR)
	#define RDISK_PAR_DTAG_TYPE		DTAG_T_DDR
#elif defined(FORCE_IO_SRAM)
	#define RDISK_PAR_DTAG_TYPE		DTAG_T_SRAM
#else
	#error "semi, ddr or sram"
#endif

static inline u32 dtags2smdtag(u32 ddtag, u32 sdtag)
{
	return (((ddtag & SEMI_DDTAG_MASK) << SEMI_DDTAG_SHIFT) | (sdtag & SEMI_SDTAG_MASK));
}

static inline u32 smdtag2rdtag(u32 smdtag)
{
	return ((smdtag >> SEMI_DDTAG_SHIFT) & SEMI_DDTAG_MASK);
}

static inline u32 smdtag2ddtag(u32 smdtag)
{
	return (((smdtag >> SEMI_DDTAG_SHIFT) & SEMI_DDTAG_MASK) | DTAG_IN_DDR_MASK);
}

static inline u32 smdtag2sdtag(u32 smdtag)
{
	return (smdtag & SEMI_SDTAG_MASK);
}

static inline void smdtag_recycle(dtag_t smdtag)
{
	dtag_t dtag;

	dtag.dtag = smdtag2ddtag(smdtag.dtag);
	rdisk_dref_dec((u32*)&dtag, 1);

	dtag.dtag = smdtag2sdtag(smdtag.dtag);
	bm_free_semi_write_load(&dtag, 1, 0);
}

/*!
 * @brief check one bit in a bitmap
 *
 * @param bitmap	bitmap
 * @param index		number of bit to be check
 *
 * @return		return status
 */
static inline fast_code u32 bitmap_check(u32 *bitmap, u32 index)
{
	return (bitmap[index >> 5] & (1 << (index & 0x1f)));
}

/*!
 * @brief reset one bit into a bitmap, and return status of reset
 *
 * @param bitmap	bitmap
 * @param index		number of bit to be reset
 *
 * @return		return true if it was set before reset
 */
static inline fast_code bool bitmap_reset(u32 *bitmap, u32 index)
{
	u32 idx = index >> 5;
	u32 off = index & 0x1F;

	if (bitmap[idx] & (1 << off)) {
		bitmap[idx] &= ~(1 << off);
		return true;
	}

	return false;
}
/*!
 * @brief reset one bit from a bitmap
 *
 * @param bitmap	bitmap
 * @param index		number of bit to be reset
 *
 * @return		not used
 */
static inline fast_code void bitmap_just_reset(u32 *bitmap, u32 index)
{
	u32 idx = index >> 5;
	u32 off = index & 0x1F;

	bitmap[idx] &= ~(1 << off);
}

#ifdef LJ1_WUNC
static inline void _reset_pdu_bmp(dtag_t dtag)
{
	extern void *shr_ddtag_meta;
	extern void *shr_dtag_meta;
	struct du_meta_fmt *meta;
	meta = dtag.b.in_ddr ? shr_ddtag_meta : shr_dtag_meta;
	meta[dtag.b.dtag].wunc.WUNC = 0;
}

static inline void reset_pdu_bmp(dtag_t dtag)
{
#if 0//RDISK_WRITE_UNC_SUPPORT
	if (dtag.b.type_ctrl & BTN_SEMI_STREAMING_MODE) {
		dtag.dtag = smdtag2ddtag(dtag.dtag);
		_reset_pdu_bmp(dtag);

		dtag.dtag = smdtag2sdtag(dtag.dtag);
		_reset_pdu_bmp(dtag);
	} else {
		_reset_pdu_bmp(dtag);
	}
#endif
}
#endif
extern void ipc_enter_read_only_handle(volatile cpu_msg_req_t *req);
extern void plp_force_flush(void);
extern void rdisk_power_loss_flush(void);

extern void read_recoveried_commit(bm_pl_t *bm_pl, u16 pdu_bmp);
extern void rdisk_fua_done(u16 btag,dtag_t dtag);
extern u64 get_req_statistics(void);
extern void plp_detect(void);

extern bool __attribute__((weak)) ncl_cmd_empty(bool rw);
extern void __attribute__((weak)) ncl_cmd_block(void);
extern void __attribute__((weak)) ncl_handle_pending_cmd(void);

//extern void rdisk_reset_disk_handler(u32 p0, u32 p1, u32 p2);
extern void rdisk_reset_disk_handler(void);
extern void rdisk_reset_l2p_handler(void);
extern void rdisk_reset_resume_handler(void);
extern void rdisk_reset_cache(void);
extern void rxcmd_abort(void);
extern void rdisk_trigger_gc_resume(void);
extern void rdisk_ECCT_op(u64 slba, u64 nlb ,u8 op);

//extern void ipc_nvmet_update_smart_stat(volatile cpu_msg_req_t *req);

extern volatile bool hostReset;
extern void Host_AssertWaitShutDown(void);
extern void Host_ServiceLoopAssert(u32 r0, u32 r1, u32 r2);
extern void rdisk_assert_reset_disk_handler(void);
