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
/*! \file ftl_host_ns.c
 * @brief define ftl host namespace
 *
 * \addtogroup ftl
 * \defgroup host_ns
 * \ingroup ftl
 * @{
 *
 */
#include "ftlprecomp.h"
#include "ftl_ns.h"
#include "ftl_l2p.h"
#include "mpc.h"
#include "system_log.h"
#include "spb_mgr.h"
#include "ftl_flash_geo.h"
#include "recon.h"
#include "frb_log.h"
#include "ncl_exports.h"
#include "ftl_spor.h"
#include "fc_export.h"
#include "srb.h"
/*! \cond PRIVATE */
#define __FILEID__ fhns
#include "trace.h"
/*! \endcond PRIVATE */

/*! @brief definition of ftl host namespace */
typedef struct _ftl_hns_t {
	ftl_ns_t *ns;		///< parent ftl namespace object

	l2p_ele_t l2p_ele;	///< l2p element, describe l2p of this namespace
} ftl_hns_t;

fast_data_zi ftl_hns_t _ftl_hns[FTL_NS_ID_END];	///< ftl host namespace objects
share_data_zi volatile u8 *shr_range_ptr;		///< range bitmap pointer
share_data_zi volatile u32 shr_range_size;		///< range bitmap size
share_data_zi volatile u8 *shr_range_wnum;
share_data volatile enum du_fmt_t host_du_fmt;
share_data volatile bool _fg_warm_boot;
share_data_zi u32 shr_l2p_seg_bmp_sz;
extern u16 host_sec_size;//joe add change sec size 20200817
extern u8 host_sec_bitz;//joe add change sec size  20200817
extern u8 tcg_reserved_spb_cnt;

init_code void ftl_host_ns_init(ftl_ns_t *ns, u32 nsid)
{
	sys_assert(nsid <= FTL_NS_ID_END);

	_ftl_hns[nsid].ns = ns;
	ftl_l2p_alloc(ns->capacity, nsid, &_ftl_hns[nsid].l2p_ele);
}

