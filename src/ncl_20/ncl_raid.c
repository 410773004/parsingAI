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
/*! \file ncl_raid.c
 * @brief NCL RAID module
 *
 * \addtogroup ncl_20
 * \defgroup ncl_raid
 * \ingroup ncl_20
 *
 * @{
 */
#include "eccu.h"
#include "finstr.h"
#include "nand.h"
#include "ficu.h"
#include "ncl.h"
#include "ddr.h"
#include "ncl_err.h"
#include "ncl_cmd.h"
#include "ncl_raid.h"
#include "raid_top_register.h"
#include "eccu_reg_access.h"
#include "ncb_top_wrap_reg_register.h"
#include "console.h"
#include "ncl.h"

/*! \cond PRIVATE */
#define __FILEID__ raid
#include "trace.h"
/*! \endcond */

fast_data_zi u8 evt_xor_cmpl;
fast_data_ni u8 stripe_lut[TTL_RAID_ID];	///< map table of raid id to stripe id
fast_data_ni u8 stripe_left_cmpl[MAX_CWL_STRIPE];	///< expected cmpl cnt of each stripe
fast_data_ni raid_cmpl_entry_t raid_cmpl_que[TTL_RAID_ID];

fast_code u8 get_raid_cmd(struct ncl_cmd_t *ncl_cmd)
{
	u8 raid_cmd = NO_RAID;

	if (ncl_cmd->flags & NCL_CMD_RAID_FLAG) {
		if (ncl_cmd->op_code == NCL_CMD_OP_WRITE) {
			if (ncl_cmd->flags & NCL_CMD_XOR_FLAG)
				raid_cmd = XOR_W_DATA;
			else if (ncl_cmd->flags & NCL_CMD_POUT_FLAG)
				raid_cmd = POUT_W_DATA;
			else if (ncl_cmd->flags & NCL_CMD_SUSPEND_FLAG)
				raid_cmd = SRAM_BUF_DATA_TO_DDR;
			else if (ncl_cmd->flags & NCL_CMD_RESUME_FLAG)
				raid_cmd = DDR_BUF_DATA_TO_SRAM;
			else if (ncl_cmd->flags & NCL_CMD_XOR_ONLY_FLAG)
				raid_cmd = XOR_WO_DATA;
#if !ONFI_DCC_TRAINING
			else if (ncl_cmd->flags & NCL_CMD_POUT_ONLY_FLAG)  //temp_mark_for_dq_training_flag_0420
				raid_cmd = POUT_WO_DATA_WO_STS_WO_BUF_RLS;
#endif
			else
				sys_assert(false);
		} else if ((ncl_cmd->op_code == NCL_CMD_OP_READ)
		            || (ncl_cmd->op_code == NCL_CMD_PATROL_READ)
                    #ifdef NCL_FW_RETRY_BY_SUBMIT
                    || (ncl_cmd->op_code == NCL_CMD_FW_RETRY_READ)
                    #endif
                    )
		{
			if (ncl_cmd->flags & NCL_CMD_XOR_ONLY_FLAG)
				raid_cmd = XOR_WO_DATA;
			else if (ncl_cmd->flags & NCL_CMD_POUT_FLAG)
				raid_cmd = POUT_W_DATA;
			else
				sys_assert(false);
		} else {
			sys_assert(false);
		}
	}

	return raid_cmd;
}

fast_code void ncl_raid_reset(void) {
    	raid_ctrs_reg0_t reg;

	reg.all = raid_top_readl(RAID_CTRS_REG0);
	reg.b.raid_reset = 1;
	raid_top_writel(reg.all, RAID_CTRS_REG0);

	reg.all = raid_top_readl(RAID_CTRS_REG0);
	reg.b.raid_reset = 0;
	raid_top_writel(reg.all, RAID_CTRS_REG0);
}

