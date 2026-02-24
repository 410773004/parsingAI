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
/*! \file btn_cmd.c
 * @brief rainier BTN command manger
 *
 * \addtogroup btn
 * \defgroup btn_cmd
 * \ingroup btn
 * @{
 *
 */
//=============================================================================
/*
 * Component for BTN_CMD
 * 0. BTN_CORE_CPU will init CMD buffer, misc
 * 1. BTN_RCMD_CPU will fill/handle/release READ cmd
 * 2. BTN_WCMD_CPU will fill/handle/release WRITE cmd
 * 3. BTN_WR_DAT_RLS_CPU will NRM/PAR Data in, WRITE_CQ, it locates with uCache
 * 4. BTN_FREE_WRITE_CPU fills Dtag
 */
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "btn_precomp.h"
#include "nvme_spec.h"
#include "btn_cmd_data_reg.h"
#include "bf_mgr.h"
#include "event.h"
#include "assert.h"
#include "vic_id.h"
#include "misc.h"
#include "rainier_soc.h"
#include "btn.h"
#include "crypto.h"
#include "btn_cmd.h"
#include "btn.h"
#include "cpu_msg.h"
#include "ipc_api.h"
#include "nvme_cfg.h"
#include "ns.h"
#include "spin_lock.h"
#include "fc_export.h"
#include "nvme_precomp.h"
/*! \cond PRIVATE */
#define __FILEID__ bcmd
#include "trace.h"
/*! \endcond */

#define BCMD_TIMEOUT		(5 * HZ)		///< bcmd internal timeout threshold
#define BCMD_TIMEOUT_MS		(BCMD_TIMEOUT * 100)	///< bcmd internal timeout threshold in ms
//#define NS_SIZE_GRANULARITY_CMD_IN	(0x200000000 >> LBA_SIZE_SHIFT)//joe add 20200730 //joe add NS 20200813

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#if defined(BTN_CMD_CPU)
#if (BTN_WCMD_CPU == BTN_DATA_IN_CPU)
#define wcmd_loc	fast_data_ni
#else
#define wcmd_loc	share_data_ni
#endif

#if (BTN_WCMD_CPU == BTN_CORE_CPU)
#define wcnt_loc	fast_data_zi
#else
#define wcnt_loc	share_data_zi
#endif

#if (BTN_RCMD_CPU == BTN_CORE_CPU)
#define rcnt_loc	fast_data_zi
#else
#define rcnt_loc	share_data_zi
#endif

//-----------------------------------------------------------------------------
// btn btag pending list
//-----------------------------------------------------------------------------
wcmd_loc btn_cmd_t _btn_w_cmds[BTN_W_CMD_CNT] __attribute__ ((aligned(32)));	///< btn commands buffer

fast_data LIST_HEAD(_otf_bcmd);		///< on-the-fly btn command list, for timeout check and abort
fast_data LIST_HEAD(_pending_bcmd);	///< pending command list, if disk was not ready, pend all coming commands on this list

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
/* Events exported for external modules */

share_data_ni btn_cmd_ex_t _btn_cmds_ex[BTN_CMD_CNT];				///< extension btn commands buffer

static fast_data_zi btn_cmd_handler_t btn_cmd_handler = NULL;			///< btn command handler
static fast_data_zi btn_fast_cmd_handler_t btn_fast_cmd_handler = NULL;		///< fast btn command handler for fast 4K read

static fast_data_zi u32 btn_rd_cmd_cnt = 0;			///< btn read command count
static fast_data_zi u32 btn_wr_cmd_cnt = 0;			///< btn write command count
static fast_data_zi u32 btn_cp_cmd_cnt = 0;			///< btn compare command count
static fast_data struct timer_list bcmd_timer;			///< monitor bcmd process

rcnt_loc btn_smart_io_t rd_io;					///< read command smart counter
wcnt_loc btn_smart_io_t wr_io;					///< write command smart counter
extern smart_statistics_t *smart_stat;

share_data_zi volatile bool save_nor_flag; //for cmd timeout
#if defined (NS_MANAGE) && (BTN_RCMD_CPU == CPU_ID)
fast_data_zi u32 _btn_otf_cmd_cnt[NVMET_NR_NS] = { 0 };
#endif

