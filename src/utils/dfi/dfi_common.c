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
/*! \file dfi_common.c
 * @brief dfi common APIs
 *
 * \addtogroup utils
 * \defgroup dfi
 * \ingroup utils
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "sect.h"
#include "dfi_init.h"
#include "dfi_config.h"
#include "dfi_common.h"
#include "dfi_reg.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "misc.h"
#include "misc_register.h"
#include "stdio.h"

#define __FILEID__ dficommon
#include "trace.h"
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Extern Data or Functions declaration:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
/*!
 * @brief delay function for nano second
 *
 * @param ns	delay nanosecond
 *
 * @return	not used
 */
norm_ps_code void ndelay(u32 ns)
{
	while (ns-- > 0)
		__asm__("");
}

/*!
 * @brief dfi syncronization
 *
 * @return	not used
 */
norm_ps_code void dfi_sync(void)
{
	sync_ctrl_0_t sync = { .all = dfi_readl(SYNC_CTRL_0), };

	sync.b.sync_rstn = 0; //Pull low sync rstn
	dfi_writel(sync.all, SYNC_CTRL_0);
	ndelay(5);

	sync.b.sync_rstn = 1; //Release sync rstn
	dfi_writel(sync.all, SYNC_CTRL_0);
	ndelay(5);

	sync.b.sync_cgs_en = 1; //Begin PHY Synchronization
	dfi_writel(sync.all, SYNC_CTRL_0);
	ndelay(20);

	sync.b.sync_cgs_en = 0; //Finish PHY Synchronization
	dfi_writel(sync.all, SYNC_CTRL_0);
}

/*!
 * @brief dfi DLL reset
 *
 * @return	not used
 */
norm_ps_code void dfi_dll_rst(void)
{
	dll_ctrl_0_t dll_ctrl_0 = { .all = dfi_readl(DLL_CTRL_0), };
	dll_ctrl_0.b.dll_rstb = 0; // assert DLL reset (active low)
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);
	ndelay(5);
	dll_ctrl_0.b.dll_rstb = 1; // deassert DLL reset
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);
}

/*!
 * @brief Polling sequence after MRx are send to confirm MRx compeletion
 *
 * @param ms	wait ms to wait
 *
 * @return	true to pulling success, else false
 */
norm_ps_code bool dfi_mc_smtq_cmd_done_poll_seq(u8 ms)
{
	u8 count_down = ms;
	u32 rdata;
	do {
		ndelay(1000);
		rdata = mc0_readl(MC_STATUS);
		if ((rdata & (~SMTQ_PENDING_REQ_MASK)) == 0x0ef71f1f)
			return true;
	} while (count_down-- != 0);

	return false;
}

/*!
 * @brief DDR Mode Register Read Write Sequence
 *
 * @param wr	MR_WRITE for write and MR_READ for read
 * @param reg	MR number
 * @param cs	chip select
 *
 * @return		not used
 */
norm_ps_code void dfi_mc_mr_rw_req_seq(u8 wr, u8 reg, u8 cs)
{
	//Reset DRAM_MODE_REG_CMD
	dram_mode_reg_cmd_t mode = { .all = 0, };
	mc0_writel(mode.all, DRAM_MODE_REG_CMD);

	if (wr)
		mode.b.mode_reg_write_req = 1;
	else
		mode.b.mode_reg_read_req = 1;
	mode.b.mode_reg_cs_sel = cs;
	mode.b.mode_reg_addr_sel = reg;
	mc0_writel(mode.all, DRAM_MODE_REG_CMD);

	dfi_mc_smtq_cmd_done_poll_seq(1);
}

/*!
 * @brief Sequence to put DDR3/4 in Multi Purpose Register (MPR) mode
 *
 * @param en	true to enable, false to disable
 *
 * @return	not used
 */
norm_ps_code void dfi_mc_ddrx_mpr_mode_seq(u8 cs, bool en)
{
	//Set MP_operation field to be sent to DRAM MPR
	device_config1_t config1 = { .all = mc0_readl(DEVICE_CONFIG1), };
	config1.b.mp_operation = en;
	mc0_writel(config1.all, DEVICE_CONFIG1);

	//Send MP_operation field to DRAM MPR
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR3, cs);
	//ndelay(1000);
}

/*!
 * @brief DDR3/4 Multi Purpose Register (MPR) Read sequence
 *
 * @param cs		chip select
 * @param mpr_page_sel	mpr page select
 * @param mpr_loc_sel	mpr location select
 *
 * @return	not used
 */
norm_ps_code void dfi_mc_ddrx_mpr_read_seq(u8 cs, u8 mpr_page_sel, u8 mpr_loc_sel)
{

	dram_calibration_cmd_t calibration = { .all = mc0_readl(
	DRAM_CALIBRATION_CMD), };
	calibration.all = 0;
	mc0_writel(calibration.all, DRAM_CALIBRATION_CMD);

	calibration.b.dram_calibration_cs_sel = cs;
	calibration.b.mpr_pg_read_req_for_cal = 1;
	calibration.b.mpr_pg_sel              = mpr_page_sel;
	calibration.b.mpr_ba_sel              = mpr_loc_sel;
	mc0_writel(calibration.all, DRAM_CALIBRATION_CMD);

	dfi_mc_smtq_cmd_done_poll_seq(1);
}

