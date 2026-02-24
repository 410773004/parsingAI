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


#include "ftlprecomp.h"
#include "ftl_flash_geo.h"
#include "bf_mgr.h"
#include "ncl_exports.h"
#include "recon.h"
#include "fc_export.h"
#include "idx_meta.h"
#include "sync_ncl_helper.h"
#include "l2p_mgr.h"
#include "ipc_api.h"
#include "l2cache.h"
#include "spb_mgr.h"
#include "ftl_remap.h"
#include "ftl_ns.h"
#include "mpc.h"

/*! \cond PRIVATE */
#define __FILEID__ recon
#include "trace.h"
/*! \endcond */
#if 0
#define RECON_P2L_GRP_CNT	2

/*! @brief reconstruction resource */
typedef struct _recon_res_t {
	bm_pl_t *bm_pl;			///< bm payload for reconstruction read, length = res_cnt
	struct info_param_t *info;	///< ncl info list for reconstruction read, length = res_cnt
	pda_t *pda;			///< pda list for reconstruction read, length = read_len
	lda_t *lda;			///< lda list for reconstruction map insert
	sn_t *sn;			///< sn list for reconstruction map insert

	dtag_t dtag;			///< use single dtag for all du read
	u32 meta_idx;			///< the start of meta index, meta length = res_cnt
	io_meta_t *meta;		///< meta buffer, list length = res_cnt

	u16 read_len;			///< read length
	u16 intlv_len;
	u16 err_du_cnt;
	u16 era_du_cnt;

	pda_t start_pda;
	u16 p2l_grp_id[RECON_P2L_GRP_CNT];	///< p2l group id
	u32 p2l_grp_dtag[RECON_P2L_GRP_CNT];	///< p2l group dtag start
	p2l_load_req_t p2l_load;	///< p2l load req

	spb_fence_t recon_fence;
} recon_res_t;

init_data u32 raid_lun;
init_data recon_res_t recon_res;
share_data_zi p2l_ext_para_t p2l_ext_para;
share_data_zi u8 *shr_range_ptr;
share_data_zi u8 *shr_range_wnum;
share_data u32 shr_gc_ddtag_start;
share_data_zi void *shr_ddtag_meta;

static inline void recon_res_init(recon_res_t *recon_res, u32 interleave, bool slc)
{
	u32 i;

	memset(recon_res->info, 0, sizeof(struct info_param_t) * interleave);

	for (i = 0; i < recon_res->read_len; i++) {
		recon_res->bm_pl[i].pl.dtag = recon_res->dtag.dtag;
		recon_res->bm_pl[i].pl.nvm_cmd_id = recon_res->meta_idx + i;
		recon_res->bm_pl[i].pl.du_ofst = i;
		recon_res->bm_pl[i].pl.btag = 0;
		recon_res->bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
		recon_res->info[i].pb_type = slc ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;
	}
}

init_code u32 recon_read(recon_res_t *recon_res, pda_t pda, bool slc, bool host)
{
	u32 i;
	u16 ret;
	u32 erased_cnt = 0;

	for (i = 0; i < recon_res->read_len; i++) {
		// consider defect here
		recon_res->meta[i].lda = 0x12345678;
		recon_res->pda[i] = ftl_remap_pda(pda + i);

		recon_res->info[i].status = 0;
		recon_res->info[i].pb_type = slc ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;
	}

#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush(recon_res->meta, recon_res->read_len * sizeof(io_meta_t));
#endif

	ftl_apl_trace(LOG_DEBUG, 0x2f43, "Read %x-%d", recon_res->pda[0], recon_res->read_len);
	log_level_t old = ipc_api_log_level_chg(LOG_ALW);
	ret = ncl_read_dus(recon_res->pda, recon_res->read_len, recon_res->bm_pl,
			recon_res->info, recon_res->info[0].pb_type, true, host);

	ipc_api_log_level_chg(old);

	if (ret != 0) {
		bool discard = false;
		u32 chk_unit = get_mp() << DU_CNT_SHIFT;

		for (i = 0; i < recon_res->read_len; i += chk_unit) {
			u32 j;
			nal_status_t s = ficu_err_good;

			for (j = 0; j < chk_unit; j++) {
				ftl_apl_trace(LOG_ERR, 0x135f, "pda %x st %d", recon_res->pda[i + j],
						recon_res->info[i + j].status);

				if (ficu_du_data_good(recon_res->info[i + j].status))
					continue;

				discard = true;
				if (s == ficu_err_good)
					s = recon_res->info[i + j].status;
				else if (s != recon_res->info[i + j].status)
					s = ficu_err_du_uc;
			}

			if (discard == false)
				continue;

			// discard check unit
			ftl_apl_trace(LOG_ERR, 0xc043, "discard MP %x-%d(%d)", recon_res->pda[i], chk_unit, s);
			if (s == ficu_err_du_erased)
				erased_cnt += chk_unit;

			recon_res->meta[i].lda = DONE_LDA;
#ifndef LJ_Meta
			recon_res->meta[i].sn = ~0;
#endif
			for (j = 1; j < chk_unit; j++) {
				recon_res->meta[i + j].lda = ERR_LDA;
#ifndef LJ_Meta
				recon_res->meta[i + j].sn = ~0;
#endif
			}
		}
	}

	return erased_cnt;
}

