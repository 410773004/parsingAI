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
/*! \file spb_pool.c
 * @brief define spb pool
 *
 * \addtogroup spb_pool
 *
 * @{
 * We only have four pool for spb, reserved, slc, native, mix.
 * 1. Reserved pool, is not allocated, not used spb pool
 * 2. slc is only be used in slc mode
 * 3. native is only be used in native mode
 * 4. mix can be used as slc or native which was determined when allocated
 */
#include "ftlprecomp.h"
#include "spb_info.h"
#include "spb_pool.h"
#include "spb_mgr.h"
#include "ftl_flash_geo.h"
#include "defect_mgr.h"
#include "ftl_remap.h"
#include "spb_mgr.h"
#include "epm.h"
#include "srb.h"
#include "ftl_ns.h"

/*! \cond PRIVATE */
#define __FILEID__ spbp
#include "trace.h"
/*! \endcond PRIVATE */

/*! @brief definition of spb pool */
typedef struct _spb_pool_t {
	u32 good_spb_cnt;		///< good spb count, included well remapped
	u32 partial_spb_cnt;		///< partial spb count
	u32 bad_spb_cnt;		///< bad spb count, valid block is too few to be used
	u32 partial_blk_cnt;		///< valid block count of all partial spb
	u32 good_blk_cnt_in_bad_spb;	///< good blocks of all bad spb
	u32 total_blk_cnt;		///< total valid blocks
} spb_pool_t;

fast_data_zi spb_pool_t _spb_pools[SPB_POOL_MAX];	///< spb pools
share_data_zi epm_info_t*  shr_epm_info;
extern u16 min_good_pl;
extern u32 CAP_NEED_PHYBLK_CNT;
extern u8 tcg_reserved_spb_cnt;

/*!
 * @brief dump function of spb pool
 *
 * @return	not used
 */
slow_code_ex void spb_pool_dump(void)
{
	u32 i;

	for (i = 0; i < 1; i++) {
		ftl_apl_trace(LOG_ALW, 0x9056, "[%d] total blk %d", i, _spb_pools[i].total_blk_cnt);
		ftl_apl_trace(LOG_ALW, 0x7d0f, "     good/partial/bad spb cnt = %d/%d/%d",
				_spb_pools[i].good_spb_cnt, _spb_pools[i].partial_spb_cnt, _spb_pools[i].bad_spb_cnt);

		ftl_apl_trace(LOG_ALW, 0x091f, "     partial blk %d, zombie blk %d",
				_spb_pools[i].partial_blk_cnt, _spb_pools[i].good_blk_cnt_in_bad_spb);

		shr_ftl_smart.good_phy_spb = _spb_pools[i].total_blk_cnt;
	}
}

ddr_code void spb_pool_init(void)
{
	u32 i;
	u32 interleave = get_interleave();
	u32 thr = interleave / 2;
/////////////////////////Alan Huang for record Phyblk_OP in EEPROM and MPInfo
#if 1
	extern epm_info_t* shr_epm_info;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	if(epm_ftl_data->BadSPBCnt == 0)
	{	
        thr = interleave - min_good_pl;
        memset(_spb_pools, 0, sizeof(_spb_pools));
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
        for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
        for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
        	u32 df_cnt = spb_get_defect_cnt(i);
        	spb_pool_t *pool = &_spb_pools[spb_info_get(i)->pool_id];

        	if (df_cnt > thr) {
        		pool->bad_spb_cnt++;
        		pool->good_blk_cnt_in_bad_spb += interleave - df_cnt;
        		df_cnt = interleave;
        	} else if (df_cnt) {
        		pool->partial_spb_cnt++;
        		pool->partial_blk_cnt += interleave - df_cnt;
        	} else {
        		pool->good_spb_cnt++;
        	}

        	pool->total_blk_cnt += interleave - df_cnt;
        }

        u32 remain_phy_blk;
        epm_ftl_data->BadSPBCnt = _spb_pools[0].bad_spb_cnt;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
        u32 avail_blk_cnt = shr_nand_info.geo.nr_blocks - 3 - srb_reserved_spb_cnt - _spb_pools[0].bad_spb_cnt - tcg_reserved_spb_cnt - CACHE_RSVD_BLOCK_CNT;  //3: 2 PBT 1 QBT
#else
        u32 avail_blk_cnt = shr_nand_info.geo.nr_blocks - 3 - srb_reserved_spb_cnt - _spb_pools[0].bad_spb_cnt - tcg_reserved_spb_cnt;  //3: 2 PBT 1 QBT
#endif
        remain_phy_blk = (avail_blk_cnt - 682)*interleave - 
                        (_spb_pools[0].partial_spb_cnt*interleave - _spb_pools[0].partial_blk_cnt) - avail_blk_cnt; //*100 change to 100%, <14 fail.   raid plane num = avail_blk_cnt. host need blk cnt:682
        epm_ftl_data->Phyblk_OP = remain_phy_blk * 100 / (682 * interleave);  
        epm_ftl_data->PhyOP = remain_phy_blk * 1000 / (682 * interleave);

        epm_update(FTL_sign,(CPU_ID-1));
        cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_SysInfo_UPDATE, 0, 0);
	}
