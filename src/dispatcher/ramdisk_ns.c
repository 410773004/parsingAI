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
/*! \file ramdisk.c
 * @brief Ramdisk support
 *
 * \addtogroup dispatcher
 * \defgroup ramdisk
 * \ingroup dispatcher
 * @{
 *
 * RAMDISK support DDR or SRAM
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
#include "bf_mgr.h"
#include "btn_export.h"
#include "mod.h"
#include "event.h"
#include "assert.h"
#include "rainier_soc.h"
#include "crc16.h"
#include "ddr.h"
#include "l2cache.h"

/*! \cond PRIVATE */
#define __FILEID__ ramdiskns
#include "trace.h"
/*! \endcond */

#include "btn_helper.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------


#if !defined(DDR)
	#error "plz define DDR"
#endif

#define RAMDISK_NS_CNT	2
#define RAMDISK_NS1_CAP		64
#define RAMDISK_NS2_CAP		512

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

typedef struct _ramdisk_ns_t {
	u32 dtag_type;
	u32 lda_cnt;
	dtag_t *l2d;
} ramdisk_ns_t;

/*!
 * @brief definition of DPE copy context
 */
typedef struct {
	dtag_t dtag;	///< dtag in the DPE copy context
} dpe_copy_ctx_t;

/*!
 * @brief definition of DPE compare context
 */
typedef struct {
	dtag_t dtag;	///< dtag in the DPE compare context
	bm_pl_t pl;	///< compare bm payload
} dpe_compare_ctx_t;

/*!
 * @brief definition of DPE merge context
 */
typedef struct {
	bm_pl_t bm_pl;	///< dtag in the DPE merge context
	dtag_t dst;	///< destination dtag
	lda_t lda;	///< merged LDA
	dtag_t copy;	///< source dtag of copy
} dpe_merge_ctx_t;

/* Due to conflict of evt types, you cannot change the type on-the-fly */

/*! @brief if no data processor, use a dtag table instead of copying data to ramdisk */
#define NR_ENTRIES_L2P		(RAMDISK_CAP >> DTAG_SHF)
static fast_data dtag_t unmap_cmp_dtag;			///< dtag in used compare command
static fast_data int ref_cmp = 0;			///< reference count
static fast_data bm_pl_que_t par_pl_q;			///< partial bm pl waiting queue
static fast_data bool par_merging = false;		///< only allow one merger

static fast_data ramdisk_ns_t _ramdisk_ns[2] = {
		{ .dtag_type = DTAG_T_SRAM, .lda_cnt = RAMDISK_NS1_CAP},
		{ .dtag_type = DTAG_T_DDR, .lda_cnt = RAMDISK_NS2_CAP}
};

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------

/*!
 * @brief callback function of data processor engine merge command
 *
 * Recycle the dtag after partial write merge process done.
 *
 * @param ctx		should be dpe_merge_ctx_t
 * @param rst		data processor engine response status
 *
 * @return		Always return 0
 */
static int ramdisk_merge_cb(void *ctx, dpe_rst_t *rst);

/*!
 * @brief common free queue updated handler in pre-assigned mode
 *
 * In pre-assigned mode, only read done dtags will be recycled to common free
 * queue, and we just dropped them, because they should be upper 1MB memory,
 * they are not in free dtag pool.
 *
 * @param[in] param	not used
 * @param[in] payload	should be BM payload list
 * @param[in] count	length of BM payload list
 *
 * @return		None
 */
static fast_code void
ramdisk_com_free_updt_pasg(u32 param, u32 payload, u32 count)
{
	dtag_put_bulk(DTAG_T_MAX, count, (dtag_t *) payload);
}

/*!
 * @brief recycle dtags after data processor engine copy command done
 *
 * After data copied, recycle dtag.
 *
 * @param[in] ctx	should be dpe_copy_ctx_t
 * @param[in] rst	data processor engine response status
 *
 * @return		Always return 0
 */
static int ramdisk_copy_cb(void *ctx, dpe_rst_t *rst)
{
	return 0;
}

/*!
 * @brief host normal write queue updated handler
 *
 * @param[in] payload	should be BM payload list
 *
 * @return		not used
 */
