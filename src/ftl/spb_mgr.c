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
/*! \file spb_mgr.c
 * @brief define spb manager, allocation method
 *
 * \addtogroup spb_mgr
 *
 * @{
 */
#include "ftlprecomp.h"
#include "event.h"
#include "system_log.h"
#include "spb_pool.h"
#include "spb_info.h"
#include "spb_mgr.h"
#include "ftl_flash_geo.h"
#include "ftl_ns.h"
#include "erase.h"
#include "l2p_mgr.h"
#include "frb_log.h"
#include "gc.h"
#include "defect_mgr.h"
#include "ftl_l2p.h"
#include "ftl.h"
#include "console.h"
#include "misc.h"
#include "ftl_export.h"
#ifdef Dynamic_OP_En
#include "epm.h"
#include "srb.h"
#endif
#include "ErrorHandle.h"	// ISU, TSSP, PushGCRelsFalInfo

/*! \cond PRIVATE */
#define __FILEID__ spbmgr
#include "trace.h"
/*! \endcond PRIVATE */

#ifdef DUAL_BE
#define BE_CNT	2
#else
#define BE_CNT	1
#endif

#if (TRIM_SUPPORT == ENABLE)
extern u8* TrimBlkBitamp;
#endif

#define WL_EC_THRESHOLD	190	///< erase count threshold to start wear leveling
#define WL_EC_GC_OPEN_THRESHOLD	((WL_EC_THRESHOLD) + 8)	///< erase count threshold to retain not as GC open
#define EPC_WL_STOP	200	///< erase count threshold to stop wear leveling
#define EC_3K             3000
#define HIGH_EC_DR_THR    1872 //78*24 (hrs)
#define LOW_EC_DR_THR_4P 4032 //168*24 (hrs)
#define LOW_EC_DR_THR_2P  3888 //162*24 (hrs)


share_data_zi volatile bool	FTL_INIT_DONE;
share_data_zi volatile u8 		QBT_BLK_CNT;
slow_data qbt_grp_t     QBT_GRP_HANDLE[2];
share_data_zi volatile spb_id_t  host_spb_close_idx;/// runtime close blk idx   Curry
share_data_zi volatile spb_id_t  gc_spb_close_idx;/// runtime close blk idx   Curry
slow_data spb_id_t      host_spb_last_idx = INV_U16;
slow_data spb_id_t      gc_spb_last_idx = INV_U16;
slow_data spb_id_t      host_spb_pre_idx = INV_U16;

slow_data bool pbt_query_need_resume;
fast_data u16 min_good_pl;


fast_data_zi u32 min_vac;
slow_data spb_id_t spb_rd_id = INV_U16;
slow_data u8 spb_rd_step = INV_U8;
fast_data_zi u32 CAP_NEED_PHYBLK_CNT = 0;

share_data_zi volatile bool shr_shutdownflag;
share_data_ni volatile close_blk_ntf_que_t close_host_blk_que;
share_data_ni volatile close_blk_ntf_que_t close_gc_blk_que;
share_data_zi volatile bool ctl_wl_flag;
extern u16 wl_suspend_spb_id;
extern u32 wl_suspend_spb_sn;

spb_id_t dr_can = INV_U16;

slow_data u8 qbt_done_cnt = 0;
//for smart use
//share_data ftl_for_smart shr_ftl_smart;
extern tencnet_smart_statistics_t *tx_smart_stat;

#if RAID_SUPPORT_UECC
share_data_zi volatile u32 nand_ecc_detection_cnt;   //host + internal 1bit fail detection cnt
share_data_zi volatile u32 host_uecc_detection_cnt;  //host 1bit fail detection cnt
share_data_zi volatile u32 internal_uecc_detection_cnt; //internal 1bit fail detection cnt
share_data_zi volatile u32 uncorrectable_sector_count;  //host raid recovery fail cnt
share_data_zi volatile u32 internal_rc_fail_cnt;  //internal raid recovery fail cnt
share_data_zi volatile u32 host_prog_fail_cnt;

share_data volatile SMART_uecc_info_t smart_uecc_info[smart_uecc_info_cnt];
share_data volatile u16 uecc_smart_ptr;

fast_data u8 *ftl_runtime_host_blk_bmp;
#endif
share_data_zi read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];
share_data_zi volatile spb_id_t rd_open_close_spb[FTL_CORE_GC + 1];
extern u32 global_gc_mode; // ISU, GCRdFalClrWeak
extern cache_VU_FLAG VU_FLAG;
extern u32 shr_pstream_busy;
extern u8 tcg_reserved_spb_cnt;
extern volatile u8 plp_trigger;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
extern volatile u8 SLC_init;
#endif

fast_data_ni struct timer_list spb_scan_over_temp_blk_timer;
fast_data_zi bool is_scan_over_temp_timer_del;
share_data_zi volatile int temperature;
extern spb_id_t last_spb_vcnt_zero;

fast_data_zi spb_id_t record_full_pbt_host;
fast_data_zi spb_id_t record_full_pbt_pre_host;
fast_data_zi spb_id_t record_full_pbt_gc;

/*! @brief spb applicant queue head */
//typedef QSIMPLEQ_HEAD(_spb_appl_queue_t, _spb_appl_t) spb_appl_queue_t; //move to spb_mgr.h

/*!
 * @brief dynamic slc statistics definition
 */
typedef struct _dslc_statistics_t {
	u16 xlc_used;		///< current xlc used
	u16 xlc_max;		///< maximal xlc usage

	u16 slc_used;		///< current slc used
	u16 slc_max;		///< maximal slc usage

	u16 slc_avail;		///< available slc count
	u16 reserved;
} dslc_statistics_t;


/*typedef struct _spb_mgr_t {
	spb_desc_t *spb_desc;		///< spb descriptor table

	u16 *sw_flags;			///< only active when runtime, it will not flush to nand

	sys_log_desc_t slog_desc;	///< system log descriptor for spb descriptor
#if(SPB_BLKLIST == mDISABLE)
	dslc_statistics_t dslc_data;	///< dynamic slc object
#endif
	spb_pool_info_t   pool_info;
	u8 evt_flush_desc;		///< event to flush descriptor
	u8 evt_spb_alloc;		///< event to allocate new spb
	u8 evt_special_erase_spb;
	spb_appl_queue_t appl_queue;	///< the queue of applicants to request SPB

	sld_flush_t sld_flush;		///< flush context of system log
	u32 *vcnt_tbl;	///< valid cnt table, dump from L2P internal sram, 32B aligned

	u32 ttl_open;			///< total open spb count
	//u32 open_cnt[SPB_POOL_MAX];	///< open spb count of each pool

	union {
		struct {
			u32 allocating : 1;	///< indicate spb allocating
			u32 sld_flush_used : 1;	///< sld_flush was issued
			u32 desc_flush_delay : 1;	///< if delay event was triggered
			u32 desc_flush_all : 1;		///< flag to flush all descriptor
		} b;
		u32 all;
	} flags;

	fsm_head_t allocate_wait_que;		///< pending state machine for allocate

    struct timer_list poh_timer;		///< poh timer
} spb_mgr_t;*/ //move to spb_mgr.h for available use

fast_data_zi spb_mgr_t _spb_mgr;			///< spb manager

/*!
 * @brief Event handler for SPB allocation
 *
 * @param param		not used
 * @param payload	not used
 * @param sts		not used
 *
 * @return 		not used
 */
static void spb_allocation_handler(u32 param, u32 payload, u32 sts);
static void spb_special_erase(u32 param, u32 payload, u32 sts);

/*!
 * @brief According applicant, pop a new SPB for applicant
 *
 * @param appl	SPB applicant
 *
 * @return      Return valid SPB id or ~0 for nothing
 */
static spb_id_t spb_appl_get_spb(spb_appl_t *appl);

/*!
 * @brief pop oldest spb in pool
 *
 * @param pool_id		pool id
 *
 * @return			spb id which has largest erase count
 */
static spb_id_t spb_pop_old(u32 pool_id);

/*!
 * @brief pop young spb in pool
 *
 * @param pool_id	pool id
 * @param dslc		if use dynamic slc
 *
 * @return		spb id which has smallest erase count
 */
#if (SPB_BLKLIST == mENABLE)
static spb_id_t spb_pop_young(u32 pool_id);
#else
static spb_id_t spb_pop_young(u32 pool_id, bool dslc);
#endif

/*!
 * @brief callback function when spb was erased
 *
 * @param ctx		erase context
 *
 * @return		not used
 */
static void spb_erase_done(erase_ctx_t *ctx);
static void spb_special_erase_done(erase_ctx_t *ctx);

/*!
 * @brief flush spb descriptor to finish spb allocation
 *
 * @param appl		spb applicant
 *
 * @return		not used
 */
static void spb_alloc_desc_flush(spb_appl_t *appl);

#ifdef Dynamic_OP_En
extern epm_info_t*  shr_epm_info;
#endif
/*!
 * @brief spb descriptor flush handler of delayed event
 *
 * @param p0		not used
 * @param p1		not used
 * @param p2		not used
 *
 * @return		not used
 */
static void spb_mgr_desc_delay_flush(u32 p0, u32 p1, u32 p2);
ddr_code void FTL_CACL_QBTPBT_CNT(void)
{
	u32 spb_size_in_du = 0;
	u32 gb = get_disk_size_in_gb();
	u32 real_gb;
	u32 l2P_table_size;
    u32 spb_size;

	extern bool unlock_power_on;
	extern u16 global_capacity;
	extern u32 delay_cycle;
	global_capacity = gb;


	if (gb == 2048) {
        real_gb = 1920;
		delay_cycle = 10400; //13us
    } else if (gb == 1024) {
        real_gb = 960;
		delay_cycle = 14400; //18us
    } else {
        real_gb = 480;
		delay_cycle = 29600; //37us
    }

	ftl_apl_trace(LOG_ERR, 0xaa34, "[FTL]get_disk_size_in_gb():%u, cal capacity gb:%u, QD1 delay_cycle:%u", gb, real_gb, delay_cycle);
	_max_capacity = calc_jesd218a_capacity(real_gb);// LDA CNT
	spb_size_in_du = get_du_cnt_in_native_spb();// TLC
#if RAID_SUPPORT
	spb_size_in_du *= shr_nand_info.lun_num * shr_nand_info.geo.nr_planes - 1;
	spb_size_in_du /= shr_nand_info.lun_num * shr_nand_info.geo.nr_planes;
#endif

#if 0//def Dynamic_OP_En, change to lba mode
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	//ftl_apl_trace(LOG_ERR, 0, "Ocan0 _max_capacity %x, OPFlag %d \n", _max_capacity, epm_ftl_data->OPFlag);
    u32 DyOP_gb;

	//DYOPCapacity = _max_capacity;
	if(epm_ftl_data->OPFlag == 1)
	{
		//ftl_apl_trace(LOG_ERR, 0, "Ocan1 _max_capacity %x, OPvalue %d, Capa_OP14 %x \n", _max_capacity, epm_ftl_data->OPValue, epm_ftl_data->Capa_OP14);
		DyOP_gb = ((100 * epm_ftl_data->Capa_OP14) + (100 + epm_ftl_data->OPValue))/(100 + epm_ftl_data->OPValue);
		if(epm_ftl_data->OPValue == 14)
		{
			DYOPCapacity = _max_capacity;
		}
		else
		{
			DYOPCapacity = calc_jesd218a_capacity(DyOP_gb);
		}
		ftl_apl_trace(LOG_ERR, 0xa3c0, "Ocan2 Capa_OP14 %x, DYOPCapacity %x, DyOP_gb %d, OPValue %d \n", epm_ftl_data->Capa_OP14, DYOPCapacity, DyOP_gb, epm_ftl_data->OPValue);
	}
	else if((epm_ftl_data->OPFlag == 0) && (epm_ftl_data->Capa_OP14 == 0))
	{
		epm_ftl_data->Capa_OP14 = (real_gb * (14 + 100) + 100)/100 ;   // OP = (SSD Phy Cap. - User Cap.)/ User Cap.  14% 3.84TB ==> SSD Phy Cap. 4377.6GB
 		ftl_apl_trace(LOG_ERR, 0x3139, "Ocan3 Capa_OP14 %x, _max_capacity %x \n", epm_ftl_data->Capa_OP14, _max_capacity);
		epm_update(FTL_sign,(CPU_ID-1));
	}
#endif

    l2P_table_size = _max_capacity;
	spb_size = spb_size_in_du * (NAND_DU_SIZE/sizeof(pda_t));
	CAP_NEED_SPBBLK_CNT = occupied_by(l2P_table_size, spb_size_in_du);
    CAP_NEED_PHYBLK_CNT = occupied_by(l2P_table_size, shr_nand_info.geo.nr_pages * DU_CNT_PER_PAGE);
	shr_ftl_smart.capability_alloc_spb = CAP_NEED_SPBBLK_CNT;
	shr_ftl_smart.model = real_gb;
	QBT_BLK_CNT = occupied_by(l2P_table_size, spb_size);

	u32 unlock_power_on_min_blk = CAP_NEED_PHYBLK_CNT + spb_pool_get_ttl_spb_cnt(0) * RAID_SUPPORT
	                              + 8 * (shr_nand_info.interleave - RAID_SUPPORT);  //need more 8 free SPB (4+1 read_only + 3 host/pbt/gc)
	if(shr_ftl_smart.good_phy_spb < unlock_power_on_min_blk)
		unlock_power_on = true;

    ftl_apl_trace(LOG_ERR, 0xfcbb, "[FTL]_max_capacity %d QBT cal cnt %d Min spb cnt %d, need phy blk cnt: %u, unlock %d|%d",
    	_max_capacity, QBT_BLK_CNT, CAP_NEED_SPBBLK_CNT, CAP_NEED_PHYBLK_CNT,unlock_power_on_min_blk,unlock_power_on);
}
fast_code void FTL_CopyFtlBlkDataFromBuffer(u32 type)
{
    u32 dtag_start_idx = GET_BLKLIST_START_DTAGIDX(type);
    u8 *FtlBlkDataBuffer = (u8 *)ddtag2mem(dtag_start_idx);

	ftl_apl_trace(LOG_ALW, 0xa40c, "[FTL]copy data from buffer idx 0x%x", dtag_start_idx);
    memcpy((u8*)spb_info_tbl,        (u8*)FtlBlkDataBuffer,   SIZE_BLKLISTTBL);
    memcpy((u8*)_spb_mgr.spb_desc,   (u8*)(FtlBlkDataBuffer + OFFSET_SPBDESC),   SIZE_SPBDESC);
	memcpy((u8*)&_spb_mgr.pool_info, (u8*)(FtlBlkDataBuffer + OFFSET_POOL_INFO), SIZE_POOL_INFO);
    memcpy((u8*)&gFtlMgr,            (u8*)(FtlBlkDataBuffer + OFFSET_MANAGER),   SIZE_MANAGER);
	ftl_apl_trace(LOG_ALW, 0xdcc7, "[FTL]GPgsn 0x%x%x", (u32)(gFtlMgr.GlobalPageSN >> 32), (u32)(gFtlMgr.GlobalPageSN));
	memcpy((u8*)&VU_FLAG,            (u8*)(FtlBlkDataBuffer + MISC_INFO),   SIZE_MISC_INFO);
	ftl_apl_trace(LOG_ALW, 0x84c2, "VU FLAG VALUE RETORE");
}
fast_code void FTL_CopyFtlBlkDataToBuffer(u32 type)
{
	u32 dtag_start_idx = GET_BLKLIST_START_DTAGIDX(type);
	u8 *FtlBlkDataBuffer = (u8*)ddtag2mem(dtag_start_idx);;
	//ftl_apl_trace(LOG_ALW, 0, "[FTL]copy data to buffer dtag_start_idx 0x%x", dtag_start_idx);
	memcpy((u8*)FtlBlkDataBuffer,                      (u8*)spb_info_tbl,        SIZE_BLKLISTTBL);
	memcpy((u8*)(FtlBlkDataBuffer + OFFSET_SPBDESC),   (u8*)_spb_mgr.spb_desc,   SIZE_SPBDESC);
	memcpy((u8*)(FtlBlkDataBuffer + OFFSET_POOL_INFO), (u8*)&_spb_mgr.pool_info, SIZE_POOL_INFO);
    memcpy((u8*)(FtlBlkDataBuffer + OFFSET_MANAGER),   (u8*)&gFtlMgr,            SIZE_MANAGER);
	//ftl_apl_trace(LOG_ALW, 0, "[FTL]copy data to buffer end");

	memcpy((u8*)(FtlBlkDataBuffer + MISC_INFO),        (u8*)&VU_FLAG,    SIZE_MISC_INFO);
	//ftl_apl_trace(LOG_ALW, 0, "VU FLAG VALUE BACK UP");
}

ddr_code void spb_scan_over_temp_blk(void *data)
{
    //ftl_apl_trace(LOG_INFO, 0, "[FTL]Check Temperature");
	if (temperature >= (MIN_TEMP + 10) && temperature <= (MAX_TEMP - 15)) // check SPB_INFO_F_OVER_TEMP need GC in normal temperature
	{
		extern volatile u8 plp_trigger;
		u16 spb_id = _spb_mgr.pool_info.head[SPB_POOL_USER];
        while (spb_id != INV_U16 && !plp_trigger)
        {
            if (spb_info_get(spb_id)->flags & SPB_INFO_F_OVER_TEMP)
            {
            	spb_set_flag(spb_id, SPB_DESC_F_OVER_TEMP_GC);
                spb_mgr_rd_cnt_upd(spb_id);
				break;
            }
			spb_id = spb_info_tbl[spb_id].block;
        }
	}
    mod_timer(&spb_scan_over_temp_blk_timer, jiffies + HZ * 10); // Set 10s delay
}

ddr_code void spb_poh_init(void)
{
    u16 spb_cnt = get_total_spb_cnt();
    u16 i;
    if(spb_info_tbl[0].poh != POHTAG)
    {

        ftl_apl_trace(LOG_INFO, 0x3b8b, "[POH]Reset POH table");

        for (i = srb_reserved_spb_cnt; i < spb_cnt; i++)
        {
            if(spb_info_tbl[i].pool_id == SPB_POOL_USER)
            {
                spb_info_tbl[i].poh = poh;
            }
            else
            {
                spb_info_tbl[i].poh = INV_U32;
            }
        }
        spb_info_tbl[0].poh = POHTAG;
    }
}

