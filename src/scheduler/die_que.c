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

/*! \file die_que.c
 * @brief scheduler module, ncl die command scheduler
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

#include "types.h"
#include "sect.h"
#include "bitops.h"
#include "stdlib.h"
#include "string.h"
#include "event.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "pool.h"
#include "scheduler.h"
#include "die_que.h"
#include "dtag.h"
#include "rdisk.h"
#include "addr_trans.h"
#include "fc_export.h"
#include "idx_meta.h"
#include "mpc.h"
#include "ftl_meta.h"
#include "ftl_export.h"
#include "ipc_api.h"
#include "ftl_remap.h"
#include "spin_lock.h"
#include "read_error.h"
#include "btn_export.h"
#include "ftl_flash_geo.h"
#include "console.h"
#include "ncl_err.h"
#include "ficu.h"
#include "GList.h"
#include "gc.h"
#include "erase.h"	// ISU, SPBErFalHdl
#include "../ftl/ErrorHandle.h"
#include "ns.h"

#define __FILEID__ dieq
#include "trace.h"


#define DIE_OP_READ	0		///< read operation index
#define DIE_OP_WRITE	1		///< write operation index
#define DIE_IO_ERASE	2		///< erase operation index

#if defined(USE_MU_NAND)
#define MAX_RD_SZ	16		///< max du number per read command
#else
#if OPEN_ROW == 1
#define MAX_RD_SZ	4
#else
#define MAX_RD_SZ	(shr_nand_info.geo.nr_planes * DU_CNT_PER_PAGE)  //2P : 8, 4P : 16
#endif
#endif

#define MAX_NCL_CMD_SZ		MAX_RD_SZ	///< max du number per die ncl command

#ifdef CO_SUPPORT_READ_REORDER
#define MAX_RD_SZ_PER_DIE 16
#endif

#ifdef DUAL_BE
#define NCL_GRP_CNT		2	///< number of ncl group
#else
#define NCL_GRP_CNT		1	///< number of ncl group
#endif

#define SCH_READ_POOL_TOTAL_RATIO 5 //HOST/GC/OTHER = 2/2/1


typedef QSIMPLEQ_HEAD(wr_era, _ncl_req_t) wr_era_q_t;	///< write/erase request link
typedef QSIMPLEQ_HEAD(other_wr_era_ncl, ncl_cmd_t) other_ncl_wr_era;
typedef QSIMPLEQ_HEAD(other_rd_ncl, ncl_cmd_t) other_ncl_rd;

typedef struct _die_mgr_t {
	die_que_t die_que[DIE_Q_MAX];			///< die queue

	u16 otf_cmd_bitmap[1];				///< on-the-fly command bit map
    u16 read_pool_ratio_cnt;
	struct ncl_cmd_t *ncl_cmd;			///< ncl command
	struct info_param_t *info;			///< info for ncl command
	wr_era_q_t wr_era;				///< write erase link

	other_ncl_rd other_rd_ncl;
    other_ncl_wr_era other_wr_era_ncl;

    u16 wr_busy_cnt;				///< number of write command count
    u8 other_rd_ncl_num;
    u8 other_wr_era_ncl_num;

    u8 total_wr_era_cnt;
    u8 total_rd_cnt;
	u8 otf_cmd_cnt;					///< number of on-the-fly command count
	u8 wr_cmd_otf_cnt;				///< number of on-the-fly write command count

	u32 wr_issue_ttl;				///< total write/erase command issued in thermal throttle round
	u32 rd_issue_ttl;				///< total read command issued in thermal throttle round

	die_cmd_info_t *die_cmd_info;			///< command info
} die_mgr_t;

enum case_select{
    CS_RD_HOST = 0,
    CS_RD_GC,
    CS_RD_OTHER,
	CS_WR_ERASE,
    CS_WR_ERASE_OTHER,
    CS_RD_PLP_DONE,
	CS_MAX,
};

slow_data_zi rc_dtag_t* rc_dtag;

share_data_zi u32 _max_capacity;	// For ECCT check LDA, DBG, LJ1-252, PgFalCmdTimeout
share_data_zi spb_rt_flags_t *spb_rt_flags;		///< runtime flags of SPB
fast_data_zi u32 pad_meta_idx[PDA_META_IN_GRP];	///< meta for pad
fast_data_zi u32 pad_meta_bmp;			///< meta bmp for pad
fast_data_zi die_mgr_t _die_mgr[MAX_DIE_MGR_CNT];	///< die manager
fast_data_zi u32 _die_mgr_busy[occupied_by(MAX_DIE_MGR_CNT, 32)] = {0};	///< die busy flag
fast_data u8 die_isu_evt = 0xff;			///< die queue issue event id
slow_data_zi rsv_die_que_t rsv_die_que = { .wptr = 0, .rptr = 0, .busy_cnt = 0 };
#ifdef CO_SUPPORT_READ_REORDER
fast_data_zi active_plane_buffer_t gPlaneBuffer[MAX_RD_SZ_PER_DIE];
#endif
fast_data_zi u8 gDieQueDieIdTable[8][8][2];  // [ch][ce][lun]

fast_data_zi u16 otf_e_reqs = 0;			///< number of on-the-fly erase request
fast_data_ni pool_t ncl_w_reqs_pool;			///< ncl request pool
fast_data_ni u32 ncl_w_reqs_free_cnt;			///< numbers of ncl free request
fast_data_zi u8 ncl_cmd_per_die;				///< number of ncl command per die
fast_data_zi u8 max_wcmd_per_die;				///< number of max ncl write command per die
fast_data_zi u32 die_que_size;
fast_data_ni struct timer_list die_que_sched_timer;	///< die queue thermal throttle timer
fast_data_ni u32 rd_cnt_threshold_close = 0;
fast_data_ni u32 rd_cnt_threshold_open = 0;
share_data_zi volatile read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];

#if (CPU_BE == CPU_ID)
slow_data_ni ncl_e_req_t ncl_e_reqs[RDISK_NCL_E_REQ_CNT];	///< erase ncl request
slow_data_ni pool_t ncl_e_reqs_pool;				///< erase ncl request pool
slow_data_ni u32 ncl_e_reqs_free_cnt;				///< erase ncl free counter
#endif

fast_data_zi static u32 _rd_cnt_sts_upd_cnt = 0;		///< how many read counters were increased
//fast_data_zi read_cnt_sts_t *_rd_cnt_sts;		///< each scheduler should have one rd count status
//share_data_zi read_cnt_sts_t shr_rd_cnt_sts[1958];
share_data volatile enum du_fmt_t host_du_fmt;		///< host du format, init in FTL
share_data_ni volatile lda_t wunc_ua[2];
share_data_ni volatile u8 wunc_bmp[2];
share_data volatile u32 ts_rd_credit;
share_data volatile u32 ts_wr_credit;
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)
share_data_zi volatile bool delay_flush_spor_qbt;
#endif

//joe add sec size 20200817
extern u8 host_sec_bitz;
extern u16 host_sec_size;
//joe add sec size 20200817

extern struct nand_info_t shr_nand_info;	/// nand infomation
share_data_zi u16 ua_btag;

extern volatile u8 full_1ns;
extern volatile u16 *sec_order_p;

#ifdef ERRHANDLE_ECCT
share_data_zi lda_t *gc_lda_list;  //gc lda list from p2l
share_data_zi void *shr_dtag_meta;
share_data_zi void *shr_ddtag_meta;
//share_data u8 host_sec_bitz;
#endif
extern u32 fcmd_outstanding_cnt;
extern bool ucache_flush_flag;
extern volatile u8 plp_trigger;
extern bool fill_dummy_flag;
extern volatile bool shr_shutdownflag;
#if SYNOLOGY_SETTINGS
share_data_zi volatile bool gc_wcnt_sw_flag;
#endif

share_data_zi volatile bool in_ppu_check;
share_data_zi volatile u8 ppu_cnt;
ddr_sh_data volatile ppu_list_t ppu_list[MAX_PPU_LIST];

extern void __attribute__((weak, alias("__ftl_spb_weak_retire"))) ftl_spb_weak_retire(spb_weak_retire_t weak_retire);

fast_code void __ftl_spb_weak_retire(spb_weak_retire_t weak_retire)
{
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SPB_WEAK_RETIRE, 0, weak_retire.all);
}

/*!
 * @brief trigger die command
 *
 * @param die_id	target die to handled
 *
 * @return		true if any command triggerd
 */
static bool die_cmd_scheduler(int die_id);

#ifndef DUAL_BE
extern void set_ncl_cmd_raid_info(ncl_w_req_t *req, struct ncl_cmd_t *ncl_cmd);
extern void raid_correct_push(rc_req_t *rc_req);
#endif

/*!
 * @brief get ncl command from target die manager
 *
 * @param mgr		pointer to die manager
 *
 * @return		ncl command
 */
static inline struct ncl_cmd_t *get_ncl_cmd(die_mgr_t *mgr)
{
	int ncl_cmd_id = find_first_zero_bit(mgr->otf_cmd_bitmap, 16);
	sys_assert(ncl_cmd_id != 16);
	struct ncl_cmd_t *ncl_cmd = &mgr->ncl_cmd[ncl_cmd_id];

	ncl_cmd->addr_param.common_param.info_list = &mgr->info[ncl_cmd_id * MAX_NCL_CMD_SZ];
	ncl_cmd->caller_priv = &mgr->die_cmd_info[ncl_cmd_id];	// default caller for read command
	memset(ncl_cmd->addr_param.common_param.info_list, 0, sizeof(struct info_param_t) * MAX_NCL_CMD_SZ);
	set_bit(ncl_cmd_id, mgr->otf_cmd_bitmap);
	return ncl_cmd;
}

/*!
 * @brief put ncl command to target die manager
 *
 * @param mgr		pointer to die manager
 * @param ncl_cmd	ncl command to be put
 *
 * @return		not used
 */
static inline void put_ncl_cmd(die_mgr_t *mgr, struct ncl_cmd_t *ncl_cmd)
{
	int ncl_cmd_id = ncl_cmd - &mgr->ncl_cmd[0];

	sys_assert(ncl_cmd_id < ncl_cmd_per_die && ncl_cmd_id >= 0);
	clear_bit(ncl_cmd_id, mgr->otf_cmd_bitmap);
}

/*!
 * @brief inline function to increase ecc error, spin lock protection
 *
 * @return	not used
 */
static inline void inc_ecc_cnt(void)
{
	spin_lock_take(SPIN_LOCK_KEY_FTL_STAT, 0, true);
	ftl_stat.total_ecc_err_cnt++;
	spin_lock_release(SPIN_LOCK_KEY_FTL_STAT);
}

/*!
 * @brief inline function to increase erase error, spin lock protection
 *
 * @return	not used
 */
static inline void inc_era_err_cnt(void)
{
	spin_lock_take(SPIN_LOCK_KEY_FTL_STAT, 0, true);
	ftl_stat.total_era_err_cnt++;
	spin_lock_release(SPIN_LOCK_KEY_FTL_STAT);
}

/*!
 * @brief inline function to increase program error, spin lock protection
 *
 * @return	not used
 */
static inline void inc_prog_err_cnt(void)
{
	spin_lock_take(SPIN_LOCK_KEY_FTL_STAT, 0, true);
	ftl_stat.total_pro_err_cnt++;
	spin_lock_release(SPIN_LOCK_KEY_FTL_STAT);
}


/*!
 * @brief initialize target die queue
 *
 * @param die_que	pointer to die queue
 * @param die_id	die id of the die queue
 * @param gid		group id
 *
 * @return		not used
 */
init_code void die_que_init(die_que_t *die_que, u32 die_id, u32 gid)
{
	//u32 _gid;
	//u32 ch;

	die_que->wptr = die_que->rptr = 0;
	die_que->die_id = die_id;
	die_que->cmpl_ptr = 0;
	//die_que->busy_cnt = 0;

	die_que->issue_sn = 0;
	die_que->cmpl_sn = 0;
	QSIMPLEQ_INIT(&die_que->pend_que);
	//ch = die_id % nand_channel_num();
	//_gid = ch / (nand_channel_num() / NCL_GRP_CNT);

	//if (gid == _gid) {
	if(gid == DIE_Q_RD)
    {
		die_que->bm_pl = sys_malloc(FAST_DATA, sizeof(bm_pl_t) * DIE_QUE_PH_SZ);
		sys_assert(die_que->bm_pl);

		die_que->pda = sys_malloc(FAST_DATA, sizeof(pda_t) * DIE_QUE_PH_SZ);
		sys_assert(die_que->pda);

		die_que->info = sys_malloc(FAST_DATA, sizeof(die_ent_info_t) * DIE_QUE_PH_SZ);
		sys_assert(die_que->info);
    }
    else if(gid == DIE_Q_GC)
    {
        die_que->bm_pl = sys_malloc(FAST_DATA, sizeof(bm_pl_t) * DIE_QUE_PH_GC_SZ);
		sys_assert(die_que->bm_pl);

		die_que->pda = sys_malloc(FAST_DATA, sizeof(pda_t) * DIE_QUE_PH_GC_SZ);
		sys_assert(die_que->pda);

		die_que->info = sys_malloc(FAST_DATA, sizeof(die_ent_info_t) * DIE_QUE_PH_GC_SZ);
		sys_assert(die_que->info);

    }
	//} else {
	//	die_que->bm_pl = NULL;
	//	die_que->pda = NULL;
	//	die_que->info = NULL;
	//}
}

fast_code void read_cnt_reset(spb_id_t spb_id)
{
	shr_rd_cnt_sts[spb_id].all = 0;
}

fast_code void ipc_spb_rd_cnt_updt_ack(volatile cpu_msg_req_t *req)
{
	if (req->pl == ~0)
		_rd_cnt_sts_upd_cnt--;
//	else
//		read_cnt_reset(req->pl);	// reset
}

fast_code void die_sched_timer_handler(void *data)
{
	u32 i;
	die_mgr_t *die_mgr;

	//reset read & write cmd count then schedule
	for (i = 0; i < MAX_DIE_MGR_CNT; i++) {
		die_mgr = &_die_mgr[i];
		die_mgr->rd_issue_ttl = 0;
		die_mgr->wr_issue_ttl = 0;

		die_cmd_scheduler(i);
	}

	mod_timer(&die_que_sched_timer, jiffies + HZ);
}

