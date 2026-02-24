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

//=============================================================================
//
/*! \file ncl_todo.c
 * @brief Temp NCL file
 *
 * \addtogroup ncl
 * \defgroup ncl
 * \ingroup ncl
 * @{
 * Temp NCL functions to be supported etc
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "ncl.h"
#include "eccu.h"
#include "ficu.h"
#include "ncl_err.h"
#include "ncl_cmd.h"
#include "nand.h"
#include "srb.h"
#include "ncb_ndcu_register.h"
#include "ncb_ficu_register.h"
#include "ndcu_reg_access.h"
#include "ficu_reg_access.h"

extern u32 fcmd_outstanding_cnt;
extern pda_t nal_rda_to_pda(rda_t *rda);

/*!
 * @brief MR mode operation
 *
 * @param rda_list RDA list
 * @param op 	operation type
 * @param bm_pl	BM payload
 * @param count	count
 *
 * @return	not used
 */
int ncl_access_mr(rda_t *rda_list, enum ncl_cmd_op_t op, bm_pl_t *bm_pl, u32 count)
{
        
	sys_assert(eccu_cmf_is_rom());
        
	int i;
	struct ncl_cmd_t ncl_cmd;
	pda_t pda_list[32];
	struct info_param_t info_list[32];
	sys_assert(count == 1);
	for (i = 0; i < count; i++) {
		sys_assert(rda_list[i].pb_type == NAL_PB_TYPE_SLC);// Do you really want TLC mode in this path???
		pda_list[i] = nal_rda_to_pda(rda_list + i);
	}

	ncl_cmd.addr_param.common_param.info_list = info_list;
	ncl_cmd.addr_param.common_param.list_len = count;
	ncl_cmd.addr_param.common_param.pda_list = pda_list;

	memset(info_list, 0, sizeof(struct info_param_t) * count);

	for (i = 0; i < count; i++) {
		info_list[i].pb_type = NAL_PB_TYPE_SLC;
		rda_list[i].pb_type = NAL_PB_TYPE_SLC;
	}

	ncl_cmd.caller_priv = NULL;
	ncl_cmd.completion = NULL;
	ncl_cmd.flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_META_DISCARD;
	ncl_cmd.op_code = op;
	ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;

	if (op == NCL_CMD_OP_WRITE) {
		struct du_meta_fmt* meta = NULL;
		switch(bm_pl[i].pl.type_ctrl & 3) {
		case META_SRAM_DTAG:
			meta = (struct du_meta_fmt*)(ficu_readl(FICU_META_DTAG_MAP_SRAM_BASE_ADDR_REG) << 4);
			break;
		default:
			sys_assert(0);
			break;
		}
		for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++) {
			bm_pl[i].pl.type_ctrl |= DTAG_QID_DROP;
			if (bm_pl[i].pl.dtag != WVTAG_ID)
				memset(meta + bm_pl[i].pl.dtag, 0, sizeof(struct du_meta_fmt));
		}
	}

	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.du_format_no = DU_FMT_MR_4K;
	ncl_cmd.status = 0;
	ncl_cmd_submit(&ncl_cmd);
	return ncl_cmd.status;
}

/*!
 * @brief Enter MR mode
 * Legacy function from Shasta, maybe obsoleted
 * Previously 2KB & 4KB DU format cannot be used together, CMF need to be switched
 *
 * @return	not used
 */
share_data_zi volatile bool stop_host_ncl;
bool ncl_enter_mr_mode(void)
{
    stop_host_ncl = true;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	eccu_switch_du_fmt(DU_FMT_MR_4K);
       
	return true;
}

/*!
 * @brief Exit MR mode
 * Legacy function from Shasta, maybe obsoleted
 *
 * @return	not used
 */
void ncl_leave_mr_mode(void)
{
    stop_host_ncl = false;
	while(fcmd_outstanding_cnt != 0) {
		ficu_done_wait();
	}
	eccu_switch_du_fmt(DU_FMT_USER_4K);
        
	return;
}

/*!
 * @brief NCL cmd submission path for SRB
 *
 * @param rda_list RDA list
 * @param op 	operation type
 * @param bm_pl	BM payload
 * @param count	count
 * @param du_format DU format
 * @param stripe_id Stripe_id for RAID operation
 *
 * @return	not used
 */
int ncl_cmd_simple_submit(rda_t *rda_list, enum ncl_cmd_op_t op,
	bm_pl_t *bm_pl, u32 count, int du_format, int stripe_id)
{
	sys_assert(eccu_cmf_is_rom());
	int i;
	struct ncl_cmd_t ncl_cmd;
	pda_t pda_list[32];
	struct info_param_t info_list[32];
	sys_assert(count == 1);
	for (i = 0; i < count; i++) {
		sys_assert(rda_list[i].pb_type == NAL_PB_TYPE_SLC);// Do you really want TLC mode in this path???
		pda_list[i] = nal_rda_to_pda(rda_list + i);
	}

	ncl_cmd.addr_param.common_param.info_list = info_list;
	ncl_cmd.addr_param.common_param.list_len = count;
	ncl_cmd.addr_param.common_param.pda_list = pda_list;

	memset(info_list, 0, sizeof(struct info_param_t) * count);
	for (i = 0; i < count; i++)
		 info_list[0].pb_type = NAL_PB_TYPE_SLC;

	ncl_cmd.caller_priv = NULL;
	ncl_cmd.completion = NULL;
	ncl_cmd.flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_META_DISCARD;
	ncl_cmd.op_code = op;
	ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;

	if (op == NCL_CMD_OP_WRITE) {
		for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++)
			bm_pl[i].pl.type_ctrl |= DTAG_QID_DROP;
	}

	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.du_format_no = DU_FMT_MR_4K;
	ncl_cmd.status = 0;
	ncl_cmd_submit(&ncl_cmd);
	return ncl_cmd.status;
}

/*!
 * @brief Get first available channel & target
 *
 * @param ch	channel pointer
 * @param dev	target pointer
 *
 * @return	not used
 */
void nal_get_first_dev(u32 *ch, u32 *dev)
{
	ficu_ch_mapping_reg_t ch_map;
	nf_ce_remap_reg0_t ce_map;

	ch_map.all = ficu_readl(FICU_CH_MAPPING_REG);
	*ch = ch_map.b.ficu_phy_ch0_map;
	ce_map.all = ndcu_readl(NF_CE_REMAP_REG0 + (*ch) * sizeof(u32));
	*dev = ce_map.b.nf_ce0_map_ch0;
}

void ncl_set_ncb_clk(u32 clk_type, u16 new_clk)
{
	sys_assert(0);
}
/*! @} */
