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
/*! \file ncl.c
 * @brief NCL interface for misc operation
 *
 * \addtogroup ncl_20
 * \defgroup ncl
 * \ingroup ncl_20
 *
 * @{
 */
#include "ndcu.h"
#include "ndphy.h"
#include "nand.h"
#include "eccu.h"
#include "ficu.h"
#include "ncl.h"
#include "nand_cfg.h"
#include "ncl_cmd.h"
#include "eccu_reg_access.h"
#include "ncb_eccu_register.h"
#include "ndcu_reg_access.h"
#include "srb.h"
#include "./feature/ncl_onfi_dcc_training.h"
/*! \cond PRIVATE */
#define __FILEID__ ncl
#include "trace.h"
/*! \endcond */
extern u32 ndphy_dll_calibration_enhance(u8 ch,u8 ce,u8 lun);

fast_data bool ncl_cold_boot = true;		///< NCL cold boot or warm boot indicator
extern u32 fcmd_rw_outstanding_cnt;
extern u32 fcmd_outstanding_cnt;
#ifdef DBG_NCL_SET_FEA_BE4_READ
extern struct ncl_cmd_t setfea_ncl_cmd[SETFEA_NCL_CMD_MAX_CNT];
extern u8 setfea_nclcmd_cnt;
#endif

/*!
 * @brief ncl empty check API
 *
 * @param rw	if check read or write only
 *
 * @return	ncl empty (NCB idle and NCL idle) or not
 */
ddr_code bool ncl_cmd_empty(bool rw)
{
	if (!rw)
		return (fcmd_outstanding_cnt != 0) ? false : true;
	else
		return (fcmd_rw_outstanding_cnt != 0) ? false : true;

//	if (!QSIMPLEQ_EMPTY(&erd_waiting_queue)) {
//		return false;
//	}

	return true;
}

/*!
 * @brief Hex value (0-15) print, for better readability of defect map, 0 is printed as '_'
 *
 * @param value	Hex value
 *
 * @return	not used
 */
fast_code void hex_digit_print(u32 value)
{
	if (value == 0) {
		ncb_ncl_trace(LOG_ERR, 0x46b5, "_");
	} else if (value < 10) {
		ncb_ncl_trace(LOG_ERR, 0x90a2, "%c", '0' + value);
	} else {
		ncb_ncl_trace(LOG_ERR, 0x57bf, "%c", 'A' - 10 + value);
	}
}

/*!
 * @brief SPB defect scan API
 *
 * @param spb		SPB index
 * @param defect	Defect map pointer for this SPB
 *
 * @return	not used
 */

#define SLC_ScanDefect 0