share_data_zi bool fgLEDBlink;
share_data_zi u16 fgLEDBlink_cnt;


extern u16 ns_valid_sec;
extern u16 drive_total_sector;
extern u16 ns_order;
//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
extern void __attribute__((weak, alias("__btn_get_r_smart_io"))) btn_get_r_smart_io(volatile btn_smart_io_t *smart_io);
extern void __attribute__((weak, alias("__btn_get_w_smart_io"))) btn_get_w_smart_io(volatile btn_smart_io_t *smart_io);

fast_code void btn_smart_io_mov(volatile btn_smart_io_t *dst, btn_smart_io_t *src)
{
	dst->cmd_recv_cnt = src->cmd_recv_cnt;
	dst->host_du_cnt = src->host_du_cnt;
	dst->running_cmd = src->running_cmd;

	src->cmd_recv_cnt = 0;
	src->host_du_cnt = 0;
}

/*!
 * @brief alias function if btn read command cpu is remote CPU
 *
 * @param smart_io		smart io to be copied
 *
 * @return			not used
 */
fast_code void __btn_get_r_smart_io(btn_smart_io_t *smart_io)
{
#if defined(MPC)
	ipc_api_get_btn_get_smart_io(smart_io, BTN_RCMD_CPU - 1, false);
#else
	panic("imp");
#endif
}

/*!
 * @brief alias function if btn write command cpu is remote CPU
 *
 * @param smart_io		smart io to be copied
 *
 * @return			not used
 */
fast_code void __btn_get_w_smart_io(btn_smart_io_t *smart_io)
{
#if defined(MPC)
	ipc_api_get_btn_get_smart_io(smart_io, BTN_WCMD_CPU - 1, true);
#else
	panic("imp");
#endif
}

/*!
 * @brief increase btn smart io counter for command and access unit
 *
 * @param btn_smart_io	smart io
 * @param nlb		number of lba
 */
extern u16 host_sec_size;//joe add change sec size 20200817
extern u8 host_sec_bitz;//joe add change sec size  20200817
//extern vu32 blink;
fast_code inline void btn_inc_duw(btn_smart_io_t *btn_smart_io, u32 nlb, bool wr)
{
	//u32 duw = nlb << (HLBASZ - 9);  /* Data Unit RW for SMART*///joe change sec size 20200817
	u32 duw = nlb << (host_sec_bitz- 9);
	/*if(wr)
	{
		smart_stat->data_units_written += duw;
		smart_stat->host_write_commands++;
	}
	else
	{
		smart_stat->data_units_read += duw;
		smart_stat->host_read_commands++;
	}*/
	btn_smart_io->host_du_cnt += duw;
	btn_smart_io->cmd_recv_cnt++;
	btn_smart_io->running_cmd++;
}

fast_code bool btn_wr_cmd_idle(void)
{
	if (wr_io.running_cmd)
		return false;

	return true;
}
fast_code bool btn_rd_cmd_idle(void)
{
	if (rd_io.running_cmd)
		return false;

	return true;
}

/*!
 * @brief	btn in idle check funtion
 *
 * @param	None
 *
 * @return	true: success, false: fail
 *
 */
fast_code bool btn_in_idle(void)
{
	//u32 loop_ct = 0;
	//while(loop_ct++ < 2000){
		btn_um_int_sts_t sts = { .all = btn_readl(BTN_MK_INT_STS), };
		if (sts.b.rd_data_group0_update |sts.b.wr_data_group0_update |sts.b.wr_data_group1_update |sts.b.rd_data_group1_update\
			|sts.b.nvm_cmd_que0_has_cmd |sts.b.nvm_cmd_que1_has_cmd|sts.b.nvm_cmd_que2_has_cmd|sts.b.nvm_cmd_que3_has_cmd){
			return false;
		}
	//}
	return true;
}

/*!
 * @brief check if btn command was read/write or not
 *
 * @param bcmd	btn command
 *
 * @return	return true if it was read/write command
 */
