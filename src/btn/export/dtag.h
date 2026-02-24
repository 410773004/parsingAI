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

/*! \file dtag.h
 * @brief Rainier DTAG Manager Module
 *
 * \addtogroup btn
 * \defgroup dtag
 * \ingroup btn
 * @{
 */
#pragma once
#include "assert.h"
#include "atomic.h"
#include "types.h"
#include "list.h"
#include "dma.h"

#if defined(USE_8K_DU)
#define DTAG_SHF		(13)
#define LDA_SIZE_SHIFT		(13)
#else
#define DTAG_SHF		(12)		///< shift of dtag size
#define LDA_SIZE_SHIFT		(12)		///< bit shift that represent lda size
#endif
#define DTAG_SZE		(1 << DTAG_SHF)	///< dtag size
#define DTAG_MSK		(DTAG_SZE - 1)	///< dtag size mask

#define LDA_SIZE		(1 << LDA_SIZE_SHIFT) 			///< lda size
#define LDA_TO_LBA(lda)		((lda) << (LDA_SIZE_SHIFT - HLBASZ))	///< lda * nr_lba_per_lda
#define LBA_TO_LDA(lba)		((lba) >> (LDA_SIZE_SHIFT - HLBASZ))	///< lba / nr_lba_per_lda

#define DTAG_INV		((1 << 24) - 1)	///< invalid dtag

#define SRAM_IN_DTAG_CNT	(SRAM_SIZE >> DTAG_SHF)			/// SRAM size in DTAG count

/* Caution: you are at risk: likely(mem != dtag2mem(mem2dtag(mem))) */
#define sdtag2mem(dtag)		(dma_to_sram((dtag) << DTAG_SHF))		///< get sram memory pointer from dtag id
#define mem2sdtag(mem)		((sram_to_dma(mem)) >> DTAG_SHF)	///< get dtag id from sram memory pointer
#define ddtag2mem(dtag)		(((dtag) << DTAG_SHF) + DDR_BASE)		///< get ddr memory pointer from ddtag_id, be careful DDR cpu window
#define ddtag2off(dtag)		((dtag) << DTAG_SHF)			///< get DDR offset of ddr dtag
#define off2ddtag(off)		((off) >> DTAG_SHF)			///< get DDR dtag id
#define mem2ddtag(mem)		((((u32) (mem)) - DDR_BASE) >> DTAG_SHF)	///< get ddr dtag id from ddr memory pointer

#define DTAG_T_SRAM	0	//!< DTAG_T_SRAM
#define DTAG_T_DDR	1	//!< DTAG_T_DDR
#define DTAG_T_HMB	2	//!< DTAG_T_HMB
#define DTAG_T_MAX	3	//!< DTAG_T_MAX

#define DTAG_IN_DDR_MASK	BIT(23)				///< DDR bit in dtag_t
#define DDTAG_MASK		(DTAG_IN_DDR_MASK - 1)		///< DDR dtag mask
#define DTAG_IN_HMB_MASK	BIT(22)				///< HMB bit in dtag_t
#define HDTAG_MASK		(DTAG_IN_HMB_MASK - 1)		///< HMB dtag mask
#define SEMI_DDTAG_SHIFT	(9)
#define SEMI_SDTAG_MASK		(0x1FF)
#define SEMI_DDTAG_MASK		(0x7FFF)
#define DTAG_MISC_MASK		(0xFF000000)

#define MAX_DTAG_CALLER_NR	(6)		///< max caller of dtag get

/*!
 * @brief dtag entry definition
 */
typedef union _dtag_t {
	u32 dtag;
	struct {
		u32 dtag   	:22;	///< dtag id
		u32 type	: 1;	///< 0 = sram type, 1 = hmb type; when .in_ddr = 1 , dtag = (.type<<22) + .dtag */
		u32 in_ddr 	: 1;	///< access to DDR
		u32 type_ctrl   : 8;	///< refer to usage
	} b;
} dtag_t;

/*! dtag caller gc routine */
typedef void (*dtag_gc_handler)(u32 *need_cnt);

/*!
 * @brief dtag manager definition
 */
