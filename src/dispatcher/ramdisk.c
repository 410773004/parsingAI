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
#include "pmu.h"

/*! \cond PRIVATE */
#define __FILEID__ ramdisk
#include "trace.h"
/*! \endcond */

#include "btn_helper.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

#if defined(DDR)
 #define RAMDISK_CAP 		(256 * 1024)		///< ASIC only use 256K for data store, no Data integrity
 #define RAMDISK_DTAG_TYPE	DTAG_T_DDR
#else
 #define RAMDISK_CAP 		(256 * 1024)		///< ASIC only use 256K for data store, no Data integrity
 #define RAMDISK_DTAG_TYPE	DTAG_T_SRAM
#endif

#define LBCNT			(RAMDISK_CAP >> HLBASZ)	///< lba count

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
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
static fast_data dtag_t ramdisk_map[NR_ENTRIES_L2P];	///< L2D table
static fast_data bool ramdisk_map_unc[NR_ENTRIES_L2P];	///< support write uncorrectable command
static fast_data dtag_t unmap_cmp_dtag;			///< dtag in used compare command
static fast_data int ref_cmp = 0;			///< reference count
static fast_data bm_pl_que_t par_pl_q;			///< partial bm pl waiting queue
static fast_data bool par_merging = false;		///< only allow one merger
#if defined(HMB_DTAG)
static fast_data u32 hmb_req_cnt = 0;
static fast_data u32 hmb_rsp_cnt = 0;
#endif

extern u8 evt_reset_disk;

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
#if defined(HMB_DTAG)
	u32 j;
	for(j = 0; j < count; j++) {
		dtag_t *dtag = &((dtag_t *)payload)[j];
		u32 i;

		disp_apl_trace(LOG_DEBUG, 0x74c8, "dtag %x, type %d count %d", dtag->b.dtag, dtag->b.type, count);
		if (dtag->b.type == 0) {
			dtag_put(DTAG_T_SRAM, *dtag);
		} else {
			hmb_rsp_cnt ++;
			for (i = 0; i < NR_ENTRIES_L2P; i++) {
				if (ramdisk_map[i].b.dtag == dtag->b.dtag)
					ramdisk_map[i].b.type = 1;
			}
		}
	}
	return;
#endif

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

	lda += bm_tag->pl.du_ofst;
	dtag_t old_dtag = ramdisk_map[lda];

	disp_apl_trace(LOG_DEBUG, 0x1fd2, "btag: 0x%x dtag N(%x) O (%x)",
		bm_tag->pl.btag, dtag.dtag, bm_tag->pl.du_ofst);

	ramdisk_map[lda] = dtag;
	ramdisk_map_unc[lda] = false;
	if (old_dtag.dtag != _inv_dtag.dtag)
		dtag_put_ex(old_dtag);

#if defined(HMB_DTAG)
	bm_hmb_req(bm_tag, dtag.b.dtag, false);
	hmb_req_cnt ++;