ps_code void die_mgr_resume(u32 gid)
{
	u32 i;
#if CPU_BE == CPU_ID
	u32 rid = 0;

	ncl_w_reqs_free_cnt = RDISK_NCL_W_REQ_CNT;
	pool_init(&ncl_w_reqs_pool,
			(char*)ncl_w_reqs,
			sizeof(ncl_w_reqs),
			sizeof(ncl_w_req_t), ncl_w_reqs_free_cnt);

	for (i = 0; i < ncl_w_reqs_free_cnt; i++) {
		ncl_w_req_t *req = &ncl_w_reqs[i];
		req->req.id = rid++;
	}

	ncl_e_reqs_free_cnt = RDISK_NCL_E_REQ_CNT;
	pool_init(&ncl_e_reqs_pool,
		(char*)&ncl_e_reqs[0],
		sizeof(ncl_e_req_t) * ncl_e_reqs_free_cnt,
		sizeof(ncl_e_req_t), ncl_e_reqs_free_cnt);
#endif

	for (i = 0; i < MAX_DIE_MGR_CNT; i++) {
		_die_mgr[i].wr_issue_ttl = 0;
		_die_mgr[i].rd_issue_ttl = 0;
	}

	die_que_sched_timer.data = NULL;
	die_que_sched_timer.function = die_sched_timer_handler;
	INIT_LIST_HEAD(&die_que_sched_timer.entry);
	mod_timer(&die_que_sched_timer, jiffies + HZ);
}


ddr_code bool die_mgr_chk_stream_rd_idle(u32 die_id)
{
	die_mgr_t *mgr = &_die_mgr[die_id];
	u32 i;
	if (mgr->otf_cmd_cnt == 0)
	{
        return true;
	}

	for (i = 0; i < ncl_cmd_per_die; i++)
	{
		if (mgr->otf_cmd_bitmap[0] & (1 << i))
		{
			if (mgr->ncl_cmd[i].op_type == NCL_CMD_FW_DATA_READ_STREAMING ||\
				(mgr->ncl_cmd[i].op_code == NCL_CMD_OP_READ_STREAMING_FAST && (mgr->die_cmd_info[i].cmd_info.b.stream == true)) ||\
				(mgr->ncl_cmd[i].op_code == NCL_CMD_PATROL_READ))
			{
				return false;
			}
		}
	}

	return true;
}

init_code void die_mgr_init(int nr_die, u32 gid)
{
	int i;
	//UNUSED u32 lda_list_size;
	u8 ch,ce,lun;

	if (nr_die >= MAX_DIE_MGR_CNT)
		nr_die = MAX_DIE_MGR_CNT;

  if (nr_die >= 64) {
		ncl_cmd_per_die = 2;
		die_que_size = 32;
	}
	else if (nr_die >= 32) {
		ncl_cmd_per_die = 4;//4;//2;
		die_que_size = 64;
	}
			  //  ncl_cmd_per_die = 4;
	else {  // 8, 16 die
		ncl_cmd_per_die = 4;
		die_que_size = 96;   //128 will cause btn timeout
	}

	max_wcmd_per_die = 1;//min(ncl_cmd_per_die / 2, 2);
	sys_assert(max_wcmd_per_die > 0);

	for (i = 0; i < nr_die; i++) {
		int j;

		for (j = 0; j < DIE_Q_MAX; j++) {
			die_que_init(&_die_mgr[i].die_que[j], i, j);
		}

		QSIMPLEQ_INIT(&_die_mgr[i].wr_era);
        QSIMPLEQ_INIT(&_die_mgr[i].other_wr_era_ncl);
        QSIMPLEQ_INIT(&_die_mgr[i].other_rd_ncl);
		_die_mgr[i].wr_busy_cnt = 0;
		_die_mgr[i].otf_cmd_bitmap[0] = ~((1 << ncl_cmd_per_die) - 1);
		_die_mgr[i].otf_cmd_cnt = 0;
		_die_mgr[i].wr_cmd_otf_cnt = 0;
        _die_mgr[i].total_rd_cnt = 0;
        _die_mgr[i].total_wr_era_cnt = 0;
        _die_mgr[i].other_rd_ncl_num = 0;
        _die_mgr[i].other_wr_era_ncl_num = 0;
        _die_mgr[i].read_pool_ratio_cnt = 0;

		_die_mgr[i].ncl_cmd = sys_malloc(FAST_DATA,
				sizeof(struct ncl_cmd_t) * ncl_cmd_per_die);
		sys_assert(_die_mgr[i].ncl_cmd);
		#if OPEN_ROW == 1
		for(u8 k = 0; k < ncl_cmd_per_die; k++)
		{
			_die_mgr[i].ncl_cmd[k].die_id = i;
			_die_mgr[i].ncl_cmd[k].via_sch = VIA_SCH_TAG;
		}
		#endif
		_die_mgr[i].info = sys_malloc(FAST_DATA,
				sizeof(struct info_param_t) * (ncl_cmd_per_die * MAX_NCL_CMD_SZ));
		sys_assert(_die_mgr[i].info);

		_die_mgr[i].die_cmd_info = sys_malloc(FAST_DATA,
				sizeof(die_cmd_info_t) * ncl_cmd_per_die);
		sys_assert(_die_mgr[i].die_cmd_info);
	}

	pad_meta_bmp = ~0;
	for (i = 0; i < PDA_META_IN_GRP; i++) {
		idx_meta_allocate(EACH_DIE_DTAG_CNT, SRAM_IDX_META, &pad_meta_idx[i]);
		clear_bit(i, &pad_meta_bmp);
	}

	evt_register(die_isu_handler, 0, &die_isu_evt);

	die_mgr_resume(gid);

	// PMU: todo backup read counter
    //_rd_cnt_sts = sys_malloc(FAST_DATA, sizeof(read_cnt_sts_t) * nand_block_num());
	//sys_assert(_rd_cnt_sts);

	cpu_msg_register(CPU_MSG_SPB_RD_CNT_UPDT_ACK, ipc_spb_rd_cnt_updt_ack);
	//shr_rd_cnt_sts[gid] = tcm_local_to_share(_rd_cnt_sts);
    //schl_apl_trace(LOG_ERR, 0, "gid:%d shr_rd_cnt_sts:%x\n", gid, shr_rd_cnt_sts[gid]);
    #if CPU_ID == 4
        //memset(shr_rd_cnt_sts, 0, sizeof(read_cnt_sts_t) * nand_block_num());

		for(int i =0;i<nand_block_num();i++)
		{
			shr_rd_cnt_sts[i].all = 0;
		}
        schl_apl_trace(LOG_ERR, 0x4152, "shr_rd_cnt_sts:0x%x", shr_rd_cnt_sts);
	    ///// get dram memory for read count per die(interleave(128) * block number(990/1958) * 2 byte)
		dtag_t rdcnt_counter_dtag_start;
		u32 rdcnt_table_dtag_start;
		u32 spb_du_cnt = occupied_by(((nand_interleave_num() / nand_plane_num()) * sizeof(read_cnt_sts_t) * shr_nand_info.geo.nr_blocks), NAND_DU_SIZE);
		u32 spb_counter_du_cnt = occupied_by(((nand_interleave_num() / nand_plane_num()) * shr_nand_info.geo.nr_blocks), NAND_DU_SIZE);
	    schl_apl_trace(LOG_ERR, 0x259c, "spb_du_cnt: %d, spb_counter_du_cnt %d", spb_du_cnt, spb_counter_du_cnt);

		//alloc dtag for p2l load
		rdcnt_counter_dtag_start = dtag_cont_get(DTAG_T_SRAM, spb_counter_du_cnt);
		rdcnt_table_dtag_start = ddr_dtag_register(spb_du_cnt);
		sys_assert(rdcnt_counter_dtag_start.dtag != _inv_dtag.dtag);
	//	schl_apl_trace(LOG_ERR, 0, "rdcnt_table_dtag_start 0x%x\n", rdcnt_table_dtag_start);
	    rd_cnt_runtime_counter = (runtime_rd_cnt_inc_t *)sdtag2mem(rdcnt_counter_dtag_start.b.dtag);
		memset(rd_cnt_runtime_counter, 0, spb_counter_du_cnt*NAND_DU_SIZE);
	    rd_cnt_runtime_table = (runtime_rd_cnt_idx_t *)ddtag2mem(rdcnt_table_dtag_start);
	    shr_rd_cnt_tbl_addr = (u32)rd_cnt_runtime_table;
		shr_rd_cnt_counter_addr = (u32)rd_cnt_runtime_counter;
	    schl_apl_trace(LOG_ERR, 0xb1df, "rd_cnt_runtime_table.runtime_rd_cnt_grp : 0x%x", rd_cnt_runtime_table);
	    schl_apl_trace(LOG_ERR, 0x56e8, "shr_rd_cnt_tbl_addr : 0x%x", shr_rd_cnt_tbl_addr);
    #endif

    //if(shr_nand_info.geo.nr_blocks == 831)
    {
        rd_cnt_threshold_close = 200000 * shr_nand_info.geo.nr_pages;
		rd_cnt_threshold_open = 150000 * shr_nand_info.geo.nr_pages;
    }
    /*else
    {
        rd_cnt_threshold_close = 90000 * shr_nand_info.geo.nr_pages;
		rd_cnt_threshold_open = 60000 * shr_nand_info.geo.nr_pages;
    }
	*/
    for (lun = 0; lun < nand_lun_num(); lun++)
	{
		for (ce = 0; ce < nand_target_num(); ce++)
		{
			for (ch = 0; ch < nand_channel_num(); ch++)
			{
                gDieQueDieIdTable[ch][ce][lun] = 0xFF;
                gDieQueDieIdTable[ch][ce][lun] = CHANGE_DIENUM_G2L(to_die_id(ch,ce,lun));
			}
		}
	}

    in_ppu_check = false;
    ppu_cnt = 0;
    for (u8 i = 0 ; i < MAX_PPU_LIST ; i++)
    {
        ppu_list[i].LDA = INV_LDA;
        ppu_list[i].valid = false;
    }
}

/*!
 * @brief trigger reserve die queue
 *
 * @return	remain busy count of reserved die queue
 */
fast_code u16 rsv_die_que_trig(void)
{
	rsv_die_que_t *dq = &rsv_die_que;
    u8 q_type;

	while(dq->rptr != dq->wptr) {
		sys_assert(dq->busy_cnt);
        if(dq->info[dq->rptr].b.gc)
        {
            q_type = DIE_Q_GC;
        }
        else
        {
            q_type = DIE_Q_RD;

        }
		bool ret = die_que_rd_ins(&dq->bm_pl[dq->rptr], dq->pda[dq->rptr],
				dq->info[dq->rptr], dq->die_id[dq->rptr], q_type, true);
		if (ret == true) {
			if (++dq->rptr >= RSV_DIE_QUE_SZ)
				dq->rptr = 0;
			dq->busy_cnt--;
		} else {
			return dq->busy_cnt;
		}
	}

	sys_assert(dq->busy_cnt == 0);
	return 0;
}

ddr_code void rsv_die_que_cancel(die_ent_info_t target_info)
{
	rsv_die_que_t *dq = &rsv_die_que;
	u32 i = dq->rptr;
	u32 wptr = dq->rptr;

// #if defined(WA_6654)
// 	sort_die_que->busy_cnt = 0;
// 	sort_die_que->wptr = sort_die_que->rptr = 0;
// #endif

	if (dq->busy_cnt == 0)
	{
		return;
	}

	dq->busy_cnt = 0;
	//i = dq->rptr;
	//wptr = dq->rptr;
	do
	{
		if ((dq->info[i].all & target_info.all) != target_info.all)
		{
			dq->bm_pl[wptr] = dq->bm_pl[i];
			dq->pda[wptr] = dq->pda[i];
			dq->info[wptr] = dq->info[i];
			dq->busy_cnt += 1;
			wptr += 1;

			if (wptr >= RSV_DIE_QUE_SZ)
			{
				wptr = 0;
			}
		}

		i += 1;

		if (i >= RSV_DIE_QUE_SZ)
		{
			i = 0;
		}

	} while (i != dq->wptr);

	dq->wptr = wptr;
}

fast_code bool rsv_die_que_rd_ins(bm_pl_t *bm_pl, pda_t pda, die_ent_info_t info, u8 die_id)
{
	rsv_die_que_t *dq = &rsv_die_que;
	u16 next = dq->wptr + 1;

	if (next >= RSV_DIE_QUE_SZ)
		next = 0;

	if (next == dq->rptr)
		return false;

	dq->busy_cnt++;

	dq->bm_pl[dq->wptr] = *bm_pl;
	dq->pda[dq->wptr] = pda;
	dq->info[dq->wptr].all = info.all;
	dq->die_id[dq->wptr] = die_id;

	dq->wptr = next;

	return true;
}

ddr_code u32 die_que_cancel(u32 die_id, die_ent_info_t target_info)
{
	die_mgr_t *mgr = &_die_mgr[die_id];
	die_que_t *dq = &mgr->die_que[DIE_Q_RD];

	u32 cancel_cnt = 0;
	u32 i;
	u32 wptr = 0;

	// if (dq->bm_pl == NULL)
	// {
	// 	return cancel_cnt;
	// }

	if (dq->wptr == dq->rptr)
	{
		//schl_apl_trace(LOG_ERR, 0, "c:%d d:%d w:%d r:%d t:%d\n", cancel_cnt, die_id, dq->wptr, dq->rptr, mgr->total_rd_cnt);
		return cancel_cnt;
	}

	// scan range rptr -> wptr, and update new wptr,
	i = dq->rptr;
	wptr = dq->rptr;
	do
	{
        if ((dq->info[i].all & target_info.all) == target_info.all)
	    {
		    cancel_cnt += 1;	// will be cancelled
			mgr->total_rd_cnt--;
	    }
	    else
		{
		    // move to new wptr
		    dq->info[wptr] = dq->info[i];
		    dq->bm_pl[wptr] = dq->bm_pl[i];
		    dq->pda[wptr] = dq->pda[i];
		    wptr += 1;
		    if (wptr >= DIE_QUE_SZ)
			{
				wptr = 0;
			}
	    }

		i += 1;

		if (i >= DIE_QUE_SZ)
		{
			i = 0;
		}
	} while (i != dq->wptr);

	dq->wptr = wptr;

	// if (cancel_cnt != 0)
	// {
	 	//schl_apl_trace(LOG_ERR, 0, "c:%d d:%d w:%d r:%d t:%d\n", cancel_cnt, die_id, dq->wptr, dq->rptr, mgr->total_rd_cnt);
	// }
	return cancel_cnt;
}

