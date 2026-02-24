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
/*! \file ftl_ns.c
 * @brief define ftl namespace object
 *
 * \addtogroup ftl
 * \defgroup ftl_ns
 * \ingroup ftl
 * @{
 *
 */
#include "ftlprecomp.h"
#include "bf_mgr.h"
#include "ftl_ns.h"
#include "ftl_flash_geo.h"
#include "spb_pool.h"
#include "ftl_l2p.h"
#include "spb_mgr.h"
#include "ipc_api.h"
#include "fc_export.h"
#include "gc.h"
#include "spb_mgr.h"
#include "event.h"
#include "idx_meta.h"
#include "frb_log.h"
#include "spb_info.h"
#include "ErrorHandle.h"	// DBG, PgFalVry (3)

#ifdef Dynamic_OP_En
#include "epm.h"
#endif

#define __FILEID__ ftlns
#include "trace.h"
/*
#if LBA_SIZE == 512
#define calc_jesd218a_capacity(x)   \
	(((21168 + (1953504ULL * x)) >> NR_LBA_PER_LDA_SHIFT))	///< define jesd disk capacity if lba is 512b
#elif LBA_SIZE == 4096
#define	calc_jesd218a_capacity(x)   \
	((2646 + (244188ULL * x)))					///< define jesd disk capacity if lba is 4k,
#else
#error "LBA size wrong define"
#endif
*/
#define MAX_HOST_PERF_IN_GC	10	// 10 mb per sec
#define MIN_HOST_PERF_IN_GC	2	// 2 mb per sec

#define host_perf_to_gc_perf(mb)	((mb * 256) / 10)

#define MAX_GC_PERF	host_perf_to_gc_perf(MAX_HOST_PERF_IN_GC)	// 10 mb per sec
#define MIN_GC_PERF	host_perf_to_gc_perf(MIN_HOST_PERF_IN_GC)	// 2 mb per sec
#define SPB_CNT_L2P_FLUSH	5

fast_data ftl_ns_t ftl_ns[FTL_NS_ID_MAX] = {
	{ .capacity = ~0, .pools = ftl_ns_pools_init, .attr.all = 0,}, // unalloc ns
	{ .capacity = 0, .pools = ftl_ns_pools_init, .attr.all = 0},
	{ .capacity = 0, .pools = ftl_ns_pools_init, .attr.all = 0}
};

fast_data_zi u32 hns_dirty_bmp = 0;
fast_data u8 evt_hns_dirty_flush = 0xff;
fast_data_zi static spb_appl_t _spb_appl[FTL_NS_ID_MAX][FTL_CORE_MAX];	///< spb applicant of each namespace and type
//fast_data_zi u32 _max_capacity = 0;					///< max capacity of disk
fast_data_zi u32 CAP_NEED_SPBBLK_CNT = 0;

share_data_zi spb_queue_t spb_queue[NS_SUPPORT][MAX_SPBQ_PER_NS];		///< host spb queue to communicate with ftl core
share_data_zi volatile u32 shr_range_size;						///< range bitmap size
share_data_zi volatile u32 shr_tbl_flush_perf;
fast_data_zi stFTL_MANAGER gFtlMgr;
share_data_zi volatile stGC_GEN_INFO gGCInfo;       //GC information
share_data_zi volatile read_only_t read_only_flags;
share_data_zi volatile none_access_mode_t noneaccess_mode_flags;

share_data volatile bool _fg_warm_boot;
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)
extern volatile u32 spor_qbtsn_for_epm;
extern volatile bool delay_flush_spor_qbt; 
#endif

extern u8  cur_ro_status;
share_data_zi volatile int _fc_credit;
extern u8 shr_gc_no_slow_down;
extern tencnet_smart_statistics_t *tx_smart_stat;
extern u32 global_gc_mode; // ISU, GCRdFalClrWeak
share_data_zi volatile spb_id_t rd_open_close_spb[FTL_CORE_GC + 1];
share_data_zi bool shr_format_fulltrim_flag;
extern volatile pbt_resume_t *pbt_resume_param;
#if(degrade_mode == ENABLE)
extern smart_statistics_t *smart_stat;
#endif




/*!
 * @brief notify function of ftl namespace spb allocated
 *
 * @param appl	spb applicant object
 *
 * @return	return true always
 */
static bool ftl_ns_spb_notify(spb_appl_t *appl);

/*!
 * @brief flush dirty host namespace after reconstruction
 *
 * @return	not used
 */
static void ftl_hns_dirty_flush(u32 parm, u32 payload, u32 sts);

/*!
 * @brief calculate op value of X
 *
 * @param op_per	percentage of op
 * @param x		number
 *
 * @return		op value of X
 */
static inline u32 spare_op(op_per_t op_per, u32 x)
{
	return x * op_per / OP_DIVIDER;
}
#if (TRIM_SUPPORT == ENABLE)
sram_sh_data volatile u32 Trim1bitmapSize;
#endif


#ifdef Dynamic_OP_En
extern epm_info_t*  shr_epm_info;
#endif

init_code void ftl_ns_init(void)
{
	ftl_ns[0].capacity = _max_capacity;
	ftl_ns[0].pools[SPB_POOL_UNALLOC].allocated = 0;

#if (TRIM_SUPPORT == ENABLE) //for trim alloc ddr buffer
    Trim1bitmapSize = occupied_by(((_max_capacity+8191) >> 13), DTAG_SZE);//8T drive = 256K
#ifdef L2PnTRIM_DDTAG_ALLOC		//20201207-Eddie
    ddr_dtag_trim_register_lock();
#else
#endif
#endif


	ftl_l2p_init(_max_capacity);

	ftl_int_ns_init(&ftl_ns[FTL_NS_ID_INTERNAL]);
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_NRM].ns_id = FTL_NS_ID_INTERNAL;
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_NRM].type = FTL_CORE_NRM;
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_NRM].flags.all = 0;
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_NRM].notify_func = ftl_ns_spb_notify;


#if (PBT_OP == mENABLE)
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_PBT].ns_id = FTL_NS_ID_INTERNAL;
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_PBT].type = FTL_CORE_PBT;
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_PBT].flags.all = 0;
	_spb_appl[FTL_NS_ID_INTERNAL][FTL_CORE_PBT].notify_func = ftl_ns_spb_notify;
#endif
	ftl_pbt_cnt = 0;
	//ftl_pbt_idx = 0;

	memset(&gFtlMgr, 0x00, sizeof(stFTL_MANAGER));
    gFtlMgr.last_host_blk = INV_U16;
    gFtlMgr.last_gc_blk   = INV_U16;

	evt_register(ftl_hns_dirty_flush, 0, &evt_hns_dirty_flush);

	shr_tbl_flush_perf = ~0;
}

