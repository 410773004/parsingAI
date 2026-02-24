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
/*! \file dpe.c
 * @brief data processor module
 *
 * \addtogroup btn
 * \defgroup dpe
 * \ingroup btn
 * @{
 * Implementation of data process engine
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "btn_precomp.h"
#include "btn_cmd_data_reg.h"
#include "bf_mgr.h"
#include "assert.h"
#include "dpe.h"
#include "misc.h"
#include "cpu_msg.h"

/*! \cond PRIVATE */
#define __FILEID__ dpe
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define DPE_ENTRY_CNT_SHF	8
#define DPE_ENTRY_CNT_CNT	(1 << DPE_ENTRY_CNT_SHF)	///< DPE request buffer count
#define DPE_ENTRY_CNT_MSK	(DPE_ENTRY_CNT_CNT - 1)

/*lint -restore*/

#define DPE_SHA3_CAL_MAX_SIZE	(4096)	///< Split big data to 480 bytes for single cmd by HW design
#define DPE_SM3_CAL_MAX_SIZE	(480)	///< Split big data to 480 bytes for single cmd by HW design

/*!
 * @brief callback context of data process command
 */
typedef struct _dpe_cb_ctx_t {
	dpe_cb_t cb;		/*! callback function when command done */
	void *ctx;		/*! caller context */
} dpe_cb_ctx_t;

/*!
 * @brief DPE SS key wrap/unwrap process entry structure
 */
typedef struct _dpe_ss_key_wp_uwp_entry_t {
	u32 bm_tag:12;		/*! BM command tag */
	u32 spr_idx_din:2;	/*! SPR index for input data */
	u32 spr_idx_kek:2;	/*! SPR index for KEK */
	u32 kek_sz_sel:2;	/*! KEK size selection */
	u32 din_sz_sel:1;	/*! Input data size selection */
	u32 kek_use_fp_en:1;	/*! KEK to use finger print enable */
	u32 sm4_mode:1;		/*! SM4 or AES mode selection */
	u32 rsvd:3;		/*! Reserved */
	u32 cmd_code:4;		/*! DPE command code */
	u32 key_wp_enc_ctrl:4;	/*! Key Wrap/encrypt control */

	u32 src_addr:21;	/*! Address of source data */
	u32 rsvd1:11;		/*! Reserved */

	u32 rslt_addr:21;	/*! Address of result data */
	u32 rsvd2:11;		/*! Reserved */

	u32 rsvd3:16;			/*! Reserved */
	u32 uwp_rslt_mek_wr_addr:6;	/*! Unwrap result MEK write address */
	u32 rsvd4:1;			/*! Reserved */
	u32 uwp_rslt_mek_wr_ena:1;	/*! Unwrap result MEK write enable */
	u32 uwp_rslt_sprx_wr_ena:1;	/*! Unwrap result SPRx write enable */
	u32 uwp_rslt_sprx_sel:2;	/*! Unwrap result SPRx selection */
	u32 rsvd5:5;		/*! Reserved */
} dpe_ss_key_wp_uwp_entry_t;

/*!
 * @brief DPE SHA3 entry structure
 */
typedef struct _dpe_sha3_entry_t {
	u32 bm_tag:12; 			/*! BM command tag */
	u32 data_from_spr_en:1; 	/*! the source data is from SPR register */
	u32 spr_index_of_data:2;	/*! The SPR register index */
	u32 rsvd:1;
	u32 result_xor_r3_en:1;		/*! Enable the result to XOR with SPR R3 register */
	u32 spr_index_of_rslt:2;	/*! The SPR selection for the result data when secure-mode enable*/
	u32 rsvd1:5;
	u32 cmd_code:4;		/*! DP engine command code */
	u32 sha3:1;		/*! 0: SHA3 mode, 1: SM3 mode */
	u32 new_calc:1;		/*! indicate first command or not */
	u32 last_calc:1;	/*! indicate last command or not */
	u32 rsvd2:1;		/*! reserved */

	u32 msg_addr;		/*! input message located memory address */
	u32 rst_addr;		/*! compute result located memory address */
	u32 count; 		/*! input message length */
} dpe_sha3_entry_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