init_code bool is_raid_lun(u32 idx)
{
	u32 interleave = nal_get_interleave() << DU_CNT_SHIFT;

	idx = idx & (interleave - 1);
	if (idx == raid_lun)
		return true;

	return false;
}

static inline u32 next_hptr(recon_res_t *recon_res, u32 hptr, u32 unit, u32 total)
{
	//ftl_apl_trace(LOG_ERR, 0, "> hptr %d %d\n", hptr, cur_sn);
	do {
		if (hptr >= total)
			break;

		hptr += unit;

	//	ftl_apl_trace(LOG_ERR, 0, "- hptr %d \n", hptr, recon_res->meta[hptr].sn);
	} while (recon_res->lda[hptr] == DONE_LDA || is_raid_lun(hptr));
	return hptr;
}

static inline u32 next_vptr(recon_res_t *recon_res, u32 vptr, u32 unit, u32 interleave, u32 total)
{
	do {
		if (vptr >= total)
			break;

		if (vptr == total - unit) {
			if (is_raid_lun(vptr))
				return total;
			else
				break;
		}

		vptr += interleave;
		if (vptr >= total) {
			vptr -= total;
			vptr += unit;
			// next die
		}

	} while (recon_res->lda[vptr] == DONE_LDA || is_raid_lun(vptr));
	return vptr;
}

init_code void recon_insert_map(recon_res_t *recon_res, u32 pc)
{
	u32 i;
	u32 interleave = nal_get_interleave() * DU_CNT_PER_PAGE;
	u32 chk_unit = get_mp() << DU_CNT_SHIFT;
	u32 total = pc * interleave;
	u32 hptr = chk_unit;
	u32 vptr = interleave;
	u32 idx = 0;
	u32 debug_sn = 0;

	for (i = 0; i < total; i += chk_unit) {
		u32 j;

		if (idx >= total)
			break;

		ftl_apl_trace(LOG_DEBUG, 0xd361, "INS %d, (%x)", idx, recon_res->sn[idx]);

		for (j = 0; j < chk_unit; j++) {
			if (recon_res->lda[idx + j] == ERR_LDA)
				break;

			if (recon_res->lda[idx + j] == INV_LDA)
				break;

			if (recon_res->lda[idx + j] == DONE_LDA)
				break;

			sys_assert(recon_res->sn[idx] >= debug_sn);
			debug_sn = recon_res->sn[idx];

			// insert map
			pda_t p = recon_res->start_pda + idx + j;

			sys_assert(p != 0);
			l2p_updt_pda(recon_res->lda[idx + j],
					p, INV_PDA, false, true, 2, 0);
			/*ftl_apl_trace(LOG_ERR, 0, "blk %d du %d pl %d ch %d ce %d lun %d pg %d\n",
					fda.blk, fda.du, fda.pl, fda.ch, fda.ce, fda.lun, fda.pg);*/
		}

		recon_res->lda[idx] = DONE_LDA;	// mark this unit finished

		// determine next step
		//ftl_apl_trace(LOG_ERR, 0, "h %d - %x\n", hptr, recon_res->sn[hptr]);
		//ftl_apl_trace(LOG_ERR, 0, "v %d - %x\n", vptr, recon_res->sn[vptr]);
		if (hptr == vptr) {
			idx = hptr;
			hptr = next_hptr(recon_res, hptr, chk_unit, total);
			vptr = next_vptr(recon_res, vptr, chk_unit, interleave, total);
			//ftl_apl_trace(LOG_ERR, 0, "next vptr %d(%x) next hptr %d(%x)\n", vptr, recon_res->lda[vptr], hptr, recon_res->lda[hptr]);
		} else if (hptr >= total) {
			goto next_vptr;
		} else if (vptr >= total) {
			goto next_hptr;
		} else if (recon_res->sn[hptr] < recon_res->sn[vptr]) {
		next_hptr:
			idx = hptr;
			hptr = next_hptr(recon_res, hptr, chk_unit, total);
			//ftl_apl_trace(LOG_ERR, 0, "next hptr %d\n", hptr);
		} else if (recon_res->sn[vptr] <= recon_res->sn[hptr]) {
		next_vptr:
			idx = vptr;
			vptr = next_vptr(recon_res, vptr, chk_unit, interleave, total);
			//ftl_apl_trace(LOG_ERR, 0, "next vptr %d\n", vptr);
		} else {
			panic("impossible");
		}
	}
}