ddr_code void Ec_tbl_update(u16 spb_id)
{
    u16 spb_cnt = get_total_spb_cnt();
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    u16 total_usable_spb_cnt = spb_cnt - spb_get_free_cnt(SPB_POOL_UNALLOC) - SRB_SYSINFO_BLOCK_CNT - tcg_reserved_spb_cnt - CACHE_RSVD_BLOCK_CNT;
#else
    u16 total_usable_spb_cnt = spb_cnt - spb_get_free_cnt(SPB_POOL_UNALLOC) - SRB_SYSINFO_BLOCK_CNT - tcg_reserved_spb_cnt;
#endif
    u16 i;

    spb_ec_tbl = (Ec_Table* )shr_ec_tbl_addr;

//    ftl_apl_trace(LOG_INFO, 0, "[EC]total_usable_spb_cnt: %d, spb_ec_tbl addr: 0x%x", total_usable_spb_cnt, spb_ec_tbl);

    if(spb_id == INV_U16)
    {
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
        for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < spb_cnt - tcg_reserved_spb_cnt; i++)
#else
        for (i = srb_reserved_spb_cnt; i < spb_cnt - tcg_reserved_spb_cnt; i++)
#endif
        {
            spb_ec_tbl->EcCnt[i] = spb_info_get(i)->erase_cnt;

            if((spb_info_get(i)->pool_id == SPB_POOL_USER) && (user_min_ec > spb_info_get(i)->erase_cnt))
            {
                user_min_ec = spb_info_get(i)->erase_cnt;
                user_min_ec_blk = i;
            }

        }
        get_avg_erase_cnt(&spb_ec_tbl->header.AvgEC, &spb_ec_tbl->header.MaxEC, &spb_ec_tbl->header.MinEC, &spb_ec_tbl->header.TotalEC);
    }
    else
    {
        spb_ec_tbl->EcCnt[spb_id] = spb_info_get(spb_id)->erase_cnt;
		if((spb_info_get(spb_id)->pool_id != SPB_POOL_QBT_ALLOC) && (spb_info_get(spb_id)->pool_id != SPB_POOL_QBT_FREE))
		{
	        if(spb_info_get(spb_id)->erase_cnt > spb_ec_tbl->header.MaxEC)
	        {
	            spb_ec_tbl->header.MaxEC = spb_info_get(spb_id)->erase_cnt;
	            max_ec_blk = spb_id;
	//            ftl_apl_trace(LOG_INFO, 0, "[EC]max: %d, spb[%d] ec: %d", spb_ec_tbl->header.MaxEC, spb_id, spb_info_tbl[spb_id].erase_cnt);
	        }

//        else if(spb_info_get(spb_id)->erase_cnt < spb_ec_tbl->header.MinEC)
//        {
//            spb_ec_tbl->header.MinEC = spb_info_get(spb_id)->erase_cnt;
//            ftl_apl_trace(LOG_INFO, 0, "[EC]min: %d, spb[%d] ec: %d", spb_ec_tbl->header.MinEC, spb_id, spb_info_tbl[spb_id].erase_cnt);
//        }

        	spb_ec_tbl->header.TotalEC++;
        	spb_ec_tbl->header.AvgEC = spb_ec_tbl->header.TotalEC / total_usable_spb_cnt;

		}
    }
}

void spb_clear_ec(void)
{
    u16 spb_cnt;
    for(spb_cnt = 0; spb_cnt < get_total_spb_cnt(); spb_cnt++)
    {
        spb_info_tbl[spb_cnt].erase_cnt = 0;
    }
}

static fast_code void ChkDR(void *data)
{
    spb_ec_tbl = (Ec_Table* )shr_ec_tbl_addr;
    u32 avg_ec = spb_ec_tbl->header.AvgEC;
    u16 thr, timer_dr;

    thr = HIGH_EC_DR_THR;

    //if(get_disk_size_in_gb() < 2000)
    {
        // 831 blk
        if(avg_ec <= EC_3K)
        {
            thr = LOW_EC_DR_THR_4P;
        }
        timer_dr = (avg_ec > EC_3K) ? 3000 : 6000;  //5mins : 10mins
    }/*
    else
    {
        // 1662 blk
        if(avg_ec <= EC_3K)
        {
            thr = LOW_EC_DR_THR_2P;
        }
        timer_dr = (avg_ec > EC_3K) ? 1800 : 6000;  //3mins : 10mins
    }*/
    //ftl_apl_trace(LOG_INFO, 0, "[DR]avg ec: %d, thr: %d", avg_ec, thr);
    if(spb_get_free_cnt(SPB_POOL_USER) != 0)
    {
        dr_can = spb_mgr_get_head(SPB_POOL_USER);

        if(((poh + ((pom_per_ms + jiffies) / 36000)) > spb_info_tbl[dr_can].poh) && spb_info_tbl[dr_can].poh != INV_U32)
        {
            if((poh + ((pom_per_ms + jiffies) / 36000)) - spb_info_tbl[dr_can].poh >= thr)
            {
                ftl_apl_trace(LOG_INFO, 0x51e2, "[DR]poh: %d, spb[%d]poh: %d, jiffies: %d", (poh + ((pom_per_ms + jiffies) / 36000)), dr_can, spb_info_tbl[dr_can].poh, jiffies);
                GC_MODE_SETTING(GC_DATARETENTION);
                if (gc_busy() == false)
                {
                    u32 nsid = _spb_mgr.spb_desc[dr_can].ns_id;
                    ftl_ns_dr_gc(nsid, dr_can);
                }
            }
        }
        else
        {
            dr_can = INV_U16;
        }
    }

	mod_timer(&_spb_mgr.poh_timer, jiffies + timer_dr*HZ/10);
}

/*!
 * @brief api to check if this spb was free or not
 *
 * @param spb_id	spb id to be checked
 *
 * @return		true if free
 */
static inline bool is_free_spb(spb_id_t spb_id)
{
	if (_spb_mgr.spb_desc[spb_id].flags == 0 && _spb_mgr.sw_flags[spb_id] == 0)
		return true;

	return false;
}

/*!
 * @brief api to check if this spb was gc candidate
 *
 * @param spb_id	spb id to be checked
 *
 * @return		true if candidate
 */
static inline bool is_gc_canidiate(spb_id_t spb_id)
{
	u32 sw_flag = _spb_mgr.sw_flags[spb_id];
	u32 flag = _spb_mgr.spb_desc[spb_id].flags;

	if (((flag & SPB_DESC_F_GCED) == 0) &&
		((sw_flag & SPB_SW_F_FREEZE) == 0)) {
		return true;
	}

	return false;
}

 bool is_qbt_spb(spb_id_t spb_id)
{
	u8 grp_cnt;
	for(grp_cnt=0; grp_cnt<2; grp_cnt++)
	{
		if((QBT_GRP_HANDLE[grp_cnt].head_qbt_idx == spb_id) || (QBT_GRP_HANDLE[grp_cnt].tail_qbt_idx == spb_id))
			return true;
	}
	return false;
}
/*!
 * @brief initialize spb descriptor when virgin
 *
 * @param desc	spb descriptor
 *
 * @return	not used
 */
init_code void spb_desc_init(sys_log_desc_t *desc)
{
//	u32 i;
//	spb_desc_t *spb_desc;

	memset(desc->ptr, 0, desc->total_size);
//	spb_desc = (spb_desc_t *) desc->ptr;
//	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt(); i++) {
//		spb_desc[i].pool_id = spb_info_get(i)->pool_id;
//	}
}

ddr_code void spb_mgr_init(ftl_initial_mode_t mode)
{
	u32 sz;
	sz = sizeof(u16) * get_total_spb_cnt();

	if(mode != FTL_INITIAL_PREFORMAT)
	{
		sys_log_desc_register(&_spb_mgr.slog_desc, get_total_spb_cnt(), sizeof(spb_desc_t), spb_desc_init);
		_spb_mgr.sw_flags = sys_malloc(FAST_DATA, sz);
		sys_assert(_spb_mgr.sw_flags);

		evt_register(spb_allocation_handler, 0, &_spb_mgr.evt_spb_alloc);
		evt_register(spb_mgr_desc_delay_flush, 0, &_spb_mgr.evt_flush_desc);
		evt_register(spb_special_erase, 0, &_spb_mgr.evt_special_erase_spb);
		ftl_apl_trace(LOG_INFO, 0x7408, "[FTL] poweron spb_mgr_init");
	}

	memset(_spb_mgr.sw_flags, 0, sz);

	QSIMPLEQ_INIT(&_spb_mgr.appl_queue);

	_spb_mgr.flags.all = 0;

	fsm_queue_init(&_spb_mgr.allocate_wait_que);

	_spb_mgr.ttl_open = 0;

#if (SPB_BLKLIST == mENABLE)
	{
		u8 i;
		host_spb_close_idx = INV_U16;
		gc_spb_close_idx = INV_U16;
		host_spb_last_idx = INV_U16;
		gc_spb_last_idx = INV_U16;
		shr_shutdownflag = false;
		for (i = 0; i < SPB_POOL_MAX; i++) {
			_spb_mgr.pool_info.head[i] = INV_U16;
			_spb_mgr.pool_info.tail[i] = INV_U16;
			_spb_mgr.pool_info.free_cnt[i] = 0;
			_spb_mgr.pool_info.open_cnt[i] = 0;
		}
		CBF_INIT(&close_host_blk_que);
		CBF_INIT(&close_gc_blk_que);
		pbt_query_ready			= 1;
		pbt_query_need_resume 	= 0;
		for (i = 0; i<2; i++){
			blklist_flush_query[i] = false;
			blklist_tbl[i].type = INV_U16;
		}
	}
#endif

	if(mode == FTL_INITIAL_PREFORMAT)
	{
		ftl_apl_trace(LOG_INFO, 0xf461, "[FTL] preformat spb_mgr_init");
	}

    _spb_mgr.poh_timer.function = ChkDR;
    _spb_mgr.poh_timer.data = "ChkDR";
    mod_timer(&_spb_mgr.poh_timer, jiffies + 6000*HZ/10);
#if CROSS_TEMP_OP
    temperature = 45; // init temperature 45°C
    cpu_msg_register(CPU_MSG_SET_SPB_OVER_TEMP_FLAG, set_spb_over_temp_flag);
    spb_scan_over_temp_blk_timer.function = spb_scan_over_temp_blk;
    spb_scan_over_temp_blk_timer.data = NULL;
    mod_timer(&spb_scan_over_temp_blk_timer, jiffies + HZ * 20); // Set 20s delay
    is_scan_over_temp_timer_del = false;
#endif
}

ddr_code void set_spb_over_temp_flag(volatile cpu_msg_req_t *req)
{
    spb_info_get(req->pl)->flags |= SPB_INFO_F_OVER_TEMP;
}

init_code void spb_mgr_sync_ns(void)
{
	u32 i;
	spb_desc_t *desc;

	desc = _spb_mgr.spb_desc ;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
//		u32 pool_id = desc[i].pool_id;

		ftl_ns_add_pool(desc[i].ns_id, spb_get_poolid(i));
	}
}

init_code void spb_mgr_start(void)
{
	u32 i;
	spb_rt_flags_t *rt_flags;
	u32 sz;
//	spb_desc_t *desc;

     _spb_mgr.spb_desc = sys_log_desc_get_ptr(&_spb_mgr.slog_desc);

	sz = sizeof(spb_rt_flags_t) * get_total_spb_cnt();
	rt_flags = sys_malloc(FAST_DATA, sz);
	sys_assert(rt_flags);
	memset(rt_flags, 0, sz);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
//		u32 pool_id = desc[i].pool_id;

		_spb_mgr.pool_info.free_cnt[spb_get_poolid(i)]++;

		//ftl_ns_add_pool(desc[i].ns_id, pool_id);
	}

	// update rt flags in CPU2
	ftl_core_restore_rt_flags(rt_flags);
	sys_free(FAST_DATA, rt_flags);
}

slow_code void spb_mgr_preformat_desc_init(spb_free_pool_t pool)
{
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	u16 blk_start = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT;
#else
	u16 blk_start = srb_reserved_spb_cnt;
#endif
	u16 blk_end = get_total_spb_cnt() - tcg_reserved_spb_cnt;
	u16 blk_idx;
	u8  pool_id;
	extern volatile u8 plp_trigger;
	ftl_apl_trace(LOG_INFO, 0x61c9, "blk_start:%d blk_end:%d", blk_start, blk_end);
	ftl_ns_check_appl_allocating();

	for (blk_idx = blk_start; blk_idx < blk_end; blk_idx++) {
        pool_id = spb_info_get(blk_idx)->pool_id;
		if(_spb_mgr.sw_flags[blk_idx]&SPB_SW_F_ALLOCATING){
			ftl_apl_trace(LOG_INFO, 0x5503, "allocating_blk_abort:%d",blk_idx);
			continue;
		}
		if(plp_trigger)
			FTL_NO_LOG = true;//reduce log
		_spb_mgr.spb_desc[blk_idx].flags = 0;
		_spb_mgr.spb_desc[blk_idx].ns_id = 0;
		_spb_mgr.spb_desc[blk_idx].rd_cnt = 0;
		_spb_mgr.spb_desc[blk_idx].sn = 0;
		_spb_mgr.pool_info.free_cnt[pool_id]++;
		if(pool_id == SPB_POOL_FREE)
		{
			FTL_BlockPopPushList(SPB_POOL_FREE, blk_idx, FTL_SORT_BY_EC);
		}
	}
}

#if (SPB_BLKLIST == mENABLE)
//-------------------------------------------------------------------
// Function       : mUINT_16 FTL_BLOCK_PopHead(mUINT_16 pool)
// Description    : Pop SBlk from Head of Pool
// Input          : Pool
// return         : Poped SBlk
//-------------------------------------------------------------------
slow_code mUINT_16 FTL_BlockPopHead(mUINT_16 ftlpool)
{
    mUINT_16 SBlk = INV_U16;


    if (_spb_mgr.pool_info.free_cnt[ftlpool])
    {
        SBlk = _spb_mgr.pool_info.head[ftlpool];

        if(SBlk == _spb_mgr.pool_info.tail[ftlpool])  // head==tail, only one SBlk in pool
        {
            _spb_mgr.pool_info.head[ftlpool] = _spb_mgr.pool_info.tail[ftlpool] = INV_U16;
        }
        else
        {
            _spb_mgr.pool_info.head[ftlpool] = spb_info_tbl[SBlk].block;
        }

        spb_info_tbl[SBlk].block = INV_U16;
        _spb_mgr.pool_info.free_cnt[ftlpool]--;

        ftl_apl_trace(LOG_INFO, 0x0f72, "[FTL]Pop Head blk:%d pool:%d cnt:%d nxt_blk:%d", SBlk, ftlpool, _spb_mgr.pool_info.free_cnt[ftlpool], spb_info_tbl[SBlk].block);
    }
    else
    {
		ftl_apl_trace(LOG_INFO, 0xbdc7, "[Blk]No Free Blk, pool:%d",ftlpool);
        sys_assert(0);
    }


    return SBlk;
}


//-------------------------------------------------------------------
// Function       : FTL_BlockPushHead(mUINT_16 ftlpool, mUINT_16 SBlk)
// Description    : Push SBlk in specific pool, to head
// Input          : Pool and SBlk
// return         : N/A
//-------------------------------------------------------------------
slow_code void FTL_BlockPushHead(mUINT_16 ftlpool, mUINT_16 SBlk)
{
    mUINT_16    tempSBlk = INV_U16;

    // "[FTL] Push Head,pool:%d blk:%d", 2 2
    if(SBlk == 0) return;

    if (_spb_mgr.pool_info.free_cnt[ftlpool] <= get_total_spb_cnt())
    {

        if(_spb_mgr.pool_info.tail[ftlpool] == INV_U16)
        {
            _spb_mgr.pool_info.head[ftlpool] = _spb_mgr.pool_info.tail[ftlpool] = SBlk;
            spb_info_tbl[SBlk].block = INV_U16;    // Next SBlk is NUL
        }
        else
        {
            tempSBlk = _spb_mgr.pool_info.head[ftlpool];
            spb_info_tbl[SBlk].block = tempSBlk;
            _spb_mgr.pool_info.head[ftlpool] = SBlk;
        }

        // Insert finish, blkCnt +1 and update Blk status
        //MSGOUT("[FTL_BlockPushHead]%d Blk : 0x%x, Pol : %d\n ",HAL_CPU_GetId(),SBlk,ftlpool);
        _spb_mgr.pool_info.free_cnt[ftlpool]++;
        spb_info_tbl[SBlk].pool_id = ftlpool;
		ftl_apl_trace(LOG_INFO, 0x6db1, "[FTL]Push head blk:%d pool:%d cnt:%d nxt_blk:%d", SBlk, ftlpool, _spb_mgr.pool_info.free_cnt[ftlpool], spb_info_tbl[SBlk].block);
    }
    else
    {
        //PRINTF_FTL("Psh-pol[%d]b[%4x]hd[%4x]tl[%4x] - Exceed NAND Blks\n", ftlpool, SBlk, gFtlBlkInfo.head[ftlpool], gFtlBlkInfo.tail[ftlpool]);
        //ASSERT_QA(FALSE);
    }

}
#ifdef Dynamic_OP_En
extern u8 DYOP_FRB_Erase_flag;
#endif
//-------------------------------------------------------------------
// Function       : UINT16 FTL_BLOCK_PopHead(UINT16 pool)
// Description    : Pop Specific SBlk from Pool
// Input          : Pool and SBlk
// return         : N/A
//-------------------------------------------------------------------
slow_code void FTL_BlockPopList(mUINT_16 ftlpool, mUINT_16 SBlk)
{
    mUINT_16     prevSBlk = INV_U16;
    mUINT_16     currSBlk = INV_U16;

    //[FTL] Pop list,pool:%d blk:%d nxt:%d

    if(SBlk == 0) return;


    if(_spb_mgr.pool_info.free_cnt[ftlpool])
    {
        // Selected SBlk == Head == Tail, => only one SBlk in pool
        if(SBlk == _spb_mgr.pool_info.head[ftlpool] && SBlk == _spb_mgr.pool_info.tail[ftlpool])
        {
            _spb_mgr.pool_info.head[ftlpool] = _spb_mgr.pool_info.tail[ftlpool] = INV_U16;
            //gFtlBlkListTbl[SBlk].block = INV_U16;
        }
        // Selected SBlk in Head
        else if(SBlk == _spb_mgr.pool_info.head[ftlpool])
        {
            _spb_mgr.pool_info.head[ftlpool] = spb_info_tbl[SBlk].block;
            //gFtlBlkListTbl[SBlk].block = INV_U16;
        }
        // Selected SBlk in Middle or Tail
        else
        {
            prevSBlk = _spb_mgr.pool_info.head[ftlpool];
            currSBlk = spb_info_tbl[prevSBlk].block;

            while((currSBlk != SBlk)&&(currSBlk != INV_U16))    // Still NEED search from the head of pool
            {
                if(currSBlk == _spb_mgr.pool_info.tail[ftlpool])    // SBlk NOT Find
                {
                    //FTL_DumpInfo();
                    //PRINTF_FTL("\nSBlkPopEr-SBlkNOTFind:[%d]b[%4x]hd[%4x]tl[%4x]\n", ftlpool, SBlk, gFtlBlkInfo.head[ftlpool], gFtlBlkInfo.tail[ftlpool]);

                    // "[FTL] BlkNotFound"

                    // Tail.block will be INV_U16 and break while loop
                    // 1. BuildBadPool may enter here.
                    // 2. Power on push Header into pools.

                    //ASSERT_QA(FALSE);
                    //return;
                }
                prevSBlk = currSBlk;
                currSBlk = spb_info_tbl[currSBlk].block;
            }

            if(currSBlk == INV_U16)
            {
            	ftl_apl_trace(LOG_INFO, 0x015e, "[FTL]SBlk %d is not in pool %d, now in pool %d", SBlk, ftlpool, spb_info_tbl[SBlk].pool_id);
                return;
            }

            spb_info_tbl[prevSBlk].block = spb_info_tbl[currSBlk].block;

            if (SBlk == _spb_mgr.pool_info.tail[ftlpool])    //Selected SBlk in Tail
            {
                _spb_mgr.pool_info.tail[ftlpool] = prevSBlk;
            }
        }

        spb_info_tbl[SBlk].block = INV_U16;
        _spb_mgr.pool_info.free_cnt[ftlpool] --;

        if(ftlpool == SPB_POOL_UNALLOC){
            spb_ec_tbl->header.TotalEC += spb_info_get(SBlk)->erase_cnt;
        }

#ifdef Dynamic_OP_En
		if(DYOP_FRB_Erase_flag != 1)
#endif
		{
			if(FTL_NO_LOG == false)
			{
				ftl_apl_trace(LOG_INFO, 0x13de, "[FTL]Pop list blk:%d pool:%d cnt:%d nxt_blk:%d", SBlk, ftlpool, _spb_mgr.pool_info.free_cnt[ftlpool], spb_info_tbl[SBlk].block);
			}
		}
    }
    else
    {
        ftl_apl_trace(LOG_INFO, 0x1df1, "[FTL] Pool %d Cnt is ZERO", ftlpool);
    }


}


