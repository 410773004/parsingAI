
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
/*! \file dfi_rdlvl_gate_seq.c
 * @brief dfi read level gate sequence APIs
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
#include "dfi_reg.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "stdio.h"
#define __FILEID__ dfirdlvlgate
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
dfi_code int dfi_rdlvl_gate_rd_fifo_clr()
{
	u32 rdata;

	lvl_all_wo_0_t lvlwo = { .all = dfi_readl(LVL_ALL_WO_0), };
	lvlwo.b.rdfifo_clear = 1;
	dfi_writel(lvlwo.all, LVL_ALL_WO_0);
	//Auto Clear, no need to write back '0'

	//Poll for done
	do {
		ndelay(100);
		rdata = dfi_readl(LVL_ALL_RO_0);
	} while (((rdata >> 3) & 1) != 1); //Bit[3] is rdfifo_clear_done

	return 0;
}

dfi_code int dfi_rdlvl_gate_seq(u8 target_cs, u8 quick_sweep, u8 phase_repeat_cnt_max, u8 debug)
{
	u8 tap_delay_start_threshold = 8;
	u16 dly_cnt_inc = 1;
	u16 dly_cnt;
	u16 dly_cnt_max;
	u8 cs;
	u8 error = 0;

	if (quick_sweep == 1) {
		tap_delay_start_threshold = 2;
		dly_cnt_inc = 4;
	}

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
	u16 byte_mask = 1;

	switch (data_width) {
	case 4:
		num_byte = 8;
		byte_mask = 255;
		break;

	case 3:
		num_byte = 4;
		byte_mask = 15;
		break;

	case 2:
		num_byte = 2;
		byte_mask = 3;
		break;

	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		byte_mask = 1;
		break;

	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
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

	if (debug >= 1)
		utils_dfi_trace(LOG_ERR, 0xb5c6, "DFI read gate - Target CS[%d], Byte width: %d, Byte mask 0x%x\n", target_cs, num_byte, byte_mask);

	//LPDDR4 need dll update due to CA training frequency change
	if (ddr_type == MC_LPDDR4) {
		dfi_dll_update();
		ndelay(1000);
	}

	//DFI setup, force receiver ON
	in_ctrl_0_t in0 = { .all = dfi_readl(IN_CTRL_0), };
	in0.b.force_dqs_rcv_en = 1;
	dfi_writel(in0.all, IN_CTRL_0);

	//Read Gate (STR Gate), configurations
	//strgt_mode = 0 for DDR3, LP4 with static preamble;
	//strgt_mode = 1 for DDR4;
	//strgt_mode = 2 for LP4 with toggle preamble;
	if (ddr_type == MC_DDR3) { //DDR3
		strgt_ctrl_0_t strgt = { .all = dfi_readl(STRGT_CTRL_0), };
		strgt.b.strgt_win_size = 1; //BL-1
		strgt.b.strgt_mode = 0; //Mode 0 for DDR3
		strgt.b.strgt_en = 1;
		dfi_writel(strgt.all, STRGT_CTRL_0);

		dfi_mc_ddrx_mpr_mode_seq(CS0_BIT, true); //Enable MPR mode for DDR3/4

	} else if (ddr_type == MC_DDR4) { //DDR4
		strgt_ctrl_0_t strgt = { .all = dfi_readl(STRGT_CTRL_0), };
		strgt.b.strgt_win_size = 0; //0:BL, 1:BL-1, 2:BL+1
		strgt.b.strgt_mode = 1; //Mode 1 for DDR4
		strgt.b.strgt_en = 1;
		dfi_writel(strgt.all, STRGT_CTRL_0);

		dfi_mc_ddrx_mpr_mode_seq(CS0_BIT, true); //Enable MPR mode for DDR3/4

	} else if (ddr_type == MC_LPDDR4) { //LPDDR4
		strgt_ctrl_0_t strgt = { .all = dfi_readl(STRGT_CTRL_0), };
		strgt.b.strgt_win_size = 1; //BL-1, assume static preamble
		strgt.b.strgt_mode = 0; //Mode 0 for LPDDR4 static preamble
		strgt.b.strgt_en = 1;
		dfi_writel(strgt.all, STRGT_CTRL_0);

	}

	//Clear Phase delay and Tap delay for all PHY byte
	//Enable shadow register simultaneous write for all bytes
	sel_ctrl_1_t ctrl1 = { .all = dfi_readl(SEL_CTRL_1), };
	ctrl1.b.simul_strgt_0 = 1;
	dfi_writel(ctrl1.all, SEL_CTRL_1);
	sel_strgt_0_t strgt0 = { .all = dfi_readl(SEL_STRGT_0), };
	strgt0.b.strgt_phase_dly = 0;
	// set a minimum Tap delay value for Phase delay sweep, 48 here
	// starting value of tap delays when first sweeping phase delay
	// then, when sweeping tap, first reset tap delay to 0
	// this allows for a larger tap window, in case that the right side windows is very close to [tap_start_val]
	// for example, if tap_start_val=0, and the window ends at tap_start_val=5, then the window is only 5-wide...
	u8 tap_start_val_for_phase = 128;
	strgt0.b.strgt_tap_dly = tap_start_val_for_phase;
	dfi_writel(strgt0.all, SEL_STRGT_0);

	//Sweep through the Phase Delay
	u8 phase_start_val[num_byte];
	u8 phase_repeat_cnt = 0;

	u32 rdata;
	u16 rd_rise_status_byte = 0; //Status for each byte
	u16 rd_fall_status_byte = 0; //Status for each byte
	u16 phase_started = 0; //Status for each byte
	u8 phase_start_done = 0;

	dly_cnt = 0;
	dly_cnt_max = 28; //Delay strobe gate in increments of 0.5 tCK, Range from 0 to 28, test case may set it to smaller values

	for (int i = 0; i < num_byte; i++) {
		phase_start_val[i] = 0;
	}

	//Main sweep loop for Phase Delay
	do {
		//Clear Read FIFO
		dfi_rdlvl_gate_rd_fifo_clr();

		in0.b.rd_rstb_force = 1; //DFI reset is active HIGH !!!
		dfi_writel(in0.all, IN_CTRL_0);
		in0.b.rd_rstb_force = 0;
		dfi_writel(in0.all, IN_CTRL_0);

		//MPC Read and Compare
		dfi_rdlvl_rd_comp_seq(cs, ddr_type);
		//Read DQ bytewise read compare status
		rdata = dfi_readl(RD_COMP_STATUS_ALL_3);
		rd_rise_status_byte = (rdata & byte_mask);
		rd_fall_status_byte = ((rdata >> 16) & byte_mask);
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x4ea2, "DFI read gate - Phase count: %d, rd_comp status rise: 0x%x, fall: 0x%x (1 indicates byte pass), byte_mask: 0x%x.\n", dly_cnt, rd_rise_status_byte, rd_fall_status_byte, byte_mask);

		for (int i = 0; i < num_byte; i++) {
			if (((phase_started >> i) & 1) == 0) { //bit i of phase_started is 0, passing phase for byte i has not arrived
				if (((rd_rise_status_byte >> i) & 1) == 1 && ((rd_fall_status_byte >> i) & 1) == 1) { //Passed Write Read Compare for Byte i
					phase_started = (phase_started | (1 << i)); //Set bit i of phase_started to 1
					phase_start_val[i] = dly_cnt; //record current dly_cnt as start point of passing phase delay
					if (debug >= 2)
						utils_dfi_trace(LOG_ERR, 0xa98f, "DFI read gate - Byte %d, phase start point found @ %d.\n", i, dly_cnt);
				}
			}
		}
		//repeat current Phase Delay setting, until it reaches phase_repeat_cnt_max
		//phase_repeat_cnt_max is input for function dfi_rdlvl_gate_seq
		if (phase_repeat_cnt == phase_repeat_cnt_max) {
			phase_repeat_cnt = 0;
		} else {
			phase_repeat_cnt += 1;
		}

		//Inc Phase delay when current Phase setting has been repeated phase_repeat_cnt_max times
		//AND the passing phase for the target byte has not arrived
		if (phase_repeat_cnt == 0) {
			dly_cnt += 1;
			strgt0.b.strgt_phase_dly = dly_cnt;
			dfi_writel(strgt0.all, SEL_STRGT_0);
		}

		//Check if all bytes have passing phase delay
		if ((phase_started & byte_mask) == byte_mask) {
			phase_start_done = 1;
		}

	} while ((dly_cnt <= dly_cnt_max) && (phase_start_done != 1));
	//End of main sweep loop for Phase Delay

	//Disable shadow register simultaneous write for all bytes
	ctrl1.b.simul_strgt_0 = 0;
	dfi_writel(ctrl1.all, SEL_CTRL_1);
	ndelay(1000);

	sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };

	if ((dly_cnt > dly_cnt_max) && (phase_start_done == 0)) {
		utils_dfi_trace(LOG_ERR, 0xf235, "Warning - DFI read gate Phase delay reached max, but no start point is detected for at least one byte. Set Phase delay to max of %d\n", dly_cnt_max);

		for (int i = 0; i < num_byte; i++) {
			phase_start_val[i] = dly_cnt_max;
		}

	}
	for (int i = 0; i < num_byte; i++) {
		ctrl0.b.sel_dbyte = i;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		strgt0.b.strgt_phase_dly = phase_start_val[i];
		strgt0.b.strgt_tap_dly = 0;
		dfi_writel(strgt0.all, SEL_STRGT_0);
		if (debug >= 1)
			utils_dfi_trace(LOG_ERR, 0x92f1, "DFI read gate - Byte[%d]: phase start value = %d.\n", i, strgt0.b.strgt_phase_dly);

	}

	//Sweep tap delay
	dly_cnt = 0;
	dly_cnt_max = 255; //test case may set it to smaller values 255

	u16 tap_started = 0; //Status for each byte
	u16 tap_ended = 0; //Status for each byte

	u16 tap_start_cnt[num_byte];
	u16 tap_start_val[num_byte];
	u16 tap_end_val[num_byte];
	u16 tap_midpoint = 0;
	u16 tap_range = 0;

	u8 tap_end_done = 0;
	u8 tap_start_done = 0;

	for (int i = 0; i < num_byte; i++) {
		tap_start_cnt[i] = 0;
	}

	//Main loop for Tap Delay sweep
	do {
		dfi_rdlvl_gate_rd_fifo_clr();

		in0.b.rd_rstb_force = 1; //DFI reset is active HIGH !!!
		dfi_writel(in0.all, IN_CTRL_0);
		in0.b.rd_rstb_force = 0;
		dfi_writel(in0.all, IN_CTRL_0);

		dfi_rdlvl_rd_comp_seq(cs, ddr_type);

		rdata = dfi_readl(RD_COMP_STATUS_ALL_3);
		rd_rise_status_byte = (rdata & byte_mask);
		rd_fall_status_byte = ((rdata >> 16) & byte_mask);
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0xee25, "DFI read gate - Tap count: %d, rd_comp_sts_rise: 0x%x, fall: 0x%x (1 indicates byte pass).\n", dly_cnt, rd_rise_status_byte, rd_fall_status_byte);

		//Sweep logic
		for (int i = 0; i < num_byte; i++) {
			if (((tap_started >> i) & 1) == 0) { //bit i of tap_started is 0, passing Tap Delay for byte i has not arrived
				if (((rd_rise_status_byte >> i) & 1) == 1 && ((rd_fall_status_byte >> i) & 1) == 1) { //Passed Write Read Compare for Byte i
					if (tap_start_cnt[i] == 0) { //first pass
						tap_start_val[i] = dly_cnt; //record current dly_cnt as start point of passing tap delay
						if (debug >= 2)
							utils_dfi_trace(LOG_ERR, 0x5423, "DFI read gate - Byte %d tap delay, find start point, first pass @ %d.\n", i, dly_cnt);
					}
					tap_start_cnt[i] += 1;
					if (debug >= 2)
						utils_dfi_trace(LOG_ERR, 0x4b55, "DFI read gate - Byte %d tap delay, find start point, start count is: %d.\n", i, tap_start_cnt[i]);
					if (tap_start_cnt[i] >= tap_delay_start_threshold) {
						tap_started = (tap_started | (1 << i)); //Set bit i  of tap_started to 1
						if (debug >= 2)
							utils_dfi_trace(LOG_ERR, 0xb362, "DFI read gate - Byte %d tap delay, found start point: %d, dly_cnt @ %d.\n", i, tap_start_val[i], dly_cnt);
					}
				} else { //Failed Write Read Compare for Byte i
					tap_start_cnt[i] = 0;
					if (debug >= 2)
						utils_dfi_trace(LOG_ERR, 0xb952, "DFI read gate - Byte %d tap delay, find start point, rd_comp did not pass @ %d.\n", i, dly_cnt);
				}
			} else if (((tap_ended >> i) & 1) == 0) { //Tap delay passing window not yet ended
				if (((rd_rise_status_byte >> i) & 1) == 0 || ((rd_fall_status_byte >> i) & 1) == 0) { //Failed Write Read Compare for Byte i
					tap_end_val[i] = dly_cnt; //End of Tap delay passing window
					tap_ended = (tap_ended | (1 << i)); //Set bit i  of tap_ended to 1
					if (debug >= 2)
						utils_dfi_trace(LOG_ERR, 0xad4a, "DFI read gate - Byte %d tap delay, found end point: %d.\n", i, dly_cnt);
				}
			}
		}

		//inc tap delay on all bytes
		if (dly_cnt + dly_cnt_inc > dly_cnt_max) {
			for (int i = 0; i < num_byte; i++) {
				if (((tap_ended >> i) & 1) == 0) {
					tap_end_val[i] = dly_cnt; //force the end of passing window
					tap_ended = (tap_ended | (1 << i));
				}
			}
		}

		dly_cnt += dly_cnt_inc;
		//Program Tap Delay
		for (int i = 0; i < num_byte; i++) {
			ctrl0.b.sel_dbyte = i;
			dfi_writel(ctrl0.all, SEL_CTRL_0);
			strgt0.all = dfi_readl(SEL_STRGT_0);
			strgt0.b.strgt_tap_dly = dly_cnt;
			dfi_writel(strgt0.all, SEL_STRGT_0);
		}

		//Check if all bytes have a passing window for Tap Delay, end of window
		if ((tap_ended & byte_mask) == byte_mask) {
			tap_end_done = 1;
		}

	} while (dly_cnt <= dly_cnt_max && tap_end_done != 1);
	//End of main loop for Tap Delay sweeping

	ndelay(1000);

	//Check if all bytes have a passing window for Tap Delay, start of window
	if ((tap_started & byte_mask) == byte_mask) {
		tap_start_done = 1;
	}

	if (tap_start_done == 0) {
		utils_dfi_trace(LOG_ERR, 0x25dd, "Error - DFI read gate tap delay reached max of %d, but no start point is detected for at least one byte.\n", dly_cnt_max);
		utils_dfi_trace(LOG_ERR, 0x6940, "Use default receiver.\n");
		strgt_ctrl_0_t strgt = { .all = dfi_readl(STRGT_CTRL_0), };
		strgt.b.strgt_en = 0; // If QS Gate training failed, use single ended receiver
		dfi_writel(strgt.all, STRGT_CTRL_0);
		error = 1;
	} else {
		for (int i = 0; i < num_byte; i++) {

			ctrl0.b.sel_dbyte = i;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			strgt0.all = dfi_readl(SEL_STRGT_0);

			//midpoint Calculation
			tap_midpoint = (tap_end_val[i] >> 1) + (tap_start_val[i] >> 1);
			tap_range = tap_end_val[i] - tap_start_val[i];

			strgt0.b.strgt_tap_dly = tap_midpoint;
			dfi_writel(strgt0.all, SEL_STRGT_0);
			if (debug >= 1) {
				utils_dfi_trace(LOG_ERR, 0x6d5d, "DFI read gate - Byte %d tap midpoint = 0x%x.\n", i, tap_midpoint);
				utils_dfi_trace(LOG_ERR, 0xf537, "DFI read gate - Byte %d tap range = 0x%x.\n", i, tap_range);
			}

		}

		ndelay(100);
	}

	//Disable Read Preamble Training for DDR4 and LP4
	if (ddr_type == MC_LPDDR4) {
		device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
		config.b.read_preamble_training = 0;
		mc0_writel(config.all, DEVICE_CONFIG_TRAINING);

		dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cs);
		ndelay(1000);
	}

	//Finish read gate leveling
	//Exit MPR mode for DDR3/4
	if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4)
		dfi_mc_ddrx_mpr_mode_seq(CS0_BIT, false);

	//diable force receiver ON
	in0.b.force_dqs_rcv_en = 0;
	dfi_writel(in0.all, IN_CTRL_0);

	ndelay(100);

	return -error;
}
/*! @} */