init_code void recon_filter_rst(recon_res_t *recon_res, u32 pc)
{
	u32 chk_unit = get_mp() << DU_CNT_SHIFT;
	u32 interleave = nal_get_interleave() * DU_CNT_PER_PAGE;
	u32 i;

	for (i = 0; i < interleave; i += chk_unit) {
		u32 j;

		if (recon_res->lda[i] == ERR_LDA) {
			for (j = 1; j < pc; j++) {
				u32 k;

				if (recon_res->lda[i + interleave] == ERR_LDA)
					break;

				for (k = 0; k < chk_unit; k++)
					recon_res->lda[i + interleave + k] = ERR_LDA;
			}
		}
	}
}

init_code bool recon_force_scan_spb(spb_id_t can, u32 nsid, bool slc)
{
	u32 interleave = nal_get_interleave() * DU_CNT_PER_PAGE;
	u32 p = 0;
	u32 end = slc ? get_slc_page_per_block() : get_page_per_block();
	bool host = nsid != FTL_NS_ID_INTERNAL ? true : false;

	recon_res_init(&recon_res, interleave, slc);

	ftl_apl_trace(LOG_INFO, 0xb8be, "force scan %d(%d), %d", can, nsid, slc);

	while (p < end) {
		u32 pc = slc ? 1 : 3; // page count
		u32 erased_cnt = 0;
		pda_t pda = nal_make_pda(can, p * interleave);
		u32 j;

		recon_res.start_pda = pda;

		for (j = 0; j < interleave * pc; j += recon_res.read_len) {
			erased_cnt += recon_read(&recon_res, pda, is_spb_slc(can), host);
			pda += recon_res.read_len;
		}

		if (erased_cnt == interleave * pc) {
			ftl_apl_trace(LOG_INFO, 0x84d9, "core WL empty");
			break;	// empty core WL
		}

		recon_filter_rst(&recon_res, pc);
		recon_insert_map(&recon_res, pc);
		p += pc;
	}

	if (p == 0)
		return true;

	return false;
}

