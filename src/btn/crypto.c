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

#include "types.h"
#include "btn_cmd_data_reg.h"
#include "io.h"
#include "rainier_soc.h"
#include "stdio.h"
#include "console.h"
#include "string.h"
#include "assert.h"
#include "crypto.h"

#if _TCG_
#include "../tcg/tcgcommon.h"
#include "../tcg/tcg.h"
#include "../tcg/tcg_sh_vars.h"

extern volatile u8 host_sec_bitz;
extern volatile u8 full_1ns;
extern u32     WrapKEK[8];        // KEK for current authority
extern sRawKey mRawKey[TCG_MAX_KEY_CNT];
extern bool    bKeyChanged;
extern enabledLockingTable_t pLockingRangeTable[LOCKING_RANGE_CNT + 1];

#ifdef NS_MANAGE
#include "nvme_spec.h"
extern struct ns_array_manage *ns_array_menu;
#endif

#endif

#if defined(USE_CRYPTO_HW)
//Andy_Crypto
#include "epm.h"
//#include "dtag.h"
#endif

/*! \cond PRIVATE */
#define __FILEID__ crypto
#include "trace.h"
/*! \endcond */

#if defined(USE_CRYPTO_HW)

#ifndef PASS_MSG
#define PASS_MSG "PASS"
#endif

#ifndef FAIL_MSG
#define FAIL_MSG "FAIL!!!"
#endif

// Enable/Disable Low Power Mode
#define CRYPTO_HW_ENABLE_LOW_PWR_MODE	0
#define CRYPTO_HW_DISABLE_LOW_PWR_MODE	1
// Enable/Disable Crypto Engine
#define CRYPTO_HW_DISABLE		0
#define CRYPTO_HW_ENABLE		1
// 128B/256B key size
#define CRYPTO_HW_KEY_SIZE_128B		0
#define CRYPTO_HW_KEY_SIZE_256B		2
// AES ECB/XTS algorithm
#define CRYPTO_HW_AES_ALGO_ECB		0
#define CRYPTO_HW_AES_ALGO_XTS		1
// Crypto Engines
#define CRYPTO_HW_ENGINE_0		0
#define CRYPTO_HW_ENGINE_1		1
#define CRYPTO_HW_ENGINE_2		2
#define CRYPTO_HW_ENGINE_3		3

// LBA map encryption/decryption enable
#define CRYPTO_LBA_ENC_EN 0x01
#define CRYPTO_LBA_DEC_EN 0x02

fast_data u32 crypto_cfg = CRYPTO_BYPASS_MODE;
#if defined (USE_SM4_ALGO)
fast_data u32 crypto_algo = CRYPTO_ALGO_SM4;
#else
fast_data u32 crypto_algo = CRYPTO_ALGO_AES;
#endif
fast_data u32 crypto_key = CRYPTO_KEY_00;

//-----------------------------------------------------------------------------
//  Inline Functions:
//-----------------------------------------------------------------------------
static inline void crypto_writel(vu32 val, u32 reg)
{
	writel(val, (void *) (BM_BASE + reg));
}

static inline vu32 crypto_readl(u32 reg)
{
	return readl((const void *) (BM_BASE + reg));
}

/*
 * @brief Select the key to program
 *
 * @param key_idx the key index(0 - 63)
 *
 * @return none
 */
static fast_code void crypto_key_sel(crypto_key_idx_t key_idx)
{
	sys_assert((key_idx >= CRYPTO_KEY_00) && (key_idx <= CRYPTO_KEY_63));
	crypto_key_sel_reg_t key_sel = {
		.all = crypto_readl(CRYPTO_KEY_SEL_REG),
	};
	key_sel.b.key_index_sel = key_idx;
	crypto_writel(key_sel.all, CRYPTO_KEY_SEL_REG);
	return;
}

/*!
 * @brief Select Crypto entry
 *
 * @param entry_idx the entry index(0 - 63)
 *
 * @return
 */
static fast_code void crypto_entry_sel(crypto_entry_idx_t entry_idx)
{
	crypto_lba_map_index_sel_t lba_map_idx = {
		.all = crypto_readl(CRYPTO_LBA_MAP_INDEX_SEL),
	};
	if (lba_map_idx.b.current_index != entry_idx) {
		lba_map_idx.b.current_index = entry_idx;
		crypto_writel(lba_map_idx.all, CRYPTO_LBA_MAP_INDEX_SEL);
	}
	return;
}

/*!
 * @brief Read Crypto entry
 *
 * @param entry_idx the entry index(0 - 63)
 *
 * @return crypto entry
 */
static fast_code crypto_entry_t crypto_read_entry(crypto_entry_idx_t entry_idx)
{
	crypto_entry_sel(entry_idx);
	crypto_entry_t entry = {
		.lm_nsid_misc.all = crypto_readl(CRYPTO_LBA_MAP_NSID_MISC),
		.lm_range_start.all = crypto_readl(CRYPTO_LBA_MAP_RANGE_START),
		.lm_range_end.all = crypto_readl(CRYPTO_LBA_MAP_RANGE_END),
	};
	return entry;
}

///<Just test code
slow_code bool crypto_entry_cmp(u32 entry_idx, crypto_entry_t old)
{
	bool flag = false;
	crypto_entry_t entry = crypto_read_entry(entry_idx);
	if ((entry.lm_nsid_misc.all != old.lm_nsid_misc.all)
		|| (entry.lm_range_start.all != old.lm_range_start.all)
		|| (entry.lm_range_end.all != old.lm_range_end.all))
		flag = true;
	//bf_mgr_trace(LOG_ERR, 0, "entry(0x%x) nsid misc: 0x%x range start: 0x%x range end: 0x%x value compare: %s\n",
	//	entry_idx, entry.lm_nsid_misc.all, entry.lm_range_start.all, entry.lm_range_end.all,
	//	flag ? FAIL_MSG : PASS_MSG);
	return flag;
}

slow_code void crypto_prog_entry(crypto_entry_idx_t entry_idx, crypto_entry_t entry) ///fast --> slow
{
	crypto_entry_sel(entry_idx);
	/* only when the whole entry programed done, you can set it valid */
	entry.lm_nsid_misc.b.valid = 0;
	crypto_writel(entry.lm_nsid_misc.all, CRYPTO_LBA_MAP_NSID_MISC);
	crypto_writel(entry.lm_range_start.all, CRYPTO_LBA_MAP_RANGE_START);
	crypto_writel(entry.lm_range_end.all, CRYPTO_LBA_MAP_RANGE_END);
	entry.lm_nsid_misc.b.valid = 1;
	crypto_writel(entry.lm_nsid_misc.all, CRYPTO_LBA_MAP_NSID_MISC);
	bf_mgr_trace(LOG_DEBUG, 0x48b2, "nsid misc: 0x%x range start: 0x%x range end: 0x%x\n",
		entry.lm_nsid_misc.all, entry.lm_range_start.all, entry.lm_range_end.all);
	sys_assert(crypto_entry_cmp(entry_idx, entry) == false);
	return;
}

/*!
 * @brief Crypto HW low power clock feature configuration
 *
 * @param	disable: true -> disable low power clock feature
 *
 * @return	None
 */
