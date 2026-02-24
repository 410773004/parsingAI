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
/*! \file ncl_erd.c
 * @brief NCL ERD module
 *
 * \addtogroup ncl_20
 * \defgroup ncl_erd
 * \ingroup ncl_20
 *
 * @{
 */

#if NCL_HAVE_ERD
#include "nand.h"
#include "nand_cfg.h"
#include "eccu_pdec_register.h"
#include "eccu_reg_access.h"
#include "eccu.h"
#include "ncl.h"
#include "ncl_err.h"
#include "ncl_cmd.h"
#include "ncl_erd.h"
#include "ddr.h"
/*! \cond PRIVATE */
#define __FILEID__ erd
#include "trace.h"
/*! \endcond */
#define ERD_REREAD_CNT	3
#if ERD_VENDOR
#define ERD_MAX_STEPS	erd_max_steps_vendor()
#else
#define ERD_MAX_STEPS	3
#endif

extern void ficu_mode_ard_control(u32 mode);
extern void eccu_set_erd_dec(u32 dec);

// 3 hard data
norm_ps_data __attribute__((weak)) u32 llr_lut[2] = {		// LLR LUT for 3-HD read ERD
	STRONG_0 | (WEAK_0 << 5) | (WEAK_0 << 10) | (WEAK_1 << 15) | (WEAK_0 << 20) | (WEAK_1 << 25) | ((WEAK_1 & 0x3) << 30),
	(WEAK_1 >> 2) | (STRONG_1 << 3)
};
fast_data bool support_erd = true;
norm_ps_data struct finstr_format erd_fins_templ = {	///< ERD F-inst template
	.dw0 = {
		.b.fins_vld		= 1,
		.b.fins			= 0,
		.b.lins			= 0,

		.b.poll_dis		= 0,
		.b.susp_en		= 0,
		.b.mlun_en		= 1,
		.b.mp_row_offset_en	= 1,

		.b.ndcmd_fmt_sel	= FINST_NAND_FMT_SLC_READ_CMD,
		.b.vsc_en		= 0,

		.b.mp_num		= 0,
		.b.finst_type		= FINST_TYPE_READ,
		.b.fins_fuse		= 0,

		.b.rsvd0		= 0,
		.b.pg_idx		= 0,
		.b.xlc_en		= 0,
		.b.raw_addr_en		= 0,
		.b.nvcmd_tag_ext	= 0,
		.b.no_eccu_path		= 0,
	},
	.dw1 = {
		.b.fcmd_id		= 0,
		.b.rsvd0		= 0,
		.b.du_dtag_ptr		= 0,
		.b.finst_info_loc	= FINST_INFO_IN_SRAM,
		.b.xfcnt_sel		= FINST_XFER_AUTO,
	},
	.dw2 = {
		.b.host_trx_dis		= 0,
		.b.ard_schem_sel	= 0,

		.b.du_fmt_sel		= DU_4K_DEFAULT_MODE,
		.b.du_num		= 0,

		.b.scrc_en		= NCL_HAVE_HCRC,
		.b.meta_strip		= 0,
		.b.hc			= 0,

		.b.force_retry_en	= 0,
		.b.sd_mode		= 0,
		.b.erd_en		= 1,
	},
};

/*!
 * @brief Configure LLR LUT for basic 3-HD ERD
 *
 * @return	not used
 */
norm_ps_code __attribute__((weak)) void ncl_erd_cfg_llr_lut(void)
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

#include "nand.h"
extern struct finstr_format read_fins_templ;
#define ERD_NUM_PER_CH 4
fast_data_zi u8 erd_cnt_tbl_by_ch[MAX_CHANNEL] = {0};

fast_data_ni ncl_cmd_queue erd_waiting_queue;
fast_data u32 erd_id_bmp = ~0;
#if !ERD_VENDOR
/*!
 * @brief Basic universal ERD of 3 HD read
 * Currently HW only support single plane ERD, 1 to 4 consecutive DU(s)
 *
 * @param erd_cmd	ERD command
 *
 * @return	not used
 */