init_code bool ftl_ns_alloc(u32 nsid, u32 cap, bool slc, bool native)
{
	u32 i;
	u32 size = 0;

	if (cap == ~0) {
		size = get_disk_size_in_gb();
#if FTL_DONT_USE_REMAP_SPB
		size /= 16;
#endif
#if RAID_SUPPORT && defined(FPGA)
		size /= 2;
#endif

#if RAID_SUPPORT && !defined(FPGA)
		size *= shr_nand_info.lun_num - 1;
		size /= shr_nand_info.lun_num;
#endif

		cap = calc_jesd218a_capacity(size);
		if (slc && (!native))
			cap /= get_nr_bits_per_cell();
	}

	sys_assert(nsid != 0);
	sys_assert(ftl_ns[nsid].capacity == 0);
	if (ftl_ns[0].capacity < cap)
		return false;

#if 0//def Dynamic_OP_En, change to LBA mode  
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	ftl_apl_trace(LOG_ERR, 0x4f03, "Ocan00 ns0CAP %x, OPFlag %d, size %x, cap %x, DYOPCapacity %x \n", ftl_ns[0].capacity, epm_ftl_data->OPFlag, size, cap, DYOPCapacity);

	u32 DYOPCap;
	DYOPCap = cap;
	if(epm_ftl_data->OPFlag == 1)
	{
		DYOPCap = DYOPCapacity;
	}
#endif

	ftl_apl_trace(LOG_ALW, 0xe2fa, "ftl ns create: %d %d(%d) attr: slc %d", nsid, cap, size, slc);
#if 0//def Dynamic_OP_En, change to LBA mode
	ftl_ns[nsid].capacity = DYOPCap;
#else
	ftl_ns[nsid].capacity = cap;
#endif
	ftl_ns[nsid].attr.b.slc = slc;
	ftl_ns[nsid].attr.b.native = native;
	ftl_ns[nsid].attr.b.mix = DSLC_SPB_RATIO && slc && native;

	#if 0
	ftl_ns[2].spb_queue[FTL_CORE_PBT] = &spb_queue[1][FTL_CORE_PBT];
	sys_assert(SPB_QUEUE_SZ == ARRAY_SIZE(ftl_ns[2].spb_queue[FTL_CORE_PBT]->buf));
	CBF_INIT(&ftl_ns[2].dirty_spb_que[FTL_CORE_PBT]);
	_spb_appl[2][FTL_CORE_PBT].ns_id = 2;
	_spb_appl[2][FTL_CORE_PBT].notify_func = ftl_ns_spb_notify;
	_spb_appl[2][FTL_CORE_PBT].type = FTL_CORE_PBT;
	_spb_appl[2][FTL_CORE_PBT].flags.all = 0;
	#endif
	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_ns[nsid].spb_queue[i] = &spb_queue[nsid - 1][i];
		sys_assert(SPB_QUEUE_SZ == ARRAY_SIZE(ftl_ns[nsid].spb_queue[i]->buf));
		CBF_INIT(&ftl_ns[nsid].dirty_spb_que[i]);
		_spb_appl[nsid][i].ns_id = nsid;
		_spb_appl[nsid][i].notify_func = ftl_ns_spb_notify;
		_spb_appl[nsid][i].type = i;
		_spb_appl[nsid][i].flags.all = 0;
//		if (i == FTL_CORE_GC)
//			_spb_appl[nsid][i].flags.b.older = 1;
	}

	ftl_ns[nsid].retention_chk = ftl_setting.gc_retention_chk;
	ftl_ns[nsid].alloc_retention_chk = ftl_setting.alloc_retention_chk;
	ftl_ns[nsid].flags.all = 0;
	ftl_ns[nsid].closed_spb_cnt = 0;

	shr_range_size = occupied_by(ftl_ns[nsid].capacity, RDISK_RANGE_SIZE);
	shr_range_size = occupied_by(shr_range_size, 8);
	shr_range_size = round_up_by_2_power(shr_range_size, 4);

	sys_log_desc_register(&ftl_ns[nsid].ns_log_desc, 1, sizeof(ftl_ns_desc_t) + shr_range_size, ftl_ns_desc_init);

	ftl_host_ns_init(&ftl_ns[nsid], nsid);
	ftl_ns[0].capacity -= cap;

	return true;
}

init_code void ftl_ns_link_pool(u32 nsid, u32 pool_id, u32 min_spb_cnt, u32 max_spb_cnt)
{
	ftl_ns[nsid].pools[pool_id].max_spb_cnt = max_spb_cnt;
	ftl_ns[nsid].pools[pool_id].min_spb_cnt = min_spb_cnt;

	ftl_ns[nsid].pools[pool_id].allocated = 0;
	if(pool_id == SPB_POOL_UNALLOC)
	{
		ftl_ns[nsid].pools[pool_id].quota = spb_pool_unalloc(min_spb_cnt, max_spb_cnt, pool_id);
	}
	else
	{
	ftl_ns[nsid].pools[pool_id].quota = spb_pool_alloc(min_spb_cnt, max_spb_cnt, pool_id);
	}

	//ftl_apl_trace(LOG_ALW, 0, "ftl ns %d link pool %d", nsid, pool_id);
}

fast_code void ftl_ns_add_pool(u32 nsid, u32 pool_id)
{
	ftl_ns[nsid].pools[pool_id].allocated++;
}

fast_code ftl_ns_pool_t *ftl_ns_get_pool(u32 nsid, u32 pool_id)
{
	return &ftl_ns[nsid].pools[pool_id];
}

fast_code u32 ftl_ns_get_capacity(u32 nsid)
{
	return ftl_ns[nsid].capacity;
}

init_code void ftl_check_fc_done(volatile cpu_msg_req_t *req)
{
	//flush dirty hns ASAP
	evt_set_cs(evt_hns_dirty_flush, 0, 0, CS_TASK);

	//recycle boot dtags when recon done
	boot_dtags_recycle(CPU_FTL - 1);
}

fast_code void ftl_ns_start()
{
	u32 fc_chk_bmp = 0;

	ftl_core_start(1);
	#if (PBT_OP == mENABLE)
	ftl_core_start(FTL_NS_ID_INTERNAL);
	fc_chk_bmp = ((1 << 1) | (1 << FTL_NS_ID_INTERNAL));
	#else
	fc_chk_bmp = (1 << 1);
	#endif

	shr_ftl_flags.b.boot_cmpl = true;
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FC_READY_CHK, 0, fc_chk_bmp);
}

slow_code void ftl_ns_check_appl_allocating(void){
	if(shr_format_fulltrim_flag){
		u8 nsid = 0;
		u8 type = 0;
		ftl_apl_trace(LOG_INFO, 0xbf45, "[IN]");
		for(nsid = FTL_NS_ID_START; nsid < FTL_NS_ID_MAX;nsid++){
			for (type = 0; type < FTL_CORE_MAX; type++) {
				if((nsid == FTL_NS_ID_START && type == FTL_CORE_PBT) || (nsid == FTL_NS_ID_INTERNAL && type == FTL_CORE_GC)){
					continue;
				}
				ftl_apl_trace(LOG_INFO, 0x69a7, "%d/%d, alloc:%d, abort:%d",nsid,type,_spb_appl[nsid][type].flags.b.allocating,_spb_appl[nsid][type].flags.b.abort);
				if(_spb_appl[nsid][type].flags.b.allocating == true && _spb_appl[nsid][type].flags.b.abort == 0){
					_spb_appl[nsid][type].flags.b.abort = true;
					if(nsid == FTL_NS_ID_START){
						ftl_apl_trace(LOG_INFO, 0x0576, "[Do]free_appl:%d/%d",nsid,type);
						ftl_free_blist_dtag(type);				
					}

					if(nsid == FTL_NS_ID_INTERNAL && type == FTL_CORE_PBT){
						pbt_query_ready 		= 1;
						pbt_query_need_resume 	= 0;	
					}
				}
			}
		}
	}
}

fast_code bool ftl_ns_spb_notify(spb_appl_t *appl)
{
	bool ret;
	spb_ent_t ent;
	ftl_ns_t *ns = &ftl_ns[appl->ns_id];

	if(appl->flags.b.abort == true){
		goto notify_abort_end;
	}	

	if (appl->flags.b.ready == 1) {
		gFtlMgr.SerialNumber++;
		spb_set_sn(appl->spb_id, gFtlMgr.SerialNumber);
        //ftl_apl_trace(LOG_INFO, 0, "[Notify]spb_id : 0x%x Blk_SN : 0x%x",appl->spb_id, gFtlMgr.SerialNumber);
	}

    if(appl->flags.b.QBT){
        gFtlMgr.LastQbtSN = gFtlMgr.SerialNumber;
        ftl_apl_trace(LOG_INFO, 0xb62c, "gFtlMgr.LastQbtSN : 0x%x", gFtlMgr.LastQbtSN);
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)
	    spor_qbtsn_for_epm = gFtlMgr.LastQbtSN;
#endif
    }

	if(appl->type == FTL_CORE_PBT){
		if((ftl_pbt_cnt % QBT_BLK_CNT)==0){
		    if((ps_open[FTL_CORE_NRM] != INV_U16) &&
		       (ps_open[FTL_CORE_GC] != INV_U16))
		    {
		        gFtlMgr.TableSN = (spb_get_sn(ps_open[FTL_CORE_NRM]) < spb_get_sn(ps_open[FTL_CORE_GC])) ? spb_get_sn(ps_open[FTL_CORE_GC]) : spb_get_sn(ps_open[FTL_CORE_NRM]);
				gFtlMgr.pbt_host_blk = ps_open[FTL_CORE_NRM];
				gFtlMgr.pbt_gc_blk = ps_open[FTL_CORE_GC];
			}
		    else if(ps_open[FTL_CORE_NRM] != INV_U16)
		    {
		        gFtlMgr.TableSN = spb_get_sn(ps_open[FTL_CORE_NRM]);
				gFtlMgr.pbt_host_blk = ps_open[FTL_CORE_NRM];
		    }
		    else if(ps_open[FTL_CORE_GC] != INV_U16)
		    {
		        gFtlMgr.TableSN = spb_get_sn(ps_open[FTL_CORE_GC]);
				gFtlMgr.pbt_gc_blk = ps_open[FTL_CORE_GC];
		    }
		    else
		    {
		        gFtlMgr.TableSN = gFtlMgr.SerialNumber;
		    }
			gFtlMgr.pbt_pre_host_blk = host_spb_pre_idx;
			ftl_apl_trace(LOG_INFO, 0xbbf1, "pbt:%d, TableSN set:0x%x, openHOST:%d, openGC:%d",appl->spb_id,gFtlMgr.TableSN,ps_open[FTL_CORE_NRM],ps_open[FTL_CORE_GC]);
			ftl_apl_trace(LOG_INFO, 0xbf98, "pbt_host:%d, pbt_gc:%d, preHOST:%d",gFtlMgr.pbt_host_blk, gFtlMgr.pbt_gc_blk, gFtlMgr.pbt_pre_host_blk);
		}
		//ftl_ns_qbf_clear(appl->ns_id,FTL_CORE_PBT);
	}