/* TODO: change to BTCM */
static slow_data_ni dpe_entry_t _dpe_reqs[DPE_ENTRY_CNT_CNT] __attribute__ ((aligned(32)));	///< data process engine request queue
static slow_data_ni dpe_entry_t _dpe_rsps[DPE_ENTRY_CNT_CNT] __attribute__ ((aligned(32)));	///< data process engine response queue
static fast_data_ni dpe_cb_ctx_t _dpe_ctxs[DPE_ENTRY_CNT_CNT + DPE_ENTRY_CNT_CNT];					///< data process engine callback context queue
static slow_data_ni u32 search_rsp_base[MAX_SEARCH_IO_CNT] __attribute__ ((aligned(32)));	///< data process engine search response base pointer queue


#define  MAX_DPE_CTX_CNT  (DPE_ENTRY_CNT_CNT)  // never excceed rsp queue cnt
static fast_data_zi u8 _dpe_ctxs_mgr[MAX_DPE_CTX_CNT >> 3];///< data process engine callback context queue valid bitmap;
fast_data_zi u16 dpe_ctxs_allocated_cnt;
static fast_data_ni u8 dpe_req_wptr;	///< data process engine request write pointer
static fast_data_ni u8 dpe_rsp_rptr;	///< data process engine response read pointer
static fast_data_zi bool in_dpe_req;

extern u16 host_sec_size;//joe add change sec size 20200817
extern u8 host_sec_bitz;//joe add change sec size  20200817
static u8 bm_wait_dpe_entry(void);

typedef struct _dpe_ctx_t {
    u64 src;
    u64 dst;
    u32 nbytes;
    dpe_cb_t callback;
    void *ctx;
} dpe_ctx_t;

#define DPE_PENDING_QUEUE_ZISE 100
ddr_data dpe_ctx_t dpe_pending_que[DPE_PENDING_QUEUE_ZISE];
ddr_data u32 bm_copy_pending_cnt;
fast_data_zi bool dpe_busy;

//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
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

fast_code bool bm_data_merge(dtag_t src, dtag_t dst, u8 lba_ofst, u8 nsec,
		dpe_cb_t callback, void *ctx)
{
    data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};

	u8 next_wptr = (dpe_req_wptr + 1) & DPE_ENTRY_CNT_MSK;

	if (next_wptr == queue.b.data_proc_req_rptr){
        bf_mgr_trace(LOG_ERR, 0x7c69, "%d, %d\n", next_wptr, dpe_rsp_rptr);
        return false;
	}

	sys_assert(dpe_ctxs_allocated_cnt <= DPE_ENTRY_CNT_CNT-1);

	if (dpe_ctxs_allocated_cnt == DPE_ENTRY_CNT_CNT-1) {
		// not allowed to fill dpe request anymore, keep one rsp slot free
		return false;
	}

    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];
	//u32 du_ctrl = (HLBASZ == 12) ? BIT(2) : BIT(0); //joe add sec size 20200817
	u32 du_ctrl = (host_sec_bitz== 12) ? BIT(2) : BIT(0);

#if defined(HMETA_SIZE)
	du_ctrl |= BIT(HMETA_SIZE / 8 - 1);
#endif

    clear_bit(cb_ctx_idx,&_dpe_ctxs_mgr);
	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_MERGE;
	entry->hdr.req.param = nsec << 4 | lba_ofst;

	entry->hdr.req.cmd_opt = du_ctrl;

	entry->payload[0] = src.dtag;
	entry->payload[1] = dst.dtag;

	dpe_req_wptr = next_wptr;
	queue.b.data_proc_req_wptr = next_wptr;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
    return true;
}

ddr_code void bm_data_copy_pend_in(u64 src, u64 dst, u32 nbytes,
		dpe_cb_t callback, void *ctx)
{
    bf_mgr_trace(LOG_ERR, 0x3fc6, "bm_data_copy(0x%x, 0x%x, 0x%x, 0x%x, 0x%x);",
        src, dst, nbytes, callback, ctx);
    sys_assert(bm_copy_pending_cnt < DPE_PENDING_QUEUE_ZISE);
    dpe_pending_que[bm_copy_pending_cnt].src = src;
    dpe_pending_que[bm_copy_pending_cnt].dst = dst;
    dpe_pending_que[bm_copy_pending_cnt].nbytes = nbytes;
    dpe_pending_que[bm_copy_pending_cnt].callback = callback;
    dpe_pending_que[bm_copy_pending_cnt].ctx = ctx;
    bm_copy_pending_cnt ++;

}

