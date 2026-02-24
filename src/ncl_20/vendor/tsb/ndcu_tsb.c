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
/*! \file ndcu_tsb.c
 * @brief ndcu toshiba
 *
 * \addtogroup ncl
 * \defgroup ncl
 * \ingroup ncl
 * @{
 * Toshiba related ndcu configurations etc
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "ncb_ndcu_register.h"
#include "ndcu_reg_access.h"
#include "nand_define.h"
#include "finstr.h"
#include "ndcu.h"
#include "nand.h"
#include "ndcmd_fmt_tsb.h"

#define SCMD_RDY56_FAIL02	NAND_FMT_RD_SCMD_SBYTE0
#define SCMD_RDY6_FAIL02	NAND_FMT_RD_SCMD_SBYTE1
#define SCMD_RDY56_FAIL0	NAND_FMT_RD_SCMD_SBYTE2
#define SCMD_RDY6_FAIL0		NAND_FMT_RD_SCMD_SBYTE3
#define SCMD_RDY56_FAIL2	NAND_FMT_RD_SCMD_SBYTE4
#define SCMD_RDY6_FAIL2		NAND_FMT_RD_SCMD_SBYTE5
#define SCMD_RDY56			NAND_FMT_RD_SCMD_SBYTE6
#define SCMD_RDY6			NAND_FMT_RD_SCMD_SBYTE7

///< Nand command registers
norm_ps_data ncb_ndcu_cmd_regs_t ncb_ndcu_cmd_regs = {	///< NDCU registers settings
	// 0xC0002060
	.nf_rcmd_reg00 = {
		.b.rcmd0 = NAND_READ_PAGE_CMD_CYC1,
		.b.rcmd1 = NAND_READ_PAGE_CMD_CYC1,
		.b.rcmd2 = NAND_TSB_XNOR_CB,
		.b.rcmd3 = NAND_TSB_XNOR_3F,
	},
	// 0xC0002064
	.nf_rcmd_reg01 = {
		.b.rcmd4 = NAND_READ_PARAM,
		.b.rcmd6 = NAND_GET_FEATURES,
		.b.rcmd7 = NAND_MU_GET_LUN_FEATURE,
	},
	// 0xC0002068
	.nf_rcmd_reg10 = {
		.b.rcmd8 = NAND_READ_PAGE_CMD_CYC2,
		.b.rcmd9 = NAND_READ_PAGE_CMD_CYC2,
		.b.rcmd10 = NAND_READ_PAGE_SNAP_CMD_CYC2,
		.b.rcmd11 = NAND_READ_PAGE_CACHE_RDM_CMD_CYC2,
	},
	// 0xC000206C
	.nf_rcmd_reg11 = {
		.b.rcmd12 = NAND_READ_PAGE_CACHE_LST_CMD_CYC1,
		.b.rcmd13 = NAND_COPYBACK_READ_CMD_CYC2,
		.b.rcmd14 = NAND_TSB_SBN_READ,
	},
	// 0xC0002070
	.nf_rcmd_mp_reg00 = {
		.b.rcmd0_mp = NAND_READ_PAGE_CMD_CYC1,
	},
	// 0xC0002074
	.nf_rcmd_mp_reg01 = {
		.b.rcmd4_mp = NAND_READ_PAGE_MP_CMD_CYC2,
		.b.rcmd5_mp = 0x60,// for secondary 60h-row-60h-row-30h mp read
	},
	// 0xC0002078
	.nf_rcmd_mp_reg10 = {
		.all = 0x00313032,//keep default value
	},
	// 0xC000207C
	.nf_rcmd_mp_reg11 = {
		.all = 0x30323232,//keep default value
	},
	// 0xC0002080
	.nf_rxcmd_reg0 = {
		.b.crc_cmd0 = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
		.b.crc_cmd1 = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
		.b.crc_cmd2 = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
		.b.crc_cmd3 = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
	},
	// 0xC0002084
	.nf_rxcmd_reg1 = {
		.b.crc_cmd4 = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
		.b.crc_cmd5 = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
		.b.crc_cmd6 = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
		.b.crc_cmd7 = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
	},
	// 0xC0002088
	.nf_rxcmd_mp_reg0 = {
		.b.crc_cmd0_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
		.b.crc_cmd1_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
		.b.crc_cmd2_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
		.b.crc_cmd3_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC1,
	},
	// 0xC000208C
	.nf_rxcmd_mp_reg1 = {
		.b.crc_cmd4_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
		.b.crc_cmd5_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
		.b.crc_cmd6_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
		.b.crc_cmd7_mp = NAND_CHANGE_READ_COLUMN_CMD_CYC2,
	},

	// 0xC0002090
	.nf_rdcmd_dummy = {
		.all = 0xE1000000,//keep default value
	},
	// 0xC0002094
	.nf_pcmd_reg00 = {
		.b.pcmd0 = NAND_PROGRAM_PAGE_CMD_CYC1,
		.b.pcmd1 = NAND_PROGRAM_PAGE_CMD_CYC1,
		.b.pcmd2 = NAND_COPYBACK_PROGRAM_CMD_CYC1,
		.b.pcmd3 = 0xC9, //SSR_READ prefix for vth tracking
	},
	// 0xC0002098
	.nf_pcmd_reg01 = {
		.b.pcmd4 = 0x81,//keep default value
		.b.pcmd5 = 0x00,//keep default value
		.b.pcmd6 = NAND_SET_FEATURES,
		.b.pcmd7 = NAND_MU_SET_LUN_FEATURE,
	},
	// 0xC000209C
	.nf_pcmd_reg10 = {
		.b.pcmd8 = NAND_PROGRAM_PAGE_CMD_CYC2,
		.b.pcmd9 = NAND_PROGRAM_PAGE_CMD_CYC2,
		.b.pcmd10 = NAND_PROGRAM_PAGE_CACHE_CMD_CYC2,
		.b.pcmd11 = NAND_PROGRAM_PAGE_TLC_CMD_CYC2,
	},
	// 0xC00020A0
	.nf_pcmd_reg11 = {
		.all = 0x10000000,//keep default value
	},
	// 0xC00020A4
	.nf_pcmd_mp_reg00 = {
		.b.pcmd0_mp = NAND_PROGRAM_PAGE_CMD_CYC1,
		.b.pcmd1_mp = NAND_COPYBACK_PROGRAM_CMD_CYC1,
	},
	// 0xC00020A8
	.nf_pcmd_mp_reg01 = {
		.b.pcmd4_mp = NAND_PROGRAM_PAGE_MP_CMD_CYC2,
	},
	// 0xC00020AC
	.nf_pcmd_mp_reg10 = {
		.all = 0x151A1211,//keep default value
	},
	// 0xC00020B0
	.nf_pcmd_mp_reg11 = {
		.all = 0x10111111,//keep default value
	},
	// 0xC00020B4
	.nf_ecmd_reg0 = {
		.b.ecmd0 = NAND_ERASE_BLOCK_CMD_CYC1,
		.b.ecmd1 = NAND_ERASE_BLOCK_CMD_CYC1,
		.b.ecmd2 = NAND_DEV_RESET,
	},
	// 0xC00020B8
	.nf_ecmd_reg1 = {
		.b.ecmd4 = NAND_ERASE_BLOCK_CMD_CYC2,
		.b.ecmd5 = NAND_ERASE_BLOCK_CMD_CYC2,
	},
	// 0xC00020BC
	.nf_ecmd_mp_reg0 = {
		.b.ecmd0_mp = NAND_ERASE_BLOCK_CMD_CYC1,
	},
	// 0xC00020C0
	.nf_ecmd_mp_reg1 = {
		.b.ecmd4_mp = NAND_ERASE_BLOCK_MP_CMD_CYC2,
	},
	// 0xC00020C4
	.nf_ecmd_jdc0 = {
		.b.ecmd0_jdec = 0x60,	//keep default value
		.b.ecmd1_jdec = 0xFA,	// Erase suspend
		.b.ecmd2_jdec = 0x00,	//keep default value
		.b.ecmd3_jdec = 0x27,	// Erase resume
	},
	// 0xC00020C8
	.nf_scmd_reg0 = {
		.b.scmd0 = NAND_READ_STATUS_70,
		.b.scmd1 = NAND_READ_STATUS_70,
		.b.scmd2 = NAND_READ_STATUS_F1,
		.b.scmd3 = NAND_READ_STATUS_F2,
	},
	// 0xC00020CC
	.nf_scmd_reg1 = {
		.b.scmd4 = 0xF8,//keep default value
		.b.scmd5 = NAND_READ_STATUS_78,
		.b.scmd6 = NAND_READ_STATUS_78,
		.b.scmd7 = NAND_READ_STATUS_78,
	},
	// 0xC00020D0
	.nf_rstcmd_reg0 = {
		.b.rst_cmd0 = 0xFF,	//keep default value
		.b.rst_cmd1 = 0xA7,	// Program suspend
		.b.rst_cmd2 = 0xFD,	//keep default value
		.b.rst_cmd3 = 0xFA,	//keep default value
	},
	// 0xC00020D4
	.nf_rstcmd_reg1 = {
		.b.rst_cmd5 = 0x48,	// Program resume
	},
	// 0xC00020D8
	.nf_precmd_reg00 = {
		.b.precmd0 = NAND_0D_PREFIX,
#if !QLC_SUPPORT
		.b.precmd1 = NAND_07_PREFIX,
#else
		.b.precmd1 = NAND_5D_PREFIX,
#endif
		.b.precmd2 = NAND_36_PREFIX,//"M2CMI19-036 BiCS FLASH Gen4 TLC Fast Read Sequence Rev0.1.pdf" in http://10.10.0.17/issues/4780#change-31842
		.b.precmd3 = NAND_C2_PREFIX,
	},
	// 0xC00020DC
	.nf_precmd_reg01 = {
	    .b.precmd4 = NAND_26_PREFIX,
	},
	// 0xC00020e0
	.nf_precmd_reg10 = {
#if TSB_XL_NAND
		.b.precmd8 = NAND_36_PREFIX,// Fast read
#else
		.b.precmd8 = NAND_SLC_PREFIX,
#endif
		.b.precmd9 = NAND_LOW_PREFIX,
		.b.precmd10 = NAND_MID_PREFIX,
		.b.precmd11 = NAND_UPR_PREFIX,
	},
	// 0xC00020e4
	.nf_precmd_reg11 = {
		.b.precmd12 = NAND_TOP_PREFIX,
	},
	// 0xC00020e8
	.nf_precmd_reg20 = {
		.all = 0xFAFCFDFF,//keep default value
	},
	// 0xC00020ec
	.nf_precmd_reg21 = {
	},
};

///< Nand ready/busy and pass/fail registers
norm_ps_data ncb_ndcu_rdy_fail_regs_t ncb_ndcu_rdy_fail_regs = {	///< NDCU Ready/busy and pass/fail registers settings
	// 0xC0002108
	.nf_fail_reg0 = {
		.b.fail_bit_chk0 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk01 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk02 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk03 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk04 = FAIL_BIT(2) | FAIL_BIT(0),
	},
	// 0xC000210C
	.nf_fail_reg1 = {
		.b.fail_bit_chk1 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk11 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk12 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk13 = FAIL_BIT(2) | FAIL_BIT(0),
		.b.fail_bit_chk14 = FAIL_BIT(2) | FAIL_BIT(0),
	},
	// 0xC0002110, ignore failure bit for SBYTE6
	.nf_fail_reg2 = {
		.b.fail_bit_chk2 = FAIL_BIT(0),
		.b.fail_bit_chk21 = FAIL_BIT(0),
		.b.fail_bit_chk22 = FAIL_BIT(0),
		.b.fail_bit_chk23 = FAIL_BIT(0),
		.b.fail_bit_chk24 = FAIL_BIT(0),
	},
	// 0xC0002114
	.nf_fail_reg3 = {
		.b.fail_bit_chk3 = FAIL_BIT(0),
		.b.fail_bit_chk31 = FAIL_BIT(0),
		.b.fail_bit_chk32 = FAIL_BIT(0),
		.b.fail_bit_chk33 = FAIL_BIT(0),
		.b.fail_bit_chk34 = FAIL_BIT(0),
	},
	// 0xC0002118
	.nf_fail_reg4 = {
		.b.fail_bit_chk4 = FAIL_BIT(2),
		.b.fail_bit_chk41 = FAIL_BIT(2),
		.b.fail_bit_chk42 = FAIL_BIT(2),
		.b.fail_bit_chk43 = FAIL_BIT(2),
		.b.fail_bit_chk44 = FAIL_BIT(2),
	},
	// 0xC0002180
	.nf_fail_reg5 = {
		.b.fail_bit_chk5 = FAIL_BIT(2),
		.b.fail_bit_chk51 = FAIL_BIT(2),
		.b.fail_bit_chk52 = FAIL_BIT(2),
		.b.fail_bit_chk53 = FAIL_BIT(2),
		.b.fail_bit_chk54 = FAIL_BIT(2),
	},
	// 0xC0002184
	.nf_fail_reg6 = {
		.all = 0,
	},
	// 0xC0002188
	.nf_fail_reg7 = {
		.all = 0,
	},
	// 0xC000211C
	.nf_rdy_reg0 = {
		.b.rdy_bit_chk0 = RDY_BIT(6) | RDY_BIT(5),
		.b.rdy_bit_chk1 = RDY_BIT(6),
		.b.rdy_bit_chk2 = RDY_BIT(6) | RDY_BIT(5),
		.b.rdy_bit_chk3 = RDY_BIT(6),
		.b.rdy_bit_chk4 = RDY_BIT(6) | RDY_BIT(5),
		.b.rdy_bit_chk5 = RDY_BIT(6),
		.b.rdy_bit_chk6 = RDY_BIT(6) | RDY_BIT(5),
		.b.rdy_bit_chk7 = RDY_BIT(6),
	},
};

///< Nand single plane pass/fail register
norm_ps_data nf_fail_reg_sp_t nf_fail_reg_sp = {	///< NDCU pass/fail registers setting for single-plane operation
	.b.fail_bit_chk0_sp = FAIL_BIT(2) | FAIL_BIT(0),
	.b.fail_bit_chk1_sp = FAIL_BIT(2) | FAIL_BIT(0),
	.b.fail_bit_chk2_sp = FAIL_BIT(0),
	.b.fail_bit_chk3_sp = FAIL_BIT(0),
	.b.fail_bit_chk4_sp = FAIL_BIT(2),
	.b.fail_bit_chk5_sp = FAIL_BIT(2),
	.b.fail_bit_chk6_sp = 0,
	.b.fail_bit_chk7_sp = 0,
};

///< Nand command format array
norm_ps_data ncmd_fmt_t ncmd_fmt_array[FINST_NAND_FMT_MAX] = {	///< Nand command format settings
	// 0 FINST_NAND_FMT_AUTO placeholder
	{
	},
#if !QLC_SUPPORT
	// 1 FINST_NAND_FMT_TLC_ERASE
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_3B_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_ERASE_JEDEC,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 2 FINST_NAND_FMT_SLC_ERASE
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_3B_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
#if !TSB_XL_NAND
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
#endif
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_ERASE_JEDEC,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL2,
		},
	},
	// 3 FINST_NAND_FMT_SLC_PROG
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
#if !TSB_XL_NAND
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
#endif
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL2,
		},
	},
	// 4 FINST_NAND_FMT_SLC_CACHE_PROG
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_1,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6_FAIL2,
		},
	},
	// 5 FINST_NAND_FMT_SLC_PROG_FOR_RANDOM
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 6 FINST_NAND_FMT_SLC_RANDOM_PROG
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL2,
		},
	},
	// 7 FINST_NAND_FMT_SLC_READ_CMD
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 8 FINST_NAND_FMT_READ_DATA
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 9 FINST_NAND_FMT_SLC_CACHE_READ_CMD
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 10 FINST_NAND_FMT_CACHE_READ_END_CMD
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
			.b.prefix_cmd2  = NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode  = NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext      = NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_IDX_3 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp       = NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 11 FINST_NAND_FMT_SLC_CB_READ_CMD
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_4 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 12 FINST_NAND_FMT_SLC_4K_READ
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 13 FINST_NAND_FMT_READ_STATUS
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 14 FINST_NAND_FMT_READ_STATUS_ENH
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_3B_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 15 FINST_NAND_FMT_READ_STATUS_FN
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 16 FINST_NAND_FMT_FEATURE
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_1B_COL,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_5 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
			//.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 17 FINST_NAND_FMT_FEATURE_LUN
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_2B_COL,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_6 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
			//.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 18 FINST_NAND_FMT_RESET
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
			.b.cmd_ext      = NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_NO,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 19 FINST_NAND_FMT_READ_PARAM_PAGE
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_1B_COL,
			.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
			.b.cmd_ext      = NAND_FMT_1ST_CMD_IDX_3 | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_06_E0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 20 FINST_NAND_FMT_XLC_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 21 FINST_NAND_FMT_XLC_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 22 FINST_NAND_FMT_XLC_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 23 FINST_NAND_FMT_XLC_READ_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_00,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 24 FINST_NAND_FMT_XLC_CACHE_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 25 FINST_NAND_FMT_XLC_CACHE_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 26 FINST_NAND_FMT_XLC_CACHE_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 27 FINST_NAND_FMT_XLC_CACHE_READ_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_00,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 28 FINST_NAND_FMT_XLC_PROG_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 29 FINST_NAND_FMT_XLC_PROG_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 30 FINST_NAND_FMT_XLC_PROG_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6_FAIL0,  // adams
		},
	},
	// 31 FINST_NAND_FMT_XLC_PROG_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 32 FINST_NAND_FMT_XLC_CACHE_PROG_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_1,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6_FAIL02,
		},
	},
	// 33 FINST_NAND_FMT_XLC_PROG_LOW_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 34 FINST_NAND_FMT_XLC_PROG_MID_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 35 FINST_NAND_FMT_XLC_PROG_UPR_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 36 FINST_NAND_FMT_XLC_PROG_TOP_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 37 FINST_NAND_FMT_XLC_CB_PROG_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 38 FINST_NAND_FMT_XLC_CB_PROG_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 39 FINST_NAND_FMT_XLC_CB_PROG_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 40 FINST_NAND_FMT_XLC_CB_PROG_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 41 FINST_NAND_FMT_XLC_CB_PROG_LOW_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 42 FINST_NAND_FMT_XLC_CB_PROG_MID_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 43 FINST_NAND_FMT_XLC_CB_PROG_UPR_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 44 FINST_NAND_FMT_XLC_CB_PROG_TOP_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 45 FINST_NAND_FMT_SLC_SOFT_BIT_SBN_READ
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,

			.b.prefix_cmd1  = NAND_FMT_PREFIX_CMD3,    //tony 20201208

			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			//.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,  //tony 20201215
			.b.cmd_ext  = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,

			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 46 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,

			.b.prefix_cmd1  = NAND_FMT_PREFIX_CMD3,   //tony 20201208

			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
            //.b.cmd_ext    = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,  //tony 20201215
            .b.cmd_ext  = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,

			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 47 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,

            .b.prefix_cmd1  = NAND_FMT_PREFIX_CMD3,   //tony 20201208

			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
            //.b.cmd_ext    = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,  //tony 20201215
            .b.cmd_ext  = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,

			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 48 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,

            .b.prefix_cmd1  = NAND_FMT_PREFIX_CMD3,   //tony 20201208

			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
            //.b.cmd_ext    = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,  //tony 20201215
            .b.cmd_ext  = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,

			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 49 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 50 FINST_NAND_FMT_XNOR_SB_CMD1
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 51 FINST_NAND_FMT_XNOR_SB_CMD2
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_2 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 52 FINST_NAND_FMT_LUN0_READ_STATUS
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 53 FINST_NAND_FMT_LUN1_READ_STATUS
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 54 FINST_NAND_FMT_SLC_READ_CMD_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 55 FINST_NAND_FMT_XLC_READ_LOW_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 56 FINST_NAND_FMT_XLC_READ_MID_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 57 FINST_NAND_FMT_XLC_READ_UPR_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 58 FINST_NAND_FMT_XLC_READ_TOP_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_00,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 59 FINST_NAND_FMT_XLC_SELF_ADJUST_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 60 FINST_NAND_FMT_XLC_SELF_ADJUST_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 61 FINST_NAND_FMT_XLC_SELF_ADJUST_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 62 FINST_NAND_FMT_GET_TEMPERATURE
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_4 | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_00,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// Jamie 20210105 Refresh Read w/o 0x36
	// 63 FINST_NAND_FMT_REFRESH_READ_SLC
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 64 FINST_NAND_FMT_REFRESH_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 65 FINST_NAND_FMT_REFRESH_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 66 FINST_NAND_FMT_REFRESH_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// Jamie 20210105 Refresh Read w/o 0x36 //
	// 67
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_4B_ROW,
		    .b.nf_addr_4b_en = NAND_FMT_4B_DIS,
		    .b.cmd_ext      = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_06_E0,
		},
	},
	// 68
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_1B_COL,
    		.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
    		.b.cmd_ext		= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_NO,
		},
	},
	// 69
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_3B_ROW,
    		.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
    		.b.cmd_ext		= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_06_E0,
		},
	},
	// 70 SSR prefix cmd for vth tracking
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_1B_COL,
            .b.nf_addr_4b_en    = NAND_FMT_4B_DIS,
            .b.cmd_ext  = NAND_FMT_1ST_CMD_IDX_2 | NAND_FMT_2ND_CMD_NO,
            .b.cmd_mp   = NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
            //.b.rd_scmd_mode = SCMD_RDY56,
            //.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 71 SSR_READ for vth tracking
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_5B_COL_ROW,
            .b.nf_addr_4b_en    = NAND_FMT_4B_DIS,
            .b.prefix_cmd1  = 0,
            .b.prefix_cmd2  = 0,
            .b.prefix_cmd3  = 0,
            .b.prefix_mode  = NAND_FMT_PREFIX_MODE_ALL_PLN,
            .b.cmd_ext  = NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
            .b.cmd_mp   = NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
            .b.rd_scmd_mode = SCMD_RDY56,
		},
	},
    // 72 FINST_NAND_FMT_XLC_LNA_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1  = NAND_FMT_PREFIX_CMD4,   
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 73 FINST_NAND_FMT_XLC_LNA_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1  = NAND_FMT_PREFIX_CMD4,   
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 74 FINST_NAND_FMT_XLC_LNA_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1  = NAND_FMT_PREFIX_CMD4,   
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 75 FINST_NAND_FMT_SLC_LNA_READ
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1  = NAND_FMT_PREFIX_CMD4, 
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
#if (Synology_case)
	// 76 FINST_NAND_FMT_XLC_GC_PROG_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6_FAIL0,  // adams			
		},
	},
#endif
#else
	// 1 FINST_NAND_FMT_TLC_ERASE
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_3B_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_ERASE_JEDEC,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 2 FINST_NAND_FMT_SLC_ERASE
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_3B_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_ERASE_JEDEC,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL2,
		},
	},
	// 3 FINST_NAND_FMT_SLC_PROG
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL2,
		},
	},
	// 4 FINST_NAND_FMT_SLC_CACHE_PROG
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_1,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL2,
		},
	},
	// 5 FINST_NAND_FMT_SLC_PROG_FOR_RANDOM
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 6 FINST_NAND_FMT_SLC_RANDOM_PROG
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL2,
		},
	},
	// 7 FINST_NAND_FMT_SLC_READ_CMD
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 8 FINST_NAND_FMT_READ_DATA
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 9 FINST_NAND_FMT_SLC_CACHE_READ_CMD
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_00,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 10 FINST_NAND_FMT_CACHE_READ_END_CMD
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
			.b.prefix_cmd2  = NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode  = NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext      = NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_IDX_3 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp       = NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 11 FINST_NAND_FMT_SLC_CB_READ_CMD
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_4 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 12 FINST_NAND_FMT_SLC_4K_READ
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 13 FINST_NAND_FMT_READ_STATUS
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 14 FINST_NAND_FMT_READ_STATUS_ENH
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_3B_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 15 FINST_NAND_FMT_READ_STATUS_FN
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 16 FINST_NAND_FMT_FEATURE
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_1B_COL,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_5 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
			//.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 17 FINST_NAND_FMT_FEATURE_LUN
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_2B_COL,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_6 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
			//.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 18 FINST_NAND_FMT_RESET
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
			.b.cmd_ext      = NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_NO,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 19 FINST_NAND_FMT_READ_PARAM_PAGE
	{
		.fmt = {
			.b.nf_addr_num  = NAND_FMT_ADDR_NUM_1B_COL,
			.b.nf_addr_4b_en = NAND_FMT_4B_DIS,
			.b.cmd_ext      = NAND_FMT_1ST_CMD_IDX_3 | NAND_FMT_2ND_CMD_NO | NAND_FMT_READ_CRC_06_E0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 20 FINST_NAND_FMT_XLC_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 21 FINST_NAND_FMT_XLC_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 22 FINST_NAND_FMT_XLC_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 23 FINST_NAND_FMT_XLC_READ_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 24 FINST_NAND_FMT_XLC_CACHE_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 25 FINST_NAND_FMT_XLC_CACHE_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 26 FINST_NAND_FMT_XLC_CACHE_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},
	// 27 FINST_NAND_FMT_XLC_CACHE_READ_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6,
		},
	},

	// 28 FINST_NAND_FMT_XLC_PROG_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 29 FINST_NAND_FMT_XLC_PROG_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 30 FINST_NAND_FMT_XLC_PROG_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 31 FINST_NAND_FMT_XLC_PROG_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 32 FINST_NAND_FMT_XLC_CACHE_PROG_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY6_FAIL0,
		},
	},
	// 33 FINST_NAND_FMT_XLC_PROG_LOW_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 34 FINST_NAND_FMT_XLC_PROG_MID_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 35 FINST_NAND_FMT_XLC_PROG_UPR_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 36 FINST_NAND_FMT_XLC_PROG_TOP_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 37 FINST_NAND_FMT_XLC_CB_PROG_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 38 FINST_NAND_FMT_XLC_CB_PROG_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 39 FINST_NAND_FMT_XLC_CB_PROG_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 40 FINST_NAND_FMT_XLC_CB_PROG_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 41 FINST_NAND_FMT_XLC_CB_PROG_LOW_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 42 FINST_NAND_FMT_XLC_CB_PROG_MID_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 43 FINST_NAND_FMT_XLC_CB_PROG_UPR_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_2,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 44 FINST_NAND_FMT_XLC_CB_PROG_TOP_1ST
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_IDX_0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_1 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL0,
		},
	},
	// 45 FINST_NAND_FMT_SLC_SOFT_BIT_SBN_READ
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 46 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 47 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_MID
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 48 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_UPR
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 49 FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_TOP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_5 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 50 FINST_NAND_FMT_XNOR_SB_CMD1
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_1 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 51 FINST_NAND_FMT_XNOR_SB_CMD2
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_2 | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 52 FINST_NAND_FMT_LUN0_READ_STATUS
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 53 FINST_NAND_FMT_LUN1_READ_STATUS
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_NO_ADR,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_NO | NAND_FMT_2ND_CMD_NO,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56_FAIL02,
		},
	},
	// 54 FINST_NAND_FMT_SLC_READ_CMD_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 55 FINST_NAND_FMT_XLC_READ_LOW_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 56 FINST_NAND_FMT_XLC_READ_MID_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 57 FINST_NAND_FMT_XLC_READ_UPR_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 58 FINST_NAND_FMT_XLC_READ_TOP_MP
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 59 FINST_NAND_FMT_XLC_REREAD_SLC,
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD0,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_FIRST_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 60 FINST_NAND_FMT_XLC_REREAD_LOW,
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 61 FINST_NAND_FMT_XLC_REREAD_MID,
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD2,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 62 FINST_NAND_FMT_XLC_REREAD_UPR,
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD3,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
	// 63 FINST_NAND_FMT_XLC_REREAD_TOP,
	{
		.fmt = {
			.b.nf_addr_num	= NAND_FMT_ADDR_NUM_5B_COL_ROW,
			.b.nf_addr_4b_en	= NAND_FMT_4B_DIS,
			.b.prefix_cmd1	= NAND_FMT_PREFIX_CMD1,
			.b.prefix_cmd2	= NAND_FMT_PREFIX_CMD4,
			.b.prefix_mode	= NAND_FMT_PREFIX_MODE_ALL_PLN,
			.b.cmd_ext	= NAND_FMT_1ST_CMD_IDX_0 | NAND_FMT_2ND_CMD_IDX_0 | NAND_FMT_READ_CRC_06_E0,
			.b.cmd_mp	= NAND_FMT_NON_FIRST_PL_IDX_0 | NAND_FMT_NON_LAST_PL_IDX_0,
			.b.rd_scmd_mode = SCMD_RDY56,
		},
	},
#endif
};

norm_ps_data struct ncb_vsc_format vsc_template_array[] =
{
	{
		.dw0 = {//0x00000064
			.b.cmd1 = NAND_CMD_WRITE_DQ_TRAINING_TX_READ,
			.b.cmd2 = 0,
			.b.rd_scmd = 0,//read status command
			.b.poll_fuse = 0,
#if TACOMAX
			.b.rsvd = 0,
#endif
		},
		.dw1 = {//0x80008401
			.b.addr_num = NAND_FMT_ADDR_NUM_1B_COL,
			.b.addr_4b_en = 0,
			.b.wcnt_sel = 0,
			.b.pcnt_sel = VSC_NO_POLL,
			.b.cmd_enh = 1,
			.b.zero_xfer = 1,
			.b.pre_cmd1 = NAND_FMT_PREFIX_NO,
			.b.pre_cmd2 = NAND_FMT_PREFIX_NO,
			.b.pre_cmd3 = NAND_FMT_PREFIX_NO,
			.b.prefix_mode = 0,
			.b.vsc_rd_buff_only = 0,
			.b.ce_fuse = 1,
		},
	},
	{
		.dw0 = {//0x00000000
			.b.cmd1 = 0,
			.b.cmd2 = 0,
			.b.rd_scmd = 0,//read status command
			.b.poll_fuse = 0,
#if TACOMAX
			.b.rsvd = 0,
#endif
		},
		.dw1 = {//0x40000000
			.b.addr_num = NAND_FMT_ADDR_NUM_NO_ADR,
			.b.addr_4b_en = 0,
			.b.wcnt_sel = 0,
			.b.pcnt_sel = VSC_NO_POLL,
			.b.cmd_enh = 0,
			.b.zero_xfer = 0,
			.b.pre_cmd1 = NAND_FMT_PREFIX_NO,
			.b.pre_cmd2 = NAND_FMT_PREFIX_NO,
			.b.pre_cmd3 = NAND_FMT_PREFIX_NO,
			.b.prefix_mode = 0,
			.b.vsc_rd_buff_only = 1,
			.b.ce_fuse = 0,
		},
	},
};
/*!
 * @brief Get low/middle/upper page type from nand command format
 * Used in error handling to know low, middle or upper page occurs error.
 * Because toggle nand low, middle, upper page has same row address
 *
 * @param ndcmd_fmt_idx	nand command format index
 *
 * @return	low, middle or upper page
 */
