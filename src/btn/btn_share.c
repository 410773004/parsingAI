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

/*! \file btn_share.c
 * @brief Rainier BTN Module, shared function in btn
 *
 * \addtogroup btn
 * \defgroup btn share
 * \ingroup btn
 * @{
 */

#include "types.h"
#include "io.h"
#include "bf_mgr.h"
#include "btn.h"
#include "sect.h"

#define __FILEID__ btns
#include "trace.h"

#define ptr_dis(prod, cons, max0base)	((prod < cons) ? (cons - prod - 1) : (max0base - prod) + (cons))	///< get pointer distance in a queue

ps_code void hdr_reg_init(hdr_reg_t *reg, void *base, u32 size,
		volatile u32 *pnter_base, u32 reg_addr)
{
	reg->pnter_dbase.all = 0;
	writel(reg->pnter_dbase.all, (void *)(reg_addr + 8));
	readl((void *)(reg_addr + 8));

	reg->entry_dbase.all = 0;
	writel(reg->entry_dbase.all, (void *)reg_addr);	/* write 0 to max_sz to reset engine */

	reg->entry_dbase.b.dbase = btcm_to_dma(base);
	reg->entry_dbase.b.max_sz = size - 1;	/* 0'h based value */

	reg->pnter_dbase.b.dbase = btcm_to_dma((void *)pnter_base);
	reg->pnter_dbase.b.updt_en = 1;

	reg->entry_pnter.all = 0;
	reg->entry_pnter.b.rptr = 0;
	*pnter_base = 0;

	writel(reg->entry_dbase.all, (void *)(reg_addr));
	writel(reg->pnter_dbase.all, (void *)(reg_addr + 8));
	writel(reg->entry_pnter.all, (void *)(reg_addr + 4));

	reg->base = base;
	reg->mmio = reg_addr;
}

fast_code int hdr_surc_list_push(btn_cmdq_t *q, u32 *addr, u32 cnt)
{
	hdr_reg_t *reg = &q->reg;
	u32 rptr = q->wptr;
	u32 wptr = reg->entry_pnter.b.wptr; // FW maintains wptr

	u8 nr_slot = ptr_dis(wptr, rptr, reg->entry_dbase.b.max_sz);
	u32 push_cnt = min(nr_slot, cnt);

	if (push_cnt != cnt) {
		bf_mgr_trace(LOG_WARNING, 0x2f09, "btn que 0x%x busy, rptr %d wptr %d max %d, push %d/%d",
			reg->mmio + 4, rptr, wptr, reg->entry_dbase.b.max_sz, push_cnt, cnt);
		//sys_assert(false);
		return -2;
	}

	u32 *entry = (u32 *)reg->base;
	u32 i;

	for (i = 0; i < push_cnt; i++) {
		entry[wptr] = addr[i];
		wptr = (wptr + 1) & reg->entry_dbase.b.max_sz;
	}

	reg->entry_pnter.b.wptr = wptr;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));

	return 0;
}

fast_code int hdr_surc_push(hdr_reg_t *reg, u32 ptr, u32 addr)
{
	u32 rptr     =  ptr;
	u32 wptr_nxt = (reg->entry_pnter.b.wptr + 1) & reg->entry_dbase.b.max_sz;

	if (rptr == wptr_nxt) {/* Q busy */
		bf_mgr_trace(LOG_WARNING, 0xc27a, "Q busy, rptr %d, wptr_nxt %d, addr %x",
		     rptr, wptr_nxt, addr);
		//sys_assert(false);
		return -2;
	}

	u32 *entry = (u32 *)reg->base;

	//bf_mgr_trace(LOG_DEBUG, 0, "rptr %d, wptr_nxt %d, addr %x",
		//     rptr, wptr_nxt, addr);
	entry[reg->entry_pnter.b.wptr] = addr;
	reg->entry_pnter.b.wptr = wptr_nxt;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));

	return 0;
}

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
ps_code void btn_hdr_init(hdr_reg_t *reg, void *base, u32 size,
		volatile u32 *pnter_base, u32 reg_addr)
{
	hdr_reg_init(reg, base, size, pnter_base, reg_addr + BM_BASE);
}

/*! @} */
