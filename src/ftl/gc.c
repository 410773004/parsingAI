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
/*! \file gc.c
 * @brief start of GC process, and calculate GC performance
 *
 * \addtogroup ftl
 * \defgroup gc
 * \ingroup ftl
 * @{
 *
 */
//=============================================================================

#include "ftlprecomp.h"
#include "l2p_mgr.h"
#include "ncl_exports.h"
#include "ftl_flash_geo.h"
#include "mpc.h"
#include "ftl_ns.h"
#include "spb_mgr.h"
#include "gc.h"
#include "spb_info.h"
#include "ftl_l2p.h"
#include "defect_mgr.h"
#include "ErrorHandle.h"
#include "event.h"

/*! \cond PRIVATE */
#define __FILEID__ fgc
#include "trace.h"
/*! \endcond */

//#define GC_PERF_MA_SHF		3			///< GC performance moving acerage count shift
//#define GC_PERF_MA_SZ		(1 << GC_PERF_MA_SHF)	///< GC performance moving average count

/*! @brief definition of gc object */
/*
typedef struct _gc_t {
	spb_id_t spb_id;	///< current gc spb
	u32 gc_blk_valid_cnt;
	u32 nsid;		///< namespace of current gc spb

	u32 du_per_die;		///< native du count per die
	u32 die_index;		///< not used
	u32 last_bit_off;	///< not used

	u32 free_du_cnt;	///< not used
	u32 start_time;		///< start time of GC

	u32 curr_gc_perf;	///< free du per 100 ms
	u32 avg_gc_perf;	///< average free du per 100 ms

	u32 gc_perf_buf[GC_PERF_MA_SZ];	///< gc performance moving average buffer

	bool full;		///< if moving average buffer was full
	u16 ma_idx;		///< index of moving average buffer
	stGC_FSM_INFO_MODE  gc_mode;        // 8 B
	gc_req_t req;		///< gc req submit to ftl core
} gc_t;
*/
fast_data_zi static gc_t _gc;	///< gc object
fast_data_zi bool gc_continue;
fast_data_zi bool gc_suspend_stop_next_spb;
#if (PLP_SUPPORT == 0)
fast_data_zi bool cancel_delay_gc_suspend;
fast_data_zi gc_action_t *delay_gc_suspend;
#endif
share_data_zi bool gc_suspend_start_wl;
fast_data_zi u16 wl_suspend_spb_id;
fast_data_zi u32 wl_suspend_spb_sn;
fast_data_zi u32 wl_suspend_host_wr_cnt;
fast_data_zi u32 gc_max_free_du_cnt;
fast_data_zi u32 gc_idle_threshold;
fast_data_zi u8 pre_defect_cnt[GC_AVG_DEFECT_CNT];
fast_data_zi u8 pre_defect_pointer;
fast_data_zi u8 gc_read_only_stat;
fast_data u8 evt_gc_end_vac0 = 0xff;
fast_data_zi bool gc_opblk_w_rd;
fast_data_zi bool clear_gc_suspend_flag;
fast_data_zi bool gc_in_small_vac;
share_data_zi volatile bool ctl_wl_flag;
share_data_zi volatile int _fc_credit;
share_data volatile u8 plp_trigger;
share_data_zi volatile bool shr_format_fulltrim_flag;
share_data_zi volatile u8 shr_is_gc_emgr;
fast_data_zi u32 gc_pre_vac;
fast_data_zi u16 gc_after_wl_slow_cnt;
share_data_zi volatile bool shr_is_gc_force;
share_data_zi volatile u8 shr_lock_power_on;
share_data_zi bool shr_shutdownflag;
share_data_zi spb_id_t rd_open_close_spb[FTL_CORE_GC + 1];
share_data_zi u16 gc_next_blk_cost;
fast_data_ni struct timer_list gc_read_only_timer;
#ifdef STOP_BG_GC
share_data_zi volatile u8 STOP_BG_GC_flag;
#endif
extern u8 shr_gc_no_slow_down;
extern u32 gc_cnt;
extern u8  cur_ro_status;
extern read_only_t read_only_flags;
extern tencnet_smart_statistics_t *tx_smart_stat;
extern u32 global_gc_mode; // ISU, GCRdFalClrWeak
extern bool delay_flush_spor_qbt;
share_data_zi u16 gc_gen_p2l_num;
share_data_zi u16 shr_max_alloc_wl_speed;
#if SYNOLOGY_SETTINGS
share_data_zi volatile bool gc_wcnt_sw_flag;
#endif

share_data epm_info_t*  shr_epm_info;

//-------------------------------------------------------------------
// Function      : gc_get_mode(void)
// Description  : Select GC mode, accroding gcMode
// Input          : N/A
// return         : N/A
//-------------------------------------------------------------------
slow_code __inline stGC_MODE gc_get_mode(void)
{
    //FUNC_IN();

#if (PLP_SUPPORT == 0)
	if(gGCInfo.mode.b.non_plp)
	{
		return (GC_MD_NON_PLP);
	}
	else 
#endif
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(gGCInfo.mode.b.plp_slc)
	{
		return (GC_MD_PLP_SLC);
	}
	else 
#endif
	if(gGCInfo.mode.b.lockGC)
    {
        return (GC_MD_LOCK);
    }
    else if(gGCInfo.mode.b.shuttleGC) // There are SBlk in GC Pool (Err or Boot or special case) // ISU, GC_mod_for_EH
    {
		return (GC_MD_SHUTTLE);
    }
    else if(gGCInfo.mode.b.emgrGC)
    {
    	return (GC_MD_EMGR);
    }
    else if(gGCInfo.rdDisturbCnt)
    {
    	return (GC_MD_READDISTURB);
    }
    else if(gGCInfo.mode.b.dataRetention)
    {
        return (GC_MD_DATARETENTION);
    }
    else if(gGCInfo.mode.b.staticWL)
    {
    	return (GC_MD_STATIC_WL);
    }
    else if(gGCInfo.mode.b.forceGC)
    {
    	return (GC_MD_FORCE);
    }
    else if(gGCInfo.mode.b.DBGGC)
    {
        return (GC_MD_DBG);
    }
	else if(gGCInfo.mode.b.idleGC)
    {
    	return (GC_MD_IDLE);
    	//return (GC_MD_NON);
    }
	else
    {
    	return (GC_MD_NON);
    }
}

#if 0//adams
/*!
 * @brief set gc performance to moving average buffer, and update average gc performance
 *
 * @param new_gc_perf	latest gc performance
 *
 * @return		not used
 */
slow_code static inline void set_gc_perf(u32 new_gc_perf)
{
	_gc.curr_gc_perf = new_gc_perf;

	if (_gc.full == false) {
		_gc.gc_perf_buf[_gc.ma_idx++] = new_gc_perf;
		_gc.avg_gc_perf += new_gc_perf;
		if (_gc.ma_idx >= GC_PERF_MA_SZ) {
			_gc.full = true;
			_gc.avg_gc_perf = _gc.avg_gc_perf >> GC_PERF_MA_SHF;	// 1st average
			_gc.ma_idx = 0;
		}
		return;
	}

	_gc.avg_gc_perf -= _gc.gc_perf_buf[_gc.ma_idx] >> GC_PERF_MA_SHF;
	_gc.avg_gc_perf += new_gc_perf >> GC_PERF_MA_SHF;
	_gc.gc_perf_buf[_gc.ma_idx++] = new_gc_perf;
	if (_gc.ma_idx >= GC_PERF_MA_SZ)
		_gc.ma_idx = 0;
}
#endif
ddr_code void gc_end_vac0_free(u32 parm, u32 payload, u32 sts)
{
	if(_gc.spb_id == payload)
		ftl_gc_done(payload, 0);
	else
		ftl_apl_trace(LOG_INFO, 0x95f2, "panic! parm %d, payload %d, sts %d, gc_spb %d", parm, payload, sts, _gc.spb_id);
}