fast_code u8 ndcu_get_xlc_page_index(int ndcmd_fmt_idx)
{
	switch (ndcmd_fmt_idx) {
	case FINST_NAND_FMT_TLC_ERASE:
		return 0;
	case FINST_NAND_FMT_XLC_READ_LOW:
	case FINST_NAND_FMT_XLC_READ_MID:
	case FINST_NAND_FMT_XLC_READ_UPR:
	case FINST_NAND_FMT_XLC_READ_TOP:
		return ndcmd_fmt_idx - FINST_NAND_FMT_XLC_READ_LOW;
#if (Synology_case)
	case FINST_NAND_FMT_XLC_GC_PROG_UPR:
		return 2;
#endif
	case FINST_NAND_FMT_XLC_READ_LOW_MP:
	case FINST_NAND_FMT_XLC_READ_MID_MP:
	case FINST_NAND_FMT_XLC_READ_UPR_MP:
	case FINST_NAND_FMT_XLC_READ_TOP_MP:
		return ndcmd_fmt_idx - FINST_NAND_FMT_XLC_READ_LOW_MP;
	case FINST_NAND_FMT_XLC_PROG_LOW:
	case FINST_NAND_FMT_XLC_PROG_MID:
	case FINST_NAND_FMT_XLC_PROG_UPR:
	case FINST_NAND_FMT_XLC_PROG_TOP:
		return ndcmd_fmt_idx - FINST_NAND_FMT_XLC_PROG_LOW;
	#if OPEN_ROW == 1
	case FINST_NAND_FMT_READ_DATA:
		return 0;
	#endif
	case FINST_NAND_FMT_REFRESH_READ_LOW:
	case FINST_NAND_FMT_REFRESH_READ_MID:
	case FINST_NAND_FMT_REFRESH_READ_UPR:
		return ndcmd_fmt_idx - FINST_NAND_FMT_REFRESH_READ_LOW;
    case FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW:
	case FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_MID:
	case FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_UPR:
		return ndcmd_fmt_idx - FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW;
    case FINST_NAND_FMT_XLC_LNA_READ_LOW:
	case FINST_NAND_FMT_XLC_LNA_READ_MID:
	case FINST_NAND_FMT_XLC_LNA_READ_UPR:
		return ndcmd_fmt_idx - FINST_NAND_FMT_XLC_LNA_READ_LOW;
	default:
		sys_assert(0);
	}
	return 0;
}