fast_code bool die_que_rd_ins(bm_pl_t *bm_pl, pda_t pda, die_ent_info_t info, u8 die_id, u8 q_type ,bool imt)
{
	die_mgr_t *mgr = &_die_mgr[die_id];
	die_que_t *dq = &mgr->die_que[q_type];
	u16 next = dq->wptr + 1;
	#ifdef CO_SUPPORT_READ_REORDER
	u16 prev = dq->wptr - 1;
	#endif
	sys_assert(dq->bm_pl);

	if (next >= DIE_QUE_SZ)
		next = 0;

	if (next == dq->cmpl_ptr)
		return false;

	#ifdef CO_SUPPORT_READ_REORDER
	if ((q_type == DIE_Q_RD) && (dq->wptr != dq->rptr) && (imt == false) && (info.b.single))
	{
		if (prev >= DIE_QUE_SZ)
		{
			prev = DIE_QUE_SZ - 1;
		}
		if ((dq->info[prev].b.single == info.b.single) && (dq->info[prev].b.pl == info.b.pl))
		{
			return false;
		}
	}
	#endif

	//dq->busy_cnt++;
    mgr->total_rd_cnt++;
	dq->bm_pl[dq->wptr] = *bm_pl;
	dq->pda[dq->wptr] = pda;
	dq->info[dq->wptr].all = info.all;
	dq->wptr = next;
	set_bit(die_id, _die_mgr_busy);

    extern bool ncl_cmd_in;
    extern bool in_ficu_isr;
	if (imt) {
		if (mgr->otf_cmd_cnt == 0 && ncl_cmd_in == false && (in_ficu_isr == false))
			die_cmd_scheduler(die_id);
		else
			evt_set_cs(die_isu_evt, 0, 0, CS_TASK);
	}

	return true;
}

#if defined(TCG_NAND_BACKUP)
ddr_code u32 die_que_mbr_rd_remain_chk(u32 die_id)
{
	u32 remained_entry = 0;
	if(rsv_die_que.rptr==rsv_die_que.wptr)
		remained_entry += ((_die_mgr[die_id].die_que[DIE_Q_RD].rptr>_die_mgr[die_id].die_que[DIE_Q_RD].wptr)?(_die_mgr[die_id].die_que[DIE_Q_RD].rptr):(_die_mgr[die_id].die_que[DIE_Q_RD].rptr+DIE_QUE_SZ)) - _die_mgr[die_id].die_que[DIE_Q_RD].wptr - 1;
	remained_entry += ((rsv_die_que.rptr>rsv_die_que.wptr)?rsv_die_que.rptr:(rsv_die_que.rptr+RSV_DIE_QUE_SZ)) - rsv_die_que.wptr - 1;

	schl_apl_trace(LOG_ERR, 0x56e9, "dieQ[%d] rem ent:%d, wp:%d, rp:%d, rsvQ wp:%d, rp:%d", die_id, remained_entry, _die_mgr[die_id].die_que[DIE_Q_RD].wptr, _die_mgr[die_id].die_que[DIE_Q_RD].rptr, rsv_die_que.wptr, rsv_die_que.rptr);

	return remained_entry;
}
#endif

fast_code bool die_que_read_avail(u8 die_id)
{
	die_mgr_t *mgr = &_die_mgr[die_id];
	die_que_t *dq = &mgr->die_que[DIE_Q_GC];
	u16 next = dq->wptr + 1;

	if (next >= DIE_QUE_SZ)
		next = 0;

	if (next == dq->cmpl_ptr)
		return false;

	return true;
}

fast_code bool die_que_wr_era_ins(ncl_req_t *ncl_req, u8 die_id)
{
	die_mgr_t *mgr;
	bool ret = true;

	if (get_target_cpu(die_id) != CPU_ID) {
		ncl_req->op_type.b.remote = 1;
		cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_WR_ERA_INS, 0, (u32)ncl_req);
		return true;
	}

#if CPU_ID == CPU_BE
	sys_assert(ncl_req->op_type.b.remote == 0);
#else
	sys_assert(ncl_req->op_type.b.remote == 1);
#endif

    die_id = CHANGE_DIENUM_G2L(die_id);
    mgr = &_die_mgr[die_id];
    sys_assert(mgr);

	//schl_apl_trace(LOG_ERR, 0, "ins die %d PDA %x OTF %d\n", die_id, ncl_req->pda[0], mgr->otf_cmd_cnt);
	QSIMPLEQ_INSERT_TAIL(&mgr->wr_era, ncl_req, link);
	mgr->wr_busy_cnt ++;
    mgr->total_wr_era_cnt++;
	set_bit(die_id, _die_mgr_busy);
	if (mgr->otf_cmd_cnt < 4)
		ret = die_cmd_scheduler(die_id);

	if (ret)
		evt_set_cs(die_isu_evt, 0, 0, CS_TASK);

	return true;
}

/*!
 * @brief dump information of target ncl command
 *
 * @param ncl_cmd	pointer to ncl command to be dumped
 *
 * @return		not used
 */
slow_code void read_err_dump(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;
	bm_pl_t *bm_pl = ncl_cmd->user_tag_list;
	pda_t *pda = ncl_cmd->addr_param.common_param.pda_list;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
	extern u32 shr_wunc_meta_flag;

    if(!plp_trigger){
	    schl_apl_trace(LOG_ERR, 0x0d6e, "read type %d", ncl_cmd->op_type);
    }

	for (i = 0; i < cnt; i++) {
        if (info[i].status > ficu_err_du_ovrlmt)
        {
        	if(info[i].status == ficu_err_par_err)
				shr_wunc_meta_flag = true;

    		u32 spb = pda2blk(pda[i]);
            u32 die = pda2die(pda[i]);

            if(!plp_trigger){
    		schl_apl_trace(LOG_ERR, 0x325a, "[%d] b(%d) du(%d) pda 0x%x st %x",
    				i, bm_pl[i].pl.btag, bm_pl[i].pl.du_ofst, pda[i], info[i].status);
            schl_apl_trace(LOG_ERR, 0x2d94, "[%d] die %d spb %d slc %d open %d page %d",
    			i, die, spb,
    			spb_rt_flags[spb].b.type,
    			spb_rt_flags[spb].b.open,
    			pda2page(pda[i]));
            }
        }
	}
}

fast_code void read_err_ua_chk(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;
	bm_pl_t *bm_pl = ncl_cmd->user_tag_list;
    //pda_t *pda = ncl_cmd->addr_param.common_param.pda_list;
    struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

	//schl_apl_trace(LOG_ERR, 0x0364, "read type %d", ncl_cmd->op_type);

	for (i = 0; i < cnt; i++)
    {
        if(bm_pl[i].pl.btag == ua_btag)
        {
            if (info[i].status != ficu_err_good)
            {
        		//schl_apl_trace(LOG_ERR, 0x9c0e, "ua data [%d] btag[%d] du[%d] pda[0x%x] st[0x%x]", i, bm_pl[i].pl.btag, bm_pl[i].pl.du_ofst, pda[i], info[i].status);

                ipc_api_ucache_read_error_data_in(bm_pl[i].pl.du_ofst, info[i].status);
            }
        }
	}
    return;
}


#ifndef ERRHANDLE_GLIST // Handle by ErrHandle_Task, Paul_20201202
fast_code void set_spb_weak_retire(spb_id_t spb_id, u32 type)
{
	spb_weak_retire_t weak_retire;
	bool issue = false;

	if ((spb_rt_flags[spb_id].b.open) && (type != SPB_RETIRED_BY_ERASE)
			&& (type != SPB_RETIRED_BY_PROG))	/// open SPB set retire for erase and write fail
		return;

	if (type == SPB_READ_WEAK && shr_rd_cnt_sts[spb_id].b.weak == 0)
	{
		weak_retire.b.spb_id = spb_id;
		weak_retire.b.type = type;
		issue = true;
		shr_rd_cnt_sts[spb_id].b.weak = 1;
    }
    else if (type == SPB_PROG_WEAK && shr_rd_cnt_sts[spb_id].b.weak == 0)// Set SPB_DESC_F_WEAK for GC, SPB_DESC_F_RETIRED will be checkd when mark bad, Paul_20201202
    {
        weak_retire.b.spb_id = spb_id;
		weak_retire.b.type = type;
		issue = true;
		shr_rd_cnt_sts[spb_id].b.weak = 1;
	}
	else if (shr_rd_cnt_sts[spb_id].b.retired == 0)
	{
		shr_rd_cnt_sts[spb_id].b.retired = 1;
		shr_rd_cnt_sts[spb_id].b.weak = 1;
		weak_retire.b.spb_id = spb_id;
		weak_retire.b.type = type;
		issue = true;
	}

	if (issue) {
		ftl_spb_weak_retire(weak_retire);
		schl_apl_trace(LOG_ALW, 0x4779, "set spb %d type %d", spb_id, type);
	}
}
#endif

fast_code void read_cnt_issue(spb_id_t spb_id)
{
//	if (_rd_cnt_sts_upd_cnt >= 4)
//		return;

//	_rd_cnt_sts_upd_cnt++;
    ipc_spb_rd_cnt_upd(spb_id);
}

fast_code void read_cnt_increase(spb_id_t spb_id, u32 die_id)
{
//	extern read_cnt_sts_t *_rd_cnt_sts;
	extern spb_rt_flags_t *spb_rt_flags;
    volatile read_cnt_sts_t *_rd_cnt_max_per_blk = (volatile read_cnt_sts_t *)&shr_rd_cnt_sts[0];

	u32 spb_num_per_dtag = DTAG_SZE / sizeof(read_cnt_sts_t) / shr_nand_info.lun_num;
    u16 rd_cnt_grp, num_of_rd_cnt_grp;

    rd_cnt_runtime_table = (runtime_rd_cnt_idx_t *)shr_rd_cnt_tbl_addr;

    rd_cnt_grp = spb_id / spb_num_per_dtag;
    num_of_rd_cnt_grp = (spb_id % spb_num_per_dtag) * shr_nand_info.lun_num + die_id;

	rd_cnt_runtime_table += rd_cnt_grp;
	rd_cnt_runtime_table->runtime_rd_cnt_idx[num_of_rd_cnt_grp] += 0x100;

	if(rd_cnt_runtime_table->runtime_rd_cnt_idx[num_of_rd_cnt_grp] > _rd_cnt_max_per_blk[spb_id].b.counter)
    {
//        _rd_cnt_sts[spb_id].b.counter = rd_cnt_tbl_ptr->runtime_rd_cnt_idx[num_of_rd_cnt_grp];
        _rd_cnt_max_per_blk[spb_id].b.counter = rd_cnt_runtime_table->runtime_rd_cnt_idx[num_of_rd_cnt_grp];
 		u32 rd_cnt_threshold = spb_rt_flags[spb_id].b.open ? rd_cnt_threshold_open : rd_cnt_threshold_close;
        if (_rd_cnt_max_per_blk[spb_id].b.counter >= rd_cnt_threshold)
        {
            if ((gGCInfo.rdDisturbCnt <= 10) && (!_rd_cnt_max_per_blk[spb_id].b.api))
            {
                _rd_cnt_max_per_blk[spb_id].b.api = true;
                schl_apl_trace(LOG_INFO, 0x6328, "_rd_cnt_max_per_blk[%d].b.counter: %d", spb_id, _rd_cnt_max_per_blk[spb_id].b.counter);
                read_cnt_issue(spb_id);
            }
        }
    }
}

#if 0
fast_data_ni struct timer_list rd_debug_timer;
fast_data_ni u32 prev_open_blk;

ddr_code void rd_debug(void *data)
{
    extern volatile u16 ps_open[3];
    extern volatile read_cnt_sts_t shr_rd_cnt_sts[MAX_SPB_COUNT];
    volatile read_cnt_sts_t *_rd_cnt_max_per_blk = (volatile read_cnt_sts_t *)&shr_rd_cnt_sts[0];
	static u8 times_cnt;
	if(prev_open_blk == INV_SPB_ID)
	{
		times_cnt = 0;
		prev_open_blk = ps_open[0];
	}
	if(times_cnt < 4)
	{
	    if(_rd_cnt_max_per_blk[prev_open_blk].b.api == true)
	    {
	        mod_timer(&rd_debug_timer, jiffies+200);
		    return;
	    }
	    _rd_cnt_max_per_blk[prev_open_blk].b.api = true;
        ipc_spb_rd_cnt_upd(prev_open_blk);
		times_cnt ++;
	    mod_timer(&rd_debug_timer, jiffies+300);
	}
	else
	{
	    prev_open_blk = INV_SPB_ID;
	    mod_timer(&rd_debug_timer, jiffies+6000);
	}
}

static slow_code int read_cnt_issue_t(int argc, char *argv[])
{
	u32 on = strtol(argv[1], (void *)0, 10);
	if(on == 1)
	{
	    rd_debug_timer.function = rd_debug;
		rd_debug_timer.data = NULL;
		prev_open_blk = INV_SPB_ID;
	    mod_timer(&rd_debug_timer, jiffies+20);
	    schl_apl_trace(LOG_ALW, 0x4cdd, "==========rd_debug begin==========");
	}
	else if(on == 0)
	{
		del_timer(&rd_debug_timer);
	    schl_apl_trace(LOG_ALW, 0xef04, "==========rd_debug stop==========");
	}
    return 0;
}

static DEFINE_UART_CMD(rd_exec, "rd_exec", "rd_exec", "rd_exec", 1, 1, read_cnt_issue_t);

static slow_code int ps_open_main(int argc, char *argv[])
{
    extern volatile u16 ps_open[3];
	schl_apl_trace(LOG_ALW, 0x8336, "open blk: NRM [%d]  GC [%d]  P/QBT [%d]", ps_open[0], ps_open[1], ps_open[2]);
    return 0;
}

static DEFINE_UART_CMD(open_blk, "open_blk", "open_blk", "open_blk", 0, 0, ps_open_main);
#endif

slow_code void read_sts_update(pda_t *pda, u32 cnt, struct info_param_t *info, bool gc)
{
	u32 i;

	for (i = 0; i < cnt; i ++)
    {
		pda_t p = pda[i];
		#ifndef ERRHANDLE_GLIST // Handle by ErrHandle_Task, Paul_20201202
		spb_id_t spb_id;
		#endif

		if (info[i].status == ficu_err_par_err)
			continue;	// it is not real error
        else if(info[i].status == ficu_err_nard)
            continue;

		if (info[i].status != ficu_err_good)
			p = ftl_re_remap_pda(p);

		if (info[i].status > ficu_err_du_ovrlmt) {
			inc_ecc_cnt();

			#ifndef ERRHANDLE_GLIST // Handle by ErrHandle_Task, Paul_20201202
			spb_id = nal_pda_get_block_id(p);
			set_spb_weak_retire(spb_id, gc ? SPB_RETIRED_BY_READ_GC : SPB_RETIRED_BY_READ);
			#endif

		} else if (info[i].status == ficu_err_du_ovrlmt) {

			#ifndef ERRHANDLE_GLIST // Handle by ErrHandle_Task, Paul_20201202
			spb_id = nal_pda_get_block_id(p);
			set_spb_weak_retire(spb_id, SPB_READ_WEAK);
			#endif

		} else {
			// should be good
		}
	}
}

/*!
 * @brief find if and read error in the ncl command
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		true if any error
 */
