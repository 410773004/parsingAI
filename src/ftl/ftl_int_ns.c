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
/*! \file ftl_int_ns.c
 * @brief define ftl internal namespace, each host namespace share one internal namespace
 *
 * \addtogroup ftl
 * \defgroup int_ns
 * \ingroup ftl
 * @{
 *
 */
#include "ftlprecomp.h"
#include "ftl_ns.h"
#include "ftl_l2p.h"
#include "system_log.h"
#include "mpc.h"
#include "spb_mgr.h"
#include "ftl_flash_geo.h"
#include "l2p_mgr.h"
#include "recon.h"
#include "erase.h"

/*! \cond PRIVATE */
#define __FILEID__ fins
#include "trace.h"
/*! \endcond PRIVATE */

/*! @brief internal namespace spb valid count */
typedef struct _int_spb_vcnt_t {
	spb_id_t spb_id;		///< spb id
	u16 rsvd;
	u32 vcnt;			///< valid count
} int_spb_vcnt_t;

/*! @brief internal ftl namespace definition */
typedef struct _ftl_ins_t {
	ftl_ns_t *ns;			///< ftl namespace object

	pda_t *lut;			///< l2p of internal namespace
	int_spb_vcnt_t *int_spb_vcnt;	///< internal spb valid count table
	sys_log_desc_t lut_sec;		///< system log descriptor of lut of internal namespace
	u32 l2p_misc_pos;		///< lda of misc data to be stored
} ftl_ins_t;

fast_data_zi static ftl_ins_t _ftl_ins;	///< ftl internal namespace object
share_data_zi spb_queue_t int_spb_queue[2];	///< spb queue of internal namespace
share_data_zi volatile pda_t *shr_ins_lut;		///< global l2p pointer of internal namespace

