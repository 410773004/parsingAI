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
/*! \file ftl_l2p.c
 * @brief define ftl l2p manager, define l2p memory usage and l2p engine operations
 *
 * \addtogroup ftl
 * \defgroup ftl_l2p
 * \ingroup ftl
 * @{
 *
 */
#include "ftlprecomp.h"
#include "ftl_flash_geo.h"
#include "stdlib.h"
#include "l2p_mgr.h"
#include "ddr.h"
#include "event.h"
#include "spb_mgr.h"
#include "dtag.h"
#include "ftl_l2p.h"
#include "ftl_ns.h"
#include "ipc_api.h"
#include "sync_ncl_helper.h"
#include "dtag.h"
#include "ncl_cmd.h"
#include "gc.h"
#include "ftl_remap.h"
#include "ddr_top_register.h"
#include "console.h"
#include "frb_log.h"

#include "fc_export.h"
#include "die_que.h"
#include "ficu.h"
#include "ftl_spor.h"
#include "read_error.h"
#include "GList.h"

/*! \cond PRIVATE */
#define __FILEID__ flxp
#include "trace.h"
/*! \endcond PRIVATE */

#ifdef DUAL_BE
#define MEM_CPU		4
#else
#define MEM_CPU		1
#endif

#define MAX_L2P_LOAD_CMD	32	///< set maximum concurrent l2p load command count
#define MAX_L2P_LOAD_CMD_SZ	12	///< set maximum l2p load command size

#define BLOCK_NO_BAD    (0)
#define BLOCK_P0_BAD    (1)
#define BLOCK_P1_BAD    (2)
#define BLOCK_ALL_BAD   ((1<<shr_nand_info.geo.nr_planes)-1) //2P:3, 4P:15

#define BIT_CHECK(x, i) ((x>>i)&1)

/*! @brief l2p table which was shared by namespaces */
typedef struct _l2p_tbl_t {
	u64 base;			///< l2p start offset of DDR
	u64 alloc_off;			///< current allocated offset
	u64 max_size;			///< byte size
	u32 l2pp_cnt;			///< total l2p parital page count, l2p partial page size is 4K
	u32 seg_cnt;			///< total l2p segment count

	u32 vcnt_in_seg;		///< valid count table size in segment size
} l2p_tbl_t;

/*!
 * @brief definition l2p load command
 */
typedef struct _l2p_ncl_cmd_t {
	struct ncl_cmd_t ncl_cmd;		///< ncl command
	bm_pl_t bm_pl[MAX_L2P_LOAD_CMD_SZ];	///< bm payload buffer
	struct info_param_t info[MAX_L2P_LOAD_CMD_SZ];	///< info buffer
	pda_t pda[MAX_L2P_LOAD_CMD_SZ];			///< pda buffer

	union {
		u32 all;
		struct {
			u32 bg_load : 1;
			u32 urg_load : 1;
			u32 misc_data : 1;
		} b;
	} flags;
} l2p_ncl_cmd_t;

typedef struct _l2p_urg_load_req_t {
	struct list_head entry;
	u32 seg;
	u32 tx;
} l2p_urg_load_req_t;

fast_data_zi static l2p_tbl_t _l2p_tbl;				///< l2p table object
fast_data_zi u32 seg_size;						///< segment size in bytes
//fast_data_zi u32 seg_size_shf;					///< shift of segment size
slow_data_zi u64 _l2p_free_cmd_bmp;				///< l2p free command bitmap
slow_data_ni l2p_ncl_cmd_t _l2p_ncl_cmds[MAX_L2P_LOAD_CMD];	///< l2p load command resource

fast_data_zi ftl_cache_remap_tbl_t _l2p_cache_remap_tbl;	///< cached remap table for l2p load commands

share_data_zi volatile pda_t *shr_ins_lut;
share_data_zi volatile u32 *shr_l2p_ready_bmp;
fast_data_ni l2p_load_mgr_t l2p_load_mgr;
fast_data spb_id_t last_spb_vcnt_zero = INV_SPB_ID;
slow_data u8 blklistbuffer_in_seg;
slow_data u8 vac_table_need;
fast_data u8 evt_wb_save_pb = 0xFF;

extern volatile u16 spor_read_pbt_blk[4];
extern volatile u8 spor_read_pbt_cnt;

#ifdef SKIP_MODE
share_data_zi u8* gl_pt_defect_tbl;
#endif
pda_t     L2P_LAST_PDA = 0;
extern volatile pbt_resume_t *pbt_resume_param;
extern volatile bool shr_format_copy_vac_flag;

#if defined(HMETA_SIZE)
extern enum du_fmt_t host_du_fmt; // Jamie 20210319
#endif

/*!
 * @brief valid count search done, if valid count was 0, release it
 *
 * @param r0	not used
 * @param r1	not used
 * @param r2	not used
 *
 * @return	not used
 */
static void ftl_spb_vcnt_fet_done(u32 r0, u32 r1, u32 r2);

/*!
 * @brief valid count notify, if valid count was 0, release it
 *
 * @param r0	not used
 * @param r1	vcnt notify entry ptr
 * @param r2	not used
 *
 * @return	not used
 */
static void ftl_spb_vcnt_notify(u32 r0, u32 r1, u32 r2);

/*!
 * @brief prebuild flush bitmap according GC spb id, search internal l2p for target spb, and set flush bit
 *
 * @param spb_id	target spb id
 *
 * @return		not used
 */
static void prebuild_flush_bmp(spb_id_t spb_id);

/*!
 * @brief load l2p table in the background
 *
 * @param r0	not used
 * @param r1	not used
 * @param r2	not used
 *
 * @return		not used
 */
static void ftl_l2p_bg_load_handle(u32 r0, u32 r1, u32 r2);

/*!
 * @brief execute urgent l2p load request
 *
 * @return		not used
 */
static void ftl_l2p_urgent_load_exec(void);

/*!
 * @brief execute background l2p load request
 *
 * @return		not used
 */
static void ftl_l2p_bg_load_exec(void);

/*!
 * @brief l2p read done handle
 *
 * @return		not used
 */
static void l2p_seg_read_done(struct ncl_cmd_t *ncl_cmd);

init_code void ftl_l2p_para_init(void)
{
	seg_size = DU_CNT_PER_PAGE * DTAG_SZE *3;
	shr_l2p_seg_sz = seg_size;
	shr_l2pp_per_seg = NR_L2PP_IN_SEG;
	ftl_apl_trace(LOG_ERR, 0x441d, "seg_size %d NR_L2PP_IN_SEG %d", seg_size, NR_L2PP_IN_SEG);
}

init_code static void ftl_l2p_load_init(void)
{
	u32 ddr_dtag;
	u32 dtag_required;

	memset(&l2p_load_mgr, 0, sizeof(l2p_load_mgr_t));
	dtag_required = occupied_by(shr_l2p_seg_bmp_sz, DTAG_SZE);

#ifdef L2P_DDTAG_ALLOC	//20201029-Eddie
	ddr_dtag = ddr_dtag_l2p_register(dtag_required);
#else
	ddr_dtag = ddr_dtag_register(dtag_required);
#endif
	l2p_load_mgr.ready_bmp = (u32*)ddtag2mem(ddr_dtag);
	shr_l2p_ready_bmp = l2p_load_mgr.ready_bmp;

#ifdef L2P_DDTAG_ALLOC
	ddr_dtag = ddr_dtag_l2p_register(dtag_required);
#else
	ddr_dtag = ddr_dtag_register(dtag_required);
#endif
	l2p_load_mgr.loading_bmp = (u32*)ddtag2mem(ddr_dtag);

	memset(l2p_load_mgr.ready_bmp, 0, shr_l2p_seg_bmp_sz);
	memset(l2p_load_mgr.loading_bmp, 0, shr_l2p_seg_bmp_sz);

	INIT_LIST_HEAD(&l2p_load_mgr.bg_waiting_list);
	INIT_LIST_HEAD(&l2p_load_mgr.urg_waiting_list);
	INIT_LIST_HEAD(&l2p_load_mgr.urg_loading_list);
	evt_register(ftl_l2p_bg_load_handle, 0, &l2p_load_mgr.evt_load);
}

init_code void ftl_l2p_init(u32 cap)
{
	u32 du_cnt_per_spb;
	u64 l2p_base;
	u32 sz;
	u64 _cap = cap;

	memset(&_l2p_tbl, 0, sizeof(_l2p_tbl));
	_l2p_free_cmd_bmp = (1ULL << MAX_L2P_LOAD_CMD) - 1;

	shr_l2p_entry_need = occupied_by(_cap * sizeof(pda_t), DTAG_SZE);
	shr_l2p_entry_need = ((shr_l2p_entry_need+NR_L2PP_IN_SEG-1)/NR_L2PP_IN_SEG);
	shr_l2p_entry_need = shr_l2p_entry_need*NR_L2PP_IN_SEG;

	// du cnt per die and aligned to 256
	du_cnt_per_spb = round_up_by_2_power(get_du_cnt_per_die(), 256);
	du_cnt_per_spb *= get_lun_per_spb();

	evt_register(ftl_spb_vcnt_fet_done, 0, &evt_status_fet_rsp);
	evt_register(ftl_spb_vcnt_notify, 0, &evt_vcnt_notify);

	_l2p_tbl.max_size = shr_l2p_entry_need;
	_l2p_tbl.max_size = _l2p_tbl.max_size * DTAG_SZE;
	_l2p_tbl.l2pp_cnt = shr_l2p_entry_need;
	_l2p_tbl.seg_cnt = shr_l2p_entry_need / NR_L2PP_IN_SEG;
	shr_l2p_seg_cnt = _l2p_tbl.seg_cnt;

	sz = occupied_by(_l2p_tbl.seg_cnt, 32) * sizeof(u32);
	shr_l2p_seg_bmp_sz = sz;

#if (FTL_L2P_SEG_BMP == mENABLE)
    //ftl_apl_trace(LOG_ALW, 0, "seg_bmp_sz : 0x%x", sz);
	shr_l2p_seg_dirty_bmp = sys_malloc(SLOW_DATA, sz);
	shr_l2p_seg_flush_bmp = sys_malloc(SLOW_DATA, sz);

	sys_assert(shr_l2p_seg_dirty_bmp);
	sys_assert(shr_l2p_seg_flush_bmp);

	memset(shr_l2p_seg_dirty_bmp, 0, sz);
	memset(shr_l2p_seg_flush_bmp, 0, sz);
#endif
	ftl_l2p_load_init();	//Move to bellow

//20200918 Curry blklist buffer
////////////////////////////////////////////////////////////////////
    shr_blklistbuffer_need	 = occupied_by(BLKINFO_SIZE_MAX, DTAG_SZE);// dtag cnt
	vac_table_need           = occupied_by(get_total_spb_cnt() * sizeof(u32), DTAG_SZE);
	blklistbuffer_in_seg	 = occupied_by(((shr_blklistbuffer_need+vac_table_need) * DTAG_SZE), seg_size);
	u32 vac_buffer_size = get_total_spb_cnt() * sizeof(u32);
	shr_vac_drambuffer_need = occupied_by(vac_buffer_size, DTAG_SZE);
#ifdef L2P_DDTAG_ALLOC
    pbt_resume_param = (pbt_resume_t *)ddtag2mem(ddr_dtag_l2p_register(occupied_by(sizeof(pbt_resume_t), DTAG_SZE)));
	#ifdef L2P_FROM_DDREND
		shr_l2p_entry_start = ddr_dtag_l2p_register(shr_l2p_entry_need);
		shr_blklistbuffer_start[0]  = DDR_BLIST_DTAG_START;
		shr_blklistbuffer_start[1]  = DDR_BLIST_DTAG_START+shr_blklistbuffer_need;
		shr_vac_drambuffer_start  = ddr_dtag_l2p_register(shr_vac_drambuffer_need);
	#else
		shr_vac_drambuffer_start  = ddr_dtag_l2p_register(shr_vac_drambuffer_need);
		shr_blklistbuffer_start[0]  = DDR_BLIST_DTAG_START;
		shr_blklistbuffer_start[1]  = DDR_BLIST_DTAG_START+shr_blklistbuffer_need;
		shr_l2p_entry_start = ddr_dtag_l2p_register(shr_l2p_entry_need);
	#endif
#else
	shr_blklistbuffer_start  = ddr_dtag_register(shr_blklistbuffer_need);
/////////////////////////////////////////////////////////////////////
	shr_l2p_entry_start = ddr_dtag_register(shr_l2p_entry_need);
#endif
	sys_assert(shr_l2p_entry_start != ~0);


#ifdef L2P_DDTAG_ALLOC
	ddr_dtag_l2p_register_lock();
#endif

	ddr_dtag_register_lock();

	ftl_apl_trace(LOG_ALW, 0x50ac, "vac res got entry %d %x blklist %d %x/%x",
		shr_vac_drambuffer_need, shr_vac_drambuffer_start, shr_blklistbuffer_need,
		shr_blklistbuffer_start[0],shr_blklistbuffer_start[1]);
	ftl_apl_trace(LOG_ALW, 0x7386, "L2P res got entry %d %x seg_cnt %d", shr_l2p_entry_need, shr_l2p_entry_start, shr_l2p_seg_cnt);

	l2p_base = ddtag2off(shr_l2p_entry_start);
	l2p_mgr_ctrl(false, cap);
	l2p_mgr_buf_init(l2p_base, (u64)shr_l2p_entry_need * DTAG_SZE,
			0, 0,
			0, (u64)get_total_spb_cnt() * sizeof(u32), CNT_IN_INTB);
	l2p_mgr_init();
	_l2p_tbl.base = ddtag2off(shr_l2p_entry_start);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	extern u64 l2p_base_addr;
	l2p_base_addr = _l2p_tbl.base;
#endif
	//ftl_apl_trace(LOG_ERR, 0, "_l2p_tbl.base 0x%x%x, _l2p_tbl.max_size 0x%x%x \n", (u32)(_l2p_tbl.base>>32), _l2p_tbl.base, (u32)(_l2p_tbl.max_size>>32), _l2p_tbl.max_size);
	// vcnt
	_l2p_tbl.vcnt_in_seg = occupied_by(get_total_spb_cnt() * sizeof(u32), seg_size);

    evt_register(warmboot_save_pbt_done, 0, &evt_wb_save_pb);
}

fast_code u32 GET_BLKLIST_START_DTAGIDX(u32 type)
{
	return shr_blklistbuffer_start[type];
}
slow_code u32 ftl_l2p_misc_cnt(void)
{
	//return _l2p_tbl.vcnt_in_seg + blklistbuffer_in_seg;
	return blklistbuffer_in_seg;
}

#if (RELEASE_LOG_CTL == mENABLE)
fast_data_zi u64 release_last = 0;	
fast_data_zi bool force_empty = false;
#endif