norm_ps_code void crypto_hw_cfg_low_power(u8 disable)
{
	crypto_sys_cfg_reg_t cfg_reg;
	cfg_reg.all = crypto_readl(CRYPTO_SYS_CFG_REG);
	cfg_reg.b.require_clk = disable;
	crypto_writel(cfg_reg.all, CRYPTO_SYS_CFG_REG);
	return;
}

/*!
 * @brief Configure 4 crypto HW engines to AES or SM4
 *
 * Egine 0 is always configured in CRYPTO BYPASS mode.
 * Engines 1, 2,3 are configured either in AES or SM4.
 * If AES, engine 1 is ECB-256B, engine 2 is XTS-128B and engine 3 is XTS-256B.
 *
 * @param crypto_algo_used - 0 (AES), 1 (SM4)
 *
 * @return		None
 */
static fast_code void crypto_hw_config(u32 crypto_algo_used)
{
	u32 i;
	u32 offset;
	u8 sm4_mode_en = (crypto_algo_used == CRYPTO_ALGO_SM4);
	if (sm4_mode_en) {
		bf_mgr_trace(LOG_INFO, 0x1e2f, "..... Algoritm:  SM4 .......");
	} else {
		bf_mgr_trace(LOG_INFO, 0x988c, "..... Algoritm:  AES .......");
	}

	crypto_hw_cfg_low_power(CRYPTO_HW_ENABLE_LOW_PWR_MODE);
	/**
	 * Configure all AES/SM4 crypto engines.
	 * Enable engine, set key size to 256 bits and set AES algoritm
	 * (ECB/XTS)
	 **/
	for (i = CRYPTO_HW_ENGINE_0, offset = CRYPTO_CFG0_REG; i < 4; i++, offset += 4) {
		crypto_cfg0_reg_t cfg;

		cfg.all = crypto_readl(offset);
		/* AES_CFG_SEL[2] in Desc_Format document will take
		 * configuration setting from here. The mapping of bits are as
		 * follows.
		 *   2'b00 -> crypto_cfg0_reg -> BYPASS mode
		 *   2'b01 -> crypto_cfg1_reg -> ECB-256
		 *   2'b10 -> crypto_cfg2_reg -> XTS-128
		 *   2'b11 -> crypto_cfg3_reg -> XTS-256
		 *
		 */
#if defined(USE_8K_DU)
		cfg.b.du_size_sel = BIT4;
#else
		cfg.b.du_size_sel = BIT3;
#endif //USE_8KDU
		if (i == CRYPTO_HW_ENGINE_0) {
			/* Engine 0: Disable encryption */
			cfg.b.cfg_en       = CRYPTO_HW_DISABLE;
			cfg.b.key_size_sel = CRYPTO_HW_KEY_SIZE_128B;
			cfg.b.alg_id_sel   = CRYPTO_HW_AES_ALGO_ECB;
			crypto_writel(cfg.all, offset);
		} else if (i == CRYPTO_HW_ENGINE_1) {
			/* Engine 1: Configure as AES ECB 256B or SM4 128B */
			cfg.b.cfg_en       = CRYPTO_HW_ENABLE;
			cfg.b.sm4_mode = sm4_mode_en;
			cfg.b.key_size_sel = sm4_mode_en ?
						 CRYPTO_HW_KEY_SIZE_128B : CRYPTO_HW_KEY_SIZE_256B;
			cfg.b.alg_id_sel   = CRYPTO_HW_AES_ALGO_ECB;
			crypto_writel(cfg.all, offset);
		} else if (i == CRYPTO_HW_ENGINE_2) {
			/* Engine 2: Configure as AES XTS 128B or SM4 128B */
			cfg.b.cfg_en       = CRYPTO_HW_ENABLE;
			cfg.b.sm4_mode = sm4_mode_en;
			cfg.b.key_size_sel = CRYPTO_HW_KEY_SIZE_128B;
			cfg.b.alg_id_sel   = CRYPTO_HW_AES_ALGO_XTS;
			crypto_writel(cfg.all, offset);
		} else {
			/* Engine 3: Configure as AES XTS 256B or SM4 128B */
			cfg.b.cfg_en       = CRYPTO_HW_ENABLE;
			cfg.b.sm4_mode = sm4_mode_en;
			cfg.b.key_size_sel = sm4_mode_en ?
						 CRYPTO_HW_KEY_SIZE_128B : CRYPTO_HW_KEY_SIZE_256B;
			cfg.b.alg_id_sel   = CRYPTO_HW_AES_ALGO_XTS;
			crypto_writel(cfg.all, offset);
		}
		bf_mgr_trace(LOG_INFO, 0xe07a, "Config reg=0x%x, val=0x%x", offset, crypto_readl(offset));
	}
	return;
}

/*!
 * @brief Program 64 encryption keys(This is just test code)
 *
 * AES-XTS algoritm requires key1 and key2. However, AES-ECB and SM4 requires only
 * key1. Also SM4 support ONLY 128 bit keys.
 *
 * @param	None
 *
 * @return	None
 */
