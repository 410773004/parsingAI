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
/*! \file dfi_console.c
 * @brief provide quick DFI access APIs from uart console
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
#include "dfi_init.h"
#include "dfi_common.h"
#include "dfi_config.h"
#include "bf_mgr.h"
#include "btn_cmd_data_reg.h"
#include "btn.h"
#include "stdio.h"
#include "string.h"
#include "console.h"
#include "io.h"
#include "mc_reg.h"
#include "ddr.h"
#include "fwconfig.h"
#include "a0_rom_10011.h"
#include "ddr_info.h"
#define __FILEID__ dficonsole
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
static inline void __dmb(void) { asm volatile ("dmb"); }

extern volatile tDRAM_Training_Result   DRAM_Train_Result;
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
#if (ENABLE_DFI_BASIC_DEBUG == 1)
norm_ps_code int write_main(int argc, char *argv[])
{
	char *p;
	char *q;
	u32 reg = strtoul(argv[1], &p, 0);
	u32 value = strtoul(argv[2], &q, 0);
	utils_dfi_trace(LOG_ERR, 0x6caa, "\nR0x%x : (0x%x)",reg, value);
	writel(value, (void *)reg);
	return 0;
}
static DEFINE_UART_CMD(write, "write",
		"write [address(hex)] [value(hex)]",
		"write 0x40000000 0x12345678",
		2, 2, write_main);

norm_ps_code int read_main(int argc, char *argv[])
{
	char *p;
	u32 reg = strtoul(argv[1], &p, 0);
	utils_dfi_trace(LOG_ERR, 0x338f, "\nR0x%x : (0x%x)", reg, readl((const void *)reg));
	return 0;
}
static DEFINE_UART_CMD(read, "read",
		"read [address(hex)]",
		"read 0x00000000",
		1, 1, read_main);

norm_ps_code int dump_main(int argc, char *argv[])
{
	char *p;
	u32 reg = strtoul(argv[1], &p, 0);
	u32 dwsize = atoi(argv[2]);

	for (int i = 0; i < dwsize; i++) {
		utils_dfi_trace(LOG_ERR, 0x2870, "\nR0x%x : (0x%x)", reg, readl((const void *)reg));
		reg += 4;
	}
	return 0;
}
static DEFINE_UART_CMD(dump, "dump",
		"dump [address(hex)] [dword size(dec)]",
		"dump 0x40000000 100",
		2, 2, dump_main);

slow_code_ex int verify_main(int argc, char *argv[])
{
	char *p;
	u32 reg = strtoul(argv[1], &p, 0);
	u32 dwsize = (u32) atoi(argv[2]);
	u8 display = (u8) atoi(argv[3]);

	void *address = (void*) reg;
	void *write_address = address;

	//u32 size = dwsize/64;
	int i = 0;
	int pattern = 0;
	int ad_cnt = 0;
	for (i = 0; i < dwsize; i++) {

	//#define DDR_SSO

#ifdef DDR_SSO
      if (((ad_cnt & 0x3)==0) || ((ad_cnt & 0x3) ==1))
	pattern = 0;
      else
	pattern = 0xffffffff;
      ad_cnt++;
#else
		pattern = i + (i << 6) + (i << 17) + (i << 27) + (i >> 5)
				+ (i >> 18) + (i >> 26) + ad_cnt;
#endif
		writel(pattern, write_address);

		if (display == 1 || (i % 8388608) == 0) {
			utils_dfi_trace(LOG_ERR, 0xb137, "Write %p:%x\n", write_address, pattern);
		}
		write_address += 4;
	}

	utils_dfi_trace(LOG_ERR, 0x7a07, "Write Finished!\n");

	ad_cnt = 0;
	for (i = 0; i < dwsize; i++) {
		//pattern = 0x55555555;
#ifdef DDR_SSO
      if (((ad_cnt & 0x3)==0) || ((ad_cnt & 0x3) ==1))
	pattern = 0;
      else
	pattern = 0xffffffff;
      ad_cnt++;
#else
		pattern = i + (i << 6) + (i << 17) + (i << 27) + (i >> 5)
				+ (i >> 18) + (i >> 26);
#endif

		u32 j = readl(address);
		if (j != pattern)
			utils_dfi_trace(LOG_ERR, 0x5c8a, "Fail at %p: expected %x, read %x\n", address,
					pattern, j);
		else if (display == 1 || (i % 8388608) == 0) {
			utils_dfi_trace(LOG_ERR, 0xb0b0, "read %p:%x\n", address, j);
		}

		address += 4;
	}
	utils_dfi_trace(LOG_ERR, 0xc3be, "\nDone\n");
	return 0;
}

static DEFINE_UART_CMD(verify, "verify",
		"verify [address(hex)] [dword size] [display]",
		"verify [address(hex)] [dword size] [display]",
		3, 3, verify_main);
#endif

#if (ENABLE_DFI_DPE_DEBUG == 1)
extern void _bm_proc_res_drop(void);
norm_ps_code int dpe_fill_and_compare_main(int argc, char *argv[])
{
	char *p;
	char *q;

	u32 reg = strtoul(argv[1], &p, 0);
	u32 bytes = (u32) atoi(argv[2]) * 4;
	u32 pattern = strtoul(argv[1], &q, 0);

	void *address = (void*) reg;
	address = (void*) ((u32) address & 0xFFFFFFE0);
	bytes = bytes & 0xFFFFFFE0;

	btn_int_mask0_t int_mask0_save = { .all = btn_readl(BTN_INT_MASK0), };
	btn_writel(int_mask0_save.all | BTN_DATA_PROCESS_DONE_MASK, BTN_INT_MASK0); // mask data_process_done

	bm_data_fill(address, bytes, pattern, NULL, NULL);

	_bm_proc_res_drop();
	data_proc_req_pointer_t req_q = { .all = btn_readl(DATA_PROC_REQ_POINTER), };
	while (req_q.b.data_proc_req_rptr != req_q.b.data_proc_req_wptr) {
		req_q.all = btn_readl(DATA_PROC_REQ_POINTER);
		_bm_proc_res_drop();
	}

	btn_writel(BTN_DATA_PROCESS_DONE_MASK, BTN_UM_INT_STS);
	btn_writel(int_mask0_save.all, BTN_INT_MASK0); // restore mask

	for (u32 i = 0; i < bytes; i += 4) {
		u32 j = readl(address);
		if (j != pattern)
			utils_dfi_trace(LOG_ERR, 0x3571, "Fail at %p: expected %x, read %x\n", address, pattern, j);
		address += 4;
	}
	utils_dfi_trace(LOG_ERR, 0xabdd, "\nDone\n");

	return 0;
}

static DEFINE_UART_CMD(dpe_fill_and_compare, "dpe_fill_and_compare",
		"dpe_fill_and_compare [address(hex)] [dword size] [pattern(hex)]",
		"dpe fill and compare memory",
		3, 3, dpe_fill_and_compare_main);

norm_ps_code int dpe_fill_main(int argc, char *argv[])
{
	char *p;
	u32 reg = strtoul(argv[1], &p, 0);
	u32 dwords = (u32) atoi(argv[2]);
	u32 pattern = 0xdc7d1ea4;

	void *address = (void*) reg;
	address = (void*) ((u32) address & 0xFFFFFFE0);
	dwords = dwords & 0xFFFFFFE0;

	btn_int_mask0_t int_mask0_save = { .all = btn_readl(BTN_INT_MASK0), };
	btn_writel(int_mask0_save.all | BTN_DATA_PROCESS_DONE_MASK,
			BTN_INT_MASK0); // mask data_process_done

	for (int i = 0; i < dwords; i++) {
		pattern = (pattern << 16) + pattern + ((pattern << 6) ^ i);
		bm_data_fill(address, 4, pattern, NULL, NULL);
		_bm_proc_res_drop();
		data_proc_req_pointer_t req_q = { .all = btn_readl(
				DATA_PROC_REQ_POINTER), };
		while (req_q.b.data_proc_req_rptr != req_q.b.data_proc_req_wptr) {
			req_q.all = btn_readl(DATA_PROC_REQ_POINTER);
			_bm_proc_res_drop();
		}

		btn_writel(BTN_DATA_PROCESS_DONE_MASK, BTN_UM_INT_STS);
		btn_writel(int_mask0_save.all, BTN_INT_MASK0); // restore mask
		address += 4;
	}

	for (u32 i=0; i < dwords; i+=4) {
		u32 j = readl(address);
		if ( j != pattern )
			utils_dfi_trace(LOG_ERR, 0xab95, "Fail at %p: expected %x, read %x\n",address,pattern,j);
		else
			utils_dfi_trace(LOG_ERR, 0x06a8, "read %p:%x\n",address,j);

		address += 4;
	}

	utils_dfi_trace(LOG_ERR, 0xff00, "\nDone\n");

	return 0;
}
static DEFINE_UART_CMD(dpe_fill, "dpe_fill",
		"dpe_fill [address(hex)] [dword size]",
		"dpe_fill [address(hex)] [dword size]",
		2, 2, dpe_fill_main);

// DPE copy with walking pattern
norm_ps_code int dpe_copy_main(int argc, char *argv[])
{
	char *p;
	char *q;
	void *src = (void*) strtoul(argv[1], &p, 0);
	void *dst = (void*) strtoul(argv[2], &q, 0);
	u32 bytes = (u32) atoi(argv[2]) * 4;

	src = (void*) ((u32) src & 0xFFFFFFE0);
	dst = (void*) ((u32) dst & 0xFFFFFFE0);
	bytes = bytes & 0xFFFFFFE0;

	// walking pattern
	for (u32 i = 0; i < bytes; i += 4)
		writel(0xdeadbeef, src + i);

	// use 0x200????? for SRAM and DDR_START~0xBFFFFFFF for DRAM
	dfi_dpe_copy(src, dst, bytes);

	for (u32 i = 0; i < bytes; i += 4) {
		u32 j = readl(dst + i);
		if (j != i)
			utils_dfi_trace(LOG_ERR, 0xf88c, "Fail at %p: expected %x, read %x\n", dst + i, i,
					j);
		else
			utils_dfi_trace(LOG_ERR, 0x8f2b, "read %p:%x\n", dst + i, j);
	}
	utils_dfi_trace(LOG_ERR, 0xc76b, "\nDone\n");

	return 0;
}
static DEFINE_UART_CMD(dpe_copy, "dpe_copy",
		"dpe_copy [src address(hex)] [dst address(hex)] [dword size]",
		"dpe_copy [src address(hex)] [dst address(hex)] [dword size]",
		3, 3, dpe_copy_main);

norm_ps_code int dpe_verify_write_main(int argc, char *argv[])
{
	char *d;
	char *t;
	char *r;
	d = &argv[1][0];
	t = &argv[2][0];
	r = &argv[3][0];
	u8 debug = (u8) atoi(d);
	u8 type = (u32) atoi(t);
	u32 bytes = (u32) atoi(r) * 4;

	bytes = bytes & 0xFFFFFFE0;
	dfi_dpe_verify_write(type, bytes, debug);

	utils_dfi_trace(LOG_ERR, 0x7418, "\nDone\n");
	return 0;
}
static DEFINE_UART_CMD(dpe_verify_write, "dpe_verify_write",
		"dpe_verify_write [debug] [dword size]",
		"dpe_verify_write [debug] [dword size]",
		3, 3, dpe_verify_write_main);

norm_ps_code int dpe_verify_read_main(int argc, char *argv[])
{
	char *d;
	char *t;
	char *r;
	d = &argv[1][0];
	t = &argv[2][0];
	r = &argv[3][0];
	u8 debug = (u8) atoi(d);
	u8 type = (u32) atoi(t);
	u32 bytes = (u32) atoi(r) * 4;

	bytes = bytes & 0xFFFFFFE0;
	dfi_dpe_verify_read(type, bytes, debug);

	utils_dfi_trace(LOG_ERR, 0x0b44, "\nDone\n");
	return 0;
}
static DEFINE_UART_CMD(dpe_verify_read, "dpe_verify_read",
		"dpe_verify_read [debug] [dword size]",
		"dpe_verify_read [debug] [dword size]",
		3, 3, dpe_verify_read_main);

// Repeatedly to DPE copy & verify
// Randomized patterns
// Randomized copy direction
norm_ps_code int dpe_verify_repeat_main(int argc, char *argv[])
{
	char *r;
	r = &argv[1][0];
	u32 repeat = (u32) atoi(r);

	dfi_dpe_verify_repeat(repeat);

	utils_dfi_trace(LOG_ERR, 0x8674, "\nDone\n");
	return 0;
}
static DEFINE_UART_CMD(dpe_verify_repeat, "dpe_verify_repeat",
		"dpe_verify_repeat [repeat]",
		"dpe_verify_repeat [repeat]",
		1, 1, dpe_verify_repeat_main);

// Repeatedly copy from SRAM to DDR
// For capturing output eye waveform
norm_ps_code int dpe_wr_repeat_main(int argc, char *argv[])
{
	char *t;
	char *r;
	t = &argv[1][0];
	r = &argv[2][0];
	u8 type = (u32) atoi(t);
	u32 repeat = (u32) atoi(r);

	dfi_dpe_wr_repeat(type, repeat);

	utils_dfi_trace(LOG_ERR, 0x6939, "\nDone\n");
	return 0;
}
static DEFINE_UART_CMD(dpe_wr_repeat, "dpe_wr_repeat",
		"dpe_wr_repeat [repeat]",
		"dpe_wr_repeat [repeat]",
		2, 2, dpe_wr_repeat_main)
;

// Repeatedly copy from DDR to SRAM
// For capturing output eye waveform
norm_ps_code int dpe_rd_repeat_main(int argc, char *argv[])
{
	char *t;
	char *r;
	t = &argv[1][0];
	r = &argv[2][0];
	u8 type = (u32) atoi(t);
	u32 repeat = (u32) atoi(r);

	dfi_dpe_rd_repeat(type, repeat);

	utils_dfi_trace(LOG_ERR, 0xbf26, "\nDone\n");
	return 0;
}
static DEFINE_UART_CMD(dpe_rd_repeat, "dpe_rd_repeat",
		"dpe_rd_repeat [repeat]",
		"dpe_rd_repeat [repeat]",
		2, 2, dpe_rd_repeat_main);
#endif

#if (ENABLE_DFI_INIT_DEBUG == 1)
norm_ps_code int dfi_phy_init_main(int argc, char *argv[])
{
	dfi_phy_init();
	utils_dfi_trace(LOG_ERR, 0x5563, "dfi_phy_init DONE\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_phy_init, "dfi_phy_init",
		"dfi_phy_init",
		"dfi_phy_init",
		0, 0, dfi_phy_init_main);

norm_ps_code int mc_init_main(int argc, char *argv[])
{
	mc_init();
	utils_dfi_trace(LOG_ERR, 0x8ef8, "mc_init DONE\n");
	return 0;
}
static DEFINE_UART_CMD(mc_init, "mc_init",
		"mc_init",
		"mc_init",
		0, 0, mc_init_main);

norm_ps_code int dfi_train_all_main(int argc, char *argv[])
{
	int target_speed = atoi(argv[1]);
	u8 target_cs = atoi(argv[2]);
	u8 debug = atoi(argv[3]);

	int res = dfi_train_all(target_speed, target_cs, debug);
	utils_dfi_trace(LOG_ERR, 0xa352, "\nDDR4 Training Result %d !\n", res);
	return res;
}
static DEFINE_UART_CMD(dfi_train_all, "dfi_train_all",
		"dfi_train_all [target_speed] [target_cs] [debug]",
		"dfi_train_all [target_speed] [target_cs] [debug]",
		3, 3, dfi_train_all_main);

norm_ps_code int dfi_train_all_lpddr4_main(int argc, char *argv[])
{
	int target_speed = atoi(argv[1]);
	u8 target_cs = atoi(argv[2]);
	u8 debug = atoi(argv[3]);

	int res = dfi_train_all_lpddr4(target_speed, target_cs, debug);
	utils_dfi_trace(LOG_ERR, 0x0432, "\nLPDDR4 Training Result %d !\n", res);
	return  res;
}

static DEFINE_UART_CMD(dfi_train_all_lpddr4, "dfi_train_all_lpddr4",
		"dfi_train_all_lpddr4 [target_speed] [target_cs] [debug]",
		"dfi_train_all_lpddr4 [target_speed] [target_cs] [debug]",
		3, 3, dfi_train_all_lpddr4_main);

// Pad calibration
norm_ps_code int dfi_pad_cal_seq_main(int argc, char *argv[])
{
	int debug = atoi(argv[1]);
	dfi_pad_cal_seq(debug);
	utils_dfi_trace(LOG_ERR, 0xd59a, "dfi_pad_cal_seq DONE\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_pad_cal_seq, "dfi_pad_cal_seq",
		"dfi_pad_cal_seq [debug]",
		"dfi_pad_cal_seq [debug]",
		1, 1, dfi_pad_cal_seq_main);


norm_ps_code int dfi_wrlvl_seq_hs_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	u8 debug = atoi(argv[2]);
	utils_dfi_trace(LOG_ERR, 0xf658, "target_cs = 0x%x\n", target_cs);
	u8 level[64];

	for (int k = 0; k < 64; k++)
		level[k] = 0;

	for (int i = 1; i < argc - 2 && i <= 64; i++) {
		level[i - 1] = atoi(argv[i + 2]);
		utils_dfi_trace(LOG_ERR, 0x19a0, "device_level[%d] = %d\n", i - 1, level[i - 1]);
	}
	dfi_wrlvl_seq_hs(target_cs, level, debug);
	utils_dfi_trace(LOG_ERR, 0x15b1, "dfi_wrlvl_seq_hs DONE\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_wrlvl_seq_hs, "dfi_wrlvl_seq_hs",
		"dfi_wrlvl_seq_hs [target_cs] [debug] [device_level0]  [device_level]  [device_leve2] ...",
		"dfi_wrlvl_seq_hs [target_cs] [debug] [device_level0]  [device_level]  [device_leve2] ...",
		3, 66, dfi_wrlvl_seq_hs_main);

norm_ps_code int dfi_wrlvl_seq_m2_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	u8 debug = atoi(argv[2]);
	utils_dfi_trace(LOG_ERR, 0xebca, "target_cs = 0x%x\n", target_cs);
	dfi_wrlvl_seq_m2(target_cs, debug);
	utils_dfi_trace(LOG_ERR, 0x2da8, "dfi_wrlvl_seq_m2 DONE\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_wrlvl_seq_m2, "dfi_wrlvl_seq_m2",
		"dfi_wrlvl_seq_m2 [target_cs] [debug]",
		"dfi_wrlvl_seq_m2 [target_cs] [debug]",
		2, 2, dfi_wrlvl_seq_m2_main);

norm_ps_code int dfi_rdlvl_rdlat_seq_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	int rcvlat_offset = atoi(argv[2]);
	int debug = atoi(argv[3]);
	utils_dfi_trace(LOG_ERR, 0xde9c, "dfi_rdlvl_rdlat_seq target_cs=0x%d, rcvlat_offset=%d\n", target_cs, rcvlat_offset);

	dfi_rdlvl_rdlat_seq(target_cs, rcvlat_offset, debug);

	return 0;
}
static DEFINE_UART_CMD(dfi_rdlvl_rdlat_seq, "dfi_rdlvl_rdlat_seq",
		"dfi_rdlvl_rdlat_seq [target_cs] [rcvlat_offset] [debug]",
		"dfi_rdlvl_rdlat_seq [target_cs] [rcvlat_offset] [debug]",
		3, 3, dfi_rdlvl_rdlat_seq_main);

norm_ps_code int dfi_rdlvl_gate_seq_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	int quick = atoi(argv[2]);
	int repeat = atoi(argv[3]);
	int debug = atoi(argv[4]);

	dfi_rdlvl_gate_seq(target_cs, quick, repeat, debug);

	return 0;
}
static DEFINE_UART_CMD(dfi_rdlvl_gate_seq, "dfi_rdlvl_gate_seq",
		"dfi_rdlvl_gate_seq [target_cs] [quick] [repeat] [debug]",
		"dfi_rdlvl_gate_seq [target_cs] [quick] [repeat] [debug]",
		4, 4, dfi_rdlvl_gate_seq_main);

norm_ps_code int dfi_rdlvl_2d_seq_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	int quick = atoi(argv[2]);
	int eye = atoi(argv[3]);
	int debug = atoi(argv[4]);
	utils_dfi_trace(LOG_ERR, 0x6d76, "dfi_rdlvl_2d_seq target_cs=%d, quick_sweep=%d, eye_size=%d\n", target_cs, quick, eye);

	dfi_rdlvl_2d_seq(target_cs, quick, eye, debug);

	return 0;
}
static DEFINE_UART_CMD(dfi_rdlvl_2d_seq, "dfi_rdlvl_2d_seq",
		"dfi_rdlvl_2d_seq [target_cs] [quick_sweep] [eye_size] [debug]",
		"dfi_rdlvl_2d_seq [target_cs] [quick_sweep] [eye_size] [debug])",
		4, 4, dfi_rdlvl_2d_seq_main);


norm_ps_code int dfi_vref_dq_seq_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	int debug = atoi(argv[2]);
	utils_dfi_trace(LOG_ERR, 0xc74f, "dfi_vref_dq_seq target_cs=%d\n", target_cs);
	dfi_vref_dq_seq(target_cs, debug);
	return 0;
}
static DEFINE_UART_CMD(dfi_vref_dq_seq, "dfi_vref_dq_seq",
		"dfi_vref_dq_seq [target_cs] [debug]",
		"dfi_vref_dq_seq [target_cs] [debug]",
		2, 2, dfi_vref_dq_seq_main);

// Write DQ-DQS delay (no VREF)
// Output DQ vs DQS training
norm_ps_code int dfi_wr_dqdqs_dly_seq_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	u8 train_type = atoi(argv[2]);
	u8 debug = atoi(argv[3]);
	utils_dfi_trace(LOG_ERR, 0x4e1e, "dfi_wr_dqdqs_dly_seq target_cs=%d, train_type=%d debug=%d\n", target_cs, train_type, debug);
	dfi_wr_dqdqs_dly_seq(target_cs, train_type, debug);

	return 0;
}
static DEFINE_UART_CMD(dfi_wr_dqdqs_dly_seq, "dfi_wr_dqdqs_dly_seq",
		"dfi_wr_dqdqs_dly_seq [target_cs] [0:FULL,1:FAST] [debug]",
		"dfi_wr_dqdqs_dly_seq [target_cs] [0:FULL,1:FAST] [debug]",
		3, 3, dfi_wr_dqdqs_dly_seq_main);

norm_ps_code int dfi_rdlvl_dpe_seq_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	u8 train_type = atoi(argv[2]);
	int debug = atoi(argv[3]);
	utils_dfi_trace(LOG_ERR, 0x368c, "dfi_rdlvl_dpe_seq target_cs=0x%d, train_type=%d, debug=%d\n", target_cs, train_type, debug);
	u8 win_size = 0; //unused
	int mod_delay = 0; //unused
	dfi_rdlvl_dpe_seq(target_cs, train_type, &win_size, &mod_delay, debug);
	return 0;
}

static DEFINE_UART_CMD(dfi_rdlvl_dpe_seq, "dfi_rdlvl_dpe_seq",
		"dfi_rdlvl_dpe_seq [target_cs] [0:FULL,1:Fast] [debug]",
		"dfi_rdlvl_dpe_seq [target_cs] [0:FULL,1:Fast] [debug]",
		3, 3, dfi_rdlvl_dpe_seq_main);

norm_ps_code int dfi_calvl_seq_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	int target_speed = atoi(argv[2]);
	int debug = atoi(argv[3]);
	utils_dfi_trace(LOG_ERR, 0x4b03, "dfi_calvl_seq target_cs=%d, target_speed=%d, debug=%d\n", target_cs, target_speed, debug);
	dfi_calvl_seq(target_cs, target_speed, debug);

	return 0;
}
static DEFINE_UART_CMD(dfi_calvl_seq, "dfi_calvl_seq",
		"dfi_calvl_seq [target_cs] [target_speed] [debug]",
		"dfi_calvl_seq [target_cs] [target_speed] [debug]",
		3, 3, dfi_calvl_seq_main);

norm_ps_code int dfi_wr_train_seq_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	int vref_range_max = atoi(argv[2]);
	int vref_value_max = atoi(argv[3]);
	int debug = atoi(argv[4]);
	utils_dfi_trace(LOG_ERR, 0xbcd1, "dfi_wr_train_seq target_cs=%d, vref_range_max=%d, vref_value_max=%d, debug=%d\n",
		target_cs, vref_range_max, vref_value_max, debug);
	dfi_wr_train_seq(target_cs, vref_range_max, vref_value_max, debug);

	return 0;
}
static DEFINE_UART_CMD(dfi_wr_train_seq, "dfi_wr_train_seq",
		"dfi_wr_train_seq [target_cs] [vref_range_max] [vref_value_max] [debug]",
		"dfi_wr_train_seq [target_cs] [vref_range_max] [vref_value_max] [debug]",
		4, 4, dfi_wr_train_seq_main);

norm_ps_code int dfi_set_pll_freq_main(int argc, char *argv[])
{
	int freq = atoi(argv[1]);
	utils_dfi_trace(LOG_ERR, 0x4c9f, "dfi_set_pll_freq freq=%d\n", freq);
	dfi_set_pll_freq(freq, true);

	return 0;
}
static DEFINE_UART_CMD(dfi_set_pll_freq, "dfi_set_pll_freq",
		"dfi_set_pll_freq [freq]",
		"dfi_set_pll_freq [freq]",
		1, 1, dfi_set_pll_freq_main);

norm_ps_code int dfi_chg_pll_freq_main(int argc, char *argv[])
{
	int freq = atoi(argv[1]);
	utils_dfi_trace(LOG_ERR, 0xb9a4, "dfi_chg_pll_freq freq=%d\n", freq);
	dfi_chg_pll_freq(freq);

	return 0;
}
static DEFINE_UART_CMD(dfi_chg_pll_freq, "dfi_chg_pll_freq",
		"dfi_chg_pll_freq [freq]",
		"dfi_chg_pll_freq [freq]",
		1, 1, dfi_chg_pll_freq_main);
#endif

#if (ENABLE_DFI_VALIDATION_DEBUG == 1)
// This function sweeps the output delay of CK, CA, and other address pins in order
// to find the setup/hold margin of the address bus.
// This is used for DDR34 as part of validation and not initialization training. Sweep ADCM_*DRV_SEL and run this code.
// LPDDR4 does this in dfi_calvl_seq() as part of initialization training.
norm_ps_code int dfi_adcm_margin_main(int argc, char *argv[])
{
	u16 target_speed = atoi(argv[1]);
	u8 type = atoi(argv[2]);
	u8 res = 0;
	u16 setup = 0;
	u16 hold = 0;
	u16 setup_array [19];
	u16 max = 100;

	// Push CK
	for (int i = 1; i < max; i++) {
		res = dfi_train_all(target_speed, 0, 0);
		if (res != 0) {
			utils_dfi_trace(LOG_ERR, 0x6255, "dfi_adcm_margin - Training failed but shouldnt...\n");
		}

		mc_self_refresh(true);
		dfi_push_ck(i);
		mc_self_refresh(false);

		res = dfi_dpe_verify_repeat(5);
		if (res != 0) {
			hold = i;
			break;
		}
	}
	if (hold == 0) {
		hold = max;
	}

	// Push CA
	if (type == 1) {
		for (int a = 0; a < 19; a++) {
			setup = 0;
			for (int i = 1; i < max; i++) {
				res = dfi_train_all(target_speed, 0, 0);
				if (res != 0) {
					utils_dfi_trace(LOG_ERR, 0x4036, "dfi_adcm_margin - Training failed but shouldnt...\n");
				}

				mc_self_refresh(true);
				dfi_push_ca(i, a);
				mc_self_refresh(false);

				res = dfi_dpe_verify_repeat(5);
				if (res != 0) {
					setup = i;
					break;
				}
			}
		if (setup == 0) {
			setup = max;
		}
		setup_array[a] = setup;
		}
	}

	// Push rank (or full)
	setup = 0;
	for (int i = 1; i < max; i++) {
		res = dfi_train_all(target_speed, 0, 0);
		if (res != 0) {
			utils_dfi_trace(LOG_ERR, 0xa68f, "dfi_adcm_margin - Training failed but shouldnt...\n");
			//break;
		}

		mc_self_refresh(true);
		if (type==0)
			dfi_push_ca(i,100); // push all AD pins along with rank
		dfi_push_rank(i);
		mc_self_refresh(false);

		res = dfi_dpe_verify_repeat(5);
		if (res != 0) {
			setup = i;
			break;
		}
	}
	if (setup == 0) {
		setup = max;
	}

	// Retrain to reset output delay
	res = dfi_train_all(target_speed, 0, 0);
	if (res != 0) {
		utils_dfi_trace(LOG_ERR, 0xb572, "dfi_adcm_margin - Training failed but shouldnt...\n");
		//break;
	}

	utils_dfi_trace(LOG_ERR, 0x3db5, "dfi_adcm_margin - Push CK, hold = %d.\n", hold);

	if (type==1) {
		for (int a = 0; a < 18; a++) {
			utils_dfi_trace(LOG_ERR, 0xb397, "dfi_adcm_margin - Push AD%d, setup = %d.", a, setup_array[a]);
			if (setup_array[a] == max)
				utils_dfi_trace(LOG_ERR, 0x0cc3, " (max sweep value)");
			utils_dfi_trace(LOG_ERR, 0x5686, "\n");
		}
		utils_dfi_trace(LOG_ERR, 0xb80f, "dfi_adcm_margin - Push CM (BA, CAS, etc), setup = %d.\n", setup_array[18]);
		utils_dfi_trace(LOG_ERR, 0x5006, "dfi_adcm_margin - Push rank (CS, CKE, ODT), setup = %d.\n", setup);
	}
	else
		utils_dfi_trace(LOG_ERR, 0x91d6, "dfi_adcm_margin - Push AD and rank, setup = %d.\n", setup);
	utils_dfi_trace(LOG_ERR, 0x082c, "dfi_adcm_margin done\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_adcm_margin, "dfi_adcm_margin",
		"dfi_adcm_margin [test_cnt] [0:fast,1:full]",
		"dfi_adcm_margin [test_cnt] [0:fast,1:full]",
		2, 2, dfi_adcm_margin_main);

norm_ps_code int dfi_pad_cal_seq_loop_main(int argc, char *argv[])
{
	u32 loop = atoi(argv[1]);
	utils_dfi_trace(LOG_ERR, 0x31e5, "loop = %d\n", loop);
	dfi_pad_cal_seq_loop(loop);
	utils_dfi_trace(LOG_ERR, 0x14fa, "dfi_pad_cal_seq_loop DONE\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_pad_cal_seq_loop, "dfi_pad_cal_seq_loop",
		"dfi_pad_cal_seq_loop [loop]",
		"dfi_pad_cal_seq_loop [loop]",
		1, 1, dfi_pad_cal_seq_loop_main);

norm_ps_code int dfi_wrlvl_loop_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	u32 loop = atoi(argv[2]);
	u8 summary = atoi(argv[3]);
	utils_dfi_trace(LOG_ERR, 0x6ac2, "target_cs = 0x%x, loop = %d, summary = %d\n", target_cs, loop, summary);
	dfi_wrlvl_loop(target_cs, loop, summary);
	utils_dfi_trace(LOG_ERR, 0x0d25, "dfi_wrlvl_loop DONE\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_wrlvl_loop, "dfi_wrlvl_loop",
		"dfi_wrlvl_loop [target_cs] [loop] [summary]",
		"dfi_wrlvl_loop [target_cs] [loop] [summary]",
		3, 3, dfi_wrlvl_loop_main);

norm_ps_code int dfi_rd_lat_man_loop_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	u32 loop = atoi(argv[2]);
	utils_dfi_trace(LOG_ERR, 0xa5eb, "dfi_rd_lat_man_loop target_cs=0x%d, loop=%d\n", target_cs, loop);

	dfi_rd_lat_man_loop(target_cs, loop);

	return 0;
}
static DEFINE_UART_CMD(dfi_rd_lat_man_loop, "dfi_rd_lat_man_loop",
		"dfi_rd_lat_man_loop [target_cs] [loop]",
		"dfi_rd_lat_man_loop [target_cs] [loop]",
		2, 2, dfi_rd_lat_man_loop_main);

norm_ps_code int dfi_wr_dqdqs_dly_loop_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	u32 loop = atoi(argv[2]);
	utils_dfi_trace(LOG_ERR, 0x8d7f, "dfi_wr_dqdqs_dly_loop(target_cs=%d, loop=%d\n", target_cs, loop);
	dfi_wr_dqdqs_dly_loop(target_cs, loop);
	return 0;
}
static DEFINE_UART_CMD(dfi_wr_dqdqs_dly_loop, "dfi_wr_dqdqs_dly_loop",
		"dfi_wr_dqdqs_dly_loop [target_cs] [loop]",
		"dfi_wr_dqdqs_dly_loop [target_cs] [loop]",
		2, 2, dfi_wr_dqdqs_dly_loop_main);

norm_ps_code int dfi_rdlvl_dpe_loop_main(int argc, char *argv[])
{
	int target_cs = atoi(argv[1]);
	u32 loop = atoi(argv[2]);
	utils_dfi_trace(LOG_ERR, 0x87f7, "dfi_rdlvl_dpe_loop target_cs=0x%d, loop=%d\n", target_cs, loop);
	dfi_rdlvl_dpe_loop(target_cs, loop);
	return 0;
}
static DEFINE_UART_CMD(dfi_rdlvl_dpe_loop, "dfi_rdlvl_dpe_loop",
		"dfi_rdlvl_dpe_loop [target_cs] [loop]",
		"dfi_rdlvl_dpe_loop [target_cs] [loop]",
		2, 2, dfi_rdlvl_dpe_loop_main);

norm_ps_code int dfi_sync_main(int argc, char *argv[])
{
	dfi_sync();
	utils_dfi_trace(LOG_ERR, 0xf7ca, "dfi_sync done.\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_sync, "dfi_sync",
		"dfi_sync",
		"dfi_sync",
		0, 0, dfi_sync_main);

norm_ps_code int dfi_wrlvl_seq_single_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	utils_dfi_trace(LOG_ERR, 0xe4c0, "target_cs = 0x%x\n", target_cs);
	u8 target_byte = atoi(argv[2]);

	dfi_wrlvl_seq_single(target_cs, target_byte);

	return 0;
}

static DEFINE_UART_CMD(dfi_wrlvl_seq_single, "dfi_wrlvl_seq_single",
		"dfi_wrlvl_seq_single [target_cs], [target_byte]",
		"dfi_wrlvl_seq_single [target_cs], [target_byte]",
		2, 2, dfi_wrlvl_seq_single_main);

norm_ps_code int dfi_wrlvl_seq_debug_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	utils_dfi_trace(LOG_ERR, 0x93a3, "target_cs = 0x%x\n", target_cs);
	bool en = atoi(argv[2]);

	dfi_wrlvl_seq_debug(target_cs, en);

	return 0;
}
static DEFINE_UART_CMD(dfi_wrlvl_seq_debug, "dfi_wrlvl_seq_debug",
		"dfi_wrlvl_seq_debug [target_cs], [enable]",
		"dfi_wrlvl_seq_debug [target_cs], [enable]",
		2, 2, dfi_wrlvl_seq_debug_main);

norm_ps_code int dfi_dev_vref_cfg_main(int argc, char *argv[])
{
	u8 target_cs = atoi(argv[1]);
	u8 range = atoi(argv[2]);
	u8 value = atoi(argv[3]);
	utils_dfi_trace(LOG_ERR, 0x289c, "dfi_dev_vref_cfg target_cs=%d, range=%d, value=0x%x\n", target_cs, range, value);
	dfi_dev_vref_cfg(target_cs, range, value);
	return 0;
}
static DEFINE_UART_CMD(dfi_dev_vref_cfg, "dfi_dev_vref_cfg",
		"dfi_dev_vref_cfg [target_cs] [range] [value]",
		"dfi_dev_vref_cfg [target_cs] [range] [value]",
		3, 3, dfi_dev_vref_cfg_main);

norm_ps_code int dfi_push_dq_main(int argc, char *argv[])
{
	int offset = atoi(argv[1]);

	dfi_push_dq(offset);
	utils_dfi_trace(LOG_ERR, 0x166a, "dfi_push_dq offset=%d\n", offset);
	return 0;
}
static DEFINE_UART_CMD(dfi_push_dq, "dfi_push_dq",
		"dfi_push_dq [offset]",
		"dfi_push_dq [offset]",
		1, 1, dfi_push_dq_main);

norm_ps_code int dfi_push_dqs_main(int argc, char *argv[])
{
	int offset = atoi(argv[1]);

	dfi_push_dqs(offset);
	utils_dfi_trace(LOG_ERR, 0x7d7a, "dfi_push_dqs offset=%d\n", offset);
	return 0;
}
static DEFINE_UART_CMD(dfi_push_dqs, "dfi_push_dqs",
		"dfi_push_dqs [offset]",
		"dfi_push_dqs [offset]",
		1, 1, dfi_push_dqs_main);

norm_ps_code int dfi_push_adcm_main(int argc, char *argv[])
{
	int offset = atoi(argv[1]);

	dfi_push_adcm(offset);
	utils_dfi_trace(LOG_ERR, 0xa229, "dfi_push_adcm offset=%d\n", offset);
	return 0;
}
static DEFINE_UART_CMD(dfi_push_adcm, "dfi_push_adcm",
		"dfi_push_adcm [offset]",
		"dfi_push_adcm [offset]",
		1, 1, dfi_push_adcm_main);

norm_ps_code int dfi_vgen_loop_main(int argc, char *argv[])
{
	u32 cnt = atoi(argv[1]);
	utils_dfi_trace(LOG_ERR, 0x6118, "dfi_vgen_loop cnt=%d\n", cnt);
	dfi_vgen_loop(cnt);

	return 0;
}
static DEFINE_UART_CMD(dfi_vgen_loop, "dfi_vgen_loop",
		"dfi_vgen_loop [cnt]",
		"dfi_vgen_loop [cnt]",
		1, 1, dfi_vgen_loop_main);

norm_ps_code int dfi_shadow_reg_dump_main(int argc, char *argv[])
{
	u8 i;
	void *src;
	u32 rdata0, rdata1, rdata2, rdata3;

	utils_dfi_trace(LOG_ERR, 0x737d, "DFI Shadow Regsiter Dump:\n");
	// Print DQ
//	utils_dfi_trace(LOG_ERR, 0, "{dq_rd_dly, dq_wr_dly}: ");
	utils_dfi_trace(LOG_ERR, 0xaa1d, "DQ{ODDLY}:\n");
	for (i = 0; i < 72; i++) {
		src = (void*) ((u32) 0xC0064200);
		writel(i, src);
//		src = (void*) ((u32) 0xC006421C);
//		rdata0 = readl(src);
		src = (void*) ((u32) 0xC0064238);
		rdata1 = readl(src);
		utils_dfi_trace(LOG_ERR, 0xa0ee, "{%x}, ", rdata1);
		if (i % 8 == 7)
			utils_dfi_trace(LOG_ERR, 0xec5e, "\n");
	}

	// Print Dbyte
	utils_dfi_trace(LOG_ERR, 0xe965, "DBYTE{DLL, STRGT, QS_ODDLY, DM_ODDLY}:\n");
	for (i = 0; i < 9; i++) {
		src = (void*) ((u32) 0xC0064200);
		writel(i << 8, src);
		src = (void*) ((u32) 0xC0064210);
		rdata0 = readl(src);
//		src = (void*) ((u32) 0xC0064218);
//		rdata1 = readl(src);
		src = (void*) ((u32) 0xC0064220);
		rdata1 = readl(src);
		src = (void*) ((u32) 0xC0064230);
		rdata2 = readl(src);
		src = (void*) ((u32) 0xC0064234);
		rdata3 = readl(src);
		utils_dfi_trace(LOG_ERR, 0xd718, "{%x, %x, %x, %x}\n ", rdata0, rdata1, rdata2, rdata3);
	}
	utils_dfi_trace(LOG_ERR, 0x0db6, "dfiSRD done.\n");
	return 0;
}
static DEFINE_UART_CMD(dfi_SRD, "dfi_SRD",
		"dfi_SRD",
		"dfi_shadow_reg_dump_main()",
		0, 0, dfi_shadow_reg_dump_main);

#define DDR_FILL_SIZE		0x10000000
#define DDR_FILL_STRD		4
#define DDR_FILL_HASH_IN	0
norm_ps_code int ddr_fill_main(int argc, char *argv[])
{
	int size = DDR_FILL_SIZE;
	int stride = DDR_FILL_STRD; // move how many dw at a time
	int repeat =  size / (4 * stride), i;
	u32* ptr = (u32*)DDR_BASE;

	for (i = 0; i < repeat; i++) {
		*ptr = dfi_simple_hash(i + DDR_FILL_HASH_IN);
		ptr += stride;
	}

	utils_dfi_trace(LOG_ERR, 0x6419, "ddr_fill done.\n");
	return 0;
}
static DEFINE_UART_CMD(ddr_fill, "ddr_fill",
		"ddr_fill",
		"ddr_fill",
		0, 0, ddr_fill_main);

norm_ps_code int ddr_chk_main(int argc, char *argv[])
{
	int size = DDR_FILL_SIZE;
	int stride = DDR_FILL_STRD; // move how many dw at a time
	int repeat =  size / (4 * stride), i;
	u32* ptr = (u32*) DDR_BASE;
	u32 err = 0;

	for (i = 0; i < repeat; i++) {
		if (*ptr != dfi_simple_hash(i + DDR_FILL_HASH_IN)) {
			utils_dfi_trace(LOG_ERR, 0xb61f, "!!!ERR!!! expected 0x%x, but 0x%x\n", dfi_simple_hash(i + DDR_FILL_HASH_IN), *ptr);
			err++;
		}
		ptr += stride;
	}

	utils_dfi_trace(LOG_ERR, 0x03ea, "err cnt %d\n ddr chk done.\n", err);
	return 0;
}
static DEFINE_UART_CMD(ddr_chk, "ddr_chk",
		"ddr_chk",
		"ddr_chk",
		0, 0, ddr_chk_main);

norm_ps_code int dfi_stress_main(int argc, char *argv[])
{
	int test_cnt = atoi(argv[1]);
	int pass = 0;
	u16 speed = mc_get_target_speed();
	u8 cs = 0;
	u8 debug = 0;
	u8 res = 0;


	for (int i = 0; i < test_cnt; i++) {
		res = dfi_train_all(speed, cs, debug);
		if (res != 0)
			break;

#if 0
	for (int i = 0; i < test_cnt; i++) {
		res = dfi_train_all_lpddr4(speed, cs, debug);
		if (res != 0)
			break;

		//DFI and PHY Init
		dfi_phy_init();
		utils_dfi_trace(LOG_ERR, 0x2585, "dfi_phy_init DONE\n");

		res = dfi_pad_cal_seq(debug);
		if (res != 0)
			return -1;
		else
			utils_dfi_trace(LOG_ERR, 0xa7b6, "dfi_cal_seq DONE\n");

		//Memory coltroller Init
		mc_init();
		utils_dfi_trace(LOG_ERR, 0x71dc, "mc_init DONE\n");

		//Write leveling training
		res = dfi_wrlvl_seq_m2(target_cs, debug);
		if (res != 0)
			return -2;
		else
			utils_dfi_trace(LOG_ERR, 0xd04e, "dfi_wr_lvl_seq_m2 DONE\n");

		//Init DRAM DQ VREF
		//dfi_dev_vref_cfg(target_cs, 1, 23);

		res = dfi_rdlvl_rdlat_seq(target_cs, 2, debug);
		if (res != 0)
			return -3;
		else
			utils_dfi_trace(LOG_ERR, 0x107f, "dfi_rdlvl_rdlat_seq DONE\n");


		res = dfi_rdlvl_gate_seq(target_cs, 0, 8, debug);
		if (res != 0)
			return -4;
		else
			utils_dfi_trace(LOG_ERR, 0xc231, "dfiRdLvlGateSeq DONE\n");

		res = dfi_rdlvl_2d_seq(target_cs, 0, 10, debug);
		if (res != 0)
			return -5;
		else
			utils_dfi_trace(LOG_ERR, 0x2bdd, "dfi_rdlvl_2d_seq DONE\n");

		res = dfi_vref_dq_seq(target_cs, debug);
		if (res != 0)
			return -6;
		else
			utils_dfi_trace(LOG_ERR, 0xd8f2, "dfiVrefDqSeq DONE\n");

		res = dfi_wr_dqdqs_dly_seq(target_cs, TRAIN_FULL, debug);
		utils_dfi_trace(LOG_ERR, 0xe3de, "dfiWrdqdqsdlySeq DONE\n");

		u8 win_size = 0; //unused
		int mod_delay = 0; //unused
		res = dfi_rdlvl_dpe_seq(target_cs, TRAIN_FULL, &win_size, &mod_delay, debug);
		if (res != 0)
			return -7;
		else
			utils_dfi_trace(LOG_ERR, 0x7db9, "dfiRdLvlDpeSeq DONE\n");
#endif
		res = dfi_dpe_verify_repeat(5000);
		if (res != 0)
			return -8;
		else
			utils_dfi_trace(LOG_ERR, 0x556a, "dpever_repeat DONE\n");

		pass += 1;
		utils_dfi_trace(LOG_ERR, 0x9165, "dfiStress pass %d of %d.\n", pass, test_cnt);

	}

	utils_dfi_trace(LOG_ERR, 0x213c, "dfiStress All Pass!\n");
	return 0;
}

static DEFINE_UART_CMD(dfi_stress, "dfi_stress",
		"dfi_stress [test_cnt]",
		"dfi_stress [test_cnt]",
		1, 1, dfi_stress_main);

enum {
	FIXED_PAT = 0,
	INTER_512SRAM_PAT = 1,
	ADDR_WALKING_PAT = 2,
	LFSR64_PAT = 3
};

enum {
	TEST0_ENGINE = 0,
	TEST1_ENGINE = 1,
	TEST01_ENGINE = 2
};

norm_ps_code bool ddr_test_pattern(u8 mode, u8 engine, u16 test0_pat, u16 test1_pat, u16 loop)
{
	extern volatile u32 _SYS_CLK;
	ddr_test0_control_t test0 = { .all = 0};
	ddr_test1_control_t test1= { .all = 0};
	fw_ddr_test_access_ctrl_t ctrl = { .all = 0};
	bool pass = true;
	u16 test_loop = loop;
	u32 i=0;
	u32 test_sz = 16 * 1024 * 1024;	// MB
	u32 cnt = 0;
	u32 sys_clk_shft20 = (SYS_CLK >> 20);
	u32 perf_mbs = 0;		// MBs
	u32 poll = 0;

	btn_writel(0, DDR_TEST0_START_4KB_ADR);
	btn_writel(test_sz/4096, DDR_TEST0_ENDING_4KB_ADR);

	btn_writel(test_sz/4096, DDR_TEST1_START_4KB_ADR);
	btn_writel((test_sz + test_sz)/4096, DDR_TEST1_ENDING_4KB_ADR);

	if ((engine == TEST0_ENGINE) || (engine == TEST01_ENGINE)) {
		test0.b.ddr_test0_run_start = 1;
		test0.b.ddr_test0_operation = 2;
		test0.b.ddr_test0_loop_num = test_loop;
		test0.b.ddr_test0_run_start = 1;
		poll |= DDR_TEST0_LOOP_DONE_MASK;
	}

	if ((engine == TEST1_ENGINE) || (engine == TEST01_ENGINE)) {
		test1.b.ddr_test1_run_start = 1;
		test1.b.ddr_test1_operation = 2;
		test1.b.ddr_test1_loop_num = test_loop;
		test1.b.ddr_test1_run_start = 1;
		poll |= DDR_TEST1_LOOP_DONE_MASK;
	}

	switch (mode) {
	case FIXED_PAT:
		btn_writel(((test1_pat<<16) | test0_pat), DDR_TEST_FIXED_PATTERN);
		test0.b.ddr_test0_pat_mixmd = 1;
		test1.b.ddr_test1_pat_mixmd = 1;
		test0.b.ddr_test0_pat_select = 0;
		test1.b.ddr_test1_pat_select = 0;
		break;
	case INTER_512SRAM_PAT:
		test0.b.ddr_test0_pat_select = 1;
		test1.b.ddr_test1_pat_select = 1;
		for (i = 0; i < 128; i++) {
			ctrl.b.fw_ddr_test_access_mode = 2; //0010: program/write  test engine 0 512Byte data-SRAM for the test pattern 1
			ctrl.b.fw_ddr_test_access_addr = i; // index walking pattern
			btn_writel(ctrl.all, FW_DDR_TEST_ACCESS_CTRL);
			btn_writel(0xbabeface, FW_DDR_TEST_ACCESS_DATA);
		}

		for (i = 0; i < 128; i++) {
			ctrl.b.fw_ddr_test_access_mode = 3; //0011: program/write  test engine 1 512Byte data-SRAM for the test pattern 1
			ctrl.b.fw_ddr_test_access_addr = i; // index walking pattern
			btn_writel(ctrl.all, FW_DDR_TEST_ACCESS_CTRL);
			btn_writel(i, FW_DDR_TEST_ACCESS_DATA);
		}
		break;
	case ADDR_WALKING_PAT:
		test0.b.ddr_test0_pat_select = 2;
		test1.b.ddr_test1_pat_select = 2;
		break;
	case LFSR64_PAT:
		test0.b.ddr_test0_pat_select = 3;
		test1.b.ddr_test1_pat_select = 3;
		break;
	}

	ddr_test_setting_status_t sts = { .all = btn_readl(DDR_TEST_SETTING_STATUS)};
	sts.b.ddr_test_loop_unit = 0;
	btn_writel(sts.all, DDR_TEST_SETTING_STATUS);

	test0.b.ddr_test0_wr_cnt_md = 0;	// 0: collect the smallest read-loop-time-counter of test engine 0
	test0.b.ddr_test0_rd_cnt_md = 0;	// 0: collect the smallest write-loop-time-counter of test engine 0
	test1.b.ddr_test1_wr_cnt_md = 0;	// 0: collect the smallest read-loop-time-counter of test engine 1
	test1.b.ddr_test1_rd_cnt_md = 0;	// 0: collect the smallest write-loop-time-counter of test engine 1
	btn_writel(test0.all, DDR_TEST0_CONTROL);
	btn_writel(test1.all, DDR_TEST1_CONTROL);

	sts.all = btn_readl(DDR_TEST_SETTING_STATUS);

	while (!(sts.all & poll)) {
		sts.all = btn_readl(DDR_TEST_SETTING_STATUS);
		ndelay(10000);
	}

	if ((sts.all & DDR_TEST1_ERR_COUNT_MASK) || (sts.all & DDR_TEST0_ERR_COUNT_MASK)) {
		utils_dfi_trace(LOG_ERR, 0x9ec6, "ddr_test mode: %d, pat0 0x%x, pat1 0x%04x failed status: 0x%x\n", mode, test0_pat, test1_pat, sts);
		utils_dfi_trace(LOG_ERR, 0xa3dc, "test0_first_rd_err_addr: 0x%x%x, test1_first_rd_err_addr: 0x%x%x\n", sts.b.ddr_test0_err_up2_adr, btn_readl(TEST0_FIRST_RD_ERR_ADDR), sts.b.ddr_test1_err_up2_adr, btn_readl(TEST1_FIRST_RD_ERR_ADDR));
		utils_dfi_trace(LOG_ERR, 0x3844, "ddr_test0_err_count: 0x%x, ddr_test1_err_count: 0x%x\n", sts.b.ddr_test0_err_count, sts.b.ddr_test1_err_count);
		pass = false;
	} else {
		/*
		FW to access the DDR test block control
		0000: no access
		0010: program/write  test engine 0 512Byte data-SRAM for the test pattern 1
		0011: program/write  test engine 1 512Byte data-SRAM for the test pattern 1
		0100: read total time(counter) of test-engine-0 to complete write operation with specifed loop
		0101: read total time(counter) of test-engine-0 to complete read operation with specifed loop
		0110: read total time(counter) of test-engine-1 to complete write operation with specifed loop
		0111: read total time(counter) of test-engine-1 to complete read operation with specifed loop
		others: reserved (internal debug)
		*/
		utils_dfi_trace(LOG_ERR, 0xc5ff, "test size %d sys_clk %d test_loop %d\n", test_sz, SYS_CLK, test_loop);
		if ((engine == TEST0_ENGINE) || (engine == TEST01_ENGINE)) {
			ctrl.b.fw_ddr_test_access_mode = 4;
			btn_writel(ctrl.all, FW_DDR_TEST_ACCESS_CTRL);
			cnt = btn_readl(FW_DDR_TEST_ACCESS_DATA) * 32;
			perf_mbs = (test_sz / cnt ) * sys_clk_shft20;
			utils_dfi_trace(LOG_ERR, 0xf21c, "test-engine-0 to complete write operation with specifed loop: %d, %d MB/s\n", cnt, perf_mbs);

			ctrl.b.fw_ddr_test_access_mode = 5;
			btn_writel(ctrl.all, FW_DDR_TEST_ACCESS_CTRL);
			cnt = btn_readl(FW_DDR_TEST_ACCESS_DATA) * 32;
			perf_mbs = (test_sz / cnt ) * sys_clk_shft20;
			utils_dfi_trace(LOG_ERR, 0xbdb9, "test-engine-0 to complete read operation with specifed loop: %d, %d MB/s\n", cnt, perf_mbs);
		}

		if ((engine == TEST1_ENGINE) || (engine == TEST01_ENGINE)) {
			ctrl.b.fw_ddr_test_access_mode = 6;
			btn_writel(ctrl.all, FW_DDR_TEST_ACCESS_CTRL);
			cnt = btn_readl(FW_DDR_TEST_ACCESS_DATA) * 32;
			perf_mbs = (test_sz / cnt ) * sys_clk_shft20;
			utils_dfi_trace(LOG_ERR, 0xb392, "test-engine-1 to complete write operation with specifed loop: %d, %d MB/s\n", cnt, perf_mbs);

			ctrl.b.fw_ddr_test_access_mode = 7;
			btn_writel(ctrl.all, FW_DDR_TEST_ACCESS_CTRL);
			cnt = btn_readl(FW_DDR_TEST_ACCESS_DATA) * 32;
			perf_mbs = (test_sz / cnt ) * sys_clk_shft20;
			utils_dfi_trace(LOG_ERR, 0x987c, "test-engine-1 to complete read operation with specifed loop: %d, %d MB/s\n", cnt, perf_mbs);
		}
	}

	btn_writel(DDR_TEST0_LOOP_DONE_MASK | DDR_TEST1_LOOP_DONE_MASK, DDR_TEST_SETTING_STATUS); //clear the status

	return pass;
}
#if 0
norm_ps_code int internal_ddr_test_block(int argc, char *argv[]) //test
{
	u16 t0_fixed_pat = 0x5A5A;
	u16 t1_fixed_pat = 0x7E7E;
	u8 engine = atoi(argv[1]);

	btn_ctrl_status_t btn_ctrl = { .all = btn_readl(BTN_CTRL_STATUS),};
	btn_ctrl.b.btn_ddr_test_mode = 1;
	btn_writel(btn_ctrl.all, BTN_CTRL_STATUS);	//enable ddr test

	utils_dfi_trace(LOG_ERR, 0xfc44, "\nDDR TEST BLOCK:\n");

	if (ddr_test_pattern(FIXED_PAT, engine, t0_fixed_pat, t1_fixed_pat, 10))
		utils_dfi_trace(LOG_ERR, 0xe83c, "\nFixed pattern test0:%x test1:%x pass\n", t0_fixed_pat, t1_fixed_pat);

	if (ddr_test_pattern(FIXED_PAT, engine, t0_fixed_pat, t1_fixed_pat, 20))
		utils_dfi_trace(LOG_ERR, 0xba54, "\nFixed pattern test0:%x test1:%x pass\n", t0_fixed_pat, t1_fixed_pat);

	if (ddr_test_pattern(INTER_512SRAM_PAT, engine, 0, 0, 10))
		utils_dfi_trace(LOG_ERR, 0xb291, "\nInternal sram 512B pattern pass\n");

	if (ddr_test_pattern(ADDR_WALKING_PAT, engine, 0, 0, 10))
		utils_dfi_trace(LOG_ERR, 0x90db, "\nAddress walking pattern pass\n");

	if (ddr_test_pattern(LFSR64_PAT, engine, 0, 0, 10))
		utils_dfi_trace(LOG_ERR, 0xdeb0, "\nLFSR-64 pass\n");

	btn_ctrl.b.btn_ddr_test_mode = 0;
	btn_writel(btn_ctrl.all, BTN_CTRL_STATUS);	//disable ddr test
	return 0;
}

