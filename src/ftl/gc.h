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
/*! \file gc.h
 * @brief start of GC process, and calculate GC performance
 *
 * \addtogroup ftl
 * \defgroup gc
 * \ingroup ftl
 * @{
 *
 */
//=============================================================================
#pragma once

#include "ftl_ns.h"
extern volatile u32 shr_host_write_cnt;
extern volatile u32 shr_host_write_cnt_old;
extern u32 shr_host_perf;
extern u32 shr_gc_perf;
extern u32 min_vac;
share_data_zi volatile u8 shr_gc_fc_ctrl;
extern u8 shr_gc_read_disturb_ctrl;
//extern u8 otf_forcepbt;

#define GC_PERF_MA_SHF		3			///< GC performance moving acerage count shift
#define GC_PERF_MA_SZ		(1 << GC_PERF_MA_SHF)	///< GC performance moving average count
extern bool gc_suspend_stop_next_spb;
extern bool gc_suspend_start_wl;
extern bool gc_opblk_w_rd;
extern u16 global_capacity;

extern u32 wl_cnt;
extern u32 gc_cnt;

typedef struct _gc_t {
	spb_id_t spb_id;	///< current gc spb
	u32 gc_blk_valid_cnt;
	u32 nsid;		///< namespace of current gc spb

	u32 du_per_die;		///< native du count per die
	u32 die_index;		///< not used
	u32 last_bit_off;	///< not used

	u32 free_du_cnt;	///< not used
	u64 start_time;		///< start time of GC

	u32 curr_gc_perf;	///< free du per 100 ms
	u32 avg_gc_perf;	///< average free du per 100 ms

	u32 gc_perf_buf[GC_PERF_MA_SZ];	///< gc performance moving average buffer

	bool full;		///< if moving average buffer was full
	u16 ma_idx;		///< index of moving average buffer
	stGC_FSM_INFO_MODE  gc_mode;        // 8 B
	gc_req_t req;		///< gc req submit to ftl core
} gc_t;


#define GC_BLKCNT_READONLY           4  //gc1 + pbt1 + buff1
#define GC_BLKCNT_DISKFULL           (GC_BLKCNT_READONLY + 2)  // 6  // host1 +gc1 +pbt1+buff1
#define GC_BLKCNT_EMER               (GC_BLKCNT_DISKFULL + 3)  // 9
#define GC_BLKCNT_EMER_SPD_CTL       (GC_BLKCNT_EMER - 2)  // 7
#define GC_BLKCNT_ACTIVE             12 //15   //20//(OP_BLK_CNT*10/13)     //star GC when free block less than OP_BLK_CNT*10/13
#define GC_BLKCNT_TH                 3//0
#define GC_BLKCNT_SPEED_UP_TH        0//0                                  
#define GC_BLKCNT_SPEED_UP           (GC_BLKCNT_ACTIVE   + GC_BLKCNT_SPEED_UP_TH)    //speed up host when ready to leave gc
#define GC_BLKCNT_DEACTIVE           (GC_BLKCNT_ACTIVE   + GC_BLKCNT_TH)    //stop GC when free block larger than op/2+TH


/************GC MODE Information*****************/
#define GC_FORCE                      (0x00000001)  // B_0
#define GC_EMGR            		      (0x00000002)  // B_1
#define GC_LOCK                       (0x00000004)  // B_2
#define GC_SHUTTLE                    (0x00000008)  // B_3
#define GC_BOOT              		  (0x00000010)  // B_4
#define GC_IDLE              		  (0x00000020)  // B_5
#define GC_STATIC_WL         		  (0x00000040)  // B_6
#define GC_Hostdown              	  (0x00000080)  // B_7
#define GC_DBG                        (0X00000100)  // B_8
#define GC_READDISTURB                (0X00000200)  // B_9
#define GC_DATARETENTION              (0X00000400)  // B_10
#define GC_PLP_SLC		              (0X00000800)  // B_11
#define GC_NON_PLP		              (0X00001000)  // B_12 



#define GC_MODE_CHECK                 (gGCInfo.mode.all16)
#define GC_MODE_SETTING(MODE)         (gGCInfo.mode.all16 |=  (MODE))
#define GC_MODE_CLEAN(MODE)           (gGCInfo.mode.all16 &= ~(MODE))
//#define GC_MODE_CHECK_CRITICAL        (gGCInfo.mode.all16 & (GC_EMGR|GC_LOCK))
#define GC_MODE_CHECK_CRITICAL        (gGCInfo.mode.all16 & (GC_LOCK))
#define GC_MODE_CHECK_STATIC_WL       (gGCInfo.mode.all16 & (GC_STATIC_WL))
#define GC_MODE_CHECK_LOCK            (gGCInfo.mode.all16 & GC_LOCK)
#define GC_MODE_CHECK_SHUTTLE         (gGCInfo.mode.all16 & GC_SHUTTLE)
//#define GC_MODE_CHECK_CRITICAL_PLUS   (gGCInfo.mode.all16 & (GC_EMGR|GC_LOCK|GC_SHUTTLE))
#define GC_MODE_CHECK_CRITICAL_PLUS   (gGCInfo.mode.all16 & (GC_LOCK|GC_SHUTTLE))
#define GC_MODE_CHECK_RD              (gGCInfo.mode.all16 & GC_READDISTURB)
#define GC_MODE_CHECK_DR              (gGCInfo.mode.all16 & GC_DATARETENTION)
#define GC_MODE_CHECK_STOP_BG_GC	  (gGCInfo.mode.all16 & (GC_READDISTURB|GC_DATARETENTION|GC_SHUTTLE|GC_PLP_SLC|GC_NON_PLP))
#define GC_MODE_CHECK_NON_PLP         (gGCInfo.mode.all16 & GC_NON_PLP)


