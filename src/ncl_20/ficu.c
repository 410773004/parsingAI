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
/*! \file ficu.c
 * @brief FICU initialization and driver file
 *
 * \addtogroup ncl_20
 * \defgroup ficu
 * \ingroup ncl_20
 *
 * @{
 */

#include "ficu.h"
#include "ncb_eccu_register.h"
#include "eccu_reg_access.h"
#include "ncb_ficu_register.h"
#include "ncl_err.h"
#include "finstr.h"
#include "ndcu.h"
#include "nand.h"
#include "eccu.h"
#include "bf_mgr.h"
#include "ncl_erd.h"
#include "ncl_cmd.h"
#include "die_que.h"
#include "GList.h"
#include "types.h" //Tony 20200924
#include "../ftl/ErrorHandle.h"
#include "nand_tsb.h"
#include "dec_top_ctrl_register.h"
#include "ncl.h"
#include "read_retry.h"
/*! \cond PRIVATE */
#define __FILEID__ ficu
#include "trace.h"
/*! \endcond */
#define XFER_CNT_TABLE_LEN 10
#define FINSTR_SIZE 16

/// CQ in DTCM or not
fast_data_zi bool cq_in_dtcm = false;
fast_data_ni struct ficu_cq_entry* ficu_cq_ptr;
fast_data_zi bool ficu_cq_phase = 0;
/// Select normal/high/urgent SQ
fast_data_zi u8 ficu_sq_select;
fast_data_ni struct param_fifo_format* param_fifo;
fast_data_zi bool pda_format = false;
fast_data_zi u32 ard_mode = 0;
extern u16 ua_btag;

fast_data_ni dtcm_cq_t dtcm_cq0;
fast_data_ni dtcm_cq_t dtcm_cq1;
fast_data_ni struct dtcm_cq_entry dtcm_cq_grp0[FSPM_CQ_FIFO_COUNT] __attribute__ ((aligned(8)));
fast_data_ni struct dtcm_cq_entry dtcm_cq_grp1[FSPM_CQ_FIFO_COUNT] __attribute__ ((aligned(8)));

#if ONFI_DCC_TRAINING
extern fast_data u32 DccReadInProcess;
#endif
#if NCL_USE_DTCM_SQ
fast_data_ni dtcm_cq_t dtcm_sq_info[6];
fast_data_ni struct finstr_format dtcm_sq_grp0[DTCM_SQ_FIFO_COUNT] __attribute__ ((aligned(16)));
fast_data_ni struct finstr_format dtcm_hsq_grp0[DTCM_HSQ_FIFO_COUNT] __attribute__ ((aligned(16)));
fast_data_ni struct finstr_format dtcm_usq_grp0[DTCM_USQ_FIFO_COUNT] __attribute__ ((aligned(16)));
fast_data_ni struct finstr_format dtcm_sq_grp1[DTCM_SQ_FIFO_COUNT] __attribute__ ((aligned(16)));
fast_data_ni struct finstr_format dtcm_hsq_grp1[DTCM_HSQ_FIFO_COUNT] __attribute__ ((aligned(16)));
fast_data_ni struct finstr_format dtcm_usq_grp1[DTCM_USQ_FIFO_COUNT] __attribute__ ((aligned(16)));
#endif

/// FINST transfer count table
norm_ps_data u16 xfcnt_tbl[XFER_CNT_TABLE_LEN] = {
	0, //  FINST_XFER_ZERO
	1,	//  FINST_XFER_1B
	2,	//  FINST_XFER_2B
	4,	//  FINST_XFER_4B
	8,	//  FINST_XFER_8B
	16,	//  FINST_XFER_16B
	0,	//  FINST_XFER_PAGE_PAD
	0,	//  place holder
	0,	//  place holder
	0,	//  place holder
};

/// initialization fspm usage pointer to SPRM_BASE
fast_data struct fspm_usage_t *fspm_usage_ptr = \
		(struct fspm_usage_t *)SPRM_BASE;

#ifndef DUAL_BE
slow_data_zi struct sram_finst_info_t sram_finst_info __attribute__ ((aligned(32)));
fast_data_zi struct dtcm_finst_info_t dtcm_finst_info;

/// initialization finst_fifo structure value to default value
fast_data struct finst_fifo_t finst_fifo = {
	.fifo_start = (struct finstr_format*)SQ_START,
	.fifo_end = (struct finstr_format*)SQ_END,
};
#else
slow_data_zi struct sram_finst_info_t _sram_finst_info __attribute__ ((aligned(32)));
share_data struct sram_finst_info_t *sram_finst_info;
fast_data_zi struct dtcm_finst_info_t dtcm_finst_info;
share_data volatile struct finst_fifo_t finst_fifo[2];
#endif

#if NCL_USE_DTCM_SQ
fast_data_ni struct dtcm_sq_t dtcm_sq;
#endif

#if 0//def ERRHANDLE_GLIST	
share_data_zi bool  fgFail_Blk_Full;        // need define in sram
share_data_zi sGLTable *pGList;             // need define in Dram  // GL_mod, Paul_20201130
share_data_zi u16   wOpenBlk[MAX_OPEN_CNT]; // need define in Dram
share_data_zi u8    bErrOpCnt;
share_data_zi u8    bErrOpIdx;
share_data_zi bool  FTL_INIT_DONE;
share_data_zi u32	EPM_LBLK; 
share_data_zi ts_tmt_t ts_tmt;

share_data_zi sGLEntry errInfo2;
share_data_zi sGLEntry errInfo4;
share_data_zi sEH_Manage_Info sErrHandle_Info;  
share_data_zi MK_FailBlk_Info Fail_Blk_Info_Temp;
share_data_zi u8 bFail_Blk_Cnt;                               //need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];

share_data_zi u16 ps_open[2];

#endif

#if 0 //def ERRHANDLE_ECCT
share_data u16 ecct_cnt;
#endif
/// Set the ARD occur flag of ncl commnad to 1 by fcmd id
extern void ncl_fcmd_ard_occur(u32 fcmd_id);
/// Execute the ncl completion function by fcmd id
extern void ncl_fcmd_completion(u32 fcmd_id);
/// Get the error PDA by fcmd id and set the relative error type flag.
extern void ncl_fcmd_pda_error(u32 fcmd_id, pda_t pda, u32 plane, enum ncb_err err);
extern struct ncl_cmd_t* ncl_cmd_retrieve(u32 fcmd_id);

/*!
 * @brief Reset the FIFO pointer
 *
 * @return Not used
 */
fast_code void ficu_fifo_sw_reset(void)
{
#ifndef DUAL_BE
	finst_fifo.fifo_ptr = finst_fifo.fifo_end- 1;
	finst_fifo.fifo_rptr = finst_fifo.fifo_end - 1;
#else
	finst_fifo[0].fifo_ptr = finst_fifo[0].fifo_end- 1;
	finst_fifo[0].fifo_rptr = finst_fifo[0].fifo_end - 1;

	finst_fifo[1].fifo_ptr = finst_fifo[1].fifo_end- 1;
	finst_fifo[1].fifo_rptr = finst_fifo[1].fifo_end - 1;
#endif

	ficu_cq_phase = 0;
	ficu_cq_ptr = fspm_usage_ptr->cq_fifo;
}

/*!
 * @brief Get an FINST slot to fill
 *
 * @return FIFO pointer
 */
fast_code u32* ficu_get_finstr_slot(void)
{
#if NCL_USE_DTCM_SQ
	u32 wr_ptr = dtcm_sq.wr_ptr;
	dtcm_sq.wr_ptr = (dtcm_sq.wr_ptr + 1) & dtcm_sq.max_sz; // point to next entry
	while (dtcm_sq.wr_ptr == *dtcm_sq.rd_ptr); // Check SQ full

	return (u32*)(&dtcm_sq.base_addr[wr_ptr]);
#else
#ifdef DUAL_BE
	finst_fifo[NRM_SQ_IDX].fifo_ptr++;
	if (finst_fifo[NRM_SQ_IDX].fifo_ptr >= finst_fifo[NRM_SQ_IDX].fifo_end)
		finst_fifo[NRM_SQ_IDX].fifo_ptr = finst_fifo[NRM_SQ_IDX].fifo_start;

	// Remain 1 entry unused for full check
	struct finstr_format *next = finst_fifo[NRM_SQ_IDX].fifo_ptr + 1;
	if (next >= finst_fifo[NRM_SQ_IDX].fifo_end)
		next = finst_fifo[NRM_SQ_IDX].fifo_start;

	while (next == finst_fifo[NRM_SQ_IDX].fifo_rptr) {
		u32 ptr;
		u32 regs[2] = {FICU_FSPM_NORM_FINST_QUE0_PTR_REG, FICU_FSPM_NORM_FINST_QUE1_PTR_REG};

		ptr = ficu_readl(regs[NRM_SQ_IDX]);
		if (ptr == 0) {
			finst_fifo[NRM_SQ_IDX].fifo_rptr = finst_fifo[NRM_SQ_IDX].fifo_end - 1;
		} else {
			finst_fifo[NRM_SQ_IDX].fifo_rptr = finst_fifo[NRM_SQ_IDX].fifo_start + (ptr/sizeof(struct finstr_format)) - 1;
		}
	}

	return (u32*)finst_fifo[NRM_SQ_IDX].fifo_ptr;
#else
	finst_fifo.fifo_ptr++;
	if (finst_fifo.fifo_ptr == finst_fifo.fifo_end) {
		finst_fifo.fifo_ptr = finst_fifo.fifo_start;
	}

	struct finstr_format *next = finst_fifo.fifo_ptr + 1;
	if (next >= finst_fifo.fifo_end)
		next = finst_fifo.fifo_start;
	// Remain 1 entry unused for full check
	while (next == finst_fifo.fifo_rptr) {
		u32 ptr;
		ptr = ficu_readl(FICU_FSPM_NORM_FINST_QUE0_PTR_REG);
		if (ptr == 0) {
			finst_fifo.fifo_rptr = finst_fifo.fifo_end - 1;
		} else {
			finst_fifo.fifo_rptr = finst_fifo.fifo_start + (ptr/sizeof(struct finstr_format)) - 1;
		}
	}
	return (u32*)finst_fifo.fifo_ptr;
#endif
#endif
}
#if NCL_USE_DTCM_SQ
/*!
 * @brief Update DTCM SQ write pointer to FICU
 *
 * @return Not used
 */
fast_code void ficu_update_sq_wrptr(void)
{
	ficu_norm_finst_que0_ptrs_t finst_que_ptrs;
	finst_que_ptrs.b._norm_finst_que0_wr_ptr = dtcm_sq.wr_ptr;
	ficu_writel(finst_que_ptrs.all, dtcm_sq.ptrs_reg);
}
#endif
/*!
 * @brief Get FDA by dtag pointer
 *
 * @param[in] du_dtag_ptr		DU datg pointer
 *
 * @return FDA dtag pointer
 */
fast_code struct fda_dtag_format *ficu_get_addr_dtag_ptr(u32 du_dtag_ptr)
{
#if FINST_INFO_IN_SRAM

#ifndef DUAL_BE
	struct sram_finst_info_t *sfi = &sram_finst_info;
#else
	struct sram_finst_info_t *sfi = sram_finst_info;
#endif

	return &sfi->fda_dtag_list[du_dtag_ptr];
#else
	return &dtcm_finst_info.fda_dtag_list[du_dtag_ptr];
#endif
}

/*!
 * @brief Dump FINST structure value
 *
 * @param[in] ins		FINST format pointer
 *
 * @return Not used
 */
fast_code void dump_finstr(struct finstr_format* ins)
{
	u32 du_dtag_ptr;
	struct fda_dtag_format *fda_dtag;
	ncb_ficu_trace(LOG_ERR, 0x7e1c, "inst @ 0x%x: 0x%x 0x%x 0x%x 0x%x ", ins, ins->dw0.all, ins->dw1.all, ins->dw2.all, ins->dw3.all);

	du_dtag_ptr = ins->dw1.b.du_dtag_ptr;

	bm_pl_t* bm_pl;
	u32 addr;

	du_dtag_ptr &= (1 << 10) - 1;
	if (ins->dw1.b.finst_info_loc) {
		addr = ficu_readl(FICU_FDA_DTAG_LIST_SRAM_BASE_ADDR0_REG) + du_dtag_ptr * sizeof(struct fda_dtag_format);
	} else {
		addr = ficu_readl(FICU_FDA_DTAG_LIST_DTCM_BASE_ADDR0_REG) + du_dtag_ptr * sizeof(struct fda_dtag_format);
	}
	fda_dtag = (struct fda_dtag_format*)addr;
	if (ins->dw0.b.raw_addr_en) {
		ncb_ficu_trace(LOG_ERR, 0x8a8b, "fda hdr 0x%x, row 0x%x, ", fda_dtag->hdr, fda_dtag->row);
	} else if ((ins->dw0.b.mp_num != 0) && (ins->dw0.b.mp_row_offset_en == 0)) {
		ncb_ficu_trace(LOG_ERR, 0xb00b, "pda list 0x%x, ", fda_dtag->pda_list);
	} else {
#if HAVE_HW_CPDA
		ncb_ficu_trace(LOG_ERR, 0x5acc, "cpda 0x%x, ", fda_dtag->pda);
#else
		ncb_ficu_trace(LOG_ERR, 0x6667, "pda 0x%x, ", fda_dtag->pda);
#endif
	}
	if (ins->dw1.b.xfcnt_sel != FINST_XFER_AUTO) {
		ncb_ficu_trace(LOG_ERR, 0x1c08, "\n");
		return;
	}

	if (ins->dw2.b.du_num == 0) {
		ncb_ficu_trace(LOG_ERR, 0xe20d, "dtag 0x%x, ", (u32)(fda_dtag->dtag.all & 0xFFFFFFFF));
		bm_pl = &fda_dtag->dtag;
		// Meta usage dump, referred to http://10.10.0.17/issues/1511
		switch(bm_pl->pl.type_ctrl & 3) {
		case 0:
			ncb_ficu_trace(LOG_ERR, 0xc42d, "meta sram dtag(%x) map\n", bm_pl->pl.dtag);
			break;
		case 1:
			ncb_ficu_trace(LOG_ERR, 0x7d80, "meta sram idx(%x) map\n", bm_pl->pl.nvm_cmd_id);
			break;
		case 2:
			ncb_ficu_trace(LOG_ERR, 0x10fc, "meta ddr dtag(%x) map\n", bm_pl->pl.dtag);
			break;
		case 3:
			ncb_ficu_trace(LOG_ERR, 0xeb22, "meta ddr idx(%x) map\n", bm_pl->pl.nvm_cmd_id);
			break;
		}
	} else {
		if (fda_dtag->dtag_ptr >= SRAM_BASE) {
			ncb_ficu_trace(LOG_ERR, 0x9192, "dtag ptr 0x%x, ", fda_dtag->dtag_ptr);
		} else {
			ncb_ficu_trace(LOG_ERR, 0x05b5, "dtag ptr 0x%x, ", dma_to_btcm(fda_dtag->dtag_ptr));
		}
		bm_pl = (bm_pl_t*)fda_dtag->dtag_ptr;
		u32 i;
		ncb_ficu_trace(LOG_ERR, 0x90ba, "meta ");
		for (i = 0; i <= ins->dw2.b.du_num; i++) {
			// Meta usage dump, referred to http://10.10.0.17/issues/1511
			switch(bm_pl[i].pl.type_ctrl & 3) {
			case 0:
				ncb_ficu_trace(LOG_ERR, 0x7817, "sram dtag(%x)", bm_pl[i].pl.dtag);
				break;
			case 1:
				ncb_ficu_trace(LOG_ERR, 0xce6c, "sram idx(%x)", bm_pl[i].pl.nvm_cmd_id);
				break;
			case 2:
				ncb_ficu_trace(LOG_ERR, 0x7e6f, "ddr dtag(%x)", bm_pl[i].pl.dtag);
				break;
			case 3:
				ncb_ficu_trace(LOG_ERR, 0x8b08, "ddr idx(%x)", bm_pl[i].pl.nvm_cmd_id);
				break;
			}
		}
		ncb_ficu_trace(LOG_ERR, 0xb239, " map\n");
	}
}