/*!
 * @brief LPDDR4 Multi Purpose Calibration (MPC) Read sequence
 *
 * @param cs	chip select
 *
 * @return	not used
 */
norm_ps_code void dfi_mc_lpddr4_mpc_read_seq(u8 cs)
{

	dram_calibration_cmd_t calibration = { .all = mc0_readl(DRAM_CALIBRATION_CMD), };
	calibration.all = 0;
	mc0_writel(calibration.all, DRAM_CALIBRATION_CMD);

	calibration.b.dram_calibration_cs_sel = cs;
	calibration.b.rd_dq_train_req = 1;
	mc0_writel(calibration.all, DRAM_CALIBRATION_CMD);

	dfi_mc_smtq_cmd_done_poll_seq(1);
}

/*!
 * @brief DFI read level read issue sequence
 *
 * @param cs		chip select
 * @param ddr_type	mc ddr type
 *
 * @return	not used
 */
norm_ps_code void dfi_mc_rdlvl_rd_issue_seq(u8 cs, u8 ddr_type)
{
	u8 mpr_page_sel = 0;
	u8 mpr_loc_sel = 0;

	//Send MPR read for DDRx
	if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4) {
		dfi_mc_ddrx_mpr_read_seq(cs, mpr_page_sel, mpr_loc_sel);
	} else if (ddr_type == MC_LPDDR4) {
		//Send MPC-1 (RD DQ DQ Calibraion read) for LPDDR4
		dfi_mc_lpddr4_mpc_read_seq(cs);
	}
}

/*!
 * @brief read level read compare sequence
 *
 * @param cs		chip select
 * @param ddr_type	mc ddr type
 *
 * @return	not used
 */
dfi_code void dfi_rdlvl_rd_comp_seq(u8 cs, u8 ddr_type)
{
	lvl_all_wo_0_t lvlwo = { .all = dfi_readl(LVL_ALL_WO_0), };
	lvlwo.b.rdlvl_comp_start = 1;
	dfi_writel(lvlwo.all, LVL_ALL_WO_0);
	//rdlvl_comp_start is auto clear, no need to write in '0'

	//Issue MPR Read
	dfi_mc_rdlvl_rd_issue_seq(cs, ddr_type);

	u32 rdata;
	do {
		rdata = dfi_readl(LVL_ALL_RO_0);
	//} while (((rdata & RDLVL_COMP_DONE_MASK) >> RDLVL_COMP_DONE_SHIFT) != 1);
	} while (((rdata >> 4) & 1) != 1);
}

/*!
 * @brief Momory controller force ODT sequence
 *
 * @param cs	chip select
 * @param en	true if enable
 *
 * @return	not used
 */
dfi_code void dfi_mc_force_odt_seq(u8 cs, bool en)
{
	if (en) {
		odt_cntl_0_t odt0 = { .all = mc0_readl(ODT_CNTL_0), };
		odt0.b.force_odt = cs;
		mc0_writel(odt0.all, ODT_CNTL_0);
	} else {
		odt_cntl_0_t odt0 = { .all = mc0_readl(ODT_CNTL_0), };
		odt0.b.force_odt = 0;
		mc0_writel(odt0.all, ODT_CNTL_0);
	}
}

/*!
 * @brief set ck gate data
 *
 * @param gate	gate
 *
 * @return	not used
 */
dfi_code void dfi_ck_gate_data(u8 gate)
{
	//gate == 1: PHY clock for data is gated off
	//gate == 0: normal mode
	ck_gate_0_t gate0 = { .all = dfi_readl(CK_GATE_0), };
	gate0.b.ck_gate_data = gate;
	dfi_writel(gate0.all, CK_GATE_0);
	ndelay(30);
}

/*!
 * @brief set ck gate adcm
 *
 * @param gate	gate
 *
 * @return	not used
 */
dfi_code void dfi_ck_gate_adcm(u8 gate)
{
	ck_gate_0_t gate0 = { .all = dfi_readl(CK_GATE_0), };
	gate0.b.ck_gate_adcm = gate;
	dfi_writel(gate0.all, CK_GATE_0);
	ndelay(30);
}

/*!
 * @brief set ck gate ck
 *
 * @param gate	gate
 *
 * @return	not used
 */
dfi_code void dfi_ck_gate_ck(u8 gate)
{
	ck_gate_0_t gate0 = { .all = dfi_readl(CK_GATE_0), };
	gate0.b.ck_gate_ck = gate;
	dfi_writel(gate0.all, CK_GATE_0);
	ndelay(30);
}

/*!
 * @brief set ck gate rank
 *
 * @param gate	gate
 *
 * @return	not used
 */
dfi_code void dfi_ck_gate_rank(u8 gate)
{
	ck_gate_0_t gate0 = { .all = dfi_readl(CK_GATE_0), };
	gate0.b.ck_gate_rank = gate;
	dfi_writel(gate0.all, CK_GATE_0);
	ndelay(30);
}

