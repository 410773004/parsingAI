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
/*! \file dfi_wr_dqdqs_dly_seq.c
 * @brief dfi write dq/dqs delay training sequence APIs
 *		Deskew write DQ/DM vs DQS delay to balance output setup/hold time
 * 		Full training (TRAIN_FULL) deskews each byte separately
 * 		Fast training (TRAIN_FAST) treats all byte pass/fail as one entity for faster speed
 * 		Window training (TRAIN_WIND) same as TRAIN_FAST, except dont update DQ/DM delays. Get window size only (for validation)
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
#include "dma.h"
#include "dtag.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "stdio.h"
#define __FILEID__ dfiwr
#include "trace.h"
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define TEST_SIZE 512

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
dfi_code int dfi_wrdqdqs_pushdq(u8 train_type, u8 byte_val, int offset)
{
	int capped = 0;
	int dly_tmp = 0;
	sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
	sel_oddly_1_t oddly1;
	sel_oddly_2_t oddly2;

	dfi_ck_gate_data(CK_GATE_CS_ALL); // must set ck_gate before changing output delays

	// TRAIN_FAST sweeps from 0 to byte_val. TRAIN_FULL only pushes by==byte_val
	u8 num_byte = train_type == TRAIN_FULL ? byte_val + 1 : byte_val ;
	u8 byte_idx = train_type == TRAIN_FULL ? byte_val     : 0 ;

	for (u8 by = byte_idx; by < num_byte ; by++) {
		ctrl0.b.sel_dbyte = by;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		oddly1.all = dfi_readl(SEL_ODDLY_1);
		dly_tmp = oddly1.b.dm_wr_dly;

		if ((dly_tmp + offset) > 255) {
			//utils_dfi_trace(LOG_ERR, 0, "dfi_wrdqdqs_pushdq - Warning: By[%d] delay overflow, set to 255!\n", by);
			dly_tmp = 255;
			capped = 1;
		} else if ((dly_tmp + offset) < 0) {
			//utils_dfi_trace(LOG_ERR, 0, "dfi_wrdqdqs_pushdq - Warning: By[%d] delay underflow, set to 0!\n", by);
			dly_tmp = 0;
			capped = 1;
		} else {
			dly_tmp = dly_tmp + offset;
		}

		oddly1.b.dm_wr_dly = dly_tmp;
		dfi_writel(oddly1.all, SEL_ODDLY_1);

		for (u8 bi = by * 8; bi < (by + 1) * 8; bi++) {
			ctrl0.b.sel_dbit = bi;
			dfi_writel(ctrl0.all, SEL_CTRL_0);
			oddly2.all = dfi_readl(SEL_ODDLY_2);
			oddly2.b.dq_wr_dly = dly_tmp;
			dfi_writel(oddly2.all, SEL_ODDLY_2);
		}
//		if (by==0) utils_dfi_trace(LOG_ERR, 0, "dfi_wrdqdqs_pushdq - byte 0 set to %d!\n", dly_tmp);
	}

	dfi_ck_gate_data(CK_GATE_NORMAL);

	return capped;
}

dfi_code int dfi_wrdqdqs_wr_test(u8 train_type, u8 byte_idx, u8 pecc_enable, u8 data_width)
{
	u8 is_ecc_byte = 0;
	u32 pattern;
	u32 actual;

	void *src;
	void *dst = (void*) DDR_START;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, &src);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	src = (void*) ((u32) src & 0xFFFFFFE0);
	dst = (void*) ((u32) dst & 0xFFFFFFE0);

	u32 bytes = TEST_SIZE; //Max 1K 32-bit words
	u32 bytesd2 = bytes >> 1;
	bytes = bytes & 0xFFFFFFE0;
	bytesd2 = bytesd2 & 0xFFFFFFE0;

	if (train_type == TRAIN_FULL) { // TRAIN_FAST uses dpe_verify, so no need to generate byte pattern
		if (pecc_enable == 1 && ((byte_idx == 4 && data_width == 3) || (byte_idx == 8 && data_width == 4))) {
			is_ecc_byte = 1;
		}

		// CPU fills the source with pattern
		for (u32 i = 0; i < bytes; i += 4) {
			pattern = dfi_gen_pattern(data_width, 0, i);
			writel(pattern, src + i);
		}
	}

	// Clear error counters (otherwise can't determine if ECC byte is passing or failing)
	ecc_err_count_status_1_t err_cnt_2 = { .all = mc0_readl(ECC_ERR_COUNT_STATUS_1), };
	err_cnt_2.b.ecc_1bit_err_count_clr = 1;
	err_cnt_2.b.ecc_2bit_err_count_clr = 1;
	mc0_writel(err_cnt_2.all, ECC_ERR_COUNT_STATUS_1);

	// TRAIN_FULL needs to determine which part of the pattern is for this particular byte
	// TRAIN_FAST just uses dpe_verify to check the entire pattern
	if ((is_ecc_byte != 1) && (train_type == TRAIN_FULL)) {
		dfi_dpe_copy(src, dst, bytesd2);
		u32 start = 0;
		if (byte_idx >= 4) {
			start = 4;
			byte_idx -= 4;
		}
		// CPU read from destination to verify lower half data
		for (u32 i = start; i < bytesd2; i += 8) {
			actual = readl(dst + i);
			pattern = dfi_gen_pattern(data_width, 0, i);
			actual = (actual & (0xFF << (byte_idx << 3)));
			pattern = (pattern & (0xFF << (byte_idx << 3)));
			if (actual != pattern) {
				dtag_put(DTAG_T_SRAM, dtag);
				return 1;
			}
		}
		// DPE copies from source (upper half) to destination (lower half), overwriting previous data
		dfi_dpe_copy(src + bytesd2, dst, bytesd2);
		// CPU read from destination to verify upper half data
		u32 i_mod;
		for (u32 i = start; i < bytesd2; i += 8) {
			actual = readl(dst + i); // destination address is the same as the lower half
			i_mod = i + bytesd2;
			pattern = dfi_gen_pattern(data_width, 0, i_mod); // but expected pattern is generated from upper half address
			actual = (actual & (0xFF << (byte_idx << 3)));
			pattern = (pattern & (0xFF << (byte_idx << 3)));
			if (actual != pattern) {
				dtag_put(DTAG_T_SRAM, dtag);
				return 1;
			}
		}
	} else {
		if (dfi_dpe_verify_training(0, src, dst, bytes, false) != 0) {
			dtag_put(DTAG_T_SRAM, dtag);
			return 1;
		}
	}

	ecc_err_count_status_t err_cnt_1 = { .all = mc0_readl(ECC_ERR_COUNT_STATUS), };
	err_cnt_2.all = mc0_readl(ECC_ERR_COUNT_STATUS_1);
	if (err_cnt_1.b.ecc_1bit_err_count != 0 || err_cnt_2.b.ecc_2bit_err_count != 0) {
//		err_cnt_2.b.ecc_1bit_err_count_clr = 1;
//		err_cnt_2.b.ecc_2bit_err_count_clr = 1;
		mc0_writel(err_cnt_2.all, ECC_ERR_COUNT_STATUS_1); //clear ECC error counters
		dtag_put(DTAG_T_SRAM, dtag);
		return 1;
	}

	dtag_put(DTAG_T_SRAM, dtag);
	return 0;
}

dfi_code int dfi_wrdqdqs_sweep(u8 train_type, u8 byte_val, u8 setup_hold, pass_window_t * window, u8 debug)
{
	//max push is 30 delay line settings, about 300ps, which is enough to sweep setup or hold margin.
	//May set push_max to higher value if trainng is performed at lower DDR speed.
	//Assuming after Write leveling, DQ delay is large enough to push left.
	u8 push_max = 30;
	u8 start = 0;
	u8 error = 0;
	int offset = 0;
	u8 capped = 0;
	u16 dly_org [9]; // up to 9 bytes
	u8 pecc_enable = 0;

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0x4c4c, "dfi_wrdqdqs_sweep - Pushing max is set to %d\n", push_max);

	device_mode_t r_mode = { .all = mc0_readl(DEVICE_MODE) };
	u8 data_width = r_mode.b.data_width;

	//Record the original delay setting before sweep
	sel_ctrl_0_t ctrl0 = { .all = dfi_readl(SEL_CTRL_0), };
	sel_oddly_1_t oddly1;
	sel_oddly_2_t oddly2;

	// TRAIN_FAST sweeps from 0 to byte_val. TRAIN_FULL only pushes by==byte_val
	u8 num_byte = train_type == TRAIN_FULL ? byte_val + 1 : byte_val ;
	u8 byte_idx = train_type == TRAIN_FULL ? byte_val     : 0        ;

	for (u8 by = byte_idx; by < num_byte ; by++) {
		ctrl0.b.sel_dbyte = by;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		oddly1.all = dfi_readl(SEL_ODDLY_1);
		dly_org[by] = oddly1.b.dm_wr_dly;
	}

	ras_cntl_t ras = { .all = mc0_readl(RAS_CNTL), };
	pecc_enable = ras.b.ecc_enb;

	if (setup_hold == 0) {
		offset = 1;
	} else {
		offset = -1;
	}

	for (int i = 0; i <= push_max; i++) {
		if (i != 0) {
			if (train_type == TRAIN_FULL)
				capped = dfi_wrdqdqs_pushdq(train_type, byte_idx, offset);
			else
				capped = dfi_wrdqdqs_pushdq(train_type, num_byte, offset);
			ndelay(100);
		}
		error = dfi_wrdqdqs_wr_test(train_type, byte_idx, pecc_enable, data_width);
		if (error == 1) {
			if (start == 1) {
				window->end = i;
				break; //No more pushing needed, if error happens after passing window starts
			}
		} else {
			if (start == 0) {
				window->start = i;
				start = 1;
			}
			if (i == push_max || capped == 1) {
				window->end = i;
			}
		}
		if (capped == 1) {
			break; //delay line setting overflow or underflow, cannot push further.
		}
	}

	if (debug >= 2)
		utils_dfi_trace(LOG_ERR, 0xb4d1, "dfi_wrdqdqs_sweep - Byte[%d]: Sweep direction: %d (0 for right, 1 for left), passing windows starts at %d, ends at %d.\n", byte_idx, setup_hold, window->start, window->end);

	//Restore the original delay setting after sweep
	dfi_ck_gate_data(CK_GATE_CS_ALL);
	for (u8 by = byte_idx; by < num_byte; by++) {
		ctrl0.b.sel_dbyte = by;
		dfi_writel(ctrl0.all, SEL_CTRL_0);
		oddly1.b.dm_wr_dly = dly_org[by];
		dfi_writel(oddly1.all, SEL_ODDLY_1);
		if (debug >= 3)
			utils_dfi_trace(LOG_ERR, 0x1bf7, "dfi_wrdqdqs_sweep - Restore DM %d delay = %d.\n", by, dly_org[by]);

		for (u8 bi = by * 8; bi < (by + 1) * 8; bi++) {
			ctrl0.b.sel_dbit = bi;
			dfi_writel(ctrl0.all, SEL_CTRL_0);
			oddly2.all = dfi_readl(SEL_ODDLY_2);
			oddly2.b.dq_wr_dly = dly_org[by];
			dfi_writel(oddly2.all, SEL_ODDLY_2);
		}
	}
	oddly2.all = dfi_readl(SEL_ODDLY_2); // dummy read before turning off ck_gate
	dfi_ck_gate_data(CK_GATE_NORMAL);

	return 0;
}


dfi_code int dfi_wr_dqdqs_dly_seq(u8 target_cs, u8 train_type, u8 debug)
{
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	u8 num_byte = 1;

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
	u8 data_width = mode.b.data_width;

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
	if (ras.b.ecc_enb == 1) { //ECC is ON
		num_byte = num_byte + 1;
	}

	if (debug >= 1) {
		if (train_type == TRAIN_FULL)
			utils_dfi_trace(LOG_ERR, 0x667e, "dfi_wr_dqdqs_dly_seq - Full training start, target CS[%d], bytes: %d.\n", target_cs, num_byte);
		else if (train_type == TRAIN_FAST)
			utils_dfi_trace(LOG_ERR, 0x4ab6, "dfi_wr_dqdqs_dly_seq - Fast training start, target CS[%d], bytes: %d.\n", target_cs, num_byte);
		else
			utils_dfi_trace(LOG_ERR, 0x7162, "dfi_wr_dqdqs_dly_seq - Window training start, target CS[%d], bytes: %d.\n", target_cs, num_byte);
	}

	pass_window_t right0;
	pass_window_t left0;
	pass_window_t *right = &right0; //Push DQ right, sweep setup time
	pass_window_t *left = &left0; //Push DQ left, sweep hold time
	right->start = 0;
	right->end = 0;
	left->start = 0;
	left->end = 0;
	int right_window_size = 0;
	int left_window_size = 0;
	int offset = 0;
	int worst_byte = 255;
	int worst_window_size = 255;


	u8 by_start  = train_type == TRAIN_FAST ? 0 : 0;
	u8 by_end    = train_type == TRAIN_FAST ? 1 : num_byte;
	u8 byte_val;

	//Per Byte sweep setup and hold time of DQS sampling DQ
	for (u8 by = by_start; by < by_end; by++) {
		byte_val = train_type == TRAIN_FULL ? by : num_byte;
		dfi_wrdqdqs_sweep(train_type, byte_val, 0, right, debug);
		dfi_wrdqdqs_sweep(train_type, byte_val, 1, left, debug);
		right_window_size = right->end - right->start;
		left_window_size = left->end - left->start;

		if (right_window_size > 0 && left_window_size == 0) { //Right side window only
			if (worst_window_size == 255 || right_window_size < worst_window_size) {
				worst_window_size = right_window_size;
				worst_byte = by;
			}
			offset = right->start + (right_window_size >> 1);
			if (debug >= 1)
				utils_dfi_trace(LOG_ERR, 0xa292, "dfi_wr_dqdqs_dly_seq - Byte[%d]: Passing window size = %d, right window only, push DQ by %d.\n", by, right_window_size, offset);

			if (train_type != TRAIN_WIND)
				dfi_wrdqdqs_pushdq(train_type, byte_val, offset);

		} else if (right_window_size == 0 && left_window_size > 0) { //Left side window only
			if (worst_window_size == 255 || left_window_size < worst_window_size) {
				worst_window_size = left_window_size;
				worst_byte = by;
			}
			offset = left->start + (left_window_size >> 1);
			if (debug >= 1)
				utils_dfi_trace(LOG_ERR, 0x49de, "dfi_wr_dqdqs_dly_seq - Byte[%d]: Passing window size = %d, left window only, push DQ by %d.\n", by, left_window_size, -offset);

			if (train_type != TRAIN_WIND)
				dfi_wrdqdqs_pushdq(train_type, byte_val, -offset);
		} else if (right_window_size > 0 && left_window_size > 0) { //Both right and left side window
			if (right->start == 0 && left->start == 0) {
				if (worst_window_size == 255 || (right_window_size + left_window_size) < worst_window_size) {
					worst_window_size = (right_window_size + left_window_size);
					worst_byte = by;
				}
				offset = ((right_window_size - left_window_size) >> 1);
				if (debug >= 1)
					utils_dfi_trace(LOG_ERR, 0x14fd, "dfi_wr_dqdqs_dly_seq - Byte[%d]: Passing window size = %d, push DQ by %d.\n", by, (right_window_size + left_window_size), offset);

				if (train_type != TRAIN_WIND)
					dfi_wrdqdqs_pushdq(train_type, byte_val, offset);
			} else { //discontinued window means no valid passing window
				if (debug >= 1)
					utils_dfi_trace(LOG_ERR, 0xac72, "dfi_wr_dqdqs_dly_seq - Byte[%d]: Discontinued passing window, no DQ delay push\n", by);
				worst_window_size = 0;
				worst_byte = by;
			}
		} else { //no right side window or left side window
			if (debug >= 1)
				utils_dfi_trace(LOG_ERR, 0x0b90, "dfi_wr_dqdqs_dly_seq - Byte[%d]: No passing window, no DQ delay push\n", by);
			worst_window_size = 0;
			worst_byte = by;
		}
	}

#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0xf983, "dfi_wr_dqdqs_dly_seq - Total cycles take: %d.\n", pc_val);
#endif

	if (debug >= 1)
		utils_dfi_trace(LOG_ERR, 0xef6c, "dfi_wr_dqdqs_dly_seq - Training finished, Byte[%d] has worst passing window size %d.\n", worst_byte, worst_window_size);

	return worst_window_size;
}

#if (ENABLE_DFI_VALIDATION_DEBUG == 1)
// Validation-use code
// Loop dfi_wr_dqdqs_dly_seq multiple times
// Use TRAIN_WIND so that the delay values do not change, and only the worst window is returned
// Can run this test for a large numer of loops while applying external changes such as temperature
#define ASIZE 10
dfi_code void dfi_wr_dqdqs_dly_loop(u8 target_cs, u32 loop)
{
	int worst_window_size ;
	int wws_arr[ASIZE] ;
	u32 count_arr[ASIZE] ;
	u8 valid_arr[ASIZE] ;
	u8 match;

	utils_dfi_trace(LOG_ERR, 0x4c8c, "dfi_wr_dqdqs_dly_loop start (prints a message every 10000 loops).\n");

	for (u8 j = 0 ; j < ASIZE ; j++) {
		valid_arr[j] = 0;
		wws_arr[j] = 0;
		count_arr[j] = 0;
	}

	for (u32 l = 0; l < loop; l++) {
		if (l%10000 == 0)
			utils_dfi_trace(LOG_ERR, 0x31d4, "On loop %d...\n", l);

		worst_window_size = dfi_wr_dqdqs_dly_seq(0, target_cs, TRAIN_WIND); // disable debug
		//utils_dfi_trace(LOG_ERR, 0, "loop %d - worst_window_size = %d.\n", l, worst_window_size);

		match = 0;
		for (u8 j = 0 ; j < ASIZE ; j++) {
			if (valid_arr[j] == 1) {
				if (wws_arr[j] == worst_window_size) {
					count_arr[j]++;
					match = 1;
				}
			}
			else if (match == 0) {
				wws_arr[j] = worst_window_size;
				valid_arr[j] = 1;
				count_arr[j] = 1;
				match = 1;
			}
		}
		if (match == 0) {
			utils_dfi_trace(LOG_ERR, 0x1c97, "Error, array size %d is not enough to capture all results.\n", ASIZE);
		}
	}

	for (u8 j = 0 ; j < ASIZE ; j++) {
		utils_dfi_trace(LOG_ERR, 0xef65, "Entry #%d. Valid = %d. Window_size = %d. Count = %d.\n", j, valid_arr[j], wws_arr[j], count_arr[j]);
	}

	utils_dfi_trace(LOG_ERR, 0xaad8, "dfi_wr_dqdqs_dly_loop end.\n");
	return;
}
#endif

/*! @} */