fast_code static inline bool is_nvm_rw_cmd(btn_cmd_t *bcmd)
{
	if (bcmd->dw0.b.cmd_type == NVM_WRITE ||
			bcmd->dw0.b.cmd_type == NVM_READ ||
			bcmd->dw0.b.cmd_type == PRI_READ) {
		return true;
	}
	return false;
}

fast_code inline void btn_rst_nvm_cmd_cnt(void)
{
	btn_cmd_ctrl_status_t sts = {.all = btn_readl(BTN_CMD_CTRL_STATUS)};
	sts.b.btn_ncmd_cnt_rst = 1;
	btn_writel(sts.all, BTN_CMD_CTRL_STATUS);
	sts.b.btn_ncmd_cnt_rst = 0;
	btn_writel(sts.all, BTN_CMD_CTRL_STATUS);
}

fast_code inline u32 btn_get_rd_cmd_cnt(void)
{
	u32 count = btn_readl(NVM_R_CMD_RECEIVE_CNT);
	u32 ret = (count > btn_rd_cmd_cnt) ? count - btn_rd_cmd_cnt : (0xFFFFFFFF - count + btn_rd_cmd_cnt + 1);
	btn_rd_cmd_cnt = count;
	return ret;
}

fast_code inline u32 btn_get_wr_cmd_cnt(void)
{
	u32 count = btn_readl(NVM_W_CMD_RECEIVE_CNT);
	u32 ret = (count > btn_wr_cmd_cnt) ? count - btn_wr_cmd_cnt : (0xFFFFFFFF - count + btn_wr_cmd_cnt + 1);
	btn_wr_cmd_cnt = count;
	return ret;
}

fast_code inline u32 btn_get_cp_cmd_cnt(void)
{
	u32 count = btn_readl(NVM_C_CMD_RECEIVE_CNT);
	u32 ret = (count > btn_cp_cmd_cnt) ? count - btn_cp_cmd_cnt : (0xFFFFFFFF - count + btn_cp_cmd_cnt + 1);
	btn_cp_cmd_cnt = count;
	return ret;
}

fast_code void btn_bcmd_push_freeQ(btn_fcmdq_t *fcmdq, btn_cmd_t *cmd, bool wr)
{
	pool_t *p = fcmdq->cmd_pool;

	if (cmd == NULL)
		cmd = pool_get_ex(p);

	if (cmd == NULL) {/* pool busy */
		//bf_mgr_trace(LOG_DEBUG, 0, "pool busy");
		return;
	}

	btn_cmdq_t *cmdq = &fcmdq->cmdq;
	hdr_reg_t *reg = &cmdq->reg;

	/* PERF: we can avoid reg read by using pointer inside TCM */
	u16 rptr = cmdq->wptr;

	u16 wptr_nxt = (reg->entry_pnter.b.wptr + 1) & reg->entry_dbase.b.max_sz;

	if (rptr == wptr_nxt) {
		//bf_mgr_trace(LOG_DEBUG, 0, "Q busy, rptr %d wptr_nxt %d", rptr, wptr_nxt);
		pool_put_ex(p, cmd);
		return;
	}

	u16 *entry = (u16 *)reg->base;
	u16 btag = bcmd2btag(cmd, wr);

	//bf_mgr_trace(LOG_DEBUG, 0, "QueueId %x, rptr %d, wptr %d, btag %d",
			//cmdq, rptr, reg->entry_pnter.b.wptr, btag);

	entry += reg->entry_pnter.b.wptr;
	btag2bcmd_ex(btag)->du_xfer_left = 0;
	btag2bcmd_ex(btag)->flags.all = 0;
	*entry = btag;
	reg->entry_pnter.b.wptr = wptr_nxt;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));

	return;
}

