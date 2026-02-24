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
/*! \file
 * @brief Read Command Component
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
#include "btn_export.h"
#include "btn.h"
#include "ipc_api.h"
#include "cpu_msg.h"
#include "spin_lock.h"


/*! \cond PRIVATE */
#define __FILEID__ btnr
#include "trace.h"
/*! \endcond */
#if BTN_HOST_READ_CPU == CPU_ID
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define Q_NRD_FREE_ENTRY_CNT	16	///< normal read free btn command slot count
#define Q_PRD_FREE_ENTRY_CNT	16	///< priority read free btn command slot count

#define Q_NRD_IN_ENTRY_CNT	8	///< normal read btn command IN slot count
#define Q_PRD_IN_ENTRY_CNT	8	///< priority read btn command IN slot count

#define Q_RLS_R_CNT		BTN_R_CMD_CNT	///< release command queue size

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
extern btn_cmd_t _btn_r_cmds[BTN_R_CMD_CNT] __attribute__ ((aligned(32)));	///< btn commands buffer

fast_data_ni static u16 p0_free_nrm_rd_entry[Q_NRD_FREE_ENTRY_CNT] __attribute__ ((aligned(8)));	///< normal read command queue buffer, entry is command tag
fast_data_ni static u16 p0_free_pri_rd_entry[Q_PRD_FREE_ENTRY_CNT] __attribute__ ((aligned(8)));	///< priority read command queue buffer

fast_data_ni static u16 nvm_recv_nrm_rd_entry[Q_NRD_IN_ENTRY_CNT] __attribute__ ((aligned(8)));	///< normal read command in buffer, entry is command tag
fast_data_ni static u16 nvm_recv_pri_rd_entry[Q_PRD_IN_ENTRY_CNT] __attribute__ ((aligned(8)));	///< priority read command in buffer

fast_data_ni static u32 rd_rls_cmd_entry[Q_RLS_R_CNT] __attribute__ ((aligned(8)));	///< btn command release buffer

fast_data_ni btn_cmd_t _btn_r_cmds[BTN_R_CMD_CNT] __attribute__ ((aligned(32)));	///< btn commands buffer

fast_data_ni btn_cmdq_t _btn_nrdq;	///< normal read cmd queue
fast_data_ni btn_cmdq_t _btn_prdq;	///< priority read cmd queue

fast_data_ni btn_fcmdq_t _btn_nrdq_free;	///< normal read free cmd queue
fast_data_ni btn_fcmdq_t _btn_prdq_free;	///< priority read free cmd queue

fast_data_ni btn_cmdq_t _btn_rd_rls;	///< normal/priority read cmd release queue

fast_data_ni pool_t _btn_rcmd_pool;	///< btn commands pool

extern btn_smart_io_t rd_io;
extern btn_smart_io_t wr_io;
extern u16 ns_valid_sec;
extern u16 drive_total_sector;
extern u16 ns_order;
//extern vu32 Rdcmd;

//share_data bool ECCT_HIT_2_FLAG;
//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------

fast_code void btn_get_r_smart_io(volatile btn_smart_io_t *smart_io)
{
	sys_assert(CPU_ID == BTN_RCMD_CPU);
	btn_smart_io_mov(smart_io, &rd_io);
}

