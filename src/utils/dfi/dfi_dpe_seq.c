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
/*! \file dfi_dpe_seq.c
 * @brief provide dfi data path engine sequence APIs
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
#include "bf_mgr.h"
#include "btn_cmd_data_reg.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "btn.h"
#define __FILEID__ dfidpe
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
 * @brief apip to drop all dpe response
 *
 * @return	not used
 */
dfi_code void _bm_proc_res_drop(void)
{
	data_proc_res_pointer_t queue = {
		.all = btn_readl(DATA_PROC_RES_POINTER),
	};
	queue.b.data_proc_res_rptr = queue.b.data_proc_res_wptr;
	btn_writel(queue.all, DATA_PROC_RES_POINTER);
}
/*!
 * @brief Use DPE to copy data from SRAM->DDR or DDR->SRAM
 *
 * @param src		src pointer
 * @param dst		dst pointer
 * @param bytes		bytes
 *
 * @return	not used
 */
dfi_code void dfi_dpe_copy(void *src, void *dst, u32 bytes)
{
	btn_int_mask0_t int_mask0_save = { .all = btn_readl(BTN_INT_MASK0), };
	btn_writel(int_mask0_save.all | BTN_DATA_PROCESS_DONE_MASK,
			BTN_INT_MASK0); // mask data_process_done

	bm_data_copy((u32)src, (u32)dst, bytes, NULL, NULL);

	_bm_proc_res_drop();
	data_proc_req_pointer_t req_q = { .all = btn_readl(DATA_PROC_REQ_POINTER), };
	while (req_q.b.data_proc_req_rptr != req_q.b.data_proc_req_wptr) {
		req_q.all = btn_readl(DATA_PROC_REQ_POINTER);
		_bm_proc_res_drop();
	}

	btn_writel(BTN_DATA_PROCESS_DONE_MASK, BTN_UM_INT_STS);
	btn_writel(int_mask0_save.all, BTN_INT_MASK0); // restore mask
}

/*!
 * @brief CPU write pattern to src, DPE copy from src->dst, CPU read from dst and compare
 *		Write & compare to same dst address twice in order to make sure
 *		that we aren't reading back stale data
 *
 * @param type		type
 * @param src		src pointer
 * @param dst		dst pointer
 * @param bytes		bytes
 * @param debug		true to show debug message
 *
 * @return	result
 */
dfi_code u8 dfi_dpe_verify(u8 type, void *src, void *dst, u32 bytes, bool debug)
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
		if (i >= bytesd2)
			writel(~pattern, src + i);
		else
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
				utils_dfi_trace(LOG_ERR, 0x74b1, "DPE verify write fail at address=%p: expected %x, read %x\n", dst + i, pattern, actual);

			failed = 1;
		} else if (i % 32 == 0) {
			if (debug)
				utils_dfi_trace(LOG_ERR, 0xcdbf, "read %p:%x\n", dst + i, actual);
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
		pattern = ~pattern;
		if (actual != pattern) {
			failed = 1;
			if (debug > 0)
				utils_dfi_trace(LOG_ERR, 0x43ae, "DPE verify write fail at address=%p: expected %x, read %x\n", dst + i, pattern, actual);
		} else if (i % 32 == 0) {
			if (debug)
				utils_dfi_trace(LOG_ERR, 0xd6b1, "read %p:%x\n", dst + i, actual);
		}
	}

	return failed;
}

/*!
 * @brief rite pattern to DDR, DPE copy to SRAM, verify
 *
 * @param type		type
 * @param bytes		bytes
 * @param debug		true to show debug message
 *
 * @return	result
 */
dfi_code u8 dfi_dpe_verify_write(u8 type, u32 bytes, bool debug)
{
	void *src;
	void *dst = (void*) DDR_START;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, &src);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	u8 ret = dfi_dpe_verify(type, src, dst, bytes, debug);

	dtag_put(DTAG_T_SRAM, dtag);
	return ret;
}

/*!
 * @brief rite pattern to DDR, DPE copy to SRAM, verify
 *
 * @param type		type
 * @param bytes		bytes
 * @param debug		true to show debug message
 *
 * @return	result
 */