init_code void ftl_host_ns_clean_boot(u32 nsid)
{
	u32 i, j;
	ftl_fence_t fence;
	ftl_hns_t *hns = &_ftl_hns[nsid];
	ftl_ns_desc_t *ftl_ns_desc = hns->ns->ftl_ns_desc;

	fence = ftl_ns_desc->ffence;
	fence.flags.all = 0;
	fence.flags.b.clean = 1;
	ftl_core_restore_fence(&fence);
	for (i = 0; i < FTL_CORE_MAX; i++) {
		for (j = 0; j < SPB_QUEUE_SZ; j++) {
			spb_ent_t spb_ent;
			bool ret;

			if (ftl_ns_desc->spb_queue[i][j] == INV_SPB_ID)
				break;

			spb_ent.all = 0;
			spb_ent.b.spb_id = ftl_ns_desc->spb_queue[i][j];
			spb_ent.b.slc = is_spb_slc(spb_ent.b.spb_id);
			spb_ent.b.sn = spb_get_sn(spb_ent.b.spb_id);
			CBF_INS(hns->ns->spb_queue[i], ret, spb_ent.all);
			sys_assert(ret == true);
		}
	}
/*
#if (SPOR_FLOW == mENABLE)
    ftl_misc_reload(L2P_LAST_PDA);
#else
	ftl_misc_reload();
#endif
*/
	ftl_l2p_bg_load(hns->l2p_ele.seg_off, hns->l2p_ele.seg_end);

#if (FTL_L2P_SEG_BMP == mENABLE)
	memset(shr_l2p_seg_dirty_bmp, 0, shr_l2p_seg_bmp_sz);
#endif
}
/*
init_code void ftl_hns_scan_handler(lda_t *lda_list, pda_t start_pda, u32 page_cnt)
{
	lda_t lda;
	u32 index;
	u32 i, j, k;
	pda_t pda;
	u32 mp_du_cnt = get_mp() << DU_CNT_SHIFT;
	u32 intlv_du_cnt = nal_get_interleave() << DU_CNT_SHIFT;

	for (i = 0; i < get_lun_per_spb(); i++) {
		for (j = 0; j < page_cnt; j++) {
			index = j * intlv_du_cnt + i * mp_du_cnt;
			pda = start_pda + index;
			for (k = 0; k < mp_du_cnt; k++) {
				if (is_raid_lun(i * mp_du_cnt + k))
					continue;

				lda = lda_list[index + k];
				if (lda == TRIM_LDA) {
					dtag_t dtag = { .dtag = recon_trim_info(pda + k)};
					recon_trim_handle((ftl_trim_t *)dtag2mem(dtag));
				} else if ((lda != ERR_LDA) && (lda != INV_LDA)) {
					l2p_mgr_set_seg_dirty(lda);
					u32 range = (lda >> RDISK_RANGE_SHIFT);
					if (test_and_clear_bit(range, shr_range_ptr)) {
						l2p_updt_trim(range << RDISK_RANGE_SHIFT, 0, true, RDISK_RANGE_SIZE, 2, 0);
						(*shr_range_wnum)--;
					}
					l2p_updt_pda(lda, pda + k, INV_PDA, false, false, 2, 0);
				}
			}
		}
	}
}
*/
extern epm_info_t* shr_epm_info;
init_code void ftl_host_ns_dirty_boot(u32 nsid)
{
	ftl_hns_t *hns = &_ftl_hns[nsid];

	hns->ns->flags.b.reconing = true;

	u32 prev_max_blk_sn = gFtlMgr.SerialNumber;

#if(WARMBOOT_FTL_HANDLE == mENABLE)
#if(WA_FW_UPDATE == DISABLE)
	if(_fg_warm_boot == true)
	{
		ftl_warmboot_reload(&hns->l2p_ele);
	}
	else
#endif
#endif
	{


#if(SPOR_FLOW == mENABLE)
    // ===== Load Blk List & other table =====
    u16 spb_id = INV_U16;
    u8  load_l2p_only = mTRUE;
	u8  fail_cnt = 0;
	u8  blist_mode = INV_U8;
	u8  build_l2p_mode = INV_U8;
	reload:
    // initial l2p/blist error flag
    ftl_set_blist_data_error_flag(mFALSE);
    ftl_set_l2p_data_error_flag(mFALSE);

    extern u8 spor_read_pbt_cnt;
	spor_read_pbt_cnt = 0;

	#if (SPOR_QBT_MAX_ONLY == mENABLE)
    if(!ftl_is_qbt_blk_max())
    {
        goto LOAD_END;
    }
	#endif

Load_Blist_Again:
    // ===== Load Blk List & VC table =====
    build_l2p_mode = ftl_spor_build_l2p_mode();
    is_skip_spor_build();//for slc cache buffer and not plp gc
    blist_mode = ftl_spor_load_blist_mode(&spb_id, fail_cnt);
    switch(blist_mode)
    {
        case FTL_QBT_LOAD_BLIST:
            load_l2p_only = mFALSE;
            break;

        case FTL_USR_LOAD_BLIST:
            if(ftl_read_misc_data_from_host(spb_id))
            {
                FTL_CopyFtlBlkDataFromBuffer(0);
            }
            else
            {
				fail_cnt += 1;
				ftl_apl_trace(LOG_ALW, 0x7b68, "[FTL]Host Blist first read fail");
				goto Load_Blist_Again;
            }
			break;

		case FTL_LOAD_BLKIST_FAIL:
			ftl_set_blist_data_error_flag(mTRUE);
			ftl_apl_trace(LOG_ALW, 0x82f9, "[FTL]Blist read fail into USER build");
			break;
        default:
            panic("impossible load blist mode!");
    }

    // ===== Read L2P table =====
    switch(build_l2p_mode)
    {
        case FTL_QBT_BUILD_L2P:
            ftl_qbt_pbt_reload(&hns->l2p_ele, SPB_POOL_QBT_ALLOC, load_l2p_only);
			//ftl_set_blist_data_error_flag(mTRUE);
    		//ftl_set_l2p_data_error_flag(mTRUE);
			if(ftl_get_l2p_data_error_flag()||ftl_get_blist_data_error_flag())
			{
				ftl_set_qbt_fail_flag();
				memset(l2p_load_mgr.ready_bmp, 0, shr_l2p_seg_bmp_sz);
				l2p_load_mgr.ttl_ready=0;
				spb_id = INV_U16;
			    load_l2p_only = mTRUE;
				fail_cnt = 0;
				blist_mode = INV_U8;
				{
					u32 i;
			    	u32 cnt;
			    	u32 *vc;
			        dtag_t dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
					ftl_l2p_reset(&hns->l2p_ele);
			        for (i = 0; i < get_total_spb_cnt(); i++) {
					    vc[i] = 0;
				    }
			        ftl_l2p_put_vcnt_buf(dtag, cnt, true);
				}
				goto reload;
			}
            if(!load_l2p_only)
            {
                if(!ftl_get_blist_data_error_flag())
                {
                    FTL_CopyFtlBlkDataFromBuffer(0);
                }
                else
                {
                    ftl_apl_trace(LOG_ALW, 0x0dae, "QBT Blist read fail");
                }
            }
            break;

        case FTL_PBT_BUILD_L2P:
			if(blist_mode == FTL_QBT_LOAD_BLIST)
			{
				ftl_qbt_pbt_reload(&hns->l2p_ele, SPB_POOL_QBT_ALLOC, load_l2p_only);
				if(!ftl_get_blist_data_error_flag())
                {
                    FTL_CopyFtlBlkDataFromBuffer(0);
                }
				ftl_l2p_reset(&hns->l2p_ele);
				memset(l2p_load_mgr.ready_bmp, 0, shr_l2p_seg_bmp_sz);
				load_l2p_only = mTRUE;
				ftl_qbt_pbt_reload(&hns->l2p_ele, SPB_POOL_PBT_ALLOC, load_l2p_only);
				ftl_apl_trace(LOG_ALW, 0x145f, "BLT:QBT L2P:PBT case");
				ftl_reset_pbt_seg_info();
			}
			else
			{
				ftl_qbt_pbt_reload(&hns->l2p_ele, SPB_POOL_PBT_ALLOC, load_l2p_only);

				if(ftl_get_l2p_data_error_flag())
					ftl_reset_pbt_seg_info();

				if(ftl_get_last_pbt_blk() != INV_U16)//add by Jay  ,read last open pbt blk 
				{
					memset(l2p_load_mgr.ready_bmp, 0, shr_l2p_seg_bmp_sz);
					load_l2p_only = mTRUE;
					ftl_l2p_reset_partial(&hns->l2p_ele,ftl_get_last_pbt_seg());
					ftl_qbt_pbt_reload(&hns->l2p_ele, SPB_POOL_PBT, load_l2p_only);
					if(ftl_get_l2p_data_error_flag())
					{
						ftl_apl_trace(LOG_ALW, 0x3643,"open PBT %d read fail reload pbt again",ftl_get_last_pbt_blk());
						ftl_reset_pbt_seg_info();
						memset(l2p_load_mgr.ready_bmp, 0, shr_l2p_seg_bmp_sz);
						ftl_l2p_reset(&hns->l2p_ele);
						goto reload;
					}	
				}

			}
            break;

        case FTL_USR_BUILD_L2P:
            break;

        default:
            panic("impossible build l2p mode!\n");
    }

    if(ftl_get_l2p_data_error_flag())
    {
        // read L2P fail, reset L2P
        ftl_apl_trace(LOG_ALW, 0xc4c0, "L2P read fail, re init");
		u32 i;
	    dtag_t dtag;
    	u32 cnt;
    	u32 *vc;
        ftl_l2p_reset(&hns->l2p_ele);
        dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
        for (i = 0; i < get_total_spb_cnt(); i++) {
		    vc[i] = 0;
	    }
        ftl_l2p_put_vcnt_buf(dtag, cnt, true);
    }

    //avoid blklist read fail then SerialNumber is smaller than last blk sn
    gFtlMgr.SerialNumber = max(gFtlMgr.SerialNumber,prev_max_blk_sn);

#if (SPOR_QBT_MAX_ONLY == mENABLE)
    LOAD_END:
#endif

    #if 0 // for dbg, by Sunny Lin
    ftl_l2p_print();
    spb_pool_table();
    ftl_vc_tbl_print();
    #endif
#else
	if(QBT_TAG)
	{
		ftl_qbt_pbt_reload(&hns->l2p_ele);
	    //ftl_misc_reload();// Curry 20200810
	}

	if(QBT_TAG == false)
	{

	    u32 i;
	    dtag_t dtag;
    	u32 cnt;
    	u32 *vc;
		ftl_apl_trace(LOG_ALW, 0x476d, "read l2p fail reset l2p & vac table");

		ftl_l2p_reset(&hns->l2p_ele);
        dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
        for (i = 0; i < get_total_spb_cnt(); i++) {
		    vc[i] = 0;
	    }
        ftl_l2p_put_vcnt_buf(dtag, cnt, true);
	}
#endif
	}
	hns->ns->flags.b.reconing = false;
}

