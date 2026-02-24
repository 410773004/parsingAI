
//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2020 Innogrit Corporation
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
/*! \file dfi_wrlvl_seq.c
 * @brief dfi write level training sequence APIs
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
#include "dfi_common.h"
#include "dfi_config.h"
#include "dfi_reg.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "stdio.h"
#define __FILEID__ dfiwrlvl
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
 * @brief Memory Control write level mode enable
 *
 * @param ddr_type	mc ddr type
 * @param cs		chip select
 * @param en		true to enabe, false to disable
 *
 * @return	not used
 */
dfi_code void dfi_mc_wrlvl_mode(u8 ddr_type, u8 cs, bool en)
{
	u8 mr;

	if (ddr_type == MC_LPDDR4) {
		mr = MR2;
	} else {
		mr = MR1;
	}

	if (en) { //enable DRAM write leveling mode
		device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
		config.b.write_leveling_mode = 1;
		mc0_writel(config.all, DEVICE_CONFIG_TRAINING);

	} else { //Disable DRAM write leveling mode
		device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
		config.b.write_leveling_mode = 0;
		mc0_writel(config.all, DEVICE_CONFIG_TRAINING);
	}

	dfi_mc_mr_rw_req_seq(MR_WRITE, mr, cs);
}

/*!
 * @brief DFI write level mode enable
 *
 * @param en	true to enabe, false to disable
 *
 * @return	not used
 */
dfi_code void dfi_wrlvl_mode(bool en)
{
	lvl_wrlvl_0_t wrlvl = { .all = dfi_readl(LVL_WRLVL_0), };
	wrlvl.b.wrlvl_mode = en;
	dfi_writel(wrlvl.all, LVL_WRLVL_0);
}

/*!
 * @brief DFI write level send dqs pulse
 *
 * @return	not used
 */
dfi_code void dfi_wrlvl_send_dqs_pulse(void)
{
	u32 rdata;
	lvl_all_wo_0_t wo0 = { .all = 0, };
	wo0.b.wrlvl_send_pulse = 1;
	dfi_writel(wo0.all, LVL_ALL_WO_0);

	do {
		rdata = dfi_readl(LVL_ALL_RO_0);
	} while (!(rdata & WRLVL_SEND_PULSE_DONE_MASK));

	ndelay(20); //tWLO + tWLOE = 9ns + 2ns (DDR3)
}

/*!
 * @brief DFI write level update dqs write
 *
 * @param dly_cnt	delay count
 *
 * @return	not used
 */
dfi_code void dfi_wrlvl_update_dqs_wr(u16 dly_cnt)
{
	dfi_ck_gate_data(CK_GATE_CS_ALL);

	sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
	oddly0.b.dqs_wr_dly = dly_cnt;
	dfi_writel(oddly0.all, SEL_ODDLY_0);

	dfi_ck_gate_data(CK_GATE_NORMAL);
}

/*!
 * @brief DFI write leveling for high speed configurations
 *
 * @param dly_cnt	delay count
 *
 * @return	not used
 */