fast_code bool is_there_uc_pda(struct ncl_cmd_t *ncl_cmd)
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

/*!
 * @brief handle function for raid recovery done
 *
 * @param req		pointer to recovery request
 *
 * @return		not used
 */
slow_code void die_que_rc_done(rc_req_t* req)
{
    struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *)req->caller_priv;

    rc_dtag_t* rc_dtag_cur = req->rc_dtag;
    rc_dtag_t* rc_dtag_prev;
	if (req->flags.b.gc) {
		// TODO: gc rd err handle
		//sys_assert(req->flags.b.fail);
		u32 grp = req->bm_pl_list[0].pl.nvm_cmd_id;
		scheduler_gc_read_done(grp, req->bm_pl_list, req->list_len);
	}
    else if(ncl_cmd->flags & NCL_CMD_P2L_READ_FLAG)
    {
        if(req->flags.b.fail == true)
        {
            ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
            ncl_cmd->retry_step = 0xFF;
        }
        else
        {
            ncl_cmd->status &= ~NCL_CMD_ERROR_STATUS;
            //schl_apl_trace(LOG_INFO, 0, "p2l rc success");
        }
        ncl_cmd->completion(ncl_cmd);
    }

    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SCAN_GLIST_TRG_HANDLE, 0, EH_BUILD_GL_TRIG_EHTASK);

	if (req->flags.b.put_ncl_cmd){
		struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *)req->caller_priv;
		die_cmd_info_t *die_cmd_info = (die_cmd_info_t *) ncl_cmd->caller_priv;
		die_que_t *die_que = die_cmd_info->die_queue;
		die_mgr_t *mgr = &_die_mgr[die_que->die_id];
		put_ncl_cmd(mgr, ncl_cmd);
		mgr->otf_cmd_cnt--;
	}

    rc_dtag_cur->r_ptr += req->mem_len;

    // chk prev
    while (rc_dtag_cur->prev != NULL)
    {
        rc_dtag_prev = rc_dtag_cur->prev;
        if(rc_dtag_prev->r_ptr == rc_dtag_prev->w_ptr){
            rc_dtag_cur->prev = rc_dtag_prev->prev;

            dtag_put(DTAG_T_SRAM, rc_dtag_prev->dtag);
            sys_free(SLOW_DATA, rc_dtag_prev);
        }
    }
}

/*!
 * @brief prepare recovery request from the ncl command
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		recovery request
 */
slow_code rc_req_t* rc_req_prepare(struct ncl_cmd_t *ncl_cmd, bool flag_put_ncl_cmd)
{
	u32 len;
	rc_req_t *req;
	pda_t *pda_list;
	bm_pl_t *bm_pl_list;
	struct info_param_t *info_list;
	die_cmd_info_t *die_cmd_info = (die_cmd_info_t *)ncl_cmd->caller_priv;
    u16 mem_len;
    rc_dtag_t* rc_dtag_new;
	len = ncl_cmd->addr_param.common_param.list_len;
    mem_len = sizeof(rc_req_t) + (sizeof(pda_t)  + sizeof(bm_pl_t)  + sizeof(struct info_param_t)) * len;

    if(rc_dtag == NULL || (rc_dtag->w_ptr + mem_len >= DTAG_SZE)){
        rc_dtag_new = sys_malloc(SLOW_DATA, sizeof(rc_dtag_t));
        sys_assert(rc_dtag_new);
        memset(rc_dtag_new, 0, sizeof(rc_dtag_t));
        rc_dtag_new->prev = rc_dtag;
        rc_dtag = rc_dtag_new;

        rc_dtag->dtag = dtag_get(DTAG_T_SRAM, (void *)&(rc_dtag->mem));
        sys_assert(rc_dtag->dtag.dtag != _inv_dtag.dtag);

        memset(rc_dtag->mem, 0, DTAG_SZE);
        rc_dtag->w_ptr = 0;
        rc_dtag->r_ptr = 0;
    }
    req = (rc_req_t*)((u8*)(rc_dtag->mem) + rc_dtag->w_ptr);
    rc_dtag->w_ptr += sizeof(rc_req_t);
    pda_list = (pda_t*)((u8*)(rc_dtag->mem) + rc_dtag->w_ptr);
    rc_dtag->w_ptr += sizeof(pda_t) * len;
    bm_pl_list = (bm_pl_t*)((u8*)(rc_dtag->mem) + rc_dtag->w_ptr);
    rc_dtag->w_ptr += sizeof(bm_pl_t) * len;
    info_list = (struct info_param_t*)((u8*)(rc_dtag->mem) + rc_dtag->w_ptr);
    rc_dtag->w_ptr += sizeof(struct info_param_t) * len;

    req->rc_dtag = rc_dtag;
    req->mem_len = mem_len;

    schl_apl_trace(LOG_ALW, 0x9ab6, "rc_dtag %d prev 0x%x w_ptr %d r_ptr %d mem_len %d",
        rc_dtag->dtag.dtag, rc_dtag->prev, rc_dtag->w_ptr, rc_dtag->r_ptr, mem_len);

	memcpy(bm_pl_list, ncl_cmd->user_tag_list, sizeof(bm_pl_t) * len);
	memcpy(pda_list, ncl_cmd->addr_param.common_param.pda_list, sizeof(pda_t) * len);
	memcpy(info_list, ncl_cmd->addr_param.common_param.info_list, sizeof(struct info_param_t) * len);

	req->list_len = len;
	req->pda_list= pda_list;
	req->info_list = info_list;
	req->bm_pl_list = bm_pl_list;

	req->flags.all = 0;
	req->flags.b.gc = die_cmd_info->cmd_info.b.gc;
	req->flags.b.host = die_cmd_info->cmd_info.b.host;
	req->flags.b.stream = die_cmd_info->cmd_info.b.stream;
	req->flags.b.put_ncl_cmd = flag_put_ncl_cmd;

	req->caller_priv = ncl_cmd;
	req->cmpl = die_que_rc_done;

	return req;
}

/*!
 * @brief handle function for ncl read done
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
fast_code void read_issue_done(struct ncl_cmd_t *ncl_cmd)
{
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;
	die_cmd_info_t *die_cmd_info = (die_cmd_info_t *) ncl_cmd->caller_priv;
	die_que_t *die_que = die_cmd_info->die_queue;
	die_mgr_t *mgr = &_die_mgr[die_que->die_id];
	bool flag_put_ncl_cmd = true;
    //#ifdef NCL_HAVE_reARD
    //bool err_handling_again = false;
    //#endif

	// decoding error happen, the cmpl of read cmd out of order
	if (die_que->cmpl_sn != die_cmd_info->sn) {
		//schl_apl_trace(LOG_DEBUG, 0, "die %d cmd 0x%x sn %d pend cmpl",
			//die_que->die_id, ncl_cmd, die_cmd_info->sn);
		QSIMPLEQ_INSERT_TAIL(&die_que->pend_que, ncl_cmd, entry);
		return;
	}
	if ((ncl_cmd->op_code == NCL_CMD_OP_READ && ncl_cmd->op_type == NCL_CMD_FW_DATA_READ_STREAMING) ||
		(ncl_cmd->op_code == NCL_CMD_OP_READ_STREAMING_FAST && ncl_cmd->op_type == NCL_CMD_FW_DATA_READ_STREAMING)) {
#if (CPU_ID == 2)
		extern u32 cpu2_streaming_read_cnt;
		cpu2_streaming_read_cnt--;
#elif (CPU_ID == 4)
	    extern u32 cpu4_streaming_read_cnt;
		cpu4_streaming_read_cnt--;
#endif
	}

#if NCL_FW_RETRY
	if(ncl_cmd->status)
    {
        if(!plp_trigger)
        {
    		u32 nsid = INT_NS_ID;
			#if (CO_SUPPORT_READ_AHEAD == TRUE)
			if (die_cmd_info->cmd_info.b.ra)
			{
				read_err_dump(ncl_cmd);
				AplReadRetry_AbortRa(ncl_cmd);
				goto cmd_put;
			}
			#endif
    		if (die_cmd_info->cmd_info.b.host)
    			nsid = INT_NS_ID - 1;

            if (ncl_cmd->flags & NCL_CMD_NO_READ_RETRY_FLAG)
            {
        		bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
                if (fcns_raid_enabled(nsid) && is_there_uc_pda(ncl_cmd) && (rced == false))
                {
        			rc_req_t* rc_req = rc_req_prepare(ncl_cmd, true);
    				flag_put_ncl_cmd = false;
        			raid_correct_push(rc_req);
        			goto cmd_put;
        		}
                else
                {
                    if(die_cmd_info->cmd_info.b.host)
                    {
                        dfu_blank_err_chk(ncl_cmd, die_cmd_info->cmd_info);
                    }
                }
            }
            else
            {
            	read_err_dump(ncl_cmd);
                if(!plp_trigger){
            	    schl_apl_trace(LOG_ERR, 0xf522, "%x read error", die_cmd_info->cmd_info.all);
                }
        		rd_err_handling(ncl_cmd);

                read_sts_update(ncl_cmd->addr_param.common_param.pda_list,
            			ncl_cmd->addr_param.common_param.list_len,
            			ncl_cmd->addr_param.common_param.info_list,
            			die_cmd_info->cmd_info.b.gc);
                return;
            }
        }
        else
        {
            //schl_apl_trace(LOG_ERR, 0xd790, "[DBG] read error after rise gpio flag");
            read_err_ua_chk(ncl_cmd);
            //goto cmd_put;
        }
    }
#else
#ifdef NCL_HAVE_reARD
    if ((ncl_cmd->status) && (!plp_trigger))
    {
        u32 nsid = INT_NS_ID;
        if (die_cmd_info->cmd_info.b.host)
            nsid = INT_NS_ID - 1;

/*
        check_rc_flag:
        if(ncl_cmd->re_ard_flag == false)   //tony 20201021
        {
    		bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && is_there_uc_pda(ncl_cmd) && (rced == false))
            {
    			rc_req_t* rc_req = rc_req_prepare(ncl_cmd, true);
				flag_put_ncl_cmd = false;
    			raid_correct_push(rc_req);
    			goto cmd_put;
    		}
            //schl_apl_trace(LOG_INFO, 0, "[Tony] fcns: %d, un pda: %d, rced: %d", fcns_raid_enabled(nsid), is_there_uc_pda(ncl_cmd), rced);
        }

        if(err_handling_again == false)
        {
    		read_err_dump(ncl_cmd);
    		schl_apl_trace(LOG_ERR, 0, "%x read error", die_cmd_info->cmd_info.all);

    		rd_err_handling(ncl_cmd);
    		read_sts_update(ncl_cmd->addr_param.common_param.pda_list,
    				ncl_cmd->addr_param.common_param.list_len,
    				ncl_cmd->addr_param.common_param.info_list,
    				die_cmd_info->cmd_info.b.gc);
        }

        if(ncl_cmd->re_ard_flag == true)
        {
            ncl_cmd->re_ard_flag = false;   //tony 20201021
            err_handling_again = true;
            goto check_rc_flag;
        }

*/


        read_err_dump(ncl_cmd);
        schl_apl_trace(LOG_ERR, 0x74b6, "%x read error", die_cmd_info->cmd_info.all);

        rd_err_handling(ncl_cmd);
        read_sts_update(ncl_cmd->addr_param.common_param.pda_list,
                ncl_cmd->addr_param.common_param.list_len,
                ncl_cmd->addr_param.common_param.info_list,
                die_cmd_info->cmd_info.b.gc);

		bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
        if (fcns_raid_enabled(nsid) && is_there_uc_pda(ncl_cmd) && (rced == false))
        {
			rc_req_t* rc_req = rc_req_prepare(ncl_cmd, true);
			flag_put_ncl_cmd = false;
			raid_correct_push(rc_req);
			goto cmd_put;
		}
    }
#endif
#endif

	if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug)) {
		//nvm_cmd_id used for gc pda group in gc read
		u32 grp = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
		u32 cnt = ncl_cmd->addr_param.common_param.list_len;

		scheduler_gc_read_done(grp, ncl_cmd->user_tag_list, cnt);
	}
#if PLP_DEBUG
    if (die_cmd_info->cmd_info.b.debug) {
        extern u32 cpu2_read_cnt;
        extern u32 cpu4_read_cnt;
        if (CPU_ID == CPU_BE)
        {
            cpu2_read_cnt += cnt;
        }
        else
        {
            cpu4_read_cnt += cnt;
        }
        //schl_apl_trace(LOG_DEBUG, 0, "plp debug read issue done cpu2:%d cpu4:%d total:%d", cpu2_read_cnt, cpu4_read_cnt,
        cpu2_read_cnt + cpu4_read_cnt);
        // 256 read done
        if (cpu2_read_cnt + cpu4_read_cnt == 127) {
            if (CPU_ID == CPU_BE)
            {
                extern void ipc_api_plp_debug_fill_done(volatile cpu_msg_req_t *req);
                ipc_api_plp_debug_fill_done((volatile cpu_msg_req_t *)NULL);
            }
            else
            {
                cpu_msg_issue(CPU_BE - 1, CPU_MSG_PLP_FILL_DONE, 0, 0);
            }

        }

    }
#endif

cmd_put:
	die_que->cmpl_sn++;
	die_que->cmpl_ptr += cnt;
	if (die_que->cmpl_ptr >= DIE_QUE_SZ)
		die_que->cmpl_ptr -= DIE_QUE_SZ;

	if(flag_put_ncl_cmd){
		put_ncl_cmd(mgr, ncl_cmd);
		mgr->otf_cmd_cnt--;
	}

	if (mgr->total_rd_cnt != 0 || mgr->total_wr_era_cnt != 0)  // wait busy cnt will cause 128K QD1 random read drop, (no >=MAX_RD_SZ)
		die_cmd_scheduler(die_que->die_id);


	if (!QSIMPLEQ_EMPTY(&die_que->pend_que)) {
		cmpl_pend_que local_que;
		struct ncl_cmd_t *_ncl_cmd;

		QSIMPLEQ_MOVE(&local_que, &die_que->pend_que);
		while (!QSIMPLEQ_EMPTY(&local_que)) {
			_ncl_cmd = QSIMPLEQ_FIRST(&local_que);
			QSIMPLEQ_REMOVE_HEAD(&local_que, entry);

			//die_cmd_info = (die_cmd_info_t *)_ncl_cmd->caller_priv;
			//schl_apl_trace(LOG_DEBUG, 0, "die %d cmd 0x%x sn %d resume cmpl",
				//die_que->die_id, _ncl_cmd, die_cmd_info->sn);
			_ncl_cmd->completion(_ncl_cmd);
		}
	}

	//resume gc read if it's blocked by die read queue
	 if (test_and_clear_bit(die_que->die_id, gc_scheduler.pend_die_bmp))
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);
}



