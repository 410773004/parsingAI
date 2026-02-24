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
/*! \file btn_core.c
 * @brief Rainier BTN core design
 *
 * \addtogroup btn
 * \defgroup btn
 * \ingroup btn
 * @{
 */
//=============================================================================

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
#include "cpu_msg.h"
#include "cmd_proc.h"
#include "nvme_spec.h"
#include "misc_register.h"

/*! \cond PRIVATE */
#define __FILEID__ btnc
#include "trace.h"
/*! \endcond */
#if BTN_CORE_CPU == CPU_ID
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define BTN_UNMAP_PAT (0)	   ///< unmapped data pattern, return this pattern for unmapped area //joe add NS ~0-->0  20200810
#define BTN_UNMAP_ZERO_PAT (0) ///< unmapped data pattern, return this pattern for unmapped area
#define BTN_DUM_PAT 0x12345678 ///< dummy data patterm, return this pattern for error lba

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
enum
{
	NORM_WR_MODE = 0,
	SEMI_STREAMING_MODE,
	DDRCOPY_SEMI_STREAMING_MODE = 3
};

enum
{
	BTN_FAST_INTR_MASK = (WR_DATA_GROUP0_UPDATE_MASK | WR_DATA_GROUP1_UPDATE_MASK | RD_DATA_GROUP0_UPDATE_MASK
						  //| RD_DATA_GROUP1_UPDATE_MASK
						  //| FW_RDATA_GROUP0_PROC_MASK
						  //| FW_RDATA_GROUP1_PROC_MASK
						  | NVM_CMD_QUE0_HAS_CMD_MASK | NVM_CMD_QUE1_HAS_CMD_MASK | NVM_CMD_QUE2_HAS_CMD_MASK | NVM_CMD_QUE3_HAS_CMD_MASK | REV_NVM_RD_CMD_RLS_MASK | REV_NVM_WR_CMD_RLS_MASK | COM_FREE_DTAG_UPDATE_MASK)
};

init_data u32 btn_core_int_mask = (BTN_DATA_PROCESS_DONE_MASK |
								   AXI_DATA_PARITY_ERROR_MASK |
								   SRAM_PARITYERROR_DET_MASK |
#if defined(ENABLE_SRAM_ECC)
								   SRAM_ECC_ERROR_DET_MASK |
#endif
								   DATA_ERR_LL_NOT_EMPTY_MASK |
								   DTAG_LINKLIST_OVERFLOW_MASK);

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
fast_data u8 evt_wd_err_upt = 0xFF;	   ///< event for dispatcher to hook when write error
fast_data u8 evt_hmb_rd_upt = 0xFF;	   ///< event for hmb request read done
fast_data_zi u16 btn_running_cmds = 0; ///< total btn running command, only read/write/compare
extern u16 dpe_ctxs_allocated_cnt;

#if !defined(DISABLE_HS_CRC_SUPPORT) && defined(DDR) && (BTN_CORE_CPU == CPU_ID)
share_data_zi void *ddr_hcrc_base = NULL;
#endif // HCRC with DDR

#if defined(HMETA_SIZE) && (BTN_CORE_CPU == CPU_ID)
static fast_data_zi dtag_t hmeta_dtag[DTAG_T_DDR + 1]; ///< sram dtag for host metadata
static fast_data_zi u16 hmeta_dtag_cnt[DTAG_T_DDR + 1]; ///< number of sram dtag with host metadata
#endif												   // HMETA_SIZE

fast_data_zi u32 btn_dp_int_mask; ///< data path interrupt
extern btn_smart_io_t rd_io;
extern btn_smart_io_t wr_io;
static fast_data_zi btn_callbacks_t _btn_err_cbs = {.hst_strm_rd_err = NULL, .write_err = NULL}; ///< callback when error data entries were met
extern tencnet_smart_statistics_t *tx_smart_stat;

extern u16 host_sec_size; //joe add change sec size 20200817
extern u8 host_sec_bitz;  //joe add change sec size  20200817
extern volatile u8 plp_trigger;
//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
extern void __attribute__((weak, alias("__bm_isr_com_free"))) bm_isr_com_free(void);

#if defined(MPC)
extern void __attribute__((weak, alias("__btn_wcmd_abort"))) btn_wcmd_abort(void);
fast_code void __btn_wcmd_abort(void)
{
	//cpu_msg_sync_start(); //SDK using sync mode
	cpu_msg_issue(BTN_WCMD_CPU - 1, CPU_MSG_BTN_WCMD_ABORT, 0, 0);
	//cpu_msg_sync_end();//SDK using sync mode
}
#endif

init_code void btn_callback_register(btn_callbacks_t *callbacks)
{
	_btn_err_cbs = *callbacks;
}

/*!
 * @brief read misc register
 *
 * @param reg	register offset
 *
 * @return	current register value
 */
static inline u32 misc_readl(u32 reg)
{
	return readl((void *)(MISC_BASE + reg));
}

/*!
 * @brief push free DTAGs to asic link list
 *
 * @param dtags		dtag list
 * @param cnt		list length
 * @param sel		dtag link list selection
 *
 * @return		not used
 */
static inline void btn_push_free_dtags(dtag_t *dtags, u32 cnt, enum dtag_llist_sel sel)
{
	u32 i;
	free_dtag_llist_push_t fdlp = {
		.b.dtag_llist_push_sel = sel,
	};

	for (i = 0; i < cnt; i++)
	{
		fdlp.b.dtag_llist_push_data = dtags[i].dtag;
		btn_writel(fdlp.all, FREE_DTAG_LLIST_PUSH);
	}
}

fast_code ALWAYS_INLINE u32 get_btn_running_cmds(void)
{
	/// this running command may not be accurate, due to share
	return rd_io.running_cmd + wr_io.running_cmd;
}

fast_code ALWAYS_INLINE u32 get_btn_rd_otf_cnt(void)
{
	/// this running command may not be accurate, due to share
	return rd_io.running_cmd;
}

fast_code void bm_free_hmb_load(void)
{
	dtag_t dtags[8];
	u32 cnt;

	cnt = dtag_get_bulk(DTAG_T_SRAM, 8, dtags);
	if (cnt)
		btn_push_free_dtags(dtags, cnt, FREE_DTAG_RD_HMB_LLIST);

	cnt = dtag_get_bulk(DTAG_T_SRAM, 8, dtags);
	if (cnt)
		btn_push_free_dtags(dtags, cnt, FREE_DTAG_INT_RD_HMB_LLIST);
}

fast_code void bm_free_hmb_pop(void)
{
	u32 i;
	dtag_t dtag;

	for (i = 0; i < 8; i++)
	{
		dtag = bm_pop_dtag_llist(FREE_DTAG_RD_HMB_LLIST);

		if (dtag.dtag != _inv_dtag.dtag)
			dtag_put(DTAG_T_SRAM, dtag);

		dtag = bm_pop_dtag_llist(FREE_DTAG_INT_RD_HMB_LLIST);

		if (dtag.dtag != _inv_dtag.dtag)
			dtag_put(DTAG_T_SRAM, dtag);
	}
}

fast_code void bm_free_rd_load(dtag_t *dtags, u32 count)
{
	btn_push_free_dtags(dtags, count, FREE_NRM_HST_RD_DTAG_LLIST);
}

fast_code void bm_free_fr_gc_load(dtag_t *dtags, u32 count)
{
	btn_push_free_dtags(dtags, count, FREE_FW_GC_RD_DTAG_LLIST);
}

fast_code void bm_free_fr_pre_load(dtag_t *dtags, u32 count)
{
	btn_push_free_dtags(dtags, count, FREE_FW_PRE_READ_LLIST);
}

fast_code void bm_free_frt_load(dtag_t *dtags, u32 count)
{
	btn_push_free_dtags(dtags, count, FREE_FW_RD_TBL_DTAG_LLIST);
}

