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

/*! \file btn.h
 * @brief Rainier BTN design
 *
 * \addtogroup btn
 * \defgroup btn
 * \ingroup btn
 * @{
 */

#pragma once
#include "btn_precomp.h"
#include "btn_export.h"
#include "pool.h"

#define RD_STRM_BUF_TAG		0xBFF		///< it was a fake dtag id when error was happened on stream buffer(internal)

/*!
 * @brief definition of btn command queue
 */
typedef struct {
	hdr_reg_t reg;				///< header register
	u32 wptr __attribute__ ((aligned(8)));	///< for asic, updated pointer, can avoid register access
	union {
		u32 all;
		struct {
			u32 bypass : 1;
            u32 hold : 1;
			u32 rsvd : 30;
		} b;

	} flags;
} btn_cmdq_t;

/*!
 * @brief definition of btn free command queue
 */
typedef struct _btn_fcmdq_t {
	btn_cmdq_t cmdq;		///< btn command queue for free commands
	pool_t *cmd_pool;		///< software pool for free commands
} btn_fcmdq_t;

/*!
 * @brief btn header register initialization, this is used in l2p, too
 *
 * @param reg		header register data structure
 * @param base		entry base
 * @param size		entry count
 * @param pnter_base	updated pointer base
 * @param reg_addr	register offset
 */
extern void hdr_reg_init(hdr_reg_t *reg, void *base, u32 size,
			 volatile u32 *pnter_base, u32 reg_addr);

/*!
 * @brief push single source location to source queue
 *
 * @param reg		header register of source queue
 * @param addr		source location to be pushed
 *
 * @return		return -2 if source queue is full, return 0 for pushed
 */
extern int hdr_surc_push(hdr_reg_t *reg, u32 ptr, u32 addr);

/*!
 * @brief push source location list to source queue
 *
 * @param q		source queue
 * @param addr		source location address list to be pushed
 * @param cnt		list length
 *
 * @return		return -2 if source queue available slot is not enough, return 0 for pushed
 */
extern int hdr_surc_list_push(btn_cmdq_t *q, u32 *addr, u32 cnt);

/*!
 * @brief initialize BM header
 * Initialize a specific HDR list:
 * starting-index, ending-index and valid count are all cleared to 0
 *
 * @param reg		HDR list registers abstract
 * @param base		Base in dTCM where hardware to fill out
 * @param size		The payload capacity
 * @param reg_addr	Register address to operate
 *
 * @return		not used
 */
extern void btn_hdr_init(hdr_reg_t *reg, void *base, u32 size,
		volatile u32 *pnter_base, u32 reg_addr);

/*!
 * @brief recycle error write data entries from write error linked list
 *
 * @param cid		error nvme command cid
 * @param btag		error command btag
 *
 * @return u32		btn commands error count
 */
extern u32 bm_wr_err_recycle(u32 cid, int btag);

/*!
 * @brief recycle dtag from free write link list
 *
 * @return		recycled count
 */
extern u32 bm_free_wr_recycle(void);

/*!
 * @brief recycle dtag from read error link list
 *
 * @return		recycled count
 */
extern void bm_handle_rd_err(void);

/*!
 * @brief registers READ access
 *
 * @param reg	which register to access
 *
 * @return	Register value
 */
static inline u32 btn_readl(u32 reg)
{
	return readl((void *)(BM_BASE + reg));
}

/*!
 * @brief registers WRITE access
 *
 * @param val	the value to update
 * @param reg	which register to access
 *
 * @return	None
 */
static inline void btn_writel(u32 val, u32 reg)
{
	writel(val, (void *)(BM_BASE + reg));
}

/*!
 * @brief bm isr disable function
 *
 * @param mask	mask to indicate which isr to be disabled
 *
 * @return	not used
 */
static inline void btn_disable_isr(u32 mask)
{
	u32 val = btn_readl(BTN_INT_MASK);

	val |= (mask);
	btn_writel(val, BTN_INT_MASK);
}

/*!
 * @brief bm isr enable function
 *
 * @param mask	mask to indicate which isr to be enabled
 *
 * @return	not used
 */
static inline void btn_enable_isr(u32 mask)
{
	u32 val = btn_readl(BTN_INT_MASK);

	val &= ~(mask);
	btn_writel(val, BTN_INT_MASK);
}

/*!
 * @brief btn module core initialization
 *
 * @return		not used
 */
void btn_core_init(void);

/*!
 * @brief btn module core resume function
 *
 * @return		not used
 */
void btn_core_resume(void);
/*! @} */