init_code void ftl_host_ns_full_scan(u32 nsid)
{
#if 0
	sn_t sn = 0;
	u32 can_cnt = 0;
	spb_id_t last = INV_SPB_ID;
	ftl_hns_t *hns = &_ftl_hns[nsid];
	ftl_ns_desc_t *ftl_ns_desc = hns->ns->ftl_ns_desc;
	u32 tot = hns->ns->pools[SPB_POOL_SLC].allocated +
			hns->ns->pools[SPB_POOL_NATIVE].allocated;

	memset(ftl_ns_desc->spb_queue, 0xff, sizeof(ftl_ns_desc->spb_queue));
	ftl_l2p_reset(&hns->l2p_ele);
	recon_res_get();
	while (1) {
		spb_id_t can = spb_mgr_pop_busy_spb(nsid, &sn, last);
		bool slc;
		spb_ent_t spb_ent;
		bool ret;

		if (can == INV_SPB_ID)
			break;

		last = can;
		slc = is_spb_slc(can);
		can_cnt++;
		ret = recon_force_scan_spb(can, nsid, slc);
		ftl_apl_trace(LOG_ERR, 0x3af0, ">>>> %d %d\n", ftl_ns_desc->spb_sn, spb_get_sn(can) + 1);
		ftl_ns_desc->spb_sn = max(ftl_ns_desc->spb_sn, spb_get_sn(can) + 1);

		if (ret == true) {
			u32 idx = is_spb_gc_write(can) ? FTL_CORE_GC : FTL_CORE_NRM;

			spb_ent.all = 0;
			spb_ent.b.spb_id = can;
			spb_ent.b.slc = slc;
			spb_ent.b.sn = spb_get_sn(spb_ent.b.spb_id);
			CBF_INS(hns->ns->spb_queue[idx], ret, spb_ent.all);
		}
	}
	sys_assert(can_cnt == tot);
	recon_res_put();
#if (FTL_L2P_SEG_BMP == mENABLE)
	memset(shr_l2p_seg_dirty_bmp, 0, shr_l2p_seg_bmp_sz);
#endif
	l2p_mgr_vcnt_rebuild();
#endif
}
#ifdef Dynamic_OP_En
extern epm_info_t* shr_epm_info;
#endif
init_code void ftl_host_ns_post(u32 nsid)
{
	u32 i;
	ftl_ns_pool_t *ns_pool;
	ftl_hns_t *hns = &_ftl_hns[nsid];

	ns_pool = &hns->ns->pools[SPB_POOL_FREE];

#ifdef Dynamic_OP_En	
	epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);	

	if((epm_ftl_data->OPFlag == 1) && ((epm_ftl_data->OP_LBA_L != 0) || (epm_ftl_data->OP_LBA_H != 0)))
	{
		u64 OP_cap = 0;

		OP_cap = (u64)epm_ftl_data->OP_LBA_L + (((u64)(epm_ftl_data->OP_LBA_H))<<32) ;
		ns[nsid - 1].cap = OP_cap ;
		ftl_apl_trace(LOG_ALW, 0x9812, "Ocan6 ns_post 0x%x%x, nsid %d, OPFlag %d, OP_LBA 0x%x 0x%x \n",(u32)(ns[nsid - 1].cap>>32), ns[nsid - 1].cap, nsid, epm_ftl_data->OPFlag, epm_ftl_data->OP_LBA_H, epm_ftl_data->OP_LBA_L);
	}	
	else