ddr_code void bm_data_copy_pend_out()
{
    for(u32 i = 0; i < bm_copy_pending_cnt; ++i){
		bf_mgr_trace(LOG_ERR, 0x7db0, "bm_data_copy(0x%x, 0x%x, 0x%x, 0x%x, 0x%x);",
            dpe_pending_que[i].src, dpe_pending_que[i].dst, dpe_pending_que[i].nbytes,
            dpe_pending_que[i].callback, dpe_pending_que[i].ctx);

        bm_data_copy(dpe_pending_que[i].src, dpe_pending_que[i].dst,
            dpe_pending_que[i].nbytes, dpe_pending_que[i].callback, dpe_pending_que[i].ctx);
    }
    bm_copy_pending_cnt = 0;
}


fast_code void bm_data_copy(u64 src, u64 dst, u32 nbytes,
		dpe_cb_t callback, void *ctx)
{
    if(dpe_busy){
        bm_data_copy_pend_in(src, dst, nbytes, callback, ctx);
        return;
    }
    
    bool src_ddr = 0;
    bool dst_ddr = 0;
	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];

	sys_assert(((((u32)src) & 0x1F) == 0) && ((((u32)dst) & 0x1F) == 0));


	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};



    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);

	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	dpe_cp_hdr_t hdr;
    if(dst >= DDR_BASE){
        dst -= DDR_BASE;
        dst_ddr = true;
    }
    if(src >= DDR_BASE){
        src -= DDR_BASE;
        src_ddr = true;
    }
    sys_assert((((src>>32)&0xF)==(((src+nbytes-1)>>32)&0xF)) && (((dst>>32)&0xF)==(((dst+nbytes-1)>>32)&0xF)));

	hdr.all = 0;
	hdr.b.bm_tag = cb_ctx_idx;
	hdr.b.cmd_code = DPE_COPY;
	hdr.b.dst_ddr = dst_ddr ? 1 : 0;
	hdr.b.src_ddr = src_ddr ? 1 : 0;
	hdr.b.ddr_win_1st = src_ddr? ((src>>32)&0xF) : 0;
	hdr.b.ddr_win_2nd = dst_ddr? ((dst>>32)&0xF) : 0;

    // bf_mgr_trace(LOG_ERR, 0, "ddr_win_1st %u ddr_win_2nd %u src %x_%x, all %x",
    //     hdr.b.ddr_win_1st, hdr.b.ddr_win_2nd, src>>32, src, hdr.all);
    
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
	entry->payload[2] = nbytes;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}


fast_code void bm_data_compare_dtag(dtag_t src, dtag_t dst,
		dpe_cb_t callback, void *ctx)
{

	u8 wptr_nxt = bm_wait_dpe_entry();

//	while (dpe_ctxs_allocated_cnt == DPE_ENTRY_CNT_CNT-1) {
//        dpe_isr();
//	}

    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;
	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];
	//u32 du_ctrl = (HLBASZ == 12) ? BIT(2) : BIT(0);
	u32 du_ctrl = (host_sec_bitz== 12) ? BIT(2) : BIT(0);//joe add 20200819

#if defined(HMETA_SIZE)
	du_ctrl |= BIT(HMETA_SIZE / 8 - 1);
#endif

	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};

    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);

	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_CMP_FM1;
	entry->hdr.req.cmd_opt = du_ctrl;
	entry->payload[0] = src.dtag;
	entry->payload[1] = dst.dtag;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

fast_code void bm_data_compare_mem(void *mem1, void *mem2, u32 len,
		dpe_cb_t callback, void *ctx)
{

	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];


	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};


    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);

	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	sys_assert((len & 0x1F) == 0);

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_CMP_FM2;

	u8 mem1_in_ddr = ((u32)mem1 >= DDR_BASE) ? 1 : 0;
	u8 mem2_in_ddr = ((u32)mem2 >= DDR_BASE) ? 1 : 0;
	entry->hdr.req.cmd_opt = (mem1_in_ddr << 0) | (mem2_in_ddr << 1);

	entry->payload[0] = (mem1_in_ddr) ? ddr_to_dma(mem1) : sram_to_dma(mem1);
	entry->payload[1] = (mem2_in_ddr) ? ddr_to_dma(mem2) : sram_to_dma(mem2);

	entry->payload[2] = len & 0x1FFFFF;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

fast_code u32 bm_dpe_setup_search_result_buf(void *res)
{
	static u32 search_rsp_id = 0;
	u32 idx = search_rsp_id;

	if (search_rsp_base[idx] != 0)
		return ~0;

	search_rsp_base[idx] = sram_to_dma(res);

	search_rsp_id++;

	if (search_rsp_id >= MAX_SEARCH_IO_CNT)
		search_rsp_id = 0;

	return idx;
}