notify_abort_end:
	sys_assert(appl->flags.b.ready);

	ns->pools[appl->pool_id].allocated++;
	ent.all = appl->spb_id;
	ent.b.sn = spb_get_sn(appl->spb_id);
	ent.b.slc = appl->flags.b.slc | appl->flags.b.dslc;
	if(appl->flags.b.abort == true){
		ent.b.ps_abort = true;
	}
	CBF_INS(ns->spb_queue[appl->type], ret, ent.all);
	sys_assert(ret);
	appl->spb_id = INV_SPB_ID;
	appl->flags.b.ready = 0;
	appl->flags.b.allocating = 0;
	appl->flags.b.abort = 0;

	if (ns->flags.b.spb_queried & (1 << appl->type))
		ns->flags.b.spb_queried &= ~(1 << appl->type);

	ipc_api_spb_ack(appl->ns_id, appl->type);

	return true;
}

fast_code void ftl_ns_add_ready_spb(u32 nsid, u32 type, spb_id_t spb_id)
{
	bool ret;
	spb_ent_t spb_ent;
	ftl_ns_t *ns = &ftl_ns[nsid];

	spb_ent.all = 0;
	spb_ent.b.spb_id = spb_id;
	spb_ent.b.slc = is_spb_slc(spb_id);
	spb_ent.b.sn = spb_get_sn(spb_id);
	CBF_INS(ns->spb_queue[type], ret, spb_ent.all);
	sys_assert(ret == true);
}

fast_code void ftl_ns_spb_trigger(u32 nsid, u32 type)
{
    ftl_ns_t *ns = &ftl_ns[nsid];
    spb_appl_t *appl = &_spb_appl[nsid][type];

    sys_assert(appl->flags.b.ready == 0);
    if (appl->flags.b.triggered)
        return;

    appl->flags.b.slc = 0;
    appl->flags.b.dslc = 0;
    appl->flags.b.native = 0;

    if (ns->attr.b.slc)
        appl->flags.b.slc = 1;
    else
        appl->flags.b.native = 1;

    if(nsid == FTL_NS_ID_INTERNAL)
    {
    	if(type == FTL_CORE_NRM)
		{
		    appl->flags.b.QBT = 1; // strong flag
		    ftl_l2p_tbl_flush_all_notify();
		    goto skip_gc_trigger;
		}
		else if(host_idle && (spb_get_free_cnt(SPB_POOL_FREE) >= GC_BLKCNT_EMER))
			goto skip_gc_trigger;
    }

	//pbt is from free pool so it should set gc mode
    GC_Mode_Assert();
    ftl_ns_gc_start_chk(FTL_NS_ID_START);

    if(nsid == FTL_NS_ID_START && type == FTL_CORE_GC && gc_get_mode() == GC_MD_STATIC_WL && global_gc_mode != GC_MD_EMGR)
    {
        appl->flags.b.older = 1;
        if(get_gc_blk() != pre_wl_blk)
            gc_suspend_start_wl = true;
    }

skip_gc_trigger:
    spb_allocation_trigger(appl);
}

fast_code void ftl_ns_upd_gc_perf(u32 decrease_percent)
{
	#if 0
	ftl_ns_t *ns = &ftl_ns[nsid];
	bool ret;
	u32 avg;
	u32 cur;
	u32 new_gc_perf;
	u32 free = spb_get_free_cnt(SPB_POOL_FREE);
	ret = get_gc_perf(&avg, &cur);
	if (ret == false) {
		avg = MAX_GC_PERF;
		cur = MAX_GC_PERF;
	}

	//u32 pool_id = ns->attr.b.mix ? SPB_POOL_MIX : SPB_POOL_NATIVE;
	//tzu maybe not use
	/*
	u32 pool_id = SPB_POOL_FREE;
	ftl_ns_pool_t *ns_pool = &ns->pools[pool_id];
	if (free <= GC_BLKCNT_ACTIVE && ns_pool->flags.b.gcing == false)
		ns_pool->flags.b.gcing = true;

	if (ns_pool->flags.b.gcing && free > GC_BLKCNT_DEACTIVE)
		ns_pool->flags.b.gcing = false;
	*/


	if (read_only_flags.b.no_free_blk && free > GC_BLKCNT_READONLY)
	{
		flush_to_nand(EVT_READ_ONLY_MODE_OUT);
	    read_only_flags.b.no_free_blk = 0;
<<<<<<< HEAD
		//if(cur_ro_status == true)
		//{
		//	cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
		//}
=======
		if(cur_ro_status == RO_MD_IN)
		{
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
		}
>>>>>>> master
	}

    if (free >= GC_BLKCNT_ACTIVE)//20
    {
		new_gc_perf = ~0;		// max performance
    }
	else if(free >GC_BLKCNT_EMER)//19~13
	{
		new_gc_perf = min(avg, cur);
	}
	else if (free > GC_BLKCNT_DISKFULL)//12~8
	{
		new_gc_perf = min(avg, cur);	// should be equal to GC performance
		new_gc_perf *= (100-((13-free)*10));
        new_gc_perf /= 100;
	}
	else if (free > GC_BLKCNT_READONLY)//8~5
	{
		new_gc_perf = MIN_GC_PERF;	// fixed minimal speed
	}
	else//4321
	{
		new_gc_perf = 0;		// zero performance
		read_only_flags.b.no_free_blk = 1;
<<<<<<< HEAD
		//if(cur_ro_status == false)
		//{
		//	cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
		//}
        //flush_to_nand(EVT_READ_ONLY_MODE_IN);
=======
		if(cur_ro_status != RO_MD_IN)
		{
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
		}
        flush_to_nand(EVT_READ_ONLY_MODE_IN);
>>>>>>> master
	}

	if(decrease_percent != 100)
	{
		new_gc_perf*=decrease_percent;
		new_gc_perf/=100;
	}

	if (new_gc_perf != shr_gc_perf)
	{
		ftl_apl_trace(LOG_ALW, 0x063c, "new gc perf %d", new_gc_perf);
		shr_gc_perf = new_gc_perf;
	}
	#endif
}
share_data_zi volatile bool in_gc_end;
share_data_zi volatile u8 STOP_BG_GC_flag;
fast_code void ftl_ns_gc_start_chk(u32 nsid)
{
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
	extern volatile u8 power_on_update_epm_flag;
	if(power_on_update_epm_flag == POWER_ON_EPM_UPDATE_ENABLE)
	{
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_INIT_DONE_SAVE_EPM, 0, 1); 
	}