fast_code void bm_free_aurl_load(dtag_t *dtags, u32 count)
{
	dtag_llist_ctrl_status_t ctrl_sts;

	bm_free_aurl_return(dtags, count);

	ctrl_sts.all = btn_readl(DTAG_LLIST_CTRL_STATUS);
	ctrl_sts.b.rd_strmbuf_dtag_only = 0;
	btn_writel(ctrl_sts.all, DTAG_LLIST_CTRL_STATUS);
}

fast_code void bm_free_semi_write_load(dtag_t *dtags, u32 count, u32 id)
{
	btn_push_free_dtags(dtags, count, id == 0 ? FREE_SEMI_STRM_LLIST0 : FREE_SEMI_STRM_LLIST1);
}

fast_code void bm_free_aurl_return(dtag_t *dtags, u32 count)
{
	btn_push_free_dtags(dtags, count, FREE_AURL_RD_DTAG_LLIST);
}

/*!
 * @brief recycle read entries while read error
 *
 * @return	not used
 */
fast_code static void bm_rd_err_recycle(int err_cnt)
{
	rd_entry_llist_pop_sel_t sel = {
		.all = (u32)PROBLEMATIC_READ_DATA_ENTRY_LLIST,
	};

	btn_writel(sel.all, RD_ENTRY_LLIST_POP_SEL);

#ifdef While_break
	u64 start ;
#endif

	while (err_cnt)
	{
		bm_pl_t data;
		rd_entry_llist_ctrl_status_t ctrl_sts;

		btn_writel(1, RD_ENTRY_LLIST_POP_DAT0); // write for pop
#ifdef While_break
		start = get_tsc_64();
#endif
		do
		{
			ctrl_sts.all = btn_readl(RD_ENTRY_LLIST_CTRL_STATUS);

#ifdef While_break
			if(Chk_break(start,__FUNCTION__, __LINE__))
				break;
#endif

		} while (ctrl_sts.b.rd_entry_ll_pop_valid == 0);

		btn_writel(ctrl_sts.all, RD_ENTRY_LLIST_CTRL_STATUS);

		data.dw0 = btn_readl(RD_ENTRY_LLIST_POP_DAT0);
		data.dw1 = btn_readl(RD_ENTRY_LLIST_POP_DAT1);

		if (data.pl.dtag != RD_STRM_BUF_TAG)
		{
			bf_mgr_trace(LOG_WARNING, 0x90b9, "recycle err rd ent b(%d) o(%d) d(%d)",
						 data.rd_err.btag, data.rd_err.du_ofst, data.rd_err.dtag);
			if (data.rd_err.btag < BTN_CMD_CNT)
			{
				if (_btn_err_cbs.hst_strm_rd_err)
					_btn_err_cbs.hst_strm_rd_err(&data);
			}
#if defined(UA_OFF)
			else if (data.rd_err.btag == UA_OFF)
			{
				if (_btn_err_cbs.hst_strm_rd_err)
					_btn_err_cbs.hst_strm_rd_err(&data);
			}
#endif
			#if (CO_SUPPORT_READ_AHEAD == TRUE)
			//else if (data.rd_err.btag == RA_OFF)
			//{
			//	if (_btn_err_cbs.hst_strm_rd_err)
			//		_btn_err_cbs.hst_strm_rd_err(&data);
			//}
			#endif
		}
		err_cnt--;
	}
}

fast_code u32 bm_free_wr_recycle(void)
{
	u32 cnt = 0;
	dtag_t dtag;

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	do {
		dtag = bm_pop_dtag_llist(FREE_WR_DTAG_LLIST);

		if (dtag.dtag != _inv_dtag.dtag)
		{
			bm_pl_t bm_pl = { .pl.dtag = dtag.dtag };
			bf_mgr_trace(LOG_INFO, 0xa64a, "free dtag %x", dtag.dtag);
			cnt++;

			if (_btn_err_cbs.write_err)
				_btn_err_cbs.write_err(&bm_pl);
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif

	} while (dtag.dtag != _inv_dtag.dtag);

	bf_mgr_trace(LOG_INFO, 0x1c2b, "recycled %d", cnt);
	return cnt;
}

// TODO Need confirm with IG
fast_code void bm_handle_rd_err(void)
{
	data_entry_err_count_t cnt = {
		.all = btn_readl(DATA_ENTRY_ERR_COUNT)
	};

	if (cnt.b.rd_entry_error_count) {
		bm_rd_err_recycle(cnt.b.rd_entry_error_count);
		btn_writel(RD_DATA_ENTRY_ERROR_MASK, BTN_ERROR_STATUS);
	}
}

/*!
 * @brief handler to recycle error link list
 *
 * @return	not used
 */
fast_code static void bm_data_entry_err_recycle(void)
{
	data_entry_err_count_t cnt = {
		.all = btn_readl(DATA_ENTRY_ERR_COUNT)};

	if (cnt.b.rd_entry_error_count)
	{
		bm_rd_err_recycle(cnt.b.rd_entry_error_count);
		btn_writel(RD_DATA_ENTRY_ERROR_MASK, BTN_ERROR_STATUS);
	}

	if (cnt.b.wd_entry_error_count)
	{
		//bm_wr_err_recycle(cnt.b.wd_entry_error_count);
		wd_entry_llist_ctrl_status_t ctrl_sts;

		btn_writel(WD_ENTRY_ERROR_LLIST, WD_ENTRY_LLIST_POP_SEL);

#ifdef While_break
		u64 start;
#endif

		do
		{
			bm_pl_t bm_pl;

			btn_writel(1, WD_ENTRY_LLIST_POP_DAT0); // write for pop
#ifdef While_break
		start = get_tsc_64();
#endif
			do
			{
				ctrl_sts.all = btn_readl(WD_ENTRY_LLIST_CTRL_STATUS);

#ifdef While_break
				if(Chk_break(start,__FUNCTION__, __LINE__))
					break;
#endif
			} while (ctrl_sts.b.wd_entry_ll_pop_valid == 0);

			btn_writel(ctrl_sts.all, WD_ENTRY_LLIST_CTRL_STATUS);

			bm_pl.dw0 = btn_readl(WD_ENTRY_LLIST_POP_DAT0);
			bm_pl.dw1 = btn_readl(WD_ENTRY_LLIST_POP_DAT1);
			//bf_mgr_trace(LOG_ERR, 0xe590, "btag %d du %d dtag %x", bm_pl.pl.btag, bm_pl.pl.du_ofst, bm_pl.pl.dtag);
			bf_mgr_trace(LOG_ERR, 0x66f4, "all(0x%x - %x) btag %d du %d dtag %x",(bm_pl.all>>32),bm_pl.all,bm_pl.pl.btag, bm_pl.pl.du_ofst, bm_pl.pl.dtag);

			if (bm_pl.semi_ddr_pl.type_ctrl & BTN_SEMI_MODE_MASK)
			{
				dtag_t sdtag = {
					.dtag = bm_pl.semi_ddr_pl.semi_sram,
				};
				u32 ddtag_id = bm_pl.semi_ddr_pl.semi_ddr;

				bm_free_semi_write_load(&sdtag, 1, 0);
				bm_pl.semi_ddr_pl.type_ctrl = 0;
				bm_pl.pl.dtag = ddtag_id | DTAG_IN_DDR_MASK;
			//}

			// TODO Need confirm with IG
			} else if (bm_pl.pl.type_ctrl == BTN_NVM_TYPE_CTRL_PAR) {
				bm_pl.pl.type_ctrl = 0;
			} else if (bm_pl.pl.type_ctrl == BTN_NVM_TYPE_CTRL_CMP) {
				bm_pl.pl.type_ctrl = 0;
			}

			bm_pl.pl.type_ctrl &= ~BTN_NVM_TYPE_CTRL_POISON_BIT;
			if(!plp_trigger)
			{
				if (_btn_err_cbs.write_err)
					_btn_err_cbs.write_err(&bm_pl);
			}
			/*
			else
			{
				bf_mgr_trace(LOG_ERR, 0, "no release this %x dtag!!!",bm_pl.pl.dtag);
			}
			*/

		} while (--cnt.b.wd_entry_error_count);

		btn_writel(WR_DATA_ENTRY_ERROR_MASK, BTN_ERROR_STATUS);
	}
	else
	{
		btn_writel(WR_DATA_ENTRY_ERROR_MASK, BTN_ERROR_STATUS);
	}
}

/*!
 * @brief AXI parity error register dump
 *
 * From Innogrit suggestion, dump releated register to identify module
 *
 * @return	not used
 */

ddr_code void HalAxiError_DumpRegister(void)
{
#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return;
	}
#endif
    btn_error_status_t error_sts = {.all = btn_readl(BTN_ERROR_STATUS)};
    u32 target = 0x5002;
    u32 status;

    bf_mgr_trace(LOG_INFO, 0x02a6, "----Detection----");
    bf_mgr_trace(LOG_ERR, 0x1092, "[AXI] data poision:%d",error_sts.b.btn_axi_poison_bit_set);
    bf_mgr_trace(LOG_ERR, 0x23aa, "[AXI] bus error:%d",error_sts.b.btn_axi_bus_parity_err);

    bf_mgr_trace(LOG_INFO, 0x8ae5, "----NVMe Part----");
    bf_mgr_trace(LOG_ERR, 0x7717, "[BTN ] 0xc001004c:0x%x",error_sts.all);
    bf_mgr_trace(LOG_ERR, 0xb183, "[PCIe] 0xc0043004:0x%x",readl((void *) 0xc0043004));

    bf_mgr_trace(LOG_INFO, 0x94ac, "----FDMA Part----");
    writel(target, (void *)0xc0000064);
    status = readl((void *) 0xc0000064);
    bf_mgr_trace(LOG_ERR, 0x1d33, "[FDMA] 0xc0000064:0x%x",status);
}