fast_code void bcmd_r_cmdq_resume(void)
{
	cmd_buf_region0_base_t region0_base = {
		.all = btn_readl(CMD_BUF_REGION0_BASE),
	};
	region0_base.b.cmd_buf_region0_base = btcm_to_dma(_btn_r_cmds);
	region0_base.b.cmd_buf_region0_dtcm = 1;
	btn_writel(region0_base.all, CMD_BUF_REGION0_BASE);

	btn_cmd_tag_split_num_t split_num = {
		.all = btn_readl(BTN_CMD_TAG_SPLIT_NUM),
	};

	split_num.b.btn_cmd_buf_split_en = 1;
	split_num.b.btn_cmd_tag_split_num = BTN_R_CMD_CNT;
	btn_writel(split_num.all, BTN_CMD_TAG_SPLIT_NUM);

	pool_init(&_btn_rcmd_pool, (void *)_btn_r_cmds,
			sizeof(_btn_r_cmds), sizeof(btn_cmd_t), BTN_R_CMD_CNT);

	btn_hdr_init(&_btn_nrdq_free.cmdq.reg,
			p0_free_nrm_rd_entry, Q_NRD_FREE_ENTRY_CNT,
			&_btn_nrdq_free.cmdq.wptr, P0_NRM_RD_CMD_SLOT_BASE);

	_btn_nrdq_free.cmd_pool = &_btn_rcmd_pool;

	btn_hdr_init(&_btn_prdq_free.cmdq.reg,
			p0_free_pri_rd_entry, Q_PRD_FREE_ENTRY_CNT,
			&_btn_prdq_free.cmdq.wptr, P0_PRI_RD_CMD_SLOT_BASE);

	_btn_prdq_free.cmd_pool = &_btn_rcmd_pool;

	/* XXX: Bind Q and populate Q */
	{
		ncmd_queue_assignment_t reg;
		reg.all = btn_readl(NCMD_QUEUE_ASSIGNMENT);
		reg.b.p0_nrm_rd_que_num = BTN_CMDQ_Q0_NRD;
		reg.b.p0_pri_rd_que_num = BTN_CMDQ_Q1_PRD;
		btn_writel(reg.all, NCMD_QUEUE_ASSIGNMENT);

		int i;
		for (i = 0; i < Q_NRD_FREE_ENTRY_CNT; i++)
			btn_bcmd_push_freeQ(&_btn_nrdq_free, NULL, false);
		for (i = 0; i < Q_PRD_FREE_ENTRY_CNT; i++)
			btn_bcmd_push_freeQ(&_btn_prdq_free, NULL, false);
	}

	/* cmd IN Q */
	btn_hdr_init(&_btn_nrdq.reg,
			nvm_recv_nrm_rd_entry, Q_NRD_IN_ENTRY_CNT,
			&_btn_nrdq.wptr, NVM_CMD_QUEUE0_BASE);
	btn_hdr_init(&_btn_prdq.reg,
			nvm_recv_pri_rd_entry, Q_PRD_IN_ENTRY_CNT,
			&_btn_prdq.wptr, NVM_CMD_QUEUE1_BASE);

	/* cmd RLS Q */
	btn_hdr_init(&_btn_rd_rls.reg, rd_rls_cmd_entry, Q_RLS_R_CNT,
			&_btn_rd_rls.wptr, NVM_RLS_RCMD_DONE_BASE);

	/* enable task event */
	poll_mask(VID_NCMD_RECV_Q0);
	poll_mask(VID_NCMD_RECV_Q1);
	poll_mask(VID_RLS_RCMD);
}

fast_code void btn_nrd_cmd_in(void)
{
	btn_cmd_in(&_btn_nrdq, &_btn_nrdq_free, false);
	btn_writel(NVM_CMD_QUE0_HAS_CMD_MASK, BTN_UM_INT_STS);
}

fast_code void btn_prd_cmd_in(void)
{
	btn_cmd_in(&_btn_prdq, &_btn_prdq_free, false);
	btn_writel(NVM_CMD_QUE1_HAS_CMD_MASK, BTN_UM_INT_STS);
}