#endif
///////////////////////////Alan Huang
#ifdef SKIP_MODE
	thr = interleave - min_good_pl; //interleave / 2  //TLC need 212 good interleave (4T:256 interleave)
#else
	thr = interleave / 2;
#endif

	ftl_apl_trace(LOG_ALW, 0x090f, "[FTL] spb_pool_init");
	memset(_spb_pools, 0, sizeof(_spb_pools));
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		u32 df_cnt = spb_get_defect_cnt(i);
		spb_pool_t *pool = &_spb_pools[spb_info_get(i)->pool_id];

		if (df_cnt > thr) {
			pool->bad_spb_cnt++;
			pool->good_blk_cnt_in_bad_spb += interleave - df_cnt;
			df_cnt = interleave;
#ifndef SKIP_MODE
		} else if (df_cnt) {
			pool->partial_spb_cnt++;
			pool->partial_blk_cnt += interleave - df_cnt;
#endif
		} else {
			pool->good_spb_cnt++;
		}

		pool->total_blk_cnt += interleave - df_cnt;
	}

	spb_pool_dump();
}

#if (SPB_BLKLIST == mENABLE)
ddr_code u32 spb_pool_alloc(u32 min_spb_cnt, u32 max_spb_cnt, u8 pool_id)
{
	u32 i;
	u32 reset_flag = mFALSE;
	u32 cnt = 0;
	u32 last_idx = INV_U32;
	u32 alloc_cnt = _spb_pools[pool_id].good_spb_cnt + max_spb_cnt;
	u8  qbt_cnt_per_grp = FTL_QBT_BLOCK_CNT;
	u8  qbt_grp = 0;
	ftl_apl_trace(LOG_INFO, 0x1b55, "min %d max %d, pool %d", min_spb_cnt, max_spb_cnt, pool_id);
	
	if(min_spb_cnt == 0 ||max_spb_cnt == 0){
		return cnt;
	}
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)	    
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if((epm_ftl_data->qbt_tag == FTL_QBT_TAG) && (pool_id == SPB_POOL_QBT_FREE))
 #else
    if(pool_id == SPB_POOL_QBT_FREE)
 #endif
	{
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)		
		QBT_GRP_HANDLE[0].head_qbt_idx = epm_ftl_data->qbt_grp1_head;
        QBT_GRP_HANDLE[0].tail_qbt_idx = epm_ftl_data->qbt_grp1_tail;
        QBT_GRP_HANDLE[1].head_qbt_idx = epm_ftl_data->qbt_grp2_head;
        QBT_GRP_HANDLE[1].tail_qbt_idx = epm_ftl_data->qbt_grp2_tail;