init_code void recon_scan_intlv(spb_id_t spb, u32 page, u32 wl_off, bool host)
{
	u32 i;
	pda_t *pda_list = recon_res.pda;
	u32 intlv_len = recon_res.intlv_len;
	bm_pl_t *bm_pl = recon_res.bm_pl;
	struct info_param_t *info = recon_res.info;
	pda_t intlv_start_pda = nal_make_pda(spb, page * intlv_len);
	u32 spb_type = is_spb_slc(spb) ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;

	recon_res.err_du_cnt = 0;
	recon_res.era_du_cnt = 0;

	//reset info list
	for (i = 0; i < intlv_len; i++) {
		info[i].status = 0;
		info[i].pb_type = spb_type;
		pda_list[i] = ftl_remap_pda(intlv_start_pda + i);
	}

#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush(recon_res.meta, intlv_len * sizeof(io_meta_t));
#endif

	//read half interleave
	log_level_t old = ipc_api_log_level_chg(LOG_ALW);
	ncl_read_dus(pda_list, intlv_len, bm_pl, info, spb_type, false, host);
	ipc_api_log_level_chg(old);

	//check read status
	u32 lda_off = wl_off * intlv_len;
	for (i = 0; i < intlv_len; i++) {
		u32 intlv_off = recon_res.pda[i] & (intlv_len - 1);
		if (recon_res.info[i].status == 0) {
			recon_res.lda[lda_off + intlv_off] = recon_res.meta[i].lda;
			continue;
		}

		recon_res.err_du_cnt++;
		recon_res.lda[lda_off + intlv_off] = ERR_LDA;
		if (recon_res.info[i].status == ficu_err_du_erased)
			recon_res.era_du_cnt++;
	}
}

init_code bool recon_chk_spb_closed(spb_id_t spb, bool host)
{
	u32 page;
	u32 spb_flag = spb_get_flag(spb);

	if ((spb_flag & SPB_DESC_F_CLOSED) && !(spb_flag & SPB_DESC_F_OPEN))
		return true;

	if (is_spb_slc(spb))
		page = get_slc_page_per_block() - 1;
	else
		page = get_page_per_block() - 1;

	recon_scan_intlv(spb, page, 0, host);
	return (recon_res.err_du_cnt == 0) ? true : false;
}

static inline u32 get_cwl_page_cnt(spb_id_t spb, u32 page)
{
	if (is_spb_slc(spb))
		return 1;

#ifdef USE_TSB_NAND
	return 3;
#else
	return mu_get_xlc_cwl(page);
#endif
}

init_code dtag_t recon_get_p2l_dtag(spb_id_t spb, u32 p2l_id)
{
	u32 i;
	u32 slot = ~0;
	dtag_t p2l_dtag;
	u32 ttl_p2l_cnt;
	u32 min_grp_id = ~0;
	p2l_load_req_t *p2l_req = &recon_res.p2l_load;
	u32 grp_id = p2l_id / p2l_ext_para.p2l_per_grp;
	u32 grp_off = p2l_id % p2l_ext_para.p2l_per_grp;

	//search in current p2l groups
	for (i = 0; i < RECON_P2L_GRP_CNT; i++) {
		if (recon_res.p2l_grp_id[i] == grp_id) {
			p2l_dtag.dtag = (recon_res.p2l_grp_dtag[i] + grp_off) | DTAG_IN_DDR_MASK;
			return p2l_dtag;
		}
	}

	//search fail, eliminate useless grp for new grp
	for (i = 0; i < RECON_P2L_GRP_CNT; i++) {
		if (recon_res.p2l_grp_id[i] == ~0) {
			slot = i;
			break;
		} else if (recon_res.p2l_grp_id[i] < min_grp_id) {
			slot = i;
			min_grp_id = recon_res.p2l_grp_id[i];
		}
	}

	sys_assert(slot < RECON_P2L_GRP_CNT);

	if (is_spb_slc(spb))
		ttl_p2l_cnt = p2l_ext_para.slc_p2l_cnt;
	else
		ttl_p2l_cnt = p2l_ext_para.xlc_p2l_cnt;

	//build p2l load req
	p2l_req->spb_id = spb;
	p2l_req->grp_id = grp_id;
	p2l_req->p2l_id = p2l_ext_para.p2l_per_grp * grp_id;
	p2l_req->p2l_cnt = min(p2l_ext_para.p2l_per_grp, ttl_p2l_cnt - p2l_req->p2l_id);

	for (i = 0; i < p2l_req->p2l_cnt; i++)
		p2l_req->dtags[i].dtag = (recon_res.p2l_grp_dtag[slot] + i) | DTAG_IN_DDR_MASK;

	ftl_core_p2l_load(p2l_req);
	recon_res.p2l_grp_id[slot] = grp_id;

	p2l_dtag.dtag = (recon_res.p2l_grp_dtag[slot] + grp_off) | DTAG_IN_DDR_MASK;
	return p2l_dtag;
}