dfi_code int dfi_wrlvl_seq_hs(u8 target_cs, u8 * level_order, u8 debug)
{
	//Input level_order stores the order of bytes to be trained.
	u8 error = 0;
	u8 cs;

	switch (target_cs) {
	case 0:
		cs = CS0_BIT;
		break;
	case 1:
		cs = CS1_BIT;
		break;
	case 2:
		cs = CS2_BIT;
		break;
	case 3:
		cs = CS3_BIT;
		break;
	default:
		cs = CS0_BIT;
	}

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };

	// When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 ddr_type = mode.b.dram_type;

	u8 num_byte = 1;
	u8 num_bit = 8;
	u16 byte_mask = 1;

	u32 rdata;

	switch (data_width) {
	case 4: //x64
		num_byte = 8;
		num_bit = 64;
		byte_mask = 255;
		break;

	case 3: //x32
		num_byte = 4;
		num_bit = 32;
		byte_mask = 15;
		break;

	case 2: //x16
		num_byte = 2;
		num_bit = 16;
		byte_mask = 3;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
		byte_mask = 1;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
		byte_mask = 1;
	}

	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	if (ras.b.ecc_enb == 1) {
		//ECC is ON
		if (num_byte == 4) {
			byte_mask = 31;
		} else if (num_byte == 8) {
			byte_mask = 511;
		}
		num_byte = num_byte + 1;
		num_bit = num_bit + 8;
	}

	if (debug >= 1)
		utils_dfi_trace(LOG_ERR, 0xb514, "DFI write leveling - Write leveling start, target CS[%d], byte width: %d, byte mask: 0x%x\n", target_cs, num_byte, byte_mask);

	//DDR3 need disable DQS PU/PD
	u8 ddr3_dqs_pupd = 0;
	if (ddr_type == MC_DDR3) {
		io_data_1_t data1 = { .all = dfi_readl(IO_DATA_1), };
		ddr3_dqs_pupd = ((data1.all >> 1) & 3); //record the original value
		data1.b.dqs_pd = 0;
		data1.b.dqsn_pu = 1; //turn off pu, active low, will be active high in next Rainer
		dfi_writel(data1.all, IO_DATA_1);
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x367f, "DFI write leveling - Disable DDR3 DQS/DQSN PDPU\n");
	}

	// LPDDR4 needs double DQS pulse
	if (ddr_type == MC_LPDDR4) {
		lvl_wrlvl_0_t wrlvl = { .all = dfi_readl(LVL_WRLVL_0), };
		wrlvl.b.wrlvl_pulse_width = 1;
		dfi_writel(wrlvl.all, LVL_WRLVL_0);
	}

	// Enable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, true);
	// Enable Write Leveling in DFI
	dfi_wrlvl_mode(true);

	//If ODT is enabled, then force ODT high, else don't force
	//Read out ODT Value for Target CS
	cs_config_sel_t config = { .all = mc0_readl(CS_CONFIG_SEL), };
	config.b.cs_config_cs_sel = target_cs;
	mc0_writel(config.all, CS_CONFIG_SEL);

	rdata = mc0_readl(CS_CONFIG_ODT);
	u8 odt_value = ((rdata >> 20) & 7); //bit [22:20] is ODT_value (RTT_NOM)
	u8 auto_odt = 0;
	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x34a2, "DFI write leveling - force ODT high\n");

		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		auto_odt = cntl0.b.odt_termination_enb; //Store the original values
		cntl0.b.odt_termination_enb = 0;
		mc0_writel(cntl0.all, ODT_CNTL_0);

		dfi_mc_force_odt_seq(cs, true);

	}

	//Enable shadow register simultaneous write
	sel_ctrl_1_t ctrl1 = { .all = dfi_readl(SEL_CTRL_1), };
	ctrl1.b.simul_oddly_dqs = 1;
	ctrl1.b.simul_oddly_dqdm = 1;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	// Clear output delay registers
	u16 dly_cnt = DATA_DLY_OFFSET;
	dfi_ck_gate_data(CK_GATE_CS_ALL);

	sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
	oddly0.b.dqs_wr_dly = dly_cnt;
	dfi_writel(oddly0.all, SEL_ODDLY_0);

	sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
	oddly1.b.dm_wr_dly = dly_cnt;
	dfi_writel(oddly1.all, SEL_ODDLY_1);

	sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
	oddly2.b.dq_wr_dly = dly_cnt;
	dfi_writel(oddly2.all, SEL_ODDLY_2);

	dfi_ck_gate_data(CK_GATE_NORMAL);

	// *************************************************************************
	// Sweep DQS output delay against CK
	// *************************************************************************
	u16 leveled_delay[num_byte];
	u16 dq_state = 0;
	u16 leveled_flag0 = 0;
	u16 leveled_flag0_0 = 0;
	u16 leveled_flag0_1 = 0;
	u16 leveled_flag0_2 = 0;
	u16 leveled_flag1 = 0;
	//u8 level_index = 0;
	u8 level_current_byte = 0;
	for (int i = 0; i < num_byte; i++) {
		leveled_delay[i] = 0;
	}

	//The tCK skew between adjacent DRAM device must be less than half tCK cycle
	for (int i = 0; i < num_byte; i++) {
		level_current_byte = level_order[i];
		while (dly_cnt <= 255) {
			dfi_wrlvl_send_dqs_pulse();
			rdata = dfi_readl(DQ_STATUS_ALL_2);
			dq_state = (((rdata & PHY_DQ_STATE_BYTEWISE_OR_MASK) >> PHY_DQ_STATE_BYTEWISE_OR_SHIFT) & byte_mask);
			if (debug >= 2)
				utils_dfi_trace(LOG_ERR, 0x008f, "DFI write leveling - level_current_byte = %d, current delay = %d, dq_state = 0x%x.\n", level_current_byte, dly_cnt, dq_state);


			//Must get '0' 3 times to set leveled_flag0 to prevent error caused by jitter
			leveled_flag0_2 = leveled_flag0_1;
			leveled_flag0_1 = leveled_flag0_0;
			leveled_flag0_0 = (~dq_state); //If get '0' from DRAM, set corresponding bit
			leveled_flag0 = (leveled_flag0 | (leveled_flag0_0 & leveled_flag0_1 & leveled_flag0_2));

			if (((dq_state >> level_current_byte) & 1) == 1 && ((leveled_flag0 >> level_current_byte) & 1) == 1) { //Get '1' from DRAM and have got '0' before
				leveled_flag1 = (leveled_flag1 | (1 << level_current_byte)); //flag current leveling byte as leveled
				leveled_delay[level_current_byte] = dly_cnt; //record the current delay for current byte leveled_delay
				if (debug >= 2)
					utils_dfi_trace(LOG_ERR, 0x01fe, "DFI write leveling - index %d, byte %d leveled at delay 0x%x.\n", i, level_current_byte, dly_cnt);

				break; //successfully level target byte, jump out of while loop
			}
			dly_cnt += 1; //increase dly_cnt
			if (dly_cnt <= 255) {
				dfi_wrlvl_update_dqs_wr(dly_cnt);
			}
		}
	}

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0xa406, "DFI write leveling - Sweep finished\n");


	// *************************************************************************
	// Write leveled output delay values back to registers
	// *************************************************************************

	//Disable shadow register simultaneous write
	ctrl1.b.simul_oddly_dqs = 0;
	ctrl1.b.simul_oddly_dqdm = 0;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	if ((leveled_flag1 != byte_mask) && (dly_cnt > 255)) {
		utils_dfi_trace(LOG_ERR, 0x53fa, "DFI write leveling Error - DFI write leveling delay maxed out. Leveling status is: 0x%x.\n", dq_state);
		error = 1;
	} else {    //Write Leveling is successful.
		dfi_ck_gate_data(CK_GATE_CS_ALL);
		//u16 last_delay = 0;
		u8 id8 = 0;
		for (int i = 0; i < num_bit; i++) {
			id8 = (i - i % 8) / 8; // get the byte index
			if (i % 8 == 0) {

				sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
				ctrl0.b.sel_dbyte = id8;
				dfi_writel(ctrl0.all, SEL_CTRL_0);

				sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
				oddly0.b.dqs_wr_dly = leveled_delay[id8];
				dfi_writel(oddly0.all, SEL_ODDLY_0);

				sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
				oddly1.b.dm_wr_dly = leveled_delay[id8];
				dfi_writel(oddly1.all, SEL_ODDLY_1);
				if (debug >= 2)
					utils_dfi_trace(LOG_ERR, 0x14ea, "DFI write leveling - Data byte %d, WL delay is: 0x%x.\n", id8, leveled_delay[id8]);

			}

			sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
			ctrl0.b.sel_dbit = i;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
			oddly2.b.dq_wr_dly = leveled_delay[id8];
			dfi_writel(oddly2.all, SEL_ODDLY_2);
			if (debug >= 2)
				utils_dfi_trace(LOG_ERR, 0xa518, "DFI write leveling - DQ%d, WL delay is: 0x%x.\n", i, leveled_delay[id8]);

		}
		dfi_ck_gate_data(CK_GATE_NORMAL);

		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x8859, "DFI write leveling - Program delay finished.\n");

	}

	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		dfi_mc_force_odt_seq(cs, false);
		//restore auto odt
		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		cntl0.b.odt_termination_enb = auto_odt;
		mc0_writel(cntl0.all, ODT_CNTL_0);
	}

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0xaa33, "DFI write leveling - Disable write leveling first in DFI then in DRAM\n");

	// Disable Write Leveling in DFI
	dfi_wrlvl_mode(false);
	// Disable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, false);

	if (ddr3_dqs_pupd != 0) {
		io_data_1_t data1 = { .all = dfi_readl(IO_DATA_1), };
		data1.b.dqs_pd = 1;
		data1.b.dqsn_pu = 0; //Active low, will be active high in Rainer
		dfi_writel(data1.all, IO_DATA_1);
		//utils_dfi_trace(LOG_ERR, 0, "DFI write leveling - Restore DDR3 DQS/DQSN PDPU\n");
	}

	if (error == 1) {
		utils_dfi_trace(LOG_ERR, 0xfd0e, "DFI write leveling - Write leveling failed.\n");
		return -1;
	}

	for (int i = 0; i < num_byte; i++) {
		if (debug >= 1)
			utils_dfi_trace(LOG_ERR, 0xb547, "DFI write leveling - Write leveling results: Byte[%d], delay=%d.\n", i, leveled_delay[i]);
	}

	return 0;
}

