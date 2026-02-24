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
/*! \file btn_cmd_wr.c
 * @brief rainier BTN command manger
 *
 * \addtogroup btn
 * \defgroup btn_cmd
 * \ingroup btn
 * @{
 *
 */
//=============================================================================

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
#include "mpc.h"
#include "ipc_api.h"
#include "nvme_apl.h"
#include "spin_lock.h"

/*! \cond PRIVATE */
#define __FILEID__ btnw
#include "trace.h"
/*! \endcond */

#if (BTN_WCMD_CPU == CPU_ID)
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#ifdef WCMD_USE_IRQ
#define RCV_HW_IRQ
#define REL_HW_IRQ
#endif
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
extern btn_cmd_t _btn_w_cmds[BTN_W_CMD_CNT];

fast_data_ni btn_cmdq_t _btn_wcmdq;		  ///< btn write command in queue
fast_data_ni btn_fcmdq_t _btn_wcmdq_free; ///< btn write command free queue
fast_data_ni btn_cmdq_t _btn_wcmd_rls;	  ///< btn write command released queues

fast_data_ni u16 nvm_recv_nrm_wr_entry[Q_NWR_IN_ENTRY_CNT] __attribute__((aligned(8)));	 ///< write command queue in buffer
fast_data_ni u16 p0_free_nrm_wr_entry[Q_NWR_FREE_ENTRY_CNT] __attribute__((aligned(8))); ///< write command free queue buffer
fast_data_ni u32 wr_rls_cmd_entry[Q_RLS_W_CNT] __attribute__((aligned(8)));				 ///< btn command release buffer

fast_data_ni pool_t _btn_wcmd_pool; ///< btn commands pool

extern btn_smart_io_t rd_io;
extern btn_smart_io_t wr_io;
//extern vu32 Wrcmd;
//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
extern void __attribute__((weak, alias("__btn_write_de_abort"))) btn_write_de_abort(u16 btag);

fast_code void __btn_write_de_abort(u16 btag)
{
#if defined(MPC)
	cpu_msg_issue(BTN_WR_DAT_RLS_CPU - 1, CPU_MSG_WRITE_DE_ABORT, 0, btag);
#else
	panic("imp");
#endif
}

fast_code void btn_get_w_smart_io(volatile btn_smart_io_t *smart_io)
{
	sys_assert(CPU_ID == BTN_WCMD_CPU);
	btn_smart_io_mov(smart_io, &wr_io);
}

ps_code void bcmd_w_cmdq_resume(void)
{
	cmd_buf_region1_base_t region1_base = {
		.all = btn_readl(CMD_BUF_REGION1_BASE),
	};
	region1_base.b.cmd_buf_region1_base = btcm_to_dma(_btn_w_cmds);
	region1_base.b.cmd_buf_region1_dtcm = 1;
	btn_writel(region1_base.all, CMD_BUF_REGION1_BASE);
	pool_init(&_btn_wcmd_pool, (void *)_btn_w_cmds,
			  sizeof(_btn_w_cmds), sizeof(btn_cmd_t), BTN_W_CMD_CNT);

	btn_hdr_init(&_btn_wcmdq_free.cmdq.reg,
				 p0_free_nrm_wr_entry, Q_NWR_FREE_ENTRY_CNT,
				 &_btn_wcmdq_free.cmdq.wptr, P0_NRM_WR_CMD_SLOT_BASE);

	_btn_wcmdq_free.cmd_pool = &_btn_wcmd_pool;

	/* XXX: Bind Q and populate Q */
	{
		ncmd_queue_assignment_t reg;
		reg.all = btn_readl(NCMD_QUEUE_ASSIGNMENT);
		reg.b.p0_nrm_wr_que_num = BTN_CMDQ_Q2_NWR;
		btn_writel(reg.all, NCMD_QUEUE_ASSIGNMENT);

		int i;
		for (i = 0; i < Q_NWR_FREE_ENTRY_CNT; i++)
			btn_bcmd_push_freeQ(&_btn_wcmdq_free, NULL, true);

	}

	/* cmd IN Q */
	btn_hdr_init(&_btn_wcmdq.reg,
				 nvm_recv_nrm_wr_entry, Q_NWR_IN_ENTRY_CNT,
				 &_btn_wcmdq.wptr, NVM_CMD_QUEUE2_BASE);

	/* cmd RLS Q */
	btn_hdr_init(&_btn_wcmd_rls.reg, wr_rls_cmd_entry, Q_RLS_W_CNT,
				 &_btn_wcmd_rls.wptr, NVM_RELEASE_WCMD_BASE);

#if defined(MPC)
	local_item_done(btn_wcmd_rst);
#endif

	/* enable task event */
	#if !defined(RCV_HW_IRQ)
	poll_mask(VID_NCMD_RECV_Q2);
	#else
	pic_mask(VID_NCMD_RECV_Q2);
	#endif
	#if !defined(REL_HW_IRQ)
	poll_mask(VID_RLS_WCMD);
	#else
	pic_mask(VID_RLS_WCMD);
	#endif
}

