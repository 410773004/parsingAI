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

/*! \file ncl_test.c
 * @brief ncl general testing function for nand
 *
 * \addtogroup test
 * \defgroup ncl_test
 * \ingroup test
 *
 * @{
 */

#include "eccu.h"
#include "ncl.h"
#include "ncl_err.h"
#include "ndcu.h"
#include "nand.h"
#include "ficu.h"
#include "ncl_cmd.h"
#include "eccu_mdec_register.h"
#include "eccu_pdec_register.h"
#include "eccu_reg_access.h"
#include "bf_mgr.h"

/*! \cond PRIVATE */
#define __FILEID__ ncltest
#include "trace.h"
/*! \endcond */
extern u32 fw_ard_cnt;

#if 0
u32 get_du_idx(u32 ch, u32 ce, u32 lun, u32 pl, u32 pg)
{
	u32 interleave;

	interleave = lun;
	interleave *= nand_target_num();
	interleave |= ce;
	interleave *= nand_channel_num();
	interleave |= ch;
	interleave *= nand_plane_num();
	interleave |= pl;
	return (pg * nand_interleave_num() + interleave) * DU_CNT_PER_PAGE;
}

slow_code void test_erase_all_nand(void)
{
	u32 spb_id, i;
	struct ncl_cmd_t ncl_cmd;
	pda_t* pda_list;

	ncl_test_trace(LOG_ERR, 0x64aa, "erase all block\n");

	pda_list = sys_malloc(SLOW_DATA, sizeof(pda_t) * nand_interleave_num());
	if (pda_list == NULL) {
		ncl_test_trace(LOG_ERR, 0x1e4c, "no mem\n");
		return;
	}


	ncl_cmd.op_code = NCL_CMD_OP_ERASE;
	ncl_cmd.addr_param.common_param.list_len = nand_interleave_num();
	ncl_cmd.addr_param.common_param.pda_list = pda_list;
	ncl_cmd.completion = NULL;
	for (spb_id = 8; spb_id < 9; spb_id++) {
		for (i = 0; i < nand_interleave_num(); i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + i * DU_CNT_PER_PAGE;
		}
		ncl_cmd_submit(&ncl_cmd);
	}
	sys_free(SLOW_DATA, pda_list);
	ncl_test_trace(LOG_ERR, 0xfef5, "erase end\n");
}

slow_code void ncl_block_copyback(u32 slc_spb, u32 tlc_spb)
{
	u32 pl, loop, idx, pg, tlc_pg;
	struct ncl_cmd_t ncl_cmd;
	u32 cnt = 16;// Max 4 plane * Qlc
	pda_t* pda_slc_list;
	pda_t* pda_tlc_list;
	struct info_param_t* info_list;
	struct xlc_cb_addr_t* cb_addr;

	pda_slc_list = sys_malloc(SLOW_DATA, (sizeof(pda_t) * 2 + sizeof(struct info_param_t)) * cnt + sizeof(struct xlc_cb_addr_t));
	if (pda_slc_list == NULL) {
		ncl_test_trace(LOG_ERR, 0x3502, "no mem\n");
		return;
	}
	pda_tlc_list = pda_slc_list + cnt;
	info_list = (struct info_param_t*)(pda_tlc_list + cnt);
	cb_addr = (struct xlc_cb_addr_t*)(info_list + cnt);
	cb_addr->slc_list_src = pda_slc_list;
	cb_addr->tlc_list_dst = pda_tlc_list;
	cb_addr->data_tag_count = 0;
	cb_addr->width = 1;


	ncl_cmd.op_code = NCL_CMD_OP_CB_MODE0;
	ncl_cmd.addr_param.cb_param.list_len = nand_bits_per_cell()*1;
	ncl_cmd.addr_param.cb_param.addr = cb_addr;
	ncl_cmd.addr_param.cb_param.info_list = info_list;
	ncl_cmd.completion = NULL;

	for (pg = 0; pg < nand_page_num()/nand_bits_per_cell(); pg++) {
		// Update PDA list
		idx = 0;
		for (loop = 0; loop < nand_bits_per_cell(); loop++) {
			tlc_pg = pg * nand_bits_per_cell() + loop;
			for (pl = 0; pl < 1; pl++) {
				pda_slc_list[idx] = (slc_spb << nand_pda_block_shift()) + DU_CNT_PER_PAGE * (pl + tlc_pg* nand_interleave_num());
				pda_tlc_list[idx] = (tlc_spb << nand_pda_block_shift()) + DU_CNT_PER_PAGE * (pl + tlc_pg*nand_interleave_num());
				idx++;
			}
		}
		for (loop = 0; loop < ncl_cmd.addr_param.cb_param.list_len; loop++) {
			//ncl_test_trace(LOG_ERR, 0, "pda %x %x\n", pda_slc_list[loop], pda_tlc_list[loop]);
		}
		ncl_cmd_submit(&ncl_cmd);
	}
	sys_free(SLOW_DATA, pda_slc_list);
}

#include "nand_cfg.h"

void ncl_slc2tlc_copyback_test(void)
{

	ncl_test_trace(LOG_ERR, 0x1a8a, "Prepare SLC block\n");
	ncl_block_erase(0x50000, NAL_PB_TYPE_SLC);
	ncl_block_write(0x50000, NAL_PB_TYPE_SLC);
	//ncl_block_read(0x50000, NAL_PB_TYPE_SLC);

	ncl_test_trace(LOG_ERR, 0x1bc8, "Erase TLC block\n");
	ncl_block_erase(0x60000, NAL_PB_TYPE_XLC);

	ncl_test_trace(LOG_ERR, 0x67ad, "Copyback SLC -> TLC\n");
	ncl_block_copyback(5, 6);
	ncl_block_read(0x60000, NAL_PB_TYPE_XLC);
}
#endif

void ncl_cmd_free(struct ncl_cmd_t *ncl_cmd)
{
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;
	u32 dtag_cnt;
	bm_pl_t* bm_pl;

	switch(ncl_cmd->op_code) {
	case NCL_CMD_OP_READ:
    case NCL_CMD_PATROL_READ:
	case NCL_CMD_OP_READ_FW_ARD:
	case NCL_CMD_OP_READ_ERD:
		dtag_cnt = cnt;
		break;
	case NCL_CMD_OP_WRITE:
		dtag_cnt = cnt * DU_CNT_PER_PAGE;
		break;
	case NCL_CMD_OP_CB_MODE2:
		dtag_cnt = DU_CNT_PER_PAGE*nand_plane_num()*nand_channel_num();
		break;
	case NCL_CMD_OP_ERASE:
	case NCL_CMD_OP_CB_MODE0:
		dtag_cnt = 0;
		break;
	default:
		// TODO other op code
		sys_assert(ncl_cmd->op_code == NCL_CMD_OP_ERASE);
		dtag_cnt = 0;
		break;
	}


	if (dtag_cnt != 0) {
		bm_pl = ncl_cmd->user_tag_list;
		dtag_t* dtag_list = (dtag_t*)(bm_pl+dtag_cnt);
		dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
		sys_free(SLOW_DATA, bm_pl);
	}
	//ncl_test_trace(LOG_ERR, 0, "free cmd %x %x %d\n", ncl_cmd, bm_pl, dtag_cnt);
	sys_free(SLOW_DATA, ncl_cmd);
	//ncl_test_trace(LOG_ERR, 0, "ncl cmd %x cmpl\n", ncl_cmd);
}

/* Allocate resource that ncl cmd need */
struct ncl_cmd_t* ncl_cmd_alloc(u8 op_code, u32 cnt)
{
	pda_t* pda_list;
	pda_t* pda_list2 = NULL;
	struct xlc_cb_addr_t* cb_addr = NULL;
	struct info_param_t* info_list;
	struct ncl_cmd_t* ncl_cmd;
	dtag_t* dtag_list;
	bm_pl_t* bm_pl = NULL;
	u32 dtag_cnt;
	int flags;
	flags = irq_save();

	switch(op_code) {
	case NCL_CMD_OP_READ:
    case NCL_CMD_PATROL_READ:
	case NCL_CMD_OP_READ_FW_ARD:
	case NCL_CMD_OP_READ_ERD:
		dtag_cnt = cnt;
		break;
	case NCL_CMD_OP_WRITE:
		dtag_cnt = cnt * DU_CNT_PER_PAGE;
		break;
	case NCL_CMD_OP_CB_MODE0:
		dtag_cnt = 0;
		break;
	case NCL_CMD_OP_CB_MODE2:
		dtag_cnt = DU_CNT_PER_PAGE*nand_plane_num()*nand_channel_num();
		break;
	default:
		// TODO other op code
		sys_assert(op_code == NCL_CMD_OP_ERASE);
		dtag_cnt = 0;
		break;
	}

	if ((op_code != NCL_CMD_OP_CB_MODE0) && (op_code != NCL_CMD_OP_CB_MODE2)) {
		ncl_cmd = sys_malloc(SLOW_DATA, sizeof(struct ncl_cmd_t) + cnt * (sizeof(pda_t) + sizeof(struct info_param_t)));
		if (ncl_cmd == NULL) {
			irq_restore(flags);
			return NULL;
		}
		pda_list = (pda_t*)(ncl_cmd + 1);
		info_list = (struct info_param_t*)(pda_list+cnt);
	} else {
		ncl_cmd = sys_malloc(SLOW_DATA, sizeof(struct ncl_cmd_t) + cnt * (sizeof(pda_t) * 2 + sizeof(struct info_param_t)) + sizeof(struct xlc_cb_addr_t));
		if (ncl_cmd == NULL) {
			irq_restore(flags);
			return NULL;
		}
		pda_list = (pda_t*)(ncl_cmd + 1);
		pda_list2 = pda_list + cnt;
		info_list = (struct info_param_t*)(pda_list2 + cnt);
		cb_addr = (struct xlc_cb_addr_t*)(info_list + cnt);
		cb_addr->slc_list_src = pda_list;
		cb_addr->tlc_list_dst = pda_list2;
		cb_addr->data_tag_count = 0;
		cb_addr->width = cnt / XLC;
	}
	u32 i;
	for (i = 0; i < cnt; i++) {
		info_list[i].status = cur_du_good;
		info_list[i].xlc.slc_idx = 0;
		info_list[i].pb_type = NAL_PB_TYPE_SLC;
	}
	if (dtag_cnt != 0) {
		// Allocate dtag list and meta memory
		bm_pl = sys_malloc(SLOW_DATA, dtag_cnt * (sizeof(dtag_t) + sizeof(bm_pl_t) + 4));
		if (bm_pl == NULL) {
			sys_free(SLOW_DATA, ncl_cmd);
			irq_restore(flags);
			return NULL;
		}
		dtag_list = (dtag_t*)(bm_pl + dtag_cnt);
		// Allocate DTAGs
		u32 alloc_cnt = dtag_get_bulk(DTAG_T_SRAM, dtag_cnt, dtag_list);
		if (alloc_cnt != dtag_cnt) {
			sys_free(SLOW_DATA, bm_pl);
			sys_free(SLOW_DATA, ncl_cmd);
			dtag_put_bulk(DTAG_T_SRAM, alloc_cnt, dtag_list);
			irq_restore(flags);
			return NULL;
		}
		u32 i;
		for (i = 0; i < dtag_cnt; i++) {
			bm_pl[i].all = 0;
			//dtag_list[i].pl.btag = 0;
			bm_pl[i].pl.dtag = dtag_list[i].b.dtag;
			bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
		}
	}
	//ncl_test_trace(LOG_ERR, 0, "allo cmd %x %x %d\n", ncl_cmd, bm_pl, dtag_cnt);