#endif

	if ((--bcmd_ex->du_xfer_left == 0) && (bcmd_ex->flags.b.wr_err == 0))	//< normal data path
		btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
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

	sys_assert(bcmd->dw1.b.compare == 1);
	slba = bcmd_get_slba(bcmd);
	lda = LBA_TO_LDA(slba);
	lda += bm_tags->pl.du_ofst;

	dtag = ramdisk_map[lda];
	if (dtag.dtag == _inv_dtag.dtag) {
		if (unmap_cmp_dtag.dtag == _inv_dtag.dtag) {
			unmap_cmp_dtag = dtag_get_urgt(RAMDISK_DTAG_TYPE, &dst);
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

	slba = bcmd_get_slba(bcmd);
	lda = LBA_TO_LDA(slba);
	lda += bm_tag->pl.du_ofst;

	cur = ramdisk_map[lda];
	if (cur.dtag == _inv_dtag.dtag) {
		//bm_data_fill(mem, DTAG_SZE, ~0, ramdisk_fill_cb, NULL);
		dtag_put(MERGE_DST_TYPE, dst);
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

	disp_apl_trace(LOG_DEBUG, 0x0e5c, "btag(%d) Dtag(%d)", bm_tag->pl.btag, bm_tag->pl.dtag);

	merge_ctx = sys_malloc(FAST_DATA, sizeof(*merge_ctx));
	sys_assert(merge_ctx != 0);

	merge_ctx->lda = lda;
	merge_ctx->bm_pl = *bm_tag;
	merge_ctx->dst = dst;
	merge_ctx->copy = cur;
	disp_apl_trace(LOG_DEBUG, 0x9c58, "merge DU(%d) %d(%d)", bm_tag->pl.du_ofst, off,cnt);
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
		dtag_t dst = dtag_get_urgt(MERGE_DST_TYPE, NULL);

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
	dtag_t old;

	par_merging = false;
	old = ramdisk_map[merge_ctx->lda];
	ramdisk_map[merge_ctx->lda] = merge_ctx->dst;
	//iol grp11.0.1.0 fail issue for 512byte sector size[#5462]
	ramdisk_map_unc[merge_ctx->lda] = false;
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

#if 0
/*!
 * @brief update host crc of partial dtag
 *
 * @param dtag		partial data tag
 * @param start		start lba whose hcrc was updated
 * @param cnt		count of hcrc updated lba
 * @param du		du of this dtag
 *
 * @return		not used
 */
static fast_code void ramdisk_update_par_hcrc(dtag_t dtag, u8 start,
		u8 cnt, u32 du)
{
	u32 i;
	u32 _lba = du << NR_LBA_PER_LDA_SHIFT;
	u16 pos = (dtag.dtag << NR_LBA_PER_LDA_SHIFT);
	u32 off = 0;

	for (i = 0; i < NR_LBA_PER_LDA; i++) {
		u16 crc;

		if (start <= i && i < start + cnt) {
			off += (1 << HLBASZ);
			continue;
		}
		memset(dtag2mem(dtag) + off, 0xff, (1 << HLBASZ));
		crc = crc16_lkup(_lba + i, 1);
		btn_hcrc_write(pos + i, crc);
		off += (1 << HLBASZ);
	}
}
#endif

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

#if defined(HMB_DTAG)
static fast_code void
ramdisk_hmb_rd_updt(u32 param, u32 payload, u32 count)
{
	u32 i = 0;
	dtag_t hdtag;

	bm_pl_t *pl = &((bm_pl_t *) payload)[0];
	hmb_rsp_cnt ++;
	/* free hdtag */
	for (i = 0; i < count; i++) {
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(pl[i].pl.btag);
		sys_assert(pl[i].hpl.type_ctrl == 5); // write-data-entry read back
		/* put hdtag */
		hdtag.dtag = pl[i].hpl.hmb_ofst_p1 | (pl[i].hpl.hmb_ofst_p2 << 12);
		hdtag.b.type = DTAG_T_SRAM;
		hdtag.b.in_ddr = 0;

		disp_apl_trace(LOG_DEBUG, 0xf0a4, "hmd read dtag(%d) succss", hdtag.dtag);

		bm_rd_dtag_commit(bcmd_ex->du_xfer_left, pl->pl.btag, hdtag);
		bcmd_ex->du_xfer_left++;
	}
}
#endif

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
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_pl->pl.btag);
		// disp_apl_trace(LOG_ERR, 0, "WD dtag(%x)", bm_pl->pl.dtag);

		if (bcmd_ex->flags.b.wr_err) {
			dtag_t dtag = { .dtag = bm_pl->pl.dtag };

			dtag_put_ex(dtag);
			--bcmd_ex->du_xfer_left;
			continue;
		}

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
static fast_code void ramdisk_format(bool pi_enable)
{
	int i;

	for (i = 0; i < NR_ENTRIES_L2P; i++) {
		if (ramdisk_map[i].dtag != _inv_dtag.dtag) {
			dtag_put_ex(ramdisk_map[i]);
			ramdisk_map[i].dtag = _inv_dtag.dtag;
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
		disp_apl_trace(LOG_DEBUG, 0xc4cd, "ramdisk finish dtag(%d)", dtag.b.dtag);
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
	extern void btn_handler_com_free();
	slba = bcmd_get_slba(bcmd);
	bcmd_ex->ndu = calc_du_cnt(slba, bcmd->dw3.b.xfer_lba_num);

	switch (bcmd->dw0.b.cmd_type) {
	case PRI_READ:
	case NVM_READ:
		btn_handler_com_free();
		lda = LBA_TO_LDA(slba);

		for (i = 0; i < bcmd_ex->ndu; i++, lda++) {
			dtag_t dtag;

			if (ramdisk_map_unc[lda] == true) {
				dtag.dtag = 0;
				dtag.b.dtag = EVTAG_ID;
				disp_apl_trace(LOG_ERR, 0x1191, "btag(%d) %x UC", btag, lda);
			} else if (ramdisk_map[lda].dtag == _inv_dtag.dtag) {
				dtag.dtag = 0;
				dtag.b.dtag = RVTAG_ID;
			} else {
				dtag = ramdisk_map[lda];
				dtag_ref_inc_ex(dtag);
			}

#if defined(HMB_DTAG)
			if (dtag.b.type != 0) {
				bcmd_ex->du_xfer_left = 0;
				bm_pl_t bm_tag;

				bm_tag.all = 0;

				bm_tag.pl.btag       =  btag;
				bm_tag.pl.nvm_cmd_id = 0;
				bm_tag.pl.dtag       = dtag.b.dtag;
				bm_hmb_req(&bm_tag, dtag.b.dtag, true);
				hmb_req_cnt ++;

			} else
#endif
			bm_rd_dtag_commit(i, btag, dtag);
		}
		break;
	case NVM_WRITE:
		load_free_dtag(bcmd_ex->ndu);
		bcmd_ex->du_xfer_left += (short) bcmd_ex->ndu;
		if (bcmd_ex->du_xfer_left == 0 && bcmd_ex->flags.b.wr_err == 0)
			btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
		disp_apl_trace(LOG_DEBUG, 0xbc4c, "(W) btag(%d) NDU(%d)", btag, bcmd_ex->ndu);
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
	switch (req->opcode) {
	case REQ_T_READ:
	case REQ_T_WRITE:
	case REQ_T_COMPARE:
		panic("not support");
		break;
	case REQ_T_FORMAT:
		ramdisk_format(true);
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
				memset(dtag2mem(ramdisk_map[cdu]), 0 , DTAG_SZE);
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
				ramdisk_map_unc[cdu] = true;
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

static slow_code void ramdisk_reset_disk(u32 param, u32 p1, u32 p2)
{
	if (0 == p2) {
		_wr_credit = 0;
	} else if (1 == p2) {
		// move to btn_callbacks_t->write_err
	} else {
		// do nothing for ramdisk
	}
}
static slow_code void
ramisk_power_state_change(u32 param, u32 ps, u32 count)
{
	disp_apl_trace(LOG_ALW, 0xfb0c, "set PS %d", ps);
	if (4 == ps) {
		//sys_assert(bcmd_list_empty(&bcmd_pending) == true);
		sys_sleep(SLEEP_MODE_PS4);
	} else {
		sys_sleep_cancel();
	}

	return;
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

	evt_register(ramdisk_wd_updt_nrm_par, 0, &evt_wd_grp0_nrm_par_upt);
	evt_register(ramdisk_wd_updt_cmp, 0, &evt_wd_grp0_cmp_upt);
	evt_register(ramdisk_com_free_updt_pasg, 0, &evt_com_free_upt);
	evt_register(wd_err_updt, 0, &evt_wd_err_upt);
#if defined(HMB_DTAG)
	evt_register(ramdisk_hmb_rd_updt, 0, &evt_hmb_rd_upt);
#endif
	evt_register(ramdisk_reset_disk, 0, &evt_reset_disk);
	evt_register(ramisk_power_state_change, 0, &evt_change_ps);
	u32 i;

	for (i = 0; i < NR_ENTRIES_L2P; i++) {
		ramdisk_map[i] = _inv_dtag;
		ramdisk_map_unc[i] = false;
	}
	unmap_cmp_dtag = _inv_dtag;

	btn_cmd_hook(bcmd_exec, NULL);

	ctrl_attr.all = 0;
	ctrl_attr.b.compare = 1;
	ctrl_attr.b.write_uc = 1;
	ctrl_attr.b.dsm = 1;
	ctrl_attr.b.write_zero = 1;
	nvmet_set_ctrlr_attrs(&ctrl_attr);

	nvme_ns_attr_t attr;
	memset((void*)&attr, 0, sizeof(attr));

	nvme_ns_attr_t *p_ns = &attr;
	p_ns->hw_attr.nsid = 1;
	p_ns->hw_attr.pad_pat_sel = 1;
	p_ns->fw_attr.support_pit_cnt = 0;
	p_ns->fw_attr.support_lbaf_cnt = 1;
	p_ns->fw_attr.type = NSID_TYPE_ACTIVE;
	p_ns->fw_attr.ncap = LBCNT;
	p_ns->fw_attr.nsz = LBCNT;
	p_ns->hw_attr.lb_cnt = LBCNT;

	nvmet_set_ns_attrs(p_ns, true);

	nvmet_restore_feat(NULL);

#if defined(USE_CRYPTO_HW)
	extern void security_test_init(void);
	security_test_init();
#endif

	disp_apl_trace(LOG_ALW, 0xfc1f, "Ramdisk Init done DTAG %d, MERGE %d",
			WR_DTAG_TYPE, MERGE_DST_TYPE);
}

/*! \cond PRIVATE */
module_init(ramdisk_init, DISP_APL);
/*! \endcond */
/*! @} */