fast_code void ftl_spb_vcnt_fet_done(u32 r0, u32 r1, u32 r2)
{
	fet_cnt_rsp_t *rslt = (fet_cnt_rsp_t *) r1;

	u16 flags = spb_get_flag(rslt->spb_id);
	u16 sw_flags = spb_get_sw_flag(rslt->spb_id);

	#if (RELEASE_LOG_CTL == mDISABLE)
	ftl_apl_trace(LOG_INFO, 0x0af3, "spb %d vcnt_srch %d, %x %x",
			rslt->spb_id, rslt->cnt,
			flags, sw_flags);
	#endif

	sys_assert((flags & SPB_DESC_F_CLOSED) == 0);
	sys_assert((sw_flags & SPB_SW_F_FREEZE) == 0);

	spb_set_flag(rslt->spb_id, SPB_DESC_F_CLOSED);
	//ftl_ns_inc_closed_spb(rslt->spb_id);

	if ((rslt->cnt == 0) && (spb_get_poolid(rslt->spb_id) != SPB_POOL_QBT_ALLOC)){
		#if (RELEASE_LOG_CTL == mENABLE)
		u64 release_last_tmp = get_tsc_64() - release_last;
		bool log_bypass = false;
		log_level_t old = 0x1;
		if(release_last_tmp < (500*CYCLE_PER_MS))	//<500ms bypass log
		{
			log_bypass = true;
			//ftl_apl_trace(LOG_INFO, 0, "%dms over",release_last_tmp);
		}
		force_empty = false;
		if(log_bypass){
			old = log_level_chg(LOG_PANIC);
			if(spb_get_free_cnt(SPB_POOL_FREE) > 20){
				force_empty = true;
			}			
		}

		ftl_apl_trace(LOG_INFO, 0xc336, "spb %d vcnt_srch %d, %x %x",
				rslt->spb_id, rslt->cnt,
				flags, sw_flags);	

		release_last = get_tsc_64();
		spb_set_sw_flag(rslt->spb_id, SPB_SW_F_VC_ZERO);
		spb_release(rslt->spb_id);
		if(log_bypass){
			log_level_chg(old);
		}
		force_empty = false;
		#else
		spb_set_sw_flag(rslt->spb_id, SPB_SW_F_VC_ZERO);
		spb_release(rslt->spb_id);		
		#endif		
	}

//	if (flags & SPB_DESC_F_WEAK) {
//		u32 nsid = spb_get_nsid(rslt->spb_id);
//
//		ftl_ns_weak_spb_gc(nsid, rslt->spb_id);
//	}
}

fast_code void ftl_spb_vcnt_notify(u32 r0, u32 r1, u32 r2)
{
	cnt_notify_t *rslt = (cnt_notify_t *)r1;
	spb_id_t spb_id = rslt->spb_id;
	u16 sw_flag = spb_get_sw_flag(spb_id);
	u16 flag    = spb_get_flag(spb_id);

	#if (RELEASE_LOG_CTL == mDISABLE)
	ftl_apl_trace(LOG_INFO, 0xdfa5, "notify spb %d with %x(%x)",rslt->spb_id, flag,sw_flag);
	#endif
	sys_assert(rslt->cnt == 0);

	if ((flag & SPB_DESC_F_CLOSED) ||
		((flag & SPB_DESC_F_OPEN)&& (flag & SPB_DESC_F_BUSY)))
	{
		if ((sw_flag & SPB_SW_F_FREEZE))
		{
			spb_set_sw_flag(spb_id, SPB_SW_F_VC_ZERO); // delay release in dirty queue remove
		}
		else
		{
			if (get_gc_blk() == spb_id)
				spb_set_sw_flag(spb_id, SPB_SW_F_VC_ZERO); // delay release this SPB in gc_end
			else{
                last_spb_vcnt_zero = spb_id;
				#if (RELEASE_LOG_CTL == mENABLE)
				u64 release_last_tmp = get_tsc_64() - release_last;
				bool log_bypass = false;
				log_level_t old = 0x1;
				if(release_last_tmp < (500*CYCLE_PER_MS))	//<500ms bypass log
				{
					log_bypass = true;
					//ftl_apl_trace(LOG_INFO, 0, "%dms over",release_last_tmp);
				}
				force_empty = false;
				if(log_bypass){
					old = log_level_chg(LOG_PANIC);
					if(spb_get_free_cnt(SPB_POOL_FREE) > 20){
						force_empty = true;
					}
				}

                if (shr_format_copy_vac_flag)   // Precautions: If spb is used to store QBT and plp is triggered during QBT storage, the VAC table will go wrong after SPOR.
                {
                    force_empty = true;
                }
				ftl_apl_trace(LOG_INFO, 0x4291, "notify spb %d with %x(%x)",rslt->spb_id, flag,sw_flag);
				release_last = get_tsc_64();
				spb_release(spb_id);
				if(log_bypass){
					log_level_chg(old);
				}
				force_empty = false;
				#else
				spb_release(spb_id);		
				#endif		
			}
		}
	}
	else
	{
		// just ignore it
	}
}

init_code u64 ftl_l2p_alloc(u32 cap, u32 nsid, l2p_ele_t *ele)
{
	u32 seg_cnt;
	u32 lut_seg;
	u64 lut_sz;
	u64 ret;
	u32 sz = cap;

	seg_cnt = occupied_by(sz, seg_size/sizeof(pda_t));
    ftl_apl_trace(LOG_ERR, 0x0b7b, "[FTL]host real seg_cnt %d\n", seg_cnt);
	lut_seg = seg_cnt;
	lut_sz  = lut_seg * seg_size;

	ret = _l2p_tbl.base + _l2p_tbl.alloc_off;
	ele->lut = ret;
	ele->seg_off = 0;
	_l2p_tbl.alloc_off += lut_sz;
	ele->seg_end = seg_cnt;
	//ftl_apl_trace(LOG_ERR, 0, "ret %d lut_sz 0x%x alloc_off 0x%x max_size 0x%x\n", ret, lut_sz,_l2p_tbl.alloc_off ,_l2p_tbl.max_size);
	sys_assert(_l2p_tbl.alloc_off <= _l2p_tbl.max_size);

	return ret;
}
typedef struct _ftl_hns_t {
	ftl_ns_t *ns;		///< parent ftl namespace object
	l2p_ele_t l2p_ele;	///< l2p element, describe l2p of this namespace
} ftl_hns_t;
extern ftl_hns_t _ftl_hns[FTL_NS_ID_END];	///< ftl host namespace objects
//share_data ftl_core_gc_t *_ftl_core_gc_trim;//joe add test
extern void gc_action_done(gc_action_t *action);
extern bool gc_action(gc_action_t *action);

extern bool format_gc_suspend_done;
fast_code void ftl_l2p_gc_suspend(void)
{
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_SUSPEND;// 2
	action->caller = NULL;
	action->cmpl = gc_action_done;

#if(PLP_GC_SUSPEND == mDISABLE)
	if (gc_action2(action))
#else
	if (gc_action(action))
#endif
	{
		sys_free(FAST_DATA, action);
		format_gc_suspend_done = 1;
	}
}
fast_code void ftl_l2p_partial_reset(u32 sec_idx)//curry add for del ns 202011
{
	//l2p_ele_t *ele = &_ftl_hns[FTL_NS_ID_START].l2p_ele;
	//u64 base = _l2p_tbl.base;
	u8 cntt=0;
	u32 ns_sec_len;
	u16 ns_sec_id = (u16)sec_idx ;
	ns_sec_len =0x200000; //lda cnt: 8GB/4096
	lda_t first_lda=ns_sec_id * ns_sec_len;
	lda_t lda_trim=0;
	//base += (((u64)ele->seg_off) * seg_size) + (ns_sec_id * ns_sec_len);
	//printk("[FTL]l2p reset sec_id %d l2p_base 0x%x%x \n", ns_sec_id, (u32)(base>>32), (u32)base);
	//bm_scrub_ddr(base, ns_sec_len, 0xffffffff);
	//if(internal_trim_flag==0){
	
	for(cntt=0;cntt<32;cntt++){
		lda_trim=first_lda+cntt*65536;
		if(cntt==0)
		ftl_apl_trace(LOG_INFO, 0x7720, "update trim lda:%x\n",lda_trim);

		/*if(_ftl_core_gc_trim->gc_action==NULL){
		printk("stop gc deletens in\n");
		gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

		action->act = 0;// 2
		action->caller = NULL;
		action->cmpl = gc_action_done;

		if (gc_action(action))
		sys_free(FAST_DATA, action);
		else
		sys_free(FAST_DATA, action);
		}*/

		//if(lda_trim<(447*0x200000))
		l2p_updt_trim(lda_trim, 0, 1, 65536, 1, 87);
	}
	//internal_trim_flag=1;
}
fast_code void gc_re(void)//joe add for ns 2020121
{


	ftl_apl_trace(LOG_INFO, 0xc763, " gc deletens re\n");//joe add gc stop actiion 202011
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_RESUME;// 2
	action->caller = NULL;
	action->cmpl = gc_action_done;

#if(PLP_GC_SUSPEND == mDISABLE)
	if (gc_action2(action))
#else
	if (gc_action(action))
#endif
		sys_free(FAST_DATA, action);


}

fast_code void ftl_l2p_reset_partial(l2p_ele_t *ele,u16 seg_cnt) 
{ 
	u64 base = _l2p_tbl.base; 
	u64 len; 

	//len = ((u64)(ele->seg_end - ele->seg_off)) << seg_size_shf; 

	//len = ((u64)(ele->seg_end - ele->seg_off + 1)) * seg_size; 
	len =  ((u64)seg_cnt * seg_size); 
	//base += ((u64)ele->seg_off) << seg_size_shf; 
	base += ((u64)ele->seg_off) * seg_size; 

	bm_scrub_ddr(base, len, 0xffffffff); 
} 


fast_code void ftl_l2p_reset(l2p_ele_t *ele)
{
	u64 base = _l2p_tbl.base;
	u64 len;

	//len = ((u64)(ele->seg_end - ele->seg_off)) << seg_size_shf;

	//len = ((u64)(ele->seg_end - ele->seg_off + 1)) * seg_size;
	len =  ((u64)shr_l2p_seg_cnt * seg_size);
	//base += ((u64)ele->seg_off) << seg_size_shf;
	base += ((u64)ele->seg_off) * seg_size;

	bm_scrub_ddr(base, len, 0xffffffff);
}

fast_code void l2p_seg_read(l2p_ncl_cmd_t *l2p_cmd, u32 cnt, u32 seg_id)
{
	struct ncl_cmd_t *ncl_cmd = &l2p_cmd->ncl_cmd;
	pda_t *pda_list = l2p_cmd->pda;
	bm_pl_t *bm_pl_list = l2p_cmd->bm_pl;
	struct info_param_t *info_list = l2p_cmd->info;

	ncl_cmd->addr_param.common_param.list_len = cnt;
	ncl_cmd->addr_param.common_param.pda_list = pda_list;
	ncl_cmd->addr_param.common_param.info_list = info_list;

#ifndef SKIP_MODE
	ftl_get_cached_remap_pda_list(pda_list, cnt, &_l2p_cache_remap_tbl);
#endif

#if defined(HMETA_SIZE)
	ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG; // Jamie 20210319
#else
    ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
#endif
	#if (QBT_TLC_MODE == mENABLE)
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	#else
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	#endif
    #if RAID_SUPPORT_UECC
    ncl_cmd->flags |= NCL_CMD_L2P_READ_FLAG;
    #endif
	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->user_tag_list = bm_pl_list;

	ncl_cmd->caller_priv = (void *)seg_id;
	ncl_cmd->completion = l2p_seg_read_done;
#if defined(HMETA_SIZE)
	ncl_cmd->du_format_no = host_du_fmt; // Jamie 20210319
	ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
#else
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
#endif
	ncl_cmd->status = 0;

    #if NCL_FW_RETRY
    ncl_cmd->retry_step = default_read;
    #endif

	#if RAID_SUPPORT_UECC
	ncl_cmd->uecc_type = NCL_UECC_L2P_RD;
	#endif	

	ncl_cmd_submit(ncl_cmd);
}

#ifdef NCL_HAVE_reARD
fast_code void l2p_seg_reread(struct ncl_cmd_t *re_ncl_cmd, u32 list_len, pda_t *pda_list, struct info_param_t *info_list, bm_pl_t *bm_pl_list, u32 seg_id)
{
	struct ncl_cmd_t *ncl_cmd = re_ncl_cmd;
	//pda_t *pda_list = l2p_cmd->pda;
	//bm_pl_t *bm_pl_list = l2p_cmd->bm_pl;
	//struct info_param_t *info_list = l2p_cmd->info;

	ncl_cmd->addr_param.common_param.list_len = list_len;
	ncl_cmd->addr_param.common_param.pda_list = pda_list;
	ncl_cmd->addr_param.common_param.info_list = info_list;

	//ftl_get_cached_remap_pda_list(pda_list, cnt, &_l2p_cache_remap_tbl);

#if defined(HMETA_SIZE)
        ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG; // Jamie 20210319
#else
        ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
#endif
	#if (QBT_TLC_MODE == mENABLE)
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	#else
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	#endif
    #if RAID_SUPPORT_UECC
    ncl_cmd->flags |= NCL_CMD_L2P_READ_FLAG;
    #endif
    ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG; //tony 20201224

	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->user_tag_list = bm_pl_list;

	ncl_cmd->caller_priv = (void *)seg_id;
	ncl_cmd->completion = l2p_seg_read_done;
#if defined(HMETA_SIZE)
    ncl_cmd->du_format_no = host_du_fmt; // Jamie 20210319
    ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
#else
    ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
    ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
#endif
	ncl_cmd->status = 0;

#if RAID_SUPPORT_UECC
    ncl_cmd->uecc_type = NCL_UECC_L2P_RD;
#endif  


	ncl_cmd_submit(ncl_cmd);
}
#endif
fast_code void l2p_rc_done(rc_req_t* req){
    struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *)req->caller_priv;
    if(req->flags.b.fail == true){
        ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
        ncl_cmd->retry_step = raid_recover_fail;
		#if SPOR_FLOW == mENABLE
        if(ncl_cmd->op_code == NCL_CMD_SPOR_SCAN_FIRST_PG){
            ftl_apl_trace(LOG_INFO, 0x67f0, "scan first pg raid recover fail");
        }else if(ncl_cmd->op_code == NCL_CMD_SPOR_SCAN_LAST_PG){
            ftl_apl_trace(LOG_INFO, 0x536f, "scan last pg raid recover fail");
        }else if(ncl_cmd->op_code == NCL_CMD_SPOR_P2L_READ){
            ftl_apl_trace(LOG_INFO, 0xa2b4, "p2l read raid recover fail");
        }else if(ncl_cmd->op_code == NCL_CMD_SPOR_BLIST_READ){
            ftl_apl_trace(LOG_INFO, 0x8875, "blklist read raid recover fail");
        }else if(ncl_cmd->op_code == NCL_CMD_SPOR_SCAN_PG_AUX){
            ftl_apl_trace(LOG_INFO, 0xf4e4, "aux build raid recover fail");
        }else
		#endif
		{
            ftl_apl_trace(LOG_INFO, 0x8ec4, "l2p raid recover fail");
        }
    }else{
        ncl_cmd->status &= ~NCL_CMD_ERROR_STATUS;
    }

	ncl_cmd->completion(ncl_cmd);

    sys_free(SLOW_DATA, req);
}

fast_code rc_req_t* l2p_rc_req_prepare(struct ncl_cmd_t *ncl_cmd)
{
	rc_req_t *req;

	req = sys_malloc(SLOW_DATA, sizeof(rc_req_t));
	sys_assert(req);

	req->list_len = ncl_cmd->addr_param.common_param.list_len;
	req->pda_list= ncl_cmd->addr_param.common_param.pda_list;
	req->info_list = ncl_cmd->addr_param.common_param.info_list;
	req->bm_pl_list = ncl_cmd->user_tag_list;

	req->flags.all = 0;

	req->caller_priv = ncl_cmd;
	req->cmpl = l2p_rc_done;

	return req;
}

fast_code bool ftl_uc_pda_chk(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	u32 pda_cnt = ncl_cmd->addr_param.common_param.list_len;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

	for (i = 0; i < pda_cnt; i++) {
        if ((info[i].status == ficu_err_par_err) || (info[i].status == ficu_err_dfu) || (info[i].status == ficu_err_du_erased))    
			return false;
	}

	return true;
}