fast_code void fw_btag_release(u32 btag)
{
	btn_cmd_t *bcmd = btag2bcmd(btag);
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);

	list_del_init(&bcmd_ex->entry);
	wr_io.running_cmd--;
	btn_bcmd_push_freeQ(&_btn_wcmdq_free, bcmd, true);
}

#if defined(MPC)
fast_code static void ipc_get_btn_wr_smart_io(volatile cpu_msg_req_t *req)
{
	volatile btn_smart_io_t *ret = (btn_smart_io_t *)req->pl;

	btn_get_w_smart_io(ret);
	cpu_msg_sync_done(req->cmd.tx);
	__dmb();
}

fast_code void ipc_fw_btag_release(volatile cpu_msg_req_t *req)
{
	u32 btag = req->pl;

	fw_btag_release(btag);
}

fast_code void ipc_btn_wcmd_abort(volatile cpu_msg_req_t *req)
{
	btn_wcmd_abort();
	cpu_msg_sync_done(req->cmd.tx);
}
#endif

fast_code void btn_iom_cmd_rels(btn_cmd_t *bcmd)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bcmd2btag(bcmd, 1));

	list_del(&bcmd_ex->entry);
	btn_bcmd_push_freeQ(&_btn_wcmdq_free, bcmd, true);
}

fast_code void btn_w_cmd_in(void)
{
	btn_cmd_in(&_btn_wcmdq, &_btn_wcmdq_free, true);
	btn_writel(NVM_CMD_QUE2_HAS_CMD_MASK, BTN_UM_INT_STS);
}

fast_code void btn_wcmd_rls_isr(void)
{
	btn_cmdq_t *cmdq = &_btn_wcmd_rls;
	hdr_reg_t *reg = &cmdq->reg;
	btn_iom_cmd_rels_cpl_t *entry = (btn_iom_cmd_rels_cpl_t *)reg->base;

	reg->entry_pnter.b.wptr = cmdq->wptr;

	while (reg->entry_pnter.b.wptr != reg->entry_pnter.b.rptr)
	{
		btn_iom_cmd_rels_cpl_t *cpl = entry + reg->entry_pnter.b.rptr;
		btn_cmd_t *bcmd = btag2bcmd(cpl->wr.btag);
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(cpl->wr.btag);

		//bf_mgr_trace(LOG_DEBUG, 0, "wr rlsQ btag %d rtype 0x%x, nid %d", cpl->wr.btag, cpl->wr.type, bcmd->dw0.b.nvm_cmd_id);
		switch (cpl->wr.type)
		{
		case RLS_T_WRITE_ABT:
			while (bcmd_ex->flags.b.bcmd_init == 0)
				btn_w_cmd_in();

			bcmd_ex->flags.b.wr_err = true;
			bf_mgr_trace(LOG_ERR, 0x95d2, "write abort: btag %d nid %d xferred %d/%d",
						 cpl->wr.btag, bcmd->dw0.b.nvm_cmd_id,
						 bcmd_ex->ndu - bcmd_ex->du_xfer_left, bcmd_ex->ndu);

			btn_write_de_abort(cpl->wr.btag);

			goto next;
			break;
		case RLS_T_WRITE:
			nvmet_core_btn_cmd_done(cpl->wr.btag, true, cpl->rd.hcmd_cid);
			break;

		case RLS_T_WRITE_ABT_RSP:
			bf_mgr_trace(LOG_ERR, 0x1c19, "btag(0x%x),type(0x%x) hcmd_id(0x%x), err", cpl->rd.btag, cpl->rd.type, cpl->rd.hcmd_cid);
			break;
		case RLS_T_WRITE_CQ:
			// successfully
			//if (bcmd_ex->flags.b.bcmd_rls == false)
			//	goto out;
			break;
		case RLS_T_READ_CQ:
		case RLS_T_READ_ABT:
		case RLS_T_READ_ERR:
		default:
			/* TODO: to abort a host read command, we need to abort all read data entries of this BTAG in BE,
			 *        before return this bcmd, otherwise we will have inv du cmd slot */
			bf_mgr_trace(LOG_ALW, 0x0e33, ": wr rlsQ, btag(0x%x),type(0x%x), hcmd_id(0x%x)", cpl->rd.btag, cpl->rd.type, cpl->rd.hcmd_cid);
			break;
		}
		if(bcmd_ex->flags.b.bcmd_abort != 1)
			wr_io.running_cmd--;
		list_del_init(&bcmd_ex->entry);
		//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
		//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
		//bf_mgr_trace(LOG_INFO, 0, "btn w cmd done %x", Wrcmd);
		btn_bcmd_push_freeQ(&_btn_wcmdq_free, bcmd, true);
	next:

		reg->entry_pnter.b.rptr = (reg->entry_pnter.b.rptr + 1) & reg->entry_dbase.b.max_sz;
	}

//out:
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	btn_writel(REV_NVM_WR_CMD_RLS_MASK, BTN_UM_INT_STS);
}