init_code void ftl_int_ns_init(ftl_ns_t *ns)
{
	u32 cap;
#if(SPB_BLKLIST == mDISABLE)
	u32 min_spb_cnt;
	u32 max_spb_cnt;
#endif
	u32 real;
	u32 sz;

	_ftl_ins.ns = ns;

	cap = ftl_ns_get_capacity(0);
	real = occupied_by(cap, ENT_IN_L2P_SEG);
	//ftl_apl_trace(LOG_ERR, 0, "[FTL]real seg cnt %d", real);
	_ftl_ins.l2p_misc_pos = real;
	real += ftl_l2p_misc_cnt();	// + misc
	//real += 32;			// + reserved, at least 32
	cap = round_up_by_2_power(real, 1024);

	//ftl_apl_trace(LOG_ALW, 0, "ins cap: %d(rsvd %d)", cap, cap - real);

	ns->spb_queue[0] = &int_spb_queue[0];
	#if (PBT_OP ==mENABLE)
	ns->spb_queue[FTL_CORE_PBT] = &int_spb_queue[1];
	#endif
	ns->capacity = cap;
	ns->attr.b.native = 1;
	ns->attr.b.tbl = 1;
	ns->flags.all = 0;
	ns->flags.b.QBT = 1;

    //FTL_CACL_QBTPBT_CNT();
	ftl_ns_link_pool(FTL_NS_ID_INTERNAL, SPB_POOL_QBT_FREE, QBT_BLK_CNT*0, QBT_BLK_CNT*0);
	ftl_ns_link_pool(FTL_NS_ID_INTERNAL, SPB_POOL_QBT_ALLOC, 0, 0);

	// each entry in internal ns is segment size
	sz = cap * sizeof(pda_t) + 0 * sizeof(int_spb_vcnt_t);
	sys_log_desc_register(&_ftl_ins.lut_sec, 1, sz, NULL);
	sys_log_desc_register(&_ftl_ins.ns->ns_log_desc, 1, sizeof(ftl_ns_desc_t), ftl_ns_desc_init);
}
/*
init_code void ftl_int_ns_vc_restore(void)
{
	u32 i;
	u32 found = 0;
	u32 *vc;
	dtag_t dtag;
	u32 dtag_cnt;

	dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **)&vc);

	//ftl_apl_trace(LOG_INFO, 0, "misc crc %x", ftl_l2p_misc_crc(&dtag));

	for (i = 0; i < _ftl_ins.ns->pools[SPB_POOL_SLC].max_spb_cnt; i++) {
		int_spb_vcnt_t *int_spb_vcnt = &_ftl_ins.int_spb_vcnt[i];

		if (int_spb_vcnt->spb_id == INV_SPB_ID)
			break;

		u8 nsid = spb_get_nsid(int_spb_vcnt->spb_id);
		sys_assert(nsid == FTL_NS_ID_INTERNAL);

		vc[int_spb_vcnt->spb_id] = int_spb_vcnt->vcnt;
		found++;
	}

	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, true);
}
*/
/*
init_code void ftl_int_ns_vc_updt(pda_t new_pda, pda_t old_pda)
{
	u32 i;
	spb_id_t old_spb;
	spb_id_t new_spb;
	bool new_spb_found;
	int_spb_vcnt_t *int_spb_vcnt;

	new_spb_found = false;
	new_spb = nal_pda_get_block_id(new_pda);

	for (i = 0; i < _ftl_ins.ns->pools[SPB_POOL_SLC].max_spb_cnt; i++) {
		int_spb_vcnt = &_ftl_ins.int_spb_vcnt[i];
		if (int_spb_vcnt->spb_id == INV_SPB_ID)
			break;

		u8 nsid = spb_get_nsid(int_spb_vcnt->spb_id);
		sys_assert(nsid == FTL_NS_ID_INTERNAL);

		if (int_spb_vcnt->spb_id == new_spb) {
			new_spb_found = true;
			int_spb_vcnt->vcnt++;
		}

		if (is_normal_pda(old_pda)) {
			old_spb = nal_pda_get_block_id(old_pda);
			if (int_spb_vcnt->spb_id == old_spb)
				int_spb_vcnt->vcnt--;
		}
	}

	if (new_spb_found == false) {
		int_spb_vcnt = &_ftl_ins.int_spb_vcnt[i];
		sys_assert(int_spb_vcnt->vcnt == 0);
		sys_assert(int_spb_vcnt->spb_id == INV_SPB_ID);

		int_spb_vcnt->vcnt++;
		int_spb_vcnt->spb_id = new_spb;
	}
}
*/
/*
init_code void ftl_ins_scan_handler(lda_t *lda_list, pda_t pda, u32 page_cnt)
{
	u32 i, j;
	u32 seg;
	bool drop_seg;
	pda_t old_pda;
	pda_t new_pda;
	u32 seg_du_cnt = seg_size >> DTAG_SHF;
	u32 intlv_du_cnt = nal_get_interleave() << DU_CNT_SHIFT;

	sys_assert(page_cnt == 1);

	for (i = 0; i < intlv_du_cnt; i += seg_du_cnt) {
		drop_seg = false;
		for (j = 0; j < seg_du_cnt; j++) {
			if (lda_list[i + j] == ERR_LDA) {
				drop_seg = true;
				break;
			}
		}

		if (drop_seg)
			continue;

		//seg = lda_list[i] >> shr_l2pp_per_seg_shf;
		seg = lda_list[i]/NR_L2PP_IN_SEG;
		new_pda = pda + i;
		old_pda = shr_ins_lut[seg];
		shr_ins_lut[seg] = new_pda;

		ftl_int_ns_vc_updt(new_pda, old_pda);
	}
}
*/
/*
init_code void ftl_int_ns_dirty_boot(ftl_ns_t *ins)
{
	int i, j;
	u32 sn;
	spb_id_t dirty_spb;
	//ftl_ns_pool_t *ns_pool;
	spb_fence_t *recon_fence;
	int_spb_vcnt_t *int_spb_vcnt;
	ftl_ns_desc_t *ftl_ns_desc = ins->ftl_ns_desc;
	ftl_fence_t *ffence = &ftl_ns_desc->ffence;
	spb_id_t fence_spb[FTL_CORE_MAX] = {INV_SPB_ID, INV_SPB_ID};

	recon_res_get();

	//reset non-used slot of int spb vc
	for (i = 0; i < ins->pools[SPB_POOL_SLC].max_spb_cnt; i++) {
		int_spb_vcnt = &_ftl_ins.int_spb_vcnt[i];
		//find end mark first
		if (int_spb_vcnt->spb_id != INV_SPB_ID)
			continue;

		for (j = i; j < ins->pools[SPB_POOL_SLC].max_spb_cnt; j++) {
			int_spb_vcnt = &_ftl_ins.int_spb_vcnt[j];
			int_spb_vcnt->vcnt = 0;
			int_spb_vcnt->spb_id = INV_SPB_ID;
		}
	}

	recon_fence = get_recon_fence(ffence, FTL_CORE_NRM);
	ftl_apl_trace(LOG_INFO, 0, "ins recon fence: spb %d ptr %d sn %d flag %x",
		recon_fence->spb_id, recon_fence->ptr, recon_fence->sn, recon_fence->flags.all);

	sn = recon_fence->sn;

#ifdef While_break
	u64 start = get_tsc_64();
#endif	

	while (true) {
		dirty_spb = spb_mgr_pop_dirty_spb(INT_NS_ID, FTL_CORE_NRM, sn);
		if (dirty_spb == INV_SPB_ID)
			break;

		u32 spb_sn = spb_get_sn(dirty_spb);
		u32 spb_flag = spb_get_flag(dirty_spb);
		sn = spb_sn + 1;//set sn for next round dirty spb search
		ftl_ns_desc->spb_sn = max(ftl_ns_desc->spb_sn, spb_sn + 1);
		ftl_apl_trace(LOG_INFO, 0, "get ins dirty spb %d sn %d", dirty_spb, spb_sn);

		if ((recon_fence->sn == spb_sn) && (recon_fence->flags.b.closed)) {
			sys_assert(spb_flag & SPB_DESC_F_CLOSED);
			fence_spb[FTL_CORE_NRM] = dirty_spb;
			ftl_apl_trace(LOG_INFO, 0, "fence spb %d closed, skip recon", dirty_spb);
			continue;
		}

		u32 scan_ptr = 0;
		if (recon_fence->sn == spb_sn && dirty_spb == recon_fence->spb_id)
			scan_ptr = recon_fence->ptr;

		bool spb_empty = recon_spb_with_meta(dirty_spb, scan_ptr, ftl_ins_scan_handler, false);
		if (spb_empty) {
			sys_assert(!(spb_flag & SPB_DESC_F_CLOSED));
			ftl_ns_add_ready_spb(INT_NS_ID, FTL_CORE_NRM, dirty_spb);
		} else {
			fence_spb[FTL_CORE_NRM] = dirty_spb;
			//set spb closed to make it can be gc candidate
			spb_set_flag(dirty_spb, SPB_DESC_F_CLOSED);
			//set spb open then do open spb gc
			ftl_core_set_spb_open(dirty_spb);
		}

#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif		
	}

	// chk if flush all l2p tbl
	// Curry
	ns_pool = &_ftl_ins.ns->pools[SPB_POOL_SLC];
	if (ns_pool->allocated >= ns_pool->min_spb_cnt + 1)
		ftl_l2p_tbl_flush_all_notify();

	recon_res_put();
	recon_restore_fence(fence_spb, INT_NS_ID);
}
*/
init_code ftl_err_t ftl_int_ns_start(void)
{
	u32 i;

	_ftl_ins.lut = sys_log_desc_get_ptr(&_ftl_ins.lut_sec);
	_ftl_ins.int_spb_vcnt = (int_spb_vcnt_t *) ptr_inc(_ftl_ins.lut, _ftl_ins.ns->capacity * sizeof(pda_t));
	shr_ins_lut = _ftl_ins.lut;

	_ftl_ins.ns->ftl_ns_desc = (ftl_ns_desc_t *) sys_log_desc_get_ptr(&_ftl_ins.ns->ns_log_desc);
	if (ftl_virgin) {
		for (i = 0; i < SPB_POOL_MAX; i++)
			_ftl_ins.ns->ftl_ns_desc->quota[i] = _ftl_ins.ns->pools[i].quota;
	}

	for (i = 0; i < SPB_POOL_MAX; i++) {
		_ftl_ins.ns->pools[i].quota = _ftl_ins.ns->ftl_ns_desc->quota[i];
		//ftl_apl_trace(LOG_ALW, 0, " pool[%d] %d/%d", i, _ftl_ins.ns->pools[i].allocated,
				//_ftl_ins.ns->pools[i].quota);
	}

	_ftl_ins.ns->ftl_ns_desc->flags.b.clean = 1;
	ftl_apl_trace(LOG_ALW, 0xcb39, "int start: v%d c%d",
			_ftl_ins.ns->ftl_ns_desc->flags.b.virgin, _ftl_ins.ns->ftl_ns_desc->flags.b.clean);

	if (1)//(_ftl_ins.ns->ftl_ns_desc->flags.b.virgin) 
	{
		_ftl_ins.ns->ftl_ns_desc->flags.b.virgin = 0;
		return FTL_ERR_VIRGIN;
	} 
	else if (_ftl_ins.ns->ftl_ns_desc->flags.b.clean) {
		ftl_ns_desc_t *ftl_ns_desc = _ftl_ins.ns->ftl_ns_desc;
		ftl_fence_t fence;
		//ftl_ns_pool_t *ns_pool;
		u32 i;

		fence = ftl_ns_desc->ffence;
		ftl_core_restore_fence(&fence);
		for (i = 0; i < SPB_QUEUE_SZ; i++) {
			spb_ent_t spb_ent;
			bool ret;

			if (ftl_ns_desc->spb_queue[0][i] == INV_SPB_ID)
				break;

			spb_ent.all = 0;
			spb_ent.b.spb_id = ftl_ns_desc->spb_queue[0][i];
			spb_ent.b.slc = is_spb_slc(spb_ent.b.spb_id);
			spb_ent.b.sn = spb_get_sn(spb_ent.b.spb_id);
			CBF_INS(_ftl_ins.ns->spb_queue[0], ret, spb_ent.all);
			sys_assert(ret == true);
		}
        /* Curry
		ns_pool = &_ftl_ins.ns->pools[SPB_POOL_SLC];
		if (ns_pool->allocated >= ns_pool->min_spb_cnt + 1) {
			// flush everything instead of GC
			ftl_l2p_tbl_flush_all_notify();
		}
		*/
		return FTL_ERR_CLEAN;
	} 
	else 
	{
#if 1
		//ftl_int_ns_dirty_boot(_ftl_ins.ns);
		return FTL_ERR_RECON_DONE;
#else
		// always return FAIL to force reconstruct host namespace
		ftl_int_ns_format(_ftl_ins.ns);
		return FTL_ERR_RECON_FAILED;
#endif
	}
}

