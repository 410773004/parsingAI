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
#include "stdio.h"
#include "io.h"
#include "rainier_soc.h"
#include "sect.h"
#include "event.h"
#include "assert.h"
#include "vic_id.h"
#include "console.h"
#include "string.h"
#include "ddr.h"
#include "l2cache_register.h"
#include "l2cache.h"

/*! \cond PRIVATE */
#define __FILEID__ lxc
#include "trace.h"
/*! \endcond */

enum {
	TAG_RAM = 0,
	DAT_RAM = 1,
};

#define L2C_BASE	0xC00E0000
#define NUM_WAY		8
#define NUM_SET		((L2CRAM_SIZE / NUM_WAY) / (cache_line == CACHE_LINE_64B ? 64 : 32))

share_data volatile u8 cache_line;

/*!
 * @brief check if l2cache common fifo was full
 *
 * @return		return true if fifo was full
 */
static bool l2c_common_fifo_full(void);

static fast_code u32 l2c_readl(u32 reg)
{
	return readl((void *)(L2C_BASE + reg));
}

static fast_code void l2c_writel(u32 val, u32 reg)
{
	writel(val, (void *)(L2C_BASE + reg));
}

fast_code bool l2cache_enable(bool enable)
{
	u32 ret;
	cache_config_reg_t ccfg = { .all = l2c_readl(CACHE_CONFIG_REG),};

	while(l2c_common_fifo_full());

	ret = ccfg.b.cache_bypass;
	ccfg.b.cache_bypass = !enable;
	l2c_writel(ccfg.all, CACHE_CONFIG_REG);
	rtos_mmgr_trace(LOG_DEBUG, 0xc364, "l2cache Bypass status was %d is %d", ret, !enable);
	return !ret;
}

static fast_code u32 _l2c_mem_ext_acc(u32 sel_id, u32 acc_addr, u32 dw_sel)
{
	u32 res;
	mem_ext_acc_reg_0_t acc_reg = { .all = l2c_readl(MEM_EXT_ACC_REG_0),};

	acc_reg.b.mem_ext_acc_en = 1;
	acc_reg.b.mem_ext_acc_sel_id = sel_id;
	acc_reg.b.mem_ext_acc_dw_sel = dw_sel;
	acc_reg.b.mem_ext_acc_adr = acc_addr;
	l2c_writel(acc_reg.all, MEM_EXT_ACC_REG_0);

	res = l2c_readl(MEM_EXT_ACC_REG_1);

	acc_reg.b.mem_ext_acc_en = 0;
	l2c_writel(acc_reg.all, MEM_EXT_ACC_REG_0);

	return res;
}

static fast_code u32 l2c_check_data_ram_allzero(void) // cpu access
{
	u32 *ram_ptr = (u32*) L2CRAM_BASE;
	u32 err = 0;
	u32 last = l2cache_enable(false);

	for (; (u32)ram_ptr < L2CRAM_BASE + L2CRAM_SIZE; ram_ptr++ ) {
		if (*ram_ptr) {
			rtos_mmgr_trace(LOG_ERR, 0xbfbc, "Not Zero: addr %x  data %x\n", ram_ptr, *ram_ptr);
			err++;
		}
	}

	l2cache_enable(last);
	return err;
}

fast_code u32 l2c_check_tag_ram_allzero(void)
{
	u32 err = 0;
	u32 set_cnt;
	u32 i;
	u32 j;

	if (cache_line == CACHE_LINE_32B)
		set_cnt = L2CRAM_SIZE / 32;
	else
		set_cnt = L2CRAM_SIZE / 64;

	for (i = 0; i < set_cnt; i++) {
		for (j = 0; j < NUM_WAY; j++) {
			u32 val = _l2c_mem_ext_acc(TAG_RAM, i, j);

			if (val) {
				rtos_mmgr_trace(LOG_ERR, 0xc70b, "Not Zero: set %d dw %d data %x", i, j, val);
				err++;
			}
		}
	}
	return err;
}

/****************************************************************************
 * Preload/Flush/Invalidate
 ****************************************************************************/
fast_code u32 l2c_preload_fifo_full(void)
{
	preload_fifo_status_reg_t sts = { .all = l2c_readl(PRELOAD_FIFO_STATUS_REG), };
	return sts.b.preload_fifo_full;
}

fast_code u32 l2c_preload_fifo_empty(void)
{
	preload_fifo_status_reg_t sts = { .all = l2c_readl(PRELOAD_FIFO_STATUS_REG), };
	return sts.b.preload_fifo_empty;
}

fast_code bool l2c_common_fifo_full(void)
{
	common_fifo_status_reg_t sts = { .all = l2c_readl(COMMON_FIFO_STATUS_REG), };
	return sts.b.common_fifo_full;
}

fast_code u32 l2c_common_fifo_empty(void)
{
	common_fifo_status_reg_t sts = { .all = l2c_readl(COMMON_FIFO_STATUS_REG), };
	return sts.b.common_fifo_empty;
}

enum {
	PRE_FIFO = 0,
	COM_FIFO = 1,
};