/*!
 * @brief interrupt routine for BTN others/slow interrupt
 *
 * all fast interrupt should be handled in specific isr
 *
 * @return	not used
 */

fast_code void bm_isr_slow(void)
{
	btn_um_int_sts_t sts = {
		.all = btn_readl(BTN_MK_INT_STS),
	};
	btn_um_int_sts_t ack = {
		.all = 0,
	};

	bool report = false;
	enum nvme_event_internal_error_status_type param;

	//bf_mgr_trace(LOG_DEBUG, 0, "sts = %x", sts.all);
	sts.all &= ~(BTN_FAST_INTR_MASK);

	/* NRM/PAR requires DPE resources, so recycle DPE immediately */
	if (sts.b.btn_data_process_done)
	{
		dpe_isr();
		ack.b.btn_data_process_done = 1;
	}
	if (sts.b.data_err_ll_not_empty)
	{
		bm_data_entry_err_recycle();
		ack.b.data_err_ll_not_empty = 1;
	}
#if defined(ENABLE_SRAM_ECC)
	if (sts.b.sram_ecc_error_det)
	{
		tx_smart_stat->sram_error_count[1]+=1;
#ifdef DUMP_SRAM_REG
	    dump_sram_error_reg();
#endif
		bf_mgr_trace(LOG_ERR, 0x25d2, "sram ecc error");
		panic("bm ecc error!");
	}

#endif
	if (sts.b.axi_data_parity_error)
	{
		//bf_mgr_trace(LOG_ERR, 0, "AXI parity error, maybe poisoned data in c001004c=%x",
		//btn_readl(BTN_ERROR_STATUS));

		if(!plp_trigger)
		{
			#ifdef DUMP_SRAM_REG
			//dump_sram_error_reg();
			#endif
			bf_mgr_trace(LOG_ERR, 0xed91, "[BTN] AXI parity error");
			//HalAxiError_DumpRegister();

			u32 _val = btn_readl(BTN_ERROR_STATUS);
			if(_val & (SRAM_RD_ECC_ERR1_MASK|SRAM_WR_ECC_ERR_MASK))
			{
				tx_smart_stat->sram_error_count[2]+=1;
			}
			btn_writel(_val, BTN_ERROR_STATUS);

			report = true;
			param = AXI_PARITY_ERRS;
		}
		else
		{
			u32 _val = btn_readl(BTN_ERROR_STATUS);
			btn_writel(_val, BTN_ERROR_STATUS);
			report = false;
		}

		ack.b.axi_data_parity_error = 1;

	}

	if (sts.b.sram_parityerror_det)
	{
		tx_smart_stat->sram_error_count[0]+=1;
		btn_error_status_t error_sts = {.all = btn_readl(BTN_ERROR_STATUS)};
		u32 error = error_sts.all;
		if(error_sts.all & 0x400)	// nvm_rd_hcrc_error
		{
            bf_mgr_trace(LOG_ERR, 0x7577, "[BTN] Read HCRC Error!");
			//tx_smart_stat->hcrc_error_count[0]++;
		}
		//error &= ~(WR_ENTRY_UPDT_Q_FULL_MASK); // queue full is not error
		// TODO Need confirm with IG
		error &= ~(WR_ENTRY_UPDT_Q_FULL_MASK | RD_ENTRY_UPDT_Q_FULL_MASK);  // queue full is not error
#if !defined(DISABLE_HS_CRC_SUPPORT)
		error &= ~(BDAT_SRAM_PAR_ERR_MASK); // it should be normal if hcrc was enabled
#endif
		if (error)
		{
			report = true;
			param = SRAM_PARITY_ERRS;
			bf_mgr_trace(LOG_ERR, 0x8e78, "sram data parity error %x", error);

#ifdef DUMP_SRAM_REG
		    dump_sram_error_reg();
#endif
		}

		btn_writel(error_sts.all, BTN_ERROR_STATUS);
		ack.b.sram_parityerror_det = 1;
	}

	sts.all &= ~ack.all;
	if (sts.all != 0)
		bf_mgr_trace(LOG_ERR, 0x2e93, "unhandled ISR sts 0x%x", sts.all);

	btn_writel(ack.all, BTN_UM_INT_STS);

	if(report == true){
		extern void nvmet_evt_aer_in();
		nvmet_evt_aer_in(((NVME_EVENT_TYPE_ERROR_STATUS << 16)|ERR_STS_PERSISTENT_INTERNAL_DEV_ERR),param);
	}
}

ddr_code void dpe_handle_incoming(void)
{
	bf_mgr_trace(LOG_INFO, 0x2922, "dpe cnt(%d),REQ_POINTER(0x%x),RES_POINTER(0x%x)",
		 dpe_ctxs_allocated_cnt, btn_readl(DATA_PROC_REQ_POINTER),btn_readl(DATA_PROC_RES_POINTER));
#ifdef While_break
	u64 start = get_tsc_64();
#endif
#if(PLP_SUPPORT == 1)
	gpio_int_t gpio_int_status;
#endif

	while(dpe_ctxs_allocated_cnt)
	{
#if(PLP_SUPPORT == 1)
		gpio_int_status.all = misc_readl(GPIO_INT);
		if((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))||plp_trigger)
			break;
#endif
		bm_isr_slow();
		cpu_msg_isr();
	#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
	#endif
	}

}
/*!
 * @brief pop all read data entry link list
 *
 * @return		not used
 */