static DEFINE_UART_CMD(ddr_test_block, "ddr_test_block",
	"ddr_test_block",
	"ddr_test_block",
	1, 1, internal_ddr_test_block);
#endif
slow_code void ddr_writel(u32 val, u64 phy_addr)
{
	u32 addr;
	if(phy_addr < 0x80000000){
		addr = (u32) phy_addr + 0x40000000;
		writel(val, (void *) addr);
	} else{
		u32 sel = 0;
		if (0x80000000 <= phy_addr && phy_addr < 0x100000000)		// 2G~4G
			sel = 1;
		else if (0x100000000 <= phy_addr && phy_addr < 0x180000000)	// 4G~6G
			sel = 2;
		else if (0x180000000 <= phy_addr && phy_addr < 0x200000000)	// 6G~8G
			sel = 3;
		else if (0x200000000 <= phy_addr && phy_addr < 0x280000000)	// 8G~10G
			sel = 4;
		else
			sys_assert(0);

		phy_addr = phy_addr - sel* 0x80000000;
		addr = (u32) phy_addr + 0x40000000;
		ddr_setup_window(sel);
		writel(val, (void *) addr);
		ddr_setup_window(0);
	}
}

slow_code u32 ddr_readl(u64 phy_addr)
{
	u32 addr;
	u32 val;
	if(phy_addr < 0x80000000){
		addr = (u32) phy_addr + 0x40000000;
		val = readl((const void *) addr);
	} else{
		u32 sel = 0;
		if (0x80000000 <= phy_addr && phy_addr < 0x100000000)		// 2G~4G
			sel = 1;
		else if (0x100000000 <= phy_addr && phy_addr < 0x180000000)	// 4G~6G
			sel = 2;
		else if (0x180000000 <= phy_addr && phy_addr < 0x200000000)	// 6G~8G
			sel = 3;
		else if (0x200000000 <= phy_addr && phy_addr < 0x280000000)	// 8G~10G
			sel = 4;
		else
			sys_assert(0);

		phy_addr = phy_addr - sel* 0x80000000;
		addr = (u32) phy_addr + 0x40000000;
		ddr_setup_window(sel);
		val = readl((const void *) addr);
		ddr_setup_window(0);
	}
	return val;
}