fast_code void l2p_seg_read_done(struct ncl_cmd_t *ncl_cmd)
{
	l2p_load_mgr_t *load_mgr = &l2p_load_mgr;
	l2p_ncl_cmd_t *l2p_cmd = (l2p_ncl_cmd_t*)ncl_cmd;
	u32 seg_id = (u32)ncl_cmd->caller_priv;
	u32 idx = l2p_cmd - _l2p_ncl_cmds;

    u32 i = 0;
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
    #if NCL_FW_RETRY
	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
    #endif

    #ifdef NCL_HAVE_reARD
    pda_t *pda_list = ncl_cmd->addr_param.common_param.pda_list;
#endif

    if (ncl_cmd->status)    //add retry flow //tony 20201026
    {
#if NCL_FW_RETRY
        if (ncl_cmd->retry_step == default_read)
        {
	        //ftl_apl_trace(LOG_INFO, 0, "l2p_rd_err_handling, ncl_cmd: 0x%x", ncl_cmd);
            rd_err_handling(ncl_cmd);
            return;
        }
        else if(ncl_cmd->retry_step != raid_recover_fail)
        {
            u32 nsid = INT_NS_ID;
            bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
			ftl_set_1bit_data_error_flag(mTRUE);
            if (fcns_raid_enabled(nsid) && ftl_uc_pda_chk(ncl_cmd) && (rced == false))
            //if (fcns_raid_enabled(nsid) && (rced == false))
            {
            	for (i = 0; i < cnt; i++)
                {
        		    if (info[i].status > ficu_err_du_ovrlmt)
                    {
                        rc_req_t* rc_req = l2p_rc_req_prepare(ncl_cmd);
                        raid_correct_push(rc_req);
                        return;
                    }
    	        }
            }
        }
#else
#ifdef NCL_HAVE_reARD
        if(ncl_cmd->re_ard_flag == false)
#endif
        {
            u32 nsid = INT_NS_ID;
            bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && (rced == false))
            {
            	for (i = 0; i < cnt; i++)
                {
        		    if (info[i].status == ficu_err_du_uc)
                    {
                        rc_req_t* rc_req = l2p_rc_req_prepare(ncl_cmd);
                        raid_correct_push(rc_req);
                        return;
                    }
    	        }
            }
        }
#ifdef NCL_HAVE_reARD
        else
        {
            ftl_apl_trace(LOG_INFO, 0x237e, "l2p need retry");
            ncl_cmd->re_ard_flag = false;
            //l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_id);
            l2p_seg_reread(ncl_cmd, cnt, pda_list, info, ncl_cmd->user_tag_list, seg_id);
            return;
        }
#endif
#endif

        _l2p_free_cmd_bmp |= (1ULL << idx);
        //sys_assert(ncl_cmd->status == 0);
        shr_ftl_flags.b.l2p_all_ready = true;
        QBT_TAG = false;
        ftl_apl_trace(LOG_INFO, 0x96c9, "ncl fail, set all l2p ready QBT_TAG %d", QBT_TAG);
#if (SPOR_FLOW == mENABLE)
        if (l2p_cmd->flags.b.misc_data == mTRUE)
        {
            ftl_set_blist_data_error_flag(mTRUE);
        }
        else
        {
            ftl_set_l2p_data_error_flag(mTRUE);
        }

        ftl_set_read_data_fail_flag(mTRUE);
#endif
        return;
    }

	//ftl_apl_trace(LOG_INFO, 0, "l2p_seg_read_done");
    ncl_cmd->flags &= ~NCL_CMD_L2P_READ_FLAG;  //prevent RD_FTL bit to be use for other ncl_cmd of read.

    _l2p_free_cmd_bmp |= (1ULL << idx);

	if (l2p_cmd->flags.b.misc_data == false) {
		int ret = test_and_set_bit(seg_id, load_mgr->ready_bmp);
		sys_assert(ret == 0);

		load_mgr->ttl_ready++;
		if (load_mgr->ttl_ready == shr_l2p_seg_cnt) {
			shr_ftl_flags.b.l2p_all_ready = true;
			ftl_apl_trace(LOG_INFO, 0x70ef, "all l2p ready");
		}
	}

	if (l2p_cmd->flags.b.bg_load || l2p_cmd->flags.b.urg_load) {
		load_mgr->otf_load--;
		int ret = test_and_clear_bit(seg_id, load_mgr->loading_bmp);
		sys_assert(ret == 1);

		l2p_bg_load_req_t *bg_load;
		l2p_urg_load_req_t *urg_load;
		struct list_head *curr, *saved;

		if ((seg_id >= load_mgr->seg_off) && (seg_id < load_mgr->seg_end)) {
			load_mgr->ready_cnt++;
		} else {
			list_for_each_safe(curr, saved, &load_mgr->bg_waiting_list) {
				bg_load = container_of(curr, l2p_bg_load_req_t, entry);
				if ((seg_id >= bg_load->seg_off) && (seg_id < bg_load->seg_end)) {
					bg_load->ready_cnt++;
					break;
				}
			}
		}

		list_for_each_safe(curr, saved, &load_mgr->urg_loading_list) {
			urg_load = container_of(curr, l2p_urg_load_req_t, entry);
			if (urg_load->seg == seg_id) {
				cpu_msg_issue(urg_load->tx, CPU_MSG_L2P_LOAD_DONE, 0, seg_id);
				list_del(&urg_load->entry);
				sys_free(FAST_DATA, urg_load);
			}
		}

		//execute urgent load first
		if (!list_empty(&load_mgr->urg_waiting_list))
			ftl_l2p_urgent_load_exec();

		//continue background load
		sys_assert(load_mgr->cur_bg_req);
		if (load_mgr->ready_cnt == load_mgr->seg_cnt) {
			bg_load = load_mgr->cur_bg_req;
			sys_free(FAST_DATA, bg_load);
			load_mgr->cur_bg_req = NULL;

			if (!list_empty(&load_mgr->bg_waiting_list))
				ftl_l2p_bg_load_exec();
		} else {
			evt_set_cs(load_mgr->evt_load, 0, 0, CS_TASK);
		}
	}

}

static inline l2p_ncl_cmd_t *get_l2p_ncl_cmd(void)
{
	u32 _idx;

	if (_l2p_free_cmd_bmp == 0) {
		while (_l2p_free_cmd_bmp != (1ULL << MAX_L2P_LOAD_CMD) - 1)
		{
			cpu_msg_isr();
		}
	}

	_idx = ctz(_l2p_free_cmd_bmp);
	sys_assert(_idx < MAX_L2P_LOAD_CMD);
	_l2p_free_cmd_bmp &= ~(1ULL << _idx);

	return &_l2p_ncl_cmds[_idx];
}

static inline void wait_cmd_idle(void)
{
	while (_l2p_free_cmd_bmp != (1ULL << MAX_L2P_LOAD_CMD) - 1)
	{
		cpu_msg_isr();
	}
}

static inline u32 scrub_l2p_seg(u32 inv_seg_cnt, u64 inv_seg_base)
{
	if (inv_seg_cnt) {
		//u64 len = ((u64)inv_seg_cnt) << seg_size_shf;
		u64 len = ((u64)inv_seg_cnt) * seg_size;
		sys_assert(inv_seg_base != 0);
		ftl_apl_trace(LOG_INFO, 0x5fd0, "clear l2p seg %x %x", inv_seg_base, inv_seg_cnt);
		bm_scrub_ddr(inv_seg_base, len, 0xffffffff);
		inv_seg_cnt = 0;
	}
	return inv_seg_cnt;
}

fast_code pda_t ftl_chk_remap_pda (pda_t pda)
{
	u32 remap_mask = (1<<shr_nand_info.pda_block_shift) - 1;
	spb_id_t spb_id = pda >> shr_nand_info.pda_block_shift;
	u32 remap_idx = ftl_remap.remap_idx[spb_id];
	if (remap_idx == 0xFFFF)
		return pda;
	u32 interleave_id = pda >> DU_CNT_SHIFT;
	interleave_id &= (ftl_remap.interleave - 1);
	spb_id_t remap_spb = ftl_remap.remap_tbl[remap_idx * ftl_remap.interleave + interleave_id];
	pda = (remap_spb << shr_nand_info.pda_block_shift) | (pda & remap_mask);
	return pda;
}
fast_code pda_t ftl_QBT_seg_lookup(u32 segid, spb_id_t spb_id, u8 page_in_wl)
{
    pda_t qbt_pda;
	u16 ch, ce, page = 0;
	u8 lun = 0;
	u16 geo_ch = get_ch();
	u16 geo_ce = get_ce();
	u8  geo_lun = get_lun();
	page = (segid/(geo_ch*geo_ce*geo_lun))*3 + page_in_wl;
	if(page == 1152)
	{
		ftl_apl_trace(LOG_ERR, 0xfcaf, "segid %d spb_id %d page_in_wl %d\n", segid, spb_id, page_in_wl);
		sys_assert(0);
	}
	ch = (segid%geo_ch);
	ce = (segid%(geo_ch*geo_ce)) / geo_ch;
	lun = (segid/(geo_ch*geo_ce)) % geo_lun;
	if(geo_lun == 2)
	{
		qbt_pda = (spb_id << 21) + (page << 10) + (lun << 9) + (ce << 6) + (ch << 3);// 8A|�DN du:2bit pn:1bit ch:3bit ce:3bit lun:1bit page:11bit blk:11bit total:32bit
	}
	else
	{
		qbt_pda = (spb_id << 21) + (page << 10) + (lun << 8) + (ce << 6) + (ch << 3);// 16A|�DN du:2bit pn:1bit ch:3bit ce:2bit lun:2bit page:11bit blk:11bit total:32bit
	}
	return ftl_chk_remap_pda(qbt_pda);
}
#ifdef SKIP_MODE
fast_code u8* ftl_get_spb_defect(u32 spb_id)
{
	u32 df_width = occupied_by(shr_nand_info.interleave, 8) << 3;
	u32 index = (spb_id * df_width) >> 3;
	return (gl_pt_defect_tbl + index);
}

fast_code u8 ftl_get_defect_pl_pair(u8* ftl_df, u32 interleave)
{
	if(interleave%shr_nand_info.geo.nr_planes!=0) panic("interleave not pl 0\n");
	u32 idx = interleave >> 3;
	u32 off = interleave & (7);
	if (shr_nand_info.geo.nr_planes == 4)
		return (((ftl_df[idx] >> off)&0xf));
	else
		return (((ftl_df[idx] >> off)&0x3));
}
fast_code u8 ftl_defect_check(u8* ftl_df, u32 interleave)
{
	u32 idx = interleave >> 3;
	u32 off = interleave & (7);
	return ((ftl_df[idx] >> off)&1);
}
#endif


// 4Plane by Howard
#if (SPOR_FLOW == mENABLE)
fast_code void ftl_qbt_pbt_reload(l2p_ele_t *ele, u8 pool_id, u8 load_l2p_only)
#else
fast_code void ftl_qbt_pbt_reload(l2p_ele_t *ele)
#endif
{
//l2p var
	u32 dtag_off = off2ddtag(_l2p_tbl.base);
	//u32 l2p_seg_cnt = shr_l2p_seg_cnt;
	u32 l2p_seg_cnt = _ftl_hns[1].l2p_ele.seg_end; // for dynamic OP
#if (SPOR_FLOW == mENABLE)
    spb_id_t  blk_idx;
#else
	spb_id_t  qbt_idx;
	u8  grp;
	u8  valid_qbt_grp = 0;
    u8  qbt_loop;
#endif
////////////////////////////

//misc var
	dtag_t dtag;
	u32 vtbl_dtag_cnt;
	u32 misc_seg_cnt = ftl_l2p_misc_cnt();
	u32 misc_dtag_cnt[2] = {0, shr_blklistbuffer_need};
	u32 misc_dtag_start[2] = {0, shr_blklistbuffer_start[0]};
    bool raid_support = fcns_raid_enabled(INT_NS_ID);
	u32 plane_idx;
	u8 parity_die = shr_nand_info.lun_num;
	u8 parity_pln_pair = BLOCK_ALL_BAD;
	u8  misc_du_cnt = 0;
	u8  dtag_desc = 0;
	u8  misc_idx = 0;
	bool misc_start = false;
	bool misc_done = false;
/////////////////////////////
	//#if (SPOR_FLOW == mENABLE)
    //load_l2p_only = mFALSE;
	//#endif
//vac table sram dtag alloc
	dtag = ftl_l2p_get_vcnt_buf(&vtbl_dtag_cnt, NULL);
	//ftl_apl_trace(LOG_ALW, 0, "[L2P]vac sdtag 0x%x", dtag.dtag);
	misc_dtag_cnt[0] = vtbl_dtag_cnt;
	misc_dtag_start[0] = dtag.dtag;
	//ftl_apl_trace(LOG_ALW, 0, "[L2P]check sdtag 0x%x", misc_dtag_start[0]);
////////////////////////////

//share var
	l2p_ncl_cmd_t *l2p_cmd;
	pda_t *pda_list;
	bm_pl_t *bm_pl;
	struct info_param_t *info;

	l2p_cmd = get_l2p_ncl_cmd();
	bm_pl = l2p_cmd->bm_pl;
	pda_list = l2p_cmd->pda;
	info = l2p_cmd->info;
	l2p_cmd->flags.all = 0;

	u32 ttl_seg_cnt = l2p_seg_cnt + misc_seg_cnt;
	u32 seg_idx = ele->seg_off;
	u16 wl, page;
	//u8	geo_pln = get_mp();
	u8  pln_idx, ofst, j;
	pda_t pda = 0;
// For 4 plane by Howard
	u32 du_idx = seg_idx;
	u32 l2p_du_cnt = l2p_seg_cnt * NR_L2PP_IN_SEG;
//  u32 misc_du_cnt; //(misc_dtag_cnt[0]+misc_dtag_cnt[1])
//	u32 ttl_du_cnt = ttl_seg_cnt * NR_L2PP_IN_SEG;


////////////////////////////
#if(SPOR_TIME_COST == mENABLE)
    u64 time_start = get_tsc_64();
#endif

    ftl_apl_trace(LOG_ALW, 0x11b7, "[IN] ftl_qbt_pbt_reload");

#if(QBT_TLC_MODE_DBG == mENABLE)
	//ftl_apl_trace(LOG_ALW, 0, "[L2P]Start l2p reload skip mode");
	ftl_apl_trace(LOG_ALW, 0x5540, "[L2P]ele->seg_off %d", ele->seg_off);
	ftl_apl_trace(LOG_ALW, 0x211e, "[L2P]ele->seg_end %d", ele->seg_end);
	ftl_apl_trace(LOG_ALW, 0x3cff, "[L2P]l2p_du_cnt %d", l2p_du_cnt);
	ftl_apl_trace(LOG_ALW, 0x09e3, "[L2P]ttl_seg_cnt %d", ttl_seg_cnt);
	ftl_apl_trace(LOG_ALW, 0xf0b4, "[L2P]ttl_du_cnt %d", l2p_du_cnt+misc_dtag_cnt[0]+misc_dtag_cnt[1]);
#endif

//check qbt grp
#if (SPOR_FLOW == mDISABLE)
	for(grp=0; grp<2; grp++)
	{
    	if(QBT_BLK_IDX == QBT_GRP_HANDLE[grp].head_qbt_idx)
    	{
			valid_qbt_grp = grp;
			break;
		}
	}
#endif
/////////////////////////////

//start load qbt
//#ifdef SKIP_MODE
#if (SPOR_FLOW == mENABLE)
    ftl_set_read_data_fail_flag(mFALSE);
    if(pool_id != SPB_POOL_PBT) 
    { 
		blk_idx = ftl_spor_get_head_from_pool(pool_id); 
    }	 
    else 
    { 
		blk_idx = ftl_spor_get_tail_from_pool(pool_id); 
		if((QBT_BLK_CNT == 2) && (ftl_spor_get_pre_blk(blk_idx) != INV_U16))//2T 2plane 
		{ 
			u16 first_pbt_blk = ftl_spor_get_pre_blk(blk_idx); 
			if(ftl_spor_get_blk_sn(first_pbt_blk) > ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_PBT_ALLOC))) 
			{ 
				blk_idx = first_pbt_blk; 
			} 
		} 
		ttl_seg_cnt = ftl_get_last_pbt_seg(); 
		l2p_du_cnt  = ttl_seg_cnt * NR_L2PP_IN_SEG; 
    } 
    while(blk_idx != INV_U16)
