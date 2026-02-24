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
/*! \file sync_dpe.c
 * @brief data processor module, use queue 2 as sync dpe engine
 *
 * \addtogroup btn
 * \defgroup dpe
 * \ingroup btn
 * @{
 * Implementation of data process engine
 */

#include "btn_precomp.h"
#include "btn_cmd_data_reg.h"
#include "dpe.h"
#include "btn.h"
#include "spin_lock.h"

/*! \cond PRIVATE */
#define __FILEID__ sdpe
#include "trace.h"
/*! \endcond */

#define DPE_DDR_SCRUB_SIZE	(8*1024*1024)

#define SYNC_DPE_ENTRY_CNT 	2
#define SYNC_DPE_ENTRY_CNT_MSK	(SYNC_DPE_ENTRY_CNT - 1)

sram_sh_data dpe_entry_t _sync_dpe_reqs[SYNC_DPE_ENTRY_CNT] __attribute__ ((aligned(32)));
sram_sh_data dpe_entry_t _sync_dpe_rsps[SYNC_DPE_ENTRY_CNT] __attribute__ ((aligned(32)));

static inline u32 wait_sync_dpe_req_fetch(void)
{
	data_proc_req1_pointer_t req;

	do {
		req.all = btn_readl(DATA_PROC_REQ1_POINTER);
	} while (req.b.data_proc_req1_rptr != req.b.data_proc_req1_wptr);

	return req.b.data_proc_req1_rptr;
}

static inline void wait_sync_dpe_rsp_sent(u32 rsp_idx)
{
	data_proc_res1_pointer_t rsp;

	do {
		rsp.all = btn_readl(DATA_PROC_RES1_POINTER);
	} while (rsp.b.data_proc_res1_wptr != rsp_idx);

	rsp.b.data_proc_res1_rptr = rsp_idx;
	btn_writel(rsp.all, DATA_PROC_RES1_POINTER);
}

static inline void lock_sync_dpe(void)
{
#if defined(MPC)
	spin_lock_take(SPIN_LOCK_KEY_SYNC_DPE, 0, true);
#endif
}

static inline void unlock_sync_dpe(void)
{
#if defined(MPC)
	spin_lock_release(SPIN_LOCK_KEY_SYNC_DPE);
#endif
}

#if CPU_ID == 1
fast_code static void sync_dpe_init(void)
{
	data_proc_req1_sram_base_t dpe_queue_base = {
		.all = 0
	};

	dpe_queue_base.b.data_proc_req1_sram_base = sram_to_dma(_sync_dpe_reqs);
	dpe_queue_base.b.data_proc_req1_max_sz = SYNC_DPE_ENTRY_CNT - 1;

	btn_writel(dpe_queue_base.all, DATA_PROC_REQ1_SRAM_BASE);
	btn_writel(sram_to_dma(_sync_dpe_rsps), DATA_PROC_RES1_SRAM_BASE);

	data_proc_res1_pointer_t res = { .all = btn_readl(DATA_PROC_RES1_POINTER) };
	data_proc_req1_pointer_t req = { .all = btn_readl(DATA_PROC_REQ1_POINTER) };

	sys_assert(res.b.data_proc_res1_rptr == res.b.data_proc_res1_wptr);
	sys_assert(req.b.data_proc_req1_rptr == req.b.data_proc_req1_wptr);
}

fast_code void sync_dpe_reset(void)
{
	lock_sync_dpe();
	sync_dpe_init();
	unlock_sync_dpe();
}
#endif

static inline void sync_dpe_submit(u8 wptr_nxt)
{
	data_proc_req1_pointer_t req;

	req.b.data_proc_req1_wptr = wptr_nxt;
	btn_writel(req.all, DATA_PROC_REQ1_POINTER);

	wait_sync_dpe_rsp_sent(wait_sync_dpe_req_fetch());
}