fast_code void btn_rcmd_rls_isr(void)
{
	btn_cmdq_t *cmdq = &_btn_rd_rls;
	btn_fcmdq_t *fcmdq = NULL;
	hdr_reg_t *reg = &cmdq->reg;
	btn_iom_cmd_rels_cpl_t *entry = (btn_iom_cmd_rels_cpl_t *)reg->base;

	reg->entry_pnter.b.wptr = cmdq->wptr;

	while (reg->entry_pnter.b.wptr != reg->entry_pnter.b.rptr) {
		btn_iom_cmd_rels_cpl_t *cpl = entry + reg->entry_pnter.b.rptr;
		btn_cmd_t *bcmd = btag2bcmd(cpl->wr.btag);
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(cpl->wr.btag);
		bool ret_cq = false;

		//bf_mgr_trace(LOG_DEBUG, 0, "rd rlsQ btag %d rtype 0x%x, nid %d", cpl->wr.btag, cpl->wr.type, bcmd->dw0.b.nvm_cmd_id);
		switch (cpl->wr.type) {
		case RLS_T_WRITE:
		case RLS_T_WRITE_ABT:
		case RLS_T_WRITE_ABT_RSP:
		case RLS_T_WRITE_CQ:
		case RLS_T_READ_ERR:
			bf_mgr_trace(LOG_ERR, 0x4ce5, "btag(0x%x),type(0x%x) hcmd_id(0x%x), nvm_id %(0x%x) err",
					cpl->rd.btag, cpl->rd.type, cpl->rd.hcmd_cid, bcmd->dw0.b.nvm_cmd_id);
			bf_mgr_trace(LOG_ERR, 0x63c4, " s %x%x len %d", bcmd->dw3.b.slba_35_32, bcmd->dw2.b.slba_31_0,
					bcmd->dw3.b.xfer_lba_num);
			if (cpl->wr.type != RLS_T_READ_ERR)
				panic("impossible");
			ret_cq = false;
			break;
		case RLS_T_READ_FW_ABORT:
            //if(bcmd_ex->flags.b.bcmd_ecc_hit == true)
			//    ret_cq = false;
            //else

            //ECCT hit case (hardware issue)
            //hit first time: cmd_proc_isr isn't triggered by power on flow when ECCT hit to push btn release(FW_ABORT)
            //hit second time: proc_isr trigger handle_cq & btn_isr bypass handle_cq

            /*
            if(bcmd_ex->flags.b.bcmd_ecc_hit)
            {
                if(ECCT_HIT_2_FLAG == true)
                {
                    ret_cq = false;
                }
                else
                {
                    ret_cq = true;
                    ECCT_HIT_2_FLAG = true;
                }
            }
            else
            */
            {
                ret_cq = true;
            }
            
            //bf_mgr_trace(LOG_ERR, 0, "[DBG]btn rcmd bcmd_ex flag[0x%x]",bcmd_ex->flags.all);

			break;
		case RLS_T_READ_CQ:
			// successfully
			break;
		case RLS_T_READ_ABT:
		default:
			/* TODO: to abort a host read command, we need to abort all read data entries of this BTAG in BE,
			 *        before return this bcmd, otherwise we will have inv du cmd slot */
			bf_mgr_trace(LOG_ALW, 0x6b23, ": rd rlsQ, btag(0x%x),type(0x%x), hcmd_id(0x%x)", cpl->rd.btag, cpl->rd.type, cpl->rd.hcmd_cid);
			break;
		}

		extern void nvmet_core_btn_cmd_done(int btag, bool ret_cq, u32 cid);
		nvmet_core_btn_cmd_done(cpl->wr.btag, ret_cq, cpl->rd.hcmd_cid);

		if(bcmd_ex->flags.b.bcmd_abort != 1)
			rd_io.running_cmd--;

		list_del_init(&bcmd_ex->entry);

		switch (bcmd->dw0.b.cmd_type) {
		case NVM_READ:
			fcmdq = &_btn_nrdq_free;
			break;
		case PRI_READ:
			fcmdq = &_btn_prdq_free;
			break;
		default:
			panic("impossible");
			break;
		}

#ifdef NS_MANAGE
//if(!(ns_order==1&&(drive_total_sector==ns_valid_sec))){
		sys_assert(_btn_otf_cmd_cnt[bcmd->dw1.b.ns_id - 1] != 0);
		//bf_mgr_trace(LOG_DEBUG, 0, "-- cnt:%d nsid: %d btag: 0x%x type: %d",
			//_btn_otf_cmd_cnt[bcmd->dw1.b.ns_id - 1], bcmd->dw1.b.ns_id, cpl->wr.btag, bcmd->dw0.b.cmd_type);
		_btn_otf_cmd_cnt[bcmd->dw1.b.ns_id - 1] --;
//}
#endif
		//spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
		//Rdcmd--;
		//spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
		//bf_mgr_trace(LOG_INFO, 0, "btn rd cmd done %x",Rdcmd);
		btn_bcmd_push_freeQ(fcmdq, bcmd, false);

		reg->entry_pnter.b.rptr = (reg->entry_pnter.b.rptr + 1) & reg->entry_dbase.b.max_sz;

#if NVME_IO_LATENCY_MONITOR
		// timestamp unit is tick/us, but lat_mon_update_io_latcy will save in tick, so xfer to tick.
		u64 ticklag = (get_tsc_64()-bcmd_ex->start_ts) + (bcmd_ex->timestamp*get_cycle_per_us());
		bf_mgr_trace(LOG_DEBUG, 0x1529, " rd time_elapsed 0x%x%x start_ts 0x%x%x",(u32)(ticklag>>32ULL),(u32)(ticklag),(u32)(bcmd_ex->start_ts>>32ULL),(u32)(bcmd_ex->start_ts));
		BEGIN_CS1
		lat_mon_update_io_latcy(ticklag, NVME_LATM_READ_LATCY);
		END_CS1
#endif
	}
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
	btn_writel(REV_NVM_RD_CMD_RLS_MASK, BTN_UM_INT_STS);
}