/*!
 * @brief Temp modify SLC program ndcmd fmt to 80h-5addr-data to do DCC training write
 *
 * @return	not used
 */
fast_code void tsb_dcc_write_change(void)
{
	nf_ncmd_fmt_ptr_t reg_ptr;
	nf_ncmd_fmt_reg0_t reg_val;
	reg_ptr.all = 0;

	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_SLC_PROG;
	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
	reg_val.all = ndcu_readl(NF_NCMD_FMT_REG0);
	reg_val.b.prefix_cmd2 = NAND_FMT_PREFIX_NO;
	reg_val.b.cmd_ext &= ~NAND_FMT_2ND_CMD_IDX_6;
	ndcu_writel(reg_val.all, NF_NCMD_FMT_REG0);
}

/*!
 * @brief Restore SLC program ndcmd fmt after DCC training write
 *
 * @return	not used
 */
fast_code void tsb_dcc_write_restore(void)
{
	nf_ncmd_fmt_ptr_t reg_ptr;
	reg_ptr.all = 0;
	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_SLC_PROG;
	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
	ndcu_writel(ncmd_fmt_array[FINST_NAND_FMT_SLC_PROG].fmt.all, NF_NCMD_FMT_REG0);
}

/*!
 * @brief nand command format adjustment after identifing nand model
 *
 * @return	not used
 */
