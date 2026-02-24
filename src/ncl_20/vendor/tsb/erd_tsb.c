//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2018 Innogrit Corporation
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
/*! \file erd_tsb.c
 * @brief ERD toshiba
 *
 * \addtogroup ncl
 * \defgroup ncl
 * \ingroup ncl
 * @{
 * Toshiba related ERD handling
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nand.h"
#include "nand_cfg.h"
#include "eccu.h"
#include "finstr.h"
#include "ncl.h"
#include "ncl_cmd.h"
#include "ncl_erd.h"
#include "eccu_pdec_register.h"
#include "eccu_reg_access.h"
#include "nand_tsb.h"

#if NCL_HAVE_ERD && ERD_VENDOR
#define ERD_REREAD_CNT_VENDOR 3
extern u32 erd_id_bmp;

norm_ps_data u32 llr_lut[2] = {		///< LLR LUT for HD + 2 SDs (Strong 1) format
	WEAK_0 | (WEAK_1 << 5) | (MEDIUM_0 << 10) | (MEDIUM_1 << 15) | (MEDIUM_0 << 20) | (MEDIUM_1 << 25) | ((STRONG_0 & 0x03) << 30),
	(STRONG_0 >> 2) | (STRONG_1 << 3)
};

/*!
 * @brief ERD LLR LUT configure
 *
 * @return	not used
 */
norm_ps_code void ncl_erd_cfg_llr_lut(void)
{
	u32 i;
	pdec_erd_llr_lut_reg0_t reg0;
	pdec_erd_llr_lut_reg1_t din;

	reg0.all = pdec_readl(PDEC_ERD_LLR_LUT_REG0);
	reg0.b.eccu_pdec_erd_llr_lut_acc_en = 1;
	pdec_writel(reg0.all, PDEC_ERD_LLR_LUT_REG0);

	for (i = 0; i < 2; i++) {
		reg0.b.eccu_pdec_erd_llr_lut_addr = i;
		pdec_writel(reg0.all, PDEC_ERD_LLR_LUT_REG0);
		din.all = llr_lut[i];
		pdec_writel(din.all, PDEC_ERD_LLR_LUT_REG1);
		reg0.b.eccu_pdec_erd_llr_lut_wr_en = 1;
		pdec_writel(reg0.all, PDEC_ERD_LLR_LUT_REG0);

		reg0.b.eccu_pdec_erd_llr_lut_wr_en = 0;
		pdec_writel(reg0.all, PDEC_ERD_LLR_LUT_REG0);
	}
	reg0.all = pdec_readl(PDEC_ERD_LLR_LUT_REG0);
	reg0.b.eccu_pdec_erd_llr_lut_acc_en = 0;
	pdec_writel(reg0.all, PDEC_ERD_LLR_LUT_REG0);
}

/*!
 * @brief Get maximum ERD steps
 *
 * @return	maximum ERD steps
 */
u32 erd_max_steps_vendor(void)
{
	return RR_STEP_SLC;
}

norm_ps_data u8 tsb_slc_read_shift_value[] = {	///< SLC read vref shift delta (positive and negative) to read soft bit
	50,
	100,
};

norm_ps_data u8 tsb_xlc_read_shift_value[] = {	///< XLC read vref shift delta (positive and negative) to read soft bit
	10,
	20,
};

extern struct finstr_format read_fins_templ;
extern struct finstr_format set_feature_tmpl;
/*!
 * @brief Single-plane ERD ncl command handling
 *
 * @param erd_cmd ERD ncl command pointer
 *
 * @return	not used
 */