#else
	for(qbt_loop=0; qbt_loop<QBT_BLK_CNT; qbt_loop++)
#endif
    {

#if (SPOR_FLOW == mENABLE)
#ifdef SKIP_MODE
		u8* spb_df_ptr = ftl_get_spb_defect(blk_idx);
	    u8 pln_pair = BLOCK_NO_BAD;
#endif
#else
		qbt_idx = (qbt_loop>0)? QBT_GRP_HANDLE[valid_qbt_grp].tail_qbt_idx:QBT_GRP_HANDLE[valid_qbt_grp].head_qbt_idx;
#ifdef SKIP_MODE
		u8* spb_df_ptr = ftl_get_spb_defect(qbt_idx);
#endif

#endif

        if(raid_support){
    		parity_die = shr_nand_info.lun_num - 1;
    		plane_idx = shr_nand_info.interleave - shr_nand_info.geo.nr_planes;
    		while(1)
    		{	
#ifdef SKIP_MODE	
				pln_pair = ftl_get_defect_pl_pair(spb_df_ptr, plane_idx);	
#endif
    			if ((pln_pair == BLOCK_ALL_BAD) && (plane_idx / shr_nand_info.geo.nr_planes == parity_die)) {
    				parity_die--;
    			} else {
                    if(shr_nand_info.geo.nr_planes - pop32(parity_pln_pair) == 1){
    			        parity_pln_pair = BLOCK_ALL_BAD; // skip all plane
                    }else{
                        bool ret = false;
                        u32 parity_die_pln_idx = 0;
                        for (parity_die_pln_idx = 0; parity_die_pln_idx < shr_nand_info.geo.nr_planes; parity_die_pln_idx++){
                            if (((pln_pair >> parity_die_pln_idx)&1)==0){ //good plane
                                ret = true;
                                    break;
                            }
                        }
                        sys_assert(ret); 
                        parity_pln_pair = pln_pair | (1<<parity_die_pln_idx); // skip parity plane
                    }
    				break;
    			}
    			if (plane_idx == 0)
    				break;
    			plane_idx-= shr_nand_info.geo.nr_planes;
    		}
        }

#if (SPOR_FLOW == mENABLE)
        spor_read_pbt_blk[spor_read_pbt_cnt] = blk_idx;
		spor_read_pbt_cnt++;
		ftl_apl_trace(LOG_ERR, 0xa31f, "[L2P]cur qbt_idx %d, parity_die %d, read_pbt_cnt %d, pln_pair %u, parity_pln_pair%u",
			blk_idx, parity_die,spor_read_pbt_cnt, pln_pair, parity_pln_pair);
#else
                ftl_apl_trace(LOG_ALW, 0x1250, "[L2P]cur qbt_idx %d, parity_die %d", qbt_idx, parity_die);
#endif

        //start at wl 0
		for(wl=0; wl<(shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell); wl++)
        {
			u16 ptr = 0; // plane from 0 ~ interleave
			if(shr_nand_info.geo.nr_planes == 2)
			{
				if(seg_idx >= ttl_seg_cnt)
				{
					ftl_apl_trace(LOG_ALW, 0xb9d7, "[L2P]2 L2P & MISC done %d %d", seg_idx, misc_idx);
					break;
				}

				do{
#ifdef SKIP_MODE
					u8 pln_pair = ftl_get_defect_pl_pair(spb_df_ptr, ptr);
#else
					u8 pln_pair = 0;
#endif
					if((seg_idx == ele->seg_end) && (misc_start == false))
					{
						ftl_apl_trace(LOG_ALW, 0x66e3, "[L2P]L2P done seg_idx %d, PDA : 0x%x", seg_idx, pda);
	                    #if (SPOR_FLOW == mENABLE)
					    if(load_l2p_only)
	                    {
	                        goto QBT_RELOAD_END;
	                    }
					    #endif

						ftl_apl_trace(LOG_ALW, 0x6153, "[L2P]Start misc reload skip mode - A");
						misc_start = true;
					}

					if(seg_idx >= ttl_seg_cnt)
					{
						ftl_apl_trace(LOG_ALW, 0xb614, "[L2P]1 L2P & MISC done %d %d", seg_idx, misc_idx);
						break;
					}

	                if(raid_support){
	    				if((ptr>>FTL_PN_SHIFT) == parity_die){
	    					pln_pair = parity_pln_pair; //for skip parity_die
	    				}
					}
	                //--------------------------------------------------------------------
					switch (pln_pair)
					{
						case 0 :
							for (pln_idx=0; pln_idx<shr_nand_info.geo.nr_planes; pln_idx++)
							{
								l2p_cmd = get_l2p_ncl_cmd();
								bm_pl = l2p_cmd->bm_pl;
								pda_list = l2p_cmd->pda;
								info = l2p_cmd->info;
								l2p_cmd->flags.all = 0;
								u32 temp_ptr = 0;

								for (j=0; j<NR_L2PP_IN_SEG; j++)
								{
									if(pln_idx == 0)// pln_0
									{
										page = wl*3 + (j/8);
										if((j>3)&&(j<8))
										{
											temp_ptr = ptr + 1;
										}
										else
										{
											temp_ptr = ptr;
										}
									}
									else// pln_1
									{
										page = (j<4)? (wl*3 + pln_idx) : (wl*3 + pln_idx + 1);
										if((j<4)||(j>7))
										{
											temp_ptr = ptr + 1;
										}
										else
										{
											temp_ptr = ptr;
										}
									}
									ofst = (j%4);
	                                #if (SPOR_FLOW == mENABLE)
									pda = (blk_idx << shr_nand_info.pda_block_shift) + (page << shr_nand_info.pda_page_shift) + (temp_ptr << DU_CNT_SHIFT) + ofst;
									#else
									pda = (qbt_idx << shr_nand_info.pda_block_shift) + (page << shr_nand_info.pda_page_shift) + (temp_ptr << DU_CNT_SHIFT) + ofst;
	                                #endif
									pda_list[j] = pda;

									if(misc_start == true)
									{
										l2p_cmd->flags.b.misc_data = 1;
									    //ftl_apl_trace(LOG_ALW, 0, "blist pda : 0x%x", pda); // added by Sunny, for test
										if(misc_du_cnt == (misc_dtag_cnt[0]+misc_dtag_cnt[1]))
										{
											if(misc_du_cnt > NR_L2PP_IN_SEG)
											{
												misc_du_cnt = misc_du_cnt - NR_L2PP_IN_SEG;
												ftl_apl_trace(LOG_ALW, 0x8123, "[L2P]MISC done real cnt %d", misc_du_cnt);
											}
											misc_done = true;
											break;
										}
										if(misc_idx == 0)
										{
											dtag_desc = ((j+1) > misc_dtag_cnt[0])? 1 : 0;
											dtag_off = (dtag_desc > 0)? (j - misc_dtag_cnt[0]) : j;
										}
										else
										{
											dtag_desc = 1;
											dtag_off = shr_l2pp_per_seg - misc_dtag_cnt[0] + j;
										}
										dtag.dtag = misc_dtag_start[dtag_desc] + dtag_off;
										ftl_apl_trace(LOG_ALW, 0x1b1b, "[L2P]pda 0x%x dtag_off %d dtag 0x%x", pda, dtag_desc, dtag_off, dtag.dtag);
										if(dtag_desc == 0)
				            			{
											bm_pl[j].pl.dtag = dtag.dtag;
											bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
											bm_pl[j].pl.nvm_cmd_id = 0;
										}
										else
										{
											bm_pl[j].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
											bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
											bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
										}
										misc_du_cnt++;
									}
									else
									{
										bm_pl[j].pl.dtag = (dtag_off + seg_idx * NR_L2PP_IN_SEG + j) | DTAG_IN_DDR_MASK;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
										bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
									}
									bm_pl[j].pl.du_ofst = j;// check??

									#if (QBT_TLC_MODE == mENABLE)
										info[j].pb_type = NAL_PB_TYPE_XLC;
									#else
										info[j].pb_type = NAL_PB_TYPE_SLC;
									#endif
										info[j].status = 0;
								} // for (j=0; j<NR_L2PP_IN_SEG; j++)

								if(misc_done == true)
								{
									ftl_apl_trace(LOG_ALW, 0x4f8c, "[L2P]MISC done cnt %d", misc_du_cnt);
									l2p_seg_read(l2p_cmd, misc_du_cnt, seg_idx);
								}
								else
								{
									l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_idx);
								}
								seg_idx++;
								if(misc_start == true)
								{
									misc_idx++;
								}
								if(seg_idx == ele->seg_end)
								{
									ftl_apl_trace(LOG_ALW, 0x3b6d, "[L2P]L2P done seg_idx %d, PDA : 0x%x", seg_idx, pda);
	                                #if (SPOR_FLOW == mENABLE)
	            				    if(load_l2p_only)
	                                {
	                                    goto QBT_RELOAD_END;
	                                }
	            				    #endif
									ftl_apl_trace(LOG_ALW, 0x6152, "[L2P]Start misc reload skip mode - B");
									misc_start = true;
								}

								if(seg_idx >= ttl_seg_cnt)
								{
									ftl_apl_trace(LOG_ALW, 0x1c0e, "[L2P]0 L2P & MISC done %d %d", seg_idx, misc_idx);
									break;
								}
							} // for (pln_idx=0; pln_idx<shr_nand_info.geo.nr_planes; pln_idx++)
							ptr+=2;
							break;
						case 1 :
							l2p_cmd = get_l2p_ncl_cmd();
							bm_pl = l2p_cmd->bm_pl;
							pda_list = l2p_cmd->pda;
							info = l2p_cmd->info;
							l2p_cmd->flags.all = 0;
							ptr++;

							for (j = 0; j<NR_L2PP_IN_SEG; j++)
							{
								page = wl*3 + (j/4);
								ofst = (j%4);
	                            #if (SPOR_FLOW == mENABLE)
								pda = (blk_idx << shr_nand_info.pda_block_shift) + (page << shr_nand_info.pda_page_shift) + (ptr << DU_CNT_SHIFT) + ofst;
								#else
								pda = (qbt_idx << 21) + (page << 10) + (ptr << DU_CNT_SHIFT) + ofst;
	                            #endif
								pda_list[j] = pda;

								if(misc_start == true)
								{
									l2p_cmd->flags.b.misc_data = 1;
								    //ftl_apl_trace(LOG_ALW, 0, "blist pda : 0x%x", pda); // added by Sunny, for test
									if(misc_du_cnt == (misc_dtag_cnt[0]+misc_dtag_cnt[1]))
									{
										if(misc_du_cnt > NR_L2PP_IN_SEG)
										{
											misc_du_cnt = misc_du_cnt - NR_L2PP_IN_SEG;
											ftl_apl_trace(LOG_ALW, 0x547b, "[L2P]MISC done real cnt %d", misc_du_cnt);
										}
										misc_done = true;
										break;
									}
									if(misc_idx == 0)
									{
										dtag_desc = ((j+1) > misc_dtag_cnt[0])? 1 : 0;
										dtag_off = (dtag_desc > 0)? (j - misc_dtag_cnt[0]) : j;
									}
									else
									{
										dtag_desc = 1;
										dtag_off = shr_l2pp_per_seg - misc_dtag_cnt[0] + j;
									}
									dtag.dtag = misc_dtag_start[dtag_desc] + dtag_off;

									ftl_apl_trace(LOG_ALW, 0x85b3, "[L2P]pda 0x%x, dtag_off %d dtag 0x%x",
										pda, dtag_desc, dtag_off, dtag.dtag);
									if(dtag_desc == 0)
			            			{
										bm_pl[j].pl.dtag = dtag.dtag;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
										bm_pl[j].pl.nvm_cmd_id = 0;
									}
									else
									{
										bm_pl[j].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
										bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
									}
									misc_du_cnt++;
								}
								else
								{
									bm_pl[j].pl.dtag = (dtag_off + seg_idx * NR_L2PP_IN_SEG + j) | DTAG_IN_DDR_MASK;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
									bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
								}
								bm_pl[j].pl.du_ofst = j;// check??

								#if (QBT_TLC_MODE == mENABLE)
									info[j].pb_type = NAL_PB_TYPE_XLC;
								#else
									info[j].pb_type = NAL_PB_TYPE_SLC;
								#endif
									info[j].status = 0;
							}
							if(misc_done == true)
							{
								ftl_apl_trace(LOG_ALW, 0x5116, "[L2P]MISC done cnt %d", misc_du_cnt);
								l2p_seg_read(l2p_cmd, misc_du_cnt, seg_idx);
							}
							else
							{
								l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_idx);
							}
							if(misc_start == true)
							{
								misc_idx++;
							}
							ptr++;
							seg_idx++;
							break;
						case 2 :
							l2p_cmd = get_l2p_ncl_cmd();
							bm_pl = l2p_cmd->bm_pl;
							pda_list = l2p_cmd->pda;
							info = l2p_cmd->info;
							l2p_cmd->flags.all = 0;

							for (j = 0; j<NR_L2PP_IN_SEG; j++)
							{
								page = wl*3 + (j/4);
								ofst = (j%4);
	                            #if (SPOR_FLOW == mENABLE)
								pda = (blk_idx << shr_nand_info.pda_block_shift) + (page << shr_nand_info.pda_page_shift) + (ptr << DU_CNT_SHIFT) + ofst;
								#else
								pda = (qbt_idx << 21) + (page << 10) + (ptr << DU_CNT_SHIFT) + ofst;
	                            #endif
								pda_list[j] = pda;

								if(misc_start == true)
								{
									//ftl_apl_trace(LOG_ALW, 0, "blist pda : 0x%x", pda); // added by Sunny, for test
									if(misc_du_cnt == (misc_dtag_cnt[0]+misc_dtag_cnt[1]))
									{
										if(misc_du_cnt > NR_L2PP_IN_SEG)
										{
											misc_du_cnt = misc_du_cnt - NR_L2PP_IN_SEG;
											ftl_apl_trace(LOG_ALW, 0x18f9, "[L2P]MISC done real cnt %d", misc_du_cnt);
										}
										misc_done = true;
										break;
									}
									ftl_apl_trace(LOG_ALW, 0x08c1, "[L2P]pda 0x%x", pda);
									if(misc_idx == 0)
									{
										dtag_desc = ((j+1) > misc_dtag_cnt[0])? 1 : 0;
										dtag_off = (dtag_desc > 0)? (j - misc_dtag_cnt[0]) : j;
									}
									else
									{
										dtag_desc = 1;
										dtag_off = shr_l2pp_per_seg - misc_dtag_cnt[0] + j;
									}
									dtag.dtag = misc_dtag_start[dtag_desc] + dtag_off;
									ftl_apl_trace(LOG_ALW, 0x82e6, "misc dtag_off %d dtag 0x%x", dtag_desc, dtag_off, dtag.dtag);
									if(dtag_desc == 0)
			            			{
										bm_pl[j].pl.dtag = dtag.dtag;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
										bm_pl[j].pl.nvm_cmd_id = 0;
									}
									else
									{
										bm_pl[j].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
										bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
									}
									misc_du_cnt++;
								}
								else
								{
									bm_pl[j].pl.dtag = (dtag_off + seg_idx * NR_L2PP_IN_SEG + j) | DTAG_IN_DDR_MASK;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
									bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
								}
								bm_pl[j].pl.du_ofst = j;// check??

								#if (QBT_TLC_MODE == mENABLE)
									info[j].pb_type = NAL_PB_TYPE_XLC;
								#else
									info[j].pb_type = NAL_PB_TYPE_SLC;
								#endif
									info[j].status = 0;
							}
							if(misc_done == true)
							{
								ftl_apl_trace(LOG_ALW, 0x5370, "[L2P]MISC done cnt %d", misc_du_cnt);
								l2p_seg_read(l2p_cmd, misc_du_cnt, seg_idx);
							}
							else
							{
								l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_idx);
							}
							if(misc_start == true)
							{
								misc_idx++;
							}
							ptr+=2;
							seg_idx++;
							break;
						case 3 :// 2pln defect
							ptr+=2;
							break;
						default :
							panic("pln case != 0~3\n");
					}
					//--------------------------------------------------------------------
#if (SPOR_FLOW == mENABLE)
	                if(ftl_get_read_data_fail_flag()) {break;}
#endif
				}while(ptr<(shr_nand_info.lun_num*shr_nand_info.geo.nr_planes));
				//L2P_LAST_PDA = pda;
				//ftl_apl_trace(LOG_ALW, 0, "L2P_LAST_PDA 0x%x", L2P_LAST_PDA);