init_code void gc_init(void)
{
	extern u32 shr_p2l_grp_cnt;  // 224(32die), 112(16die), 56(8die)
	extern u32 shr_gc_op;
	gc_continue=false;
	gc_suspend_stop_next_spb=false;
	gc_suspend_start_wl = false;
	shr_gc_read_disturb_ctrl = false;
	ctl_wl_flag = false;
	clear_gc_suspend_flag = false;
	pre_shr_host_write_cnt = 0;
	gc_pre_vac = 0;
	_gc.spb_id = INV_SPB_ID;
	_gc.nsid = 0;
	_gc.full = false;
    gGCInfo.mode.all16 = 0;
    gGCInfo.state.all16 = 0;
    gGCInfo.shuttleCnt = 0;
	gGCInfo.patrlrdCnt = 0;
    gGCInfo.rdDisturbCnt = 0;
    set_open_gc_blk_rd(false);
    shr_dummy_blk = INV_SPB_ID;
	rd_gc_flag = false;
	wl_suspend_spb_id = INV_SPB_ID;
	wl_suspend_spb_sn = INV_SN;
	wl_suspend_host_wr_cnt = 0;
	gc_after_wl_slow_cnt = 0;
	gc_in_small_vac = false;
	shr_gc_op = 1000;

	#if (PLP_SUPPORT == 0)
	delay_gc_suspend = NULL;
	cancel_delay_gc_suspend = false;
	#endif
	evt_register(gc_end_vac0_free, 0, &evt_gc_end_vac0);
	for(u8 i = 0; i < GC_AVG_DEFECT_CNT; i++)
		pre_defect_cnt[i] = 0;
	pre_defect_pointer = 0;

    rd_open_close_spb[FTL_CORE_NRM] = INV_SPB_ID;
    rd_open_close_spb[FTL_CORE_GC] = INV_SPB_ID;

	gc_read_only_stat = 0;
    gc_read_only_timer.function = gc_read_only_handle;
    gc_read_only_timer.data = NULL;

	_gc.du_per_die = round_up_by_2_power(get_mp() * (get_page_per_block()) * DU_CNT_PER_PAGE, 256);
	_gc.die_index = (get_ch() - 1) * (get_lun() * get_ce()) + (get_ce() - 1) * get_lun() + (get_lun() - 1);

	_gc.last_bit_off = (get_page_per_block() - 1) * get_mp() * DU_CNT_PER_PAGE \
			+ (get_mp() - 1) * DU_CNT_PER_PAGE + (DU_CNT_PER_PAGE - 1);

	gc_max_free_du_cnt = (shr_nand_info.lun_num * shr_nand_info.geo.nr_planes - RAID_SUPPORT) * DU_CNT_PER_PAGE * shr_nand_info.geo.nr_pages \
		-  shr_nand_info.geo.nr_planes * DU_CNT_PER_PAGE * shr_p2l_grp_cnt;
	gc_idle_threshold = gc_max_free_du_cnt >> 10;  // less than 1/1000 vac will trigger idle gc
}

fast_code bool gc_busy(void)
{
	if (_gc.spb_id != INV_SPB_ID)
    {
    	#if 0
    	u32 decrease = _gc.prev_free_cnt - spb_get_free_cnt(SPB_POOL_FREE);
    	if(decrease > 3)
    	{
    		decrease = 100-(decrease*10);
			if(decrease <= 0)
				decrease=10;
			ftl_ns_upd_gc_perf(_gc.nsid,decrease);
		}
		#endif
		//ftl_ns_upd_gc_perf(100);

        ftl_apl_trace(LOG_ERR, 0xf168, "_gc.spb_id: %d", _gc.spb_id);
		tzu_get_gc_info();
		return true;
    }

	return false;
}

fast_code bool gc_start(spb_id_t spb_id, u32 nsid, u32 vac, completion_t done)
{
	extern u32 shr_gc_op;
#if(PLP_SUPPORT == 0) 
    epm_FTL_t* epm_ftl_data; 
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag); 
#endif 
//  ftl_apl_trace(LOG_INFO, 0, "rd_gc_flag %d, shr_gc_read_disturb_ctrl %d, shr_gc_fc_ctrl %d",
//        rd_gc_flag, shr_gc_read_disturb_ctrl, shr_gc_fc_ctrl);
    if((GC_MODE_CHECK & GC_EMGR) || ((!host_idle) && (GC_MODE_CHECK & GC_FORCE)) || gc_in_small_vac || (GC_MODE_CHECK & GC_NON_PLP)){
		gc_in_small_vac = false;
        shr_gc_read_disturb_ctrl = false;
    }
    else if(rd_gc_flag || (GC_MODE_CHECK & GC_STATIC_WL)){
        shr_gc_read_disturb_ctrl = true;
    }

	if((!shr_gc_read_disturb_ctrl) && ((GC_MODE_CHECK & GC_FORCE) || (GC_MODE_CHECK & GC_STATIC_WL) || (_gc.gc_mode.mode == GC_MD_IDLE)))
	{
		_fc_credit = (shr_gc_fc_ctrl != true) ? 0 : 256;//0;  // continue in GC control mode, do not reset it to prevent large latency, or give min 256
		shr_gc_fc_ctrl = true;
		//_fc_credit = 0;
	}

    if(shr_gc_read_disturb_ctrl && shr_gc_fc_ctrl == true){
        shr_gc_read_disturb_ctrl = false;
    }

#ifdef ERRHANDLE_GLIST
    //u8 find_in_gc = find_shuttle_blk_cnt(SPB_POOL_GC);
    if((ps_open[FTL_CORE_GC] != INV_U16) && (spb_get_sw_flag(ps_open[FTL_CORE_GC]) & SPB_SW_F_PADDING))
    {
		ftl_apl_trace(LOG_ALW, 0x4feb, "[EH] gc opBlk:%d clsing", ps_open[FTL_CORE_GC]);

		// Pop from GCD and push back to GC and wait for shuttle GC, ISU, LJ1-242
		#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(spb_id == PLP_SLC_BUFFER_BLK_ID)
			return false;
		#endif
		if (spb_get_flag(spb_id) & (SPB_DESC_F_WEAK | SPB_DESC_F_RD_DIST | SPB_DESC_F_PATOAL_RD))
			FTL_BlockPopPushList(SPB_POOL_GC, spb_id, FTL_SORT_NONE);
		else
			FTL_BlockPopPushList(SPB_POOL_USER, spb_id, FTL_SORT_BY_SN);

        return false;
    }
#endif

	if (_gc.spb_id != INV_SPB_ID) {
		ftl_apl_trace(LOG_ALW, 0x5eb5, "GCING, pend blk:%d ns:%d", spb_id, nsid);
		return false;
	}
	if(plp_trigger || shr_shutdownflag)
	{
		gc_suspend_stop_next_spb = true;
		return false;
	}
	gc_suspend_stop_next_spb=false; //tzu need check
	u16 flags = spb_get_flag(spb_id);
	u16 sw_flags = spb_get_sw_flag(spb_id);
	sys_assert((sw_flags & SPB_SW_F_FREEZE) == 0);

	_gc.spb_id = spb_id;
	_gc.nsid = nsid;
	_gc.start_time = get_tsc_64();//jiffies;

	_gc.req.spb_id = spb_id;
	_gc.req.sn = spb_get_sn(spb_id);
	_gc.req.done = done;
	_gc.req.flags.all = 0;

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(spb_id == PLP_SLC_BUFFER_BLK_ID)
		goto next;