	ncl_cmd->op_code = op_code;
	ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
	ncl_cmd->flags = 0;
	ncl_cmd->status = 0;
	if (op_code == NCL_CMD_OP_READ_ERD) {
		ncl_cmd->addr_param.rapid_du_param.list_len = cnt;
		ncl_cmd->addr_param.rapid_du_param.info.status = cur_du_good;
	} else if ((op_code != NCL_CMD_OP_CB_MODE0) && (op_code != NCL_CMD_OP_CB_MODE2)) {
		if (op_code == NCL_CMD_OP_READ || op_code == NCL_CMD_PATROL_READ) {
			ncl_cmd->flags |= NCL_CMD_META_DISCARD;
		}
		ncl_cmd->addr_param.common_param.list_len = cnt;
		ncl_cmd->addr_param.common_param.pda_list = pda_list;
		ncl_cmd->addr_param.common_param.info_list = info_list;
	} else {
		ncl_cmd->addr_param.cb_param.list_len = cnt;
		ncl_cmd->addr_param.cb_param.addr = cb_addr;
		ncl_cmd->addr_param.cb_param.info_list = info_list;
	}
	ncl_cmd->user_tag_list = bm_pl;
	ncl_cmd->completion = ncl_cmd_free;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	irq_restore(flags);
	return ncl_cmd;
}

/* Simple SLC SPB stress test
 * 1. Erase SPB
 * 2. Program SPB, each command write a interleave pages
 * 3. Read SPB, each command read a interleave pages
 */
bool self_stress_test_slc_simple(void)
{
	u32 spb_id = 8;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	bool ret = true;
	u32 max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;

	if (max_page_cnt > nand_interleave_num()) {
		max_page_cnt = nand_interleave_num();
	}
	cnt = nand_interleave_num();

	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = spb_id << nand_pda_block_shift();
	for (i = 1; i < cnt; i++) {
		pda_list[i] = pda_list[i - 1] + DU_CNT_PER_PAGE;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	for (pg = 0; pg < nand_page_num_slc() * nand_interleave_num(); ) {
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
		pg += cnt;
	}

	// Read whole SPB
	for (pg = 0; pg < nand_page_num_slc() * nand_interleave_num(); ) {
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd_submit(ncl_cmd);
		pg += cnt;
	}

	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	ncl_test_trace(LOG_ERR, 0x2628, "over\n");
	return ret;
}

u32 random_ptr = 0;
u32 random_generator(void)
{
	u32* ptr;
	u32 size;
	ptr = get_scr_seed_buf();
	size = get_scr_seed_size();
	size /= sizeof(u32);
	random_ptr++;
	if (random_ptr >= size) {
		random_ptr = 0;
	}
	return ptr[random_ptr];
}

// 1 read error, 2 read data error, 3 erase error, 4, program error
u32 stress_result = 0;
void stress_test_erase_check(struct ncl_cmd_t * ncl_cmd)
{
	u32 i;
	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
		if (stress_result == 0) {
			ncl_test_trace(LOG_ERR, 0xc0a4, "Erase error\n");
			for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
				ncl_test_trace(LOG_ERR, 0x9a0e, "%d: pda 0x%x status %d\n", i, ncl_cmd->addr_param.common_param.pda_list[i], ncl_cmd->addr_param.common_param.info_list[i].status);
			}
			stress_result = 3;
		}
	}
	ncl_cmd_free(ncl_cmd);
}

void stress_test_write_check(struct ncl_cmd_t * ncl_cmd)
{
	u32 i;
	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
		if (stress_result == 0) {
			ncl_test_trace(LOG_ERR, 0xf0ff, "Program error\n");
			for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
				ncl_test_trace(LOG_ERR, 0x38eb, "%d: pda 0x%x status %d\n", i, ncl_cmd->addr_param.common_param.pda_list[i], ncl_cmd->addr_param.common_param.info_list[i].status);
			}
			stress_result = 4;
		}
	}
	ncl_cmd_free(ncl_cmd);
}

void stress_test_read_data_verify(struct ncl_cmd_t * ncl_cmd)
{
	u32 i;
	pda_t * pda_list;

	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
		if (stress_result == 0) {
			ncl_test_trace(LOG_ERR, 0x46f1, "Read error\n");
			for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
				ncl_test_trace(LOG_ERR, 0x0bf8, "%d: pda 0x%x status %d\n", i, pda_list[i], ncl_cmd->addr_param.common_param.info_list[i].status);
			}
			stress_result = 1;
		}
	} else {
		for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
			u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
			if (*data != pda_list[i]) {
				if (stress_result == 0) {
					stress_result = 2;
					ncl_test_trace(LOG_ERR, 0x3403, "Read data error PDA 0x%x\n", pda_list[i], *data);
				}
				break;
			}
		}
	}
	ncl_cmd_free(ncl_cmd);
}

/* More complicated SLC SPB stress test
 * 1. Choose 2 SPBs
 * 2. Erase 1 SPB
 * 3. Random program or read random number pages
 * 4. When SPB program full, switch to the other SPB and ping-pong using the 2 SPBs
 */
void self_stress_test_slc_random(void)
{
	u32 round = 0;
	u32 spb_id[2] = {8, 9};
	u32 spb_vld_bmp = 0;// Valid bitmap
	u32 cur_spb_idx = 1;
	u32 cur_spb_wr_ptr;
	u32 op, opcode;
	u32 page_per_spb;
	u32 cnt;// program/read page count
	pda_t pda;
	u32 ptr;
	u32 max_page_cnt;
	max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;

	page_per_spb = nand_page_num_slc() * nand_interleave_num();
	cur_spb_wr_ptr = page_per_spb;

	while (1) {
		// Randomly decide operations
		op = random_generator() % 3;
		switch (op) {
		case 0:// Erase
			if (cur_spb_wr_ptr == page_per_spb) {// Switch SPB
				cur_spb_idx = !cur_spb_idx;
				cur_spb_wr_ptr = 0;
			} else {
				continue;
			}
			pda = spb_id[cur_spb_idx] << nand_pda_block_shift();
			cnt = nand_interleave_num();
			opcode = NCL_CMD_OP_ERASE;
			round++;
			ncl_test_trace(LOG_ERR, 0xddc1, "erase pda %x, cnt %d, loop %d\n", pda, cnt, round);
			break;
		case 1:// Program
			if (cur_spb_wr_ptr == page_per_spb) {
				// SPB full, cannot program
				continue;
			}
			cnt = random_generator();
			cnt %= nand_interleave_num();
			cnt++;
			if ((cnt + cur_spb_wr_ptr) > page_per_spb) {
				cnt = page_per_spb - cur_spb_wr_ptr;
			}
			if (cnt > max_page_cnt) {
				cnt = max_page_cnt;
			}
			spb_vld_bmp |= 1 << cur_spb_idx;
			pda = spb_id[cur_spb_idx] << nand_pda_block_shift();
			pda += cur_spb_wr_ptr * DU_CNT_PER_PAGE;
			//ncl_test_trace(LOG_ERR, 0, "program pda %x, cnt %d\n", pda, cnt);
			cur_spb_wr_ptr += cnt;
			opcode = NCL_CMD_OP_WRITE;
			break;
		case 2:// Read
			if (spb_vld_bmp == 0) {// Neither of the SPB has valid data
				continue;
			}
			u32 read_spb;
			u32 max_ptr;
			if (spb_vld_bmp == 3) {// Both of the SPB have valid data
				read_spb = !cur_spb_idx;
				max_ptr = page_per_spb;
			} else {// Only current SPB has valid data
				if (cur_spb_wr_ptr == 0) {
					read_spb = !cur_spb_idx;
					max_ptr = page_per_spb;
				} else {
					read_spb = cur_spb_idx;
					max_ptr = cur_spb_wr_ptr;
				}
			}
			ptr = random_generator() % max_ptr;
			cnt = (random_generator() % nand_interleave_num()) + 1;
			if (ptr + cnt > max_ptr) {
				cnt = max_ptr - ptr;
			}
			if (cnt > max_page_cnt) {
				cnt = max_page_cnt;
			}
			pda = spb_id[read_spb] << nand_pda_block_shift();
			pda += ptr * DU_CNT_PER_PAGE;
			cnt *= DU_CNT_PER_PAGE;
			opcode = NCL_CMD_OP_READ;
			//ncl_test_trace(LOG_ERR, 0, "read    pda %x, cnt %d\n", pda, cnt);
			break;
		}

		// Submit command
		struct ncl_cmd_t* ncl_cmd;
		pda_t* pda_list;
		u32 step;
		u32 i;
		while(1) {
			ncl_cmd = ncl_cmd_alloc(opcode, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		if (op == 2) {
			step = 1;
			ncl_cmd->completion = stress_test_read_data_verify;
		} else {
			step = DU_CNT_PER_PAGE;
			if (op == 1) {
				for (i = 0; i < cnt*DU_CNT_PER_PAGE; i++) {
					u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
					data[0] = pda + i;
					data[1] = round;
				}
				ncl_cmd->completion = stress_test_write_check;
			} else {
				ncl_cmd->completion = stress_test_erase_check;
			}

		}
		for (i = 0; i < cnt; i++) {
			pda_list[i] = pda + step*i;
		}
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);

		if (stress_result != 0) {
			break;
		}
	}
	if (stress_result != 0) {
		sys_assert(0);;
	}
}

u8* du_status_map;

void stress_test_read_error_check(struct ncl_cmd_t * ncl_cmd)
{
	u32 i;
	pda_t * pda_list;
	struct info_param_t *info_list;

	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	info_list = ncl_cmd->addr_param.common_param.info_list;
	for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
		u32 du_idx = pda_list[i] & ((1 << nand_pda_block_shift()) - 1);
		switch (du_status_map[du_idx]) {
		case cur_du_ucerr:
			if (info_list[i].status != cur_du_ucerr) {
				if (info_list[i].status == cur_du_good) {
					//ncl_test_trace(LOG_ERR, 0, "PDA %x info. status %d\n", pda_list[i], info_list[i].status);
					continue;
				}
				if (stress_result == 0) {
					stress_result = 5;
				}
				//u32* data = dtag2mem(ncl_cmd->user_tag_list[i].pl.dtag);
				//ncl_test_trace(LOG_ERR, 0, "Read data %x 0x%x\n", data, *data);
			}
			if (!(ncl_cmd->status & NCL_CMD_ARD_STATUS)) {
				ncl_test_trace(LOG_ERR, 0x46f7, "ARD not happen!!!\n");
			}

			break;
		case cur_du_erase:
			if (info_list[i].status != cur_du_erase) {
				if (stress_result == 0) {
					stress_result = 6;
				}
			}
			if (!(ncl_cmd->status & NCL_CMD_ARD_STATUS)) {
				ncl_test_trace(LOG_ERR, 0x5d24, "ard not happen!!!\n");
			}
			break;
		case cur_du_good:
			if (info_list[i].status != cur_du_good) {
				if (stress_result == 0) {
					stress_result = 6;
				}
			} else {
				u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
				if (*data != pda_list[i]) {
					if (stress_result == 0) {
						stress_result = 7;
					}
				}
			}
			break;
		default:
			sys_assert(0);
			break;
		}
	}
	//ncl_test_trace(LOG_ERR, 0, "check\n");
	ncl_cmd_free(ncl_cmd);
}