//-------------------------------------------------------------------
// Function       : FTL_BlockPushList(mUINT_16 ftlpool, mUINT_16 SBlk, mUINT_16 sortRule)
// Description    : Push SBlk in specific pool, accroding to sortRule
// Input          : Pool and SBlk and sortRule
// return         : N/A
//-------------------------------------------------------------------
slow_code void FTL_BlockPushList(mUINT_16 ftlpool, mUINT_16 SBlk, mUINT_16 sortRule)
{
    mUINT_32    CurCnt, ComCnt;
    mUINT_16    prevSBlk = INV_U16;
    mUINT_16    currSBlk = INV_U16;
    mUINT_8     i=0;
    mUINT_16    counter = 0;  // for test

    if(SBlk == 0) return;

    if (_spb_mgr.pool_info.free_cnt[ftlpool]<= get_total_spb_cnt())
    {
        // Pool is empty
        if(_spb_mgr.pool_info.tail[ftlpool] == INV_U16)
        {
            _spb_mgr.pool_info.head[ftlpool] = _spb_mgr.pool_info.tail[ftlpool] = SBlk;
            spb_info_tbl[SBlk].block = INV_U16;    // Next SBlk is NUL
        }
        else
        {
            switch(sortRule)
            {
                case FTL_SORT_NONE:    //  No sort, add from tail
                {
                    spb_info_tbl[_spb_mgr.pool_info.tail[ftlpool]].block = SBlk;
                    spb_info_tbl[SBlk].block = INV_U16;    // Next SBlk is NUL
                    _spb_mgr.pool_info.tail[ftlpool] = SBlk;         // Update Tail
                    break;
                }
                case FTL_SORT_BY_EC:    // Sort from Head
                {
                    prevSBlk = _spb_mgr.pool_info.head[ftlpool];    // Sort from Head

                    // Search insert point
                    if(prevSBlk != _spb_mgr.pool_info.tail[ftlpool])
                    {
                        currSBlk = spb_info_tbl[prevSBlk].block;    // Next SBlk
                        ComCnt = spb_info_tbl[SBlk].erase_cnt;

#if (NEW_STATIC_WL == mENABLE)
                        if((ComCnt < spb_info_tbl[prevSBlk].erase_cnt) && (ftlpool == SPB_POOL_USER))
                        {
                            spb_info_tbl[SBlk].block = prevSBlk;
                            _spb_mgr.pool_info.head[SPB_POOL_USER] = SBlk;
                            break;
                        }
                        else
                        {
                            while(1)
                            {
                                if(currSBlk == INV_U16)
                                {
                                    break;
                                }
                                CurCnt = spb_info_tbl[currSBlk].erase_cnt;

                                if(ComCnt < CurCnt && (ftlpool == SPB_POOL_USER))
                                {
                                    break;
                                }
                                else if(i >= 6 && ComCnt < CurCnt)
                                {
                                    break;
                                }
                                else if (currSBlk == _spb_mgr.pool_info.tail[ftlpool])
                                {
                                    prevSBlk = _spb_mgr.pool_info.tail[ftlpool];
                                    break;
                                }
                                prevSBlk = currSBlk;
                                currSBlk = spb_info_tbl[currSBlk].block;
                                i++;
                            }
                        }
#else
                        if(ComCnt < spb_info_tbl[prevSBlk].erase_cnt)
                        {
                            spb_info_tbl[SBlk].block = prevSBlk;
                            _spb_mgr.pool_info.head[ftlpool] = SBlk;
                            break;
                        }
                        else
                        {
                            while(1)
                            {
                                if(currSBlk == INV_U16)
                                {
                                    break;
                                }
                                CurCnt = spb_info_tbl[currSBlk].erase_cnt;

                                if(ComCnt < CurCnt)
                                {
                                    break;
                                }
                                else if(i >= 6 && ComCnt < CurCnt)
                                {
                                    break;
                                }
                                else if (currSBlk == _spb_mgr.pool_info.tail[ftlpool])
                                {
                                    prevSBlk = _spb_mgr.pool_info.tail[ftlpool];
                                    break;
                                }
                                prevSBlk = currSBlk;
                                currSBlk = spb_info_tbl[currSBlk].block;
                                i++;
                            }
                        }
#endif
                    }

                    // Insert SBlk
                    if(prevSBlk == _spb_mgr.pool_info.tail[ftlpool])
                    {
                        if(_spb_mgr.pool_info.head[ftlpool]==prevSBlk && spb_info_tbl[SBlk].erase_cnt<spb_info_tbl[prevSBlk].erase_cnt)
                        {
                            spb_info_tbl[SBlk].block = prevSBlk;
                            spb_info_tbl[prevSBlk].block = INV_U16;
                            _spb_mgr.pool_info.head[ftlpool] = SBlk;
                        }
                        else
                        {
                            spb_info_tbl[_spb_mgr.pool_info.tail[ftlpool]].block = SBlk;
                            spb_info_tbl[SBlk].block = INV_U16;
                            _spb_mgr.pool_info.tail[ftlpool] = SBlk;
                        }
                    }
                    else    // Insert point in middle
                    {
                        spb_info_tbl[prevSBlk].block = SBlk;
                        spb_info_tbl[SBlk].block     = currSBlk;
                    }

                    break;
                }
                case FTL_SORT_BY_SN:
                {
                    prevSBlk = _spb_mgr.pool_info.head[ftlpool];    // Sort from Head
                    currSBlk = spb_info_tbl[prevSBlk].block;
                    ComCnt = spb_get_sn(SBlk);

                    if(ComCnt < spb_get_sn(prevSBlk))   // Check Head SBlk's SN. if  so, add SBlk from Head
                    {
                        spb_info_tbl[SBlk].block = prevSBlk;
                        _spb_mgr.pool_info.head[ftlpool] = SBlk;
                    }
                    else
                    {
                        // Search insert point
                        while(1)
                        {
                            if(currSBlk == INV_U16) //last block
                            {
                                break;
                            }
                            CurCnt = spb_get_sn(currSBlk);
                            if(ComCnt < CurCnt)
                            {
                                break;
                            }
                            else if (currSBlk == _spb_mgr.pool_info.tail[ftlpool])
                            {
                                prevSBlk = _spb_mgr.pool_info.tail[ftlpool];
                                break;
                            }
                            prevSBlk = currSBlk;
                            currSBlk = spb_info_tbl[currSBlk].block;

                            counter++;
                            if(counter>2048)
                            {
                                //MSGOUT("Infinite Loops\n");
                            }

                        }

                        // Insert SBlk
                        if(prevSBlk == _spb_mgr.pool_info.tail[ftlpool])  // Push from tail
                        {
                            spb_info_tbl[_spb_mgr.pool_info.tail[ftlpool]].block = SBlk;
                            spb_info_tbl[SBlk].block = INV_U16;
                            _spb_mgr.pool_info.tail[ftlpool] = SBlk;
                        }
                        else    // Insert point in middle
                        {
                            spb_info_tbl[prevSBlk].block = SBlk;
                            spb_info_tbl[SBlk].block     = currSBlk;
                        }
                    }
                    break;
                }
                case FTL_SORT_BY_IDX:
                {
                    prevSBlk = _spb_mgr.pool_info.head[ftlpool];
                    currSBlk = spb_info_tbl[prevSBlk].block;

                    if(SBlk < prevSBlk)
                    {
                        spb_info_tbl[SBlk].block = prevSBlk;
                        _spb_mgr.pool_info.head[ftlpool] = SBlk;
                    }
                    else
                    {
                        // Search insert point
                        while(1)
                        {
                            if(currSBlk == INV_U16) //last block
                            {
                                break;
                            }

                            if(SBlk < currSBlk)
                            {
                                break;
                            }
                            else if (currSBlk == _spb_mgr.pool_info.tail[ftlpool])
                            {
                                prevSBlk = _spb_mgr.pool_info.tail[ftlpool];
                                break;
                            }
                            prevSBlk = currSBlk;
                            currSBlk = spb_info_tbl[currSBlk].block;
                        }

                        // Insert SBlk
                        if(prevSBlk == _spb_mgr.pool_info.tail[ftlpool])  // Push from tail
                        {
                            spb_info_tbl[_spb_mgr.pool_info.tail[ftlpool]].block = SBlk;
                            spb_info_tbl[SBlk].block = INV_U16;
                            _spb_mgr.pool_info.tail[ftlpool] = SBlk;
                        }
                        else    // Insert point in middle
                        {
                            spb_info_tbl[prevSBlk].block = SBlk;
                            spb_info_tbl[SBlk].block     = currSBlk;
                        }
                    }
                    break;
                }
                default:  // SortRule Fail
                {
                    //FTL_DumpInfo();
                    //PRINTF_FTL("\nSBlkPshEr-sortRule:[%d]b[%4x]hd[%4x]tl[%4x]stRl[%d]\n", ftlpool, SBlk, gFtlBlkInfo.head[ftlpool], gFtlBlkInfo.tail[ftlpool], sortRule);
                    //ASSERT_QA(FALSE);
                    break;
                }
            }
        }
        // Insert finish, blkCnt +1 and update Blk status
        _spb_mgr.pool_info.free_cnt[ftlpool]++;
        spb_info_tbl[SBlk].pool_id = ftlpool;

        if((ftlpool == SPB_POOL_UNALLOC) && (spb_ec_tbl->header.TotalEC >= spb_info_get(SBlk)->erase_cnt)){
            spb_ec_tbl->header.TotalEC -= spb_info_get(SBlk)->erase_cnt;
        }

#ifdef Dynamic_OP_En
		if(DYOP_FRB_Erase_flag != 1)
#endif
		{
			if(FTL_NO_LOG == false)
			{
				ftl_apl_trace(LOG_INFO, 0x8362, "[FTL]Push list blk:%d pool:%d cnt:%d nxt_blk:%d", SBlk, ftlpool, _spb_mgr.pool_info.free_cnt[ftlpool], spb_info_tbl[SBlk].block);
			}
		}
    }
    else
    {
    	sys_assert(0);
    }
    if(ftlpool == SPB_POOL_FREE)
    {
        if(GC_MODE_CHECK)
        {
            GC_Mode_DeAssert();
        }

//        if((GC_GetGCReadOnlyFlag()) && (spb_get_free_cnt(SPB_POOL_FREE) > GC_BLKCNT_READONLY))
//        {
//            //soutb4(0xAC, 0x01, 0x00, 0x2D); // "[GC] clear ROM"
//            printk("[GC] clear ROM\n");
//
//            if(GC_GetGCReadOnlyFlag())
//            {
//                GC_SetGCReadOnlyFlag(mFALSE);
//            }
//
//            if(GC_STATE_CHECK(GC_STATE_READONLY))
//            {
//                GC_STATE_CLEAN(GC_STATE_READONLY);
//            }
//
//            GC_Mode_Assert();
//        }

    }
}


//-------------------------------------------------------------------
// Function     : void FTL_BlockPopPushList(mUINT_16 pool, mUINT_16 block, mUINT_16 sortRule)
// Description  :
// Input        : N/A
// return       : N/A
//-------------------------------------------------------------------
slow_code void FTL_BlockPopPushList(mUINT_16 pool, mUINT_16 block, mUINT_16 sortRule)
{
    FTL_BlockPopList(spb_info_tbl[block].pool_id, block);
    FTL_BlockPushList(pool, block, sortRule);
}

extern void spb_pool_table(void){
	u8 i;

	for (i = 0; i < SPB_POOL_MAX; i++) {
		ftl_apl_trace(LOG_ALW, 0x4fb9, "[FTL]Pool[%d] H:[0x%x] T:[0x%x] Cnt:[%d]",
			i,_spb_mgr.pool_info.head[i],_spb_mgr.pool_info.tail[i], _spb_mgr.pool_info.free_cnt[i]);
	}

	if(FTL_INIT_DONE == false)
    {
		u16 blk_cnt = 0;
		for (i = 0; i < SPB_POOL_MAX; i++) {
			blk_cnt += _spb_mgr.pool_info.free_cnt[i];
		}
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
        if(blk_cnt != (get_total_spb_cnt() - srb_reserved_spb_cnt - tcg_reserved_spb_cnt - CACHE_RSVD_BLOCK_CNT))
#else
        if(blk_cnt != (get_total_spb_cnt() - srb_reserved_spb_cnt - tcg_reserved_spb_cnt))
#endif
        {
            ftl_apl_trace(LOG_ALW, 0x354a, "block list pool cnt error!!");
        }
        //sys_assert(blk_cnt == (get_total_spb_cnt()-1));
	}
}


slow_code void spb_blk_info(){
	u32 i;
	for (i = srb_reserved_spb_cnt; i < 11; i++) {
	ftl_apl_trace(LOG_ALW, 0x506c, "[FTL]SBlk[0x%x] EC:[0x%x] RC:[0x%x] POH:[%d]",
					i, spb_info_get(i)->erase_cnt, _spb_mgr.spb_desc[i].rd_cnt, spb_info_get(i)->poh);
	}

}

slow_code void spb_blk_list(){
	u32 i;
	for (i = srb_reserved_spb_cnt; i < 11; i++) {
	ftl_apl_trace(LOG_ALW, 0x3212, "[FTL]SBlk[0x%x] Next:[0x%x] pool:[%d] Flg:[0x%x] swFlg:[0x%x]",
					i, spb_info_get(i)->block, spb_info_get(i)->pool_id, spb_get_flag(i),_spb_mgr.sw_flags[i]);
	}
}

slow_code void spb_ftl_mgr_info(){
	ftl_apl_trace(LOG_ALW, 0x1e3b, "[FTL]GPSN[0x%x-%x] PreSN[0x%x] SN[[0x%x] TblSN[0x%X]",
					(u32)(gFtlMgr.GlobalPageSN >> 32), (u32)gFtlMgr.GlobalPageSN, gFtlMgr.PrevTableSN, gFtlMgr.SerialNumber, gFtlMgr.TableSN);
}


slow_code void spb_mgr_set_head(u32 pool_id, u32 idx)
{
	_spb_mgr.pool_info.head[pool_id] = idx;
}

slow_code u16 spb_mgr_get_head(u32 pool_id)
{
	return _spb_mgr.pool_info.head[pool_id];
}

slow_code u16 spb_mgr_get_next_spb(u16 blk)
{
    return spb_info_tbl[blk].block;
}
slow_code void spb_mgr_set_tail(u32 pool_id, u32 idx)
{
	 _spb_mgr.pool_info.tail[pool_id] = idx;
}

slow_code u16 spb_mgr_get_tail(u32 pool_id)
{
	return _spb_mgr.pool_info.tail[pool_id];
}

extern u16 ftl_chk_qbt_ec(u16 spb_id);

#if 0
ddr_code void qbt_retire_handle(u16 retire_qbt)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	if(shr_ftl_flags.b.qbt_retire_need == 1)
	{
		ftl_apl_trace(LOG_INFO, 0xe02e, "[FTL]qbt_retire_handle:retire");
		u16 cand_spb_id = spb_mgr_get_head(SPB_POOL_FREE);

		if(retire_qbt == epm_ftl_data->qbt_grp1_head)
		{
			epm_ftl_data->qbt_grp1_head = cand_spb_id;
			QBT_GRP_HANDLE[0].head_qbt_idx = cand_spb_id;
			FTL_BlockPopPushList(SPB_POOL_QBT_FREE, cand_spb_id, FTL_SORT_NONE);
			if(QBT_BLK_CNT > 1)
			{
				FTL_BlockPopPushList(SPB_POOL_QBT_FREE, QBT_GRP_HANDLE[0].tail_qbt_idx, FTL_SORT_NONE);
			}
		}

		if(retire_qbt == epm_ftl_data->qbt_grp1_tail)
		{
			epm_ftl_data->qbt_grp1_tail = cand_spb_id;
			QBT_GRP_HANDLE[0].tail_qbt_idx = cand_spb_id;
			if(QBT_BLK_CNT > 1)
			{
				FTL_BlockPopPushList(SPB_POOL_QBT_FREE, QBT_GRP_HANDLE[0].head_qbt_idx, FTL_SORT_NONE);
				FTL_BlockPopPushList(SPB_POOL_QBT_FREE, cand_spb_id, FTL_SORT_NONE);
			}
		}

		if(retire_qbt == epm_ftl_data->qbt_grp2_head)
		{
			epm_ftl_data->qbt_grp2_head = cand_spb_id;
			QBT_GRP_HANDLE[1].head_qbt_idx = cand_spb_id;
			FTL_BlockPopPushList(SPB_POOL_QBT_FREE, cand_spb_id, FTL_SORT_NONE);
			if(QBT_BLK_CNT > 1)
			{
				FTL_BlockPopPushList(SPB_POOL_QBT_FREE, QBT_GRP_HANDLE[1].tail_qbt_idx, FTL_SORT_NONE);
			}
		}


		if(retire_qbt == epm_ftl_data->qbt_grp2_tail)
		{
			epm_ftl_data->qbt_grp2_tail = cand_spb_id;
			QBT_GRP_HANDLE[1].tail_qbt_idx = cand_spb_id;
			if(QBT_BLK_CNT > 1)
			{
				FTL_BlockPopPushList(SPB_POOL_QBT_FREE, QBT_GRP_HANDLE[1].head_qbt_idx, FTL_SORT_NONE);
				FTL_BlockPopPushList(SPB_POOL_QBT_FREE, cand_spb_id, FTL_SORT_NONE);
			}
		}


		FTL_BlockPopPushList(SPB_POOL_UNALLOC, retire_qbt, FTL_SORT_NONE);
		ftl_apl_trace(LOG_INFO, 0xef77, "N-QBT[%d], EC:%d", cand_spb_id, spb_info_get(cand_spb_id)->erase_cnt);

		shr_ftl_flags.b.qbt_retire_need = 0;
	}
	else if(retire_qbt == INV_U16)
	{
		ftl_apl_trace(LOG_INFO, 0x5388, "[FTL]qbt_retire_handle:chk ec");
		u16 qbt_free_h= spb_mgr_get_head(SPB_POOL_QBT_FREE);
		if(qbt_free_h != INV_U16)
		{
			u8 i;
			for(i=0; i<FTL_QBT_BLOCK_CNT; i++)
	    	{
		        u16 chk_qbt = FTL_BlockPopHead(SPB_POOL_QBT_FREE);
		        chk_qbt = ftl_chk_qbt_ec(chk_qbt);
		        FTL_BlockPushList(SPB_POOL_QBT_FREE, chk_qbt, FTL_SORT_NONE);
			}
		}
	}
}
#endif
extern void ipc_api_spb_ack(u32 nsid, u32 type);
extern ftl_ns_t ftl_ns[FTL_NS_ID_MAX];
slow_code void shutdown_abort_spb(u8 nsid, u8 type)
{
    spb_queue_t *spb_queue;
    spb_ent_t ent;
    bool ret;
    
    spb_queue = ftl_ns[nsid].spb_queue[type];
    if (!CBF_EMPTY(spb_queue))
    {
        CBF_FETCH(spb_queue, ent.all);
        ftl_apl_trace(LOG_ALW, 0x2071, "nsid:%u, type:%u, abort spb:%u", nsid, type, ent.b.spb_id);
        ent.b.ps_abort = true;
        CBF_INS(spb_queue, ret, ent.all);
        if (ret == false)
        {
            ftl_apl_trace(LOG_ALW, 0x21dc, "warning!!! nsid:%u, type:%u, spb_queue is full!!", nsid, type);
        }
        ipc_api_spb_ack(nsid, type);    // call ftl_core_put_ctx, prevent ftl_core_ctx_cnt <= 0 during false power-down test

        if (nsid == FTL_NS_ID_START)
        {
            ftl_free_blist_dtag(type); // release dtag, prevent dtag_count <= 0 during false power-down test
        }
    }
}