#if (SPOR_FLOW == mENABLE)
	            if(ftl_get_read_data_fail_flag()) {break;}
#endif
			}
			else if(shr_nand_info.geo.nr_planes == 4)
			{
				if(du_idx >= l2p_du_cnt+misc_dtag_cnt[0]+misc_dtag_cnt[1])
				{
					ftl_apl_trace(LOG_ALW, 0xb204, "[L2P]4 L2P & MISC done %d", du_idx);
					break;
				}

				// For each plane
				do{
#ifdef SKIP_MODE
					u8 pln_type = ftl_get_defect_pl_pair(spb_df_ptr, ptr);
#else
					u8 pln_type = 0;
#endif	
					if((du_idx == l2p_du_cnt) && (misc_start == false))
					{
						ftl_apl_trace(LOG_ALW, 0x6498, "[L2P]L2P done du_idx %d, PDA : 0x%x", du_idx, pda);
#if (SPOR_FLOW == mENABLE)
					    if(load_l2p_only)
	                    {
	                        goto QBT_RELOAD_END;
	                    }
#endif
						ftl_apl_trace(LOG_ALW, 0xe54b, "[L2P]Start misc reload skip mode - A");
						misc_start = true;
					}
					if(du_idx >= l2p_du_cnt+misc_dtag_cnt[0] + misc_dtag_cnt[1])
					{
						ftl_apl_trace(LOG_ALW, 0xdc16, "[L2P]3 L2P & MISC done %d", du_idx);
						break;
					}

	                if(raid_support)
					{
	    				if((ptr>>FTL_PN_SHIFT) == parity_die)
	    					pln_type = parity_pln_pair; //for skip parity_die
					}
					u8 i;
					u8 bits = pln_type;
					u8 df_pln = 0;
					//count how many defect planes in a die (0~4)
					for(i=0; i<shr_nand_info.geo.nr_planes; i++, bits>>=1)
					{
						df_pln += bits & 1;
					}
					u8 low_mid_up_idx, du_in_page;
					u32 temp_pln = 0;
					//bool misc_last = false;

					//ftl_apl_trace(LOG_ALW, 0, "[L2P]wl %d ptr %d pln_type %d", wl, ptr, pln_type);
					for (low_mid_up_idx=0; low_mid_up_idx<shr_nand_info.bit_per_cell; low_mid_up_idx++)
					{	
						if(du_idx >= l2p_du_cnt+misc_dtag_cnt[0]+misc_dtag_cnt[1])
						{
							ftl_apl_trace(LOG_ALW, 0xe7f3, "[L2P]2 L2P & MISC done %d", du_idx);
							break;
						}
						//ftl_apl_trace(LOG_ALW, 0, "[L2P]low_mid_up_idx %d", low_mid_up_idx);
						for (pln_idx=0; pln_idx<shr_nand_info.geo.nr_planes; pln_idx++)
						{	
							if(du_idx >= l2p_du_cnt+misc_dtag_cnt[0] + misc_dtag_cnt[1])
							{
								ftl_apl_trace(LOG_ALW, 0xcb27, "[L2P]1 L2P & MISC done %d", du_idx);
								break;
							}
							//ftl_apl_trace(LOG_ALW, 0, "[L2P]pln_idx %d", pln_idx);
							if (BIT_CHECK(pln_type, pln_idx)){
								continue;
							}
							for(du_in_page=0; du_in_page<DU_CNT_PER_PAGE; du_in_page++)
							{	
								//init a seg
								if ((du_idx % NR_L2PP_IN_SEG == 0) && du_idx!=0)
								{
									l2p_cmd = get_l2p_ncl_cmd();
									bm_pl = l2p_cmd->bm_pl;
									pda_list = l2p_cmd->pda;
									info = l2p_cmd->info;
									l2p_cmd->flags.all = 0;
								}

								page = wl * shr_nand_info.bit_per_cell + low_mid_up_idx;
								temp_pln = ptr + pln_idx;
#if (SPOR_FLOW == mENABLE)
								pda = (blk_idx << shr_nand_info.pda_block_shift) + (page << shr_nand_info.pda_page_shift) + (temp_pln << DU_CNT_SHIFT) + du_in_page;
#else
								pda = (qbt_idx << shr_nand_info.pda_block_shift) + (page << shr_nand_info.pda_page_shift) + (temp_pln << DU_CNT_SHIFT) + du_in_page;
#endif
								pda_list[du_idx % NR_L2PP_IN_SEG] = pda;

								j = du_idx % NR_L2PP_IN_SEG;

								if(!misc_start)
								{	//dtag_off = off2ddtag(_l2p_tbl.base);
									bm_pl[j].pl.dtag = (dtag_off + du_idx) | DTAG_IN_DDR_MASK;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
									bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
								}
								else
								{
									l2p_cmd->flags.b.misc_data = 1;

									if(misc_du_cnt >= misc_dtag_cnt[0])
									{
										dtag.dtag = misc_dtag_start[1] + (misc_du_cnt - misc_dtag_cnt[0]);
										bm_pl[j].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
										bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
									}
									else
									{
										dtag.dtag = misc_dtag_start[0] + misc_du_cnt;
										bm_pl[j].pl.dtag = dtag.dtag;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
										bm_pl[j].pl.nvm_cmd_id = 0;
									}

									ftl_apl_trace(LOG_ALW, 0xb7f7, "[L2P]pda 0x%x misc_du_cnt %d dtag 0x%x", pda, misc_du_cnt, dtag.dtag);
									misc_du_cnt++;
								}

								bm_pl[j].pl.du_ofst = j;
							#if (QBT_TLC_MODE == mENABLE)
								info[j].pb_type = NAL_PB_TYPE_XLC;
							#else
								info[j].pb_type = NAL_PB_TYPE_SLC;
							#endif
								info[j].status = 0;

								//ftl_apl_trace(LOG_ALW, 0, "[L2P]blk_idx %d page %d temp_pln %d du_in_page %d", blk_idx, page, temp_pln, du_in_page);
								//ftl_apl_trace(LOG_ALW, 0, "[L2P]j %d du_idx %d pda 0x%x dtag 0x%x", j, du_idx, pda, bm_pl[j].pl.dtag);
								//Read a seg
								if ((du_idx+1) % NR_L2PP_IN_SEG == 0){
									//ftl_apl_trace(LOG_ALW, 0, "[L2P]l2p_seg_read 12 du seg %d", du_idx/NR_L2PP_IN_SEG);
									l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, du_idx/NR_L2PP_IN_SEG);
								}
								else if(misc_du_cnt == (misc_dtag_cnt[0] + misc_dtag_cnt[1])){
									ftl_apl_trace(LOG_ALW, 0x469b, "[L2P]l2p_seg_read misc_du_cnt %d", misc_du_cnt);
									l2p_seg_read(l2p_cmd, (misc_du_cnt%NR_L2PP_IN_SEG), du_idx/NR_L2PP_IN_SEG);
								}

								du_idx++;

								if(du_idx == l2p_du_cnt)
								{
									ftl_apl_trace(LOG_ALW, 0x7ab0, "[L2P]L2P done du_idx %d, PDA : 0x%x", du_idx, pda);
#if (SPOR_FLOW == mENABLE)
				            		if(load_l2p_only)
									{
										goto QBT_RELOAD_END;
									}
#endif
									ftl_apl_trace(LOG_ALW, 0x5bdc, "[L2P]Start misc reload skip mode - B");
									misc_start = true;
								}
								if(du_idx >= l2p_du_cnt+misc_dtag_cnt[0] + misc_dtag_cnt[1])
								{
									ftl_apl_trace(LOG_ALW, 0x0118, "[L2P]0 L2P & MISC done %d", du_idx);
									break;
								}
							}
						}
					}
					ptr += 4;
				}while(ptr<(shr_nand_info.lun_num*shr_nand_info.geo.nr_planes));
#if (SPOR_FLOW == mENABLE)
	            if(ftl_get_read_data_fail_flag()) {break;}
#endif
			}
		} // for (each wordline)
		L2P_LAST_PDA = pda;
		ftl_apl_trace(LOG_ALW, 0x79f8, "L2P_LAST_PDA 0x%x", L2P_LAST_PDA);
#if (SPOR_FLOW == mENABLE)
        if(ftl_get_read_data_fail_flag()) {break;}
        blk_idx = ftl_spor_get_next_blk(blk_idx);
#endif
	} // end while(blk_idx != INV_U16)
//#endif

#if (SPOR_FLOW == mENABLE)
QBT_RELOAD_END:
#endif
    // wait cmd done & release vac sram dtag
	wait_cmd_idle();

#if (SPOR_FLOW == mENABLE)
    if(!ftl_get_read_data_fail_flag())
    {
        ftl_l2p_put_vcnt_buf((dtag_t)misc_dtag_start[0], misc_dtag_cnt[0], true);
    }
    else
    {
        ftl_set_read_data_fail_flag(mFALSE);
        ftl_set_blist_data_error_flag(mTRUE);
        ftl_set_l2p_data_error_flag(mTRUE);
        ftl_l2p_put_vcnt_buf((dtag_t)misc_dtag_start[0], misc_dtag_cnt[0], false);
    }
#else
	ftl_l2p_put_vcnt_buf((dtag_t)misc_dtag_start[0], misc_dtag_cnt[0], true);
#endif

#if(SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0x1e21, "Function time cost : %d us", time_elapsed_in_us(time_start));
#endif
}