fast_code void bm_data_search(void *mem, u32 nbytes, u32 mask,
		u32 pat, dpe_cb_t callback, u32 result_id, void *ctx)
{

	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr,MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];


	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};


    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);
	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.param = nbytes >> 5;
	entry->hdr.req.cmd_code = DPE_SEARCH;
	entry->hdr.req.cmd_opt = result_id;

	entry->payload[0] = sram_to_dma(mem);
	entry->payload[1] = mask;
	entry->payload[2] = pat;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

fast_code void bm_data_fill(void *mem, u32 nbytes, u32 pat,
		dpe_cb_t callback, void *ctx)
{
	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];


	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};


    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);

	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_FILL;
	entry->hdr.req.cmd_opt = (u32)mem >= DDR_BASE ? 1 : 0;   // DDR : SRAM
	//entry->hdr.req.cmd_opt = 1;   //DDR

	entry->payload[0] = (u32)mem >= DDR_BASE ? (u32)mem - DDR_BASE : sram_to_dma(mem);
	//entry->payload[0] = (u32)mem; //DDR

	entry->payload[1] = nbytes;
	entry->payload[2] = pat;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

/*!
 * @brief wait DPE free entry
 *
 * @return	next write pointer of DPE register
 */
static fast_code u8 bm_wait_dpe_entry(void)
{
	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};
    sys_assert(in_dpe_req == false);
	u8 wptr_nxt = (dpe_req_wptr + 1) & DPE_ENTRY_CNT_MSK;
    in_dpe_req = true;
	while ((wptr_nxt == queue.b.data_proc_req_rptr) || (dpe_ctxs_allocated_cnt == DPE_ENTRY_CNT_CNT-1)) {
        dpe_isr();
		queue.all = btn_readl(DATA_PROC_REQ_POINTER);
        wptr_nxt = (dpe_req_wptr + 1) & DPE_ENTRY_CNT_MSK;
	}
    in_dpe_req = false;
	return wptr_nxt;
}

fast_code void bm_set_secure_mode(u32 mode)
{
	u32 ss_ctl_sts;

	ss_ctl_sts = btn_readl(SEC_SUBSYSTEM_CTRL);
	mode &= 3;
	ss_ctl_sts |= mode; /* Set/Clear secure and test mode */
	btn_writel(ss_ctl_sts, SEC_SUBSYSTEM_CTRL);
}

slow_code void bm_clear_secure_mode(void)
{
	sec_subsystem_ctrl_t ss_ctl = {
		.all = btn_readl(SEC_SUBSYSTEM_CTRL),
	};
	ss_ctl.b.sec_sub_secure_mode = 0;
	ss_ctl.b.sec_sub_test_mode = 0;
	btn_writel(ss_ctl.all, SEC_SUBSYSTEM_CTRL);
}


/*!
 * @brief SHA3/SM3 calculation function
 *
 * Build real Data Process engine command for SHA3/SM3 calculation
 *
 * @param mem		SRAM buffer to store data message
 * @param result	SRAM buffer for store calculated result
 * @param count		How many bytes of data message to be calculate
 * @param callback	DPE callback function pointer
 * @param ctx		DPE context
 * @param first		true if this is the first command
 * @param sm3		true for SM3 mode, false for SHA3 mode
 * @param split		single command max data size
 *
 * @return		calculation byte size, max is 480
 */
ddr_code u32 _bm_sha3_sm3_calc(void *mem, void *result,
		u32 count, dpe_cb_t callback, void *ctx, bool first,
		bool sm3, u32 split)
{
	u8 wptr_nxt = bm_wait_dpe_entry();
	u32 calc_size;
	void *buf;
	/*lint -e(740) it should be compatible */
    u16 cb_ctx_idx = 0;
	
	dpe_sha3_entry_t *entry = (dpe_sha3_entry_t *)&_dpe_reqs[dpe_req_wptr];

	calc_size = min(count, split);
	count -= calc_size;

    if(count == 0)
    {
        cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
        sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
        sys_assert(!dpe_busy);
		dpe_ctxs_allocated_cnt++;
    }

    dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];

	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};

	buf = mem;

	memset(entry, 0x00, sizeof(*entry));
	entry->bm_tag = cb_ctx_idx;
	entry->cmd_code = DPE_SHA3_SM3_CAL;
	entry->new_calc = (u32) first;
	entry->sha3 = (u32) sm3;
	entry->msg_addr = sram_to_dma(buf);
	entry->rst_addr = sram_to_dma(result);
	entry->count = calc_size;
	if (count == 0)
    {
        clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);
		entry->last_calc = 1;
		cb_ctx->cb = callback;
		cb_ctx->ctx = ctx;
	} else {
		entry->last_calc = 0;
	}

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;

	btn_writel(queue.all, DATA_PROC_REQ_POINTER);

	bm_wait_dpe_data_process_done();

	return calc_size;
}