//Write leveling for High Speed M.2 board configurations, no byte order is required.
//The skew between Farthest DRAM byte and tCK MUST be less than one tCK cycle
dfi_code int dfi_wrlvl_seq_m2(u8 target_cs, u8 debug)
{
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	u8 error = 0;
	u8 cs;

	switch (target_cs) {
	case 0:
		cs = CS0_BIT;
		break;
	case 1:
		cs = CS1_BIT;
		break;
	case 2:
		cs = CS2_BIT;
		break;
	case 3:
		cs = CS3_BIT;
		break;
	default:
		cs = CS0_BIT;
	}

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	//When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 ddr_type = mode.b.dram_type;

	u8 num_byte = 1;
	u8 num_bit = 8;
	u16 byte_mask = 1;

	u32 rdata;

	switch (data_width) {
	case 4: //x64
		num_byte = 8;
		num_bit = 64;
		byte_mask = 255;
		break;

	case 3: //x32
		num_byte = 4;
		num_bit = 32;
		byte_mask = 15;
		break;

	case 2: //x16
		num_byte = 2;
		num_bit = 16;
		byte_mask = 3;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
		byte_mask = 1;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
		byte_mask = 1;
	}

	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	if (ras.b.ecc_enb == 1) {
		//ECC is ON
		if (num_byte == 4) {
			byte_mask = 31;
		} else if (num_byte == 8) {
			byte_mask = 511;
		}
		num_byte = num_byte + 1;
		num_bit = num_bit + 8;
	}

	if (debug >= 1)
		utils_dfi_trace(LOG_ERR, 0xbe95, "DFI write leveling - Write leveling start, target CS[%d], byte width: %d, byte mask: 0x%x\n", target_cs, num_byte, byte_mask);

	//DDR3 need disable DQS PU/PD
	u8 ddr3_dqs_pupd = 0;
	if (ddr_type == MC_DDR3) {
		io_data_1_t data1 = { .all = dfi_readl(IO_DATA_1), };
		ddr3_dqs_pupd = ((data1.all >> 1) & 3); //record the original value
		data1.b.dqs_pd = 0;
		data1.b.dqsn_pu = 1; //turn off pu, active low, will be active high in next Rainer
		dfi_writel(data1.all, IO_DATA_1);
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0xf5ff, "DFI write leveling - Disable DDR3 DQS/DQSN PDPU\n");
	}

	// LPDDR4 needs double DQS pulse
	if (ddr_type == MC_LPDDR4) {
		lvl_wrlvl_0_t wrlvl = { .all = dfi_readl(LVL_WRLVL_0), };
		wrlvl.b.wrlvl_pulse_width = 1;
		dfi_writel(wrlvl.all, LVL_WRLVL_0);
	}

	// Enable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, true);
	// Enable Write Leveling in DFI
	dfi_wrlvl_mode(true);

	//If ODT is enabled, then force ODT high, else don't force
	//Read out ODT Value for Target CS
	cs_config_sel_t config = { .all = mc0_readl(CS_CONFIG_SEL), };
	config.b.cs_config_cs_sel = target_cs;
	mc0_writel(config.all, CS_CONFIG_SEL);

	rdata = mc0_readl(CS_CONFIG_ODT);
	u8 odt_value = ((rdata & ODT_VALUE_MASK) >> ODT_VALUE_SHIFT);
	u8 auto_odt = 0;
	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x8db9, "DFI write leveling - force ODT high\n");

		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		auto_odt = cntl0.b.odt_termination_enb; //Store the original values
		cntl0.b.odt_termination_enb = 0;
		mc0_writel(cntl0.all, ODT_CNTL_0);

		dfi_mc_force_odt_seq(cs, true);

	}

	//Enable shadow register simultaneous write
	sel_ctrl_1_t ctrl1 = { .all = dfi_readl(SEL_CTRL_1), };
	ctrl1.b.simul_oddly_dqs = 1;
	ctrl1.b.simul_oddly_dqdm = 1;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	// Clear output delay registers
	u16 dly_cnt = DATA_DLY_OFFSET;
	dfi_ck_gate_data(CK_GATE_CS_ALL);

	sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
	oddly0.b.dqs_wr_dly = dly_cnt;
	dfi_writel(oddly0.all, SEL_ODDLY_0);

	sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
	oddly1.b.dm_wr_dly = dly_cnt;
	dfi_writel(oddly1.all, SEL_ODDLY_1);

	sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
	oddly2.b.dq_wr_dly = dly_cnt;
	dfi_writel(oddly2.all, SEL_ODDLY_2);

	dfi_ck_gate_data(CK_GATE_NORMAL);

	// *************************************************************************
	// Sweep DQS output delay against CK
	// *************************************************************************
	u16 leveled_delay[num_byte];
	u16 solid_zero[num_byte];
	u16 dq_state = 0;
	u16 leveled_flag0 = 0;
	u16 leveled_flag1 = 0;
	u16 leveled_flag0_tmp = 0;
	u16 leveled_flag1_tmp = 0;

	for (int i = 0; i < num_byte; i++) {
		leveled_delay[i] = 0;
		solid_zero[i] = 0;
	}

	while (dly_cnt <= 255 && leveled_flag1 != byte_mask) {
		dfi_wrlvl_send_dqs_pulse();
		rdata = dfi_readl(DQ_STATUS_ALL_2);
		dq_state = ((rdata >> 16) & byte_mask); //Bit[24:16] is phy_dq_state_bytewise_or

		for (int i = 0; i < num_byte; i++) {
			if ((((leveled_flag1 >> i) & 1) == 1)) { //Skip leveld Byte
				continue;
			}
			if (((dq_state >> i) & 1) == 1) { //Get '1' at Byte[i]
				// Got enough consecutive '1'
				if ( ((leveled_flag1_tmp >> i) & 1) == 1 && dly_cnt-leveled_delay[i]>=10) {
					leveled_flag1 = (leveled_flag1 | (1 << i)); //flag current leveling byte as leveled
					if (debug)
						utils_dfi_trace(LOG_ERR, 0xbb47, "DFI write leveling - Byte %d leveled at delay 0x%x.\n", i, leveled_delay[i]);

					continue; // current byte is trained, move to the next one
				}
				// filter out noise '0'
				if ( ((leveled_flag0_tmp >> i) & 1) == 1 && dly_cnt-solid_zero[i]<10 ) {
					leveled_flag0_tmp = (leveled_flag0_tmp & ~(1<<i));
					solid_zero[i] = 0;
				}
				// mark first '1', when enough consecutive '0's have been seen.
				if ( ((leveled_flag1_tmp >> i) & 1) == 0 && ((leveled_flag0 >> i) & 1) == 1 ) {
					leveled_flag1_tmp = (leveled_flag1_tmp | (1 << i));
					leveled_delay[i] = dly_cnt;
				}
			}else{//Get '0' at Byte[i]
				// Got enought consecutive '0'
				if ( ((leveled_flag0_tmp >> i) & 1) == 1 && dly_cnt-solid_zero[i]>=10) {
					leveled_flag0 = (leveled_flag0 | (1 << i));
				}
				// filter out noise '1'
				if ( ((leveled_flag1_tmp >> i) & 1) == 1 && dly_cnt-leveled_delay[i]<10 ) {
					leveled_flag1_tmp = (leveled_flag1_tmp & ~(1<<i));
					leveled_delay[i] = 0;
				}
				// mark first '0'
				if ( ((leveled_flag0_tmp >> i) & 1) == 0 ) {
					leveled_flag0_tmp = (leveled_flag0_tmp | (1 << i));
					solid_zero[i] = dly_cnt;
				}
			}
		}

		dly_cnt += 1; //increase dly_cnt
		if (dly_cnt <= 255) {
			dfi_wrlvl_update_dqs_wr(dly_cnt);
		}

	}

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0x61c7, "DFI write leveling - Sweep finished\n");


	// *************************************************************************
	// Write leveled output delay values back to registers
	// *************************************************************************

	//Disable shadow register simultaneous write
	ctrl1.b.simul_oddly_dqs = 0;
	ctrl1.b.simul_oddly_dqdm = 0;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	if ((leveled_flag1 != byte_mask) && (dly_cnt > 255)) {
		utils_dfi_trace(LOG_ERR, 0x3c9b, "DFI write leveling Error - DFI write leveling delay maxed out. Leveling status is: 0x%x.\n", dq_state);
		error = 1;
	} else {    //Write Leveling is successful.
		dfi_ck_gate_data(CK_GATE_CS_ALL);
		u8 id8 = 0;
		for (int i = 0; i < num_bit; i++) {
			id8 = (i - i % 8) / 8; // get the byte index
			if (i % 8 == 0) {

				sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
				ctrl0.b.sel_dbyte = id8;
				dfi_writel(ctrl0.all, SEL_CTRL_0);

				sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
				oddly0.b.dqs_wr_dly = leveled_delay[id8];
				dfi_writel(oddly0.all, SEL_ODDLY_0);

				sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
				oddly1.b.dm_wr_dly = leveled_delay[id8];
				dfi_writel(oddly1.all, SEL_ODDLY_1);
				if (debug >= 2)
					utils_dfi_trace(LOG_ERR, 0xa3c2, "DFI write leveling - Data byte %d, WL delay is: 0x%x.\n", id8, leveled_delay[id8]);
			}

			sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
			ctrl0.b.sel_dbit = i;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
			oddly2.b.dq_wr_dly = leveled_delay[id8];
			dfi_writel(oddly2.all, SEL_ODDLY_2);
			if (debug >= 2)
				utils_dfi_trace(LOG_ERR, 0xc885, "DFI write leveling - DQ%d, WL delay is: 0x%x.\n", i, leveled_delay[id8]);

		}
		dfi_ck_gate_data(CK_GATE_NORMAL);

		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x2b54, "DFI write leveling - Program delay finished.\n");
	}

	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		dfi_mc_force_odt_seq(cs, false);
		//restore auto odt
		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		cntl0.b.odt_termination_enb = auto_odt;
		mc0_writel(cntl0.all, ODT_CNTL_0);
	}

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0x4ef1, "DFI write leveling - Disable write leveling first in DFI then in DRAM\n");

	// Disable Write Leveling in DFI
	dfi_wrlvl_mode(false);
	// Disable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, false);

	if (ddr3_dqs_pupd != 0) {
		io_data_1_t data1 = { .all = dfi_readl(IO_DATA_1), };
		data1.b.dqs_pd = 1;
		data1.b.dqsn_pu = 0; //Active low, will be active high in Rainer
		dfi_writel(data1.all, IO_DATA_1);
		//utils_dfi_trace(LOG_ERR, 0, "DFI write leveling - Restore DDR3 DQS/DQSN PDPU\n");
	}