fast_code u32 ftl_int_ns_get_misc_pos(void)
{
	return _ftl_ins.l2p_misc_pos;
}

fast_code pda_t ftl_int_ns_lookup(u32 segid)
{
	return _ftl_ins.lut[segid];
}

fast_code void ftl_int_ns_update_desc(void)
{
	ftl_ns_t *this_ns = _ftl_ins.ns;
	ftl_ns_desc_t *desc = this_ns->ftl_ns_desc;
	ftl_fence_t ffence;
	u32 rptr;
	spb_ent_t ent;
	u32 j;

	ffence.nsid = FTL_NS_ID_INTERNAL;
	ftl_core_update_fence(&ffence);
	desc->ffence = ffence;

	if (CBF_EMPTY(this_ns->spb_queue[0])) {
		desc->spb_queue[0][0] = INV_SPB_ID;
	} else {
		j = 0;
		rptr = this_ns->spb_queue[0]->rptr;
		while (rptr != this_ns->spb_queue[0]->wptr) {
			ent.all = this_ns->spb_queue[0]->buf[rptr];
			desc->spb_queue[0][j] = ent.b.spb_id;
			ftl_apl_trace(LOG_ERR, 0x4bad, "ins spb queue [%d] %d\n", j, ent.b.spb_id);
			j++;
			rptr = next_cbf_idx_2n(rptr, SPB_QUEUE_SZ);
		}
	}

#if 0
	for (j = 0; j < MAX_ASPB_IN_PS; j++) {
		spb_fence_t *fence = &desc->ffence.spb_fence[0][j];
		ftl_apl_trace(LOG_ERR, 0xdbc1, "ins t aspb[%d] spb %d sn %d fence 0x%x flag 0x%x\n",
			j, fence->spb_id, fence->sn, fence->ptr, fence->flags.all);
	}
#endif
}