/*!
 * @brief set memory controller self refresh mode
 *
 * @param en	true to enter
 *
 * @return	not used
 */
norm_ps_code void mc_self_refresh(bool en)
{
	dram_pwr_cmd_t pwrcmd = { .all = mc0_readl(DRAM_PWR_CMD), };
	pwrcmd.b.dram_pwr_cmd_cs_sel = 0xf;
	if (en)
		pwrcmd.b.sr_req = 1;		//enter self refresh
	else
		pwrcmd.b.pwdn_exit_req = 0;	//exit self refresh

	mc0_writel(pwrcmd.all, DRAM_PWR_CMD);

	while (1) {
		mc_status_t status = { .all = mc0_readl(MC_STATUS), };
		if ((status.all & (~0x20000000)) == 0xef71f1f)
			break;
	}

}

/*!
 * @brief dfi set PLL freq before DDR initialization
 *
 * @param freq	frequency
 * @param debug	true to show debug message
 *
 * @return	register read of PLL2_CTRL_0
 */
dfi_code u32 dfi_set_pll_freq(int freq, bool debug)
{
	u32 value = 0;

	switch (freq) {
	case 3200:
		value = DDR_PLL_3200;
		break;
	case 2666:
		value = DDR_PLL_2666;
		break;
	case 2400:
		value = DDR_PLL_2400;
		break;
	case 2133:
		value = DDR_PLL_2133;
		break;
	case 2000:
		value = DDR_PLL_2000;
		break;
	case 1866:
		value = DDR_PLL_1866;
		break;
	case 1600:
		value = DDR_PLL_1600;
		break;
	case 1333:
		value = DDR_PLL_1333;
		break;
	case 1066:
		value = DDR_PLL_1066;
		break;
	case 800:
		value = DDR_PLL_800;
		break;
	case 400:
		value = DDR_PLL_400;
		break;
	default:
		freq = 800;
		value = DDR_PLL_800;
	}

	misc_writel(value, PLL2_CTRL_0);
	ndelay(100);

	if (debug)
		utils_dfi_trace(LOG_ERR, 0x1740, "dfi_set_pll_freq - Set DDR PLL frequency to %d, register read out: 0x%x\n", freq, misc_readl(PLL2_CTRL_0));

	return misc_readl(PLL2_CTRL_0);
}

/*!
 * @brief dfi change frequency while DDR is active
 *
 * @param freq	frequency
 *
 * @return	not used
 */
dfi_code void dfi_chg_pll_freq(int freq)
{
	// Disable data requests & refresh
	mc_dfc_cmd_t r_dfc = { .all = mc0_readl(MC_DFC_CMD), };
	r_dfc.b.dfc_mode_enb = 1;
	r_dfc.b.halt_rw_traffic = 1;
	mc0_writel(r_dfc.all, MC_DFC_CMD);

	mc_self_refresh(true);

	u32 value = dfi_set_pll_freq(freq, true);
	dfi_sync();
	ndelay(10);

	mc_self_refresh(false);

	// Enable data request
	r_dfc.b.dfc_mode_enb = 0;
	r_dfc.b.halt_rw_traffic = 0;
	mc0_writel(r_dfc.all, MC_DFC_CMD);

	utils_dfi_trace(LOG_ERR, 0x58ff, "dfi_chg_pll_freq - Set DDR PLL frequency to %d, register read out: 0x%x\n", freq, value);
}

/*!
 * @brief dfi push ADCM output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
dfi_code void dfi_push_adcm(int ofst)
{
	sel_ctrl_1_t sel1 = { .all = dfi_readl(SEL_CTRL_1), };
	sel1.b.simul_oadly_ca = 1;
	//Currently only support 1 rank
	sel1.b.simul_oadly_ck = 1;
	sel1.b.simul_oadly_rank = 1;
	dfi_writel(sel1.all,SEL_CTRL_1);

	dfi_ck_gate_adcm(CK_GATE_CS_ALL);
	dfi_ck_gate_ck(CK_GATE_CS_ALL);
	dfi_ck_gate_rank(CK_GATE_CS_ALL);

	int temp = 0;
	u8 fail = 0;
	sel_oadly_0_t oadly0 = { .all = dfi_readl(SEL_OADLY_0), };
	temp = oadly0.b.ca_wr_dly + ofst;
	if (temp>=0 && temp<=255)
		oadly0.b.ca_wr_dly = temp;
	else
		fail = 1;
	dfi_writel(oadly0.all,SEL_OADLY_0);

	sel_oadly_1_t oadly1 = { .all = dfi_readl(SEL_OADLY_1), };
	temp = oadly1.b.ck_wr_dly + ofst;
	if (temp>=0 && temp<=255)
		oadly1.b.ck_wr_dly = temp;
	else
		fail = 1;
	dfi_writel(oadly1.all,SEL_OADLY_1);

	sel_oadly_2_t oadly2 = { .all = dfi_readl(SEL_OADLY_2), };
	temp = oadly2.b.rank_wr_dly + ofst;
	if (temp>=0 && temp<=255)
		oadly2.b.rank_wr_dly = temp;
	else
		fail = 1;
	dfi_writel(oadly2.all,SEL_OADLY_2);

	oadly_0_t cm_oadly0 = { .all = dfi_readl(OADLY_0), };
	temp = cm_oadly0.b.cm_wr_dly + ofst;
	if (temp >= 0 && temp <= 255)
		cm_oadly0.b.cm_wr_dly = temp;
	else
		fail = 1;

	dfi_writel(cm_oadly0.all,OADLY_0);

	if (fail==1)
		utils_dfi_trace(LOG_ERR, 0x08c0, "dfi_push_adcm - Warning, pushing ADCM out of range. Abandon pushing.\n");
	else
		utils_dfi_trace(LOG_ERR, 0x2de0, "dfi_push_adcm - Pushing ADCM delay by %d.\n", ofst);

	dfi_ck_gate_adcm(CK_GATE_NORMAL);
	dfi_ck_gate_ck(CK_GATE_NORMAL);
	dfi_ck_gate_rank(CK_GATE_NORMAL);

	sel1.b.simul_oadly_ca = 0;
	sel1.b.simul_oadly_ck = 0;
	sel1.b.simul_oadly_rank = 0;
	dfi_writel(sel1.all,SEL_CTRL_1);
}

/*!
 * @brief dfi push CA (no clock) output delay
 *
 * @param ofst	offset
 * @param index	target AD pin
 *
 * @return	not used
 */