ddr_code void bm_sha3_sm3_calc(void *mem, void *result, bool sm3,
		u32 count, dpe_cb_t callback, void *ctx)
{
	bool first = true;
	u8 *buf = (u8 *)mem;
	u32 max_size;

	max_size = sm3 ? DPE_SM3_CAL_MAX_SIZE : DPE_SHA3_CAL_MAX_SIZE;
	do {
		u32 calc_size;

		calc_size = _bm_sha3_sm3_calc((void *)buf, result, count,
				callback, ctx, first, sm3, max_size);
		first = false;
		count -= calc_size;
		buf += calc_size;
	} while (count != 0);
}

ddr_code void bm_sha3_sm3_calc_part(void *mem, void *result, bool sm3,
		u32 count, u32 cur_count, dpe_cb_t callback, void *ctx, bool first)
{
	u8 *buf = (u8 *)mem;
	u32 max_size;

	max_size = sm3 ? DPE_SM3_CAL_MAX_SIZE : DPE_SHA3_CAL_MAX_SIZE;
	do {
		u32 calc_size;

		max_size = min(max_size, cur_count);
		calc_size = _bm_sha3_sm3_calc((void *)buf, result, count,
				callback, ctx, first, sm3, max_size);
		first = false;
		count -= calc_size;
		cur_count -= calc_size;
		buf += calc_size;
	} while (cur_count != 0);
}

fast_code void bm_wait_dpe_data_process_done(void)
{
	data_proc_req_pointer_t req_q = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (req_q.b.data_proc_req_rptr != req_q.b.data_proc_req_wptr) {
		req_q.all = btn_readl(DATA_PROC_REQ_POINTER);

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}
}

fast_code void bm_rsa_calc(void *src, void *result, unsigned int nprime0, dpe_cb_t callback, void *ctx, enum rsa_mode_ctrl mode)
{
	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_entry_t *entry = (dpe_entry_t *)&_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];

    	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};
	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_SEC_RSA_CAL;
	/* starting SRAM address for source data. BIT[4:0] must be 0s
	the source data is 2560-byte always */
	entry->payload[0] = sram_to_dma(src);
	/* starting SRAM address for result. BIT[4:0] must be 0s
	the source data is 512-byte always */
	entry->payload[1] = sram_to_dma(result);
	/* the n-prime-0 value for the RSA calculation */
	entry->payload[2] = nprime0;
    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);
	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;

	/* The RSA engine supports RSA1024/2048/3072/4096, so select rsa mode before submit dpe request */
	sec_subsystem_ctrl_t ss_ctrl;
	ss_ctrl.all = btn_readl(SEC_SUBSYSTEM_CTRL);
	ss_ctrl.b.sec_rsa_calc_ctrl = mode;
	btn_writel(ss_ctrl.all, SEC_SUBSYSTEM_CTRL);
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

/*!
 * @brief Security Subsystem SPR register programming function
 *
 * Program SPR register to load data (ex: key) for diffrent usage.
 * Depending on the usage, this works in both secure and non-secure mode.
 *
 * @param mem		SRAM buffer to data.
 * @param spr_idx	Index of SPR register (0 to 3)
 * @param callback	DPE callback function pointer
 * @param ctx		DPE context
 *
 * @return		None
 */
ddr_code void bm_ss_spr_prgm(void* mem, enum spr_reg_idx spr_idx, dpe_cb_t callback, void *ctx)
{

	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr,MAX_DPE_CTX_CNT);
    sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
	dpe_ctxs_allocated_cnt++;
	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];


	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};


    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);
	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_SEC_SPR_PROG;
	entry->hdr.req.cmd_opt = spr_idx;
	entry->payload[0] = sram_to_dma(mem);

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}


fast_code void bm_sm2_cal(void *mem, void *result, enum sm2_func_sel func_sel,
			dpe_cb_t callback, void *ctx)
{

	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
	sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;
	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];


	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};


    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);
	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_SM2_CAL;
	entry->hdr.req.cmd_opt = (u32) func_sel;
	entry->payload[0] = sram_to_dma(mem);
	entry->payload[1] = sram_to_dma(result);

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