typedef struct {
	u8 caller_idx;		///< dtag caller GC index
	u8 caller_cnt;		///< how many callers were registered
	u8 need_gc_bmp;		///< indicate which cacller need gc
	u8 gc_avail_bmp;	///< indicate which cacller can release free dtag
	u32 total;		///< max dtag count in manager
	u32 urgent_cnt;		///< reserved for urgent usage
	u32 added;		///< how many dtags were added to manager
	dtag_gc_handler callers_gc[MAX_DTAG_CALLER_NR];	///< gc callback of registered caller
	u32 evt_cnt;			///< current pending event count
	struct list_head evt_list;	///< current pending event list
	atomic_t dtag_avail;		///< dtag available count
	atomic_t *dtag_refs;		///< dtag reference count, the dtag is busy if reference count is not zero
	unsigned long *dtag_bitmap;	///< bitmap for free dtags
} dtag_mgr_t;

extern dtag_t _inv_dtag;		///< invalid dtag, it's 0xffff
extern dtag_mgr_t _dtag_mgr[DTAG_T_MAX];	///< dtag manager
//for fill up
#define pending_dtag_cnt 64//it better be same with the max ipc cnt of get remote dtag

typedef struct{
	struct list_head entry1; //dtag pending list
	struct list_head entry2; //for free cnt

	u8 availa_cnt;
	u8 free_cnt;
	u8 total;
}dtag_pend_mgr_t;

typedef struct{
	struct list_head pending;
	u32 pl;
	u8 index;
}dtag_pend;

extern dtag_pend pend_dtag[pending_dtag_cnt];
extern dtag_pend_mgr_t* pend_dtag_mgr;
extern u8 dtag_evt_trigger_cnt;
/*!
 * @brief api to get dtag type
 *
 * @param dtag	data tag
 *
 * @return	return DTAG_T_xxx
 */
static inline int get_dtag_type(dtag_t dtag)
{
#ifdef DDR
	if (dtag.b.in_ddr)
		return DTAG_T_DDR;
#endif

#ifdef HMB_DTAG
	if (dtag.b.type)
		return DTAG_T_HMB;
#endif

	return DTAG_T_SRAM;
}

/*!
 * @brief api to get dtag from pointer
 *
 * @note be careful DDR pointer over 1G(2G with MPU)
 *
 * @param mem	memory pointer
 *
 * @return	dtag id
 */
static inline dtag_t mem2dtag(void *mem)
{
	dtag_t dtag;
#ifdef DDR
	if ((u32) mem > DDR_BASE) {
		dtag.dtag = mem2ddtag(mem);
		dtag.b.in_ddr = 1;
		return dtag;
	}
#endif
	dtag.dtag = mem2sdtag(mem);
	return dtag;
}

/*!
 * @brief api to get pointer from dtag
 *
 * @note be careful DDR dtag, pointer over 1G(2G with MPU) is wrong
 *
 * @param dtag	data tag
 *
 * @return	return pointer of dtag
 */
static inline void *dtag2mem(dtag_t dtag)
{
	int type = get_dtag_type(dtag);

#ifdef DDR
	if (type == DTAG_T_DDR) {
		u32 tag = (dtag.b.type << 22) | dtag.b.dtag;
		return (void *)ddtag2mem(tag);
	}
#endif

	if (type == DTAG_T_SRAM)
		return (void *) (sdtag2mem(dtag.b.dtag));

	sys_assert(0);
	return NULL;
}

/*!
 * @brief cast function u32 dtagid to dtag_t
 *
 * @param dtag_id	dtag_id
 *
 * @return		pointer of dtag_id
 */
static inline void *dtagid2mem(u32 dtag_id)
{
	dtag_t dtag = { .dtag = dtag_id};
	return dtag2mem(dtag);
}

/*!
 * @brief increase reference count of dtag
 *
 * Increase reference to avoid used dtag was released too early.
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param dtag	dtag which reference count will be increased
 *
 * @return	not used
 */
static inline void dtag_ref_inc(int type, dtag_t dtag)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	atomic_inc(&dtag_mgr->dtag_refs[dtag.b.dtag]);
}