fast_code void ncl_spb_defect_scan(u32 spb, u32 *defect)
{
    //ncb_ncl_trace(LOG_INFO, 0, "enter ncl_spb_defect_scan");
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info;
	pda_t base_pda;
	u32 nr_loop, pg, nr_pg;
#if !defined(FAST_MODE)
	bool bad_spb = false;
#endif

#if DEFECT_MARK_LAST_PAGE
	nr_pg = 2;
#else
	nr_pg = 1;
#endif

#if DEFECT_MARK_USER
	nr_loop = 2;
#else
	nr_loop = 1;
#endif
	for (pg = 0; pg < nr_pg; pg++) {
		// Multi-plane read command stage
		ncl_cmd.op_code = NCL_CMD_READ_REFRESH;
		ncl_cmd.status = 0;
		ncl_cmd.completion = NULL;
		ncl_cmd.addr_param.common_param.list_len = 1;
#if defined(USE_TSB_NAND) || defined(USE_SNDK_NAND)
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
#else
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
#endif
		ncl_cmd.addr_param.common_param.info_list = &info;
		ncl_cmd.addr_param.common_param.pda_list = &base_pda;
		if (pg == 0) {
			base_pda = spb << nand_pda_block_shift();
            base_pda |= 0x07; //refresh read flag for single cpu do
		} else {
//#if defined(USE_TSB_NAND) || defined(USE_SNDK_NAND)
#if SLC_ScanDefect
			base_pda = (spb << nand_pda_block_shift()) + (nand_page_num_slc() - 1) * nand_interleave_num() * DU_CNT_PER_PAGE;
#else
			base_pda = (spb << nand_pda_block_shift()) + (nand_page_num() - 1) * nand_interleave_num() * DU_CNT_PER_PAGE;
#endif
            base_pda |= 0x07;
		}
        ncl_cmd_submit(&ncl_cmd);
        base_pda &= (~0x07);
#ifdef FAST_MODE
		ndcu_fast_mode(false);
		eccu_fast_mode(false);
#endif
		// Random data out defect mark
		u32 offset = 0;
		u32 lun;//ch
#ifdef FAST_MODE
		for (ch = 0; ch < max_channel; ch++) {
#else
		for (lun = 0; lun < nand_lun_num(); lun++) {  // (ch = 0; ch < nand_channel_num(); ch++)
#endif
			u32 ce;
			for (ce = 0; ce < nand_target_num(); ce++) { // (ce = 0; ce < nand_target_num(); ce++)
				u32 ch; //lun
				for (ch = 0; ch < nand_channel_num(); ch++) {  //(lun = 0; lun < nand_lun_num(); lun++)
					ncl_cmd.op_code = NCL_CMD_READ_DEFECT;
					ncl_cmd.completion = NULL;
					ncl_cmd.addr_param.fda_param.list_len = 1;
					ncl_cmd.addr_param.fda_param.info_list = &info;
					info.pb_type = NAL_PB_TYPE_XLC;
//#if defined(USE_TSB_NAND) || defined(USE_SNDK_NAND)
#if SLC_ScanDefect
					info.pb_type = NAL_PB_TYPE_SLC;
#endif
					u32 pl;
					for (pl = 0; pl < nand_plane_num(); pl++) {
						u32 page;
						if (pg ==0) {
							page = 0;
						} else {
//#if defined(USE_TSB_NAND) || defined(USE_SNDK_NAND)
#if SLC_ScanDefect
							page = nand_page_num_slc() - 1;
#else
							page = nand_page_num() - 1;
#endif
						}
						u32 row = row_assemble(lun, pl, spb, page);
						ncl_cmd.status = 0;
//#if defined(USE_TSB_NAND) || defined(USE_SNDK_NAND)
#if SLC_ScanDefect
						ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
#else
						ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
#endif
						ncl_cmd.addr_param.fda_param.ch = ch;
						ncl_cmd.addr_param.fda_param.ce = ce;
						ncl_cmd.addr_param.fda_param.row = row;
						ncl_cmd_submit(&ncl_cmd);

						bool is_defect = false;
						struct param_fifo_format* param_fifo;
						u32 i;
						for (i = 0; i < nr_loop; i++) {
							param_fifo = get_param_fifo();
							if ((param_fifo->param[0] & 0xFF) != 0xFF) {
								is_defect = true;
#if !defined(FAST_MODE)
								bad_spb = true;
#endif
							}
						}
						u32 idx, off;
#ifdef FAST_MODE
						u32 offset_in_virtual_ch = offset % nand_interleave_num();
						idx = offset_in_virtual_ch >> 5;
						off = offset_in_virtual_ch & 0x1F;
#else
						idx = offset >> 5;
						off = offset & 0x1F;
#endif
						if (is_defect) {
							defect[idx] |= 1 << off;
#ifdef FAST_MODE
							ncb_ncl_trace(LOG_WARNING, 0x2e2c, "SPB %d Blk off %d is defect", spb, offset_in_virtual_ch);
#else
							//ncb_ncl_trace(LOG_INFO, 0, "SPB %d Blk off %d is defect, ch %d ce %d lun %d pl %d", spb, offset, ch, ce, lun, pl);
#endif
						} else {
#ifndef FAST_MODE
							/* Good block */
							defect[idx] &= ~(1 << off);
#endif
						}
						offset++;
					}
				}
			}
		}
#ifdef FAST_MODE
		ndcu_fast_mode(true);
		eccu_fast_mode(true);
#endif
	}

#if !defined(FAST_MODE)
	if (bad_spb) {
		
	if((spb < 25) || (spb > shr_nand_info.geo.nr_blocks - 25))  //AlanCC
	{	
		ncb_ncl_trace(LOG_ERR, 0x321a, "SPB %d:", spb);
		u32 pl_per_ce;
		pl_per_ce = nand_lun_num() * nand_plane_num();
		u32 ch;
		u32 offset;
		offset = 0;
		for (ch = 0; ch < nand_channel_num(); ch++) {
			u32 ce;
			u32 idx;
			u32 off;
			for (ce = 0; ce < nand_target_num(); ce++) {
				off = offset & 0x1F;
				idx = offset >> 5;
				if (pl_per_ce == 2) {
					hex_digit_print((defect[idx] >> off) & 0x3);
				} else {
					u32 cnt = pl_per_ce;
					while (cnt) {
						hex_digit_print((defect[idx] >> off) & 0xF);
						cnt -= min(cnt, 4);
						off += 4;
					}
				}
				offset += pl_per_ce;
			}
			ncb_ncl_trace(LOG_ERR, 0x803e, " | ");
		}
		ncb_ncl_trace(LOG_ERR, 0x0a3c, "\n");
	}
	}
#endif
}

u32 ncl_polling_status(pda_t pda)
{
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 du_dtag_ptr;
	u32 fcmd_id;
	u32 is_sts_fail;
	struct info_param_t info;
	struct ncl_cmd_t ncl_cmd;

	//ncl_cmd = sys_malloc(SLOW_DATA, sizeof(struct ncl_cmd_t));
	ncl_cmd.completion = NULL;
	ncl_cmd.op_code = NCL_CMD_OP_POLLING_STATUS;
	ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
	//ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
	ncl_cmd.flags = 0;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = &info;
	ncl_cmd.status = 0;

	du_dtag_ptr = ncl_acquire_dtag_ptr();
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->pda = pda;
	ncl_cmd.dtag_ptr = du_dtag_ptr;

	extern struct finstr_format status_fins_tmpl;
	ins = status_fins_tmpl;

	ins.dw0.b.fins = 1;
	ins.dw0.b.lins = 1;
	ins.dw0.b.poll_dis= 0;
	//ins.dw0.b.ndcmd_fmt_sel= FINST_NAND_FMT_READ_STATUS;
	fcmd_id = du_dtag_ptr;

	ins.dw1.b.fcmd_id = fcmd_id;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;

	ncl_cmd_save(fcmd_id, &ncl_cmd);
	ficu_fill_finst(ins);
	ficu_submit_finstr(1);

	extern u32 fcmd_outstanding_cnt;
	fcmd_outstanding_cnt++;
	SET_NCL_BUSY();
	//ncb_ncl_trace(LOG_ERR, 0, "ncl_polling_status : cmd %x, ncl_cmd->status %x\n", &ncl_cmd, ncl_cmd.status);

	//while(NCL_CMD_PENDING(ncl_cmd))
	while(!(ncl_cmd.flags & NCL_CMD_COMPLETED_FLAG))
	{
		ficu_done_wait();
	}

	if(ncl_cmd.status & NCL_CMD_ERROR_STATUS)
	{
		//ncl_cmd_trace(LOG_NCL_ERR, 0, "ncl_polling_status: Flash report Err Status");
		is_sts_fail=true;
	}
	else
	{
		//ncl_cmd_trace(LOG_NCL_ERR, 0, "ncl_polling_status: Flash report Done");
		is_sts_fail=false;
	}

	return is_sts_fail;
}

/*!
 * @brief FICU mode set feature and set feature by LUN API
 *
 * @param pda	PDA address to decice CH, CE, LUN
 * @param fa	Feature address
 * @param val	feature value
 *
 * @return	not used
 */
fast_code void ncl_set_feature(pda_t pda, u32 fa,bool by_lun, u32 val)
{
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info;
	struct raw_column_list sf_addr;

	// Multi-plane read command stage
	ncl_cmd.op_code = NCL_CMD_SET_GET_FEATURE;
	ncl_cmd.flags = 0;
    if (by_lun) {
	ncl_cmd.flags |= NCL_CMD_FEATURE_LUN_FLAG;
	}
	ncl_cmd.status = 0;
	ncl_cmd.completion = NULL;
	ncl_cmd.addr_param.rw_raw_param.list_len = 1;
	ncl_cmd.addr_param.rw_raw_param.info_list = &info;
	ncl_cmd.addr_param.rw_raw_param.pda_list = &pda;
	ncl_cmd.addr_param.rw_raw_param.column = &sf_addr;
	ncl_cmd.sf_val = val;
	sf_addr.column = fa;
	ncl_cmd_submit(&ncl_cmd);
}

#ifdef DBG_NCL_SET_FEA_BE4_READ
fast_code void set_feature_dbg(pda_t pda, u32 val)
{
	//struct ncl_cmd_t ncl_cmd;
	struct info_param_t info;
	struct raw_column_list sf_addr;
    u32 fa;
    u32 pg_idx = pda2pg_idx(pda);
	// Multi-plane read command stage
	setfea_ncl_cmd[setfea_nclcmd_cnt].op_code = NCL_CMD_SET_GET_FEATURE;
	setfea_ncl_cmd[setfea_nclcmd_cnt].flags = 0;
	setfea_ncl_cmd[setfea_nclcmd_cnt].flags |= NCL_CMD_FEATURE_LUN_FLAG;
	setfea_ncl_cmd[setfea_nclcmd_cnt].status = 0;
	setfea_ncl_cmd[setfea_nclcmd_cnt].completion = NULL;
	setfea_ncl_cmd[setfea_nclcmd_cnt].addr_param.rw_raw_param.list_len = 1;
	setfea_ncl_cmd[setfea_nclcmd_cnt].addr_param.rw_raw_param.info_list = &info;
	setfea_ncl_cmd[setfea_nclcmd_cnt].addr_param.rw_raw_param.pda_list = &pda;
	setfea_ncl_cmd[setfea_nclcmd_cnt].addr_param.rw_raw_param.column = &sf_addr;
	setfea_ncl_cmd[setfea_nclcmd_cnt].sf_val = val;
    //if (nand_support_aipr() && (pda2plane(ncl_cmd->addr_param.rapid_du_param.pda) == 1))
    if (nand_support_aipr() && (pda2plane(pda) == 1))
    {
    // When AIPR enabled, for plane 1 ,89h/8Ah/8Bh changed to 85h/86h/87h
        fa = Retry_feature_addr[pg_idx] - 4;
    } 
    else 
    {
        fa = Retry_feature_addr[pg_idx];
    }
	sf_addr.column = fa;
    ncl_cmd_submit(&setfea_ncl_cmd[setfea_nclcmd_cnt]);
    if(setfea_nclcmd_cnt >= (SETFEA_NCL_CMD_MAX_CNT-1))
    {
        setfea_nclcmd_cnt = 0;
        //ncl_cmd_submit(&setfea_ncl_cmd[SETFEA_NCL_CMD_MAX_CNT-1]);
    }
    else
    {
        setfea_nclcmd_cnt++;
        //ncl_cmd_submit(&setfea_ncl_cmd[setfea_nclcmd_cnt-1]);
    }
	//ncl_cmd_submit(&setfea_ncl_cmd[setfea_nclcmd_cnt]);
}

fast_code void set_feature_be4_read_dbg(struct ncl_cmd_t *ncl_cmd, u32 val)  //for DBG use, val = shift level val
{
    pda_t *pda_list = ncl_cmd->addr_param.common_param.pda_list;
    u32 pda_cnt = ncl_cmd->addr_param.common_param.list_len;
    u32 i;
    //u32 val = 0x80808080;
    for(i = 0; i < pda_cnt; i++)
    {
        set_feature_dbg(pda_list[i], val);
    }
}
#endif

/*!
 * @brief FICU mode get feature and get feature by LUN API
 *
 * @param pda		PDA address to decice CH, CE, LUN
 * @param fa		Feature address
 * @param by_lun	Get feature or get feature by LUN
 *
 * @return	get feature value
 */
fast_code u32 ncl_get_feature(pda_t pda, u32 fa, bool by_lun)
{
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info;
	struct raw_column_list sf_addr;

	// Multi-plane read command stage
	ncl_cmd.op_code = NCL_CMD_SET_GET_FEATURE;
	ncl_cmd.flags = NCL_CMD_FEATURE_GET_FLAG;
	if (by_lun) {
		ncl_cmd.flags |= NCL_CMD_FEATURE_LUN_FLAG;
	}
	ncl_cmd.status = 0;
	ncl_cmd.completion = NULL;
	ncl_cmd.addr_param.rw_raw_param.list_len = 1;
	ncl_cmd.addr_param.rw_raw_param.info_list = &info;
	ncl_cmd.addr_param.rw_raw_param.pda_list = &pda;
	ncl_cmd.addr_param.rw_raw_param.column = &sf_addr;
	sf_addr.column = fa;
	ncl_cmd_submit(&ncl_cmd);
	struct param_fifo_format* ptr = get_param_fifo();
	return ptr->param[0];
}

/*!
 * @brief Meta buffer base address configure API
 *
 * @param base_addr	Meta buffer base address
 * @param type		Meta buffer type
 *
 * @return	not used
 */
init_code void ncl_set_meta_base(void *base_addr, enum meta_base_type type)
{
	u32 addr = (u32) base_addr;
	ficu_meta_dtag_map_sram_base_addr_reg_t meta_dtag_sram_base;

	sys_assert(((u32)addr & 0xF) == 0);
	if (addr >= DDR_BASE) {
		addr -= DDR_BASE;
		addr = addr >> 4;
		addr |= BIT(31);
	} else if (addr >= SRAM_BASE) {
		addr = addr >> 4;
	} else if (addr == 0) {
		// clear meta register
	} else {
		panic("not support");
	}
	meta_dtag_sram_base.b.ficu_meta_dtag_map_sram_base_addr = addr;

	switch (type) {
	case META_DTAG_SRAM_BASE:
		ficu_writel(meta_dtag_sram_base.all, FICU_META_DTAG_MAP_SRAM_BASE_ADDR_REG);
		break;
	case META_DTAG_DDR_BASE:
		ficu_writel(meta_dtag_sram_base.all, FICU_META_DTAG_MAP_DDR_BASE_ADDR_REG);
		break;
	case META_IDX_SRAM_BASE:
		ficu_writel(meta_dtag_sram_base.all, FICU_META_IDX_MAP_SRAM_BASE_ADDR_REG);
		break;
	case META_IDX_DDR_BASE:
		ficu_writel(meta_dtag_sram_base.all, FICU_META_IDX_MAP_DDR_BASE_ADDR_REG);
		break;
	case META_DTAG_HMB_BASE:
		ficu_writel(meta_dtag_sram_base.all, FICU_META_DTAG_MAP_HMB_BASE_ADDR_REG);
		break;
	case META_IDX_HBM_BASE:
		ficu_writel(meta_dtag_sram_base.all, FICU_META_IDX_MAP_HMB_BASE_ADDR_REG);
		break;
	default:
		sys_assert(0);
		break;
	}
}

/*!
 * @brief Get nand geometry API
 *
 * @return	nand geometry
 */
fast_code struct target_geo_t* ncl_get_geo(void)
{
	return &nand_info.geo;
}


/*!
 * @brief wait all running command done and stop to issue any command
 * Refer to http://10.10.0.17/projects/tacoma_fw/wiki/NCL_API
 *
 * @return	not used
 */
fast_code void ncl_cmd_wait_completion(void)
{
	// Block newly incoming cmd
	ncl_cmd_wait = true;

	// Wait current cmd completion
	while(!IS_NCL_IDLE()) {
		ficu_done_wait();
	}
}

extern bool in_ficu_isr;
fast_code void ncl_cmd_wait_finish(void)
{
       if(ncl_busy[NRM_SQ_IDX] == true && in_ficu_isr == false) {
               ficu_done_wait();
       }
}


/*!
 * @brief block all incoming commands
 *
 * @return	not used
 */
fast_code void ncl_cmd_block(void)
{
	ncl_cmd_wait = true;
	//ncl_cmd_start_block(BLOCK_REASON_BTN_RESET);
	ncb_ncl_trace(LOG_ERR, 0x9ed6, "[TODO] ncl_cmd_block");

}

/*!
 * @brief resume all pending commands
 *
 * @return	not used
 */
fast_code void ncl_handle_pending_cmd(void)
{
	struct ncl_cmd_t* ncl_cmd;
	struct ncl_cmd_t* next_cmd;

	// Disable block newly incoming cmd
	ncl_cmd_wait = false;
	ncb_ncl_trace(LOG_ERR, 0x59ed, "ncl_handle_pending_cmd");
	// Restart blocked cmd
	QSIMPLEQ_FOREACH_SAFE(ncl_cmd, &ncl_cmds_wait_queue, entry, next_cmd) {
		QSIMPLEQ_REMOVE(&ncl_cmds_wait_queue, ncl_cmd, ncl_cmd_t, entry);
		if (ncl_cmd->flags & NCL_CMD_RAPID_PATH) {
			ncl_cmd_rapid_single_du_read(ncl_cmd);
		} else {
			ncl_cmd_submit(ncl_cmd);
		}
	}
}

fast_code int ncl_get_temperature(pda_t pda)
{
#if HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT
	u32 ch, ce;
#endif /* HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT */
	u32 t_min = 0xFF, t_max = 0xFF;
	int t;
#if HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT || HAVE_MICRON_SUPPORT
	bool by_lun = true;
	u32 val_verify;
#endif

#if HAVE_MICRON_SUPPORT
	u32 feature_addr;

	feature_addr = 0xE7;//micron get temperature address
	if (by_lun) {
		feature_addr <<= 8;
	}

	//extern u32 ficu_get_feature(ch, ce, feature_addr, by_lun);

	//for (ch = 0; ch < nand_channel_num(); ch++) {
		//for (ce = 0; ce < nand_target_num(); ce++) {
			//val_verify = ficu_get_feature(ch, ce, feature_addr, by_lun);
			val_verify = ncl_get_feature(pda, feature_addr, by_lun);

			t = (u32)((val_verify & 0x7F) - 37);
			if (t_min == 0xFF) {
				t_max = t;
				t_min = t;
			} else if (t > t_max) {
				t_max = t;
			} else if (t < t_min) {
				t_min = t;
			}
		//}
	//}
#endif
#if HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT
	nf_rdcmd_dummy_t rdcmd_reg_bak, rdcmd_reg;
	rdcmd_reg.all = ndcu_readl(NF_RDCMD_DUMMY);
	rdcmd_reg_bak.all = rdcmd_reg.all; // backup
	rdcmd_reg.b.r_dumy_cmd0 = 0x7C; // NAND_TSB_GET_TEMP_7C
	ndcu_writel(rdcmd_reg.all, NF_RDCMD_DUMMY);
	ch = pda2ch(pda);
	ce = pda2ce(pda);

	extern u32 ficu_get_tempature(u8 ch, u8 ce, bool by_lun);

	//for (ch = 0; ch < nand_channel_num(); ch++) {
		//for (ce = 0; ce < nand_target_num(); ce++) {
			val_verify = ficu_get_tempature(ch, ce, by_lun);
			t = (u32)((val_verify & 0xFF) - 42);
			if (t_min == 0xFF) {
				t_max = t;
				t_min = t;
			} else if (t > t_max) {
				t_max = t;
			} else if (t < t_min) {
				t_min = t;
			}
		//}
	//}

	ndcu_writel(rdcmd_reg_bak.all, NF_RDCMD_DUMMY); // restore
#endif

	t = t_min < 0 ? t_min : t_max;
	//ncb_ncl_trace(LOG_ERR, 0, "\n\n\nt %d, t_max %d, t_min %d\n", t, t_max, t_min);
	return t;
}

#if defined(PROGRAMMER)
u8	ndphy_dll_cali[16][8][4];	/* ndphy dll value per ch/ce/lun */
#endif

/*!
 * @brief Drive strength & DLL phase calibration
 *
 * @return	not used
 */
fast_code void ncl_dll_calibration_enhance(void)
{
	u8 ch,ce,lun;
	bool fail = false;
	#if 0
	if (nand_is_bics4_TH58LJT1V24BA8H())  // 8DP 256Gb (LJ1 PCB0D 4T)
		ncb_ncl_trace(LOG_ERR, 0x7cb2, "nand_is_bics4_TH58LJT1V24BA8H\n");
	else if (nand_is_bics4_TH58TFT2T23BA8J()) // 8DP 512Gb (LJ1 PCB0D 8T)
		ncb_ncl_trace(LOG_ERR, 0xdd81, "nand_is_bics4_TH58TFT2T23BA8J\n");
	else if (nand_is_bics4_16DP()) // 16DP 256Gb (LJ1 PCB0B 4T)
		ncb_ncl_trace(LOG_ERR, 0x3bd5, "nand_is_bics4_16DP\n");
	else if (nand_is_bics4_800mts()) // 16DP 512Gb (LJ1 PCB0B 8T)
		ncb_ncl_trace(LOG_ERR, 0xbcc0, "nand_is_bics4_800mts\n");
  #endif

	do {
		u32 idx_meta = ficu_readl(FICU_META_IDX_MAP_SRAM_BASE_ADDR_REG);
		dtag_t metad;
		void *meta;

		if (idx_meta == 0) {
			metad = dtag_get(DTAG_T_SRAM, &meta);
			if (metad.dtag == _inv_dtag.dtag)
				break;

			ncl_set_meta_base(meta, META_IDX_SRAM_BASE);
		}

		for (ch = 0; ch < nand_channel_num(); ch++) {
#ifdef U2
			if (nand_is_bics4_TH58LJT1V24BA8H() || nand_is_bics4_TH58TFT2T23BA8J()){
				//ndphy_odt_level(ch, 7);  // 8DP max ODT
				ndphy_odt_level(ch, 3);  // 75 ohm
			}
			else
				ndphy_odt_level(ch, 3);// 50 ohm for 16DP T0
#endif
#ifdef M2
			ndphy_drv_str_level(ch, NDPHY_DRVSTR_6_35OHM);// 35 ohm
#else
			if (nand_is_bics4_TH58LJT1V24BA8H() || nand_is_bics4_TH58TFT2T23BA8J()){
				ndphy_drv_str_level(ch, 15);  // 8DP max driving
			}
			else
			  ndphy_drv_str_level(ch, 12);// 25 ohm for  16DP  T0
#endif
			//ndphy_odt_level(ch, NDPHY_ODT3_75OHM);// 75 ohm
			for (ce = 0; ce < nand_target_num(); ce++) {
				for (lun = 0;lun < nand_lun_num(); lun++) {
					u32 dll_val;
					u32 best_result = ~0;
					srb_t *srb = (srb_t *)SRB_HD_ADDR;
					if ((srb->srb_hdr.srb_signature == SRB_SIGNATURE) && (srb->ndphy_dll_valid)) {
						best_result = (u32)srb->ndphy_dll_set[ch][ce][lun];
						//ncb_ncl_trace(LOG_ERR, 0, "CH %d ce %d lun %d DLL %b from SRB\n", ch,ce,lun, best_result);
					} else {
						log_level_chg(LOG_ALW);
						dll_val = ndphy_dll_calibration_enhance(ch,ce,lun);
						if (dll_val < best_result) {
							best_result = dll_val;
						}
						log_level_chg(LOG_INFO);
						if (best_result == ~0) {
							ncb_ncl_trace(LOG_ERR, 0x37a6, "CH %d ce %d lun %d DLL cali fail!\n", ch,ce,lun);
							fail = true;
						} else if ((dll_val >> 24) == 0) {// Find window of 0 error bit
							ncb_ncl_trace(LOG_ERR, 0xbac3, "CH %d ce %d lun %d DLL %b[%b-%b]\n", ch,ce,lun, best_result & 0xFF, (best_result>>16) & 0xFF, (best_result>>8) & 0xFF);
						} else {
							// ncb_ncl_trace(LOG_ERR, 0, "CH %d ce %d lun %d DLL %b[%b-%b], err bit %d\n", ch,ce,lun, best_result & 0xFF, (best_result>>16) & 0xFF, (best_result>>8) & 0xFF, best_result >> 24);
						}
#if defined(PROGRAMMER)
						ndphy_dll_cali[ch][ce][lun] = best_result & 0x3F;
#endif
					}
					ndphy_set_dll_phase_enhance(ch,ce,lun, best_result & 0x3F);
				}
			}
		}
		nf_dll_updt_ctrl_reg0_t reg0;
		reg0.all = ndcu_readl(NF_DLL_UPDT_CTRL_REG0);
		reg0.b.nf_dll_updt_en = 0x3;
		ndcu_writel(reg0.all, NF_DLL_UPDT_CTRL_REG0);
		nphy_ctrl_reg0_t reg1;
		for (ch = 0; ch < nand_channel_num(); ch++) {
			reg1.all = ndphy_readl(ch, NPHY_CTRL_REG0);
			reg1.b.nphy_io_strb_en = 1;
			ndphy_writel(ch, reg1.all, NPHY_CTRL_REG0);
		}
		if (idx_meta == 0) {
			dtag_put(DTAG_T_SRAM, metad);
			ncl_set_meta_base(0, META_IDX_SRAM_BASE);
		}
	} while (0);
	if (fail) {
		sys_assert(0);
	}
}


/*!
 * @brief NCL module initialization
 *
 * @return	not used
 */
init_code void ncl_init(void)
{
#ifdef OTF_MEASURE_TIME
	global_time_start[5] = get_tsc_64();
#endif	
#if !defined(RDISK) && defined(FPGA)
	if (!soc_cfg_reg2.b.nvm_en) {// H2K bitfile, no NVME
		extern void btn_resume(enum sleep_mode_t mode);
		btn_resume(SUSPEND_INIT);
	}
#endif
	eccu_conf_reg_t cfg = { .all = eccu_readl(ECCU_CONF_REG), };

	if (cfg.b.ch_num < max_channel) {
		max_channel = cfg.b.ch_num;
	}
	if (cfg.b.ce_num < max_target) {
		max_target = cfg.b.ce_num;
	}

	//log_level_chg(LOG_INFO);
	ndcu_init();
	eccu_init();
	ficu_init();

	ncl_cold_boot = false;

	ncl_cmd_init();

#ifndef ONFI_DCC_TRAINING
#if (OTF_TIME_REDUCE == ENABLE)
	if (!misc_is_warm_boot())
#endif
	{
		nand_dcc_training();
	}

#if 0
	extern void ncl_backend_only_perf_test(void);
	ncl_backend_only_perf_test();
	sys_assert(0);
#endif
#if !defined(FPGA)
	ncl_dll_calibration_enhance();
#endif
#else
	srb_t *srb = (srb_t *)SRB_HD_ADDR;
	for (u32 ch = 0; ch < nand_channel_num(); ch++) {        
        if (srb->DLL_Auto_Tune == 0x55)
        {
            ndphy_odt_level(ch, srb->ndphy_odt[ch]);
            ndphy_drv_str_level(ch, srb->ndphy_drv[ch]);
    		srb->DLL_margin_ndphy_odt = srb->ndphy_odt[ch];
    		srb->DLL_margin_ndphy_drv = srb->ndphy_drv[ch];
        }
        else
        {
            if(nand_is_bics5_TH58LKT1Y45BA8C())
    		{
    			ndphy_odt_level(ch, NDPHY_ODT0_150OHM);// 50 ohm
    			ndphy_drv_str_level(ch, NDPHY_DRVSTR_6_35OHM);// 35 ohm
    			srb->DLL_margin_ndphy_odt = NDPHY_ODT0_150OHM;
    			srb->DLL_margin_ndphy_drv = NDPHY_DRVSTR_6_35OHM;
    		}
    		else //bics4 etlc 16dp
    		{
    			ndphy_odt_level(ch, NDPHY_ODT0_150OHM);// 50 ohm
    			ndphy_drv_str_level(ch, NDPHY_DRVSTR_12_25OHM);// 25 ohm
    			srb->DLL_margin_ndphy_odt = NDPHY_ODT0_150OHM;
    			srb->DLL_margin_ndphy_drv = NDPHY_DRVSTR_12_25OHM;
    		}
		}
	}


	bool ret = false;
	u8 tunning_retry = 0;
	do {
#if TACOMAX
		ndphy_io_strobe_enable(false);
#endif
		ret = nand_data_training();
		if (ret) {
			tunning_retry++;
			printk("<tunning retry times %d>", tunning_retry);
		}
		else{
			ncb_ncl_trace(LOG_INFO, 0xdf01, "nand data training done");	
            ncb_ncl_trace(LOG_INFO, 0x9cf3, "DLL_Auto_Tune: 0x%x, ndphy_odt: %d, ndphy_drv: %d, nand_odt: %d, nand_drv: %d", srb->DLL_Auto_Tune, srb->DLL_margin_ndphy_odt, srb->DLL_margin_ndphy_drv, srb->DLL_margin_nand_odt, srb->DLL_margin_nand_drv);	
        }
        
	} while(ret && (tunning_retry < 6));

#endif

#if NCL_STRESS
	log_level_chg(LOG_INFO);
	extern void self_stress_test_slc_random(void);
	self_stress_test_slc_random();
#endif

	ncb_ncl_trace(LOG_INFO, 0xb5f1, "NCL init done");
#if NCL_STRESS
	while(1);
#endif
	// HW initialization
	ncl_pmu_init();

#ifdef DBG_NCL_SET_FEA_BE4_READ
    setfea_nclcmd_cnt = 0;
    u8 i;
    for(i = 0; i < SETFEA_NCL_CMD_MAX_CNT; i++)
    {
        memset(&setfea_ncl_cmd[i], 0, sizeof(struct ncl_cmd_t));
    }
#endif

#ifdef OTF_MEASURE_TIME
	evlog_printk(LOG_ALW,"[M_T]NCL init %8d us\n", time_elapsed_in_us(global_time_start[5]));
#endif

}

#if (!defined(MPC) || (CPU_ID == CPU_BE) || (CPU_ID == CPU_BE2))
/*! \cond PRIVATE */
module_init(ncl_init, NCL_APL);
/*! \endcond */
#endif
/*! @} */