ddr_code void bm_pop_all_rd_entry_list(void)
{
	rd_entry_llist_sts_data_t sts;
	u32 sel;

	for (sel = 0; sel <= PROBLEMATIC_READ_DATA_ENTRY_LLIST; sel++)
	{
		/* step 1: Select related linked-list to check status */
		btn_writel(sel, RD_ENTRY_LLIST_STS_SEL);

		/* step 2: check data count in linked-list */
		sts.all = btn_readl(RD_ENTRY_LLIST_STS_DATA);

		bf_mgr_trace(LOG_INFO, 0x4a0d, "[LLST] :%d %x", sel, sts.all);
	}
}

/*!
 * @brief not used now
 *
 * pop read data entries from link list, only support streaming data entries
 *
 * @param sel		must be HOST_RD_AURL_DATA_ENTRY_LLIST
 * @param bm_pl		return data entries in this pointer
 *
 * @return		return true if popped
 */
fast_code bool bm_pop_rd_entry_list(enum rd_entry_llist_sel sel, bm_pl_t *bm_pl)
{
	rd_entry_llist_sts_data_t sts;
	rd_entry_llist_pop_sel_t pop_sel;
	rd_entry_llist_ctrl_status_t ctrl_sts;

	if (sel != HOST_RD_AURL_DATA_ENTRY_LLIST && sel != FW_RD_ADJ_DROP_DTAG_DATA_ENTRY_LLIST && sel != FW_RD_ADJ_FREE_DTAG_DATA_ENTRY_LLIST)
	{
		return false;
	}

	/* step 1: Select related linked-list to check status */
	btn_writel((u32)sel, RD_ENTRY_LLIST_STS_SEL);

	/* step 2: check data count in linked-list */
	sts.all = btn_readl(RD_ENTRY_LLIST_STS_DATA);

	if (sts.b.rd_entry_llist_v_count == 0)
	{
		bf_mgr_trace(LOG_INFO, 0xe6ea, "free count %x", sts.all);
		return false;
	}

	/* step 3: select related linked-list to pop out*/
	pop_sel.b.rd_entry_llist_pop_sel = (u32)sel;
	btn_writel(pop_sel.all, RD_ENTRY_LLIST_POP_SEL);

	/* step 4: write pop data register to trigger pop*/
	btn_writel(0, RD_ENTRY_LLIST_POP_DAT0);
	btn_writel(0, RD_ENTRY_LLIST_POP_DAT1);

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	/* step 5: polling the ctrlr status bit 12 to check pop operation done*/
	do
	{
		ctrl_sts.all = btn_readl(RD_ENTRY_LLIST_CTRL_STATUS);
		if (ctrl_sts.b.rd_entry_ll_pop_valid)
		{
			bm_pl->dw0 = btn_readl(RD_ENTRY_LLIST_POP_DAT0);
			bm_pl->dw1 = btn_readl(RD_ENTRY_LLIST_POP_DAT1);

			btn_writel(ctrl_sts.all, RD_ENTRY_LLIST_CTRL_STATUS);
			break;
		}
		/*lint -e(506) while-loop to check hw ready */

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif

	} while (1);
	return true;
}

fast_code dtag_t bm_pop_wd_entry_llist(enum wd_entry_llist_sel sel)
{
	wd_entry_llist_sts_data_t sts;
	wd_entry_llist_pop_dat0_t pop0;
	wd_entry_llist_pop_dat1_t pop1;
	wd_entry_llist_pop_sel_t pop_sel;
	wd_entry_llist_ctrl_status_t ctrl_sts;
	dtag_t dtag;
	u32 flags;

	if (sel != NRM_WD_ENTRY_LLIST &&
		sel != PART_WD_ENTRY_LLIST &&
		sel != CMP_WD_ENTRY_LLIST &&
		sel != PDONE_WD_ENTRY_LLIST &&
		sel != WD_ENTRY_ERROR_LLIST)
	{
		bf_mgr_trace(LOG_INFO, 0xdfe8, "no support");
		return _inv_dtag;
	}

	flags = irq_save();
	dtag = _inv_dtag;
	/* step 1: Select related linked-list to check status */
	btn_writel((u32)sel, WD_ENTRY_LLIST_STS_SEL);

	/* step 2: check data count in linked-list */
	sts.all = btn_readl(WD_ENTRY_LLIST_STS_DATA);

	if (sts.b.wd_entry_llist_v_count == 0)
	{
		bf_mgr_trace(LOG_INFO, 0x1993, "free count %x", sts.all);
		dtag = _inv_dtag;
		goto out;
	}

	/* step 3: select related linked-list to pop out*/
	pop_sel.b.wd_entry_llist_pop_sel = (u32)sel;
	btn_writel(pop_sel.all, WD_ENTRY_LLIST_POP_SEL);

	/* step 4: write pop data register to trigger pop*/
	pop0.all = pop1.all = 0;
	btn_writel(pop0.all, WD_ENTRY_LLIST_POP_DAT0);
	btn_writel(pop1.all, WD_ENTRY_LLIST_POP_DAT1);

	/* step 5: polling the ctrlr status bit 12 to check pop operation done*/
	do
	{
		ctrl_sts.all = btn_readl(WD_ENTRY_LLIST_CTRL_STATUS);
		if (ctrl_sts.b.wd_entry_ll_pop_valid)
		{
			pop0.all = btn_readl(WD_ENTRY_LLIST_POP_DAT0);
			pop1.all = btn_readl(WD_ENTRY_LLIST_POP_DAT1);
			dtag.dtag = pop0.b.wd_entry_llist_pop_dat0; // todo syu
			btn_writel(ctrl_sts.all, WD_ENTRY_LLIST_CTRL_STATUS);
			break;
		}
		/*lint -e(506) while-loop to check hw ready */
	} while (1);
out:
	irq_restore(flags);
	return dtag;
}

fast_code void bm_unmap_ctrl(bool enable, u32 pattern, u32 idx)
{
	read_unmap_ctrl_reg_t umap_dummy_ctrl;
	write_dummy_ctrl_reg_t write_dummy_ctrl;
	u32 pat_reg;
	u32 ctrl_reg;

	if (idx > 1)
	{
		bf_mgr_trace(LOG_ERR, 0xf78c, "wrong unmap idx %d", idx);
		return;
	}

	pat_reg = (idx == 0) ? READ_UNMAP_DATA_PAT : READ_UNMAP1_DATA_PAT;
	ctrl_reg = (idx == 0) ? READ_UNMAP_CTRL_REG : READ_UNMAP1_CTRL_REG;
	/* XXX: BTN write dummy ? */
	if (enable)
	{
		if (idx == 0)
		{
			btn_writel(pattern, WRITE_DUMMY_DATA_PAT);
			write_dummy_ctrl.b.bm_write_dummy_enable = 1;
			write_dummy_ctrl.b.write_dummy_dtag_num = WVTAG_ID;
			btn_writel(write_dummy_ctrl.all, WRITE_DUMMY_CTRL_REG);

			umap_dummy_ctrl.b.read_unmap_dtag_num = RVTAG_ID;
		}
		else
		{
			umap_dummy_ctrl.b.read_unmap_dtag_num = RVTAG2_ID;
		}

#if (PI_SUPPORT)
		umap_dummy_ctrl.b.ump_pi_reftag_fill_sel = 1;
		umap_dummy_ctrl.b.ump_pi_apptag_fill_sel = 1;
		umap_dummy_ctrl.b.ump_pi_guard_fill_sel = 1;
#endif
		btn_writel(pattern, pat_reg);
		umap_dummy_ctrl.b.bm_read_unmap_enable = 1;
	}
	else
	{
		if (idx == 0)
			btn_writel(0, WRITE_DUMMY_CTRL_REG);

		umap_dummy_ctrl.all = 0;
	}

	btn_writel(umap_dummy_ctrl.all, ctrl_reg);
}

fast_code void btn_semi_wait_switch(bool wait)
{
	wr_semistrm_ctrl_status_t semi_ctrl = {.all = btn_readl(WR_SEMISTRM_CTRL_STATUS)};

	semi_ctrl.b.wait_sramcopy_tag_en = wait;

	btn_writel(semi_ctrl.all, WR_SEMISTRM_CTRL_STATUS);
}