void dfi_push_ca(int ofst, u8 index)
{
	sel_ctrl_0_t sel0 = { .all = dfi_readl(SEL_CTRL_0), };
	sel_ctrl_1_t sel1 = { .all = dfi_readl(SEL_CTRL_1), };

	u8 cm_index = 18; 	// If index > cm_index, push both CA/CM. Otherwise, push individually.
	if (index > cm_index ) {
		sel1.b.simul_oadly_ca = 1;
		dfi_writel(sel1.all,SEL_CTRL_1);
	} else {
		sel0.b.sel_ca = index;
		dfi_writel(sel0.all,SEL_CTRL_0);
	}

	dfi_ck_gate_adcm(CK_GATE_CS_ALL);

	int temp = 0;
	u8 fail = 0;

	if (index != cm_index) {
		sel_oadly_0_t oadly0 = { .all = dfi_readl(SEL_OADLY_0), };
		temp = oadly0.b.ca_wr_dly + ofst;
		if (temp>=0 && temp<=255)
			oadly0.b.ca_wr_dly = temp;
		else
			fail = 1;
		dfi_writel(oadly0.all,SEL_OADLY_0);
	}

	// The other commands (BA, RAS, CAS, etc)
	if (index >= cm_index) {
		oadly_0_t cm_oadly0 = { .all = dfi_readl(OADLY_0), };
		temp = cm_oadly0.b.cm_wr_dly + ofst;
		if (temp >=0 && temp <= 255)
			cm_oadly0.b.cm_wr_dly = temp;
		else
			fail = 1;

		dfi_writel(cm_oadly0.all,OADLY_0);
	}

	if (fail==1)
		utils_dfi_trace(LOG_ERR, 0x3e05, "dfi_push_ca - Warning, pushing CA out of range.\n");
	else
		utils_dfi_trace(LOG_ERR, 0xca24, "dfi_push_ca - Pushing CA%d delay by %d.\n", index, ofst);

	dfi_ck_gate_adcm(CK_GATE_NORMAL);

	sel1.b.simul_oadly_ca = 0;
	dfi_writel(sel1.all,SEL_CTRL_1);
}