static bool _cacheline_addr_check(u32 addr)
{
	bool ret = false;

	if (cache_line == CACHE_LINE_64B) {
		if (addr & (0x3 << 30)) //for 64B-aligned, bit[30] is 0
			ret = 1;
	}
	return ret;
}

static u32 _set_way_valid_check(u32 set, u32 way)
{
	u32 ret = 0;

	if (way >= NUM_WAY)
		ret = 1;

	if (cache_line == CACHE_LINE_64B) {
		if (set > 255) //for 64B-aligned, set_index 0~255
			ret = 1;
	} else {
		if (set > 511) //for 32B-aligned, set_index 0~511
			ret = 1;
	}
	return ret;
}

u32 l2c_submit_preload(u32 fifo, u32 addr)
{
	preload_address_reg_t preld;
	preld.b.preload_fifo_sel = fifo;
	preld.b.preload_address = addr;

	if (_cacheline_addr_check(addr)) {
		panic("address invalid");
		//rtos_mmgr_trace(LOG_ERR, 0, "Address invalid\n");
	}
	if (fifo == PRE_FIFO) {
		while(l2c_preload_fifo_full());
	} else if (fifo == COM_FIFO) {
		while(l2c_common_fifo_full());
	} else {
		panic("fifo type wrong");
	}

	l2c_writel(preld.all, PRELOAD_ADDRESS_REG);

	return 0;
}

fast_code void l2c_submit_flush(l2c_flush_type_t adr_type, u32 addr_set, u32 way)
{
	flush_address_reg_t f_addr;
	flush_way_set_reg_t f_ws;

	while(l2c_common_fifo_full());

	if (adr_type == ADDRESS) {
		u32 cline_shift = (cache_line == CACHE_LINE_32B) ? 5 : 6;

		if (_cacheline_addr_check(addr_set >> cline_shift))
			panic("address invalid");
		f_addr.b.flush_address = addr_set;
		l2c_writel(f_addr.all, FLUSH_ADDRESS_REG);
	} else if (adr_type == WAYSET) {
		if (_set_way_valid_check(addr_set, way))
			panic("set way invalid");
		f_ws.b.flush_set_index = addr_set;
		f_ws.b.flush_way_index = way;
		l2c_writel(f_ws.all, FLUSH_WAY_SET_REG);
	} else {
		panic("wrong addr type");
	}
	// todo: this should be optimized
	while (!l2c_common_fifo_empty());
}

fast_code void l2c_submit_invalidate(u32 adr_type, u32 addr_set, u32 way)
{
	invalidate_address_reg_t i_addr;
	invalidate_way_set_reg_t i_ws;

	while(l2c_common_fifo_full());

	if (adr_type == ADDRESS) {
		u32 cline_shift = (cache_line == CACHE_LINE_32B) ? 5 : 6;

		if (_cacheline_addr_check(addr_set >> cline_shift))
			panic("address invalid");

		i_addr.b.invalidate_address = addr_set;
		l2c_writel(i_addr.all, INVALIDATE_ADDRESS_REG);
	} else if (adr_type == WAYSET) {
		if (_set_way_valid_check(addr_set, way)) {
			panic("set way invalid");
		}
		i_ws.b.invalidate_set_index = addr_set;
		i_ws.b.invalidate_way_index = way;
		l2c_writel(i_ws.all, INVALIDATE_WAY_SET_REG);
	} else {
		panic("wrong addr type");
	}
}

/****************************************************************************
 * Test functions
 ****************************************************************************/
u32 l2c_stat_rd_hit(void)
{
	cache_rd_info_0_reg_t rd_cnt  = { .all = l2c_readl(CACHE_RD_INFO_0_REG) };
	cache_rd_info_1_reg_t rht_cnt = { .all = l2c_readl(CACHE_RD_INFO_1_REG) };
	rtos_mmgr_trace(LOG_ERR, 0xc395, "hit/rd: %d / %d\n", rht_cnt.b.cache_rd_hit_count, rd_cnt.b.cache_rd_count);
	return rht_cnt.b.cache_rd_hit_count;
}

u32 l2c_stat_wr_hit(void)
{
	cache_wr_info_0_reg_t wr_cnt  = { .all = l2c_readl(CACHE_WR_INFO_0_REG) };
	cache_wr_info_1_reg_t wht_cnt = { .all = l2c_readl(CACHE_WR_INFO_1_REG) };
	rtos_mmgr_trace(LOG_ERR, 0xb0b7, "hit/wr: %d / %d\n", wht_cnt.b.cache_wr_hit_count, wr_cnt.b.cache_wr_count);
	return wht_cnt.b.cache_wr_hit_count;
}