#if defined(MPC)
fast_code static void ipc_btn_semi_wait_switch(volatile cpu_msg_req_t *req)
{
	bool wait = (bool)req->pl;

	btn_semi_wait_switch(wait);
}
#endif

fast_code u32 btn_semi_write_ctrl(bool ddr_copy)
{
	wr_semistrm_ctrl_status_t semi_ctrl = {.all = 0};

	semi_ctrl.b.semistrm_dtag_ll0_en = 1;
	semi_ctrl.b.wait_sramcopy_tag_en = 1;
	if (ddr_copy)
		semi_ctrl.b.wd_dtag_handle_type = DDRCOPY_SEMI_STREAMING_MODE;
	else
		semi_ctrl.b.wd_dtag_handle_type = SEMI_STREAMING_MODE;

	btn_writel(semi_ctrl.all, WR_SEMISTRM_CTRL_STATUS);

	return 0;
}

/*!
 * @brief initialized pi buffer
 *
 * @return	not used
 */
#define ALL_DRAM_WI_PI

init_code void btn_hmeta_init(void)
{
#if defined(HMETA_SIZE) && (BTN_CORE_CPU == CPU_ID)
	u32 hmeta_cnt;
	u32 hmeta_size;

	sys_assert(HMETA_SIZE == 8 || HMETA_SIZE == 16);

	hmeta_cnt = SRAM_IN_DTAG_CNT << (DTAG_SHF - HLBASZ);  // no replace HLBASZ, because allocate MAX Buf
	hmeta_size = hmeta_cnt * HMETA_SIZE;

	hmeta_dtag_cnt[DTAG_T_SRAM] = (u8)occupied_by(hmeta_size, DTAG_SZE);
	hmeta_dtag[DTAG_T_SRAM] = dtag_cont_get(DTAG_T_SRAM, hmeta_dtag_cnt[DTAG_T_SRAM]);
	/* TODO: fix dtag_init order to make hmeta_dtag[DTAG_T_SRAM] allocation dynamically*/
	//ipc_api_remote_dtag_get((u32 *) &hmeta_dtag[DTAG_T_SRAM], true, DTAG_T_SRAM);
	//u32 i;
	//for (i = 1; i < hmeta_dtag_cnt[DTAG_T_SRAM]; i++) {
	//	ipc_api_remote_dtag_get((u32 *) NULL, true, DTAG_T_SRAM);
	//}

	sys_assert(hmeta_dtag[DTAG_T_SRAM].dtag != _inv_dtag.dtag);
#if defined(DDR)
#ifndef ALL_DRAM_WI_PI
	hmeta_cnt = MAX_DDR_DTAG_CNT << (DTAG_SHF - HLBASZ);
	hmeta_size = hmeta_cnt * HMETA_SIZE;
	hmeta_dtag_cnt[DTAG_T_DDR] = (u16)occupied_by(hmeta_size, DTAG_SZE);
#else
    hmeta_cnt = ddr_get_capapcity() >> HLBASZ;
    hmeta_size = hmeta_cnt * HMETA_SIZE;
    hmeta_dtag_cnt[DTAG_T_DDR] = (u16)occupied_by(hmeta_size, DTAG_SZE);
    //evlog_printk(LOG_ALW, "ddr capacity 0x%x%x", (ddr_get_capapcity() >> 32) & 0xFFFFFFFF, ddr_get_capapcity() & 0xFFFFFFFF);
#endif
    hmeta_dtag[DTAG_T_DDR].dtag = ddr_dtag_register(hmeta_dtag_cnt[DTAG_T_DDR]);
    evlog_printk(LOG_ALW, "hmeta_dtag start dtag %d", hmeta_dtag[DTAG_T_DDR].dtag);
	sys_assert(hmeta_dtag[DTAG_T_DDR].dtag != ~0);
	hmeta_dtag[DTAG_T_DDR].b.in_ddr = 1;
#endif //DDR
#endif
}

#if !defined(DISABLE_HS_CRC_SUPPORT)
init_code void btn_hcrc_init(void)
{
#if defined(DDR)
	//u32 cnt = occupied_by(MAX_DDR_DTAG_CNT * 2 * NR_LBA_PER_LDA, DTAG_SZE);
	u32 cnt = 0;
	//if (host_sec_bitz == 9)									   //joe add 20200820//here need confirm 20200904
		cnt = occupied_by(MAX_DDR_DTAG_CNT * 2 * 8, DTAG_SZE); //joe add sec size 20200817
	//else
		//cnt = occupied_by(MAX_DDR_DTAG_CNT * 2 * 1, DTAG_SZE); //joe add sec size 20200820
	u32 ddr_dtag_id;

	ddr_dtag_id = ddr_dtag_register(cnt);
	sys_assert(ddr_dtag_id != ~0);
	ddr_hcrc_base = (void *)ddtag2mem(ddr_dtag_id);
	memset(ddr_hcrc_base, 0, cnt * DTAG_SZE);
#if defined(ENABLE_L2CACHE)
	l2cache_mem_flush(ddr_hcrc_base, cnt * DTAG_SZE);
#endif
#endif
}

fast_code u16 btn_hcrc_read(dtag_t dtag, u32 off)
{
	btn_sram_rd_addr_t hcrc_rd_addr;
	btn_sram_rd_dat0_t hcrc_rd_dat0;
	u32 addr = dtag.b.dtag << 3;

#if defined(USE_8K_DU)
	addr <<= 1;
#endif
	//off &= NR_LBA_PER_LDA_MASK;
	if (host_sec_bitz == 9)
		off &= 8 - 1; //joe add sec size 20200820
	else
		off &= 1 - 1; //joe add sec size 20200820
#if defined(DDR)
	if (dtag.b.in_ddr)
	{
		u16 *hcrc_base = (u16 *)ddr_hcrc_base;
		return hcrc_base[addr + off];
	}
#endif
#if defined(HMB_DTAG)
	if (dtag.b.type)
	{
		return 0; // hmb no hcrc in device
	}
#endif
	hcrc_rd_addr.b.btn_sram_select = 1; // host crc seleted
	hcrc_rd_addr.b.btn_sram_rd_addr = addr + off;
	btn_writel(hcrc_rd_addr.all, BTN_SRAM_RD_ADDR);
	hcrc_rd_dat0.all = btn_readl(BTN_SRAM_RD_DAT0);
	/* 16 bit hcrc */
	return (hcrc_rd_dat0.all & 0xffff);
}

fast_code void btn_hcrc_write(dtag_t dtag, u32 off, u16 hcrc)
{
	btn_hcrc_sram_write_t hcrc_wr;
	u32 addr = dtag.b.dtag << 3;

#if defined(USE_8K_DU)
	addr <<= 1;
#endif
	//off &= NR_LBA_PER_LDA_MASK;
	if (host_sec_bitz == 9)
		off &= 8 - 1; //joe add sec size 20200820
	else
		off &= 1 - 1; //joe add sec size 20200820
	addr += off;

#if defined(DDR)
	if (dtag.b.in_ddr)
	{
		u16 *hcrc_base = (u16 *)ddr_hcrc_base;
		hcrc_base[addr] = hcrc;
		return;
	}
#endif

#if defined(HMB_DTAG)
	if (dtag.b.type)
		return;
#endif


#ifdef While_break
	u64 start = get_tsc_64();
#endif

	do
	{
		hcrc_wr.all = btn_readl(BTN_HCRC_SRAM_WRITE);

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	} while (hcrc_wr.b.hcrc_sram_wr_busy);

	hcrc_wr.b.hcrc_sram_address = addr;
	hcrc_wr.b.hcrc_sram_wr_data = hcrc;

	btn_writel(hcrc_wr.all, BTN_HCRC_SRAM_WRITE);
}