init_code void btn_cmd_hook(btn_cmd_handler_t handler, btn_fast_cmd_handler_t fast_handler)
{
	btn_cmd_handler = handler;
	btn_fast_cmd_handler = fast_handler;

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (!list_empty(&_pending_bcmd)) {
		btn_cmd_ex_t *bcmd_ex = list_first_entry(&_pending_bcmd, btn_cmd_ex_t, entry);
		int btag = bcmd_ex2btag(bcmd_ex);
		btn_cmd_t *bcmd = btag2bcmd(btag);

		list_del(&bcmd_ex->entry);

		list_add_tail(&bcmd_ex->entry, &_otf_bcmd);
		btn_cmd_handler(bcmd, btag);

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}
}
#if(CPU_ID == BTN_CORE_CPU)
#ifdef STOP_BG_GC

#define READ_STOP_GC_TH (1)
#define WRITE_STOP_GC_TH (0)
extern volatile ftl_flags_t shr_ftl_flags;
share_data_zi volatile u8 STOP_BG_GC_flag;
share_data_zi volatile bool in_gc_end;
fast_data_zi struct timer_list STOP_BG_GC_timer;
fast_data_zi u64 HOST_WRITE_CMD_CNT_BAK;
fast_data bool STOP_BG_GC_tick;
extern bool __attribute__((weak)) gc_action(gc_action_t *act);

ddr_code void stop_bg_gc_done(gc_action_t *action)
{
    sys_free(FAST_DATA, action);
    STOP_BG_GC_tick = false;
    del_timer(&STOP_BG_GC_timer);
    bf_mgr_trace(LOG_ALW, 0xb81f, " ");
}

ddr_code void stop_bg_gc_check(void *data)
{
	//bf_mgr_trace(LOG_ALW, 0, "STOP_BG_GC_CHK BAK: 0x%x%x, recv: 0x%x%x, hostwr: 0x%x%x", HOST_WRITE_CMD_CNT_BAK>>32, HOST_WRITE_CMD_CNT_BAK&0xFFFFFFFF,
	//			wr_io.cmd_recv_cnt>>32, wr_io.cmd_recv_cnt&0xFFFFFFFF, smart_stat->host_write_commands>>32, smart_stat->host_write_commands&0xFFFFFFFF);
    if(shr_ftl_flags.b.gcing == false|| (wr_io.running_cmd > WRITE_STOP_GC_TH)
        || (rd_io.running_cmd < READ_STOP_GC_TH) || (HOST_WRITE_CMD_CNT_BAK != (smart_stat->host_write_commands + wr_io.cmd_recv_cnt)) || (rd_gc_flag)){
        STOP_BG_GC_tick = false;
        del_timer(&STOP_BG_GC_timer);
        return;
    }
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));
	sys_assert(action);

	action->act = GC_ACT_STOP_BG_GC;
	action->caller = NULL;
	action->cmpl = stop_bg_gc_done;	//No need  to free memory since gc_action_t is located in registered ddtag
	//STOP_BG_GC_flag = 1;
    bf_mgr_trace(LOG_ALW, 0xc070, " send gc action");
    if(gc_action(action)){
        stop_bg_gc_done(action);
    }
}


init_code void stop_bg_gc_init()
{
	INIT_LIST_HEAD(&STOP_BG_GC_timer.entry);
    STOP_BG_GC_timer.function = stop_bg_gc_check;
    STOP_BG_GC_timer.data = "IO_stop_gc";
    STOP_BG_GC_tick = false;
    STOP_BG_GC_flag = 0;
    in_gc_end = false;
}

fast_code void stop_bd_gc_trigger()
{
    if(shr_ftl_flags.b.gcing && (wr_io.running_cmd <= WRITE_STOP_GC_TH)
        && (rd_io.running_cmd >= READ_STOP_GC_TH) && !STOP_BG_GC_tick && !STOP_BG_GC_flag && !rd_gc_flag)
    {
            HOST_WRITE_CMD_CNT_BAK = (smart_stat->host_write_commands + wr_io.cmd_recv_cnt);
			//bf_mgr_trace(LOG_ALW, 0, "STOP_BG_GC_timer start,wr:%x,rd:%d", wr_io.running_cmd,rd_io.running_cmd);
			//bf_mgr_trace(LOG_ALW, 0, "STOP_BG_GC_timer BAK: 0x%x%x, recv: 0x%x%x, hostwr: 0x%x%x", HOST_WRITE_CMD_CNT_BAK>>32, HOST_WRITE_CMD_CNT_BAK&0xFFFFFFFF,
			//			wr_io.cmd_recv_cnt>>32, wr_io.cmd_recv_cnt&0xFFFFFFFF, smart_stat->host_write_commands>>32, smart_stat->host_write_commands&0xFFFFFFFF);
        	mod_timer(&STOP_BG_GC_timer, jiffies + 3*HZ/10);
            STOP_BG_GC_tick = true;
    }
}
#endif
#endif
/*!
 * @brief dispatch a io btn command
 *
 * @param bcmd		btn command
 * @param btag		command tag of btn command
 *
 * @return		return -1 if it was not a io command, return 0 for dispatched
 */