fast_code void ncl_cmd_put(struct ncl_cmd_t *ncl_cmd, bool flag_put_ncl_cmd)
{
	die_cmd_info_t *die_cmd_info = (die_cmd_info_t *) ncl_cmd->caller_priv;
	die_que_t *die_que = die_cmd_info->die_queue;
	die_mgr_t *mgr = &_die_mgr[die_que->die_id];
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;

	die_que->cmpl_sn++;
	die_que->cmpl_ptr += cnt;
	if (die_que->cmpl_ptr >= DIE_QUE_SZ)
		die_que->cmpl_ptr -= DIE_QUE_SZ;

	if(flag_put_ncl_cmd){
		put_ncl_cmd(mgr, ncl_cmd);
		mgr->otf_cmd_cnt--;
	}

	if (mgr->total_rd_cnt || mgr->total_wr_era_cnt)  // wait busy cnt will cause 128K QD1 random read drop, (no >=MAX_RD_SZ)
		die_cmd_scheduler(die_que->die_id);


	if (!QSIMPLEQ_EMPTY(&die_que->pend_que)) {
		cmpl_pend_que local_que;
		struct ncl_cmd_t *_ncl_cmd;

		QSIMPLEQ_MOVE(&local_que, &die_que->pend_que);
		while (!QSIMPLEQ_EMPTY(&local_que)) {
			_ncl_cmd = QSIMPLEQ_FIRST(&local_que);
			QSIMPLEQ_REMOVE_HEAD(&local_que, entry);

			die_cmd_info = (die_cmd_info_t *)_ncl_cmd->caller_priv;
			//schl_apl_trace(LOG_DEBUG, 0, "die %d cmd 0x%x sn %d resume cmpl",
			//	die_que->die_id, _ncl_cmd, die_cmd_info->sn);
			_ncl_cmd->completion(_ncl_cmd);
		}
	}

	//resume gc read if it's blocked by die read queue
	 if (test_and_clear_bit(die_que->die_id, gc_scheduler.pend_die_bmp))
		scheduler_gc_vpda_read(GC_LOCAL_PDA_GRP);

}
/*!
 * @brief handle function for ncl write done
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
fast_code void write_issue_done(struct ncl_cmd_t *ncl_cmd)
{
	ncl_w_req_t *ncl_req = ncl_cmd->caller_priv;
	die_mgr_t *mgr;
	u16 die_id;
	u8  duIdx;

    die_id = CHANGE_DIENUM_G2L(ncl_req->req.die_id);
    mgr = &_die_mgr[die_id];

	if (ncl_cmd->status) {
		u32 i;
		//bm_pl_t *bm_pl = ncl_cmd->user_tag_list;
		//u32 idx = 0;

        #ifdef ERRHANDLE_ECCT
        u32 LDA;
        #endif

		inc_prog_err_cnt();

		for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
			//u32 j;

			schl_apl_trace(LOG_ERR, 0x4913, "PDA[%d] %x", i, ncl_cmd->addr_param.common_param.pda_list[i]);

			// Check info_list[i] for NAND err, DBG, PgFalVry
			// And check lda[i * DU_CNT_PER_PAGE + duIdx] for PPU err.
			//for (j = 0; j < DU_CNT_PER_PAGE; j++) {
			//	schl_apl_trace(LOG_ERR, 0, "  [%d] dtag %x", idx, bm_pl[idx].pl.dtag);
#if 0//!defined(ENABLE_L2CACHE) && !defined(DISABLE_HS_CRC_SUPPORT)// debug

                u8 num_lba=0;
				if(host_sec_bitz==9)
                    num_lba=8;//joe  add sec size 20200818
                else
				    num_lba=1;

				if (bm_pl[idx].pl.dtag & DTAG_IN_DDR_MASK) {
					extern void *ddr_hcrc_base;
					u16 *hcrc = (u16 *) ddr_hcrc_base;
					u32 k;
					dtag_t dtag = { .dtag = bm_pl[idx].pl.dtag};


					//for (k = 0; k < NR_LBA_PER_LDA; k++)//joe add sec size 20200817
						//schl_apl_trace(LOG_ERR, 0, "%x ", hcrc[dtag.b.dtag * NR_LBA_PER_LDA + k]);
					for (k = 0; k < num_lba; k++)
						schl_apl_trace(LOG_ERR, 0x581d, "%x ", hcrc[dtag.b.dtag * num_lba + k]);

					schl_apl_trace(LOG_ERR, 0xb17f, "\n");
				}

#endif
				if(ncl_cmd->addr_param.common_param.info_list[i].status == cur_du_good)
				{
				    if (ppu_cnt)
                    {
                        for (duIdx = 0; ppu_cnt && duIdx < DU_CNT_PER_PAGE; duIdx++)
						{
							LDA = ncl_req->w.lda[i * DU_CNT_PER_PAGE + duIdx];
                            for (u8 j = 0 ; j < MAX_PPU_LIST ; j++)
                            {
                                if (ppu_list[j].valid && ppu_list[j].LDA == LDA)
                                {
                                    schl_apl_trace(LOG_ERR, 0xd3ce, "release first times hit ppu error, LDA=0x%x", LDA);
                                    ppu_list[j].valid = false;
                                    ppu_cnt--;
                                    break;
                                }
                            }
                        }
                    }

				}
				else if (ncl_cmd->addr_param.common_param.info_list[i].status == nand_err)
				{
					schl_apl_trace(LOG_ERR, 0x3371, "r 0x%x nand_err error at 0x%x with t 0x%x c 0x%x f 0x%x",
							(u32)ncl_req, ncl_req->req.die_id, ncl_cmd->op_type, ncl_cmd->op_code, ncl_cmd->flags);
					ncl_req->req.op_type.b.prog_err = true;
				}
                else	// Encoder error cases, DBG, LJ1-252, PgFalCmdTimeout
                {
                    #ifdef ERRHANDLE_ECCT

                    if (!plp_trigger)
                    {
                        while (in_ppu_check); // wait other CPU check PPU error
                    }

                    in_ppu_check = true;
                    if(ncl_req->req.op_type.b.host)
                    {
    					bool is_gc = ((ncl_req->req.nsid == HOST_NS_ID) && (ncl_req->req.type == FTL_CORE_GC));
                        schl_apl_trace(LOG_ERR, 0x2271, "r 0x%x ppu error at 0x%x with t 0x%x c 0x%x f 0x%x",
							(u32)ncl_req, ncl_req->req.die_id, ncl_cmd->op_type, ncl_cmd->op_code, ncl_cmd->flags);

						for (duIdx = 0; duIdx < DU_CNT_PER_PAGE; duIdx++)
						{
							LDA = ncl_req->w.lda[i * DU_CNT_PER_PAGE + duIdx];

							// Found ppu err on user data LDA.
							if ((LDA & ECC_LDA_POISON_BIT) && ((LDA & ~ECC_LDA_POISON_BIT) <= _max_capacity))
							{
								ncl_req->w.lda[i * DU_CNT_PER_PAGE + duIdx] &= ~ECC_LDA_POISON_BIT;

								LDA &= ~ECC_LDA_POISON_BIT;
                                if (is_gc || plp_trigger)
                                {
                                    if (CPU_ID != CPU_BE)
		                            {
		                                cpu_msg_issue(CPU_BE - 1, CPU_MSG_ECCT_MARK, ECC_REG_WHCRC, LDA);
		                            }
		                            else
		                            {
		                                Register_ECCT_By_Raid_Recover(LDA, ~(0x00), ECC_REG_WHCRC);
		                            }

                                    continue;
                                }

                                u32 invalid_idx = MAX_PPU_LIST;
                                u8 j;
                                for (j = 0 ; j < MAX_PPU_LIST ; j++)
                                {
                                    if (invalid_idx == MAX_PPU_LIST && !ppu_list[j].valid)
                                        invalid_idx = j;

                                    if (ppu_list[j].valid && ppu_list[j].LDA == LDA)
                                        break;
                                }


                                if (j != MAX_PPU_LIST)
                                {
                                    schl_apl_trace(LOG_ERR, 0x3df5, "second times hit ppu error, regist ECCT.");
                                    ppu_list[j].valid = false;
                                    ppu_cnt--;
                                    if (CPU_ID != CPU_BE)
		                            {
		                                cpu_msg_issue(CPU_BE - 1, CPU_MSG_ECCT_MARK, ECC_REG_WHCRC, LDA);
		                            }
		                            else
		                            {
		                                Register_ECCT_By_Raid_Recover(LDA, ~(0x00), ECC_REG_WHCRC);
		                            }
                                }
                                else
                                {
                                    schl_apl_trace(LOG_ERR, 0x6783, "first time hit ppu error, record & re-write.");
                                    ppu_list[invalid_idx].LDA = LDA;
                                    ppu_list[invalid_idx].valid = true;
                                    ppu_cnt++;
                                    ncl_req->req.op_type.b.skip_updt = true;
                                    if (CPU_ID != CPU_BE)
		                            {
		                                cpu_msg_issue(CPU_BE - 1, CPU_MSG_ECCT_RECORD, ECC_REG_WHCRC, LDA);
		                            }
		                            else
		                            {
		                                wECC_Record_Table(LDA, ~(0x00), ECC_REG_WHCRC);
		                            }
                                }
							}
						}
                    }

                    flush_to_nand(EVT_WHCRC_REG_ECCT);
                    in_ppu_check = false;
                    #endif
                }
				//idx++;
			//}
		}

	}

    if(ncl_req->req.pad_sidx == PDA_META_IN_GRP){
        ncl_req->req.pad_sidx = 0;
    }else{
    	if ((ncl_req->req.op_type.b.host == 1) && (ncl_req->req.padding_cnt || ncl_req->req.wunc_cnt)) {
    		/* release host write padding resource*/
            if (!ncl_req->req.op_type.b.dummy) {
                u32 set = test_and_clear_bit(ncl_req->req.pad_sidx, &pad_meta_bmp);
                sys_assert(set == 1);
                //schl_apl_trace(LOG_DEBUG, 0, "mgr %x rls pad meta for %x", mgr, ncl_req);
            }
    	}
    }
#ifndef DUAL_BE
	ncl_req->cmpl(ncl_req);
#else
	if (ncl_req->req.op_type.b.remote)
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_WR_ERA_DONE, 0, (u32)ncl_req);
	else{
        #if PLP_DEBUG
         if(plp_trigger){
           if(ucache_flush_flag){
               schl_apl_trace(LOG_PLP, 0x5b25, "ucache flush done req 0x%x count %d ",ncl_req,fcmd_outstanding_cnt);
           }
         }
         #endif
		ncl_req->req.cmpl(ncl_req);
	}
#endif

	put_ncl_cmd(mgr, ncl_cmd);
	mgr->wr_cmd_otf_cnt--;
	mgr->otf_cmd_cnt--;
	if (mgr->total_rd_cnt != 0|| mgr->total_wr_era_cnt != 0)
		evt_set_cs(die_isu_evt, 0, 0, CS_TASK);
}