/*!
 * @brief Transfer a CPDA to PDA
 *
 * @param[in] cpda		CPDA
 *
 * @return value(PDA)
 */
fast_code pda_t cpda_to_pda(pda_t cpda)
{
	pda_t pda;
	u32 ch, ce, lun, plane, block, page, du;

	// CPDA to ch, ce, lun, pl, blk, pg, du
	du = cpda & (DU_CNT_PER_PAGE - 1);
	cpda >>= DU_CNT_SHIFT;
	plane = cpda & (nand_info.geo.nr_planes- 1);
	cpda >>= ctz(nand_info.geo.nr_planes);
	lun = cpda & (nand_info.geo.nr_luns - 1);
	cpda >>= ctz(nand_info.geo.nr_luns);
	ce = cpda % nand_info.geo.nr_targets;
	cpda /= nand_info.geo.nr_targets;
	ch = cpda % nand_info.geo.nr_channels;
	cpda /= nand_info.geo.nr_channels;
	page = cpda % nand_info.geo.nr_pages;
	block = cpda / nand_info.geo.nr_pages;

	// ch, ce, lun, pl, blk, pg, du to PDA
	pda = block << nand_info.pda_block_shift;
	pda |= page << nand_info.pda_page_shift;
	pda |= ch << nand_info.pda_ch_shift;
	pda |= ce << nand_info.pda_ce_shift;
	pda |= lun << nand_info.pda_lun_shift;
	pda |= plane << nand_info.pda_plane_shift;
	pda |= du << nand_info.pda_du_shift;

	return pda;
}

/*!
 * @brief Transfer a PDA to CPDA
 *
 * @param[in] pda		PDA
 *
 * @return value(CPDA)
 */
fast_code pda_t pda_to_cpda(pda_t pda)
{
	pda_t cpda;
	u32 ch, ce, lun, plane, block, page, du;

	// Split each fields
	ch = (pda >> nand_info.pda_ch_shift) & (nand_info.geo.nr_channels - 1);
	ce = (pda >> nand_info.pda_ce_shift) & (nand_info.geo.nr_targets - 1);
	lun = (pda >> nand_info.pda_lun_shift) & (nand_info.geo.nr_luns - 1);
	plane = (pda >> nand_info.pda_plane_shift) & (nand_info.geo.nr_planes - 1);
	block = (pda >> nand_info.pda_block_shift) & nand_info.pda_block_mask;
	page = (pda >> nand_info.pda_page_shift) & nand_info.pda_page_mask;
	du = (pda >> nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);

	// Construct CPDA
	cpda = block;
	cpda *= nand_page_num();
	cpda += page;
	cpda *= nand_info.geo.nr_channels;
	cpda += ch;
	cpda *= nand_info.geo.nr_targets;
	cpda += ce;
	cpda *= nand_info.geo.nr_luns;
	cpda += lun;
	cpda *= nand_info.geo.nr_planes;
	cpda += plane;
	cpda *= DU_CNT_PER_PAGE;
	cpda += du;
	return cpda;
}

#if HAVE_HW_CPDA
/*!
 * @brief Transfer a CPDA to PDA
 *
 * @param[in] cpda		CPDA
 *
 * @return value(PDA)
 */
fast_code pda_t cpda2pda(pda_t cpda)
{
	if (pda_format) {
		return cpda;
	}
	return cpda_to_pda(cpda);
}
#endif

#if DEBUG
/*!
 * @brief Dump FINST structure value
 *
 * @return Not used
 */
fast_code void ficu_finst_dump(void)
{
#if NCL_USE_DTCM_SQ
	struct finstr_format* inst;
	u32 rd_ptr = *dtcm_sq.rd_ptr;

	while(1) {
		inst = (struct finstr_format*)&dtcm_sq.base_addr[rd_ptr];
		dump_finstr(inst);
		rd_ptr = (rd_ptr + 1) & dtcm_sq.max_sz;
		if (rd_ptr == dtcm_sq.wr_ptr) {
			break;
		}
	}
#else

    
    
        
        // Print whole fifo finst.
    
	struct finstr_format* inst;

	while(1) {
		finst_fifo.fifo_rptr++;
		if (finst_fifo.fifo_rptr == finst_fifo.fifo_end) {
			finst_fifo.fifo_rptr = finst_fifo.fifo_start;
		}
		inst = (struct finstr_format*)finst_fifo.fifo_rptr;
		dump_finstr(inst);
		if (finst_fifo.fifo_rptr == finst_fifo.fifo_ptr) {
			break;
		}
	}
	
	
#endif
}

// DBG_EH, mark for finst error, Paul_20201214

#endif

/*!
 * @brief Dump FINST structure value
 *
 * @param[in] du_dtag_ptr	DU dtag pointer
 *
 * @return Not used
 */
void fast_code ficu_dump_fcmd(u32 du_dtag_ptr)
{
#if NCL_USE_DTCM_SQ
	struct finstr_format* inst;
	u32 wr_ptr = dtcm_sq.wr_ptr;

	// Search backwards to find the specific Finst
	do {
		wr_ptr = (wr_ptr - 1) & dtcm_sq.max_sz;
		inst = &dtcm_sq.base_addr[wr_ptr];
	} while((inst->dw1.b.du_dtag_ptr & 0x3FF) != du_dtag_ptr);

	// Search backwards to find the beginning of the FCMD
	while(!inst->dw0.b.fins) {
		wr_ptr = (wr_ptr - 1) & dtcm_sq.max_sz;
		inst = &dtcm_sq.base_addr[wr_ptr];
	}

	// Dump each Finst and finst info
	while (1) {
		dump_finstr(inst);
		if (inst->dw0.b.lins) {
			break;
		}
		wr_ptr = (wr_ptr + 1) & dtcm_sq.max_sz;
		inst = &dtcm_sq.base_addr[wr_ptr];
	}
#else
#ifndef DUAL_BE
	struct finstr_format* inst;

	inst = finst_fifo.fifo_ptr;
	// Search backwards to find the specific Finst
	while((inst->dw1.b.du_dtag_ptr & 0x3FF) != du_dtag_ptr) {
		if (inst == finst_fifo.fifo_start) {
			inst = finst_fifo.fifo_end - 1;
		} else {
			inst--;
		}
	}
	// Search backwards to find the beginning of the FCMD
	while(!inst->dw0.b.fins) {
		if (inst == finst_fifo.fifo_start) {
			inst = finst_fifo.fifo_end - 1;
		} else {
			inst--;
		}
	}
	// Dump each Finst and finst info
	while (1) {
		dump_finstr(inst);
		if (inst->dw0.b.lins) {
			break;
		}
		inst++;
		if (inst == finst_fifo.fifo_end) {
			inst = finst_fifo.fifo_start;
		}
	}
#endif
#endif
}

extern void ficu_status_regs_check(u32 pda);

/*!
 * @brief FCMD error handler
 *
 * @param[in] int_src	interrupt source register value
 *
 * @return Not used
 */
#ifdef ERRHANDLE_GLIST	
slow_code void Fill_Error_Info(pda_t pda, u32 fcmd_id, u32 err)	// Save code size, Paul_20210715
{
	
    sGLEntry* errInfo = NULL;
	u32 ch_shift = 0, ce_shift = 0, lun_shift = 0, pl_shift = 0, LUN = 0, CH = 0, CE = 0, plane = 0;
	struct ncl_cmd_t* ncl_cmd;
    #if 0//RAID_SUPPORT_UECC
    struct ncl_cmd_t* rc_ncl_cmd = NULL;
    die_cmd_info_t *rc_die_cmd_info = NULL;
    #endif
    ncl_w_req_t *ncl_req = NULL;
    die_cmd_info_t *die_cmd_info = NULL;
	pl_shift = nand_info.pda_interleave_shift;				//2
	ch_shift = pl_shift + ctz(nand_info.geo.nr_planes); 	//3
	ce_shift = ch_shift + ctz(nand_info.geo.nr_channels);	//6
	lun_shift = ce_shift + ctz(nand_info.geo.nr_targets);	//9

	CH	= (pda >> ch_shift) & (nand_info.geo.nr_channels - 1);
	CE	= (pda >> ce_shift) & (nand_info.geo.nr_targets - 1);
	LUN = (pda >> lun_shift) & (nand_info.geo.nr_luns - 1); 
    plane = (pda >> pl_shift) & (nand_info.geo.nr_planes - 1);
	//shr_nand_info.pda_ce_shift
	
	ncl_cmd = (struct ncl_cmd_t*)ncl_cmd_retrieve(fcmd_id);
    sys_assert(ncl_cmd);
    if((ncl_cmd->op_code == NCL_CMD_OP_READ) || (ncl_cmd->op_code == NCL_CMD_PATROL_READ) || (ncl_cmd->op_code == NCL_CMD_OP_READ_STREAMING_FAST) 
        #ifdef NCL_FW_RETRY_BY_SUBMIT
        || (ncl_cmd->op_code == NCL_CMD_FW_RETRY_READ)
        #endif
        || (ncl_cmd->op_code == NCL_CMD_OP_READ_FW_ARD)
        )
    {
        #if 0//RAID_SUPPORT_UECC
        if(ncl_cmd->flags & NCL_CMD_RCED_FLAG)      
        {
            rc_ncl_cmd = (struct ncl_cmd_t*)ncl_cmd->caller_priv;
            rc_die_cmd_info = (die_cmd_info_t *)rc_ncl_cmd->caller_priv;
        }
        else
        #endif
        {
            die_cmd_info = (die_cmd_info_t *)ncl_cmd->caller_priv; // For Read ncl_cmd only
        }
    }
    else    
        ncl_req = (ncl_w_req_t *)ncl_cmd->caller_priv; // For Write ncl_cmd only, Paul_20201202
    
#if (CPU_ID != CPU_BE) //(ncl_req->req.op_type.b.remote)
	//extern sGLEntry errInfo4;
	//memset((void*)&errInfo4, 0, sizeof(errInfo4));
	#if 1
	if (((Err_Info_Wptr + 1) & (Err_Info_size - 1)) == Err_Info_Rptr)
    {
        //sys_assert(0);              //the q is full
        ncb_ficu_trace(LOG_ALW, 0xeceb, "Err_Info Q full");
        return;
    }
    else
    #endif
    {
        errInfo = &Err_Info_Cpu4_Addr[Err_Info_Wptr];
		memset((void*)errInfo, 0, sizeof(sGLEntry));	// Init flags, DBG, PgFalVry
        errInfo->CPU4_Flag = 1;
        Err_Info_Wptr = ((Err_Info_Wptr + 1) & (Err_Info_size - 1));
    }
#else
	memset((void*)&errInfo2, 0, sizeof(errInfo2));
	errInfo = &errInfo2;
#endif

	errInfo->AU 			 = pda & (DU_CNT_PER_PAGE - 1);
	errInfo->bDie			 = nand_info.geo.nr_channels * nand_info.geo.nr_targets * LUN + nand_info.geo.nr_channels * CE + CH;	
	errInfo->bTemper		 = ts_tmt.cur_ts;
	//errInfo->dLBlk_EC		 = 0x10;
	//errInfo->dLBlk_POH     = 0x10;
	//errInfo->dLBlk_RdCnt	 = 0x10;
	//errInfo->dLBlk_SN		 = 0x10;
	//errInfo->dLBlk_flags	 = 0x10;

	
	//errInfo->Mark_Bad		 = false;
	//errInfo->NeedGC 		 = false;
	//errInfo->RD_2BFail     = false;
	//errInfo->RD_RecvFail	 = false;
	errInfo->wLBlk_Idx		 = pda >> nand_info.pda_block_shift;
	errInfo->wPage			 = ((pda >> nand_info.pda_page_shift) & (nand_info.pda_page_mask));
	errInfo->wPhyBlk		 = errInfo->wLBlk_Idx * nand_info.geo.nr_planes + plane;


    #if 0   // Paul_20201202
	if (ncl_cmd->op_code == NCL_CMD_OP_WRITE)
	{
		errInfo->bError_Type	 = GL_PROG_FAIL;	
	}
	else if (ncl_cmd->op_code == NCL_CMD_OP_ERASE)
	{
		errInfo->bError_Type	 = GL_ERASE_FAIL;															   
	}
    else if(ncl_cmd->op_code == NCL_CMD_OP_READ)  //tony 20200930
    {
        if(err == cur_du_ucerr)
        {
            errInfo->bError_Type     = GL_1BREAD_FAIL;
        }
        else
        {
            ncb_ficu_trace(LOG_ERR, 0x6975, "[ERR] other NAND Error case OP Type: %d, err status: %d", ncl_cmd->op_code, err); //tony 20201006                
            return;
        }
    }
	else
	{
		//ncb_ficu_trace(LOG_ERR, 0, "[ERR]other NAND Error case OP Type:%d\n", ncl_cmd->op_code);
		ncb_ficu_trace(LOG_ERR, 0x5ccf, "[ERR] other NAND Error case OP Type: %d, err status: %d", ncl_cmd->op_code, err); //tony 20201006
        return;
	}
	//ftl blk	
    if (ncl_req->req.op_type.b.ftl)
    {
        errInfo->RD_Ftl = true;
    }
    
    #else
    switch (ncl_cmd->op_code)
    {
        case NCL_CMD_OP_READ:   // 0
        case NCL_CMD_PATROL_READ:
        case NCL_CMD_OP_READ_STREAMING_FAST:
        case NCL_CMD_OP_READ_FW_ARD:
		#ifdef NCL_FW_RETRY_BY_SUBMIT	// DBG, SMARTVry
		case NCL_CMD_FW_RETRY_READ:	
		#endif
        {
            #if 1
            if(err == ficu_err_1bit_retry_err)   // 13
            {
                if(ncl_cmd->rty_blank_flg)
                    errInfo->Blank = 1;
                else if(ncl_cmd->rty_dfu_flg)
                    errInfo->DFU = 1;
                    
                if(ncl_cmd->flags & NCL_CMD_RCED_FLAG)
            	{
					errInfo->bError_Type = GL_RECV_1BREAD_FAIL;
					#if RAID_SUPPORT_UECC
					//if(!rc_die_cmd_info->cmd_info.b.host)
					//if((ncl_cmd->uecc_type == NCL_UECC_RC_FTL) || (ncl_cmd->uecc_type == NCL_UECC_RC_L2P))
					if(ncl_cmd->uecc_type == NCL_UECC_RC_L2P)  //NCL_UECC_RC_FTL not use after FTL_INIT_DONE
						errInfo->RD_Ftl = 1;
					#endif
            	}
				else
				{
	                if(ncl_cmd->flags & NCL_CMD_P2L_READ_FLAG)
	                {
	                    errInfo->bError_Type = GL_P2L_1BREAD_FAIL;
	                }
                    #if RAID_SUPPORT_UECC
	                else if(ncl_cmd->flags & NCL_CMD_L2P_READ_FLAG) //Just QBT & PBT can reg GList at Power on, other SPOR read was block on interrupt. 
	                {
						errInfo->bError_Type = GL_FTL_1BREAD_FAIL;
                        errInfo->RD_Ftl = 1;
                    }
                    #endif
                    else
                    {
                        if(die_cmd_info->cmd_info.b.gc)
                        {
                            errInfo->bError_Type = GL_GC_1BREAD_FAIL;
                        }
                        else if(die_cmd_info->cmd_info.b.stream)
                        {
                            errInfo->bError_Type = GL_1BREAD_FAIL;
                        }
                        else
                        {
                            errInfo->bError_Type = GL_1BREAD_FAIL;
							if(ncl_cmd->flags & NCL_CMD_RAPID_PATH)
							{
								if(ncl_cmd->du_user_tag_list.pl.btag == ua_btag)
									errInfo->RD_uaFlag = 1;
							}
                            //errInfo->RD_Ftl = 1;
                        }
	                }
				}
            }
            #ifdef EH_ENABLE_2BIT_RETRY
            else if(err == ficu_err_2bit_retry_err)   //14
            {
                if(ncl_cmd->flags & NCL_CMD_RCED_FLAG)
            	{
                	errInfo->bError_Type = GL_RECV_2BREAD_FAIL;
				    #if 0//RAID_SUPPORT_UECC
					if((ncl_cmd->uecc_type == NCL_UECC_RC_FTL) || (ncl_cmd->uecc_type == NCL_UECC_RC_L2P))
						errInfo->RD_Ftl = 1;
					#endif
                    if(ncl_cmd->rty_blank_flg)
                        errInfo->Blank = 1;
                    else if(ncl_cmd->rty_dfu_flg)
                        errInfo->DFU = 1;
            	}
				else
				{
					errInfo->bError_Type = GL_2BREAD_FAIL;
				}
            }
            else if(err == ficu_err_2bit_nard_err)  //15
            {
                errInfo->bError_Type = GL_RECV_NO_ARD_ERROR;
                if(ncl_cmd->flags & NCL_CMD_RCED_FLAG)
            	{
			        #if 0//RAID_SUPPORT_UECC
				    if((ncl_cmd->uecc_type == NCL_UECC_RC_FTL) || (ncl_cmd->uecc_type == NCL_UECC_RC_L2P))
					    errInfo->RD_Ftl = 1;
				    #endif
                    if(ncl_cmd->rty_dfu_flg)
                        errInfo->DFU = 1;
                }
            }
            #endif
			else if(err == ficu_err_du_ovrlmt)	//2   
			{
				if(ncl_cmd->du_format_no == DU_FMT_USER_4K_PATROL_READ)
				{
					errInfo->bError_Type = GL_OVER_LIMIT;
				}
				else
				{
					ncb_ficu_trace(LOG_ERR, 0xd786, "[EH] Not UNC, OpCd[%d] ErrTyp[%d]", ncl_cmd->op_code, err);
					return;
				}
			}
			else if(err == ficu_err_raid)	// 8
			{
				errInfo->bError_Type = GL_RECV_FAIL;
			}
			else
			{
				ncb_ficu_trace(LOG_ERR, 0x4bd9, "[EH] Not UNC, OpCd[%d] ErrTyp[%d]", ncl_cmd->op_code, err);
				return;
			}

            if(errInfo->Blank && (errInfo->bError_Type != GL_1BREAD_FAIL) && (errInfo->bError_Type != GL_GC_1BREAD_FAIL))  //read blank reg glist just for host data(include host, fill up, gc)
                return;
            #else
            if(ncl_cmd->flags & NCL_CMD_RCED_FLAG)
            {
                if(err == ficu_err_1bit_retry_err)   // 13
                {
                    errInfo->bError_Type = GL_RECV_1BREAD_FAIL;
                }
                else if(err == ficu_err_2bit_retry_err)   //14
                {
                    errInfo->bError_Type = GL_RECV_2BREAD_FAIL;
                }
                else if(err == ficu_err_2bit_nard_err)  //15
                {
                    errInfo->bError_Type = GL_RECV_NO_ARD_ERROR;
                }
                else
                {
                    ncb_ficu_trace(LOG_ERR, 0xd11b, "[EH] Not UNC, OpCd[%d] ErrTyp[%d]", ncl_cmd->op_code, err);
                    return;
                }
            }
            else
            {
                if(err == ficu_err_du_ovrlmt)   //2   
                {
                    if(ncl_cmd->du_format_no == DU_FMT_USER_4K_PATROL_READ)
                    {
                        errInfo->bError_Type = GL_OVER_LIMIT;
                    }
                    else
                    {
                        ncb_ficu_trace(LOG_ERR, 0x2e7b, "[EH] Not UNC, OpCd[%d] ErrTyp[%d]", ncl_cmd->op_code, err);
                        return;
                    }
                }
                else if(err == ficu_err_raid)   // 8
                {
                    errInfo->bError_Type = GL_RECV_FAIL;
                }
                else if(err == ficu_err_1bit_retry_err)   // 13
                {
                    errInfo->bError_Type = GL_1BREAD_FAIL;
                }
                else if(err == ficu_err_2bit_retry_err)   // 14
                {
                    errInfo->bError_Type = GL_2BREAD_FAIL;
                }
                else
                {
                    ncb_ficu_trace(LOG_ERR, 0x44bc, "[EH] Not UNC, OpCd[%d] ErrTyp[%d]", ncl_cmd->op_code, err);
                    return;
                }
            }
            #endif
        }
            break;
        
        case NCL_CMD_OP_WRITE:  // 4
        {
            errInfo->bError_Type = GL_PROG_FAIL;	
            errInfo->RD_Ftl      = ncl_req->req.op_type.b.ftl;
            

        }    
            break;
            
        case NCL_CMD_OP_ERASE:  // 5
        {
            errInfo->bError_Type = GL_ERASE_FAIL;	
            errInfo->RD_Ftl      = ncl_req->req.op_type.b.ftl;  // TBC_EH, go through w/ ncl_w_req_t
        }    
            break;
            
        default:
            ncb_ficu_trace(LOG_ERR, 0x303c, "[EH] Not Rd/Wr/Er, OpCd[%d]", ncl_cmd->op_code);
            break;    
    }
    
    #endif
    
    // The same log in GList
	//ncb_ficu_trace(LOG_ERR, 0, "[ERR] FILL ERROR INFO Blk:%d Phy:%d Page:%d ERRType:%d Die:%d AU:%d", errInfo->wLBlk_Idx, errInfo->wPhyBlk, errInfo->wPage, errInfo->bError_Type, errInfo->bDie, errInfo->AU);		
    ncb_ficu_trace(LOG_ERR, 0x5be0, "[EH] ErrInfor[Err/LB/D/PB/Pg/Au](%d/%d/%d/%d/%d/%d)", errInfo->bError_Type, errInfo->wLBlk_Idx, errInfo->bDie, errInfo->wPhyBlk, errInfo->wPage, errInfo->AU);		
		
#if (CPU_ID != CPU_BE)// (ncl_req->req.op_type.b.remote)
		if((errInfo->wLBlk_Idx == ps_open[FTL_CORE_NRM]) || (errInfo->wLBlk_Idx == ps_open[FTL_CORE_GC]))
		{
		    errInfo->Open_blk = 1;

			if (errInfo->wLBlk_Idx == ps_open[FTL_CORE_GC])	// ISU, GCOpClsBefSuspnd
				errInfo->gcOpen = 1; 

			if (err == ficu_err_1bit_retry_err) 			// ISU, RdOpBlkHdl
			{
				if (errInfo->wLBlk_Idx == ps_open[FTL_CORE_NRM])
					rdOpBlkFal[FTL_CORE_NRM] = errInfo->wLBlk_Idx;
				else 
					rdOpBlkFal[FTL_CORE_GC] = errInfo->wLBlk_Idx;
			}
            
		    // The same log in GList
			//ncb_ficu_trace(LOG_ERR, 0, "[EH] Host_Open: 0x%x, GC_Open: 0x%x", ps_open[FTL_CORE_NRM], ps_open[FTL_CORE_GC]); 
			cpu_msg_issue(CPU_BE - 1, CPU_MSG_SET_FAIL_OP_BLK, 0, (u32)errInfo->wLBlk_Idx);
		}
		
		ncb_ficu_trace(LOG_INFO, 0x5e6f, "[EH] GLReg ipc"); 
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_REG_GLIST, 0, (u32)errInfo);
		//ncb_ficu_trace(LOG_ERR, 0, "[EH]REG GL to CPU2\n");
		
