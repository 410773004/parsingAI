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
#include "trim.h"
/*! \cond PRIVATE */
#define __FILEID__ btnz
#include "trace.h"
/*! \endcond */

#if (BTN_WR_DAT_RLS_CPU == CPU_ID)
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define Q_RLS_FW_CNT 			BTN_W_CMD_CNT
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
typedef union {
	u32 all;
	struct {
		u32 btag       : 12;
		u32 rls_type   : 4;
		u32 nvm_cmd_id : 10;
		u32 rsvd_31_26 : 6;
	} b;
} btn_cmd_rels_req_t;

/* XXX: Due to the order, waste a litte, :-) */
fast_data_ni static btn_cmdq_t _btn_fw_cmd_rls;
extern btn_cmd_t _btn_w_cmds[];		///< btn command array
//extern vu32 Wrcmd;
extern u16 ns_valid_sec;
extern u16 drive_total_sector;
extern u16 ns_order;
extern u8 host_sec_bitz;
fast_data_ni u32 fw_rls_cmd_entry[Q_RLS_FW_CNT] __attribute__ ((aligned(8)));
//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
#if defined(MPC)
extern __attribute__((weak, alias("__fw_btag_release"))) void fw_btag_release(u32 btag);

fast_code void __fw_btag_release(u32 btag)
{
	cpu_msg_issue(BTN_WCMD_CPU - 1, CPU_MSG_FW_BTAG_RELEASE, 0, btag);
}
#endif

ps_code void bcmd_fw_cmdq_resume(void)
{
	/* fw RLS Q */
	btn_hdr_init(&_btn_fw_cmd_rls.reg, fw_rls_cmd_entry, Q_RLS_FW_CNT,
			&_btn_fw_cmd_rls.wptr, BTN_RLS_CMD_DONE_BASE);
}