#if 0
ucmd_code int get_hcrc_console(int argc, char *argv[])
{
	if (argc >= 2)
    {
        dtag_t dtag = { .dtag = strtol(argv[1], (void *)0, 0)};
        pda_t pda = 0;
		if (argc >= 3)
    		pda = strtol(argv[2], (void *)0, 0);

        u8 j;
        u8 lba_cnt_per_lba = 4096 / host_sec_size;
        u16 hcrc = 0;
        u16 hcrc_0 = 0;
        for (j = 0; j < lba_cnt_per_lba; j++)
        {
            //off = j;
            hcrc = btn_hcrc_read(dtag, j);
            if(j == 0)
                hcrc_0 = hcrc;

            bf_mgr_trace(LOG_INFO, 0x1fe5, "[HCRC] dtag: 0x%x, pda: 0x%x, hcrc[%d]: 0x%x", dtag.dtag, pda, j, hcrc);  //test tony
        }
        bf_mgr_trace(LOG_INFO, 0x3870, "[HCRC] dtag: 0x%x, pda: 0x%x, hcrc[%d]: 0x%x", dtag.dtag, pda, 0, hcrc_0);  //test tony
	}
    else
    {
		bf_mgr_trace(LOG_ERR, 0x1cec, "\nInvalid number of argument\n");
	}

	return 0;
}

static DEFINE_UART_CMD(get_hcrc, "get_hcrc",
		"get_hcrc [dtag] [pda] ",
		"read PDA & dtag from a 16 hex pda",
		1, 2, get_hcrc_console);

#endif

init_code void btn_hcrc_buf_init(void)
{
    u32 i;
	u32 j;
	for (i = 0; i < SRAM_IN_DTAG_CNT; i++) {
		for (j = 0; j < NR_LBA_PER_LDA; j++) {   //no replace NR_LBA_PER_LDA, because allocate MAX Buf
			dtag_t dtag;
			dtag.b.dtag = i;
			btn_hcrc_write(dtag, j, 0);
		}
	}
}

#endif // DISABLE_HS_CRC_SUPPORT

fast_code void __bm_isr_com_free(void)
{

#ifdef While_break
	u64 start = get_tsc_64();
#endif


	do
	{
		btn_um_int_sts_t sts = {.all = btn_readl(BTN_UM_INT_STS)};

		if (sts.b.com_free_dtag_update == 0)
			break;

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	} while (1);
}

fast_code void btn_wait_w_cmd_idle(void)
{
	btn_um_int_sts_t un_sts = {.all = btn_readl(BTN_UM_INT_STS)};
	extern __attribute__((weak)) void btn_w_cmd_in();
	extern __attribute__((weak)) void btn_wcmd_rls_isr();

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	do
	{

		if (un_sts.b.nvm_cmd_que2_has_cmd == 0 &&
			un_sts.b.rev_nvm_wr_cmd_rls == 0)
		{
			break;
		}
		if (btn_w_cmd_in)
			btn_w_cmd_in();
		if (btn_wcmd_rls_isr)
			btn_wcmd_rls_isr();

		btn_writel(un_sts.b.nvm_cmd_que2_has_cmd | un_sts.b.rev_nvm_wr_cmd_rls, BTN_UM_INT_STS);

		un_sts.all = btn_readl(BTN_UM_INT_STS);

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	} while (1);
}