/*!
 * @brief handle function for ncl erase done
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
fast_code void erase_issue_done(struct ncl_cmd_t *ncl_cmd)
{
	ncl_w_req_t *ncl_req = ncl_cmd->caller_priv;
    die_mgr_t *mgr;
    u16 die_id;

    die_id = CHANGE_DIENUM_G2L(ncl_req->req.die_id);
    mgr = &_die_mgr[die_id];

	if (ncl_cmd->status) {
		//inc_era_err_cnt();
		//panic("todo");

		// ISU, SPBErFalHdl
		ncl_req->req.op_type.b.erase_err = true;
	}

#ifndef DUAL_BE
	ncl_req->cmpl(ncl_req);
#else
	if (ncl_req->req.op_type.b.remote)
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_WR_ERA_DONE, 0, (u32)ncl_req);
	else
		ncl_req->req.cmpl(ncl_req);
#endif

	put_ncl_cmd(mgr, ncl_cmd);
	mgr->wr_cmd_otf_cnt--;
	mgr->otf_cmd_cnt--;
	if (mgr->total_rd_cnt != 0 || mgr->total_wr_era_cnt != 0)
		evt_set_cs(die_isu_evt, 0, 0, CS_TASK);
}

/*!
 * @brief issue on ncl read command of the die queue
 *
 * @praam die_que	pointer to die queue
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
 #if OPEN_ROW == 1
fast_code bool read_issue(die_que_t *die_que, struct ncl_cmd_t *ncl_cmd)
{
	die_cmd_info_t *die_cmd_info = ncl_cmd->caller_priv;
	//u32 btag;
	u32 cnt;
	u16 rptr        = die_que->rptr;
	u16 end         = die_que->wptr;
    struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
	//u8 mp_mask      = (DU_CNT_PER_PAGE * shr_nand_info.geo.nr_planes) - 1;
	u8 sp_mask      = DU_CNT_PER_PAGE - 1;
	bool round      = false;
	//bool host       = die_que->info[rptr].b.host;
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
	bool slc        = die_que->info[rptr].b.slc;
#endif
	bool gc         = die_que->info[rptr].b.gc;
    bool stream     = die_que->info[rptr].b.stream;
	bm_pl_t *bm_pl  = &die_que->bm_pl[rptr];
	pda_t pda      = 0;
	//btag            = bm_pl->pl.btag;
    pda_t PreviousPda;

	sys_assert(die_cmd_info);

	die_cmd_info->die_queue    = die_que;
	die_cmd_info->cmd_info.all = die_que->info[rptr].all;

    #if NCL_FW_RETRY
	ncl_cmd->retry_cnt = 0;
	ncl_cmd->retry_step = default_read;
    #endif

	ncl_cmd->status            = 0;
	ncl_cmd->op_code           = NCL_CMD_OP_READ;
	ncl_cmd->du_format_no      = host_du_fmt;
	ncl_cmd->completion        = read_issue_done;
	ncl_cmd->flags             = NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
	ncl_cmd->op_type           = NCL_CMD_FW_DATA_READ_STREAMING;

	#if RAID_SUPPORT_UECC
	ncl_cmd->uecc_type = NCL_UECC_NORMAL_RD;
	#endif

	//if (host == 0)
	//{
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
		if (slc)
		{
			ncl_cmd->flags = NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
		}
#endif

	//}
	//else
	//{
		if (!stream)
		{
			extern volatile u8 bFail_Blk_Cnt;

            ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
			if (gc)
			{
			    if( bFail_Blk_Cnt > 0)
			        ncl_cmd->flags |= NCL_CMD_DIS_ARD_FLAG;
			}
		}
		 //ncl_cmd->op_type = NCL_CMD_FW_DATA_PREREAD_DA_DTAG;
		//panic("todo");
	//}

	ncl_cmd->user_tag_list = bm_pl;
	ncl_cmd->addr_param.common_param.pda_list = &die_que->pda[rptr];
	pda = die_que->pda[rptr];

	cnt = 0;
	//memset(info, 0, MAX_RD_SZ * sizeof(*info));
	do {
		    // todo: mp aligened
		    // now we just issue 16 PDA here
		if (cnt > 0)
		{
			//if (die_que->info[rptr].b.host == host)
			//{
			    if (die_que->info[rptr].b.stream != stream)
                {
                    break;
                }

                //if(!gc)
                //{
    			//	if (die_que->bm_pl[rptr].pl.btag != btag)
    			//	{
    			//		break;
    			//	}
                //}



				//if (die_que->info[rptr].b.gc != gc)
				//{
				//	break;
				//}
				//because host and gc have queue seperate
			//}
			//else
			//{
			//	if (die_que->info[rptr].b.slc != slc)
			//	{
			//		break;
			//	}
			//}
		}

		if (cnt >= MAX_RD_SZ)
		{
			break;
		}
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
		if (slc)
			info[cnt].pb_type = NAL_PB_TYPE_SLC;
		else
#endif
			info[cnt].pb_type = NAL_PB_TYPE_XLC;
		//}

		cnt++;

		if (round) {
			die_que->info[DIE_QUE_SZ + rptr] = die_que->info[rptr];
			die_que->bm_pl[DIE_QUE_SZ + rptr] = die_que->bm_pl[rptr];
			die_que->pda[DIE_QUE_SZ + rptr] = pda;
		}

        //if(gc)
        //{
            PreviousPda = pda;
        //}

		if (++rptr == DIE_QUE_SZ) {
			rptr = 0;
			round = true;
		}

		pda = die_que->pda[rptr];

		if((pda & sp_mask) == 0)
		{
			break;
		}

		//if ((pda & mp_mask) == 0)
		//	break;

        //if(gc)
        //{
            if (pda != (PreviousPda + 1))
			    break;
        //}

	} while (rptr != end);

	sys_assert(cnt != 0);

	die_cmd_info->sn = die_que->issue_sn;
	die_que->issue_sn++;

	ncl_cmd->addr_param.common_param.list_len = cnt;
    _die_mgr[die_que->die_id].total_rd_cnt -= cnt;
	//die_que->busy_cnt -= cnt;

    #ifdef NCL_HAVE_reARD
    ncl_cmd->re_ard_flag = false;  //tony 20201021
    #endif

    //if (slc == 0)
	//{
	//	if ((host == 1) && (gc == 0))
	//	{
			if (cnt == 1)
			{
				ncl_cmd->op_code = NCL_CMD_OP_READ_STREAMING_FAST;
			}
	//	}
	//}


	ncl_cmd_submit(ncl_cmd);
	die_que->rptr = rptr;
	if (die_que->rptr >= DIE_QUE_SZ)
		die_que->rptr = 0;

    return true;

}
#else
fast_code bool read_issue(die_que_t *die_que, struct ncl_cmd_t *ncl_cmd)
{
	die_cmd_info_t *die_cmd_info = ncl_cmd->caller_priv;
	u32 btag;
	u32 cnt;
	u16 rptr        = die_que->rptr;
	u16 end         = die_que->wptr;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
	bool round      = false;
	//bool host       = die_que->info[rptr].b.host;
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
	bool slc        = die_que->info[rptr].b.slc;
#endif
	bool gc         = die_que->info[rptr].b.gc;
	bool stream     = die_que->info[rptr].b.stream;
	#ifdef CO_SUPPORT_READ_REORDER
	bool single     = die_que->info[rptr].b.single;
	#endif
	bm_pl_t *bm_pl  = &die_que->bm_pl[rptr];
	pda_t *pda      = &die_que->pda[rptr];
	#ifdef CO_SUPPORT_READ_REORDER
	u32 die         = pda2die(*pda);
	#endif
	btag            = bm_pl->pl.btag;
    u32 startpage  = (die_que->pda[rptr] >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask;  // >> 10
    //u32 mp_mask    = (DU_CNT_PER_PAGE * shr_nand_info.geo.nr_planes) - 1;
    pda_t PreviousPda;
    //bool plane1 = false;
    //bool plane2 = false;
    //bool plane3 = false;
	u8 plane;
	#ifdef CO_SUPPORT_READ_REORDER
	bool ret = false;
	#endif
	sys_assert(die_cmd_info);

	die_cmd_info->die_queue    = die_que;
	die_cmd_info->cmd_info.all = die_que->info[rptr].all;

    #if NCL_FW_RETRY
	ncl_cmd->retry_cnt = 0;
	ncl_cmd->retry_step = default_read;
    #endif

	ncl_cmd->status            = 0;
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
	ncl_cmd->op_code           = NCL_CMD_OP_READ;
#else
	//ncl_cmd->op_code           = NCL_CMD_OP_READ;
#endif
	ncl_cmd->du_format_no      = host_du_fmt;
	ncl_cmd->completion        = read_issue_done;
	ncl_cmd->flags             = NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;

    if (stream)
    {
        ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_STREAMING;
#if (CPU_ID == 2)
		extern u32 cpu2_streaming_read_cnt;
		cpu2_streaming_read_cnt++;
#elif (CPU_ID == 4)
		extern u32 cpu4_streaming_read_cnt;
		cpu4_streaming_read_cnt++;
#endif
    }else
    {
        ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
    }

	#if RAID_SUPPORT_UECC
	ncl_cmd->uecc_type = NCL_UECC_NORMAL_RD;
	#endif
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
	if (slc)
	{
		ncl_cmd->flags = NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
	}
#endif
	//if (host == 0)
	//{
	//	if (slc)
	//	{
	//		ncl_cmd->flags = NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
	//	}
	//}
	//else
	//{
	//	if (!stream)
	//	{
	//		extern volatile u8 bFail_Blk_Cnt;

			//ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
	//		if (gc)
	//		{
	//		    if( bFail_Blk_Cnt > 0)
	//		        ncl_cmd->flags |= NCL_CMD_DIS_ARD_FLAG;
	//		}
	//	}
		 //ncl_cmd->op_type = NCL_CMD_FW_DATA_PREREAD_DA_DTAG;
		//panic("todo");
	//}

	ncl_cmd->user_tag_list = bm_pl;
	ncl_cmd->addr_param.common_param.pda_list = pda;
    if(gc)
    {
      /*if ((die_que->pda[rptr] & mp_mask) >= DU_CNT_PER_PAGE * 3)  // start pda at p3(4P)
        plane3 = true;
      else if ((die_que->pda[rptr] & mp_mask) >= DU_CNT_PER_PAGE * 2)  // start pda at p2(4P)
    	  plane2 = true;
    	else if ((die_que->pda[rptr] & mp_mask) >= DU_CNT_PER_PAGE)  // start pda at p1
    		plane1 = true;*/
		plane = die_que->info[rptr].b.pl;

	    extern volatile u8 bFail_Blk_Cnt;
	    if(bFail_Blk_Cnt > 0)
	        ncl_cmd->flags |= NCL_CMD_DIS_ARD_FLAG;
    }

	cnt = 0;
	//memset(info, 0, MAX_RD_SZ * sizeof(*info));
	do {
			// todo: mp aligened
			// now we just issue 16 PDA here
		if (!gc && cnt > 0)
		{
			//if (die_que->info[rptr].b.host == host)
			//{
				if (die_que->info[rptr].b.stream != stream)
				{
					break;
				}
				//if(!gc)
				//{
					if (die_que->bm_pl[rptr].pl.btag != btag)
					{
						#ifdef CO_SUPPORT_READ_REORDER
						//if ((die_que->info[rptr].b.single == true) && (single == true))
						if (1)
						{
							u32 count_bond = 0;

							if ((die_que->info[rptr].b.single == true) && (single == true))
							{
								count_bond = shr_nand_info.geo.nr_planes;
							}
							else
							{
								if (shr_nand_info.lun_num == 16) // for xfusion test condition, tmp workaround
								{
									count_bond = MAX_RD_SZ;
								}
								else
								{
									count_bond = 0;
								}
							}

							if (cnt < count_bond)
							{
								for (u32 idx = 0; idx < cnt; idx++)
								{
									if (gPlaneBuffer[idx].pl == die_que->info[rptr].b.pl)
									{
										ret = true;
										break;
									}
								}

								if (ret == true)
								{
									for (u32 idx = 0; idx < cnt; idx++)
									{
										rd_cnt_inc(nal_pda_get_block_id(gPlaneBuffer[idx].pda), die, 1);  // cnt 1~4 : +1, 5~8 :+2...
									}
									break;
								}
								else
								{
									btag = die_que->bm_pl[rptr].pl.btag;
								}
							}
							else
							{
								if (shr_nand_info.lun_num == 16) // for xfusion test condition, tmp workaround
								{
									btag = die_que->bm_pl[rptr].pl.btag;
								}
								break;
							}
						}
						else
						#endif
						{
							break;
						}
					}
				//}

				//if (die_que->info[rptr].b.gc != gc)
				//{
				//	break;
				//}
				//because host and gc have queue seperate
			//}
			//else
			//{
			//	if (die_que->info[rptr].b.slc != slc)
			//	{
			//		break;
			//	}
			//}
		}

		if (cnt >= MAX_RD_SZ)
		{
			break;
		}
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
		if (slc)
			info[cnt].pb_type = NAL_PB_TYPE_SLC;
		else
#endif
			info[cnt].pb_type = NAL_PB_TYPE_XLC;
		#ifdef CO_SUPPORT_READ_REORDER
		if(!gc)
		{
			gPlaneBuffer[cnt].pl = die_que->info[rptr].b.pl;
			gPlaneBuffer[cnt].pda = die_que->pda[rptr];
		}
		#endif

		cnt++;

		if (round) {
			die_que->info[DIE_QUE_SZ + rptr] = die_que->info[rptr];
			die_que->bm_pl[DIE_QUE_SZ + rptr] = die_que->bm_pl[rptr];
			die_que->pda[DIE_QUE_SZ + rptr] = die_que->pda[rptr];
		}

        if(gc)
        {
            PreviousPda = die_que->pda[rptr];
        }

		if (++rptr == DIE_QUE_SZ) {
			rptr = 0;
			round = true;
		}


      if(gc)
      {
        /*if (die_que->pda[rptr] != (PreviousPda + 1)){
          if (plane1 == false){  // only allow p0 to p1 max 2 plane
		        if ((die_que->pda[rptr] & mp_mask) >= 0 && startpage != ((die_que->pda[rptr] >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask))  // next in plane 0
			        break;
          }
          else{  // only allow p1 to p0 max 2 plane
			      if ((die_que->pda[rptr] & mp_mask) >= DU_CNT_PER_PAGE && startpage != ((die_que->pda[rptr] >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask))
			  	    break;
          }
        }*/
        // pda not continue and different page
        if (die_que->pda[rptr] != (PreviousPda + 1) && startpage != ((die_que->pda[rptr] >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask)){
        	/*if (plane1 == true){ // start from p1
        		if ((die_que->pda[rptr] & mp_mask) >= DU_CNT_PER_PAGE)  // next in plane 1
			        break;
        	}
        	else if (plane2 == true){
        		if ((die_que->pda[rptr] & mp_mask) >= DU_CNT_PER_PAGE * 2)  // next in plane 2
			        break;
        	}
        	else if (plane3 == true){
        		if ((die_que->pda[rptr] & mp_mask) >= DU_CNT_PER_PAGE * 3)  // next in plane 3
			        break;
        	}
        	else{  // start from p0
        		//if (die_que->pda[rptr] & mp_mask >= 0)  // next in plane 0
			        break;
        	}*/
			if(plane <= die_que->info[rptr].b.pl)
				break;
        }
      }
      /*else{
      	if ((die_que->pda[rptr] & mp_mask) == 0)  // next in plane 0  SR 5057MB/s
			    break;
      }*/

	} while (rptr != end);

	sys_assert(cnt != 0);

	die_cmd_info->sn = die_que->issue_sn;
	die_que->issue_sn++;

	ncl_cmd->addr_param.common_param.list_len = cnt;
	_die_mgr[die_que->die_id].total_rd_cnt -= cnt;
	//die_que->busy_cnt -= cnt;

    #ifdef NCL_HAVE_reARD
    ncl_cmd->re_ard_flag = false;  //tony 20201021
    #endif

	//if(slc)
	//	ncl_cmd->op_code = NCL_CMD_OP_READ_STREAMING_FAST;
#if ((PLP_SLC_BUFFER_ENABLE  == mENABLE) || (defined(TCG_NAND_BACKUP)))
	if (slc == 0)
#endif
		ncl_cmd->op_code = (cnt == 1) ? NCL_CMD_OP_READ_STREAMING_FAST : NCL_CMD_OP_READ;
	//{
	//	{
  if (!gc){
  	//schl_apl_trace(LOG_ALW, 0, "pda %x spb %d die %d", *pda, nal_pda_get_block_id(*pda), pda2die(*pda));
		#ifdef CO_SUPPORT_READ_REORDER
		if (ret == false)
		#endif
		{
			rd_cnt_inc(nal_pda_get_block_id(*pda), pda2die(*pda), (cnt + (DU_CNT_PER_PAGE - 1)) / DU_CNT_PER_PAGE);  // cnt 1~4 : +1, 5~8 :+2...
		}
	}
	//	}
	//}

	ncl_cmd_submit(ncl_cmd);
	if(rptr >= DIE_QUE_SZ)    //willis
		rptr = 0;
	die_que->rptr = rptr;

	return true;

}
#endif

slow_code void decrease_otf_cnt (u8 die_id)
{
	die_mgr_t *mgr = &_die_mgr[die_id];
    mgr->otf_cmd_cnt --;
}

slow_code void read_done_rc_entry(struct ncl_cmd_t *ncl_cmd)
{
    schl_apl_trace(LOG_INFO, 0x9cde, "to do raid recovery");
    rc_req_t* rc_req = rc_req_prepare(ncl_cmd, false);
    raid_correct_push(rc_req);
}

/*!
 * @brief set padding meta of the ncl request
 *
 * @praam ncl_req	pointer to ncl request
 * @param sidx		starting meta index
 *
 * @return		not used
 */