static slow_code void crypto_hw_program_keys(void)//norm_ps_code->init_code
{
	u32 value;
	u32 i, j, k;
	u32 key1;
	u32 key2;
	u32 key_step = 0x100;

	crypto_hw_cfg_low_power(CRYPTO_HW_DISABLE_LOW_PWR_MODE);

	/* Program 64 key1 and 64 key2 */
	for (i = 0; i < CRYPTO_KEY_MAX; i++) {
		key1 = 0x12000000;
		key2 = 0x00340000;
		key1 += i * key_step;
		key2 += i * key_step;

		/* Set key index */
		crypto_key_sel((crypto_key_idx_t)i);
		/* Write 8 k1 entries */
		for (j = 0; j < 8; j++) {
			crypto_writel(key1, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
			key1++;
		}

		/* Write 8 k2 8 entries */
		for (k = 0; k < 8; k++) {
			crypto_writel(key2, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
			key2++;
		}

		/* Read status. key_addr_offset[8:0],
		 * [8:4] = index, [3:0] = 0
		 */
		value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
		value &= KEY_ADDR_OFFSET_MASK;
		//bf_mgr_trace(LOG_INFO, 0, "Reg=0x%x, val=0x%x\n", CRYPTO_KEY_ADDR_OFFSET_REG, value);

	}

	crypto_hw_cfg_low_power(CRYPTO_HW_ENABLE_LOW_PWR_MODE);
	/* Display the crypto_key_prog_done_reg (0x20c) */
	value = crypto_readl(CRYPTO_KEY_PROG_DONE_REG);
	bf_mgr_trace(LOG_INFO, 0xe736, "KEY_DONE: os=0x%x, val=0x%x\n", CRYPTO_KEY_PROG_DONE_REG, value);
}

norm_ps_code void crypto_hw_prgm_one_key(u8 *key1_array, u8 *key2_array,
	u8 key_index, enum crypto_key_type key_type)
{
	u32 value;
	u32 i;
	u32 key;
	u32 key_zero = 0x00000000;

	/* Disable low power state */
	crypto_hw_cfg_low_power(CRYPTO_HW_DISABLE_LOW_PWR_MODE);

	/* Set key index */
	crypto_key_sel((crypto_key_idx_t) key_index);
	
	//bf_mgr_trace(LOG_INFO, 0, "program one key() key_array : %x",*(u32 *)key1_array);
	
	/* Write key1 from index 0 to 3 (key1_array) */
	for (i = 0; i < 4; i++) {
		key = *(u32 *)(key1_array + i*4);
		crypto_writel(key, CRYPTO_KEY_ADDR_REG);
		value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
	}
	/* Write key1 from index 4 to 7 (key1_array) */
	if ((key_type == AES_XTS_256B_KEY) || (key_type == AES_ECB_256B_KEY)) {
		for (i = 4; i < 8; i++) {
			key = *(u32 *)(key1_array + i*4);
			crypto_writel(key, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
		}
	} else {
		for (i = 4; i < 8; i++) {
			crypto_writel(key_zero, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
		}
	}

	/* Write key2 from index 0 to 3 (key2_array) */
	if ((key_type == AES_XTS_256B_KEY) || (key_type == AES_XTS_128B_KEY)) {
		for (i = 0; i < 4; i++) {
			key = *(u32 *)(key2_array + i*4);
			crypto_writel(key, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
		}
	} else {
		for (i = 0; i < 4; i++) {
			crypto_writel(key_zero, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
		}
	}
	/* Write key2 from index 4 to 7 (key2_array) */
	if (key_type == AES_XTS_256B_KEY) {
		for (i = 4; i < 8; i++) {
			key = *(u32 *)(key2_array + i*4);
			crypto_writel(key, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
		}
	} else {
		for (i = 4; i < 8; i++) {
			crypto_writel(key_zero, CRYPTO_KEY_ADDR_REG);
			value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
		}
	}

	/* Read status. key_addr_offset[8:0],
	 * [8:4] = index, [3:0] = 0
	 */
	value = crypto_readl(CRYPTO_KEY_ADDR_OFFSET_REG);
	value &= KEY_ADDR_OFFSET_MASK;

	/* Enable low power state */
	crypto_hw_cfg_low_power(CRYPTO_HW_ENABLE_LOW_PWR_MODE);

	/* Display the crypto_key_prog_done_reg (0x20c) */
	value = crypto_readl(CRYPTO_KEY_PROG_DONE_REG);
}

/*!
 * @brief enable LBA map encryption/decryption
 *
 * @param enable encryption or decryption
 *
 * @param none
 */
static norm_ps_code void crypto_lba_map_enable(u8 enable)
{
	enable &= (MAP_DEC_EN_MASK | MAP_ENC_EN_MASK);
	crypto_writel(enable, CRYPTO_LBA_MAP_CTRL_REG);
	bf_mgr_trace(LOG_INFO, 0xea98, "LBA map ctrl reg: 0x%x offset: 0x%x\n",
		crypto_readl(CRYPTO_LBA_MAP_CTRL_REG), CRYPTO_LBA_MAP_CTRL_REG);
	return;
}

slow_code void crypto_mek_refresh_trigger(void)
{
	crypto_lba_map_ctrl_reg_t lba_map_ctrl = {
		.all = crypto_readl(CRYPTO_LBA_MAP_CTRL_REG),
	};
	lba_map_ctrl.b.key_refresh_request = 1;
	crypto_writel(lba_map_ctrl.all, CRYPTO_LBA_MAP_CTRL_REG);
	
#ifdef While_break
	u64 start = get_tsc_64();
#endif	
	
	do {
		lba_map_ctrl.all = crypto_readl(CRYPTO_LBA_MAP_CTRL_REG);
		
#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif		
	} while(lba_map_ctrl.b.key_refresh_request);
	bf_mgr_trace(LOG_DEBUG, 0x7a5d, "MEK refresh done");
}

init_code void crypto_entry_chg_range_start(u32 entry_idx, u32 range_start)//fast code->init_code
{
	crypto_entry_t entry = crypto_read_entry(entry_idx);
	bf_mgr_trace(LOG_INFO, 0x0d59, "entry index: 0x%x range end: 0x%x --> 0x%x",
		entry.lm_range_start.all, range_start);
	entry.lm_range_start.all = range_start;
	crypto_prog_entry(entry_idx, entry);
	return;
}

init_code void crypto_entry_chg_range_end(u32 entry_idx, u32 range_end)//fast code -> init code
{
	crypto_entry_t entry = crypto_read_entry(entry_idx);
	bf_mgr_trace(LOG_INFO, 0x9f84, "entry index: 0x%x range end: 0x%x --> 0x%x",
		entry.lm_range_end.all, range_end);
	entry.lm_range_end.all = range_end;
	crypto_prog_entry(entry_idx, entry);
	return;
}

init_code void crypto_entry_chg_cfg_sel(u32 entry_idx, u32 cfg_sel)//fast code -> init code
{
	crypto_entry_t entry = crypto_read_entry(entry_idx);
	bf_mgr_trace(LOG_INFO, 0xb90b, "entry: 0x%x cfg sel: 0x%x -> 0x%x",
		entry_idx, entry.lm_nsid_misc.b.cfg_sel, cfg_sel);
	entry.lm_nsid_misc.b.cfg_sel = cfg_sel;
	crypto_prog_entry(entry_idx, entry);
	return;
}

ddr_code bool crypto_hw_selftest(u32 du_cnt)
{
	bool res = false;
	crypto_selftest_cfg_reg_t st_reg = { .all = 0 };
	crypto_selftest_cnt_reg_t st_cnt_reg = { .all = 0 };
	crypto_selftest_status_reg_t st_status_reg = { .all = 0 };

	crypto_hw_cfg_low_power(CRYPTO_HW_DISABLE_LOW_PWR_MODE);

	/* Set data unit numbers, maximum 0xFFFFh */
	st_cnt_reg.b.st_cnt_val = du_cnt;
	bf_mgr_trace(LOG_INFO, 0xfcb5, "data unit number: 0x%x", du_cnt);
	crypto_writel(st_cnt_reg.all, CRYPTO_SELFTEST_CNT_REG);

	/* clear previous self test result */
	crypto_writel(ST_CLEAR_MASK, CRYPTO_SELFTEST_CFG_REG);
	while(crypto_readl(CRYPTO_SELFTEST_STATUS_REG));
	/* Enable Crypto Self test mode */

	st_reg.b.st_mode_en = 1;
	crypto_writel(st_reg.all, CRYPTO_SELFTEST_CFG_REG);
	/* Trigger self test procedure */
	st_reg.b.st_start_trigger = 1;
	crypto_writel(st_reg.all, CRYPTO_SELFTEST_CFG_REG);

#ifdef While_break
	u64 start = get_tsc_64();
#endif	
	
	do {
		st_reg.all = crypto_readl(CRYPTO_SELFTEST_CFG_REG);
		///< debug code
		//st_status_reg.all = crypto_readl(CRYPTO_SELFTEST_STATUS_REG);
		//bf_mgr_trace(LOG_INFO, 0, "DU count remaining: %x\n",
		//	st_status_reg.b.du_cnt_remaining);
#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif		
	} while (st_reg.b.st_start_trigger != 0);

	res = (st_status_reg.b.fail_flag || st_status_reg.b.din_size_err || st_status_reg.b.dout_size_err);
	bf_mgr_trace(LOG_INFO, 0x55a9, "CRYPTO selftest: reg value(0x%x) offset: 0x%x  result: 0x%x\n",
		st_status_reg.all, CRYPTO_SELFTEST_STATUS_REG, res);
	/* Clear register */
	st_reg.all = 0;
	crypto_writel(st_reg.all, CRYPTO_SELFTEST_CFG_REG);

	crypto_hw_cfg_low_power(CRYPTO_HW_ENABLE_LOW_PWR_MODE);
	return res;
}

ddr_code void crypto_mek_sram_init(void)
{
	crypto_key_sel_reg_t key_sel;
	key_sel.all = crypto_readl(CRYPTO_KEY_SEL_REG);
	key_sel.b.mek_sram_init = 1;
	crypto_writel(key_sel.all, CRYPTO_KEY_SEL_REG);
	
#ifdef While_break
	u64 start = get_tsc_64();
#endif	
	
	do {
		key_sel.all = crypto_readl(CRYPTO_KEY_SEL_REG);

#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif		
		
	} while (key_sel.b.mek_sram_init);
	bf_mgr_trace(LOG_INFO, 0xcda1, "CRYPTO MEK SRAM init done");
	return;
}

///<just test code
void aes_crypto_hw_regs_check(void)
{
	u32 value;
	u32 offset;

	bf_mgr_trace(LOG_ERR, 0x3651, "CRYPTO: register validation ....");

	/**
	 * cryoto_capability_reg @offset 0x000
	 **/
	offset = 0x400;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xb2e9, "CRYPTO capability offst: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x40030402 ? PASS_MSG:FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xf42b, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == value ? PASS_MSG:FAIL_MSG);
	/**
	 * cryoto_capability0_reg @offset 0x004
	 **/
	offset = 0x404;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xd1fe, "CRYPTO capability0 offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00057F00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0x8e68, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x00FFFFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_capability1_reg @offset 0x008
	 **/
	offset = 0x408;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x81a1, "CRYPTO capability1 offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00057F01 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xa382, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x00FFFFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_sys_cfg_reg @offset 0x080
	 **/
	offset = 0x480;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x1667, "CRYPTO sys cfg offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x230000 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0x5288, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x80770000 ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_cfg0_reg @offset 0x100
	 **/
	offset = 0x500;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x2ed1, "CRYPTO sys cfg offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xf34e, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0xC000FFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_cfg1_reg @offset 0x104
	 **/
	offset = 0x504;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xcaf8, "CRYPTO sys cfg1 offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0x84e1, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0xC000FFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_cfg2_reg @offset 0x108
	 **/
	offset = 0x508;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x5861, "CRYPTO sys cfg2 offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0x81be, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0xC000FFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_cfg3_reg @offset 0x10c
	 **/
	offset = 0x50c;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x4230, "CRYPTO sys cfg3 offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0x4819, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0xC000FFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_selftest_cfg_reg @offset 0x180
	 **/
	offset = 0x580;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x054d, "CRYPTO selftest cfg offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);

	/**
	 * cryoto_selftest_cnt_reg @offset 0x184
	 **/
	offset = 0x584;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x5ea3, "CRYPTO selftest cnt offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0xFFFF ? PASS_MSG : FAIL_MSG);


	/**
	 * cryoto_selftest_status_reg @offset 0x188
	 **/
	offset = 0x588;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x6391, "CRYPTO selftest status offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xb719, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x0 ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_key_addr_reg @offset 0x200
	 **/
	offset = 0x600;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xc4fe, "CRYPTO key addr offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xc3e9, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0 ? PASS_MSG:FAIL_MSG);

	/**
	 * cryoto_key_sel_reg @offset 0x204
	 **/
	offset = 0x604;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x0d6a, "CRYPTO key sel offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);

	/**
	 * cryoto_key_addr_offset_reg @offset 0x208
	 **/
	offset = 0x608;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xf695, "CRYPTO key addr offset offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);

	/**
	 * cryoto_key_prog_done_reg @offset 0x20c
	 **/
	offset = 0x60c;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x3af1, "CRYPTO key program done offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);

	/**
	 * cryoto_key_ecc_info_reg @offset 0x210
	 **/
	offset = 0x610;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x736b, "CRYPTO key ecc info offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG : FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xd6e9, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x0 ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_map_index_sel @offset 0x230
	 **/
	offset = 0x630;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xc775, "CRYPTO lba map index_sel: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0x505c, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x000007FF ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_map_nsid_misc @offset 0x234
	 **/
	offset = 0x634;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x9eb8, "CRYPTO lba map nsid misc offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_map_range_start @offset 0x238
	 **/
	offset = 0x638;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xb0ec, "CRYPTO lba map range start offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0x0696, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x03FFFFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_map_range_end @offset 0x23c
	 **/
	offset = 0x63c;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xd894, "CRYPTO lba map range end offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xdd3b, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x03FFFFFF ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_map_ctrl_reg @offset 0x244
	 **/
	offset = 0x644;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xdd04, "CRYPTO lba map ctrl reg offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xb3c9, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0x00000003 ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_map_re_enc_reg @offset 0x248
	 **/
	offset = 0x648;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0xbac4, "CRYPTO lba map re-encryption reg offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_blk_cipher_event_reg @offset 0x250
	 **/
	offset = 0x650;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x8065, "CRYPTO lba block cipher envent reg offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);

	/**
	 * crypto_lba_blk_cipher_status_reg @offset 0x254
	 **/
	offset = 0x654;
	value = crypto_readl(offset);
	bf_mgr_trace(LOG_ERR, 0x25ae, "CRYPTO lba block cipher status reg offset: 0x%x  value: 0x%x%t %s\n", offset, value,
		value == 0x00 ? PASS_MSG:FAIL_MSG);
	crypto_writel(0xFFFFFFFF, offset);
	bf_mgr_trace(LOG_ERR, 0xdf5a, "W/R test: value(0x%x)  %s\n",
		crypto_readl(offset), crypto_readl(offset) == 0xFFFFFFFF ? PASS_MSG:FAIL_MSG);

	return;
}
//Andy_Crypto
///Generate random key
extern void sec_gen_rand_number(u8 *buf, u8 rand_len);

///<just test code
static slow_code void crypto_hw_program_entrys(void)
{
	//u32 i;
	crypto_entry_t entry;
	//u32 cnt = 0;
	//Start from 1 range 0 don't use //Andy test
	#if 0
	for (i = 1; i < CRYPTO_LBA_MAP_ENTRY_63; i += 4) {
		{
			memset(&entry, 0, sizeof(entry));
			entry.lm_nsid_misc.b.cfg_sel = CRYPTO_HW_ENGINE_0;
			entry.lm_range_start.b.range_start = cnt;
			entry.lm_range_end.b.range_end = cnt;
			entry.lm_nsid_misc.b.namespace_id = 1;
			crypto_prog_entry(i + 0, entry);
		}
		{
			++cnt;
			memset(&entry, 0, sizeof(entry));
			entry.lm_nsid_misc.b.cfg_sel = CRYPTO_HW_ENGINE_0;
			entry.lm_range_start.b.range_start = cnt;
			entry.lm_range_end.b.range_end = cnt;
			entry.lm_nsid_misc.b.namespace_id = 1;
			crypto_prog_entry(i + 1, entry);
		}
		{
			++cnt;
			memset(&entry, 0, sizeof(entry));
			entry.lm_nsid_misc.b.cfg_sel = CRYPTO_HW_ENGINE_0;
			entry.lm_range_start.b.range_start = cnt;
			entry.lm_range_end.b.range_end = cnt;
			entry.lm_nsid_misc.b.namespace_id = 1;
			crypto_prog_entry(i + 2, entry);
		}
		{
			++cnt;
			memset(&entry, 0, sizeof(entry));
			entry.lm_nsid_misc.b.cfg_sel = CRYPTO_HW_ENGINE_0;
			entry.lm_range_start.b.range_start = cnt;
			entry.lm_range_end.b.range_end = cnt;
			entry.lm_nsid_misc.b.namespace_id = 1;
			crypto_prog_entry(i + 3, entry);
		}
	}
#endif
	{
		memset(&entry, 0, sizeof(entry));
		entry.lm_nsid_misc.b.cfg_sel = CRYPTO_HW_ENGINE_1;
		entry.lm_nsid_misc.b.namespace_id = 1;
		entry.lm_nsid_misc.b.isglobal_namespace = 1;
		//crypto_prog_entry(CRYPTO_LBA_MAP_ENTRY_62, entry);
	}

	{
		//Andy_Crypto
		//memset(&entry, 0, sizeof(entry));
		//entry.lm_nsid_misc.b.cfg_sel = CRYPTO_HW_ENGINE_0;
		//entry.lm_nsid_misc.b.isglobal_globally = 1;
		crypto_prog_entry(CRYPTO_LBA_MAP_ENTRY_01, entry);
	}
        //Andy_Crypto
		////Test code
        #if 0
		u32 buffer[8];
		sec_gen_rand_number((u8 *)buffer, 32);	
		
		bf_mgr_trace(LOG_ALW, 0xd7df, "b0: %x", *buffer);
		bf_mgr_trace(LOG_ALW, 0x0e48, "b1: %x", *(buffer+1));
		bf_mgr_trace(LOG_ALW, 0x5bce, "b2: %x", *(buffer+2));
		bf_mgr_trace(LOG_ALW, 0xe4c8, "b3: %x", *(buffer+3));
		bf_mgr_trace(LOG_ALW, 0x5662, "b4: %x", *(buffer+4));
		bf_mgr_trace(LOG_ALW, 0x7f38, "b5: %x", *(buffer+5));
		bf_mgr_trace(LOG_ALW, 0xf0d1, "b6: %x", *(buffer+6));
		bf_mgr_trace(LOG_ALW, 0x79cc, "b7: %x", *(buffer+7));
        #endif
	return;
}


ddr_code void crypto_init(void)
{
	sys_assert(crypto_hw_selftest(0xFFF) == false);

	crypto_hw_config(crypto_algo);
	crypto_mek_sram_init();

	///< they are test code
	{
		crypto_hw_program_entrys();
		crypto_hw_program_keys();
		crypto_mek_refresh_trigger();
	}

	crypto_lba_map_enable(CRYPTO_LBA_DEC_EN | CRYPTO_LBA_ENC_EN);
	return;
}
//Andy_Crypto
extern epm_info_t* shr_epm_info;

////Andy add function
#if CPU_ID == 1
ddr_code void crypto_change_mode_range(u8 crypto_type, u8 NS_ID, u8 change_key, u8 cryptoID)
//fast_code void crypto_change_mode_range(volatile ipc_req_t *req)
{
//u32 range, u8 mode
	crypto_entry_t entry;
	u32 key1[8];
	u32 key2[8];


//////Set enctypto type, range, namespace ID, global namespace
	//*	2'b00 -> crypto_cfg0_reg -> BYPASS mode
	//*	2'b01 -> crypto_cfg1_reg -> ECB-256
	//*	2'b10 -> crypto_cfg2_reg -> XTS-128
	//*	2'b11 -> crypto_cfg3_reg -> XTS-256

	memset(&entry, 0, sizeof(entry));
	entry.lm_nsid_misc.b.cfg_sel = crypto_type;
	entry.lm_range_start.b.range_start = 0;
	entry.lm_range_end.b.range_end = 0;
	entry.lm_nsid_misc.b.namespace_id = NS_ID;
	
	//set to encrypto specifies namespace
	//1. Namespace ID != 0
	//2. Range Start == Range End == 0
	entry.lm_nsid_misc.b.isglobal_namespace = 1;

	
	//set to encrypto range is global without any namespeace
	//1. Namespace ID == 0
	//2. Range Start == Range End == 0
	//entry.lm_nsid_misc.b.isglobal_globally = 1;

	///set entry = NSID
	crypto_prog_entry(cryptoID, entry);
	//crypto_prog_entry(CRYPTO_LBA_MAP_ENTRY_01, entry);
	
	bf_mgr_trace(LOG_INFO, 0x8a16, "[Andy] in");
	bf_mgr_trace(LOG_INFO, 0x6ac1, "Andy test sqipc crypto: tpye:%d ID:%d",crypto_type, NS_ID);
	bf_mgr_trace(LOG_INFO, 0x7660, "Andy test sqipc crypto: change key:%d cryptentry:%d",change_key, cryptoID);

//////Change key
//	AES-XTS algoritm requires key1 and key2!!!! 
//However, AES-ECB and SM4 requires only key1. Also SM4 support ONLY 128 bit keys.
//* 2'b00 -> crypto_cfg0_reg -> BYPASS mode
//* 2'b01 -> crypto_cfg1_reg -> ECB-256
//* 2'b10 -> crypto_cfg2_reg -> XTS-128
//* 2'b11 -> crypto_cfg3_reg -> XTS-256
#if 1
	if(change_key == 1)
	{
		memset(&key1, 0, sizeof(key1));
		memset(&key2, 0, sizeof(key2));
		#if defined (USE_SM4_ALGO)
			////SM4 only 128b key length
			sec_gen_rand_number((u8 *)key1, 16);	
		#else
			///Generate random key
			//extern void sec_gen_rand_number(u8 *buf, u8 rand_len);
			//Need check key size?
			//if(key_size == 15 && (crypto_type == 1 || crypto_type == 3))
			if( crypto_type == 1 || crypto_type == 3 )
			{   
				#if 0 //def TCG_SUPPORT
				sec_gen_rand_number((u8*)&pG3->b.mLckKAES_256_Tbl.val[0].key, 32);				
				//memcpy(&mRawKey[0].dek, &pG3->b.mLckKAES_256_Tbl.val[0].key, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key));
				#else
				sec_gen_rand_number((u8 *)key1, 32);	//256bit key
				#endif
				//XTS has key 2
				if(crypto_type == 3)
				{
					sec_gen_rand_number((u8 *)key2, 32);	//256bit key
				}				
			}
			else
			{
				sec_gen_rand_number((u8 *)key1, 16);  //128bit key
				//XTS has key 2
				if(crypto_type == 3)
				{
					sec_gen_rand_number((u8 *)key2, 16);	//256bit key
				}
			}

			//if(NS_ID == 0)
			/*
			{
				bf_mgr_trace(LOG_ALW, 0, "b0: %x", *key1);
				bf_mgr_trace(LOG_ALW, 0, "b1: %x", *(key1+1));
				bf_mgr_trace(LOG_ALW, 0, "b2: %x", *(key1+2));
				bf_mgr_trace(LOG_ALW, 0, "b3: %x", *(key1+3));
				bf_mgr_trace(LOG_ALW, 0, "b4: %x", *(key1+4));
				bf_mgr_trace(LOG_ALW, 0, "b5: %x", *(key1+5));
				bf_mgr_trace(LOG_ALW, 0, "b6: %x", *(key1+6));
				bf_mgr_trace(LOG_ALW, 0, "b7: %x", *(key1+7));
			}
			*/
			#if 0
			bf_mgr_trace(LOG_ALW, 0x5a40, "b0: %x", *key2);
			bf_mgr_trace(LOG_ALW, 0xe452, "b1: %x", *(key2+1));
			bf_mgr_trace(LOG_ALW, 0xfc52, "b2: %x", *(key2+2));
			bf_mgr_trace(LOG_ALW, 0xe9ae, "b3: %x", *(key2+3));
			bf_mgr_trace(LOG_ALW, 0x3869, "b4: %x", *(key2+4));
			bf_mgr_trace(LOG_ALW, 0x2d52, "b5: %x", *(key2+5));
			bf_mgr_trace(LOG_ALW, 0x330a, "b6: %x", *(key2+6));
			bf_mgr_trace(LOG_ALW, 0xeab1, "b7: %x", *(key2+7));
			#endif

			#if _TCG_
				
			#endif
			
			////Set key ti register
			crypto_hw_prgm_one_key((u8*)key1,(u8*)key2,cryptoID,crypto_type);
			///Update key
			crypto_mek_refresh_trigger();
		#endif
	}
#endif

#if 1
	///Test EPM AES
	epm_aes_t* epm_aes_data = (epm_aes_t*)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	EPM_crypto_info_t *crpto_information = (EPM_crypto_info_t*)(&epm_aes_data->data[0]);
	bf_mgr_trace(LOG_INFO, 0xa300, "Andy EPM update Start");

	//Update to EPM
	crpto_information[cryptoID].Crypt_FirstTime = 0xDEADFACE;
	crpto_information[cryptoID].crypto_type = crypto_type;
	crpto_information[cryptoID].NameSpace_ID = NS_ID;
	crpto_information[cryptoID].key_valid = 1;
	crpto_information[cryptoID].crypt_entry= cryptoID;

	//bf_mgr_trace(LOG_INFO, 0, "Andy EPM Set End");
	if(change_key == 1)
	{
		memcpy(&(crpto_information[cryptoID].key1), &key1, sizeof(key1));
		memcpy(&(crpto_information[cryptoID].key2), &key2, sizeof(key2));
	}

	epm_update(AES_sign,(CPU_ID-1));

	bf_mgr_trace(LOG_INFO, 0xf52c, "Andy EPM update end");
#endif

	flush_to_nand(EVT_CHANGE_KEY);

	return;
}

ddr_code void crypto_AES_EPM_Read(u8 cryptID, u8 mode)//slow code->init code
{
	///mode 0.....load EPM 
	///mode 1.....check FirstTime
	///Uart test EPM AES OK......20200917 Andy

	///Get epm aes info
	epm_aes_t* epm_aes_data = (epm_aes_t*)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	EPM_crypto_info_t *crpto_information = (EPM_crypto_info_t*)(&epm_aes_data->data[0]);

	if(mode == 1)
	{
		if(crpto_information[cryptID].Crypt_FirstTime != 0xDEADFACE)
		{
			return;
		}
		else
		{
			return;
		}
	}

	if(crpto_information[cryptID].Crypt_FirstTime != 0xDEADFACE)
		return;


	////Show which encrypt entry and entry info
	{
		bf_mgr_trace(LOG_INFO, 0x5a7e, "cryptID[%d]: tpye:%d ID:%d cryptentry:%d",cryptID,crpto_information[cryptID].crypto_type, 
			crpto_information[cryptID].NameSpace_ID,crpto_information[cryptID].crypt_entry);
	}

#if 0

	for(i=0 ;i<8 ;i++)
	{		
		bf_mgr_trace(LOG_INFO, 0xe9d8, "AES EPM key 1[%d] =:%x",i,crpto_information[cryptID].key1[i]);
	}

	
		for(i=0 ;i<8 ;i++)
		{
			bf_mgr_trace(LOG_INFO, 0xc52e, "AES EPM key 2[%d] =:%x",i,crpto_information[cryptID].key2[i]);
		}
#endif


	crypto_entry_t entry;

	////Update crypto entry
	memset(&entry, 0, sizeof(entry));
	entry.lm_nsid_misc.b.cfg_sel = crpto_information[cryptID].crypto_type;
	entry.lm_range_start.b.range_start = 0;
	entry.lm_range_end.b.range_end = 0;
	entry.lm_nsid_misc.b.namespace_id = crpto_information[cryptID].NameSpace_ID ;
	entry.lm_nsid_misc.b.isglobal_namespace = 1;
	//entry.lm_nsid_misc.b.isglobal_globally = 1;
	
	///Set crypto entry to register, set entry = NSID
	crypto_prog_entry(cryptID, entry);
	//crypto_prog_entry(CRYPTO_LBA_MAP_ENTRY_01, entry);

	///Set key to register
	crypto_hw_prgm_one_key((u8*)crpto_information[cryptID].key1,(u8*)crpto_information[cryptID].key2,cryptID,crpto_information[cryptID].crypto_type);
	///Update key
	crypto_mek_refresh_trigger();

	return;

}

#if _TCG_
ddr_code void tcg_set_mbr_range(u8 crypto_type, u8 cryptoID, u8 nsid)
{
	crypto_entry_t entry;
	memset(&entry, 0, sizeof(entry));

	entry.lm_nsid_misc.b.cfg_sel = crypto_type;
	entry.lm_range_start.b.range_start = 0;
	entry.lm_range_end.b.range_end = ((0x8000000 >> DTAG_SHF >> 10)-1);
	entry.lm_nsid_misc.b.namespace_id = nsid;

	bf_mgr_trace(LOG_INFO, 0x97ec, "Set Global Range with single namespace");

	crypto_prog_entry(cryptoID, entry);
}

ddr_code void tcg_set_aes_range(u8 crypto_type, u8 cryptoID, u8 nsid, u8 isGlobal, u8 arr_id)
{
//////Set enctypto type, range, namespace ID, global namespace
	//*	2'b00 -> crypto_cfg0_reg -> BYPASS mode
	//*	2'b01 -> crypto_cfg1_reg -> ECB-256
	//*	2'b10 -> crypto_cfg2_reg -> XTS-128
	//*	2'b11 -> crypto_cfg3_reg -> XTS-256

	crypto_entry_t entry;
	memset(&entry, 0, sizeof(entry));
	
	entry.lm_nsid_misc.b.cfg_sel = crypto_type;
#ifdef NS_MANAGE
	if(ns_array_menu->total_order_now > 1)
	{
		entry.lm_range_start.b.range_start = 0;
		entry.lm_range_end.b.range_end = 0;
		entry.lm_nsid_misc.b.namespace_id = nsid;
		entry.lm_nsid_misc.b.isglobal_namespace = 1;

		bf_mgr_trace(LOG_INFO, 0x2549, "[TCG] Set multiple NS Range, NSID: %d", nsid);
		//cryptoID--;
	}
	else
#endif
	{
		if(isGlobal)
		{
			//entry.lm_range_start.b.range_start = 0;
			//entry.lm_range_end.b.range_end = 0;
			entry.lm_nsid_misc.b.namespace_id = nsid;
			entry.lm_nsid_misc.b.isglobal_namespace = 1;
			
			bf_mgr_trace(LOG_INFO, 0xcf44, "[TCG] Set Global Range with single namespace");
		}
		else
		{
			entry.lm_range_start.b.range_start = (pLockingRangeTable[arr_id].rangeStart >> (DTAG_SHF +10 - host_sec_bitz));
			entry.lm_range_end.b.range_end = (pLockingRangeTable[arr_id].rangeEnd >> (DTAG_SHF + 10 - host_sec_bitz));
			entry.lm_nsid_misc.b.namespace_id = nsid;

			bf_mgr_trace(LOG_INFO, 0x9afc, "[TCG] Set non-Global Range: %d, with single namespace", cryptoID);
		}
	}

	///set entry = NSID
	crypto_prog_entry(cryptoID, entry);
	//crypto_prog_entry(CRYPTO_LBA_MAP_ENTRY_01, entry);
}

ddr_code void tcg_init_aes_key_range(void)
{
	u32 array_id = 0;
	u32 entry_id = 0;
    u32 rangeNo;
#ifdef NS_MANAGE
	u32 ns_id = ns_array_menu->array_order[0] + 1;
#else
	u32 ns_id = 1;
#endif
	
    //crypto_init();

	if(mTcgStatus & MBR_SHADOW_MODE)
	{
#ifdef NS_MANAGE
		for(u8 i=0; i<ns_array_menu->total_order_now; i++, entry_id++)
		{
			ns_id = ns_array_menu->array_order[i] + 1;
			bf_mgr_trace(LOG_INFO, 0x73cd, "[TCG] MBR Crypto entry ID %d, NSID %d", entry_id, ns_id);
			tcg_set_mbr_range(Disable_crypto, entry_id, ns_id);
		}
#else
		bf_mgr_trace(LOG_INFO, 0x92ff, "[TCG] MBR Crypto entry ID %d, NSID %d", entry_id, ns_id);
		tcg_set_mbr_range(Disable_crypto, entry_id, ns_id);
		entry_id++;
#endif
	}

#ifdef NS_MANAGE
	if(ns_array_menu->total_order_now > 1)
	{
		for(u8 i=0; i<ns_array_menu->total_order_now; i++, entry_id++)
		{
			ns_id = ns_array_menu->array_order[i] + 1;
			bf_mgr_trace(LOG_INFO, 0xf5ba, "[TCG] Crypto entry ID %d, NSID %d", entry_id, ns_id);
			tcg_set_aes_range(AES_XTS_256B_KEY, entry_id, ns_id, true, 0);

			PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt,sizeof(pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt)
						       ,1,32,(u32*)WrapKEK);		//Gen current kek
				
			Tcg_Key_wp_uwp(LOCKING_RANGE_CNT+ns_id, AES_256B_KUWP_NO_SECURE);        //unwrap Tbl key to mRawkey

			crypto_hw_prgm_one_key((u8 *)&mRawKey[LOCKING_RANGE_CNT+ns_id].dek.aesKey, (u8 *)&mRawKey[LOCKING_RANGE_CNT+ns_id].dek.xtsKey, entry_id, AES_XTS_256B_KEY);
		}
	}
	else
#endif
	{
    	for(array_id = 0; array_id <= LOCKING_RANGE_CNT; array_id++, entry_id++)
    	{	
		
        	if (pLockingRangeTable[array_id].rangeNo==0)
				break;
            
        	rangeNo = pLockingRangeTable[array_id].rangeNo;
		
			bf_mgr_trace(LOG_INFO, 0xa363, "[TCG] check rangeNo & entry_id : %x|%x", rangeNo, entry_id);
		
			tcg_set_aes_range(AES_XTS_256B_KEY, entry_id, ns_id, false, array_id);
			
			PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[rangeNo].salt,sizeof(pG3->b.mKEKsalt[rangeNo].salt)
							  ,1,32,(u32*)WrapKEK);	//Gen current kek
								
			Tcg_Key_wp_uwp(rangeNo, AES_256B_KUWP_NO_SECURE); 	   //unwrap Tbl key to mRawkey
			
        	crypto_hw_prgm_one_key((u8 *)&mRawKey[rangeNo].dek.aesKey, (u8 *)&mRawKey[rangeNo].dek.xtsKey, entry_id, AES_XTS_256B_KEY);
		
		}

    	//global range init
    	tcg_set_aes_range(AES_XTS_256B_KEY, entry_id, ns_id, true, 0);

		bf_mgr_trace(LOG_INFO, 0x0a25, "[TCG] Global check entry_id : %x", entry_id);
		
		PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[0].salt,sizeof(pG3->b.mKEKsalt[0].salt)
						  ,1,32,(u32*)WrapKEK);	//Gen current kek
							
		Tcg_Key_wp_uwp(0, AES_256B_KUWP_NO_SECURE);	   //unwrap Tbl key to mRawkey
		
		crypto_hw_prgm_one_key((u8 *)&mRawKey[0].dek.aesKey, (u8 *)&mRawKey[0].dek.xtsKey, entry_id, AES_XTS_256B_KEY);
	}
    //tcg_set_aes_range();
    crypto_mek_refresh_trigger();

	bKeyChanged = mFALSE;

	memset(mRawKey , 0, sizeof(mRawKey));

    //crypto_lba_map_enable(CRYPTO_LBA_DEC_EN | CRYPTO_LBA_ENC_EN);
}
#endif // _TCG_

#endif // CPU_ID == 1


////Andy add uart
#if CPU_ID == 1

static ddr_code int crypt_bypass_tcg(int argc, char *argv[])
{
	bool bypass = strtol(argv[1], (void *)0, 0);

	if(bypass)
	{
		bf_mgr_trace(LOG_INFO, 0x3ac3, "Bypass AES encrypto");
		for(u8 i=0; i<64; i++)
		{
			crypto_entry_t entry;
			memset(&entry, 0, sizeof(entry));

			if(i==0)
				entry.lm_nsid_misc.b.isglobal_globally = 1;

			crypto_prog_entry(i, entry);
		}
	}
	else
	{
		bf_mgr_trace(LOG_INFO, 0xb868, "Resume AES encrypto");
#if (_TCG_)
		tcg_init_aes_key_range();
#else
		epm_aes_t* epm_aes_data = (epm_aes_t*)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		EPM_crypto_info_t *crpto_information = (EPM_crypto_info_t*)(&epm_aes_data->data[0]);
		for(u8 cryptID = 0; cryptID < 32; cryptID++)
		{
			if(crpto_information[cryptID].Crypt_FirstTime != 0xDEADFACE)
				continue;
			////Show which encrypt entry and entry info
			{
				bf_mgr_trace(LOG_INFO, 0x68a1, "cryptID[%d]: tpye:%d ID:%d cryptentry:%d",cryptID,crpto_information[cryptID].crypto_type, 
					crpto_information[cryptID].NameSpace_ID,crpto_information[cryptID].crypt_entry);
			}
		
			crypto_entry_t entry;
		
			////Update crypto entry
			memset(&entry, 0, sizeof(entry));
			entry.lm_nsid_misc.b.cfg_sel = crpto_information[cryptID].crypto_type;
			entry.lm_range_start.b.range_start = 0;
			entry.lm_range_end.b.range_end = 0;
			entry.lm_nsid_misc.b.namespace_id = crpto_information[cryptID].NameSpace_ID ;
			entry.lm_nsid_misc.b.isglobal_namespace = 1;
			//entry.lm_nsid_misc.b.isglobal_globally = 1;
			
			///Set crypto entry to register, set entry = NSID
			crypto_prog_entry(cryptID, entry);
			//crypto_prog_entry(CRYPTO_LBA_MAP_ENTRY_01, entry);
		
			///Set key to register
			crypto_hw_prgm_one_key((u8*)crpto_information[cryptID].key1,(u8*)crpto_information[cryptID].key2,cryptID,crpto_information[cryptID].crypto_type);
			///Update key
			crypto_mek_refresh_trigger();
		}
#endif
	}
	return 0;
}

static DEFINE_UART_CMD(crypt_bypass_tcg, "crypt_bypass_tcg", "crypt_bypass_tcg", "crypt_bypass_tcg", 1, 1,crypt_bypass_tcg);

#if 0
static init_code int crypt_testfun(int argc, char *argv[])
{
	crypto_change_mode_range(0,1, 0,0);
	return 0;
}
static init_code int crypt_test_AES_epm(int argc, char *argv[])
{
	//crypto_change_mode_range(2,1, 0,0);
	crypto_AES_EPM_Read(0,0);
	return 0;
}
static init_code int crypt2(int argc, char *argv[])
{
	crypto_change_mode_range(1,1, 0,1);
	return 0;
}
static init_code int crypt3(int argc, char *argv[])
{
	crypto_change_mode_range(2,1, 1,1);
	return 0;
}
static init_code int crypt4(int argc, char *argv[])
{
	crypto_change_mode_range(1,1, 1,1);
	return 0;
}
static init_code int crypt5(int argc, char *argv[])
{
	crypto_change_mode_range(2,1, 0,1);
	return 0;
}


////Andy add function
static DEFINE_UART_CMD(crypt_test, "crypt_test", "crypt_test",
		"test random generate", 0, 0,crypt_testfun);
static DEFINE_UART_CMD(crypt_epm_check, "crypt_epm_check", "crypt_epm_check",
		"test epm data", 0, 0,crypt_test_AES_epm);
static DEFINE_UART_CMD(crypt2, "crypt2", "crypt2",
				"crypt2", 0, 0,crypt2);
static DEFINE_UART_CMD(crypt3, "crypt3", "crypt3",
				"crypt3", 0, 0,crypt3);
static DEFINE_UART_CMD(crypt4, "crypt4", "crypt4",
				"crypt4", 0, 0,crypt4);
static DEFINE_UART_CMD(crypt5, "crypt5", "crypt5",
				"crypt5", 0, 0,crypt5);


#endif


#endif

#if 0 ///<validation code
#define TICKET_3226 0
#define TICKET_3090 0
#define TICKET_3585 0
/**
 * Perform cryoto engine hw tests
 *
 * @param[IN] None
 *
 * @return None
 **/
void crypto_engine_hw_tests(void)
{
	//aes_crypto_hw_regs_check();

#if TICKET_3226
	u32 i, fail_cnt = 0;
	for (i = 0; i < 100; i++) {
		crypto_hw_config(CRYPTO_ALGO_SM4);
		if (crypto_hw_selftest(0xFF))
			fail_cnt++;
		crypto_hw_config(CRYPTO_ALGO_AES);
		if (crypto_hw_selftest(0xFF))
			fail_cnt++;
		bf_mgr_trace(LOG_INFO, 0x821c, "Crypto self test fail cnt: 0x%x\n", fail_cnt);
	}
#endif //TICKET_3226
#if TICKET_3090
	crypto_algo = CRYPTO_ALGO_AES;
	crypto_hw_config(crypto_algo);
	crypto_mek_sram_init();
	crypto_hw_program_keys();
	crypto_mek_refresh_trigger();
	crypto_hw_program_entrys();
	crypto_lba_map_enable(CRYPTO_LBA_DEC_EN | CRYPTO_LBA_ENC_EN);
#endif //TICKET_3090

#if TICKET_3585
	crypto_algo = CRYPTO_ALGO_AES;
	crypto_hw_config(crypto_algo);
	crypto_lba_map_enable(CRYPTO_LBA_DEC_EN | CRYPTO_LBA_ENC_EN);
#endif //TICKET_3585
	return;
}

int crypto_chg_mode(int argc, char *argv[])
{
	bf_mgr_trace(LOG_INFO, 0xb1f1, "Crypto mode: %x-->%x\n", crypto_algo, atoi(argv[1]));
	crypto_hw_config(atoi(argv[1]));
	crypto_algo = atoi(argv[1]);
	return 0;
}

int crypto_refresh_key(int argc, char *argv[])
{
	u32 i;
	u32 start = atoi(argv[1]);
	u32 end = atoi(argv[2]);
	for (i = start; i <= end; i++) {
		u32 mek1[8] = {i, 0x22222222, i, 0x44444444,
			i, 0x66666666, i, 0x88888888};
		u32 mek2[8] = {0x12121212, i, 0x34343434, i,
			0x56565656, i, 0x78787878, i};
		crypto_hw_prgm_one_key((u8 *)mek1,
			(u8 *)mek2, i, AES_XTS_256B_KEY);
	}
	crypto_mek_refresh_trigger();
	return 0;
}

int crypto_configuration_select(int argc, char *argv[])
{
	crypto_entry_chg_cfg_sel(atoi(argv[1]), atoi(argv[2]));
	return 0;
}

static DEFINE_UART_CMD(key_refresh, "key_refresh",
	"key_refresh [start key index(min: 0)][end key index(max: 63)]", "",
	2, 2, crypto_refresh_key);

static DEFINE_UART_CMD(cfg_sel, "cfg_sel",
	"cfg_sel [entry index(min: 0)][cfg_sel]", "",
	2, 2, crypto_configuration_select);

static DEFINE_UART_CMD(crypto, "crypto", "crypto", "crypto: AES(0) SM4(1)",
	1, 1, crypto_chg_mode);
#endif //validation code
#endif //USE_CRYPTO_HW