dfi_code u8 dfi_dpe_verify_read(u8 type, u32 bytes, bool debug)
{
	void *src = (void*) DDR_START;
	void *dst;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, &dst);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	u8 ret = dfi_dpe_verify(type, src, dst, bytes, debug);

	dtag_put(DTAG_T_SRAM, dtag);
	return ret;
}

/*!
 * @brief Write pattern to DDR, DPE copy to SRAM, verify
 *
 * @param repeat	repeat
 *
 * @return	result
 */
dfi_code u8 dfi_dpe_verify_repeat(u32 repeat)
{
	u32 bytes = 2048;

	u8 type;
	u8 ver;
	u8 failed;
	u8 failed_flag = 0;
	int err_cnt = 0;

	for (u32 i = 0; i < repeat; i++) {
		if ((i & 0xFFF) == 0)
			utils_dfi_trace(LOG_ERR, 0xd867, "...on loop=%d.\n", i);
		if ((i & 0x3FFFF) == 0)
			utils_dfi_trace(LOG_ERR, 0x9bad, "...on loop=%d, total accumulated error=%d.\n", i, err_cnt);

		type = (i & 3);
		ver = ((i >> 5) & 1);

		if (ver)
			failed = dfi_dpe_verify_write(type, bytes, false);
		else
			failed = dfi_dpe_verify_read(type, bytes, false);

		if (failed)
			err_cnt += 1;

		failed_flag = (failed_flag | failed);

	}
	utils_dfi_trace(LOG_ERR, 0xb11d, "Finished, total accumulated error=%d.\n", err_cnt);
	return failed_flag;
}

/*!
 * @brief CPU write pattern to src, DPE copy from src->dst, CPU read from dst and compare
 *		Write & compare to same dst address twice in order to make sure
 *		that we aren't reading back stale data
 *
 * @param type		type
 * @param repeat	repeat
 *
 * @return	not used
 */
dfi_code void dfi_dpe_wr_repeat(u8 type, u32 repeat)
{
	void *src;
	void *dst = (void*) DDR_START;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, &src);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
	u32 bytes = 2048;

	u32 pattern;

	// Generate & fill source pattern just once
	//
	// Get data_width to generate appropriate patterns
	// 4=x64, 3=x32, 2=x16
	cs_config_device1_t device1 = { .all = 0 };
	u8 data_width = device1.b.device_die_data_width;
	//
	// CPU fills the source with pattern
	for (u32 i = 0; i < bytes; i += 4) {
		pattern = dfi_gen_pattern(data_width, type, i);
		writel(pattern, src + i);
	}

	// Repeat copy to DDR multiple times
	// Allows for output eye capture
	for (int i = 0; i < repeat; i++) {
		if ((i & 0xFFF) == 0)
			utils_dfi_trace(LOG_ERR, 0x4116, "...on loop=%d.\n", i);
		dfi_dpe_copy(src, dst, bytes);
	}

	dtag_put(DTAG_T_SRAM, dtag);
}


/*!
 * @brief cpu write pattern to DRAM once, dpe copy from DRAM->SRAM, CPU read from SRAM and compare.
 *
 * @param type		type
 * @param repeat	repeat
 *
 * @return	not used
 */
dfi_code void dfi_dpe_rd_repeat(u8 type, u32 repeat)
{
	void *src = (void*) DDR_START;
	void *dst;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, &dst);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
	u32 bytes = 2048;

	u32 pattern;

	// Generate & fill source pattern just once
	//
	// Get data_width to generate appropriate patterns
	// 4=x64, 3=x32, 2=x16
	cs_config_device1_t device1 = { .all = 0 };
	u8 data_width = device1.b.device_die_data_width;
	//
	// CPU fills the source with pattern
	for (u32 i = 0; i < bytes; i += 4) {
		pattern = dfi_gen_pattern(data_width, type, i);
		writel(pattern, src + i);
	}

	// Repeat copy from DDR multiple times
	// Allows for output eye capture
	for (int i = 0; i < repeat; i++) {
		if ((i & 0xFFF) == 0)
			utils_dfi_trace(LOG_ERR, 0xa048, "...on loop=%d.\n", i);
		dfi_dpe_copy(src, dst, bytes);
	}

	dtag_put(DTAG_T_SRAM, dtag);
}
/*! @} */