/*!
 * @brief Function to start Wrap/Unwrap HW.
 *
 * Build Data Process engine command for Key Wrap/Unwrap operation. This can be
 * controlled in secure/non-secure, AES/SM4, chip finger print enable/disable
 * mode, RAW encrypt/decrypt mode etc.
 *
 * @param mem		SRAM buffer to source data (plain/encrypted text)
 * @param result	SRAM buffer to result data (plain/encrypted text)
 * @param callback	DPE callback function pointer
 * @param ctx		DPE context
 * @param kwp_cfg	Pointer to key wrap/unwrap configuration structure
 *
 * @return		None
 *
 */
ddr_code void bm_ss_key_wp_uwp(void* mem, void *result,
		   dpe_cb_t callback, void *ctx, kwp_cfg_t *kwp_cfg)
{
	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
	sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_ss_key_wp_uwp_entry_t *entry = (dpe_ss_key_wp_uwp_entry_t *)&_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];

	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};

	memset((void *)entry, 0, sizeof(dpe_ss_key_wp_uwp_entry_t));
	/* DW0 */
	entry->bm_tag = cb_ctx_idx;
	/* Valid for "secure mode" and "key wrap" operation only */
	if ((kwp_cfg->sec_mode_en) && (kwp_cfg->wrap_op == SS_KEY_WRAP)) {
		entry->spr_idx_din = kwp_cfg->din_spr_idx; /* data in SPR index */
	}
	entry->spr_idx_kek = kwp_cfg->kek_spr_idx;
	entry->kek_sz_sel = kwp_cfg->kek_size;
	entry->din_sz_sel = kwp_cfg->din_size;

	/* Valid when "secure mode" and "chip finger print enable" */
	if ((kwp_cfg->sec_mode_en) && (kwp_cfg->cfp_en)) {
		entry->kek_use_fp_en = 1;
	}
	/* Set SM4 mode */
	if (kwp_cfg->aes_sm4_mode) {
		entry->sm4_mode = 1;
	}
	entry->cmd_code = DPE_SEC_KWP_WRAP;
	entry->key_wp_enc_ctrl = kwp_cfg->wrap_op;

	/* DW1 - non-secure mode: source address of key wrap/unwrap operation */
	/* DW1 - secure mode: source address of key unwrap opetaion */
	entry->src_addr = sram_to_dma(mem);

	/* DW2 - non-secure mode: result of key wrap/unwrap operation */
	/* DW2 - secure mode: result of key wrap operation */
	entry->rslt_addr = sram_to_dma(result);

	/* DW3 - secure mode: Unwrap operation */
	/* Valid when "secure" and "unwrap" condition */
	if ((kwp_cfg->sec_mode_en) && (kwp_cfg->wrap_op == SS_KEY_UWRAP)) {
		entry->uwp_rslt_sprx_sel = kwp_cfg->sec_ena_uwp_rslt_spr_idx;	 /* Write to spr idx */
		entry->uwp_rslt_sprx_wr_ena = 1; /* Enable spr write */
		entry->uwp_rslt_mek_wr_ena = 1;  /* Enable mek sram write */
		entry->uwp_rslt_mek_wr_addr = kwp_cfg->sec_ena_uwp_rslt_mek_idx; /* mek sram address 0 to 63 */
	}
    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);
	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;

	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

/*!
 * @brief Security Subsystem Random Number Generator function
 *
 * Program to start Data Process Engine's random number generator command.
 *
 * @param spr_idx	SPR register index to store random number
 * @param callback	DPE callback function pointer
 * @param ctx		DPE context
 *
 * @return		None
 */
fast_code void bm_ss_rn_gen(enum spr_reg_idx spr_idx, dpe_cb_t callback, void *ctx)
{

	u8 wptr_nxt = bm_wait_dpe_entry();
    u16 cb_ctx_idx = find_first_bit(&_dpe_ctxs_mgr, MAX_DPE_CTX_CNT);
	sys_assert(cb_ctx_idx != MAX_DPE_CTX_CNT);
    sys_assert(!dpe_busy);
    dpe_ctxs_allocated_cnt++;

	dpe_entry_t *entry = &_dpe_reqs[dpe_req_wptr];
	dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[cb_ctx_idx];
	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};


    clear_bit(cb_ctx_idx, &_dpe_ctxs_mgr);
	cb_ctx->cb = callback;
	cb_ctx->ctx = ctx;

	entry->hdr.all = 0;
	entry->hdr.req.bm_tag = cb_ctx_idx;
	entry->hdr.req.cmd_code = DPE_SEC_RND_NUMBER;
	entry->hdr.req.cmd_opt = spr_idx;

	dpe_req_wptr = wptr_nxt;
	queue.b.data_proc_req_wptr = wptr_nxt;
	btn_writel(queue.all, DATA_PROC_REQ_POINTER);
}