#if 0
u32 l2c_hitcount_test(void)
{
	u32 ofst = 0; // get_tsc_lo(); //
	rtos_mmgr_trace(LOG_ERR, 0x4a09, "ofst %x\n", ofst);

	u32 i, j;
#define DDR_TEST_BASE 0x40080000
#define DDR_TEST_BASE2 0x40294000

#define DDR_TEST_STEP 0x20
	for (i=0; i < 10; i++) { // CPU write DDR
		*(u32*)(DDR_TEST_BASE + i*DDR_TEST_STEP) = 0x40000000 + i + ofst;
	}

	for (i=0; i < 10; i++) { // write another section
		*(u32*)(DDR_TEST_BASE2 + i*DDR_TEST_STEP) = 0x40000000 + i + ofst;
	}

	for (j = 0; j < 1; j++) { // read
		for (i=0; i < 10; i++) {
			if (*(u32*)(DDR_TEST_BASE+i*DDR_TEST_STEP) != 0x40000000 + i + ofst) {
				rtos_mmgr_trace(LOG_ERR, 0x0dbd, "mismatch @ %x: %x\n", 0x40000000+i, *(u32*)(DDR_TEST_BASE+i*DDR_TEST_STEP));
			}
		}
	}

}
#endif

fast_code static void _l2cache_rd_info_init(void)
{
	cache_rd_info_ctrl_reg_t rd_ctrl = { .all = l2c_readl(CACHE_RD_INFO_CTRL_REG), };
	rd_ctrl.b.cache_rd_info_en = 1;
	rd_ctrl.b.cache_rd_info_reset = 1;
	l2c_writel(rd_ctrl.all, CACHE_RD_INFO_CTRL_REG);
	while (rd_ctrl.b.cache_rd_info_reset)
		rd_ctrl.all = l2c_readl(CACHE_RD_INFO_CTRL_REG);
}

fast_code static void _l2cache_wr_info_init(void)
{
	cache_wr_info_ctrl_reg_t wr_ctrl = { .all = l2c_readl(CACHE_WR_INFO_CTRL_REG), };
	wr_ctrl.b.cache_wr_info_en = 1;
	wr_ctrl.b.cache_wr_info_reset = 1;
	l2c_writel(wr_ctrl.all, CACHE_WR_INFO_CTRL_REG);
	while (wr_ctrl.b.cache_wr_info_reset)
		wr_ctrl.all = l2c_readl(CACHE_WR_INFO_CTRL_REG);
}

// reload l2cache with data starting from addr
fast_code void l2cache_preload_all(u32 addr)
{
	u32 cline_sz = (cache_line == CACHE_LINE_32B) ? 32 : 64;
	u32 ttl = L2CRAM_SIZE / cline_sz;
	u32 i;

	for ( i = 0; i < ttl; i++)
		l2c_submit_preload(PRE_FIFO, addr / cline_sz + i);

	rtos_mmgr_trace(LOG_INFO, 0xdefd, "cache reload done from addr %x to %x", addr, addr + L2CRAM_SIZE - 1);
}

fast_code void l2cache_flush_all(void)
{
	u32 set;
	u32 way;

#if defined(MPC)
	spin_lock_take(SPIN_LOCK_KEY_L2C_OP, 0, true);
#endif
	for (set = 0; set < NUM_SET; set++) {
		for (way = 0; way < 8; way++)
			l2c_submit_flush(WAYSET, set, way);
	}
#if defined(MPC)
	spin_lock_release(SPIN_LOCK_KEY_L2C_OP);
#endif
	rtos_mmgr_trace(LOG_INFO, 0x89dc, "cache flush all");
}

fast_code void l2cache_invalidate_all(void)
{
	cache_config_reg_t ccfg = { .all = l2c_readl(CACHE_CONFIG_REG), };

	ccfg.b.cache_initialize = 1;  // zero all TAG/DATA RAM
	l2c_writel(ccfg.all, CACHE_CONFIG_REG);
	while (ccfg.b.cache_initialize)
		ccfg.all = l2c_readl(CACHE_CONFIG_REG);

	rtos_mmgr_trace(LOG_DEBUG, 0x44a3, "TAG/DAT RAM zeroed");
}

ddr_code void l2cache_init(l2cache_line_t l2cache_line, l2cache_type_t l2cache_type)
{
	mem_ext_acc_reg_0_t acc_reg = { .all = l2c_readl(MEM_EXT_ACC_REG_0), };

	acc_reg.b.mem_ext_acc_en = 0;
	l2c_writel(acc_reg.all, MEM_EXT_ACC_REG_0);

	cache_config_reg_t ccfg = { .all = l2c_readl(CACHE_CONFIG_REG), };

	cache_line = l2cache_line;
	ccfg.b.cache_line_size_sel = l2cache_line;
	ccfg.b.memory_type_ctrl = l2cache_type;
	ccfg.b.cache_reset = 0;

	ccfg.b.cache_initialize = 1;  // zero all TAG/DATA RAM
	l2c_writel(ccfg.all, CACHE_CONFIG_REG);

	while (ccfg.b.cache_initialize)
		ccfg.all = l2c_readl(CACHE_CONFIG_REG);

	if (l2c_check_data_ram_allzero() || l2c_check_tag_ram_allzero())
		sys_assert(0);
	else
		rtos_mmgr_trace(LOG_ERR, 0x5f79, "Tag/Data RAM all ZEROs\n");

	_l2cache_rd_info_init();
	_l2cache_wr_info_init();

	l2cache_enable(false);
}