fast_code void btn_cmd_rels_push(struct btn_cmd *bcmd, btn_nvm_rls_type_t rls_type)
{
	btn_cmdq_t *cmdq = &_btn_fw_cmd_rls;
	hdr_reg_t *reg = &cmdq->reg;
	//if(rls_type == RLS_T_WRITE_CQ){
	//	Wrcmd--;
	//}
	reg->entry_pnter.all = readl((void *)(reg->mmio + 4));
	u16 rptr      = reg->entry_pnter.b.rptr;
	u16 wptr_nxt = (reg->entry_pnter.b.wptr + 1) & reg->entry_dbase.b.max_sz;

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (rptr == wptr_nxt) { /* TODO Q busy */
		bf_mgr_trace(LOG_WARNING, 0xf6ad, "Q busy, rptr %d, wptr_nxt  %d",
			rptr, wptr_nxt);
		reg->entry_pnter.all = btn_readl(reg->mmio + 4);
		rptr = reg->entry_pnter.b.rptr;

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

	btn_cmd_rels_req_t *entry = (btn_cmd_rels_req_t *)reg->base;

#ifdef NS_MANAGE
	extern bool nvmet_ns_valid_check(u8 nsid);
	if ((false == nvmet_ns_valid_check(bcmd->dw1.b.ns_id)) &&
		(rls_type == RLS_T_WRITE_CQ)) {
		rls_type = RLS_T_WRITE;	// write command don't need to return CQ if nsid is invalid.
	}
#endif

	entry += reg->entry_pnter.b.wptr;
	entry->b.btag       = bcmd2btag(bcmd, 1);
	entry->b.rls_type   = rls_type;
	entry->b.nvm_cmd_id = bcmd->dw0.b.nvm_cmd_id;
	entry->b.rsvd_31_26 = 0;

	//bf_mgr_trace(LOG_DEBUG, 0, "bcmd rels rptr %d, wptr %d, payload %x",
		//     rptr, reg->entry_pnter.b.wptr, entry->all);

	reg->entry_pnter.b.wptr = wptr_nxt;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

extern bool esr_err_fua_flag;
ddr_code void btn_write_de_abort(u16 btag)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
	btn_cmd_t *bcmd = btag2bcmd(btag);
	u32 old = bcmd_ex->du_xfer_left;

	///< step .1 pop normal dtag queue
	extern void btn_data_in_isr(void);
	btn_data_in_isr();

	bf_mgr_trace(LOG_INFO, 0x84f2, "btag %d(%d) left %d -> %d", btag, bcmd->dw0.b.nvm_cmd_id, old, bcmd_ex->du_xfer_left);

	if (esr_err_fua_flag || is_fua_bcmd(bcmd)){
		extern void err_cmd_flush(u32 btag);
        err_cmd_flush(btag);
	}
	else {

		if (evt_wd_err_upt != 0xFF)
			evt_set_imt(evt_wd_err_upt, btag, 0);
   		///< step .2 adjust control variable
    	// dispatcher need to know how many DTAG was aborted, aborted dtag == xferred dtag
    	// and du_xfer_left is correct now, included all xfer dtag and abort dtag
        	///< step .3 release bcmd
    	bcmd_ex->du_xfer_left = 0;
		//< last DU error, directly release btn
		btn_cmd_rels_push(bcmd, RLS_T_WRITE_ABT_RSP);
	}

}

fast_code void btn_cmd_rels_push_bulk(struct btn_cmd **bcmds, int count)
{
	int i = 0;
	btn_cmdq_t *cmdq = &_btn_fw_cmd_rls;
	hdr_reg_t *reg = &cmdq->reg;
	reg->entry_pnter.all = readl((void *)(reg->mmio + 4));
	u16 rptr      = reg->entry_pnter.b.rptr;
//#ifndef NS_MANAGE
//#endif

	do {
		btn_cmd_t *bcmd = bcmds[i];

		u16 wptr_nxt = (reg->entry_pnter.b.wptr + 1) & reg->entry_dbase.b.max_sz;

#ifdef While_break
		u64 start = get_tsc_64();
#endif

		while (rptr == wptr_nxt) {
			reg->entry_pnter.all = btn_readl(reg->mmio + 4);
			rptr = reg->entry_pnter.b.rptr;
			writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));

#ifdef While_break
			if(Chk_break(start,__FUNCTION__, __LINE__))
				break;
#endif
		}

		btn_cmd_rels_req_t *entry = (btn_cmd_rels_req_t *)reg->base;

		entry += reg->entry_pnter.b.wptr;
		entry->b.btag       = (int) (bcmd - _btn_w_cmds) + BTN_R_CMD_CNT;
		entry->b.rls_type   = RLS_T_WRITE_CQ;
		#if TCG_WRITE_DATA_ENTRY_ABORT
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(entry->b.btag);
		if (bcmd_ex->flags.b.tcg_wr_abrt) {
			entry->b.rls_type   = RLS_T_WRITE;
		}
		#endif
		entry->b.nvm_cmd_id = bcmd->dw0.b.nvm_cmd_id;
		entry->b.rsvd_31_26 = 0;
		/*if (gResetFlag & (BIT(cNvmeSubsystemReset)|BIT(cNvmeFlrPfReset)))	//for jira_10. solution(1/12).shengbin yang 2023/11/15
		{
			entry->b.rls_type   = RLS_T_WRITE;
			bf_mgr_trace(LOG_ERR, 0, "RLS_T_WRITE. entry->b.btag %d nvm_cmd_id %d", entry->b.btag,entry->b.nvm_cmd_id);
		}*/

		reg->entry_pnter.b.wptr = wptr_nxt;

		i++;

		//Wrcmd--;
	} while (i < count);

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

#if defined(MPC)
fast_code void ipc_btn_write_de_abort(volatile cpu_msg_req_t *req)
{
	btn_write_de_abort(req->pl);
}
#endif

init_code void bcmd_fw_cmdq_init(void)
{
#if defined(MPC)
	cpu_msg_register(CPU_MSG_WRITE_DE_ABORT, ipc_btn_write_de_abort);
#endif
}
#endif
/*! @} */