/************GC state Information*****************/
#define GC_STATE_IDLE        		  (0x00000001)      //when GC SSD is not enough make GC IDLE
#define GC_STATE_HALT        		  (0x00000002)      //stop GC
#define GC_STATE_RESET       		  (0x00000004)
#define GC_STATE_STOPATPOR  		  (0x00000008)      //POR
#define GC_STATE_BYPASS               (0x00000010)      // bypass mode
#define GC_STATE_READONLY  			  (0x00000020)      //only read
#define GC_STATE_HOSTIDLE             (0x00000040)
#define GC_STATE_MASK_STOP            (GC_STATE_IDLE|GC_STATE_HALT|GC_STATE_READONLY)   //all will make GC STOP
#define GC_STATE_MASK      			  (GC_STATE_IDLE|GC_STATE_HALT|GC_STATE_RESET|GC_STATE_STOPATPOR)  // CAN NOT RESET READONLY FLAG
#define GC_STATE_MASK_RESET           (GC_STATE_IDLE|GC_STATE_HALT|GC_STATE_RESET|GC_STATE_STOPATPOR|GC_STATE_BYPASS)  // CAN NOT RESET READONLY FLAG
#define GC_STATE_STOP_MASK  		  (GC_STATE_HALT|GC_STATE_READONLY)

#define GC_STATE_SETTING(STATE)       (gGCInfo.state.all16 |= ((gGCInfo.state.b.readOnly) ? (STATE | GC_STATE_READONLY) : STATE))
#define GC_STATE_CLEAN(STATE)         (gGCInfo.state.all16 &= ~STATE)
#define GC_STATE_CHECK(STATE)         (gGCInfo.state.all16 & STATE)
#define GC_STATE_RESET_ALL()          (gGCInfo.state.all16 &= ~ (GC_STATE_MASK_RESET))
#define GC_STATE_CHECK_ACTIVE         (gGCInfo.state.all16 & (GC_STATE_MASK_STOP))

#define ENABLE_WL					  (1)
#define GC_AVG_DEFECT_CNT			  (31)

static inline u32 gc_slow_down_per_blk(u32 total_du, u32 freeblkcnt)
{
	switch(global_capacity)
	{
		case 2048: //2T
			return (total_du * (GC_BLKCNT_ACTIVE - freeblkcnt) >> 10);  // *0.1%
			break;
		case 1024: //1T
			return (total_du * (GC_BLKCNT_ACTIVE - freeblkcnt) >> 9);  // *0.2%
			break;
		case 512: //512G
			return (total_du * (GC_BLKCNT_ACTIVE - freeblkcnt) * 3 >> 10);  // *0.293%
			break;
		default:
			return (total_du * (GC_BLKCNT_ACTIVE - freeblkcnt) >> 10);  // *0.1% use 2^10 replace 1000
			break;
	}
}

/*!
 * @brief initialization of gc module
 */
void gc_init(void);

/*!
 * @brief check if GC doing
 *
 * @return	return true if GCing
 */
bool gc_busy(void);

/*!
 * @brief start GC for spb id
 *
 * @param spb_id	gc candidate
 * @param nsid		namespace of candidate
 * @param done		callback function when gc end
 *
 * @return		return true if issued
 */
bool gc_start(spb_id_t spb_id, u32 nsid, u32 vac, completion_t done);

/*!
 * @brief end of GC
 *
 * @param spb_id	GCed spb id
 * @param free_du_cnt	total free du count
 *
 * @return		not used
 */
void gc_end(spb_id_t spb_id, u32 free_du_cnt);

/*!
 * @brief gc action
 *
 * @param spb_id	GCed spb id
 * @param free_du_cnt	total free du count
 *
 * @return		return true if gc action done
 */
bool gc_action(gc_action_t *action);

/*!
 * @brief resume gc if gc suspended
 *
 * @return		not used
 */
void gc_resume(void);

/*!
 * @brief api to get average GC performance and current GC performance
 *
 * @param avg		average GC performance
 * @param cur		latest GC performance
 *
 * @return		return false if no GC performed yet
 */
bool get_gc_perf(u32 *avg, u32 *cur);

/*!
 * @brief api to get current GC candidate
 *
 * @return		current GC spb
 */
void Set_gc_shuttle_mode(void);
void Set_gc_RdDisturb_mode(void);
void Set_gc_shuttleCnt(void);
void Set_gc_patorlrdCnt(bool para);
void Set_gc_rdDisturbCnt(bool para);
u16 Get_gc_rdDisturbCnt(void);
spb_id_t GC_FSM_Blk_Select(u32 nsid, u32 pool_id);
void gc_read_only_handle(void *data);
void lock_power_on_cancle(void *data);
void lock_power_on_gc_ready(void *data);
void gc_end_vac0_free(u32 parm, u32 payload, u32 sts);
void GC_Mode_Assert(void);
void GC_Mode_DeAssert(void);
spb_id_t get_gc_blk(void);
stGC_MODE gc_get_mode(void);
void set_open_gc_blk_rd(bool flag);
bool get_open_gc_blk_rd(void);
void gc_clear_mode(spb_id_t spb_id);

/*! @} */
