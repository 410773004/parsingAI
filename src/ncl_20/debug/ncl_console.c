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
/*! \file ncl_console.c
 * @brief ncl console command
 *
 * \addtogroup debug
 * \defgroup ncl_console
 * \ingroup debug
 *
 * @{
 */

#include "ncl.h"
#include "ndphy.h"
#include "ndcu.h"
#include "eccu.h"
#include "ncb_ficu_register.h"
#include "ficu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "ndcu_reg_access.h"
#include "ncb_eccu_register.h"
#include "dec_top_ctrl_register.h"
#include "eccu_mdec_register.h"
#include "eccu_pdec_register.h"
#include "eccu_reg_access.h"
#include "nand.h"
#include "nand_define.h"
#include "nand_cfg.h"
#include "ncl_cmd.h"
#include "ncl_err.h"
#include "finstr.h"
#include "ficu.h"
#include "srb.h"
#include "../../nvme/apl/decoder/ssstc_cmd.h"   //Sean_20220707
/*! \cond PRIVATE */
#define __FILEID__ nclconsole
#include "trace.h"
#include "ncl.h"
#include "ncl_exports.h"
#include "ddr_info.h"
#include "ddr.h"
#include "spi.h"
#include "spi_register.h"
#include "GList.h"
/*! \endcond */

#define UART_SRB_ERASE		1
#define EX_URAT_ENABLE 		0
#ifdef SAVE_DDR_CFG	//20201008-Eddie
#define CLEAR_DDRTRAIN_DONE		1
#else
#define CLEAR_DDRTRAIN_DONE		0
#endif
ddr_data u32 SPI_record_btyecnt = 0;

#ifdef NCL_RETRY_PASS_REWRITE
// idx 0 for SLC, idx 1-7 or 1-15 for XLC
#include "nand.h"

/*!
 * @brief Set feature for nand VREF
 *
 * @param[in] pda		test PDA
 * @param[in] idx		test Vt index(0 to 7)
 * @param[in] val		test value
 *
 * @return		Not uesd
 */

ddr_code void nand_vref_cfg(pda_t pda, u8 idx, u8 val)
{
	u32 fa = 0;
	u32 fval = 0;

#if HAVE_TSB_SUPPORT
	u32 shift = 0;
	switch(idx) {
	case 0:
		fa = NAND_FA_SLC_READ_RETRY;
		break;
#if !QLC_SUPPORT
	case 1:
	case 5:
		fa = NAND_FA_LOW_READ_RETRY;
		shift = (idx == 1) ? 0 : 2;
		break;
	case 3:
	case 7:
		fa = NAND_FA_UPR_READ_RETRY;
		shift = (idx == 3) ? 1 : 3;
		break;
	case 2:
	case 4:
	case 6:
		fa = NAND_FA_MID_READ_RETRY;
		shift = (idx - 2) >> 1;
		break;
#else
	// Vref for low pages
	case 1:
	case 4:
	case 6:
	case 11:
		fa = NAND_FA_LOW_READ_RETRY;
		shift = idx / 3;
		break;
	// Vref for middle pages
	case 3:
	case 7:
	case 9:
	case 13:
		fa = NAND_FA_MID_READ_RETRY;
		shift = idx / 4;
		break;
	// Vref for upper pages
	case 2:
	case 8:
	case 14:
		fa = NAND_FA_UPR_READ_RETRY;
		shift = idx / 6;
		break;
	// Vref for top pages
	case 5:
	case 10:
	case 12:
		fa = NAND_FA_TOP_READ_RETRY;
		shift = (idx - 1) / 5;
		break;
	case 15:
		fa = NAND_FA_TOP_READ_RETRY;
		shift = 3;
		break;
#endif
	}
#if QLC_SUPPORT
	sys_assert(0);// Clock limit to within 400MT/s maybe needed
	//nal_nf_clk_switch(CLK_TYPE_NF, 100);
#endif
	fval = ncl_get_feature(pda, fa, false);
	ncl_console_trace(LOG_ERR, 0x4887, "FA %b %x\n", fa, fval);
	fval &= ~(0xFF << (shift * 8));
	fval |= val << (shift * 8);
	ncl_set_feature(pda, fa, fval);
	fval = ncl_get_feature(pda, fa, false);
	ncl_console_trace(LOG_ERR, 0x8ac0, "FA %b %x\n", fa, fval);
#elif HAVE_MICRON_SUPPORT || (HAVE_UNIC_SUPPORT && !QLC_SUPPORT)
	fval = val;
	switch(idx) {
	case 0:
		fa = MU_SF_SLC_READ_OFFSET;
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		fa = MU_SF_TLC_EXT_L1_READ_OFFSET + idx - 1;
		break;
	}
	ncl_set_feature(pda, fa, fval);
	//fval = ncl_get_feature(pda, fa, false);
	//ncl_console_trace(LOG_ERR, 0, "get %x, %x\n", fa, fval);
#elif HAVE_YMTC_SUPPORT || HAVE_SAMSUNG_SUPPORT
	u32 shift = 0;
	switch(idx) {
	case 0:
		fa = NAND_FA_SLC_READ_RETRY;
		break;
	case 1:
	case 5:
		fa = NAND_FA_LOW_READ_RETRY;
		shift = (idx == 1) ? 0 : 1;
		break;
	case 3:
	case 7:
		fa = NAND_FA_UPR_READ_RETRY;
#if HAVE_YMTC_SUPPORT
		shift = (idx == 3) ? 0 : 1;
#endif
#if HAVE_SAMSUNG_SUPPORT
		shift = (idx == 3) ? 2 : 3;
#endif
		break;
	case 2:
	case 4:
	case 6:
		fa = NAND_FA_MID_READ_RETRY;
		shift = (idx - 2) >> 1;
		break;
	}
	fval = ncl_get_feature(pda, fa, false);
	ncl_console_trace(LOG_ERR, 0x63ab, "FA %b %x\n", fa, fval);
	fval &= ~(0xFF << (shift * 8));
	fval |= val << (shift * 8);
	ncl_set_feature(pda, fa, fval);
	fval = ncl_get_feature(pda, fa, false);
	ncl_console_trace(LOG_ERR, 0x7cb7, "FA %b %x\n", fa, fval);
#elif HAVE_HYNIX_SUPPORT
	fval = val;
	switch(idx) {
	case 0:
		fa = NAND_FA_SLC_READ_RETRY;
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		fa = NAND_FA_UPR1_READ_RETRY + idx - 1;
		break;
	}
	ncl_set_feature(pda, fa, fval);
#elif HAVE_UNIC_SUPPORT && QLC_SUPPORT
	fval = val;
	// For Intel QLC NAND ID 89h, D4h, 0Ch, 32h, AAh
	if (idx == 0) {
		fa = 0xA4; // MU_SF_SLC_READ_OFFSET;
	} else {
		fa = 0xC1 + idx - 1; // 15 Vrefs for QLC WLs
	}
	ncl_set_feature(pda, fa, fval);
#else
	ncl_console_trace(LOG_ERR, 0x7339, "TODO %d %d\n", fa, fval);
#endif
}

/*!
 * @brief Reset nand VREF
 *
 * @param[in] argc		testing parameters
 * @param[in] argv		testing parameters
 *
 * @return		value(true or false)
 */

static ucmd_code int nand_vref_set(int argc, char *argv[])
{
	pda_t pda;
	u8 idx;
	nf_ddr_data_dly_ctrl_reg0_t reg;
	reg.all = ndcu_readl(NF_DDR_DATA_DLY_CTRL_REG0);
#if HAVE_TSB_SUPPORT
	reg.b.nf_set_feat_mode = 1;
#endif
#if HAVE_MICRON_SUPPORT
	reg.b.nf_set_feat_mode = 0;// 1 has error on Micron, #4797
#endif
	ndcu_writel(reg.all, NF_DDR_DATA_DLY_CTRL_REG0);
	pda = strtol(argv[1], (void *)0, 0);
	if (argc >= 4) {
		idx = (bool)strtol(argv[2], (void *)0, 0);
		u8 val = (bool)strtol(argv[3], (void *)0, 0);
		nand_vref_cfg(pda, idx, val);
	} else if (argc <= 2) {
		ncl_console_trace(LOG_ERR, 0xd2f2, "Reset all vref.\n");
		for (idx = 0; idx < 8; idx++) {
			nand_vref_cfg(pda, idx, 0);
		}
	}
	return 0;
}

static DEFINE_UART_CMD(set_vref, "set_vref",
                       "set_vref pda idx val",
                        "Set nand Vref",
                      1, 3, nand_vref_set);
#endif

#ifdef ERRHANDLE_ECCT
ddr_code int UART_ECCT_Operation(int argc, char *argv[])
{
	if (argc >= 4) 
    {
        stECCT_ipc_t ecct_info;
        memset(&ecct_info, 0, sizeof(stECCT_ipc_t));
        ecct_info.source = ECC_REG_VU; 
        ecct_info.type = strtol(argv[1], (void *)0, 0); 
        ecct_info.lba = strtol(argv[2], (void *)0, 0);
        ecct_info.total_len = strtol(argv[3], (void *)0, 0);

        if(ecct_info.type >= VSC_ECC_rc_reg)
            ecct_info.type = VSC_ECC_dump_table;
        
        //if((ecct_info.type == VSC_ECC_reset) || (ecct_info.type == VSC_ECC_dump_table))
        //{
        //    ecct_info.lba = 0;
        //    ecct_info.total_len = 0;
        //}

        printk("[UART] ECCT type[%d] lba[0x%x] len[%d]", ecct_info.type, ecct_info.lba, ecct_info.total_len);
        
        ECC_Table_Operation(&ecct_info);
	} 
    else 
    {
		printk("Invalid number of argument\n");
	}

	return 0;
}

static DEFINE_UART_CMD(ecct, "ecct",
		"ecct [type] [slba] [len]",
		"type: 0:reg, 1:unreg, 2:reset, else:dump tbl",
		1, 3, UART_ECCT_Operation);
#endif


#if ENA_DEBUG_UART
/*!
 * @brief Transfer a CPDA to PDA
 *
 * @param[in] cpda		CPDA
 *
 * @return value(PDA)
 */
ddr_code int ncl_cpda2pda(int argc, char *argv[])
{
	pda_t cpda;
	pda_t pda;

	extern pda_t cpda_to_pda(pda_t cpda);
	cpda = strtol(argv[1], (void *)0, 0);
	pda = cpda_to_pda(cpda);
	ncl_console_trace(LOG_ERR, 0x7a6f, "CPDA %x -> PDA %x\n", cpda, pda);
	return 0;
}

static DEFINE_UART_CMD(cpda2pda, "cpda2pda",
		"cpda2pda [CPDA]",
		"Convert CPDA to PDA",
		1, 1, ncl_cpda2pda);

/*!
 * @brief Transfer a PDA to CPDA
 *
 * @param[in] pda		PDA
 *
 * @return value(CPDA)
 */
ddr_code int ncl_pda2cpda(int argc, char *argv[])
{
	pda_t cpda;
	pda_t pda;

	extern pda_t pda_to_cpda(pda_t cpda);
	pda = strtol(argv[1], (void *)0, 0);
	cpda = pda_to_cpda(pda);
	ncl_console_trace(LOG_ERR, 0x9d40, "PDA %x -> CPDA %x\n", pda, cpda);
	return 0;
}

static DEFINE_UART_CMD(pda2cpda, "pda2cpda",
		"pda2cpda [PDA]",
		"Convert PDA to CPDA",
		1, 1, ncl_pda2cpda);

// Code in this file will be used as console command in future
ucmd_code void ncl_cmd_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	ncl_cmd->flags |= NCL_CMD_COMPLETED_FLAG;
}

/*!
 * @brief Get NAND block erase busy time(tERS)
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 *
 * @return		difference value of two timer
 */

ddr_code u32 _ncl_get_tERS(pda_t pda, u32 pb_type)
{
	vu32 t1, t2;
	struct ncl_cmd_t ncl_cmd;
	int flags;

	extern bool cq_in_dtcm;
	ficu_ctrl_reg0_t cq_reg;
	if (cq_in_dtcm) {
		cq_reg.all = ficu_readl(FICU_CTRL_REG0);
		cq_reg.b.ficu_fcmd_cq_loc_sel = 0;
		ficu_writel(cq_reg.all, FICU_CTRL_REG0);
	}

	ncl_cmd.op_code = NCL_CMD_OP_ERASE;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	if (pb_type == NAL_PB_TYPE_SLC) {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	}
	ncl_cmd.completion = ncl_cmd_cmpl;
	flags = irq_save();
	ncl_cmd_submit(&ncl_cmd);
	t1 = get_tsc_lo();
	while((ficu_readl(FICU_FCMD_CQ_CNT_REG) & 0xFFFF) == 0);
	t2 = get_tsc_lo();
	irq_restore(flags);
	while(NCL_CMD_PENDING(&ncl_cmd)) {
		ficu_done_wait();
	}
	if (cq_in_dtcm) {
		cq_reg.b.ficu_fcmd_cq_loc_sel = 1;
		ficu_writel(cq_reg.all, FICU_CTRL_REG0);
	}
	return t2 - t1;
}

static ddr_code int ncl_get_tERS(int argc, char *argv[])
{
	u32 pb_type;
	pda_t pda;
	u32 tick;
	u32 time;

	if (argc >= 2)
		pb_type = strtol(argv[1], (void *)0, 0);
	else
		pb_type = NAL_PB_TYPE_SLC;

	if (argc >= 3)
		pda = strtol(argv[2], (void *)0, 0);
	else {
		u32 row = row_assemble(0, 0, 0, 0);
		pda = pda_assemble(3, 0, row, 0);
	}
	ncl_console_trace(LOG_INFO, 0x9d55, "pda 0x%x ch %d, ce %d, row 0x%x", pda, pda2ch(pda), pda2ce(pda), pda2row(pda, pb_type));
	tick = _ncl_get_tERS(pda, pb_type);
	time = tick * 100 / (SYS_CLK/1000);
	ncl_console_trace(LOG_INFO, 0xf9f2, "tERASE %d.%d%dms", time / 100, (time / 10) % 10, time % 10);
	return 0;
}

static DEFINE_UART_CMD(get_tBERS, "get_tBERS",
		"get_tBERS [PB TYPE] [PDA]",
		"get tBERS from specified block and block mode",
		1, 3, ncl_get_tERS);

extern struct finstr_format refresh_fins_templ;

/*!
 * @brief Get NAND page read busy time(tR)
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 *
 * @return		difference value of two timer
 */

ddr_code u32 _ncl_get_tR(pda_t pda, u32 pb_type)
{
	u8 fcmd_id = 0;
	u8 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	struct ncl_cmd_t ncl_cmd;
	int flags;


	extern bool cq_in_dtcm;
	ficu_ctrl_reg0_t cq_reg;
	if (cq_in_dtcm) {
		cq_reg.all = ficu_readl(FICU_CTRL_REG0);
		cq_reg.b.ficu_fcmd_cq_loc_sel = 0;
		ficu_writel(cq_reg.all, FICU_CTRL_REG0);
	}

	extern u32 fcmd_outstanding_cnt;
	fcmd_outstanding_cnt++;

	du_dtag_ptr = ncl_acquire_dtag_ptr();

	ncl_cmd.dtag_ptr = du_dtag_ptr;
	ncl_cmd.completion = NULL;
	ncl_cmd.flags = 0;

	// Configure fda list
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->pda = pda;

	// Configure instruction
	ins = refresh_fins_templ;
	if (pb_type == 1) {
		ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_TLC_READ(pda2pg_idx(pda));
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
	}
	ins.dw0.b.fins = 1;
	fcmd_id = du_dtag_ptr;
	ins.dw0.b.lins = 1;
	ncl_cmd_save(fcmd_id, &ncl_cmd);
	ins.dw1.b.fcmd_id = fcmd_id;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw0.b.mp_num		= 0,
	ficu_fill_finst(ins);
	// Final submit instruction(s)
	flags = irq_save();
	ficu_submit_finstr(1);

	u32 t1, t2;
	t1 = get_tsc_lo();
	while((ficu_readl(FICU_FCMD_CQ_CNT_REG) & 0xFFFF) == 0);
	t2 = get_tsc_lo();
	while(NCL_CMD_PENDING(&ncl_cmd)) {
		ficu_isr();
	}
	irq_restore(flags);
	if (cq_in_dtcm) {
		cq_reg.b.ficu_fcmd_cq_loc_sel = 1;
		ficu_writel(cq_reg.all, FICU_CTRL_REG0);
	}
	return t2 - t1;
}

/*!
 * @brief NCL get nand read tR
 *
 * @param[in] argc		testing parameter
 * @param[in] argv		testing parameter
 *
 * @return		value(true or false)
 */

ddr_code int ncl_get_tR(int argc, char *argv[])
{
	u32 pb_type;
	pda_t pda;
	u32 tick;
	u32 time;

	if (argc >= 2)
		pb_type = strtol(argv[1], (void *)0, 0);
	else
		pb_type = NAL_PB_TYPE_SLC;

	if (argc >= 3)
		pda = strtol(argv[2], (void *)0, 0);
	else {
		u32 row = row_assemble(0, 0, 0, 0);
		pda = pda_assemble(3, 0, row, 0);
	}

	ncl_console_trace(LOG_INFO, 0xf602, "pda %x(%s) ch %d, ce %d, row 0x%x", pda, (u32)((pb_type == NAL_PB_TYPE_SLC) ? "SLC" : "TLC"), pda2ch(pda), pda2ce(pda), pda2row(pda, pb_type));
	tick = _ncl_get_tR(pda, pb_type);
#if FPGA
	time = tick * 10000 / (SYS_CLK/1000);
#else
	extern int cur_cpu_clk_freq;
	time = tick * 10 / cur_cpu_clk_freq;
#endif
	ncl_console_trace(LOG_INFO, 0x001e, "tR %d.%dus", time /10, time % 10);
	return 0;
}

static DEFINE_UART_CMD(get_tR, "get_tR",
		"get_tR [PB TYPE] [PDA]",
		"get tR from specified page and block mode",
		1, 3, ncl_get_tR);

/*!
 * @brief NCL get nand cache read tRCBSY console
 *
 * @param[in] argc		testing parameter
 * @param[in] argv		testing parameter
 *
 * @return		value(true or false)
 */

ddr_code int ncl_get_tRCBSY(int argc, char *argv[])
{
	u32 pb_type;
	pda_t pda;
	u32 tick;
	u32 time;

	if (argc >= 2)
		pb_type = strtol(argv[1], (void *)0, 0);
	else
		pb_type = NAL_PB_TYPE_SLC;

	if (argc >= 3)
		pda = strtol(argv[2], (void *)0, 0);
	else {
		u32 row = row_assemble(0, 0, 0, 0);
		pda = pda_assemble(3, 0, row, 0);
	}

	ncl_console_trace(LOG_INFO, 0x1471, "pda %x(%s) ch %d, ce %d, row 0x%x", pda, (u32)((pb_type == NAL_PB_TYPE_SLC) ? "SLC" : "TLC"), pda2ch(pda), pda2ce(pda), pda2row(pda, pb_type));
	tick = _ncl_get_tR(pda, pb_type);

	nf_rcmd_reg10_t reg_bak;
	reg_bak.all = ndcu_readl(NF_RCMD_REG10);
	ndcu_writel(0x31313131, NF_RCMD_REG10);
	tick = _ncl_get_tR(pda+nand_info.interleave*DU_CNT_PER_PAGE, pb_type);
	time = tick * 10000 / (SYS_CLK/1000);
	ncl_console_trace(LOG_INFO, 0xedad, "tRCBSY %d.%dus", time /10, time % 10);
	ndcu_writel(reg_bak.all, NF_RCMD_REG10);
	return 0;
}

/*!
 * @brief Get NAND page program busy time(tPROG)
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 * @param[in] step			Not used
 *
 * @return		difference value of two timer
 */