slow_code void blk_shutdown_handle(blk_handle_t mode)
{
	if(mode == SPB_SHUTDOWN)
	{
		if((host_spb_last_idx != INV_U16) && (host_spb_last_idx != 0)  && (spb_info_get(host_spb_last_idx)->pool_id == SPB_POOL_FREE))
		{
		    ftl_apl_trace(LOG_ALW, 0xeb62, "[FTL]host_spb_last_idx %d push in pool %d", host_spb_last_idx, SPB_POOL_FREE);
			FTL_BlockPopPushList(SPB_POOL_FREE, host_spb_last_idx, FTL_SORT_BY_EC);
	        	_spb_mgr.spb_desc[host_spb_last_idx].flags = 0;
			host_spb_last_idx = INV_U16;
            shutdown_abort_spb(FTL_NS_ID_START, FTL_CORE_NRM);
		}

		if((gc_spb_last_idx != INV_U16) && (gc_spb_last_idx != 0) && (spb_info_get(gc_spb_last_idx)->pool_id == SPB_POOL_FREE))
		{
			ftl_apl_trace(LOG_ALW, 0x0522, "[FTL]gc_spb_last_idx %d push in pool %d", gc_spb_last_idx, SPB_POOL_FREE);
			FTL_BlockPopPushList(SPB_POOL_FREE, gc_spb_last_idx, FTL_SORT_BY_EC);
	       	 _spb_mgr.spb_desc[gc_spb_last_idx].flags = 0;
			gc_spb_last_idx = INV_U16;
            shutdown_abort_spb(FTL_NS_ID_START, FTL_CORE_GC);
		}
	}
	if(mode == SPB_WARMBOOT)
	{
		if((spb_get_poolid(host_spb_last_idx) != SPB_POOL_USER) && (host_spb_last_idx != INV_U16) && (host_spb_last_idx != 0))
		{
		    ftl_apl_trace(LOG_ALW, 0x8136, "[FTL]host_spb_last_idx %d push in pool %d", host_spb_last_idx, SPB_POOL_USER);
			FTL_BlockPopPushList(SPB_POOL_USER, host_spb_last_idx, FTL_SORT_BY_SN);
	        	spb_set_flag(host_spb_last_idx, (SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE|SPB_DESC_F_WARMBOOT_OPEN));
		}
		if((spb_get_poolid(gc_spb_last_idx) != SPB_POOL_USER) && (gc_spb_last_idx != INV_U16) && (gc_spb_last_idx != 0))
		{
			ftl_apl_trace(LOG_ALW, 0xcb31, "[FTL]gc_spb_last_idx %d push in pool %d", gc_spb_last_idx, SPB_POOL_USER);
			FTL_BlockPopPushList(SPB_POOL_USER, gc_spb_last_idx, FTL_SORT_BY_SN);
	       	 spb_set_flag(gc_spb_last_idx, (SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE|SPB_DESC_F_WARMBOOT_OPEN));
		}
	}
}

fast_code void chk_close_blk_push(void)
{
 	if(host_spb_close_idx != INV_U16)
 	{
		u16 close_idx;
		while (CBF_EMPTY(&close_host_blk_que) == false) {
			CBF_HEAD(&close_host_blk_que, close_idx);
	        if(spb_get_flag(close_idx) & SPB_DESC_F_RD_DIST || spb_get_flag(close_idx) & SPB_DESC_F_WEAK)
	        {
	            ftl_apl_trace(LOG_INFO, 0x7aaa, "[FTL]host close spb %d in pool %d", close_idx, SPB_POOL_GC);
	        }
	        else if(spb_info_get(close_idx)->pool_id == SPB_POOL_FREE)
	        {
	    		FTL_BlockPushList(SPB_POOL_USER, close_idx, FTL_SORT_BY_SN);
	    		ftl_apl_trace(LOG_INFO, 0x6bbe, "[FTL]host close push spb %d in pool %d", close_idx, SPB_POOL_USER);
	        }
	        else
	        {
	            ftl_apl_trace(LOG_INFO, 0xd0b9, "[FTL]host close spb %d in pool %d", close_idx, spb_info_get(close_idx)->pool_id);
	        }
            gFtlMgr.last_host_blk = INV_U16;
			host_spb_close_idx = INV_U16;
			CBF_REMOVE_HEAD(&close_host_blk_que);
		}

	}

	if(gc_spb_close_idx != INV_U16)
 	{
		u16 close_idx;
		while (CBF_EMPTY(&close_gc_blk_que) == false) {
			CBF_HEAD(&close_gc_blk_que, close_idx);
	        if(spb_get_flag(close_idx) & SPB_DESC_F_RD_DIST || spb_get_flag(close_idx) & SPB_DESC_F_WEAK)
	        {
	            if((spb_get_poolid(close_idx) != SPB_POOL_GC) && (spb_get_poolid(close_idx) != SPB_POOL_GCD))
	            {
	                FTL_BlockPushList(SPB_POOL_GC, close_idx, FTL_SORT_NONE);
	                set_open_gc_blk_rd(false);
	                shr_dummy_blk = INV_SPB_ID;
	                ftl_apl_trace(LOG_ALW, 0xb1e5, "[FTL]gc close RD SHU push spb %d in pool %d", close_idx, SPB_POOL_GC);
	            }
	            else
	            {
	            ftl_apl_trace(LOG_INFO, 0x2283, "[FTL]gc close spb %d in pool %d", close_idx, SPB_POOL_GC);
		        }
	        }
	        else if(spb_info_get(close_idx)->pool_id == SPB_POOL_FREE)
	        {
	            if(shr_dummy_blk == close_idx){
                    set_open_gc_blk_rd(false);
                    shr_dummy_blk = INV_SPB_ID;
			        ftl_apl_trace(LOG_INFO, 0x4ece, "shr_dummy_blk == close_idx %u", close_idx);
                }
			    FTL_BlockPushList(SPB_POOL_USER, close_idx, FTL_SORT_BY_SN);
			    ftl_apl_trace(LOG_INFO, 0xb78c, "[FTL]gc close push spb %d in pool %d", close_idx, SPB_POOL_USER);
	        }
	        else
	        {
	            ftl_apl_trace(LOG_INFO, 0x7c0d, "[FTL]gc close spb %d in pool %d", close_idx, spb_info_get(close_idx)->pool_id);
	        }
            gFtlMgr.last_gc_blk = INV_U16;
			gc_spb_close_idx = INV_U16;
			CBF_REMOVE_HEAD(&close_gc_blk_que);
		}

	}

}

#if (SPOR_FLOW == mDISABLE)
ddr_code sn_t find_max_sn(void)
{
	sn_t max_sn = 0;
	spb_desc_t *desc = _spb_mgr.spb_desc;
	u16  i;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for(i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) {
#else
	for(i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) {
#endif
		if(desc[i].sn >= max_sn)
		{
			max_sn = desc[i].sn;
		}
	}
	return max_sn;
}
#endif

fast_code u32 find_min_sn_spb(u32 prev_sn)
{

	sn_t min_sn = INV_U32;
    u32 spb = 0, min_sn_spb = INV_U16;
	spb_desc_t *desc = _spb_mgr.spb_desc;
    ftl_apl_trace(LOG_INFO, 0xb944, "patrol read spb pool id or sn changed, rescan");
    spb = spb_mgr_get_head(SPB_POOL_USER);
    while(spb != INV_U16)
    {
        if((desc[spb].sn > prev_sn) && (desc[spb].sn < min_sn)){
			min_sn = desc[spb].sn;
            min_sn_spb = spb;
            //break;
        }
        spb = spb_info_tbl[spb].block;
    }
    //ftl_apl_trace(LOG_INFO, 0, "spb %d sn %d min_sn %d", spb, desc[spb].sn, min_sn);
	return min_sn_spb;
}

static ddr_code int Dump_BLKinfo(int argc, char *argv[])
{
	u32 i;
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt(); i++) {
	ftl_apl_trace(LOG_ALW, 0x0536, "[FTL]SBlk[%d] EC:[0x%x] RC:[0x%x] POH:[%d]",
					i, spb_info_get(i)->erase_cnt, _spb_mgr.spb_desc[i].rd_cnt, spb_info_get(i)->poh);
	}

	return 0;
}

static ddr_code int Dump_BLKlist(int argc, char *argv[])
{
	u32 i;
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt(); i++) {
	ftl_apl_trace(LOG_ALW, 0x6f08, "[FTL]SBlk[%d] Next:[%d] pool:[%d] f:[0x%x] swf:[0x%x] nid:[%d]",
					i, spb_info_get(i)->block, spb_info_get(i)->pool_id, spb_get_flag(i),_spb_mgr.sw_flags[i], _spb_mgr.spb_desc[i].ns_id);
	}

	return 0;
}

static ddr_code int Dump_BLKtable(int argc, char *argv[])
{
	spb_pool_table();

	return 0;
}

static ddr_code int Dump_PoolBlk(int argc, char *argv[])
{
	u8 i;
    spb_id_t spb;
	for (i = 0; i < SPB_POOL_MAX; i++) {;
        spb = _spb_mgr.pool_info.head[i];
        while (spb != INV_SPB_ID)
        {
        	ftl_apl_trace(LOG_ALW, 0xad67, "[FTL]Pool[%d] SBlk[%d] Next:[%d] EC:[0x%x]",
        		spb_info_get(spb)->pool_id, spb, spb_info_get(spb)->block, spb_info_get(spb)->erase_cnt);
            spb = spb_info_get(spb)->block;
        }
	}

	return 0;
}


static DEFINE_UART_CMD(blkinfo, "blkinfo", "show blkinfo","show blkinfo", 0, 0, Dump_BLKinfo);
static DEFINE_UART_CMD(blklist, "blklist", "show blklist","show blklist", 0, 0, Dump_BLKlist);
static DEFINE_UART_CMD(blk, "blk", "show blktable","show blklist", 0, 0, Dump_BLKtable);
static DEFINE_UART_CMD(poolblk, "poolblk", "show pool spb","show pool spb", 0, 0, Dump_PoolBlk);
#endif


init_code void spb_mgr_rebuild_free_cnt(void)
{
	u32 i;
	u32 *vc;
	u32 dtag_cnt;
	spb_desc_t *desc = _spb_mgr.spb_desc;
	dtag_t dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void**)&vc);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		if ((desc[i].flags & SPB_DESC_F_CLOSED) && (vc[i] == 0)) {
			sys_assert(desc[i].flags & SPB_DESC_F_BUSY);

			if (spb_get_sw_flag(i) & SPB_SW_F_FREEZE)
				spb_set_sw_flag(i, SPB_SW_F_VC_ZERO);
			else
				spb_release(i);
		}
	}

	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
}
/*
fast_code bool is_dslc_enabled(void)
{
	dslc_statistics_t *dslc = &_spb_mgr.dslc_data;

	if (dslc->slc_used < dslc->slc_max && dslc->xlc_used < dslc->xlc_max && dslc->slc_avail)
		return true;
	else
		return false;
}
*/
fast_code u32 spb_get_free_cnt(u32 pool)
{
	return _spb_mgr.pool_info.free_cnt[pool];
}

fast_code void spb_allocation_trigger(spb_appl_t *appl)
{
	if (appl && appl->flags.b.triggered == 0) {
		appl->flags.b.triggered = 1;
		QSIMPLEQ_INSERT_TAIL(&_spb_mgr.appl_queue, appl, link);
	} else {
		if (QSIMPLEQ_EMPTY(&_spb_mgr.appl_queue)) {
			fsm_queue_run(&_spb_mgr.allocate_wait_que);
			return;
		}
	}

	evt_set_cs(_spb_mgr.evt_spb_alloc, 0, 0, CS_TASK); //spb_allocation_handler
}

fast_code void spb_allocation_handler(u32 param, u32 payload, u32 sts)
{
	spb_appl_t *appl;
	spb_appl_t *next;

	if (_spb_mgr.flags.b.allocating)
		return;
    chk_close_blk_push();
	QSIMPLEQ_FOREACH_SAFE(appl, &_spb_mgr.appl_queue, link, next) {
		u32 can;
        if (plp_trigger)
        {
            if (!(appl->ns_id == FTL_NS_ID_START && appl->type == FTL_CORE_NRM))
            {
                continue;
            }
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
            else if(SLC_init == true)
            {
                continue;
            }
#endif
        }
		can = spb_appl_get_spb(appl);
		if (can == ~0) {
			ftl_apl_trace(LOG_ALW, 0xb937, "no spb for %d", appl->ns_id);
			continue;
		}
		shr_pstream_busy = true;
		appl->flags.b.allocating = 1;

		_spb_mgr.flags.b.allocating = 1;
		appl->spb_id = can;

		_spb_mgr.spb_desc[can].ns_id = appl->ns_id;

        erase_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(erase_ctx_t));
		sys_assert(ctx);

		ctx->spb_ent.all = can;
		ctx->spb_ent.b.slc = appl->flags.b.slc | appl->flags.b.dslc;
		ctx->spb_ent.b.erFal = 0;	// ISU, SPBErFalHdl

		ctx->cmpl_cnt = 0;
		ctx->issue_cnt = 0;
		ctx->caller = appl;
		ctx->cmpl = spb_erase_done;

		// erase as soon as possible
		ftl_core_erase(ctx);
        ftl_apl_trace(LOG_INFO, 0x8ae8, "[EC]max: %d, min: %d, tot: %d, avg: %d, spb[%d].ec: %d", spb_ec_tbl->header.MaxEC, spb_ec_tbl->header.MinEC, spb_ec_tbl->header.TotalEC, spb_ec_tbl->header.AvgEC, can, spb_ec_tbl->EcCnt[can]);

        if((appl->ns_id == FTL_NS_ID_START) && (appl->type == FTL_CORE_NRM))
		{
			ftl_apl_trace(LOG_INFO, 0x3096, "[FTL]alloc %d, ns %d(%d) for Host", appl->spb_id, appl->ns_id, appl->type);
			if(host_spb_last_idx != INV_U16){
				host_spb_pre_idx = host_spb_last_idx;
			}
        	host_spb_last_idx = appl->spb_id;
            gFtlMgr.last_host_blk = host_spb_last_idx;
            #if 0 //RAID_SUPPORT_UECC
            smart_uecc_fill_host_blk_info(appl->spb_id, true);
            #endif
			//ftl_apl_trace(LOG_ERR, 0, "host_spb_last_idx %d", host_spb_last_idx);
		}

		if((appl->ns_id == FTL_NS_ID_START) && (appl->type == FTL_CORE_GC))
		{
			ftl_apl_trace(LOG_INFO, 0xa053, "[FTL]alloc %d, ns %d(%d) for GC", appl->spb_id, appl->ns_id, appl->type);
        	gc_spb_last_idx = appl->spb_id;
            gFtlMgr.last_gc_blk = gc_spb_last_idx;
            #if 0 //RAID_SUPPORT_UECC
            smart_uecc_fill_host_blk_info(appl->spb_id, true);
            #endif
			//ftl_apl_trace(LOG_INFO, 0, "gc_spb_last_idx %d", gc_spb_last_idx);
		}

		if(appl->ns_id == FTL_NS_ID_START){
			ftl_blklist_copy(appl->ns_id,appl->type);
		}

		//spb_info_flush(can); Curry 20200803

		if((appl->ns_id == FTL_NS_ID_INTERNAL) && (appl->type == FTL_CORE_PBT))
		{
            #if 0 //RAID_SUPPORT_UECC
            smart_uecc_fill_host_blk_info(appl->spb_id, false);
            #endif
			ftl_apl_trace(LOG_INFO, 0x2ba9, "[FTL]alloc %d, ns %d(%d) for PBT", appl->spb_id, appl->ns_id, appl->type);
		}
		spb_ReleaseEmptyPool();

	#if CROSS_TEMP_OP
		if(is_scan_over_temp_timer_del)
		{
			is_scan_over_temp_timer_del = false;
			mod_timer(&spb_scan_over_temp_blk_timer, jiffies + HZ * 10); // Set 10s delay
		}
	#endif
		// clearn rd cnt table here instead of erase done to save time
		clean_rd_cnt_counter(appl->spb_id);

		QSIMPLEQ_REMOVE(&_spb_mgr.appl_queue, appl, _spb_appl_t, link);
		break;
	}
}

fast_code void spb_special_erase_trigger(u16 blk)
{
	evt_set_cs(_spb_mgr.evt_special_erase_spb, (u32)blk, 0, CS_TASK); //spb_special_erase
}

slow_code void spb_special_erase(u32 param, u32 payload, u32 sts)
{
	u32 rd_blk = payload;
	if(_spb_mgr.flags.b.allocating)
	{
		evt_set_cs(_spb_mgr.evt_special_erase_spb, (u32)rd_blk, 0, CS_TASK);
	}
	else
	{
		ftl_apl_trace(LOG_INFO, 0x24f5, "[SPB]spb_special_erase %d", rd_blk);
		_spb_mgr.flags.b.allocating = 1;
		erase_ctx_t *ctx = sys_malloc(FAST_DATA, sizeof(erase_ctx_t));
		sys_assert(ctx);

		ctx->spb_ent.all = rd_blk;
		ctx->spb_ent.b.slc = 0;
		ctx->cmpl_cnt = 0;
		ctx->issue_cnt = 0;
		ctx->caller = (void*)(u32)rd_blk;
		ctx->cmpl = spb_special_erase_done;
		ftl_core_erase(ctx);
	}
}


#if (SPB_BLKLIST == mENABLE)
fast_code void pbt_spb_soft(spb_id_t spb_id){
	//ftl_ns_qbf_clear(FTL_NS_ID_INTERNAL,SPB_POOL_PBT);
	if(_spb_mgr.pool_info.free_cnt[SPB_POOL_PBT] >= 2*QBT_BLK_CNT){
		u8 i;
		u16 release_spb;
		record_full_pbt_host = gFtlMgr.pbt_host_blk;
		record_full_pbt_pre_host = gFtlMgr.pbt_pre_host_blk;
		record_full_pbt_gc = gFtlMgr.pbt_gc_blk;
		gFtlMgr.PrevTableSN = spb_get_sn(_spb_mgr.pool_info.head[SPB_POOL_PBT]);
		epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		epm_ftl_data->record_PrevTableSN = gFtlMgr.PrevTableSN;
		if( epm_ftl_data->pbt_force_flush_flag != 0)
		{
			epm_update(FTL_sign,(CPU_ID-1)); 
		}
	    ftl_apl_trace(LOG_INFO, 0xdcb9, "gFtlMgr.PrevTableSN:0x%x host:%d pre_h:%d gc:%d", 
	    	gFtlMgr.PrevTableSN,record_full_pbt_host,record_full_pbt_pre_host,record_full_pbt_gc);
		for(i = 0; i < QBT_BLK_CNT; i++){
			release_spb = _spb_mgr.pool_info.head[SPB_POOL_PBT];
			ftl_apl_trace(LOG_INFO, 0xefb4, "[spb]free older pbt:%d, to pool:%d, presn:0x%x",release_spb,SPB_POOL_FREE,gFtlMgr.PrevTableSN);
			FTL_BlockPopPushList(SPB_POOL_FREE,release_spb,FTL_SORT_BY_EC);
//			_spb_mgr.spb_desc[release_spb].pool_id 	= SPB_POOL_FREE;
			_spb_mgr.spb_desc[release_spb].flags 	= 0;
			_spb_mgr.sw_flags[release_spb] 			= 0;
		}
	}
}