#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0xa82b, "dfi_wrlvl_seq_m2 - Total cycles take: %d.\n", pc_val);
#endif

	if (error == 1) {
		utils_dfi_trace(LOG_ERR, 0xb8a8, "DFI write leveling - Write leveling failed.\n");
		return -1;
	}

	for (int i = 0; i < num_byte; i++) {
		if (debug >= 1)
			utils_dfi_trace(LOG_ERR, 0x51d7, "DFI write leveling - Write leveling results: Byte[%d], delay=%d.\n", i, leveled_delay[i]);
	}

	return 0;
}

//************************************************************************************************************
//Write leveling for single device specified by target_byte
dfi_code int dfi_wrlvl_seq_single(u8 target_cs, u8 target_byte)
{
	utils_dfi_trace(LOG_ERR, 0xff2d, "DFI write leveling - start\n");
	u8 cs;

	switch (target_cs) {
	case 0:
		cs = CS0_BIT;
		break;
	case 1:
		cs = CS1_BIT;
		break;
	case 2:
		cs = CS2_BIT;
		break;
	case 3:
		cs = CS3_BIT;
		break;
	default:
		cs = CS0_BIT;
	}

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	//When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 ddr_type = mode.b.dram_type;

	u8 num_byte = 1;
	//u8 num_bit = 8;
	u16 byte_mask = 1;

	u32 rdata;

	switch (data_width) {

	case 4: //x64
		num_byte = 8;
		//num_bit = 64;
		byte_mask = 255;
		break;

	case 3: //x32
		num_byte = 4;
		//num_bit = 32;
		byte_mask = 15;
		break;

	case 2: //x16
		num_byte = 2;
		//num_bit = 16;
		byte_mask = 3;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		//num_bit = 8;
		byte_mask = 1;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		//num_bit = 8;
		byte_mask = 1;
	}

	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	if (ras.b.ecc_enb == 1) {
		//ECC is ON
		if (num_byte == 4) {
			byte_mask = 31;
		} else if (num_byte == 8) {
			byte_mask = 511;
		}
		num_byte = num_byte + 1;
	}

	byte_mask = (byte_mask & (1 << target_byte));
	utils_dfi_trace(LOG_ERR, 0x5d74, "DFI write leveling - Target CS[%d], Byte width: %d, Target Byte: %d, Byte mask 0x%x\n", target_cs, num_byte, target_byte, byte_mask);

	// Enable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, true);
	// Enable Write Leveling in DFI
	dfi_wrlvl_mode(true);

	//If ODT is enabled, then force ODT high, else don't force
	//Read out ODT Value for Target CS
	//rdata = ReadReg_MC("cs_config_odt");//bit [22:20] is ODT_value
	cs_config_sel_t config = { .all = mc0_readl(CS_CONFIG_SEL), };
	config.b.cs_config_cs_sel = target_cs;
	mc0_writel(config.all, CS_CONFIG_SEL);

	rdata = mc0_readl(CS_CONFIG_ODT);
	u8 odt_value = ((rdata >> 20) & 7); //bit [22:20] is ODT_value
	u8 auto_odt = 0;
	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		utils_dfi_trace(LOG_ERR, 0xcf55, "DFI write leveling - force ODT high\n");
		//WriteReg_MC("odt_cntl_0", "ODT_termination_enb", 0);
		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		auto_odt = cntl0.b.odt_termination_enb; //Store the original values
		cntl0.b.odt_termination_enb = 0;
		mc0_writel(cntl0.all, ODT_CNTL_0);

		dfi_mc_force_odt_seq(cs, true);
	}

	//Enable shadow register simultaneous write
	sel_ctrl_1_t ctrl1 = { .all = dfi_readl(SEL_CTRL_1), };
	ctrl1.b.simul_oddly_dqs = 1;
	ctrl1.b.simul_oddly_dqdm = 1;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	// Clear output delay registers
	u16 dly_cnt = 0;
	dfi_ck_gate_data(CK_GATE_CS_ALL);
	utils_dfi_trace(LOG_ERR, 0x8ad8, "DFI write leveling - set dly_cnt to: 0x%x.\n", dly_cnt);

	//WriteReg_DFI("SEL_ODDLY_0", "dqs_wr_dly", dly_cnt);
	sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
	oddly0.b.dqs_wr_dly = dly_cnt;
	dfi_writel(oddly0.all, SEL_ODDLY_0);

	//WriteReg_DFI("SEL_ODDLY_1", "dm_wr_dly", dly_cnt);
	sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
	oddly1.b.dm_wr_dly = dly_cnt;
	dfi_writel(oddly1.all, SEL_ODDLY_1);

	//WriteReg_DFI("SEL_ODDLY_2", "dq_wr_dly", dly_cnt);
	sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
	oddly2.b.dq_wr_dly = dly_cnt;
	dfi_writel(oddly2.all, SEL_ODDLY_2);

	dfi_ck_gate_data(CK_GATE_NORMAL);

	// *************************************************************************
	// Sweep DQS output delay against CK
	// *************************************************************************
	u16 leveled_delay[num_byte];
	u16 dq_state = 0;

	u16 leveled_flag0 = 0;
	u16 leveled_flag0_0 = 0;
	u16 leveled_flag0_1 = 0;
	u16 leveled_flag0_2 = 0;
	u16 leveled_flag1 = 0;

	u8 level_index = 0;
	u8 level_current_byte = 0;
	for (int i = 0; i < num_byte; i++)
		leveled_delay[i] = 0;

	do {    //Main sweep loop

		dfi_wrlvl_send_dqs_pulse();

		rdata = dfi_readl(DQ_STATUS_ALL_2);
		dq_state = ((rdata >> 16) & byte_mask); //Bit[24:16] is phy_dq_state_bytewise_or

		//Fetch the current leveling byte from input level order
		level_current_byte = target_byte;

		utils_dfi_trace(LOG_ERR, 0xb387, "DFI write leveling - level_current_byte = %d, current delay = %d, dq_state = 0x%x.\n", level_current_byte, dly_cnt, dq_state);
		//Check dq_state for each byte

		//Must get '0' 3 times to set leveled_flag0 to prevent error caused by jitter
		leveled_flag0_2 = leveled_flag0_1;
		leveled_flag0_1 = leveled_flag0_0;
		leveled_flag0_0 = (~dq_state); //If get '0' from DRAM, set corresponding bit
		leveled_flag0 = (leveled_flag0 | (leveled_flag0_0 & leveled_flag0_1 & leveled_flag0_2));

		if (((dq_state >> level_current_byte) & 1) == 1 && ((leveled_flag0 >> level_current_byte) & 1) == 1) { //current leveling byte get 1 from DRAM
			leveled_flag1 = (leveled_flag1 | (1 << level_current_byte)); //flag current leveling byte as leveled
			leveled_delay[level_current_byte] = dly_cnt; //record the current delay for current byte leveled_delay
			utils_dfi_trace(LOG_ERR, 0x186f, "DFI write leveling - index %d, byte %d leveled at delay 0x%x.\n", level_index, level_current_byte, dly_cnt);
		}

		//increase dly_cnt
		dly_cnt += 1;
		/*
		 utils_dfi_trace(LOG_ERR, 0, "DFI write leveling - set dly_cnt to: 0x%x.\n", dly_cnt);
		 */

		if (dly_cnt <= 255) {
			dfi_wrlvl_update_dqs_wr(dly_cnt);
		}

	} while ((leveled_flag1 != byte_mask) && (dly_cnt <= 255));

	utils_dfi_trace(LOG_ERR, 0xa790, "DFI write leveling - Sweep finished\n");

	// *************************************************************************
	// Write leveled output delay values back to registers
	// *************************************************************************

	//Disable shadow register simultaneous write
	//WriteReg_DFI("SEL_CTRL_1", "oddly_dqs", 0);
	//WriteReg_DFI("SEL_CTRL_1", "oddly_dqdm", 0);
	ctrl1.b.simul_oddly_dqs = 0;
	ctrl1.b.simul_oddly_dqdm = 0;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	if ((leveled_flag1 != byte_mask) && (dly_cnt > 255)) {
		utils_dfi_trace(LOG_ERR, 0xd3aa, "DFI write leveling Error - DFI write leveling delay maxed out. Leveling status is: 0x%x.\n", dq_state);
	} else {    //Write Leveling is successful.
		dfi_ck_gate_data(CK_GATE_CS_ALL);
		//u16 last_delay = 0;
		sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
		ctrl0.b.sel_dbyte = target_byte;
		dfi_writel(ctrl0.all, SEL_CTRL_0);

		//WriteReg_DFI("SEL_ODDLY_0", "dqs_wr_dly", leveled_delay[id8]);
		sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
		oddly0.b.dqs_wr_dly = leveled_delay[target_byte];
		dfi_writel(oddly0.all, SEL_ODDLY_0);

		//WriteReg_DFI("SEL_ODDLY_1", "dm_wr_dly", leveled_delay[id8]);
		sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
		oddly1.b.dm_wr_dly = leveled_delay[target_byte];
		dfi_writel(oddly1.all, SEL_ODDLY_1);

		utils_dfi_trace(LOG_ERR, 0xfb1b, "DFI write leveling - Data byte %d, WL delay is: 0x%x.\n", target_byte, leveled_delay[target_byte]);

		u8 bit_start = 8 * target_byte;
		u8 bit_end = 8 * (target_byte + 1);

		for (int i = bit_start; i < bit_end; i++) {

			sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
			ctrl0.b.sel_dbit = i;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
			oddly2.b.dq_wr_dly = leveled_delay[target_byte];
			dfi_writel(oddly2.all, SEL_ODDLY_2);

			utils_dfi_trace(LOG_ERR, 0x242b, "DFI write leveling - DQ%d, WL delay is: 0x%x.\n", i, leveled_delay[target_byte]);
		}
		dfi_ck_gate_data(CK_GATE_NORMAL);
		utils_dfi_trace(LOG_ERR, 0x451c, "DFI write leveling - Program delay finished.\n");
	}

	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		dfi_mc_force_odt_seq(cs, false);
		//restore auto odt
		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		cntl0.b.odt_termination_enb = auto_odt;
		mc0_writel(cntl0.all, ODT_CNTL_0);
	}

	utils_dfi_trace(LOG_ERR, 0x0f7d, "DFI write leveling - Disable write leveling first in DFI then in DRAM\n");
	// Disable Write Leveling in DFI
	dfi_wrlvl_mode(false);
	// Disable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, false);

	utils_dfi_trace(LOG_ERR, 0x5e98, "DFI write leveling - Write leveling finished.\n");
	//More ndelay
	return 0;
}
//************************************************************************************************************
//Enter (enable) or Exit (disable) Write leveing mode only
dfi_code void dfi_wrlvl_seq_debug(u8 target_cs, bool en)
{
	u8 cs;

	switch (target_cs) {
	case 0:
		cs = 1;
		utils_dfi_trace(LOG_ERR, 0xf228, "DFI write leveling - Target CS[0]\n");
		break;
	case 1:
		cs = 2;
		utils_dfi_trace(LOG_ERR, 0xb403, "DFI write leveling - Target CS[1]\n");
		break;
	case 2:
		cs = 4;
		utils_dfi_trace(LOG_ERR, 0x1287, "DFI write leveling - Target CS[2]\n");
		break;
	case 3:
		cs = 8;
		utils_dfi_trace(LOG_ERR, 0xdc22, "DFI write leveling - Target CS[3]\n");
		break;
	default:
		cs = 1;
	}

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	//When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 ddr_type = mode.b.dram_type;

	u8 num_byte = 1;
	//u8 num_bit = 8;
	u16 byte_mask = 1;

	//u32 rdata;

	switch (data_width) {
	//case 72:
	//    num_byte = 9;
	//    num_bit = 72;
	//    byte_mask = 511;
	//    break;

	case 4: //x64
		num_byte = 8;
		//num_bit = 64;
		byte_mask = 255;
		break;

		//case 40:
		//    num_byte = 5;
		//    num_bit = 40;
		//    byte_mask = 31;
		//    break;

	case 3: //x32
		num_byte = 4;
		//num_bit = 32;
		byte_mask = 15;
		break;

	case 2: //x16
		num_byte = 2;
		//num_bit = 16;
		byte_mask = 3;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		//num_bit = 8;
		byte_mask = 1;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		//num_bit = 8;
		byte_mask = 1;
	}

	if (en) {
		utils_dfi_trace(LOG_ERR, 0x0194, "DFI write leveling - start\n");

		utils_dfi_trace(LOG_ERR, 0x2626, "DFI write leveling - Byte width: %d, Byte mask 0x%x\n", num_byte, byte_mask);
		// Enable Write Leveling in DRAM
		dfi_mc_wrlvl_mode(ddr_type, cs, en);
		// Enable Write Leveling in DFI
		dfi_wrlvl_mode(en);

		//Enable shadow register simultaneous write
		sel_ctrl_1_t ctrl1 = { .all = dfi_readl(SEL_CTRL_1), };
		ctrl1.b.simul_oddly_dqs = 1;
		ctrl1.b.simul_oddly_dqdm = 1;
		dfi_writel(ctrl1.all, SEL_CTRL_1);

		// Clear output delay registers
		u16 dly_cnt = 0;
		dfi_ck_gate_data(CK_GATE_CS_ALL);
		utils_dfi_trace(LOG_ERR, 0x427a, "DFI write leveling - set dly_cnt to: 0x%x.\n", dly_cnt);

		//WriteReg_DFI("SEL_ODDLY_0", "dqs_wr_dly", dly_cnt);
		sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
		oddly0.b.dqs_wr_dly = dly_cnt;
		dfi_writel(oddly0.all, SEL_ODDLY_0);

		//WriteReg_DFI("SEL_ODDLY_1", "dm_wr_dly", dly_cnt);
		sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
		oddly1.b.dm_wr_dly = dly_cnt;
		dfi_writel(oddly1.all, SEL_ODDLY_1);

		//WriteReg_DFI("SEL_ODDLY_2", "dq_wr_dly", dly_cnt);
		sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
		oddly2.b.dq_wr_dly = dly_cnt;
		dfi_writel(oddly2.all, SEL_ODDLY_2);

		dfi_ck_gate_data(CK_GATE_NORMAL);

		ctrl1.b.simul_oddly_dqs = 0;
		ctrl1.b.simul_oddly_dqdm = 0;
		dfi_writel(ctrl1.all, SEL_CTRL_1);
	} else {
		utils_dfi_trace(LOG_ERR, 0x8ea7, "DFI write leveling - Disable write leveling first in DFI then in DRAM\n");
		// Disable Write Leveling in DFI
		dfi_wrlvl_mode(en);
		// Disable Write Leveling in DRAM
		dfi_mc_wrlvl_mode(ddr_type, cs, en);
	}
}
//************************************************************************************************************