// Return timer ticks spent
ddr_code u32 _ncl_get_tPROG(pda_t pda, u32 pb_type, u32 step)
{
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info[DU_CNT_PER_PAGE];
	vu32 t1, t2;
	u32 i;
	bm_pl_t dctag[4];
	dtag_t dtag[4];
	u32 ndcmd_fmt_id;
	int flags;

	extern bool cq_in_dtcm;
	ficu_ctrl_reg0_t cq_reg;
	if (cq_in_dtcm) {
		cq_reg.all = ficu_readl(FICU_CTRL_REG0);
		cq_reg.b.ficu_fcmd_cq_loc_sel = 0;
		ficu_writel(cq_reg.all, FICU_CTRL_REG0);
	}

	// Allocate dtag and meta buffer
	i = dtag_get_bulk(DTAG_T_SRAM, DU_CNT_PER_PAGE, dtag);
	if (i < DU_CNT_PER_PAGE) {
		ncl_console_trace(LOG_ERR, 0x823f, "No enough dtag %d.", i);
		dtag_put_bulk(DTAG_T_SRAM, i, dtag);
		return -1;
	}
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		u32* data;

		data = dtag2mem(dtag[i]);
		memset(data, pda, NAND_DU_SIZE);

		dctag[i].all = 0;
		dctag[i].pl.dtag = dtag[i].b.dtag;
		dctag[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}

	ncl_cmd.completion = NULL;
	ncl_cmd.op_code = NCL_CMD_OP_WRITE;
	if (pb_type == NAL_PB_TYPE_SLC) {
		ndcmd_fmt_id = FINST_NAND_FMT_SLC_PROG;
		ncl_cmd.flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
#if MULTI_PROG_STEPS
		for (i = 0; i < DU_CNT_PER_PAGE; i++) {
			info[i].xlc.cb_step = step;
		}
		if (step == PROG_1ST_STEP) {
			ndcmd_fmt_id = FINST_NAND_FMT_QLC_PROG_1ST(pda2pg_idx(pda));
		} else {
			ndcmd_fmt_id = FINST_NAND_FMT_TLC_PROG(pda2pg_idx(pda));
		}
#else
		ndcmd_fmt_id = FINST_NAND_FMT_TLC_PROG(pda2pg_idx(pda));
#endif
#if HAVE_SAMSUNG_SUPPORT
		sys_assert(0);// TODO
#endif
	}
	// Disable CMD2 in nand command format to send prefix-80h-addr-data, no CMD2 10h
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = info;
	ncl_cmd.user_tag_list = dctag;
	ncl_cmd.op_type = NCL_CMD_PROGRAM_TABLE;
	ncl_cmd.du_format_no = DU_FMT_USER_4K;
	ndcu_writel(ndcmd_fmt_id, NF_NCMD_FMT_PTR);
	nf_ncmd_fmt_reg0_t ndcmd_fmt;
	ndcmd_fmt.all = ndcu_readl(NF_NCMD_FMT_REG0);
	ndcmd_fmt.b.cmd_ext &= ~NAND_FMT_2ND_CMD_IDX_6;
	ndcmd_fmt.b.cmd_ext |= NAND_FMT_2ND_CMD_NO;
	ndcu_writel(ndcmd_fmt.all, NF_NCMD_FMT_REG0);
	extern struct finstr_format prog_fins_templ;
	prog_fins_templ.dw0.b.poll_dis = 1;
	ncl_cmd_submit(&ncl_cmd);
	prog_fins_templ.dw0.b.poll_dis = 0;
	dtag_put_bulk(DTAG_T_SRAM, DU_CNT_PER_PAGE, dtag);

	// Send 85h-addr-10h/1Ah and measure tPROG
	ndcmd_fmt.all = ndcu_readl(NF_NCMD_FMT_REG0);
	ndcmd_fmt.b.cmd_ext &= ~NAND_FMT_1ST_CMD_IDX_6;
	ndcmd_fmt.b.cmd_ext |= ncmd_fmt_array[FINST_NAND_FMT_SLC_RANDOM_PROG].fmt.b.cmd_ext & NAND_FMT_1ST_CMD_IDX_6;
	//ndcmd_fmt.b.cmd_ext |= NAND_FMT_2ND_CMD_IDX_0;
	ndcmd_fmt.b.cmd_ext &= ~NAND_FMT_2ND_CMD_IDX_6;
	ndcmd_fmt.b.cmd_ext |= ncmd_fmt_array[ndcmd_fmt_id].fmt.b.cmd_ext & NAND_FMT_2ND_CMD_IDX_6;
	ndcmd_fmt.b.prefix_cmd1 = NAND_FMT_PREFIX_NO;
	ndcmd_fmt.b.prefix_cmd2 = NAND_FMT_PREFIX_NO;
	ndcmd_fmt.b.prefix_cmd3 = NAND_FMT_PREFIX_NO;
	ndcu_writel(ndcmd_fmt.all, NF_NCMD_FMT_REG0);

	u8 fcmd_id;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;

	extern u32 fcmd_outstanding_cnt;
	fcmd_outstanding_cnt++;
	fcmd_id = ncl_acquire_dtag_ptr();
	// Configure fda list
	fda_dtag = ficu_get_addr_dtag_ptr(fcmd_id);
	fda_dtag->pda = pda;
	// Configure instruction
	extern struct finstr_format prog_fins_templ;
	ins = prog_fins_templ;
	ins.dw0.b.fins = 1;
	ins.dw0.b.lins = 1;
	ins.dw0.b.ndcmd_fmt_sel = ndcmd_fmt_id;
	if (pb_type == NAL_PB_TYPE_XLC) {
		ins.dw0.b.xlc_en = ROW_WL_ADDR;
	}
	ins.dw1.b.fcmd_id = fcmd_id;
	ins.dw1.b.du_dtag_ptr = fcmd_id;
	ins.dw1.b.xfcnt_sel = FINST_XFER_ZERO;
	ins.dw2.b.du_num = 0;
	ficu_fill_finst(ins);
	ncl_cmd.flags = 0;
	ncl_cmd_save(fcmd_id, &ncl_cmd);
	flags = irq_save();
	ficu_submit_finstr(1);
	t1 = get_tsc_lo();
	while((ficu_readl(FICU_FCMD_CQ_CNT_REG) & 0xFFFF) == 0);
	t2 = get_tsc_lo();
	irq_restore(flags);
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	if (cq_in_dtcm) {
		cq_reg.b.ficu_fcmd_cq_loc_sel = 1;
		ficu_writel(cq_reg.all, FICU_CTRL_REG0);
	}
	// Restore changed registers
	ndcu_writel(ndcmd_fmt_id, NF_NCMD_FMT_PTR);
	ndcu_writel(ncmd_fmt_array[ndcmd_fmt_id].fmt.all, NF_NCMD_FMT_REG0);
	return t2 - t1;
}

static ddr_code int ncl_get_tPROG(int argc, char *argv[])
{
	u32 pb_type;
	pda_t pda;
	u32 tick;
	u32 time;

	if (argc >= 2)
		pb_type = strtol(argv[1], (void *)0, 0);
	else
		pb_type = NAL_PB_TYPE_SLC;

	if (argc >= 3)
		pda = strtol(argv[2], (void *)0, 0);
	else {
		u32 row = row_assemble(0, 0, 0, 0);
		pda = pda_assemble(3, 0, row, 0);
	}

	ncl_console_trace(LOG_INFO, 0x3e85, "pda %x ch %d, ce %d, row 0x%x", pda, pda2ch(pda), pda2ce(pda), pda2row(pda, pb_type));
	// Set WCNT to minimum, otherwise 1st polled status may ready, time measure wrong
	nf_wcnt_reg3_t wcnt, wcnt_bak;
	wcnt.all = wcnt_bak.all = ndcu_readl(NF_WCNT_REG3);
	wcnt.b.nf_wcnt71 = 0;
	wcnt.b.nf_wcnt72 = 0;
	wcnt.b.nf_div7 = 0;
	ndcu_writel(wcnt.all, NF_WCNT_REG3);
	tick = _ncl_get_tPROG(pda, pb_type, 0);
	ndcu_writel(wcnt_bak.all, NF_WCNT_REG3);
#if FPGA
	time = tick * 10000 / (SYS_CLK/1000);
#else
	extern int cur_cpu_clk_freq;
	time = tick * 10 / cur_cpu_clk_freq;
#endif
	ncl_console_trace(LOG_INFO, 0x2c6e, "tPROG %d.%dus", time /10, time % 10);
	return 0;
}

static DEFINE_UART_CMD(get_tPROG, "get_tPROG",
		"get_tPROG [PB TYPE] [PDA]",
		"get tPROG from specified page and block mode",
		1, 3, ncl_get_tPROG);

extern bool erase_pda(pda_t pda, nal_pb_type_t pb_type);
extern bool write_pda_page(pda_t pda, nal_pb_type_t pb_type, u32 step);
extern bool read_pda_du(pda_t pda, nal_pb_type_t pb_type, u32 check_level, u8 du_fmt, bool fail_retry);

ddr_code void test_nand_pda(pda_t pda, u32 pb_type)
{
	u32 wl;
	u32 pg;
	u32 du_no;
	bool ret = true;
	pda_t default_pda;

	pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);
	default_pda = pda;

	if (pb_type == NAL_PB_TYPE_SLC) {
		ncl_console_trace(LOG_ERR, 0x7523, "SLC type:\n");
		ncl_console_trace(LOG_ERR, 0xc92c, "========================================\n");
		pda = default_pda;
		ret &= erase_pda(pda, pb_type);
		if(!ret) {
			ncl_console_trace(LOG_ERR, 0xe2fd, "erase fail\n");
		} else {
			ncl_console_trace(LOG_ERR, 0x7fab, "erase pass\n");
		}

		for (pg = 0; pg < nand_page_num_slc(); pg++) {
			pda = default_pda;
			pda = pda + (pg << nand_info.pda_page_shift);
			ret &= write_pda_page(pda, pb_type, 0);
			if(!ret) {
				ncl_console_trace(LOG_ERR, 0xa776, "write page %d fail\n", pg);
			}
		}
		if(ret) {
			ncl_console_trace(LOG_ERR, 0x8783, "write block pass\n");
		}

		for (pg = 0; pg < nand_page_num_slc(); pg++) {
			for (du_no = 0; du_no < DU_CNT_PER_PAGE; du_no++) {
				pda = default_pda;
				pda = pda + (pg << nand_info.pda_page_shift);
				pda = pda + du_no;
				ret &= read_pda_du(pda, pb_type, true, DU_FMT_USER_4K, false);
				if(!ret) {
					ncl_console_trace(LOG_ERR, 0x3c10, "read page %d du %d fail\n", pg, du_no);
				}
			}
		}
		if(ret) {
			ncl_console_trace(LOG_ERR, 0x6cbb, "read block pass\n");
		}
	}
	else
	{
		ncl_console_trace(LOG_ERR, 0x166c, "TLC type:\n");
		ncl_console_trace(LOG_ERR, 0x62ef, "========================================\n");
		pda = default_pda;
		ret &= erase_pda(pda, pb_type);
		if(!ret) {
			ncl_console_trace(LOG_ERR, 0xf6c0, "erase fail\n");
		} else {
			ncl_console_trace(LOG_ERR, 0x68ff, "erase pass\n");
		}

		for (wl = 0; wl < nand_page_num_slc(); wl++) {
			for (pg = 0; pg < XLC; pg++) {
				pda = default_pda;
				pda = pda + (((wl * XLC) + pg) << nand_info.pda_page_shift);
				ret &= write_pda_page(pda, pb_type, 0);
				if(!ret) {
					ncl_console_trace(LOG_ERR, 0xe2b1, "write wl %d fail\n", wl);
				}
			}
		}
		if(ret) {
			ncl_console_trace(LOG_ERR, 0x859a, "write block pass\n");
		}

		for (pg = 0; pg < nand_page_num(); pg++) {
			for (du_no = 0; du_no < DU_CNT_PER_PAGE; du_no++) {
				pda = default_pda;
				pda = pda + (pg << nand_info.pda_page_shift);
				pda = pda + du_no;
				ret &= read_pda_du(pda, pb_type, true, DU_FMT_USER_4K, false);
				if(!ret) {
					ncl_console_trace(LOG_ERR, 0x77c4, "read page %d du %d fail\n", pg, du_no);
				}
			}
		}
		if(ret) {
			ncl_console_trace(LOG_ERR, 0xcbfa, "read block pass\n");
		}
	}
}

ddr_code int ncl_test_nand_pda(int argc, char *argv[])
{
	u32 pb_type;
	pda_t pda;

	if (argc >= 2)
		pb_type = strtol(argv[1], (void *)0, 0);
	else
		pb_type = NAL_PB_TYPE_SLC;

	if (argc >= 3)
		pda = strtol(argv[2], (void *)0, 0);
	else {
		pda = (20 << nand_pda_block_shift());
	}

	ncl_console_trace(LOG_INFO, 0x314e, "pda %x(%s)", pda, (u32)((pb_type == NAL_PB_TYPE_SLC) ? "SLC" : "TLC"));
	test_nand_pda(pda, pb_type);

	return 0;
}

static DEFINE_UART_CMD(test_nand_pda, "test_nand_pda",
		"test_nand pda [PB TYPE] [PDA]",
		"test nand pda erase/write/read status",
		1, 3, ncl_test_nand_pda);

ddr_code void dump_performance(u32 iops, u32 speed)
{
	ncl_console_trace(LOG_ERR, 0xa124, "IOPS ");
	if (iops >= 1000) {
		ncl_console_trace(LOG_ERR, 0x10dd, "%d,%d%d%d", iops/1000,(iops / 100) % 10, (iops / 10) % 10, iops % 10);
	} else {
		ncl_console_trace(LOG_ERR, 0x1e8b, "%d", iops);
	}
	ncl_console_trace(LOG_ERR, 0x037b, ", thru ");
	if (speed >= 1000) {
		ncl_console_trace(LOG_ERR, 0x0c3a, "%d,%d%d%d", speed/1000,(speed / 100) % 10, (speed / 10) % 10, speed % 10);
	} else {
		ncl_console_trace(LOG_ERR, 0x03de, "%d", speed);
	}
	ncl_console_trace(LOG_ERR, 0x0d1b, " MB/s\n");

}
#if 0
ucmd_code void get_rand_read_performance(u32 tR, u32 nf_clk)
{
	//u32 tR = 300;// In unit of 0.1us
	//u32 nf_clk = 533;
	u32 du_size;
	u32 tXFR;// Transfer time of 4KB DU in unit of 0.1us
	u32 lun_per_ch;
	u32 iops;
	u32 throughput;

	ncl_console_trace(LOG_ERR, 0x8628, "\nRAND read:\n");
	lun_per_ch = nand_target_num() * nand_lun_num();
	du_size = get_encoded_du_size();
	tXFR = (du_size * 100) / nf_clk;
	if ((tXFR % 10) >= 5) {
		tXFR = (tXFR / 10) + 1;// Round up
	} else {
		tXFR = tXFR / 10;// Round down
	}
	if (tR < (lun_per_ch - 1) * tXFR) {// tR fast, always has data to transfer
		iops = (1000000 * 10) / tXFR;
		ncl_console_trace(LOG_ERR, 0xaa92, "tR <");
	} else {
		iops = (1000000 * 10) * lun_per_ch / (tR + tXFR);
		ncl_console_trace(LOG_ERR, 0xaa94, "tR >");
	}
	ncl_console_trace(LOG_ERR, 0x77ac, " tXFR %d.%dus * (%d - 1)\n", tXFR / 10, tXFR % 10, lun_per_ch);
	iops *= nand_channel_num();
	throughput = iops * 4 / 1024;
	dump_performance(iops, throughput);
}

ucmd_code void get_seq_read_performance(u32 tR, u32 nf_clk)
{
	//u32 tR = 300;// In unit of 0.1us
	//u32 nf_clk = 533;
	u32 du_size;
	u32 tXFR;// Transfer time of 4KB DU in unit of 0.1us
	u32 lun_per_ch;
	u32 iops;
	u32 throughput;

	ncl_console_trace(LOG_ERR, 0x6a99, "\nSEQ read:\n");
	lun_per_ch = nand_target_num() * nand_lun_num();
	du_size = get_encoded_du_size();
	tXFR = (du_size * DU_CNT_PER_PAGE * nand_plane_num() * 100) / nf_clk;
	if ((tXFR % 10) >= 5) {
		tXFR = (tXFR / 10) + 1;// Round up
	} else {
		tXFR = tXFR / 10;// Round down
	}
	ncl_console_trace(LOG_ERR, 0xdd32, "tXFR %d.%dus", tXFR / 10, tXFR % 10);
	if (tR < (lun_per_ch - 1) * tXFR) {// tR fast, always has data to transfer
		iops = (1000000 * 10) / tXFR;
		ncl_console_trace(LOG_ERR, 0xbdd0, "\n");
	} else {
		if (lun_per_ch != 1) {
			ncl_console_trace(LOG_ERR, 0x9a15, ", nand slow, really?\n");
		}
		iops = (1000000 * 10) * lun_per_ch / (tR + tXFR);
	}
	iops *= nand_channel_num();
	throughput = iops * 4 * DU_CNT_PER_PAGE * nand_plane_num() / 1024;
	dump_performance(iops, throughput);
}

ucmd_code void get_seq_write_performance(u32 tPROG, u32 nf_clk)
{
	//u32 tR = 300;// In unit of 0.1us
	//u32 nf_clk = 533;
	u32 du_size;
	u32 tXFR;// Transfer time of 4KB DU in unit of 0.1us
	u32 lun_per_ch;
	u32 iops;
	u32 throughput;

	ncl_console_trace(LOG_ERR, 0x9c2a, "\nSEQ write:\n");
	lun_per_ch = nand_target_num() * nand_lun_num();
	du_size = get_encoded_du_size();
	tXFR = (du_size * DU_CNT_PER_PAGE * nand_plane_num() * 100) / nf_clk;
	if ((tXFR % 10) >= 5) {
		tXFR = (tXFR / 10) + 1;// Round up
	} else {
		tXFR = tXFR / 10;// Round down
	}
	if (tPROG < (lun_per_ch - 1) * tXFR) {// tR fast, always has data to transfer
		iops = (1000000 * 10) / tXFR;
		ncl_console_trace(LOG_ERR, 0x7140, "tPROG <");
	} else {
		iops = (1000000 * 10) * lun_per_ch / (tPROG + tXFR);
		ncl_console_trace(LOG_ERR, 0x7142, "tPROG >");
	}
	ncl_console_trace(LOG_ERR, 0x152f, " tXFR %d.%dus * (%d - 1)\n", tXFR / 10, tXFR % 10, lun_per_ch);
	iops *= nand_channel_num();
	throughput = iops * 4 * DU_CNT_PER_PAGE * nand_plane_num() / 1024;
	dump_performance(iops, throughput);
}
#endif
/*!
 * @brief dump NCB register
 *
 * @return		Not used
 */