void self_stress_read_error(void)
{
	u32 spb_id = 8;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	u32 du_cnt;
	u32 max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;

	du_cnt = nand_page_num_slc() * nand_interleave_num() * DU_CNT_PER_PAGE;
	du_status_map = sys_malloc(SLOW_DATA,du_cnt*sizeof(u8));
	if (max_page_cnt > nand_interleave_num()) {
		max_page_cnt = nand_interleave_num();
	}
	cnt = nand_interleave_num();

	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = spb_id << nand_pda_block_shift();
	for (i = 1; i < cnt; i++) {
		pda_list[i] = pda_list[i - 1] + DU_CNT_PER_PAGE;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	u32 du_status = cur_du_ucerr;
	for (pg = 0; pg < nand_page_num_slc() * nand_interleave_num(); pg += cnt) {
		if (pg >= ((nand_page_num_slc() - 6) * nand_interleave_num())) {
			u32 j;
			for (j = 0; j < DU_CNT_PER_PAGE; j++) {
				du_status_map[pg * DU_CNT_PER_PAGE+j] = cur_du_erase;
			}
			cnt = 1;
			continue;
		} else if (pg >= ((nand_page_num_slc() - 10) * nand_interleave_num())) {
			if (du_status == cur_du_ucerr) {
				du_status = cur_du_good;
				extern u32 fcmd_outstanding_cnt;
				while(fcmd_outstanding_cnt != 0) {
					ficu_done_wait();
				}

				// Switch another CMF to simulate decoding error
				extern struct fdma_cfg_group fdma_cfg_table[];
				fdma_cfg_table[FDMA_ENC_CMF1_DL].addr = (u32)get_enc_cmf_buf(0);
				fdma_cfg_table[FDMA_ENC_CMF1_DL].size.b.fdma_conf_xfer_sz = get_enc_cmf_size(0) / sizeof(u32);
				fdma_cfg_table[FDMA_DEC_CMF1_DL].addr = (u32)get_dec_cmf_buf(0);
				fdma_cfg_table[FDMA_DEC_CMF1_DL].size.b.fdma_conf_xfer_sz = get_dec_cmf_size(0) / sizeof(u32);
				eccu_fdma_cfg(FDMA_ENC_CMF1_DL,false);
				eccu_fdma_cfg(FDMA_DEC_CMF1_DL,false);
				eccu_fdma_cfg_reg_t fdma_reg;
				fdma_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
				fdma_reg.b.fdma_conf_mode = FDMA_CONF_NORMAL_MODE;
				eccu_writel(fdma_reg.all, ECCU_FDMA_CFG_REG);

				ficu_mode_disable();
				ficu_mode_enable();
			}
		}
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
			u32 j;
			for (j = 0; j < DU_CNT_PER_PAGE; j++) {
				du_status_map[(pg + i) * DU_CNT_PER_PAGE+j] = du_status;
			}
		}
		for (i = 0; i < cnt*DU_CNT_PER_PAGE; i++) {
			u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
			u32 j;
			for (j = 0; j < NAND_DU_SIZE/sizeof(u32); j++) {
				data[j] = pda_list[i/DU_CNT_PER_PAGE]+(i % DU_CNT_PER_PAGE);
			}
		}
		//ncl_test_trace(LOG_ERR, 0, "Read data %x 0x%x\n", data, *data);

		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
	}
	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	// Enable ARD
	ficu_ctrl_reg0_t reg;
	reg.all = ficu_readl(FICU_CTRL_REG0);
	reg.b.ficu_ard_mode = 0;
	ficu_writel(reg.all, FICU_CTRL_REG0);
	log_level_chg(LOG_ALW);


	u32 loop = 0;
	while (1) {
		loop++;
		if ((loop % 10000) == 0) {
			ncl_test_trace(LOG_ERR, 0x3d85, "loop %d\n", loop);
		}
		u32 list_len;
		list_len = max_page_cnt*DU_CNT_PER_PAGE/3;

		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}

		// Generate random pda list
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		i = 0;
		while(i < list_len) {
			u32 ptr;
			ptr = random_generator() % du_cnt;
			cnt = (random_generator() % (nand_plane_num() * DU_CNT_PER_PAGE)) + 1;
			if ((i + cnt) > list_len) {
				cnt = list_len - i;
			}
			if ((ptr + cnt) > du_cnt) {
				cnt = du_cnt - ptr;
			}
			u32 j;
			for (j = 0; j < cnt; j++) {
				pda_list[i + j] = (spb_id << nand_pda_block_shift()) + ptr + j;
			}
			i += cnt;
		}
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = stress_test_read_error_check;
		ncl_cmd_submit(ncl_cmd);
		if (stress_result != 0) {
			break;
		}
	}
	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	ncl_test_trace(LOG_ERR, 0x8142, "Read error stress fail stop\n");
	sys_free(SLOW_DATA, du_status_map);
}

void force_mdec_fail(bool en)
{
	mdec_ctrs_reg4_t reg4;
	reg4.all = mdec_readl(MDEC_CTRS_REG4);
	if (en) {
		reg4.b.eccu_mdec_force_cw_fail = ~0;
	} else {
		reg4.b.eccu_mdec_force_cw_fail = 0;
	}
	mdec_writel(reg4.all, MDEC_CTRS_REG4);
}

void force_pdec_fail(bool en)
{
	pdec_ctrs_reg3_t reg3;
	reg3.all = pdec_readl(PDEC_CTRS_REG3);
	if (en) {
		reg3.b.eccu_pdec_force_cw_fail = ~0;
	} else {
		reg3.b.eccu_pdec_force_cw_fail = 0;
	}
	pdec_writel(reg3.all, PDEC_CTRS_REG3);

}

void self_stress_ard_test(void)
{
	u32 spb_id = 8;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	u32 max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;

	if (max_page_cnt > nand_interleave_num()) {
		max_page_cnt = nand_interleave_num();
	}
	cnt = nand_interleave_num();

	ncb_ncl_trace(LOG_INFO, 0xae42, "Prepare SPB");
	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = spb_id << nand_pda_block_shift();
	for (i = 1; i < cnt; i++) {
		pda_list[i] = pda_list[i - 1] + DU_CNT_PER_PAGE;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	for (pg = 0; pg < nand_page_num_slc() * nand_interleave_num(); ) {
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
		pg += cnt;
	}

	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	// Force mdec/pdec fail
	force_mdec_fail(true);
	force_pdec_fail(true);
	u32 loop = 0;

	// Generate read ncl_cmd with random pda_list
	u32 du_per_spb = nand_interleave_num() * nand_page_num_slc() * DU_CNT_PER_PAGE;
	u32 max_len = 32, max_seq = 8;
	u32 list_len;
	u32 ptr;

	while(1) {
		loop++;
		if ((loop % 1000) == 0) {
			ncb_ncl_trace(LOG_INFO, 0x6c98, "loop %d", loop);
		}
		list_len = (random_generator() % max_len) + 1;
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < list_len; ) {
			ptr = random_generator() % du_per_spb;
			cnt = (random_generator() % max_seq) + 1;
			if (cnt > (list_len - i)) {
				cnt = list_len - i;
			}
			if ((ptr + cnt) > du_per_spb) {
				cnt = du_per_spb - ptr;
			}
			u32 j;
			for (j = 0; j < cnt; j++) {
				pda_list[i+j] = (spb_id << nand_pda_block_shift()) + ptr + j;
			}
			i += cnt;
		}
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
#if WA_BM_WRITE && !defined(MPC)
		extern void btn_grp_enable(u32 grp, bool enable);
		while(NCL_CMD_PENDING(ncl_cmd)) {
			ficu_done_wait();
		}
		btn_grp_enable(0/*BM_GRP_DTAG*/, false);
		btn_grp_enable(0/*BM_GRP_DTAG*/, true);
#endif
	}
	// Clear mdec/pdec fail
	force_mdec_fail(false);
	force_pdec_fail(false);
}

void self_stress_erd_test(void)
{
	u32 spb_id = 8;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	u32 max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;

	if (max_page_cnt > nand_interleave_num()) {
		max_page_cnt = nand_interleave_num();
	}
	cnt = nand_interleave_num();

	ncb_ncl_trace(LOG_INFO, 0xf40c, "Prepare SPB");
	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = spb_id << nand_pda_block_shift();
	for (i = 1; i < cnt; i++) {
		pda_list[i] = pda_list[i - 1] + DU_CNT_PER_PAGE;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	for (pg = 0; pg < nand_page_num_slc() * nand_interleave_num(); ) {
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
		pg += cnt;
	}

	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	// Force mdec/pdec fail
	eccu_set_dec_mode(DEC_MODE_MDEC);
	eccu_set_ard_dec(ARD_MODE_MDEC);
	force_mdec_fail(true);
	u32 loop = 0;

	// Generate read ncl_cmd with random pda_list
	u32 du_per_spb = nand_interleave_num() * nand_page_num_slc() * DU_CNT_PER_PAGE;
	u32 max_len = 32, max_seq = 8;
	u32 list_len;
	u32 ptr;

	while(1) {
		loop++;
		if ((loop % 100) == 0) {
			ncb_ncl_trace(LOG_INFO, 0x6d1d, "loop %d", loop);
		}
		list_len = (random_generator() % max_len) + 1;
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < list_len; ) {
			ptr = random_generator() % du_per_spb;
			cnt = (random_generator() % max_seq) + 1;
			if (cnt > (list_len - i)) {
				cnt = list_len - i;
			}
			if ((ptr + cnt) > du_per_spb) {
				cnt = du_per_spb - ptr;
			}
			u32 j;
			for (j = 0; j < cnt; j++) {
				pda_list[i+j] = (spb_id << nand_pda_block_shift()) + ptr + j;
			}
			i += cnt;
		}
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
	}
	// Clear mdec fail
	force_mdec_fail(false);
}