fast_code spb_id_t spb_appl_get_spb(spb_appl_t *appl)
{
	u32 tar;
	u32 pool_id;

	spb_ec_tbl = (Ec_Table* )shr_ec_tbl_addr;

	if(appl->flags.b.QBT)
		pool_id = SPB_POOL_QBT_FREE;
	else
		//pool_id = SPB_POOL_NATIVE;
		pool_id = SPB_POOL_FREE;

	appl->pool_id = pool_id;

reopen_blk:
	if (appl->flags.b.older)
    {
		tar = spb_pop_old(pool_id);
        appl->flags.b.older = 0;
    }
	else
		tar = spb_pop_young(pool_id);

	/*Push SPB from SPB_POOL_QBT_FREE to SPB_POOL_QBT_ALLOC*/
	if(appl->flags.b.QBT){
		sys_assert(tar != INV_U16);
		FTL_BlockPushList(SPB_POOL_QBT_ALLOC, tar, FTL_SORT_NONE);
		ftl_apl_trace(LOG_INFO, 0x0382, "[FTL]qbtblk %d to qbt alloc cnt %d sw_flag %d", tar, spb_get_free_cnt(SPB_POOL_QBT_ALLOC), spb_get_sw_flag(tar));
	}

	if (tar != INV_SPB_ID) {
		spb_info_get(tar)->erase_cnt++;
        Ec_tbl_update(tar);

        #if ENABLE_WL
        u32 max_sn = gFtlMgr.SerialNumber;
        u32 min_sn = spb_get_sn(spb_mgr_get_head(SPB_POOL_USER));
		u32 free_erase_cnt = spb_info_get(spb_mgr_get_head(SPB_POOL_FREE))->erase_cnt;
//        if(max_sn-min_sn > get_total_spb_cnt() && (spb_ec_tbl->header.MaxEC - user_min_ec > WL_EC_THRESHOLD) && (!GC_MODE_CHECK_STATIC_WL))
        if((spb_ec_tbl->header.MaxEC - user_min_ec > WL_EC_THRESHOLD) && ((max_sn-min_sn) >  get_total_spb_cnt()) && (!GC_MODE_CHECK_STATIC_WL))
        {
            if((spb_get_sn(user_min_ec_blk) >= gFtlMgr.PrevTableSN) || (spb_get_poolid(user_min_ec_blk) != SPB_POOL_USER)
				|| (spb_ec_tbl->header.MaxEC - spb_info_get(user_min_ec_blk)->erase_cnt <= WL_EC_THRESHOLD) || (user_min_ec_blk == INV_U16))
            {
                rescan_ec(appl->ns_id,SPB_POOL_USER);
            }
            ftl_apl_trace(LOG_INFO, 0x513f, "[WL] max sn %d ec %d min sn %d ec %d blk %d",max_sn,spb_ec_tbl->header.MaxEC,min_sn,user_min_ec,user_min_ec_blk);
			ftl_apl_trace(LOG_INFO, 0x7925, "[WL] free head spb[%d] ec %d", spb_mgr_get_head(SPB_POOL_FREE), free_erase_cnt);
            if((user_min_ec_blk != pre_wl_blk) && (spb_ec_tbl->header.MaxEC - user_min_ec > WL_EC_THRESHOLD) && !ctl_wl_flag 
				&& ((free_erase_cnt >= user_min_ec) && (free_erase_cnt - user_min_ec > 50)))
            {
                GC_MODE_SETTING(GC_STATIC_WL);
                pre_wl_blk =  user_min_ec_blk;
                ftl_apl_trace(LOG_INFO, 0x8c6e, "[WL] gc blk[%d] pre_wl_blk[%d]", get_gc_blk(),pre_wl_blk);
                user_min_ec_blk = INV_U16;
                //user_min_ec = INV_U32;
                //For case: when Force GC open new blk trigger WL, suspend to do WL
                if(appl->ns_id == FTL_NS_ID_START && appl->type == FTL_CORE_GC && gc_get_mode() == GC_MD_STATIC_WL && global_gc_mode != GC_MD_EMGR)
                {
                	spb_info_get(tar)->erase_cnt--;
					spb_ec_tbl = (Ec_Table* )shr_ec_tbl_addr;
					spb_ec_tbl->EcCnt[tar] = spb_info_get(tar)->erase_cnt;
					spb_ec_tbl->header.TotalEC--;
					ftl_apl_trace(LOG_INFO, 0x087a, "[WL] Force GC trigger WL, open old");
					FTL_BlockPushList(pool_id, tar, FTL_SORT_BY_EC);
					appl->flags.b.older = 1;
					gc_suspend_start_wl = true;
					goto reopen_blk;
            	}
				ftl_ns_gc_start_chk(HOST_NS_ID);
            }
			else if(ctl_wl_flag)
			{
				ctl_wl_flag = false;
			}
        }
        else if((pre_wl_blk == user_min_ec_blk) || (spb_ec_tbl->header.MaxEC - user_min_ec < WL_EC_THRESHOLD) || ((max_sn-min_sn) <  get_total_spb_cnt()))
        {
        	ctl_wl_flag = false;
            GC_MODE_CLEAN(GC_STATIC_WL);
        }
		#endif
		// todo: issue spb info flush

		_spb_mgr.sw_flags[tar] |= SPB_SW_F_ALLOCATING;
		//sys_assert(_spb_mgr.pool_info.free_cnt[pool_id]);
		//_spb_mgr.pool_info.free_cnt[pool_id]--;
		/*
		if (pool_id == SPB_POOL_QBT_FREE){
        	FTL_BlockPushList(SPB_POOL_QBT_ALLOC, tar, FTL_SORT_NONE);
			_spb_mgr.spb_desc[tar].pool_id = appl->pool_id = SPB_POOL_QBT_ALLOC;
		    ftl_apl_trace(LOG_INFO, 0, "[FTL] pool %d cnt %d", SPB_POOL_QBT_ALLOC, _spb_mgr.pool_info.free_cnt[SPB_POOL_QBT_ALLOC]);
		}
		*/
		//ftl_apl_trace(LOG_INFO, 0, "[FTL] pool %d free %d", pool_id, _spb_mgr.pool_info.free_cnt[pool_id]);
		#if (PBT_OP == mENABLE)
		if(appl->type == FTL_CORE_PBT){
			ftl_apl_trace(LOG_INFO, 0x3188, "[spb]pbt:%d,  defect:%d",tar,spb_get_defect_cnt(tar));
			FTL_BlockPushList(SPB_POOL_PBT_ALLOC, tar, FTL_SORT_NONE);
			//_spb_mgr.spb_desc[tar].pool_id = appl->pool_id = SPB_POOL_PBT_ALLOC;
			_spb_mgr.sw_flags[tar] |= (SPB_SW_F_ALLOCATING | SPB_SW_F_PBT_ALLOC);
		}
		#endif
	} else {
		panic("stop");
	}

	return tar;
}


fast_code spb_id_t spb_pop_old(u32 pool_id)
{
	//TODO: GC select proper spb
	spb_id_t tar, tail, next;
    u16 ec_next;
again:
    tar = INV_SPB_ID;
    tail = _spb_mgr.pool_info.tail[pool_id];
    if(((spb_info_get(tail)->erase_cnt - user_min_ec) > WL_EC_GC_OPEN_THRESHOLD)
        && (_spb_mgr.pool_info.free_cnt[pool_id] < 20)){
        tar = _spb_mgr.pool_info.head[pool_id];
        while(tar != INV_SPB_ID && spb_info_tbl[tar].block != INV_SPB_ID
            && ((spb_info_get(spb_info_tbl[tar].block)->erase_cnt - user_min_ec) <= WL_EC_GC_OPEN_THRESHOLD)){
            tar = spb_info_tbl[tar].block;  //get next one
        }
    }

    if(tar == INV_SPB_ID){
        tar = tail;
    }
    next = spb_info_tbl[tar].block;
    if(next != INV_SPB_ID){
        ec_next = spb_info_get(next)->erase_cnt;
    }else{
        ec_next = INV_U16;
    }
    ftl_apl_trace(LOG_INFO, 0xae26, "[WL]pop open(old): [%u]%u, next: [%u]%u, tail: [%u]%u",
        tar, spb_info_get(tar)->erase_cnt, next, ec_next, tail, spb_info_get(tail)->erase_cnt);
	FTL_BlockPopList(pool_id, tar);
	sys_assert(tar!=INV_SPB_ID);
	if (is_free_spb(tar) == mFALSE){
		dtag_t dtag;
		u32 *vc;
		u32 cnt;
		dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
		if(vc[tar] == 0)
		{
			_spb_mgr.spb_desc[tar].flags = 0;
			_spb_mgr.sw_flags[tar] = 0;
		}
		else
		{
			ftl_apl_trace(LOG_INFO, 0xaf03, "[spb]blk vac not 0 in free pool");
			sys_assert(0);
		}
		ftl_l2p_put_vcnt_buf(dtag, cnt, false);
		ftl_apl_trace(LOG_INFO, 0x0c51, "[spb]block select again");
		FTL_BlockPushList(pool_id, tar, FTL_SORT_NONE);
		goto again;
	}
	#if CROSS_TEMP_OP
	spb_info_tbl[tar].flags &= ~SPB_INFO_F_OVER_TEMP;
	#endif
	return tar;

}

fast_code spb_id_t spb_pop_young(u32 pool_id)
{
	// need MIN EC block
	spb_id_t tar = INV_SPB_ID;

#ifdef SKIP_MODE
	u32 interleave = get_interleave();
	u32 thr = interleave - min_good_pl;   //TLC need 212 good interleave (4T:256 interleave)
#endif

again:
		tar = FTL_BlockPopHead(pool_id);
		sys_assert(tar!=INV_SPB_ID);

        if (tar == last_spb_vcnt_zero && pool_id == SPB_POOL_FREE)
        {
            spb_id_t tmp_tar = tar;
            tar = FTL_BlockPopHead(pool_id);
            sys_assert(tar!=INV_SPB_ID);
            ftl_apl_trace(LOG_INFO, 0x15e8, "[spb]last_spb_vcnt_zero:%u, tar:%u", last_spb_vcnt_zero, tar);
			FTL_BlockPushList(SPB_POOL_FREE, tmp_tar, FTL_SORT_BY_EC);
        }


#ifdef SKIP_MODE
		if (spb_get_defect_cnt(tar) > thr)
#else
		if (spb_get_defect_cnt(tar) != 0)
#endif
		{
			ftl_apl_trace(LOG_INFO, 0xab2d, "[spb]defect over thr block select again error");
			FTL_BlockPushList(SPB_POOL_UNALLOC, tar, FTL_SORT_NONE);
			goto again;
		}
		if (is_free_spb(tar) == mFALSE){
			dtag_t dtag;
			u32 *vc;
			u32 cnt;
			dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
			if(vc[tar] == 0)
			{
				_spb_mgr.spb_desc[tar].flags = 0;
				_spb_mgr.sw_flags[tar] = 0;
			}
			else
			{
				ftl_apl_trace(LOG_INFO, 0xd4dc, "[spb]blk %d vac 0x%x not 0 in free",tar,vc[tar]);
				sys_assert(0);
			}
			ftl_l2p_put_vcnt_buf(dtag, cnt, false);
			ftl_apl_trace(LOG_INFO, 0x62d8, "[spb]block select again");
			FTL_BlockPushList(pool_id, tar, FTL_SORT_NONE);
			goto again;
		}
	#if CROSS_TEMP_OP
	spb_info_tbl[tar].flags &= ~SPB_INFO_F_OVER_TEMP;
	#endif
	return tar;
}

fast_code void spb_ReleaseEmptyPool(void)
{
    u16 blk;
    u16 nxt_blk;

    //ftl_apl_trace(LOG_INFO, 0, "[spb]spb_ReleaseEmptyPool");
#if (PLP_SUPPORT == 0)
    if((_spb_mgr.pool_info.free_cnt[SPB_POOL_EMPTY] == 0) || GC_MODE_CHECK_NON_PLP)
#else
	if(_spb_mgr.pool_info.free_cnt[SPB_POOL_EMPTY] == 0)
#endif
		return;

	u32 max_release_cnt = 3;
	if(_spb_mgr.pool_info.free_cnt[SPB_POOL_FREE] < GC_BLKCNT_DEACTIVE + 3)
	{
		max_release_cnt = GC_BLKCNT_DEACTIVE + 6 - _spb_mgr.pool_info.free_cnt[SPB_POOL_FREE];
	}

    blk = _spb_mgr.pool_info.head[SPB_POOL_EMPTY];
    while(blk!=INV_U16 && max_release_cnt)
    {
        nxt_blk = spb_info_tbl[blk].block;
        if(((spb_get_sn(blk) < gFtlMgr.PrevTableSN))
            && (blk != record_full_pbt_pre_host) && (blk != record_full_pbt_host) && (blk != record_full_pbt_gc))
        {
        	//TODO : need to confirm here after SPOR cut in
        	//if ((blk != gFtlMgr.PBT_PreOpenHostPta.fields.block) &&
            //    (blk != gFtlMgr.PBT_PreOpenGCPta.fields.block) &&
            //    (blk != gPBT_Last_PreOpenHostPta.fields.block) && // for PreHost/GC cannot be GC and free, Sunny 20200424
            //    (blk != gPBT_Last_PreOpenGCPta.fields.block))     // for PreHost/GC cannot be GC and free, Sunny 20200424
        		{
		        	ftl_apl_trace(LOG_INFO, 0xc72e, "spb %d(0x%x), TableSN 0x%x",blk,spb_get_sn(blk),gFtlMgr.TableSN);
		        	FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
                    _spb_mgr.spb_desc[blk].flags = 0;
             		_spb_mgr.sw_flags[blk] = 0;
					max_release_cnt--;
                }
        }

        blk = nxt_blk;
    }
}

#else
fast_code spb_id_t spb_appl_get_spb(spb_appl_t *appl)
{
	u32 tar;
	u32 pool_id;
	//ftl_ns_t *ns = ftl_ns_get(appl->ns_id);

	if (appl->flags.b.QBT)
		pool_id = SPB_POOL_QBT_FREE;
	else
		pool_id = SPB_POOL_FREE;

	appl->pool_id = pool_id;

	if (appl->flags.b.older)
		tar = spb_pop_old(pool_id);
	else
		tar = spb_pop_young(pool_id, appl->flags.b.dslc);

	if (tar != INV_SPB_ID) {
		spb_info_get(tar)->erase_cnt++;
		Ec_tbl_update(tar);

		// todo: issue spb info flush

		_spb_mgr.sw_flags[tar] |= SPB_SW_F_ALLOCATING;
		sys_assert(_spb_mgr.pool_info.free_cnt[pool_id]);
		_spb_mgr.pool_info.free_cnt[pool_id]--;
		ftl_apl_trace(LOG_INFO, 0x278f, "pool[%d] free %d", pool_id, _spb_mgr.pool_info.free_cnt[pool_id]);
	}
	else {
		panic("stop");
	}

	return tar;
}

fast_code spb_id_t spb_pop_old(u32 pool_id)
{
	u32 i;
	spb_id_t tar = INV_SPB_ID;
	u32 max_ec = 0;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		u32 ec;

		if (pool_id != spb_get_poolid(i))
			continue;

		if (is_free_spb(i) == false)
			continue;

		ec = spb_info_get(i)->erase_cnt;
		if (ec >= max_ec) {
			max_ec = ec;
			tar = i;
		}
	}

	return tar;
}

fast_code spb_id_t spb_pop_young(u32 pool_id, bool dslc)
{
	u32 i;
	u32 min_ec = 0xffffffff;
	spb_id_t tar = INV_SPB_ID;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		if (pool_id != spb_get_poolid(i))
			continue;

		if (is_free_spb(i) == false)
			continue;

		u32 df_cnt = spb_get_defect_cnt(i);
		u32 ec = spb_info_get(i)->erase_cnt;
		if ((dslc == false) || (df_cnt == 0)) {
			if (ec < min_ec) {
				tar = i;
				min_ec = ec;
			}
		}
	}

	return tar;
}
#endif

init_code void spb_set_sw_flags_zero(spb_id_t spb_id)
{
	_spb_mgr.sw_flags[spb_id] = 0;
}

init_code void spb_set_desc_flags_zero(spb_id_t spb_id)
{
	_spb_mgr.spb_desc[spb_id].flags = 0;
}

fast_code spb_desc_t* spb_get_desc(spb_id_t spb_id)
{
	return &_spb_mgr.spb_desc[spb_id];
}

fast_code void spb_set_sn(spb_id_t spb_id, sn_t sn)
{
	_spb_mgr.spb_desc[spb_id].sn = sn;
}

fast_code sn_t spb_get_sn(spb_id_t spb_id)
{
	return _spb_mgr.spb_desc[spb_id].sn;
}

fast_code u8 spb_get_nsid(spb_id_t spb_id)
{
	return _spb_mgr.spb_desc[spb_id].ns_id;
}

fast_code u8 spb_get_poolid(spb_id_t spb_id)
{
    return spb_info_tbl[spb_id].pool_id;
//	return _spb_mgr.spb_desc[spb_id].pool_id;
}

/*!
 * @brief callback function when system log flush was pended
 *
 * @param sld_flush	system log flush context
 *
 * @return		not used
 */
fast_code void spb_desc_flush_wait(sld_flush_t *sld_flush)
{
	spb_appl_t *appl = (spb_appl_t*) sld_flush->caller;

	_spb_mgr.flags.b.sld_flush_used = 0;
	if (appl) {
		ftl_apl_trace(LOG_INFO, 0xd6d2, "spb flush resume %d %d", appl->spb_id, appl->ns_id);
		spb_alloc_desc_flush(appl);
	} else {
		ftl_apl_trace(LOG_INFO, 0xb335, "spb flush all resume");
		spb_mgr_flush_desc();
	}
}

/*!
 * @brief callback function when spb descriptor system log flush done
 *
 * @note will notify applicant and continue to spb allocation
 *
 * @param sld_flush		system log flush context
 *
 * @return			not used
 */
fast_code void spb_desc_flush_done(sld_flush_t *sld_flush)
{
	spb_appl_t *appl = (spb_appl_t*) sld_flush->caller;

	appl->flags.b.ready = 1;
	appl->flags.b.triggered = 0;
	appl->flags.b.flushing = 0;
	_spb_mgr.sw_flags[appl->spb_id] &= ~SPB_SW_F_ALLOCATING;
	appl->notify_func(appl);

	_spb_mgr.flags.b.allocating = 0;
	_spb_mgr.flags.b.sld_flush_used = 0;
	spb_allocation_trigger(NULL);
}