#endif

	spb_id_t can = INV_SPB_ID;
	// adjust priority if weak spb was more danger than timeout
    if (gc_suspend_stop_next_spb == false)
    {
        if((gc_busy() == false))
        {
        	if(otf_forcepbt){
				u16 empty_cnt = spb_get_free_cnt(SPB_POOL_EMPTY);
				ftl_apl_trace(LOG_INFO, 0x6849, "OTF_free_empty:%d", empty_cnt);
				//if(empty_cnt){
				if(1){
					return;
				}
        	}

            #if (RD_NO_OPEN_BLK == mENABLE)
            if(get_open_gc_blk_rd())
            {
                gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

                action->act = GC_ACT_SUSPEND;
                action->caller = NULL;
                action->cmpl = NULL;

                if (gc_action(action))
                {
                sys_free(FAST_DATA, action);
                }
                if(ps_open[FTL_CORE_GC] < get_total_spb_cnt())
                {
                    spb_id_t gc_open = ps_open[FTL_CORE_GC];
                    sys_assert(rd_open_close_spb[FTL_CORE_GC] == INV_SPB_ID);
                    rd_open_close_spb[FTL_CORE_GC] = gc_open;
            	    spb_set_flag(gc_open, SPB_DESC_F_OPEN);
                    ftl_ns_close_open(spb_get_nsid(gc_open), gc_open, FILL_TYPE_RD_OPEN);	// ISU, LJ1-337, PgFalClsNotDone (1)
                }
                set_open_gc_blk_rd(false);
            }
            else if(GC_MODE_CHECK)
            #else
            if(GC_MODE_CHECK)
            #endif
            {
                //if (GC_MODE_CHECK & (GC_EMGR | GC_LOCK))  // free block too few, do not slow down GC speed
            	if (spb_get_free_cnt(SPB_POOL_FREE) < GC_BLKCNT_EMER_SPD_CTL)  // free block too few, do not slow down GC speed
					shr_gc_no_slow_down = true;
				else
					shr_gc_no_slow_down = false;
				
               ftl_apl_trace(LOG_INFO, 0xef15, "GC_MODE_CHECK 0x%x", GC_MODE_CHECK);

                can = GC_FSM_Blk_Select(nsid, SPB_POOL_USER);
                //ftl_ns_upd_gc_perf(nsid, 100);
                if (can != INV_SPB_ID)
                {
            		gc_start(can, nsid, min_vac, NULL);
                }
            }
        }
    }
    #ifdef STOP_BG_GC
    else if(STOP_BG_GC_flag && in_gc_end)
    {
        gc_suspend_stop_next_spb = false;
        STOP_BG_GC_flag = 0;
        shr_gc_fc_ctrl = 2;
        _fc_credit = 0;
        global_gc_mode = 0; // ISU, GCRdFalClrWeak
    }
    #endif
    else
    {
    	extern volatile u8 plp_trigger;
    	if(!plp_trigger)
    	{
			#ifdef STOP_BG_GC
			ftl_apl_trace(LOG_INFO, 0x7dc8, "gc_suspend_stop_next_spb %d STOP_BG_GC_flag %d in_gc_end %d",
			    gc_suspend_stop_next_spb, STOP_BG_GC_flag, in_gc_end);
			#else
			ftl_apl_trace(LOG_INFO, 0xaec0, "gc_suspend_stop_next_spb %d STOP_BG_GC_flag %d in_gc_end %d",
			    gc_suspend_stop_next_spb, 0, 0);
			#endif

    	}

    }
}

init_code void ftl_ns_calc_cap_min_max(u32 nsid, u32 *min_spb_cnt, u32 *max_spb_cnt, op_per_t min_spare_ratio, op_per_t max_spare_ratio, u32 mu_ratio)
{
	u32 spb_size_in_du;

	spb_size_in_du = ftl_ns[nsid].attr.b.native ? get_du_cnt_in_native_spb() : get_du_cnt_in_slc_spb();
	// refine
#if RAID_SUPPORT == 1
	if (nsid == 1) {
		spb_size_in_du *= shr_nand_info.lun_num - 1;
		spb_size_in_du /= shr_nand_info.lun_num;
	}
#endif

	ftl_ns[nsid].capacity_in_spb = occupied_by(ftl_ns[nsid].capacity * mu_ratio, spb_size_in_du);

	spb_size_in_du = spare_op(min_spare_ratio, spb_size_in_du);
	ftl_apl_trace(LOG_ERR, 0x039d, ">>> %d \n", spb_size_in_du);
	*min_spb_cnt = occupied_by(ftl_ns[nsid].capacity * mu_ratio, spb_size_in_du);

	spb_size_in_du = ftl_ns[nsid].attr.b.native ? get_du_cnt_in_native_spb() : get_du_cnt_in_slc_spb();
#if RAID_SUPPORT == 1
	if (nsid == 1) {
		spb_size_in_du *= shr_nand_info.lun_num - 1;
		spb_size_in_du /= shr_nand_info.lun_num;
	}
#endif
	spb_size_in_du = spare_op(max_spare_ratio, spb_size_in_du);
	*max_spb_cnt = occupied_by(ftl_ns[nsid].capacity * mu_ratio, spb_size_in_du);
}

fast_code void ftl_ns_spb_rel(u32 nsid, u32 pool_id)
{
	if (pool_id == SPB_POOL_USER)
	{
		ftl_ns[nsid].pools[SPB_POOL_FREE].allocated--;
		ftl_ns_upd_gc_perf(100);
	}
	else
	{
		ftl_ns[nsid].pools[pool_id].allocated--;
	}
}

fast_code ftl_ns_t *ftl_ns_get(u32 nsid)
{
	return &ftl_ns[nsid];
}
fast_code u16 get_gcinfo_shuttle_cnt(void)
{
	return gGCInfo.shuttleCnt;
}

fast_code u16 get_gcinfo_rddisturb_cnt(void)
{
	return gGCInfo.rdDisturbCnt;
}

fast_code u16 get_gcinfo_patrolrd_cnt(void)
{
	return gGCInfo.patrlrdCnt;
}

fast_code u16 set_gcinfo_patrolrd_cnt(void)
{
	return gGCInfo.patrlrdCnt--;
}

fast_code u16 set_gcinfo_shuttle_cnt(void)
{
	return gGCInfo.shuttleCnt--;
}

fast_code u16 set_gcinfo_rddisturb_cnt(void)
{
	return gGCInfo.rdDisturbCnt--;
}

init_code void ftl_ns_desc_init(sys_log_desc_t *desc)
{
	memset(desc->ptr, 0, desc->total_size);

	ftl_ns_desc_t *ftl_ns_desc = (ftl_ns_desc_t *) desc->ptr;

	ftl_ns_desc->flags.all = 0;
	ftl_ns_desc->flags.b.virgin = 1;
	ftl_ns_desc->spb_sn = 0;

	memset(ftl_ns_desc->spb_queue, 0xff, sizeof(ftl_ns_desc->spb_queue));
}

/*!
 * @brief callback function of ftl namespace descriptor flush
 *
 * @param ctx		system log descriptor of ftl namespace descriptor
 *
 * @return		not used
 */
fast_code void ftl_ns_desc_flush_cmpl(sld_flush_t *ctx)
{
	ftl_ns_t *this_ns = container_of(ctx, ftl_ns_t, sld_flush);

	this_ns->flags.b.desc_flushing = 0;

	if (ctx->caller_cmpl)
		ctx->caller_cmpl(ctx->caller);
}

fast_code bool ftl_ns_make_clean(u32 nsid, void *caller, completion_t caller_cmpl)
{
	ftl_ns_t *this_ns = &ftl_ns[nsid];

	ftl_apl_trace(LOG_INFO, 0x4bef, "make ns %d clean, now %d", nsid,
			this_ns->ftl_ns_desc->flags.b.clean);
	if (this_ns->ftl_ns_desc->flags.b.clean)
		return true;

	this_ns->ftl_ns_desc->flags.b.clean = 1;
	ftl_apl_trace(LOG_ERR, 0xa4bf, "make clean bit 1");
	return true;
	/*
	this_ns->sld_flush.caller = caller;
	this_ns->sld_flush.caller_cmpl = caller_cmpl;
	this_ns->sld_flush.cmpl = ftl_ns_desc_flush_cmpl;
	this_ns->flags.b.desc_flushing = 1;
	sys_log_desc_flush(&this_ns->ns_log_desc, 0, &this_ns->sld_flush);
	return false;
	*/
}

fast_code bool ftl_ns_make_dirty(u32 nsid, void *caller, completion_t caller_cmpl, bool force)
{
	ftl_ns_t *this_ns = &ftl_ns[nsid];

	ftl_apl_trace(LOG_INFO, 0x8322, "make ns %d dirty, now %d", nsid,
			this_ns->ftl_ns_desc->flags.b.clean);

	if (this_ns->ftl_ns_desc->flags.b.clean == 0 && force == false)
		return true;

	this_ns->ftl_ns_desc->flags.b.clean = 0;
	this_ns->sld_flush.caller = caller;
	this_ns->sld_flush.caller_cmpl = caller_cmpl;
	this_ns->sld_flush.caller_cmpl(caller); // foward to complete
    ftl_apl_trace(LOG_ERR, 0x1b44, "make clean bit 0");
	return false;
	/*
	if (this_ns->flags.b.desc_flushing)
		return false;

	this_ns->ftl_ns_desc->flags.b.clean = 0;
	this_ns->sld_flush.caller = caller;
	this_ns->sld_flush.caller_cmpl = caller_cmpl;
	this_ns->sld_flush.cmpl = ftl_ns_desc_flush_cmpl;
	this_ns->flags.b.desc_flushing = 1;
	sys_log_desc_flush(&this_ns->ns_log_desc, 0, &this_ns->sld_flush);
	return false;
	*/
}