#endif

		spb_info_get(QBT_GRP_HANDLE[0].head_qbt_idx)->pool_id = pool_id;		
		spb_info_get(QBT_GRP_HANDLE[0].tail_qbt_idx)->pool_id = pool_id;		
		spb_info_get(QBT_GRP_HANDLE[1].head_qbt_idx)->pool_id = pool_id;		
		spb_info_get(QBT_GRP_HANDLE[1].tail_qbt_idx)->pool_id = pool_id;

        QBT_GRP_HANDLE[0].grp_cnt_in_alloc = 0;
		QBT_GRP_HANDLE[1].grp_cnt_in_alloc = 0;

		if((QBT_GRP_HANDLE[0].head_qbt_idx == QBT_GRP_HANDLE[0].tail_qbt_idx) && (FTL_QBT_BLOCK_CNT == 1))
		{
			spb_info_get(QBT_GRP_HANDLE[0].head_qbt_idx)->block = QBT_GRP_HANDLE[1].head_qbt_idx;
			spb_info_get(QBT_GRP_HANDLE[1].head_qbt_idx)->block = INV_U16;
		}
		else
		{
			spb_info_get(QBT_GRP_HANDLE[0].head_qbt_idx)->block = QBT_GRP_HANDLE[0].tail_qbt_idx;
			spb_info_get(QBT_GRP_HANDLE[0].tail_qbt_idx)->block = QBT_GRP_HANDLE[1].head_qbt_idx;
			spb_info_get(QBT_GRP_HANDLE[1].head_qbt_idx)->block = QBT_GRP_HANDLE[1].tail_qbt_idx;
			spb_info_get(QBT_GRP_HANDLE[1].tail_qbt_idx)->block = INV_U16;
		}
		
		spb_mgr_set_head(pool_id, QBT_GRP_HANDLE[0].head_qbt_idx);
		spb_mgr_set_tail(pool_id, QBT_GRP_HANDLE[1].tail_qbt_idx);
		_spb_pools[pool_id].good_spb_cnt += FTL_MAX_QBT_CNT;
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)	        
		ftl_apl_trace(LOG_INFO, 0xdab5, "grp1_h %d grp1_t %d" ,epm_ftl_data->qbt_grp1_head,epm_ftl_data->qbt_grp1_tail);
		ftl_apl_trace(LOG_INFO, 0x7dcf, "grp2_h %d grp2_t %d" ,epm_ftl_data->qbt_grp2_head,epm_ftl_data->qbt_grp2_tail);
#endif        
		return QBT_BLK_CNT*2;
	}

	if(_spb_pools[pool_id].good_spb_cnt > 0){
		last_idx = spb_mgr_get_tail(pool_id);
	}
#ifdef SKIP_MODE	
	u32 interleave = get_interleave();
	u32 thr = interleave - min_good_pl;   //TLC need 212 good interleave (4T:256 interleave)
#endif
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
#ifdef SKIP_MODE
		u32 df_cnt = spb_get_defect_cnt(i);

		//if((pool_id == SPB_POOL_SLC) && (df_cnt!=0)) continue;
		//if (spb_info_get(i)->pool_id == SPB_POOL_UNALLOC)

		ftl_apl_trace(LOG_DEBUG, 0xd76f, "spb:%d, pool:%d, df_cnt:%d, thr:%d",i,spb_info_get(i)->pool_id, df_cnt,thr);
		if ((spb_info_get(i)->pool_id == SPB_POOL_UNALLOC) && (df_cnt <= thr))
#else
		u32 df_cnt = spb_get_defect_cnt(i);
		if (df_cnt == 0 && spb_info_get(i)->pool_id == SPB_POOL_UNALLOC) 
