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
/*! \file btn.c
 * @brief Rainier BTN design
 *
 * \addtogroup btn
 * \defgroup btn
 * \ingroup btn
 * @{
 */
//=============================================================================

/*
 * Hub of BTN, we will integrate multiple components for good CPU partition
 *
 * 1) BTN_CORE_CPU is for core BTN management
 * 	etc. CMD buffer, GRP enable/disable ...
 * 2) BTN_RCMD_CPU is for BTN READ cmd IN/RLS
 * 3) BTN_WCMD_CPU is for BTN WRITE cmd IN/RLS
 * 4) BTN_WR_DAT_RLS_CPU (NRM/PAR data in and WRITE_CQ)
 * 5) BTN_FREE_WRITE_CPU
 */
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "btn_precomp.h"
#include "btn_cmd_data_reg.h"
#include "bf_mgr.h"
#include "event.h"
#include "assert.h"
#include "vic_id.h"
#include "pmu.h"
#include "console.h"
#include "rainier_soc.h"
#include "btn.h"
#include "dpe.h"
#include "misc.h"
#include "crypto.h"
#include "btn_cmd.h"
#include "btn_dat.h"
#include "btn.h"
#include "mpc.h"
#include "ddr.h"
#include "l2cache.h"
#include "dtag.h"

/*! \cond PRIVATE */
#define __FILEID__ btn
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

ps_data u32 backup_int_mask;		///< current CPU BTN backup interrupt mask when PMU
share_data volatile u16 host_sec_size;//joe add change sec size 20200817
share_data volatile u8 host_sec_bitz;//joe add change sec size  20200817

//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------

fast_code dtag_t bm_pop_dtag_llist(enum dtag_llist_sel sel)
{
	dtag_llist_sts_data_t sts;
	data_tag_llist_pop_t pop;
	dtag_llist_ctrl_status_t ctrl_sts;
	dtag_t dtag;

	dtag = _inv_dtag;
	if (sel != FREE_FW_RD_TBL_DTAG_LLIST &&
			sel != FREE_NRM_HST_RD_DTAG_LLIST &&
			sel != FREE_AURL_RD_DTAG_LLIST &&
			sel != FREE_WR_DTAG_LLIST &&
			sel != FREE_DTAG_RD_HMB_LLIST &&
			sel != FREE_DTAG_INT_RD_HMB_LLIST &&
			sel != FREE_FW_PRE_READ_LLIST
			) {
		bf_mgr_trace(LOG_INFO, 0xeeab, "no support");
		return _inv_dtag;
	}

	btn_writel((u32) sel, DTAG_LLIST_STS_SEL);

	sts.all = btn_readl(DTAG_LLIST_STS_DATA);

	if (sts.b.dtag_llist_v_count == 0) {
		bf_mgr_trace(LOG_INFO, 0x92ed, "free count %x", sts.all);
		dtag = _inv_dtag;
		goto out;
	}

	pop.all = 0;
	pop.b.dtag_llist_select = (u32) sel;
	btn_writel(pop.all, DATA_TAG_LLIST_POP);

	#ifdef While_break
		u64 start = get_tsc_64();
	#endif

	do {
		ctrl_sts.all = btn_readl(DTAG_LLIST_CTRL_STATUS);
		if (ctrl_sts.b.dtag_llist_pop_valid) {
			pop.all = btn_readl(DATA_TAG_LLIST_POP);
			dtag.dtag = pop.b.dtag_llist_pop_data;
			btn_writel(ctrl_sts.all, DTAG_LLIST_CTRL_STATUS);
			break;
		}
		if (ctrl_sts.b.dtag_llist_pop_fail) {
			btn_writel(ctrl_sts.all, DTAG_LLIST_CTRL_STATUS);
			dtag = _inv_dtag;
			break;
		}
		/*lint -e(506) while-loop to check hw ready */
		#ifdef While_break
			if(Chk_break(start,__FUNCTION__, __LINE__))
				break;
		#endif
	} while (1);
out:
	return dtag;
}

/*!
 * @brief PMU suspend callback function of BTN, toDO
 *
 * @param mode	sleep mode
 *
 * @return	always return true
 */
ddr_code bool btn_suspend(enum sleep_mode_t mode)//ps_code -> ddr_code by suda
{
	return true;
}

/*!
 * @brief PMU resume callback function of BTN
 *
 * @param mode	sleep mode
 *
 * @return	not used
 */
ps_code void btn_resume(enum sleep_mode_t mode)
{
	if (mode == SUSPEND_ABORT)
		return;

#if BTN_CORE_CPU == CPU_ID
	btn_core_resume();
#endif

	/* Restore interrupts */
	btn_writel(backup_int_mask, BTN_INT_MASK);

#if defined(BTN_CMD_CPU)
	btn_cmdq_resume();
#endif

	btn_datq_resume();
#ifdef HMB_DTAG
	btn_hmb_resume();
#endif
}

fast_code void btn_reset_resume(void)
{
	btn_resume(SUSPEND_INIT);
}

/* Each BTN CPU will link the init and wait for the other components */
init_code void btn_init(void)
{
	//joe 20200826 need to think how to re-read here?
	//joe 20200817 init sec info  //need recover form nand when not new drive
	//Eric 20231212 4K for Tencent FG248

	host_sec_size=512;//Eric 20231212
	host_sec_bitz=9;

	btn_writel(~0, BTN_INT_MASK);
#if defined(BTN_CMD_CPU)
	btn_cmdq_init();
#endif

	btn_datq_init();

#if BTN_CORE_CPU == CPU_ID
	btn_core_init();
#endif
	backup_int_mask = btn_readl(BTN_INT_MASK);

	btn_resume(SUSPEND_INIT);

	pmu_register_handler(SUSPEND_COOKIE_BM, btn_suspend,
			RESUME_COOKIE_BM, btn_resume);
#if defined(MPC) && (BTN_CORE_CPU == CPU_ID)
    #ifdef STOP_BG_GC
    extern void stop_bg_gc_init();
    stop_bg_gc_init();
    #endif
	local_item_done(btn_rst);
#if BTN_WCMD_CPU != CPU_ID
	wait_remote_item_done_ex(btn_wcmd_rst, BTN_WCMD_CPU - 1);
#endif
#endif
}

fast_code int btn_cmd_idle(void)
{
	return !(btn_readl(BTN_UM_INT_STS) &
			(NVM_CMD_QUE3_HAS_CMD_MASK |
			NVM_CMD_QUE2_HAS_CMD_MASK |
			NVM_CMD_QUE1_HAS_CMD_MASK |
			NVM_CMD_QUE0_HAS_CMD_MASK));
}

/*! \cond PRIVATE */
module_init(btn_init, BTN_APL);
/*! \endcond */
/*! @} */