fast_code bool ftl_ns_flush_desc(u32 nsid, void *caller, completion_t caller_cmpl)
{
	ftl_ns_t *this_ns = &ftl_ns[nsid];

	this_ns->sld_flush.caller = caller;
	this_ns->sld_flush.caller_cmpl = caller_cmpl;
	this_ns->sld_flush.cmpl = ftl_ns_desc_flush_cmpl;
	this_ns->flags.b.desc_flushing = 1;
	sys_log_desc_flush(&this_ns->ns_log_desc, 0, &this_ns->sld_flush);
	return false;
}


fast_code void ftl_ns_format(u32 nsid, bool host_meta)
{
	ftl_ns_t *ns = &ftl_ns[nsid];

	if (nsid == FTL_NS_ID_INTERNAL)
		ftl_int_ns_format(ns);
	else
		ftl_host_ns_format(ns, nsid, host_meta);
}

fast_code void force_pad_spb_done(ftl_core_ctx_t *ctx)
{
	spb_id_t spb_id = (spb_id_t) (u32) ctx->caller;
    u32 *vc;
    dtag_t dtag;
    u32 dtag_cnt;
	u32 vac;
	u8 gc_mode = 0;

	spb_clr_sw_flag(spb_id, SPB_SW_F_PADDING);
	ftl_apl_trace(LOG_INFO, 0xb8da, "force close spb %d done", spb_id);
	sys_free(FAST_DATA, ctx);
    
    ftl_apl_trace(LOG_ALW, 0x8a2a, "spb %d rd dummy done rd_open_close_spb %d %d",
        spb_id, rd_open_close_spb[FTL_CORE_NRM], rd_open_close_spb[FTL_CORE_GC]);
    if(rd_open_close_spb[FTL_CORE_NRM] == spb_id || rd_open_close_spb[FTL_CORE_GC] == spb_id)
    {
        
        spb_set_flag(spb_id, SPB_DESC_F_NO_NEED_CLOSE);
        ftl_apl_trace(LOG_ALW, 0x2b7f, "spb %d rd dummy done rd_open_close_spb %d %d pool_id %d",
            spb_id, rd_open_close_spb[FTL_CORE_NRM], rd_open_close_spb[FTL_CORE_GC], spb_get_poolid(spb_id));
        if(spb_get_poolid(spb_id) == SPB_POOL_FREE){
            FTL_BlockPushList(SPB_POOL_USER, spb_id, FTL_SORT_NONE);
        }

        ftl_apl_trace(LOG_INFO, 0x4622, "spb %u need force PBT",spb_id);
        cpu_msg_issue(1, CPU_MSG_FORCE_PBT, 0, 4);
        if(rd_open_close_spb[FTL_CORE_NRM] == spb_id){
            rd_open_close_spb[FTL_CORE_NRM] = INV_SPB_ID;
        }else if(rd_open_close_spb[FTL_CORE_GC] == spb_id){
            rd_open_close_spb[FTL_CORE_GC] = INV_SPB_ID;
        }else{
            sys_assert(0);
        }

#if (RD_NO_OPEN_BLK == mENABLE)
        //	ftl_apl_trace(LOG_INFO, 0, "spb_id: %d, shr_dummy_blk: %d", spb_id, shr_dummy_blk);
        //	ftl_apl_trace(LOG_INFO, 0, "host open: %d, gc open: %d", ps_open[FTL_CORE_NRM], ps_open[FTL_CORE_GC]);
        if (spb_id == shr_dummy_blk)
        {
//        ftl_apl_trace(LOG_INFO, 0, "spb_id: %d, shr_dummy_blk: %d", spb_id, shr_dummy_blk);
//    	ftl_apl_trace(LOG_INFO, 0, "host open: %d, gc open: %d", ps_open[FTL_CORE_NRM], ps_open[FTL_CORE_GC]);
            gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

            action->act = GC_ACT_RESUME;
            action->caller = NULL;
            action->cmpl = NULL;

            if (gc_action(action))
            {
            sys_free(FAST_DATA, action);
            }
            shr_dummy_blk = INV_SPB_ID;
        }
#endif
        return;
    }

    FTL_BlockPopPushList(SPB_POOL_GC, spb_id, FTL_SORT_NONE);
	if(spb_get_flag(spb_id) & SPB_DESC_F_WEAK)
	{
		#ifdef EH_FORCE_CLOSE_SPB
		if (!(spb_get_flag(spb_id) & SPB_DESC_F_CLOSED)) // ISU, SetGCSuspFlag
			spb_set_flag(spb_id, SPB_DESC_F_OPEN);	// Notice GC this spb is force closed, ISU, Tx, PgFalClsNotDone (1)
		#endif
		//cpu_msg_issue(CPU_BE - 1, CPU_MSG_DEL_FAILBLK, 0, (u32)spb_id);	// Release FailBlkInfo when push to GC, ISU, TSSP, PushGCRelsFalInfo	// ISU, BakupPBTLat
		MK_FailBlk_Info failBlkInfo;
		failBlkInfo.all = 0;
		failBlkInfo.b.wBlkIdx = spb_id;
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_DEL_FAILBLK, 0, failBlkInfo.all);
		Set_gc_shuttleCnt();
		Set_gc_shuttle_mode();
	}
	else if(spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST)
	{
		Set_gc_RdDisturb_mode();
	}
    ftl_apl_trace(LOG_INFO, 0x4f7e, "spb_id: %d, nsid: %d, mode: 0x%x",spb_id,spb_get_nsid(spb_id), GC_MODE_CHECK);

    if((gc_busy() == false) && (gc_suspend_stop_next_spb == false))
    {
        dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
        if(spb_get_flag(spb_id) & SPB_DESC_F_WEAK)
        {
        	rd_gc_flag = true; // ISU, EHPerfImprove
			gc_mode = GC_MD_SHUTTLE;
        }
        else if(spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST)
        {
        	if(!(spb_get_flag(spb_id) & SPB_DESC_F_OVER_TEMP_GC))
				tx_smart_stat->rd_count++;
			gc_mode = GC_MD_READDISTURB;
			rd_gc_flag = true;
			ftl_apl_trace(LOG_INFO, 0x256f, "[GC]RD mode, rd_cnt:%d rd_gc_flag: %d", tx_smart_stat->rd_count, rd_gc_flag);
        }
		vac = vc[spb_id];
//        ftl_apl_trace(LOG_INFO, 0, "SPB_DESC_F_WEAK: %d, SPB_DESC_F_RD_DIST: %d",(spb_get_flag(spb_id) & SPB_DESC_F_WEAK),(spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST));
		ftl_apl_trace(LOG_INFO, 0x22d9, "[GC]Blk_Select:MD[0x%x]Blk[%d]VAC[%d]EC[%d]", gc_mode, spb_id, vc[spb_id], spb_info_get(spb_id)->erase_cnt);
        ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
        sys_assert(spb_id != INV_SPB_ID);
        FTL_BlockPopPushList(SPB_POOL_GCD, spb_id, FTL_SORT_NONE);

		global_gc_mode = gc_mode;	// ISU, GCRdFalClrWeak
        gc_start(spb_id, spb_get_nsid(spb_id), vac, NULL);
    }
	return;
}

ddr_code void ftl_ns_dr_gc(u32 nsid, spb_id_t spb_id)
{
	u16 flags;

    u32 tar_vc = 0xffffffff;
    u32 *vc;
    dtag_t dtag;
    u32 dtag_cnt;

	if (nsid == FTL_NS_ID_INTERNAL)
		return;

	flags = spb_get_flag(spb_id);
	if (flags & SPB_DESC_F_GCED)
		return;

	if (((flags & SPB_DESC_F_CLOSED) || (flags & SPB_DESC_F_NO_NEED_CLOSE)) && (gc_suspend_stop_next_spb == false)) {
        FTL_BlockPopPushList(SPB_POOL_GCD, spb_id, FTL_SORT_NONE);
        dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
        tar_vc = vc[spb_id];
        ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
        ftl_apl_trace(LOG_INFO, 0x978f, "[GC]Blk_Select:MD[0x%x]Blk[%d]VAC[%d]EC[%d]", GC_MD_DATARETENTION, spb_id, tar_vc, spb_info_get(spb_id)->erase_cnt);
        ftl_apl_trace(LOG_INFO, 0x815b, "DATA RETENTION START");
		tx_smart_stat->dr_count++;
		rd_gc_flag = true;             //adams
		global_gc_mode = GC_MD_DATARETENTION;	// ISU, GCRdFalClrWeak
        gc_start(spb_id, nsid, tar_vc, NULL);
    }


}

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
ddr_code void set_epm_shuttle_gc_blk_sn(spb_id_t spb_id, u32 blk_sn)
{
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	if( epm_ftl_data->max_shuttle_gc_blk_sn < blk_sn )
	{
		epm_ftl_data->max_shuttle_gc_blk_sn = blk_sn;
		epm_ftl_data->max_shuttle_gc_blk = spb_id;
		ftl_apl_trace(LOG_ERR, 0xde4c, "record to epm -> spb :%d sn:0x%x preTableSN:0x%x",spb_id,blk_sn,gFtlMgr.PrevTableSN );
		epm_update(FTL_sign , CPU_ID - 1);
	}
}
#endif