//Write leveling for High Speed M.2 board configurations, no byte order is required.
//The skew between Farthest DRAM byte and tCK MUST be less than one tCK cycle
dfi_code int dfi_wrlvl_retrain_seq(u8 target_cs, u8 debug)
{
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	u8 error = 0;
	u8 cs;

	switch (target_cs) {
	case 0:
		cs = CS0_BIT;
		break;
	case 1:
		cs = CS1_BIT;
		break;
	case 2:
		cs = CS2_BIT;
		break;
	case 3:
		cs = CS3_BIT;
		break;
	default:
		cs = CS0_BIT;
	}

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	//When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 ddr_type = mode.b.dram_type;

	u8 num_byte = 1;
	u8 num_bit = 8;
	u16 byte_mask = 1;

	u32 rdata;

	switch (data_width) {
	case 4: //x64
		num_byte = 8;
		num_bit = 64;
		byte_mask = 255;
		break;

	case 3: //x32
		num_byte = 4;
		num_bit = 32;
		byte_mask = 15;
		break;

	case 2: //x16
		num_byte = 2;
		num_bit = 16;
		byte_mask = 3;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
		byte_mask = 1;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		num_bit = 8;
		byte_mask = 1;
	}

	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	if (ras.b.ecc_enb == 1) {
		//ECC is ON
		if (num_byte == 4) {
			byte_mask = 31;
		} else if (num_byte == 8) {
			byte_mask = 511;
		}
		num_byte = num_byte + 1;
		num_bit = num_bit + 8;
	}

	if (debug >= 1)
		utils_dfi_trace(LOG_ERR, 0x6df6, "DFI write leveling - Write leveling retrain start, target CS[%d], byte width: %d, byte mask: 0x%x\n", target_cs, num_byte, byte_mask);

	//DDR3 need disable DQS PU/PD
	u8 ddr3_dqs_pupd = 0;
	if (ddr_type == MC_DDR3) {
		io_data_1_t data1 = { .all = dfi_readl(IO_DATA_1), };
		ddr3_dqs_pupd = ((data1.all >> 1) & 3); //record the original value
		data1.b.dqs_pd = 0;
		data1.b.dqsn_pu = 1; //turn off pu, active low, will be active high in next Rainer
		dfi_writel(data1.all, IO_DATA_1);
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x8606, "DFI write leveling - Disable DDR3 DQS/DQSN PDPU\n");
	}

	// LPDDR4 needs double DQS pulse
	if (ddr_type == MC_LPDDR4) {
		lvl_wrlvl_0_t wrlvl = { .all = dfi_readl(LVL_WRLVL_0), };
		wrlvl.b.wrlvl_pulse_width = 1;
		dfi_writel(wrlvl.all, LVL_WRLVL_0);
	}

	// Enable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, true);
	// Enable Write Leveling in DFI
	dfi_wrlvl_mode(true);

	//If ODT is enabled, then force ODT high, else don't force
	//Read out ODT Value for Target CS
	cs_config_sel_t config = { .all = mc0_readl(CS_CONFIG_SEL), };
	config.b.cs_config_cs_sel = target_cs;
	mc0_writel(config.all, CS_CONFIG_SEL);

	rdata = mc0_readl(CS_CONFIG_ODT);
	u8 odt_value = ((rdata >> 20) & 7); //bit [22:20] is ODT_value (RTT_NOM)
	u8 auto_odt = 0;
	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0xaf3d, "DFI write leveling - force ODT high\n");

		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		auto_odt = cntl0.b.odt_termination_enb; //Store the original values
		cntl0.b.odt_termination_enb = 0;
		mc0_writel(cntl0.all, ODT_CNTL_0);

		dfi_mc_force_odt_seq(cs, true);

	}

	//Enable shadow register simultaneous write
	sel_ctrl_1_t ctrl1 = { .all = dfi_readl(SEL_CTRL_1), };
	ctrl1.b.simul_oddly_dqs = 1;
	ctrl1.b.simul_oddly_dqdm = 1;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	// Clear output delay registers
	u16 dly_cnt = 0;
	dfi_ck_gate_data(CK_GATE_CS_ALL);

	sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
	oddly0.b.dqs_wr_dly = dly_cnt;
	dfi_writel(oddly0.all, SEL_ODDLY_0);

	sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
	oddly1.b.dm_wr_dly = dly_cnt;
	dfi_writel(oddly1.all, SEL_ODDLY_1);

	sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
	oddly2.b.dq_wr_dly = dly_cnt;
	dfi_writel(oddly2.all, SEL_ODDLY_2);

	dfi_ck_gate_data(CK_GATE_NORMAL);

	// *************************************************************************
	// Sweep DQS output delay against CK
	// *************************************************************************
	u16 leveled_delay[num_byte];
	u16 solid_zero[num_byte];
	u16 dq_state = 0;
	u16 leveled_flag0 = 0;
	u16 leveled_flag1 = 0;
	u16 leveled_flag0_tmp = 0;
	u16 leveled_flag1_tmp = 0;

	for (int i = 0; i < num_byte; i++) {
		leveled_delay[i] = 0;
		solid_zero[i] = 0;
	}

	while (dly_cnt <= 255 && leveled_flag1 != byte_mask) {
		dfi_wrlvl_send_dqs_pulse();
		rdata = dfi_readl(DQ_STATUS_ALL_2);
		dq_state = ((rdata >> 16) & byte_mask); //Bit[24:16] is phy_dq_state_bytewise_or

		for (int i = 0; i < num_byte; i++) {
			if ((((leveled_flag1 >> i) & 1) == 1)) { //Skip leveld Byte
				continue;
			}
			if (((dq_state >> i) & 1) == 1) { //Get '1' at Byte[i]
				// Got enough consecutive '1'
				if ( ((leveled_flag1_tmp >> i) & 1) == 1 && dly_cnt-leveled_delay[i]>=10) {
					leveled_flag1 = (leveled_flag1 | (1 << i)); //flag current leveling byte as leveled
					if (debug >= 2)
						utils_dfi_trace(LOG_ERR, 0x2576, "DFI write leveling - Byte %d leveled at delay 0x%x.\n", i, leveled_delay[i]);

					continue; // current byte is trained, move to the next one
				}
				// filter out noise '0'
				if ( ((leveled_flag0_tmp >> i) & 1) == 1 && dly_cnt-solid_zero[i]<10 ) {
					leveled_flag0_tmp = (leveled_flag0_tmp & ~(1<<i));
					solid_zero[i] = 0;
				}
				// mark first '1', when enough consecutive '0's have been seen.
				if ( ((leveled_flag1_tmp >> i) & 1) == 0 && ((leveled_flag0 >> i) & 1) == 1 ) {
					leveled_flag1_tmp = (leveled_flag1_tmp | (1 << i));
					leveled_delay[i] = dly_cnt;
				}
			}else{//Get '0' at Byte[i]
				// Got enought consecutive '0'
				if ( ((leveled_flag0_tmp >> i) & 1) == 1 && dly_cnt-solid_zero[i]>=10) {
					leveled_flag0 = (leveled_flag0 | (1 << i));
				}
				// filter out noise '1'
				if ( ((leveled_flag1_tmp >> i) & 1) == 1 && dly_cnt-leveled_delay[i]<10 ) {
					leveled_flag1_tmp = (leveled_flag1_tmp & ~(1<<i));
					leveled_delay[i] = 0;
				}
				// mark first '0'
				if ( ((leveled_flag0_tmp >> i) & 1) == 0 ) {
					leveled_flag0_tmp = (leveled_flag0_tmp | (1 << i));
					solid_zero[i] = dly_cnt;
				}
			}
		}

		dly_cnt += 1; //increase dly_cnt
		if (dly_cnt <= 255) {
			dfi_wrlvl_update_dqs_wr(dly_cnt);
		}

	}

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0xcdc4, "DFI write leveling - Sweep finished\n");


	// *************************************************************************
	// Write leveled output delay values back to registers
	// *************************************************************************

	//Disable shadow register simultaneous write
	ctrl1.b.simul_oddly_dqs = 0;
	ctrl1.b.simul_oddly_dqdm = 0;
	dfi_writel(ctrl1.all, SEL_CTRL_1);

	if ((leveled_flag1 != byte_mask) && (dly_cnt > 255)) {
		utils_dfi_trace(LOG_ERR, 0x844a, "DFI write leveling Error - DFI write leveling delay maxed out. Leveling status is: 0x%x.\n", dq_state);
		error = 1;
	} else {    //Write Leveling is successful.
		dfi_ck_gate_data(CK_GATE_CS_ALL);
		u8 id8 = 0;
		for (int i = 0; i < num_bit; i++) {
			id8 = (i - i % 8) / 8; // get the byte index
			if (i % 8 == 0) {

				sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
				ctrl0.b.sel_dbyte = id8;
				dfi_writel(ctrl0.all, SEL_CTRL_0);

				sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
				oddly0.b.dqs_wr_dly = leveled_delay[id8];
				dfi_writel(oddly0.all, SEL_ODDLY_0);

				sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
				oddly1.b.dm_wr_dly = leveled_delay[id8];
				dfi_writel(oddly1.all, SEL_ODDLY_1);
				if (debug >= 2)
					utils_dfi_trace(LOG_ERR, 0x460a, "DFI write leveling - Data byte %d, WL delay is: 0x%x.\n", id8, leveled_delay[id8]);

			}

			sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
			ctrl0.b.sel_dbit = i;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };
			oddly2.b.dq_wr_dly = leveled_delay[id8];
			dfi_writel(oddly2.all, SEL_ODDLY_2);
			if (debug >= 2)
				utils_dfi_trace(LOG_ERR, 0x1ab2, "DFI write leveling - DQ%d, WL delay is: 0x%x.\n", i, leveled_delay[id8]);

		}
		dfi_ck_gate_data(CK_GATE_NORMAL);

		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0xce9a, "DFI write leveling - Program delay finished.\n");

	}

	if (odt_value != 0 && ddr_type != MC_LPDDR4) {
		dfi_mc_force_odt_seq(cs, false);
		//restore auto odt
		odt_cntl_0_t cntl0 = { .all = mc0_readl(ODT_CNTL_0), };
		cntl0.b.odt_termination_enb = auto_odt;
		mc0_writel(cntl0.all, ODT_CNTL_0);
	}

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0xc4e4, "DFI write leveling - Disable write leveling first in DFI then in DRAM\n");

	// Disable Write Leveling in DFI
	dfi_wrlvl_mode(false);
	// Disable Write Leveling in DRAM
	dfi_mc_wrlvl_mode(ddr_type, cs, false);

	if (ddr3_dqs_pupd != 0) {
		io_data_1_t data1 = { .all = dfi_readl(IO_DATA_1), };
		data1.b.dqs_pd = 1;
		data1.b.dqsn_pu = 0; //Active low, will be active high in Rainer
		dfi_writel(data1.all, IO_DATA_1);
		//utils_dfi_trace(LOG_ERR, 0, "DFI write leveling - Restore DDR3 DQS/DQSN PDPU\n");
	}