fast_code void dpe_isr(void)
{
	data_proc_res_pointer_t queue = {
		.all = btn_readl(DATA_PROC_RES_POINTER),
	};

	while (dpe_rsp_rptr != queue.b.data_proc_res_wptr) {
		dpe_entry_t *rsp_e = &_dpe_rsps[dpe_rsp_rptr];
		dpe_cb_ctx_t *cb_ctx = &_dpe_ctxs[rsp_e->hdr.rsp.rsp_tag];
		dpe_rst_t rst;

		rst.error = 0;
		switch (rsp_e->hdr.rsp.rsp_code) {
		case DPE_COPY:
		case DPE_MERGE:
		case DPE_FILL:
		case DPE_SHA3_SM3_CAL:
		case DPE_SEC_RSA_CAL:
			break;

		case DPE_CMP_FM1:
			rst.cmp_fmt1.cmp_err_cnt = rsp_e->hdr.rsp.rsp_sts;
			rst.cmp_fmt1.err_loc = rsp_e->payload[0];
			rst.cmp_fmt1.idx = rsp_e->hdr.req.param;
			break;

		case DPE_CMP_FM2:
			rst.cmp_fmt2.cmp_err_cnt = rsp_e->hdr.rsp.rsp_misc;
			rst.cmp_fmt2.err_loc = rsp_e->payload[0];
			break;

		case DPE_SEARCH:
			search_rsp_base[rsp_e->hdr.rsp.rsp_sts] = 0;
			rst.search.rsp_buf_id = rsp_e->hdr.rsp.rsp_sts;
			rst.search.search_hit = rsp_e->hdr.rsp.rsp_misc;
			break;

		case DPE_SM2_CAL:
			rst.sm2.func_sel = rsp_e->hdr.req.cmd_opt;
			rst.sm2.cal_sts.all = (u16) rsp_e->payload[0];
			break;
		case DPE_SEC_SPR_PROG:
		case DPE_SEC_KWP_WRAP:
			break;

		case DPE_SEC_RND_NUMBER:
			rst.trng.trng_chk_flag = rsp_e->hdr.rsp.rsp_misc;
			break;

		default:
			rst.error = -1;
			break;
		}

		if (rst.error != 0) {
			bf_mgr_trace(LOG_ERR, 0x3353, "DPE sts: %d code: %d",
				rst.error, rsp_e->hdr.rsp.rsp_code);
		}

		if (rst.error != -1 && cb_ctx->cb)
			cb_ctx->cb(cb_ctx->ctx, &rst);
        set_bit(rsp_e->hdr.rsp.rsp_tag,&_dpe_ctxs_mgr);
		dpe_ctxs_allocated_cnt--;
		dpe_rsp_rptr = (dpe_rsp_rptr + 1) & DPE_ENTRY_CNT_MSK;
    	queue.b.data_proc_res_rptr = dpe_rsp_rptr;
	    btn_writel(queue.all, DATA_PROC_RES_POINTER);
	}
}

#if defined(MPC)
fast_code int bm_copy_done(void *ctx, dpe_rst_t *rst)
{
	bm_copy_ipc_hdl_t *hdl = (bm_copy_ipc_hdl_t*)ctx;
	u8 rx = hdl->tx;

	cpu_msg_issue(rx, CPU_MSG_BM_COPY_DONE, 0, (u32)ctx);
	return 0;
}

fast_code void ipc_bm_copy(volatile cpu_msg_req_t *req)
{
	bm_copy_ipc_hdl_t *hdl = (bm_copy_ipc_hdl_t*)req->pl;
	hdl->tx = req->cmd.tx;
    
	bm_data_copy(hdl->src, hdl->dst, hdl->len, bm_copy_done, (void*)hdl);
}