#endif	
        {
#if FTL_DONT_USE_REMAP_SPB
			if (is_remapped_spb(i))
				continue;
#endif
			if(last_idx != INV_U32){
				spb_info_get(last_idx)->block = i;
			}

			spb_info_get(i)->pool_id = pool_id;

			last_idx = i;
			
			//if (pool_id == SPB_POOL_SLC)
				//spb_info_get(i)->flags |= SPB_INFO_F_SLC;		//this flag just for dump log

			if (pool_id == SPB_POOL_FREE)
				spb_info_get(i)->flags |= SPB_INFO_F_NATIVE;	//this flag just for dump log

			//if (pool_id == SPB_POOL_MIX)
				//spb_info_get(i)->flags |= SPB_INFO_F_MIX;		//this flag just for dump log

			_spb_pools[pool_id].good_spb_cnt++;
			_spb_pools[pool_id].total_blk_cnt += get_interleave() - df_cnt;
			
			if(1 == _spb_pools[pool_id].good_spb_cnt){
				spb_mgr_set_head(pool_id, i);
			}
			if(_spb_pools[pool_id].good_spb_cnt >= alloc_cnt){
				spb_info_get(i)->block = INV_U16;
				spb_mgr_set_tail(pool_id, i);
				reset_flag = mTRUE;
			}	

            if(pool_id == SPB_POOL_QBT_FREE)
            {
            	if(qbt_cnt_per_grp == FTL_QBT_BLOCK_CNT)
            	{
					QBT_GRP_HANDLE[qbt_grp].head_qbt_idx = i;
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)                     
					if(qbt_grp == 0)
					{
						epm_ftl_data->qbt_grp1_head = i;
					}
					else if(qbt_grp == 1)
					{
						epm_ftl_data->qbt_grp2_head = i;
					}
#endif                    
					QBT_GRP_HANDLE[qbt_grp].grp_cnt_in_alloc = 0;
					ftl_apl_trace(LOG_ERR, 0xcef4, "[FTL]qbt[%d] head %d\n", qbt_grp, QBT_GRP_HANDLE[qbt_grp].head_qbt_idx);
				}
				qbt_cnt_per_grp--;
				if(qbt_cnt_per_grp == 0)
				{
					QBT_GRP_HANDLE[qbt_grp].tail_qbt_idx = i;
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)                     
					if(qbt_grp == 0)
					{
						epm_ftl_data->qbt_grp1_tail = i;
					}
					else if(qbt_grp == 1)
					{
						epm_ftl_data->qbt_grp2_tail = i;
						ftl_apl_trace(LOG_INFO, 0xc538, "grp1_h %d grp1_t %d" ,epm_ftl_data->qbt_grp1_head,epm_ftl_data->qbt_grp1_tail);
						ftl_apl_trace(LOG_INFO, 0xabc3, "grp2_h %d grp2_t %d" ,epm_ftl_data->qbt_grp2_head,epm_ftl_data->qbt_grp2_tail);
					}
#endif                    
					qbt_grp++;
					qbt_cnt_per_grp = FTL_QBT_BLOCK_CNT;
				}
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)                 
				epm_ftl_data->qbt_tag = FTL_QBT_TAG;