/*!
 * @brief decrease reference count of dtag
 *
 * Decrease reference to avoid used dtag was released too early.
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param dtag	dtag which reference count will be decreased
 *
 * @return	not used
 */
static inline void dtag_ref_dec(int type, dtag_t dtag)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	dtag_mgr->dtag_refs[dtag.b.dtag].data--;
}

/*!
 * @brief increase reference count of a bulk of dtag
 *
 * Increase reference to avoid used dtag was released too early.
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param dtag	dtag list of which reference count will be increased
 * @param cnt	number of dtag
 *
 * @return	not used
 */
extern void dtag_ref_inc_bulk(int type, dtag_t *dtag, u32 cnt);

/*!
 * @brief according dtag type to increase reference count
 *
 * @param dtag	dtag to be increased
 *
 * @return	not used
 */
static inline void dtag_ref_inc_ex(dtag_t dtag)
{
	if (dtag.b.in_ddr)
		dtag_ref_inc(DTAG_T_DDR, dtag);
	else
		dtag_ref_inc(DTAG_T_SRAM, dtag);
}

/*!
 * @brief get dtag reference count
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param dtag	the dtag to check
 *
 * @return 	ref count of this dtag
 */
static inline u32 dtag_get_ref(int type, dtag_t dtag)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	return dtag_mgr->dtag_refs[dtag.b.dtag].data;
}

/*!
 * @brief require one dtag
 *
 * Allocate a dtag and fill out the memory address if necessary
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param mem	return memory pointer of required dtag
 *
 * @return	dtag allocated or _inv_dtag
 */
extern dtag_t dtag_get(int type, void **mem);

/*!
 * @brief require one urgent dtag
 *
 * Allocate a Dtag from Dtag urgent pool
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param mem	return memory pointer of required dtag
 *
 * @return	dtag allocated _inv_dtag if dtag pool was empty
 */
extern dtag_t dtag_get_urgt(int type, void **mem);

/*!
 * @brief recycle a dtag
 *
 * The reference count will be reduced in dtag_put, once reference count was
 * reduced to zero, it will be recycled.
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param dtag	dtag to be returned
 *
 * @return	not used
 */
extern void dtag_put(int type, dtag_t dtag);

/*!
 * @brief according dtag type to put dtag
 *
 * @param dtag	dtag to be put
 *
 * @return	not used
 */
static inline void dtag_put_ex(dtag_t dtag)
{
	if (dtag.b.in_ddr == 1)
		dtag_put(DTAG_T_DDR, dtag);
	else
		dtag_put(DTAG_T_SRAM, dtag);
}

/*!
 * @brief get amount of dtag
 *
 * Try to allocate required number dtags until urgent threshold was reached.
 *
 * @param type		dtag type, DTAG_T_XXX
 * @param required	required dtag count
 * @param dtags		dtag array to be filled
 *
 * @return		Return allocated dtag count, <= required
 */
extern u32 dtag_get_bulk(int type, u32 required, dtag_t *dtags);

/*!
 * @brief return amount of dtag
 *
 * All reference count of dtag in dtags will be reduced, once reference count
 * was reduced to zero, that dtag will be recycled.
 *
 * @param type		dtag type, DTAG_T_XXX
 * @param recycled	returned dtag count
 * @param dtags		dtag array
 *
 * @return		not used
 */
extern void dtag_put_bulk(int type, u32 recycled, dtag_t *dtags);

/*!
 * @brief refill the dtag pool
 *
 * Change memory range to dtags and add them into free dtag pool
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param mem	memory to be added
 * @param size	memory size
 *
 * @return	not used
 */
extern void dtag_add(int type, void *mem, u32 size);

/*!
 * @brief remove memory which was already allocated by linker
 *
 * Some memory may be allocated in ld file, after used done, they will be
 * returned to pool, before that, remove them from free dtag pool.
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param mem	memory allocated in linker
 * @param size	size of mem
 *
 * @return	not used
 */
extern void dtag_blackout(int type, void *mem, u32 size);

/*!
 * @brief require continuous dtags from pool
 *
 * Allocate continuous dtags from free pool.
 *
 * @param type		dtag type, DTAG_T_XXX
 * @param required	required count
 *
 * @return		First dtag of continuous dtags, or _inv_dtag
 */