init_code void recon_spb_with_p2l(spb_id_t spb)
{
	u32 i;
	u32 page;
	u32 ttl_page;
	dtag_t p2l_dtag;
	u32 last_p2l_id = ~0;
	lda_t *p2l_base = NULL;
	u32 mp_du_cnt = get_mp() << DU_CNT_SHIFT;
	u32 intlv_du_cnt = nal_get_interleave() << DU_CNT_SHIFT;

	if (is_spb_slc(spb))
		ttl_page = get_slc_page_per_block();
	else
		ttl_page = get_page_per_block();

	//reset p2l grp id before rebuild
	for (i = 0; i < RECON_P2L_GRP_CNT; i++)
		recon_res.p2l_grp_id[i] = ~0;

	//now we only support v-mode
	page = 0;
	while (page < ttl_page) {
		u32 j, k;
		u32 page_cnt;
		u32 cur_page;

		page_cnt = get_cwl_page_cnt(spb, page);
		for (i = 0; i < get_lun_per_spb(); i++) {
			for (j = 0; j < page_cnt; j++) {
				cur_page = page + j;
				u32 du_off = cur_page * intlv_du_cnt + i * mp_du_cnt;
				u32 p2l_id = du_off >> DU_CNT_PER_P2L_SHIFT;
				u32 p2l_off = du_off & DU_CNT_PER_P2L_MASK;

				if (last_p2l_id != p2l_id) {
					last_p2l_id = p2l_id;
					p2l_dtag = recon_get_p2l_dtag(spb, p2l_id);
					p2l_base = dtag2mem(p2l_dtag);
				}

				sys_assert(p2l_base);

				pda_t pda = nal_make_pda(spb, du_off);
				for (k = 0; k < mp_du_cnt; k++) {
					lda_t lda = p2l_base[p2l_off + k];
					if (lda < TRIM_LDA) {
						l2p_mgr_set_seg_dirty(lda);
						u32 range = (lda >> RDISK_RANGE_SHIFT);
						if (test_and_clear_bit(range, shr_range_ptr)) {
							l2p_updt_trim(range << RDISK_RANGE_SHIFT, 0, true, RDISK_RANGE_SIZE, 2, 0);
							(*shr_range_wnum)--;
						}
						l2p_updt_pda(lda, pda + k, INV_PDA, false, false, 2, 0);
					} else if (lda == TRIM_LDA) {
						dtag_t dtag = { .dtag = recon_trim_info(pda + k)};
						recon_trim_handle((ftl_trim_t *)dtag2mem(dtag));
					}
				}
			}
		}

		page += page_cnt;
	}
}

init_code void recon_trim_handle(ftl_trim_t *ftl_trim)
{
	u32 num = ftl_trim->num;

	u32 i;
	for (i = 0; i < num; i ++)
	{
		u32 slda = ftl_trim->range[i].slda;
		u32 len = ftl_trim->range[i].len;
		sys_assert(len);

		int srange = (slda >> RDISK_RANGE_SHIFT);
		int erange = ((slda + len - 1) >> RDISK_RANGE_SHIFT);
		if (erange - srange > 1) {
			len = ((srange + 1) << RDISK_RANGE_SHIFT) - slda;
			l2p_updt_trim(slda, 0, true, len, 2, 0);
			slda = (erange << RDISK_RANGE_SHIFT);
			len = (slda + len) - slda;
			l2p_updt_trim(slda, 0, true, len, 2, 0);

			u32 j;
			for (j = (srange + 1); j < erange; j++) {
				if (!test_and_set_bit(j, shr_range_ptr))
					(*shr_range_wnum)++;
			}
		} else {
			if (len > RDISK_RANGE_SIZE) {
				l2p_updt_trim(slda, 0, true, RDISK_RANGE_SIZE, 2, 0);
				len -= RDISK_RANGE_SIZE;
				slda += RDISK_RANGE_SIZE;
			}
			l2p_updt_trim(slda, 0, true, len, 2, 0);
		}
	}
}