#endif

	if(vac == INV_U32)
	{
	    u32* vc;
	    u32 dtag_cnt;
		dtag_t dtag;
	    dtag=ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
		vac = vc[spb_id];
	    ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
	}
	if(vac == 0)
	{
		evt_set_cs(evt_gc_end_vac0, spb_id, 0, CS_TASK);
		//ftl_gc_done(spb_id, 0);  //may lead to stack_overflow
		return true;
	}
	extern u32 shr_p2l_grp_cnt;  // 224(32die), 112(16die), 56(8die)
	/*int total = (shr_nand_info.lun_num * shr_nand_info.geo.nr_planes - RAID_SUPPORT) * DU_CNT_PER_PAGE * shr_nand_info.geo.nr_pages \
            - shr_nand_info.geo.nr_planes * DU_CNT_PER_PAGE * shr_p2l_grp_cnt;//recalculate p2l_du_cnt*/
    u32 total = gc_max_free_du_cnt;
	u32 freeblkCnt = spb_get_free_cnt(SPB_POOL_FREE);
	if (freeblkCnt < GC_BLKCNT_ACTIVE)    //11,10,9
		total -= gc_slow_down_per_blk(total, freeblkCnt);       // *0.1% use 2^10 replace 1000
	else if (freeblkCnt > GC_BLKCNT_SPEED_UP && freeblkCnt <= GC_BLKCNT_DEACTIVE)	//13,14,15   speed up host to keep in GC
		total += (total * (freeblkCnt - GC_BLKCNT_SPEED_UP) >> 8);	   // speed up host wr by 0.39% per block when ready to leave GC

	u32 gc_defect_cnt = spb_get_defect_cnt(spb_id);				//current gc spb defect cnt
	u32 ttl_blk_defect = gc_defect_cnt;
	for(u8 i = 0; i < GC_AVG_DEFECT_CNT; i++)
		ttl_blk_defect += pre_defect_cnt[i];
	int denominator = total-vac-(ttl_blk_defect*(shr_nand_info.geo.nr_pages / shr_nand_info.bit_per_cell)*12/(GC_AVG_DEFECT_CNT+1))-gc_next_blk_cost;  // GC end will add  (must - defect die for consistency)
	if (denominator < 0) denominator = 0;
	pre_defect_cnt[pre_defect_pointer] = (u8)gc_defect_cnt;
	pre_defect_pointer++;
	if(pre_defect_pointer == GC_AVG_DEFECT_CNT)
		pre_defect_pointer = 0;

    shr_host_write_cnt = denominator * gc_gen_p2l_num / shr_p2l_grp_cnt;  // one p2l could serve host write du counts cpu3 adams

	if(spb_id == wl_suspend_spb_id && _gc.req.sn == wl_suspend_spb_sn)
	{
		shr_host_write_cnt = wl_suspend_host_wr_cnt;  //Avoid host speed increase quickly when doing the SPB suspended by WL
		_gc.req.flags.b.wl_suspend = true;
	}
	else if(shr_host_write_cnt < (shr_nand_info.lun_num << 2))  // 512 / 128 die = 4, << 2
		shr_host_write_cnt = (shr_nand_info.lun_num << 2);
    if(GC_MODE_CHECK & GC_LOCK)
    {
        shr_host_write_cnt*=30;
        shr_host_write_cnt/=100;
    }
    else if(GC_MODE_CHECK & GC_EMGR)
    {
    	if(freeblkCnt > GC_BLKCNT_EMER_SPD_CTL)
    	{
	    	//shr_host_write_cnt*=95;
	    	shr_host_write_cnt*=90;
	        shr_host_write_cnt/=100;
			//if((shr_host_write_cnt > pre_shr_host_write_cnt) && (pre_shr_host_write_cnt > 0))
			//	shr_host_write_cnt = pre_shr_host_write_cnt;
		}
	    else if(freeblkCnt == GC_BLKCNT_EMER_SPD_CTL)
	    {
    		shr_host_write_cnt*=80;
			shr_host_write_cnt/=100;
	    }
		else
		{
			shr_host_write_cnt*=50;
			shr_host_write_cnt/=100;
		}
	}
	else if(GC_MODE_CHECK & GC_STATIC_WL && global_gc_mode != GC_MD_FORCE)
	{
//		ftl_apl_trace(LOG_INFO, 0, "shr_host_write_cnt %d, pre_shr_host_write_cnt %d", shr_host_write_cnt, pre_shr_host_write_cnt);
		if(pre_shr_host_write_cnt != 0 && gc_pre_vac != 0)
		{
			/*if (pre_shr_host_write_cnt > (total * 45 / 100 *2 / shr_p2l_grp_cnt))
			{
				shr_host_write_cnt = (pre_shr_host_write_cnt * 90 / 100);
			}else if(pre_shr_host_write_cnt > (total * 40 / 100 *2 / shr_p2l_grp_cnt)){

				shr_host_write_cnt = (pre_shr_host_write_cnt * 95 / 100);
			}else*/
			u16 wl_time_ratio = max(100, min(vac * 90 / gc_pre_vac, 300)); // ratio of vac, likely to the ratio of time of GC (90% for WL not in high speed)
			u32 pre_host_wr_cnt = shr_host_write_cnt;
			shr_host_write_cnt = pre_shr_host_write_cnt * wl_time_ratio / 100; //increase speed for WL may cost more time (100 <= percent <= 300)
			u16 wl_speed_slow_percent = (max(pre_host_wr_cnt, shr_host_write_cnt) - pre_host_wr_cnt) * 100 / pre_shr_host_write_cnt;
			gc_after_wl_slow_cnt += occupied_by(wl_speed_slow_percent, 5); // how many 5% should be slowed down speed after WL
			//ftl_apl_trace(LOG_INFO, 0, "host: %d, pre: %d, ratio: %d, percent: %d, slow_cnt: %d", 
			//	shr_host_write_cnt, pre_shr_host_write_cnt, wl_time_ratio, wl_speed_slow_percent, gc_after_wl_slow_cnt);
		} else{

			if(freeblkCnt >= GC_BLKCNT_ACTIVE)
				shr_host_write_cnt = shr_max_alloc_wl_speed * gc_gen_p2l_num;
		}
		//gc_wl_speed_flag = true;
		if(shr_host_write_cnt > (shr_max_alloc_wl_speed * gc_gen_p2l_num))  // limit max shr_host_write_cnt
			shr_host_write_cnt = shr_max_alloc_wl_speed * gc_gen_p2l_num;
//		ftl_apl_trace(LOG_INFO, 0, "shr_host_write_cnt %d, pre_shr_host_write_cnt %d", shr_host_write_cnt, pre_shr_host_write_cnt);
    }
	/*
	if(otf_forcepbt == true){
		shr_host_write_cnt_old = shr_host_write_cnt;
		shr_host_write_cnt     = 512;
		ftl_apl_trace(LOG_INFO, 0, "shr_host_write_cnt_old %d",shr_host_write_cnt_old);
	}
	*/
    u32 OP = (denominator << 10) / total;
	if(global_gc_mode != GC_MD_STATIC_WL)
	{
	    pre_shr_host_write_cnt = shr_host_write_cnt;
		if(global_gc_mode <= GC_MD_LOCK)
			shr_gc_op = OP;
		if(gc_after_wl_slow_cnt)  // slow down 5% after WL
		{
		    if(freeblkCnt < GC_BLKCNT_ACTIVE && (!(GC_MODE_CHECK & GC_EMGR)))
				shr_host_write_cnt = shr_host_write_cnt * 95 / 100;
			gc_after_wl_slow_cnt--;
		}
	}
	ftl_apl_trace(LOG_INFO, 0x2ab0, "start gc spb %d (ns %d flag 0x%x sw 0x%x sn %d) OP %d/10", 
		spb_id, nsid, flags,sw_flags, _gc.req.sn, OP); 
	ftl_apl_trace(LOG_INFO, 0x40ea, "rd_gc_flag %d, shr_gc_read_disturb_ctrl %d, shr_gc_fc_ctrl %d, defect %d->%d/32, vac %d",
        rd_gc_flag, shr_gc_read_disturb_ctrl, shr_gc_fc_ctrl, gc_defect_cnt, ttl_blk_defect, vac);

#if(PLP_SUPPORT == 1) 
	if (flags & SPB_DESC_F_OPEN)
#else
    if ((flags & SPB_DESC_F_OPEN)||(epm_ftl_data->host_open_blk[0] == spb_id)||(epm_ftl_data->gc_open_blk[0] == spb_id)) 
#endif
	{
		_gc.req.flags.b.spb_open = true;
		if(flags & SPB_DESC_F_WARMBOOT_OPEN)
			_gc.req.flags.b.spb_warmboot_open = true;
		//evlog_printk(LOG_ERR,"tzu force close blk %d %d",spb_id);
		//spb_set_flag(spb_id,SPB_DESC_F_CLOSED);//for SPB_SW_F_VC_ZERO flag
	}
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
	if (flags & SPB_DESC_F_PLP_LAST_P2L)
		_gc.req.flags.b.spb_spor_open = true;
#endif
	if (flags & SPB_DESC_F_DSLC)
		_gc.req.flags.b.dslc = true;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	next:
#endif	
	shr_ftl_flags.b.gcing = true;
#if SYNOLOGY_SETTINGS
	gc_wcnt_sw_flag = true;
#endif
	tx_smart_stat->gc_count++;
	ftl_core_gc_start(&_gc.req);
	return true;
}
share_data_zi volatile bool in_gc_end;
share_data_zi volatile u8 STOP_BG_GC_flag;

fast_code void gc_clear_mode(spb_id_t spb_id)
{
    extern read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];
    read_cnt_sts_t *rd_cnt_max_per_blk = (read_cnt_sts_t *)&shr_rd_cnt_sts[0];

	if((spb_get_flag(spb_id) & SPB_DESC_F_WEAK) || (spb_get_flag(spb_id) & SPB_DESC_F_PATOAL_RD))
	{
		//if(get_gcinfo_shuttle_cnt() && (spb_get_flag(spb_id) & SPB_DESC_F_WEAK))
		if((spb_get_flag(spb_id) & SPB_DESC_F_WEAK))	// Consider case: gc read failed and set weak w/o increase shuttle cnt, ISU, GCRdFalClrWeak
		{
			if(get_gcinfo_shuttle_cnt() && global_gc_mode == GC_MD_SHUTTLE) // ISU, GCRdFalClrWeak
				set_gcinfo_shuttle_cnt();

			spb_set_sw_flag(spb_id, SPB_SW_F_SHUTTLE_FREE);
			spb_clear_flag(spb_id, SPB_DESC_F_WEAK);
//			ftl_apl_trace(LOG_INFO, 0, "gc pool cnt %d shuttle cnt %d", spb_get_free_cnt(SPB_POOL_GC), gGCInfo.shuttleCnt);
		}

		if(get_gcinfo_patrolrd_cnt() && (spb_get_flag(spb_id) & SPB_DESC_F_PATOAL_RD))
		{
			set_gcinfo_patrolrd_cnt();
			spb_clear_flag(spb_id, SPB_DESC_F_PATOAL_RD);
//			ftl_apl_trace(LOG_INFO, 0, "gc pool cnt %d patrolrd cnt %d", spb_get_free_cnt(SPB_POOL_GC), gGCInfo.patrlrdCnt);
		}
		if((get_gcinfo_shuttle_cnt() == 0) && (get_gcinfo_patrolrd_cnt() == 0)  && GC_MODE_CHECK_SHUTTLE)
		{
			GC_MODE_CLEAN(GC_SHUTTLE);
		}
		ftl_apl_trace(LOG_INFO, 0xe743, "clear SPB[%d]flag: %d, shuttle cnt: %d, patrolrd cnt %d, sw flag 0x%x", spb_id, spb_get_flag(spb_id), get_gcinfo_shuttle_cnt(), get_gcinfo_patrolrd_cnt(), spb_get_sw_flag(spb_id));
	}
	rd_cnt_max_per_blk[spb_id].b.api = false;
	if(spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST)
	{
		if(get_gcinfo_rddisturb_cnt())
		{
			set_gcinfo_rddisturb_cnt();
//			ftl_apl_trace(LOG_INFO, 0, "gc pool cnt %d rdDisturb cnt %d", spb_get_free_cnt(SPB_POOL_GC), gGCInfo.rdDisturbCnt);
		}
		spb_clear_flag(spb_id, SPB_DESC_F_RD_DIST);
		if((get_gcinfo_rddisturb_cnt() == 0) && GC_MODE_CHECK_RD)
		{
			GC_MODE_CLEAN(GC_READDISTURB);
		}
		ftl_apl_trace(LOG_INFO, 0x722b, "clear SPB_DESC_F_RD_DIST GC[%d]flag: %d, rd cnt: %d", spb_id, spb_get_flag(spb_id), get_gcinfo_rddisturb_cnt());
	}