#endif		
	{
	//ns[nsid - 1].cap = LDA_TO_LBA((u64)hns->ns->capacity);
	ns[nsid - 1].cap =(((u64)hns->ns->capacity) << (LDA_SIZE_SHIFT - host_sec_bitz));//joe add sec size 20200820
	}	

	ns[nsid - 1].seg_off = hns->l2p_ele.seg_off;
	ns[nsid - 1].seg_end = hns->l2p_ele.seg_end;
	ns[nsid - 1].flags.all = 0;

	flush_fsm_init(nsid);
	hns->ns->ftl_ns_desc = sys_log_desc_get_ptr(&hns->ns->ns_log_desc);
	shr_range_ptr = hns->ns->ftl_ns_desc->frange;
/*
	host_du_fmt = DU_FMT_USER_4K;
#if defined(HMETA_SIZE)
	if (hns->ns->ftl_ns_desc->flags.b.hmeta) {
		ns[nsid - 1].flags.b.host_meta = 1;
		host_du_fmt = DU_FMT_USER_4K_HMETA;
	}
#endif
*/
    evlog_printk(LOG_ALW, "ftl_host_ns_post host_du_fmt 0x%x %d", &host_du_fmt, host_du_fmt);
	if (hns->ns->ftl_ns_desc->flags.b.du4k)
		ns[nsid - 1].flags.b.du4k = 1;

	if (ftl_virgin) {
		for (i = 0; i < SPB_POOL_MAX; i++)
			hns->ns->ftl_ns_desc->quota[i] = hns->ns->pools[i].quota;

		frb_log_set_spare_cnt(ns_pool->quota * get_interleave());
	}

	for (i = 0; i < SPB_POOL_MAX; i++) {
		hns->ns->pools[i].quota = hns->ns->ftl_ns_desc->quota[i];
		//ftl_apl_trace(LOG_ALW, 0, " pool[%d] %d/%d",
			//i, hns->ns->pools[i].allocated, hns->ns->pools[i].quota);
	}
}