#if(WARMBOOT_FTL_HANDLE == mENABLE)
#if(WA_FW_UPDATE == DISABLE)
slow_code_ex void ftl_warmboot_reload(l2p_ele_t *ele)
{
//l2p var
	u32 dtag_off = off2ddtag(_l2p_tbl.base);
	//u32 l2p_seg_cnt = shr_l2p_seg_cnt;
	u32 l2p_seg_cnt = _ftl_hns[1].l2p_ele.seg_end; // for dynamic OP
	spb_id_t  qbt_idx;
	u8  grp;
	u8  valid_qbt_grp = 0;
    u8  qbt_loop;
////////////////////////////

//misc var
	dtag_t dtag;
	u32 vtbl_dtag_cnt;
	u32 misc_seg_cnt = ftl_l2p_misc_cnt();
	u32 misc_dtag_cnt[2] = {0, shr_blklistbuffer_need};
	u32 misc_dtag_start[2] = {0, shr_blklistbuffer_start[0]};
	u8  misc_du_cnt = 0;
	u8  dtag_desc = 0;
	u8  misc_idx = 0;
	bool misc_start = false;
	bool misc_done = false;
/////////////////////////////

//vac table sram dtag alloc
	dtag = ftl_l2p_get_vcnt_buf(&vtbl_dtag_cnt, NULL);
	ftl_apl_trace(LOG_ALW, 0xe214, "[L2P]vac sdtag 0x%x", dtag.dtag);
	misc_dtag_cnt[0] = vtbl_dtag_cnt;
	misc_dtag_start[0] = dtag.dtag;
	ftl_apl_trace(LOG_ALW, 0x54d9, "[L2P]check sdtag 0x%x", misc_dtag_start[0]);
////////////////////////////

//share var
	l2p_ncl_cmd_t *l2p_cmd;
	pda_t *pda_list;
	bm_pl_t *bm_pl;
	struct info_param_t *info;
	u32 ttl_seg_cnt = l2p_seg_cnt + misc_seg_cnt;
	u32 seg_idx = ele->seg_off;
	u16 wl, page;
	u8	geo_pln = get_mp();
	u8  pln_idx, ofst, j;
	pda_t pda = 0;
////////////////////////////

#if(QBT_TLC_MODE_DBG == mENABLE)
	ftl_apl_trace(LOG_ALW, 0x4daa, "[L2P]Start l2p reload skip mode");
	ftl_apl_trace(LOG_ALW, 0x2ff1, "[L2P]ele->seg_off %d", ele->seg_off);
	ftl_apl_trace(LOG_ALW, 0x75f7, "[L2P]ele->seg_end %d", ele->seg_end);
	ftl_apl_trace(LOG_ALW, 0x2f2c, "[L2P]ttl_seg_cnt %d", ttl_seg_cnt);
#endif

//check qbt grp
	for(grp=0; grp<2; grp++)
	{
    	if(QBT_BLK_IDX == QBT_GRP_HANDLE[grp].head_qbt_idx)
    	{
			valid_qbt_grp = grp;
			break;
		}
	}
/////////////////////////////

//start load qbt
//#ifdef SKIP_MODE
	for(qbt_loop=0; qbt_loop<QBT_BLK_CNT; qbt_loop++)
    {
		qbt_idx = (qbt_loop>0)? QBT_GRP_HANDLE[valid_qbt_grp].tail_qbt_idx:QBT_GRP_HANDLE[valid_qbt_grp].head_qbt_idx;
#ifdef SKIP_MODE
				u8* spb_df_ptr = ftl_get_spb_defect(qbt_idx);
#endif


		ftl_apl_trace(LOG_ALW, 0x63a5, "[L2P]cur qbt_idx %d", qbt_idx);
        //start at wl 0
		for(wl=0; wl<384; wl++)
        {
			u16 ptr = 0;
			if(seg_idx >= ttl_seg_cnt)
			{
				ftl_apl_trace(LOG_ALW, 0xe0b1, "[L2P]2 L2P & MISC done %d %d", seg_idx, misc_idx);
				break;
			}

			do{
#ifdef SKIP_MODE
								u8 pln_pair = ftl_get_defect_pl_pair(spb_df_ptr,ptr);
#else
								u8 pln_pair = 0;
#endif


				if((seg_idx == ele->seg_end) && (misc_start == false))
				{
					ftl_apl_trace(LOG_ALW, 0x52d7, "[L2P]L2P done seg_idx %d, PDA : 0x%x", seg_idx, pda);
					ftl_apl_trace(LOG_ALW, 0x485f, "[L2P]Start misc reload skip mode - A");
					misc_start = true;
				}

				if(seg_idx >= ttl_seg_cnt)
				{
					ftl_apl_trace(LOG_ALW, 0x7087, "[L2P]1 L2P & MISC done %d %d", seg_idx, misc_idx);
					break;
				}
                //--------------------------------------------------------------------
				switch (pln_pair)
				{
					case 0 :
						for (pln_idx=0; pln_idx<shr_nand_info.geo.nr_planes; pln_idx++)
						{
							l2p_cmd = get_l2p_ncl_cmd();
							bm_pl = l2p_cmd->bm_pl;
							pda_list = l2p_cmd->pda;
							info = l2p_cmd->info;
							l2p_cmd->flags.all = 0;
							u32 temp_ptr = 0;

							for (j=0; j<NR_L2PP_IN_SEG; j++)
							{
								if(pln_idx == 0)// pln_0
								{
									page = wl*3 + (j/8);
									if((j>3)&&(j<8))
									{
										temp_ptr = ptr + 1;
									}
									else
									{
										temp_ptr = ptr;
									}
								}
								else// pln_1
								{
									page = (j<4)? (wl*3 + pln_idx) : (wl*3 + pln_idx + 1);
									if((j<4)||(j>7))
									{
										temp_ptr = ptr + 1;
									}
									else
									{
										temp_ptr = ptr;
									}
								}
								ofst = (j%4);
								pda = (qbt_idx << 21) + (page << 10) + (temp_ptr << DU_CNT_SHIFT) + ofst;
								pda_list[j] = pda;

								if(misc_start == true)
								{
									l2p_cmd->flags.b.misc_data = 1;
								    ftl_apl_trace(LOG_ALW, 0x6665, "blist pda : 0x%x", pda); // added by Sunny, for test
									if(misc_du_cnt == (misc_dtag_cnt[0]+misc_dtag_cnt[1]))
									{
										if(misc_du_cnt > NR_L2PP_IN_SEG)
										{
											misc_du_cnt = misc_du_cnt - NR_L2PP_IN_SEG;
											ftl_apl_trace(LOG_ALW, 0xe70c, "[L2P]MISC done real cnt %d", misc_du_cnt);
										}
										misc_done = true;
										break;
									}
									ftl_apl_trace(LOG_ALW, 0x6a56, "[L2P]pda 0x%x", pda);
									if(misc_idx == 0)
									{
										dtag_desc = ((j+1) > misc_dtag_cnt[0])? 1 : 0;
										dtag_off = (dtag_desc > 0)? (j - misc_dtag_cnt[0]) : j;
									}
									else
									{
										dtag_desc = 1;
										dtag_off = shr_l2pp_per_seg - misc_dtag_cnt[0] + j;
									}
									dtag.dtag = misc_dtag_start[dtag_desc] + dtag_off;
									ftl_apl_trace(LOG_ALW, 0x1e45, "misc dtag_desc %d dtag_off %d dtag 0x%x", dtag_desc, dtag_off, dtag.dtag);
									if(dtag_desc == 0)
			            			{
										bm_pl[j].pl.dtag = dtag.dtag;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
										bm_pl[j].pl.nvm_cmd_id = 0;
									}
									else
									{
										bm_pl[j].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
										bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
										bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
									}
									misc_du_cnt++;
								}
								else
								{
									bm_pl[j].pl.dtag = (dtag_off + seg_idx * NR_L2PP_IN_SEG + j) | DTAG_IN_DDR_MASK;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
									bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
								}
								bm_pl[j].pl.du_ofst = j;// check??

#if (QBT_TLC_MODE == mENABLE)
								info[j].pb_type = NAL_PB_TYPE_XLC;
#else
								info[j].pb_type = NAL_PB_TYPE_SLC;
#endif
								info[j].status = 0;
							} // for (j=0; j<NR_L2PP_IN_SEG; j++)

							if(misc_done == true)
							{
								ftl_apl_trace(LOG_ALW, 0x53bf, "[L2P]MISC done cnt %d", misc_du_cnt);
								l2p_seg_read(l2p_cmd, misc_du_cnt, seg_idx);
							}
							else
							{
								l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_idx);
							}
							seg_idx++;
							if(misc_start == true)
							{
								misc_idx++;
							}
							if(seg_idx == ele->seg_end)
							{
								ftl_apl_trace(LOG_ALW, 0x9571, "[L2P]L2P done seg_idx %d, PDA : 0x%x", seg_idx, pda);
								ftl_apl_trace(LOG_ALW, 0x4862, "[L2P]Start misc reload skip mode - B");
								misc_start = true;
							}

							if(seg_idx >= ttl_seg_cnt)
							{
								ftl_apl_trace(LOG_ALW, 0x682b, "[L2P]0 L2P & MISC done %d %d", seg_idx, misc_idx);
								break;
							}
						} // for (pln_idx=0; pln_idx<shr_nand_info.geo.nr_planes; pln_idx++)
						ptr+=2;
						break;
					case 1 :
						l2p_cmd = get_l2p_ncl_cmd();
						bm_pl = l2p_cmd->bm_pl;
						pda_list = l2p_cmd->pda;
						info = l2p_cmd->info;
						l2p_cmd->flags.all = 0;
						ptr++;

						for (j = 0; j<NR_L2PP_IN_SEG; j++)
						{
							page = wl*3 + (j/4);
							ofst = (j%4);
							pda = (qbt_idx << 21) + (page << 10) + (ptr << DU_CNT_SHIFT) + ofst;
							pda_list[j] = pda;

							if(misc_start == true)
							{
								l2p_cmd->flags.b.misc_data = 1;
							    ftl_apl_trace(LOG_ALW, 0x6c70, "blist pda : 0x%x", pda); // added by Sunny, for test
								if(misc_du_cnt == (misc_dtag_cnt[0]+misc_dtag_cnt[1]))
								{
									if(misc_du_cnt > NR_L2PP_IN_SEG)
									{
										misc_du_cnt = misc_du_cnt - NR_L2PP_IN_SEG;
										ftl_apl_trace(LOG_ALW, 0xae02, "[L2P]MISC done real cnt %d", misc_du_cnt);
									}
									misc_done = true;
									break;
								}
								ftl_apl_trace(LOG_ALW, 0x6283, "[L2P]pda 0x%x", pda);
								if(misc_idx == 0)
								{
									dtag_desc = ((j+1) > misc_dtag_cnt[0])? 1 : 0;
									dtag_off = (dtag_desc > 0)? (j - misc_dtag_cnt[0]) : j;
								}
								else
								{
									dtag_desc = 1;
									dtag_off = shr_l2pp_per_seg - misc_dtag_cnt[0] + j;
								}
								dtag.dtag = misc_dtag_start[dtag_desc] + dtag_off;
								ftl_apl_trace(LOG_ALW, 0x9861, "misc dtag_desc %d dtag_off %d dtag 0x%x", dtag_desc, dtag_off, dtag.dtag);
								if(dtag_desc == 0)
		            			{
									bm_pl[j].pl.dtag = dtag.dtag;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
									bm_pl[j].pl.nvm_cmd_id = 0;
								}
								else
								{
									bm_pl[j].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
									bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
								}
								misc_du_cnt++;
							}
							else
							{
								bm_pl[j].pl.dtag = (dtag_off + seg_idx * NR_L2PP_IN_SEG + j) | DTAG_IN_DDR_MASK;
								bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
								bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
							}
							bm_pl[j].pl.du_ofst = j;// check??

#if (QBT_TLC_MODE == mENABLE)
							info[j].pb_type = NAL_PB_TYPE_XLC;
#else
							info[j].pb_type = NAL_PB_TYPE_SLC;
#endif
							info[j].status = 0;
						}
						if(misc_done == true)
						{
							ftl_apl_trace(LOG_ALW, 0x0193, "[L2P]MISC done cnt %d", misc_du_cnt);
							l2p_seg_read(l2p_cmd, misc_du_cnt, seg_idx);
						}
						else
						{
							l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_idx);
						}
						if(misc_start == true)
						{
							misc_idx++;
						}
						ptr++;
						seg_idx++;
						break;
					case 2 :
						l2p_cmd = get_l2p_ncl_cmd();
						bm_pl = l2p_cmd->bm_pl;
						pda_list = l2p_cmd->pda;
						info = l2p_cmd->info;
						l2p_cmd->flags.all = 0;

						for (j = 0; j<NR_L2PP_IN_SEG; j++)
						{
							page = wl*3 + (j/4);
							ofst = (j%4);
							pda = (qbt_idx << 21) + (page << 10) + (ptr << DU_CNT_SHIFT) + ofst;
							pda_list[j] = pda;

							if(misc_start == true)
							{
								ftl_apl_trace(LOG_ALW, 0xa52d, "blist pda : 0x%x", pda); // added by Sunny, for test
								if(misc_du_cnt == (misc_dtag_cnt[0]+misc_dtag_cnt[1]))
								{
									if(misc_du_cnt > NR_L2PP_IN_SEG)
									{
										misc_du_cnt = misc_du_cnt - NR_L2PP_IN_SEG;
										ftl_apl_trace(LOG_ALW, 0xbc85, "[L2P]MISC done real cnt %d", misc_du_cnt);
									}
									misc_done = true;
									break;
								}
								ftl_apl_trace(LOG_ALW, 0x4347, "[L2P]pda 0x%x", pda);
								if(misc_idx == 0)
								{
									dtag_desc = ((j+1) > misc_dtag_cnt[0])? 1 : 0;
									dtag_off = (dtag_desc > 0)? (j - misc_dtag_cnt[0]) : j;
								}
								else
								{
									dtag_desc = 1;
									dtag_off = shr_l2pp_per_seg - misc_dtag_cnt[0] + j;
								}
								dtag.dtag = misc_dtag_start[dtag_desc] + dtag_off;
								ftl_apl_trace(LOG_ALW, 0x2aa3, "misc dtag_desc %d dtag_off %d dtag 0x%x", dtag_desc, dtag_off, dtag.dtag);
								if(dtag_desc == 0)
		            			{
									bm_pl[j].pl.dtag = dtag.dtag;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
									bm_pl[j].pl.nvm_cmd_id = 0;
								}
								else
								{
									bm_pl[j].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
									bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
									bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
								}
								misc_du_cnt++;
							}
							else
							{
								bm_pl[j].pl.dtag = (dtag_off + seg_idx * NR_L2PP_IN_SEG + j) | DTAG_IN_DDR_MASK;
								bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
								bm_pl[j].pl.nvm_cmd_id = ddr_dummy_meta_idx;
							}
							bm_pl[j].pl.du_ofst = j;// check??

#if (QBT_TLC_MODE == mENABLE)
							info[j].pb_type = NAL_PB_TYPE_XLC;
#else
							info[j].pb_type = NAL_PB_TYPE_SLC;
#endif
							info[j].status = 0;
						}
						if(misc_done == true)
						{
							ftl_apl_trace(LOG_ALW, 0x4c0a, "[L2P]MISC done cnt %d", misc_du_cnt);
							l2p_seg_read(l2p_cmd, misc_du_cnt, seg_idx);
						}
						else
						{
							l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_idx);
						}
						if(misc_start == true)
						{
							misc_idx++;
						}
						ptr+=2;
						seg_idx++;
						break;
					case 3 :// 2pln defect
						ptr+=2;
						break;
					default :
						panic("pln case != 0~3\n");
				}
				//--------------------------------------------------------------------

			}while(ptr<(shr_nand_info.lun_num*geo_pln));
			//L2P_LAST_PDA = pda;
			//ftl_apl_trace(LOG_ALW, 0, "L2P_LAST_PDA 0x%x", L2P_LAST_PDA);
		} // for(wl=0; wl<384; wl++)
		L2P_LAST_PDA = pda;
		ftl_apl_trace(LOG_ALW, 0xbcbe, "L2P_LAST_PDA 0x%x", L2P_LAST_PDA);
	}
//#endif

    // wait cmd done & release vac sram dtag
	wait_cmd_idle();
	ftl_l2p_put_vcnt_buf((dtag_t)misc_dtag_start[0], misc_dtag_cnt[0], true);
}
#endif
#endif