void ncl_cmd_erd_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	//ncl_cmd->flags |= NCL_CMD_COMPLETED_FLAG;
	if (ncl_cmd->op_code == NCL_CMD_OP_READ_ERD) {
		u32 ch;
		ch = pda2ch(ncl_cmd->addr_param.rapid_du_param.pda);
		extern u8* erd_cnt_tbl_by_ch;
		erd_cnt_tbl_by_ch[ch]--;

		//ncl_test_trace(LOG_ERR, 0, "PDA %x status %d\n", ncl_cmd->addr_param.rapid_du_param.pda, ncl_cmd->addr_param.rapid_du_param.info.status);
		if ((ncl_cmd->flags & NCL_CMD_IGNORE_ERR_FLAG) == 0) {
			sys_assert(ncl_cmd->addr_param.rapid_du_param.info.status == cur_du_good);
		}
	} else {
		u32 i;
		//ncl_test_trace(LOG_ERR, 0, "normal read %d\n", ncl_cmd->addr_param.common_param.list_len);
		if ((ncl_cmd->flags & NCL_CMD_IGNORE_ERR_FLAG) == 0) {
			for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
				if (ncl_cmd->addr_param.common_param.info_list[i].status != cur_du_good) {
					sys_assert(0);
				}
			}
		}
	}
	ncl_cmd_free(ncl_cmd);
}

void self_stress_mix_erd_normal_test(void)
{
	u32 spb_id = 4;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	u32 max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;
	dec_top_ctrs_reg3_t erd_reg;
	u32 max_ch_erd;
	erd_reg.all = dec_top_readl(DEC_TOP_CTRS_REG3);
	max_ch_erd = erd_reg.b.eccu_erd_max_num_ch;

	if (max_page_cnt > nand_interleave_num()) {
		max_page_cnt = nand_interleave_num();
	}
	cnt = nand_interleave_num();

	ficu_mode_ard_control(0);

if (1) {
	ncb_ncl_trace(LOG_INFO, 0x7184, "Prepare SPB");
	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = spb_id << nand_pda_block_shift();
	for (i = 1; i < cnt; i++) {
		pda_list[i] = pda_list[i - 1] + DU_CNT_PER_PAGE;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	for (pg = 0; pg < nand_page_num_slc() * nand_interleave_num(); ) {
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
		pg += cnt;
	}

	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
}
	// Force mdec/pdec fail
	u32 loop = 0;

	// Generate read ncl_cmd with random pda_list
	u32 du_per_spb = nand_interleave_num() * nand_page_num_slc() * DU_CNT_PER_PAGE;
	u32 max_len = 32, max_seq = 8;
	u32 list_len;
	u32 ptr;

	while(1) {
		bool erd;
		u8 opcode;
		loop++;
		if ((loop % 100) == 0) {
			ncb_ncl_trace(LOG_INFO, 0x4611, "loop %d", loop);
		}
		erd = ((random_generator() % 4) > 1) ? true : false;
		if (erd) {
			list_len = 1;
			opcode = NCL_CMD_OP_READ_ERD;
		} else {
			list_len = (random_generator() % max_len) + 1;
			opcode = NCL_CMD_OP_READ;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(opcode, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		ncl_cmd->completion = ncl_cmd_erd_cmpl;
		if (erd) {
			pda_list = &ncl_cmd->addr_param.rapid_du_param.pda;
			ncl_cmd->addr_param.rapid_du_param.list_len = list_len;
		} else {
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
		}

		ptr = random_generator() % du_per_spb;
		if (erd) {
			pda_list[0] = (spb_id << nand_pda_block_shift()) + ptr;
			//pda_list[0] = 0x80000;//0x105D80;//
		} else {
			for (i = 0; i < list_len; ) {
				ptr = random_generator() % du_per_spb;
				cnt = (random_generator() % max_seq) + 1;
				if (cnt > (list_len - i)) {
					cnt = list_len - i;
				}
				if ((ptr + cnt) > du_per_spb) {
					cnt = du_per_spb - ptr;
				}
				u32 j;
				for (j = 0; j < cnt; j++) {
					pda_list[i+j] = (spb_id << nand_pda_block_shift()) + ptr + j;
				}
				i += cnt;
			}
		}
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
		extern u8* erd_cnt_tbl_by_ch;
		while(erd_cnt_tbl_by_ch[0] >= max_ch_erd) {
			ficu_done_wait();
		}
	}
}

void self_stress_mix_fw_ard_normal_test(void)
{
	u32 spb_id = 2;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	u32 max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;

	if (max_page_cnt > nand_interleave_num()) {
		max_page_cnt = nand_interleave_num();
	}
	cnt = nand_interleave_num();
	ficu_mode_ard_control(2);
	eccu_ctrl_reg1_t eccu_reg;
	eccu_reg.all = eccu_readl(ECCU_CTRL_REG1);
	eccu_reg.b.dec_fw_ard_en = 1;
	eccu_writel(eccu_reg.all, ECCU_CTRL_REG1);

if (0) {
	ncb_ncl_trace(LOG_INFO, 0xdb3e, "Prepare SPB");
	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = spb_id << nand_pda_block_shift();
	for (i = 1; i < cnt; i++) {
		pda_list[i] = pda_list[i - 1] + DU_CNT_PER_PAGE;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	for (pg = 0; pg < (nand_page_num_slc() - 1) * nand_interleave_num(); ) {
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
		pg += cnt;
	}

	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
}
	// Force mdec/pdec fail
	u32 loop = 0;

	// Generate read ncl_cmd with random pda_list
	u32 du_per_spb = nand_interleave_num() * nand_page_num_slc() * DU_CNT_PER_PAGE;
	u32 max_len = 16, max_seq = 8;
	u32 list_len;
	u32 ptr;

	dec_top_ctrs_reg1_t dec_top_reg;
	dec_top_reg.all = dec_top_readl(DEC_TOP_CTRS_REG1);
	dec_top_reg.b.eccu_ard_data_fmt = 2;
	dec_top_writel(dec_top_reg.all, DEC_TOP_CTRS_REG1);

	while(1) {
		bool ard;
		u8 opcode;
		loop++;
		if ((loop % 100) == 0) {
			ncb_ncl_trace(LOG_INFO, 0xa7aa, "loop %d", loop);
		}
		if (fw_ard_cnt != 0) {
			ard = false;
		} else {
			ard = true;
		}
		if (ard) {
			list_len = 1;
			opcode = NCL_CMD_OP_READ_FW_ARD;
		} else {
			list_len = (random_generator() % max_len) + 1;
			opcode = NCL_CMD_OP_READ;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(opcode, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		ncl_cmd->completion = ncl_cmd_erd_cmpl;
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		if (ard) {
			ncl_cmd->ard_rd_cnt = 3;
		}

		ptr = random_generator() % (du_per_spb - (nand_interleave_num() * DU_CNT_PER_PAGE));
		if (ard) {
			pda_list[0] = (spb_id << nand_pda_block_shift()) + ptr;
		} else {
			for (i = 0; i < list_len; ) {
				ptr = random_generator() % (du_per_spb - (nand_interleave_num() * DU_CNT_PER_PAGE));
				cnt = (random_generator() % max_seq) + 1;
				if (cnt > (list_len - i)) {
					cnt = list_len - i;
				}
				if ((ptr + cnt) > du_per_spb - (nand_interleave_num() * DU_CNT_PER_PAGE)) {
					cnt = du_per_spb - (nand_interleave_num() * DU_CNT_PER_PAGE) - ptr;
				}
				u32 j;
				for (j = 0; j < cnt; j++) {
					pda_list[i+j] = (spb_id << nand_pda_block_shift()) + ptr + j;
				}
				i += cnt;
			}
		}
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		//ncl_cmd->flags |= NCL_CMD_IGNORE_ERR_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
	}
}

void stress_test_good_check(struct ncl_cmd_t * ncl_cmd)
{
	u32 i;
	pda_t * pda_list;
	struct info_param_t *info_list;

	if (ncl_cmd->status & NCL_CMD_ARD_STATUS) {
		ncl_test_trace(LOG_ERR, 0x9f45, "ARD happen!!!\n");
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	info_list = ncl_cmd->addr_param.common_param.info_list;
	for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
		if (info_list[i].status != cur_du_good) {
			if (stress_result == 0) {
				//stress_result = 6;
				ncl_test_trace(LOG_ERR, 0x1cb5, "fail op %d, pda %x, status %d\n", ncl_cmd->op_code, pda_list[i], info_list[i].status);
			}
		} else if (ncl_cmd->op_code == NCL_CMD_OP_READ){// Check data
			u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
			if (*data != pda_list[i]) {
				if (stress_result == 0) {
					stress_result = 10;
				}
			}
		}
	}
	//ncl_test_trace(LOG_ERR, 0, "check\n");
	ncl_cmd_free(ncl_cmd);
}

void self_stress_ard_test_with_real_error(void)
{
	u32 spb_id = 8;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	u32 du_cnt;
	u32 max_page_cnt = (dtag_get_avail_cnt(DTAG_T_SRAM) - 4/*DTAG_URGT_CNT*/) / DU_CNT_PER_PAGE;
	u32 rw_spb_id = 99;
	u32 rw_spb_ptr = nand_page_num_slc() * nand_interleave_num();
	log_level_chg(LOG_DEBUG);
	du_cnt = nand_page_num_slc() * nand_interleave_num() * DU_CNT_PER_PAGE;
	du_status_map = sys_malloc(SLOW_DATA,du_cnt*sizeof(u8));
	if (max_page_cnt > nand_interleave_num()) {
		max_page_cnt = nand_interleave_num();
	}
	cnt = nand_interleave_num();
	eccu_ctrl_reg0_t eccu_ctrl;
	eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
	eccu_ctrl.b.enc_rpt_on_enc_err = 0;
	eccu_ctrl.b.dec_rpt_on_dfu_err = 0;
	//eccu_ctrl.b.dec_halt_on_err = 0;
	//eccu_ctrl.b.dec_ard_halt_on_err = 0;
	//eccu_ctrl.b.dec_halt_on_send_status = 0;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	pda_list[0] = spb_id << nand_pda_block_shift();
	for (i = 1; i < cnt; i++) {
		pda_list[i] = pda_list[i - 1] + DU_CNT_PER_PAGE;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	u32 du_status = cur_du_ucerr;
	for (pg = 0; pg < nand_page_num_slc() * nand_interleave_num(); pg += cnt) {
		if (pg >= ((nand_page_num_slc() - 6) * nand_interleave_num())) {
			u32 j;
			for (j = 0; j < DU_CNT_PER_PAGE; j++) {
				du_status_map[pg * DU_CNT_PER_PAGE+j] = cur_du_erase;
			}
			cnt = 1;
			continue;
		} else if (pg >= ((nand_page_num_slc() - 10) * nand_interleave_num())) {
			if (du_status == cur_du_ucerr) {
				du_status = cur_du_good;
			}
		}
		cnt = max_page_cnt;
		if ((pg + cnt) > nand_page_num_slc() * nand_interleave_num()) {
			cnt = nand_page_num_slc() * nand_interleave_num() - pg;
		}
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg + i) * DU_CNT_PER_PAGE;
			u32 j;
			for (j = 0; j < DU_CNT_PER_PAGE; j++) {
				du_status_map[(pg + i) * DU_CNT_PER_PAGE+j] = du_status;
			}
		}
		for (i = 0; i < cnt*DU_CNT_PER_PAGE; i++) {
			u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
			u32 j;
			for (j = 0; j < NAND_DU_SIZE/sizeof(u32); j++) {
				data[j] = pda_list[i/DU_CNT_PER_PAGE]+(i % DU_CNT_PER_PAGE);
			}
		}
		//ncl_test_trace(LOG_ERR, 0, "Read data %x 0x%x\n", data, *data);

		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		if (du_status == cur_du_ucerr) {
			ncl_cmd->du_format_no = DU_FMT_RAW_4K;
		} else {
			ncl_cmd->du_format_no = DU_FMT_USER_4K;
		}
		ncl_cmd_submit(ncl_cmd);
	}
	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	ncl_test_trace(LOG_ERR, 0x9cc4, "spb prepared\n");

	// Enable ARD
	ficu_ctrl_reg0_t reg;
	reg.all = ficu_readl(FICU_CTRL_REG0);
	reg.b.ficu_ard_mode = 1;
	ficu_writel(reg.all, FICU_CTRL_REG0);
	log_level_chg(LOG_ALW);

	mdec_ctrs_reg4_t mdec_reg;
	mdec_reg.all = mdec_readl(MDEC_CTRS_REG4);
	mdec_reg.b.eccu_mdec_max_iter_cnt = 3;
	mdec_writel(mdec_reg.all, MDEC_CTRS_REG4);

	pdec_ctrs_reg3_t pdec_reg;
	pdec_reg.all = pdec_readl(PDEC_CTRS_REG3);
	pdec_reg.b.eccu_pdec_max_iter_cnt = 1;
	pdec_writel(pdec_reg.all, PDEC_CTRS_REG3);


	u32 loop = 0;
	while (1) {
		u32 random = random_generator() % 100;
		if (random < 80) {// 80% do read that will trigger ARD
		//if (0) {
		} else {
			if (rw_spb_ptr == nand_page_num_slc() * nand_interleave_num()) {// Full
				// Erase SPB
				rw_spb_ptr = 0;
				// Erase whole SPB
				while(1) {

					ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, nand_interleave_num());
					if (ncl_cmd == NULL) {
						ficu_done_wait();
					} else {
						break;
					}
				}
				pda_list = ncl_cmd->addr_param.common_param.pda_list;
				for (i = 0; i < nand_interleave_num(); i++) {
					pda_list[i] = (rw_spb_id << nand_pda_block_shift()) + DU_CNT_PER_PAGE*i;
				}
				ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
				ncl_cmd->du_format_no = DU_FMT_USER_4K;
				ncl_cmd->completion = stress_test_good_check;
				ncl_cmd_submit(ncl_cmd);
				ncl_test_trace(LOG_ERR, 0x9da0, "erase another SPB\n");
			} else {
				if (random < 90) {// 10%d Write
				//ncl_test_trace(LOG_ERR, 0, "write another SPB\n");
					while(1) {

						ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, nand_plane_num());
						if (ncl_cmd == NULL) {
							ficu_done_wait();
						} else {
							break;
						}
					}
					pda_list = ncl_cmd->addr_param.common_param.pda_list;
					for (i = 0; i < nand_plane_num(); i++) {
						pda_list[i] = (rw_spb_id << nand_pda_block_shift()) + (rw_spb_ptr + i) * DU_CNT_PER_PAGE;
						ncl_cmd->addr_param.common_param.info_list[i].xlc.slc_idx = 0;
					}
					for (i = 0; i < nand_plane_num()*DU_CNT_PER_PAGE; i++) {
						u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
						data[0] = pda_list[i/DU_CNT_PER_PAGE]+(i % DU_CNT_PER_PAGE);
					}
					ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
					ncl_cmd->du_format_no = DU_FMT_USER_4K;
					ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
					ncl_cmd->completion = stress_test_good_check;
					ncl_cmd_submit(ncl_cmd);
					rw_spb_ptr += nand_plane_num();
					//ncl_test_trace(LOG_ERR, 0, "write another SPB %x\n", pda_list[0]);
				} else {// 10% Read
if (rw_spb_ptr < 32) {
	continue;
}
//ncl_test_trace(LOG_ERR, 0, "Read another SPB\n");
					while(1) {

						ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, nand_plane_num() *DU_CNT_PER_PAGE);
						if (ncl_cmd == NULL) {
							ficu_done_wait();
						} else {
							break;
						}
					}
					pda_list = ncl_cmd->addr_param.common_param.pda_list;
					for (i = 0; i < nand_plane_num() * DU_CNT_PER_PAGE; i++) {
						pda_list[i] = (rw_spb_id << nand_pda_block_shift()) + (rw_spb_ptr - 8 * nand_plane_num()) * DU_CNT_PER_PAGE + i;
						//pda_list[i] = (rw_spb_id << nand_pda_block_shift()) + (pg * 0x10) + i;
					}
					ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
					ncl_cmd->du_format_no = DU_FMT_USER_4K;
					ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
					ncl_cmd->completion = stress_test_good_check;
					ncl_cmd_submit(ncl_cmd);
					//ncl_test_trace(LOG_ERR, 0, "Read another SPB %x\n", pda_list[0]);
				}
			}
#if 1
		extern u32 fcmd_outstanding_cnt;
		while(fcmd_outstanding_cnt != 0) {
			ficu_done_wait();
		}
#endif
			continue;
		}

		loop++;
		if ((loop % 1000) == 0) {
			ncl_test_trace(LOG_ERR, 0xd7b6, "loop %d\n", loop);
		}
		u32 list_len;
		//list_len = max_page_cnt*DU_CNT_PER_PAGE/3;
		list_len = 4;
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}

		// Generate random pda list
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		i = 0;
		while(i < list_len) {
			u32 ptr;
			ptr = random_generator() % du_cnt;
			cnt = (random_generator() % (nand_plane_num() * DU_CNT_PER_PAGE)) + 1;
			if ((i + cnt) > list_len) {
				cnt = list_len - i;
			}
			if ((ptr + cnt) > du_cnt) {
				cnt = du_cnt - ptr;
			}
			u32 j;
			for (j = 0; j < cnt; j++) {
				pda_list[i + j] = (spb_id << nand_pda_block_shift()) + ptr + j;
			}
			i += cnt;
		}
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd->completion = stress_test_read_error_check;
		ncl_cmd_submit(ncl_cmd);
		if (stress_result != 0) {
			break;
		}
#if 1
		extern u32 fcmd_outstanding_cnt;
		while(fcmd_outstanding_cnt != 0) {
			ficu_done_wait();
		}
#endif
	}
	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	ncl_test_trace(LOG_ERR, 0xd6d1, "Read error stress fail stop\n");
	sys_free(SLOW_DATA, du_status_map);
}

extern bool erase_pda(pda_t pda, nal_pb_type_t pb_type);
extern bool write_pda_page(pda_t pda, nal_pb_type_t pb_type, u32 step);
extern bool read_pda_du(pda_t pda, nal_pb_type_t pb_type, u32 check_level, u8 du_fmt, bool fail_retry);
bool slc_block_test(pda_t pda)
{
	u32 pg;
	u32 pg_per_blk;
	bool ret = true;

	pg_per_blk = nand_page_num_slc();

	pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);
	pda &= ~(DU_CNT_PER_PAGE - 1);

	// Erase block
	ncb_ncl_trace(LOG_INFO, 0xd3c4, "erase");
	ret &= erase_pda(pda, NAL_PB_TYPE_SLC);

	// Program block
	ncb_ncl_trace(LOG_INFO, 0x3892, "program");
	for (pg = 0; pg < pg_per_blk; pg++) {
		ret &= write_pda_page(pda, NAL_PB_TYPE_SLC, 0);
		pda += nand_interleave_num() * DU_CNT_PER_PAGE;
	}

	pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);

	// Read block
	ncb_ncl_trace(LOG_INFO, 0x63c0, "read");
	for (pg = 0; pg < pg_per_blk; pg++) {
		u32 du;
		for (du = 0; du < DU_CNT_PER_PAGE; du++) {
			ret &= read_pda_du(pda+du, NAL_PB_TYPE_SLC, true, DU_FMT_USER_4K, false);
		}
		pda += nand_interleave_num() * DU_CNT_PER_PAGE;
	}
	ncb_ncl_trace(LOG_INFO, 0x79c5, "SLC blk test %s", (u32)(ret ? "pass" : "fail"));
	return ret;
}