init_code ftl_err_t ftl_host_ns_start(u32 nsid, ftl_err_t ins_err)
{
	extern void ftl_l2p_print_lda0(void);
	ftl_hns_t *hns = &_ftl_hns[nsid];
	ftl_ns_pool_t *ns_pool;
	ftl_err_t ret;

	ns_pool = &hns->ns->pools[SPB_POOL_FREE];

	ns_pool->gc_thr.block = 2;
	ns_pool->gc_thr.heavy = 4;
	ns_pool->gc_thr.start = 20;//6;
	ns_pool->gc_thr.end = 24;//10;

	hns->ns->ftl_ns_desc->flags.b.clean = 1;
	ftl_apl_trace(LOG_ALW, 0xf611, "host ns%d start: v%d c%d", nsid,
			hns->ns->ftl_ns_desc->flags.b.virgin, hns->ns->ftl_ns_desc->flags.b.clean);

#if(WARMBOOT_FTL_HANDLE == mENABLE)
#if(WA_FW_UPDATE == DISABLE)
	if(_fg_warm_boot == true)
	{
		ftl_apl_trace(LOG_ALW, 0x149c, "[FTL]Warm boot l2p not init");
		shr_ftl_flags.b.l2p_all_ready = true;
		ret = FTL_ERR_RECON_DONE;
		return ret;
	}
	else
#endif
#endif
	{
#if (SPOR_FLOW == mENABLE)
    ftl_l2p_reset(&hns->l2p_ele);
	hns->ns->ftl_ns_desc->flags.b.virgin = 0;
#if (FTL_L2P_SEG_BMP == mENABLE)
	memset(shr_l2p_seg_dirty_bmp, 0xff, shr_l2p_seg_bmp_sz);
#endif

    // if EPM set op flag is true, don't load blk list & L2P, Sunny 20211028
    #ifdef Dynamic_OP_En
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    if(epm_ftl_data->Set_OP_Start != EPM_SET_OP_TAG)
    #endif
    {
        if(ftl_is_host_data_exist() && (epm_ftl_data->format_tag != FTL_FULL_TRIM_TAG) )
        {
            ftl_host_ns_dirty_boot(nsid);
        }
    }

	ftl_l2p_print_lda0();
	ret = FTL_ERR_RECON_DONE;
#else
    if (QBT_TAG == false)//(hns->ns->ftl_ns_desc->flags.b.virgin)
	{
		ftl_l2p_reset(&hns->l2p_ele);
		hns->ns->ftl_ns_desc->flags.b.virgin = 0;
#if (FTL_L2P_SEG_BMP == mENABLE)
		memset(shr_l2p_seg_dirty_bmp, 0xff, shr_l2p_seg_bmp_sz);
#endif
		shr_ftl_flags.b.l2p_all_ready = true;
		ret = FTL_ERR_RECON_DONE;
	}
	else
	{
		ftl_host_ns_dirty_boot(nsid);
		shr_ftl_flags.b.spor = true;
		shr_ftl_flags.b.l2p_all_ready = true;
		ret = FTL_ERR_RECON_DONE;
	}
#endif


	ftl_ns_upd_gc_perf(100);
	return ret;
	}
}

