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

/*! \file nand_cache_prog_test.c
 * @brief test nand cache program function
 *
 * \addtogroup test
 * \defgroup nand_cache_prog_test
 * \ingroup test
 *
 * @{
 */

#include "eccu.h"
#include "ncl.h"
#include "ncl_err.h"
#include "nand.h"
#include "ndcu.h"
#include "ficu.h"
#include "ncl_cmd.h"
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "nand_define.h"
/*! \cond PRIVATE */
#define __FILEID__ nandcachept
#include "trace.h"
/*! \endcond */

#if NAND_TEST
// This file test nand cache read of corner cases, and FW may need change based on the result

extern struct ncl_cmd_t* ncl_cmd_alloc(u8 op_code, u32 cnt);
extern void ncl_cmd_free(struct ncl_cmd_t *ncl_cmd);
extern void ncl_tasks_clear(void);
extern u32 fcmd_outstanding_cnt;

static u32 slc_spb_id = 0;

slow_code void ncl_cmd_success_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
		sys_assert(0);
	}
	ncl_cmd_free(ncl_cmd);
}

slow_code bool cache_prog_test_switch_block(void)
{
	u32 spb_id;
	pda_t* pda_list;
	u32 pg;
	struct ncl_cmd_t* ncl_cmd;
	u32 pg_per_blk;
	u32 loop;

	pg_per_blk = nand_page_num_slc();
	spb_id = slc_spb_id;
	for (loop = 0; loop < 2; loop++) {
		// Erase SLC SPB
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, 1);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		pda_list[0] = ((spb_id + loop) << nand_pda_block_shift());
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);

		// Program whole SPB
		for (pg = 0; pg < pg_per_blk; pg++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, 1);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			pda_list[0] = ((spb_id + loop) << nand_pda_block_shift()) + (pg << nand_info.pda_page_shift);
			ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_CACHE_PROGRAM_FLAG;
			ncl_cmd->du_format_no = DU_FMT_USER_4K;
			ncl_cmd->completion = ncl_cmd_success_cmpl;
			ncl_cmd_submit(ncl_cmd);
		}
	}


	for (loop = 0; loop < 2; loop++) {
		for (pg = 0; pg < pg_per_blk; pg++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, 1);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			pda_list[0] = ((spb_id + loop) << nand_pda_block_shift()) + (pg << nand_info.pda_page_shift);
			ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_SLC;
			ncl_cmd->du_format_no = DU_FMT_USER_4K;
			ncl_cmd->completion = ncl_cmd_success_cmpl;
			ncl_cmd_submit(ncl_cmd);
		}
	}
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	return true;
}

slow_code bool cache_prog_test_mix_with_read(void)
{
	u32 spb_id;
	pda_t* pda_list;
	u32 pg, step = 4, i;
	struct ncl_cmd_t* ncl_cmd;
	u32 pg_per_blk;

	pg_per_blk = nand_page_num_slc();
	spb_id = slc_spb_id;
	// Erase SLC SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, 1);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = (spb_id << nand_pda_block_shift());
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd->completion = ncl_cmd_success_cmpl;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	for (pg = 0; pg < pg_per_blk; pg+=step) {
		for (i = 0; i < step; i++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, 1);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			pda_list[0] = (spb_id << nand_pda_block_shift()) + ((pg + i) << nand_info.pda_page_shift);
			ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_CACHE_PROGRAM_FLAG;
			ncl_cmd->du_format_no = DU_FMT_USER_4K;
			ncl_cmd->completion = ncl_cmd_success_cmpl;
			ncl_cmd_submit(ncl_cmd);
		}


		for (i = 0; i < step; i++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, 1);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			pda_list[0] = (spb_id << nand_pda_block_shift()) + ((pg + i) << nand_info.pda_page_shift);
			ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_SLC;
			ncl_cmd->du_format_no = DU_FMT_USER_4K;
			ncl_cmd->completion = ncl_cmd_success_cmpl;
			ncl_cmd_submit(ncl_cmd);
		}
	}
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	return true;
}

slow_code void nand_cache_program_tPROG_measure(void)
{
	u32 tR[2], tCR[2];
	nf_pcmd_reg10_t pcmd, pcmd_bak;

	sys_assert(ncmd_fmt_array[FINST_NAND_FMT_SLC_PROG].fmt.b.rd_scmd_mode == NAND_FMT_RD_SCMD_SBYTE0);

	extern ncb_ndcu_rdy_fail_regs_t ncb_ndcu_rdy_fail_regs;
	sys_assert(ncb_ndcu_rdy_fail_regs.nf_rdy_reg0.b.rdy_bit_chk0 == RDY_BIT(6));

	extern u32 _ncl_get_tPROG(pda_t pda, u32 pb_type, u32 step);
	tR[0] = _ncl_get_tPROG(slc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_SLC, 0);
	//tR[1] = _ncl_get_tPROG(xlc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_XLC);
	pcmd_bak.all = pcmd.all = ndcu_readl(NF_PCMD_REG10);
	pcmd.b.pcmd9 = 0x15;
	ndcu_writel(pcmd.all, NF_PCMD_REG10);
	tCR[0] = _ncl_get_tPROG(slc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_SLC, 0);
	//tCR[1] = _ncl_get_tPROG(xlc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_XLC);
	ndcu_writel(pcmd_bak.all, NF_PCMD_REG10);
	ncl_test_trace(LOG_ERR, 0x938b, "time (SLC) %d V.S. %dus\n", tR[0]/(SYS_CLK/1000000), tCR[0]/(SYS_CLK/1000000));
	//ncl_test_trace(LOG_ERR, 0, "time (XLC) %d V.S. %dus\n", tR[1]/(SYS_CLK/1000000), tCR[1]/(SYS_CLK/1000000));
}

slow_code bool nand_cache_prog_test(void)
{
	bool ret = true;

	sys_assert(POLL_CACHE_PROG);
	sys_assert(!HAVE_CACHE_READ);

	ret = cache_prog_test_switch_block();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0xf670, "Cache program switch block fail!\n");
		return ret;
	}

	ret = cache_prog_test_mix_with_read();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0x02b9, "Cache program switch to read fail!!\n");
		return ret;
	}

	ncl_test_trace(LOG_ERR, 0x9d95, "Cache program OK!\n");

	nand_cache_program_tPROG_measure();

	return ret;
}
#endif

/*! @} */