void spb_bad_block_erase_test(u32 spb_id, nal_pb_type_t pb_type, u8*bmp)
{
	pda_t* pda_list;
	u32 i, cnt;
	struct ncl_cmd_t* ncl_cmd;

	cnt = nand_interleave_num();
	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	for (i = 0; i < cnt; i++) {
		pda_list[i] = (spb_id << nand_pda_block_shift()) + (i * DU_CNT_PER_PAGE);
		ncl_cmd->addr_param.common_param.info_list[i].status = cur_du_good;
	}
	if (pb_type == NAL_PB_TYPE_SLC) {
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	}
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd->completion = NULL;
	ncl_cmd_submit(ncl_cmd);
	ncl_cmd_free(ncl_cmd);
	for (i = 0; i < cnt; i++) {
		u32 idx, off;
		idx = i / 8;
		off = i % 8;
		if (ncl_cmd->addr_param.common_param.info_list[i].status == cur_du_good) {
			//bmp[idx] &= ~(1 << off);
		} else {
			bmp[idx] |= 1 << off;
		}
	}
}

void spb_bad_block_write_test(u32 spb_id, nal_pb_type_t pb_type, u8*bmp)
{
	pda_t* pda_list;
	u32 i, cnt;
	u32 pg, pg_cnt;
	struct ncl_cmd_t* ncl_cmd;

	cnt = nand_interleave_num();
	// Write whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	if (pb_type == NAL_PB_TYPE_SLC) {
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	}
	ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd->completion = NULL;
	if (pb_type == NAL_PB_TYPE_SLC) {
		pg_cnt = nand_page_num_slc();
	} else {
		pg_cnt = nand_page_num();
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	for (i = 0; i < cnt * DU_CNT_PER_PAGE; i++) {
		u32 j;
		u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
		for (j = 0; j < NAND_DU_SIZE/sizeof(u32); j++) {
			data[j] = 0x55AA33CC;
		}
	}
	for (pg = 0; pg < pg_cnt; pg++) {
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + ((pg * nand_interleave_num() + i) * DU_CNT_PER_PAGE);
			ncl_cmd->addr_param.common_param.info_list[i].status = cur_du_good;
		}
		ncl_cmd_submit(ncl_cmd);
		for (i = 0; i < cnt; i++) {
			u32 idx, off;
			idx = i / 8;
			off = i % 8;
			if (ncl_cmd->addr_param.common_param.info_list[i].status == cur_du_good) {
				//bmp[idx] &= ~(1 << off);
			} else {
				bmp[idx] |= 1 << off;
			}
		}
	}
	ncl_cmd_free(ncl_cmd);
}