fast_code __attribute__((weak)) void _ncl_cmd_sp_erd(struct ncl_cmd_t *erd_cmd)
{
	u32 i;
	u32 erd_id;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;

	erd_id = ctz(erd_id_bmp);
	erd_id_bmp &= ~(1 << erd_id);
	erd_cmd->erd_id = erd_id;
	erd_cmd->dtag_ptr = erd_id;
	ncl_cmd_save(erd_cmd->erd_id, erd_cmd);

	fda_dtag = ficu_get_addr_dtag_ptr(erd_id);
	// Configure PDA & Dtag
	fda_dtag->pda = erd_cmd->addr_param.rapid_du_param.pda;
	fda_dtag->dtag.all = erd_cmd->user_tag_list[0].all;
	if (erd_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG) {
		erd_fins_templ.dw0.b.xlc_en = ROW_WL_ADDR;
		erd_fins_templ.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pda2pg_idx(erd_cmd->addr_param.rapid_du_param.pda));
	} else {
		erd_fins_templ.dw0.b.xlc_en = 0;
		erd_fins_templ.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD;
	}
	erd_fins_templ.dw0.b.nvcmd_tag_ext = !!(erd_cmd->flags & NCL_CMD_TAG_EXT_FLAG);
	erd_fins_templ.dw0.b.pg_idx = erd_cmd->addr_param.rapid_du_param.info.xlc.slc_idx;
	erd_fins_templ.dw1.b.fcmd_id = erd_cmd->erd_id;
	erd_fins_templ.dw1.b.du_dtag_ptr = erd_id;
	erd_fins_templ.dw2.b.btn_op_type = erd_cmd->op_type;
	if (erd_cmd->flags & NCL_CMD_META_DISCARD) {
		erd_fins_templ.dw2.b.meta_strip = 1;
	}
	erd_fins_templ.dw2.b.du_fmt_sel = erd_cmd->du_format_no;
	for (i = 0; i < ERD_REREAD_CNT; i++) {
		// Configure instruction
		ins = erd_fins_templ;

		if (i == 0) {
			ins.dw0.b.fins = 1;
		}
		if ((i + 1) == ERD_REREAD_CNT) {
			ins.dw2.b.sd_mode = 1;
		}
		ficu_fill_finst(ins);
	}
	//ncl_erd_trace(LOG_ERR, 0, "    erd %x reread step %d\n", erd_cmd, erd_cmd->erd_step);

	// Configure instruction
	ins = erd_fins_templ;

	ins.dw0.b.lins = 1;
	ins.dw2.b.erd_cmd = ERD_DEC_START;
	ins.dw0.b.erd_dec_mask = (1 << erd_cmd->addr_param.rapid_du_param.list_len) - 1;
	ficu_fill_finst(ins);
	//ncl_erd_trace(LOG_ERR, 0, "    erd %x start\n", erd_cmd);

	// Final submit instruction
	ficu_submit_finstr(ERD_REREAD_CNT + 1);
}
#endif

/*!
 * @brief Update original NCL command DU status based on ERD sucess/failure
 *
 * @param ncl_cmd	Orignial NCL command
 * @param erd_cmd	Derived ERD command
 *
 * @return	not used
 */
