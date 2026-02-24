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

/*! \file nand_cache_read_test.c
 * @brief test nand cache read function
 *
 * \addtogroup test
 * \defgroup nand_cache_read_test
 * \ingroup test
 *
 * @{
 */
#include "eccu.h"
#include "ncl.h"
#include "ncl_err.h"
#include "nand.h"
#include "ficu.h"
#include "ncl_cmd.h"
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
/*! \cond PRIVATE */
#define __FILEID__ nandcachert
#include "trace.h"
/*! \endcond */

#if NAND_TEST
// This file test nand cache read of corner cases, and FW may need change based on the result

extern struct ncl_cmd_t* ncl_cmd_alloc(u8 op_code, u32 cnt);
extern void ncl_cmd_free(struct ncl_cmd_t *ncl_cmd);
extern void ncl_tasks_clear(void);
extern u32 fcmd_outstanding_cnt;

static u32 slc_spb_id = 0;
static u32 xlc_spb_id = 1;

slow_code void ncl_cmd_success_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
		sys_assert(0);
	}
	ncl_cmd_free(ncl_cmd);
}

slow_code bool cache_read_test_prepare(void)
{
	u32 spb_id;
	pda_t* pda_list;
	u32 pg;
	struct ncl_cmd_t* ncl_cmd;
	u32 i;
	u32 mp_num = nand_plane_num();
	u32 pg_per_blk;
	u32 loop;

	for (loop = 0; loop < 2; loop++) {
		if (loop == 0) {
			pg_per_blk = nand_page_num_slc();
			spb_id = slc_spb_id;
		} else {
			pg_per_blk = nand_page_num();
			spb_id = xlc_spb_id;
		}
		// Erase SLC SPB
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, mp_num);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < mp_num; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + i * DU_CNT_PER_PAGE;
		}
		if (loop == 0) {
			ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		} else {
			ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
		}
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);

		// Program whole SPB
		for (pg = 0; pg < pg_per_blk; pg++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, mp_num);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			for (i = 0; i < mp_num; i++) {
				pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg << nand_info.pda_page_shift) + i * DU_CNT_PER_PAGE;
			}
			if (loop == 0) {
				ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
			} else {
				ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
			}
			ncl_cmd->du_format_no = DU_FMT_USER_4K;
			ncl_cmd->completion = ncl_cmd_success_cmpl;
			ncl_cmd_submit(ncl_cmd);
		}

		while(fcmd_outstanding_cnt != 0) {
			ficu_done_wait();
		}
	}

	return true;
}

slow_code bool cache_read_test_slc_and_xlc(void)
{
	struct ncl_cmd_t* ncl_cmd;
	pda_t* pda_list;
	u32 loop;

	for (loop = 0; loop < 2; loop++) {
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, 1);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		if (loop == 0) {
			pda_list[0] = (slc_spb_id << nand_pda_block_shift());
			ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_SLC;
		} else {
			pda_list[0] = (xlc_spb_id << nand_pda_block_shift());
			ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_XLC;
		}
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);
	}

	ncl_tasks_clear();
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	return true;
}

slow_code bool cache_read_test_xlc_and_slc(void)
{
	struct ncl_cmd_t* ncl_cmd;
	pda_t* pda_list;
	u32 loop;

	for (loop = 0; loop < 2; loop++) {
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, 1);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		if (loop == 0) {
			pda_list[0] = (xlc_spb_id << nand_pda_block_shift());
			ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_XLC;
		} else {
			pda_list[0] = (slc_spb_id << nand_pda_block_shift());
			ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_SLC;
		}
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);
	}

	ncl_tasks_clear();
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	return true;
}

slow_code bool cache_read_test_switch_plane(void)
{
	struct ncl_cmd_t* ncl_cmd;
	pda_t* pda_list;
	u32 loop;

	for (loop = 0; loop < nand_plane_num(); loop++) {
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, 1);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		pda_list[0] = (slc_spb_id << nand_pda_block_shift()) + loop * DU_CNT_PER_PAGE;
		ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_SLC;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);
	}

	ncl_tasks_clear();
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	return true;
}