#if CROSS_TEMP_OP
	if(spb_info_get(spb_id)->flags & SPB_INFO_F_OVER_TEMP)
	{
        pGList->dGL_CrossTempCnt++;
        __dmb();
        epm_update(GLIST_sign, (CPU_ID-1));
        //ftl_apl_trace(LOG_INFO, 0, "[GC] OVER_TEMP SPB: %d gc done", spb_id);
	}
#endif
	if(pre_wl_blk == spb_id)
	{
		if(!(GC_MODE_CHECK & GC_FORCE))
		{
			shr_gc_fc_ctrl = 2;
			_fc_credit = 0;
		}
		GC_MODE_CLEAN(GC_STATIC_WL);
	}
	if(dr_can == spb_id)
	{
		dr_can = INV_U16;
		GC_MODE_CLEAN(GC_DATARETENTION);
	}
#if (PLP_SUPPORT == 0)
	if(GC_MODE_CHECK_NON_PLP && global_gc_mode == GC_MD_NON_PLP)
	{
		epm_FTL_t* epm_ftl_data;
		epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
        if(epm_ftl_data->host_open_blk[0] == spb_id)//host blk
        {
            epm_ftl_data->host_open_blk[0] = INV_U16;
            for(u8 i=0;i<SPOR_CHK_WL_CNT;i++){
                epm_ftl_data->host_open_wl[i] = INV_U16;
                epm_ftl_data->host_die_bit[i] = 0;
            }
            epm_ftl_data->host_aux_group = INV_U16;
        }
        if(epm_ftl_data->gc_open_blk[0] == spb_id) //gc blk
        {
            epm_ftl_data->gc_open_blk[0] = INV_U16;
            for(u8 i=0;i<SPOR_CHK_WL_CNT;i++){
                epm_ftl_data->gc_open_wl[i] = INV_U16;
                epm_ftl_data->gc_die_bit[i] = 0;
            }
			epm_ftl_data->gc_aux_group = INV_U16;
        }

        __dmb();
        if(epm_ftl_data->host_open_blk[0] == INV_U16 && epm_ftl_data->gc_open_blk[0] == INV_U16)//gc done
        {
			epm_ftl_data->non_plp_gc_tag 	  = 0;
			epm_ftl_data->non_plp_last_blk_sn = INV_U32;
			epm_update(FTL_sign, (CPU_ID-1));
        }
	}
#endif
}
ddr_code void gc_clear_all(void)  //shutdown & flush fqbt will run this func
{
	extern spb_id_t spb_rd_id;
	gc_continue=false;
	shr_gc_read_disturb_ctrl = false;
	gc_suspend_start_wl = false;
	ctl_wl_flag = false;
	pre_shr_host_write_cnt = 0;
	_gc.spb_id = INV_SPB_ID;
	_gc.nsid = 0;
	_gc.full = false;
	gGCInfo.mode.all16 = 0;
	gGCInfo.state.all16 = 0;
	gGCInfo.shuttleCnt = 0;
	gGCInfo.patrlrdCnt = 0;
	gGCInfo.rdDisturbCnt = 0;
	set_open_gc_blk_rd(false);
	shr_dummy_blk = INV_SPB_ID;
	spb_rd_id = INV_U16;
	pre_wl_blk = INV_U16;
	rd_gc_flag = false;
	shr_is_gc_force = false;
	if(!delay_flush_spor_qbt)
	{
		shr_lock_power_on &= (~GC_LOCK_POWER_ON); //gc_lock --
		gc_read_only_handle(NULL);
	}
	//gc_read_only_stat = 0;
	//shr_lock_power_on &= (~READ_ONLY_LOCK_IO);  // read_only_lock --
}