fast_code void spb_alloc_desc_flush(spb_appl_t *appl)
{
	u32 ele = appl->spb_id;
	spb_desc_t *desc;

	if (is_sys_log_flushing()) {
		if (_spb_mgr.flags.b.sld_flush_used == 0) {
			_spb_mgr.sld_flush.caller = appl;
			_spb_mgr.sld_flush.cmpl = spb_desc_flush_wait;
			_spb_mgr.flags.b.sld_flush_used = 1;
			sys_log_flush_wait(&_spb_mgr.sld_flush);
		} else {
			sys_assert(_spb_mgr.sld_flush.caller == NULL);
			_spb_mgr.sld_flush.caller = appl;
		}
		ftl_apl_trace(LOG_WARNING, 0xd43d, "spb alloc wait spb %d %d", appl->spb_id, appl->ns_id);
		return;
	}

	sys_log_cache_on();

	desc = &_spb_mgr.spb_desc[appl->spb_id];
	desc->rd_cnt = 0;
	desc->ns_id = appl->ns_id;
	desc->flags |= SPB_DESC_F_BUSY;
	if (appl->flags.b.dslc)
		desc->flags |= SPB_DESC_F_DSLC;

	if (appl->type == FTL_CORE_GC)
		desc->flags |= SPB_DESC_F_GC_SPB;

	appl->flags.b.flushing = 1;
	appl->notify_func(appl);

	_spb_mgr.sld_flush.caller = appl;
	_spb_mgr.sld_flush.cmpl = spb_desc_flush_done;

	if (_spb_mgr.flags.b.desc_flush_all) {
		ele = ~0;
		_spb_mgr.flags.b.desc_flush_all = 0;
	}
	_spb_mgr.flags.b.sld_flush_used = 1;
	sys_log_desc_flush(&_spb_mgr.slog_desc, ele, &_spb_mgr.sld_flush);
	sys_log_cache_off();
}

fast_code void spb_erase_done(erase_ctx_t *ctx)
{
	spb_appl_t *appl = (spb_appl_t *)ctx->caller;
	spb_desc_t *desc;
    spb_id_t spb_id_erased = appl->spb_id;

	// ISU, SPBErFalHdl
	if (ctx->spb_ent.b.erFal)
	{
		// ISU, TSSP, PushGCRelsFalInfo
		MK_FailBlk_Info failBlkInfo;
		failBlkInfo.all = 0;
		failBlkInfo.b.wBlkIdx = ctx->spb_ent.b.spb_id;
		failBlkInfo.b.need_gc = false;
		ErrHandle_Task(failBlkInfo);	// CPU3, trigger mark bad directly.
		#if 1
		// ISU, ErFalRetired
		u16 spbId = ctx->spb_ent.b.spb_id;
		if (spb_info_tbl[spbId].pool_id == SPB_POOL_UNALLOC)
		{
			spb_set_sw_flags_zero(spbId);
	        spb_set_desc_flags_zero(spbId);
			sys_free(FAST_DATA, ctx);

			// Let it allocate another SPB again.
			appl->flags.b.triggered = 0;
			_spb_mgr.sw_flags[appl->spb_id] &= ~SPB_SW_F_ALLOCATING;
			_spb_mgr.flags.b.allocating = 0;
			spb_allocation_trigger(appl);

			ftl_apl_trace(LOG_INFO, 0xc272, "[EH] ErFal, spb[%d] retired, pop ano blk", spbId);
			shr_pstream_busy = false;
			return;
		}
		#endif
	}


//u32 time = ((u32)get_tsc_64() - timerL) / 800;//adams

//////////////////////////////// Curry 20200619

    last_spb_vcnt_zero = INV_SPB_ID;
	appl->flags.b.ready = 1;
	appl->flags.b.triggered = 0;
	appl->flags.b.flushing = 0;
	//_spb_mgr.sw_flags[appl->spb_id] &= ~SPB_SW_F_ALLOCATING;
    desc = &_spb_mgr.spb_desc[appl->spb_id];
	desc->rd_cnt = 0;
	desc->ns_id = appl->ns_id;
	desc->flags |= SPB_DESC_F_BUSY;

#if (TRIM_SUPPORT == ENABLE)
    clear_bit(appl->spb_id, TrimBlkBitamp);
#endif
    //clean_rd_cnt_counter(appl->spb_id);    move to erase start

    // notify cpu2 as soon as possible
	appl->notify_func(appl);

    spb_info_tbl[spb_id_erased].poh = (poh + (jiffies / 36000));
    ftl_apl_trace(LOG_INFO, 0x7223, "spb[%d] opened time: %d", spb_id_erased, spb_info_tbl[spb_id_erased].poh);
	_spb_mgr.flags.b.allocating = 0;
  	sys_free(FAST_DATA, ctx);
	spb_allocation_trigger(NULL);
	shr_pstream_busy = false;
//////////////////////////////////
	//spb_alloc_desc_flush(appl);
}

fast_code void spb_special_erase_done(erase_ctx_t *ctx)
{
	u32 blk = (u32)ctx->caller;
	sys_free(FAST_DATA, ctx);
	_spb_mgr.flags.b.allocating = 0;
	ftl_apl_trace(LOG_INFO, 0xdf8e, "[SPB]spb_special_erase_done %d", blk);
	spb_release((u16)blk);
}

ddr_code int print_poh_tbl(int argc, char *argv[])	// Save atcm code size, ISU, SPBErFalHdl
{
	u16 i;

    for(i = 0;i<get_total_spb_cnt();i++)
    {
        ftl_apl_trace(LOG_ERR, 0x3f21, "spb[%d] poh: %d\n", i, spb_info_tbl[i].poh);
    }

	return 0;
}

static DEFINE_UART_CMD(print_poh_tbl, "print_poh_tbl", "print_poh_tbl", "print_poh_tbl", 0, 0, print_poh_tbl);
fast_code void spb_set_flag(spb_id_t spb_id, u32 flag)
{
	_spb_mgr.spb_desc[spb_id].flags |= (flag);

	if (flag & SPB_DESC_F_OPEN) {
//		u32 pool_id = _spb_mgr.spb_desc[spb_id].pool_id;
        u32 pool_id = spb_get_poolid(spb_id);
		_spb_mgr.pool_info.open_cnt[pool_id]++;
		_spb_mgr.ttl_open++;
	}
}

fast_code void spb_clear_flag(spb_id_t spb_id, u32 flag)
{
	_spb_mgr.spb_desc[spb_id].flags &= ~(flag);
}

fast_code u16 spb_get_flag(spb_id_t spb_id)
{
	return _spb_mgr.spb_desc[spb_id].flags;
}

fast_code bool is_spb_slc(spb_id_t spb_id)
{
	if (_spb_mgr.spb_desc[spb_id].flags & SPB_DESC_F_DSLC)
		return true;

	return false;
}

fast_code bool is_spb_gc_write(spb_id_t spb_id)
{
	if (_spb_mgr.spb_desc[spb_id].flags & SPB_DESC_F_GC_SPB)
		return true;
	else
		return false;
}

slow_code void spb_set_sw_flag(spb_id_t spb_id, u32 flag)
{
	_spb_mgr.sw_flags[spb_id] |= flag;
}

slow_code void spb_clr_sw_flag(spb_id_t spb_id, u32 flag)
{
	_spb_mgr.sw_flags[spb_id] &= ~flag;
}

slow_code u16 spb_get_sw_flag(spb_id_t spb_id)
{
	return _spb_mgr.sw_flags[spb_id];
}

fast_code void spb_retire(spb_id_t spb_id)
{
	u32 nsid = spb_get_nsid(spb_id);
	u32 poolid = spb_get_poolid(spb_id);

	spb_set_defect_blk(spb_id, ~0);
	ftl_apl_trace(LOG_ALW, 0x83ed, "spb %d retired(%d)", spb_id, nsid);
	if (nsid >= FTL_NS_ID_START && nsid < FTL_NS_ID_MAX)
		ftl_ns_spb_retire(nsid, poolid);
}

fast_code void spb_PurgePool2Free(u16 pool, u16 sortRule)
{
    u16 blk;
    u16 dest_pool;

	dest_pool = SPB_POOL_FREE;

	ftl_apl_trace(LOG_INFO, 0x33b1, "pool:%d release2pool:%d", pool, dest_pool);

	if(pool == SPB_POOL_PBT_ALLOC){
		pbt_query_ready			= 1;
		pbt_query_need_resume 	= 0;
	}

    while(_spb_mgr.pool_info.free_cnt[pool])
    {
        blk = FTL_BlockPopHead(pool);
        if (blk == INV_U16)
        {
            break;
        }
        FTL_BlockPushList(dest_pool, blk, sortRule);
		ftl_ns_spb_rel(_spb_mgr.spb_desc[blk].ns_id,spb_get_poolid(blk));
		if (_spb_mgr.spb_desc[blk].flags & SPB_DESC_F_OPEN) {
			_spb_mgr.ttl_open--;
			_spb_mgr.pool_info.open_cnt[spb_get_poolid(blk)]--;
		}
//		_spb_mgr.spb_desc[blk].pool_id = dest_pool;
		_spb_mgr.spb_desc[blk].ns_id = 0;
		_spb_mgr.spb_desc[blk].flags = 0;
		_spb_mgr.sw_flags[blk] = 0;
    }

}

//-------------------------------------------------------------------
// Function     : FTL_SearchGCDPoolVac(void)
// Description  :
// Input        : N/A
// return       : N/A
//-------------------------------------------------------------------
extern void gc_clear_all(void);
fast_data_zi u16 gc_pool_shuttle_spb[8];
fast_code void FTL_SearchGCDGC(void)
{
    u16 gcd_blk;
    u16 gcd_nxt_blk;
	u16 gc_blk;
	u16 gc_nxt_blk;

	gc_clear_all();	//clear all gc information when shut down flag is set

	u8 i = 0;
	for(i = 0; i < 8; i++)
		gc_pool_shuttle_spb[i] = INV_SPB_ID;

    if((_spb_mgr.pool_info.free_cnt[SPB_POOL_GCD] == 0) && (_spb_mgr.pool_info.free_cnt[SPB_POOL_GC] == 0))
	{
		return;
	}

    gcd_blk = _spb_mgr.pool_info.head[SPB_POOL_GCD];

    while(gcd_blk!=INV_U16)
    {
        gcd_nxt_blk = spb_info_tbl[gcd_blk].block;

		FTL_BlockPopPushList(SPB_POOL_USER, gcd_blk, FTL_SORT_BY_SN);

        gcd_blk = gcd_nxt_blk;
    }

	gc_blk = _spb_mgr.pool_info.head[SPB_POOL_GC];
	i = 0;
    while(gc_blk!=INV_U16)
    {
        gc_nxt_blk = spb_info_tbl[gc_blk].block;
		if(spb_get_flag(gc_blk) & SPB_DESC_F_WEAK && i < 8)
		{
			gc_pool_shuttle_spb[i] = gc_blk;
			i++;
		}
		FTL_BlockPopPushList(SPB_POOL_USER, gc_blk, FTL_SORT_BY_SN);
        gc_blk = gc_nxt_blk;
    }
}

fast_code void pop_shuttle_back_to_gc(void)
{
	for(u8 i = 0; i < 8; i++)
	{
		if(gc_pool_shuttle_spb[i] < get_total_spb_cnt())
		{
			FTL_BlockPopPushList(SPB_POOL_GC, gc_pool_shuttle_spb[i], FTL_SORT_NONE);
			Set_gc_shuttleCnt();
		}
		else
			break;
	}
	if(get_gcinfo_shuttle_cnt())
		Set_gc_shuttle_mode();
}

fast_code void pbt_release(spb_id_t spb_id)
{
	if(spb_id != INV_U16){
		spb_desc_t *desc = &_spb_mgr.spb_desc[spb_id];
		u8 pool = spb_get_poolid(spb_id);
		if (desc->flags & SPB_DESC_F_OPEN) {
			_spb_mgr.ttl_open--;
			_spb_mgr.pool_info.open_cnt[pool]--;
		}	
		ftl_ns_spb_rel(desc->ns_id, pool);
		desc->ns_id = 0;
		desc->flags = 0; // release
		_spb_mgr.sw_flags[spb_id] = 0;

		FTL_BlockPopPushList(SPB_POOL_FREE,spb_id,FTL_SORT_BY_EC);

		/*if(FTL_MAX_QBT_CNT == 4)// 8T
		{
			if((ftl_pbt_idx%2) != 0)
			{
				ftl_pbt_idx = ftl_pbt_idx - 1;
			}
		}*/
		ftl_pbt_cnt = _spb_mgr.pool_info.free_cnt[SPB_POOL_PBT];

		pbt_query_ready			= 1;
		pbt_query_need_resume 	= 0;
	}

    if(QBT_BLK_CNT > 1)
	{
		while((_spb_mgr.pool_info.free_cnt[SPB_POOL_PBT]%QBT_BLK_CNT)!=0){
		    FTL_BlockPopPushList(SPB_POOL_FREE,_spb_mgr.pool_info.tail[SPB_POOL_PBT],FTL_SORT_BY_EC);
		}
        
		ftl_pbt_cnt = _spb_mgr.pool_info.free_cnt[SPB_POOL_PBT];
	}
}

fast_code void spb_release(spb_id_t spb_id)
{
	spb_desc_t *desc = &_spb_mgr.spb_desc[spb_id];
    u8 pool = spb_get_poolid(spb_id);
	u32 sw_flag = spb_get_sw_flag(spb_id);

	if(shr_shutdownflag == true && (pool == SPB_POOL_QBT_ALLOC)){
		qbt_done_cnt++;
		ftl_apl_trace(LOG_INFO, 0x0fe7, "[SPB]shutdown, abort release:%d",spb_id);
		return;
	}
#ifdef ERRHANDLE_GLIST
    if(spb_get_flag(spb_id) & (SPB_DESC_F_WEAK | SPB_DESC_F_RD_DIST | SPB_DESC_F_PATOAL_RD))
    {
        ftl_apl_trace(LOG_INFO, 0x598c, "blk :%d is weak gc done will release",spb_id);
        return;
    }
#endif
	if(host_spb_close_idx == spb_id || gc_spb_close_idx == spb_id){
		chk_close_blk_push();
	}
	//ftl_apl_trace(LOG_INFO, 0, "pool %d ns_quota %d ns_alloc %d\n", desc->pool_id, ns_pool->quota, ns_pool->allocated);

	//sys_assert(desc->flags & SPB_DESC_F_BUSY);// Curry
	//sys_assert(desc->flags & SPB_DESC_F_CLOSED);
	//sys_assert((spb_get_sw_flag(spb_id) & SPB_SW_F_FREEZE) == 0);

	if (desc->flags & SPB_DESC_F_OPEN) {
		_spb_mgr.ttl_open--;
		_spb_mgr.pool_info.open_cnt[pool]--;
	}

	// check valid count

	if (desc->flags & SPB_DESC_F_RETIRED) {
		// keep all flags
		spb_retire(spb_id);
		return;
	}

#if ((PBT_OP == mENABLE) && (SPB_BLKLIST == mENABLE))
	if(_spb_mgr.sw_flags[spb_id] & SPB_SW_F_PBT_ALLOC){
		ftl_pbt_cnt++;
		//ftl_pbt_idx++;
		FTL_BlockPopPushList(SPB_POOL_PBT, spb_id, FTL_SORT_NONE);
		ftl_apl_trace(LOG_INFO, 0x266d, "[SPB]release pbt %d, cnt:%d", spb_id, _spb_mgr.pool_info.free_cnt[SPB_POOL_PBT]);

		//Release QBT when write whole l2p table in PBT // Howard
		if((ftl_pbt_cnt == spb_get_free_cnt(SPB_POOL_QBT_ALLOC)) && (!shr_shutdownflag)){
			spb_PurgePool2Free(SPB_POOL_QBT_ALLOC, FTL_SORT_BY_EC);
			ftl_apl_trace(LOG_INFO, 0x687e, "[SPB]release qbt cnt:%d", _spb_mgr.pool_info.free_cnt[SPB_POOL_QBT_ALLOC]);
		}

		if(ftl_pbt_cnt == QBT_BLK_CNT*2){
            ftl_pbt_cnt = 0;
		}
		/*if(ftl_pbt_idx == QBT_BLK_CNT*2){
            ftl_pbt_idx = 0;
		}*/
//		_spb_mgr.spb_desc[spb_id].pool_id = SPB_POOL_PBT;
		pbt_spb_soft(spb_id);
		if(_spb_mgr.pool_info.free_cnt[SPB_POOL_PBT] % QBT_BLK_CNT == 0){
			spb_ReleaseEmptyPool();
		}
		pbt_query_ready = true;
		if(pbt_query_need_resume == true){
			pbt_query_need_resume = false;
			ftl_ns_spb_trigger(2, 2);
		}
	}else
#endif
	if((pool == SPB_POOL_QBT_ALLOC) || ((pool == SPB_POOL_QBT_FREE) && (desc->ns_id == FTL_NS_ID_INTERNAL)))
	{
		FTL_BlockPopPushList(SPB_POOL_FREE, spb_id, FTL_SORT_BY_EC);
		ftl_apl_trace(LOG_ERR, 0xb756, "[SPB]release qbt ns %d spb %d",desc->ns_id, spb_id);
	}
	// GC TODO Empty pool
	else if(desc->flags & SPB_DESC_F_GCED)
	{
		if(sw_flag & SPB_SW_F_SHUTTLE_FREE)
		{
			FTL_BlockPopList(spb_info_tbl[spb_id].pool_id, spb_id);
			FTL_BlockPushHead(SPB_POOL_FREE, spb_id);
		    ftl_apl_trace(LOG_ERR, 0x0327, "[SPB] GC SHUT release ns %d spb %d",desc->ns_id ,spb_id);
		}
#ifdef SKIP_MODE
	#if (PLP_SUPPORT == 0)
        else if((spb_get_sn(spb_id) < gFtlMgr.PrevTableSN) && (force_empty == false) && (!GC_MODE_CHECK_NON_PLP))
	#else
        else if((spb_get_sn(spb_id) < gFtlMgr.PrevTableSN) && (force_empty == false))
	#endif
		{
			FTL_BlockPopPushList(SPB_POOL_FREE, spb_id, FTL_SORT_BY_EC);
		    ftl_apl_trace(LOG_ERR, 0xdc11, "[SPB]GC release ns %d spb %d",desc->ns_id ,spb_id);
//			_spb_mgr.spb_desc[spb_id].pool_id = SPB_POOL_FREE;
		}
		else
		{
			FTL_BlockPopPushList(SPB_POOL_EMPTY, spb_id, FTL_SORT_NONE);
			ftl_apl_trace(LOG_INFO, 0x271b, "[SPB]GC release ns %d spb %d to empty, sn:0x%x, presn:0x%x,force:%d",desc->ns_id,spb_id,spb_get_sn(spb_id),gFtlMgr.PrevTableSN,force_empty);
//			_spb_mgr.spb_desc[spb_id].pool_id = SPB_POOL_EMPTY;
		}
#else
		else{
			FTL_BlockPopPushList(SPB_POOL_FREE, spb_id, FTL_SORT_BY_EC);
		    ftl_apl_trace(LOG_ERR, 0x73f3, "[SPB]GC release ns %d spb %d",desc->ns_id ,spb_id);
//			_spb_mgr.spb_desc[spb_id].pool_id = SPB_POOL_FREE;
			}
#endif

//		_spb_mgr.spb_desc[spb_id].pool_id = SPB_POOL_FREE;
	}
	else{
#if (SPB_BLKLIST == mENABLE)
		if (host_spb_last_idx == spb_id)
		{
			ftl_apl_trace(LOG_ERR, 0x0bdf, "host_spb_last_idx:%u,now set INV_U16", host_spb_last_idx);
			host_spb_pre_idx = host_spb_last_idx;
			host_spb_last_idx = INV_U16;
		}else if (gc_spb_last_idx == spb_id)
		{
			ftl_apl_trace(LOG_ERR, 0x1220, "gc_spb_last_idx:%u,now set INV_U16", gc_spb_last_idx);
			gc_spb_last_idx = INV_U16;
		}
#ifdef SKIP_MODE
		if((spb_get_sn(spb_id) < gFtlMgr.PrevTableSN) && (force_empty == false)){
			FTL_BlockPopPushList(SPB_POOL_FREE, spb_id, FTL_SORT_BY_EC);

			ftl_apl_trace(LOG_ERR, 0x79ca, "[SPB]release ns %d spb %d",desc->ns_id ,spb_id);

//			_spb_mgr.spb_desc[spb_id].pool_id = SPB_POOL_FREE;
		}else{
			FTL_BlockPopPushList(SPB_POOL_EMPTY, spb_id, FTL_SORT_NONE);
			ftl_apl_trace(LOG_INFO, 0x93b9, "[SPB]release ns %d spb %d to empty, sn:0x%x, presn:0x%x,force:%d",desc->ns_id,spb_id,spb_get_sn(spb_id),gFtlMgr.PrevTableSN,force_empty);
//			_spb_mgr.spb_desc[spb_id].pool_id = SPB_POOL_EMPTY;
		}
#else
	FTL_BlockPopPushList(SPB_POOL_FREE, spb_id, FTL_SORT_BY_EC);	
	ftl_apl_trace(LOG_ERR, 0x2b88, "[SPB]release ns %d spb %d",desc->ns_id ,spb_id);
#endif
#endif
	}
	//ftl_apl_trace(LOG_INFO, 0, "[spb]release:%d(%d), to pool:%d, cnt:%d",spb_id,desc->ns_id,desc->pool_id,_spb_mgr.pool_info.free_cnt[desc->pool_id]);
	// check valid count
	ftl_ns_spb_rel(desc->ns_id, pool);
	desc->ns_id = 0;
	desc->flags = 0; // release
	_spb_mgr.sw_flags[spb_id] = 0;

}