fast_code void ncl_raid_enable() {
	raid_ctrs_reg0_t reg;
	reg.all = raid_top_readl(RAID_CTRS_REG0);
	reg.b.raid_en = 1;
	raid_top_writel(reg.all, RAID_CTRS_REG0);

	eccu_ctrl_reg1_t eccReg;
	eccReg.all = eccu_readl(ECCU_CTRL_REG1);
	eccReg.b.eccu_raid_en = 1;
	eccReg.b.dec_raid_bypass_err = 1;
	eccu_writel(eccReg.all, ECCU_CTRL_REG1);

	ficu_ctrl_reg0_t ficuReg;
	ficuReg.all = ficu_readl(FICU_CTRL_REG0);
	ficuReg.b.ficu_raid_en = 1;
	ficu_writel(ficuReg.all, FICU_CTRL_REG0);
}

fast_code struct partial_du_fmt_t get_du_fmt(void)
{
	eccu_du_fmt_sel_reg_t fmt_sel;
	struct partial_du_fmt_t fmt;

	fmt_sel.all = eccu_readl(ECCU_DU_FMT_SEL_REG);
	fmt_sel.b.du_fmt_cfg_idx = DU_FMT_USER_4K;
	eccu_writel(fmt_sel.all, ECCU_DU_FMT_SEL_REG);
	fmt.reg0.all = eccu_readl(ECCU_DU_FMT_REG0);
	fmt.reg1.all = eccu_readl(ECCU_DU_FMT_REG1);

	return fmt;
}

fast_code void set_du_fmt(void)
{
	eccu_du_fmt_sel_reg_t fmt_sel;
	eccu_du_fmt_reg0_t fmt_reg0;
	fmt_sel.all = eccu_readl(ECCU_DU_FMT_SEL_REG);
	fmt_sel.b.du_fmt_cfg_idx = DU_FMT_USER_4K;
	eccu_writel(fmt_sel.all, ECCU_DU_FMT_SEL_REG);

	fmt_reg0.all = du_fmt_tbl[DU_FMT_USER_4K].fmt0.all;
	eccu_writel(fmt_reg0.all, ECCU_DU_FMT_REG0);
	ficu_mode_disable();
	ficu_mode_enable();
}

fast_code u32 get_blk_size(enum buf_type buf_type)
{
	struct partial_du_fmt_t fmt = get_du_fmt();
	u32 hlba_sz = 0, du_sz;
	// round up to 16 in sram, 32 in ddr
	u32 r = (buf_type == BUF_TYPE_SRAM) ? 16 : 32;

	switch (fmt.reg0.b.hlba_sz_20) {
	case 0:
		hlba_sz = 0;
		break;
	case 1:
		hlba_sz = 4;
		break;
	case 2:
		hlba_sz = 8;
		break;
	default:
		sys_assert(0);
	}

	du_sz = fmt.reg0.b.meta_sz + hlba_sz +
		(fmt.reg0.b.host_sector_sz + fmt.reg1.b.pi_sz) * fmt.reg0.b.du2host_ratio;

	return round_up_by_2_power(du_sz, r) * DU_CNT_PER_PAGE;
}

fast_code void *get_raid_sram_buff_addr(u8 raid_id)
{
	// setup mem size
	u32 block_size = 17152;
	u32 bank_size = block_size * RAID_BUF_PER_BANK;

	// get raid id info
	raid_top_writel(raid_id, RAID_ID_STATUS_REG);
	u32 reg_val = raid_top_readl(RAID_ID_STATUS_REG);
	u32 bank_id = (reg_val >> 20) & 0x07;
	u32 block_id = (reg_val >> 16) & 0x07;
	u32 raid_id_state = (reg_val >> 8) & 0x0f;

	// calc sram buff addr
	u32 addr_base = RAID_BASE;
	u32 bank_base = bank_size * bank_id;
	u32 buff_base = block_size * block_id;
	u32 addr = addr_base + bank_base + buff_base;

    if((raid_id_state != 1 && raid_id_state != 2)
        || (addr < RAID_BASE) || (addr + block_size > RAID_BASE + RAID_SIZE)){   //1 0r 2: raid id in sram 0x20200000~202860000
        ncl_raid_trace(LOG_ALW, 0x2011, "[error] reg_val 0x%x raid_id_state %d addr 0x%x", reg_val, raid_id_state, addr);
        return NULL;
    }
	return (void *)(addr);
}