/*!
 * @brief abort all running commands, pop all entries on _otf_bcmd/_pending_bcmd, and return them
 *
 * @return	not used
 */
//fast_code void btn_rcmd_abort(void)
ddr_code void btn_rcmd_abort(void)
{
	btn_cmd_ex_t *bcmd_ex;
	extern void nvmet_core_btn_cmd_done(int btag, bool ret_cq, u32 cid);
	int btag;

	//btn_nrd_cmd_in();
	//btn_prd_cmd_in();

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (!list_empty(&_otf_bcmd)) {
		bcmd_ex = list_first_entry(&_otf_bcmd, btn_cmd_ex_t, entry);
		btag = bcmd_ex2btag(bcmd_ex);
		bf_mgr_trace(LOG_DEBUG, 0xccf2, "abort bcmd %d", btag);
		list_del(&bcmd_ex->entry);
		bcmd_ex->du_xfer_left = 0;
		bcmd_ex->flags.all = 0;
		bcmd_ex->flags.b.bcmd_abort = 1;

		if(btag < BTN_R_CMD_CNT) {	// adjust read io count
			rd_io.running_cmd --;
		}
		else {	// adjust write io count
			wr_io.running_cmd --;
		}
#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

#ifdef While_break
	start = get_tsc_64();
#endif

	while (!list_empty(&_pending_bcmd)) {
		bcmd_ex = list_first_entry(&_pending_bcmd, btn_cmd_ex_t, entry);
		btag = bcmd_ex2btag(bcmd_ex);
		bf_mgr_trace(LOG_DEBUG, 0x30ab, "abort pend bcmd %d", btag);
		list_del(&bcmd_ex->entry);
		bcmd_ex->du_xfer_left = 0;
		bcmd_ex->flags.all = 0;
		bcmd_ex->flags.b.bcmd_abort = 1;

		if(btag < BTN_R_CMD_CNT) {	// adjust read io count
			rd_io.running_cmd --;
		}
		else {	// adjust write io count
			wr_io.running_cmd --;
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif

	}
}

#if defined(MPC)
fast_code static void ipc_get_btn_rd_smart_io(volatile cpu_msg_req_t *req)
{
	volatile btn_smart_io_t *ret = (volatile btn_smart_io_t *) req->pl;

	btn_get_r_smart_io(ret);
	cpu_msg_sync_done(req->cmd.tx);
	__dmb();
}
#endif

init_code void bcmd_r_cmdq_init(void)
{
#if defined(MPC)
	cpu_msg_register(CPU_MSG_GET_BTN_RD_SMART_IO, ipc_get_btn_rd_smart_io);
#endif
	poll_irq_register(VID_NCMD_RECV_Q0, btn_nrd_cmd_in);
	btn_enable_isr(NVM_CMD_QUE0_HAS_CMD_MASK);
	poll_irq_register(VID_NCMD_RECV_Q1, btn_prd_cmd_in);
	btn_enable_isr(NVM_CMD_QUE1_HAS_CMD_MASK);

	poll_irq_register(VID_RLS_RCMD, btn_rcmd_rls_isr);
	btn_enable_isr(REV_NVM_RD_CMD_RLS_MASK);
}
#endif

/*! @} */