extern dtag_t dtag_cont_get(int type, u32 required);

/*!
 * @brief return continuous dtags to pool
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param dtag	the first dtag of continuous dtags
 * @param cnt	count of continuous dtags
 *
 * @return	not used
 */
extern void dtag_cont_put(int type, dtag_t dtag, u32 cnt);

/*!
 * @brief dtag event handler type
 */
typedef void (*dtag_evt)(void *ctx); /*! dtag event handler type */

/*!
 * @brief register event handler if the current job is out of dtags
 *
 * Register a dtag event when out of dtags, once dtag was recycled, the event
 * will be resumed via event callback.
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param evt	event handler
 * @param ctx	event context
 * @param head	insert to list head or tail
 *
 * @return	not used
 */
extern void dtag_register_evt(int type, dtag_evt evt, void *ctx, bool head);

/*!
 * @brief remove dtag event from queue if found
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param ctx	target context to be removed
 *
 * @return	not used
 */
extern void dtag_remove_evt(int type, void *ctx);

/*!
 * @brief register dtag gc handler to free more dtag resource
 *
 * once one caller can not get dtag resource, it will trigger other callers' gc routine
 *
 * @param type	dtag type, DTAG_T_XXX
 * @param gc	dtag gc handler
 *
 * @return	caller id
 */
extern u8 dtag_register_caller_gc(int type, dtag_gc_handler gc);

/*!
 * @brief set the caller who has free dtags to be gc
 *
 * @param type			dtag type, DTAG_T_XXX
 * @param dtag_caller_id	the caller who has free dtag to be released
 *
 * @return			not used
 */
extern void dtag_set_gc_avail(int type, u8 dtag_caller_id);

/*!
 * @brief set the caller who has no free dtags to be gc
 *
 * @param type			dtag type, DTAG_T_XXX
 * @param dtag_caller_id	the caller who has no free dtag to be released
 *
 * @return			not used
 */
extern void dtag_clear_gc_avail(int type, u8 dtag_caller_id);

/*!
 * @brief check if the caller has dtags to be gc
 *
 * @param type			dtag type, DTAG_T_XXX
 * @param dtag_caller_id	caller id to be checked
 *
 * @return			true if the caller who has free dtags for gc
 */
extern bool dtag_check_gc_avail(int type, u8 dtag_caller_id);

/*!
 * @brief get the bitmap to indicate which caller has dtags to be gc
 *
 * @param type	dtag type, DTAG_T_XXX
 * @return	the bitmap to indicate which caller has dtags to be gc
 */
extern u8 dtag_get_gc_avail(int type);

/*!
 * @brief get how mant dtag events were pended
 *
 * @param type	dtag type, DTAG_T_XXX
 *
 * @return	not used
 */
extern u32 dtag_get_evt_cnt(int type);

static inline u32 dtag_get_avail_cnt(int type)
{
	return _dtag_mgr[type].dtag_avail.data;
}

/*!
 * @brief check non-urgent dtag available
 *
 * @param type	dtag type, DTAG_T_XXX
 *
 * @return	true if available
 */
extern bool dtag_get_nrm_avail(int type);

/*!
 * @brief dtag manager initialization
 *
 * @param type		DTAG_T_XXX
 * @param total		total dtag count
 * @param urgent_cnt	urgent dtag count
 *
 * @return		not used
 */
extern void dtag_mgr_init(int type, u32 total, u32 urgent_cnt);

/*!
 * @brief destroy dtag manger, not verified yet
 *
 * @param type		DTAG_T_XXX
 *
 * @return		not used
 */
extern void dtag_mgr_destroy(int type);

/*!
 * @brief recycle cpu's boot dtags(init_section or reserved dtags)
 *
 * @param cpu	target cpu
 *
 * @return	not used
 */
extern void boot_dtags_recycle(u32 cpu);

#if (CPU_ID == CPU_FE)
extern bool dtag_get_admin_avail(int type);
#endif

/*!
 * @brief dtag resume function from PMU, restore dtag event pool
 *
 * @return	not used
 */
extern void dtag_resume(void);

/*! @} */