#else
		if((errInfo->wLBlk_Idx == ps_open[FTL_CORE_NRM] ) || (errInfo->wLBlk_Idx == ps_open[FTL_CORE_GC]))
		{
            errInfo->Open_blk = 1;

			if (errInfo->wLBlk_Idx == ps_open[FTL_CORE_GC])	// ISU, GCOpClsBefSuspnd
				errInfo->gcOpen   = 1; 

			if (err == ficu_err_1bit_retry_err) 			// ISU, RdOpBlkHdl
			{
				if (errInfo->wLBlk_Idx == ps_open[FTL_CORE_NRM])
					rdOpBlkFal[FTL_CORE_NRM] = errInfo->wLBlk_Idx;
				else 
					rdOpBlkFal[FTL_CORE_GC] = errInfo->wLBlk_Idx;
			}
        
		    // The same log in GList
			//ncb_ficu_trace(LOG_ERR, 0, "[EH] Host_Open: 0x%x, GC_Open: 0x%x", ps_open[FTL_CORE_NRM], ps_open[FTL_CORE_GC]); 
			Set_Fail_OP_Blk(errInfo->wLBlk_Idx);
		}

		//ncb_ficu_trace(LOG_ERR, 0, "[EH]Send to REG GL\n");

		//ncb_ficu_trace(LOG_DEBUG, 0, "[EH]Send to REG GL"); 
		wGListRegBlock(errInfo);
		
#endif
}

#endif