void spb_bad_block_read_test(u32 spb_id, nal_pb_type_t pb_type, u8*bmp)
{
	pda_t* pda_list;
	u32 i, cnt;
	u32 pg, pg_cnt;
	struct ncl_cmd_t* ncl_cmd;

	cnt = nand_interleave_num() * DU_CNT_PER_PAGE;
	// Write whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	if (pb_type == NAL_PB_TYPE_SLC) {
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	}
	ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd->completion = NULL;
	if (pb_type == NAL_PB_TYPE_SLC) {
		pg_cnt = nand_page_num_slc();
	} else {
		pg_cnt = nand_page_num();
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	for (pg = 0; pg < pg_cnt; pg++) {
		for (i = 0; i < cnt; i++) {
			pda_list[i] = (spb_id << nand_pda_block_shift()) + (pg * nand_interleave_num() * DU_CNT_PER_PAGE + i);
			ncl_cmd->addr_param.common_param.info_list[i].status = cur_du_good;
		}
		ncl_cmd_submit(ncl_cmd);
		for (i = 0; i < cnt; i++) {
			u32 idx, off;
			idx = (i / DU_CNT_PER_PAGE) / 8;
			off = (i / DU_CNT_PER_PAGE) % 8;
			if (ncl_cmd->addr_param.common_param.info_list[i].status == cur_du_good) {
				//bmp[i] &= ~(1 << off);
				u32* data = dtagid2mem(ncl_cmd->user_tag_list[i].pl.dtag);
				u32 j;
				for (j = 0; j < NAND_DU_SIZE/sizeof(u32); j++) {
					if (data[j] != 0x55AA33CC) {
						bmp[idx] |= 1 << off;
						break;
					}
				}
			} else {
				bmp[idx] |= 1 << off;
			}
		}
	}
	ncl_cmd_free(ncl_cmd);
}

void spb_bad_block_scan(u32 spb_id, u8* bmp)
{
	spb_bad_block_erase_test(spb_id, NAL_PB_TYPE_SLC, bmp);
	spb_bad_block_write_test(spb_id, NAL_PB_TYPE_SLC, bmp);
	spb_bad_block_read_test(spb_id, NAL_PB_TYPE_SLC, bmp);
	spb_bad_block_erase_test(spb_id, NAL_PB_TYPE_XLC, bmp);
	spb_bad_block_write_test(spb_id, NAL_PB_TYPE_XLC, bmp);
	spb_bad_block_read_test(spb_id, NAL_PB_TYPE_XLC, bmp);
}

void nand_bad_block_scan(void)
{
	u32 spb;
	u8* bmp_tmp;
	u32 i;
	u32 byte_size;

	byte_size = (nand_interleave_num() + 7) / 8;

	bmp_tmp = sys_malloc(SLOW_DATA, byte_size);
	sys_assert(nand_interleave_num());
	for (spb = 0; spb < nand_block_num(); spb++) {
		for (i = 0; i < byte_size; i++) {
			bmp_tmp[i] = 0;
		}
		spb_bad_block_scan(spb, bmp_tmp);
	}
	ncl_test_trace(LOG_ERR, 0x4d93, "scan done\n");
	sys_free(SLOW_DATA, bmp_tmp);
}

slow_code void ncl_test(void)
{
	//test_raw_read();
	//ncl_console();
	//test_read_refresh();
	//ncl_slc2tlc_copyback_test();
	//tlc_block_test(11);
	//ncl_read_block(0xB0000, NAL_PB_TYPE_XLC);
	//ncl_regression();
	//self_stress_test_slc_simple();
	self_stress_test_slc_random();
	while(1);
}

#include "pmu.h"
void ncl_pmu_test(void)
{
	extern bool ncl_pmu_suspend(enum sleep_mode_t mode);
	extern void ncl_pmu_resume(enum sleep_mode_t mode);

	self_stress_test_slc_simple();

	// PMU suspend and resume
	ncl_pmu_suspend(0);
	misc_reset(RESET_NCB);// Simulate NCB power off
	ncl_pmu_resume(0);

	// After PMU resume, basic erase/program/read should OK
	self_stress_test_slc_simple();
}

void stress_cmf_read_check(struct ncl_cmd_t * ncl_cmd)
{
	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
		sys_assert(0);
	}
	ncl_cmd_free(ncl_cmd);
}

// 1 MP block use 1 du_fmt & CMF, another MP block use different du_fmt & CMF
// Async erase 2 MP blocks, async program 2 MP blocks (MP page by page), then async read
// Currently 2 MP block on same LUN, you may change to diff CH/CE/LUN
bool self_stress_test_two_cmf(void)
{
	u32 spb_id = 8;
	pda_t pda_base[2];
	pda_t* pda_list;
	u32 i, pg, cnt, blk;
	struct ncl_cmd_t* ncl_cmd;
	bool ret = true;

#if !ENABLE_2_CMF
	// Please enable 2 CMF
	sys_assert(0);
#endif
	extern eccu_conf_reg_t eccu_cfg;
	if (!eccu_cfg.b.cmf_1) {
		sys_assert(0);
	}
	cnt = nand_plane_num();

	pda_base[0] = spb_id << nand_pda_block_shift();
	pda_base[1] = ((spb_id + 1) << nand_pda_block_shift());// + (1 << nand_info.pda_ch_shift);

	// Erase whole SPB
	for (blk = 0; blk < 2; blk++) {
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = pda_base[blk] + DU_CNT_PER_PAGE * i;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd_submit(ncl_cmd);
	}

	// Program whole SPB
	for (pg = 0; pg < nand_page_num_slc(); pg++) {
		for (blk = 0; blk < 2; blk++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			for (i = 0; i < cnt; i++) {
				pda_list[i] = pda_base[blk] + (pg * nand_interleave_num() + i) * DU_CNT_PER_PAGE;
			}
			ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
			if (blk == 0) {
				ncl_cmd->du_format_no = DU_FMT_USER_4K;
			} else {
				ncl_cmd->du_format_no = DU_FMT_USER_4K_CMF1;
			}
			ncl_cmd_submit(ncl_cmd);
		}
	}

	// Read whole SPB
	for (pg = 0; pg < nand_page_num_slc(); pg++) {
		for (blk = 0; blk < 2; blk++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, cnt*DU_CNT_PER_PAGE);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			for (i = 0; i < cnt; i++) {
				u32 j;
				for (j = 0; j < DU_CNT_PER_PAGE; j++) {
					pda_list[i*DU_CNT_PER_PAGE + j] = pda_base[blk] + (pg * nand_interleave_num() + i) * DU_CNT_PER_PAGE + j;
				}
			}
			ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
			if (blk == 0) {
				ncl_cmd->du_format_no = DU_FMT_USER_4K;
			} else {
				ncl_cmd->du_format_no = DU_FMT_USER_4K_CMF1;
			}
			ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
			ncl_cmd->completion = stress_cmf_read_check;
			ncl_cmd_submit(ncl_cmd);
		}
	}

	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	ncl_test_trace(LOG_ERR, 0xb381, "over\n");
	return ret;
}

// 1 MP block use 1 du_fmt & CMF, another MP block use different du_fmt & CMF
// Async erase 2 MP blocks, async program 2 MP blocks (MP page by page), then async read
// Currently 2 MP block on same LUN, you may change to diff CH/CE/LUN
#include "ncb_ndcu_register.h"
#include "ndcu.h"
bool self_stress_test_aipr(void)
{
	u32 spb_id = 8;
	pda_t pda_base;
	pda_t* pda_list;
	u32 i, pg, cnt;
	struct ncl_cmd_t* ncl_cmd;
	bool ret = true;

#if !HAVE_TSB_SUPPORT
	sys_assert(0);
#endif

	if (ncmd_fmt_array[FINST_NAND_FMT_SLC_READ_CMD].fmt.b.rd_scmd_mode < NAND_FMT_RD_SCMD_SBYTE5) {
		// Please configure 78h CMD polling status
		sys_assert(0);
	}
	if ((nand_info.id[5] & 0x7) == 3) {
		ficu_mode_disable();
		ndcu_en_reg_control_mode();
		extern void set_feature(u8 ch, u8 ce, u8 fa, u32 val);
		extern u32 get_feature(u8 ch, u8 ce, u8 fa);
		u32 ch, ce;
		for (ch = 0; ch < nand_info.geo.nr_channels; ch++) {
			for (ce = 0; ce < nand_info.geo.nr_targets; ce++) {
				set_feature(ch, ce, 0x91, 1);
				u32 value = get_feature(ch, ce, 0x91);
				if (value == 1) {
					ncl_test_trace(LOG_ERR, 0xa20d, "BiCS4, enable AIPR\n");
				} else {
					ncl_test_trace(LOG_ERR, 0xc96b, "BiCS4, ch %d ce %d enable AIPR %x\n", ch, ce, value);
				}
			}
		}
		ndcu_dis_reg_control_mode();
		ficu_mode_enable();
	} else {
		ncl_test_trace(LOG_ERR, 0x716f, "Not BiCS4, use BiCS4 if possible!\n");
	}

	cnt = nand_plane_num();

	pda_base = spb_id << nand_pda_block_shift();
	// Erase whole SPB
	while(1) {
		ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_ERASE, cnt);
		if (ncl_cmd == NULL) {
			ficu_done_wait();
		} else {
			break;
		}
	}
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	for (i = 0; i < cnt; i++) {
		pda_list[i] = pda_base + DU_CNT_PER_PAGE * i;
	}
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd_submit(ncl_cmd);

	// Program whole SPB
	for (pg = 0; pg < nand_page_num_slc(); pg++) {
		while(1) {
			ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_WRITE, cnt);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		pda_list = ncl_cmd->addr_param.common_param.pda_list;
		for (i = 0; i < cnt; i++) {
			pda_list[i] = pda_base + (pg * nand_interleave_num() + i) * DU_CNT_PER_PAGE;
		}
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd_submit(ncl_cmd);
	}
	extern u32 fcmd_outstanding_cnt;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}

	u32 loop;
	extern struct finstr_format read_fins_templ;
	for (loop = 0; loop < 2; loop++) {
		if (loop == 0) {
			read_fins_templ.dw0.b.mp_enh = 0;
		} else {
			read_fins_templ.dw0.b.mp_enh = 1;
		}
		u32 t1, t2;
		t1 = get_tsc_lo();
		// Read whole SPB
		for (pg = 0; pg < nand_page_num_slc(); pg++) {
			while(1) {
				ncl_cmd = ncl_cmd_alloc(NCL_CMD_OP_READ, cnt);
				if (ncl_cmd == NULL) {
					ficu_done_wait();
				} else {
					break;
				}
			}
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
			for (i = 0; i < cnt; i++) {
				pda_list[i] = pda_base + (pg * nand_interleave_num() + i) * DU_CNT_PER_PAGE;
			}
			ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
			ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
			ncl_cmd_submit(ncl_cmd);
		}

		extern u32 fcmd_outstanding_cnt;
		while(fcmd_outstanding_cnt != 0) {
			ficu_done_wait();
		}
		t2 = get_tsc_lo();
		if (loop == 0) {
			ncl_test_trace(LOG_ERR, 0x18fb, "Disable AIPR, tick %d\n", t2 - t1);
		} else {
			ncl_test_trace(LOG_ERR, 0x9ce4, "Enable AIPR, tick %d\n", t2 - t1);
		}
	}
	read_fins_templ.dw0.b.mp_enh = 0;
	return ret;
}