init_code void ncl_raid_init_reg(void)
{
	u32 sram_blk_sz;
	u32 ddr_blk_sz;
	u32 ddr_start;
	u32 ddr_sz;

	raid_cfg_blk_reg_t blk_reg;
	raid_cfg_sram_reg_t sram_reg;
	raid_cfg_ddr_st_addr_reg_t ddr_st_reg;
	raid_cfg_ddr_end_addr_reg_t ddr_end_reg;
	raid_cfg_ddr_reg_t ddr_reg;

	set_du_fmt();
	sram_blk_sz = (get_blk_size(BUF_TYPE_SRAM) >> 4) + (0x438-0x410) + 32;
	ddr_blk_sz = (get_blk_size(BUF_TYPE_DDR) >> 5) + (0x438-0x410) +32;

	sram_blk_sz = 1072;//1072 << 4 = 17152B;
	ddr_blk_sz = 536;//536 << 5 = 17152B;

	ddr_sz = (TTL_RAID_BUF - RAID_BUF_PER_BANK * RAID_BANK_NUM + 1) * ddr_blk_sz;
	ddr_start = ddr_dtag_register(occupied_by(ddr_sz, DTAG_SZE));
	ddr_start = ddtag2mem(ddr_start) >> 5; // 32B aligned

	ncl_raid_trace(LOG_ERR, 0xe065, "ddr sz: 0x%x, ddr start: 0x%x\n", ddr_sz, ddr_start);

	blk_reg.all = raid_top_readl(RAID_CFG_BLK_REG);
	blk_reg.b.raid_cfg_total_blk_num = TTL_RAID_BUF;
	raid_top_writel(blk_reg.all, RAID_CFG_BLK_REG);

	sram_reg.all = raid_top_readl(RAID_CFG_SRAM_REG);
	sram_reg.b.raid_cfg_sram_blk_size = sram_blk_sz;
	sram_reg.b.raid_cfg_sram_bank_num = RAID_BANK_NUM;
	sram_reg.b.raid_cfg_sram_blk_num_per_bank = RAID_BUF_PER_BANK;
	raid_top_writel(sram_reg.all, RAID_CFG_SRAM_REG);

	ddr_st_reg.all = raid_top_readl(RAID_CFG_DDR_ST_ADDR_REG);
	ddr_st_reg.b.raid_cfg_ddr_st_addr = ddr_start;
	raid_top_writel(ddr_st_reg.all, RAID_CFG_DDR_ST_ADDR_REG);

	ddr_end_reg.all = raid_top_readl(RAID_CFG_DDR_END_ADDR_REG);
	ddr_end_reg.b.raid_cfg_ddr_end_addr = ddr_start + ddr_sz;
	raid_top_writel(ddr_end_reg.all, RAID_CFG_DDR_END_ADDR_REG);

	ddr_reg.all = raid_top_readl(RAID_CFG_DDR_REG);
	ddr_reg.b.raid_cfg_ddr_addr_round_sel = 0; // 32B aligned
	ddr_reg.b.raid_cfg_ddr_blk_size = ddr_blk_sz;
	raid_top_writel(ddr_reg.all, RAID_CFG_DDR_REG);

#if 0
	//mask all raid err in T0
	raid_top_writel(~0, RAID_ENC_XOR_FAULT_MASK_REG);
	raid_top_writel(~0, RAID_DEC_XOR_FAULT_MASK_REG);
	raid_top_writel(~0, RAID_SUS_SWITCH_FAULT_MASK_REG);
	raid_top_writel(~0, RAID_RES_SWITCH_FAULT_MASK_REG);
#endif

    	raid_ctrl_reg0_t ctrl_reg;
    	ctrl_reg.all = ncb_top_wrap_readl(RAID_CTRL_REG0);
   	ctrl_reg.all |= 1<<28;
   	ncb_top_wrap_writel(ctrl_reg.all,RAID_CTRL_REG0);
}