/*
#if (SPOR_FLOW == mENABLE)
fast_code void ftl_misc_reload(pda_t pda_start)
#else
fast_code void ftl_misc_reload(void)
#endif
{
	dtag_t dtag;
	dtag_t vcnt_start;
	u32 vcnt_dtag_cnt;
	u32 left_du_cnt[2] = {0, shr_blklistbuffer_need};
	//log_level_t old;
	pda_t *pda_list;
	bm_pl_t *bm_pl;
	l2p_ncl_cmd_t *l2p_cmd;
	struct info_param_t *info;
	u32 seg_cnt = ftl_l2p_misc_cnt();
	u32 seg_start = ftl_int_ns_get_misc_pos();
	spb_id_t  qbt_idx = 1;
	u32 spb_size_in_du = get_du_cnt_in_native_spb();
	u32 seg_cnt_per_tlc_spb = spb_size_in_du/NR_L2PP_IN_SEG;
#if (SPOR_FLOW == mDISABLE)
	u8  qbt_grp;
#endif
    u8  dtag_cnt;
	u8  dtag_desc = 0;
	u8  misc_idx = 0;
	u32 du_ofst;

#ifdef SKIP_MODE
	u8 pln_idx, ofst;
	u16 page;
	u32 interleave_mask = ((1 << 10)-1);
	u32 page_mask = ((1 << 21)-1);
#if (SPOR_FLOW == mENABLE)
    u32 spb_df_ptr_idx = ((pda_start & interleave_mask) >> 2) + 1;
	u16 l2p_last_pda_page = ((pda_start & page_mask) >> 10);
    qbt_idx = pda2blk(pda_start);
#else
	u32 spb_df_ptr_idx = ((L2P_LAST_PDA & interleave_mask) >> 2) + 1;
	u16 l2p_last_pda_page = ((L2P_LAST_PDA & page_mask) >> 10);
#endif
	pda_t pda = 0;
#endif
	//check if vc tbl never flushed
#ifdef SKIP_MODE
	ftl_apl_trace(LOG_ALW, 0, "[IN] misc reload skip mode");
#if(SPOR_TIME_COST == mENABLE)
    u64 time_start = get_tsc_64();
#endif

	ftl_apl_trace(LOG_ERR, 0, "interleave_mask 0x%x, page_mask 0x%x", interleave_mask, page_mask);
	ftl_apl_trace(LOG_ERR, 0, "spb_df_ptr_idx [%d] l2p_last_pda_page [%d]", spb_df_ptr_idx, l2p_last_pda_page);
	if(spb_df_ptr_idx == get_interleave())
	{
		ftl_apl_trace(LOG_ERR, 0, "spb_df_ptr_idx %d = interleave",spb_df_ptr_idx);
		spb_df_ptr_idx = 0;
		l2p_last_pda_page +=1;
		ftl_apl_trace(LOG_ERR, 0, "real pda page [%d]",l2p_last_pda_page);
	}
	if((spb_df_ptr_idx%2)!=0)
	{
		spb_df_ptr_idx += 1;
		ftl_apl_trace(LOG_ERR, 0, "interleave not pl 0 ptr idx [%d]",spb_df_ptr_idx);
	}
	if((l2p_last_pda_page%3)!=0)
	{
		l2p_last_pda_page -=2;
		ftl_apl_trace(LOG_ERR, 0, "real pda page [%d]",l2p_last_pda_page);
	}
#else
	ftl_apl_trace(LOG_ALW, 0, "[IN] misc reload remap mode");
#endif

//check misc in which qbt
///////////////////////////////////////////////////////
#if (SPOR_FLOW == mDISABLE)
	for(qbt_grp=0; qbt_grp<2; qbt_grp++)
	{
    	if(QBT_BLK_IDX == QBT_GRP_HANDLE[qbt_grp].head_qbt_idx)
    	{
			qbt_idx = QBT_GRP_HANDLE[qbt_grp].tail_qbt_idx;
			ftl_apl_trace(LOG_ERR, 0, "misc in qbt_idx %d \n", qbt_idx);
			break;
		}
	}
#endif
/////////////////////////////////////////////////////

//change seg_start idx for 8T
////////////////////////////////////////////////////
    if(seg_start > seg_cnt_per_tlc_spb)
    {
	  ftl_apl_trace(LOG_ERR, 0, "original_seg_start %d \n", seg_start);
	  seg_start = seg_start-seg_cnt_per_tlc_spb;
	  ftl_apl_trace(LOG_ERR, 0, "seg_start %d \n", seg_start);
	}
////////////////////////////////////////////////////


#ifdef SKIP_MODE
	u8* spb_df_ptr = ftl_get_spb_defect(qbt_idx);
#endif
	//old = (log_level_t)ipc_api_log_level_chg(LOG_INFO);
	dtag = ftl_l2p_get_vcnt_buf(&vcnt_dtag_cnt, NULL);
	left_du_cnt[0] = vcnt_dtag_cnt;
	vcnt_start = dtag;

//load misc data
////////////////////////////////////////////////////
#ifdef SKIP_MODE
	do{
		u8 pln_pair = ftl_get_defect_pl_pair(spb_df_ptr,spb_df_ptr_idx);

		if((misc_idx*NR_L2PP_IN_SEG) >= _l2p_tbl.vcnt_in_seg)
		{
			dtag.dtag = shr_blklistbuffer_start[0] + (misc_idx-_l2p_tbl.vcnt_in_seg)*NR_L2PP_IN_SEG;
			dtag_desc = 1;
			ftl_apl_trace(LOG_ERR, 0, "blklist dtag 0x%x \n", dtag.dtag);
		}
		dtag_cnt = min(left_du_cnt[dtag_desc], NR_L2PP_IN_SEG);

		if(misc_idx >= seg_cnt)
		{
			break;
		}

		switch (pln_pair)
		{
			case 0 :
				for (pln_idx=0; pln_idx<shr_nand_info.geo.nr_planes; pln_idx++)
				{
					if(misc_idx >= seg_cnt)
					{
						break;
					}

					l2p_cmd = get_l2p_ncl_cmd();
					bm_pl = l2p_cmd->bm_pl;
					pda_list = l2p_cmd->pda;
					info = l2p_cmd->info;
					u32 temp_ptr = 0;

					if((misc_idx*NR_L2PP_IN_SEG) >= _l2p_tbl.vcnt_in_seg)
					{
						dtag.dtag = shr_blklistbuffer_start[0] + (misc_idx-_l2p_tbl.vcnt_in_seg)*NR_L2PP_IN_SEG;
						dtag_desc = 1;
						ftl_apl_trace(LOG_ERR, 0, "blklist dtag 0x%x \n", dtag.dtag);
					}
					dtag_cnt = min(left_du_cnt[dtag_desc], NR_L2PP_IN_SEG);

					for (du_ofst=0; du_ofst<dtag_cnt; du_ofst++)
					{
						if(pln_idx == 0)// pln_0
						{
							page = l2p_last_pda_page + (du_ofst/8);
							if((du_ofst>3)&&(du_ofst<8))
							{
								temp_ptr = spb_df_ptr_idx + 1;
							}
							else
							{
								temp_ptr = spb_df_ptr_idx;
							}
						}
						else// pln_1
						{
							page = (du_ofst<4)? (l2p_last_pda_page + pln_idx) : (l2p_last_pda_page + pln_idx + 1);
							if((du_ofst<4)||(du_ofst>7))
							{
								temp_ptr = spb_df_ptr_idx + 1;
							}
							else
							{
								temp_ptr = spb_df_ptr_idx;
							}
						}
						ofst = (du_ofst%4);
						pda = (qbt_idx << 21) + (page << 10) + (temp_ptr << DU_CNT_SHIFT) + ofst;
						pda_list[du_ofst] = pda;
						ftl_apl_trace(LOG_ERR, 0, "misc pda 0x%x\n", pda);
						if(dtag_desc == 0)
            			{
							bm_pl[du_ofst].pl.dtag = dtag.dtag;
							bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
							bm_pl[du_ofst].pl.nvm_cmd_id = 0;
						}
						else
						{
							bm_pl[du_ofst].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
							bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
							bm_pl[du_ofst].pl.nvm_cmd_id = ddr_dummy_meta_idx;
							//ftl_apl_trace(LOG_ERR, 0, "pl.dtag 0x%x \n", bm_pl[du_ofst].pl.dtag);
						}
						bm_pl[du_ofst].pl.du_ofst = du_ofst;

						#if (QBT_TLC_MODE == mENABLE)
							info[du_ofst].pb_type = NAL_PB_TYPE_XLC;
						#else
							info[du_ofst].pb_type = NAL_PB_TYPE_SLC;
						#endif
							info[du_ofst].status = 0;
						dtag.dtag++;
					}
				misc_idx++;
				l2p_cmd->flags.all = 0;
				l2p_cmd->flags.b.misc_data = true;
				l2p_seg_read(l2p_cmd, dtag_cnt, misc_idx + seg_start);
				left_du_cnt[dtag_desc] -= dtag_cnt;
				}
				spb_df_ptr_idx+=2;
				break;
			case 1 :
				l2p_cmd = get_l2p_ncl_cmd();
				bm_pl = l2p_cmd->bm_pl;
				pda_list = l2p_cmd->pda;
				info = l2p_cmd->info;
				spb_df_ptr_idx++;

				for (du_ofst=0; du_ofst<dtag_cnt; du_ofst++)
				{
					page = l2p_last_pda_page + (du_ofst/4);
					ofst = (du_ofst%4);
					pda = (qbt_idx << 21) + (page << 10) + (spb_df_ptr_idx << DU_CNT_SHIFT) + ofst;
					pda_list[du_ofst] = pda;
					if(dtag_desc == 0)
        			{
						bm_pl[du_ofst].pl.dtag = dtag.dtag;
						bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
						bm_pl[du_ofst].pl.nvm_cmd_id = 0;
					}
					else
					{
						bm_pl[du_ofst].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
						bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
						bm_pl[du_ofst].pl.nvm_cmd_id = ddr_dummy_meta_idx;
						//ftl_apl_trace(LOG_ERR, 0, "pl.dtag 0x%x \n", bm_pl[du_ofst].pl.dtag);
					}
					bm_pl[du_ofst].pl.du_ofst = du_ofst;

					#if (QBT_TLC_MODE == mENABLE)
						info[du_ofst].pb_type = NAL_PB_TYPE_XLC;
					#else
						info[du_ofst].pb_type = NAL_PB_TYPE_SLC;
					#endif
						info[du_ofst].status = 0;
					dtag.dtag++;
				}
				spb_df_ptr_idx++;
				misc_idx++;
				l2p_cmd->flags.all = 0;
				l2p_cmd->flags.b.misc_data = true;
				l2p_seg_read(l2p_cmd, dtag_cnt, misc_idx + seg_start);
				left_du_cnt[dtag_desc] -= dtag_cnt;
				break;
			case 2 :
				l2p_cmd = get_l2p_ncl_cmd();
				bm_pl = l2p_cmd->bm_pl;
				pda_list = l2p_cmd->pda;
				info = l2p_cmd->info;

				for (du_ofst=0; du_ofst<dtag_cnt; du_ofst++)
				{
					page = l2p_last_pda_page + (du_ofst/4);
					ofst = (du_ofst%4);
					pda = (qbt_idx << 21) + (page << 10) + (spb_df_ptr_idx << DU_CNT_SHIFT) + ofst;
					pda_list[du_ofst] = pda;
					if(dtag_desc == 0)
        			{
						bm_pl[du_ofst].pl.dtag = dtag.dtag;
						bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
						bm_pl[du_ofst].pl.nvm_cmd_id = 0;
					}
					else
					{
						bm_pl[du_ofst].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
						bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
						bm_pl[du_ofst].pl.nvm_cmd_id = ddr_dummy_meta_idx;
						//ftl_apl_trace(LOG_ERR, 0, "pl.dtag 0x%x \n", bm_pl[du_ofst].pl.dtag);
					}
					bm_pl[du_ofst].pl.du_ofst = du_ofst;

					#if (QBT_TLC_MODE == mENABLE)
						info[du_ofst].pb_type = NAL_PB_TYPE_XLC;
					#else
						info[du_ofst].pb_type = NAL_PB_TYPE_SLC;
					#endif
						info[du_ofst].status = 0;
					dtag.dtag++;
				}
				spb_df_ptr_idx+=2;
				misc_idx++;
				l2p_cmd->flags.all = 0;
				l2p_cmd->flags.b.misc_data = true;
				l2p_seg_read(l2p_cmd, dtag_cnt, misc_idx + seg_start);
				left_du_cnt[dtag_desc] -= dtag_cnt;
				break;
			case 3 :// 2pln defect
				spb_df_ptr_idx+=2;
				break;
			default :
				panic("pln case != 0~3\n");
		}
#if (SPOR_FLOW == mENABLE)
    pda_start = pda;
	ftl_apl_trace(LOG_ALW, 0, "MISC_LAST_PDA 0x%x", pda_start);
#else
	L2P_LAST_PDA = pda;
	ftl_apl_trace(LOG_ALW, 0, "MISC_LAST_PDA 0x%x", L2P_LAST_PDA);
#endif
	}while(misc_idx < seg_cnt);
#else
//remap mode
	for (misc_idx = 0; misc_idx < seg_cnt; misc_idx++) {
		u8 dtag_cnt;
		u8 dtag_desc = 0;
		u8 page_in_wl_loop;

		if((misc_idx*NR_L2PP_IN_SEG) >= _l2p_tbl.vcnt_in_seg)
		{
			dtag.dtag = shr_blklistbuffer_start + (misc_idx-_l2p_tbl.vcnt_in_seg)*NR_L2PP_IN_SEG;
			dtag_desc = 1;
			ftl_apl_trace(LOG_ERR, 0, "blklist dtag 0x%x \n", dtag.dtag);
		}
		l2p_cmd = get_l2p_ncl_cmd();
		bm_pl = l2p_cmd->bm_pl;
		pda_list = l2p_cmd->pda;
		info = l2p_cmd->info;

		dtag_cnt = min(left_du_cnt[dtag_desc], NR_L2PP_IN_SEG);

		for (du_ofst = 0; du_ofst < dtag_cnt; du_ofst++) {
			page_in_wl_loop = (du_ofst/8);
		#if(QBT_TLC_MODE == mENABLE)
			pda_t pda = ftl_QBT_seg_lookup(misc_idx + seg_start, qbt_idx, page_in_wl_loop);
		#else
			pda_t pda = ftl_int_ns_lookup(misc_idx + seg_start);
		#endif
			pda_list[du_ofst] = pda + (du_ofst%8);
            if(dtag_desc == 0)
            {
				bm_pl[du_ofst].pl.dtag = dtag.dtag;
				bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
				bm_pl[du_ofst].pl.nvm_cmd_id = 0;
			}
			else
			{
				bm_pl[du_ofst].pl.dtag = dtag.dtag | DTAG_IN_DDR_MASK;
				bm_pl[du_ofst].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
				bm_pl[du_ofst].pl.nvm_cmd_id = ddr_dummy_meta_idx;
				ftl_apl_trace(LOG_ERR, 0, "pl.dtag 0x%x \n", bm_pl[du_ofst].pl.dtag);
			}
			bm_pl[du_ofst].pl.du_ofst = du_ofst;
			//bm_pl[j].pl.nvm_cmd_id = 0;

			#if (QBT_TLC_MODE == mENABLE)
				info[du_ofst].pb_type = NAL_PB_TYPE_XLC;
			#else
				info[du_ofst].pb_type = NAL_PB_TYPE_SLC;
			#endif
				info[du_ofst].status = 0;
			dtag.dtag++;
		}

		l2p_cmd->flags.all = 0;
		l2p_cmd->flags.b.misc_data = true;
		l2p_seg_read(l2p_cmd, dtag_cnt, misc_idx + seg_start);
		left_du_cnt[dtag_desc] -= dtag_cnt;
	}
#endif
////////////////////////////////////////////////////

//check load all misc data
////////////////////////////////////////////////////
	sys_assert(left_du_cnt[0] == 0);
	sys_assert(left_du_cnt[1] == 0);

	wait_cmd_idle();
	//ipc_api_log_level_chg(old);
	ftl_l2p_put_vcnt_buf(vcnt_start, vcnt_dtag_cnt, true);
#if(SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0, "Function time cost : %d us", time_elapsed_in_us(time_start));
#endif
////////////////////////////////////////////////////
}
*/

fast_code void ftl_l2p_flush(ftl_flush_tbl_t *flush_tbl)
{
	ftl_ns_t *ftl_ns = ftl_ns_get(flush_tbl->nsid);
	ftl_ns->flags.b.flushing = 1;

	if (shr_ftl_flags.b.flush_tbl_all) {
		flush_tbl->gc_spb = INV_SPB_ID;
		flush_tbl->flags.b.flush_all = true;
		shr_ftl_flags.b.flush_tbl_all = false;
	}

	ftl_core_flush_tbl(flush_tbl);
}

fast_code dtag_t ftl_l2p_get_vcnt_buf(u32 *cnt, void **buf)
{
	u32 dtag_cnt = occupied_by(get_total_spb_cnt() * sizeof(u32), DTAG_SZE);
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);

	sys_assert(dtag.dtag != _inv_dtag.dtag);

	l2p_mgr_vcnt_move(0, dtag2mem(dtag), dtag_cnt * DTAG_SZE);
	if (cnt)
		*cnt = dtag_cnt;
	if (buf)
		*buf = dtag2mem(dtag);

	return dtag;
}

fast_code void ftl_l2p_put_vcnt_buf(dtag_t dtag, u32 cnt, bool restore)
{
	u32 dtag_cnt = occupied_by(get_total_spb_cnt() * sizeof(u32), DTAG_SZE);
	sys_assert(dtag_cnt == cnt);

	if (restore)
		l2p_mgr_vcnt_move(1, dtag2mem(dtag), dtag_cnt * DTAG_SZE);

	dtag_cont_put(DTAG_T_SRAM, dtag, dtag_cnt);
}


fast_code bool ftl_l2p_misc_flush(ftl_flush_misc_t *flush_misc)
{
	u32 dtag_cnt;
	dtag_t dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, NULL);

	flush_misc->dtag_start[0] = dtag.dtag;
	flush_misc->dtag_cnt[0] = dtag_cnt;
	flush_misc->dtag_start[1] = shr_blklistbuffer_start[0];
	flush_misc->dtag_cnt[1] = shr_blklistbuffer_need;
	flush_misc->seg_cnt = ftl_l2p_misc_cnt();
    ftl_apl_trace(LOG_INFO, 0x2eb3, "seg_cnt %d [0]start 0x%x dcnt %d [1]start 0x%x dcnt %d",
		flush_misc->seg_cnt, dtag.dtag, dtag_cnt, shr_blklistbuffer_start[0], shr_blklistbuffer_need);
	ftl_core_flush_misc(flush_misc);
	//ftl_apl_trace(LOG_INFO, 0, "misc crc %x", ftl_l2p_misc_crc(&dtag));
	return false;
}

fast_code u32 ftl_l2p_misc_crc(dtag_t *dtag)
{
	u32 *ptr;
	u32 i;
	u32 cnt;
	u32 crc = 0x12345678;
	u32 crc2 = 0x12345678;
	dtag_t _dtag = { .dtag = DTAG_INV };
	u32 dtag_cnt = 0;

	if (dtag == NULL)
		_dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &ptr);
	else
		ptr = (u32 *) dtag2mem(*dtag);

	cnt = get_total_spb_cnt();
	for (i = 0; i < cnt; i++) {
		crc ^= ptr[i];
		if (spb_get_nsid(i) == FTL_NS_ID_INTERNAL)
			crc2 ^= 0;
		else
			crc2 ^= ptr[i];
	}
	ftl_apl_trace(LOG_ERR, 0x7188, "crc2 %x \n", crc2);

	if (dtag == NULL)
		ftl_l2p_put_vcnt_buf(_dtag, dtag_cnt, false);
	return crc;
}

slow_code u32 ftl_l2p_crc(void)
{
	u32 *ptr = (u32 *) (((u32) _l2p_tbl.base) + DDR_BASE);
	u32 dw_cnt = _l2p_tbl.alloc_off / sizeof(u32);
	u32 i;
	u32 crc = 0;

	for (i = 0; i < dw_cnt; i++)
		crc += ptr[i];

	return crc;
}

fast_code void ftl_l2p_tbl_flush_all_notify(void)
{
	// it will affect how to build flush bitmap
	if (shr_ftl_flags.b.flush_tbl_all == 0) {
		shr_ftl_flags.b.flush_tbl_all = 1;
		ftl_apl_trace(LOG_ALW, 0x44d5, "set flush all flag");
	}
}

