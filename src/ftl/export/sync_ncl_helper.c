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
/*! \file sync_ncl_helper.h
 * @brief sync interface of NCL
 *
 * \addtogroup sync_ncl_helper
 * @{
 *
 */
#include "sect.h"
#include "sync_ncl_helper.h"
#include "ncl_cmd.h"
#include "ftl_export.h"
#include "dtag.h"
#include "ipc_api.h"
#include "mpc.h"
#include "ftl_remap.h"

/*! \cond PRIVATE */
#define __FILEID__ sncl
#include "trace.h"
/*! \endcond */

fast_data_zi ftl_cache_remap_tbl_t _sync_cache_remap_tbl;	///< cached remap table for sync ncl helper
share_data volatile enum du_fmt_t host_du_fmt;

/*!
 * @brief check if pointer can be accessed in remote CPU
 *
 * @param ptr	pointer to be checked
 *
 * @return	not used
 */
static inline void ptr_chk(void *ptr)
{
	u32 t = (u32) ptr;

	if (SRAM_BASE <= t && t < SRAM_BASE + SRAM_SIZE)
		return;

	if (BTCM_SH_BASE <= t && t < BTCM_SH_BASE + BTCM_SH_SIZE)
		return;

	ftl_apl_trace(LOG_ERR, 0xabc1, "%x\n", ptr);
	panic("stop");
}

/*!
 * @brief api to get remapped pda by pda
 *
 * @param pda	pda to be remapped
 *
 * @return	return accessible PDA
 */
static inline pda_t ncl_sync_get_remap(pda_t pda)
{
	return ftl_get_cached_remap_pda(pda, &_sync_cache_remap_tbl);
}

/*!
 * @brief api to remap a pda list
 *
 * @param pda	pda list to be remapped
 * @param cnt	list length
 *
 * @return	not used
 */
static inline void ncl_sync_get_remap_list(pda_t *pda, u32 cnt)
{
	ftl_get_cached_remap_pda_list(pda, cnt, &_sync_cache_remap_tbl);
}

fast_code void ncl_sync_reinit_remap(void)
{
	_sync_cache_remap_tbl.cache_spb = INV_SPB_ID;
}

init_code bool ncl_spb_sync_erase(spb_id_t spb_id, nal_pb_type_t type,
		struct info_param_t** info_list)
{
	u32 i;
	bool ret;
	u32 width;
	pda_t pda;
	struct ncl_cmd_t *ncl_cmd;
	pda_t *pda_list;
	struct info_param_t *info;

	width = shr_nand_info.interleave;
	ncl_cmd = share_malloc(sizeof(*ncl_cmd));
	pda_list = share_malloc(sizeof(pda_t) * width);
	info = share_malloc(sizeof(struct info_param_t) * width);

#ifdef SKIP_MODE
	pda = nal_make_pda(spb_id, 0);
#else
	pda = ftl_remap_pda(nal_make_pda(spb_id, 0));
#endif

	for (i = 0; i < width; i++) {
		//pda_list[i] = ncl_sync_get_remap(pda);  //NOW ncl_spb_sync_erase() only use in ftl_scan_defect. If want to use in the other place, need to check this remap.
		pda_list[i] = pda;		
		pda += DU_CNT_PER_PAGE;
		info[i].pb_type = type;
		info[i].status = ficu_err_good;
	}

	ncl_cmd->completion = NULL;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG;
	if (type == NAL_PB_TYPE_SLC)
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;

	ncl_cmd->addr_param.common_param.list_len = width;
	ncl_cmd->addr_param.common_param.pda_list = pda_list;
	ncl_cmd->addr_param.common_param.info_list = info;

	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;

	ncl_cmd_submit(ncl_cmd);

	ret = true;
	if (ncl_cmd->status != 0) {
		if (info_list)
			*info_list = info;

		ret = false;
	} else {
		share_free(info);
	}
	share_free(pda_list);
	share_free(ncl_cmd);
	return ret;
}

fast_code u16 ncl_read_dus(pda_t *pda_list, u32 cnt, bm_pl_t *bm_pl_list,
	struct info_param_t *info_list, nal_pb_type_t type, bool remap, bool host)
{
	struct ncl_cmd_t *ncl_cmd;
	u16 status;

	ptr_chk(pda_list);
	ptr_chk(bm_pl_list);
	ptr_chk(info_list);
	ncl_cmd = share_malloc(sizeof(*ncl_cmd));

	if (remap)
		ncl_sync_get_remap_list(pda_list, cnt);