/*!
 * @brief dfi push CK output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
void dfi_push_ck(int ofst)
{
	sel_ctrl_1_t sel1 = { .all = dfi_readl(SEL_CTRL_1), };
	sel1.b.simul_oadly_ck = 1;
	dfi_writel(sel1.all,SEL_CTRL_1);

	dfi_ck_gate_ck(CK_GATE_CS_ALL);

	int temp = 0;
	u8 fail = 0;

	sel_oadly_1_t oadly1 = { .all = dfi_readl(SEL_OADLY_1), };
	temp = oadly1.b.ck_wr_dly + ofst;
	if (temp>=0 && temp<=255)
		oadly1.b.ck_wr_dly = temp;
	else
		fail = 1;
	dfi_writel(oadly1.all,SEL_OADLY_1);

	if (fail==1)
		utils_dfi_trace(LOG_ERR, 0xa00b, "dfi_push_ck - Warning, pushing CK out of range.\n");
	else
		utils_dfi_trace(LOG_ERR, 0x0dce, "dfi_push_ck - Pushing CK delay by %d.\n", ofst);

	dfi_ck_gate_ck(CK_GATE_NORMAL);

	sel1.b.simul_oadly_ck = 0;
	dfi_writel(sel1.all,SEL_CTRL_1);
}

/*!
 * @brief dfi push rank (no clock) output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
void dfi_push_rank(int ofst)
{
	sel_ctrl_1_t sel1 = { .all = dfi_readl(SEL_CTRL_1), };
	//Currently only support 1 rank
	sel1.b.simul_oadly_rank = 1;
	dfi_writel(sel1.all,SEL_CTRL_1);

	dfi_ck_gate_rank(CK_GATE_CS_ALL);

	int temp = 0;
	u8 fail = 0;

	sel_oadly_2_t oadly2 = { .all = dfi_readl(SEL_OADLY_2), };
	temp = oadly2.b.rank_wr_dly + ofst;
	if (temp>=0 && temp<=255)
		oadly2.b.rank_wr_dly = temp;
	else
		fail = 1;
	dfi_writel(oadly2.all,SEL_OADLY_2);

	if (fail==1)
		utils_dfi_trace(LOG_ERR, 0x2b32, "dfi_push_rank - Warning, pushing rank out of range.\n");
	else
		utils_dfi_trace(LOG_ERR, 0x3c55, "dfi_push_rank - Pushing rank delay by %d.\n", ofst);

	dfi_ck_gate_rank(CK_GATE_NORMAL);

	sel1.b.simul_oadly_rank = 0;
	dfi_writel(sel1.all,SEL_CTRL_1);
}

/*!
 * @brief dfi push DQ output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
dfi_code void dfi_push_dq(int ofst)
{
	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };

	//When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 num_byte = 1;
	u8 num_bit = 8;

	switch (data_width) {
	case 4: //x64
		num_byte = 8;
		num_bit = 64;
		break;

	case 3: //x32
		num_byte = 4;
		num_bit = 32;
		break;

	case 2: //x16
		num_byte = 2;
		num_bit = 16;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
	}

	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	if (ras.b.ecc_enb == 1) {
		//ECC is ON
		num_byte = num_byte + 1;
		num_bit = num_bit + 8;
	}

	utils_dfi_trace(LOG_ERR, 0x8d69, "dfi_push_dq - Byte width: %d, pushing existing dq_wr_dly by %d\n", num_byte, ofst);

	int dly_tmp = 0;
	u8 id8 = 0;

	dfi_ck_gate_data(CK_GATE_CS_ALL);

	for (int i = 0; i < num_bit; i++) {
		sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
		ctrl0.b.sel_dbit = i;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
		if (i % 8 == 0) {

			dly_tmp = oddly2.b.dq_wr_dly; //Choose the bit[0] as existing byte delay.
			if (dly_tmp + ofst > 255) {
				utils_dfi_trace(LOG_ERR, 0x193f, "dfi_push_dq - Warning: Byte[%d] DQ delay settings overflow, set its delay to 255!\n", i);
				dly_tmp = 255;
			} else if (dly_tmp + ofst < 0) {
				utils_dfi_trace(LOG_ERR, 0xc686, "dfi_push_dq - Warning: Byte[%d] DQ delay settings underflow, set its delay to 0!\n", i);
				dly_tmp = 0;
			} else {
				dly_tmp = dly_tmp + ofst;
			}

			//Update DM delay with DQ delay
			id8 = (i - i % 8) / 8; // get the byte index
			sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
			ctrl0.b.sel_dbyte = id8;
			dfi_writel(ctrl0.all, SEL_CTRL_0);
			sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
			oddly1.b.dm_wr_dly = dly_tmp;
			dfi_writel(oddly1.all, SEL_ODDLY_1);
		}
		oddly2.b.dq_wr_dly = dly_tmp;
		dfi_writel(oddly2.all, SEL_ODDLY_2);
	}

	dfi_ck_gate_data(CK_GATE_NORMAL);

	utils_dfi_trace(LOG_ERR, 0x4870, "dfi_push_dq - Successfully finished.\n");
}

/*!
 * @brief dfi push DQS output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
dfi_code void dfi_push_dqs(int ofst)
{
	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	//When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 num_byte = 1;

	switch (data_width) {
	case 4: //x64
		num_byte = 8;
		break;

	case 3: //x32
		num_byte = 4;
		break;

	case 2: //x16
		num_byte = 2;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
	}

	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	if (ras.b.ecc_enb == 1) {
		//ECC is ON
		num_byte = num_byte + 1;
	}

	utils_dfi_trace(LOG_ERR, 0x6ea0, "dfi_push_dqs - Byte width: %d, pushing existing dqs_wr_dly by %d\n", num_byte, ofst);

	int dly_tmp = 0;

	dfi_ck_gate_data(CK_GATE_CS_ALL);

	for (int i = 0; i < num_byte; i++) {
		sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
		ctrl0.b.sel_dbyte = i;
		dfi_writel(ctrl0.all, SEL_CTRL_0);

		sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };

		dly_tmp = oddly0.b.dqs_wr_dly;
		if (dly_tmp + ofst > 255) {
			utils_dfi_trace(LOG_ERR, 0x9230, "dfi_push_dqs - Warning: Byte[%d] DQS delay settings overflow, set its delay to 255!\n", i);
			dly_tmp = 255;
		} else if (dly_tmp + ofst < 0) {
			utils_dfi_trace(LOG_ERR, 0x316b, "dfi_push_dqs - Warning: Byte[%d] DQS delay settings underflow, set its delay to 0!\n", i);
			dly_tmp = 0;
		} else {
			dly_tmp = dly_tmp + ofst;
		}

		oddly0.b.dqs_wr_dly = dly_tmp;
		//oddly1.b.dm_wr_dly   = dly_tmp;
		dfi_writel(oddly0.all, SEL_ODDLY_0);
		//dfi_writel(oddly1.all,SEL_ODDLY_1);
	}

	dfi_ck_gate_data(CK_GATE_NORMAL);

	utils_dfi_trace(LOG_ERR, 0x0661, "dfi_push_dqs - Successfully finished.\n");
}

/*!
 * @brief dfi generate simple hash
 *
 * @param value	value
 *
 * @return	hash result
 */