slow_code void ddr_size_auto_detect(ddr_cfg_t* ddr_cfg)
{
	u32 init_tag = 0x7e7e55aa;
	u32 chk_tag = 0x12345678;
	volatile u32 tag;
	u64 test_addr = 0x40;		// test address for col, raw, bank_gp, cs, default as DDR4 32bits and start from 4th col = 0x40
	u8 mc_area_len[] = {MC_AREA_LEN_256M, MC_AREA_LEN_512M, MC_AREA_LEN_1G, MC_AREA_LEN_2G, MC_AREA_LEN_4G, MC_AREA_LEN_8G};
	u8 mc_bus_width = MC_BUS_WIDTH_32; // default 32bit width
	u8 bus_width_bit_cnt = 5;	// 1 base, default 32bit width
	u8 col_bit_cnt = 4;		// 1 base, start from 4th col
	u8 row_bit_cnt = 1;		// 1 base, start from 1st row
	u8 cs_bit_cnt = 1;		// 0 base, start from 2nd cs
	u8 ddr_size_bit_cnt = 0;
#if defined(LPDDR4)
	u32 bank = MC_BANK_NUM_8;	// lpddr4 all 8 banks
	u8 bank_bit_cnt = 3;		// 1 base, lpddr4 all 8 banks, 2^3 = 8
	u8 mc_bank_gp = MC_BANK_GP_NUM_0;
#else
	u32 bank = MC_BANK_NUM_4;	// ddr4 all 4 banks
	u8 bank_bit_cnt = 2;		// 1 base, ddr4 all 4 baks, 2^2 = 4
	u8 bank_gp_bit_cnt = 2;		// 1 base, start from 2nd bank group bit
	u8 mc_bank_gp = MC_BANK_GP_NUM_4; // default max bank group (4 bank groups)
#endif
	u32 BIT_CNT_256MB = 31;

	extern void mc_init_single(u8 cs, u8 area_length, u8 data_width, u8 col, u8 row, u8 bank, u8 bank_group, bool brc);

	dfi_set_pll_freq(400, 0);
	dfi_phy_init();
	dfi_pad_cal_seq(0);

#if defined(LPDDR4)
	ddr_cfg->type = DDR_TYPE_LPDDR4;
	// 1. scan bank with default
	ddr_cfg->bank = bank_bit_cnt;
	// 2. scan bus width with BRC mode
	ddr_cfg->bus_width = bus_width_bit_cnt;
	test_addr = 0x20;	// 4th col start from 0x20
#else
	ddr_cfg->type = DDR_TYPE_DDR4;
	// 1. scan bank with default
	ddr_cfg->bank = bank_bit_cnt;
	// 2. scan bus width with BRC mode
	mc_init_single(0, MC_AREA_LEN_16G, MC_BUS_WIDTH_32, MC_COL_ADDR(MC_MAX_COL_ADDR), MC_ROW_ADDR(MC_MAX_ROW_ADDR), bank, mc_bank_gp, true);
	ddr_writel(init_tag, 0);
	__dmb();
	tag = ddr_readl(0);
	if (init_tag == tag) {
		mc_bus_width = MC_BUS_WIDTH_32;
		bus_width_bit_cnt = 5;
		test_addr = 0x40;	// 4th col start from 0x40
	} else if ((init_tag & 0x0000FFFF) == (tag & 0x0000FFFF)) {
		mc_bus_width = MC_BUS_WIDTH_16;
		bus_width_bit_cnt = 4;
		test_addr = 0x20;	// 4th col start from 0x20
	}
	ddr_cfg->bus_width = bus_width_bit_cnt;
#endif

	// 3. scan column with BRC mode
	mc_init_single(0, MC_AREA_LEN_16G, mc_bus_width, MC_COL_ADDR(MC_MAX_COL_ADDR), MC_ROW_ADDR(MC_MAX_ROW_ADDR), bank, mc_bank_gp, true);
	ddr_writel(init_tag, 0);
	for (col_bit_cnt = 4; col_bit_cnt <= MC_MAX_COL_ADDR; col_bit_cnt++) {
		ddr_writel(chk_tag, test_addr);
		__dmb();
		tag = ddr_readl(0);
		if (tag == chk_tag) {
			col_bit_cnt--;
			break;
		} else {
			test_addr = test_addr << 1;
		}
	}
	ddr_cfg->col = col_bit_cnt;
	sys_assert(col_bit_cnt <= MC_MAX_COL_ADDR);

	// 4. scan row with BRC mode
	mc_init_single(0, MC_AREA_LEN_16G, mc_bus_width, MC_COL_ADDR(col_bit_cnt), MC_ROW_ADDR(MC_MAX_ROW_ADDR), bank, mc_bank_gp, true);
	ddr_writel(init_tag, 0);
	for (row_bit_cnt = 1; row_bit_cnt <= MC_MAX_ROW_ADDR; row_bit_cnt++) {
		ddr_writel(chk_tag, test_addr);
		__dmb();
		tag = ddr_readl(0);
		if (tag == chk_tag) {
			row_bit_cnt--;
			break;
		} else {
			test_addr = test_addr << 1;
		}
	}
	ddr_cfg->row = row_bit_cnt;
	sys_assert(row_bit_cnt <= MC_MAX_ROW_ADDR);

#if !defined(LPDDR4)
	// 5. scan bank group with BRC mode (DDR4 only)
	dfi_phy_init();
	mc_init_single(0, MC_AREA_LEN_16G, mc_bus_width, MC_COL_ADDR(col_bit_cnt), MC_ROW_ADDR(row_bit_cnt), bank, mc_bank_gp, true);
	ddr_writel(init_tag, 0);
	ddr_writel(chk_tag, test_addr);
	__dmb();
	tag = ddr_readl(0);
	if (tag == chk_tag) {
		bank_gp_bit_cnt--;
	} else {
		test_addr = test_addr << 1;
	}
	ddr_cfg->bank_gp = bank_gp_bit_cnt;
	mc_bank_gp = bank_gp_bit_cnt;
#endif

#if defined(LPDDR4)
	ddr_size_bit_cnt = col_bit_cnt + row_bit_cnt + bank_bit_cnt + bus_width_bit_cnt + (cs_bit_cnt - 1);
#else
	ddr_size_bit_cnt = col_bit_cnt + row_bit_cnt + bank_bit_cnt + bank_gp_bit_cnt + bus_width_bit_cnt + (cs_bit_cnt - 1);
#endif

	// 6. scan cs with BRC mode
	test_addr = test_addr << bank_bit_cnt;
	mc_init_single(0, mc_area_len[ddr_size_bit_cnt-BIT_CNT_256MB], mc_bus_width, MC_COL_ADDR(col_bit_cnt), MC_ROW_ADDR(row_bit_cnt), bank, mc_bank_gp, true);
	mc_init_single(1, mc_area_len[ddr_size_bit_cnt-BIT_CNT_256MB], mc_bus_width, MC_COL_ADDR(col_bit_cnt), MC_ROW_ADDR(row_bit_cnt), bank, mc_bank_gp, true);

	ddr_writel(init_tag, 0);
	__dmb();
	tag = ddr_readl(0);
	//utils_dfi_trace(LOG_ERR, 0, "detect cs0 tag %x\n", tag);

	ddr_writel(chk_tag, test_addr);
	__dmb();
	tag = ddr_readl(test_addr);
	//utils_dfi_trace(LOG_ERR, 0, "detect cs1 tag %x\n", tag);
	if (tag == chk_tag){
		cs_bit_cnt++;
	}
	ddr_cfg->cs = cs_bit_cnt;

#if defined(LPDDR4)
	ddr_size_bit_cnt = col_bit_cnt + row_bit_cnt + bank_bit_cnt + bus_width_bit_cnt + (cs_bit_cnt - 1);
#else
	ddr_size_bit_cnt = col_bit_cnt + row_bit_cnt + bank_bit_cnt + bank_gp_bit_cnt + bus_width_bit_cnt + (cs_bit_cnt - 1);
#endif
	ddr_cfg->size = ddr_size_bit_cnt - BIT_CNT_256MB;
}