static fast_code void
ramdisk_nrm_wd_updt(bm_pl_t *bm_tag)
{
	dtag_t dtag = {.dtag = bm_tag->pl.dtag };
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_tag->pl.btag);
	btn_cmd_t *bcmd = btag2bcmd(bm_tag->pl.btag);
	lda_t lda = LBA_TO_LDA(bcmd_get_slba(bcmd));
	ramdisk_ns_t *rns;

	lda += bm_tag->pl.du_ofst;

	disp_apl_trace(LOG_DEBUG, 0x46ea, "btag(%d) Dtag(%d)", bm_tag->pl.btag, bm_tag->pl.dtag);

	dtag_t old_dtag;

	rns = &_ramdisk_ns[bcmd->dw1.b.ns_id - 1];
	old_dtag = rns->l2d[lda];
	rns->l2d[lda] = dtag;

	if (old_dtag.dtag != _inv_dtag.dtag && old_dtag.dtag != ~0)
		dtag_put_ex(old_dtag);

	if ((--bcmd_ex->du_xfer_left == 0) && (bcmd_ex->flags.b.wr_err == 0)) {	//< normal data path
		btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
	}
}

/*!
 * @brief calback function of dpe fill
 *
 * @param ctx	context
 * @param rst	DPE result buffer
 *
 * @return	not used
 */
static int ramdisk_fill_cb(void *ctx, dpe_rst_t *rst)
{
	return 0;
}

/*!
 * @brief recycle dtags after data processor engine compare command done
 *
 * Callback function to recycle dtags and free context resources after
 * data processor engine compare command finished.
 *
 * @param ctx		should be dpe_compare_ctx_t
 * @param rst		data processor engine response status
 *
 * @return		Always return 0
 */
static int ramdisk_cmp_cb(void *ctx, dpe_rst_t *rst)
{
	dpe_compare_ctx_t *cmp = ctx;
	btn_cmd_ex_t *bcmd_ex;
	int btag = cmp->pl.pl.btag;
	dtag_t dtag;

	sys_assert(rst->error != -1);
	bcmd_ex = btag2bcmd_ex(btag);
	bcmd_ex->du_xfer_left--;

	if (rst->cmp_fmt2.cmp_err_cnt)
		bcmd_ex->flags.b.err = 1;

	if (cmp->dtag.dtag == unmap_cmp_dtag.dtag) {
		ref_cmp--;
		if (ref_cmp == 0) {
			dtag_put_ex(unmap_cmp_dtag);
			unmap_cmp_dtag = _inv_dtag;
		}
	}

	dtag.dtag = cmp->pl.pl.dtag;
	dtag_put_ex(dtag);

	if (bcmd_ex->du_xfer_left == 0) {
		if (bcmd_ex->flags.b.err == 1)
			btn_cmd_rels_push(btag2bcmd(btag), RLS_T_WRITE);
		else
			btn_cmd_rels_push(btag2bcmd(btag), RLS_T_WRITE_CQ);
	}

	sys_free(FAST_DATA, ctx);
	return 0;
}

/*!
 * @brief host compare write queue updated event handler
 *
 * After received compare data entry, build data processor engine compare
 * command to compare it with ramdisk memory (upper 1M).
 *
 * @param[in] bm_tags	should be BM payload
 *
 * @return		not used
 */
static fast_code void
ramdisk_cmp_wd_updt(bm_pl_t *bm_tags)
{
	btn_cmd_t *bcmd = btag2bcmd(bm_tags->pl.btag);
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_tags->pl.btag);
	lda_t lda;
	dtag_t dtag;
	void *dst;
	void *src;
	dtag_t s = { .dtag = bm_tags->pl.dtag };
	u32 size;
	u64 slba;
	ramdisk_ns_t *rns;

	sys_assert(bcmd->dw1.b.compare == 1);
	slba = bcmd_get_slba(bcmd);
	lda = LBA_TO_LDA(slba);
	lda += bm_tags->pl.du_ofst;

	rns = &_ramdisk_ns[bcmd->dw1.b.ns_id - 1];
	dtag = rns->l2d[lda];
	if (dtag.dtag == _inv_dtag.dtag || dtag.dtag == ~0) {
		if (unmap_cmp_dtag.dtag == _inv_dtag.dtag) {
			unmap_cmp_dtag = dtag_get_urgt(DTAG_T_SRAM, &dst);
			bm_data_fill(dst, DTAG_SZE, ~0, ramdisk_fill_cb, NULL);
			ref_cmp = 0;
		}
		ref_cmp++;
		dtag = unmap_cmp_dtag;
	} else {
		dtag_ref_inc_ex(dtag);
	}

	dst = dtag2mem(dtag);
	src = dtag2mem(s);

	if (bm_tags->pl.du_ofst == 0) {
		u8 off = LBA_OFST_LDA(slba);
		u32 cnt;

		cnt = get_ua_head_cnt(off, bcmd->dw3.b.xfer_lba_num);
		if (off != 0) {
			src = ptr_inc(src, off * LBA_SIZE);
			dst = ptr_inc(dst, off * LBA_SIZE);
		}
		size = cnt * LBA_SIZE;
	} else if (bm_tags->pl.du_ofst == bcmd_ex->ndu - 1) {
		u32 cnt;

		cnt = get_ua_tail_cnt(slba, bcmd->dw3.b.xfer_lba_num);
		size = cnt * LBA_SIZE;
	} else {
		size = DTAG_SZE;
	}

	dpe_compare_ctx_t *cmp = sys_malloc(FAST_DATA, sizeof(dpe_compare_ctx_t));

	cmp->dtag = dtag;
	cmp->pl = *bm_tags;

	bm_data_compare_mem(src, dst, size, ramdisk_cmp_cb, cmp);
}