#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0x702e, "dfi_wrlvl_retrain_seq - Total cycles take: %d.\n", pc_val);
#endif

	if (error == 1) {
		utils_dfi_trace(LOG_ERR, 0xfa02, "DFI write leveling retrain - Write leveling failed.\n");
		return -1;
	}

	for (int i = 0; i < num_byte; i++) {
		if (debug >= 1)
			utils_dfi_trace(LOG_ERR, 0x9295, "DFI write leveling retrain - Write leveling results: Byte[%d], delay=%d.\n", i, leveled_delay[i]);
	}

	return 0;
}

#if (ENABLE_DFI_VALIDATION_DEBUG == 1)
// Validation-use code
// Loop dfi_wrlvl_loop multiple times
// Set summary to '1' to record Write leveling results for each Byte during loop.
// Can run this test for a large numer of loops while applying external changes such as temperature
#define ASIZE 10
dfi_code void dfi_wrlvl_loop(u8 target_cs, u32 loop, u8 summary)
{
	u8 debug = 0;
	int cur_res = DFI_TRAIN_PASS;

#if !(defined(M2) || defined(U2))
	u8 level_tacoma[] = { 6, 7, 5, 4, 2, 3, 1, 0 };
	u8 level_tacoma_ecc[] = { 6, 7, 5, 4, 8, 2, 3, 1, 0 };
	u8 level_rainier[] = { 2, 3, 0, 1};
	u8 level_rainier_ecc[] = { 2, 3, 0, 1, 4};
	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
#endif

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
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

	u32 leveled_delay[num_byte][256];
	for (u8 i = 0; i < num_byte; i++) {
		for(u16 j = 0; j < 256; j++) {
			leveled_delay[i][j] = 0;
		}
	}

	utils_dfi_trace(LOG_ERR, 0x0396, "dfi_wrlvl_loop starts (prints a status message every 10000 loops).\n");

	for (u32 l = 0; l < loop; l++) {
		if (l%10000 == 0)
			utils_dfi_trace(LOG_ERR, 0xeda5, "On loop %d...\n", l);

		#if defined(M2) || defined(U2)
			cur_res = dfi_wrlvl_seq_m2(target_cs, debug);
		#else // EVB
			if (data_width == MC_BUS_WIDTH_64) {
				if (ras.b.ecc_enb)
					cur_res = dfi_wrlvl_seq_hs(target_cs, level_tacoma_ecc, debug);
				else
					cur_res = dfi_wrlvl_seq_hs(target_cs, level_tacoma, debug);
			} else {
				if (ras.b.ecc_enb)
					cur_res = dfi_wrlvl_seq_hs(target_cs, level_rainier_ecc, debug);
				else
					cur_res = dfi_wrlvl_seq_hs(target_cs, level_rainier, debug);
			}
		#endif //M.2/U.2/EVB
		if (cur_res != DFI_TRAIN_PASS){
			utils_dfi_trace(LOG_ERR, 0xe1bc, "dfi_wrlvl_loop failed on loop = %d.\n", l);
			return;
		}

		if (summary != 0) {
			for(u8 n = 0; n < num_byte; n++) {
				sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
				ctrl0.b.sel_dbyte = n;
				dfi_writel(ctrl0.all, SEL_CTRL_0);
				sel_oddly_0_t oddly0 = { .all = dfi_readl(SEL_ODDLY_0), };
				leveled_delay[n][oddly0.b.dqs_wr_dly] ++;
			}
		}
	}

	if (summary != 0){
		for(u8 n = 0; n < num_byte; n++) {
			for (u16 d = 0; d < 256; d++) {
				if (leveled_delay[n][d] != 0)
					utils_dfi_trace(LOG_ERR, 0xb832, "Write leveling result = %d for Byte %d happned %d out of %d.\n", d, n, leveled_delay[n][d], loop);
			}
		}
	}

	utils_dfi_trace(LOG_ERR, 0x2c99, "dfi_wrlvl_loop end.\n");
	return;
}
#endif

/*! @} */