fast_code void ncl_cmd_erd_update_status(struct ncl_cmd_t *ncl_cmd, struct ncl_cmd_t *erd_cmd)
{
	u32 i, j;

	for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
		if (ncl_cmd->addr_param.common_param.pda_list[i] == erd_cmd->addr_param.rapid_du_param.pda) {
			sys_assert((i + erd_cmd->addr_param.rapid_du_param.list_len) <= ncl_cmd->addr_param.common_param.list_len);
			for (j = 0; j < erd_cmd->addr_param.rapid_du_param.list_len; j++) {
				if (erd_cmd->status & NCL_CMD_ERROR_STATUS) {
					ncl_cmd->addr_param.common_param.info_list[i + j].status = erd_cmd->addr_param.rapid_du_param.info.status;
					ncl_erd_trace(LOG_ALW, 0xdd1f, "NCL err: cmd 0x%x, PDA 0x%x(%d/%d) type %d erd fail %d", ncl_cmd, ncl_cmd->addr_param.common_param.pda_list[i + j], i + j, ncl_cmd->addr_param.common_param.list_len, ncl_cmd->addr_param.common_param.info_list[i + j].pb_type, erd_cmd->addr_param.rapid_du_param.info.status);
				} else {
					ncl_cmd->addr_param.common_param.info_list[i + j].status = cur_du_ovrlmt_err;
					ncl_erd_trace(LOG_ALW, 0xdac8, "cmd 0x%x, PDA 0x%x(%d/%d) type %d erd ok @ %d", ncl_cmd, ncl_cmd->addr_param.common_param.pda_list[i + j], i + j, ncl_cmd->addr_param.common_param.list_len, ncl_cmd->addr_param.common_param.info_list[i + j].pb_type, erd_cmd->erd_step);
				}
			}
		}
	}
	for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
		if (ncl_cmd->addr_param.common_param.info_list[i].status != cur_du_good) {
			break;
		}
	}
	if (i == ncl_cmd->addr_param.common_param.list_len) {
		ncl_cmd->status &= ~NCL_CMD_ERROR_STATUS;
	}
}
extern bool erd_subcmd_start(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief Single plane ERD completion handler
 *
 * @param erd_cmd	ERD command
 *
 * @return	not used
 */
fast_code void ncl_cmd_sp_erd_cmpl(struct ncl_cmd_t *erd_cmd)
{
	u32 ch;
	struct ncl_cmd_t* ncl_cmd;

	ch = pda2ch(erd_cmd->addr_param.rapid_du_param.pda);
	erd_cnt_tbl_by_ch[ch]--;

	//ncl_erd_trace(LOG_ERR, 0, "    erd %x cmpl\n", erd_cmd);

	if (erd_cmd->status & NCL_CMD_ERROR_STATUS) {
		if (++erd_cmd->erd_step < ERD_MAX_STEPS) {
			erd_cmd->status &= ~NCL_CMD_ERROR_STATUS;
			erd_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
			ncl_cmd_submit(erd_cmd);
			return;
		}
	}
	QSIMPLEQ_FOREACH(ncl_cmd, &erd_waiting_queue, entry) {
		if (ncl_cmd->erd_ch_bmp & (1 << ch)) {
			ncl_cmd->erd_ch_bmp &= ~(1 << ch);
			while(erd_subcmd_start(ncl_cmd));
		}
	}

	if (erd_cmd->caller_priv != NULL) {
		ncl_cmd = (struct ncl_cmd_t*)erd_cmd->caller_priv;
		// Find in original PDA list and update du status
		ncl_cmd_erd_update_status(ncl_cmd, erd_cmd);
		ncl_cmd->erd_cnt--;
		if ((ncl_cmd->erd_cnt == 0) && (ncl_cmd->erd_id == ncl_cmd->addr_param.common_param.list_len)) {
			//ncl_erd_trace(LOG_ERR, 0, "cmd %x cmpl\n", ncl_cmd);
			ncl_cmd->flags |= NCL_CMD_COMPLETED_FLAG;
			QSIMPLEQ_REMOVE(&erd_waiting_queue, ncl_cmd, ncl_cmd_t, entry);
			extern u32 fcmd_rw_outstanding_cnt;
			fcmd_rw_outstanding_cnt--;
			if (ncl_cmd->completion) {
				ncl_cmd->completion(ncl_cmd);
			}
		}
		sys_free(SLOW_DATA, erd_cmd);
	}
}

/*!
 * @brief Single plane ERD submission
 *
 * @param ncl_cmd	NCL command
 *
 * @return	ERD sub command started or not
 */
fast_code void ncl_cmd_sp_erd(struct ncl_cmd_t *erd_cmd)
{
	u32 ch;

	ch = pda2ch(erd_cmd->addr_param.rapid_du_param.pda);
	if (erd_cnt_tbl_by_ch[ch] == ERD_NUM_PER_CH) {// ERD number on this channel maximum
		sys_assert(0);
		return;
	}
	// ERD re-read fcmd
	if (erd_cmd->completion != NULL) {// Sync
		erd_cnt_tbl_by_ch[ch]++;
	}
	_ncl_cmd_sp_erd(erd_cmd);
}

/*!
 * @brief ERD sub command start
 *
 * @param ncl_cmd	NCL command
 *
 * @return	ERD sub command started or not
 */
fast_code bool erd_subcmd_start(struct ncl_cmd_t *ncl_cmd)
{
	u32 ch;
	u32 cnt;
	u32 i;
	u32 idx;
	pda_t* pda_list;
	struct info_param_t	*info_list;
	u32 pda_cnt;

	// Find 1st decoding error DU from current position
	while (1) {
		if (ncl_cmd->erd_id == ncl_cmd->addr_param.common_param.list_len) {
			return false;
		}
		//ncl_erd_trace(LOG_ERR, 0, "idx %d %d\n", ncl_cmd->erd_id, ncl_cmd->addr_param.common_param.info_list[ncl_cmd->erd_id].status);
		if (ncl_cmd->addr_param.common_param.info_list[ncl_cmd->erd_id].status == cur_du_ucerr) {
#if HAVE_HYNIX_SUPPORT
			if (ncl_cmd->addr_param.common_param.info_list[ncl_cmd->erd_id].pb_type == NAL_PB_TYPE_SLC) {
				continue;
			}
#endif
			break;
		}
		ncl_cmd->erd_id++;
	}

	idx = ncl_cmd->erd_id;
	info_list = ncl_cmd->addr_param.common_param.info_list + idx;
	pda_list = ncl_cmd->addr_param.common_param.pda_list + idx;
	pda_cnt = ncl_cmd->addr_param.common_param.list_len - idx;

	// Check this channel is able to start another ERD cmd
	ch = pda2ch(pda_list[0]);
	if (erd_cnt_tbl_by_ch[ch] == ERD_NUM_PER_CH) {// ERD number on this channel maximum
		ncl_cmd->erd_ch_bmp |= 1 << ch;
		return false;
	}

	// Merge consecutive dec error du within same page
	cnt = 1;
if (0) {
	for (i = 1; i < pda_cnt; i++) {
		if (info_list[i].status != cur_du_ucerr) {
			break;
		}
		if (pda_list[i] != (pda_list[i - 1] + 1)) {
			break;// Not DU sequential
		}
		if ((pda_list[i] & (DU_CNT_PER_PAGE - 1)) == 0) {// About to across page boundary
			break;
		}
		cnt++;
	}
}

	struct ncl_cmd_t* erd_cmd;
	erd_cmd = sys_malloc(SLOW_DATA, sizeof(struct ncl_cmd_t));
	if (erd_cmd == NULL) {
		return false;
	}
	ncl_cmd->erd_id += cnt;
	memset(erd_cmd, 0, sizeof(struct ncl_cmd_t));
	erd_cmd->caller_priv = (void*)ncl_cmd;
	ncl_cmd->erd_cnt++;
	erd_cmd->erd_step = 0;
	erd_cmd->du_format_no = ncl_cmd->du_format_no;
	erd_cmd->op_code = NCL_CMD_OP_READ_ERD;
	erd_cmd->op_type = ncl_cmd->op_type;
	erd_cmd->status = 0;
	erd_cmd->flags = ncl_cmd->flags;
	if (info_list[0].pb_type == NAL_PB_TYPE_SLC) {
		erd_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		erd_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	}
	erd_cmd->addr_param.rapid_du_param.pda = pda_list[0];
	erd_cmd->addr_param.rapid_du_param.list_len = cnt;
	erd_cmd->addr_param.rapid_du_param.info.xlc.slc_idx = 0;
	erd_cmd->addr_param.rapid_du_param.info.status = cur_du_good;
	erd_cmd->addr_param.rapid_du_param.info.pb_type = info_list[0].pb_type;
	erd_cmd->user_tag_list = ncl_cmd->user_tag_list + idx;
	erd_cmd->completion = ncl_cmd_sp_erd_cmpl;
	//ncl_erd_trace(LOG_ERR, 0, "    erd %x @ %d, pda 0x%x cnt %d\n", erd_cmd, idx, pda_list[0], cnt);
	ncl_cmd_submit(erd_cmd);

	return true;
}

/*!
 * @brief Add a NCL command that need ERD (1 or more DU)
 *
 * @param ncl_cmd	NCL command
 *
 * @return	All ERD tasks submitted or not, if not, need continue ERD in later
 */
fast_code bool ncl_cmd_erd_add(struct ncl_cmd_t *ncl_cmd)
{
	bool retry = false;
	ncl_cmd->erd_id = 0;
	ncl_cmd->erd_cnt = 0;
	QSIMPLEQ_INSERT_TAIL(&erd_waiting_queue, ncl_cmd, entry);
	//ncl_erd_trace(LOG_ERR, 0, "cmd %x retry\n", ncl_cmd);
	// Add ncl_cmd to ERD handling queue
	while (erd_subcmd_start(ncl_cmd)) {
		retry = true;
	}
	if (!retry) {
		if (ncl_cmd->erd_id == ncl_cmd->addr_param.common_param.list_len) {
			QSIMPLEQ_REMOVE(&erd_waiting_queue, ncl_cmd, ncl_cmd_t, entry);
		} else {
			retry = true;
		}
	}
	return retry;
}

/*!
 * @brief ERD HW initialization
 *
 * @return	Not used
 */
init_code void ncl_erd_hw_init(void)
{
	dec_top_ctrs_reg3_t erd_reg;
	erd_reg.all = dec_top_readl(DEC_TOP_CTRS_REG3);
	erd_reg.b.eccu_erd_max_num_ch = ERD_NUM_PER_CH;
	erd_reg.b.eccu_erd_max_rr_num = 9;// Direct mode 9 re-read at most, indirect mode HW not support yet
	erd_reg.b.eccu_erd_max_du_sz = nand_whole_page_size() / DU_CNT_PER_PAGE;
	erd_reg.b.eccu_erd_max_du_sz += 0x1F;// Should be 32B align
	erd_reg.b.eccu_erd_max_du_sz &= ~0x1F;
	erd_reg.b.eccu_erd_rr_def_val = 0;
	dec_top_writel(erd_reg.all, DEC_TOP_CTRS_REG3);

	ncl_erd_cfg_llr_lut();
	eccu_set_erd_dec(ERD_MODE_PDEC);

	u32 dtag_cnt;
	u32 dtag_start;
	dec_erd_ddr_st_addr_reg_t start_addr_reg;
	dec_erd_ddr_end_addr_reg_t end_addr_reg;
	start_addr_reg.all = dec_top_readl(DEC_ERD_DDR_ST_ADDR_REG);
	end_addr_reg.all = dec_top_readl(DEC_ERD_DDR_END_ADDR_REG);
	dtag_cnt = (end_addr_reg.all - start_addr_reg.all) << 4;	// it's 16 byte address
	dtag_cnt = (dtag_cnt + DTAG_SZE - 1) >> DTAG_SHF;
	dtag_start = ddr_dtag_register(dtag_cnt);
	if (dtag_start == ~0) {
		sys_assert(0);
	} else {
		dec_top_writel(ddtag2off(dtag_start) >> 4, DEC_ERD_DDR_ST_ADDR_REG);
	}
}

/*!
 * @brief ERD initialization
 *
 * @return	Not used
 */
init_code void ncl_erd_init(void)
{
#ifdef FPGA
	extern soc_cfg_reg1_t soc_cfg_reg1;
	if (soc_cfg_reg1.b.ddr == 0) {
		ncl_erd_trace(LOG_INFO, 0x0a76, "Disable ERD on H2K");
		support_erd = false;
		return;
	}
	// See ticket #3977
	if ((nand_channel_num() > 1) && (soc_svn_id < 0x10265)) {
		ncl_erd_trace(LOG_INFO, 0xb6e9, "Disable ERD for ticket#3977");
		support_erd = false;
		return;
	}
#endif
#if defined(U2)
	//if (nand_is_bics4_800mts()) {
		// disable first, under debugging
		support_erd = false;
		return;
	//}
#endif
	ncl_erd_trace(LOG_INFO, 0xf334, "ERD enabled");
	memset(erd_cnt_tbl_by_ch, 0, nand_channel_num());
	QSIMPLEQ_INIT(&erd_waiting_queue);

#if !defined(DUAL_BE)
	erd_id_bmp = ~0;
#else
#if CPU_BE == CPU_ID
	erd_id_bmp = (1 << 16) - 1;
#endif
#if CPU_BE_LITE == CPU_ID
	erd_id_bmp = ((1 << 16) - 1) << 16;
#endif
#endif
}
#endif
/*! @} */