fast_code void ncl_raid_set_cmpl_para(u32 raid_id, u32 du_cnt, bool add, bool last)
{
	raid_compl_ctrl_reg1_t compl_ctrl_reg1;

	ncl_raid_trace(LOG_DEBUG, 0xa99a, "id %d du_cnt %d", raid_id, du_cnt);

	compl_ctrl_reg1.all = 0;
	compl_ctrl_reg1.b.w_du_cnt = du_cnt;
	compl_ctrl_reg1.b.raid_id_sel = raid_id;
	compl_ctrl_reg1.b.add_num_ops = add;
	compl_ctrl_reg1.b.set_last_flag = last;
	ncb_top_wrap_writel(compl_ctrl_reg1.all, RAID_COMPL_CTRL_REG1);
}

fast_code void ncl_raid_clr_last_flag(u32 raid_id)
{
	raid_compl_ctrl_reg1_t compl_ctrl_reg1;

	compl_ctrl_reg1.all = 0;
	compl_ctrl_reg1.b.clear_last_flag = true;
	compl_ctrl_reg1.b.raid_id_sel = raid_id;
	ncb_top_wrap_writel(compl_ctrl_reg1.all, RAID_COMPL_CTRL_REG1);
}

fast_code void ncl_raid_id_reset(u32 raid_id)
{
	raid_compl_ctrl_reg1_t compl_ctrl_reg1;

	compl_ctrl_reg1.all = 0;
	compl_ctrl_reg1.b.reset_ops = true;
	compl_ctrl_reg1.b.raid_id_sel = raid_id;
	ncb_top_wrap_writel(compl_ctrl_reg1.all, RAID_COMPL_CTRL_REG1);
}

fast_code void ncl_raid_set_intl_cnt(u32 raid_id, u32 du_cnt)
{
	raid_compl_ctrl_reg1_t compl_ctrl_reg1;

	ncl_raid_trace(LOG_DEBUG, 0xe97d, "direct set id %d internal cnt %d", raid_id, du_cnt);

	compl_ctrl_reg1.all = 0;
	compl_ctrl_reg1.b.w_du_cnt = du_cnt;
	compl_ctrl_reg1.b.raid_id_sel = raid_id;
	compl_ctrl_reg1.b.direct_set_internal_cnt = 1;
	ncb_top_wrap_writel(compl_ctrl_reg1.all, RAID_COMPL_CTRL_REG1);
}

/*!
 * @brief FICU RAID ID XOR DONE or ERR interrupt handling
 *
 * @return Not used
 */
fast_code void ncl_raid_cmpl_isr(void)
{
#if XOR_CMPL_BY_CMPL_CTRL
	u8 stripe_id;
	u8 wptr, rptr;
	raid_cmpl_entry_t *cmpl_entry;

	raid_top_wrap_int_status_reg_t raid_top_wrap_int_status;
	raid_top_wrap_int_status.all = ncb_top_wrap_readl(RAID_TOP_WRAP_INT_STATUS_REG);

	if (raid_top_wrap_int_status.b.raid_compl_int_status) {
		raid_compl_que_ptrs_t raid_compl_que_ptrs;
		raid_compl_que_ptrs.all = ncb_top_wrap_readl(RAID_COMPL_QUE_PTRS);

		rptr = raid_compl_que_ptrs.b.raid_compl_que_rd_ptr;
		wptr = raid_compl_que_ptrs.b.raid_compl_que_wr_ptr;
		sys_assert(wptr < TTL_RAID_ID);

		while (rptr != wptr) {
			cmpl_entry = &raid_cmpl_que[rptr];
            if(cmpl_entry->raid_cmd != XOR_W_DATA){
                ncl_raid_trace(LOG_ALW, 0xe9b1, "cmpl_entry->raid_cmd %u",cmpl_entry->raid_cmd);
            }
			sys_assert(cmpl_entry->raid_cmd == XOR_W_DATA || cmpl_entry->raid_cmd == XOR_WO_DATA || cmpl_entry->raid_cmd == POUT_W_DATA);

			stripe_id = stripe_lut[cmpl_entry->raid_id];
			sys_assert(stripe_left_cmpl[stripe_id]);
			if (--stripe_left_cmpl[stripe_id] == 0)
				evt_set_imt(evt_xor_cmpl, stripe_id, 1);

			rptr = (rptr + 1) & (TTL_RAID_ID - 1);
		}

		sys_assert(rptr == wptr);
		raid_compl_que_ptrs.b.raid_compl_que_rd_ptr = rptr;
		ncb_top_wrap_writel(raid_compl_que_ptrs.all, RAID_COMPL_QUE_PTRS);
	}

	if (raid_top_wrap_int_status.b.raid_compl_invalid_add_ops_status)
		panic("impossible");
#endif
}