/*!
 * @brief api to check if partial waiting queue is empty or not
 *
 * @return	return true if empty
 */
static fast_code bool ramdisk_par_q_empty(void)
{
	return (par_pl_q.wptr == par_pl_q.rptr) ? true : false;
}

/*!
 * @brief issue DPE merge command to merge partial data to destination
 *
 * @param dst		destination dtag
 * @param bm_tag	partial bm payload
 *
 * @return		not used
 */
static fast_code void ramdisk_merge(dtag_t dst, bm_pl_t *bm_tag)
{
	dtag_t cur;
	u64 slba;
	btn_cmd_t *bcmd = btag2bcmd(bm_tag->pl.btag);
	u8 off;
	u8 cnt;
	dpe_merge_ctx_t *merge_ctx;
	dtag_t dtag = { .dtag = bm_tag->pl.dtag };
	lda_t lda;
	ramdisk_ns_t *rns;

	slba = bcmd_get_slba(bcmd);
	lda = LBA_TO_LDA(slba);
	lda += bm_tag->pl.du_ofst;

	rns = &_ramdisk_ns[bcmd->dw1.b.ns_id - 1];
	cur = rns->l2d[lda];
	if (cur.dtag == _inv_dtag.dtag) {
		//bm_data_fill(mem, DTAG_SZE, ~0, ramdisk_fill_cb, NULL);
		dtag_put_ex(dst);
		ramdisk_nrm_wd_updt(bm_tag);
		return;
	} else {
#if defined(USE_8K_DU)
		bm_data_merge(cur, dst, 0, NR_LBA_PER_LDA / 2, NULL, NULL);
		bm_data_merge(cur, dst, NR_LBA_PER_LDA / 2, NR_LBA_PER_LDA / 2, ramdisk_copy_cb, NULL);
#else
		bm_data_merge(cur, dst, 0, NR_LBA_PER_LDA, ramdisk_copy_cb, NULL);
#endif
		dtag_ref_inc_ex(cur);
	}

	if (bm_tag->pl.du_ofst == 0) {
		/** head */
		off = LBA_OFST_LDA(slba);
		cnt = get_ua_head_cnt(off, bcmd->dw3.b.xfer_lba_num);
	} else {
		/** must be the last DU, tail */
		off = 0;
		cnt = get_ua_tail_cnt(slba, bcmd->dw3.b.xfer_lba_num);
	}

	disp_apl_trace(LOG_DEBUG, 0xd564, "btag(%d) Dtag(%d)", bm_tag->pl.btag, bm_tag->pl.dtag);

	merge_ctx = sys_malloc(FAST_DATA, sizeof(*merge_ctx));
	sys_assert(merge_ctx != 0);

	merge_ctx->lda = lda;
	merge_ctx->bm_pl = *bm_tag;
	merge_ctx->dst = dst;
	merge_ctx->copy = cur;
	disp_apl_trace(LOG_DEBUG, 0x9e54, "merge DU(%d) %d(%d)", bm_tag->pl.du_ofst, off,cnt);
	par_merging = true;
	bm_data_merge(dtag, dst, off, cnt, ramdisk_merge_cb, merge_ctx);
}

/*!
 * @brief handler of partial payload queue, merge them one by one
 *
 * @return	not used
 */