/*	// Move to gl_error_to_aer, control by GL(phyBlk), DBG, PgFalVry
ddr_code void ficu_error_to_aer(u32 fcmd_id)
{
	extern __attribute__((weak)) void nvmet_evt_aer_in();

	u32 type;
	struct ncl_cmd_t* ncl_cmd;
	ncl_cmd = (struct ncl_cmd_t*)ncl_cmd_retrieve(fcmd_id);

	switch(ncl_cmd->op_code)
	{
		case NCL_CMD_OP_WRITE:
			type = 7;//prog fail
			break;
		case NCL_CMD_OP_READ:
			type = 8;//erase fail
			break;
		default:
			return;
	}

	nvmet_evt_aer_in((2<<16)|type, 0);
}
*/
fast_data_zi u32 no_dqs_cnt = 0;
fast_code void ficu_fcmd_err_handler(ficu_int_src_reg_t int_src)
{
	u32 idx;
	u32 err = cur_du_good;
	ficu_status_reg1_t reg1;
	ficu_status_reg2_t reg2;
    dec_top_ctrs_reg1_t dtop_reg1;
    #if RAID_SUPPORT_UECC
    bool mk_GL_chk = false;
    #endif
    
	ncb_ficu_trace(LOG_DEBUG, 0xb6f0, "FICU INT err %x", int_src.all);
	int_src.all &= (FICU_SQ_GRP0_FCMD_ERR_INT_MASK | FICU_SQ_GRP1_FCMD_ERR_INT_MASK);
	while(int_src.all) {
		idx = ctz(int_src.all);
		int_src.all &= ~(1 << idx);
		switch(1 << idx) {
        case FICU_NO_DQS_ERR_INT_MASK:
#if ONFI_DCC_TRAINING
			if ((no_dqs_cnt < 20)&&(!DccReadInProcess)) {
#else
			if (no_dqs_cnt < 20) {
#endif
				ncb_ficu_trace(LOG_ERR, 0x051b, "no dqs:%d!",no_dqs_cnt);
				no_dqs_cnt++;
			}
#if RAINIER_S
			extern void ficu_no_dqs_err_bit_clr(void);
			ficu_no_dqs_err_bit_clr();
#endif
			ficu_writel(FICU_NO_DQS_ERR_INT_MASK, FICU_INT_SRC_REG);
			break;
		case FICU_SQ_GRP0_FCMD_ERR_INT_MASK:
		case FICU_SQ_GRP1_FCMD_ERR_INT_MASK:
			reg1.all = ficu_readl(FICU_STATUS_REG1);
			reg2.all = ficu_readl(FICU_STATUS_REG2);
        	dtop_reg1.all = dec_top_readl(DEC_TOP_CTRS_REG1);

#if 0//PATROL_READ
            struct ncl_cmd_t* pard_ncl_cmd;  
            pard_ncl_cmd = (struct ncl_cmd_t*)ncl_cmd_retrieve(reg1.b.ficu_err_fcmd_id);
            if(pard_ncl_cmd->op_code == NCL_CMD_PATROL_READ){
                if(reg1.b.ficu_ec_err && !reg1.b.ficu_ec_err_d0e1){
                    //ncb_ficu_trace(LOG_ERR, 0, "[ERR] FCMD ERR reg1: 0x%x  ", reg1.all);
                    err = eccu_dec_err_get();
                    resume_decoder();
                    if((err == cur_du_ovrlmt_err) && (pard_ncl_cmd->du_format_no == DU_FMT_USER_4K_PATROL_READ))
                        pard_ncl_cmd->status |= NCL_CMD_ERROR_STATUS;                       
                }
                ficu_writel(1 << idx, FICU_INT_SRC_REG);
                return;
            }           
#endif      
            struct ncl_cmd_t* ncl_cmd_tmp0;
            ncl_cmd_tmp0 = (struct ncl_cmd_t*)ncl_cmd_retrieve(reg1.b.ficu_err_fcmd_id);

            if(ncl_cmd_tmp0->op_code==NCL_CMD_OP_SSR_READ){
                    u16 cnt1=0;
					err = eccu_dec_err_get(&cnt1);
                    
                    if(err==cur_du_ucerr||err==cur_du_dfu_err){
                        ncl_cmd_tmp0->raw_1bit_cnt=cnt1;
                    }

                    resume_decoder();
                    break;
            }

            pda_t pda = eccu_error_pda_get();	// Return first pda only.
			u32 pl;
            u16 lblk = pda >> nand_info.pda_block_shift;

			#if (SPOR_FLOW == mENABLE) // to disable log while spor scan first page, added by Sunny 20201211
            if(ncl_cmd_tmp0->op_code != NCL_CMD_SPOR_SCAN_FIRST_PG)
			#endif
            {
                ncb_ficu_trace(LOG_DEBUG, 0x7c61, "[ERR] FCMD ERR reg1: 0x%x  pda:0x%x", reg1.all, pda);
            }
            
            if (reg1.b.ficu_nf_err) 
			{
				err = nand_err;
				ncb_ficu_trace(LOG_NCL_ERR, 0x4aaa, "NF err, reg2 %x", reg2.all);
				for (pl=0; pl < nand_plane_num(); pl++) 
                {
					if (reg2.b.ficu_err_finst_status & (1 << pl)) 
                    {
						ncl_fcmd_pda_error(reg1.b.ficu_err_fcmd_id, pda, pl, err);
						
						// NCL_CMD_OP_WRITE and NCL_CMD_OP_ERASE w/ 1-p operation, DBG, PgFalVry
						// 1-p op, no matter pl 0/1, will polled error status at ficu_err_finst_status[0] only.
						//ficu_error_to_aer(reg1.b.ficu_err_fcmd_id);
						//#ifdef ERRHANDLE_GLIST				
                        //if(FTL_INIT_DONE && (lblk != EPM_LBLK))    //ftl init done & exclude epm blk //tony 20201006 
						//    Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, pl, 0);
						//#endif
				    }
			    }

				// Check info_list.status after reocrd it, DBG, PgFalVry
				#ifdef ERRHANDLE_GLIST
				u8 i = 0;
				if (FTL_INIT_DONE && (lblk != EPM_LBLK))	// ftl init done & exclude epm blk
				{
					for (i = 0; i < ncl_cmd_tmp0->addr_param.common_param.list_len; i++) 
					{
						if (ncl_cmd_tmp0->addr_param.common_param.info_list[i].status == nand_err) 
						{			
							pda = ncl_cmd_tmp0->addr_param.common_param.pda_list[i];
						    Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, err);
						}
					}
				}				
				#endif
			} 
            else if (reg1.b.ficu_ec_err) 
            {
                struct ncl_cmd_t* err_ncl_cmd; 
                err_ncl_cmd = (struct ncl_cmd_t*)ncl_cmd_retrieve(reg1.b.ficu_err_fcmd_id);
                pl = (pda >> shr_nand_info.pda_du_shift) & (shr_nand_info.geo.nr_planes - 1);   //pl of pda

                #if RAID_SUPPORT_UECC
				#if (SPOR_FLOW == mENABLE)
				if(!FTL_INIT_DONE)
				{
					if((err_ncl_cmd->uecc_type == NCL_UECC_L2P_RD) || (err_ncl_cmd->uecc_type == NCL_UECC_RC_L2P))
					{
						mk_GL_chk = true;
					}
					else
					{
						mk_GL_chk = false;
					}
				}
				else
				#endif
				{
					//for FTL_INIT_DONE
					if(err_ncl_cmd->uecc_type == NCL_UECC_AUX_RD)
					{			 
						mk_GL_chk = false;
					}
					else
					{
						mk_GL_chk = true;	
					}
				}
                #endif
                
				if (reg1.b.ficu_ec_err_d0e1) 
                {
					err = eccu_enc_err_get();
					resume_encoder();
                    ncl_fcmd_pda_error(reg1.b.ficu_err_fcmd_id, pda, pl, err);
					#if RAID_SUPPORT_UECC 
					if (lblk != EPM_LBLK)
					#else
                    if (FTL_INIT_DONE && (lblk != EPM_LBLK))
					#endif
                    {
                        if(err == cur_du_ppu_err)
                        {
							extern volatile u8 plp_trigger;
							
							if(!plp_trigger)
							{
								ncb_ficu_trace(LOG_ERR, 0x4f05, "[ERR] enc ppu err");
							}
                        }
                        else if(err == cur_du_dfu_err)
                        {
                            ncb_ficu_trace(LOG_ERR, 0x4652, "[ERR] enc dfu err");
                        }
                        else if(err == cur_du_enc_err)
                        {
                            ncb_ficu_trace(LOG_ERR, 0xd170, "[ERR] enc err");
                        }
                        else if(err == cur_du_raid_err)
                        {
                            ncb_ficu_trace(LOG_ERR, 0xa659, "[ERR] enc raid err");
                        }
                    }
                } 
                else 
                {   
                    //pda_t *ncl_pda = err_ncl_cmd->addr_param.common_param.pda_list;
					u16 cnt_temp=0;
					err = eccu_dec_err_get(&cnt_temp);
                    //u8 max_retry_cnt = (err_ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG) ? RR_STEP_SLC : RR_STEP_XLC; //test
#if NCL_FW_RETRY
    #ifdef EH_ENABLE_2BIT_RETRY                    
                    if(err_ncl_cmd->flags & NCL_CMD_RCED_FLAG)
                    {
                        if((err_ncl_cmd->op_code == NCL_CMD_OP_READ)
                            #ifdef NCL_FW_RETRY_BY_SUBMIT
                            || (err_ncl_cmd->op_code == NCL_CMD_FW_RETRY_READ)
                            #endif
                            || (err_ncl_cmd->op_code == NCL_CMD_OP_READ_FW_ARD)
                            )
                        {   
                            if(err_ncl_cmd->retry_step == retry_read || err_ncl_cmd->retry_step == retry_history_read)
                            {
                                if(err == cur_du_erase)
                                {
                                    err_ncl_cmd->rty_blank_flg = true;
                                    err = cur_du_1bit_vth_retry_err; //End read retry.
                                }
                                else if(err == cur_du_dfu_err)
                                {
                                    err_ncl_cmd->rty_dfu_flg = true;
                                    err = cur_du_1bit_vth_retry_err; //End read retry.
                                }
                                else
                                {
                                    err = cur_du_1bit_retry_err; //Go to do vth tracking. Don't use table read retry.
                                }
                            }
                            else if(err_ncl_cmd->retry_step == last_1bit_vth_read)
                            {
                                if(err == cur_du_erase)
                                    err_ncl_cmd->rty_blank_flg = true;
                                else if(err == cur_du_dfu_err)
                                    err_ncl_cmd->rty_dfu_flg = true;

                                err = cur_du_1bit_vth_retry_err;
                            }
                            else if(err_ncl_cmd->retry_step == last_2bit_read)
                            {
                                if(err == cur_du_erase)
                                    err_ncl_cmd->rty_blank_flg = true;
                                else if(err == cur_du_dfu_err)
                                    err_ncl_cmd->rty_dfu_flg = true;


                                err = cur_du_2bit_retry_err;
                                if(dtop_reg1.b.eccu_ard_num_ovrflw)     
                                {
                                   ncb_ficu_trace(LOG_ERR, 0xeb81, "[ERR] ARD num overflow!!");
                                }
                            }
                        }
                    }
                    else
    #endif  
                    {   
                        if(err_ncl_cmd->retry_step == retry_history_read || err_ncl_cmd->retry_step == retry_read)
                        {
                            if(err == cur_du_erase)
                            {
                                err_ncl_cmd->rty_blank_flg = true;
                                err = cur_du_1bit_vth_retry_err; //End read retry.
                            }
                            else if(err == cur_du_dfu_err)
                            {
                                err_ncl_cmd->rty_dfu_flg = true;
                                err = cur_du_1bit_vth_retry_err; //End read retry.
                            }
                        }
                        else if(err_ncl_cmd->retry_step == last_1bit_read)
                        {
                            if(err == cur_du_erase)
                            {
                                err_ncl_cmd->rty_blank_flg = true;
                                err = cur_du_1bit_vth_retry_err; //End read retry.
                            }
                            else if(err == cur_du_dfu_err)
                            {
                                err_ncl_cmd->rty_dfu_flg = true;
                                err = cur_du_1bit_vth_retry_err; //End read retry.
                            }
                            else
                            {
                                err = cur_du_1bit_retry_err; //Go to do vth tracking.
                            }
                        }
                        else if(err_ncl_cmd->retry_step == last_1bit_vth_read)
                        {
                            if(err == cur_du_erase)
                                err_ncl_cmd->rty_blank_flg = true;
                            else if(err == cur_du_dfu_err)
                                err_ncl_cmd->rty_dfu_flg = true;

                            err = cur_du_1bit_vth_retry_err;
                        }
                        else if(err_ncl_cmd->retry_step == last_2bit_read)  //Non-raid 2bit read retry error
                        {
                            if(err == cur_du_erase)
                                err_ncl_cmd->rty_blank_flg = true;
                            else if(err == cur_du_dfu_err)
                                err_ncl_cmd->rty_dfu_flg = true;

                            err = cur_du_2bit_retry_err;
                            if(dtop_reg1.b.eccu_ard_num_ovrflw)     
                            {
                               ncb_ficu_trace(LOG_ERR, 0xbade, "[ERR] ARD num overflow!!");
                            }
                        }
                        #if SPOR_RETRY
                        else if(err_ncl_cmd->retry_step == spor_retry_read && err_ncl_cmd->retry_cnt >= 3)
                        {
                            err = cur_du_spor_err;
                        }
                        #endif
                    }
#endif
#ifdef NCL_HAVE_reARD
                    if(!reg1.b.ficu_ard_err)
                    {
                        if((dtop_reg1.b.eccu_ard_num_ovrflw) && (err != cur_du_erase))
                        {
                            err = cur_du_nard; 
                        }
                    }     
#endif
#ifdef EH_ENABLE_2BIT_RETRY 
                    if(err_ncl_cmd->flags & NCL_CMD_POUT_FLAG)
                    {
                        if(pda != err_ncl_cmd->addr_param.common_param.pda_list[0])
                        {
                            //ncb_ficu_trace(LOG_INFO, 0, "[FICU] RAID do parity out");
                            pda = err_ncl_cmd->addr_param.common_param.pda_list[0];
                        }
                    }
#endif
                    resume_decoder();  // TBC_EH, should move below after EH done?
                    ncl_fcmd_pda_error(reg1.b.ficu_err_fcmd_id, pda, 0, err);   

#ifdef ERRHANDLE_GLIST
    #if NCL_FW_RETRY
					#if RAID_SUPPORT_UECC 
					if (mk_GL_chk && (lblk != EPM_LBLK))
					#else
                    if (FTL_INIT_DONE && (lblk != EPM_LBLK))
					#endif
					{
                        //pl = (pda >> 2) & 1;  //pl of pda
                        //struct ncl_cmd_t* err_ncl_cmd;   //tony 20201021
                        //err_ncl_cmd = (struct ncl_cmd_t*)ncl_cmd_retrieve(reg1.b.ficu_err_fcmd_id);
                        //if(((err == ficu_err_du_uc) || (err == ficu_err_du_erased)) && (err_ncl_cmd->retry_step == retry_end))
                        if(err == cur_du_1bit_vth_retry_err)
                        {
                            //ncb_ficu_trace(LOG_ERR, 0, "[ERR] 1Bit fail : 0x%x", err_ncl_cmd->retry_step);
                            ncb_ficu_trace(LOG_ERR, 0xb555, "[ERR] 1Bit fail, re_step[0x%x] uecc[%d] flag[0x%x]", err_ncl_cmd->retry_step, err_ncl_cmd->uecc_type, err_ncl_cmd->flags);
                            Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, cur_du_1bit_retry_err);
                        }
        #ifdef EH_ENABLE_2BIT_RETRY 
                        else if((err == cur_du_2bit_retry_err))
                        {
                            ncb_ficu_trace(LOG_ERR, 0x3558, "[ERR] 2Bit fail : 0x%x", err_ncl_cmd->retry_step);
                            Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, err); 
                        }
                        else if((err == cur_du_2bit_nard_err) && (err_ncl_cmd->retry_step == last_2bit_read))
                        {
                            ncb_ficu_trace(LOG_ERR, 0xb39a, "[ERR] 2bit not do ard fail : 0x%x", err_ncl_cmd->retry_step);
                            Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, err); 
                        }
        #endif
                    }                            
    #else
                    if(err == cur_du_ucerr)
                    {
                        ncb_ficu_trace(LOG_ERR, 0x0c4e, "[ERR] 1Bit fail");
    					#if RAID_SUPPORT_UECC 
						if (mk_GL_chk && (lblk != EPM_LBLK))
						#else
	                    if (FTL_INIT_DONE && (lblk != EPM_LBLK))
						#endif
                        {
                            //pl = (pda >> 2) & 1;  //pl of pda
                            Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, err); 
                        }
                    }
                    #ifdef NCL_HAVE_reARD
                    else if(err == cur_du_nard)
                    {
                        ncb_ficu_trace(LOG_ERR, 0x2e78, "[ERR] Not do ARD");
                        //struct ncl_cmd_t* ncl_cmd_reARD;   //tony 20201021
                    	//ncl_cmd_reARD = (struct ncl_cmd_t*)ncl_cmd_retrieve(reg1.b.ficu_err_fcmd_id);
                        //ncl_cmd_reARD->re_ard_flag = true;
                        err_ncl_cmd->re_ard_flag = true;
                        ncb_ficu_trace(LOG_INFO, 0x52a8, "[ECCU]ficu isr ARD CH id: 0x%x;", dtop_reg1.b.eccu_ard_ch_id);
                        ncb_ficu_trace(LOG_INFO, 0x87c4, "[ECCU]ficu isr ARD overflow bit: %d", dtop_reg1.b.eccu_ard_num_ovrflw);
                    }
                    else if(err == cur_du_erase)
                    {
                        #if (SPOR_FLOW == mENABLE) // to disable log while spor scan first page, added by Sunny 20201211
                        //struct ncl_cmd_t* ncl_cmd_tmp1;
                        //ncl_cmd_tmp1 = (struct ncl_cmd_t*)ncl_cmd_retrieve(reg1.b.ficu_err_fcmd_id);
                        //if(ncl_cmd_tmp1->op_code != NCL_CMD_SPOR_SCAN_FIRST_PG)
						if(err_ncl_cmd->op_code != NCL_CMD_SPOR_SCAN_FIRST_PG)
                        {
                            ncb_ficu_trace(LOG_ERR, 0xf536, "[ERR] Read blank");
                        }
                        #else
                        ncb_ficu_trace(LOG_ERR, 0xb09c, "[ERR] Read blank");
                        #endif

                        //Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, pl, err);
                        //ncb_ficu_trace(LOG_ERR, 0, "[ECCU] Read blank by pass erase: %d", dtop_reg1.b.eccu_ard_bypass_erase); //tony 20201022
                    }
                    #endif
    #endif
					#if RAID_SUPPORT_UECC 
					if (mk_GL_chk && (lblk != EPM_LBLK))
					#else
                    if (FTL_INIT_DONE && (lblk != EPM_LBLK))
					#endif
                    {
                        if((err == cur_du_ovrlmt_err) && (err_ncl_cmd->du_format_no == DU_FMT_USER_4K_PATROL_READ))
                        {
                            ncb_ficu_trace(LOG_ERR, 0x93be, "[ERR] patrol read ECCT overlimit threshold");
                        }
                        else if(err == cur_du_ppu_err)
                        {   
                            ncb_ficu_trace(LOG_ERR, 0x2983, "[ERR] dec ppu err");
                        }
						else if(err == cur_du_dfu_err)
                        {   
                            ncb_ficu_trace(LOG_ERR, 0xebf6, "[ERR] dec dfu err");
                        }
                        else if(err == cur_du_raid_err)
                        {
                            ncb_ficu_trace(LOG_ERR, 0x204b, "[ERR] raid recover err");
                            Fill_Error_Info(pda, reg1.b.ficu_err_fcmd_id, err);
                        }
                    }

                    #if (SPOR_FLOW == mENABLE) // to disable log while spor scan first page, added by Sunny 20201211
                    //struct ncl_cmd_t* ncl_cmd_tmp2;
                    //ncl_cmd_tmp2 = (struct ncl_cmd_t*)ncl_cmd_retrieve(reg1.b.ficu_err_fcmd_id);
                    //if(ncl_cmd_tmp2->op_code != NCL_CMD_SPOR_SCAN_FIRST_PG)
					if(err_ncl_cmd->op_code != NCL_CMD_SPOR_SCAN_FIRST_PG)
                    {
                        ncb_ficu_trace(LOG_DEBUG, 0x5034, "[ERR] err fcmd = 0x%x, finst_id = 0x%x, du = 0x%d", reg1.b.ficu_err_fcmd_id, reg2.b.ficu_err_finst_id, reg2.b.ficu_err_du_id);
                    }
                    #else
                    ncb_ficu_trace(LOG_ERR, 0xcb22, "[ERR] err fcmd = 0x%x, finst_id = 0x%x, du = 0x%d", reg1.b.ficu_err_fcmd_id, reg2.b.ficu_err_finst_id, reg2.b.ficu_err_du_id);
                    #endif

#endif
                }
			} 
            else if (reg1.b.ficu_ard_err) 
            {
				//ncb_ficu_trace(LOG_ERR, 0, "ARD err");
				sys_assert(0);
			} 
            else 
            {
				// Hmm?? What error
				//ncb_ficu_trace(LOG_NCL_ERR, 0, "Unknow err, ficu_status_reg1 0x%x, reg2 0x%x", reg1.all, reg2.all);
				ncb_ficu_trace(LOG_ERR, 0x9e3a, "Unknow err, ficu_status_reg1 0x%x, reg2 0x%x", reg1.all, reg2.all);
				sys_assert(0);
			}			
			break;
		default:
			ncb_ficu_trace(LOG_ERR, 0x8126, "TODO ficu err %x", 1 << idx);
			break;
		}
		ficu_writel(1 << idx, FICU_INT_SRC_REG);
	}
	
}

slow_code void ficu_no_dqs_err_handler(void)
{
	ficu_int_src_reg_t int_src;
	u32 ficu_int_sts_addr_map[] = {
		FICU_INT_STATUS_REG0,
		FICU_INT_STATUS_REG1,
		FICU_INT_STATUS_REG2,
		FICU_INT_STATUS_REG3,
	};
	u32 sts_addr = ficu_int_sts_addr_map[CPU_ID-1];


	resume_decoder();
	resume_encoder();
	eccu_hw_reset();

	extern void ndphy_reset();
	ndphy_reset();

	int_src.all = ficu_readl(sts_addr);
	ncb_ficu_trace(LOG_ERR, 0x944d, "0.1  int_src.all =%x, \n", int_src.all);
	ficu_mode_disable();
	ficu_mode_enable();
	//int_src.all = ficu_readl(sts_addr);
	//ncb_ficu_trace(LOG_ERR, 0, "0.2  int_src.all =%x, \n", int_src.all);
	ndcu_delay(10);
	//ncb_ficu_trace(LOG_ERR, 0, "1.1  int_src.all =%x, \n", int_src.all);
	//int_src.all = ficu_readl(sts_addr);
	//ncb_ficu_trace(LOG_ERR, 0, "1.2  int_src.all =%x, \n", int_src.all);
	ficu_writel(FICU_NO_DQS_ERR_MASK, FICU_INT_SRC_REG);
	int_src.all = ficu_readl(sts_addr);
	ncb_ficu_trace(LOG_ERR, 0xd9e8, "1.3  int_src.all =%x, \n", int_src.all);
	if(int_src.b.ficu_no_dqs_err == 1)
	{
		sys_assert(0);
	}
}