//fast_code void ftl_ns_weak_spb_gc(u32 nsid, spb_id_t spb_id)	// FET, PfmtHdlGL
ddr_code void ftl_ns_weak_spb_gc(u32 nsid, spb_id_t spb_id)
{
	dtag_t dtag;
	ftl_ns_t *ns = &ftl_ns[nsid];	
    u32 *vc;
	u32 tar_vc = 0xffffffff;
	u32 dtag_cnt;
	u16 flags;
	u16 sw_flags;
    u8 show_mode = 0;	
	u8 pool_id = spb_get_poolid(spb_id);	// ISU, LJ1-242
	
	if (nsid == FTL_NS_ID_INTERNAL)
		return;

	flags = spb_get_flag(spb_id);
	
	// DBG, PgFalVry (3)
	// Cannot judge vac == 0 here, some vac may be OTF. Let close blk do its job.
	//dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
	//tar_vc = vc[spb_id];
	//ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
	//if(tar_vc == 0)
	//{
	//	ftl_apl_trace(LOG_INFO, 0, "vac is zero spb %d pool %d flags %d",spb_id,spb_get_poolid(spb_id),flags);
	//	//spb_release(spb_id);
	//	return;
	//}
	
	//if (flags & SPB_DESC_F_GCED)	// ISU, LJ1-242
	// In GC : shuttle cnt had been recorded already.
	// In GCD: need not to trigger shuttle GC then.
	if ((flags & SPB_DESC_F_GCED) || (pool_id == SPB_POOL_GC) || (pool_id == SPB_POOL_GCD))
		return;

    if(spb_get_flag(spb_id) & SPB_DESC_F_WEAK)
    {
        Set_gc_shuttleCnt();
		#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
        set_epm_shuttle_gc_blk_sn(spb_id,spb_get_sn(spb_id));
        #endif
    }
	else if(spb_get_flag(spb_id) & SPB_DESC_F_PATOAL_RD)
	{
		Set_gc_patorlrdCnt(1);
	}
    else if(spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST)
    {
        Set_gc_rdDisturbCnt(1);
    }

	if ((flags & SPB_DESC_F_CLOSED) || (flags & SPB_DESC_F_NO_NEED_CLOSE)) 
    {
		sw_flags = spb_get_sw_flag(spb_id);
		if (sw_flags & SPB_SW_F_FREEZE) {
			ns->flags.b.weak_spb_gc = 1;
			return;
		}

		// DBG, PgFalVry (3)
		dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
		tar_vc = vc[spb_id];
		ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
		if(tar_vc == 0)
		{
			ftl_apl_trace(LOG_INFO, 0xf905, "vac is zero spb %d pool %d flags %d", spb_id, spb_get_poolid(spb_id), flags);

			if(spb_get_flag(spb_id) & SPB_DESC_F_WEAK)
	        {
	            if(get_gcinfo_shuttle_cnt())
	            {
	                set_gcinfo_shuttle_cnt();
	            }
	            spb_clear_flag(spb_id, SPB_DESC_F_WEAK);
				spb_set_sw_flag(spb_id, SPB_SW_F_SHUTTLE_FREE);
	            ftl_apl_trace(LOG_INFO, 0x9695, "clear SPB_DESC_F_WEAK GC[%d]flag: %d, shuttle cnt: %d sw flag 0x%x", spb_id, spb_get_flag(spb_id), get_gcinfo_shuttle_cnt(), spb_get_sw_flag(spb_id));
				spb_release(spb_id);

				#ifdef ERRHANDLE_GLIST  
			    // Wake up ErrHandle_Task to mark bad BMP.
				// ISU, TSSP, PushGCRelsFalInfo
				MK_FailBlk_Info failBlkInfo;
				failBlkInfo.all 	  = 0;
				failBlkInfo.b.wBlkIdx = spb_id;
				failBlkInfo.b.need_gc = false;
				ErrHandle_Task(failBlkInfo);
				#if 0
				u8 bIdx = 0;
				if (bFail_Blk_Cnt != 0)
				{
					for (bIdx = 0; bIdx < MAX_FAIL_BLK_CNT; bIdx++)
					{
						if (spb_id == Fail_Blk_Info[bIdx].b.wBlkIdx)
						{
							Fail_Blk_Info[bIdx].b.need_gc = false;
							ErrHandle_Task(bIdx);
						}
					}
				}
				#endif
				#endif
			}
			else
			{
				gc_clear_mode(spb_id);
				spb_release(spb_id);				
			}
			return;
		}
		if((spb_get_flag(spb_id) & SPB_DESC_F_WEAK) || (spb_get_flag(spb_id) & SPB_DESC_F_PATOAL_RD))
		{
			Set_gc_shuttle_mode();
		}
		else if(spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST)
		{
			Set_gc_RdDisturb_mode();
		}

        if(gc_busy() == false && (gc_suspend_stop_next_spb == false)&&(delay_flush_spor_qbt == false))
        {
            if((spb_get_flag(spb_id) & SPB_DESC_F_WEAK) || (spb_get_flag(spb_id) & SPB_DESC_F_PATOAL_RD))
            {
				if(spb_get_flag(spb_id) & SPB_DESC_F_PATOAL_RD)
				{
					tx_smart_stat->dr_count++;
					//rd_gc_flag = false;	// ISU, EHPerfImprove
				}
				//else // ISU, EHPerfImprove
				//{
					rd_gc_flag = true;
				//}
                show_mode = GC_MD_SHUTTLE;
            }
            else if(spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST)
            {
            	if(!(flags & SPB_DESC_F_OVER_TEMP_GC))
                	tx_smart_stat->rd_count++;
//				ftl_apl_trace(LOG_INFO, 0, "[GC]rd_cnt: %d", rd_cnt);
                show_mode = GC_MD_READDISTURB;
				rd_gc_flag = true;
			    ftl_apl_trace(LOG_INFO, 0x8b5d, "[GC]rd_cnt:%d rd_gc_flag: %d", tx_smart_stat->rd_count, rd_gc_flag);
            }
            FTL_BlockPopPushList(SPB_POOL_GCD, spb_id, FTL_SORT_NONE);
            ftl_apl_trace(LOG_INFO, 0xfdb9, "[GCD]spb_id: %d, pool: %d, flags: %d", spb_id, spb_info_tbl[spb_id].pool_id, flags);

            // todo: reduce this function call, it need time to backup valid count to buffer
            ftl_apl_trace(LOG_INFO, 0x1fc2, "[GC]Blk_Select:MD[0x%x]Blk[%d]VAC[%d]EC[%d]", show_mode, spb_id, tar_vc, spb_info_get(spb_id)->erase_cnt);

			global_gc_mode = show_mode;	// ISU, GCRdFalClrWeak
            gc_start(spb_id, nsid, tar_vc, NULL);
        }
        else
        {
            FTL_BlockPopPushList(SPB_POOL_GC, spb_id, FTL_SORT_NONE);
            ftl_apl_trace(LOG_INFO, 0xd357, "[GC]spb_id: %d, pool: %d, flags: %d", spb_id, spb_info_tbl[spb_id].pool_id, flags);
        }
	}
    else if ((spb_get_sw_flag(spb_id) & SPB_SW_F_PADDING) == 0)
    {

        #if (RD_NO_OPEN_BLK == mENABLE)
        //if(spb_id == ps_open[FTL_CORE_GC] && gc_busy() == true)	// ISU, TSSP, PushGCRelsFalInfo
        if((spb_id == ps_open[FTL_CORE_GC]) && (gc_busy() == true) && (spb_get_flag(spb_id) & SPB_DESC_F_RD_DIST))
        {
            ftl_apl_trace(LOG_INFO, 0xfbfa, "[GC]set_open_gc_blk_rd spb[%d]", spb_id);
            shr_dummy_blk = spb_id;
            set_open_gc_blk_rd(true);
        }
        else
        #endif
        {
        	// ISU, LJ1-337, PgFalClsNotDone (1)
        	#ifdef EH_FORCE_CLOSE_SPB
        	if (spb_get_flag(spb_id) & SPB_DESC_F_WEAK)	// Request by error handle.
        	{
				// ISU, EH_GC_not_done
				#if 0//ndef EH_GCOPEN_HALT_DATA_IN	// Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
				if ((spb_id == ps_open[FTL_CORE_GC]))	// WA, gc openBlk will suspend first.
					ftl_ns_close_open(nsid, spb_id, FILL_TYPE_BLK);
				else
				#endif
                    set_gcinfo_shuttle_cnt();
	            	ftl_ns_close_open(nsid, spb_id, FILL_TYPE_WL);	// DBG, ISU, LJ1-337, PgFalClsNotDone (1)
	            	//ftl_ns_close_open(nsid, spb_id, FILL_TYPE_LAYER);
        	}	
            else
			#endif	
				ftl_ns_close_open(nsid, spb_id, FILL_TYPE_BLK);
        }
	}
}