fast_code void gc_end(spb_id_t spb_id, u32 write_du_cnt)
{
	//u32 gc_ttl_time;
	//u32 new_gc_perf;
	//ftl_ns_pool_t *ns_pool;
	u32 nsid = spb_get_nsid(spb_id);
	u32 *vc;
	u32 dtag_cnt;
	dtag_t dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
	bool skip_chk = false;

#ifdef ERRHANDLE_GLIST
    u16 spb_flag = 0;
#endif
	//u32 pool_id = spb_get_poolid(spb_id);
	#ifdef STOP_BG_GC
    in_gc_end = true;
    #endif
	sys_assert(_gc.spb_id == spb_id);
	ftl_apl_trace(LOG_ALW, 0xb578, "spb %d(%d) gc done, flag: 0x%x, write du %d in %d ms",
		spb_id, nsid, spb_info_get(spb_id)->flags, write_du_cnt, time_elapsed_in_ms(_gc.start_time));

	/*
	if (free_du_cnt != ~0) {
		gc_ttl_time = jiffies - _gc.start_time;
		if(gc_ttl_time == 0)
			gc_ttl_time=1;
		new_gc_perf = (free_du_cnt-(spb_get_defect_cnt(spb_id)*12*384)) / gc_ttl_time;
		//shr_host_perf = new_gc_perf * HZ;
		//u32 remain = spb_get_free_cnt(SPB_POOL_FREE);//ns_pool->quota - ns_pool->allocated;

		ftl_apl_trace(LOG_ALW, 0, "free du %d in %d 100ms", free_du_cnt, gc_ttl_time);
//		if (remain < ns_pool->gc_thr.end) {

        if (remain < GC_BLKCNT_DEACTIVE) {
            if(remain < 15)
            {
				new_gc_perf *= (100-((15-remain)*3));
                new_gc_perf /= 100;
            }
			if (new_gc_perf == 0)
				new_gc_perf = 64;

			set_gc_perf(new_gc_perf);
		}
        ftl_apl_trace(LOG_ALW, 0, "spb %d gc perf %d remain blk cnt %d", spb_id, new_gc_perf,remain);

		if (new_gc_perf == 0)
		new_gc_perf = 64;

		set_gc_perf(new_gc_perf);
	}
	*/


    shr_gc_read_disturb_ctrl=false;

	_gc.nsid = 0;
	_gc.spb_id = INV_SPB_ID;
	shr_ftl_flags.b.gcing = false;
	//gc_wl_speed_flag = false;

	if(plp_trigger)
		gc_suspend_stop_next_spb = true;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(spb_id == PLP_SLC_BUFFER_BLK_ID){
		if(gc_pro_fail)
			gc_pro_fail = false;
		if(write_du_cnt != ~0)
		{
			/*
			extern epm_info_t* shr_epm_info;
            epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

			if(write_du_cnt != epm_ftl_data->plp_flush_slc_du_cnt)
			{
				extern pda_t shr_plp_slc_start_pda;
				extern pda_t shr_plp_slc_end_pda;
				ftl_apl_trace(LOG_ERR, 0, "possible error in gc slc, wr_cnt: %d, st: 0x%x, end: 0x%x", write_du_cnt, epm_ftl_data->plp_flush_slc_du_cnt, shr_plp_slc_start_pda, shr_plp_slc_end_pda);
			}
			*/
			if(vc[PLP_SLC_BUFFER_BLK_ID] != 0)
			{
				ftl_apl_trace(LOG_ERR, 0x1e11,"slc gc fail!!  vc:0x%x %d",vc[PLP_SLC_BUFFER_BLK_ID],vc[PLP_SLC_BUFFER_BLK_ID]);
				//sys_assert(0);
			}
			GC_MODE_CLEAN(GC_PLP_SLC);
			skip_chk = true;
		}
		else
			ftl_apl_trace(LOG_INFO, 0xe9df, "slc gc was suspended, suspend_flag: %d, pro_fail: %d",gc_suspend_stop_next_spb, gc_pro_fail);
		goto end_next;
	}
#endif
//	u32 sw_flag = spb_get_sw_flag(spb_id);	// ISU, GC_mod_for_EH

	if(vc[spb_id] == 0)	// ISU, GC_mod_for_EH
	{
        if(spb_get_flag(spb_id) & SPB_DESC_F_WEAK)
        {
        	#ifdef ERRHANDLE_GLIST
			// ISU, TSSP, PushGCRelsFalInfo
			MK_FailBlk_Info failBlkInfo;
			failBlkInfo.all = 0;
			failBlkInfo.b.wBlkIdx = spb_id;
			failBlkInfo.b.need_gc = false;
			failBlkInfo.b.bakupPBT = true;	// ISU, BakupPBTLat
			ErrHandle_Task(failBlkInfo);
			#endif
        }
		gc_clear_mode(spb_id);
		if(spb_id == wl_suspend_spb_id)
		{
			wl_suspend_spb_id = INV_SPB_ID;
			wl_suspend_spb_sn = INV_U32;
		}
		//ftl_apl_trace(LOG_INFO, 0, "[GC] release blk %d", spb_id);
	    spb_release(spb_id);
	}
#ifdef ERRHANDLE_GLIST
	else if(gc_suspend_stop_next_spb == true || (gc_pro_fail) || gc_suspend_start_wl)
#else
    else if(gc_suspend_stop_next_spb == true || gc_suspend_start_wl)
#endif
	{
#ifdef ERRHANDLE_GLIST
	    gc_pro_fail = false;

        spb_flag = spb_get_flag(spb_id);
        if(spb_flag & (SPB_DESC_F_WEAK | SPB_DESC_F_RD_DIST | SPB_DESC_F_PATOAL_RD))
        {
            FTL_BlockPopPushList(SPB_POOL_GC, spb_id, FTL_SORT_BY_SN);
        }
        else
        {
			if(gc_suspend_start_wl)
			{
				wl_suspend_spb_id = spb_id;
				wl_suspend_spb_sn = spb_get_sn(spb_id);
				wl_suspend_host_wr_cnt = shr_host_write_cnt;
			}
			else if(pre_wl_blk == spb_id)
			{
				ctl_wl_flag = false;
				gc_suspend_start_wl = true;
			}
            ftl_apl_trace(LOG_INFO, 0x3e7d, "suspend and push %d to user pool, vcnt: 0x%x, wl_suspend:%d",spb_id, vc[spb_id], gc_suspend_start_wl);
//			ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
            FTL_BlockPopPushList(SPB_POOL_USER, spb_id, FTL_SORT_BY_SN);
        }
#else
		ftl_apl_trace(LOG_INFO, 0x818f, "suspend and push %d to user pool",spb_id);
		FTL_BlockPopPushList(SPB_POOL_USER, spb_id, FTL_SORT_BY_SN);
#endif
	}
	else
	{
		// ISU, FolwVacCntZeroHdl
		//ftl_apl_trace(LOG_ERR, 0, "possible error can't be free %d, vcnt: 0x%x", spb_id, vc[spb_id]);
		//flush_to_nand(EVT_GC_CANT_FREE_SPB);
		u8 flgWeakTmp = spb_get_flag(spb_id) & SPB_DESC_F_WEAK;
		ftl_apl_trace(LOG_ERR, 0x7b24, "possible error can't be free %d, vcnt: 0x%x, weakFlg: %d", spb_id, vc[spb_id], flgWeakTmp);
		gc_clear_mode(spb_id);
		if (flgWeakTmp)	// Keep weak flag to notice EH when vac == 0 later.
			spb_set_flag(spb_id, SPB_DESC_F_WEAK);
		flush_to_nand(EVT_GC_CANT_FREE_SPB);
	}
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)    
end_next:
#endif
#ifndef EH_GCOPEN_HALT_DATA_IN	// Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
    // GC done, check if EH is waiting for gc suspend.
{
    u8 bIdx = 0;
	if (bFail_Blk_Cnt != 0)
	{
		for (bIdx = 0; bIdx < MAX_FAIL_BLK_CNT; bIdx++)	// DBG, PgFalVry
		{
            u8 bFound = false;	// ISU, GCOpClsBefSuspnd

            if((ps_open[FTL_CORE_GC] == Fail_Blk_Info[bIdx].b.wBlkIdx) && (ps_open[FTL_CORE_GC] != INV_SPB_ID))	// ISU, TSSP, PushGCRelsFalInfo
            {
                gc_suspend_done_flag = true;
				bFound = true;
            }
			else if (Fail_Blk_Info[bIdx].b.gcOpen && Fail_Blk_Info[bIdx].b.wBlkIdx != INV_SPB_ID) // Handle GcOp closed before suspend, ISU, GCOpClsBefSuspnd
			{
				bFound = true;
			}

			if (bFound)
			{
				// ISU, TSSP, PushGCRelsFalInfo
				ftl_apl_trace(LOG_ERR, 0x3bc9, "[EH] GC OpBlk[%d] suspendDoneFlag[%d]", Fail_Blk_Info[bIdx].b.wBlkIdx, gc_suspend_done_flag);
				MK_FailBlk_Info failBlkInfo;
				failBlkInfo.all = 0;
				failBlkInfo.b.wBlkIdx = Fail_Blk_Info[bIdx].b.wBlkIdx;
				failBlkInfo.b.need_gc = true;	// Trigger handle GC open block, need_gc should be required.
				ErrHandle_Task(failBlkInfo);
			}
		}
	}
}
#endif

//	u32 *vc;
//    u32 dtag_cnt;
//	dtag_t dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
	u32 i = 0;
	u16 vac_zero=spb_mgr_get_head(SPB_POOL_GCD);

	for(i =0; i<spb_get_free_cnt(SPB_POOL_GCD);i++)
	{
		if(vc[vac_zero] == 0)
		{
			if(spb_get_flag(vac_zero) & SPB_DESC_F_WEAK)
			{
				#ifdef ERRHANDLE_GLIST	// Recover vac == 0 spb in GCD, ISU, Tx, PgFalClsNotDone (1)
				// ISU, TSSP, PushGCRelsFalInfo
				MK_FailBlk_Info failBlkInfo;
				failBlkInfo.all = 0;
				failBlkInfo.b.wBlkIdx = vac_zero;
				failBlkInfo.b.need_gc = false;
				failBlkInfo.b.bakupPBT = true;	// ISU, BakupPBTLat
				ErrHandle_Task(failBlkInfo);
				#endif
			}
			gc_clear_mode(vac_zero);
			spb_release(vac_zero);
		}
		vac_zero= spb_info_tbl[vac_zero].block;
	}
	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);

	if (clear_gc_suspend_flag)
    {
        gc_suspend_stop_next_spb = false;
        clear_gc_suspend_flag = false;
    }
	//if (_gc.req.done)
	//{
	//	_gc.req.done((void*)free_du_cnt);
	//}
	//else if (free_du_cnt != ~0) //gc is not been aborted or suspended, trigger next gc
	//{
	if(shr_format_fulltrim_flag == false && !skip_chk)
    {
		GC_Mode_DeAssert();
		ftl_ns_gc_start_chk(HOST_NS_ID);
    }
    #ifdef STOP_BG_GC
    in_gc_end = false;
    #endif
	//}
}