#if HAVE_TSB_SUPPORT && QLC_SUPPORT
void bics4_qlc_block_test(void)
{
	u32 spb_id = 8;
	pda_t pda_base;
	u32 wl;
	u32 pg;

	extern bool erase_pda(pda_t pda, nal_pb_type_t pb_type);
	extern bool write_pda_page(pda_t pda, nal_pb_type_t pb_type, u32 step);
	extern bool read_pda_du(pda_t pda, nal_pb_type_t pb_type, u32 check_level, u8 du_fmt, bool fail_retry);

	pda_base = spb_id << nand_info.pda_block_shift;

	// Erase
	erase_pda(pda_base, 1);

	// Program whole block
	for (wl = 0; wl < nand_page_num_slc(); wl++) {
		//ncl_test_trace(LOG_ERR, 0, "Prog (1st) wl %d\n", wl);
		for (pg = 0; pg < XLC; pg++) {
			write_pda_page(pda_base + (((wl * XLC) + pg) << nand_info.pda_page_shift), 1, PROG_1ST_STEP);
		}
		if (wl >= 4) {
			//ncl_test_trace(LOG_ERR, 0, "Prog (2nd) wl %d\n", wl - 4);
			for (pg = 0; pg < XLC; pg++) {
				write_pda_page(pda_base + ((((wl - 4) * XLC) + pg) << nand_info.pda_page_shift), 1, PROG_2ND_STEP);
			}
		}
	}
	// Program (2nd) last layer (last 4 wordline)
	for (wl = nand_page_num_slc() - 4; wl < nand_page_num_slc(); wl++) {
		//ncl_test_trace(LOG_ERR, 0, "Prog (2nd) wl %d\n", wl);
		for (pg = 0; pg < XLC; pg++) {
			write_pda_page(pda_base + (((wl * XLC) + pg) << nand_info.pda_page_shift), 1, PROG_2ND_STEP);
		}
	}

	// Read
	for (wl = 0; wl < nand_page_num_slc(); wl++) {
		ncl_test_trace(LOG_ERR, 0x552b, "Read wl %d\n", wl);
		for (pg = 0; pg < XLC; pg++) {
			read_pda_du(pda_base + (((wl * XLC) + pg) << nand_info.pda_page_shift), 1, 1, DU_FMT_USER_4K, false);
		}
	}
}
#endif

extern void ncl_cmd_cmpl(struct ncl_cmd_t *ncl_cmd);
#if !ENA_DEBUG_UART
// Code in this file will be used as console command in future
fast_code void ncl_cmd_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	ncl_cmd->flags |= NCL_CMD_COMPLETED_FLAG;
}
#endif


extern u16 nxt_spb;	///< next SPB

/* Backend only stress test
 * 1. Erase SPB
 * 2. Program SPB, each command write a interleave pages
 * 3. Read SPB, each command read a interleave pages
 */
