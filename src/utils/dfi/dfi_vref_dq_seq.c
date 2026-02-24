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
/*! \file dfi_vref_dq_seq.c
 * @brief dfi voltage reference dq training sequence APIs
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
#include "dma.h"
#include "dtag.h"
#include "misc.h"
#define __FILEID__ dfiverf
#include "trace.h"
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef struct _vref_cfg_t{
	u8 vref_range :1;
	u8 vref_value :6;
} vref_cfg_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Extern Data or Functions declaration:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
dfi_code void dfi_vgen_loop(u32 cnt)
{
	vgen_ctrl_0_t r_vgen = { .all = dfi_readl(VGEN_CTRL_0), };

	for (int a = 0; a < cnt; a++) {
		r_vgen.b.vgen_range = 2; // ddr4 lower range
		for (int i = 0; i < 64; i++) {
			r_vgen.b.vgen_vsel = i;
			dfi_writel(r_vgen.all, VGEN_CTRL_0);
			for (int z = 0; z < 100; z++) {
				r_vgen.all = dfi_readl(VGEN_CTRL_0);
			}
			//mdelay(1)
		}

		r_vgen.b.vgen_range = 3; // ddr4 upper range
		for (int i = 0; i < 64; i++) {
			r_vgen.b.vgen_vsel = i;
			dfi_writel(r_vgen.all, VGEN_CTRL_0);
			for (int z = 0; z < 100; z++) {
				r_vgen.all = dfi_readl(VGEN_CTRL_0);
			}
			//mdelay(1);
		}

		r_vgen.b.vgen_range = 0; // ddr4 upper range
		for (int i = 0; i < 64; i++) {
			r_vgen.b.vgen_vsel = i;
			dfi_writel(r_vgen.all, VGEN_CTRL_0);
			for (int z = 0; z < 100; z++) {
				r_vgen.all = dfi_readl(VGEN_CTRL_0);
			}
			//mdelay(1);
		}

		r_vgen.b.vgen_range = 1; // ddr4 upper range
		for (int i = 0; i < 64; i++) {
			r_vgen.b.vgen_vsel = i;
			dfi_writel(r_vgen.all, VGEN_CTRL_0);
			for (int z = 0; z < 100; z++) {
				r_vgen.all = dfi_readl(VGEN_CTRL_0);
			}
			//mdelay(1);
		}
	}

	return;
}

dfi_code u8 dfi_wr_test_pattern(u8 cnt, u32 pattern, u8 pecc_enable, bool debug)
{
	u32 wdata = pattern;
	u32 rdata = 0;
	void *address = (void *) DDR_START;
	while (cnt > 0) {
		cnt--;
		writel(wdata, address);
		rdata = readl(address);
		if (pecc_enable) {
			ecc_err_count_status_t err_cnt_1 = { .all = mc0_readl(ECC_ERR_COUNT_STATUS), };
			ecc_err_count_status_1_t err_cnt_2 = { .all = mc0_readl(ECC_ERR_COUNT_STATUS_1), };
			if (err_cnt_1.b.ecc_1bit_err_count != 0 || err_cnt_2.b.ecc_2bit_err_count != 0) {
				err_cnt_2.b.ecc_1bit_err_count_clr = 1;
				err_cnt_2.b.ecc_2bit_err_count_clr = 1;
				mc0_writel(err_cnt_2.all, ECC_ERR_COUNT_STATUS_1);
				if (debug)
					utils_dfi_trace(LOG_ERR, 0xff2f, "Write Read Test with user pattern - Compare failed! 1 bit ECC error count: %d, 2 bit ECC error count: %d.\n", err_cnt_1.b.ecc_1bit_err_count, err_cnt_2.b.ecc_2bit_err_count);
				return 1;
			}
		} else {
			if (wdata != rdata) {
				if (debug)
					utils_dfi_trace(LOG_ERR, 0xea04, "Write Read Test with user pattern - Compare failed! Expected=0x%x. Read data=0x%x.\n", wdata, rdata);
				return 1;
			}
		}
		wdata = (wdata >> 7) + (wdata >> 21) + (wdata << 7) + (wdata << 21);
		address += 4;
	}
	return 0;
}

dfi_code u8 dfi_wr_test_dpe(u8 pecc_enable, u8 debug)
{
	u8 error = 0;
	void *src;
	void *dst = (void*) DDR_START;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, &src);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
	u32 bytes = 256; //Max 1K 32-bit words

	error = dfi_dpe_verify_training(0, src, dst, bytes, false);
	dtag_put(DTAG_T_SRAM, dtag);

	if (error != 0) {
		if (debug >= 2)
			utils_dfi_trace(LOG_ERR, 0x6625, "dfi_wr_test_pattern_dpe - Compare failed!\n");
		return 1;
	}
	if (pecc_enable == 1) {
		ecc_err_count_status_t err_cnt_1 = { .all = mc0_readl(ECC_ERR_COUNT_STATUS), };
		ecc_err_count_status_1_t err_cnt_2 = { .all = mc0_readl(ECC_ERR_COUNT_STATUS_1), };
		if (err_cnt_1.b.ecc_1bit_err_count != 0 || err_cnt_2.b.ecc_2bit_err_count != 0) {
			err_cnt_2.b.ecc_1bit_err_count_clr = 1;
			err_cnt_2.b.ecc_2bit_err_count_clr = 1;
			mc0_writel(err_cnt_2.all, ECC_ERR_COUNT_STATUS_1);
			if (debug)
				utils_dfi_trace(LOG_ERR, 0x9322, "dfi_wr_test_pattern_dpe - ECC check failed! 1 bit ECC error count: %d, 2 bit ECC error count: %d.\n", err_cnt_1.b.ecc_1bit_err_count, err_cnt_2.b.ecc_2bit_err_count);
			return 1;
		}
	}

	return 0;
}

dfi_code int dfi_vref_dq_sweep(u8 cs, u8 sweep_start, u8 sweep_end, vref_cfg_t* vref_pass, vref_cfg_t* vref_fail, u8 debug)
{
	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0x492a, "DFI Vref DQ Training - Sweep DRAM DQ VREF at Range %d, Value from %d to %d\n", vref_pass->vref_range + 1, sweep_start, sweep_end);

	u8 pecc_enable;
	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	pecc_enable = ras.b.ecc_enb;

	vref_cfg_t cur;
	vref_cfg_t *vref_cur = &cur;
	vref_cur->vref_range = vref_pass->vref_range;

	device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
	config.b.dq_vref_training = 1;
	config.b.dq_vref_training_range = vref_cur->vref_range;
	mc0_writel(config.all, DEVICE_CONFIG_TRAINING);

	//First MR cmd to put DRAM in VREF DQ Training mode
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);
	ndelay(1000);

	//u32 pattern;
	u8 errors = 0;
	u8 vref_val_pass_tmp = 255;
	for (vref_cur->vref_value = sweep_start; vref_cur->vref_value <= sweep_end; vref_cur->vref_value++) {
		//Issue MRW cmd to update VREF Value config
		config.b.dq_vref_training_value = vref_cur->vref_value;
		mc0_writel(config.all, DEVICE_CONFIG_TRAINING);
		dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs); // program new VREF value into DDR
		ndelay(1000);

		// Perform data test to see if current VREF value is good
		//u32 pattern = dfi_simple_hash((u32) vref_cur->vref_value );
		//errors = dfi_wr_test_pattern(64, pattern, pecc_enable, debug);
		errors = dfi_wr_test_dpe(pecc_enable, debug);

		if (errors > 0) {	//Write Read error found
			if (debug >= 2)
				utils_dfi_trace(LOG_ERR, 0x0914, "Vref DQ Training - Write Read error found, Bad VREF of Range %d, Value %d\n", vref_cur->vref_range + 1, vref_cur->vref_value);

			if (vref_val_pass_tmp != 255 && (vref_cur->vref_value - vref_val_pass_tmp) > (vref_fail->vref_value - vref_pass->vref_value)) {
				vref_fail->vref_value = vref_cur->vref_value;
				vref_pass->vref_value = vref_val_pass_tmp;
			}
			vref_val_pass_tmp = 255;
		} else {		//No error
			if (debug >= 2)
				utils_dfi_trace(LOG_ERR, 0x1e75, "Vref DQ Training - No Write Read error, Good VREF of Range %d, Value %d\n", vref_cur->vref_range + 1, vref_cur->vref_value);

			if (vref_val_pass_tmp == 255) {
				vref_val_pass_tmp = vref_cur->vref_value;
			}
		}

	}

	//If still Good VREF at sweep_end, calculate the passing window from passing point to sweep_end + 1
	if (vref_val_pass_tmp != 255 && (sweep_end + 1 - vref_val_pass_tmp) > (vref_fail->vref_value - vref_pass->vref_value)) {
		vref_fail->vref_value = sweep_end + 1;
		vref_pass->vref_value = vref_val_pass_tmp;
	}

	if (debug >= 2) {
		if ((vref_fail->vref_value - vref_pass->vref_value) == 0) {
			utils_dfi_trace(LOG_ERR, 0xd26f, "DFI Vref DQ Training - No valid VREF value at current Range %d\n", vref_cur->vref_range + 1);
		}
	}

	//Exit VREF DQ training
	config.b.dq_vref_training = 0;
	mc0_writel(config.all, DEVICE_CONFIG_TRAINING);
	//Last MR cmd to exit VREF training
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);
	ndelay(1000);

	return 0;
}

dfi_code int dfi_vref_dq_seq(u8 target_cs, u8 debug)
{
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	if (mode.b.dram_type != MC_DDR4) {
		utils_dfi_trace(LOG_ERR, 0x7f4e, "DFI Vref DQ Training - Vref DQ Training is for DDR4 only, current DRAM type is 0x%x\n", mode.b.dram_type);
		utils_dfi_trace(LOG_ERR, 0x6ca8, "DFI Vref DQ Training - DRAM type: 0x1 -> DDR3, 0x0-> DDR4, 0xA -> LPDDR4\n");
		return -1;
	}

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

	if (debug)
		utils_dfi_trace(LOG_ERR, 0x6974, "DFI Vref DQ Training - Enter DDR4 VREF DQ Training mode, Target CS[%d]\n", target_cs);
	//Stop sending Refresh command during training

	mc_dfc_cmd_t dfc = { .all = mc0_readl(MC_DFC_CMD), };
	dfc.b.dfc_mode_enb = 1;
	mc0_writel(dfc.all, MC_DFC_CMD); //halt traffic

	vref_cfg_t pass0;
	vref_cfg_t fail0;
	vref_cfg_t *vref_pass_r0 = &pass0;
	vref_cfg_t *vref_fail_r0 = &fail0;
	vref_pass_r0->vref_value = 0;
	vref_fail_r0->vref_value = 0;

	//Sweep DRAM DQ VREF range 1 from 60% (0) to 92.5% (50)
	u8 sweep_start_r0 = 0;
	u8 sweep_end_r0 = 50;
	vref_pass_r0->vref_range = 0;
	vref_fail_r0->vref_range = 0;
	dfi_vref_dq_sweep(cs, sweep_start_r0, sweep_end_r0, vref_pass_r0, vref_fail_r0, debug);
	if (debug)
		utils_dfi_trace(LOG_ERR, 0x7332, "DFI Vref DQ Training - Range %d, Pass %d, Fail %d\n", vref_pass_r0->vref_range + 1, vref_pass_r0->vref_value, vref_fail_r0->vref_value);

	vref_cfg_t pass1;
	vref_cfg_t fail1;
	vref_cfg_t *vref_pass_r1 = &pass1;
	vref_cfg_t *vref_fail_r1 = &fail1;
	vref_pass_r1->vref_value = 0;
	vref_fail_r1->vref_value = 0;

	//Sweep DRAM DQ VREF range 2 from 45% (0) to 60% (24)
	u8 sweep_start_r1 = 0;
	u8 sweep_end_r1 = 24;
	vref_pass_r1->vref_range = 1;
	vref_fail_r1->vref_range = 1;
	dfi_vref_dq_sweep(cs, sweep_start_r1, sweep_end_r1, vref_pass_r1, vref_fail_r1, debug);
	if (debug)
		utils_dfi_trace(LOG_ERR, 0x066d, "DFI Vref DQ Training - Range %d, Pass %d, Fail %d\n", vref_pass_r1->vref_range + 1, vref_pass_r1->vref_value, vref_fail_r1->vref_value);

	//Calculate final VREF config based on sweep results
	vref_cfg_t final;
	vref_cfg_t *vref_final = &final;

	u8 vref_window_r0 = vref_fail_r0->vref_value - vref_pass_r0->vref_value;
	u8 vref_window_r1 = vref_fail_r1->vref_value - vref_pass_r1->vref_value;
	u8 final_window;

	if (vref_pass_r0->vref_value == 0 && vref_fail_r1->vref_value == sweep_end_r1 + 1) { //VREF settings are valid across vref_range
		if (vref_window_r0 > vref_window_r1) { //Range 0 has bigger passing window
			vref_final->vref_range = 0;
			vref_final->vref_value = (vref_fail_r0->vref_value >> 1) - ((vref_window_r1) >> 1);
		} else { //Range 2 has bigger passing window
			vref_final->vref_range = 1;
			vref_final->vref_value = (vref_fail_r1->vref_value >> 1) + ((vref_window_r0) >> 1);
		}
		final_window = vref_window_r0 + vref_window_r1;
	} else { //Valid VREF settings are not across vref_range
		if ((vref_window_r0) > (vref_window_r1)) { //Range 1 has bigger passing window
			vref_final->vref_range = 0;
			vref_final->vref_value = (vref_fail_r0->vref_value >> 1) + (vref_pass_r0->vref_value >> 1);
			final_window = vref_window_r0;
		} else { //Range 2 has bigger passing window
			vref_final->vref_range = 1;
			vref_final->vref_value = (vref_fail_r1->vref_value >> 1) + (vref_pass_r1->vref_value >> 1);
			final_window = vref_window_r1;
		}
	}

	// Print 1D vector to show all passing/failing points
	if (debug) {
		utils_dfi_trace(LOG_ERR, 0x0a54, "Range 2 value 0 (45%%)                                  Range 1 value 50 (92.5%%)\n");
		utils_dfi_trace(LOG_ERR, 0xd468, "MIN                                                                      MAX\n");
		u8 vref_norm;

		if (vref_final->vref_range == 0)
			vref_norm = sweep_end_r1 + 1;
		else
			vref_norm = 0;

		vref_norm += vref_final->vref_value;

		for (u8 z = 0; z < sweep_end_r1 + 1; z++) {
			if (z == vref_norm)
				utils_dfi_trace(LOG_ERR, 0x8324, "@");
			else if (z < vref_pass_r1->vref_value)
				utils_dfi_trace(LOG_ERR, 0x1642, "F");
			else if (z < vref_fail_r1->vref_value)
				utils_dfi_trace(LOG_ERR, 0x8334, "P");
			else
				utils_dfi_trace(LOG_ERR, 0x5d7f, "F");
		}

		for (u8 z = sweep_end_r1 + 1; z < sweep_end_r1 + 1 + sweep_end_r0 + 1; z++) {
			if (z == vref_norm)
				utils_dfi_trace(LOG_ERR, 0x36de, "@");
			else if (z < vref_pass_r0->vref_value + sweep_end_r1 + 1)
				utils_dfi_trace(LOG_ERR, 0xb613, "F");
			else if (z < vref_fail_r0->vref_value + sweep_end_r1 + 1)
				utils_dfi_trace(LOG_ERR, 0x91df, "P");
			else
				utils_dfi_trace(LOG_ERR, 0x8d5a, "F");
		}

		utils_dfi_trace(LOG_ERR, 0xdee7, "\n");
		utils_dfi_trace(LOG_ERR, 0x14d0, "DFI Vref DQ Training - Training finished, set trained VREF range: %d, VREF value: %d\n", vref_final->vref_range + 1, vref_final->vref_value);
	}

	device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
	config.b.dq_vref_training = 1;
	config.b.dq_vref_training_range = vref_final->vref_range;
	config.b.dq_vref_training_value = vref_final->vref_value;
	mc0_writel(config.all, DEVICE_CONFIG_TRAINING);

	//First MR cmd to put DRAM in VREF DQ Training mode
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);
	ndelay(1000);
	//mdelay(1);
	//Second MR cmd to write in trained VREF config
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);
	ndelay(1000);
	//mdelay(1);

	//Exit VREF DQ training
	config.b.dq_vref_training = 0;
	mc0_writel(config.all, DEVICE_CONFIG_TRAINING);
	//Last MR cmd to exit VREF training
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);
	ndelay(1000);
	//mdelay(1);
	//Restore refresh command after training

	dfc.b.dfc_mode_enb = 0;
	mc0_writel(dfc.all, MC_DFC_CMD); //halt traffic

#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0xe74b, "dfi_vref_dq_seq - Total cycles take: %d.\n", pc_val);
#endif

	if (final_window < 8) {
		utils_dfi_trace(LOG_ERR, 0x68dc, "DFI Vref DQ Training - Error. Passing write DQ VREF range=%d is too small.\n", final_window);
		return -1;
	}

	return 0;
}

dfi_code int dfi_dev_vref_cfg(u8 target_cs, u8 range, u8 value)
{
	device_mode_t mode = { .all = mc0_readl(DEVICE_MODE), };
	if (mode.b.dram_type != 0) {
		utils_dfi_trace(LOG_ERR, 0xb67e, "Device Vref config - Device Vref config is for DDR4 only, current DRAM type is 0x%x\n", mode.b.dram_type);
		utils_dfi_trace(LOG_ERR, 0x2978, "Device Vref config - DRAM type: 0x1 -> DDR3, 0x0-> DDR4, 0xA -> LPDDR4\n");
		return -1;
	}

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
	utils_dfi_trace(LOG_ERR, 0x65f2, "Device Vref config - Config Device[%d] VREF to Range: %d, Value:0x%x\n", target_cs, range, value);
	device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
	config.b.dq_vref_training = 1;
	config.b.dq_vref_training_range = range;
	config.b.dq_vref_training_value = value;
	mc0_writel(config.all, DEVICE_CONFIG_TRAINING);

	//Stop sending Refresh command during training
	mc_dfc_cmd_t dfc = { .all = mc0_readl(MC_DFC_CMD), };
	dfc.b.dfc_mode_enb = 1;
	mc0_writel(dfc.all, MC_DFC_CMD); //halt traffic

	//First MR cmd to put DRAM in VREF DQ Training mode
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);

	mdelay(1);
	//Second MR cmd to write in trained VREF config
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);

	mdelay(1);
	//Exit VREF DQ training
	config.b.dq_vref_training = 0;
	mc0_writel(config.all, DEVICE_CONFIG_TRAINING);
	//Last MR cmd to exit VREF training
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cs);

	mdelay(1);
	//Restore refresh command after training
	dfc.b.dfc_mode_enb = 0;
	mc0_writel(dfc.all, MC_DFC_CMD);

	utils_dfi_trace(LOG_ERR, 0xb251, "Device Vref config - Config successfully finished.\n");

	return 0;
}
/*! @} */