fast_code void ftl_int_ns_flush_l2p(void)
{
	u32 i;
	u32 found = 0;
	u32 *vc;
	dtag_t dtag;
	u32 dtag_cnt;

	dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);

	for (i = 0; i < get_total_spb_cnt(); i++) {
		u8 nsid = spb_get_nsid(i);

		if (nsid == FTL_NS_ID_INTERNAL) {
			if (vc[i] == 0) {
				u16 flags = spb_get_flag(i);

				if (flags & SPB_DESC_F_CLOSED) {
					//spb_release(i);
					continue;
				}
			}
			_ftl_ins.int_spb_vcnt[found].spb_id = i;
			_ftl_ins.int_spb_vcnt[found].vcnt = vc[i];
			found++;
#if 0
			if (found == _ftl_ins.ns->pools[SPB_POOL_SLC].allocated) {
				break;
			}
#else
			//sys_assert(found <= _ftl_ins.ns->pools[SPB_POOL_SLC].allocated);
#endif
		}
	}

	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);

	if (found != 0)//_ftl_ins.ns->pools[SPB_POOL_SLC].max_spb_cnt)
		_ftl_ins.int_spb_vcnt[found].spb_id = INV_SPB_ID;	// end of SPB

	//sys_log_desc_flush(&_ftl_ins.lut_sec, ~0, NULL);
}

fast_code void ftl_int_ns_format(ftl_ns_t *ns)
{
	u32 i;
	u32 *vc;
	u32 dtag_cnt;
	dtag_t dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);

	memset(_ftl_ins.lut, 0xff, sizeof(pda_t) * ns->capacity);

	for (i = 0; i < get_total_spb_cnt(); i++) {
		//u8 nsid = spb_get_nsid(i);

		//if (nsid != FTL_NS_ID_INTERNAL)
			//continue;
		vc[i] = 0;
		//spb_set_flag(i, SPB_DESC_F_CLOSED);
		//spb_release(i);
	}

	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, true);
	ftl_ns_link_pool(FTL_NS_ID_INTERNAL, SPB_POOL_QBT_FREE, QBT_BLK_CNT*0, QBT_BLK_CNT*0);
	ftl_ns_link_pool(FTL_NS_ID_INTERNAL, SPB_POOL_QBT_ALLOC, 0, 0);
	sys_assert(ns->pools[SPB_POOL_QBT_ALLOC].allocated == 0);

}

/*! @} */