fast_code void ftl_ns_close_open(u32 nsid, spb_id_t spb_id, u8 fill_type)	// ISU, LJ1-337, PgFalClsNotDone (1)
{
    ftl_spb_pad_t *spb_pad = sys_malloc(FAST_DATA, sizeof(ftl_spb_pad_t));

		sys_assert(spb_pad);

		//ERRHANDLE_GLIST
		//TBC_EH, should be removed?
		// Fix kernel crash, init vars here?
		/*
		memset(spb_pad, 0, sizeof(ftl_spb_pad_t));
		spb_pad->param.cwl_cnt = 0;
		spb_pad->param.pad_all = true;
		spb_pad->param.type = FTL_CORE_GC;
		spb_pad->cur.cwl_cnt = 0;
    spb_pad->cur.type = FTL_CORE_NRM;
    */
    //	    ns->flags.b.weak_spb_gc = 1;

    spb_pad->spb_id = spb_id;
    spb_pad->ctx.caller = (void *) (u32) spb_id;
    spb_pad->ctx.cmpl = force_pad_spb_done;
    spb_pad->param.start_nsid = nsid; // 20200914 Curry
    spb_pad->param.end_nsid = nsid; // 20200914 Curry
    spb_pad->cur.nsid = nsid;

	switch (fill_type) {
	#ifdef EH_FORCE_CLOSE_SPB	
	case FILL_TYPE_WL:
		spb_pad->param.cwl_cnt = 2;
		spb_pad->param.pad_all = false;
		break;
	case FILL_TYPE_LAYER:	// For now no use, mark to save code size.
		spb_pad->param.cwl_cnt = 4;
		spb_pad->param.pad_all = false;
		break;
	#endif
	default: //case FILL_TYPE_BLK:
		spb_pad->param.cwl_cnt = 0;
		spb_pad->param.pad_all = true;
		break;
	}
	
    spb_set_sw_flag(spb_id, SPB_SW_F_PADDING);
    ftl_apl_trace(LOG_WARNING, 0x8943, "force close spb %d", spb_id);
    ftl_core_spb_pad(spb_pad);
}


fast_code void ftl_ns_dirty_spb_remove(u32 nsid)
{
	u32 i, j;
	spb_fence_t *spb_fence;
	spb_fence_t *valid_fence;
	ftl_ns_t *ns = &ftl_ns[nsid];
	ftl_fence_t *ffence = &ns->ftl_ns_desc->ffence;

	for (i = FTL_CORE_NRM; i < FTL_CORE_MAX; i++) {
		valid_fence = NULL;

		//get the latest fence
		for (j = 0; j < MAX_ASPB_IN_PS; j++) {
			spb_fence = &ffence->spb_fence[i][j];
			if (spb_fence->flags.b.valid == false)
				continue;

			if ((valid_fence == NULL) || (spb_fence->sn > valid_fence->sn))
				valid_fence = spb_fence;
		}

		//virgin
		if (valid_fence == NULL)
			continue;

		u32 sn;
		u32 flag;
		spb_id_t spb;
		spb_ent_t _spb_ent;

		while (!CBF_EMPTY(&ns->dirty_spb_que[i])) {
			CBF_HEAD(&ns->dirty_spb_que[i], _spb_ent.all);

			spb = _spb_ent.b.spb_id;
			sn = spb_get_sn(spb);
			flag = spb_get_flag(spb);

			if (sn > valid_fence->sn)
				break;

			//frozen spb can't be released
			sys_assert(sn == _spb_ent.b.sn);
			sys_assert(flag & SPB_DESC_F_BUSY);

			spb_clr_sw_flag(spb, SPB_SW_F_FREEZE);
			if (spb_get_sw_flag(spb) & SPB_SW_F_VC_ZERO)
				spb_release(spb);

			CBF_REMOVE_HEAD(&ns->dirty_spb_que[i]);
		}
	}
}

fast_code void spb_close_tbl_flush_done(flush_ctx_t *ctx)
{
	ftl_apl_trace(LOG_INFO, 0xb184, "runtime l2p tbl flush done");
	shr_tbl_flush_perf = ~0;
	sys_free(FAST_DATA, ctx);
}

fast_code void ftl_ns_inc_closed_spb(spb_id_t spb)
{
	u8 nsid = spb_get_nsid(spb);
	if (nsid != FTL_NS_ID_START)
		return;

	ftl_ns_t *ns = &ftl_ns[nsid];
	ns->closed_spb_cnt++;
	spb_set_sw_flag(spb, SPB_SW_F_FREEZE);

	bool ret;
	spb_ent_t spb_ent;
	u8 type = is_spb_gc_write(spb) ? FTL_CORE_GC : FTL_CORE_NRM;

	spb_ent.all = 0;
	spb_ent.b.spb_id = spb;
	spb_ent.b.slc = is_spb_slc(spb);
	spb_ent.b.sn = spb_get_sn(spb);
	CBF_INS(&ns->dirty_spb_que[type], ret, spb_ent.all);
	sys_assert(ret == true);

	if (ns->closed_spb_cnt < SPB_CNT_L2P_FLUSH)
		return;

	u32 avail;
	u32 dirty;

	cbf_avail_ins_sz(avail, (&ns->dirty_spb_que[type]));
	dirty = ns->dirty_spb_que[type].size - avail;
	if (dirty >= ((SPB_CNT_L2P_FLUSH << 1) - 1))
		shr_tbl_flush_perf = MIN_GC_PERF;
	else if (dirty >= ((SPB_CNT_L2P_FLUSH << 1) - 2))
			shr_tbl_flush_perf = MAX_GC_PERF;
	ftl_apl_trace(LOG_INFO, 0xb143, "close %d dirty %d tbl fc perf %d",
			ns->closed_spb_cnt, dirty, shr_tbl_flush_perf);

	if (ns->flags.b.flushing || ns->flags.b.reconing)
		return;

	ns->closed_spb_cnt = 0;
	flush_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(flush_ctx_t));
	sys_assert(ctx);

	ctx->flags.all = 0;
	ctx->flags.b.spb_close = 1;
	ctx->nsid = FTL_NS_ID_START;

	ctx->caller = ns;
	ctx->cmpl = spb_close_tbl_flush_done;
	ftl_apl_trace(LOG_INFO, 0x1942, "runtime l2p tbl flush begin");
	ftl_flush(ctx);
}

fast_code void ftl_hns_dirty_flush_done(flush_ctx_t *ctx)
{
	u32 nsid = ctx->nsid;

	ftl_apl_trace(LOG_INFO, 0x41c4, "dirty hns %d flush done", nsid);
	sys_free(FAST_DATA, ctx);

	//go to get next dirty hns
	evt_set_cs(evt_hns_dirty_flush, 0, 0, CS_TASK);
}

fast_code void ftl_hns_dirty_flush(u32 parm, u32 payload, u32 sts)
{
	u32 nsid;
	flush_ctx_t *ctx;

	nsid = find_first_bit(&hns_dirty_bmp, FTL_NS_ID_MAX);
	if (nsid < FTL_NS_ID_MAX) {
		clear_bit(nsid, &hns_dirty_bmp);

		ctx = sys_malloc(FAST_DATA, sizeof(flush_ctx_t));
		sys_assert(ctx);

		ctx->nsid = nsid;
		ctx->flags.all = 0;
		ctx->flags.b.spb_close = 1;

		ctx->caller = &ftl_ns[nsid];
		ctx->cmpl = ftl_hns_dirty_flush_done;
		//ftl_apl_trace(LOG_INFO, 0, "dirty hns %d flush begin", nsid);
		ftl_flush(ctx);
	} else {
	    //ftl_apl_trace(LOG_ALW, 0, "nsid[0x%x] > FTL_NS_ID_MAX", nsid);
		shr_ftl_flags.b.ftl_ready = true;
		spb_mgr_open_spb_gc();
	}
}