dfi_code u32 dfi_simple_hash(u32 value)
{
	u32 result;
	u32 value1 = value + 1;
	result = (value1 << 6) + (value1 << (8 + 5)) + (value1 << (16 + 4)) + (value1 << (24 + 3));
	result = result - (value1 << 3) - (value1 << (8 + 2)) - (value1 << (16 + 1)) - (value1 << 24);

	return result;
}

/*!
 * @brief dfi generate pattern based on address
 *		Either SSO patterns (dbi on vs off) or random pattern hashed from address inpu
 *		Need data_width in order to generate SSO patterns because in x64, it takes 2x4B for one data beat
 *		type 0 : Random pattern hashed from input address
 *		type 1 : SSO pattern w/ write DBI enabled
 *		type 2 : SSO pattern w/o DBI
 *
 * @param data_width	data width
 * @param type		value
 * @param in		data in
 *
 * @return	pattern
 */
dfi_code u32 dfi_gen_pattern(u8 data_width, u8 type, u32 in)
{
	u32 pattern;

	// SSO pattern w/o DBI
	if (type == 2) {
		if (data_width == 4) { // x64
			if ((in & 0x8) == 0)
				pattern = 0x00000000;
			else
				pattern = 0xFFFFFFFF;
		} else if (data_width == 3) { // x32
			if ((in & 0x4) == 0)
				pattern = 0x00000000;
			else
				pattern = 0xFFFFFFFF;
		} else { // x16
			pattern = 0xFFFF0000;
		}
	} else if (type == 1) { // SSO w/ write DBI enabled
		if (data_width == 4) { // x64
			if ((in & 0x8) == 0)
				pattern = 0xA5A5A5A5;
			else
				pattern = 0x5A5A5A5A;
		} else if (data_width == 3) { // x32
			if ((in & 0x4) == 0)
				pattern = 0xA5A5A5A5;
			else
				pattern = 0x5A5A5A5A;
		} else { // x16
			pattern = 0xA5A55A5A;
		}
	} else {
		pattern = dfi_simple_hash(in);
	}

	return pattern;
}

/*!
 * @brief dfi use dpe to verify training
 *
 * @param type		type
 * @param src		src pointer
 * @param dst		dst pointer
 * @param bytes		bytes to be verified
 * @param debug		show debug message
 *
 * @return result
 */
dfi_code u8 dfi_dpe_verify_training(u8 type, void *src, void *dst, u32 bytes, bool debug)
{
	u32 pattern;
	u32 bytesd2 = bytes >> 1;
	u8 failed = 0;
	u32 actual;

	// normalize to 32B-aligned because of DPE minimum granularity
	src = (void*) ((u32) src & 0xFFFFFFE0);
	dst = (void*) ((u32) dst & 0xFFFFFFE0);
	bytes = bytes & 0xFFFFFFE0;
	bytesd2 = bytesd2 & 0xFFFFFFE0;

	// Get data_width to generate appropriate patterns
	// 4=x64, 3=x32, 2=x16
	device_mode_t r_mode = { .all = mc0_readl(DEVICE_MODE) };
	u8 data_width = r_mode.b.data_width;

	// CPU fills the source with pattern
	for (u32 i = 0; i < bytes; i += 4) {
		pattern = dfi_gen_pattern(data_width, type, i);
		writel(pattern, src + i);
	}

	// DPE copies from source (lower half) to destination (lower half)
	dfi_dpe_copy(src, dst, bytesd2);

	// CPU read from destination to verify lower half data
	for (u32 i = 0; i < bytesd2; i += 4) {
		actual = readl(dst + i);
		pattern = dfi_gen_pattern(data_width, type, i);
		if (actual != pattern) {
			if (debug)
				utils_dfi_trace(LOG_ERR, 0xe98d, "DPE verify write fail at address=%p: expected %x, read %x\n", dst + i, pattern, actual);
			failed = 1;
		} else {
			if (i % 32 == 0) {
				if (debug)
					utils_dfi_trace(LOG_ERR, 0x5827, "read %p:%x\n", dst + i, actual);
			}
		}
	}

	// DPE copies from source (upper half) to destination (lower half), overwriting previous data
	dfi_dpe_copy(src + bytesd2, dst, bytesd2);

	// CPU read from destination to verify upper half data
	u32 i_mod;
	for (u32 i = 0; i < bytesd2; i += 4) {
		actual = readl(dst + i); // destination address is the same as the lower half
		i_mod = i + bytesd2;
		pattern = dfi_gen_pattern(data_width, type, i_mod); // but expected pattern is generated from upper half address
		if (actual != pattern) {
			failed = 1;
			if (debug)
				utils_dfi_trace(LOG_ERR, 0xaccc, "DPE verify write fail at address=%p: expected %x, read %x\n", dst + i, pattern, actual);
		} else {
			if (i % 32 == 0) {
				if (debug)
					utils_dfi_trace(LOG_ERR, 0x9480, "read %p:%x\n", dst + i, actual);
			}
		}
	}

	return failed;
}