fast_code bool is_spb_mgr_allocating(fsm_ctx_t *fsm)
{
	if (_spb_mgr.flags.b.allocating) {
		if (fsm)
			fsm_queue_insert_tail(&_spb_mgr.allocate_wait_que, fsm);
		return true;
	}
	return false;
}

fast_code void spb_mgr_rd_cnt_upd_all(read_cnt_sts_t *rd_cnt_sts)
{
	u32 i;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		if (rd_cnt_sts[i].b.counter == 0)
			continue;

		_spb_mgr.spb_desc[i].rd_cnt = rd_cnt_sts[i].b.counter;
        //ftl_apl_trace(LOG_ERR, 0, "_spb_mgr.spb_desc[%d].rd_cnt: %d", i, _spb_mgr.spb_desc[i].rd_cnt);
//		rd_cnt_sts[i].b.counter = 0;
	}
}

fast_code void spb_mgr_flush_desc(void)
{
//	u32 i;
	extern read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];
//	extern read_cnt_sts_t *shr_rd_cnt_sts;

//	for (i = 0; i < BE_CNT; i++)
		spb_mgr_rd_cnt_upd_all(shr_rd_cnt_sts);
//    spb_mgr_rd_cnt_upd_all(shr_rd_cnt_sts);

	//sys_log_desc_flush(&_spb_mgr.slog_desc, ~0, NULL); //Curry
	_spb_mgr.flags.b.desc_flush_all = 0;
}
ddr_code void copy_rd_cnt_from_nand(read_cnt_sts_t *rd_cnt_sts)
{
	u32 i, j;//, cnt_16 = 0;
	u32 spb_num_per_dtag = DTAG_SZE / sizeof(read_cnt_sts_t) / shr_nand_info.lun_num; 
//    ftl_apl_trace(LOG_ERR, 0, "in copy_rd_cnt_from_nand\n");
    rd_cnt_runtime_table = (runtime_rd_cnt_idx_t *)shr_rd_cnt_tbl_addr;
//    ftl_apl_trace(LOG_ERR, 0, "shr_rd_cnt_tbl_addr: 0x%x\n", shr_rd_cnt_tbl_addr);
//    ftl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table: 0x%x\n", rd_cnt_runtime_table);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) {
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) {
#endif

        int die_cnt;
//		if (_spb_mgr.spb_desc[i].rd_cnt == 0)
//            ftl_apl_trace(LOG_ERR, 0, "_spb_mgr.spb_desc[i].rd_cnt: %d\n", _spb_mgr.spb_desc[i].rd_cnt);
//			continue;
//        die_cnt = 0;

		rd_cnt_sts[i].b.counter = _spb_mgr.spb_desc[i].rd_cnt;

//        grp_cnt = i / 16;
//        ftl_apl_trace(LOG_ERR, 0, "rd_cnt_sts[%d].b.counter: %d\n", i, rd_cnt_sts[i].b.counter);
        //if((i > 0) && (i % 8 == 0))
        if((i > 0) && (i % spb_num_per_dtag == 0))
        {
            rd_cnt_runtime_table++;
        }
        //for(j = 0; j < 128; j++)
        for(j = 0; j < shr_nand_info.lun_num; j++)//adams
        {
			//die_cnt = (i % 8) * 128 + j;
			die_cnt = (i % spb_num_per_dtag) * shr_nand_info.lun_num + j;//adams
//            ftl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table:0x%x\n", rd_cnt_runtime_table);
            rd_cnt_runtime_table->runtime_rd_cnt_idx[die_cnt] = rd_cnt_sts[i].b.counter;
//            rd_cnt_runtime_table->runtime_rd_cnt_idx[die_cnt] = 0;

//            if(i < 16)
//            {
//                ftl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table->runtime_rd_cnt_idx[%d]:0x%x\n", die_cnt, &rd_cnt_runtime_table->runtime_rd_cnt_idx[die_cnt]);
//                ftl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table->runtime_rd_cnt_idx[%d]:%d\n", die_cnt, rd_cnt_runtime_table->runtime_rd_cnt_idx[die_cnt]);
//            }
        }
//        cnt_16++;
//        if(cnt_16 > 15)
//        {
//            cnt_16 = 0;
//        }
	}
//    ftl_apl_trace(LOG_ERR, 0, "end copy_rd_cnt_from_nand\n");
}

//fast_code void init_max_rd_cnt_tbl(void)
slow_code void init_max_rd_cnt_tbl(void)
{
//      u8 i;
      extern read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];
//        extern read_cnt_sts_t *shr_rd_cnt_sts[1];
//    rd_cnt_runtime_table = (runtime_rd_cnt_idx_t *)shr_rd_cnt_tbl_addr;
//    ftl_apl_trace(LOG_ERR, 0, "shr_rd_cnt_tbl_addr: 0x%x\n", shr_rd_cnt_tbl_addr);
//    ftl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table: 0x%x\n", rd_cnt_runtime_table);
//
    ftl_apl_trace(LOG_ERR, 0x42fe, "in init_max_rd_cnt_tbl\n");
//        for(i = 0; i < 128; i++)
    {
//        ftl_apl_trace(LOG_ERR, 0, "shr_rd_cnt_tbl_addr[%d] : 0x%x\n", i, rd_cnt_runtime_table->runtime_rd_cnt_idx[i]);
    }

//	for (i = 0; i < BE_CNT; i++)
//		copy_rd_cnt_from_nand(shr_rd_cnt_sts[i]);
    copy_rd_cnt_from_nand(shr_rd_cnt_sts);
}

fast_code void clean_rd_cnt_counter(spb_id_t spb_id)
{
    extern read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];

    read_cnt_sts_t *_rd_cnt_max_per_blk = (read_cnt_sts_t *)&shr_rd_cnt_sts[0];
    rd_cnt_runtime_table = (runtime_rd_cnt_idx_t *)shr_rd_cnt_tbl_addr;
    //u16 dtag_cnt = spb_id / 8;
    u32 spb_num_per_dtag = (DTAG_SZE / sizeof(read_cnt_sts_t)) / shr_nand_info.lun_num;
    u32 dtag_cnt = spb_id / spb_num_per_dtag;
    //u16 dtag_off = (spb_id % 8)*128;
    u32 dtag_off = (spb_id % spb_num_per_dtag) * shr_nand_info.lun_num;//adams
//    u8 i;

    //ftl_apl_trace(LOG_ERR, 0, "spb_id: %d\n", spb_id);
    //ftl_apl_trace(LOG_ERR, 0, "no. %d dtag, offset: %d\n", dtag_cnt, dtag_off);
    _rd_cnt_max_per_blk[spb_id].b.counter = 0;
    _rd_cnt_max_per_blk[spb_id].b.api = false;
//    ftl_apl_trace(LOG_ERR, 0, "_rd_cnt_max_per_blk[%d]: 0x%x/ counter %d\n", spb_id, &_rd_cnt_max_per_blk[spb_id], _rd_cnt_max_per_blk[spb_id].b.counter);
    rd_cnt_runtime_table += dtag_cnt;
    //ftl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table+dtag_off: 0x%x\n", &rd_cnt_runtime_table->runtime_rd_cnt_idx[dtag_off]);

    //memset(&rd_cnt_runtime_table->runtime_rd_cnt_idx[dtag_off],0,128 * sizeof(u32));
    memset(&rd_cnt_runtime_table->runtime_rd_cnt_idx[dtag_off],0,shr_nand_info.lun_num * sizeof(u32));//adams

	rd_cnt_runtime_counter = (runtime_rd_cnt_inc_t *)shr_rd_cnt_counter_addr;
	spb_num_per_dtag = (DTAG_SZE / sizeof(u8)) / shr_nand_info.lun_num;
	dtag_cnt = spb_id / spb_num_per_dtag;
	dtag_off = (spb_id % spb_num_per_dtag) * shr_nand_info.lun_num;
	rd_cnt_runtime_counter += dtag_cnt;
	memset(&rd_cnt_runtime_counter->runtime_rd_cnt_idx[dtag_off],0,shr_nand_info.lun_num * sizeof(u8));

//    for(i = 0; i < 8; i++)
//    {
//        ftl_apl_trace(LOG_ERR, 0, "rd_cnt_runtime_table->runtime_rd_cnt_idx[%d] = %d\n",dtag_off+(i*16), rd_cnt_runtime_table->runtime_rd_cnt_idx[dtag_off+(i*16)]);
//    }

}


//fast_code int rd_cnt_call(int argc, char *argv[])
//{
//    u32 cnt;
//    for(cnt = 0; cnt < 990; cnt++)
//    {
//        ftl_apl_trace(LOG_ERR, 0, "_spb_mgr.spb_desc[%d].rd_cnt = %d\n", cnt, _spb_mgr.spb_desc[cnt].rd_cnt);
//    }
//
//	return 0;
//}
//
//static DEFINE_UART_CMD(rd_cnt_call, "rd_cnt_call", "rd_cnt_call",
//		"rd_cnt_call", 0, 0, rd_cnt_call);
//
//    fast_code int rd_cnt_runtime_call(int argc, char *argv[])
//    {
//        u32 cnt, k;
//        runtime_rd_cnt_idx_t *_rd_cnt_runtime_per_blk = (runtime_rd_cnt_idx_t *)shr_rd_cnt_tbl_addr;
//
//        ftl_apl_trace(LOG_ERR, 0, "shr_rd_cnt_tbl_addr = 0x%x\n",shr_rd_cnt_tbl_addr);
//        for(k = 0; k < 62; k++)
//        {
//            ftl_apl_trace(LOG_ERR, 0, "_rd_cnt_runtime_per_blk = 0x%x\n",_rd_cnt_runtime_per_blk);
//            for(cnt = 0; cnt < 8; cnt++)
//            {
//                ftl_apl_trace(LOG_ERR, 0, "_rd_cnt_runtime_per_blk.runtime_rd_cnt_idx[%d] = %d\n", cnt*128, _rd_cnt_runtime_per_blk->runtime_rd_cnt_idx[cnt*128]);
//            }
//            _rd_cnt_runtime_per_blk++;
//        }
//        return 0;
//    }
//
//static DEFINE_UART_CMD(rd_cnt_runtime_call, "rd_cnt_runtime_call", "rd_cnt_runtime_call",
//        "rd_cnt_runtime_call", 0, 0, rd_cnt_runtime_call);
fast_code void show_blk(u32 nsid, u32 pool_id)
{
	u32 i;
	u32 *vc;
	dtag_t dtag;
	u32 dtag_cnt;
	ftl_apl_trace(LOG_INFO, 0x67df, "PrevTableSN %d",gFtlMgr.PrevTableSN);
	// todo: reduce this function call, it need time to backup valid count to buffer
	dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++)
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++)
#endif
	{
		ftl_apl_trace(LOG_INFO, 0x6994, "blk %d vc %d ns %d pool %d sn %d",i,vc[i],
				_spb_mgr.spb_desc[i].ns_id,spb_get_poolid(i),_spb_mgr.spb_desc[i].sn);
		ftl_apl_trace(LOG_INFO, 0x5655, "sw_flag %x",_spb_mgr.sw_flags[i]);
		ftl_apl_trace(LOG_INFO, 0x91fe, "flag %x",_spb_mgr.spb_desc[i].flags);
	}

	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
}

fast_code spb_id_t find_shuttle_blk(u32 pool_id)
{
        spb_id_t i;
    spb_id_t can = _spb_mgr.pool_info.head[SPB_POOL_GC];

//    ftl_apl_trace(LOG_INFO, 0, "spb_get_free_cnt(SPB_POOL_GC): %d",spb_get_free_cnt(SPB_POOL_GC));
    for(i = 0; i < _spb_mgr.pool_info.free_cnt[SPB_POOL_GC]; i++)
    {
    	if(get_gcinfo_shuttle_cnt())
    	{
	        if(spb_get_flag(can) & SPB_DESC_F_WEAK)
	        {
	        	Set_gc_shuttle_mode();
				rd_gc_flag = true; // ISU, EHPerfImprove
				ftl_apl_trace(LOG_INFO, 0x85c6, "shttle spb[%d], flag: 0x%x",can, spb_get_flag(can));
	            return can;
	        }
    	}
		else
		{
			if(spb_get_flag(can) & SPB_DESC_F_PATOAL_RD)
			{
	        	Set_gc_shuttle_mode();
				dr_cnt++;
				rd_gc_flag = true;
				ftl_apl_trace(LOG_INFO, 0x50a0, "patrol spb[%d], flag: 0x%x",can, spb_get_flag(can));
	            return can;
			}
		}
		can = spb_info_tbl[can].block;
    }

    ftl_apl_trace(LOG_INFO, 0x07c6, "[GC]shut selec fail");
    GC_MODE_CLEAN(GC_SHUTTLE);
    gGCInfo.shuttleCnt = 0;
	gGCInfo.patrlrdCnt = 0;
    return INV_SPB_ID;
}

fast_code spb_id_t find_min_vc(u32 nsid, u32 pool_id)
{
	u16 i;
	spb_id_t tar = INV_SPB_ID;
	u32 min_vc = 0xffffffff;
	u32 *vc;
	dtag_t dtag;
	u32 dtag_cnt;
	extern u32 gc_pre_vac;

	// todo: reduce this function call, it need time to backup valid count to buffer
	dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);

	if(wl_suspend_spb_id != INV_SPB_ID && pool_id == spb_get_poolid(wl_suspend_spb_id) 
		&& _spb_mgr.spb_desc[wl_suspend_spb_id].sn == wl_suspend_spb_sn)
	{
		tar =  wl_suspend_spb_id;
		min_vc = vc[wl_suspend_spb_id];
		goto skip_find_min;
	}

//#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
//	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++)
//#else
//	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++)
//#endif
//	{
//		if (pool_id != spb_get_poolid(i))
//			continue;
//
//		if (nsid != _spb_mgr.spb_desc[i].ns_id)
//			continue;
//
//		if (is_gc_canidiate(i) == false)
//			continue;
//#if 1
//		//4t
//		/*if(QBT_BLK_CNT == 1)
//		{*/
//			if(spb_get_free_cnt(SPB_POOL_UNALLOC) <= 74)  //729 + 5 + 1 + 2 + 2 + 18
//			{
//				if(_spb_mgr.spb_desc[i].sn > gFtlMgr.PrevTableSN)//tzu for skip empty blk
//				continue;
//			}
//		/*}
//		else//8t
//		{
//			if(spb_get_free_cnt(SPB_POOL_UNALLOC) <= 282)
//			{
//				if(_spb_mgr.spb_desc[i].sn > gFtlMgr.PrevTableSN)//tzu for skip empty blk
//				continue;
//			}
//		}*/
//#endif
//        if (vc[i] <= min_vc) {
//			min_vc = vc[i];
//			tar = i;
//		}
//	}
	i = spb_mgr_get_head(pool_id);
	while(i != INV_SPB_ID)
	{
		if (is_gc_canidiate(i) == false)
		{
			i = spb_info_tbl[i].block;
			continue;
		}
		if(_spb_mgr.spb_desc[i].sn > gFtlMgr.PrevTableSN)  //tzu for skip empty blk
		{
			if(spb_get_free_cnt(SPB_POOL_UNALLOC) <= 74)  //729 + 5 + 1 + 2 + 2 + 18
				break;
		}
		if (vc[i] < min_vc)
		{
			min_vc = vc[i];
			tar = i;
		}
		i = spb_info_tbl[i].block;
	}
	gc_pre_vac = min_vc;
skip_find_min:
	ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
	min_vac = min_vc;

	ftl_apl_trace(LOG_INFO, 0xca46, "gc find in ns %d pool %d",nsid, pool_id);
	//ftl_apl_trace(LOG_INFO, 0, "tar %d min_vc %d", tar, min_vc);
	if(tar == INV_SPB_ID)
	{
		//ftl_apl_trace(LOG_INFO, 0, "tzu no blk get ns %d pool %d",nsid, spb_get_poolid(tar));
		if(gc_get_mode() != GC_MD_IDLE)
			show_blk(nsid, pool_id);
	}
//	else
//	{
//		ftl_apl_trace(LOG_INFO, 0x851a, "gc blk %d vc %d sw %x flag 0x%x"
//			,tar,min_vc,_spb_mgr.sw_flags[tar],_spb_mgr.spb_desc[tar].flags);
//	}
	return tar;
}

#if (PLP_SUPPORT == 0)
slow_code spb_id_t find_non_plp_blk()
{
	epm_FTL_t* epm_ftl_data;
	epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	for(u8 i = 0; i < 2; i++)
	{
		if(epm_ftl_data->host_open_blk[i] != INV_U16)
			return epm_ftl_data->host_open_blk[i];
		else if(epm_ftl_data->gc_open_blk[i] != INV_U16)
			return epm_ftl_data->gc_open_blk[i];
	}
	//all blk done, clean lock
	GC_MODE_CLEAN(GC_NON_PLP);
	extern volatile u8 shr_lock_power_on;
	extern volatile bool shr_nonplp_gc_state;
	shr_lock_power_on &= (~NON_PLP_LOCK_POWER_ON);  // nonplp_lock --
	shr_nonplp_gc_state = SPOR_NONPLP_GC_DONE;
	
	ftl_apl_trace(LOG_INFO, 0x7f80, "[NONPLP] All GC Done!!");

	return INV_SPB_ID;
}
#endif