fast_code void ncl_raid_status_save(void)
{
	raid_pwr_ctrl_reg_t raid_pwr_ctrl;

	raid_pwr_ctrl.all = raid_top_readl(RAID_PWR_CTRL_REG);
	raid_pwr_ctrl.b.raid_status_save_en = 1;
	raid_top_writel(raid_pwr_ctrl.all, RAID_PWR_CTRL_REG);

	//wait status save done
	while (raid_pwr_ctrl.b.raid_status_save_en) {
		raid_pwr_ctrl.all = raid_top_readl(RAID_PWR_CTRL_REG);
	}
}

fast_code void ncl_raid_status_restore(void)
{
	raid_pwr_ctrl_reg_t raid_pwr_ctrl;

	raid_pwr_ctrl.all = raid_top_readl(RAID_PWR_CTRL_REG);
	raid_pwr_ctrl.b.raid_status_restore_en = 1;
	raid_top_writel(raid_pwr_ctrl.all, RAID_PWR_CTRL_REG);

	//wait status restore done
	while (raid_pwr_ctrl.b.raid_status_restore_en) {
		raid_pwr_ctrl.all = raid_top_readl(RAID_PWR_CTRL_REG);
	}
}

init_code void ncl_raid_cmpl_init(void)
{
	//enable raid cmpl
	raid_compl_ctrl_reg0_t compl_ctrl_reg0;
	compl_ctrl_reg0.all = ncb_top_wrap_readl(RAID_COMPL_CTRL_REG0);
	compl_ctrl_reg0.b.raid_compl_ctrl_en = 1;
	ncb_top_wrap_writel(compl_ctrl_reg0.all, RAID_COMPL_CTRL_REG0);

	//init cmpl que
	raid_compl_que_base_addr_t raid_compl_que_base_addr;
	raid_compl_que_base_addr.all = ncb_top_wrap_readl(RAID_COMPL_QUE_BASE_ADDR);
	raid_compl_que_base_addr.b.raid_compl_que_base_addr = (u32)ptr2busmem(raid_cmpl_que);
	raid_compl_que_base_addr.b.raid_compl_que_entry_max_sz = TTL_RAID_ID - 1; // 0 based
	ncb_top_wrap_writel(raid_compl_que_base_addr.all, RAID_COMPL_QUE_BASE_ADDR);

	raid_compl_que_ptrs_t raid_compl_que_ptrs;
	raid_compl_que_ptrs.all = ncb_top_wrap_readl(RAID_COMPL_QUE_PTRS);
	raid_compl_que_ptrs.b.raid_compl_que_wr_ptr = 0;
	raid_compl_que_ptrs.b.raid_compl_que_rd_ptr = 0;
	ncb_top_wrap_writel(raid_compl_que_ptrs.all, RAID_COMPL_QUE_PTRS);

	raid_top_wrap_int_en_reg_t raid_top_wrap_int_en_reg;
	raid_top_wrap_int_en_reg.all = ncb_top_wrap_readl(RAID_TOP_WRAP_INT_EN_REG);
	raid_top_wrap_int_en_reg.b.raid_compl_invalid_add_ops_int_en = 1;
	raid_top_wrap_int_en_reg.b.raid_compl_int_en = 1;
	ncb_top_wrap_writel(raid_top_wrap_int_en_reg.all, RAID_TOP_WRAP_INT_EN_REG);

	raid_top_wrap_int_mask_reg_t raid_top_wrap_int_mask_reg;
	raid_top_wrap_int_mask_reg.all = ~0;
	raid_top_wrap_int_mask_reg.all &= ~(0x3 << ((CPU_ID - 1) * 8));
	ncb_top_wrap_writel(raid_top_wrap_int_mask_reg.all, RAID_TOP_WRAP_INT_MASK_REG);

	u32 i;
	raid_compl_ctrl_reg1_t compl_ctrl_reg1;
	//reset all raid id cmpl para
	for (i = 0; i < TTL_RAID_ID; i++) {
		compl_ctrl_reg1.all = 0;
		compl_ctrl_reg1.b.raid_id_sel = i;
		compl_ctrl_reg1.b.reset_ops = true;
		ncb_top_wrap_writel(compl_ctrl_reg1.all, RAID_COMPL_CTRL_REG1);
	}

	//register isr and enable
	poll_irq_register(VID_NCB1_INT, ncl_raid_cmpl_isr);
#if XOR_CMPL_BY_CMPL_CTRL
	poll_mask(VID_NCB1_INT);
#endif
}