fast_code void ndcu_ndcmd_format_adjust(void)
{
	if (nand_support_aipr()) {
		// Use 78h instead of Fnh to poll accurate ready/busy of a plane
		ncmd_fmt_array[FINST_NAND_FMT_SLC_READ_CMD].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_LOW].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_MID].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_UPR].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_TOP].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_SLC_4K_READ].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;

		ncmd_fmt_array[FINST_NAND_FMT_SLC_READ_CMD_MP].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_LOW_MP].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_MID_MP].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_UPR_MP].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_TOP_MP].fmt.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE7;
	}
	// BiCS4 support fast read with 36h prefix, tR is shorter, but only support SP read
	if (nand_is_bics4()) {
		ncmd_fmt_array[FINST_NAND_FMT_SLC_READ_CMD].fmt.b.prefix_cmd1 = NAND_FMT_PREFIX_CMD2;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_LOW].fmt.b.prefix_cmd1 = NAND_FMT_PREFIX_CMD2;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_MID].fmt.b.prefix_cmd1 = NAND_FMT_PREFIX_CMD2;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_UPR].fmt.b.prefix_cmd1 = NAND_FMT_PREFIX_CMD2;
		ncmd_fmt_array[FINST_NAND_FMT_XLC_READ_TOP].fmt.b.prefix_cmd1 = NAND_FMT_PREFIX_CMD2;
	}
}