fast_code int btn_cmd_dispatcher(btn_cmd_t *bcmd, int btag)
{
	btn_cmd_ex_t *cmd_ex;

	if (is_nvm_rw_cmd(bcmd) == false) {
		/* XXX: IO_MGR cmd is in WRITE que */
		if (bcmd->dw0.b.cmd_type == IO_MGR) {
			btn_io_mgr_cmd_t *io_bcmd = (btn_io_mgr_cmd_t *) bcmd;

			if ((io_bcmd->dw0.b.cmd_op_type != NVME_OPC_FLUSH) &&
					(io_bcmd->dw0.b.cmd_op_type != NVME_OPC_DATASET_MANAGEMENT)) {
				bf_mgr_trace(LOG_WARNING, 0xd0d4, "IO_MGR cmd id %d, type %d", io_bcmd->dw2.b.cmd_cid,
						io_bcmd->dw0.b.cmd_op_type);
				return -1;
			}
		} else {
			bf_mgr_trace(LOG_WARNING, 0x1617, "not write or read. bcmd type(0x%x)", bcmd->dw0.b.cmd_type);
			return -1;
		}
	} else {
		/* IPC to CPU for NVMe stat or in data in */
		bcmd->dw3.b.xfer_lba_num += 1;
		bool wr = (bcmd->dw0.b.cmd_type == NVM_WRITE) ? true : false;
		btn_inc_duw((wr ? &wr_io : &rd_io), bcmd->dw3.b.xfer_lba_num, wr);
		fgLEDBlink = true;

        #if (BTN_WCMD_CPU == CPU_ID) && defined(WCMD_DROP_SEMI)
        extern u32 dropsemi;
        u8 numlba=0;
        if(host_sec_bitz == 9){
            numlba = 8;
        }else{
            numlba = 1;
        }
        if(wr&&(wr_io.running_cmd <= 1)&&(bcmd->dw3.b.xfer_lba_num <= numlba)){
            if(dropsemi < 100)
                dropsemi ++;
        }
        else{
            dropsemi = 0;
        }
        #endif
        #if(CPU_ID == BTN_CORE_CPU)
        #ifdef STOP_BG_GC
        if(shr_ftl_flags.b.gcing && !wr){
            stop_bd_gc_trigger();
        }
        #endif
        #endif
#ifdef NS_MANAGE
		// convert host cmd start to namespace based start lba.
		//joe add NS 20200813
		//u64 slba = bcmd_get_slba(bcmd);
		//slba += get_ns_slba(bcmd->dw1.b.ns_id);
		//bcmd->dw2.all = (u32)slba;
		//bcmd->dw3.b.slba_35_32 = (u32)(slba >> 32) & 0xF;
//if(!(ns_order==1&&(drive_total_sector==ns_valid_sec))){
#if (BTN_RCMD_CPU == CPU_ID)
		//bf_mgr_trace(LOG_DEBUG, 0, "++ cnt:%d nsid: %d btag: 0x%x type: %d",
			//_btn_otf_cmd_cnt[bcmd->dw1.b.ns_id - 1], bcmd->dw1.b.ns_id, btag, bcmd->dw0.b.cmd_type);
		_btn_otf_cmd_cnt[bcmd->dw1.b.ns_id - 1] ++;
#endif
//}
#endif
	}

	cmd_ex = btag2bcmd_ex(btag);
	cmd_ex->flags.all = 0;
	cmd_ex->flags.b.bcmd_init = 1;
	cmd_ex->start = jiffies;

	// only pend bcmd if btn cmd handler was not hooked yet
	if (btn_cmd_handler == NULL) {
		cmd_ex->flags.b.pend = 1;
		list_add_tail(&cmd_ex->entry, &_pending_bcmd);
		return 0;
	}

	list_add_tail(&cmd_ex->entry, &_otf_bcmd);

#if defined(RAWDISK) && defined(MPC) // only support in mpc rawdisk
	if ((bcmd->dw0.b.cmd_type == NVM_READ ||
			bcmd->dw0.b.cmd_type == PRI_READ) &&
			bcmd->dw3.b.xfer_lba_num == NR_LBA_PER_LDA &&
			(bcmd_get_slba(bcmd) & NR_LBA_PER_LDA_MASK) == 0) {
		bool ret;

		ret = btn_fast_cmd_handler(bcmd, btag);
		if (ret == true)
			return 0;
	}
#endif

	btn_cmd_handler(bcmd, btag);

	return 0;
}

