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
/*! \file dfi_wr_train_seq.c
 * @brief dfi write training sequence APIs
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
#include "bf_mgr.h"
#include "btn_cmd_data_reg.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "stdio.h"
#define __FILEID__ dfiwrtrain
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
dfi_code void dfi_mc_sr_seq(bool en)
{
	if (en) { //Enter self-refresh
		dram_pwr_cmd_t r_drampwrcmd = { .all = mc0_readl(DRAM_PWR_CMD), };
		r_drampwrcmd.b.sr_req = 1; //sr_req is Auto_CLR
		mc0_writel(r_drampwrcmd.all, DRAM_PWR_CMD);
	} else { //out of self-refresh
		dram_pwr_cmd_t r_drampwrcmd = { .all = mc0_readl(DRAM_PWR_CMD), };
		r_drampwrcmd.b.pwdn_exit_req = 1;
		mc0_writel(r_drampwrcmd.all, DRAM_PWR_CMD);
	}
	dfi_mc_smtq_cmd_done_poll_seq(100);
}

dfi_code void dfi_mc_vref_set(u8 ca_dq, u8 vref_range, u8 vref_value, u8 cs, u8 debug)
{
	device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
	if (ca_dq == VREF_SET_DQ) {
		config.b.dq_vref_training_range = vref_range;
		config.b.dq_vref_training_value = vref_value;
	} else {
		config.b.ca_vref_training_range = vref_range;
		config.b.ca_vref_training_value = vref_value;
	}
	mc0_writel(config.all,DEVICE_CONFIG_TRAINING);
	ndelay(100);

	if (ca_dq == VREF_SET_DQ)
		dfi_mc_mr_rw_req_seq(MR_WRITE, MR14, cs);
	else
		dfi_mc_mr_rw_req_seq(MR_WRITE, MR12, cs);
	ndelay(100);

	if (debug)  {
		if (ca_dq == VREF_SET_DQ)
			dfi_mc_mr_rw_req_seq(MR_READ, MR14, cs);
		else
			dfi_mc_mr_rw_req_seq(MR_READ, MR12, cs);

		void *dst  = (void*) 0xc006010c;
		u32 rdata =  readl(dst);
		u8 vref_value_rb = (rdata & 0x3f);
		u8 vref_range_rb = ((rdata>>6)&1);
		utils_dfi_trace(LOG_ERR, 0xc06c, "Mode register Read back=0x%x, vref_range=%d, vref_value=%d.\n", rdata, vref_range_rb, vref_value_rb);
	}
}

dfi_code int dfi_wrtrain_sweep(u16 * dq_wr_dly, pass_window_t * window, u8 num_bit, u8 debug)
{
	u8 pass = 0;
	u8 fail = 0;
	u8 erros = 0;
	u16 dq_dly_inc = 0;
	u16 dq_dly_tmp = 0;
	u8 max_out = 0;
	u8 byte_idx;

	sel_ctrl_0_t  ctrl0  = { .all = dfi_readl(SEL_CTRL_0), };
	sel_oddly_1_t oddly1 = { .all = dfi_readl(SEL_ODDLY_1), };
	sel_oddly_2_t oddly2 = { .all = dfi_readl(SEL_ODDLY_2), };

	void *src;
	void *dst = (void*) DDR_START;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, &src);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
	src = (void*)((u32)src & 0xFFFFFFE0);
	dst = (void*)((u32)dst & 0xFFFFFFE0);

	u32 bytes = 128; //Max 1K 32-bit words
	u8 consecutive = 0;
	do {
		//Perform Write Read compare test
		erros = dfi_dpe_verify_training(0, src, dst, bytes, false);

		if (pass==0) {
			if (erros>0) {
				consecutive = 0;
				dq_dly_inc += 1;
				dfi_ck_gate_data(CK_GATE_CS_ALL);
				for (int i=0; i<num_bit; i++) {
					byte_idx = (i-i%8)/8;
					if (i%8==0) {
						dq_dly_tmp = dq_wr_dly[byte_idx] + dq_dly_inc;
						if (dq_dly_tmp>255) {
							dq_dly_tmp = 255;
							max_out = 1;
						}
						ctrl0.b.sel_dbyte  = byte_idx;
						dfi_writel(ctrl0.all,SEL_CTRL_0);
						oddly1.b.dm_wr_dly = dq_dly_tmp;
						dfi_writel(oddly1.all,  SEL_ODDLY_1);
						if (debug)
							utils_dfi_trace(LOG_ERR, 0xd0cd, "DFI Write Training - Set dq_wr_dly and dm_wr_dly to %d for byte %d\n", dq_dly_tmp, byte_idx);
					}
					ctrl0.b.sel_dbit   = i;
					dfi_writel(ctrl0.all,  SEL_CTRL_0);
					oddly2.b.dq_wr_dly = dq_dly_tmp;
					dfi_writel(oddly2.all, SEL_ODDLY_2);
				}

				dfi_ck_gate_data(CK_GATE_NORMAL);

				if (max_out == 1) {
					if (debug)
						utils_dfi_trace(LOG_ERR, 0x7c3d, "DFI Write Training - Error, dq_wr_dly max out before passing window starts\n");
					dtag_put(DTAG_T_SRAM, dtag);
					window->start = 0;
					window->end = 0;
					return -1;
				}
			} else {
				consecutive += 1;
				if (consecutive==5) {
					pass = 1;
					window->start = dq_dly_inc - 4;
				} else {
					dq_dly_inc += 1;
					dfi_ck_gate_data(CK_GATE_CS_ALL);
					for ( int i=0; i<num_bit; i++) {
						byte_idx = (i-i%8)/8;
						if (i%8==0) {
							dq_dly_tmp = dq_wr_dly[byte_idx] + dq_dly_inc;
							if (dq_dly_tmp>255) {
								dq_dly_tmp = 255;
								max_out = 1;
							}
							ctrl0.b.sel_dbyte  = byte_idx;
							dfi_writel(ctrl0.all,SEL_CTRL_0);
							oddly1.b.dm_wr_dly = dq_dly_tmp;
							dfi_writel(oddly1.all,  SEL_ODDLY_1);
							if (debug)
								utils_dfi_trace(LOG_ERR, 0xbf34, "DFI Write Training - Sweep Pass, Set dq_wr_dly and dm_wr_dly to %d for byte %d\n", dq_dly_tmp, byte_idx);
						}
						ctrl0.b.sel_dbit   = i;
						dfi_writel(ctrl0.all,  SEL_CTRL_0);
						oddly2.b.dq_wr_dly = dq_dly_tmp;
						dfi_writel(oddly2.all, SEL_ODDLY_2);
					}
					dfi_ck_gate_data(CK_GATE_NORMAL);
					if (max_out == 1) {
						if (debug)
							utils_dfi_trace(LOG_ERR, 0x3f3b, "DFI Write Training - Error, dq_wr_dly max out before passing window starts\n");
						dtag_put(DTAG_T_SRAM, dtag);
						return -1;
					}
				}
			}
		}else if (fail==0) {
			if (erros == 0) {
				dq_dly_inc += 1;
				dfi_ck_gate_data(CK_GATE_CS_ALL);
				for (int i=0; i<num_bit; i++) {
					byte_idx = (i-i%8)/8;
					if (i%8==0) {
						dq_dly_tmp = dq_wr_dly[byte_idx] + dq_dly_inc;
						if (dq_dly_tmp>255) {
							dq_dly_tmp = 255;
							max_out = 1;
						}
						ctrl0.b.sel_dbyte  = byte_idx;
						dfi_writel(ctrl0.all,SEL_CTRL_0);
						oddly1.b.dm_wr_dly = dq_dly_tmp;
						dfi_writel(oddly1.all,  SEL_ODDLY_1);
						if (debug)
							utils_dfi_trace(LOG_ERR, 0x8228, "DFI Write Training - Sweep Fail, Set dq_wr_dly and dm_wr_dly to %d for byte %d\n", dq_dly_tmp, byte_idx);
					}
					ctrl0.b.sel_dbit   = i;
					dfi_writel(ctrl0.all,  SEL_CTRL_0);
					oddly2.b.dq_wr_dly = dq_dly_tmp;
					dfi_writel(oddly2.all, SEL_ODDLY_2);
				}
				dfi_ck_gate_data(CK_GATE_NORMAL);

				if (max_out == 1) {
					if (debug)
					utils_dfi_trace(LOG_ERR, 0x9b12, "DFI Write Training - dq_wr_dly max out before passing window ends\n");
					fail = 1;
					window->end = dq_dly_inc-1;
				}
			}else{
				if (debug)
					utils_dfi_trace(LOG_ERR, 0xa185, "DFI Write Training - Finding passing window end point, Write Read compare failed\n");
				fail = 1;
				window->end = dq_dly_inc;
			}
		}
	} while (pass == 0 || fail == 0);

	dtag_put(DTAG_T_SRAM, dtag);

	return 0;
}

dfi_code int dfi_wr_train_seq(u8 target_cs, int vref_range_max, int vref_value_max, u8 debug)
{
	//vref_range_max must be 0 or 1
	//vref_value_max must be 0 between 50, inclusive
	u8 num_byte = 1;
	u8 num_bit = 8;
	u16 byte_mask = 1;
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
	/*
	DRAM Bus Width
	When using x32, disable bytes 4, 5, 6, and 7 in <dfi_data_byte_disable> and set DFI register <ecc_byte_remap> to 1.
	0x1: x8 (reserved)
	0x2: x16 (reserved)
	0x3: x32
	0x4: x64
	Others: Reserved
	*/
	u8 data_width =  mode.b.data_width;
	/*
	DRAM Memory Type
	0x0: DDR4
	0x1: DDR3
	0xA: LPDDR4
	others: Reserved
	*/
	u8 ddr_type = mode.b.dram_type;

	switch(data_width) {
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

	if (ddr_type != MC_LPDDR4) {
		utils_dfi_trace(LOG_ERR, 0xa79f, "DFI Write Training - Error! Write Training is for LPDDR4 only, current DDR type is 0x%x.\n", ddr_type);
		return -1;
	}

	if (debug)
		utils_dfi_trace(LOG_ERR, 0xb8f1, "DFI Write Training - Start sequence, target CS[%d], byte width: %d, byte mask: 0x%x\n", target_cs, num_byte, byte_mask);

	// For LPDDR4, set DQ to be half-cycle earlier (edge-aligend) to sweep passing window size
	io_data_1_t data1 = { .all = dfi_readl(IO_DATA_1), };
	data1.b.dq_edge_sel = 0;
	dfi_writel(data1.all, IO_DATA_1);

	// Read out the byte wise WR delay setting after write leveling annd before sweeping
	u16 dq_wr_dly[num_byte];
	sel_ctrl_0_t  ctrl0  = { .all = dfi_readl(SEL_CTRL_0), };
	sel_oddly_2_t oddly2;
	for (int i=0; i<num_byte; i++) {
		ctrl0.b.sel_dbit = i*8;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		oddly2.all = dfi_readl(SEL_ODDLY_2);
		dq_wr_dly[i] = oddly2.b.dq_wr_dly;
		if (debug)
			utils_dfi_trace(LOG_ERR, 0x4828, "dq_wr_dly is %d for byte %d\n", dq_wr_dly[i], i);
	}

	// Sweep VREF settings
	u8 vref_range=0;
	u8 vref_value=0;
	u8 dq_dly_window=0;
	u8 dq_dly_window_final=0;
	u8 vref_range_final=0;
	u8 vref_value_final=0;
	u8 byte_idx;

	pass_window_t pass_window_t_vref;
	pass_window_t * pass_window_t_vref_sweep = &pass_window_t_vref;

	for (int vr=0; vr<=vref_range_max; vr++) {
		if (vr==0) {
			vref_range = 0;
		}else{
			vref_range = 1;
		}
		for (vref_value=0; vref_value<=vref_value_max; vref_value++) {
			dfi_mc_vref_set(VREF_SET_DQ, vref_range, vref_value, cs, debug);
			//Sweep the passing window of current VREF setting
			dfi_wrtrain_sweep(dq_wr_dly, pass_window_t_vref_sweep, num_bit, debug);
			dq_dly_window = pass_window_t_vref_sweep->end - pass_window_t_vref_sweep->start;
			if (debug)
			utils_dfi_trace(LOG_ERR, 0x52fb, "DFI Write Training - VREF Range = %d, VREF Value = %d, Passing Window = %d\n", vref_range, vref_value, dq_dly_window);
			if (dq_dly_window > dq_dly_window_final) {
				dq_dly_window_final = dq_dly_window;
				vref_range_final = vref_range;
				vref_value_final = vref_value;
			}
		}
	}

	if (debug)
		utils_dfi_trace(LOG_ERR, 0x28f8, "DFI Write Training - Max passing window = %d @ VREF Range = %d, VREF Value = %d\n", dq_dly_window_final, vref_range_final, vref_value_final);


	//Set the final VREF setting that maximize passing window, VREF setting apply to all ranks
	dfi_mc_vref_set(VREF_SET_DQ, vref_range_final, vref_value_final, CS_ALL_BITS, debug);

	//Center align DQ with DQS to sweep tDQS2DQ plus tDSS
	data1.b.dq_edge_sel = 1;
	dfi_writel(data1.all, IO_DATA_1);

	//Restore the original dq_wr_dly
	sel_oddly_1_t oddly1;

	dfi_ck_gate_data(CK_GATE_CS_ALL);
	for (int i=0; i<num_bit; i++) {
		byte_idx = (i-i%8)/8;
		if (i%8==0) {
			ctrl0.b.sel_dbyte  = byte_idx;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			oddly1.all = dfi_readl(SEL_ODDLY_1);
			oddly1.b.dm_wr_dly = dq_wr_dly[byte_idx];
			dfi_writel(oddly1.all,  SEL_ODDLY_1);
		}
		ctrl0.b.sel_dbit   = i;
		dfi_writel(ctrl0.all,  SEL_CTRL_0);
		oddly2.all = dfi_readl(SEL_ODDLY_2);
		oddly2.b.dq_wr_dly = dq_wr_dly[byte_idx];
		dfi_writel(oddly2.all, SEL_ODDLY_2);
	}
	dfi_ck_gate_data(CK_GATE_NORMAL);

	//Sweep to find tDQS2DQ plus tDSS
	pass_window_t pass_window_t_dq_dly;
	pass_window_t * pass_window_dq_dly_sweep = &pass_window_t_dq_dly;
	dfi_wrtrain_sweep(dq_wr_dly, pass_window_dq_dly_sweep, num_bit, debug);
	int dq_dly_adj=0;
	dq_dly_adj = pass_window_dq_dly_sweep->end - (dq_dly_window_final>>1);

	if (debug)
		utils_dfi_trace(LOG_ERR, 0xd1cc, "DFI Write Training - Adding %d to dq_wr_dly and dm_wr_dly\n", dq_dly_adj);

	u16 dly_tmp = 0;
	dfi_ck_gate_data(CK_GATE_CS_ALL);
	for (int i=0; i<num_bit; i++) {
		byte_idx = (i-i%8)/8;

		if (i%8==0) {
			ctrl0.b.sel_dbyte  = byte_idx;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			//oddly1.all = dfi_readl(SEL_ODDLY_1);
			if (dq_wr_dly[byte_idx] + dq_dly_adj < 0 )
				dly_tmp = 0;
			else if (dq_wr_dly[byte_idx] + dq_dly_adj > 255)
				dly_tmp = 255;
			else
				dly_tmp = dq_wr_dly[byte_idx] + dq_dly_adj;
			oddly1.b.dm_wr_dly = dly_tmp;
			dfi_writel(oddly1.all,  SEL_ODDLY_1);
			if (debug)
				utils_dfi_trace(LOG_ERR, 0x95ae, "DFI Write Training - Setting Byte[%d] WR delay to %d\n", byte_idx,  oddly1.b.dm_wr_dly);
		}

		ctrl0.b.sel_dbit   = i;
		dfi_writel(ctrl0.all,SEL_CTRL_0);
		oddly2.all = dfi_readl(SEL_ODDLY_2);
		oddly2.b.dq_wr_dly = dly_tmp;
		dfi_writel(oddly2.all,SEL_ODDLY_2);
	}
	dfi_ck_gate_data(CK_GATE_NORMAL);

	ndelay(100);
	if (debug)
		utils_dfi_trace(LOG_ERR, 0xdffc, "DFI Write Training - Finished.\n");

	return 0;
}
/*! @} */