slow_code int ddr_size_auto_scan_main(int argc, char *argv[])
{
	char* ddr_bit_cnt_to_size[] = {"256MB", "512MB", "1GB", "2GB", "4GB", "8GB"};
	ddr_cfg_t ddr_cfg;
	ddr_size_auto_detect(&ddr_cfg);
	utils_dfi_trace(LOG_ERR, 0xc8c1, "\n");
	utils_dfi_trace(LOG_ERR, 0x4729, "detect ddr type: %d\n", ddr_cfg.type);
	#if defined(LPDDR4)
	utils_dfi_trace(LOG_ERR, 0x352b, "detect: bank %d bits, bus width: %d bits, col: %d bits, row: %d bits, cs cnt: %d\n",
		ddr_cfg.bank, 1<<ddr_cfg.bus_width,
		ddr_cfg.col, ddr_cfg.row,
		ddr_cfg.cs);
	#else
	utils_dfi_trace(LOG_ERR, 0x8d8e, "detect: bank %d bits, bus width: %d bits, col: %d bits, row: %d bits, bg: %d bits, cs cnt: %d\n",
		ddr_cfg.bank, 1<<ddr_cfg.bus_width,
		ddr_cfg.col, ddr_cfg.row,
		ddr_cfg.bank_gp, ddr_cfg.cs);
	#endif
	utils_dfi_trace(LOG_ERR, 0x6b6b, "ddr size auto detect: %s\n", ddr_bit_cnt_to_size[ddr_cfg.size]);
	return 0;
}

