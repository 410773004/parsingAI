
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
/*! \file dfi_rdlvl_seq.c
 * @brief dfi read level sequence APIs
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
#include "misc.h"
#define __FILEID__ dfirdlvlseq
#include "trace.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
// # of bytes to compare for each test. Vary from about 128 to 4096
// For TSIZE_WIND, a smaller value actually yields more variance
#define TSIZE_FULL 2048
#define TSIZE_FAST 128
#define TSIZE_WIND 128

// +2 to HW-trained rdlat value
#define RDLAT_DELAY 1

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef struct _dll_lvl_res_t{
	u8 phase0[9];
	u8 phase1[9];
	u8 worst_range;
} dll_lvl_res_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Extern Data or Functions declaration:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
norm_ps_code int dfi_rdlvl_rdlat_start(u8 cs, u8 ddr_type, u8 debug)
{

	lvl_all_wo_0_t lvlwo = { .all = dfi_readl(LVL_ALL_WO_0), };
	lvlwo.b.rdlvl_rdlat_start = 1;
	dfi_writel(lvlwo.all, LVL_ALL_WO_0);
	//Auto clear, no need to write in to clear

	dfi_mc_rdlvl_rd_issue_seq(cs, ddr_type);

	u32 rdata;
	do {
		rdata = dfi_readl(LVL_ALL_RO_0);
	} while (((rdata >> 5) & 1) != 1); //Bit[5] is rdlvl_rdlat_done

	return 0;
}

norm_ps_code int dfi_rdlvl_rdlat_seq(u8 target_cs, u8 rcvlat_offset, u8 debug)
{
	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	u8 ddr_type = mode.b.dram_type;
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

	if (debug >= 1)
		utils_dfi_trace(LOG_ERR, 0xd33f, "DFI read level - DFI RDLAT training start, Target CS[%d]\n", target_cs);
    udelay(1000); // wait for VREF ready...
    #if 0
	//CLear previous training results
	in_ctrl_0_t in0     = { .all = dfi_readl(IN_CTRL_0), };
	in0.b.rcvlat    = 0;
	in0.b.rdlat     = 0;
	in0.b.rd_rstb_force = 1; //DFI reset is active HIGH !!!
	dfi_writel(in0.all,IN_CTRL_0);
	lvl_all_wo_0_t lvlwo = { .all = 0 };
 	lvlwo.b.rdlat_update = 1;
	dfi_writel(lvlwo.all,LVL_ALL_WO_0);
	in0.b.rd_rstb_force = 0;
	in0.b.force_dqs_rcv_en = 1;
	dfi_writel(in0.all,IN_CTRL_0);

	//Swich to single ended DQS for RDLAT training
	strgt_ctrl_0_t strgt = { .all = dfi_readl(STRGT_CTRL_0), };
	u8 strgt_mode = strgt.b.strgt_mode;
	u8 strgt_en = strgt.b.strgt_en;
	strgt.b.strgt_mode = 0x3; //single-ended DQS
	strgt.b.strgt_en = 0; //disable strobe gate
	dfi_writel(strgt.all, STRGT_CTRL_0);

	//Put DRAM in MPR mode for DDR3 and DDR4
	if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4)
		dfi_mc_ddrx_mpr_mode_seq(cs, true);
    #else
    strgt_ctrl_0_t strgt = { .all = dfi_readl(STRGT_CTRL_0), };
    u8 strgt_mode = strgt.b.strgt_mode;
    u8 strgt_en = strgt.b.strgt_en;
    strgt.b.strgt_mode = 0x3;       // single-ended DQS
    strgt.b.strgt_en = 0;           // disable strobe gate
    dfi_writel(strgt.all, STRGT_CTRL_0);

    // clear previous training results
    in_ctrl_0_t in0 = { .all = dfi_readl(IN_CTRL_0), };
    utils_dfi_trace(LOG_ERR, 0x3d48, "Before Re-Train, RDLAT=%d, RCVLAT=%d.\n", in0.b.rdlat, in0.b.rcvlat);
    in0.b.rcvlat = 0;
    in0.b.rdlat = 0;
    in0.b.rd_rstb_force = 1;        // dfi reset is active high
    in0.b.force_dqs_rcv_en = 1;
    dfi_writel(in0.all,IN_CTRL_0);

    lvl_all_wo_0_t lvlwo = { .all = 0 };
    lvlwo.b.rdlat_update = 1;
    dfi_writel(lvlwo.all,LVL_ALL_WO_0);


    //Put DRAM in MPR mode for DDR3 and DDR4
	if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4)
		dfi_mc_ddrx_mpr_mode_seq(cs, true);

    in0.b.rd_rstb_force = 0;
    dfi_writel(in0.all,IN_CTRL_0);
    #endif
	//Issue MPR Read
	dfi_rdlvl_rdlat_start(cs, ddr_type, debug);
	u32 rdata;
	rdata = dfi_readl(LVL_RDLVL_0);
	u8 rdlat = (rdata & 0x3f); //Bit[5:0] are rdlat_actual[5:0]
	in0.b.rdlat = rdlat + RDLAT_DELAY;

	//Calculate rcvlat from rdlat
	u8 rcvlat = 0;
	if (rdlat >= rcvlat_offset) {
		rcvlat = rdlat - rcvlat_offset;
	}
	in0.b.rcvlat = rcvlat;
	in0.b.force_dqs_rcv_en = 0; //Disable force receiver on
	dfi_writel(in0.all, IN_CTRL_0);

	//lvlwo.b.rdlat_update = 1;
	//dfi_writel(lvlwo.all,LVL_ALL_WO_0);

	//Exit DRAM MPR mode,  not necessary when RDLAT training is called within RDLVL training
	if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4)
		dfi_mc_ddrx_mpr_mode_seq(cs, false);


	//Restore STR Gate settings
	strgt.b.strgt_mode = strgt_mode;
	strgt.b.strgt_en = strgt_en;
	dfi_writel(strgt.all, STRGT_CTRL_0);

	ndelay(1000);
	if (debug >= 1)
		utils_dfi_trace(LOG_ERR, 0x08f2, "DFI rdlat training finished, RDLAT=%d, RCVLAT=%d.\n", rdlat, rcvlat);
	// If rdlat results is larger than 20, report error and return
	if (rdlat > 20) {
		utils_dfi_trace(LOG_ERR, 0x0895, "DFI rdlat training ERROR, RDLAT is too large.\n");
		return -1;
	}
	return 0;
}

norm_ps_code int dfi_dll_update(void)
{
	dll_ctrl_0_t dll = { .all = dfi_readl(DLL_CTRL_0), };
	dll.b.dll_update_en = 0;
	dfi_writel(dll.all, DLL_CTRL_0);
	dll.b.dll_update_en = 1;
	dfi_writel(dll.all, DLL_CTRL_0);
	ndelay(10);
	dll.b.dll_update_en = 0;
	dfi_writel(dll.all, DLL_CTRL_0);
	ndelay(10);
	return 0;
}

dfi_code int dfi_rdlvl_seq(u8 target_cs, u8 quick_sweep, u8 eye_size, dll_lvl_res_t * lvl_res, u8 debug)
{
#ifdef DDR_PERF_CNTR_DLL
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

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

	u8 start_threshold = eye_size;
	u8 dly_cnt_inc = 1;

	if (quick_sweep == 1) {
		start_threshold = (start_threshold >> 2);
		dly_cnt_inc = 4;
		if (debug >= 1)
			utils_dfi_trace(LOG_ERR, 0x19b3, "DFI read level - QUICK_SWEEP is enabled\n");
	}

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	//When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
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

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0x9238, "DFI read level - DLL training starts, target CS[%d], Byte width: %d, Byte mask 0x%x\n", target_cs, num_byte, byte_mask);

	//DFI Read Reset
	in_ctrl_0_t in0 = { .all = dfi_readl(IN_CTRL_0), };
	in0.b.rd_rstb_force = 1; //DFI reset is active HIGH !!!
	dfi_writel(in0.all, IN_CTRL_0);
	in0.b.rd_rstb_force = 0;
	dfi_writel(in0.all, IN_CTRL_0);

	//Enable MPR mode for DDR3/4

	if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4)
		dfi_mc_ddrx_mpr_mode_seq(cs, true);

	//Enable shadow-register simultaneous write
	//Write DLL phase delay to all bytes at the same time
	sel_ctrl_1_t sel1 = { .all = dfi_readl(SEL_CTRL_1), };
	sel1.b.simul_dll_phase = 1;
	dfi_writel(sel1.all, SEL_CTRL_1);
	u8 dly_cnt = 0;
	sel_dll_0_t sdll0 = { .all = dfi_readl(SEL_DLL_0), };
	sdll0.b.dll_phase1 = 0;
	sdll0.b.dll_phase0 = 0;
	dfi_writel(sdll0.all, SEL_DLL_0);
	ndelay(100);
	dfi_dll_update();

	//Status flag for each byte
	u32 phase1_started = 0;
	u32 phase0_started = 0;
	u32 phase1_ended = 0;
	u32 phase0_ended = 0;
	u32 rd_rise_status_byte = 0;
	u32 rd_fall_status_byte = 0;

	//Pass counter for each byte
	u8 phase1_start_cnt[num_byte];
	u8 phase0_start_cnt[num_byte];

	//Training results of each phase
	u8 phase1_midpoint = 0;
	u8 phase0_midpoint = 0;
	u8 phase1_range = 0;
	u8 phase0_range = 0;
	u8 phase0_offset = 0;
	u8 phase1_offset = 0;

	//Pass Fail point for each byte
	u8 phase1_start_val[num_byte];
	u8 phase0_start_val[num_byte];
	u8 phase1_end_val[num_byte];
	u8 phase0_end_val[num_byte];

	u32 rdata = 0;
	u8 phase_end_done = 0;
	u8 phase_start_done = 0;

	for (int i = 0; i < num_byte; i++) {
		phase1_start_cnt[i] = 0;
		phase0_start_cnt[i] = 0;
		phase1_start_val[i] = 0;
		phase0_start_val[i] = 0;
		phase1_end_val[i] = 0;
		phase0_end_val[i] = 0;
	}

	//Main Training Loop
	do {
		//Perform MPR write-read-compare
		dfi_rdlvl_rd_comp_seq(cs, ddr_type);
		//Read DQ bytewise read compare status
		rdata = dfi_readl(RD_COMP_STATUS_ALL_3);
		rd_rise_status_byte = (rdata & byte_mask);
		rd_fall_status_byte = ((rdata >> 16) & byte_mask);
		if (debug >= 3)
			utils_dfi_trace(LOG_ERR, 0x245a, "DFI read level - Count: %d, rd_comp status rise: 0x%x, fall: 0x%x (1 indicates that byte passes, expect 0x%x).\n", dly_cnt, rd_rise_status_byte, rd_fall_status_byte, byte_mask);

		//Sweep each byte and check result of each byte
		for (int i = 0; i < num_byte; i++) {
			//find window for Strobe rising edge, Phase0
			if (((phase0_started >> i) & 1) == 0) {    //Try to find start point for byte i
				if (((rd_rise_status_byte >> i) & 1) == 1) {    //Passed Write Read Compare for Byte i
					if (phase0_start_cnt[i] == 0) {
						phase0_start_val[i] = dly_cnt;    //use the first start point as start value
						if (debug >= 3)
							utils_dfi_trace(LOG_ERR, 0x7d61, "DFI read level - Byte %d rising edge, find start point, first pass @ %d.\n", i, dly_cnt);
					}
					phase0_start_cnt[i] += 1;
					if (debug >= 3)
						utils_dfi_trace(LOG_ERR, 0x0700, "DFI read level - Byte %d rising edge, find start point, start count is: %d.\n", i, phase0_start_cnt[i]);

					if (phase0_start_cnt[i] >= start_threshold) {
						phase0_started = (phase0_started | (1 << i));    //Set bit i of phase0_started to 1
						if (debug >= 3)
							utils_dfi_trace(LOG_ERR, 0x7fdd, "DFI read level - Byte %d rising edge, found start point: %d, dly_cnt @ %d.\n", i, phase0_start_val[i], dly_cnt);
					}
				} else {    //Failed Write Read Compare for Byte i
					phase0_start_cnt[i] = 0; //Reset pass counter
					if (debug >= 3)
						utils_dfi_trace(LOG_ERR, 0x9fd2, "DFI read level - Byte %d rising edge, find start point, rd_comp did not pass @ %d.\n", i, dly_cnt);
				}
			} else if (((phase0_ended >> i) & 1) == 0) { //Try to find end point for byte i
				if (((rd_rise_status_byte >> i) & 1) == 0) { //Failed Write Read Compare for Byte i
					phase0_end_val[i] = dly_cnt;
					phase0_ended = (phase0_ended | (1 << i)); //Set bit i of phase0_ended to 1
					if (debug >= 3)
						utils_dfi_trace(LOG_ERR, 0x5469, "DFI read level - Byte %d rising edge, found end point: %d.\n", i, dly_cnt);
				}
			}

			//find window for Strobe Failling edge, Phase1
			if (((phase1_started >> i) & 1) == 0) { //Try to find start point for byte i
				if (((rd_fall_status_byte >> i) & 1) == 1) { //Passed Write Read Compare for Byte i
					if (phase1_start_cnt[i] == 0) {
						phase1_start_val[i] = dly_cnt;
						if (debug >= 3)
							utils_dfi_trace(LOG_ERR, 0x5c82, "DFI read level - Byte %d falling edge, find start point, first pass @ %d.\n", i, dly_cnt);
					}
					phase1_start_cnt[i] += 1;
					if (debug >= 3)
						utils_dfi_trace(LOG_ERR, 0xd044, "DFI read level - Byte %d falling edge, find start point, start count is: %d.\n", i, phase1_start_cnt[i]);
					if (phase1_start_cnt[i] >= start_threshold) {
						phase1_started = (phase1_started | (1 << i)); //Set bit i of phase1_started to 1
						if (debug >= 3)
							utils_dfi_trace(LOG_ERR, 0x91da, "DFI read level - Byte %d falling edge, found start point: %d, dly_cnt @ %d.\n", i, phase1_start_val[i], dly_cnt);
					}
				} else { //Failed Write Read Compare for Byte i
					phase1_start_cnt[i] = 0; //Reset pass counter
					if (debug >= 3)
						utils_dfi_trace(LOG_ERR, 0x5ae9, "DFI read level - Byte %d falling edge, find start point, rd_comp did not pass @ %d.\n", i, dly_cnt);
				}
			} else if (((phase1_ended >> i) & 1) == 0) { //Try to find end point for byte i
				if (((rd_fall_status_byte >> i) & 1) == 0) { //Failed Write Read Compare for Byte i
					phase1_end_val[i] = dly_cnt;
					phase1_ended = (phase1_ended | (1 << i)); //Set bit i of phase1_ended to 1
					if (debug >= 3)
						utils_dfi_trace(LOG_ERR, 0x08b0, "DFI read level - Byte %d falling edge, found end point: %d.\n", i, dly_cnt);
				}
			}
		}

		//First check if dly_cnt is at max or not
		if (dly_cnt + dly_cnt_inc > 63) { //for test
			for (int i = 0; i < num_byte; i++) {
				//Regardless of pass/fail, if "ended" is not set, then set it to the max value for calculations
				//If "started" is not set, then it will be flagged as an error and return 1 in the next part.
				if (((phase1_ended >> i) & 1) == 0) {
					phase1_end_val[i] = dly_cnt;
					phase1_ended = (phase1_ended | (1 << i));                //Set bit i of phase1_ended to 1
				}
				if (((phase0_ended >> i) & 1) == 0) {
					phase0_end_val[i] = dly_cnt;
					phase0_ended = (phase0_ended | (1 << i));                //Set bit i of phase1_ended to 1
				}
			}
		}

		//Inc DLL Phase delay on all bytes
		dly_cnt = dly_cnt + dly_cnt_inc;

		if (dly_cnt <= 63) {
			if (debug >= 3)
				utils_dfi_trace(LOG_ERR, 0x1344, "DFI read level - program DLL phase to: %d.\n", dly_cnt);

			sel_dll_0_t sdll0 = { .all = dfi_readl(SEL_DLL_0), };
			sdll0.b.dll_phase1 = dly_cnt;
			sdll0.b.dll_phase0 = dly_cnt;
			dfi_writel(sdll0.all, SEL_DLL_0);
			ndelay(100);
			dfi_dll_update();
		}

		if (debug >= 3) {
			utils_dfi_trace(LOG_ERR, 0x3aee, "DFI read level - Count: %d, Phase0_started is: 0x%x, Phase1_started is: 0x%x.\n", dly_cnt, phase0_started, phase1_started);
			utils_dfi_trace(LOG_ERR, 0x4cac, "DFI read level - Count: %d, Phase0_ended is: 0x%x, Phase1_ended is: 0x%x.\n", dly_cnt, phase0_ended, phase1_ended);
		}

		if (((phase1_ended & byte_mask) == byte_mask) && ((phase0_ended & byte_mask) == byte_mask)) {
			phase_end_done = 1;
		}

	} while ((phase_end_done != 1) && (dly_cnt <= 63));

	// Disable shadow-register simultaneous write
	sel1.b.simul_dll_phase = 0;
	dfi_writel(sel1.all, SEL_CTRL_1);
	ndelay(1000);

	//Calculate the DLL passing eye
	if (((phase1_started & byte_mask) == byte_mask) && ((phase0_started & byte_mask) == byte_mask)) {
		phase_start_done = 1;
	}

#ifdef DDR_PERF_CNTR_DLL
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0xaf4b, "dfi_rdlvl_seq - Total cycles take: %d.\n", pc_val);
#endif

	if ((dly_cnt > 63) && (phase_start_done == 0)) {
		if (debug >= 2) {
			utils_dfi_trace(LOG_ERR, 0x5250, "DFI read level - Error, DFI read leveling reached max, but no start point is detected for at least one byte.\n");
			utils_dfi_trace(LOG_ERR, 0x6165, "DFI read level - Error, Rising edge start status is: 0x%x, end status is: 0x%x . Falling edge start status is: 0x%x, end status is: 0x%x (1 indicates that byte passes).\n", phase0_started, phase0_ended, phase1_started, phase1_ended);
		}

		//Exit DRAM MPR mode
		if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4)
			dfi_mc_ddrx_mpr_mode_seq(cs, false);

		return -1;
	} else { //Training success
		u8 worst_range = 255;
		u8 worst_range_byte = 255;
		u8 worst_offset = 0;
		u8 worst_midpoint = 255;
		u8 worst_midpoint_byte = 255;
		for (int i = 0; i < num_byte; i++) {
			sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
			ctrl0.b.sel_dbyte = i;
			dfi_writel(ctrl0.all, SEL_CTRL_0);
			//phase1_midpoint = (phase1_end_val[i] >> 1) + (phase1_start_val[i] >> 1);
			//phase0_midpoint = (phase0_end_val[i] >> 1) + (phase0_start_val[i] >> 1);
			phase1_midpoint = phase1_start_val[i] + ((phase1_end_val[i]-phase1_start_val[i])>>1);
			phase0_midpoint = phase0_start_val[i] + ((phase0_end_val[i]-phase0_start_val[i])>>1);
			phase1_range = (phase1_end_val[i] - phase1_start_val[i]);
			phase0_range = (phase0_end_val[i] - phase0_start_val[i]);
			sel_dll_0_t sdll0 = { .all = dfi_readl(SEL_DLL_0), };
			sdll0.b.dll_phase1 = phase1_midpoint;
			sdll0.b.dll_phase0 = phase0_midpoint;
			dfi_writel(sdll0.all, SEL_DLL_0);
			if (debug)
				utils_dfi_trace(LOG_ERR, 0x92f7, "DFI read level - Byte %d PHASE0=0x%x, Range0=0x%x, PHASE1=0x%x, Range1=0x%x.\n", i, phase0_midpoint, phase0_range, phase1_midpoint, phase1_range);
			lvl_res->phase0[i] = phase0_midpoint;
			lvl_res->phase1[i] = phase1_midpoint;

			if (phase0_range < worst_range) {
				worst_range = phase0_range;
				worst_range_byte = i;
			}

			if (phase1_range < worst_range) {
				worst_range = phase1_range;
				worst_range_byte = i;
			}

			if (phase0_midpoint > 0x1f)
				phase0_offset = phase0_midpoint - 0x1f;
			else
				phase0_offset = 0x1f - phase0_midpoint;

			if (phase1_midpoint > 0x1f)
				phase1_offset = phase1_midpoint - 0x1f;
			else
				phase1_offset = 0x1f - phase1_midpoint;

			if (phase0_offset > worst_offset) {
				worst_offset = phase0_offset;
				worst_midpoint = phase0_midpoint;
				worst_midpoint_byte = i;
			}

			if (phase1_offset > worst_offset) {
				worst_offset = phase1_offset;
				worst_midpoint = phase1_midpoint;
				worst_midpoint_byte = i;
			}
		}

		lvl_res->worst_range = worst_range;

		if (debug >= 2) {
			utils_dfi_trace(LOG_ERR, 0xd3f8, "DFI read level - Worst midpoint=0x%x at byte=%d. Offset=0x%x.\n", worst_midpoint, worst_midpoint_byte, worst_offset);
			utils_dfi_trace(LOG_ERR, 0xc0dc, "DFI read level - Worst range   =0x%x at byte=%d.\n", worst_range, worst_range_byte);
		}
		//Exit DRAM MPR mode
		if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4)
			dfi_mc_ddrx_mpr_mode_seq(cs, false);

		//Update final DLL settings
		dfi_dll_update();

	}

	return 0;
}

dfi_code int dfi_rdlvl_2d_seq(u8 target_cs, u8 quick_sweep, u8 eye_size, u8 debug)
{
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	// When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	u8 data_width = mode.b.data_width;
	u8 ddr_type = mode.b.dram_type;

	u8 num_byte = 1;

	switch (data_width) {
	case 4:
		num_byte = 8;
		break;

	case 3:
		num_byte = 4;
		break;

	case 2:
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
		num_byte = num_byte + 1;
	}

	//Reset DLL
	dll_ctrl_0_t dll_ctrl_0 = { .all = dfi_readl(DLL_CTRL_0), };
	dll_ctrl_0.b.dll_rstb = 0; //reset DLL
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);
	ndelay(10);
	dll_ctrl_0.b.dll_rstb = 1; //release reset
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);
	//mdelay(1);
	ndelay(5000); //ndelay for DLL lock

	dll_lvl_res_t lvl_res;
	dll_lvl_res_t *lvl_res_cur = &lvl_res;

	u8 error = 0;
	u8 vgen_range = 2;
	u8 vgen_vsel = 0;
	u8 phase0_final[num_byte];
	u8 phase1_final[num_byte];
	u8 worst_range_all = 0;
	u8 vgen_range_final = 0;
	u8 vgen_vsel_final = 0;
	vgen_ctrl_0_t vgen = { .all = 0 };

	if (ddr_type == MC_DDR3 || ddr_type == MC_DDR4) { //DDR3 or DDR4 custom training pattern
		lvl_rdlvl_0_t rdlvl_0 = { .all = dfi_readl(LVL_RDLVL_0), };
		lvl_rdlvl_1_t rdlvl_1 = { .all = dfi_readl(LVL_RDLVL_1), };
		rdlvl_0.b.rd_dq_cal_custom_en = 0;
		rdlvl_1.b.rd_dq_cal_invert = 0;
		rdlvl_1.b.rd_dq_cal_pattern = 0x00cc;
		dfi_writel(rdlvl_0.all, LVL_RDLVL_0);
		dfi_writel(rdlvl_1.all, LVL_RDLVL_1);
	}

	if (ddr_type == MC_LPDDR4)
		vgen_range = 0;

	//Sweep VGEN range 2 for DDR4, 44.3% ~ 83.1% of 1.2V
	for (u8 i = 0; i < 64; i++) {
		vgen.b.vgen_vsel = vgen_vsel;
		vgen.b.vgen_range = vgen_range;
		vgen.b.vgen_pu = 0; //power up
		dfi_writel(vgen.all, VGEN_CTRL_0);
		ndelay(1000);
		vgen.b.vgen_pu = 1; //power up
		dfi_writel(vgen.all, VGEN_CTRL_0);
		ndelay(500000); //Must waif for VREF Voltage stable

		for (int i = 0; i < 9; i++) {
			lvl_res_cur->phase0[i] = 0;
			lvl_res_cur->phase1[i] = 0;
		}
		lvl_res_cur->worst_range = 0;
		error = dfi_rdlvl_seq(target_cs, quick_sweep, eye_size, lvl_res_cur, debug);
		if (debug)
			utils_dfi_trace(LOG_ERR, 0x80df, "DFI read level 2D - VREF Range: %d, VREF Value: 0x%x, Worst DLL eye: %d\n", vgen_range, vgen_vsel, lvl_res_cur->worst_range);
		if (error == 0) {
			if (lvl_res_cur->worst_range > worst_range_all) {
				worst_range_all = lvl_res_cur->worst_range;
				for (int i = 0; i < num_byte; i++) {
					phase0_final[i] = lvl_res_cur->phase0[i];
					phase1_final[i] = lvl_res_cur->phase1[i];
				}
				vgen_range_final = vgen_range;
				vgen_vsel_final = vgen_vsel;
			}
		}
		vgen_vsel += 1;
	}
	if (worst_range_all < 5) {
		utils_dfi_trace(LOG_ERR, 0x6979, "DFI read level 2D - Error! DLL eye is too small, eye size=%d\n", worst_range_all);
        switch (ddr_type) {
            case 0://DDR4
                vgen_vsel_final   = 0x22; //65.38%
                vgen_range_final  = 0x2; //DDR4, choose Range 2 (44.3% to 83.1% of 1.2v).
                break;
            case 1://DDR3
                vgen.b.vgen_bypass = 1; //use internal half VDDQ
                vgen.b.vgen_pu     = 0; //power up
                break;
            case 10://LPDDR4
                vgen_vsel_final   = 0x20; //15.92% VREF, changed to 300mW
                vgen_range_final  = 0; //0:10%~33.4% of 1.1v, 1:22.3%~46.5% of 1.1v
                break;
            default://default to DDR4
                vgen_vsel_final   = 0x22; //65.38%
                vgen_range_final  = 0x2; //DDR4, choose Range 2 (44.3% to 83.1% of 1.2v).
        }
        for (int i=0; i<num_byte; i++) {
            phase0_final[i] = 0x1F;
            phase1_final[i] = 0x1F;
        }
	}
	else if (debug >= 1) {
		utils_dfi_trace(LOG_ERR, 0xacc8, "DFI read level 2D - Best VREF setting: Range=%d, Value=0x%x.\n", vgen_range_final, vgen_vsel_final);
		utils_dfi_trace(LOG_ERR, 0xb564, "DFI read level 2D - DLL eye size=%d\n", worst_range_all);
	}

	vgen.b.vgen_vsel = vgen_vsel_final;
	vgen.b.vgen_range = vgen_range_final;
	vgen.b.vgen_pu = 0; //power up
	dfi_writel(vgen.all, VGEN_CTRL_0);
	ndelay(1000000);
	vgen.b.vgen_pu = 1; //power up
	dfi_writel(vgen.all, VGEN_CTRL_0);
	ndelay(1000000);
	sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
	sel_dll_0_t sdll0 = { .all = dfi_readl(SEL_DLL_0), };
	for (int i = 0; i < num_byte; i++) {
		ctrl0.b.sel_dbyte = i;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		sdll0.b.dll_phase0 = phase0_final[i];
		sdll0.b.dll_phase1 = phase1_final[i];
		dfi_writel(sdll0.all, SEL_DLL_0);
		if (debug >= 1)
			utils_dfi_trace(LOG_ERR, 0x8949, "DFI read level 2D - Byte %d PHASE0=0x%x, PHASE1=0x%x.\n", i, phase0_final[i], phase1_final[i]);
	}
	ndelay(100);
	dfi_dll_update();
	ndelay(1000);

#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	if (debug)
		utils_dfi_trace(LOG_ERR, 0xf71e, "dfi_rdlvl_seq_2d - Total cycles take: %d.\n", pc_val);
#endif

	return 0;
}

// Stress read and update DLL for all bytes as a whole
dfi_code int dfi_rdlvl_dpe_seq(u8 target_cs, u8 train_type, u8 *win_size, int *mod_delay, u8 debug)
{
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	u8 data_width = mode.b.data_width;
	u8 num_byte = 1;
	switch (data_width) {
	case 4:
		num_byte = 8;
		break;
	case 3:
		num_byte = 4;
		break;
	case 2:
		num_byte = 2;
		break;
	case 1: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
		break;
	default: //Default DFI DFI_WIDTH is 8
		num_byte = 1;
	}
	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	num_byte = (ras.b.ecc_enb) ? num_byte + 1 : num_byte;

	u16 byte_mask = 1;

	if (debug >= 1) {
		if (train_type == TRAIN_FULL)
			utils_dfi_trace(LOG_ERR, 0x29eb, "DFI read level DPE Full");
		else if (train_type == TRAIN_FAST)
			utils_dfi_trace(LOG_ERR, 0x761a, "DFI read level DPE Fast");
		else
			utils_dfi_trace(LOG_ERR, 0x3c8c, "DFI read level DPE Window (use FAST)");

		utils_dfi_trace(LOG_ERR, 0x0db9, " - DLL training starts, target CS[%d], Byte width: %d, Byte mask 0x%x\n", target_cs, num_byte, byte_mask);
	}

	u8 phase0_final[num_byte];
	u8 phase1_final[num_byte];

	u8 phase_min = 100;
	u8 phase_max = 0;

	// Find min and max phase delays, backup current trained values
	sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
	sel_dll_0_t sdll0 = { .all = dfi_readl(SEL_DLL_0), };
	for (int i = 0; i < num_byte; i++) {
		ctrl0.b.sel_dbyte = i;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		sdll0.all = dfi_readl(SEL_DLL_0);
		phase0_final[i] = sdll0.b.dll_phase0;
		phase1_final[i] = sdll0.b.dll_phase1;

		if (sdll0.b.dll_phase0 < phase_min)
			phase_min = sdll0.b.dll_phase0;
		if (sdll0.b.dll_phase0 > phase_max)
			phase_max = sdll0.b.dll_phase0;
		if (sdll0.b.dll_phase1 < phase_min)
			phase_min = sdll0.b.dll_phase1;
		if (sdll0.b.dll_phase1 > phase_max)
			phase_max = sdll0.b.dll_phase1;
	}
	if (debug >= 1) {
		utils_dfi_trace(LOG_ERR, 0xdf02, "DFI read level DPE - Min Phase=0x%x, Max Phase=0x%x.\n", phase_min, phase_max);
		utils_dfi_trace(LOG_ERR, 0x1fb5, "DFI read level DPE - Increment DLL phase.\n");
	}

	u8 failed_push;
	u8 failed_pull;
	u8 push_delay = 0;
	u8 pull_delay = 0;
	// Push DLL Phase delay
	for (int i = 1; i < (64 - phase_max); i++) {

		for (int by = 0; by < num_byte; by++) {
			ctrl0.b.sel_dbyte = by;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			sdll0.b.dll_phase0 = phase0_final[by] + i;
			sdll0.b.dll_phase1 = phase1_final[by] + i;
			dfi_writel(sdll0.all, SEL_DLL_0);
			dfi_dll_update();
		}

		if (train_type == TRAIN_FULL)
			failed_push = dfi_dpe_verify_read(0, TSIZE_FULL, false);
		else if (train_type == TRAIN_FAST)
			failed_push = dfi_dpe_verify_read(0, TSIZE_FAST, false);
		else //(train_type == TRAIN_WIND)
			failed_push = dfi_dpe_verify_read(0, TSIZE_WIND, false);

		if (failed_push | (i==63)) {
			push_delay = i;
			if (debug > 0)
				utils_dfi_trace(LOG_ERR, 0xff8c, "DFI read level DPE - Push DLL phase by 0x%x. Failed=%d.\n", push_delay, failed_push);
			break;
		}
	}

	// Pull DLL Phase delay
	for (u8 i = 1; i <= phase_min; i++) {

		for (int by = 0; by < num_byte; by++) {
			ctrl0.b.sel_dbyte = by;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			sdll0.b.dll_phase0 = phase0_final[by] - i;
			sdll0.b.dll_phase1 = phase1_final[by] - i;
			dfi_writel(sdll0.all, SEL_DLL_0);
			dfi_dll_update();
		}

		if (train_type == TRAIN_FULL)
			failed_pull = dfi_dpe_verify_read(0, TSIZE_FULL, false);
		else if (train_type == TRAIN_FAST)
			failed_push = dfi_dpe_verify_read(0, TSIZE_FAST, false);
		else //(train_type == TRAIN_WIND)
			failed_push = dfi_dpe_verify_read(0, TSIZE_WIND, false);

		if (failed_pull | (i == phase_min)) {
			pull_delay = i;
			if (debug > 0)
				utils_dfi_trace(LOG_ERR, 0x8b1a, "DFI read level DPE - Pull DLL phase by 0x%x, Failed=%d.\n", pull_delay, failed_pull);
			break;
		}
	}

	// Calculate window size
	*win_size = 1 + push_delay + pull_delay - failed_push - failed_pull ;
	u8 window_size = *win_size;
	// Calculate new Phase delays
	u8 is_push;
	if (push_delay > pull_delay) {
		is_push = 1;
		push_delay = ((push_delay - pull_delay) >> 1);
		if (debug > 0)
			utils_dfi_trace(LOG_ERR, 0x217b, "DFI read level DPE - Update new DLL phase delays. Push DLL delays by 0x%x steps.\n", push_delay);
	} else {
		is_push = 0;
		pull_delay = ((pull_delay - push_delay) >> 1);
		if (debug > 0)
			utils_dfi_trace(LOG_ERR, 0xb39b, "DFI read level DPE - Update new DLL phase delays. Pull DLL delays by 0x%x steps.\n", pull_delay);
	}

	// Write new DLL phase delay values back to registers
	if (is_push) {
		*mod_delay = push_delay;
	} else {
		*mod_delay = 0 - pull_delay;
	}

	if (train_type != TRAIN_WIND) {
		for (int by = 0; by < num_byte; by++) {
			ctrl0.b.sel_dbyte = by;
			dfi_writel(ctrl0.all, SEL_CTRL_0);

			if (is_push) {
				sdll0.b.dll_phase0 = phase0_final[by] + push_delay;
				sdll0.b.dll_phase1 = phase1_final[by] + push_delay;
				*mod_delay = push_delay;
			} else {
				sdll0.b.dll_phase0 = phase0_final[by] - pull_delay;
				sdll0.b.dll_phase1 = phase1_final[by] - pull_delay;
				*mod_delay = 0 - pull_delay;
			}
			dfi_writel(sdll0.all, SEL_DLL_0);
		}
		dfi_dll_update();
		ndelay(1000);
	}

#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0xad0c, "dfi_rdlvl_dpe_seq - Total cycles take: %d.\n", pc_val);
#endif

	if (window_size < 8) {
		utils_dfi_trace(LOG_ERR, 0x9b33, "DFI read level DPE - Error. Total DLL phase window=%x is too small.\n", window_size);
		return -1;
	}
	return 0;
}

#if (ENABLE_DFI_VALIDATION_DEBUG == 1)
// Validation-use code
// Loop dfi_rdlvl_dpe_seq multiple times
// Use TRAIN_WIND so that the delay values do not change, and only the worst window is returned
// Can run this test for a large numer of loops while applying external changes such as temperature
#define ASIZE 10
dfi_code void dfi_rdlvl_dpe_loop(u8 target_cs, u32 loop)
{
	u8 win_arr[ASIZE] ;
	int delay_arr[ASIZE] ;
	u32 wcount_arr[ASIZE] ;
	u32 dcount_arr[ASIZE] ;
	u8 wvalid_arr[ASIZE] ;
	u8 dvalid_arr[ASIZE] ;
	u8 wmatch;
	u8 dmatch;
	u8 win_size;
	int mod_delay;

	utils_dfi_trace(LOG_ERR, 0xdbe2, "dfi_rdlvl_dpe_loop start (prints a status message every 10000 loops).\n");

	for (u8 j = 0 ; j < ASIZE ; j++) {
		win_arr[j] = 0;
		delay_arr[j] = 0;
		wvalid_arr[j] = 0;
		dvalid_arr[j] = 0;
		wcount_arr[j] = 0;
		dcount_arr[j] = 0;
	}

	for (u32 l = 0; l < loop; l++) {
		if (l%10000 == 0)
			utils_dfi_trace(LOG_ERR, 0x3416, "On loop... win_size=%d, mod_delay=%d.\n", win_size, mod_delay);

		dfi_rdlvl_dpe_seq(target_cs, TRAIN_FAST, &win_size, &mod_delay, 0); // disable debug

		wmatch = 0;
		dmatch = 0;
		for (u8 j = 0 ; j < ASIZE ; j++) {
			// Keep track of window size
			if (wvalid_arr[j] == 1) {
				if (win_arr[j] == win_size) {
					wcount_arr[j]++;
					wmatch = 1;
				}
			}
			else if (wmatch == 0) {
				win_arr[j] = win_size;
				wvalid_arr[j] = 1;
				wcount_arr[j] = 1;
				wmatch = 1;
			}

			// Keep track of delay offset
			if (dvalid_arr[j] == 1) {
				if (delay_arr[j] == mod_delay) {
					dcount_arr[j]++;
					dmatch = 1;
				}
			}
			else if (dmatch == 0) {
				delay_arr[j] = mod_delay;
				dvalid_arr[j] = 1;
				dcount_arr[j] = 1;
				dmatch = 1;
			}
		}
		if ((wmatch == 0) || (dmatch == 0))
			utils_dfi_trace(LOG_ERR, 0x7883, "Error, array size %d is not enough to capture all results.\n", ASIZE);
	}

	utils_dfi_trace(LOG_ERR, 0xfaf0, "******\n");
	utils_dfi_trace(LOG_ERR, 0x6a37, "Read DLL worst window size (up to %d entries):\n", ASIZE);
	for (u8 j = 0 ; j < ASIZE ; j++)
		utils_dfi_trace(LOG_ERR, 0x2a8e, "Entry #%d valid = %d. Value = %d. Count = %d.\n", j, wvalid_arr[j], win_arr[j], wcount_arr[j]);
	utils_dfi_trace(LOG_ERR, 0x9311, "******\n");
	utils_dfi_trace(LOG_ERR, 0xee45, "Read DLL change offset (up to %d entries):\n", ASIZE);
	for (u8 j = 0 ; j < ASIZE ; j++)
		utils_dfi_trace(LOG_ERR, 0x59f6, "Entry #%d valid = %d. Value = %d. Count = %d.\n", j, dvalid_arr[j], delay_arr[j], dcount_arr[j]);
	utils_dfi_trace(LOG_ERR, 0xf79e, "******\n");

	utils_dfi_trace(LOG_ERR, 0x7420, "dfi_rdlvl_dpe_loop end.\n");
}

// This function manually sweeps RDLAT to check for read FIFO margins (and to prove that HW rdlat sequence is good)
#define RDLAT_SIZE 16
#define RCVLAT_OFFSET 10
dfi_code void dfi_rd_lat_man_loop(u8 target_cs, u32 loop)
{
	u8 rcvlat_orig;
	u8 rdlat_orig;
	u8 rdsts;
	u32 rdsts_arr[RDLAT_SIZE+1];

	for (u8 r = 0; r <= RDLAT_SIZE ; r++) {
		rdsts_arr[r] = 0;
	}

	utils_dfi_trace(LOG_ERR, 0x82a6, "RDLAT manual sweep loop start (prints a status message every 10000 loops).\n");

	//CLear previous training results
	in_ctrl_0_t in0     = { .all = dfi_readl(IN_CTRL_0), };
	rcvlat_orig = in0.b.rcvlat;
	rdlat_orig  = in0.b.rdlat;
	in0.b.rcvlat    = 0;
	in0.b.rdlat     = 0;
	dfi_writel(in0.all,IN_CTRL_0);
	utils_dfi_trace(LOG_ERR, 0xe0d9, "Original rdlat = %d, rcvlat = %d. \n", rdlat_orig, rcvlat_orig);

	lvl_all_wo_0_t lvlwo = { .all = dfi_readl(LVL_ALL_WO_0), };
 	lvlwo.b.rdlat_update = 1;

	u8 rcvlat;
	for (u32 l = 0; l < loop; l++) {
		if (l%10000 == 0)
			utils_dfi_trace(LOG_ERR, 0xcdd4, "On loop %d...\n", l);

		dfi_sync(); // syncing increases the chance that boundary settings could fail

		for (u8 r = 1; r <= RDLAT_SIZE ; r++) {
			if (r < RCVLAT_OFFSET)
				rcvlat = 0;
			else
				rcvlat = r - RCVLAT_OFFSET;
			in0.b.rcvlat = rcvlat;
			in0.b.rdlat  = r;
			dfi_writel(in0.all,IN_CTRL_0);

			dfi_writel(lvlwo.all,LVL_ALL_WO_0); // issue rdlat_update

			rdsts = dfi_dpe_verify_read(TRAIN_FULL, TSIZE_FULL, false);
			//utils_dfi_trace(LOG_ERR, 0, "rdlat = %d, rcvlat = %d, fail = %d.\n", r, rcvlat, rdsts);
			if (rdsts == 1)
				rdsts_arr[r]++;
		}
	}


	for (u8 r = 1; r <= RDLAT_SIZE ; r++) {
		utils_dfi_trace(LOG_ERR, 0xaec5, "rdlat = %d, fail count = %d.", r, rdsts_arr[r]);
		if (r == (rdlat_orig - RDLAT_DELAY))
			utils_dfi_trace(LOG_ERR, 0x6481, " <-- HW-trained setting ");
		if (r == (rdlat_orig))
			utils_dfi_trace(LOG_ERR, 0x8b22, " <-- Current setting (+%d offset) ", RDLAT_DELAY);
		utils_dfi_trace(LOG_ERR, 0x154c, "\n");
	}

	in0.b.rcvlat = rcvlat_orig;
	in0.b.rdlat  = rdlat_orig;
	dfi_writel(in0.all,IN_CTRL_0);
	dfi_writel(lvlwo.all,LVL_ALL_WO_0); // issue rdlat_update
	utils_dfi_trace(LOG_ERR, 0x811f, "RDLAT manual sweep loop end.\n");
	return;
}
#endif

/*! @} */