fast_code void _ncl_cmd_sp_erd(struct ncl_cmd_t *erd_cmd)
{
	u32 i;
	u32 erd_id;
	u32 fins_cnt = 0;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	u32 ch, ce;
	enum page_type_t pg_type;
	u32 page_no;
	u32 sf_addr = 0;
	u32 sf_val = 0;
	u8* sf_val_delta;
	u32 sf_val_byte = 4;
	u32 plane_no = 0;

	ch = pda2ch(erd_cmd->addr_param.rapid_du_param.pda);
	ce = pda2ce(erd_cmd->addr_param.rapid_du_param.pda);
	page_no = pda2page(erd_cmd->addr_param.rapid_du_param.pda);
#if !QLC_SUPPORT
	if (nand_support_aipr()) {
		plane_no = pda2plane(erd_cmd->addr_param.rapid_du_param.pda);
	}
#endif
	if (erd_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
		pg_type = get_xlc_page_type(page_no);
#if QLC_SUPPORT
		switch(pg_type) {
		case XLC_LOW_PAGE:
			sf_addr = NAND_FA_LOW_READ_RETRY;
			sf_val = tsb_qlc_low_87_rr_offset[erd_cmd->erd_step];
			break;
		case XLC_MID_PAGE:
			sf_addr = NAND_FA_MID_READ_RETRY;
			sf_val = tsb_qlc_mid_88_rr_offset[erd_cmd->erd_step];
			break;
		case XLC_UPR_PAGE:
			sf_addr = NAND_FA_UPR_READ_RETRY;
			sf_val = tsb_qlc_upr_89_rr_offset[erd_cmd->erd_step];
			sf_val_byte = 3;
			break;
		case XLC_TOP_PAGE:
			sf_addr = NAND_FA_TOP_READ_RETRY;
			sf_val = tsb_qlc_top_8a_rr_offset[erd_cmd->erd_step];
			break;
		default:
			sys_assert(0);
			break;
		}
#else
		if (pg_type == XLC_MID_PAGE) {
			if (plane_no == 0) {
				sf_addr = NAND_FA_MID_READ_RETRY;
			} else { // AIPR plane 1 feature address
				sf_addr = NAND_FA_MID_READ_RETRY_P1;
			}
			if (nand_info.id[1] == 0x3E) {	//4T
				sf_val = tsb_tlc_mid_8a_rr_offset_256Gb[erd_cmd->erd_step];
			} else {
				sf_val = tsb_tlc_mid_8a_rr_offset_512Gb[erd_cmd->erd_step];
			}
			//sf_val = tsb_tlc_mid_8a_rr_offset[erd_cmd->erd_step];
			sf_val_byte = 3;
		} else {
			sys_assert(NAND_FA_LOW_READ_RETRY == NAND_FA_UPR_READ_RETRY);
			if (plane_no == 0) {
				sf_addr = NAND_FA_LOW_READ_RETRY;
			} else { // AIPR plane 1 feature address
				sf_addr = NAND_FA_LOW_READ_RETRY_P1;
			}
			if (nand_info.id[1] == 0x3E) {	//4T
				sf_val = tsb_tlc_low_upr_89_rr_offset_256Gb[erd_cmd->erd_step];
			} else {
				sf_val = tsb_tlc_low_upr_89_rr_offset_512Gb[erd_cmd->erd_step];
			}
			//sf_val = tsb_tlc_low_upr_89_rr_offset[erd_cmd->erd_step];
		}
#endif
		sf_val_delta = tsb_xlc_read_shift_value;
	} else {
		pg_type = SLC_PAGE;
#if QLC_SUPPORT
		sf_addr = NAND_FA_SLC_READ_RETRY;
#else
		if (plane_no == 0) {
			sf_addr = NAND_FA_SLC_READ_RETRY;
		} else { // AIPR plane 1 feature address
			sf_addr = NAND_FA_SLC_READ_RETRY_P1;
		}
#endif
		sf_val = tsb_slc_8b_rr_offset[erd_cmd->erd_step];
		sf_val_delta = tsb_slc_read_shift_value;
		sf_val_byte = 1;
	}

	erd_id = ctz(erd_id_bmp);
	erd_id_bmp &= ~(1 << erd_id);
	erd_cmd->erd_id = erd_id;
	erd_cmd->dtag_ptr = erd_id;
	ncl_cmd_save(erd_cmd->erd_id, erd_cmd);

	fda_dtag = ficu_get_addr_dtag_ptr(erd_id);
	// Configure PDA & Dtag
	fda_dtag->pda = erd_cmd->addr_param.rapid_du_param.pda;
	fda_dtag->dtag.all = erd_cmd->user_tag_list[0].all;

	fda_dtag->ch_id = ch;
	fda_dtag->dev_id = ce;
	fda_dtag->fda_col = sf_addr;
	set_feature_tmpl.dw1.b.du_dtag_ptr = erd_id;

	erd_fins_templ.dw0.b.nvcmd_tag_ext = !!(erd_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
	erd_fins_templ.dw0.b.pg_idx = erd_cmd->addr_param.rapid_du_param.info.xlc.slc_idx;
	erd_fins_templ.dw1.b.fcmd_id = erd_cmd->erd_id;
	erd_fins_templ.dw1.b.du_dtag_ptr = erd_id;
	erd_fins_templ.dw2.b.btn_op_type = erd_cmd->op_type;
	if (erd_cmd->flags & NCL_CMD_META_DISCARD) {
		erd_fins_templ.dw2.b.meta_strip = 1;
	}
	erd_fins_templ.dw2.b.du_fmt_sel = erd_cmd->du_format_no;

	// 00h-5addr-30, 05h-5addr-E0h to send out hard data
	// SF positive, 00h-5addr-30h, SF negative, 00h-5addr-3Ch, CBh, 3Fh, 05h-5addr-E0h send out soft data (strong 1)
	// SF positive, 00h-5addr-30h, SF negative, 00h-5addr-3Ch, CBh, 3Fh, 05h-5addr-E0h send out soft data (strong 1)
	set_feature_tmpl.dw1.b.fcmd_id = erd_id;
	for (i = 0; i < ERD_REREAD_CNT_VENDOR; i++) {
		if (i == 0) {
			ins = set_feature_tmpl;
			ins.dw2.all = sf_val;
			ins.dw0.b.fins = 1;
			ficu_fill_finst(ins);
			fins_cnt++;
		} else {
			struct finstr_format rcmd_ins;
			u32 j;
			u8* byte;

			// Set feature positive
			ins = set_feature_tmpl;
			byte = (u8*)&ins.dw2.all;
			for (j = 0; j < sf_val_byte; j++) {
				byte[j] = ((sf_val >> (j * 8)) & 0xFF) + sf_val_delta[i - 1];
			}
			ficu_fill_finst(ins);
			fins_cnt++;

			// 00h-30h cmd w/o data
			rcmd_ins = read_fins_templ;
			if (erd_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
				rcmd_ins.dw0.b.xlc_en = ROW_WL_ADDR;
				// Implicit TLC_(LOW/MID/UPR)_PAGE, value 0/1/2
				sys_assert(XLC_LOW_PAGE == 0);
#if !QLC_SUPPORT
				rcmd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_type);
#else
				rcmd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_REREAD_LOW + pg_type;
#endif
			}
#if QLC_SUPPORT
			else {
				rcmd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_REREAD_SLC;
			}
#endif
			rcmd_ins.dw1.b.xfcnt_sel = FINST_XFER_ZERO;
			rcmd_ins.dw1.b.fcmd_id = erd_cmd->erd_id;
			rcmd_ins.dw1.b.du_dtag_ptr = erd_id;
			ficu_fill_finst(rcmd_ins);
			fins_cnt++;

			// Set feature negative
			ins = set_feature_tmpl;
			for (j = 0; j < sf_val_byte; j++) {
				byte[j] = ((sf_val >> (j * 8)) & 0xFF) - sf_val_delta[i - 1];
			}
			ficu_fill_finst(ins);
			fins_cnt++;

			// Read command 00h-5addr-3Ch
			if (erd_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
				rcmd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_SBN_READ(pg_type);
			} else {
				rcmd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_SOFT_BIT_SBN_READ;
			}
			ficu_fill_finst(rcmd_ins);
			fins_cnt++;

			// CBh
			rcmd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XNOR_SB_CMD1;
			ficu_fill_finst(rcmd_ins);
			fins_cnt++;

			// 3Fh
			rcmd_ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XNOR_SB_CMD2;
			ficu_fill_finst(rcmd_ins);
			fins_cnt++;
		}

		// Configure instruction
		ins = erd_fins_templ;

		if (i == 0) {
			if (erd_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
				ins.dw0.b.xlc_en = ROW_WL_ADDR;
				// Implicit TLC_(LOW/MID/UPR)_PAGE, value 0/1/2
				sys_assert(XLC_LOW_PAGE == 0);
#if !QLC_SUPPORT
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pg_type);
#else
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_REREAD_LOW + pg_type;
#endif
			}
#if QLC_SUPPORT
			else {
				ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_REREAD_SLC;
			}
#endif
		} else {
			ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
			ins.dw0.b.finst_type = FINST_TYPE_READ_DATA;
		}

		if ((i + 1) == ERD_REREAD_CNT_VENDOR) {
			ins.dw2.b.sd_mode = 1;
		}

		ficu_fill_finst(ins);
		fins_cnt++;

		// Clear set feature before HD read
		if ((i + 1) == ERD_REREAD_CNT_VENDOR) {
			ins = set_feature_tmpl;
			ins.dw2.all = 0;
			ficu_fill_finst(ins);
			fins_cnt++;
		}
	}
	//ncl_vendor_trace(LOG_ERR, 0, "    erd %x reread step %d\n", erd_cmd, erd_cmd->erd_step);

	// ERD start
	ins = erd_fins_templ;
	ins.dw0.b.lins = 1;
	ins.dw2.b.erd_cmd = ERD_DEC_START;
	ins.dw0.b.erd_dec_mask = (1 << erd_cmd->addr_param.rapid_du_param.list_len) - 1;
	ficu_fill_finst(ins);

	// Final submit instruction
	ficu_submit_finstr(fins_cnt + 1);
}
#endif