slow_code bool cache_read_test_sp_and_mp(void)
{
	struct ncl_cmd_t* ncl_cmd;
	pda_t* pda_list;
	u32 loop;
	u32 list_len;
	u32 i;

	for (loop = 0; loop < 2; loop++) {
		if (loop == 0) {
			list_len = 1;
		} else {
			list_len = nand_plane_num() * DU_CNT_PER_PAGE;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i <list_len; i++) {
			pda_list[i] = (slc_spb_id << nand_pda_block_shift()) + (loop << nand_info.pda_page_shift) + i;
			ncl_cmd->addr_param.common_param.info_list[i].pb_type = NAL_PB_TYPE_SLC;
		}
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);
	}

	ncl_tasks_clear();
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	return true;
}

slow_code bool cache_read_test_mp_and_sp(void)
{
	struct ncl_cmd_t* ncl_cmd;
	pda_t* pda_list;
	u32 loop;
	u32 list_len;
	u32 i;

	for (loop = 0; loop < 2; loop++) {
		if (loop == 0) {
			list_len = nand_plane_num() * DU_CNT_PER_PAGE;
		} else {
			list_len = 1;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i <list_len; i++) {
			pda_list[i] = (slc_spb_id << nand_pda_block_shift()) + (loop << nand_info.pda_page_shift) + i;
			ncl_cmd->addr_param.common_param.info_list[i].pb_type = NAL_PB_TYPE_SLC;
		}
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);
	}

	ncl_tasks_clear();
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	return true;
}

slow_code bool cache_read_test_same_page(void)
{
	struct ncl_cmd_t* ncl_cmd;
	pda_t* pda_list;
	u32 loop;

	for (loop = 0; loop < 2; loop++) {
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, 1);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		pda_list[0] = (slc_spb_id << nand_pda_block_shift());
		ncl_cmd->addr_param.common_param.info_list[0].pb_type = NAL_PB_TYPE_SLC;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = ncl_cmd_success_cmpl;
		ncl_cmd_submit(ncl_cmd);
	}

	ncl_tasks_clear();
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	return true;
}

slow_code void nand_cache_read_tR_measure(void)
{
	u32 tR[2], tCR[2];
	nf_rcmd_reg10_t rcmd, rcmd_bak;
	extern u32 _ncl_get_tR(pda_t pda, u32 pb_type);
	tR[0] = _ncl_get_tR(slc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_SLC);
	tR[1] = _ncl_get_tR(xlc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_XLC);
	rcmd_bak.all = rcmd.all = ndcu_readl(NF_RCMD_REG10);
	rcmd.b.rcmd9 = 0x31;
	ndcu_writel(rcmd.all, NF_RCMD_REG10);
	tCR[0] = _ncl_get_tR(slc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_SLC);
	tCR[1] = _ncl_get_tR(xlc_spb_id << nand_pda_block_shift(), NAL_PB_TYPE_XLC);
	ndcu_writel(rcmd_bak.all, NF_RCMD_REG10);
	ncl_test_trace(LOG_ERR, 0x6b39, "time (SLC) %d V.S. %dus\n", tR[0]/(SYS_CLK/1000000), tCR[0]/(SYS_CLK/1000000));
	ncl_test_trace(LOG_ERR, 0xbbf6, "time (XLC) %d V.S. %dus\n", tR[1]/(SYS_CLK/1000000), tCR[1]/(SYS_CLK/1000000));
}

slow_code bool nand_cache_read_test(void)
{
	bool ret = true;

	sys_assert(HAVE_CACHE_READ);

	ret = cache_read_test_prepare();
	if (!ret)
		return ret;

	ret = cache_read_test_slc_and_xlc();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0xc661, "Cache read SLC & XLC fail!\n");
		return ret;
	}

	ret = cache_read_test_xlc_and_slc();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0x6f15, "Cache read XLC & SLC fail!\n");
		return ret;
	}

	ret = cache_read_test_switch_plane();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0x6f9d, "Cache read switch plane fail!\n");
		return ret;
	}

	ret = cache_read_test_sp_and_mp();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0xdf24, "Cache read SP & MP fail!\n");
		return ret;
	}

	ret = cache_read_test_mp_and_sp();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0xcdbe, "Cache read MP & SP fail!\n");
		return ret;
	}
	ret = cache_read_test_same_page();
	if (!ret) {
		ncl_test_trace(LOG_ERR, 0x6d3a, "Cache read same page fail!\n");
		return ret;
	}

	ncl_test_trace(LOG_ERR, 0x126b, "Cache read OK!\n");

	nand_cache_read_tR_measure();

	return ret;
}
#endif

/*! @} */