init_code u32 recon_trim_info(u32 pda)
{
	pda_t *pda_list = recon_res.pda;
	bm_pl_t *bm_pl = recon_res.bm_pl;
	struct info_param_t *info = recon_res.info;
	io_meta_t *meta = recon_res.meta;
	u16 spb = pda2blk(pda);
	u32 spb_type;

	if (is_spb_slc(spb))
		spb_type = NAL_PB_TYPE_SLC;
	else
		spb_type = NAL_PB_TYPE_XLC;

	pda_list[0] = pda;
	info[0].status = 0;
	info[0].pb_type = spb_type;

	ncl_read_dus(pda_list, 1, bm_pl, info, spb_type, false, true);
	sys_assert(meta[0].lda);
	return bm_pl->pl.dtag;
}

init_code bool recon_spb_with_meta(spb_id_t spb, u32 ptr, scan_handler_t handler, bool host)
{
	u32 i;
	u32 page;
	u32 ttl_page;
	u32 page_cnt;
	u32 cwl_err_du_cnt;
	u32 cwl_era_du_cnt;
	u32 intlv_len = recon_res.intlv_len;

	if (is_spb_slc(spb))
		ttl_page = get_slc_page_per_block();
	else
		ttl_page = get_page_per_block();

	page = ptr / nal_get_interleave();
	ftl_apl_trace(LOG_INFO, 0xb43c, "scan spb %d from page %d", spb, page);

	while (page < ttl_page) {
		cwl_err_du_cnt = 0;
		cwl_era_du_cnt = 0;
		page_cnt = get_cwl_page_cnt(spb, page);

		cpu_msg_isr();

		//scan cwl
		for (i = 0; i < page_cnt; i++) {
			recon_scan_intlv(spb, page + i, i, host);
			cwl_err_du_cnt += recon_res.err_du_cnt;
			cwl_era_du_cnt += recon_res.era_du_cnt;
		}

		//empty cwl
		if (cwl_era_du_cnt == (intlv_len * page_cnt)) {
			ftl_apl_trace(LOG_INFO, 0xd4c7, "spb %d cwl %d empty", spb, page);
			break;
		}

		ftl_apl_trace(LOG_DEBUG, 0xa4d8, " page %d err %d era %d",
				page, recon_res.err_du_cnt, recon_res.era_du_cnt);

		if (cwl_err_du_cnt < (intlv_len * page_cnt)) {
			pda_t pda = nal_make_pda(spb, page * intlv_len);
			handler(recon_res.lda, pda, page_cnt);
		}

		page += page_cnt;
	}

	return (page == 0) ? true : false;
}

init_code spb_fence_t *get_recon_fence(ftl_fence_t *ffence, u32 type)
{
	u32 i;
	spb_fence_t *spb_fence = ffence->spb_fence[type];
	spb_fence_t *recon_fence = &recon_res.recon_fence;

	recon_fence->sn = 0;
	recon_fence->ptr = 0;
	recon_fence->flags.all = 0;
	recon_fence->spb_id = INV_SPB_ID;

	for (i = 0; i < MAX_ASPB_IN_PS; i++) {
		//invalid fence
		if (spb_fence[i].flags.b.valid == false)
			continue;

		//first valid fence
		if (recon_fence->flags.b.valid == false) {
			*recon_fence = spb_fence[i];
			continue;
		}

		if (spb_fence[i].ptr && recon_fence->ptr) {
			//two programed spb, get the latest
			if (spb_fence[i].sn > recon_fence->sn) {
				sys_assert(recon_fence->flags.b.closed);
				*recon_fence = spb_fence[i];
			}
		} else if ((spb_fence[i].ptr == 0) && (recon_fence->ptr == 0)) {
			//two empty spb, get the least
			if (recon_fence->sn > spb_fence[i].sn)
				*recon_fence = spb_fence[i];
		} else if (spb_fence[i].ptr && (recon_fence->ptr == 0)) {
			sys_assert(spb_fence[i].sn < recon_fence->sn);
			*recon_fence = spb_fence[i];
		} else if (spb_fence[i].ptr == 0 && recon_fence->ptr) {
			sys_assert(recon_fence->sn < spb_fence[i].sn);
		}
	}

	return recon_fence;
}

