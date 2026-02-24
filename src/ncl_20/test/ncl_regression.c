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

/*! \file ncl_regression.c
 * @brief test ncl read/program/erase function
 *
 * \addtogroup test
 * \defgroup ncl_regression
 * \ingroup test
 *
 * @{
 */

#include "eccu.h"
#include "ncl.h"
#include "nand_cfg.h"

#define __FILEID__ nclr
#include "trace.h"
/*!
 * @brief Erase block
 *
 * @param[in] pda			erase PDA
 * @param[in] pb_type		erase PDA's PB type
 *
 * @return		difference value of two timer
 */

void ncl_block_erase(pda_t pda, nal_pb_type_t pb_type)
{
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info;

	ncl_test_trace(LOG_ERR, 0xead1, "Erase block (pda 0x%x)\n", pda);
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd.op_code = NCL_CMD_OP_ERASE;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = &info;
	ncl_cmd.completion = NULL;
	ncl_cmd_submit(&ncl_cmd);
}

void ncl_page_program(pda_t pda, nal_pb_type_t pb_type, dtag_t* dtag_list, struct du_meta_fmt* meta)
{
	u32 i;
	struct ncl_cmd_t ncl_cmd;
	bm_pl_t bm_pl[DU_CNT_PER_PAGE];
	struct info_param_t info;

	if (dtag_list == NULL) {
		ncl_test_trace(LOG_ERR, 0x5e67, "no dtag\n");
		return;
	}

	//ncl_test_trace(LOG_ERR, 0, "Program ch %d, ce %d, lun %d, plane %d, block 0x%d, pg %d, pda 0x%x\n", ch, ce, lun, pl, blk, pg, pda);
	ncl_cmd.op_code = NCL_CMD_OP_WRITE;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = &info;
	info.xlc.slc_idx = 0;
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd.du_format_no = DU_FMT_USER_4K;
	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.completion = NULL;
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		bm_pl[i].all = 0;
		//dtag_list[i].pl.btag = 0;
		bm_pl[i].pl.dtag = dtag_list[i].b.dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}

	ncl_cmd_submit(&ncl_cmd);
}

void ncl_page_read(pda_t pda, nal_pb_type_t pb_type, dtag_t* dtag_list, struct du_meta_fmt* meta)
{
	u32 i;
	pda_t pda_list[DU_CNT_PER_PAGE];
	struct ncl_cmd_t ncl_cmd;
	bm_pl_t bm_pl[DU_CNT_PER_PAGE];
	struct info_param_t info_list[DU_CNT_PER_PAGE];

	if (dtag_list == NULL) {
		ncl_test_trace(LOG_ERR, 0xecf2, "no dtag\n");
		return;
	}

	//ncl_test_trace(LOG_ERR, 0, "Read ch %d, ce %d, lun %d, plane %d, block 0x%d, pg %d\n", ch, ce, lun, pl, blk, pg);
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		pda_list[i] = pda + i;
		info_list[i].pb_type = NAL_PB_TYPE_SLC;
	}

	ncl_cmd.op_code = NCL_CMD_OP_READ;
	ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
	ncl_cmd.addr_param.common_param.list_len = DU_CNT_PER_PAGE;
	ncl_cmd.addr_param.common_param.pda_list = pda_list;
	ncl_cmd.addr_param.common_param.info_list = info_list;
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd.du_format_no = DU_FMT_USER_4K;
	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.completion = NULL;
    #if NCL_FW_RETRY
	ncl_cmd.retry_step = default_read;
    #endif

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		bm_pl[i].all = 0;
		//dtag_list[i].pl.btag = 0;
		bm_pl[i].pl.dtag = dtag_list[i].b.dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}
	ncl_cmd_submit(&ncl_cmd);
}

