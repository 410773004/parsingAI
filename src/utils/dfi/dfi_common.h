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
/*! \file dfi_common.h
 * @brief dfi common APIs header
 *
 * \addtogroup utils
 * \defgroup dfi
 * \ingroup utils
 * @{
 */

#pragma once
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "mc_config.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
//#define DDR_PERF_CNTR
#define TRAIN_FULL 0
#define TRAIN_FAST 1
#define TRAIN_WIND 2

#if (RDISK)
#define ENABLE_DFI_BASIC_DEBUG		1
#define ENABLE_DFI_DPE_DEBUG		0
#define ENABLE_DFI_INIT_DEBUG		0
#define ENABLE_DFI_VALIDATION_DEBUG	0
#else
#define ENABLE_DFI_BASIC_DEBUG		1
#define ENABLE_DFI_DPE_DEBUG		1
#define ENABLE_DFI_INIT_DEBUG		1
#define ENABLE_DFI_VALIDATION_DEBUG	1
#endif
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
/*!
 * @brief delay function for nano second
 *
 * @param ns	delay nanosecond
 *
 * @return	not used
 */
void ndelay(u32 ns);

/*!
 * @brief dfi syncronization
 *
 * @return	not used
 */
void dfi_sync(void);

/*!
 * @brief dfi DLL reset
 *
 * @return	not used
 */
void dfi_dll_rst(void);

/*!
 * @brief Polling sequence after MRx are send to confirm MRx compeletion
 *
 * @param ms	wait ms to wait
 *
 * @return	true to pulling success, else false
 */
bool dfi_mc_smtq_cmd_done_poll_seq(u8 ms);

/*!
 * @brief DDR Mode Register Read Write Sequence
 *
 * @param wr	MR_WRITE for write and MR_READ for read
 * @param reg	MR number
 * @param cs	chip select
 *
 * @return		not used
 */
void dfi_mc_mr_rw_req_seq(bool wr, u8 reg, u8 cs);

/*!
 * @brief Sequence to put DDR3/4 in Multi Purpose Register (MPR) mode
 *
 * @param cs	chip select
 * @param en	true to enable, false to disable
 *
 * @return	not used
 */
void dfi_mc_ddrx_mpr_mode_seq(u8 cs, bool en);

/*!
 * @brief set ck gate data
 *
 * @param gate	gate
 *
 * @return	not used
 */
void dfi_ck_gate_data(u8 gate);

/*!
 * @brief set ck gate adcm
 *
 * @param gate	gate
 *
 * @return	not used
 */
void dfi_ck_gate_adcm(u8 gate);

/*!
 * @brief set ck gate ck
 *
 * @param gate	gate
 *
 * @return	not used
 */
void dfi_ck_gate_ck(u8 gate);

/*!
 * @brief set ck gate rank
 *
 * @param gate	gate
 *
 * @return	not used
 */
void dfi_ck_gate_rank(u8 gate);

/*!
 * @brief set memory controller self refresh mode
 *
 * @param en	true to enable, false to disable
 *
 * @return	not used
 */
void mc_self_refresh(bool en);

/*!
 * @brief dfi set PLL freq before DDR initialization
 *
 * @param freq	frequency
 * @param debug	true to show debug message
 *
 * @return	register read of PLL2_CTRL_0
 */
u32 dfi_set_pll_freq(int freq, bool debug);

/*!
 * @brief dfi change frequency while DDR is active
 *
 * @param freq	frequency
 *
 * @return	not used
 */
void dfi_chg_pll_freq(int freq);

/*!
 * @brief dfi push ADCM output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
void dfi_push_adcm(int ofst);

/*!
 * @brief dfi push CA output delay
 *
 * @param ofst	offset
 * @param index		target AD or CA pin
 *
 * @return	not used
 */
void dfi_push_ca(int ofst, u8 index);

/*!
 * @brief dfi push CK output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
void dfi_push_ck(int ofst);

/*!
 * @brief dfi push rank output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
void dfi_push_rank(int ofst);

/*!
 * @brief dfi push DQ output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
void dfi_push_dq(int offset);

/*!
 * @brief dfi push DQS output delay
 *
 * @param ofst	offset
 *
 * @return	not used
 */
void dfi_push_dqs(int offset);

/*!
 * @brief dfi generate simple hash
 *
 * @param value	value
 *
 * @return	hash result
 */
u32 dfi_simple_hash(u32 value);

/*!
 * @brief dfi generate pattern based on address
 *		Either SSO patterns (dbi on vs off) or random pattern hashed from address input
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
u32 dfi_gen_pattern(u8 data_width, u8 type, u32 in);

/*!
 * @brief dfi use dpe to verify training
 *
 * @param type		type
 * @param src		src pointer
 * @param dst		dst pointer
 * @param dst		dst pointer
 * @param bytes		bytes to be verified
 * @param debug		true to show debug message
 *
 *
 * @return result
 */
u8 dfi_dpe_verify_training(u8 type, void *src, void *dst, u32 bytes, bool debug);

/*!
 * @brief read level read compare sequence
 *
 * @param cs		chip select
 * @param ddr_type	mc ddr type
 *
 * @return	not used
 */
void dfi_rdlvl_rd_comp_seq(u8 cs, u8 ddr_type);

/*!
 * @brief Momory controller force ODT sequence
 *
 * @param cs	chip select
 * @param en	true if enable
 *
 * @return	not used
 */
void dfi_mc_force_odt_seq(u8 cs, bool en);

/*!
 * @brief DFI read level read issue sequence
 *
 * @param cs		chip select
 * @param ddr_type	mc ddr type
 *
 * @return	not used
 */
void dfi_mc_rdlvl_rd_issue_seq(u8 cs, u8 ddr_type);

#ifdef DDR_PERF_CNTR
/*!
 * @brief MC performance counters clock start
 *
 * @param pc_sel	performance counter select
 *
 * @return	not used
 */
void mc_pc_clk_start(u8 pc_sel);

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
void mc_pc_clk_get(u8 pc_sel, u32 *pc_val, u16 *pc_val_up, u8 *pc_over);

/*!
 * @brief MC performance counters clock stop
 *
 * @param pc_sel	performance counter select
 *
 * @return	not used
 */
void mc_pc_clk_stop(u8 pc_sel);
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
int dfi_train_all(int target_speed, u8 target_cs, u8 debug);

/*!
 * @brief dfi training for lpddr4
 *
 * @param target_speed	target training speed
 * @param target_cs	target training cs
 * @param debug		debug message level
 *
 * @return	result
 */
int dfi_train_all_lpddr4(int target_speed, u8 target_cs, u8 debug);

/*! @} */