u32 BE_PERF_CNT = 4;
#define READ_TEST_PER_PAGE 0
extern u32 fcmd_outstanding_cnt;
extern int cur_cpu_clk_freq;
fast_code void ncl_backend_only_perf_test_blk_type(bool slc_type)
{
	u32 spb_id = 2;
	pda_t* pda_list[BE_PERF_CNT];
	struct info_param_t* info_list;
	struct ncl_cmd_t ncl_cmd_list[BE_PERF_CNT];
	u32 *mem = NULL;
	dtag_t dtag;
	bm_pl_t* bm_pl_list;
	u32 i;
#if !READ_TEST_PER_PAGE
	u32 pg;
#endif
	u32 t1, t2;
	u32 size;
	u32 speed;
	u32 pg_per_blk;
	u32 len;

	if (slc_type) {
		pg_per_blk = nand_page_num_slc();
		len = nand_interleave_num() * DU_CNT_PER_PAGE;
	} else {
		pg_per_blk = nand_page_num();
		len = nand_interleave_num() * XLC * DU_CNT_PER_PAGE;
	}
	size = DU_CNT_PER_PAGE * nand_interleave_num() * pg_per_blk * (4096 / 4);// Divide by 4 to avoid overflow
	// Allocate resource
	for (i = 0; i < BE_PERF_CNT; i++) {
		pda_list[i] = sys_malloc(SLOW_DATA, sizeof(pda_t) * len);
		sys_assert(pda_list[i] != NULL);
	}
	info_list = sys_malloc(SLOW_DATA, sizeof(struct info_param_t) * len);
	bm_pl_list = sys_malloc(SLOW_DATA, sizeof(bm_pl_t) * len);
	sys_assert((info_list != NULL) && (bm_pl_list != NULL));
	dtag = dtag_get(DTAG_T_SRAM, (void*)&mem);
	if (mem == NULL) {
		sys_assert(0);
	}

	// Preparation
	for (i = 0; i < len; i++) {
		bm_pl_list[i].all = 0;
		bm_pl_list[i].pl.dtag = dtag.b.dtag;
		bm_pl_list[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}
	for (i = 0; i < BE_PERF_CNT; i++) {
		if (slc_type) {
			ncl_cmd_list[i].flags = NCL_CMD_COMPLETED_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_META_DISCARD;
		} else {
			ncl_cmd_list[i].flags = NCL_CMD_COMPLETED_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_META_DISCARD;
		}
		ncl_cmd_list[i].flags |= NCL_CMD_CACHE_PROGRAM_FLAG;
		ncl_cmd_list[i].addr_param.common_param.list_len = nand_interleave_num();
		ncl_cmd_list[i].addr_param.common_param.pda_list = pda_list[i];
		ncl_cmd_list[i].addr_param.common_param.info_list = info_list;
		ncl_cmd_list[i].du_format_no = DU_FMT_USER_4K;
		//ncl_cmd_list[i].op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd_list[i].op_type = INT_TABLE_WRITE_PRE_ASSIGN;// fix encode err when HCRC enable
		ncl_cmd_list[i].user_tag_list = bm_pl_list;
		ncl_cmd_list[i].completion = ncl_cmd_cmpl;
	}
	ncl_test_trace(LOG_ERR, 0x1fca, "start ncl_backend_only_perf_test at spb %d, interleave %d, is SLC %d.\n", spb_id, nand_interleave_num(), slc_type);

#if !READ_TEST_PER_PAGE
	// Measure erase/program
	t1 = *(vu32*)0xC0201044;
	// Erase
	ncl_cmd_list[BE_PERF_CNT - 1].op_code = NCL_CMD_OP_ERASE;
	ncl_cmd_list[BE_PERF_CNT - 1].flags &= ~NCL_CMD_COMPLETED_FLAG;
	ncl_cmd_list[BE_PERF_CNT - 1].status = 0;
	for (i = 0; i < nand_interleave_num(); i++) {
		pda_list[BE_PERF_CNT - 1][i] = (spb_id << nand_pda_block_shift()) + i * DU_CNT_PER_PAGE;
	}
	ncl_cmd_submit(&ncl_cmd_list[BE_PERF_CNT - 1]);

	// Program
	for (i = 0; i < BE_PERF_CNT; i++) {
		ncl_cmd_list[i].op_code = NCL_CMD_OP_WRITE;
	}
#if HAVE_TSB_SUPPORT
	if (!slc_type) {
		for (i = 0; i < BE_PERF_CNT; i++) {
			ncl_cmd_list[i].addr_param.common_param.list_len = XLC * nand_interleave_num();
		}
		u32 wl, lun;
		for (wl = 0; wl < pg_per_blk/3; wl++) {
				u32 idx;
				idx = wl % BE_PERF_CNT;
				while(NCL_CMD_PENDING(&ncl_cmd_list[idx])) {
					ficu_done_wait();
				}
				ncl_cmd_list[idx].status = 0;
				for (lun = 0; lun < nand_interleave_num()/nand_plane_num(); lun++) {
					for (pg = 0; pg < XLC; pg++) {
						for (i = 0; i < nand_plane_num(); i++) {
							pda_list[idx][lun * XLC * nand_plane_num() + pg * nand_plane_num() + i] = (spb_id << nand_pda_block_shift()) + (((wl * 3) + pg) * nand_interleave_num() + i) * DU_CNT_PER_PAGE + (lun << nand_info.pda_ch_shift);
						}
					}
				}
				ncl_cmd_list[wl % BE_PERF_CNT].flags &= ~NCL_CMD_COMPLETED_FLAG;
				ncl_cmd_submit(&ncl_cmd_list[wl % BE_PERF_CNT]);
		}
	} else
#endif
	{
		for (pg = 0; pg < pg_per_blk; pg++) {
			u32 idx;
			idx = pg % BE_PERF_CNT;
			while(NCL_CMD_PENDING(&ncl_cmd_list[idx])) {
				ficu_done_wait();
			}
			ncl_cmd_list[idx].status = 0;
			for (i = 0; i < nand_interleave_num(); i++) {
				pda_list[idx][i] = (spb_id << nand_pda_block_shift()) + (pg * nand_interleave_num() + i) * DU_CNT_PER_PAGE;
			}
			ncl_cmd_list[pg % BE_PERF_CNT].flags &= ~NCL_CMD_COMPLETED_FLAG;
			ncl_cmd_submit(&ncl_cmd_list[pg % BE_PERF_CNT]);
		}
	}
	while(fcmd_outstanding_cnt > 0) {
		ficu_done_wait();
	}
	t2 = *(vu32*)0xC0201044;
	speed = (size / 10) / (((t2 - t1) / 4) / cur_cpu_clk_freq);// GB, not Gib
	ncl_test_trace(LOG_ERR, 0x546b, "Seq write %d.%d%d GB/s\n", speed/100, (speed/10)%10, speed%10);
#endif
	// Measure read
	t1 = *(vu32*)0xC0201044;
#if !READ_TEST_PER_PAGE
	for (i = 0; i < BE_PERF_CNT; i++) {
		ncl_cmd_list[i].op_code = NCL_CMD_OP_READ;
		ncl_cmd_list[i].addr_param.common_param.list_len = nand_interleave_num() * DU_CNT_PER_PAGE;
		u32 j;
		for (j = 0; j < ncl_cmd_list[i].addr_param.common_param.list_len; j++) {
			if (slc_type) {
				ncl_cmd_list[i].addr_param.common_param.info_list[j].pb_type = NAL_PB_TYPE_SLC;
			} else {
				ncl_cmd_list[i].addr_param.common_param.info_list[j].pb_type = NAL_PB_TYPE_XLC;
			}
		}
	}

	for (pg = 0; pg < pg_per_blk; pg++) {
		u32 idx;
		idx = pg % BE_PERF_CNT;
		while(NCL_CMD_PENDING(&ncl_cmd_list[idx])) {
			ficu_done_wait();
		}
		ncl_cmd_list[idx].status = 0;
		for (i = 0; i < nand_interleave_num() * DU_CNT_PER_PAGE; i++) {
			pda_list[idx][i] = (spb_id << nand_pda_block_shift()) + pg * nand_interleave_num() * DU_CNT_PER_PAGE + i;
		}
		ncl_cmd_list[pg % BE_PERF_CNT].flags &= ~NCL_CMD_COMPLETED_FLAG;
		ncl_cmd_submit(&ncl_cmd_list[pg % BE_PERF_CNT]);
	}
#else
	u32 total_du_cnt = nand_interleave_num() * DU_CNT_PER_PAGE * pg_per_blk;
	u32 du_cnt_per_cmd = 32;//nand_interleave_num() * DU_CNT_PER_PAGE;
	u32 du_idx;
	u32 idx = 0;

	for (i = 0; i < BE_PERF_CNT; i++) {
		ncl_cmd_list[i].op_code = NCL_CMD_OP_READ;
		ncl_cmd_list[i].addr_param.common_param.list_len = du_cnt_per_cmd;
		u32 j;
		for (j = 0; j < ncl_cmd_list[i].addr_param.common_param.list_len; j++) {
			if (slc_type) {
				ncl_cmd_list[i].addr_param.common_param.info_list[j].pb_type = NAL_PB_TYPE_SLC;
			} else {
				ncl_cmd_list[i].addr_param.common_param.info_list[j].pb_type = NAL_PB_TYPE_XLC;
			}
		}
	}

	for (du_idx = 0; du_idx < total_du_cnt;) {
		idx = (idx < BE_PERF_CNT) ?  idx : (idx % BE_PERF_CNT);
		ncl_cmd_list[idx].status = 0;
		for (i = 0; i < du_cnt_per_cmd; i++) {
			pda_list[idx][i] = (spb_id << nand_pda_block_shift()) + du_idx + i;
		}
		//ncl_test_trace(LOG_ERR, 0, "check pda %x.\n", pda_list[idx][0]);
		ncl_cmd_submit(&ncl_cmd_list[idx]);
		while(fcmd_outstanding_cnt >= 1) {
			ficu_done_wait();
		}
		du_idx = du_idx + du_cnt_per_cmd;
		idx++;
	}
#endif
	while(fcmd_outstanding_cnt > 0) {
		ficu_done_wait();
	}
	t2 = *(vu32*)0xC0201044;
	speed = (size / 10) / (((t2 - t1) / 4) / cur_cpu_clk_freq);// GB, not Gib
	ncl_test_trace(LOG_ERR, 0xd066, "Seq read %d.%d%d GB/s\n", speed/100, (speed/10)%10, speed%10);

	u32 plane_step;
	if (nand_support_aipr()) {
		plane_step = 1;
	} else {
		plane_step = nand_plane_num();
	}
	// Measure random 4KB read
	t1 = *(vu32*)0xC0201044;
	for (i = 0; i < BE_PERF_CNT; i++) {
		ncl_cmd_list[i].op_code = NCL_CMD_OP_READ;
		ncl_cmd_list[i].addr_param.common_param.list_len = nand_interleave_num() / plane_step;
		u32 j;
		for (j = 0; j < ncl_cmd_list[i].addr_param.common_param.list_len; j++) {
			if (slc_type) {
				ncl_cmd_list[i].addr_param.common_param.info_list[j].pb_type = NAL_PB_TYPE_SLC;
			} else {
				ncl_cmd_list[i].addr_param.common_param.info_list[j].pb_type = NAL_PB_TYPE_XLC;
			}
		}
	}

	for (pg = 0; pg < pg_per_blk; pg++) {
		u32 idx;
		idx = pg % BE_PERF_CNT;
		while(NCL_CMD_PENDING(&ncl_cmd_list[idx])) {
			ficu_done_wait();
		}
		ncl_cmd_list[idx].status = 0;
		for (i = 0; i < nand_interleave_num() / plane_step; i++) {
			pda_list[idx][i] = (spb_id << nand_pda_block_shift()) + (pg * nand_interleave_num() + i * plane_step) * DU_CNT_PER_PAGE;
		}
		ncl_cmd_list[pg % BE_PERF_CNT].flags &= ~NCL_CMD_COMPLETED_FLAG;
		ncl_cmd_submit(&ncl_cmd_list[pg % BE_PERF_CNT]);
	}
	while(fcmd_outstanding_cnt > 0) {
		ficu_done_wait();
	}
	t2 = *(vu32*)0xC0201044;
	size /= DU_CNT_PER_PAGE * plane_step;
	speed = (size) / (((t2 - t1) / 4)  / cur_cpu_clk_freq);// GB, not Gib
	u32 iops = (size / 4096 * 1000) / (((t2 - t1) / 4) / cur_cpu_clk_freq);
	ncl_test_trace(LOG_ERR, 0x5c54, "RR4K %d.%d%d GB/s, IOPS %dK\n", speed/1000, (speed/100)%10, (speed/10)%10, iops);
	sys_free(SLOW_DATA, pda_list[1]);
	sys_free(SLOW_DATA, pda_list[0]);
	sys_free(SLOW_DATA, info_list);
	sys_free(SLOW_DATA, bm_pl_list);
	dtag_put(DTAG_T_SRAM, dtag);

}

fast_code void ncl_backend_only_perf_test(void)
{
	ncl_backend_only_perf_test_blk_type(1);
	ncl_backend_only_perf_test_blk_type(0);
}

/*
 * Submit random read, each ncl_cmd 1 DU, to test cache read in future
 */
void ncl_random_read_perf_test(void)
{
	struct ncl_cmd_t** ncl_cmd_list;
	u32 i;
	u32 cnt;
	u32 interleave;
	u32 step;
	u32 du_cnt;

	if (nand_support_aipr()) {
		interleave = nand_interleave_num();
		step = DU_CNT_PER_PAGE;
	} else {
		interleave = nand_interleave_num() / nand_plane_num();
		step = DU_CNT_PER_PAGE * nand_plane_num();
	}

	cnt = interleave * 3;
	ncl_cmd_list = sys_malloc(FAST_DATA, sizeof(struct ncl_cmd_t*) * cnt);
	sys_assert(ncl_cmd_list != NULL);


	for (du_cnt = 1; du_cnt < nand_plane_num() * DU_CNT_PER_PAGE; du_cnt <<= 1) {
		for (i = 0; i < cnt; i++) {
			while(1) {
				ncl_cmd_list[i] = ncl_cmd_alloc(NCL_CMD_OP_READ, du_cnt);
				if (ncl_cmd_list[i] == NULL) {
					ficu_done_wait();
				} else {
					ncl_cmd_list[i]->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
					ncl_cmd_list[i]->op_type = INT_TABLE_READ_PRE_ASSIGN;
					ncl_cmd_list[i]->completion = ncl_cmd_cmpl;
					ncl_cmd_list[i]->flags |= NCL_CMD_COMPLETED_FLAG;
					break;
				}
			}
		}
		pda_t* pda_list;
		pda_t pda_base = 0x10 << nand_info.pda_block_shift;
		u32 t1, t2;

		t1 = *(vu32*)0xC0201044;
		for (i = 0; i < nand_page_num_slc() * interleave; i++) {
			u32 idx;
			u32 j;
			idx = i % cnt;
			while(NCL_CMD_PENDING(ncl_cmd_list[idx])) {
				ficu_done_wait();
			}
			pda_list = ncl_cmd_list[idx]->addr_param.common_param.pda_list;
			for (j = 0; j < du_cnt; j++) {
				pda_list[j] = pda_base + i * step + j;
			}
			ncl_cmd_list[idx]->flags &= ~NCL_CMD_COMPLETED_FLAG;
			ncl_cmd_submit(ncl_cmd_list[idx]);
		}

#if HAVE_CACHE_READ
		extern void ncl_tasks_clear(void);
		ncl_tasks_clear();
#endif

		while(fcmd_outstanding_cnt > 0) {
			ficu_done_wait();
		}
		t2 = *(vu32*)0xC0201044;
		u32 size;
		u32 speed;
		u32 iops;
		size = nand_page_num_slc() * interleave * 4096;
		speed = (size * du_cnt) / ((t2 - t1) / cur_cpu_clk_freq);// GB, not Gib
		iops = (size / 4096 * 1000) / ((t2 - t1) / cur_cpu_clk_freq);
		ncl_test_trace(LOG_ERR, 0x746a, "RR%dK %d.%d%d GB/s, IOPS %dK\n", du_cnt*4, speed/1000, (speed/100)%10, (speed/10)%10, iops);
		for (i = 0; i < cnt; i++) {
			ncl_cmd_free(ncl_cmd_list[i]);
		}
	}
	sys_free(FAST_DATA, ncl_cmd_list);
}
/*! @} */
void self_stress_erd_ficu_abort_test(void)
{
	u32 spb_id = 4;
	pda_t* pda_list;
	struct ncl_cmd_t* ncl_cmd;
	dec_top_ctrs_reg3_t erd_reg;
	u32 max_ch_erd;
	erd_reg.all = dec_top_readl(DEC_TOP_CTRS_REG3);
	max_ch_erd = erd_reg.b.eccu_erd_max_num_ch;

	// Force mdec/pdec fail
	u32 loop = 0;

	// Generate read ncl_cmd with random pda_list
	u32 du_per_spb = nand_interleave_num() * nand_page_num_slc() * DU_CNT_PER_PAGE;
	u32 list_len;
	u32 ptr;
	bool erd = true;
	u8 op_type = NCL_CMD_OP_READ_ERD;

#if defined(FPGA)
	extern soc_cfg_reg1_t soc_cfg_reg1;
	if (soc_cfg_reg1.b.ddr == 0) {
		erd = false;
		op_type = NCL_CMD_OP_READ;
	}
#endif

	while(1) {
		loop++;
		if ((loop % 100) == 0) {
			//ncb_ncl_trace(LOG_INFO, 0, "loop %d", loop);
			ncl_test_trace(LOG_ERR, 0x539f, "loop %d\n", loop);
		}
		list_len = 1;
		while(1) {
			ncl_cmd = ncl_cmd_alloc(op_type, list_len);
			if (ncl_cmd == NULL) {
				ficu_done_wait();
			} else {
				break;
			}
		}
		ncl_cmd->completion = ncl_cmd_erd_cmpl;
		if (erd) {
			pda_list = &ncl_cmd->addr_param.rapid_du_param.pda;
			ncl_cmd->addr_param.rapid_du_param.list_len = list_len;
		} else {
			pda_list = ncl_cmd->addr_param.common_param.pda_list;
		}

		ptr = random_generator() % du_per_spb;
		pda_list[0] = (spb_id << nand_pda_block_shift()) + ptr;
		ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
		ncl_cmd->flags |= NCL_CMD_IGNORE_ERR_FLAG;
		ncl_cmd->du_format_no = DU_FMT_USER_4K;
		ncl_cmd_submit(ncl_cmd);
		u32 cnt;
		if (erd) {
			extern u8* erd_cnt_tbl_by_ch;
			cnt = erd_cnt_tbl_by_ch[0];
		} else {
			cnt = fcmd_outstanding_cnt;
		}
		if (cnt >= max_ch_erd) {
			u32 delay = random_generator();
			if (erd) {
				delay &= 2047;
			} else {
				delay &= 255;
			}
			delay_us(delay);
			extern void ficu_abort(void);
			ficu_abort();
			ficu_done_wait();
		}
	}
}