#ifdef DDR_PERF_CNTR
/*!
 * @brief MC performance counters clock start
 *
 * @param pc_sel	performance counter select
 *
 * @return	not used
 */
void mc_pc_clk_start(u8 pc_sel)
{
	// Clear the selected performance counter (from 0 to 7) and start it.
	// Select event is clk, which counts the number of MC clock cycles
	mc0_writel(pc_sel, PERFORMANCE_COUNTER_SEL);
	performance_counter_cntl0_t reg0 = { .all = 0 };
	reg0.b.pc_counter_clr = 1;
	reg0.b.pc_event_sel = 0x13; // event == mc_clk
	mc0_writel(reg0.all, PERFORMANCE_COUNTER_CNTL0);
	reg0.b.pc_counter_clr = 0;
	reg0.b.pc_event_enb = 1;
	mc0_writel(reg0.all, PERFORMANCE_COUNTER_CNTL0);
}

/*!
 * @brief MC performance counters clock get
 *
 * @param pc_sel	performance counter select
 * @param pc_val	performance counter value output
 * @param pc_val_up	performance counter upper 16 bits value output
 * @param pc_over	performance counter overflow
 *
 * @return	not used
 */
void mc_pc_clk_get(u8 pc_sel, u32 *pc_val, u16 *pc_val_up, u8 *pc_over)
{
	mc0_writel(pc_sel, PERFORMANCE_COUNTER_SEL);
	*pc_val = mc0_readl(PERFORMANCE_COUNTER_VALUE);
	u32 pc_sts = mc0_readl(PERFORMANCE_COUNTER_STATUS);
	*pc_val_up = pc_sts>>16;
	*pc_over = pc_sts & 1;
}

/*!
 * @brief MC performance counters clock stop
 *
 * @param pc_sel	performance counter select
 *
 * @return	not used
 */
void mc_pc_clk_stop(u8 pc_sel)
{
	mc0_writel(pc_sel, PERFORMANCE_COUNTER_SEL);
	performance_counter_cntl0_t reg0 = { .all = 0 };
	reg0.b.pc_event_enb = 0;
	mc0_writel(reg0.all, PERFORMANCE_COUNTER_CNTL0);
}
#endif

/*!
 * @brief dfi training for ddr3/ddr4
 *
 * @param target_speed	target training speed
 * @param target_cs	target training cs
 * @param debug		debug message level
 *
 * @return	result
 */