fast_code void ftl_host_ns_update_desc(u32 nsid)
{
	ftl_ns_t *this_ns = ftl_ns_get(nsid);
	ftl_ns_desc_t *desc = this_ns->ftl_ns_desc;
	ftl_fence_t ffence;
	u32 i;
	u32 j;

	ffence.nsid = nsid;
	ftl_core_update_fence(&ffence);
	desc->ffence = ffence;
	for (i = 0; i < FTL_CORE_MAX; i++) {
		u32 rptr;
		spb_ent_t ent;

		if (CBF_EMPTY(this_ns->spb_queue[i])) {
			desc->spb_queue[i][0] = INV_SPB_ID;
			continue;
		}
		j = 0;
		rptr = this_ns->spb_queue[i]->rptr;
		while (rptr != this_ns->spb_queue[i]->wptr) {
			ent.all = this_ns->spb_queue[i]->buf[rptr];
			desc->spb_queue[i][j] = ent.b.spb_id;
			ftl_apl_trace(LOG_ERR, 0x0d61, "ns %d spb queue [%d] %d\n", nsid, j, ent.b.spb_id);
			j++;
			rptr = next_cbf_idx_2n(rptr, SPB_QUEUE_SZ);
		}
	}

#if 0
	for (i = 0; i < FTL_CORE_MAX; i++) {
		for (j = 0; j < MAX_ASPB_IN_PS; j++) {
			spb_fence_t *fence = &ffence.spb_fence[i][j];
			ftl_apl_trace(LOG_ERR, 0x7677, "ns %d t %d aspb[%d] spb %d sn %d fence 0x%x flag 0x%x\n",
				nsid, i, j, fence->spb_id, fence->sn, fence->ptr, fence->flags.all);
		}
	}
#endif

	for (i = 0; i < shr_range_size; i++)
		desc->frange[i] = shr_range_ptr[i];
	for (i = 0; i < shr_range_size; i++)
		desc->frange[i] |= shr_range_ptr[shr_range_size + i];
}