fast_code void btn_wait_r_cmd_idle(void)
{
	btn_um_int_sts_t un_sts = {.all = btn_readl(BTN_UM_INT_STS)};
	extern __attribute__((weak)) void btn_nrd_cmd_in();
	extern __attribute__((weak)) void btn_prd_cmd_in();
	extern __attribute__((weak)) void btn_rcmd_rls_isr();

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	do
	{

		if (un_sts.b.nvm_cmd_que0_has_cmd == 0 &&
			un_sts.b.nvm_cmd_que1_has_cmd == 0 &&
			un_sts.b.rev_nvm_rd_cmd_rls == 0)
		{
			break;
		}
		if (btn_nrd_cmd_in)
			btn_nrd_cmd_in();
		if (btn_prd_cmd_in)
			btn_prd_cmd_in();
		if (btn_rcmd_rls_isr)
			btn_rcmd_rls_isr();

		btn_writel(un_sts.b.nvm_cmd_que0_has_cmd |
					   un_sts.b.nvm_cmd_que1_has_cmd |
					   un_sts.b.rev_nvm_rd_cmd_rls,
				   BTN_UM_INT_STS);

		un_sts.all = btn_readl(BTN_UM_INT_STS);
#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	} while (1);
}

fast_code void __btn_nrd_cmd_in(void)
{
#ifdef While_break
	u64 start = get_tsc_64();
#endif

	do
	{
		btn_um_int_sts_t un_sts = {.all = btn_readl(BTN_UM_INT_STS)};
		if (un_sts.b.nvm_cmd_que0_has_cmd == 0 &&
			un_sts.b.nvm_cmd_que0_has_cmd == 0)
		{
			break;
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif

	} while (1);
}

//fast_code void btn_abort_all(void)
ddr_code void btn_abort_all(void)
{
	btn_rcmd_abort();
	btn_wcmd_abort();
}

slow_code void btn_err_dtag_ret(dtag_t dtag)
{
	bm_pl_t pl = { .pl.dtag = dtag.dtag, };

	if (_btn_err_cbs.write_err)
		_btn_err_cbs.write_err(&pl);
}
extern volatile u8 plp_trigger;
extern bool ucache_flush_flag;
ddr_code u32 btn_feed_rd_dtag(void)
{
	btn_um_int_sts_t sts;
	dtag_t dtag = dtag_get_urgt(DTAG_T_SRAM, NULL);

	u32 cnt = 0;
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	btn_writel(RD_DTAG_REQS_PENDING_MASK, BTN_UM_INT_STS);

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	do {
		sts.all = btn_readl(BTN_UM_INT_STS);
		if (sts.b.rd_dtag_reqs_pending) {
			bm_free_aurl_load(&dtag, 1);
			cnt++;
			btn_writel(RD_DTAG_REQS_PENDING_MASK, BTN_UM_INT_STS);
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif

	} while (sts.b.rd_dtag_reqs_pending);

	dtag_put(DTAG_T_SRAM, dtag);
	return cnt;
}

fast_code void btn_handle_incoming_cmd(void)
{
	/* l2o_isr for unmap case */
	extern void l2p_isr_q0_srch(void);
	btn_um_int_sts_t isr_sts = { .all = (btn_readl(BTN_UM_INT_STS) & btn_dp_int_mask), };
	btn_um_int_sts_t isr_sts_bkp;

	bf_mgr_trace(LOG_WARNING, 0x3a2e, "collect all incoming WR nromal/error DE/s %x %x dpe_ctxs_allocated_cnt %u",
        isr_sts.all, btn_readl(BTN_UM_INT_STS), dpe_ctxs_allocated_cnt);

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (isr_sts.all)
	{
		bm_isr_slow();
		btn_data_in_isr();
		bm_isr_com_free();
		#if defined(RDISK)
		cpu_msg_isr();
		l2p_isr_q0_srch();
		#endif
		isr_sts.all = isr_sts_bkp.all = btn_readl(BTN_UM_INT_STS) & btn_dp_int_mask;
		isr_sts_bkp.all &= ~(BTN_FAST_INTR_MASK);
		if (!isr_sts_bkp.b.btn_data_process_done && !isr_sts_bkp.b.data_err_ll_not_empty)
		{
			bf_mgr_trace(LOG_WARNING, 0x12c1, "[BTN ] %x",isr_sts_bkp.all);
			break;
		}

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

	//sys_assert(isr_sts.all == 0);  //SDK with assert, axi parity error status can not be discard , it will always in this loop......
}

// ddr_code void btn_reset_pending(void)
// {
// 	// to check if any interrupt was not clear
// 	//bf_mgr_trace(LOG_ALW, 0, "2. wait on the fly L2P requests back");
//
//    // TODO cause hang
// 	//bm_free_wr_recycle();
//
// 	//btn_abort_all();
// }

// fast_code void btn_reset_pending(void)
// {
// 	btn_um_int_sts_t isr_sts = {
// 		.all = (btn_readl(BTN_UM_INT_STS) & btn_dp_int_mask),
// 	};
// 	//log_level_t old = log_level_chg(LOG_DEBUG);
// 	extern u8 evt_reset_disk;
// #if 0
// 	if (evt_reset_disk != 0xff)
// 		evt_set_imt(evt_reset_disk, 0, 0/*cancel wr credit */);
// #endif
//
// 	bf_mgr_trace(LOG_WARNING, 0, "wait bm idle %x", isr_sts.all);
// 	while (isr_sts.all)
// 	{
// 		bm_isr_slow();
// 		btn_data_in_isr();
// 		btn_wait_w_cmd_idle();
// 		btn_wait_r_cmd_idle();
// 		bm_isr_com_free();
//
// 		isr_sts.all = btn_readl(BTN_UM_INT_STS) & btn_dp_int_mask;
// 	}
// 	sys_assert(isr_sts.all == 0);
// 	// to check if any interrupt was not clear
// 	bf_mgr_trace(LOG_WARNING, 0, "2.wait bm idle %x", btn_readl(BTN_UM_INT_STS));
//
// 	// new step to clear bit[20] of unmasked BTN status
// 	btn_um_int_sts_t btn_sts = {.all = btn_readl(BTN_UM_INT_STS)};
// 	if (btn_sts.b.wr_dtag_reqs_pending)
// 	{
// 		// clear bit [20] by write 1
// 		btn_writel(WR_DTAG_REQS_PENDING_MASK, BTN_UM_INT_STS);
//
// 		btn_sts.all = btn_readl(BTN_UM_INT_STS);
//
// 		while (btn_sts.b.wr_dtag_reqs_pending)
// 		{
// 			extern u8 evt_dtag_ins;
// 			if (0xff != evt_dtag_ins)
// 				evt_set_imt(evt_dtag_ins, 0, 0);
//
// 			btn_sts.all = btn_readl(BTN_UM_INT_STS);
// 		}
// 	}
//
// 	// new while-loop to handle new coming data entry
// 	isr_sts.all = btn_readl(BTN_UM_INT_STS) & btn_dp_int_mask;
//
// 	while ((isr_sts.all) || !(is_cmd_proc_dma_idle()))
// 	{
// 		bm_isr_slow();
// 		btn_data_in_isr();
//
// 		isr_sts.all = btn_readl(BTN_UM_INT_STS) & btn_dp_int_mask;
// 	}
//
// #if !defined(RDISK)
// 	u32 i;
// 	dtag_llist_sts_data_t dtag_sts;
// 	// reclaim DTAGs in free queue
// 	btn_writel((u32)FREE_WR_DTAG_LLIST, DTAG_LLIST_STS_SEL);
// 	dtag_sts.all = btn_readl(DTAG_LLIST_STS_DATA);
//
// 	for (i = 0; i < dtag_sts.b.dtag_llist_v_count; i++)
// 	{
// 		dtag_t dtag = bm_pop_dtag_llist(FREE_WR_DTAG_LLIST);
// 		sys_assert(dtag.dtag != DTAG_INV);
// 		bm_pl_t bm_pl = {.all = dtag.dtag};
//
// 		if (_btn_err_cbs.write_err)
// 			_btn_err_cbs.write_err(&bm_pl);
// 	}
// 	bf_mgr_trace(LOG_WARNING, 0, "reset freew %d", dtag_sts.b.dtag_llist_v_count);
// #endif
//
// 	btn_abort_all();
//
// 	if (evt_reset_disk != 0xff)
// 		evt_set_imt(evt_reset_disk, 0, 0 /*cancel wr credit */);
// }

ps_code void btn_hmb_resume(void)
{
	u32 i;
	free_dtag_llist_push_t fdlp = {
		.b.dtag_llist_push_sel = FREE_DTAG_RD_HMB_LLIST,
	};
	for (i = 0; i < 8; i++)
	{
		dtag_t dtg = dtag_get(DTAG_T_SRAM, NULL);
		bf_mgr_trace(LOG_ALW, 0x0119, "assigned dtg HDR_DT_FREE_HMBS %x", dtg.b.dtag);
		fdlp.b.dtag_llist_push_data = dtg.b.dtag;
		btn_writel(fdlp.all, FREE_DTAG_LLIST_PUSH);
	}

	free_dtag_llist_push_t fdlp_ir = {
		.b.dtag_llist_push_sel = FREE_DTAG_INT_RD_HMB_LLIST,
	};

	for (i = 0; i < 8; i++)
	{
		dtag_t dtg = dtag_get(DTAG_T_SRAM, NULL);
		bf_mgr_trace(LOG_ALW, 0x0ae3, "assigned dtg HDR_DT_FREE_HMBIR %x", dtg.b.dtag);
		fdlp_ir.b.dtag_llist_push_data = dtg.b.dtag;
		btn_writel(fdlp_ir.all, FREE_DTAG_LLIST_PUSH);
	}

	free_dtag_llist_push_t fdlp_nr = {
		.b.dtag_llist_push_sel = FREE_DTAG_NVM_WR_HMB_LLIST,
	};

	for (i = 0; i < 8; i++)
	{
		dtag_t dtg = dtag_get(DTAG_T_SRAM, NULL);
		bf_mgr_trace(LOG_ALW, 0xac13, "assigned dtg HDR_DT_FREE_HMBNR %x", dtg.b.dtag);
		fdlp_nr.b.dtag_llist_push_data = dtg.b.dtag;
		btn_writel(fdlp_nr.all, FREE_DTAG_LLIST_PUSH);
	}
}

fast_code bool btn_io_queue_debug(void)
{
    nvm_cmd_queue0_ptrs_t rd0 = { .all = btn_readl(NVM_CMD_QUEUE0_PTRS) };
    nvm_cmd_queue1_ptrs_t rd1 = { .all = btn_readl(NVM_CMD_QUEUE1_PTRS) };
    nvm_cmd_queue2_ptrs_t wr0 = { .all = btn_readl(NVM_CMD_QUEUE2_PTRS) };
	if(rd0.all||rd1.all||wr0.all)
    	bf_mgr_trace(LOG_INFO, 0x7499, "[BTN] NR[%x] PR[%x] W[%x]", rd0.all, rd1.all, wr0.all);

    return true;
}

#define MIX_SECTOR_SIZE
extern u32 WUNC_DTAG;

ps_code void btn_core_resume(void)
{
	wd_entry_llist_ctrl_status_t llist_ctrl_sts;
	btn_control_reg_t btn_ctrl = {
		.all = btn_readl(BTN_CONTROL_REG),
	};

	dpe_init();

#if defined(DISABLE_HS_CRC_SUPPORT)
	btn_ctrl.b.host_sector_crc_en = 0;
#else
#if defined(DDR)
	sys_assert(ddr_hcrc_base);
	btn_writel(ddr_to_dma(ddr_hcrc_base) >> 5, D_BASE_D_DTAG_HSCRC);
#endif
	btn_ctrl.b.host_sector_crc_en = 1;
#endif

#if defined(USE_CRYPTO_HW)
	btn_ctrl.b.data_crypto_en = 1;
	crypto_init();
#else
	btn_ctrl.b.data_crypto_en = 0;
#endif

#if defined(HMETA_SIZE)
	btn_writel(sram_to_dma(dtag2mem(hmeta_dtag[DTAG_T_SRAM])) >> 5, S_BASE_S_DTAG_HMETA); // 32-byte based
#if defined(DDR)
	btn_writel(ddr_to_dma(dtag2mem(hmeta_dtag[DTAG_T_DDR])) >> 5, D_BASE_D_DTAG_HMETA); // 32-byte based
	u16 * p = (void *)(dtag2mem(hmeta_dtag[DTAG_T_DDR]) + HMETA_SIZE*8*WUNC_DTAG);
    for(u8 i = 0 ;i < 8; i++){
        memset((void*)p,0xFF,HMETA_SIZE);
        *p = 0;
        p += (HMETA_SIZE >> 1);
    }
#endif
	//btn_ctrl.b.host_meta_enbale = 1;
	//btn_ctrl.b.host_meta_enbale = 0;//joe test 20210125
	//btn_ctrl.b.host_meta_16byte = (HMETA_SIZE > 8) ? 1 : 0;
	//btn_ctrl.b.host_meta_size_mix = (HMETA_SIZE > 8) ? 1 : 0;
#endif //HMETA_SIZE

	/*lint -e506 -save allow constant value boolean */
	//btn_ctrl.b.host_sector_size = (HLBASZ == 12) ? 1 : 0;//joe change sec 20200817
	btn_ctrl.b.host_sector_size = (host_sec_bitz == 12) ? 1 : 0;
	btn_ctrl.b.data_unit_size = (DTAG_SHF == 13) ? 1 : 0;
	/*lint -restore */
	btn_ctrl.b.read_auto_release_en = 1; /* enable Streaming mode by default */
	/* enable btn hmb buffer access */
#if defined(HMB_DTAG)
	btn_ctrl.b.hmb_databuf_en = 1;
#if !defined(DISABLE_HS_CRC_SUPPORT)
	btn_ctrl.b.hmb_sectorcrc_en = 1;
#endif
#endif

#if defined(ENABLE_SRAM_ECC)
	/* enable SRAM ECC check feature */
	btn_ctrl.b.sram_ecc_check_en = 1;
	/* clear internal error bit [31:28] of bm_ctrl_status register */
	u32 err = SRAM_WR_ECC_ERR_MASK | SRAM_RD_ECC_ERR1_MASK;
	btn_writel(err, BTN_ERROR_STATUS);
#else
	btn_ctrl.b.sram_ecc_check_en = 0;
#endif
#if defined(FAST_MODE)
	bm_ctrlr_sts.b.fast_read_mode = 1;
#endif

#if defined(MIX_SECTOR_SIZE)
	btn_ctrl.b.host_sector_size_mix = 1;
#endif

	bm_unmap_ctrl(true, BTN_UNMAP_PAT, 0);
	bm_unmap_ctrl(true, BTN_UNMAP_ZERO_PAT, 1);
	btn_writel(btn_ctrl.all, BTN_CONTROL_REG);

	read_dummy_ctrl_reg_t rd_dummy_ctrl;
	rd_dummy_ctrl.b.bm_read_dummy_enable = 1;
	rd_dummy_ctrl.b.read_dummy_dtag_num = EVTAG_ID;
	btn_writel(rd_dummy_ctrl.all, READ_DUMMY_CTRL_REG);

	read_dummy_data_pat_t rd_dummy_data_pat;
	rd_dummy_data_pat.b.read_dummy_data_pat = BTN_DUM_PAT;
	btn_writel(rd_dummy_data_pat.all, READ_DUMMY_DATA_PAT);

	llist_ctrl_sts.all = btn_readl(WD_ENTRY_LLIST_CTRL_STATUS);
#if (HOST_NVME_FEATURE_HMB == FEATURE_SUPPORTED)
	llist_ctrl_sts.b.hmb_wd_stream_en = 1;
#endif
	btn_writel(llist_ctrl_sts.all, WD_ENTRY_LLIST_CTRL_STATUS);

	/* enable task event */
	poll_mask(VID_PROC_DONE);
	poll_mask(VID_BTN_OTHER);

	/* clear all bm interrupts before enabling it */
	btn_writel(btn_readl(BTN_UM_INT_STS), BTN_UM_INT_STS);
}

init_code void btn_core_init(void)
{
#if !defined(DISABLE_HS_CRC_SUPPORT)
    btn_hcrc_buf_init();
	btn_hcrc_init();
#endif

#if defined(HMETA_SIZE)
	btn_hmeta_init();
#endif
	// btn_dp_int_mask =
	// 	NVM_CMD_QUE0_HAS_CMD_MASK |
	// 	NVM_CMD_QUE1_HAS_CMD_MASK |
	// 	NVM_CMD_QUE2_HAS_CMD_MASK |
	// 	NVM_CMD_QUE3_HAS_CMD_MASK |
	// 	btn_core_int_mask |
	// 	REV_NVM_RD_CMD_RLS_MASK |
	// 	REV_NVM_WR_CMD_RLS_MASK |
	// 	WR_DATA_GROUP0_UPDATE_MASK |
	// 	WR_DATA_GROUP1_UPDATE_MASK |
	// 	RD_DATA_GROUP0_UPDATE_MASK |
	// 	COM_FREE_DTAG_UPDATE_MASK;

// TODO Need confirm with IG
		btn_dp_int_mask =
			/* NVM_CMD_QUE0_HAS_CMD_MASK |
			NVM_CMD_QUE1_HAS_CMD_MASK |
			NVM_CMD_QUE2_HAS_CMD_MASK |
			NVM_CMD_QUE3_HAS_CMD_MASK | */
			btn_core_int_mask |
			/* REV_NVM_RD_CMD_RLS_MASK |
			REV_NVM_WR_CMD_RLS_MASK | */
			WR_DATA_GROUP0_UPDATE_MASK |
			WR_DATA_GROUP1_UPDATE_MASK |
			RD_DATA_GROUP0_UPDATE_MASK |
			COM_FREE_DTAG_UPDATE_MASK;

	poll_irq_register(VID_PROC_DONE, bm_isr_slow);
	poll_irq_register(VID_BTN_OTHER, bm_isr_slow);
	btn_enable_isr(btn_core_int_mask);

#if defined(DISABLE_BM_RD_ERR)
	btn_disable_isr(INT_BIT_RD_DATA_ERR);
#endif
#if defined(MPC)
	cpu_msg_register(CPU_MSG_BTN_SEMI_WAIT_SWITCH, ipc_btn_semi_wait_switch);
#endif
}

#if !defined(DISABLE_HS_CRC_SUPPORT)
static ddr_code int dump_hcrc_main(int argc, char *argv[])
{
	dtag_t dtag = {.dtag = strtol(argv[1], (void *)0, 0)};
	u16 hcrc;
	u32 i;

	bf_mgr_trace(LOG_ERR, 0xc545, "dtag %x: ", dtag.dtag);
	u8 lba_cnt_per_lba = 4096 / host_sec_size; //joe add sec size 20200817
	//for (i = 0; i < NR_LBA_PER_LDA; i++) {
	for (i = 0; i < lba_cnt_per_lba; i++)
	{
		hcrc = btn_hcrc_read(dtag, i);
		bf_mgr_trace(LOG_ERR, 0x1b74, "%x ", hcrc);
	}
	bf_mgr_trace(LOG_ERR, 0x00f6, "\n");
	return 0;
}

static DEFINE_UART_CMD(dump_hcrc, "dump_hcrc",
					   "dump hcrc",
					   "synctx: dump_hcrc [dtag_id in hex]",
					   1, 1, dump_hcrc_main);
#endif

ddr_code int axi_error_dump_main(int argc, char *argv[])
{
    HalAxiError_DumpRegister();
    return 0;
}

static DEFINE_UART_CMD(axi_error, "axi_error", "axi_error", "axi_error", 0, 0, axi_error_dump_main);

#endif
/*! @} */