dfi_code int dfi_train_all(int target_speed, u8 target_cs, u8 debug)
{
	int res = DFI_TRAIN_PASS;

	dfi_set_pll_freq(target_speed, true);

	dfi_phy_init();
	utils_dfi_trace(LOG_ERR, 0xb0af, "dfi_phy_init DONE\n");

	res = dfi_pad_cal_seq(debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_PAD_CAL_FAIL;
	else if (debug)
		utils_dfi_trace(LOG_ERR, 0x1795, "dfi_pad_cal_seq DONE\n");

	mc_init();
	utils_dfi_trace(LOG_ERR, 0xba19, "mc_init DONE\n");


#if defined(M2) || defined(U2)
	res = dfi_wrlvl_seq_m2(target_cs, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_WR_LVL_FAIL;
	else if (debug)
#if defined(M2)
		utils_dfi_trace(LOG_ERR, 0xac66, "dfi_wrlvl_seq_m2 DONE\n");
#elif defined(U2)
		utils_dfi_trace(LOG_ERR, 0xf6bf, "dfi_wrlvl_seq_u2 DONE\n");
#endif
#else // EVB
	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	u8 data_width = mode.b.data_width;
	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };

	if (data_width == MC_BUS_WIDTH_64) {
		u8 level_tacoma[] = { 6, 7, 5, 4, 2, 3, 1, 0 };
		u8 level_tacoma_ecc[] = { 6, 7, 5, 4, 8, 2, 3, 1, 0 };
		if (ras.b.ecc_enb)
			res = dfi_wrlvl_seq_hs(target_cs, level_tacoma_ecc, debug);
		else
			res = dfi_wrlvl_seq_hs(target_cs, level_tacoma, debug);
	} else {
		u8 level_rainier[] = { 2, 3, 0, 1};
		u8 level_rainier_ecc[] = { 2, 3, 0, 1, 4};
		if (ras.b.ecc_enb)
			res = dfi_wrlvl_seq_hs(target_cs, level_rainier_ecc, debug);
		else
			res = dfi_wrlvl_seq_hs(target_cs, level_rainier, debug);
	}

	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_WR_LVL_FAIL;
	else if (debug)
		utils_dfi_trace(LOG_ERR, 0xc079, "dfi_wrlvl_seq_hs DONE\n");

#endif //M.2/U.2/EVB

	res = dfi_rdlvl_rdlat_seq(target_cs, 10, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_RDLAT_FAIL;
	else if (debug)
		utils_dfi_trace(LOG_ERR, 0x67c0, "dfi_rdlvl_rdlat_seq DONE\n");

	res = dfi_rdlvl_2d_seq(target_cs, 0, 10, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_2D_FAIL;
	else if (debug)
		utils_dfi_trace(LOG_ERR, 0x0625, "dfi_rdlvl_2d_seq DONE\n");

	res = dfi_vref_dq_seq(target_cs, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_VREF_DQ_FAIL;
	else if (debug)
		utils_dfi_trace(LOG_ERR, 0x5178, "dfi_vref_dq_seq DONE\n");

	res = dfi_wr_dqdqs_dly_seq(target_cs, TRAIN_FULL, debug);
	if (res == 0)
		return DFI_TRAIN_WR_DQ_DQS_DLY_FAIL;
	else if (debug)
		utils_dfi_trace(LOG_ERR, 0x4c0b, "dfi_wr_dqdqs_dly_seq DONE\n");

	u8 win_size = 0; //unused
	int mod_delay = 0; //unused
	res = dfi_rdlvl_dpe_seq(target_cs, TRAIN_FULL, &win_size, &mod_delay, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_DPE_FAIL;
	else if (debug)
		utils_dfi_trace(LOG_ERR, 0x2a11, "dfi_rdlvl_dpe_seq DONE, win_size %d mod_delay %d\n", win_size, mod_delay);

	utils_dfi_trace(LOG_ERR, 0x9e02, "dfi_train_all DONE\n");

	return DFI_TRAIN_PASS;
}

/*!
 * @brief dfi training for lpddr4
 *
 * @param target_speed	target training speed
 * @param target_cs	target training cs
 * @param debug		debug message level
 *
 * @return	result
 */
dfi_code int dfi_train_all_lpddr4(int target_speed, u8 target_cs, u8 debug)
{
	int res = DFI_TRAIN_PASS;

	dfi_set_pll_freq(target_speed, true);

	dfi_phy_init();
	utils_dfi_trace(LOG_ERR, 0x3f3d, "dfi_phy_init DONE\n");

	res = dfi_pad_cal_seq(debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_PAD_CAL_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0x2ce9, "dfi_pad_cal_seq DONE\n");

	mc_init();
	utils_dfi_trace(LOG_ERR, 0xd68c, "mc_init DONE\n");

	utils_dfi_trace(LOG_ERR, 0xe688, "dfi_calvl_seq target_cs %d target_speed = %d\n", target_cs, target_speed);
	res = dfi_calvl_seq(target_cs, target_speed, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_CA_LVL_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0x7c56, "dfi_calvl_seq DONE\n");

	//Write leveling training, optional for LPDDR4
	res = dfi_wrlvl_seq_m2(target_cs, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_WR_LVL_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0xa33c, "dfi_wrlvl_seq_m2 DONE\n");

	res = dfi_rdlvl_rdlat_seq(target_cs, 10, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_RDLAT_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0xb801, "dfi_rdlvl_rdlat_seq DONE\n");

	res = dfi_rdlvl_2d_seq(target_cs, 0, 10, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_2D_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0x143c, "dfi_rdlvl_2d_seq DONE\n");

	res = dfi_rdlvl_gate_seq(target_cs, 0, 8, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_GATE_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0x16f4, "dfi_rdlvl_gate_seq DONE\n");

	res = dfi_rdlvl_2d_seq(target_cs, 0, 10, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_2D_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0x85f8, "dfi_rdlvl_2d_seq DONE\n");

	res = dfi_wr_train_seq(target_cs, 0, 30, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_WR_TRAIN_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0xb58e, "dfi_wr_train_seq DONE\n");

	u8 win_size = 0; //unused
	int mod_delay = 0; //unused
	res = dfi_rdlvl_dpe_seq(target_cs, TRAIN_FULL, &win_size, &mod_delay, debug);
	if (res != DFI_TRAIN_PASS)
		return DFI_TRAIN_RD_LVL_DPE_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0x518f, "dfi_rdlvl_dpe_seq DONE, win_size %d mod_delay %d\n", win_size, mod_delay);

	res = dfi_wr_dqdqs_dly_seq(target_cs, TRAIN_FULL, debug);
	if (res < 10)
		return DFI_TRAIN_WR_DQ_DQS_DLY_FAIL;
	else
		utils_dfi_trace(LOG_ERR, 0xd0a7, "dfi_wr_dqdqs_dly_seq DONE\n");

	utils_dfi_trace(LOG_ERR, 0x9148, "dfi_train_all_lpddr4 DONE\n");

	return DFI_TRAIN_PASS;
}

/*! @} */