share_data u32 WUNC_DTAG;
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
extern volatile spb_id_t plp_spb_flush_p2l;
#endif
fast_code u8 set_pad_meta(ncl_w_req_t *ncl_req, u32 sidx, u32 type)
{
	bm_pl_t *bm_pl = ncl_req->w.pl;
#ifndef LJ_Meta
	pda_t *pda = ncl_req->w.pda;
#endif
	lda_t *lda = ncl_req->w.lda;
	io_meta_t *meta,*meta_dtag;
	u32 ttl_du_cnt = ncl_req->req.cnt << DU_CNT_SHIFT;
    u8 WUNC_bis = 0;
    #ifdef HMETA_SIZE
    u8 mp_cnt = get_mp_cnt(ncl_req->w.pda,ttl_du_cnt);
    #endif
#ifdef NS_MANAGE
    u64 ns_lba_temp=0;
    u64 lda_secid1=0;
    u64 secorder=0;
#endif

	meta = (io_meta_t *)shr_idx_meta[type];

    u32 meta_page_sn_L = INV_U32;
	u32 i;
	for (i = 0; i < ttl_du_cnt; i++) {
        if((i%DU_CNT_PER_PAGE) == 0){
        	meta[sidx + i].fmt1.page_sn_L = 0;
        	meta[sidx + i + 1].fmt2.page_sn_H = 0;
            if(ncl_req->req.nsid == HOST_NS_ID && (lda[i] != INV_LDA) && (bm_pl[i].pl.dtag != EVTAG_ID)){
                dtag_t dtag;
                if (bm_pl[i].pl.type_ctrl == BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM) {
                    dtag.dtag = (bm_pl[i].pl.dtag & SEMI_SDTAG_MASK);
                } else {
                    dtag.dtag = bm_pl[i].pl.dtag;
                }
                meta_dtag = (io_meta_t *)(dtag.b.in_ddr ? shr_ddtag_meta : shr_dtag_meta);
                meta_page_sn_L = meta_dtag[dtag.b.dtag].fmt1.page_sn_L;
            }else{
                meta_page_sn_L = INV_U32;
            }
        }

		if ((lda[i] != INV_LDA) && (bm_pl[i].pl.dtag != EVTAG_ID))
			continue;
#ifndef LJ_Meta
		pda_t p = pda[i >> DU_CNT_SHIFT] + (i & (DU_CNT_PER_PAGE - 1));
#endif

#if CPU_ID == CPU_BE_LITE
		//replace it with BE_LITE's dummy meta idx
		bm_pl[i].pl.nvm_cmd_id = wr_dummy_meta_idx;
#endif

		sys_assert(bm_pl[i].pl.type_ctrl & META_SRAM_IDX);
		sys_assert(bm_pl[i].pl.nvm_cmd_id == wr_dummy_meta_idx);

        if(type == DDR_IDX_META){
            bm_pl[i].pl.type_ctrl &= ~META_DDR_MASK;
            bm_pl[i].pl.type_ctrl |= META_DDR_IDX;
        }

        bm_pl[i].pl.nvm_cmd_id = sidx + i;
#ifndef LJ_Meta
		meta[sidx + i].seed_index = meta_seed_setup(p);
#endif
		if(lda[i] == INV_LDA || lda[i] == PBT_LDA || lda[i] == QBT_LDA)
		{
		#if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)
			if(plp_spb_flush_p2l)
				meta[sidx + i].lda = DUMMY_PLP_LDA;
			else
		#endif
				meta[sidx + i].lda = DUMMY_LDA;
		}
		else{
			meta[sidx + i].lda = lda[i];
		}
		//meta[sidx + i].hlba = LDA_TO_LBA(lda[i]);
		meta[sidx + i].hlba = (((u64)lda[i]) << (LDA_SIZE_SHIFT - host_sec_bitz));
        if(((i%DU_CNT_PER_PAGE) == 1) && meta_page_sn_L != INV_U32){
            u32 page_sn_L = (shr_page_sn&0xFFFFFF);
            u32 page_sn_H = ((shr_page_sn>>24)&0xFFFFFF);
            if(page_sn_L < meta_page_sn_L){
                schl_apl_trace(LOG_INFO, 0x3a19, "page_sn_L 0x%x meta_page_sn_L 0x%x page_sn_H 0x%x",
                    page_sn_L, meta_page_sn_L, page_sn_H);
                page_sn_H --;
            }
            meta[sidx + i].fmt2.page_sn_H = page_sn_H;
        }

		if (bm_pl[i].pl.dtag == EVTAG_ID) {
#ifdef LJ1_WUNC
			meta[sidx + i].wunc.WUNC = 0xFF;
			if (lda[i] == wunc_ua[0]){
				meta[sidx + i].wunc.WUNC = wunc_bmp[0];
			}
			if (lda[i] == wunc_ua[1]){
				meta[sidx + i].wunc.WUNC = wunc_bmp[1];
			}
#endif
            #ifdef HMETA_SIZE
			bm_pl[i].pl.dtag = WUNC_DTAG|DTAG_IN_DDR_MASK;
            WUNC_bis |= BIT(i/(mp_cnt*DU_CNT_PER_PAGE));
            //schl_apl_trace(LOG_INFO, 0, "LDA:0x%x,PDA:0x%x",lda[i],(ncl_req->w.pda[i >> DU_CNT_SHIFT]+ (i & (DU_CNT_PER_PAGE - 1))));
            #else
            bm_pl[i].pl.dtag = WVTAG_ID;
            #endif

#ifdef NS_MANAGE
            ns_lba_temp = lda[i];
            if(!full_1ns){
        		if(host_sec_bitz==9){
        			ns_lba_temp=ns_lba_temp<<3;
        			lda_secid1=(ns_lba_temp)>>NS_SIZE_GRANULARITY1_BITOP1;
        			secorder=sec_order_p[lda_secid1];
        			meta[sidx + i].hlba=(ns_lba_temp-(lda_secid1<<NS_SIZE_GRANULARITY1_BITOP1))+(secorder<<NS_SIZE_GRANULARITY1_BITOP1);
        		}else{
        			lda_secid1=(ns_lba_temp)>>NS_SIZE_GRANULARITY1_BITOP2;
        			secorder=sec_order_p[lda_secid1];
        			meta[sidx + i].hlba=(ns_lba_temp-(lda_secid1<<NS_SIZE_GRANULARITY1_BITOP2))+(secorder<<NS_SIZE_GRANULARITY1_BITOP2);
        		}
        	}else{
        		meta[sidx + i].hlba = (host_sec_bitz==9) ? (ns_lba_temp<<3) : ns_lba_temp;
            }
#else
            meta[sidx + i].hlba = LDA_TO_LBA(lda[i]);
#endif

		}
        else {
            meta[sidx + i].wunc.WUNC = 0;
        }
	}
    if(WUNC_bis){
        schl_apl_trace(LOG_INFO, 0x443b, "EVTAG set WUNC:0x%x,ncl_req:0x%x",WUNC_bis,ncl_req);
    }
    return WUNC_bis;
}

#ifdef DUAL_BE // there are two functions entity, please unify them
fast_code void set_ncl_cmd_raid_info(ncl_w_req_t *req, struct ncl_cmd_t *ncl_cmd)
{
	u32 i, j, k = 0;
	u32 plane_num;
	u32 mp_page_cnt;
	struct info_param_t *info;

	plane_num = nand_plane_num();
	info = ncl_cmd->addr_param.common_param.info_list;
	mp_page_cnt = req->req.cnt / req->req.tpage;

	for (i = 0; i < req->req.tpage; i++) {
		for (j = 0; j < mp_page_cnt; j++) {
			info[k].raid_id = req->w.raid_id[i][j % plane_num].raid_id;
			info[k].bank_id = req->w.raid_id[i][j % plane_num].bank_id;
			k++;
		}
	}

	if (req->req.op_type.b.xor)
		ncl_cmd->flags |= NCL_CMD_RAID_XOR_FLAG_SET;
	else if (req->req.op_type.b.pout)
		ncl_cmd->flags |= NCL_CMD_RAID_POUT_FLAG_SET;
}

fast_code void set_ncl_cmd_mix_info(ncl_req_t *req, struct ncl_cmd_t *ncl_cmd)
{
	u32 page;
	u32 plane_num = nand_plane_num();

    #ifdef SKIP_MODE
         if (req->op_type.b.pln_write)
            plane_num = req->op_type.b.pln_write;
    #endif
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

	//only program one page, the data must be all P2L
	if (req->tpage == 1 || req->op_type.b.p2l_pad) {
#if defined(HMETA_SIZE)
		ncl_cmd->op_type = NCL_CMD_PROGRAM_DATA;
#else
        ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;
#endif
		return;
	}

	ncl_cmd->flags |= NCL_CMD_HOST_INTERNAL_MIX;
	for (page = 0; page < req->tpage; page++) {
		u8 index = page * plane_num;
		u8 op_type = NCL_CMD_PROGRAM_HOST;
		if ((page == req->p2l_page) || (req->op_type.b.dummy)
            || (page == req->pgsn_page) || (req->blklist_pg[page] != 0xFF)) {
#if defined(HMETA_SIZE)
            op_type = NCL_CMD_PROGRAM_DATA;
#else
            op_type = NCL_CMD_PROGRAM_TABLE;
#endif
		}
        if (plane_num == 1) {
            info[index + 0].op_type = op_type;
        } else if (plane_num == 2) {
			info[index + 0].op_type = op_type;
			info[index + 1].op_type = op_type;
		} else if (plane_num == 3) {
			info[index + 0].op_type = op_type;
			info[index + 1].op_type = op_type;
			info[index + 2].op_type = op_type;
		} else if (plane_num == 4) {
			info[index + 0].op_type = op_type;
			info[index + 1].op_type = op_type;
			info[index + 2].op_type = op_type;
			info[index + 3].op_type = op_type;
		}
        if(req->op_type.b.parity_mix){// plane 0 is parity, disable hcrc
#if defined(HMETA_SIZE)
            info[index + 0].op_type = NCL_CMD_PROGRAM_DATA;
#else
            info[index + 0].op_type = NCL_CMD_PROGRAM_TABLE;
#endif
        }
	}
}
#endif

fast_code void ncl_cmd_submit_insert_schedule(struct ncl_cmd_t *ncl_cmd, bool direct_handle)
{
    die_mgr_t *mgr;
    u8 die_id = 0;


    if(direct_handle)
    {
        ncl_cmd_submit(ncl_cmd);
    }
    else
    {
        die_id = CHANGE_DIENUM_G2L(ncl_cmd->die_id);
        sys_assert(die_id < MAX_DIE_MGR_CNT);
        ncl_cmd->die_id = die_id;
		#if OPEN_ROW == 1
		ncl_cmd->via_sch = VIA_SCH_TAG;
		#endif
        mgr = &_die_mgr[die_id];
        if(ncl_cmd->op_code >= NCL_CMD_OP_WRITE)
        {
            QSIMPLEQ_INSERT_TAIL(&mgr->other_wr_era_ncl, ncl_cmd, entry);
            mgr->other_wr_era_ncl_num++;
            mgr->total_wr_era_cnt++;
        }
        else
        {
            QSIMPLEQ_INSERT_TAIL(&mgr->other_rd_ncl, ncl_cmd, entry);
            mgr->other_rd_ncl_num++;
            mgr->total_rd_cnt++;
        }
        set_bit(die_id, _die_mgr_busy);

        evt_set_cs(die_isu_evt, 0, 0, CS_TASK);

    }
}

/*!
 * @brief issue write/erase command of the die manager
 *
 * @param mgr		pointer to the die manager
 * @praam ncl_cmd	pointer to ncl command
 *
 * @return		true if any command issued
 */
share_data_zi volatile bool stop_host_ncl;

#if PLP_TEST
extern ncl_w_req_t *req_debug;
extern u16 die;
fast_data_zi bool ucache_print_flag = 0;
fast_data_zi bool dummy_print_flag = 0;