static fast_code void ramdisk_par_q_handle(void)
{
	if (par_merging == true)
		return;

	while (par_pl_q.rptr != par_pl_q.wptr) {
		dtag_t dst = dtag_get_urgt(DTAG_T_SRAM, NULL);

		if (dst.dtag != _inv_dtag.dtag) {
			bm_pl_t *bm_pl = &par_pl_q.que[par_pl_q.rptr];

			ramdisk_merge(dst, bm_pl);
			par_pl_q.rptr++;
			if (par_pl_q.rptr >= BM_PL_QUE_SZ)
				par_pl_q.rptr = 0;
		}

		if (par_merging)
			break;
	}
}

fast_code int ramdisk_merge_cb(void *ctx, dpe_rst_t *rst)
{
	dpe_merge_ctx_t *merge_ctx = ctx;
	dtag_t dtag;
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(merge_ctx->bm_pl.pl.btag);
	btn_cmd_t *bcmd = btag2bcmd(merge_ctx->bm_pl.pl.btag);
	dtag_t old;
	ramdisk_ns_t *rns;

	par_merging = false;
	rns = &_ramdisk_ns[bcmd->dw1.b.ns_id - 1];
	old = rns->l2d[merge_ctx->lda];
	rns->l2d[merge_ctx->lda] = merge_ctx->dst;

	dtag.dtag = merge_ctx->bm_pl.pl.dtag;
	dtag_put_ex(dtag);
	if (old.dtag != _inv_dtag.dtag)
		dtag_put_ex(old);

	if (merge_ctx->copy.dtag != _inv_dtag.dtag)
		dtag_put_ex(merge_ctx->copy);

	if ((--bcmd_ex->du_xfer_left == 0)) {
		btn_cmd_t *bcmd = btag2bcmd(merge_ctx->bm_pl.pl.btag);

		btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
	}

	sys_free(FAST_DATA, ctx);

	if (ramdisk_par_q_empty() == false)
		ramdisk_par_q_handle();

	return 0;
}

/*!
 * @brief partial write entry handler
 *
 * After received partial write data entry, insert it to partial waiting queue
 * and handle them one by one
 *
 * @param bm_tag	partial write data entry
 *
 * @return		not used
 */
static fast_code void
ramdisk_part_wd_updt(bm_pl_t *bm_tag)
{
	bm_pl_que_ins(&par_pl_q, bm_tag);

	if (ramdisk_par_q_empty() == false) {
		ramdisk_par_q_handle();
		return;
	}
}

/*!
 * @brief event handler for btn write group 0 update
 *
 * @param param		not used
 * @param payload	payload address list
 * @param count		list count
 *
 * @return		not used
 */
fast_code static void ramdisk_wd_updt_nrm_par(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32 *) payload;
	u32 i;

	_free_otf_wd -= (int) count;
	if (_wr_evt) {
		_wr_evt = false;
		dtag_remove_evt(WR_DTAG_TYPE, &_wr_evt);
	}

	for (i = 0; i < count; i++) {
		bm_pl_t *bm_pl = (bm_pl_t *) dma_to_btcm(addr[i]);

		switch (bm_pl->pl.type_ctrl) {
		case BTN_NVM_TYPE_CTRL_NRM:
			ramdisk_nrm_wd_updt(bm_pl);
			break;
		case BTN_NVM_TYPE_CTRL_PAR:
			ramdisk_part_wd_updt(bm_pl);
			break;
		default:
			panic("no support");
			break;
		}
	}

	reload_free_dtag(NULL);
}

fast_code static void ramdisk_wd_updt_cmp(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32 *) payload;
	u32 i;

	_free_otf_wd -= (int) count;
	if (_wr_evt) {
		_wr_evt = false;
		dtag_remove_evt(WR_DTAG_TYPE, &_wr_evt);
	}

	for (i = 0; i < count; i++) {
		bm_pl_t *bm_pl = (bm_pl_t *) dma_to_btcm(addr[i]);
		ramdisk_cmp_wd_updt(bm_pl);
	}

	reload_free_dtag(NULL);
}

/*!
 * @brief ramdisk format function
 *
 * Format the entire ramdisk. If pi_enable is set, reset the lba mapping also.
 *
 * @param pi_enable		PI (protection info) enable or not
 *
 * @return			not used
 *
 * @note only pre-assign support PI in ramdisk
 */