extern bool ucache_flush_flag;

fast_code void ficu_dtcm_isr(void)
{
#if !defined(DUAL_BE) || (defined(DUAL_BE) && CPU_BE == CPU_ID)
    ficu_grp0_fcmd_compl_que_ptrs_t ptr0;

    ptr0.all = ficu_readl(FICU_GRP0_FCMD_COMPL_QUE_PTRS);
    while (ptr0.b.grp0_fcmd_compl_que_wr_ptr != ptr0.b.grp0_fcmd_compl_que_rd_ptr) {
        struct dtcm_cq_entry *p = &dtcm_cq_grp0[ptr0.b.grp0_fcmd_compl_que_rd_ptr];
            
    


                //for (pl=0; pl < nand_plane_num(); pl++) 
    
                        //if(FTL_INIT_DONE && (lblk != EPM_LBLK))    //ftl init done & exclude epm blk //tony 20201006 
    

    


                //blk_gc = ps_open[FTL_CORE_GC];
                //for (pl=0; pl < nand_plane_num(); pl++) 
    
                        //if(FTL_INIT_DONE && (lblk != EPM_LBLK))    //ftl init done & exclude epm blk //tony 20201006 
    
#if !PERF_NCL
        ncb_ficu_trace(LOG_DEBUG, 0x3915, "wptr %d, rptr %d, %x",
                  ptr0.b.grp0_fcmd_compl_que_wr_ptr, ptr0.b.grp0_fcmd_compl_que_rd_ptr,
                  *(u32*)p);
#endif
        #if PLP_DEBUG
        if(ucache_flush_flag){
            ncb_ficu_trace(LOG_PLP, 0xad5e, "ncl cmd 0x%x",ncl_cmd_retrieve(p->fcmd_id));
        }
        #endif
        ncl_fcmd_completion(p->fcmd_id);
        ptr0.b.grp0_fcmd_compl_que_rd_ptr = (ptr0.b.grp0_fcmd_compl_que_rd_ptr + 1) &
            (FSPM_CQ_FIFO_COUNT - 1);

    }
    ficu_writel(ptr0.all, FICU_GRP0_FCMD_COMPL_QUE_PTRS);
#endif

#if !defined(DUAL_BE) || (defined(DUAL_BE) && CPU_BE_LITE == CPU_ID)
    ficu_grp0_fcmd_compl_que_ptrs_t ptr1;
    ptr1.all = ficu_readl(FICU_GRP1_FCMD_COMPL_QUE_PTRS);
    while (ptr1.b.grp0_fcmd_compl_que_wr_ptr != ptr1.b.grp0_fcmd_compl_que_rd_ptr) {
        struct dtcm_cq_entry *p = &dtcm_cq_grp1[ptr1.b.grp0_fcmd_compl_que_rd_ptr];

#if !PERF_NCL
        ncb_ficu_trace(LOG_DEBUG, 0xdb3d, "wptr %d, rptr %d, %x",
                  ptr1.b.grp0_fcmd_compl_que_wr_ptr, ptr1.b.grp0_fcmd_compl_que_rd_ptr,
                  *(u32*)p);
#endif
        #if PLP_DEBUG
        if(ucache_flush_flag){
            ncb_ficu_trace(LOG_INFO, 0xb9ba, "ncl cmd 0x%x",ncl_cmd_retrieve(p->fcmd_id));
        }
        #endif
        ncl_fcmd_completion(p->fcmd_id);
        ptr1.b.grp0_fcmd_compl_que_rd_ptr = (ptr1.b.grp0_fcmd_compl_que_rd_ptr + 1) &
            (FSPM_CQ_FIFO_COUNT - 1);

    }
    ficu_writel(ptr1.all, FICU_GRP1_FCMD_COMPL_QUE_PTRS);
#endif
}


/*!
 * @brief FCMD DONE or ERR interrupt handling
 *
 * @return Not used
 */
fast_data bool in_ficu_isr = false;
fast_code void ficu_isr(void)
{
    sys_assert(in_ficu_isr == false);  //add by vito,use to check ficu_isr call ficu_isr error
    in_ficu_isr = true;
	if (cq_in_dtcm) {
		ficu_dtcm_isr();
	}
	struct ficu_cq_entry* cq_entry;
	ficu_int_src_reg_t int_src;
	ficu_fcmd_cq_cnt_reg_t cq_reg;
	u32 i;

	u32 ficu_int_sts_addr_map[] = {
		FICU_INT_STATUS_REG0,
		FICU_INT_STATUS_REG1,
		FICU_INT_STATUS_REG2,
		FICU_INT_STATUS_REG3,
	};
	u32 sts_addr = ficu_int_sts_addr_map[CPU_ID-1];
retry:
	int_src.all = ficu_readl(sts_addr);
	
	if (int_src.all) {
		ncb_ficu_trace(LOG_DEBUG, 0xb904, "int %x\n", int_src.all);
	}
	int_src.all &= ~FICU_NO_DQS_ERR_MASK;

	while (int_src.all) {
		u32 bit = ctz(int_src.all);
		switch(1 << bit) {
		case FICU_FCMD_GRP0_CQ_DONE_MASK:
		case FICU_FCMD_GRP1_CQ_DONE_MASK:
			sys_assert(cq_in_dtcm);
			ficu_writel(1 << bit, FICU_INT_SRC_REG);
			break;
		case FICU_SQ_GRP0_FCMD_ERR_MASK:
		case FICU_SQ_GRP1_FCMD_ERR_MASK:
			
#if DEBUG
			{
				ficu_status_reg1_t reg1;
				u32 fcmd_id;
				reg1.all = ficu_readl(FICU_STATUS_REG1);
				fcmd_id = reg1.b.ficu_err_fcmd_id;
				struct ncl_cmd_t* ncl_cmd;
				ncl_cmd = ncl_cmd_retrieve(fcmd_id);
				if (ncl_cmd->sq_grp) {
					sys_assert(bit == FICU_SQ_GRP1_FCMD_ERR_SHIFT);
				} else {
					sys_assert(bit == FICU_SQ_GRP0_FCMD_ERR_SHIFT);
				}
			}
#endif
			    ficu_fcmd_err_handler(int_src);
			break;
		case FICU_FSPM_FCMD_CQ_DONE_MASK:
			sys_assert(!cq_in_dtcm);
			cq_reg.all = ficu_readl(FICU_FCMD_CQ_CNT_REG);
			if (cq_reg.all == 0) {
				goto retry;
			}
			sys_assert(cq_reg.all != 0);
			for (i = 0; i < cq_reg.b.ficu_fcmd_cq_cnt; i++) {
				//ncb_ficu_trace(LOG_DEBUG, 0, "INT %d cq %d", int_src.all, cq_reg.all);

				cq_entry = ficu_cq_ptr;
#if DEBUG
				if (ficu_cq_phase != cq_entry->phase) {
					ncb_ficu_trace(LOG_ERR, 0x0ed6, "phase wrong");
				}
				struct ncl_cmd_t* ncl_cmd;
				ncl_cmd = ncl_cmd_retrieve(cq_entry->fcmd_id);
				if (ncl_cmd != NULL) {
					sys_assert (ncl_cmd->sq_grp == cq_entry->sq_grp);
				}
#endif
				if (cq_entry->ard) {
					ncl_fcmd_ard_occur(cq_entry->fcmd_id);
				}
				// Notify FW fcmd completion
				ncl_fcmd_completion(cq_entry->fcmd_id);

				ficu_cq_ptr++;
				if ((u32)ficu_cq_ptr == CQ_END) {
					ficu_cq_ptr = (struct ficu_cq_entry*)CQ_START;
#if DEBUG
					ficu_cq_phase = !ficu_cq_phase;
#endif
				}
			}
			ficu_writel(cq_reg.all, FICU_FCMD_CQ_CNT_REG);
			ficu_writel(1 << bit, FICU_INT_SRC_REG);
			break;

		case FICU_NO_DQS_ERR_MASK:
			//Maybe next isr is other error, no_DQS_err should handle last!
			ndcu_delay(10);
			int_src.all = ficu_readl(sts_addr);
			if ((int_src.all & (FICU_NO_DQS_ERR_MASK |FICU_SQ_GRP0_FCMD_ERR_MASK | FICU_SQ_GRP1_FCMD_ERR_MASK)) == FICU_NO_DQS_ERR_MASK)
			{
				ncb_ficu_trace(LOG_ERR, 0x570d, "ficu_no_dqs_err_handler(), int_src 0x%x,\n", int_src.all);
				if (cq_in_dtcm) {
					ficu_dtcm_isr();
					int_src.all = ficu_readl(sts_addr);
					ncb_ficu_trace(LOG_ERR, 0xd3b7, "handle CQ_DONE, int_src 0x%x,\n", int_src.all);
				}
				ficu_no_dqs_err_handler();
			}
			else
			{
				goto retry;
			}

			break;
		default:
            ncb_ficu_trace(LOG_ERR, 0x45a1, "int_src.all:0x%x, bit:%d\n", int_src.all, bit);
			sys_assert(0);
			break;
		}
		int_src.all &= ~(1 << bit);
	}
    sys_assert(in_ficu_isr == true);
    in_ficu_isr = false;
}

/*!
 * @brief Get the error PDA
 *
 * @return value(PDA)
 */
fast_code pda_t eccu_error_pda_get(void)
{
	u32 ch, dev, col, row;
	u32 i;
	ficu_status_reg3_t err_col;
	ficu_status_reg4_t err_row;
	pda_t pda;
	ficu_ch_mapping_reg_t ch_remap;
	u32 lun, plane, block, page, du;
	u32 ch_shift = 0, ce_shift = 0, lun_shift = 0, pl_shift = 0;
	u32 row_page_mask;
	ficu_status_reg1_t err_reg1 = {.all = ficu_readl(FICU_STATUS_REG1)};

    #ifdef PREVENT_PDA_MISMATCH
    struct ncl_cmd_t* err_ncl_cmd = ncl_cmd_retrieve(err_reg1.b.ficu_err_fcmd_id);
    //pda_t ncl_pda = err_ncl_cmd->addr_param.rapid_du_param.pda;
    #endif
    
	err_row.all = ficu_readl(FICU_STATUS_REG4);
	err_col.all = ficu_readl(FICU_STATUS_REG3);

	ch = err_col.b.ficu_err_ch_id;
	dev = err_col.b.ficu_err_dev_id;
	col = err_col.b.ficu_err_finst_col_addr;
	row = err_row.b.ficu_err_finst_row_addr;

	ch_remap.all = ficu_readl(FICU_CH_MAPPING_REG);

	for (i = 0; i < nand_channel_num(); i++) {
		if (ch == ((ch_remap.all >> (i * 4)) & 0xF)) {
			ch = i;
			break;
		}
	}

	row_page_mask = (1 << (nand_info.row_pl_shift - nand_info.row_page_shift)) - 1;

	page = (row >> nand_info.row_page_shift) & row_page_mask;
	plane = (row >> nand_info.row_pl_shift) & (nand_info.geo.nr_planes - 1);
	block = (row >> nand_info.row_block_shift) & nand_info.pda_block_mask;
	lun = row >> nand_info.row_lun_shift;
	du = col / (NAND_DU_SIZE+1);// Currently support 4KB du
	// Convert to PDA of interleave type 2
	pl_shift = nand_info.pda_interleave_shift;
	ch_shift = pl_shift + ctz(nand_info.geo.nr_planes);
	ce_shift = ch_shift + ctz(nand_info.geo.nr_channels);
	lun_shift = ce_shift + ctz(nand_info.geo.nr_targets);
#if HAVE_TOGGLE_SUPPORT
	if (err_reg1.b.ficu_err_tlc_en) {
		page *= XLC;
		page += ndcu_get_xlc_page_index(err_reg1.b.ficu_err_ndcmd_fmt_sel);
	}
	#if (OPEN_ROW == 1)
	if(err_reg1.b.ficu_err_ndcmd_fmt_sel == FINST_NAND_FMT_READ_DATA)
	{
		ficu_status_reg2_t err_reg2;
		struct ncl_cmd_t* ncl_cmd;
		//u32 actDiePlane;
		err_reg2.all = ficu_readl(FICU_STATUS_REG2);
		ncl_cmd = ncl_cmd_retrieve(err_reg1.b.ficu_err_fcmd_id);
		//pda = ncl_cmd->addr_param.common_param.pda_list[0];
		pda = ncl_cmd->addr_param.common_param.pda_list[err_reg2.b.ficu_err_du_id];
		//actDiePlane = GET_DIE_PLANE_NUM_FORM_PDA(ncl_cmd->die_id, pda2plane(pda));
		//evlog_printk(LOG_INFO,"[jia]pda:0x%x exp:0x%x",pda, pda2ActDiePlane_Row(pda), actDiePlane);
		//openRow[actDiePlane] = 0xFFFFFFFF;
		page += pda2pg_idx(pda);
	}
	#endif
#endif
	pda = (lun << lun_shift) | \
		(dev << ce_shift) | \
		(ch << ch_shift) | \
		(plane << pl_shift) | \
		(block << nand_info.pda_block_shift) | \
		(page << nand_info.pda_page_shift) | \
		(du << nand_info.pda_du_shift);

	//ncb_ficu_trace(LOG_DEBUG, 0, "ch %d, ce %d, row 0x%x, col 0x%x",
	//	       ch, dev, row, col);
    #ifdef PREVENT_PDA_MISMATCH
    ncb_ficu_trace(LOG_ALW, 0x2aaf, "[DBG] ncl_cmd[0x%x] pda[0x%x] ch[%d] ce[%d] row[0x%x] col[0x%x]",
    	err_ncl_cmd, pda, ch, dev, row, col);
    #endif

	// TODO convert nand address to PDA
	//sys_assert(0);
	return pda;
}

/*!
 * @brief Set the PDA config register
 *
 * @return Not used
 */