init_code void recon_restore_fence(spb_id_t *fence_spb, u32 nsid)
{
	u32 i, j;
	bool restore;
	ftl_fence_t ffence;
	spb_fence_t *spb_fence;

	restore = false;
	ffence.nsid = nsid;
	ffence.flags.b.clean = false;

	for (i = FTL_CORE_NRM; i < FTL_CORE_MAX; i++) {
		for (j = 0; j < MAX_ASPB_IN_PS; j++) {
			spb_fence = &ffence.spb_fence[i][j];
			spb_fence->spb_id = INV_SPB_ID;
		}

		//no valid fence spb get
		if (fence_spb[i] == INV_SPB_ID)
			continue;

		restore = true;
		spb_fence = &ffence.spb_fence[i][0];
		spb_fence->spb_id = fence_spb[i];
		spb_fence->sn = spb_get_sn(fence_spb[i]);

		spb_fence->flags.all = 0;
		spb_fence->flags.b.valid = true;
		spb_fence->flags.b.closed = true;
		spb_fence->flags.b.slc = is_spb_slc(fence_spb[i]);

		spb_fence->ptr = nal_get_interleave();
		if (spb_fence->flags.b.slc)
			spb_fence->ptr *= get_slc_page_per_block();
		else
			spb_fence->ptr *= get_page_per_block();
	}

	if (restore)
		ftl_core_restore_fence(&ffence);
}

init_code void recon_res_get(void)
{
	u32 i;
	u32 mem_size;
	void *mem_start;
	u32 intlv_len = recon_res.intlv_len;

	mem_size = (sizeof(bm_pl_t) + sizeof(pda_t) + sizeof(struct info_param_t)) * intlv_len;
	mem_start = share_malloc(mem_size);
	sys_assert(mem_start);

	recon_res.bm_pl = mem_start;
	recon_res.info = ptr_inc(recon_res.bm_pl, sizeof(bm_pl_t) * intlv_len);
	recon_res.pda = ptr_inc(recon_res.info, sizeof(struct info_param_t) * intlv_len);

	mem_size = (sizeof(lda_t) + sizeof(sn_t)) * intlv_len * XLC;
	mem_start = sys_malloc(FAST_DATA, mem_size);
	sys_assert(mem_start);
	recon_res.lda = mem_start;
	recon_res.sn = ptr_inc(recon_res.lda, sizeof(lda_t) * intlv_len * XLC);

	for (i = 0; i < intlv_len; i++) {
		recon_res.bm_pl[i].pl.btag = 0;
		recon_res.bm_pl[i].pl.du_ofst = i;
		//reuse gc ddr dtag in recon stage
		recon_res.bm_pl[i].pl.dtag = (shr_gc_ddtag_start + i) | DTAG_IN_DDR_MASK;
		recon_res.bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_DTAG;
	}
}

init_code void recon_res_put(void)
{
	share_free(recon_res.bm_pl);
	sys_free(FAST_DATA, recon_res.lda);
}

init_code void recon_pre_init(void)
{
	u32 i;
	u32 intlv_du_cnt = nal_get_interleave() << DU_CNT_SHIFT;

	recon_res.intlv_len = intlv_du_cnt;
	recon_res.read_len = intlv_du_cnt;
	//init raid lun
	raid_lun = RAID_SUPPORT ? (intlv_du_cnt - get_mp() * DU_CNT_PER_PAGE) : (~0);
	//reuse gc ddr dtag meta
	sys_assert(intlv_du_cnt <= FCORE_GC_DTAG_CNT);
	recon_res.meta = &((io_meta_t*)shr_ddtag_meta)[shr_gc_ddtag_start];

	//allocate p2l dtag
	u32 dtag_start = ddr_dtag_register(p2l_ext_para.p2l_per_grp * RECON_P2L_GRP_CNT);
	for (i = 0; i < RECON_P2L_GRP_CNT; i++)
		recon_res.p2l_grp_dtag[i] = dtag_start + p2l_ext_para.p2l_per_grp * i;
}
#endif