fast_code void btn_cmd_in(btn_cmdq_t *cmdq, btn_fcmdq_t *fcmdq, bool wr)
{
	hdr_reg_t *reg = &cmdq->reg;

	reg->entry_pnter.b.wptr = cmdq->wptr;

	u16 *entry = (u16 *)reg->base;

	while (reg->entry_pnter.b.wptr != reg->entry_pnter.b.rptr)
	{
		u16 btag = entry[reg->entry_pnter.b.rptr];
		btn_cmd_t *bcmd = btag2bcmd(btag);

/*#if defined(SRIOV_SUPPORT)
		bf_mgr_trace(LOG_DEBUG, 0, "Q(%x) btag(%d) slba 0x%x, pf(%d), vf_num(%d)",
				cmdq, btag, bcmd->dw2.b.slba_31_0,
				bcmd->dw1.b.pf, bcmd->dw0.b.vf_num);
#else
		bf_mgr_trace(LOG_DEBUG, 0, "Q(%x) btag(%d) (slba 0x%x%x nlb %d)",
				cmdq,
				btag,
				bcmd->dw3.b.slba_35_32, bcmd->dw2.b.slba_31_0,
				bcmd->dw3.b.xfer_lba_num + 1);
#endif*/

/*#if CPU_ID_0 == 0
		extern vu32 Rdcmd;
		Rdcmd ++;
#else
		extern vu32 Wrcmd;
		Wrcmd ++;
#endif*/
		//bf_mgr_trace(LOG_INFO, 0, "btn cmd in %x",blink);
		if (btn_cmd_dispatcher(bcmd, btag))
			btn_bcmd_push_freeQ(fcmdq, bcmd, wr);
		else
			btn_bcmd_push_freeQ(fcmdq, NULL, wr);
		reg->entry_pnter.b.rptr = (reg->entry_pnter.b.rptr + 1) & reg->entry_dbase.b.max_sz;
	}
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void btn_rw_cmd_in(void)
{
	extern __attribute__((weak)) void btn_nrd_cmd_in();
	extern __attribute__((weak)) void btn_prd_cmd_in();
	extern __attribute__((weak)) void btn_w_cmd_in();

	if (btn_prd_cmd_in)
		btn_prd_cmd_in();

	if (btn_nrd_cmd_in)
		btn_nrd_cmd_in();

	if (btn_w_cmd_in)
		btn_w_cmd_in();
}

ps_code void btn_cmdq_resume(void)
{
#if (BTN_RCMD_CPU == CPU_ID)
	extern void bcmd_r_cmdq_resume(void);
	bcmd_r_cmdq_resume();

#ifdef NS_MANAGE
	memset(_btn_otf_cmd_cnt, 0 , sizeof(_btn_otf_cmd_cnt));
#endif
#endif

#if (BTN_WCMD_CPU == CPU_ID)
	extern void bcmd_w_cmdq_resume(void);
	bcmd_w_cmdq_resume();
#endif

#if (BTN_WR_DAT_RLS_CPU == CPU_ID)
	extern void bcmd_fw_cmdq_resume(void);
	bcmd_fw_cmdq_resume();
#endif
}

ddr_code void btn_otf_cmd_chk(void *data)
{
	u32 timeout = BCMD_TIMEOUT_MS;
	int max_otf_cmd_cnt = BTN_R_CMD_CNT + BTN_W_CMD_CNT;
	struct list_head *entry;
	u32 panic_timeout = 0; 	//20210326-Eddie
	bool timeout_dump = false;

	panic_timeout = 60000;
	#if (CPU_ID == CPU_BE_LITE) && defined(WCMD_USE_IRQ)
	BEGIN_CS1
	#endif

	list_for_each(entry, &_otf_bcmd) {
		btn_cmd_ex_t *bcmd_ex = container_of(entry, btn_cmd_ex_t, entry);
		u32 t = (jiffies - bcmd_ex->start) * (1000 / HZ);

		if (t > timeout) {
			u32 btag = bcmd_ex2btag(bcmd_ex);
			btn_cmd_t *bcmd = btag2bcmd(btag);

			bf_mgr_trace(LOG_WARNING, 0xa5bd, "btag[%d] op[%d] nvmid[%d] timeout %d > %d",
					btag, bcmd->dw0.b.cmd_type, bcmd->dw0.b.nvm_cmd_id, t, timeout);
			bf_mgr_trace(LOG_WARNING, 0xe281, "slba[0x%x%x] nlb[%d] t[0x%x]",
							bcmd->dw3.b.slba_35_32, bcmd->dw2.b.slba_31_0,
							bcmd->dw3.b.xfer_lba_num,
							bcmd_ex->start);
			timeout_dump = true;
			if (t > panic_timeout && save_nor_flag) {		//20210326-Eddie
				// to avoid endless print
				flush_to_nand(EVT_CMD_TIMEOUT);//for tx	replace panic 22/2/7
				save_nor_flag = false;
			}
		}
		max_otf_cmd_cnt--;
		if (!max_otf_cmd_cnt){
			bf_mgr_trace(LOG_WARNING, 0xa2d3, "btn_otf_cmd_chk link list fail");
			break;
		}
	}
	#if (CPU_ID == CPU_BE_LITE) && defined(WCMD_USE_IRQ)
	END_CS1
	#endif

	if (timeout_dump){
		#if BTN_RCMD_CPU == CPU_ID
		extern void btn_rcmd_timeout_dump(void);
		btn_rcmd_timeout_dump();
		#endif
		#if BTN_WCMD_CPU == CPU_ID
		extern void btn_wcmd_timeout_dump();
		btn_wcmd_timeout_dump();
		#endif
	}
	mod_timer(&bcmd_timer, jiffies + 2*HZ);
}

#ifdef NS_MANAGE
fast_code bool btn_otf_cmd_idle_check(u8 nsid)
{
	return (_btn_otf_cmd_cnt[nsid] != 0 ? 0 : 1);
}
#endif

init_code void btn_cmdq_init(void)
{
#if (BTN_RCMD_CPU == CPU_ID)
	bcmd_r_cmdq_init();	/// only init interrupt and
#endif

#if (BTN_WCMD_CPU == CPU_ID)
	bcmd_w_cmdq_init();
#endif

#if (BTN_WR_DAT_RLS_CPU == CPU_ID)
	bcmd_fw_cmdq_init();
#endif
	memset(&wr_io, 0, sizeof(wr_io));
	memset(&rd_io, 0, sizeof(rd_io));
#if CO_SUPPORT_DEVICE_SELF_TEST
#if (CPU_ID ==1)
		//DST timer set
		extern void DST_speed_control(void *data);
		extern struct timer_list DST_timer;
		DST_timer.function = DST_speed_control;
		DST_timer.data = NULL;
#endif
#endif
	save_nor_flag = true;
	bcmd_timer.function = btn_otf_cmd_chk;
	bcmd_timer.data = "bcmd_timer";
	mod_timer(&bcmd_timer, jiffies + 2*HZ);

}
#endif
/*! @} */