fast_code void ficu_pda_conf_reg(void)
{
	ficu_pda_conf_reg0_t pda_reg0;
	ficu_pda_conf_reg1_t pda_reg1;
	ficu_pda_conf_reg2_t pda_reg2;

	pda_reg1.b.ficu_pda_intlv_ch_bw = ctz(nand_channel_num());
	pda_reg1.b.ficu_pda_intlv_ce_bw = ctz(nand_target_num());
	pda_reg1.b.ficu_pda_intlv_lun_bw = ctz(nand_lun_num());
	pda_reg1.b.ficu_pda_intlv_pln_bw = ctz(nand_plane_num());
	pda_reg1.b.ficu_pda_intlv_type = INTERLEAVE_TYPE;
	ncb_ficu_trace(LOG_INFO, 0x7e2d, "width %d %d %d %d", pda_reg1.b.ficu_pda_intlv_ch_bw, \
					pda_reg1.b.ficu_pda_intlv_ce_bw, \
					pda_reg1.b.ficu_pda_intlv_lun_bw, \
					pda_reg1.b.ficu_pda_intlv_pln_bw);
	ficu_writel(pda_reg1.all, FICU_PDA_CONF_REG1);

	pda_reg0.b.ficu_pda_du_offset = 0;
	pda_reg0.b.ficu_pda_intlv_offset = DU_CNT_SHIFT;
	pda_reg0.b.ficu_pda_page_offset = pda_reg0.b.ficu_pda_intlv_offset + \
					pda_reg1.b.ficu_pda_intlv_ch_bw + \
					pda_reg1.b.ficu_pda_intlv_ce_bw + \
					pda_reg1.b.ficu_pda_intlv_lun_bw + \
					pda_reg1.b.ficu_pda_intlv_pln_bw;
	u32 page_width = ctz(get_next_power_of_two(nand_page_num()));
	pda_reg0.b.ficu_spb_offset = pda_reg0.b.ficu_pda_page_offset + page_width;
	u32 block_width = ctz(get_next_power_of_two(nand_block_num()));
	pda_reg0.b.ficu_pda_vld_msb = pda_reg0.b.ficu_spb_offset + block_width;
	ncb_ficu_trace(LOG_INFO, 0xa8e7, "offset %d %d %d %d. msb %d", pda_reg0.b.ficu_pda_du_offset, \
					pda_reg0.b.ficu_pda_intlv_offset, \
					pda_reg0.b.ficu_pda_page_offset, \
					pda_reg0.b.ficu_spb_offset, \
					pda_reg0.b.ficu_pda_vld_msb);
	ficu_writel(pda_reg0.all, FICU_PDA_CONF_REG0);

	pda_reg2.all = 0;
	pda_reg2.b.ficu_pda_total_num_ch = nand_channel_num();
	pda_reg2.b.ficu_pda_ce_per_ch = nand_target_num();
	pda_reg2.b.ficu_pda_page_per_blk = nand_page_num();
#if SVN_VERSION >= 7606
	pda_reg2.b.ficu_pda_total_num_ch = nand_channel_num() - 1;
	pda_reg2.b.ficu_pda_ce_per_ch = nand_target_num() - 1;
	pda_reg2.b.ficu_pda_page_per_blk = nand_page_num() - 1;
#else
	if (soc_svn_id >= 0x7606) {
		pda_reg2.b.ficu_pda_total_num_ch = nand_channel_num() - 1;
		pda_reg2.b.ficu_pda_ce_per_ch = nand_target_num() - 1;
		pda_reg2.b.ficu_pda_page_per_blk = nand_page_num() - 1;
	} else {
		pda_reg2.b.ficu_pda_total_num_ch = nand_channel_num();
		pda_reg2.b.ficu_pda_ce_per_ch = nand_target_num();
		pda_reg2.b.ficu_pda_page_per_blk = nand_page_num();
	}
#endif
	ficu_writel(pda_reg2.all, FICU_PDA_CONF_REG2);
}

/*!
 * @brief Switch to the PDA mode
 *
 * @return Not used
 */
fast_code void ficu_switch_pda_mode(void)
{
	ficu_pda_conf_reg0_t pda_reg0;

	pda_reg0.all = ficu_readl(FICU_PDA_CONF_REG0);
	pda_reg0.b.ficu_fda_gen_mode_sel = 1;
	pda_format = 1;
	ficu_writel(pda_reg0.all, FICU_PDA_CONF_REG0);
}

/*!
 * @brief Switch to the CPDA mode
 *
 * @return Not used
 */
fast_code void ficu_switch_cpda_mode(void)
{
	ficu_pda_conf_reg0_t pda_reg0;

	pda_reg0.all = ficu_readl(FICU_PDA_CONF_REG0);
	pda_reg0.b.ficu_fda_gen_mode_sel = 0;
	pda_format = 0;
	ficu_writel(pda_reg0.all, FICU_PDA_CONF_REG0);
}

/*!
 * @brief Remapping the channel location
 *
 * @param[in] remap	remap value
 *
 * @return Not used
 */
fast_code void ficu_channel_remap(u32 remap)
{
	ficu_writel(remap, FICU_CH_MAPPING_REG);
}

/*!
 * @brief Set DTCM config
 *
 * @return Not used
 */
init_code void ficu_dtcm_conf(void)
{
#ifndef DUAL_BE
	extern struct dtcm_finst_info_t dtcm_finst_info;
	extern struct sram_finst_info_t sram_finst_info;
#endif
	// DTCM FDA/dtag list base0
	ficu_fda_dtag_list_dtcm_base_addr0_reg_t dtag_dtcm_base0;
	dtag_dtcm_base0.b.ficu_fda_dtag_list_dtcm_base_addr0 = btcm_to_dma(dtcm_finst_info.fda_dtag_list);
	ficu_writel(dtag_dtcm_base0.all, FICU_FDA_DTAG_LIST_DTCM_BASE_ADDR0_REG);

	// DTCM FDA/dtag list base1
	//ficu_fda_dtag_list_dtcm_base_addr1_reg_t dtag_dtcm_base1;
	//dtag_dtcm_base1.b.ficu_fda_dtag_list_dtcm_base_addr1 = btcm_to_dma(dtcm_finst_info.fda_dtag_list);
	//ficu_writel(dtag_dtcm_base1.all, FICU_FDA_DTAG_LIST_DTCM_BASE_ADDR1_REG);

	// DTCM cost & LLR list base1
	//ficu_cost_llr_list_dtcm_base_addr1_reg_t dtag_cost_llr_base1;
	//dtag_cost_llr_base1.b.ficu_cost_llr_list_dtcm_base_addr1 = btcm_to_dma(dtcm_finst_info.extra_info_list);
	//ficu_writel(dtag_cost_llr_base1.all, FICU_COST_LLR_LIST_DTCM_BASE_ADDR1_REG);

	hdr_reg_init(&dtcm_cq0.reg, dtcm_cq_grp0, ARRAY_SIZE(dtcm_cq_grp0), &dtcm_cq0.ptr, FICU_REG_ADDR + FICU_GRP0_FCMD_COMPL_QUE_BASE_ADDR);
#if !defined(DUAL_BE)
	hdr_reg_init(&dtcm_cq1.reg, dtcm_cq_grp1, ARRAY_SIZE(dtcm_cq_grp1), &dtcm_cq1.ptr, FICU_REG_ADDR + FICU_GRP1_FCMD_COMPL_QUE_BASE_ADDR);
#endif
#if NCL_USE_DTCM_SQ
	hdr_reg_init(&dtcm_sq_info[0].reg, dtcm_sq_grp0, ARRAY_SIZE(dtcm_sq_grp0), &dtcm_sq_info[0].ptr, FICU_REG_ADDR + FICU_NORM_FINST_QUE0_BASE_ADDR);
	hdr_reg_init(&dtcm_sq_info[1].reg, dtcm_hsq_grp0, ARRAY_SIZE(dtcm_hsq_grp0), &dtcm_sq_info[1].ptr, FICU_REG_ADDR + FICU_HIGH_FINST_QUE0_BASE_ADDR);
	hdr_reg_init(&dtcm_sq_info[2].reg, dtcm_usq_grp0, ARRAY_SIZE(dtcm_usq_grp0), &dtcm_sq_info[2].ptr, FICU_REG_ADDR + FICU_URGENT_FINST_QUE0_BASE_ADDR);
	hdr_reg_init(&dtcm_sq_info[3].reg, dtcm_sq_grp1, ARRAY_SIZE(dtcm_sq_grp1), &dtcm_sq_info[3].ptr, FICU_REG_ADDR + FICU_NORM_FINST_QUE1_BASE_ADDR);
	hdr_reg_init(&dtcm_sq_info[4].reg, dtcm_hsq_grp1, ARRAY_SIZE(dtcm_hsq_grp1), &dtcm_sq_info[4].ptr, FICU_REG_ADDR + FICU_HIGH_FINST_QUE1_BASE_ADDR);
	hdr_reg_init(&dtcm_sq_info[5].reg, dtcm_usq_grp1, ARRAY_SIZE(dtcm_usq_grp1), &dtcm_sq_info[5].ptr, FICU_REG_ADDR + FICU_URGENT_FINST_QUE1_BASE_ADDR);
#endif
}



/*!
 * @brief Set SRAM config
 *
 * @return Not used
 */
init_code void ficu_sram_conf(void)
{
	//  SRAM start
	ficu_sys_sram_st_addr_reg_t sram_start;
	sram_start.b.ficu_sys_sram_st_addr = SRAM_BASE;
	ficu_writel(sram_start.all, FICU_SYS_SRAM_ST_ADDR_REG);

	//  SRAM end
	ficu_sys_sram_end_addr_reg_t sram_end;
	sram_end.b.ficu_sys_sram_end_addr = SRAM_BASE + SRAM_SIZE - 1;
	ficu_writel(sram_end.all, FICU_SYS_SRAM_END_ADDR_REG);

#ifndef DUAL_BE
	extern struct sram_finst_info_t sram_finst_info;
	struct sram_finst_info_t *sfi = &sram_finst_info;
#else
	//extern struct sram_finst_info_t *sram_finst_info;
	struct sram_finst_info_t *sfi;
	sfi = sram_finst_info = &_sram_finst_info;
#endif

	// SRAM FDA/dtag list base
	ficu_fda_dtag_list_sram_base_addr0_reg_t dtag_sram_base0;
	dtag_sram_base0.b.ficu_fda_dtag_list_sram_base_addr0 = (u32)sfi->fda_dtag_list;
	ficu_writel(dtag_sram_base0.all, FICU_FDA_DTAG_LIST_SRAM_BASE_ADDR0_REG);

	// SRAM FDA/dtag list base
	ficu_fda_dtag_list_sram_base_addr1_reg_t dtag_sram_base1;
	dtag_sram_base1.b.ficu_fda_dtag_list_sram_base_addr1 = (u32)sfi->fda_dtag_list;
	ficu_writel(dtag_sram_base1.all, FICU_FDA_DTAG_LIST_SRAM_BASE_ADDR1_REG);
}

init_code void ficu_ddr_conf(void)
{
	// TODO DDR meta dtag/idx map

}

/*!
 * @brief FSPM initialization
 *
 * @return Not used
 */
norm_ps_code void ficu_fspm_init(void)
{
#ifndef HAVE_SYSTEMC /* fspm is not need zero under systemC, it should only need when ECC/parity enabled */
	ficu_ctrl_reg0_t ctrl_reg0;
	ctrl_reg0.all = ficu_readl(FICU_CTRL_REG0);
	ctrl_reg0.b.ficu_fspm_init = 1;
	ficu_writel(ctrl_reg0.all, FICU_CTRL_REG0);
	ctrl_reg0.all = ficu_readl(FICU_CTRL_REG0);
	while (ctrl_reg0.b.ficu_fspm_init) {
		udelay(1);
		ctrl_reg0.all = ficu_readl(FICU_CTRL_REG0);
	}
#endif
}

/*!
 * @brief Set FSPM config
 *
 * @return Not used
 */
init_code void ficu_fspm_conf(void)
{
	// FSPM start
	ficu_fspm_st_addr_reg_t fspm_start;
	fspm_start.b.ficu_fspm_st_addr = SPRM_BASE;
	ficu_writel(fspm_start.all, FICU_FSPM_ST_ADDR_REG);

	// FSPM end
	ficu_fspm_end_addr_reg_t fspm_end;
	fspm_end.b.ficu_fspm_end_addr = SPRM_BASE + SPRM_SIZE - 1;
	ficu_writel(fspm_end.all, FICU_FSPM_END_ADDR_REG);

	ficu_fspm_init_reg_t fspm_init;
	fspm_init.all = ficu_readl(FICU_FSPM_INIT_REG);
	fspm_init.b.ficu_fspm_init_st_addr = 0;
	fspm_init.b.ficu_fspm_init_sz = (SPRM_SIZE) >> 4;
	ficu_writel(fspm_init.all, FICU_FSPM_INIT_REG);

	ficu_fspm_init();

	// CQ start
	ficu_fcmd_cq_st_addr_reg_t cq_start;
	cq_start.b.ficu_fcmd_cq_st_addr =  (u32)fspm_usage_ptr->cq_fifo;
	ficu_writel(cq_start.all, FICU_FCMD_CQ_ST_ADDR_REG);

	// CQ end
	ficu_fcmd_cq_end_addr_reg_t cq_end;
	cq_end.b.ficu_fcmd_cq_end_addr = (u32)fspm_usage_ptr->cq_fifo_end - 1;
	ficu_writel(cq_end.all, FICU_FCMD_CQ_END_ADDR_REG);

	// FW normal SQ0 start
	ficu_fspm_norm_finst_que0_st_addr_reg_t fw_norm_sq0_start;
	fw_norm_sq0_start.b.ficu_fspm_norm_finst_que0_st_addr =  (u32)fspm_usage_ptr->sq0_fifo;
	ficu_writel(fw_norm_sq0_start.all, FICU_FSPM_NORM_FINST_QUE0_ST_ADDR_REG);

	// FW normal SQ0 end
	ficu_fspm_norm_finst_que0_end_addr_reg_t fw_norm_sq0_end;
	fw_norm_sq0_end.b.ficu_fspm_norm_finst_que0_end_addr = (u32)fspm_usage_ptr->sq0_fifo_end - 1;
	ficu_writel(fw_norm_sq0_end.all, FICU_FSPM_NORM_FINST_QUE0_END_ADDR_REG);

	// FW normal SQ1 start
	ficu_fspm_norm_finst_que1_st_addr_reg_t fw_norm_sq1_start;
	fw_norm_sq1_start.b.ficu_fspm_norm_finst_que1_st_addr =  (u32)fspm_usage_ptr->sq1_fifo;
	ficu_writel(fw_norm_sq1_start.all, FICU_FSPM_NORM_FINST_QUE1_ST_ADDR_REG);

	// FW normal SQ1 end
	ficu_fspm_norm_finst_que1_end_addr_reg_t fw_norm_sq1_end;
	fw_norm_sq1_end.b.ficu_fspm_norm_finst_que1_end_addr = (u32)fspm_usage_ptr->sq1_fifo_end - 1;
	ficu_writel(fw_norm_sq1_end.all, FICU_FSPM_NORM_FINST_QUE1_END_ADDR_REG);

	// FW high SQ0 start
	ficu_fspm_high_finst_que0_st_addr_reg_t fw_high_sq0_start;
	fw_high_sq0_start.b.ficu_fspm_high_finst_que0_st_addr =  (u32)fspm_usage_ptr->hsq0_fifo;
	ficu_writel(fw_high_sq0_start.all, FICU_FSPM_HIGH_FINST_QUE0_ST_ADDR_REG);

	// FW high SQ0 end
	ficu_fspm_high_finst_que0_end_addr_reg_t fw_high_sq0_end;
	fw_high_sq0_end.b.ficu_fspm_high_finst_que0_end_addr = (u32)fspm_usage_ptr->hsq0_fifo_end - 1;
	ficu_writel(fw_high_sq0_end.all, FICU_FSPM_HIGH_FINST_QUE0_END_ADDR_REG);

	// FW high SQ1 start
	ficu_fspm_high_finst_que1_st_addr_reg_t fw_high_sq1_start;
	fw_high_sq1_start.b.ficu_fspm_high_finst_que1_st_addr =  (u32)fspm_usage_ptr->hsq1_fifo;
	ficu_writel(fw_high_sq1_start.all, FICU_FSPM_HIGH_FINST_QUE1_ST_ADDR_REG);

	// FW high SQ1 end
	ficu_fspm_high_finst_que1_end_addr_reg_t fw_high_sq1_end;
	fw_high_sq1_end.b.ficu_fspm_high_finst_que1_end_addr = (u32)fspm_usage_ptr->hsq1_fifo_end - 1;
	ficu_writel(fw_high_sq1_end.all, FICU_FSPM_HIGH_FINST_QUE1_END_ADDR_REG);

	// FW urgent SQ0 start
	ficu_fspm_urgent_finst_que0_st_addr_reg_t fw_urgent_sq0_start;
	fw_urgent_sq0_start.b.ficu_fspm_urgent_finst_que0_st_addr =  (u32)fspm_usage_ptr->usq0_fifo;
	ficu_writel(fw_urgent_sq0_start.all, FICU_FSPM_URGENT_FINST_QUE0_ST_ADDR_REG);

	// FW urgent SQ0 end
	ficu_fspm_urgent_finst_que0_end_addr_reg_t fw_urgent_sq0_end;
	fw_urgent_sq0_end.b.ficu_fspm_urgent_finst_que0_end_addr = (u32)fspm_usage_ptr->usq0_fifo_end - 1;
	ficu_writel(fw_urgent_sq0_end.all, FICU_FSPM_URGENT_FINST_QUE0_END_ADDR_REG);

	// FW urgent SQ1 start
	ficu_fspm_urgent_finst_que1_st_addr_reg_t fw_urgent_sq1_start;
	fw_urgent_sq1_start.b.ficu_fspm_urgent_finst_que1_st_addr =  (u32)fspm_usage_ptr->usq1_fifo;
	ficu_writel(fw_urgent_sq1_start.all, FICU_FSPM_URGENT_FINST_QUE1_ST_ADDR_REG);

	// FW urgent SQ1 end
	ficu_fspm_urgent_finst_que1_end_addr_reg_t fw_urgent_sq1_end;
	fw_urgent_sq1_end.b.ficu_fspm_urgent_finst_que1_end_addr = (u32)fspm_usage_ptr->usq1_fifo_end - 1;
	ficu_writel(fw_urgent_sq1_end.all, FICU_FSPM_URGENT_FINST_QUE1_END_ADDR_REG);

	// Get parameter FIFO start
	ficu_get_param_fifo_st_addr_reg_t param_start;
	param_start.b.ficu_get_param_fifo_st_addr = (u32)fspm_usage_ptr->param_fifo;
	ficu_writel(param_start.all, FICU_GET_PARAM_FIFO_ST_ADDR_REG);

	// Get parameter FIFO end
	ficu_get_param_fifo_end_addr_reg_t param_end;
	param_end.b.ficu_get_param_fifo_end_addr = (u32)fspm_usage_ptr->param_fifo_end - 1;
	ficu_writel(param_end.all, FICU_GET_PARAM_FIFO_END_ADDR_REG);

	// Get parameter FIFO pointer
	ficu_get_param_fifo_ptr_reg_t param_ptr;
	param_ptr.b.ficu_get_param_fifo_ptr = (u32)fspm_usage_ptr->param_fifo;
	ficu_writel(param_ptr.all, FICU_GET_PARAM_FIFO_PTR_REG);

	// ARD Template address
	ficu_ard_conf_reg0_t ard_conf;
	ard_conf.b.ficu_ard_tmpl_base_addr = (u32)fspm_usage_ptr->ard_template;
	ficu_writel(ard_conf.all, FICU_ARD_CONF_REG0);

#if NCL_HAVE_ARD
	// Configure ARD template
	ficu_conf_ard_template();
#endif

	// LUT start
	ficu_lut_st_addr_reg_t lut_start;
	lut_start.b.ficu_lut_st_addr = (u32)fspm_usage_ptr->lut;
	ficu_writel(lut_start.all, FICU_LUT_ST_ADDR_REG);

	// LUT end
	ficu_lut_end_addr_reg_t lut_end;
	lut_end.b.ficu_lut_end_addr = (u32)fspm_usage_ptr->lut_end - 1;
	ficu_writel(lut_end.all, FICU_LUT_END_ADDR_REG);
}