/*!
 * @brief abort all running commands, pop all entries on _otf_bcmd/_pending_bcmd, and return them
 *
 * @return	not used
 */
fast_code void btn_wcmd_abort(void)
{
	btn_cmd_ex_t *bcmd_ex;
	int btag;

    // TODO SDK 3.1.8.1 remove it
	//btn_w_cmd_in();

	evt_set_imt(evt_wr_pending_abort,0,0);

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (!list_empty(&_otf_bcmd))
	{
		bcmd_ex = list_first_entry(&_otf_bcmd, btn_cmd_ex_t, entry);
		btag = bcmd_ex2btag(bcmd_ex);
		bf_mgr_trace(LOG_DEBUG, 0xadd7, "abort bcmd %d", btag);
		list_del(&bcmd_ex->entry);

		if (bcmd_ex->flags.b.fua == 0)
		{
			bcmd_ex->du_xfer_left = 0;
		}
		bcmd_ex->flags.all = 0;
		bcmd_ex->flags.b.bcmd_abort = 1;

		if (btag < BTN_R_CMD_CNT)
		{ // adjust read io count
			rd_io.running_cmd--;
		}
		else
		{ // adjust write io count
			wr_io.running_cmd--;
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif

	}

#ifdef While_break
	start = get_tsc_64();
#endif

	while (!list_empty(&_pending_bcmd))
	{
		bcmd_ex = list_first_entry(&_pending_bcmd, btn_cmd_ex_t, entry);
		btag = bcmd_ex2btag(bcmd_ex);
		bf_mgr_trace(LOG_DEBUG, 0x2487, "abort pend bcmd %d", btag);
		list_del(&bcmd_ex->entry);
		bcmd_ex->du_xfer_left = 0;
		bcmd_ex->flags.all = 0;
		bcmd_ex->flags.b.bcmd_abort = 1;

		if (btag < BTN_R_CMD_CNT)
		{ // adjust read io count
			rd_io.running_cmd--;
		}
		else
		{ // adjust write io count
			wr_io.running_cmd--;
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}
}

init_code void bcmd_w_cmdq_init(void)
{
#if defined(MPC)
	cpu_msg_register(CPU_MSG_GET_BTN_WR_SMART_IO, ipc_get_btn_wr_smart_io);
	cpu_msg_register(CPU_MSG_FW_BTAG_RELEASE, ipc_fw_btag_release);
	cpu_msg_register(CPU_MSG_BTN_WCMD_ABORT, ipc_btn_wcmd_abort);
#endif
	#if !defined(RCV_HW_IRQ)
	poll_irq_register(VID_NCMD_RECV_Q2, btn_w_cmd_in);
	#else
	irq_register(VID_NCMD_RECV_Q2, btn_w_cmd_in);
	#endif
	btn_enable_isr(NVM_CMD_QUE2_HAS_CMD_MASK);

	#if !defined(REL_HW_IRQ)
	poll_irq_register(VID_RLS_WCMD, btn_wcmd_rls_isr);
	#else
	irq_register(VID_RLS_WCMD, btn_wcmd_rls_isr);
	#endif
	btn_enable_isr(REV_NVM_WR_CMD_RLS_MASK);
}
#endif
/*! @} */