init_code void ncl_raid_init(void)
{
	ncl_raid_reset();
	raid_sram_init();
	ncl_raid_enable();
	ncl_raid_init_reg();
//#if !XOR_CMPL_BY_PDONE
	ncl_raid_cmpl_init();
//#endif
	ficu_mode_disable();
	ficu_mode_enable();
}

fast_code buf_info_t get_raid_buf_info(u32 raid_id)
{
	raid_dbg_id_sel_t reg;
	buf_info_t buf_info;

	reg.all = raid_top_readl(RAID_DBG_REG);
	reg.b.raid_sel_raid_id = raid_id;
	reg.b.raid_sel_3 = 0x3;
	reg.b.raid_sel_7 = 0x7;
	raid_top_writel(reg.all, RAID_DBG_REG);

	reg.all = raid_top_readl(RAID_DBG_REG);
	buf_info.buf_idx = (u32)(reg.b.raid_status_sram_buffer_id);
	buf_info.bank = (u32)(reg.b.raid_status_sram_bank_id);

	return buf_info;
}

#if 1
ddr_code void *get_raid_buf_du_addr(u8 buf_type, u8 bank, u8 buf_idx, u8 du_idx)
{
	u32 blk_sz = 17280;//get_blk_size(buf_type);
	u32 du_sz = blk_sz >> ctz(DU_CNT_PER_PAGE);
	u32 ddr_base = raid_top_readl(RAID_CFG_DDR_ST_ADDR_REG);
	u32 addr_base = (buf_type == BUF_TYPE_SRAM) ? RAID_BASE : ddr_base;

	return (void *)(addr_base + RAID_BANK_SIZE * bank + buf_idx * blk_sz + du_sz * du_idx);
}

ddr_code void raid_id_dump(u32 raid_id)
{
	raid_compl_ctrl_reg1_t compl_ctrl_reg1;
	compl_ctrl_reg1.all = ncb_top_wrap_readl(RAID_COMPL_CTRL_REG1);
	compl_ctrl_reg1.b.raid_id_sel = raid_id;
	ncb_top_wrap_writel(compl_ctrl_reg1.all, RAID_COMPL_CTRL_REG1);

	raid_compl_status_reg0_t raid_compl_status_reg0;
	raid_compl_status_reg0.all = ncb_top_wrap_readl(RAID_COMPL_STATUS_REG0);

	ncl_raid_trace(LOG_ALW, 0x755f, "raid_id %u busy %d last %d expected %d internal %d",
		raid_id,
		raid_compl_status_reg0.b.busy_flag,
		raid_compl_status_reg0.b.raid_id_last_flag,
		raid_compl_status_reg0.b.expected_du_num_val,
		raid_compl_status_reg0.b.completed_du_cnt_val);
}

static ddr_code int raid_id_dump_main(int argc, char *argv[])
{
    ncl_raid_trace(LOG_ALW, 0x1144, "======");
    for(u32 i = 0; i < (RAID_BUF_PER_BANK * RAID_BANK_NUM); i ++){
        raid_id_dump(i);
    }
    ncl_raid_trace(LOG_ALW, 0x01b2, "======");
	return 0;
}

static DEFINE_UART_CMD(raid_id_dump, "raid_id_dump", "dump raid id xor info",
			"raid_id_dump [id]", 0, 0, raid_id_dump_main);
#endif
/*! @} */