static fast_code void ramdisk_format(ramdisk_ns_t *rns, bool pi_enable)
{
	int i;

	for (i = 0; i < rns->lda_cnt; i++) {
		if (rns->l2d[i].dtag != _inv_dtag.dtag && rns->l2d[i].dtag != ~0) {
			dtag_put_ex(rns->l2d[i]);
			rns->l2d[i] = _inv_dtag;
		}
	}
}

/*!
 * @brief ramdisk trim function
 *
 * @param req	trim request
 *
 * @return	not used
 */
static fast_code void ramdisk_trim(req_t *req)
{
	dtag_t dtag;

	dtag = mem2dtag(req->op_fields.trim.dsmr);

	if (req->completion) {
		disp_apl_trace(LOG_DEBUG, 0x81e0, "ramdisk finish dtag(%d)", dtag.b.dtag);
		req->completion(req);
	}
	dtag_put(DTAG_T_SRAM, dtag);
}

/*!
 * @brief btn command handler, handler write/read commands here
 *
 * @param bcmd		btn command
 * @param btag		tag of btn command
 *
 * @return		not used
 */
fast_code void bcmd_exec(btn_cmd_t *bcmd, int btag)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
	lda_t lda;
	u64 slba;
	u32 i;
	ramdisk_ns_t *rns = &_ramdisk_ns[bcmd->dw1.b.ns_id - 1];
	extern void btn_handler_com_free();
	slba = bcmd_get_slba(bcmd);
	bcmd_ex->ndu = calc_du_cnt(slba, bcmd->dw3.b.xfer_lba_num);

	switch (bcmd->dw0.b.cmd_type) {
	case PRI_READ:
	case NVM_READ:
		btn_handler_com_free();
		lda = LBA_TO_LDA(slba);

		for (i = 0; i < bcmd_ex->ndu; i++, lda++) {
			dtag_t dtag = rns->l2d[lda];

			if (dtag.dtag == ~0) {
				dtag.dtag = 0;
				dtag.b.dtag = EVTAG_ID;
				disp_apl_trace(LOG_ERR, 0x3575, "btag(%d) %x UC", btag, lda);
			} else if (dtag.dtag == _inv_dtag.dtag) {
				dtag.dtag = 0;
				dtag.b.dtag = RVTAG_ID;
			} else {
				dtag_ref_inc_ex(dtag);
			}

			bm_rd_dtag_commit(i, btag, dtag);
		}
		break;
	case NVM_WRITE:
		load_free_dtag(bcmd_ex->ndu);
		bcmd_ex->du_xfer_left += (short) bcmd_ex->ndu;
		if (bcmd_ex->du_xfer_left == 0 && bcmd_ex->flags.b.wr_err == 0)
			btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
		disp_apl_trace(LOG_DEBUG, 0x603e, "(W) btag(%d) NDU(%d)", btag, bcmd_ex->ndu);
		break;
	case IO_MGR:
		btn_iom_cmd_rels(bcmd);
		break;
	default:
		panic("not support");
		break;
	}
}

/*!
 * @brief ramdisk non-IO request execution
 *
 * For read/write, will be handled in bcmd_exec
 *
 * @param req		request to be executed.
 *
 * @return		not used
 */
fast_code bool req_exec(req_t *req)
{
	ramdisk_ns_t *rns = &_ramdisk_ns[req->nsid];

	switch (req->opcode) {
	case REQ_T_READ:
	case REQ_T_WRITE:
	case REQ_T_COMPARE:
		panic("not support");
		break;
	case REQ_T_FORMAT:
		ramdisk_format(rns, true);
		req->completion(req);
		break;
	case REQ_T_FLUSH:
		req->completion(req);
		break;
	case REQ_T_TRIM:
		ramdisk_trim(req);
		break;
	case REQ_T_WZEROS:
		{
			u64 slba = req->lba.srage.slba;
			u64 lba = slba; /* current lba */
			u16 nlb = req->lba.srage.nlb;
			u64 elba = slba + nlb;
			u32 xfer = 0;
			for (; lba < elba; lba += xfer) {
				int ofst = LBA_OFST_LDA(lba);
				int len = NR_LBA_PER_LDA - ofst;

				xfer =  nlb < len ? nlb : len;

				u32 cdu = lba >> NR_LBA_PER_LDA_SHIFT;
				memset(dtag2mem(rns->l2d[cdu]), 0 , DTAG_SZE);
				nlb -= xfer;
			}
			req->completion(req);
		}
		break;
	case REQ_T_WUNC:
		{
			u64 slba = req->lba.srage.slba;
			u64 lba = slba; /* current lba */
			u16 nlb = req->lba.srage.nlb;
			u64 elba = slba + nlb;
			u32 xfer = 0;
			for (; lba < elba; lba += xfer) {
				int ofst = LBA_OFST_LDA(lba);
				int len = NR_LBA_PER_LDA - ofst;

				xfer =  nlb < len ? nlb : len;

				u32 cdu = lba >> NR_LBA_PER_LDA_SHIFT;
				if (rns->l2d[cdu].dtag != ~0 && rns->l2d[cdu].dtag != _inv_dtag.dtag)
					dtag_put_ex(rns->l2d[cdu]);

				rns->l2d[cdu].dtag = ~0;
				nlb -= xfer;
			}
			req->completion(req);
		}
		break;
	default:
		sys_assert(0);
		break;
	}

	return true;
}