/*!
 * @brief Set wcnt busy time for important nand command format
 *
 * @return	not used
 */
//fast_code void ndcu_nand_cmd_format_init(void)
slow_code void ndcu_nand_cmd_format_init(void)
{
	extern void set_busy_time(u32 ndcmd_fmt, u32 op_type, u32 wcnt_sel, u32 tbusy);

	// Erase
	set_busy_time(FINST_NAND_FMT_TLC_ERASE, FINST_TYPE_ERASE, 1, 2000);
	set_busy_time(FINST_NAND_FMT_SLC_ERASE, FINST_TYPE_ERASE, 2, 2000);

	// Program
	set_busy_time(FINST_NAND_FMT_SLC_PROG, FINST_TYPE_PROG, 3, 100);
	set_busy_time(FINST_NAND_FMT_SLC_CACHE_PROG, FINST_TYPE_PROG, 4, 100);
	set_busy_time(FINST_NAND_FMT_XLC_PROG_LOW, FINST_TYPE_PROG, 5, 0);
	set_busy_time(FINST_NAND_FMT_XLC_PROG_MID, FINST_TYPE_PROG, 6, 0);
	if (nand_is_bics4_800mts() || nand_is_bics4_TH58TFT2T23BA8J())
    	set_busy_time(FINST_NAND_FMT_XLC_PROG_UPR, FINST_TYPE_PROG, 7, 2200);  // 512Gb
	else{
#ifdef MDOT2_SUPPORT
			if(nand_is_bics5_TH58LKT2Y45BA8H())
			{
  #if (Synology_case)
				set_busy_time(FINST_NAND_FMT_XLC_PROG_UPR, FINST_TYPE_PROG, 7, 4200); // 2T
				set_busy_time(FINST_NAND_FMT_XLC_GC_PROG_UPR, FINST_TYPE_PROG, 13, 3700);
  #else
				set_busy_time(FINST_NAND_FMT_XLC_PROG_UPR, FINST_TYPE_PROG, 7, 2000); // 2T
				//set_busy_time(FINST_NAND_FMT_XLC_GC_PROG_UPR, FINST_TYPE_PROG, 13, 2000);
  #endif
			}else
#endif
			{
				set_busy_time(FINST_NAND_FMT_XLC_PROG_UPR, FINST_TYPE_PROG, 7, 1700);
#if (Synology_case)
				set_busy_time(FINST_NAND_FMT_XLC_GC_PROG_UPR, FINST_TYPE_PROG, 13, 1700);
#endif
			}
	}
	set_busy_time(FINST_NAND_FMT_XLC_CACHE_PROG_UPR, FINST_TYPE_PROG, 8, 800);  //Sean_temp

	// read
	set_busy_time(FINST_NAND_FMT_SLC_READ_CMD, FINST_TYPE_READ, 9, 10);
	set_busy_time(FINST_NAND_FMT_XLC_READ_LOW, FINST_TYPE_READ, 10, 8);  //Bics5 tR 35/65/65, tune lower page wcnt 40-> 32 us
	set_busy_time(FINST_NAND_FMT_XLC_READ_MID, FINST_TYPE_READ, 11, 15);
	set_busy_time(FINST_NAND_FMT_XLC_READ_UPR, FINST_TYPE_READ, 12, 15);
}