extern struct ncl_cmd_t* ncl_cmd_ptr[DTAG_PTR_COUNT];
//extern u32 fill_done_cnt,fill_req_cnt;
#endif
fast_code bool wr_era_issue(die_mgr_t *mgr, struct ncl_cmd_t *ncl_cmd)
{
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
	ncl_req_t *ncl_req;
    u8 WUNC_bits = 0;
	ncl_req = QSIMPLEQ_FIRST(&mgr->wr_era);
    #if PLP_DEBUG
	/*if(plp_trigger && ucache_flush_flag){
        schl_apl_trace(LOG_PLP, 0, "ucache flush, outstanding count %d ncl cmd 0x%x",fcmd_outstanding_cnt, ncl_cmd);

    }*/
    u32 read_count = 0,write_count = 0;
    if(plp_trigger && fcmd_outstanding_cnt && (!ucache_print_flag || !dummy_print_flag) && (ucache_flush_flag || fill_dummy_flag)){
        for(int i = 0;i < DTAG_PTR_COUNT;i++){
            if(ncl_cmd_ptr[i] != NULL){
                if(ncl_cmd_ptr[i]->op_code == 1 || ncl_cmd_ptr[i]->op_code == 0)
                    read_count++;
                if(ncl_cmd_ptr[i]->op_code == 14)
                    write_count++;
                /*schl_apl_trace(LOG_DEBUG, 0, "fcmd id %d, ncl cmd addr 0x%x", i, ncl_cmd_ptr[i]);
                schl_apl_trace(LOG_DEBUG, 0, "cpl 0x%x, flags %d, code %d, type %d",\
                    ncl_cmd_ptr[i]->completion, ncl_cmd_ptr[i]->flags, ncl_cmd_ptr[i]->op_code,
                    ncl_cmd_ptr[i]->op_type);      */
            }
        }
        schl_apl_trace(LOG_ERR, 0xaf2d, " fcmd_outstanding_cnt %d read cnt %d write cnt %d",\
             fcmd_outstanding_cnt,read_count,write_count);
        if(ucache_flush_flag)
            ucache_print_flag = 1;
        else if(fill_dummy_flag){
            dummy_print_flag = 1;
            ucache_print_flag = 1;
        }
    }
    #endif
	if(stop_host_ncl&&(ncl_req->op_type.b.host == 1)){
		//schl_apl_trace(LOG_DEBUG, 0, "Host write or erase hit MR mode");
		return false;
	}

    if(ncl_req->pad_sidx != PDA_META_IN_GRP){
        /* for write req with padding, set dummy meta seed*/
        if ((ncl_req->op_type.b.host == 1) && (!ncl_req->op_type.b.erase)
            && ((ncl_req->padding_cnt) || ncl_req->wunc_cnt)) {
            int idx = find_first_zero_bit(&pad_meta_bmp, PDA_META_IN_GRP - 1);
            if (ncl_req->op_type.b.dummy) {
                idx = PDA_META_IN_GRP - 1;
            }
            if ((idx != PDA_META_IN_GRP - 1) || (ncl_req->op_type.b.dummy)) {
                ncl_req->pad_sidx = idx;
                set_bit(idx, &pad_meta_bmp);
                WUNC_bits = set_pad_meta((ncl_w_req_t *)ncl_req, pad_meta_idx[idx], SRAM_IDX_META);
                //schl_apl_trace(LOG_DEBUG, 0, "mrg %x pad meta set for %x", mgr, ncl_req);
            } else {
                //schl_apl_trace(LOG_DEBUG, 0, "mrg %x pad meta pend %x", mgr, ncl_req);
                return false;
            }
        }
    }

	QSIMPLEQ_REMOVE_HEAD(&mgr->wr_era, link);
	mgr->wr_busy_cnt --;
    mgr->total_wr_era_cnt --;
	sys_assert(mgr->wr_busy_cnt >= 0);

    ncl_cmd->dis_hcrc = WUNC_bits|ncl_req->trim_page;

	ncl_cmd->status = 0;
	ncl_cmd->caller_priv = ncl_req;
	ncl_cmd->completion = ncl_req->op_type.b.erase ? erase_issue_done : write_issue_done;

	ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
	if (ncl_req->op_type.b.erase == 0) {
		ncl_w_req_t *wreq = (ncl_w_req_t *) ncl_req;

		ncl_cmd->op_code = NCL_CMD_OP_WRITE;

		ncl_cmd->user_tag_list = wreq->w.pl;
#ifdef SKIP_MODE
		ncl_cmd->addr_param.common_param.pda_list = wreq->w.pda;
#else
		if (wreq->w.pda[0] != INV_PDA) //indicate use the l2p pda to issue ncl cmd
			ncl_cmd->addr_param.common_param.pda_list = wreq->w.pda;
		else
			ncl_cmd->addr_param.common_param.pda_list = wreq->w.l2p_pda;
#endif
        ncl_cmd->du_format_no = ncl_req->op_type.b.host ? host_du_fmt : DU_4K_DEFAULT_MODE;
	} else {
		ncl_cmd->op_code = NCL_CMD_OP_ERASE;
		ncl_cmd->addr_param.common_param.pda_list = ((ncl_e_req_t *)ncl_req)->e.pda;
		ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	}

	if (ncl_req->op_type.b.slc)
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
#if SYNOLOGY_SETTINGS
	else if(gc_wcnt_sw_flag == true)
		ncl_cmd->flags |= (NCL_CMD_GC_PROG_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG);
#endif
	else
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;

	if (ncl_req->op_type.b.host)
		ncl_cmd->op_type = NCL_CMD_PROGRAM_HOST;
	else
		ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;

	memset(info, 0, ncl_req->cnt * sizeof(*info));
	ncl_cmd->addr_param.common_param.list_len = ncl_req->cnt;

#if defined(HMETA_SIZE)
    if (ncl_req->op_type.b.ftl || ncl_req->op_type.b.xor || ncl_req->op_type.b.pout
        || ncl_req->op_type.b.parity_mix) {
        ncl_cmd->du_format_no = host_du_fmt;
		if (ncl_req->op_type.b.host)
			ncl_cmd->op_type = NCL_CMD_PROGRAM_HOST;
		else
	        ncl_cmd->op_type = NCL_CMD_PROGRAM_DATA;
    }
#endif

	if (ncl_req->op_type.b.xor || ncl_req->op_type.b.pout) {
		set_ncl_cmd_raid_info((ncl_w_req_t *)ncl_req, ncl_cmd);
	}

	if (ncl_req->op_type.b.p2l_nrm || ncl_req->op_type.b.p2l_pad ||
	        ncl_req->op_type.b.dummy || ncl_req->op_type.b.blklist || ncl_req->op_type.b.parity_mix)
		set_ncl_cmd_mix_info(ncl_req, ncl_cmd);

        ncl_cmd_submit(ncl_cmd);

	return true;
}

static inline fast_code enum case_select sch_r_pool_select(die_mgr_t *mgr)
{
	if (unlikely(plp_trigger))
	{
		if(mgr->die_que[DIE_Q_RD].wptr != mgr->die_que[DIE_Q_RD].rptr)
		{
			return CS_RD_HOST;
		}
		else if(mgr->die_que[DIE_Q_GC].wptr != mgr->die_que[DIE_Q_GC].rptr)
		{
			return CS_RD_PLP_DONE;
		} else {
			return CS_RD_OTHER;
		}
	}
	else
	{
	    switch(mgr->read_pool_ratio_cnt++)
	    {
	        //read pool ratio host/gc/other = 2:2:1
	        //host read
	        case 0:
	        case 1:
	            if(mgr->die_que[DIE_Q_RD].wptr != mgr->die_que[DIE_Q_RD].rptr)
	            {
	                return CS_RD_HOST;
	            }
	            else if(mgr->die_que[DIE_Q_GC].wptr != mgr->die_que[DIE_Q_GC].rptr)
	            {
	                return CS_RD_GC;
	            }
	            else
	            {
	                return CS_RD_OTHER;
	            }
	            break;
	        //gc read
	        case 2:
	        case 3:
	            if(mgr->die_que[DIE_Q_GC].wptr != mgr->die_que[DIE_Q_GC].rptr)
	            {
	                return CS_RD_GC;
	            }
	            else if(mgr->other_rd_ncl_num)
	            {
	                return CS_RD_OTHER;
	            }
	            else
	            {
	                return CS_RD_HOST;
	            }
	            break;
	        //other read
	        case 4:
				mgr->read_pool_ratio_cnt = 0;
	            if(mgr->other_rd_ncl_num)
	            {
	                return CS_RD_OTHER;
	            }
	            else if(mgr->die_que[DIE_Q_RD].wptr != mgr->die_que[DIE_Q_RD].rptr)
	            {
	                return CS_RD_HOST;
	            }
	            else
	            {
	                return CS_RD_GC;
	            }
	            break;
	    }
	}

    sys_assert(0);
    return false;
}

extern bool ncl_cmd_in;
extern bool in_ficu_isr;
extern volatile bool ts_reset_sts;
share_data u32 er_blk_cnt;
extern volatile u8 fw_update_flag;

//extern u32 shr_hostw_perf;
fast_code bool die_cmd_scheduler(int die_id)
{
    enum case_select case_sel = 0xFF;
	die_mgr_t *mgr = &_die_mgr[die_id];
	if(fw_update_flag)	// stop handle die_cmd when enter MR mode. 2024/6/14 shengbin yang
		return true;

	if (mgr->total_rd_cnt == 0 && mgr->total_wr_era_cnt == 0) {
		clear_bit(die_id, _die_mgr_busy);
		return false;
	}

  /*if ((GC_MODE_CHECK & GC_FORCE) && shr_hostw_perf < 100000) { // < 100K IOPS ==> QD1 report 0308
  //if (gGCInfo.mode.all16) { // < 75K IOPS ==> QD1
    if (mgr->otf_cmd_cnt >= 1)
        return true;
  }
  else*/
  {
  	//if (mgr->otf_cmd_cnt >= ncl_cmd_per_die)
  	if ((mgr->otf_cmd_cnt >= ncl_cmd_per_die) || (mgr->wr_cmd_otf_cnt >= 1))//max_wcmd_per_die  4214MB/s
        return true;
  }
    struct ncl_cmd_t *ncl_cmd;

    if (ncl_cmd_in || in_ficu_isr) {
        return true;
    }
#if(PLP_SUPPORT == 0)
    // In order to prevent the read command from taking too long, the read priority is higher than the write during fake QBT writing.
    if (delay_flush_spor_qbt)
    {
        if (mgr->total_rd_cnt != 0)
    	{   //read
    	    if (mgr->rd_issue_ttl < ts_rd_credit || plp_trigger)//read
    	    {
    	        case_sel = sch_r_pool_select(mgr);
                goto handler;
    	    }
    	}
    }
#endif

    //if((mgr->wr_issue_ttl < ts_wr_credit) && ((mgr->wr_cmd_otf_cnt < max_wcmd_per_die) || (host_read == 0 && gc_read == 0)) && mgr->wr_busy_cnt != 0)
    if(mgr->total_wr_era_cnt!= 0)//write
    {
    	/*
		 *add ts_reset_sts for reset_flow .2023/1/12 shengbin yang
		 *add er_blk_cnt for preformat_erase. 2023/1/17 shengbin yang
		 */
    	if(mgr->wr_issue_ttl < ts_wr_credit || plp_trigger || shr_shutdownflag || ts_reset_sts || er_blk_cnt)
        {
        	//if((mgr->wr_cmd_otf_cnt < max_wcmd_per_die))
            //{
                if(mgr->wr_busy_cnt)
                {
                    case_sel = CS_WR_ERASE;
                }
                else
                {
                    case_sel = CS_WR_ERASE_OTHER;
                }
                goto handler;
            //  }
            //  else if(mgr->total_rd_cnt == 0)  // 142K  (1W1R)
			//  {
            //	  return true;
			//  }
        }
	}


	if (mgr->total_rd_cnt != 0)
	{//read
	    if (mgr->rd_issue_ttl < ts_rd_credit || plp_trigger)//read
	    {
	        case_sel = sch_r_pool_select(mgr);
	    }
		else
		{
			return true;
		}
	}
	else
	{
		return true;
	}

    /*if(plp_trigger){
        die_que_t *dq = &mgr->die_que[case_sel];
        if(dq->info[dq->rptr].b.stream == true){
            if((case_sel == CS_RD_HOST) || (case_sel == CS_RD_GC)){
                //schl_apl_trace(LOG_PLP, 0, "abort read cmd while plp");
                return false;
            }
        }
    }*/
    handler:
    switch(case_sel)
    {
        case CS_RD_HOST:
        case CS_RD_GC:

			if(stop_host_ncl){
				//schl_apl_trace(LOG_DEBUG, 0, "Host read hit MR mode");
				break;
			}

			ncl_cmd = get_ncl_cmd(mgr);

            #ifdef DBG_NCL_SET_FEA_BE4_READ
            set_feature_be4_read_dbg(ncl_cmd, 0x80808080);
            #endif

			if (read_issue(&mgr->die_que[case_sel], ncl_cmd)) {
				mgr->otf_cmd_cnt++;
				mgr->rd_issue_ttl++;
			} else {
				put_ncl_cmd(mgr, ncl_cmd);
			}
            break;

        case CS_RD_OTHER:
			if (!plp_trigger) {
            ncl_cmd = QSIMPLEQ_FIRST(&mgr->other_rd_ncl);
            QSIMPLEQ_REMOVE_HEAD(&mgr->other_rd_ncl, entry);
            mgr->other_rd_ncl_num --;
            mgr->total_rd_cnt --;
            mgr->otf_cmd_cnt++;
            ncl_cmd_submit(ncl_cmd);
			} else {
				while (!QSIMPLEQ_EMPTY(&mgr->other_rd_ncl)) {
		            QSIMPLEQ_REMOVE_HEAD(&mgr->other_rd_ncl, entry);
	            mgr->other_rd_ncl_num --;
	            mgr->total_rd_cnt --;
				}
			}
            break;

        case CS_WR_ERASE:
            ncl_cmd = get_ncl_cmd(mgr);
        	if (wr_era_issue(mgr, ncl_cmd)) {
        		mgr->wr_cmd_otf_cnt++;
        		mgr->otf_cmd_cnt++;
        		mgr->wr_issue_ttl++;
        		return false;
        	} else {
        		put_ncl_cmd(mgr, ncl_cmd);
        		return true;
        	}
            break;

        case CS_WR_ERASE_OTHER:
            ncl_cmd = QSIMPLEQ_FIRST(&mgr->other_wr_era_ncl);
            QSIMPLEQ_REMOVE_HEAD(&mgr->other_wr_era_ncl, entry);
            mgr->other_wr_era_ncl_num --;
            mgr->total_wr_era_cnt --;
            mgr->wr_cmd_otf_cnt++;
        	mgr->otf_cmd_cnt++;
            ncl_cmd_submit(ncl_cmd);
            break;

		case CS_RD_PLP_DONE:
			if(mgr->die_que[DIE_Q_GC].wptr > mgr->die_que[DIE_Q_GC].rptr)
			{
				u32 cnt;
				cnt = mgr->die_que[DIE_Q_GC].wptr - mgr->die_que[DIE_Q_GC].rptr;
				mgr->total_rd_cnt -= cnt;
			}
			else if (mgr->die_que[DIE_Q_GC].wptr < mgr->die_que[DIE_Q_GC].rptr)
			{
			    u32 cnt;
				cnt = DIE_QUE_SZ - mgr->die_que[DIE_Q_GC].rptr + mgr->die_que[DIE_Q_GC].wptr;
				mgr->total_rd_cnt -= cnt;
			}
			mgr->die_que[DIE_Q_GC].wptr = mgr->die_que[DIE_Q_GC].rptr;
            break;

        default:
            sys_assert(0);
            break;

    }

	return true;
}

fast_code void die_isu_handler(u32 param0, u32 param1, u32 param2)
{
	int die_id = 0;
	bool ret = false;

	do {
		die_id = find_next_bit(_die_mgr_busy, MAX_DIE_MGR_CNT, die_id);
		if (die_id != MAX_DIE_MGR_CNT) {
			ret |= die_cmd_scheduler(die_id);
			die_id = die_id + 1;
		}
	} while (die_id != MAX_DIE_MGR_CNT);

	ret |= !!rsv_die_que_trig();

	if (ret)
		evt_set_cs(die_isu_evt, 0, 0, CS_TASK);
}

#if (CPU_ID == 2) || (CPU_ID == 4)
ddr_code void AplDieQueue_DumpInfo(u32 flag)
{
    u32 die_id;
    u32 job;
    schl_apl_trace(LOG_ERR, 0xbbe1, "[FCMD] |%d",fcmd_outstanding_cnt);
    schl_apl_trace(LOG_ERR, 0x995a, "[FICU] NF_FINST =0x%x",readl((const void *)(0xc0001178)));
    schl_apl_trace(LOG_ERR, 0x33e4, "[FICU] DU_DONE  =0x%x",readl((const void *)(0xc0001180)));
    schl_apl_trace(LOG_ERR, 0x1754, "[FICU] DU_START =0x%x",readl((const void *)(0xc0001188)));
    for (die_id = 0; die_id < MAX_DIE_MGR_CNT; die_id++)
    {
        if (_die_mgr[die_id].otf_cmd_cnt != 0)
        {
            schl_apl_trace(LOG_ERR, 0x4394, "[DieQ]: Id|%d Otf|%d ",die_id,_die_mgr[die_id].otf_cmd_cnt);
            die_mgr_t *mgr = &_die_mgr[die_id];
            for (job = 0; job < ncl_cmd_per_die; job++)
            {
                if (_die_mgr[die_id].otf_cmd_bitmap[0] & (1 << job))
                {
                    schl_apl_trace(LOG_ERR, 0xed5f, "[NCL]: OP|%d Type|0x%x ",mgr->ncl_cmd[job].op_code,mgr->ncl_cmd[job].op_type);
                }
            }
        }
    }
}
#endif