static DEFINE_UART_CMD(ddr_size_auto_scan, "ddr_size_auto_scan",
	"ddr_size_auto_scan",
	"ddr_size_auto_scan",
	0, 0, ddr_size_auto_scan_main);
#endif

ddr_code int Dump_DDR_Training_Result(int argc, char *argv[])
{
	utils_dfi_trace(LOG_ERR, 0x7d63, "========================================================");
    utils_dfi_trace(LOG_ERR, 0x0fb2, "===================PAD Calibration======================");
    utils_dfi_trace(LOG_ERR, 0x7a1f, "PSTR:%d",DRAM_Train_Result.element.pad.pstr);
    utils_dfi_trace(LOG_ERR, 0xa6e0, "NSTR:%d",DRAM_Train_Result.element.pad.nstr);
    utils_dfi_trace(LOG_ERR, 0xb1f9, "===================Write Leveling=======================");
    utils_dfi_trace(LOG_ERR, 0xb914, "level_delay[0]:%d",DRAM_Train_Result.element.wrlvl.level_delay[0]);
    utils_dfi_trace(LOG_ERR, 0x9f24, "level_delay[1]:%d",DRAM_Train_Result.element.wrlvl.level_delay[1]);
    utils_dfi_trace(LOG_ERR, 0x632d, "level_delay[2]:%d",DRAM_Train_Result.element.wrlvl.level_delay[2]);
    utils_dfi_trace(LOG_ERR, 0xac40, "level_delay[3]:%d",DRAM_Train_Result.element.wrlvl.level_delay[3]);
    utils_dfi_trace(LOG_ERR, 0x1efe, "level_delay[4]:%d",DRAM_Train_Result.element.wrlvl.level_delay[4]);
    utils_dfi_trace(LOG_ERR, 0xdbbc, "===================Read Latency=========================");
    utils_dfi_trace(LOG_ERR, 0xa405, "rdlat:%d",DRAM_Train_Result.element.rdlat.rdlat);
    utils_dfi_trace(LOG_ERR, 0x8e4a, "rcvlat:%d",DRAM_Train_Result.element.rdlat.rcvlat);
    utils_dfi_trace(LOG_ERR, 0x7eb3, "===================Read eye=============================");
    utils_dfi_trace(LOG_ERR, 0x3b8a, "vgen_range_final:%d",DRAM_Train_Result.element.rdeye.vgen_range_final);
    utils_dfi_trace(LOG_ERR, 0x12f2, "vgen_vsel_final:%d",DRAM_Train_Result.element.rdeye.vgen_vsel_final);
    utils_dfi_trace(LOG_ERR, 0x5564, "worst_range_all:%d",DRAM_Train_Result.element.rdeye.worst_range_all);
    for(int i=0;i<5;i++)
    {
        utils_dfi_trace(LOG_ERR, 0x7b03, "phase0_pre_final[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase0_pre_final[i]);
        utils_dfi_trace(LOG_ERR, 0x00eb, "phase1_pre_final[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase1_pre_final[i]);
        utils_dfi_trace(LOG_ERR, 0xcd79, "phase0_start[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase0_start[i]);
        utils_dfi_trace(LOG_ERR, 0xf9a8, "phase0_end[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase0_end[i]);
        utils_dfi_trace(LOG_ERR, 0x9b87, "phase0_range[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase0_range[i]);
        utils_dfi_trace(LOG_ERR, 0x65a5, "phase1_start[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase1_start[i]);
        utils_dfi_trace(LOG_ERR, 0xd669, "phase1_end[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase1_end[i]);
        utils_dfi_trace(LOG_ERR, 0x17f2, "phase1_range[%d]:%d",i,DRAM_Train_Result.element.rdeye.phase1_range[i]);
    }
    utils_dfi_trace(LOG_ERR, 0x3fca, "===================Write Vref===========================");
    utils_dfi_trace(LOG_ERR, 0x7819, "vref_range:%d",DRAM_Train_Result.element.wrvref.vref_range);
    utils_dfi_trace(LOG_ERR, 0xc9a2, "vref_value:%d",DRAM_Train_Result.element.wrvref.vref_value);
    utils_dfi_trace(LOG_ERR, 0xc9c0, "vref_norm:%d",DRAM_Train_Result.element.wrvref.vref_norm);
    utils_dfi_trace(LOG_ERR, 0xe216, "range1_pass_start:%d",DRAM_Train_Result.element.wrvref.range1_pass_start);
    utils_dfi_trace(LOG_ERR, 0x3a75, "range1_fail_start:%d",DRAM_Train_Result.element.wrvref.range1_fail_start);
    utils_dfi_trace(LOG_ERR, 0x800d, "range2_pass_start:%d",DRAM_Train_Result.element.wrvref.range2_pass_start);
    utils_dfi_trace(LOG_ERR, 0x2f01, "range2_fail_start:%d",DRAM_Train_Result.element.wrvref.range2_fail_start);
    utils_dfi_trace(LOG_ERR, 0x124c, "===================Write Deskew=========================");
    for(int i=0;i<5;i++)
    {
        utils_dfi_trace(LOG_ERR, 0xf073, "Byte[%d]:EyeSize:%d",i,DRAM_Train_Result.element.wrdeskew.ByteX_Eye_Size_Byte[i]);
        utils_dfi_trace(LOG_ERR, 0xb3fc, "Byte[%d]:Offset:%d",i,DRAM_Train_Result.element.wrdeskew.ByteX_offset[i]);
        utils_dfi_trace(LOG_ERR, 0xc6c2, "Byte[%d]:Right Start:%d, Right End:%d",i,DRAM_Train_Result.element.wrdeskew.Right_start[i],DRAM_Train_Result.element.wrdeskew.Right_end[i]);
        utils_dfi_trace(LOG_ERR, 0x5c19, "Byte[%d]:Left Start:%d, left End:%d",i,DRAM_Train_Result.element.wrdeskew.Left_start[i],DRAM_Train_Result.element.wrdeskew.Left_end[i]);
    }
    utils_dfi_trace(LOG_ERR, 0x4eab, "===================Read Gate===========================");
    for(int i=0;i<5;i++)
    {
        utils_dfi_trace(LOG_ERR, 0x05fc, "strgt_phase_dly:%d",DRAM_Train_Result.element.rdlvlgate.strgt_phase_dly[i]);
        utils_dfi_trace(LOG_ERR, 0x6c8a, "strgt_tap_dly:%d",DRAM_Train_Result.element.rdlvlgate.strgt_tap_dly[i]);        
    }
    utils_dfi_trace(LOG_ERR, 0x7466, "===================Read level dpe=======================");
    for(int i=0;i<5;i++)
    {
        utils_dfi_trace(LOG_ERR, 0xde2e, "window_size:%d",DRAM_Train_Result.element.rdlvldpe.window_size[i]);
        utils_dfi_trace(LOG_ERR, 0xd230, "is_push:%d",DRAM_Train_Result.element.rdlvldpe.is_push[i]);
        utils_dfi_trace(LOG_ERR, 0x11f0, "push_delay:%d",DRAM_Train_Result.element.rdlvldpe.push_delay[i]);
        utils_dfi_trace(LOG_ERR, 0x259b, "pull_delay:%d",DRAM_Train_Result.element.rdlvldpe.pull_delay[i]);
        utils_dfi_trace(LOG_ERR, 0x2962, "phase0_post_final[%d]:%d",i,DRAM_Train_Result.element.rdlvldpe.phase0_post_final[i]);
        utils_dfi_trace(LOG_ERR, 0x930c, "phase1_post_final[%d]:%d",i,DRAM_Train_Result.element.rdlvldpe.phase1_post_final[i]);
    }
    utils_dfi_trace(LOG_ERR, 0x8dda, "========================================================");
	return 0;
}

static DEFINE_UART_CMD(Dump_DDR_Training_Result, "dump_ddr_train_result",
		"dump ddr training result after training",
		"dump_ddr_train_result",
		0, 0, Dump_DDR_Training_Result);



norm_ps_code int dfi_main(int argc, char *argv[])
{
	#if (ENABLE_DFI_BASIC_DEBUG == 1)
	utils_dfi_trace(LOG_ERR, 0xcc22, "write                 [address(hex)] [value(hex)]\n");
	utils_dfi_trace(LOG_ERR, 0x3d77, "read                  [address(hex)]\n");
	utils_dfi_trace(LOG_ERR, 0xf1fc, "verify                [address(hex)] [dword size] [display]\n");
	utils_dfi_trace(LOG_ERR, 0x914a, "dump                  [address(hex)] [dword size]\n");
	#endif
	#if (ENABLE_DFI_DPE_DEBUG == 1)
	utils_dfi_trace(LOG_ERR, 0x2f73, "dpe_fill_and_compare  [address(hex)] [dword size] [pattern(hex)]\n");
	utils_dfi_trace(LOG_ERR, 0x4dfb, "dpe_fill              [address(hex)] [dword size]\n");
	utils_dfi_trace(LOG_ERR, 0x1176, "dpe_copy              [src address(hex)] [dst address(hex)] [dword size] (walking pattern)\n");
	utils_dfi_trace(LOG_ERR, 0x4c34, "dpe_verify_repeat     [repeat] \n");
	utils_dfi_trace(LOG_ERR, 0x7c3a, "dpe_verify_write      [debug] [type=rand,sso_dbi,sso_nodbi] [dword size]\n");
	utils_dfi_trace(LOG_ERR, 0x11e9, "dpe_verify_read       [debug] [type=rand,sso_dbi,sso_nodbi] [dword size]\n");
	utils_dfi_trace(LOG_ERR, 0xa3f5, "dpe_wr_repeat         [type=rand,sso_dbi,sso_nodbi] [repeat]\n");
	utils_dfi_trace(LOG_ERR, 0x1cee, "dpe_rd_repeat         [type=rand,sso_dbi,sso_nodbi] [repeat]\n");
	#endif
	#if (ENABLE_DFI_INIT_DEBUG == 1)
	utils_dfi_trace(LOG_ERR, 0x5176, "dfi_phy_init\n");
	utils_dfi_trace(LOG_ERR, 0xea5a, "mc_init\n");
	utils_dfi_trace(LOG_ERR, 0xe430, "dfi_train_all         [target_speed] [target_cs] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0x7898, "dfi_train_all_lpddr4  [target_speed] [target_cs] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0x0cef, "dfi_pad_cal_seq       [debug]\n");
	utils_dfi_trace(LOG_ERR, 0x6d2e, "dfi_wrlvl_seq_hs      [target_cs] [debug] [device_level0] [device_level1] [device_level2] ...\n");
	utils_dfi_trace(LOG_ERR, 0xd845, "dfi_wrlvl_seq_m2      [target_cs] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0xd838, "dfi_rdlvl_rdlat_seq   [target_cs] [rcvlat_offset] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0xf495, "dfi_rdlvl_gate_seq    [target_cs] [quick_sweep] [repeat] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0x7d16, "dfi_rdlvl_2d_seq      [target_cs] [quick_sweep] [eye_size] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0xa012, "dfi_vref_dq_seq       [target_cs] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0x1e67, "dfi_wr_dqdqs_dly_seq  [target_cs] [0:FULL,1:FAST] [debug] \n");
	utils_dfi_trace(LOG_ERR, 0x9303, "dfi_rdlvl_dpe_seq     [target_cs] [0:FULL,1:FAST] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0xa795, "dfi_calvl_seq         [target_cs] [target_speed] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0x184b, "dfi_wr_train_seq      [target_cs] [vref_range_max] [vref_value_max] [debug]\n");
	utils_dfi_trace(LOG_ERR, 0x1e66, "dfi_set_pll_freq      [freq]\n");
	utils_dfi_trace(LOG_ERR, 0x9d10, "dfi_chg_pll_freq      [freq] (includes self-refresh enter & exit)\n");
	#endif
	#if (ENABLE_DFI_VALIDATION_DEBUG == 1)
	utils_dfi_trace(LOG_ERR, 0x5c46, "dfi_pad_cal_seq_loop  [loop]\n");
	utils_dfi_trace(LOG_ERR, 0xa68b, "dfi_adcm_margin       [target speed] [0:FAST,1:FULL] (for non-LPDDR)\n");
	utils_dfi_trace(LOG_ERR, 0x07b8, "dfi_wrlvl_loop        [target_cs] [loop] [summary]\n");
	utils_dfi_trace(LOG_ERR, 0xc916, "dfi_rd_lat_man_loop   [target_cs] [loop]\n");
	utils_dfi_trace(LOG_ERR, 0x6071, "dfi_wr_dqdqs_dly_loop [target_cs] [loop]\n");
	utils_dfi_trace(LOG_ERR, 0x75d3, "dfi_rdlvl_dpe_loop    [target_cs] [loop]\n");
	utils_dfi_trace(LOG_ERR, 0x098b, "dfi_sync\n");
	utils_dfi_trace(LOG_ERR, 0xe989, "dfi_wrlvl_seq_single  [target_cs] [target_byte]\n");
	utils_dfi_trace(LOG_ERR, 0xe86e, "dfi_wrlvl_seq_debug   [target_cs] [enable]\n");
	utils_dfi_trace(LOG_ERR, 0x68c1, "dfi_dev_vref_cfg      [target_cs] [range] [value]\n");
	utils_dfi_trace(LOG_ERR, 0x6740, "dfi_push_dq           [offset]\n");
	utils_dfi_trace(LOG_ERR, 0x4964, "dfi_push_dqs          [offset]\n");
	utils_dfi_trace(LOG_ERR, 0x7e60, "dfi_push_adcm         [offset]\n");
	utils_dfi_trace(LOG_ERR, 0xb86f, "dfi_vgen_loop         [cnt]\n");
	utils_dfi_trace(LOG_ERR, 0xb2c1, "dfi_SRD               (shadow register dump)\n");
	utils_dfi_trace(LOG_ERR, 0x3018, "dfi_stress            [test_cnt]\n");
	utils_dfi_trace(LOG_ERR, 0x34af, "ddr_test_block        [0: engin0, 1:engin1, 2: engin0 and 1]\n");
	utils_dfi_trace(LOG_ERR, 0xa882, "ddr_size_auto_scan    \n");
	#endif

    utils_dfi_trace(LOG_ERR, 0xb979, "dump_ddr_train_result");

    return 0;
}
static DEFINE_UART_CMD(dfi, "dfi",
		"dfi",
		"show dfi functions",
		0, 0, dfi_main);
/*! @} */