void ncl_block_write(pda_t pda, nal_pb_type_t pb_type)
{
	u32 i, j;
	u32 pg, pg_per_blk;
	dtag_t dtag_list[DU_CNT_PER_PAGE];
	struct du_meta_fmt* meta;
	u32* data[DU_CNT_PER_PAGE];
	u32 cnt;
	bool err = false;

	cnt = dtag_get_bulk(DU_CNT_PER_PAGE, dtag_list);
	if (cnt != DU_CNT_PER_PAGE) {
		ncl_test_trace(LOG_ERR, 0xa419, "no dtag\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}

	meta = sys_malloc(SLOW_DATA, sizeof(struct du_meta_fmt) * DU_CNT_PER_PAGE);
	if (meta == NULL) {
		ncl_test_trace(LOG_ERR, 0x1a19, "no enough meta\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		data[i] = dtag2mem(dtag_list[i].dtag);
	}

	ncl_test_trace(LOG_ERR, 0x29c4, "Program block (pda 0x%x)", pda);
	pg_per_blk = nand_page_num();
	if (pb_type == NAL_PB_TYPE_SLC) {
		pg_per_blk /= nand_bits_per_cell();
	}
	for (pg = 0; pg < pg_per_blk; pg++) {
		if ((pg * 16) % pg_per_blk == 0) {
			ncl_test_trace(LOG_ERR, 0xbfa8, ".");
		}
		pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);
		pda |= pg << nand_info.pda_page_shift;
		for (i = 0; i < DU_CNT_PER_PAGE; i++) {
			for (j = 0; j < (NAND_DU_SIZE/sizeof(u32)); j++) {
				data[i][j] = pda + i;
			}
			meta[i].hlba = pda + i;
			meta[i].seed_index = pda + i;
			meta[i].meta0 = pda + i;
			meta[i].meta1 = pda + i;
		}
		//ncl_test_trace(LOG_ERR, 0, "page %x write\n", pg);
		ncl_page_program(pda, pb_type, dtag_list, meta);
	}
	ncl_test_trace(LOG_ERR, 0x4969, "\n");
	dtag_put_bulk(DU_CNT_PER_PAGE, dtag_list);
	sys_free(SLOW_DATA, meta);
}


void ncl_block_read(pda_t pda, nal_pb_type_t pb_type)
{
	u32 i, j;
	u32 pg, pg_per_blk;
	dtag_t dtag_list[DU_CNT_PER_PAGE];
	struct du_meta_fmt* meta;
	u32* data[DU_CNT_PER_PAGE];
	u32 cnt;
	bool err = false;

	cnt = dtag_get_bulk(DU_CNT_PER_PAGE, dtag_list);
	if (cnt != DU_CNT_PER_PAGE) {
		ncl_test_trace(LOG_ERR, 0x786b, "no dtag\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}
	meta = sys_malloc(SLOW_DATA, sizeof(struct du_meta_fmt) * DU_CNT_PER_PAGE);
	if (meta == NULL) {
		ncl_test_trace(LOG_ERR, 0xfbf8, "no enough meta\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		data[i] = dtag2mem(dtag_list[i].dtag);
	}

	pg_per_blk = nand_page_num();
	if (pb_type == NAL_PB_TYPE_SLC) {
		pg_per_blk /= nand_bits_per_cell();
	}

	ncl_test_trace(LOG_ERR, 0x32fc, "Read block (pda 0x%x)", pda);
	for (pg = 0; pg < pg_per_blk; pg++) {
		if ((pg * 16) % pg_per_blk == 0) {
			ncl_test_trace(LOG_ERR, 0xbf39, ".");
		}
		pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);
		pda |= pg << nand_info.pda_page_shift;
		ncl_page_read(pda, pb_type, dtag_list, meta);
		//ncl_test_trace(LOG_ERR, 0, "page pda 0x%x read data: ", pda);
		for (i = 0; i < DU_CNT_PER_PAGE; i++) {
			//ncl_test_trace(LOG_ERR, 0, "0x%x ", data[i][0]);
			if (data[i][0] != pda + i) {
				err = true;
				//break;
			}
		}
		//ncl_test_trace(LOG_ERR, 0, "\n");
	}
	ncl_test_trace(LOG_ERR, 0x999b, "\n");
	ncl_test_trace(LOG_ERR, 0x2b58, "%s test %s\n", (pb_type == NAL_PB_TYPE_SLC) ? "SLC" : "XLC", err ? "fail" : "pass");
	dtag_put_bulk(DU_CNT_PER_PAGE, dtag_list);
	sys_free(SLOW_DATA, meta);
}

void ncl_slc_block_test(pda_t pda)
{
	ncl_block_erase(pda, NAL_PB_TYPE_SLC);
	ncl_block_write(pda, NAL_PB_TYPE_SLC);
	ncl_block_read(pda, NAL_PB_TYPE_SLC);
}

void ncl_tlc_block_test(pda_t pda)
{
	ncl_block_erase(pda, NAL_PB_TYPE_XLC);
	ncl_block_write(pda, NAL_PB_TYPE_XLC);
	ncl_block_read(pda, NAL_PB_TYPE_XLC);
}

void ncl_mp_block_erase(pda_t pda, nal_pb_type_t pb_type)
{
	struct ncl_cmd_t ncl_cmd;
	pda_t pda_list[4];
	struct info_param_t info[4];
	u32 i;

	for (i = 0; i < nand_plane_num(); i++) {
		pda_list[i] = pda + i * DU_CNT_PER_PAGE;
	}
	ncl_test_trace(LOG_ERR, 0x4dde, "MP erase block (pda 0x%x)\n", pda);
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd.op_code = NCL_CMD_OP_ERASE;
	ncl_cmd.completion = NULL;
	ncl_cmd.addr_param.common_param.list_len = nand_plane_num();
	ncl_cmd.addr_param.common_param.pda_list = pda_list;
	ncl_cmd.addr_param.common_param.info_list = info;
	ncl_cmd_submit(&ncl_cmd);
}

void ncl_mp_page_program(pda_t pda, nal_pb_type_t pb_type, dtag_t* dtag_list, struct du_meta_fmt* meta)
{
	u32 i;
	struct ncl_cmd_t ncl_cmd;
	bm_pl_t bm_pl[DU_CNT_PER_PAGE*4];
	pda_t pda_list[4];
	struct info_param_t info[4];

	if (dtag_list == NULL) {
		ncl_test_trace(LOG_ERR, 0x416f, "no dtag\n");
		return;
	}

	for (i = 0; i < nand_plane_num(); i++) {
		pda_list[i] = pda + i * DU_CNT_PER_PAGE;
		info[i].xlc.slc_idx = 0;
	}

	//ncl_test_trace(LOG_ERR, 0, "Program ch %d, ce %d, lun %d, plane %d, block 0x%d, pg %d, pda 0x%x\n", ch, ce, lun, pl, blk, pg, pda);
	ncl_cmd.op_code = NCL_CMD_OP_WRITE;
	ncl_cmd.completion = NULL;
	ncl_cmd.addr_param.common_param.list_len = nand_plane_num();
	ncl_cmd.addr_param.common_param.pda_list = pda_list;
	ncl_cmd.addr_param.common_param.info_list = info;
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd.du_format_no = DU_FMT_USER_4K;
	ncl_cmd.user_tag_list = bm_pl;
	for (i = 0; i < DU_CNT_PER_PAGE*nand_plane_num(); i++) {
		bm_pl[i].all = 0;
		//dtag_list[i].pl.btag = 0;
		bm_pl[i].pl.dtag = dtag_list[i].b.dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}

	ncl_cmd_submit(&ncl_cmd);
}

void ncl_mp_page_read(pda_t pda, nal_pb_type_t pb_type, dtag_t* dtag_list, struct du_meta_fmt* meta)
{
	u32 i;
	pda_t pda_list[DU_CNT_PER_PAGE*4];
	struct ncl_cmd_t ncl_cmd;
	bm_pl_t bm_pl[DU_CNT_PER_PAGE*4];
	struct info_param_t info_list[DU_CNT_PER_PAGE*4];

	if (dtag_list == NULL) {
		ncl_test_trace(LOG_ERR, 0x4a98, "no dtag\n");
		return;
	}

	//ncl_test_trace(LOG_ERR, 0, "Read ch %d, ce %d, lun %d, plane %d, block 0x%d, pg %d\n", ch, ce, lun, pl, blk, pg);
	for (i = 0; i < DU_CNT_PER_PAGE*nand_plane_num(); i++) {
		pda_list[i] = pda + i;
		info_list[i].pb_type = NAL_PB_TYPE_SLC;
	}

	ncl_cmd.op_code = NCL_CMD_OP_READ;
	ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
	ncl_cmd.completion = NULL;
	ncl_cmd.addr_param.common_param.list_len = DU_CNT_PER_PAGE*nand_plane_num();
	ncl_cmd.addr_param.common_param.pda_list = pda_list;
	ncl_cmd.addr_param.common_param.info_list = info_list;
	if (pb_type == NAL_PB_TYPE_XLC) {
		ncl_cmd.flags = NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd.du_format_no = DU_FMT_USER_4K;
	ncl_cmd.user_tag_list = bm_pl;
    #if NCL_FW_RETRY
	ncl_cmd.retry_step = default_read;
    #endif

	for (i = 0; i < DU_CNT_PER_PAGE*nand_plane_num(); i++) {
		bm_pl[i].all = 0;
		//dtag_list[i].pl.btag = 0;
		bm_pl[i].pl.dtag = dtag_list[i].b.dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
	}
	ncl_cmd_submit(&ncl_cmd);
}

void ncl_mp_block_write(pda_t pda, nal_pb_type_t pb_type)
{
	u32 i, j;
	u32 pg, pg_per_blk;
	dtag_t dtag_list[DU_CNT_PER_PAGE*4];
	struct du_meta_fmt* meta;
	u32* data[DU_CNT_PER_PAGE*4];
	u32 cnt;
	bool err = false;

	cnt = dtag_get_bulk(DU_CNT_PER_PAGE*nand_plane_num(), dtag_list);
	if (cnt != DU_CNT_PER_PAGE*nand_plane_num()) {
		ncl_test_trace(LOG_ERR, 0x0c60, "no dtag\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}

	meta = sys_malloc(SLOW_DATA, sizeof(struct du_meta_fmt) * DU_CNT_PER_PAGE*nand_plane_num());
	if (meta == NULL) {
		ncl_test_trace(LOG_ERR, 0xb98b, "no enough meta\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}

	for (i = 0; i < DU_CNT_PER_PAGE*nand_plane_num(); i++) {
		data[i] = dtag2mem(dtag_list[i].dtag);
	}

	ncl_test_trace(LOG_ERR, 0x6e48, "MP program block (pda 0x%x)", pda);
	pg_per_blk = nand_page_num();
	if (pb_type == NAL_PB_TYPE_SLC) {
		pg_per_blk /= nand_bits_per_cell();
	}
	for (pg = 0; pg < pg_per_blk; pg++) {
		if ((pg * 16) % pg_per_blk == 0) {
			ncl_test_trace(LOG_ERR, 0x161f, ".");
		}
		pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);
		pda |= pg << nand_info.pda_page_shift;
		for (i = 0; i < DU_CNT_PER_PAGE*nand_plane_num(); i++) {
			for (j = 0; j < (NAND_DU_SIZE/sizeof(u32)); j++) {
				data[i][j] = pda + i;
			}
			meta[i].hlba = pda + i;
			meta[i].seed_index = pda + i;
			meta[i].meta0 = pda + i;
			meta[i].meta1 = pda + i;
		}
		//ncl_test_trace(LOG_ERR, 0, "page %x write\n", pg);
		ncl_mp_page_program(pda, pb_type, dtag_list, meta);
	}
	ncl_test_trace(LOG_ERR, 0xb4ff, "\n");
	dtag_put_bulk(DU_CNT_PER_PAGE, dtag_list);
	sys_free(SLOW_DATA, meta);
}

void ncl_mp_block_read(pda_t pda, nal_pb_type_t pb_type)
{
	u32 i;
	u32 pg, pg_per_blk;
	dtag_t dtag_list[DU_CNT_PER_PAGE*4];
	struct du_meta_fmt* meta;
	u32* data[DU_CNT_PER_PAGE*4];
	u32 cnt;
	bool err = false;

	cnt = dtag_get_bulk(DU_CNT_PER_PAGE*nand_plane_num(), dtag_list);
	if (cnt != DU_CNT_PER_PAGE*nand_plane_num()) {
		ncl_test_trace(LOG_ERR, 0xa524, "no dtag\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}
	meta = sys_malloc(SLOW_DATA, sizeof(struct du_meta_fmt) * DU_CNT_PER_PAGE*nand_plane_num());
	if (meta == NULL) {
		ncl_test_trace(LOG_ERR, 0x4722, "no enough meta\n");
		dtag_put_bulk(cnt, dtag_list);
		return;
	}

	for (i = 0; i < DU_CNT_PER_PAGE*nand_plane_num(); i++) {
		data[i] = dtag2mem(dtag_list[i].dtag);
	}

	pg_per_blk = nand_page_num();
	if (pb_type == NAL_PB_TYPE_SLC) {
		pg_per_blk /= nand_bits_per_cell();
	}

	ncl_test_trace(LOG_ERR, 0xf934, "MP read block (pda 0x%x)", pda);
	for (pg = 0; pg < pg_per_blk; pg++) {
		if ((pg * 16) % pg_per_blk == 0) {
			ncl_test_trace(LOG_ERR, 0xe53c, ".");
		}
		pda &= ~(nand_info.pda_page_mask << nand_info.pda_page_shift);
		pda |= pg << nand_info.pda_page_shift;
		ncl_mp_page_read(pda, pb_type, dtag_list, meta);
		//ncl_test_trace(LOG_ERR, 0, "page pda 0x%x read data: ", pda);
		for (i = 0; i < DU_CNT_PER_PAGE*nand_plane_num(); i++) {
			//ncl_test_trace(LOG_ERR, 0, "0x%x ", data[i][0]);
			if (data[i][0] != pda + i) {
				err = true;
				//break;
			}
		}
		//ncl_test_trace(LOG_ERR, 0, "\n");
	}
	ncl_test_trace(LOG_ERR, 0xbc23, "\n");
	ncl_test_trace(LOG_ERR, 0xff1f, "%s MP test %s\n", (pb_type == NAL_PB_TYPE_SLC) ? "SLC" : "XLC", err ? "fail" : "pass");
	dtag_put_bulk(DU_CNT_PER_PAGE, dtag_list);
	sys_free(SLOW_DATA, meta);
}


void ncl_mp_test(void)
{
	ncl_mp_block_erase(0x80000, NAL_PB_TYPE_SLC);
	ncl_mp_block_write(0x80000, NAL_PB_TYPE_SLC);
	ncl_mp_block_read(0x80000, NAL_PB_TYPE_SLC);
}


slow_code void ncl_regression(void)
{
	ncl_slc_block_test(0x50000);
	ncl_tlc_block_test(0x50000);

	while(1);
}

/*! @} */