static fast_code int ipc_sha3_sm3_calc_callback(void *ctx, dpe_rst_t *rst)
{
	sha3_sm3_calc_ipc_hdl_t *hdl = (sha3_sm3_calc_ipc_hdl_t *) ctx;

	cpu_msg_issue(hdl->tx, CPU_MSG_SHA3_SM3_CALLBACK, 0, (u32)hdl);
	return 0;
}

fast_code void ipc_ddr_sha3_sm3_calc(volatile cpu_msg_req_t *req)
{
	sha3_sm3_calc_ipc_hdl_t *hdl = (sha3_sm3_calc_ipc_hdl_t *) req->pl;
	hdl->tx = req->cmd.tx;

	bm_sha3_sm3_calc_part(hdl->mem, hdl->result, hdl->sm3,  hdl->count,
		hdl->cur_count, ipc_sha3_sm3_calc_callback, (void *)hdl, hdl->first);
	cpu_msg_sync_done(hdl->tx);
}
#endif

UNUSED init_code void dpe_early_init(void)
{
	data_proc_req_sram_base_t dpe_queue_base = {
		.all = 0
	};

	dpe_queue_base.b.data_proc_req_sram_base = sram_to_dma(_dpe_reqs);
	dpe_queue_base.b.data_proc_req_max_sz = DPE_ENTRY_CNT_CNT - 1;

	dpe_req_wptr = 0;
	dpe_rsp_rptr = 0;

	data_process_ctrl_sts_t ctrlr_sts = {
		.all = btn_readl(DATA_PROCESS_CTRL_STS),
	};

	if (ctrlr_sts.b.data_process_enable)
		btn_writel(0, DATA_PROCESS_CTRL_STS);

	btn_writel(dpe_queue_base.all, DATA_PROC_REQ_SRAM_BASE);
	btn_writel(sram_to_dma(_dpe_rsps), DATA_PROC_RES_SRAM_BASE);

	sync_dpe_reset();

	ctrlr_sts.b.data_process_enable = 1;
	btn_writel(ctrlr_sts.all, DATA_PROCESS_CTRL_STS);
}

ps_code void dpe_init(void)
{
	data_proc_req_sram_base_t dpe_queue_base = {
		.all = 0
	};

	dpe_queue_base.b.data_proc_req_sram_base = sram_to_dma(_dpe_reqs);
	dpe_queue_base.b.data_proc_req_max_sz = DPE_ENTRY_CNT_CNT - 1;

	dpe_req_wptr = 0;
	dpe_rsp_rptr = 0;
	dpe_ctxs_allocated_cnt = 0;
    memset(&_dpe_ctxs_mgr,0xFF,sizeof(_dpe_ctxs_mgr));

	data_process_ctrl_sts_t ctrlr_sts = {
		.all = btn_readl(DATA_PROCESS_CTRL_STS),
	};

	if (ctrlr_sts.b.data_process_enable)
		btn_writel(0, DATA_PROCESS_CTRL_STS);

	btn_writel(dpe_queue_base.all, DATA_PROC_REQ_SRAM_BASE);
	btn_writel(sram_to_dma(_dpe_rsps), DATA_PROC_RES_SRAM_BASE);

	sync_dpe_reset();

	ctrlr_sts.b.data_process_enable = 1;
	btn_writel(ctrlr_sts.all, DATA_PROCESS_CTRL_STS);

	memset(search_rsp_base, 0, MAX_SEARCH_IO_CNT * sizeof(u32));
	btn_writel(sram_to_dma(search_rsp_base), DP_SEARCH_RES_SRAM_BASE);
#if defined(MPC)
	cpu_msg_register(CPU_MSG_BM_COPY, ipc_bm_copy);
	cpu_msg_register(CPU_MSG_SHA3_SM3_CALC, ipc_ddr_sha3_sm3_calc);
#endif
}

__attribute__((unused)) void bm_dpe_dump_last_cmd(u32 id)
{
	data_proc_req_pointer_t queue = {
		.all = btn_readl(DATA_PROC_REQ_POINTER),
	};
	u32 last_req = queue.b.data_proc_req_wptr;

	last_req = (last_req - 1) & DPE_ENTRY_CNT_MSK;

	bf_mgr_trace(LOG_ERR, 0xa7a2, "last req dump: %d", last_req);

	bf_mgr_trace(LOG_ERR, 0x368f, "%x %x %x %x",
			_dpe_reqs[last_req].hdr.all,
			_dpe_reqs[last_req].payload[0],
			_dpe_reqs[last_req].payload[1],
			_dpe_reqs[last_req].payload[2]);
}
/*! @} */