#endif
			}

			ftl_apl_trace(LOG_DEBUG, 0xeb61, "spb %d in pool %d", i, pool_id);
			cnt++;
			if (cnt >= max_spb_cnt)
				break;
		}
	}
	if(reset_flag == mFALSE){
		spb_info_get(last_idx)->block = INV_U16;
		spb_mgr_set_tail(pool_id, last_idx);
	}

	ftl_apl_trace(LOG_INFO, 0xa743, "pool %d has %d spb, head:%d, tail:%d, total_blk_cnt: %u", pool_id, _spb_pools[pool_id].good_spb_cnt,spb_mgr_get_head(pool_id),spb_mgr_get_tail(pool_id), _spb_pools[pool_id].total_blk_cnt);
	sys_assert(cnt >= min_spb_cnt);
    if (pool_id == SPB_POOL_FREE)
    {
#if RAID_SUPPORT
        sys_assert((_spb_pools[pool_id].total_blk_cnt - _spb_pools[pool_id].good_spb_cnt - CAP_NEED_PHYBLK_CNT) * 100 / CAP_NEED_PHYBLK_CNT >= 1);  //_spb_pools[pool_id].good_spb_cnt: 1spb=>1plane raid,  1: 1%op
#else
        sys_assert((_spb_pools[pool_id].total_blk_cnt - CAP_NEED_PHYBLK_CNT) * 100 / CAP_NEED_PHYBLK_CNT >= 1); 
#endif
    }
	return cnt;
}
ddr_code u32 spb_pool_unalloc(u32 min_spb_cnt, u32 max_spb_cnt, u8 pool_id)
{
	u32 i;
	u32 last_idx = INV_U32;
	u32 cnt = 0;
	u32 tmp_bad_cnt = _spb_pools[pool_id].bad_spb_cnt;
	u32 interleave = get_interleave();
	u32 thr = interleave - min_good_pl;
	ftl_apl_trace(LOG_INFO, 0xd827, "min %d max %d bad %d, pool %d", min_spb_cnt, max_spb_cnt, tmp_bad_cnt, pool_id);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		u32 df_cnt = spb_get_defect_cnt(i);
		if((spb_info_get(i)->pool_id == SPB_POOL_UNALLOC) && (df_cnt > thr)){
			if(last_idx != INV_U32){
				spb_info_get(last_idx)->block = i;
			}
			spb_info_get(i)->pool_id = pool_id;
			last_idx = i;
			if(tmp_bad_cnt == _spb_pools[pool_id].bad_spb_cnt){
				spb_mgr_set_head(pool_id, i);
			}
			tmp_bad_cnt--;
			cnt++;
		}
	}	
	spb_info_get(last_idx)->block = INV_U16;
	spb_mgr_set_tail(pool_id, last_idx);
	return cnt;
}
#else
init_code u32 spb_pool_alloc(u32 min_spb_cnt, u32 max_spb_cnt, u8 pool_id)
{
	u32 i;
	u32 cnt = 0;

#ifdef SKIP_MODE	
	u32 interleave = get_interleave();
	u32 thr = interleave - min_good_pl;   //TLC need 212 good interleave (4T:256 interleave)
#endif

	ftl_apl_trace(LOG_ALW, 0xc4b3, "min %d max %d, pool %d", min_spb_cnt, max_spb_cnt, pool_id);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{

#ifdef SKIP_MODE
		u32 df_cnt = spb_get_defect_cnt(i);

		//if((pool_id == SPB_POOL_SLC) && (df_cnt!=0)) continue;
		//if (spb_info_get(i)->pool_id == SPB_POOL_UNALLOC) 
		if ((spb_info_get(i)->pool_id == SPB_POOL_UNALLOC) && (df_cnt <= thr))
#else
		u32 df_cnt = spb_get_defect_cnt(i);
		if (df_cnt == 0 && spb_info_get(i)->pool_id == SPB_POOL_UNALLOC) 
#endif			
		{

#if FTL_DONT_USE_REMAP_SPB
			if (is_remapped_spb(i))
				continue;
#endif
			spb_info_get(i)->pool_id = pool_id;

			if (pool_id == SPB_POOL_FREE)
				spb_info_get(i)->flags |= SPB_INFO_F_NATIVE;

			_spb_pools[pool_id].good_spb_cnt++;
			_spb_pools[pool_id].total_blk_cnt += get_interleave();
			ftl_apl_trace(LOG_DEBUG, 0xca22, "spb %d in pool %d", i, pool_id);
			cnt++;
			if (cnt >= max_spb_cnt)
				break;
		}
	}

	sys_assert(cnt >= min_spb_cnt);
	ftl_apl_trace(LOG_ALW, 0xff1e, "pool %d has %d spb", pool_id, _spb_pools[pool_id].good_spb_cnt);

	return cnt;
}
#endif

init_code u32 spb_pool_get_good_spb_cnt(u32 pool_id)
{
	return _spb_pools[pool_id].good_spb_cnt;
}

init_code u32 spb_pool_get_ttl_spb_cnt(u32 pool_id)
{
	return _spb_pools[pool_id].good_spb_cnt + _spb_pools[pool_id].partial_spb_cnt;
}

/*! @} */