/*!
 * @brief Initialization the SQ pointer value
 *
 * @return Not used
 */
init_code void ficu_sq_ptr_init(void)
{
	// FW normal SQ0 ptr
	ficu_fspm_norm_finst_que0_ptr_reg_t fw_norm_sq0_ptr;
	fw_norm_sq0_ptr.b.ficu_fspm_norm_finst_que0_ptr = 0;
	ficu_writel(fw_norm_sq0_ptr.all, FICU_FSPM_NORM_FINST_QUE0_PTR_REG);

	// FW high SQ0 ptr
	ficu_fspm_high_finst_que0_ptr_reg_t fw_high_sq0_ptr;
	fw_high_sq0_ptr.b.ficu_fspm_high_finst_que0_ptr = 0;
	ficu_writel(fw_high_sq0_ptr.all, FICU_FSPM_HIGH_FINST_QUE0_PTR_REG);

	// FW urgent SQ0 ptr
	ficu_fspm_urgent_finst_que0_ptr_reg_t fw_urgent_sq0_ptr;
	fw_urgent_sq0_ptr.b.ficu_fspm_urgent_finst_que0_ptr = 0;
	ficu_writel(fw_urgent_sq0_ptr.all, FICU_FSPM_URGENT_FINST_QUE0_PTR_REG);

	// FW normal SQ1 ptr
	ficu_fspm_norm_finst_que1_ptr_reg_t fw_norm_sq1_ptr;
	fw_norm_sq1_ptr.b.ficu_fspm_norm_finst_que1_ptr = 0;
	ficu_writel(fw_norm_sq1_ptr.all, FICU_FSPM_NORM_FINST_QUE1_PTR_REG);

	// FW high SQ1 ptr
	ficu_fspm_high_finst_que1_ptr_reg_t fw_high_sq1_ptr;
	fw_high_sq1_ptr.b.ficu_fspm_high_finst_que1_ptr = 0;
	ficu_writel(fw_high_sq1_ptr.all, FICU_FSPM_HIGH_FINST_QUE1_PTR_REG);

	// FW urgent SQ1 ptr
	ficu_fspm_urgent_finst_que1_ptr_reg_t fw_urgent_sq1_ptr;
	fw_urgent_sq1_ptr.b.ficu_fspm_urgent_finst_que1_ptr = 0;
	ficu_writel(fw_urgent_sq1_ptr.all, FICU_FSPM_URGENT_FINST_QUE1_PTR_REG);
}

/*!
 * @brief Enable FICU mode
 *
 * @return Not used
 */
norm_ps_code void ficu_mode_enable(void)
{
	ficu_ctrl_reg0_t reg;

#if NCL_USE_DTCM_SQ
	// Initial FW pointer, set ficu_enable will clear FICU SQ/CQ pointer
	dtcm_sq.wr_ptr = 0;
	dtcm_sq_info[0].ptr = 0;
	dtcm_sq_info[1].ptr = 0;
	dtcm_sq_info[2].ptr = 0;
	dtcm_sq_info[3].ptr = 0;
	dtcm_sq_info[4].ptr = 0;
	dtcm_sq_info[5].ptr = 0;
#endif

	reg.all = ficu_readl(FICU_CTRL_REG0);
	reg.b.ficu_enable = 1;

	ficu_writel(reg.all, FICU_CTRL_REG0);
	/* After switch to register control mode and switch back to ficu mode,
	 * ficu_mode_enable(), should reset instruction FIFO ptr
	*/
#ifndef DUAL_BE
	finst_fifo.fifo_ptr = finst_fifo.fifo_end - 1;
	finst_fifo.fifo_rptr = finst_fifo.fifo_end - 1;
#else
	finst_fifo[0].fifo_ptr = finst_fifo[0].fifo_end - 1;
	finst_fifo[0].fifo_rptr = finst_fifo[0].fifo_end - 1;

	finst_fifo[1].fifo_ptr = finst_fifo[1].fifo_end - 1;
	finst_fifo[1].fifo_rptr = finst_fifo[1].fifo_end - 1;
#endif
	ficu_cq_phase = 0;
	ficu_cq_ptr = fspm_usage_ptr->cq_fifo;
	param_fifo = fspm_usage_ptr->param_fifo;
}

/*!
 * @brief Switch the SQ type
 *
 * @param[in] sq	SQ type
 *
 * @return Not used
 */
fast_code void ficu_sq_switch(u32 sq)
{
#if NCL_USE_DTCM_SQ
	if (sq != ficu_sq_select) {
		switch(sq) {
		case 0:
			dtcm_sq.base_addr = dtcm_sq_grp0;
			dtcm_sq.max_sz = DTCM_SQ_FIFO_COUNT - 1;
			dtcm_sq.ptrs_reg = FICU_NORM_FINST_QUE0_PTRS;
			break;
		case 1:
			dtcm_sq.base_addr = dtcm_hsq_grp0;
			dtcm_sq.max_sz = DTCM_HSQ_FIFO_COUNT - 1;
			dtcm_sq.ptrs_reg = FICU_HIGH_FINST_QUE0_PTRS;
			break;
		case 2:
			dtcm_sq.base_addr = dtcm_usq_grp0;
			dtcm_sq.max_sz = DTCM_USQ_FIFO_COUNT - 1;
			dtcm_sq.ptrs_reg = FICU_URGENT_FINST_QUE0_PTRS;
			break;
		case 3:
			dtcm_sq.base_addr = dtcm_sq_grp1;
			dtcm_sq.max_sz = DTCM_SQ_FIFO_COUNT - 1;
			dtcm_sq.ptrs_reg = FICU_NORM_FINST_QUE1_PTRS;
			break;
		case 4:
			dtcm_sq.base_addr = dtcm_hsq_grp1;
			dtcm_sq.max_sz = DTCM_HSQ_FIFO_COUNT - 1;
			dtcm_sq.ptrs_reg = FICU_HIGH_FINST_QUE1_PTRS;
			break;
		case 5:
			dtcm_sq.base_addr = dtcm_usq_grp1;
			dtcm_sq.max_sz = DTCM_USQ_FIFO_COUNT - 1;
			dtcm_sq.ptrs_reg = FICU_URGENT_FINST_QUE1_PTRS;
			break;
		default:
			sys_assert(0);
			break;
		}
		ficu_norm_finst_que0_ptrs_t finst_que_ptrs;
		finst_que_ptrs.all = ficu_readl(dtcm_sq.ptrs_reg);
		dtcm_sq.wr_ptr = finst_que_ptrs.b._norm_finst_que0_wr_ptr;
		dtcm_sq.rd_ptr = &dtcm_sq_info[sq].ptr;
		ficu_sq_select = sq;
	}
#else
#ifndef DUAL_BE
	u32 fifo_ptr = 0;
	if (sq != ficu_sq_select) {
		switch(sq) {
		case 0:
			finst_fifo.fifo_start = fspm_usage_ptr->sq0_fifo;
			finst_fifo.fifo_end = fspm_usage_ptr->sq0_fifo_end;
			fifo_ptr = ficu_readl(FICU_FSPM_NORM_FINST_QUE0_PTR_REG);
			break;
		case 1:
			finst_fifo.fifo_start = fspm_usage_ptr->hsq0_fifo;
			finst_fifo.fifo_end = fspm_usage_ptr->hsq0_fifo_end;
			fifo_ptr = ficu_readl(FICU_FSPM_HIGH_FINST_QUE0_PTR_REG);
			break;
		case 2:
			finst_fifo.fifo_start = fspm_usage_ptr->usq0_fifo;
			finst_fifo.fifo_end = fspm_usage_ptr->usq0_fifo_end;
			fifo_ptr = ficu_readl(FICU_FSPM_URGENT_FINST_QUE0_PTR_REG);
			break;
		case 3:
			finst_fifo.fifo_start = fspm_usage_ptr->sq1_fifo;
			finst_fifo.fifo_end = fspm_usage_ptr->sq1_fifo_end;
			fifo_ptr = ficu_readl(FICU_FSPM_NORM_FINST_QUE1_PTR_REG);
			break;
		case 4:
			finst_fifo.fifo_start = fspm_usage_ptr->hsq1_fifo;
			finst_fifo.fifo_end = fspm_usage_ptr->hsq1_fifo_end;
			fifo_ptr = ficu_readl(FICU_FSPM_HIGH_FINST_QUE1_PTR_REG);
			break;
		case 5:
			finst_fifo.fifo_start = fspm_usage_ptr->usq1_fifo;
			finst_fifo.fifo_end = fspm_usage_ptr->usq1_fifo_end;
			fifo_ptr = ficu_readl(FICU_FSPM_URGENT_FINST_QUE1_PTR_REG);
			break;
		default:
			sys_assert(0);
			break;
		}
		fifo_ptr /= sizeof(struct finstr_format);
		if (fifo_ptr == 0) {
			finst_fifo.fifo_ptr = finst_fifo.fifo_end - 1;
		} else {
			finst_fifo.fifo_ptr = finst_fifo.fifo_start + fifo_ptr - 1;
		}
		finst_fifo.fifo_rptr = finst_fifo.fifo_ptr;
		//ncb_ficu_trace(LOG_ERR, 0, "Switch to SQ %d: %x %x %x\n", sq, finst_fifo.fifo_start, finst_fifo.fifo_end, finst_fifo.fifo_ptr);
		ficu_sq_select = sq;
	}
#endif
#endif
}

/*!
 * @brief Disable FICU mode
 *
 * @return Not used
 */
norm_ps_code void ficu_mode_disable(void)
{
	ficu_ctrl_reg0_t reg;

	reg.all = ficu_readl(FICU_CTRL_REG0);
	reg.b.ficu_abort_mode = 0;
	ficu_writel(reg.all, FICU_CTRL_REG0);

	reg.all = ficu_readl(FICU_CTRL_REG0);
	reg.b.ficu_enable = 0;
	ficu_writel(reg.all, FICU_CTRL_REG0);
    u32 i = 0;
	do {
		reg.all = ficu_readl(FICU_CTRL_REG0);
        i++;
        if (i % 10000 == 0)
        	ncb_ficu_trace(LOG_INFO, 0x96dd, "ficu mode dis %d", i);
        if (i == 100000) {
        	ncb_ficu_trace(LOG_INFO, 0xcde5, "timeout break");
            break;
        }
	} while (reg.b.ficu_busy);
}