init_code void ficu_vsc_template_init(void)
{
	u32 idx = 0;
	u32 i, cnt;
	cnt = sizeof(vsc_template_array) / sizeof(vsc_template_array[0]);
	sys_assert(cnt < FSPM_VSC_TEMPLATE_COUNT);
	for (i = 0; i < cnt; i++) {
		ficu_vsc_tmpl_info_conf_reg_t info_conf_reg;
		ficu_vsc_tmpl_info_reg_t	  info_reg;
		info_conf_reg.all = ficu_readl(FICU_VSC_TMPL_INFO_CONF_REG);
		info_conf_reg.b.ficu_vsc_tmpl_info_conf_ptr = idx;
		ficu_writel(info_conf_reg.all, FICU_VSC_TMPL_INFO_CONF_REG);
		info_reg.all = ficu_readl(FICU_VSC_TMPL_INFO_REG);//0xC00010C4 set Vsc num and start address
		info_reg.b.ficu_vsc_tmpl_st_addr   = i * sizeof(struct ncb_vsc_format) + (u32)fspm_usage_ptr->vsc_template - (u32)fspm_usage_ptr;//offset base fspm address
		if (idx == FINST_NAND_FMT_VSC_WRITE_DQ_TRAINING_TX_READ) {
			info_reg.b.ficu_vsc_tmpl_finst_num = 2;
			i++;
		} else {
			info_reg.b.ficu_vsc_tmpl_finst_num = 1;
		}
		ficu_writel(info_reg.all, FICU_VSC_TMPL_INFO_REG);
		idx++;
	}
	memcpy(fspm_usage_ptr->vsc_template, &vsc_template_array, sizeof(vsc_template_array));
}