UNUSED fast_code void prebuild_flush_bmp(spb_id_t spb_id)
{
#if (FTL_L2P_SEG_BMP == mENABLE)
	u32 i;
	u64 tick = get_tsc_64();

	memset(shr_l2p_seg_flush_bmp, 0, shr_l2p_seg_bmp_sz);
	for (i = 0; i < _l2p_tbl.seg_cnt; i++) {
		pda_t pda = shr_ins_lut[i];
		spb_id_t _spb_id = nal_pda_get_block_id(pda);

		if (spb_id == _spb_id)
			set_bit(i, shr_l2p_seg_flush_bmp);
	}

	ftl_apl_trace(LOG_ALW, 0x7b78, "%d us", time_elapsed_in_us(tick));
#endif
}

fast_code u32 ftl_l2p_scrub_all_virgin_seg(u32 seg_off, u32 seg_end)
{
	u32 i, j;
	u32 inv_seg_start;
	u32 inv_seg_ttl = 0;
	u32 inv_seg_cnt = 0;
	u64 inv_seg_base = 0;

	for (i = seg_off; i < seg_end; i++) {
		if (ftl_int_ns_lookup(i) == INV_PDA) {
			if (inv_seg_cnt == 0) {
				inv_seg_start = i;
				inv_seg_base = _l2p_tbl.base + (i * seg_size);
			}

			inv_seg_ttl++;
			inv_seg_cnt++;
		} else if (inv_seg_cnt) {
			scrub_l2p_seg(inv_seg_cnt, inv_seg_base);
			for (j = 0; j < inv_seg_cnt; j++) {
				int ret = test_and_set_bit(inv_seg_start + j, l2p_load_mgr.ready_bmp);
				sys_assert(ret == 0);
			}

			l2p_load_mgr.ttl_ready += inv_seg_cnt;
			inv_seg_cnt = 0;
		}
	}

	if (inv_seg_cnt) {
		scrub_l2p_seg(inv_seg_cnt, inv_seg_base);
		for (j = 0; j < inv_seg_cnt; j++) {
			int ret = test_and_set_bit(inv_seg_start + j, l2p_load_mgr.ready_bmp);
			sys_assert(ret == 0);
		}
	}

	l2p_load_mgr.ttl_ready += inv_seg_cnt;
	if (l2p_load_mgr.ttl_ready == shr_l2p_seg_cnt) {
		shr_ftl_flags.b.l2p_all_ready = true;
		ftl_apl_trace(LOG_INFO, 0x7b74, "all l2p ready");
	}

	return inv_seg_ttl;
}

fast_code void ftl_l2p_bg_load_handle(u32 r0, u32 r1, u32 r2)
{
	u32 i;
	u32 seg;
	pda_t pda;
	u32 dtag_start;
	l2p_ncl_cmd_t *l2p_cmd;
	l2p_load_mgr_t *load_mgr = &l2p_load_mgr;

	while (load_mgr->otf_load < MAX_L2P_LOAD_CMD) {
		seg = find_next_zero_bit(load_mgr->ready_bmp, load_mgr->seg_end, load_mgr->seg_idx);
		if (seg == load_mgr->seg_end)
			break;

		load_mgr->seg_idx = seg + 1;
		if (test_bit(seg, load_mgr->loading_bmp))
			continue;

		load_mgr->otf_load++;
		set_bit(seg, load_mgr->loading_bmp);

		pda = ftl_int_ns_lookup(seg);
		l2p_cmd = get_l2p_ncl_cmd();
		dtag_start = shr_l2p_entry_start + seg * NR_L2PP_IN_SEG;

		for (i = 0; i < NR_L2PP_IN_SEG; i++) {
			l2p_cmd->pda[i] = pda + i;
			l2p_cmd->bm_pl[i].pl.du_ofst = i;
			l2p_cmd->bm_pl[i].pl.nvm_cmd_id = ddr_dummy_meta_idx;
			l2p_cmd->bm_pl[i].pl.dtag = (dtag_start + i) | DTAG_IN_DDR_MASK;
			l2p_cmd->bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;

			l2p_cmd->info[i].status = 0;
			l2p_cmd->info[i].pb_type = NAL_PB_TYPE_SLC;
		}

		l2p_cmd->flags.all = 0;
		l2p_cmd->flags.b.bg_load = true;
		l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg);
	}
}

fast_code void ftl_l2p_resume(void)
{
	shr_ftl_flags.b.l2p_all_ready = false;

	l2p_load_mgr.seg_off = 0;
	l2p_load_mgr.seg_idx = 0;
	l2p_load_mgr.seg_end = shr_l2p_seg_cnt;
	l2p_load_mgr.seg_cnt = shr_l2p_seg_cnt;

	l2p_load_mgr.otf_load = 0;
	l2p_load_mgr.ttl_ready = 0;
	l2p_load_mgr.ready_cnt = 0;

	memset(l2p_load_mgr.ready_bmp, 0, shr_l2p_seg_bmp_sz);
	memset(l2p_load_mgr.loading_bmp, 0, shr_l2p_seg_bmp_sz);

	//first scrub all virgin l2p seg as this won't take long
	ftl_l2p_scrub_all_virgin_seg(0, shr_l2p_seg_cnt);

	//start to load l2p seg from nand
	if (l2p_load_mgr.ttl_ready < shr_l2p_seg_cnt)
		evt_set_cs(l2p_load_mgr.evt_load, 0, 0, CS_TASK);
}

fast_code void ftl_l2p_bg_load_exec(void)
{
	l2p_bg_load_req_t *bg_load;

again:
	bg_load = list_first_entry(&l2p_load_mgr.bg_waiting_list, l2p_bg_load_req_t, entry);
	list_del(&bg_load->entry);

	if (bg_load->ready_cnt < bg_load->seg_cnt) {
		l2p_load_mgr.cur_bg_req = bg_load;
		l2p_load_mgr.seg_off = bg_load->seg_off;
		l2p_load_mgr.seg_idx = bg_load->seg_off;
		l2p_load_mgr.seg_end = bg_load->seg_end;
		l2p_load_mgr.seg_cnt = bg_load->seg_cnt;
		l2p_load_mgr.ready_cnt = bg_load->ready_cnt;

		evt_set_cs(l2p_load_mgr.evt_load, 0, 0, CS_TASK);
	} else {
		sys_free(FAST_DATA, bg_load);
		if (!list_empty(&l2p_load_mgr.bg_waiting_list))
			goto again;
	}
}

fast_code void ftl_l2p_bg_load(u32 seg_off, u32 seg_end)
{
	u32 scrub_cnt;
	l2p_bg_load_req_t *bg_load;

	//scrub inv seg first as it won't take long
	scrub_cnt = ftl_l2p_scrub_all_virgin_seg(seg_off, seg_end);
	if (scrub_cnt < (seg_end - seg_off)) {
		bg_load = sys_malloc(FAST_DATA, sizeof(l2p_bg_load_req_t));
		sys_assert(bg_load);

		bg_load->seg_off = seg_off;
		bg_load->seg_end = seg_end;
		bg_load->seg_cnt = seg_end - seg_off;
		bg_load->ready_cnt = scrub_cnt;

		INIT_LIST_HEAD(&bg_load->entry);
		list_add_tail(&bg_load->entry, &l2p_load_mgr.bg_waiting_list);

		if (l2p_load_mgr.cur_bg_req == NULL)
			ftl_l2p_bg_load_exec();
	}
}

fast_code void ftl_l2p_urgent_load_exec(void)
{
	u32 i;
	u32 seg_id;
	u32 dtag_base;
	l2p_ncl_cmd_t *l2p_cmd;
	l2p_urg_load_req_t *urg_load;
	struct list_head *cur, *saved;

	list_for_each_safe(cur, saved, &l2p_load_mgr.urg_waiting_list) {
		urg_load = container_of(cur, l2p_urg_load_req_t, entry);
		//move to loading list
		list_del_init(&urg_load->entry);
		list_add_tail(&urg_load->entry, &l2p_load_mgr.urg_loading_list);

		seg_id = urg_load->seg;
		l2p_load_mgr.otf_load++;
		set_bit(seg_id, l2p_load_mgr.loading_bmp);

		pda_t pda = ftl_int_ns_lookup(seg_id);
		sys_assert(pda != INV_PDA);

		l2p_cmd = get_l2p_ncl_cmd();
		dtag_base = shr_l2p_entry_start + seg_id * NR_L2PP_IN_SEG;

		for (i = 0; i < NR_L2PP_IN_SEG; i++) {
			l2p_cmd->pda[i] = pda + i;
			l2p_cmd->bm_pl[i].pl.du_ofst = i;
			l2p_cmd->bm_pl[i].pl.nvm_cmd_id = ddr_dummy_meta_idx;
			l2p_cmd->bm_pl[i].pl.dtag = (dtag_base + i) | DTAG_IN_DDR_MASK;
			l2p_cmd->bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;

			l2p_cmd->info[i].status = 0;
			l2p_cmd->info[i].pb_type = NAL_PB_TYPE_SLC;
		}

		l2p_cmd->flags.all = 0;
		l2p_cmd->flags.b.urg_load = true;
		l2p_seg_read(l2p_cmd, NR_L2PP_IN_SEG, seg_id);

		if (l2p_load_mgr.otf_load == MAX_L2P_LOAD_CMD)
			break;
	}
}

fast_code void ftl_l2p_urgent_load(u32 seg_id, u32 tx)
{
	l2p_urg_load_req_t *urg_load;

	if (test_bit(seg_id, l2p_load_mgr.ready_bmp)) {
		cpu_msg_issue(tx, CPU_MSG_L2P_LOAD_DONE, 0, seg_id);
		return;
	}

	urg_load = sys_malloc(FAST_DATA, sizeof(l2p_urg_load_req_t));
	sys_assert(urg_load);

	urg_load->tx = tx;
	urg_load->seg = seg_id;
	INIT_LIST_HEAD(&urg_load->entry);

	if (test_bit(seg_id, l2p_load_mgr.loading_bmp)) {
		list_add_tail(&urg_load->entry, &l2p_load_mgr.urg_loading_list);
	} else {
		list_add_tail(&urg_load->entry, &l2p_load_mgr.urg_waiting_list);
		if (l2p_load_mgr.otf_load < MAX_L2P_LOAD_CMD)
			ftl_l2p_urgent_load_exec();
	}
}

#if CO_SUPPORT_DEVICE_SELF_TEST

slow_code pda_t DST_L2P_search(lda_t data)
{
    u64 addr;
    u8  win = 0;
    pda_t pda = INV_U32;
    lda_t lda;
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
    u8 old = ctrl0.b.cpu3_ddr_window_sel;

    lda  = data;
    addr = (_l2p_tbl.base | 0x40000000) + ((u64)lda * 4);

    while (addr >= 0xC0000000) {
    	addr -= 0x80000000;
     	win++;
    }

	if(old != win){
		ctrl0.b.cpu3_ddr_window_sel = win;
		writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
		__dmb();
		pda = *((u32*)((u32)addr));
		ctrl0.b.cpu3_ddr_window_sel = old;
		writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
		__dmb();
	}
	else{
		pda = *((u32*)(u32)addr);
	}

	ftl_apl_trace(LOG_ALW, 0x4fae, "lda 0x%x pda 0x%x\n", lda, pda);
	ftl_apl_trace(LOG_ALW, 0xbef0, "lda 0x%x pda 0x%x\n", lda, pda);//show twice avoid log info disorderly

	return pda;
}

#endif

slow_code_ex int uart_l2p(int argc, char *argv[])
{
    u8  mode = strtol(argv[1], (void *)0, 0);
    u32 data = strtol(argv[2], (void *)0, 0);
    u32 length = 0;
    if(argc == 4)
    {
		length = strtol(argv[3], (void *)0, 0);
    }
    u64 addr;
    u8  win = 0;
    pda_t pda = INV_U32;
    lda_t lda , lda_start , lda_end;
    u32 result = INV_U32;
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
    u8 old = ctrl0.b.cpu3_ddr_window_sel;
    switch(mode)
    {
        case 0:
            lda  = data;
            addr = (_l2p_tbl.base | 0x40000000) + ((u64)lda * 4);
        	while (addr >= 0xC0000000) {
        		addr -= 0x80000000;
        		win++;
        	}
            if(old != win){
            	ctrl0.b.cpu3_ddr_window_sel = win;
            	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                __dmb();
                pda = *((u32*)((u32)addr));
            	ctrl0.b.cpu3_ddr_window_sel = old;
            	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                __dmb();
            }
            else{
                pda = *((u32*)(u32)addr);
            }
        	ftl_apl_trace(LOG_ALW, 0xfdec, "lda 0x%x pda 0x%x\n", lda, pda);
           	ftl_apl_trace(LOG_ALW, 0xe2ce, "lda 0x%x pda 0x%x\n", lda, pda);//show twice avoid log info disorderly
            break;

        case 1:
            addr = (_l2p_tbl.base | 0x40000000);
            for(lda = 0; lda < _max_capacity; lda++)
            {
                if(addr >= 0xC0000000)
                {
                	addr -= 0x80000000;
                    win++;
                	ctrl0.b.cpu3_ddr_window_sel = win;
                    writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                    __dmb();
                }
                pda = *((u32*)(u32)addr);
                addr += sizeof(lda_t);
                if(data == pda)
                {
                    result = lda;
                    break;
                }
            }
            if(old!=win)
            {
            	ctrl0.b.cpu3_ddr_window_sel = old;
                writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                __dmb();
            }
            ftl_apl_trace(LOG_ALW, 0x49e5, "pda 0x%x lda 0x%x", data, result);
            ftl_apl_trace(LOG_ALW, 0xc928, "pda 0x%x lda 0x%x", data, result);//show twice avoid log info disorderly

            break;
		case 2:
            result = 0;
            addr = (_l2p_tbl.base | 0x40000000);
            for(lda = 0; lda < _max_capacity; lda++)
            {
                if(addr >= 0xC0000000)
                {
                	addr -= 0x80000000;
                    win++;
                	ctrl0.b.cpu3_ddr_window_sel = win;
                    writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                    __dmb();
                }
                pda = *((u32*)(u32)addr);
                addr += sizeof(lda_t);
                if(data == ((pda >> shr_nand_info.pda_block_shift) & shr_nand_info.pda_block_mask))
                {
                    result ++;
                    ftl_apl_trace(LOG_ALW, 0x29c1, "pda 0x%x lda 0x%x wl %d", pda, lda,
                        ((pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask)/shr_nand_info.bit_per_cell);
                }
            }
            if(old!=win)
            {
            	ctrl0.b.cpu3_ddr_window_sel = old;
                writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
                __dmb();
            }
            ftl_apl_trace(LOG_ALW, 0x9805, "spb %u result %u", data, result);

            break;
		case 3:
			lda_start = data;
			lda_end   = data + length;
			ftl_apl_trace(LOG_ALW, 0xed05, "lda_start 0x%x length:%d", lda_start, length);
			while(lda_start <= lda_end)
			{
				addr = (_l2p_tbl.base | 0x40000000) + ((u64)lda_start * 4);
				while (addr >= 0xC0000000) {
        		addr -= 0x80000000;
        		win++;
        		}
	            if(old != win){
	            	ctrl0.b.cpu3_ddr_window_sel = win;
	            	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
	                __dmb();
	                pda = *((u32*)((u32)addr));
	            	ctrl0.b.cpu3_ddr_window_sel = old;
	            	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
	                __dmb();
	            }
	            else{
	                pda = *((u32*)(u32)addr);
	            }
	        	ftl_apl_trace(LOG_ALW, 0x4427, "lda 0x%x pda 0x%x\n", lda_start, pda);
	           	ftl_apl_trace(LOG_ALW, 0x5121, "lda 0x%x pda 0x%x\n", lda_start, pda);//show twice avoid log info disorderly
				lda_start++;
			}
			break;

        default:
            panic("no such mode!\n");
            break;
    }

	return 0;
}

/*! @brief help command declaration */
static DEFINE_UART_CMD(l2p, "l2p",
		"l2p [MODE] [PDA/LDA]",
		"mode 0 : LDA to find PDA ; mode 1 : PDA to find LDA ; mode 2 : show blk l2p info",
		1, 3, uart_l2p);

/*! @} */