fast_code void sync_dpe_copy(u64 src, u64 dst, u32 size)
{
	data_proc_req1_pointer_t req;
	dpe_entry_t *entry;
    bool src_ddr = 0;
    bool dst_ddr = 0;
	lock_sync_dpe();

	req.all = btn_readl(DATA_PROC_REQ1_POINTER);
	entry = &_sync_dpe_reqs[req.b.data_proc_req1_wptr];

	sys_assert(((((u32)src) & 0x1F) == 0) && ((((u32)dst) & 0x1F) == 0));

	u8 wptr_nxt = (req.b.data_proc_req1_wptr + 1) & SYNC_DPE_ENTRY_CNT_MSK;
	dpe_cp_hdr_t hdr;
    if(dst >= DDR_BASE){
        dst -= DDR_BASE;
        dst_ddr = true;
    }
    if(src >= DDR_BASE){
        src -= DDR_BASE;
        src_ddr = true;
    }
    sys_assert((((src>>32)&0xF)==(((src+size-1)>>32)&0xF)) &&(((dst>>32)&0xF)==(((dst+size-1)>>32)&0xF)));
	hdr.all = 0;
	hdr.b.bm_tag = req.b.data_proc_req1_wptr;
	hdr.b.cmd_code = DPE_COPY;
	hdr.b.dst_ddr = dst_ddr ? 1 : 0;
	hdr.b.src_ddr = src_ddr ? 1 : 0;
	hdr.b.ddr_win_1st = src_ddr? ((src>>32)&0xF) : 0;
	hdr.b.ddr_win_2nd = dst_ddr? ((dst>>32)&0xF) : 0;
    if((dst_ddr==false)&&(src_ddr==false)){
	    if (is_btcm((void*)(u32)src) && is_btcm((void*)(u32)dst))
		panic("no support");
    }
    if ((src_ddr == false) && is_btcm((void*)(u32)src))
		hdr.b.dtcm_sel = DPE_CPY_SRC_IS_DTCM;
	else if ((dst_ddr == false) && is_btcm((void*)(u32)dst))
		hdr.b.dtcm_sel = DPE_CPY_DST_IS_DTCM;
	else
		hdr.b.dtcm_sel = 0;

	entry->hdr.all = hdr.all;

	entry->payload[0] = src_ddr ? ((u32)src) : mem_to_dma((void*)(u32)src);
	entry->payload[1] = dst_ddr ? ((u32)dst) : mem_to_dma((void*)(u32)dst);

	entry->payload[2] = size;

	sync_dpe_submit(wptr_nxt);

	unlock_sync_dpe();
}

/*!
 * @brief api to fill ddr
 *
 * @param start		start offset
 * @param len		length to be filled
 * @param pat		fill pattern
 *
 * @return		not used
 */
fast_code void bm_data_fill_ddr(u64 start, u64 len, u32 pat)
{
	dpe_entry_t *entry;
	data_proc_req1_pointer_t req;

	req.all = btn_readl(DATA_PROC_REQ1_POINTER);
	entry = &_sync_dpe_reqs[req.b.data_proc_req1_wptr];

	u8 wptr_nxt = (req.b.data_proc_req1_wptr + 1) & SYNC_DPE_ENTRY_CNT_MSK;

	// start and length need 32 bytes aligned
	sys_assert(len <= DPE_DDR_SCRUB_SIZE);
	sys_assert((start & 0x1F) == 0);
	sys_assert((len & 0x1F) == 0);

	u8 swin = (start >> 32) & 0xF;
	u8 ewin = ((start + len - 1) >> 32) & 0xF;
	sys_assert(swin == ewin);

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = req.b.data_proc_req1_wptr;
	entry->hdr.req.cmd_code = DPE_FILL;
	entry->hdr.req.cmd_opt = 1;
	entry->hdr.req.param = swin;

	entry->payload[0] = (start & 0xFFFFFFFF);
	entry->payload[1] = len;
	entry->payload[2] = pat;

	sync_dpe_submit(wptr_nxt);
}

fast_code void bm_scrub_ddr(u64 start, u64 len, u32 pat)
{
	lock_sync_dpe();
	//bf_mgr_trace(LOG_INFO, 0, "Scrubing DDR start 0x%x%x len 0x%x%x ... ",
	//		(u32)(start >> 32), start, (u32)(len >> 32), len);

	while (len) {
		u64 l;

		l = min(DPE_DDR_SCRUB_SIZE, len);
		if (start & (DPE_DDR_SCRUB_SIZE - 1)) {
			 u64 eff = DPE_DDR_SCRUB_SIZE - (start & (DPE_DDR_SCRUB_SIZE - 1));

			 l = min(eff, l);
		}

		bm_data_fill_ddr(start, l, pat);
		start += l;
		len -= l;
	}

	sys_assert(len == 0);
	//bf_mgr_trace(LOG_INFO, 0, "Done.");
	unlock_sync_dpe();
}

/*! @} */