norm_ps_code void ficu_abort(void)
{
	// 2. Disable FICU
	ficu_ctrl_reg0_t reg;
	reg.all = ficu_readl(FICU_CTRL_REG0);
	reg.b.ficu_enable = 0;
	ficu_writel(reg.all, FICU_CTRL_REG0);

	// 3. Check FCMD completion queue, process all pending jobs in CQs. Go to step 4.
	if (cq_in_dtcm) {
		ficu_grp0_fcmd_compl_que_ptrs_t	ptr0;
		ficu_grp1_fcmd_compl_que_ptrs_t	ptr1;

		while (1) {
			ptr0.all = ficu_readl(FICU_GRP0_FCMD_COMPL_QUE_PTRS);
			ptr1.all = ficu_readl(FICU_GRP1_FCMD_COMPL_QUE_PTRS);
			if ((ptr0.b.grp0_fcmd_compl_que_wr_ptr == ptr0.b.grp0_fcmd_compl_que_rd_ptr) &&
			    (ptr1.b.grp1_fcmd_compl_que_wr_ptr == ptr1.b.grp1_fcmd_compl_que_rd_ptr)) {
				break;
			}
			ficu_dtcm_isr();
		}
	} else {
		if (ficu_readl(FICU_FCMD_CQ_CNT_REG) & FICU_FCMD_CQ_CNT_MASK) {
		//ncb_ficu_trace(LOG_ERR, 0, "Cnt %d\n", ficu_readl(FICU_FCMD_CQ_CNT_REG) & FICU_FCMD_CQ_CNT_MASK);
		}
		while (ficu_readl(FICU_FCMD_CQ_CNT_REG) & FICU_FCMD_CQ_CNT_MASK) {
			ficu_isr();
		}
	}

	// 4. Check FICU_ctrl_reg0_.FICU_BUSY, wait until it is 0. Go to step 5.
	do {
		reg.all = ficu_readl(FICU_CTRL_REG0);
	} while (reg.b.ficu_busy);

	struct ncl_cmd_t* ncl_cmd;
	u32 fcmd_id;
	u32 list_len = 0;
	u32 i;
	struct info_param_t* info_list = NULL;
	for (fcmd_id = DTAG_PTR_STR_SQ; fcmd_id < DTAG_PTR_END_SQ; fcmd_id++) {
		ncl_cmd = ncl_cmd_retrieve(fcmd_id);
		if ((u32)ncl_cmd > DTAG_PTR_COUNT) {
			//ncb_ficu_trace(LOG_ERR, 0, "ncl cmd %d timeout\n", ncl_cmd);
			ncl_cmd->status |= NCL_CMD_TIMEOUT_STATUS;
			switch(ncl_cmd->op_code) {
			case NCL_CMD_OP_READ:
            case NCL_CMD_OP_READ_RAW:  //Sean_test_0208
            case NCL_CMD_PATROL_READ:
			case NCL_CMD_OP_READ_STREAMING_FAST:
            #ifdef NCL_FW_RETRY_BY_SUBMIT
            case NCL_CMD_FW_RETRY_READ:
            #endif
				if (ncl_cmd->flags & NCL_CMD_RAPID_PATH) {
					list_len = ncl_cmd->addr_param.rapid_du_param.list_len;
					info_list = &ncl_cmd->addr_param.rapid_du_param.info;
					break;
				}
			case NCL_CMD_OP_WRITE:
			case NCL_CMD_OP_ERASE:
				list_len = ncl_cmd->addr_param.common_param.list_len;
				info_list = ncl_cmd->addr_param.common_param.info_list;
				break;
			case NCL_CMD_OP_READ_ERD:
				list_len = ncl_cmd->addr_param.rapid_du_param.list_len;
				info_list = &ncl_cmd->addr_param.rapid_du_param.info;
				break;
			default:
				sys_assert(0);
				break;
			}
			for (i = 0; i < list_len; i++ ) {
				info_list[i].status = cur_du_timeout;
			}
			ncl_fcmd_completion(fcmd_id);
		}
	}

	// 5.  Check FCMD completion queue, make sure no pending FCMD in CQs, then go to step 6.
	if (cq_in_dtcm) {
		ficu_grp0_fcmd_compl_que_ptrs_t	ptr0;
		ficu_grp1_fcmd_compl_que_ptrs_t	ptr1;

		while (1) {
			ptr0.all = ficu_readl(FICU_GRP0_FCMD_COMPL_QUE_PTRS);
			ptr1.all = ficu_readl(FICU_GRP1_FCMD_COMPL_QUE_PTRS);
			if ((ptr0.b.grp0_fcmd_compl_que_wr_ptr == ptr0.b.grp0_fcmd_compl_que_rd_ptr) &&
			    (ptr1.b.grp1_fcmd_compl_que_wr_ptr == ptr1.b.grp1_fcmd_compl_que_rd_ptr)) {
				break;
			}
			ficu_dtcm_isr();
		}
	} else {
		if (ficu_readl(FICU_FCMD_CQ_CNT_REG) & FICU_FCMD_CQ_CNT_MASK) {
		//ncb_ficu_trace(LOG_ERR, 0, "CNT %d\n", ficu_readl(FICU_FCMD_CQ_CNT_REG) & FICU_FCMD_CQ_CNT_MASK);
		}
		while (ficu_readl(FICU_FCMD_CQ_CNT_REG) & FICU_FCMD_CQ_CNT_MASK) {
			ficu_isr();
		}
	}

	/* 6. Reset SQ&CQ pointers in FSPM or DTCM.
	      Reset processed FSPM SQ in FW to 0. Reset FSPM_FW_SQ  write pointer and FSPM_FW_CQ  read pointer in FW both to 0.
     	 Reset processed DTCM SQ in FW to 0. Reset DTCM_FW_SQ  write pointer and DTCM_FW_CQ read pointer in FW both to 0.
	      Go to step 7. */

	// 7. Enabled FICU
	ficu_mode_enable();
}

/*!
 * @brief Get specific FIFO parameter
 *
 * @return value(FIFO pointer)
 */
fast_code struct param_fifo_format* get_param_fifo(void)
{
	struct param_fifo_format* ptr;
	ptr = param_fifo;
#if DEBUG
	ncb_ficu_trace(LOG_INFO, 0x6531, "%x: fcmd %d finst %d param %x %x", param_fifo, param_fifo->fcmd_id, param_fifo->finst_id, param_fifo->param[0], param_fifo->param[1]);
#endif
	param_fifo++;
	if (param_fifo == fspm_usage_ptr->param_fifo_end) {
		param_fifo = fspm_usage_ptr->param_fifo;
	}
	return ptr;
}

/*!
 * @brief Set the FICU transfer count config
 *
 * @return Not used
 */
norm_ps_code void ficu_xfcnt_reg_cfg(void)
{
	int i;
	ficu_xfcnt_lut_conf_reg_t xfcnt_conf;
	ficu_xfcnt_lut_val_reg_t xfcnt_val;

	for (i = 0; i < XFER_CNT_TABLE_LEN; i++) {
		xfcnt_conf.all = 0;
		xfcnt_conf.b.ficu_xfcnt_lut_conf_ptr = i;
		ficu_writel(xfcnt_conf.all, FICU_XFCNT_LUT_CONF_REG);

		xfcnt_val.all = ficu_readl(FICU_XFCNT_LUT_VAL_REG);
		xfcnt_val.b.ficu_xfcnt_lut_val = xfcnt_tbl[i];
		ficu_writel(xfcnt_val.all, FICU_XFCNT_LUT_VAL_REG);
	}
}

/*!
 * @brief Initialization FICU HW register
 *
 * @return Not used
 */

init_code void ficu_hw_init(void)
{
	ficu_ctrl_reg0_t reg;
	ficu_dummy_du_dtag_reg_t dummy_reg;

	ficu_mode_disable();

	//  Plane number for FICU LUT
	reg.all = ficu_readl(FICU_CTRL_REG0);
	//reg.b.ficu_ard_mode = 1;
#if NCL_HAVE_ARD  //tony20200821
#if 0 //NCL_HAVE_ERD
	reg.b.ficu_ard_mode = 0;
#else
	reg.b.ficu_ard_mode = 2; //use FW_ARD , NCL_FW_ARD
#endif
#endif
	ficu_writel(reg.all, FICU_CTRL_REG0);

	//  Set Dummy DU Dtag
	dummy_reg.all = ficu_readl(FICU_DUMMY_DU_DTAG_REG);
	dummy_reg.b.ficu_dummy_du_dtag = WVTAG_ID;
	ficu_writel(dummy_reg.all, FICU_DUMMY_DU_DTAG_REG);

	//  Configure SRAM/DTCM/DDR/FSPM usage
	ficu_sram_conf();
	ficu_dtcm_conf();
	ficu_ddr_conf();
	ficu_fspm_conf();

	//  Initialize SQ pointers
	ficu_sq_ptr_init();

	// Configure transfer count LUT
	xfcnt_tbl[FINST_XFER_ONE_DU] = get_encoded_du_size();
	xfcnt_tbl[FINST_XFER_PAGE_PAD] = nand_whole_page_size();
	ficu_xfcnt_reg_cfg();

	u32 ficu_int_en_addr_map[] = {
		FICU_INT_EN_REG0,
		FICU_INT_EN_REG1,
		FICU_INT_EN_REG2,
		FICU_INT_EN_REG3,
	};

	ficu_int_en_reg0_t int_reg;
	u32 en_addr = ficu_int_en_addr_map[CPU_ID-1];
	int_reg.all = ficu_readl(en_addr);
#if defined(DUAL_BE)
	#if CPU_ID != CPU_BE_LITE
	int_reg.b.ficu_sq_grp1_fcmd_err_en = 0;
	int_reg.b.ficu_fcmd_grp1_cq_done_en = 0;

	int_reg.b.ficu_fcmd_grp0_cq_done_en = 1;
	int_reg.b.ficu_sq_grp0_fcmd_err_en = 1;
	#endif
#else
	int_reg.b.ficu_sq_grp0_fcmd_err_en = 1;
	int_reg.b.ficu_sq_grp1_fcmd_err_en = 1;
	int_reg.b.ficu_fcmd_grp0_cq_done_en = 1;
	int_reg.b.ficu_fcmd_grp1_cq_done_en = 1;
#endif
	int_reg.b.ficu_fspm_fcmd_cq_done_en = 1;
	int_reg.b.ficu_finst_err_en = 1;
	int_reg.b.ficu_if_rw_err_en = 1;
	int_reg.b.ficu_lst_llrm_done_en = 1;
	int_reg.b.ficu_ard_fifo_err_en = 1;
	int_reg.b.ficu_no_dqs_err_en = 1;
	ficu_writel(int_reg.all, en_addr);

	ficu_pda_conf_reg();
	ficu_mp_row_offset_t mp_row_reg;
	mp_row_reg.all = ficu_readl(FICU_MP_ROW_OFFSET);
#if ROW_WL_ADDR
	// Directly WL address
	u32 mp_row_offset = nand_page_num_slc();
#if HAVE_HYNIX_SUPPORT || HAVE_TSB_SUPPORT
	// Directly WL address, 576, not power of 2
	mp_row_offset = get_next_power_of_two(mp_row_offset);
#else //sandisk
	sys_assert((mp_row_offset & (mp_row_offset - 1)) == 0);
#endif
#else	// Micron, Unic, Samsung
	// Directly page address
	u32 mp_row_offset = nand_page_num();
	if ((mp_row_offset & (mp_row_offset - 1)) == 0) {
	} else {
		mp_row_offset = 1 << (32 - clz(mp_row_offset));
	}
#endif
	mp_row_reg.b.ficu_mp_row_offset = mp_row_offset;
	ficu_writel(mp_row_reg.all, FICU_MP_ROW_OFFSET);
	ficu_row_addr_conf_reg_t row_reg;

	row_reg.b.ficu_row_page_per_wl = nand_bits_per_cell();
	row_reg.b.ficu_row_page_offset = 0;
	row_reg.b.ficu_row_blk_offset = ctz(mp_row_offset);
	row_reg.b.ficu_row_lun_offset = row_reg.b.ficu_row_blk_offset + ctz(get_next_power_of_two(nand_block_num())) + ctz(nand_plane_num());
	row_reg.b.ficu_row_vld_msb = row_reg.b.ficu_row_lun_offset + ctz(nand_lun_num()) + 1;// I don't know why add 1, copied shasta usage
	ficu_writel(row_reg.all, FICU_ROW_ADDR_CONF_REG);

	ficu_meta_dtag_idx_map_unit_size_reg_t meta_size;
	meta_size.all = ficu_readl(FICU_META_DTAG_IDX_MAP_UNIT_SIZE_REG);
	meta_size.b.ficu_meta_dtag_idx_map_unit_size = 32;// Fix use 32
	ficu_writel(meta_size.all, FICU_META_DTAG_IDX_MAP_UNIT_SIZE_REG);

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL
	ficu_ctrl_reg2_t ficu_ctrl_reg2;
	ficu_ctrl_reg2.all = ficu_readl(FICU_CTRL_REG2);
	ficu_ctrl_reg2.b.ficu_raid_compl_if_disable = true;
	ficu_writel(ficu_ctrl_reg2.all, FICU_CTRL_REG2);
#endif

	ficu_mode_enable();

}

/*!
 * @brief Set ARD DU size
 *
 * @param[in] encoded_du_size	DU size
 *
 * @return Not used
 */

norm_ps_code void ficu_set_ard_du_size(u16 encoded_du_size)
{
	xfcnt_tbl[FINST_XFER_ONE_DU] = encoded_du_size;
	ficu_xfcnt_reg_cfg();
}

/*!
 * @brief Set ARD enable or not
 *
 * @param[in] enable	enable or not
 *
 * @return Not used
 */
norm_ps_code void ficu_mode_ard_control(u32 mode)
{
	return;

	ficu_ctrl_reg0_t reg;

	sys_assert(mode <= 2);
#if !NCL_HAVE_ARD
	return;
#endif
	ard_mode = mode;
	reg.all = ficu_readl(FICU_CTRL_REG0);
	reg.b.ficu_ard_mode = mode;
	ficu_writel(reg.all, FICU_CTRL_REG0);
}

norm_ps_code void ficu_isr_init(void)
{
	poll_irq_register(VID_NCB0_INT, ficu_isr);
	poll_irq_register(VID_GRP0_FCMD_CPL, ficu_isr);
#if NCL_USE_BCL
	cq_in_dtcm = true;
#endif
}

norm_ps_code void ficu_isr_resume(enum sleep_mode_t mode)
{
	u32 ficu_int_en_addr_map[] = {
		FICU_INT_EN_REG0,
		FICU_INT_EN_REG1,
		FICU_INT_EN_REG2,
		FICU_INT_EN_REG3,
	};

	ficu_int_en_reg0_t int_reg;
	u32 en_addr = ficu_int_en_addr_map[CPU_ID-1];
	int_reg.all = ficu_readl(en_addr);
#if !defined(DUAL_BE)
	panic("impossible");
#else
	int_reg.b.ficu_fcmd_grp0_cq_done_en = 0;
	int_reg.b.ficu_sq_grp0_fcmd_err_en = 0;
	int_reg.b.ficu_sq_grp1_fcmd_err_en = 1;
	int_reg.b.ficu_fcmd_grp1_cq_done_en = 1;
	int_reg.b.ficu_fspm_fcmd_cq_done_en = 1;
	int_reg.b.ficu_finst_err_en = 1;
	int_reg.b.ficu_if_rw_err_en = 1;
	int_reg.b.ficu_lst_llrm_done_en = 1;
	int_reg.b.ficu_ard_fifo_err_en = 1;
	int_reg.b.ficu_no_dqs_err_en = 1;
#endif
	ficu_writel(int_reg.all, en_addr);

#if defined(DUAL_BE) && (CPU_ID == CPU_BE_LITE)
	hdr_reg_init(&dtcm_cq1.reg, dtcm_cq_grp1, ARRAY_SIZE(dtcm_cq_grp1), &dtcm_cq1.ptr, FICU_REG_ADDR + FICU_GRP1_FCMD_COMPL_QUE_BASE_ADDR);
#endif

	poll_mask(VID_NCB0_INT);
	poll_mask(VID_GRP0_FCMD_CPL);

	//extern void bcl_mgr_resume(enum sleep_mode_t mode);
	//bcl_mgr_resume(mode);
}

/*!
 * @brief Initialization FICU module
 *
 * @return Not used
 */
init_code void ficu_init(void)
{
#if NCL_USE_DTCM_SQ
	dtcm_sq.base_addr = dtcm_sq_grp0;
	dtcm_sq.max_sz = DTCM_SQ_FIFO_COUNT - 1;
	dtcm_sq.wr_ptr = 0;
	dtcm_sq.rd_ptr = &dtcm_sq_info[0].ptr;
	dtcm_sq.ptrs_reg = FICU_NORM_FINST_QUE0_PTRS;
#endif
#ifndef DUAL_BE
	ficu_sq_select = 0;
	finst_fifo.fifo_start = fspm_usage_ptr->sq0_fifo;
	finst_fifo.fifo_end = fspm_usage_ptr->sq0_fifo_end;
#else
	finst_fifo[0].fifo_start = fspm_usage_ptr->sq0_fifo;
	finst_fifo[0].fifo_end = fspm_usage_ptr->sq0_fifo_end;

	finst_fifo[1].fifo_start = fspm_usage_ptr->sq1_fifo;
	finst_fifo[1].fifo_end = fspm_usage_ptr->sq1_fifo_end;
#endif

	ficu_hw_init();

	ficu_ctrl_reg0_t reg0 = {.all = ficu_readl(FICU_CTRL_REG0),};
#if NCL_USE_BCL
	reg0.b.ficu_fcmd_cq_loc_sel = 0x1;
	cq_in_dtcm = true;
#else
	reg0.b.ficu_fcmd_cq_loc_sel = 0x0;
	cq_in_dtcm = false;
#endif
	ficu_writel(reg0.all, FICU_CTRL_REG0);

	extern bool ncl_cold_boot;
	if (ncl_cold_boot) {
		poll_irq_register(VID_NCB0_INT, ficu_isr);
		poll_irq_register(VID_GRP0_FCMD_CPL, ficu_isr);
	}
	poll_mask(VID_NCB0_INT);
	poll_mask(VID_GRP0_FCMD_CPL);

#if !HAVE_HW_CPDA
	ficu_switch_pda_mode();
#endif

#ifdef ERRHANDLE_ECCT
    update_epm_init();
#endif
#ifdef NCL_RETRY_PASS_REWRITE
    host_retry_rewrite_evt_init();
#endif
	ncb_ficu_trace(LOG_INFO, 0x109e, "FICU init done");
}
/*! @} */