ddr_code void dump_ncb_registers(void)
{
	u32 reg;

	ncl_console_trace(LOG_INFO, 0x1ee5, "ndphy reg dump");
	for (reg = 0; reg < sizeof(ndphy_regs_t); reg+=sizeof(u32)) {
		if ((reg & 0xF) == 0x0) {
			ncl_console_trace(LOG_ERR, 0x9d70, "%x:", reg + NDPHY_REG_ADDR(0));
		}
		ncl_console_trace(LOG_ERR, 0xb1ba, "%x ", ndphy_readl(0, reg));
		if ((reg & 0xF) == 0xC) {
			ncl_console_trace(LOG_ERR, 0xa0b7, "\n");
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_console_trace(LOG_ERR, 0x4de1, "\n");
	}

	ncl_console_trace(LOG_INFO, 0xf72f, "ndcu reg dump");
	for (reg = 0; reg < sizeof(ncb_ndcu_regs_t); reg+=sizeof(u32)) {
		if ((reg & 0xF) == 0x0) {
			ncl_console_trace(LOG_ERR, 0x70e2, "%x:", reg + NDCU_REG_ADDR);
		}
		ncl_console_trace(LOG_ERR, 0x4664, "%x ", ndcu_readl(reg));
		if ((reg & 0xF) == 0xC) {
			ncl_console_trace(LOG_ERR, 0xf4a1, "\n");
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_console_trace(LOG_ERR, 0xe963, "\n");
	}

	ncl_console_trace(LOG_INFO, 0x1313, "eccu reg dump");
	for (reg = 0; reg < sizeof(ncb_eccu_regs_t); reg+=sizeof(u32)) {
		if ((reg & 0xF) == 0x0) {
			ncl_console_trace(LOG_ERR, 0x46ff, "%x:", reg + ECCU_REG_ADDR);
		}
		ncl_console_trace(LOG_ERR, 0x4270, "%x ", eccu_readl(reg));
		if ((reg & 0xF) == 0xC) {
			ncl_console_trace(LOG_ERR, 0xcabe, "\n");
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_console_trace(LOG_ERR, 0xf310, "\n");
	}

	ncl_console_trace(LOG_INFO, 0x7bcb, "eccu dec top reg dump");
	for (reg = 0; reg < sizeof(dec_top_ctrl_regs_t); reg+=sizeof(u32)) {
		if ((reg & 0xF) == 0x0) {
			ncl_console_trace(LOG_ERR, 0xe910, "%x:", reg + DEC_TOP_REG_ADDR);
		}
		ncl_console_trace(LOG_ERR, 0x7cad, "%x ", dec_top_readl(reg));
		if ((reg & 0xF) == 0xC) {
			ncl_console_trace(LOG_ERR, 0x6cdc, "\n");
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_console_trace(LOG_ERR, 0x286a, "\n");
	}

	ncl_console_trace(LOG_INFO, 0x5c7b, "eccu mdec reg dump");
	for (reg = 0; reg < sizeof(eccu_mdec_regs_t); reg+=sizeof(u32)) {
		if ((reg & 0xF) == 0x0) {
			ncl_console_trace(LOG_ERR, 0x7c12, "%x:", reg + MDEC_REG_ADDR);
		}
		ncl_console_trace(LOG_ERR, 0x5e81, "%x ", mdec_readl(reg));
		if ((reg & 0xF) == 0xC) {
			ncl_console_trace(LOG_ERR, 0xd90f, "\n");
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_console_trace(LOG_ERR, 0xb7a5, "\n");
	}

	ncl_console_trace(LOG_INFO, 0x537f, "eccu pdec reg dump");
	for (reg = 0; reg < sizeof(eccu_pdec_regs_t); reg+=sizeof(u32)) {
		if ((reg & 0xF) == 0x0) {
			ncl_console_trace(LOG_ERR, 0xfc84, "%x:", reg + PDEC_REG_ADDR);
		}
		ncl_console_trace(LOG_ERR, 0x4481, "%x ", pdec_readl(reg));
		if ((reg & 0xF) == 0xC) {
			ncl_console_trace(LOG_ERR, 0x7252, "\n");
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_console_trace(LOG_ERR, 0x6c15, "\n");
	}

	ncl_console_trace(LOG_INFO, 0x7d40, "ficu reg dump");
	for (reg = 0; reg < sizeof(ncb_ficu_regs_t); reg+=sizeof(u32)) {
		if ((reg & 0xF) == 0x0) {
			ncl_console_trace(LOG_ERR, 0x778f, "%x:", reg + FICU_REG_ADDR);
		}
		ncl_console_trace(LOG_ERR, 0xb632, "%x ", ficu_readl(reg));
		if ((reg & 0xF) == 0xC) {
			ncl_console_trace(LOG_ERR, 0x1fda, "\n");
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_console_trace(LOG_ERR, 0xa724, "\n");
	}
}

/*!
 * @brief dump memory data
 *
 * @param[in] ptr		point to a memory addr
 * @param[in] size		dump memory size
 *
 * @return		Not used
 */

ucmd_code void dump_mem_data(u32* ptr, u32 size)
{
	u32 i;
	ncl_console_trace(LOG_ERR, 0x07ca, "ptr %x\n", ptr);
	for (i = 0; i < size/sizeof(u32); i++) {
		if ((i & 0x7) == 0) {
			ncl_console_trace(LOG_ERR, 0x3ddf, "0x%x:", i*4);
		}
		ncl_console_trace(LOG_ERR, 0xb73b, " 0x%x,", ptr[i]);
		if ((i & 0x7) == 7) {
			ncl_console_trace(LOG_ERR, 0x5109, "\n");
		}
	}
	if ((i & 0x7) != 0) {
		ncl_console_trace(LOG_ERR, 0xad79, "\n");
	}
}

ddr_code void get_theory_performance(void)
{
	u32 nf_clk = 533;
	u32 tR;// In unit of 0.1us
	u32 tPROG;// In unit of 0.1us
	u32 tERASE;// In unit of 0.1us

	tR = 300;
	tPROG = 2000;
	tERASE = 50000;
	tPROG += tERASE / (nand_page_num() / nand_bits_per_cell());
	ncl_console_trace(LOG_ERR, 0x8533, "\n\nSLC: %d CH * %d DIE, %d MT/s, tR %d.%dus, tPROG %dus, tERASE %dus\n", nand_channel_num(), nand_target_num() * nand_lun_num(), nf_clk, tR/10, tR%10, tPROG/10, tERASE/10);
	get_rand_read_performance(tR, nf_clk);
	get_seq_read_performance(tR, nf_clk);
	get_seq_write_performance(tPROG + tERASE / nand_page_num(), nf_clk);

	tR = 500;
	tPROG = 5000;
	tERASE = 50000;
	tPROG += tERASE / nand_page_num();
	ncl_console_trace(LOG_ERR, 0x97c4, "\n\nTLC: %d CH * %d DIE, %d MT/s, tR %d.%dus, tPROG %dus, tERASE %dus\n", nand_channel_num(), nand_target_num() * nand_lun_num(), nf_clk, tR/10, tR%10, tPROG/10, tERASE/10);
	get_rand_read_performance(tR, nf_clk);
	get_seq_read_performance(tR, nf_clk);
	get_seq_write_performance(tPROG + tERASE / nand_page_num(), nf_clk);
}

ddr_code void ncl_console(void)
{
	get_theory_performance();
}
#endif

// Jmaie 20200928 random generator
vu32 cur_seed;
void mysrand(int new_seed)
{
    //time = get_tsc_lo() + get_tsc_hi();
    cur_seed = new_seed;
}
u32 myrand()
{
    cur_seed = (cur_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return (cur_seed / 65536) & 0x7FFF;
}

u32 myrand32()
{
    u8 i;
    u32 rand_val = 0;
    for (i = 0; i < 4; i++) {
        cur_seed = (cur_seed * 1103515245 + 12345) & 0x7FFFFFFF;
        rand_val = (rand_val << 8);
        rand_val |= (cur_seed / 65536) & 0xFF;
    }
    return rand_val;
}

/*!
 * @brief Erase one PDA
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 *
 * @return		result (true or false)
 */

ucmd_code bool erase_pda(pda_t pda, nal_pb_type_t pb_type)
{
	bool ret = true;
	struct info_param_t info;
	struct ncl_cmd_t ncl_cmd;

	memset(&info, 0, sizeof(struct info_param_t));
	ncl_cmd.op_code = NCL_CMD_OP_ERASE;
	ncl_cmd.completion = NULL;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = &info;
	ncl_cmd.status = 0;
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd_submit(&ncl_cmd);
	if (ncl_cmd.status & NCL_CMD_ERROR_STATUS) {
		ret = false;
	}
	return ret;
}
u32 pda2blk(pda_t pda);

/*!
 * @brief NCL block erase console
 *
 * @param[in] argc		testing parameter
 * @param[in] argc		testing parameter
 *
 * @return		value(true or false)
 */

ddr_code int ncl_block_erase_console(int argc, char *argv[])
{
	if (argc >= 2) {
		pda_t pda = strtol(argv[1], (void *)0, 0);
		enum nal_pb_type pb_type = NAL_PB_TYPE_SLC;

		if (argc >= 3)
			pb_type = (enum nal_pb_type)strtol(argv[2], (void *)0, 10);

		ncl_console_trace(LOG_ERR, 0xabb1, "\nErase PDA %x, type %d, blk %d:\n", pda, pb_type, pda2blk(pda));

		erase_pda(pda, pb_type);
	}

	return 0;
}

ddr_code int ncl_depda(int argc, char *argv[])		//20200706-Eddie
{
	// u32 ch, ce, lun, plane, block, page ;
	// pda_t pda;
	// pda= strtol(argv[1], (void *)0, 0);

    //    ch=pda2ch(pda);
	// ce=pda2ce(pda);
	// lun=pda2lun(pda);
	// block=pda2blk(pda);
	// page=pda2page(pda);
	// plane = (pda >> nand_info.pda_plane_shift) & (nand_info.geo.nr_planes - 1);

	//  ncl_console_trace(LOG_ERR, 0, "\nblkshift=%d, chshift=%d, pageshift=%d, ceshift=%d, lunshift=%d, planeshift=%d, dushift=%d\n",nand_info.pda_block_shift,nand_info.pda_ch_shift,nand_info.pda_page_shift,nand_info.pda_ce_shift,nand_info.pda_lun_shift,nand_info.pda_plane_shift,nand_info.pda_du_shift);

	// ncl_console_trace(LOG_ERR, 0, "\n PDA %x----->ch=%d, ce=%d, lun=%d, blk=%d, page=%d, plane=%d \n",  pda,ch,ce,lun,block,page,plane );

	return 0;
}

static DEFINE_UART_CMD(depda, "depda",
		"[GENPDA]",
		"gen PDA",
		1, 2, ncl_depda);
/*! \endcond */
/*!
 * @brief GEN PDA
 *
 * @param[in] GENPDA
 *
 * @return value(PDA)
 */
ddr_code int ncl_genpda(int argc, char *argv[])  // 20210224 Jamie ucmd_code -> ddr_code
{
	u32 ch, ce, lun, block, plane, page = 0;
	pda_t pda=0;
	ch = ( u32) strtol(argv[1], (void *)0, 0);
	ce = ( u32) strtol(argv[2], (void *)0, 0);
	lun = ( u32) strtol(argv[3], (void *)0, 0);
	plane = ( u32) strtol(argv[4], (void *)0, 0);
	block = ( u32) strtol(argv[5], (void *)0, 0);
	//page =( u32) strtol(argv[6], (void *)0, 0);
	//du = (u32 )strtol(argv[7], (void *)0, 0);
       pda = block << nand_info.pda_block_shift;
	pda |= page << nand_info.pda_page_shift;
	pda |= ch << nand_info.pda_ch_shift;
	pda |= ce << nand_info.pda_ce_shift;
	pda |= lun << nand_info.pda_lun_shift;
	pda |= plane << nand_info.pda_plane_shift;
	//pda |= du << nand_info.pda_du_shift;
	ncl_console_trace(LOG_ERR, 0x274a, " PDA %x\n",  pda);
	return 0;
}

static DEFINE_UART_CMD(genpda, "genpda",
		"[GENPDA]",
		"gen PDA",
		1, 5, ncl_genpda);

ucmd_code void spb_erase(u32 spb_id, u32 pb_type)
{
	int i;
	struct ncl_cmd_t _ncl_cmd;
	struct ncl_cmd_t *ncl_cmd = &_ncl_cmd;
	pda_t *pda;
	u32 width = nal_get_interleave();
	struct info_param_t *info;

	ncl_console_trace(LOG_ERR, 0x26b9, "spb_erase : %d \n", spb_id);
	pda = sys_malloc(FAST_DATA, sizeof(pda_t) * width);
	sys_assert(pda);
	info = sys_malloc(FAST_DATA, sizeof(*info) * width);
	sys_assert(info);
	memset(info, 0, sizeof(*info) * width);

	for (i = 0; i < width; i++) {
		pda[i] = nal_make_pda(spb_id, i * DU_CNT_PER_PAGE);
		info[i].pb_type = pb_type;
	}

	ncl_cmd->completion = NULL;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
	ncl_cmd->addr_param.common_param.list_len = width;
	ncl_cmd->addr_param.common_param.pda_list = pda;
	ncl_cmd->addr_param.common_param.info_list = info;
	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;

	ncl_cmd_submit(ncl_cmd);

	sys_free(FAST_DATA, pda);
	sys_free(FAST_DATA, info);
}

ddr_code int ncl_pberase(int argc, char *argv[])		//20200706-Eddie
{
	u32 spb, spb_total1;

	enum nal_pb_type pb_type = NAL_PB_TYPE_SLC;

	spb_total1= strtol(argv[1], (void *)0, 0);

	spb_total1++;

	for ( spb = 0; spb < spb_total1; spb++) {
			spb_erase(spb, pb_type);

		}
	return 0;
}
#if EX_URAT_ENABLE
ddr_code int spb_erase_main(int argc, char *argv[])		//erase one blk
{
	u32 spb;

	enum nal_pb_type pb_type = NAL_PB_TYPE_SLC;

	spb= strtol(argv[1], (void *)0, 0);

	spb_erase(spb, pb_type);

	return 0;
}
#endif

#if defined(UART_SRB_ERASE)
ddr_code int erase_allsrb(int argc, char *argv[]) // 20210224 Jamie ucmd_code -> ddr_code
{
	u8 ch, ce, lun, pln;
	pda_t pda=0;
	int retval = 0;
	u8 mode;
	u8 ch_s, ce_s, lun_s, pln_s;
	u8 ch_e, ce_e, lun_e, pln_e;

	ncl_console_trace(LOG_ERR, 0x8434, "\n");

	mode = strtol(argv[1], (void *)0, 0);

	if (mode == 0){
		ch_s = 0;
		ce_s = 0;
		lun_s = 0;
		pln_s = 0;
		ch_e =  nal_get_channels();
		ce_e =  nal_get_targets();
		lun_e =  nal_lun_count_per_dev();
		pln_e = nand_plane_num();
		}
	else if (mode == 1){	//Only Clear SRB info & FW
		ch_s = 0;
		ce_s = 0;
		lun_s = 0;
		pln_s = 0;
		ch_e =  4;
		ce_e =  nal_get_targets();
		lun_e =  nal_lun_count_per_dev();
		pln_e =  nand_plane_num();
		}
	else if (mode == 2){	//Only Clear SRB info
		ch_s = 0;
		ce_s = 0;
		lun_s = 0;
		pln_s = 0;
		ch_e =  4;
		ce_e =  nal_get_targets();
		lun_e =  1;
		pln_e =  nand_plane_num();
		}
	else{
		ncl_console_trace(LOG_ERR, 0x5f9a, "Please enter 0~2 \n");
		return 0;
		}

	// ncl_console_trace(LOG_ERR, 0, "ch %d~%d, ce %d~%d, lun %d~%d, pl %d~%d, \n",ch_s,ch_e-1,ce_s,ce_e-1,lun_s,lun_e-1,pln_s,pln_e-1);
	ncl_enter_mr_mode();
	for (ch = ch_s; ch < ch_e; ch++) {		//Only ch 0 ~ 3
		for (ce = ce_s; ce < ce_e; ce++) {
			if ((ch == 4)&&((ce == 0) ||(ce == 1))){
				ncl_console_trace(LOG_ERR, 0xcd0a, "Bypass CH4 CE 0&1 \n");
				continue;
			}
			for (lun = lun_s; lun < lun_e; lun++) {	//20200921-Eddie
				for (pln = pln_s; pln < pln_e; pln++) {

				       pda = 0 << nand_info.pda_block_shift;
					pda |= 0 << nand_info.pda_page_shift;
					pda |= ch << nand_info.pda_ch_shift;
					pda |= ce << nand_info.pda_ce_shift;
					pda |= lun << nand_info.pda_lun_shift;
					pda |= pln << nand_info.pda_plane_shift;
					//ncl_console_trace(LOG_ERR, 0, "SRB Erased ch: %d, ce: %d, lun: %d, pl: %d \n",ch,ce,lun,pln);

					retval = erase_pda(pda, NAL_PB_TYPE_SLC);

					}
				}
			}
		}
	ncl_leave_mr_mode();

	ncl_console_trace(LOG_ERR, 0x3bfb, "ALLSRB Erased : %d \n",retval);
	return 0;
}

static DEFINE_UART_CMD(srberase, "srberase",
		"[SRBERASE]",
		"All SRB erase",
		1, 2, erase_allsrb);
#endif

#if defined(CLEAR_DDRTRAIN_DONE)		//20200922-Eddie
//extern ddr_info_t* ddr_info_buf_in_ddr;
share_data volatile ddr_info_t* ddr_info_buf_in_ddr;
share_data fw_config_set_t* fw_config_main_in_ddr;
extern fw_config_set_t *fw_config_main;
ucmd_code int clear_ddrtrn_done(int argc, char *argv[])
{
	//memprint("ddr_info_buf",ddr_info_buf_in_ddr,320);
	ddr_info_buf_in_ddr->cfg.training_done = 0;
	ddr_info_buf_in_ddr->cfg.bkup_fwconfig_done = 0;
	//memprint("ddr_info_buf",ddr_info_buf_in_ddr,320);
	//memprint("fw_config_main_in_ddr",fw_config_main_in_ddr,4096);
	memcpy(fw_config_main_in_ddr->board.ddr_info, (void*) ddr_info_buf_in_ddr, sizeof(ddr_info_t));

	//memprint("main save",fw_config_main,320);
	FW_CONFIG_Rebuild(fw_config_main_in_ddr);

	ncl_console_trace(LOG_ERR, 0x6b38, "DDR traning done cleared !! \n");
	return 0;
}

static DEFINE_UART_CMD(clrddrdone, "clrddrdone",
		"[CLRDDRDONE]",
		"Clear DDR training done",
		0, 0, clear_ddrtrn_done);
#endif

static DEFINE_UART_CMD(spberase, "pberase",
		"[PBERASE]",
		"PB erase",
		1, 5, ncl_pberase);
#if EX_URAT_ENABLE
static DEFINE_UART_CMD(erasespb, "erasespb",
		"erase_idx",
		"PB erase",
		1, 1, spb_erase_main);
#endif

static DEFINE_UART_CMD(pda_erase, "pda_erase",
		"pda_erase [PDA] [PB type]",
		"Erase blocks to a 16 hex pda, type:0 - SLC(Default), 1 - XLC",
		1, 2, ncl_block_erase_console);

/*!
 * @brief Write one PDA page
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 * @param[in] step			program step
 *
 * @return		result (true or false)
 */

ucmd_code bool write_pda_page(pda_t pda, nal_pb_type_t pb_type, u32 step)
{
	bool ret = true;
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info;
	bm_pl_t* bm_pl;
	dtag_t* dtag_list;
	u32 dtag_cnt;

	// Allocate dtag list
	dtag_list = sys_malloc(SLOW_DATA, sizeof(dtag_t) * DU_CNT_PER_PAGE);
	if (dtag_list == NULL) {
		return false;
	}

	bm_pl = sys_malloc(SLOW_DATA, sizeof(bm_pl_t) * DU_CNT_PER_PAGE);
	if (bm_pl == NULL) {
		sys_free(SLOW_DATA, dtag_list);
		return false;
	}
	// Allocate DTAGs
	dtag_cnt = dtag_get_bulk(DTAG_T_SRAM, DU_CNT_PER_PAGE, dtag_list);
	if (DU_CNT_PER_PAGE != dtag_cnt) {
		sys_free(SLOW_DATA, bm_pl);
		sys_free(SLOW_DATA, dtag_list);
		dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
		return false;
	}

	// Prepare data & meta as PDA value
	u32 du, i;
	u32* data;
	struct meta_entry* meta_entry;
	meta_entry = (struct meta_entry*)(ficu_readl(FICU_META_IDX_MAP_SRAM_BASE_ADDR_REG) << 4);

	for (du = 0; du < DU_CNT_PER_PAGE; du++) {
		data = dtag2mem(dtag_list[du]);
		for (i = 0; i < NAND_DU_SIZE/sizeof(u32);i++) {
			data[i] = pda + du;
		}
		for (i = 0; i < META_SIZE/sizeof(u32); i++) {
			meta_entry[du].meta[i] = pda + du;
		}
		bm_pl[du].all = 0;
		bm_pl[du].pl.dtag = dtag_list[du].b.dtag;
		bm_pl[du].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_IDX;
		bm_pl[du].pl.nvm_cmd_id = du;
	}

	memset(&info, 0, sizeof(struct info_param_t));
	ncl_cmd.op_code = NCL_CMD_OP_WRITE;
	ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
	ncl_cmd.status = 0;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = &info;
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
		info.xlc.cb_step = step;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd.du_format_no = DU_FMT_USER_4K;
	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.completion = NULL;

	ncl_cmd_submit(&ncl_cmd);

	dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
	sys_free(SLOW_DATA, bm_pl);
	sys_free(SLOW_DATA, dtag_list);

	if (ncl_cmd.status & NCL_CMD_ERROR_STATUS) {
		ret = false;
	}

	return ret;
}

/*!
 * @brief NCL page write console
 *
 * @param[in] argc		testing parameter
 * @param[in] argc		testing parameter
 *
 * @return		value(true or false)
 */

static ddr_code int ncl_page_write_console(int argc, char *argv[])
{
	if (argc >= 2) {
		pda_t pda = strtol(argv[1], (void *)0, 0);
		enum nal_pb_type pb_type = NAL_PB_TYPE_SLC;
		u32 step = 0;

		if (argc >= 3)
			pb_type = (enum nal_pb_type)strtol(argv[2], (void *)0, 10);

		if (argc >= 4)
			step = (enum nal_pb_type)strtol(argv[3], (void *)0, 10);

		ncl_console_trace(LOG_ERR, 0x3b33, "\nWrite PDA %x, type %d\n", pda, pb_type);

		write_pda_page(pda, pb_type, step);
	}

	return 0;
}

static DEFINE_UART_CMD(pda_write, "pda_write",
		"pda_write [PDA] [PB TYPE]",
		"write page with a 16 hex pda",
		1, 2, ncl_page_write_console);
slow_code u8 get_meta_sz(u8 du_fmt)
{

    u8 meta_sz;
    eccu_du_fmt_sel_reg_t fmt_sel;

    fmt_sel.all = eccu_readl(ECCU_DU_FMT_SEL_REG);
    fmt_sel.b.du_fmt_cfg_idx = du_fmt;
    eccu_writel(fmt_sel.all, ECCU_DU_FMT_SEL_REG);

    eccu_du_fmt_reg0_t du_fmt_reg0;
    du_fmt_reg0.all = eccu_readl(ECCU_DU_FMT_REG0);

    meta_sz = du_fmt_reg0.b.meta_sz;

    return meta_sz;
}

// _JAMIE_
ddr_code u16 read_ecc(pda_t pda, nal_pb_type_t pb_type, u8 du_fmt, u8 dec_mode)
{
    struct ncl_cmd_t ncl_cmd;
    bm_pl_t bm_pl;
    struct info_param_t info;
    u16 err_cnt;
    struct meta_entry* entry;

    // select dec mode
    dec_top_ctrs_reg0_t dtop_ctr, dtop_ctr_bak;
    dtop_ctr_bak.all = dtop_ctr.all = dec_top_readl(DEC_TOP_CTRS_REG0);
    dtop_ctr.b.eccu_otf_dec_mode = dec_mode; // Should enable PDEC if need.
    dec_top_writel(dtop_ctr.all, DEC_TOP_CTRS_REG0);

    entry = (struct meta_entry*)(ficu_readl(FICU_META_IDX_MAP_SRAM_BASE_ADDR_REG) << 4);

    // enable ecc register
    mdec_ctrs_reg1_t mdec_err_reg;
    mdec_ctrs_reg4_t mdec_ctrs_reg;
    mdec_ctrs_reg.all = mdec_readl(MDEC_CTRS_REG4);
    mdec_ctrs_reg.b.eccu_mdec_err_cnt_en = 1;
    mdec_writel(mdec_ctrs_reg.all, MDEC_CTRS_REG4);
    pdec_ctrs_reg1_t pdec_err_reg;
    pdec_ctrs_reg3_t pdec_ctrs_reg;
    pdec_ctrs_reg.all = pdec_readl(PDEC_CTRS_REG3);
    pdec_ctrs_reg.b.eccu_pdec_err_cnt_en = 1;
    pdec_writel(pdec_ctrs_reg.all, PDEC_CTRS_REG3);

    u32 *mem = NULL;
    dtag_t dtag;
    dtag = dtag_get(DTAG_T_SRAM, (void *)&mem);
    if (mem == NULL) {
        ncl_console_trace(LOG_WARNING, 0x5f6f, "No dtag");
        return 0xFFFF;
    }

    memset(&info, 0, sizeof(struct info_param_t));
    info.pb_type = pb_type;
    ncl_cmd.op_code = NCL_CMD_OP_READ;
    ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
    ncl_cmd.status = 0;
    ncl_cmd.addr_param.common_param.list_len = 1;
    ncl_cmd.addr_param.common_param.pda_list = &pda;
    ncl_cmd.addr_param.common_param.info_list = &info;
    ncl_cmd.flags = (pb_type == NAL_PB_TYPE_SLC) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG;
    if (entry == NULL) {
        ncl_cmd.flags |= NCL_CMD_META_DISCARD;
    }
    ncl_cmd.flags |= (NCL_CMD_DIS_ARD_FLAG | NCL_CMD_NO_READ_RETRY_FLAG); //_GENE_20201015
    ncl_cmd.du_format_no = du_fmt;
    ncl_cmd.user_tag_list = &bm_pl;
    ncl_cmd.completion = NULL;
    bm_pl.all = 0;
    bm_pl.pl.dtag = dtag.b.dtag;
    bm_pl.pl.type_ctrl = DTAG_QID_DROP | META_SRAM_IDX;
    bm_pl.pl.nvm_cmd_id = 0;
    ncl_cmd_submit(&ncl_cmd);

    if (((ncl_cmd.status & NCL_CMD_ERROR_STATUS) == 0) || (info.status == ficu_err_par_err)) {
        u32 cnt_0to1, cnt_1to0;
        if (dtop_ctr.b.eccu_otf_dec_mode == DEC_MODE_PDEC) {
            pdec_err_reg.all = pdec_readl(PDEC_CTRS_REG1);
            cnt_0to1 = pdec_err_reg.b.eccu_pdec_err_cnt0to1;
            cnt_1to0 = pdec_err_reg.b.eccu_pdec_err_cnt1to0;
        } else {
            mdec_err_reg.all = mdec_readl(MDEC_CTRS_REG1);
            cnt_0to1 = mdec_err_reg.b.eccu_mdec_err_cnt0to1;
            cnt_1to0 = mdec_err_reg.b.eccu_mdec_err_cnt1to0;
        }
        err_cnt = cnt_0to1 + cnt_1to0;
    } else {
        ncl_console_trace(LOG_INFO, 0x594e, "pda_read err %d", info.status);
        if (info.status == ficu_err_du_erased)
            err_cnt = 0xEEEE;
        else
            err_cnt = 0xFFFF;
    }
    dec_top_writel(dtop_ctr_bak.all, DEC_TOP_CTRS_REG0);
    dtag_put(DTAG_T_SRAM, dtag);
    return err_cnt;
}
ddr_code void record_data_spi(u8 plp_flag, u32 data)
{
    u32 i;
    if(plp_flag)
    {
        if(spi_nor_write(0x140000, SPI_record_btyecnt) != 0)
        {
            printk("SPI-Write Fail!!!\n");
        }
    }
    else
    {
        if(SPI_record_btyecnt==0)
        {
            for(i=0;i<2;i++)
            {
        		if(spi_nor_erase(0x140000+i*SPI_BLOCK_SIZE) != 0){
        			printk("SPI-Erase Fail!!!\n");
        		}
            }
        }
    	if(spi_nor_write(0x140004+SPI_record_btyecnt, data) != 0)
        {
    	    printk("SPI-Write Fail!!!\n");
        }
        SPI_record_btyecnt+=4;
    }
}
ddr_code void read_data_spi()
{
//    u32 bytecnt,i;
//    bytecnt = spi_nor_read(0x140000);
//    printk("bytecnt = %d\n",bytecnt);
//    if(bytecnt<=0)
//    {
//        printk("No record data!!\n");
//    }
//    else if
//    {
//        for(i=0;i<bytecnt;i+=4)
//        {
//            printk("%x\n", spi_nor_read(0x140004+i));
//        }
//    }
    u32 i;
    for(i=0;i<256;i+=4)
    {
        evlog_printk(LOG_ALW, "%x", spi_nor_read(0x140004+i));
    }
}

ddr_code int get_spilog(int argc, char *argv[]) 
{
	spi_savelog_info_t spi_savelog_info;
//    u8 data[4];
//    u32* data2;
	u8 mode;
	u32 byte_count, byte_count_cur=0;
	u32 spi_addr;
    u32 data;
//	data2=(u32*)data;
	mode = strtol(argv[1], (void *)0, 0);
    data = strtol(argv[2], (void *)0, 0);
	if(mode==0){
		printk(" SPI-NOR ID:%x\n", spi_read_id());
		spi_savelog_info.all = spi_nor_read(0x0);
		if(spi_savelog_info.b.signature == 0x535049){	
			spi_addr = 20 ;
			for(u32 McpIdx=0; McpIdx < MPC; McpIdx++){			
				byte_count = spi_nor_read(4+(McpIdx*4));
				byte_count = byte_count - byte_count_cur;
				if(byte_count == 0){
					continue;
				}
				printk("\nCPU[%x] ByteCnt[%x]:", McpIdx, byte_count);

				for(u32 IdxAddr=0; IdxAddr < byte_count; IdxAddr+=4){//u32byte_cnt[3]
//                    *data2 = spi_nor_read(spi_addr+IdxAddr);
//					printk("%b %b %b %b ", data[0],data[1],data[2],data[3]);
					printk("%x", spi_nor_read(spi_addr+IdxAddr));
					if(IdxAddr%16==0){ printk("\n");}//1024
				}
				byte_count_cur = byte_count;
				spi_addr = spi_addr + byte_count;
			}
		}
		else{
			printk("SPI-NOR is Empty...\n");
		}
		ncl_console_trace(LOG_ERR, 0x7ad3, "Get Spi Log done.\n",0);
	}
	else if(mode==1){
		panic("Test for SPI save log.");
	}
    else if(mode==2){
    u32 Norsize=0;
    u32 sign;
    u32 test=0x535049;
    u8* result;
    result = (u8*)&test;
    Norsize = spi_nor_read(0x10);
    sign = spi_nor_read(0x0);
    printk("Norsize = 0x%x\n, sign = 0x%x\n",Norsize,sign);
    printk("result[0] =0x%x,result[1] =0x%x,result[2] =0x%x,result[3] =0x%x\n",result[0],result[1],result[2],result[3]);
    }
    else if(mode==3){
        record_data_spi(0,data);
    }
    else if(mode==4){
        record_data_spi(1,data);
    }
    else if(mode==5){
        read_data_spi();
    }
	return 0;
}
static DEFINE_UART_CMD(getspilog, "get_spilog", "[GETSPILOG]", "Get SpiLog", 1, 3, get_spilog);


//#if CPU_ID == 4 ////_GENE_20201015
#if 1 // Jamie

static ddr_code int ecc_scan_console(int argc, char *argv[])
{
    dtag_t* dtag_list;
    u32 dtag_cnt;
    u8  dtag_required;

    u16 total_du = 0;
    pda_t pda    = strtol(argv[1], (void *)0, 0);
    bool xlc_mode = strtol(argv[2], (void *)0, 0);
    u8 du_fmt = DU_FMT_USER_4K;
    
    if (argc >= 4)
        du_fmt = strtol(argv[3], (void *)0, 10);

    u16 iPage, iDU;
    u16 end_page;
    u16* ecc_save;


    if (xlc_mode) {
        total_du = nand_page_num() * DU_CNT_PER_PAGE;
        end_page = nand_page_num();
    } else {
        total_du = nand_page_num_slc() * DU_CNT_PER_PAGE;
        end_page = nand_page_num_slc();
    }

    dtag_required = (total_du * 2 + 4095) / 4096;

    dtag_list = sys_malloc(SLOW_DATA, sizeof(dtag_t) * dtag_required);
    if (dtag_list == NULL) {
        ncl_console_trace(LOG_ERR, 0x1d2b, "sys_malloc fail\n");
        return false;
    }
    // Allocate DTAGs
    dtag_cnt = dtag_get_bulk(DTAG_T_SRAM, dtag_required, dtag_list);
    if (dtag_required != dtag_cnt) {
        ncl_console_trace(LOG_ERR, 0x7c17, "dtag_cnt %d dtag_required %d fail", dtag_cnt, dtag_required);
        sys_free(SLOW_DATA, dtag_list);
        dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
        return false;
    }
    // memset
    for (iDU = 0; iDU < dtag_required; iDU++) {
        ecc_save = dtag2mem(dtag_list[iDU]);
        //ncl_console_trace(LOG_ERR, 0, "dtag_list[%d] = 0x%x\n", iDU, ecc_save);
        memset(ecc_save, 0xFF, 4096);
    }



    rda_t rda;
    nal_pda_to_rda(pda, xlc_mode, &rda);
    u32 blk, /*pg, */lun, pl;

    lun = pda2lun(pda);
    pl = pda2plane(pda);
    blk = pda2blk(pda);
    // pg = pda2page(pda);

    // ncl_console_trace(LOG_ERR, 0, "\n");
    ncl_console_trace(LOG_ERR, 0xbd01, "pda = 0x%x ; mode = %d", pda, xlc_mode);
    // ncl_console_trace(LOG_ERR, 0, "ch %d ce %d lun %d pl %d blk %d pg %d du %d\n", rda.ch, rda.dev, lun, pl, blk, pg, rda.du_off);
    ncl_console_trace(LOG_ERR, 0xb82e, "ecc_scan start\n");

    u32 start_row;
    start_row = row_assemble(lun, pl, blk, 0);

    pda_t start_pda, cur_pda;
    start_pda = pda_assemble_xlc(rda.ch, rda.dev, start_row, 0, 1);
    ncl_console_trace(LOG_ERR, 0xdf31, "start_pda = 0x%x", start_pda);
    u16 ecc_idx = 0;
    ficu_mode_ard_control(0);

    for (iPage = 0; iPage < end_page; iPage++) {
        for (iDU = 0; iDU < DU_CNT_PER_PAGE; iDU++) {
            cur_pda = start_pda + (iPage << nand_info.pda_page_shift) + (iDU << nand_info.pda_du_shift);
            ecc_save = dtag2mem(dtag_list[ecc_idx/2048]);
            ecc_save[ecc_idx%2048] = read_ecc(cur_pda, xlc_mode, du_fmt, DEC_MODE_PDEC);

            ecc_idx++;
        }
    }
    ficu_mode_ard_control(2);
    ncl_console_trace(LOG_ERR, 0x8ead, "ecc_scan end");

    ncl_console_trace(LOG_ERR, 0xc6fd, "ecc dump start");
    ecc_idx = 0;

    u16 min_ecc, max_ecc, uncr_cnt;
    min_ecc = 0xFFFF;
    max_ecc = 0;
    uncr_cnt = 0;

    ncl_console_trace(LOG_ERR, 0xa746, "MIN : %w ; MAX : %w ; UNCR : %d", min_ecc, max_ecc, uncr_cnt);
	printk("\n");
    for (iPage = 0; iPage < end_page; iPage++) {
        cur_pda = start_pda + (iPage << nand_info.pda_page_shift);
        if (iPage % 3 == 0)
        {
            //ncl_console_trace(LOG_ERR, 0, "%x : ", cur_pda);
            printk("%x : ", cur_pda);
        }

        for (iDU = 0; iDU < DU_CNT_PER_PAGE; iDU++) {
            ecc_save = dtag2mem(dtag_list[ecc_idx/2048]);
            if (ecc_save[ecc_idx%2048] == 0xFFFF) {
                //ncl_console_trace(LOG_ERR, 0, "UNCR ");
                printk("UNCR ");
                uncr_cnt++;
            } else if (ecc_save[ecc_idx%2048] == 0xEEEE) {
                //ncl_console_trace(LOG_ERR, 0, "ERSE ");
                printk("ERSE ");
            } else {
                //ncl_console_trace(LOG_ERR, 0, "%w ", ecc_save[ecc_idx%2048]);
                printk("%w ", ecc_save[ecc_idx%2048]);

                if (min_ecc > ecc_save[ecc_idx%2048]) {
                    min_ecc = ecc_save[ecc_idx%2048];
                }

                if (max_ecc < ecc_save[ecc_idx%2048]) {
                    max_ecc = ecc_save[ecc_idx%2048];
                }
            }

            ecc_idx++;
        }
        //ncl_console_trace(LOG_ERR, 0, "| ");
        printk("| ");
        if (iPage % 3 == 2)
        {
            //ncl_console_trace(LOG_ERR, 0, "\n");
            printk("\n");
        }
    }

    ncl_console_trace(LOG_ERR, 0x9566, "MIN : %w ; MAX : %w ; UNCR : %d", min_ecc, max_ecc, uncr_cnt);

    dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
    sys_free(SLOW_DATA, dtag_list);
    return 0;
}

static DEFINE_UART_CMD(ecc_scan, "ecc_scan",
        "ecc_scan [PDA] [0-SLC/1-TLC] [DU FMT]",
        "ecc_scan [PDA] [0-SLC/1-TLC] [DU FMT]",
        2, 3, ecc_scan_console);

ddr_code bool write_pda_page_enh(pda_t pda, nal_pb_type_t pb_type, u8 du_fmt, u8 data_type, u32 fix_pattern)
{
    bool ret = true;
    struct ncl_cmd_t ncl_cmd;
    struct info_param_t info;
    bm_pl_t* bm_pl;
    dtag_t* dtag_list;
    u32 dtag_cnt;

    // Allocate dtag list
    dtag_list = sys_malloc(SLOW_DATA, sizeof(dtag_t) * DU_CNT_PER_PAGE);
    if (dtag_list == NULL) {
        return false;
    }

    bm_pl = sys_malloc(SLOW_DATA, sizeof(bm_pl_t) * DU_CNT_PER_PAGE);
    if (bm_pl == NULL) {
        sys_free(SLOW_DATA, dtag_list);
        return false;
    }
    // Allocate DTAGs
    dtag_cnt = dtag_get_bulk(DTAG_T_SRAM, DU_CNT_PER_PAGE, dtag_list);
    if (DU_CNT_PER_PAGE != dtag_cnt) {
        sys_free(SLOW_DATA, bm_pl);
        sys_free(SLOW_DATA, dtag_list);
        dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
        return false;
    }

    // Prepare data & meta as PDA value
    u32 du, i;
    u32* data;
    struct meta_entry* meta_entry;
    meta_entry = (struct meta_entry*)(ficu_readl(FICU_META_IDX_MAP_SRAM_BASE_ADDR_REG) << 4);

    // data_type 0 : pda / 1 : fix_pattern / 2 : 32B random / 3 : full random
    u32 write_pattern = 0;
    mysrand(get_tsc_lo() + get_tsc_hi());
    // ncl_console_trace(LOG_ERR, 0, "\n");
    switch (data_type) {
        case 0:
            ncl_console_trace(LOG_ERR, 0xae56, "PDA\n");
            break;
        case 1:
            write_pattern = fix_pattern;
            ncl_console_trace(LOG_ERR, 0x09f0, "Fix Pattern = 0x%x\n", fix_pattern);
            break;
        case 2:
            write_pattern = myrand32();
            ncl_console_trace(LOG_ERR, 0x1ec2, "32B Random Pattern = 0x%x\n", write_pattern);
            break;
        case 3:
            // ncl_console_trace(LOG_ERR, 0, "\n");
            ncl_console_trace(LOG_ERR, 0xc5ef, "Full Random\n");
            break;
    }

    u8 meta_sz = get_meta_sz(du_fmt);

    for (du = 0; du < DU_CNT_PER_PAGE; du++) {
        data = dtag2mem(dtag_list[du]);
        if (data_type == 0)
            write_pattern = pda + du;
        for (i = 0; i < NAND_DU_SIZE/sizeof(u32);i++) {
            if (data_type != 3)
                data[i] = write_pattern;
            else {
                data[i] = myrand32();
            }
        }
        for (i = 0; i < meta_sz/sizeof(u32); i++) {
            if (data_type != 3)
                meta_entry[du].meta[i] = write_pattern;
            else
                meta_entry[du].meta[i] = myrand32();
        }
        bm_pl[du].all = 0;
        bm_pl[du].pl.dtag = dtag_list[du].b.dtag;
        bm_pl[du].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_IDX;
        bm_pl[du].pl.nvm_cmd_id = du;
    }

    memset(&info, 0, sizeof(struct info_param_t));
    ncl_cmd.op_code = NCL_CMD_OP_WRITE;
    ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
    ncl_cmd.status = 0;
    ncl_cmd.addr_param.common_param.list_len = 1;
    ncl_cmd.addr_param.common_param.pda_list = &pda;
    ncl_cmd.addr_param.common_param.info_list = &info;
    if (pb_type == NAL_PB_TYPE_XLC) {
        ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
    } else {
        ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
    }
    ncl_cmd.du_format_no = du_fmt;
    ncl_cmd.user_tag_list = bm_pl;
    ncl_cmd.completion = NULL;

    ncl_cmd_submit(&ncl_cmd);

    dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
    sys_free(SLOW_DATA, bm_pl);
    sys_free(SLOW_DATA, dtag_list);

    if (ncl_cmd.status & NCL_CMD_ERROR_STATUS) {
        ret = false;
    }

    return ret;
}

static ddr_code int enh_write_console(int argc, char *argv[])
{
    if (argc >= 2) {
        pda_t pda = strtol(argv[1], (void *)0, 0);
        enum nal_pb_type pb_type = NAL_PB_TYPE_SLC;
        u8 du_fmt = DU_FMT_USER_4K;
        u8 data_type = 0;
        u32 fix_pattern = 0;

        if (argc >= 3)
            pb_type = (enum nal_pb_type)strtol(argv[2], (void *)0, 10);

        if (argc >= 4)
            du_fmt = strtol(argv[3], (void *)0, 10);

        if (argc >= 5)
            data_type = strtol(argv[4], (void *)0, 10);

        if (argc >= 6)
            fix_pattern = strtol(argv[5], (void *)0, 0);

        ncl_console_trace(LOG_ERR, 0xd86f, "\nWrite PDA %x, type %d\n", pda, pb_type);
        if (fix_pattern != 0) {
            ncl_console_trace(LOG_ERR, 0x8454, "Fix Pattern = 0x%x\n", fix_pattern);
        }

        write_pda_page_enh(pda, pb_type, du_fmt, data_type, fix_pattern);
    }

    return 0;
}

static DEFINE_UART_CMD(enh_write, "enh_write",
        "enh_write [PDA] [PB TYPE] [DU FMT] [DATA TYPE] [fix_pattern]",
        "data_type 0 : pda / 1 : fix_pattern / 2 : 32B random / 3 : full random",
        1, 5, enh_write_console);

static ddr_code int blk_prog_console(int argc, char *argv[])
{
    pda_t pda    = strtol(argv[1], (void *)0, 0);
    bool xlc_mode = strtol(argv[2], (void *)0, 0);

    u8 du_fmt = DU_FMT_USER_4K;
    u8 data_type = 0;
    u32 fix_pattern = 0;

    if (argc >= 4)
        du_fmt = strtol(argv[3], (void *)0, 10);

    if (argc >= 5)
        data_type = strtol(argv[4], (void *)0, 10);

    if (argc >= 6)
        fix_pattern = strtol(argv[5], (void *)0, 0);


    u16 iPage;
    u16 end_page;

    if (xlc_mode) {
        end_page = nand_page_num();
    } else {
        end_page = nand_page_num_slc();
    }

    if (argc >=7)
        end_page = strtol(argv[6], (void *)0, 0);

    rda_t rda;
    nal_pda_to_rda(pda, xlc_mode, &rda);
    u32 blk, /*pg, */lun, pl;

    lun = pda2lun(pda);
    pl = pda2plane(pda);
    blk = pda2blk(pda);
    // pg = pda2page(pda);

    //ncl_console_trace(LOG_ERR, 0, "\n");
    ncl_console_trace(LOG_ERR, 0x6e9e, "pda = 0x%x ; mode = %d\n", pda, xlc_mode);
// ncl_console_trace(LOG_ERR, 0, "ch %d ce %d lun %d pl %d blk %d pg %d du %d\n", rda.ch, rda.dev, lun, pl, blk, pg, rda.du_off);
    ncl_console_trace(LOG_ERR, 0x5062, "blk_prog start\n");

    u32 start_row;
    start_row = row_assemble(lun, pl, blk, 0);

    pda_t start_pda, cur_pda;
    start_pda = pda_assemble_xlc(rda.ch, rda.dev, start_row, 0, 1);
    ncl_console_trace(LOG_ERR, 0x3620, "start_pda = 0x%x\n", start_pda);

    for (iPage = 0; iPage < end_page; iPage++) {
        cur_pda = start_pda + (iPage << nand_info.pda_page_shift);
        write_pda_page_enh(cur_pda, xlc_mode, du_fmt, data_type, fix_pattern);
    }
    ncl_console_trace(LOG_ERR, 0x8909, "blk_prog end\n");
    return 0;
}

static DEFINE_UART_CMD(blk_prog, "blk_prog",
        "blk_prog [PDA] [PB TYPE] [DU FMT] [DATA TYPE] [fix_pattern] [endpage]",
        "data_type 0 : pda / 1 : fix_pattern / 2 : 32B random / 3 : full random",
        2, 6, blk_prog_console);

#endif
#if EX_URAT_ENABLE
static ddr_code int pda_prog_main(int argc, char *argv[])
{
    pda_t pda    = strtol(argv[1], (void *)0, 0);
    bool xlc_mode = strtol(argv[2], (void *)0, 0);

    u8 du_fmt = DU_FMT_USER_4K;
    u8 data_type = 0;
    u32 fix_pattern = 0;

    if (argc >= 4)
        du_fmt = strtol(argv[3], (void *)0, 10);

    if (argc >= 5)
        data_type = strtol(argv[4], (void *)0, 10);

    if (argc >= 6)
        fix_pattern = strtol(argv[5], (void *)0, 0);


    u16 iPage;
    u16 end_page;

    if (xlc_mode) {
        end_page = 1;
    } else {
        end_page = 1;
    }

    if (argc >=7)
        end_page = strtol(argv[6], (void *)0, 0);

    ncl_console_trace(LOG_ERR, 0xa32e, "\n");
    ncl_console_trace(LOG_ERR, 0xc57a, "pda = 0x%x ; page = %d ; wl = %d ; die = %d ; mode = %d", 
    							pda,pda2page(pda),pda2page(pda)/(xlc_mode?3:1) ,pda2die(pda),xlc_mode);
// ncl_console_trace(LOG_ERR, 0, "ch %d ce %d lun %d pl %d blk %d pg %d du %d\n", rda.ch, rda.dev, lun, pl, blk, pg, rda.du_off);
    ncl_console_trace(LOG_ERR, 0xd406, "blk_prog start");



    pda_t cur_pda;


    for (iPage = 0; iPage < end_page; iPage++) {
        cur_pda = pda + (iPage << nand_info.pda_page_shift);
        write_pda_page_enh(cur_pda, xlc_mode, du_fmt, data_type, fix_pattern);
    }
    ncl_console_trace(LOG_ERR, 0xcf31, "blk_prog end\n");
    return 0;
}

static DEFINE_UART_CMD(pda_prog, "pda_prog",
        "blk_prog [PDA] [PB TYPE] [DU FMT] [DATA TYPE] [fix_pattern] [pageNum]",
        "data_type 0 : pda / 1 : fix_pattern / 2 : 32B random / 3 : full random",
        2, 6, pda_prog_main);
#endif

#if defined(HMETA_SIZE)
static ddr_code int cmf_switch(int argc, char *argv[]) {
    u8 idx = strtol(argv[1], (void *)0, 0);
    // Switch dufmt first!!!
    eccu_dufmt_switch(idx);
    eccu_switch_cmf(idx);
    return 0;
}

static DEFINE_UART_CMD(cmf_switch, "cmf_switch",
        "cmf switch 0:MR / 1:61_512 / 2:61_4K / 3:62 / 4:291",
        "cmf switch 0:MR / 1:61_512 / 2:61_4K / 3:62 / 4:291",
        1, 1, cmf_switch);
#endif


/*!
 * @brief Read one PDA DU
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 * @param[in] check_level	print message or not
 * @param[in] du_fmt		DU format no
 *
 * @return		result (true or false)
 */

// Read a DU
ucmd_code bool read_pda_du(pda_t pda, nal_pb_type_t pb_type, u32 check_level, u8 du_fmt, bool fail_retry)
{
	struct ncl_cmd_t ncl_cmd;
	bm_pl_t bm_pl;
	struct info_param_t info;
	u32 i;
	bool ret = true;
	struct meta_entry* entry;

#if defined(HMETA_SIZE)
	eccu_du_fmt_reg0_t reg0;
	eccu_du_fmt_reg1_t reg1;
	u32 * pi_buffer;
#endif

	dec_top_ctrs_reg0_t dtop_ctr, dtop_ctr_bak;
	dtop_ctr_bak.all = dtop_ctr.all = dec_top_readl(DEC_TOP_CTRS_REG0);
	if (eccu_cfg.b.pdec) {
		dtop_ctr.b.eccu_otf_dec_mode = DEC_MODE_PDEC;
	} else {
		dtop_ctr.b.eccu_otf_dec_mode = DEC_MODE_MDEC;
	}
	dec_top_writel(dtop_ctr.all, DEC_TOP_CTRS_REG0);

	entry = (struct meta_entry*)(ficu_readl(FICU_META_IDX_MAP_SRAM_BASE_ADDR_REG) << 4);

	if (!check_level) {
		ncl_console_trace(LOG_INFO, 0x1a0e, "Read %s PDA 0x%x", (u32)((pb_type == NAL_PB_TYPE_SLC) ? "SLC" : "TLC"), pda);
	}
	mdec_ctrs_reg1_t mdec_err_reg;
	mdec_ctrs_reg4_t mdec_ctrs_reg;
	mdec_ctrs_reg.all = mdec_readl(MDEC_CTRS_REG4);
	mdec_ctrs_reg.b.eccu_mdec_err_cnt_en = 1;
	mdec_writel(mdec_ctrs_reg.all, MDEC_CTRS_REG4);
	pdec_ctrs_reg1_t pdec_err_reg;
	pdec_ctrs_reg3_t pdec_ctrs_reg;
	pdec_ctrs_reg.all = pdec_readl(PDEC_CTRS_REG3);
	pdec_ctrs_reg.b.eccu_pdec_err_cnt_en = 1;
	pdec_writel(pdec_ctrs_reg.all, PDEC_CTRS_REG3);

	u32 *mem = NULL;
	dtag_t dtag;
	dtag = dtag_get(DTAG_T_SRAM, (void *)&mem);
	if (mem == NULL) {
		ncl_console_trace(LOG_WARNING, 0x9b21, "No dtag");
		return false;
	}
	memset(&info, 0, sizeof(struct info_param_t));
	info.pb_type = pb_type;
	ncl_cmd.op_code = NCL_CMD_OP_READ;
	ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
	ncl_cmd.status = 0;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = &info;
	ncl_cmd.flags = (pb_type == NAL_PB_TYPE_SLC) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG;
	if (entry == NULL) {
		ncl_cmd.flags |= NCL_CMD_META_DISCARD;
	}
	if (!fail_retry) {
		ncl_cmd.flags |= NCL_CMD_NO_READ_RETRY_FLAG;
	}
	ncl_cmd.du_format_no = du_fmt;
	ncl_cmd.user_tag_list = &bm_pl;
	ncl_cmd.completion = NULL;
	bm_pl.all = 0;
	bm_pl.pl.dtag = dtag.b.dtag;
	bm_pl.pl.type_ctrl = DTAG_QID_DROP | META_SRAM_IDX;
	bm_pl.pl.nvm_cmd_id = 0;
	ncl_cmd_submit(&ncl_cmd);

	if (((ncl_cmd.status & NCL_CMD_ERROR_STATUS) == 0) || (info.status == ficu_err_par_err)) {
		u32 cnt_0to1, cnt_1to0;
		if (eccu_cfg.b.pdec) {
			pdec_err_reg.all = pdec_readl(PDEC_CTRS_REG1);
			cnt_0to1 = pdec_err_reg.b.eccu_pdec_err_cnt0to1;
			cnt_1to0 = pdec_err_reg.b.eccu_pdec_err_cnt1to0;
		} else {
			mdec_err_reg.all = mdec_readl(MDEC_CTRS_REG1);
			cnt_0to1 = mdec_err_reg.b.eccu_mdec_err_cnt0to1;
			cnt_1to0 = mdec_err_reg.b.eccu_mdec_err_cnt1to0;
		}
		if (check_level > 1) {
			if ((cnt_1to0 + cnt_0to1) != 0) {
				//ret = false;
			}
		}
		if (!check_level) {
			ncl_console_trace(LOG_ERR, 0x5fce, "err bits %d + %d = %d", cnt_1to0, cnt_0to1, cnt_1to0 + cnt_0to1);
			ncl_console_trace(LOG_INFO, 0xf7d0, "User data:");
		}
		printk("\n");
		for (i = 0; i < NAND_DU_SIZE / sizeof(u32); i++) {
			if (!check_level) {
				if ((i & 3) == 0) {
					//ncl_console_trace(LOG_ERR, 0, "%x:", i << 2);
					printk("%x:", i << 2);
				}
				//ncl_console_trace(LOG_ERR, 0, "%x ", mem[i]);
				printk("%x ", mem[i]);
				if ((i & 3) == 3) {
					//ncl_console_trace(LOG_ERR, 0, "\n");
					printk("\n");
				}
			} else if (mem[i] != pda) {// Currently data pattern as PDA
				ret = false;
				break;
			}
		}

		if (entry != NULL) {
			if (!check_level) {
				ncl_console_trace(LOG_INFO, 0x875c, "meta1 : %x_%x_%x_%x", entry->meta[0], entry->meta[1], entry->meta[2], entry->meta[3]);
				ncl_console_trace(LOG_INFO, 0x9434, "meta2 : %x_%x_%x_%x", entry->meta[4], entry->meta[5], entry->meta[6], entry->meta[7]);
#if defined(HMETA_SIZE)
				eccu_writel((u32)du_fmt, ECCU_DU_FMT_SEL_REG);
				reg0.all = eccu_readl(ECCU_DU_FMT_REG0);
				reg1.all = eccu_readl(ECCU_DU_FMT_REG1);
				pi_buffer = (u32*)((readl((void*)0xC0010200) << 5) | SRAM_BASE);
				pi_buffer = &pi_buffer[dtag.b.dtag * 8 * 2];
				if (reg1.b.pi_sz != 0) {
					for (i = 0; i < reg0.b.du2host_ratio; i++) {
						evlog_printk(LOG_ALW, "pi%d : %x_%x", i, pi_buffer[i*2], pi_buffer[i*2 + 1]);
					}
				}
#endif
			} else {
				// Currently meta pattern as PDA
				for (i = 0; i < META_SIZE/sizeof(u32); i++) {
					if (entry->meta[i] != pda) {
						ret = false;
						break;
					}
				}
			}
		}
	} else {
		ncl_console_trace(LOG_INFO, 0x17bf, "pda_read err %d", info.status);
		ret = false;
	}
	dec_top_writel(dtop_ctr_bak.all, DEC_TOP_CTRS_REG0);
	dtag_put(DTAG_T_SRAM, dtag);
	return ret;
}

/*!
 * @brief NCL PDA read for ERD
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 * @param[in] cnt			test PDA count
 *
 * @return		Not used
 */

ucmd_code void ncl_pda_erd(pda_t pda, nal_pb_type_t pb_type, u32 cnt)
{
	// ERD should be within 1 page
	sys_assert(((pda & (DU_CNT_PER_PAGE - 1)) + cnt) <= DU_CNT_PER_PAGE);
	bm_pl_t bm_pl;
	u32 *mem = NULL;
	dtag_t dtag;
	dtag = dtag_get(DTAG_T_SRAM, (void *)&mem);
	if (mem == NULL) {
		ncl_console_trace(LOG_WARNING, 0xc5c9, "No dtag");
		return;
	}
	bm_pl.all = 0;
	bm_pl.pl.dtag = dtag.b.dtag;
	bm_pl.pl.type_ctrl = DTAG_QID_DROP | META_SRAM_IDX;
	bm_pl.pl.nvm_cmd_id = 0;

	struct ncl_cmd_t erd_cmd;
	memset(&erd_cmd, 0, sizeof(struct ncl_cmd_t));
	erd_cmd.du_format_no = DU_FMT_USER_4K;
	erd_cmd.op_code = NCL_CMD_OP_READ_ERD;
	erd_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
	erd_cmd.flags = NCL_CMD_META_DISCARD;
	erd_cmd.status = 0;
	if (pb_type == NAL_PB_TYPE_SLC) {
		erd_cmd.flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		erd_cmd.flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	}
	erd_cmd.addr_param.rapid_du_param.pda = pda;
	erd_cmd.addr_param.rapid_du_param.list_len = cnt;
	erd_cmd.addr_param.rapid_du_param.info.xlc.slc_idx = 0;
	erd_cmd.addr_param.rapid_du_param.info.status = cur_du_good;
	erd_cmd.addr_param.rapid_du_param.info.pb_type = NAL_PB_TYPE_SLC;
	erd_cmd.user_tag_list = &bm_pl;
	erd_cmd.completion = NULL;

	ncl_cmd_submit(&erd_cmd);

	dtag_put(DTAG_T_SRAM, dtag);
}

/*!
 * @brief NCL PDA read console
 *
 * @param[in] argc		testing parameter
 * @param[in] argv		testing parameter
 *
 * @return		value(true or false)
 */

static ddr_code int ncl_pda_read_console(int argc, char *argv[])
{
	if (argc >= 2) {
		pda_t pda = strtol(argv[1], (void *)0, 0);
		enum nal_pb_type pb_type = NAL_PB_TYPE_SLC;
		u32 pda_count = 1;
		u8 du_fmt = DU_FMT_USER_4K;
		bool from_cache = false;
		if (argc >= 3)
			pb_type = (enum nal_pb_type)strtol(argv[2], (void *)0, 10);

		if (argc >= 4)
			pda_count = strtol(argv[3], (void *)0, 10);

		if (argc >= 5)
			du_fmt = strtol(argv[4], (void *)0, 10);

		if (argc >= 6) {
			from_cache = strtol(argv[5], (void *)0, 10);
		}

		extern struct finstr_format read_fins_templ;
		if (from_cache) {
			// Assume FINST_NAND_FMT_SLC_READ_CMD is identical as FINST_NAND_FMT_READ_DATA except prefix
			read_fins_templ.dw0.b.finst_type = FINST_TYPE_READ_DATA;
			ficu_mode_ard_control(0);
			ncl_console_trace(LOG_ERR, 0x2f4d, "\nRead PDA %x, type %d, count %d from cache\n", pda, pb_type, pda_count);
		} else {
			ncl_console_trace(LOG_ERR, 0x9326, "\nRead PDA %x, type %d, count %d\n", pda, pb_type, pda_count);
		}

		int i;
		for (i = 1; i <= pda_count; ++i) {
			read_pda_du(pda, pb_type, 0, du_fmt, true);
			ncl_console_trace(LOG_ERR, 0x43bd, "\n===========================================================\n");
			pda += 1;
		}

		if (from_cache) {
			ficu_mode_ard_control(2);
			read_fins_templ.dw0.b.finst_type = FINST_TYPE_READ;
		}
	} else {
		ncl_console_trace(LOG_ERR, 0xc316, "\nInvalid number of argument\n");
	}

	return 0;
}

static DEFINE_UART_CMD(pda_read, "pda_read",
		"pda_read [PDA] [PB TYPE] [PDA COUNT] [DU FMT][read cache]",
		"read PDA data from a 16 hex pda, type:0 - SLC(Default), 1 - XLC",
		1, 5, ncl_pda_read_console);

/*!
 * @brief PDA DU raw data dump and count 1 bits
 *
 * @param[in] pda			test PDA
 * @param[in] data		2 dtag memory
 *
 * @return		1 bit count
 */
ucmd_code u32 du_raw_data_dump(pda_t pda, u32** data, bool en_print)
{
	u32 i;
	u32 col_addr;
	u32 bit1_cnt = 0;
	col_addr = pda2column(pda);
	for (i = 0; i < NAND_DU_SIZE / sizeof(u32); i++) {
		u32 tmp = data[0][(((i & (~0x3)) << 1) + 3 - i)];
		if (en_print) {
			if ((i & 3) == 0) {
				ncl_console_trace(LOG_ERR, 0xad07, "%x: ", (i << 2) + col_addr);
			}
			ncl_console_trace(LOG_ERR, 0xd314, "%x ", tmp);
		}
		while (tmp) {
			bit1_cnt++;
			tmp &= tmp - 1;
		}
		if (en_print) {
			if ((i & 3) == 3) {
				ncl_console_trace(LOG_ERR, 0x258b, "\n");
			}
		}
	}

	u32 spare_size;
	spare_size = get_encoded_du_size() - NAND_DU_SIZE;

	for (i = 0; i < (spare_size + 3) / sizeof(u32); i++) {
		if (en_print) {
			if ((i & 3) == 0) {
				ncl_console_trace(LOG_ERR, 0xd42e, "%x: ", (i << 2) + NAND_DU_SIZE + col_addr);
			}
		}
		u32 tmp;
		if (((i + 1) << 2) > spare_size) {// Cross DU boundary
			// Mask data of next DU
			u32 byte = (spare_size - (i << 2));
			tmp = data[1][(((i & (~0x3)) << 1) + 3 - i)] & ((1 << (byte << 3)) - 1);
			if (en_print) {
				ncl_console_trace(LOG_ERR, 0xe594, "%x", tmp);
			}
		} else {
			tmp = data[1][(((i & (~0x3)) << 1) + 3 - i)];
			if (en_print) {
				ncl_console_trace(LOG_ERR, 0xcc2c, "%x ", tmp);
			}
		}
		while (tmp) {
			bit1_cnt++;
			tmp &= tmp - 1;
		}

		if (en_print) {
			if ((i & 3) == 3) {
				ncl_console_trace(LOG_ERR, 0xff09, "\n");
			}
		}
	}
	if (en_print) {
		if ((i & 3) != 0) {
			ncl_console_trace(LOG_ERR, 0xf788, "\n");
		}
		ncl_console_trace(LOG_ERR, 0xe89d, "%d*0 vs %d*1\n", get_encoded_du_size() * 8 - bit1_cnt, bit1_cnt);
	}
	return bit1_cnt;
}

/*!
 * @brief PDA raw read function
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 * @param[in] bm_pl		BM payload
 *
 * @return		not used
 */
ddr_code void _pda_raw_read(pda_t pda, nal_pb_type_t pb_type, bm_pl_t* bm_pl)
{
	struct ncl_cmd_t ncl_cmd;
	ncl_cmd.op_code = NCL_CMD_READ_RAW;
	ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
	ncl_cmd.flags = (pb_type == NAL_PB_TYPE_SLC) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG;
	ncl_cmd.du_format_no = 1;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.completion = NULL;
	ncl_cmd_submit(&ncl_cmd);
}

// Return 1bit count
/*!
 * @brief PDA raw read function for nand VREF
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 * @param[in] en_print		print the uart message
 *
 * @return		1bits count
 */
ddr_code u32 pda_raw_read(pda_t pda, nal_pb_type_t pb_type, bool en_print)
{
	u32 cnt, i;
	bm_pl_t bm_pl[2];
	dtag_t dtag_list[2];
	u32* data[2];
	u32 bit1_cnt = 0;

	if (en_print) {
		ncl_console_trace(LOG_INFO, 0xf1c4, "Raw read %s PDA 0x%x", (u32)((pb_type == NAL_PB_TYPE_SLC) ? "SLC" : "TLC"), pda);
	}
	cnt = 2;
	cnt = dtag_get_bulk(DTAG_T_SRAM, cnt, dtag_list);
	if (cnt != 2) {
		ncl_console_trace(LOG_WARNING, 0x355a, "no dtag");
		dtag_put_bulk(DTAG_T_SRAM, cnt, dtag_list);
		sys_assert(0);
	}

	for (i = 0; i < cnt; i++) {
		data[i] = dtag2mem(dtag_list[i]);
	}

	for (i = 0; i < cnt; i++) {
		bm_pl[i].all = 0;
		bm_pl[i].pl.dtag = dtag_list[i].dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}

	_pda_raw_read(pda, pb_type, bm_pl);

	bit1_cnt = du_raw_data_dump(pda, data, en_print);
	dtag_put_bulk(DTAG_T_SRAM, cnt, dtag_list);
	return bit1_cnt;
}

/*!
 * @brief NCL PDA raw read console
 *
 * @param[in] argc		testing parameter
 * @param[in] argv		testing parameter
 *
 * @return		value(true or false)
 */

static ddr_code int ncl_pda_raw_read_console(int argc, char *argv[])
{
	if (argc >= 2) {
		pda_t pda = strtol(argv[1], (void *)0, 0);
		enum nal_pb_type pb_type = NAL_PB_TYPE_SLC;
		bool from_cache = false;

		if (argc >= 3)
			pb_type = (enum nal_pb_type)strtol(argv[2], (void *)0, 10);
		if (argc >= 4) {
			from_cache = strtol(argv[3], (void *)0, 10);
		}

		extern struct finstr_format rawread_fins_templ;
		if (from_cache) {
			// Assume FINST_NAND_FMT_SLC_READ_CMD is identical as FINST_NAND_FMT_READ_DATA except prefix
			rawread_fins_templ.dw0.b.finst_type = FINST_TYPE_READ_DATA;
			ncl_console_trace(LOG_ERR, 0xf99a, "\nRawread PDA %x, type %d from cache\n", pda, pb_type);
		} else {
			ncl_console_trace(LOG_ERR, 0xcce4, "\nRawread PDA %x, type %d\n", pda, pb_type);
		}
		pda_raw_read(pda, pb_type, true);
		if (from_cache) {
			rawread_fins_templ.dw0.b.finst_type = FINST_TYPE_READ;
		}
	}

	return 0;
}

static DEFINE_UART_CMD(pda_rawread, "pda_rawread",
		"pda_rawread [PDA] [PB type][read cache]",
		"read PDA raw data from a 16 hex pda, type:0 - SLC(Default), 1 - XLC",
		1, 3, ncl_pda_raw_read_console);

#if ENA_DEBUG_UART
#if (USE_TSB_NAND || USE_MU_NAND)
/*!
 * @brief NCL PDA register control mode read console
 *
 * @param[in] argc		testing parameter
 * @param[in] argv		testing parameter
 *
 * @return		value(bit 1 count or -1)
 */
static ucmd_code int pda_rctrl_read(int argc, char *argv[])
{
	if (argc == 2) {
		pda_t pda = strtol(argv[1], (void *)0, 0);
		u32 *buf;
		u32 ch = pda2ch(pda);
		u32 ce = pda2ce(pda);
		u32 row = pda2row(pda, NAL_PB_TYPE_SLC);
		u32 col = pda2column(pda);
		u32 enc_du_sz = get_encoded_du_size();
		u32 enc_pg_sz = enc_du_sz * DU_CNT_PER_PAGE;
		u32 du_off = (pda >> nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);
		u32 i;
		u32 bit1_cnt = 0;

		ncl_console_trace(LOG_ERR, 0x41d8, "\nRegister control mode SLC read pda 0x%x started\n", pda);
		ficu_mode_disable();
		ndcu_en_reg_control_mode();

		buf = (u32 *)sys_malloc(SLOW_DATA, enc_pg_sz);
		if (buf == NULL) {
			ncl_console_trace(LOG_ERR, 0xf6d1, "Couldn't alloc enough space from SRAM\n");
			sys_assert(0);
		}
		memset(buf, 0, enc_pg_sz);

		/* SLC mode */
		ndcu_ind_t ctrl = {
			.write = true,
			.cmd_num = 0,
			.cle_mode = 1,
			.xfcnt = 0,
#ifdef USE_MU_NAND
			.reg1.b.ind_byte0 = NAND_MU_SLC_ENABLE,
#endif
#ifdef USE_TSB_NAND
			.reg1.b.ind_byte0 = NAND_SLC_PREFIX,
#endif
		};
		ndcu_open(&ctrl, ch, ce);
		ndcu_start(&ctrl);
		ndcu_close(&ctrl);

		/* Issue Read cycles */
		ndcu_ind_t ctrl_r = {
			.write = true,
			.cmd_num = 6,
			.cle_mode = 1,
			.xfcnt = 0,
			.reg1.b.ind_byte0 = NAND_READ_PAGE_CMD_CYC1,
			.reg1.b.ind_byte1 = 0,
			.reg1.b.ind_byte2 = 0,
			.reg1.b.ind_byte3 = row & 0xFF,
			.reg2.b.ind_byte4 = (row >> 8) & 0xFF,
			.reg2.b.ind_byte5 = (row >> 16) & 0xFF,
			.reg2.b.ind_byte6 = NAND_READ_PAGE_CMD_CYC2,
		};
		ndcu_open(&ctrl_r, ch, ce);
		ndcu_start(&ctrl_r);
		ndcu_close(&ctrl_r);

		nand_wait_ready(ch, ce);

#ifdef USE_MU_NAND
		/* Data out 0x00 */
		ndcu_ind_t ctrl_d = {
			.write = false,
			.cmd_num = 0,
			.cle_mode = 1,
			.xfcnt = enc_pg_sz,
			.buf = (u8 *)buf,
			.reg1.b.ind_byte0 = 0,
		};
#endif
#ifdef USE_TSB_NAND
		/* Data out 0x05-0xE0 */
		ndcu_ind_t ctrl_d = {
			.write = false,
			.cmd_num = 6,
			.cle_mode = 1,
			.xfcnt = enc_pg_sz,
			.buf = (u8 *)buf,
			.reg1.b.ind_byte0 = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
			.reg1.b.ind_byte1 = 0,
			.reg1.b.ind_byte2 = 0,
			.reg1.b.ind_byte3 = row & 0xFF,
			.reg2.b.ind_byte4 = (row >> 8) & 0xFF,
			.reg2.b.ind_byte5 = (row >> 16) & 0xFF,
			.reg2.b.ind_byte6 = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
		};
#endif
		ndcu_open(&ctrl_d, ch, ce);
		ndcu_start(&ctrl_d);

		do {
			if (ndcu_xfer(&ctrl_d))
				break;
		} while (1);
		ndcu_close(&ctrl_d);

		/* Swap higher 2 bytes with lower 2 bytes for printing */
		for (i = 0; i < enc_pg_sz / sizeof(u32); i++)
			buf[i] = ((buf[i] & 0xFFFF) << 16) | ((buf[i] >> 16) & 0xFFFF);

		/* Start printing */
		// ncl_console_trace(LOG_ERR, 0, "\n");
		for (i = 0; i < enc_du_sz / sizeof(u32); i++) {
			if ((i & 3) == 0) {
				ncl_console_trace(LOG_ERR, 0xc241, "%x: ", (i << 2) + col);
			}
			u32 off = du_off * enc_du_sz / sizeof(u32);
			ncl_console_trace(LOG_ERR, 0x0d08, "%x ", buf[i + off]);
			u32 tmp = buf[i + off];
			while (tmp) {
				bit1_cnt++;
				tmp &= tmp - 1;
			}
			if ((i & 3) == 3) {
				ncl_console_trace(LOG_ERR, 0xd4e2, "\n");
			}
		}
		ncl_console_trace(LOG_ERR, 0x079d, "\n%d*0 vs %d*1\n", enc_du_sz * 8 - bit1_cnt, bit1_cnt);

		sys_free(SLOW_DATA, buf);
		ncl_console_trace(LOG_ERR, 0x5c94, "Register control mode SLC read pda 0x%x done\n", pda);

		ndcu_dis_reg_control_mode();
		ficu_mode_enable();

		return bit1_cnt;
	} else {
		ncl_console_trace(LOG_ERR, 0x5ec5, "\nInvalid input parameter count\n");
		return -1;
	}
}

static DEFINE_UART_CMD(pda_rctrl_read, "pda_rctrl_read",
		"pda_rctrl_read [PDA]",
		"Read SLC DU by register control mode from a 16 hex pda",
		1, 1, pda_rctrl_read);
#endif

// Used for GoLogic trigger to send a reserved command
void ddr_code ncl_cmd_trigger(void)
{
	u32 ch = 0, ce = 0;
	// According to ONFI spec, 0Bh command is reserved
	ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 0,
		.reg1.b.ind_byte0 = 0x0B,
		.xfcnt = 0,
	};

	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_close(&ctrl);
}

/*!
 * @brief Count 1bits in one page
 *
 * @param[in] pda			test PDA
 * @param[in] pb_type		test PDA's PB type
 *
 * @return		1bits number
 */

ddr_code u32 pda_1bits_cnt(pda_t pda, nal_pb_type_t pb_type)
{
	u32 cnt, i;
	struct ncl_cmd_t ncl_cmd;
	bm_pl_t bm_pl[2];
	dtag_t dtag_list[2];
	u32* data[2];
	//u32 col_addr;
	u32 bit1_cnt = 0;

	cnt = 2;
	cnt = dtag_get_bulk(DTAG_T_SRAM, cnt, dtag_list);
	if (cnt != 2) {
		sys_assert(0);
	}

	for (i = 0; i < cnt; i++) {
		data[i] = dtag2mem(dtag_list[i]);
	}

	for (i = 0; i < cnt; i++) {
		bm_pl[i].all = 0;
		bm_pl[i].pl.dtag = dtag_list[i].dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}

	ncl_cmd.op_code = NCL_CMD_READ_RAW;
	ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
	ncl_cmd.flags = (pb_type == NAL_PB_TYPE_SLC) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG;
	ncl_cmd.du_format_no = 1;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.completion = NULL;
	ncl_cmd_submit(&ncl_cmd);

	for (i = 0; i < NAND_DU_SIZE / sizeof(u32); i++) {
		u32 tmp = data[0][i];
		while (tmp) {
			bit1_cnt++;
			tmp &= tmp - 1;
		}
	}

	u32 spare_size;
	spare_size = get_encoded_du_size() - NAND_DU_SIZE;

	for (i = 0; i < (spare_size + 3) / sizeof(u32); i++) {
		u32 tmp;
		if (((i + 1) << 2) > spare_size) {// Cross DU boundary
			// Mask data of next DU
			u32 byte = (spare_size - (i << 2));
			tmp = data[1][i] & ((1 << (byte << 3)) - 1);
		} else {
			tmp = data[1][i];
		}
		while (tmp) {
			bit1_cnt++;
			tmp &= tmp - 1;
		}
	}

	//ncl_console_trace(LOG_ERR, 0, "%d*0 vs %d*1\n", get_encoded_du_size() * 8 - bit1_cnt, bit1_cnt);
	dtag_put_bulk(DTAG_T_SRAM, cnt, dtag_list);
	return bit1_cnt;
}

/*!
 * @brief Do nand set feature
 *
 * @param[in] pda		PDA
 * @param[in] fa		flash addr
 * @param[in] value		value
 *
 * @return		Not used
 */

ucmd_code void set_feature_by_lun(pda_t pda, u32 fa, u32 val)
{
	u8 fcmd_id = 0;
	u8 du_dtag_ptr;
	struct finstr_format ins;
	struct fda_dtag_format *fda_dtag;
	struct ncl_cmd_t ncl_cmd;

	extern u32 fcmd_outstanding_cnt;
	fcmd_outstanding_cnt++;

	du_dtag_ptr = ncl_acquire_dtag_ptr();

	ncl_cmd.dtag_ptr = du_dtag_ptr;
	ncl_cmd.completion = NULL;
	ncl_cmd.flags = 0;

	// Configure fda list
	fda_dtag = ficu_get_addr_dtag_ptr(du_dtag_ptr);
	fda_dtag->ch_id = pda2ch(pda);
	fda_dtag->dev_id = pda2ce(pda);
	fda_dtag->fda_col = pda2lun(pda) + (fa << 8);

	// Configure instruction
	extern struct finstr_format set_feature_tmpl;
	ins = set_feature_tmpl;
	ins.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_FEATURE_LUN;
	ins.dw0.b.fins = 1;
	fcmd_id = du_dtag_ptr;
	ins.dw0.b.lins = 1;
	ncl_cmd_save(fcmd_id, &ncl_cmd);
	ins.dw1.b.fcmd_id = fcmd_id;
	ins.dw1.b.du_dtag_ptr = du_dtag_ptr;
	ins.dw2.all = val;
	ficu_fill_finst(ins);
	// Final submit instruction(s)
	ficu_submit_finstr(1);
	while(NCL_CMD_PENDING(&ncl_cmd)) {
		ficu_done_wait();
	}
}

ddr_code bool pda_self_adjust_read(pda_t pda, nal_pb_type_t pb_type, u32 check_level)
{
	bool ret = 0;

#if NCL_HAVE_ARD
	extern void ficu_mode_ard_control(u32 mode);
	ficu_mode_ard_control(0);
#endif

#if HAVE_TSB_SUPPORT && !QLC_SUPPORT
	sys_assert(pb_type == NAL_PB_TYPE_XLC);
	sys_assert(nand_is_bics4());

	u8 start_shift = 0xFC;
	u32 step = 1;
	u32 cnt = 7;//15
	u32 page_type;

	page_type = pda2pg_idx(pda);
	if (page_type != 1) {// Low or upper page
		set_feature_by_lun(pda, 0x89, start_shift * 0x01010101);
	}
	set_feature_by_lun(pda, 0x8A, (cnt << 29) + (step << 24) + start_shift * 0x010101);
	u32 idx;
	for (idx = FINST_NAND_FMT_XLC_READ_LOW; idx <= FINST_NAND_FMT_XLC_READ_UPR; idx++) {
		ndcu_writel(idx, NF_NCMD_FMT_PTR);
		ndcu_writel(ncmd_fmt_array[idx + FINST_NAND_FMT_XLC_SELF_ADJUST_READ_LOW - FINST_NAND_FMT_XLC_READ_LOW].fmt.all, NF_NCMD_FMT_REG0);
	}

	ret = read_pda_du(pda, pb_type, check_level, DU_FMT_USER_4K, false);
	for (idx = FINST_NAND_FMT_XLC_READ_LOW; idx <= FINST_NAND_FMT_XLC_READ_UPR; idx++) {
		ndcu_writel(idx, NF_NCMD_FMT_PTR);
		ndcu_writel(ncmd_fmt_array[idx].fmt.all, NF_NCMD_FMT_REG0);
	}
	if (pda2pg_idx(pda) != 1) {// Low or upper page
		set_feature_by_lun(pda, 0x89, 0);
	}
	set_feature_by_lun(pda, 0x8A, 0);
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern u32 tsb_read_detected_step(u8 ch, u8 ce, u8 lun, u8 fa);
	u8 tmp;
	switch(page_type) {
	case 0:
		tmp = tsb_read_detected_step(pda2ch(pda), pda2ce(pda), pda2lun(pda), 0x00);
		ncl_console_trace(LOG_ERR, 0x3cc7, "A %b, G %b\n", start_shift + step*(tmp & 0xF), start_shift + step*((tmp >> 4) & 0xF));
		break;
	case 1:
		tmp = tsb_read_detected_step(pda2ch(pda), pda2ce(pda), pda2lun(pda), 0x01);
		ncl_console_trace(LOG_ERR, 0xea59, "B %b, D %b, ", start_shift + step*(tmp & 0xF), start_shift + step*((tmp >> 4) & 0xF));
		tmp = tsb_read_detected_step(pda2ch(pda), pda2ce(pda), pda2lun(pda), 0x02);
		ncl_console_trace(LOG_ERR, 0x11a5, "F %b\n", start_shift + step*(tmp & 0xF));
		break;
	case 2:
		tmp = tsb_read_detected_step(pda2ch(pda), pda2ce(pda), pda2lun(pda), 0x03);
		ncl_console_trace(LOG_ERR, 0xd992, "C %b, G %b\n", start_shift + step*(tmp & 0xF), start_shift + step*((tmp >> 4) & 0xF));
		break;
	default:
		sys_assert(0);
	}
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
#endif

#if HAVE_MICRON_SUPPORT || HAVE_UNIC_SUPPORT
	u32 page;
	enum page_type_t page_type;
	page = pda2page(pda);
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	set_feature(pda2ch(pda), pda2ce(pda), NAND_MU_READ_CALIB, 3);
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();

	ret = read_pda_du(pda, pb_type, check_level, DU_FMT_USER_4K, false);
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	u32 i;
	u32 step = 0;
	u32 cnt = 0;
	u32 start_fa = 0;

	if (pb_type == NAL_PB_TYPE_XLC) {
		page_type = get_xlc_page_type(page);
	} else {
		page_type = SLC_PAGE;
	}
	switch(page_type) {
	case XLC_LOW_PAGE:
		ncl_console_trace(LOG_ERR, 0xfbb4, "TLC low page\n");
		start_fa = MU_SF_TLC_LOW_L4_READ_OFFSET;
		cnt = 2;
		step = 4;
		// When mid/upr page unprogrammed, use feature addr 0xAC
		break;
	case XLC_MID_PAGE:
		ncl_console_trace(LOG_ERR, 0x4dc0, "TLC extra page\n");
		start_fa = MU_SF_TLC_EXT_L1_READ_OFFSET;
		step = 2;
		cnt = 4;
		break;
	case XLC_UPR_PAGE:
		ncl_console_trace(LOG_ERR, 0x887c, "TLC upper page\n");
		start_fa = MU_SF_TLC_UPP_L2_READ_OFFSET;
		step = 4;
		cnt = 2;
		break;
	case SLC_PAGE:
	case SLC_LOW_PAGE:
		ncl_console_trace(LOG_ERR, 0x44bb, "SLC low page\n");
		start_fa = MU_SF_SLC_READ_OFFSET;
		cnt = 1;
		break;
	case MLC_LOW_PAGE:
		ncl_console_trace(LOG_ERR, 0x418e, "MLC low page\n");
		start_fa = MU_SF_MLC_LOW_L2_READ_OFFSET;
		cnt = 1;
		break;
	case MLC_UPR_PAGE:
		ncl_console_trace(LOG_ERR, 0xdd77, "MLC upper page\n");
		start_fa = MU_SF_MLC_UPP_L1_READ_OFFSET;
		step = 2;
		cnt = 2;
		break;
	}

	for (i = 0; i < cnt; i++) {
		u32 tmp;
		tmp = get_feature(pda2ch(pda), pda2ce(pda), start_fa + i * step);
		ncl_console_trace(LOG_ERR, 0x67cc, "%b: %b\n", start_fa + i * step, (tmp >> 16) & 0xFF);
	}

	set_feature(pda2ch(pda), pda2ce(pda), NAND_MU_READ_CALIB, 2);
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
#endif

#if NCL_HAVE_ARD
	ficu_mode_ard_control(2);
#endif
	return ret;
}
#endif

extern pda_t nal_rda_to_pda(rda_t * rda);
// Return lsb of best DLL suggested, 2nd lsb range end, 2nd msb range start, msb maximum error bits number

/*!
 * @brief Do PHY dll calibration for each channel
 *
 * @param[in] ch		device channel no
 *
 * @return		phase value
 */

 ucmd_code u32 ndphy_dll_calibration_enhance(u8 ch,u8 ce,u8 lun)
  {
  	pda_t pda;
  	rda_t rda;
  	u32 max_err_bits;// Maximum error bits number of certain DLL value
  	u32 best_dll_max_err_bits = ~0;// Minimum maximum error bits number of all DLL values
  	pdec_ctrs_reg3_t reg3;
  	u32 i;
  	//nf_dll_updt_ctrl_reg0_t reg0;
  	//nphy_ctrl_reg0_t reg1;

  	reg3.all = pdec_readl(PDEC_CTRS_REG3);
  	reg3.b.eccu_pdec_err_cnt_en = 1;
  	pdec_writel(reg3.all, PDEC_CTRS_REG3);
  	nf_rcmd_reg00_t reg00;
  	nf_rcmd_reg00_t reg10;

  	reg00.all = ndcu_readl(NF_RCMD_REG00);
  	reg10.all = ndcu_readl(NF_RCMD_REG10);

  	ndcu_writel(0x70707070, NF_RCMD_REG00);
  	ndcu_writel(0x70707070, NF_RCMD_REG10);

  	// Modify nand command format, so read/program from/to NAND page register
  	nf_ncmd_fmt_ptr_t reg_ptr;
  	reg_ptr.all = 0;

  	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_SLC_PROG;
  	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
  	ndcu_writel(ncmd_fmt_array[FINST_NAND_FMT_SLC_PROG_FOR_RANDOM].fmt.all, NF_NCMD_FMT_REG0);


  	// Raw write to page registers
  	extern struct finstr_format prog_fins_templ;
  	prog_fins_templ.dw0.b.poll_dis = 1;
  	rda.ch = ch;
  	rda.dev = ce;
  	rda.row = lun << nand_info.row_lun_shift;
  	rda.du_off = 0;
  	rda.pb_type = NAL_PB_TYPE_SLC;
  	pda = nal_rda_to_pda(&rda);
  	//ncl_pda_raw_operation(pda, NAL_PB_TYPE_SLC, 0, bm_pl_wr, 2, false, NULL);
  	write_pda_page(pda, NAL_PB_TYPE_SLC, 0);
  	prog_fins_templ.dw0.b.poll_dis = 0;

  #if NCL_HAVE_ARD
  	ficu_mode_ard_control(0);
  #endif
  	// Raw read from page registers
  	u32 start[2] = {0, 0}, end[2] = {0, 0};// One for no error bit range, one for a few error bit range
  	bool range[2] = {false, false};
  	bool found[2] = {false, false};
  	u32 result = 0xFFFFFFFF;
  	u32 val = 0;
  	for (val = 0; val < 64; val++) {
  		max_err_bits = 0;
  		bool fail[2] = {false, false};

  		ndphy_set_dll_phase(ch, val);
  		ndphy_init_per_ch_dll_lock(ch);
  		rda.ch = ch;
  		rda.dev = ce;
  		rda.row = lun << nand_info.row_lun_shift;
  		rda.du_off = 0;
  		rda.pb_type = NAL_PB_TYPE_SLC;
  		pda = nal_rda_to_pda(&rda);
  		//ncl_pda_raw_operation(pda, NAL_PB_TYPE_SLC, 0, &bm_pl_rd, false, false, NULL);
  		u32 ret = read_pda_du(pda, NAL_PB_TYPE_SLC, 2, DU_FMT_USER_4K, false);
  		if (!ret) {
  			fail[0] = true;
  			fail[1] = true;
  		}
  		pdec_ctrs_reg1_t err_cnt_reg;
  		err_cnt_reg.all = pdec_readl(PDEC_CTRS_REG1);
  		u32 err_bits;
  		err_bits = err_cnt_reg.b.eccu_pdec_err_cnt0to1 + err_cnt_reg.b.eccu_pdec_err_cnt1to0;
  		if (err_bits != 0) {
  			fail[0] = true;
  			if (err_bits > 8) {
  				fail[1] = true;
  			}
  		}
  		if (max_err_bits < err_bits) {
  			max_err_bits = err_bits;
  		}
  		u32 i;
  		for (i = 0; i < 2; i++) {
  			if (fail[i]) {
  				if (range[i]) {
  					range[i] = false;
  					end[i] = val - 1;
  				}
  			} else {
  				if (!range[i]) {
  					if (!found[i]) {
  						range[i] = true;
  						found[i] = true;
  						start[i] = val;
  					}
  				}
  				if (best_dll_max_err_bits > max_err_bits) {
  					best_dll_max_err_bits = max_err_bits;
  				}
  			}
  		}
  	}
  	for (i = 0; i < 2; i++) {
  		if (found[i]) {
  			if (range[i]) {
  				end[i] = 63;
  			}
  			result = (start[i] + end[i]) / 2;
  			ndphy_set_dll_phase(ch, result);
  			ndphy_init_per_ch_dll_lock(ch);
  			//ncl_console_trace(LOG_ERR, 0, "Ch %d range [%b %b] good, suggest %b, max err %d\n", ch, start[i], end[i], result, best_dll_max_err_bits);
  			result |= (best_dll_max_err_bits << 24) | (start[i] << 16) | (end[i] << 8);
  			break;
  		}
  	}
  	if (i == 2) {
  		ndphy_set_dll_phase(ch, 0x1F);
  		ndphy_init_per_ch_dll_lock(ch);
  		//ncl_console_trace(LOG_ERR, 0, "Ch %d DLL cali fail\n", ch);
  	}

  #if NCL_HAVE_ARD
  	ficu_mode_ard_control(2);
  #endif

  	ndcu_writel(reg00.all, NF_RCMD_REG00);
  	ndcu_writel(reg10.all, NF_RCMD_REG10);

  	// Restore to original ndcmd format
  	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_SLC_PROG;
  	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
  	ndcu_writel(ncmd_fmt_array[FINST_NAND_FMT_SLC_PROG].fmt.all, NF_NCMD_FMT_REG0);

  	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_SLC_READ_CMD;
  	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
  	ndcu_writel(ncmd_fmt_array[FINST_NAND_FMT_SLC_READ_CMD].fmt.all, NF_NCMD_FMT_REG0);
  	reg3.b.eccu_pdec_err_cnt_en = 0;
  	pdec_writel(reg3.all, PDEC_CTRS_REG3);
  	return result;
  }



#if ENA_DEBUG_UART
/*!
 * @brief Main function for PHY dll calibration for each channel console
 *
 * @param[in] argc		testing parameters
 * @param[in] argv		testing parameters
 *
 * @return		value(true or false)
 */

 static ucmd_code int ndphy_dll_calibration_console(int argc, char *argv[])
 {
 	if (argc >= 4) {
 		u8 ch = (bool)strtol(argv[1], (void *)0, 0);
 		u8 ce = (bool)strtol(argv[2], (void *)0, 0);
 		u8 lun = (bool)strtol(argv[3], (void *)0, 0);
 		if (ch >= nand_channel_num()) {
 			ncl_console_trace(LOG_ERR, 0x7056, " Channel %d is invalid.\n", ch);
 			return -1;
 		}
 		log_level_t old = log_level_chg(LOG_ALW);
 		ndphy_dll_calibration_enhance(ch,ce,lun);
 		log_level_chg(old);
 	}
 	return 0;
 }

 static DEFINE_UART_CMD(phy_dll_cali, "phy_cali",
                        "phy_cali ch ce lun",
                         "Phy calibration per channel",
                       3, 3, ndphy_dll_calibration_console);


#if HAVE_HYNIX_SUPPORT
fast_data u8 vref_bitmap[XLC] = {
	BIT3 | BIT7,// Lower page
	BIT2 | BIT4 | BIT6,// Middle(Extra page)
	BIT1 | BIT5// Upper page
};
fast_data u8 vref_stitch_dist[(1 << XLC) - 2] = {0};							///< distance between 7 vrefs unknown
fast_data u8 vref_shift_max[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};	///< maximum vref shift to the right
#elif HAVE_TOGGLE_SUPPORT || HAVE_YMTC_SUPPORT
#if HAVE_TSB_SUPPORT && QLC_SUPPORT
// For toshiba QLC etc
fast_data u16 vref_bitmap[XLC] = {
	BIT1 | BIT4 | BIT6 | BIT11,// Lower page
	BIT3 | BIT7 | BIT9 | BIT13,// Middle(Extra page)
	BIT2 | BIT8 | BIT14,// Upper page
	BIT5 | BIT10 | BIT12 | BIT15
};
#else
// For toshiba BiCS3 etc
fast_data u8 vref_bitmap[XLC] = {
	BIT1 | BIT5,// Lower page
	BIT2 | BIT4 | BIT6,// Middle(Extra page)
	BIT3 | BIT7// Upper page
};
#endif
#if HAVE_SAMSUNG_SUPPORT
fast_data u8 vref_stitch_dist[(1 << XLC) - 2] = {72, 78, 76, 81, 81, 99};		///< distance between 7 vrefs
#elif HAVE_TSB_SUPPORT && !QLC_SUPPORT
fast_data u8 vref_stitch_dist[(1 << XLC) - 2] = {59, 62, 63, 62, 64, 70};							///< BiCS3
#else
fast_data u8 vref_stitch_dist[(1 << XLC) - 2] = {0};							///< distance between 7 vrefs unknown
#endif
fast_data u8 vref_shift_max[15] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};	///< maximum vref shift to the right
#elif HAVE_UNIC_SUPPORT
#if QLC_SUPPORT
// For Intel QLC NAND ID 89h, D4h, 0Ch, 32h, AAh
fast_data u16 vref_bitmap[XLC] = {
	BIT8,// Lower page
	BIT4 | BIT12,// Middle page
	BIT2 | BIT6 | BIT10 | BIT14,// Upper page
	BIT1 | BIT3 | BIT5 | BIT7 | BIT9 | BIT11 | BIT13 | BIT15,// Top page
};
#else
// For Unic B17 NAND ID 89h, C4h, 08h, 32h, A6h
fast_data u8 vref_bitmap[XLC] = {
	BIT4,// Lower page
	BIT1 | BIT3 | BIT5 | BIT7,// Middle(Extra page)
	BIT2 | BIT6// Upper page
};
#endif
fast_data u8 vref_stitch_dist[(1 << XLC) - 2] = {0};	///< distance between 15 vrefs
fast_data u8 vref_shift_max[15] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};	///< maximum vref shift to the right
#else
// For micron, B17A B27A etc
fast_data u8 vref_bitmap[XLC] = {
	BIT4,// Lower page
	BIT1 | BIT3 | BIT5 | BIT7,// Middle(Extra page)
	BIT2 | BIT6// Upper page
};
fast_data u8 vref_stitch_dist[(1 << XLC) - 2] = {95, 100, 100, 84, 87, 96};	///< distance between 7 vrefs of B27A
fast_data u8 vref_shift_max[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xB2, 0xFF, 0xFF};	///< maximum vref shift to the right
#endif

/*!
 * @brief Count 1bits in one page, called by nand_histogram()
 *
 * @param[in] page			test page
 * @param[in] page_list		point to the word line pages of testing page
 *
 * @return		the number of pages per word line
 */

ddr_code u32 nand_pages_of_wl(u32 page, u32* page_list)
{
	u32 page_per_wl;

#if HAVE_SAMSUNG_SUPPORT
	u32 i;
	if ((page < 8) || (page > 0x2EF)) {
		sys_assert(0);
	} else {
	for (i = 0; i < XLC; i++) {
			page_list[i] = page - ((page + 1) % XLC) + i;
		}
		return XLC;
	}
#elif HAVE_TOGGLE_SUPPORT || HAVE_YMTC_SUPPORT
	u32 i;
	for (i = 0; i < XLC; i++) {
		page_list[i] = page - (page % XLC) + i;
	}
	return XLC;
#elif HAVE_UNIC_SUPPORT && QLC_SUPPORT
	page_per_wl = 4;
	if (page == 48) {
		page_list[0] = 48;
		page_list[1] = 49;
		page_list[2] = 50;
		page_list[3] = 120;
	} else {
		// TODO
		sys_assert(0);
	}
	return 4;
#elif HAVE_UNIC_SUPPORT && !QLC_SUPPORT
	page_per_wl = 3;
	if (page == 36) {
		page_list[0] = 36;
		page_list[1] = 60;
		page_list[2] = 61;
	} else {
		// TODO
		sys_assert(0);
	}
	return 3;
#else
	if (page == 200) {
		page_per_wl = 3;
		page_list[0] = 200;
		page_list[1] = 306;
		page_list[2] = 307;
	}
	if (page < 90) {
		if (page >= 54) {
			page_per_wl = 3;
			page_list[0] = page;
			page_list[1] = 90 + (page - 54) * 3;
			page_list[2] = 91 + (page - 54) * 3;
		} else {
			// SLC or MLC WL
			sys_assert(0);
		}
	} else if (page < 2440) {
		switch(page % 3) {
		case 0://XP
			if (page < 197) {
				page_list[0] = 54 + (page - 90) / 3;
			} else {
				page_list[0] = page - (198 - 92);
			}
			page_list[1] = page;
			page_list[2] = page + 1;
			break;
		case 1://UP
			if (page < 197) {
				page_list[0] = 54 + (page - 91) / 3;
			} else {
				page_list[0] = page - (199 - 92);
			}
			page_list[1] = page - 1;
			page_list[2] = page;
			break;
		case 2://LP
			page_list[0] = page;
			page_list[1] = page + (198 - 92);
			page_list[2] = page + (199 - 92);
			break;
		}
		page_per_wl = 3;
	} else {
		// TODO
		sys_assert(0);
	}
#endif
	return page_per_wl;
}

#define XLC_VREF_CNT	((1 << XLC) - 1)	///< XLC vref count

/*!
 * @brief Some nand need extra workaround to enable vref shift for read
 *
 * @param[in] en			enable
 *
 * @return		Not used
 */
ddr_code void nand_read_enable_vref(bool en, pda_t pda)
{
#if HAVE_UNIC_SUPPORT
#if QLC_SUPPORT
	if (en) {
		ncl_set_feature(pda, 0xEE, 1);///< Read Offset for Snap Read and Program Pre-Read
	} else {
		ncl_set_feature(pda, 0xEE, 0);
	}
#else
	if (en) {
		ncl_set_feature(pda, 0xF5, 4);// Enable snap read
	} else {
		ncl_set_feature(pda, 0xF5, 0);
	}
#endif
#elif HAVE_TSB_SUPPORT && QLC_SUPPORT
	nf_ncmd_fmt_ptr_t reg_ptr;
	nf_ncmd_fmt_reg0_t reg;
	u32 i;
	reg_ptr.all = 0;
	// Add 5Dh prefix required for read with vref shift
	for (i = 0; i < XLC; i++) {
		reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_TLC_READ(i);
		ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
		reg.all = ndcu_readl(NF_NCMD_FMT_REG0);
		if (en) {
			reg.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1;
		} else {
			reg.b.prefix_cmd1	= NAND_FMT_PREFIX_NO;
		}
		ndcu_writel(reg.all, NF_NCMD_FMT_REG0);
		reg.all = ndcu_readl(NF_NCMD_FMT_REG0);
	}
#endif
	if (0)  {
		sys_assert(en);
	}
}

/*!
 * @brief Count 1bits in one page, called by nand_histogram()
 *
 * @param[in] pda			test PDA
 * @param[in] repeat		testing repeat time
 *
 * @return		Not used
 */
ddr_code void nand_wl_vt_histogram(pda_t pda, u32 repeat, u32* pages)
{
	u32 page;
	u32 page_list[XLC];
	bool inc[XLC];// Bit 1 cnt increase or decrease as vref shift right
	u32 page_per_wl;
	u32 i, pg_idx = 0;
	u32 loop;
	u32 cnt[256];
	s16* diff[XLC_VREF_CNT];

	diff[0] = sys_malloc(SLOW_DATA, sizeof(u16) * XLC_VREF_CNT * 256);
	if (diff[0] == NULL) {
		return;
	}
	for (i = 1; i < XLC_VREF_CNT; i++) {
		diff[i] = diff[i - 1] + 256;
	}

	nand_read_enable_vref(true, pda);

	page = pda2page(pda);
	pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);

	if (pages == NULL) {
		page_per_wl = nand_pages_of_wl(page, page_list);
		sys_assert(page_per_wl == XLC);
	} else {
		page_per_wl = XLC;
		for (i = 0; i < page_per_wl; i++) {
				page_list[i] = pages[i];
		}
	}

	for (i = 0; i < XLC; i++) {
		inc[i] = true;
	}

	for (loop = 1; loop < (1 << XLC); loop++) {
		for (i = 0; i < XLC; i++) {
			if (vref_bitmap[i] & (1 << loop)) {
				pg_idx = i;
				break;
			}
		}
		ncl_console_trace(LOG_ERR, 0x8428, "Part %d page %d---------------------\n", loop, page_list[pg_idx]);
		switch(pg_idx) {
		case 0:
			ncl_console_trace(LOG_ERR, 0xd046, "Low page\n");
			break;
		case 1:
			ncl_console_trace(LOG_ERR, 0x3253, "Mid page\n");
			break;
		case 2:
			ncl_console_trace(LOG_ERR, 0xae70, "Upper page\n");
			break;
		case 3:
			ncl_console_trace(LOG_ERR, 0xe958, "Top page\n");
			break;
		}
		for (i = 0; i < 256; i++) {
			cnt[i] = 0;
			nand_vref_cfg(pda, loop, (i - 0x80) & 0xFF);
			u32 r;
			for (r = 0; r < repeat; r++) {
				cnt[i] += pda_raw_read(pda + (page_list[pg_idx] << nand_info.pda_page_shift), NAL_PB_TYPE_XLC, false);
			}
			cnt[i] += repeat/2;
			cnt[i] /= repeat;
		}
		nand_vref_cfg(pda, loop, 0);

		// Calculate delta of '1' bit count of adjacent vrefs
		for (i = 1; i < 0x100; i++) {
			if ((cnt[i] == get_encoded_du_size() * 8) || (cnt[i - 1] == get_encoded_du_size() * 8)) {
				diff[loop - 1][i] = 0;
			} else if (inc[pg_idx]) {
				diff[loop - 1][i] = cnt[i] - cnt[i - 1];
			} else {
				diff[loop - 1][i] = cnt[i - 1] - cnt[i];
			}
			if ((diff[loop - 1][i] > 1000) && (diff[loop - 1][i] < 65535)) {
				diff[loop - 1][i] = 0;
			}
			if (diff[loop - 1][i] < -1000) {
				diff[loop - 1][i] = 0;
			}
		}
		inc[pg_idx] = !inc[pg_idx];
	}
	for (i = 1; i < 256; i++) {
		ncl_console_trace(LOG_ERR, 0x15c0, "%xh", i);
		for (loop = 0; loop < XLC_VREF_CNT; loop++) {
			ncl_console_trace(LOG_ERR, 0xfef6, ",%d", diff[loop][i]);
		}
		// ncl_console_trace(LOG_ERR, 0, "\n");
	}

	u32 range_cnt[1 << XLC];// Bit count of each 8 ranges

	for (i = 0; i < (1 << XLC); i++) {
		range_cnt[i] = 0;
	}

	for (i = 1; i <= 0x80; i++) {
		ncl_console_trace(LOG_ERR, 0xd586, "%d\n", diff[0][i]);
		range_cnt[0] +=  diff[0][i];
	}

	ncl_console_trace(LOG_ERR, 0xb0a5, "-20\n");// Default vref indicator

	// Stitch 7 adjacent histograms into a whole histogram
	for (loop = 1; loop < (1 << XLC) - 1; loop++) {
		if (vref_stitch_dist[loop - 1] != 0) {
			u32 shift;
			if ((vref_shift_max[loop - 1] - 0x80) > vref_stitch_dist[loop - 1]) {
				shift = 0x80 + vref_stitch_dist[loop - 1];
			} else {
				shift = vref_shift_max[loop - 1];
			}
			for (i = 0x81; i < shift; i++) {
				ncl_console_trace(LOG_ERR, 0x2ed9, "%d\n", diff[loop - 1][i]);
				range_cnt[loop] +=  diff[loop - 1][i];
			}
			for (i = shift - vref_stitch_dist[loop - 1]; i <= 0x80; i++) {
				ncl_console_trace(LOG_ERR, 0x5913, "%d\n", diff[loop][i]);
				range_cnt[loop] +=  diff[loop][i];
			}
			ncl_console_trace(LOG_ERR, 0x4a53, "-20\n");// Default vref indicator
			continue;
		}
		bool lock = false;
		u32 prev_start = 0, prev_end = 0;
		u32 next_start = 0, next_end = 0;
		u32 cnt = 0;// Counting down filter for noise

		// Find first hill window of previous vref shift positive
		cnt = 0;
		for (i = 0x80; i < 256; i++) {
			if (diff[loop - 1][i] > 20) {
				if ((cnt == 0) && !lock) {
					prev_start = i;
				}
				cnt = 3;
				if (diff[loop - 1][i] > 70) {
					lock = true;
				}
			} else {
				if (cnt == 0) {
					if (lock) {
						prev_end = i - 4;
						break;
					} else {
						continue;
					}
				} else {
					cnt--;
				}
			}
		}
		//ncl_console_trace(LOG_ERR, 0, "loop %d window %b %b\n", loop, prev_start, prev_end);

		// Find first hill window of next vref shift negative
		cnt = 255;
		lock = false;
		for (i = 0x80; i > 0; i--) {
			if (diff[loop][i] > 20) {
				if ((cnt > 100) && !lock) {
					next_end = i;
				}
				cnt = 3;
				if (diff[loop][i] > 70) {
					lock = true;
				}
			} else {
				if (cnt == 0) {
					if (lock) {
						next_start = i + 4;
						break;
					} else {
						next_start = i + 4;
						cnt = 255;
					}
				} else {
					cnt--;
				}
			}
		}
		//ncl_console_trace(LOG_ERR, 0, "LOOP %d window %b %b\n", loop, next_start, next_end);

		u32 width[2];
		u32 distance;
		u32 best_diff = 0;

		// Get a gross distance
		width[0] = prev_end - prev_start;
		width[1] = next_end - next_start;
		if (width[0] > width[1]) {
			distance = prev_end - next_end;
		} else {
			distance = prev_start - next_start;
		}

		// Fine tune the distance of 2 hill windows by minimum differences
		best_diff = ~0;
		u32 shift;
		u32 best_shift = 0;
		for (shift = 0; shift < 15; shift++) {
			u32 cur_diff;
			cur_diff = 0;
			if (width[0] > width[1]) {
				for (i = next_start; i <= next_end; i++) {
					if (diff[loop][i] > diff[loop - 1][i + (distance + shift - 7)]) {
						cur_diff += diff[loop][i] - diff[loop - 1][i + (distance + shift - 7)];
					} else {
						cur_diff += diff[loop - 1][i + (distance + shift - 7)] - diff[loop][i];
					}
				}
			} else {
				for (i = prev_start; i <= prev_end; i++) {
					if (diff[loop][i - (distance + shift - 7)] > diff[loop - 1][i]) {
						cur_diff += diff[loop][i - (distance + shift - 7)] - diff[loop - 1][i];
					} else {
						cur_diff += diff[loop - 1][i] - diff[loop][i - (distance + shift - 7)];
					}
				}
			}
			if (cur_diff < best_diff) {
				best_shift = shift;
				best_diff = cur_diff;
			}
		}
		distance = distance + best_shift - 7;

		// Find hill middle (probably peak) to stitch them together
		if (width[1] < width[0]) {
			shift = (next_start + next_end) / 2 + distance;
		} else {
			shift = (prev_start + prev_end) / 2;
		}
		for (i = 0x81; i < shift; i++) {
			ncl_console_trace(LOG_ERR, 0xf180, "%d\n", diff[loop - 1][i]);
		}
		for (i = shift - distance; i <= 0x80; i++) {
			ncl_console_trace(LOG_ERR, 0x983f, "%d\n", diff[loop][i]);
		}
		if (width[1] < width[0]) {
			shift = next_end + distance;
		} else {
			shift = prev_start;
		}
		for (i = 0x81; i < shift; i++) {
			range_cnt[loop] +=  diff[loop - 1][i];
		}
		for (i = shift - distance; i <= 0x80; i++) {
			range_cnt[loop] +=  diff[loop][i];
		}
		ncl_console_trace(LOG_ERR, 0x61b9, "-20\n");// Default vref indicator
	}

	for (i = 0x81; i < 0x100; i++) {
		ncl_console_trace(LOG_ERR, 0x33ca, "%d\n", diff[XLC_VREF_CNT-1][i]);
		range_cnt[XLC_VREF_CNT] += diff[XLC_VREF_CNT-1][i];
	}
	for (i = 0; i < (1 << XLC); i++) {
		u32 ratio;
		ratio = range_cnt[i] * 1000 / (get_encoded_du_size() * 8);
		ncl_console_trace(LOG_ERR, 0x008a, "Range %d, %d, %d.%d%%\n", i, range_cnt[i], ratio/10, ratio%10);
	}

	sys_free(SLOW_DATA, diff[0]);

	nand_read_enable_vref(false, pda);
}

/*!
 * @brief Get nand histogram main function
 *
 * @param[in] argc		testing parameters
 * @param[in] argv		testing parameters
 *
 * @return		value(true or false)
 */

static ddr_code int nand_histogram(int argc, char *argv[])
{
	bool support = false;

#if HAVE_MICRON_SUPPORT
	if (nand_is_b27a()) {
		support = true;
	}
#endif

#if HAVE_TSB_SUPPORT
	if (nand_is_bics3() || nand_is_bics4_TH58LJT1V24BA8H()) {
		support = true;
	}
#endif

#if HAVE_YMTC_SUPPORT
	if (nand_is_YMN08TB1B1DU1B()) {
		support = true;
	}
#endif

#if HAVE_HYNIX_SUPPORT
	if (nand_is_3dv5()) {
		support = true;
	}
#endif

#if HAVE_SAMSUNG_SUPPORT
	if (nand_is_K9AFGD8H0A()) {
		support = true;
	}
#endif

#if HAVE_UNIC_SUPPORT
	if (nand_is_b17()) {
		support = true;
	}
#endif

	pda_t pda;
	u32 repeat;
	u32 pages[XLC];
	u32* page_list = NULL;

	if (support) {
		pda = strtol(argv[1], (void *)0, 0);
		if (argc >= 3) {
			repeat = strtol(argv[2], (void *)0, 0);
			if ((repeat < 1) || (repeat > 32)) {
				return 0;
			}

			if (argc > 3) {
				if (argc == (3 + XLC)) {
					u32 i;
					for (i = 0; i < XLC; i++) {
						pages[i] = strtol(argv[3 + i], (void *)0, 0);
					}
					page_list = pages;
				}
			} else {
				return 0;
			}

		} else {
			repeat = 1;
		}
		nand_wl_vt_histogram(pda, repeat, page_list);
	} else {
		ncl_console_trace(LOG_ERR, 0x0ea1, "Not support\n");
	}
	return 0;
}

static DEFINE_UART_CMD(hist, "hist",
                       "hist pda [repeat]",
                        "Draw histogram of WL of pda",
                      1, 5, nand_histogram);

/*!
 * @brief Read soft data by XOR 2 hard data (vref positive & negative)
 * Strong 0 for better readability
 *
 * @param pda	PDA
 * @param shift	Vref shift
 *
 * @return	not used
 */
static ddr_code int pda_soft_read(int argc, char *argv[])
{
	u32 cnt, i;
	bm_pl_t bm_pl[4];
	dtag_t dtag_list[6];	// 2 for negative raw data, 2 for positive, and 2 for soft data
	u32* data[6];
	pda_t pda;
	enum nal_pb_type pb_type = NAL_PB_TYPE_XLC;
	u32 vref_shift = 8;

	pda = strtol(argv[1], (void *)0, 0);
	if (argc >= 3) {
		vref_shift = strtol(argv[2], (void *)0, 0);
	}

	cnt = 6;
	cnt = dtag_get_bulk(DTAG_T_SRAM, cnt, dtag_list);
	if (cnt != 6) {
		ncl_console_trace(LOG_WARNING, 0xb41f, "no dtag");
		dtag_put_bulk(DTAG_T_SRAM, cnt, dtag_list);
		sys_assert(0);
	}

	for (i = 0; i < cnt; i++) {
		data[i] = dtag2mem(dtag_list[i]);
	}

	for (i = 0; i < cnt; i++) {
		bm_pl[i].all = 0;
		bm_pl[i].pl.dtag = dtag_list[i].dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}

	nand_read_enable_vref(true, pda);
	// Vref shift negative and raw read
	for (i = 0; i <= XLC_VREF_CNT; i++) {
		nand_vref_cfg(pda, i, (0 - vref_shift) & 0xFF);
	}

	_pda_raw_read(pda, pb_type, bm_pl);

	// Vref shift positive and raw read
	for (i = 0; i <= XLC_VREF_CNT; i++) {
		nand_vref_cfg(pda, i, vref_shift & 0xFF);
	}
	_pda_raw_read(pda, pb_type, bm_pl+2);

	nand_read_enable_vref(false, pda);

	// Vref reset to default
	for (i = 0; i <= XLC_VREF_CNT; i++) {
		nand_vref_cfg(pda, i, 0x0);
	}

	// XOR 2 hard data
	for (i = 0; i < 2; i++) {
		u32 j;
		for (j = 0; j < NAND_DU_SIZE / sizeof(u32); j++) {
			data[i + 4][j] = data[i][j] ^ data[i+2][j];
		}
	}

	// Soft data dump
	du_raw_data_dump(pda, data + 4, true);

	dtag_put_bulk(DTAG_T_SRAM, cnt, dtag_list);
	return 0;
}

static DEFINE_UART_CMD(soft_read, "soft_read",
                       "soft_read pda [vref_shift]",
                        "Read soft data of PDA (strong 0)",
                      1, 2, pda_soft_read);
/*! @} */

ucmd_code int get_temp()
{
#if HAVE_MICRON_SUPPORT || HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT
	u32 ch, ce;
#endif /* HAVE_MICRON_SUPPORT || HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT */
	u32 t_min = 0xFF, t_max = 0xFF, t;

#if HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT || HAVE_MICRON_SUPPORT
	bool by_lun = true;
	u32 val_verify;
#endif /* HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT || HAVE_MICRON_SUPPORT */

#if HAVE_MICRON_SUPPORT
	u32 feature_addr;

	feature_addr = 0xE7;//micron get temperature address
	if (by_lun) {
		feature_addr <<= 8;
	}

	extern u32 ficu_get_feature(u8 ch, u8 ce, u16 fa, bool by_lun);

	for (ch = 0; ch < nand_channel_num(); ch++) {
		for (ce = 0; ce < nand_target_num(); ce++) {
			val_verify = ficu_get_feature(ch, ce, feature_addr, by_lun);

			t = (u32)((val_verify & 0x7F) - 37);
			if (t_min == 0xFF) {
				t_max = t;
				t_min = t;
			} else if (t > t_max) {
				t_max = t;
			} else if (t < t_min) {
				t_min = t;
			}
		}
	}
#endif
#if HAVE_TSB_SUPPORT || HAVE_SANDISK_SUPPORT
	nf_rdcmd_dummy_t rdcmd_reg_bak, rdcmd_reg;
	rdcmd_reg.all = ndcu_readl(NF_RDCMD_DUMMY);
	rdcmd_reg_bak.all = rdcmd_reg.all; // backup
	rdcmd_reg.b.r_dumy_cmd0 = 0x7C; // NAND_TSB_GET_TEMP_7C
	ndcu_writel(rdcmd_reg.all, NF_RDCMD_DUMMY);

	extern u32 ficu_get_tempature(u8 ch, u8 ce, bool by_lun);

	for (ch = 0; ch < nand_channel_num(); ch++) {
		for (ce = 0; ce < nand_target_num(); ce++) {
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
		}
	}

	ndcu_writel(rdcmd_reg_bak.all, NF_RDCMD_DUMMY); // restore
#endif

	t = t_min < 0 ? t_min : t_max;
	ncl_console_trace(LOG_ERR, 0x22ba, "\n\n\nt %d, t_max %d, t_min %d\n", t, t_max, t_min);

	return 0;
}

static DEFINE_UART_CMD(gettemp, "gettemp",
                       "gettemp",
                        "get flash temperature",
                      0, 0, get_temp);
#endif

ddr_code void ncl_panic_dump(void)
{
	u32 reg;

	evlog_printk(LOG_ERR, "CH0 ndphy reg dump %x - %x", NDPHY_REG_ADDR(0), NDPHY_REG_ADDR(0) + sizeof(ndphy_regs_t));
	for (reg = 0; reg < sizeof(ndphy_regs_t); reg += (4 * sizeof(u32))) {
		evlog_printk(LOG_ERR, "%x : %x %x %x %x", reg + NDPHY_REG_ADDR(0), 
		    ndphy_readl(0, reg), ndphy_readl(0, reg + 4), ndphy_readl(0, reg + 8), ndphy_readl(0, reg + 12));
	}

	evlog_printk(LOG_ERR, "ndcu reg dump %x - %x", NDCU_REG_ADDR, NDCU_REG_ADDR + sizeof(ncb_ndcu_regs_t));
	for (reg = 0; reg < sizeof(ncb_ndcu_regs_t); reg += (4 * sizeof(u32))) {
		evlog_printk(LOG_ERR, "%x : %x %x %x %x", reg + NDCU_REG_ADDR, 
		    ndcu_readl(reg), ndcu_readl(reg + 4), ndcu_readl(reg + 8), ndcu_readl(reg + 12));
	}

	evlog_printk(LOG_ERR, "eccu reg dump %x - %x", ECCU_REG_ADDR, ECCU_REG_ADDR + sizeof(ncb_eccu_regs_t));
	for (reg = 0; reg < sizeof(ncb_eccu_regs_t); reg += (4 * sizeof(u32))) {
		evlog_printk(LOG_ERR, "%x : %x %x %x %x", reg + ECCU_REG_ADDR, 
		    eccu_readl(reg), eccu_readl(reg + 4), eccu_readl(reg + 8), eccu_readl(reg + 12));
	}

	#if 0
	evlog_printk(LOG_ERR, "eccu dec top reg dump %x - %x", DEC_TOP_REG_ADDR, DEC_TOP_REG_ADDR + sizeof(dec_top_ctrl_regs_t));
	for (reg = 0; reg < sizeof(dec_top_ctrl_regs_t); reg += (4 * sizeof(u32))) {
		evlog_printk(LOG_ERR, "%x : %x %x %x %x", reg + DEC_TOP_REG_ADDR, 
		    dec_top_readl(reg), dec_top_readl(reg + 4), dec_top_readl(reg + 8), dec_top_readl(reg + 12));
	}

	evlog_printk(LOG_ERR, "eccu mdec reg dump %x - %x", MDEC_REG_ADDR, MDEC_REG_ADDR + sizeof(eccu_mdec_regs_t));
	for (reg = 0; reg < sizeof(eccu_mdec_regs_t); reg += (4 * sizeof(u32))) {
		evlog_printk(LOG_ERR, "%x : %x %x %x %x", reg + MDEC_REG_ADDR, 
		    mdec_readl(reg), mdec_readl(reg + 4), mdec_readl(reg + 8), mdec_readl(reg + 12));
	}

	evlog_printk(LOG_ERR, "eccu pdec reg dump %x - %x", PDEC_REG_ADDR, PDEC_REG_ADDR + sizeof(eccu_pdec_regs_t));
	for (reg = 0; reg < sizeof(eccu_pdec_regs_t); reg += (4 * sizeof(u32))) {
		evlog_printk(LOG_ERR, "%x : %x %x %x %x", reg + PDEC_REG_ADDR, 
		    pdec_readl(reg), pdec_readl(reg + 4), pdec_readl(reg + 8), pdec_readl(reg + 12));
	}
	#endif
	
	evlog_printk(LOG_ERR, "ficu reg dump %x - %x", FICU_REG_ADDR, FICU_REG_ADDR + sizeof(ncb_ficu_regs_t));
	for (reg = 0; reg < sizeof(ncb_ficu_regs_t); reg += (4 * sizeof(u32))) {
		evlog_printk(LOG_ERR, "%x : %x %x %x %x", reg + FICU_REG_ADDR, 
		    ficu_readl(reg), ficu_readl(reg + 4), ficu_readl(reg + 8), ficu_readl(reg + 12));
	}
	
	evlog_printk(LOG_ERR, "BTN reg Dump");

	u32 *addr = 0;
	u32 dw_cnt = 0;

	addr = (u32*)BM_BASE;
	dw_cnt = sizeof(btn_cmd_data_reg_regs_t) >> 2;
	
	while (dw_cnt > 0) {
		if (dw_cnt < 4)
			dw_cnt = 4;

		evlog_printk(LOG_ERR, "%08x : %08x %08x %08x %08x",
				addr, addr[0], addr[1], addr[2], addr[3]);
		addr += 4;
		dw_cnt -= 4;
	}
}
#if CPU_ID == 4
ddr_code int srb_bkup_console(int argc, char *argv[])  //Sean_20220704
{

	extern void SysInfo_bkup(u8 pl);
	extern bool defect_Rd_SRB(void * record, u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page, u8 DU_offset, u8 flag);

	AGING_TEST_MAP_t *MPIN_bkup;
	MPIN_bkup = sys_malloc(SLOW_DATA, sizeof(AGING_TEST_MAP_t));
	memset((void *)MPIN_bkup, 0x00, sizeof(AGING_TEST_MAP_t));
	
	u8 target_pl = strtol(argv[1], (void *)0, 0);
	u8 src_pl = (target_pl == 0) ? 1 : 0 ;
	u32 status;
	
	ncl_console_trace(LOG_ALW, 0x448e, "Copy CH4 CH0 PL %d SRB to CH4 CE0 PL %d", src_pl, target_pl);

	ncl_enter_mr_mode();
	status = defect_Rd_SRB((void *)MPIN_bkup, 4, 0, 0, src_pl, 0, 0, FLAG_AGINGTESTMAP);
	ncl_leave_mr_mode();
	if (status){
		SysInfo_bkup(target_pl);
		ncl_enter_mr_mode();
		status = defect_Rd_SRB((void *)MPIN_bkup, 4, 0, 0, target_pl, 0, 0, FLAG_AGINGTESTMAP);
		ncl_leave_mr_mode();
	}

	if (!status){
		ncl_console_trace(LOG_ERR, 0xeacd, "PL %d Backup Fail", target_pl);
	}
	else{
		ncl_console_trace(LOG_ERR, 0xc610, "PL %d Backup succeed", target_pl);
	}
	return 0;
}

static DEFINE_UART_CMD(srb_bkup, "srb_bkup",
		"srb_bkup [target_pl]",
		"backup srb to target_pl",
		1, 1, srb_bkup_console);
#endif

#if 0
// Reset nand during tPROG busy and check histogram, current code for BiCS3
void nand_xlc_block_write_abort(void)
{
	pda_t pda;
	extern bool erase_pda(pda_t pda, nal_pb_type_t pb_type);
	extern bool write_pda_page(pda_t pda, nal_pb_type_t pb_type, u32 step);
	extern bool read_pda_du(pda_t pda, nal_pb_type_t pb_type, u32 check_level, u8 du_fmt);
	u32 t1, t2;

	pda = 0x2000000;
	erase_pda(pda, NAL_PB_TYPE_XLC);
	write_pda_page(pda, NAL_PB_TYPE_XLC, 0);
	t1 = get_tsc_lo();
	write_pda_page(pda + (1 << nand_info.pda_page_shift), NAL_PB_TYPE_XLC, 0);
	t2 = get_tsc_lo();
	extern struct finstr_format prog_fins_templ;
	prog_fins_templ.dw0.b.poll_dis = 1;
	t1 = get_tsc_lo();
	write_pda_page(pda + (2 << nand_info.pda_page_shift), NAL_PB_TYPE_XLC, 0);
	t2 = get_tsc_lo();
	prog_fins_templ.dw0.b.poll_dis = 0;

	delay_us(2000);
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern void nand_reset_all(void);
	nand_reset_all();
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
	//read_pda_du(pda, NAL_PB_TYPE_XLC, 1, DU_FMT_USER_4K);
	nand_wl_vt_histogram(pda, 32);
}
#endif