fast_code void ftl_ns_spb_retire(u32 nsid, u32 pool_id)
{
	//sys_assert(ftl_ns[nsid].pools[pool_id].quota);
	pool_id=SPB_POOL_FREE;//tzu workaround
	ftl_ns[nsid].pools[pool_id].quota -= 1;

	if (ftl_ns[nsid].pools[pool_id].quota <= ftl_ns[nsid].pools[pool_id].min_spb_cnt) {
		read_only_flags.b.spb_retire = 1;
		if(cur_ro_status != RO_MD_IN)
		{
			#if(degrade_mode == ENABLE)
				smart_stat->critical_warning.raw |= 0xD; //set bit[0]、bit[2]、bit[3]
			#endif
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
		}
        flush_to_nand(EVT_READ_ONLY_MODE_IN);
		ftl_apl_trace(LOG_ALW, 0xcf00, "DISK enter RO!!");
	}
}


fast_code bool ftl_ns_is_clean(u32 nsid)
{
	ftl_ns_t *this_ns = &ftl_ns[nsid];

	return this_ns->ftl_ns_desc->flags.b.clean ? true : false;
}

init_code void ftl_ns_post(void)
{
	ftl_err_t ins_err;
	u8 i;

	for (i = 1; i < FTL_NS_ID_END; i++){
		ftl_host_ns_post(i);
	}
	ins_err = ftl_int_ns_start();
	for (i = 1; i < FTL_NS_ID_END; i++) {
		ftl_host_ns_start(i, ins_err);
	}

	//if ((ins_err == FTL_ERR_CLEAN) || (ins_err == FTL_ERR_RECON_DONE))
		//ftl_int_ns_vc_restore();

#if (WARMBOOT_FTL_HANDLE == mENABLE)
if(_fg_warm_boot == true)
{

	#if (WA_FW_UPDATE == DISABLE)
	u16 blk;
	u32 dtag_cnt;
	u32 *vc;
	dtag_t dtag;
	u32 *ddr_vac_buffer = (u32*)ddtag2mem(shr_vac_drambuffer_start);
	u32 total_vldcnt_per_spb = get_du_cnt_in_native_spb();

    FTL_CopyFtlBlkDataFromBuffer(0);

	dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
	for(blk=0; blk<get_total_spb_cnt(); blk++)
	{
		vc[blk] = ddr_vac_buffer[blk];
		//ftl_apl_trace(LOG_WARNING, 0, "[FTL]blk:%d, svac:%d, dvac:%d", blk, vc[blk], ddr_vac_buffer[blk]);
		if(((spb_info_get(blk)->pool_id == SPB_POOL_USER) || (spb_info_get(blk)->pool_id == SPB_POOL_EMPTY) || (spb_info_get(blk)->pool_id == SPB_POOL_GC)) 
            && (vc[blk] == 0) && (spb_get_sn(blk) < gFtlMgr.PrevTableSN))
		{
			ftl_apl_trace(LOG_ALW, 0x4c28, "[FTL]blk:%d pool:%d vac zero not in free", blk, spb_info_get(blk)->pool_id);
			FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
			spb_set_sw_flags_zero(blk);
			spb_set_desc_flags_zero(blk);			
		}
		if((vc[blk]>total_vldcnt_per_spb) || (vc[blk]<0))
		{
			ftl_apl_trace(LOG_ALW, 0x8c78, "[FTL]Warmboot vac invalid blk[%d]:%d", blk, vc[blk]);
			sys_assert(0);
		}
	}
	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, true);
	ftl_apl_trace(LOG_ALW, 0xbcd4, "[FTL]Warmboot copy blklist,vac table from buf %d", _fg_warm_boot);
	
	ftl_ns_upd_gc_perf(100);
	#endif

	if(_fg_warm_boot == true)
	{	
        shr_qbt_loop = gFtlMgr.qbt_cur_loop%FTL_MAX_QBT_CNT;
        shr_qbt_loop = (shr_qbt_loop/QBT_BLK_CNT)*QBT_BLK_CNT;
        
		ftl_pbt_cnt = spb_get_free_cnt(SPB_POOL_PBT);
		shr_pbt_loop = gFtlMgr.pbt_cur_loop;
        if ((spb_get_free_cnt(SPB_POOL_PBT) % QBT_BLK_CNT != 0) || spb_get_free_cnt(SPB_POOL_PBT_ALLOC) > 0)
        {
            pbt_resume_param->pbt_info.flags.b.resume_pbt = true;
            if (spb_get_free_cnt(SPB_POOL_PBT_ALLOC) > 0)
                spb_set_sw_flag(spb_mgr_get_head(SPB_POOL_PBT_ALLOC), pbt_resume_param->sw_flag);

            ftl_apl_trace(LOG_ALW, 0xc521, "[OTF]ftl_pbt_cnt: %u, shr_pbt_loop: %u, pbt_blk: %u, flag: 0x%x, sw_flags: 0x%x", 
		        ftl_pbt_cnt, shr_pbt_loop, spb_mgr_get_head(SPB_POOL_PBT_ALLOC), spb_get_flag(spb_mgr_get_head(SPB_POOL_PBT_ALLOC)), spb_get_sw_flag(spb_mgr_get_head(SPB_POOL_PBT_ALLOC)));
        }
        else
        {
            pbt_resume_param->pbt_info.flags.b.resume_pbt = false;
        }
		/*if(shr_pbt_loop >= FTL_MAX_QBT_CNT)
		{
			//ftl_apl_trace(LOG_ALW, 0, "shr_pbt_loop invalid %d", shr_pbt_loop);
			shr_pbt_loop = 0;
		}
		ftl_pbt_idx = shr_pbt_loop;*/
	}
	//spb_blk_info();
	//spb_blk_list();
	//spb_ftl_mgr_info();
	#if (OTF_TIME_REDUCE == DISABLE)
		_fg_warm_boot = false;
	#endif
}
#endif

#if (SPOR_FLOW == mDISABLE)
#if (QBT_TLC_MODE == mENABLE)
	if(QBT_TAG == true)
	{
		FTL_CopyFtlBlkDataFromBuffer(0);
	}
#endif

#endif

	spb_mgr_sync_ns();
//	spb_poh_init();
//    Ec_tbl_update(INV_U16);
//	qbt_poweron_handle();
#if (SPOR_FLOW == mDISABLE)
	gFtlMgr.SerialNumber = find_max_sn();
#endif

	if(_fg_warm_boot == false)
	{
		epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		ftl_apl_trace(LOG_ERR, 0x72a1, "POR_tag 0x%x\n", epm_ftl_data->POR_tag);		
		if((epm_ftl_data->POR_tag != FTL_EPM_POR_TAG))
		{
			shr_ftl_flags.b.spor = true;  //for unsafe_shutdown count			
		}		
	}		
	
    local_item_done(l2p_init);
	wait_remote_item_done(cache_init);
}

fast_code void ftl_ns_dump(void)
{
	u32 nsid;

	for (nsid = FTL_NS_ID_START; nsid < FTL_NS_ID_MAX; nsid++) {
		ftl_ns_t *ns = &ftl_ns[nsid];
		u32 i;

		ftl_apl_trace(LOG_ALW, 0x5b59, "ns %d attr %x flag %x closed %d", nsid, ns->attr.all, ns->flags.all, ns->closed_spb_cnt);
		for (i = 0; i < FTL_CORE_MAX; i++) {
			if (ns->spb_queue[i])
				ftl_apl_trace(LOG_ALW, 0x838a, "spb que[%d] %d/%d", i, ns->spb_queue[i]->rptr, ns->spb_queue[i]->wptr);

			ftl_apl_trace(LOG_ALW, 0x5097, "dirty que[%d] %d/%d", i, ns->dirty_spb_que[i].rptr, ns->dirty_spb_que[i].wptr);
		}

		for (i = 0; i < SPB_POOL_MAX; i++) {
			ftl_ns_pool_t *pool = &ns->pools[i];

			ftl_apl_trace(LOG_ALW, 0xe9f8, "pool[%d] f %x a %d/%d", i, pool->flags.all, pool->allocated, pool->quota);
		}
	}
}

fast_code void ftl_ns_qbf_clear(u8 nsid, u8 type){
	ftl_ns_t *ns = &ftl_ns[nsid];
	CBF_INIT(ns->spb_queue[type]);
}


/*! @ */