/*!
 * @brief ramdisk initialize
 *
 * Register all BTN event handler functions.
 *
 * @return	not used
 */
static void ramdisk_init(void)
{
	nvme_ctrl_attr_t ctrl_attr;
	u32 i;

	evt_register(ramdisk_wd_updt_nrm_par, 0, &evt_wd_grp0_nrm_par_upt);
	evt_register(ramdisk_wd_updt_cmp, 0, &evt_wd_grp0_cmp_upt);
	evt_register(ramdisk_com_free_updt_pasg, 0, &evt_com_free_upt);
	evt_register(wd_err_updt, 0, &evt_wd_err_upt);
#if defined(HMB_DTAG)
	evt_register(ramdisk_hmb_rd_updt, 0, &evt_hmb_rd_upt);
#endif

	for (i = 0; i < RAMDISK_NS_CNT; i++) {
		u32 lda_cnt = _ramdisk_ns[i].lda_cnt;
		u32 j;

		_ramdisk_ns[i].l2d = sys_malloc(FAST_DATA, sizeof(dtag_t) * lda_cnt);
		if (_ramdisk_ns[i].l2d == NULL)
			_ramdisk_ns[i].l2d = sys_malloc(SLOW_DATA, sizeof(dtag_t) * lda_cnt);

		sys_assert(_ramdisk_ns[i].l2d);
		for (j = 0; j < lda_cnt; j++) {
			_ramdisk_ns[i].l2d[j] = _inv_dtag;
		}
	}

	unmap_cmp_dtag = _inv_dtag;

	btn_cmd_hook(bcmd_exec, NULL);

	ctrl_attr.all = 0;
	ctrl_attr.b.compare = 1;
	ctrl_attr.b.write_uc = 1;
	ctrl_attr.b.dsm = 1;
	ctrl_attr.b.write_zero = 1;
	nvmet_set_ctrlr_attrs(&ctrl_attr);

	nvme_ns_attr_t attr[RAMDISK_NS_CNT];
	nvme_ns_attr_t *p_ns;

	for (i = 0; i < RAMDISK_NS_CNT; i++) {
		memset((void*)&attr[i], 0, sizeof(nvme_ns_attr_t));
		p_ns = &attr[i];
		p_ns->hw_attr.nsid = 1 + i;
		p_ns->hw_attr.pad_pat_sel = 1;
		p_ns->fw_attr.support_pit_cnt = 0;
		p_ns->fw_attr.support_lbaf_cnt = 1;
		p_ns->fw_attr.type = NSID_TYPE_ACTIVE;
		p_ns->fw_attr.ncap = LDA_TO_LBA(_ramdisk_ns[i].lda_cnt);
		p_ns->fw_attr.nsz = LDA_TO_LBA(_ramdisk_ns[i].lda_cnt);
		p_ns->hw_attr.lb_cnt = LDA_TO_LBA(_ramdisk_ns[i].lda_cnt);
		nvmet_set_ns_attrs(p_ns, true);
	}

	nvmet_restore_feat(NULL);

#if defined(USE_CRYPTO_HW)
	extern void security_test_init(void);
	security_test_init();
#endif

	disp_apl_trace(LOG_ALW, 0x33fa, "Ramdisk Init done");
}

/*! \cond PRIVATE */
module_init(ramdisk_init, DISP_APL);
/*! \endcond */
/*! @} */