fast_code void ftl_host_ns_format(ftl_ns_t *ns, u32 ns_id, bool host_meta)
{
	u32 i;
	dtag_t dtag;
	u32 cnt;
	u32 *vc;
	u32 free_pool_cnt = spb_pool_get_good_spb_cnt(SPB_POOL_UNALLOC);// - FTL_MAX_QBT_CNT;
	ftl_hns_t *hns = &_ftl_hns[ns_id];

	ftl_apl_trace(LOG_ERR, 0x66e6, "free_pool_cnt %d", free_pool_cnt);

	dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);

	ftl_l2p_reset(&hns->l2p_ele);

	ns->closed_spb_cnt = 0;
	for (i = 0; i < FTL_CORE_MAX; i++)
		CBF_INIT(&ns->dirty_spb_que[i]);

	for (i = 0; i < get_total_spb_cnt(); i++) {
		//u8 nsid = spb_get_nsid(i);

		//if (nsid != ns_id)//
			//continue;

		//u32 flag = spb_get_flag(i);
		// retired spb keep all flags when it's released, can't release twice
		//if ((flag & SPB_DESC_F_RETIRED) && (flag & SPB_DESC_F_GCED))
			//continue;

		vc[i] = 0;

		//spb_clr_sw_flag(i, SPB_SW_F_FREEZE);
		//spb_set_flag(i, SPB_DESC_F_CLOSED | SPB_DESC_F_GCED);
		//spb_release(i);
	}

	ftl_l2p_put_vcnt_buf(dtag, cnt, true);
	ftl_ns_link_pool(1, SPB_POOL_UNALLOC, 0, 0);
	ftl_ns_link_pool(1, SPB_POOL_FREE, CAP_NEED_SPBBLK_CNT, free_pool_cnt);
	ftl_ns_link_pool(1, SPB_POOL_USER, 0, 0);
	spb_mgr_preformat_desc_init(SPB_FREE);

	hns->ns->ftl_ns_desc->flags.b.hmeta = host_meta;

	sys_assert(spb_get_free_cnt(SPB_POOL_USER) == 0);

}

fast_code void get_host_ns_spare_cnt(u32 nsid, u32 *avail, u32 *spare, u32 *thr)
{
    //ftl_ns_t *ns = _ftl_hns[nsid].ns;

    //u32 pool_id;

    extern u32 DYOP_LBA_H;

    extern u32 DYOP_LBA_L;

    u64 lba = ((u64)DYOP_LBA_H<<32)|(u64)DYOP_LBA_L;

    u32 LDA_cnt = LBA2LDA(lba+NR_LBA_PER_LDA_MASK);

    u32 host_op_blk = 0;

    extern spb_mgr_t _spb_mgr;

    *thr = ftl_setting.avail_spare_thr;

    u32 spb_size_in_du = shr_nand_info.geo.nr_pages * shr_nand_info.interleave * DU_CNT_PER_PAGE;
    u32 host_need_spb_cnt = occupied_by(_max_capacity, spb_size_in_du);


    if(lba!=0)
    {
        LDA_cnt = _max_capacity - LDA_cnt;
        host_op_blk = LDA_cnt / spb_size_in_du;           //lba->spb
    }


	*avail = (shr_nand_info.geo.nr_blocks - (2*QBT_BLK_CNT + srb_reserved_spb_cnt) - (host_need_spb_cnt - host_op_blk) - _spb_mgr.pool_info.free_cnt[SPB_POOL_UNALLOC] - tcg_reserved_spb_cnt);
	//tatol capacity - (qbt&pbt + system blk) - (user capacity - host op)  - unalloc pool cnt - support TCG
	*spare = ((host_need_spb_cnt*OVER_PROVISION/100) + host_op_blk);	//14%op + host op
	ftl_apl_trace(LOG_ERR, 0xb7ee, "y_avail:%d spare:%d host_need_spb_cnt:%d host op:%d unalloc pool cnt: %d ",
        *avail, *spare, host_need_spb_cnt, host_op_blk, _spb_mgr.pool_info.free_cnt[SPB_POOL_UNALLOC]);

    if((*avail)>(shr_nand_info.geo.nr_blocks)) //prevent avail<0 due to available spare error

    {

         *avail = 0;

    }

    else if((*avail)>=(*spare))

    {

        *avail = *spare;

    }

}

/*! @} */