fast_code bool gc_action(gc_action_t *action)
{
	if(plp_trigger)
		FTL_NO_LOG = true;
    u8 act = action->act;
    switch(act)
    {
        case GC_ACT_RESUME:
			#if (PLP_SUPPORT == 0)
			if(delay_gc_suspend != NULL)
				cancel_delay_gc_suspend = true;
			#endif

			if(delay_flush_spor_qbt)
				break;
			else if (_gc.spb_id == INV_SPB_ID)
            {
                gc_suspend_stop_next_spb = false;
                shr_gc_fc_ctrl = 2;
                _fc_credit = 0;
                global_gc_mode = 0; // ISU, GCRdFalClrWeak
                //ftl_apl_trace(LOG_INFO, 0, "initial gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
                ftl_ns_gc_start_chk(FTL_NS_ID_START);
            }
            else if (gc_suspend_stop_next_spb)
            {
                clear_gc_suspend_flag = true;
            }
            break;

         #ifdef STOP_BG_GC
         case GC_ACT_STOP_BG_GC:
            if (_gc.spb_id != INV_SPB_ID){
                STOP_BG_GC_flag = 3;
                if(GC_MODE_CHECK_STOP_BG_GC || spb_get_free_cnt(SPB_POOL_FREE) <= GC_BLKCNT_EMER_SPD_CTL){
	                gc_suspend_stop_next_spb = false;
					STOP_BG_GC_flag = 0;
					ftl_apl_trace(LOG_INFO, 0x649a, "ugrt case not suspend,gc mode 0x%x" ,GC_MODE_CHECK);
	                return true;
                }
            }
            else{
                break;
		    }
         #endif
         case GC_ACT_SUSPEND:
         case GC_ACT_ABORT:
            #ifdef STOP_BG_GC
            if((STOP_BG_GC_flag == 3)&&(gc_suspend_stop_next_spb == false)){
                STOP_BG_GC_flag = 1;
            }else{
                STOP_BG_GC_flag = 0;
            }
            #endif
			#if (PLP_SUPPORT == 0)
			if(gc_get_mode() == GC_MD_NON_PLP && !gc_pro_fail && !plp_trigger)
			{
				gc_suspend_stop_next_spb = false;
				cancel_delay_gc_suspend = false;
				if(delay_gc_suspend == NULL)
				{
					delay_gc_suspend = action;
					delay_gc_suspend->next_delay_act = NULL;
				}
				else
				{
					gc_action_t *gc_action_slot = delay_gc_suspend;
					while (gc_action_slot->next_delay_act != NULL)
						gc_action_slot = gc_action_slot->next_delay_act;
					gc_action_slot->next_delay_act = action;
					action->next_delay_act = NULL;
				}
				ftl_apl_trace(LOG_INFO, 0xc1a7, "NON_PLP_BLK GCing, delay suspend");
				return false;
			}
			else
			#endif
			{
		        gc_suspend_stop_next_spb = true;
				clear_gc_suspend_flag = false;
	            //ftl_apl_trace(LOG_INFO, 0, "set gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
			    //gc is running
	    		if (_gc.spb_id != INV_SPB_ID)
	    		{
	    		     if(ftl_core_gc_action(action) == false)
	    			{

	    				//ftl_apl_trace(LOG_INFO, 0, "set gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
	    			}
	                ftl_apl_trace(LOG_INFO, 0xba6b, " gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
	    			return false;
	    		}
			}
            break;
         default:
            sys_assert(0);
            break;
    }
    ftl_apl_trace(LOG_INFO, 0x707a, " gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
	return true;
}
#if(PLP_GC_SUSPEND == mDISABLE)
fast_code bool gc_action2(gc_action_t *action)//joe add test 202011
{
	if (action->act == GC_ACT_RESUME) {
		gc_suspend_stop_next_spb = false;
		shr_gc_fc_ctrl = 2;
		_fc_credit = 0;
        global_gc_mode = 0; // ISU, GCRdFalClrWeak
		ftl_apl_trace(LOG_INFO, 0xc7fc, "[gc2]initial gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
		if (_gc.spb_id == INV_SPB_ID)
			ftl_ns_gc_start_chk(FTL_NS_ID_START);
	}
	else
	{
		//gc is running
		gc_suspend_stop_next_spb = true;
        #ifdef STOP_BG_GC
        STOP_BG_GC_flag = 0;
        #endif
		ftl_apl_trace(LOG_INFO, 0x55a1, "[gc2]set gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
		if (_gc.spb_id != INV_SPB_ID) {
			//ftl_core_gc_action2(action);
			#if(PLP_GC_SUSPEND == mDISABLE)
            if(ftl_core_gc_action2(action) == false)
	        #else
			if(ftl_core_gc_action(action) == false)
			#endif
			{
				ftl_apl_trace(LOG_INFO, 0x85c9, "[gc2]set gc_suspend_stop_next_spb %d",gc_suspend_stop_next_spb);
			}
			return false;
		}
	}

	return true;
}
#endif
ddr_code void gc_resume(void)
{
	ftl_ns_gc_start_chk(FTL_NS_ID_START);
}
#if 0//adams
fast_code bool get_gc_perf(u32 *avg, u32 *cur)
{
	if (_gc.full == false && _gc.ma_idx == 0) {
		return false; // no GC finished yet
	} else if (_gc.full == false) {
		*avg = _gc.avg_gc_perf / _gc.ma_idx;
		*cur = _gc.curr_gc_perf;
		return true;
	} else {
		*avg = _gc.avg_gc_perf;
		*cur = _gc.curr_gc_perf;
		return true;
	}
}
#endif

ddr_code void Set_gc_shuttle_mode(void)
{
    GC_MODE_SETTING(GC_SHUTTLE);
//    gGCInfo.shuttleCnt++;
    ftl_apl_trace(LOG_INFO, 0xe3aa, "shuttleCnt: %d", gGCInfo.shuttleCnt);
    return;
}

ddr_code void Set_gc_RdDisturb_mode(void)
{
    GC_MODE_SETTING(GC_READDISTURB);
    ftl_apl_trace(LOG_INFO, 0x66b2, "rdDisturbCnt: %d",  gGCInfo.rdDisturbCnt);
    return;
}

ddr_code void Set_gc_shuttleCnt(void)
{
    gGCInfo.shuttleCnt++;
    ftl_apl_trace(LOG_INFO, 0x875e, "set shuttleCnt: %d", gGCInfo.shuttleCnt);
    return;
}

ddr_code void Set_gc_patorlrdCnt(bool para)
{
	switch(para)
	{
		case 0:
			gGCInfo.patrlrdCnt--;
		break;
		case 1:
			gGCInfo.patrlrdCnt++;
		break;
	}
	ftl_apl_trace(LOG_INFO, 0x2d20, "set patrlrdCnt: %d, para: %d",  gGCInfo.patrlrdCnt, para);
	return;
}

ddr_code void Set_gc_rdDisturbCnt(bool para)
{
    switch(para)
    {
        case 0:
            gGCInfo.rdDisturbCnt--;
        break;
        case 1:
            gGCInfo.rdDisturbCnt++;
        break;
    }
    ftl_apl_trace(LOG_INFO, 0x72a5, "set rdDisturbCnt: %d, para: %d",  gGCInfo.rdDisturbCnt, para);
    return;
}

ddr_code u16 Get_gc_rdDisturbCnt(void)
{
    ftl_apl_trace(LOG_INFO, 0x5e40, "get rdDisturbCnt: %d",  gGCInfo.rdDisturbCnt);
    return gGCInfo.rdDisturbCnt;
}



ddr_code spb_id_t GC_FSM_Blk_Select(u32 nsid, u32 pool_id)
//fast_code spb_id_t GC_FSM_Blk_Select(u32 nsid, u32 pool_id)
{
    spb_id_t can;
    can = INV_SPB_ID;
    u32 *vc;
    dtag_t dtag;
    u32 dtag_cnt;
	static u8 wl_to_force_gc_cnt = 0;
//    _gc.spb_id = INV_SPB_ID;
    _gc.gc_mode.mode = gc_get_mode();
//    global_gc_mode = (u32)_gc.gc_mode.mode; // ISU, EHPerfImprove
//    ftl_apl_trace(LOG_INFO, 0, "_gc.gc_mode.mode: %d", _gc.gc_mode.mode);
#if (PLP_SUPPORT == 0)
re_select:
#endif
    switch(_gc.gc_mode.mode)
    {
        case GC_MD_SHUTTLE:
        {
            can = find_shuttle_blk(pool_id);
            break;
        }
        case GC_MD_FORCE:
        case GC_MD_EMGR:
        case GC_MD_LOCK:
        {
            can = find_min_vc(nsid, pool_id);
            break;
        }
        case GC_MD_READDISTURB:
        {
            can = find_weak(pool_id);
            break;
        }
        case GC_MD_DATARETENTION:
        {
            can = find_dr(nsid, pool_id);
			if(can == INV_SPB_ID)
            {
				_gc.gc_mode.mode = gc_get_mode();
				if(GC_MODE_CHECK & GC_FORCE)
				{
					can = find_min_vc(nsid, pool_id);
				}
				else
				{
					return INV_SPB_ID;
				}
            }
            break;
        }
        #if ENABLE_WL
        case GC_MD_STATIC_WL:
        {
            if(spb_get_poolid(pre_wl_blk) != SPB_POOL_USER)
            {
                ftl_apl_trace(LOG_INFO, 0x3345, "[WL]rescan_ec WL now");
                rescan_ec(nsid, SPB_POOL_USER);
                pre_wl_blk = user_min_ec_blk;
                user_min_ec_blk = INV_SPB_ID;
            }
			if(pre_wl_blk == INV_SPB_ID || ctl_wl_flag)
            {
                GC_MODE_CLEAN(GC_STATIC_WL);
				ctl_wl_flag = false;
				gc_suspend_start_wl = false;
				wl_to_force_gc_cnt = 0;
				_gc.gc_mode.mode = gc_get_mode();
				if(GC_MODE_CHECK & GC_FORCE)
				{
					can = find_min_vc(nsid, pool_id);
				}
				else
				{
					ftl_apl_trace(LOG_INFO, 0x97e3, "[WL]pre_wl_blk: %d, can't do WL", pre_wl_blk);
					shr_gc_fc_ctrl = 2;
					_fc_credit = 0;
					return INV_SPB_ID;
				}
            }
            else if(!gc_suspend_start_wl && (GC_MODE_CHECK & GC_FORCE) && (wl_to_force_gc_cnt < 2))  // only can wait for open blk in 2 GC
            {
				ftl_apl_trace(LOG_INFO, 0x826b, "[WL] wait for GC to start new open blk");
				wl_to_force_gc_cnt ++;
				_gc.gc_mode.mode = GC_MD_FORCE;
				can = find_min_vc(nsid, pool_id);
            }
			else
			{
				wl_to_force_gc_cnt = 0;
                can = pre_wl_blk;
                tx_smart_stat->wl_exec_count++;
				gc_suspend_start_wl = false;
				if(GC_MODE_CHECK & GC_FORCE)
					ctl_wl_flag = true;
//                ftl_apl_trace(LOG_INFO, 0, "[GC]wl_cnt: %d", tx_smart_stat->wl_exec_count);
			}
        	break;
        }
		#endif
		#if (PLP_SLC_BUFFER_ENABLE  == mENABLE) 
        case GC_MD_PLP_SLC:
        {
            can = PLP_SLC_BUFFER_BLK_ID;
            ftl_apl_trace(LOG_INFO, 0x7916, "[SLC] GC Start");
            break;
        }
		#endif
        #if (PLP_SUPPORT == 0)
        case GC_MD_NON_PLP:
        {
            can = find_non_plp_blk();
			if(can == INV_SPB_ID)
			{
				if(delay_gc_suspend == NULL)
				{
					_gc.gc_mode.mode = gc_get_mode();
					if(_gc.gc_mode.mode == GC_MD_NON)
						return can;
					else
						goto re_select;
				}
				else
				{
					gc_action_t *action;
					do
					{
						action = delay_gc_suspend->next_delay_act;
						if(delay_gc_suspend->cmpl != NULL)
							delay_gc_suspend->cmpl(delay_gc_suspend);
						else
							sys_free(FAST_DATA, delay_gc_suspend);
						delay_gc_suspend = action;
					}while(delay_gc_suspend != NULL);

					if(!cancel_delay_gc_suspend)
					{
						gc_suspend_stop_next_spb = true;
						return INV_SPB_ID;
					}
					else
					{
						_gc.gc_mode.mode = gc_get_mode();
						if(_gc.gc_mode.mode == GC_MD_NON)
							return INV_SPB_ID;
						else
							goto re_select;
					}
				}
			}
            break;
        }
		#endif
		case GC_MD_IDLE:
		{
			can = find_min_vc(nsid, pool_id);
			if(min_vac > gc_idle_threshold || can == INV_SPB_ID)
			{
				GC_MODE_CLEAN(GC_IDLE);
				return INV_SPB_ID;
			}
			gc_in_small_vac = true;
			break;
		}
        default:
        {
            ftl_apl_trace(LOG_INFO, 0x783a, "[GC] GC False MD[%x]", _gc.gc_mode.mode);
            can = INV_SPB_ID;
            break;
        }
    }
        if(can < get_total_spb_cnt())
        {
			// ISU, EHPerfImprove
			global_gc_mode = (u32)_gc.gc_mode.mode;
			ftl_apl_trace(LOG_INFO, 0x4c26, "_gc.gc_mode.mode: %d", _gc.gc_mode.mode);
			#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(can != PLP_SLC_BUFFER_BLK_ID)
			#endif
            	FTL_BlockPopPushList(SPB_POOL_GCD, can, FTL_SORT_NONE);
            //ftl_apl_trace(LOG_INFO, 0, "[GC]push block %d in GCD POOL", can);

            if(_gc.gc_mode.mode > GC_MD_LOCK)
            {
                dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
                min_vac = vc[can];
				if(_gc.gc_mode.mode != GC_MD_STATIC_WL)
					gc_pre_vac = min_vac;
				else if(!(GC_MODE_CHECK & GC_FORCE) && (min_vac < (gc_max_free_du_cnt / 5)))
					gc_in_small_vac = true; // if WL without Force GC in small vac(20%), WL need speed up
                ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
            }
			else if((_gc.gc_mode.mode > GC_MD_FORCE) && (gc_pre_vac < (gc_max_free_du_cnt / 10)))
			{
				shr_gc_no_slow_down = true;  // if EMER/LOCK GC in small vac(10%), no need wait for host
			}

            ftl_apl_trace(LOG_INFO, 0x89a2, "[GC]Blk_Select:MD[0x%x]Blk[%d]VAC[%d]EC[%d]", _gc.gc_mode.mode, can, min_vac, spb_info_get(can)->erase_cnt);
			ftl_apl_trace(LOG_INFO, 0x14b5, "sw %x flag %x",spb_get_sw_flag(can),spb_get_desc(can)->flags);
            return can;
        }
        else
        {
			ftl_apl_trace(LOG_ERR, 0xe916, "possible something wrong stop gc!!! Blk_Select:MD[0x%x]Blk[%d]", _gc.gc_mode.mode, can);
			//read_only_flags.b.gc_stop = 1;
			//if(cur_ro_status != RO_MD_IN)
			//{
			//cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
			//}
			//flush_to_nand(EVT_READ_ONLY_MODE_IN);

			shr_gc_fc_ctrl = 2;
			_fc_credit = 0;
			//gGCInfo.mode.all16 = 0;
            global_gc_mode = 0; // ISU, GCRdFalClrWeak
			return INV_SPB_ID;
    //            gGCBuf.fsm_flow.nxtState = GC_ST_IDLE;
        }
}

ddr_code void lock_power_on_cancle(void *data)
{
	shr_lock_power_on &= (~GC_LOCK_POWER_ON);  //gc_lock --
}

ddr_code void lock_power_on_gc_ready(void *data)
{
	if(!(GC_MODE_CHECK & GC_FORCE))
	{
		shr_lock_power_on &= (~GC_LOCK_POWER_ON);  //gc_lock --
		GC_Mode_Assert();
	}
}

slow_code void gc_read_only_handle(void *data)
{
	if(gc_read_only_stat)
	{
		gc_read_only_stat = 0;
		shr_lock_power_on &= (~READ_ONLY_LOCK_IO);  // read_only_lock --
		u32 free = spb_get_free_cnt(SPB_POOL_FREE);
		ftl_apl_trace(LOG_ALW, 0x4ac4, "read only lock cancel: 0x%x, freeblkcnt:%d", shr_lock_power_on, free);
		if(free <= GC_BLKCNT_READONLY)
		{
			read_only_flags.b.no_free_blk = 1;

#if(degrade_mode == ENABLE)
			extern smart_statistics_t *smart_stat;
			smart_stat->critical_warning.bits.read_only = 1;
			ftl_apl_trace(LOG_ALW, 0x569c, "Set critical warning bit[3] because gc free block under threshold");
#endif

			if(cur_ro_status != RO_MD_IN)
			{

				cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
				flush_to_nand(EVT_READ_ONLY_MODE_IN);
			}
		}
	}
}
//called when blockpop and one time init
fast_code void GC_Mode_Assert(void)
{
#if(PLP_GC_SUSPEND == mENABLE)
    u32 free = spb_get_free_cnt(SPB_POOL_FREE);
    u8 thr_no_host_write = 0;
    if(host_idle && rd_gc_flag){
        thr_no_host_write = 1;
    }
	if (read_only_flags.b.no_free_blk && free > GC_BLKCNT_READONLY)
	{
			read_only_flags.b.no_free_blk = 0;
  #if(degrade_mode == ENABLE)
			if(read_only_flags.all == 0)
			{
				extern smart_statistics_t *smart_stat;
				smart_stat->critical_warning.bits.read_only = 0;
				ftl_apl_trace(LOG_ALW, 0x3b8d, "Disable critical warning bit[3]");
				cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
			}
  #else
			if(cur_ro_status == RO_MD_IN)
			{
				cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
				flush_to_nand(EVT_READ_ONLY_MODE_OUT);
			}
  #endif

	}

    if(free < GC_BLKCNT_DISKFULL) // 6
    {
    	shr_gc_no_slow_down = true;
		GC_MODE_SETTING(GC_LOCK|GC_EMGR|GC_FORCE);

		if(free <= GC_BLKCNT_READONLY) //4
		{
			if((gc_read_only_stat == 0) && (!read_only_flags.b.no_free_blk))
			{
				gc_read_only_stat = 1;
				shr_lock_power_on |= READ_ONLY_LOCK_IO;  // read_only_lock ++
				mod_timer(&gc_read_only_timer, jiffies + 5*HZ);
				ftl_apl_trace(LOG_INFO, 0xb546, "set read only timer, freeblkcnt:%d", free);
			}
		}
        //GC_STATE_SETTING(GC_STATE_BYPASS);
    	//ftl_apl_trace(LOG_ERR, 0, "[GC] GC Lock FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(free < (GC_BLKCNT_EMER - thr_no_host_write)) // 10
    {
		GC_MODE_SETTING(GC_EMGR|GC_FORCE);
		//if(free < GC_BLKCNT_EMER_SPD_CTL)
		//	shr_gc_no_slow_down = true;
       	//ftl_apl_trace(LOG_ERR, 0, "[GC] GC EMGR FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(free < GC_BLKCNT_ACTIVE) // 20
    {
    	GC_MODE_SETTING(GC_FORCE);
        //ftl_apl_trace(LOG_ERR, 0, "[GC] GC FORE FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
	if(GC_MODE_CHECK & GC_EMGR)
	{
		shr_is_gc_emgr = true;
	}
	if(GC_MODE_CHECK & GC_FORCE)
	{
		shr_is_gc_force = true;
        if(!host_idle){
            if(shr_gc_read_disturb_ctrl){
                shr_gc_read_disturb_ctrl = false;
                shr_gc_fc_ctrl = true;
                _fc_credit = 0;
            }
        }
	}
	ftl_apl_trace(LOG_ERR, 0x31a7, "[GC] GC_Mode_Assert %d FreeBlkCnt:%d, BadBlockCnt:%d",
	GC_MODE_CHECK,free, spb_get_free_cnt(SPB_POOL_UNALLOC));

	/*
	if((free < GC_BLKCNT_ACTIVE) && (GC_STATE_CHECK(GC_STATE_HALT)))
	{
        ftl_apl_trace(LOG_ERR, 0, "[GC] Clean GC HALT!");
		GC_STATE_CLEAN(GC_STATE_HALT);
	}
	*/
#else
    if(spb_get_free_cnt(SPB_POOL_FREE) < GC_BLKCNT_DISKFULL) // 6
    {
		GC_MODE_SETTING(GC_LOCK|GC_EMGR|GC_FORCE);
//		GC_Set_TH[GC_MD_LOCK]();
        GC_STATE_SETTING(GC_STATE_BYPASS);
    	ftl_apl_trace(LOG_ERR, 0xed93, "[GC] GC Lock FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(spb_get_free_cnt(SPB_POOL_FREE) < GC_BLKCNT_EMER) // 10
    {
		GC_MODE_SETTING(GC_EMGR|GC_FORCE);
//		if(gGCBuf.gc_mode.mode != GC_MD_SHUTTLE &&
//		   gGCBuf.gc_mode.type != GC_AUX)
//		{
//			GC_Set_TH[GC_MD_EMGR]();
            //GC_STATE_SETTING(GC_STATE_BYPASS);
//		}
       ftl_apl_trace(LOG_ERR, 0x9d9d, "[GC] GC EMGR FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(spb_get_free_cnt(SPB_POOL_FREE) < GC_BLKCNT_ACTIVE) // 20
    {
    	GC_MODE_SETTING(GC_FORCE);
//        GC_Set_TH[GC_MD_FORCE]();

        ftl_apl_trace(LOG_ERR, 0x4955, "[GC] GC FORE FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }

	if((spb_get_free_cnt(SPB_POOL_FREE) < GC_BLKCNT_ACTIVE) && (GC_STATE_CHECK(GC_STATE_HALT)))
	{
        ftl_apl_trace(LOG_ERR, 0xe9d0, "[GC] Clean GC HALT!");
		GC_STATE_CLEAN(GC_STATE_HALT);
	}

    //if((gGCBuf.gc_mode.mode == GC_MD_FORCE) && (gFtlBlkInfo.blkCnt[FTL_BLOCK_POOL_FREE] < GC_BLKCNT_EMER)) // 9
    //{
    //    GC_Set_TH[gc_get_mode()]();
    //}
#endif
}

//called when push bock to free
fast_code void GC_Mode_DeAssert()
{
#if(PLP_GC_SUSPEND == mENABLE)
    u32 free = spb_get_free_cnt(SPB_POOL_FREE);
    u8 thr_no_host_write = 0;
    if(host_idle && rd_gc_flag){
        thr_no_host_write = 1;
    }
    if(shr_lock_power_on && free > GC_BLKCNT_EMER_SPD_CTL)
    {
		shr_lock_power_on &= (~(GC_LOCK_POWER_ON|READ_ONLY_LOCK_IO));  // gc_lock & read_only_lock --
		gc_read_only_stat = 0;
    }
	if(ctl_wl_flag && free > GC_BLKCNT_ACTIVE)
		ctl_wl_flag = false;

	if (read_only_flags.b.no_free_blk && free > GC_BLKCNT_READONLY)
	{
		read_only_flags.b.no_free_blk = 0;
  #if(degrade_mode == ENABLE)
		if(read_only_flags.all == 0)
		{
			extern smart_statistics_t *smart_stat;
			smart_stat->critical_warning.bits.read_only = 0;
			ftl_apl_trace(LOG_ALW, 0xe2cf, "Disable critical warning bit[3]");
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
		}
  #else
		if(cur_ro_status == RO_MD_IN)
		{
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
			flush_to_nand(EVT_READ_ONLY_MODE_OUT);
		}
  #endif
	}

    if(free > GC_BLKCNT_DEACTIVE)		// 19
    {
		//shr_gc_fc_ctrl = 2;
	    //_fc_credit = 0;

		//if (!get_gcinfo_shuttle_cnt())	// ISU, EHPerfImprove, Paul_20220221
        //    global_gc_mode = 0; // ISU, GCRdFalClrWeak

		shr_is_gc_emgr = false;
		shr_is_gc_force = false;
		rd_gc_flag = false;
		//ftl_apl_trace(LOG_INFO, 0, "[GC]rd_gc_flag: %d, fc_ctl: %d", rd_gc_flag, shr_gc_fc_ctrl);
		GC_MODE_CLEAN(GC_FORCE|GC_EMGR|GC_LOCK);
		//if (!GC_MODE_CHECK)		//if still need to do or doing GC in other mode, not clear this status
        //    global_gc_mode = 0;
    	//ftl_apl_trace(LOG_ERR, 0, "[GC] GC Clr Lock FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(free > (GC_BLKCNT_EMER - thr_no_host_write))		// 12
    {
		GC_MODE_CLEAN(GC_EMGR|GC_LOCK);
		shr_is_gc_emgr = false;
        //ftl_apl_trace(LOG_ERR, 0, "[GC] GC Clr EMGR FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(free > GC_BLKCNT_DISKFULL)	// 8
    {
    	GC_MODE_CLEAN(GC_LOCK);
        //ftl_apl_trace(LOG_ERR, 0, "[GC] GC Clr FORE FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    if(in_gc_end && !(GC_MODE_CHECK & GC_FORCE))
	{
		if(min_vac <= gc_idle_threshold && shr_gc_fc_ctrl)
			GC_MODE_SETTING(GC_IDLE);
		shr_gc_fc_ctrl = 2;
    	_fc_credit = 0;
		if (!GC_MODE_CHECK)		//if still need to do GC, not clear this status
			global_gc_mode = 0;
		ftl_apl_trace(LOG_INFO, 0xa44d, "[GC]rd_gc_flag: %d, fc_ctl: %d", rd_gc_flag, shr_gc_fc_ctrl);
	}
	if(!plp_trigger)
	{
		ftl_apl_trace(LOG_ERR, 0x1ae6, "[GC] GC_Mode_DeAssert %d FreeBlkCnt:%d, BadBlockCnt:%d"
			,GC_MODE_CHECK, free, spb_get_free_cnt(SPB_POOL_UNALLOC));
	}

#else
    if(spb_get_free_cnt(SPB_POOL_FREE) > GC_BLKCNT_DEACTIVE)
    {
    	if(GC_BLKCNT_ACTIVE > GC_BLKCNT_EMER)
    	{
			GC_MODE_CLEAN(GC_FORCE|GC_EMGR|GC_LOCK);
    	}
		else
		{
			GC_MODE_CLEAN(GC_FORCE);
		}
//        if((gGCBuf.gc_mode.mode != GC_MD_SHUTTLE) &&
//		   (gGCBuf.gc_mode.type != GC_AUX))
//		{
//            GC_STATE_CLEAN(GC_STATE_BYPASS);
//        }
    	ftl_apl_trace(LOG_ERR, 0x9804, "[GC] GC Clr Lock FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(spb_get_free_cnt(SPB_POOL_FREE) > GC_BLKCNT_EMER)
    {
		GC_MODE_CLEAN(GC_EMGR|GC_LOCK);
//        if((gGCBuf.gc_mode.mode != GC_MD_SHUTTLE) &&
//		   (gGCBuf.gc_mode.type != GC_AUX))
//		{
//            GC_STATE_CLEAN(GC_STATE_BYPASS);
//        }

        ftl_apl_trace(LOG_ERR, 0xc63b, "[GC] GC Clr EMGR FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
    else if(spb_get_free_cnt(SPB_POOL_FREE) > GC_BLKCNT_DISKFULL)
    {
    	GC_MODE_CLEAN(GC_LOCK);
        ftl_apl_trace(LOG_ERR, 0x5c33, "[GC] GC Clr FORE FreeBlkCnt:%d, BadBlockCnt:%d", spb_get_free_cnt(SPB_POOL_FREE), spb_get_free_cnt(SPB_POOL_UNALLOC));
    }
#endif
}

fast_code spb_id_t get_gc_blk(void)
{
    return _gc.spb_id;
}


fast_code void set_open_gc_blk_rd(bool flag)
{
    gc_opblk_w_rd = flag;
    ftl_apl_trace(LOG_INFO, 0xf72e, "[GC]set_open_gc_blk_rd: %d", gc_opblk_w_rd);
}

fast_code bool get_open_gc_blk_rd(void)
{
    ftl_apl_trace(LOG_INFO, 0xf836, "[GC]get_open_gc_blk_rd: %d", gc_opblk_w_rd);
    return gc_opblk_w_rd;
}




/*! @} */