	ncl_cmd->addr_param.common_param.list_len = cnt;
	ncl_cmd->addr_param.common_param.pda_list = pda_list;
	ncl_cmd->addr_param.common_param.info_list = info_list;

	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_TAG_EXT_FLAG;
	if (type == NAL_PB_TYPE_SLC)
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	else if (type == NAL_PB_TYPE_XLC)
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;

	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->user_tag_list = bm_pl_list;
	ncl_cmd->du_format_no = host ? host_du_fmt : DU_4K_DEFAULT_MODE;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->completion = NULL;
	ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
	ncl_cmd->status = 0;
    #if NCL_FW_RETRY
	ncl_cmd->retry_step = 0;
    #endif
	ncl_cmd_submit(ncl_cmd);

	status = ncl_cmd->status;

    #if NCL_FW_RETRY
	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
    if (status)
    {
        if (ncl_cmd->retry_step == default_read)
        {
	        ftl_apl_trace(LOG_ALW, 0xec1e, "frb_rd_err_handling, ncl_cmd: 0x%x", ncl_cmd);
            rd_err_handling(ncl_cmd);
            status = ncl_cmd->status;
        }
    }
    #endif
    
	share_free(ncl_cmd);
	return status;
}

fast_code nal_status_t ncl_read_one_page(pda_t pda, void *ptr, u32 meta_idx)
{
	bm_pl_t *bm_pl_list;
	pda_t *pda_list;
	struct info_param_t *info_list;
	u32 i = 0;
	u8 *p = (u8*) ptr;
	nal_status_t ret;
	u32 status;
	log_level_t old = ipc_api_log_level_chg(LOG_ALW);
	ncl_page_res_t *page_res;

	page_res = share_malloc(sizeof(*page_res));
	bm_pl_list = page_res->bm_pl;
	pda_list = page_res->pda;
	info_list = page_res->info;

	memset(info_list, 0, DU_CNT_PER_PAGE * sizeof(*info_list));

	do {
		dtag_t dtag;

		dtag = mem2dtag(p);
		bm_pl_list[i].pl.dtag = dtag.dtag;
		bm_pl_list[i].pl.nvm_cmd_id = meta_idx + i; // preassign use nvme cmd id as meta idx
		bm_pl_list[i].pl.du_ofst = i;
		bm_pl_list[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
		if (dtag.b.in_ddr)
			bm_pl_list[i].pl.type_ctrl |= META_DDR_IDX;
		else
			bm_pl_list[i].pl.type_ctrl |= META_SRAM_IDX;

		pda_list[i] = pda + i;
		info_list[i].pb_type = NAL_PB_TYPE_SLC;
		p += NAND_DU_SIZE;
	} while (++i < DU_CNT_PER_PAGE);

	status = ncl_read_dus(pda_list, DU_CNT_PER_PAGE, bm_pl_list,
			info_list, NAL_PB_TYPE_SLC, false, false);
    //ftl_apl_trace(LOG_ERR, 0, "[FRB] read one page ncl_cmd status = %d\n", status);
	i = 0;
	ret = ficu_err_good;
	if (status != 0) {
		do {
            //ftl_apl_trace(LOG_ERR, 0, "[FRB] one page finst status: %d, pda: 0x%x\n", info_list[i].status, pda_list[i]);
			if (info_list[i].status > ret) {
				ret = info_list[i].status;
			}
		} while (++i < DU_CNT_PER_PAGE);
	}
	ipc_api_log_level_chg(old);
	share_free(page_res);
	return ret;
}

fast_code bool ncl_write_pages(pda_t *pda_list,
		struct info_param_t *info_list, u32 list_len, bm_pl_t *bm_pl,
		u32 flags)
{
	bool ret = true;
	struct ncl_cmd_t *ncl_cmd;

	ptr_chk(pda_list);
	ptr_chk(bm_pl);
	ptr_chk(info_list);
	ncl_cmd = share_malloc(sizeof(*ncl_cmd));

	ncl_cmd->addr_param.common_param.info_list = info_list;
	ncl_cmd->addr_param.common_param.list_len = list_len;
	ncl_cmd->addr_param.common_param.pda_list = pda_list;

	ncl_sync_get_remap_list(pda_list, list_len);

	memset(info_list, 0, sizeof(struct info_param_t) * list_len);

	ncl_cmd->caller_priv = NULL;
	ncl_cmd->completion = NULL;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG | flags;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;
	ncl_cmd->user_tag_list = bm_pl;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->status = 0;

	ncl_cmd_submit(ncl_cmd);

	if (ncl_cmd->status != 0)
		ret = false;

	share_free(ncl_cmd);

	return ret;
}

/*! @} */