fast_code spb_id_t find_weak(u32 pool_id)
{
	spb_id_t i;
    spb_id_t can = _spb_mgr.pool_info.head[SPB_POOL_GC];

//	ftl_apl_trace(LOG_INFO, 0, "spb_get_free_cnt(SPB_POOL_GC): %d",spb_get_free_cnt(SPB_POOL_GC));
    for(i = 0; i < _spb_mgr.pool_info.free_cnt[SPB_POOL_GC]; i++)
    {
//		ftl_apl_trace(LOG_INFO, 0, "spb[%d], flag: %d",can, spb_get_flag(can));
        if(spb_get_flag(can) & SPB_DESC_F_RD_DIST)
        {
        	Set_gc_RdDisturb_mode();
			rd_gc_flag = true;
			if(!(spb_get_flag(can) & SPB_DESC_F_OVER_TEMP_GC))
            	tx_smart_stat->rd_count++;
    		ftl_apl_trace(LOG_INFO, 0x0602, "return spb[%d], rd_cnt: %d rd_gc_flag: %d", can, tx_smart_stat->rd_count, rd_gc_flag);
            return can;
        }
        can = spb_info_tbl[can].block;
    }
    GC_MODE_CLEAN(GC_READDISTURB);
    gGCInfo.rdDisturbCnt = 0;
    ftl_apl_trace(LOG_INFO, 0xf540, "rd can fail, return spb[%d]",INV_SPB_ID);

	return INV_SPB_ID;
}

void rescan_ec(u32 nsid, u32 pool_id)
{
	int i;
    spb_id_t tar = _spb_mgr.pool_info.head[pool_id];

    user_min_ec = INV_U32;
    user_min_ec_blk = INV_U16;

	for (i = 0; i < spb_get_free_cnt(pool_id); i++) {
		u32 ec = (u32) spb_info_get(tar)->erase_cnt;

		if (is_gc_canidiate(tar) && (_spb_mgr.spb_desc[tar].sn < gFtlMgr.PrevTableSN))
		{
            if(ec < user_min_ec)
			{
				user_min_ec_blk = tar;
				user_min_ec = ec;
			}
		}
        tar = spb_info_tbl[tar].block;
	}
    ftl_apl_trace(LOG_INFO, 0xf0e8, "[WL] min blk: %d, min ec: %d, min pool: %d", user_min_ec_blk, user_min_ec, spb_get_poolid(user_min_ec_blk));
}

#if 0
fast_code spb_id_t find_open(u32 nsid, u32 pool_id)
{
	spb_id_t i;

	/*
       ** open spb gc don't care nsid, only gc open spb in the pool
	*/

	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt(); i++) {
		if (pool_id != spb_get_poolid(i))
			continue;

		u32 flag = _spb_mgr.spb_desc[i].flags;
		//if (is_gc_canidiate(i) && (flag & SPB_DESC_F_OPEN)) {
		if (flag & SPB_DESC_F_OPEN)
		{
			u32 *vc;
			dtag_t dtag;
			u32 dtag_cnt;

			// todo: reduce this function call, it need time to backup valid count to buffer
			dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);

			ftl_apl_trace(LOG_INFO, 0x85cc, "can %d vc %d", i, vc[i]);

			ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
			if(vc != 0)
			return i;
		}
	}
	return INV_SPB_ID;
}
#endif

fast_code spb_id_t find_dr(u32 nsid, u32 pool_id)
{
    if(spb_get_poolid(dr_can) != SPB_POOL_USER)
//    {
//
//        u32 timer_dr, thr;
//        u32 avg_ec, max_ec, min_ec, total_ec;
//
//        get_avg_erase_cnt(&avg_ec, &max_ec, &min_ec, &total_ec);
//
//        thr = (avg_ec > EC_7K) ? HIGH_EC_DR_THR : LOW_EC_DR_THR;
//        dr_can = spb_mgr_get_head(SPB_POOL_USER);
//
//        if((poh + (jiffies / 36000)) - spb_info_tbl[dr_can].poh >= thr)
//        {
//            return dr_can;
//        }
//    }
//    else
    {

		ftl_apl_trace(LOG_INFO, 0x0d67, "[GC]dr blk %d isn't in user pool", dr_can);
        dr_can = INV_SPB_ID;
        GC_MODE_CLEAN(GC_DATARETENTION);
    }

    if(dr_can != INV_SPB_ID)
    {
        tx_smart_stat->dr_count++;
		rd_gc_flag = true;             //adams
//        ftl_apl_trace(LOG_INFO, 0, "[GC]dr_cnt: %d", dr_cnt);
    }
    return dr_can;
}
fast_code spb_id_t spb_mgr_find_gc_candidate(u32 nsid, u32 pool_id, spb_gc_can_policy_t policy)
{
	spb_id_t spb_id;
	stGC_MODE gc_mode = gc_get_mode();

	switch (policy) {
	case SPB_GC_CAN_POLICY_MIN_VC:
		spb_id = find_min_vc(nsid, pool_id);
		break;
	case SPB_GC_CAN_POLICY_WEAK:
		spb_id = find_weak(pool_id);
		break;
	case SPB_GC_CAN_POLICY_WL:
        rescan_ec(nsid, pool_id);
		spb_id = user_min_ec_blk;
        user_min_ec_blk = INV_SPB_ID;
		break;
//	case SPB_GC_CAN_POLICY_OPEN:
//		spb_id = find_open(nsid, pool_id);
//		break;
	case SPB_GC_CAN_SHOW:
		spb_id = INV_SPB_ID;
		show_blk(nsid, pool_id);
		break;
	case SPB_GC_CAN_POLICY_DR:
        spb_id = find_dr(nsid, pool_id);
		break;
	default:
		spb_id = INV_SPB_ID;
		panic("todo");
		break;
	}
	if(spb_id < get_total_spb_cnt())
    {
        FTL_BlockPopPushList(SPB_POOL_GCD, spb_id, FTL_SORT_NONE);
        ftl_apl_trace(LOG_INFO, 0x2c60, "[GC]push block %d in GCD POOL", spb_id);

        ftl_apl_trace(LOG_INFO, 0xef9e, "[GC]Blk_Select:MD[0x%x]Blk[%d]VAC[%d]EC[%d]", gc_mode, spb_id, min_vac, spb_info_get(spb_id)->erase_cnt);

        return spb_id;
    }
    else
    {

         ftl_apl_trace(LOG_INFO, 0xbf09, "gc blk: %d error, move to gc_idle", spb_id);
		 return INV_SPB_ID;
    }

}

init_code spb_id_t spb_mgr_pop_busy_spb(u32 nsid, sn_t *sn, spb_id_t last)
{
	u32 i;
	spb_id_t tar = INV_SPB_ID;
	u32 min_sn = 0;
	u32 _sn = *sn;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt(); i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt(); i++) 
#endif
	{
		if (_spb_mgr.spb_desc[i].ns_id != nsid)
			continue;

		ftl_apl_trace(LOG_ERR, 0xf443, "can %d sn %d, last %d", i, _spb_mgr.spb_desc[i].sn, last);
		if (_spb_mgr.spb_desc[i].sn >= _sn) {
			// reconstruct target
			if (last == i)
				continue;

			// target
			if (tar == INV_SPB_ID || min_sn > _spb_mgr.spb_desc[i].sn) {
				tar = i;
				min_sn = _spb_mgr.spb_desc[i].sn;
			}
		}
	}

	if (tar != INV_SPB_ID) {
		ftl_apl_trace(LOG_ERR, 0x3bfc, "target %d %x", tar, min_sn);
		*sn = min_sn;
	}

	return tar;
}

init_code spb_id_t spb_mgr_pop_dirty_spb(u32 nsid, u32 type, sn_t sn)
{
	u32 i;
	u32 spb_type;
	u32 min_sn = 0;
	spb_id_t tar = INV_SPB_ID;
	spb_desc_t *spb_desc = _spb_mgr.spb_desc;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt+ CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		if (spb_desc[i].ns_id != nsid)
			continue;

		spb_type = FTL_CORE_NRM;
		if (spb_desc[i].flags & SPB_DESC_F_GC_SPB)
			spb_type = FTL_CORE_GC;

		if (spb_type != type)
			continue;

		if (spb_desc[i].sn >= sn) {
			if ((tar == INV_SPB_ID) || (spb_desc[i].sn < min_sn)) {
				tar = i;
				min_sn = spb_desc[i].sn;
			}
		}
	}

	return tar;
}

fast_code void clear_api(void){
    read_cnt_sts_t *_rd_cnt_max_per_blk = (read_cnt_sts_t *)&shr_rd_cnt_sts[0];

	spb_id_t i;
    spb_id_t spb = _spb_mgr.pool_info.head[SPB_POOL_USER];
    u32 cnt = 0;
    for(i = 0; i < _spb_mgr.pool_info.free_cnt[SPB_POOL_USER]; i++)
    {
        sys_assert(spb < get_total_spb_cnt());
        if(_rd_cnt_max_per_blk[spb].b.api){
            ftl_apl_trace(LOG_INFO, 0x3d38, "clear [%u].b.api %d",spb, _rd_cnt_max_per_blk[spb].b.api);
            _rd_cnt_max_per_blk[spb].b.api = false;
            cnt ++;

            if(spb == spb_rd_id){
                spb_rd_id = INV_U16;
                spb_rd_step = INV_U8;
            }
        }
        spb = spb_info_tbl[spb].block;
    }

    ftl_apl_trace(LOG_INFO, 0x4cca, "clear cnt %u", cnt);
}

fast_code void spb_mgr_rd_cnt_upd(spb_id_t spb_id)
{
	if(host_spb_close_idx == spb_id || gc_spb_close_idx == spb_id)
		chk_close_blk_push();
	u32 nsid = _spb_mgr.spb_desc[spb_id].ns_id;
    spb_id_t last_spb = spb_rd_id;
    spb_rd_id = spb_id;
    if(last_spb != spb_id){
        spb_rd_step = INV_U8;
    }

    bool hit_open_gc = spb_id == ps_open[FTL_CORE_GC];
    bool hit_open_host = spb_id == ps_open[FTL_CORE_NRM];

    #if (RD_NO_OPEN_BLK == mENABLE)
    if(spb_info_get(spb_id)->pool_id == SPB_POOL_USER || (hit_open_gc && (gc_busy() == false)) || (hit_open_host))
    #else
    if((spb_info_get(spb_id)->pool_id == SPB_POOL_USER) || (!(spb_get_flag(spb_id) & SPB_DESC_F_OPEN)))
    #endif
    {
		ftl_apl_trace(LOG_INFO, 0xe5ce, "gFtlMgr.PrevTableSN %u, spb_get_sn(%u)=%u flag 0x%x rd_open_close_spb %d %d", gFtlMgr.PrevTableSN,
            spb_id, spb_get_sn(spb_id), spb_get_flag(spb_id), rd_open_close_spb[FTL_CORE_NRM], rd_open_close_spb[FTL_CORE_GC]);
		ftl_apl_trace(LOG_INFO, 0x1634, " hit host open %d hit gc open %d host open %u gc open %u, last_spb %u, spb_rd_step %u",
            hit_open_host, hit_open_gc, ps_open[FTL_CORE_NRM], ps_open[FTL_CORE_GC], last_spb, spb_rd_step);

        if(hit_open_gc && (gc_busy() == true)){
            if(last_spb == spb_id && spb_rd_step == 1){
                return;
            }

            ftl_apl_trace(LOG_INFO, 0x4e09, "[GC]set_open_gc_blk_rd spb[%d]", spb_id);
            shr_dummy_blk = spb_id;
            set_open_gc_blk_rd(true);
            spb_rd_step = 1;
        }
        else if((hit_open_host || hit_open_gc) && (spb_info_get(spb_id)->pool_id != SPB_POOL_USER))
        {
            if(last_spb == spb_id && spb_rd_step == 2){
                return;
            }

            if(hit_open_host){
                sys_assert(rd_open_close_spb[FTL_CORE_NRM] == INV_SPB_ID);
                rd_open_close_spb[FTL_CORE_NRM] = spb_id;
            }else if(hit_open_gc){
                sys_assert(rd_open_close_spb[FTL_CORE_GC] == INV_SPB_ID);
                rd_open_close_spb[FTL_CORE_GC] = spb_id;
            }

            spb_set_flag(spb_id, SPB_DESC_F_OPEN);
            ftl_apl_trace(LOG_INFO, 0x00b4, "rd_open_close_spb %d %d, flag 0x%x", rd_open_close_spb[FTL_CORE_NRM], rd_open_close_spb[FTL_CORE_GC], spb_get_flag(spb_id));
            ftl_ns_close_open(nsid, spb_id, FILL_TYPE_RD_OPEN);
            spb_rd_step = 2;
        }else if(gFtlMgr.PrevTableSN >= spb_get_sn(spb_id) && !hit_open_gc && !hit_open_host){
            if(last_spb == spb_id && spb_rd_step == 3){
                return;
            }

        	spb_set_flag(spb_id, SPB_DESC_F_RD_DIST);
        	ftl_ns_weak_spb_gc(nsid, spb_id);
            spb_rd_step = 3;
        }
        else
        {
            if(last_spb == spb_id){
				if(spb_rd_step >= 6){
                	return;
				}else if(spb_rd_step >= 4){
					spb_rd_step ++;
				}else{
					spb_rd_step = 4;
				}
            }else{
				spb_rd_step = 4;
			}
            ftl_apl_trace(LOG_INFO, 0xd023, "spb %u need force PBT",spb_id);
            cpu_msg_issue(1, CPU_MSG_FORCE_PBT, 0, 4);
        }
	}
    else if(spb_info_get(spb_id)->pool_id != SPB_POOL_GC && spb_info_get(spb_id)->pool_id != SPB_POOL_GCD)
	{
		read_cnt_sts_t *_rd_cnt_max_per_blk = (read_cnt_sts_t *)&shr_rd_cnt_sts[0];

		_rd_cnt_max_per_blk[spb_id].b.api = false;
		spb_rd_id = INV_U16;
        spb_rd_step = INV_U8;
    }

}

fast_code void spb_mgr_desc_delay_flush(u32 p0, u32 p1, u32 p2)
{
	_spb_mgr.flags.b.desc_flush_delay = 0;
	if (_spb_mgr.flags.b.desc_flush_all) {
		if (is_sys_log_flushing()) {
			if (_spb_mgr.flags.b.sld_flush_used == 0) {
				_spb_mgr.sld_flush.caller = NULL;
				_spb_mgr.sld_flush.cmpl = spb_desc_flush_wait;
				_spb_mgr.flags.b.sld_flush_used = 1;
				sys_log_flush_wait(&_spb_mgr.sld_flush);
			}
		} else {
			ftl_apl_trace(LOG_INFO, 0x99a7, "flush all spb in delay evt");
			spb_mgr_flush_desc();
		}
	}
}

fast_code bool spb_mgr_open_spb_gc(void)
{
	u32 pool_id;
	u32 gc_can;
	u32 nsid = FTL_NS_ID_START;

    #if 1 // added by Sunny
    ftl_apl_trace(LOG_ALW, 0xe07d, "GC Todo!");
    return false;
    #endif

	if (_spb_mgr.ttl_open == 0)
		return false;

	for (pool_id = 1; pool_id < SPB_POOL_MAX; pool_id++) {
		if (_spb_mgr.pool_info.open_cnt[pool_id] == 0)
			continue;

		//actually open spb gc don't care nsid
		//gc_can = spb_mgr_find_gc_candidate(nsid, pool_id, SPB_GC_CAN_POLICY_OPEN);
		//sys_assert(gc_can != INV_SPB_ID);
		if(gc_can != INV_SPB_ID)
		{
			nsid = _spb_mgr.spb_desc[gc_can].ns_id;
			gc_start(gc_can, nsid, INV_U32, NULL);
			break;
		}
		else
		{
			ftl_apl_trace(LOG_ALW, 0x2812, "open blk is invalid may vac 0");
		}
	}

	return true;
}

fast_code u32 spb_mgr_get_open_pool(void)
{
	u32 pool_id;

	for (pool_id = 1; pool_id < SPB_POOL_MAX; pool_id++) {
		if (_spb_mgr.pool_info.open_cnt[pool_id])
			return pool_id;
	}

	return SPB_POOL_MAX;
}

fast_code void spb_mgr_dump(void)
{
	u32 i;

	ftl_apl_trace(LOG_ALW, 0xa166, "f %x ttl open %d", _spb_mgr.flags.all, _spb_mgr.ttl_open);
	for (i = 0; i < SPB_POOL_MAX; i++)
		ftl_apl_trace(LOG_ALW, 0xf001, "p[%d] free %d, open %d", i, _spb_mgr.pool_info.free_cnt[i], _spb_mgr.pool_info.open_cnt[i]);

}

#if RAID_SUPPORT_UECC
#if 0
fast_code void smart_uecc_fill_host_blk_info(u16 lblk_id, bool mark_flag)  //runtime
{
    u8 bmp_idx = lblk_id / 8;
    u8 bit_flag = lblk_id % 8;

    if(mark_flag)
    {
        ftl_runtime_host_blk_bmp[bmp_idx] |= BIT(bit_flag);
    }
    else
    {
        ftl_runtime_host_blk_bmp[bmp_idx] &= ~BIT(bit_flag);
    }
}

ddr_code bool smart_uecc_get_host_blk_info(u16 lblk_id)    //runtime
{
    u8 bmp_idx = lblk_id / 8;
    u8 bit_flag = lblk_id % 8;

    if((spb_info_tbl[lblk_id].pool_id == SPB_POOL_USER) || (spb_info_tbl[lblk_id].pool_id == SPB_POOL_GC) || (spb_info_tbl[lblk_id].pool_id == SPB_POOL_GCD))
    {
        return true;
    }
    else
    {
        if(ftl_runtime_host_blk_bmp[bmp_idx] & BIT(bit_flag))
        {
            return true;
        }
        return false;
    }
}

ddr_code void fill_smart_uecc_info(u16 lblk, u16 err_type)
{
    switch(err_type)
    {
        case uecc_detect:
            if(smart_uecc_get_host_blk_info(lblk))
            {
                host_uecc_detection_cnt++;
            }
            else
            {
                internal_uecc_detection_cnt++;
            }
            nand_ecc_detection_cnt++;
            break;
        case uecc_fail:
            if(smart_uecc_get_host_blk_info(lblk))
            {
                uncorrectable_sector_count++;
            }
            else
            {
                internal_rc_fail_cnt++;
            }
            break;
        default:
            break;
    }
    //ftl_apl_trace(LOG_INFO, 0, "[DBG]SMART Host: det[%d]fail[%d]; Inter: det[%d]fail[%d] ",
    //                host_uecc_detection_cnt, uncorrectable_sector_count,
    //                internal_uecc_detection_cnt, internal_rc_fail_cnt);
}

ddr_code void ipc_fill_smart_uecc_info(volatile cpu_msg_req_t *req)
{
    SMART_uecc_info_t* uecc_info = (SMART_uecc_info_t*) req->pl;
    fill_smart_uecc_info(uecc_info->lblk_idx, uecc_info->err_type);
}
#endif
init_code void uecc_smart_info_init(void)
{
    //ftl_runtime_host_blk_bmp = (u8*)ddtag2mem(ddr_dtag_register(1));
    //memset(ftl_runtime_host_blk_bmp, 0, DTAG_SZE);

	//used before restore from epm at power on flow
    //nand_ecc_detection_cnt = 0;
	//uncorrectable_sector_count = 0;
    //internal_rc_fail_cnt = 0;
    //host_uecc_detection_cnt = 0;
    internal_uecc_detection_cnt = 0;

    host_prog_fail_cnt = 0;
    //uecc_smart_ptr = 0;

    //memset(smart_uecc_info, 0, smart_uecc_info_cnt * sizeof(SMART_uecc_info_t));
}
#endif

/*! @} */
